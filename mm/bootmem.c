#define INIT_MEMBLOCK_REGIONS	128
#define INIT_PHYSMEM_REGIONS	4


static struct memblock_region memblock_memory_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;
static struct memblock_region memblock_reserved_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;
#ifdef CONFIG_HAVE_MEMBLOCK_PHYS_MAP
static struct memblock_region memblock_physmem_init_regions[INIT_PHYSMEM_REGIONS] __initdata_memblock;
#endif

struct memblock_region {
	phys_addr_t base;
	phys_addr_t size;
	unsigned long flags;
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
	int nid;
#endif
};


struct memblock_type {
	unsigned long cnt;	/* number of regions */
	unsigned long max;	/* size of the allocated array */
	phys_addr_t total_size;	/* size of all regions */
	struct memblock_region *regions;
};


struct memblock {
	bool bottom_up;  /* is bottom up direction? */
	phys_addr_t current_limit;
	struct memblock_type memory;
	struct memblock_type reserved;
#ifdef CONFIG_HAVE_MEMBLOCK_PHYS_MAP
	struct memblock_type physmem;
#endif
};


struct memblock memblock __initdata_memblock = {
	.memory.regions		= memblock_memory_init_regions,
	.memory.cnt		= 1,	/* empty dummy entry */
	.memory.max		= INIT_MEMBLOCK_REGIONS,

	.reserved.regions	= memblock_reserved_init_regions,
	.reserved.cnt		= 1,	/* empty dummy entry */
	.reserved.max		= INIT_MEMBLOCK_REGIONS,

#ifdef CONFIG_HAVE_MEMBLOCK_PHYS_MAP
	.physmem.regions	= memblock_physmem_init_regions,
	.physmem.cnt		= 1,	/* empty dummy entry */
	.physmem.max		= INIT_PHYSMEM_REGIONS,
#endif

	.bottom_up		= false,
	.current_limit		= MEMBLOCK_ALLOC_ANYWHERE,
};

static phys_addr_t memory_limit = (phys_addr_t)ULLONG_MAX;

void __init arm64_memblock_init(void)
{
	memblock_enforce_memory_limit(memory_limit);

	/*
	 * Register the kernel text, kernel data, initrd, and initial
	 * pagetables with memblock.
	 * 保留kernel所占用的内存
	 */
	memblock_reserve(__pa(_text), _end - _text);
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		memblock_reserve(__virt_to_phys(initrd_start), initrd_end - initrd_start);
#endif

	early_init_fdt_scan_reserved_mem();

	/* 4GB maximum for 32-bit only capable devices */
	if (IS_ENABLED(CONFIG_ZONE_DMA))
		arm64_dma_phys_limit = max_zone_dma_phys();
	else
		arm64_dma_phys_limit = PHYS_MASK + 1;
	dma_contiguous_reserve(arm64_dma_phys_limit);

	memblock_allow_resize();
	memblock_dump_all();
}


int __init_memblock memblock_reserve(phys_addr_t base, phys_addr_t size)
{
	return memblock_reserve_region(base, size, MAX_NUMNODES, 0);
}

void __init early_init_fdt_scan_reserved_mem(void)
{
	int n;
	u64 base, size;

	if (!initial_boot_params)
		return;

	/* Reserve the dtb region 保留dtb占用的内存*/
	early_init_dt_reserve_memory_arch(__pa(initial_boot_params),
					  fdt_totalsize(initial_boot_params),
					  0);

	/* Process header /memreserve/ fields ，保留使用/memreserve/定义的内存
	 * /memreserve/定义的内存会被放到dtb一个专有的结构中进行保存
	 * reserved-memory定义的内存是放到node中的，具体的区别见：
	 * https://blog.csdn.net/kickxxx/article/details/54631535
	 */
	for (n = 0; ; n++) {
		fdt_get_mem_rsv(initial_boot_params, n, &base, &size);
		if (!size)
			break;
		early_init_dt_reserve_memory_arch(base, size, 0);
	}
	/* 扫描dts中的reserved-memory */
	of_scan_flat_dt(__fdt_scan_reserved_mem, NULL);
	fdt_init_reserved_mem();
}

