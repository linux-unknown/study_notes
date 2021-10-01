static void register_types(void)
{
    static TypeInfo object_info = {
        .name = TYPE_OBJECT,
        .instance_size = sizeof(Object),
        .instance_init = object_instance_init,
        .abstract = true,
    };
}

type_init(register_types)



static const TypeInfo bus_info = {
    .name = TYPE_BUS,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(BusState),
    .abstract = true,
    .class_size = sizeof(BusClass),
    .instance_init = qbus_initfn,
    .instance_finalize = qbus_finalize,
    .class_init = bus_class_init,
};


static const TypeInfo virtio_bus_info = {
    .name = TYPE_VIRTIO_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VirtioBusState),
    .abstract = true,
    .class_size = sizeof(VirtioBusClass),
    .class_init = virtio_bus_class_init
};


static const TypeInfo virtio_pci_bus_info = {
    .name          = TYPE_VIRTIO_PCI_BUS,
    .parent        = TYPE_VIRTIO_BUS,
    .instance_size = sizeof(VirtioPCIBusState),
    .class_init    = virtio_pci_bus_class_init,
};



static void bus_class_init(ObjectClass *class, void *data)
{
    BusClass *bc = BUS_CLASS(class);

    class->unparent = bus_unparent;
    bc->get_fw_dev_path = default_bus_get_fw_dev_path;
}

static void virtio_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *bus_class = BUS_CLASS(klass);
    bus_class->get_dev_path = virtio_bus_get_dev_path;
    bus_class->get_fw_dev_path = virtio_bus_get_fw_dev_path;
}


static void virtio_pci_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *bus_class = BUS_CLASS(klass);
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);
    bus_class->max_dev = 1;
    k->notify = virtio_pci_notify;
    k->save_config = virtio_pci_save_config;
    k->load_config = virtio_pci_load_config;
    k->save_queue = virtio_pci_save_queue;
    k->load_queue = virtio_pci_load_queue;
    k->save_extra_state = virtio_pci_save_extra_state;
    k->load_extra_state = virtio_pci_load_extra_state;
    k->has_extra_state = virtio_pci_has_extra_state;
    k->query_guest_notifiers = virtio_pci_query_guest_notifiers;
    k->set_host_notifier = virtio_pci_set_host_notifier;
    k->set_guest_notifiers = virtio_pci_set_guest_notifiers;
    k->vmstate_change = virtio_pci_vmstate_change;
    k->pre_plugged = virtio_pci_pre_plugged;
    k->device_plugged = virtio_pci_device_plugged;
    k->device_unplugged = virtio_pci_device_unplugged;
    k->query_nvectors = virtio_pci_query_nvectors;
}



static void object_instance_init(Object *obj)
{
    object_property_add_str(obj, "type", qdev_get_type, NULL, NULL);
}

static void qbus_initfn(Object *obj)
{
    BusState *bus = BUS(obj);

    QTAILQ_INIT(&bus->children);
    object_property_add_link(obj, QDEV_HOTPLUG_HANDLER_PROPERTY,
                             TYPE_HOTPLUG_HANDLER,
                             (Object **)&bus->hotplug_handler,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             NULL);
    object_property_add_bool(obj, "realized",
                             bus_get_realized, bus_set_realized, NULL);
}






static int virtio_pci_query_nvectors(DeviceState *d)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);

    return proxy->nvectors;
}


/* This is called by virtio-bus just after the device is plugged. */
static void virtio_pci_device_plugged(DeviceState *d, Error **errp)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);
    VirtioBusState *bus = &proxy->bus;
    bool legacy = virtio_pci_legacy(proxy);
    bool modern;
    bool modern_pio = proxy->flags & VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY;
    uint8_t *config;
    uint32_t size;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    /*
     * Virtio capabilities present without
     * VIRTIO_F_VERSION_1 confuses guests
     */
    if (!virtio_has_feature(vdev->host_features, VIRTIO_F_VERSION_1)) {
        virtio_pci_disable_modern(proxy);

        if (!legacy) {
            error_setg(errp, "Device doesn't support modern mode, and legacy"
                             " mode is disabled");
            error_append_hint(errp, "Set disable-legacy to off\n");

            return;
        }
    }

    modern = virtio_pci_modern(proxy);

    config = proxy->pci_dev.config;
    if (proxy->class_code) {
        pci_config_set_class(config, proxy->class_code);
    }

    if (legacy) {
    } else {
        /* pure virtio-1.0 */
        pci_set_word(config + PCI_VENDOR_ID, PCI_VENDOR_ID_REDHAT_QUMRANET);
        pci_set_word(config + PCI_DEVICE_ID, 0x1040 + virtio_bus_get_vdev_id(bus));
        pci_config_set_revision(config, 1);
    }
    config[PCI_INTERRUPT_PIN] = 1;


    if (modern) {
        struct virtio_pci_cap cap = {
            .cap_len = sizeof cap,
        };
        struct virtio_pci_notify_cap notify = {
            .cap.cap_len = sizeof notify,
            .notify_off_multiplier = cpu_to_le32(virtio_pci_queue_mem_mult(proxy)),
        };
        struct virtio_pci_cfg_cap cfg = {
            .cap.cap_len = sizeof cfg,
            .cap.cfg_type = VIRTIO_PCI_CAP_PCI_CFG,
        };
        struct virtio_pci_notify_cap notify_pio = {
            .cap.cap_len = sizeof notify,
            .notify_off_multiplier = cpu_to_le32(0x0),
        };

        struct virtio_pci_cfg_cap *cfg_mask;

        virtio_pci_modern_regions_init(proxy);

		/* 在virtio_pci_realize中会赋值common，irs， device等的offset */
        virtio_pci_modern_mem_region_map(proxy, &proxy->common, &cap);
        virtio_pci_modern_mem_region_map(proxy, &proxy->isr, &cap);
        virtio_pci_modern_mem_region_map(proxy, &proxy->device, &cap);
        virtio_pci_modern_mem_region_map(proxy, &proxy->notify, &notify.cap);


        pci_register_bar(&proxy->pci_dev, proxy->modern_mem_bar,
                         PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_PREFETCH |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                         &proxy->modern_bar);

        proxy->config_cap = virtio_pci_add_mem_cap(proxy, &cfg.cap);
        cfg_mask = (void *)(proxy->pci_dev.wmask + proxy->config_cap);
        pci_set_byte(&cfg_mask->cap.bar, ~0x0);
        pci_set_long((uint8_t *)&cfg_mask->cap.offset, ~0x0);
        pci_set_long((uint8_t *)&cfg_mask->cap.length, ~0x0);
        pci_set_long(cfg_mask->pci_cfg_data, ~0x0);
    }

    if (proxy->nvectors) {
        int err = msix_init_exclusive_bar(&proxy->pci_dev, proxy->nvectors,
                                          proxy->msix_bar);
 
    }

    proxy->pci_dev.config_write = virtio_write_config;
    proxy->pci_dev.config_read = virtio_read_config;


    if (!kvm_has_many_ioeventfds()) {
        proxy->flags &= ~VIRTIO_PCI_FLAG_USE_IOEVENTFD;
    }
}

