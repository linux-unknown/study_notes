
static struct spdk_subsystem g_spdk_subsystem_bdev = {
	.name = "bdev",
	.init = spdk_bdev_subsystem_initialize,
	.fini = spdk_bdev_subsystem_finish,
	.config = spdk_bdev_config_text,
	.write_config_json = _spdk_bdev_subsystem_config_json,
};

SPDK_SUBSYSTEM_REGISTER(g_spdk_subsystem_bdev);
SPDK_SUBSYSTEM_DEPEND(bdev, copy);



static void
spdk_bdev_subsystem_initialize(void)
{
	spdk_bdev_initialize(spdk_bdev_initialize_complete, NULL);
}

void
spdk_bdev_initialize(spdk_bdev_init_cb cb_fn, void *cb_arg)
{
	int cache_size;
	int rc = 0;
	char mempool_name[32];

	g_init_cb_fn = cb_fn;
	g_init_cb_arg = cb_arg;

	snprintf(mempool_name, sizeof(mempool_name), "bdev_io_%d", getpid());

	g_bdev_mgr.bdev_io_pool = spdk_mempool_create(mempool_name,
				  SPDK_BDEV_IO_POOL_SIZE,
				  sizeof(struct spdk_bdev_io) +
				  spdk_bdev_module_get_max_ctx_size(),
				  0,
				  SPDK_ENV_SOCKET_ID_ANY);


	/**
	 * Ensure no more than half of the total buffers end up local caches, by
	 *   using spdk_env_get_core_count() to determine how many local caches we need
	 *   to account for.
	 */
	cache_size = BUF_SMALL_POOL_SIZE / (2 * spdk_env_get_core_count());
	snprintf(mempool_name, sizeof(mempool_name), "buf_small_pool_%d", getpid());

	g_bdev_mgr.buf_small_pool = spdk_mempool_create(mempool_name,
				    BUF_SMALL_POOL_SIZE,
				    SPDK_BDEV_SMALL_BUF_MAX_SIZE + 512,
				    cache_size,
				    SPDK_ENV_SOCKET_ID_ANY);
	

	cache_size = BUF_LARGE_POOL_SIZE / (2 * spdk_env_get_core_count());
	snprintf(mempool_name, sizeof(mempool_name), "buf_large_pool_%d", getpid());

	g_bdev_mgr.buf_large_pool = spdk_mempool_create(mempool_name,
				    BUF_LARGE_POOL_SIZE,
				    SPDK_BDEV_LARGE_BUF_MAX_SIZE + 512,
				    cache_size,
				    SPDK_ENV_SOCKET_ID_ANY);
	

	g_bdev_mgr.zero_buffer = spdk_dma_zmalloc(ZERO_BUFFER_SIZE, ZERO_BUFFER_SIZE, NULL);

#ifdef SPDK_CONFIG_VTUNE
	g_bdev_mgr.domain = __itt_domain_create("spdk_bdev");
#endif

	spdk_io_device_register(&g_bdev_mgr, spdk_bdev_mgmt_channel_create,
				spdk_bdev_mgmt_channel_destroy,
				sizeof(struct spdk_bdev_mgmt_channel));

	rc = spdk_bdev_modules_init();


	spdk_bdev_module_action_complete();
}

static void
spdk_bdev_initialize_complete(void *cb_arg, int rc)
{
	/* 执行其他subsystem初始化 */
	spdk_subsystem_init_next(rc);
}

static int
spdk_bdev_module_get_max_ctx_size(void)
{
	struct spdk_bdev_module *bdev_module;
	int max_bdev_module_size = 0;

	TAILQ_FOREACH(bdev_module, &g_bdev_mgr.bdev_modules, tailq) {
		if (bdev_module->get_ctx_size && bdev_module->get_ctx_size() > max_bdev_module_size) {
			max_bdev_module_size = bdev_module->get_ctx_size();
		}
	}

	return max_bdev_module_size;
}


static void
spdk_bdev_module_action_complete(void)
{
	struct spdk_bdev_module *m;

	/*
	 * Don't finish bdev subsystem initialization if
	 * module pre-initialization is still in progress, or
	 * the subsystem been already initialized.
	 */
	if (!g_bdev_mgr.module_init_complete || g_bdev_mgr.init_complete) {
		return;
	}

	/*
	 * Check all bdev modules for inits/examinations in progress. If any
	 * exist, return immediately since we cannot finish bdev subsystem
	 * initialization until all are completed.
	 */
	TAILQ_FOREACH(m, &g_bdev_mgr.bdev_modules, tailq) {
		if (m->action_in_progress > 0) {
			return;
		}
	}

	/*
	 * Modules already finished initialization - now that all
	 * the bdev modules have finished their asynchronous I/O
	 * processing, the entire bdev layer can be marked as complete.
	 */
	spdk_bdev_init_complete(0);
}


void
spdk_io_device_register(void *io_device, spdk_io_channel_create_cb create_cb,
			spdk_io_channel_destroy_cb destroy_cb, uint32_t ctx_size)
{
	struct io_device *dev, *tmp;

	dev = calloc(1, sizeof(struct io_device));

	dev->io_device = io_device;
	dev->create_cb = create_cb;
	dev->destroy_cb = destroy_cb;
	dev->unregister_cb = NULL;
	dev->ctx_size = ctx_size;
	dev->for_each_count = 0;
	dev->unregistered = false;
	dev->refcnt = 0;

	pthread_mutex_lock(&g_devlist_mutex);
	TAILQ_FOREACH(tmp, &g_io_devices, tailq) {
		if (tmp->io_device == io_device) {
			SPDK_ERRLOG("io_device %p already registered\n", io_device);
			free(dev);
			pthread_mutex_unlock(&g_devlist_mutex);
			return;
		}
	}
	/* 将dev添加到g_io_devices中 
	* 后面分析在哪里处理该io
	*/
	TAILQ_INSERT_TAIL(&g_io_devices, dev, tailq);
	pthread_mutex_unlock(&g_devlist_mutex);
}

static struct spdk_bdev_mgr g_bdev_mgr = {
	.bdev_modules = TAILQ_HEAD_INITIALIZER(g_bdev_mgr.bdev_modules),
	.bdevs = TAILQ_HEAD_INITIALIZER(g_bdev_mgr.bdevs),
	.init_complete = false,
	.module_init_complete = false,
};


static int
spdk_bdev_modules_init(void)
{
	struct spdk_bdev_module *module;
	int rc = 0;
	/* SPDK_BDEV_MODULE_REGISTER最终会往bdev_modules添加元素 */
	TAILQ_FOREACH(module, &g_bdev_mgr.bdev_modules, tailq) {
		rc = module->module_init(); /* 执行module_init函数 */
		if (rc != 0) {
			break;
		}

		if (module->get_ctx_size) {
			int ctx_size = module->get_ctx_size();
			if (ctx_size > g_bdev_mgr.max_ctx_size) {
				g_bdev_mgr.max_ctx_size = ctx_size;
			}
		}
	}

	g_bdev_mgr.module_init_complete = true;
	return rc;
}


static struct spdk_bdev_module nvme_if = {
	.name = "nvme",
	.module_init = bdev_nvme_library_init,
	.module_fini = bdev_nvme_library_fini,
	.config_text = bdev_nvme_get_spdk_running_config,
	.config_json = bdev_nvme_config_json,
	.get_ctx_size = bdev_nvme_get_ctx_size,

};
SPDK_BDEV_MODULE_REGISTER(&nvme_if)

/*
 * Macro used to register module for later initialization.
 */
#define SPDK_BDEV_MODULE_REGISTER(_module)							\
	__attribute__((constructor)) static void						\
	SPDK_BDEV_MODULE_REGISTER_FN_NAME(__LINE__)  (void)					\
	{											\
	    spdk_bdev_module_list_add(_module);							\
	}

