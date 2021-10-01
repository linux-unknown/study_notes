
static TypeInfo object_info = {
    .name = TYPE_OBJECT,
    .instance_size = sizeof(Object),
    .instance_init = object_instance_init,
    .abstract = true,
};

static const TypeInfo device_type_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DeviceState),
    .instance_init = device_initfn, /* 会调用 virtio_device_realize */
    .instance_post_init = device_post_init,
    .instance_finalize = device_finalize,
    .class_base_init = device_class_base_init,
    .class_init = device_class_init,
    .abstract = true,
    .class_size = sizeof(DeviceClass),
};

static const TypeInfo virtio_device_info = {
    .name = TYPE_VIRTIO_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VirtIODevice),
    .class_init = virtio_device_class_init,
    .abstract = true,
    .class_size = sizeof(VirtioDeviceClass),
};

static const TypeInfo vhost_user_blk_info = {
    .name = TYPE_VHOST_USER_BLK,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserBlk),
    .instance_init = vhost_user_blk_instance_init,
    .class_init = vhost_user_blk_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_user_blk_info);
}

type_init(virtio_register_types);


/* object_initialize 会先执行class_init,然后执行instance_init */
static void device_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);

    class->unparent = device_unparent;
	/* 设置 DeviceClass的realize函数 */
    dc->realize = device_realize;
    dc->unrealize = device_unrealize;

    /* by default all devices were considered as hotpluggable,
     * so with intent to check it in generic qdev_unplug() /
     * device_set_realized() functions make every device
     * hotpluggable. Devices that shouldn't be hotpluggable,
     * should override it in their class_init()
     */
    dc->hotpluggable = true;
}

static void virtio_device_class_init(ObjectClass *klass, void *data)
{
    /* Set the default value here. */
    DeviceClass *dc = DEVICE_CLASS(klass);
	/* 覆盖了device_class_init中赋值的dc->realize */
    dc->realize = virtio_device_realize;
    dc->unrealize = virtio_device_unrealize;
    dc->bus_type = TYPE_VIRTIO_BUS;
    dc->props = virtio_properties;
}

/* 通过 chardev 属性和 chardev 关联在一起 */
static Property vhost_user_blk_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBlk, chardev),
    DEFINE_PROP_UINT16("num-queues", VHostUserBlk, num_queues, 1),
    DEFINE_PROP_UINT32("queue-size", VHostUserBlk, queue_size, 128),
    DEFINE_PROP_BIT("config-wce", VHostUserBlk, config_wce, 0, true),
    DEFINE_PROP_BIT("config-ro", VHostUserBlk, config_ro, 0, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = vhost_user_blk_properties;
    //dc->vmsd = &vmstate_vhost_user_blk;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_user_blk_device_realize;
    vdc->unrealize = vhost_user_blk_device_unrealize;
    vdc->get_config = vhost_user_blk_update_config;
    vdc->set_config = vhost_user_blk_set_config;
    vdc->get_features = vhost_user_blk_get_features;
    vdc->set_status = vhost_user_blk_set_status;
    vdc->reset = vhost_user_blk_reset;
    vdc->save = vhost_user_blk_save_inflight;
    vdc->load = vhost_user_blk_load_inflight;
}

/******instance_init root ******/
static void object_instance_init(Object *obj)
{
    object_property_add_str(obj, "type", qdev_get_type, NULL, NULL);
}

static void device_initfn(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    ObjectClass *class;
    Property *prop;

    if (qdev_hotplug) {
        dev->hotplugged = 1;
        qdev_hot_added = true;
    }

    dev->instance_id_alias = -1;
    dev->realized = false;

	/* 添加 realized 属性 */
    object_property_add_bool(obj, "realized",
                             device_get_realized, device_set_realized, NULL);
    object_property_add_bool(obj, "hotpluggable",
                             device_get_hotpluggable, NULL, NULL);
    object_property_add_bool(obj, "hotplugged",
                             device_get_hotplugged, device_set_hotplugged,
                             &error_abort);

    class = object_get_class(OBJECT(dev));
    do {
		/*  将 DeviceClass->props 定义的静态属性添加到   dev中 */
        for (prop = DEVICE_CLASS(class)->props; prop && prop->name; prop++) {
            qdev_property_add_legacy(dev, prop, &error_abort);
            qdev_property_add_static(dev, prop, &error_abort);
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));

    object_property_add_link(OBJECT(dev), "parent_bus", TYPE_BUS,
                             (Object **)&dev->parent_bus, NULL, 0,
                             &error_abort);
    QLIST_INIT(&dev->gpios);
}


