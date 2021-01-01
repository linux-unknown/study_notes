# fork

[TOC]

## glibc系统调用

以ioctl为例

```assembly
ENTRY(__ioctl)
	/* 将系统调用号__NR_ioctl保存到x8寄存器中 */
	mov	x8, #__NR_ioctl
	sxtw	x0, w0
	svc	#0x0
	cmn	x0, #4095
	b.cs	.Lsyscall_error
	ret
PSEUDO_END (__ioctl)
```

## arm64 异常向量表

```assembly
/*
 * Exception vectors.
 */
	.align	11
ENTRY(vectors)
	ventry	el1_sync_invalid		// Synchronous EL1t
	ventry	el1_irq_invalid			// IRQ EL1t
	ventry	el1_fiq_invalid			// FIQ EL1t
	ventry	el1_error_invalid		// Error EL1t

	/* ARMv8在AArch64中的模式EL0t、EL1t & EL1h、EL2t & EL2h、EL3t & EL3h，
	 * 后缀t表示SP_EL0堆栈指针，h表示SP_ELx堆栈指针 
	 */
	ventry	el1_sync			// Synchronous EL1h
	ventry	el1_irq				// IRQ EL1h
	ventry	el1_fiq_invalid			// FIQ EL1h
	ventry	el1_error_invalid		// Error EL1h

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

/*
 * Vector entry
 */
.macro	ventry	label
	.align	7
	b	\label
.endm
```

系统调用属于`el0_sync`异常

### el0_sync

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
	 * 比较是不是ESR_ELx_EC_SVC64，如果是b.eq就执行
	 * 跳转到el0_svc
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

##### sys_call_table

定义在arch/arm64/kernel/sys.c

```c
#undef __SYSCALL
#define __SYSCALL(nr, sym)	[nr] = sym,

void * const sys_call_table[__NR_syscalls] __aligned(4096) = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,/* 现将所有的初始化为sys_ni_syscall */
    /*最终头文件的展开形式如下,这是gnu扩展的数组初始化方式
     * [0] = sys_io_setup
     * [1] = sys_io_destroy,
     * [2] = sys_io_submit
     */
	#include <asm/unistd.h>
};
```

#####  <asm/unistd.h>

```c
/**
 * 在sys.c中
 * #undef __SYSCALL
 * #define __SYSCALL(nr, sym)	[nr] = sym,
 */
#ifndef __SYSCALL
#define __SYSCALL(x, y)
#endif

#if __BITS_PER_LONG == 32 || defined(__SYSCALL_COMPAT)
#define __SC_3264(_nr, _32, _64) __SYSCALL(_nr, _32)
#else
#define __SC_3264(_nr, _32, _64) __SYSCALL(_nr, _64)
#endif

#ifdef __SYSCALL_COMPAT
#define __SC_COMP(_nr, _sys, _comp) __SYSCALL(_nr, _comp)
#define __SC_COMP_3264(_nr, _32, _64, _comp) __SYSCALL(_nr, _comp)
#else
#define __SC_COMP(_nr, _sys, _comp) __SYSCALL(_nr, _sys)
#define __SC_COMP_3264(_nr, _32, _64, _comp) __SC_3264(_nr, _32, _64)
#endif

#define __NR_io_setup 0
__SC_COMP(__NR_io_setup, sys_io_setup, compat_sys_io_setup)
#define __NR_io_destroy 1
__SYSCALL(__NR_io_destroy, sys_io_destroy)
#define __NR_io_submit 2
__SC_COMP(__NR_io_submit, sys_io_submit, compat_sys_io_submit)
#define __NR_io_cancel 3
__SYSCALL(__NR_io_cancel, sys_io_cancel)
#define __NR_io_getevents 4
__SC_COMP(__NR_io_getevents, sys_io_getevents, compat_sys_io_getevents)
/* fs/xattr.c */
#define __NR_setxattr 5
__SYSCALL(__NR_setxattr, sys_setxattr)

#define __NR_fork 1079
#ifdef CONFIG_MMU
__SYSCALL(__NR_fork, sys_fork)
#else
__SYSCALL(__NR_fork, sys_ni_syscall)
#endif /* CONFIG_MMU */
```

### sys_fork

```c
SYSCALL_DEFINE0(fork)
{
	return do_fork(SIGCHLD, 0, 0, NULL, NULL);
}
```

#### SYSCALL_DEFINE0

