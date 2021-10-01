int main(int argc, char *argv[])
{
	configure_accelerator(current_machine);
}


#define KVM_CAP_NR_MEMSLOTS 10   /* returns max memory slots per vm */

static QTAILQ_HEAD(memory_listeners, MemoryListener) memory_listeners
    = QTAILQ_HEAD_INITIALIZER(memory_listeners);


int configure_accelerator(MachineState *ms)
{
    const char *p;
    char buf[10];
    int ret;
    bool accel_initialised = false;
    bool init_failed = false;
    AccelClass *acc = NULL;

	/* accel=kvm */
    p = qemu_opt_get(qemu_get_machine_opts(), "accel");
    if (p == NULL) {
        /* Use kvm accelerator by default, fail to tcg*/
        p = "kvm:tcg";
    }

    while (!accel_initialised && *p != '\0') {
        if (*p == ':') {
            p++;
        }
        p = get_opt_name(buf, sizeof(buf), p, ':');
        acc = accel_find(buf);
        if (!acc) {
            fprintf(stderr, "\"%s\" accelerator not found.\n", buf);
            continue;
        }
        if (acc->available && !acc->available()) {
            printf("%s not supported for this target\n",
                   acc->name);
            continue;
        }
        ret = accel_init_machine(acc, ms);
        if (ret < 0) {
            init_failed = true;
            fprintf(stderr, "failed to initialize %s: %s\n",
                    acc->name,
                    strerror(-ret));
        } else {
            accel_initialised = true;
        }
    }

    if (!accel_initialised) {
        if (!init_failed) {
            fprintf(stderr, "No accelerator found!\n");
        }
        exit(1);
    }

    if (init_failed) {
        fprintf(stderr, "Back to %s accelerator.\n", acc->name);
    }

    return !accel_initialised;
}

static void tcg_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "tcg";
    ac->init_machine = tcg_init;
    ac->allowed = &tcg_allowed;
}

#define TYPE_TCG_ACCEL ACCEL_CLASS_NAME("tcg")


static const TypeInfo accel_type = {
    .name = TYPE_ACCEL,
    .parent = TYPE_OBJECT,
    .class_size = sizeof(AccelClass),
    .instance_size = sizeof(AccelState),
};


static const TypeInfo tcg_accel_type = {
    .name = TYPE_TCG_ACCEL,
    .parent = TYPE_ACCEL,
    .class_init = tcg_accel_class_init,
};

static void register_accel_types(void)
{
    type_register_static(&accel_type);
    type_register_static(&tcg_accel_type);
}

type_init(register_accel_types);


static const TypeInfo kvm_accel_type = {
    .name = TYPE_KVM_ACCEL,
    .parent = TYPE_ACCEL,
    .class_init = kvm_accel_class_init,
    .instance_size = sizeof(KVMState),
};

static void kvm_type_init(void)
{
    type_register_static(&kvm_accel_type);
}

type_init(kvm_type_init);

static void kvm_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "KVM";
    ac->init_machine = kvm_init;
    ac->allowed = &kvm_allowed;
}

static int accel_init_machine(AccelClass *acc, MachineState *ms)
{
    ObjectClass *oc = OBJECT_CLASS(acc);
    const char *cname = object_class_get_name(oc);
    AccelState *accel = ACCEL(object_new(cname));
    int ret;
    ms->accelerator = accel;
    *(acc->allowed) = true;
	/*  调用 kvm_init */
    ret = acc->init_machine(ms);

    return ret;
}



static int kvm_set_ioeventfd_pio(int fd, uint16_t addr, uint16_t val,
                                 bool assign, uint32_t size, bool datamatch)
{
    struct kvm_ioeventfd kick = {
        .datamatch = datamatch ? adjust_ioeventfd_endianness(val, size) : 0,
        .addr = addr,
        .flags = KVM_IOEVENTFD_FLAG_PIO,
        .len = size,
        .fd = fd,
    };
    int r;
    if (!kvm_enabled()) {
        return -ENOSYS;
    }
    if (datamatch) {
        kick.flags |= KVM_IOEVENTFD_FLAG_DATAMATCH;
    }
    if (!assign) {
        kick.flags |= KVM_IOEVENTFD_FLAG_DEASSIGN;
    }
    r = kvm_vm_ioctl(kvm_state, KVM_IOEVENTFD, &kick);
    if (r < 0) {
        return r;
    }
    return 0;
}

