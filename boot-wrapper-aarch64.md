# boot-wrapper-aarch64

***
**boot-wrapper-aarch64作为一个简单的boot loader用于启动arm64 linux，该代码会将kernel、dtb、文件系统（如果选择的话）打包成一个文件linux-system.axf。**
***
## linux-system.axf生成过程

编译过程的log如下：

make_linux_axf.sh脚本中集成了boot-wrapper-aarch64的配置如下

```shell
./configure --enable-psci=yes --host=aarch64-linux-gnu --with-kernel-dir=/home/assin/linux/linux_mainline --with-dtb=/home/assin/linux/linux_mainline/arc    h/arm64/boot/dts/arm/foundation-v8.dtb  --with-cmdline="console=ttyAMA0  earlyprintk=pl011,0x1c090000 consolelog=9 rw root=/dev/vda2"
```

assin@assin-pc:~/armv8/boot-wrapper-aarch64$ ./make_linux_axf.sh 

test -z "linux-system.axf boot.o cache.o gic.o mmu.o ns.o psci.o model.lds fdt.dtb" || rm -f linux-system.axf boot.o cache.o gic.o mmu.o ns.o psci.o model.lds fdt.dtb
configure: WARNING: you should use --build, --host, --target
checking build system type... Invalid configuration \`rw': machine `rw' not recognized
configure: error: /bin/sh ./config.sub rw failed

#### 编译单独文件过程
```c
aarch64-linux-gnu-gcc  -g -O2 -DCNTFRQ=0x01800000	 -DCPU_IDS=0x0,0x1,0x2,0x3 -DSYSREGS_BASE=0x000000001c010000 -DUART_BASE=0x000000001c090000  -DGIC_CPU_BASE=0x000000002c002000 -DGIC_DIST_BASE=0x000000002c001000 -c -o boot.o boot.S
aarch64-linux-gnu-gcc  -g -O2 -DCNTFRQ=0x01800000	 -DCPU_IDS=0x0,0x1,0x2,0x3 -DSYSREGS_BASE=0x000000001c010000 -DUART_BASE=0x000000001c090000  -DGIC_CPU_BASE=0x000000002c002000 -DGIC_DIST_BASE=0x000000002c001000 -c -o cache.o cache.S
aarch64-linux-gnu-gcc  -g -O2 -DCNTFRQ=0x01800000	 -DCPU_IDS=0x0,0x1,0x2,0x3 -DSYSREGS_BASE=0x000000001c010000 -DUART_BASE=0x000000001c090000  -DGIC_CPU_BASE=0x000000002c002000 -DGIC_DIST_BASE=0x000000002c001000 -c -o gic.o gic.S
aarch64-linux-gnu-gcc  -g -O2 -DCNTFRQ=0x01800000	 -DCPU_IDS=0x0,0x1,0x2,0x3 -DSYSREGS_BASE=0x000000001c010000 -DUART_BASE=0x000000001c090000  -DGIC_CPU_BASE=0x000000002c002000 -DGIC_DIST_BASE=0x000000002c001000 -c -o mmu.o mmu.S
aarch64-linux-gnu-gcc  -g -O2 -DCNTFRQ=0x01800000	 -DCPU_IDS=0x0,0x1,0x2,0x3 -DSYSREGS_BASE=0x000000001c010000 -DUART_BASE=0x000000001c090000  -DGIC_CPU_BASE=0x000000002c002000 -DGIC_DIST_BASE=0x000000002c001000 -c -o ns.o ns.S
aarch64-linux-gnu-gcc  -g -O2 -DCNTFRQ=0x01800000	 -DCPU_IDS=0x0,0x1,0x2,0x3 -DSYSREGS_BASE=0x000000001c010000 -DUART_BASE=0x000000001c090000  -DGIC_CPU_BASE=0x000000002c002000 -DGIC_DIST_BASE=0x000000002c001000 -c -o psci.o psci.S
```
#### 生成连接脚本model.lds

