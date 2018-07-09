# memblock

[TOC]

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

###  early_init_dt_scan_nodes

```c
void __init early_init_dt_scan_nodes(void)
{
	/* Setup memory, calling early_init_dt_add_memory_arch */
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);
}
```

####  early_init_dt_scan_memory

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

#### early_init_dt_add_memory_arch

```c
void __init __weak early_init_dt_add_memory_arch(u64 base, u64 size)
{
	const u64 phys_offset = __pa(PAGE_OFFSET);
	memblock_add(base, size);
}
```

#### memblock_add

```c
int __init_memblock memblock_add(phys_addr_t base, phys_addr_t size)
{
	/* 将该region添加到memblock.memory */
	return memblock_add_range(&memblock.memory, base, size, MAX_NUMNODES, 0);
}
```

#### memblock_add_range

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

#### memblock_insert_region

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



### arm64_memblock_init

### memblock数据结构

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
```c
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

### memblock_enforce_memory_limit

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

### memblock_reserve

```c
int __init_memblock memblock_reserve(phys_addr_t base, phys_addr_t size)
{
	return memblock_reserve_region(base, size, MAX_NUMNODES, 0);
}
```

#### memblock_reserve_region

```c
static int __init_memblock memblock_reserve_region(phys_addr_t base, phys_addr_t size, 
                             int nid,
						   unsigned long flags)
{
	struct memblock_type *_rgn = &memblock.reserved;
	/* 添加到reserved    memblock_type中, 表示该region被使用*/
	return memblock_add_range(_rgn, base, size, nid, flags);
}
```

###  early_init_fdt_scan_reserved_mem

```c
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
	/* 
	 * Process header /memreserve/ fields ，保留使用/memreserve/定义的内存
	 * /memreserve/定义的内存会被放到dtb一个专有的结构中进行保存
	 * reserved-memory定义的内存是放到node中的，具体的区别见：
	 * https://blog.csdn.net/kickxxx/article/details/54631535
	 */
	for (n = 0; ; n++) {
		/* 从off_mem_rsvmap中获得base和size */
		fdt_get_mem_rsv(initial_boot_params, n, &base, &size);
		if (!size)
			break;
		early_init_dt_reserve_memory_arch(base, size, 0);
	}
	/* 扫描dts中的reserved-memory */
	of_scan_flat_dt(__fdt_scan_reserved_mem, NULL);
	fdt_init_reserved_mem();
}
```

#### early_init_dt_reserve_memory_arch

```c
int __init __weak early_init_dt_reserve_memory_arch(phys_addr_t base,
					phys_addr_t size, bool nomap)
{
	/* 如果不需要映射，则把该region从memblock.memory中移除 */
	if (nomap)
		return memblock_remove(base, size);
	/* 将该region添加到memblock.reserved中 */
	return memblock_reserve(base, size);
}
```

### __fdt_scan_reserved_mem

```c
/**
 * fdt_scan_reserved_mem() - scan a single FDT node for reserved memory
 */
static int __init __fdt_scan_reserved_mem(unsigned long node, const char *uname,
					  int depth, void *data)
{
	static int found;
	const char *status;
	int err;

	if (!found && depth == 1 && strcmp(uname, "reserved-memory") == 0) {
		if (__reserved_mem_check_root(node) != 0) {
			pr_err("Reserved memory: unsupported node format, ignoring\n");
			/* break scan */
			return 1;
		}
		found = 1;
		/* scan next node */
		return 0;
	} else if (!found) {
		/* scan next node */
		return 0;
	} else if (found && depth < 2) {
		/* scanning of /reserved-memory has been finished */
		return 1;
	}

	status = of_get_flat_dt_prop(node, "status", NULL);
	if (status && strcmp(status, "okay") != 0 && strcmp(status, "ok") != 0)
		return 0;

	err = __reserved_mem_reserve_reg(node, uname);
	/* 
	 * 返回ENOENT，表示没有reg属性，
	 * 从reg属性会知道起始地址和大小，如果没有reg，则查找有没有size属性 
	 */
	if (err == -ENOENT && of_get_flat_dt_prop(node, "size", NULL))
		/* 可以看到如果有size属性，则传递进去的size值为0 */
		fdt_reserved_mem_save_node(node, uname, 0, 0);

	/* scan next node */
	return 0;
}
```

#### __reserved_mem_reserve_reg

```c
/**
 * res_mem_reserve_reg() - reserve all memory described in 'reg' property
 */
