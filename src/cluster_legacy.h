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

#ifndef CLUSTER_LEGACY_H
#define CLUSTER_LEGACY_H

#define CLUSTER_PORT_INCR 10000 /* Cluster port = baseport + PORT_INCR */

/* The following defines are amount of time, sometimes expressed as
 * multiplicators of the node timeout value (when ending with MULT). */
#define CLUSTER_FAIL_REPORT_VALIDITY_MULT 2 /* Fail report validity. */
#define CLUSTER_FAIL_UNDO_TIME_MULT 2 /* Undo fail if master is back. */
#define CLUSTER_MF_TIMEOUT 5000 /* Milliseconds to do a manual failover. */
#define CLUSTER_MF_PAUSE_MULT 2 /* Master pause manual failover mult. */
#define CLUSTER_SLAVE_MIGRATION_DELAY 5000 /* Delay for slave migration. */

/* Reasons why a slave is not able to failover. */
#define CLUSTER_CANT_FAILOVER_NONE 0
#define CLUSTER_CANT_FAILOVER_DATA_AGE 1
#define CLUSTER_CANT_FAILOVER_WAITING_DELAY 2
#define CLUSTER_CANT_FAILOVER_EXPIRED 3
#define CLUSTER_CANT_FAILOVER_WAITING_VOTES 4
#define CLUSTER_CANT_FAILOVER_RELOG_PERIOD (10) /* seconds. */

/* clusterState todo_before_sleep flags. */
#define CLUSTER_TODO_HANDLE_FAILOVER (1<<0)
#define CLUSTER_TODO_UPDATE_STATE (1<<1)
#define CLUSTER_TODO_SAVE_CONFIG (1<<2)
#define CLUSTER_TODO_FSYNC_CONFIG (1<<3)
#define CLUSTER_TODO_HANDLE_MANUALFAILOVER (1<<4)

/* clusterLink encapsulates everything needed to talk with a remote node. */
typedef struct clusterLink {
    mstime_t ctime;             /* Link creation time */
    connection *conn;           /* Connection to remote node */
    list *send_msg_queue;        /* List of messages to be sent */
    size_t head_msg_send_offset; /* Number of bytes already sent of message at head of queue */
    unsigned long long send_msg_queue_mem; /* Memory in bytes used by message queue */
    char *rcvbuf;               /* Packet reception buffer */
    size_t rcvbuf_len;          /* Used size of rcvbuf */
    size_t rcvbuf_alloc;        /* Allocated size of rcvbuf */
    clusterNode *node;          /* Node related to this link. Initialized to NULL when unknown */
    int inbound;                /* 1 if this link is an inbound link accepted from the related node */
} clusterLink;

/* Cluster node flags and macros. */
#define CLUSTER_NODE_MASTER 1     /* The node is a master */
#define CLUSTER_NODE_SLAVE 2      /* The node is a slave */
#define CLUSTER_NODE_PFAIL 4      /* Failure? Need acknowledge */
#define CLUSTER_NODE_FAIL 8       /* The node is believed to be malfunctioning */
#define CLUSTER_NODE_MYSELF 16    /* This node is myself */
#define CLUSTER_NODE_HANDSHAKE 32 /* We have still to exchange the first ping */
#define CLUSTER_NODE_NOADDR   64  /* We don't know the address of this node */
#define CLUSTER_NODE_MEET 128     /* Send a MEET message to this node */
#define CLUSTER_NODE_MIGRATE_TO 256 /* Master eligible for replica migration. */
#define CLUSTER_NODE_NOFAILOVER 512 /* Slave will not try to failover. */
#define CLUSTER_NODE_EXTENSIONS_SUPPORTED 1024 /* This node supports extensions. */
#define CLUSTER_NODE_NULL_NAME "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"

