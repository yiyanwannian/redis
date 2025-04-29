# Redis Sentinel实现原理

## 1. Redis Sentinel概述

Redis Sentinel是一个为Redis提供高可用性的分布式系统。它监控Redis主节点和从节点实例，检测故障，并在主节点不可用时执行自动故障转移。Sentinel在`sentinel.c`文件中实现，作为Redis服务器的一种特殊模式运行。

## 2. 核心数据结构

### 2.1 Sentinel主状态结构

Sentinel的核心是`sentinelState`结构（第233-256行），它维护了：

```c
struct sentinelState {
    char myid[CONFIG_RUN_ID_SIZE+1]; /* 当前Sentinel的ID */
    uint64_t current_epoch;         /* 当前纪元 */
    dict *masters;      /* 主节点sentinelRedisInstance字典 */
    int tilt;           /* 是否处于TILT模式 */
    int running_scripts;    /* 当前正在执行的脚本数量 */
    mstime_t tilt_start_time;       /* TILT模式开始时间 */
    mstime_t previous_time;         /* 上次运行时间处理器的时间 */
    list *scripts_queue;            /* 用户脚本执行队列 */
    char *announce_ip;  /* 向其他Sentinel公告的IP地址 */
    int announce_port;  /* 向其他Sentinel公告的端口 */
    unsigned long simfailure_flags; /* 故障模拟标志 */
    int deny_scripts_reconfig; /* 是否允许通过SENTINEL SET更改脚本路径 */
    char *sentinel_auth_pass;    /* 用于对其他Sentinel进行AUTH的密码 */
    char *sentinel_auth_user;    /* 用于对其他Sentinel进行ACL AUTH的用户名 */
    int resolve_hostnames;       /* 是否支持使用主机名 */
    int announce_hostnames;      /* 是否公告主机名而不是IP */
};
```

### 2.2 Redis实例表示

每个Redis实例（主节点、从节点或Sentinel）由`sentinelRedisInstance`结构表示（第161-231行）：

```c
typedef struct sentinelRedisInstance {
    int flags;      /* 见SRI_...定义 */
    char *name;     /* 从该Sentinel角度看的主节点名称 */
    char *runid;    /* 该实例的运行ID，或者如果是Sentinel则为唯一ID */
    uint64_t config_epoch;  /* 配置纪元 */
    sentinelAddr *addr; /* 主机地址 */
    instanceLink *link; /* 到实例的链接，对于Sentinel可能是共享的 */
    mstime_t last_pub_time;   /* 上次通过Pub/Sub发送hello的时间 */
    mstime_t last_hello_time; /* 仅当SRI_SENTINEL设置时使用。上次通过Pub/Sub
                                 从该Sentinel接收hello的时间 */
    mstime_t last_master_down_reply_time; /* 上次回复SENTINEL 
                                             is-master-down命令的时间 */
    mstime_t s_down_since_time; /* 主观下线的开始时间 */
    mstime_t o_down_since_time; /* 客观下线的开始时间 */
    mstime_t down_after_period; /* 在该时间段后认为实例下线 */
    /* ... 更多字段 ... */
} sentinelRedisInstance;
```

### 2.3 连接管理

Sentinel使用`instanceLink`结构（第134-159行）管理与Redis实例的连接：

```c
typedef struct instanceLink {
    int refcount;          /* sentinelRedisInstance拥有者的数量 */
    int disconnected;      /* 如果需要重新连接cc或pc，则非零 */
    int pending_commands;  /* 已发送等待回复的命令数量 */
    redisAsyncContext *cc; /* 用于命令的hiredis上下文 */
    redisAsyncContext *pc; /* 用于发布/订阅的hiredis上下文 */
    mstime_t cc_conn_time; /* cc连接时间 */
    mstime_t pc_conn_time; /* pc连接时间 */
    /* ... 更多字段 ... */
} instanceLink;
```

## 3. 初始化和配置

### 3.1 Sentinel初始化

