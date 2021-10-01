#define TYPE_OBJECT "object"
#define TYPE_DEVICE "device"
#define TYPE_VIRTIO_DEVICE "virtio-device"
#define TYPE_VIRTIO_NET "virtio-net-device"


typedef struct VirtioDeviceClass {
    /*< private >*/
    DeviceClass parent;
    /*< public >*/

    /* This is what a VirtioDevice must implement */
    DeviceRealize realize;
    DeviceUnrealize unrealize;
    uint64_t (*get_features)(VirtIODevice *vdev,
                             uint64_t requested_features,
                             Error **errp);
    uint64_t (*bad_features)(VirtIODevice *vdev);
    void (*set_features)(VirtIODevice *vdev, uint64_t val);
    int (*validate_features)(VirtIODevice *vdev);
    void (*get_config)(VirtIODevice *vdev, uint8_t *config);
    void (*set_config)(VirtIODevice *vdev, const uint8_t *config);
    void (*reset)(VirtIODevice *vdev);
    void (*set_status)(VirtIODevice *vdev, uint8_t val);
    /* Test and clear event pending status.
     * Should be called after unmask to avoid losing events.
     * If backend does not support masking,
     * must check in frontend instead.
     */
    bool (*guest_notifier_pending)(VirtIODevice *vdev, int n);
    /* Mask/unmask events from this vq. Any events reported
     * while masked will become pending.
     * If backend does not support masking,
     * must mask in frontend instead.
     */
    void (*guest_notifier_mask)(VirtIODevice *vdev, int n, bool mask);
    /* Saving and loading of a device; trying to deprecate save/load
     * use vmsd for new devices.
     */
    void (*save)(VirtIODevice *vdev, QEMUFile *f);
    int (*load)(VirtIODevice *vdev, QEMUFile *f, int version_id);
    const VMStateDescription *vmsd;
} VirtioDeviceClass;


static const TypeInfo device_type_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DeviceState),
    .instance_init = device_initfn,
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


static const TypeInfo virtio_net_info = {
    .name = TYPE_VIRTIO_NET,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIONet),
    .instance_init = virtio_net_instance_init,
    .class_init = virtio_net_class_init,
};

/********object_initialize 中先执行class_init, 然后执行instance_init*********/


/********class_init的root***** */
/**
 * 调用顺序
 * device_class_init-->virtio_device_class_init-->virtio_net_class_init
 *
 */
static void device_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);

    class->unparent = device_unparent;
	/* 设置 DeviceClass 的 realize 函数 */
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

static Property virtio_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIODevice, host_features),
    DEFINE_PROP_BOOL("__com.redhat_rhel6_ctrl_guest_workaround", VirtIODevice,
                     rhel6_ctrl_guest_workaround, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_device_class_init(ObjectClass *klass, void *data)
{
    /* Set the default value here. */
    DeviceClass *dc = DEVICE_CLASS(klass);
	/* 设置 DeviceClass->realize */
    dc->realize = virtio_device_realize;
    dc->unrealize = virtio_device_unrealize;
    dc->bus_type = TYPE_VIRTIO_BUS;
    dc->props = virtio_properties;
}

#define DEFINE_PROP(_name, _state, _field, _prop, _type) { \
			.name	   = (_name),									 \
			.info	   = &(_prop),									 \
			.offset    = offsetof(_state, _field)					 \
				+ type_check(_type, typeof_field(_state, _field)),	 \
			}

#define DEFINE_PROP_NETDEV(_n, _s, _f)             \
		DEFINE_PROP(_n, _s, _f, qdev_prop_netdev, NICPeers)


#define DEFINE_NIC_PROPERTIES(_state, _conf)                            \
    DEFINE_PROP_MACADDR("mac",   _state, _conf.macaddr),                \
    DEFINE_PROP_VLAN("vlan",     _state, _conf.peers),                   \
    DEFINE_PROP_NETDEV("netdev", _state, _conf.peers)
	/* netdev属性用于将virtio-net和netdev联系起来 */

