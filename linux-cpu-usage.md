# linux cpu使用率简单分析

top命令中cpu使用率有两种，一种是cpu的使用率，一种是每个进程的cpu使用率。

## cpu使用率的计算方式

Linux系统中计算CPU利用率是通过读取/proc/stat文件数据而计算得来。

/proc/stat和cpu使用率相关数据解析

```
cat /proc/stat 
cpu  75014 7 95063 764546989 50985 0 1866 0 0 0
cpu0 11144 1 11672 95504621 50048 0 425 0 0 0
cpu1 8974 0 12069 95576989 308 0 240 0 0 0
cpu2 9332 0 11639 95579070 153 0 174 0 0 0
cpu3 8904 0 11521 95578492 131 0 185 0 0 0
cpu4 9674 0 12146 95576049 149 0 156 0 0 0
cpu5 9003 0 11432 95577006 80 0 232 0 0 0
cpu6 8911 2 12101 95579315 63 0 160 0 0 0
cpu7 9070 0 12481 95575444 51 0 292 0 0 0
```

第一列为cpu变化，第二列到往后依次是：

- user
  Time spent in user mode.
  
- nice
  Time spent in user mode with low priority (nice).
  
- system

  Time spent in system mode.

- idle

  Time spent in the idle task.  This value should be USER_HZ times the second entry in the /proc/uptime pseudo-file.

- iowait

  Time waiting for I/O to complete.

- irq

  Time servicing interrupts.

- softirq

  Time servicing softirqs.

- steal

  Stolen time, which is the time spent in other operating systems when running in a virtualized environment

- guest

  Time spent running a virtual CPU for guest operating systems under the control of the Linux kernel.

- guest_nice

  Time spent running a niced guest (virtual CPU for guest operating systems under the control of the Linux kernel).

guest和user是一样的，guest_nice和nice是一样的。

cpu使用率计算方式：

```
CPU时间=user+system+nice+idle+iowait+irq+softirq+stl
```
各种使用率的计算方式
```
%us=(User time + Nice time)/CPU时间*100%

%sy=(System time + Hard Irq time +SoftIRQ time)/CPU时间*100%

%id=(Idle time)/CPU时间*100%

%ni=(Nice time)/CPU时间*100%

%wa=(Waiting time)/CPU时间*100%

%hi=(Hard Irq time)/CPU时间*100%

%si=(SoftIRQ time)/CPU时间*100%

%st=(Steal time)/CPU时间*100%
```