static void vhost_user_blk_instance_init(Object *obj)
{
    VHostUserBlk *s = VHOST_USER_BLK(obj);

    device_add_bootindex_property(obj, &s->bootindex, "bootindex",
                                  "/disk@0,0", DEVICE(obj), NULL);
}

static Property virtio_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIODevice, host_features),
    DEFINE_PROP_BOOL("__com.redhat_rhel6_ctrl_guest_workaround", VirtIODevice,
                     rhel6_ctrl_guest_workaround, false),
    DEFINE_PROP_END_OF_LIST(),
};


/**
 * qdev_device_add-->object_property_set_bool-->object_property_set
 */
void object_property_set(Object *obj, Visitor *v, const char *name, Error **errp)
{
    ObjectProperty *prop = object_property_find(obj, name, errp);

	/**
	 * 调用  根Object 的relaize函数，函数额调用顺序如下：
	 * property_set_bool->device_set_realized->virtio_pci_dc_realize
	 * pci_qdev_realize->virtio_pci_realize->vhost_user_blk_pci_realize
	 * object_property_set->property_set_bool->device_set_realized->
	 * virtio_device_realize->vhost_user_blk_device_realize
	 */
    prop->set(obj, v, name, prop->opaque, errp);
}

static void device_set_realized(Object *obj, bool value, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    HotplugHandler *hotplug_ctrl;
    BusState *bus;
    Error *local_err = NULL;
    bool unattached_parent = false;
    static int unattached_count;

    if (dev->hotplugged && !dc->hotpluggable) {
        error_setg(errp, QERR_DEVICE_NO_HOTPLUG, object_get_typename(obj));
        return;
    }

    if (value && !dev->realized) {
        if (!obj->parent) {
            gchar *name = g_strdup_printf("device[%d]", unattached_count++);

            object_property_add_child(container_get(qdev_get_machine(),
                                                    "/unattached"),
                                      name, obj, &error_abort);
            unattached_parent = true;
            g_free(name);
        }

        hotplug_ctrl = qdev_get_hotplug_handler(dev);

		/* 调用 virtio_device_realize */
        if (dc->realize) {
            dc->realize(dev, &local_err);
        }

        DEVICE_LISTENER_CALL(realize, Forward, dev);

        if (hotplug_ctrl) {
            hotplug_handler_plug(hotplug_ctrl, dev, &local_err);
        }

        if (local_err != NULL) {
            goto post_realize_fail;
        }

        if (qdev_get_vmsd(dev)) {
            vmstate_register_with_alias_id(dev, -1, qdev_get_vmsd(dev), dev,
                                           dev->instance_id_alias,
                                           dev->alias_required_for_version);
        }

        QLIST_FOREACH(bus, &dev->child_bus, sibling) {
            object_property_set_bool(OBJECT(bus), true, "realized",
                                         &local_err);
            if (local_err != NULL) {
                goto child_realize_fail;
            }
        }
        if (dev->hotplugged) {
            device_reset(dev);
        }
        dev->pending_deleted_event = false;
    } else if (!value && dev->realized) {
        Error **local_errp = NULL;
        QLIST_FOREACH(bus, &dev->child_bus, sibling) {
            local_errp = local_err ? NULL : &local_err;
            object_property_set_bool(OBJECT(bus), false, "realized",
                                     local_errp);
        }
        if (qdev_get_vmsd(dev)) {
            vmstate_unregister(dev, qdev_get_vmsd(dev), dev);
        }
        if (dc->unrealize) {
            local_errp = local_err ? NULL : &local_err;
            dc->unrealize(dev, local_errp);
        }
        dev->pending_deleted_event = true;
        DEVICE_LISTENER_CALL(unrealize, Reverse, dev);
    }

    dev->realized = value;
    return;
}

