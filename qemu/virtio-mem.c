address_space_init(&proxy->modern_as, &proxy->modern_cfg, "virtio-pci-cfg-as");

// virtio_pci_modern_regions_init(proxy);

static void virtio_pci_modern_regions_init(VirtIOPCIProxy *proxy)
{
    static const MemoryRegionOps common_ops = {
        .read = virtio_pci_common_read,
        .write = virtio_pci_common_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps isr_ops = {
        .read = virtio_pci_isr_read,
        .write = virtio_pci_isr_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps device_ops = {
        .read = virtio_pci_device_read,
        .write = virtio_pci_device_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps notify_ops = {
        .read = virtio_pci_notify_read,
        .write = virtio_pci_notify_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps notify_pio_ops = {
        .read = virtio_pci_notify_read,
        .write = virtio_pci_notify_write_pio,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };


    memory_region_init_io(&proxy->common.mr, OBJECT(proxy),
                          &common_ops,
                          proxy,
                          "virtio-pci-common",
                          proxy->common.size);

    memory_region_init_io(&proxy->isr.mr, OBJECT(proxy),
                          &isr_ops,
                          proxy,
                          "virtio-pci-isr",
                          proxy->isr.size);

    memory_region_init_io(&proxy->device.mr, OBJECT(proxy),
                          &device_ops,
                          virtio_bus_get_device(&proxy->bus),
                          "virtio-pci-device",
                          proxy->device.size);

    memory_region_init_io(&proxy->notify.mr, OBJECT(proxy),
                          &notify_ops,
                          virtio_bus_get_device(&proxy->bus),
                          "virtio-pci-notify",
                          proxy->notify.size);

    memory_region_init_io(&proxy->notify_pio.mr, OBJECT(proxy),
                          &notify_pio_ops,
                          virtio_bus_get_device(&proxy->bus),
                          "virtio-pci-notify-pio",
                          proxy->notify.size);
}



/* 在virtio_pci_realize中会赋值common，irs， device等的offset
 * virtio_pci_modern_mem_region_map(proxy, &proxy->common, &cap);
 * virtio_pci_modern_mem_region_map(proxy, &proxy->isr, &cap);
 * virtio_pci_modern_mem_region_map(proxy, &proxy->device, &cap);
 * virtio_pci_modern_mem_region_map(proxy, &proxy->notify, &notify.cap);
 */

static void virtio_pci_modern_mem_region_map(VirtIOPCIProxy *proxy,
                                             VirtIOPCIRegion *region,
                                             struct virtio_pci_cap *cap)
{
    virtio_pci_modern_region_map(proxy, region, cap,
                                 &proxy->modern_bar, proxy->modern_mem_bar);
}

static void virtio_pci_modern_region_map(VirtIOPCIProxy *proxy,
                                         VirtIOPCIRegion *region,
                                         struct virtio_pci_cap *cap,
                                         MemoryRegion *mr,
                                         uint8_t bar)
{
	/* proxy->common，proxy->isr等的mr作为proxy->modern_bar的子mr */
    memory_region_add_subregion(mr, region->offset, &region->mr);

    cap->cfg_type = region->type;
    cap->bar = bar;
    cap->offset = cpu_to_le32(region->offset);
    cap->length = cpu_to_le32(region->size);
    virtio_pci_add_mem_cap(proxy, cap);

}

void memory_region_add_subregion(MemoryRegion *mr,
                                 hwaddr offset,
                                 MemoryRegion *subregion)
{
    subregion->may_overlap = false;
    subregion->priority = 0;
    memory_region_add_subregion_common(mr, offset, subregion);
}

static void memory_region_add_subregion_common(MemoryRegion *mr,
                                               hwaddr offset,
                                               MemoryRegion *subregion)
{
    assert(!subregion->container);
    subregion->container = mr;
    subregion->addr = offset;
    memory_region_update_container_subregions(subregion);
}

static void memory_region_update_container_subregions(MemoryRegion *subregion)
{
    hwaddr offset = subregion->addr;
    MemoryRegion *mr = subregion->container;
    MemoryRegion *other;

    memory_region_transaction_begin();

    memory_region_ref(subregion);
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->may_overlap || other->may_overlap) {
            continue;
        }
        if (int128_ge(int128_make64(offset),
                      int128_add(int128_make64(other->addr), other->size))
            || int128_le(int128_add(int128_make64(offset), subregion->size),
                         int128_make64(other->addr))) {
            continue;
        }
#if 0
        printf("warning: subregion collision %llx/%llx (%s) "
               "vs %llx/%llx (%s)\n",
               (unsigned long long)offset,
               (unsigned long long)int128_get64(subregion->size),
               subregion->name,
               (unsigned long long)other->addr,
               (unsigned long long)int128_get64(other->size),
               other->name);
#endif
    }
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->priority >= other->priority) {
            QTAILQ_INSERT_BEFORE(other, subregion, subregions_link);
            goto done;
        }
    }
	/* 将subregions添加到subregions_link链表中         */
    QTAILQ_INSERT_TAIL(&mr->subregions, subregion, subregions_link);
done:
    memory_region_update_pending |= mr->enabled && subregion->enabled;
    memory_region_transaction_commit();
}

