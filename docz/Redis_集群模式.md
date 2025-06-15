# Redis Cluster模式

Redis集群模式是Redis提供的分布式解决方案，允许数据自动分片到多个节点，并提供一定程度的高可用性和故障转移能力。

## 集群模式特点

- 数据自动分片：数据自动分布到多个节点
- 去中心化：无中心节点，所有节点地位平等
- 高可用性：部分节点故障时集群仍能继续工作
- 自动故障转移：主节点故障时自动选举从节点接管
- 水平扩展：可以动态添加节点扩展集群容量

## 集群模式架构

```mermaid
graph TB
    subgraph ClusterArch["Redis集群模式架构"]
        Client["客户端<br/>Client"]

        subgraph RedisCluster["Redis集群"]
            subgraph MasterNodes["主节点"]
                Node1["节点1<br/>主节点<br/>(槽0-5460)"]
                Node2["节点2<br/>主节点<br/>(槽5461-10922)"]
                Node3["节点3<br/>主节点<br/>(槽10923-16383)"]
            end

            subgraph SlaveNodes["从节点"]
                Node4["节点4<br/>从节点<br/>(复制节点1)"]
                Node5["节点5<br/>从节点<br/>(复制节点2)"]
                Node6["节点6<br/>从节点<br/>(复制节点3)"]
            end
        end
    end

    %% 客户端连接
    Client -->|读/写请求| Node1
    Client -->|读/写请求| Node2
    Client -->|读/写请求| Node3
    Client -->|读请求| Node4
    Client -->|读请求| Node5
    Client -->|读请求| Node6

    %% 集群总线通信
    Node1 <-->|集群总线通信| Node2
    Node2 <-->|集群总线通信| Node3
    Node3 <-->|集群总线通信| Node1

    %% 主从数据同步
    Node1 <-->|数据同步| Node4
    Node2 <-->|数据同步| Node5
    Node3 <-->|数据同步| Node6

    %% 从节点间通信
    Node4 <-->|集群总线通信| Node5
    Node5 <-->|集群总线通信| Node6
    Node6 <-->|集群总线通信| Node4

    %% 注释
    Node1 -.->|说明1| Note1["每个主节点负责一部分哈希槽<br/>(总共16384个槽)"]
    Node4 -.->|说明2| Note2["每个主节点至少有一个从节点<br/>提供高可用性"]

    %% 样式
    classDef clientStyle fill:#e3f2fd,stroke:#1976d2
    classDef masterStyle fill:#bbdefb,stroke:#1976d2
    classDef slaveStyle fill:#c8e6c9,stroke:#388e3c
    classDef noteStyle fill:#fff3e0,stroke:#f57c00

    class Client clientStyle
    class Node1,Node2,Node3 masterStyle
    class Node4,Node5,Node6 slaveStyle
    class Note1,Note2 noteStyle
```

## 集群数据分片原理

```mermaid
sequenceDiagram
    participant Client as 客户端<br/>Client
    participant Node as Redis集群节点
    participant Slot1 as 哈希槽0-5460
    participant Slot2 as 哈希槽5461-10922
    participant Slot3 as 哈希槽10923-16383

    Note over Client,Slot3: Redis集群数据分片原理

    Client->>Node: SET key1 value1
    Node->>Node: CRC16(key1) % 16384 = 槽X

    alt 槽X在当前节点
        Node->>Node: 在本地执行命令
        Node-->>Client: OK
    else 槽X不在当前节点
        Node-->>Client: MOVED 槽X 目标节点地址
        Client->>Node: 重定向到正确节点
        Node->>Node: 在本地执行命令
        Node-->>Client: OK
    end

    Note over Node: 使用CRC16算法计算键的哈希值<br/>对16384取模得到槽位<br/>根据槽位确定负责的节点
```

## 集群节点通信原理

