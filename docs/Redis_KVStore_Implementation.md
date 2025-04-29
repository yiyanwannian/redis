# Redis KVStore实现原理详解

## 1. 概述

KVStore（Key-Value Store）是Redis中一个重要的数据结构，它是一个由多个字典（dict）组成的数组，主要用于高效地管理和访问键值对。KVStore的主要目的是能够轻松访问属于同一个字典（即具有相同dict-index）的所有键。

在Redis集群模式下，KVStore被用来将映射到相同哈希槽（hash-slot）的所有键保存在KVStore结构中的单独字典中，这使得可以轻松访问映射到特定哈希槽的所有键。

## 2. 核心数据结构

### 2.1 KVStore结构

```c
struct _kvstore {
    int flags;                          // 标志位，控制KVStore的行为
    dictType dtype;                     // 字典类型，定义了字典的各种操作函数
    dict **dicts;                       // 字典数组
    long long num_dicts;                // 字典总数
    long long num_dicts_bits;           // 字典数量的位数（log2(num_dicts)）
    list *rehashing;                    // 正在重哈希的字典列表
    int resize_cursor;                  // 用于逐步调整字典大小的游标
    int allocated_dicts;                // 已分配的字典数量
    int non_empty_dicts;                // 非空字典的数量
    unsigned long long key_count;       // KVStore中的键总数
    unsigned long long bucket_count;    // KVStore中所有字典的桶总数
    unsigned long long *dict_size_index; // 二进制索引树，描述了每个字典索引的累积键频率
    size_t overhead_hashtable_lut;      // 所有字典的开销
    size_t overhead_hashtable_rehashing; // 重哈希字典的开销
    void *metadata[];                   // 根据flags条件分配的元数据
};
```

### 2.2 迭代器结构

KVStore提供了两种迭代器：

1. **KVStore迭代器**：用于跨多个字典进行迭代
```c
struct _kvstoreIterator {
    kvstore *kvs;
    long long didx;
    long long next_didx;
    dictIterator di;
};
```

2. **KVStore字典迭代器**：用于迭代特定字典
```c
struct _kvstoreDictIterator {
    kvstore *kvs;
    long long didx;
    dictIterator di;
};
```

### 2.3 元数据结构

KVStore为每个字典维护元数据：

1. **基本元数据**：
```c
typedef struct {
    listNode *rehashing_node;   // 重哈希列表中的节点
} kvstoreDictMetaBase;
```

2. **扩展元数据**（用于键大小直方图）：
```c
typedef struct {
    kvstoreDictMetaBase base;  // 必须是结构中的第一个成员
    kvstoreDictMetadata meta;  // 外部元数据
} kvstoreDictMetaEx;
```

## 3. 工作原理

### 3.1 创建和初始化

KVStore通过`kvstoreCreate`函数创建，该函数接受三个参数：
- `dictType *type`：字典类型，定义了字典的各种操作函数
- `int num_dicts_bits`：字典数量的位数（log2(num_dicts)），例如0表示1个字典，3表示8个字典
- `int flags`：控制KVStore行为的标志位

创建过程中，KVStore会：
1. 分配内存
2. 设置回调函数
3. 根据需要创建字典
4. 初始化各种计数器和数据结构

