#define SPDK_VHOST_DEFAULT_CONFIG "/usr/local/etc/spdk/vhost.conf"
#define SPDK_VHOST_DEFAULT_MEM_SIZE 1024

static const char *g_pid_path = NULL;
static const char *g_vhost_lock_path = "/var/run/spdk/vhost.lock";
static int g_vhost_lock_fd = -1;

int
main(int argc, char *argv[])
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
		exit(rc);
	}

	g_vhost_lock_fd = open(g_vhost_lock_path, O_RDONLY | O_CREAT, 0600);

	rc = flock(g_vhost_lock_fd, LOCK_EX | LOCK_NB);

	if (g_pid_path) {
		save_pid(g_pid_path);
	}

	/* Blocks until the application is exiting */
	rc = spdk_app_start(&opts, vhost_started, NULL, NULL);

	spdk_app_fini();

	return rc;
}


struct spdk_reactor {
	/* Logical core number for this reactor. */
	uint32_t					lcore;

	/* Socket ID for this reactor. */
	uint32_t					socket_id;

	/* Poller for get the rusage for the reactor. */
	struct spdk_poller				*rusage_poller;

	/* The last known rusage values */
	struct rusage					rusage;

	/*
	 * Contains pollers actively running on this reactor.  Pollers
	 *  are run round-robin. The reactor takes one poller from the head
	 *  of the ring, executes it, then puts it back at the tail of
	 *  the ring.
	 */
	TAILQ_HEAD(, spdk_poller)			active_pollers;

	/**
	 * Contains pollers running on this reactor with a periodic timer.
	 */
	TAILQ_HEAD(timer_pollers_head, spdk_poller)	timer_pollers;

	struct spdk_ring				*events;

	/* Pointer to the per-socket g_spdk_event_mempool for this reactor. */
	struct spdk_mempool				*event_mempool;

	uint64_t					max_delay_us;
} __attribute__((aligned(64)));


struct spdk_poller {
	TAILQ_ENTRY(spdk_poller)	tailq;
	uint32_t			lcore;

	/* Current state of the poller; should only be accessed from the poller's thread. */
	enum spdk_poller_state		state;

	uint64_t			period_ticks;
	uint64_t			next_run_tick;
	spdk_poller_fn			fn;
	void				*arg;
};


static void
vhost_app_opts_init(struct spdk_app_opts *opts)
{
	spdk_app_opts_init(opts);
	opts->name = "vhost";
	opts->config_file = SPDK_VHOST_DEFAULT_CONFIG;
	opts->mem_size = SPDK_VHOST_DEFAULT_MEM_SIZE;
}

void
spdk_app_opts_init(struct spdk_app_opts *opts)
{

	memset(opts, 0, sizeof(*opts));

	opts->enable_coredump = true;
	opts->shm_id = -1;
	opts->mem_size = SPDK_APP_DPDK_DEFAULT_MEM_SIZE;
	opts->master_core = SPDK_APP_DPDK_DEFAULT_MASTER_CORE;
	opts->mem_channel = SPDK_APP_DPDK_DEFAULT_MEM_CHANNEL;
	opts->reactor_mask = NULL;
	opts->max_delay_us = 0;
	opts->print_level = SPDK_APP_DEFAULT_LOG_PRINT_LEVEL;
	opts->rpc_addr = SPDK_DEFAULT_RPC_ADDR;
}

#define SPDK_APP_GETOPT_STRING "c:de:F:ghi:m:n:p:qr:s:t:uv"

pdk_app_parse_args_rvals_t
spdk_app_parse_args(int argc, char **argv, struct spdk_app_opts *opts,
		    const char *app_getopt_str, void (*app_parse)(int ch, char *arg),
		    void (*app_usage)(void))
{
	int ch, rc;
	struct spdk_app_opts default_opts;
	char *getopt_str;
	spdk_app_parse_args_rvals_t rval = SPDK_APP_PARSE_ARGS_SUCCESS;

	memcpy(&default_opts, opts, sizeof(default_opts));

	if (opts->config_file && access(opts->config_file, F_OK) != 0) {
		opts->config_file = NULL;
	}

	getopt_str = spdk_sprintf_alloc("%s%s", app_getopt_str, SPDK_APP_GETOPT_STRING);

	while ((ch = getopt(argc, argv, getopt_str)) != -1) {
		switch (ch) {
		case 'c':
			opts->config_file = optarg;
			break;
		case 'd':
			opts->enable_coredump = false;
			break;
		case 'e':
			opts->tpoint_group_mask = optarg;
			break;
		case 'F':
			opts->mem_file = optarg;
			break;
		case 'g':
			opts->hugepage_single_segments = true;
			break;
		case 'h':
			usage(argv[0], &default_opts, app_usage);
			rval = SPDK_APP_PARSE_ARGS_HELP;
			goto parse_done;
		case 'i':
			opts->shm_id = atoi(optarg);
			break;
		case 'm':
			opts->reactor_mask = optarg;
			break;
		case 'n':
			opts->mem_channel = atoi(optarg);
			break;
		case 'p':
			opts->master_core = atoi(optarg);
			break;
		case 'q':
			opts->print_level = SPDK_LOG_WARN;
			break;
		case 'r':
			opts->rpc_addr = optarg;
			break;
		case 's': {
			uint64_t mem_size_mb;
			bool mem_size_has_prefix;

			rc = spdk_parse_capacity(optarg, &mem_size_mb, &mem_size_has_prefix);
			if (mem_size_has_prefix) {
				/* the mem size is in MB by default, so if a prefix was
				 * specified, we need to manually convert to MB.
				 */
				mem_size_mb /= 1024 * 1024;
			}

			opts->mem_size = (int) mem_size_mb;
			break;
		}
		case 't':
			rc = spdk_log_set_trace_flag(optarg);
			opts->print_level = SPDK_LOG_DEBUG;
		case 'u':
			opts->no_pci = true;
			break;
		case 'v':
			version();
			exit(0);
		case '?':
			/*
			 * In the event getopt() above detects an option
			 * in argv that is NOT in the getopt_str,
			 * getopt() will return a '?' indicating failure.
			 */
			usage(argv[0], &default_opts, app_usage);
			rval = SPDK_APP_PARSE_ARGS_FAIL;
			goto parse_done;
		default:
			app_parse(ch, optarg); /* 调用vhost_parse_arg */
		}
	}

parse_done:
	free(getopt_str);

parse_early_fail:
	return rval;
}

static void
vhost_parse_arg(int ch, char *arg)
{
	switch (ch) {
	case 'f':
		g_pid_path = arg;
		break;
	case 'S':
		spdk_vhost_set_socket_path(arg);
		break;
	}
}

int
spdk_vhost_set_socket_path(const char *basename)
{
	int ret;

	if (basename && strlen(basename) > 0) {
		ret = snprintf(dev_dirname, sizeof(dev_dirname) - 2, "%s", basename);

		if (dev_dirname[ret - 1] != '/') {
			dev_dirname[ret] = '/';
			dev_dirname[ret + 1]  = '\0';
		}
	}

	return 0;
}

int
spdk_app_start(struct spdk_app_opts *opts, spdk_event_fn start_fn,
	       void *arg1, void *arg2)
{
	struct spdk_conf	*config = NULL;
	int			rc;
	struct spdk_event	*app_start_event;

	/* g_spdk_log_print_level = opts->print_level */
	spdk_log_set_print_level(opts->print_level);

#ifndef SPDK_NO_RLIMIT
	if (opts->enable_coredump) {
		struct rlimit core_limits;

		core_limits.rlim_cur = core_limits.rlim_max = RLIM_INFINITY;
		setrlimit(RLIMIT_CORE, &core_limits);
	}
#endif
	/* 解析opts->config_file文件，默认在/usr/local/etc/spdk/vhost.conf
	 * 将解析的结果保存在default_config全局变量中
	 */
	config = spdk_app_setup_conf(opts->config_file);

	/* 从解析的config文件中读取Global section的内容 */
	spdk_app_read_config_file_global_params(opts);

	spdk_log_set_level(SPDK_APP_DEFAULT_LOG_LEVEL);
	spdk_log_open();

	if (spdk_app_setup_env(opts) < 0) {
	}

	SPDK_NOTICELOG("Total cores available: %d\n", spdk_env_get_core_count());

	/*
	 * If mask not specified on command line or in configuration file,
	 *  reactor_mask will be 0x1 which will enable core 0 to run one
	 *  reactor.
	 */
	if ((rc = spdk_reactors_init(opts->max_delay_us)) != 0) {
	}

	/*
	 * Note the call to spdk_app_setup_trace() is located here
	 * ahead of spdk_app_setup_signal_handlers().
	 * That's because there is not an easy/direct clean
	 * way of unwinding alloc'd resources that can occur
	 * in spdk_app_setup_signal_handlers().
	 */
	if (spdk_app_setup_trace(opts) != 0) {
		goto app_start_log_close_err;
	}

	/* 设置信号处理函数 */
	if ((rc = spdk_app_setup_signal_handlers(opts)) != 0) {

	}

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

/**
[Global]
[Nvme]
  TransportID "trtype:PCIe traddr:10000:02:00.0" 632f5018-0c5c-49b7-9aa9-aba41291c3d3-nvme ldisk-4sa0xp63
  TransportID "trtype:PCIe traddr:10000:04:00.0" 272869e5-3721-463e-9d5b-d7ab22f7fbf7-nvme ldisk-9iz94i11
  TransportID "trtype:PCIe traddr:10000:03:00.0" 5911f9f2-f462-41e0-bb92-d3baa4a8b5b0-nvme ldisk-oj4jixvr
  TransportID "trtype:PCIe traddr:10000:01:00.0" dcc91ede-3d1a-4a93-8ea5-f8ba74637ee4-nvme ldisk-j5hja161
[VhostBlk1]
  Name vhost_blk_socket-632f5018-0c5c-49b7-9aa9-aba41291c3d3-nvme
  Dev 632f5018-0c5c-49b7-9aa9-aba41291c3d3-nvmen1
  Cpumask 0x10
[VhostBlk0]
  Name vhost_blk_socket-dcc91ede-3d1a-4a93-8ea5-f8ba74637ee4-nvme
  Dev dcc91ede-3d1a-4a93-8ea5-f8ba74637ee4-nvmen1
  Cpumask 0x10
[VhostBlk3]
  Name vhost_blk_socket-272869e5-3721-463e-9d5b-d7ab22f7fbf7-nvme
  Dev 272869e5-3721-463e-9d5b-d7ab22f7fbf7-nvmen1
  Cpumask 0x1000000
[VhostBlk2]
  Name vhost_blk_socket-5911f9f2-f462-41e0-bb92-d3baa4a8b5b0-nvme
  Dev 5911f9f2-f462-41e0-bb92-d3baa4a8b5b0-nvmen1
  Cpumask 0x1000000
*/

static struct spdk_conf *default_config = NULL;

static struct spdk_conf *
spdk_app_setup_conf(const char *config_file)
{
	struct spdk_conf *config;
	int rc;
	/* 分配大小为struct spdk_conf的内存 */
	config = spdk_conf_allocate();
	if (config_file) {
		/* 解析配置文件,配置文件如上 */
		rc = spdk_conf_read(config, config_file);
		if (spdk_conf_first_section(config) == NULL) {
			SPDK_ERRLOG("Invalid config file %s\n", config_file);
			goto error;
		}
	}
	/* default_config = cp;*/
	spdk_conf_set_as_default(config);
	return config;
}

static void
spdk_app_read_config_file_global_params(struct spdk_app_opts *opts)
{
	struct spdk_conf_section *sp;
	/*  Global查找Global*/
	sp = spdk_conf_find_section(NULL, "Global");

	if (opts->shm_id == -1) {
		if (sp != NULL) {
			opts->shm_id = spdk_conf_section_get_intval(sp, "SharedMemoryID");
		}
	}

	if (opts->reactor_mask == NULL) {
		if (sp && spdk_conf_section_get_val(sp, "ReactorMask")) {
			opts->reactor_mask = spdk_conf_section_get_val(sp, "ReactorMask");
		} else {
			opts->reactor_mask = SPDK_APP_DPDK_DEFAULT_CORE_MASK;
		}
	}

	if (!opts->no_pci && sp) {
		opts->no_pci = spdk_conf_section_get_boolval(sp, "NoPci", false);
	}

	if (opts->tpoint_group_mask == NULL) {
		if (sp != NULL) {
			opts->tpoint_group_mask = spdk_conf_section_get_val(sp, "TpointGroupMask");
		}
	}
}

static int
spdk_app_setup_env(struct spdk_app_opts *opts)
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

void
spdk_env_opts_init(struct spdk_env_opts *opts)
{
	memset(opts, 0, sizeof(*opts));

	opts->name = SPDK_ENV_DPDK_DEFAULT_NAME;
	opts->core_mask = SPDK_ENV_DPDK_DEFAULT_CORE_MASK;
	opts->shm_id = SPDK_ENV_DPDK_DEFAULT_SHM_ID;
	opts->mem_size = SPDK_ENV_DPDK_DEFAULT_MEM_SIZE;
	opts->master_core = SPDK_ENV_DPDK_DEFAULT_MASTER_CORE;
	opts->mem_channel = SPDK_ENV_DPDK_DEFAULT_MEM_CHANNEL;
	opts->mem_file = NULL;
}

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

	fflush(stdout);
	orig_optind = optind;
	optind = 1;
	/* 使用上面构造的参数进行rte_eal的初始化 */
	rc = rte_eal_init(eal_cmdline_argcount, dpdk_args);
	optind = orig_optind;

	free(dpdk_args);

	
	if (opts->shm_id < 0 && !opts->hugepage_single_segments) {
		/*
		 * Unlink hugepage and config info files after init.  This will ensure they get
		 *  deleted on app exit, even if the app crashes and does not exit normally.
		 *  Only do this when not in multi-process mode, since for multi-process other
		 *  apps will need to open these files. These files are not created for
		 *  "single file segments".
		 */
		spdk_env_unlink_shared_files();
	}

	if (spdk_mem_map_init() < 0) {
	}

	/* 建立用户空间页表虚拟地址到物理地址 */
	if (spdk_vtophys_init() < 0) {
	
	}

	return 0;
}

