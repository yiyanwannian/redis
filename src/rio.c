/*
 * Copyright (c) 2009-Present, Redis Ltd.
 * All rights reserved.
 *
 * Copyright (c) 2024-present, Valkey contributors.
 * All rights reserved.
 *
 * Licensed under your choice of the Redis Source Available License 2.0
 * (RSALv2) or the Server Side Public License v1 (SSPLv1).
 *
 * Portions of this file are available under BSD3 terms; see REDISCONTRIBUTIONS for more information.
 */

/* rio.c is a simple stream-oriented I/O abstraction that provides an interface
 * to write code that can consume/produce data using different concrete input
 * and output devices. For instance the same rdb.c code using the rio
 * abstraction can be used to read and write the RDB format using in-memory
 * buffers or files.
 *
 * A rio object provides the following methods:
 *  read: read from stream.
 *  write: write to stream.
 *  tell: get the current offset.
 *
 * It is also possible to set a 'checksum' method that is used by rio.c in order
 * to compute a checksum of the data written or read, or to query the rio object
 * for the current checksum.
 *
 * ----------------------------------------------------------------------------
 */

/* rio.c is a simple stream-oriented I/O abstraction that provides an interface
 * to write code that can consume/produce data using different concrete input
 * and output devices. For instance the same rdb.c code using the rio
 * abstraction can be used to read and write the RDB format using in-memory
 * buffers or files.
 *
 * A rio object provides the following methods:
 *  read: read from stream.
 *  write: write to stream.
 *  tell: get the current offset.
 *
 * It is also possible to set a 'checksum' method that is used by rio.c in order
 * to compute a checksum of the data written or read, or to query the rio object
 * for the current checksum.
 *
 * ----------------------------------------------------------------------------
 */
/* rio.c 是一个简单的面向流的 I/O 抽象，提供了一个接口，
 * 用于编写可以使用不同具体输入和输出设备来消费/生成数据的代码。
 * 例如，使用 rio 抽象的相同 rdb.c 代码可以用来通过内存缓冲区或文件
 * 读取和写入 RDB 格式。
 *
 * 一个 rio 对象提供以下方法：
 *  read: 从流中读取。
 *  write: 写入到流中。
 *  tell: 获取当前偏移量。
 *
 * 还可以设置一个 'checksum' 方法，rio.c 使用该方法来计算写入或读取数据的校验和，
 * 或查询 rio 对象的当前校验和。
 *
 * ----------------------------------------------------------------------------
 */

/* rio.c 是一个简单的面向流的 I/O 抽象，提供了一个接口，
 * 用于编写可以使用不同具体输入和输出设备来消费/生成数据的代码。
 * 例如，使用 rio 抽象的相同 rdb.c 代码可以用来通过内存缓冲区或文件
 * 读取和写入 RDB 格式。
 *
 * 一个 rio 对象提供以下方法：
 *  read: 从流中读取。
 *  write: 写入到流中。
 *  tell: 获取当前偏移量。
 *
 * 还可以设置一个 'checksum' 方法，rio.c 使用该方法来计算写入或读取数据的校验和，
 * 或查询 rio 对象的当前校验和。
 *
 * ----------------------------------------------------------------------------
 */

#include "fmacros.h"
#include "fpconv_dtoa.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "rio.h"
#include "util.h"
#include "crc64.h"
#include "config.h"
#include "server.h"

/* ------------------------- Buffer I/O implementation ----------------------- */

/*
 * 缓冲区写入函数
 * 将数据追加到 SDS 字符串中，并更新位置指针
 * 返回 1 表示成功，0 表示失败
 */
static size_t rioBufferWrite(rio *r, const void *buf, size_t len) {
    /* 使用 sdscatlen 将数据追加到 SDS 字符串中 */
    r->io.buffer.ptr = sdscatlen(r->io.buffer.ptr,(char*)buf,len);
    /* 更新位置指针 */
    r->io.buffer.pos += len;
    return 1; /* 始终成功 */
}

/*
 * 缓冲区读取函数
 * 从 SDS 字符串的当前位置读取指定长度的数据
 * 返回 1 表示成功，0 表示失败（数据不足）
 */
