struct TypeImpl
{
    const char *name;

    size_t class_size;

    size_t instance_size;

    void (*class_init)(ObjectClass *klass, void *data);
    void (*class_base_init)(ObjectClass *klass, void *data);
    void (*class_finalize)(ObjectClass *klass, void *data);

    void *class_data;

    void (*instance_init)(Object *obj);
    void (*instance_post_init)(Object *obj);
    void (*instance_finalize)(Object *obj);

    bool abstract;

    const char *parent;
    TypeImpl *parent_type;

    ObjectClass *class;

    int num_interfaces;
    InterfaceImpl interfaces[MAX_INTERFACES];
};


typedef struct TypeImpl *Type;


struct ObjectClass
{
    /*< private >*/
    Type type;
    GSList *interfaces;

    const char *object_cast_cache[OBJECT_CLASS_CAST_CACHE];
    const char *class_cast_cache[OBJECT_CLASS_CAST_CACHE];

    ObjectUnparent *unparent;

    GHashTable *properties;
};


struct Object
{
    /*< private >*/
    ObjectClass *class;
    ObjectFree *free;
    GHashTable *properties;
    uint32_t ref;
    Object *parent;
};


struct DeviceState {
    /*< private >*/
    Object parent_obj;
    /*< public >*/

    const char *id;
    bool realized;
    bool pending_deleted_event;
    QemuOpts *opts;
    int hotplugged;
    BusState *parent_bus;
    QLIST_HEAD(, NamedGPIOList) gpios;
    QLIST_HEAD(, BusState) child_bus;
    int num_child_bus;
    int instance_id_alias;
    int alias_required_for_version;
};

typedef struct DeviceClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    DECLARE_BITMAP(categories, DEVICE_CATEGORY_MAX);
    const char *fw_name;
    const char *desc;
    Property *props;

    /*
     * Shall we hide this device model from -device / device_add?
     * All devices should support instantiation with device_add, and
     * this flag should not exist.  But we're not there, yet.  Some
     * devices fail to instantiate with cryptic error messages.
     * Others instantiate, but don't work.  Exposing users to such
     * behavior would be cruel; this flag serves to protect them.  It
     * should never be set without a comment explaining why it is set.
     * TODO remove once we're there
     */
    bool cannot_instantiate_with_device_add_yet;
    /*
     * Does this device model survive object_unref(object_new(TNAME))?
     * All device models should, and this flag shouldn't exist.  Some
     * devices crash in object_new(), some crash or hang in
     * object_unref().  Makes introspecting properties with
     * qmp_device_list_properties() dangerous.  Bad, because it's used
     * by -device FOO,help.  This flag serves to protect that code.
     * It should never be set without a comment explaining why it is
     * set.
     * TODO remove once we're there
     */
    bool cannot_destroy_with_object_finalize_yet;

    bool hotpluggable;

    /* callbacks */
    void (*reset)(DeviceState *dev);
    DeviceRealize realize;
    DeviceUnrealize unrealize;

    /* device state */
    const struct VMStateDescription *vmsd;

    /* Private to qdev / bus.  */
    qdev_initfn init; /* TODO remove, once users are converted to realize */
    qdev_event exit; /* TODO remove, once users are converted to unrealize */
    const char *bus_type;
} DeviceClass;

struct PCIDevice {
    DeviceState qdev;

    /* PCI config space */
    uint8_t *config;

    /* Used to enable config checks on load. Note that writable bits are
     * never checked even if set in cmask. */
    uint8_t *cmask;

    /* Used to implement R/W bytes */
    uint8_t *wmask;

    /* Used to implement RW1C(Write 1 to Clear) bytes */
    uint8_t *w1cmask;

    /* Used to allocate config space for capabilities. */
    uint8_t *used;

