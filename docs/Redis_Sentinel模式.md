# Redis Sentinel模式

Redis Sentinel是Redis的高可用解决方案，提供监控、通知、自动故障转移和配置提供者服务。Sentinel模式基于主从复制模式，增加了自动故障检测和转移功能。

## Sentinel模式特点

- 监控：监控Redis主从节点的健康状态
- 通知：当被监控的Redis实例出现问题时，通知管理员或其他程序
- 自动故障转移：当主节点故障时，自动选择一个从节点升级为新的主节点
- 配置提供者：客户端连接到Sentinel获取当前主节点的地址

## Sentinel模式架构

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Redis Sentinel模式架构

actor "客户端\nClient" as Client
database "Redis主节点\nMaster" as Master #LightBlue
database "Redis从节点1\nReplica 1" as Slave1 #LightGreen
database "Redis从节点2\nReplica 2" as Slave2 #LightGreen
control "Sentinel节点1" as Sentinel1 #LightPink
control "Sentinel节点2" as Sentinel2 #LightPink
control "Sentinel节点3" as Sentinel3 #LightPink

Client -down-> Sentinel1 : 查询主节点
Client -down-> Sentinel2 : 查询主节点
Client -down-> Sentinel3 : 查询主节点
Client -down-> Master : 读/写请求
Client -down-> Slave1 : 读请求
Client -down-> Slave2 : 读请求

Master -right-> Slave1 : 数据同步
Master -right-> Slave2 : 数据同步

Sentinel1 -down-> Master : 监控
Sentinel1 -down-> Slave1 : 监控
Sentinel1 -down-> Slave2 : 监控

Sentinel2 -down-> Master : 监控
Sentinel2 -down-> Slave1 : 监控
Sentinel2 -down-> Slave2 : 监控

Sentinel3 -down-> Master : 监控
Sentinel3 -down-> Slave1 : 监控
Sentinel3 -down-> Slave2 : 监控

Sentinel1 <-right-> Sentinel2 : 通信
Sentinel2 <-right-> Sentinel3 : 通信
Sentinel3 <-right-> Sentinel1 : 通信

note right of Sentinel1
  监控Redis节点
  检测节点故障
  执行故障转移
  通知客户端
end note

@enduml
```

## Sentinel工作原理

```plantuml
@startuml
!theme plain
skinparam backgroundColor white
skinparam sequenceMessageAlign center

title Redis Sentinel工作原理

participant "Sentinel节点A" as SentinelA #LightPink
participant "Sentinel节点B" as SentinelB #LightPink
participant "Sentinel节点C" as SentinelC #LightPink
participant "Redis主节点\nMaster" as Master #LightBlue
participant "Redis从节点1\nReplica 1" as Slave1 #LightGreen
participant "Redis从节点2\nReplica 2" as Slave2 #LightGreen

== 监控阶段 ==
SentinelA -> Master: PING (每秒)
Master --> SentinelA: PONG
SentinelA -> Master: INFO (每10秒)
Master --> SentinelA: 返回INFO信息

SentinelA -> Slave1: PING
Slave1 --> SentinelA: PONG
SentinelA -> Slave1: INFO
Slave1 --> SentinelA: 返回INFO信息

SentinelA -> Slave2: PING
Slave2 --> SentinelA: PONG
SentinelA -> Slave2: INFO
Slave2 --> SentinelA: 返回INFO信息

== Sentinel之间的通信 ==
SentinelA -> Master: PUBLISH __sentinel__:hello [payload]
Master -> SentinelB: 转发hello消息
Master -> SentinelC: 转发hello消息
SentinelB -> SentinelB: 更新SentinelA的状态信息
SentinelC -> SentinelC: 更新SentinelA的状态信息

SentinelB -> Master: PUBLISH __sentinel__:hello [payload]
Master -> SentinelA: 转发hello消息
Master -> SentinelC: 转发hello消息
SentinelA -> SentinelA: 更新SentinelB的状态信息
SentinelC -> SentinelC: 更新SentinelB的状态信息

== 故障检测 ==
note over Master: 主节点故障
SentinelA -> Master: PING (超时)
SentinelA -> SentinelA: 标记主节点为主观下线(SDOWN)

SentinelB -> Master: PING (超时)
SentinelB -> SentinelB: 标记主节点为主观下线(SDOWN)

SentinelC -> Master: PING (超时)
SentinelC -> SentinelC: 标记主节点为主观下线(SDOWN)

== 客观下线判定 ==
SentinelA -> SentinelB: SENTINEL is-master-down-by-addr
SentinelA -> SentinelC: SENTINEL is-master-down-by-addr
SentinelB --> SentinelA: 回复主节点已下线
SentinelC --> SentinelA: 回复主节点已下线
SentinelA -> SentinelA: 标记主节点为客观下线(ODOWN)

@enduml
```

## Sentinel故障转移流程

```plantuml
@startuml
!theme plain
skinparam backgroundColor white
skinparam sequenceMessageAlign center

title Redis Sentinel故障转移流程

participant "Sentinel节点A" as SentinelA #LightPink
participant "Sentinel节点B" as SentinelB #LightPink
participant "Sentinel节点C" as SentinelC #LightPink
participant "Redis主节点\nMaster" as Master #LightBlue
participant "Redis从节点1\nReplica 1" as Slave1 #LightGreen
participant "Redis从节点2\nReplica 2" as Slave2 #LightGreen
participant "客户端\nClient" as Client

== 领导者选举 ==
SentinelA -> SentinelA: 增加当前纪元(current_epoch)
SentinelA -> SentinelB: SENTINEL is-master-down-by-addr [请求投票]
SentinelA -> SentinelC: SENTINEL is-master-down-by-addr [请求投票]
SentinelB --> SentinelA: 投票给SentinelA
SentinelC --> SentinelA: 投票给SentinelA
SentinelA -> SentinelA: 确认自己为领导者

