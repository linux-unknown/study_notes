#define TYPE_PCI_BUS "PCI"


static const TypeInfo pci_bus_info = {
    .name = TYPE_PCI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(PCIBus),
    .class_size = sizeof(PCIBusClass),
    .class_init = pci_bus_class_init,
};

static void pci_register_types(void)
{
    type_register_static(&pci_bus_info);
    type_register_static(&pcie_bus_info);
    type_register_static(&pci_device_type_info);
}

type_init(pci_register_types)


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

