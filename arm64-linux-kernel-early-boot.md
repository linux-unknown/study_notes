

# arm64 linux kernel 启动

### 代码入口地址

**kernel的入口地址由链接脚本制定：** 
OUTPUT_ARCH(aarch64)
ENTRY(_text)
ENTRY(\_text)表示程序入口的标号为_text,该标号可以是链接脚本中定义的也可以是代码中定义的。本例中是链接脚本中定义的，所以arm64 kernel的第一调指令执行的指令是\_text
程序入口的地址在连接脚本vmlinux.lds中制定：
```c
SECTIONS
{
	......
	. = PAGE_OFFSET + TEXT_OFFSET;
	.head.text : {
		_text = .;
		HEAD_TEXT
	}
	......
｝
```

第4行中的“.”被成为 location counter。. = PAGE_OFFSET + TEXT_OFFSET;表示将location counter赋值为PAGE_OFFSET + TEXT_OFFSET，后续代码链接的地址从该地址开始。

```c
#define VA_BITS			(CONFIG_ARM64_VA_BITS)
#define PAGE_OFFSET		(UL(0xffffffffffffffff) << (VA_BITS - 1))
```
从上面的宏可以看出PAGE_OFFSET的值和虚拟地址的位数相关。
TEXT_OFFSET定义在arch/arm64/Makefile中，如下：
```cmake
ifeq ($(CONFIG_ARM64_RANDOMIZE_TEXT_OFFSET), y)
TEXT_OFFSET := $(shell awk 'BEGIN {srand(); printf "0x%03x000\n", int(512 * rand())}')
else
TEXT_OFFSET := 0x00080000
endif
```
如果没有配置随机TEXT_OFFSET则TEXT_OFFSET为0x0008 0000，该值为kernel image被加到RAM中，距离RAM起始地址的偏移。

但是我们在head.S中并没有看到\_text这个标号。从上面的链接脚本中我们可以看到\_text是定义在链接脚本中，下来紧接着就是HEAD_TEXT，HEAD_TEXT是一个宏展开后如下：

```c
/* Section used for early init (in .S files) */
#define HEAD_TEXT  *(.head.text) /*vmlinux.lds.h*/
```

\_text = .紧接着是 *(.head.text)，所以\_text的地址可以 *(.head.text)的第一条指令的地址是相同的，.head.text段的定义是通过宏\__HEAD定义的，如下：

```c
#define __HEAD		.section	".head.text","ax" /*init.h*/
```

\__HEAD宏也仅在head.S文件中有，如下：

```assembly
__HEAD
	/*
	 * DO NOT MODIFY. Image header expected by Linux boot-loaders.
	 */
#ifdef CONFIG_EFI
efi_head:
	/*
	 * This add instruction has no meaningful effect except that
	 * its opcode forms the magic "MZ" signature required by UEFI.
	 */
	add	x13, x18, #0x16
	b	stext
#else
	b	stext
```

所以add	x13, x18, #0x16是kernel第一条执行的指令，根据上面的注释add	x13, x18, #0x16这条指令是没有什么用的，所以stext算是真正的“第一条指令”。

附上vmlinux开始部分的反汇编代码：

```assembly
vmlinux:     file format elf64-littleaarch64

Disassembly of section .head.text:

ffff800000080000 <_text>: 
ffff800000080000:       91005a4d        add     x13, x18, #0x16
ffff800000080004:       140003ff        b       ffff800000081000 <stext>
ffff800000080008:       00080000        .word   0x00080000
ffff80000008000c:       00000000        .word   0x00000000
ffff800000080010:       00eb5000        .word   0x00eb5000
```
可以看到\_text和 add     x13, x18, #0x16的地址是一样的。

### kernel header

根据Documentation/arm64/booting.txt

 The decompressed kernel image contains a 64-byte header as follows:
```
   u32 code0;                    /* Executable code */
   u32 code1;                    /* Executable code */
   u64 text_offset;              /* Image load offset, little endian */
   u64 image_size;               /* Effective Image size, little endian */
   u64 flags;                    /* kernel flags, little endian */
   u64 res2      = 0;            /* reserved */
   u64 res3      = 0;            /* reserved */
   u64 res4      = 0;            /* reserved */
   u32 magic     = 0x644d5241;   /* Magic number, little endian, "ARM\x64" */
   u32 res5;                     /* reserved (used for PE COFF offset) */
```
下面是对上面字段的说明：

Header notes:

- As of v3.17, all fields are little endian unless stated otherwise.

  > *对于v3.17，所有的字段都是小端，除非另有说明*

- code0/code1 are responsible for branching to stext.

  > *code0/code1用于跳转到stext.*

- when booting through EFI, code0/code1 are initially skipped. res5 is an offset to the PE header and the PE header has the EFI entry point (efi_stub_entry).  When the stub has done its work, it jumps to code0 to resume the normal boot process.

  > *通过EFI启动，code0/code1被跳过，res5是一个到PE header的偏移，PE header有entry point (efi_stub_entry)，当stub完成自己的功过，然后跳转到code0恢复正常的启动过程。*

- Prior to v3.17, the endianness of text_offset was not specified.  In these cases image_size is zero and text_offset is 0x80000 in the endianness of the kernel.  Where image_size is non-zero image_size is little-endian and must be respected.  Where image_size is zero,text_offset can be assumed to be 0x80000.

  > *v3.17之前，text_offset的字节序是没有指定的。在这个情况下，image_size是0，text_offset是0x80000，以kernel的字节序。在image_size是非零，image_size是小端。在image_size是0，text_offset可以被假定为0x80000*

