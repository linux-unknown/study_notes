TypeInfo 继承关系

```c
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
```

上面的TypeInfo都会通过 type_register_static 进行注册，会把TypeInfo根据name放入hash表中。

1. 在qdev_get_device_class-->object_class_by_name-->type_initialize中会递归调用给TypeInfo.class分配class_init内存

2. 在object_new-->object_new_with_type-->object_initialize_with_type-->object_init_with_typez中会递归调用instance_init

   在object_new_with_type中会给分配object内存，大小为vhost_user_blk_pci_info的instance_size(在该例子中)

