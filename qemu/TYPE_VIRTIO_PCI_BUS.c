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

        if (modern_pio) {
            memory_region_init(&proxy->io_bar, OBJECT(proxy),
                               "virtio-pci-io", 0x4);

            pci_register_bar(&proxy->pci_dev, proxy->modern_io_bar,
                             PCI_BASE_ADDRESS_SPACE_IO, &proxy->io_bar);

            virtio_pci_modern_io_region_map(proxy, &proxy->notify_pio,
                                            &notify_pio.cap);
        }

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
        if (err) {
            /* Notice when a system that supports MSIx can't initialize it.  */
            if (err != -ENOTSUP) {
                error_report("unable to init msix vectors to %" PRIu32,
                             proxy->nvectors);
            }
            proxy->nvectors = 0;
        }
    }

    proxy->pci_dev.config_write = virtio_write_config;
    proxy->pci_dev.config_read = virtio_read_config;


    if (!kvm_has_many_ioeventfds()) {
        proxy->flags &= ~VIRTIO_PCI_FLAG_USE_IOEVENTFD;
    }
}

static void virtio_pci_modern_regions_init(VirtIOPCIProxy *proxy)
{
    static const MemoryRegionOps common_ops = {
        .read = virtio_pci_common_read,
        .write = virtio_pci_common_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps isr_ops = {
        .read = virtio_pci_isr_read,
        .write = virtio_pci_isr_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps device_ops = {
        .read = virtio_pci_device_read,
        .write = virtio_pci_device_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps notify_ops = {
        .read = virtio_pci_notify_read,
        .write = virtio_pci_notify_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps notify_pio_ops = {
        .read = virtio_pci_notify_read,
        .write = virtio_pci_notify_write_pio,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };


    memory_region_init_io(&proxy->common.mr, OBJECT(proxy),
                          &common_ops,
                          proxy,
                          "virtio-pci-common",
                          proxy->common.size);

    memory_region_init_io(&proxy->isr.mr, OBJECT(proxy),
                          &isr_ops,
                          proxy,
                          "virtio-pci-isr",
                          proxy->isr.size);

    memory_region_init_io(&proxy->device.mr, OBJECT(proxy),
                          &device_ops,
                          virtio_bus_get_device(&proxy->bus),
                          "virtio-pci-device",
                          proxy->device.size);

    memory_region_init_io(&proxy->notify.mr, OBJECT(proxy),
                          &notify_ops,
                          virtio_bus_get_device(&proxy->bus),
                          "virtio-pci-notify",
                          proxy->notify.size);

    memory_region_init_io(&proxy->notify_pio.mr, OBJECT(proxy),
                          &notify_pio_ops,
                          virtio_bus_get_device(&proxy->bus),
                          "virtio-pci-notify-pio",
                          proxy->notify.size);
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

static void memory_region_add_subregion_common(MemoryRegion *mr,
                                               hwaddr offset,
                                               MemoryRegion *subregion)
{
    assert(!subregion->container);
    subregion->container = mr;
    subregion->addr = offset;
    memory_region_update_container_subregions(subregion);
}

static void memory_region_update_container_subregions(MemoryRegion *subregion)
{
    hwaddr offset = subregion->addr;
    MemoryRegion *mr = subregion->container;
    MemoryRegion *other;

    memory_region_transaction_begin();

    memory_region_ref(subregion);
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->may_overlap || other->may_overlap) {
            continue;
        }
        if (int128_ge(int128_make64(offset),
                      int128_add(int128_make64(other->addr), other->size))
            || int128_le(int128_add(int128_make64(offset), subregion->size),
                         int128_make64(other->addr))) {
            continue;
        }
#if 0
        printf("warning: subregion collision %llx/%llx (%s) "
               "vs %llx/%llx (%s)\n",
               (unsigned long long)offset,
               (unsigned long long)int128_get64(subregion->size),
               subregion->name,
               (unsigned long long)other->addr,
               (unsigned long long)int128_get64(other->size),
               other->name);
#endif
    }
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->priority >= other->priority) {
            QTAILQ_INSERT_BEFORE(other, subregion, subregions_link);
            goto done;
        }
    }
    QTAILQ_INSERT_TAIL(&mr->subregions, subregion, subregions_link);
done:
    memory_region_update_pending |= mr->enabled && subregion->enabled;
    memory_region_transaction_commit();
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
    pci_dev->io_regions[region_num].memory = memory;
    pci_dev->io_regions[region_num].address_space
        = type & PCI_BASE_ADDRESS_SPACE_IO
        ? pci_dev->bus->address_space_io
        : pci_dev->bus->address_space_mem;
}


int pci_bar(PCIDevice *d, int reg)
{
    uint8_t type;

    if (reg != PCI_ROM_SLOT)
        return PCI_BASE_ADDRESS_0 + reg * 4;

    type = d->config[PCI_HEADER_TYPE] & ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    return type == PCI_HEADER_TYPE_BRIDGE ? PCI_ROM_ADDRESS1 : PCI_ROM_ADDRESS;
}

tatic void virtio_write_config(PCIDevice *pci_dev, uint32_t address,
                                uint32_t val, int len)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    struct virtio_pci_cfg_cap *cfg;

    pci_default_write_config(pci_dev, address, val, len);

    if (range_covers_byte(address, len, PCI_COMMAND) &&
        !(pci_dev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        virtio_pci_stop_ioeventfd(proxy);
        virtio_set_status(vdev, vdev->status & ~VIRTIO_CONFIG_S_DRIVER_OK);
    }

    if (proxy->config_cap &&
        ranges_overlap(address, len, proxy->config_cap + offsetof(struct virtio_pci_cfg_cap,
                                                                  pci_cfg_data),
                       sizeof cfg->pci_cfg_data)) {
        uint32_t off;
        uint32_t len;

        cfg = (void *)(proxy->pci_dev.config + proxy->config_cap);
        off = le32_to_cpu(cfg->cap.offset);
        len = le32_to_cpu(cfg->cap.length);

        if (len == 1 || len == 2 || len == 4) {
            assert(len <= sizeof cfg->pci_cfg_data);
            virtio_address_space_write(&proxy->modern_as, off,
                                       cfg->pci_cfg_data, len);
        }
    }
}

/* Below are generic functions to do memcpy from/to an address space,
 * without byteswaps, with input validation.
 *
 * As regular address_space_* APIs all do some kind of byteswap at least for
 * some host/target combinations, we are forced to explicitly convert to a
 * known-endianness integer value.
 * It doesn't really matter which endian format to go through, so the code
 * below selects the endian that causes the least amount of work on the given
 * host.
 *
 * Note: host pointer must be aligned.
 */
static
void virtio_address_space_write(AddressSpace *as, hwaddr addr,
                                const uint8_t *buf, int len)
{
    uint32_t val;

    /* address_space_* APIs assume an aligned address.
     * As address is under guest control, handle illegal values.
     */
    addr &= ~(len - 1);

    /* Make sure caller aligned buf properly */
    assert(!(((uintptr_t)buf) & (len - 1)));

	/* 下面的函数最终都会调用到 address_space_translate             在virtio的address_space中进行地址转换
	 * 然后调用对应的写函数
	 */
    switch (len) {
    case 1:
        val = pci_get_byte(buf);
        address_space_stb(as, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 2:
        val = pci_get_word(buf);
        address_space_stw_le(as, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 4:
        val = pci_get_long(buf);
        address_space_stl_le(as, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    default:
        /* As length is under guest control, handle illegal values. */
        break;
    }
}

void address_space_stl_le(AddressSpace *as, hwaddr addr, uint32_t val,
                       MemTxAttrs attrs, MemTxResult *result)
{
    address_space_stl_internal(as, addr, val, attrs, result,
                               DEVICE_LITTLE_ENDIAN);
}

/* warning: addr must be aligned */
static inline void address_space_stl_internal(AddressSpace *as,
                                              hwaddr addr, uint32_t val,
                                              MemTxAttrs attrs,
                                              MemTxResult *result,
                                              enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;
    MemTxResult r;
    bool release_lock = false;

    rcu_read_lock();
	/* 地址转换找到对应的mr */
    mr = address_space_translate(as, addr, &addr1, &l,
                                 true);
    if (l < 4 || !memory_access_is_direct(mr, true)) {
        release_lock |= prepare_mmio_access(mr);

#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap32(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap32(val);
        }
#endif
        r = memory_region_dispatch_write(mr, addr1, val, 4, attrs);
    } else {
        /* RAM case */
        addr1 += memory_region_get_ram_addr(mr) & TARGET_PAGE_MASK;
        ptr = qemu_get_ram_ptr(mr->ram_block, addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stl_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stl_be_p(ptr, val);
            break;
        default:
            stl_p(ptr, val);
            break;
        }
        invalidate_and_set_dirty(mr, addr1, 4);
        r = MEMTX_OK;
    }
    if (result) {
        *result = r;
    }
    if (release_lock) {
        qemu_mutex_unlock_iothread();
    }
    rcu_read_unlock();
}

MemTxResult memory_region_dispatch_write(MemoryRegion *mr,
                                         hwaddr addr,
                                         uint64_t data,
                                         unsigned size,
                                         MemTxAttrs attrs)
{
    if (!memory_region_access_valid(mr, addr, size, true)) {
        unassigned_mem_write(mr, addr, data, size);
        return MEMTX_DECODE_ERROR;
    }

    adjust_endianness(mr, &data, size);

    if ((!kvm_eventfds_enabled()) &&
        memory_region_dispatch_write_eventfds(mr, addr, data, size, attrs)) {
        return MEMTX_OK;
    }
	/* 最终会调用mr的       ops->write  */
    if (mr->ops->write) {
        return access_with_adjusted_size(addr, &data, size,
                                         mr->ops->impl.min_access_size,
                                         mr->ops->impl.max_access_size,
                                         memory_region_write_accessor, mr,
                                         attrs);
    } else if (mr->ops->write_with_attrs) {
        return
            access_with_adjusted_size(addr, &data, size,
                                      mr->ops->impl.min_access_size,
                                      mr->ops->impl.max_access_size,
                                      memory_region_write_with_attrs_accessor,
                                      mr, attrs);
    } else {
        return access_with_adjusted_size(addr, &data, size, 1, 4,
                                         memory_region_oldmmio_write_accessor,
                                         mr, attrs);
    }
}

/* Macro versions of offsets for the Old Timers! */
#define VIRTIO_PCI_CAP_VNDR		0
#define VIRTIO_PCI_CAP_NEXT		1
#define VIRTIO_PCI_CAP_LEN		2
#define VIRTIO_PCI_CAP_CFG_TYPE		3
#define VIRTIO_PCI_CAP_BAR		4
#define VIRTIO_PCI_CAP_OFFSET		8
#define VIRTIO_PCI_CAP_LENGTH		12

#define VIRTIO_PCI_NOTIFY_CAP_MULT	16

#define VIRTIO_PCI_COMMON_DFSELECT	0
#define VIRTIO_PCI_COMMON_DF		4
#define VIRTIO_PCI_COMMON_GFSELECT	8
#define VIRTIO_PCI_COMMON_GF		12
#define VIRTIO_PCI_COMMON_MSIX		16
#define VIRTIO_PCI_COMMON_NUMQ		18
#define VIRTIO_PCI_COMMON_STATUS	20
#define VIRTIO_PCI_COMMON_CFGGENERATION	21
#define VIRTIO_PCI_COMMON_Q_SELECT	22
#define VIRTIO_PCI_COMMON_Q_SIZE	24
#define VIRTIO_PCI_COMMON_Q_MSIX	26
#define VIRTIO_PCI_COMMON_Q_ENABLE	28
#define VIRTIO_PCI_COMMON_Q_NOFF	30
#define VIRTIO_PCI_COMMON_Q_DESCLO	32
#define VIRTIO_PCI_COMMON_Q_DESCHI	36
#define VIRTIO_PCI_COMMON_Q_AVAILLO	40
#define VIRTIO_PCI_COMMON_Q_AVAILHI	44
#define VIRTIO_PCI_COMMON_Q_USEDLO	48
#define VIRTIO_PCI_COMMON_Q_USEDHI	52

static void virtio_pci_common_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    switch (addr) {
    case VIRTIO_PCI_COMMON_DFSELECT:
        proxy->dfselect = val;
        break;
    case VIRTIO_PCI_COMMON_GFSELECT:
        proxy->gfselect = val;
        break;
    case VIRTIO_PCI_COMMON_GF:
        if (proxy->gfselect < ARRAY_SIZE(proxy->guest_features)) {
            proxy->guest_features[proxy->gfselect] = val;
            virtio_set_features(vdev,
                                (((uint64_t)proxy->guest_features[1]) << 32) |
                                proxy->guest_features[0]);
        }
        break;
    case VIRTIO_PCI_COMMON_MSIX:
        msix_vector_unuse(&proxy->pci_dev, vdev->config_vector);
        /* Make it possible for guest to discover an error took place. */
        if (msix_vector_use(&proxy->pci_dev, val) < 0) {
            val = VIRTIO_NO_VECTOR;
        }
        vdev->config_vector = val;
        break;
    case VIRTIO_PCI_COMMON_STATUS:
        if (!(val & VIRTIO_CONFIG_S_DRIVER_OK)) {
            virtio_pci_stop_ioeventfd(proxy);
        }
		/* Linux驱动完成之后回写VIRTIO_CONFIG_S_DRIVER_OK */
        virtio_set_status(vdev, val & 0xFF);

        if (val & VIRTIO_CONFIG_S_DRIVER_OK) {
            virtio_pci_start_ioeventfd(proxy);
        }

        if (vdev->status == 0) {
            virtio_pci_reset(DEVICE(proxy));
        }

        break;
    case VIRTIO_PCI_COMMON_Q_SELECT:
        if (val < VIRTIO_QUEUE_MAX) {
            vdev->queue_sel = val;
        }
        break;
    case VIRTIO_PCI_COMMON_Q_SIZE:
        proxy->vqs[vdev->queue_sel].num = val;
        break;
    case VIRTIO_PCI_COMMON_Q_MSIX:
        msix_vector_unuse(&proxy->pci_dev,
                          virtio_queue_vector(vdev, vdev->queue_sel));
        /* Make it possible for guest to discover an error took place. */
        if (msix_vector_use(&proxy->pci_dev, val) < 0) {
            val = VIRTIO_NO_VECTOR;
        }
        virtio_queue_set_vector(vdev, vdev->queue_sel, val);
        break;
    case VIRTIO_PCI_COMMON_Q_ENABLE:
		/* guest kernel中申请的vring的地址等，guest          kernel的物理地址就是
		 * qemu进程的虚拟地址
		 */
        /* TODO: need a way to put num back on reset. */
        virtio_queue_set_num(vdev, vdev->queue_sel,
                             proxy->vqs[vdev->queue_sel].num);
        virtio_queue_set_rings(vdev, vdev->queue_sel,
                       ((uint64_t)proxy->vqs[vdev->queue_sel].desc[1]) << 32 |
                       proxy->vqs[vdev->queue_sel].desc[0],
                       ((uint64_t)proxy->vqs[vdev->queue_sel].avail[1]) << 32 |
                       proxy->vqs[vdev->queue_sel].avail[0],
                       ((uint64_t)proxy->vqs[vdev->queue_sel].used[1]) << 32 |
                       proxy->vqs[vdev->queue_sel].used[0]);
        proxy->vqs[vdev->queue_sel].enabled = 1;
        break;
    case VIRTIO_PCI_COMMON_Q_DESCLO:
        proxy->vqs[vdev->queue_sel].desc[0] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_DESCHI:
        proxy->vqs[vdev->queue_sel].desc[1] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_AVAILLO:
        proxy->vqs[vdev->queue_sel].avail[0] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_AVAILHI:
        proxy->vqs[vdev->queue_sel].avail[1] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_USEDLO:
        proxy->vqs[vdev->queue_sel].used[0] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_USEDHI:
        proxy->vqs[vdev->queue_sel].used[1] = val;
        break;
    default:
        break;
    }
}

void virtio_queue_set_rings(VirtIODevice *vdev, int n, hwaddr desc,
                            hwaddr avail, hwaddr used)
{
    vdev->vq[n].vring.desc = desc;
    vdev->vq[n].vring.avail = avail;
    vdev->vq[n].vring.used = used;
}


int virtio_set_status(VirtIODevice *vdev, uint8_t val)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    trace_virtio_set_status(vdev, val);

    if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        if (!(vdev->status & VIRTIO_CONFIG_S_FEATURES_OK) &&
            val & VIRTIO_CONFIG_S_FEATURES_OK) {
            int ret = virtio_validate_features(vdev);

            if (ret) {
                return ret;
            }
        }
    }
    if (k->set_status) {
		/* 调用 vhost_user_blk_set_status */
        k->set_status(vdev, val);
    }
    vdev->status = val;
    return 0
}