- The flags field (introduced in v3.17) is a little-endian 64-bit field composed as follows:
  Bit 0:        Kernel endianness.  1 if BE, 0 if LE.
  Bits 1-63:    Reserved.

  > *flag字段(v3.17引入)是64bit小端格式，组成如下:*
  > *bit 0: kernel字节序。 1 大端，0 小端。*
  > *bits 1-63：保留*

- When image_size is zero, a bootloader should attempt to keep as much memory as possible free for use by the kernel immediately after the end of the kernel image. The amount of space required will vary depending on selected features, and is effectively unbound.

  > *当image_sizes是0，bootloader应该应该在kernel image的end端保留足够多的memory，数量的多少依赖与选择的features，实际上是没有限制的*

The Image must be placed text_offset bytes from a 2MB aligned base address near the start of usable system RAM and called there. Memory below that base address is currently unusable by Linux, and therefore it is strongly recommended that this location is the start of system RAM. At least image_size bytes from the start of the image must be free for use by the kernel.

> image应该被放在距离起始地址 text_offset的地方，起始地址是2M对齐的，接近system RAM的开始地方。linux现在没有使用base address一下的地址，强烈建议该位置是system RAM的开始地址。至少从image开始image_size字节必须是free的可以被kenrel使用。

kernel header并没有使用什么工具生成，而是直接在代码中指定的：

```assembly
	__HEAD

	/*
	 * DO NOT MODIFY. Image header expected by Linux boot-loaders.
	 */
#ifdef CONFIG_EFI
efi_head:
	/*
	 * This add instruction has no meaningful effect except that
	 * its opcode forms the magic "MZ" signature required by UEFI.
	 */
	add	x13, x18, #0x16		/*code0*/		
	b	stext			    /*code1*/
#else
	b	stext				// branch to kernel start, magic  /*code0*/
	.long	0				// reserved						/*code0*/
#endif
	/*text_offset*/
	.quad	_kernel_offset_le		// Image load offset from start of RAM, little-endian
	/*image_size */
	.quad	_kernel_size_le			// Effective size of kernel image, little-endian
	/* flags*/
	.quad	_kernel_flags_le		// Informative flags, little-endian
	.quad	0				// reserved	/*res2*/
	.quad	0				// reserved /*res3*/
	.quad	0				// reserved /*res4*/
	/* magic */
	.byte	0x41				// Magic number, "ARM\x64"
	.byte	0x52
	.byte	0x4d
	.byte	0x64
#ifdef CONFIG_EFI	/*res5*/
	.long	pe_header - efi_head		// Offset to the PE header.
#else
	.word	0				// reserved	/*res5*/
#endif
```

### bootloader启动kernel准备工作

    1. The requirements are:

    2. MMU = off, D-cache = off, I-cache = on or off,

    3. x0 = physical address to the FDT blob.
### kernel正式启动

```assembly
	/*#define __HEAD .section	".head.text","ax"*/
	__HEAD 
	/*
	 * DO NOT MODIFY. Image header expected by Linux boot-loaders.
	 */
#ifdef CONFIG_EFI
efi_head:
	/*
	 * This add instruction has no meaningful effect except that
	 * its opcode forms the magic "MZ" signature required by UEFI.
	 */
	add	x13, x18, #0x16
	b	stext
#else
	b	stext				// branch to kernel start, magic
	.long	0				// reserved
#endif
```

直接跳转到了stext

#### stext

```assembly
ENTRY(stext)
	mov	x21, x0				// x21=FDT
	bl	el2_setup			// Drop to EL1, w20=cpu_boot_mode
	bl	__calc_phys_offset		// x24=PHYS_OFFSET, x28=PHYS_OFFSET-PAGE_OFFSET
	bl	set_cpu_boot_mode_flag
	mrs	x22, midr_el1			// x22=cpuid
	mov	x0, x22
	bl	lookup_processor_type
	mov	x23, x0				// x23=current cpu_table
	/*
	 * __error_p may end up out of range for cbz if text areas are
	 * aligned up to section sizes.
	 */
	cbnz	x23, 1f				// invalid processor (x23=0)?，如果x23不等于0，跳到1f
	b	__error_p
1:
	bl	__vet_fdt					//检验fdt的起始地址是否符合规范
	bl	__create_page_tables		// x25=TTBR0, x26=TTBR1
	/*
	 * The following calls CPU specific code in a position independent
	 * manner. See arch/arm64/mm/proc.S for details. x23 = base of
	 * cpu_info structure selected by lookup_processor_type above.
	 * On return, the CPU will be ready for the MMU to be turned on and
	 * the TCR will have been set.
	 */
	ldr	x27, __switch_data		// address to jump to after
						// MMU has been enabled
	adrp	lr, __enable_mmu		// return (PIC) address，得到__enable_mmu地址的[63:12]
	add	lr, lr, #:lo12:__enable_mmu	 //lr + __enable_mmu地址的[11:0]构成完整的地址
	ldr	x12, [x23, #CPU_INFO_SETUP]	//x23为返回的cpu_tabl的地址
	add	x12, x12, x28			// __virt_to_phys
	br	x12				// initialise processor,跳转到x12寄存器中的地址处，既__cpu_setup
ENDPROC(stext)
```

 #### el2_setup