static Property virtio_net_properties[] = {
    DEFINE_PROP_BIT("csum", VirtIONet, host_features, VIRTIO_NET_F_CSUM, true),
    DEFINE_PROP_BIT("guest_csum", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_CSUM, true),
    DEFINE_PROP_BIT("gso", VirtIONet, host_features, VIRTIO_NET_F_GSO, true),
    DEFINE_PROP_BIT("guest_tso4", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_TSO4, true),
    DEFINE_PROP_BIT("guest_tso6", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_TSO6, true),
    DEFINE_PROP_BIT("guest_ecn", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_ECN, true),
    DEFINE_PROP_BIT("guest_ufo", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_UFO, true),
    DEFINE_PROP_BIT("guest_announce", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_ANNOUNCE, true),
    DEFINE_PROP_BIT("host_tso4", VirtIONet, host_features,
                    VIRTIO_NET_F_HOST_TSO4, true),
    DEFINE_PROP_BIT("host_tso6", VirtIONet, host_features,
                    VIRTIO_NET_F_HOST_TSO6, true),
    DEFINE_PROP_BIT("host_ecn", VirtIONet, host_features,
                    VIRTIO_NET_F_HOST_ECN, true),
    DEFINE_PROP_BIT("host_ufo", VirtIONet, host_features,
                    VIRTIO_NET_F_HOST_UFO, true),
    DEFINE_PROP_BIT("mrg_rxbuf", VirtIONet, host_features,
                    VIRTIO_NET_F_MRG_RXBUF, true),
    DEFINE_PROP_BIT("status", VirtIONet, host_features,
                    VIRTIO_NET_F_STATUS, true),
    DEFINE_PROP_BIT("ctrl_vq", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_VQ, true),
    DEFINE_PROP_BIT("ctrl_rx", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_RX, true),
    DEFINE_PROP_BIT("ctrl_vlan", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_VLAN, true),
    DEFINE_PROP_BIT("ctrl_rx_extra", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_RX_EXTRA, true),
    DEFINE_PROP_BIT("ctrl_mac_addr", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_MAC_ADDR, true),
    DEFINE_PROP_BIT("ctrl_guest_offloads", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_GUEST_OFFLOADS, true),
    DEFINE_PROP_BIT("mq", VirtIONet, host_features, VIRTIO_NET_F_MQ, false),
    DEFINE_NIC_PROPERTIES(VirtIONet, nic_conf),
    DEFINE_PROP_UINT32("x-txtimer", VirtIONet, net_conf.txtimer,
                       TX_TIMER_INTERVAL),
    DEFINE_PROP_INT32("x-txburst", VirtIONet, net_conf.txburst, TX_BURST),
    DEFINE_PROP_STRING("tx", VirtIONet, net_conf.tx),
    DEFINE_PROP_UINT16("rx_queue_size", VirtIONet, net_conf.rx_queue_size,
                       VIRTIO_NET_RX_QUEUE_DEFAULT_SIZE),
    DEFINE_PROP_UINT16("tx_queue_size", VirtIONet, net_conf.tx_queue_size,
                       VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE),
    DEFINE_PROP_END_OF_LIST(),
};


static void virtio_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
	/* 重新设置 dc->props */
    dc->props = virtio_net_properties;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    vdc->realize = virtio_net_device_realize;
    vdc->unrealize = virtio_net_device_unrealize;
    vdc->get_config = virtio_net_get_config;
    vdc->set_config = virtio_net_set_config;
    vdc->get_features = virtio_net_get_features;
    vdc->set_features = virtio_net_set_features;
    vdc->bad_features = virtio_net_bad_features;
    vdc->reset = virtio_net_reset;
    vdc->set_status = virtio_net_set_status;
    vdc->guest_notifier_mask = virtio_net_guest_notifier_mask;
    vdc->guest_notifier_pending = virtio_net_guest_notifier_pending;
    vdc->load = virtio_net_load_device;
    vdc->save = virtio_net_save_device;
}

/************instance_init root****************/

