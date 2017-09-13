# early_fixmap_init

调用关系：start_kernel-->setup_arch-->early_fixmap_init

```c
static pte_t bm_pte[PTRS_PER_PTE] __page_aligned_bss;
#if CONFIG_ARM64_PGTABLE_LEVELS > 2
static pmd_t bm_pmd[PTRS_PER_PMD] __page_aligned_bss;
#endif
#if CONFIG_ARM64_PGTABLE_LEVELS > 3
static pud_t bm_pud[PTRS_PER_PUD] __page_aligned_bss;
#endif

void __init early_fixmap_init(void)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	unsigned long addr = FIXADDR_START;

  	/*获得addr在pgd中对应页表项的虚拟地址*/
	pgd = pgd_offset_k(addr);
  	/* 把bm_pud页表的起始物理地址写入addr在pgd页表对应的页表项中 */
	pgd_populate(&init_mm, pgd, bm_pud);
  	/* 获得addr在pud页表中页表项对应的虚拟地址 */
	pud = pud_offset(pgd, addr);
  	/* 将bm_pmd页表起始的物理地址写入addr在pud页表中对应的页表项中 */
	pud_populate(&init_mm, pud, bm_pmd);
  	/* 获得addr在pmd页表中页表项对应的虚拟地址 */
	pmd = pmd_offset(pud, addr);
  	/* 将bm_pte页表起始的物理地址写入addr在pud页表中对应的页表项中 */
	pmd_populate_kernel(&init_mm, pmd, bm_pte);

  	/**
     * 我们并没有看到继续填充bm_pte页表中的内容，在bm_pte中才真正实现映射到物理地址中
     * 最后一步是通过调用__set_fixmap函数来完成的。还函数有被重新定义为：
     * #define __early_set_fixmap __set_fixmap
     * 
     */
  
	/*
	 * The boot-ioremap range spans multiple pmds, for which
	 * we are not preparted:
	 */
}
```

### FIXADDR_START

```c
/*F:arch/arm64/include/asm/memory.h*/
#define PCI_IO_SIZE		SZ_16M
#define VA_BITS			(CONFIG_ARM64_VA_BITS)
#define PAGE_OFFSET		(UL(0xffffffffffffffff) << (VA_BITS - 1))
#define MODULES_END		(PAGE_OFFSET)
#define MODULES_VADDR	(MODULES_END - SZ_64M)
#define PCI_IO_END		(MODULES_VADDR - SZ_2M)
#define PCI_IO_START	(PCI_IO_END - PCI_IO_SIZE)
#define FIXADDR_TOP		(PCI_IO_START - SZ_2M)
//arch/arm64/include/asm/fixmap.h
enum fixed_addresses {
	FIX_HOLE,
	FIX_EARLYCON_MEM_BASE,
	__end_of_permanent_fixed_addresses,

	/*
	 * Temporary boot-time mappings, used by early_ioremap(),
	 * before ioremap() is functional.
	 */
#ifdef CONFIG_ARM64_64K_PAGES
#define NR_FIX_BTMAPS		4
#else
#define NR_FIX_BTMAPS		64
#endif
#define FIX_BTMAPS_SLOTS	7
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)

	FIX_BTMAP_END = __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1,
	FIX_TEXT_POKE0,
	__end_of_fixed_addresses
};
/*FIXADDR_SIZE = 2 << 12 = 8K*/
#define FIXADDR_SIZE	(__end_of_permanent_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE) 
```

### arm64内核内存布局

```c
[    0.000000]     vmalloc : 0xffff000000000000 - 0xffff7bffbfff0000   (126974 GB)
[    0.000000]     vmemmap : 0xffff7bffc0000000 - 0xffff7fffc0000000   (  4096 GB maximum)
[    0.000000]               0xffff7bffc1000000 - 0xffff7bffc2000000   (    16 MB actual)
[    0.000000]     fixed   : 0xffff7ffffabfe000 - 0xffff7ffffac00000   (     8 KB)
[    0.000000]     PCI I/O : 0xffff7ffffae00000 - 0xffff7ffffbe00000   (    16 MB)
[    0.000000]     modules : 0xffff7ffffc000000 - 0xffff800000000000   (    64 MB)
[    0.000000]     memory  : 0xffff800000000000 - 0xffff800040000000   (  1024 MB)
[    0.000000]       .init : 0xffff800000d28000 - 0xffff800000e92000   (  1448 KB)
[    0.000000]       .text : 0xffff800000080000 - 0xffff800000d276e4   ( 12958 KB)
[    0.000000]       .data : 0xffff800000e96000 - 0xffff800000efbe00   (   408 KB)
```
### 各页表项定义

```c
typedef u64 pteval_t;
typedef u64 pmdval_t;
typedef u64 pudval_t;
typedef u64 pgdval_t;

typedef struct { pgdval_t pgd; } pgd_t;
typedef struct { pudval_t pud; } pud_t;
typedef struct { pmdval_t pmd; } pmd_t;
typedef struct { pteval_t pte; } pte_t;

/**
 * 在计算addr在页表中页表项的偏移地址时，比如：((mm)->pgd + pgd_index(addr))，其中
 * pgd_index为(((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
 * ((mm)->pgd为pgd_t类型的指针，所以((mm)->pgd + pgd_index(addr))中 pgd_index(addr))不需要乘以
 * 8（一个页表项8个字节），以为指针相加时，C语言已经做了处理
 */
```


### pgd_offset_k

```c
/*F:arch/arm64/include/asm/pgtable.h*/
/* to find an entry in a page-table-directory */
#define pgd_index(addr)			(((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
/* mm->pgd既swapper_pg_dir */
#define pgd_offset(mm, addr)	((mm)->pgd + pgd_index(addr))
/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)
```

