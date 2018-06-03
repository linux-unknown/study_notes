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
el1_irq:
	kernel_entry 1	/*el=1*/
	enable_dbg
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif

	irq_handler

#ifdef CONFIG_PREEMPT
	get_thread_info tsk
	ldr	w24, [tsk, #TI_PREEMPT]		// get preempt count
	cbnz	w24, 1f				// preempt count != 0
	ldr	x0, [tsk, #TI_FLAGS]		// get flags
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

.if \el == 0
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
stp x22, x23, [sp, #S_PC]	/*将x22，异常返回地址，和x23，cpsr值，保存到 pt_regs的pc和cpsr中*/

/*
 * Set syscallno to -1 by default (overridden later if real syscall).
 */
.if \el == 0
mvn x21, xzr
str x21, [sp, #S_SYSCALLNO]
.endif

/*
 * Registers that may be useful after this macro is invoked:
 *
 * x21 - aborted SP
 * x22 - aborted PC
 * x23 - aborted PSTATE
*/
.endm

.macro	kernel_exit, el, ret = 0
ldp x21, x22, [sp, #S_PC]		// load ELR, SPSR
.if \el == 0
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