static int __init_memblock memblock_reserve_region(phys_addr_t base,
						   phys_addr_t size,
						   int nid,
						   unsigned long flags)
{
	struct memblock_type *_rgn = &memblock.reserved;

	memblock_dbg("memblock_reserve: [%#016llx-%#016llx] flags %#02lx %pF\n",
		     (unsigned long long)base,
		     (unsigned long long)base + size - 1,
		     flags, (void *)_RET_IP_);
	/* 添加到reserved    memblock_type中*/
	return memblock_add_range(_rgn, base, size, nid, flags);
}




void __init memblock_enforce_memory_limit(phys_addr_t limit)
{
	phys_addr_t max_addr = (phys_addr_t)ULLONG_MAX;
	struct memblock_region *r;

	if (!limit)
		return;

	/* find out max address 
	 * 这个时候已经读取了dts中的memory信息，并且存放到了memblock.memory中
	 */
	for_each_memblock(memory, r) {
		if (limit <= r->size) {
			max_addr = r->base + limit;
			break;
		}
		limit -= r->size;
	}

	/* truncate（缩短） both memory and reserved regions */
	memblock_remove_range(&memblock.memory, max_addr,
			      (phys_addr_t)ULLONG_MAX);
	memblock_remove_range(&memblock.reserved, max_addr,
			      (phys_addr_t)ULLONG_MAX);
}


int __init_memblock memblock_remove_range(struct memblock_type *type,
					  phys_addr_t base, phys_addr_t size)
{
	int start_rgn, end_rgn;
	int i, ret;

	ret = memblock_isolate_range(type, base, size, &start_rgn, &end_rgn);
	if (ret)
		return ret;
	/* start_rgn = 0，退出 */
	for (i = end_rgn - 1; i >= start_rgn; i--)
		memblock_remove_region(type, i);
	return 0;
}

					  
static int __init_memblock memblock_isolate_range(struct memblock_type *type,
					phys_addr_t base, phys_addr_t size,
					int *start_rgn, int *end_rgn)
{
	phys_addr_t end = base + memblock_cap_size(base, &size);
	int i;

	*start_rgn = *end_rgn = 0;
	/* size = 0,直接推出 */
	if (!size)
		return 0;

	/* we'll create at most two more regions */
	while (type->cnt + 2 > type->max)
		if (memblock_double_array(type, base, size) < 0)
			return -ENOMEM;

	for (i = 0; i < type->cnt; i++) {
		struct memblock_region *rgn = &type->regions[i];
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;

		if (rbase >= end)
			break;
		if (rend <= base)
			continue;

		if (rbase < base) {
			/*
			 * @rgn intersects from below.  Split and continue
			 * to process the next region - the new top half.
			 */
			rgn->base = base;
			rgn->size -= base - rbase;
			type->total_size -= base - rbase;
			memblock_insert_region(type, i, rbase, base - rbase,
					       memblock_get_region_node(rgn),
					       rgn->flags);
		} else if (rend > end) {
			/*
			 * @rgn intersects from above.  Split and redo the
			 * current region - the new bottom half.
			 */
			rgn->base = end;
			rgn->size -= end - rbase;
			type->total_size -= end - rbase;
			memblock_insert_region(type, i--, rbase, end - rbase,
					       memblock_get_region_node(rgn),
					       rgn->flags);
		} else {
			/* @rgn is fully contained, record it */
			if (!*end_rgn)
				*start_rgn = i;
			*end_rgn = i + 1;
		}
	}

	return 0;
}

/* adjust *@size so that (@base + *@size) doesn't overflow, return new size */
static inline phys_addr_t memblock_cap_size(phys_addr_t base, phys_addr_t *size)
{
	/* 传递进来的base =      ULLONG_MAX,所以size等于0 */
	return *size = min(*size, (phys_addr_t)ULLONG_MAX - base);
}


static void __init setup_machine_fdt(phys_addr_t dt_phys)
{
	if (!dt_phys || !early_init_dt_scan(phys_to_virt(dt_phys))) {
	}
}

bool __init early_init_dt_scan(void *params)
{
	bool status;
	early_init_dt_scan_nodes();
	return true;
}

void __init early_init_dt_scan_nodes(void)
{
	/* Setup memory, calling early_init_dt_add_memory_arch */
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);
}

