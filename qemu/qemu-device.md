# qemu device

```c
int main(int argc, char * argv [])
{
	/* 将device opts结构体添加到vm_config_groups中 */
    qemu_add_opts(&qemu_device_opts);

	switch(popt->index) {
	/* 解析qemu 命令行中device的参数*/
	case QEMU_OPTION_device:
		if (!qemu_opts_parse_noisily(qemu_find_opts("device"), optarg, true)) {
			exit(1);
		}
	break;
	}
    /* 进行device的初始化，qemu_opts_foreach中会调用device_init_func */
    if (qemu_opts_foreach(qemu_find_opts("device"), device_init_func, NULL, NULL)) {
		exit(1);
    }
}
```

## device_init_func

以下面的参数进行device_init 的学习

```
-device vhost-user-blk-pci,chardev=drive-virtio-disk7,num-queues=4,bus=pci.0,addr=0x7,id=virtio-disk7
```
device_init_func-->qdev_device_add
```c
DeviceState *qdev_device_add(QemuOpts *opts, Error **errp)
{
    DeviceClass *dc;
    const char *driver, *path, *id;
    DeviceState *dev;
    BusState *bus = NULL;
    Error *err = NULL;

	/**
	 * -device vhost-user-blk-pci,chardev=drive-virtio-disk5,num-queues=4, bus=pci.0,addr=0x5,
	 * id=virtio-disk5
	 * driver是隐式参数，不带=就表示是driver的参数，即driver=vhost-user-blk-pci
	 */
    driver = qemu_opt_get(opts, "driver");

    /* find driver,根据driver名称找到DeviceClass */
    dc = qdev_get_device_class(&driver, errp);

    /* find bus */
    path = qemu_opt_get(opts, "bus");
    if (path != NULL) {
		/* 后面在分析 */
        bus = qbus_find(path, errp);
        if (!bus) {
            return NULL;
        }
        if (!object_dynamic_cast(OBJECT(bus), dc->bus_type)) {
            error_setg(errp, "Device '%s' can't go on %s bus",
                       driver, object_get_typename(OBJECT(bus)));
            return NULL;
        }
    } else if (dc->bus_type != NULL) {
        bus = qbus_find_recursive(sysbus_get_default(), NULL, dc->bus_type);
        if (!bus || qbus_is_full(bus)) {
            error_setg(errp, "No '%s' bus found for device '%s'",
                       dc->bus_type, driver);
            return NULL;
        }
    }
    if (qdev_hotplug && bus && !qbus_is_hotpluggable(bus)) {
        error_setg(errp, QERR_BUS_NO_HOTPLUG, bus->name);
        return NULL;
    }

    /* create device */
    dev = DEVICE(object_new(driver));

    if (bus) {
        qdev_set_parent_bus(dev, bus);
    }

    id = qemu_opts_id(opts);
    if (id) {
        dev->id = id;
    }

    if (dev->id) {
        object_property_add_child(qdev_get_peripheral(), dev->id,
                                  OBJECT(dev), NULL);
    } else {
        static int anon_count;
        gchar *name = g_strdup_printf("device[%d]", anon_count++);
        object_property_add_child(qdev_get_peripheral_anon(), name,
                                  OBJECT(dev), NULL);
        g_free(name);
    }

    /* set properties 对opts中的每个ops参数，调用 set_property 函数 */
    if (qemu_opt_foreach(opts, set_property, dev, &err)) {
        error_propagate(errp, err);
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        return NULL;
    }

    dev->opts = opts;
    /* 调用dev的realized属性函数 */
    object_property_set_bool(OBJECT(dev), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        dev->opts = NULL;
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        return NULL;
    }
    return dev;
}

```

qdev_device_add在qemu-typeinfo中已经分析过了