Sentinel的初始化过程在`initSentinel()`函数（第469-487行）中进行：

```c
void initSentinel(void) {
    /* 初始化各种数据结构 */
    sentinel.current_epoch = 0;
    sentinel.masters = dictCreate(&instancesDictType);
    sentinel.tilt = 0;
    sentinel.tilt_start_time = 0;
    sentinel.previous_time = mstime();
    sentinel.running_scripts = 0;
    sentinel.scripts_queue = listCreate();
    sentinel.announce_ip = NULL;
    sentinel.announce_port = 0;
    sentinel.simfailure_flags = SENTINEL_SIMFAILURE_NONE;
    sentinel.deny_scripts_reconfig = SENTINEL_DEFAULT_DENY_SCRIPTS_RECONFIG;
    sentinel.sentinel_auth_pass = NULL;
    sentinel.sentinel_auth_user = NULL;
    sentinel.resolve_hostnames = SENTINEL_DEFAULT_RESOLVE_HOSTNAMES;
    sentinel.announce_hostnames = SENTINEL_DEFAULT_ANNOUNCE_HOSTNAMES;
    memset(sentinel.myid,0,sizeof(sentinel.myid));
    server.sentinel_config = NULL;
}
```

### 3.2 配置加载

Sentinel从Redis配置文件加载其配置，包括：
- 要监控的主节点实例
- 故障检测的法定人数设置
- 故障转移超时和参数
- 认证凭据

## 4. 监控过程

### 4.1 实例连接

Sentinel与每个被监控的实例建立两种连接：
1. **命令连接**（`link->cc`）：用于发送PING、INFO等命令
2. **发布/订阅连接**（`link->pc`）：用于订阅Sentinel通道

### 4.2 周期性命令

`sentinelSendPeriodicCommands()`函数（第3094-3157行）定期发送三种类型的命令：

```c
void sentinelSendPeriodicCommands(sentinelRedisInstance *ri) {
    /* ... */
    
    /* 向主节点和从节点发送INFO，不向Sentinel发送 */
    if ((ri->flags & SRI_SENTINEL) == 0 &&
        (ri->info_refresh == 0 ||
        (now - ri->info_refresh) > info_period))
    {
        /* ... 发送INFO命令 ... */
    }

    /* 向所有三种类型的实例发送PING */
    if ((now - ri->link->last_pong_time) > ping_period &&
               (now - ri->link->last_ping_time) > ping_period/2) {
        sentinelSendPing(ri);
    }

    /* 向所有三种类型的实例发布hello消息 */
    if ((now - ri->last_pub_time) > sentinel_publish_period) {
        sentinelSendHello(ri);
    }
}
```

### 4.3 健康检查

Sentinel实现了两级下线状态检测：

1. **主观下线（SDOWN）**：当单个Sentinel无法连接到主节点时
   ```c
   void sentinelCheckSubjectivelyDown(sentinelRedisInstance *ri) {
       mstime_t elapsed = mstime() - ri->link->last_avail_time;
       
       if (elapsed > ri->down_after_period) {
           /* 标记为主观下线 */
           ri->flags |= SRI_S_DOWN;
           /* ... */
       }
   }
   ```

2. **客观下线（ODOWN）**：当足够多的Sentinel同意主节点已下线时
   ```c
   void sentinelCheckObjectivelyDown(sentinelRedisInstance *ri) {
       /* 检查是否有足够多的Sentinel报告该主节点为SDOWN */
       if (sentinelGetQuorumCount(ri) >= ri->quorum) {
           /* 标记为客观下线 */
           ri->flags |= SRI_O_DOWN;
           /* ... */
       }
   }
   ```

## 5. 故障转移过程

故障转移过程是Sentinel的核心功能，由几个状态组成：

### 5.1 故障转移启动

当主节点被标记为客观下线（ODOWN）时，故障转移开始：

