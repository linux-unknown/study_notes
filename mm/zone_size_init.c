typedef struct pglist_data {
	struct zone node_zones[MAX_NR_ZONES];
	struct zonelist node_zonelists[MAX_ZONELISTS];
	int nr_zones;
#ifdef CONFIG_FLAT_NODE_MEM_MAP	/* means !SPARSEMEM */
	struct page *node_mem_map;
#ifdef CONFIG_PAGE_EXTENSION
	struct page_ext *node_page_ext;
#endif
#endif
#ifndef CONFIG_NO_BOOTMEM
	struct bootmem_data *bdata;
#endif
#ifdef CONFIG_MEMORY_HOTPLUG
	/*
	 * Must be held any time you expect node_start_pfn, node_present_pages
	 * or node_spanned_pages stay constant.  Holding this will also
	 * guarantee that any pfn_valid() stays that way.
	 *
	 * pgdat_resize_lock() and pgdat_resize_unlock() are provided to
	 * manipulate node_size_lock without checking for CONFIG_MEMORY_HOTPLUG.
	 *
	 * Nests above zone->lock and zone->span_seqlock
	 */
	spinlock_t node_size_lock;
#endif
	unsigned long node_start_pfn;
	unsigned long node_present_pages; /* total number of physical pages */
	unsigned long node_spanned_pages; /* total size of physical page
					     range, including holes */
	int node_id;
	wait_queue_head_t kswapd_wait;
	wait_queue_head_t pfmemalloc_wait;
	struct task_struct *kswapd;	/* Protected by
					   mem_hotplug_begin/end() */
	int kswapd_max_order;
	enum zone_type classzone_idx;
#ifdef CONFIG_NUMA_BALANCING
	/* Lock serializing the migrate rate limiting window */
	spinlock_t numabalancing_migrate_lock;

	/* Rate limiting time interval */
	unsigned long numabalancing_migrate_next_window;

	/* Number of pages migrated during the rate limiting time interval */
	unsigned long numabalancing_migrate_nr_pages;
#endif
} pg_data_t;

void __init bootmem_init(void)
{
	unsigned long min, max;

	min = PFN_UP(memblock_start_of_DRAM());
	max = PFN_DOWN(memblock_end_of_DRAM());

	/*
	 * Sparsemem tries to allocate bootmem in memory_present(), so must be
	 * done after the fixed reservations.
	 */
	arm64_memory_present();

	sparse_init();
	zone_sizes_init(min, max);

	high_memory = __va((max << PAGE_SHIFT) - 1) + 1;
	max_pfn = max_low_pfn = max;
}

/* lowest address */
phys_addr_t __init_memblock memblock_start_of_DRAM(void)
{
	return memblock.memory.regions[0].base;
}

phys_addr_t __init_memblock memblock_end_of_DRAM(void)
{
	int idx = memblock.memory.cnt - 1;

	return (memblock.memory.regions[idx].base + memblock.memory.regions[idx].size);
}


#define MAX_NR_ZONES 3 /* __MAX_NR_ZONES	// */

enum zone_type {
#ifdef CONFIG_ZONE_DMA
	/*
	 * ZONE_DMA is used when there are devices that are not able
	 * to do DMA to all of addressable memory (ZONE_NORMAL). Then we
	 * carve out the portion of memory that is needed for these devices.
	 * The range is arch specific.
	 *
	 * Some examples
	 *
	 * Architecture		Limit
	 * ---------------------------
	 * parisc, ia64, sparc	<4G
	 * s390			<2G
	 * arm			Various
	 * alpha		Unlimited or 0-16MB.
	 *
	 * i386, x86_64 and multiple other arches
	 * 			<16M.
	 */
	ZONE_DMA,
#endif
#ifdef CONFIG_ZONE_DMA32
	/*
	 * x86_64 needs two ZONE_DMAs because it supports devices that are
	 * only able to do DMA to the lower 16M but also 32 bit devices that
	 * can only do DMA areas below 4G.
	 */
	ZONE_DMA32,
#endif
	/*
	 * Normal addressable memory is in ZONE_NORMAL. DMA operations can be
	 * performed on pages in ZONE_NORMAL if the DMA devices support
	 * transfers to all addressable memory.
	 */
	ZONE_NORMAL,
#ifdef CONFIG_HIGHMEM
	/*
	 * A memory area that is only addressable by the kernel through
	 * mapping portions into its own address space. This is for example
	 * used by i386 to allow the kernel to address the memory beyond
	 * 900MB. The kernel will set up special mappings (page
	 * table entries on i386) for each page that the kernel needs to
	 * access.
	 */
	ZONE_HIGHMEM,
#endif
	ZONE_MOVABLE,
	__MAX_NR_ZONES
};

