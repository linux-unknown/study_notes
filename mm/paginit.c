struct mem_section {
	/*
	 * This is, logically, a pointer to an array of struct
	 * pages.  However, it is stored with some other magic.
	 * (see sparse.c::sparse_init_one_section())
	 *
	 * Additionally during early boot we encode node id of
	 * the location of the section here to guide allocation.
	 * (see sparse.c::memory_present())
	 *
	 * Making it a UL at least makes someone do a cast
	 * before using it wrong.
	 */
	unsigned long section_mem_map;

	/* See declaration of similar field in struct zone */
	unsigned long *pageblock_flags;
#ifdef CONFIG_PAGE_EXTENSION
	/*
	 * If !SPARSEMEM, pgdat doesn't have page_ext pointer. We use
	 * section. (see page_ext.h about this.)
	 */
	struct page_ext *page_ext;
	unsigned long pad;
#endif
	/*
	 * WARNING: mem_section must be a power-of-2 in size for the
	 * calculation and use of SECTION_ROOT_MASK to make sense.
	 */
};

void __init paging_init(void)
{
	void *zero_page;

	map_mem();
	fixup_executable();

	/* allocate the zero page. */
	zero_page = early_alloc(PAGE_SIZE);

	bootmem_init();

	empty_zero_page = virt_to_page(zero_page);

	/*
	 * TTBR0 is only used for the identity mapping at this stage. Make it
	 * point to zero page to avoid speculatively fetching new entries.
	 */
	cpu_set_reserved_ttbr0();
	flush_tlb_all();
}

static void __init map_mem(void)
{
	struct memblock_region *reg;
	phys_addr_t limit;

	/*
	 * Temporarily limit the memblock range. We need to do this as
	 * create_mapping requires puds, pmds and ptes to be allocated from
	 * memory addressable from the initial direct kernel mapping.
	 *
	 * The initial direct kernel mapping, located at swapper_pg_dir, gives
	 * us PUD_SIZE (4K pages) or PMD_SIZE (64K pages) memory starting from
	 * PHYS_OFFSET (which must be aligned to 2MB as per
	 * Documentation/arm64/booting.txt).
	 */

	limit = PHYS_OFFSET + PUD_SIZE;
	memblock_set_current_limit(limit);

	/* map all the memory banks */
	for_each_memblock(memory, reg) {
		phys_addr_t start = reg->base;
		phys_addr_t end = start + reg->size;

		if (start >= end)
			break;

#ifndef CONFIG_ARM64_64K_PAGES
		/*
		 * For the first memory bank align the start address and
		 * current memblock limit to prevent create_mapping() from
		 * allocating pte page tables from unmapped memory.
		 * When 64K pages are enabled, the pte page table for the
		 * first PGDIR_SIZE is already present in swapper_pg_dir.
		 */
		if (start < limit)
			start = ALIGN(start, PMD_SIZE);
		if (end < limit) {
			limit = end & PMD_MASK;
			memblock_set_current_limit(limit);
		}
#endif
		/* 为memblock.memory中的region进行页表映射，而且是线性映射 */
		__map_memblock(start, end);
	}

	/* Limit no longer required. */
	memblock_set_current_limit(MEMBLOCK_ALLOC_ANYWHERE);
}

static void __init __map_memblock(phys_addr_t start, phys_addr_t end)
{
	create_mapping(start, __phys_to_virt(start), end - start,
			PAGE_KERNEL_EXEC);
}

void __init bootmem_init(void)
{
	unsigned long min, max;

	min = PFN_UP(memblock_start_of_DRAM());
	max = PFN_DOWN(memblock_end_of_DRAM());

	/*
	 * Sparsemem(稀疏内存) tries to allocate bootmem in memory_present(), so must be
	 * done after the fixed reservations.
	 */
	arm64_memory_present();

	sparse_init();
	zone_sizes_init(min, max);

	high_memory = __va((max << PAGE_SHIFT) - 1) + 1;
	max_pfn = max_low_pfn = max;
}