static void virtio_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(dev);
    Error *err = NULL;

	/* 调用 vhost_user_blk_device_realize */
    if (vdc->realize != NULL) {
        vdc->realize(dev, &err);

    }

    virtio_bus_device_plugged(vdev, &err);
}


static void vhost_user_blk_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    int i, ret;
    Error *err = NULL;
    static int virtio_blk_id;


    virtio_init(vdev, "virtio-blk", VIRTIO_ID_BLOCK, sizeof(struct virtio_blk_config));

	/* num-queues通过deivce  参数指定，会在qemu启动的时候进行解析 */
    for (i = 0; i < s->num_queues; i++) {
        virtio_add_queue(vdev, s->queue_size, vhost_user_blk_handle_output);
    }

    s->watch = 0;
    s->should_start = false;
    s->connected = false;

	/* 连接之后会调用blk_vhost_user_event回调 */
    qemu_chr_add_handlers(s->chardev, NULL, NULL, blk_vhost_user_event, dev);

reconnect:
	/* 创建socket, 链接vhost  ,即  spdk，qemu_chr_wait_connected回执行blk_vhost_user_event       */
    if (qemu_chr_wait_connected(s->chardev, &err) < 0) {
    }
    if (!s->connected) {
        usleep(1000);
        goto reconnect;
    }

    s->inflight = g_new0(struct vhost_inflight, 1);
    s->dev.nvqs = s->num_queues;
    s->dev.vqs = g_new0(struct vhost_virtqueue, s->dev.nvqs);
    s->dev.vq_index = 0;
    s->dev.backend_features = 0;
    vhost_dev_set_config_notifier(&s->dev, &blk_ops);

    ret = vhost_dev_init(&s->dev, s->chardev, VHOST_BACKEND_TYPE_USER, 0);
 
    ret = vhost_dev_get_config(&s->dev, (uint8_t *)&s->blkcfg, sizeof(struct virtio_blk_config));

    if (s->blkcfg.num_queues != s->num_queues) {
        s->blkcfg.num_queues = s->num_queues;
    }

    register_savevm(dev, "vhost-user-blk", virtio_blk_id++, 1,
                    vhost_user_blk_save, vhost_user_blk_load, s);

    vhost_user_blk_mig_init((void *)&s->dev);

    return;
}

#define VIRTIO_QUEUE_MAX 1024

void virtio_init(VirtIODevice *vdev, const char *name, uint16_t device_id, size_t config_size)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i;
    int nvectors = k->query_nvectors ? k->query_nvectors(qbus->parent) : 0;

    if (nvectors) {
        vdev->vector_queues = g_malloc0(sizeof(*vdev->vector_queues) * nvectors);
    }

    vdev->device_id = device_id;
    vdev->status = 0;
    vdev->isr = 0;
    vdev->queue_sel = 0;
    vdev->config_vector = VIRTIO_NO_VECTOR;
    vdev->vq = g_malloc0(sizeof(VirtQueue) * VIRTIO_QUEUE_MAX);
    vdev->vm_running = runstate_is_running();
    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        vdev->vq[i].vector = VIRTIO_NO_VECTOR;
        vdev->vq[i].vdev = vdev;
        vdev->vq[i].queue_index = i;
    }

    vdev->name = name;
    vdev->config_len = config_size;
    if (vdev->config_len) {
        vdev->config = g_malloc0(config_size);
    } else {
        vdev->config = NULL;
    }
    vdev->vmstate = qemu_add_vm_change_state_handler(virtio_vmstate_change,
                                                     vdev);
    vdev->device_endian = virtio_default_endian();
    vdev->use_guest_notifier_mask = true;
}

VirtQueue *virtio_add_queue(VirtIODevice *vdev, int queue_size,
                            void (*handle_output)(VirtIODevice *, VirtQueue *))
{
    int i;

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0)
            break;
    }

    if (i == VIRTIO_QUEUE_MAX || queue_size > VIRTQUEUE_MAX_SIZE)
        abort();

    vdev->vq[i].vring.num = queue_size;
    vdev->vq[i].vring.num_default = queue_size;
    vdev->vq[i].vring.align = VIRTIO_PCI_VRING_ALIGN;
    vdev->vq[i].handle_output = handle_output;
    vdev->vq[i].handle_aio_output = NULL;

    return &vdev->vq[i];
}


