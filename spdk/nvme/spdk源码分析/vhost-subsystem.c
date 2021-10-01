struct spdk_subsystem_depend_list g_subsystems_deps = TAILQ_HEAD_INITIALIZER(g_subsystems_deps);
static struct spdk_subsystem *g_next_subsystem;


static struct spdk_subsystem g_spdk_subsystem_vhost = {
	.name = "vhost",
	.init = spdk_vhost_subsystem_init,
	.fini = spdk_vhost_subsystem_fini,
	.config = NULL,
	.write_config_json = spdk_vhost_config_json,
};

SPDK_SUBSYSTEM_REGISTER(g_spdk_subsystem_vhost);
SPDK_SUBSYSTEM_DEPEND(vhost, scsi);

spdk_vhost_subsystem_init(void)
{
	int rc = 0;

	rc = spdk_vhost_init();

	spdk_subsystem_init_next(rc);
}

static uint32_t *g_num_ctrlrs;

int
spdk_vhost_init(void)
{
	uint32_t last_core;
	int ret;

	last_core = spdk_env_get_last_core();
	g_num_ctrlrs = calloc(last_core + 1, sizeof(uint32_t));

	ret = spdk_vhost_scsi_controller_construct();

	ret = spdk_vhost_blk_controller_construct();

	ret = spdk_vhost_nvme_controller_construct();

	return 0;
}

int
spdk_vhost_blk_controller_construct(void)
{
	struct spdk_conf_section *sp;
	unsigned ctrlr_num;
	char *bdev_name;
	char *cpumask;
	char *name;
	bool readonly;

	/* 解析的spdk配置文件/usr/local/etc/spdk/vhost.conf内容如下 */
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

	for (sp = spdk_conf_first_section(NULL); sp != NULL; sp = spdk_conf_next_section(sp)) {
		if (!spdk_conf_section_match_prefix(sp, "VhostBlk")) {
			continue;
		}

		/* 根据VhostBlk%u解析出ctrlr_num的值 */
		if (sscanf(spdk_conf_section_get_name(sp), "VhostBlk%u", &ctrlr_num) != 1) {
		}
		/* 获得Name 后面的值 */
		name = spdk_conf_section_get_val(sp, "Name");
	
		cpumask = spdk_conf_section_get_val(sp, "Cpumask");
		readonly = spdk_conf_section_get_boolval(sp, "ReadOnly", false);

		bdev_name = spdk_conf_section_get_val(sp, "Dev");

		/* 会对每一个nvem设置执行该函数 */
		if (spdk_vhost_blk_construct(name, cpumask, bdev_name, readonly) < 0) {
			continue;
		}
	}

	return 0;
}

static const struct spdk_vhost_dev_backend vhost_blk_device_backend = {
	.virtio_features = SPDK_VHOST_FEATURES |
	(1ULL << VIRTIO_BLK_F_SIZE_MAX) | (1ULL << VIRTIO_BLK_F_SEG_MAX) |
	(1ULL << VIRTIO_BLK_F_GEOMETRY) | (1ULL << VIRTIO_BLK_F_RO) |
	(1ULL << VIRTIO_BLK_F_BLK_SIZE) | (1ULL << VIRTIO_BLK_F_TOPOLOGY) |
	(1ULL << VIRTIO_BLK_F_BARRIER)  | (1ULL << VIRTIO_BLK_F_SCSI) |
	(1ULL << VIRTIO_BLK_F_FLUSH)    | (1ULL << VIRTIO_BLK_F_CONFIG_WCE) |
	(1ULL << VIRTIO_BLK_F_MQ),
	.disabled_features = SPDK_VHOST_DISABLED_FEATURES | (1ULL << VIRTIO_BLK_F_GEOMETRY) |
	(1ULL << VIRTIO_BLK_F_RO) | (1ULL << VIRTIO_BLK_F_FLUSH) | (1ULL << VIRTIO_BLK_F_CONFIG_WCE) |
	(1ULL << VIRTIO_BLK_F_BARRIER) | (1ULL << VIRTIO_BLK_F_SCSI),
	.start_device =  spdk_vhost_blk_start,
	.resume_device =  spdk_vhost_blk_resume,
	.stop_device = spdk_vhost_blk_stop,
	.drain_device = spdk_vhost_blk_drain,
	.vhost_get_config = spdk_vhost_blk_get_config,
	.dump_info_json = spdk_vhost_blk_dump_info_json,
	.write_config_json = spdk_vhost_blk_write_config_json,
	.remove_device = spdk_vhost_blk_destroy,
};


struct spdk_bdev {
	/** User context passed in by the backend */
	void *ctxt;

	/** Unique name for this block device. */
	char *name;

	/** Unique aliases for this block device. */
	TAILQ_HEAD(spdk_bdev_aliases_list, spdk_bdev_alias) aliases;

	/** Unique product name for this kind of block device. */
	char *product_name;

	/** Size in bytes of a logical block for the backend */
	uint32_t blocklen;

	/** Number of blocks */
	uint64_t blockcnt;

	/** Number of active channels on this bdev except the QoS bdev channel */
	uint32_t channel_count;

	/** Quality of service parameters */
	struct spdk_bdev_qos {
		/** True if QoS is enabled */
		bool enabled;

		/** True if the state of the QoS is being modified */
		bool mod_in_progress;

		/** Rate limit, in I/O per second */
		uint64_t rate_limit;

		/** The channel that all I/O are funneled through */
		struct spdk_bdev_channel *ch;

		/** The thread on which the poller is running. */
		struct spdk_thread *thread;

		/** Queue of I/O waiting to be issued. */
		bdev_io_tailq_t queued;

		/** Maximum allowed IOs to be issued in one timeslice (e.g., 1ms) and
		 *  only valid for the master channel which manages the outstanding IOs. */
		uint64_t max_ios_per_timeslice;

		/** Submitted IO in one timeslice (e.g., 1ms) */
		uint64_t io_submitted_this_timeslice;

		/** Polller that processes queued I/O commands each time slice. */
		struct spdk_poller *poller;
	} qos;

	/** write cache enabled, not used at the moment */
	int write_cache;

	/**
	 * This is used to make sure buffers are sector aligned.
	 * This causes double buffering on writes.
	 */
	int need_aligned_buffer;

	/**
	 * Optimal I/O boundary in blocks, or 0 for no value reported.
	 */
	uint32_t optimal_io_boundary;

	/**
	 * UUID for this bdev.
	 *
	 * Fill with zeroes if no uuid is available.
	 */
	struct spdk_uuid uuid;

	/**
	 * Pointer to the bdev module that registered this bdev.
	 */
	struct spdk_bdev_module *module;

	/** function table for all LUN ops */
	const struct spdk_bdev_fn_table *fn_table;

	/** Mutex protecting claimed */
	pthread_mutex_t mutex;

	/** The bdev status */
	enum spdk_bdev_status status;

	/** The array of block devices that this block device is built on top of (if any). */
	struct spdk_bdev **base_bdevs;
	size_t base_bdevs_cnt;


	/** The array of virtual block devices built on top of this block device. */
	struct spdk_bdev **vbdevs;
	size_t vbdevs_cnt;

	/**
	 * Pointer to the module that has claimed this bdev for purposes of creating virtual
	 *  bdevs on top of it.  Set to NULL if the bdev has not been claimed.
	 */
	struct spdk_bdev_module *claim_module;

	/** Callback function that will be called after bdev destruct is completed. */
	spdk_bdev_unregister_cb	unregister_cb;

	/** Unregister call context */
	void *unregister_ctx;

	/** List of open descriptors for this block device. */
	TAILQ_HEAD(, spdk_bdev_desc) open_descs;

	TAILQ_ENTRY(spdk_bdev) link;

	/** points to a reset bdev_io if one is in progress. */
	struct spdk_bdev_io *reset_in_progress;

	struct __bdev_internal_fields {
		/** poller for tracking the queue_depth of a device, NULL if not tracking */
		struct spdk_poller *qd_poller;

		/** period at which we poll for queue depth information */
		uint64_t period;

		/** used to aggregate queue depth while iterating across the bdev's open channels */
		uint64_t temporary_queue_depth;

		/** queue depth as calculated the last time the telemetry poller checked. */
		uint64_t measured_queue_depth;

		/** most recent value of ticks spent performing I/O. Used to calculate the weighted time doing I/O */
		uint64_t io_time;

		/** weighted time performing I/O. Equal to measured_queue_depth * period */
		uint64_t weighted_io_time;

		/** accumulated I/O statistics for previously deleted channels of this bdev */
		struct spdk_bdev_io_stat stat;
	} internal;
};
struct spdk_vhost_blk_dev {
	struct spdk_vhost_dev vdev;
	struct spdk_bdev *bdev;
	struct spdk_bdev_desc *bdev_desc;
	struct spdk_io_channel *bdev_io_channel;
	struct spdk_poller *requestq_poller;
	bool readonly;
};

struct spdk_bdev_desc {
	struct spdk_bdev		*bdev;
	spdk_bdev_remove_cb_t		remove_cb;
	void				*remove_ctx;
	bool				write;
	TAILQ_ENTRY(spdk_bdev_desc)	link;
};

struct spdk_io_channel {
	struct spdk_thread		*thread;
	struct io_device		*dev;
	uint32_t			ref;
	TAILQ_ENTRY(spdk_io_channel)	tailq;
	spdk_io_channel_destroy_cb	destroy_cb;

	/*
	 * Modules will allocate extra memory off the end of this structure
	 *  to store references to hardware-specific references (i.e. NVMe queue
	 *  pairs, or references to child device spdk_io_channels (i.e.
	 *  virtual bdevs).
	 */
};

int
spdk_vhost_blk_construct(const char *name, const char *cpumask, const char *dev_name, bool readonly)
{
	struct spdk_vhost_blk_dev *bvdev = NULL;
	struct spdk_bdev *bdev;
	int ret = 0;

	spdk_vhost_lock();

	/* 从g_bdev_mgr.bdevs查找dev_name对应的
	 * SPDK_BDEV_MODULE_REGISTER会注册module，nvme_if会根据/usr/local/etc/spdk/vhost.conf创建bdev
	 */
	bdev = spdk_bdev_get_by_name(dev_name);

	/* 分配bvdev */
	bvdev = spdk_dma_zmalloc(sizeof(*bvdev), SPDK_CACHE_LINE_SIZE, NULL);

	ret = spdk_bdev_open(bdev, true, bdev_remove_cb, bvdev, &bvdev->bdev_desc);

	bvdev->bdev = bdev;
	bvdev->readonly = readonly;
	bvdev->vdev.io_dropped = false;
	ret = spdk_vhost_dev_register(&bvdev->vdev, name, cpumask, &vhost_blk_device_backend);

	if (readonly && rte_vhost_driver_enable_features(bvdev->vdev.path, (1ULL << VIRTIO_BLK_F_RO))) {
	}

	SPDK_INFOLOG(SPDK_LOG_VHOST, "Controller %s: using bdev '%s'\n", name, dev_name);
out:
	spdk_vhost_unlock();
	return ret;
}

int
spdk_bdev_open(struct spdk_bdev *bdev, bool write, spdk_bdev_remove_cb_t remove_cb,
	       void *remove_ctx, struct spdk_bdev_desc **_desc)
{
	struct spdk_bdev_desc *desc;

	/* 分配desc */
	desc = calloc(1, sizeof(*desc));

	pthread_mutex_lock(&bdev->mutex);

	if (write && bdev->claim_module) {
		SPDK_INFOLOG(SPDK_LOG_BDEV, "Could not open %s - already claimed\n", bdev->name);
		free(desc);
		pthread_mutex_unlock(&bdev->mutex);
		return -EPERM;
	}

	/* 添加desc到bdev->open_descs */
	TAILQ_INSERT_TAIL(&bdev->open_descs, desc, link);

	desc->bdev = bdev;
	desc->remove_cb = remove_cb;
	desc->remove_ctx = remove_ctx;
	desc->write = write;
	*_desc = desc;

	pthread_mutex_unlock(&bdev->mutex);

	return 0;
}

int
spdk_vhost_dev_register(struct spdk_vhost_dev *vdev, const char *name, const char *mask_str,
			const struct spdk_vhost_dev_backend *backend)
{
	static unsigned ctrlr_num;
	char path[PATH_MAX];
	struct stat file_stat;
	struct spdk_cpuset *cpumask;
	int rc;

	cpumask = spdk_cpuset_alloc();

	if (spdk_vhost_parse_core_mask(mask_str, cpumask) != 0) {
	}

	if (spdk_vhost_dev_find(name)) {
		SPDK_ERRLOG("vhost controller %s already exists.\n", name);
		rc = -EEXIST;
		goto out;
	}

	/* dev_dirname是启动的时候通过-S 指定的
	 * ./bin/vhost_spdk -m 0x10000010 -s 2048 -S /var/run/spdk -F /dev/hugepages
	 * /var/run/spdk下的socket文件，比如：
	 * vhost_blk_socket-107773af-72bc-4cd6-a649-9bfc0d7013a7-nvme
	 * vhost_blk_socket-562ccec7-3aea-4cd2-be01-7fab31fd3878-nvme
	 * vhost_blk_socket-b15f68c4-1f71-4317-b4ff-d39c35f6953c-nvme
	 */
	if (snprintf(path, sizeof(path), "%s%s", dev_dirname, name) >= (int)sizeof(path)) {
		SPDK_ERRLOG("Resulting socket path for controller %s is too long: %s%s\n", name, dev_dirname, name);
	}

	/* Register vhost driver to handle vhost messages. */
	if (stat(path, &file_stat) != -1) {
		if (!S_ISSOCK(file_stat.st_mode)) { /* 文件存在且不是socket文件 */
			SPDK_ERRLOG("Cannot create a domain socket at path \"%s\": "
				    "The file already exists and is not a socket.\n",
				    path);
			rc = -EIO;
			goto out;
		} else if (unlink(path) != 0) { /* socket存在但是unlink失败 */
			SPDK_ERRLOG("Cannot create a domain socket at path \"%s\": "
				    "The socket already exists and failed to unlink.\n",
				    path);
			rc = -EIO;
			goto out;
		}
	}

	/* 创建struct vhost_user_socket *vsocket，并且创建socket */
	if (rte_vhost_driver_register(path, 0) != 0) {
	}

	if (rte_vhost_driver_set_features(path, backend->virtio_features) ||
	    rte_vhost_driver_disable_features(path, backend->disabled_features)) {
	}

	/* struct vhost_user_socket *vsocket->notify_ops = g_spdk_vhost_ops */
	if (rte_vhost_driver_callback_register(path, &g_spdk_vhost_ops) != 0) {
	}

	/* The following might start a POSIX thread that polls for incoming
	 * socket connections and calls backend->start/stop_device. These backend
	 * callbacks are also protected by the global SPDK vhost mutex, so we're
	 * safe with not initializing the vdev just yet.
	 */
	/* 调用_start_rte_driver */
	if (spdk_call_unaffinitized(_start_rte_driver, path) == NULL) {
	}

	/* name 为这种vhost_blk_socket-632f5018-0c5c-49b7-9aa9-aba41291c3d3-nvme */
	vdev->name = strdup(name);
	vdev->path = strdup(path);
	vdev->id = ctrlr_num++;
	vdev->vid = -1;
	vdev->lcore = -1;
	vdev->cpumask = cpumask;
	vdev->registered = true;
	vdev->backend = backend;

	spdk_vhost_set_coalescing(vdev, SPDK_VHOST_COALESCING_DELAY_BASE_US,
		SPDK_VHOST_VQ_IOPS_COALESCING_THRESHOLD);
	vdev->next_stats_check_time = 0;
	vdev->stats_check_interval = SPDK_VHOST_DEV_STATS_CHECK_INTERVAL_MS * spdk_get_ticks_hz() / 1000UL;

	/* 向g_spdk_vhost_devices添加元素，这里注册的vdev的vid都是-1 */
	TAILQ_INSERT_TAIL(&g_spdk_vhost_devices, vdev, tailq);

	SPDK_INFOLOG(SPDK_LOG_VHOST, "Controller %s: new controller added\n", vdev->name);
	return 0;
	/* 后续的流程就都在fdset_event_dispatch中了 */
}

/*
 * Register a new vhost-user socket; here we could act as server
 * (the default case), or client (when RTE_VHOST_USER_CLIENT) flag
 * is set.
 */
int
rte_vhost_driver_register(const char *path, uint64_t flags)
{
	int ret = -1;
	struct vhost_user_socket *vsocket;

	pthread_mutex_lock(&vhost_user.mutex);

	if (vhost_user.vsocket_cnt == MAX_VHOST_SOCKET) {
		RTE_LOG(ERR, VHOST_CONFIG, "error: the number of vhost sockets reaches maximum\n");
		goto out;
	}

	/* 分配struct vhost_user_socket */
	vsocket = malloc(sizeof(struct vhost_user_socket));

	memset(vsocket, 0, sizeof(struct vhost_user_socket));
	/* 将path保存到vsocket->path中，后面通过path可以找到该vsocket */
	vsocket->path = strdup(path);
	TAILQ_INIT(&vsocket->conn_list);
	pthread_mutex_init(&vsocket->conn_mutex, NULL);
	/* flag为0，故dequeue_zero_copy为false */
	vsocket->dequeue_zero_copy = flags & RTE_VHOST_USER_DEQUEUE_ZERO_COPY;

	/*
	 * Set the supported features correctly for the builtin vhost-user
	 * net driver.
	 *
	 * Applications know nothing about features the builtin virtio net
	 * driver (virtio_net.c) supports, thus it's not possible for them
	 * to invoke rte_vhost_driver_set_features(). To workaround it, here
	 * we set it unconditionally. If the application want to implement
	 * another vhost-user driver (say SCSI), it should call the
	 * rte_vhost_driver_set_features(), which will overwrite following
	 * two values.
	 */
	vsocket->supported_features = VIRTIO_NET_SUPPORTED_FEATURES;
	vsocket->features           = VIRTIO_NET_SUPPORTED_FEATURES;

	/* flags为0,因此不走该路径*/
	if ((flags & RTE_VHOST_USER_CLIENT) != 0) {
		vsocket->reconnect = !(flags & RTE_VHOST_USER_NO_RECONNECT);
		if (vsocket->reconnect && reconn_tid == 0) {
			if (vhost_user_reconnect_init() < 0) {
				free(vsocket->path);
				free(vsocket);
				goto out;
			}
		}
	} else {
		/* flag为0 所以是server */
		vsocket->is_server = true;
	}
	ret = create_unix_socket(vsocket);

	/* 将vsocket记录到 vhost_user.vsockets数组中*/
	vhost_user.vsockets[vhost_user.vsocket_cnt++] = vsocket;

out:
	pthread_mutex_unlock(&vhost_user.mutex);

	return ret;
}

static int
create_unix_socket(struct vhost_user_socket *vsocket)
{
	int fd;
	struct sockaddr_un *un = &vsocket->un;

	/* 创建socket */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);

	RTE_LOG(INFO, VHOST_CONFIG, "vhost-user %s: socket created, fd: %d\n",
		vsocket->is_server ? "server" : "client", fd);

	/* vsocket->is_server = true */
	if (!vsocket->is_server && fcntl(fd, F_SETFL, O_NONBLOCK)) {
		RTE_LOG(ERR, VHOST_CONFIG,
			"vhost-user: can't set nonblocking mode for socket, fd: "
			"%d (%s)\n", fd, strerror(errno));
		close(fd);
		return -1;
	}

	memset(un, 0, sizeof(*un));
	un->sun_family = AF_UNIX;
	/* 将path赋值给sun_path */
	strncpy(un->sun_path, vsocket->path, sizeof(un->sun_path));
	un->sun_path[sizeof(un->sun_path) - 1] = '\0';

	/* 赋值fd到vsocket->socket_fd */
	vsocket->socket_fd = fd;
	return 0;
}

int
rte_vhost_driver_set_features(const char *path, uint64_t features)
{
	struct vhost_user_socket *vsocket;

	pthread_mutex_lock(&vhost_user.mutex);
	vsocket = find_vhost_user_socket(path);
	if (vsocket) {
		vsocket->supported_features = features;
		vsocket->features = features;
	}
	pthread_mutex_unlock(&vhost_user.mutex);

	return vsocket ? 0 : -1;
}

static struct vhost_user_socket *
find_vhost_user_socket(const char *path)
{
	int i;

	for (i = 0; i < vhost_user.vsocket_cnt; i++) {
		struct vhost_user_socket *vsocket = vhost_user.vsockets[i];

		if (!strcmp(vsocket->path, path))
			return vsocket;
	}

	return NULL;
}

int
rte_vhost_driver_callback_register(const char *path,
	struct vhost_device_ops const * const ops)
{
	struct vhost_user_socket *vsocket;

	pthread_mutex_lock(&vhost_user.mutex);
	vsocket = find_vhost_user_socket(path);
	if (vsocket)
		vsocket->notify_ops = ops;
	pthread_mutex_unlock(&vhost_user.mutex);

	return vsocket ? 0 : -1;
}

void *
spdk_call_unaffinitized(void *cb(void *arg), void *arg)
{
	rte_cpuset_t orig_cpuset;
	void *ret;

	rte_thread_get_affinity(&orig_cpuset);
	/* 解除affinity，即 */
	spdk_unaffinitize_thread();

	ret = cb(arg);

	rte_thread_set_affinity(&orig_cpuset);

	return ret;
}

static void *
_start_rte_driver(void *arg)
{
	char *path = arg;

	if (rte_vhost_driver_start(path) != 0) {
		return NULL;
	}

	return path;
}

static struct vhost_user vhost_user = {
	.fdset = {
		.fd = { [0 ... MAX_FDS - 1] = {-1, NULL, NULL, NULL, 0} },
		.fd_mutex = PTHREAD_MUTEX_INITIALIZER,
		.num = 0
	},
	.vsocket_cnt = 0,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
};


