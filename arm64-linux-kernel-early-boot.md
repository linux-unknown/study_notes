# arm64 linux kernel 启动

## 代码入口地址

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