static void vhost_user_blk_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;
    int ret;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (s->dev.started == should_start) {
        return;
    }

    if (should_start) {
        s->should_start = true;
        ret = vhost_user_blk_start(vdev);
        if (ret < 0) {
            error_report("vhost-user-blk: vhost start failed: %s",
                         strerror(-ret));
            qemu_chr_disconnect(s->chardev);
        }
    } else {
        vhost_user_blk_stop(vdev);
        s->should_start = false;
    }

}

static int vhost_user_blk_start(VirtIODevice *vdev)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i, ret;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&s->dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    s->dev.acked_features = vdev->guest_features;

    if (!s->inflight->addr) {
        ret = vhost_dev_get_inflight(&s->dev, s->queue_size, s->inflight);
        if (ret < 0) {
            error_report("Error get inflight: %d", -ret);
            goto err_guest_notifiers;
        }
    }

    ret = vhost_dev_set_inflight(&s->dev, s->inflight);
    if (ret < 0) {
        error_report("Error set inflight: %d", -ret);
        goto err_guest_notifiers;
    }

    ret = vhost_dev_start(&s->dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }

    ret = vhost_setup_slave_channel(&s->dev);
    if (ret < 0) {
        error_report("Error setting vhost slave channel: %d", -ret);
        goto err_guest_notifiers;
    }

    /* guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < s->dev.nvqs; i++) {
        vhost_virtqueue_mask(&s->dev, vdev, i, false);
    }

    return ret;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&s->dev, vdev);

    return ret;
}

/* Host notifiers must be enabled at this point. */
int vhost_dev_start(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    int i, r;

    hdev->started = true;
    hdev->vdev = vdev;

    r = vhost_dev_set_features(hdev, hdev->log_enabled);

    r = hdev->vhost_ops->vhost_set_mem_table(hdev, hdev->mem);

    for (i = 0; i < hdev->nvqs; ++i) {
        r = vhost_virtqueue_start(hdev,
                                  vdev,
                                  hdev->vqs + i,
                                  hdev->vq_index + i);

    }

    if (hdev->log_enabled) {
        uint64_t log_base;

        hdev->log_size = vhost_get_log_size(hdev);
        hdev->log = vhost_log_get(hdev->log_size,
                                  vhost_dev_log_is_shared(hdev));
        log_base = (uintptr_t)hdev->log->log;
        r = hdev->vhost_ops->vhost_set_log_base(hdev,
                                                hdev->log_size ? log_base : 0,
                                                hdev->log);
    }

    return 0;
}

