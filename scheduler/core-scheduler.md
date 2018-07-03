# core-scheduler

[TOC]

task休眠的例子：

## msleep()

```c
void msleep(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs) + 1;

	while (timeout)
		timeout = schedule_timeout_uninterruptible(timeout);
}
```

### schedule_timeout_uninterruptible

```c
signed long __sched schedule_timeout_uninterruptible(signed long timeout)
{
    /*设置task_struct->state = TASK_UNINTERRUPTIBLE*/
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_uninterruptible);
```

### schedule_timeout

```c
signed long __sched schedule_timeout(signed long timeout)
{
	struct timer_list timer;
	unsigned long expire;

	switch (timeout)
	{
	case MAX_SCHEDULE_TIMEOUT:
		/*
		 * These two special cases are useful to be comfortable
		 * in the caller. Nothing more. We could take
		 * MAX_SCHEDULE_TIMEOUT from one of the negative value
		 * but I' d like to return a valid offset (>=0) to allow
		 * the caller to do everything it want with the retval.
		 */
		schedule();
		goto out;
	default:
		/*
		 * Another bit of PARANOID. Note that the retval will be
		 * 0 since no piece of kernel is supposed to do a check
		 * for a negative retval of schedule_timeout() (since it
		 * should never happens anyway). You just have the printk()
		 * that will tell you if something is gone wrong and where.
		 */
		if (timeout < 0) {
			printk(KERN_ERR "schedule_timeout: wrong timeout "
				"value %lx\n", timeout);
			dump_stack();
			current->state = TASK_RUNNING;
			goto out;
		}
	}
	expire = timeout + jiffies;
	setup_timer_on_stack(&timer, process_timeout, (unsigned long)current);
    /*创建一个timer，定时器的超时值为睡眠时间*/
	__mod_timer(&timer, expire, false, TIMER_NOT_PINNED);
    /*执行调度函数，切换到其他进程*/
	schedule();
    /*重启调度回来之后，删除timer*/
	del_singleshot_timer_sync(&timer);
	/* Remove the timer from the object tracker */
	destroy_timer_on_stack(&timer);
	timeout = expire - jiffies;
 out:
	return timeout < 0 ? 0 : timeout;
}
```

###process_timeout

```c
static void process_timeout(unsigned long __data)
{
	/*到期后唤醒该进程*/
	wake_up_process((struct task_struct *)__data);
}
```

## schedule()

```c
asmlinkage __visible void __sched schedule(void)
{
	struct task_struct *tsk = current;
    /*注释中是用于避免死锁的，和块设备相关*/
	sched_submit_work(tsk);
	do {
		__schedule();
	} while (need_resched());
}

```

### __schedule