```assembly
ENTRY(el2_setup)
	mrs	x0, CurrentEL	/*读取异常级别*/
	cmp	x0, #CurrentEL_EL2
	b.ne	1f	/*如果不是EL2跳转到1标号*/
	mrs	x0, sctlr_el2
CPU_BE(	orr	x0, x0, #(1 << 25)	)	// Set the EE bit for EL2
CPU_LE(	bic	x0, x0, #(1 << 25)	)	// Clear the EE bit for EL2
	msr	sctlr_el2, x0
	b	2f

/*从el1启动kenrel*/
1:	mrs	x0, sctlr_el1
CPU_BE(	orr	x0, x0, #(3 << 24)	)	// Set the EE and E0E bits for EL1
CPU_LE(	bic	x0, x0, #(3 << 24)	)	// Clear the EE and E0E bits for EL1
	msr	sctlr_el1, x0
	mov	w20, #BOOT_CPU_MODE_EL1		// This cpu booted in EL1 
	isb
	ret

/*从el2启动kernel*/
	/* Hyp configuration. */
2:	mov	x0, #(1 << 31)			// 64-bit EL1 ，跳转到el1是64bit
	msr	hcr_el2, x0

	/* Generic timers. */
	mrs	x0, cnthctl_el2
	orr	x0, x0, #3			// Enable EL1 physical timers
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr		// Clear virtual offset

#ifdef CONFIG_ARM_GIC_V3
	/* GICv3 system register access */
	mrs	x0, id_aa64pfr0_el1
	ubfx	x0, x0, #24, #4
	cmp	x0, #1
	b.ne	3f

	mrs_s	x0, ICC_SRE_EL2
	orr	x0, x0, #ICC_SRE_EL2_SRE	// Set ICC_SRE_EL2.SRE==1
	orr	x0, x0, #ICC_SRE_EL2_ENABLE	// Set ICC_SRE_EL2.Enable==1
	msr_s	ICC_SRE_EL2, x0
	isb					// Make sure SRE is now set
	msr_s	ICH_HCR_EL2, xzr		// Reset ICC_HCR_EL2 to defaults

3:
#endif

	/* Populate ID registers. */
	mrs	x0, midr_el1
	mrs	x1, mpidr_el1
	msr	vpidr_el2, x0
	msr	vmpidr_el2, x1

	/* sctlr_el1 */
	mov	x0, #0x0800			// Set/clear RES{1,0} bits
CPU_BE(	movk	x0, #0x33d0, lsl #16	)	// Set EE and E0E on BE systems
CPU_LE(	movk	x0, #0x30d0, lsl #16	)	// Clear EE and E0E on LE systems
	msr	sctlr_el1, x0

	/* Coprocessor traps. */
	mov	x0, #0x33ff
	msr	cptr_el2, x0			// Disable copro. traps to EL2

#ifdef CONFIG_COMPAT
	msr	hstr_el2, xzr			// Disable CP15 traps to EL2
#endif

	/* Stage-2 translation */
	msr	vttbr_el2, xzr

	/* Hypervisor stub */
	adrp	x0, __hyp_stub_vectors	/*el2异常向量*/
	add	x0, x0, #:lo12:__hyp_stub_vectors
	msr	vbar_el2, x0

	/*切换到el1，返回的地址是lr中地址，既__calc_phys_offset的地址*/
	/* spsr */
	mov	x0, #(PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT |\
		      PSR_MODE_EL1h)
	msr	spsr_el2, x0
	msr	elr_el2, lr
	mov	w20, #BOOT_CPU_MODE_EL2		// This CPU booted in EL2 /*将启动模式保存到w20寄存器*/
	eret
ENDPROC(el2_setup)
```

#### __calc_phys_offset

```
/*
 * Calculate the start of physical memory.
 */
__calc_phys_offset:
	adr	x0, 1f			//使用adr指令获得1 lable当前的物理地址
	ldp	x1, x2, [x0]	//从x0读取两个64bit值到x1，和x2
	/*
    *x1为1标号对应的虚拟地址，x0为1 标号对应的物理地址。
    *x0 - x1 物理地址和虚拟地址的一个offset
    *x2为kernel image的虚拟地址，x2 + x29就可以计算出，kernel image被加载的物理地址。
    */
	sub	x28, x0, x1		// x28 = PHYS_OFFSET - PAGE_OFFSET 
	add	x24, x2, x28	// x24 = PHYS_OFFSET
	ret
ENDPROC(__calc_phys_offset)

	.align 3
1:	.quad	.
	.quad	PAGE_OFFSET
```
##### PAGE_OFFSET & VA_BITS
```assembly
/*memory.h*/
/*
 * PAGE_OFFSET - the virtual address of the start of the kernel image (top
 *		 (VA_BITS - 1))
 * VA_BITS - the maximum number of bits for virtual addresses.
 * TASK_SIZE - the maximum size of a user space task.
 * TASK_UNMAPPED_BASE - the lower boundary of the mmap VM area.
 * The module space lives between the addresses given by TASK_SIZE
 * and PAGE_OFFSET - it must be within 128MB of the kernel text.
 */
#define VA_BITS			(CONFIG_ARM64_VA_BITS)	/*和内涵配置有关，本文中为48bit*/
#define PAGE_OFFSET		(UL(0xffffffffffffffff) << (VA_BITS - 1))  //VA_BITS = 48
```