```mermaid
sequenceDiagram
    participant NodeA as 节点A<br/>(主节点)
    participant NodeB as 节点B<br/>(主节点)
    participant NodeC as 节点C<br/>(主节点)
    participant NodeA1 as 节点A'<br/>(从节点)

    Note over NodeA,NodeA1: Redis集群节点通信原理

    Note over NodeA,NodeC: 集群总线通信(Gossip协议)
    NodeA->>NodeB: PING (包含部分集群状态)
    NodeB-->>NodeA: PONG (包含部分集群状态)
    NodeA->>NodeA: 更新集群状态视图

    NodeB->>NodeC: PING
    NodeC-->>NodeB: PONG
    NodeB->>NodeB: 更新集群状态视图

    NodeC->>NodeA: PING
    NodeA-->>NodeC: PONG
    NodeC->>NodeC: 更新集群状态视图

    Note over NodeA,NodeA1: 主从复制
    NodeA->>NodeA1: 数据同步
    NodeA1->>NodeA: REPLCONF ACK

    Note over NodeA,NodeC: 故障检测
    NodeA->>NodeB: PING (超时)
    NodeA->>NodeA: 标记NodeB为PFAIL(疑似下线)
    NodeA->>NodeC: PING (包含NodeB的PFAIL信息)
    NodeC->>NodeC: 也检测到NodeB无响应
    NodeC->>NodeC: 标记NodeB为PFAIL
    NodeC-->>NodeA: PONG (确认NodeB的PFAIL状态)
    NodeA->>NodeA: 收集足够PFAIL报告
    NodeA->>NodeA: 将NodeB标记为FAIL(确认下线)
    NodeA->>NodeC: 广播NodeB的FAIL状态
    NodeC->>NodeC: 标记NodeB为FAIL
```

## 集群故障转移流程

```mermaid
sequenceDiagram
    participant NodeA as 节点A<br/>(主节点)
    participant NodeB as 节点B<br/>(主节点)
    participant NodeC as 节点C<br/>(主节点)
    participant NodeB1 as 节点B1<br/>(从节点)
    participant NodeB2 as 节点B2<br/>(从节点)
    participant Client as 客户端<br/>Client

    Note over NodeA,Client: Redis集群故障转移流程

    Note over NodeA,NodeB2: 故障检测
    Note over NodeB: 节点故障
    NodeA->>NodeB: PING (超时)
    NodeA->>NodeA: 标记NodeB为PFAIL(疑似下线)
    NodeC->>NodeB: PING (超时)
    NodeC->>NodeC: 标记NodeB为PFAIL

    NodeA->>NodeC: PING (包含NodeB的PFAIL信息)
    NodeC-->>NodeA: PONG (确认NodeB的PFAIL状态)
    NodeA->>NodeA: 收集足够PFAIL报告
    NodeA->>NodeA: 将NodeB标记为FAIL(确认下线)
    NodeA->>NodeC: 广播NodeB的FAIL状态
    NodeA->>NodeB1: 广播NodeB的FAIL状态
    NodeA->>NodeB2: 广播NodeB的FAIL状态
    NodeC->>NodeC: 标记NodeB为FAIL

    Note over NodeB1,NodeB2: 从节点选举
    NodeB1->>NodeB1: 检测到主节点NodeB已FAIL
    NodeB2->>NodeB2: 检测到主节点NodeB已FAIL
    NodeB1->>NodeB1: 延迟一个随机时间
    NodeB2->>NodeB2: 延迟一个随机时间
    NodeB1->>NodeA: FAILOVER_AUTH_REQUEST (请求选票)
    NodeB1->>NodeC: FAILOVER_AUTH_REQUEST (请求选票)
    NodeA-->>NodeB1: FAILOVER_AUTH_ACK (投票支持)
    NodeC-->>NodeB1: FAILOVER_AUTH_ACK (投票支持)
    NodeB1->>NodeB1: 获得多数选票，当选为新主节点

    Note over NodeB1,NodeB2: 从节点提升
    NodeB1->>NodeB1: 提升自己为主节点
    NodeB1->>NodeB1: 接管NodeB的哈希槽
    NodeB1->>NodeA: PONG (包含新角色和槽信息)
    NodeB1->>NodeC: PONG (包含新角色和槽信息)
    NodeB1->>NodeB2: PONG (包含新角色和槽信息)
    NodeA->>NodeA: 更新集群状态
    NodeC->>NodeC: 更新集群状态
    NodeB2->>NodeB2: 更新集群状态

    NodeB2->>NodeB1: REPLICATE (成为NodeB1的从节点)
    NodeB1-->>NodeB2: OK
    NodeB1->>NodeB2: 同步数据

    Note over Client,NodeB1: 客户端重定向
    Client->>NodeA: GET key (key在NodeB负责的槽中)
    NodeA-->>Client: MOVED 槽X NodeB1地址
    Client->>NodeB1: GET key
    NodeB1-->>Client: value
```