static int __init __reserved_mem_reserve_reg(unsigned long node, const char *uname)
{
	int t_len = (dt_root_addr_cells + dt_root_size_cells) * sizeof(__be32);
	phys_addr_t base, size;
	int len;
	const __be32 *prop;
	int nomap, first = 1;

	prop = of_get_flat_dt_prop(node, "reg", &len);
	if (!prop)
		return -ENOENT;

	if (len && len % t_len != 0) {
		pr_err("Reserved memory: invalid reg property in '%s', skipping node.\n",
		       uname);
		return -EINVAL;
	}
	/* 读取no-map属性的值 */
	nomap = of_get_flat_dt_prop(node, "no-map", NULL) != NULL;

	while (len >= t_len) {
		base = dt_mem_next_cell(dt_root_addr_cells, &prop);
		size = dt_mem_next_cell(dt_root_size_cells, &prop);

		if (size &&
		    early_init_dt_reserve_memory_arch(base, size, nomap) == 0)
			pr_debug("Reserved memory: reserved region for node '%s': base %pa, size %ld MiB\n",
				uname, &base, (unsigned long)size / SZ_1M);
		else
			pr_info("Reserved memory: failed to reserve memory for node '%s': base %pa, size %ld MiB\n",
				uname, &base, (unsigned long)size / SZ_1M);

		len -= t_len;
		if (first) {
			fdt_reserved_mem_save_node(node, uname, base, size);
			first = 0;
		}
	}
	return 0;
}
```

#### fdt_reserved_mem_save_node

```c
/**
 * res_mem_save_node() - save fdt node for second pass initialization
 */
void __init fdt_reserved_mem_save_node(unsigned long node, const char *uname,
				      phys_addr_t base, phys_addr_t size)
{
	/* 将信息保存到reserved_mem中 */
	struct reserved_mem *rmem = &reserved_mem[reserved_mem_count];

	if (reserved_mem_count == ARRAY_SIZE(reserved_mem)) {
		pr_err("Reserved memory: not enough space all defined regions.\n");
		return;
	}

	rmem->fdt_node = node;
	rmem->name = uname;
	rmem->base = base;
	rmem->size = size;

	reserved_mem_count++;
	return;
}
```

### fdt_init_reserved_mem

```c
/**
 * fdt_init_reserved_mem - allocate and init all saved reserved memory regions
 */
void __init fdt_init_reserved_mem(void)
{
	int i;
	for (i = 0; i < reserved_mem_count; i++) {
		struct reserved_mem *rmem = &reserved_mem[i];
		unsigned long node = rmem->fdt_node;
		int len;
		const __be32 *prop;
		int err = 0;

		prop = of_get_flat_dt_prop(node, "phandle", &len);
		if (!prop)
			prop = of_get_flat_dt_prop(node, "linux,phandle", &len);
		if (prop)
			rmem->phandle = of_read_number(prop, len/4);
		/* 
		 * 没有reg属性的reserved-memory node，其 rmem->size为0 
		 * 以为只有size，没有知道base，所以需要分配size大小的内存
		 */
		if (rmem->size == 0)
			err = __reserved_mem_alloc_size(node, rmem->name,
						 &rmem->base, &rmem->size);
		if (err == 0)
			__reserved_mem_init_node(rmem);
	}
}
```

#### __reserved_mem_alloc_size

```c
/**
 * res_mem_alloc_size() - allocate reserved memory described by 'size', 'align'
 *			  and 'alloc-ranges' properties
 */
static int __init __reserved_mem_alloc_size(unsigned long node,
	const char *uname, phys_addr_t *res_base, phys_addr_t *res_size)
{
	int t_len = (dt_root_addr_cells + dt_root_size_cells) * sizeof(__be32);
	phys_addr_t start = 0, end = 0;
	phys_addr_t base = 0, align = 0, size;
	int len;
	const __be32 *prop;
	int nomap;
	int ret;

	prop = of_get_flat_dt_prop(node, "size", &len);


	size = dt_mem_next_cell(dt_root_size_cells, &prop);

	nomap = of_get_flat_dt_prop(node, "no-map", NULL) != NULL;

	prop = of_get_flat_dt_prop(node, "alignment", &len);
	if (prop) {
		align = dt_mem_next_cell(dt_root_addr_cells, &prop);
	}

	prop = of_get_flat_dt_prop(node, "alloc-ranges", &len);
	if (prop) {
		base = 0;

		while (len > 0) {
			start = dt_mem_next_cell(dt_root_addr_cells, &prop);
			end = start + dt_mem_next_cell(dt_root_size_cells,
						       &prop);

			ret = early_init_dt_alloc_reserved_memory_arch(size,
					align, start, end, nomap, &base);
				break;
			}
			len -= t_len;
		}

	} else {
		ret = early_init_dt_alloc_reserved_memory_arch(size, align,
							0, 0, nomap, &base);
	}
	*res_base = base;
	*res_size = size;
	return 0;
}

