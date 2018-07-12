# arm64 虚拟内存布局

下面是qemu中arm64打印出来的linux kerne虚拟内存布局

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

可以看到地址是安装有小到大排列的。

打印的函数如下：

```c
#define MLK(b, t) b, t, ((t) - (b)) >> 10
#define MLM(b, t) b, t, ((t) - (b)) >> 20
#define MLG(b, t) b, t, ((t) - (b)) >> 30
#define MLK_ROUNDUP(b, t) b, t, DIV_ROUND_UP(((t) - (b)), SZ_1K)

	pr_notice("Virtual kernel memory layout:\n"
		  "    vmalloc : 0x%16lx - 0x%16lx   (%6ld GB)\n"
#ifdef CONFIG_SPARSEMEM_VMEMMAP
		  "    vmemmap : 0x%16lx - 0x%16lx   (%6ld GB maximum)\n"
		  "              0x%16lx - 0x%16lx   (%6ld MB actual)\n"
#endif
		  "    fixed   : 0x%16lx - 0x%16lx   (%6ld KB)\n"
		  "    PCI I/O : 0x%16lx - 0x%16lx   (%6ld MB)\n"
		  "    modules : 0x%16lx - 0x%16lx   (%6ld MB)\n"
		  "    memory  : 0x%16lx - 0x%16lx   (%6ld MB)\n"
		  "      .init : 0x%p" " - 0x%p" "   (%6ld KB)\n"
		  "      .text : 0x%p" " - 0x%p" "   (%6ld KB)\n"
		  "      .data : 0x%p" " - 0x%p" "   (%6ld KB)\n",
		  MLG(VMALLOC_START, VMALLOC_END),
#ifdef CONFIG_SPARSEMEM_VMEMMAP
		  MLG((unsigned long)vmemmap,
		      (unsigned long)vmemmap + VMEMMAP_SIZE),
		  MLM((unsigned long)virt_to_page(PAGE_OFFSET),
		      (unsigned long)virt_to_page(high_memory)),
#endif
		  MLK(FIXADDR_START, FIXADDR_TOP),
		  MLM(PCI_IO_START, PCI_IO_END),
		  MLM(MODULES_VADDR, MODULES_END),
		  MLM(PAGE_OFFSET, (unsigned long)high_memory),
		  MLK_ROUNDUP(__init_begin, __init_end),
		  MLK_ROUNDUP(_text, _etext),
		  MLK_ROUNDUP(_sdata, _edata));

```

下来看下各个内存是怎么得到的。kernel在计算各个部分占用虚拟地址空间的大小时，是按照有高到低计算的。

## kernel本身占用内存

init段.text段和.data段在编译完kernel之后就已经决定了。

```c
[    0.000000]       .init : 0xffff800000d28000 - 0xffff800000e92000   (  1448 KB)
[    0.000000]       .text : 0xffff800000080000 - 0xffff800000d276e4   ( 12958 KB)
[    0.000000]       .data : 0xffff800000e96000 - 0xffff800000efbe00   (   408 KB)
```

## memory

```c
[    0.000000]     memory  : 0xffff800000000000 - 0xffff800040000000   (  1024 MB)
```

memory：物理内存的大小，由传递给内核的参数决定。

物理内存的起始地址映射到kernel的虚拟地址为PAGE_OFFSET，即改例中的0xffff800000000000

## modules

```c
/* 
 * PAGE_OFFSET, kernel image的开始虚拟地址，即kenrek image创建物理内存地址和虚拟地址映射时从该地址
 * 开始。该地址以下的地址是没有物理内存地址和其直接对应的（即没有线性映射）。vmalloc等都是在该地址的下
 * 方。在该例中PAGE_OFFSET为0xffff800000000000
 */
#define MODULES_END        (PAGE_OFFSET)           
#define MODULES_VADDR      (MODULES_END - SZ_64M)  /* modules 起始地址 */
```

## PCI I/O

```c
#define PCI_IO_SIZE		SZ_16M
/* modules的起始地址，就是PCI_IO的结束地址，2M起隔离作用 */
#define PCI_IO_END		(MODULES_VADDR - SZ_2M) 
#define PCI_IO_START	        (PCI_IO_END - PCI_IO_SIZE)
```

## fixed

```c
/* 2M起隔离作用 */
#define FIXADDR_TOP		(PCI_IO_START - SZ_2M)
#define FIXADDR_START	        (FIXADDR_TOP - FIXADDR_SIZE)
```

## vmemmap

```c
/* 
 * 1UL << (VA_BITS - PAGE_SHIFT)表示1<<VA_BITS的内存中有多少个1<<PAGE_SHIFT大小的页
 * 然后乘以一页的大小sizeof(struct page)
 */
#define VMEMMAP_SIZE   ALIGN((1UL << (VA_BITS - PAGE_SHIFT)) * sizeof(struct page), PUD_SIZE)
#define vmemmap        ((struct page *)(VMALLOC_END + SZ_64K))
```

## vmalloc

```c
#define VA_BITS			(CONFIG_ARM64_VA_BITS)
#define PAGE_OFFSET		(UL(0xffffffffffffffff) << (VA_BITS - 1))
#define PAGE_SHIFT		12
#define PUD_SHIFT		((PAGE_SHIFT - 3) * 3 + 3)
#define PUD_SIZE		(_AC(1, UL) << PUD_SHIFT)

/* (UL(0xffffffffffffffff) << VA_BITS) kernel空间的起始虚拟地址 */
#define VMALLOC_START		(UL(0xffffffffffffffff) << VA_BITS)
/* 
 * PAGE_OFFSET, kernel image的开始虚拟地址，即kenrek image创建物理内存地址和虚拟地址映射时从该地址
 * 开始。该地址以下的地址是没有物理内存地址和其直接对应的（即没有线性映射）。vmalloc等都是在该地址的
 * 下方。在该例中PAGE_OFFSET为0xffff800000000000
 */
#define VMALLOC_END    (PAGE_OFFSET - PUD_SIZE - VMEMMAP_SIZE - SZ_64K)
```

