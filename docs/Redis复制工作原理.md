# Redis 复制（Replication）工作原理详解

Redis 复制是 Redis 高可用性和数据冗余的核心机制，它允许多个 Redis 服务器（从节点）拥有主节点数据的精确副本。本文将详细介绍 Redis 复制的工作原理、实现细节和实际应用场景。

## 1. 复制的基本概念

Redis 复制采用主从（master-slave）模型：

- **主节点（Master）**：接收写操作，将数据变更同步给从节点
- **从节点（Slave/Replica）**：接收主节点的数据更新，默认情况下只提供读服务

一个主节点可以有多个从节点，而一个从节点只能有一个主节点。从节点也可以拥有自己的从节点，形成复制链。

## 2. 复制的建立过程

Redis 复制的建立过程包括以下几个步骤：

### 2.1 复制初始化

当一个 Redis 实例被配置为另一个实例的从节点时（通过 `SLAVEOF` 或 `REPLICAOF` 命令或配置文件），复制过程开始初始化：

```
REPLICAOF master_ip master_port
```

### 2.2 连接建立

从节点会尝试与主节点建立 TCP 连接。如果连接失败，它会定期重试，直到连接成功。

### 2.3 握手和身份验证

连接建立后，从节点会发送 `PING` 命令检测连接是否正常，然后进行身份验证（如果主节点配置了密码）：

```
AUTH master_password
```

### 2.4 复制同步

握手成功后，从节点会发送 `PSYNC` 命令请求同步数据。根据情况，可能会进行全量同步或部分同步：

```
PSYNC replication_id offset
```

## 3. 全量同步（Full Resynchronization）

全量同步在以下情况下发生：

- 从节点第一次连接到主节点
- 复制中断时间过长，导致复制积压缓冲区中的数据已被覆盖
- 主节点没有足够的信息来支持部分同步

全量同步的过程如下：

### 3.1 主节点响应

主节点收到 `PSYNC` 命令后，如果需要进行全量同步，会响应：

```
+FULLRESYNC replication_id offset
```

### 3.2 RDB 文件生成

主节点会执行 `BGSAVE` 命令，在后台生成 RDB 文件：

```c
// 在主节点上
int masterTryPartialResynchronization(client *c) {
    // ...
    if (需要全量同步) {
        // 通知从节点进行全量同步
        addReplyBulkFormat(c,"+FULLRESYNC %s %lld\r\n",
                           server.replid,server.master_repl_offset);
        // 开始后台保存 RDB 文件
        startBgsaveForReplication(c->slave_capa);
    }
    // ...
}
```

### 3.3 RDB 文件传输

RDB 文件生成后，主节点会将其发送给从节点：

```c
// 在主节点上
void sendBulkToSlave(client *slave) {
    // ...
    // 打开 RDB 文件
    fd = open(server.rdb_filename,O_RDONLY);
    // 设置从节点状态为正在发送 RDB
    slave->replstate = SLAVE_STATE_SEND_BULK;
    // 创建文件事件，准备发送 RDB 文件
    if (aeCreateFileEvent(server.el, slave->fd, AE_WRITABLE,
        sendBulkToSlaveCB, slave) == AE_ERR)
    {
        fclose(slave->repldbfd);
        slave->repldbfd = NULL;
        slave->replstate = SLAVE_STATE_ONLINE;
        return;
    }
    // ...
}
```

### 3.4 从节点加载 RDB

从节点接收并加载 RDB 文件，完成初始数据同步：

```c
// 在从节点上
void readSyncBulkPayload(aeEventLoop *el, int fd, void *privdata, int mask) {
    // ...
    // 接收 RDB 文件数据
    nread = read(fd,buf,PROTO_IOBUF_LEN);
    // 将数据写入临时文件
    if (write(server.repl_transfer_fd,buf,nread) != nread) {
        // 处理错误...
    }
    // ...
    // 检查是否接收完成
    if (server.repl_transfer_read == server.repl_transfer_size) {
        // 加载 RDB 文件
        replicationCreateMasterClient(server.repl_transfer_s,rsi.repl_stream_db);
        // ...
    }
    // ...
}
```

### 3.5 命令传播开始

RDB 文件加载完成后，主节点会开始将新的写命令传播给从节点。

## 4. 部分同步（Partial Resynchronization）

部分同步是 Redis 2.8 引入的特性，用于在网络短暂中断后恢复复制，避免全量同步的开销。

### 4.1 复制积压缓冲区

主节点维护一个固定大小的环形缓冲区（复制积压缓冲区），用于存储最近执行的写命令：

