struct spdk_subsystem_list g_subsystems = TAILQ_HEAD_INITIALIZER(g_subsystems);
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
SPDK_SUBSYSTEM_DEPEND(vhost, scsi)


/**
 * \brief Register a new subsystem
 */
#define SPDK_SUBSYSTEM_REGISTER(_name) \
	__attribute__((constructor)) static void _name ## _register(void)	\
	{									\
		spdk_add_subsystem(&_name);					\
	}

/**
 * \brief Declare that a subsystem depends on another subsystem.
 */
#define SPDK_SUBSYSTEM_DEPEND(_name, _depends_on)						\
	static struct spdk_subsystem_depend __subsystem_ ## _name ## _depend_on ## _depends_on = { \
	.name = #_name,										\
	.depends_on = #_depends_on,								\
	};											\
	__attribute__((constructor)) static void _name ## _depend_on ## _depends_on(void)	\
	{											\
		spdk_add_subsystem_depend(&__subsystem_ ## _name ## _depend_on ## _depends_on); \
	}

void
spdk_add_subsystem(struct spdk_subsystem *subsystem)
{
	TAILQ_INSERT_TAIL(&g_subsystems, subsystem, tailq);
}


void
spdk_add_subsystem_depend(struct spdk_subsystem_depend *depend)
{
	TAILQ_INSERT_TAIL(&g_subsystems_deps, depend, tailq);
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

void
spdk_app_stop(int rc)
{
	if (rc) {
		SPDK_WARNLOG("spdk_app_stop'd on non-zero\n");
	}
	g_spdk_app.rc = rc;
	/*
	 * We want to run spdk_subsystem_fini() from the same lcore where spdk_subsystem_init()
	 * was called.
	 */
	spdk_event_call(spdk_event_allocate(g_init_lcore, _spdk_app_stop, NULL, NULL));
}

static void
_spdk_app_stop(void *arg1, void *arg2)
{
	struct spdk_event *app_stop_event;

	spdk_rpc_finish();

	app_stop_event = spdk_event_allocate(spdk_env_get_current_core(), spdk_reactors_stop, NULL, NULL);
	spdk_subsystem_fini(app_stop_event);
}

void
spdk_reactors_stop(void *arg1, void *arg2)
{
	/* 当reactor检测到这个状态，线程就会退出 */
	g_reactor_state = SPDK_REACTOR_STATE_EXITING;
}

static void
subsystem_sort(void)
{
	bool depends_on, depends_on_sorted;
	struct spdk_subsystem *subsystem, *subsystem_tmp;
	struct spdk_subsystem_depend *subsystem_dep;

	struct spdk_subsystem_list subsystems_list = TAILQ_HEAD_INITIALIZER(subsystems_list);

	while (!TAILQ_EMPTY(&g_subsystems)) {
		TAILQ_FOREACH_SAFE(subsystem, &g_subsystems, tailq, subsystem_tmp) {
			depends_on = false;
			TAILQ_FOREACH(subsystem_dep, &g_subsystems_deps, tailq) {
				/* 
				 * subsystem->name：subsystem的name
				 * subsystem_dep->name：依赖该subsystem的name
				 */
				if (strcmp(subsystem->name, subsystem_dep->name) == 0) {
					/* 找到了依赖的subsystem */
					depends_on = true;
					depends_on_sorted = !!spdk_subsystem_find(&subsystems_list, subsystem_dep->depends_on);
					/* 第一次肯定返回NULL，则集训循环，直到将依赖的subsystem添加到subsystems_list，
					 * depends_on_sorted才为真。这时候表示依赖的subsystem已经被添加到了subsystems_list
					 */
					if (depends_on_sorted) {
						continue;
					}
					break;
				}
			}

			if (depends_on == false) {
				/* 没有依赖的subsystem首先添加到subsystems_list */
				TAILQ_REMOVE(&g_subsystems, subsystem, tailq);
				TAILQ_INSERT_TAIL(&subsystems_list, subsystem, tailq);
			} else {
				if (depends_on_sorted == true) {
					TAILQ_REMOVE(&g_subsystems, subsystem, tailq);
					TAILQ_INSERT_TAIL(&subsystems_list, subsystem, tailq);
				}
			}
		}
	}

	/* subsystems_list中已经进行了排序，确保被依赖的subsystem排在前面 */
	TAILQ_FOREACH_SAFE(subsystem, &subsystems_list, tailq, subsystem_tmp) {
		TAILQ_REMOVE(&subsystems_list, subsystem, tailq);
		/* 将排序过的subsystem重新添加到g_subsystems中 */
		TAILQ_INSERT_TAIL(&g_subsystems, subsystem, tailq);
	}
}

struct spdk_subsystem *
spdk_subsystem_find(struct spdk_subsystem_list *list, const char *name)
{
	struct spdk_subsystem *iter;

	TAILQ_FOREACH(iter, list, tailq) {
		if (strcmp(name, iter->name) == 0) {
			return iter;
		}
	}

	return NULL;
}

/* 每个子系统都会在调用spdk_subsystem_init_next，来执行下一个subsystem的init */
void
spdk_subsystem_init_next(int rc)
{
	if (rc) {
		SPDK_ERRLOG("Init subsystem %s failed\n", g_next_subsystem->name);
		spdk_app_stop(rc);
		return;
	}

	if (!g_next_subsystem) {
		g_next_subsystem = TAILQ_FIRST(&g_subsystems);
	} else {
		g_next_subsystem = TAILQ_NEXT(g_next_subsystem, tailq);
	}

	if (!g_next_subsystem) {
		/* 所有子系统初始化玩之后，然后发送g_app_start_event，reactor会调用start_rpc*/
		g_subsystems_initialized = true;
		spdk_event_call(g_app_start_event);
		return;
	}

	if (g_next_subsystem->init) {
		g_next_subsystem->init();
	} else {
		spdk_subsystem_init_next(0);
	}
}

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

		if (spdk_vhost_blk_construct(name, cpumask, bdev_name, readonly) < 0) {
			continue;
		}
	}

	return 0;
}

