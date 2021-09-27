# linux进程内核栈，用户栈

在Linux创建进程的时候会创建进程的内核栈，以arm64为例进行说明

## arm64 栈寄存器

arm64的栈寄存器在EL0，EL1，EL2，EL3，分别有一个寄存器，SP_EL0，SP_EL1，SP_EL2，SP_EL3，EL0只能使用SP_EL0，EL1，EL2，EL3可以使用EL0，也可以使用对应EL的SP，这个通过PSTATE.SP 位来设置，可以通过下面的指令操作该位

```
msr	SPsel, #1
```

默认情况下，产生异常时，使用对应异常级别的SP_ELx，软件页可以在当前的异常级别通过修改PSTATE.SP来选择使用SP_EL0或SP_ELx(EL0只能使用SP_EL0)。

在发生异常时，即使没有改变异常级别，也会选择该异常级别的SP_ELx，例如，当前在EL1，使用的是SP_EL0，然后产生异常到EL1，那么SP就会切换为SP_EL1。

arm64 linux内核SPsel设置位1。在内核里面会使用SP_ELx。内核可能运行在EL2或EL1。

## 内核太栈分配

### dup_task_struct

```c
static struct task_struct *dup_task_struct(struct task_struct *orig)
{
	struct task_struct *tsk;
	struct thread_info *ti;
	int node = tsk_fork_get_node(orig);
	int err;

	tsk = alloc_task_struct_node(node);
	ti = alloc_thread_info_node(tsk, node);
    /* *dst = *src，将orig的内容赋值给tsk */
	err = arch_dup_task_struct(tsk, orig);
    /* 将 task_struct的stack赋值为分配的 struct thread_info, tsk->stack就是内核栈 */
	tsk->stack = ti; 
    /* 
     * 将org的thread_info内容赋值给p的thread_info
     * *task_thread_info(p) = *task_thread_info(org);
	 * task_thread_info(p)->task = p;
     */
	setup_thread_stack(tsk, orig);
	set_task_stack_end_magic(tsk);

	return tsk;
}
```

alloc_task_struct_node会分配`struct task_struct`。alloc_thread_info_node会分配`struct thread_info`分配的时候大小为THREAD_SIZE，即进程内核栈的大小。

## 用户态栈分配

在调用exec执行的时候do_execveat_common-->bprm_mm_init-->__bprm_mm_init

## __bprm_mm_init

```c
static int __bprm_mm_init(struct linux_binprm *bprm)
{
	int err;
	struct vm_area_struct *vma = NULL;
	struct mm_struct *mm = bprm->mm;

	bprm->vma = vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	down_write(&mm->mmap_sem);
	vma->vm_mm = mm;

	/*
	 * Place the stack at the largest stack address the architecture
	 * supports. Later, we'll move this to an appropriate place. We don't
	 * use STACK_TOP because that can depend on attributes which aren't
	 * configured yet.
	 */
	BUILD_BUG_ON(VM_STACK_FLAGS & VM_STACK_INCOMPLETE_SETUP);
	vma->vm_end = STACK_TOP_MAX;
	vma->vm_start = vma->vm_end - PAGE_SIZE;
	vma->vm_flags = VM_SOFTDIRTY | VM_STACK_FLAGS | VM_STACK_INCOMPLETE_SETUP;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	INIT_LIST_HEAD(&vma->anon_vma_chain);

	err = insert_vm_struct(mm, vma);

	mm->stack_vm = mm->total_vm = 1;
	arch_bprm_mm_init(mm, vma);
	up_write(&mm->mmap_sem);
    /* bprm->p为栈指针，后面会赋值给struct pt_regs的sp 
     * vma->vm_end = STACK_TOP_MAX;
     * STACK_TOP_MAX为用户进程地址空间的大小
     * #define STACK_TOP_MAX		TASK_SIZE_64   
     */
	bprm->p = vma->vm_end - sizeof(void *);
	return 0;
}
```

### start_thread