```c
kvstore *kvstoreCreate(dictType *type, int num_dicts_bits, int flags) {
    /* 我们不能支持超过2^16个字典，因为我们希望为字典游标保留48位 */
    assert(num_dicts_bits <= 16);

    /* 计算kvstore大小 */   
    size_t kvsize = sizeof(kvstore);
    /* 有条件地计算直方图大小 */
    if (flags & KVSTORE_ALLOC_META_KEYS_HIST) 
        kvsize += sizeof(kvstoreMetadata);
    
    kvstore *kvs = zcalloc(kvsize);
    memcpy(&kvs->dtype, type, sizeof(kvs->dtype));
    kvs->flags = flags;

    /* kvstore必须设置这些回调，所以我们确保调用者没有这样做 */
    assert(!type->userdata);
    assert(!type->dictMetadataBytes);
    assert(!type->rehashingStarted);
    assert(!type->rehashingCompleted);
    kvs->dtype.userdata = kvs;
    if (flags & KVSTORE_ALLOC_META_KEYS_HIST)
        kvs->dtype.dictMetadataBytes = kvstoreDictMetadataExtendSize;
    else
        kvs->dtype.dictMetadataBytes = kvstoreDictMetaBaseSize;
    kvs->dtype.rehashingStarted = kvstoreDictRehashingStarted;
    kvs->dtype.rehashingCompleted = kvstoreDictRehashingCompleted;

    kvs->num_dicts_bits = num_dicts_bits;
    kvs->num_dicts = 1 << kvs->num_dicts_bits;
    kvs->dicts = zcalloc(sizeof(dict*) * kvs->num_dicts);
    if (!(kvs->flags & KVSTORE_ALLOCATE_DICTS_ON_DEMAND)) {
        for (int i = 0; i < kvs->num_dicts; i++)
            createDictIfNeeded(kvs, i);
    }

    kvs->rehashing = listCreate();
    kvs->key_count = 0;
    kvs->non_empty_dicts = 0;
    kvs->resize_cursor = 0;
    kvs->dict_size_index = kvs->num_dicts > 1? zcalloc(sizeof(unsigned long long) * (kvs->num_dicts + 1)) : NULL;
    kvs->bucket_count = 0;
    kvs->overhead_hashtable_lut = 0;
    kvs->overhead_hashtable_rehashing = 0;
    return kvs;
}
```

### 3.2 键的分布和访问

KVStore使用哈希函数将键映射到特定的字典：

1. **获取字典索引**：通过键的哈希值确定字典索引
2. **按需创建字典**：如果设置了`KVSTORE_ALLOCATE_DICTS_ON_DEMAND`标志，字典会在需要时才创建
3. **访问键**：通过字典索引和键访问值

```c
/* 根据需要创建字典并返回它 */
static dict *createDictIfNeeded(kvstore *kvs, int didx) {
    dict *d = kvstoreGetDict(kvs, didx);
    if (d) return d;

    kvs->dicts[didx] = dictCreate(&kvs->dtype);
    kvs->allocated_dicts++;
    return kvs->dicts[didx];
}
```

### 3.3 二进制索引树（BIT）

KVStore使用二进制索引树（也称为Fenwick树）来跟踪累积键频率：

1. **读取累积键数**：`cumulativeKeyCountRead`函数可以在O(log(num_dicts))时间内返回直到给定字典索引（包括该索引）的键总数

```c
/* 返回直到给定字典索引（包括该索引）的键总数（累积）。
 * 时间复杂度为O(log(kvs->num_dicts))。 */
static unsigned long long cumulativeKeyCountRead(kvstore *kvs, int didx) {
    if (kvs->num_dicts == 1) {
        assert(didx == 0);
        return kvstoreSize(kvs);
    }
    int idx = didx + 1;
    unsigned long long sum = 0;
    while (idx > 0) {
        sum += kvs->dict_size_index[idx];
        idx -= (idx & -idx);
    }
    return sum;
}
```

2. **更新累积键数**：`cumulativeKeyCountAdd`函数在添加或删除键时更新二进制索引树

```c
/* 更新二进制索引树（也称为Fenwick树），增加给定字典的键计数。
 * 你可以在这里了解更多关于这种数据结构的信息 https://en.wikipedia.org/wiki/Fenwick_tree
 * 时间复杂度为O(log(kvs->num_dicts))。 */
static void cumulativeKeyCountAdd(kvstore *kvs, int didx, long delta) {
    kvs->key_count += delta;

    dict *d = kvstoreGetDict(kvs, didx);
    size_t dsize = dictSize(d);
    /* 如果dsize为1且delta为正（插入第一个元素，字典变为非空），则增加。
     * 如果dsize为0（字典变为空），则减少。 */
    int non_empty_dicts_delta = (dsize == 1 && delta > 0) ? 1 : (dsize == 0) ? -1 : 0;
    kvs->non_empty_dicts += non_empty_dicts_delta;

    /* 当只有一个字典时，不需要计算BIT。 */
    if (kvs->num_dicts == 1)
        return;

    /* 更新BIT */
    int idx = didx + 1; /* 与字典索引不同，BIT是从1开始的，所以我们需要加1。 */
    while (idx <= kvs->num_dicts) {
        if (delta < 0) {
            assert(kvs->dict_size_index[idx] >= (unsigned long long)labs(delta));
        }
        kvs->dict_size_index[idx] += delta;
        idx += (idx & -idx);
    }
}
```

### 3.4 重哈希机制

KVStore支持字典的增量重哈希：