/* This Macro is used to create a mendacious bdev. */
#define MENDACIOUS_BDEV "Mendacious-device"  /*  虚假的device */


int
spdk_vhost_blk_construct(const char *name, const char *cpumask, const char *dev_name, bool readonly)
{
	struct spdk_vhost_blk_dev *bvdev = NULL;
	struct spdk_bdev *bdev;
	int ret = 0;

	spdk_vhost_lock();
	/* nvmek可以找到 */
	bdev = spdk_bdev_get_by_name(dev_name);
	if (bdev == NULL) {
		SPDK_ERRLOG("Controller %s: bdev '%s' not found\n",
			    name, dev_name);
		if (strstr(dev_name, "nvme")) {
			/* Load the concealed bdev for NVMe devices that encouter disk failure. */
			bdev = spdk_bdev_get_by_name(MENDACIOUS_BDEV);
			if (bdev == NULL) {
				SPDK_ERRLOG("Mendacious bdev does not exist\n");
				ret = -1;
				goto out;
			}
		} else {
			ret = -1;
			goto out;
		}
	}

	bvdev = spdk_dma_zmalloc(sizeof(*bvdev), SPDK_CACHE_LINE_SIZE, NULL);

	ret = spdk_bdev_open(bdev, true, bdev_remove_cb, bvdev, &bvdev->bdev_desc);

	bvdev->bdev = bdev;
	bvdev->readonly = readonly;
	bvdev->vdev.io_dropped = false;
	ret = spdk_vhost_dev_register(&bvdev->vdev, name, cpumask, &vhost_blk_device_backend);

	if (readonly && rte_vhost_driver_enable_features(bvdev->vdev.path, (1ULL << VIRTIO_BLK_F_RO))) {
		SPDK_ERRLOG("Controller %s: failed to set as a readonly\n", name);
		spdk_bdev_close(bvdev->bdev_desc);

		if (spdk_vhost_dev_unregister(&bvdev->vdev) != 0) {
			SPDK_ERRLOG("Controller %s: failed to remove controller\n", name);
		}

		ret = -1;
		goto out;
	}

	SPDK_INFOLOG(SPDK_LOG_VHOST, "Controller %s: using bdev '%s'\n", name, dev_name);
out:
	if (ret != 0 && bvdev) {
		spdk_dma_free(bvdev);
	}
	spdk_vhost_unlock();
	return ret;
}

struct spdk_bdev *
spdk_bdev_get_by_name(const char *bdev_name)
{
	struct spdk_bdev_alias *tmp;
	struct spdk_bdev *bdev = spdk_bdev_first();

	while (bdev != NULL) {
		if (strcmp(bdev_name, bdev->name) == 0) {
			return bdev;
		}

		TAILQ_FOREACH(tmp, &bdev->aliases, tailq) {
			if (strcmp(bdev_name, tmp->alias) == 0) {
				return bdev;
			}
		}

		bdev = spdk_bdev_next(bdev);
	}

	return NULL;
}

