

/**
 * qemu参数
 * -machine pc-i440fx-rhel7.3.0,accel=kvm,usb=off,dump-guest-core=off
 */

static void pc_init_rhel730(MachineState *machine)
{
    pc_init1(machine, TYPE_I440FX_PCI_HOST_BRIDGE, \
             TYPE_I440FX_PCI_DEVICE);
}

static void pc_machine_rhel730_options(MachineClass *m)
{
    m->family = "pc_piix_Y";
    m->alias = "pc";
    m->desc = "RHEL 7.3.0 PC (i440FX + PIIX, 1996)";
    m->is_default = 1;
    m->default_machine_opts = "firmware=bios-256k.bin";
    m->default_display = "std";
    SET_MACHINE_COMPAT(m, PC_RHEL_COMPAT);
}

DEFINE_PC_MACHINE(rhel730, "pc-i440fx-rhel7.3.0", pc_init_rhel730,
                  pc_machine_rhel730_options);


static void pc_init1(MachineState *machine, const char *host_type, const char *pci_type)
{
    MemoryRegion *pci_memory;

	if (pcmc->pci_enabled) {
        pci_memory = g_new(MemoryRegion, 1);
        memory_region_init(pci_memory, NULL, "pci", UINT64_MAX);
        rom_memory = pci_memory;
    } else {
        pci_memory = NULL;
        rom_memory = system_memory;
    }

	pci_bus = i440fx_init(host_type, /* TYPE_I440FX_PCI_HOST_BRIDGE */
                          pci_type, /* TYPE_I440FX_PCI_DEVICE */
                          &i440fx_state, &piix3_devfn, &isa_bus, gsi,
                          system_memory, system_io, machine->ram_size,
                          pcms->below_4g_mem_size,
                          pcms->above_4g_mem_size,
                          pci_memory, ram_memory);
}


PCIBus *i440fx_init(const char *host_type, const char *pci_type,
                    PCII440FXState **pi440fx_state,
                    int *piix3_devfn,
                    ISABus **isa_bus, qemu_irq *pic,
                    MemoryRegion *address_space_mem,
                    MemoryRegion *address_space_io,
                    ram_addr_t ram_size,
                    ram_addr_t below_4g_mem_size,
                    ram_addr_t above_4g_mem_size,
                    MemoryRegion *pci_address_space,
                    MemoryRegion *ram_memory)
{
	dev = qdev_create(NULL, host_type);
    s = PCI_HOST_BRIDGE(dev);
    b = pci_bus_new(dev, NULL, pci_address_space, address_space_io, 0, TYPE_PCI_BUS);
	qdev_init_nofail(dev);
}


DeviceState *qdev_create(BusState *bus, const char *name)
{
    DeviceState *dev;
    dev = qdev_try_create(bus, name);
    return dev;
}

DeviceState *qdev_try_create(BusState *bus, const char *type)
{
    DeviceState *dev;

	/* 会调用type对应的class的 class_init      和 instance_init
	 * 先调用class_init
	 */
    if (object_class_by_name(type) == NULL) {
        return NULL;
    }
    dev = DEVICE(object_new(type));
  
    if (!bus) {
        bus = sysbus_get_default();
    }

    qdev_set_parent_bus(dev, bus);
    object_unref(OBJECT(dev));
    return dev;
}

void qdev_init_nofail(DeviceState *dev)
{
    Error *err = NULL;
	/* 执行relaize 属性 */
    object_property_set_bool(OBJECT(dev), true, "realized", &err);

}


static const TypeInfo i440fx_pcihost_info = {
    .name          = TYPE_I440FX_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(I440FXState),
    .instance_init = i440fx_pcihost_initfn,
    .class_init    = i440fx_pcihost_class_init,
};

static void i440fx_register_types(void)
{
    type_register_static(&i440fx_info);
    type_register_static(&piix3_pci_type_info);
    type_register_static(&piix3_info);
    type_register_static(&piix3_xen_info);
    type_register_static(&i440fx_pcihost_info);
}