    /* the following fields are read only */
    PCIBus *bus;
    int32_t devfn;
    /* Cached device to fetch requester ID from, to avoid the PCI
     * tree walking every time we invoke PCI request (e.g.,
     * MSI). For conventional PCI root complex, this field is
     * meaningless. */
    PCIReqIDCache requester_id_cache;
    char name[64];
    PCIIORegion io_regions[PCI_NUM_REGIONS];
    AddressSpace bus_master_as;
    MemoryRegion bus_master_enable_region;

    /* do not access the following fields */
    PCIConfigReadFunc *config_read;
    PCIConfigWriteFunc *config_write;

    /* Legacy PCI VGA regions */
    MemoryRegion *vga_regions[QEMU_PCI_VGA_NUM_REGIONS];
    bool has_vga;

    /* Current IRQ levels.  Used internally by the generic PCI code.  */
    uint8_t irq_state;

    /* Capability bits */
    uint32_t cap_present;

    /* Offset of MSI-X capability in config space */
    uint8_t msix_cap;

    /* MSI-X entries */
    int msix_entries_nr;

    /* Space to store MSIX table & pending bit array */
    uint8_t *msix_table;
    uint8_t *msix_pba;
    /* MemoryRegion container for msix exclusive BAR setup */
    MemoryRegion msix_exclusive_bar;
    /* Memory Regions for MSIX table and pending bit entries. */
    MemoryRegion msix_table_mmio;
    MemoryRegion msix_pba_mmio;
    /* Reference-count for entries actually in use by driver. */
    unsigned *msix_entry_used;
    /* MSIX function mask set or MSIX disabled */
    bool msix_function_masked;
    /* Version id needed for VMState */
    int32_t version_id;

    /* Offset of MSI capability in config space */
    uint8_t msi_cap;

    /* PCI Express */
    PCIExpressDevice exp;

    /* SHPC */
    SHPCDevice *shpc;

    /* Location of option rom */
    char *romfile;
    bool has_rom;
    MemoryRegion rom;
    uint32_t rom_bar;

    /* INTx routing notifier */
    PCIINTxRoutingNotifier intx_routing_notifier;

    /* MSI-X notifiers */
    MSIVectorUseNotifier msix_vector_use_notifier;
    MSIVectorReleaseNotifier msix_vector_release_notifier;
    MSIVectorPollNotifier msix_vector_poll_notifier;
};

typedef struct PCIDeviceClass {
    DeviceClass parent_class;

    void (*realize)(PCIDevice *dev, Error **errp);
    int (*init)(PCIDevice *dev);/* TODO convert to realize() and remove */
    PCIUnregisterFunc *exit;
    PCIConfigReadFunc *config_read;
    PCIConfigWriteFunc *config_write;

    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision;
    uint16_t class_id;
    uint16_t subsystem_vendor_id;       /* only for header type = 0 */
    uint16_t subsystem_id;              /* only for header type = 0 */

    /*
     * pci-to-pci bridge or normal device.
     * This doesn't mean pci host switch.
     * When card bus bridge is supported, this would be enhanced.
     */
    int is_bridge;

    /* pcie stuff */
    int is_express;   /* is this device pci express? */

    /* rom bar */
    const char *romfile;
} PCIDeviceClass;

struct VirtIOPCIProxy {
    PCIDevice pci_dev;
    MemoryRegion bar;
    VirtIOPCIRegion common;
    VirtIOPCIRegion isr;
    VirtIOPCIRegion device;
    VirtIOPCIRegion notify;
    VirtIOPCIRegion notify_pio;
    MemoryRegion modern_bar;
    MemoryRegion io_bar;
    MemoryRegion modern_cfg;
    AddressSpace modern_as;
    uint32_t legacy_io_bar;
    uint32_t msix_bar;
    uint32_t modern_io_bar;
    uint32_t modern_mem_bar;
    int config_cap;
    uint32_t flags;
    bool disable_modern;
    OnOffAuto disable_legacy;
    uint32_t class_code;
    uint32_t nvectors;
    uint32_t dfselect;
    uint32_t gfselect;
    uint32_t guest_features[2];
    VirtIOPCIQueue vqs[VIRTIO_QUEUE_MAX];