int
rte_vhost_driver_start(const char *path)
{
	struct vhost_user_socket *vsocket;
	static pthread_t fdset_tid;

	pthread_mutex_lock(&vhost_user.mutex);
	vsocket = find_vhost_user_socket(path);
	pthread_mutex_unlock(&vhost_user.mutex);

	if (fdset_tid == 0) {
		/* 只会创建一个线程 */
		int ret = pthread_create(&fdset_tid, NULL, fdset_event_dispatch, &vhost_user.fdset);
	}

	if (vsocket->is_server) /* vsocket->is_server是true */
		return vhost_user_start_server(vsocket);
	else
		return vhost_user_start_client(vsocket);
}

static int
vhost_user_start_server(struct vhost_user_socket *vsocket)
{
	int ret;
	int fd = vsocket->socket_fd;
	const char *path = vsocket->path;

	ret = bind(fd, (struct sockaddr *)&vsocket->un, sizeof(vsocket->un));
	RTE_LOG(INFO, VHOST_CONFIG, "bind to %s\n", path);

	change_unix_socket_mode(vsocket);

	ret = listen(fd, MAX_VIRTIO_BACKLOG);
	/* 将fd添加到vhost_user.fdset中，fdset_event_dispatch一直在poll vhost_user.fdset中的fd，
	 * 当有client链接上，fdset_event_dispatch线程会调用vhost_user_server_new_connection */
	ret = fdset_add(&vhost_user.fdset, fd, vhost_user_server_new_connection, NULL, vsocket);

	return 0;
}

static void
change_unix_socket_mode(struct vhost_user_socket *vsocket)
{
	struct sockaddr_un *un = &vsocket->un;
	struct stat file_stat;
	mode_t file_mode;
	
	if (stat((const char*)un->sun_path, &file_stat)) {
		RTE_LOG(ERR, VHOST_CONFIG, "can't get file %s stat. err:%d\n",
				un->sun_path, errno);
		return;
	}

	file_mode = file_stat.st_mode;
	RTE_LOG(INFO, VHOST_CONFIG, "the old mode %o, new mode %o\n",
			file_mode, file_mode | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (chmod((const char*)un->sun_path,
				file_mode | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)) {
		RTE_LOG(ERR, VHOST_CONFIG, "change file %s mode failed. err:%d\n",
				un->sun_path, errno);
		return;
	}

	return;
}


/**
 * This functions runs in infinite blocking loop until there is no fd in
 * pfdset. It calls corresponding r/w handler if there is event on the fd.
 *
 * Before the callback is called, we set the flag to busy status; If other
 * thread(now rte_vhost_driver_unregister) calls fdset_del concurrently, it
 * will wait until the flag is reset to zero(which indicates the callback is
 * finished), then it could free the context after fdset_del.
 */
void *
fdset_event_dispatch(void *arg)
{
	int i;
	struct pollfd *pfd;
	struct fdentry *pfdentry;
	fd_cb rcb, wcb;
	void *dat;
	int fd, numfds;
	int remove1, remove2;
	int need_shrink;
	struct fdset *pfdset = arg;
	bool first = true;

	while (1) {

		/*
		 * When poll is blocked, other threads might unregister
		 * listenfds from and register new listenfds into fdset.
		 * When poll returns, the entries for listenfds in the fdset
		 * might have been updated. It is ok if there is unwanted call
		 * for new listenfds.
		 */
		pthread_mutex_lock(&pfdset->fd_mutex);
		numfds = pfdset->num;
		pthread_mutex_unlock(&pfdset->fd_mutex);

		if (first) {
			poll(pfdset->rwfds, numfds, 10 /* millisecs */);
			first = false;
		} else {
			poll(pfdset->rwfds, numfds, 10 /* millisecs */);
		}

		need_shrink = 0;
		for (i = 0; i < numfds; i++) {
			int used = 0;
			pthread_mutex_lock(&pfdset->fd_mutex);

			pfdentry = &pfdset->fd[i];
			fd = pfdentry->fd;
			pfd = &pfdset->rwfds[i];

			if (fd < 0) {
				need_shrink = 1;
				pfdentry->used = -1;
				pthread_mutex_unlock(&pfdset->fd_mutex);
				continue;
			}

			if (!pfd->revents) {
				pthread_mutex_unlock(&pfdset->fd_mutex);
				continue;
			}

			remove1 = remove2 = 0;

			rcb = pfdentry->rcb;
			wcb = pfdentry->wcb;
			dat = pfdentry->dat;
			pfdentry->busy = 1;
			used = pfdentry->used;

			pthread_mutex_unlock(&pfdset->fd_mutex);

			if (rcb && (pfd->revents & (POLLIN | FDPOLLERR) || (used == 0)))
				/* 有可以读的事件，
				 * 有client连接时调用：vhost_user_server_new_connection
				 * 连接之后会调用：vhost_user_read_cb
				 */
				rcb(fd, dat, &remove1);
			if (wcb && pfd->revents & (POLLOUT | FDPOLLERR))
				wcb(fd, dat, &remove2);
			pfdentry->busy = 0;
			/*
			 * fdset_del needs to check busy flag.
			 * We don't allow fdset_del to be called in callback
			 * directly.
			 */
			/*
			 * When we are to clean up the fd from fdset,
			 * because the fd is closed in the cb,
			 * the old fd val could be reused by when creates new
			 * listen fd in another thread, we couldn't call
			 * fd_set_del.
			 */
			if (remove1 || remove2) {
				pfdentry->fd = -1;
				need_shrink = 1;
			}
		}

		if (need_shrink)
			fdset_shrink(pfdset);
	}

	return NULL;
}

/**
 * Register the fd in the fdset with read/write handler and context.
 */
int
fdset_add(struct fdset *pfdset, int fd, fd_cb rcb, fd_cb wcb, void *dat)
{
	int i;

	pthread_mutex_lock(&pfdset->fd_mutex);
	i = pfdset->num < MAX_FDS ? pfdset->num++ : -1;
	if (i == -1) {
		fdset_shrink_nolock(pfdset);
		i = pfdset->num < MAX_FDS ? pfdset->num++ : -1;
		if (i == -1) {
			pthread_mutex_unlock(&pfdset->fd_mutex);
			return -2;
		}
	}

	fdset_add_fd(pfdset, i, fd, rcb, wcb, dat);
	pthread_mutex_unlock(&pfdset->fd_mutex);

	return 0;
}

static void
fdset_add_fd(struct fdset *pfdset, int idx, int fd,
	fd_cb rcb, fd_cb wcb, void *dat)
{
	struct fdentry *pfdentry = &pfdset->fd[idx];
	struct pollfd *pfd = &pfdset->rwfds[idx];

	/* fdset_event_dispatch中会调用 */
	pfdentry->fd  = fd;
	pfdentry->rcb = rcb;  /* rcb = vhost_user_server_new_connection */
	pfdentry->wcb = wcb;
	pfdentry->dat = dat;
	pfdentry->used = 1;

	pfd->fd = fd;
	pfd->events  = rcb ? POLLIN : 0;
	pfd->events |= wcb ? POLLOUT : 0;
	pfd->revents = 0;
}

/* call back when there is new vhost-user connection from client  */
static void
vhost_user_server_new_connection(int fd, void *dat, int *remove __rte_unused)
{
	struct vhost_user_socket *vsocket = dat;

	fd = accept(fd, NULL, NULL);

	RTE_LOG(INFO, VHOST_CONFIG, "new vhost user connection is %d\n", fd);
	vhost_user_add_connection(fd, vsocket);
}

static void
vhost_user_add_connection(int fd, struct vhost_user_socket *vsocket)
{
	int vid;
	size_t size;
	struct vhost_user_connection *conn;
	int ret;

	conn = malloc(sizeof(*conn));
	/* 创建struct virtio_net    dev */
	vid = vhost_new_device(vsocket->features);

	size = strnlen(vsocket->path, PATH_MAX);
	/* struct virtio_net *dev的ifname = vsocket->path */
	vhost_set_ifname(vid, vsocket->path, size);
	/* vsocket->dequeue_zero_copy为false */
	if (vsocket->dequeue_zero_copy)
		vhost_enable_dequeue_zero_copy(vid);

	RTE_LOG(INFO, VHOST_CONFIG, "new device, handle is %d\n", vid);

	if (vsocket->notify_ops->new_connection) {
		/* 调用g_spdk_vhost_ops的new_connection
		 * dev->vid = vid
		 * 可以通过vid找到struct virtio_net *dev
		 * struct spdk_vhost_dev *vdev, vdev->vid = vid
		 */
		ret = vsocket->notify_ops->new_connection(vid);
	}

	conn->connfd = fd;
	conn->vsocket = vsocket;
	conn->vid = vid;
	/* 将该fd添加到poll中，该函数最终也会在fdset_event_dispatch中被调用
	 * 虚拟机数据收发将会调用vhost_user_read_cb
	 */
	ret = fdset_add(&vhost_user.fdset, fd, vhost_user_read_cb, NULL, conn);

	pthread_mutex_lock(&vsocket->conn_mutex);
	/* 添加 conn到vsocket->conn_list */
	TAILQ_INSERT_TAIL(&vsocket->conn_list, conn, next);
	pthread_mutex_unlock(&vsocket->conn_mutex);
	return;
}

/*
 * Invoked when there is a new vhost-user connection established (when
 * there is a new virtio device being attached).
 */
int
vhost_new_device(uint64_t features)
{
	struct virtio_net *dev;
	int i;

	/* 创建struct virtio_net   dev */
	dev = rte_zmalloc(NULL, sizeof(struct virtio_net), 0);

	for (i = 0; i < MAX_VHOST_DEVICE; i++) {
		if (vhost_devices[i] == NULL)
			break;
	}

	vhost_devices[i] = dev;
	dev->vid = i;
	dev->features = features;
	dev->protocol_features = VHOST_USER_PROTOCOL_FEATURES;
	dev->slave_req_fd = -1;

	return i;
}


void
vhost_set_ifname(int vid, const char *if_name, unsigned int if_len)
{
	struct virtio_net *dev;
	unsigned int len;

	dev = get_device(vid);

	len = if_len > sizeof(dev->ifname) ? sizeof(dev->ifname) : if_len;
	/* ifname为vsocket->path */
	strncpy(dev->ifname, if_name, len);
	dev->ifname[sizeof(dev->ifname) - 1] = '\0';
}

struct virtio_net *
get_device(int vid)
{
	struct virtio_net *dev = vhost_devices[vid];

	return dev;
}

static int
new_connection(int vid)
{
	struct spdk_vhost_dev *vdev;
	char ifname[PATH_MAX];

	pthread_mutex_lock(&g_spdk_vhost_mutex);
	/* 通过vid找到dev，然后找到ifname */
	if (rte_vhost_get_ifname(vid, ifname, PATH_MAX) < 0) {
	}

	vdev = spdk_vhost_dev_find(ifname);

	/* 将vid和vdev关联起来，相当于将vdev和dev关联起来 
	 * 没新连接一个vhost user就会执行vdev和vid的绑定
	 * 后面的绑定会替换前面的，所以会导致先去的peer close的时候
	 * destroy_connection 通过vid找不到之前对应的vdev
	 * 
	 */
	vdev->vid = vid;
	pthread_mutex_unlock(&g_spdk_vhost_mutex);
	return 0;
}

struct spdk_vhost_dev *
spdk_vhost_dev_find(const char *ctrlr_name)
{
	struct spdk_vhost_dev *vdev;
	size_t dev_dirname_len = strlen(dev_dirname);

	if (strncmp(ctrlr_name, dev_dirname, dev_dirname_len) == 0) {
		/* 跳过dev_dirname字符串的长度 */
		ctrlr_name += dev_dirname_len;
	}

	TAILQ_FOREACH(vdev, &g_spdk_vhost_devices, tailq) {
		if (strcmp(vdev->name, ctrlr_name) == 0) {
			return vdev;
		}
	}

	return NULL;
}


static void
vhost_user_read_cb(int connfd, void *dat, int *remove)
{
	struct vhost_user_connection *conn = dat;
	struct vhost_user_socket *vsocket = conn->vsocket;
	int ret;

	ret = vhost_user_msg_handler(conn->vid, connfd);
	if (ret < 0) {
		close(connfd);
		*remove = 1;
		vhost_destroy_device(conn->vid);

		if (vsocket->notify_ops->destroy_connection)
			vsocket->notify_ops->destroy_connection(conn->vid);

		pthread_mutex_lock(&vsocket->conn_mutex);
		TAILQ_REMOVE(&vsocket->conn_list, conn, next);
		pthread_mutex_unlock(&vsocket->conn_mutex);

		free(conn);

		if (vsocket->reconnect) {
			create_unix_socket(vsocket);
			vhost_user_start_client(vsocket);
		}
	}
}
/*
 * Maximum size of virtio device config space
 */
 /* refer to hw/virtio/vhost-user.c */
#define VHOST_MEMORY_MAX_NREGIONS 8
#define VHOST_USER_MAX_CONFIG_SIZE 256

#define VHOST_USER_PROTOCOL_F_MQ	0
#define VHOST_USER_PROTOCOL_F_LOG_SHMFD	1
#define VHOST_USER_PROTOCOL_F_RARP	2
#define VHOST_USER_PROTOCOL_F_REPLY_ACK	3
#define VHOST_USER_PROTOCOL_F_NET_MTU 4
#define VHOST_USER_PROTOCOL_F_SLAVE_REQ 5
#define VHOST_USER_PROTOCOL_F_CONFIG 9
#define VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD 12
#define VHOST_USER_PROTOCOL_F_IO_DRAIN 20

#define VHOST_USER_PROTOCOL_FEATURES	((1ULL << VHOST_USER_PROTOCOL_F_MQ) | \
					 (1ULL << VHOST_USER_PROTOCOL_F_LOG_SHMFD) |\
					 (1ULL << VHOST_USER_PROTOCOL_F_RARP) | \
					 (1ULL << VHOST_USER_PROTOCOL_F_REPLY_ACK) | \
					 (1ULL << VHOST_USER_PROTOCOL_F_NET_MTU) | \
					 (1ULL << VHOST_USER_PROTOCOL_F_SLAVE_REQ) | \
					 (1ULL << VHOST_USER_PROTOCOL_F_CONFIG) | \
					 (1ULL << VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD) | \
					 (1ULL << VHOST_USER_PROTOCOL_F_IO_DRAIN))

typedef enum VhostUserRequest {
	VHOST_USER_NONE = 0,
	VHOST_USER_GET_FEATURES = 1,
	VHOST_USER_SET_FEATURES = 2,
	VHOST_USER_SET_OWNER = 3,
	VHOST_USER_RESET_OWNER = 4,
	VHOST_USER_SET_MEM_TABLE = 5,
	VHOST_USER_SET_LOG_BASE = 6,
	VHOST_USER_SET_LOG_FD = 7,
	VHOST_USER_SET_VRING_NUM = 8,
	VHOST_USER_SET_VRING_ADDR = 9,
	VHOST_USER_SET_VRING_BASE = 10,
	VHOST_USER_GET_VRING_BASE = 11,
	VHOST_USER_SET_VRING_KICK = 12,
	VHOST_USER_SET_VRING_CALL = 13,
	VHOST_USER_SET_VRING_ERR = 14,
	VHOST_USER_GET_PROTOCOL_FEATURES = 15,
	VHOST_USER_SET_PROTOCOL_FEATURES = 16,
	VHOST_USER_GET_QUEUE_NUM = 17,
	VHOST_USER_SET_VRING_ENABLE = 18,
	VHOST_USER_SEND_RARP = 19,
	VHOST_USER_NET_SET_MTU = 20,
	VHOST_USER_SET_SLAVE_REQ_FD = 21,
	VHOST_USER_GET_CONFIG = 24,
	VHOST_USER_SET_CONFIG = 25,
	VHOST_USER_GET_INFLIGHT_FD = 31,
	VHOST_USER_SET_INFLIGHT_FD = 32,
	VHOST_USER_GET_INFLIGHT_NUM = 33,
	/* Reserve some fields for upstream features */
	VHOST_USER_GET_PENDING_NUM = 60,
	VHOST_USER_SET_IO_DOWN = 61,
	VHOST_USER_SET_IO_CLEANUP = 62,
	VHOST_USER_SET_IO_FINAL = 63,
	VHOST_USER_NVME_ADMIN = 80,
	VHOST_USER_NVME_SET_CQ_CALL = 81,
	VHOST_USER_NVME_GET_CAP = 82,
	VHOST_USER_NVME_START_STOP = 83,
	VHOST_USER_NVME_IO_CMD = 84,
	VHOST_USER_MAX
} VhostUserRequest;

typedef enum VhostUserSlaveRequest {
	VHOST_USER_SLAVE_NONE = 0,
	VHOST_USER_SLAVE_IOTLB_MSG = 1,
	VHOST_USER_SLAVE_CONFIG_CHANGE_MSG = 2,
	VHOST_USER_SLAVE_MAX
} VhostUserSlaveRequest;

static const char *vhost_message_str[VHOST_USER_MAX] = {
	[VHOST_USER_NONE] = "VHOST_USER_NONE",
	[VHOST_USER_GET_FEATURES] = "VHOST_USER_GET_FEATURES",
	[VHOST_USER_SET_FEATURES] = "VHOST_USER_SET_FEATURES",
	[VHOST_USER_SET_OWNER] = "VHOST_USER_SET_OWNER",
	[VHOST_USER_RESET_OWNER] = "VHOST_USER_RESET_OWNER",
	[VHOST_USER_SET_MEM_TABLE] = "VHOST_USER_SET_MEM_TABLE",
	[VHOST_USER_SET_LOG_BASE] = "VHOST_USER_SET_LOG_BASE",
	[VHOST_USER_SET_LOG_FD] = "VHOST_USER_SET_LOG_FD",
	[VHOST_USER_SET_VRING_NUM] = "VHOST_USER_SET_VRING_NUM",
	[VHOST_USER_SET_VRING_ADDR] = "VHOST_USER_SET_VRING_ADDR",
	[VHOST_USER_SET_VRING_BASE] = "VHOST_USER_SET_VRING_BASE",
	[VHOST_USER_GET_VRING_BASE] = "VHOST_USER_GET_VRING_BASE",
	[VHOST_USER_SET_VRING_KICK] = "VHOST_USER_SET_VRING_KICK",
	[VHOST_USER_SET_VRING_CALL] = "VHOST_USER_SET_VRING_CALL",
	[VHOST_USER_SET_VRING_ERR]  = "VHOST_USER_SET_VRING_ERR",
	[VHOST_USER_GET_PROTOCOL_FEATURES]  = "VHOST_USER_GET_PROTOCOL_FEATURES",
	[VHOST_USER_SET_PROTOCOL_FEATURES]  = "VHOST_USER_SET_PROTOCOL_FEATURES",
	[VHOST_USER_GET_QUEUE_NUM]  = "VHOST_USER_GET_QUEUE_NUM",
	[VHOST_USER_SET_VRING_ENABLE]  = "VHOST_USER_SET_VRING_ENABLE",
	[VHOST_USER_SEND_RARP]  = "VHOST_USER_SEND_RARP",
	[VHOST_USER_NET_SET_MTU]  = "VHOST_USER_NET_SET_MTU",
	[VHOST_USER_SET_SLAVE_REQ_FD]  = "VHOST_USER_SET_SLAVE_REQ_FD",
	[VHOST_USER_GET_CONFIG] = "VHOST_USER_GET_CONFIG",
	[VHOST_USER_SET_CONFIG] = "VHOST_USER_SET_CONFIG",
	[VHOST_USER_GET_INFLIGHT_FD] = "VHOST_USER_GET_INFLIGHT_FD",
	[VHOST_USER_SET_INFLIGHT_FD] = "VHOST_USER_SET_INFLIGHT_FD",
	[VHOST_USER_GET_INFLIGHT_NUM] = "VHOST_USER_GET_INFLIGHT_NUM",
	[VHOST_USER_GET_PENDING_NUM] = "VHOST_USER_GET_PENDING_NUM",
	[VHOST_USER_SET_IO_DOWN] = "VHOST_USER_SET_IO_DOWN",
	[VHOST_USER_SET_IO_CLEANUP] = "VHOST_USER_SET_IO_CLEANUP",
	[VHOST_USER_SET_IO_FINAL] = "VHOST_USER_SET_IO_FINAL",
	[VHOST_USER_NVME_ADMIN] = "VHOST_USER_NVME_ADMIN",
	[VHOST_USER_NVME_SET_CQ_CALL] = "VHOST_USER_NVME_SET_CQ_CALL",
	[VHOST_USER_NVME_GET_CAP] = "VHOST_USER_NVME_GET_CAP",
	[VHOST_USER_NVME_START_STOP] = "VHOST_USER_NVME_START_STOP",
	[VHOST_USER_NVME_IO_CMD] = "VHOST_USER_NVME_IO_CMD"
};

typedef enum VhostUserSlaveRequest {
	VHOST_USER_SLAVE_NONE = 0,
	VHOST_USER_SLAVE_IOTLB_MSG = 1,
	VHOST_USER_SLAVE_CONFIG_CHANGE_MSG = 2,
	VHOST_USER_SLAVE_MAX
} VhostUserSlaveRequest;

typedef struct VhostUserMemoryRegion {
	uint64_t guest_phys_addr;
	uint64_t memory_size;
	uint64_t userspace_addr;
	uint64_t mmap_offset;
} VhostUserMemoryRegion;

typedef struct VhostUserMemory {
	uint32_t nregions;
	uint32_t padding;
	VhostUserMemoryRegion regions[VHOST_MEMORY_MAX_NREGIONS];
} VhostUserMemory;

/**
 * Device structure contains all configuration information relating
 * to the device.
 */