#define SPDK_MAX_SOCKET		64
#define SPDK_APP_HUGEPAGE_FILE	"vhost_spdk_reserved"

static char **eal_cmdline;
static int eal_cmdline_argcount;


static int
spdk_build_eal_cmdline(const struct spdk_env_opts *opts)
{
	int argcount = 0;
	char **args;

	args = NULL;

	/* set the program name 
	 * 将格式化的参数保存到args中
	 */
	args = spdk_push_arg(args, &argcount, _sprintf_alloc("%s", opts->name));
	

	/* set the coremask */
	/* NOTE: If coremask starts with '[' and ends with ']' it is a core list
	 */
	if (opts->core_mask[0] == '[') {
		char *l_arg = _sprintf_alloc("-l %s", opts->core_mask + 1);
		int len = strlen(l_arg);
		if (l_arg[len - 1] == ']') {
			l_arg[len - 1] = '\0';
		}
		args = spdk_push_arg(args, &argcount, l_arg);
	} else {
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("-c %s", opts->core_mask));
	}

	/* set the memory channel number */
	if (opts->mem_channel > 0) {
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("-n %d", opts->mem_channel));
	}

	/* set the memory size */
	if (opts->mem_size > 0) {
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("-m %d", opts->mem_size));
	}

	/* set the hugetlbfs file */
	if (opts->mem_file) {
		uint64_t nr_hugepages;
		int32_t i, socket_count = 1;
		int32_t ret;
		char *socket_file;
		char *mem_file;

#ifdef RTE_EAL_NUMA_AWARE_HUGEPAGES
                socket_count = numa_max_node() + 1;
#endif
		if (socket_count <= 0) {
			return -1;
		}

		mem_file = malloc(2048);

		memset(mem_file, 0, 2048);

		nr_hugepages = spdk_max(opts->mem_size/1024, socket_count);
		for (i = 0; i < socket_count; i++) {
			socket_file = _sprintf_alloc("%s/%s_%d", opts->mem_file, SPDK_APP_HUGEPAGE_FILE, i);
			strcat(mem_file, socket_file);

			if (i < socket_count - 1) {
				strcat(mem_file, ",");
			}
			/* 每个socket保留1M的映射 */
			ret = spdk_reserve_hugepage(socket_file, i, (nr_hugepages / socket_count)<<30);
			free(socket_file);

		}

		args = spdk_push_arg(args, &argcount, _sprintf_alloc("--mem-file"));
	
		args = spdk_push_arg(args, &argcount, mem_file);
	}

	/* set the master core */
	if (opts->master_core > 0) {
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("--master-lcore=%d",
				     opts->master_core));
	}

	/* set no pci  if enabled */
	if (opts->no_pci) {
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("--no-pci"));
	}

	/* create just one hugetlbfs file */
	if (opts->hugepage_single_segments) {
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("--single-file-segments"));
	}

#ifdef __linux__
	if (opts->shm_id < 0) {
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("--file-prefix=spdk_pid%d",
				     getpid()));
	} else {
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("--file-prefix=spdk%d",
				     opts->shm_id));

		/* set the base virtual address */
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("--base-virtaddr=0x1000000000"));
		/* set the process type */
		args = spdk_push_arg(args, &argcount, _sprintf_alloc("--proc-type=auto"));
	}
#endif

	eal_cmdline = args;
	eal_cmdline_argcount = argcount;
	/* 进程退出的时候会调用 */
	if (atexit(spdk_destruct_eal_cmdline) != 0) {
		fprintf(stderr, "Failed to register cleanup handler\n");
	}

	return argcount;
}


static int
spdk_reserve_hugepage(const char *file, int socket, size_t size)
{
	void *addr;
	int fd;

#ifdef RTE_EAL_NUMA_AWARE_HUGEPAGES
	numa_set_preferred(socket);
#endif

	fd = open(file, O_CREAT | O_RDWR, 0600);

	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
	if (addr)
		munmap(addr, size); /* 为什么要munmap */
	close(fd);

	return addr == MAP_FAILED ? -1 : 0;
}

/* Launch threads, called at application init(). */
int
rte_eal_init(int argc, char **argv)
{
	int i, fctret, ret;
	pthread_t thread_id;
	static rte_atomic32_t run_once = RTE_ATOMIC32_INIT(0);
	const char *logid;
	char cpuset[RTE_CPU_AFFINITY_STR_LEN];
	char thread_name[RTE_MAX_THREAD_NAME_LEN];

	/* checks if the machine is adequate */
	if (!rte_cpu_is_supported()) {
	}

	if (!rte_atomic32_test_and_set(&run_once)) {
	}

	logid = strrchr(argv[0], '/');
	logid = strdup(logid ? logid + 1: argv[0]);

	thread_id = pthread_self();

	eal_reset_internal_config(&internal_config);

	/* set log level as early as possible 
	 * 解析OPT_LOG_LEVEL_NUM 命令
	 */
	eal_log_level_parse(argc, argv);

	/* 初始化cpu的数据结构，赋初值 */
	if (rte_eal_cpu_init() < 0) {
	}

	/* 解析的值会赋值给internal_config */
	fctret = eal_parse_args(argc, argv);
	
	if (eal_plugins_init() < 0) {

	}

	if (eal_option_device_parse()) {
	
	}

	if (rte_bus_scan()) {
	}

	/* autodetect the iova mapping mode (default is iova_pa) */
	rte_eal_get_configuration()->iova_mode = rte_bus_get_iommu_class();

	/* Workaround for KNI which requires physical address to work */
	if (rte_eal_get_configuration()->iova_mode == RTE_IOVA_VA &&
			rte_eal_check_module("rte_kni") == 1) {
		rte_eal_get_configuration()->iova_mode = RTE_IOVA_PA;
		RTE_LOG(WARNING, EAL,
			"Some devices want IOVA as VA but PA will be used because.. "
			"KNI module inserted\n");
	}

	/* 好像没有这个选项 */
	if (internal_config.no_hugetlbfs == 0 &&
			internal_config.process_type != RTE_PROC_SECONDARY &&
			eal_hugepage_info_init() < 0) {
	}

	if (internal_config.memory == 0 && internal_config.force_sockets == 0) {
		if (internal_config.no_hugetlbfs)
			internal_config.memory = MEMSIZE_IF_NO_HUGE_PAGE;
	}

	rte_srand(rte_rdtsc());

	rte_config_init();

	if (rte_eal_log_init(logid, internal_config.syslog_facility) < 0) {
	}

#ifdef VFIO_PRESENT
	if (rte_eal_vfio_setup() < 0) {
	}
#endif

	if (rte_eal_memory_init() < 0) {
	}

	/* the directories are locked during eal_hugepage_info_init */
	eal_hugedirs_unlock();

	if (rte_eal_memzone_init() < 0) {
	}

	if (rte_eal_tailqs_init() < 0) {
	}

	if (rte_eal_alarm_init() < 0) {
	}

	if (rte_eal_timer_init() < 0) {
	}

	eal_check_mem_on_local_socket();

	eal_thread_init_master(rte_config.master_lcore);

	ret = eal_thread_dump_affinity(cpuset, RTE_CPU_AFFINITY_STR_LEN);

	RTE_LOG(DEBUG, EAL, "Master lcore %u is ready (tid=%x;cpuset=[%s%s])\n",
		rte_config.master_lcore, (int)thread_id, cpuset,
		ret == 0 ? "" : "...");

	if (rte_eal_intr_init() < 0) {
		rte_eal_init_alert("Cannot init interrupt-handling thread\n");
		return -1;
	}

	RTE_LCORE_FOREACH_SLAVE(i) {

		/*
		 * create communication pipes between master thread
		 * and children
		 */
		if (pipe(lcore_config[i].pipe_master2slave) < 0)
			rte_panic("Cannot create pipe\n");
		if (pipe(lcore_config[i].pipe_slave2master) < 0)
			rte_panic("Cannot create pipe\n");

		lcore_config[i].state = WAIT;

		/* create a thread for each lcore */
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
	/* Probe all the buses and devices/drivers on them */
	if (rte_bus_probe()) {
	}

	/* initialize default service/lcore mappings and start running. Ignore
	 * -ENOTSUP, as it indicates no service coremask passed to EAL.
	 */
	ret = rte_service_start_with_defaults();

	rte_eal_mcfg_complete();

	return fctret;
}

void
eal_reset_internal_config(struct internal_config *internal_cfg)
{
	int i;

	internal_cfg->memory = 0;
	internal_cfg->force_nrank = 0;
	internal_cfg->force_nchannel = 0;
	internal_cfg->hugefile_prefix = HUGEFILE_PREFIX_DEFAULT;
	internal_cfg->hugepage_dir = NULL;
	internal_cfg->force_sockets = 0;
	/* zero out the NUMA config */
	for (i = 0; i < RTE_MAX_NUMA_NODES; i++)
		internal_cfg->socket_mem[i] = 0;
	/* zero out hugedir descriptors */
	for (i = 0; i < MAX_HUGEPAGE_SIZES; i++)
		internal_cfg->hugepage_info[i].lock_descriptor = -1;
	internal_cfg->base_virtaddr = 0;

	internal_cfg->syslog_facility = LOG_DAEMON;

	/* if set to NONE, interrupt mode is determined automatically */
	internal_cfg->vfio_intr_mode = RTE_INTR_MODE_NONE;

#ifdef RTE_LIBEAL_USE_HPET
	internal_cfg->no_hpet = 0;
#else
	internal_cfg->no_hpet = 1;
#endif
	internal_cfg->vmware_tsc_map = 0;
	internal_cfg->create_uio_dev = 0;
	internal_cfg->mbuf_pool_ops_name = RTE_MBUF_DEFAULT_MEMPOOL_OPS;
}


/* internal configuration (per-core) */
struct lcore_config lcore_config[RTE_MAX_LCORE];

/*
 * Parse /sys/devices/system/cpu to get the number of physical and logical
 * processors on the machine. The function will fill the cpu_info
 * structure.
 */
int
rte_eal_cpu_init(void)
{
	/* pointer to global configuration */
	struct rte_config *config = rte_eal_get_configuration();
	unsigned lcore_id;
	unsigned count = 0;

	/*
	 * Parse the maximum set of logical cores, detect the subset of running
	 * ones and enable them by default.
	 */
	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		lcore_config[lcore_id].core_index = count;

		/* init cpuset for per lcore config */
		CPU_ZERO(&lcore_config[lcore_id].cpuset);

		/* in 1:1 mapping, record related cpu detected state */
		lcore_config[lcore_id].detected = eal_cpu_detected(lcore_id);
		if (lcore_config[lcore_id].detected == 0) {
			config->lcore_role[lcore_id] = ROLE_OFF;
			lcore_config[lcore_id].core_index = -1;
			continue;
		}

		/* By default, lcore 1:1 map to cpu id */
		CPU_SET(lcore_id, &lcore_config[lcore_id].cpuset);

		/* By default, each detected core is enabled */
		config->lcore_role[lcore_id] = ROLE_RTE;
		lcore_config[lcore_id].core_role = ROLE_RTE;
		lcore_config[lcore_id].core_id = eal_cpu_core_id(lcore_id);
		lcore_config[lcore_id].socket_id = eal_cpu_socket_id(lcore_id);
		if (lcore_config[lcore_id].socket_id >= RTE_MAX_NUMA_NODES) {
#ifdef RTE_EAL_ALLOW_INV_SOCKET_ID
			lcore_config[lcore_id].socket_id = 0;
#else
			RTE_LOG(ERR, EAL, "Socket ID (%u) is greater than "
				"RTE_MAX_NUMA_NODES (%d)\n",
				lcore_config[lcore_id].socket_id,
				RTE_MAX_NUMA_NODES);
			return -1;
#endif
		}
		RTE_LOG(DEBUG, EAL, "Detected lcore %u as "
				"core %u on socket %u\n",
				lcore_id, lcore_config[lcore_id].core_id,
				lcore_config[lcore_id].socket_id);
		count++;
	}
	/* Set the count of enabled logical cores of the EAL configuration */
	config->lcore_count = count;
	RTE_LOG(DEBUG, EAL,
		"Support maximum %u logical core(s) by configuration.\n",
		RTE_MAX_LCORE);
	RTE_LOG(INFO, EAL, "Detected %u lcore(s)\n", config->lcore_count);

	return 0;
}