    bool ioeventfd_disabled;
    bool ioeventfd_started;
    VirtIOIRQFD *vector_irqfd;
    int nvqs_with_notifiers;
    VirtioBusState bus;
};

typedef struct VirtioPCIClass {
    PCIDeviceClass parent_class;
    DeviceRealize parent_dc_realize;
    void (*realize)(VirtIOPCIProxy *vpci_dev, Error **errp);
} VirtioPCIClass;

typedef struct NICConf {
    MACAddr macaddr;
    NICPeers peers;
    int32_t bootindex;
} NICConf;


typedef struct VirtIONet {
    VirtIODevice parent_obj;
    uint8_t mac[ETH_ALEN];
    uint16_t status;
    VirtIONetQueue *vqs;
    VirtQueue *ctrl_vq;
    NICState *nic;
    uint32_t tx_timeout;
    int32_t tx_burst;
    uint32_t has_vnet_hdr;
    size_t host_hdr_len;
    size_t guest_hdr_len;
    uint32_t host_features;
    uint8_t has_ufo;
    int mergeable_rx_bufs;
    uint8_t promisc;
    uint8_t allmulti;
    uint8_t alluni;
    uint8_t nomulti;
    uint8_t nouni;
    uint8_t nobcast;
    uint8_t vhost_started;
    struct {
        uint32_t in_use;
        uint32_t first_multi;
        uint8_t multi_overflow;
        uint8_t uni_overflow;
        uint8_t *macs;
    } mac_table;
    uint32_t *vlans;
    virtio_net_conf net_conf;
    NICConf nic_conf;
    DeviceState *qdev;
    int multiqueue;
    uint16_t max_queues;
    uint16_t curr_queues;
    size_t config_size;
    char *netclient_name;
    char *netclient_type;
    uint64_t curr_guest_offloads;
    QEMUTimer *announce_timer;
    int announce_counter;
    bool needs_vnet_hdr_swap;
} VirtIONet;

struct VirtIONetPCI {
    VirtIOPCIProxy parent_obj;
    VirtIONet vdev;
};

struct VirtIODevice
{
    DeviceState parent_obj;
    const char *name;
    uint8_t status;
    uint8_t isr;
    uint16_t queue_sel;
    uint64_t guest_features;
    uint64_t host_features;
    size_t config_len;
    void *config;
    uint16_t config_vector;
    uint32_t generation;
    int nvectors;
    VirtQueue *vq;
    uint16_t device_id;
    bool vm_running;
    VMChangeStateEntry *vmstate;
    char *bus_name;
    uint8_t device_endian;
    bool use_guest_notifier_mask;
    bool rhel6_ctrl_guest_workaround;
    QLIST_HEAD(, VirtQueue) *vector_queues;
};


#define TYPE_OBJECT "object"
#define TYPE_DEVICE "device"
#define TYPE_PCI_DEVICE "pci-device"
#define TYPE_VIRTIO_PCI "virtio-pci"
#define TYPE_VIRTIO_NET_PCI "virtio-net-pci"
#define TYPE_VIRTIO_NET "virtio-net-device"

static void register_types(void)
{
    static TypeInfo interface_info = {
        .name = TYPE_INTERFACE,
        .class_size = sizeof(InterfaceClass),
        .abstract = true,
    };

    static TypeInfo object_info = {
        .name = TYPE_OBJECT,
        .instance_size = sizeof(Object),
        .instance_init = object_instance_init,
        .abstract = true,
    };

    type_interface = type_register_internal(&interface_info);
    type_register_internal(&object_info);
}

type_init(register_types)

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


static const TypeInfo pci_device_type_info = {
    .name = TYPE_PCI_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .abstract = true,
    .class_size = sizeof(PCIDeviceClass),
    .class_init = pci_device_class_init,
};


static const TypeInfo virtio_pci_info = {
    .name          = TYPE_VIRTIO_PCI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VirtIOPCIProxy),
    .class_init    = virtio_pci_class_init,
    .class_size    = sizeof(VirtioPCIClass),
    .abstract      = true,
}


