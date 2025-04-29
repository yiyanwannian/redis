# Redis主从模式

Redis主从模式是一种基本的高可用配置，由一个主节点(Master)和一个或多个从节点(Replica/Slave)组成。主节点处理写操作并将数据变更同步到从节点，从节点主要处理读操作。

## 主从模式特点

- 数据冗余，提高数据安全性
- 读写分离，提高读取性能
- 主节点仍存在单点故障风险
- 不提供自动故障转移功能

## 主从模式架构

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Redis主从模式架构

actor "客户端\nClient" as Client
database "Redis主节点\nMaster" as Master #LightBlue
database "Redis从节点1\nReplica 1" as Slave1 #LightGreen
database "Redis从节点2\nReplica 2" as Slave2 #LightGreen

Client -down-> Master : 写请求
Client -down-> Master : 读请求
Client -down-> Slave1 : 读请求
Client -down-> Slave2 : 读请求

Master -right-> Slave1 : 数据同步
Master -right-> Slave2 : 数据同步

note right of Master
  处理所有写操作
  将数据变更同步到从节点
  可以处理读操作
end note

note right of Slave1
  只读节点
  从主节点复制数据
  分担读负载
end note

@enduml
```

## 主从复制工作原理

```plantuml
@startuml
!theme plain
skinparam backgroundColor white
skinparam sequenceMessageAlign center

title Redis主从复制工作原理

participant "Redis主节点\nMaster" as Master #LightBlue
participant "Redis从节点\nReplica" as Slave #LightGreen

== 复制初始化 ==
Slave -> Slave: 配置 replicaof <masterip> <port>
Slave -> Master: 连接到主节点
Master --> Slave: 接受连接

== 全量同步(SYNC) ==
Slave -> Master: PSYNC ? -1 (首次复制)
Master -> Master: 执行BGSAVE生成RDB文件
Master --> Slave: 发送RDB文件
Slave -> Slave: 清空当前数据
Slave -> Slave: 加载RDB文件

== 命令传播(增量同步) ==
Master -> Master: 执行写命令
Master -> Master: 写入复制缓冲区
Master --> Slave: 发送写命令
Slave -> Slave: 执行相同的写命令

== 部分重同步(PSYNC) ==
note over Master, Slave: 网络短暂断开
Slave -> Slave: 尝试重新连接
Slave -> Master: PSYNC <runid> <offset>
Master -> Master: 检查复制偏移量
Master --> Slave: +CONTINUE <offset>
Master --> Slave: 发送断开期间的命令

== 心跳检测 ==
Slave -> Master: REPLCONF ACK <offset>
Master -> Master: 更新从节点复制偏移量
Master -> Master: 检测从节点延迟

@enduml
```

## 主从模式启动流程

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Redis主从模式启动流程

start
split
  :主节点启动;
  :加载配置文件;
  :初始化服务器状态;
  :创建事件循环;
  :初始化数据结构;
  :打开服务器监听端口;
  :初始化数据库;
  :启动定时器;
  :进入事件循环;
split again
  :从节点启动;
  :加载配置文件;
  :初始化服务器状态;
  :创建事件循环;
  :初始化数据结构;
  :打开服务器监听端口;
  :初始化数据库;
  :连接到主节点;
  :执行全量同步;
  :启动定时器;
  :进入事件循环;
end split
stop

@enduml
```

## 主从模式交互时序图

```plantuml
@startuml
!theme plain
skinparam backgroundColor white
skinparam sequenceMessageAlign center

title Redis主从模式交互时序图

participant "客户端\nClient" as Client
participant "Redis主节点\nMaster" as Master #LightBlue
participant "Redis从节点1\nReplica 1" as Slave1 #LightGreen
participant "Redis从节点2\nReplica 2" as Slave2 #LightGreen

== 初始化复制 ==
Slave1 -> Master: REPLICAOF <masterip> <port>
Master --> Slave1: 确认连接
Slave1 -> Master: PSYNC ? -1
Master -> Master: BGSAVE生成RDB文件
Master --> Slave1: 发送RDB文件
Slave1 -> Slave1: 加载RDB数据

Slave2 -> Master: REPLICAOF <masterip> <port>
Master --> Slave2: 确认连接
Slave2 -> Master: PSYNC ? -1
Master -> Master: BGSAVE生成RDB文件
Master --> Slave2: 发送RDB文件
Slave2 -> Slave2: 加载RDB数据

== 读写操作 ==
Client -> Master: SET key1 value1 (写操作)
Master -> Master: 执行命令
Master --> Client: OK
Master --> Slave1: 传播写命令
Master --> Slave2: 传播写命令
Slave1 -> Slave1: 执行相同命令
Slave2 -> Slave2: 执行相同命令

Client -> Slave1: GET key1 (读操作)
Slave1 --> Client: "value1"

Client -> Slave2: GET key1 (读操作)
Slave2 --> Client: "value1"

== 心跳检测 ==
Slave1 -> Master: REPLCONF ACK <offset>
Master --> Slave1: OK

Slave2 -> Master: REPLCONF ACK <offset>
Master --> Slave2: OK

== 网络中断和部分重同步 ==
note over Master, Slave1: 网络短暂断开
Client -> Master: SET key2 value2
Master -> Master: 执行命令并记录到复制缓冲区
Master --> Client: OK
Master --> Slave2: 传播写命令
Slave2 -> Slave2: 执行相同命令

note over Master, Slave1: 网络恢复
Slave1 -> Master: PSYNC <runid> <offset>
Master -> Master: 检查复制偏移量
Master --> Slave1: +CONTINUE <offset>
Master --> Slave1: 发送断开期间的命令
Slave1 -> Slave1: 执行命令追赶主节点

@enduml
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