```c
#define task_stack_page(task)	((task)->stack)

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_START_SP + task_stack_page(p)) - 1)

#define current_pt_regs() task_pt_regs(current)

static int load_elf_binary(struct linux_binprm *bprm)
{
	struct pt_regs *regs = current_pt_regs();
 	/* elf_entry为elf入口地址，bprm->p为栈指针 */
	start_thread(regs, elf_entry, bprm->p);   
}

static inline void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp)
{
	start_thread_common(regs, pc);
	/* 将pstate设置为用户模式 */
	regs->pstate = PSR_MODE_EL0t;
    /* 将sp保存到内核栈的regs中 */
	regs->sp = sp;
}
```

## 第一个内核态进程栈初始化

swapper进程作为内核启动的第一个进程，而且是内核进程，它的栈是人为指定的。在系统启动的时候会赋值给sp寄存器

```c
/* 全局的变量 */
union thread_union init_thread_union __init_task_data =
	{ INIT_THREAD_INFO(init_task) };

#define INIT_TASK(tsk)	\
{									\
	.state		= 0,						\
	.stack		= &init_thread_info,				\
	.usage		= ATOMIC_INIT(2),				\
	.flags		= PF_KTHREAD,					\
	.prio		= MAX_PRIO-20,					\
	.static_prio	= MAX_PRIO-20,					\
	.normal_prio	= MAX_PRIO-20,					\
	.policy		= SCHED_NORMAL,					\
	.cpus_allowed	= CPU_MASK_ALL,					\
	.nr_cpus_allowed= NR_CPUS,					\
	.mm		= NULL,						\
	.active_mm	= &init_mm,
}
```

### __switch_data

```assembly
/* arch/arm64/include/asm/thread_info.h*/
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
	/*init_thread_union定义在/init/init_task.c */
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
	mov	sp, x16				// 初始化堆栈指针
	str	x22, [x4]			// Save processor ID
	str	x21, [x5]			// Save FDT pointer
	str	x24, [x6]			// Save PHYS_OFFSET
	mov	x29, #0
	b	start_kernel		//跳转到c语言实现的函数start_kernel	
ENDPROC(__mmap_switched)
```

## 第一个用户态进程栈初始化

可以从第一个用户进程的创建过程看下

kernel_init-->run_init_process(ramdisk_execute_command);

kernel_init是通过kernel_thread创建的一个内核线程，kernel_thread其实就是执行的do_fork。

在do_fork的path中copy_thread会对新进程的struct pt_regs赋值

### copy_thread

```c
#define THREAD_SIZE		16384
#define THREAD_START_SP		(THREAD_SIZE - 16)

#define task_stack_page(task)	((task)->stack)

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_START_SP + task_stack_page(p)) - 1)

struct cpu_context {
	unsigned long x19;
	unsigned long x20;
	unsigned long x21;
	unsigned long x22;
	unsigned long x23;
	unsigned long x24;
	unsigned long x25;
	unsigned long x26;
	unsigned long x27;
	unsigned long x28;
	unsigned long fp;
	unsigned long sp;
	unsigned long pc;
};

struct thread_struct {
	struct cpu_context	cpu_context;	/* cpu context */
	unsigned long		tp_value;
	struct fpsimd_state	fpsimd_state;
	unsigned long		fault_address;	/* fault info */
	unsigned long		fault_code;	/* ESR_EL1 value */
	struct debug_info	debug;		/* debugging */
};

struct task_struct {
    /* CPU-specific state of this task */
    struct thread_struct thread;
}

int copy_thread(unsigned long clone_flags, unsigned long stack_start,
		unsigned long stk_sz, struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);
	unsigned long tls = p->thread.tp_value;

	memset(&p->thread.cpu_context, 0, sizeof(struct cpu_context));

	if (likely(!(p->flags & PF_KTHREAD))) { /* 如果是用户线程 */
		*childregs = *current_pt_regs(); /* 复制当前进程的值给新进程 */
		childregs->regs[0] = 0; /* 新进程要返回0，所以要赋值为0 */
		if (is_compat_thread(task_thread_info(p))) {
			if (stack_start)
				childregs->compat_sp = stack_start;
		} else {
			/*
			 * Read the current TLS pointer from tpidr_el0 as it may be
			 * out-of-sync with the saved value.
			 */
			asm("mrs %0, tpidr_el0" : "=r" (tls));
			if (stack_start) {
				/* 16-byte aligned stack mandatory on AArch64 */
				if (stack_start & 15) return -EINVAL;
				childregs->sp = stack_start; /* 如果创建的线程指定了用户态堆栈地址 */
			}
		}
		/*
		 * If a TLS pointer was passed to clone (4th argument), use it
		 * for the new thread.
		 */
		if (clone_flags & CLONE_SETTLS)
			tls = childregs->regs[3];
	} else {
		/* 内核线程，新进程的 struct pt_regs全为0 */
		memset(childregs, 0, sizeof(struct pt_regs));
		childregs->pstate = PSR_MODE_EL1h;
        /* 内核线程x19为线程执行的函数地址，x20为函数的参数 */
		p->thread.cpu_context.x19 = stack_start;
		p->thread.cpu_context.x20 = stk_sz;
	}
	p->thread.cpu_context.pc = (unsigned long)ret_from_fork;
	p->thread.cpu_context.sp = (unsigned long)childregs;
	p->thread.tp_value = tls;

	ptrace_hw_copy_thread(p);

	return 0;
}
```