static void i440fx_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    hc->root_bus_path = i440fx_pcihost_root_bus_path;
    dc->realize = i440fx_pcihost_realize;
    dc->fw_name = "pci";
    dc->props = i440fx_props;
    /* Reason: needs to be wired up by pc_init1 */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static void i440fx_pcihost_initfn(Object *obj)
{
    PCIHostState *s = PCI_HOST_BRIDGE(obj);
    I440FXState *d = I440FX_PCI_HOST_BRIDGE(obj);

	/* cmd ioprt 操作函数 */
    memory_region_init_io(&s->conf_mem, obj, &pci_host_conf_le_ops, s,
                          "pci-conf-idx", 4);
	/* data ioport 操作函数 */
    memory_region_init_io(&s->data_mem, obj, &pci_host_data_le_ops, s,
                          "pci-conf-data", 4);

}

static void i440fx_pcihost_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *s = PCI_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

	/* x86 pci枚举用来发送cmd的端口 
	 * 把s->conf_mem添加到system_io中
	 */
    sysbus_add_io(sbd, 0xcf8, &s->conf_mem);
    sysbus_init_ioports(sbd, 0xcf8, 4);
	/* x86 pci枚举用来读取和发送数据的端口 */
    sysbus_add_io(sbd, 0xcfc, &s->data_mem);
    sysbus_init_ioports(sbd, 0xcfc, 4);
}

MemoryRegion *get_system_io(void)
{
    return system_io;
}

void sysbus_add_io(SysBusDevice *dev, hwaddr addr,
                       MemoryRegion *mem)
{
    memory_region_add_subregion(get_system_io(), addr, mem);
}

void sysbus_init_ioports(SysBusDevice *dev, pio_addr_t ioport, pio_addr_t size)
{
    pio_addr_t i;

    for (i = 0; i < size; i++) {
        dev->pio[dev->num_pio++] = ioport++;
    }
}

/* 当写端口0xcf8就会调用到pci_host_conf_le_ops */
const MemoryRegionOps pci_host_conf_le_ops = {
    .read = pci_host_config_read,
    .write = pci_host_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void pci_host_config_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;

    PCI_DPRINTF("%s addr " TARGET_FMT_plx " len %d val %"PRIx64"\n",
                __func__, addr, len, val);
    if (addr != 0 || len != 4) {
        return;
    }
    s->config_reg = val;
}

const MemoryRegionOps pci_host_conf_be_ops = {
    .read = pci_host_config_read,
    .write = pci_host_config_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pci_host_data_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;
    PCI_DPRINTF("write addr " TARGET_FMT_plx " len %d val %x\n",
                addr, len, (unsigned)val);
	/* 需要操作的地址 config_reg */
    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, len);
}


/*
 * PCI address
 * bit 16 - 24: bus number
 * bit  8 - 15: devfun number
 * bit  0 -  7: offset in configuration space of a given pci device
 */

/* the helper function to get a PCIDevice* for a given pci address */
static inline PCIDevice *pci_dev_find_by_addr(PCIBus *bus, uint32_t addr)
{
    uint8_t bus_num = addr >> 16;
    uint8_t devfn = addr >> 8;

    return pci_find_device(bus, bus_num, devfn);
}

PCIDevice *pci_find_device(PCIBus *bus, int bus_num, uint8_t devfn)
{
    bus = pci_find_bus_nr(bus, bus_num);

    if (!bus)
        return NULL;

    return bus->devices[devfn];
}


void pci_data_write(PCIBus *s, uint32_t addr, uint32_t val, int len)
{
    PCIDevice *pci_dev = pci_dev_find_by_addr(s, addr);
    uint32_t config_addr = addr & (PCI_CONFIG_SPACE_SIZE - 1);


    PCI_DPRINTF("%s: %s: addr=%02" PRIx32 " val=%08" PRIx32 " len=%d\n",
                __func__, pci_dev->name, config_addr, val, len);
    pci_host_config_write_common(pci_dev, config_addr, PCI_CONFIG_SPACE_SIZE,
                                 val, len);
}

void pci_host_config_write_common(PCIDevice *pci_dev, uint32_t addr,
                                  uint32_t limit, uint32_t val, uint32_t len)
{
	/* 调用到具体的pci 设备的write函数 */
    pci_dev->config_write(pci_dev, addr, val, MIN(len, limit - addr));
}