struct virtio_net {
	/* Frontend (QEMU) memory and memory region information */
	struct rte_vhost_memory	*mem;
	uint64_t		features;
	uint64_t		negotiated_features;
	uint64_t		protocol_features;
	int			vid;
	uint32_t		is_nvme;
	uint32_t		flags;
	uint16_t		vhost_hlen;
	/* to tell if we need broadcast rarp packet */
	rte_atomic16_t		broadcast_rarp;
	uint32_t		nr_vring;
	int			dequeue_zero_copy;
	struct vhost_virtqueue	*virtqueue[VHOST_MAX_QUEUE_PAIRS * 2];
#define IF_NAME_SZ (PATH_MAX > IFNAMSIZ ? PATH_MAX : IFNAMSIZ)
	char			ifname[IF_NAME_SZ];
	uint64_t		log_size;
	uint64_t		log_base;
	uint64_t		log_addr;
	uint64_t		inflight_addr;
	uint64_t		inflight_size;
	struct ether_addr	mac;
	uint16_t		mtu;

	struct vhost_device_ops const *notify_ops;

	uint32_t		nr_guest_pages;
	uint32_t		max_guest_pages;
	struct guest_page       *guest_pages;
	int                     has_new_mem_table;
	struct VhostUserMemory  mem_table;
	int                     mem_table_fds[VHOST_MEMORY_MAX_NREGIONS];

	int			slave_req_fd;
} __rte_cache_aligned;

/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT       1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE      2
/* This means the buffer contains a list of buffer descriptors. */
#define VRING_DESC_F_INDIRECT   4

/* The Host uses this in used->flags to advise the Guest: don't kick me
 * when you add a buffer.  It's unreliable, so it's simply an
 * optimization.  Guest will still kick if it's out of buffers. */
#define VRING_USED_F_NO_NOTIFY  1
/* The Guest uses this in avail->flags to advise the Host: don't
 * interrupt me when you consume a buffer.  It's unreliable, so it's
 * simply an optimization.  */
#define VRING_AVAIL_F_NO_INTERRUPT  1

/* VirtIO ring descriptors: 16 bytes.
 * These can chain together via "next". */
struct vring_desc {
	uint64_t addr;  /*  Address (guest-physical). */
	uint32_t len;   /* Length. */
	uint16_t flags; /* The flags as indicated above. */
	uint16_t next;  /* We chain unused descriptors via this. */
};

struct vring_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[0];
};

/* id is a 16bit index. uint32_t is used here for ids for padding reasons. */
struct vring_used_elem {
	/* Index of start of used descriptor chain. */
	uint32_t id;
	/* Total length of the descriptor chain which was written to. */
	uint32_t len;
};

struct vring_used {
	uint16_t flags;
	volatile uint16_t idx;
	struct vring_used_elem ring[0];
};

struct vring {
	unsigned int num;
	struct vring_desc  *desc;
	struct vring_avail *avail;
	struct vring_used  *used;
};


/*
log中msg的顺序
[2020-07-30 09:53:50.725]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_SLAVE_REQ_FD
[2020-07-30 09:54:05.706]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_INFLIGHT_FD
[2020-07-30 09:56:20.852]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_SLAVE_REQ_FD
[2020-07-30 09:56:20.852]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_INFLIGHT_FD
[2020-07-30 09:56:20.852]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_FEATURES
[2020-07-30 09:56:20.852]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_MEM_TABLE
[2020-07-30 09:56:20.852]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_VRING_NUM
[2020-07-30 09:56:20.852]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_VRING_BASE
[2020-07-30 09:56:20.852]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_VRING_ADDR
[2020-07-30 09:56:20.854]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_VRING_KICK
[2020-07-30 09:56:20.854]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_VRING_CALL
[2020-07-30 09:56:20.856]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_VRING_CALL
[2020-07-30 09:56:20.860]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_VRING_CALL
[2020-07-30 09:56:20.864]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-c6100cbb-1577-4b47-941e-3d2a1280e6b6-nvme: read message VHOST_USER_SET_VRING_CALL

read message VHOST_USER_SET_SLAVE_REQ_FD
read message VHOST_USER_SET_INFLIGHT_FD
read message VHOST_USER_SET_SLAVE_REQ_FD
read message VHOST_USER_SET_INFLIGHT_FD
read message VHOST_USER_SET_FEATURES
read message VHOST_USER_SET_MEM_TABLE
read message VHOST_USER_SET_VRING_NUM
read message VHOST_USER_SET_VRING_BASE
read message VHOST_USER_SET_VRING_ADDR
read message VHOST_USER_SET_VRING_KICK
read message VHOST_USER_SET_VRING_CALL
read message VHOST_USER_SET_VRING_CALL
read message VHOST_USER_SET_VRING_CALL
read message VHOST_USER_SET_VRING_CALL
*/


/*
[2020-04-07 11:43:40.221]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-2bd5a1b6-eff5-4083-961e-5a8010a62019-nvme: read message VHOST_USER_GET_INFLIGHT_FD
[2020-04-07 11:43:40.221]: VHOST_CONFIG: set_inflight_fd num_queues: 4
[2020-04-07 11:43:40.221]: VHOST_CONFIG: set_inflight_fd queue_size: 128
[2020-04-07 11:43:40.221]: VHOST_CONFIG: send inflight mmap_size: 8448
[2020-04-07 11:43:40.221]: VHOST_CONFIG: send inflight mmap offset: 0
[2020-04-07 11:43:40.221]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-2bd5a1b6-eff5-4083-961e-5a8010a62019-nvme: read message VHOST_USER_SET_INFLIGHT_FD
[2020-04-07 11:43:40.221]: VHOST_CONFIG: set_inflight_fd mmap_size: 8448
[2020-04-07 11:43:40.221]: VHOST_CONFIG: set_inflight_fd mmap_offset: 0
[2020-04-07 11:43:40.221]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-2bd5a1b6-eff5-4083-961e-5a8010a62019-nvme: read message VHOST_USER_SET_FEATURES
[2020-04-07 11:43:40.221]: VHOST_CONFIG: dev->negotiated_features=0x110001444
[2020-04-07 11:43:40.221]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-2bd5a1b6-eff5-4083-961e-5a8010a62019-nvme: read message VHOST_USER_SET_MEM_TABLE
[2020-04-07 11:43:40.221]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-2bd5a1b6-eff5-4083-961e-5a8010a62019-nvme: read message VHOST_USER_SET_VRING_NUM
[2020-04-07 11:43:40.221]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-2bd5a1b6-eff5-4083-961e-5a8010a62019-nvme: read message VHOST_USER_SET_VRING_BASE
[2020-04-07 11:43:40.221]: VHOST_CONFIG: /var/run/spdk/vhost_blk_socket-2bd5a1b6-eff5-4083-961e-5a8010a62019-nvme: read message VHOST_USER_SET_VRING_ADDR

*/

int
vhost_user_msg_handler(int vid, int fd)
{
	struct virtio_net *dev;
	struct VhostUserMsg msg;
	struct vhost_vring_file file;
	int ret;
	uint64_t cap;
	uint64_t enable;
	uint8_t cqe[16];
	uint8_t cmd[64];
	uint8_t buf[4096];
	uint16_t qid, tail_head;
	bool is_submission_queue;

	dev = get_device(vid);

	/* struct virtio_net *dev刚创建dev->notify_ops为null */
	if (!dev->notify_ops) {
		/* struct virtio_net *dev notify_ops = g_spdk_vhost_ops*/
		dev->notify_ops = vhost_driver_callback_get(dev->ifname);
	}

	ret = read_vhost_message(fd, &msg);
	if (ret <= 0 || msg.request.master >= VHOST_USER_MAX) {
		if (ret < 0)
			RTE_LOG(ERR, VHOST_CONFIG,
				"vhost read message failed\n");
		else if (ret == 0)
			RTE_LOG(INFO, VHOST_CONFIG,
				"vhost peer closed\n");
		else
			RTE_LOG(ERR, VHOST_CONFIG,
				"vhost read incorrect message\n");
		return -1;
	}

	memset(&msg, 0, sizeof(struct VhostUserMsg));
	ret = read_vhost_message(fd, &msg);

	RTE_LOG(INFO, VHOST_CONFIG, "%s: read message %s\n", dev->ifname, vhost_message_str[msg.request]);

	ret = vhost_user_check_and_alloc_queue_pair(dev, &msg);

	switch (msg.request) {
	case VHOST_USER_GET_CONFIG:
		/* 调用g_spdk_vhost_ops的get_config */
		if (dev->notify_ops->get_config(dev->vid,
						msg.payload.config.region,
						msg.payload.config.size) != 0) {
			msg.size = sizeof(uint64_t);
		}
		send_vhost_message(fd, &msg);
		break;
	case VHOST_USER_SET_CONFIG:
		if ((dev->notify_ops->set_config(dev->vid,
						msg.payload.config.region,
						msg.payload.config.offset,
						msg.payload.config.size,
						msg.payload.config.flags)) != 0) {
			ret = 1;
		} else {
			ret = 0;
		}
		break;
	case VHOST_USER_NVME_ADMIN:
		if (!dev->is_nvme) {
			dev->is_nvme = 1;
		}
		memcpy(cmd, msg.payload.nvme.cmd.req, sizeof(cmd));
		ret = vhost_user_nvme_admin_passthrough(dev, cmd, cqe, buf);
		memcpy(msg.payload.nvme.cmd.cqe, cqe, sizeof(cqe));
		msg.size = sizeof(cqe);
		/* NVMe Identify Command */
		if (cmd[0] == 0x06) {
			memcpy(msg.payload.nvme.buf, &buf, 4096);
			msg.size += 4096;
		} else if (cmd[0] == 0x09 || cmd[0] == 0x0a) {
			memcpy(&msg.payload.nvme.buf, &buf, 4);
			msg.size += 4096;

		}
		send_vhost_message(fd, &msg);
		break;
	case VHOST_USER_NVME_SET_CQ_CALL:
		file.index = msg.payload.u64 & VHOST_USER_VRING_IDX_MASK;
		file.fd = msg.fds[0];
		ret = vhost_user_nvme_set_cq_call(dev, file.index, file.fd);
		break;
	case VHOST_USER_NVME_GET_CAP:
		ret = vhost_user_nvme_get_cap(dev, &cap);
		if (!ret)
			msg.payload.u64 = cap;
		else
			msg.payload.u64 = 0;
		msg.size = sizeof(msg.payload.u64);
		send_vhost_message(fd, &msg);
		break;
	case VHOST_USER_NVME_START_STOP:
		enable = msg.payload.u64;
		/* device must be started before set cq call */
		if (enable) {
			if (!(dev->flags & VIRTIO_DEV_RUNNING) &&
				!(dev->flags & VIRTIO_DEV_DRAIN)) {
				/* net_dev->notify_ops = g_spdk_vhost_ops */
				if (dev->notify_ops->new_device(dev->vid) == 0)
					dev->flags |= VIRTIO_DEV_RUNNING;
			}
		} else {
			if ((dev->flags & VIRTIO_DEV_RUNNING) &&
				!(dev->flags & VIRTIO_DEV_DRAIN)) {
				dev->flags &= ~VIRTIO_DEV_RUNNING;
				dev->notify_ops->destroy_device(dev->vid);
			}
		}
		break;
	case VHOST_USER_NVME_IO_CMD:
		qid = msg.payload.nvme_io.qid;
		tail_head = msg.payload.nvme_io.tail_head;
		is_submission_queue = (msg.payload.nvme_io.queue_type == VHOST_USER_NVME_SUBMISSION_QUEUE) ? true : false;
		vhost_user_nvme_io_request_passthrough(dev, qid, tail_head, is_submission_queue);
		break;
	case VHOST_USER_GET_FEATURES:
		msg.payload.u64 = vhost_user_get_features(dev);
		msg.size = sizeof(msg.payload.u64);
		send_vhost_message(fd, &msg);
		break;
	case VHOST_USER_SET_FEATURES:
		vhost_user_set_features(dev, msg.payload.u64);
		break;

	case VHOST_USER_GET_PROTOCOL_FEATURES:
		msg.payload.u64 = VHOST_USER_PROTOCOL_FEATURES;
		msg.size = sizeof(msg.payload.u64);
		send_vhost_message(fd, &msg);
		break;
	case VHOST_USER_SET_PROTOCOL_FEATURES:
		vhost_user_set_protocol_features(dev, msg.payload.u64);
		break;

	case VHOST_USER_SET_OWNER:
		vhost_user_set_owner();
		break;
	case VHOST_USER_RESET_OWNER:
		vhost_user_reset_owner(dev);
		break;

	case VHOST_USER_SET_MEM_TABLE:
		ret = vhost_user_set_mem_table(dev, &msg);
		break;

	case VHOST_USER_SET_LOG_BASE:
		vhost_user_set_log_base(dev, &msg);

		/* it needs a reply */
		msg.size = sizeof(msg.payload.u64);
		send_vhost_message(fd, &msg);
		break;
	case VHOST_USER_SET_LOG_FD:
		close(msg.fds[0]);
		RTE_LOG(INFO, VHOST_CONFIG, "not implemented.\n");
		break;

	case VHOST_USER_SET_VRING_NUM:
		vhost_user_set_vring_num(dev, &msg);
		break;
	case VHOST_USER_SET_VRING_ADDR:
		vhost_user_set_vring_addr(dev, &msg);
		break;
	case VHOST_USER_SET_VRING_BASE:
		vhost_user_set_vring_base(dev, &msg);
		break;

	case VHOST_USER_GET_VRING_BASE:
		vhost_user_get_vring_base(dev, &msg);
		msg.size = sizeof(msg.payload.state);
		send_vhost_message(fd, &msg);
		break;

	case VHOST_USER_SET_VRING_KICK:
		vhost_user_set_vring_kick(dev, &msg);
		break;
	case VHOST_USER_SET_VRING_CALL:
		vhost_user_set_vring_call(dev, &msg);
		break;

	case VHOST_USER_SET_VRING_ERR:
		if (!(msg.payload.u64 & VHOST_USER_VRING_NOFD_MASK))
			close(msg.fds[0]);
		RTE_LOG(INFO, VHOST_CONFIG, "not implemented\n");
		break;

	case VHOST_USER_GET_QUEUE_NUM:
		msg.payload.u64 = VHOST_MAX_QUEUE_PAIRS;
		msg.size = sizeof(msg.payload.u64);
		send_vhost_message(fd, &msg);
		break;

	case VHOST_USER_SET_VRING_ENABLE:
		vhost_user_set_vring_enable(dev, &msg);
		break;
	case VHOST_USER_SEND_RARP:
		vhost_user_send_rarp(dev, &msg);
		break;

	case VHOST_USER_NET_SET_MTU:
		ret = vhost_user_net_set_mtu(dev, &msg);
		break;

	case VHOST_USER_SET_SLAVE_REQ_FD:
		ret = vhost_user_set_req_fd(dev, &msg);
		break;

	case VHOST_USER_GET_INFLIGHT_FD:
		if(vhost_user_get_inflight_fd(dev, &msg)){
			msg.size = sizeof(msg.payload.inflight);
		}
		send_vhost_message(fd, &msg);
		if (msg.fds[0] > 0) {
			close(msg.fds[0]);
		}
		break;

	case VHOST_USER_SET_INFLIGHT_FD:
		vhost_user_set_inflight_fd(dev, &msg);
		break;

	case VHOST_USER_GET_INFLIGHT_NUM:
		if(vhost_user_get_inflight_num(dev, &msg)){
			msg.size = sizeof(msg.payload.state);
		}
		send_vhost_message(fd, &msg);
		break;

	case VHOST_USER_GET_PENDING_NUM:
		if(vhost_user_get_pending_num(dev, &msg)){
			msg.size = sizeof(msg.payload.drainio);
		}
		send_vhost_message(fd, &msg);
		return 0;

	case VHOST_USER_SET_IO_DOWN:
		if(vhost_user_set_io_down(dev, &msg)){
			msg.size = sizeof(msg.payload.drainio);
		}
		send_vhost_message(fd, &msg);
		return 0;

	case VHOST_USER_SET_IO_CLEANUP:
		if(vhost_user_set_io_cleanup(dev, &msg)){
			msg.size = sizeof(msg.payload.drainio);
		}
		send_vhost_message(fd, &msg);
		return 0;

	case VHOST_USER_SET_IO_FINAL:
		if(vhost_user_set_io_final(dev, &msg)){
			msg.size = sizeof(msg.payload.drainio);
		}
		/*
		 * NOTE: io_final is the last session for live migration,
		 * so spdk will not response to qemu.
		 */
		return 0;

	default:
		ret = -1;
		break;

	}

	if (msg.flags & VHOST_USER_NEED_REPLY) {
		msg.payload.u64 = !!ret;
		msg.size = sizeof(msg.payload.u64);
		send_vhost_message(fd, &msg);
	}

	if (!(dev->flags & VIRTIO_DEV_RUNNING) && virtio_is_ready(dev)
			&& !(dev->flags & VIRTIO_DEV_DRAIN)) {
		dev->flags |= VIRTIO_DEV_READY;

		if (!(dev->flags & VIRTIO_DEV_RUNNING)) {
			if (dev->dequeue_zero_copy) {
				RTE_LOG(INFO, VHOST_CONFIG,
						"dequeue zero copy is enabled\n");
			}
			/* 调用gg_spdk_vhost_ops的 start_device   */
			if (dev->notify_ops->new_device(dev->vid) == 0)
				dev->flags |= VIRTIO_DEV_RUNNING;
		}
	}

	return 0;
}

struct vhost_device_ops const *
vhost_driver_callback_get(const char *path)
{
	struct vhost_user_socket *vsocket;

	pthread_mutex_lock(&vhost_user.mutex);
	vsocket = find_vhost_user_socket(path);
	pthread_mutex_unlock(&vhost_user.mutex);

	return vsocket ? vsocket->notify_ops : NULL;
}

typedef struct VhostUserMsg {
	VhostUserRequest request;

#define VHOST_USER_VERSION_MASK     0x3
#define VHOST_USER_REPLY_MASK       (0x1 << 2)
#define VHOST_USER_NEED_REPLY		(0x1 << 3)
	uint32_t flags;
	uint32_t size; /* the following payload size */
	union {
#define VHOST_USER_VRING_IDX_MASK   0xff
#define VHOST_USER_VRING_NOFD_MASK  (0x1<<8)
		uint64_t u64;
		struct vhost_vring_state state;
		struct vhost_vring_addr addr;
		VhostUserMemory memory;
		VhostUserLog    log;
		VhostUserConfig config;
		struct nvme {
			union {
				uint8_t req[64];
				uint8_t cqe[16];
			} cmd;
			uint8_t buf[4096];
		} nvme;
		struct VhostUserNvmeIO nvme_io;
		struct VhostUserInflight inflight;
		struct VhostUserDrainIO drainio;
	} payload;
	int fds[VHOST_MEMORY_MAX_NREGIONS];
	int fd_num;
} __attribute((packed)) VhostUserMsg;

/**
 * Structure contains variables relevant to RX/TX virtqueues.
 */
struct vhost_virtqueue {
	struct vring_desc	*desc;
	struct vring_avail	*avail;
	struct vring_used	*used;
	uint32_t		size;

	uint16_t		last_avail_idx;
	uint16_t		last_used_idx;
#define VIRTIO_INVALID_EVENTFD		(-1)
#define VIRTIO_UNINITIALIZED_EVENTFD	(-2)

	/* Backend value to determine if device should started/stopped */
	int			backend;
	/* Used to notify the guest (trigger interrupt) */
	int			callfd;
	/* Currently unused as polling mode is enabled */
	int			kickfd;
	int			enabled;

	/* Physical address of used ring, for logging */
	uint64_t		log_guest_addr;

	uint16_t		nr_zmbuf;
	uint16_t		zmbuf_size;
	uint16_t		last_zmbuf_idx;
	struct zcopy_mbuf	*zmbufs;
	struct zcopy_mbuf_list	zmbuf_list;

	struct vring_used_elem  *shadow_used_ring;
	uint16_t                shadow_used_idx;
} __rte_cache_aligned;


#define VHOST_USER_HDR_SIZE offsetof(VhostUserMsg, payload.u64)

/* The version of the protocol we support */
#define VHOST_USER_VERSION    0x1

/* return bytes# of read on success or negative val on failure. */
static int
read_vhost_message(int sockfd, struct VhostUserMsg *msg)
{
	int ret;

	ret = read_fd_message(sockfd, (char *)msg, VHOST_USER_HDR_SIZE,
		msg->fds, VHOST_MEMORY_MAX_NREGIONS);

	if (msg && msg->size) {
		ret = read(sockfd, &msg->payload, msg->size);
	}

	return ret;
}

/* return bytes# of read on success or negative val on failure. */
int
read_fd_message(int sockfd, char *buf, int buflen, int *fds, int fd_num)
{
	struct iovec iov;
	struct msghdr msgh;
	size_t fdsize = fd_num * sizeof(int);
	char control[CMSG_SPACE(fdsize)];
	struct cmsghdr *cmsg;
	int ret;

	memset(&msgh, 0, sizeof(msgh));
	iov.iov_base = buf;
	iov.iov_len  = buflen;

	msgh.msg_iov = &iov;
	msgh.msg_iovlen = 1;
	msgh.msg_control = control;
	msgh.msg_controllen = sizeof(control);

	ret = recvmsg(sockfd, &msgh, 0);

	if (msgh.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
		RTE_LOG(ERR, VHOST_CONFIG, "truncted msg\n");
		return -1;
	}

	for (cmsg = CMSG_FIRSTHDR(&msgh); cmsg != NULL;
		cmsg = CMSG_NXTHDR(&msgh, cmsg)) {
		if ((cmsg->cmsg_level == SOL_SOCKET) &&
			(cmsg->cmsg_type == SCM_RIGHTS)) {
			memcpy(fds, CMSG_DATA(cmsg), fdsize);
			break;
		}
	}

	return ret;
}