/**
 * 调用顺序
 * object_instance_init-->device_initfn-->virtio_net_instance_init
 *
 */

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
	/* 添加 realized  属性   */
    object_property_add_bool(obj, "realized", device_get_realized, device_set_realized, NULL);
    object_property_add_bool(obj, "hotpluggable", device_get_hotpluggable, NULL, NULL);
    object_property_add_bool(obj, "hotplugged", device_get_hotplugged, device_set_hotplugged, &error_abort);

    class = object_get_class(OBJECT(dev));
    do {
		/* 将 DeviceClass 的props 属性 添加到 dev中 */
        for (prop = DEVICE_CLASS(class)->props; prop && prop->name; prop++) {
            qdev_property_add_legacy(dev, prop, &error_abort);
            qdev_property_add_static(dev, prop, &error_abort);
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));

    object_property_add_link(OBJECT(dev), "parent_bus", TYPE_BUS, (Object **)&dev->parent_bus, NULL, 0,
                             &error_abort);
    QLIST_INIT(&dev->gpios);
}

static void virtio_net_instance_init(Object *obj)
{
    VirtIONet *n = VIRTIO_NET(obj);

    /*
     * The default config_size is sizeof(struct virtio_net_config).
     * Can be overriden with virtio_net_set_config_size.
     */
    n->config_size = sizeof(struct virtio_net_config);
    device_add_bootindex_property(obj, &n->nic_conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(n), NULL);
}


/***************relealized**********************/

/***
 * device_set_realized-->virtio_device_realize-->virtio_net_device_realize
 *
 */
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
        	/* 如果该dev下有bus，则会调用bus随意的relaize函数 */
            object_property_set_bool(OBJECT(bus), true, "realized",
                                         &local_err);
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

	/* 调用 virtio_net_device_realize */
    if (vdc->realize != NULL) {
        vdc->realize(dev, &err);

    }

    virtio_bus_device_plugged(vdev, &err);
}

#define VIRTIO_ID_NET		1 /* virtio net */
#define VIRTIO_ID_BLOCK		2 /* virtio block */
#define VIRTIO_ID_CONSOLE	3 /* virtio console */
#define VIRTIO_ID_RNG		4 /* virtio rng */
#define VIRTIO_ID_BALLOON	5 /* virtio balloon */
#define VIRTIO_ID_RPMSG		7 /* virtio remote processor messaging */
#define VIRTIO_ID_SCSI		8 /* virtio scsi */
#define VIRTIO_ID_9P		9 /* 9p virtio console */
#define VIRTIO_ID_RPROC_SERIAL 11 /* virtio remoteproc serial link */
#define VIRTIO_ID_CAIF	       12 /* Virtio caif */
#define VIRTIO_ID_GPU          16 /* virtio GPU */
#define VIRTIO_ID_INPUT        18 /* virtio input */