```c
aarch64-linux-gnu-gcc -E  -ansi -DPHYS_OFFSET=0x0000000080000000 -DMBOX_OFFSET=0xfff8 -DKERNEL_OFFSET=0x80000 -DFDT_OFFSET=0x08000000 -DFS_OFFSET=0x10000000 -DKERNEL=/home/assin/linux/linux_mainline/arch/arm64/boot/Image -DFILESYSTEM= -DBOOTMETHOD=psci.o -DGIC=gic.o -P -C -o model.lds model.lds.S
```
#### 修改dtb中的chosen节点和psic

先将dtb文件反编译为dts，然后添加psci节点，chosen节点

```
( /home/assin/bin/dtc -O dts -I dtb /home/assin/linux/linux_mainline/arch/arm64/boot/dts/arm/foundation-v8.dtb ; echo "/ { chosen { bootargs = \"console=ttyAMA0\"; }; psci { compatible = \"arm,psci\"; method = \"smc\"; cpu_on = <0x84000002>; cpu_off = <0x84000001>; }; cpus { cpu@0 { 	enable-method = \"psci\";   reg = <0 0x0>; }; cpu@1 { 	enable-method = \"psci\"; 	reg = <0 0x1>; }; cpu@2 { 	enable-method = \"psci\"; 	reg = <0 0x2>; }; cpu@3 {   enable-method = \"psci\"; 	reg = <0 0x3>; }; }; };" ) | /home/assin/bin/dtc -O dtb -o fdt.dtb -
<stdout>: Warning (unit_address_vs_reg): Node /smb has a reg or ranges property, but no unit name
fdt.dtb: Warning (unit_address_vs_reg): Node /smb has a reg or ranges property, but no unit name
```
**链接生成linux-system.axf**

```c
aarch64-linux-gnu-ld -o linux-system.axf --script=model.lds
```

#### 链接脚本

最终生成的链接脚本model.lds如下：

```c
OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
TARGET(binary)
INPUT(./boot.o) /*The INPUT command directs the linker to include the named files in the link*/
INPUT(./cache.o)
INPUT(./gic.o)
INPUT(./mmu.o)
INPUT(./ns.o)
INPUT(./spin.o)
INPUT(/home/assin/linux/linux_mainline/arch/arm64/boot/Image)
INPUT(./fdt.dtb)
SECTIONS
{
 . = 0x0000000080000000;
 .text : { boot.o }
 .text : { cache.o }
 .text : { gic.o }
 .text : { mmu.o }
 .text : { ns.o }
 .text : { spin.o }
 . = 0x0000000080000000 + 0xfff8;
 mbox = .;
 .mbox : { QUAD(0x0) }
 . = 0x0000000080000000 + 0x80000;
 kernel = .;
 .kernel : { /home/assin/linux/linux_mainline/arch/arm64/boot/Image }
 . = 0x0000000080000000 + 0x08000000;
 dtb = .;
 .dtb : { ./fdt.dtb }
 . = 0x0000000080000000 + 0x10000000;
 filesystem = .;
 .data : { *(.data) }
 .bss : { *(.bss) }
}
```
## 启动过程分析

**只分析使能了PSCI的过程**

PSCI： **Power State Coordination Interface**提供了一个通用接口用于core的电源管理，其中就包括了core的Secondary core boot。具体的说明可以参考文档：DEN0022C_Power_State_Coordination_Interface.pdf。下载地址：http://101.96.8.165/infocenter.arm.com/help/topic/com.arm.doc.den0022c/DEN0022C_Power_State_Coordination_Interface.pdf

### boot.S 没有EL3