```c
/*
 * __schedule() is the main scheduler function.
 *
 * The main means of driving the scheduler and thus entering this function are:
 *
 *   1. Explicit blocking: mutex, semaphore, waitqueue, etc.
 *
 *   2. TIF_NEED_RESCHED flag is checked on interrupt and userspace return
 *      paths. For example, see arch/x86/entry_64.S.
 *
 *      To drive preemption between tasks, the scheduler sets the flag in timer
 *      interrupt handler scheduler_tick().
 *
 *   3. Wakeups don't really cause entry into schedule(). They add a
 *      task to the run-queue and that's it.
 *
 *      Now, if the new task added to the run-queue preempts the current
 *      task, then the wakeup sets TIF_NEED_RESCHED and schedule() gets
 *      called on the nearest possible occasion:
 *
 *       - If the kernel is preemptible (CONFIG_PREEMPT=y):
 *
 *         - in syscall or exception context, at the next outmost
 *           preempt_enable(). (this might be as soon as the wake_up()'s
 *           spin_unlock()!)
 *
 *         - in IRQ context, return from interrupt-handler to
 *           preemptible context
 *
 *       - If the kernel is not preemptible (CONFIG_PREEMPT is not set)
 *         then at the next:
 *
 *          - cond_resched() call
 *          - explicit schedule() call
 *          - return from syscall or exception to user-space
 *          - return from interrupt-handler to user-space
 *
 * WARNING: all callers must re-check need_resched() afterward and reschedule
 * accordingly in case an event triggered the need for rescheduling (such as
 * an interrupt waking up a task) while preemption was disabled in __schedule().
 */
static void __sched __schedule(void)
{
	struct task_struct *prev, *next;
	unsigned long *switch_count;
	struct rq *rq;
	int cpu;
	/*禁止抢占*/
	preempt_disable();
	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	rcu_note_context_switch();
	prev = rq->curr;

	schedule_debug(prev);
	/* 
	 * 如果支持HRTICK feature，则取消hrtick_clear
	 * HRTICK的func中会调用调度类的rq->curr->sched_class->task_tick(rq, rq->curr, 1);
	 * task_tick一般情况只在tick中调用。在HRTICK调用可以更快的进行抢占
	 * kernel中的注释是Use HR-timers to deliver accurate preemption points.
	 * 提供精确的抢占点
	 */
	if (sched_feat(HRTICK))
		hrtick_clear(rq);

	/*
	 * Make sure that signal_pending_state()->signal_pending() below
	 * can't be reordered with __set_current_state(TASK_INTERRUPTIBLE)
	 * done by the caller to avoid the race with signal_wake_up().
	 */
	smp_mb__before_spinlock();
	raw_spin_lock_irq(&rq->lock);

	rq->clock_skip_update <<= 1; /* promote REQ to ACT */

	switch_count = &prev->nivcsw;
    /*
     * 如果是内核抢占，preempt_count() & PREEMPT_ACTIVE会不为0
     * 也可看出如果是kernel抢占，则不会调用deactivate_task
     * state： -1 unrunnable, 0 runnable, >0 stopped
     */
	if (prev->state && !(preempt_count() & PREEMPT_ACTIVE)) {
        /*判断有没有信号要处理，如果有这把prev->state = TASK_RUNNING*/
		if (unlikely(signal_pending_state(prev->state, prev))) {
			prev->state = TASK_RUNNING;
		} else {
 			/*将prev task中run-queue中移除*/
			deactivate_task(rq, prev, DEQUEUE_SLEEP);
			prev->on_rq = 0;/*表示prev task不再run-queue上*/

			/*
			 * If a worker went to sleep, notify and ask workqueue
			 * whether it wants to wake up a task to maintain
			 * concurrency.
			 */
			if (prev->flags & PF_WQ_WORKER) {
				struct task_struct *to_wakeup;
				to_wakeup = wq_worker_sleeping(prev, cpu);
				if (to_wakeup)
					try_to_wake_up_local(to_wakeup);
			}
		}
		switch_count = &prev->nvcsw;
	}
	/*如果task->on_rq为1，如果没有执行上面else路径则这种情况会成立*/
	if (task_on_rq_queued(prev))
		update_rq_clock(rq);
	/*选择先一个进程，会调用到调度了中的pick_next_task*/
	next = pick_next_task(rq, prev);
    /*清楚TIF_NEED_RESCHED标志*/
	clear_tsk_need_resched(prev);
    /*该函数除了x86，其他平台为空函数*/
	clear_preempt_need_resched();
	rq->clock_skip_update = 0;

	if (likely(prev != next)) {
		rq->nr_switches++;
		rq->curr = next;/*设置当前run-queue的task为next*/
		++*switch_count;
		rq = context_switch(rq, prev, next); /* unlocks the rq */
		cpu = cpu_of(rq);
	} else
		raw_spin_unlock_irq(&rq->lock);

	post_schedule(rq);

	sched_preempt_enable_no_resched();
}
```

### __schedule注释

__schedule() is the main scheduler function.

__scheduler是主要的scheduler函数

The main means of driving the scheduler and thus entering this function are:

