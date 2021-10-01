# qemu 对PCI设备配置空间的模拟

## x86 访问访问PCI 配置空间的方式

x86 访问pci配置空间是通过端口进行的

```
#define PORT_PCI_CMD           0x0cf8
#define PORT_PCI_DATA          0x0cfc
```

0x0cf8是命令端口地址

0x0cfc是数据端口地址，通过该端口可以进行数据读写。

## pci_config_writew

这个是Linux系统的一个写config的函数

```c
void pci_config_writew(u16 bdf, u32 addr, u16 val)
{
    /* PORT_PCI_CMD 选择pci设备，通过bdf选择 
     * bit[31]:是否使能pci配置功能，1表示使能
     * bit[30:24]:保留位
     * bit[23:16]:pci总线号
     * bit[15:11]:选定总线的设备号
     * bit[10:8]:功能号
     * bit[7:2]:表示所选总线，设备号，功能号对应的pci设备的寄存器地址
     * bit[1:0]:保留
     */
    outl(0x8000 0000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    
    outw(val, PORT_PCI_DATA + (addr & 2));
}
```

PORT_PCI_CMD用来选定bdf和配置空间的地址，

PORT_PCI_DATA用来写入数据

# qemu中的模拟

在qemu中搜索0x0cf8可以看到在`i440fx_pcihost_realize`函数中有使用

```c
sysbus_add_io(sbd, 0xcf8, &s->conf_mem);
sysbus_init_ioports(sbd, 0xcf8, 4);
```

注册0xcf8端口，大小为4个。

下面学习下注册的过程

## i440fx_pcihost_info

```c
#define TYPE_I440FX_PCI_HOST_BRIDGE "i440FX-pcihost"
static const TypeInfo i440fx_pcihost_info = {
    .name          = TYPE_I440FX_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(I440FXState),
    .instance_init = i440fx_pcihost_initfn,
    .class_init    = i440fx_pcihost_class_init,
};
```

先看下class_init函数

## i440fx_pcihost_class_init

```c
static void i440fx_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    hc->root_bus_path = i440fx_pcihost_root_bus_path;
    dc->realize = i440fx_pcihost_realize;/* 给realize函数赋值 */
    dc->fw_name = "pci";
    dc->props = i440fx_props;
    /* Reason: needs to be wired up by pc_init1 */
    dc->cannot_instantiate_with_device_add_yet = true;
}
```

instance_init函数

## i440fx_pcihost_initfn

```c
static void i440fx_pcihost_initfn(Object *obj)
{
    PCIHostState *s = PCI_HOST_BRIDGE(obj);
    I440FXState *d = I440FX_PCI_HOST_BRIDGE(obj);

	/* 注册了两个io mr，看着是和pci的端口有关 */
    memory_region_init_io(&s->conf_mem, obj, &pci_host_conf_le_ops, s, "pci-conf-idx", 4);
    memory_region_init_io(&s->data_mem, obj, &pci_host_data_le_ops, s, "pci-conf-data", 4);

    d->pci_info.w32.end = IO_APIC_DEFAULT_ADDRESS;
}
```

## TYPE_I440FX_PCI_HOST_BRIDGE初始化

## pc_init_rhel730

```c
#define DEFINE_PC_MACHINE(suffix, namestr, initfn, optsfn) \
    static void pc_machine_##suffix##_class_init(ObjectClass *oc, void *data) \
    { \
        MachineClass *mc = MACHINE_CLASS(oc); \
        optsfn(mc); \
        mc->name = namestr; \
        mc->init = initfn; \
    } \
    static const TypeInfo pc_machine_type_##suffix = { \
        .name       = namestr TYPE_MACHINE_SUFFIX, \
        .parent     = TYPE_PC_MACHINE, \
        .class_init = pc_machine_##suffix##_class_init, \
    }; \
    static void pc_machine_init_##suffix(void) \
    { \
        type_register(&pc_machine_type_##suffix); \
    } \
    type_init(pc_machine_init_##suffix)

DEFINE_PC_MACHINE(rhel730, "pc-i440fx-rhel7.3.0", pc_init_rhel730,
                  pc_machine_rhel730_options);
```

