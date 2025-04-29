# Redis节点交互总结

Redis提供了多种部署模式，每种模式中节点之间的交互方式和关系各不相同。本文总结了Redis各种部署模式下的节点类型、关系和交互方式。

## Redis节点类型概览

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Redis节点类型概览

package "Redis节点类型" {
  class "单机节点" as Standalone {
    + 独立运行
    + 无特殊关系
    + 处理所有读写请求
  }
  
  class "主节点" as Master {
    + 处理写请求
    + 数据同步到从节点
    + 可处理读请求
  }
  
  class "从节点" as Slave {
    + 从主节点复制数据
    + 默认只读
    + 分担读负载
    + 可在故障时提升为主节点
  }
  
  class "Sentinel节点" as Sentinel {
    + 监控Redis节点
    + 检测节点故障
    + 执行故障转移
    + 提供配置服务
  }
  
  class "集群节点" as Cluster {
    + 负责部分数据分片
    + 参与集群状态维护
    + 可以是主节点或从节点
    + 通过集群总线通信
  }
}

Standalone -[hidden]-> Master
Master --> Slave : 数据复制
Sentinel --> Master : 监控
Sentinel --> Slave : 监控
Cluster --> Cluster : 集群通信

@enduml
```

## 各模式节点交互对比

| 特性 | 单机模式 | 主从模式 | Sentinel模式 | 集群模式 |
|------|---------|---------|------------|---------|
| 节点类型 | 单一Redis节点 | 主节点、从节点 | 主节点、从节点、Sentinel节点 | 集群主节点、集群从节点 |
| 数据分布 | 单节点存储全部数据 | 全节点存储全部数据 | 全节点存储全部数据 | 数据分片存储在多节点 |
| 节点通信 | 无 | 主从复制协议 | 主从复制协议、Sentinel协议 | 集群总线、主从复制协议 |
| 故障检测 | 无 | 无自动检测 | Sentinel主观/客观下线检测 | 集群PFAIL/FAIL机制 |
| 故障转移 | 无 | 无自动转移 | Sentinel自动选举和转移 | 集群自动选举和转移 |
| 客户端连接 | 直接连接 | 直接连接主/从 | 通过Sentinel获取主节点 | 连接任意节点，自动重定向 |

## 节点间通信协议

Redis节点间的通信使用多种协议，根据不同的部署模式和功能需求：

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Redis节点间通信协议

package "Redis通信协议" {
  class "RESP协议" as RESP {
    + Redis序列化协议
    + 客户端与服务器通信
    + 简单高效的文本协议
  }
  
  class "复制协议" as Replication {
    + 主从节点间数据同步
    + 全量同步(RDB传输)
    + 增量同步(命令传播)
    + PSYNC命令
  }
  
  class "Sentinel协议" as SentinelProtocol {
    + Sentinel节点间通信
    + 基于Redis发布/订阅
    + SENTINEL命令集
    + 领导者选举
  }
  
  class "集群协议" as ClusterProtocol {
    + 集群节点间通信
    + 集群总线(TCP)
    + Gossip协议
    + 槽位分配
    + 故障检测
  }
}

RESP <-- Replication : 基于
RESP <-- SentinelProtocol : 基于
RESP <-- ClusterProtocol : 基于

@enduml
```

## 各模式下的节点交互流程

### 1. 主从模式交互流程

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title 主从模式节点交互流程

actor "客户端\nClient" as Client
participant "Redis主节点\nMaster" as Master #LightBlue
participant "Redis从节点\nReplica" as Slave #LightGreen

== 复制初始化 ==
Slave -> Master: REPLICAOF <masterip> <port>
Master --> Slave: 确认连接

Slave -> Master: PSYNC ? -1
Master -> Master: BGSAVE生成RDB文件
Master --> Slave: 发送RDB文件
Slave -> Slave: 加载RDB数据

== 命令传播 ==
Client -> Master: SET key value
Master -> Master: 执行命令
Master --> Client: OK
Master --> Slave: 传播写命令
Slave -> Slave: 执行相同命令

== 心跳检测 ==
Slave -> Master: REPLCONF ACK <offset>
Master -> Master: 更新从节点复制偏移量