#### set_cpu_boot_mode_flag

```
/*
 * Sets the __boot_cpu_mode flag depending on the CPU boot mode passed
 * in x20. See arch/arm64/include/asm/virt.h for more info.
 */
ENTRY(set_cpu_boot_mode_flag)
	ldr	x1, =__boot_cpu_mode		// Compute __boot_cpu_mode
	add	x1, x1, x28					//获得__boot_cpu_mode的物理地址
	cmp	w20, #BOOT_CPU_MODE_EL2		//比较是否el2启动，刚开始都是从从el2启动？
	b.ne	1f						//不等于el2，则表示已经启动到了el1
	add	x1, x1, #4
1:	str	w20, [x1]			// This CPU has booted in EL1
	dmb	sy
	dc	ivac, x1			// Invalidate potentially stale cache line
	ret
ENDPROC(set_cpu_boot_mode_flag)

/*
 * We need to find out the CPU boot mode long after boot, so we need to
 * store it in a writable variable.
 *
 * This is not in .bss, because we set it sufficiently early that the boot-time
 * zeroing of .bss would clobber it.
 */
	.pushsection	.data..cacheline_aligned
	.align	L1_CACHE_SHIFT
ENTRY(__boot_cpu_mode)
	.long	BOOT_CPU_MODE_EL2
	.long	0
	.popsection

```

#### lookup_processor_type

```assembly
 #define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))
 DEFINE(CPU_INFO_SZ,		sizeof(struct cpu_info));

ENTRY(lookup_processor_type)
	adr	x1, __lookup_processor_type_data	/*x0保存有CPUID*/
	ldp	x2, x3, [x1]
	sub	x1, x1, x2			// get offset between VA and PA
	add	x3, x3, x1			// convert VA to PA
1:
	ldp	w5, w6, [x3]			// load cpu_id_val and cpu_id_mask
	cbz	w5, 2f				// end of list?，如果w5为0，跳转到2
	and	w6, w6, w0			//w6和w0与，w0保存有CPUID
	cmp	w5, w6
	b.eq	3f
	add	x3, x3, #CPU_INFO_SZ
	b	1b
2:
	mov	x3, #0				// unknown processor
3:
	mov	x0, x3		/*返回cpu_table数组中对应cpu的地址*/
	ret
ENDPROC(lookup_processor_type)

	.align	3
	.type	__lookup_processor_type_data, %object
__lookup_processor_type_data:
	.quad	.
	.quad	cpu_table	/*cpu_table定义在arch\arm64\kernel\cputable.c*/
	.size	__lookup_processor_type_data, . - __lookup_processor_type_data
```

#####  cpu_table

```c
struct cpu_info {
	unsigned int	cpu_id_val;
	unsigned int	cpu_id_mask;
	const char	*cpu_name;
	unsigned long	(*cpu_setup)(void);
};

struct cpu_info cpu_table[] = {
	{
		.cpu_id_val	= 0x000f0000,
		.cpu_id_mask	= 0x000f0000,
		.cpu_name	= "AArch64 Processor",
		.cpu_setup	= __cpu_setup,
	},
	{ /* Empty */ },
};
```

#### __create_page_tables

```assembly
/*
 * Setup the initial page tables. We only setup the barest amount which is
 * required to get the kernel running. The following sections are required:
 *   - identity mapping to enable the MMU (low address, TTBR0)
 *   - first few MB of the kernel linear mapping to jump to once the MMU has
 *     been enabled, including the FDT blob (TTBR1)
 *   - pgd entry for fixed mappings (TTBR1)
 */
__create_page_tables:
	/*pgtbl为宏，定义在arch\arm64\kernel\head.S*/
	pgtbl	x25, x26, x28	// idmap_pg_dir and swapper_pg_dir addresses，
	mov	x27, lr /*x27保存返回地址*/

	/*
	 * Invalidate the idmap and swapper page tables to avoid potential
	 * dirty cache lines being evicted.
	 */
	mov	x0, x25
	add	x1, x26, #SWAPPER_DIR_SIZE
	bl	__inval_cache_range

	/*
	 * Clear the idmap and swapper page tables.
	 */
	mov	x0, x25
	add	x6, x26, #SWAPPER_DIR_SIZE
1:	stp	xzr, xzr, [x0], #16
	stp	xzr, xzr, [x0], #16
	stp	xzr, xzr, [x0], #16
	stp	xzr, xzr, [x0], #16
	cmp	x0, x6
	b.lo	1b


	ldr	x7, =MM_MMUFLAGS  /*0x711,  0111 0001 0001*/

	/*
	 * Create the identity mapping.identity mapping是虚拟地址等于物理地址的映射，该映射是
	 * ARM Architecture Reference Manual建议的,如下： 
	 * If the PA of the software that enables or disables a particular stage of address 		 * translation differs from its VA,speculative instruction fetching can cause 				 * complications. ARM strongly recommends that the PA and VA of any software that enables 	   * or disables a stage of address translation are identical if that stage of translation 	    * controls translations that apply to the software currently being executed.
	 */
	mov	x0, x25				// idmap_pg_dir
	ldr	x3, =KERNEL_START
	add	x3, x3, x28			// __pa(KERNEL_START)
	create_pgd_entry x0, x3, x5, x6
	ldr	x6, =KERNEL_END
	mov	x5, x3				// __pa(KERNEL_START)
	add	x6, x6, x28			// __pa(KERNEL_END)
	create_block_map x0, x7, x3, x5, x6

	/*
	 * Map the kernel image (starting with PHYS_OFFSET).以映射kernel为例进行分析
	 */
	mov	x0, x26				// swapper_pg_dir
	mov	x5, #PAGE_OFFSET
	
	create_pgd_entry x0, x5, x3, x6
	ldr	x6, =KERNEL_END
	mov	x3, x24				// phys offset
	create_block_map x0, x7, x3, x5, x6

	/*
	 * Map the FDT blob (maximum 2MB; must be within 512MB of
	 * PHYS_OFFSET).
	 */
	mov	x3, x21				// FDT phys address
	and	x3, x3, #~((1 << 21) - 1)	// 2MB aligned
	mov	x6, #PAGE_OFFSET
	sub	x5, x3, x24			// subtract PHYS_OFFSET
	tst	x5, #~((1 << 29) - 1)		// within 512MB?
	csel	x21, xzr, x21, ne		// zero the FDT pointer
	b.ne	1f
	add	x5, x5, x6			// __va(FDT blob)
	add	x6, x5, #1 << 21		// 2MB for the FDT blob
	sub	x6, x6, #1			// inclusive range
	create_block_map x0, x7, x3, x5, x6
1:
	/*
	 * Since the page tables have been populated with non-cacheable
	 * accesses (MMU disabled), invalidate the idmap and swapper page
	 * tables again to remove any speculatively loaded cache lines.
	 */
	mov	x0, x25
	add	x1, x26, #SWAPPER_DIR_SIZE
	bl	__inval_cache_range

	mov	lr, x27
	ret
ENDPROC(__create_page_tables)
	.ltorg
```