void
spdk_bdev_module_list_add(struct spdk_bdev_module *bdev_module)
{
	if (spdk_bdev_module_list_find(bdev_module->name)) {
		fprintf(stderr, "ERROR: module '%s' already registered.\n", bdev_module->name);
		assert(false);
	}

	if (bdev_module->async_init) {
		bdev_module->action_in_progress = 1;
	}

	/*
	 * Modules with examine callbacks must be initialized first, so they are
	 *  ready to handle examine callbacks from later modules that will
	 *  register physical bdevs.
	 */
	if (bdev_module->examine != NULL) {
		TAILQ_INSERT_HEAD(&g_bdev_mgr.bdev_modules, bdev_module, tailq);
	} else {
		TAILQ_INSERT_TAIL(&g_bdev_mgr.bdev_modules, bdev_module, tailq);
	}
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


static int
bdev_nvme_library_init(void)
{
	struct spdk_conf_section *sp;
	const char *val;
	int rc = 0;
	size_t i;
	struct nvme_probe_ctx *probe_ctx = NULL;
	int retry_count;
	uint32_t local_nvme_num = 0;

	sp = spdk_conf_find_section(NULL, "Nvme");


	probe_ctx = calloc(1, sizeof(*probe_ctx));
	
	if ((retry_count = spdk_conf_section_get_intval(sp, "RetryCount")) < 0) {
		if ((retry_count = spdk_conf_section_get_intval(sp, "NvmeRetryCount")) < 0) {
			retry_count = SPDK_NVME_DEFAULT_RETRY_COUNT;
		} else {
			SPDK_WARNLOG("NvmeRetryCount was renamed to RetryCount\n");
			SPDK_WARNLOG("Please update your configuration file\n");
		}
	}

	spdk_nvme_retry_count = retry_count;

	if ((g_timeout = spdk_conf_section_get_intval(sp, "Timeout")) < 0) {
		/* Check old name for backward compatibility */
		if ((g_timeout = spdk_conf_section_get_intval(sp, "NvmeTimeoutValue")) < 0) {
			g_timeout = 0;
		} else {
			SPDK_WARNLOG("NvmeTimeoutValue was renamed to Timeout\n");
			SPDK_WARNLOG("Please update your configuration file\n");
		}
	}

	if (g_timeout > 0) {
		val = spdk_conf_section_get_val(sp, "ActionOnTimeout");
		if (val != NULL) {
			if (!strcasecmp(val, "Reset")) {
				g_action_on_timeout = TIMEOUT_ACTION_RESET;
			} else if (!strcasecmp(val, "Abort")) {
				g_action_on_timeout = TIMEOUT_ACTION_ABORT;
			}
		} else {
			/* Handle old name for backward compatibility */
			val = spdk_conf_section_get_val(sp, "ResetControllerOnTimeout");
			if (val) {
				SPDK_WARNLOG("ResetControllerOnTimeout was renamed to ActionOnTimeout\n");
				SPDK_WARNLOG("Please update your configuration file\n");

				if (spdk_conf_section_get_boolval(sp, "ResetControllerOnTimeout", false)) {
					g_action_on_timeout = TIMEOUT_ACTION_RESET;
				}
			}
		}
	}

	g_nvme_adminq_poll_timeout_us = spdk_conf_section_get_intval(sp, "AdminPollRate");
	if (g_nvme_adminq_poll_timeout_us <= 0) {
		g_nvme_adminq_poll_timeout_us = 100000;
	}

	if (spdk_process_is_primary()) {
		g_nvme_hotplug_enabled = spdk_conf_section_get_boolval(sp, "HotplugEnable", false);
	}

	g_nvme_hotplug_poll_timeout_us = spdk_conf_section_get_intval(sp, "HotplugPollRate");
	if (g_nvme_hotplug_poll_timeout_us <= 0 || g_nvme_hotplug_poll_timeout_us > 100000) {
		g_nvme_hotplug_poll_timeout_us = 100000;
	}

	g_nvme_shutdown_timeout_ms = spdk_conf_section_get_intval(sp, "ShutdownTimeout");
	if (g_nvme_shutdown_timeout_ms < 0) {
		g_nvme_shutdown_timeout_ms = 0;
	} else if (g_nvme_shutdown_timeout_ms > 10000) {
		g_nvme_shutdown_timeout_ms = 10000;
	}

	for (i = 0; i < NVME_MAX_CONTROLLERS; i++) {
		val = spdk_conf_section_get_nmval(sp, "TransportID", i, 0);
	

		rc = spdk_nvme_transport_id_parse(&probe_ctx->trids[i], val);


		val = spdk_conf_section_get_nmval(sp, "TransportID", i, 1);
	
		probe_ctx->names[i] = val;
		probe_ctx->count++;

		val = spdk_conf_section_get_nmval(sp, "TransportID", i, 2);
		if (val == NULL) {
			SPDK_ERRLOG("No disk ID provided for TransportID\n");
			probe_ctx->disk_id[i] = "";
		} else
			probe_ctx->disk_id[i] = val;

		if (probe_ctx->trids[i].trtype != SPDK_NVME_TRANSPORT_PCIE) {
			if (probe_ctx->trids[i].subnqn[0] == '\0') {
				SPDK_ERRLOG("Need to provide subsystem nqn\n");
				rc = -1;
				goto end;
			}

			if (spdk_nvme_probe(&probe_ctx->trids[i], probe_ctx, probe_cb, attach_cb, NULL)) {
				rc = -1;
				goto end;
			}
		} else {
			local_nvme_num++;
		}
	}

	if (local_nvme_num > 0) {
		/* 只调用一次spdk_nvme_probe */
		/* used to probe local NVMe device */
		if (spdk_nvme_probe(NULL, probe_ctx, probe_cb, attach_cb, NULL)) {
			rc = -1;
			goto end;
		}
	}

	if (g_nvme_hotplug_enabled) {
		g_hotplug_poller = spdk_poller_register(bdev_nvme_hotplug, NULL,
							g_nvme_hotplug_poll_timeout_us);
	}

end:
	free(probe_ctx);
	return rc;
}

int
spdk_nvme_probe(const struct spdk_nvme_transport_id *trid, void *cb_ctx,
		spdk_nvme_probe_cb probe_cb, spdk_nvme_attach_cb attach_cb,
		spdk_nvme_remove_cb remove_cb)
{
	int rc;
	struct spdk_nvme_transport_id trid_pcie;

	rc = nvme_driver_init();
	/* trid = NULL */
	if (trid == NULL) {
		memset(&trid_pcie, 0, sizeof(trid_pcie));
		trid_pcie.trtype = SPDK_NVME_TRANSPORT_PCIE;
		trid = &trid_pcie;
	}
	/* remove_cb = NULL */
	return spdk_nvme_probe_internal(trid, cb_ctx, probe_cb, attach_cb, remove_cb, NULL);
}

#define SPDK_NVME_DRIVER_NAME "spdk_nvme_driver"

struct nvme_driver	*g_spdk_nvme_driver;


static int
nvme_driver_init(void)
{
	int ret = 0;
	/* Any socket ID */
	int socket_id = -1;

	/* Each process needs its own pid. */
	g_pid = getpid();

	/*
	 * Only one thread from one process will do this driver init work.
	 * The primary process will reserve the shared memory and do the
	 *  initialization.
	 * The secondary process will lookup the existing reserved memory.
	 */
	if (spdk_process_is_primary()) {
		/* The unique named memzone already reserved. */
		if (g_spdk_nvme_driver != NULL) {
			assert(g_spdk_nvme_driver->initialized == true);

			return 0;
		} else {
			g_spdk_nvme_driver = spdk_memzone_reserve(SPDK_NVME_DRIVER_NAME,
					     sizeof(struct nvme_driver), socket_id, 0);
		}
	} else {
		g_spdk_nvme_driver = spdk_memzone_lookup(SPDK_NVME_DRIVER_NAME);

		/* The unique named memzone already reserved by the primary process. */
		if (g_spdk_nvme_driver != NULL) {
			int ms_waited = 0;

			/* Wait the nvme driver to get initialized. */
			while ((g_spdk_nvme_driver->initialized == false) &&
			       (ms_waited < g_nvme_driver_timeout_ms)) {
				ms_waited++;
				nvme_delay(1000); /* delay 1ms */
			}
			if (g_spdk_nvme_driver->initialized == false) {
				SPDK_ERRLOG("timeout waiting for primary process to init\n");

				return -1;
			}
		} else {
			SPDK_ERRLOG("primary process is not started yet\n");

			return -1;
		}

		return 0;
	}

	/*
	 * At this moment, only one thread from the primary process will do
	 * the g_spdk_nvme_driver initialization
	 */
	assert(spdk_process_is_primary());

	ret = nvme_robust_mutex_init_shared(&g_spdk_nvme_driver->lock);

	nvme_robust_mutex_lock(&g_spdk_nvme_driver->lock);

	g_spdk_nvme_driver->initialized = false;

	TAILQ_INIT(&g_spdk_nvme_driver->shared_attached_ctrlrs);

	spdk_uuid_generate(&g_spdk_nvme_driver->default_extended_host_id);

	nvme_robust_mutex_unlock(&g_spdk_nvme_driver->lock);

	return ret;
}

/* This function must only be called while holding g_spdk_nvme_driver->lock */
static int
spdk_nvme_probe_internal(const struct spdk_nvme_transport_id *trid, void *cb_ctx,
			 spdk_nvme_probe_cb probe_cb, spdk_nvme_attach_cb attach_cb,
			 spdk_nvme_remove_cb remove_cb, struct spdk_nvme_ctrlr **connected_ctrlr)
{
	/* connected_ctrlr = NULL */
	int rc;
	struct spdk_nvme_ctrlr *ctrlr;
	bool direct_connect = (connected_ctrlr != NULL);

	if (!spdk_nvme_transport_available(trid->trtype)) {
	}

	nvme_robust_mutex_lock(&g_spdk_nvme_driver->lock);

	nvme_transport_ctrlr_scan(trid, cb_ctx, probe_cb, remove_cb, direct_connect);

	/*
	 * Probe controllers on the shared_attached_ctrlrs list
	 */
	/* 不执行该路径 */
	if (!spdk_process_is_primary() && (trid->trtype == SPDK_NVME_TRANSPORT_PCIE)) {
		TAILQ_FOREACH(ctrlr, &g_spdk_nvme_driver->shared_attached_ctrlrs, tailq) {
			/* Do not attach other ctrlrs if user specify a valid trid */
			if ((strlen(trid->traddr) != 0) &&
			    (spdk_nvme_transport_id_compare(trid, &ctrlr->trid))) {
				continue;
			}

			nvme_ctrlr_proc_get_ref(ctrlr);

			/*
			 * Unlock while calling attach_cb() so the user can call other functions
			 *  that may take the driver lock, like nvme_detach().
			 */
			if (attach_cb) {
				nvme_robust_mutex_unlock(&g_spdk_nvme_driver->lock);
				attach_cb(cb_ctx, &ctrlr->trid, ctrlr, &ctrlr->opts);
				nvme_robust_mutex_lock(&g_spdk_nvme_driver->lock);
			}
		}

		nvme_robust_mutex_unlock(&g_spdk_nvme_driver->lock);

		rc = 0;

		goto exit;
	}

	nvme_robust_mutex_unlock(&g_spdk_nvme_driver->lock);
	/*
	 * Keep going even if one or more nvme_attach() calls failed,
	 *  but maintain the value of rc to signal errors when we return.
	 */

	rc = nvme_init_controllers(cb_ctx, attach_cb);

exit:
	if (connected_ctrlr) {
		*connected_ctrlr = spdk_nvme_get_ctrlr_by_trid(trid);
	}

	return rc;
}


int
nvme_transport_ctrlr_scan(const struct spdk_nvme_transport_id *trid,
			  void *cb_ctx,
			  spdk_nvme_probe_cb probe_cb,
			  spdk_nvme_remove_cb remove_cb,
			  bool direct_connect)
{
	/* 调用   nvme_pcie_ctrlr_scan */
	NVME_TRANSPORT_CALL(trid->trtype, ctrlr_scan, (trid, cb_ctx, probe_cb, remove_cb, direct_connect));
}


#define NVME_TRANSPORT_CALL(trtype, func_name, args)		\
	do {							\
		switch (trtype) {				\
		TRANSPORT_PCIE(func_name, args)			\  
		TRANSPORT_FABRICS_RDMA(func_name, args)		\
		TRANSPORT_DEFAULT(trtype)			\
		}						\
		SPDK_UNREACHABLE();				\
	} while (0)

#define TRANSPORT_PCIE(func_name, args)	case SPDK_NVME_TRANSPORT_PCIE: return nvme_pcie_ ## func_name args;


int
nvme_pcie_ctrlr_scan(const struct spdk_nvme_transport_id *trid,
		     void *cb_ctx,
		     spdk_nvme_probe_cb probe_cb,
		     spdk_nvme_remove_cb remove_cb,
		     bool direct_connect)
{
	struct nvme_pcie_enum_ctx enum_ctx = {};

	enum_ctx.probe_cb = probe_cb;
	enum_ctx.cb_ctx = cb_ctx;

	if (strlen(trid->traddr) != 0) { /* strlen(trid->traddr)为0 */
		if (spdk_pci_addr_parse(&enum_ctx.pci_addr, trid->traddr)) {
			return -1;
		}
		enum_ctx.has_pci_addr = true;
	}

	if (hotplug_fd < 0) {
		hotplug_fd = spdk_uevent_connect();
		if (hotplug_fd < 0) {
			SPDK_DEBUGLOG(SPDK_LOG_NVME, "Failed to open uevent netlink socket\n");
		}
	} else {
		_nvme_pcie_hotplug_monitor(cb_ctx, probe_cb, remove_cb);
	}

	if (enum_ctx.has_pci_addr == false) {
		return spdk_pci_nvme_enumerate(pcie_nvme_enum_cb, &enum_ctx);
	} else {
		return spdk_pci_nvme_device_attach(pcie_nvme_enum_cb, &enum_ctx, &enum_ctx.pci_addr);
	}
}

/**
 * PCI class code for NVMe devices.
 *
 * Base class code 01h: mass storage
 * Subclass code 08h: non-volatile memory
 * Programming interface 02h: NVM Express
 */
#define SPDK_PCI_CLASS_NVME		0x010802
#define SPDK_PCI_ANY_ID			0xffff


static struct rte_pci_id nvme_pci_driver_id[] = {
#if RTE_VERSION >= RTE_VERSION_NUM(16, 7, 0, 1)
	{
		.class_id = SPDK_PCI_CLASS_NVME,
		.vendor_id = PCI_ANY_ID,
		.device_id = PCI_ANY_ID,
		.subsystem_vendor_id = PCI_ANY_ID,
		.subsystem_device_id = PCI_ANY_ID,
	},
#else
	{RTE_PCI_DEVICE(0x8086, 0x0953)},
#endif
	{ .vendor_id = 0, /* sentinel */ },
};

struct spdk_pci_enum_ctx {
	struct rte_pci_driver	driver;
	spdk_pci_enum_cb	cb_fn;
	void			*cb_arg;
	pthread_mutex_t		mtx;
	bool			is_registered;
};


static struct spdk_pci_enum_ctx g_nvme_pci_drv = {
	.driver = {
		.drv_flags	= RTE_PCI_DRV_NEED_MAPPING,
		.id_table	= nvme_pci_driver_id,
#if RTE_VERSION >= RTE_VERSION_NUM(16, 11, 0, 0)
		.probe		= spdk_pci_device_init,
		.remove		= spdk_pci_device_fini,
		.driver.name	= "spdk_nvme",
#else
		.devinit	= spdk_pci_device_init,
		.devuninit	= spdk_pci_device_fini,
		.name		= "spdk_nvme",
#endif
	},

	.cb_fn = NULL,
	.cb_arg = NULL,
	.mtx = PTHREAD_MUTEX_INITIALIZER,
	.is_registered = false,
};


int
spdk_pci_nvme_enumerate(spdk_pci_enum_cb enum_cb, void *enum_ctx)
{
	return spdk_pci_enumerate(&g_nvme_pci_drv, enum_cb, enum_ctx);
}

int
spdk_pci_enumerate(struct spdk_pci_enum_ctx *ctx,
		   spdk_pci_enum_cb enum_cb,
		   void *enum_ctx)
{
	pthread_mutex_lock(&ctx->mtx);

	if (!ctx->is_registered) {
		ctx->is_registered = true;
#if RTE_VERSION >= RTE_VERSION_NUM(17, 05, 0, 4)
		/*  注册driver */
		rte_pci_register(&ctx->driver);
#else
		rte_eal_pci_register(&ctx->driver);
#endif
	}

	ctx->cb_fn = enum_cb;
	ctx->cb_arg = enum_ctx;

#if RTE_VERSION >= RTE_VERSION_NUM(17, 11, 0, 3)
	/* 会调用bus的probe函数 */
	if (rte_bus_probe() != 0) {
#elif RTE_VERSION >= RTE_VERSION_NUM(17, 05, 0, 4)
	if (rte_pci_probe() != 0) {
#else
	if (rte_eal_pci_probe() != 0) {
#endif
		ctx->cb_arg = NULL;
		ctx->cb_fn = NULL;
		pthread_mutex_unlock(&ctx->mtx);
		return -1;
	}

	ctx->cb_arg = NULL;
	ctx->cb_fn = NULL;
	pthread_mutex_unlock(&ctx->mtx);

	return 0;
}


struct rte_pci_bus rte_pci_bus = {
	.bus = {
		/*rte_bus_scan会调用scan，
		rte_pci_scan最终会执行TAILQ_INSERT_TAIL(&rte_pci_bus.device_list, pci_dev, next);
		*/
		.scan = rte_pci_scan,  
		.probe = rte_pci_probe,
		.find_device = pci_find_device,
		.plug = pci_plug,
		.unplug = pci_unplug,
		.parse = pci_parse,
		.get_iommu_class = rte_pci_get_iommu_class,
	},
	.device_list = TAILQ_HEAD_INITIALIZER(rte_pci_bus.device_list),
	.driver_list = TAILQ_HEAD_INITIALIZER(rte_pci_bus.driver_list),
};
RTE_REGISTER_BUS(pci, rte_pci_bus.bus);


/* register a driver */
void
rte_pci_register(struct rte_pci_driver *driver)
{
	TAILQ_INSERT_TAIL(&rte_pci_bus.driver_list, driver, next);
	driver->bus = &rte_pci_bus;
}

/*
 * Scan the content of the PCI bus, and call the probe() function for
 * all registered drivers that have a matching entry in its id_table
 * for discovered devices.
 */
int
rte_pci_probe(void)
{
	struct rte_pci_device *dev = NULL;
	size_t probed = 0, failed = 0;
	struct rte_devargs *devargs;
	int probe_all = 0;
	int ret = 0;

	if (rte_pci_bus.bus.conf.scan_mode != RTE_BUS_SCAN_WHITELIST)
		probe_all = 1;

	FOREACH_DEVICE_ON_PCIBUS(dev) {
		probed++;

		devargs = dev->device.devargs;
		/* probe all or only whitelisted devices */
		if (probe_all)
			ret = pci_probe_all_drivers(dev);
		else if (devargs != NULL &&
			devargs->policy == RTE_DEV_WHITELISTED)
			ret = pci_probe_all_drivers(dev);
		if (ret < 0) {
			RTE_LOG(ERR, EAL, "Requested device " PCI_PRI_FMT
				 " cannot be used\n", dev->addr.domain, dev->addr.bus,
				 dev->addr.devid, dev->addr.function);
			rte_errno = errno;
			failed++;
			ret = 0;
		}
	}

	return (probed && probed == failed) ? -1 : 0;
}

/* main->spdk_app_start->spdk_app_setup_env->spdk_env_init->rte_eal_init-->
 * rte_bus_scan-->rte_pci_scan-->pci_scan_one-->rte_pci_add_device会往
 * 添加元素rte_pci_bus.device_list
 */
/* PCI Bus iterators */
#define FOREACH_DEVICE_ON_PCIBUS(p)	\
		TAILQ_FOREACH(p, &(rte_pci_bus.device_list), next)


/* RTE_PMD_REGISTER_PCI和rte_pci_register会向rte_pci_bus.driver_list添加driver */
#define FOREACH_DRIVER_ON_PCIBUS(p)	\
		TAILQ_FOREACH(p, &(rte_pci_bus.driver_list), next)

/*
 * If vendor/device ID match, call the probe() function of all
 * registered driver for the given device. Return -1 if initialization
 * failed, return 1 if no driver is found for this device.
 */
static int
pci_probe_all_drivers(struct rte_pci_device *dev)
{
	struct rte_pci_driver *dr = NULL;
	int rc = 0;

	if (dev == NULL)
		return -1;

	/* Check if a driver is already loaded */
	if (dev->driver != NULL)
		return 0;

	FOREACH_DRIVER_ON_PCIBUS(dr) {
		rc = rte_pci_probe_one_driver(dr, dev);
			return -1;
		if (rc > 0)
			/* positive value means driver doesn't support it */
			continue;
		return 0;
	}
	return 1;
}

/* PRIx16 c库头文件里面定义的 */
/** Formatting string for PCI device identifier: Ex: 0000:00:01.0 */
#define PCI_PRI_FMT "%.4" PRIx16 ":%.2" PRIx8 ":%.2" PRIx8 ".%" PRIx8
#define PCI_PRI_STR_SIZE sizeof("XXXXXXXX:XX:XX.X")

/*
 * If vendor/device ID match, call the probe() function of the
 * driver.
 */
static int
rte_pci_probe_one_driver(struct rte_pci_driver *dr,
			 struct rte_pci_device *dev)
{
	int ret;
	struct rte_pci_addr *loc;

	loc = &dev->addr;

	/* The device is not blacklisted; Check if driver supports it */
	if (!rte_pci_match(dr, dev))
		/* Match of device and driver failed */
		return 1;

	RTE_LOG(INFO, EAL, "PCI device "PCI_PRI_FMT" on NUMA socket %i\n",
			loc->domain, loc->bus, loc->devid, loc->function,
			dev->device.numa_node);

	/* no initialization when blacklisted, return without error */
	if (dev->device.devargs != NULL && dev->device.devargs->policy == RTE_DEV_BLACKLISTED) {
		RTE_LOG(INFO, EAL, "  Device is blacklisted, not"" initializing\n");
		return 1;
	}

	if (dev->device.numa_node < 0) {
		RTE_LOG(WARNING, EAL, "  Invalid NUMA socket, default to 0\n");
		dev->device.numa_node = 0;
	}

	RTE_LOG(INFO, EAL, "  probe driver: %x:%x %s\n", dev->id.vendor_id,
		dev->id.device_id, dr->driver.name);

	if (dr->drv_flags & RTE_PCI_DRV_NEED_MAPPING) {
		/* map resources for devices that use igb_uio */
		/* 会进行uio    map给 uio_cfg_fd赋值 */
		ret = rte_pci_map_device(dev);
	}

	/* reference driver structure */
	dev->driver = dr;
	dev->device.driver = &dr->driver;

	/* call the driver probe() function */
	/* 以spdk_nvme的probe spdk_pci_device_init为例*/
	ret = dr->probe(dr, dev);
	return ret;
}

/*
 * Match the PCI Driver and Device using the ID Table
 */
int
rte_pci_match(const struct rte_pci_driver *pci_drv,
	      const struct rte_pci_device *pci_dev)
{
	const struct rte_pci_id *id_table;

	for (id_table = pci_drv->id_table; id_table->vendor_id != 0;
	     id_table++) {
		/* check if device's identifiers match the driver's ones */
		if (id_table->vendor_id != pci_dev->id.vendor_id &&
				id_table->vendor_id != PCI_ANY_ID)
			continue;
		if (id_table->device_id != pci_dev->id.device_id &&
				id_table->device_id != PCI_ANY_ID)
			continue;
		if (id_table->subsystem_vendor_id !=
		    pci_dev->id.subsystem_vendor_id &&
		    id_table->subsystem_vendor_id != PCI_ANY_ID)
			continue;
		if (id_table->subsystem_device_id !=
		    pci_dev->id.subsystem_device_id &&
		    id_table->subsystem_device_id != PCI_ANY_ID)
			continue;
		if (id_table->class_id != pci_dev->id.class_id &&
				id_table->class_id != RTE_CLASS_ANY_ID)
			continue;

		return 1;
	}

	return 0;
}

/* Map pci device */
int
rte_pci_map_device(struct rte_pci_device *dev)
{
	int ret = -1;

	/* try mapping the NIC resources using VFIO if it exists */
	switch (dev->kdrv) {
	case RTE_KDRV_VFIO:
#ifdef VFIO_PRESENT
		if (pci_vfio_is_enabled())
			ret = pci_vfio_map_resource(dev);
#endif
		break;
	case RTE_KDRV_IGB_UIO:
	case RTE_KDRV_UIO_GENERIC:
		if (rte_eal_using_phys_addrs()) {
			/* map resources for devices that use uio */
			ret = pci_uio_map_resource(dev);
		}
		break;
	default:
		RTE_LOG(DEBUG, EAL,
			"  Not managed by a supported kernel driver, skipped\n");
		ret = 1;
		break;
	}

	return ret;
}

/* map the PCI resource of a PCI device in virtual memory */
int
pci_uio_map_resource(struct rte_pci_device *dev)
{
	int i, map_idx = 0, ret;
	uint64_t phaddr;
	struct mapped_pci_resource *uio_res = NULL;
	struct mapped_pci_res_list *uio_res_list =
		RTE_TAILQ_CAST(rte_uio_tailq.head, mapped_pci_res_list);

	dev->intr_handle.fd = -1;
	dev->intr_handle.uio_cfg_fd = -1;
	dev->intr_handle.type = RTE_INTR_HANDLE_UNKNOWN;

	/* secondary processes - use already recorded details */
	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
		return pci_uio_map_secondary(dev);

	/* allocate uio resource */
	ret = pci_uio_alloc_resource(dev, &uio_res);

	/* Map all BARs */
	for (i = 0; i != PCI_MAX_RESOURCE; i++) {
		/* skip empty BAR */
		phaddr = dev->mem_resource[i].phys_addr;
		if (phaddr == 0)
			continue;

		ret = pci_uio_map_resource_by_index(dev, i, uio_res, map_idx);

		map_idx++;
	}

	uio_res->nb_maps = map_idx;

	TAILQ_INSERT_TAIL(uio_res_list, uio_res, next);

	return 0;
}

int
pci_uio_alloc_resource(struct rte_pci_device *dev, struct mapped_pci_resource **uio_res)
{
	char dirname[PATH_MAX];
	char cfgname[PATH_MAX];
	char devname[PATH_MAX]; /* contains the /dev/uioX */
	int uio_num;
	struct rte_pci_addr *loc;

	loc = &dev->addr;

	/* find uio resource */
	uio_num = pci_get_uio_dev(dev, dirname, sizeof(dirname), 1);

	snprintf(devname, sizeof(devname), "/dev/uio%u", uio_num);

	/* save fd if in primary process */
	dev->intr_handle.fd = open(devname, O_RDWR);

	snprintf(cfgname, sizeof(cfgname), "/sys/class/uio/uio%u/device/config", uio_num);
	dev->intr_handle.uio_cfg_fd = open(cfgname, O_RDWR);

	if (dev->kdrv == RTE_KDRV_IGB_UIO)
		dev->intr_handle.type = RTE_INTR_HANDLE_UIO;
	else {
		dev->intr_handle.type = RTE_INTR_HANDLE_UIO_INTX;

		/* set bus master that is not done by uio_pci_generic */
		if (pci_uio_set_bus_master(dev->intr_handle.uio_cfg_fd)) {
			RTE_LOG(ERR, EAL, "Cannot set up bus mastering!\n");
			goto error;
		}
	}

	/* allocate the mapping details for secondary processes*/
	*uio_res = rte_zmalloc("UIO_RES", sizeof(**uio_res), 0);

	snprintf((*uio_res)->path, sizeof((*uio_res)->path), "%s", devname);
	memcpy(&(*uio_res)->pci_addr, &dev->addr, sizeof((*uio_res)->pci_addr));

	return 0;
}


void *pci_map_addr = NULL;

int
pci_uio_map_resource_by_index(struct rte_pci_device *dev, int res_idx,
		struct mapped_pci_resource *uio_res, int map_idx)
{
	int fd;
	char devname[PATH_MAX];
	void *mapaddr;
	struct rte_pci_addr *loc;
	struct pci_map *maps;

	loc = &dev->addr;
	maps = uio_res->maps;

	/* update devname for mmap  */
	snprintf(devname, sizeof(devname), "%s/" PCI_PRI_FMT "/resource%d",
			rte_pci_get_sysfs_path(),
			loc->domain, loc->bus, loc->devid,
			loc->function, res_idx);

	/* allocate memory to keep path */
	maps[map_idx].path = rte_malloc(NULL, strlen(devname) + 1, 0);

	/*
	 * open resource file, to mmap it
	 */
	fd = open(devname, O_RDWR);

	/* try mapping somewhere close to the end of hugepages */
	if (pci_map_addr == NULL)
		pci_map_addr = pci_find_max_end_va();

	mapaddr = pci_map_resource(pci_map_addr, fd, 0, (size_t)dev->mem_resource[res_idx].len, 0);
	close(fd);

	pci_map_addr = RTE_PTR_ADD(mapaddr, (size_t)dev->mem_resource[res_idx].len);

	maps[map_idx].phaddr = dev->mem_resource[res_idx].phys_addr;
	maps[map_idx].size = dev->mem_resource[res_idx].len;
	maps[map_idx].addr = mapaddr;
	maps[map_idx].offset = 0;
	strcpy(maps[map_idx].path, devname);
	dev->mem_resource[res_idx].addr = mapaddr;

	return 0;

error:
	rte_free(maps[map_idx].path);
	return -1;
}

/* map a particular resource from a file */
void *
pci_map_resource(void *requested_addr, int fd, off_t offset, size_t size,
		 int additional_flags)
{
	void *mapaddr;

	/* Map the PCI memory resource of device */
	mapaddr = mmap(requested_addr, size, PROT_READ | PROT_WRITE,
			MAP_SHARED | additional_flags, fd, offset);
	if (mapaddr == MAP_FAILED) {
	} else
		RTE_LOG(DEBUG, EAL, "  PCI memory mapped at %p\n", mapaddr);

	return mapaddr;
}


int
spdk_pci_device_init(struct rte_pci_driver *driver,
		     struct rte_pci_device *device)
{
	/*  这个用法真奇葩，虽然ctx的第一个成员是driver */
	struct spdk_pci_enum_ctx *ctx = (struct spdk_pci_enum_ctx *)driver;
	int rc;

	if (device->kdrv == RTE_KDRV_VFIO) {
		/*
		 * TODO: This is a workaround for an issue where the device is not ready after VFIO reset.
		 * Figure out what is actually going wrong and remove this sleep.
		 */
		usleep(500 * 1000);
	}

	/* 调用 pcie_nvme_enum_cb    ctx->cb_arg = enum_ctx*/
	rc = ctx->cb_fn(ctx->cb_arg, (struct spdk_pci_device *)device);

	spdk_vtophys_pci_device_added(device);
	return 0;
}

static int
pcie_nvme_enum_cb(void *ctx, struct spdk_pci_device *pci_dev)
{
	struct spdk_nvme_transport_id trid = {};
	struct nvme_pcie_enum_ctx *enum_ctx = ctx;
	struct spdk_nvme_ctrlr *ctrlr;
	struct spdk_pci_addr pci_addr;

	pci_addr = spdk_pci_device_get_addr(pci_dev);

	trid.trtype = SPDK_NVME_TRANSPORT_PCIE;
	spdk_pci_addr_fmt(trid.traddr, sizeof(trid.traddr), &pci_addr);

	/* Verify that this controller is not already attached */
	ctrlr = spdk_nvme_get_ctrlr_by_trid_unsafe(&trid);
	if (ctrlr) {
		if (spdk_process_is_primary()) {
			/* Already attached */
			return 0;
		} else {
			return nvme_ctrlr_add_process(ctrlr, pci_dev);
		}
	}

	/* check whether user passes the pci_addr */
	if (enum_ctx->has_pci_addr && (spdk_pci_addr_compare(&pci_addr, &enum_ctx->pci_addr) != 0)) {
		return 1;
	}

	return nvme_ctrlr_probe(&trid, pci_dev,
				enum_ctx->probe_cb, enum_ctx->cb_ctx);
}

struct spdk_pci_addr
spdk_pci_device_get_addr(struct spdk_pci_device *pci_dev)
{
	struct spdk_pci_addr pci_addr;

	pci_addr.domain = spdk_pci_device_get_domain(pci_dev);
	pci_addr.bus = spdk_pci_device_get_bus(pci_dev);
	pci_addr.dev = spdk_pci_device_get_dev(pci_dev);
	pci_addr.func = spdk_pci_device_get_func(pci_dev);

	return pci_addr;
}

int
spdk_pci_addr_fmt(char *bdf, size_t sz, const struct spdk_pci_addr *addr)
{
	int rc;

	rc = snprintf(bdf, sz, "%04x:%02x:%02x.%x",
		      addr->domain, addr->bus,
		      addr->dev, addr->func);

	if (rc > 0 && (size_t)rc < sz) {
		return 0;
	}

	return -1;
}

static TAILQ_HEAD(, spdk_nvme_ctrlr) g_nvme_init_ctrlrs =
	TAILQ_HEAD_INITIALIZER(g_nvme_init_ctrlrs);


int
nvme_ctrlr_probe(const struct spdk_nvme_transport_id *trid, void *devhandle,
		 spdk_nvme_probe_cb probe_cb, void *cb_ctx)
{
	struct spdk_nvme_ctrlr *ctrlr;
	struct spdk_nvme_ctrlr_opts opts;

	assert(trid != NULL);

	spdk_nvme_ctrlr_get_default_ctrlr_opts(&opts, sizeof(opts));
	/* 调用 probe_cb */
	if (!probe_cb || probe_cb(cb_ctx, trid, &opts)) {
		ctrlr = nvme_transport_ctrlr_construct(trid, &opts, devhandle);

		/* 将该ctrlr添加到g_nvme_init_ctrlrs链表 */
		TAILQ_INSERT_TAIL(&g_nvme_init_ctrlrs, ctrlr, tailq);
		return 0;
	}

	return 1;
}

void
spdk_nvme_ctrlr_get_default_ctrlr_opts(struct spdk_nvme_ctrlr_opts *opts, size_t opts_size)
{
	char host_id_str[SPDK_UUID_STRING_LEN];

	memset(opts, 0, opts_size);

#define FIELD_OK(field) \
	offsetof(struct spdk_nvme_ctrlr_opts, field) + sizeof(opts->field) <= opts_size

	if (FIELD_OK(num_io_queues)) {
		opts->num_io_queues = DEFAULT_MAX_IO_QUEUES;
	}

	if (FIELD_OK(use_cmb_sqs)) {
		opts->use_cmb_sqs = true;
	}

	if (FIELD_OK(arb_mechanism)) {
		opts->arb_mechanism = SPDK_NVME_CC_AMS_RR;
	}

	if (FIELD_OK(keep_alive_timeout_ms)) {
		opts->keep_alive_timeout_ms = 10 * 1000;
	}

	if (FIELD_OK(io_queue_size)) {
		opts->io_queue_size = DEFAULT_IO_QUEUE_SIZE;
	}

	if (FIELD_OK(io_queue_requests)) {
		opts->io_queue_requests = DEFAULT_IO_QUEUE_REQUESTS;
	}

	if (FIELD_OK(host_id)) {
		memset(opts->host_id, 0, sizeof(opts->host_id));
	}

	if (FIELD_OK(extended_host_id)) {
		memcpy(opts->extended_host_id, &g_spdk_nvme_driver->default_extended_host_id,
		       sizeof(opts->extended_host_id));
	}

	if (FIELD_OK(hostnqn)) {
		spdk_uuid_fmt_lower(host_id_str, sizeof(host_id_str),
				    &g_spdk_nvme_driver->default_extended_host_id);
		snprintf(opts->hostnqn, sizeof(opts->hostnqn), "2014-08.org.nvmexpress:uuid:%s", host_id_str);
	}

	if (FIELD_OK(src_addr)) {
		memset(opts->src_addr, 0, sizeof(opts->src_addr));
	}

	if (FIELD_OK(src_svcid)) {
		memset(opts->src_svcid, 0, sizeof(opts->src_svcid));
	}
#undef FIELD_OK
}

static bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{

	SPDK_DEBUGLOG(SPDK_LOG_BDEV_NVME, "Probing device %s\n", trid->traddr);

	if (nvme_ctrlr_get(trid)) {
		SPDK_ERRLOG("A controller with the provided trid (traddr: %s) already exists.\n",
			    trid->traddr);
		return false;
	}

	if (trid->trtype == SPDK_NVME_TRANSPORT_PCIE) {
		bool claim_device = false;
		struct nvme_probe_ctx *ctx = cb_ctx;
		size_t i;

		for (i = 0; i < ctx->count; i++) {
			/* ctx->trids[i]为从/usr/local/etc/spdk/vhost.conf解析出来的的BDF地址 */
			if (spdk_nvme_transport_id_compare(trid, &ctx->trids[i]) == 0) {
				claim_device = true;
				break;
			}
		}
	}

	return true;
}

static struct nvme_ctrlr *
nvme_ctrlr_get(const struct spdk_nvme_transport_id *trid)
{
	struct nvme_ctrlr	*nvme_ctrlr;

	TAILQ_FOREACH(nvme_ctrlr, &g_nvme_ctrlrs, tailq) {
		if (spdk_nvme_transport_id_compare(trid, &nvme_ctrlr->trid) == 0) {
			return nvme_ctrlr;
		}
	}

	return NULL;
}

int
spdk_nvme_transport_id_compare(const struct spdk_nvme_transport_id *trid1,
			       const struct spdk_nvme_transport_id *trid2)
{
	int cmp;

	cmp = cmp_int(trid1->trtype, trid2->trtype);

	if (trid1->trtype == SPDK_NVME_TRANSPORT_PCIE) {
		struct spdk_pci_addr pci_addr1;
		struct spdk_pci_addr pci_addr2;

		/* Normalize PCI addresses before comparing */
		if (spdk_pci_addr_parse(&pci_addr1, trid1->traddr) < 0 ||
		    spdk_pci_addr_parse(&pci_addr2, trid2->traddr) < 0) {
			return -1;
		}

		/* PCIe transport ID only uses trtype and traddr */
		return spdk_pci_addr_compare(&pci_addr1, &pci_addr2);
	}

	cmp = strcasecmp(trid1->traddr, trid2->traddr);
	cmp = cmp_int(trid1->adrfam, trid2->adrfam);
	cmp = strcasecmp(trid1->trsvcid, trid2->trsvcid);
	cmp = strcmp(trid1->subnqn, trid2->subnqn);

	return 0;
}

struct spdk_nvme_ctrlr *nvme_transport_ctrlr_construct(const struct spdk_nvme_transport_id *trid,
		const struct spdk_nvme_ctrlr_opts *opts,
		void *devhandle)
{
	/* 调用nvme_pcie_ctrlr_construct */
	NVME_TRANSPORT_CALL(trid->trtype, ctrlr_construct, (trid, opts, devhandle));
}

struct spdk_nvme_ctrlr *nvme_pcie_ctrlr_construct(const struct spdk_nvme_transport_id *trid,
		const struct spdk_nvme_ctrlr_opts *opts,
		void *devhandle)
{
	struct spdk_pci_device *pci_dev = devhandle;
	struct nvme_pcie_ctrlr *pctrlr;
	union spdk_nvme_cap_register cap;
	uint32_t cmd_reg;
	int rc, claim_fd;
	struct spdk_pci_id pci_id;
	struct spdk_pci_addr pci_addr;

	if (spdk_pci_addr_parse(&pci_addr, trid->traddr)) {
		SPDK_ERRLOG("could not parse pci address\n");
		return NULL;
	}

	claim_fd = spdk_pci_device_claim(&pci_addr);

	/* 申请pctrlr内存 */
	pctrlr = spdk_dma_zmalloc(sizeof(struct nvme_pcie_ctrlr), 64, NULL);

	pctrlr->is_remapped = false;
	pctrlr->ctrlr.is_removed = false;
	pctrlr->ctrlr.trid.trtype = SPDK_NVME_TRANSPORT_PCIE;
	pctrlr->devhandle = devhandle;
	pctrlr->ctrlr.opts = *opts;
	pctrlr->claim_fd = claim_fd;
	memcpy(&pctrlr->ctrlr.trid, trid, sizeof(pctrlr->ctrlr.trid));

	rc = nvme_pcie_ctrlr_allocate_bars(pctrlr);

	/* Enable PCI busmaster and disable INTx */
	spdk_pci_device_cfg_read32(pci_dev, &cmd_reg, 4);
	cmd_reg |= 0x404;
	spdk_pci_device_cfg_write32(pci_dev, cmd_reg, 4);

	if (nvme_ctrlr_get_cap(&pctrlr->ctrlr, &cap)) {
		SPDK_ERRLOG("get_cap() failed\n");
		close(claim_fd);
		spdk_dma_free(pctrlr);
		return NULL;
	}

	nvme_ctrlr_init_cap(&pctrlr->ctrlr, &cap);

	/* Doorbell stride is 2 ^ (dstrd + 2),
	 * but we want multiples of 4, so drop the + 2 */
	pctrlr->doorbell_stride_u32 = 1 << cap.bits.dstrd;

	rc = nvme_ctrlr_construct(&pctrlr->ctrlr);

	pci_id = spdk_pci_device_get_id(pci_dev);
	pctrlr->ctrlr.quirks = nvme_get_quirks(&pci_id);

	rc = nvme_pcie_ctrlr_construct_admin_qpair(&pctrlr->ctrlr);

	/* Construct the primary process properties */
	rc = nvme_ctrlr_add_process(&pctrlr->ctrlr, pci_dev);


	if (g_sigset != true) {
		nvme_pcie_ctrlr_setup_signal();
		g_sigset = true;
	}

	return &pctrlr->ctrlr;
}

int
spdk_pci_device_claim(const struct spdk_pci_addr *pci_addr)
{
	int dev_fd;
	char dev_name[64];
	int pid;
	void *dev_map;
	struct flock pcidev_lock = {
		.l_type = F_WRLCK,
		.l_whence = SEEK_SET,
		.l_start = 0,
		.l_len = 0,
	};

	snprintf(dev_name, sizeof(dev_name), "/tmp/spdk_pci_lock_%04x:%02x:%02x.%x", pci_addr->domain,
		 pci_addr->bus,
		 pci_addr->dev, pci_addr->func);

	dev_fd = open(dev_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

	/* ftruncate()会将参数fd指定的文件大小改为参数length指定的大小 */
	if (ftruncate(dev_fd, sizeof(int)) != 0) {
		fprintf(stderr, "could not truncate %s\n", dev_name);
		close(dev_fd);
		return -1;
	}

	dev_map = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, dev_fd, 0);

	if (fcntl(dev_fd, F_SETLK, &pcidev_lock) != 0) {
	}

	/* 将pid保存到dev_map地址中 */
	*(int *)dev_map = (int)getpid();
	munmap(dev_map, sizeof(int));
	/* Keep dev_fd open to maintain the lock. */
	return dev_fd;
}

static int
nvme_pcie_ctrlr_allocate_bars(struct nvme_pcie_ctrlr *pctrlr)
{
	int rc;
	void *addr;
	uint64_t phys_addr, size;

	rc = spdk_pci_device_map_bar(pctrlr->devhandle, 0, &addr, &phys_addr, &size);
	pctrlr->regs = (volatile struct spdk_nvme_registers *)addr;


	pctrlr->regs_size = size;
	nvme_pcie_ctrlr_map_cmb(pctrlr);

	return 0;
}

int
spdk_pci_device_map_bar(struct spdk_pci_device *device, uint32_t bar,
			void **mapped_addr, uint64_t *phys_addr, uint64_t *size)
{
	struct rte_pci_device *dev = device;

	*mapped_addr = dev->mem_resource[bar].addr;
	*phys_addr = (uint64_t)dev->mem_resource[bar].phys_addr;
	*size = (uint64_t)dev->mem_resource[bar].len;

	return 0;
}


static void
nvme_pcie_ctrlr_map_cmb(struct nvme_pcie_ctrlr *pctrlr)
{
	int rc;
	void *addr;
	uint32_t bir;
	union spdk_nvme_cmbsz_register cmbsz;
	union spdk_nvme_cmbloc_register cmbloc;
	uint64_t size, unit_size, offset, bar_size, bar_phys_addr;
	uint64_t mem_register_start, mem_register_end;

	if (nvme_pcie_ctrlr_get_cmbsz(pctrlr, &cmbsz) ||
	    nvme_pcie_ctrlr_get_cmbloc(pctrlr, &cmbloc)) {
		SPDK_ERRLOG("get registers failed\n");
		goto exit;
	}


	bir = cmbloc.bits.bir;
	/* Values 0 2 3 4 5 are valid for BAR */

	/* unit size for 4KB/64KB/1MB/16MB/256MB/4GB/64GB */
	unit_size = (uint64_t)1 << (12 + 4 * cmbsz.bits.szu);
	/* controller memory buffer size in Bytes */
	size = unit_size * cmbsz.bits.sz;
	/* controller memory buffer offset from BAR in Bytes */
	offset = unit_size * cmbloc.bits.ofst;

	rc = spdk_pci_device_map_bar(pctrlr->devhandle, bir, &addr, &bar_phys_addr, &bar_size);

	pctrlr->cmb_bar_virt_addr = addr;
	pctrlr->cmb_bar_phys_addr = bar_phys_addr;
	pctrlr->cmb_size = size;
	pctrlr->cmb_current_offset = offset;
	pctrlr->cmb_max_offset = offset + size;

	if (!cmbsz.bits.sqs) {
		pctrlr->ctrlr.opts.use_cmb_sqs = false;
	}

	/* If only SQS is supported use legacy mapping */
	if (cmbsz.bits.sqs && !(cmbsz.bits.wds || cmbsz.bits.rds)) {
		return;
	}

	/* If CMB is less than 4MiB in size then abort CMB mapping */
	if (pctrlr->cmb_size < (1ULL << 22)) {
		goto exit;
	}

	mem_register_start = (((uintptr_t)pctrlr->cmb_bar_virt_addr + offset + 0x1fffff) & ~(0x200000 - 1));
	mem_register_end = ((uintptr_t)pctrlr->cmb_bar_virt_addr + offset + pctrlr->cmb_size);
	mem_register_end &= ~(uint64_t)(0x200000 - 1);
	pctrlr->cmb_mem_register_addr = (void *)mem_register_start;
	pctrlr->cmb_mem_register_size = mem_register_end - mem_register_start;

	rc = spdk_mem_register(pctrlr->cmb_mem_register_addr, pctrlr->cmb_mem_register_size);

	pctrlr->cmb_current_offset = mem_register_start - ((uint64_t)pctrlr->cmb_bar_virt_addr);
	pctrlr->cmb_max_offset = mem_register_end - ((uint64_t)pctrlr->cmb_bar_virt_addr);
	pctrlr->cmb_io_data_supported = true;

	return;
}

static int
nvme_pcie_ctrlr_get_cmbsz(struct nvme_pcie_ctrlr *pctrlr, union spdk_nvme_cmbsz_register *cmbsz)
{
	return nvme_pcie_ctrlr_get_reg_4(&pctrlr->ctrlr, offsetof(struct spdk_nvme_registers, cmbsz.raw),
					 &cmbsz->raw);
}


int
spdk_mem_register(void *vaddr, size_t len)
{
	struct spdk_mem_map *map;
	int rc;
	void *seg_vaddr;
	size_t seg_len;

	if ((uintptr_t)vaddr & ~MASK_256TB) {
		DEBUG_PRINT("invalid usermode virtual address %p\n", vaddr);
		return -EINVAL;
	}

	if (((uintptr_t)vaddr & MASK_2MB) || (len & MASK_2MB)) {
		DEBUG_PRINT("invalid %s parameters, vaddr=%p len=%ju\n",
			    __func__, vaddr, len);
		return -EINVAL;
	}

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

/* This function should be called once at ctrlr initialization to set up constant properties. */
void
nvme_ctrlr_init_cap(struct spdk_nvme_ctrlr *ctrlr, const union spdk_nvme_cap_register *cap)
{
	ctrlr->cap = *cap;

	ctrlr->min_page_size = 1u << (12 + ctrlr->cap.bits.mpsmin);

	/* For now, always select page_size == min_page_size. */
	ctrlr->page_size = ctrlr->min_page_size;

	ctrlr->opts.io_queue_size = spdk_max(ctrlr->opts.io_queue_size, SPDK_NVME_IO_QUEUE_MIN_ENTRIES);
	ctrlr->opts.io_queue_size = spdk_min(ctrlr->opts.io_queue_size, ctrlr->cap.bits.mqes + 1u);

	ctrlr->opts.io_queue_requests = spdk_max(ctrlr->opts.io_queue_requests, ctrlr->opts.io_queue_size);
}

int
nvme_ctrlr_construct(struct spdk_nvme_ctrlr *ctrlr)
{
	int rc;

	nvme_ctrlr_set_state(ctrlr, NVME_CTRLR_STATE_INIT, NVME_TIMEOUT_INFINITE);
	ctrlr->flags = 0;
	ctrlr->free_io_qids = NULL;
	ctrlr->is_resetting = false;
	ctrlr->is_failed = false;

	TAILQ_INIT(&ctrlr->active_io_qpairs);
	STAILQ_INIT(&ctrlr->queued_aborts);
	ctrlr->outstanding_aborts = 0;

	rc = nvme_robust_mutex_init_recursive_shared(&ctrlr->ctrlr_lock);

	TAILQ_INIT(&ctrlr->active_procs);

	return rc;
}

static void
nvme_ctrlr_set_state(struct spdk_nvme_ctrlr *ctrlr, enum nvme_ctrlr_state state,
		     uint64_t timeout_in_ms)
{
	ctrlr->state = state;
	if (timeout_in_ms == NVME_TIMEOUT_INFINITE) {
		SPDK_DEBUGLOG(SPDK_LOG_NVME, "setting state to %s (no timeout)\n",
			      nvme_ctrlr_state_string(ctrlr->state));
		ctrlr->state_timeout_tsc = NVME_TIMEOUT_INFINITE;
	} else {
		SPDK_DEBUGLOG(SPDK_LOG_NVME, "setting state to %s (timeout %" PRIu64 " ms)\n",
			      nvme_ctrlr_state_string(ctrlr->state), timeout_in_ms);
		ctrlr->state_timeout_tsc = spdk_get_ticks() + (timeout_in_ms * spdk_get_ticks_hz()) / 1000;
	}
}

struct spdk_pci_id
spdk_pci_device_get_id(struct spdk_pci_device *pci_dev)
{
	struct spdk_pci_id pci_id;

	pci_id.vendor_id = spdk_pci_device_get_vendor_id(pci_dev);
	pci_id.device_id = spdk_pci_device_get_device_id(pci_dev);
	pci_id.subvendor_id = spdk_pci_device_get_subvendor_id(pci_dev);
	pci_id.subdevice_id = spdk_pci_device_get_subdevice_id(pci_dev);

	return pci_id;
}

static const struct nvme_quirk nvme_quirks[] = {
	{	{SPDK_PCI_VID_INTEL, 0x0953, SPDK_PCI_ANY_ID, SPDK_PCI_ANY_ID},
		NVME_INTEL_QUIRK_READ_LATENCY |
		NVME_INTEL_QUIRK_WRITE_LATENCY |
		NVME_INTEL_QUIRK_STRIPING |
		NVME_QUIRK_READ_ZERO_AFTER_DEALLOCATE
	},
	{	{SPDK_PCI_VID_INTEL, 0x0A53, SPDK_PCI_ANY_ID, SPDK_PCI_ANY_ID},
		NVME_INTEL_QUIRK_READ_LATENCY |
		NVME_INTEL_QUIRK_WRITE_LATENCY |
		NVME_INTEL_QUIRK_STRIPING |
		NVME_QUIRK_READ_ZERO_AFTER_DEALLOCATE
	},
	{	{SPDK_PCI_VID_INTEL, 0x0A54, SPDK_PCI_ANY_ID, SPDK_PCI_ANY_ID},
		NVME_INTEL_QUIRK_READ_LATENCY |
		NVME_INTEL_QUIRK_WRITE_LATENCY |
		NVME_INTEL_QUIRK_STRIPING |
		NVME_QUIRK_READ_ZERO_AFTER_DEALLOCATE
	},
	{	{SPDK_PCI_VID_INTEL, 0x0A55, SPDK_PCI_ANY_ID, SPDK_PCI_ANY_ID},
		NVME_INTEL_QUIRK_READ_LATENCY |
		NVME_INTEL_QUIRK_WRITE_LATENCY |
		NVME_INTEL_QUIRK_STRIPING |
		NVME_QUIRK_READ_ZERO_AFTER_DEALLOCATE
	},
	{	{SPDK_PCI_VID_MEMBLAZE, 0x0540, SPDK_PCI_ANY_ID, SPDK_PCI_ANY_ID},
		NVME_QUIRK_DELAY_BEFORE_CHK_RDY
	},
	{	{SPDK_PCI_VID_VIRTUALBOX, 0x4e56, SPDK_PCI_ANY_ID, SPDK_PCI_ANY_ID},
		NVME_QUIRK_DELAY_AFTER_QUEUE_ALLOC
	},
	{	{0x0000, 0x0000, 0x0000, 0x0000}, 0}
};


uint64_t
nvme_get_quirks(const struct spdk_pci_id *id)
{
	const struct nvme_quirk *quirk = nvme_quirks;

	SPDK_DEBUGLOG(SPDK_LOG_NVME, "Searching for %04x:%04x [%04x:%04x]...\n",
		      id->vendor_id, id->device_id,
		      id->subvendor_id, id->subdevice_id);

	while (quirk->id.vendor_id) {
		if (pci_id_match(&quirk->id, id)) {
			SPDK_DEBUGLOG(SPDK_LOG_NVME, "Matched quirk %04x:%04x [%04x:%04x]:\n",
				      quirk->id.vendor_id, quirk->id.device_id,
				      quirk->id.subvendor_id, quirk->id.subdevice_id);

#define PRINT_QUIRK(quirk_flag) \
			do { \
				if (quirk->flags & (quirk_flag)) { \
					SPDK_DEBUGLOG(SPDK_LOG_NVME, "Quirk enabled: %s\n", #quirk_flag); \
				} \
			} while (0)

			PRINT_QUIRK(NVME_INTEL_QUIRK_READ_LATENCY);
			PRINT_QUIRK(NVME_INTEL_QUIRK_WRITE_LATENCY);
			PRINT_QUIRK(NVME_QUIRK_DELAY_BEFORE_CHK_RDY);
			PRINT_QUIRK(NVME_INTEL_QUIRK_STRIPING);
			PRINT_QUIRK(NVME_QUIRK_DELAY_AFTER_QUEUE_ALLOC);
			PRINT_QUIRK(NVME_QUIRK_READ_ZERO_AFTER_DEALLOCATE);

			return quirk->flags;
		}
		quirk++;
	}

	SPDK_DEBUGLOG(SPDK_LOG_NVME, "No quirks found.\n");

	return 0;
}

enum spdk_nvme_qprio {
	SPDK_NVME_QPRIO_URGENT		= 0x0,
	SPDK_NVME_QPRIO_HIGH		= 0x1,
	SPDK_NVME_QPRIO_MEDIUM		= 0x2,
	SPDK_NVME_QPRIO_LOW		= 0x3
};

#define NVME_ADMIN_ENTRIES	(128)

static int
nvme_pcie_ctrlr_construct_admin_qpair(struct spdk_nvme_ctrlr *ctrlr)
{
	struct nvme_pcie_qpair *pqpair;
	int rc;

	pqpair = spdk_dma_zmalloc(sizeof(*pqpair), 64, NULL);

	pqpair->num_entries = NVME_ADMIN_ENTRIES;

	ctrlr->adminq = &pqpair->qpair;

	/* 构建admin qpair */
	rc = nvme_qpair_init(ctrlr->adminq,
			     0, /* qpair ID */
			     ctrlr,
			     SPDK_NVME_QPRIO_URGENT,
			     NVME_ADMIN_ENTRIES);

	return nvme_pcie_qpair_construct(ctrlr->adminq);
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

	/* 分配req_buf */
	qpair->req_buf = spdk_dma_zmalloc(req_size_padded * num_requests, 64, NULL);

	for (i = 0; i < num_requests; i++) {
		struct nvme_request *req = qpair->req_buf + i * req_size_padded;
		/* 将req_buf添加到qpair->free_req中 */
		STAILQ_INSERT_HEAD(&qpair->free_req, req, stailq);
	}

	return 0;
}

static inline struct nvme_pcie_ctrlr *
nvme_pcie_ctrlr(struct spdk_nvme_ctrlr *ctrlr)
{
	return (struct nvme_pcie_ctrlr *)((uintptr_t)ctrlr - offsetof(struct nvme_pcie_ctrlr, ctrlr));
}

static inline struct nvme_pcie_qpair *
nvme_pcie_qpair(struct spdk_nvme_qpair *qpair)
{
	return (struct nvme_pcie_qpair *)((uintptr_t)qpair - offsetof(struct nvme_pcie_qpair, qpair));
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

static void
nvme_qpair_construct_tracker(struct nvme_tracker *tr, uint16_t cid, uint64_t phys_addr)
{
	tr->prp_sgl_bus_addr = phys_addr + offsetof(struct nvme_tracker, u.prp);
	tr->cid = cid;
	tr->active = false;
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

	memset(pqpair->cmd, 0, pqpair->num_entries * sizeof(struct spdk_nvme_cmd));
	memset(pqpair->cpl, 0, pqpair->num_entries * sizeof(struct spdk_nvme_cpl));

	return 0;
}

int
nvme_ctrlr_add_process(struct spdk_nvme_ctrlr *ctrlr, void *devhandle)
{
	struct spdk_nvme_ctrlr_process	*ctrlr_proc, *active_proc;
	pid_t				pid = getpid();

	/* Check whether the process is already added or not */
	TAILQ_FOREACH(active_proc, &ctrlr->active_procs, tailq) {
		if (active_proc->pid == pid) {
			return 0;
		}
	}

	/* Initialize the per process properties for this ctrlr */
	ctrlr_proc = spdk_dma_zmalloc(sizeof(struct spdk_nvme_ctrlr_process), 64, NULL);

	ctrlr_proc->is_primary = spdk_process_is_primary();
	ctrlr_proc->pid = pid;
	STAILQ_INIT(&ctrlr_proc->active_reqs);
	ctrlr_proc->devhandle = devhandle;
	ctrlr_proc->ref = 0;
	TAILQ_INIT(&ctrlr_proc->allocated_io_qpairs);

	/* 将ctrlr_proc添加到ctrlr->active_procs中 */
	TAILQ_INSERT_TAIL(&ctrlr->active_procs, ctrlr_proc, tailq);

	return 0;
}

static int
nvme_init_controllers(void *cb_ctx, spdk_nvme_attach_cb attach_cb)
{
	int rc = 0;
	int start_rc;
	struct spdk_nvme_ctrlr *ctrlr, *ctrlr_tmp;

	nvme_robust_mutex_lock(&g_spdk_nvme_driver->lock);

	/* Initialize all new controllers in the g_nvme_init_ctrlrs list in parallel. */
	while (!TAILQ_EMPTY(&g_nvme_init_ctrlrs)) {
		TAILQ_FOREACH_SAFE(ctrlr, &g_nvme_init_ctrlrs, tailq, ctrlr_tmp) {
			/* Drop the driver lock while calling nvme_ctrlr_process_init()
			 *  since it needs to acquire the driver lock internally when calling
			 *  nvme_ctrlr_start().
			 *
			 * TODO: Rethink the locking - maybe reset should take the lock so that start() and
			 *  the functions it calls (in particular nvme_ctrlr_set_num_qpairs())
			 *  can assume it is held.
			 */
			nvme_robust_mutex_unlock(&g_spdk_nvme_driver->lock);
			start_rc = nvme_ctrlr_process_init(ctrlr);
			nvme_robust_mutex_lock(&g_spdk_nvme_driver->lock);

			if (start_rc) {
				/* Controller failed to initialize. */
				TAILQ_REMOVE(&g_nvme_init_ctrlrs, ctrlr, tailq);
				SPDK_ERRLOG("Failed to initialize SSD: %s\n", ctrlr->trid.traddr);
				nvme_ctrlr_destruct(ctrlr);
				continue;
			}

			if (ctrlr->state == NVME_CTRLR_STATE_READY) {
				/*
				 * Controller has been initialized.
				 *  Move it to the attached_ctrlrs list.
				 */
				TAILQ_REMOVE(&g_nvme_init_ctrlrs, ctrlr, tailq);
				if (nvme_ctrlr_shared(ctrlr)) {
					TAILQ_INSERT_TAIL(&g_spdk_nvme_driver->shared_attached_ctrlrs, ctrlr, tailq);
				} else {
					TAILQ_INSERT_TAIL(&g_nvme_attached_ctrlrs, ctrlr, tailq);
				}

				/*
				 * Increase the ref count before calling attach_cb() as the user may
				 * call nvme_detach() immediately.
				 */
				nvme_ctrlr_proc_get_ref(ctrlr);

				/*
				 * Unlock while calling attach_cb() so the user can call other functions
				 *  that may take the driver lock, like nvme_detach().
				 */
				if (attach_cb) {
					nvme_robust_mutex_unlock(&g_spdk_nvme_driver->lock);
					/* 调用attach_cb回调 */
					attach_cb(cb_ctx, &ctrlr->trid, ctrlr, &ctrlr->opts);
					nvme_robust_mutex_lock(&g_spdk_nvme_driver->lock);
				}

				break;
			}
		}
	}

	g_spdk_nvme_driver->initialized = true;

	nvme_robust_mutex_unlock(&g_spdk_nvme_driver->lock);
	return rc;
}

/**
 * This function will be called repeatedly during initialization until the controller is ready.
 */
int
nvme_ctrlr_process_init(struct spdk_nvme_ctrlr *ctrlr)
{
	union spdk_nvme_cc_register cc;
	union spdk_nvme_csts_register csts;
	uint32_t ready_timeout_in_ms;
	int rc;

	/*
	 * May need to avoid accessing any register on the target controller
	 * for a while. Return early without touching the FSM.
	 * Check sleep_timeout_tsc > 0 for unit test.
	 */
	if ((ctrlr->sleep_timeout_tsc > 0) &&
	    (spdk_get_ticks() <= ctrlr->sleep_timeout_tsc)) {
		return 0;
	}
	ctrlr->sleep_timeout_tsc = 0;

	if (nvme_ctrlr_get_cc(ctrlr, &cc) ||
	    nvme_ctrlr_get_csts(ctrlr, &csts)) {
		if (ctrlr->state_timeout_tsc != NVME_TIMEOUT_INFINITE) {
			/* While a device is resetting, it may be unable to service MMIO reads
			 * temporarily. Allow for this case.
			 */
			SPDK_ERRLOG("Get registers failed while waiting for CSTS.RDY == 0\n");
			goto init_timeout;
		}
		SPDK_ERRLOG("Failed to read CC and CSTS in state %d\n", ctrlr->state);
		nvme_ctrlr_fail(ctrlr, false);
		return -EIO;
	}

	ready_timeout_in_ms = 500 * ctrlr->cap.bits.to;

	/*
	 * Check if the current initialization step is done or has timed out.
	 */
	switch (ctrlr->state) {
	case NVME_CTRLR_STATE_INIT:
		/* Begin the hardware initialization by making sure the controller is disabled. */
		if (cc.bits.en) {
			SPDK_DEBUGLOG(SPDK_LOG_NVME, "CC.EN = 1\n");
			/*
			 * Controller is currently enabled. We need to disable it to cause a reset.
			 *
			 * If CC.EN = 1 && CSTS.RDY = 0, the controller is in the process of becoming ready.
			 *  Wait for the ready bit to be 1 before disabling the controller.
			 */
			if (csts.bits.rdy == 0) {
				SPDK_DEBUGLOG(SPDK_LOG_NVME, "CC.EN = 1 && CSTS.RDY = 0 - waiting for reset to complete\n");
				nvme_ctrlr_set_state(ctrlr, NVME_CTRLR_STATE_DISABLE_WAIT_FOR_READY_1, ready_timeout_in_ms);
				return 0;
			}

			/* CC.EN = 1 && CSTS.RDY == 1, so we can immediately disable the controller. */
			SPDK_DEBUGLOG(SPDK_LOG_NVME, "Setting CC.EN = 0\n");
			cc.bits.en = 0;
			if (nvme_ctrlr_set_cc(ctrlr, &cc)) {
				SPDK_ERRLOG("set_cc() failed\n");
				nvme_ctrlr_fail(ctrlr, false);
				return -EIO;
			}
			nvme_ctrlr_set_state(ctrlr, NVME_CTRLR_STATE_DISABLE_WAIT_FOR_READY_0, ready_timeout_in_ms);

			/*
			 * Wait 2 secsonds before accessing PCI registers.
			 * Not using sleep() to avoid blocking other controller's initialization.
			 */
			if (ctrlr->quirks & NVME_QUIRK_DELAY_BEFORE_CHK_RDY) {
				SPDK_DEBUGLOG(SPDK_LOG_NVME, "Applying quirk: delay 2 seconds before reading registers\n");
				ctrlr->sleep_timeout_tsc = spdk_get_ticks() + 2 * spdk_get_ticks_hz();
			}
			return 0;
		} else {
			if (csts.bits.rdy == 1) {
				SPDK_DEBUGLOG(SPDK_LOG_NVME, "CC.EN = 0 && CSTS.RDY = 1 - waiting for shutdown to complete\n");
			}

			nvme_ctrlr_set_state(ctrlr, NVME_CTRLR_STATE_DISABLE_WAIT_FOR_READY_0, ready_timeout_in_ms);
			return 0;
		}
		break;

	case NVME_CTRLR_STATE_DISABLE_WAIT_FOR_READY_1:
		if (csts.bits.rdy == 1) {
			SPDK_DEBUGLOG(SPDK_LOG_NVME, "CC.EN = 1 && CSTS.RDY = 1 - disabling controller\n");
			/* CC.EN = 1 && CSTS.RDY = 1, so we can set CC.EN = 0 now. */
			SPDK_DEBUGLOG(SPDK_LOG_NVME, "Setting CC.EN = 0\n");
			cc.bits.en = 0;
			if (nvme_ctrlr_set_cc(ctrlr, &cc)) {
				SPDK_ERRLOG("set_cc() failed\n");
				nvme_ctrlr_fail(ctrlr, false);
				return -EIO;
			}
			nvme_ctrlr_set_state(ctrlr, NVME_CTRLR_STATE_DISABLE_WAIT_FOR_READY_0, ready_timeout_in_ms);
			return 0;
		}
		break;

	case NVME_CTRLR_STATE_DISABLE_WAIT_FOR_READY_0:
		if (csts.bits.rdy == 0) {
			SPDK_DEBUGLOG(SPDK_LOG_NVME, "CC.EN = 0 && CSTS.RDY = 0\n");
			nvme_ctrlr_set_state(ctrlr, NVME_CTRLR_STATE_ENABLE, ready_timeout_in_ms);
			/*
			 * Delay 100us before setting CC.EN = 1.  Some NVMe SSDs miss CC.EN getting
			 *  set to 1 if it is too soon after CSTS.RDY is reported as 0.
			 */
			spdk_delay_us(100);
			return 0;
		}
		break;

	case NVME_CTRLR_STATE_ENABLE:
		SPDK_DEBUGLOG(SPDK_LOG_NVME, "Setting CC.EN = 1\n");
		rc = nvme_ctrlr_enable(ctrlr);
		nvme_ctrlr_set_state(ctrlr, NVME_CTRLR_STATE_ENABLE_WAIT_FOR_READY_1, ready_timeout_in_ms);
		return rc;

	case NVME_CTRLR_STATE_ENABLE_WAIT_FOR_READY_1:
		if (csts.bits.rdy == 1) {
			SPDK_DEBUGLOG(SPDK_LOG_NVME, "CC.EN = 1 && CSTS.RDY = 1 - controller is ready\n");
			/*
			 * The controller has been enabled.
			 *  Perform the rest of initialization in nvme_ctrlr_start() serially.
			 */
			rc = nvme_ctrlr_start(ctrlr);
			nvme_ctrlr_set_state(ctrlr, NVME_CTRLR_STATE_READY, NVME_TIMEOUT_INFINITE);
			return rc;
		}
		break;

	case NVME_CTRLR_STATE_READY:
		SPDK_DEBUGLOG(SPDK_LOG_NVME, "Ctrlr already in ready state\n");
		return 0;

	default:
		assert(0);
		nvme_ctrlr_fail(ctrlr, false);
		return -1;
	}

init_timeout:
	if (ctrlr->state_timeout_tsc != NVME_TIMEOUT_INFINITE &&
	    spdk_get_ticks() > ctrlr->state_timeout_tsc) {
		SPDK_ERRLOG("Initialization timed out in state %d\n", ctrlr->state);
		nvme_ctrlr_fail(ctrlr, false);
		return -1;
	}

	return 0;
}

/* Returns true if ctrlr should be stored on the multi-process shared_attached_ctrlrs list */
static bool
nvme_ctrlr_shared(const struct spdk_nvme_ctrlr *ctrlr)
{
	return ctrlr->trid.trtype == SPDK_NVME_TRANSPORT_PCIE;
}

static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
	struct nvme_ctrlr *nvme_ctrlr;
	struct nvme_probe_ctx *ctx = cb_ctx;
	char *name = NULL;
	char *disk_id = NULL;
	size_t i;

	if (ctx) {
		for (i = 0; i < ctx->count; i++) {
			if (spdk_nvme_transport_id_compare(trid, &ctx->trids[i]) == 0) {
				name = strdup(ctx->names[i]);
				disk_id = strdup(ctx->disk_id[i]);
				break;
			}
		}
	} else {
		name = spdk_sprintf_alloc("HotInNvme%d", g_hot_insert_nvme_controller_index++);
	}


	SPDK_DEBUGLOG(SPDK_LOG_BDEV_NVME, "Attached to %s (%s)\n", trid->traddr, name);

	/* 创建nvme   controller */
	nvme_ctrlr = calloc(1, sizeof(*nvme_ctrlr));

	nvme_ctrlr->adminq_timer_poller = NULL;
	nvme_ctrlr->ctrlr = ctrlr;
	nvme_ctrlr->ref = 0;
	nvme_ctrlr->trid = *trid;
	nvme_ctrlr->name = name;
	nvme_ctrlr->disk_id = disk_id;

	spdk_io_device_register(ctrlr, bdev_nvme_create_cb, bdev_nvme_destroy_cb,
				sizeof(struct nvme_io_channel));

	if (nvme_ctrlr_create_bdevs(nvme_ctrlr) != 0) {
	}

	nvme_ctrlr->adminq_timer_poller = spdk_poller_register(bdev_nvme_poll_adminq, ctrlr,
					  g_nvme_adminq_poll_timeout_us);

	/*  将nvme_ctrlr添加到g_nvme_ctrlrs中 */
	TAILQ_INSERT_TAIL(&g_nvme_ctrlrs, nvme_ctrlr, tailq);

	if (g_action_on_timeout != TIMEOUT_ACTION_NONE) {
		spdk_nvme_ctrlr_register_timeout_callback(ctrlr, g_timeout,
				timeout_cb, NULL);
	}
}

struct nvme_bdev {
	struct spdk_bdev	disk;
	struct nvme_ctrlr	*nvme_ctrlr;
	struct spdk_nvme_ns	*ns;

	TAILQ_ENTRY(nvme_bdev)	link;
};


static int
nvme_ctrlr_create_bdevs(struct nvme_ctrlr *nvme_ctrlr)
{
	struct nvme_bdev	*bdev;
	struct spdk_nvme_ctrlr	*ctrlr = nvme_ctrlr->ctrlr;
	struct spdk_nvme_ns	*ns;
	const struct spdk_nvme_ctrlr_data *cdata;
	const struct spdk_uuid	*uuid;
	int			rc;
	int			bdev_created = 0;
	uint32_t		nsid;

	/* &ctrlr->cdata */
	cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	for (nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr);
	     nsid != 0; nsid = spdk_nvme_ctrlr_get_next_active_ns(ctrlr, nsid)) {
		ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if (!ns) {
			SPDK_DEBUGLOG(SPDK_LOG_BDEV_NVME, "Skipping invalid NS %d\n", nsid);
			continue;
		}

		if (!spdk_nvme_ns_is_active(ns)) {
			SPDK_DEBUGLOG(SPDK_LOG_BDEV_NVME, "Skipping inactive NS %d\n", nsid);
			continue;
		}

		/* 分配bdev内存 */
		bdev = calloc(1, sizeof(*bdev));

		bdev->nvme_ctrlr = nvme_ctrlr;
		bdev->ns = ns;
		nvme_ctrlr->ref++;

		bdev->disk.name = spdk_sprintf_alloc("%sn%d", nvme_ctrlr->name, spdk_nvme_ns_get_id(ns));
	
		bdev->disk.product_name = strdup(nvme_ctrlr->disk_id);

		bdev->disk.write_cache = 0;
		if (cdata->vwc.present) {
			/* Enable if the Volatile Write Cache exists */
			bdev->disk.write_cache = 1;
		}
		bdev->disk.blocklen = spdk_nvme_ns_get_sector_size(ns);
		bdev->disk.blockcnt = spdk_nvme_ns_get_num_sectors(ns);
		bdev->disk.optimal_io_boundary = spdk_nvme_ns_get_optimal_io_boundary(ns);

		uuid = spdk_nvme_ns_get_uuid(ns);
		if (uuid != NULL) {
			bdev->disk.uuid = *uuid;
		}

		bdev->disk.ctxt = bdev;
		bdev->disk.fn_table = &nvmelib_fn_table;
		bdev->disk.module = &nvme_if;
		rc = spdk_bdev_register(&bdev->disk);

		TAILQ_INSERT_TAIL(&g_nvme_bdevs, bdev, link);

		bdev_created++;
	}

	/*
	 * We need to record NVMe devices' SN in SPDK's log file to help system
	 * administrator identify a certain NVMe device.
	 */

	/*
	 * 这样的log
	 * [2020-07-27 20:15:25.932]: bdev_nvme.c:1214:nvme_ctrlr_create_bdevs: 
	 * *NOTICE*: nvme name:562ccec7-3aea-4cd2-be01-7fab31fd3878-nvme, 
	 * serial number:PHLJ940302FB4P0DGN  INTEL SSDPE2KX040T8                     VDV10131
	 */
	SPDK_NOTICELOG("nvme name:%s, serial number:%s\n", nvme_ctrlr->name, cdata->sn);

	return (bdev_created > 0) ? 0 : -1;
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


uint32_t
spdk_nvme_ctrlr_get_first_active_ns(struct spdk_nvme_ctrlr *ctrlr)
{
	return ctrlr->active_ns_list ? ctrlr->active_ns_list[0] : 0;
}

struct spdk_nvme_ns *
spdk_nvme_ctrlr_get_ns(struct spdk_nvme_ctrlr *ctrlr, uint32_t nsid)
{
	if (nsid < 1 || nsid > ctrlr->num_ns) {
		return NULL;
	}

	return &ctrlr->ns[nsid - 1];
}


uint32_t
spdk_nvme_ns_get_id(struct spdk_nvme_ns *ns)
{
	return ns->id;
}

int
spdk_bdev_register(struct spdk_bdev *bdev)
{
	int rc = spdk_bdev_init(bdev);

	if (rc == 0) {
		spdk_bdev_start(bdev);
	}

	return rc;
}

#define __bdev_to_io_dev(bdev)		(((char *)bdev) + 1)
#define __bdev_from_io_dev(io_dev)	((struct spdk_bdev *)(((char *)io_dev) - 1))


static int
spdk_bdev_init(struct spdk_bdev *bdev)
{
	if (spdk_bdev_get_by_name(bdev->name)) {
		SPDK_ERRLOG("Bdev name:%s already exists\n", bdev->name);
		return -EEXIST;
	}

	bdev->status = SPDK_BDEV_STATUS_READY;
	bdev->internal.measured_queue_depth = UINT64_MAX;

	TAILQ_INIT(&bdev->open_descs);

	TAILQ_INIT(&bdev->aliases);

	bdev->reset_in_progress = NULL;

	_spdk_bdev_qos_config(bdev);

	spdk_io_device_register(__bdev_to_io_dev(bdev),
				spdk_bdev_channel_create, spdk_bdev_channel_destroy,
				sizeof(struct spdk_bdev_channel));

	spdk_bdev_set_qd_sampling_period(bdev, 1000);

	pthread_mutex_init(&bdev->mutex, NULL);
	return 0;
}

static void
_spdk_bdev_qos_config(struct spdk_bdev *bdev)
{
	struct spdk_conf_section	*sp = NULL;
	const char			*val = NULL;
	uint64_t			ios_per_sec = 0;
	int				i = 0;

	sp = spdk_conf_find_section(NULL, "QoS");
	if (!sp) {
		return;
	}

	while (true) {
		val = spdk_conf_section_get_nmval(sp, "Limit_IOPS", i, 0);
		if (!val) {
			break;
		}

		if (strcmp(bdev->name, val) != 0) {
			i++;
			continue;
		}

		val = spdk_conf_section_get_nmval(sp, "Limit_IOPS", i, 1);
		if (!val) {
			return;
		}

		ios_per_sec = strtoull(val, NULL, 10);
		if (ios_per_sec > 0) {
			if (ios_per_sec % SPDK_BDEV_QOS_MIN_IOS_PER_SEC) {
				SPDK_ERRLOG("Assigned IOPS %" PRIu64 " on bdev %s is not multiple of %u\n",
					    ios_per_sec, bdev->name, SPDK_BDEV_QOS_MIN_IOS_PER_SEC);
				SPDK_ERRLOG("Failed to enable QoS on this bdev %s\n", bdev->name);
			} else {
				bdev->qos.enabled = true;
				bdev->qos.rate_limit = ios_per_sec;
				SPDK_DEBUGLOG(SPDK_LOG_BDEV, "Bdev:%s QoS:%lu\n",
					      bdev->name, bdev->qos.rate_limit);
			}
		}

		return;
	}
}

void
spdk_bdev_set_qd_sampling_period(struct spdk_bdev *bdev, uint64_t period)
{
	bdev->internal.period = period;

	if (bdev->internal.qd_poller != NULL) {
		spdk_poller_unregister(&bdev->internal.qd_poller);
		bdev->internal.measured_queue_depth = UINT64_MAX;
	}

	if (period != 0) {
		bdev->internal.qd_poller = spdk_poller_register(spdk_bdev_calculate_measured_queue_depth, bdev,
					   period);
	}
}

static int
spdk_bdev_calculate_measured_queue_depth(void *ctx)
{
	struct spdk_bdev *bdev = ctx;
	bdev->internal.temporary_queue_depth = 0;
	spdk_for_each_channel(__bdev_to_io_dev(bdev), _calculate_measured_qd, bdev,
			      _calculate_measured_qd_cpl);
	return 0;
}


void
spdk_for_each_channel(void *io_device, spdk_channel_msg fn, void *ctx,
		      spdk_channel_for_each_cpl cpl)
{
	struct spdk_thread *thread;
	struct spdk_io_channel *ch;
	struct spdk_io_channel_iter *i;

	i = calloc(1, sizeof(*i));
	if (!i) {
		SPDK_ERRLOG("Unable to allocate iterator\n");
		return;
	}

	i->io_device = io_device;
	i->fn = fn;
	i->ctx = ctx;
	i->cpl = cpl;

	pthread_mutex_lock(&g_devlist_mutex);
	i->orig_thread = _get_thread();

	TAILQ_FOREACH(thread, &g_threads, tailq) {
		TAILQ_FOREACH(ch, &thread->io_channels, tailq) {
			if (ch->dev->io_device == io_device) {
				ch->dev->for_each_count++;
				i->dev = ch->dev;
				i->cur_thread = thread;
				i->ch = ch;
				pthread_mutex_unlock(&g_devlist_mutex);
				spdk_thread_send_msg(thread, _call_channel, i);
				return;
			}
		}
	}

	pthread_mutex_unlock(&g_devlist_mutex);

	cpl(i, 0);

	free(i);
}

void
spdk_thread_send_msg(const struct spdk_thread *thread, spdk_thread_fn fn, void *ctx)
{
	/* _spdk_reactor_send_msg，通过event发送 */
	thread->msg_fn(fn, ctx, thread->thread_ctx);
}

static void
spdk_bdev_start(struct spdk_bdev *bdev)
{
	struct spdk_bdev_module *module;

	SPDK_DEBUGLOG(SPDK_LOG_BDEV, "Inserting bdev %s into list\n", bdev->name);
	TAILQ_INSERT_TAIL(&g_bdev_mgr.bdevs, bdev, link);

	TAILQ_FOREACH(module, &g_bdev_mgr.bdev_modules, tailq) {
		if (module->examine) {
			module->action_in_progress++;
			module->examine(bdev); /* nvme bdev没有实现examine */
		}
	}
}

static int
bdev_nvme_poll_adminq(void *arg)
{
	struct spdk_nvme_ctrlr *ctrlr = arg;

	return spdk_nvme_ctrlr_process_admin_completions(ctrlr);
}

int32_t
spdk_nvme_ctrlr_process_admin_completions(struct spdk_nvme_ctrlr *ctrlr)
{
	int32_t num_completions;

	nvme_robust_mutex_lock(&ctrlr->ctrlr_lock);
	if (ctrlr->keep_alive_interval_ticks) {
		nvme_ctrlr_keep_alive(ctrlr);
	}
	num_completions = spdk_nvme_qpair_process_completions(ctrlr->adminq, 0);
	nvme_robust_mutex_unlock(&ctrlr->ctrlr_lock);

	return num_completions;
}