/*
 * Allocate a queue pair if it hasn't been allocated yet
 */
static int
vhost_user_check_and_alloc_queue_pair(struct virtio_net *dev, VhostUserMsg *msg)
{
	uint32_t vring_idx;

	switch (msg->request) {
	case VHOST_USER_SET_VRING_KICK:
	case VHOST_USER_SET_VRING_CALL:
	case VHOST_USER_SET_VRING_ERR:
		vring_idx = msg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
		break;
	case VHOST_USER_SET_VRING_NUM:
	case VHOST_USER_SET_VRING_BASE:
	case VHOST_USER_GET_VRING_BASE:
	case VHOST_USER_SET_VRING_ENABLE:
		vring_idx = msg->payload.state.index;
		break;
	case VHOST_USER_SET_VRING_ADDR:
		vring_idx = msg->payload.addr.index;
		break;
	default:
		return 0;
	}

	if (dev->virtqueue[vring_idx])
		return 0;

	return alloc_vring_queue(dev, vring_idx);
}

int
alloc_vring_queue(struct virtio_net *dev, uint32_t vring_idx)
{
	struct vhost_virtqueue *vq;

	vq = rte_malloc(NULL, sizeof(struct vhost_virtqueue), 0);

	dev->virtqueue[vring_idx] = vq;
	init_vring_queue(vq);

	dev->nr_vring += 1;

	return 0;
}

static void
init_vring_queue(struct vhost_virtqueue *vq)
{
	memset(vq, 0, sizeof(struct vhost_virtqueue));

	vq->kickfd = VIRTIO_UNINITIALIZED_EVENTFD;
	vq->callfd = VIRTIO_UNINITIALIZED_EVENTFD;

	/* Backends are set to -1 indicating an inactive device. */
	vq->backend = -1;

	/*
	 * always set the vq to enabled; this is to keep compatibility
	 * with the old QEMU, whereas there is no SET_VRING_ENABLE message.
	 */
	vq->enabled = 1;

	TAILQ_INIT(&vq->zmbuf_list);
}

static int
send_vhost_message(int sockfd, struct VhostUserMsg *msg)
{
	int ret;

	if (!msg)
		return 0;

	msg->flags &= ~VHOST_USER_VERSION_MASK;
	msg->flags &= ~VHOST_USER_NEED_REPLY;
	msg->flags |= VHOST_USER_VERSION;
	msg->flags |= VHOST_USER_REPLY_MASK;

	ret = send_fd_message(sockfd, (char *)msg,
		VHOST_USER_HDR_SIZE + msg->size, msg->fds, msg->fd_num);

	return ret;
}

int
send_fd_message(int sockfd, char *buf, int buflen, int *fds, int fd_num)
{

	struct iovec iov;
	struct msghdr msgh;
	size_t fdsize = fd_num * sizeof(int);
	char control[CMSG_SPACE(fdsize)];
	struct cmsghdr *cmsg;
	int ret;

	memset(&msgh, 0, sizeof(msgh));
	iov.iov_base = buf;
	iov.iov_len = buflen;

	msgh.msg_iov = &iov;
	msgh.msg_iovlen = 1;

	if (fds && fd_num > 0) {
		msgh.msg_control = control;
		msgh.msg_controllen = sizeof(control);
		cmsg = CMSG_FIRSTHDR(&msgh);
		if (cmsg == NULL) {
			RTE_LOG(ERR, VHOST_CONFIG,  "cmsg == NULL\n");
			errno = EINVAL;
			return -1;
		}
		cmsg->cmsg_len = CMSG_LEN(fdsize);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		memcpy(CMSG_DATA(cmsg), fds, fdsize);
	} else {
		msgh.msg_control = NULL;
		msgh.msg_controllen = 0;
	}

	do {
		ret = sendmsg(sockfd, &msgh, 0);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0) {
		RTE_LOG(ERR, VHOST_CONFIG,  "sendmsg error\n");
		return ret;
	}

	return ret;
}

static int
vhost_user_set_req_fd(struct virtio_net *dev, struct VhostUserMsg *msg)
{
	int fd = msg->fds[0];

	dev->slave_req_fd = fd;
	return 0;
}

static int
vhost_user_set_inflight_fd(struct virtio_net *dev, struct VhostUserMsg *msg)
{
	int fd;
	uint64_t mmap_size, mmap_offset;
	void *addr;

	fd = msg->fds[0];

	mmap_size = msg->payload.inflight.mmap_size;
	mmap_offset = msg->payload.inflight.mmap_offset;

	RTE_LOG(INFO, VHOST_CONFIG,"set_inflight_fd mmap_size: %"PRId64"\n", mmap_size);
	RTE_LOG(INFO, VHOST_CONFIG,"set_inflight_fd mmap_offset: %"PRId64"\n", mmap_offset);

	addr = mmap(0, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mmap_offset);

	close(fd);

	/* 如果已经mmap了 */
	if (dev->inflight_addr) {
		munmap((void *)(uintptr_t)dev->inflight_addr, dev->inflight_size);
	}

	dev->inflight_addr = (uint64_t)(uintptr_t)addr;
	dev->inflight_size = mmap_size;

	return 0;
}

/*
 * We receive the negotiated features supported by us and the virtio device.
 */
static int
vhost_user_set_features(struct virtio_net *dev, uint64_t features)
{
	uint64_t vhost_features = 0;

	vhost_features = vhost_user_get_features(dev);

	if ((dev->flags & VIRTIO_DEV_RUNNING) && (dev->negotiated_features != features)
			&& !(dev->flags & VIRTIO_DEV_DRAIN)) {
		if (dev->notify_ops->features_changed) {
			dev->notify_ops->features_changed(dev->vid, features);
		} else {
			dev->flags &= ~VIRTIO_DEV_RUNNING;
			dev->notify_ops->destroy_device(dev->vid);
		}
	}

	dev->negotiated_features = features;
	if (dev->negotiated_features &
		((1 << VIRTIO_NET_F_MRG_RXBUF) | (1ULL << VIRTIO_F_VERSION_1))) {
		dev->vhost_hlen = sizeof(struct virtio_net_hdr_mrg_rxbuf);
	} else {
		dev->vhost_hlen = sizeof(struct virtio_net_hdr);
	}
	VHOST_LOG_DEBUG(VHOST_CONFIG,
		"(%d) mergeable RX buffers %s, virtio 1 %s\n",
		dev->vid,
		(dev->negotiated_features & (1 << VIRTIO_NET_F_MRG_RXBUF)) ? "on" : "off",
		(dev->negotiated_features & (1ULL << VIRTIO_F_VERSION_1)) ? "on" : "off");

	RTE_LOG(INFO, VHOST_CONFIG, "dev->negotiated_features=0x%lx\n", (long)dev->negotiated_features);
	return 0;
}

/*
 * The features that we support are requested.
 */
static uint64_t
vhost_user_get_features(struct virtio_net *dev)
{
	return dev->features;
}

static int
vhost_user_set_mem_table(struct virtio_net *dev, struct VhostUserMsg *pmsg)
{
	uint32_t i;

	if (dev->has_new_mem_table) {
		/*
		 * The previous mem table was not consumed, so close the
		 *  file descriptors from that mem table before copying
		 *  the new one.
		 */
		for (i = 0; i < dev->mem_table.nregions; i++) {
			close(dev->mem_table_fds[i]);
		}
	}

	memcpy(&dev->mem_table, &pmsg->payload.memory, sizeof(dev->mem_table));
	memcpy(dev->mem_table_fds, pmsg->fds, sizeof(dev->mem_table_fds));
	dev->has_new_mem_table = 1;
	/* vhost-user-nvme will not send
	 * set vring addr message, enable
	 * memory address table now.
	 */

	/* 这里dev->is_nvme是false，所以没有调用vhost_setup_mem_table */
	if (dev->has_new_mem_table && dev->is_nvme) {
		vhost_setup_mem_table(dev);
		dev->has_new_mem_table = 0;
	}

	return 0;
}

struct vhost_vring_file {
	unsigned int index;
	int fd;
};


static void
vhost_user_set_vring_call(struct virtio_net *dev, struct VhostUserMsg *pmsg)
{
	struct vhost_vring_file file;
	struct vhost_virtqueue *vq;

	/* Remove from the data plane. */
	if ((dev->flags & VIRTIO_DEV_RUNNING) &&
			!(dev->flags & VIRTIO_DEV_DRAIN)) {
		dev->flags &= ~VIRTIO_DEV_RUNNING;
		dev->notify_ops->destroy_device(dev->vid);
	}

	file.index = pmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
	if (pmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK)
		file.fd = VIRTIO_INVALID_EVENTFD;
	else
		file.fd = pmsg->fds[0];
	RTE_LOG(INFO, VHOST_CONFIG,
		"vring call idx:%d file:%d\n", file.index, file.fd);

	vq = dev->virtqueue[file.index];
	if (vq->callfd >= 0)
		close(vq->callfd);

	vq->callfd = file.fd;
}

const struct vhost_device_ops g_spdk_vhost_ops = {
	.new_device =  start_device,
	.resume_device = resume_device,
	.destroy_device = stop_device,
	.drain_device = drain_device,
	.get_config = get_config,
	.set_config = set_config,
	.new_connection = new_connection,
	.destroy_connection = destroy_connection,
	.vhost_nvme_admin_passthrough = spdk_vhost_nvme_admin_passthrough,
	.vhost_nvme_set_cq_call = spdk_vhost_nvme_set_cq_call,
	.vhost_nvme_get_cap = spdk_vhost_nvme_get_cap,
	.get_inflight_queue_size = get_inflight_queue_size,
	.get_inflight_num = get_inflight_num,
};

static int
start_device(int vid)
{
	struct spdk_vhost_dev *vdev;
	struct spdk_vhost_virtqueue *vq;
	int rc = -1;
	uint16_t i;
	uint64_t addr = 0;
	uint64_t inflight_offset = 0;

	pthread_mutex_lock(&g_spdk_vhost_mutex);

	vdev = spdk_vhost_dev_find_by_vid(vid);


	if (vdev->lcore != -1) {
		SPDK_ERRLOG("Controller %s already loaded.\n", vdev->name);
		goto out;
	}

	if (rte_vhost_get_protocol_features(vid, &vdev->protocol_features) != 0) {
		SPDK_ERRLOG("vhost device %d: Failed to get protocol driver features\n", vid);
		goto out;
	}

	if (rte_vhost_get_inflight_addr(vid, (void **)&addr) != 0) {
		SPDK_ERRLOG("vhost device %d: Failed to get inflight addr\n", vid);
		goto out;
	}

	vdev->max_queues = 0;
	memset(vdev->virtqueue, 0, sizeof(vdev->virtqueue));
	for (i = 0; i < SPDK_VHOST_MAX_VQUEUES; i++) {
		if (vdev->protocol_features & (1ULL << VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD)){
			vdev->virtqueue[i].is_inflight = 1;
		}

		if (rte_vhost_get_vhost_vring(vid, i, &vdev->virtqueue[i].vring)) {
			continue;
		}

		if (vdev->virtqueue[i].vring.desc == NULL ||
		    vdev->virtqueue[i].vring.size == 0) {
			continue;
		}

		vq = &vdev->virtqueue[i];
		inflight_offset = get_inflight_queue_size(vdev->virtqueue[i].vring.size) * i;
		vq->inflight = (struct virtq_inflight *)((char *)addr + inflight_offset);
		vq->inflight->desc_num = vdev->virtqueue[i].vring.size;
		vq->inflight->version = INFLIGHT_VERSION;

		/* Disable notifications. */
		if (rte_vhost_enable_guest_notification(vid, i, 0) != 0) {
			SPDK_ERRLOG("vhost device %d: Failed to disable guest notification on queue %"PRIu16"\n", vid, i);
			goto out;
		}

		vdev->max_queues = i + 1;
	}

	if (rte_vhost_get_negotiated_features(vid, &vdev->negotiated_features) != 0) {
		SPDK_ERRLOG("vhost device %d: Failed to get negotiated driver features\n", vid);
		goto out;
	}

	if (rte_vhost_get_mem_table(vid, &vdev->mem) != 0) {
		SPDK_ERRLOG("vhost device %d: Failed to get guest memory table\n", vid);
		goto out;
	}

	/*
	 * Not sure right now but this look like some kind of QEMU bug and guest IO
	 * might be frozed without kicking all queues after live-migration. This look like
	 * the previous vhost instance failed to effectively deliver all interrupts before
	 * the GET_VRING_BASE message. This shouldn't harm guest since spurious interrupts
	 * should be ignored by guest virtio driver.
	 *
	 * Tested on QEMU 2.10.91 and 2.11.50.
	 */
	for (i = 0; i < vdev->max_queues; i++) {
		if (vdev->virtqueue[i].vring.callfd != -1) {
			eventfd_write(vdev->virtqueue[i].vring.callfd, (eventfd_t)1);
		}

		if(check_queue_inflights(vdev, &vdev->virtqueue[i], vdev->virtqueue[i].vring.kickfd) != 0) {
			SPDK_ERRLOG("vhost device %d: Failed to check inflights for vq: %d\n", vid, i);
		}

	}

	/* 不同的nvem设备会被分配到不同的lcore上，第一个nvme在第一个core，以此类推，
	 * 如果nvme设备多于core。剩下的nvme设备按照相同的规则分别到lcore上，因此
	 * _spdk_vhost_event_send会发送到不同的core上去执行，所以spdk_vhost_blk_start在
	 * 中执行threa_self获得的tid也是对应执行spdk_vhost_blk_start的线程上，不一定是master
	 * 线程
	 */
	vdev->lcore = spdk_vhost_allocate_reactor(vdev->cpumask);
	spdk_vhost_dev_mem_register(vdev);
	/* 将vdev->backend->start_device传递给event，即vhost_blk_device_backend的spdk_vhost_blk_start */
	rc = _spdk_vhost_event_send(vdev, vdev->backend->start_device, 3, "start device");
out:
	pthread_mutex_unlock(&g_spdk_vhost_mutex);
	return rc;
}

static int
get_config(int vid, uint8_t *config, uint32_t len)
{
	struct spdk_vhost_dev *vdev;
	int rc = -1;

	pthread_mutex_lock(&g_spdk_vhost_mutex);
	vdev = spdk_vhost_dev_find_by_vid(vid);
	/* 调用vhost_blk_device_backend的vhost_get_config*/
	if (vdev->backend->vhost_get_config) {
		rc = vdev->backend->vhost_get_config(vdev, config, len);
	}

out:
	pthread_mutex_unlock(&g_spdk_vhost_mutex);
	return rc;
}

static int
spdk_vhost_blk_get_config(struct spdk_vhost_dev *vdev, uint8_t *config,
			  uint32_t len)
{
	struct virtio_blk_config *blkcfg = (struct virtio_blk_config *)config;
	struct spdk_vhost_blk_dev *bvdev;
	struct spdk_bdev *bdev;
	uint32_t blk_size;
	uint64_t blkcnt;

	bvdev = to_blk_dev(vdev);

	bdev = bvdev->bdev;
	blk_size = spdk_bdev_get_block_size(bdev);
	blkcnt = spdk_bdev_get_num_blocks(bdev);

	memset(blkcfg, 0, sizeof(*blkcfg));
	blkcfg->blk_size = blk_size;
	/* minimum I/O size in blocks */
	blkcfg->min_io_size = 1;
	/* expressed in 512 Bytes sectors */
	blkcfg->capacity = (blkcnt * blk_size) / 512;
	blkcfg->size_max = 131072;
	/*  -2 for REQ and RESP and -1 for region boundary splitting */
	blkcfg->seg_max = SPDK_VHOST_IOVS_MAX - 2 - 1;
	/* QEMU can overwrite this value when started */
	blkcfg->num_queues = SPDK_VHOST_MAX_VQUEUES;

	return 0;
}


/*
 * The virtio device sends us the size of the descriptor ring.
 */
static int
vhost_user_set_vring_num(struct virtio_net *dev,
			 VhostUserMsg *msg)
{
	struct vhost_virtqueue *vq = dev->virtqueue[msg->payload.state.index];

	vq->size = msg->payload.state.num;

	if (dev->dequeue_zero_copy) {
		vq->nr_zmbuf = 0;
		vq->last_zmbuf_idx = 0;
		vq->zmbuf_size = vq->size;
		vq->zmbufs = rte_zmalloc(NULL, vq->zmbuf_size * sizeof(struct zcopy_mbuf), 0);
		if (vq->zmbufs == NULL) {
			RTE_LOG(WARNING, VHOST_CONFIG,
				"failed to allocate mem for zero copy; "
				"zero copy is force disabled\n");
			dev->dequeue_zero_copy = 0;
		}
	}

	vq->shadow_used_ring = rte_malloc(NULL,
				vq->size * sizeof(struct vring_used_elem),
				RTE_CACHE_LINE_SIZE);
	if (!vq->shadow_used_ring) {
		RTE_LOG(ERR, VHOST_CONFIG,
			"failed to allocate memory for shadow used ring.\n");
		return -1;
	}

	return 0;
}

/*
 * The virtio device sends us the available ring last used index.
 */
static int
vhost_user_set_vring_base(struct virtio_net *dev,
			  VhostUserMsg *msg)
{
	/* Remove from the data plane. */
	if ((dev->flags & VIRTIO_DEV_RUNNING) &&
			!(dev->flags & VIRTIO_DEV_DRAIN)) {
		dev->flags &= ~VIRTIO_DEV_RUNNING;
		dev->notify_ops->destroy_device(dev->vid);
	}

	dev->virtqueue[msg->payload.state.index]->last_used_idx  = msg->payload.state.num;
	dev->virtqueue[msg->payload.state.index]->last_avail_idx = msg->payload.state.num;

	return 0;
}

/*
 * The virtio device sends us the desc, used and avail ring addresses.
 * This function then converts these to our address space.
 */
static int
vhost_user_set_vring_addr(struct virtio_net *dev, VhostUserMsg *msg)
{
	struct vhost_virtqueue *vq;
	uint64_t len;

	if (dev->has_new_mem_table) {
		vhost_setup_mem_table(dev);
		dev->has_new_mem_table = 0;
	}


	if (dev->mem == NULL)
		return -1;

	/* Remove from the data plane. */
	if ((dev->flags & VIRTIO_DEV_RUNNING) && !(dev->flags & VIRTIO_DEV_DRAIN)) {
		dev->flags &= ~VIRTIO_DEV_RUNNING;
		dev->notify_ops->destroy_device(dev->vid);
	}

	/* addr->index refers to the queue index. The txq 1, rxq is 0. */
	vq = dev->virtqueue[msg->payload.addr.index];

	/* The addresses are converted from QEMU virtual to Vhost virtual. */
	len = sizeof(struct vring_desc) * vq->size;

	vq->desc = (struct vring_desc *)(uintptr_t)qva_to_vva(dev, msg->payload.addr.desc_user_addr, &len);

	if (vq->desc == 0 || len != sizeof(struct vring_desc) * vq->size) {
		RTE_LOG(ERR, VHOST_CONFIG,
			"(%d) failed to map desc ring.\n",
			dev->vid);
		return -1;
	}

	/* numa_realloc:性能优化的一个操作，不用管 */
	dev = numa_realloc(dev, msg->payload.addr.index);
	vq = dev->virtqueue[msg->payload.addr.index];

	len = sizeof(struct vring_avail) + sizeof(uint16_t) * vq->size;

	vq->avail = (struct vring_avail *)(uintptr_t)qva_to_vva(dev, msg->payload.addr.avail_user_addr, &len);
	if (vq->avail == 0 || len != sizeof(struct vring_avail) + sizeof(uint16_t) * vq->size) {
		RTE_LOG(ERR, VHOST_CONFIG, "(%d) failed to find avail ring address.\n", dev->vid);
		return -1;
	}

	len = sizeof(struct vring_used) + sizeof(struct vring_used_elem) * vq->size;
	
	vq->used = (struct vring_used *)(uintptr_t)qva_to_vva(dev, msg->payload.addr.used_user_addr, &len);
	if (vq->used == 0 || len != sizeof(struct vring_used) + sizeof(struct vring_used_elem) * vq->size) {
		RTE_LOG(ERR, VHOST_CONFIG, "(%d) failed to find used ring address.\n", dev->vid);
		return -1;
	}

	if (vq->last_used_idx != vq->used->idx) {
		RTE_LOG(WARNING, VHOST_CONFIG,
			"last_used_idx (%u) and vq->used->idx (%u) mismatches; "
			"some packets maybe resent for Tx and dropped for Rx\n",
			vq->last_used_idx, vq->used->idx);
		vq->last_used_idx  = vq->used->idx;
		vq->last_avail_idx = vq->used->idx;
	}

	vq->log_guest_addr = msg->payload.addr.log_guest_addr;

	VHOST_LOG_DEBUG(VHOST_CONFIG, "(%d) mapped address desc: %p\n",
			dev->vid, vq->desc);
	VHOST_LOG_DEBUG(VHOST_CONFIG, "(%d) mapped address avail: %p\n",
			dev->vid, vq->avail);
	VHOST_LOG_DEBUG(VHOST_CONFIG, "(%d) mapped address used: %p\n",
			dev->vid, vq->used);
	VHOST_LOG_DEBUG(VHOST_CONFIG, "(%d) log_guest_addr: %" PRIx64 "\n",
			dev->vid, vq->log_guest_addr);

	return 0;
}