主要调用scheduler 的入口函数是：

  1. Explicit blocking: mutex, semaphore, waitqueue, etc.

     显示的阻塞：mutex，semaphore，waitqueue等到

  2. TIF_NEED_RESCHED flag is checked on interrupt and userspace return paths. For example, see arch/x86/entry_64.S.

     To drive preemption between tasks, the scheduler sets the flag in timer interrupt handler scheduler_tick().

     中断和用户空间返回时检查TIF_NEED_RESCHED 。

     在task之间驱动抢占，scheduler设置该flag在timer中断处理函数scheduler_tick()中。

  3. Wakeups don't really cause entry into schedule(). They add a task to the run-queue and that's it.

     wakeup不会真正的调用schedule()，他们就是将task添加到run-queue中。

     Now, if the new task added to the run-queue preempts the current task, then the wakeup sets TIF_NEED_RESCHED and schedule() gets called on the nearest possible occasion:

     如果新的task加入run-queue抢占当前task，然后wakeup设置TIF_NEED_RESCHED，schedule()将会在最近的可能场景被调用：

      - If the kernel is preemptible (CONFIG_PREEMPT=y):

        如果支持kernel抢占

        - in syscall or exception context, at the next outmost preempt_enable(). (this might be as soon as the wake_up()'s spin_unlock()!)

          在系统调用，或者异常上下文，在下一个最外面的preempt_enable()。(这可能会在wake_up()的spin_unlock()之后出现!)

        - in IRQ context, return from interrupt-handler to preemptible context

          在IRQ上下文，从interrupt处理返回返回到抢占上下文。

      - If the kernel is not preemptible (CONFIG_PREEMPT is not set) then at the next:

        如果不支持kenrel抢占，下面的会调用scheduler

         - cond_resched() call
         - explicit schedule() call
         - return from syscall or exception to user-space
         - return from interrupt-handler to user-space

WARNING: all callers must re-check need_resched() afterward and reschedule accordingly in case an event triggered the need for rescheduling (such as an interrupt waking up a task) while preemption was disabled in __schedule().

**警告：在这之后所有的调用这必须重新检查need_resched()，并行重新调度，以防事件触发重新调度（比如中断唤醒一个task），由于在__schedule()中，抢占是禁止的。**

### context_switch

```c
static inline struct rq *
context_switch(struct rq *rq, struct task_struct *prev,
	       struct task_struct *next)
{
	struct mm_struct *mm, *oldmm;
	/*一些准备工作*/
	prepare_task_switch(rq, prev, next);

	mm = next->mm;
	oldmm = prev->active_mm;
	/*
	 * For paravirt, this is coupled with an exit in switch_to to
	 * combine the page table reload and the switch backend into
	 * one hypercall.
	 */
	arch_start_context_switch(prev);/*arm64该函数为空*/
	/* 
	 * mm为空表示是kernel thread，kernel thread使用的是内核空间堆栈。
	 * 借用prev的mm，而且也不用切换进程的页表。
	 */
	if (!mm) {
		next->active_mm = oldmm;
		atomic_inc(&oldmm->mm_count);/*增加引用计数，在什么时候减少*/
		enter_lazy_tlb(oldmm, next);
	} else
		switch_mm(oldmm, mm, next);

	if (!prev->mm) {/*表示是kernel 线程*/
		prev->active_mm = NULL;
        /*
         * 如果之前的也是kernel线程，则将之前的mm保存到run qeue中
         * 如果是用户进程之间切换，rq->prev_mm为NULL
         */
		rq->prev_mm = oldmm;
	}
	/*
	 * Since the runqueue lock will be released by the next
	 * task (which is an invalid locking op but in the case
	 * of the scheduler it's an obvious special-case), so we
	 * do an early lockdep release here:
	 */
	spin_release(&rq->lock.dep_map, 1, _THIS_IP_);

	context_tracking_task_switch(prev, next);
	/* Here we just switch the register state and the stack. */
	switch_to(prev, next, prev);
	barrier();

	return finish_task_switch(prev);
}
```
###  switch_mm
```c
/*
 * This is the actual mm switch as far as the scheduler is concerned.  No registers are
 * touched.  We avoid calling the CPU specific function when the mm hasn't actually changed.
 */
static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();
	/*
	 * init_mm.pgd does not contain any user mappings and it is always
	 * active for kernel addresses in TTBR1. Just set the reserved TTBR0.
	 * init_mm是kernel第一个task的struct mm_struct， INIT_TASK的时候会将active_mm
	 * 初始化为init_mm。什么时候mm赋值为init_mm？,在idle_task_exit中next会为init_mm
	 */
	if (next == &init_mm) {
        /*
         * Set TTBR0 to empty_zero_page. No translations will be possible via TTBR0
         */
		cpu_set_reserved_ttbr0();
		return;
	}
	if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)) || prev != next)
		check_and_switch_context(next, tsk);
}
```

#### check_and_switch_context

```c
static inline void check_and_switch_context(struct mm_struct *mm,
					    struct task_struct *tsk)
{
	/*
	 * Required during context switch to avoid speculative page table
	 * walking with the wrong TTBR.
	 * 将TTBR0设置为empty_zero_page，No translations will be possible via TTBR0
	 */
	cpu_set_reserved_ttbr0();
	/* 
	 * cpu_last_asid被初始化为ASID_FIRST_VERSION,ASID_FIRST_VERSION = (1 << MAX_ASID_BITS)
	 * 然后cpu_last_asid会加1，所以正常的context.id都会大于ASID_FIRST_VERSION，if表达式为false
	 * 则mm->context.id为0，新的进程会初始化为0，或者mm->context.id小于ASID_FIRST_VERSION，这个
	 * 不可能，或者mm->context.id最高位的1和cpu_last_asid最高位的1不一样，这也就是说cpu_last_asid
	 * 自增已经超过了2^16 - 1,表示意见ASID已经分配完。则需要重新分配ASID，也意味着要flush tlb
	 * 综上，只有mm->context.id为0才会走到该if路径。
	 */
	if (!((mm->context.id ^ cpu_last_asid) >> MAX_ASID_BITS))
		/*
		 * The ASID is from the current generation, just switch to the
		 * new pgd. This condition is only true for calls from
		 * context_switch() and interrupts are already disabled.
		 */
		cpu_switch_mm(mm->pgd, mm);
	else if (irqs_disabled())
		/*
		 * Defer the new ASID allocation until after the context
		 * switch critical region since __new_context() cannot be
		 * called with interrupts disabled.
		 * 延迟新的ASID分配。直到到context switch 临界区之后，由于__new_context()
		 * 不能在中断禁止情况下调用。
		 * 进入该路径，只会设置一个flag，在finish_task_switch只会会进行处理。
		 * 对于一个新的进程mm->pgd会分配一个新的，执行到该path，用户空间页表是空的。
		 */
		set_ti_thread_flag(task_thread_info(tsk), TIF_SWITCH_MM);
	else
		/*
		 * That is a direct call to switch_mm() or activate_mm() with
		 * interrupts enabled and a new context.
		 * 直接调用switch_mm() or activate_mm()会进入到该路径
		 */
		switch_new_context(mm);
}
```

#### cpu_switch_mm

```c
#define cpu_switch_mm(pgd,mm)				\
do {							\
	BUG_ON(pgd == swapper_pg_dir);			\
	cpu_do_switch_mm(virt_to_phys(pgd),mm);		\
} while (0)
```

#### cpu_do_switch_mm

```assembly
ENTRY(cpu_do_switch_mm)
	mmid	w1, x1				// get mm->context.id
	/*将x1寄存器从48bit开始复制16bit到x0寄存器的48-63bit，x0寄存器就是pgd*/
	bfi	x0, x1, #48, #16		// set the ASID
	msr	ttbr0_el1, x0			// set TTBR0，将pgd写入ttbr0_el1
	isb
	ret
ENDPROC(cpu_do_switch_mm)
```

```assembly
.macro	mmid, rd, rn
ldr	\rd, [\rn, #MM_CONTEXT_ID]
.endm
```

#### switch_new_context

```c
static inline void switch_new_context(struct mm_struct *mm)
{
	unsigned long flags;
	__new_context(mm);
	local_irq_save(flags);
	cpu_switch_mm(mm->pgd, mm);
	local_irq_restore(flags);
}
```

#### __new_context

```c
void __new_context(struct mm_struct *mm)
{
	unsigned int asid;
	/*读取ID_AA64MMFR0_EL1寄存器，获取ASID bits,在qemu上返回16*/
	unsigned int bits = asid_bits();

	raw_spin_lock(&cpu_asid_lock);
#ifdef CONFIG_SMP
	/*
	 * Check the ASID again, in case the change was broadcast from another
	 * CPU before we acquired the lock.
	 * cpu_last_asid有可能在其他cpu总进行了更改，所以再次进行判断
	 */
	if (!unlikely((mm->context.id ^ cpu_last_asid) >> MAX_ASID_BITS)) {
		cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
		raw_spin_unlock(&cpu_asid_lock);
		return;
	}
#endif
	/*
	 * At this point, it is guaranteed that the current mm (with an old
	 * ASID) isn't active on any other CPU since the ASIDs are changed
	 * simultaneously via IPI.
	 */
	asid = ++cpu_last_asid;
	/*
	 * If we've used up all our ASIDs, we need to start a new version and
	 * flush the TLB.
	 */
	if (unlikely((asid & ((1 << bits) - 1)) == 0)) {/*硬件ASID使用完毕*/
		/* increment the ASID version */
 		/* 硬件ASID也有可能是8位 */
		cpu_last_asid += (1 << MAX_ASID_BITS) - (1 << bits);
 		/*cpu_last_asid == 0表示cpu_last_asid已经超过int的表示范围，出现了溢出*/
		if (cpu_last_asid == 0) 
			cpu_last_asid = ASID_FIRST_VERSION;
		asid = cpu_last_asid + smp_processor_id();
 		/*硬件ASID溢出，会flush tlb*/
		flush_context();
#ifdef CONFIG_SMP
		smp_wmb();
		smp_call_function(reset_context, NULL, 1);
#endif
		cpu_last_asid += NR_CPUS - 1;
	}
	set_mm_context(mm, asid);
	raw_spin_unlock(&cpu_asid_lock);
}
```

#### set_mm_context

```c
static inline void set_mm_context(struct mm_struct *mm, unsigned int asid)
{
	mm->context.id = asid;
	cpumask_copy(mm_cpumask(mm), cpumask_of(smp_processor_id()));
}
```

### switch_to

```c
#define switch_to(prev, next, last)					\
	do {								\
		((last) = __switch_to((prev), (next)));			\
	} while (0)
```

#### __switch_to

```c
struct task_struct *__switch_to(struct task_struct *prev,
				struct task_struct *next)
{
	struct task_struct *last;

	fpsimd_thread_switch(next);
	tls_thread_switch(next);
	hw_breakpoint_thread_switch(next);
	contextidr_thread_switch(next);
	/*
	 * Complete any pending TLB or cache maintenance on this CPU in case
	 * the thread migrates to a different CPU.
	 */
	dsb(ish);
	/* the actual thread switch */
	last = cpu_switch_to(prev, next);
	return last;
}
```

#### cpu_switch_to

```assembly
/*
 * Register switch for AArch64. The callee-saved registers need to be saved
 * and restored. On entry:
 *   x0 = previous task_struct (must be preserved across the switch)
 *   x1 = next task_struct
 * Previous and next are guaranteed not to be the same.
 *
 */
ENTRY(cpu_switch_to)
	/* x0为prev，x1为next*/
	/* x8为prev.thread.cpu_context的指针*/
	/* DEFINE(THREAD_CPU_CONTEXT,	offsetof(struct task_struct, thread.cpu_context));*/
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
	ldp	x29, x9, [x8], #16
	/* 对于fork刚创建的进程，lr的值为ret_from_fork。已有的进程lr的值为return last的值*/
	ldr	lr, [x8]
	mov	sp, x9/*x9 为sp的值*/
	ret /*ret默认会跳转到x30寄存器（即lr寄存器）的值，这样就开始执行新进程了*/
ENDPROC(cpu_switch_to)
```

#### cpu_context

```c
struct task_struct {
    struct thread_struct thread;
}
struct thread_struct {
	struct cpu_context	cpu_context;	/* cpu context */
	unsigned long		tp_value;
	struct fpsimd_state	fpsimd_state;
	unsigned long		fault_address;	/* fault info */
	unsigned long		fault_code;	/* ESR_EL1 value */
	struct debug_info	debug;		/* debugging */
};

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
```

#### switch_to三个参数

从switch_to看，它有三个参数，如下：

```c
#define switch_to(prev, next, last)					\
	do {								\
		((last) = __switch_to((prev), (next)));			\
	} 
```

虽然在调用switch_to的时候，传递的prev和last是相同的，但是在进程重新被调度，并且返回的时候，last和prev并不一定是相同的。

假设有两个进程A，B

在进程A的时候调用了switch_to，进一步最终调用到\__switch_to，__switch_to可以分为两部分：

```c
struct task_struct *__switch_to(struct task_struct *prev,
				struct task_struct *next)
{
	/*调用之后就会切换到next task*/
	last = cpu_switch_to(prev, next);
    /*重新调用到prev进程后执行*/
	return last;
}
```

进程A中执行cpu_switch_to之后，会把进程A的PC，LR，FP，X28到X19寄存器保存到A进程的cpu_context中，然后执行B进程，在B进程执行中，有可能被抢占，有可能进程B最终会调用到\__schedule等等，然后切换到另一个进程X，X也需也和B进程一样，总之历经N多次进程切换，然后Y进程执行__schedule，next进程为A，然后调用到cpu_switch_to，在cpu_switch_to中使用A进程的cpu_context还原寄存器，然后就跳转到A进程执行,这个时候A进程执行return last;代码，last是什么呢，这里的last就是Y进程，但是我们调用switch_to的时候传递的last就是prev，这里怎么变了呢。注意在cpu_switch_to中，并不保存X0寄存器，所以说每次执行cpu_switch_to的时候，X0寄存器就是prev指针，而在ARM64中返回值，是使用X0寄存器传递的，所以last为Y进程。综上，可以看到在进程切换的时候，除了next，和prev还有last。

### finish_task_switch

```c
/*这里的prev就是A进程经过了N多次调用之后，然后调用__schedule切换到A进程的进程Y*/
static struct rq *finish_task_switch(struct task_struct *prev)
	__releases(rq->lock)
{
	struct rq *rq = this_rq();
	struct mm_struct *mm = rq->prev_mm;
	long prev_state;
	/*将run queue的prev_mm置NULL*/
	rq->prev_mm = NULL;

	/*
	 * A task struct has one reference for the use as "current".
	 * If a task dies, then it sets TASK_DEAD in tsk->state and calls
	 * schedule one last time. The schedule call will never return, and
	 * the scheduled task must drop that reference.
	 * The test for TASK_DEAD must occur while the runqueue locks are
	 * still held, otherwise prev could be scheduled on another cpu, die
	 * there before we look at prev->state, and then the reference would
	 * be dropped twice.
	 *		Manfred Spraul <manfred@colorfullife.com>
	 */
	prev_state = prev->state;
	vtime_task_switch(prev);
	finish_arch_switch(prev);
	perf_event_task_sched_in(prev, current);
	finish_lock_switch(rq, prev);
	finish_arch_post_lock_switch();

	fire_sched_in_preempt_notifiers(current);
    /*
     * mm =  rq->prev_mm
     * 如果切换的时候prev为内核线程，会将rq->prev_mm设置为oldmm，其他情况rq->prev_mm都
     * 为NULL，如果mm不为NULL则表示之前在进程切换的时候，借用了其他用户空间的进程，
     * mmdrop中会对mm->mm_count减1
     */
	if (mm)
		mmdrop(mm);
	if (unlikely(prev_state == TASK_DEAD)) {
		if (prev->sched_class->task_dead)
			prev->sched_class->task_dead(prev);

		/*
		 * Remove function-return probe instances associated with this
		 * task and put them back on the free list.
		 */
		kprobe_flush_task(prev);
		put_task_struct(prev);
	}

	tick_nohz_task_switch(current);
	return rq;
}
```

#### finish_arch_post_lock_switch

```c
static inline void finish_arch_post_lock_switch(void)
{
	/*
	 * 在check_and_switch_context中可能会设置TIF_SWITCH_MM，
	 * 设置该flag会延后分配新的ASID和执行cpu_switch_mm
	 */
	if (test_and_clear_thread_flag(TIF_SWITCH_MM)) {
		struct mm_struct *mm = current->mm;
		unsigned long flags;

		__new_context(mm);

		local_irq_save(flags);
		cpu_switch_mm(mm->pgd, mm);
		local_irq_restore(flags);
	}
}
```

### 调度时机

从对`__schedule`的注释用可以看到调度的时机

1. 阻塞，mutex，等待队列，信号量等。

2. 中断和用户空间返回（系统调用）检查TIF_NEED_RESCHED flag。

   A. tick中断`scheduler_tick()`会设置TIF_NEED_RESCHED flag。进程时间片用完。

   B. 抢占当前进程会设置TIF_NEED_RESCHED 。高优先级抢占低优先级。

   C. 用户空间设置进程优先级的时候，可能会设置TIF_NEED_RESCHED 。设置调度参数。

   D. 调用yeld。

   E. 内核定时器。

   上面只是列出了几个场景，并不是所有。

#### 内核抢占

在发生内核抢占的时候会调用

```c
__preempt_count_add(PREEMPT_ACTIVE);
```

我们看到在`__schedule()`会对PREEMPT_ACTIVE进行判断

```c
if (prev->state && !(preempt_count() & PREEMPT_ACTIVE)) {
	if (unlikely(signal_pending_state(prev->state, prev))) {
		prev->state = TASK_RUNNING;
	} else {
		deactivate_task(rq, prev, DEQUEUE_SLEEP);
		prev->on_rq = 0;
	}
	switch_count = &prev->nvcsw;
}
```

可以看到如果prev->state等于0（表示prev是running状态，如果是其他调度，会先设置state的状态）或者preempt_count() & PREEMPT_ACTIVE不为0，那么就直接执行`context_switch()`抢占当前进程，主要是为了快速执行。**这时被抢占的task还在run queue中。**

`preempt_count()`其实就是检查preempt_count，该**值为0表示该进程时可以抢占的**。如果是内核抢占，会设置PREEMPT_ACTIVE。表示是kernel抢占。

为什么执行kernel抢占要设置PREEMPT_ACTIVE呢。

从`if (prev->state && !(preempt_count() & PREEMPT_ACTIVE))`可以看到如果prev->state == 0则直接执行进程切换，这个时候，该进程还在run queue中，那么在以后的时间就会重新调用到该进程。如果prev->state != 0,则需要检查preempt_count() & PREEMPT_ACTIVE)。看下面一个例子