#define nodeIsSlave(n) ((n)->flags & CLUSTER_NODE_SLAVE)
#define nodeInHandshake(n) ((n)->flags & CLUSTER_NODE_HANDSHAKE)
#define nodeHasAddr(n) (!((n)->flags & CLUSTER_NODE_NOADDR))
#define nodeTimedOut(n) ((n)->flags & CLUSTER_NODE_PFAIL)
#define nodeFailed(n) ((n)->flags & CLUSTER_NODE_FAIL)
#define nodeCantFailover(n) ((n)->flags & CLUSTER_NODE_NOFAILOVER)
#define nodeSupportsExtensions(n) ((n)->flags & CLUSTER_NODE_EXTENSIONS_SUPPORTED)

/* This structure represent elements of node->fail_reports. */
typedef struct clusterNodeFailReport {
    clusterNode *node;         /* Node reporting the failure condition. */
    mstime_t time;             /* Time of the last report from this node. */
} clusterNodeFailReport;

/* Redis cluster messages header */

/* Message types.
 *
 * Note that the PING, PONG and MEET messages are actually the same exact
 * kind of packet. PONG is the reply to ping, in the exact format as a PING,
 * while MEET is a special PING that forces the receiver to add the sender
 * as a node (if it is not already in the list). */
#define CLUSTERMSG_TYPE_PING 0          /* Ping */
#define CLUSTERMSG_TYPE_PONG 1          /* Pong (reply to Ping) */
#define CLUSTERMSG_TYPE_MEET 2          /* Meet "let's join" message */
#define CLUSTERMSG_TYPE_FAIL 3          /* Mark node xxx as failing */
#define CLUSTERMSG_TYPE_PUBLISH 4       /* Pub/Sub Publish propagation */
#define CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST 5 /* May I failover? */
#define CLUSTERMSG_TYPE_FAILOVER_AUTH_ACK 6     /* Yes, you have my vote */
#define CLUSTERMSG_TYPE_UPDATE 7        /* Another node slots configuration */
#define CLUSTERMSG_TYPE_MFSTART 8       /* Pause clients for manual failover */
#define CLUSTERMSG_TYPE_MODULE 9        /* Module cluster API message. */
#define CLUSTERMSG_TYPE_PUBLISHSHARD 10 /* Pub/Sub Publish shard propagation */
#define CLUSTERMSG_TYPE_COUNT 11        /* Total number of message types. */

/* Initially we don't know our "name", but we'll find it once we connect
 * to the first node, using the getsockname() function. Then we'll use this
 * address for all the next messages. */
typedef struct {
    char nodename[CLUSTER_NAMELEN];
    uint32_t ping_sent;
    uint32_t pong_received;
    char ip[NET_IP_STR_LEN];  /* IP address last time it was seen */
    uint16_t port;              /* primary port last time it was seen */
    uint16_t cport;             /* cluster port last time it was seen */
    uint16_t flags;             /* node->flags copy */
    uint16_t pport;             /* secondary port last time it was seen */
    uint16_t notused1;
} clusterMsgDataGossip;

typedef struct {
    char nodename[CLUSTER_NAMELEN];
} clusterMsgDataFail;

typedef struct {
    uint32_t channel_len;
    uint32_t message_len;
    unsigned char bulk_data[8]; /* 8 bytes just as placeholder. */
} clusterMsgDataPublish;

typedef struct {
    uint64_t configEpoch; /* Config epoch of the specified instance. */
    char nodename[CLUSTER_NAMELEN]; /* Name of the slots owner. */
    unsigned char slots[CLUSTER_SLOTS/8]; /* Slots bitmap. */
} clusterMsgDataUpdate;

typedef struct {
    uint64_t module_id;     /* ID of the sender module. */
    uint32_t len;           /* ID of the sender module. */
    uint8_t type;           /* Type from 0 to 255. */
    unsigned char bulk_data[3]; /* 3 bytes just as placeholder. */
} clusterMsgModule;

/* The cluster supports optional extension messages that can be sent
 * along with ping/pong/meet messages to give additional info in a
 * consistent manner. */
typedef enum {
    CLUSTERMSG_EXT_TYPE_HOSTNAME,
    CLUSTERMSG_EXT_TYPE_HUMAN_NODENAME,
    CLUSTERMSG_EXT_TYPE_FORGOTTEN_NODE,
    CLUSTERMSG_EXT_TYPE_SHARDID,
    CLUSTERMSG_EXT_TYPE_INTERNALSECRET,
} clusterMsgPingtypes;