struct mem_section *mem_section[NR_SECTION_ROOTS];


static void arm64_memory_present(void)
{
	struct memblock_region *reg;

	for_each_memblock(memory, reg){
		/* 对内存探测得出的每个region调用memory_present() */
		memory_present(0, memblock_region_memory_base_pfn(reg),
			       memblock_region_memory_end_pfn(reg));
	}
}

/**
 * memblock_region_memory_base_pfn - Return the lowest pfn intersecting with the memory region
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_memory_base_pfn(const struct memblock_region *reg)
{
	return PFN_UP(reg->base);
}

/**
 * memblock_region_memory_end_pfn - Return the end_pfn this region
 * @reg: memblock_region structure
 */
static inline unsigned long memblock_region_memory_end_pfn(const struct memblock_region *reg)
{
	return PFN_DOWN(reg->base + reg->size);
}






#ifdef CONFIG_SPARSEMEM
#define MAX_PHYSMEM_BITS	48
#define SECTION_SIZE_BITS	30
#endif

#define PAGE_SHIFT		12

#define PA_SECTION_SHIFT	(SECTION_SIZE_BITS)
#define PFN_SECTION_SHIFT	(SECTION_SIZE_BITS - PAGE_SHIFT) /* 30 - 12 = 18 */

#define pfn_to_section_nr(pfn) ((pfn) >> PFN_SECTION_SHIFT) /* 18 */

/* 一个section中包含的页 */
#define PAGES_PER_SECTION       (1UL << PFN_SECTION_SHIFT)


#define PAGE_SIZE		(_AC(1,UL) << PAGE_SHIFT)

/* 一个root mem_section中有多少个子mem_section */
#define SECTIONS_PER_ROOT       (PAGE_SIZE / sizeof (struct mem_section))

#define SECTION_NR_TO_ROOT(sec)	((sec) / SECTIONS_PER_ROOT)


/* SECTION_SHIFT	#bits space required to store a section # */
#define SECTIONS_SHIFT	(MAX_PHYSMEM_BITS - SECTION_SIZE_BITS) /* 48 - 30 = 18*/


#define NR_MEM_SECTIONS		(1UL << SECTIONS_SHIFT)

/* NR_MEM_SECTIONS * SECTIONS_PER_ROOT */
#define NR_SECTION_ROOTS	DIV_ROUND_UP(NR_MEM_SECTIONS, SECTIONS_PER_ROOT)

#define SECTION_NID_SHIFT	2

#define	SECTION_MARKED_PRESENT	(1UL<<0)

/**
 * pfn : addr >>  PAGE_SHIFT
 * section_nr: addr >> (PFN_SECTION_SHIFT + PAGE_SHIFT)
 *
 */
void __init memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;

	start &= PAGE_SECTION_MASK;
	mminit_validate_memmodel_limits(&start, &end);
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		unsigned long section = pfn_to_section_nr(pfn);
		struct mem_section *ms;

		sparse_index_init(section, nid);
		set_section_nid(section, nid);
		/* ms为root mem_setcion中对应section的的mem_section */
		ms = __nr_to_section(section);
		if (!ms->section_mem_map)
			/* 表示section已经存在 */
			ms->section_mem_map = sparse_encode_early_nid(nid) |
							SECTION_MARKED_PRESENT;
	}
}

static u8 section_to_node_table[NR_MEM_SECTIONS];

static void set_section_nid(unsigned long section_nr, int nid)
{
	section_to_node_table[section_nr] = nid;
}

/*
 * During early boot, before section_mem_map is used for an actual
 * mem_map, we use section_mem_map to store the section's NUMA
 * node.  This keeps us from having to use another data structure.  The
 * node information is cleared just before we store the real mem_map.
 */
static inline unsigned long sparse_encode_early_nid(int nid)
{
	return (nid << SECTION_NID_SHIFT);
}