```c
#define SYSCALL_DEFINE0(sname)					\
	SYSCALL_METADATA(_##sname, 0);/* 和ftrace相关，不关心 */			\
	asmlinkage long sys_##sname(void)
```

#### fork

```c
long do_fork(unsigned long clone_flags, unsigned long stack_start,
	      unsigned long stack_size,
	      int __user *parent_tidptr,
	      int __user *child_tidptr)
{
	struct task_struct *p;
	int trace = 0;
	long nr;

	p = copy_process(clone_flags, stack_start, stack_size,
			 child_tidptr, NULL, trace);
	/*
	 * Do this prior waking up the new thread - the thread pointer
	 * might get invalid after that point, if the thread exits quickly.
	 */
	if (!IS_ERR(p)) {
		struct completion vfork;
		struct pid *pid;

		trace_sched_process_fork(current, p);

		pid = get_task_pid(p, PIDTYPE_PID);
		nr = pid_vnr(pid);

		wake_up_new_task(p);

		if (clone_flags & CLONE_VFORK) {
			if (!wait_for_vfork_done(p, &vfork))
				ptrace_event_pid(PTRACE_EVENT_VFORK_DONE, pid);
		}

		put_pid(pid);
	} else {
		nr = PTR_ERR(p);
	}
	return nr;
}
```

##### copy_process