/* Helper function for making sure extensions are eight byte aligned. */
#define EIGHT_BYTE_ALIGN(size) ((((size) + 7) / 8) * 8)
#define CLUSTER_INTERNALSECRETLEN 40      /* sha1 hex length */

typedef struct {
    char hostname[1]; /* The announced hostname, ends with \0. */
} clusterMsgPingExtHostname;

typedef struct {
    char human_nodename[1]; /* The announced nodename, ends with \0. */
} clusterMsgPingExtHumanNodename;

typedef struct {
    char name[CLUSTER_NAMELEN]; /* Node name. */
    uint64_t ttl; /* Remaining time to blacklist the node, in seconds. */
} clusterMsgPingExtForgottenNode;

static_assert(sizeof(clusterMsgPingExtForgottenNode) % 8 == 0, "");

typedef struct {
    char shard_id[CLUSTER_NAMELEN]; /* The shard_id, 40 bytes fixed. */
} clusterMsgPingExtShardId;

typedef struct {
    char internal_secret[CLUSTER_INTERNALSECRETLEN]; /* Current shard internal secret */
} clusterMsgPingExtInternalSecret;

typedef struct {
    uint32_t length; /* Total length of this extension message (including this header) */
    uint16_t type; /* Type of this extension message (see clusterMsgPingExtTypes) */
    uint16_t unused; /* 16 bits of padding to make this structure 8 byte aligned. */
    union {
        clusterMsgPingExtHostname hostname;
        clusterMsgPingExtHumanNodename human_nodename;
        clusterMsgPingExtForgottenNode forgotten_node;
        clusterMsgPingExtShardId shard_id;
        clusterMsgPingExtInternalSecret internal_secret;
    } ext[]; /* Actual extension information, formatted so that the data is 8
              * byte aligned, regardless of its content. */
} clusterMsgPingExt;

union clusterMsgData {
    /* PING, MEET and PONG */
    struct {
        /* Array of N clusterMsgDataGossip structures */
        clusterMsgDataGossip gossip[1];
        /* Extension data that can optionally be sent for ping/meet/pong
         * messages. We can't explicitly define them here though, since
         * the gossip array isn't the real length of the gossip data. */
    } ping;

    /* FAIL */
    struct {
        clusterMsgDataFail about;
    } fail;

    /* PUBLISH */
    struct {
        clusterMsgDataPublish msg;
    } publish;

    /* UPDATE */
    struct {
        clusterMsgDataUpdate nodecfg;
    } update;

    /* MODULE */
    struct {
        clusterMsgModule msg;
    } module;
};

#define CLUSTER_PROTO_VER 1 /* Cluster bus protocol version. */

typedef struct {
    char sig[4];        /* Signature "RCmb" (Redis Cluster message bus). */
    uint32_t totlen;    /* Total length of this message */
    uint16_t ver;       /* Protocol version, currently set to 1. */
    uint16_t port;      /* Primary port number (TCP or TLS). */
    uint16_t type;      /* Message type */
    uint16_t count;     /* Only used for some kinds of messages. */
    uint64_t currentEpoch;  /* The epoch accordingly to the sending node. */
    uint64_t configEpoch;   /* The config epoch if it's a master, or the last
                               epoch advertised by its master if it is a
                               slave. */
    uint64_t offset;    /* Master replication offset if node is a master or
                           processed replication offset if node is a slave. */
    char sender[CLUSTER_NAMELEN]; /* Name of the sender node */
    unsigned char myslots[CLUSTER_SLOTS/8];
    char slaveof[CLUSTER_NAMELEN];
    char myip[NET_IP_STR_LEN];    /* Sender IP, if not all zeroed. */
    uint16_t extensions; /* Number of extensions sent along with this packet. */
    char notused1[30];   /* 30 bytes reserved for future usage. */
    uint16_t pport;      /* Secondary port number: if primary port is TCP port, this is
                            TLS port, and if primary port is TLS port, this is TCP port.*/
    uint16_t cport;      /* Sender TCP cluster bus port */
    uint16_t flags;      /* Sender node flags */
    unsigned char state; /* Cluster state from the POV of the sender */
    unsigned char mflags[3]; /* Message flags: CLUSTERMSG_FLAG[012]_... */
    union clusterMsgData data;
} clusterMsg;