/* Address of global and public configuration */
static struct rte_config rte_config = {
		.mem_config = &early_mem_config,
};


/* Return a pointer to the configuration structure */
struct rte_config *
rte_eal_get_configuration(void)
{
	return &rte_config;
}


#define SYS_CPU_DIR "/sys/devices/system/cpu/cpu%u"
#define CORE_ID_FILE "topology/core_id"
#define NUMA_NODE_PATH "/sys/devices/system/node"

/* Check if a cpu is present by the presence of the cpu information for it */
int
eal_cpu_detected(unsigned lcore_id)
{
	char path[PATH_MAX];
	int len = snprintf(path, sizeof(path), SYS_CPU_DIR
		"/"CORE_ID_FILE, lcore_id);
	if (len <= 0 || (unsigned)len >= sizeof(path))
		return 0;
	if (access(path, F_OK) != 0)
		return 0;

	return 1;
}

/* Get the cpu core id value from the /sys/.../cpuX core_id value */
unsigned
eal_cpu_core_id(unsigned lcore_id)
{
	char path[PATH_MAX];
	unsigned long id;

	int len = snprintf(path, sizeof(path), SYS_CPU_DIR "/%s", lcore_id, CORE_ID_FILE);
	if (len <= 0 || (unsigned)len >= sizeof(path))
		goto err;
	if (eal_parse_sysfs_value(path, &id) != 0)
		goto err;
	return (unsigned)id;

err:
	RTE_LOG(ERR, EAL, "Error reading core id value from %s "
			"for lcore %u - assuming core 0\n", SYS_CPU_DIR, lcore_id);
	return 0;
}

/* internal configuration */
struct internal_config internal_config;


const char
eal_short_options[] =
	"b:" /* pci-blacklist */
	"c:" /* coremask */
	"s:" /* service coremask */
	"d:" /* driver */
	"h"  /* help */
	"l:" /* corelist */
	"S:" /* service corelist */
	"m:" /* memory size */
	"n:" /* memory channels */
	"r:" /* memory ranks */
	"v"  /* version */
	"w:" /* pci-whitelist */
	;

const struct option
eal_long_options[] = {
	{OPT_BASE_VIRTADDR,     1, NULL, OPT_BASE_VIRTADDR_NUM    },
	{OPT_CREATE_UIO_DEV,    0, NULL, OPT_CREATE_UIO_DEV_NUM   },
	{OPT_FILE_PREFIX,       1, NULL, OPT_FILE_PREFIX_NUM      },
	{OPT_HELP,              0, NULL, OPT_HELP_NUM             },
	{OPT_HUGE_DIR,          1, NULL, OPT_HUGE_DIR_NUM         },
	{OPT_HUGE_UNLINK,       0, NULL, OPT_HUGE_UNLINK_NUM      },
	{OPT_LCORES,            1, NULL, OPT_LCORES_NUM           },
	{OPT_LOG_LEVEL,         1, NULL, OPT_LOG_LEVEL_NUM        },
	{OPT_MASTER_LCORE,      1, NULL, OPT_MASTER_LCORE_NUM     },
	{OPT_MBUF_POOL_OPS_NAME, 1, NULL, OPT_MBUF_POOL_OPS_NAME_NUM},
	{OPT_NO_HPET,           0, NULL, OPT_NO_HPET_NUM          },
	{OPT_NO_HUGE,           0, NULL, OPT_NO_HUGE_NUM          },
	{OPT_NO_PCI,            0, NULL, OPT_NO_PCI_NUM           },
	{OPT_NO_SHCONF,         0, NULL, OPT_NO_SHCONF_NUM        },
	{OPT_PCI_BLACKLIST,     1, NULL, OPT_PCI_BLACKLIST_NUM    },
	{OPT_PCI_WHITELIST,     1, NULL, OPT_PCI_WHITELIST_NUM    },
	{OPT_PROC_TYPE,         1, NULL, OPT_PROC_TYPE_NUM        },
	{OPT_SOCKET_MEM,        1, NULL, OPT_SOCKET_MEM_NUM       },
	{OPT_MEM_FILE,          1, NULL, OPT_MEM_FILE_NUM         },
	{OPT_SYSLOG,            1, NULL, OPT_SYSLOG_NUM           },
	{OPT_VDEV,              1, NULL, OPT_VDEV_NUM             },
	{OPT_VFIO_INTR,         1, NULL, OPT_VFIO_INTR_NUM        },
	{OPT_VMWARE_TSC_MAP,    0, NULL, OPT_VMWARE_TSC_MAP_NUM   },
	{0,                     0, NULL, 0                        }
};


/* Parse the argument given in the command line of the application */
static int
eal_parse_args(int argc, char **argv)
{
	int opt, ret;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];
	const int old_optind = optind;
	const int old_optopt = optopt;
	char * const old_optarg = optarg;

	argvopt = argv;
	optind = 1;

	while ((opt = getopt_long(argc, argvopt, eal_short_options,
				  eal_long_options, &option_index)) != EOF) {

		/* getopt is not happy, stop right now */
		if (opt == '?') {
			eal_usage(prgname);
			ret = -1;
			goto out;
		}

		ret = eal_parse_common_option(opt, optarg, &internal_config);
		/* common parser is not happy */
		if (ret < 0) {
			eal_usage(prgname);
			ret = -1;
			goto out;
		}
		/* common parser handled this option */
		if (ret == 0)
			continue;

		switch (opt) {
		case 'h':
			eal_usage(prgname);
			exit(EXIT_SUCCESS);

		case OPT_HUGE_DIR_NUM:
			internal_config.hugepage_dir = strdup(optarg);
			break;

		case OPT_FILE_PREFIX_NUM: /* file-prefix */
			internal_config.hugefile_prefix = strdup(optarg);
			break;

		case OPT_SOCKET_MEM_NUM:
			if (eal_parse_socket_mem(optarg) < 0) {
				RTE_LOG(ERR, EAL, "invalid parameters for --"
					OPT_SOCKET_MEM "\n");
				eal_usage(prgname);
				ret = -1;
				goto out;
			}
			break;

		case OPT_MEM_FILE_NUM: /* mem-file */
			if (eal_parse_mem_file(optarg) < 0) {
				RTE_LOG(ERR, EAL, "invalid parameters for --"
			}
			break;

		case OPT_BASE_VIRTADDR_NUM:
			if (eal_parse_base_virtaddr(optarg) < 0) {
				RTE_LOG(ERR, EAL, "invalid parameter for --"
						OPT_BASE_VIRTADDR "\n");
				eal_usage(prgname);
				ret = -1;
				goto out;
			}
			break;

		case OPT_VFIO_INTR_NUM:
			if (eal_parse_vfio_intr(optarg) < 0) {
				RTE_LOG(ERR, EAL, "invalid parameters for --"
						OPT_VFIO_INTR "\n");
				eal_usage(prgname);
				ret = -1;
				goto out;
			}
			break;

		case OPT_CREATE_UIO_DEV_NUM:
			internal_config.create_uio_dev = 1;
			break;

		case OPT_MBUF_POOL_OPS_NAME_NUM:
			internal_config.mbuf_pool_ops_name = optarg;
			break;

		default:
			eal_usage(prgname);
			ret = -1;
			goto out;
		}
	}

	if (eal_adjust_config(&internal_config) != 0) {
		ret = -1;
		goto out;
	}

	/* sanity checks */
	if (eal_check_common_options(&internal_config) != 0) {
		eal_usage(prgname);
		ret = -1;
		goto out;
	}

	if (optind >= 0)
		argv[optind-1] = prgname;
	ret = optind-1;

out:
	/* restore getopt lib */
	optind = old_optind;
	optopt = old_optopt;
	optarg = old_optarg;

	return ret;
}

int
eal_parse_common_option(int opt, const char *optarg,
			struct internal_config *conf)
{
	static int b_used;
	static int w_used;

	switch (opt) {
	/* blacklist */
	case 'b':
		if (w_used)
			goto bw_used;
		if (eal_option_device_add(RTE_DEVTYPE_BLACKLISTED_PCI,
				optarg) < 0) {
			return -1;
		}
		b_used = 1;
		break;
	/* whitelist */
	case 'w':
		if (b_used)
			goto bw_used;
		if (eal_option_device_add(RTE_DEVTYPE_WHITELISTED_PCI,
				optarg) < 0) {
			return -1;
		}
		w_used = 1;
		break;
	/* coremask */
	case 'c': 	/* 解析cpu 会保存到rte_config中和lcore_config，
				 *会解析出cpu的个数和那些会被使用 
				 */
		if (eal_parse_coremask(optarg) < 0) {
			RTE_LOG(ERR, EAL, "invalid coremask\n");
			return -1;
		}
		core_parsed = 1;
		break;
	/* corelist */
	case 'l':
		if (eal_parse_corelist(optarg) < 0) {
			RTE_LOG(ERR, EAL, "invalid core list\n");
			return -1;
		}
		core_parsed = 1;
		break;
	/* service coremask */
	case 's':
		if (eal_parse_service_coremask(optarg) < 0) {
			RTE_LOG(ERR, EAL, "invalid service coremask\n");
			return -1;
		}
		break;
	/* service corelist */
	case 'S':
		if (eal_parse_service_corelist(optarg) < 0) {
			RTE_LOG(ERR, EAL, "invalid service core list\n");
			return -1;
		}
		break;
	/* size of memory */
	case 'm':
		conf->memory = atoi(optarg);
		conf->memory *= 1024ULL;
		conf->memory *= 1024ULL;
		mem_parsed = 1;
		break;
	/* force number of channels */
	case 'n':
		conf->force_nchannel = atoi(optarg);
		if (conf->force_nchannel == 0) {
			RTE_LOG(ERR, EAL, "invalid channel number\n");
			return -1;
		}
		break;
	/* force number of ranks */
	case 'r':
		conf->force_nrank = atoi(optarg);
		if (conf->force_nrank == 0 ||
		    conf->force_nrank > 16) {
			RTE_LOG(ERR, EAL, "invalid rank number\n");
			return -1;
		}
		break;
	/* force loading of external driver */
	case 'd':
		if (eal_plugin_add(optarg) == -1)
			return -1;
		break;
	case 'v':
		/* since message is explicitly requested by user, we
		 * write message at highest log level so it can always
		 * be seen
		 * even if info or warning messages are disabled */
		RTE_LOG(CRIT, EAL, "RTE Version: '%s'\n", rte_version());
		break;

	/* long options */
	case OPT_HUGE_UNLINK_NUM:
		conf->hugepage_unlink = 1;
		break;

	case OPT_NO_HUGE_NUM:
		conf->no_hugetlbfs = 1;
		break;

	case OPT_NO_PCI_NUM:
		conf->no_pci = 1;
		break;

	case OPT_NO_HPET_NUM:
		conf->no_hpet = 1;
		break;

	case OPT_VMWARE_TSC_MAP_NUM:
		conf->vmware_tsc_map = 1;
		break;

	case OPT_NO_SHCONF_NUM:
		conf->no_shconf = 1;
		break;

	case OPT_PROC_TYPE_NUM:
		conf->process_type = eal_parse_proc_type(optarg);
		break;

	case OPT_MASTER_LCORE_NUM:
		if (eal_parse_master_lcore(optarg) < 0) {
			RTE_LOG(ERR, EAL, "invalid parameter for --"
					OPT_MASTER_LCORE "\n");
			return -1;
		}
		break;

	case OPT_VDEV_NUM:
		if (eal_option_device_add(RTE_DEVTYPE_VIRTUAL,
				optarg) < 0) {
			return -1;
		}
		break;

	case OPT_SYSLOG_NUM:
		if (eal_parse_syslog(optarg, conf) < 0) {
			RTE_LOG(ERR, EAL, "invalid parameters for --"
					OPT_SYSLOG "\n");
			return -1;
		}
		break;

	case OPT_LOG_LEVEL_NUM: {
		if (eal_parse_log_level(optarg) < 0) {
			RTE_LOG(ERR, EAL,
				"invalid parameters for --"
				OPT_LOG_LEVEL "\n");
			return -1;
		}
		break;
	}
	case OPT_LCORES_NUM:
		if (eal_parse_lcores(optarg) < 0) {
			RTE_LOG(ERR, EAL, "invalid parameter for --"
				OPT_LCORES "\n");
			return -1;
		}
		core_parsed = 1;
		break;

	/* don't know what to do, leave this to caller */
	default:
		return 1;

	}

