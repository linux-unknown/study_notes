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
	cbnz	x23, 1f				// invalid processor (x23=0)?
	b	__error_p
1:
	bl	__vet_fdt
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
	adrp	lr, __enable_mmu		// return (PIC) address
	add	lr, lr, #:lo12:__enable_mmu
	ldr	x12, [x23, #CPU_INFO_SETUP]
	add	x12, x12, x28			// __virt_to_phys
	br	x12				// initialise processor
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

### __calc_phys_offset

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
### PAGE_OFFSET & VA_BITS
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

### set_cpu_boot_mode_flag

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



