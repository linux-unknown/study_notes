# vhost-spdk启动

[TOC]

vhost-spdk作为用户空间程序，入口函数就是main

## main

```c
int main(int argc, char *argv[])
{
	struct spdk_app_opts opts = {};
	int rc;
	set_max_open_files();
	/* opts初始化 */
	vhost_app_opts_init(&opts);
	/* 解析运行时传进来的参数 
	 * ./bin/vhost_spdk -m 0x10000010 -s 2048 -S /var/run/spdk -F /dev/hugepages
	 * 将解析之后的值保存在opts里面
	 */
	if ((rc = spdk_app_parse_args(argc, argv, &opts, "f:S:", vhost_parse_arg, vhost_usage)) !=
	    SPDK_APP_PARSE_ARGS_SUCCESS) {
	}
	g_vhost_lock_fd = open(g_vhost_lock_path, O_RDONLY | O_CREAT, 0600);
	rc = flock(g_vhost_lock_fd, LOCK_EX | LOCK_NB);

	/* Blocks until the application is exiting */
	rc = spdk_app_start(&opts, vhost_started, NULL, NULL);

	spdk_app_fini();

	return rc;
}
```

主要的工作都是在spdk_app_start中完成

### spdk_app_start

```c
int spdk_app_start(struct spdk_app_opts *opts, spdk_event_fn start_fn, void *arg1, void *arg2)
{
	struct spdk_conf	*config = NULL;
	int			rc;
	struct spdk_event	*app_start_event;

	/* 解析opts->config_file文件，默认在/usr/local/etc/spdk/vhost.conf
	 * 将解析的结果保存在default_config全局变量中
	 */
	config = spdk_app_setup_conf(opts->config_file);
	/* 从解析的config文件中读取Global section的内容 */
	spdk_app_read_config_file_global_params(opts);

	if (spdk_app_setup_env(opts) < 0) { }
	SPDK_NOTICELOG("Total cores available: %d\n", spdk_env_get_core_count());
	/*
	 * If mask not specified on command line or in configuration file,
	 *  reactor_mask will be 0x1 which will enable core 0 to run one
	 *  reactor.
	 */
	if ((rc = spdk_reactors_init(opts->max_delay_us)) != 0) { }

	/* 设置信号处理函数 */
	if ((rc = spdk_app_setup_signal_handlers(opts)) != 0) {}

	memset(&g_spdk_app, 0, sizeof(g_spdk_app));
	g_spdk_app.config = config;
	g_spdk_app.shm_id = opts->shm_id;
	g_spdk_app.shutdown_cb = opts->shutdown_cb;
	g_spdk_app.rc = 0;
	g_init_lcore = spdk_env_get_current_core();
	g_app_start_fn = start_fn;
	g_app_start_arg1 = arg1;
	g_app_start_arg2 = arg2;
	app_start_event = spdk_event_allocate(g_init_lcore, start_rpc, (void *)opts->rpc_addr, NULL);

	spdk_subsystem_init(app_start_event);

	/* This blocks until spdk_app_stop is called */
	spdk_reactors_start();

	return g_spdk_app.rc;
}
```

#### spdk_app_setup_env

```c
static int spdk_app_setup_env(struct spdk_app_opts *opts)
{
	struct spdk_env_opts env_opts = {};
	int rc;

	spdk_env_opts_init(&env_opts);

	env_opts.name = opts->name;
	env_opts.core_mask = opts->reactor_mask;
	env_opts.shm_id = opts->shm_id;
	env_opts.mem_channel = opts->mem_channel;
	env_opts.master_core = opts->master_core;
	env_opts.mem_size = opts->mem_size;
	env_opts.no_pci = opts->no_pci;
	env_opts.mem_file = opts->mem_file;

	rc = spdk_env_init(&env_opts);

	return rc;
}
```

##### spdk_env_init