static inline struct mem_section *__nr_to_section(unsigned long nr)
{
	/* root mem_section是否为空，该值指向子mem_section */
	if (!mem_section[SECTION_NR_TO_ROOT(nr)])
		return NULL;
	/* mem_section为一个指针数组，每一个元素都是一个 mem_section类型的指针
	 * mem_section[SECTION_NR_TO_ROOT(nr)][nr & SECTION_ROOT_MASK]可以分开看
	 * mem_section[SECTION_NR_TO_ROOT(nr)]表示一个mem_section的指针，该指针是在sparse_index_init中赋值的
	 * mem_section[SECTION_NR_TO_ROOT(nr)][nr & SECTION_ROOT_MASK]表示一个mem_section元素
	 * 二维数组的第二维不一定是连续的，只有定义的时候才是连续的，数组和指针的关系
	 */
	return &mem_section[SECTION_NR_TO_ROOT(nr)][nr & SECTION_ROOT_MASK];
}



static int __meminit sparse_index_init(unsigned long section_nr, int nid)
{
	/* 
	 * section_nr / SECTIONS_PER_ROOT
	 */
	unsigned long root = SECTION_NR_TO_ROOT(section_nr);
	struct mem_section *section;

	if (mem_section[root])
		return -EEXIST;

	section = sparse_index_alloc(nid);
	if (!section)
		return -ENOMEM;

	mem_section[root] = section;

	return 0;
}

static struct mem_section noinline __init_refok *sparse_index_alloc(int nid)
{
	struct mem_section *section = NULL;
	unsigned long array_size = SECTIONS_PER_ROOT * sizeof(struct mem_section);

	if (slab_is_available()) {
		if (node_state(nid, N_HIGH_MEMORY))
			section = kzalloc_node(array_size, GFP_KERNEL, nid);
		else
			section = kzalloc(array_size, GFP_KERNEL);
	} else {
		section = memblock_virt_alloc_node(array_size, nid);
	}

	return section;
}


#define PMD_SHIFT		((PAGE_SHIFT - 3) * 2 + 3) /* (12 - 3)*2 + 3 = 21*/

#define HPAGE_SHIFT		PMD_SHIFT	/*21*/

#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT) /*21 - 12 = 9*/

#define pageblock_order		HUGETLB_PAGE_ORDER /*9*/

#define section_nr_to_pfn(sec) ((sec) << PFN_SECTION_SHIFT)


void __init sparse_init(void)
{
	unsigned long pnum;
	struct page *map;
	unsigned long *usemap;
	unsigned long **usemap_map;
	int size;


	/* see include/linux/mmzone.h 'struct mem_section' definition */
	BUILD_BUG_ON(!is_power_of_2(sizeof(struct mem_section)));

	/* Setup pageblock_order for HUGETLB_PAGE_SIZE_VARIABLE 
	 * HUGETLB_PAGE_SIZE_VARIABLE没有定义，set_pageblock_order为空
	 */
	set_pageblock_order();

	/*
	 * map is using big page (aka 2M in x86 64 bit)
	 * usemap is less one page (aka 24 bytes)
	 * so alloc 2M (with 2M align) and 24 bytes in turn will
	 * make next 2M slip to one more 2M later.
	 * then in big system, the memory will have a lot of holes...
	 * here try to allocate 2M pages continuously.
	 *
	 * powerpc need to call sparse_init_one_section right after each
	 * sparse_early_mem_map_alloc, so allocate usemap_map at first.
	 */
	size = sizeof(unsigned long *) * NR_MEM_SECTIONS;
	usemap_map = memblock_virt_alloc(size, 0);

	/* 为每一个node 分配usemap*/
	alloc_usemap_and_memmap(sparse_early_usemaps_alloc_node,
							(void *)usemap_map);


	for (pnum = 0; pnum < NR_MEM_SECTIONS; pnum++) {
		/* 非present的section跳过 */
		if (!present_section_nr(pnum))
			continue;

		usemap = usemap_map[pnum];
		if (!usemap)
			continue;

		/* pnum secton对应的虚拟地址 */
		map = sparse_early_mem_map_alloc(pnum);
		if (!map)
			continue;

		sparse_init_one_section(__nr_to_section(pnum), pnum, map,
								usemap);
	}

	vmemmap_populate_print_last();

	memblock_free_early(__pa(usemap_map), size);
}