static int
vhost_setup_mem_table(struct virtio_net *dev)
{
	/*  在VHOST_USER_SET_MEM_TABLE中已经设置 */
	struct VhostUserMemory memory = dev->mem_table;
	struct rte_vhost_mem_region *reg;
	void *mmap_addr;
	uint64_t mmap_size;
	uint64_t mmap_offset;
	uint64_t alignment;
	uint32_t i;
	int fd;

	if (dev->mem) {
		free_mem_region(dev);
		rte_free(dev->mem);
		dev->mem = NULL;
	}

	dev->nr_guest_pages = 0;
	if (!dev->guest_pages) {
		dev->max_guest_pages = 8;
		dev->guest_pages = malloc(dev->max_guest_pages * sizeof(struct guest_page));
	}

	dev->mem = rte_zmalloc("vhost-mem-table", sizeof(struct rte_vhost_memory) +
		sizeof(struct rte_vhost_mem_region) * memory.nregions, 0);

	dev->mem->nregions = memory.nregions;

	/** 打印的log如下
	VHOST_CONFIG: guest memory region 0, size: 0x1f40000000
		guest physical addr: 0x100000000
		guest virtual  addr: 0x7f09c0000000
		host  virtual  addr: 0x2aac40000000
		mmap addr : 0x2aab80000000
		mmap size : 0x2000000000
		mmap align: 0x40000000
		mmap off  : 0xc0000000
	VHOST_CONFIG: guest memory region 1, size: 0xa0000
		guest physical addr: 0x0
		guest virtual  addr: 0x7f0900000000
		host  virtual  addr: 0x2acb80000000
		mmap addr : 0x2acb80000000
		mmap size : 0x40000000
		mmap align: 0x40000000
		mmap off  : 0x0
	*/
	for (i = 0; i < memory.nregions; i++) {
		fd  = dev->mem_table_fds[i];
		reg = &dev->mem->regions[i];
		/* userspace_addr 在guset中的物理地址 */
		reg->guest_phys_addr = memory.regions[i].guest_phys_addr;
		/* userspace_addr 表示geust系统的物理地址在qemu进程中的虚拟地址 */
		reg->guest_user_addr = memory.regions[i].userspace_addr;
		reg->size            = memory.regions[i].memory_size;
		reg->fd              = fd;

		mmap_offset = memory.regions[i].mmap_offset;
		mmap_size   = reg->size + mmap_offset;

		/* mmap() without flag of MAP_ANONYMOUS, should be called
		 * with length argument aligned with hugepagesz at older
		 * longterm version Linux, like 2.6.32 and 3.2.72, or
		 * mmap() will fail with EINVAL.
		 *
		 * to avoid failure, make sure in caller to keep length
		 * aligned.
		 */
		alignment = get_blk_size(fd);
		if (alignment == (uint64_t)-1) {
			RTE_LOG(ERR, VHOST_CONFIG,
				"couldn't get hugepage size through fstat\n");
			goto err_mmap;
		}
		mmap_size = RTE_ALIGN_CEIL(mmap_size, alignment);

		mmap_addr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
				 MAP_SHARED | MAP_POPULATE, fd, 0);


		if (madvise(mmap_addr, mmap_size, MADV_DONTDUMP) != 0) {
			RTE_LOG(INFO, VHOST_CONFIG,
				"MADV_DONTDUMP advice setting failed.\n");
		}

		reg->mmap_addr = mmap_addr;
		reg->mmap_size = mmap_size;
		/* vhost 地址空间的base地址 */
		reg->host_user_addr = (uint64_t)(uintptr_t)mmap_addr + mmap_offset;

		if (dev->dequeue_zero_copy) /* 应该是没有实现 */
			add_guest_pages(dev, reg, alignment);

		RTE_LOG(INFO, VHOST_CONFIG,
			"guest memory region %u, size: 0x%" PRIx64 "\n"
			"\t guest physical addr: 0x%" PRIx64 "\n"
			"\t guest virtual  addr: 0x%" PRIx64 "\n"
			"\t host  virtual  addr: 0x%" PRIx64 "\n"
			"\t mmap addr : 0x%" PRIx64 "\n"
			"\t mmap size : 0x%" PRIx64 "\n"
			"\t mmap align: 0x%" PRIx64 "\n"
			"\t mmap off  : 0x%" PRIx64 "\n",
			i, reg->size,
			reg->guest_phys_addr,
			reg->guest_user_addr,
			reg->host_user_addr,
			(uint64_t)(uintptr_t)mmap_addr,
			mmap_size,
			alignment,
			mmap_offset);
	}

	dump_guest_pages(dev);

	return 0;
}

/*
 * Converts QEMU virtual address to Vhost virtual address. This function is
 * used to convert the ring addresses to our address space.
 */
static uint64_t
qva_to_vva(struct virtio_net *dev, uint64_t qva, uint64_t *len)
{
	struct rte_vhost_mem_region *reg;
	uint32_t i;

	/* Find the region where the address lives. */
	for (i = 0; i < dev->mem->nregions; i++) {
		reg = &dev->mem->regions[i];

		if (qva >= reg->guest_user_addr &&
		    qva <  reg->guest_user_addr + reg->size) {

			if (unlikely(*len > reg->guest_user_addr + reg->size - qva))
				*len = reg->guest_user_addr + reg->size - qva;
			/* host_user_addr是our address space即spdk */
			return qva - reg->guest_user_addr + reg->host_user_addr;
		}
	}

	return 0;
}

int
rte_vhost_get_protocol_features(int vid, uint64_t *features)
{
	struct virtio_net *dev;

	dev = get_device(vid);

	*features = dev->protocol_features;
	return 0;
}

int
rte_vhost_get_negotiated_features(int vid, uint64_t *features)
{
	struct virtio_net *dev;

	dev = get_device(vid);

	*features = dev->negotiated_features;
	return 0;
}

int
rte_vhost_get_mem_table(int vid, struct rte_vhost_memory **mem)
{
	struct virtio_net *dev;
	struct rte_vhost_memory *m;
	size_t size;

	dev = get_device(vid);

	size = dev->mem->nregions * sizeof(struct rte_vhost_mem_region);
	m = malloc(sizeof(struct rte_vhost_memory) + size);

	m->nregions = dev->mem->nregions;
	memcpy(m->regions, dev->mem->regions, size);
	*mem = m;

	return 0;
}

int
rte_vhost_get_inflight_addr(int vid, void **addr)
{
	struct virtio_net *dev;

	dev = get_device(vid);

	*addr = (void *)dev->inflight_addr;

	return 0;
}

int
rte_vhost_get_vhost_vring(int vid, uint16_t vring_idx,
			  struct rte_vhost_vring *vring)
{
	struct virtio_net *dev;
	struct vhost_virtqueue *vq;

	dev = get_device(vid);

	vq = dev->virtqueue[vring_idx];

	vring->desc  = vq->desc;
	vring->avail = vq->avail;
	vring->used  = vq->used;
	vring->log_guest_addr  = vq->log_guest_addr;

	vring->callfd  = vq->callfd;
	vring->kickfd  = vq->kickfd;
	vring->size    = vq->size;

	return 0;
}

static void
vhost_user_set_vring_kick(struct virtio_net *dev, struct VhostUserMsg *pmsg)
{
	struct vhost_vring_file file;
	struct vhost_virtqueue *vq;

	/* Remove from the data plane. */
	if ((dev->flags & VIRTIO_DEV_RUNNING) &&
			!(dev->flags & VIRTIO_DEV_DRAIN)) {
		dev->flags &= ~VIRTIO_DEV_RUNNING;
		dev->notify_ops->destroy_device(dev->vid);
	}

	file.index = pmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
	if (pmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK)
		file.fd = VIRTIO_INVALID_EVENTFD;
	else
		file.fd = pmsg->fds[0];
	RTE_LOG(INFO, VHOST_CONFIG,
		"vring kick idx:%d file:%d\n", file.index, file.fd);

	vq = dev->virtqueue[file.index];
	if (vq->kickfd >= 0)
		close(vq->kickfd);
	vq->kickfd = file.fd;
}

int
rte_vhost_enable_guest_notification(int vid, uint16_t queue_id, int enable)
{
	struct virtio_net *dev = get_device(vid);


	if (enable) {
		RTE_LOG(ERR, VHOST_CONFIG,
			"guest notification isn't supported.\n");
		return -1;
	}

	dev->virtqueue[queue_id]->used->flags = VRING_USED_F_NO_NOTIFY;
	return 0;
}

static uint32_t
spdk_vhost_allocate_reactor(struct spdk_cpuset *cpumask)
{
	uint32_t i, selected_core;
	uint32_t min_ctrlrs;

	min_ctrlrs = INT_MAX;
	selected_core = spdk_env_get_first_core();

	/* 	会选择不同的core，从高到底 */
	SPDK_ENV_FOREACH_CORE(i) {
		if (!spdk_cpuset_get_cpu(cpumask, i)) {
			continue;
		}
		/* g_num_ctrlrs[]一开始都是0 
		 * 确保每个core分配都是一样的
		 * 如果只有core 24和4
		 * 第一次分配到24，则g_num_ctrlrs[selected_core] = 1
		 * 第二次分别的时候min_ctrlrs = g_num_ctrlrs[i] = 1
		 * g_num_ctrlrs[0] = 0 < min_ctrlrs,已没有其他core了，所以
		 * selected_core = 4
		 */
		if (g_num_ctrlrs[i] < min_ctrlrs) {
			selected_core = i;
			min_ctrlrs = g_num_ctrlrs[i];
		}
	}
	/* 表示该core被选中，一个core可以被选中多次应该 */
	g_num_ctrlrs[selected_core]++;
	return selected_core;
}

static void
spdk_vhost_dev_mem_register(struct spdk_vhost_dev *vdev)
{
	struct rte_vhost_mem_region *region;
	uint32_t i;

	for (i = 0; i < vdev->mem->nregions; i++) {
		uint64_t start, end, len;
		region = &vdev->mem->regions[i];
		start = FLOOR_2MB(region->mmap_addr);
		end = CEIL_2MB(region->mmap_addr + region->mmap_size);
		len = end - start;
		SPDK_INFOLOG(SPDK_LOG_VHOST, "Registering VM memory for vtophys translation - 0x%jx len:0x%jx\n",
			     start, len);

		if (spdk_mem_register((void *)start, len) != 0) {
			SPDK_WARNLOG("Failed to register memory region %"PRIu32". Future vtophys translation might fail.\n",
				     i);
			continue;
		}
	}
}

int
spdk_mem_register(void *vaddr, size_t len)
{
	struct spdk_mem_map *map;
	int rc;
	void *seg_vaddr;
	size_t seg_len;

	pthread_mutex_lock(&g_spdk_mem_map_mutex);

	seg_vaddr = vaddr;
	seg_len = 0;
	while (len > 0) {
		uint64_t ref_count;

		/* In g_mem_reg_map, the "translation" is the reference count */
		ref_count = spdk_mem_map_translate(g_mem_reg_map, (uint64_t)vaddr);
		spdk_mem_map_set_translation(g_mem_reg_map, (uint64_t)vaddr, VALUE_2MB, ref_count + 1);

		if (ref_count > 0) {
			if (seg_len > 0) {
				TAILQ_FOREACH(map, &g_spdk_mem_maps, tailq) {
					rc = map->notify_cb(map->cb_ctx, map, SPDK_MEM_MAP_NOTIFY_REGISTER, seg_vaddr, seg_len);
					if (rc != 0) {
						pthread_mutex_unlock(&g_spdk_mem_map_mutex);
						return rc;
					}
				}
			}

			seg_vaddr = vaddr + VALUE_2MB;
			seg_len = 0;
		} else {
			seg_len += VALUE_2MB;
		}

		vaddr += VALUE_2MB;
		len -= VALUE_2MB;
	}

	if (seg_len > 0) {
		TAILQ_FOREACH(map, &g_spdk_mem_maps, tailq) {
			rc = map->notify_cb(map->cb_ctx, map, SPDK_MEM_MAP_NOTIFY_REGISTER, seg_vaddr, seg_len);
			if (rc != 0) {
				pthread_mutex_unlock(&g_spdk_mem_map_mutex);
				return rc;
			}
		}
	}

	pthread_mutex_unlock(&g_spdk_mem_map_mutex);
	return 0;
}

static int
_spdk_vhost_event_send(struct spdk_vhost_dev *vdev, spdk_vhost_event_fn cb_fn,
		       unsigned timeout_sec, const char *errmsg)
{
	struct spdk_vhost_dev_event_ctx ev_ctx = {0};
	struct spdk_event *ev;
	struct timespec timeout;
	int rc;

	rc = sem_init(&ev_ctx.sem, 0, 0);

	ev_ctx.vdev = vdev;
	ev_ctx.cb_fn = cb_fn;
	ev = spdk_event_allocate(vdev->lcore, spdk_vhost_event_cb, &ev_ctx, NULL);

	spdk_event_call(ev);
	pthread_mutex_unlock(&g_spdk_vhost_mutex);

	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += timeout_sec;

	rc = sem_timedwait(&ev_ctx.sem, &timeout);

	sem_destroy(&ev_ctx.sem);
	pthread_mutex_lock(&g_spdk_vhost_mutex);
	return ev_ctx.response;
}

static void
spdk_vhost_event_cb(void *arg1, void *arg2)
{
	struct spdk_vhost_dev_event_ctx *ctx = arg1;

	/* 调用vhost_blk_device_backend的spdk_vhost_blk_start */
	ctx->cb_fn(ctx->vdev, ctx);
}

static struct spdk_vhost_blk_dev *
to_blk_dev(struct spdk_vhost_dev *vdev)
{
	if (vdev == NULL) {
		return NULL;
	}

	if (vdev->backend != &vhost_blk_device_backend) {
		SPDK_ERRLOG("%s: not a vhost-blk device\n", vdev->name);
		return NULL;
	}

	return SPDK_CONTAINEROF(vdev, struct spdk_vhost_blk_dev, vdev);
}

/*  每个nvme设备都会执行该函数 */
static int
spdk_vhost_blk_start(struct spdk_vhost_dev *vdev, void *event_ctx)
{
	struct spdk_vhost_blk_dev *bvdev;
	int i, rc = 0;

	bvdev = to_blk_dev(vdev);

	rc = alloc_task_pool(bvdev);

	if (bvdev->bdev) {
		bvdev->bdev_io_channel = spdk_bdev_get_io_channel(bvdev->bdev_desc);

	}
	/* 注册poller */
	bvdev->requestq_poller = spdk_poller_register(bvdev->bdev ? vdev_worker : no_bdev_vdev_worker,
				 bvdev, 0);
	SPDK_NOTICELOG("Started poller for vhost controller %s on lcore %d\n",
		     vdev->name, vdev->lcore);
out:
	spdk_vhost_dev_backend_event_done(event_ctx, rc);
	return rc;
}

void
spdk_vhost_dev_backend_event_done(void *event_ctx, int response)
{
	struct spdk_vhost_dev_event_ctx *ctx = event_ctx;

	ctx->response = response;
	sem_post(&ctx->sem);
}


static int
alloc_task_pool(struct spdk_vhost_blk_dev *bvdev)
{
	struct spdk_vhost_virtqueue *vq;
	struct spdk_vhost_blk_task *task;
	uint32_t task_cnt;
	uint16_t i;
	uint32_t j;

	for (i = 0; i < bvdev->vdev.max_queues; i++) {
		vq = &bvdev->vdev.virtqueue[i];
		if (vq->vring.desc == NULL) {
			continue;
		}

		task_cnt = vq->vring.size;
	
		vq->tasks = spdk_dma_zmalloc(sizeof(struct spdk_vhost_blk_task) * task_cnt,
					     SPDK_CACHE_LINE_SIZE, NULL);
	

		for (j = 0; j < task_cnt; j++) {
			task = &((struct spdk_vhost_blk_task *)vq->tasks)[j];
			task->bvdev = bvdev;
			task->req_idx = j;
			task->vq = vq;
		}
	}

	return 0;
}

#define __bdev_to_io_dev(bdev)		(((char *)bdev) + 1)
#define __bdev_from_io_dev(io_dev)	((struct spdk_bdev *)(((char *)io_dev) - 1))


struct spdk_io_channel *
spdk_bdev_get_io_channel(struct spdk_bdev_desc *desc)
{
	return spdk_get_io_channel(__bdev_to_io_dev(desc->bdev));
}


struct spdk_io_channel *
spdk_get_io_channel(void *io_device)
{
	struct spdk_io_channel *ch;
	struct spdk_thread *thread;
	struct io_device *dev;
	int rc;

	pthread_mutex_lock(&g_devlist_mutex);
	/* spdk_io_device_register会向g_io_devices添加元素 */
	TAILQ_FOREACH(dev, &g_io_devices, tailq) {
		if (dev->io_device == io_device) {
			break;
		}
	}

	/* 当前进程对应的thread */
	thread = _get_thread();

	TAILQ_FOREACH(ch, &thread->io_channels, tailq) {
		if (ch->dev == dev) {
			ch->ref++;
			/*
			 * An I/O channel already exists for this device on this
			 *  thread, so return it.
			 */
			pthread_mutex_unlock(&g_devlist_mutex);
			return ch;
		}
	}

	/* __bdev_to_io_dev(bdev)的dev->ctx_size位为sizeof(struct spdk_bdev_channel) 
	 * ctrl的 dev->ctx_size为 sizeof(struct nvme_io_channel)
	 * g_bdev_mgr的dev->ctx_size为sizeof(struct spdk_bdev_mgmt_channel)
	 */
	ch = calloc(1, sizeof(*ch) + dev->ctx_size);

	ch->dev = dev;
	ch->destroy_cb = dev->destroy_cb;
	ch->thread = thread;
	ch->ref = 1;
	/* 将ch加入到thread->io_channels */
	TAILQ_INSERT_TAIL(&thread->io_channels, ch, tailq);

	dev->refcnt++;

	pthread_mutex_unlock(&g_devlist_mutex);

	/* 
	 * bdev的io_device create_cb为:spdk_bdev_channel_create,先调用该函数
	 * ctrlr的io_device create_cb为:bdev_nvme_create_cb
	 * g_bdev_mgr io_device create_cb为:spdk_bdev_mgmt_channel_create
	 */
	rc = dev->create_cb(io_device, (uint8_t *)ch + sizeof(*ch));

	return ch;
}

static int
spdk_bdev_channel_create(void *io_device, void *ctx_buf)
{
	struct spdk_bdev		*bdev = __bdev_from_io_dev(io_device);
	struct spdk_bdev_channel	*ch = ctx_buf;

	if (_spdk_bdev_channel_create(ch, io_device) != 0) {
		_spdk_bdev_channel_destroy_resource(ch);
		return -1;
	}

#ifdef SPDK_CONFIG_VTUNE
	{
		char *name;
		__itt_init_ittlib(NULL, 0);
		name = spdk_sprintf_alloc("spdk_bdev_%s_%p", ch->bdev->name, ch);
		if (!name) {
			_spdk_bdev_channel_destroy_resource(ch);
			return -1;
		}
		ch->handle = __itt_string_handle_create(name);
		free(name);
		ch->start_tsc = spdk_get_ticks();
		ch->interval_tsc = spdk_get_ticks_hz() / 100;
	}
#endif

	pthread_mutex_lock(&bdev->mutex);

	if (_spdk_bdev_enable_qos(bdev, ch)) {
		_spdk_bdev_channel_destroy_resource(ch);
		pthread_mutex_unlock(&bdev->mutex);
		return -1;
	}

	bdev->channel_count++;

	pthread_mutex_unlock(&bdev->mutex);

	return 0;
}

static int
_spdk_bdev_channel_create(struct spdk_bdev_channel *ch, void *io_device)
{
	struct spdk_bdev		*bdev = __bdev_from_io_dev(io_device);
	struct spdk_io_channel		*mgmt_io_ch;
	struct spdk_bdev_mgmt_channel	*mgmt_ch;
	struct spdk_bdev_module_channel *module_ch;

	/* spdk_bdev_channel和bdev相关联 */
	ch->bdev = bdev;
	/* nvme_ctrlr_create_bdevs中会给bdev->fn_table赋值nvmelib_fn_table */
	/* spdk_bdev_channel和spdk_io_channel相关联，调用bdev_nvme_get_io_channel */
	ch->channel = bdev->fn_table->get_io_channel(bdev->ctxt);

	mgmt_io_ch = spdk_get_io_channel(&g_bdev_mgr);

	mgmt_ch = spdk_io_channel_get_ctx(mgmt_io_ch);

	TAILQ_FOREACH(module_ch, &mgmt_ch->module_channels, link) {
		if (module_ch->module_ch == ch->channel) {
			spdk_put_io_channel(mgmt_io_ch);
			module_ch->ref++;
			break;
		}
	}

	if (module_ch == NULL) {
		module_ch = calloc(1, sizeof(*module_ch));

		module_ch->mgmt_ch = mgmt_ch;
		module_ch->io_outstanding = 0;
		TAILQ_INIT(&module_ch->nomem_io);
		module_ch->nomem_threshold = 0;
		module_ch->nomem_io_trigger_poller = NULL;
		module_ch->nomem_io_abort = false;
		module_ch->module_ch = ch->channel;
		module_ch->ref = 1;
		TAILQ_INSERT_TAIL(&mgmt_ch->module_channels, module_ch, link);
	}

	memset(&ch->stat, 0, sizeof(ch->stat));
	ch->io_outstanding = 0;
	TAILQ_INIT(&ch->queued_resets);
	ch->flags = 0;
	ch->module_ch = module_ch;

	return 0;
}

static const struct spdk_bdev_fn_table nvmelib_fn_table = {
	.destruct		= bdev_nvme_destruct,
	.submit_request		= bdev_nvme_submit_request,
	.io_type_supported	= bdev_nvme_io_type_supported,
	.get_io_channel		= bdev_nvme_get_io_channel,
	.dump_info_json		= bdev_nvme_dump_info_json,
	.write_config_json	= bdev_nvme_write_config_json,
	.get_spin_time		= bdev_nvme_get_spin_time,
};

static struct spdk_io_channel *
bdev_nvme_get_io_channel(void *ctx)
{
	struct nvme_bdev *nvme_bdev = ctx;

	/* 会调用bdev_nvme_create_cb */
	return spdk_get_io_channel(nvme_bdev->nvme_ctrlr->ctrlr);
}