static inline VirtIODevice *virtio_bus_get_device(VirtioBusState *bus)
{
    BusState *qbus = &bus->parent_obj;
    BusChild *kid = QTAILQ_FIRST(&qbus->children);
    DeviceState *qdev = kid ? kid->child : NULL;

    /* This is used on the data path, the cast is guaranteed
     * to succeed by the qdev machinery.
     */
    return (VirtIODevice *)qdev;
}

static void virtio_pci_modern_mem_region_map(VirtIOPCIProxy *proxy,
                                             VirtIOPCIRegion *region,
                                             struct virtio_pci_cap *cap)
{
    virtio_pci_modern_region_map(proxy, region, cap,
                                 &proxy->modern_bar, proxy->modern_mem_bar);
}


static void virtio_pci_modern_region_map(VirtIOPCIProxy *proxy,
                                         VirtIOPCIRegion *region,
                                         struct virtio_pci_cap *cap,
                                         MemoryRegion *mr,
                                         uint8_t bar)
{
    memory_region_add_subregion(mr, region->offset, &region->mr);

    cap->cfg_type = region->type;
    cap->bar = bar;
    cap->offset = cpu_to_le32(region->offset);
    cap->length = cpu_to_le32(region->size);
    virtio_pci_add_mem_cap(proxy, cap);

}

void memory_region_add_subregion(MemoryRegion *mr,
                                 hwaddr offset,
                                 MemoryRegion *subregion)
{
    subregion->may_overlap = false;
    subregion->priority = 0;
    memory_region_add_subregion_common(mr, offset, subregion);
}

static int virtio_pci_add_mem_cap(VirtIOPCIProxy *proxy,
                                   struct virtio_pci_cap *cap)
{
    PCIDevice *dev = &proxy->pci_dev;
    int offset;

    offset = pci_add_capability(dev, PCI_CAP_ID_VNDR, 0, cap->cap_len);

    memcpy(dev->config + offset + PCI_CAP_FLAGS, &cap->cap_len, cap->cap_len - PCI_CAP_FLAGS);

    return offset;
}

void pci_register_bar(PCIDevice *pci_dev, int region_num,
                      uint8_t type, MemoryRegion *memory)
{
    PCIIORegion *r;
    uint32_t addr;
    uint64_t wmask;
    pcibus_t size = memory_region_size(memory);

    assert(region_num >= 0);
    assert(region_num < PCI_NUM_REGIONS);
    if (size & (size-1)) {
        fprintf(stderr, "ERROR: PCI region size must be pow2 "
                    "type=0x%x, size=0x%"FMT_PCIBUS"\n", type, size);
        exit(1);
    }

    r = &pci_dev->io_regions[region_num];
    r->addr = PCI_BAR_UNMAPPED;
    r->size = size;
    r->type = type;
    r->memory = NULL;

    wmask = ~(size - 1);
    addr = pci_bar(pci_dev, region_num);
    if (region_num == PCI_ROM_SLOT) {
        /* ROM enable bit is writable */
        wmask |= PCI_ROM_ADDRESS_ENABLE;
    }
    pci_set_long(pci_dev->config + addr, type);
    if (!(r->type & PCI_BASE_ADDRESS_SPACE_IO) &&
        r->type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
        pci_set_quad(pci_dev->wmask + addr, wmask);
        pci_set_quad(pci_dev->cmask + addr, ~0ULL);
    } else {
        pci_set_long(pci_dev->wmask + addr, wmask & 0xffffffff);
        pci_set_long(pci_dev->cmask + addr, 0xffffffff);
    }
	/* 设置io region的 address_space 为 address_space_io或address_space_mem  */
    pci_dev->io_regions[region_num].memory = memory;
    pci_dev->io_regions[region_num].address_space
        = type & PCI_BASE_ADDRESS_SPACE_IO
        ? pci_dev->bus->address_space_io
        : pci_dev->bus->address_space_mem;
}