static size_t rioBufferRead(rio *r, void *buf, size_t len) {
    /* 检查是否有足够的数据可读 */
    if (sdslen(r->io.buffer.ptr)-r->io.buffer.pos < len)
        return 0; /* 数据不足 */

    /* 从当前位置复制数据 */
    memcpy(buf,r->io.buffer.ptr+r->io.buffer.pos,len);
    r->io.buffer.pos += len;
    return 1;
}

/*
 * 获取缓冲区当前位置
 * 返回当前读/写位置
 */
static off_t rioBufferTell(rio *r) {
    return r->io.buffer.pos;
}

/*
 * 缓冲区刷新函数
 * 对于缓冲区 I/O，刷新操作是空操作
 * 返回 1 表示成功
 */
static int rioBufferFlush(rio *r) {
    UNUSED(r);
    return 1; /* 缓冲区 I/O 不需要刷新操作 */
}

/* 缓冲区 I/O 的 RIO 对象模板 */
static const rio rioBufferIO = {
        rioBufferRead,       /* 读取函数 */
        rioBufferWrite,      /* 写入函数 */
        rioBufferTell,       /* 位置查询函数 */
        rioBufferFlush,      /* 刷新函数 */
        NULL,                /* 校验和更新函数（默认为空） */
        0,                   /* 当前校验和 */
        0,                   /* 标志 */
        0,                   /* 已处理字节数 */
        0,                   /* 读/写块大小 */
        { { NULL, 0 } }      /* I/O 特定变量的联合体 */
};

/*
 * 初始化缓冲区 I/O
 * 参数:
 *   r: 要初始化的 RIO 对象
 *   s: 用作缓冲区的 SDS 字符串
 */
void rioInitWithBuffer(rio *r, sds s) {
    /* 复制模板 */
    *r = rioBufferIO;
    /* 设置缓冲区指针 */
    r->io.buffer.ptr = s;
    /* 初始化位置为 0 */
    r->io.buffer.pos = 0;
}

/* --------------------- Stdio file pointer implementation ------------------- */

/*
 * 文件写入函数
 * 将数据写入文件，并在启用自动同步时管理缓冲区和同步操作
 * 返回 1 表示成功，0 表示失败
 */
static size_t rioFileWrite(rio *r, const void *buf, size_t len) {
    /* 如果未启用自动同步，直接写入文件 */
    if (!r->io.file.autosync) return fwrite(buf,len,1,r->io.file.fp);

    /* 写入文件 */
    if (fwrite(buf,len,1,r->io.file.fp) == 0) return 0;

    /* 更新已缓冲的字节数 */
    r->io.file.buffered += len;

    /* 如果缓冲的数据超过了自动同步阈值，则执行同步 */
    if (r->io.file.autosync && r->io.file.buffered >= r->io.file.autosync) {
        /* 刷新文件缓冲区 */
        fflush(r->io.file.fp);

        /* 根据配置决定是否回收页面缓存 */
        if (r->io.file.reclaim_cache) {
            /* 同步文件到磁盘 */
            redis_fsync(fileno(r->io.file.fp));
            /* 回收页面缓存 */
            reclaimFilePageCache(fileno(r->io.file.fp), 0, 0);
        } else {
            /* 只同步文件到磁盘 */
            redis_fsync(fileno(r->io.file.fp));
        }

        /* 重置缓冲计数器 */
        r->io.file.buffered = 0;
    }

    return 1;
}

/*
 * 文件读取函数
 * 从文件读取指定长度的数据
 * 返回 1 表示成功，0 表示失败
 */
static size_t rioFileRead(rio *r, void *buf, size_t len) {
    return fread(buf,len,1,r->io.file.fp);
}

/*
 * 获取文件当前位置
 * 返回文件指针的当前位置
 */
static off_t rioFileTell(rio *r) {
    return ftello(r->io.file.fp);
}

/*
 * 文件刷新函数
 * 刷新文件缓冲区到磁盘
 * 返回 1 表示成功，0 表示失败
 */
static int rioFileFlush(rio *r) {
    return (fflush(r->io.file.fp) == 0) ? 1 : 0;
}