static const TypeInfo virtio_net_pci_info = {
    .name          = TYPE_VIRTIO_NET_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIONetPCI),
    .instance_init = virtio_net_pci_instance_init,
    .class_init    = virtio_net_pci_class_init,
};


/* device_init_func-->qdev_device_add 中会先执行class_init，然后才是instance_init */

/********class_init的root***** */

/**
 * 调用顺序
 * device_class_init-->pci_device_class_init-->virtio_net_pci_class_init
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


static Property pci_props[] = {
    DEFINE_PROP_PCI_DEVFN("addr", PCIDevice, devfn, -1),
    DEFINE_PROP_STRING("romfile", PCIDevice, romfile),
    DEFINE_PROP_UINT32("rombar",  PCIDevice, rom_bar, 1),
    DEFINE_PROP_BIT("multifunction", PCIDevice, cap_present,
                    QEMU_PCI_CAP_MULTIFUNCTION_BITNR, false),
    DEFINE_PROP_BIT("command_serr_enable", PCIDevice, cap_present,
                    QEMU_PCI_CAP_SERR_BITNR, true),
    DEFINE_PROP_BIT("x-pcie-lnksta-dllla", PCIDevice, cap_present,
                    QEMU_PCIE_LNKSTA_DLLLA_BITNR, true),
    DEFINE_PROP_END_OF_LIST()
};


static void pci_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
	/* 将 DeviceClass的 realize 替换成  pci_qdev_realize  */
    k->realize = pci_qdev_realize;
    k->unrealize = pci_qdev_unrealize;
    k->bus_type = TYPE_PCI_BUS;
    k->props = pci_props;
    pc->realize = pci_default_realize;
}

static Property virtio_pci_properties[] = {
    DEFINE_PROP_BIT("virtio-pci-bus-master-bug-migration", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_BUS_MASTER_BUG_MIGRATION_BIT, false),
    DEFINE_PROP_ON_OFF_AUTO("disable-legacy", VirtIOPCIProxy, disable_legacy,
                            ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BOOL("disable-modern", VirtIOPCIProxy, disable_modern, false),
    DEFINE_PROP_BIT("migrate-extra", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_MIGRATE_EXTRA_BIT, true),
    DEFINE_PROP_BIT("modern-pio-notify", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY_BIT, false),
    DEFINE_PROP_BIT("x-disable-pcie", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_DISABLE_PCIE_BIT, false),
    DEFINE_PROP_BIT("page-per-vq", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_PAGE_PER_VQ_BIT, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *vpciklass = VIRTIO_PCI_CLASS(klass);

    dc->props = virtio_pci_properties;
    k->realize = virtio_pci_realize;
    k->exit = virtio_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_OTHERS;

	/* vpciklass->parent_dc_realize = pci_qdev_realize */
    vpciklass->parent_dc_realize = dc->realize;

	/* 将 DeviceClass 的 realize 在次替换成 virtio_pci_dc_realize */
    dc->realize = virtio_pci_dc_realize;
    dc->reset = virtio_pci_reset;
}

static Property virtio_net_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, false),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_net_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *vpciklass = VIRTIO_PCI_CLASS(klass);

    k->romfile = "pxe-virtio.rom";
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_VIRTIO_NET;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->props = virtio_net_properties;
    vpciklass->realize = virtio_net_pci_realize;
}

/************instance_init root****************/

/**
 * 调用顺序
 * object_instance_init-->device_initfn-->virtio_net_pci_instance_init
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

static void virtio_net_pci_instance_init(Object *obj)
{
    VirtIONetPCI *dev = VIRTIO_NET_PCI(obj);
	/**
	 *  struct VirtIONetPCI {
     *		VirtIOPCIProxy parent_obj;
     *		VirtIONet vdev;
	 *	};
	 *  VirtIONetPCI将pci设备和virtio设备联系在一起
	 */
    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_NET);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev), "bootindex", &error_abort);
}

