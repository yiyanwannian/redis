# Redis `syncWithMaster` 函数的数据传输实现详解

`syncWithMaster` 是Redis复制功能中的核心函数，负责从节点与主节点之间的同步过程。本文从源码角度详细分析这个函数中的数据传输实现机制。

## 1. `syncWithMaster` 函数概述

`syncWithMaster` 函数位于 `src/replication.c` 文件中，是从节点与主节点建立同步连接的核心处理函数。它作为一个事件处理器被注册到Redis的事件循环中，处理与主节点的连接、协议交互和数据传输。

```c
void syncWithMaster(aeEventLoop *el, int fd, void *privdata, int mask) {
    // 函数实现...
}
```

## 2. 连接建立与握手阶段

首先，让我们看看连接建立和初始握手是如何实现的：

```c
void syncWithMaster(aeEventLoop *el, int fd, void *privdata, int mask) {
    char tmpfile[256], *err = NULL;
    int dfd = -1, maxtries = 5;
    int sockerr = 0, psync_result;
    socklen_t errlen = sizeof(sockerr);
    UNUSED(el);
    UNUSED(privdata);
    UNUSED(mask);

    // 检查套接字错误
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &errlen) == -1)
        sockerr = errno;
    if (sockerr) {
        serverLog(LL_WARNING,"Error condition on socket for SYNC: %s",
            strerror(sockerr));
        goto error;
    }

    // 发送PING命令
    if (server.repl_state == REPL_STATE_CONNECTING) {
        serverLog(LL_NOTICE,"Non blocking connect for SYNC fired the event.");
        // 删除读事件处理器，因为我们只关心写事件
        aeDeleteFileEvent(server.el,fd,AE_READABLE);
        server.repl_state = REPL_STATE_RECEIVE_PONG;
        // 发送PING命令
        if (syncWrite(fd,"PING\r\n",6,server.repl_syncio_timeout*1000) == -1) {
            serverLog(LL_WARNING,"Writing initial PING failed: %s",
                strerror(errno));
            goto error;
        }
        return;
    }
    
    // 接收PONG响应
    if (server.repl_state == REPL_STATE_RECEIVE_PONG) {
        char buf[1024];
        
        // 读取响应
        if (syncReadLine(fd,buf,sizeof(buf),server.repl_syncio_timeout*1000) == -1) {
            serverLog(LL_WARNING,"Reading PONG failed: %s", strerror(errno));
            goto error;
        }
        
        // 检查响应是否为PONG
        if (strcmp(buf,"+PONG\r\n")) {
            serverLog(LL_WARNING,"Master replied with wrong ping reply.");
            goto error;
        } else {
            serverLog(LL_NOTICE,"Master replied to PING, replication can continue...");
        }
        
        // 进入下一阶段
        server.repl_state = REPL_STATE_SEND_AUTH;
    }
    
    // 认证阶段...
    // 配置阶段...
    
    // 其余代码...
}
```

这部分代码处理了初始连接和PING-PONG握手，使用了 `syncWrite` 和 `syncReadLine` 函数进行同步I/O操作。

## 3. PSYNC协商阶段

接下来是PSYNC命令的发送和处理，这决定了是进行全量同步还是部分同步：

```c
// 发送PSYNC命令
if (server.repl_state == REPL_STATE_SEND_PSYNC) {
    char buf[128];
    char psync_replid[CONFIG_RUN_ID_SIZE+1];
    long long psync_offset = -1;
    
    // 确定PSYNC参数
    if (server.master_replid[0] != '\0') {
        // 尝试部分同步
        memcpy(psync_replid,server.master_replid,sizeof(psync_replid));
        psync_offset = server.master_repl_offset+1;
    } else {
        // 全量同步
        memcpy(psync_replid,"?",2);
        psync_offset = -1;
    }
    
    // 构建PSYNC命令
    snprintf(buf,sizeof(buf),"PSYNC %s %lld\r\n",
             psync_replid, psync_offset);
    
    // 发送PSYNC命令
    if (syncWrite(fd,buf,strlen(buf),server.repl_syncio_timeout*1000) == -1) {
        serverLog(LL_WARNING,"Sending PSYNC to master: %s",
            strerror(errno));
        goto error;
    }
    
    server.repl_state = REPL_STATE_RECEIVE_PSYNC;
    return;
}

// 处理PSYNC响应
if (server.repl_state == REPL_STATE_RECEIVE_PSYNC) {
    char buf[128];
    
    // 读取响应
    psync_result = slaveTryPartialResynchronization(fd,0);
    
    if (psync_result == PSYNC_CONTINUE) {
        // 部分同步成功
        serverLog(LL_NOTICE, "MASTER <-> REPLICA sync: Master accepted a Partial Resynchronization.");
        return;
    } else if (psync_result == PSYNC_FULLRESYNC) {
        // 需要全量同步
        server.repl_state = REPL_STATE_TRANSFER;
        serverLog(LL_NOTICE, "MASTER <-> REPLICA sync: Preparing for full resync...");
        
        // 准备接收RDB文件
        // ...
    } else {
        // PSYNC失败
        serverLog(LL_WARNING, "MASTER <-> REPLICA sync: Error trying to perform a partial resynchronization.");
        goto error;
    }
}
```

