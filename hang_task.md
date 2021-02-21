# hang_task

## hung_task_init
```c
static int __init hung_task_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);
	watchdog_task = kthread_run(watchdog, NULL, "khungtaskd");/* 创建一个内核线程 */
	return 0;
}
subsys_initcall(hung_task_init);
```

### watchdog

```c
static atomic_t reset_hung_task = ATOMIC_INIT(0);
static int watchdog(void *dummy)
{
	set_user_nice(current, 0);
	for ( ; ; ) {
		unsigned long timeout = sysctl_hung_task_timeout_secs;
		/* 睡眠 sysctl_hung_task_timeout_secs这么长时间 */
        while (schedule_timeout_interruptible(timeout_jiffies(timeout)))
			timeout = sysctl_hung_task_timeout_secs;
		/* reset_hung_task设置为0，返回原来的值 */
		if (atomic_xchg(&reset_hung_task, 0))
			continue;

		check_hung_uninterruptible_tasks(timeout);
	}
	return 0;
}
```

#### check_hung_uninterruptible_tasks

```c
unsigned long __read_mostly sysctl_hung_task_check_count = PID_MAX_LIMIT;
#define HUNG_TASK_BATCHING 1024

#define do_each_thread(g, t) \
	for (g = t = &init_task ; (g = t = next_task(g)) != &init_task ; ) do
/*
 * Check whether a TASK_UNINTERRUPTIBLE does not get woken up for
 * a really long time (120 seconds). If that happens, print out
 * a warning.
 */
static void check_hung_uninterruptible_tasks(unsigned long timeout)
{
	int max_count = sysctl_hung_task_check_count;
	int batch_count = HUNG_TASK_BATCHING;
	struct task_struct *g, *t;
	/*
	 * If the system crashed already then all bets are off, do not report extra hung tasks:
	 */
	if (test_taint(TAINT_DIE) || did_panic)
		return;

	rcu_read_lock();
    /* 从init_task开始遍历 */
	do_each_thread(g, t) {
		if (!max_count--)
			goto unlock;
		if (!--batch_count) {
			batch_count = HUNG_TASK_BATCHING;
            /* batch_count为0了可以重新调度 */
			if (!rcu_lock_break(g, t))
				goto unlock;
		}
		/* use "==" to skip the TASK_KILLABLE tasks waiting on NFS */
		if (t->state == TASK_UNINTERRUPTIBLE)
			check_hung_task(t, timeout);
	} while_each_thread(g, t);
 unlock:
	rcu_read_unlock();
}
```



```c
static void check_hung_task(struct task_struct *t, unsigned long timeout)
{
    /* nvcsw表示进程主动切换次数，nivcsw表示进程被动切换次数，两者之和就是进程总的切换次数 */
	unsigned long switch_count = t->nvcsw + t->nivcsw;
	/*
	 * Ensure the task is not frozen. Also, skip vfork and any other user process that freezer should skip.
	 */
	if (unlikely(t->flags & (PF_FROZEN | PF_FREEZER_SKIP)))
	    return;
	/*
	 * When a freshly created task is scheduled once, changes its state to TASK_UNINTERRUPTIBLE without
     * having ever been switched out once, it musn't be checked.刚创建的进程
	 */
	if (unlikely(!switch_count))
		return;

	if (switch_count != t->last_switch_count) {
		t->last_switch_count = switch_count;
		return;
	}
	/* switch_count 等于 t->last_switch_count表示 sysctl_hung_task_timeout_secs时间没有调度过*/
	trace_sched_process_hang(t);

	if (!sysctl_hung_task_warnings && !sysctl_hung_task_panic)
		return;

	/*
	 * Ok, the task did not get scheduled for more than 2 minutes,
	 * complain:
	 */
	if (sysctl_hung_task_warnings) {
		if (sysctl_hung_task_warnings > 0)
			sysctl_hung_task_warnings--;
		printk(KERN_ERR "INFO: task %s:%d blocked for more than "
				"%ld seconds.\n", t->comm, t->pid, timeout);
		printk(KERN_ERR "\"echo 0 > /proc/sys/kernel/hung_task_timeout_secs\""
				" disables this message.\n");
		sched_show_task(t);
		debug_show_held_locks(t);
	}
	/* 设置soft和hard lockup */
	touch_nmi_watchdog();
	/* 如果sysctl_hung_task_panic为0，则只会有警告 */
	if (sysctl_hung_task_panic) {
		trigger_all_cpu_backtrace();
		panic("hung_task: blocked tasks");
	}
}
```

