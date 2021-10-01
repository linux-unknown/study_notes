main()
{
	/*
	case QEMU_OPTION_M:
	case QEMU_OPTION_machine:
		 olist = qemu_find_opts("machine");
		 opts = qemu_opts_parse_noisily(olist, optarg, true);
		 if (!opts) {
			 exit(1);
		 }
		 break;
	*/
	MachineClass *machine_class;
	machine_class = select_machine();

	current_machine = MACHINE(object_new(object_class_get_name(
                          OBJECT_CLASS(machine_class))));
	machine_opts = qemu_get_machine_opts();
	 if (qemu_opt_foreach(machine_opts, machine_set_property, current_machine,
						  NULL)) {
		 object_unref(OBJECT(current_machine));
		 exit(1);
	 }

}

MachineClass *find_default_machine(void)
{
	GSList *el, *machines = object_class_get_list(TYPE_MACHINE, false);
	MachineClass *mc = NULL;

	for (el = machines; el; el = el->next) {
		MachineClass *temp = el->data;

		if (temp->is_default) {
			mc = temp;
			break;
		}
	}

	g_slist_free(machines);
	return mc;
}


static MachineClass *select_machine(void)
{
	MachineClass *machine_class = find_default_machine();
	const char *optarg;
	QemuOpts *opts;
	Location loc;

	loc_push_none(&loc);

	opts = qemu_get_machine_opts();
	qemu_opts_loc_restore(opts);
	/* 根据type 参数找到machine */
	optarg = qemu_opt_get(opts, "type");
	if (optarg) {
		machine_class = machine_parse(optarg);
	}

	if (!machine_class) {
		error_report("No machine specified, and there is no default");
		error_printf("Use -machine help to list supported machines\n");
		exit(1);
	}

	loc_pop(&loc);
	return machine_class;
}


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
     * to be used at the moment, 32K should be enough for a while.  */
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



static const TypeInfo machine_info = {
    .name = TYPE_MACHINE,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(MachineClass),
    .class_init    = machine_class_init,
    .class_base_init = machine_class_base_init,
    .class_finalize = machine_class_finalize,
    .instance_size = sizeof(MachineState),
    .instance_init = machine_initfn,
    .instance_finalize = machine_finalize,
};

