# Redis HGET命令执行流程详解

本文从源码角度详细分析Redis中`HGET`命令的执行流程，深入到底层的KVStore实现，展示了从接收命令到返回结果的完整过程。

## 1. HGET命令概述

`HGET`命令用于获取哈希表中指定字段的值。命令格式为：

```
HGET key field
```

- 如果键不存在，返回nil
- 如果键存在但不是哈希类型，返回错误
- 如果字段不存在于哈希表中，返回nil

## 2. 命令入口：hgetCommand函数

`hgetCommand`函数是处理HGET命令的入口点，定义在`src/t_hash.c`文件中（第2638行）：

```c
void hgetCommand(client *c) {
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.null[c->resp])) == NULL ||
        checkType(c,o,OBJ_HASH)) return;

    addHashFieldToReply(c, o, c->argv[2]->ptr, HFE_LAZY_EXPIRE);
}
```

这个函数非常简洁，主要完成三个步骤：
1. 使用`lookupKeyReadOrReply`查找键
2. 检查键的类型是否为哈希
3. 调用`addHashFieldToReply`获取并返回字段值

## 3. 键查找过程

### 3.1 lookupKeyReadOrReply函数

位于`src/db.c`文件（第240行）：

```c
robj *lookupKeyReadOrReply(client *c, robj *key, robj *reply) {
    robj *o = lookupKeyRead(c->db, key);
    if (!o) addReplyOrErrorObject(c, reply);
    return o;
}
```

这个函数调用`lookupKeyRead`查找键，如果键不存在，则向客户端返回指定的回复。

### 3.2 lookupKeyRead函数

位于`src/db.c`文件（第215行）：

```c
robj *lookupKeyRead(redisDb *db, robj *key) {
    return lookupKeyReadWithFlags(db,key,LOOKUP_NONE);
}
```

### 3.3 lookupKeyReadWithFlags函数

位于`src/db.c`文件（第208行）：

```c
robj *lookupKeyReadWithFlags(redisDb *db, robj *key, int flags) {
    serverAssert(!(flags & LOOKUP_WRITE));
    return lookupKey(db, key, flags, NULL);
}
```

### 3.4 lookupKey函数

位于`src/db.c`文件（第141行）：

```c
robj *lookupKey(redisDb *db, robj *key, int flags, dictEntry **deref) {
    const int key_slot = getKeySlot(key->ptr);
    dictEntry *de = dbFindWithKeySlot(db, key->ptr, key_slot);
    robj *val = NULL;
    if (de) {
        val = dictGetVal(de);
        // 检查键是否过期
        if (expireIfNeededWithSlot(db, key, expire_flags, key_slot) != KEY_VALID) {
            val = NULL;
        }
    }

    if (val) {
        // 更新访问时间(LRU/LFU)
        if (!hasActiveChildProcess() && !(flags & LOOKUP_NOTOUCH)){
            if (server.maxmemory_policy & MAXMEMORY_FLAG_LFU) {
                updateLFU(val);
            } else {
                val->lru = LRU_CLOCK();
            }
        }
        // 更新命中统计
        if (!(flags & (LOOKUP_NOSTATS | LOOKUP_WRITE)))
            server.stat_keyspace_hits++;
    } else {
        // 更新未命中统计
        if (!(flags & (LOOKUP_NOSTATS | LOOKUP_WRITE)))
            server.stat_keyspace_misses++;
    }

    if (val && deref) *deref = de;
    return val;
}
```

这个函数是键查找的核心，它完成以下工作：
1. 计算键的哈希槽
2. 调用`dbFindWithKeySlot`在数据库中查找键
3. 检查键是否过期
4. 更新访问时间(LRU/LFU)
5. 更新命中/未命中统计

## 4. KVStore查找过程

### 4.1 dbFindWithKeySlot函数

位于`src/db.c`文件（第2364行）：

```c
static inline dictEntry *dbFindWithKeySlot(redisDb *db, void *key, int keySlot) {
    return dbFindGenericWithKeySlot(db->keys, key, keySlot);
}
```

### 4.2 dbFindGenericWithKeySlot函数

位于`src/db.c`文件（第2352行）：

```c
static inline dictEntry *dbFindGenericWithKeySlot(kvstore *kvs, void *key, int keySlot) {
    return kvstoreDictFind(kvs, keySlot, key);
}
```

### 4.3 kvstoreDictFind函数

位于`src/kvstore.c`文件（第852行）：

```c
dictEntry *kvstoreDictFind(kvstore *kvs, int didx, void *key) {
    dict *d = kvstoreGetDict(kvs, didx);
    if (!d)
        return NULL;
    return dictFind(d, key);
}
```

### 4.4 kvstoreGetDict函数

位于`src/kvstore.c`文件（第86行）：

```c
static dict *kvstoreGetDict(kvstore *kvs, int didx) {
    return kvs->dicts[didx];
}
```

### 4.5 dictFind函数

位于`src/dict.c`中，在字典中查找键，返回对应的`dictEntry`或NULL。

## 5. 哈希字段查找过程

### 5.1 addHashFieldToReply函数

位于`src/t_hash.c`文件（第2615行）：

```c
static GetFieldRes addHashFieldToReply(client *c, robj *o, sds field, int hfeFlags) {
    if (o == NULL) {
        addReplyNull(c);
        return GETF_NOT_FOUND;
    }

    unsigned char *vstr = NULL;
    unsigned int vlen = UINT_MAX;
    long long vll = LLONG_MAX;

    GetFieldRes res = hashTypeGetValue(c->db, o, field, &vstr, &vlen, &vll, hfeFlags, NULL);
    if (res == GETF_OK) {
        if (vstr) {
            addReplyBulkCBuffer(c, vstr, vlen);
        } else {
            addReplyBulkLongLong(c, vll);
        }
    } else {
        addReplyNull(c);
    }
    return res;
}
```