void memory_region_transaction_commit(void)
{
    AddressSpace *as;

    assert(memory_region_transaction_depth);
    --memory_region_transaction_depth;
    if (!memory_region_transaction_depth) {
        if (memory_region_update_pending) {
			/* 调用listener的begin函数 */
            MEMORY_LISTENER_CALL_GLOBAL(begin, Forward);

            QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
                address_space_update_topology(as);
            }

            MEMORY_LISTENER_CALL_GLOBAL(commit, Forward);
        } else if (ioeventfd_update_pending) {
            QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
                address_space_update_ioeventfds(as);
            }
        }
        memory_region_clear_pending();
   }
}

static void address_space_update_topology(AddressSpace *as)
{
    FlatView *old_view = address_space_get_flatview(as);
    FlatView *new_view = generate_memory_topology(as->root);

    address_space_update_topology_pass(as, old_view, new_view, false);
    address_space_update_topology_pass(as, old_view, new_view, true);

    /* Writes are protected by the BQL.  */
    atomic_rcu_set(&as->current_map, new_view);
    call_rcu(old_view, flatview_unref, rcu);

    /* Note that all the old MemoryRegions are still alive up to this
     * point.  This relieves most MemoryListeners from the need to
     * ref/unref the MemoryRegions they get---unless they use them
     * outside the iothread mutex, in which case precise reference
     * counting is necessary.
     */
    flatview_unref(old_view);

    address_space_update_ioeventfds(as);
}

