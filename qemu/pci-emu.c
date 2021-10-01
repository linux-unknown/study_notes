
	static void pc_machine_class_init(ObjectClass *oc, void *data)
	{
		MachineClass *mc = MACHINE_CLASS(oc);
		PCMachineClass *pcmc = PC_MACHINE_CLASS(oc);
		HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);
		NMIClass *nc = NMI_CLASS(oc);
	
		pcmc->get_hotplug_handler = mc->get_hotplug_handler;
		pcmc->pci_enabled = true;
		pcmc->has_acpi_build = true;
		pcmc->rsdp_in_ram = true;
		pcmc->smbios_defaults = true;
		pcmc->smbios_uuid_encoded = true;
		pcmc->gigabyte_align = true;
		pcmc->has_reserved_memory = true;
		pcmc->kvmclock_enabled = true;
		pcmc->enforce_aligned_dimm = true;
		/* BIOS ACPI tables: 128K. Other BIOS datastructures: less than 4K reported
		 * to be used at the moment, 32K should be enough for a while.	*/
		pcmc->acpi_data_size = 0x20000 + 0x8000;
		pcmc->save_tsc_khz = true;
		mc->get_hotplug_handler = pc_get_hotpug_handler;
		mc->cpu_index_to_socket_id = pc_cpu_index_to_socket_id;
		mc->possible_cpu_arch_ids = pc_possible_cpu_arch_ids;
		mc->query_hotpluggable_cpus = pc_query_hotpluggable_cpus;
		mc->default_boot_order = "cad";
		mc->hot_add_cpu = pc_hot_add_cpu;
		/* 240: max CPU count for RHEL */
		mc->max_cpus = 240;
		mc->reset = pc_machine_reset;
		hc->pre_plug = pc_machine_device_pre_plug_cb;
		hc->plug = pc_machine_device_plug_cb;
		hc->unplug_request = pc_machine_device_unplug_request_cb;
		hc->unplug = pc_machine_device_unplug_cb;
		nc->nmi_monitor_handler = x86_nmi;
	}


	static void pc_machine_initfn(Object *obj)
	{
		PCMachineState *pcms = PC_MACHINE(obj);
	
		object_property_add(obj, PC_MACHINE_MEMHP_REGION_SIZE, "int",
							pc_machine_get_hotplug_memory_region_size,
							NULL, NULL, NULL, &error_abort);
	
		pcms->max_ram_below_4g = 0xe0000000; /* 3.5G */
		object_property_add(obj, PC_MACHINE_MAX_RAM_BELOW_4G, "size",
							pc_machine_get_max_ram_below_4g,
							pc_machine_set_max_ram_below_4g,
							NULL, NULL, &error_abort);
		object_property_set_description(obj, PC_MACHINE_MAX_RAM_BELOW_4G,
										"Maximum ram below the 4G boundary (32bit boundary)",
										&error_abort);
	
		pcms->smm = ON_OFF_AUTO_AUTO;
		object_property_add(obj, PC_MACHINE_SMM, "OnOffAuto",
							pc_machine_get_smm,
							pc_machine_set_smm,
							NULL, NULL, &error_abort);
		object_property_set_description(obj, PC_MACHINE_SMM,
										"Enable SMM (pc & q35)",
										&error_abort);
	
		pcms->vmport = ON_OFF_AUTO_AUTO;
		object_property_add(obj, PC_MACHINE_VMPORT, "OnOffAuto",
							pc_machine_get_vmport,
							pc_machine_set_vmport,
							NULL, NULL, &error_abort);
		object_property_set_description(obj, PC_MACHINE_VMPORT,
										"Enable vmport (pc & q35)",
										&error_abort);
	
		/* nvdimm is disabled on default. */
		pcms->acpi_nvdimm_state.is_enabled = false;
		object_property_add_bool(obj, PC_MACHINE_NVDIMM, pc_machine_get_nvdimm,
								 pc_machine_set_nvdimm, &error_abort);
	
		pcms->smbus = true;
		object_property_add_bool(obj, PC_MACHINE_SMBUS,
								 pc_machine_get_smbus, pc_machine_set_smbus,
								 &error_abort);
	
		pcms->sata = true;
		object_property_add_bool(obj, PC_MACHINE_SATA,
								 pc_machine_get_sata, pc_machine_set_sata,
								 &error_abort);
	
		pcms->pit = true;
		object_property_add_bool(obj, PC_MACHINE_PIT,
								 pc_machine_get_pit, pc_machine_set_pit,
								 &error_abort);
	}

	
	static const TypeInfo pc_machine_info = {
		.name = TYPE_PC_MACHINE,
		.parent = TYPE_MACHINE,
		.abstract = true,
		.instance_size = sizeof(PCMachineState),
		.instance_init = pc_machine_initfn,
		.class_size = sizeof(PCMachineClass),
		.class_init = pc_machine_class_init,
		.interfaces = (InterfaceInfo[]) {
			 { TYPE_HOTPLUG_HANDLER },
			 { TYPE_NMI },
			 { }
		},
	};



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

