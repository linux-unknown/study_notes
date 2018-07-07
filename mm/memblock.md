# memblock

memblock是linux kernel在伙伴系统还没有完成初始的时候用来进行简单内存管理，memblock不追求性能。

从linux的代码执行流程看，memblock是和架构相关的，我们只关注arm64。

## setup_arch

```c
void __init setup_arch(char **cmdline_p)
{
	/* 最早调用和memblock相关的是在该函数中 */
	setup_machine_fdt(__fdt_pointer);

	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code   = (unsigned long) _etext;
	init_mm.end_data   = (unsigned long) _edata;
	init_mm.brk	   = (unsigned long) _end;

	*cmdline_p = boot_command_line;
	arm64_memblock_init();
	paging_init();
}
```

`setup_machine_fdt->early_init_dt_scan->early_init_dt_scan_nodes`

## early_init_dt_scan_nodes

```c
void __init early_init_dt_scan_nodes(void)
{
	/* Setup memory, calling early_init_dt_add_memory_arch */
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);
}
```

##  early_init_dt_scan_memory

```c
int __init early_init_dt_scan_memory(unsigned long node, const char *uname,
				     int depth, void *data)
{
	/* 获取device_type属性，dts中的memory node 会有该属性 */
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
		pr_debug(" - %llx ,  %llx\n", (unsigned long long)base, (unsigned long long)size);
		/* 得到memory的base和size */
		early_init_dt_add_memory_arch(base, size);
	}
	return 0;
}
```

```
memory {
  reg = <0x0 0x40000000 0x0 0x40000000>;
  device_type = "memory";
};  
```

## early_init_dt_add_memory_arch

```c
void __init __weak early_init_dt_add_memory_arch(u64 base, u64 size)
{
	const u64 phys_offset = __pa(PAGE_OFFSET);
	memblock_add(base, size);
}
```

## memblock_add

```c
int __init_memblock memblock_add(phys_addr_t base, phys_addr_t size)
{
	/* 将该region添加到memblock.memory */
	return memblock_add_range(&memblock.memory, base, size, MAX_NUMNODES, 0);
}
```

```c
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
	 * 下面的代码会执行两次，第一次insert是false，第二次是true，第一次计算需要
	 * 容纳的region的数量，第二次会执行真正得insert操作
	 */
	base = obase;
	nr_new = 0;
	/* 如果insert的region是最打的则，则该for循环会扫描到所有region，然后退出 */
	for (i = 0; i < type->cnt; i++) {
		struct memblock_region *rgn = &type->regions[i];
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;
		/* 不满足，表示左边有重合，region的是一个插入排序过程，按照region的数组大小从小到大排列 */
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
				memblock_insert_region(type, i++, base, rbase - base, nid, flags);
		}
		/* area below @rend is dealt with, forget about it */
		base = min(rend, end);
	}

	/* insert the remaining portion */
	if (base < end) {
		nr_new++;
		if (insert)
			memblock_insert_region(type, i, base, end - base, nid, flags);
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

```

```c
static void __init_memblock memblock_insert_region(struct memblock_type *type, int idx,
                             phys_addr_t base,
						   phys_addr_t size,
						   int nid, unsigned long flags)
{
	struct memblock_region *rgn = &type->regions[idx];

	BUG_ON(type->cnt >= type->max);
	/* 
	 * 将idx对应region之后的所有regions搬移到rgn + 1处， 
	 * 其实就是一种插入排序，region的大小安装region数组的index从左到右依次增大
	 */
	memmove(rgn + 1, rgn, (type->cnt - idx) * sizeof(*rgn));
	rgn->base = base;
	rgn->size = size;
	rgn->flags = flags;
	memblock_set_region_node(rgn, nid);
	type->cnt++;/* type->cnt自增 */
	type->total_size += size;
}
```

## memblock

```c
#define INIT_MEMBLOCK_REGIONS	128

static struct memblock_region memblock_memory_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;
static struct memblock_region memblock_reserved_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;

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
	/* memory表示总共物理内存 */
	struct memblock_type memory;
	/* reserved的表示被占用的 */
	struct memblock_type reserved;
};

struct memblock memblock __initdata_memblock = {
	.memory.regions		= memblock_memory_init_regions,
	.memory.cnt		= 1,	/* empty dummy entry */
	.memory.max		= INIT_MEMBLOCK_REGIONS,

	.reserved.regions	= memblock_reserved_init_regions,
	.reserved.cnt		= 1,	/* empty dummy entry */
	.reserved.max		= INIT_MEMBLOCK_REGIONS,

	.bottom_up		= false,
	.current_limit		= MEMBLOCK_ALLOC_ANYWHERE,
};
```

```
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
```

```c
void __init memblock_enforce_memory_limit(phys_addr_t limit)
{
	phys_addr_t max_addr = (phys_addr_t)ULLONG_MAX;
	struct memblock_region *r;

	if (!limit)
		return;

	/* find out max address 
	 * 这个时候已经读取了dts中的memory信息，并且存放到了memblock.memory中
	 * 找到最大的max address，如果limit超过所有region的size之和，则max_adress不变
	 * 等于ULLONG_MAX
	 */
	for_each_memblock(memory, r) {
		if (limit <= r->size) {
			/* r->base + limit会大于limit，这样limit就没意义了？
			 * base size    limit(100)
			 * 10   30              100 - 30 = 70
			 * 40   60              80 - 60 = 10
			 * 95   30              100 + 10 = 110
			 * 没看明白为什么这么搞
			 */
			max_addr = r->base + limit;
			break;
		}
		/* 
		 * limit = limit - r->size，
		 * 如果limit 一直大于 r->size，则表示所有region都在limit之内
		 * 那么max_address不变
		 */
		limit -= r->size;
	}

	/* truncate（缩短） both memory and reserved regions，将max_addr到ULLONG_MAX的区域移除 */
	memblock_remove_range(&memblock.memory, max_addr, (phys_addr_t)ULLONG_MAX);
	memblock_remove_range(&memblock.reserved, max_addr, (phys_addr_t)ULLONG_MAX);
}
```