/**
 *  alloc_usemap_and_memmap - memory alloction for pageblock flags and vmemmap
 *  @map: usemap_map for pageblock flags or mmap_map for vmemmap
 */
static void __init alloc_usemap_and_memmap(void (*alloc_func)
					(void *, unsigned long, unsigned long,
					unsigned long, int), void *data)
{
	unsigned long pnum;
	unsigned long map_count;
	int nodeid_begin = 0;
	unsigned long pnum_begin = 0;

	for (pnum = 0; pnum < NR_MEM_SECTIONS; pnum++) {
		struct mem_section *ms;
		/* 对于不存在内存的section，直接跳过 */
		if (!present_section_nr(pnum))
			continue;
		ms = __nr_to_section(pnum);
		nodeid_begin = sparse_early_nid(ms);
		pnum_begin = pnum;
		break;
	}
	/* 退出循环表示找到一个存在内存的section */
	map_count = 1;
	for (pnum = pnum_begin + 1; pnum < NR_MEM_SECTIONS; pnum++) {
		struct mem_section *ms;
		int nodeid;

		if (!present_section_nr(pnum))
			continue;
		/* 处理已经存在的mem_section */
		ms = __nr_to_section(pnum);
		nodeid = sparse_early_nid(ms);
		if (nodeid == nodeid_begin) {
			/* map_count表示每个node id映射的个数 */
			map_count++;
			continue;
		}
		/* 走到这里表示该section已经属于另外一个node                 */
		/* ok, we need to take cake of from pnum_begin to pnum - 1*/
		alloc_func(data, pnum_begin, pnum, map_count, nodeid_begin);
		/* new start, update count etc*/
		nodeid_begin = nodeid;
		pnum_begin = pnum;
		map_count = 1;
	}
	/* ok, last chunk */
	alloc_func(data, pnum_begin, NR_MEM_SECTIONS, map_count, nodeid_begin);
}

static inline int present_section_nr(unsigned long nr)
{
	return present_section(__nr_to_section(nr));
}

static inline int present_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_MARKED_PRESENT));
}

static inline int sparse_early_nid(struct mem_section *section)
{
	return (section->section_mem_map >> SECTION_NID_SHIFT);
}

	/* Bit indices that affect a whole block of pages */
enum pageblock_bits {
	PB_migrate,
	PB_migrate_end = PB_migrate + 3 - 1,
			/* 3 bits required for migrate types */
	PB_migrate_skip,/* If set the block is skipped by compaction */

	/*
	 * Assume the bits will always align on a word. If this assumption
	 * changes then get/set pageblock needs updating.
	 */
	NR_PAGEBLOCK_BITS
};


#define SECTION_BLOCKFLAGS_BITS \
	((1UL << (PFN_SECTION_SHIFT - pageblock_order)) * NR_PAGEBLOCK_BITS)


unsigned long usemap_size(void)
{
	unsigned long size_bytes;
	size_bytes = roundup(SECTION_BLOCKFLAGS_BITS, 8) / 8;
	size_bytes = roundup(size_bytes, sizeof(unsigned long));
	return size_bytes;
}
struct pglist_data  contig_page_data;

#define NODE_DATA(nid)		(&contig_page_data)

/* pnum_begin:该node    id，开始的section     nr，pnum_end： 该node id对应的结束section nr*/
static void __init sparse_early_usemaps_alloc_node(void *data,
				 unsigned long pnum_begin,
				 unsigned long pnum_end,
				 unsigned long usemap_count, int nodeid)
{
	void *usemap;
	unsigned long pnum;
	unsigned long **usemap_map = (unsigned long **)data;
	int size = usemap_size();
	/* 分配usemap */
	usemap = sparse_early_usemaps_alloc_pgdat_section(NODE_DATA(nodeid),
							  size * usemap_count);


	for (pnum = pnum_begin; pnum < pnum_end; pnum++) {
		/* 不存在内存的section，跳过 */
		if (!present_section_nr(pnum))
			continue;
		usemap_map[pnum] = usemap;
		usemap += size;
		/* 空函数 */
		check_usemap_section_nr(nodeid, usemap_map[pnum]);
	}
}