/* clusterMsg defines the gossip wire protocol exchanged among Redis cluster
 * members, which can be running different versions of redis-server bits,
 * especially during cluster rolling upgrades.
 *
 * Therefore, fields in this struct should remain at the same offset from
 * release to release. The static asserts below ensure that incompatible
 * changes in clusterMsg are caught at compile time.
 */

static_assert(offsetof(clusterMsg, sig) == 0, "unexpected field offset");
static_assert(offsetof(clusterMsg, totlen) == 4, "unexpected field offset");
static_assert(offsetof(clusterMsg, ver) == 8, "unexpected field offset");
static_assert(offsetof(clusterMsg, port) == 10, "unexpected field offset");
static_assert(offsetof(clusterMsg, type) == 12, "unexpected field offset");
static_assert(offsetof(clusterMsg, count) == 14, "unexpected field offset");
static_assert(offsetof(clusterMsg, currentEpoch) == 16, "unexpected field offset");
static_assert(offsetof(clusterMsg, configEpoch) == 24, "unexpected field offset");
static_assert(offsetof(clusterMsg, offset) == 32, "unexpected field offset");
static_assert(offsetof(clusterMsg, sender) == 40, "unexpected field offset");
static_assert(offsetof(clusterMsg, myslots) == 80, "unexpected field offset");
static_assert(offsetof(clusterMsg, slaveof) == 2128, "unexpected field offset");
static_assert(offsetof(clusterMsg, myip) == 2168, "unexpected field offset");
static_assert(offsetof(clusterMsg, extensions) == 2214, "unexpected field offset");
static_assert(offsetof(clusterMsg, notused1) == 2216, "unexpected field offset");
static_assert(offsetof(clusterMsg, pport) == 2246, "unexpected field offset");
static_assert(offsetof(clusterMsg, cport) == 2248, "unexpected field offset");
static_assert(offsetof(clusterMsg, flags) == 2250, "unexpected field offset");
static_assert(offsetof(clusterMsg, state) == 2252, "unexpected field offset");
static_assert(offsetof(clusterMsg, mflags) == 2253, "unexpected field offset");
static_assert(offsetof(clusterMsg, data) == 2256, "unexpected field offset");

#define CLUSTERMSG_MIN_LEN (sizeof(clusterMsg)-sizeof(union clusterMsgData))

/* Message flags better specify the packet content or are used to
 * provide some information about the node state. */
#define CLUSTERMSG_FLAG0_PAUSED (1<<0) /* Master paused for manual failover. */
#define CLUSTERMSG_FLAG0_FORCEACK (1<<1) /* Give ACK to AUTH_REQUEST even if
                                            master is up. */
#define CLUSTERMSG_FLAG0_EXT_DATA (1<<2) /* Message contains extension data */