```c
for (;;) {
	prepare_to_wait(&wq, &__wait, TASK_UNINTERRUPTIBLE);
	if (condition)
		break;
	schedule();
}
```

第一种情形：

​	在第二行中，如果设置了task的状态（state不为0），但是还没有将该task添加到wq中，这个时候如果出现了kernel抢占，如果不判断preempt_count() & PREEMPT_ACTIVE)，那么就会执行`deactivate_task(rq, prev, DEQUEUE_SLEEP);`将该task中run queue中移除，这个时候没有机会将该task添加到run queue中，那么该task将没有机会在运行了。调用wake_up，这时等待对了中是没有该task的。

第二种情形：

​	已经执行了schedule，然后等到了wake_up，这个时候该进程不唤醒重新调用prepare_to_wait，如果这个时候，发生了内核抢占，如果没有preempt_count() & PREEMPT_ACTIVE)检查，则该进程也会被移除等待队列，**如果只有这一次wake_up**,那么该task以后也没有机会执行了。

#### 中断上下文进入抢占

```assembly
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
```

preempt_schedule_irq会调用preempt_schedule_irq

```c
/*
 * this is the entry point to schedule() from kernel preemption
 * off of irq context.
 * Note, that this is called and return with irqs disabled. This will
 * protect us against recursive calling from irq.
 */
/* 中断上下kernel preemption off进入 schedule()的入口，进入schedule()的时候
 * kernel preemption是关的
 */
asmlinkage __visible void __sched preempt_schedule_irq(void)
{
	enum ctx_state prev_state;

	/* Catch callers which need to be fixed */
	BUG_ON(preempt_count() || !irqs_disabled());

	prev_state = exception_enter();

	do {
		__preempt_count_add(PREEMPT_ACTIVE);
		/* 在中断处理函数中中断是disable的，所以这里需要enable */
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

### PREEMPT_ACTIVE作用域

   ```c