run_init_process其实就是执行exec

过程和[用户态栈分配]()章节一样

从上面也可以看到：

1. 对于fork的进程来说，进程内核栈是不一样的，因为有自己分配的thread_info。用户栈寄存器除了regs[0] = 0，其他都是是一样的，因为复制了父进程的struct pt_regs。但是因为新的进行有了自己的mm，即自己的页表，在访问的时候对应栈中的地址时，会分配新的地址。所以虽然寄存器一样，但是实际的内存可能不一样。

   我们看到fork中只是设置了 p->thread.cpu_context

```c
p->thread.cpu_context.pc = (unsigned long)ret_from_fork;
p->thread.cpu_context.sp = (unsigned long)childregs;
```

​	进程在切换的时候，就是使用的是p->thread.cpu_context保存的寄存器值。所以新进程运行的时候是从ret_from_fork开始运行。

2. fork之后，然后执行exec，exec会设置

```c
regs->pstate = PSR_MODE_EL0t;
/* 将sp保存到内核栈的regs中 */
regs->sp = sp;

regs->syscallno = ~0UL;
/* 设置regs->pc为elf文件入口地址 */
regs->pc = pc;
```

在退出exec系统调用的时候会执行kernel_exit，使用regs中保存的寄存器值恢复寄存器，所以就跳转到了对应的函数了。

3. 内核线程也有自己的内核态栈，但是内核线程是共享一个mm的。

## 内核态栈和用户态栈切换

### 内核线程

只有内核态栈，在创建内核线程的时候，内核栈中的childregs值都是0。调度的时候使用p->thread.cpu_context.sp 恢复寄存器

#### 内核栈指针切换到用户态栈指针
用户态进进程fork。新进程的内核态栈的childregs除了reg[0]=0，其他的值都和父进程一样，新进程被调度到，cpu_switch_to使用`p->thread.cpu_context`

```c
p->thread.cpu_context.pc = (unsigned long)ret_from_fork;
p->thread.cpu_context.sp = (unsigned long)childregs;
```

恢复上下文，这个时候新进程的栈指针就指向了childregs，然后执行`ret_from_fork`。