struct vhost_memory {
	__u32 nregions;
	__u32 padding;
	struct vhost_memory_region regions[0];
};


int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, uint32_t busyloop_timeout)
{
    uint64_t features;
    int i, r, n_initialized_vqs = 0;

    hdev->vdev = NULL;
    hdev->migration_blocker = NULL;

    r = vhost_set_backend_type(hdev, backend_type);

    r = hdev->vhost_ops->vhost_backend_init(hdev, opaque);


    if (used_memslots > hdev->vhost_ops->vhost_backend_memslots_limit(hdev)) {
    }

    r = hdev->vhost_ops->vhost_set_owner(hdev);

    r = hdev->vhost_ops->vhost_get_features(hdev, &features);

    for (i = 0; i < hdev->nvqs; ++i, ++n_initialized_vqs) {
        r = vhost_virtqueue_init(hdev, hdev->vqs + i, hdev->vq_index + i);
    }

    if (busyloop_timeout) {
        for (i = 0; i < hdev->nvqs; ++i) {
            r = vhost_virtqueue_set_busyloop_timeout(hdev, hdev->vq_index + i,
                                                     busyloop_timeout);
            if (r < 0) {
                goto fail_busyloop;
            }
        }
    }

    hdev->features = features;

    hdev->memory_listener = (MemoryListener) {
        .begin = vhost_begin,
        .commit = vhost_commit,
        .region_add = vhost_region_add,
        .region_del = vhost_region_del,
        .region_nop = vhost_region_nop,
        .log_start = vhost_log_start,
        .log_stop = vhost_log_stop,
        .log_sync = vhost_log_sync,
        .log_global_start = vhost_log_global_start,
        .log_global_stop = vhost_log_global_stop,
        .eventfd_add = vhost_eventfd_add,
        .eventfd_del = vhost_eventfd_del,
        .priority = 10
    };

    if (hdev->migration_blocker == NULL) {
        if (!(hdev->features & (0x1ULL << VHOST_F_LOG_ALL))) {
            error_setg(&hdev->migration_blocker,
                       "Migration disabled: vhost lacks VHOST_F_LOG_ALL feature.");
        } else if (!qemu_memfd_check()) {
            error_setg(&hdev->migration_blocker,
                       "Migration disabled: failed to allocate shared memory");
        }
    }

    if (hdev->migration_blocker != NULL) {
        migrate_add_blocker(hdev->migration_blocker);
    }

    hdev->mem = g_malloc0(offsetof(struct vhost_memory, regions));
    hdev->n_mem_sections = 0;
    hdev->mem_sections = NULL;
    hdev->log = NULL;
    hdev->log_size = 0;
    hdev->log_enabled = false;
    hdev->started = false;
    hdev->memory_changed = false;
	/* 注册memory_listener，注册的时候就会调用 */
    memory_listener_register(&hdev->memory_listener, &address_space_memory);
    QLIST_INSERT_HEAD(&vhost_devices, hdev, entry);
    return 0;
}

int vhost_set_backend_type(struct vhost_dev *dev, VhostBackendType backend_type)
{
    int r = 0;
	/* 根据类型设置ops，spdk是 VHOST_BACKEND_TYPE_USER */
    switch (backend_type) {
    case VHOST_BACKEND_TYPE_KERNEL:
        dev->vhost_ops = &kernel_ops;
        break;
    case VHOST_BACKEND_TYPE_USER:
        dev->vhost_ops = &user_ops;
        break;
    default:
        error_report("Unknown vhost backend type");
        r = -1;
    }

    return r;
}

static int vhost_virtqueue_init(struct vhost_dev *dev,
                                struct vhost_virtqueue *vq, int n)
{
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, n);
    struct vhost_vring_file file = {
        .index = vhost_vq_index,
    };
    int r = event_notifier_init(&vq->masked_notifier, 0);

    file.fd = event_notifier_get_fd(&vq->masked_notifier);
    r = dev->vhost_ops->vhost_set_vring_call(dev, &file);
    return 0;
}