static int vhost_virtqueue_start(struct vhost_dev *dev,
                                struct VirtIODevice *vdev,
                                struct vhost_virtqueue *vq,
                                unsigned idx)
{
    hwaddr s, l, a;
    int r;
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, idx);
    struct vhost_vring_file file = {
        .index = vhost_vq_index
    };
    struct vhost_vring_state state = {
        .index = vhost_vq_index
    };
    struct VirtQueue *vvq = virtio_get_queue(vdev, idx);

    a = virtio_queue_get_desc_addr(vdev, idx);
    if (a == 0) {
        /* Queue might not be ready for start */
        return 0;
    }

    vq->num = state.num = virtio_queue_get_num(vdev, idx);
    r = dev->vhost_ops->vhost_set_vring_num(dev, &state);


    state.num = virtio_queue_get_last_avail_idx(vdev, idx);
    r = dev->vhost_ops->vhost_set_vring_base(dev, &state);

    if (vhost_needs_vring_endian(vdev)) {
        r = vhost_virtqueue_set_vring_endian_legacy(dev,
                                                    virtio_is_big_endian(vdev),
                                                    vhost_vq_index);
    }

    s = l = virtio_queue_get_desc_size(vdev, idx);
    a = virtio_queue_get_desc_addr(vdev, idx);
	/* 将guset kernel申请的内存的物理地址转换为qemu中对应的虚拟地址 */
    vq->desc = cpu_physical_memory_map(a, &l, 0);
    s = l = virtio_queue_get_avail_size(vdev, idx);

    a = virtio_queue_get_avail_addr(vdev, idx);
    vq->avail = cpu_physical_memory_map(a, &l, 0);
  
    vq->used_size = s = l = virtio_queue_get_used_size(vdev, idx);
    vq->used_phys = a = virtio_queue_get_used_addr(vdev, idx);
    vq->used = cpu_physical_memory_map(a, &l, 1);
  
    vq->ring_size = s = l = virtio_queue_get_ring_size(vdev, idx);
    vq->ring_phys = a = virtio_queue_get_ring_addr(vdev, idx);
    vq->ring = cpu_physical_memory_map(a, &l, 1);
  
    r = vhost_virtqueue_set_addr(dev, vq, vhost_vq_index, dev->log_enabled);
  
    file.fd = event_notifier_get_fd(virtio_queue_get_host_notifier(vvq));
    r = dev->vhost_ops->vhost_set_vring_kick(dev, &file);
 
    /* Clear and discard previous events if any. */
    event_notifier_test_and_clear(&vq->masked_notifier);

    /* Init vring in unmasked state, unless guest_notifier_mask
     * will do it later.
     */
    if (!vdev->use_guest_notifier_mask) {
        /* TODO: check and handle errors. */
        vhost_virtqueue_mask(dev, vdev, idx, false);
    }

    return 0;


}

