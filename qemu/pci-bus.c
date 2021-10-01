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


static const TypeInfo pci_bus_info = {
    .name = TYPE_PCI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(PCIBus),
    .class_size = sizeof(PCIBusClass),
    .class_init = pci_bus_class_init,
};



static void bus_class_init(ObjectClass *class, void *data)
{
    BusClass *bc = BUS_CLASS(class);

    class->unparent = bus_unparent;
    bc->get_fw_dev_path = default_bus_get_fw_dev_path;
}


static void pci_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);
    PCIBusClass *pbc = PCI_BUS_CLASS(klass);

    k->print_dev = pcibus_dev_print;
    k->get_dev_path = pcibus_get_dev_path;
    k->get_fw_dev_path = pcibus_get_fw_dev_path;
    k->realize = pci_bus_realize;
    k->unrealize = pci_bus_unrealize;
    k->reset = pcibus_reset;

    pbc->is_root = pcibus_is_root;
    pbc->bus_num = pcibus_num;
    pbc->numa_node = pcibus_numa_node;
}