== 选择新主节点 ==
SentinelA -> SentinelA: 开始故障转移
SentinelA -> SentinelA: 状态: WAIT_START
SentinelA -> SentinelA: 状态: SELECT_SLAVE
SentinelA -> Slave1: INFO
Slave1 --> SentinelA: 返回INFO信息
SentinelA -> Slave2: INFO
Slave2 --> SentinelA: 返回INFO信息
SentinelA -> SentinelA: 选择Slave1作为最佳从节点

== 提升新主节点 ==
SentinelA -> SentinelA: 状态: SEND_SLAVEOF_NOONE
SentinelA -> Slave1: SLAVEOF NO ONE
Slave1 --> SentinelA: OK
Slave1 -> Slave1: 转变为主节点

SentinelA -> SentinelA: 状态: WAIT_PROMOTION
SentinelA -> Slave1: INFO
Slave1 --> SentinelA: 角色=master
SentinelA -> SentinelA: 确认提升成功

== 重新配置其他从节点 ==
SentinelA -> SentinelA: 状态: RECONF_SLAVES
SentinelA -> Slave2: SLAVEOF Slave1的IP PORT
Slave2 --> SentinelA: OK
Slave2 -> Slave2: 重新配置为Slave1的从节点
Slave2 -> Slave1: PSYNC
Slave1 --> Slave2: 同步数据

== 更新配置 ==
SentinelA -> SentinelA: 状态: UPDATE_CONFIG
SentinelA -> SentinelA: 更新配置
SentinelA -> SentinelA: 发布+switch-master事件
SentinelA -> SentinelA: 持久化配置到磁盘

== 通知其他Sentinel和客户端 ==
SentinelA -> SentinelB: PUBLISH __sentinel__:hello [新配置]
SentinelA -> SentinelC: PUBLISH __sentinel__:hello [新配置]
SentinelB -> SentinelB: 更新配置
SentinelC -> SentinelC: 更新配置

Client -> SentinelA: 获取主节点信息
SentinelA --> Client: 返回新主节点(Slave1)地址
Client -> Slave1: 连接到新主节点
Slave1 --> Client: 连接成功

@enduml
```

## Sentinel状态机

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Redis Sentinel故障转移状态机

state "SENTINEL_FAILOVER_STATE_NONE" as NONE
state "SENTINEL_FAILOVER_STATE_WAIT_START" as WAIT_START
state "SENTINEL_FAILOVER_STATE_SELECT_SLAVE" as SELECT_SLAVE
state "SENTINEL_FAILOVER_STATE_SEND_SLAVEOF_NOONE" as SEND_SLAVEOF_NOONE
state "SENTINEL_FAILOVER_STATE_WAIT_PROMOTION" as WAIT_PROMOTION
state "SENTINEL_FAILOVER_STATE_RECONF_SLAVES" as RECONF_SLAVES
state "SENTINEL_FAILOVER_STATE_UPDATE_CONFIG" as UPDATE_CONFIG

[*] --> NONE
NONE --> WAIT_START: 检测到主节点客观下线\nsentinelStartFailover()
WAIT_START --> SELECT_SLAVE: 当前Sentinel被选为领导者
SELECT_SLAVE --> SEND_SLAVEOF_NOONE: 选择最佳从节点
SEND_SLAVEOF_NOONE --> WAIT_PROMOTION: 发送SLAVEOF NO ONE命令
WAIT_PROMOTION --> RECONF_SLAVES: 从节点成功晋升为主节点
RECONF_SLAVES --> UPDATE_CONFIG: 所有从节点重新配置完成
UPDATE_CONFIG --> NONE: 更新Sentinel配置

WAIT_START --> NONE: 选举超时或失败
SELECT_SLAVE --> NONE: 没有合适的从节点
SEND_SLAVEOF_NOONE --> NONE: 命令发送失败或超时
WAIT_PROMOTION --> NONE: 提升超时
RECONF_SLAVES --> NONE: 重配置超时

@enduml
```

## Sentinel模式的优缺点

### 优点
1. **高可用性**：自动故障检测和转移
2. **分布式监控**：多个Sentinel共同监控，避免单点故障
3. **客户端支持**：提供服务发现机制，客户端可以查询当前主节点
4. **通知功能**：可以配置通知脚本，在故障发生时通知管理员

### 缺点
1. **部署复杂**：需要部署多个Sentinel节点
2. **额外资源消耗**：Sentinel节点需要额外的服务器资源
3. **不支持分片**：无法解决数据量过大的问题
4. **故障转移期间短暂不可用**：在故障转移过程中可能有短暂的服务不可用

## Sentinel模式配置示例

### Sentinel配置 (sentinel.conf)
```conf
port 26379
daemonize yes
pidfile /var/run/redis-sentinel.pid
logfile /var/log/redis/sentinel.log
dir /tmp

# 监控的主节点，名称为mymaster，至少需要2个Sentinel同意才能进行故障转移
sentinel monitor mymaster 127.0.0.1 6379 2

# 30秒内无法连接主节点则判定为主观下线
sentinel down-after-milliseconds mymaster 30000

# 故障转移超时时间
sentinel failover-timeout mymaster 180000

# 同时进行复制的从节点数量
sentinel parallel-syncs mymaster 1

# 通知脚本
# sentinel notification-script mymaster /var/redis/notify.sh

# 客户端重新配置脚本
# sentinel client-reconfig-script mymaster /var/redis/reconfig.sh
```

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
replicaof 127.0.0.1 6379
replica-read-only yes
```