参考：[Linux CPU利用率计算原理及内核实现 – Linux Kernel Exploration (ilinuxkernel.com)](https://ilinuxkernel.com/?p=333)

## 进程cpu使用率计算

进程cpu使用率计算方式和cpu类似，不过总的时间，应该是采样时间。进程cpu使用率计算只有user和sys，guest，top中只有整体，pidstat有sys，user，guest

进程读取的文件是/proc/[pid]/stat

```
cat /proc/1020/stat
1020 (dockerd) S 1 1020 1020 0 -1 4202752 62256 31734 301 67 22488 5776 1 0 20 0 18 0 8439 1002749952 15331 18446744073709551615 139834514894848 139834573050428 140736735806192 140736735805552 139834542789011 0 1006249984 0 2143420159 18446744073709551615 0 0 17 5 0 0 272 0 0 139834575148744 139834607394016 139834632134656 140736735809315 140736735809386 140736735809386 140736735809511 0
```

时间相关的解析，(14)表示是proc/[pid]/stat的第14项数据

- utime %lu 
   (14)  Amount  of  time  that  this  process  has  been  scheduled  in  user  mode,  measured  in  clock  ticks .  This includes guest time, guest_time (time spent running a  virtual  CPU,  see  below),  so  that applications that are not aware of the guest time field do not lose that time from their calculations.
- stime %lu 
  (15)  Amount  of  time  that  this  process  has  been  scheduled  in  kernel  mode,  measured  in clock ticks.
- cutime %ld
  (16) Amount of time that this process's waited-for children have been scheduled in user mode,  measured  in  clock  ticks .   This includes guest time, cguest_time (time spent running a vir‐tual CPU, see below).
- cstime %ld 
  (17) Amount of time that this process's waited-for children have been scheduled in kernel mode, measured in  clock  ticks.
- guest_time %lu
  (43) Guest time of the process (time spent running a virtual CPU for a guest operating system), measured in  clock  ticks.
- cguest_time %ld
  (44) Guest time of the process's children, measured in clock ticks.

cutime和sctime具体含义还不太明白。从上面的解析看到utime 已经包含了guest_time 。

## 时间更新代码走读

### cpu时间显示

#### proc_stat_init

```c
/* cpu的运行时间 */
static const struct file_operations proc_stat_operations = {
	.open		= stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_stat_init(void)
{
	proc_create("stat", 0, NULL, &proc_stat_operations);
	return 0;
}
module_init(proc_stat_init);
```

#### show_stat

```c
static int stat_open(struct inode *inode, struct file *file)
{
	size_t size = 1024 + 128 * num_online_cpus();

	/* minimum size to display an interrupt count : 2 bytes */
	size += 2 * nr_irqs;
	return single_open_size(file, show_stat, NULL, size);
}

#define kcpustat_cpu(cpu) per_cpu(kernel_cpustat, cpu)

static int show_stat(struct seq_file *p, void *v)
{
	for_each_online_cpu(i) {
		/* Copy values here to work around gcc-2.95.3, gcc-2.96 */
		user = kcpustat_cpu(i).cpustat[CPUTIME_USER];
		nice = kcpustat_cpu(i).cpustat[CPUTIME_NICE];
		system = kcpustat_cpu(i).cpustat[CPUTIME_SYSTEM];
		idle = get_idle_time(i);
		iowait = get_iowait_time(i);
		irq = kcpustat_cpu(i).cpustat[CPUTIME_IRQ];
		softirq = kcpustat_cpu(i).cpustat[CPUTIME_SOFTIRQ];
		steal = kcpustat_cpu(i).cpustat[CPUTIME_STEAL];
		guest = kcpustat_cpu(i).cpustat[CPUTIME_GUEST];
		guest_nice = kcpustat_cpu(i).cpustat[CPUTIME_GUEST_NICE];
		seq_printf(p, "cpu%d", i);
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(user));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(nice));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(system));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(idle));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(iowait));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(irq));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(softirq));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(steal));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(guest));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(guest_nice));
		seq_putc(p, '\n');
	}
}
```

### 进程时间显示

#### proc_tid_stat

```c
static const struct pid_entry tid_base_stuff[] = {
	ONE("stat", 	 S_IRUGO, proc_tid_stat), /* 为proc的show函数 */
};
```

#### do_task_stat

```c
int proc_tid_stat(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task)
{
	return do_task_stat(m, ns, pid, task, 0);
}

static int do_task_stat(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task, int whole)
{
	state = *get_task_state(task);
	vsize = eip = esp = 0;
	permitted = ptrace_may_access(task, PTRACE_MODE_READ | PTRACE_MODE_NOAUDIT);
	mm = get_task_mm(task);
	if (mm) {
		vsize = task_vsize(mm);
		if (permitted) {
			eip = KSTK_EIP(task);
			esp = KSTK_ESP(task);
		}
	}

	get_task_comm(tcomm, task);

	sigemptyset(&sigign);
	sigemptyset(&sigcatch);
	cutime = cstime = utime = stime = 0;
	cgtime = gtime = 0;

	if (lock_task_sighand(task, &flags)) {
		/* add up live thread stats at the group level
         * 如果是线程组，whole为1
         */
		if (whole) {
			thread_group_cputime_adjusted(task, &utime, &stime);
			gtime += sig->gtime;
		}

		sid = task_session_nr_ns(task, ns);
		ppid = task_tgid_nr_ns(task->real_parent, ns);
		pgid = task_pgrp_nr_ns(task, ns);

		unlock_task_sighand(task, &flags);
	}

	/* 以单独的进程为例子 */
	if (!whole) {
		task_cputime_adjusted(task, &utime, &stime);
		gtime = task_gtime(task);
	}

	seq_put_decimal_ull(m, ' ', cputime_to_clock_t(utime));
	seq_put_decimal_ull(m, ' ', cputime_to_clock_t(stime));
	seq_put_decimal_ll(m, ' ', cputime_to_clock_t(cutime));
	seq_put_decimal_ll(m, ' ', cputime_to_clock_t(cstime));
	seq_put_decimal_ull(m, ' ', cputime_to_clock_t(gtime));
	seq_put_decimal_ll(m, ' ', cputime_to_clock_t(cgtime));
	return 0;
}
```

#### task_cputime_adjusted

```c
void task_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime = {
		.sum_exec_runtime = p->se.sum_exec_runtime,
	};

	task_cputime(p, &cputime.utime, &cputime.stime);
    /* 对time进行调整，有点复杂，暂时不关心 */
	cputime_adjust(&cputime, &p->prev_cputime, ut, st);
}

static inline void task_cputime(struct task_struct *t, cputime_t *utime, cputime_t *stime)
{
	/* 直接将task的时间赋值 */
	if (utime)
		*utime = t->utime;
	if (stime)
		*stime = t->stime;
}
```

### 时间更新

时间更新主要在update_process_times函数中，调用栈如下：

```
local_apic_timer_interrupt-->hrtimer_interrupt-->__hrtimer_run_queues-->tick_sched_timer-->
tick_sched_handle-->update_process_times
```

#### update_process_times

```c
static void tick_sched_handle(struct tick_sched *ts, struct pt_regs *regs)
{
	/* user_mode判断user还是sys模式 */
	update_process_times(user_mode(regs));
	profile_tick(CPU_PROFILING);
}
/*
 * Called from the timer interrupt handler to charge one tick to the current
 * process.  user_tick is 1 if the tick is user time, 0 for system.
 */
void update_process_times(int user_tick)
{
	struct task_struct *p = current;
	int cpu = smp_processor_id();

	/* Note: this timer irq context must be accounted for as well. */
	account_process_tick(p, user_tick);
}
```

#### account_process_tick

```c
#define cputime_one_jiffy		jiffies_to_cputime(1)

void account_process_tick(struct task_struct *p, int user_tick)
{
	cputime_t one_jiffy_scaled = cputime_to_scaled(cputime_one_jiffy);
	struct rq *rq = this_rq();

	if (vtime_accounting_enabled())
		return;

	/* sched_clock_irqtime为0 */
	if (sched_clock_irqtime) {
		irqtime_account_process_tick(p, user_tick, rq);
		return;
	}

	if (steal_account_process_tick())
		return;
	/* 因为每个tick都会执行，所以增加的时间为cputime_one_jiffy */
	if (user_tick)
		account_user_time(p, cputime_one_jiffy, one_jiffy_scaled);
	else if ((p != rq->idle) || (irq_count() != HARDIRQ_OFFSET))
		account_system_time(p, HARDIRQ_OFFSET, cputime_one_jiffy,
				    one_jiffy_scaled);
	else
		account_idle_time(cputime_one_jiffy);
}

```

#### account_user_time

```c
/*
 * Account user cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in user space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
void account_user_time(struct task_struct *p, cputime_t cputime, cputime_t cputime_scaled)
{
	int index;

	/* Add user time to process.更新用户空间时间 */
	p->utime += cputime;
	p->utimescaled += cputime_scaled;
	account_group_user_time(p, cputime);
	/* 进程的nice是不是大于0，大于0，index为CPUTIME_NICE */
	index = (TASK_NICE(p) > 0) ? CPUTIME_NICE : CPUTIME_USER;

	/* Add user time to cpustat.更新cpu的时间 */
	task_group_account_field(p, index, (__force u64) cputime);

	/* Account for user time used，和acct相关 */
	acct_account_cputime(p);
}

static inline void task_group_account_field(struct task_struct *p, int index, u64 tmp)
{
	/*
	 * Since all updates are sure to touch the root cgroup, we
	 * get ourselves ahead and touch it first. If the root cgroup
	 * is the only cgroup, then nothing else should be necessary.
	 *
	 */
    /* 将时间增加到该cpu的kernel_cpustat的cpustat[index]中 */
	__get_cpu_var(kernel_cpustat).cpustat[index] += tmp;
	/* 和cgrup相关，暂不考虑 */
	cpuacct_account_field(p, index, tmp);
}
```

#### account_system_time

```c
/*
 * Account system cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @hardirq_offset: the offset to subtract from hardirq_count()
 * @cputime: the cpu time spent in kernel space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
void account_system_time(struct task_struct *p, int hardirq_offset,
			 cputime_t cputime, cputime_t cputime_scaled)
{
	int index;

	/* 如果该进程是跑vcpu的进程 */
	if ((p->flags & PF_VCPU) && (irq_count() - hardirq_offset == 0)) {
		account_guest_time(p, cputime, cputime_scaled);
		return;
	}

	if (hardirq_count() - hardirq_offset)
		index = CPUTIME_IRQ;
	else if (in_serving_softirq())
		index = CPUTIME_SOFTIRQ;
	else
		index = CPUTIME_SYSTEM;

	__account_system_time(p, cputime, cputime_scaled, index);
}

/*
 * Account system cpu time to a process and desired cpustat field
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in kernel space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 * @target_cputime64: pointer to cpustat field that has to be updated
 */
static inline
void __account_system_time(struct task_struct *p, cputime_t cputime,
			cputime_t cputime_scaled, int index)
{
	/* Add system time to process. */
	p->stime += cputime;
	p->stimescaled += cputime_scaled;
	account_group_system_time(p, cputime);

	/* Add system time to cpustat.更新cpu的对应的CPUTIME_IRQ或CPUTIME_SOFTIRQ或CPUTIME_SYSTEM时间 */
	task_group_account_field(p, index, (__force u64) cputime);

	/* Account for system time used */
	acct_account_cputime(p);
}
```

##### account_guest_time

```c
/*
 * Account guest cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in virtual machine since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
static void account_guest_time(struct task_struct *p, cputime_t cputime,
			       cputime_t cputime_scaled)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	/* Add guest time to process. */
	/* 将guest time添加到utime(user mode时间) */
	p->utime += cputime;
	p->utimescaled += cputime_scaled;
	account_group_user_time(p, cputime);
	p->gtime += cputime;

	/* Add guest time to cpustat.更新cpu的对应时间 */
	if (TASK_NICE(p) > 0) {
		cpustat[CPUTIME_NICE] += (__force u64) cputime;
		cpustat[CPUTIME_GUEST_NICE] += (__force u64) cputime;
	} else {
		cpustat[CPUTIME_USER] += (__force u64) cputime;
		cpustat[CPUTIME_GUEST] += (__force u64) cputime;
	}
}
```

#### account_idle_time

```c
/*
 * Account for idle time.
 * @cputime: the cpu time spent in idle wait
 */
void account_idle_time(cputime_t cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;
	struct rq *rq = this_rq();
	/* io_schedule_timeout中会将rq->nr_iowait加1，表达等待io */
	if (atomic_read(&rq->nr_iowait) > 0)
		cpustat[CPUTIME_IOWAIT] += (__force u64) cputime;
	else
		cpustat[CPUTIME_IDLE] += (__force u64) cputime;
}
```



从上面的简单分析可以得到：

1. 如果进程跑vpcu，那么在进程进入vpcu状态的时候，内核是吧时间计算在user模式的。
2. 对应进程cpu使用率，当进程运行的时候，进程的的user或者sys时间会增加，如果进程睡眠，那么这两个时间都不会增加。如果进程因为等待io睡眠，那么对应cpu的iowait时间会增加。所以如果进程的cpu使用率高，那么可以确定肯定是在运行态时间比较长
