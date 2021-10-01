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
};

static const TypeInfo vhost_user_blk_pci_info = {
    .name           = TYPE_VHOST_USER_BLK_PCI,
    .parent         = TYPE_VIRTIO_PCI,
    .instance_size  = sizeof(VHostUserBlkPCI),
    .instance_init  = vhost_user_blk_pci_instance_init,
    .class_init     = vhost_user_blk_pci_class_init,
};

#define TYPE_OBJECT "object"
#define TYPE_DEVICE "device"
#define TYPE_PCI_DEVICE "pci-device"
#define TYPE_VIRTIO_PCI "virtio-pci"
#define TYPE_VHOST_USER_BLK "vhost-user-blk"

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

typedef struct DeviceClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    DECLARE_BITMAP(categories, DEVICE_CATEGORY_MAX);
    const char *fw_name;
    const char *desc;
    Property *props;

    bool cannot_instantiate_with_device_add_yet;
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

typedef struct VirtioPCIClass {
    PCIDeviceClass parent_class;
    DeviceRealize parent_dc_realize;
    void (*realize)(VirtIOPCIProxy *vpci_dev, Error **errp);
} VirtioPCIClass;

/***********instance size***********/
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

struct VHostUserBlkPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserBlk vdev;
};

typedef struct VHostUserBlk {
    VirtIODevice parent_obj;
    CharDriverState *chardev;
    int32_t bootindex;
    struct virtio_blk_config blkcfg;
    uint16_t num_queues;
    uint32_t queue_size;
    uint32_t config_wce;
    uint32_t config_ro;
    struct vhost_dev dev;
    struct vhost_inflight *inflight;
    guint watch;
    bool should_start;
    bool connected;
} VHostUserBlk;

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



/* device_init_func-->qdev_device_add 中会先执行class_init，然后才是instance_init */

/********class_init的root***** */

/**
 * 调用顺序
 * device_class_init-->pci_device_class_init-->virtio_pci_class_init-->vhost_user_blk_pci_class_init
 *
 */

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

static void pci_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

	/* 修改了device_class_init中设置的k->realize */
    k->realize = pci_qdev_realize;
    k->unrealize = pci_qdev_unrealize;
    k->bus_type = TYPE_PCI_BUS;
    k->props = pci_props;
	/* PCIDeviceClass的 realize赋值 */
    pc->realize = pci_default_realize;
}

#define PCI_VENDOR_ID_REDHAT_QUMRANET    0x1af4

static void virtio_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *vpciklass = VIRTIO_PCI_CLASS(klass);

    dc->props = virtio_pci_properties;

	/* 修改了 pci_device_class_init 中realize的值 */
    k->realize = virtio_pci_realize;
    k->exit = virtio_pci_exit;
	/* 初始化pic的vendir id */
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_OTHERS;

	/* 将dc->realize赋值给parent_dc_realize，即pci_qdev_realize */
    vpciklass->parent_dc_realize = dc->realize;

	/* 修改了pci_device_class_init 中设置的dc->realize 
	* 所以当执行object_property_set_bool(OBJECT(dev), true, "realized", &err)
	* 时device_set_realized会调用virtio_pci_dc_realize
	*/
    dc->realize = virtio_pci_dc_realize;
    dc->reset = virtio_pci_reset;
}

static void vhost_user_blk_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

	/* DeviceClass props最终的值 */
    dc->props = vhost_user_blk_pci_properties;

	/* 修改了virtio_pci_class_init中realize的值 */
    k->realize = vhost_user_blk_pci_realize;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_BLOCK;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}


/************instance_init root****************/

/**
 * 调用顺序
 * object_instance_init-->device_initfn-->vhost_user_blk_pci_instance_init
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
		/* 将DeviceClass->props的静态定义属性添加到            dev中 */
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

static void vhost_user_blk_pci_instance_init(Object *obj)
{
    VHostUserBlkPCI *dev = VHOST_USER_BLK_PCI(obj);

	/* struct VHostUserBlkPCI {
     *		VirtIOPCIProxy parent_obj;
     *		VHostUserBlk vdev;
	 * };
	 * VHostUserBlkPCI将PCIDevice和VirtIODevice联系在了一起
	 * 初始化 TYPE_VHOST_USER_BLK
	 */
    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_BLK);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex", &error_abort);
}
/***************** instance_init  end ********************/



