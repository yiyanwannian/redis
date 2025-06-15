# Redis单机模式

Redis单机模式是最基本的部署方式，只有一个Redis实例提供所有服务。

## 单机模式特点

- 简单易用，配置简单
- 没有数据冗余，单点故障风险高
- 受限于单机内存和计算能力
- 适合开发环境或数据不敏感的场景

## 单机模式架构

```mermaid
graph TB
    subgraph StandaloneArch["Redis单机模式架构"]
        Client["客户端<br/>Client"]
        Redis["Redis实例<br/>(单机)"]

        Client -->|读/写请求| Redis
        Redis -->|响应| Client
    end

    %% 注释
    Redis -.->|说明| Note["单个Redis实例处理所有请求<br/>数据存储在单机内存中<br/>没有数据冗余和高可用保障"]

    %% 样式
    classDef clientStyle fill:#e3f2fd,stroke:#1976d2
    classDef redisStyle fill:#ffecb3,stroke:#f57c00
    classDef noteStyle fill:#f3e5f5,stroke:#7b1fa2

    class Client clientStyle
    class Redis redisStyle
    class Note noteStyle
```

## 单机模式启动流程

```mermaid
flowchart TD
    Start([开始]) --> LoadConfig[加载配置文件]
    LoadConfig --> InitServer[初始化服务器状态]
    InitServer --> CreateEventLoop[创建事件循环]
    CreateEventLoop --> InitDataStructures[初始化数据结构]
    InitDataStructures --> OpenPort[打开服务器监听端口]
    OpenPort --> InitDatabase[初始化数据库]
    InitDatabase --> StartTimer[启动定时器]
    StartTimer --> EnterEventLoop[进入事件循环]
    EnterEventLoop --> End([结束])

    %% 注释
    EnterEventLoop -.->|说明| Note["单机模式下没有主从复制、<br/>Sentinel或集群相关的初始化"]

    %% 样式
    classDef startEnd fill:#c8e6c9,stroke:#388e3c
    classDef process fill:#e1f5fe,stroke:#1976d2
    classDef noteStyle fill:#fff3e0,stroke:#f57c00

    class Start,End startEnd
    class LoadConfig,InitServer,CreateEventLoop,InitDataStructures,OpenPort,InitDatabase,StartTimer,EnterEventLoop process
    class Note noteStyle
```

## 单机模式交互时序图

```mermaid
sequenceDiagram
    participant ClientA as 客户端A<br/>Client A
    participant ClientB as 客户端B<br/>Client B
    participant Redis as Redis实例<br/>(单机)

    Note over ClientA,Redis: 连接建立
    ClientA->>Redis: 建立TCP连接
    Redis-->>ClientA: 连接确认

    ClientB->>Redis: 建立TCP连接
    Redis-->>ClientB: 连接确认

    Note over ClientA,Redis: 数据操作
    ClientA->>Redis: SET key1 value1
    Redis->>Redis: 在内存中设置键值对
    Redis-->>ClientA: OK

    ClientB->>Redis: GET key1
    Redis->>Redis: 从内存中读取键值
    Redis-->>ClientB: "value1"

    ClientA->>Redis: INCR counter
    Redis->>Redis: 原子递增操作
    Redis-->>ClientA: 1

    ClientB->>Redis: INCR counter
    Redis->>Redis: 原子递增操作
    Redis-->>ClientB: 2

    Note over ClientA,Redis: 事务操作
    ClientA->>Redis: MULTI
    Redis-->>ClientA: OK
    ClientA->>Redis: SET key2 value2
    Redis-->>ClientA: QUEUED
    ClientA->>Redis: SET key3 value3
    Redis-->>ClientA: QUEUED
    ClientA->>Redis: EXEC
    Redis->>Redis: 原子执行事务中的命令
    Redis-->>ClientA: [OK, OK]

    Note over ClientA,Redis: 过期和内存管理
    ClientA->>Redis: SET key4 value4 EX 10
    Redis->>Redis: 设置键值对并设置10秒过期时间
    Redis-->>ClientA: OK

    Note over Redis: 10秒后
    Redis->>Redis: 惰性删除或<br/>定期删除过期键

    Note over Redis: 内存达到maxmemory
    Redis->>Redis: 根据淘汰策略<br/>删除部分键值对

    Note over ClientA,Redis: 连接关闭
    ClientA->>Redis: QUIT
    Redis-->>ClientA: OK
    ClientA->>Redis: 关闭TCP连接
```

## 单机模式的局限性

1. **单点故障风险**：Redis实例崩溃或所在服务器故障会导致服务不可用
2. **容量限制**：受限于单台服务器的内存容量
3. **性能瓶颈**：单个实例的处理能力有限
4. **数据安全性低**：没有数据冗余，数据丢失风险高

## 单机模式适用场景

- 开发和测试环境
- 数据量较小且对可用性要求不高的应用
- 作为本地缓存使用
- 简单的队列或计数器应用

## 单机模式配置示例

```conf
# redis.conf 单机模式配置示例
port 6379
bind 127.0.0.1
daemonize yes
pidfile /var/run/redis.pid
logfile /var/log/redis.log
dir /var/lib/redis
dbfilename dump.rdb
appendonly yes
appendfilename "appendonly.aof"
```