void virtio_instance_init_common(Object *proxy_obj, void *data,
                                 size_t vdev_size, const char *vdev_name)
{
    DeviceState *vdev = data;
	/* 初始化  TYPE_VIRTIO_NET  
	 * object_initialize 中先执行class_init, 然后执行instance_init
	 */
    object_initialize(vdev, vdev_size, vdev_name);
    object_property_add_child(proxy_obj, "virtio-backend", OBJECT(vdev), NULL);
    object_unref(OBJECT(vdev));
	/* 将 TYPE_VIRTIO_NET 的 DeviceClass->props 在 proxy_obj 中创建一个别名
	 * 在 proxy_obj中设置该属性，就会调用 TYPE_VIRTIO_NET中的属性
	 */
    qdev_alias_all_properties(vdev, proxy_obj);
}



/***************relaize*******************/


/* 调用  根Object 的relaize函数，函数额调用顺序如下：
 * property_set_bool->device_set_realized->virtio_pci_dc_realize
 * pci_qdev_realize->virtio_pci_realize->virtio_net_pci_realize
 * 
 * object_property_set->property_set_bool->device_set_realized->
 * virtio_device_realize->vhost_user_blk_device_realize
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

		/* 调用 virtio_pci_dc_realize */
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


static void virtio_pci_dc_realize(DeviceState *qdev, Error **errp)
{
	/* obj->class  VirtioPCIClass 内嵌 ObjectClass */
    VirtioPCIClass *vpciklass = VIRTIO_PCI_GET_CLASS(qdev);
	/* VirtIOPCIProxy 内嵌Object */
    VirtIOPCIProxy *proxy = VIRTIO_PCI(qdev);
    PCIDevice *pci_dev = &proxy->pci_dev;

    if (!(proxy->flags & VIRTIO_PCI_FLAG_DISABLE_PCIE) &&
        virtio_pci_modern(proxy)) {
        pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    }

	/* 调用   pci_qdev_realize */
    vpciklass->parent_dc_realize(qdev, errp);
}


static void pci_qdev_realize(DeviceState *qdev, Error **errp)
{
    PCIDevice *pci_dev = (PCIDevice *)qdev;
	/* obj->class */
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pci_dev);
    Error *local_err = NULL;
    PCIBus *bus;
    bool is_default_rom;

    /* initialize cap_present for pci_is_express() and pci_config_size() */
    if (pc->is_express) {
        pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    }

    bus = PCI_BUS(qdev_get_parent_bus(qdev));
	/* 注册pci device */
    pci_dev = do_pci_register_device(pci_dev, bus, object_get_typename(OBJECT(qdev)),
                                     pci_dev->devfn, errp);

	/* 调用 virtio_pci_realize */
    if (pc->realize) {
        pc->realize(pci_dev, &local_err);
    }

    /* rom loading */
    is_default_rom = false;
    if (pci_dev->romfile == NULL && pc->romfile != NULL) {
        pci_dev->romfile = g_strdup(pc->romfile);
        is_default_rom = true;
    }

    pci_add_option_rom(pci_dev, is_default_rom, &local_err);
}