int vhost_dev_get_config(struct vhost_dev *hdev, uint8_t *config,
                         uint32_t config_len)
{
    if (hdev->vhost_ops->vhost_get_config) {
        return hdev->vhost_ops->vhost_get_config(hdev, config, config_len);
    }
}

static struct SaveVMHandlers savevm_vhost_user_blk_handlers = {
    .save_live_io_pending = vhost_user_blk_save_io_pending,
    .save_live_io_down    = vhost_user_blk_save_io_down,
    .save_live_io_cleanup = vhost_user_blk_save_io_cleanup,
    .save_live_io_final   = vhost_user_blk_save_io_final,
};

void vhost_user_blk_mig_init(void* opaque) {
    register_savevm_live(NULL, "vhost-user-blk", 0, 1, &savevm_vhost_user_blk_handlers,
        opaque);
}

/* A VirtIODevice is being plugged */
void virtio_bus_device_plugged(VirtIODevice *vdev, Error **errp)
{
    DeviceState *qdev = DEVICE(vdev);
	/* return dev->parent_bus
	 * 在qdev_set_parent_bus会设置 parent_bus 
	 */
    BusState *qbus = BUS(qdev_get_parent_bus(qdev));
    VirtioBusState *bus = VIRTIO_BUS(qbus);
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(bus);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);

    DPRINTF("%s: plug device.\n", qbus->name);

    if (klass->pre_plugged != NULL) {
        klass->pre_plugged(qbus->parent, errp);
    }

    /* Get the features of the plugged device. */
    vdev->host_features = vdc->get_features(vdev, vdev->host_features,
                                            errp);
	/* 调用 virtio_pci_device_plugged */
    if (klass->device_plugged != NULL) {
        klass->device_plugged(qbus->parent, errp);
    }
}

static void vhost_region_add(MemoryListener *listener,
                             MemoryRegionSection *section)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev, memory_listener);

	/* section必须是ram，并且有与之对应的fd */
    if (!vhost_section(dev, section)) {
        return;
    }

    ++dev->n_mem_sections;
    dev->mem_sections = g_renew(MemoryRegionSection, dev->mem_sections, dev->n_mem_sections);
    dev->mem_sections[dev->n_mem_sections - 1] = *section;
    memory_region_ref(section->mr);
    vhost_set_memory(listener, section, true);
}

static bool vhost_section(struct vhost_dev *dev, MemoryRegionSection *section)
{
	/* mr需要是ram */
    bool result = memory_region_is_ram(section->mr) && 
        !memory_region_is_rom(section->mr);

	/* vhost user调用vhost_user_mem_section_filter */
    if(result && dev->vhost_ops->vhost_backend_mem_section_filter) {
        result &= dev->vhost_ops->vhost_backend_mem_section_filter(dev, section);
    }

    return result;
}

static inline bool memory_region_is_ram(MemoryRegion *mr)
{
    return mr->ram;
}

static inline bool memory_region_is_rom(MemoryRegion *mr)
{
    return mr->ram && mr->readonly;
}

static bool vhost_user_mem_section_filter(struct vhost_dev *dev,
                                          MemoryRegionSection *section)
{
    bool result;
    result = memory_region_get_fd(section->mr) >= 0;

    return result;
}

int memory_region_get_fd(MemoryRegion *mr)
{
    if (mr->alias) {
		/* 递归调用 */
        return memory_region_get_fd(mr->alias);
    }

    return qemu_get_ram_fd(memory_region_get_ram_addr(mr) & TARGET_PAGE_MASK);
}


ram_addr_t memory_region_get_ram_addr(MemoryRegion *mr)
{
	/* offset为qemu为给guset虚拟的物理地址的偏移 */
    return mr->ram_block ? mr->ram_block->offset : RAM_ADDR_INVALID;
}

int qemu_get_ram_fd(ram_addr_t addr)
{
    RAMBlock *block;
    int fd;

    rcu_read_lock();
    block = qemu_get_ram_block(addr);
    fd = block->fd;
    rcu_read_unlock();
    return fd;
}