/* pci设备的注册 */

static const TypeInfo pci_device_type_info = {
    .name = TYPE_PCI_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .abstract = true,
    .class_size = sizeof(PCIDeviceClass),
    .class_init = pci_device_class_init,
};

static void pci_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    k->realize = pci_qdev_realize;
    k->unrealize = pci_qdev_unrealize;
    k->bus_type = TYPE_PCI_BUS;
    k->props = pci_props;
    pc->realize = pci_default_realize;
}

static void pci_qdev_realize(DeviceState *qdev, Error **errp)
{
    PCIDevice *pci_dev = (PCIDevice *)qdev;
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pci_dev);
    Error *local_err = NULL;
    PCIBus *bus;
    bool is_default_rom;

    /* initialize cap_present for pci_is_express() and pci_config_size() */
    if (pc->is_express) {
        pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    }

    bus = PCI_BUS(qdev_get_parent_bus(qdev));
    pci_dev = do_pci_register_device(pci_dev, bus,
                                     object_get_typename(OBJECT(qdev)),
                                     pci_dev->devfn, errp);


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
    }

    if (devfn < 0) {
        for(devfn = bus->devfn_min ; devfn < ARRAY_SIZE(bus->devices);
            devfn += PCI_FUNC_MAX) {
            if (!bus->devices[devfn])
                goto found;
        }
    found: ;
    } else if (bus->devices[devfn]) {
      
    } else if (dev->hotplugged && pci_get_function_0(pci_dev)) {
 
    }

    pci_dev->devfn = devfn;
    pci_dev->requester_id_cache = pci_req_id_cache_get(pci_dev);

    if (qdev_hotplug) {
        pci_init_bus_master(pci_dev);
    }
    pstrcpy(pci_dev->name, sizeof(pci_dev->name), name);
    pci_dev->irq_state = 0;
    pci_config_alloc(pci_dev);

	/* 设置配置空间 */
    pci_config_set_vendor_id(pci_dev->config, pc->vendor_id);
    pci_config_set_device_id(pci_dev->config, pc->device_id);
    pci_config_set_revision(pci_dev->config, pc->revision);
    pci_config_set_class(pci_dev->config, pc->class_id);

    if (!pc->is_bridge) {
        if (pc->subsystem_vendor_id || pc->subsystem_id) {
            pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID,
                         pc->subsystem_vendor_id);
            pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID,
                         pc->subsystem_id);
        } else {
            pci_set_default_subsystem_id(pci_dev);
        }
    } else {
        /* subsystem_vendor_id/subsystem_id are only for header type 0 */
        assert(!pc->subsystem_vendor_id);
        assert(!pc->subsystem_id);
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

	/* 赋值读写函数 */
    pci_dev->config_read = config_read;
    pci_dev->config_write = config_write;
    bus->devices[devfn] = pci_dev;
    pci_dev->version_id = 2; /* Current pci device vmstate version */
    return pci_dev;
}


/* kvm退出 */
int kvm_cpu_exec(CPUState *cpu)
{

	switch (run->exit_reason) {
    case KVM_EXIT_IO:
	    DPRINTF("handle_io\n");
	    /* Called outside BQL */
	    inc_vcpu_count(cpu->cpu_index, kvm_cpu_exec_count9);
	    kvm_handle_io(run->io.port, attrs,
	                  (uint8_t *)run + run->io.data_offset,
	                  run->io.direction,
	                  run->io.size,
	                  run->io.count);
	    ret = 0;
	    break;
	case KVM_EXIT_MMIO:
	    DPRINTF("handle_mmio\n");
	    /* Called outside BQL */
	    inc_vcpu_count(cpu->cpu_index, kvm_cpu_exec_count10);
	    address_space_rw(&address_space_memory,
	                     run->mmio.phys_addr, attrs,
	                     run->mmio.data,
	                     run->mmio.len,
	                     run->mmio.is_write);
	    ret = 0;
	    break;
	}
}

static void kvm_handle_io(uint16_t port, MemTxAttrs attrs, void *data, int direction,
                          int size, uint32_t count)
{
    int i;
    uint8_t *ptr = data;

    for (i = 0; i < count; i++) {
        address_space_rw(&address_space_io, port, attrs,
                         ptr, size,
                         direction == KVM_EXIT_IO_OUT);
        ptr += size;
    }
}