hwaddr virtio_queue_get_desc_addr(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.desc;
}

void *cpu_physical_memory_map(hwaddr addr,
                              hwaddr *plen,
                              int is_write)
{
    return address_space_map(&address_space_memory, addr, plen, is_write);
}

void *address_space_map(AddressSpace *as,
                        hwaddr addr,
                        hwaddr *plen,
                        bool is_write)
{
    hwaddr len = *plen;
    hwaddr done = 0;
    hwaddr l, xlat, base;
    MemoryRegion *mr, *this_mr;
    ram_addr_t raddr;
    void *ptr;

    if (len == 0) {
        return NULL;
    }

    l = len;
    rcu_read_lock();
    mr = address_space_translate(as, addr, &xlat, &l, is_write);

    if (!memory_access_is_direct(mr, is_write)) {
        if (atomic_xchg(&bounce.in_use, true)) {
            rcu_read_unlock();
            return NULL;
        }
        /* Avoid unbounded allocations */
        l = MIN(l, TARGET_PAGE_SIZE);
        bounce.buffer = qemu_memalign(TARGET_PAGE_SIZE, l);
        bounce.addr = addr;
        bounce.len = l;

        memory_region_ref(mr);
        bounce.mr = mr;
        if (!is_write) {
            address_space_read(as, addr, MEMTXATTRS_UNSPECIFIED,
                               bounce.buffer, l);
        }

        rcu_read_unlock();
        *plen = l;
        return bounce.buffer;
    }

    base = xlat;
    raddr = memory_region_get_ram_addr(mr);

    for (;;) {
        len -= l;
        addr += l;
        done += l;
        if (len == 0) {
            break;
        }

        l = len;
        this_mr = address_space_translate(as, addr, &xlat, &l, is_write);
        if (this_mr != mr || xlat != base + done) {
            break;
        }
    }

    memory_region_ref(mr);
    *plen = done;
    ptr = qemu_ram_ptr_length(mr->ram_block, raddr + base, plen);
    rcu_read_unlock();

    return ptr;
}