#define TYPE_I440FX_PCI_HOST_BRIDGE "i440FX-pcihost"
#define TYPE_I440FX_PCI_DEVICE "i440FX"

static void pc_init_rhel730(MachineState *machine)
{
    pc_init1(machine, TYPE_I440FX_PCI_HOST_BRIDGE, \
             TYPE_I440FX_PCI_DEVICE);
}

/* PC hardware initialisation */
static void pc_init1(MachineState *machine,
                     const char *host_type, const char *pci_type)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *system_io = get_system_io();
    int i;
    PCIBus *pci_bus;
    ISABus *isa_bus;
    PCII440FXState *i440fx_state;
    int piix3_devfn = -1;
    qemu_irq *gsi;
    qemu_irq *i8259;
    qemu_irq smi_irq;
    GSIState *gsi_state;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BusState *idebus[MAX_IDE_BUS];
    ISADevice *rtc_state;
    MemoryRegion *ram_memory;
    MemoryRegion *pci_memory;
    MemoryRegion *rom_memory;
    ram_addr_t lowmem;

    /*
     * Calculate ram split, for memory below and above 4G.  It's a bit
     * complicated for backward compatibility reasons ...
     *
     *  - Traditional split is 3.5G (lowmem = 0xe0000000).  This is the
     *    default value for max_ram_below_4g now.
     *
     *  - Then, to gigabyte align the memory, we move the split to 3G
     *    (lowmem = 0xc0000000).  But only in case we have to split in
     *    the first place, i.e. ram_size is larger than (traditional)
     *    lowmem.  And for new machine types (gigabyte_align = true)
     *    only, for live migration compatibility reasons.
     *
     *  - Next the max-ram-below-4g option was added, which allowed to
     *    reduce lowmem to a smaller value, to allow a larger PCI I/O
     *    window below 4G.  qemu doesn't enforce gigabyte alignment here,
     *    but prints a warning.
     *
     *  - Finally max-ram-below-4g got updated to also allow raising lowmem,
     *    so legacy non-PAE guests can get as much memory as possible in
     *    the 32bit address space below 4G.
     *
     * Examples:
     *    qemu -M pc-1.7 -m 4G    (old default)    -> 3584M low,  512M high
     *    qemu -M pc -m 4G        (new default)    -> 3072M low, 1024M high
     *    qemu -M pc,max-ram-below-4g=2G -m 4G     -> 2048M low, 2048M high
     *    qemu -M pc,max-ram-below-4g=4G -m 3968M  -> 3968M low (=4G-128M)
     */
    lowmem = pcms->max_ram_below_4g;
    if (machine->ram_size >= pcms->max_ram_below_4g) {
        if (pcmc->gigabyte_align) {
            if (lowmem > 0xc0000000) {
                lowmem = 0xc0000000;
            }
            if (lowmem & ((1ULL << 30) - 1)) {
                error_report("Warning: Large machine and max_ram_below_4g "
                             "(%" PRIu64 ") not a multiple of 1G; "
                             "possible bad performance.",
                             pcms->max_ram_below_4g);
            }
        }
    }

    if (machine->ram_size >= lowmem) {
        pcms->above_4g_mem_size = machine->ram_size - lowmem;
        pcms->below_4g_mem_size = lowmem;
    } else {
        pcms->above_4g_mem_size = 0;
        pcms->below_4g_mem_size = machine->ram_size;
    }

    if (xen_enabled()) {
        xen_hvm_init(pcms, &ram_memory);
    }

    pc_cpus_init(pcms);

    if (kvm_enabled() && pcmc->kvmclock_enabled) {
        kvmclock_create();
    }

    if (pcmc->pci_enabled) {
        pci_memory = g_new(MemoryRegion, 1);
        memory_region_init(pci_memory, NULL, "pci", UINT64_MAX);
        rom_memory = pci_memory;
    } else {
        pci_memory = NULL;
        rom_memory = system_memory;
    }

    pc_guest_info_init(pcms);

    if (pcmc->smbios_defaults) {
        MachineClass *mc = MACHINE_GET_CLASS(machine);
        /* These values are guest ABI, do not change */
        smbios_set_defaults("Smdbmds", "KVM",
                            mc->desc, pcmc->smbios_legacy_mode,
                            pcmc->smbios_uuid_encoded,
                            SMBIOS_ENTRY_POINT_21);
    }

    /* allocate ram and load rom/bios */
    if (!xen_enabled()) {
        pc_memory_init(pcms, system_memory,
                       rom_memory, &ram_memory);
    } else if (machine->kernel_filename != NULL) {
        /* For xen HVM direct kernel boot, load linux here */
        xen_load_linux(pcms);
    }

    gsi_state = g_malloc0(sizeof(*gsi_state));
    if (kvm_ioapic_in_kernel()) {
        kvm_pc_setup_irq_routing(pcmc->pci_enabled);
        gsi = qemu_allocate_irqs(kvm_pc_gsi_handler, gsi_state,
                                 GSI_NUM_PINS);
    } else {
        gsi = qemu_allocate_irqs(gsi_handler, gsi_state, GSI_NUM_PINS);
    }

    if (pcmc->pci_enabled) {
        pci_bus = i440fx_init(host_type,
                              pci_type,
                              &i440fx_state, &piix3_devfn, &isa_bus, gsi,
                              system_memory, system_io, machine->ram_size,
                              pcms->below_4g_mem_size,
                              pcms->above_4g_mem_size,
                              pci_memory, ram_memory);
        pcms->bus = pci_bus;
    } else {
        pci_bus = NULL;
        i440fx_state = NULL;
        isa_bus = isa_bus_new(NULL, get_system_memory(), system_io,
                              &error_abort);
        no_hpet = 1;
    }
    isa_bus_irqs(isa_bus, gsi);

    if (kvm_pic_in_kernel()) {
        i8259 = kvm_i8259_init(isa_bus);
    } else if (xen_enabled()) {
        i8259 = xen_interrupt_controller_init();
    } else {
        i8259 = i8259_init(isa_bus, pc_allocate_cpu_irq());
    }

    for (i = 0; i < ISA_NUM_IRQS; i++) {
        gsi_state->i8259_irq[i] = i8259[i];
    }
    g_free(i8259);
    if (pcmc->pci_enabled) {
        ioapic_init_gsi(gsi_state, "i440fx");
    }

    pc_register_ferr_irq(gsi[13]);

    pc_vga_init(isa_bus, pcmc->pci_enabled ? pci_bus : NULL);

    assert(pcms->vmport != ON_OFF_AUTO__MAX);
    if (pcms->vmport == ON_OFF_AUTO_AUTO) {
        pcms->vmport = xen_enabled() ? ON_OFF_AUTO_OFF : ON_OFF_AUTO_ON;
    }

    /* init basic PC hardware */
    pc_basic_device_init(isa_bus, gsi, &rtc_state, true,
                         (pcms->vmport != ON_OFF_AUTO_ON), pcms->pit, 0x4);

    pc_nic_init(isa_bus, pci_bus);

    ide_drive_get(hd, ARRAY_SIZE(hd));
    if (pcmc->pci_enabled) {
        PCIDevice *dev;
        if (xen_enabled()) {
            dev = pci_piix3_xen_ide_init(pci_bus, hd, piix3_devfn + 1);
        } else {
            dev = pci_piix3_ide_init(pci_bus, hd, piix3_devfn + 1);
        }
        idebus[0] = qdev_get_child_bus(&dev->qdev, "ide.0");
        idebus[1] = qdev_get_child_bus(&dev->qdev, "ide.1");
    } else {
        for(i = 0; i < MAX_IDE_BUS; i++) {
            ISADevice *dev;
            char busname[] = "ide.0";
            dev = isa_ide_init(isa_bus, ide_iobase[i], ide_iobase2[i],
                               ide_irq[i],
                               hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
            /*
             * The ide bus name is ide.0 for the first bus and ide.1 for the
             * second one.
             */
            busname[4] = '0' + i;
            idebus[i] = qdev_get_child_bus(DEVICE(dev), busname);
        }
    }

    pc_cmos_init(pcms, idebus[0], idebus[1], rtc_state);

    if (pcmc->pci_enabled && usb_enabled()) {
        pci_create_simple(pci_bus, piix3_devfn + 2, "piix3-usb-uhci");
    }

    if (pcmc->pci_enabled && acpi_enabled) {
        DeviceState *piix4_pm;
        I2CBus *smbus;

        smi_irq = qemu_allocate_irq(pc_acpi_smi_interrupt, first_cpu, 0);
        /* TODO: Populate SPD eeprom data.  */
        smbus = piix4_pm_init(pci_bus, piix3_devfn + 3, 0xb100,
                              gsi[9], smi_irq,
                              pc_machine_is_smm_enabled(pcms),
                              &piix4_pm);
        smbus_eeprom_init(smbus, 8, NULL, 0);

        object_property_add_link(OBJECT(machine), PC_MACHINE_ACPI_DEVICE_PROP,
                                 TYPE_HOTPLUG_HANDLER,
                                 (Object **)&pcms->acpi_dev,
                                 object_property_allow_set_link,
                                 OBJ_PROP_LINK_UNREF_ON_RELEASE, &error_abort);
        object_property_set_link(OBJECT(machine), OBJECT(piix4_pm),
                                 PC_MACHINE_ACPI_DEVICE_PROP, &error_abort);
    }

    if (pcmc->pci_enabled) {
        pc_pci_device_init(pci_bus);
    }

    if (pcms->acpi_nvdimm_state.is_enabled) {
        nvdimm_init_acpi_state(&pcms->acpi_nvdimm_state, system_io,
                               pcms->fw_cfg, OBJECT(pcms));
    }
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
    DeviceState *dev;
    PCIBus *b;
    PCIDevice *d;
    PCIHostState *s;
    PIIX3State *piix3;
    PCII440FXState *f;
    unsigned i;
    I440FXState *i440fx;

    dev = qdev_create(NULL, host_type);
    s = PCI_HOST_BRIDGE(dev);
    b = pci_bus_new(dev, NULL, pci_address_space,
                    address_space_io, 0, TYPE_PCI_BUS);
    s->bus = b;
    object_property_add_child(qdev_get_machine(), "i440fx", OBJECT(dev), NULL);
    qdev_init_nofail(dev);

    d = pci_create_simple(b, 0, pci_type);
    *pi440fx_state = I440FX_PCI_DEVICE(d);
    f = *pi440fx_state;
    f->system_memory = address_space_mem;
    f->pci_address_space = pci_address_space;
    f->ram_memory = ram_memory;

    i440fx = I440FX_PCI_HOST_BRIDGE(dev);
    i440fx->pci_info.w32.begin = below_4g_mem_size;

    /* setup pci memory mapping */
    pc_pci_as_mapping_init(OBJECT(f), f->system_memory,
                           f->pci_address_space);

    /* if *disabled* show SMRAM to all CPUs */
    memory_region_init_alias(&f->smram_region, OBJECT(d), "smram-region",
                             f->pci_address_space, 0xa0000, 0x20000);
    memory_region_add_subregion_overlap(f->system_memory, 0xa0000,
                                        &f->smram_region, 1);
    memory_region_set_enabled(&f->smram_region, true);

    /* smram, as seen by SMM CPUs */
    memory_region_init(&f->smram, OBJECT(d), "smram", 1ull << 32);
    memory_region_set_enabled(&f->smram, true);
    memory_region_init_alias(&f->low_smram, OBJECT(d), "smram-low",
                             f->ram_memory, 0xa0000, 0x20000);
    memory_region_set_enabled(&f->low_smram, true);
    memory_region_add_subregion(&f->smram, 0xa0000, &f->low_smram);
    object_property_add_const_link(qdev_get_machine(), "smram",
                                   OBJECT(&f->smram), &error_abort);

    init_pam(dev, f->ram_memory, f->system_memory, f->pci_address_space,
             &f->pam_regions[0], PAM_BIOS_BASE, PAM_BIOS_SIZE);
    for (i = 0; i < 12; ++i) {
        init_pam(dev, f->ram_memory, f->system_memory, f->pci_address_space,
                 &f->pam_regions[i+1], PAM_EXPAN_BASE + i * PAM_EXPAN_SIZE,
                 PAM_EXPAN_SIZE);
    }

    /* Xen supports additional interrupt routes from the PCI devices to
     * the IOAPIC: the four pins of each PCI device on the bus are also
     * connected to the IOAPIC directly.
     * These additional routes can be discovered through ACPI. */
    if (xen_enabled()) {
        PCIDevice *pci_dev = pci_create_simple_multifunction(b,
                             -1, true, "PIIX3-xen");
        piix3 = PIIX3_PCI_DEVICE(pci_dev);
        pci_bus_irqs(b, xen_piix3_set_irq, xen_pci_slot_get_pirq,
                piix3, XEN_PIIX_NUM_PIRQS);
    } else {
        PCIDevice *pci_dev = pci_create_simple_multifunction(b,
                             -1, true, "PIIX3");
        piix3 = PIIX3_PCI_DEVICE(pci_dev);
        pci_bus_irqs(b, piix3_set_irq, pci_slot_get_pirq, piix3,
                PIIX_NUM_PIRQS);
        pci_bus_set_route_irq_fn(b, piix3_route_intx_pin_to_irq);
    }
    piix3->pic = pic;
    *isa_bus = ISA_BUS(qdev_get_child_bus(DEVICE(piix3), "isa.0"));

    *piix3_devfn = piix3->dev.devfn;

    ram_size = ram_size / 8 / 1024 / 1024;
    if (ram_size > 255) {
        ram_size = 255;
    }
    d->config[I440FX_COREBOOT_RAM_SIZE] = ram_size;

    i440fx_update_memory_mappings(f);

    return b;
}