pc_init_rhel730在class_init中会被赋值。

main-->machine_class->init(current_machine);会调用pc_init_rhel730函数。qemu的参数为

```c
-machine pc-i440fx-rhel7.3.0,accel=kvm,usb=off,dump-guest-core=off
```

所以会调用到`pc_init_rhel730`函数

### pc_init_rhel730

```c
static void pc_init_rhel730(MachineState *machine)
{
    pc_init1(machine, TYPE_I440FX_PCI_HOST_BRIDGE, \
             TYPE_I440FX_PCI_DEVICE);
}
```

pc_init1-->i440fx_init-->i440fx_init-->qdev_create

### qdev_create

```c
/* name就说TYPE_I440FX_PCI_HOST_BRIDGE */
DeviceState *qdev_create(BusState *bus, const char *name)
{
    DeviceState *dev;

    dev = qdev_try_create(bus, name);

    return dev;
}
```

### qdev_try_create

```c
DeviceState *qdev_try_create(BusState *bus, const char *type)
{
    DeviceState *dev;
	/* 初始化 TYPE_I440FX_PCI_HOST_BRIDGE
     *  调用class_init, instance_init
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
```

## realize调用

i440fx_init-->qdev_init_nofail

### qdev_init_nofail

```c++
/* dev为qdev_try_create返回的dev */
void qdev_init_nofail(DeviceState *dev)
{
    Error *err = NULL;
	/* 设置 realized 属性，最终调用到 i440fx_pcihost_realize */
    object_property_set_bool(OBJECT(dev), true, "realized", &err);
}
```

### i440fx_pcihost_realize

```c
static void i440fx_pcihost_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *s = PCI_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

	/* 注册io地址 */
    sysbus_add_io(sbd, 0xcf8, &s->conf_mem);
    sysbus_init_ioports(sbd, 0xcf8, 4);

    sysbus_add_io(sbd, 0xcfc, &s->data_mem);
    sysbus_init_ioports(sbd, 0xcfc, 4);
}
```

### sysbus_add_io

```c
void sysbus_add_io(SysBusDevice *dev, hwaddr addr, MemoryRegion *mem)
{
    /* get_system_io直接返回 system_io */
    memory_region_add_subregion(get_system_io(), addr, mem);
}
```

### sysbus_init_ioports

```c
void sysbus_init_ioports(SysBusDevice *dev, pio_addr_t ioport, pio_addr_t size)
{
    pio_addr_t i;
    for (i = 0; i < size; i++) {
        dev->pio[dev->num_pio++] = ioport++;
    }
}
```

sysbus_add_io分别将`s->data_mem`和`s->data_mem`添加到了system_io中，在i440fx_pcihost_initfn中给这个两mr注册了ops。

当子机执行对这两个端口的操作，就会退出，然后在qemu中执行对应的ops的操作。

## 端口操作函数

```c
const MemoryRegionOps pci_host_conf_le_ops = {
    .read = pci_host_config_read,
    .write = pci_host_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};
const MemoryRegionOps pci_host_data_le_ops = {
    .read = pci_host_data_read,
    .write = pci_host_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};
```

### config 操作函数

```c
static void pci_host_config_write(void *opaque, hwaddr addr, uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;

    PCI_DPRINTF("%s addr " TARGET_FMT_plx " len %d val %"PRIx64"\n", __func__, addr, len, val);
    if (addr != 0 || len != 4) {
        return;
    }
    s->config_reg = val;
}

static uint64_t pci_host_config_read(void *opaque, hwaddr addr, unsigned len)
{
    PCIHostState *s = opaque;
    uint32_t val = s->config_reg;

    PCI_DPRINTF("%s addr " TARGET_FMT_plx " len %d val %"PRIx32"\n", __func__, addr, len, val);
    return val;
}
```