/* Return a host pointer to guest's ram. Similar to qemu_get_ram_ptr
 * but takes a size argument.
 *
 * Called within RCU critical section.
 */
static void *qemu_ram_ptr_length(RAMBlock *ram_block, ram_addr_t addr,
                                 hwaddr *size)
{
    RAMBlock *block = ram_block;
    ram_addr_t offset_inside_block;
    if (*size == 0) {
        return NULL;
    }

    if (block == NULL) {
        block = qemu_get_ram_block(addr);
    }
    offset_inside_block = addr - block->offset;
    *size = MIN(*size, block->max_length - offset_inside_block);

 

    return ramblock_ptr(block, offset_inside_block);
}

static inline void *ramblock_ptr(RAMBlock *block, ram_addr_t offset)
{
	/* host为gust的物理地址其地址对应在qemu中的虚拟地址
	 * 在添加物理地址的时候RAMBlock的host是值是mmap大页的地址
	 */
    return (char *)block->host + offset;
}

static int vhost_virtqueue_set_addr(struct vhost_dev *dev,
                                    struct vhost_virtqueue *vq,
                                    unsigned idx, bool enable_log)
{
    struct vhost_vring_addr addr = {
        .index = idx,
        .desc_user_addr = (uint64_t)(unsigned long)vq->desc,
        .avail_user_addr = (uint64_t)(unsigned long)vq->avail,
        .used_user_addr = (uint64_t)(unsigned long)vq->used,
        .log_guest_addr = vq->used_phys,
        .flags = enable_log ? (1 << VHOST_VRING_F_LOG) : 0,
    };
    int r = dev->vhost_ops->vhost_set_vring_addr(dev, &addr);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_set_vring_addr failed");
        return -errno;
    }
    return 0;
}