```c
static struct task_struct *copy_process(unsigned long clone_flags,
					unsigned long stack_start,
					unsigned long stack_size,
					int __user *child_tidptr,
					struct pid *pid,
					int trace)
{
	int retval;
	struct task_struct *p;

	p = dup_task_struct(current);

	rt_mutex_init_task(p);

	retval = -EAGAIN;
	if (nr_threads >= max_threads)
		goto bad_fork_cleanup_count;

	delayacct_tsk_init(p);	/* Must remain after dup_task_struct() */
	p->flags &= ~(PF_SUPERPRIV | PF_WQ_WORKER);
	p->flags |= PF_FORKNOEXEC;
	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->sibling);
	rcu_copy_process(p);
	p->vfork_done = NULL;
	spin_lock_init(&p->alloc_lock);

	init_sigpending(&p->pending);

	p->utime = p->stime = p->gtime = 0;
	p->utimescaled = p->stimescaled = 0;

	p->default_timer_slack_ns = current->timer_slack_ns;

	task_io_accounting_init(&p->ioac);
	acct_clear_integrals(p);

	posix_cpu_timers_init(p);

	p->start_time = ktime_get_ns();
	p->real_start_time = ktime_get_boot_ns();
	p->io_context = NULL;
	p->audit_context = NULL;
	if (clone_flags & CLONE_THREAD)
		threadgroup_change_begin(current);
	cgroup_fork(p);
#ifdef CONFIG_NUMA
	p->mempolicy = mpol_dup(p->mempolicy);
	if (IS_ERR(p->mempolicy)) {
		retval = PTR_ERR(p->mempolicy);
		p->mempolicy = NULL;
		goto bad_fork_cleanup_threadgroup_lock;
	}
#endif

#ifdef CONFIG_DEBUG_MUTEXES
	p->blocked_on = NULL; /* not blocked yet */
#endif
#ifdef CONFIG_BCACHE
	p->sequential_io	= 0;
	p->sequential_io_avg	= 0;
#endif

	/* Perform scheduler related setup. Assign this task to a CPU. */
	retval = sched_fork(clone_flags, p);

	retval = audit_alloc(p);
	shm_init_task(p);
	retval = copy_semundo(clone_flags, p);
	retval = copy_files(clone_flags, p);
	retval = copy_fs(clone_flags, p);
	retval = copy_sighand(clone_flags, p);
	retval = copy_signal(clone_flags, p);
	retval = copy_mm(clone_flags, p);
	retval = copy_namespaces(clone_flags, p);
	retval = copy_io(clone_flags, p);
	retval = copy_thread(clone_flags, stack_start, stack_size, p);

	if (pid != &init_struct_pid) {
		pid = alloc_pid(p->nsproxy->pid_ns_for_children);
	}

	p->set_child_tid = (clone_flags & CLONE_CHILD_SETTID) ? child_tidptr : NULL;
	/*
	 * Clear TID on mm_release()?
	 */
	p->clear_child_tid = (clone_flags & CLONE_CHILD_CLEARTID) ? child_tidptr : NULL;
#ifdef CONFIG_BLOCK
	p->plug = NULL;
#endif
#ifdef CONFIG_FUTEX
	p->robust_list = NULL;
#ifdef CONFIG_COMPAT
	p->compat_robust_list = NULL;
#endif
	INIT_LIST_HEAD(&p->pi_state_list);
	p->pi_state_cache = NULL;
#endif
	/*
	 * sigaltstack should be cleared when sharing the same VM
	 */
	if ((clone_flags & (CLONE_VM|CLONE_VFORK)) == CLONE_VM)
		p->sas_ss_sp = p->sas_ss_size = 0;

	/*
	 * Syscall tracing and stepping should be turned off in the
	 * child regardless of CLONE_PTRACE.
	 */
	user_disable_single_step(p);
	clear_tsk_thread_flag(p, TIF_SYSCALL_TRACE);
#ifdef TIF_SYSCALL_EMU
	clear_tsk_thread_flag(p, TIF_SYSCALL_EMU);
#endif
	clear_all_latency_tracing(p);

	/* ok, now we should be set up.. */
	p->pid = pid_nr(pid);
	if (clone_flags & CLONE_THREAD) {
		p->exit_signal = -1;
		p->group_leader = current->group_leader;
		p->tgid = current->tgid;
	} else {
		if (clone_flags & CLONE_PARENT)
			p->exit_signal = current->group_leader->exit_signal;
		else
			p->exit_signal = (clone_flags & CSIGNAL);
		p->group_leader = p;
		p->tgid = p->pid;
	}

	p->nr_dirtied = 0;
	p->nr_dirtied_pause = 128 >> (PAGE_SHIFT - 10);
	p->dirty_paused_when = 0;

	p->pdeath_signal = 0;
	INIT_LIST_HEAD(&p->thread_group);
	p->task_works = NULL;

	/*
	 * Make it visible to the rest of the system, but dont wake it up yet.
	 * Need tasklist lock for parent etc handling!
	 */
	write_lock_irq(&tasklist_lock);

	/* CLONE_PARENT re-uses the old parent */
	if (clone_flags & (CLONE_PARENT|CLONE_THREAD)) {
		p->real_parent = current->real_parent;
		p->parent_exec_id = current->parent_exec_id;
	} else {
		p->real_parent = current;
		p->parent_exec_id = current->self_exec_id;
	}

	spin_lock(&current->sighand->siglock);

	/*
	 * Copy seccomp details explicitly here, in case they were changed
	 * before holding sighand lock.
	 */
	copy_seccomp(p);

	/*
	 * Process group and session signals need to be delivered to just the
	 * parent before the fork or both the parent and the child after the
	 * fork. Restart if a signal comes in before we add the new process to
	 * it's process group.
	 * A fatal signal pending means that current will exit, so the new
	 * thread can't slip out of an OOM kill (or normal SIGKILL).
	*/
	recalc_sigpending();
	if (signal_pending(current)) {
		spin_unlock(&current->sighand->siglock);
		write_unlock_irq(&tasklist_lock);
		retval = -ERESTARTNOINTR;
		goto bad_fork_free_pid;
	}

	if (likely(p->pid)) {
		ptrace_init_task(p, (clone_flags & CLONE_PTRACE) || trace);

		init_task_pid(p, PIDTYPE_PID, pid);
		if (thread_group_leader(p)) {
			init_task_pid(p, PIDTYPE_PGID, task_pgrp(current));
			init_task_pid(p, PIDTYPE_SID, task_session(current));

			if (is_child_reaper(pid)) {
				ns_of_pid(pid)->child_reaper = p;
				p->signal->flags |= SIGNAL_UNKILLABLE;
			}

			p->signal->leader_pid = pid;
			p->signal->tty = tty_kref_get(current->signal->tty);
			list_add_tail(&p->sibling, &p->real_parent->children);
			list_add_tail_rcu(&p->tasks, &init_task.tasks);
			attach_pid(p, PIDTYPE_PGID);
			attach_pid(p, PIDTYPE_SID);
			__this_cpu_inc(process_counts);
		} else {
			current->signal->nr_threads++;
			atomic_inc(&current->signal->live);
			atomic_inc(&current->signal->sigcnt);
			list_add_tail_rcu(&p->thread_group,
					  &p->group_leader->thread_group);
			list_add_tail_rcu(&p->thread_node,
					  &p->signal->thread_head);
		}
		attach_pid(p, PIDTYPE_PID);
		nr_threads++;
	}

	total_forks++;
	spin_unlock(&current->sighand->siglock);
	syscall_tracepoint_update(p);
	write_unlock_irq(&tasklist_lock);

	proc_fork_connector(p);
	cgroup_post_fork(p);
	if (clone_flags & CLONE_THREAD)
		threadgroup_change_end(current);
	perf_event_fork(p);

	trace_task_newtask(p, clone_flags);
	uprobe_copy_process(p, clone_flags);

	return p;
}
```

