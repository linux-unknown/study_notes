# CFS scheduler

[TOC]

## weight

在cfs中，进程的优先级是同归weight来体现的。

```c
/*
 * Nice levels are multiplicative, with a gentle 10% change for every
 * nice level changed. I.e. when a CPU-bound task goes from nice 0 to
 * nice 1, it will get ~10% less CPU time than another CPU-bound task
 * that remained on nice 0.
 *
 * The "10% effect" is relative and cumulative: from _any_ nice level,
 * if you go up 1 level, it's -10% CPU usage, if you go down 1 level
 * it's +10% CPU usage. (to achieve that we use a multiplier of 1.25.
 * If a task goes up by ~10% and another task goes down by ~10% then
 * the relative distance between them is ~25%.)
 */
static const int prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};
```

每一个nice的优先级都对应一个weight值，优先级越高，weight越大。

### set_load_weight

```c
static void set_load_weight(struct task_struct *p)
{
    /*p->static_prio - MAX_RT_PRIO获得user priority，在100到139之间*/
	int prio = p->static_prio - MAX_RT_PRIO;
	struct load_weight *load = &p->se.load;

	/*
	 * SCHED_IDLE tasks get minimal weight:
	 */
	if (p->policy == SCHED_IDLE) {
		load->weight = scale_load(WEIGHT_IDLEPRIO);
		load->inv_weight = WMULT_IDLEPRIO;
		return;
	}

	load->weight = scale_load(prio_to_weight[prio]);
	load->inv_weight = prio_to_wmult[prio];
}
```

scale_load：是一个宏，现在什么都没有做。

## virtual time

```c
static inline u64 calc_delta_fair(u64 delta, struct sched_entity *se)
{
	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &se->load);

	return delta;
}
```

**delta**:实际运行时间的delta值，单位是ns

**se**：调度实体，嵌入到struct task_struct结构中。

如果se->load.weight 等于 NICE_0_LOAD则直接返回delta，即如果task的nice值为0，即weight为NICE_0_LOAD，则虚拟运行时间等于实际运行时间。

__calc_delta函数的作用主要是实现delta_exec * weight / lw.weight。所以进程的虚拟运行时间为

**delta = delta_exec * NICE_0_LOAD / lw.weight**，所以进程的**优先级越高，虚拟时间增加的越少**。

## ideal_runtime

ideal_runtime 表示task一次运行的最长时间

```c
ideal_runtime = sched_slice(cfs_rq, curr);
```

### sched_slice

```c
/*
 * We calculate the wall-time slice from the period by taking a part
 * proportional to the weight.
 *
 * s = p*P[w/rw]
 */
static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	u64 slice = __sched_period(cfs_rq->nr_running + !se->on_rq);

	for_each_sched_entity(se) {
		struct load_weight *load;
		struct load_weight lw;

		cfs_rq = cfs_rq_of(se);
		load = &cfs_rq->load;
		/*如果该se不再run queue中*/
		if (unlikely(!se->on_rq)) {
			lw = cfs_rq->load;
			/*将该se的weight增加到run queue中*/
			update_load_add(&lw, se->load.weight);
			load = &lw;
		}
		slice = __calc_delta(slice, se->load.weight, load);
	}
	return slice;
}
```

__sched_period的参数为run queue上task的个数。

cfs_rq->nr_running + !se->on_rq，如果当前se在run queue则on_rq为1，不再run queue则on_rq为0，因为调用

sched_slice函数的时候，task不一定在运行。

**slice = slice \* se->load.weight / cfs_rq->load**

**slice为该se的weight占run queue load的比例，然后乘以总的slice时间**

### __sched_period

```c
/*
 * Targeted preemption latency for CPU-bound tasks:
 * (default: 6ms * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * NOTE: this latency value is not the same as the concept of
 * 'timeslice length' - timeslices in CFS are of variable length
 * and have no persistent notion like in traditional, time-slice
 * based scheduling concepts.
 *
 * (to see the precise effective timeslice length of your workload,
 *  run vmstat and monitor the context-switches (cs) field)
 */
unsigned int sysctl_sched_latency = 6000000ULL; /*6ms*/

/*
 * is kept at sysctl_sched_latency / sysctl_sched_min_granularity
 */
static unsigned int sched_nr_latency = 8;

/*
 * Minimal preemption granularity for CPU-bound tasks:
 * (default: 0.75 msec * (1 + ilog(ncpus)), units: nanoseconds)
 */
unsigned int sysctl_sched_min_granularity = 750000ULL; /*0.75ms*/
```

```c
/*
 * The idea is to set a period in which each task runs once.
 *
 * When there are too many tasks (sched_nr_latency) we have to stretch
 * this period because otherwise the slices get too small.
 *
 * p = (nr <= nl) ? l : l*nr/nl
 */
static u64 __sched_period(unsigned long nr_running)
{
	u64 period = sysctl_sched_latency;/*6ms*/
	unsigned long nr_latency = sched_nr_latency;
	/*
	 * 如果nr_running大于nr_latency
	 * period = nr_running * sysctl_sched_min_granularity
	 * 否则period = sysctl_sched_latency
	 */
	if (unlikely(nr_running > nr_latency)) {
		period = sysctl_sched_min_granularity;/*0.75ms*/
		period *= nr_running;
	}

	return period;
}
```

### update_load_add

```c
static inline void update_load_add(struct load_weight *lw, unsigned long inc)
{
	lw->weight += inc;
	lw->inv_weight = 0;
}
```