/* 文件 I/O 的 RIO 对象模板 */
static const rio rioFileIO = {
        rioFileRead,         /* 读取函数 */
        rioFileWrite,        /* 写入函数 */
        rioFileTell,         /* 位置查询函数 */
        rioFileFlush,        /* 刷新函数 */
        NULL,                /* 校验和更新函数（默认为空） */
        0,                   /* 当前校验和 */
        0,                   /* 标志 */
        0,                   /* 已处理字节数 */
        0,                   /* 读/写块大小 */
        { { NULL, 0 } }      /* I/O 特定变量的联合体 */
};

/*
 * 初始化文件 I/O
 * 参数:
 *   r: 要初始化的 RIO 对象
 *   fp: 文件指针
 */
void rioInitWithFile(rio *r, FILE *fp) {
    /* 复制模板 */
    *r = rioFileIO;
    /* 设置文件指针 */
    r->io.file.fp = fp;
    /* 初始化缓冲计数器 */
    r->io.file.buffered = 0;
    /* 默认不启用自动同步 */
    r->io.file.autosync = 0;
    /* 默认不回收页面缓存 */
    r->io.file.reclaim_cache = 0;
}

/* ------------------- Connection implementation -------------------
 * We use this RIO implementation when reading an RDB file directly from
 * the connection to the memory via rdbLoadRio(), thus this implementation
 * only implements reading from a connection that is, normally,
 * just a socket. */

/*
 * 连接写入函数
 * 连接 I/O 主要用于读取，不支持写入
 * 返回 0 表示失败
 */
static size_t rioConnWrite(rio *r, const void *buf, size_t len) {
    UNUSED(r);
    UNUSED(buf);
    UNUSED(len);
    return 0; /* 连接 I/O 不支持写入 */
}

/*
 * 连接读取函数
 * 从连接读取数据，并跟踪已读取的字节数
 * 返回 1 表示成功，0 表示失败
 */
static size_t rioConnRead(rio *r, void *buf, size_t len) {
    /* 计算还可以读取的字节数 */
    size_t avail = r->io.conn.read_limit - r->io.conn.read_so_far;

    /* 检查是否超过读取限制 */
    if (avail == 0) return 0;
    if (len > avail) len = avail;

    /* 从连接读取数据 */
    if (connRead(r->io.conn.conn, buf, len) != (ssize_t)len) return 0;
    r->io.conn.read_so_far += len;
    return 1;
}

/*
 * 获取连接当前位置
 * 返回已读取的字节数
 */
static off_t rioConnTell(rio *r) {
    return r->io.conn.read_so_far;
}

/*
 * 连接刷新函数
 * 对于连接 I/O，刷新操作是空操作
 * 返回 1 表示成功
 */
static int rioConnFlush(rio *r) {
    UNUSED(r);
    return 1; /* 连接 I/O 不需要刷新操作 */
}

/* 连接 I/O 的 RIO 对象模板 */
static const rio rioConnIO = {
        rioConnRead,         /* 读取函数 */
        rioConnWrite,        /* 写入函数（不支持） */
        rioConnTell,         /* 位置查询函数 */
        rioConnFlush,        /* 刷新函数 */
        NULL,                /* 校验和更新函数（默认为空） */
        0,                   /* 当前校验和 */
        0,                   /* 标志 */
        0,                   /* 已处理字节数 */
        0,                   /* 读/写块大小 */
        { .conn = { NULL, 0, NULL, 0, 0 } }      /* I/O 特定变量的联合体，修正初始化 */
};

/*
 * 初始化连接 I/O
 * 参数:
 *   r: 要初始化的 RIO 对象
 *   conn: 连接对象
 *   read_limit: 最大读取字节数
 */
void rioInitWithConn(rio *r, connection *conn, size_t read_limit) {
    /* 复制模板 */
    *r = rioConnIO;
    /* 设置连接对象 */
    r->io.conn.conn = conn;
    /* 设置读取限制 */
    r->io.conn.read_limit = read_limit;
    /* 初始化已读计数器 */
    r->io.conn.read_so_far = 0;
    r->io.conn.pos = 0;
    r->io.conn.buf = NULL;
}

/*
 * 释放连接 I/O 资源
 * 参数:
 *   r: RIO 对象
 *   out_remainingBufferedData: 如果不为 NULL，将未读缓冲区返回给调用者
 */
