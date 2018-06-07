# arm64 中断处理

### 中断vector初始化

```assembly
__enable_mmu:
	ldr	x5, =vectors	
	msr	vbar_el1, x5			/*初始化中断基地址*/
	msr	ttbr0_el1, x25			// load TTBR0
	msr	ttbr1_el1, x26			// load TTBR1
	isb
	b	__turn_mmu_on
ENDPROC(__enable_mmu)
```

### 中断向量表

```assembly
	.align	11
ENTRY(vectors)
	ventry	el1_sync_invalid		// Synchronous EL1t
	ventry	el1_irq_invalid			// IRQ EL1t
	ventry	el1_fiq_invalid			// FIQ EL1t
	ventry	el1_error_invalid		// Error EL1t

	/*同级别的，没有级别跃迁*/
	ventry	el1_sync			// Synchronous EL1h
	ventry	el1_irq				// IRQ EL1h
	ventry	el1_fiq_invalid			// FIQ EL1h
	ventry	el1_error_invalid		// Error EL1h

	/*不同级别，有级别跃迁*/
	ventry	el0_sync			// Synchronous 64-bit EL0,系统调用属于不同级别的同步 Exception 
	ventry	el0_irq				// IRQ 64-bit EL0
	ventry	el0_fiq_invalid			// FIQ 64-bit EL0
	ventry	el0_error_invalid		// Error 64-bit EL0
END(vectors)
```

```
/*
* Vector entry
*/
.macro ventry	label
.align	7
b	\label
.endm
```

ventry 宏表示直接跳转到对应的lable中。其中的对其应该是arm64架构要求的。

下图是arm64 官方的exception vector table：

![arm64 exception tabls](U:\study\study_notes\interrupt\arm64 exception table.bmp)

从kernel的中断向量代码可以看出kenrel使用的是SPx，即使用对应el级别的sp。

## 同级产生别中断

### el1_irq

```assembly
	.align	6
el1_irq:
	/*保存上下文*/
	kernel_entry 1	/*el=1*/
	enable_dbg	/*使能watchpoint*/
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif
	/*跳转到handle_arch_irq = gic_handle_irq*/
	irq_handler

#ifdef CONFIG_PREEMPT
	/*tsk为寄存器28*/
	get_thread_info tsk
	ldr	w24, [tsk, #TI_PREEMPT]		// get preempt count
	cbnz	w24, 1f				// preempt count != 0
	ldr	x0, [tsk, #TI_FLAGS]		// get flags
	/*如果x0的TIF_NEED_RESCHED置位，则表示需要调度*/
	tbz	x0, #TIF_NEED_RESCHED, 1f	// needs rescheduling?
	bl	el1_preempt
1:
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on
#endif
	kernel_exit 1
ENDPROC(el1_irq)
```

### kernel_entry

```assembly
.macro	kernel_entry, el, regsize = 64
/*
 *DEFINE(S_FRAME_SIZE,	  sizeof(struct pt_regs));
 *栈顶减去pt_regs结构体大小用来压栈寄存器值
 *我们的分析中regsize为64，el = 1
 */
sub sp, sp, #S_FRAME_SIZE
.if \regsize == 32
mov w0, w0				// zero upper 32 bits of x0
.endif
/*一次压栈连个寄存器，所以要乘以16，一个寄存器64bit，执行完之后sp的值是不变的*/
stp x0, x1, [sp, #16 * 0]	
stp x2, x3, [sp, #16 * 1]
stp x4, x5, [sp, #16 * 2]
stp x6, x7, [sp, #16 * 3]
stp x8, x9, [sp, #16 * 4]
stp x10, x11, [sp, #16 * 5]
stp x12, x13, [sp, #16 * 6]
stp x14, x15, [sp, #16 * 7]
stp x16, x17, [sp, #16 * 8]
stp x18, x19, [sp, #16 * 9]
stp x20, x21, [sp, #16 * 10]
stp x22, x23, [sp, #16 * 11]
stp x24, x25, [sp, #16 * 12]
stp x26, x27, [sp, #16 * 13]
stp x28, x29, [sp, #16 * 14]

.if \el == 0				//同级别el=1
	mrs x21, sp_el0				//将栈寄存器sp_el0保存到x21
	get_thread_info tsk 		// Ensure MDSCR_EL1.SS is clear,
	ldr x19, [tsk, #TI_FLAGS]		// since we can unmask debug
	disable_step_tsk x19, x20		// exceptions when scheduling.调试相关
.else
	/*sp + S_FRAME_SIZEx21是栈顶位置，即struct pt_regs结构体的指针*/
	add x21, sp, #S_FRAME_SIZE	
.endif
/*将elr_el1和spsr_el1保存在x22，和x23*/
mrs x22, elr_el1
mrs x23, spsr_el1

/*
 *将lr和x21保存到struct pt_regs结构体的reg[30]和reg[31],  
 *DEFINE(S_LR,	offsetof(struct pt_regs, regs[30]));
 */
stp lr, x21, [sp, #S_LR]

/*将x22(异常返回地址elr_el1)，x23(cpsr_el1)，保存到 pt_regs的pc和pstate中*/
stp x22, x23, [sp, #S_PC]	

/*
 * Set syscallno to -1 by default (overridden later if real syscall).
 */
.if \el == 0
mvn x21, xzr
str x21, [sp, #S_SYSCALLNO]
.endif
```