###### dup_task_struct

```c
static struct task_struct *dup_task_struct(struct task_struct *orig)
{
	struct task_struct *tsk;
	struct thread_info *ti;
	int node = tsk_fork_get_node(orig);
	int err;
	/* 只是分配了内存 */
	tsk = alloc_task_struct_node(node);
	/* 只是分配了内存 */
	ti = alloc_thread_info_node(tsk, node);
	/* *dst = *src;将src的内容赋值给dst，相当于给新的tsk初始化 */
	err = arch_dup_task_struct(tsk, orig);

	tsk->stack = ti;
	/* 将orig栈的内容赋值给tsk的栈，相当于初始化 */
	setup_thread_stack(tsk, orig);
	clear_user_return_notifier(tsk);
	clear_tsk_need_resched(tsk);
	set_task_stack_end_magic(tsk);

	/*
	 * One for us, one for whoever does the "release_task()" (usually
	 * parent)
	 */
	atomic_set(&tsk->usage, 2);
#ifdef CONFIG_BLK_DEV_IO_TRACE
	tsk->btrace_seq = 0;
#endif
	tsk->splice_pipe = NULL;
	tsk->task_frag.page = NULL;

	account_kernel_stack(ti, 1);

	return tsk;
}
```

```c
#define task_thread_info(task)	((struct thread_info *)(task)->stack)

static inline void setup_thread_stack(struct task_struct *p, struct task_struct *org)
{
    	/* org的thread_info赋值给新的thread_info，结构体赋值
     	 * 栈也目前使用父进程的
     	 */
	*task_thread_info(p) = *task_thread_info(org);
	task_thread_info(p)->task = p;
}
```

```c
static inline void clear_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	clear_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline void clear_tsk_need_resched(struct task_struct *tsk)
{
	clear_tsk_thread_flag(tsk,TIF_NEED_RESCHED);
}
```

```c
static inline unsigned long *end_of_stack(struct task_struct *p)
{
	return (unsigned long *)(task_thread_info(p) + 1);
}
#define STACK_END_MAGIC		0x57AC6E9D
void set_task_stack_end_magic(struct task_struct *tsk)
{
	unsigned long *stackend;
	stackend = end_of_stack(tsk);
	*stackend = STACK_END_MAGIC;	/* for overflow detection */
}
```

###### copy_mm

```c
static int copy_mm(unsigned long clone_flags, struct task_struct *tsk)
{
	struct mm_struct *mm, *oldmm;
	int retval;

	tsk->min_flt = tsk->maj_flt = 0;
	tsk->nvcsw = tsk->nivcsw = 0;
#ifdef CONFIG_DETECT_HUNG_TASK
	tsk->last_switch_count = tsk->nvcsw + tsk->nivcsw;
#endif

	tsk->mm = NULL;
	tsk->active_mm = NULL;

	/*
	 * Are we cloning a kernel thread?
	 * We need to steal a active VM for that..
	 * 内核线程current->mm为NULL
	 */
	oldmm = current->mm;
	if (!oldmm)
		return 0;

	/* initialize the new vmacache entries */
	vmacache_flush(tsk);
	/* 负责进程共享内存 */
	if (clone_flags & CLONE_VM) {
		atomic_inc(&oldmm->mm_users);
		mm = oldmm;
		goto good_mm;
	}
	mm = dup_mm(tsk);
good_mm:
	tsk->mm = mm;
	tsk->active_mm = mm;
	return 0;
}
```