void virtio_instance_init_common(Object *proxy_obj, void *data,
                                 size_t vdev_size, const char *vdev_name)
{
    DeviceState *vdev = data;
	/* vdev_name 即 TYPE_VHOST_USER_BLK，初始化 TYPE_VHOST_USER_BLK */
    object_initialize(vdev, vdev_size, vdev_name);
    object_property_add_child(proxy_obj, "virtio-backend", OBJECT(vdev), NULL);
    object_unref(OBJECT(vdev));
    qdev_alias_all_properties(vdev, proxy_obj);
}

void object_initialize(void *data, size_t size, const char *typename)
{
	/* typename 即 TYPE_VHOST_USER_BLK */
    TypeImpl *type = type_get_by_name(typename);

    object_initialize_with_type(data, size, type);
}


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


/* 调用  根Object 的relaize函数，函数额调用顺序如下：
 * property_set_bool->device_set_realized->virtio_pci_dc_realize
 * pci_qdev_realize->virtio_pci_realize->vhost_user_blk_pci_realize
 * 
 * object_property_set->property_set_bool->device_set_realized->
 * virtio_device_realize->vhost_user_blk_device_realize
 */


/* relaize  */
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
	/* 调用 vhost_user_blk_pci_realize   */
    if (k->realize) {
        k->realize(proxy, errp);
    }
}


static void vhost_user_blk_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserBlkPCI *dev = VHOST_USER_BLK_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = dev->vdev.num_queues + 1;
    }

	/* 总线的名type     TYPE_VIRTIO_PCI_BUS */
    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
	/* 调用 TYPE_VHOST_USER_BLK 对应的 realized属性  
	 * vhost_user_blk_pci_instance_init 中进行了TYPE_VHOST_USER_BLK初始化
	 * 将vhost_user_blk_pci和vhost_user_blk联系在了一起
	 */
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

/**********realize end*******/

/* -1 for devfn means auto assign */
static PCIDevice *do_pci_register_device(PCIDevice *pci_dev, PCIBus *bus,
                                         const char *name, int devfn,
                                         Error **errp)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pci_dev);
    PCIConfigReadFunc *config_read = pc->config_read;
    PCIConfigWriteFunc *config_write = pc->config_write;
    Error *local_err = NULL;
    DeviceState *dev = DEVICE(pci_dev);

    pci_dev->bus = bus;
    /* Only pci bridges can be attached to extra PCI root buses */
    if (pci_bus_is_root(bus) && bus->parent_dev && !pc->is_bridge) {
        error_setg(errp, "PCI: Only PCI/PCIe bridges can be plugged into %s",
                    bus->parent_dev->name);
        return NULL;
    }

    if (devfn < 0) { /* devfn为-1表示自动分配 */
        for(devfn = bus->devfn_min ; devfn < ARRAY_SIZE(bus->devices);
            devfn += PCI_FUNC_MAX) {
            if (!bus->devices[devfn])
                goto found;
        }
        error_setg(errp, "PCI: no slot/function available for %s, all in use", name);
        return NULL;
    found: ;
    } else if (bus->devices[devfn]) { /* 已经被使用 */
        error_setg(errp, "PCI: slot %d function %d not available for %s,"
                   " in use by %s",
                   PCI_SLOT(devfn), PCI_FUNC(devfn), name,
                   bus->devices[devfn]->name);
        return NULL;
    } else if (dev->hotplugged && pci_get_function_0(pci_dev)) {
		error_setg(errp, "PCI: slot %d function 0 already ocuppied by %s,"
					" new func %s cannot be exposed to guest.",
					PCI_SLOT(devfn),
					bus->devices[PCI_DEVFN(PCI_SLOT(devfn), 0)]->name,
					name);

       return NULL;
    }

    pci_dev->devfn = devfn;
    pci_dev->requester_id_cache = pci_req_id_cache_get(pci_dev);

    if (qdev_hotplug) {
        pci_init_bus_master(pci_dev);
    }
    pstrcpy(pci_dev->name, sizeof(pci_dev->name), name);
    pci_dev->irq_state = 0;
	/* 分配配置空间 */
    pci_config_alloc(pci_dev);
	/* 初始化配置空间 */
    pci_config_set_vendor_id(pci_dev->config, pc->vendor_id);
    pci_config_set_device_id(pci_dev->config, pc->device_id);
    pci_config_set_revision(pci_dev->config, pc->revision);
    pci_config_set_class(pci_dev->config, pc->class_id);

    if (!pc->is_bridge) {
        if (pc->subsystem_vendor_id || pc->subsystem_id) {
            pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID, pc->subsystem_vendor_id);
            pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID, pc->subsystem_id);
        } else {
            pci_set_default_subsystem_id(pci_dev);
        }
    } else {

    }
    pci_init_cmask(pci_dev);
    pci_init_wmask(pci_dev);
    pci_init_w1cmask(pci_dev);
    if (pc->is_bridge) {
        pci_init_mask_bridge(pci_dev);
    }
    pci_init_multifunction(bus, pci_dev, &local_err);

    if (!config_read)
        config_read = pci_default_read_config;
    if (!config_write)
        config_write = pci_default_write_config;
	/* 初始化读写函数 */
    pci_dev->config_read = config_read;
    pci_dev->config_write = config_write;
    bus->devices[devfn] = pci_dev;
    pci_dev->version_id = 2; /* Current pci device vmstate version */
    return pci_dev;
}