```c
// 在主节点上
void createReplicationBacklog(void) {
    server.repl_backlog = zmalloc(server.repl_backlog_size);
    server.repl_backlog_histlen = 0;
    server.repl_backlog_idx = 0;
    server.repl_backlog_off = 0;
}
```

### 4.2 复制偏移量

主节点和从节点都会维护一个复制偏移量，用于跟踪复制进度：

- 主节点：记录已发送的数据量
- 从节点：记录已接收的数据量

### 4.3 部分同步过程

当从节点重新连接到主节点时，会发送 `PSYNC` 命令，包含复制 ID 和偏移量：

```
PSYNC replication_id offset
```

如果主节点的复制积压缓冲区中包含从节点请求的偏移量之后的所有数据，主节点会响应：

```
+CONTINUE offset
```

然后发送从节点缺失的命令：

```c
// 在主节点上
int masterTryPartialResynchronization(client *c) {
    // ...
    // 检查是否可以进行部分同步
    if (可以部分同步) {
        // 通知从节点继续同步
        addReplyBulkFormat(c,"+CONTINUE %s\r\n",server.replid);
        // 发送从节点缺失的命令
        psync_len = addReplyReplicationBacklog(c,psync_offset);
        // ...
    }
    // ...
}
```

## 5. 命令传播（Command Propagation）

在初始同步完成后，主节点会将所有修改数据的命令实时传播给从节点。

### 5.1 命令捕获

主节点在执行写命令后，会将命令添加到复制积压缓冲区，并传播给所有从节点：

```c
// 在主节点上
void propagate(struct redisCommand *cmd, int dbid, robj **argv, int argc,
               int flags)
{
    // 将命令添加到 AOF 缓冲区（如果启用了 AOF）
    if (flags & PROPAGATE_AOF)
        feedAppendOnlyFile(cmd,dbid,argv,argc);
    // 将命令传播给从节点
    if (flags & PROPAGATE_REPL)
        replicationFeedSlaves(server.slaves,dbid,argv,argc);
}
```

### 5.2 命令接收和执行

从节点接收到命令后，会执行这些命令，保持数据与主节点一致：

```c
// 在从节点上
void readQueryFromClient(connection *conn) {
    // ...
    // 如果是从节点，处理来自主节点的命令
    if (c->flags & CLIENT_SLAVE && !(c->flags & CLIENT_MONITOR)) {
        // 处理主节点发来的命令
        processCommandFromMaster(c);
    }
    // ...
}
```

## 6. 心跳机制

为了维持复制连接的稳定性，主从节点之间会定期交换心跳信息：

### 6.1 主节点发送 PING

主节点会定期（由 `repl-ping-slave-period` 配置，默认 10 秒）向从节点发送 PING 命令：

```c
// 在主节点上
void replicationCron(void) {
    // ...
    // 检查是否需要向从节点发送 PING
    if (server.masterhost == NULL &&
        server.repl_ping_slave_period &&
        server.unixtime - server.repl_ping_slave_period >= ping_time)
    {
        // 向所有从节点发送 PING
        replicationFeedSlaves(server.slaves, -1, ping_argv, 1);
        ping_time = server.unixtime;
    }
    // ...
}
```

### 6.2 从节点发送 REPLCONF ACK

从节点会定期（默认每秒）向主节点发送 `REPLCONF ACK` 命令，报告复制偏移量：

```c
// 在从节点上
void replicationCron(void) {
    // ...
    // 如果是从节点，向主节点发送 ACK
    if (server.masterhost && server.master &&
        !(server.master->flags & CLIENT_PRE_PSYNC))
    {
        // 发送 REPLCONF ACK 命令
        replicationSendAck();
    }
    // ...
}
```

## 7. 实际应用示例

### 7.1 基本复制设置

假设我们有两个 Redis 实例，一个作为主节点（端口 6379），一个作为从节点（端口 6380）：

**主节点配置（redis-6379.conf）**：
```
port 6379
```

**从节点配置（redis-6380.conf）**：
```
port 6380
replicaof 127.0.0.1 6379
```

启动两个实例：
```bash
redis-server redis-6379.conf
redis-server redis-6380.conf
```

### 7.2 动态设置复制

也可以在运行时通过命令设置复制关系：

```bash
# 在从节点上执行
redis-cli -p 6380
> REPLICAOF 127.0.0.1 6379
OK
```

### 7.3 监控复制状态

可以通过 `INFO replication` 命令查看复制状态：

```bash
# 在主节点上执行
redis-cli -p 6379
> INFO replication
# Replication
role:master
connected_slaves:1
slave0:ip=127.0.0.1,port=6380,state=online,offset=1234,lag=0

# 在从节点上执行
redis-cli -p 6380
> INFO replication
# Replication
role:slave
master_host:127.0.0.1
master_port:6379
master_link_status:up
master_last_io_seconds_ago:0
master_sync_in_progress:0
slave_repl_offset:1234
```