##### pgtbl宏

```assembly
	.macro	pgtbl, ttb0, ttb1, virt_to_phys
	ldr	\ttb1, =swapper_pg_dir
	ldr	\ttb0, =idmap_pg_dir
	add	\ttb1, \ttb1, \virt_to_phys
	add	\ttb0, \ttb0, \virt_to_phys
	.endm
```

pgtbl	x25, x26, x28展开：

```assembly
	ldr	x26, =swapper_pg_dir
	ldr	x25, =idmap_pg_dir
	add	x26, x26, x28	/*x26保存swapper_pg_dir的物理地址*/
	add	x25, x25, x28	/*x25保存idmap_pg_dir的物理地址*/
```

x28为虚拟得之和物理地址的偏移量，通过该offset可以得到虚拟地址对应的物理地址。idmap_pg_dir和swapper_pg_dir定义在vmlinux.lds.S中

```assembly
/*arch/arm64/include/asm/page.h*/
/*CONFIG_ARM64_PGTABLE_LEVELS定义在.config中，在该分析的kernel中配置为4*/
#define SWAPPER_PGTABLE_LEVELS	(CONFIG_ARM64_PGTABLE_LEVELS - 1) 

#define SWAPPER_DIR_SIZE	(SWAPPER_PGTABLE_LEVELS * PAGE_SIZE)
#define IDMAP_DIR_SIZE		(SWAPPER_DIR_SIZE)

. = ALIGN(PAGE_SIZE);
idmap_pg_dir = .;
. += IDMAP_DIR_SIZE;
swapper_pg_dir = .;
. += SWAPPER_DIR_SIZE;
_end = .;/*kernel image的结束地址*/
```

##### map the kernel image

以映射kernel image为例进行分析

```assembly
#define KERNEL_END	_end /*kernel image的结束地址_end定义在链接脚本中*/
	/*
	 * Map the kernel image (starting with PHYS_OFFSET).以映射kernel为例进行分析
	 */
	mov	x0, x26				// swapper_pg_dir
	mov	x5, #PAGE_OFFSET
	
	create_pgd_entry x0, x5, x3, x6
	ldr	x6, =KERNEL_END		//
	/*
	*x24保存为PAGE_OFFSET的物理地址，
	*在__calc_phys_offset计算的,既kernel image被放到内存中的地址
	*/
	mov	x3, x24				// phys offset
	create_block_map x0, x7, x3, x5, x6
```

##### PAGE_OFFSET

PAGE_OFFSET定义如下，定义在arch\arm64\include\asm\memory.h

```c
/*
 * PAGE_OFFSET - the virtual address of the start of the kernel image (top
 *		 (VA_BITS - 1))
 * VA_BITS - the maximum number of bits for virtual addresses.
 * TASK_SIZE - the maximum size of a user space task.
 * TASK_UNMAPPED_BASE - the lower boundary of the mmap VM area.
 * The module space lives between the addresses given by TASK_SIZE
 * and PAGE_OFFSET - it must be within 128MB of the kernel text.
 */
#define VA_BITS			(CONFIG_ARM64_VA_BITS) /*CONFIG_ARM64_VA_BITS = 48*/
/*kernel image虚拟地址映射的起始地址*/
#define PAGE_OFFSET		(UL(0xffffffffffffffff) << (VA_BITS - 1))
```

##### create_pgd_entry