static int
bdev_nvme_create_cb(void *io_device, void *ctx_buf)
{
	struct spdk_nvme_ctrlr *ctrlr = io_device;
	struct nvme_io_channel *ch = ctx_buf;

#ifdef SPDK_CONFIG_VTUNE
	ch->collect_spin_stat = true;
#else
	ch->collect_spin_stat = false;
#endif

	ch->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ctrlr, NULL, 0);

	ch->poller = spdk_poller_register(bdev_nvme_poll, ch, 0);
	return 0;
}

/* PCIe transport extensions for spdk_nvme_qpair */
struct nvme_pcie_qpair {
	/* Submission queue tail doorbell */
	volatile uint32_t *sq_tdbl;

	/* Completion queue head doorbell */
	volatile uint32_t *cq_hdbl;

	/* Submission queue shadow tail doorbell */
	volatile uint32_t *sq_shadow_tdbl;

	/* Completion queue shadow head doorbell */
	volatile uint32_t *cq_shadow_hdbl;

	/* Submission queue event index */
	volatile uint32_t *sq_eventidx;

	/* Completion queue event index */
	volatile uint32_t *cq_eventidx;

	/* Submission queue */
	struct spdk_nvme_cmd *cmd;

	/* Completion queue */
	struct spdk_nvme_cpl *cpl;

	TAILQ_HEAD(, nvme_tracker) free_tr;
	TAILQ_HEAD(nvme_outstanding_tr_head, nvme_tracker) outstanding_tr;

	/* Array of trackers indexed by command ID. */
	struct nvme_tracker *tr;

	uint16_t num_entries;

	uint16_t max_completions_cap;

	uint16_t sq_tail;
	uint16_t cq_head;
	uint16_t sq_head;

	uint8_t phase;

	bool is_enabled;

	/*
	 * Base qpair structure.
	 * This is located after the hot data in this structure so that the important parts of
	 * nvme_pcie_qpair are in the same cache line.
	 */
	struct spdk_nvme_qpair qpair;

	/*
	 * Fields below this point should not be touched on the normal I/O path.
	 */

	bool sq_in_cmb;

	uint64_t cmd_bus_addr;
	uint64_t cpl_bus_addr;
};


struct spdk_nvme_qpair *
spdk_nvme_ctrlr_alloc_io_qpair(struct spdk_nvme_ctrlr *ctrlr,
			       const struct spdk_nvme_io_qpair_opts *user_opts,
			       size_t opts_size)
{
	uint32_t				qid;
	struct spdk_nvme_qpair			*qpair;
	union spdk_nvme_cc_register		cc;
	struct spdk_nvme_io_qpair_opts		opts;

	/*
	 * Get the default options, then overwrite them with the user-provided options
	 * up to opts_size.
	 *
	 * This allows for extensions of the opts structure without breaking
	 * ABI compatibility.
	 */
	spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr, &opts, sizeof(opts));
	/* user_opts为NULL */
	if (user_opts) {
		memcpy(&opts, user_opts, spdk_min(sizeof(opts), opts_size));
	}

	if (nvme_ctrlr_get_cc(ctrlr, &cc)) {
		SPDK_ERRLOG("get_cc failed\n");
		return NULL;
	}

	/* Only the low 2 bits (values 0, 1, 2, 3) of QPRIO are valid. */
	if ((opts.qprio & 3) != opts.qprio) {
		return NULL;
	}

	/*
	 * Only value SPDK_NVME_QPRIO_URGENT(0) is valid for the
	 * default round robin arbitration method.
	 */
	if ((cc.bits.ams == SPDK_NVME_CC_AMS_RR) && (opts.qprio != SPDK_NVME_QPRIO_URGENT)) {
		SPDK_ERRLOG("invalid queue priority for default round robin arbitration method\n");
		return NULL;
	}

	nvme_robust_mutex_lock(&ctrlr->ctrlr_lock);

	/*
	 * Get the first available I/O queue ID.
	 */
	qid = spdk_bit_array_find_first_set(ctrlr->free_io_qids, 1);

	qpair = nvme_transport_ctrlr_create_io_qpair(ctrlr, qid, &opts);

	spdk_bit_array_clear(ctrlr->free_io_qids, qid);
	/* 将qpair添加到trlr->active_io_qpairs中 */
	TAILQ_INSERT_TAIL(&ctrlr->active_io_qpairs, qpair, tailq);

	nvme_ctrlr_proc_add_io_qpair(qpair);

	nvme_robust_mutex_unlock(&ctrlr->ctrlr_lock);

	if (ctrlr->quirks & NVME_QUIRK_DELAY_AFTER_QUEUE_ALLOC) {
		spdk_delay_us(100);
	}

	return qpair;
}

void
spdk_nvme_ctrlr_get_default_io_qpair_opts(struct spdk_nvme_ctrlr *ctrlr,
		struct spdk_nvme_io_qpair_opts *opts,
		size_t opts_size)
{
	memset(opts, 0, opts_size);

#define FIELD_OK(field) \
	offsetof(struct spdk_nvme_io_qpair_opts, field) + sizeof(opts->field) <= opts_size

	if (FIELD_OK(qprio)) {
		opts->qprio = SPDK_NVME_QPRIO_URGENT;
	}

	if (FIELD_OK(io_queue_size)) {
		opts->io_queue_size = ctrlr->opts.io_queue_size;
	}

	if (FIELD_OK(io_queue_requests)) {
		opts->io_queue_requests = ctrlr->opts.io_queue_requests;
	}

#undef FIELD_OK
}

static int
nvme_ctrlr_get_cc(struct spdk_nvme_ctrlr *ctrlr, union spdk_nvme_cc_register *cc)
{
	return nvme_transport_ctrlr_get_reg_4(ctrlr, offsetof(struct spdk_nvme_registers, cc.raw),
					      &cc->raw);
}

int
nvme_transport_ctrlr_get_reg_4(struct spdk_nvme_ctrlr *ctrlr, uint32_t offset, uint32_t *value)
{
	/* 调用nvme_pcie_ctrlr_get_reg_4 */
	NVME_TRANSPORT_CALL(ctrlr->trid.trtype, ctrlr_get_reg_4, (ctrlr, offset, value));
}


int
nvme_pcie_ctrlr_get_reg_4(struct spdk_nvme_ctrlr *ctrlr, uint32_t offset, uint32_t *value)
{
	struct nvme_pcie_ctrlr *pctrlr = nvme_pcie_ctrlr(ctrlr);

	g_thread_mmio_ctrlr = pctrlr;
	*value = spdk_mmio_read_4(nvme_pcie_reg_addr(ctrlr, offset));
	g_thread_mmio_ctrlr = NULL;
	if (~(*value) == 0) {
		return -1;
	}

	return 0;
}

static volatile void *
nvme_pcie_reg_addr(struct spdk_nvme_ctrlr *ctrlr, uint32_t offset)
{
	struct nvme_pcie_ctrlr *pctrlr = nvme_pcie_ctrlr(ctrlr);

	return (volatile void *)((uintptr_t)pctrlr->regs + offset);
}

struct spdk_nvme_qpair *
nvme_transport_ctrlr_create_io_qpair(struct spdk_nvme_ctrlr *ctrlr, uint16_t qid,
				     const struct spdk_nvme_io_qpair_opts *opts)
{
	/* 调用nvme_pcie_ctrlr_create_io_qpair */
	NVME_TRANSPORT_CALL(ctrlr->trid.trtype, ctrlr_create_io_qpair, (ctrlr, qid, opts));
}

struct spdk_nvme_qpair *
nvme_pcie_ctrlr_create_io_qpair(struct spdk_nvme_ctrlr *ctrlr, uint16_t qid,
				const struct spdk_nvme_io_qpair_opts *opts)
{
	struct nvme_pcie_qpair *pqpair;
	struct spdk_nvme_qpair *qpair;
	int rc;

	pqpair = spdk_dma_zmalloc(sizeof(*pqpair), 64, NULL);

	pqpair->num_entries = opts->io_queue_size;

	qpair = &pqpair->qpair;

	/* 构建io   qpair */
	rc = nvme_qpair_init(qpair, qid, ctrlr, opts->qprio, opts->io_queue_requests);

	rc = nvme_pcie_qpair_construct(qpair);

	rc = _nvme_pcie_ctrlr_create_io_qpair(ctrlr, qpair, qid);

	return qpair;
}

int
nvme_qpair_init(struct spdk_nvme_qpair *qpair, uint16_t id,
		struct spdk_nvme_ctrlr *ctrlr,
		enum spdk_nvme_qprio qprio,
		uint32_t num_requests)
{
	size_t req_size_padded;
	uint32_t i;

	qpair->id = id;
	qpair->qprio = qprio;

	qpair->in_completion_context = 0;
	qpair->delete_after_completion_context = 0;
	qpair->no_deletion_notification_needed = 0;

	qpair->ctrlr = ctrlr;
	qpair->trtype = ctrlr->trid.trtype;

	STAILQ_INIT(&qpair->free_req);
	STAILQ_INIT(&qpair->queued_req);

	req_size_padded = (sizeof(struct nvme_request) + 63) & ~(size_t)63;

	qpair->req_buf = spdk_dma_zmalloc(req_size_padded * num_requests, 64, NULL);

	for (i = 0; i < num_requests; i++) {
		struct nvme_request *req = qpair->req_buf + i * req_size_padded;

		STAILQ_INSERT_HEAD(&qpair->free_req, req, stailq);
	}

	return 0;
}

static int
nvme_pcie_qpair_construct(struct spdk_nvme_qpair *qpair)
{
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;
	struct nvme_pcie_ctrlr	*pctrlr = nvme_pcie_ctrlr(ctrlr);
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_tracker	*tr;
	uint16_t		i;
	volatile uint32_t	*doorbell_base;
	uint64_t		phys_addr = 0;
	uint64_t		offset;
	uint16_t		num_trackers;
	size_t			page_size = sysconf(_SC_PAGESIZE);

	/*
	 * Limit the maximum number of completions to return per call to prevent wraparound,
	 * and calculate how many trackers can be submitted at once without overflowing the
	 * completion queue.
	 */
	pqpair->max_completions_cap = pqpair->num_entries / 4;
	pqpair->max_completions_cap = spdk_max(pqpair->max_completions_cap, NVME_MIN_COMPLETIONS);
	pqpair->max_completions_cap = spdk_min(pqpair->max_completions_cap, NVME_MAX_COMPLETIONS);
	num_trackers = pqpair->num_entries - pqpair->max_completions_cap;

	SPDK_INFOLOG(SPDK_LOG_NVME, "max_completions_cap = %" PRIu16 " num_trackers = %" PRIu16 "\n",
		     pqpair->max_completions_cap, num_trackers);

	pqpair->sq_in_cmb = false;

	/* cmd and cpl rings must be aligned on page size boundaries. */
	if (ctrlr->opts.use_cmb_sqs) {
		if (nvme_pcie_ctrlr_alloc_cmb(ctrlr, pqpair->num_entries * sizeof(struct spdk_nvme_cmd),
					      page_size, &offset) == 0) {
			pqpair->cmd = pctrlr->cmb_bar_virt_addr + offset;
			pqpair->cmd_bus_addr = pctrlr->cmb_bar_phys_addr + offset;
			pqpair->sq_in_cmb = true;
		}
	}
	if (pqpair->sq_in_cmb == false) {
		pqpair->cmd = spdk_dma_zmalloc(pqpair->num_entries * sizeof(struct spdk_nvme_cmd),
					       page_size,
					       &pqpair->cmd_bus_addr);
	}

	pqpair->cpl = spdk_dma_zmalloc(pqpair->num_entries * sizeof(struct spdk_nvme_cpl),
				       page_size,
				       &pqpair->cpl_bus_addr);

	doorbell_base = &pctrlr->regs->doorbell[0].sq_tdbl;
	pqpair->sq_tdbl = doorbell_base + (2 * qpair->id + 0) * pctrlr->doorbell_stride_u32;
	pqpair->cq_hdbl = doorbell_base + (2 * qpair->id + 1) * pctrlr->doorbell_stride_u32;

	/*
	 * Reserve space for all of the trackers in a single allocation.
	 *   struct nvme_tracker must be padded so that its size is already a power of 2.
	 *   This ensures the PRP list embedded in the nvme_tracker object will not span a
	 *   4KB boundary, while allowing access to trackers in tr[] via normal array indexing.
	 */
	pqpair->tr = spdk_dma_zmalloc(num_trackers * sizeof(*tr), sizeof(*tr), &phys_addr);

	TAILQ_INIT(&pqpair->free_tr);
	TAILQ_INIT(&pqpair->outstanding_tr);

	for (i = 0; i < num_trackers; i++) {
		tr = &pqpair->tr[i];
		nvme_qpair_construct_tracker(tr, i, phys_addr);
		TAILQ_INSERT_HEAD(&pqpair->free_tr, tr, tq_list);
		phys_addr += sizeof(struct nvme_tracker);
	}

	nvme_pcie_qpair_reset(qpair);

	return 0;
}

static int
nvme_pcie_ctrlr_alloc_cmb(struct spdk_nvme_ctrlr *ctrlr, uint64_t length, uint64_t aligned,
			  uint64_t *offset)
{
	struct nvme_pcie_ctrlr *pctrlr = nvme_pcie_ctrlr(ctrlr);
	uint64_t round_offset;

	round_offset = pctrlr->cmb_current_offset;
	round_offset = (round_offset + (aligned - 1)) & ~(aligned - 1);

	/* CMB may only consume part of the BAR, calculate accordingly */
	if (round_offset + length > pctrlr->cmb_max_offset) {
		SPDK_ERRLOG("Tried to allocate past valid CMB range!\n");
		return -1;
	}

	*offset = round_offset;
	pctrlr->cmb_current_offset = round_offset + length;

	return 0;
}


int
nvme_pcie_qpair_reset(struct spdk_nvme_qpair *qpair)
{
	struct nvme_pcie_qpair *pqpair = nvme_pcie_qpair(qpair);

	pqpair->sq_tail = pqpair->cq_head = 0;

	/*
	 * First time through the completion queue, HW will set phase
	 *  bit on completions to 1.  So set this to 1 here, indicating
	 *  we're looking for a 1 to know which entries have completed.
	 *  we'll toggle the bit each time when the completion queue
	 *  rolls over.
	 */
	pqpair->phase = 1;

	memset(pqpair->cmd, 0,
	       pqpair->num_entries * sizeof(struct spdk_nvme_cmd));
	memset(pqpair->cpl, 0,
	       pqpair->num_entries * sizeof(struct spdk_nvme_cpl));

	return 0;
}


static int
_nvme_pcie_ctrlr_create_io_qpair(struct spdk_nvme_ctrlr *ctrlr, struct spdk_nvme_qpair *qpair,
				 uint16_t qid)
{
	struct nvme_pcie_ctrlr	*pctrlr = nvme_pcie_ctrlr(ctrlr);
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_completion_poll_status	status;
	int					rc;

	status.done = false;
	rc = nvme_pcie_ctrlr_cmd_create_io_cq(ctrlr, qpair, nvme_completion_poll_cb, &status);

	while (status.done == false) {
		spdk_nvme_qpair_process_completions(ctrlr->adminq, 0);
	}
	if (spdk_nvme_cpl_is_error(&status.cpl)) {
		SPDK_ERRLOG("nvme_create_io_cq failed!\n");
		return -1;
	}

	status.done = false;
	rc = nvme_pcie_ctrlr_cmd_create_io_sq(qpair->ctrlr, qpair, nvme_completion_poll_cb, &status);

	while (status.done == false) {
		spdk_nvme_qpair_process_completions(ctrlr->adminq, 0);
	}
	if (spdk_nvme_cpl_is_error(&status.cpl)) {
		SPDK_ERRLOG("nvme_create_io_sq failed!\n");
		/* Attempt to delete the completion queue */
		status.done = false;
		rc = nvme_pcie_ctrlr_cmd_delete_io_cq(qpair->ctrlr, qpair, nvme_completion_poll_cb, &status);
		if (rc != 0) {
			return -1;
		}
		while (status.done == false) {
			spdk_nvme_qpair_process_completions(ctrlr->adminq, 0);
		}
		return -1;
	}

	if (ctrlr->shadow_doorbell) {
		pqpair->sq_shadow_tdbl = ctrlr->shadow_doorbell + (2 * qpair->id + 0) * pctrlr->doorbell_stride_u32;
		pqpair->cq_shadow_hdbl = ctrlr->shadow_doorbell + (2 * qpair->id + 1) * pctrlr->doorbell_stride_u32;
		pqpair->sq_eventidx = ctrlr->eventidx + (2 * qpair->id + 0) * pctrlr->doorbell_stride_u32;
		pqpair->cq_eventidx = ctrlr->eventidx + (2 * qpair->id + 1) * pctrlr->doorbell_stride_u32;
	}
	nvme_pcie_qpair_reset(qpair);

	return 0;
}

static int
nvme_pcie_ctrlr_cmd_create_io_cq(struct spdk_nvme_ctrlr *ctrlr,
				 struct spdk_nvme_qpair *io_que, spdk_nvme_cmd_cb cb_fn,
				 void *cb_arg)
{
	struct nvme_pcie_qpair *pqpair = nvme_pcie_qpair(io_que);
	struct nvme_request *req;
	struct spdk_nvme_cmd *cmd;

	req = nvme_allocate_request_null(ctrlr->adminq, cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = SPDK_NVME_OPC_CREATE_IO_CQ;

	/*
	 * TODO: create a create io completion queue command data
	 *  structure.
	 */
	cmd->cdw10 = ((pqpair->num_entries - 1) << 16) | io_que->id;
	/*
	 * 0x2 = interrupts enabled
	 * 0x1 = physically contiguous
	 */
	cmd->cdw11 = 0x1;
	cmd->dptr.prp.prp1 = pqpair->cpl_bus_addr;

	return nvme_ctrlr_submit_admin_request(ctrlr, req);
}

struct nvme_request *
nvme_allocate_request_null(struct spdk_nvme_qpair *qpair, spdk_nvme_cmd_cb cb_fn, void *cb_arg)
{
	return nvme_allocate_request_contig(qpair, NULL, 0, cb_fn, cb_arg);
}

struct nvme_request *
nvme_allocate_request_contig(struct spdk_nvme_qpair *qpair,
			     void *buffer, uint32_t payload_size,
			     spdk_nvme_cmd_cb cb_fn, void *cb_arg)
{
	struct nvme_payload payload;

	payload.type = NVME_PAYLOAD_TYPE_CONTIG;
	payload.u.contig = buffer;
	payload.md = NULL;

	return nvme_allocate_request(qpair, &payload, payload_size, cb_fn, cb_arg);
}

struct nvme_request *
nvme_allocate_request(struct spdk_nvme_qpair *qpair,
		      const struct nvme_payload *payload, uint32_t payload_size,
		      spdk_nvme_cmd_cb cb_fn, void *cb_arg)
{
	struct nvme_request *req;

	req = STAILQ_FIRST(&qpair->free_req);
	if (req == NULL) {
		return req;
	}

	STAILQ_REMOVE_HEAD(&qpair->free_req, stailq);

	/*
	 * Only memset up to (but not including) the children
	 *  TAILQ_ENTRY.  children, and following members, are
	 *  only used as part of I/O splitting so we avoid
	 *  memsetting them until it is actually needed.
	 *  They will be initialized in nvme_request_add_child()
	 *  if the request is split.
	 */
	memset(req, 0, offsetof(struct nvme_request, children));
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
	req->payload = *payload;
	req->payload_size = payload_size;
	req->qpair = qpair;
	req->pid = g_pid;

	return req;
}

int
nvme_ctrlr_submit_admin_request(struct spdk_nvme_ctrlr *ctrlr,
				struct nvme_request *req)
{
	return nvme_qpair_submit_request(ctrlr->adminq, req);
}

int
nvme_qpair_submit_request(struct spdk_nvme_qpair *qpair, struct nvme_request *req)
{
	int			rc = 0;
	struct nvme_request	*child_req, *tmp;
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;
	bool			child_req_failed = false;

	if (ctrlr->is_failed) {
		nvme_free_request(req);
		return -ENXIO;
	}

	if (req->num_children) {
		/*
		 * This is a split (parent) request. Submit all of the children but not the parent
		 * request itself, since the parent is the original unsplit request.
		 */
		TAILQ_FOREACH_SAFE(child_req, &req->children, child_tailq, tmp) {
			if (!child_req_failed) {
				rc = nvme_qpair_submit_request(qpair, child_req);
				if (rc != 0) {
					child_req_failed = true;
				}
			} else { /* free remaining child_reqs since one child_req fails */
				nvme_request_remove_child(req, child_req);
				nvme_free_request(child_req);
			}
		}

		return rc;
	}

	return nvme_transport_qpair_submit_request(qpair, req);
}

int
nvme_transport_qpair_submit_request(struct spdk_nvme_qpair *qpair, struct nvme_request *req)
{
	/* 调用nvme_pcie_qpair_submit_request */
	NVME_TRANSPORT_CALL(qpair->trtype, qpair_submit_request, (qpair, req));
}

int
nvme_pcie_qpair_submit_request(struct spdk_nvme_qpair *qpair, struct nvme_request *req)
{
	struct nvme_tracker	*tr;
	int			rc = 0;
	void			*md_payload;
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);

	nvme_pcie_qpair_check_enabled(qpair);

	if (nvme_qpair_is_admin_queue(qpair)) {
		nvme_robust_mutex_lock(&ctrlr->ctrlr_lock);
	}

	tr = TAILQ_FIRST(&pqpair->free_tr);

	if (tr == NULL || !pqpair->is_enabled) {
		/*
		 * No tracker is available, or the qpair is disabled due to
		 *  an in-progress controller-level reset.
		 *
		 * Put the request on the qpair's request queue to be
		 *  processed when a tracker frees up via a command
		 *  completion or when the controller reset is
		 *  completed.
		 */
		STAILQ_INSERT_TAIL(&qpair->queued_req, req, stailq);
		goto exit;
	}

	TAILQ_REMOVE(&pqpair->free_tr, tr, tq_list); /* remove tr from free_tr */
	TAILQ_INSERT_TAIL(&pqpair->outstanding_tr, tr, tq_list);
	tr->req = req;
	req->cmd.cid = tr->cid;

	if (req->payload_size && req->payload.md) {
		md_payload = req->payload.md + req->md_offset;
		tr->req->cmd.mptr = spdk_vtophys(md_payload);
		if (tr->req->cmd.mptr == SPDK_VTOPHYS_ERROR) {
			nvme_pcie_fail_request_bad_vtophys(qpair, tr);
			rc = -EINVAL;
			goto exit;
		}
	}
	/* 构建数据 */
	if (req->payload_size == 0) {
		/* Null payload - leave PRP fields zeroed */
		rc = 0;
	} else if (req->payload.type == NVME_PAYLOAD_TYPE_CONTIG) {
		rc = nvme_pcie_qpair_build_contig_request(qpair, req, tr);
	} else if (req->payload.type == NVME_PAYLOAD_TYPE_SGL) {
		if (ctrlr->flags & SPDK_NVME_CTRLR_SGL_SUPPORTED) {
			rc = nvme_pcie_qpair_build_hw_sgl_request(qpair, req, tr);
		} else {
			rc = nvme_pcie_qpair_build_prps_sgl_request(qpair, req, tr);
		}
	} else {
		nvme_pcie_fail_request_bad_vtophys(qpair, tr);
		rc = -EINVAL;
	}

	if (rc < 0) {
		goto exit;
	}

	/* 更新寄存器 */
	nvme_pcie_qpair_submit_tracker(qpair, tr);

exit:
	if (nvme_qpair_is_admin_queue(qpair)) {
		nvme_robust_mutex_unlock(&ctrlr->ctrlr_lock);
	}

	return rc;
}