```c
static struct mm_struct *dup_mm(struct task_struct *tsk)
{
	struct mm_struct *mm, *oldmm = current->mm;
	int err;
	mm = allocate_mm();/* 分配struct mm_struct */
    /* 父进程的mm值赋值给新的mm */
	memcpy(mm, oldmm, sizeof(*mm));

	if (!mm_init(mm, tsk)) {
	}

	dup_mm_exe_file(oldmm, mm);

	err = dup_mmap(mm, oldmm);
	mm->hiwater_rss = get_mm_rss(mm);
	mm->hiwater_vm = mm->total_vm;

	if (mm->binfmt && !try_module_get(mm->binfmt->module))
		goto free_pt;

	return mm;
}
```

```c
static struct mm_struct *mm_init(struct mm_struct *mm, struct task_struct *p)
{
	mm->mmap = NULL;
	mm->mm_rb = RB_ROOT;
	mm->vmacache_seqnum = 0;
	atomic_set(&mm->mm_users, 1);
	atomic_set(&mm->mm_count, 1);
	init_rwsem(&mm->mmap_sem);
	INIT_LIST_HEAD(&mm->mmlist);
	mm->core_state = NULL;
	atomic_long_set(&mm->nr_ptes, 0);
	mm_nr_pmds_init(mm);
	mm->map_count = 0;
	mm->locked_vm = 0;
	mm->pinned_vm = 0;
	memset(&mm->rss_stat, 0, sizeof(mm->rss_stat));
	spin_lock_init(&mm->page_table_lock);
	mm_init_cpumask(mm);
	mm_init_aio(mm);
	mm_init_owner(mm, p);
	mmu_notifier_mm_init(mm);
	clear_tlb_flush_pending(mm);
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
	mm->pmd_huge_pte = NULL;
#endif

	if (current->mm) {
		mm->flags = current->mm->flags & MMF_INIT_MASK;
		mm->def_flags = current->mm->def_flags & VM_INIT_DEF_MASK;
	} else {
		mm->flags = default_dump_filter;
		mm->def_flags = 0;
	}

	if (mm_alloc_pgd(mm)) {}

	if (init_new_context(p, mm)) {}

	return mm;
}
```

```c
static inline int mm_alloc_pgd(struct mm_struct *mm)
{
	/* pgd是虚拟地址 */
	mm->pgd = pgd_alloc(mm);
	return 0;
}
```

```c
static int dup_mmap(struct mm_struct *mm, struct mm_struct *oldmm)
{
	struct vm_area_struct *mpnt, *tmp, *prev, **pprev;
	struct rb_node **rb_link, *rb_parent;
	int retval;
	unsigned long charge;
	/* 不是很懂，后面分析 */
	uprobe_start_dup_mmap();
	down_write(&oldmm->mmap_sem);
	flush_cache_dup_mm(oldmm);
	uprobe_dup_mmap(oldmm, mm);
	/*
	 * Not linked in yet - no deadlock potential:
	 */
	down_write_nested(&mm->mmap_sem, SINGLE_DEPTH_NESTING);

	mm->total_vm = oldmm->total_vm;
	mm->shared_vm = oldmm->shared_vm;
	mm->exec_vm = oldmm->exec_vm;
	mm->stack_vm = oldmm->stack_vm;

	rb_link = &mm->mm_rb.rb_node;
	rb_parent = NULL;
	pprev = &mm->mmap;
	retval = ksm_fork(mm, oldmm);

	retval = khugepaged_fork(mm, oldmm);

	prev = NULL;
	for (mpnt = oldmm->mmap; mpnt; mpnt = mpnt->vm_next) {
		struct file *file;

		if (mpnt->vm_flags & VM_DONTCOPY) {
			vm_stat_account(mm, mpnt->vm_flags, mpnt->vm_file,
							-vma_pages(mpnt));
			continue;
		}
		charge = 0;
		if (mpnt->vm_flags & VM_ACCOUNT) {
			unsigned long len = vma_pages(mpnt);

			if (security_vm_enough_memory_mm(oldmm, len)) /* sic */
				goto fail_nomem;
			charge = len;
		}
		tmp = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);

		*tmp = *mpnt;
		INIT_LIST_HEAD(&tmp->anon_vma_chain);
		retval = vma_dup_policy(mpnt, tmp);

		tmp->vm_mm = mm;
		if (anon_vma_fork(tmp, mpnt))
			goto fail_nomem_anon_vma_fork;
		tmp->vm_flags &= ~VM_LOCKED;
		tmp->vm_next = tmp->vm_prev = NULL;
		file = tmp->vm_file;
		if (file) {
			struct inode *inode = file_inode(file);
			struct address_space *mapping = file->f_mapping;

			get_file(file);
			if (tmp->vm_flags & VM_DENYWRITE)
				atomic_dec(&inode->i_writecount);
			i_mmap_lock_write(mapping);
			if (tmp->vm_flags & VM_SHARED)
				atomic_inc(&mapping->i_mmap_writable);
			flush_dcache_mmap_lock(mapping);
			/* insert tmp into the share list, just after mpnt */
			vma_interval_tree_insert_after(tmp, mpnt,
					&mapping->i_mmap);
			flush_dcache_mmap_unlock(mapping);
			i_mmap_unlock_write(mapping);
		}

		/*
		 * Clear hugetlb-related page reserves for children. This only
		 * affects MAP_PRIVATE mappings. Faults generated by the child
		 * are not guaranteed to succeed, even if read-only
		 */
		if (is_vm_hugetlb_page(tmp))
			reset_vma_resv_huge_pages(tmp);

		/*
		 * Link in the new vma and copy the page table entries.
		 */
		*pprev = tmp;
		pprev = &tmp->vm_next;
		tmp->vm_prev = prev;
		prev = tmp;

		__vma_link_rb(mm, tmp, rb_link, rb_parent);
		rb_link = &tmp->vm_rb.rb_right;
		rb_parent = &tmp->vm_rb;

		mm->map_count++;
		retval = copy_page_range(mm, oldmm, mpnt);

		if (tmp->vm_ops && tmp->vm_ops->open)
			tmp->vm_ops->open(tmp);

		if (retval)
			goto out;
	}
	/* a new mm has just been created */
	arch_dup_mmap(oldmm, mm);
	retval = 0;
out:
	up_write(&mm->mmap_sem);
	flush_tlb_mm(oldmm);
	up_write(&oldmm->mmap_sem);
	uprobe_end_dup_mmap();
	return retval;
}

```