static void kvm_io_ioeventfd_add(MemoryListener *listener,
                                 MemoryRegionSection *section,
                                 bool match_data, uint64_t data,
                                 EventNotifier *e)
{
    int fd = event_notifier_get_fd(e);
    int r;

    r = kvm_set_ioeventfd_pio(fd, section->offset_within_address_space,
                              data, true, int128_get64(section->size),
                              match_data);
}


static MemoryListener kvm_io_listener = {
    .eventfd_add = kvm_io_ioeventfd_add,
    .eventfd_del = kvm_io_ioeventfd_del,
    .priority = 10,
};


static int kvm_init(MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    static const char upgrade_note[] =
        "Please upgrade to at least kernel 2.6.29 or recent kvm-kmod\n"
        "(see http://sourceforge.net/projects/kvm).\n";
    struct {
        const char *name;
        int num;
    } num_cpus[] = {
        { "SMP",          smp_cpus },
        { "hotpluggable", max_cpus },
        { NULL, }
    }, *nc = num_cpus;
    int soft_vcpus_limit, hard_vcpus_limit;
    KVMState *s;
    const KVMCapabilityInfo *missing_cap;
    int ret;
    int type = 0;
    const char *kvm_type;

    s = KVM_STATE(ms->accelerator);

    /*
     * On systems where the kernel can support different base page
     * sizes, host page size may be different from TARGET_PAGE_SIZE,
     * even with KVM.  TARGET_PAGE_SIZE is assumed to be the minimum
     * page size for the system though.
     */
    assert(TARGET_PAGE_SIZE <= getpagesize());

    s->sigmask_len = 8;

#ifdef KVM_CAP_SET_GUEST_DEBUG
    QTAILQ_INIT(&s->kvm_sw_breakpoints);
#endif
    QLIST_INIT(&s->kvm_parked_vcpus);
    s->vmfd = -1;
    s->fd = qemu_open("/dev/kvm", O_RDWR);


    ret = kvm_ioctl(s, KVM_GET_API_VERSION, 0);
 
 
    s->nr_slots = kvm_check_extension(s, KVM_CAP_NR_MEMSLOTS);

    /* If unspecified, use the default value */
    if (!s->nr_slots) {
        s->nr_slots = 32;
    }

    /* check the vcpu limits */
    soft_vcpus_limit = kvm_recommended_vcpus(s);
    hard_vcpus_limit = kvm_max_vcpus(s);

#ifdef HOST_PPC64

#else
    /* RHEL doesn't support nr_vcpus > soft_vcpus_limit */
    hard_vcpus_limit = soft_vcpus_limit;
#endif

    while (nc->name) {
        if (nc->num > soft_vcpus_limit) {
            fprintf(stderr,
                    "Warning: Number of %s cpus requested (%d) exceeds "
                    "the recommended cpus supported by KVM (%d)\n",
                    nc->name, nc->num, soft_vcpus_limit);

            if (nc->num > hard_vcpus_limit) {
                fprintf(stderr, "Number of %s cpus requested (%d) exceeds "
                        "the maximum cpus supported by KVM (%d)\n",
                        nc->name, nc->num, hard_vcpus_limit);
                exit(1);
            }
        }
        nc++;
    }

    set_vcpu_counts(smp_cpus);
	/* x86 没有这个选项 */
    kvm_type = qemu_opt_get(qemu_get_machine_opts(), "kvm-type");
    if (mc->kvm_type) {
        type = mc->kvm_type(kvm_type);
    } else if (kvm_type) {
        ret = -EINVAL;
        fprintf(stderr, "Invalid argument kvm-type=%s\n", kvm_type);
        goto err;
    }

    do {
        ret = kvm_ioctl(s, KVM_CREATE_VM, type);
    } while (ret == -EINTR);


    s->vmfd = ret;
    missing_cap = kvm_check_extension_list(s, kvm_required_capabilites);
    if (!missing_cap) {
        missing_cap =
            kvm_check_extension_list(s, kvm_arch_required_capabilities);
    }
    if (missing_cap) {
        ret = -EINVAL;
        fprintf(stderr, "kvm does not support %s\n%s",
                missing_cap->name, upgrade_note);
        goto err;
    }

    s->coalesced_mmio = kvm_check_extension(s, KVM_CAP_COALESCED_MMIO);
    s->coalesced_pio = s->coalesced_mmio && kvm_check_extension(s, KVM_CAP_COALESCED_PIO);

    s->broken_set_mem_region = 1;
    ret = kvm_check_extension(s, KVM_CAP_JOIN_MEMORY_REGIONS_WORKS);
    if (ret > 0) {
        s->broken_set_mem_region = 0;
    }

#ifdef KVM_CAP_VCPU_EVENTS
    s->vcpu_events = kvm_check_extension(s, KVM_CAP_VCPU_EVENTS);
#endif

    s->robust_singlestep = kvm_check_extension(s, KVM_CAP_X86_ROBUST_SINGLESTEP);

#ifdef KVM_CAP_DEBUGREGS
    s->debugregs = kvm_check_extension(s, KVM_CAP_DEBUGREGS);
#endif

#ifdef KVM_CAP_IRQ_ROUTING
    kvm_direct_msi_allowed = (kvm_check_extension(s, KVM_CAP_SIGNAL_MSI) > 0);
#endif

    s->intx_set_mask = kvm_check_extension(s, KVM_CAP_PCI_2_3);

    s->irq_set_ioctl = KVM_IRQ_LINE;
    if (kvm_check_extension(s, KVM_CAP_IRQ_INJECT_STATUS)) {
        s->irq_set_ioctl = KVM_IRQ_LINE_STATUS;
    }

#ifdef KVM_CAP_READONLY_MEM
    kvm_readonly_mem_allowed =
        (kvm_check_extension(s, KVM_CAP_READONLY_MEM) > 0);
#endif

    kvm_eventfds_allowed = (kvm_check_extension(s, KVM_CAP_IOEVENTFD) > 0);

    kvm_irqfds_allowed = (kvm_check_extension(s, KVM_CAP_IRQFD) > 0);

    kvm_resamplefds_allowed = (kvm_check_extension(s, KVM_CAP_IRQFD_RESAMPLE) > 0);

    kvm_vm_attributes_allowed = (kvm_check_extension(s, KVM_CAP_VM_ATTRIBUTES) > 0);

    kvm_ioeventfd_any_length_allowed = (kvm_check_extension(s, KVM_CAP_IOEVENTFD_ANY_LENGTH) > 0);

    ret = kvm_arch_init(ms, s);

    if (machine_kernel_irqchip_allowed(ms)) {
        kvm_irqchip_create(ms, s);
    }

    kvm_state = s;

    if (kvm_eventfds_allowed) {
        s->memory_listener.listener.eventfd_add = kvm_mem_ioeventfd_add;
        s->memory_listener.listener.eventfd_del = kvm_mem_ioeventfd_del;
    }
    s->memory_listener.listener.coalesced_io_add = kvm_coalesce_mmio_region;
    s->memory_listener.listener.coalesced_io_del = kvm_uncoalesce_mmio_region;

    kvm_memory_listener_register(s, &s->memory_listener, &address_space_memory, 0);
    memory_listener_register(&kvm_io_listener, &address_space_io);
    memory_listener_register(&kvm_coalesced_pio_listener, &address_space_io);

    s->many_ioeventfds = kvm_check_many_ioeventfds();

    cpu_interrupt_handler = kvm_handle_interrupt;

    return 0;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    uint64_t identity_base = 0xfffbc000;
    uint64_t shadow_mem;
    int ret;
    struct utsname utsname;

#ifdef KVM_CAP_XSAVE
    has_xsave = kvm_check_extension(s, KVM_CAP_XSAVE);
#endif

#ifdef KVM_CAP_XCRS
    has_xcrs = kvm_check_extension(s, KVM_CAP_XCRS);
#endif

#ifdef KVM_CAP_PIT_STATE2
    has_pit_state2 = kvm_check_extension(s, KVM_CAP_PIT_STATE2);
#endif

    ret = kvm_get_supported_msrs(s);
    if (ret < 0) {
        return ret;
    }

    uname(&utsname);
    lm_capable_kernel = strcmp(utsname.machine, "x86_64") == 0;

    /*
     * On older Intel CPUs, KVM uses vm86 mode to emulate 16-bit code directly.
     * In order to use vm86 mode, an EPT identity map and a TSS  are needed.
     * Since these must be part of guest physical memory, we need to allocate
     * them, both by setting their start addresses in the kernel and by
     * creating a corresponding e820 entry. We need 4 pages before the BIOS.
     *
     * Older KVM versions may not support setting the identity map base. In
     * that case we need to stick with the default, i.e. a 256K maximum BIOS
     * size.
     */
    if (kvm_check_extension(s, KVM_CAP_SET_IDENTITY_MAP_ADDR)) {
        /* Allows up to 16M BIOSes. */
        identity_base = 0xfeffc000;

        ret = kvm_vm_ioctl(s, KVM_SET_IDENTITY_MAP_ADDR, &identity_base);
        if (ret < 0) {
            return ret;
        }
    }

    /* Set TSS base one page after EPT identity map. */
    ret = kvm_vm_ioctl(s, KVM_SET_TSS_ADDR, identity_base + 0x1000);
    if (ret < 0) {
        return ret;
    }

    /* Tell fw_cfg to notify the BIOS to reserve the range. */
    ret = e820_add_entry(identity_base, 0x4000, E820_RESERVED);
    if (ret < 0) {
        fprintf(stderr, "e820_add_entry() table is full\n");
        return ret;
    }
    qemu_register_reset(kvm_unpoison_all, NULL);

    shadow_mem = machine_kvm_shadow_mem(ms);
    if (shadow_mem != -1) {
        shadow_mem /= 4096;
        ret = kvm_vm_ioctl(s, KVM_SET_NR_MMU_PAGES, shadow_mem);
        if (ret < 0) {
            return ret;
        }
    }

    if (kvm_check_extension(s, KVM_CAP_X86_SMM)) {
        smram_machine_done.notify = register_smram_listener;
        qemu_add_machine_init_done_notifier(&smram_machine_done);
    }

    if (enable_cpu_pm) {
        int ret;
        int disable_exits = kvm_check_extension(s, KVM_CAP_X86_DISABLE_EXITS);

        if (disable_exits) {
            disable_exits &= (KVM_X86_DISABLE_EXITS_MWAIT |
                              KVM_X86_DISABLE_EXITS_HLT |
                              KVM_X86_DISABLE_EXITS_PAUSE);
        }

        ret = kvm_vm_enable_cap(s, KVM_CAP_X86_DISABLE_EXITS, 0, disable_exits);
 
    }
    return 0;
}