```assembly
ENTRY(cpu_switch_to)
	/* x0为prev，x1为next */
	/* x8为prev.thread.cpu_context的指针*/
	/* DEFINE(THREAD_CPU_CONTEXT,	offsetof(struct task_struct,thread.cpu_context)); */
	add	x8, x0, #THREAD_CPU_CONTEXT
	mov	x9, sp
	/**
	 * 将x19和x20存放到x8寄存器值对应的地址中，
	 * 然后x8 + 16（因为存了两个64bit的寄存器，所以加16个字节）
	 */
	stp	x19, x20, [x8], #16		// store callee-saved registers
	stp	x21, x22, [x8], #16
	stp	x23, x24, [x8], #16
	stp	x25, x26, [x8], #16
	stp	x27, x28, [x8], #16
	/** 
	 * x29:fp
	 * x9:sp
	 * lr:pc
	 */
	stp	x29, x9, [x8], #16
	/**
	 * lr，即x30寄存器，它的值为返回调用cpu_switch_to函数的值。即，执行return last
	 */
	str	lr, [x8]
	/* 上面的代码把寄存器lr的值值存放到prev_task.thread.cpu_context.pc中 */

	/* x8为next.thread.cpu_context的指针 */
	/* 将next.thread.cpu_context的值存到寄存器x8中 */
	add	x8, x1, #THREAD_CPU_CONTEXT
	ldp	x19, x20, [x8], #16		// restore callee-saved registers
	ldp	x21, x22, [x8], #16
	ldp	x23, x24, [x8], #16
	ldp	x25, x26, [x8], #16
	ldp	x27, x28, [x8], #16
	/**
	 * copy_thread中p->thread.cpu_context.sp = (unsigned long)childregs;
	 * cpu_context.fp 赋值给x29
	 * cpu_context.sp 赋值给x9，即，将pr_regs保存到x9
	 */
	ldp	x29, x9, [x8], #16
	/** 
	 * 将 cpu_context.pc 赋值给lr寄存器
	 * 对于fork刚创建的进程，cpu_context.pc的值为ret_from_fork。已有的进程lr的值为return last的值
	 */
	ldr	lr, [x8]
	mov	sp, x9 /* x9 为sp的值 */
	ret /* ret默认会跳转到x30寄存器（即lr寄存器）的值，这样就开始执行新进程了*/
ENDPROC(cpu_switch_to)
```
#### ret_from_fork

```assembly
ENTRY(ret_from_fork)
	bl	schedule_tail
	cbz	x19, 1f				// not a kernel thread
	mov	x0, x20
	blr	x19
1:	get_thread_info tsk
	b	ret_to_user
ENDPROC(ret_from_fork)
```

#### get_thread_info

```assembly
tsk	.req	x28		// current thread_info

.macro	get_thread_info, rd
mov	\rd, sp
and	\rd, \rd, #~(THREAD_SIZE - 1)	// top of stack
.endm

/*
 * 此时的sp寄存器的值还是kernel_init的内核栈
 * 最终get_thread_info展开后为
 * 将栈指针赋值给x28
 */
mov	x28, sp
/* x28与上#~(THREAD_SIZE - 1)则得到栈顶 */
and	x28, x28, #~(THREAD_SIZE - 1)	// top of stack，栈从高向低增长
```

#### ret_to_user

```assembly
/*
 * "slow" syscall return path.
 */
ret_to_user:
	disable_irq				// disable interrupts
	ldr	x1, [tsk, #TI_FLAGS] // DEFINE(TI_FLAGS,	offsetof(struct thread_info, flags));
	/* work to do on interrupt/exception return */
	/* #define _TIF_WORK_MASK							\
	 * (0x0000FFFF &							\
	 * ~(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|			\
	 *  _TIF_SINGLESTEP|_TIF_SECCOMP|_TIF_SYSCALL_EMU))
	 */
	and	x2, x1, #_TIF_WORK_MASK
	cbnz	x2, work_pending /* 如果x2寄存器不是0，则跳转到work_pending */
	enable_step_tsk x1, x2 /* 应该是和debug有关，先不管 */
no_work_pending:
	kernel_exit 0, ret = 0
ENDPROC(ret_to_user)

.macro  disable_irq
	msr     daifset, #2
.endm

.macro  enable_irq
	msr     daifclr, #2
.endm
```
#### struct pt_regs

```c
struct pt_regs {
	union {
		struct user_pt_regs user_regs;
		struct {
			u64 regs[31];
			u64 sp;
			u64 pc;
			u64 pstate;
		};
	};
	u64 orig_x0;
	u64 syscallno;
};
```
#### kernel_exit

