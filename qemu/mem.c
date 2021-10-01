void memory_region_init(MemoryRegion *mr,
                        Object *owner,
                        const char *name,
                        uint64_t size)
{
    object_initialize(mr, sizeof(*mr), TYPE_MEMORY_REGION);
    mr->size = int128_make64(size);
    if (size == UINT64_MAX) {
        mr->size = int128_2_64();
    }
    mr->name = g_strdup(name);
    mr->owner = owner;
    mr->ram_block = NULL;

    if (name) {
        char *escaped_name = memory_region_escape_name(name);
        char *name_array = g_strdup_printf("%s[*]", escaped_name);

        if (!owner) {
            owner = container_get(qdev_get_machine(), "/unattached");
        }

        object_property_add_child(owner, name_array, OBJECT(mr), &error_abort);
        object_unref(OBJECT(mr));
        g_free(name_array);
        g_free(escaped_name);
    }
}

void memory_region_init_alias(MemoryRegion *mr,
                              Object *owner,
                              const char *name,
                              MemoryRegion *orig,
                              hwaddr offset,
                              uint64_t size)
{
    memory_region_init(mr, owner, name, size);
    mr->alias = orig;
    mr->alias_offset = offset;
}

struct PhysPageEntry {
    /* How many bits skip to next level (in units of L2_SIZE). 0 for a leaf. */
    uint32_t skip : 6;
     /* index into phys_sections (!skip) or phys_map_nodes (skip) */
    uint32_t ptr : 26;
};

struct AddressSpaceDispatch {
    struct rcu_head rcu;

    MemoryRegionSection *mru_section;
    /* This is a multi-level map on the physical address space.
     * The bottom level has pointers to MemoryRegionSections.
     */
    PhysPageEntry phys_map;
    PhysPageMap map;
    AddressSpace *as;
};

typedef struct PhysPageMap {
    struct rcu_head rcu;

    unsigned sections_nb;
    unsigned sections_nb_alloc;
    unsigned nodes_nb;
    unsigned nodes_nb_alloc;
    Node *nodes;
    MemoryRegionSection *sections;
} PhysPageMap;

/**
 * MemoryRegionSection: describes a fragment of a #MemoryRegion
 *
 * @mr: the region, or %NULL if empty
 * @address_space: the address space the region is mapped in
 * @offset_within_region: the beginning of the section, relative to @mr's start
 * @size: the size of the section; will not exceed @mr's boundaries
 * @offset_within_address_space: the address of the first byte of the section
 *     relative to the region's address space
 * @readonly: writes to this section are ignored
 */
struct MemoryRegionSection {
    MemoryRegion *mr;
    AddressSpace *address_space;
    hwaddr offset_within_region;
    Int128 size;
    hwaddr offset_within_address_space;
    bool readonly;
};

#define PHYS_MAP_NODE_NIL (((uint32_t)~0) >> 6)


void address_space_init(AddressSpace *as, MemoryRegion *root, const char *name)
{
    memory_region_ref(root);
    memory_region_transaction_begin();
    as->ref_count = 1;
    as->root = root;
    as->malloced = false;
    as->current_map = g_new(FlatView, 1);
    flatview_init(as->current_map);
    as->ioeventfd_nb = 0;
    as->ioeventfds = NULL;
	/* 将as的 address_spaces_link 字段添加到 address_spaces 链表中 */
    QTAILQ_INSERT_TAIL(&address_spaces, as, address_spaces_link);
    as->name = g_strdup(name ? name : "anonymous");
    address_space_init_dispatch(as);
    memory_region_update_pending |= root->enabled;
    memory_region_transaction_commit();
}

void memory_region_transaction_begin(void)
{
    qemu_flush_coalesced_mmio_buffer();
    ++memory_region_transaction_depth;
}

static void flatview_init(FlatView *view)
{
    view->ref = 1;
    view->ranges = NULL;
    view->nr = 0;
    view->nr_allocated = 0;
}