config 的操作函数实现很简单，写入就是将值保存到s->config_reg中，读取就说返回s->config_reg，s->config_reg会在后面的数据操作函数中用到，s->config_reg保存了pci的bdf和寄存器地址值。

### data操作函数

#### pci_host_data_write

```c
static void pci_host_data_write(void *opaque, hwaddr addr, uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;
    PCI_DPRINTF("write addr " TARGET_FMT_plx " len %d val %x\n", addr, len, (unsigned)val);
    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, len);
}
```

##### pci_data_write

```c
void pci_data_write(PCIBus *s, uint32_t addr, uint32_t val, int len)
{
    /*  */
    PCIDevice *pci_dev = pci_dev_find_by_addr(s, addr);
    uint32_t config_addr = addr & (PCI_CONFIG_SPACE_SIZE - 1);

    PCI_DPRINTF("%s: %s: addr=%02" PRIx32 " val=%08" PRIx32 " len=%d\n",
                __func__, pci_dev->name, config_addr, val, len);
    pci_host_config_write_common(pci_dev, config_addr, PCI_CONFIG_SPACE_SIZE, val, len);
}
```

###### pci_dev_find_by_addr

```c
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
```

###### pci_find_device

```c
PCIDevice *pci_find_device(PCIBus *bus, int bus_num, uint8_t devfn)
{
    bus = pci_find_bus_nr(bus, bus_num);
    return bus->devices[devfn];
}
```

#### pci_host_config_write_common

```c
void pci_host_config_write_common(PCIDevice *pci_dev, uint32_t addr,
                                  uint32_t limit, uint32_t val, uint32_t len)
{
    /* non-zero functions are only exposed when function 0 is present,
     * allowing direct removal of unexposed functions.
     */
    if (pci_dev->qdev.hotplugged && !pci_get_function_0(pci_dev)) {
        return;
    }

    trace_pci_cfg_write(pci_dev->name, PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn), addr, val);
    pci_dev->config_write(pci_dev, addr, val, MIN(len, limit - addr));
}
```

调用到具体的pci设备的config_write函数了。

#### pci_host_data_read

```c
static uint64_t pci_host_data_read(void *opaque, hwaddr addr, unsigned len)
{
    PCIHostState *s = opaque;
    uint32_t val;
    if (!(s->config_reg & (1U << 31))) {
        return 0xffffffff;
    }
    val = pci_data_read(s->bus, s->config_reg | (addr & 3), len);
    PCI_DPRINTF("read addr " TARGET_FMT_plx " len %d val %x\n", addr, len, val);
    return val;
}
```

##### pci_data_read

```c
uint32_t pci_data_read(PCIBus *s, uint32_t addr, int len)
{
    PCIDevice *pci_dev = pci_dev_find_by_addr(s, addr);
    uint32_t config_addr = addr & (PCI_CONFIG_SPACE_SIZE - 1);
    uint32_t val;

    val = pci_host_config_read_common(pci_dev, config_addr, PCI_CONFIG_SPACE_SIZE, len);
    PCI_DPRINTF("%s: %s: addr=%02"PRIx32" val=%08"PRIx32" len=%d\n",
                __func__, pci_dev->name, config_addr, val, len);

    return val;
}
```

##### pci_host_config_read_common

```c
uint32_t pci_host_config_read_common(PCIDevice *pci_dev, uint32_t addr, uint32_t limit, uint32_t len)
{
    uint32_t ret;
    ret = pci_dev->config_read(pci_dev, addr, MIN(len, limit - addr));
    return ret;
}
```

调用到具体的pci设备config_read函数

## 设备的config函数

### pci_device_type_info

```c
static const TypeInfo pci_device_type_info = {
    .name = TYPE_PCI_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .abstract = true,
    .class_size = sizeof(PCIDeviceClass),
    .class_init = pci_device_class_init,
};
```

### pci_device_class_init

```c
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
```

### pci_qdev_realize

```c
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
```

#### do_pci_register_device

