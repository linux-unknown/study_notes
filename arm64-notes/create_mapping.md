# create_mapping

```c
static void __init __map_memblock(phys_addr_t start, phys_addr_t end)
{
	create_mapping(start, __phys_to_virt(start), end - start,
			PAGE_KERNEL_EXEC);
}
```

```c
/**
 * phys:物理地址
 * virt：虚拟地址
 * size：需要映射地址的大小
 * prot：映射的属性，比如可执行，是否cache等等
 */
static void __ref create_mapping(phys_addr_t phys, unsigned long virt,
				  phys_addr_t size, pgprot_t prot)
{
	if (virt < VMALLOC_START) {
		pr_warn("BUG: not creating mapping for %pa at 0x%016lx - outside kernel range\n",
			&phys, virt);
		return;
	}
    /* pgd_offset_k,获取虚拟地址对应的pgd_offset */
	__create_mapping(&init_mm, pgd_offset_k(virt & PAGE_MASK), phys, virt,
			 size, prot, early_alloc);
}
```

## pgd_offset_k

```c
/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)
```

```c
/* to find an entry in a page-table-directory */
#define pgd_index(addr)		(((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#define pgd_offset(mm, addr)	((mm)->pgd+pgd_index(addr))
```

```c
/*
 * Create the page directory entries and any necessary page tables for the
 * mapping specified by 'md'.
 */
static void  __create_mapping(struct mm_struct *mm, pgd_t *pgd,
				    phys_addr_t phys, unsigned long virt,
				    phys_addr_t size, pgprot_t prot,
				    void *(*alloc)(unsigned long size))
{
	unsigned long addr, length, end, next;
	/* 虚拟地址起始地址 */
	addr = virt & PAGE_MASK;
    /* 对齐之后的长度 */
	length = PAGE_ALIGN(size + (virt & ~PAGE_MASK));
	/* 虚拟地址的结束地址 */
	end = addr + length;
	do {
        /* pgd_addr_end返回addr + pgd映射的大小 */
        next = pgd_addr_end(addr, end);
        alloc_init_pud(mm, pgd, addr, next, phys, prot, alloc);
        phys += next - addr;
     /* 逗号运算符，表达式的值为最后一个表达式的值，即addr != end */
	} while (pgd++, addr = next, addr != end);
}
```

```c
#define pgd_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + PGDIR_SIZE) & PGDIR_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})
```