这个函数调用`hashTypeGetValue`获取哈希字段的值，然后根据结果向客户端返回相应的回复。

### 5.2 hashTypeGetValue函数

位于`src/t_hash.c`文件（第722行）：

```c
GetFieldRes hashTypeGetValue(redisDb *db, robj *o, sds field, unsigned char **vstr,
                             unsigned int *vlen, long long *vll, 
                             int hfeFlags, uint64_t *expiredAt)
{
    sds key;
    GetFieldRes res;
    uint64_t dummy;
    if (expiredAt == NULL) expiredAt = &dummy;
    if (o->encoding == OBJ_ENCODING_LISTPACK ||
        o->encoding == OBJ_ENCODING_LISTPACK_EX) {
        *vstr = NULL;
        res = hashTypeGetFromListpack(o, field, vstr, vlen, vll, expiredAt);

        if (res == GETF_NOT_FOUND)
            return GETF_NOT_FOUND;

    } else if (o->encoding == OBJ_ENCODING_HT) {
        sds value = NULL;
        res = hashTypeGetFromHashTable(o, field, &value, expiredAt);

        if (res == GETF_NOT_FOUND)
            return GETF_NOT_FOUND;

        *vstr = (unsigned char*) value;
        *vlen = sdslen(value);
    } else {
        serverPanic("Unknown hash encoding");
    }

    // 检查字段是否过期
    if ((*expiredAt >= (uint64_t) commandTimeSnapshot()) || 
        (hfeFlags & HFE_LAZY_ACCESS_EXPIRED))
        return GETF_OK;

    // 处理过期字段
    // ...省略过期处理代码...

    return res;
}
```

这个函数根据哈希的编码类型，调用不同的函数获取字段值：
- 如果是listpack编码，调用`hashTypeGetFromListpack`
- 如果是哈希表编码，调用`hashTypeGetFromHashTable`

然后检查字段是否过期，并处理过期字段。

### 5.3 hashTypeGetFromHashTable函数

位于`src/t_hash.c`文件（第689行）：

```c
GetFieldRes hashTypeGetFromHashTable(robj *o, sds field, sds *value, uint64_t *expiredAt) {
    dictEntry *de;

    *expiredAt = EB_EXPIRE_TIME_INVALID;

    serverAssert(o->encoding == OBJ_ENCODING_HT);

    de = dictFind(o->ptr, field);

    if (de == NULL)
        return GETF_NOT_FOUND;

    *expiredAt = hfieldGetExpireTime(dictGetKey(de));
    *value = (sds) dictGetVal(de);
    return GETF_OK;
}
```

### 5.4 hashTypeGetFromListpack函数

位于`src/t_hash.c`文件（第636行）：

```c
GetFieldRes hashTypeGetFromListpack(robj *o, sds field,
                            unsigned char **vstr,
                            unsigned int *vlen,
                            long long *vll,
                            uint64_t *expiredAt)
{
    *expiredAt = EB_EXPIRE_TIME_INVALID;
    unsigned char *zl, *fptr = NULL, *vptr = NULL;

    if (o->encoding == OBJ_ENCODING_LISTPACK) {
        zl = o->ptr;
        fptr = lpFirst(zl);
        if (fptr != NULL) {
            fptr = lpFind(zl, fptr, (unsigned char*)field, sdslen(field), 1);
            if (fptr != NULL) {
                /* Grab pointer to the value (fptr points to the field) */
                vptr = lpNext(zl, fptr);
                serverAssert(vptr != NULL);
            }
        }
    } else if (o->encoding == OBJ_ENCODING_LISTPACK_EX) {
        // 处理带过期时间的listpack
        // ...省略代码...
    } else {
        serverPanic("Unknown hash encoding: %d", o->encoding);
    }

    if (vptr != NULL) {
        *vstr = lpGetValue(vptr, vlen, vll);
        return GETF_OK;
    }

    return GETF_NOT_FOUND;
}
```

## 6. KVStore数据结构

KVStore是Redis中用于存储键值对的重要数据结构，特别是在集群模式下，它用于管理哈希槽。定义在`src/kvstore.c`文件（第36行）：

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

KVStore的主要特点：
1. 由多个字典组成的数组
2. 每个字典对应一个哈希槽（在集群模式下）
3. 支持按需分配字典
4. 使用二进制索引树跟踪累积键频率
5. 支持增量重哈希

## 7. 执行流程总结

HGET命令的执行流程可以总结为以下步骤：

1. **命令解析**：Redis服务器解析客户端发送的HGET命令，提取键名和字段名。

2. **键查找**：
   - 计算键的哈希槽
   - 在KVStore中查找对应的字典
   - 在字典中查找键
   - 检查键是否过期
   - 更新访问时间和统计信息

3. **类型检查**：确保找到的键是哈希类型。

4. **字段查找**：
   - 根据哈希的编码类型（listpack或哈希表）选择不同的查找方法
   - 在哈希中查找字段
   - 检查字段是否过期

5. **返回结果**：
   - 如果字段存在且未过期，返回字段值
   - 如果字段不存在或已过期，返回nil

整个过程涉及多层函数调用和数据结构操作，展示了Redis高效的命令处理机制和数据组织方式。

## 8. 时序图

时序图详细展示了HGET命令从接收到处理完成的整个流程，包括键查找、KVStore操作和哈希字段查找的所有步骤。请参考`hgetCommand_sequence.puml`文件。