###### copy_thread

```c
int copy_thread(unsigned long clone_flags, unsigned long stack_start,
		unsigned long stk_sz, struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);
	unsigned long tls = p->thread.tp_value;

	memset(&p->thread.cpu_context, 0, sizeof(struct cpu_context));

	if (likely(!(p->flags & PF_KTHREAD))) { /* 用户进程 */
        /* 拷贝父进程的pt_regs */
		*childregs = *current_pt_regs();
		childregs->regs[0] = 0;  /* 子进程返回0 */
		if (is_compat_thread(task_thread_info(p))) {
			if (stack_start)
				childregs->compat_sp = stack_start;
		} else {
			/*
			 * Read the current TLS pointer from tpidr_el0 as it may be
			 * out-of-sync with the saved value.
			 */
			asm("mrs %0, tpidr_el0" : "=r" (tls));
			if (stack_start) { /* fork的时候stack_start为空 */
				/* 16-byte aligned stack mandatory on AArch64 */
				if (stack_start & 15)
					return -EINVAL;
				childregs->sp = stack_start;
			}
		}
		/*
		 * If a TLS pointer was passed to clone (4th argument), use it
		 * for the new thread.
		 */
		if (clone_flags & CLONE_SETTLS)
			tls = childregs->regs[3];
	} else {
		memset(childregs, 0, sizeof(struct pt_regs));
		childregs->pstate = PSR_MODE_EL1h;
		p->thread.cpu_context.x19 = stack_start;
		p->thread.cpu_context.x20 = stk_sz;
	}
    /* 设置子进程的pc为ret_from_fork，sp为childregs，sp栈顶保存struct pt_regs */
	p->thread.cpu_context.pc = (unsigned long)ret_from_fork;
	p->thread.cpu_context.sp = (unsigned long)childregs;
	p->thread.tp_value = tls;

	ptrace_hw_copy_thread(p);

	return 0;
}
```

#### ret_fast_syscall

函数一步步返回后，执行ret_fast_syscall

```assembly
ret_fast_syscall:
	disable_irq				// disable interrupts
	ldr	x1, [tsk, #TI_FLAGS]
	and	x2, x1, #_TIF_WORK_MASK
	cbnz	x2, fast_work_pending /* 没执行 */
	enable_step_tsk x1, x2 /* 和调试相关，先不管 */
	kernel_exit 0, ret = 1
```

##### kernel_exit

