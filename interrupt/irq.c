/*
 * Setup common bits before finally enabling the MMU. Essentially this is just
 * loading the page table pointer and vector base registers.
 *
 * On entry to this code, x0 must contain the SCTLR_EL1 value for turning on
 * the MMU.
 */
__enable_mmu:
	ldr	x5, =vectors	
	msr	vbar_el1, x5			/*初始化中断基地址*/
	msr	ttbr0_el1, x25			// load TTBR0
	msr	ttbr1_el1, x26			// load TTBR1
	isb
	b	__turn_mmu_on
ENDPROC(__enable_mmu)


/*
* Vector entry
*/
.macro ventry	label
.align	7
b	\label
.endm


/*
* Interrupt handling.
*/
.macro	irq_handler
adrp	x1, handle_arch_irq
ldr x1, [x1, #:lo12:handle_arch_irq]
mov x0, sp
blr x1
.endm

.macro	get_thread_info, rd
mov	\rd, sp
and	\rd, \rd, #~(THREAD_SIZE - 1)	// top of stack
.endm


/*
* Enable and disable debug exceptions.
*/
.macro	disable_dbg
msr daifset, #8
.endm

.macro	enable_dbg
msr daifclr, #8
.endm


	/*
	 * These are the registers used in the syscall handler, and allow us to
	 * have in theory up to 7 arguments to a function - x0 to x6.
	 *
	 * x7 is reserved for the system call number in 32-bit mode.
	 */
	sc_nr	.req	x25 	// number of system calls
	scno	.req	x26 	// syscall number
	stbl	.req	x27 	// syscall table pointer
	tsk .req	x28 	// current thread_info



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
	ventry	el0_sync			// Synchronous 64-bit EL0
	ventry	el0_irq				// IRQ 64-bit EL0
	ventry	el0_fiq_invalid			// FIQ 64-bit EL0
	ventry	el0_error_invalid		// Error 64-bit EL0

#ifdef CONFIG_COMPAT
	ventry	el0_sync_compat			// Synchronous 32-bit EL0
	ventry	el0_irq_compat			// IRQ 32-bit EL0
	ventry	el0_fiq_invalid_compat		// FIQ 32-bit EL0
	ventry	el0_error_invalid_compat	// Error 32-bit EL0
#else
	ventry	el0_sync_invalid		// Synchronous 32-bit EL0
	ventry	el0_irq_invalid			// IRQ 32-bit EL0
	ventry	el0_fiq_invalid			// FIQ 32-bit EL0
	ventry	el0_error_invalid		// Error 32-bit EL0
#endif
END(vectors)


	.align	6
el0_irq:
	kernel_entry 0
el0_irq_naked:
	enable_dbg
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif

	ct_user_exit
	irq_handler

#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on
#endif
	b	ret_to_user
ENDPROC(el0_irq)


	.align	6
el1_irq:
	/*保存上下文*/
	kernel_entry 1	/*el=1*/
	enable_dbg
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


.macro	kernel_entry, el, regsize = 64
/*
 *DEFINE(S_FRAME_SIZE,	  sizeof(struct pt_regs));
 *栈顶减去pt_regs结构体大小用来压栈寄存器值
 */
sub sp, sp, #S_FRAME_SIZE
.if \regsize == 32
mov w0, w0				// zero upper 32 bits of x0
.endif
stp x0, x1, [sp, #16 * 0]	/*一次压栈连个寄存器，所以要乘以16，一个寄存器64bit，执行完之后sp的值是不变的*/
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
mrs x21, sp_el0
get_thread_info tsk 		// Ensure MDSCR_EL1.SS is clear,
ldr x19, [tsk, #TI_FLAGS]		// since we can unmask debug
disable_step_tsk x19, x20		// exceptions when scheduling.
.else
add x21, sp, #S_FRAME_SIZE	/*x21刚入栈是栈顶位置，起始也就是struct pt_regs结构体的指针*/
.endif
mrs x22, elr_el1			/*将elr_el1和spsr_el1保存在x22，和x23*/
mrs x23, spsr_el1

/*
 *将lr和x21保存到struct pt_regs结构体的reg[30]和reg[31],  
 *DEFINE(S_LR,			offsetof(struct pt_regs, regs[30]));
 */
stp lr, x21, [sp, #S_LR]

/*将x22，异常返回地址elr_el1，和x23，cpsr值，保存到 pt_regs的pc和cpsr中*/
stp x22, x23, [sp, #S_PC]	

/*
 * Set syscallno to -1 by default (overridden later if real syscall).
 */
.if \el == 0
mvn x21, xzr
str x21, [sp, #S_SYSCALLNO]
.endif

.endm


/*
 * Registers that may be useful after this macro is invoked:
 *
 * x21 - aborted SP
 * x22 - aborted PC
 * x23 - aborted PSTATE
*/


.macro	kernel_exit, el, ret = 0
/*在kernel enter时将elr_el1,spsr_el1保存到了 pt_regs的pc和cpsr中*/
ldp x21, x22, [sp, #S_PC]		// load ELR, SPSR
.if \el == 0					//同级别的el=1
ct_user_enter
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
eret					// return to kernel
.endm

.macro	get_thread_info, rd
mov \rd, sp
and \rd, \rd, #~(THREAD_SIZE - 1)	// top of stack
.endm



#ifdef CONFIG_PREEMPT
el1_preempt:
	mov	x24, lr
1:	bl	preempt_schedule_irq		// irq en/disable is done inside
	ldr	x0, [tsk, #TI_FLAGS]		// get new tasks TI_FLAGS
	tbnz	x0, #TIF_NEED_RESCHED, 1b	// needs rescheduling?
	ret	x24
#endif



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

static inline int handle_domain_irq(struct irq_domain *domain,
				    unsigned int hwirq, struct pt_regs *regs)
{
	return __handle_domain_irq(domain, hwirq, true, regs);
}


int __handle_domain_irq(struct irq_domain *domain, unsigned int hwirq,
			bool lookup, struct pt_regs *regs)
{
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
		generic_handle_irq(irq);
	}

	irq_exit();
	set_irq_regs(old_regs);
	return ret;
}


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


/*
 * this is the entry point to schedule() from kernel preemption
 * off of irq context.
 * Note, that this is called and return with irqs disabled. This will
 * protect us against recursive calling from irq.
 */
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