```c
int spdk_env_init(const struct spdk_env_opts *opts)
{
	char **dpdk_args = NULL;
	int i, rc;
	int orig_optind;
	/* 解析opts，然后构造参数。opts的值也来源与启动spdk_host传递的参数 */
	rc = spdk_build_eal_cmdline(opts);
	/**
	 最终的参数如下：
	Starting SPDK v18.04 (007-202005121400@bio_v20200323_787f7f6(V18.04_Build0051)-26-g97bb6bb)  / DPDK 17.11.0 initialization...
    [ DPDK EAL parameters: vhost -c 0x1000010 -m 2048 --mem-file /dev/hugepages/vhost_spdk_reserved_0,/dev/hugepages/vhost_spdk_reserved_1 --file-prefix=spdk_pid178902 ]
	*/

	printf("Starting %s / %s initialization...\n", SPDK_VERSION_STRING, rte_version());
	printf("[ DPDK EAL parameters: ");
	for (i = 0; i < eal_cmdline_argcount; i++) {
		printf("%s ", eal_cmdline[i]);
	}
	printf("]\n");

	/* DPDK rearranges the array we pass to it, so make a copy
	 * before passing so we can still free the individual strings
	 * correctly.
	 */
	dpdk_args = calloc(eal_cmdline_argcount, sizeof(char *));
	
	memcpy(dpdk_args, eal_cmdline, sizeof(char *) * eal_cmdline_argcount);

	/* 使用上面构造的参数进行rte_eal的初始化 */
	rc = rte_eal_init(eal_cmdline_argcount, dpdk_args);

	if (spdk_mem_map_init() < 0) {}

	/* 建立用户空间页表虚拟地址到物理地址 */
	if (spdk_vtophys_init() < 0) {	}

	return 0;
}
```

###### rte_eal_init

```c
int rte_eal_init(int argc, char **argv)
{
	int i, fctret, ret;
	pthread_t thread_id;
	static rte_atomic32_t run_once = RTE_ATOMIC32_INIT(0);
	const char *logid;
	char cpuset[RTE_CPU_AFFINITY_STR_LEN];
	char thread_name[RTE_MAX_THREAD_NAME_LEN];

	thread_id = pthread_self();

	eal_reset_internal_config(&internal_config);

	/* 初始化cpu的数据结构，赋初值 */
	if (rte_eal_cpu_init() < 0) { }
	/* 解析的值会赋值给internal_config */
	fctret = eal_parse_args(argc, argv);
	/* 解析devic scan的option */
	if (eal_option_device_parse()) { }
	/* 调用bus的scan函数，
     * rte_bus_register会往rte_bus_list添加元素,RTE_REGISTER_BUS宏修饰的会在main函数之前执行。
	 * pcie的rte_pci_scan会扫描/sys/bus/pci/device里面的 pci_dev，并添加到rte_pci_bus.device_list
	 */
	if (rte_bus_scan()) { }
	rte_srand(rte_rdtsc());
	rte_config_init();
#ifdef VFIO_PRESENT
	if (rte_eal_vfio_setup() < 0) {
	}
#endif
	/* 打开mem_file，然后进行mmap，添加到mem_config中 */
	if (rte_eal_memory_init() < 0) {}
	/* the directories are locked during eal_hugepage_info_init */
	eal_hugedirs_unlock();

    /* 将memseg添加到heap中 */
	if (rte_eal_memzone_init() < 0) {}
	if (rte_eal_tailqs_init() < 0) {}
	if (rte_eal_alarm_init() < 0) {}
	if (rte_eal_timer_init() < 0) {}
	eal_check_mem_on_local_socket();

    /* 设置mater core的亲和性 */
	eal_thread_init_master(rte_config.master_lcore);
	ret = eal_thread_dump_affinity(cpuset, RTE_CPU_AFFINITY_STR_LEN);

	RTE_LOG(DEBUG, EAL, "Master lcore %u is ready (tid=%x;cpuset=[%s%s])\n",
		rte_config.master_lcore, (int)thread_id, cpuset,
		ret == 0 ? "" : "...");

	if (rte_eal_intr_init() < 0) {}
	RTE_LCORE_FOREACH_SLAVE(i) {
		/*
		 * create communication pipes between master thread and children
		 */
		if (pipe(lcore_config[i].pipe_master2slave) < 0)
			rte_panic("Cannot create pipe\n");
		if (pipe(lcore_config[i].pipe_slave2master) < 0)
			rte_panic("Cannot create pipe\n");

		lcore_config[i].state = WAIT;
		/* create a thread for each lcore, 为每一个slav core创建eal_thread_loop线程 */
		ret = pthread_create(&lcore_config[i].thread_id, NULL, eal_thread_loop, NULL);
		/* Set thread_name for aid in debugging. */
		snprintf(thread_name, RTE_MAX_THREAD_NAME_LEN, "lcore-slave-%d", i);
		ret = rte_thread_setname(lcore_config[i].thread_id, thread_name);
	}
	/*
	 * Launch a dummy function on all slave lcores, so that master lcore
	 * knows they are all ready when this function returns.
	 */
	rte_eal_mp_remote_launch(sync_func, NULL, SKIP_MASTER);
	rte_eal_mp_wait_lcore();

	/* initialize services so vdevs register service during bus_probe. */
	ret = rte_service_init();
	/* Probe all the buses and devices/drivers on them， 调用bus->probe函数，为device匹配driver
     * 调用rte_pci_register会将driver添加到rte_pci_bus.driver_list
     */
	if (rte_bus_probe()) {}

	/* initialize default service/lcore mappings and start running. Ignore
	 * -ENOTSUP, as it indicates no service coremask passed to EAL.
	 */
	ret = rte_service_start_with_defaults();

	rte_eal_mcfg_complete();

	return fctret;
}
```

