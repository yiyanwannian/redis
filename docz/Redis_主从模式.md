# Redis主从模式

Redis主从模式是一种基本的高可用配置，由一个主节点(Master)和一个或多个从节点(Replica/Slave)组成。主节点处理写操作并将数据变更同步到从节点，从节点主要处理读操作。

## 主从模式特点

- 数据冗余，提高数据安全性
- 读写分离，提高读取性能
- 主节点仍存在单点故障风险
- 不提供自动故障转移功能

## 主从模式架构

```mermaid
graph TB
    subgraph MasterSlaveArch["Redis主从模式架构"]
        Client["客户端<br/>Client"]

        subgraph RedisNodes["Redis节点"]
            Master["Redis主节点<br/>Master"]
            Slave1["Redis从节点1<br/>Replica 1"]
            Slave2["Redis从节点2<br/>Replica 2"]
        end
    end

    %% 客户端连接
    Client -->|写请求| Master
    Client -->|读请求| Master
    Client -->|读请求| Slave1
    Client -->|读请求| Slave2

    %% 数据同步
    Master -->|数据同步| Slave1
    Master -->|数据同步| Slave2

    %% 注释
    Master -.->|说明1| Note1["处理所有写操作<br/>将数据变更同步到从节点<br/>可以处理读操作"]
    Slave1 -.->|说明2| Note2["只读节点<br/>从主节点复制数据<br/>分担读负载"]

    %% 样式
    classDef clientStyle fill:#e3f2fd,stroke:#1976d2
    classDef masterStyle fill:#bbdefb,stroke:#1976d2
    classDef slaveStyle fill:#c8e6c9,stroke:#388e3c
    classDef noteStyle fill:#fff3e0,stroke:#f57c00

    class Client clientStyle
    class Master masterStyle
    class Slave1,Slave2 slaveStyle
    class Note1,Note2 noteStyle
```

## 主从复制工作原理

```mermaid
sequenceDiagram
    participant Master as Redis主节点<br/>Master
    participant Slave as Redis从节点<br/>Replica

    Note over Master,Slave: Redis主从复制工作原理

    Note over Master,Slave: 复制初始化
    Slave->>Slave: 配置 replicaof <masterip> <port>
    Slave->>Master: 连接到主节点
    Master-->>Slave: 接受连接

    Note over Master,Slave: 全量同步(SYNC)
    Slave->>Master: PSYNC ? -1 (首次复制)
    Master->>Master: 执行BGSAVE生成RDB文件
    Master-->>Slave: 发送RDB文件
    Slave->>Slave: 清空当前数据
    Slave->>Slave: 加载RDB文件

    Note over Master,Slave: 命令传播(增量同步)
    Master->>Master: 执行写命令
    Master->>Master: 写入复制缓冲区
    Master-->>Slave: 发送写命令
    Slave->>Slave: 执行相同的写命令

    Note over Master,Slave: 部分重同步(PSYNC)
    Note over Master,Slave: 网络短暂断开
    Slave->>Slave: 尝试重新连接
    Slave->>Master: PSYNC <runid> <offset>
    Master->>Master: 检查复制偏移量
    Master-->>Slave: +CONTINUE <offset>
    Master-->>Slave: 发送断开期间的命令

    Note over Master,Slave: 心跳检测
    Slave->>Master: REPLCONF ACK <offset>
    Master->>Master: 更新从节点复制偏移量
    Master->>Master: 检测从节点延迟
```

## 主从模式启动流程

```mermaid
flowchart TD
    Start([开始]) --> Split{启动模式}

    %% 主节点启动流程
    Split -->|主节点| MasterStart[主节点启动]
    MasterStart --> MasterLoadConfig[加载配置文件]
    MasterLoadConfig --> MasterInitServer[初始化服务器状态]
    MasterInitServer --> MasterCreateEventLoop[创建事件循环]
    MasterCreateEventLoop --> MasterInitDataStructures[初始化数据结构]
    MasterInitDataStructures --> MasterOpenPort[打开服务器监听端口]
    MasterOpenPort --> MasterInitDatabase[初始化数据库]
    MasterInitDatabase --> MasterStartTimer[启动定时器]
    MasterStartTimer --> MasterEventLoop[进入事件循环]
    MasterEventLoop --> End([结束])

    %% 从节点启动流程
    Split -->|从节点| SlaveStart[从节点启动]
    SlaveStart --> SlaveLoadConfig[加载配置文件]
    SlaveLoadConfig --> SlaveInitServer[初始化服务器状态]
    SlaveInitServer --> SlaveCreateEventLoop[创建事件循环]
    SlaveCreateEventLoop --> SlaveInitDataStructures[初始化数据结构]
    SlaveInitDataStructures --> SlaveOpenPort[打开服务器监听端口]
    SlaveOpenPort --> SlaveInitDatabase[初始化数据库]
    SlaveInitDatabase --> SlaveConnectMaster[连接到主节点]
    SlaveConnectMaster --> SlaveFullSync[执行全量同步]
    SlaveFullSync --> SlaveStartTimer[启动定时器]
    SlaveStartTimer --> SlaveEventLoop[进入事件循环]
    SlaveEventLoop --> End

    %% 样式
    classDef startEnd fill:#c8e6c9,stroke:#388e3c
    classDef masterProcess fill:#bbdefb,stroke:#1976d2
    classDef slaveProcess fill:#c8e6c9,stroke:#388e3c
    classDef decision fill:#fff3e0,stroke:#f57c00

    class Start,End startEnd
    class Split decision
    class MasterStart,MasterLoadConfig,MasterInitServer,MasterCreateEventLoop,MasterInitDataStructures,MasterOpenPort,MasterInitDatabase,MasterStartTimer,MasterEventLoop masterProcess
    class SlaveStart,SlaveLoadConfig,SlaveInitServer,SlaveCreateEventLoop,SlaveInitDataStructures,SlaveOpenPort,SlaveInitDatabase,SlaveConnectMaster,SlaveFullSync,SlaveStartTimer,SlaveEventLoop slaveProcess
```