struct _clusterNode {
    mstime_t ctime; /* Node object creation time. */ /* 节点对象创建时间 */
    char name[CLUSTER_NAMELEN]; /* Node name, hex string, sha1-size */ /* 节点名称，十六进制字符串，sha1大小 */
    char shard_id[CLUSTER_NAMELEN]; /* shard id, hex string, sha1-size */ /* 分片ID，十六进制字符串，sha1大小 */
    int flags; /* CLUSTER_NODE_... */ /* 节点标志 */
    uint64_t configEpoch; /* Last configEpoch observed for this node */ /* 此节点观察到的最后配置纪元 */
    unsigned char slots[CLUSTER_SLOTS/8]; /* slots handled by this node */ /* 此节点处理的槽位 */
    uint16_t *slot_info_pairs; /* Slots info represented as (start/end) pair (consecutive index). */ /* 槽位信息表示为（起始/结束）对（连续索引） */
    int slot_info_pairs_count; /* Used number of slots in slot_info_pairs */ /* slot_info_pairs 中使用的槽位数量 */
    int numslots; /* Number of slots handled by this node */ /* 此节点处理的槽位数量 */
    int numslaves; /* Number of slave nodes, if this is a master */ /* 如果是主节点，则为从节点的数量 */
    clusterNode **slaves; /* pointers to slave nodes */ /* 指向从节点的指针 */
    clusterNode *slaveof; /* pointer to the master node. Note that it
                         may be NULL even if the node is a slave
                         if we don't have the master node in our
                         tables. */ /* 指向主节点的指针。注意，即使节点是从节点，如果我们在表中没有主节点，它也可能为 NULL */
    unsigned long long last_in_ping_gossip; /* The number of the last carried in the ping gossip section */ /* ping gossip 部分中最后携带的编号 */
    mstime_t ping_sent; /* Unix time we sent latest ping */ /* 发送最新 ping 的 Unix 时间 */
    mstime_t pong_received; /* Unix time we received the pong */ /* 接收 pong 的 Unix 时间 */
    mstime_t data_received; /* Unix time we received any data */ /* 接收任何数据的 Unix 时间 */
    mstime_t fail_time; /* Unix time when FAIL flag was set */ /* 设置 FAIL 标志的 Unix 时间 */
    mstime_t voted_time; /* Last time we voted for a slave of this master */ /* 上次为此主节点的从节点投票的时间 */
    mstime_t repl_offset_time; /* Unix time we received offset for this node */ /* 接收此节点偏移量的 Unix 时间 */
    mstime_t orphaned_time; /* Starting time of orphaned master condition */ /* 孤立主节点状态的开始时间 */
    long long repl_offset; /* Last known repl offset for this node. */ /* 此节点的最后已知复制偏移量 */
    char ip[NET_IP_STR_LEN]; /* Latest known IP address of this node */ /* 此节点的最新已知 IP 地址 */
    sds hostname; /* The known hostname for this node */ /* 此节点的已知主机名 */
    sds human_nodename; /* The known human readable nodename for this node */ /* 此节点的已知可读节点名称 */
    int tcp_port; /* Latest known clients TCP port. */ /* 最新已知的客户端 TCP 端口 */
    int tls_port; /* Latest known clients TLS port */ /* 最新已知的客户端 TLS 端口 */
    int cport; /* Latest known cluster port of this node. */ /* 此节点的最新已知集群端口 */
    clusterLink *link; /* TCP/IP link established toward this node */ /* 建立到此节点的 TCP/IP 链接 */
    clusterLink *inbound_link; /* TCP/IP link accepted from this node */ /* 从此节点接受的 TCP/IP 链接 */
    list *fail_reports; /* List of nodes signaling this as failing */ /* 标记此节点为失败的节点列表 */
};