```assembly
#include "common.S"

	.text

	.globl	_start
_start:
	/*
	 * EL3 initialisation
	 */
	mrs	x0, CurrentEL		/*读取当前的 Exception levels*/
	cmp	x0, #CURRENTEL_EL3	/*和EL3进行比较*/
	b.ne	start_no_el3	// 如果不等于EL3，skip EL3 initialisation,start_no_el3在psci.S

	mov	x0, #0x30			// RES1
	orr	x0, x0, #(1 << 0)		// Non-secure EL1  /*EL1和EL0在Non-secure*/
	orr	x0, x0, #(1 << 8)		// HVC enable
	orr	x0, x0, #(1 << 10)		// 64-bit EL2   /*Execution state control for lower Exception levels,1表示aarch64*/
	msr	scr_el3, x0

	msr	cptr_el3, xzr			// Disable copro. traps to EL3

	ldr	x0, =CNTFRQ
	msr	cntfrq_el0, x0

	bl	gic_secure_init

	b	start_el3

	.ltorg

	.org	0x100
```
#### start_no_el3
```assembly
/*
 * This PSCI implementation requires EL3. Without EL3 we'll only boot the
 * primary cpu, all others will be trapped in an infinite loop.
 */
 /*PSCI的实现需要EL3，如果没有EL3，在只boot primary CPU，其他的cpu将无限循环*/
start_no_el3:
	mrs	x0, mpidr_el1		/*读取cpu id*/
	ldr	x1, =MPIDR_ID_BITS	/*定义cpu id的bit位*/
	and	x0, x0, x1
	bl	find_logical_id		/*定义在psci.S*/
	cbz	x0, start_cpu0		/*如果x0寄存器等于0，则跳转调start_cpu0*/
spin_dead:					/*primary cpu的logic id为0，如果x0不为0则表示为其他的cpu，那么无限循环*/
	wfe
	b	spin_dead
```
#### find_logical_id
```assembly
/*
 * Takes masked MPIDR in x0, returns logical id in x0
 * Returns -1 for unknown MPIDRs
 * Clobbers x1, x2, x3
 */
find_logical_id:
__find_logical_index:
	adr	x2, id_table	//获取id_table的地址
	mov	x1, xzr			//x1 清零
1:	mov	x3, #nr_cpus	// check we haven't walked off the end of the array
	cmp	x1, x3
	b.gt	3f			/*如果x1 大于x3，则跳转到3标号，x1为0，大于x3，则表示x3为负数。*/
	ldr	x3, [x2, x1, lsl #3] /*把x2 + x1 << 3地址处的数据加载到x3*/
	cmp	x3, x0				/*x0保存有当前的cpu id*/
	b.eq	2f				/*如果x0等于x3，跳转到标号2*/
	add	x1, x1, #1			/*x1自增1*/
	b 1b					/*跳转到标号1，继续循环*/
2:	mov	x0, x1				
	ret						/*返回，这时x0表示cpu logic id*/
3:	mov	x0, #-1				/*表示失败*/
	ret

id_table:
	.quad CPU_IDS	/*编译时定义的宏，-DCPU_IDS=0x0,0x1,0x2,0x3，quad定义的一个数据占8个字节*/
__id_end:
	.quad MPIDR_INVALID	/*#define MPIDR_INVALID		(-1)*/

.equ	nr_cpus, ((__id_end - id_table) / 8)/*总共CPU的个数*/
```
#### start_cpu0
```assembly
start_cpu0:
	/*
	 * Kernel parameters
	 */
	mov	x0, xzr
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr

	bl	ns_init_system /*non-secure初始化，主要是初始化串口和CLCD，不用关系*/
	/*dtb和kernel都是model.lds中定义的*/
	ldr	x0, =dtb
	b	kernel /*跳转到kernel*/
```

### boot.S 有EL3