static RAMBlock *qemu_get_ram_block(ram_addr_t addr)
{
    RAMBlock *block;

    block = atomic_rcu_read(&ram_list.mru_block);
    if (block && addr - block->offset < block->max_length) {
        return block;
    }
    QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
        if (addr - block->offset < block->max_length) {
            goto found;
        }
    }

    fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
    abort();

found:
    /* It is safe to write mru_block outside the iothread lock.  This
     * is what happens:
     *
     *     mru_block = xxx
     *     rcu_read_unlock()
     *                                        xxx removed from list
     *                  rcu_read_lock()
     *                  read mru_block
     *                                        mru_block = NULL;
     *                                        call_rcu(reclaim_ramblock, xxx);
     *                  rcu_read_unlock()
     *
     * atomic_rcu_set is not needed here.  The block was already published
     * when it was placed into the list.  Here we're just making an extra
     * copy of the pointer.
     */
    ram_list.mru_block = block;
    return block;
}

static void vhost_set_memory(MemoryListener *listener, MemoryRegionSection *section,
                             bool add)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev, memory_listener);
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    bool log_dirty = memory_region_get_dirty_log_mask(section->mr) & ~(1 << DIRTY_MEMORY_MIGRATION);
    int s = offsetof(struct vhost_memory, regions) + (dev->mem->nregions + 1) * sizeof dev->mem->regions[0];
    void *ram;

    dev->mem = g_realloc(dev->mem, s);

    if (log_dirty) {
        add = false;
    }

    /* Optimize no-change case. At least cirrus_vga does this a lot at this time. */
	/* ram地址是物理地址在qemu进程空间中的虚拟地址 
	 * 在listener_add_address_space函数中会给section->offset_within_region赋值
	 * 同时也会给section->offset_within_address_space赋值
	 * memory_region_get_ram_ptr 返回mr物理地址对应的虚拟地址
	 */
    ram = memory_region_get_ram_ptr(section->mr) + section->offset_within_region;
    if (add) {
        if (!vhost_dev_cmp_memory(dev, start_addr, size, (uintptr_t)ram)) {
            /* Region exists with same address. Nothing to do. */
            return;
        }
    } else {
        if (!vhost_dev_find_reg(dev, start_addr, size)) {
            /* Removing region that we don't access. Nothing to do. */
            return;
        }
    }

    vhost_dev_unassign_memory(dev, start_addr, size);
    if (add) {
        /* Add given mapping, merging adjacent regions if any */
        vhost_dev_assign_memory(dev, start_addr, size, (uintptr_t)ram);
    } else {
        /* Remove old mapping for this memory, if any. */
        vhost_dev_unassign_memory(dev, start_addr, size);
    }
    dev->mem_changed_start_addr = MIN(dev->mem_changed_start_addr, start_addr);
    dev->mem_changed_end_addr = MAX(dev->mem_changed_end_addr, start_addr + size - 1);
    dev->memory_changed = true;
    used_memslots = dev->mem->nregions;
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
    void *ptr;
    uint64_t offset = 0;

    rcu_read_lock();
    while (mr->alias) {
        offset += mr->alias_offset;
        mr = mr->alias;
    }

    ptr = qemu_get_ram_ptr(mr->ram_block, memory_region_get_ram_addr(mr) & TARGET_PAGE_MASK);
    rcu_read_unlock();

    return ptr + offset;
}

void *qemu_get_ram_ptr(RAMBlock *ram_block, ram_addr_t addr)
{
    RAMBlock *block = ram_block;

    if (block == NULL) {
        block = qemu_get_ram_block(addr);
    }

    return ramblock_ptr(block, addr - block->offset);
}

