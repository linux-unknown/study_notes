# linux lockup 分析

kernel version 3.10

kernel_init_freeable-->lockup_detector_init

## lockup_detector_init

```c
int __read_mostly watchdog_thresh = 10;
/*
 * Hard-lockup warnings should be triggered after just a few seconds. Soft-lockups can have false positives
 * under extreme conditions. So we generally want a higher threshold for soft lockups than for hard lockups.
 * So we couple the thresholds with a factor: we make the soft threshold twice the amount of time the hard
 * threshold is.
 * 软件阈值是硬件阈值的两倍
 */
static int get_softlockup_thresh(void) { return watchdog_thresh * 2; }

static void set_sample_period(void)
{
	/*
	 * convert watchdog_thresh from seconds to ns the divide by 5 is to give hrtimer several chances (two
	 * or three with the current relation between the soft and hard thresholds) to increment before the
	 * hardlockup detector generates a warning
	 */
	sample_period = get_softlockup_thresh() * ((u64)NSEC_PER_SEC / 5);
}

void __init lockup_detector_init(void)
{
	set_sample_period();
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_enabled()) {
		if (!cpumask_empty(tick_nohz_full_mask))
			pr_info("Disabling watchdog on nohz_full cores by default\n");
		cpumask_andnot(&watchdog_cpumask, cpu_possible_mask, tick_nohz_full_mask);
	} else
		cpumask_copy(&watchdog_cpumask, cpu_possible_mask);
#else
	cpumask_copy(&watchdog_cpumask, cpu_possible_mask);
#endif

	if (watchdog_enabled)
		watchdog_enable_all_cpus();
}
```

### watchdog_enable_all_cpus

```c
static int watchdog_enable_all_cpus(void)
{
	int err = 0;
	if (!watchdog_running) {
        /* 注册watchdog_threads，会为每一个cpu 创建一个线程，该线程会执行watchdog_threads中的一系列函数 */
		err = smpboot_register_percpu_thread(&watchdog_threads);
		watchdog_running = 1;
	} 
	return err;
}
```

#### smpboot_register_percpu_thread

```c
int smpboot_register_percpu_thread(struct smp_hotplug_thread *plug_thread)
{
	unsigned int cpu;
	int ret = 0;
	cpumask_copy(plug_thread->cpumask, cpu_possible_mask);
	get_online_cpus();
	mutex_lock(&smpboot_threads_lock);
	for_each_online_cpu(cpu) {
		ret = __smpboot_create_thread(plug_thread, cpu);
        /* 会调用pre_unpark，watchdog_threads中没有实现 */
		smpboot_unpark_thread(plug_thread, cpu);
	}
	list_add(&plug_thread->list, &hotplug_threads);
out:
	mutex_unlock(&smpboot_threads_lock);
	put_online_cpus();
	return ret;
}
```

#### __smpboot_create_thread

```c
static int __smpboot_create_thread(struct smp_hotplug_thread *ht, unsigned int cpu)
{
	struct task_struct *tsk = *per_cpu_ptr(ht->store, cpu);
	struct smpboot_thread_data *td;
	/* 分配td，这个数据结构会传递个smpboot_thread_fn线程 */
	td = kzalloc_node(sizeof(*td), GFP_KERNEL, cpu_to_node(cpu));
	td->cpu = cpu;
	td->ht = ht;
	/* 创建线程smpboot_thread_fn */
	tsk = kthread_create_on_cpu(smpboot_thread_fn, td, cpu, ht->thread_comm);
	get_task_struct(tsk);
	/* 将线程的tsk写入ht->store即softlockup_watchdog中 */
	*per_cpu_ptr(ht->store, cpu) = tsk;
	/* 没有实现create */
	if (ht->create) {}
	return 0;
}
```
#### smpboot_thread_fn
```c
static int smpboot_thread_fn(void *data)
{
	struct smpboot_thread_data *td = data;
	struct smp_hotplug_thread *ht = td->ht;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		preempt_disable();
		if (kthread_should_stop()) {
			__set_current_state(TASK_RUNNING);
			preempt_enable();
			if (ht->cleanup)
				ht->cleanup(td->cpu, cpu_online(td->cpu));
			kfree(td);
			return 0;
		}

		if (kthread_should_park()) {
			__set_current_state(TASK_RUNNING);
			preempt_enable();
			if (ht->park && td->status == HP_THREAD_ACTIVE) {
				BUG_ON(td->cpu != smp_processor_id());
				ht->park(td->cpu);
				td->status = HP_THREAD_PARKED;
			}
			kthread_parkme();
			/* We might have been woken for stop */
			continue;
		}

		BUG_ON(td->cpu != smp_processor_id());

		/* Check for state change setup */
        /* 初始 status为0 即 HP_THREAD_NONE*/
		switch (td->status) {
		case HP_THREAD_NONE:
			__set_current_state(TASK_RUNNING);
			preempt_enable();
			if (ht->setup)
				ht->setup(td->cpu); /* 调用watchdog_enable */
			td->status = HP_THREAD_ACTIVE;
			continue;

		case HP_THREAD_PARKED:
			__set_current_state(TASK_RUNNING);
			preempt_enable();
			if (ht->unpark)
				ht->unpark(td->cpu);
			td->status = HP_THREAD_ACTIVE;
			continue;
		}
		/* 调用watchdog_should_run，watchdog_should_run返回1，才会执行watchdog，
		 * 则要hrtimer_interrupts不等于soft_lockup_hrtimer_cnt，当执行了watchdog之后
		 * hrtimer_interrupts等于soft_lockup_hrtimer_cnt，该进程进入睡眠
		 */
		if (!ht->thread_should_run(td->cpu)) {
			preempt_enable_no_resched();
            /* 前面设置为TASK_INTERRUPTIBLE，此时调用schedule睡眠 */
			schedule();
            /* 唤醒之后继续执行 */
		} else {
			__set_current_state(TASK_RUNNING);
			preempt_enable();
            /* 调用watchdog，watchdog中会将 hrtimer_interrupts赋值给soft_lockup_hrtimer_cnt 
             * 从而hrtimer_interrupts等于soft_lockup_hrtimer_cnt，再次循环thread_should_run返回0
             * 该进程进入睡眠状态。
             */
			ht->thread_fn(td->cpu);
		}
	}
}
```