/*
 * this is the entry point to schedule() from in-kernel preemption
 * off of preempt_enable. Kernel preemptions off return from interrupt
 * occur there and call schedule directly.
 */
 
 /* 从preempt_enable 进入schedule()，当然进入后preemption是关闭的。*/
asmlinkage __visible void __sched notrace preempt_schedule(void)
{
	/*
	 * If there is a non-zero preempt_count or interrupts are disabled,
	 * we do not want to preempt the current task. Just return..
	 */
	if (likely(!preemptible()))
		return;

	preempt_schedule_common();
}
   ```

从上面的代码看

```c
__preempt_count_add(PREEMPT_ACTIVE);
local_irq_enable();
__schedule();
local_irq_disable();
__preempt_count_sub(PREEMPT_ACTIVE);
```

先是调用了`__preempt_count_add(PREEMPT_ACTIVE);`然后调用`__schedule();`这样是不是表示在没有调用`local_irq_disable();`之前系统是不可抢占呢？但是调用`__preempt_count_sub(PREEMPT_ACTIVE);`的时机应该是在重新调度道该task才会调用`__preempt_count_sub(PREEMPT_ACTIVE);`这不知道需要什么时候。

起始看下`__preempt_count_add(PREEMPT_ACTIVE);`的实现

```c
static __always_inline void __preempt_count_add(int val)
{
	*preempt_count_ptr() += val;
}
```

```c
static __always_inline int *preempt_count_ptr(void)
{
	return &current_thread_info()->preempt_count;c
}
```

**可以看出是否可以抢占其实只是针对该task的，该task不能抢占，并不表示要运行的task不能抢占。**

在后面的kenrel中PREEMPT_ACTIVE已经被去掉了，`__schedule`变成了`static void __sched __schedule(bool preempt)`增加了一个参数，表示是不是kernel抢占。这样显得更合理。个人感觉使用PREEMPT_ACTIVE显得比较别扭。

## ASID

ASID全程adress space ID。

CPU上运行了若干的用户空间的进程和内核线程，为了加快性能，CPU中往往设计了TLB和Cache这样的HW block。Cache为了更快的访问main memory中的数据和指令，而TLB是为了更快的进行地址翻译而将部分的页表内容缓存到了Translation lookasid buffer中，避免了从main memory访问页表的过程。

假如不做任何的处理，那么在进程A切换到进程B的时候，TLB和Cache中同时存在了A和B进程的数据。对于kernel space其实无所谓，因为所有的进程都是共享的，但是对于A和B进程，它们各种有自己的独立的用户地址空间，也就是说，同样的一个虚拟地址X，在A的地址空间中可以被翻译成Pa，而在B地址空间中会被翻译成Pb，如果在地址翻译过程中，TLB中同时存在A和B进程的数据，那么旧的A地址空间的缓存项会影响B进程地址空间的翻译，因此，在进程切换的时候，需要有tlb的操作，以便清除旧进程的影响

当系统发生进程切换，从进程A切换到进程B，从而导致地址空间也从A切换到B，这时候，我们可以认为在A进程执行过程中，所有TLB和Cache的数据都是for A进程的，一旦切换到B，整个地址空间都不一样了，因此需要全部flush掉（注意：我这里使用了linux内核的术语，flush就是意味着将TLB或者cache中的条目设置为无效，对于一个ARM平台上的嵌入式工程师，一般我们会更习惯使用invalidate这个术语，不管怎样，在本文中，flush等于invalidate）。

这种方案当然没有问题，当进程B被切入执行的时候，其面对的CPU是一个干干净净，从头开始的硬件环境，TLB和Cache中不会有任何的残留的A进程的数据来影响当前B进程的执行。当然，稍微有一点遗憾的就是在B进程开始执行的时候，TLB和Cache都是冰冷的（空空如也），因此，B进程刚开始执行的时候，TLB miss和Cache miss都非常严重，从而导致了性能的下降。

### 如何提高TLB的性能？

对一个模块的优化往往需要对该模块的特性进行更细致的分析、归类，上一节，我们采用进程地址空间这样的术语，其实它可以被进一步细分为内核地址空间和用户地址空间。对于所有的进程（包括内核线程），内核地址空间是一样的，因此对于这部分地址翻译，无论进程如何切换，内核地址空间转换到物理地址的关系是永远不变的。对于用户地址空间，各个进程都有自己独立的地址空间，在进程A切换到B的时候，TLB中的和A进程相关的entry对于B是完全没有任何意义的，需要flush掉。

在这样的思路指导下，我们其实需要区分global和local（其实就是process-specific的意思）这两种类型的地址翻译，因此，在页表描述符中往往有一个bit来标识该地址翻译是global还是local的，同样的，在TLB中，这个标识global还是local的flag也会被缓存起来。有了这样的设计之后，我们可以根据不同的场景而flush all或者只是flush local tlb entry。

### 特殊情况的考量

我们考虑下面的场景：进程A切换到内核线程K之后，其实地址空间根本没有必要切换，线程K能访问的就是内核空间的那些地址，而这些地址也是和进程A共享的。既然没有切换地址空间，那么也就不需要flush 那些进程特定的tlb entry了，当从K切换会A进程后，那么所有TLB的数据都是有效的，从大大降低了tlb miss。此外，对于多线程环境，切换可能发生在一个进程中的两个线程，这时候，线程在同样的地址空间，也根本不需要flush tlb。

### 进一步提升TLB的性能 

当然可以，不过这需要我们在设计TLB block的时候需要识别process specific的tlb entry，也就是说，TLB block需要感知到各个进程的地址空间。为了完成这样的设计，我们需要标识不同的address space，这里有一个术语叫做ASID（address space ID）。原来TLB查找是通过虚拟地址VA来判断是否TLB hit。有了ASID的支持后，TLB hit的判断标准修改为（虚拟地址＋ASID），ASID是每一个进程分配一个，标识自己的进程地址空间。TLB block如何知道一个tlb entry的ASID呢？一般会来自CPU的系统寄存器（对于ARM64平台，它来自TTBRx_EL1寄存器），这样在TLB block在缓存（VA-PA-Global flag）的同时，也就把当前的ASID缓存在了对应的TLB entry中，这样一个TLB entry中包括了（VA-PA-Global flag-ASID）。

有了ASID的支持后，A进程切换到B进程再也不需要flush tlb了，因为A进程执行时候缓存在TLB中的残留A地址空间相关的entry不会影响到B进程，虽然A和B可能有相同的VA，但是ASID保证了硬件可以区分A和B进程地址空间。

ASID部分机会全部引用http://www.wowotech.net/process_management/context-switch-tlb.html