```c
int sentinelStartFailoverIfNeeded(sentinelRedisInstance *master) {
    /* 如果主节点不是O_DOWN状态，我们不能进行故障转移 */
    if (!(master->flags & SRI_O_DOWN)) return 0;

    /* 故障转移已经在进行中？ */
    if (master->flags & SRI_FAILOVER_IN_PROGRESS) return 0;

    /* 上次故障转移尝试开始时间太近？ */
    if (mstime() - master->failover_start_time < master->failover_timeout*2) {
        /* ... */
        return 0;
    }

    sentinelStartFailover(master);
    return 1;
}
```

### 5.2 领导者选举

Sentinel使用领导者选举过程来确定哪个Sentinel将协调故障转移：

```c
void sentinelFailoverWaitStart(sentinelRedisInstance *ri) {
    char *leader;
    int isleader;

    /* 检查我们是否是该故障转移纪元的领导者 */
    leader = sentinelGetLeader(ri, ri->failover_epoch);
    isleader = leader && strcasecmp(leader,sentinel.myid) == 0;
    sdsfree(leader);

    /* 如果我不是领导者，并且不是通过SENTINEL FAILOVER强制故障转移，
     * 那么我不能继续故障转移 */
    if (!isleader && !(ri->flags & SRI_FORCE_FAILOVER)) {
        /* ... */
        return;
    }
    
    /* 继续故障转移 */
    /* ... */
}
```

### 5.3 故障转移状态机

故障转移过程被实现为一个状态机，具有以下状态：

1. **WAIT_START**：等待领导者选举完成
2. **SELECT_SLAVE**：选择最佳从节点进行提升
3. **SEND_SLAVEOF_NOONE**：向选定的从节点发送SLAVEOF NO ONE
4. **WAIT_PROMOTION**：等待从节点成为主节点
5. **RECONF_SLAVES**：重新配置其他从节点以跟随新主节点
6. **UPDATE_CONFIG**：更新Sentinel的配置

```c
void sentinelFailoverStateMachine(sentinelRedisInstance *ri) {
    serverAssert(ri->flags & SRI_MASTER);

    if (!(ri->flags & SRI_FAILOVER_IN_PROGRESS)) return;

    switch(ri->failover_state) {
        case SENTINEL_FAILOVER_STATE_WAIT_START:
            sentinelFailoverWaitStart(ri);
            break;
        case SENTINEL_FAILOVER_STATE_SELECT_SLAVE:
            sentinelFailoverSelectSlave(ri);
            break;
        case SENTINEL_FAILOVER_STATE_SEND_SLAVEOF_NOONE:
            sentinelFailoverSendSlaveOfNoOne(ri);
            break;
        case SENTINEL_FAILOVER_STATE_WAIT_PROMOTION:
            sentinelFailoverWaitPromotion(ri);
            break;
        case SENTINEL_FAILOVER_STATE_RECONF_SLAVES:
            sentinelFailoverReconfNextSlave(ri);
            break;
    }
}
```

### 5.4 从节点选择

Sentinel根据几个标准选择最佳从节点进行提升：

```c
sentinelRedisInstance *sentinelSelectSlave(sentinelRedisInstance *master) {
    sentinelRedisInstance *slave = NULL, *selected = NULL;
    
    /* 基于以下条件选择：
     * 1. 复制偏移量（最新的）
     * 2. 优先级（越低越好）
     * 3. 运行ID（字典序较小的）
     */
    
    /* ... 选择逻辑 ... */
    
    return selected;
}
```

### 5.5 配置更新

成功故障转移后，Sentinel更新其配置并将其持久化到磁盘：

```c
void sentinelFailoverSwitchToPromotedSlave(sentinelRedisInstance *master) {
    sentinelRedisInstance *ref = master->promoted_slave ?
                                 master->promoted_slave : master;

    sentinelEvent(LL_WARNING,"+switch-master",master,"%s %s %d %s %d",
        master->name, announceSentinelAddr(master->addr), master->addr->port,
        announceSentinelAddr(ref->addr), ref->addr->port);

    sentinelResetMasterAndChangeAddress(master,ref->addr->hostname,ref->addr->port);
}
```