static void address_space_update_topology_pass(AddressSpace *as,
                                               const FlatView *old_view,
                                               const FlatView *new_view,
                                               bool adding)
{
    unsigned iold, inew;
    FlatRange *frold, *frnew;

    /* Generate a symmetric difference of the old and new memory maps.
     * Kill ranges in the old map, and instantiate ranges in the new map.
     */
    iold = inew = 0;
    while (iold < old_view->nr || inew < new_view->nr) {
        if (iold < old_view->nr) {
            frold = &old_view->ranges[iold];
        } else {
            frold = NULL;
        }
        if (inew < new_view->nr) {
            frnew = &new_view->ranges[inew];
        } else {
            frnew = NULL;
        }

        if (frold && (!frnew
                || int128_lt(frold->addr.start, frnew->addr.start)
                || (int128_eq(frold->addr.start, frnew->addr.start)
                    && !flatrange_equal(frold, frnew)))) {
            /* In old but not in new, or in both but attributes changed. */

            if (!adding) {
                MEMORY_LISTENER_UPDATE_REGION(frold, as, Reverse, region_del);
            }

            ++iold;
        } else if (frold && frnew && flatrange_equal(frold, frnew)) {
            /* In both and unchanged (except logging may have changed) */

            if (adding) {
				/* 会调用  listener的region_nop ，默认region的region_nop是region_add */
                MEMORY_LISTENER_UPDATE_REGION(frnew, as, Forward, region_nop);
                if (frnew->dirty_log_mask & ~frold->dirty_log_mask) {
                    /*
                     * only care about address_space_memory,
                     * cpu-memory/piix3-ide/virtio-blk-pci/KVM-SMRAM is not considered
                     */
                    if (delay_log()->delay && as == &address_space_memory && 
                        int128_get64(frnew->addr.start) > DELAY_LOG_HWADDR) {
                        delay_log_add(as, frnew, frold->dirty_log_mask);
                    } else {
                        MEMORY_LISTENER_UPDATE_REGION(frnew, as, Forward, log_start,
                                                      frold->dirty_log_mask,
                                                      frnew->dirty_log_mask);
                    }
                }
                if (frold->dirty_log_mask & ~frnew->dirty_log_mask) {
                    /*
                     * only care about address_space_memory,
                     * cpu-memory/piix3-ide/virtio-blk-pci/KVM-SMRAM is not considered
                     */
                    if (delay_log()->delay && as == &address_space_memory && 
                        int128_get64(frnew->addr.start) > DELAY_LOG_HWADDR) {
                        delay_log_add(as, frnew, frold->dirty_log_mask);
                    } else {
                        MEMORY_LISTENER_UPDATE_REGION(frnew, as, Reverse, log_stop,
                                                      frold->dirty_log_mask,
                                                      frnew->dirty_log_mask);
                    }
                }
            }

            ++iold;
            ++inew;
        } else {
            /* In new */

            if (adding) {
                MEMORY_LISTENER_UPDATE_REGION(frnew, as, Forward, region_add);
            }

            ++inew;
        }
    }
}

#define MEMORY_LISTENER_UPDATE_REGION(fr, as, dir, callback, _args...)  \
    MEMORY_LISTENER_CALL(callback, dir, (&(MemoryRegionSection) {       \
        .mr = (fr)->mr,                                                 \
        .address_space = (as),                                          \
        .offset_within_region = (fr)->offset_in_region,                 \
        .size = (fr)->addr.size,                                        \
        .offset_within_address_space = int128_get64((fr)->addr.start),  \
        .readonly = (fr)->readonly,                                     \
              }), ##_args)

define MEMORY_LISTENER_CALL(_callback, _direction, _section, _args...) \
    do {                                                                \
        MemoryListener *_listener;                                      \
                                                                        \
        switch (_direction) {                                           \
        case Forward:                                                   \
            QTAILQ_FOREACH(_listener, &memory_listeners, link) {        \
                if (_listener->_callback                                \
                    && memory_listener_match(_listener, _section)) {    \
                    _listener->_callback(_listener, _section, ##_args); \
                }                                                       \
            }                                                           \
            break;                                                      \
        case Reverse:                                                   \
            QTAILQ_FOREACH_REVERSE(_listener, &memory_listeners,        \
                                   memory_listeners, link) {            \
                if (_listener->_callback                                \
                    && memory_listener_match(_listener, _section)) {    \
                    _listener->_callback(_listener, _section, ##_args); \
                }                                                       \
            }                                                           \
            break;                                                      \
        default:                                                        \
            abort();                                                    \
        }                                                               \
    } while (0)


static bool memory_listener_match(MemoryListener *listener,
                                  MemoryRegionSection *section)
{
    return !listener->address_space_filter
        || listener->address_space_filter == section->address_space;
}


static int virtio_pci_add_mem_cap(VirtIOPCIProxy *proxy,
                                   struct virtio_pci_cap *cap)
{
    PCIDevice *dev = &proxy->pci_dev;
    int offset;

    offset = pci_add_capability(dev, PCI_CAP_ID_VNDR, 0, cap->cap_len);

    memcpy(dev->config + offset + PCI_CAP_FLAGS, &cap->cap_len, cap->cap_len - PCI_CAP_FLAGS);

    return offset;
}