### watchdog_threads

```c++
static struct smp_hotplug_thread watchdog_threads = {
	.store			= &softlockup_watchdog,
	.thread_should_run	= watchdog_should_run,
	.thread_fn		= watchdog,
	.thread_comm		= "watchdog/%u",
	.setup			= watchdog_enable,
	.cleanup		= watchdog_cleanup,
    /* park和unpark是cpu hotplug调用的 */
	.park			= watchdog_disable, 
	.unpark			= watchdog_enable,
};
```

#### watchdog_enable

```c
static DEFINE_PER_CPU(struct hrtimer, watchdog_hrtimer);

static void watchdog_enable(unsigned int cpu)
{
	struct hrtimer *hrtimer = &__raw_get_cpu_var(watchdog_hrtimer);
	/* kick off the timer for the hardlockup detector, 初始化定时器 */
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = watchdog_timer_fn;
	/* Enable the perf event */
	watchdog_nmi_enable(cpu);
	/* done here because hrtimer_start can only pin to smp_processor_id()，
	 * 启动定时器,超时值为 sample_period，即4秒钟
	 */
	hrtimer_start(hrtimer, ns_to_ktime(sample_period), HRTIMER_MODE_REL_PINNED);
	/* initialize timestamp，设置该线程的调度策略和优先级 */
	watchdog_set_prio(SCHED_FIFO, MAX_RT_PRIO - 1);
	__touch_watchdog();
}
```

#### __touch_watchdog

```c
/* Commands for resetting the watchdog */
static void __touch_watchdog(void)
{
    /* 将当前时间写入 watchdog_touch_ts中 */
	__this_cpu_write(watchdog_touch_ts, get_timestamp());
}
```

#### watchdog_should_run

```c
static int watchdog_should_run(unsigned int cpu)
{
	return __this_cpu_read(hrtimer_interrupts) != __this_cpu_read(soft_lockup_hrtimer_cnt);
}
```

#### watchdog

```c
/*
 * The watchdog thread function - touches the timestamp.
 *
 * It only runs once every sample_period seconds (4 seconds by
 * default) to reset the softlockup timestamp. If this gets delayed
 * for more than 2*watchdog_thresh seconds then the debug-printout
 * triggers in watchdog_timer_fn().
 */
static void watchdog(unsigned int cpu)
{
    /* 将hrtimer_interrupts写入soft_lockup_hrtimer_cnt */
	__this_cpu_write(soft_lockup_hrtimer_cnt, __this_cpu_read(hrtimer_interrupts));
	__touch_watchdog(); /* watchdog_touch_ts写入当前时间 */
	/*
	 * watchdog_nmi_enable() clears the NMI_WATCHDOG_ENABLED bit in the
	 * failure path. Check for failures that can occur asynchronously -
	 * for example, when CPUs are on-lined - and shut down the hardware
	 * perf event on each CPU accordingly.
	 *
	 * The only non-obvious place this bit can be cleared is through
	 * watchdog_nmi_enable(), so a pr_info() is placed there.  Placing a
	 * pr_info here would be too noisy as it would result in a message
	 * every few seconds if the hardlockup was disabled but the softlockup
	 * enabled.
	 */
	if (!(watchdog_enabled & NMI_WATCHDOG_ENABLED))
		watchdog_nmi_disable(cpu);
}
```

### watchdog_timer_fn