###### eal_thread_loop

```c
/* main loop of threads */
__attribute__((noreturn)) void * eal_thread_loop(__attribute__((unused)) void *arg)
{
	char c;
	int n, ret;
	unsigned lcore_id;
	pthread_t thread_id;
	int m2s, s2m;
	char cpuset[RTE_CPU_AFFINITY_STR_LEN];

	thread_id = pthread_self();

	m2s = lcore_config[lcore_id].pipe_master2slave[0];
	s2m = lcore_config[lcore_id].pipe_slave2master[1];


	/* set CPU affinity */
	if (eal_thread_set_affinity() < 0)
		rte_panic("cannot set affinity\n");

	/* read on our pipe to get commands */
	while (1) {
		void *fct_arg;

		/* wait command, rte_eal_remote_launch会通过管道写命令 */
		do {
			n = read(m2s, &c, 1);
		} while (n < 0 && errno == EINTR);

		lcore_config[lcore_id].state = RUNNING;
		/* send ack */
		n = 0;
		while (n == 0 || (n < 0 && errno == EINTR))
			n = write(s2m, &c, 1);
		/* call the function and store the return value */
		fct_arg = lcore_config[lcore_id].arg;
		/* rte_eal_remote_launch会给f赋值 */
		ret = lcore_config[lcore_id].f(fct_arg);
		lcore_config[lcore_id].ret = ret;
		rte_wmb();

		/* when a service core returns, it should go directly to WAIT
		 * state, because the application will not lcore_wait() for it.
		 */
		if (lcore_config[lcore_id].core_role == ROLE_SERVICE)
			lcore_config[lcore_id].state = WAIT;
		else
			lcore_config[lcore_id].state = FINISHED;
	}
}
```

#### spdk_reactors_init

```c
int spdk_reactors_init(unsigned int max_delay_us)
{
	int rc;
	uint32_t i, j, last_core;
	struct spdk_reactor *reactor;
	uint64_t socket_mask = 0x0;
	uint8_t socket_count = 0;
	char mempool_name[32];

	socket_mask = spdk_reactor_get_socket_mask();
	SPDK_NOTICELOG("Occupied cpu socket mask is 0x%lx\n", socket_mask);

	for (i = 0; i < SPDK_MAX_SOCKET; i++) {
		if ((1ULL << i) & socket_mask) {
			snprintf(mempool_name, sizeof(mempool_name), "evtpool%d_%d", i, getpid());
			/* 为每socket创建一个event 的mempoll */
			g_spdk_event_mempool[i] = spdk_mempool_create(mempool_name, (262144 / socket_count),
						  sizeof(struct spdk_event), SPDK_MEMPOOL_DEFAULT_CACHE_SIZE, i);
		}
	}

	/* struct spdk_reactor must be aligned on 64 byte boundary */
	last_core = spdk_env_get_last_core();
	rc = posix_memalign((void **)&g_reactors, 64, (last_core + 1) * sizeof(struct spdk_reactor));

	memset(g_reactors, 0, (last_core + 1) * sizeof(struct spdk_reactor));

	SPDK_ENV_FOREACH_CORE(i) {
		reactor = spdk_reactor_get(i);
		spdk_reactor_construct(reactor, i, max_delay_us);
	}

	g_reactor_state = SPDK_REACTOR_STATE_INITIALIZED;

	return 0;
}
```

##### spdk_reactor_construct