MemTxResult address_space_rw(AddressSpace *as, hwaddr addr, MemTxAttrs attrs,
                             uint8_t *buf, int len, bool is_write)
{
    if (is_write) {
        return address_space_write(as, addr, attrs, (uint8_t *)buf, len);
    } else {
        return address_space_read(as, addr, attrs, (uint8_t *)buf, len);
    }
}

MemTxResult address_space_write(AddressSpace *as, hwaddr addr, MemTxAttrs attrs,
                                const uint8_t *buf, int len)
{
    hwaddr l;
    hwaddr addr1;
    MemoryRegion *mr;
    MemTxResult result = MEMTX_OK;

    if (len > 0) {
        rcu_read_lock();
        l = len;
        mr = address_space_translate(as, addr, &addr1, &l, true);
        result = address_space_write_continue(as, addr, attrs, buf, len,
                                              addr1, l, mr);
        rcu_read_unlock();
    }

    return result;
}

/* Called from RCU critical section */
MemoryRegion *address_space_translate(AddressSpace *as, hwaddr addr,
                                      hwaddr *xlat, hwaddr *plen,
                                      bool is_write)
{
    IOMMUTLBEntry iotlb;
    MemoryRegionSection *section;
    MemoryRegion *mr;

    for (;;) {
        AddressSpaceDispatch *d = atomic_rcu_read(&as->dispatch);
        section = address_space_translate_internal(d, addr, &addr, plen, true);
        mr = section->mr;
    }

    *xlat = addr;
    return mr;
}

* Called within RCU critical section.  */
static MemTxResult address_space_write_continue(AddressSpace *as, hwaddr addr,
                                                MemTxAttrs attrs,
                                                const uint8_t *buf,
                                                int len, hwaddr addr1,
                                                hwaddr l, MemoryRegion *mr)
{
    uint8_t *ptr;
    uint64_t val;
    MemTxResult result = MEMTX_OK;
    bool release_lock = false;

    for (;;) {
        if (!memory_access_is_direct(mr, true)) {
            release_lock |= prepare_mmio_access(mr);
            l = memory_access_size(mr, l, addr1);
            /* XXX: could force current_cpu to NULL to avoid
               potential bugs */
            switch (l) {
            case 8:
                /* 64 bit write access */
                val = ldq_p(buf);
                result |= memory_region_dispatch_write(mr, addr1, val, 8,
                                                       attrs);
                break;
            case 4:
                /* 32 bit write access */
                val = ldl_p(buf);
                result |= memory_region_dispatch_write(mr, addr1, val, 4,
                                                       attrs);
                break;
            case 2:
                /* 16 bit write access */
                val = lduw_p(buf);
                result |= memory_region_dispatch_write(mr, addr1, val, 2,
                                                       attrs);
                break;
            case 1:
                /* 8 bit write access */
                val = ldub_p(buf);
                result |= memory_region_dispatch_write(mr, addr1, val, 1,
                                                       attrs);
                break;
            default:
                abort();
            }
        } else {
            addr1 += memory_region_get_ram_addr(mr);
            /* RAM case */
            ptr = qemu_get_ram_ptr(mr->ram_block, addr1);
            memcpy(ptr, buf, l);
            invalidate_and_set_dirty(mr, addr1, l);
        }

        if (release_lock) {
            qemu_mutex_unlock_iothread();
            release_lock = false;
        }

        len -= l;
        buf += l;
        addr += l;

        if (!len) {
            break;
        }

        l = len;
        mr = address_space_translate(as, addr, &addr1, &l, true);
    }

    return result;
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

static MemTxResult memory_region_write_accessor(MemoryRegion *mr,
                                                hwaddr addr,
                                                uint64_t *value,
                                                unsigned size,
                                                unsigned shift,
                                                uint64_t mask,
                                                MemTxAttrs attrs)
{
    uint64_t tmp;

    tmp = (*value >> shift) & mask;
    /* 调用mr对应的write */
    mr->ops->write(mr->opaque, addr, tmp, size);
    return MEMTX_OK;
}