struct clusterState {
    clusterNode *myself;  /* This node */ /* 当前节点 */
    uint64_t currentEpoch; /* Current epoch */ /* 当前纪元 */
    int state;            /* CLUSTER_OK, CLUSTER_FAIL, ... */ /* 集群状态，例如 CLUSTER_OK 或 CLUSTER_FAIL */
    int size;             /* Num of master nodes with at least one slot */ /* 至少有一个槽的主节点数量 */
    dict *nodes;          /* Hash table of name -> clusterNode structures */ /* 名称到 clusterNode 结构的哈希表 */
    dict *shards;         /* Hash table of shard_id -> list (of nodes) structures */ /* 分片 ID 到节点列表的哈希表 */
    dict *nodes_black_list; /* Nodes we don't re-add for a few seconds. */ /* 短时间内不会重新添加的节点黑名单 */
    clusterNode *migrating_slots_to[CLUSTER_SLOTS]; /* Migrating slots destination */ /* 正在迁移的槽的目标节点 */
    clusterNode *importing_slots_from[CLUSTER_SLOTS]; /* Importing slots source */ /* 正在导入的槽的源节点 */
    clusterNode *slots[CLUSTER_SLOTS]; /* Slots mapping */ /* 槽位映射 */
    char internal_secret[CLUSTER_INTERNALSECRETLEN]; /* Internal secret */ /* 内部密钥 */
    /* The following fields are used to take the slave state on elections. */ /* 以下字段用于选举时记录从节点状态 */
    mstime_t failover_auth_time; /* Time of previous or next election. */ /* 上次或下一次选举的时间 */
    int failover_auth_count;    /* Number of votes received so far. */ /* 到目前为止收到的选票数量 */
    int failover_auth_sent;     /* True if we already asked for votes. */ /* 如果已经请求投票则为真 */
    int failover_auth_rank;     /* This slave rank for current auth request. */ /* 当前授权请求中此从节点的排名 */
    uint64_t failover_auth_epoch; /* Epoch of the current election. */ /* 当前选举的纪元 */
    int cant_failover_reason;   /* Why a slave is currently not able to
                                   failover. See the CANT_FAILOVER_* macros. */ /* 当前从节点无法进行故障转移的原因，参见 CANT_FAILOVER_* 宏 */
    /* Manual failover state in common. */ /* 手动故障转移的通用状态 */
    mstime_t mf_end;            /* Manual failover time limit (ms unixtime).
                                   It is zero if there is no MF in progress. */  /* 手动故障转移的时间限制（毫秒 Unix 时间），如果没有正在进行的手动故障转移，则为零 */
    /* Manual failover state of master. */ /* 主节点的手动故障转移状态 */
    clusterNode *mf_slave;      /* Slave performing the manual failover. */ /* 执行手动故障转移的从节点 */
    /* Manual failover state of slave. */ /* 从节点的手动故障转移状态 */
    long long mf_master_offset; /* Master offset the slave needs to start MF
                                   or -1 if still not received. */  /* 从节点开始手动故障转移所需的主节点偏移量，如果尚未接收到则为 -1 */
    int mf_can_start;           /* If non-zero signal that the manual failover
                                   can start requesting masters vote. */ /* 如果非零，则表示手动故障转移可以开始请求主节点投票 */
    /* The following fields are used by masters to take state on elections. */ /* 以下字段由主节点在选举中使用 */
    uint64_t lastVoteEpoch;     /* Epoch of the last vote granted. */ /* 上次投票授予的纪元 */
    int todo_before_sleep; /* Things to do in clusterBeforeSleep(). */ /* 在 clusterBeforeSleep() 中需要完成的任务 */
    /* Stats */ /* 统计信息 */
    /* Messages received and sent by type. */ /* 按类型统计接收和发送的消息 */
    long long stats_bus_messages_sent[CLUSTERMSG_TYPE_COUNT]; /* Messages sent */ /* 发送的消息数量 */
    long long stats_bus_messages_received[CLUSTERMSG_TYPE_COUNT]; /* Messages received */ /* 接收的消息数量 */
    long long stats_pfail_nodes;    /* Number of nodes in PFAIL status,
                                       excluding nodes without address. */  /* 处于 PFAIL 状态的节点数量，不包括没有地址的节点 */
    unsigned long long stat_cluster_links_buffer_limit_exceeded;  /* Total number of cluster links freed due to exceeding buffer limit */   /* 因超出缓冲区限制而释放的集群链接总数 */

    /* Bit map for slots that are no longer claimed by the owner in cluster PING
     * messages. During slot migration, the owner will stop claiming the slot after
     * the ownership transfer. Set the bit corresponding to the slot when a node
     * stops claiming the slot. This prevents spreading incorrect information (that
     * source still owns the slot) using UPDATE messages. */ /* 在集群 PING 消息中，槽位所有者不再声明的槽位位图。在槽位迁移期间，所有者在所有权转移后将停止声明槽位。
        * 当节点停止声明槽位时，设置对应槽位的位。这可以防止通过 UPDATE 消息传播错误信息（即源节点仍然拥有槽位）。 */
    unsigned char owner_not_claiming_slot[CLUSTER_SLOTS / 8];
};


#endif //CLUSTER_LEGACY_H