static void pci_config_alloc(PCIDevice *pci_dev)
{
    int config_size = pci_config_size(pci_dev);

    pci_dev->config = g_malloc0(config_size);
    pci_dev->cmask = g_malloc0(config_size);
    pci_dev->wmask = g_malloc0(config_size);
    pci_dev->w1cmask = g_malloc0(config_size);
    pci_dev->used = g_malloc0(config_size);
}

static void virtio_pci_bus_new(VirtioBusState *bus, size_t bus_size,
                               VirtIOPCIProxy *dev)
{
    DeviceState *qdev = DEVICE(dev);
    char virtio_bus_name[] = "virtio-bus";

    qbus_create_inplace(bus, bus_size, TYPE_VIRTIO_PCI_BUS, qdev,
                        virtio_bus_name);
}

#define TYPE_VIRTIO_PCI_BUS "virtio-pci-bus"

void qbus_create_inplace(void *bus, size_t size, const char *typename,
                         DeviceState *parent, const char *name)
{
	/* 初始化 typeImpl了，递归调用class_init和inctance_init */
    object_initialize(bus, size, typename);
    qbus_realize(bus, parent, name);
}

static void qbus_realize(BusState *bus, DeviceState *parent, const char *name)
{
    const char *typename = object_get_typename(OBJECT(bus));
    BusClass *bc;
    char *buf;
    int i, len, bus_id;

    bus->parent = parent;

    if (name) {
        bus->name = g_strdup(name);
    } else if (bus->parent && bus->parent->id) {
        /* parent device has id -> use it plus parent-bus-id for bus name */
        bus_id = bus->parent->num_child_bus;

        len = strlen(bus->parent->id) + 16;
        buf = g_malloc(len);
        snprintf(buf, len, "%s.%d", bus->parent->id, bus_id);
        bus->name = buf;
    } else {
        /* no id -> use lowercase bus type plus global bus-id for bus name */
        bc = BUS_GET_CLASS(bus);
        bus_id = bc->automatic_ids++;

        len = strlen(typename) + 16;
        buf = g_malloc(len);
        len = snprintf(buf, len, "%s.%d", typename, bus_id);
        for (i = 0; i < len; i++) {
            buf[i] = qemu_tolower(buf[i]);
        }
        bus->name = buf;
    }

    if (bus->parent) {
		/* 将该bus添加到父bus的链表中 */
        QLIST_INSERT_HEAD(&bus->parent->child_bus, bus, sibling);
        bus->parent->num_child_bus++;
        object_property_add_child(OBJECT(bus->parent), bus->name, OBJECT(bus), NULL);
        object_unref(OBJECT(bus));
    } else if (bus != sysbus_get_default()) {
        /* TODO: once all bus devices are qdevified,
           only reset handler for main_system_bus should be registered here. */
        qemu_register_reset(qbus_reset_all_fn, bus);
    }
}

tatic void bus_add_child(BusState *bus, DeviceState *child)
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