```assembly
#include "common.S"

	.text

	.globl	_start
_start:
	/*
	 * EL3 initialisation
	 */
	mrs	x0, CurrentEL		/*读取当前的 Exception levels*/
	cmp	x0, #CURRENTEL_EL3	/*和EL3进行比较*/
	b.ne	start_no_el3	// 如果不等于EL3，skip EL3 initialisation,start_no_el3在psci.S

	mov	x0, #0x30			// RES1
	orr	x0, x0, #(1 << 0)		// Non-secure EL1  /*EL1和EL0在Non-secure*/
	orr	x0, x0, #(1 << 8)		// HVC enable
	orr	x0, x0, #(1 << 10)		// 64-bit EL2   /*Execution state control for lower Exception levels,1表示aarch64*/
	msr	scr_el3, x0

	msr	cptr_el3, xzr			// Disable copro. traps to EL3

	ldr	x0, =CNTFRQ				//初始化timer的频率，后面分析
	msr	cntfrq_el0, x0

	bl	gic_secure_init			//没有时能giv-v3，所以只分析gic.S

	b	start_el3				//psci.S

	.ltorg

	.org	0x100
```
#### gic_secure_init
```assembly
/*
 * gic.S - Secure gic initialisation for stand-alone Linux booting
 *
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#include "common.S"

	.text

	.global gic_secure_init

gic_secure_init:
	/*
	 * Check for the primary CPU to avoid a race on the distributor
	 * registers.
	 */
	mrs	x0, mpidr_el1
	ldr	x1, =MPIDR_ID_BITS
	tst	x0, x1					/*x0先和x1进行与运算，然后和0进行比较*/
	b.ne	1f					/* secondary CPU  如果为0则表示为core 0*/

	ldr	x1, =GIC_DIST_BASE		// GICD_CTLR
	mov	w0, #3					// EnableGrp0 | EnableGrp1
	str	w0, [x1]

1:	ldr	x1, =GIC_DIST_BASE + 0x80	// GICD_IGROUPR
	mov	w0, #~0				// Grp1 interrupts  ,中断都属于group 1
	str	w0, [x1]
	/*
	*Only local interrupts for secondary CPUs  
	*对于core 0会执行下面的指令，其他的core直接跳转到2处
	*/
	b.ne	2f				
	ldr     x2, =GIC_DIST_BASE + 0x04       // GICD_TYPER
	ldr     w3, [x2]
	ands    w3, w3, #0x1f                   // ITLinesNumber，
	b.eq    2f
1:	str     w0, [x1, #4]!				//将所有中断都配置为属于group 1
	subs    w3, w3, #1
	b.ne    1b

2:	ldr	x1, =GIC_CPU_BASE		// GICC_CTLR
	mov	w0, #3					// EnableGrp0 | EnableGrp1
	str	w0, [x1]

	mov	w0, #1 << 7				// allow NS access to GICC_PMR
	str	w0, [x1, #4]			// GICC_PMR

	ret

```

#### start_el3

```assembly
start_el3:
	bl	setup_vector		/*配置异常向量 psci.S*/
	bl	switch_to_idmap		/*配置地址映射，mmu.S*/

	/* only boot the primary cpu (entry 0 in the table) */
	mrs	x0, mpidr_el1
	ldr	x1, =MPIDR_ID_BITS
	and	x0, x0, x1
	bl	find_logical_id
	cbnz	x0, spin	/*如果x0不等于0，跳转到spin*/

	adr	x2, branch_table
	adr	x1, start_cpu0
	str	x1, [x2]   /*把start_cpu0写入branch_table，start_cpu0为core 0的入启动kernel的函数*/
	sevl
	b	spin
```

#### setup_vector

```assembly
setup_vector:
	adr	x0, vector
	msr	VBAR_EL3, x0   /*配置EL3的异常基地址*/
	isb
	ret
```

vector为异常向量的入口地址,如下：

```assembly

	.macro	ventry	label
	.align	7
	b	\label
	.endm

	.data

	.align 11
vector:
	// current EL, SP_EL0
	ventry	err_exception	// synchronous
	ventry	err_exception	// IRQ
	ventry	err_exception	// FIQ
	ventry	err_exception	// SError

	// current EL, SP_ELx
	ventry	err_exception
	ventry	err_exception
	ventry	err_exception
	ventry	err_exception

	// lower EL, AArch64
	ventry	psci_call64
	ventry	err_exception
	ventry	err_exception
	ventry	err_exception

	// lower EL, AArch32
	ventry	psci_call32
	ventry	err_exception
	ventry	err_exception
	ventry	err_exception
```

#### switch_to_idmap