phys_addr_t arm64_dma_phys_limit;


static void __init zone_sizes_init(unsigned long min, unsigned long max)
{
	struct memblock_region *reg;
	unsigned long zone_size[MAX_NR_ZONES], zhole_size[MAX_NR_ZONES];
	unsigned long max_dma = min;

	memset(zone_size, 0, sizeof(zone_size));

	/* 4GB maximum for 32-bit only capable devices */
	if (IS_ENABLED(CONFIG_ZONE_DMA)) {
		/* arm64_dma_phys_limit在arm64_memblock_init中会被赋值
		 * 对于arm64 来说arm64_dma_phys_limit和dram是一样的
		 */
		max_dma = PFN_DOWN(arm64_dma_phys_limit);
		zone_size[ZONE_DMA] = max_dma - min;
	}
	zone_size[ZONE_NORMAL] = max - max_dma;

	memcpy(zhole_size, zone_size, sizeof(zhole_size));

	for_each_memblock(memory, reg) {
		unsigned long start = memblock_region_memory_base_pfn(reg);
		unsigned long end = memblock_region_memory_end_pfn(reg);

		if (start >= max)
			continue;

		if (IS_ENABLED(CONFIG_ZONE_DMA) && start < max_dma) {
			unsigned long dma_end = min(end, max_dma);
			zhole_size[ZONE_DMA] -= dma_end - start;
		}

		if (end > max_dma) {
			unsigned long normal_end = min(end, max);
			unsigned long normal_start = max(start, max_dma);
			zhole_size[ZONE_NORMAL] -= normal_end - normal_start;
		}
	}

	free_area_init_node(0, zone_size, min, zhole_size);
}

void __paginginit free_area_init_node(int nid, unsigned long *zones_size,
		unsigned long node_start_pfn, unsigned long *zholes_size)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	unsigned long start_pfn = 0;
	unsigned long end_pfn = 0;

	/* pg_data_t should be reset to zero when it's allocated */
	WARN_ON(pgdat->nr_zones || pgdat->classzone_idx);

	pgdat->node_id = nid;
	pgdat->node_start_pfn = node_start_pfn;
	calculate_node_totalpages(pgdat, start_pfn, end_pfn, zones_size, zholes_size);

	alloc_node_mem_map(pgdat);

	free_area_init_core(pgdat, start_pfn, end_pfn,
			    zones_size, zholes_size);
}

static void __meminit calculate_node_totalpages(struct pglist_data *pgdat,
						unsigned long node_start_pfn,
						unsigned long node_end_pfn,
						unsigned long *zones_size,
						unsigned long *zholes_size)
{
	unsigned long realtotalpages, totalpages = 0;
	enum zone_type i;

	for (i = 0; i < MAX_NR_ZONES; i++)
		totalpages += zone_spanned_pages_in_node(pgdat->node_id, i,
							 node_start_pfn,
							 node_end_pfn,
							 zones_size);
	pgdat->node_spanned_pages = totalpages;

	realtotalpages = totalpages;
	for (i = 0; i < MAX_NR_ZONES; i++)
		realtotalpages -=
			zone_absent_pages_in_node(pgdat->node_id, i,
						  node_start_pfn, node_end_pfn,
						  zholes_size);
	pgdat->node_present_pages = realtotalpages;
	printk(KERN_DEBUG "On node %d totalpages: %lu\n", pgdat->node_id,
							realtotalpages);
}

/*
 * Return the number of pages a zone spans in a node, including holes
 * present_pages = zone_spanned_pages_in_node() - zone_absent_pages_in_node()
 */
static inline unsigned long __meminit zone_spanned_pages_in_node(int nid,
					unsigned long zone_type,
					unsigned long node_start_pfn,
					unsigned long node_end_pfn,
					unsigned long *zones_size)
{
	return zones_size[zone_type];
}