	return 0;
bw_used:
	RTE_LOG(ERR, EAL, "Options blacklist (-b) and whitelist (-w) "
		"cannot be used at the same time\n");
	return -1;
}


int
eal_adjust_config(struct internal_config *internal_cfg)
{
	int i;
	struct rte_config *cfg = rte_eal_get_configuration();

	if (!core_parsed)
		eal_auto_detect_cores(cfg);

	if (internal_config.process_type == RTE_PROC_AUTO)
		internal_config.process_type = eal_proc_type_detect();

	/* default master lcore is the first one */
	if (!master_lcore_parsed) {
		cfg->master_lcore = rte_get_next_lcore(-1, 0, 0);
		lcore_config[cfg->master_lcore].core_role = ROLE_RTE;
	}

	/* if no memory amounts were requested, this will result in 0 and
	 * will be overridden later, right after eal_hugepage_info_init() */
	for (i = 0; i < RTE_MAX_NUMA_NODES; i++)
		internal_cfg->memory += internal_cfg->socket_mem[i];

	return 0;
}

static void
eal_auto_detect_cores(struct rte_config *cfg)
{
	unsigned int lcore_id;
	unsigned int removed = 0;
	rte_cpuset_t affinity_set;
	pthread_t tid = pthread_self();

	if (pthread_getaffinity_np(tid, sizeof(rte_cpuset_t),
				&affinity_set) < 0)
		CPU_ZERO(&affinity_set);

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (cfg->lcore_role[lcore_id] == ROLE_RTE &&
		    !CPU_ISSET(lcore_id, &affinity_set)) {
			cfg->lcore_role[lcore_id] = ROLE_OFF;
			removed++;
		}
	}

	cfg->lcore_count -= removed;
}


static int
eal_parse_mem_file(char *mem_file)
{
	int ret;
	/* 将mem_file按照，好分开，保存到 internal_config.mem_file[i]中*/
	ret = rte_strsplit(mem_file, strlen(mem_file), internal_config.mem_file,
			   RTE_MAX_NUMA_NODES, ',');

	return ret <= 0 ? -1 : 0;
}

static const char *default_solib_dir = RTE_EAL_PMD_PATH;/* RTE_EAL_PMD_PATH = "" */

int
eal_plugins_init(void)
{
	struct shared_driver *solib = NULL;
	struct stat sb;

	if (*default_solib_dir != '\0' && stat(default_solib_dir, &sb) == 0 &&
				S_ISDIR(sb.st_mode))
		eal_plugin_add(default_solib_dir);

	TAILQ_FOREACH(solib, &solib_list, next) {

		if (stat(solib->name, &sb) == 0 && S_ISDIR(sb.st_mode)) {
			if (eal_plugindir_init(solib->name) == -1) {
				return -1;
			}
		} else {
			RTE_LOG(DEBUG, EAL, "open shared lib %s\n", solib->name);
			solib->lib_handle = dlopen(solib->name, RTLD_NOW);
		}

	}
	return 0;
}

int
eal_option_device_parse(void)
{
	struct device_option *devopt;
	void *tmp;
	int ret = 0;

	TAILQ_FOREACH_SAFE(devopt, &devopt_list, next, tmp) {
		if (ret == 0) {
			ret = rte_eal_devargs_add(devopt->type, devopt->arg);
			if (ret)
				RTE_LOG(ERR, EAL, "Unable to parse device '%s'\n",
					devopt->arg);
		}
		TAILQ_REMOVE(&devopt_list, devopt, next);
		free(devopt);
	}
	return ret;
}

/* store a whitelist parameter for later parsing */
int
rte_eal_devargs_add(enum rte_devtype devtype, const char *devargs_str)
{
	struct rte_devargs *devargs = NULL;
	struct rte_bus *bus = NULL;
	const char *dev = devargs_str;

	/* use calloc instead of rte_zmalloc as it's called early at init */
	devargs = calloc(1, sizeof(*devargs));

	if (rte_eal_devargs_parse(dev, devargs))

	devargs->type = devtype;
	bus = devargs->bus;
	if (devargs->type == RTE_DEVTYPE_BLACKLISTED_PCI)
		devargs->policy = RTE_DEV_BLACKLISTED;
	if (bus->conf.scan_mode == RTE_BUS_SCAN_UNDEFINED) {
		if (devargs->policy == RTE_DEV_WHITELISTED)
			bus->conf.scan_mode = RTE_BUS_SCAN_WHITELIST;
		else if (devargs->policy == RTE_DEV_BLACKLISTED)
			bus->conf.scan_mode = RTE_BUS_SCAN_BLACKLIST;
	}
	TAILQ_INSERT_TAIL(&devargs_list, devargs, next);
	return 0;
}

int
rte_eal_devargs_parse(const char *dev, struct rte_devargs *da)
{
	struct rte_bus *bus = NULL;
	const char *devname;
	const size_t maxlen = sizeof(da->name);
	size_t i;

	/* Retrieve eventual bus info */
	do {
		devname = dev;
		bus = rte_bus_find(bus, bus_name_cmp, dev);
		if (bus == NULL)
			break;
		devname = dev + strlen(bus->name) + 1;
		if (rte_bus_find_by_device_name(devname) == bus)
			break;
	} while (1);
	/* Store device name */
	i = 0;
	while (devname[i] != '\0' && devname[i] != ',') {
		da->name[i] = devname[i];
		i++;
	}
	da->name[i] = '\0';
	if (bus == NULL) {
		bus = rte_bus_find_by_device_name(da->name);
	}
	da->bus = bus;
	/* Parse eventual device arguments */
	if (devname[i] == ',')
		da->args = strdup(&devname[i + 1]);
	else
		da->args = strdup("");
	return 0;
}

struct rte_bus *
rte_bus_find(const struct rte_bus *start, rte_bus_cmp_t cmp,
	     const void *data)
{
	struct rte_bus *bus;

	if (start != NULL)
		bus = TAILQ_NEXT(start, next);
	else
		bus = TAILQ_FIRST(&rte_bus_list);
	while (bus != NULL) {
		if (cmp(bus, data) == 0)
			break;
		bus = TAILQ_NEXT(bus, next);
	}
	return bus;
}

static int
bus_name_cmp(const struct rte_bus *bus, const void *name)
{
	return strncmp(bus->name, name, strlen(bus->name));
}

static int
bus_can_parse(const struct rte_bus *bus, const void *_name)
{
	const char *name = _name;

	return !(bus->parse && bus->parse(name, NULL) == 0);
}


struct rte_bus *
rte_bus_find_by_device_name(const char *str)
{
	char name[RTE_DEV_NAME_MAX_LEN];
	char *c;

	snprintf(name, sizeof(name), "%s", str);
	c = strchr(name, ',');
	if (c != NULL)
		c[0] = '\0';
	return rte_bus_find(NULL, bus_can_parse, name);
}


/* Scan all the buses for registered devices */
int
rte_bus_scan(void)
{
	int ret;
	struct rte_bus *bus = NULL;

	/* rte_bus_register会往rte_bus_list添加元素
	 * RTE_REGISTER_BUS宏修饰的会在main函数之前执行
	 */
	TAILQ_FOREACH(bus, &rte_bus_list, next) {
		ret = bus->scan();
		if (ret)
			RTE_LOG(ERR, EAL, "Scan for (%s) bus failed.\n",
				bus->name);
	}

	return 0;
}


#define RTE_INIT_PRIO(func, prio) \
static void __attribute__((constructor(prio), used)) func(void)

#define RTE_REGISTER_BUS(nm, bus) \
RTE_INIT_PRIO(businitfn_ ##nm, 110); \
static void businitfn_ ##nm(void) \
{\
	(bus).name = RTE_STR(nm);\
	rte_bus_register(&bus); \
}

/*
 * Get iommu class of devices on the bus.
 */
enum rte_iova_mode
rte_bus_get_iommu_class(void)
{
	int mode = RTE_IOVA_DC;
	struct rte_bus *bus;

	TAILQ_FOREACH(bus, &rte_bus_list, next) {

		if (bus->get_iommu_class)
			mode |= bus->get_iommu_class();
	}

	if (mode != RTE_IOVA_VA) {
		/* Use default IOVA mode */
		mode = RTE_IOVA_PA;
	}
	return mode;
}

/* Sets up rte_config structure with the pointer to shared memory config.*/
static void
rte_config_init(void)
{
	/* 没有指定，默认就是RTE_PROC_PRIMARY */
	rte_config.process_type = internal_config.process_type;

	switch (rte_config.process_type){
	case RTE_PROC_PRIMARY:
		rte_eal_config_create();
		break;
	case RTE_PROC_SECONDARY:
		rte_eal_config_attach();
		rte_eal_mcfg_wait_complete(rte_config.mem_config);
		rte_eal_config_reattach();
		break;
	case RTE_PROC_AUTO:
	case RTE_PROC_INVALID:
		rte_panic("Invalid process type\n");
	}
}

/* define fd variable here, because file needs to be kept open for the
 * duration of the program, as we hold a write lock on it in the primary proc */
static int mem_cfg_fd = -1;


/* create memory configuration in shared/mmap memory. Take out
 * a write lock on the memsegs, so we can auto-detect primary/secondary.
 * This means we never close the file while running (auto-close on exit).
 * We also don't lock the whole file, so that in future we can use read-locks
 * on other parts, e.g. memzones, to detect if there are running secondary
 * processes. */
static void
rte_eal_config_create(void)
{
	void *rte_mem_cfg_addr;
	int retval;

	const char *pathname = eal_runtime_config_path();

	if (internal_config.no_shconf)
		return;

	/* map the config before hugepage address so that we don't waste a page */
	if (internal_config.base_virtaddr != 0)
		rte_mem_cfg_addr = (void *)
			RTE_ALIGN_FLOOR(internal_config.base_virtaddr -
			sizeof(struct rte_mem_config), sysconf(_SC_PAGE_SIZE));
	else
		rte_mem_cfg_addr = NULL;

	if (mem_cfg_fd < 0){
		mem_cfg_fd = open(pathname, O_RDWR | O_CREAT, 0660);
	}

	retval = ftruncate(mem_cfg_fd, sizeof(*rte_config.mem_config));

	retval = fcntl(mem_cfg_fd, F_SETLK, &wr_lock);

	rte_mem_cfg_addr = mmap(rte_mem_cfg_addr, sizeof(*rte_config.mem_config),
				PROT_READ | PROT_WRITE, MAP_SHARED, mem_cfg_fd, 0);
	/* 将early_mem_config的内容写到rte_mem_cfg_addr中 */
	memcpy(rte_mem_cfg_addr, &early_mem_config, sizeof(early_mem_config));
	rte_config.mem_config = rte_mem_cfg_addr;

	/* store address of the config in the config itself so that secondary
	 * processes could later map the config into this exact location */
	rte_config.mem_config->mem_cfg_addr = (uintptr_t) rte_mem_cfg_addr;

}
static const char *default_config_dir = "/var/run";
/** Path of rte config file. */
#define RUNTIME_CONFIG_FMT "%s/.%s_config"

static inline const char *
eal_runtime_config_path(void)
{
	static char buffer[PATH_MAX]; /* static so auto-zeroed */
	const char *directory = default_config_dir;
	const char *home_dir = getenv("HOME");

	if (getuid() != 0 && home_dir != NULL)
		directory = home_dir;
	/* hugefile_prefix为spdk+pid号，如--file-prefix=spdk_pid178902
	 * buffer为/var/run/.spdk_pid178902
	 */
	snprintf(buffer, sizeof(buffer) - 1, RUNTIME_CONFIG_FMT, directory,
			internal_config.hugefile_prefix);
	return buffer;
}

/* init memory subsystem */
int
rte_eal_memory_init(void)
{
	RTE_LOG(DEBUG, EAL, "Setting up physically contiguous memory...\n");

	const int retval = rte_eal_process_type() == RTE_PROC_PRIMARY ?
			rte_eal_hugepage_init() :
			rte_eal_hugepage_attach();
	if (retval < 0)
		return -1;

	/* internal_config.no_shconf为0 */
	if (internal_config.no_shconf == 0 && rte_eal_memdevice_init() < 0)
		return -1;

	return 0;
}

int
rte_eal_hugepage_init(void)
{
	int ret;

	if (internal_config.mem_file[0])
		ret = rte_eal_hugepage_init_from_file();
	else
		ret = rte_eal_hugepage_init_old_and_slow();

	return ret;
}


static int
rte_eal_hugepage_init_from_file(void)
{
	int i;
	char *file;
	int last_idx = 0;

	for (i = 0; i < RTE_MAX_NUMA_NODES; i++) {
		file = internal_config.mem_file[i];
		if (file) {
			if (init_mem_from_file(file, i, &last_idx) < 0)
				return -1;
		}
	}

	return 0;
}

static int
init_mem_from_file(const char *file, int socket, int *last_idx)
{
	int fd;
	struct stat st;
	void *addr;

	fd = open(file, O_RDWR, 0600);

	if (fstat(fd, &st) < 0) {
	}

	/*
	 * keep MAP_POPULATE here, just in case the provided hugepage file
	 * is not populated before. In such case, we could observe a long
	 * startup time, which means something is wrong.
	 */
	addr = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_POPULATE, fd, 0);

	add_memsegs(addr, st.st_size, socket, st.st_blksize, last_idx);

	return 0;
}