void rioFreeConn(rio *r, sds* out_remainingBufferedData) {
    /* 如果调用者需要未读数据，返回缓冲区 */
    if (out_remainingBufferedData)
        *out_remainingBufferedData = r->io.conn.buf;
    else
        sdsfree(r->io.conn.buf);

    /* 清除未读缓冲区指针 */
    r->io.conn.buf = NULL;
}

/* ------------------- File descriptor implementation ------------------- */

/*
 * 文件描述符写入函数
 * 将数据写入文件描述符，支持缓冲和刷新
 * 返回 1 表示成功，0 表示失败
 */
static size_t rioFdWrite(rio *r, const void *buf, size_t len) {
    ssize_t retval;
    unsigned char *p = (unsigned char*) buf;
    /* 检查是否是刷新操作 */
    int doflush = (buf == NULL && len == 0);

    /* 如果是刷新操作 */
    if (doflush) {
        /* 如果有缓冲区且不为空，将缓冲区数据写入文件描述符 */
        if (r->io.fd.buf && sdslen(r->io.fd.buf) > 0) {
            retval = write(r->io.fd.fd, r->io.fd.buf, sdslen(r->io.fd.buf));
            if (retval <= 0) return 0;
        }
        return 1;
    }

    /* 如果有缓冲区，先写入缓冲区 */
    if (r->io.fd.buf) {
        /* 追加数据到缓冲区 */
        r->io.fd.buf = sdscatlen(r->io.fd.buf, buf, len);

        /* 如果缓冲区超过了阈值，将缓冲区数据写入文件描述符 */
        if (sdslen(r->io.fd.buf) > PROTO_IOBUF_LEN) {
            retval = write(r->io.fd.fd, r->io.fd.buf, sdslen(r->io.fd.buf));
            if (retval <= 0) return 0;
            /* 清空缓冲区 */
            sdsclear(r->io.fd.buf);
        }

        /* 更新位置 */
        r->io.fd.pos += len;
        return 1;
    }

    /* 直接写入文件描述符 */
    while(len > 0) {
        retval = write(r->io.fd.fd, p, len);
        if (retval <= 0) {
            /* 处理中断错误 */
            if (retval == -1 && errno == EINTR) continue;
            /* 处理阻塞错误 */
            if (retval == -1 && errno == EWOULDBLOCK) errno = ETIMEDOUT;
            return 0; /* 写入失败 */
        }
        p += retval;
        len -= retval;
    }

    /* 更新位置 */
    r->io.fd.pos += len;
    return 1;
}

/*
 * 文件描述符读取函数
 * 文件描述符 I/O 主要用于写入，不支持读取
 * 返回 0 表示失败
 */
static size_t rioFdRead(rio *r, void *buf, size_t len) {
    UNUSED(r);
    UNUSED(buf);
    UNUSED(len);
    return 0; /* 文件描述符 I/O 不支持读取 */
}

/*
 * 获取文件描述符当前位置
 * 返回当前位置
 */
static off_t rioFdTell(rio *r) {
    return r->io.fd.pos;
}

/*
 * 文件描述符刷新函数
 * 调用写入函数执行刷新操作
 * 返回 1 表示成功，0 表示失败
 */
static int rioFdFlush(rio *r) {
    return rioFdWrite(r, NULL, 0);
}

/* 文件描述符 I/O 的 RIO 对象模板 */
static const rio rioFdIO = {
        rioFdRead,           /* 读取函数（不支持） */
        rioFdWrite,          /* 写入函数 */
        rioFdTell,           /* 位置查询函数 */
        rioFdFlush,          /* 刷新函数 */
        NULL,                /* 校验和更新函数（默认为空） */
        0,                   /* 当前校验和 */
        0,                   /* 标志 */
        0,                   /* 已处理字节数 */
        0,                   /* 读/写块大小 */
        { { 0, 0 } }      /* I/O 特定变量的联合体 */
};

/*
 * 初始化文件描述符 I/O
 * 参数:
 *   r: 要初始化的 RIO 对象
 *   fd: 文件描述符
 */