static uint16_t phys_section_add(PhysPageMap *map, MemoryRegionSection *section)
{
    /* The physical section number is ORed with a page-aligned
     * pointer to produce the iotlb entries.  Thus it should
     * never overflow into the page-aligned value.
     */
    assert(map->sections_nb < TARGET_PAGE_SIZE);

    if (map->sections_nb == map->sections_nb_alloc) {
        map->sections_nb_alloc = MAX(map->sections_nb_alloc * 2, 16);
		/* 申请  map->sections_nb_alloc 个 MemoryRegionSection 内存的起始为 map->sections        */
        map->sections = g_renew(MemoryRegionSection, map->sections, map->sections_nb_alloc);
    }
	/* 将 section 保存到 map->sections[map->sections_nb] 数组中 */
    map->sections[map->sections_nb] = *section;
    memory_region_ref(section->mr);
    return map->sections_nb++;
}

static uint16_t dummy_section(PhysPageMap *map, AddressSpace *as,
                              MemoryRegion *mr)
{
    assert(as);
    MemoryRegionSection section = {
        .address_space = as,
        .mr = mr,
        .offset_within_address_space = 0,
        .offset_within_region = 0,
        .size = int128_2_64(),
    };

    return phys_section_add(map, &section);
}

MemoryRegion io_mem_rom, io_mem_notdirty;
static MemoryRegion io_mem_unassigned;
static MemoryRegion io_mem_watch;

static void mem_begin(MemoryListener *listener)
{
    AddressSpace *as = container_of(listener, AddressSpace, dispatch_listener);
	/* 创建 AddressSpaceDispatch */
    AddressSpaceDispatch *d = g_new0(AddressSpaceDispatch, 1);
    uint16_t n;

    n = dummy_section(&d->map, as, &io_mem_unassigned);
    assert(n == PHYS_SECTION_UNASSIGNED);
    n = dummy_section(&d->map, as, &io_mem_notdirty);
    assert(n == PHYS_SECTION_NOTDIRTY);
    n = dummy_section(&d->map, as, &io_mem_rom);
    assert(n == PHYS_SECTION_ROM);
    n = dummy_section(&d->map, as, &io_mem_watch);
    assert(n == PHYS_SECTION_WATCH);

    d->phys_map  = (PhysPageEntry) { .ptr = PHYS_MAP_NODE_NIL, .skip = 1 };
    d->as = as;
    as->next_dispatch = d;
}

static void mem_add(MemoryListener *listener, MemoryRegionSection *section)
{
    AddressSpace *as = container_of(listener, AddressSpace, dispatch_listener);
    AddressSpaceDispatch *d = as->next_dispatch;
    MemoryRegionSection now = *section, remain = *section;
    Int128 page_size = int128_make64(TARGET_PAGE_SIZE);

	/* 页对齐 */
    if (now.offset_within_address_space & ~TARGET_PAGE_MASK) {
        uint64_t left = TARGET_PAGE_ALIGN(now.offset_within_address_space)
                       - now.offset_within_address_space;

        now.size = int128_min(int128_make64(left), now.size);
        register_subpage(d, &now);
    } else {
        now.size = int128_zero();
    }
    while (int128_ne(remain.size, now.size)) {
        remain.size = int128_sub(remain.size, now.size);
        remain.offset_within_address_space += int128_get64(now.size);
        remain.offset_within_region += int128_get64(now.size);
        now = remain;
        if (int128_lt(remain.size, page_size)) {
            register_subpage(d, &now);
        } else if (remain.offset_within_address_space & ~TARGET_PAGE_MASK) {
            now.size = page_size;
            register_subpage(d, &now);
        } else {
            now.size = int128_and(now.size, int128_neg(page_size));
            register_multipage(d, &now);
        }
    }
}

