static Property virtio_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIODevice, host_features),
    DEFINE_PROP_BOOL("__com.redhat_rhel6_ctrl_guest_workaround", VirtIODevice,
                     rhel6_ctrl_guest_workaround, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(dev);
    Error *err = NULL;

    if (vdc->realize != NULL) {
        vdc->realize(dev, &err);
 
    }

    virtio_bus_device_plugged(vdev, &err);
}

/* A VirtIODevice is being plugged */
void virtio_bus_device_plugged(VirtIODevice *vdev, Error **errp)
{
    DeviceState *qdev = DEVICE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(qdev));
    VirtioBusState *bus = VIRTIO_BUS(qbus);
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(bus);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);

    DPRINTF("%s: plug device.\n", qbus->name);

    if (klass->pre_plugged != NULL) {
        klass->pre_plugged(qbus->parent, errp);
    }

    /* Get the features of the plugged device. */
    assert(vdc->get_features != NULL);
    vdev->host_features = vdc->get_features(vdev, vdev->host_features,
                                            errp);

    if (klass->device_plugged != NULL) {
        klass->device_plugged(qbus->parent, errp);
    }
}

static void virtio_device_class_init(ObjectClass *klass, void *data)
{
    /* Set the default value here. */
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = virtio_device_realize;
    dc->unrealize = virtio_device_unrealize;
    dc->bus_type = TYPE_VIRTIO_BUS;
    dc->props = virtio_properties;
}

static const TypeInfo virtio_device_info = {
    .name = TYPE_VIRTIO_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VirtIODevice),
    .class_init = virtio_device_class_init,
    .abstract = true,
    .class_size = sizeof(VirtioDeviceClass),
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_device_info);
}

type_init(virtio_register_types)