`slaveTryPartialResynchronization` 函数解析主节点的PSYNC响应，决定是进行部分同步还是全量同步。

## 4. 全量同步的数据传输实现

当需要全量同步时，从节点需要接收主节点发送的RDB文件：

```c
// 准备接收RDB文件
snprintf(tmpfile,sizeof(tmpfile),
         "temp-%d.%ld.rdb",(int)server.unixtime,(long int)getpid());
dfd = open(tmpfile,O_CREAT|O_WRONLY|O_EXCL,0644);
if (dfd == -1) {
    serverLog(LL_WARNING,"Opening the temp file needed for MASTER <-> REPLICA synchronization: %s",strerror(errno));
    goto error;
}

// 设置RDB传输状态
server.repl_transfer_fd = dfd;
server.repl_transfer_size = -1;
server.repl_transfer_read = 0;
server.repl_transfer_last_fsync_off = 0;
server.repl_transfer_tmpfile = zstrdup(tmpfile);


// 修改文件事件处理器，准备接收RDB数据
aeDeleteFileEvent(server.el,fd,AE_READABLE);
if (aeCreateFileEvent(server.el, fd, AE_READABLE, readSyncBulkPayload, NULL)
    == AE_ERR)
{
    serverLog(LL_WARNING,
        "Can't create readable event for SYNC: %s (fd=%d)",
        strerror(errno), fd);
    goto error;
}
```

这段代码创建了一个临时文件用于存储RDB数据，并注册了一个新的事件处理器 `readSyncBulkPayload` 来接收数据。

## 5. `readSyncBulkPayload` 函数 - RDB文件传输的核心

`readSyncBulkPayload` 函数是实际接收RDB文件数据的核心：