static void register_subpage(AddressSpaceDispatch *d, MemoryRegionSection *section)
{
    subpage_t *subpage;
    hwaddr base = section->offset_within_address_space & TARGET_PAGE_MASK;
	/* 查看section中的地址是否覆盖了 */
    MemoryRegionSection *existing = phys_page_find(d->phys_map, base, d->map.nodes, d->map.sections);
    MemoryRegionSection subsection = {
        .offset_within_address_space = base,
        .size = int128_make64(TARGET_PAGE_SIZE),
    };

    hwaddr start, end;

    assert(existing->mr->subpage || existing->mr == &io_mem_unassigned);

    if (!(existing->mr->subpage)) {
        subpage = subpage_init(d->as, base);
        subsection.address_space = d->as;
        subsection.mr = &subpage->iomem;
		/* 将section添加到qemu自己的页表中           */
        phys_page_set(d, base >> TARGET_PAGE_BITS, 1, phys_section_add(&d->map, &subsection));
    } else {
        subpage = container_of(existing->mr, subpage_t, iomem);
    }
    start = section->offset_within_address_space & ~TARGET_PAGE_MASK;
    end = start + int128_get64(section->size) - 1;
    subpage_register(subpage, start, end, phys_section_add(&d->map, section));
}

#define SUBPAGE_IDX(addr) ((addr) & ~TARGET_PAGE_MASK)

static int subpage_register (subpage_t *mmio, uint32_t start, uint32_t end,
                             uint16_t section)
{
    int idx, eidx;

    if (start >= TARGET_PAGE_SIZE || end >= TARGET_PAGE_SIZE)
        return -1;
    idx = SUBPAGE_IDX(start);
    eidx = SUBPAGE_IDX(end);
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p start %08x end %08x idx %08x eidx %08x section %d\n",
           __func__, mmio, start, end, idx, eidx, section);
#endif
	/* 将section添加到    sub_section中 */
    for (; idx <= eidx; idx++) {
        mmio->sub_section[idx] = section;
    }

    return 0;
}

static void mem_commit(MemoryListener *listener)
{
    AddressSpace *as = container_of(listener, AddressSpace, dispatch_listener);
    AddressSpaceDispatch *cur = as->dispatch;
    AddressSpaceDispatch *next = as->next_dispatch;

    phys_page_compact_all(next, next->map.nodes_nb);

    atomic_rcu_set(&as->dispatch, next);
    if (cur) {
        call_rcu(cur, address_space_dispatch_free, rcu);
    }
}

static void phys_page_compact_all(AddressSpaceDispatch *d, int nodes_nb)
{
    DECLARE_BITMAP(compacted, nodes_nb);

    if (d->phys_map.skip) {
        phys_page_compact(&d->phys_map, d->map.nodes, compacted);
    }
}


/* Compact a non leaf page entry. Simply detect that the entry has a single child,
 * and update our entry so we can skip it and go directly to the destination.
 */
static void phys_page_compact(PhysPageEntry *lp, Node *nodes, unsigned long *compacted)
{
    unsigned valid_ptr = P_L2_SIZE;
    int valid = 0;
    PhysPageEntry *p;
    int i;

    if (lp->ptr == PHYS_MAP_NODE_NIL) {
        return;
    }

    p = nodes[lp->ptr];
    for (i = 0; i < P_L2_SIZE; i++) {
        if (p[i].ptr == PHYS_MAP_NODE_NIL) {
            continue;
        }

        valid_ptr = i;
        valid++;
        if (p[i].skip) {
            phys_page_compact(&p[i], nodes, compacted);
        }
    }

    /* We can only compress if there's only one child. */
    if (valid != 1) {
        return;
    }

    assert(valid_ptr < P_L2_SIZE);

    /* Don't compress if it won't fit in the # of bits we have. */
    if (lp->skip + p[valid_ptr].skip >= (1 << 3)) {
        return;
    }

    lp->ptr = p[valid_ptr].ptr;
    if (!p[valid_ptr].skip) {
        /* If our only child is a leaf, make this a leaf. */
        /* By design, we should have made this node a leaf to begin with so we
         * should never reach here.
         * But since it's so simple to handle this, let's do it just in case we
         * change this rule.
         */
        lp->skip = 0;
    } else {
        lp->skip += p[valid_ptr].skip;
    }
}