/* Return the number of page frames in holes in a zone on a node */
static inline unsigned long __meminit zone_absent_pages_in_node(int nid,
						unsigned long zone_type,
						unsigned long node_start_pfn,
						unsigned long node_end_pfn,
						unsigned long *zholes_size)
{
	if (!zholes_size)
		return 0;

	return zholes_size[zone_type];
}

static void __init_refok alloc_node_mem_map(struct pglist_data *pgdat)
{
	/* Skip empty nodes */
	if (!pgdat->node_spanned_pages)
		return;
}

/*
 * Set up the zone data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 *
 * NOTE: pgdat should get zeroed by caller.
 */
static void __paginginit free_area_init_core(struct pglist_data *pgdat,
		unsigned long node_start_pfn, unsigned long node_end_pfn,
		unsigned long *zones_size, unsigned long *zholes_size)
{
	enum zone_type j;
	int nid = pgdat->node_id;
	unsigned long zone_start_pfn = pgdat->node_start_pfn;
	int ret;

	/* 没有定义CONFIG_MEMORY_HOTPLUG，该函数为空 */
	pgdat_resize_init(pgdat);
	init_waitqueue_head(&pgdat->kswapd_wait);
	init_waitqueue_head(&pgdat->pfmemalloc_wait);
	/* 定义了CONFIG_SPARSEMEM，该函数为空函数 */
	pgdat_page_ext_init(pgdat);

	for (j = 0; j < MAX_NR_ZONES; j++) {
		struct zone *zone = pgdat->node_zones + j;
		unsigned long size, realsize, freesize, memmap_pages;

		size = zone_spanned_pages_in_node(nid, j, node_start_pfn,
						  node_end_pfn, zones_size);
		realsize = freesize = size - zone_absent_pages_in_node(nid, j,
								node_start_pfn,
								node_end_pfn,
								zholes_size);

		/*
		 * Adjust freesize so that it accounts for how much memory
		 * is used by this zone for memmap. This affects the watermark
		 * and per-cpu initialisations
		 */
		memmap_pages = calc_memmap_size(size, realsize);
		if (!is_highmem_idx(j)) {
			if (freesize >= memmap_pages) {
				freesize -= memmap_pages;
				if (memmap_pages)
					printk(KERN_DEBUG
					       "  %s zone: %lu pages used for memmap\n",
					       zone_names[j], memmap_pages);
			} else
				printk(KERN_WARNING
					"  %s zone: %lu pages exceeds freesize %lu\n",
					zone_names[j], memmap_pages, freesize);
		}

		/* Account for reserved pages */
		if (j == 0 && freesize > dma_reserve) {
			freesize -= dma_reserve;
			printk(KERN_DEBUG "  %s zone: %lu pages reserved\n",
					zone_names[0], dma_reserve);
		}

		if (!is_highmem_idx(j))
			nr_kernel_pages += freesize;
		/* Charge for highmem memmap if there are enough kernel pages */
		else if (nr_kernel_pages > memmap_pages * 2)
			nr_kernel_pages -= memmap_pages;
		nr_all_pages += freesize;

		zone->spanned_pages = size;
		zone->present_pages = realsize;
		/*
		 * Set an approximate value for lowmem here, it will be adjusted
		 * when the bootmem allocator frees pages into the buddy system.
		 * And all highmem pages will be managed by the buddy system.
		 */
		zone->managed_pages = is_highmem_idx(j) ? realsize : freesize;
#ifdef CONFIG_NUMA
		zone->node = nid;
		zone->min_unmapped_pages = (freesize*sysctl_min_unmapped_ratio)
						/ 100;
		zone->min_slab_pages = (freesize * sysctl_min_slab_ratio) / 100;
#endif
		zone->name = zone_names[j];
		spin_lock_init(&zone->lock);
		spin_lock_init(&zone->lru_lock);
		zone_seqlock_init(zone);
		zone->zone_pgdat = pgdat;
		zone_pcp_init(zone);

		/* For bootup, initialized properly in watermark setup */
		mod_zone_page_state(zone, NR_ALLOC_BATCH, zone->managed_pages);

		lruvec_init(&zone->lruvec);
		if (!size)
			continue;

		set_pageblock_order();
		setup_usemap(pgdat, zone, zone_start_pfn, size);
		ret = init_currently_empty_zone(zone, zone_start_pfn,
						size, MEMMAP_EARLY);
		BUG_ON(ret);
		memmap_init(size, nid, j, zone_start_pfn);
		zone_start_pfn += size;
	}
}