```assembly
	.globl switch_to_idmap
	.globl switch_to_physmap

switch_to_idmap:

	mov	x28, x30	/*x30为lr寄存器，默认保存有跳转的返回地址*/
	/*
	 * We assume that the d-caches are invalid at power-on, and hence do
	 * not need to be invalidated. However the icache(s) and TLBs may still
	 * be filled with garbage.
	 */
	ic	iallu
	tlbi	alle3
	dsb	sy
	isb

	adr	x0, pgtable_l1 /*虚拟地址=物理地址的映射*/
	msr	ttbr0_el3, x0 /*初始化el3页表*/
	
	ldr	x0, =MAIR_ATTR
	msr	mair_el3, x0

	ldr	x0, =TCR_VAL
	msr	tcr_el3, x0

	isb

	ldr	x0, =SCTLR_VAL
	msr	sctlr_el3, x0

	isb
	/* Identity map now active, branch back to phys/virt address */
	ret	x28 /*跳转到x28寄存器中的地址。如果ret后面没有指定寄存器，默认为x30*/

```

#### spin

```assembly
/*
 * Poll the release table, waiting for a valid address to appear.
 * When a valid address appears, branch to it.
 */
spin:
	mrs	x0, mpidr_el1
	ldr	x1, =MPIDR_ID_BITS
	and	x0, x0, x1
	bl	find_logical_id
	cmp	x0, #-1		/*获取cpu logic id失败，无线循环*/
	b.eq	spin_dead

	adr	x1, branch_table
	mov	x3, #ADDR_INVALID

	add	x1, x1, x0, lsl #3   /*x0为core id*/

	/**
	*如果不是core 0则会在这里一直循环，在core 0起来后，会把branch_table地址对应core的地址处写入core	*需要执行的地址
	*/
1:	wfe
	ldr	x2, [x1]
	/*
	*比较对应core logic id中的branch_table的入口地址是否-1，core 0会初始化对应的入口地址为	
	*start_cpu0
	*/
	cmp	x2, x3	
	b.eq	1b

	ldr	x0, =SCTLR_EL2_RESET   /*(3 << 28 | 3 << 22 | 1 << 18 | 1 << 16 | 1 << 11 | 3 << 4)*/
	msr	sctlr_el2, x0

	mov	x3, #SPSR_KERNEL	/*异常返回的设置，返回到EL2h*/
	adr	x4, el2_trampoline 	/*el2_trampoline  //异常返回时PC的地址*/
	mov	x0, x2				/*core 在branch_table中对应的跳转地址*/
	
	/*
	*msr	elr_el3, x4 */
	*msr	spsr_el3, x3 
	*eret
	*/
	drop_el	x3, x4 /*drop到EL2*/

branch_table:
	.rept (nr_cpus)
	.quad ADDR_INVALID
	.endr
```

#### el2_trampoline

```assembly
el2_trampoline:
	/*
	*x0为从branch_table读出的对应core的跳转地址，对于core 0为start_cpu0地址，
	*对于其他core 则为kernel启动后写入的地址，为secondary_boot
	*/
	mov	x15, x0  
	bl	flush_caches
	br	x15	/*core 0为start_cpu0，其他core为secondary_boot*/
```

### secondary core启动过程

***

#### psci init

在core 0启动linux kernel之后，会进行psci初始化

```c
static const struct of_device_id psci_of_match[] __initconst = {
	{ .compatible = "arm,psci",	.data = psci_0_1_init},
	{ .compatible = "arm,psci-0.2",	.data = psci_0_2_init},
	{},
};

int __init psci_init(void)
{
	struct device_node *np;
	const struct of_device_id *matched_np;
	psci_initcall_t init_fn;

	np = of_find_matching_node_and_match(NULL, psci_of_match, &matched_np);

	if (!np)
		return -ENODEV;

	init_fn = (psci_initcall_t)matched_np->data;
	return init_fn(np);
}
```

psci节点:

```c
psci {
		compatible = "arm,psci-0.2";
		method = "smc";
	};
```

在从np = of_find_matching_node_and_match(NULL, psci_of_match, &matched_np);返回后matched_np则为和compatible = "arm,psci-0.2";名称相同的of_device_id结构图的指针，在本例中matched_np的data为psci\_0\_2_init。