```c
/* watchdog kicker functions */
static enum hrtimer_restart watchdog_timer_fn(struct hrtimer *hrtimer)
{
	unsigned long touch_ts = __this_cpu_read(watchdog_touch_ts);
	struct pt_regs *regs = get_irq_regs();
	int duration;
	int softlockup_all_cpu_backtrace = sysctl_softlockup_all_cpu_backtrace;

	if (atomic_read(&watchdog_park_in_progress) != 0)
		return HRTIMER_NORESTART;
	/* kick the hardlockup detector
	/ *hrtimer_interrupts加1 */  
	watchdog_interrupt_count();
	/* kick the softlockup detector */
    /* 唤醒smpboot_thread_fn，定时器超时函数是在中断上下文中运行的，该函数执行结束，被唤醒的进程才会执行 */
	wake_up_process(__this_cpu_read(softlockup_watchdog));

	/* .. and repeat，重新设置timer */
	hrtimer_forward_now(hrtimer, ns_to_ktime(sample_period));
	/* 什么时候会为0 */
	if (touch_ts == 0) {
		if (unlikely(__this_cpu_read(softlockup_touch_sync))) {
			/*
			 * If the time stamp was touched atomically make sure the scheduler tick is up to date.
			 */
			__this_cpu_write(softlockup_touch_sync, false);
			sched_clock_tick();
		}

		/* Clear the guest paused flag on watchdog reset */
		kvm_check_and_clear_guest_paused();
		__touch_watchdog();
		return HRTIMER_RESTART;
	}

	/* check for a softlockup
	 * This is done by making sure a high priority task is being scheduled.  The task touches the watchdog
     * to indicate it is getting cpu time. If it hasn't then this is a good indication some task is hogging
     * the cpu
	 */
	duration = is_softlockup(touch_ts);
	if (unlikely(duration)) {
		/*
		 * If a virtual machine is stopped by the host it can look to the watchdog like a soft lockup, check
         * to see if the host stopped the vm before we issue the warning，目前还不清楚是干啥的
		 */
		if (kvm_check_and_clear_guest_paused())
			return HRTIMER_RESTART;

		/* only warn once，如果已经warn了就 */
		if (__this_cpu_read(soft_watchdog_warn) == true) {
			/*
			 * When multiple processes are causing softlockups the softlockup detector only warns on the
             * first one because the code relies on a full quiet cycle to re-arm.  The second process
             * prevents the quiet cycle and never gets reported.  Use task pointers to detect this.
             * 如果另外一个进程发hang，仅warn第一个
			 */
			if (__this_cpu_read(softlockup_task_ptr_saved) != current) {
				__this_cpu_write(soft_watchdog_warn, false);
				__touch_watchdog();
			}
			return HRTIMER_RESTART;
		}

		if (softlockup_all_cpu_backtrace) {
			/* Prevent multiple soft-lockup reports if one cpu is already engaged in dumping cpu back traces
			 */
			if (test_and_set_bit(0, &soft_lockup_nmi_warn)) {
				/* Someone else will report us. Let's give up */
				__this_cpu_write(soft_watchdog_warn, true);
				return HRTIMER_RESTART;
			}
		}
		/* 打印出当前cpu中 hang的进程 */
		pr_emerg("BUG: soft lockup - CPU#%d stuck for %us! [%s:%d]\n", smp_processor_id(), duration,
			current->comm, task_pid_nr(current));
		__this_cpu_write(softlockup_task_ptr_saved, current);
		print_modules();
		print_irqtrace_events(current);
		if (regs)
			show_regs(regs);
		else
			dump_stack();

		if (softlockup_all_cpu_backtrace) {
			/* Avoid generating two back traces for current given that one is already made above */
			trigger_allbutself_cpu_backtrace();

			clear_bit(0, &soft_lockup_nmi_warn);
			/* Barrier to sync with other cpus */
			smp_mb__after_atomic();
		}

		add_taint(TAINT_SOFTLOCKUP, LOCKDEP_STILL_OK);
		if (softlockup_panic)
			panic("softlockup: hung tasks");
		__this_cpu_write(soft_watchdog_warn, true);
	} else
		__this_cpu_write(soft_watchdog_warn, false);

	return HRTIMER_RESTART;
}
```

#### is_softlockup

```c
static int is_softlockup(unsigned long touch_ts)
{
	unsigned long now = get_timestamp();
	if ((watchdog_enabled & SOFT_WATCHDOG_ENABLED) && watchdog_thresh){
		/* Warn about unreasonable delays. */
        /* 定时器中的时间总是超过__touch_watchdog时间的。定时器更新了时间之后，watchdog_touch_ts才能更新
         * 在该函数中touch_ts的值为上一次定时器产生或者上上一次，或许更久之前的定时器唤醒smpboot_thread_fn
         * 更新的值，所以正常情况下，now的值是要超前touch_ts的
         * 假设都从0时刻开始
         * hrtimer            0  4(中断)                           8(中断)
         *                       唤醒进程(进程还没有执行)             唤醒进程(进程还没有执行)
         *                       读取watchdog_touch_ts，为0         读取watchdog_touch_ts，为0
         *                       超时函数退出，watchdog执行，更新时间  超时函数退出，watchdog执行，更新时间
         * watchdog_touch_ts  0	 4(忽略微小差异)					   8
         * 如果smpboot_thread_fn不能得到执行，那么watchdog就不能更新时间，定时器中的时间就会超过watchdog_touch_ts更多
         * 直到超过get_softlockup_thresh()这么多
         */
		if (time_after(now, touch_ts + get_softlockup_thresh()))
			return now - touch_ts;
	}
	return 0;
}
```

​    