int __init early_init_dt_scan_memory(unsigned long node, const char *uname,
				     int depth, void *data)
{
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be32 *reg, *endp;
	int l;


	reg = of_get_flat_dt_prop(node, "linux,usable-memory", &l);
	if (reg == NULL)
		reg = of_get_flat_dt_prop(node, "reg", &l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(__be32));

	pr_debug("memory scan node %s, reg size %d, data: %x %x %x %x,\n",
	    uname, l, reg[0], reg[1], reg[2], reg[3]);

	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		u64 base, size;

		base = dt_mem_next_cell(dt_root_addr_cells, &reg);
		size = dt_mem_next_cell(dt_root_size_cells, &reg);

		if (size == 0)
			continue;
		pr_debug(" - %llx ,  %llx\n", (unsigned long long)base,
		    (unsigned long long)size);

		early_init_dt_add_memory_arch(base, size);
	}

	return 0;
}

void __init __weak early_init_dt_add_memory_arch(u64 base, u64 size)
{
	const u64 phys_offset = __pa(PAGE_OFFSET);
	

	memblock_add(base, size);
}

int __init_memblock memblock_add(phys_addr_t base, phys_addr_t size)
{
	return memblock_add_range(&memblock.memory, base, size,
				   MAX_NUMNODES, 0);
}

int __init_memblock memblock_add_range(struct memblock_type *type, phys_addr_t base, phys_addr_t size,
				int nid, unsigned long flags)
{
	bool insert = false;
	phys_addr_t obase = base;
	phys_addr_t end = base + memblock_cap_size(base, &size);
	int i, nr_new;

	if (!size)
		return 0;

	/* special case for empty array */
	if (type->regions[0].size == 0) {
		type->regions[0].base = base;
		type->regions[0].size = size;
		type->regions[0].flags = flags;
		memblock_set_region_node(&type->regions[0], nid);
		type->total_size = size;
		return 0;
	}
repeat:
	/*
	 * The following is executed twice.  Once with %false @insert and
	 * then with %true.  The first counts the number of regions needed
	 * to accomodate the new area.  The second actually inserts them.
	 */
	base = obase;
	nr_new = 0;

	for (i = 0; i < type->cnt; i++) {
		struct memblock_region *rgn = &type->regions[i];
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;
		/* 不满足，表示左边有重合，region的是一个插入排序过程，region按照从小到大排列 */
		if (rbase >= end)
			break;
		/* 不满足，表示右边有重合 
		 * 如果最新的region是最大，则会继续查找，这时i会=	 type->cnt而推出
		 */
		if (rend <= base)
			continue;
		/*
		 * @rgn overlaps.  If it separates the lower part of new
		 * area, insert that portion.
		 */
		if (rbase > base) {
			nr_new++;
			if (insert)
				memblock_insert_region(type, i++, base,
						       rbase - base, nid,
						       flags);
		}
		/* area below @rend is dealt with, forget about it */
		base = min(rend, end);
	}

	/* insert the remaining portion */
	if (base < end) {
		nr_new++;
		if (insert)
			memblock_insert_region(type, i, base, end - base,
					       nid, flags);
	}

	/*
	 * If this was the first round, resize array and repeat for actual
	 * insertions; otherwise, merge and return.
	 */
	if (!insert) {
		while (type->cnt + nr_new > type->max)
			if (memblock_double_array(type, obase, size) < 0)
				return -ENOMEM;
		insert = true;
		goto repeat;
	} else {
		memblock_merge_regions(type);
		return 0;
	}
}

/**
 * memblock_insert_region - insert new memblock region
 * @type:	memblock type to insert into
 * @idx:	index for the insertion point
 * @base:	base address of the new region
 * @size:	size of the new region
 * @nid:	node id of the new region
 * @flags:	flags of the new region
 *
 * Insert new memblock region [@base,@base+@size) into @type at @idx.
 * @type must already have extra room to accomodate the new region.
 */
static void __init_memblock memblock_insert_region(struct memblock_type *type,
						   int idx, phys_addr_t base,
						   phys_addr_t size,
						   int nid, unsigned long flags)
{
	struct memblock_region *rgn = &type->regions[idx];

	BUG_ON(type->cnt >= type->max);
	/* 
	 * 将idx之后rgn之后的所有regions版移到rgn + 1 
	 * 起始就是一种插入排序，最左边的是按照region排列的话，从左到有一次增大
	 */
	memmove(rgn + 1, rgn, (type->cnt - idx) * sizeof(*rgn));
	rgn->base = base;
	rgn->size = size;
	rgn->flags = flags;
	memblock_set_region_node(rgn, nid);
	type->cnt++;
	type->total_size += size;
}

