# Redis单机模式

Redis单机模式是最基本的部署方式，只有一个Redis实例提供所有服务。

## 单机模式特点

- 简单易用，配置简单
- 没有数据冗余，单点故障风险高
- 受限于单机内存和计算能力
- 适合开发环境或数据不敏感的场景

## 单机模式架构

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Redis单机模式架构

actor "客户端\nClient" as Client
database "Redis实例\n(单机)" as Redis

Client -down-> Redis : 读/写请求
Redis -up-> Client : 响应

note right of Redis
  单个Redis实例处理所有请求
  数据存储在单机内存中
  没有数据冗余和高可用保障
end note

@enduml
```

## 单机模式启动流程

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Redis单机模式启动流程

start
:加载配置文件;
:初始化服务器状态;
:创建事件循环;
:初始化数据结构;
:打开服务器监听端口;
:初始化数据库;
:启动定时器;
:进入事件循环;
stop

note right: 单机模式下没有主从复制、\nSentinel或集群相关的初始化

@enduml
```

## 单机模式交互时序图

```plantuml
@startuml
!theme plain
skinparam backgroundColor white
skinparam sequenceMessageAlign center

title Redis单机模式交互时序图

participant "客户端A\nClient A" as ClientA
participant "客户端B\nClient B" as ClientB
participant "Redis实例\n(单机)" as Redis

== 连接建立 ==
ClientA -> Redis: 建立TCP连接
Redis --> ClientA: 连接确认

ClientB -> Redis: 建立TCP连接
Redis --> ClientB: 连接确认

== 数据操作 ==
ClientA -> Redis: SET key1 value1
Redis -> Redis: 在内存中设置键值对
Redis --> ClientA: OK

ClientB -> Redis: GET key1
Redis -> Redis: 从内存中读取键值
Redis --> ClientB: "value1"

ClientA -> Redis: INCR counter
Redis -> Redis: 原子递增操作
Redis --> ClientA: 1

ClientB -> Redis: INCR counter
Redis -> Redis: 原子递增操作
Redis --> ClientB: 2

== 事务操作 ==
ClientA -> Redis: MULTI
Redis --> ClientA: OK
ClientA -> Redis: SET key2 value2
Redis --> ClientA: QUEUED
ClientA -> Redis: SET key3 value3
Redis --> ClientA: QUEUED
ClientA -> Redis: EXEC
Redis -> Redis: 原子执行事务中的命令
Redis --> ClientA: [OK, OK]

== 过期和内存管理 ==
ClientA -> Redis: SET key4 value4 EX 10
Redis -> Redis: 设置键值对并设置10秒过期时间
Redis --> ClientA: OK

note over Redis: 10秒后
Redis -> Redis: 惰性删除或\n定期删除过期键

note over Redis: 内存达到maxmemory
Redis -> Redis: 根据淘汰策略\n删除部分键值对

== 连接关闭 ==
ClientA -> Redis: QUIT
Redis --> ClientA: OK
ClientA -> Redis: 关闭TCP连接

@enduml
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