static unsigned long * __init
sparse_early_usemaps_alloc_pgdat_section(struct pglist_data *pgdat,
					 unsigned long size)
{
	return memblock_virt_alloc_node_nopanic(size, pgdat->node_id);
}
					 
static inline void * __init memblock_virt_alloc_node_nopanic(
						phys_addr_t size, int nid)
{
	return memblock_virt_alloc_try_nid_nopanic(size, 0, BOOTMEM_LOW_LIMIT,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    nid);
}


/**
 * memblock_virt_alloc_try_nid_nopanic - allocate boot memory block
 * @size: size of memory block to be allocated in bytes
 * @align: alignment of the region and block's size
 * @min_addr: the lower bound of the memory region from where the allocation
 *	  is preferred (phys address)
 * @max_addr: the upper bound of the memory region from where the allocation
 *	      is preferred (phys address), or %BOOTMEM_ALLOC_ACCESSIBLE to
 *	      allocate only from memory limited by memblock.current_limit value
 * @nid: nid of the free area to find, %NUMA_NO_NODE for any node
 *
 * Public version of _memblock_virt_alloc_try_nid_nopanic() which provides
 * additional debug information (including caller info), if enabled.
 *
 * RETURNS:
 * Virtual address of allocated memory block on success, NULL on failure.
 */
void * __init memblock_virt_alloc_try_nid_nopanic(
				phys_addr_t size, phys_addr_t align,
				phys_addr_t min_addr, phys_addr_t max_addr,
				int nid)
{
	return memblock_virt_alloc_internal(size, align, min_addr,
					     max_addr, nid);
}

/**
 * memblock_virt_alloc_internal - allocate boot memory block
 * @size: size of memory block to be allocated in bytes
 * @align: alignment of the region and block's size
 * @min_addr: the lower bound of the memory region to allocate (phys address)
 * @max_addr: the upper bound of the memory region to allocate (phys address)
 * @nid: nid of the free area to find, %NUMA_NO_NODE for any node
 *
 * The @min_addr limit is dropped if it can not be satisfied and the allocation
 * will fall back to memory below @min_addr. Also, allocation may fall back
 * to any node in the system if the specified node can not
 * hold the requested memory.
 *
 * The allocation is performed from memory region limited by
 * memblock.current_limit if @max_addr == %BOOTMEM_ALLOC_ACCESSIBLE.
 *
 * The memory block is aligned on SMP_CACHE_BYTES if @align == 0.
 *
 * The phys address of allocated boot memory block is converted to virtual and
 * allocated memory is reset to 0.
 *
 * In addition, function sets the min_count to 0 using kmemleak_alloc for
 * allocated boot memory block, so that it is never reported as leaks.
 *
 * RETURNS:
 * Virtual address of allocated memory block on success, NULL on failure.
 */
static void * __init memblock_virt_alloc_internal(
				phys_addr_t size, phys_addr_t align,
				phys_addr_t min_addr, phys_addr_t max_addr,
				int nid)
{
	phys_addr_t alloc;
	void *ptr;

	if (WARN_ONCE(nid == MAX_NUMNODES, "Usage of MAX_NUMNODES is deprecated. Use NUMA_NO_NODE instead\n"))
		nid = NUMA_NO_NODE;

	/*
	 * Detect any accidental use of these APIs after slab is ready, as at
	 * this moment memblock may be deinitialized already and its
	 * internal data may be destroyed (after execution of free_all_bootmem)
	 */
	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, nid);

	if (!align)
		align = SMP_CACHE_BYTES;

	if (max_addr > memblock.current_limit)
		max_addr = memblock.current_limit;

