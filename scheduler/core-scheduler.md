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

### __schedule注释：

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
		atomic_inc(&oldmm->mm_count);
		enter_lazy_tlb(oldmm, next);
	} else
		switch_mm(oldmm, mm, next);

	if (!prev->mm) {
		prev->active_mm = NULL;
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
 * This is the actual mm switch as far as the scheduler
 * is concerned.  No registers are touched.  We avoid
 * calling the CPU specific function when the mm hasn't
 * actually changed.
 */
static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();
	/*
	 * init_mm.pgd does not contain any user mappings and it is always
	 * active for kernel addresses in TTBR1. Just set the reserved TTBR0.
	 * init_mm是kernel第一个task的struct mm_struct
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

### check_and_switch_context

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
	 * context.id没有溢出，直接切换mm 
	 * cpu_last_asid为上一次分配的context.id，MAX_ASID_BITS为16。armv8 asid为16bit即硬件
	 * 支持的asid为2^16 -1=65536，如果
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
		 */
		set_ti_thread_flag(task_thread_info(tsk), TIF_SWITCH_MM);
	else
		/*
		 * That is a direct call to switch_mm() or activate_mm() with
		 * interrupts enabled and a new context.
		 */
		switch_new_context(mm);
}
```

### cpu_switch_mm

```c
#define cpu_switch_mm(pgd,mm)				\
do {							\
	BUG_ON(pgd == swapper_pg_dir);			\
	cpu_do_switch_mm(virt_to_phys(pgd),mm);		\
} while (0)
```

### cpu_do_switch_mm

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



### ASID

CPU上运行了若干的用户空间的进程和内核线程，为了加快性能，CPU中往往设计了TLB和Cache这样的HW block。Cache为了更快的访问main memory中的数据和指令，而TLB是为了更快的进行地址翻译而将部分的页表内容缓存到了Translation lookasid buffer中，避免了从main memory访问页表的过程。

假如不做任何的处理，那么在进程A切换到进程B的时候，TLB和Cache中同时存在了A和B进程的数据。对于kernel space其实无所谓，因为所有的进程都是共享的，但是对于A和B进程，它们各种有自己的独立的用户地址空间，也就是说，同样的一个虚拟地址X，在A的地址空间中可以被翻译成Pa，而在B地址空间中会被翻译成Pb，如果在地址翻译过程中，TLB中同时存在A和B进程的数据，那么旧的A地址空间的缓存项会影响B进程地址空间的翻译，因此，在进程切换的时候，需要有tlb的操作，以便清除旧进程的影响，具体怎样做呢？我们下面一一讨论。

2、绝对没有问题，但是性能不佳的方案

当系统发生进程切换，从进程A切换到进程B，从而导致地址空间也从A切换到B，这时候，我们可以认为在A进程执行过程中，所有TLB和Cache的数据都是for A进程的，一旦切换到B，整个地址空间都不一样了，因此需要全部flush掉（注意：我这里使用了linux内核的术语，flush就是意味着将TLB或者cache中的条目设置为无效，对于一个ARM平台上的嵌入式工程师，一般我们会更习惯使用invalidate这个术语，不管怎样，在本文中，flush等于invalidate）。

这种方案当然没有问题，当进程B被切入执行的时候，其面对的CPU是一个干干净净，从头开始的硬件环境，TLB和Cache中不会有任何的残留的A进程的数据来影响当前B进程的执行。当然，稍微有一点遗憾的就是在B进程开始执行的时候，TLB和Cache都是冰冷的（空空如也），因此，B进程刚开始执行的时候，TLB miss和Cache miss都非常严重，从而导致了性能的下降。

3、如何提高TLB的性能？

对一个模块的优化往往需要对该模块的特性进行更细致的分析、归类，上一节，我们采用进程地址空间这样的术语，其实它可以被进一步细分为内核地址空间和用户地址空间。对于所有的进程（包括内核线程），内核地址空间是一样的，因此对于这部分地址翻译，无论进程如何切换，内核地址空间转换到物理地址的关系是永远不变的，其实在进程A切换到B的时候，不需要flush掉，因为B进程也可以继续使用这部分的TLB内容（上图中，橘色的block）。对于用户地址空间，各个进程都有自己独立的地址空间，在进程A切换到B的时候，TLB中的和A进程相关的entry（上图中，青色的block）对于B是完全没有任何意义的，需要flush掉。

在这样的思路指导下，我们其实需要区分global和local（其实就是process-specific的意思）这两种类型的地址翻译，因此，在页表描述符中往往有一个bit来标识该地址翻译是global还是local的，同样的，在TLB中，这个标识global还是local的flag也会被缓存起来。有了这样的设计之后，我们可以根据不同的场景而flush all或者只是flush local tlb entry。

4、特殊情况的考量

我们考虑下面的场景：进程A切换到内核线程K之后，其实地址空间根本没有必要切换，线程K能访问的就是内核空间的那些地址，而这些地址也是和进程A共享的。既然没有切换地址空间，那么也就不需要flush 那些进程特定的tlb entry了，当从K切换会A进程后，那么所有TLB的数据都是有效的，从大大降低了tlb miss。此外，对于多线程环境，切换可能发生在一个进程中的两个线程，这时候，线程在同样的地址空间，也根本不需要flush tlb。

4、进一步提升TLB的性能 

当然可以，不过这需要我们在设计TLB block的时候需要识别process specific的tlb entry，也就是说，TLB block需要感知到各个进程的地址空间。为了完成这样的设计，我们需要标识不同的address space，这里有一个术语叫做ASID（address space ID）。原来TLB查找是通过虚拟地址VA来判断是否TLB hit。有了ASID的支持后，TLB hit的判断标准修改为（虚拟地址＋ASID），ASID是每一个进程分配一个，标识自己的进程地址空间。TLB block如何知道一个tlb entry的ASID呢？一般会来自CPU的系统寄存器（对于ARM64平台，它来自TTBRx_EL1寄存器），这样在TLB block在缓存（VA-PA-Global flag）的同时，也就把当前的ASID缓存在了对应的TLB entry中，这样一个TLB entry中包括了（VA-PA-Global flag-ASID）。

有了ASID的支持后，A进程切换到B进程再也不需要flush tlb了，因为A进程执行时候缓存在TLB中的残留A地址空间相关的entry不会影响到B进程，虽然A和B可能有相同的VA，但是ASID保证了硬件可以区分A和B进程地址空间