```c

static int __init psci_0_2_init(struct device_node *np)
{
	int err, ver;

	err = get_set_conduit_method(np);/*获得psci使用什么指令进行切换*/
	ver = psci_get_version();

	pr_info("Using standard PSCI v0.2 function IDs\n");
	psci_function_id[PSCI_FN_CPU_SUSPEND] = PSCI_0_2_FN64_CPU_SUSPEND;
	psci_ops.cpu_suspend = psci_cpu_suspend;

	psci_function_id[PSCI_FN_CPU_OFF] = PSCI_0_2_FN_CPU_OFF;
	psci_ops.cpu_off = psci_cpu_off;
	
	psci_function_id[PSCI_FN_CPU_ON] = PSCI_0_2_FN64_CPU_ON;
	psci_ops.cpu_on = psci_cpu_on;

	psci_function_id[PSCI_FN_MIGRATE] = PSCI_0_2_FN64_MIGRATE;
	psci_ops.migrate = psci_migrate;

	psci_function_id[PSCI_FN_AFFINITY_INFO] = PSCI_0_2_FN64_AFFINITY_INFO;
	psci_ops.affinity_info = psci_affinity_info;

	psci_function_id[PSCI_FN_MIGRATE_INFO_TYPE] =
		PSCI_0_2_FN_MIGRATE_INFO_TYPE;
	psci_ops.migrate_info_type = psci_migrate_info_type;

	arm_pm_restart = psci_sys_reset;

	pm_power_off = psci_sys_poweroff;

out_put_node:
	of_node_put(np);
	return err;
}
```

```c
static int get_set_conduit_method(struct device_node *np)
{
	const char *method;
	pr_info("probing for conduit method from DT.\n");
	if (of_property_read_string(np, "method", &method)) {
		pr_warn("missing \"method\" property\n");
		return -ENXIO;
	}
	if (!strcmp("hvc", method)) {
		invoke_psci_fn = __invoke_psci_fn_hvc;
	} else if (!strcmp("smc", method)) { /*通过svc指令*/
		invoke_psci_fn = __invoke_psci_fn_smc;
	} else {
		pr_warn("invalid \"method\" property: %s\n", method);
		return -EINVAL;
	}
	return 0;
}
```

PSCI\_0\_2\_FN\_CPU_OFF,PSCI\_0\_2\_FN64_CPU_ON为ARM公司定义的编号,称作**SMC Function Identifier**，不同SMC Function Identifier代表不同功能。改编后可以见文档：ARM_DEN0028A_SMC_Calling_Convention.pdf。

| SMC Function Identifier | Reserved use and sub-range ownership | Notes                                    |
| :---------------------- | :----------------------------------- | :--------------------------------------- |
| 0xC4000000-0xC400001F   | PSCI SMC64 bit Calls                 | A range of SMC calls. See [5] for details of |

####  PSCI\_0\_2\_FN64_CPU_ON

```c
/* PSCI v0.2 interface */
#define PSCI_0_2_FN_BASE			0x84000000
#define PSCI_0_2_FN(n)				(PSCI_0_2_FN_BASE + (n))
#define PSCI_0_2_64BIT				0x40000000
#define PSCI_0_2_FN64_BASE			(PSCI_0_2_FN_BASE + PSCI_0_2_64BIT)
#define PSCI_0_2_FN64(n)			(PSCI_0_2_FN64_BASE + (n))

#define PSCI_0_2_FN64_CPU_ON		PSCI_0_2_FN64(3)
```

#### boot secondary cores

在smp_init中会启动其他的core

```c
/* Called by boot processor to activate the rest. */
void __init smp_init(void)
{
	unsigned int cpu;

	idle_threads_init();

	/* FIXME: This should be done in userspace --RR */
	for_each_present_cpu(cpu) {
		if (num_online_cpus() >= setup_max_cpus)
			break;
		if (!cpu_online(cpu))
			cpu_up(cpu);
	}

	/* Any cleanup work */
	smp_announce();
	smp_cpus_done(setup_max_cpus);
}
```