```c
void readSyncBulkPayload(aeEventLoop *el, int fd, void *privdata, int mask) {
    char buf[4096];
    ssize_t nread, readlen;
    off_t left;
    UNUSED(el);
    UNUSED(privdata);
    UNUSED(mask);

    // 如果还不知道RDB文件大小，先读取大小信息
    if (server.repl_transfer_size == -1) {
        // 读取RDB文件大小
        char buf[1024];
        
        if (syncReadLine(fd,buf,sizeof(buf),server.repl_syncio_timeout*1000) == -1) {
            serverLog(LL_WARNING,
                "I/O error reading bulk count from MASTER: %s",
                strerror(errno));
            goto error;
        }
        
        // 解析RDB文件大小
        if (buf[0] == '-') {
            // 错误响应
            serverLog(LL_WARNING,
                "MASTER aborted replication with an error: %s",
                buf+1);
            goto error;
        } else if (buf[0] == '$') {
            // 正常响应，格式为 $<size>\r\n
            server.repl_transfer_size = strtol(buf+1,NULL,10);
            serverLog(LL_NOTICE,
                "MASTER <-> REPLICA sync: receiving %lld bytes from master",
                (long long) server.repl_transfer_size);
            return;
        } else {
            // 未知响应
            serverLog(LL_WARNING,
                "Bad protocol from MASTER, the first byte is not '$' "
                "(we received '%s')", buf);
            goto error;
        }
    }

    // 计算本次应该读取的数据量
    left = server.repl_transfer_size - server.repl_transfer_read;
    readlen = (left < (signed)sizeof(buf)) ? left : (signed)sizeof(buf);
    
    // 读取数据
    nread = read(fd,buf,readlen);
    if (nread <= 0) {
        // 读取错误或连接关闭
        serverLog(LL_WARNING,"I/O error trying to sync with MASTER: %s",
            (nread == -1) ? strerror(errno) : "connection lost");
        cancelReplicationHandshake();
        return;
    }
    
    // 写入临时文件
    if (write(server.repl_transfer_fd,buf,nread) != nread) {
        serverLog(LL_WARNING,
            "Write error or short write writing to the DB dump file needed "
            "for MASTER <-> REPLICA synchronization: %s",
            strerror(errno));
        goto error;
    }
    
    // 更新已读取的数据量
    server.repl_transfer_read += nread;
    
    // 定期执行fsync，确保数据写入磁盘
    if (server.repl_transfer_read >=
        server.repl_transfer_last_fsync_off + server.repl_transfer_fsync_step)
    {
        off_t sync_size = server.repl_transfer_read -
                         server.repl_transfer_last_fsync_off;
        rdb_fsync_range(server.repl_transfer_fd,
            server.repl_transfer_last_fsync_off, sync_size);
        server.repl_transfer_last_fsync_off += sync_size;
    }
    
    // 检查是否接收完成
    if (server.repl_transfer_read == server.repl_transfer_size) {
        // 关闭文件
        if (close(server.repl_transfer_fd) == -1) {
            serverLog(LL_WARNING,"Error closing the temp file during MASTER <-> REPLICA synchronization: %s", strerror(errno));
            goto error;
        }
        
        // 重命名临时文件为正式RDB文件
        if (rename(server.repl_transfer_tmpfile,server.rdb_filename) == -1) {
            serverLog(LL_WARNING,"Error renaming the temp file into dump.rdb in MASTER <-> REPLICA synchronization: %s", strerror(errno));
            goto error;
        }
        
        // 加载RDB文件
        serverLog(LL_NOTICE, "MASTER <-> REPLICA sync: Loading DB in memory");
        rdbSaveInfo rsi = RDB_SAVE_INFO_INIT;
        if (rdbLoad(server.rdb_filename,&rsi) != C_OK) {
            serverLog(LL_WARNING,"Failed trying to load the MASTER synchronization DB from disk");
            goto error;
        }
        
        // 设置复制状态
        // ...
        
        // 清理资源
        zfree(server.repl_transfer_tmpfile);
        server.repl_transfer_tmpfile = NULL;
        server.repl_transfer_fd = -1;
        
        // 修改文件事件处理器
        aeDeleteFileEvent(server.el,fd,AE_READABLE);
        if (aeCreateFileEvent(server.el, fd, AE_READABLE, readQueryFromClient, server.master) == AE_ERR) {
            serverLog(LL_WARNING,"Can't create readable event for MASTER: %s (fd=%d)", strerror(errno), fd);
            goto error;
        }
        
        // 完成同步
        serverLog(LL_NOTICE, "MASTER <-> REPLICA sync: Finished with success");
    }
    
    return;

error:
    // 错误处理...
}
```

这个函数实现了RDB文件的接收过程：
1. 首先读取RDB文件的大小信息
2. 然后分块读取RDB文件数据
3. 将数据写入临时文件
4. 定期执行fsync确保数据持久化
5. 接收完成后，重命名临时文件并加载RDB数据

## 6. 部分同步的数据传输实现

当可以进行部分同步时，数据传输通过普通的命令传播机制进行：

```c
// 在slaveTryPartialResynchronization函数中
if (strncmp(reply,"+CONTINUE",9) == 0) {
    // 部分同步成功
    
    // 解析主节点的复制偏移量
    char *p = reply + 10;
    server.master_initial_offset = strtoll(p, NULL, 10);
    
    // 设置复制状态
    server.repl_state = REPL_STATE_CONNECTED;
    
    // 修改文件事件处理器，准备接收命令
    aeDeleteFileEvent(server.el,fd,AE_READABLE);
    if (aeCreateFileEvent(server.el, fd, AE_READABLE, readQueryFromClient, server.master) == AE_ERR) {
        serverLog(LL_WARNING,"Can't create readable event for MASTER");
        goto error;
    }
    
    // 记录日志
    serverLog(LL_NOTICE,
        "MASTER <-> REPLICA partial resynchronization: success");
    
    // 返回部分同步成功
    return PSYNC_CONTINUE;
}
```