```assembly
el = 0, ret = 1

.macro	kernel_exit, el, ret = 0
	/* 从sp pt_regs的pc和pstate中加载elr和SPSR */
	ldp	x21, x22, [sp, #S_PC]		// load ELR, SPSR
	.if	\el == 0
	ct_user_enter
	/* 从sp中加载用户栈到x23 */
	ldr	x23, [sp, #S_SP]		// load return stack pointer
	/* 将x23赋值给用户态栈寄存器 */
	msr	sp_el0, x23
	.endif
	/* 恢复elr_el1和spsr_el1 */
	msr	elr_el1, x21			// set up the return data
	msr	spsr_el1, x22
	.if	\ret  /* ret = 1，因为要通过x0返回pid，所以不能恢复x0 */
	/* 加载x1寄存器 */
	ldr	x1, [sp, #S_X1]			// preserve x0 (syscall return)
	.else
	ldp	x0, x1, [sp, #16 * 0]
	.endif
	/* 恢复其他寄存器 */
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
	ldr	lr, [sp, #S_LR] /*恢复lr寄存器*/
	add	sp, sp, #S_FRAME_SIZE		// restore sp，恢复kernel的指针
	eret					// return to kernel，返回用户空间，继续执行父进程代码
.endm
```

#### 子进程执行

#### cpu_switch_to

进程切换

```assembly
ENTRY(cpu_switch_to)
	/* x0为prev，x1为next*/
	/* x8为prev.thread.cpu_context的指针*/
	/* DEFINE(THREAD_CPU_CONTEXT,	offsetof(struct task_struct,
	 * thread.cpu_context));
	 */
	add	x8, x0, #THREAD_CPU_CONTEXT
	mov	x9, sp
	/*
	 * 将x19和x20存放到x8寄存器值对应的地址中，
	 * 然后x8 + 16（因为存了两个64bit的寄存器，所以加16个字节）
	 */
	stp	x19, x20, [x8], #16		// store callee-saved registers
	stp	x21, x22, [x8], #16
	stp	x23, x24, [x8], #16
	stp	x25, x26, [x8], #16
	stp	x27, x28, [x8], #16
	/*x29:fp, x9:sp,lr:pc*/
	stp	x29, x9, [x8], #16
	/*
	 *lr，即x30寄存器，的值为返回调用cpu_switch_to函数的值。即，执行return last
	 */
	str	lr, [x8]
	/*上面的代码把寄存器值存放到prev_task.thread.cpu_context中*/

	/* x8为next.thread.cpu_context的指针*/
	/* 将 next.thread.cpu_context的值存到寄存器中*/
	add	x8, x1, #THREAD_CPU_CONTEXT
	ldp	x19, x20, [x8], #16		// restore callee-saved registers
	ldp	x21, x22, [x8], #16
	ldp	x23, x24, [x8], #16
	ldp	x25, x26, [x8], #16
	ldp	x27, x28, [x8], #16
	/* copy_thread中
	 * p->thread.cpu_context.sp = (unsigned long)childregs;
	 * 将pr_regs保存到x9
	 */
	ldp	x29, x9, [x8], #16
	/* 对于fork刚创建的进程，lr的值为ret_from_fork。已有的进程lr的值为return last的值*/
	ldr	lr, [x8]
	mov	sp, x9/*x9 为sp的值*/
	ret /*ret默认会跳转到x30寄存器（即lr寄存器）的值，这样就开始执行新进程了*/
ENDPROC(cpu_switch_to)
```



在copy_thread中将子进程的p->thread.cpu_context.pc = (unsigned long)ret_from_fork，因此从ret_from_fork开始执行。

##### ret_from_fork

```assembly
ENTRY(ret_from_fork)
	bl	schedule_tail
	cbz	x19, 1f				// not a kernel thread,用户进程，直接调转到1处
	mov	x0, x20
	blr	x19
1:	get_thread_info tsk
	b	ret_to_user			//
ENDPROC(ret_from_fork)
```

##### ret_from_fork

```assembly
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
	/* 执行kernel_exit
	 * 在cpu_switch_to将sp赋值为子进程的pr_regs,子进程和
	 * 父进程pr_regs仅仅x0寄存器值不同，对于子进程为0，对于
	 * 父进程为自己测id。fork的父子进程进程空间是一样的，子进
	 * 程返回和父进程同样的地方开始执行，但是因为返回值不一样，
	 * 所以开始执行子进程的内容
	 */
	kernel_exit 0, ret = 0
ENDPROC(ret_to_user)
```