```c
/* Requires cpu_add_remove_lock to be held */
static int _cpu_up(unsigned int cpu, int tasks_frozen)
{
	int ret, nr_calls = 0;
	void *hcpu = (void *)(long)cpu;
	unsigned long mod = tasks_frozen ? CPU_TASKS_FROZEN : 0;
	struct task_struct *idle;

	cpu_hotplug_begin();

	if (cpu_online(cpu) || !cpu_present(cpu)) {
		ret = -EINVAL;
		goto out;
	}

	idle = idle_thread_get(cpu);
	ret = smpboot_create_threads(cpu);

	ret = __cpu_notify(CPU_UP_PREPARE | mod, hcpu, -1, &nr_calls);
	
	/* Arch-specific enabling code. */
	ret = __cpu_up(cpu, idle);
	BUG_ON(!cpu_online(cpu));

	/* Wake the per cpu threads */
	smpboot_unpark_threads(cpu);

	/* Now call notifier in preparation. */
	cpu_notify(CPU_ONLINE | mod, hcpu);

out_notify:
	if (ret != 0)
		__cpu_notify(CPU_UP_CANCELED | mod, hcpu, nr_calls, NULL);
out:
	cpu_hotplug_done();

	return ret;
}
```

```c
int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	int ret;

	/*
	 * We need to tell the secondary core where to find its stack and the
	 * page tables.
	 */
	secondary_data.stack = task_stack_page(idle) + THREAD_START_SP;
	__flush_dcache_area(&secondary_data, sizeof(secondary_data));

	/*
	 * Now bring the CPU into our world.
	 */
	ret = boot_secondary(cpu, idle);
	if (ret == 0) {
		/*
		 * CPU was successfully started, wait for it to come online or
		 * time out.
		 */
		wait_for_completion_timeout(&cpu_running,
					    msecs_to_jiffies(1000));

		if (!cpu_online(cpu)) {
			pr_crit("CPU%u: failed to come online\n", cpu);
			ret = -EIO;
		}
	} else {
		pr_err("CPU%u: failed to boot: %d\n", cpu, ret);
	}

	secondary_data.stack = NULL;

	return ret;
}
```

```c
static int boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	if (cpu_ops[cpu]->cpu_boot)
		return cpu_ops[cpu]->cpu_boot(cpu);
	return -EOPNOTSUPP;
}
```

cpu_ops初始化在cpu_read_bootcpu_ops

```
void __init cpu_read_bootcpu_ops(void)
{
	struct device_node *dn = of_get_cpu_node(0, NULL);/*获得cpu 0的node*/
	cpu_read_ops(dn, 0);
}
```

cpu0节点如下：

```c
cpus {
		#address-cells = <2>;
		#size-cells = <0>;

		A57_0: cpu@0 {
			compatible = "arm,cortex-a57","arm,armv8";
			reg = <0x0 0x0>;
			device_type = "cpu";
			enable-method = "psci";
			next-level-cache = <&A57_L2>;
		};
```

```c
int __init cpu_read_ops(struct device_node *dn, int cpu)
{
	const char *enable_method = of_get_property(dn, "enable-method", NULL);
	cpu_ops[cpu] = cpu_get_ops(enable_method);
	return 0;
}
```

```c
static const struct cpu_operations * __init cpu_get_ops(const char *name)
{
	const struct cpu_operations **ops = supported_cpu_ops;

	while (*ops) {
		if (!strcmp(name, (*ops)->name))
			return *ops;

		ops++;
	}

	return NULL;
}
```

```c
static const struct cpu_operations *supported_cpu_ops[] __initconst = {
#ifdef CONFIG_SMP
	&smp_spin_table_ops,
#endif
	&cpu_psci_ops,
	NULL,
};
```

得到的cpu_ops[0] = &cpu_psci_ops

cpu_ops[cpu]->cpu_boot(cpu);调用的函数为cpu_psci_cpu_boot

```c
static int cpu_psci_cpu_boot(unsigned int cpu)
{
	int err = psci_ops.cpu_on(cpu_logical_map(cpu), __pa(secondary_entry));
	return err;
}
```

 psci_ops.cpu_on为psci_cpu_on。的第一个参数为cpu logic id，第二个参数为cpu boot时的入口地址的物理地址，因为其他的cpu还没有配置mmu。