static void virtio_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(pci_dev);
    VirtioPCIClass *k = VIRTIO_PCI_GET_CLASS(pci_dev);
    bool pcie_port = pci_bus_is_express(pci_dev->bus) && !pci_bus_is_root(pci_dev->bus);

    /*
     * virtio pci bar layout used by default.
     * subclasses can re-arrange things if needed.
     *
     *   region 0   --  virtio legacy io bar
     *   region 1   --  msi-x bar
     *   region 4+5 --  virtio modern memory (64bit) bar
     *
     */
    proxy->legacy_io_bar  = 0;
    proxy->msix_bar       = 1;
    proxy->modern_io_bar  = 2;
    proxy->modern_mem_bar = 4;

    proxy->common.offset = 0x0;
    proxy->common.size = 0x1000;
    proxy->common.type = VIRTIO_PCI_CAP_COMMON_CFG;

    proxy->isr.offset = 0x1000;
    proxy->isr.size = 0x1000;
    proxy->isr.type = VIRTIO_PCI_CAP_ISR_CFG;

    proxy->device.offset = 0x2000;
    proxy->device.size = 0x1000;
    proxy->device.type = VIRTIO_PCI_CAP_DEVICE_CFG;

    proxy->notify.offset = 0x3000;
    proxy->notify.size = virtio_pci_queue_mem_mult(proxy) * VIRTIO_QUEUE_MAX;
    proxy->notify.type = VIRTIO_PCI_CAP_NOTIFY_CFG;

    proxy->notify_pio.offset = 0x0;
    proxy->notify_pio.size = 0x4;
    proxy->notify_pio.type = VIRTIO_PCI_CAP_NOTIFY_CFG;

    /* subclasses can enforce modern, so do this unconditionally */
    memory_region_init(&proxy->modern_bar, OBJECT(proxy), "virtio-pci",
                       /* PCI BAR regions must be powers of 2 */
                       pow2ceil(proxy->notify.offset + proxy->notify.size));

    memory_region_init_alias(&proxy->modern_cfg, OBJECT(proxy),
                             "virtio-pci-cfg", &proxy->modern_bar,
                             0, memory_region_size(&proxy->modern_bar));

    address_space_init(&proxy->modern_as, &proxy->modern_cfg, "virtio-pci-cfg-as");

    if (proxy->disable_legacy == ON_OFF_AUTO_AUTO) {
        proxy->disable_legacy = pcie_port ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
    }

	/* pcie相关 */
    if (pcie_port && pci_is_express(pci_dev)) {
        int pos;

        pos = pcie_endpoint_cap_init(pci_dev, 0);

        pos = pci_add_capability(pci_dev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF);

        /*
         * Indicates that this function complies with revision 1.2 of the
         * PCI Power Management Interface Specification.
         */
        pci_set_word(pci_dev->config + pos + PCI_PM_PMC, 0x3);
    } else {
        /*
         * make future invocations of pci_is_express() return false
         * and pci_config_size() return PCI_CONFIG_SPACE_SIZE.
         */
        pci_dev->cap_present &= ~QEMU_PCI_CAP_EXPRESS;
    }
	/* 创建virtio 总线 */
    virtio_pci_bus_new(&proxy->bus, sizeof(proxy->bus), proxy);
	/* 调用 virtio_net_pci_realize   */
    if (k->realize) {
        k->realize(proxy, errp);
    }
}


static void virtio_net_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    DeviceState *qdev = DEVICE(vpci_dev);
    VirtIONetPCI *dev = VIRTIO_NET_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    virtio_net_set_netclient_name(&dev->vdev, qdev->id, object_get_typename(OBJECT(qdev)));
    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
	/* 调用 TYPE_VIRTIO_NET relealized */
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

void virtio_net_set_netclient_name(VirtIONet *n, const char *name,
                                   const char *type)
{

    g_free(n->netclient_name);
    g_free(n->netclient_type);
    n->netclient_name = g_strdup(name);
    n->netclient_type = g_strdup(type);
}

const char *object_get_typename(Object *obj)
{
    return obj->class->type->name;
}

void qdev_set_parent_bus(DeviceState *dev, BusState *bus)
{
    dev->parent_bus = bus;
    object_ref(OBJECT(bus));
    bus_add_child(bus, dev);
}

static void bus_add_child(BusState *bus, DeviceState *child)
{
    char name[32];
    BusChild *kid = g_malloc0(sizeof(*kid));

    kid->index = bus->max_index++;
    kid->child = child;
    object_ref(OBJECT(kid->child));

    QTAILQ_INSERT_HEAD(&bus->children, kid, sibling);

    /* This transfers ownership of kid->child to the property.  */
    snprintf(name, sizeof(name), "child[%d]", kid->index);
    object_property_add_link(OBJECT(bus), name,
                             object_get_typename(OBJECT(child)),
                             (Object **)&kid->child,
                             NULL, /* read-only property */
                             0, /* return ownership on prop deletion */
                             NULL);
}