static void
nvme_pcie_qpair_submit_tracker(struct spdk_nvme_qpair *qpair, struct nvme_tracker *tr)
{
	struct nvme_request	*req;
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_pcie_ctrlr	*pctrlr = nvme_pcie_ctrlr(qpair->ctrlr);

	tr->timed_out = 0;
	if (spdk_unlikely(qpair->active_proc && qpair->active_proc->timeout_cb_fn != NULL)) {
		tr->submit_tick = spdk_get_ticks();
	}

	req = tr->req;
	pqpair->tr[tr->cid].active = true;

	/* Copy the command from the tracker to the submission queue. */
	nvme_pcie_copy_command(&pqpair->cmd[pqpair->sq_tail], &req->cmd);

	if (++pqpair->sq_tail == pqpair->num_entries) {
		pqpair->sq_tail = 0;
	}

	if (pqpair->sq_tail == pqpair->sq_head) {
		SPDK_ERRLOG("sq_tail is passing sq_head!\n");
	}

	spdk_wmb();
	g_thread_mmio_ctrlr = pctrlr;
	if (spdk_likely(nvme_pcie_qpair_update_mmio_required(qpair,
			pqpair->sq_tail,
			pqpair->sq_shadow_tdbl,
			pqpair->sq_eventidx))) {
		/* 就是更新寄存器 */
		spdk_mmio_write_4(pqpair->sq_tdbl, pqpair->sq_tail);
	}
	g_thread_mmio_ctrlr = NULL;
}

/**
 * This function will be called when the process allocates the IO qpair.
 * Note: the ctrlr_lock must be held when calling this function.
 */
static void
nvme_ctrlr_proc_add_io_qpair(struct spdk_nvme_qpair *qpair)
{
	struct spdk_nvme_ctrlr_process	*active_proc;
	struct spdk_nvme_ctrlr		*ctrlr = qpair->ctrlr;
	pid_t				pid = getpid();

	/* nvme_ctrlr_add_process将ctrlr_proc，即active_proc，添加到ctrlr->active_procs中
	 * struct spdk_nvme_ctrlr		*ctrlr只有一个，每一个nvme设备会有一个
	 * struct spdk_nvme_ctrlr_process	*active_proc，这里面的allocated_io_qpairs
	 * 表示要处理的qpairs
	 */
	TAILQ_FOREACH(active_proc, &ctrlr->active_procs, tailq) {
		if (active_proc->pid == pid) {
			/* qpair添加到active_proc->allocated_io_qpairs中 */
			TAILQ_INSERT_TAIL(&active_proc->allocated_io_qpairs, qpair, per_process_tailq);
			qpair->active_proc = active_proc;
			break;
		}
	}
}


static int
bdev_nvme_poll(void *arg)
{
	struct nvme_io_channel *ch = arg;
	int32_t num_completions;


	if (ch->collect_spin_stat && ch->start_ticks == 0) {
		ch->start_ticks = spdk_get_ticks();
	}

	num_completions = spdk_nvme_qpair_process_completions(ch->qpair, 0);

	if (ch->collect_spin_stat) {
		if (num_completions > 0) {
			if (ch->end_ticks != 0) {
				ch->spin_ticks += (ch->end_ticks - ch->start_ticks);
				ch->end_ticks = 0;
			}
			ch->start_ticks = 0;
		} else {
			ch->end_ticks = spdk_get_ticks();
		}
	}

	return num_completions;
}

static int
spdk_bdev_mgmt_channel_create(void *io_device, void *ctx_buf)
{
	struct spdk_bdev_mgmt_channel *ch = ctx_buf;

	STAILQ_INIT(&ch->need_buf_small);
	STAILQ_INIT(&ch->need_buf_large);

	STAILQ_INIT(&ch->per_thread_cache);
	ch->per_thread_cache_count = 0;

	TAILQ_INIT(&ch->module_channels);

	return 0;
}

int32_t
spdk_nvme_qpair_process_completions(struct spdk_nvme_qpair *qpair, uint32_t max_completions)
{
	int32_t ret;

	if (qpair->ctrlr->is_failed) {
		nvme_qpair_fail(qpair);
		return 0;
	}

	qpair->in_completion_context = 1;
	ret = nvme_transport_qpair_process_completions(qpair, max_completions);
	qpair->in_completion_context = 0;
	if (qpair->delete_after_completion_context) {
		/*
		 * A request to delete this qpair was made in the context of this completion
		 *  routine - so it is safe to delete it now.
		 */
		spdk_nvme_ctrlr_free_io_qpair(qpair);
	}
	return ret;
}

int32_t
nvme_transport_qpair_process_completions(struct spdk_nvme_qpair *qpair, uint32_t max_completions)
{
	/* 调用nvme_pcie_qpair_process_completions */
	NVME_TRANSPORT_CALL(qpair->trtype, qpair_process_completions, (qpair, max_completions));
}

int32_t
nvme_pcie_qpair_process_completions(struct spdk_nvme_qpair *qpair, uint32_t max_completions)
{
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_pcie_ctrlr	*pctrlr = nvme_pcie_ctrlr(qpair->ctrlr);
	struct nvme_tracker	*tr;
	struct spdk_nvme_cpl	*cpl;
	uint32_t		 num_completions = 0;
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;

	if (spdk_unlikely(!nvme_pcie_qpair_check_enabled(qpair))) {
		/*
		 * qpair is not enabled, likely because a controller reset is
		 *  is in progress.  Ignore the interrupt - any I/O that was
		 *  associated with this interrupt will get retried when the
		 *  reset is complete.
		 */
		return 0;
	}

	if (spdk_unlikely(nvme_qpair_is_admin_queue(qpair))) {
		nvme_robust_mutex_lock(&ctrlr->ctrlr_lock);
	}

	if (max_completions == 0 || max_completions > pqpair->max_completions_cap) {
		/*
		 * max_completions == 0 means unlimited, but complete at most
		 * max_completions_cap batch of I/O at a time so that the completion
		 * queue doorbells don't wrap around.
		 */
		max_completions = pqpair->max_completions_cap;
	}

	while (1) {
		cpl = &pqpair->cpl[pqpair->cq_head];

		if (cpl->status.p != pqpair->phase) {
			break;
		}

		tr = &pqpair->tr[cpl->cid];
		pqpair->sq_head = cpl->sqhd;

		if (tr->active) {
			nvme_pcie_qpair_complete_tracker(qpair, tr, cpl, true);
		} else {
			SPDK_ERRLOG("cpl does not map to outstanding cmd\n");
			nvme_qpair_print_completion(qpair, cpl);
			assert(0);
		}

		if (spdk_unlikely(++pqpair->cq_head == pqpair->num_entries)) {
			pqpair->cq_head = 0;
			pqpair->phase = !pqpair->phase;
		}

		if (++num_completions == max_completions) {
			break;
		}
	}

	if (num_completions > 0) {
		g_thread_mmio_ctrlr = pctrlr;
		if (spdk_likely(nvme_pcie_qpair_update_mmio_required(qpair, pqpair->cq_head,
				pqpair->cq_shadow_hdbl,
				pqpair->cq_eventidx))) {
			spdk_mmio_write_4(pqpair->cq_hdbl, pqpair->cq_head);
		}
		g_thread_mmio_ctrlr = NULL;
	}

	/* We don't want to expose the admin queue to the user,
	 * so when we're timing out admin commands set the
	 * qpair to NULL.
	 */
	if (!nvme_qpair_is_admin_queue(qpair) && spdk_unlikely(qpair->active_proc->timeout_cb_fn != NULL) &&
	    qpair->ctrlr->state == NVME_CTRLR_STATE_READY) {
		/*
		 * User registered for timeout callback
		 */
		nvme_pcie_qpair_check_timeout(qpair);
	}

	/* Before returning, complete any pending admin request. */
	if (spdk_unlikely(nvme_qpair_is_admin_queue(qpair))) {
		nvme_pcie_qpair_complete_pending_admin_request(qpair);

		nvme_robust_mutex_unlock(&ctrlr->ctrlr_lock);
	}

	return num_completions;
}

static void
nvme_pcie_qpair_complete_tracker(struct spdk_nvme_qpair *qpair, struct nvme_tracker *tr,
				 struct spdk_nvme_cpl *cpl, bool print_on_error)
{
	struct nvme_pcie_qpair		*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_request		*req;
	bool				retry, error, was_active;
	bool				req_from_current_proc = true;

	req = tr->req;

	assert(req != NULL);

	error = spdk_nvme_cpl_is_error(cpl);
	retry = error && nvme_completion_is_retry(cpl) &&
		req->retries < spdk_nvme_retry_count;

	if (error && print_on_error) {
		nvme_qpair_print_command(qpair, &req->cmd);
		nvme_qpair_print_completion(qpair, cpl);
	}

	was_active = pqpair->tr[cpl->cid].active;
	pqpair->tr[cpl->cid].active = false;

	assert(cpl->cid == req->cmd.cid);

	if (retry) {
		req->retries++;
		nvme_pcie_qpair_submit_tracker(qpair, tr);
	} else {
		if (was_active) {
			/* Only check admin requests from different processes. */
			if (nvme_qpair_is_admin_queue(qpair) && req->pid != getpid()) {
				req_from_current_proc = false;
				nvme_pcie_qpair_insert_pending_admin_request(qpair, req, cpl);
			} else {
				/* 调用回调函数nvme_completion_poll_cb */
				if (req->cb_fn) {
					req->cb_fn(req->cb_arg, cpl);
				}
			}
		}

		if (req_from_current_proc == true) {
			nvme_free_request(req);
		}

		tr->req = NULL;

		TAILQ_REMOVE(&pqpair->outstanding_tr, tr, tq_list);
		TAILQ_INSERT_HEAD(&pqpair->free_tr, tr, tq_list);

		/*
		 * If the controller is in the middle of resetting, don't
		 *  try to submit queued requests here - let the reset logic
		 *  handle that instead.
		 */
		if (!STAILQ_EMPTY(&qpair->queued_req) &&
		    !qpair->ctrlr->is_resetting) {
			req = STAILQ_FIRST(&qpair->queued_req);
			STAILQ_REMOVE_HEAD(&qpair->queued_req, stailq);
			nvme_qpair_submit_request(qpair, req);
		}
	}
}

void
nvme_completion_poll_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
	struct nvme_completion_poll_status	*status = arg;

	/*
	 * Copy status into the argument passed by the caller, so that
	 *  the caller can check the status to determine if the
	 *  the request passed or failed.
	 */
	memcpy(&status->cpl, cpl, sizeof(*cpl));
	status->done = true;
}

static int
vdev_worker(void *arg)
{
	struct spdk_vhost_blk_dev *bvdev = arg;
	uint16_t q_idx;

	for (q_idx = 0; q_idx < bvdev->vdev.max_queues; q_idx++) {
		process_vq(bvdev, &bvdev->vdev.virtqueue[q_idx]);
	}

	spdk_vhost_dev_used_signal(&bvdev->vdev);

	return -1;
}

static void
process_vq(struct spdk_vhost_blk_dev *bvdev, struct spdk_vhost_virtqueue *vq)
{
	struct spdk_vhost_blk_task *task;
	int rc;
	uint16_t reqs[32];
	uint16_t reqs_cnt, i;

	reqs_cnt = spdk_vhost_vq_avail_ring_get(vq, reqs, SPDK_COUNTOF(reqs));
	if (!reqs_cnt) {
		return;
	}

	for (i = 0; i < reqs_cnt; i++) {
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "====== Starting processing request idx %"PRIu16"======\n",
			      reqs[i]);

		if (spdk_unlikely(reqs[i] >= vq->vring.size)) {
			SPDK_ERRLOG("%s: request idx '%"PRIu16"' exceeds virtqueue size (%"PRIu16").\n",
				    bvdev->vdev.name, reqs[i], vq->vring.size);
			spdk_vhost_vq_used_ring_enqueue(&bvdev->vdev, vq, reqs[i], 0);
			continue;
		}

		task = &((struct spdk_vhost_blk_task *)vq->tasks)[reqs[i]];
		if (spdk_unlikely(task->used)) {
			SPDK_ERRLOG("%s: request with idx '%"PRIu16"' is already pending.\n",
				    bvdev->vdev.name, reqs[i]);
			spdk_vhost_vq_used_ring_enqueue(&bvdev->vdev, vq, reqs[i], 0);
			continue;
		}

		bvdev->vdev.task_cnt++;

		task->used = true;
		task->iovcnt = SPDK_COUNTOF(task->iovs);
		task->status = NULL;
		task->used_len = 0;
		task->aligned_vector = NULL;

		rc = process_blk_request(task, bvdev, vq);
		if (rc == 0) {
			SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "====== Task %p req_idx %d submitted ======\n", task,
				      reqs[i]);
		} else {
			SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "====== Task %p req_idx %d failed ======\n", task, reqs[i]);
		}
	}
}

static int
process_blk_request(struct spdk_vhost_blk_task *task, struct spdk_vhost_blk_dev *bvdev,
		    struct spdk_vhost_virtqueue *vq)
{
	const struct virtio_blk_outhdr *req;
	struct iovec *iov;
	struct iovec *data_iov = NULL;
	int iov_cnt;
	uint32_t type;
	uint32_t payload_len;
	int rc;

	if (blk_iovs_setup(&bvdev->vdev, vq, task->req_idx, task->iovs, &task->iovcnt, &payload_len)) {
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Invalid request (req_idx = %"PRIu16").\n", task->req_idx);
		/* Only READ and WRITE are supported for now. */
		invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
		return -1;
	}

	iov = &task->iovs[0];
	if (spdk_unlikely(iov->iov_len != sizeof(*req))) {
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK,
			      "First descriptor size is %zu but expected %zu (req_idx = %"PRIu16").\n",
			      iov->iov_len, sizeof(*req), task->req_idx);
		invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
		return -1;
	}

	req = iov->iov_base;

	iov = &task->iovs[task->iovcnt - 1];
	if (spdk_unlikely(iov->iov_len != 1)) {
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK,
			      "Last descriptor size is %zu but expected %d (req_idx = %"PRIu16").\n",
			      iov->iov_len, 1, task->req_idx);
		invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
		return -1;
	}

	task->status = iov->iov_base;
	payload_len -= sizeof(*req) + sizeof(*task->status);
	task->iovcnt -= 2;

	type = req->type;
#ifdef VIRTIO_BLK_T_BARRIER
	/* Don't care about barier for now (as QEMU's virtio-blk do). */
	type &= ~VIRTIO_BLK_T_BARRIER;
#endif

	switch (type) {
	case VIRTIO_BLK_T_IN:
	case VIRTIO_BLK_T_OUT:
		if (spdk_unlikely((payload_len & (512 - 1)) != 0)) {
			SPDK_ERRLOG("%s - passed IO buffer is not multiple of 512b (req_idx = %"PRIu16").\n",
				    type ? "WRITE" : "READ", task->req_idx);
			invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
			return -1;
		}

		if (is_vector_aligned(&task->iovs[1], task->iovcnt)) {
			data_iov = &task->iovs[1];
			iov_cnt = task->iovcnt;
		} else {
			struct aligned_vector *aligned_iov;

			if (get_vector_size(&task->iovs[1], task->iovcnt) != payload_len) {
				SPDK_ERRLOG("task io vector size mismatch.\n");
				invalid_blk_request(task, VIRTIO_BLK_S_IOERR);
				return -1;
			}

			aligned_iov = malloc_aligned_vector(payload_len);


			task->aligned_vector = aligned_iov;
			data_iov = aligned_iov->iov;
			iov_cnt = aligned_iov->iov_cnt;

			/* write operation, copy data before passthrough */
			if (type == VIRTIO_BLK_T_OUT) {
				copy_vector(data_iov, &task->iovs[1], payload_len);
			}
		}

		if (type == VIRTIO_BLK_T_IN) {
			task->used_len = payload_len + sizeof(*task->status);
			rc = spdk_bdev_readv(bvdev->bdev_desc, bvdev->bdev_io_channel,
					     data_iov, iov_cnt, req->sector * 512,
					     payload_len, blk_request_complete_cb, task);
		} else if (!bvdev->readonly) {
			task->used_len = sizeof(*task->status);
			rc = spdk_bdev_writev(bvdev->bdev_desc, bvdev->bdev_io_channel,
					      data_iov, iov_cnt, req->sector * 512,
					      payload_len, blk_request_complete_cb, task);
		} else {
			SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Device is in read-only mode!\n");
			rc = -1;
		}

		if (rc) {
			invalid_blk_request(task, VIRTIO_BLK_S_IOERR);
			return -1;
		}
		break;
	case VIRTIO_BLK_T_GET_ID:
		if (!task->iovcnt || !payload_len) {
			invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
			return -1;
		}
		task->used_len = spdk_min((size_t)VIRTIO_BLK_ID_BYTES, task->iovs[1].iov_len);
		spdk_strcpy_pad(task->iovs[1].iov_base, spdk_bdev_get_product_name(bvdev->bdev),
				task->used_len, ' ');
		blk_request_finish(true, task);
		break;
	default:
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Not supported request type '%"PRIu32"'.\n", type);
		invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
		return -1;
	}

	return 0;
}

int
spdk_bdev_readv(struct spdk_bdev_desc *desc, struct spdk_io_channel *ch,
		struct iovec *iov, int iovcnt,
		uint64_t offset, uint64_t nbytes,
		spdk_bdev_io_completion_cb cb, void *cb_arg)
{
	uint64_t offset_blocks, num_blocks;

	if (spdk_bdev_bytes_to_blocks(desc->bdev, offset, &offset_blocks, nbytes, &num_blocks) != 0) {
		return -EINVAL;
	}

	return spdk_bdev_readv_blocks(desc, ch, iov, iovcnt, offset_blocks, num_blocks, cb, cb_arg);
}


static void
blk_request_complete_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
	struct spdk_vhost_blk_task *task = cb_arg;
	struct aligned_vector *aligned_iov = task->aligned_vector;

	if (aligned_iov) {
		struct iovec *iov;
		struct virtio_blk_outhdr *req;

		iov = &task->iovs[0];
		req = iov->iov_base;

		/* read operation, copy data back to src vector buffer */
		if (req->type == VIRTIO_BLK_T_IN) {
			copy_vector(&task->iovs[1], aligned_iov->iov, aligned_iov->size);
		}

		free_vector(task->aligned_vector);
		task->aligned_vector = NULL;
	}

	spdk_bdev_free_io(bdev_io);
	blk_request_finish(success, task);
}

static int
vhost_user_nvme_set_cq_call(struct virtio_net *dev, uint16_t qid, int fd)
{
	if (dev->notify_ops->vhost_nvme_set_cq_call) {
		return dev->notify_ops->vhost_nvme_set_cq_call(dev->vid, qid, fd);
	}

	return -1;
}

static uint64_t
spdk_bdev_bytes_to_blocks(struct spdk_bdev *bdev, uint64_t offset_bytes, uint64_t *offset_blocks,
			  uint64_t num_bytes, uint64_t *num_blocks)
{
	uint32_t block_size = bdev->blocklen;

	*offset_blocks = offset_bytes / block_size;
	*num_blocks = num_bytes / block_size;

	return (offset_bytes % block_size) | (num_bytes % block_size);
}

int spdk_bdev_readv_blocks(struct spdk_bdev_desc *desc, struct spdk_io_channel *ch,
			   struct iovec *iov, int iovcnt,
			   uint64_t offset_blocks, uint64_t num_blocks,
			   spdk_bdev_io_completion_cb cb, void *cb_arg)
{
	struct spdk_bdev *bdev = desc->bdev;
	struct spdk_bdev_io *bdev_io;
	struct spdk_bdev_channel *channel = spdk_io_channel_get_ctx(ch);

	if (!spdk_bdev_io_valid_blocks(bdev, offset_blocks, num_blocks)) {
		return -EINVAL;
	}

	bdev_io = spdk_bdev_get_io(channel);

	bdev_io->ch = channel;
	bdev_io->type = SPDK_BDEV_IO_TYPE_READ;
	bdev_io->u.bdev.iovs = iov;
	bdev_io->u.bdev.iovcnt = iovcnt;
	bdev_io->u.bdev.num_blocks = num_blocks;
	bdev_io->u.bdev.offset_blocks = offset_blocks;
	spdk_bdev_io_init(bdev_io, bdev, cb_arg, cb);

	spdk_bdev_io_submit(bdev_io);
	return 0;
}