/* Called after unassign, so no regions overlap the given range. */
static void vhost_dev_assign_memory(struct vhost_dev *dev, uint64_t start_addr,
                                    uint64_t size, uint64_t uaddr)
{
    int from, to;
    struct vhost_memory_region *merged = NULL;
    for (from = 0, to = 0; from < dev->mem->nregions; ++from, ++to) {
        struct vhost_memory_region *reg = dev->mem->regions + to;
        uint64_t prlast, urlast;
        uint64_t pmlast, umlast;
        uint64_t s, e, u;

        /* clone old region */
        if (to != from) {
            memcpy(reg, dev->mem->regions + from, sizeof *reg);
        }
        prlast = range_get_last(reg->guest_phys_addr, reg->memory_size);
        pmlast = range_get_last(start_addr, size);
        urlast = range_get_last(reg->userspace_addr, reg->memory_size);
        umlast = range_get_last(uaddr, size);

        /* check for overlapping regions: should never happen. */
        assert(prlast < start_addr || pmlast < reg->guest_phys_addr);
        /* Not an adjacent or overlapping region - do not merge. */
        if ((prlast + 1 != start_addr || urlast + 1 != uaddr) &&
            (pmlast + 1 != reg->guest_phys_addr ||
             umlast + 1 != reg->userspace_addr)) {
            continue;
        }

        if (dev->vhost_ops->vhost_backend_can_merge &&
            !dev->vhost_ops->vhost_backend_can_merge(dev, uaddr, size,
                                                     reg->userspace_addr,
                                                     reg->memory_size)) {
            continue;
        }

        if (merged) {
            --to;

        } else {
            merged = reg;
        }
        u = MIN(uaddr, reg->userspace_addr);
        s = MIN(start_addr, reg->guest_phys_addr);
        e = MAX(pmlast, prlast);
        uaddr = merged->userspace_addr = u;
        start_addr = merged->guest_phys_addr = s;
        size = merged->memory_size = e - s + 1;
    }

    if (!merged) {
        struct vhost_memory_region *reg = dev->mem->regions + to;
        memset(reg, 0, sizeof *reg);
        reg->memory_size = size;
		/* guest_phys_addr： guest 中的物理地址
		 * userspace_addr：该mr在qemu地址空间的虚拟地址
		 */
        reg->guest_phys_addr = start_addr;
        reg->userspace_addr = uaddr;
        ++to;
    }
    dev->mem->nregions = to;
}

static void vhost_commit(MemoryListener *listener)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    hwaddr start_addr = 0;
    ram_addr_t size = 0;
    uint64_t log_size;
    int r;

    if (!dev->memory_changed) {
        return;
    }
    if (!dev->started) {
        return;
    }
    if (dev->mem_changed_start_addr > dev->mem_changed_end_addr) {
        return;
    }

    if (dev->started) {
        start_addr = dev->mem_changed_start_addr;
        size = dev->mem_changed_end_addr - dev->mem_changed_start_addr + 1;

        r = vhost_verify_ring_mappings(dev, start_addr, size);
        assert(r >= 0);
    }

    if (!dev->log_enabled) {
		/* 调用 vhost_user_set_mem_table */
        r = dev->vhost_ops->vhost_set_mem_table(dev, dev->mem);
        if (r < 0) {
            VHOST_OPS_DEBUG("vhost_set_mem_table failed");
        }
        dev->memory_changed = false;
        return;
    }
    log_size = vhost_get_log_size(dev);
    /* We allocate an extra 4K bytes to log,
     * to reduce the * number of reallocations. */
#define VHOST_LOG_BUFFER (0x1000 / sizeof *dev->log)
    /* To log more, must increase log size before table update. */
    if (dev->log_size < log_size) {
        vhost_dev_log_resize(dev, log_size + VHOST_LOG_BUFFER);
    }
    r = dev->vhost_ops->vhost_set_mem_table(dev, dev->mem);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_set_mem_table failed");
    }
    /* To log less, can only decrease log size after table update. */
    if (dev->log_size > log_size + VHOST_LOG_BUFFER) {
        vhost_dev_log_resize(dev, log_size);
    }
    dev->memory_changed = false;
}