## 主从模式交互时序图

```mermaid
sequenceDiagram
    participant Client as 客户端<br/>Client
    participant Master as Redis主节点<br/>Master
    participant Slave1 as Redis从节点1<br/>Replica 1
    participant Slave2 as Redis从节点2<br/>Replica 2

    Note over Client,Slave2: Redis主从模式交互时序图

    Note over Master,Slave2: 初始化复制
    Slave1->>Master: REPLICAOF <masterip> <port>
    Master-->>Slave1: 确认连接
    Slave1->>Master: PSYNC ? -1
    Master->>Master: BGSAVE生成RDB文件
    Master-->>Slave1: 发送RDB文件
    Slave1->>Slave1: 加载RDB数据

    Slave2->>Master: REPLICAOF <masterip> <port>
    Master-->>Slave2: 确认连接
    Slave2->>Master: PSYNC ? -1
    Master->>Master: BGSAVE生成RDB文件
    Master-->>Slave2: 发送RDB文件
    Slave2->>Slave2: 加载RDB数据

    Note over Client,Slave2: 读写操作
    Client->>Master: SET key1 value1 (写操作)
    Master->>Master: 执行命令
    Master-->>Client: OK
    Master-->>Slave1: 传播写命令
    Master-->>Slave2: 传播写命令
    Slave1->>Slave1: 执行相同命令
    Slave2->>Slave2: 执行相同命令

    Client->>Slave1: GET key1 (读操作)
    Slave1-->>Client: "value1"

    Client->>Slave2: GET key1 (读操作)
    Slave2-->>Client: "value1"

    Note over Master,Slave2: 心跳检测
    Slave1->>Master: REPLCONF ACK <offset>
    Master-->>Slave1: OK

    Slave2->>Master: REPLCONF ACK <offset>
    Master-->>Slave2: OK

    Note over Master,Slave2: 网络中断和部分重同步
    Note over Master,Slave1: 网络短暂断开
    Client->>Master: SET key2 value2
    Master->>Master: 执行命令并记录到复制缓冲区
    Master-->>Client: OK
    Master-->>Slave2: 传播写命令
    Slave2->>Slave2: 执行相同命令

    Note over Master,Slave1: 网络恢复
    Slave1->>Master: PSYNC <runid> <offset>
    Master->>Master: 检查复制偏移量
    Master-->>Slave1: +CONTINUE <offset>
    Master-->>Slave1: 发送断开期间的命令
    Slave1->>Slave1: 执行命令追赶主节点
```

## 主从模式的优缺点

### 优点
1. **读写分离**：提高系统读取性能
2. **数据冗余**：多个副本提高数据安全性
3. **高可用基础**：为Sentinel和集群模式提供基础

### 缺点
1. **主节点单点故障**：主节点故障时无法自动切换
2. **写性能瓶颈**：所有写操作都集中在主节点
3. **全量复制开销**：新从节点加入或断开重连时可能需要全量复制
4. **复制延迟**：从节点数据可能落后于主节点

## 主从模式配置示例

### 主节点配置 (redis-master.conf)
```conf
port 6379
bind 0.0.0.0
daemonize yes
pidfile /var/run/redis_6379.pid
logfile /var/log/redis/redis_6379.log
dir /var/lib/redis
dbfilename dump_6379.rdb
appendonly yes
appendfilename "appendonly_6379.aof"
```

### 从节点配置 (redis-replica.conf)
```conf
port 6380
bind 0.0.0.0
daemonize yes
pidfile /var/run/redis_6380.pid
logfile /var/log/redis/redis_6380.log
dir /var/lib/redis
dbfilename dump_6380.rdb
appendonly yes
appendfilename "appendonly_6380.aof"
replicaof 127.0.0.1 6379  # 指定主节点
replica-read-only yes     # 从节点只读
```