static void
add_memsegs(unsigned char *addr, size_t size, int socket, int page_size, int *last_idx)
{
	unsigned char *end = addr + size;
	unsigned char *next;
	uint64_t phys;

	do {
		phys  = rte_mem_virt2iova(addr);
		next  = addr + page_size;
		while (next < end) {
			if (rte_mem_virt2iova(next) != phys + page_size)
				break;
			next += page_size;
		}
		/* one memseg大小应该是page_size */
		add_one_memseg(addr, phys, next - addr, socket, page_size, last_idx);

		addr = next;
	} while (addr < end);
}

rte_iova_t
rte_mem_virt2iova(const void *virtaddr)
{
	if (rte_eal_iova_mode() == RTE_IOVA_VA)
		return (uintptr_t)virtaddr;
	return rte_mem_virt2phy(virtaddr);
}

static void
add_one_memseg(void *addr, phys_addr_t phys, size_t size, int socket,
	       int page_size, int *last_idx)
{
	struct rte_mem_config *mcfg;
	int i = *last_idx;

	assert(i < RTE_MAX_MEMSEG);

	mcfg = rte_eal_get_configuration()->mem_config;

	mcfg->memseg[i].iova = phys;
	mcfg->memseg[i].addr = addr;
	mcfg->memseg[i].hugepage_sz = page_size;
	mcfg->memseg[i].len = size;
	mcfg->memseg[i].socket_id = socket;

	*last_idx = i + 1;
}

/*
 * Init the memzone subsystem
 */
int
rte_eal_memzone_init(void)
{
	struct rte_mem_config *mcfg;
	const struct rte_memseg *memseg;

	/* get pointer to global configuration */
	mcfg = rte_eal_get_configuration()->mem_config;

	/* secondary processes don't need to initialise anything */
	if (rte_eal_process_type() == RTE_PROC_SECONDARY)
		return 0;

	memseg = rte_eal_get_physmem_layout();

	rte_rwlock_write_lock(&mcfg->mlock);

	/* delete all zones */
	mcfg->memzone_cnt = 0;
	memset(mcfg->memzone, 0, sizeof(mcfg->memzone));

	rte_rwlock_write_unlock(&mcfg->mlock);

	return rte_eal_malloc_heap_init();
}

const struct rte_memseg *
rte_eal_get_physmem_layout(void)
{
	return rte_eal_get_configuration()->mem_config->memseg;
}

int
rte_eal_malloc_heap_init(void)
{
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;
	unsigned ms_cnt;
	struct rte_memseg *ms;

	for (ms = &mcfg->memseg[0], ms_cnt = 0;
			(ms_cnt < RTE_MAX_MEMSEG) && (ms->len > 0);
			ms_cnt++, ms++) {
		malloc_heap_add_memseg(&mcfg->malloc_heaps[ms->socket_id], ms);
	}

	return 0;
}

/*
 * Expand the heap with a memseg.
 * This reserves the zone and sets a dummy malloc_elem header at the end
 * to prevent overflow. The rest of the zone is added to free list as a single
 * large free block
 */
static void
malloc_heap_add_memseg(struct malloc_heap *heap, struct rte_memseg *ms)
{
	/* allocate the memory block headers, one at end, one at start */
	struct malloc_elem *start_elem = (struct malloc_elem *)ms->addr;
	struct malloc_elem *end_elem = RTE_PTR_ADD(ms->addr,
			ms->len - MALLOC_ELEM_OVERHEAD);
	end_elem = RTE_PTR_ALIGN_FLOOR(end_elem, RTE_CACHE_LINE_SIZE);
	const size_t elem_size = (uintptr_t)end_elem - (uintptr_t)start_elem;

	malloc_elem_init(start_elem, heap, ms, elem_size);
	malloc_elem_mkend(end_elem, start_elem);
	malloc_elem_free_list_insert(start_elem);

	heap->total_size += elem_size;
}

/*
 * Add the specified element to its heap's free list.
 */
void
malloc_elem_free_list_insert(struct malloc_elem *elem)
{
	size_t idx;

	idx = malloc_elem_free_list_index(elem->size - MALLOC_ELEM_HEADER_LEN);
	elem->state = ELEM_FREE;
	LIST_INSERT_HEAD(&elem->heap->free_head[idx], elem, free_list);
}

int
rte_eal_tailqs_init(void)
{
	struct rte_tailq_elem *t;

	rte_tailqs_count = 0;

	/*EAL_REGISTER_TAILQ会进行注册*/
	TAILQ_FOREACH(t, &rte_tailq_elem_head, next) {
		/* second part of register job for "early" tailqs, see
		 * rte_eal_tailq_register and EAL_REGISTER_TAILQ */
		rte_eal_tailq_update(t);
	}

	return 0;
}

static void
rte_eal_tailq_update(struct rte_tailq_elem *t)
{
	if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
		/* primary process is the only one that creates */
		t->head = rte_eal_tailq_create(t->name);
	} else {
		t->head = rte_eal_tailq_lookup(t->name);
	}
}

static struct rte_tailq_head *
rte_eal_tailq_create(const char *name)
{
	struct rte_tailq_head *head = NULL;

	if (!rte_eal_tailq_lookup(name) &&
	    (rte_tailqs_count + 1 < RTE_MAX_TAILQ)) {
		struct rte_mem_config *mcfg;

		mcfg = rte_eal_get_configuration()->mem_config;
		head = &mcfg->tailq_head[rte_tailqs_count];
		snprintf(head->name, sizeof(head->name) - 1, "%s", name);
		TAILQ_INIT(&head->tailq_head);
		rte_tailqs_count++;
	}

	return head;
}

struct rte_tailq_head *
rte_eal_tailq_lookup(const char *name)
{
	unsigned i;
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;

	if (name == NULL)
		return NULL;

	for (i = 0; i < RTE_MAX_TAILQ; i++) {
		if (!strncmp(name, mcfg->tailq_head[i].name,
			     RTE_TAILQ_NAMESIZE-1))
			return &mcfg->tailq_head[i];
	}

	return NULL;
}

static struct rte_intr_handle intr_handle = {.fd = -1 };


int
rte_eal_alarm_init(void)
{
	intr_handle.type = RTE_INTR_HANDLE_ALARM;
	/* create a timerfd file descriptor */
	intr_handle.fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	return 0;
}

enum timer_source eal_timer_source = EAL_TIMER_HPET;


int
rte_eal_timer_init(void)
{

	eal_timer_source = EAL_TIMER_TSC;

	set_tsc_freq();
	check_tsc_flags();
	return 0;
}

void
set_tsc_freq(void)
{
	uint64_t freq;

	freq = get_tsc_freq_arch();
	if (!freq)
		freq = get_tsc_freq();
	if (!freq)
		freq = estimate_tsc_freq();

	RTE_LOG(DEBUG, EAL, "TSC frequency is ~%" PRIu64 " KHz\n", freq / 1000);
	eal_tsc_resolution_hz = freq;
}

uint64_t
get_tsc_freq_arch(void)
{
	uint64_t tsc_hz = 0;
	uint32_t a, b, c, d, maxleaf;
	uint8_t mult, model;
	int32_t ret;

	/*
	 * Time Stamp Counter and Nominal Core Crystal Clock
	 * Information Leaf
	 */
	maxleaf = __get_cpuid_max(0, NULL);

	if (maxleaf >= 0x15) {
		__cpuid(0x15, a, b, c, d);

		/* EBX : TSC/Crystal ratio, ECX : Crystal Hz */
		if (b && c)
			return c * (b / a);
	}

	__cpuid(0x1, a, b, c, d);
	model = rte_cpu_get_model(a);

	if (check_model_wsm_nhm(model))
		mult = 133;
	else if ((c & bit_AVX) || check_model_gdm_dnv(model))
		mult = 100;
	else
		return 0;

	ret = rdmsr(0xCE, &tsc_hz);
	if (ret < 0)
		return 0;

	return ((tsc_hz >> 8) & 0xff) * mult * 1E6;
}

static void
eal_check_mem_on_local_socket(void)
{
	const struct rte_memseg *ms;
	int i, socket_id;

	socket_id = rte_lcore_to_socket_id(rte_config.master_lcore);

	ms = rte_eal_get_physmem_layout();

	for (i = 0; i < RTE_MAX_MEMSEG; i++)
		if (ms[i].socket_id == socket_id &&
				ms[i].len > 0)
			return;

	RTE_LOG(WARNING, EAL, "WARNING: Master core has no "
			"memory on local socket!\n");
}

void eal_thread_init_master(unsigned lcore_id)
{
	/* set the lcore ID in per-lcore memory area */
	RTE_PER_LCORE(_lcore_id) = lcore_id;

	/* set CPU affinity */
	if (eal_thread_set_affinity() < 0)
		rte_panic("cannot set affinity\n");
}

int
eal_thread_dump_affinity(char *str, unsigned size)
{
	rte_cpuset_t cpuset;
	unsigned cpu;
	int ret;
	unsigned int out = 0;

	rte_thread_get_affinity(&cpuset);

	for (cpu = 0; cpu < RTE_MAX_LCORE; cpu++) {
		if (!CPU_ISSET(cpu, &cpuset))
			continue;

		ret = snprintf(str + out,
			       size - out, "%u,", cpu);
		out += ret;
	}

	ret = 0;
exit:
	/* remove the last separator */
	if (out > 0)
		str[out - 1] = '\0';

	return ret;
}

int
rte_eal_intr_init(void)
{
	int ret = 0, ret_1 = 0;
	char thread_name[RTE_MAX_THREAD_NAME_LEN];

	/* init the global interrupt source head */
	TAILQ_INIT(&intr_sources);

	/**
	 * create a pipe which will be waited by epoll and notified to
	 * rebuild the wait list of epoll.
	 */
	if (pipe(intr_pipe.pipefd) < 0) {
	}

	/* create the host thread to wait/handle the interrupt */
	ret = pthread_create(&intr_thread, NULL, eal_intr_thread_main, NULL);
	if (ret != 0) {

	} else {
		/* Set thread_name for aid in debugging. */
		snprintf(thread_name, RTE_MAX_THREAD_NAME_LEN, "eal-intr-thread");
		ret_1 = rte_thread_setname(intr_thread, thread_name);
	}

	return -ret;
}

/**
 * It builds/rebuilds up the epoll file descriptor with all the
 * file descriptors being waited on. Then handles the interrupts.
 *
 * @param arg
 *  pointer. (unused)
 *
 * @return
 *  never return;
 */
