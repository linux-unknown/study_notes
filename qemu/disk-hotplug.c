
piix4_device_plug_cb
	acpi_pcihp_device_plug_cb
		acpi_send_event
			piix4_send_gpe
				acpi_send_gpe_event
					acpi_update_sci
						qemu_set_irq
							kvm_pic_set_irq
								kvm_set_irq
									kvm_vm_ioctl
		
		
static void piix4_pm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_CLASS(klass);

    k->realize = piix4_pm_realize;
    k->config_write = pm_write_config;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82371AB_3;
    k->revision = 0x03;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
    dc->desc = "PM";
    dc->vmsd = &vmstate_acpi;
    dc->props = piix4_pm_properties;
    /*
     * Reason: part of PIIX4 southbridge, needs to be wired up,
     * e.g. by mips_malta_init()
     */
    dc->cannot_instantiate_with_device_add_yet = true;
    dc->hotpluggable = false;
    hc->plug = piix4_device_plug_cb;
    hc->unplug_request = piix4_device_unplug_request_cb;
    hc->unplug = piix4_device_unplug_cb;
    adevc->ospm_status = piix4_ospm_status;
    adevc->send_event = piix4_send_gpe;
    adevc->madt_cpu = pc_madt_cpu_entry;
}


static void piix4_device_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev, Error **errp)
{
    PIIX4PMState *s = PIIX4_PM(hotplug_dev);

    if (s->acpi_memory_hotplug.is_enabled &&
        object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        acpi_memory_plug_cb(hotplug_dev, &s->acpi_memory_hotplug, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_plug_cb(hotplug_dev, &s->acpi_pci_hotplug, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        if (s->cpu_hotplug_legacy) {
            legacy_acpi_cpu_plug_cb(hotplug_dev, &s->gpe_cpu, dev, errp);
        } else {
            acpi_cpu_plug_cb(hotplug_dev, &s->cpuhp_state, dev, errp);
        }
    } else {
        error_setg(errp, "acpi: device plug request for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}


void hotplug_handler_plug(HotplugHandler *plug_handler,
                          DeviceState *plugged_dev,
                          Error **errp)
{
    HotplugHandlerClass *hdc = HOTPLUG_HANDLER_GET_CLASS(plug_handler);

    if (hdc->plug) {
        hdc->plug(plug_handler, plugged_dev, errp);
    }
}


static void device_set_realized(Object *obj, bool value, Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    HotplugHandler *hotplug_ctrl;
    BusState *bus;
    Error *local_err = NULL;
    bool unattached_parent = false;
    static int unattached_count;

    if (dev->hotplugged && !dc->hotpluggable) {
        error_setg(errp, QERR_DEVICE_NO_HOTPLUG, object_get_typename(obj));
        return;
    }

    if (value && !dev->realized) {
        if (!obj->parent) {
            gchar *name = g_strdup_printf("device[%d]", unattached_count++);

            object_property_add_child(container_get(qdev_get_machine(),
                                                    "/unattached"),
                                      name, obj, &error_abort);
            unattached_parent = true;
            g_free(name);
        }

        hotplug_ctrl = qdev_get_hotplug_handler(dev);
        if (hotplug_ctrl) {
            hotplug_handler_pre_plug(hotplug_ctrl, dev, &local_err);
            if (local_err != NULL) {
                goto fail;
            }
        }

        if (dc->realize) {
            dc->realize(dev, &local_err);
        }

        if (local_err != NULL) {
            goto fail;
        }

        DEVICE_LISTENER_CALL(realize, Forward, dev);

        if (hotplug_ctrl) {
            hotplug_handler_plug(hotplug_ctrl, dev, &local_err);
        }

        if (local_err != NULL) {
            goto post_realize_fail;
        }

        if (qdev_get_vmsd(dev)) {
            vmstate_register_with_alias_id(dev, -1, qdev_get_vmsd(dev), dev,
                                           dev->instance_id_alias,
                                           dev->alias_required_for_version);
        }

        QLIST_FOREACH(bus, &dev->child_bus, sibling) {
            object_property_set_bool(OBJECT(bus), true, "realized",
                                         &local_err);
            if (local_err != NULL) {
                goto child_realize_fail;
            }
        }
        if (dev->hotplugged) {
            device_reset(dev);
        }
        dev->pending_deleted_event = false;
    } else if (!value && dev->realized) {
        Error **local_errp = NULL;
        QLIST_FOREACH(bus, &dev->child_bus, sibling) {
            local_errp = local_err ? NULL : &local_err;
            object_property_set_bool(OBJECT(bus), false, "realized",
                                     local_errp);
        }
        if (qdev_get_vmsd(dev)) {
            vmstate_unregister(dev, qdev_get_vmsd(dev), dev);
        }
        if (dc->unrealize) {
            local_errp = local_err ? NULL : &local_err;
            dc->unrealize(dev, local_errp);
        }
        dev->pending_deleted_event = true;
        DEVICE_LISTENER_CALL(unrealize, Reverse, dev);
    }

    if (local_err != NULL) {
        goto fail;
    }

    dev->realized = value;
    return;

child_realize_fail:
    QLIST_FOREACH(bus, &dev->child_bus, sibling) {
        object_property_set_bool(OBJECT(bus), false, "realized",
                                 NULL);
    }

    if (qdev_get_vmsd(dev)) {
        vmstate_unregister(dev, qdev_get_vmsd(dev), dev);
    }

post_realize_fail:
    if (dc->unrealize) {
        dc->unrealize(dev, NULL);
    }

fail:
    error_propagate(errp, local_err);
    if (unattached_parent) {
        object_unparent(OBJECT(dev));
        unattached_count--;
    }
}