void rioInitWithFd(rio *r, int fd) {
    /* 复制模板 */
    *r = rioFdIO;
    /* 设置文件描述符 */
    r->io.fd.fd = fd;
    /* 初始化位置为 0 */
    r->io.fd.pos = 0;
    /* 创建空的缓冲区 */
    r->io.fd.buf = sdsempty();
}

/*
 * 释放文件描述符 I/O 资源
 * 参数:
 *   r: RIO 对象
 */
void rioFreeFd(rio *r) {
    /* 释放缓冲区 */
    sdsfree(r->io.fd.buf);
}

/* ------------------- Connection set implementation ------------------
 * This target is used to write the RDB file to a set of replica connections as
 * part of rdb channel replication. */

/* Returns 1 for success, 0 for failure.
 * The function returns success as long as we are able to correctly write
 * to at least one file descriptor.
 *
 * When buf is NULL or len is 0, the function performs a flush operation if
 * there is some pending buffer, so this function is also used in order to
 * implement rioConnsetFlush(). */
static size_t rioConnsetWrite(rio *r, const void *buf, size_t len) {
    const size_t pre_flush_size = 256 * 1024;
    unsigned char *p = (unsigned char*) buf;
    size_t buflen = len;
    size_t failed = 0; /* number of connections that write() returned error. */

    /* For small writes, we rather keep the data in user-space buffer, and flush
     * it only when it grows. however for larger writes, we prefer to flush
     * any pre-existing buffer, and write the new one directly without reallocs
     * and memory copying. */
    if (len > pre_flush_size) {
        rioConnsetWrite(r, NULL, 0);
    } else {
        if (buf && len) {
            r->io.connset.buf = sdscatlen(r->io.connset.buf, buf, len);
            if (sdslen(r->io.connset.buf) <= PROTO_IOBUF_LEN)
                return 1;
        }

        p = (unsigned char *)r->io.connset.buf;
        buflen = sdslen(r->io.connset.buf);
    }

    while (buflen > 0) {
        /* Write in little chunks so that when there are big writes we
         * parallelize while the kernel is sending data in background to the
         * TCP socket. */
        size_t limit = PROTO_IOBUF_LEN * 2;
        size_t count = buflen < limit ? buflen : limit;

        for (size_t i = 0; i < r->io.connset.n_dst; i++) {
            size_t n_written = 0;

            if (r->io.connset.dst[i].failed != 0) {
                failed++;
                continue; /* Skip failed connections. */
            }

            do {
                ssize_t ret;
                connection *c = r->io.connset.dst[i].conn;

                ret = connWrite(c, p + n_written, count - n_written);
                if (ret <= 0) {
                    if (errno == 0)
                        errno = EIO;
                    /* With blocking sockets, which is the sole user of this
                     * rio target, EWOULDBLOCK is returned only because of
                     * the SO_SNDTIMEO socket option, so we translate the error
                     * into one more recognizable by the user. */
                    if (ret == -1 && errno == EWOULDBLOCK)
                        errno = ETIMEDOUT;

                    r->io.connset.dst[i].failed = 1;
                    break;
                }
                n_written += ret;
            } while (n_written != count);
        }
        if (failed == r->io.connset.n_dst)
            return 0; /* All the connections have failed. */

        p += count;
        buflen -= count;
        r->io.connset.pos += count;
    }

    sdsclear(r->io.connset.buf);
    return 1;
}

/* Returns 1 or 0 for success/failure. */
static size_t rioConnsetRead(rio *r, void *buf, size_t len) {
    UNUSED(r);
    UNUSED(buf);
    UNUSED(len);
    return 0; /* Error, this target does not support reading. */
}

/* Returns the number of sent bytes. */
static off_t rioConnsetTell(rio *r) {
    return r->io.connset.pos;
}

/* Flushes any buffer to target device if applicable. Returns 1 on success
 * and 0 on failures. */
static int rioConnsetFlush(rio *r) {
    /* Our flush is implemented by the write method, that recognizes a
     * buffer set to NULL with a count of zero as a flush request. */
    return rioConnsetWrite(r, NULL, 0);
}

static const rio rioConnsetIO = {
        rioConnsetRead,
        rioConnsetWrite,
        rioConnsetTell,
        rioConnsetFlush,
        NULL,            /* update_checksum */
        0,               /* current checksum */
        0,               /* flags */
        0,               /* bytes read or written */
        0,               /* read/write chunk size */
        { { NULL, 0 } }  /* union for io-specific vars */
};