### irq_handler

```assembly
/*
 * Interrupt handling.
 */
 .macro	irq_handler
 /*获得handle_arch_irq的[63:13]地址*/
 adrp	x1, handle_arch_irq
 /*:lo12:handle_arch_irq表示handle_arch_irq标号的[12:0]地址。
  *将handle_arch_irq变量的值加载到x1寄存器中。
  */
 ldr	x1, [x1, #:lo12:handle_arch_irq]
 mov	x0, sp	/*x0为struct pt_regs结构体的指针，传递给gic_handle_irq*/
 blr	x1	/*跳转到handle_arch_irq指向的函数gic_handle_irq*/
 .endm
```

handle_arch_irq是一个函数指针变量，定义如下：

```c
void (*handle_arch_irq)(struct pt_regs *) = NULL;
```
 set_handle_irq会对该变量进行赋值
```c
void __init set_handle_irq(void (*handle_irq)(struct pt_regs *))
{
	if (handle_arch_irq)
		return;
	handle_arch_irq = handle_irq;
}
```

gic_of_init-->gic_init_bases--->set_handle_irq

### gic_handle_irq

```c
static void __exception_irq_entry gic_handle_irq(struct pt_regs *regs)
{
	u32 irqstat, irqnr;
	struct gic_chip_data *gic = &gic_data[0];
	void __iomem *cpu_base = gic_data_cpu_base(gic);

	do {
		irqstat = readl_relaxed(cpu_base + GIC_CPU_INTACK);
		irqnr = irqstat & GICC_IAR_INT_ID_MASK;

		if (likely(irqnr > 15 && irqnr < 1021)) {
			handle_domain_irq(gic->domain, irqnr, regs);
			continue;
		}
		if (irqnr < 16) {
			writel_relaxed(irqstat, cpu_base + GIC_CPU_EOI);
#ifdef CONFIG_SMP
			handle_IPI(irqnr, regs);
#endif
			continue;
		}
		break;
	} while (1);
}
```
### handle_domain_irq
```c
static inline int handle_domain_irq(struct irq_domain *domain, unsigned int hwirq, struct 					pt_regs *regs)
{
	return __handle_domain_irq(domain, hwirq, true, regs);
}
```
### __handle_domain_irq

```c
int __handle_domain_irq(struct irq_domain *domain, unsigned int hwirq,
			bool lookup, struct pt_regs *regs)
{
 	/**
     * 将当前正在处理的中断现场保存到每CPU变量__irq_regs中去，
     * __irq_regs的类型为struct pt_regs指针。
     * 这样做的目的，是为了在其他代码中，直接读取__irq_regs中的值，找到中断前的现场。
     * 而不用将regs参数层层传递下去。
     * 之所以叫old_regs,应是和中断嵌套有关系。
     */
	struct pt_regs *old_regs = set_irq_regs(regs);
	unsigned int irq = hwirq;
	int ret = 0;

	irq_enter();

#ifdef CONFIG_IRQ_DOMAIN
	if (lookup)
		irq = irq_find_mapping(domain, hwirq);
#endif

	/*
	 * Some hardware gives randomly wrong interrupts.  Rather
	 * than crashing, do something sensible.
	 */
	if (unlikely(!irq || irq >= nr_irqs)) {
		ack_bad_irq(irq);
		ret = -EINVAL;
	} else {
        /*处理具体的irq*/
		generic_handle_irq(irq);
	}

	irq_exit();
	set_irq_regs(old_regs);
	return ret;
}
```