### 7.4 复制链示例

Redis 支持复制链，即从节点可以有自己的从节点：

```
A (主节点) -> B (从节点/中间主节点) -> C (从节点)
```

配置如下：

**节点 A（端口 6379）**：
```
port 6379
```

**节点 B（端口 6380）**：
```
port 6380
replicaof 127.0.0.1 6379
```

**节点 C（端口 6381）**：
```
port 6381
replicaof 127.0.0.1 6380
```

### 7.5 数据写入测试

在主节点写入数据，然后在从节点验证：

```bash
# 在主节点上写入数据
redis-cli -p 6379
> SET key1 "value1"
OK

# 在从节点上读取数据
redis-cli -p 6380
> GET key1
"value1"
```

## 8. 复制的内部实现细节

### 8.1 RIO 系统在复制中的应用

Redis 使用 RIO 系统处理复制过程中的数据传输：

```c
// 在主节点上，使用 RIO 系统发送 RDB 文件
void masterSendBulkToSlave(aeEventLoop *el, int fd, void *privdata, int mask) {
    client *slave = privdata;
    UNUSED(el);
    UNUSED(mask);
    char buf[PROTO_IOBUF_LEN];
    ssize_t nwritten, buflen;

    // 从 RDB 文件读取数据
    buflen = read(slave->repldbfd,buf,PROTO_IOBUF_LEN);
    if (buflen <= 0) {
        // 处理错误或文件结束...
        return;
    }

    // 使用 RIO 系统写入数据到从节点
    if ((nwritten = connWrite(slave->conn,buf,buflen)) == -1) {
        // 处理错误...
        return;
    }

    // 更新统计信息
    slave->repldboff += nwritten;

    // 检查是否发送完成
    if (slave->repldboff == slave->repldbsize) {
        // RDB 文件发送完成，关闭文件
        close(slave->repldbfd);
        slave->repldbfd = -1;
        // 更新从节点状态
        slave->replstate = SLAVE_STATE_ONLINE;
        // ...
    }
}
```

### 8.2 复制积压缓冲区的实现

复制积压缓冲区是一个环形缓冲区，用于存储最近执行的写命令：

```c
// 将命令添加到复制积压缓冲区
void feedReplicationBacklog(void *ptr, size_t len) {
    unsigned char *p = ptr;

    // 计算可用空间
    size_t available = server.repl_backlog_size - server.repl_backlog_idx;

    // 如果当前位置到缓冲区末尾的空间不足，分两次写入
    if (len > available) {
        // 先写入 available 字节
        memcpy(server.repl_backlog + server.repl_backlog_idx, p, available);
        // 剩余部分写入缓冲区开头
        memcpy(server.repl_backlog, p + available, len - available);
        server.repl_backlog_idx = len - available;
    } else {
        // 直接写入
        memcpy(server.repl_backlog + server.repl_backlog_idx, p, len);
        server.repl_backlog_idx += len;
        // 如果到达缓冲区末尾，重置索引
        if (server.repl_backlog_idx == server.repl_backlog_size)
            server.repl_backlog_idx = 0;
    }

    // 更新历史长度和偏移量
    server.repl_backlog_histlen += len;
    server.master_repl_offset += len;
}
```

## 9. 复制的优缺点

### 9.1 优点

1. **读写分离**：可以将读请求分发到从节点，减轻主节点负担
2. **数据冗余**：提供了数据的多个副本，增强了数据安全性
3. **高可用性**：结合哨兵或集群，可以实现自动故障转移
4. **数据备份**：从节点可以用于备份，不影响主节点性能
5. **地理分布**：可以将从节点部署在不同地理位置，提供就近服务

### 9.2 缺点

1. **复制延迟**：从节点数据可能落后于主节点
2. **网络开销**：复制过程会消耗网络带宽
3. **全量同步开销**：全量同步会增加主节点负载
4. **主节点单点故障**：如果没有配合哨兵或集群，主节点故障会导致写服务不可用
5. **一致性问题**：Redis 复制是异步的，不保证强一致性

## 10. 最佳实践

1. **合理配置复制积压缓冲区大小**：根据网络质量和复制中断可能的最长时间，设置足够大的复制积压缓冲区，避免不必要的全量同步
   ```
   repl-backlog-size 100mb
   ```

2. **启用磁盘持久化**：在从节点上启用 RDB 或 AOF 持久化，防止从节点重启后需要全量同步
   ```
   appendonly yes
   ```

