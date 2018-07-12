
#define CONFIG_ARM64_VA_BITS 48

/*
 * PAGE_OFFSET - the virtual address of the start of the kernel image (top
 *		 (VA_BITS - 1))
 * VA_BITS - the maximum number of bits for virtual addresses.
 * TASK_SIZE - the maximum size of a user space task.
 * TASK_UNMAPPED_BASE - the lower boundary of the mmap VM area.
 * The module space lives between the addresses given by TASK_SIZE
 * and PAGE_OFFSET - it must be within 128MB of the kernel text.
 */
#define VA_BITS			(CONFIG_ARM64_VA_BITS)
#define PAGE_OFFSET		(UL(0xffffffffffffffff) << (VA_BITS - 1))

#define PAGE_SHIFT		12

#define PUD_SHIFT		((PAGE_SHIFT - 3) * 3 + 3)
#define PUD_SIZE		(_AC(1, UL) << PUD_SHIFT)


/* vmalloc */
/* (UL(0xffffffffffffffff) << VA_BITS) kernel空间的起始虚拟地址 */
#define VMALLOC_START		(UL(0xffffffffffffffff) << VA_BITS)
/* 
 * PAGE_OFFSET, kernel image的开始虚拟地址，即kenrek image创建物理内存地址和虚拟地址映射时从该地址开始。
 * 该地址以下的地址是没有物理内存地址和其直接对应的（即没有线性映射）。vmalloc等都是在该地址的下方 
 * 在该例中PAGE_OFFSET为0xffff800000000000
 */
#define VMALLOC_END			(PAGE_OFFSET - PUD_SIZE - VMEMMAP_SIZE - SZ_64K)


/* vmemmap */
/* 1UL << (VA_BITS - PAGE_SHIFT)表示1<<VA_BITS的内存有多少1<<PAGE_SHIFT大小的页*/
#define VMEMMAP_SIZE		ALIGN((1UL << (VA_BITS - PAGE_SHIFT)) * sizeof(struct page), PUD_SIZE)
#define vmemmap				((struct page *)(VMALLOC_END + SZ_64K))

/*fixed*/
#define FIXADDR_TOP		(PCI_IO_START - SZ_2M)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE)

/* PCI I/O */
#define PCI_IO_SIZE		SZ_16M
#define PCI_IO_END		(MODULES_VADDR - SZ_2M)
#define PCI_IO_START	(PCI_IO_END - PCI_IO_SIZE)

/* modules */
#define MODULES_END		(PAGE_OFFSET)
#define MODULES_VADDR	(MODULES_END - SZ_64M) /* modules 起始地址 */


/** arm64内存布局，地址安装从小到达排列
 * [    0.000000]     vmalloc : 0xffff000000000000 - 0xffff7bffbfff0000   (126974 GB)
 * [    0.000000]     vmemmap : 0xffff7bffc0000000 - 0xffff7fffc0000000   (  4096 GB maximum)
 * [    0.000000]               0xffff7bffc1000000 - 0xffff7bffc2000000   (    16 MB actual)
 * [    0.000000]     fixed   : 0xffff7ffffabfe000 - 0xffff7ffffac00000   (     8 KB)
 * [    0.000000]     PCI I/O : 0xffff7ffffae00000 - 0xffff7ffffbe00000   (    16 MB)
 * [    0.000000]     modules : 0xffff7ffffc000000 - 0xffff800000000000   (    64 MB)
 * [    0.000000]     memory  : 0xffff800000000000 - 0xffff800040000000   (  1024 MB)
 * [    0.000000]       .init : 0xffff800000d28000 - 0xffff800000e92000   (  1448 KB)
 * [    0.000000]       .text : 0xffff800000080000 - 0xffff800000d276e4   ( 12958 KB)
 * [    0.000000]       .data : 0xffff800000e96000 - 0xffff800000efbe00   (   408 KB)
 */