### irq_exit

irq_exit中会处理软中断

```c
/*
 * Exit an interrupt context. Process softirqs if needed and possible:
 */
void irq_exit(void)
{
#ifndef __ARCH_IRQ_EXIT_IRQS_DISABLED
	local_irq_disable();
#else
	WARN_ON_ONCE(!irqs_disabled());
#endif

	account_irq_exit_time(current);
	preempt_count_sub(HARDIRQ_OFFSET);
	if (!in_interrupt() && local_softirq_pending())
		invoke_softirq();/*会处理软中断*/
 
	tick_irq_exit();
	rcu_irq_exit();
	trace_hardirq_exit(); /* must be last! */
}
```

### CONFIG_PREEMPT

```c
#ifdef CONFIG_PREEMPT
	/*tsk为寄存器x28
	 *get_thread_info 获取当前进程的thread_info指针,并存在x28寄存
	 */
	get_thread_info tsk
	/*DEFINE(TI_PREEMPT,		offsetof(struct thread_info, preempt_count));
	 *获取thread_info的preempt_count变量值，如果为0则表示可以抢占。
	 */
	ldr	w24, [tsk, #TI_PREEMPT]		// get preempt count
	/*如果w24不等于0，则执行跳转到1标签处*/
	cbnz	w24, 1f				// preempt count != 0
	/* DEFINE(TI_FLAGS,		offsetof(struct thread_info, flags));
	 * 获取thread_info的flags
	 */
	ldr	x0, [tsk, #TI_FLAGS]		// get flags
	/*如果x0(flags)的TIF_NEED_RESCHED置位，则表示需要调度*/
	tbz	x0, #TIF_NEED_RESCHED, 1f	// needs rescheduling?
	bl	el1_preempt
1:
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on
#endif
	kernel_exit 1
```

```assembly
sc_nr	.req	x25		// number of system calls
scno	.req	x26		// syscall number
stbl	.req	x27		// syscall table pointer
tsk		.req	x28		// current thread_info
```

```assembly
.macro	get_thread_info, rd
mov	\rd, sp
and	\rd, \rd, #~(THREAD_SIZE - 1)	// top of stack
.endm
```

### el1_preempt

```assembly
#ifdef CONFIG_PREEMPT
el1_preempt:
	mov	x24, lr
1:	bl	preempt_schedule_irq		// irq en/disable is done inside
	ldr	x0, [tsk, #TI_FLAGS]		// get new tasks TI_FLAGS
	/*如过TIF_NEED_RESCHED置位则跳转到1标签处*/
	tbnz	x0, #TIF_NEED_RESCHED, 1b	// needs rescheduling?
	ret	x24 /*返回之前的函数*/
#endif
```

### preempt_schedule_irq

```c
asmlinkage __visible void __sched preempt_schedule_irq(void)
{
	enum ctx_state prev_state;

	/* Catch callers which need to be fixed */
	BUG_ON(preempt_count() || !irqs_disabled());

	prev_state = exception_enter();

	do {
		__preempt_count_add(PREEMPT_ACTIVE);
		local_irq_enable();
		__schedule();
		local_irq_disable();
		__preempt_count_sub(PREEMPT_ACTIVE);

		/*
		 * Check again in case we missed a preemption opportunity
		 * between schedule and now.
		 */
		barrier();
	} while (need_resched());

	exception_exit(prev_state);
}
```
### kernel_exit

