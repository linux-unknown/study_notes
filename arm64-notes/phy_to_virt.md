# linux kernel 虚拟地址和物理地址转换宏

## virt_to_phys

```c
static inline phys_addr_t virt_to_phys(const volatile void *x)
{
	return __virt_to_phys((unsigned long)(x));
}
```

```c
#define __virt_to_phys(x)	(((phys_addr_t)(x) - PAGE_OFFSET + PHYS_OFFSET))
```

## PAGE_OFFSET

```c
#define PAGE_OFFSET		(UL(0xffffffffffffffff) << (VA_BITS - 1))
```

PAGE_OFFSET为kernel地址空间的起始地址，既物理地址映射后的虚拟地址。

## PHYS_OFFSET

```c
extern phys_addr_t		memstart_addr;
/* PHYS_OFFSET - the physical address of the start of memory. */
#define PHYS_OFFSET		({ memstart_addr; })
```

PHYS_OFFSET表示kernel地址空间起始地址对应的物理地址

## memstart_addr

```assembly
__switch_data:
	.quad	__mmap_switched
	.quad	__bss_start			// x6
	.quad	__bss_stop			// x7
	.quad	processor_id			// x4
	.quad	__fdt_pointer			// x5
	.quad	memstart_addr			// x6
	.quad	init_thread_union + THREAD_START_SP // sp
	
	/* physical memory */
EXPORT_SYMBOL(memstart_addr);
```

在跳转到start_kernel之前，会把PHYS_OFFSET的值写入大memstart_addr变量中。

## __mmap_switched

```assembly
__mmap_switched:
	adr	x3, __switch_data + 8

	ldp	x6, x7, [x3], #16
1:	cmp	x6, x7
	b.hs	2f
	str	xzr, [x6], #8			// Clear BSS
	b	1b
2:
	ldp	x4, x5, [x3], #16
	ldr	x6, [x3], #8
	ldr	x16, [x3]
	mov	sp, x16
	str	x22, [x4]			// Save processor ID
	str	x21, [x5]			// Save FDT pointer
	str	x24, [x6]			// Save PHYS_OFFSET,将PHYS_OFFSET写到变量memstart_addr
	mov	x29, #0
	b	start_kernel
ENDPROC(__mmap_switched)
```

## PHYS_OFFSET

PHYS_OFFSET的计算是在__calc_phys_offset

```assembly
/*
 * Calculate the start of physical memory.
 */
__calc_phys_offset:
	adr	x0, 1f			//使用adr指令获得1 lable当前的物理地址
	ldp	x1, x2, [x0]	//从x0读取两个64bit值到x1，和x2
	/*
	 * x1为1标号对应的虚拟地址，x0为1标号对应的物理地址。
	 * x0 - x1 物理地址和虚拟地址的一个offset，应为内核空间NORMAL zone是线性映射的，
	 * 所以该offset对整个地址空间都适用
	 * x2为kernel image的虚拟地址，x2 + x28就可以计算出，kernel image被加载的物理地址。
	 */
	sub	x28, x0, x1		// x28 = PHYS_OFFSET - PAGE_OFFSET 
	add	x24, x2, x28	// x24 = PHYS_OFFSET,
	ret
ENDPROC(__calc_phys_offset)

	.align 3
1:	.quad	.
	.quad	PAGE_OFFSET
```

x28 = 1标号的物理地址减去1标号的虚拟地址。kernel是以虚拟地址链接的。因为内核空间的NORMAL zone是线性映射的，所以x28也可以表示成下面的方式：

x28 = x24 - x2，既

x28 = memstart_addr - PAGE_OFFSET ，既

x28 = PHYS_OFFSET - PAGE_OFFSET 

x28 表示物理地址和虚拟地址的一个offset。

所以通过虚拟地址计算物理地址为：phy = virt + x28

所以：phy = virt + x28

​		= virt +  PHYS_OFFSET - PAGE_OFFSET

同理：virt = phy  - PHYS_OFFSET + PAGE_OFFSET  