1. **重哈希开始**：当字典开始重哈希时，`kvstoreDictRehashingStarted`回调被调用，将字典添加到重哈希列表中

```c
/* 将字典添加到重哈希列表中，这使我们能够
 * 在增量重哈希期间快速找到重哈希目标。
 *
 * 如果有多个字典，更新给定字典在DB中的桶计数，
 * 在重哈希阶段，桶计数会增加新的ht大小。
 * 如果只有一个字典，桶计数可以直接从单个字典桶获取。 */
static void kvstoreDictRehashingStarted(dict *d) {
    kvstore *kvs = d->type->userdata;
    kvstoreDictMetaBase *metadata = (kvstoreDictMetaBase *)dictMetadata(d);
    listAddNodeTail(kvs->rehashing, d);
    metadata->rehashing_node = listLast(kvs->rehashing);

    unsigned long long from, to;
    dictRehashingInfo(d, &from, &to);
    kvs->bucket_count += to; /* 开始重哈希（添加新的ht大小） */
    kvs->overhead_hashtable_lut += to;
    kvs->overhead_hashtable_rehashing += from;
}
```

2. **增量重哈希**：`kvstoreIncrementallyRehash`函数在给定时间阈值内执行增量重哈希

3. **重哈希完成**：当字典完成重哈希时，`kvstoreDictRehashingCompleted`回调被调用，从重哈希列表中移除字典

```c
/* 从重哈希列表中移除字典。
 *
 * 更新给定字典在DB中的桶计数。它从DB的桶总数中
 * 移除字典的旧ht大小。 */
static void kvstoreDictRehashingCompleted(dict *d) {
    kvstore *kvs = d->type->userdata;
    kvstoreDictMetaBase *metadata = (kvstoreDictMetaBase *)dictMetadata(d);
    if (metadata->rehashing_node) {
        listDelNode(kvs->rehashing, metadata->rehashing_node);
        metadata->rehashing_node = NULL;
    }

    unsigned long long from, to;
    dictRehashingInfo(d, &from, &to);
    kvs->bucket_count -= from; /* 完成重哈希（移除旧的ht大小） */
    kvs->overhead_hashtable_lut -= from;
    kvs->overhead_hashtable_rehashing -= from;
}
```

### 3.5 内存管理

KVStore实现了高效的内存管理策略：

1. **按需分配**：如果设置了`KVSTORE_ALLOCATE_DICTS_ON_DEMAND`标志，字典只在需要时才创建
2. **释放空字典**：如果设置了`KVSTORE_FREE_EMPTY_DICTS`标志，空字典会被释放以节省内存

```c
/* 当字典将删除条目时调用，该函数将检查
 * KVSTORE_FREE_EMPTY_DICTS以确定是否需要释放空字典。
 *
 * 注意，对于重哈希字典，即在安全迭代器和Scan的情况下，
 * 我们不会删除字典。我们会在释放迭代器时检查是否需要删除它。 */
static void freeDictIfNeeded(kvstore *kvs, int didx) {
    if (!(kvs->flags & KVSTORE_FREE_EMPTY_DICTS) ||
        !kvstoreGetDict(kvs, didx) ||
        kvstoreDictSize(kvs, didx) != 0 ||
        kvstoreDictIsRehashingPaused(kvs, didx))
        return;
    dictRelease(kvs->dicts[didx]);
    kvs->dicts[didx] = NULL;
    kvs->allocated_dicts--;
}
```

3. **内存使用统计**：`kvstoreMemUsage`函数可以计算KVStore的内存使用情况

```c
size_t kvstoreMemUsage(kvstore *kvs) {
    size_t mem = sizeof(*kvs);
    size_t metaSize = sizeof(kvstoreDictMetaBase);

    if (kvs->flags & KVSTORE_ALLOC_META_KEYS_HIST)
        metaSize = sizeof(kvstoreDictMetaEx);
    
    unsigned long long keys_count = kvstoreSize(kvs);
    mem += keys_count * dictEntryMemUsage() +
           kvstoreBuckets(kvs) * sizeof(dictEntry*) +
           kvs->allocated_dicts * (sizeof(dict) + metaSize);

    /* 值是与kvs->dicts共享的dict* */
    mem += listLength(kvs->rehashing) * sizeof(listNode);

    if (kvs->dict_size_index)
        mem += sizeof(unsigned long long) * (kvs->num_dicts + 1);

    return mem;
}
```