```
/*
 * Macro to populate the PGD (and possibily PUD) for the corresponding
 * block entry in the next level (tbl) for the given virtual address.
 *
 * Preserves:	tbl, next, virt
 * Corrupts:	tmp1, tmp2
 */
	.macro	create_pgd_entry, tbl, virt, tmp1, tmp2
	create_table_entry \tbl, \virt, PGDIR_SHIFT, PTRS_PER_PGD, \tmp1, \tmp2
#if SWAPPER_PGTABLE_LEVELS == 3
	create_table_entry \tbl, \virt, TABLE_SHIFT, PTRS_PER_PTE, \tmp1, \tmp2
#endif
	.endm
```

##### create_table_entry

```
/*
 * Macro to create a table entry to the next page.
 *
 *	tbl:	page table address
 *	virt:	virtual address
 *	shift:	#imm page table shift
 *	ptrs:	#imm pointers per table page
 *
 * Preserves:	virt
 * Corrupts:	tmp1, tmp2
 * Returns:	tbl -> next level table page address
 */
	.macro	create_table_entry, tbl, virt, shift, ptrs, tmp1, tmp2
	lsr	\tmp1, \virt, #\shift
	and	\tmp1, \tmp1, #\ptrs - 1	// table index
	add	\tmp2, \tbl, #PAGE_SIZE
	orr	\tmp2, \tmp2, #PMD_TYPE_TABLE	// address of next table and entry type
	str	\tmp2, [\tbl, \tmp1, lsl #3]
	add	\tbl, \tbl, #PAGE_SIZE		// next level table page
	.endm
```


该分析的kernel配置为4KB的粒度，采用四级页表，所以页表的格式如下：

Translation table lookup with 4KB pages，由于armV8a只支持48bit地址，如果没有开启tag，[63:48]都是一样的，全是0或1.在内核空间全是1，在用户空间全是0.全是0则用TTBR0作为页表的起始地址，全是1则使用TTBR1作为页表的起始地址。

**内核启动时进行的地址映射与下面的有一点区别，内核使用了block映射，所以没有L3**。

![table_format](./table_format.JPG)

kernel映射create_pgd_entry展开，create_pgd_entry只填写了前两级页表，最后映射到的物理地址由create_block_map决定

```assembly
create_pgd_entry x0, x5, x3, x6
--->
	create_table_entry x0, x5, PGDIR_SHIFT, PTRS_PER_PGD, x3, x6	//x5:虚拟地址
	--->
		lsr	x3, x5, #PGDIR_SHIFT		//x5虚拟地址右移PGDIR_SHIFT
		/*
		 * table index，获取该虚拟地址对应的页表index。 
		 * PTRS_PER_PGD = 512,PTRS_PER_PGD表示该级页表总共有多少个entry，
		 * 减1用于生产掩码。
		 */
		and	x3, x3, #PTRS_PER_PGD - 1	// table index
		/*
		*x6现在为下一级页表的起始地址，因为PG级页表总共512项，每一项占8字节，
		*所以刚好占用4K字节，即一页。
		*/
		add	x6, x0, #PAGE_SIZE
		/*PMD_TYPE_TABLE = 3表示下一级页表为一个页表描述符,由ARM参考手册规定*/
		orr	x6, x6, #PMD_TYPE_TABLE		// address of next table and entry type
		/*把下一级页表的起始地址写入该虚拟地址对应的PGD索引中，一个页表项占8个字节，所以左移3位*/
		str	x6, [x0, x3, lsl #3]
		/*x0现在为下一级页表的起始地址，原来为PGD起始地址，既swapper_pg_dir*/
		add	x0, x0, #PAGE_SIZE			// next level table page

	create_table_entry x0, x5, TABLE_SHIFT, PTRS_PER_PTE, x3, x6 /*x5为虚拟地址*/
	--->
		lsr	x3, x5, #TABLE_SHIFT		//TABLE_SHIFT = 30,//x5虚拟地址右移TABLE_SHIFT
		and	x3, x3, #PTRS_PER_PTE - 1	// table index,得带该虚拟地址在下一级页表中的索引
		/*
		*x6现在为下一级页表的起始地址，因为下一级页表总共512项，每一项占8字节，
		*所以刚好占用4K字节，即一页。
		*/
		add	x6, x0, #PAGE_SIZE
        /*PMD_TYPE_TABLE = 3表示下一级页表为一个页表描述符,由ARM参考手册规定*/
		orr	x6, x6, #PMD_TYPE_TABLE		// address of next table and entry type
		/*把下一级页表的起始地址写入该虚拟地址对应的页表索引中，一个页表项占8个字节，所以左移3位*/
		str	x6, [x0, x3, lsl #3]
		add	x0, x0, #PAGE_SIZE			// next level table page
```