static __attribute__((noreturn)) void *
eal_intr_thread_main(__rte_unused void *arg)
{
	struct epoll_event ev;

	/* host thread, never break out */
	for (;;) {
		/* build up the epoll fd with all descriptors we are to
		 * wait on then pass it to the handle_interrupts function
		 */
		static struct epoll_event pipe_event = {
			.events = EPOLLIN | EPOLLPRI,
		};
		struct rte_intr_source *src;
		unsigned numfds = 0;

		/* create epoll fd */
		int pfd = epoll_create(1);

		pipe_event.data.fd = intr_pipe.readfd;
		/**
		 * add pipe fd into wait list, this pipe is used to
		 * rebuild the wait list.
		 */
		if (epoll_ctl(pfd, EPOLL_CTL_ADD, intr_pipe.readfd, &pipe_event) < 0) {
		
		}
		numfds++;

		rte_spinlock_lock(&intr_lock);
		/* 
		 * 在rte_intr_callback_register会向intr_sources链表添加元素
		 */

		TAILQ_FOREACH(src, &intr_sources, next) {
			if (src->callbacks.tqh_first == NULL)
				continue; /* skip those with no callbacks */
			ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP | EPOLLHUP;
			ev.data.fd = src->intr_handle.fd;

			/**
			 * add all the uio device file descriptor
			 * into wait list.
			 */
			if (epoll_ctl(pfd, EPOLL_CTL_ADD, src->intr_handle.fd, &ev) < 0){
			}
			else
				numfds++;
		}
		rte_spinlock_unlock(&intr_lock);
		/* serve the interrupt */
		eal_intr_handle_interrupts(pfd, numfds);

		/**
		 * when we return, we need to rebuild the
		 * list of fds to monitor.
		 */
		close(pfd);
	}
}

/**
 * It handles all the interrupts.
 *
 * @param pfd
 *  epoll file descriptor.
 * @param totalfds
 *  The number of file descriptors added in epoll.
 *
 * @return
 *  void
 */
static void
eal_intr_handle_interrupts(int pfd, unsigned totalfds)
{
	struct epoll_event events[totalfds];
	int nfds = 0;

	for(;;) {
		nfds = epoll_wait(pfd, events, totalfds, EAL_INTR_EPOLL_WAIT_FOREVER);
		/* epoll_wait fail */
		if (nfds < 0) {
		}
		/* epoll_wait timeout, will never happens here */
		else if (nfds == 0)
			continue;
		/* epoll_wait has at least one fd ready to read */
		if (eal_intr_process_interrupts(events, nfds) < 0)
			return;
	}
}


static int
eal_intr_process_interrupts(struct epoll_event *events, int nfds)
{
	bool call = false;
	int n, bytes_read;
	struct rte_intr_source *src;
	struct rte_intr_callback *cb;
	union rte_intr_read_buffer buf;
	struct rte_intr_callback active_cb;

	for (n = 0; n < nfds; n++) {

		rte_spinlock_lock(&intr_lock);
		TAILQ_FOREACH(src, &intr_sources, next)
			if (src->intr_handle.fd == events[n].data.fd)
				break;
		if (src == NULL){
			rte_spinlock_unlock(&intr_lock);
			continue;
		}

		/* mark this interrupt source as active and release the lock. */
		src->active = 1;
		rte_spinlock_unlock(&intr_lock);

		/* set the length to be read dor different handle type */
		switch (src->intr_handle.type) {
		case RTE_INTR_HANDLE_UIO:
		case RTE_INTR_HANDLE_UIO_INTX:
			bytes_read = sizeof(buf.uio_intr_count);
			break;
		case RTE_INTR_HANDLE_ALARM:
			bytes_read = sizeof(buf.timerfd_num);
			break;
#ifdef VFIO_PRESENT
		case RTE_INTR_HANDLE_VFIO_MSIX:
		case RTE_INTR_HANDLE_VFIO_MSI:
		case RTE_INTR_HANDLE_VFIO_LEGACY:
			bytes_read = sizeof(buf.vfio_intr_count);
			break;
#endif
		case RTE_INTR_HANDLE_VDEV:
		case RTE_INTR_HANDLE_EXT:
			bytes_read = 0;
			call = true;
			break;

		default:
			bytes_read = 1;
			break;
		}

		if (bytes_read > 0) {
			/**
			 * read out to clear the ready-to-be-read flag
			 * for epoll_wait.
			 */
			bytes_read = read(events[n].data.fd, &buf, bytes_read);
			if (bytes_read < 0) {
				if (errno == EINTR || errno == EWOULDBLOCK)
					continue;

				RTE_LOG(ERR, EAL, "Error reading from file "
					"descriptor %d: %s\n",
					events[n].data.fd,
					strerror(errno));
			} else if (bytes_read == 0)
				RTE_LOG(ERR, EAL, "Read nothing from file "
					"descriptor %d\n", events[n].data.fd);
			else
				call = true;
		}

		/* grab a lock, again to call callbacks and update status. */
		rte_spinlock_lock(&intr_lock);

		if (call) {

			/* Finally, call all callbacks. */
			TAILQ_FOREACH(cb, &src->callbacks, next) {

				/* make a copy and unlock. */
				active_cb = *cb;
				rte_spinlock_unlock(&intr_lock);

				/* call the actual callback */
				active_cb.cb_fn(active_cb.cb_arg);

				/*get the lock back. */
				rte_spinlock_lock(&intr_lock);
			}
		}

		/* we done with that interrupt source, release it. */
		src->active = 0;
		rte_spinlock_unlock(&intr_lock);
	}

	return 0;
}

/* internal configuration (per-core) */
struct lcore_config lcore_config[RTE_MAX_LCORE];


/* main loop of threads */
__attribute__((noreturn)) void *
eal_thread_loop(__attribute__((unused)) void *arg)
{
	char c;
	int n, ret;
	unsigned lcore_id;
	pthread_t thread_id;
	int m2s, s2m;
	char cpuset[RTE_CPU_AFFINITY_STR_LEN];

	thread_id = pthread_self();

	/* retrieve our lcore_id from the configuration structure */
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (thread_id == lcore_config[lcore_id].thread_id)
			break;
	}


	m2s = lcore_config[lcore_id].pipe_master2slave[0];
	s2m = lcore_config[lcore_id].pipe_slave2master[1];

	/* set the lcore ID in per-lcore memory area */
	RTE_PER_LCORE(_lcore_id) = lcore_id;

	/* set CPU affinity */
	if (eal_thread_set_affinity() < 0)
		rte_panic("cannot set affinity\n");

	ret = eal_thread_dump_affinity(cpuset, RTE_CPU_AFFINITY_STR_LEN);

	RTE_LOG(DEBUG, EAL, "lcore %u is ready (tid=%x;cpuset=[%s%s])\n",
		lcore_id, (int)thread_id, cpuset, ret == 0 ? "" : "...");

	/* read on our pipe to get commands */
	while (1) {
		void *fct_arg;

		/* wait command */
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

	/* never reached */
	/* pthread_exit(NULL); */
	/* return NULL; */
}

/*
 * Check that every SLAVE lcores are in WAIT state, then call
 * rte_eal_remote_launch() for all of them. If call_master is true
 * (set to CALL_MASTER), also call the function on the master lcore.
 */
int
rte_eal_mp_remote_launch(int (*f)(void *), void *arg,
			 enum rte_rmt_call_master_t call_master)
{
	int lcore_id;
	int master = rte_get_master_lcore();

	/* check state of lcores */
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (lcore_config[lcore_id].state != WAIT)
			return -EBUSY;
	}

	/* send messages to cores */
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		rte_eal_remote_launch(f, arg, lcore_id);
	}

	if (call_master == CALL_MASTER) {
		lcore_config[master].ret = f(arg);
		lcore_config[master].state = FINISHED;
	}

	return 0;
}

static int
sync_func(__attribute__((unused)) void *arg)
{
	return 0;
}

/*
 * Send a message to a slave lcore identified by slave_id to call a
 * function f with argument arg. Once the execution is done, the
 * remote lcore switch in FINISHED state.
 */
int
rte_eal_remote_launch(int (*f)(void *), void *arg, unsigned slave_id)
{
	int n;
	char c = 0;
	int m2s = lcore_config[slave_id].pipe_master2slave[1];
	int s2m = lcore_config[slave_id].pipe_slave2master[0];

	if (lcore_config[slave_id].state != WAIT)
		return -EBUSY;

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

/*
 * Do a rte_eal_wait_lcore() for every lcore. The return values are
 * ignored.
 */
void
rte_eal_mp_wait_lcore(void)
{
	unsigned lcore_id;

	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		rte_eal_wait_lcore(lcore_id);
	}
}

/*
 * Wait until a lcore finished its job.
 */
int
rte_eal_wait_lcore(unsigned slave_id)
{
	if (lcore_config[slave_id].state == WAIT)
		return 0;

	while (lcore_config[slave_id].state != WAIT &&
	       lcore_config[slave_id].state != FINISHED)
		rte_pause();

	rte_rmb();

	/* we are in finished state, go to wait state */
	lcore_config[slave_id].state = WAIT;
	return lcore_config[slave_id].ret;
}

static uint32_t rte_service_count;
static struct rte_service_spec_impl *rte_services;
static struct core_state *lcore_states;
static uint32_t rte_service_library_initialized;


int32_t rte_service_init(void)
{
	if (rte_service_library_initialized) {
		printf("service library init() called, init flag %d\n",
			rte_service_library_initialized);
		return -EALREADY;
	}
	/* 从socket分配内存 */
	rte_services = rte_calloc("rte_services", RTE_SERVICE_NUM_MAX,
			sizeof(struct rte_service_spec_impl),
			RTE_CACHE_LINE_SIZE);
	

	lcore_states = rte_calloc("rte_service_core_states", RTE_MAX_LCORE,
			sizeof(struct core_state), RTE_CACHE_LINE_SIZE);
	

	int i;
	int count = 0;
	struct rte_config *cfg = rte_eal_get_configuration();
	for (i = 0; i < RTE_MAX_LCORE; i++) {
		if (lcore_config[i].core_role == ROLE_SERVICE) {
			if ((unsigned int)i == cfg->master_lcore)
				continue;
			rte_service_lcore_add(i);
			count++;
		}
	}

	rte_service_library_initialized = 1;
	return 0;
}

int32_t
rte_service_lcore_add(uint32_t lcore)
{
	set_lcore_state(lcore, ROLE_SERVICE);

	/* ensure that after adding a core the mask and state are defaults */
	lcore_states[lcore].service_mask = 0;
	lcore_states[lcore].runstate = RUNSTATE_STOPPED;

	rte_smp_wmb();

	return rte_eal_wait_lcore(lcore);
}

static void
set_lcore_state(uint32_t lcore, int32_t state)
{
	/* mark core state in hugepage backed config */
	struct rte_config *cfg = rte_eal_get_configuration();
	cfg->lcore_role[lcore] = state;

	/* mark state in process local lcore_config */
	lcore_config[lcore].core_role = state;

	/* update per-lcore optimized state tracking */
	lcore_states[lcore].is_service_core = (state == ROLE_SERVICE);
}

/*
 * Wait until a lcore finished its job.
 */
int
rte_eal_wait_lcore(unsigned slave_id)
{
	if (lcore_config[slave_id].state == WAIT)
		return 0;

	while (lcore_config[slave_id].state != WAIT &&
	       lcore_config[slave_id].state != FINISHED)
		rte_pause();

	rte_rmb();

	/* we are in finished state, go to wait state */
	lcore_config[slave_id].state = WAIT;
	return lcore_config[slave_id].ret;
}

/* Probe all devices of all buses */
int
rte_bus_probe(void)
{
	int ret;
	struct rte_bus *bus, *vbus = NULL;

	/* RTE_REGISTER_BUS宏修饰的函数会在main函数之前自动调用，然后将buf添加到rte_bus_list链表 */
	TAILQ_FOREACH(bus, &rte_bus_list, next) {
		if (!strcmp(bus->name, "vdev")) {
			vbus = bus;
			continue;
		}

		ret = bus->probe();
	}

	/* 这样看只有一个vbus，即只有一个bus名称为vdev */
	if (vbus) {
		ret = vbus->probe();
	}

	return 0;
}

int32_t
rte_service_start_with_defaults(void)
{
	/* create a default mapping from cores to services, then start the
	 * services to make them transparent to unaware applications.
	 */
	uint32_t i;
	int ret;
	uint32_t count = rte_service_get_count();

	int32_t lcore_iter = 0;
	uint32_t ids[RTE_MAX_LCORE] = {0};
	int32_t lcore_count = rte_service_lcore_list(ids, RTE_MAX_LCORE);

	/* lcore_count为0 */
	if (lcore_count == 0)
		return -ENOTSUP;


	for (i = 0; (int)i < lcore_count; i++)
		rte_service_lcore_start(ids[i]);

	for (i = 0; i < count; i++) {
		/* do 1:1 core mapping here, with each service getting
		 * assigned a single core by default. Adding multiple services
		 * should multiplex to a single core, or 1:1 if there are the
		 * same amount of services as service-cores
		 */
		ret = rte_service_map_lcore_set(i, ids[lcore_iter], 1);

		lcore_iter++;
		if (lcore_iter >= lcore_count)
			lcore_iter = 0;

		ret = rte_service_runstate_set(i, 1);

	}

	return 0;
}