## 4. 在Redis中的应用

### 4.1 集群模式

在Redis集群模式下，KVStore被用来管理哈希槽：

1. 每个哈希槽对应一个字典
2. 映射到同一哈希槽的键存储在同一字典中
3. 这使得可以轻松迁移特定哈希槽的所有键

### 4.2 数据库实现

Redis的数据库使用KVStore来存储键值对：

```c
/* 创建Redis数据库，并初始化其他内部状态 */
for (j = 0; j < server.dbnum; j++) {
    server.db[j].keys = kvstoreCreate(&dbDictType, slot_count_bits, flags | KVSTORE_ALLOC_META_KEYS_HIST);
    server.db[j].expires = kvstoreCreate(&dbExpiresDictType, slot_count_bits, flags);
    /* ... 其他初始化 ... */
}
```

1. `server.db[j].keys`：存储数据库中的键值对
2. `server.db[j].expires`：存储键的过期时间

### 4.3 发布/订阅系统

Redis的发布/订阅系统也使用KVStore：

```c
/* 注意，server.pubsub_channels被选择为kvstore（只有一个字典，看起来很奇怪）
 * 只是为了使代码更清晰，使其与server.pubsubshard_channels类型相同
 * （后者必须是kvstore），参见pubsubtype.serverPubSubChannels */
server.pubsub_channels = kvstoreCreate(&objToDictDictType, 0, KVSTORE_ALLOCATE_DICTS_ON_DEMAND);
server.pubsubshard_channels = kvstoreCreate(&objToDictDictType, slot_count_bits, KVSTORE_ALLOCATE_DICTS_ON_DEMAND | KVSTORE_FREE_EMPTY_DICTS);
```

1. `server.pubsub_channels`：存储频道和订阅者
2. `server.pubsubshard_channels`：存储分片频道和订阅者

## 5. 性能优化

KVStore实现了多种性能优化：

### 5.1 增量重哈希

为了避免因重哈希导致的长时间阻塞，KVStore支持增量重哈希：

```c
/* 尝试对字典进行增量重哈希，直到达到给定的时间阈值。
 * 返回重哈希的步数。 */
uint64_t kvstoreIncrementallyRehash(kvstore *kvs, uint64_t threshold_us) {
    uint64_t steps = 0;
    monotime rehash_start = getMonotonicUs();
    
    listIter li;
    listNode *ln;
    listRewind(kvs->rehashing, &li);
    
    while ((ln = listNext(&li)) && (getMonotonicUs() - rehash_start < threshold_us)) {
        dict *d = ln->value;
        steps += dictRehashMilliseconds(d, 1);
    }
    
    return steps;
}
```

### 5.2 按需分配

通过`KVSTORE_ALLOCATE_DICTS_ON_DEMAND`标志，KVStore可以在需要时才创建字典，减少内存使用：

```c
if (!(kvs->flags & KVSTORE_ALLOCATE_DICTS_ON_DEMAND)) {
    for (int i = 0; i < kvs->num_dicts; i++)
        createDictIfNeeded(kvs, i);
}
```

### 5.3 二进制索引树

KVStore使用二进制索引树（Fenwick树）来实现O(log n)时间复杂度的累积键频率查询，这对于大规模数据集非常有效。

### 5.4 空字典释放

通过`KVSTORE_FREE_EMPTY_DICTS`标志，KVStore可以释放空字典以减少内存占用：

```c
if (!(kvs->flags & KVSTORE_FREE_EMPTY_DICTS) ||
    !kvstoreGetDict(kvs, didx) ||
    kvstoreDictSize(kvs, didx) != 0 ||
    kvstoreDictIsRehashingPaused(kvs, didx))
    return;
dictRelease(kvs->dicts[didx]);
kvs->dicts[didx] = NULL;
kvs->allocated_dicts--;
```

## 6. 总结

Redis的KVStore是一个高效的键值存储结构，通过将键分布到多个字典中，它能够支持高效的键值访问和管理。在Redis集群模式下，KVStore特别有用，因为它允许按哈希槽组织键，从而支持高效的集群操作。

KVStore的设计体现了Redis对性能和内存效率的重视，通过增量重哈希、按需分配和空字典释放等机制，KVStore能够在高负载下保持良好的性能。

通过深入理解KVStore的实现原理，我们可以更好地理解Redis的内部工作机制，以及如何优化Redis的性能和内存使用。