static void virtio_net_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIONet *n = VIRTIO_NET(dev);
    NetClientState *nc;
    int i;

    virtio_net_set_config_size(n, n->host_features);
    virtio_init(vdev, "virtio-net", VIRTIO_ID_NET, n->config_size);

    /*
     * We set a lower limit on RX queue size to what it always was.
     * Guests that want a smaller ring can always resize it without
     * help from us (using virtio 1 and up).
     */
    if (n->net_conf.rx_queue_size < VIRTIO_NET_RX_QUEUE_MIN_SIZE ||
        n->net_conf.rx_queue_size > VIRTQUEUE_MAX_SIZE ||
        (n->net_conf.rx_queue_size & (n->net_conf.rx_queue_size - 1))) {
        error_setg(errp, "Invalid rx_queue_size (= %" PRIu16 "), "
                   "must be a power of 2 between %d and %d.",
                   n->net_conf.rx_queue_size, VIRTIO_NET_RX_QUEUE_MIN_SIZE,
                   VIRTQUEUE_MAX_SIZE);
        virtio_cleanup(vdev);
        return;
    }

    if (n->net_conf.tx_queue_size < VIRTIO_NET_TX_QUEUE_MIN_SIZE ||
        n->net_conf.tx_queue_size > VIRTQUEUE_MAX_SIZE ||
        !is_power_of_2(n->net_conf.tx_queue_size)) {
        error_setg(errp, "Invalid tx_queue_size (= %" PRIu16 "), "
                   "must be a power of 2 between %d and %d",
                   n->net_conf.tx_queue_size, VIRTIO_NET_TX_QUEUE_MIN_SIZE,
                   VIRTQUEUE_MAX_SIZE);
        virtio_cleanup(vdev);
        return;
    }

    n->max_queues = MAX(n->nic_conf.peers.queues, 1);
    if (n->max_queues * 2 + 1 > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "Invalid number of queues (= %" PRIu32 "), "
                   "must be a positive integer less than %d.",
                   n->max_queues, (VIRTIO_QUEUE_MAX - 1) / 2);
        virtio_cleanup(vdev);
        return;
    }
    n->vqs = g_malloc0(sizeof(VirtIONetQueue) * n->max_queues);
    n->curr_queues = 1;
    n->tx_timeout = n->net_conf.txtimer;

    if (n->net_conf.tx && strcmp(n->net_conf.tx, "timer")
                       && strcmp(n->net_conf.tx, "bh")) {
        error_report("virtio-net: "
                     "Unknown option tx=%s, valid options: \"timer\" \"bh\"",
                     n->net_conf.tx);
        error_report("Defaulting to \"bh\"");
    }

    for (i = 0; i < n->max_queues; i++) {
        virtio_net_add_queue(n, i);
    }

    n->ctrl_vq = virtio_add_queue(vdev, 64, virtio_net_handle_ctrl);
    qemu_macaddr_default_if_unset(&n->nic_conf.macaddr);
    memcpy(&n->mac[0], &n->nic_conf.macaddr, sizeof(n->mac));
    n->status = VIRTIO_NET_S_LINK_UP;
    n->announce_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                     virtio_net_announce_timer, n);

    if (n->netclient_type) {
        /*
         * Happen when virtio_net_set_netclient_name has been called.
         */
        n->nic = qemu_new_nic(&net_virtio_info, &n->nic_conf,
                              n->netclient_type, n->netclient_name, n);
    } else {
        n->nic = qemu_new_nic(&net_virtio_info, &n->nic_conf,
                              object_get_typename(OBJECT(dev)), dev->id, n);
    }

    peer_test_vnet_hdr(n);
    if (peer_has_vnet_hdr(n)) {
        for (i = 0; i < n->max_queues; i++) {
            qemu_using_vnet_hdr(qemu_get_subqueue(n->nic, i)->peer, true);
        }
        n->host_hdr_len = sizeof(struct virtio_net_hdr);
    } else {
        n->host_hdr_len = 0;
    }

    qemu_format_nic_info_str(qemu_get_queue(n->nic), n->nic_conf.macaddr.a);

    n->vqs[0].tx_waiting = 0;
    n->tx_burst = n->net_conf.txburst;
    virtio_net_set_mrg_rx_bufs(n, 0, 0);
    n->promisc = 1; /* for compatibility */

    n->mac_table.macs = g_malloc0(MAC_TABLE_ENTRIES * ETH_ALEN);

    n->vlans = g_malloc0(MAX_VLAN >> 3);

    nc = qemu_get_queue(n->nic);
    nc->rxfilter_notify_enabled = 1;

    n->qdev = dev;
    register_savevm(dev, "virtio-net", -1, VIRTIO_NET_VM_VERSION,
                    virtio_net_save, virtio_net_load, n);
}


NetClientState *qemu_get_queue(NICState *nic)
{
    return qemu_get_subqueue(nic, 0);
}

NetClientState *qemu_get_subqueue(NICState *nic, int queue_index)
{
    return nic->ncs + queue_index;
}

/* A VirtIODevice is being plugged */
void virtio_bus_device_plugged(VirtIODevice *vdev, Error **errp)
{
    DeviceState *qdev = DEVICE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(qdev));
    VirtioBusState *bus = VIRTIO_BUS(qbus);
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(bus);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);

    DPRINTF("%s: plug device.\n", qbus->name);

    if (klass->pre_plugged != NULL) {
        klass->pre_plugged(qbus->parent, errp);
    }

    /* Get the features of the plugged device. */
    assert(vdc->get_features != NULL);
    vdev->host_features = vdc->get_features(vdev, vdev->host_features,
                                            errp);
	/* 调用virtio_pci_device_plugged */
    if (klass->device_plugged != NULL) {
        klass->device_plugged(qbus->parent, errp);
    }
}