int32_t
rte_service_lcore_list(uint32_t array[], uint32_t n)
{
	uint32_t count = rte_service_lcore_count();
	uint32_t i;
	uint32_t idx = 0;
	for (i = 0; i < RTE_MAX_LCORE; i++) {
		struct core_state *cs = &lcore_states[i];
		if (cs->is_service_core) {
			array[idx] = i;
			idx++;
		}
	}

	return count;
}


int32_t
rte_service_lcore_start(uint32_t lcore)
{
	struct core_state *cs = &lcore_states[lcore];
	if (!cs->is_service_core)
		return -EINVAL;

	if (cs->runstate == RUNSTATE_RUNNING)
		return -EALREADY;

	/* set core to run state first, and then launch otherwise it will
	 * return immediately as runstate keeps it in the service poll loop
	 */
	lcore_states[lcore].runstate = RUNSTATE_RUNNING;

	int ret = rte_eal_remote_launch(rte_service_runner_func, 0, lcore);
	/* returns -EBUSY if the core is already launched, 0 on success */
	return ret;
}

static int32_t
rte_service_runner_func(void *arg)
{
	RTE_SET_USED(arg);
	uint32_t i;
	const int lcore = rte_lcore_id();
	struct core_state *cs = &lcore_states[lcore];

	while (lcore_states[lcore].runstate == RUNSTATE_RUNNING) {
		const uint64_t service_mask = cs->service_mask;

		for (i = 0; i < RTE_SERVICE_NUM_MAX; i++) {
			/* return value ignored as no change to code flow */
			service_run(i, cs, service_mask);
		}

		rte_smp_rmb();
	}

	lcore_config[lcore].state = WAIT;

	return 0;
}

static inline int32_t
service_run(uint32_t i, struct core_state *cs, uint64_t service_mask)
{
	if (!service_valid(i))
		return -EINVAL;
	struct rte_service_spec_impl *s = &rte_services[i];
	if (s->comp_runstate != RUNSTATE_RUNNING ||
			s->app_runstate != RUNSTATE_RUNNING ||
			!(service_mask & (UINT64_C(1) << i)))
		return -ENOEXEC;

	/* check do we need cmpset, if MT safe or <= 1 core
	 * mapped, atomic ops are not required.
	 */
	const int use_atomics = (service_mt_safe(s) == 0) &&
				(rte_atomic32_read(&s->num_mapped_cores) > 1);
	if (use_atomics) {
		if (!rte_atomic32_cmpset((uint32_t *)&s->execute_lock, 0, 1))
			return -EBUSY;

		rte_service_runner_do_callback(s, cs, i);
		rte_atomic32_clear(&s->execute_lock);
	} else
		rte_service_runner_do_callback(s, cs, i);

	return 0;
}

static inline void
rte_service_runner_do_callback(struct rte_service_spec_impl *s,
			       struct core_state *cs, uint32_t service_idx)
{
	void *userdata = s->spec.callback_userdata;

	if (service_stats_enabled(s)) {
		uint64_t start = rte_rdtsc();
		s->spec.callback(userdata);
		uint64_t end = rte_rdtsc();
		s->cycles_spent += end - start;
		cs->calls_per_service[service_idx]++;
		s->calls++;
	} else
		s->spec.callback(userdata);
}

inline static void
rte_eal_mcfg_complete(void)
{
	/* ALL shared mem_config related INIT DONE */
	if (rte_config.process_type == RTE_PROC_PRIMARY)
		rte_config.mem_config->magic = RTE_MAGIC;
}

int
spdk_mem_map_init(void)
{
	struct rte_mem_config *mcfg;
	size_t seg_idx;

	g_mem_reg_map = spdk_mem_map_alloc(0, NULL, NULL);
	

	/*
	 * Walk all DPDK memory segments and register them
	 * with the master memory map
	 */
	mcfg = rte_eal_get_configuration()->mem_config;

	for (seg_idx = 0; seg_idx < RTE_MAX_MEMSEG; seg_idx++) {
		struct rte_memseg *seg = &mcfg->memseg[seg_idx];

		if (seg->addr == NULL) {
			break;
		}

		spdk_mem_register(seg->addr, seg->len);
	}
	return 0;
}

struct spdk_mem_map *
spdk_mem_map_alloc(uint64_t default_translation, spdk_mem_map_notify_cb notify_cb, void *cb_ctx)
{
	struct spdk_mem_map *map;

	map = calloc(1, sizeof(*map));

	if (pthread_mutex_init(&map->mutex, NULL)) {
	}

	map->default_translation = default_translation;
	map->notify_cb = notify_cb;
	map->cb_ctx = cb_ctx;

	pthread_mutex_lock(&g_spdk_mem_map_mutex);

	/* (notify_cb)为NULL */
	if (notify_cb) {
		spdk_mem_map_notify_walk(map, SPDK_MEM_MAP_NOTIFY_REGISTER);
		TAILQ_INSERT_TAIL(&g_spdk_mem_maps, map, tailq);
	}

	pthread_mutex_unlock(&g_spdk_mem_map_mutex);

	return map;
}

int
spdk_vtophys_init(void)
{
#if SPDK_VFIO_ENABLED
	spdk_vtophys_iommu_init();
#endif

	g_vtophys_map = spdk_mem_map_alloc(SPDK_VTOPHYS_ERROR, spdk_vtophys_notify, NULL);
	return 0;
}

static struct spdk_reactor *g_reactors;


int
spdk_reactors_init(unsigned int max_delay_us)
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
			socket_count++;
		}
	}
	if (socket_count == 0) {
		SPDK_ERRLOG("No sockets occupied (internal error)\n");
		return -1;
	}

	for (i = 0; i < SPDK_MAX_SOCKET; i++) {
		if ((1ULL << i) & socket_mask) {
			snprintf(mempool_name, sizeof(mempool_name), "evtpool%d_%d", i, getpid());
			/* 为每一个socket创建一个event 的mempoll */
			g_spdk_event_mempool[i] = spdk_mempool_create(mempool_name,
						  (262144 / socket_count),
						  sizeof(struct spdk_event),
						  SPDK_MEMPOOL_DEFAULT_CACHE_SIZE, i);

			if (g_spdk_event_mempool[i] == NULL) {
				SPDK_NOTICELOG("Event_mempool creation failed on preferred socket %d.\n", i);

				/*
				 * Instead of failing the operation directly, try to create
				 * the mempool on any available sockets in the case that
				 * memory is not evenly installed on all sockets. If still
				 * fails, free all allocated memory and exits.
				 */
				g_spdk_event_mempool[i] = spdk_mempool_create(
								  mempool_name,
								  (262144 / socket_count),
								  sizeof(struct spdk_event),
								  SPDK_MEMPOOL_DEFAULT_CACHE_SIZE,
								  SPDK_ENV_SOCKET_ID_ANY);

				if (g_spdk_event_mempool[i] == NULL) {
					for (j = i - 1; j < i; j--) {
						if (g_spdk_event_mempool[j] != NULL) {
							spdk_mempool_free(g_spdk_event_mempool[j]);
						}
					}
					SPDK_ERRLOG("spdk_event_mempool creation failed\n");
					return -1;
				}
			}
		} else {
			g_spdk_event_mempool[i] = NULL;
		}
	}

	/* struct spdk_reactor must be aligned on 64 byte boundary */
	last_core = spdk_env_get_last_core();
	rc = posix_memalign((void **)&g_reactors, 64,
			    (last_core + 1) * sizeof(struct spdk_reactor));
	if (rc != 0) {
		SPDK_ERRLOG("Could not allocate array size=%u for g_reactors\n",
			    last_core + 1);
		for (i = 0; i < SPDK_MAX_SOCKET; i++) {
			if (g_spdk_event_mempool[i] != NULL) {
				spdk_mempool_free(g_spdk_event_mempool[i]);
			}
		}
		return -1;
	}

	memset(g_reactors, 0, (last_core + 1) * sizeof(struct spdk_reactor));

	SPDK_ENV_FOREACH_CORE(i) {
		reactor = spdk_reactor_get(i);
		spdk_reactor_construct(reactor, i, max_delay_us);
	}

	g_reactor_state = SPDK_REACTOR_STATE_INITIALIZED;

	return 0;
}

enum spdk_ring_type {
	SPDK_RING_TYPE_SP_SC,		/* Single-producer, single-consumer */
	SPDK_RING_TYPE_MP_SC,		/* Multi-producer, single-consumer */
};


static void
spdk_reactor_construct(struct spdk_reactor *reactor, uint32_t lcore, uint64_t max_delay_us)
{
	reactor->lcore = lcore;
	reactor->socket_id = spdk_env_get_socket_id(lcore);
	assert(reactor->socket_id < SPDK_MAX_SOCKET);
	reactor->max_delay_us = max_delay_us;

	TAILQ_INIT(&reactor->active_pollers);
	TAILQ_INIT(&reactor->timer_pollers);

	/* 创建event	 ring */
	reactor->events = spdk_ring_create(SPDK_RING_TYPE_MP_SC, 65536, reactor->socket_id);
	if (!reactor->events) {
		SPDK_NOTICELOG("Ring creation failed on preferred socket %d. Try other sockets.\n",
			       reactor->socket_id);

		reactor->events = spdk_ring_create(SPDK_RING_TYPE_MP_SC, 65536,
						   SPDK_ENV_SOCKET_ID_ANY);
	}
	assert(reactor->events != NULL);

	reactor->event_mempool = g_spdk_event_mempool[reactor->socket_id];
}

struct spdk_ring *
spdk_ring_create(enum spdk_ring_type type, size_t count, int socket_id)
{
	char ring_name[64];
	static uint32_t ring_num = 0;
	unsigned flags = 0;

	switch (type) {
	case SPDK_RING_TYPE_SP_SC:
		flags = RING_F_SP_ENQ | RING_F_SC_DEQ;
		break;
	case SPDK_RING_TYPE_MP_SC:
		flags = RING_F_SC_DEQ;
		break;
	default:
		return NULL;
	}

	snprintf(ring_name, sizeof(ring_name), "ring_%u_%d",
		 __sync_fetch_and_add(&ring_num, 1), getpid());

	return (struct spdk_ring *)rte_ring_create(ring_name, count, socket_id, flags);
}


/* create the ring */
struct rte_ring *
rte_ring_create(const char *name, unsigned count, int socket_id,
		unsigned flags)
{
	char mz_name[RTE_MEMZONE_NAMESIZE];
	struct rte_ring *r;
	struct rte_tailq_entry *te;
	const struct rte_memzone *mz;
	ssize_t ring_size;
	int mz_flags = 0;
	struct rte_ring_list* ring_list = NULL;
	const unsigned int requested_count = count;
	int ret;

	ring_list = RTE_TAILQ_CAST(rte_ring_tailq.head, rte_ring_list);

	/* for an exact size ring, round up from count to a power of two */
	if (flags & RING_F_EXACT_SZ)
		count = rte_align32pow2(count + 1);

	ring_size = rte_ring_get_memsize(count);


	ret = snprintf(mz_name, sizeof(mz_name), "%s%s",
		RTE_RING_MZ_PREFIX, name);
	

	te = rte_zmalloc("RING_TAILQ_ENTRY", sizeof(*te), 0);
	
	rte_rwlock_write_lock(RTE_EAL_TAILQ_RWLOCK);

	/* reserve a memory zone for this ring. If we can't get rte_config or
	 * we are secondary process, the memzone_reserve function will set
	 * rte_errno for us appropriately - hence no check in this this function */
	mz = rte_memzone_reserve_aligned(mz_name, ring_size, socket_id,
					 mz_flags, __alignof__(*r));
	if (mz != NULL) {
		r = mz->addr;
		/* no need to check return value here, we already checked the
		 * arguments above */
		rte_ring_init(r, name, requested_count, flags);

		te->data = (void *) r;
		r->memzone = mz;

		TAILQ_INSERT_TAIL(ring_list, te, next);
	} else {
		r = NULL;
		RTE_LOG(ERR, RING, "Cannot reserve memory\n");
		rte_free(te);
	}
	rte_rwlock_write_unlock(RTE_EAL_TAILQ_RWLOCK);

	return r;
}