```c
/*\arch\arm64\include\asm\pgtable-hwdef.h*/

/*
 * PGDIR_SHIFT determines the size a top-level page table entry can map
 * (depending on the configuration, this level can be 0, 1 or 2).
 */
/*(12 -3)*4 +3 = 39*/
#define PGDIR_SHIFT		((PAGE_SHIFT - 3) * CONFIG_ARM64_PGTABLE_LEVELS + 3) 
#define PGDIR_SIZE		(_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))
/*PGD中页表项的数目1 << (48 - 12) = 512*/
#define PTRS_PER_PGD		(1 << (VA_BITS - 39)) 

/*
 * PUD_SHIFT determines the size a level 1 page table entry can map.
 */
#if CONFIG_ARM64_PGTABLE_LEVELS > 3
#define PUD_SHIFT		((PAGE_SHIFT - 3) * 3 + 3)
#define PUD_SIZE		(_AC(1, UL) << PUD_SHIFT)
#define PUD_MASK		(~(PUD_SIZE-1))
#define PTRS_PER_PUD		PTRS_PER_PTE
#endif

#define PTRS_PER_PTE		(1 << (PAGE_SHIFT - 3)) /* 1 << 9 = 512*/

/*arch\arm64\kernel\head.S*/
#define TABLE_SHIFT	PUD_SHIFT /* (12 - 3) * 3 + 3 = 30 */
```
##### create_block_map
```
/*
 * Macro to populate block entries in the page table for the start..end
 * virtual range (inclusive).
 *
 * Preserves:	tbl, flags
 * Corrupts:	phys, start, end, pstate
 */
	.macro	create_block_map, tbl, flags, phys, start, end
	lsr	\phys, \phys, #BLOCK_SHIFT
	lsr	\start, \start, #BLOCK_SHIFT
	and	\start, \start, #PTRS_PER_PTE - 1	// table index
	orr	\phys, \flags, \phys, lsl #BLOCK_SHIFT	// table entry
	lsr	\end, \end, #BLOCK_SHIFT
	and	\end, \end, #PTRS_PER_PTE - 1		// table end index
9999:	str	\phys, [\tbl, \start, lsl #3]		// store the entry
	add	\start, \start, #1			// next entry
	add	\phys, \phys, #BLOCK_SIZE		// next block
	cmp	\start, \end
	b.ls	9999b
	.endm
```

create_block_map x0, x7, x3, x5, x6展开如下：

```assembly
/*F:arch\arm64\include\asm\pgtable-hwdef.h*/
#define PMD_SHIFT		((PAGE_SHIFT - 3) * 2 + 3) /*(12-3)*2+3 = 21*/
#define SECTION_SHIFT		PMD_SHIFT
#define SECTION_SIZE		(_AC(1, UL) << SECTION_SHIFT)
#define SECTION_MASK		(~(SECTION_SIZE-1))

#define BLOCK_SHIFT	SECTION_SHIFT
#define BLOCK_SIZE	SECTION_SIZE
#define TABLE_SHIFT	PUD_SHIFT

/*create_block_map宏展开*/		
/*x0=swapper_pg_dir,x7=MM_MMUFLAGS,x3=PAGE_OFFSET,x5=PAGE_OFFSET x6=KERNEL_END*/
create_block_map x0, x7, x3, x5, x6  
--->
	create_block_map, tbl, flags, phys, start, end
	展开如下：
		/*
		*BLOCK映射，一个页表项映射BLOCK物理地址大小，所以物理地址都是BLOCK的整数倍，
		*所以右移BLOCK_SHIFT
		*/
		lsr	x3, x3, #BLOCK_SHIFT			//BLOCK_SHIFT = 21
		lsr	x5, x5, #BLOCK_SHIFT			//虚拟地址右移BLOCK_SHIFT，获取页表中的索引
		and	x5, x5, #PTRS_PER_PTE - 1		// table index,起始虚拟地址对应的索引
		orr	x3, x7, x3, lsl #BLOCK_SHIFT	// table entry, 配置block地址属性
		lsr	x6, x6, #BLOCK_SHIFT			//因为是BLOCK映射，随意虚拟地址结束是BLOCK整数倍
		and	x6, x6, #PTRS_PER_PTE - 1		// table end index，虚拟地址结束的索引
	9999:
		/*把虚拟地址对应的物理地址写入到该虚拟地址对应的索引地址中，一个entry占8个字节，所以左移3*/
		str	x3, [x0, x5, lsl #3]			// store the entry
		/*索引加1*/
		add	x5, x5, #1						// next entry
		/*
		 * BLOCK_SIZE物理地址增加一个BLOCK_SIZE,BLOCK_SIZE = 1<<BLOCK_SHIFT = 2M,
		 * 因为一个entry映射一个BLOCK_SIZE
		 */
		add	x3, x3, #BLOCK_SIZE				// next block， 

		/*
		 * x5是起始虚拟地址对应的索引， 
		 * x6是结束虚拟地址对应的索引
		 * 一直循环知道将所有虚拟地址映射完毕，该映射的虚拟地址和物理地址都是连续的。
		 */
		cmp	x5, x6							
		b.ls	9999b	
        /*
		* 由于block描述符输出的是物理地址，所以实现了虚拟地址到物理地址的映射过程
		*/
```

页表映射如下图：

![页表在内存中的布局](swapper_pg_dir_mmu_table.jpg)

#### __cpu_setup

mmu配置相关，后面分析

