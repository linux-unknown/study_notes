memory_region_init(system_memory, NULL, "system", UINT64_MAX);
address_space_init(&address_space_memory, system_memory, "memory");

void pc_pci_as_mapping_init(Object *owner, MemoryRegion *system_memory,
                            MemoryRegion *pci_address_space)
{
    /* Set to lower priority than RAM 
	 * system_memory即全局变量system_memory
	 */
    memory_region_add_subregion_overlap(system_memory, 0x0,
                                        pci_address_space, -1);
}


/* b = pci_bus_new(dev, NULL, pci_address_space, address_space_io, 0, TYPE_PCI_BUS);
 * pci_bus_new-->pci_bus_init
 */
static void pci_bus_init(PCIBus *bus, DeviceState *parent,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io,
                         uint8_t devfn_min)
{
    assert(PCI_FUNC(devfn_min) == 0);
    bus->devfn_min = devfn_min;
    bus->address_space_mem = address_space_mem;
    bus->address_space_io = address_space_io;

    /* host bridge */
    QLIST_INIT(&bus->child);

    pci_host_bus_register(parent);
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


// pci_default_write_config
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


static void pci_update_mappings(PCIDevice *d)
{
    PCIIORegion *r;
    int i;
    pcibus_t new_addr;

    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];

        /* this region isn't registered */
        if (!r->size)
            continue;

        new_addr = pci_bar_address(d, i, r->type, r->size);

        /* This bar isn't changed */
        if (new_addr == r->addr)
            continue;

        /* now do the real mapping */
        if (r->addr != PCI_BAR_UNMAPPED) {
            trace_pci_update_mappings_del(d, pci_bus_num(d->bus),
                                          PCI_SLOT(d->devfn),
                                          PCI_FUNC(d->devfn),
                                          i, r->addr, r->size);
            memory_region_del_subregion(r->address_space, r->memory);
        }
        r->addr = new_addr;
        if (r->addr != PCI_BAR_UNMAPPED) {
            trace_pci_update_mappings_add(d, pci_bus_num(d->bus),
                                          PCI_SLOT(d->devfn),
                                          PCI_FUNC(d->devfn),
                                          i, r->addr, r->size);
			/* memory作为子memregion添加到address_space中
			 * r->address_space为
			 */
            memory_region_add_subregion_overlap(r->address_space,
                                                r->addr, r->memory, 1);
        }
    }

    pci_update_vga(d);
}


/*
 *  1.先是注册pci设备，register_bar
 *  2.在pci设备枚举的时候会给bar设置地址，这个时候调用pci_default_write_config注册
 *    bar对应的memory_region
 */