@enduml
```

### 2. Sentinel模式交互流程

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Sentinel模式节点交互流程

participant "Sentinel节点A" as SentinelA #LightPink
participant "Sentinel节点B" as SentinelB #LightPink
participant "Redis主节点\nMaster" as Master #LightBlue
participant "Redis从节点\nReplica" as Slave #LightGreen

== 监控阶段 ==
SentinelA -> Master: PING (每秒)
Master --> SentinelA: PONG
SentinelA -> Master: INFO (每10秒)
Master --> SentinelA: 返回INFO信息

SentinelA -> Slave: PING
Slave --> SentinelA: PONG
SentinelA -> Slave: INFO
Slave --> SentinelA: 返回INFO信息

== Sentinel之间的通信 ==
SentinelA -> Master: PUBLISH __sentinel__:hello [payload]
Master -> SentinelB: 转发hello消息
SentinelB -> SentinelB: 更新SentinelA的状态信息

SentinelB -> Master: PUBLISH __sentinel__:hello [payload]
Master -> SentinelA: 转发hello消息
SentinelA -> SentinelA: 更新SentinelB的状态信息

== 故障检测 ==
note over Master: 主节点故障
SentinelA -> Master: PING (超时)
SentinelA -> SentinelA: 标记主节点为主观下线(SDOWN)
SentinelA -> SentinelB: SENTINEL is-master-down-by-addr
SentinelB --> SentinelA: 回复主节点已下线
SentinelA -> SentinelA: 标记主节点为客观下线(ODOWN)

@enduml
```

### 3. 集群模式交互流程

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title 集群模式节点交互流程

actor "客户端\nClient" as Client
participant "集群节点A\n(主节点)" as NodeA #LightBlue
participant "集群节点B\n(主节点)" as NodeB #LightBlue
participant "集群节点A'\n(从节点)" as NodeA1 #LightGreen

== 集群通信 ==
NodeA -> NodeB: PING (Gossip协议)
NodeB --> NodeA: PONG (包含集群状态)
NodeA -> NodeA: 更新集群状态视图

NodeA -> NodeA1: PING
NodeA1 --> NodeA: PONG
NodeA -> NodeA: 更新集群状态视图

== 数据操作 ==
Client -> NodeA: GET key1 (key1映射到NodeA负责的槽)
NodeA --> Client: "value1"

Client -> NodeA: GET key2 (key2映射到NodeB负责的槽)
NodeA --> Client: MOVED <slot> <ip:port>
Client -> NodeB: GET key2
NodeB --> Client: "value2"

Client -> NodeA: SET key1 newvalue
NodeA -> NodeA: 执行命令
NodeA --> Client: OK
NodeA -> NodeA1: 同步写命令
NodeA1 -> NodeA1: 执行相同命令

@enduml
```

## 节点状态转换

Redis节点在不同模式下会经历不同的状态转换，特别是在故障检测和恢复过程中：

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

title Redis节点状态转换

state "主节点\n(Master)" as Master {
  state "正常运行" as MasterNormal
  state "故障" as MasterFail
  
  MasterNormal --> MasterFail : 节点崩溃/网络故障
  MasterFail --> MasterNormal : 节点恢复
}

state "从节点\n(Replica)" as Replica {
  state "复制中" as SlaveReplicating
  state "断开连接" as SlaveDisconnected
  state "提升为主节点" as SlavePromoted
  
  SlaveReplicating --> SlaveDisconnected : 连接断开
  SlaveDisconnected --> SlaveReplicating : 重新连接
  SlaveDisconnected --> SlavePromoted : 故障转移
  SlavePromoted --> SlaveReplicating : 原主节点恢复
}

state "Sentinel节点" as Sentinel {
  state "监控中" as SentinelMonitoring
  state "故障检测" as SentinelDetecting
  state "执行故障转移" as SentinelFailover
  
  SentinelMonitoring --> SentinelDetecting : 检测到异常
  SentinelDetecting --> SentinelFailover : 确认故障
  SentinelFailover --> SentinelMonitoring : 转移完成
}

state "集群节点" as Cluster {
  state "正常" as ClusterNormal
  state "疑似下线(PFAIL)" as ClusterPFail
  state "确认下线(FAIL)" as ClusterFail
  state "故障转移中" as ClusterFailover
  
  ClusterNormal --> ClusterPFail : 节点无响应
  ClusterPFail --> ClusterFail : 多数节点确认
  ClusterFail --> ClusterFailover : 开始故障转移
  ClusterFailover --> ClusterNormal : 转移完成
  ClusterPFail --> ClusterNormal : 节点恢复
}

@enduml
```

## 总结

Redis提供了多种部署模式，从简单的单机模式到复杂的集群模式，以满足不同的可用性、可扩展性和性能需求。各种模式下的节点交互方式各不相同：

1. **单机模式**：最简单的部署方式，无节点间交互
2. **主从模式**：通过复制协议实现数据同步，提高读性能和数据安全性
3. **Sentinel模式**：在主从基础上增加Sentinel节点，通过监控和自动故障转移提高可用性
4. **集群模式**：通过数据分片和集群协议实现水平扩展，同时提供高可用性

了解这些不同模式下的节点交互方式，有助于更好地设计、部署和维护Redis系统，根据实际需求选择合适的部署模式。