again:
	alloc = memblock_find_in_range_node(size, align, min_addr, max_addr,
					    nid);
	if (alloc)
		goto done;

	if (nid != NUMA_NO_NODE) {
		alloc = memblock_find_in_range_node(size, align, min_addr,
						    max_addr,  NUMA_NO_NODE);
		if (alloc)
			goto done;
	}

	if (min_addr) {
		min_addr = 0;
		goto again;
	} else {
		goto error;
	}

done:
	memblock_reserve(alloc, size);
	ptr = phys_to_virt(alloc);
	memset(ptr, 0, size);

	/*
	 * The min_count is set to 0 so that bootmem allocated blocks
	 * are never reported as leaks. This is because many of these blocks
	 * are only referred via the physical address which is not
	 * looked up by kmemleak.
	 */
	kmemleak_alloc(ptr, size, 0, 0);

	return ptr;

error:
	return NULL;
}

static struct page __init *sparse_early_mem_map_alloc(unsigned long pnum)
{
	struct page *map;
	struct mem_section *ms = __nr_to_section(pnum);
	int nid = sparse_early_nid(ms);

	map = sparse_mem_map_populate(pnum, nid);
	if (map)
		return map;

	printk(KERN_ERR "%s: sparsemem memory map backing failed "
			"some memory will not be available.\n", __func__);
	ms->section_mem_map = 0;
	return NULL;
}

#define vmemmap			((struct page *)(VMALLOC_END + SZ_64K))

/* memmap is virtually contiguous.  */
#define __pfn_to_page(pfn)	(vmemmap + (pfn))
#define __page_to_pfn(page)	(unsigned long)((page) - vmemmap)


struct page * __meminit sparse_mem_map_populate(unsigned long pnum, int nid)
{
	unsigned long start;
	unsigned long end;
	struct page *map;

	map = pfn_to_page(pnum * PAGES_PER_SECTION);
	start = (unsigned long)map;
	end = (unsigned long)(map + PAGES_PER_SECTION);
	/* 创建页表，只创建到了pmd */
	if (vmemmap_populate(start, end, nid))
		return NULL;

	return map;
}

int __meminit vmemmap_populate(unsigned long start, unsigned long end, int node)
{
	unsigned long addr = start;
	unsigned long next;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	do {
		next = pmd_addr_end(addr, end);

		pgd = vmemmap_pgd_populate(addr, node);
		if (!pgd)
			return -ENOMEM;

		pud = vmemmap_pud_populate(pgd, addr, node);
		if (!pud)
			return -ENOMEM;

		pmd = pmd_offset(pud, addr);
		if (pmd_none(*pmd)) {
			void *p = NULL;

			p = vmemmap_alloc_block_buf(PMD_SIZE, node);
			if (!p)
				return -ENOMEM;

			set_pmd(pmd, __pmd(__pa(p) | PROT_SECT_NORMAL));
		} else
			vmemmap_verify((pte_t *)pmd, node, addr, next);
	} while (addr = next, addr != end);

	return 0;
}

static int __meminit sparse_init_one_section(struct mem_section *ms,
		unsigned long pnum, struct page *mem_map,
		unsigned long *pageblock_bitmap)
{
	if (!present_section(ms))
		return -EINVAL;

	ms->section_mem_map &= ~SECTION_MAP_MASK;
	ms->section_mem_map |= sparse_encode_mem_map(mem_map, pnum) | SECTION_HAS_MEM_MAP;
 	ms->pageblock_flags = pageblock_bitmap;

	return 1;
}

/*
 * Subtle, we encode the real pfn into the mem_map such that
 * the identity pfn - section_mem_map will return the actual
 * physical page frame number.
 */
static unsigned long sparse_encode_mem_map(struct page *mem_map, unsigned long pnum)
{
	return (unsigned long)(mem_map - (section_nr_to_pfn(pnum)));
}