static int vhost_user_set_mem_table(struct vhost_dev *dev, struct vhost_memory *mem)
{
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    int i, fd;
    size_t fd_num = 0;
    bool reply_supported = virtio_has_feature(dev->protocol_features,
                                              VHOST_USER_PROTOCOL_F_REPLY_ACK);

    VhostUserMsg msg = {
        .request = VHOST_USER_SET_MEM_TABLE,
        .flags = VHOST_USER_VERSION,
    };

    if (reply_supported) {
        msg.flags |= VHOST_USER_NEED_REPLY_MASK;
    }

    for (i = 0; i < dev->mem->nregions; ++i) {
        struct vhost_memory_region *reg = dev->mem->regions + i;
        ram_addr_t ram_addr;

		/* 返回userspace_addr对应的guest的物理地址 */
        qemu_ram_addr_from_host((void *)(uintptr_t)reg->userspace_addr, &ram_addr);

        fd = qemu_get_ram_fd(ram_addr);
        if (fd > 0) {
            msg.payload.memory.regions[fd_num].userspace_addr = reg->userspace_addr;
            msg.payload.memory.regions[fd_num].memory_size  = reg->memory_size;
            msg.payload.memory.regions[fd_num].guest_phys_addr = reg->guest_phys_addr;

			/* qemu_get_ram_block_host_ptr,返回物理地址ram_addr对应的qemu地址空间的虚拟地址 
			 * mmap_offset不为0，可能是由于me合并的原因，后面分析
			 */
            msg.payload.memory.regions[fd_num].mmap_offset = reg->userspace_addr -
                					(uintptr_t) qemu_get_ram_block_host_ptr(ram_addr);

            fds[fd_num++] = fd;
        }
    }

    msg.payload.memory.nregions = fd_num;

    if (!fd_num) {
        error_report("Failed initializing vhost-user memory map, "
                     "consider using -object memory-backend-file share=on");
        return -1;
    }

    msg.size = sizeof(msg.payload.memory.nregions);
    msg.size += sizeof(msg.payload.memory.padding);
    msg.size += fd_num * sizeof(VhostUserMemoryRegion);

    if (vhost_user_write(dev, &msg, fds, fd_num) < 0) {
        return -1;
    }

    if (reply_supported) {
        return process_message_reply(dev, msg.request);
    }

    return 0;
}

MemoryRegion *qemu_ram_addr_from_host(void *ptr, ram_addr_t *ram_addr)
{
    RAMBlock *block;
    ram_addr_t offset; /* Not used */

    block = qemu_ram_block_from_host(ptr, false, ram_addr, &offset);

    if (!block) {
        return NULL;
    }

    return block->mr;
}

/*
 * Translates a host ptr back to a RAMBlock, a ram_addr and an offset
 * in that RAMBlock.
 *
 * ptr: Host pointer to look up
 * round_offset: If true round the result offset down to a page boundary
 * *ram_addr: set to result ram_addr
 * *offset: set to result offset within the RAMBlock
 *
 * Returns: RAMBlock (or NULL if not found)
 *
 * By the time this function returns, the returned pointer is not protected
 * by RCU anymore.  If the caller is not within an RCU critical section and
 * does not hold the iothread lock, it must have other means of protecting the
 * pointer, such as a reference to the region that includes the incoming
 * ram_addr_t.
 */
RAMBlock *qemu_ram_block_from_host(void *ptr, bool round_offset,
                                   ram_addr_t *ram_addr,
                                   ram_addr_t *offset)
{
    RAMBlock *block;
    uint8_t *host = ptr;


    rcu_read_lock();
    block = atomic_rcu_read(&ram_list.mru_block);
    if (block && block->host && host - block->host < block->max_length) {
        goto found;
    }

    QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
        /* This case append when the block is not mapped. */
        if (block->host == NULL) {
            continue;
        }
        if (host - block->host < block->max_length) {
            goto found;
        }
    }

    rcu_read_unlock();
    return NULL;

found:
    *offset = (host - block->host);
    if (round_offset) {
        *offset &= TARGET_PAGE_MASK;
    }
    *ram_addr = block->offset + *offset;
    rcu_read_unlock();
    return block;
}

void *qemu_get_ram_block_host_ptr(ram_addr_t addr)
{
    RAMBlock *block;
    void *ptr;

    rcu_read_lock();
    block = qemu_get_ram_block(addr);
    ptr = ramblock_ptr(block, 0);
    rcu_read_unlock();
    return ptr;
}