## 集群模式启动流程

```mermaid
flowchart TD
    Start([开始]) --> PrepareNodes[准备多个Redis节点]
    PrepareNodes --> ConfigCluster[配置每个节点启用集群模式]
    ConfigCluster --> StartNodes[启动所有Redis节点]
    StartNodes --> CreateCluster[使用redis-cli创建集群]
    CreateCluster --> AllocateSlots[分配哈希槽]
    AllocateSlots --> EstablishRelation[建立主从关系]
    EstablishRelation --> NodeHandshake[节点间握手]
    NodeHandshake --> ClusterReady[集群完成初始化]
    ClusterReady --> End([结束])

    %% 注释
    CreateCluster -.->|命令示例| Note1["redis-cli --cluster create<br/>127.0.0.1:7000 127.0.0.1:7001 127.0.0.1:7002<br/>127.0.0.1:7003 127.0.0.1:7004 127.0.0.1:7005<br/>--cluster-replicas 1"]
    AllocateSlots -.->|说明| Note2["16384个哈希槽<br/>平均分配给主节点"]
    EstablishRelation -.->|说明| Note3["每个主节点分配<br/>指定数量的从节点"]
    NodeHandshake -.->|说明| Note4["交换集群状态信息<br/>建立集群总线连接"]

    %% 样式
    classDef startEnd fill:#c8e6c9,stroke:#388e3c
    classDef process fill:#e1f5fe,stroke:#1976d2
    classDef noteStyle fill:#fff3e0,stroke:#f57c00

    class Start,End startEnd
    class PrepareNodes,ConfigCluster,StartNodes,CreateCluster,AllocateSlots,EstablishRelation,NodeHandshake,ClusterReady process
    class Note1,Note2,Note3,Note4 noteStyle
```

## 集群模式的优缺点

### 优点
1. **水平扩展**：可以通过添加节点来扩展集群容量
2. **高可用性**：主节点故障时自动进行故障转移
3. **分布式存储**：数据自动分片到多个节点
4. **去中心化**：无中心节点，避免单点故障

### 缺点
1. **事务限制**：不支持跨槽的事务操作
2. **数据迁移开销**：节点加入/删除时需要迁移槽和数据
3. **客户端复杂性**：客户端需要处理重定向和集群拓扑变化
4. **一致性保证有限**：在特定场景下可能出现数据不一致

## 集群模式配置示例

### 集群节点配置 (redis-7000.conf)
```conf
port 7000
cluster-enabled yes
cluster-config-file nodes-7000.conf
cluster-node-timeout 5000
appendonly yes
dir ./redis-cluster/7000
daemonize yes
protected-mode no
bind 0.0.0.0
pidfile /var/run/redis_7000.pid
logfile /var/log/redis/redis_7000.log
```

### 集群节点配置 (redis-7001.conf)
```conf
port 7001
cluster-enabled yes
cluster-config-file nodes-7001.conf
cluster-node-timeout 5000
appendonly yes
dir ./redis-cluster/7001
daemonize yes
protected-mode no
bind 0.0.0.0
pidfile /var/run/redis_7001.pid
logfile /var/log/redis/redis_7001.log
```

### 创建集群命令
```bash
redis-cli --cluster create 127.0.0.1:7000 127.0.0.1:7001 127.0.0.1:7002 \
127.0.0.1:7003 127.0.0.1:7004 127.0.0.1:7005 --cluster-replicas 1
```

## 集群常用操作命令

```bash
# 检查集群状态
redis-cli -c -p 7000 cluster info

# 查看集群节点
redis-cli -c -p 7000 cluster nodes

# 查看槽位分配
redis-cli -c -p 7000 cluster slots

# 添加新主节点
redis-cli --cluster add-node 127.0.0.1:7006 127.0.0.1:7000

# 添加从节点
redis-cli --cluster add-node 127.0.0.1:7007 127.0.0.1:7000 --cluster-slave --cluster-master-id <master-node-id>

# 重新分片
redis-cli --cluster reshard 127.0.0.1:7000
```