DeviceState *qdev_create(BusState *bus, const char *name)
{
    DeviceState *dev;

    dev = qdev_try_create(bus, name);

    return dev;
}


void qdev_init_nofail(DeviceState *dev)
{
    Error *err = NULL;
	/* 设置 realized 属性，最终调用到  i440fx_pcihost_realize */
    object_property_set_bool(OBJECT(dev), true, "realized", &err);
   
}

DeviceState *qdev_try_create(BusState *bus, const char *type)
{
    DeviceState *dev;
	/* 初始化 TYPE_I440FX_PCI_HOST_BRIDGE */
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



static const TypeInfo i440fx_pcihost_info = {
    .name          = TYPE_I440FX_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(I440FXState),
    .instance_init = i440fx_pcihost_initfn,
    .class_init    = i440fx_pcihost_class_init,
};


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


static void i440fx_pcihost_initfn(Object *obj)
{
    PCIHostState *s = PCI_HOST_BRIDGE(obj);
    I440FXState *d = I440FX_PCI_HOST_BRIDGE(obj);

	/* 注册io   */
    memory_region_init_io(&s->conf_mem, obj, &pci_host_conf_le_ops, s,
                          "pci-conf-idx", 4);
    memory_region_init_io(&s->data_mem, obj, &pci_host_data_le_ops, s,
                          "pci-conf-data", 4);

    object_property_add(obj, PCI_HOST_PROP_PCI_HOLE_START, "int",
                        i440fx_pcihost_get_pci_hole_start,
                        NULL, NULL, NULL, NULL);

    object_property_add(obj, PCI_HOST_PROP_PCI_HOLE_END, "int",
                        i440fx_pcihost_get_pci_hole_end,
                        NULL, NULL, NULL, NULL);

    object_property_add(obj, PCI_HOST_PROP_PCI_HOLE64_START, "int",
                        i440fx_pcihost_get_pci_hole64_start,
                        NULL, NULL, NULL, NULL);

    object_property_add(obj, PCI_HOST_PROP_PCI_HOLE64_END, "int",
                        i440fx_pcihost_get_pci_hole64_end,
                        NULL, NULL, NULL, NULL);

    d->pci_info.w32.end = IO_APIC_DEFAULT_ADDRESS;
}

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
 
/* 以上完成后，pci       在枚举的时候，向端口 0xcf8和0xcfc写和读取就会调用到pci_host_conf_le_ops和
 * pci_host_data_le_ops的read和write函数
 *
 */

