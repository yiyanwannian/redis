# Redis服务器启动流程分析

本文从源码角度分析Redis服务器的启动过程，详细解释从main函数开始到服务器完全初始化并开始接受连接的整个流程。

## 1. 入口点：main函数

Redis服务器的入口点是`src/server.c`中的`main`函数。启动过程可以分为以下几个主要阶段：

### 1.1 基础初始化

```c
int main(int argc, char **argv) {
    struct timeval tv;
    int j;
    char config_from_stdin = 0; /* 标记是否从标准输入读取配置 */
    
    /* 初始化库和基础配置 */
    tzset(); /* 填充全局变量'timezone' */
    zmalloc_set_oom_handler(redisOutOfMemoryHandler);
    
    /* 初始化随机数生成器 */
    gettimeofday(&tv,NULL);
    srand(time(NULL)^getpid()^tv.tv_usec);
    srandom(time(NULL)^getpid()^tv.tv_usec);
    init_genrand64(((long long) tv.tv_sec * 1000000 + tv.tv_usec) ^ getpid());
    crc64_init();
    
    /* 设置umask值 */
    umask(server.umask = umask(0777));
    
    /* 设置字典哈希函数的种子 */
    uint8_t hashseed[16];
    getRandomBytes(hashseed,sizeof(hashseed));
    dictSetHashFunctionSeed(hashseed);
```

这个阶段主要完成基础环境的初始化，包括：
- 设置时区
- 设置内存溢出处理器
- 初始化随机数生成器
- 设置文件权限掩码
- 初始化字典哈希函数种子

### 1.2 检测运行模式

```c
    char *exec_name = strrchr(argv[0], '/');
    if (exec_name == NULL) exec_name = argv[0];
    server.sentinel_mode = checkForSentinelMode(argc,argv, exec_name);
    
    /* 检查是否需要以redis-check-rdb/aof模式启动 */
    if (strstr(exec_name,"redis-check-rdb") != NULL)
        redis_check_rdb_main(argc,argv,NULL);
    else if (strstr(exec_name,"redis-check-aof") != NULL)
        redis_check_aof_main(argc,argv);
```

Redis可以以不同的模式运行：
- 标准服务器模式
- Sentinel模式（用于监控和故障转移）
- RDB/AOF检查工具模式

### 1.3 服务器配置初始化

```c
    initServerConfig();
    ACLInit(); /* ACL子系统必须尽快初始化 */
    moduleInitModulesSystem();
    connTypeInitialize();
    
    /* 存储可执行文件路径和参数 */
    server.executable = getAbsolutePath(argv[0]);
    server.exec_argv = zmalloc(sizeof(char*)*(argc+1));
    server.exec_argv[argc] = NULL;
    for (j = 0; j < argc; j++) server.exec_argv[j] = zstrdup(argv[j]);
```

`initServerConfig`函数初始化服务器的默认配置，包括：
- 端口号
- 数据库数量
- 最大客户端连接数
- 日志级别
- 持久化选项
- 内存限制
- 复制设置

### 1.4 解析命令行参数和配置文件

```c
    if (argc >= 2) {
        j = 1; /* 要在argv[]中解析的第一个选项 */
        sds options = sdsempty();
        
        /* 处理特殊选项 --help 和 --version */
        if (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0)
        {
            sds version = getVersion();
            printf("Redis server %s\n", version);
            sdsfree(version);
            exit(0);
        }
        
        /* 解析命令行选项和配置文件 */
        if (argv[1][0] != '-') {
            /* 将server.exec_argv中的配置文件替换为其绝对路径 */
            server.configfile = getAbsolutePath(argv[1]);
            zfree(server.exec_argv[1]);
            server.exec_argv[1] = zstrdup(server.configfile);
            j = 2; // 解析选项时跳过这个参数
        }
        
        /* ... 解析其他命令行选项 ... */
        
        loadServerConfig(server.configfile, config_from_stdin, options);
        if (server.sentinel_mode) loadSentinelConfigFromQueue();
        sdsfree(options);
    }
```

Redis支持从以下来源加载配置：
1. 配置文件（通常是redis.conf）
2. 标准输入
3. 命令行参数

配置加载的优先级是：命令行参数 > 标准输入 > 配置文件

### 1.5 系统检查

```c
    /* 进行系统检查 */
#ifdef __linux__
    linuxMemoryWarnings();
    sds err_msg = NULL;
    if (checkXenClocksource(&err_msg) < 0) {
        serverLog(LL_WARNING, "WARNING %s", err_msg);
        sdsfree(err_msg);
    }
    /* ... 其他系统检查 ... */
#endif
```

在Linux系统上，Redis会执行一系列系统检查，确保运行环境适合Redis的正常运行，包括：
- 内存过度提交设置
- 时钟源配置
- 特定平台的内核bug检查

### 1.6 守护进程化

```c
    /* 如果需要，进行守护进程化 */
    server.supervised = redisIsSupervised(server.supervised_mode);
    int background = server.daemonize && !server.supervised;
    if (background) daemonize();
```

如果配置了以守护进程模式运行，Redis会调用`daemonize()`函数将自己转变为后台进程。

## 2. 服务器初始化

### 2.1 主要初始化函数

```c
    initServer();
    if (background || server.pidfile) createPidFile();
    if (server.set_proc_title) redisSetProcTitle(NULL);
    redisAsciiArt();
    checkTcpBacklogSettings();
```

`initServer`函数是Redis服务器初始化的核心，它完成了以下工作：