void rioInitWithConnset(rio *r, connection **conns, size_t n_conns) {
    *r = rioConnsetIO;
    r->io.connset.dst = zcalloc(sizeof(*r->io.connset.dst) * n_conns);
    r->io.connset.n_dst = n_conns;
    r->io.connset.pos = 0;
    r->io.connset.buf = sdsempty();

    for (size_t i = 0; i < n_conns; i++)
        r->io.connset.dst[i].conn = conns[i];
}

/* release the rio stream. */
void rioFreeConnset(rio *r) {
    zfree(r->io.connset.dst);
    sdsfree(r->io.connset.buf);
}

/* ---------------------------- Generic functions ---------------------------- */

/* This function can be installed both in memory and file streams when checksum
 * computation is needed. */
void rioGenericUpdateChecksum(rio *r, const void *buf, size_t len) {
    r->cksum = crc64(r->cksum,buf,len);
}

/* Set the file-based rio object to auto-fsync every 'bytes' file written.
 * By default this is set to zero that means no automatic file sync is
 * performed.
 *
 * This feature is useful in a few contexts since when we rely on OS write
 * buffers sometimes the OS buffers way too much, resulting in too many
 * disk I/O concentrated in very little time. When we fsync in an explicit
 * way instead the I/O pressure is more distributed across time. */
void rioSetAutoSync(rio *r, off_t bytes) {
    if(r->write != rioFileIO.write) return;
    r->io.file.autosync = bytes;
}

/* Set the file-based rio object to reclaim cache after every auto-sync.
 * In the Linux implementation POSIX_FADV_DONTNEED skips the dirty
 * pages, so if auto sync is unset this option will have no effect.
 *
 * This feature can reduce the cache footprint backed by the file. */
void rioSetReclaimCache(rio *r, int enabled) {
    r->io.file.reclaim_cache = enabled;
}

/* Check the type of rio. */
uint8_t rioCheckType(rio *r) {
    if (r->read == rioFileRead) {
        return RIO_TYPE_FILE;
    } else if (r->read == rioBufferRead) {
        return RIO_TYPE_BUFFER;
    } else if (r->read == rioConnRead) {
        return RIO_TYPE_CONN;
    } else {
        /* r->read == rioFdRead */
        return RIO_TYPE_FD;
    }
}

/* --------------------------- Higher level interface --------------------------
 *
 * The following higher level functions use lower level rio.c functions to help
 * generating the Redis protocol for the Append Only File. */

/* Write multi bulk count in the format: "*<count>\r\n". */
size_t rioWriteBulkCount(rio *r, char prefix, long count) {
    char cbuf[128];
    int clen;

    cbuf[0] = prefix;
    clen = 1+ll2string(cbuf+1,sizeof(cbuf)-1,count);
    cbuf[clen++] = '\r';
    cbuf[clen++] = '\n';
    if (rioWrite(r,cbuf,clen) == 0) return 0;
    return clen;
}

/* Write binary-safe string in the format: "$<count>\r\n<payload>\r\n". */
size_t rioWriteBulkString(rio *r, const char *buf, size_t len) {
    size_t nwritten;

    if ((nwritten = rioWriteBulkCount(r,'$',len)) == 0) return 0;
    if (len > 0 && rioWrite(r,buf,len) == 0) return 0;
    if (rioWrite(r,"\r\n",2) == 0) return 0;
    return nwritten+len+2;
}

/* Write a long long value in format: "$<count>\r\n<payload>\r\n". */
size_t rioWriteBulkLongLong(rio *r, long long l) {
    char lbuf[32];
    unsigned int llen;

    llen = ll2string(lbuf,sizeof(lbuf),l);
    return rioWriteBulkString(r,lbuf,llen);
}

/* Write a double value in the format: "$<count>\r\n<payload>\r\n" */
size_t rioWriteBulkDouble(rio *r, double d) {
    char dbuf[128];
    unsigned int dlen;
    dlen = fpconv_dtoa(d, dbuf);
    dbuf[dlen] = '\0';
    return rioWriteBulkString(r,dbuf,dlen);
}