```assembly
/*\arch\arm64\mm\proc.S*/
/*
 *	__cpu_setup
 *
 *	Initialise the processor for turning the MMU on.  Return in x0 the
 *	value of the SCTLR_EL1 register.
 */
ENTRY(__cpu_setup)
	ic	iallu				// I+BTB cache invalidate
	tlbi	vmalle1is			// invalidate I + D TLBs
	dsb	ish

	mov	x0, #3 << 20
	msr	cpacr_el1, x0			// Enable FP/ASIMD
	msr	mdscr_el1, xzr			// Reset mdscr_el1
	/*
	 * Memory region attributes for LPAE:
	 *
	 *   n = AttrIndx[2:0]
	 *			n	MAIR
	 *   DEVICE_nGnRnE	000	00000000
	 *   DEVICE_nGnRE	001	00000100
	 *   DEVICE_GRE		010	00001100
	 *   NORMAL_NC		011	01000100
	 *   NORMAL		100	11111111
	 */
	ldr	x5, =MAIR(0x00, MT_DEVICE_nGnRnE) | \
		     MAIR(0x04, MT_DEVICE_nGnRE) | \
		     MAIR(0x0c, MT_DEVICE_GRE) | \
		     MAIR(0x44, MT_NORMAL_NC) | \
		     MAIR(0xff, MT_NORMAL)
	msr	mair_el1, x5
	/*
	 * Prepare SCTLR
	 */
	adr	x5, crval
	ldp	w5, w6, [x5]
	mrs	x0, sctlr_el1
	bic	x0, x0, x5			// clear bits
	orr	x0, x0, x6			// set bits
	/*
	 * Set/prepare TCR and TTBR. We use 512GB (39-bit) address range for
	 * both user and kernel.
	 */
	ldr	x10, =TCR_TxSZ(VA_BITS) | TCR_CACHE_FLAGS | TCR_SMP_FLAGS | \
			TCR_TG_FLAGS | TCR_ASID16 | TCR_TBI0
	/*
	 * Read the PARange bits from ID_AA64MMFR0_EL1 and set the IPS bits in
	 * TCR_EL1.
	 */
	mrs	x9, ID_AA64MMFR0_EL1
	bfi	x10, x9, #32, #3
	msr	tcr_el1, x10
	/*lr寄存器中保存的为__enable_mmu地址，所以返回到__enable_mmu处执行*/
	ret					// return to head.S
ENDPROC(__cpu_setup)
```

#### __enable_mmu

```assembly
/*
 * Setup common bits before finally enabling the MMU. Essentially this is just
 * loading the page table pointer and vector base registers.
 *
 * On entry to this code, x0 must contain the SCTLR_EL1 value for turning on
 * the MMU.
 */
__enable_mmu:
	ldr	x5, =vectors
	msr	vbar_el1, x5			//配置异常基地址
	msr	ttbr0_el1, x25			// load TTBR0
	msr	ttbr1_el1, x26			// load TTBR1
	isb
	b	__turn_mmu_on
ENDPROC(__enable_mmu)
```

#### __turn_mmu_on

```assembly
/*
 * Enable the MMU. This completely changes the structure of the visible memory
 * space. You will not be able to trace execution through this.
 *
 *  x0  = system control register
 *  x27 = *virtual* address to jump to upon completion
 *
 * other registers depend on the function called upon completion
 *
 * We align the entire function to the smallest power of two larger than it to
 * ensure it fits within a single block map entry. Otherwise were PHYS_OFFSET
 * close to the end of a 512MB or 1GB block we might require an additional
 * table to map the entire function.
 */
	.align	4
__turn_mmu_on:
	msr	sctlr_el1, x0
	isb
	br	x27  //x27 = __switch_data，跳转到__switch_data
ENDPROC(__turn_mmu_on)
```

#### __switch_data

`__switch_data`既`__mmap_switched`的地址，所以实际跳转到`__mmap_switched`

```assembly
/*\arch\arm64\include\asm\thread_info.h*/
#define THREAD_SIZE		16384	/*4个页（如果也为4K）*/
#define THREAD_START_SP		(THREAD_SIZE - 16)

__switch_data:
	/*.quad占用8个字节*/
	.quad	__mmap_switched
	.quad	__bss_start			// x6
	.quad	__bss_stop			// x7
	.quad	processor_id			// x4
	.quad	__fdt_pointer			// x5
	.quad	memstart_addr			// x6
	/*init_thread_union定义在\init\init_task.c */
	.quad	init_thread_union + THREAD_START_SP // sp

/*
 * The following fragment of code is executed with the MMU on in MMU mode, and
 * uses absolute addresses; this is not position independent.
 */
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
	mov	sp, x16				//初始化堆栈指针
	str	x22, [x4]			// Save processor ID
	str	x21, [x5]			// Save FDT pointer
	str	x24, [x6]			// Save PHYS_OFFSET
	mov	x29, #0
	b	start_kernel		//跳转到c语言实现的函数start_kernel	
ENDPROC(__mmap_switched)
```

#### init_thread_union

```c

/* Attach to the init_task data structure for proper alignment */
#define __init_task_data __attribute__((__section__(".data..init_task")))

union thread_union init_thread_union __init_task_data =
	{ INIT_THREAD_INFO(init_task) };

```

init_thread_union是也对齐的。在vmlinux.lds.S中定义

```assembly
#define INIT_TASK_DATA(align)						\
	. = ALIGN(align);						\
	*(.data..init_task)

#define RW_DATA_SECTION(cacheline, pagealigned, inittask)		\
	. = ALIGN(PAGE_SIZE);						\
	.data : AT(ADDR(.data) - LOAD_OFFSET) {				\
		INIT_TASK_DATA(inittask)				\
		NOSAVE_DATA						\
		PAGE_ALIGNED_DATA(pagealigned)				\
		CACHELINE_ALIGNED_DATA(cacheline)			\
		READ_MOSTLY_DATA(cacheline)				\
		DATA_DATA						\
		CONSTRUCTORS						\
	}

. = ALIGN(PAGE_SIZE);
	_data = .;
	_sdata = .;
	RW_DATA_SECTION(64, PAGE_SIZE, THREAD_SIZE)
	PECOFF_EDATA_PADDING
	_edata = .;
```