```c
void initServer(void) {
    int j;
    
    /* 设置信号处理器 */
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    setupSignalHandlers();
    
    /* 初始化线程管理器 */
    ThreadsManager_init();
    makeThreadKillable();
    
    /* 创建共享对象 */
    createSharedObjects();
    
    /* 调整打开文件限制 */
    adjustOpenFilesLimit();
    
    /* 初始化单调时钟 */
    const char *clk_msg = monotonicInit();
    
    /* 创建事件循环 */
    server.el = aeCreateEventLoop(server.maxclients+CONFIG_FDSET_INCR);
    
    /* 创建数据库 */
    server.db = zmalloc(sizeof(redisDb)*server.dbnum);
    
    /* 创建监听套接字 */
    if (server.port != 0 &&
        listenToPort(server.port,server.ipfd,&server.ipfd_count) == C_ERR)
        exit(1);
    
    /* 创建定时器事件 */
    if (aeCreateTimeEvent(server.el, 1, serverCron, NULL, NULL) == AE_ERR)
        serverPanic("Can't create event loop timers.");
    
    /* 初始化各种子系统 */
    luaEnvInit();
    scriptingInit(1);
    functionsInit();
    slowlogInit();
    latencyMonitorInit();
}
```

### 2.2 模块系统初始化

```c
    if (!server.sentinel_mode) {
        moduleInitModulesSystemLast();
        moduleLoadInternalModules();
        moduleLoadFromQueue();
    }
    ACLLoadUsersAtStartup();
    initListeners();
```

Redis的模块系统允许通过动态加载的模块扩展Redis功能。在这个阶段，Redis会：
1. 完成模块系统的最终初始化
2. 加载内部模块
3. 加载配置中指定的外部模块

### 2.3 最终初始化

```c
    InitServerLast();
```

`InitServerLast`函数执行一些需要在模块加载后才能完成的初始化工作：

```c
void InitServerLast(void) {
    bioInit();           /* 初始化后台I/O系统 */
    initThreadedIO();    /* 初始化线程化I/O */
    set_jemalloc_bg_thread(server.jemalloc_bg_thread);
    server.initial_memory_usage = zmalloc_used_memory();
}
```

## 3. 数据加载和准备接受连接

### 3.1 加载数据

```c
    if (!server.sentinel_mode) {
        serverLog(LL_NOTICE,"Server initialized");
        aofLoadManifestFromDisk();
        loadDataFromDisk();
        aofOpenIfNeededOnServerStart();
        aofDelHistoryFiles();
        applyAppendOnlyConfig();
        
        /* ... */
        
        for (j = 0; j < CONN_TYPE_MAX; j++) {
            connListener *listener = &server.listeners[j];
            if (listener->ct == NULL)
                continue;
            
            serverLog(LL_NOTICE,"Ready to accept connections %s", 
                     listener->ct->get_type(NULL));
        }
    }
```

在这个阶段，Redis会：
1. 从RDB文件和/或AOF文件加载数据
2. 打开AOF文件（如果启用了AOF持久化）
3. 清理不需要的历史AOF文件
4. 准备接受客户端连接

### 3.2 通知监督系统

```c
    if (server.supervised_mode == SUPERVISED_SYSTEMD) {
        if (!server.masterhost) {
            redisCommunicateSystemd("STATUS=Ready to accept connections\n");
        } else {
            redisCommunicateSystemd("STATUS=Ready to accept connections in read-only mode. Waiting for MASTER <-> REPLICA sync\n");
        }
        redisCommunicateSystemd("READY=1\n");
    }
```

如果Redis是由systemd监督的，它会通知systemd服务已经准备好了。

## 4. 启动事件循环

```c
    redisSetCpuAffinity(server.server_cpulist);
    setOOMScoreAdj(-1);
    
    aeMain(server.el);
    aeDeleteEventLoop(server.el);
    return 0;
}
```

最后，Redis设置CPU亲和性，调整OOM分数，然后启动事件循环。`aeMain`函数是Redis的主事件循环，它会一直运行直到服务器关闭。

## 5. 事件处理机制

Redis使用基于事件的架构，主要处理两种类型的事件：

### 5.1 文件事件

文件事件是对套接字操作的抽象，Redis通过套接字与客户端（或其他Redis服务器）进行通信。文件事件可以是：
- 读事件：当客户端发送命令到服务器时触发
- 写事件：当服务器有数据要发送给客户端时触发

### 5.2 时间事件

时间事件是Redis定期执行的任务，最重要的时间事件是`serverCron`函数，它每100毫秒执行一次，负责：
- 更新服务器统计信息
- 清理过期的客户端连接
- 触发持久化操作
- 主从复制相关操作
- 集群状态更新
- 内存回收

## 6. 总结

Redis服务器的启动过程可以概括为以下几个主要步骤：

1. **基础初始化**：设置基本环境，如随机数生成器、信号处理等
2. **配置加载**：从配置文件、命令行参数或标准输入加载配置
3. **服务器初始化**：创建事件循环、初始化数据库、设置监听套接字等
4. **模块加载**：加载内部和外部模块
5. **数据加载**：从RDB和/或AOF文件加载数据
6. **开始接受连接**：准备接受客户端连接
7. **启动事件循环**：开始处理事件，包括客户端连接、命令执行和定期任务

这个精心设计的启动流程确保了Redis服务器能够高效、可靠地运行，并为其出色的性能奠定了基础。