```c
static int psci_cpu_on(unsigned long cpuid, unsigned long entry_point)
{
	int err;
	u32 fn;

	fn = psci_function_id[PSCI_FN_CPU_ON];/*SMC functions ID*/
	err = invoke_psci_fn(fn, cpuid, entry_point, 0);/*invoke_psci_fn=__invoke_psci_fn_smc*/
	return psci_to_linux_errno(err);
}
```

```assembly
/* int __invoke_psci_fn_smc(u64 function_id, u64 arg0, u64 arg1, u64 arg2) */
ENTRY(__invoke_psci_fn_smc)
	smc	#0			/*执行smc指令之后就会触发中断切换到EL3*/
	ret
ENDPROC(__invoke_psci_fn_smc)
```

smc指令后面的的立即数为16bit，可以在ESR_EL3寄存器的ISS部分的[15:0]读取

在psci.S中的中断向量如下：

```assembly
vector:
	......
	// lower EL, AArch64
	ventry	psci_call64
	......
```

```
psci_call64:
	/*在执行smc命令的时候，x0=smc functions id，x1为cpu id， x2为跳转地址*/
	ldr	x7, =PSCI_CPU_OFF
	cmp	x0, x7
	b.eq	psci_cpu_off

	ldr	x7, =PSCI_CPU_ON /*我们只分析PSCI_CPU_ON*/
	cmp	x0, x7
	b.eq	psci_cpu_on

	mov	x0, PSCI_RET_NOT_IMPL
	eret
```

```
/*
 * x1 - target cpu
 * x2 - address
 */
psci_cpu_on:
	mov	x15, x30 /*保存有kernel中的返回地址*/
	mov	x14, x2
	mov	x0, x1

	bl	find_logical_id
	cmp	x0, #-1
	b.eq	1f

	adr	x3, branch_table
	add	x3, x3, x0, lsl #3 /*x0为cpu的ID*/

	ldr	x4, =ADDR_INVALID

	ldxr	x5, [x3]
	cmp	x4, x5			/*初始都被初始化为-1*/
	b.ne	1f

	/*
	*把x14即secondary_entry的物理地址写入branch_table对应的core id中
	*其他的cpu core在等待event事件，等到后会检查branch_table中对应core id的跳转地址是否为-1
	*如果不是-1，字执行和core 0一样的动作，陷入EL2然后执行el2_trampoline，
	*/
	stxr	w4, x14, [x3] 
	cbnz	w4, 1f

	dsb	ishst
	sev					/*发送event唤醒其他cpu core*/

	mov	x0, #PSCI_RET_SUCCESS
	mov	x30, x15
	eret	/*返回异常*/

1:	mov	x0, #PSCI_RET_DENIED
	mov	x30, x15
	eret
```
#### 其他core的执行过程
```
1:	wfe
	ldr	x2, [x1]
	/*
	*比较对应core logic id中的branch_table的入口地址是否-1，core 0会初始化对应的入口地址为	
	*start_cpu0,其他的core的跳转地址开始是-1，在kernel启动后会写为secondary_entry
	*/
	cmp	x2, x3	
	b.eq	1b

	ldr	x0, =SCTLR_EL2_RESET   /*(3 << 28 | 3 << 22 | 1 << 18 | 1 << 16 | 1 << 11 | 3 << 4)*/
	msr	sctlr_el2, x0

	mov	x3, #SPSR_KERNEL	/*异常返回的设置，返回到EL2h*/
	adr	x4, el2_trampoline 	/*el2_trampoline  //异常返回时PC的地址*/
	mov	x0, x2				/*core 在branch_table中对应的跳转地址*/
	
	/*
	*msr	elr_el3, x4 */
	*msr	spsr_el3, x3 
	*eret
	*/
	drop_el	x3, x4 /*drop到EL2*/

el2_trampoline:
	/*
	*x0为从branch_table读出的对应core的跳转地址，对于core 0为start_cpu0地址，
	*对于其他core 则为kernel启动后写入的地址，为secondary_boot
	*/
	mov	x15, x0  
	bl	flush_caches
	br	x15	/*core 0为start_cpu0，其他core为secondary_boot*/
```