```assembly
.macro	kernel_exit, el, ret = 0
/*在kernel enter时将elr_el1,spsr_el1保存到了 pt_regs的pc和cpsr中*/
ldp x21, x22, [sp, #S_PC]		// load ELR, SPSR
.if \el == 0					//同级别的el=1
/*调试相关*/
ct_user_enter
/*加载el0的栈值到x23*/
ldr x23, [sp, #S_SP]		// load return stack pointer
msr sp_el0, x23
.endif
msr elr_el1, x21			// set up the return data
msr spsr_el1, x22
.if \ret
ldr x1, [sp, #S_X1] 		// preserve x0 (syscall return)
.else
ldp x0, x1, [sp, #16 * 0]
.endif
ldp x2, x3, [sp, #16 * 1]
ldp x4, x5, [sp, #16 * 2]
ldp x6, x7, [sp, #16 * 3]
ldp x8, x9, [sp, #16 * 4]
ldp x10, x11, [sp, #16 * 5]
ldp x12, x13, [sp, #16 * 6]
ldp x14, x15, [sp, #16 * 7]
ldp x16, x17, [sp, #16 * 8]
ldp x18, x19, [sp, #16 * 9]
ldp x20, x21, [sp, #16 * 10]
ldp x22, x23, [sp, #16 * 11]
ldp x24, x25, [sp, #16 * 12]
ldp x26, x27, [sp, #16 * 13]
ldp x28, x29, [sp, #16 * 14]
ldr lr, [sp, #S_LR]
add sp, sp, #S_FRAME_SIZE		// restore sp
/*eret使用spsr_el1恢复PSTATE并跳转到elr_el1的地址*/
eret					// return to kernel
.endm
```



## 不同级别产生的中断

从el0 到el1的中断，入口为el0_irq

### el0_irq

```assembly
el0_irq:
	kernel_entry 0
el0_irq_naked:
	enable_dbg
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif
	/*统计tracing相关*/
	ct_user_exit
	irq_handler

#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on
#endif
	b	ret_to_user
ENDPROC(el0_irq)
```

kernel_entry传递的参数el=0，主要的区别是栈寄存器为sp_el0。退出的最大不同是先调用ret_to_user。

### ret_to_user

```assembly
ret_to_user:
	disable_irq				// disable interrupts
	/* DEFINE(TI_FLAGS,		offsetof(struct thread_info, flags));
	 * 获取当前进程的thread_info的flgas
	 */
	ldr	x1, [tsk, #TI_FLAGS]
	/*如果_TIF_WORK_MASK被置位
	 *#define _TIF_WORK_MASK (_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
	 *				 _TIF_NOTIFY_RESUME | _TIF_FOREIGN_FPSTATE)
	 */
	and	x2, x1, #_TIF_WORK_MASK
	/*如果x2不等于0，跳转到work_pending*/
	cbnz	x2, work_pending
	enable_step_tsk x1, x2
no_work_pending:
	kernel_exit 0, ret = 0
ENDPROC(ret_to_user)
```

```assembly
/*
* Enable and disable interrupts.
*/
.macro	disable_irq
msr	daifset, #2
.endm

.macro	enable_irq
msr	daifclr, #2
.endm
```

### work_pending

```assembly
work_pending:
	/*如果需要进行调度，则跳转到work_resched*/
	tbnz	x1, #TIF_NEED_RESCHED, work_resched

	/* TIF_SIGPENDING, TIF_NOTIFY_RESUME or TIF_FOREIGN_FPSTATE case */
	/* 中断进来的时候保存的中断上下
	 * DEFINE(S_PSTATE,		offsetof(struct pt_regs, pstate));
	 * pstate为cpsr_el1的值，保存的是中断之前的状态
	 */
	ldr	x2, [sp, #S_PSTATE]
	mov	x0, sp				// 'regs'
	tst	x2, #PSR_MODE_MASK		// user mode regs?
	/*如果是用户模式，继续执行*/
	b.ne	no_work_pending			// returning to kernel
	enable_irq				// enable interrupts for do_notify_resume()
	bl	do_notify_resume
	b	ret_to_user			//重新跳转到ret_to_user不断判断
work_resched:
	bl	schedule
```

### do_notify_resume

```c
asmlinkage void do_notify_resume(struct pt_regs *regs, unsigned int thread_flags)
{
	if (thread_flags & _TIF_SIGPENDING)
		do_signal(regs);

	if (thread_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}

	if (thread_flags & _TIF_FOREIGN_FPSTATE)
		fpsimd_restore_current_state();

}
```