部分同步成功后，从节点会注册 `readQueryFromClient` 事件处理器，通过正常的命令接收机制接收主节点发送的写命令。

## 7. 数据传输中的关键辅助函数

### 7.1 `syncWrite` - 同步写入函数

```c
int syncWrite(int fd, char *ptr, ssize_t size, long long timeout) {
    ssize_t nwritten, ret = size;
    long long start = mstime();
    
    // 循环写入直到完成或超时
    while(size) {
        // 检查超时
        if (timeout && (mstime()-start) > timeout) {
            errno = ETIMEDOUT;
            return -1;
        }
        
        // 写入数据
        nwritten = write(fd,ptr,size);
        if (nwritten == -1) {
            // 处理EAGAIN错误
            if (errno == EAGAIN) {
                // 等待套接字可写
                struct pollfd pfd;
                pfd.fd = fd;
                pfd.events = POLLOUT;
                
                int retval = poll(&pfd, 1, timeout ? 
                                 ((timeout-(mstime()-start))+1) : -1);
                if (retval == 0) {
                    // 超时
                    errno = ETIMEDOUT;
                    return -1;
                }
                if (retval == -1) {
                    // 错误
                    return -1;
                }
                continue;
            }
            // 其他错误
            return -1;
        }
        
        // 更新指针和剩余大小
        ptr += nwritten;
        size -= nwritten;
    }
    
    return ret;
}
```

`syncWrite` 函数实现了带超时的同步写入，它会循环尝试写入直到所有数据都写入完成或发生错误/超时。

### 7.2 `syncReadLine` - 同步读取一行数据

```c
int syncReadLine(int fd, char *ptr, ssize_t size, long long timeout) {
    ssize_t nread = 0;
    long long start = mstime();
    
    // 循环读取直到找到换行符或达到缓冲区大小
    while(nread < size) {
        // 检查超时
        if (timeout && (mstime()-start) > timeout) {
            errno = ETIMEDOUT;
            return -1;
        }
        
        // 读取一个字节
        char c;
        ssize_t ret = read(fd,&c,1);
        if (ret == -1) {
            // 处理EAGAIN错误
            if (errno == EAGAIN) {
                // 等待套接字可读
                struct pollfd pfd;
                pfd.fd = fd;
                pfd.events = POLLIN;
                
                int retval = poll(&pfd, 1, timeout ? 
                                 ((timeout-(mstime()-start))+1) : -1);
                if (retval == 0) {
                    // 超时
                    errno = ETIMEDOUT;
                    return -1;
                }
                if (retval == -1) {
                    // 错误
                    return -1;
                }
                continue;
            }
            // 其他错误
            return -1;
        }
        if (ret == 0) {
            // 连接关闭
            return 0;
        }
        
        // 存储字符
        ptr[nread] = c;
        nread++;
        
        // 检查是否读取到完整行
        if (nread >= 2 && ptr[nread-2] == '\r' && ptr[nread-1] == '\n') {
            break;
        }
    }
    
    return nread;
}
```

`syncReadLine` 函数实现了带超时的行读取，它会循环读取字符直到找到 `\r\n` 行结束符或达到缓冲区大小限制。

## 8. 数据传输的性能优化

Redis在数据传输过程中采用了多种性能优化措施：

1. **分块传输**：RDB文件以固定大小的块进行传输，避免一次性分配过大的内存
2. **异步I/O**：使用事件驱动模型处理网络I/O，避免阻塞
3. **定期fsync**：在接收RDB文件时定期执行fsync，平衡性能和数据安全
4. **直接文件I/O**：数据直接写入文件，避免在内存中缓存整个RDB文件
5. **超时控制**：所有网络操作都有超时控制，防止无限等待

## 9. 总结

`syncWithMaster` 函数及其相关函数实现了Redis从节点与主节点之间的数据传输过程，主要包括：

1. **连接建立**：通过TCP连接连接到主节点
2. **握手协商**：通过PING-PONG和PSYNC命令协商同步方式
3. **数据传输**：
   - 全量同步：接收RDB文件并加载
   - 部分同步：接收增量命令并执行
4. **状态转换**：从初始连接到完成同步的状态管理

这个实现展示了Redis在处理分布式数据同步时的精巧设计，既保证了数据一致性，又兼顾了性能和可靠性。