static struct spdk_bdev_io *
spdk_bdev_get_io(struct spdk_bdev_channel *channel)
{
	struct spdk_bdev_mgmt_channel *ch = channel->module_ch->mgmt_ch;
	struct spdk_bdev_io *bdev_io;

	if (ch->per_thread_cache_count > 0) {
		bdev_io = STAILQ_FIRST(&ch->per_thread_cache);
		STAILQ_REMOVE_HEAD(&ch->per_thread_cache, buf_link);
		ch->per_thread_cache_count--;
	} else {
		bdev_io = spdk_mempool_get(g_bdev_mgr.bdev_io_pool);
		if (!bdev_io) {
			SPDK_ERRLOG("Unable to get spdk_bdev_io\n");
			return NULL;
		}
	}

	return bdev_io;
}

static void
spdk_bdev_io_init(struct spdk_bdev_io *bdev_io,
		  struct spdk_bdev *bdev, void *cb_arg,
		  spdk_bdev_io_completion_cb cb)
{
	bdev_io->bdev = bdev;
	bdev_io->caller_ctx = cb_arg;
	bdev_io->cb = cb;
	bdev_io->status = SPDK_BDEV_IO_STATUS_PENDING;
	bdev_io->in_submit_request = false;
	bdev_io->buf = NULL;
	bdev_io->io_submit_ch = NULL;
	memset(bdev_io->driver_ctx, 0, g_bdev_mgr.max_ctx_size);
}

static void
spdk_bdev_io_submit(struct spdk_bdev_io *bdev_io)
{
	struct spdk_bdev *bdev = bdev_io->bdev;

	assert(bdev_io->status == SPDK_BDEV_IO_STATUS_PENDING);

	if (bdev_io->ch->flags & BDEV_CH_QOS_ENABLED) {
		bdev_io->io_submit_ch = bdev_io->ch;
		bdev_io->ch = bdev->qos.ch;
		spdk_thread_send_msg(bdev->qos.thread, _spdk_bdev_io_submit, bdev_io);
	} else {
		_spdk_bdev_io_submit(bdev_io);
	}
}

static void
_spdk_bdev_io_submit(void *ctx)
{
	struct spdk_bdev_io *bdev_io = ctx;
	struct spdk_bdev *bdev = bdev_io->bdev;
	struct spdk_bdev_channel *bdev_ch = bdev_io->ch;
	struct spdk_io_channel *ch = bdev_ch->channel;
	struct spdk_bdev_module_channel	*module_ch = bdev_ch->module_ch;

	bdev_io->submit_tsc = spdk_get_ticks();
	bdev_ch->io_outstanding++;
	module_ch->io_outstanding++;
	bdev_io->in_submit_request = true;
	if (spdk_likely(bdev_ch->flags == 0)) {
		if (spdk_likely(TAILQ_EMPTY(&module_ch->nomem_io))) {
			/* 调用 fn_table的bdev_nvme_submit_request*/
			bdev->fn_table->submit_request(ch, bdev_io);
		} else {
			bdev_ch->io_outstanding--;
			module_ch->io_outstanding--;
			TAILQ_INSERT_TAIL(&module_ch->nomem_io, bdev_io, link);
		}
	} else if (bdev_ch->flags & BDEV_CH_RESET_IN_PROGRESS) {
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
	} else if (bdev_ch->flags & BDEV_CH_QOS_ENABLED) {
		bdev_ch->io_outstanding--;
		module_ch->io_outstanding--;
		TAILQ_INSERT_TAIL(&bdev->qos.queued, bdev_io, link);
		_spdk_bdev_qos_io_submit(bdev_ch);
	} else {
		SPDK_ERRLOG("unknown bdev_ch flag %x found\n", bdev_ch->flags);
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
	}
	bdev_io->in_submit_request = false;
}

static void
bdev_nvme_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
	int rc = _bdev_nvme_submit_request(ch, bdev_io);

	if (spdk_unlikely(rc != 0)) {
		if (rc == -ENOMEM) {
			spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_NOMEM);
		} else {
			spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
		}
	}
}

static int
_bdev_nvme_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
	struct nvme_io_channel *nvme_ch = spdk_io_channel_get_ctx(ch);
	if (nvme_ch->qpair == NULL) {
		/* The device is currently resetting */
		return -1;
	}

	switch (bdev_io->type) {
	case SPDK_BDEV_IO_TYPE_READ:
		spdk_bdev_io_get_buf(bdev_io, bdev_nvme_get_buf_cb,
				     bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen);
		return 0;

	case SPDK_BDEV_IO_TYPE_WRITE:
		return bdev_nvme_writev((struct nvme_bdev *)bdev_io->bdev->ctxt,
					ch,
					(struct nvme_bdev_io *)bdev_io->driver_ctx,
					bdev_io->u.bdev.iovs,
					bdev_io->u.bdev.iovcnt,
					bdev_io->u.bdev.num_blocks,
					bdev_io->u.bdev.offset_blocks);

	case SPDK_BDEV_IO_TYPE_WRITE_ZEROES:
		return bdev_nvme_unmap((struct nvme_bdev *)bdev_io->bdev->ctxt,
				       ch,
				       (struct nvme_bdev_io *)bdev_io->driver_ctx,
				       bdev_io->u.bdev.offset_blocks,
				       bdev_io->u.bdev.num_blocks);

	case SPDK_BDEV_IO_TYPE_UNMAP:
		return bdev_nvme_unmap((struct nvme_bdev *)bdev_io->bdev->ctxt,
				       ch,
				       (struct nvme_bdev_io *)bdev_io->driver_ctx,
				       bdev_io->u.bdev.offset_blocks,
				       bdev_io->u.bdev.num_blocks);

	case SPDK_BDEV_IO_TYPE_RESET:
		return bdev_nvme_reset((struct nvme_bdev *)bdev_io->bdev->ctxt,
				       (struct nvme_bdev_io *)bdev_io->driver_ctx);

	case SPDK_BDEV_IO_TYPE_FLUSH:
		return bdev_nvme_flush((struct nvme_bdev *)bdev_io->bdev->ctxt,
				       (struct nvme_bdev_io *)bdev_io->driver_ctx,
				       bdev_io->u.bdev.offset_blocks,
				       bdev_io->u.bdev.num_blocks);

	case SPDK_BDEV_IO_TYPE_NVME_ADMIN:
		return bdev_nvme_admin_passthru((struct nvme_bdev *)bdev_io->bdev->ctxt,
						ch,
						(struct nvme_bdev_io *)bdev_io->driver_ctx,
						&bdev_io->u.nvme_passthru.cmd,
						bdev_io->u.nvme_passthru.buf,
						bdev_io->u.nvme_passthru.nbytes);

	case SPDK_BDEV_IO_TYPE_NVME_IO:
		return bdev_nvme_io_passthru((struct nvme_bdev *)bdev_io->bdev->ctxt,
					     ch,
					     (struct nvme_bdev_io *)bdev_io->driver_ctx,
					     &bdev_io->u.nvme_passthru.cmd,
					     bdev_io->u.nvme_passthru.buf,
					     bdev_io->u.nvme_passthru.nbytes);

	case SPDK_BDEV_IO_TYPE_NVME_IO_MD:
		return bdev_nvme_io_passthru_md((struct nvme_bdev *)bdev_io->bdev->ctxt,
						ch,
						(struct nvme_bdev_io *)bdev_io->driver_ctx,
						&bdev_io->u.nvme_passthru.cmd,
						bdev_io->u.nvme_passthru.buf,
						bdev_io->u.nvme_passthru.nbytes,
						bdev_io->u.nvme_passthru.md_buf,
						bdev_io->u.nvme_passthru.md_len);

	default:
		return -EINVAL;
	}
	return 0;
}

void
spdk_bdev_io_get_buf(struct spdk_bdev_io *bdev_io, spdk_bdev_io_get_buf_cb cb, uint64_t len)
{
	struct spdk_mempool *pool;
	bdev_io_stailq_t *stailq;
	void *buf = NULL;
	struct spdk_bdev_mgmt_channel *mgmt_ch;

	assert(cb != NULL);
	assert(bdev_io->u.bdev.iovs != NULL);

	if (spdk_unlikely(bdev_io->u.bdev.iovs[0].iov_base != NULL)) {
		/* Buffer already present */
		/* 如果不存在调用bdev_nvme_get_buf_cb */
		cb(bdev_io->ch->channel, bdev_io);
		return;
	}

	assert(len <= SPDK_BDEV_LARGE_BUF_MAX_SIZE);
	mgmt_ch = bdev_io->ch->module_ch->mgmt_ch;

	bdev_io->buf_len = len;
	bdev_io->get_buf_cb = cb;
	if (len <= SPDK_BDEV_SMALL_BUF_MAX_SIZE) {
		pool = g_bdev_mgr.buf_small_pool;
		stailq = &mgmt_ch->need_buf_small;
	} else {
		pool = g_bdev_mgr.buf_large_pool;
		stailq = &mgmt_ch->need_buf_large;
	}

	buf = spdk_mempool_get(pool);

	if (!buf) {
		STAILQ_INSERT_TAIL(stailq, bdev_io, buf_link);
	} else {
		spdk_bdev_io_set_buf(bdev_io, buf);
	}
}

static void
bdev_nvme_get_buf_cb(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
	int ret;

	ret = bdev_nvme_readv((struct nvme_bdev *)bdev_io->bdev->ctxt,
			      ch,
			      (struct nvme_bdev_io *)bdev_io->driver_ctx,
			      bdev_io->u.bdev.iovs,
			      bdev_io->u.bdev.iovcnt,
			      bdev_io->u.bdev.num_blocks,
			      bdev_io->u.bdev.offset_blocks);

	if (spdk_likely(ret == 0)) {
		return;
	} else if (ret == -ENOMEM) {
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_NOMEM);
	} else {
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
	}
}

static int
bdev_nvme_readv(struct nvme_bdev *nbdev, struct spdk_io_channel *ch,
		struct nvme_bdev_io *bio,
		struct iovec *iov, int iovcnt, uint64_t lba_count, uint64_t lba)
{
	struct nvme_io_channel *nvme_ch = spdk_io_channel_get_ctx(ch);

	SPDK_DEBUGLOG(SPDK_LOG_BDEV_NVME, "read %lu blocks with offset %#lx\n",
		      lba_count, lba);

	return bdev_nvme_queue_cmd(nbdev, nvme_ch->qpair, bio, BDEV_DISK_READ,
				   iov, iovcnt, lba_count, lba);
}

static int
bdev_nvme_queue_cmd(struct nvme_bdev *bdev, struct spdk_nvme_qpair *qpair,
		    struct nvme_bdev_io *bio,
		    int direction, struct iovec *iov, int iovcnt, uint64_t lba_count,
		    uint64_t lba)
{
	int rc;

	bio->iovs = iov;
	bio->iovcnt = iovcnt;
	bio->iovpos = 0;
	bio->iov_offset = 0;

	if (direction == BDEV_DISK_READ) {
		rc = spdk_nvme_ns_cmd_readv(bdev->ns, qpair, lba,
					    lba_count, bdev_nvme_queued_done, bio, 0,
					    bdev_nvme_queued_reset_sgl, bdev_nvme_queued_next_sge);
	} else {
		rc = spdk_nvme_ns_cmd_writev(bdev->ns, qpair, lba,
					     lba_count, bdev_nvme_queued_done, bio, 0,
					     bdev_nvme_queued_reset_sgl, bdev_nvme_queued_next_sge);
	}

	if (rc != 0 && rc != -ENOMEM) {
		SPDK_ERRLOG("%s failed: rc = %d\n", direction == BDEV_DISK_READ ? "readv" : "writev", rc);
	}
	return rc;
}

int
spdk_nvme_ns_cmd_readv(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
		       uint64_t lba, uint32_t lba_count,
		       spdk_nvme_cmd_cb cb_fn, void *cb_arg, uint32_t io_flags,
		       spdk_nvme_req_reset_sgl_cb reset_sgl_fn,
		       spdk_nvme_req_next_sge_cb next_sge_fn)
{
	struct nvme_request *req;
	struct nvme_payload payload;

	if (reset_sgl_fn == NULL || next_sge_fn == NULL) {
		return -EINVAL;
	}

	payload.type = NVME_PAYLOAD_TYPE_SGL;
	payload.md = NULL;
	payload.u.sgl.reset_sgl_fn = reset_sgl_fn;
	payload.u.sgl.next_sge_fn = next_sge_fn;
	payload.u.sgl.cb_arg = cb_arg;

	/* cb_fn完成之后的回调 */
	req = _nvme_ns_cmd_rw(ns, qpair, &payload, 0, 0, lba, lba_count, cb_fn, cb_arg, SPDK_NVME_OPC_READ,
			      io_flags, 0, 0, true);
	if (req != NULL) {
		return nvme_qpair_submit_request(qpair, req);
	} else {
		return -ENOMEM;
	}
}

/* _nvme_ns_cmd_rw会把bdev_nvme_queued_done设置成完成之后的回调 */
static void
bdev_nvme_queued_done(void *ref, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_bdev_io *bdev_io = spdk_bdev_io_from_ctx((struct nvme_bdev_io *)ref);

	spdk_bdev_io_complete_nvme_status(bdev_io, cpl->status.sct, cpl->status.sc);
}

void
spdk_bdev_io_complete_nvme_status(struct spdk_bdev_io *bdev_io, int sct, int sc)
{
	if (sct == SPDK_NVME_SCT_GENERIC && sc == SPDK_NVME_SC_SUCCESS) {
		bdev_io->status = SPDK_BDEV_IO_STATUS_SUCCESS;
	} else {
		bdev_io->error.nvme.sct = sct;
		bdev_io->error.nvme.sc = sc;
		bdev_io->status = SPDK_BDEV_IO_STATUS_NVME_ERROR;
	}

	spdk_bdev_io_complete(bdev_io, bdev_io->status);
}

void
spdk_bdev_io_complete(struct spdk_bdev_io *bdev_io, enum spdk_bdev_io_status status)
{
	struct spdk_bdev *bdev = bdev_io->bdev;
	struct spdk_bdev_channel *bdev_ch = bdev_io->ch;
	struct spdk_bdev_module_channel	*module_ch = bdev_ch->module_ch;

	bdev_io->status = status;

	if (spdk_unlikely(bdev_io->type == SPDK_BDEV_IO_TYPE_RESET)) {
		bool unlock_channels = false;

		if (status == SPDK_BDEV_IO_STATUS_NOMEM) {
			SPDK_ERRLOG("NOMEM returned for reset\n");
		}
		pthread_mutex_lock(&bdev->mutex);
		if (bdev_io == bdev->reset_in_progress) {
			bdev->reset_in_progress = NULL;
			unlock_channels = true;
		}
		pthread_mutex_unlock(&bdev->mutex);

		if (unlock_channels) {
			/* Explicitly handle the QoS bdev channel as no IO channel associated */
			if (bdev->qos.enabled && bdev->qos.thread) {
				spdk_thread_send_msg(bdev->qos.thread,
						     _spdk_bdev_unfreeze_qos_channel, bdev);
			}

			spdk_for_each_channel(__bdev_to_io_dev(bdev), _spdk_bdev_unfreeze_channel,
					      bdev_io, _spdk_bdev_reset_complete);
			return;
		}
	} else {
		assert(bdev_ch->io_outstanding > 0);
		assert(module_ch->io_outstanding > 0);
		bdev_ch->io_outstanding--;
		module_ch->io_outstanding--;

		if (spdk_unlikely(status == SPDK_BDEV_IO_STATUS_NOMEM)) {
			TAILQ_INSERT_HEAD(&module_ch->nomem_io, bdev_io, link);
			/*
			 * Wait for some of the outstanding I/O to complete before we
			 *  retry any of the nomem_io.  Normally we will wait for
			 *  NOMEM_THRESHOLD_COUNT I/O to complete but for low queue
			 *  depth channels we will instead wait for half to complete.
			 */
			module_ch->nomem_threshold = spdk_max((int64_t)module_ch->io_outstanding / 2,
							      (int64_t)module_ch->io_outstanding - NOMEM_THRESHOLD_COUNT);

			/*
			 *  When last IO complete with NOMEM and module_ch->io_outstanding is 0(no inflght IO),
			 *  it means the IOs on nomem_io list would not be processed any more. 
			 *  Only would be happen when work with multi-bdev.
			 *
			 *  So we need to force retry one IO which on nomem_io list to trigger the list can be 
			 *  processed again.
			 */
			if (!module_ch->io_outstanding && (module_ch->nomem_io_trigger_poller == NULL)) {
				module_ch->nomem_io_trigger_poller = spdk_poller_register(spdk_bdev_nomem_io_trigger,
								bdev_ch, 0);
			}
			return;
		}

		if ((module_ch->nomem_io_abort == false) && spdk_unlikely(!TAILQ_EMPTY(&module_ch->nomem_io))) {
			_spdk_bdev_ch_retry_io(bdev_ch, false);
		}
	}

	_spdk_bdev_io_complete(bdev_io);
}

static inline void
_spdk_bdev_io_complete(void *ctx)
{
	struct spdk_bdev_io *bdev_io = ctx;

	if (spdk_unlikely(bdev_io->in_submit_request || bdev_io->io_submit_ch)) {
		/*
		 * Send the completion to the thread that originally submitted the I/O,
		 * which may not be the current thread in the case of QoS.
		 */
		if (bdev_io->io_submit_ch) {
			bdev_io->ch = bdev_io->io_submit_ch;
			bdev_io->io_submit_ch = NULL;
		}

		/*
		 * Defer completion to avoid potential infinite recursion if the
		 * user's completion callback issues a new I/O.
		 */
		spdk_thread_send_msg(spdk_io_channel_get_thread(bdev_io->ch->channel),
				     _spdk_bdev_io_complete, bdev_io);
		return;
	}

	if (bdev_io->status == SPDK_BDEV_IO_STATUS_SUCCESS) {
		switch (bdev_io->type) {
		case SPDK_BDEV_IO_TYPE_READ:
			bdev_io->ch->stat.bytes_read += bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen;
			bdev_io->ch->stat.num_read_ops++;
			bdev_io->ch->stat.read_latency_ticks += (spdk_get_ticks() - bdev_io->submit_tsc);
			break;
		case SPDK_BDEV_IO_TYPE_WRITE:
			bdev_io->ch->stat.bytes_written += bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen;
			bdev_io->ch->stat.num_write_ops++;
			bdev_io->ch->stat.write_latency_ticks += (spdk_get_ticks() - bdev_io->submit_tsc);
			break;
		default:
			break;
		}
	}

#ifdef SPDK_CONFIG_VTUNE
	uint64_t now_tsc = spdk_get_ticks();
	if (now_tsc > (bdev_io->ch->start_tsc + bdev_io->ch->interval_tsc)) {
		uint64_t data[5];

		data[0] = bdev_io->ch->stat.num_read_ops;
		data[1] = bdev_io->ch->stat.bytes_read;
		data[2] = bdev_io->ch->stat.num_write_ops;
		data[3] = bdev_io->ch->stat.bytes_written;
		data[4] = bdev_io->bdev->fn_table->get_spin_time ?
			  bdev_io->bdev->fn_table->get_spin_time(bdev_io->ch->channel) : 0;

		__itt_metadata_add(g_bdev_mgr.domain, __itt_null, bdev_io->ch->handle,
				   __itt_metadata_u64, 5, data);

		memset(&bdev_io->ch->stat, 0, sizeof(bdev_io->ch->stat));
		bdev_io->ch->start_tsc = now_tsc;
	}
#endif

	if (bdev_io->cb == NULL) {
		/*
		 * In some case, eg. vhost blk dev destroy and wait timeout for inflight io complete,
		 * the mapped guest memory would be unmaped and the task pool would be freed at first.
		 *
		 * If so, don't do completet_cb in case mem access segfault, just free bdev_io.
		 */
		spdk_bdev_free_io(bdev_io);
		return;
	}

	assert(bdev_io->cb != NULL);
	assert(spdk_get_thread() == spdk_io_channel_get_thread(bdev_io->ch->channel));

	/* 调用blk_request_complete_cb */
	/* spdk_bdev_io_init会把blk_request_complete_cb设置成回调 */
	bdev_io->cb(bdev_io, bdev_io->status == SPDK_BDEV_IO_STATUS_SUCCESS,
		    bdev_io->caller_ctx);
}

int
spdk_bdev_free_io(struct spdk_bdev_io *bdev_io)
{
	if (bdev_io->status == SPDK_BDEV_IO_STATUS_PENDING) {
		SPDK_ERRLOG("bdev_io is in pending state\n");
		assert(false);
		return -1;
	}

	spdk_bdev_put_io(bdev_io);

	return 0;
}

static void
blk_request_finish(bool success, struct spdk_vhost_blk_task *task)
{
	*task->status = success ? VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR;
	spdk_vhost_vq_used_ring_enqueue(&task->bvdev->vdev, task->vq, task->req_idx,
					task->used_len);
	SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Finished task (%p) req_idx=%d\n status: %s\n", task,
		      task->req_idx, success ? "OK" : "FAIL");
	blk_task_finish(task);
}

static void
blk_task_finish(struct spdk_vhost_blk_task *task)
{
	assert(task->bvdev->vdev.task_cnt > 0);
	task->bvdev->vdev.task_cnt--;
	task->used = false;
}