```assembly
el = 0
.macro	kernel_exit, el, ret = 0
	/* DEFINE(S_PC,	offsetof(struct pt_regs, pc));
	/* load ELR, SPSR。struct pt_regs的pc保存到x21，struct pt_regs的的pstate保存到x22 
	 */
	ldp	x21, x22, [sp, #S_PC]	
	.if	\el == 0
	ct_user_enter				/* debug用，不用管 */
	ldr	x23, [sp, #S_SP]		/* load return stack pointer，将struct pt_regs的sp保存到x23 */
	msr	sp_el0, x23				/* 用户空间的栈指针赋值给sp寄存器 */
	.endif
	msr	elr_el1, x21			/* set up the return data，返回地址为用户空间入口地址 */
	msr	spsr_el1, x22			/* x22 保存的为pstate */
	.if	\ret
	ldr	x1, [sp, #S_X1]			/* preserve x0 (syscall return) */
	.else
	ldp	x0, x1, [sp, #16 * 0]
	.endif
	ldp	x2, x3, [sp, #16 * 1]
	ldp	x4, x5, [sp, #16 * 2]
	ldp	x6, x7, [sp, #16 * 3]
	ldp	x8, x9, [sp, #16 * 4]
	ldp	x10, x11, [sp, #16 * 5]
	ldp	x12, x13, [sp, #16 * 6]
	ldp	x14, x15, [sp, #16 * 7]
	ldp	x16, x17, [sp, #16 * 8]
	ldp	x18, x19, [sp, #16 * 9]
	ldp	x20, x21, [sp, #16 * 10]
	ldp	x22, x23, [sp, #16 * 11]
	ldp	x24, x25, [sp, #16 * 12]
	ldp	x26, x27, [sp, #16 * 13]
	ldp	x28, x29, [sp, #16 * 14]
	ldr	lr, [sp, #S_LR]
	add	sp, sp, #S_FRAME_SIZE		// restore sp
	eret					// return to kernel，就返回到了用户空间，用户空间使用用户态栈
.endm
```

`kernel_exit`使用内核栈中的pt_regs恢复寄存器，这个时候用户来恢复寄存器的sp就是父进程用户态的sp，然后返回用户空间，这个时候栈就切换到了用户空间栈。因为每个进程有自己的用户态地址空间页表，所以虽然地址一样，但是实际物理内存是不一样的。

用户进程执行exec系统调用，exec会重新设置内核栈sp的值，值为`vma->vm_end - sizeof(void *)`在系统调用返回用户态时执行`kernel_exit`使用用户态sp的值恢复寄存器。



### 用户态栈切换到内核态

用户态进入内核态有两种方式：

1. 主导调用系统调用产生软中断
2. 异常，包括中断，如tick中断，或者外设中断。

#### 系统调用

arm64 异常向量表

```assembly
/*
 * Exception vectors.
 */
	.align	11
ENTRY(vectors)
	/* 同级别产生的异常,如果产生异常之前使用SP_EL0 */
	ventry	el1_sync_invalid		// Synchronous EL1t
	ventry	el1_irq_invalid			// IRQ EL1t
	ventry	el1_fiq_invalid			// FIQ EL1t
	ventry	el1_error_invalid		// Error EL1t

	/* ARMv8在AArch64中的模式EL0t、EL1t & EL1h、EL2t & EL2h、EL3t & EL3h，
	 * 后缀t表示SP_EL0堆栈指针，h表示SP_ELx堆栈指针 
	 */
	/* 同级别产生异常，如果产生异常之前使用SP_ELx */
	ventry	el1_sync			// Synchronous EL1h
	ventry	el1_irq				// IRQ EL1h
	ventry	el1_fiq_invalid			// FIQ EL1h
	ventry	el1_error_invalid		// Error EL1h
	/* 低级别切换到高级别 */
	ventry	el0_sync			// Synchronous 64-bit EL0
	ventry	el0_irq				// IRQ 64-bit EL0
	ventry	el0_fiq_invalid			// FIQ 64-bit EL0
	ventry	el0_error_invalid		// Error 64-bit EL0
END(vectors)

/*
 * Vector entry
 */
.macro	ventry	label
	.align	7
	b	\label
.endm
```

系统调用属于低级别切换到高级别，而且时同步中断，因此中断入口是`el0_sync`

##### el0_sync

```assembly
/*
 * EL0 mode handlers.
 */
	.align	6
el0_sync:
	kernel_entry 0
	/* esr_el1读到x25 */
	mrs	x25, esr_el1			 	// read the syndrome register
	/* #define ESR_ELx_EC_SHIFT	(26)
	 * x25左移ESR_ELx_EC_SHIFT位
	 */
	lsr	x24, x25, #ESR_ELx_EC_SHIFT	// exception class
	/* #define ESR_ELx_EC_SVC64	(0x15)
	 * 比较是不是ESR_ELx_EC_SVC64，如果是就执行b.eq	el0_svc，跳转到el0_svc
	 */
	cmp	x24, #ESR_ELx_EC_SVC64		// SVC in 64-bit state
	b.eq	el0_svc					// C库使用svc指令陷入kernel，只分析该项
	cmp	x24, #ESR_ELx_EC_DABT_LOW	// data abort in EL0
	b.eq	el0_da
	/* 删除其他本次不关心情况 */
```