```c
static void spdk_reactor_construct(struct spdk_reactor *reactor, uint32_t lcore, uint64_t max_delay_us)
{
	reactor->lcore = lcore;
	reactor->socket_id = spdk_env_get_socket_id(lcore);
	assert(reactor->socket_id < SPDK_MAX_SOCKET);
	reactor->max_delay_us = max_delay_us;

	TAILQ_INIT(&reactor->active_pollers);
	TAILQ_INIT(&reactor->timer_pollers);
	/* 创建event	 ring */
	reactor->events = spdk_ring_create(SPDK_RING_TYPE_MP_SC, 65536, reactor->socket_id);
	reactor->event_mempool = g_spdk_event_mempool[reactor->socket_id];
}
```

#### spdk_subsystem_init

```c
void spdk_subsystem_init(struct spdk_event *app_start_event)
{
	struct spdk_event *verify_event;
	g_app_start_event = app_start_event;
	/* spdk_subsystem_verify会对subsystem进行初始化 */
	verify_event = spdk_event_allocate(spdk_env_get_current_core(), spdk_subsystem_verify, NULL, NULL);
	/* 给自己发送一个event，在后面的reactor线程里面会处理行 */
	spdk_event_call(verify_event);
}
```

#### spdk_reactors_start

```c
void spdk_reactors_start(void)
{
	struct spdk_reactor *reactor;
	uint32_t i, current_core;
	int rc;

	g_reactor_state = SPDK_REACTOR_STATE_RUNNING;
	g_spdk_app_core_mask = spdk_cpuset_alloc();

	current_core = spdk_env_get_current_core();
	SPDK_ENV_FOREACH_CORE(i) {
        /* 对非本core调用spdk_env_thread_launch_pinned */
		if (i != current_core) {
			reactor = spdk_reactor_get(i);
            /* eal_thread_loop会执行_spdk_reactor_run函数 */
			rc = spdk_env_thread_launch_pinned(reactor->lcore, _spdk_reactor_run, reactor);
		}
		spdk_cpuset_set_cpu(g_spdk_app_core_mask, i, true);
	}

	/* Start the master reactor */
	reactor = spdk_reactor_get(current_core);
    /* 当前core继续执行_spdk_reactor_run */
	_spdk_reactor_run(reactor);

	spdk_env_thread_wait_all();

	g_reactor_state = SPDK_REACTOR_STATE_SHUTDOWN;
	spdk_cpuset_free(g_spdk_app_core_mask);
	g_spdk_app_core_mask = NULL;
}
```

##### spdk_env_thread_launch_pinned

```c
int spdk_env_thread_launch_pinned(uint32_t core, thread_start_fn fn, void *arg)
{
	int rc;

	/* eal_thread_loop 线程会执行fn，每一个slave cpu都有eal_thread_loop线程*/
	rc = rte_eal_remote_launch(fn, arg, core);

	return rc;
}
```

###### rte_eal_remote_launch

```c
int rte_eal_remote_launch(int (*f)(void *), void *arg, unsigned slave_id)
{
	int n;
	char c = 0;
	int m2s = lcore_config[slave_id].pipe_master2slave[1];
	int s2m = lcore_config[slave_id].pipe_slave2master[0];

	lcore_config[slave_id].f = f;
	lcore_config[slave_id].arg = arg;

	/* send message */
	n = 0;
	while (n == 0 || (n < 0 && errno == EINTR))
		n = write(m2s, &c, 1);

	/* wait ack */
	do {
		n = read(s2m, &c, 1);
	} while (n < 0 && errno == EINTR);

	return 0;
}
```

#### _spdk_reactor_run