struct spdk_bdev *
spdk_bdev_first(void)
{
	struct spdk_bdev *bdev;

	bdev = TAILQ_FIRST(&g_bdev_mgr.bdevs);
	if (bdev) {
		SPDK_DEBUGLOG(SPDK_LOG_BDEV, "Starting bdev iteration at %s\n", bdev->name);
	}

	return bdev;
}

int
spdk_bdev_open(struct spdk_bdev *bdev, bool write, spdk_bdev_remove_cb_t remove_cb,
	       void *remove_ctx, struct spdk_bdev_desc **_desc)
{
	struct spdk_bdev_desc *desc;

	desc = calloc(1, sizeof(*desc));

	pthread_mutex_lock(&bdev->mutex);

	if (write && bdev->claim_module) {
		SPDK_INFOLOG(SPDK_LOG_BDEV, "Could not open %s - already claimed\n", bdev->name);
		free(desc);
		pthread_mutex_unlock(&bdev->mutex);
		return -EPERM;
	}

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

	/* We expect devices inside g_spdk_vhost_devices to be sorted in ascending
	 * order in regard of vdev->id. For now we always set vdev->id = ctrlr_num++
	 * and append each vdev to the very end of g_spdk_vhost_devices list.
	 * This is required for foreach vhost events to work.
	 */
	
	cpumask = spdk_cpuset_alloc();
	

	if (spdk_vhost_parse_core_mask(mask_str, cpumask) != 0) {
	
	}

	if (spdk_vhost_dev_find(name)) {
	
	}

	if (snprintf(path, sizeof(path), "%s%s", dev_dirname, name) >= (int)sizeof(path)) {
		
	}

	/* Register vhost driver to handle vhost messages. */
	if (stat(path, &file_stat) != -1) {
		if (!S_ISSOCK(file_stat.st_mode)) {
			SPDK_ERRLOG("Cannot create a domain socket at path \"%s\": "
				    "The file already exists and is not a socket.\n",
				    path);
			rc = -EIO;
			goto out;
		} else if (unlink(path) != 0) {
			SPDK_ERRLOG("Cannot create a domain socket at path \"%s\": "
				    "The socket already exists and failed to unlink.\n",
				    path);
			rc = -EIO;
			goto out;
		}
	}

	if (rte_vhost_driver_register(path, 0) != 0) {
		SPDK_ERRLOG("Could not register controller %s with vhost library\n", name);
		SPDK_ERRLOG("Check if domain socket %s already exists\n", path);
		rc = -EIO;
		goto out;
	}
	if (rte_vhost_driver_set_features(path, backend->virtio_features) ||
	    rte_vhost_driver_disable_features(path, backend->disabled_features)) {
		SPDK_ERRLOG("Couldn't set vhost features for controller %s\n", name);

		rte_vhost_driver_unregister(path);
		rc = -EIO;
		goto out;
	}

	if (rte_vhost_driver_callback_register(path, &g_spdk_vhost_ops) != 0) {
		rte_vhost_driver_unregister(path);
		SPDK_ERRLOG("Couldn't register callbacks for controller %s\n", name);
		rc = -EIO;
		goto out;
	}

	/* The following might start a POSIX thread that polls for incoming
	 * socket connections and calls backend->start/stop_device. These backend
	 * callbacks are also protected by the global SPDK vhost mutex, so we're
	 * safe with not initializing the vdev just yet.
	 */
	if (spdk_call_unaffinitized(_start_rte_driver, path) == NULL) {
		SPDK_ERRLOG("Failed to start vhost driver for controller %s (%d): %s\n",
			    name, errno, spdk_strerror(errno));
		rte_vhost_driver_unregister(path);
		rc = -EIO;
		goto out;
	}

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
	vdev->stats_check_interval = SPDK_VHOST_DEV_STATS_CHECK_INTERVAL_MS * spdk_get_ticks_hz() /
				     1000UL;

	TAILQ_INSERT_TAIL(&g_spdk_vhost_devices, vdev, tailq);

	SPDK_INFOLOG(SPDK_LOG_VHOST, "Controller %s: new controller added\n", vdev->name);
	return 0;
}