#### kernel_entry

```assembly
.macro	kernel_entry, el, regsize = 64
	/* DEFINE(S_FRAME_SIZE, sizeof(struct pt_regs));既struct pt_regs结构体大小 
	 * 先减去S_FRAME_SIZE，因为stp x0, x1, [sp, #16 * 0]是增加的
	 */
	sub	sp, sp, #S_FRAME_SIZE
	.if	\regsize == 32
	mov	w0, w0				// zero upper 32 bits of x0
	.endif
	/* 将寄存器保存到内核的堆栈中 */
	stp	x0, x1, [sp, #16 * 0]
	stp	x2, x3, [sp, #16 * 1]
	stp	x4, x5, [sp, #16 * 2]
	stp	x6, x7, [sp, #16 * 3]
	stp	x8, x9, [sp, #16 * 4]
	stp	x10, x11, [sp, #16 * 5]
	stp	x12, x13, [sp, #16 * 6]
	stp	x14, x15, [sp, #16 * 7]
	stp	x16, x17, [sp, #16 * 8]
	stp	x18, x19, [sp, #16 * 9]
	stp	x20, x21, [sp, #16 * 10]
	stp	x22, x23, [sp, #16 * 11]
	stp	x24, x25, [sp, #16 * 12]
	stp	x26, x27, [sp, #16 * 13]
	stp	x28, x29, [sp, #16 * 14]

	.if	\el == 0
	mrs	x21, sp_el0				/* 将用户态的栈赋值给x21寄存器 */
	get_thread_info tsk			// Ensure MDSCR_EL1.SS is clear,
	/* DEFINE(TI_FLAGS,	offsetof(struct thread_info, flags)); */
	ldr	x19, [tsk, #TI_FLAGS]		// since we can unmask debug
	disable_step_tsk x19, x20		// exceptions when scheduling.调试相关，先不管
	.else
	add	x21, sp, #S_FRAME_SIZE
	.endif
	mrs	x22, elr_el1	/* 将elr_el1保存到x22 */
	mrs	x23, spsr_el1	/* spsr_el1保存到x23 */
	/* 将lr(既x30)和x21压栈到regs[30]和sp中，x21保存用户态栈 */
	stp	lr, x21, [sp, #S_LR]
	/* 将x22和x23压栈到regs的pc和regs的pstate，
	 * x22保存有异常返回地址，x23保存有异常发生前状态
	 */
	stp	x22, x23, [sp, #S_PC]

	/*
	 * Set syscallno to -1 by default (overridden later if real syscall).
	 */
	.if	\el == 0
	mvn	x21, xzr /* 将x21负责为-1 */
	str	x21, [sp, #S_SYSCALLNO] /*x21压栈到regs的syscallno*/
	.endif

	/*
	 * Registers that may be useful after this macro is invoked:
	 *
	 * x21 - aborted SP
	 * x22 - aborted PC
	 * x23 - aborted PSTATE
	*/
.endm
```

##### get_thread_info

```assembly
tsk	.req	x28		// current thread_info

.macro	get_thread_info, rd
mov	\rd, sp
and	\rd, \rd, #~(THREAD_SIZE - 1)	// top of stack
.endm

最终get_thread_info展开后为
/* 将栈指针赋值给x28 */
mov	x28, sp
/* x28与上#~(THREAD_SIZE - 1)则得到栈顶 */
and	x28, x28, #~(THREAD_SIZE - 1)	// top of stack，栈从高向低增长
```

当切换到EL1的时候，栈寄存器会切换到SP_EL1寄存器。kernel_entry宏将寄存器的值保存到EL1的堆栈中的pt_regs中，对于新进程SP_EL1被在ret_from_wrok中初始化为childregs，即内核栈的顶部。

#### el0_svc