## 6. Sentinel之间的通信

### 6.1 发布/订阅消息

Sentinel通过Redis的发布/订阅机制在`__sentinel__:hello`通道上相互通信：

```c
#define SENTINEL_HELLO_CHANNEL "__sentinel__:hello"

/* 格式化并发送Hello消息 */
snprintf(payload,sizeof(payload),
    "%s,%d,%s,%llu," /* 关于此Sentinel的信息 */
    "%s,%s,%d,%llu", /* 关于当前主节点的信息 */
    announce_ip, announce_port, sentinel.myid,
    (unsigned long long) sentinel.current_epoch,
    /* --- */
    master->name,announceSentinelAddr(master_addr),master_addr->port,
    (unsigned long long) master->config_epoch);
```

### 6.2 Sentinel命令

Sentinel还使用`SENTINEL IS-MASTER-DOWN-BY-ADDR`等命令直接相互通信，以就主节点状态达成共识。

## 7. 客户端通知和脚本

### 7.1 事件通知

Sentinel为重要的状态变化生成事件，并将其发布给客户端：

```c
void sentinelEvent(int level, char *type, sentinelRedisInstance *ri,
                   const char *fmt, ...) {
    /* 如果日志级别允许，记录消息 */
    if (level >= server.verbosity)
        serverLog(level,"%s %s",type,msg);

    /* 如果不是调试消息，通过发布/订阅发布消息 */
    if (level != LL_DEBUG) {
        channel = createStringObject(type,strlen(type));
        payload = createStringObject(msg,strlen(msg));
        pubsubPublishMessage(channel,payload,0);
        decrRefCount(channel);
        decrRefCount(payload);
    }

    /* 如果适用，调用通知脚本 */
    if (level == LL_WARNING && ri != NULL) {
        /* ... 执行通知脚本 ... */
    }
}
```

### 7.2 客户端重新配置

Sentinel可以在故障转移期间执行用户定义的脚本，以通知外部系统：

```c
void sentinelCallClientReconfScript(sentinelRedisInstance *master, int role, 
                                   char *state, sentinelAddr *from, sentinelAddr *to) {
    /* ... 执行客户端重新配置脚本 ... */
}
```

## 8. 持久化和配置管理

### 8.1 配置持久化

Sentinel将其配置持久化到磁盘，以在重启后维持状态：

```c
int sentinelFlushConfig(void) {
    int saved_hz = server.hz;
    int rewrite_status;

    server.hz = CONFIG_DEFAULT_HZ;
    rewrite_status = rewriteConfig(server.configfile, 0);
    server.hz = saved_hz;

    if (rewrite_status == -1) {
        serverLog(LL_WARNING,"警告：Sentinel无法将新配置保存到磁盘!!!: %s", strerror(errno));
        return C_ERR;
    } else {
        serverLog(LL_NOTICE,"Sentinel新配置已保存到磁盘");
        return C_OK;
    }
}
```

### 8.2 运行时配置

Sentinel提供命令以在运行时修改配置：

```c
void sentinelSetCommand(client *c) {
    /* 处理选项如：
     * - down-after-milliseconds
     * - failover-timeout
     * - parallel-syncs
     * - notification-script
     * - client-reconfig-script
     * - auth-pass
     * - quorum
     * 等
     */
}
```

## 9. 总结

Redis Sentinel是一个复杂的分布式系统，通过以下方式为Redis提供高可用性：

1. **监控**：持续检查Redis实例的健康状态
2. **通知**：向客户端通报状态变化
3. **自动故障转移**：在主节点故障时提升从节点
4. **配置提供者**：作为客户端服务发现的权威来源

其实现使用了以下组合：
- 基于hiredis的异步网络通信
- 分布式共识算法
- 基于状态机的故障转移过程
- 使用发布/订阅进行Sentinel间通信

这种架构使Sentinel能够提供可靠的高可用性，而无需复杂的外部协调系统。