```

#### early_init_dt_alloc_reserved_memory_arch

```c
int __init __weak early_init_dt_alloc_reserved_memory_arch(phys_addr_t size,
	phys_addr_t align, phys_addr_t start, phys_addr_t end, bool nomap,
	phys_addr_t *res_base)
{
	/*
	 * We use __memblock_alloc_base() because memblock_alloc_base()
	 * panic()s on allocation failure.
	 */
	phys_addr_t base = __memblock_alloc_base(size, align, end);

	*res_base = base;
	/* 
	 * memblock.memory中的内存，所以会进行映射
	 * 如果no map，则从memblock.memory中移除
	 */
	if (nomap)
		return memblock_remove(base, size);
	return 0;
}
```

####  __memblock_alloc_base

```
phys_addr_t __init __memblock_alloc_base(phys_addr_t size, phys_addr_t align, phys_addr_t max_addr)
{
	return memblock_alloc_base_nid(size, align, max_addr, NUMA_NO_NODE);
}
```

#### memblock_alloc_range_nid

```c
static phys_addr_t __init memblock_alloc_range_nid(phys_addr_t size,
					phys_addr_t align, phys_addr_t start,
					phys_addr_t end, int nid)
{
	phys_addr_t found;

	if (!align)
		align = SMP_CACHE_BYTES;

	found = memblock_find_in_range_node(size, align, start, end, nid);
	/* 找到起始地址，并且增加到memblock.reserved region中 */
	if (found && !memblock_reserve(found, size)) {
		/*
		 * The min_count is set to 0 so that memblock allocations are
		 * never reported as leaks.
		 */
		kmemleak_alloc(__va(found), size, 0, 0);
		return found;
	}
	return 0;
}
```

#### memblock_find_in_range_node

```c
phys_addr_t __init_memblock memblock_find_in_range_node(phys_addr_t size,
					phys_addr_t align, phys_addr_t start,
					phys_addr_t end, int nid)
{
	phys_addr_t kernel_end, ret;

	/* pump up @end */
	if (end == MEMBLOCK_ALLOC_ACCESSIBLE)
		end = memblock.current_limit;

	/* avoid allocating the first page */
	start = max_t(phys_addr_t, start, PAGE_SIZE);
	end = max(start, end);
	kernel_end = __pa_symbol(_end);

	/*
	 * try bottom-up allocation only when bottom-up mode
	 * is set and @end is above the kernel image.
	 */
	if (memblock_bottom_up() && end > kernel_end) {
	
	}

	return __memblock_find_range_top_down(start, end, size, align, nid);
}
```

#### __memblock_find_range_top_down

```c
static phys_addr_t __init_memblock
__memblock_find_range_top_down(phys_addr_t start, phys_addr_t end,
			       phys_addr_t size, phys_addr_t align, int nid)
{
	phys_addr_t this_start, this_end, cand;
	u64 i;

	/* 
	 * 找到一个memblock，该memblock在memblock.memory范围内，但是不再memblock.reserved中
	 * 即，该memblock，没有被使用，memblock.reserved中的内存表示已经使用的
	 */
	for_each_free_mem_range_reverse(i, nid, &this_start, &this_end, NULL) {
		this_start = clamp(this_start, start, end);
		this_end = clamp(this_end, start, end);

		if (this_end < size)
			continue;

		cand = round_down(this_end - size, align);
		if (cand >= this_start)
			/* 返回起始地址 */
			return cand;
	}

	return 0;
}
```

### __reserved_mem_init_node

```c
static int __init __reserved_mem_init_node(struct reserved_mem *rmem)
{
	extern const struct of_device_id __reservedmem_of_table[];
	const struct of_device_id *i;

	/* 
	 * 调用使用RESERVEDMEM_OF_DECLARE声明的结构体中的init函数，例如cma
	 * RESERVEDMEM_OF_DECLARE(cma, "shared-dma-pool", rmem_cma_setup);
	 */
	for (i = __reservedmem_of_table; i < &__rmem_of_table_sentinel; i++) {
		reservedmem_of_init_fn initfn = i->data;
		const char *compat = i->compatible;

		if (!of_flat_dt_is_compatible(rmem->fdt_node, compat))
			continue;

		if (initfn(rmem) == 0) {
			pr_info("Reserved memory: initialized node %s, compatible id %s\n",
				rmem->name, compat);
			return 0;
		}
	}
	return -ENOENT;
}
```

## memblock总结

memblock中有两个type：memory和reserved

memory：表示系统中可以使用的内存，而且这里面的内存最后会被进行虚拟地址和物理地址映射。

reserved：表示已经使用的内存。reserved内存都是在memory的范围内