void kvm_memory_listener_register(KVMState *s, KVMMemoryListener *kml,
                                  AddressSpace *as, int as_id)
{
    int i;

    kml->slots = g_malloc0(s->nr_slots * sizeof(KVMSlot));
    kml->as_id = as_id;

    for (i = 0; i < s->nr_slots; i++) {
        kml->slots[i].slot = i;
    }

    kml->listener.region_add = kvm_region_add;
    kml->listener.region_del = kvm_region_del;
    kml->listener.log_start = kvm_log_start;
    kml->listener.log_stop = kvm_log_stop;
    kml->listener.log_sync = kvm_log_sync;
    kml->listener.priority = 10;

    memory_listener_register(&kml->listener, as);
}

void memory_listener_register(MemoryListener *listener, AddressSpace *filter)
{
    MemoryListener *other = NULL;
    AddressSpace *as;

    listener->address_space_filter = filter;
    if (QTAILQ_EMPTY(&memory_listeners)
        || listener->priority >= QTAILQ_LAST(&memory_listeners,
                                             memory_listeners)->priority) {
        QTAILQ_INSERT_TAIL(&memory_listeners, listener, link);
    } else {
        QTAILQ_FOREACH(other, &memory_listeners, link) {
            if (listener->priority < other->priority) {
                break;
            }
        }
		/* 将address_space lisenter添加到memory_listeners链表中 */
        QTAILQ_INSERT_BEFORE(other, listener, link);
    }

    QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
        listener_add_address_space(listener, as);
    }
}