```C
static int _spdk_reactor_run(void *arg)
{
	struct spdk_reactor	*reactor = arg;
	struct spdk_poller	*poller;
	uint32_t		event_count;
	uint64_t		idle_started, now;
	uint64_t		spin_cycles, sleep_cycles;
	uint32_t		sleep_us;
	uint32_t		timer_poll_count;
	char			thread_name[32];

	snprintf(thread_name, sizeof(thread_name), "reactor_%u", reactor->lcore);
	/* 会将这些函数赋值给struct spdk_thread *thread; */
	if (spdk_allocate_thread(_spdk_reactor_send_msg, _spdk_reactor_start_poller, _spdk_reactor_stop_poller,
				 reactor, thread_name) == NULL) {
		return -1;
	}
	SPDK_NOTICELOG("Reactor started on core %u on socket %u\n", reactor->lcore, reactor->socket_id);

	spin_cycles = SPDK_REACTOR_SPIN_TIME_USEC * spdk_get_ticks_hz() / SPDK_SEC_TO_USEC;
	sleep_cycles = reactor->max_delay_us * spdk_get_ticks_hz() / SPDK_SEC_TO_USEC;
	idle_started = 0;
	timer_poll_count = 0;
	if (g_context_switch_monitor_enabled) {
		_spdk_reactor_context_switch_monitor_start(reactor, NULL);
	}
	while (1) {
		bool took_action = false;
		/* 处理event */
		event_count = _spdk_event_queue_run_batch(reactor);
		if (event_count > 0) {
			took_action = true;
		}
		/* spdk_poller_register可以注册poller */
		/* 找出active_pollers第一个poller */
		poller = TAILQ_FIRST(&reactor->active_pollers);
		if (poller) {
			/* 将该poller从链表中移除 */
			TAILQ_REMOVE(&reactor->active_pollers, poller, tailq);
			poller->state = SPDK_POLLER_STATE_RUNNING;
			poller->fn(poller->arg);
			if (poller->state == SPDK_POLLER_STATE_UNREGISTERED) {
				free(poller);
			} else {
				poller->state = SPDK_POLLER_STATE_WAITING;
				/* 将该poller添加到链表的末尾，TAILQ_INSERT_TAIL是C库里面的 */
				TAILQ_INSERT_TAIL(&reactor->active_pollers, poller, tailq);
			}
			took_action = true;
		}

		if (timer_poll_count >= SPDK_TIMER_POLL_ITERATIONS) {/* 大于迭代次数 */
			/* 处理timer_pollers */
			poller = TAILQ_FIRST(&reactor->timer_pollers);
			if (poller) {
				now = spdk_get_ticks();

				if (now >= poller->next_run_tick) {
					/* 大于poller->next_run_tick则处理该poller */
					TAILQ_REMOVE(&reactor->timer_pollers, poller, tailq);
					poller->state = SPDK_POLLER_STATE_RUNNING;
					poller->fn(poller->arg);
					if (poller->state == SPDK_POLLER_STATE_UNREGISTERED) {
						free(poller);
					} else {/* 继续添加到timer_pollers中下次进程处理 */
						poller->state = SPDK_POLLER_STATE_WAITING;
						_spdk_poller_insert_timer(reactor, poller, now);
					}
					took_action = true;
				}
			}
			/* timer_poll_count置0 */
			timer_poll_count = 0;
		} else {
			timer_poll_count++; /* poll增加 */
		}

		/* 如果event_count和poller不为空，则took_action为true */
		if (took_action) {
			/* We were busy this loop iteration. Reset the idle timer. */
			idle_started = 0;
		} else if (idle_started == 0) {
			/* We were previously busy, but this loop we took no actions. */
			idle_started = spdk_get_ticks();
		}

		/* Determine if the thread can sleep */
		if (sleep_cycles && idle_started) {
			now = spdk_get_ticks();
			/* spin时间已经大于了spin_cycles */
			if (now >= (idle_started + spin_cycles)) {
				sleep_us = reactor->max_delay_us;

				poller = TAILQ_FIRST(&reactor->timer_pollers);
				/* 如果没有timer_pollers则可以睡眠reactor->max_delay_us */
				if (poller) {
					/* There are timers registered, so don't sleep beyond
					 * when the next timer should fire */
					if (poller->next_run_tick < (now + sleep_cycles)) {
						if (poller->next_run_tick <= now) {
							/* 比now还小，说明要马上处理timer_pollers，所以不能sleep */
							sleep_us = 0;
						} else {
							/* 可以睡到next_run_tick的时间 */
							sleep_us = ((poller->next_run_tick - now) *
								    SPDK_SEC_TO_USEC) / spdk_get_ticks_hz();
						}
					}
				}

				if (sleep_us > 0) {
					usleep(sleep_us);
				}

				/* After sleeping, always poll for timers */
				timer_poll_count = SPDK_TIMER_POLL_ITERATIONS;
			}
		}

		if (g_reactor_state != SPDK_REACTOR_STATE_RUNNING) {
			break;
		}
	}

	_spdk_reactor_context_switch_monitor_stop(reactor, NULL);
	spdk_free_thread();
	return 0;
}
```