static int
spdk_app_setup_trace(struct spdk_app_opts *opts)
{
	char		shm_name[64];
	uint64_t	tpoint_group_mask;
	char		*end;

	if (opts->shm_id >= 0) {
		snprintf(shm_name, sizeof(shm_name), "/%s_trace.%d", opts->name, opts->shm_id);
	} else {
		snprintf(shm_name, sizeof(shm_name), "/%s_trace.pid%d", opts->name, (int)getpid());
	}

	if (spdk_trace_init(shm_name) != 0) {
		return -1;
	}

	if (opts->tpoint_group_mask != NULL) {
		errno = 0;
		tpoint_group_mask = strtoull(opts->tpoint_group_mask, &end, 16);
		if (*end != '\0' || errno) {
			SPDK_ERRLOG("invalid tpoint mask %s\n", opts->tpoint_group_mask);
		} else {
			SPDK_NOTICELOG("Tracepoint Group Mask %s specified.\n", opts->tpoint_group_mask);
			SPDK_NOTICELOG("Use 'spdk_trace -s %s %s %d' to capture a snapshot of events at runtime.\n",
				       opts->name,
				       opts->shm_id >= 0 ? "-i" : "-p",
				       opts->shm_id >= 0 ? opts->shm_id : getpid());
			spdk_trace_set_tpoint_group_mask(tpoint_group_mask);
		}
	}

	return 0;
}

int
spdk_trace_init(const char *shm_name)
{
	int i = 0;

	snprintf(g_shm_name, sizeof(g_shm_name), "%s", shm_name);

	g_trace_fd = shm_open(shm_name, O_RDWR | O_CREAT, 0600);

	if (ftruncate(g_trace_fd, sizeof(*g_trace_histories)) != 0) {
		
	}

	g_trace_histories = mmap(NULL, sizeof(*g_trace_histories), PROT_READ | PROT_WRITE,
				 MAP_SHARED, g_trace_fd, 0);
	

	memset(g_trace_histories, 0, sizeof(*g_trace_histories));

	g_trace_flags = &g_trace_histories->flags;

	g_trace_flags->tsc_rate = spdk_get_ticks_hz();

	for (i = 0; i < SPDK_TRACE_MAX_LCORE; i++) {
		g_trace_histories->per_lcore_history[i].lcore = i;
	}

	spdk_trace_flags_init();

	return 0;
}

void
spdk_trace_flags_init(void)
{
	struct spdk_trace_register_fn *reg_fn;

	reg_fn = g_reg_fn_head;
	while (reg_fn) {
		reg_fn->reg_fn();
		reg_fn = reg_fn->next;
	}
}

struct spdk_event *
spdk_event_allocate(uint32_t lcore, spdk_event_fn fn, void *arg1, void *arg2)
{
	struct spdk_event *event = NULL;
	struct spdk_reactor *reactor = spdk_reactor_get(lcore);

	if (!reactor) {
		assert(false);
		return NULL;
	}

	event = spdk_mempool_get(reactor->event_mempool);
	if (event == NULL) {
		assert(false);
		return NULL;
	}

	event->lcore = lcore;
	event->fn = fn;
	event->arg1 = arg1;
	event->arg2 = arg2;

	return event;
}

void
spdk_subsystem_init(struct spdk_event *app_start_event)
{
	struct spdk_event *verify_event;

	g_app_start_event = app_start_event;

	verify_event = spdk_event_allocate(spdk_env_get_current_core(), spdk_subsystem_verify, NULL, NULL);
	/* 给自己发送一个event，在后面的reactor线程里面会处理行 */
	spdk_event_call(verify_event);
}

void
spdk_event_call(struct spdk_event *event)
{
	int rc;
	struct spdk_reactor *reactor;

	reactor = spdk_reactor_get(event->lcore);

	assert(reactor->events != NULL);
	rc = spdk_ring_enqueue(reactor->events, (void **)&event, 1);
	if (rc != 1) {
		assert(false);
	}
}

void
spdk_reactors_start(void)
{
	struct spdk_reactor *reactor;
	uint32_t i, current_core;
	int rc;

	g_reactor_state = SPDK_REACTOR_STATE_RUNNING;
	g_spdk_app_core_mask = spdk_cpuset_alloc();

	current_core = spdk_env_get_current_core();
	SPDK_ENV_FOREACH_CORE(i) {
		if (i != current_core) {
			reactor = spdk_reactor_get(i);
			rc = spdk_env_thread_launch_pinned(reactor->lcore, _spdk_reactor_run, reactor);
			if (rc < 0) {
				SPDK_ERRLOG("Unable to start reactor thread on core %u\n", reactor->lcore);
				assert(false);
				return;
			}
		}
		spdk_cpuset_set_cpu(g_spdk_app_core_mask, i, true);
	}

	/* Start the master reactor */
	reactor = spdk_reactor_get(current_core);
	_spdk_reactor_run(reactor);

	spdk_env_thread_wait_all();

	g_reactor_state = SPDK_REACTOR_STATE_SHUTDOWN;
	spdk_cpuset_free(g_spdk_app_core_mask);
	g_spdk_app_core_mask = NULL;
}

int
spdk_env_thread_launch_pinned(uint32_t core, thread_start_fn fn, void *arg)
{
	int rc;

	/* eal_thread_loop 线程会执行fn，每一个slave cpu都有eal_thread_loop线程*/
	rc = rte_eal_remote_launch(fn, arg, core);

	return rc;
}

static int
_spdk_reactor_run(void *arg)
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
	if (spdk_allocate_thread(_spdk_reactor_send_msg,
				 _spdk_reactor_start_poller,
				 _spdk_reactor_stop_poller,
				 reactor, thread_name) == NULL) {
		return -1;
	}
	SPDK_NOTICELOG("Reactor started on core %u on socket %u\n", reactor->lcore,
		       reactor->socket_id);

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

struct spdk_thread *
spdk_allocate_thread(spdk_thread_pass_msg msg_fn,
		     spdk_start_poller start_poller_fn,
		     spdk_stop_poller stop_poller_fn,
		     void *thread_ctx, const char *name)
{
	struct spdk_thread *thread;
	pthread_mutex_lock(&g_devlist_mutex);

	thread = _get_thread();
	/* 找不到才正常  */
	if (thread) {
		SPDK_ERRLOG("Double allocated SPDK thread\n");
		pthread_mutex_unlock(&g_devlist_mutex);
		return NULL;
	}

	thread = calloc(1, sizeof(*thread));

	thread->thread_id = pthread_self();
	thread->msg_fn = msg_fn;
	thread->start_poller_fn = start_poller_fn;
	thread->stop_poller_fn = stop_poller_fn;
	thread->thread_ctx = thread_ctx;
	TAILQ_INIT(&thread->io_channels);
	/* 设置线程的名称 */
	TAILQ_INSERT_TAIL(&g_threads, thread, tailq);
	if (name) {
		_set_thread_name(name);
		thread->name = strdup(name);
	}

	pthread_mutex_unlock(&g_devlist_mutex);

	return thread;
}

static void
_spdk_reactor_context_switch_monitor_start(void *arg1, void *arg2)
{
	struct spdk_reactor *reactor = arg1;

	if (reactor->rusage_poller == NULL) {
		getrusage(RUSAGE_THREAD, &reactor->rusage);
		reactor->rusage_poller = spdk_poller_register(get_rusage, reactor, 1000000);
	}
}

struct spdk_poller *
spdk_poller_register(spdk_poller_fn fn,
		     void *arg,
		     uint64_t period_microseconds)
{
	struct spdk_thread *thread;
	struct spdk_poller *poller;

	thread = spdk_get_thread();

	if (!thread->start_poller_fn || !thread->stop_poller_fn) {
	}
	/* 调用 _spdk_reactor_start_poller */
	poller = thread->start_poller_fn(thread->thread_ctx, fn, arg, period_microseconds);
	return poller;
}


struct spdk_thread *
spdk_get_thread(void)
{
	struct spdk_thread *thread;

	pthread_mutex_lock(&g_devlist_mutex);

	thread = _get_thread();
	if (!thread) {
		SPDK_ERRLOG("No thread allocated\n");
	}

	pthread_mutex_unlock(&g_devlist_mutex);

	return thread;
}

static struct spdk_thread *
_get_thread(void)
{
	pthread_t thread_id;
	struct spdk_thread *thread;

	thread_id = pthread_self();

	thread = NULL;
	TAILQ_FOREACH(thread, &g_threads, tailq) {
		if (thread->thread_id == thread_id) {
			return thread;
		}
	}

	return NULL;
}

static inline uint32_t
_spdk_event_queue_run_batch(struct spdk_reactor *reactor)
{
	unsigned count, i;
	void *events[SPDK_EVENT_BATCH_SIZE];

#ifdef DEBUG
	/*
	 * spdk_ring_dequeue() fills events and returns how many entries it wrote,
	 * so we will never actually read uninitialized data from events, but just to be sure
	 * (and to silence a static analyzer false positive), initialize the array to NULL pointers.
	 */
	memset(events, 0, sizeof(events));
#endif
	/* 从ring中取出event */
	count = spdk_ring_dequeue(reactor->events, events, SPDK_EVENT_BATCH_SIZE);
	if (count == 0) {
		return 0;
	}

	for (i = 0; i < count; i++) {
		struct spdk_event *event = events[i];
		assert(event != NULL);
		/* 执行event中的fn函数 */
		event->fn(event->arg1, event->arg2);
	}
	/* 将event的内存返还给memmool */
	spdk_mempool_put_bulk(reactor->event_mempool, events, count);

	return count;
}

static void
_spdk_reactor_send_msg(spdk_thread_fn fn, void *ctx, void *thread_ctx)
{
	struct spdk_event *event;
	struct spdk_reactor *reactor;

	reactor = thread_ctx;

	event = spdk_event_allocate(reactor->lcore, _spdk_reactor_msg_passed, fn, ctx);

	spdk_event_call(event);
}

static struct spdk_poller *
_spdk_reactor_start_poller(void *thread_ctx,
			   spdk_poller_fn fn,
			   void *arg,
			   uint64_t period_microseconds)
{
	struct spdk_poller *poller;
	struct spdk_reactor *reactor;
	uint64_t quotient, remainder, ticks;

	reactor = thread_ctx;

	poller = calloc(1, sizeof(*poller));

	poller->lcore = reactor->lcore;
	poller->state = SPDK_POLLER_STATE_WAITING;
	poller->fn = fn;
	poller->arg = arg;

	if (period_microseconds) {
		quotient = period_microseconds / SPDK_SEC_TO_USEC;
		remainder = period_microseconds % SPDK_SEC_TO_USEC;
		ticks = spdk_get_ticks_hz();

		poller->period_ticks = ticks * quotient + (ticks * remainder) / SPDK_SEC_TO_USEC;
	} else {
		poller->period_ticks = 0;
	}

	if (poller->period_ticks) {
		/* 如果有period，则添加到timer_pollers中 */
		_spdk_poller_insert_timer(reactor, poller, spdk_get_ticks());
	} else {
		/* 直接添加到active_pollers中 */
		TAILQ_INSERT_TAIL(&reactor->active_pollers, poller, tailq);
	}

	return poller;
}

static void
_spdk_poller_insert_timer(struct spdk_reactor *reactor, struct spdk_poller *poller, uint64_t now)
{
	struct spdk_poller *iter;
	uint64_t next_run_tick;

	next_run_tick = now + poller->period_ticks;
	poller->next_run_tick = next_run_tick;

	/*
	 * Insert poller in the reactor's timer_pollers list in sorted order by next scheduled
	 * run time.
	 */
	/* 安装到期时间从小到大排序 */
	TAILQ_FOREACH_REVERSE(iter, &reactor->timer_pollers, timer_pollers_head, tailq) {
		if (iter->next_run_tick <= next_run_tick) {
			TAILQ_INSERT_AFTER(&reactor->timer_pollers, iter, poller, tailq);
			return;
		}
	}

	/* No earlier pollers were found, so this poller must be the new head */
	/* 没有比该poller到期时间更小的，则把该poller添加到头 */
	TAILQ_INSERT_HEAD(&reactor->timer_pollers, poller, tailq);
}

static void
spdk_subsystem_verify(void *arg1, void *arg2)
{
	struct spdk_subsystem_depend *dep;

	/* Verify that all dependency name and depends_on subsystems are registered */
	TAILQ_FOREACH(dep, &g_subsystems_deps, tailq) {
		if (!spdk_subsystem_find(&g_subsystems, dep->name)) {
			SPDK_ERRLOG("subsystem %s is missing\n", dep->name);
			spdk_app_stop(-1);
			return;
		}
		if (!spdk_subsystem_find(&g_subsystems, dep->depends_on)) {
			SPDK_ERRLOG("subsystem %s dependency %s is missing\n",
				    dep->name, dep->depends_on);
			spdk_app_stop(-1);
			return;
		}
	}

	subsystem_sort();

	spdk_subsystem_init_next(0);
}