static void kvm_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);

    memory_region_ref(section->mr);
    kvm_set_phys_mem(kml, section, true);
}

static void kvm_set_phys_mem(KVMMemoryListener *kml,
                             MemoryRegionSection *section, bool add)
{
    KVMState *s = kvm_state;
    KVMSlot *mem, old;
    int err;
    MemoryRegion *mr = section->mr;
    bool writeable = !mr->readonly && !mr->rom_device;
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    void *ram = NULL;
    unsigned delta;

    /* kvm works in page size chunks, but the function may be called
       with sub-page size and unaligned start address. Pad the start
       address to next and truncate size to previous page boundary. */
    delta = qemu_real_host_page_size - (start_addr & ~qemu_real_host_page_mask);
    delta &= ~qemu_real_host_page_mask;
    if (delta > size) {
        return;
    }
    start_addr += delta;
    size -= delta;
    size &= qemu_real_host_page_mask;
    if (!size || (start_addr & ~qemu_real_host_page_mask)) {
        return;
    }

    if (!memory_region_is_ram(mr)) {
        if (writeable || !kvm_readonly_mem_allowed) {
            return;
        } else if (!mr->romd_mode) {
            /* If the memory device is not in romd_mode, then we actually want
             * to remove the kvm memory slot so all accesses will trap. */
            add = false;
        }
    }

    ram = memory_region_get_ram_ptr(mr) + section->offset_within_region + delta;

    while (1) {
        mem = kvm_lookup_overlapping_slot(kml, start_addr, start_addr + size);
        if (!mem) {
            break;
        }

        if (add && start_addr >= mem->start_addr &&
            (start_addr + size <= mem->start_addr + mem->memory_size) &&
            (ram - start_addr == mem->ram - mem->start_addr)) {
            /* The new slot fits into the existing one and comes with
             * identical parameters - update flags and done. */
            kvm_slot_update_flags(kml, mem, mr);
            return;
        }

        old = *mem;

        if (mem->flags & KVM_MEM_LOG_DIRTY_PAGES) {
            kvm_physical_sync_dirty_bitmap(kml, section);
        }

        /* unregister the overlapping slot */
        mem->memory_size = 0;
        mem->flags = 0;
        err = kvm_set_user_memory_region(kml, mem, false);
 

        /* Workaround for older KVM versions: we can't join slots, even not by
         * unregistering the previous ones and then registering the larger
         * slot. We have to maintain the existing fragmentation. Sigh.
         *
         * This workaround assumes that the new slot starts at the same
         * address as the first existing one. If not or if some overlapping
         * slot comes around later, we will fail (not seen in practice so far)
         * - and actually require a recent KVM version. */
        if (s->broken_set_mem_region &&
            old.start_addr == start_addr && old.memory_size < size && add) {
            mem = kvm_alloc_slot(kml);
            mem->memory_size = old.memory_size;
            mem->start_addr = old.start_addr;
            mem->ram = old.ram;
            mem->flags = kvm_mem_flags(mr);

            err = kvm_set_user_memory_region(kml, mem, true);
            if (err) {
                fprintf(stderr, "%s: error updating slot: %s\n", __func__,
                        strerror(-err));
                abort();
            }

            start_addr += old.memory_size;
            ram += old.memory_size;
            size -= old.memory_size;
            continue;
        }

        /* register prefix slot */
        if (old.start_addr < start_addr) {
            mem = kvm_alloc_slot(kml);
            mem->memory_size = start_addr - old.start_addr;
            mem->start_addr = old.start_addr;
            mem->ram = old.ram;
            mem->flags =  kvm_mem_flags(mr);

            err = kvm_set_user_memory_region(kml, mem, true);
        }

        /* register suffix slot */
        if (old.start_addr + old.memory_size > start_addr + size) {
            ram_addr_t size_delta;

            mem = kvm_alloc_slot(kml);
            mem->start_addr = start_addr + size;
            size_delta = mem->start_addr - old.start_addr;
            mem->memory_size = old.memory_size - size_delta;
            mem->ram = old.ram + size_delta;
            mem->flags = kvm_mem_flags(mr);

            err = kvm_set_user_memory_region(kml, mem, true);
        }
    }

    /* in case the KVM bug workaround already "consumed" the new slot */
    if (!size) {
        return;
    }
    if (!add) {
        return;
    }
    mem = kvm_alloc_slot(kml);
    mem->memory_size = size;
    mem->start_addr = start_addr;
    mem->ram = ram;
    mem->flags = kvm_mem_flags(mr);

    err = kvm_set_user_memory_region(kml, mem, true);
}

static KVMSlot *kvm_alloc_slot(KVMMemoryListener *kml)
{
    KVMSlot *slot = kvm_get_free_slot(kml);

    if (slot) {
        return slot;
    }
}

static int kvm_set_user_memory_region(KVMMemoryListener *kml, KVMSlot *slot, bool new)
{
    int ret;
    KVMState *s = kvm_state;
    struct kvm_userspace_memory_region mem;

    mem.slot = slot->slot | (kml->as_id << 16);
    mem.guest_phys_addr = slot->start_addr;
    mem.userspace_addr = (unsigned long)slot->ram;
    mem.flags = slot->flags;

    if (slot->memory_size && !new && (mem.flags ^ slot->old_flags) & KVM_MEM_READONLY) {
        /* Set the slot size to 0 before setting the slot to the desired
         * value. This is needed based on KVM commit 75d61fbc. */
        mem.memory_size = 0;
        kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
    }
    mem.memory_size = slot->memory_size;
    ret = kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
    slot->old_flags = mem.flags;

    return ret;
}

int kvm_vm_ioctl(KVMState *s, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    trace_kvm_vm_ioctl(type, arg);
    ret = ioctl(s->vmfd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}