3. **配置合理的超时参数**：根据网络环境调整复制超时参数
   ```
   repl-timeout 60
   ```

4. **监控复制延迟**：定期检查主从节点的复制偏移量差异，监控复制延迟
   ```
   redis-cli -p 6379 INFO replication | grep offset
   ```

5. **结合哨兵或集群使用**：使用 Redis Sentinel 或 Redis Cluster 提供自动故障转移能力

## 11. 总结

Redis 复制是 Redis 高可用架构的基础，通过主从复制机制，Redis 实现了数据的冗余备份和读写分离。复制过程包括全量同步和部分同步两种方式，通过复制积压缓冲区和复制偏移量等机制，Redis 实现了高效的数据同步。

在实际应用中，Redis 复制通常与哨兵或集群配合使用，形成完整的高可用解决方案。理解 Redis 复制的工作原理和实现细节，有助于我们更好地设计和维护 Redis 高可用系统。

## 12. 复制系统中的数据结构实现

为了更好地理解Redis复制的内部实现，我们来看看复制系统中使用的关键数据结构。

### 12.1 listNode数据结构

在Redis的复制系统中，`listNode`是一个重要的数据结构，它是Redis双向链表实现的核心组件。下面详细解释它在`replication.c`中的作用。

#### 12.1.1 listNode的基本定义

`listNode`定义在`adlist.h`文件中，是Redis双向链表的节点结构：

```c
typedef struct listNode {
    struct listNode *prev;  // 前一个节点的指针
    struct listNode *next;  // 后一个节点的指针
    void *value;           // 节点存储的值
} listNode;
```

#### 12.1.2 在复制系统中的应用

在`replication.c`文件中，`listNode`主要用于以下几个方面：

##### a) 管理从节点列表

主节点使用链表来管理所有连接的从节点：

```c
// 在server.h中定义
struct redisServer {
    // ...
    list *slaves;          // 从节点列表
    // ...
};
```

每个从节点作为一个客户端连接，被封装为一个`client`结构，然后作为`listNode`的值存储在`slaves`链表中。

##### b) 管理等待同步的从节点

当主节点执行BGSAVE为复制准备RDB文件时，会将等待同步的从节点放入一个等待队列：

```c
// 在replication.c中
list *clients_waiting_acks;  // 等待确认的客户端列表
```

##### c) 遍历从节点进行操作

在复制过程中，经常需要遍历从节点列表进行操作，例如：

```c
// 向所有从节点发送命令
void replicationFeedSlaves(list *slaves, int dictid, robj **argv, int argc) {
    listNode *ln;
    listIter li;

    // 初始化链表迭代器
    listRewind(slaves, &li);

    // 遍历所有从节点
    while ((ln = listNext(&li))) {
        client *slave = ln->value;  // 获取从节点客户端

        // 对从节点进行操作...
    }
}
```

##### d) 添加和删除从节点

当新的从节点连接或断开连接时，需要对链表进行操作：

```c
// 添加新的从节点
void attachReplicationSlaveToMaster(client *slave) {
    // 将从节点添加到主节点的从节点列表
    listAddNodeTail(server.slaves, slave);
}

// 删除从节点
void detachReplicationSlaveFromMaster(client *slave) {
    // 从主节点的从节点列表中删除从节点
    listDelNode(server.slaves, listSearchKey(server.slaves, slave));
}
```

#### 12.1.3 具体例子

以下是`replication.c`中使用`listNode`的一个具体例子：

```c
// 在主节点上，向所有从节点传播写命令
void replicationFeedSlaves(list *slaves, int dictid, robj **argv, int argc) {
    listNode *ln;
    listIter li;

    // 如果没有从节点，直接返回
    if (listLength(slaves) == 0) return;

    // 初始化链表迭代器
    listRewind(slaves, &li);

    // 遍历所有从节点
    while ((ln = listNext(&li))) {
        // 获取从节点客户端
        client *slave = ln->value;

        // 检查从节点状态
        if (slave->replstate != SLAVE_STATE_ONLINE) continue;

        // 向从节点发送命令
        // ...
    }
}
```

在这个例子中，`listNode *ln`用于在遍历过程中指向当前处理的链表节点，通过`ln->value`可以获取到存储在节点中的从节点客户端对象。

通过以上分析，我们可以看到在Redis的复制系统中，`listNode`是实现双向链表的基础结构，主要用于：

1. 存储和管理从节点列表
2. 实现从节点的添加、删除和遍历操作
3. 支持复制过程中的各种操作，如命令传播、状态更新等

通过链表结构，Redis可以高效地管理多个从节点，实现主从复制的核心功能。