void address_space_init_dispatch(AddressSpace *as)
{
    as->dispatch = NULL;
    as->dispatch_listener = (MemoryListener) {
        .begin = mem_begin,
        .commit = mem_commit,
        .region_add = mem_add,
        .region_nop = mem_add,
        .priority = 0,
    };
    memory_listener_register(&as->dispatch_listener, as);
}

void memory_listener_register(MemoryListener *listener, AddressSpace *filter)
{
    MemoryListener *other = NULL;
    AddressSpace *as;

    listener->address_space_filter = filter;
    if (QTAILQ_EMPTY(&memory_listeners) || listener->priority >= QTAILQ_LAST(&memory_listeners,
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
    	/* 对 address_spaces中的每一个as */
        listener_add_address_space(listener, as);
    }
}

#define FOR_EACH_FLAT_RANGE(var, view)          \
    for (var = (view)->ranges; var < (view)->ranges + (view)->nr; ++var)

struct FlatView {
    struct rcu_head rcu;
    unsigned ref;
    FlatRange *ranges;
    unsigned nr;
    unsigned nr_allocated;
};

struct FlatRange {
    MemoryRegion *mr;
    hwaddr offset_in_region;
    AddrRange addr;
    uint8_t dirty_log_mask;
    bool romd_mode;
    bool readonly;
};


static void listener_add_address_space(MemoryListener *listener,
                                       AddressSpace *as)
{
    FlatView *view;
    FlatRange *fr;
	/* 找到litener对应的as */
    if (listener->address_space_filter
        && listener->address_space_filter != as) {
        return;
    }

    if (listener->begin) {
        listener->begin(listener);
    }
    if (global_dirty_log) {
        if (listener->log_global_start) {
            listener->log_global_start(listener);
        }
    }
	/* 返回 as->current_map   */
    view = address_space_get_flatview(as);
	/* fr即FlatRange */
    FOR_EACH_FLAT_RANGE(fr, view) {
        MemoryRegionSection section = {
            .mr = fr->mr,
            .address_space = as,
            .offset_within_region = fr->offset_in_region,
            .size = fr->addr.size,
            .offset_within_address_space = int128_get64(fr->addr.start),
            .readonly = fr->readonly,
        };
        if (fr->dirty_log_mask && listener->log_start) {
            listener->log_start(listener, &section, 0, fr->dirty_log_mask);
        }
        if (listener->region_add) {
            listener->region_add(listener, &section);
        }
    }
    if (listener->commit) {
        listener->commit(listener);
    }
    flatview_unref(view);
}

static const MemoryRegionOps subpage_ops = {
    .read_with_attrs = subpage_read,
    .write_with_attrs = subpage_write,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .valid.accepts = subpage_accepts,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static subpage_t *subpage_init(AddressSpace *as, hwaddr base)
{
    subpage_t *mmio;

    mmio = g_malloc0(sizeof(subpage_t));

    mmio->as = as;
    mmio->base = base;
    memory_region_init_io(&mmio->iomem, NULL, &subpage_ops, mmio, NULL, TARGET_PAGE_SIZE);
    mmio->iomem.subpage = true;
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p base " TARGET_FMT_plx " len %08x\n", __func__,
           mmio, base, TARGET_PAGE_SIZE);
#endif
    subpage_register(mmio, 0, TARGET_PAGE_SIZE-1, PHYS_SECTION_UNASSIGNED);

    return mmio;
}

void memory_region_init_io(MemoryRegion *mr,
                           Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
    memory_region_init(mr, owner, name, size);
    mr->ops = ops ? ops : &unassigned_mem_ops;
    mr->opaque = opaque;
    mr->terminates = true;
}

static void phys_page_set(AddressSpaceDispatch *d,
                          hwaddr index, hwaddr nb,
                          uint16_t leaf)
{
    /* Wildly overreserve - it doesn't matter much. */
    phys_map_node_reserve(&d->map, 3 * P_L2_LEVELS);

    phys_page_set_level(&d->map, &d->phys_map, &index, &nb, leaf, P_L2_LEVELS - 1);
}

static void phys_map_node_reserve(PhysPageMap *map, unsigned nodes)
{
    if (map->nodes_nb + nodes > map->nodes_nb_alloc) {
        map->nodes_nb_alloc = MAX(map->nodes_nb_alloc * 2, 16);
        map->nodes_nb_alloc = MAX(map->nodes_nb_alloc, map->nodes_nb + nodes);
        map->nodes = g_renew(Node, map->nodes, map->nodes_nb_alloc);
    }
}

static void phys_page_set_level(PhysPageMap *map, PhysPageEntry *lp,
                                hwaddr *index, hwaddr *nb, uint16_t leaf,
                                int level)
{
    PhysPageEntry *p;
    hwaddr step = (hwaddr)1 << (level * P_L2_BITS);

    if (lp->skip && lp->ptr == PHYS_MAP_NODE_NIL) {
        lp->ptr = phys_map_node_alloc(map, level == 0);
    }
    p = map->nodes[lp->ptr];
    lp = &p[(*index >> (level * P_L2_BITS)) & (P_L2_SIZE - 1)];

    while (*nb && lp < &p[P_L2_SIZE]) {
        if ((*index & (step - 1)) == 0 && *nb >= step) {
            lp->skip = 0;
            lp->ptr = leaf;
            *index += step;
            *nb -= step;
        } else {
            phys_page_set_level(map, lp, index, nb, leaf, level - 1);
        }
        ++lp;
    }
}
#define P_L2_BITS 9
#define P_L2_SIZE (1 << P_L2_BITS)

static uint32_t phys_map_node_alloc(PhysPageMap *map, bool leaf)
{
    unsigned i;
    uint32_t ret;
    PhysPageEntry e;
    PhysPageEntry *p;

    ret = map->nodes_nb++;
    p = map->nodes[ret];
    assert(ret != PHYS_MAP_NODE_NIL);
    assert(ret != map->nodes_nb_alloc);

    e.skip = leaf ? 0 : 1;
    e.ptr = leaf ? PHYS_SECTION_UNASSIGNED : PHYS_MAP_NODE_NIL;
    for (i = 0; i < P_L2_SIZE; ++i) {
        memcpy(&p[i], &e, sizeof(e));
    }
    return ret;
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

/* Render a memory topology into a list of disjoint absolute ranges. */
static FlatView *generate_memory_topology(MemoryRegion *mr)
{
    FlatView *view;

    view = g_new(FlatView, 1);
    flatview_init(view);

    if (mr) {
        render_memory_region(view, mr, int128_zero(),
                             addrrange_make(int128_zero(), int128_2_64()), false);
    }
    flatview_simplify(view);

    return view;
}

static void render_memory_region(FlatView *view,
                                 MemoryRegion *mr,
                                 Int128 base,
                                 AddrRange clip,
                                 bool readonly)
{
    MemoryRegion *subregion;
    unsigned i;
    hwaddr offset_in_region;
    Int128 remain;
    Int128 now;
    FlatRange fr;
    AddrRange tmp;

    if (!mr->enabled) {
        return;
    }

	/* base = base + mr->addr */
    int128_addto(&base, int128_make64(mr->addr));
    readonly |= mr->readonly;

    tmp = addrrange_make(base, mr->size);

	/* 是否相交，交叉 */
    if (!addrrange_intersects(tmp, clip)) {
        return;
    }
	/*  交叉 */
    clip = addrrange_intersection(tmp, clip);

    if (mr->alias) {
        int128_subfrom(&base, int128_make64(mr->alias->addr));
        int128_subfrom(&base, int128_make64(mr->alias_offset));
        render_memory_region(view, mr->alias, base, clip, readonly);
        return;
    }

    /* Render subregions in priority order. */
    QTAILQ_FOREACH(subregion, &mr->subregions, subregions_link) {
        render_memory_region(view, subregion, base, clip, readonly);
    }

    if (!mr->terminates) {
        return;
    }

    offset_in_region = int128_get64(int128_sub(clip.start, base));
    base = clip.start;
    remain = clip.size;

    fr.mr = mr;
    fr.dirty_log_mask = memory_region_get_dirty_log_mask(mr);
    fr.romd_mode = mr->romd_mode;
    fr.readonly = readonly;

    /* Render the region itself into any gaps left by the current view. */
    for (i = 0; i < view->nr && int128_nz(remain); ++i) {
        if (int128_ge(base, addrrange_end(view->ranges[i].addr))) {
            continue;
        }

        if (int128_lt(base, view->ranges[i].addr.start)) {
            now = int128_min(remain, int128_sub(view->ranges[i].addr.start, base));
            fr.offset_in_region = offset_in_region;
            fr.addr = addrrange_make(base, now);
            flatview_insert(view, i, &fr);
            ++i;
            int128_addto(&base, now);
            offset_in_region += int128_get64(now);
            int128_subfrom(&remain, now);
        }
        now = int128_sub(int128_min(int128_add(base, remain), addrrange_end(view->ranges[i].addr)), base);

        int128_addto(&base, now);
        offset_in_region += int128_get64(now);
        int128_subfrom(&remain, now);
    }

    if (int128_nz(remain)) {
        fr.offset_in_region = offset_in_region;
        fr.addr = addrrange_make(base, remain);
        flatview_insert(view, i, &fr);
    }
}

static inline void int128_addto(Int128 *a, Int128 b)
{
    *a = int128_add(*a, b);
}

static inline Int128 int128_add(Int128 a, Int128 b)
{
    uint64_t lo = a.lo + b.lo;

    /* a.lo <= a.lo + b.lo < a.lo + k (k is the base, 2^64).  Hence,
     * a.lo + b.lo >= k implies 0 <= lo = a.lo + b.lo - k < a.lo.
     * Similarly, a.lo + b.lo < k implies a.lo <= lo = a.lo + b.lo < k.
     *
     * So the carry is lo < a.lo.
     */
    return (Int128) { lo, (uint64_t)a.hi + b.hi + (lo < a.lo) };
}

static AddrRange addrrange_make(Int128 start, Int128 size)
{
    return (AddrRange) { start, size };
}

static bool addrrange_contains(AddrRange range, Int128 addr)
{
    return int128_ge(addr, range.start)
        && int128_lt(addr, addrrange_end(range));
}

static bool addrrange_intersects(AddrRange r1, AddrRange r2)
{
    return addrrange_contains(r1, r2.start)
        || addrrange_contains(r2, r1.start);
}

static AddrRange addrrange_intersection(AddrRange r1, AddrRange r2)
{
    Int128 start = int128_max(r1.start, r2.start);
    Int128 end = int128_min(addrrange_end(r1), addrrange_end(r2));
    return addrrange_make(start, int128_sub(end, start));
}

/* Insert a range into a given position.  Caller is responsible for maintaining
 * sorting order.
 */
static void flatview_insert(FlatView *view, unsigned pos, FlatRange *range)
{
    if (view->nr == view->nr_allocated) {
        view->nr_allocated = MAX(2 * view->nr, 10);
        view->ranges = g_realloc(view->ranges, view->nr_allocated * sizeof(*view->ranges));
    }
    memmove(view->ranges + pos + 1, view->ranges + pos, (view->nr - pos) * sizeof(FlatRange));
    view->ranges[pos] = *range;
    memory_region_ref(range->mr);
    ++view->nr;
}