### pgd_populate

```c
/*arch/arm64/include/asm/pgtable-types.h */
#define __pgd(x)	((pgd_t) { (x) } )

/* arch/arm64/include/asm/pgtable.h */
static inline void set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	*pgdp = pgd;/*pgdp为虚拟地址，所以可以直接指针操作*/
	dsb(ishst);
}
/* arch/arm64/include/asm/pgtable-hwdef.h 表示是一个页表项*/
#define PUD_TYPE_TABLE		(_AT(pudval_t, 3) << 0)

/* arch/arm64/include/asm/pgalloc.h */
static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pud_t *pud)
{
	/*页表项中存储的是物理地址，所以把pud虚拟地址转换为物理地址*/
	set_pgd(pgd, __pgd(__pa(pud) | PUD_TYPE_TABLE));
}
```

### pud_offset

```c
#define pgd_val(x)	((x).pgd)

/*
 * Highest possible physical address supported.
 */
#define PHYS_MASK_SHIFT		(48)
#define PHYS_MASK		((UL(1) << PHYS_MASK_SHIFT) - 1)
#define PAGE_MASK		(~(PAGE_SIZE-1))

static inline pud_t *pgd_page_vaddr(pgd_t pgd)
{
	return __va(pgd_val(pgd) & PHYS_MASK & (s32)PAGE_MASK);
}

/* Find an entry in the frst-level page table. */
#define pud_index(addr)		(((addr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1))
static inline pud_t *pud_offset(pgd_t *pgd, unsigned long addr)
{
  	/**
     * pud_index:获得addr在pud中的索引
     * *pgd保存呢的是pud页表起始地址的物理地址，在kernel中不能直接操作
     * pgd_page_vaddr将pud的物理地址转换为虚拟地址
     * 两部分合起来则为addr在pud页表中对应项的虚拟地址。
     */
	return (pud_t *)pgd_page_vaddr(*pgd) + pud_index(addr);
}
```

### pud_populate

```c
static inline void set_pud(pud_t *pudp, pud_t pud)
{
	*pudp = pud;
	dsb(ishst);
	isb();
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud(__pa(pmd) | PMD_TYPE_TABLE));
}
```

### pmd_offset

```c
/* 和pud_offset基本一样，就不详细分析 */
static inline pmd_t *pud_page_vaddr(pud_t pud)
{
	return __va(pud_val(pud) & PHYS_MASK & (s32)PAGE_MASK);
}

/* Find an entry in the second-level page table. */
#define pmd_index(addr)		(((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))

static inline pmd_t *pmd_offset(pud_t *pud, unsigned long addr)
{
	return (pmd_t *)pud_page_vaddr(*pud) + pmd_index(addr);
}
```

###  pmd_populate_kernel

```c
/* 和前面的pud_populate基本相同 */
static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp, pte_t *ptep)
{
	/*
	 * The pmd must be loaded with the physical address of the PTE table
	 */
	__pmd_populate(pmdp, __pa(ptep), PMD_TYPE_TABLE);
}

static inline void __pmd_populate(pmd_t *pmdp, phys_addr_t pte,
				  pmdval_t prot)
{
	set_pmd(pmdp, __pmd(pte | prot));
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
	dsb(ishst);
	isb();
}
```

### __set_fixmap

```c
#define pfn_pte(pfn,prot)	(__pte(((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot)))
#define PROT_DEFAULT		(PTE_TYPE_PAGE | PTE_AF | PTE_SHARED)

void __set_fixmap(enum fixed_addresses idx,
			       phys_addr_t phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *pte;

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}

	/* 获得addr在pte页表中页表项的虚拟地址 */
	pte = fixmap_pte(addr);
	/**
     * flags用于配置memory的属性，一般都会或上宏PROT_DEFAULT，其中的PTE_TYPE_PAGE项定义为
     * #define PTE_TYPE_PAGE		(_AT(pteval_t, 3) << 0)
     * 3表示页表项有效，该也表现为一个table
     */
	if (pgprot_val(flags)) {
		set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, flags));
	} else {
		pte_clear(&init_mm, addr, pte);
		flush_tlb_kernel_range(addr, addr+PAGE_SIZE);
	}
}

```

```c
static inline pte_t *pmd_page_vaddr(pmd_t pmd)
{
	return __va(pmd_val(pmd) & PHYS_MASK & (s32)PAGE_MASK);
}

#define pte_index(addr)		(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir,addr)	(pmd_page_vaddr(*(dir)) + pte_index(addr))

static inline pte_t * fixmap_pte(unsigned long addr)
{
	/* 获得addr在pmd页表中页表项的虚拟地址 */
	pmd_t *pmd = fixmap_pmd(addr);

	/* 获得addr在pte页表中页表项的虚拟地址 */
	return pte_offset_kernel(pmd, addr);
}
```

```c
static inline pmd_t * fixmap_pmd(unsigned long addr)
{
	/* 获得addr在pud页表中页表项的虚拟地址 */
	pud_t *pud = fixmap_pud(addr);

	/* 获得addr在pmd页表中页表项的虚拟地址 */
	return pmd_offset(pud, addr);
}
```

```c
static inline pud_t * fixmap_pud(unsigned long addr)
{
	/* 获得addr在pgd页表中页表项的虚拟地址 */
	pgd_t *pgd = pgd_offset_k(addr);

	/* 获得addr在pud页表中页表项的虚拟地址 */
	return pud_offset(pgd, addr);
}
```