```assembly
sc_nr	.req	x25		// number of system calls
scno	.req	x26		// syscall number
stbl	.req	x27		// syscall table pointer

/*
 * SVC handler.
 */
	.align	6
el0_svc:
	/* 将sys_call_table加载到x27寄存器中 */
	adrp	stbl, sys_call_table		// load syscall table pointer
	/* 将w8 保存到x26，没有看到uxtw是什么之类，应该是gcc自己定义的，编译之后为mov
	 * w8保存只有系统调用号
	 */
	uxtw	scno, w8			// syscall number in w8
	mov	sc_nr, #__NR_syscalls
el0_svc_naked:					// compat entry point
	/* 将x0，和系统调用号，保存到pt_regs的orig_x0和syscallno */
	stp	x0, scno, [sp, #S_ORIG_X0]	// save the original x0 and syscall number
	enable_dbg_and_irq //使能dbg和irq异常
	ct_user_exit 1	//没有开启，不关心

	ldr	x16, [tsk, #TI_FLAGS]		// check for syscall hooks
	tst	x16, #_TIF_SYSCALL_WORK
	b.ne	__sys_trace				// 跳过执行
	cmp     scno, sc_nr             // check upper syscall limit
	b.hs	ni_sys
	/* 找到系统调用对应的地址 */
	ldr	x16, [stbl, scno, lsl #3]	// address in the syscall table
	blr	x16				// call sys_* routine 跳转到系统调用对应的函数。我们分析fork
	b	ret_fast_syscall
ni_sys:
	mov	x0, sp
	bl	do_ni_syscall
	b	ret_fast_syscall
ENDPROC(el0_svc)
```

##### 系统调用返回

```assembly
ret_fast_syscall:
	disable_irq				// disable interrupts
	ldr	x1, [tsk, #TI_FLAGS]
	and	x2, x1, #_TIF_WORK_MASK
	cbnz	x2, fast_work_pending /* 没执行 */
	enable_step_tsk x1, x2 /* 和调试相关，先不管 */
	kernel_exit 0, ret = 1
```

`kernel_exit`是用pt_regs中的值恢复寄存器，于是就切换到了用户态栈。

#### tick中断
##### el0_irq

```assembly
el0_irq:
	kernel_entry 0 /* 保存进程信息到pt_regs */
	ct_user_exit
	irq_handler		/* 调用中断处理函数 */
	b	ret_to_user
ENDPROC(el0_irq)
```
ret_to_user
```assembly
ret_to_user:
	disable_irq				// disable interrupts
	ldr	x1, [tsk, #TI_FLAGS]
	and	x2, x1, #_TIF_WORK_MASK 
	cbnz	x2, work_pending	/* 如果需要调度则x2不为0，跳转到work_pending */
	enable_step_tsk x1, x2
no_work_pending:
	kernel_exit 0, ret = 0
ENDPROC(ret_to_user)
```
work_pending
```assembly
work_pending:
	/* 查看bit TIF_NEED_RESCHED是否不为0，则跳转到work_resched */
	tbnz	x1, #TIF_NEED_RESCHED, work_resched 
	/* TIF_SIGPENDING, TIF_NOTIFY_RESUME or TIF_FOREIGN_FPSTATE case */
	ldr	x2, [sp, #S_PSTATE]
	mov	x0, sp				// 'regs'
	tst	x2, #PSR_MODE_MASK		// user mode regs?
	b.ne	no_work_pending			// returning to kernel
	enable_irq				// enable interrupts for do_notify_resume()
	bl	do_notify_resume
	b	ret_to_user
work_resched:
	bl	schedule	/* 进行调度，调度回来之后继续执行ret_to_user */
	
/*
 * "slow" syscall return path.
 */
ret_to_user:
	disable_irq				// disable interrupts
	ldr	x1, [tsk, #TI_FLAGS]
	and	x2, x1, #_TIF_WORK_MASK
	cbnz	x2, work_pending
	enable_step_tsk x1, x2
no_work_pending:
	kernel_exit 0, ret = 0
ENDPROC(ret_to_user)
```

tick中断和系统调用一样，也是通过`kernel_entry`和`kernel_exit`进行栈的切换和保存恢复。

**也可以看到，内核在进行上下文切换使用的是`p->thread.cpu_context`进行上下文的保存恢复。**

**pt_regs，用以异常切换，中断的上下文保存切换恢复。**