```c
/* -1 for devfn means auto assign */
static PCIDevice *do_pci_register_device(PCIDevice *pci_dev, PCIBus *bus, const char *name, int devfn,
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
        error_setg(errp, "PCI: Only PCI/PCIe bridges can be plugged into %s", bus->parent_dev->name);
        return NULL;
    }

    if (devfn < 0) {
        for(devfn = bus->devfn_min ; devfn < ARRAY_SIZE(bus->devices);
            devfn += PCI_FUNC_MAX) {
            if (!bus->devices[devfn])
                goto found;
        }
        error_setg(errp, "PCI: no slot/function available for %s, all in use", name);
        return NULL;
    found: ;
    } else if (bus->devices[devfn]) {
        error_setg(errp, "PCI: slot %d function %d not available for %s,"
                   " in use by %s", PCI_SLOT(devfn), PCI_FUNC(devfn), name, bus->devices[devfn]->name);
        return NULL;
    } else if (dev->hotplugged &&
               pci_get_function_0(pci_dev)) {
        error_setg(errp, "PCI: slot %d function 0 already ocuppied by %s,"
                   " new func %s cannot be exposed to guest.", PCI_SLOT(devfn),
                   bus->devices[PCI_DEVFN(PCI_SLOT(devfn), 0)]->name, name);
       return NULL;
    }

    pci_dev->devfn = devfn;
    pci_dev->requester_id_cache = pci_req_id_cache_get(pci_dev);

    if (qdev_hotplug) {
        pci_init_bus_master(pci_dev);
    }
    pstrcpy(pci_dev->name, sizeof(pci_dev->name), name);
    pci_dev->irq_state = 0;
    pci_config_alloc(pci_dev);
	/* 给config空间赋值 */
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
	/* 给pci 设备的config 配置读写函数 */
    if (!config_read)
        config_read = pci_default_read_config;
    if (!config_write)
        config_write = pci_default_write_config;
    pci_dev->config_read = config_read;
    pci_dev->config_write = config_write;
    /* 将pci_dev 赋值给bus中的devices数组 */
    bus->devices[devfn] = pci_dev;
    pci_dev->version_id = 2; /* Current pci device vmstate version */
    return pci_dev;
}
```

### pci_default_write_config

```c
void pci_default_write_config(PCIDevice *d, uint32_t addr, uint32_t val_in, int l)
{
    int i, was_irq_disabled = pci_irq_disabled(d);
    uint32_t val = val_in;

    for (i = 0; i < l; val >>= 8, ++i) {
        uint8_t wmask = d->wmask[addr + i];
        uint8_t w1cmask = d->w1cmask[addr + i];
        assert(!(wmask & w1cmask));
        d->config[addr + i] = (d->config[addr + i] & ~wmask) | (val & wmask);
        d->config[addr + i] &= ~(val & w1cmask); /* W1C: Write 1 to Clear */
    }
    /* ranges_overlap 潘睿addr + l，是不是在 PCI_BASE_ADDRESS_0 + 24的范围内 */
    if (ranges_overlap(addr, l, PCI_BASE_ADDRESS_0, 24) ||
        ranges_overlap(addr, l, PCI_ROM_ADDRESS, 4) ||
        ranges_overlap(addr, l, PCI_ROM_ADDRESS1, 4) ||
        range_covers_byte(addr, l, PCI_COMMAND))
        pci_update_mappings(d);

    if (range_covers_byte(addr, l, PCI_COMMAND)) {
        pci_update_irq_disabled(d, was_irq_disabled);
        memory_region_set_enabled(&d->bus_master_enable_region,
                                  pci_get_word(d->config + PCI_COMMAND)
                                    & PCI_COMMAND_MASTER);
    }

    msi_write_config(d, addr, val_in, l);
    msix_write_config(d, addr, val_in, l);
}

```

#### pci_default_read_config

```c
uint32_t pci_default_read_config(PCIDevice *d, uint32_t address, int len)
{
    uint32_t val = 0;

    memcpy(&val, d->config + address, len);
    return le32_to_cpu(val);
}
```

