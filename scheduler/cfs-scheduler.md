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

__sched_period的参数为run queue上task的个数。这里不考虑组调度，此时for_each_sched_entity只会执行一次。

cfs_rq->nr_running + !se->on_rq，如果当前se在run queue则on_rq为1，不再run queue则on_rq为0，因为调用

sched_slice函数的时候，task不一定在运行。

**slice = slice \* se->load.weight / cfs_rq->load**

**slice为该se的weight占run queue load的比例，然后乘以总的slice时间**

sched_slice函数可以用一个公式表示：s = p*P[w/rw]

p：每一个task执行一次的总时间

P：应该是和组调度相关的，现在还不清楚

w：se的weight

rw：run queue的weight

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

##  min_vruntime

```c
static void update_min_vruntime(struct cfs_rq *cfs_rq)
{
    /*每一个cfs_rq上都会维护一个min_vruntime*/
	u64 vruntime = cfs_rq->min_vruntime;
	/*如果cfs_rq当前有se，则将vruntime赋值为当前se的vruntime*/
	if (cfs_rq->curr)
		vruntime = cfs_rq->curr->vruntime;

	if (cfs_rq->rb_leftmost) {
        /*
         * 当前cfs_rq红黑树的最左边有节点，则选取最左边节点的se
         * 最左边节点的vruntime是所有红黑树中最小的。
         */
		struct sched_entity *se = rb_entry(cfs_rq->rb_leftmost,
						   struct sched_entity,
						   run_node);
		/*
		 * 如果当前cfs_rq的curr为空，则min_runtime设置为最左边节点的vruntime和
		 * cfs_rq->min_vruntime
		 */
		if (!cfs_rq->curr)
			vruntime = se->vruntime;
		else /*cfs_rq->curr不为空，则在cfs_rq->curr->vruntime和se->vruntime选择较小的*/
			vruntime = min_vruntime(vruntime, se->vruntime);
	}

	/* ensure we never gain time by being placed backwards. */
    /* 为确保min_vruntime是递增的，所以选择一个最大的，确保时间不会回退 */
	cfs_rq->min_vruntime = max_vruntime(cfs_rq->min_vruntime, vruntime);
#ifndef CONFIG_64BIT
	smp_wmb();
	cfs_rq->min_vruntime_copy = cfs_rq->min_vruntime;
#endif
}
```

从上面的if语句可以看到会分下面几种情况

1. cfs_rq->curr和cfs_rq->rb_leftmost都为null，那么min_vruntime就是cfs_rq->min_vruntime;这个时候所有的task应该都属于cfs调度类。

2. cfs_rq->curr为null ，cfs_rq->rb_leftmost不为null，min_vruntime为cfs_rq->rb_leftmost->se.vruntime或cfs_rq->min_vruntime

3. cfs_rq->curr不为null，cfs_rq->rb_leftmost为null，min_vruntime为cfs_rq->min_vruntime和cfs_rq->curr->vruntime较大的

4. cfs_rq->curr和cfs_rq->rb_leftmost都为不null，min_vruntime可能为：

   cfs_rq->min_vruntime和cfs_rq->rb_leftmost->se.vruntime较大的

   或cfs_rq->min_vruntime和cfs_rq->curr->vruntime较大的

| cfs_rq->curr | cfs_rq->rb_leftmost | cfs_rq->min_vruntime                                        |
| ------------ | ------------------- | ----------------------------------------------------------- |
| NULL         | NULL                | cfs_rq->min_vruntime                                        |
| NULL         | 非NULL              | max(cfs_rq->rb_leftmost->se.vruntime，cfs_rq->min_vruntime) |
| 非NULL       | NULL                | max(cfs_rq->min_vruntime,cfs_rq->curr->vruntime)            |
| 非NULL |非NULL|cfs_rq->min_vruntime,cfs_rq->curr->vruntime，cfs_rq->rb_leftmost->se.vruntime|

## pick_next_task_fair

```c
static struct task_struct * pick_next_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se;
	struct task_struct *p;
	int new_tasks;

again:
	/*略去组调度*/
    /*如果当前rq上运行的进程数目为0，直接调到idle*/
	if (!cfs_rq->nr_running)
		goto idle;

	put_prev_task(rq, prev);

	do {
		se = pick_next_entity(cfs_rq, NULL);
		set_next_entity(cfs_rq, se);
		cfs_rq = group_cfs_rq(se);
	} while (cfs_rq);

	p = task_of(se);

	if (hrtick_enabled(rq))
		hrtick_start_fair(rq, p);

	return p;

idle:
	new_tasks = idle_balance(rq);
	/*
	 * Because idle_balance() releases (and re-acquires) rq->lock, it is
	 * possible for any higher priority task to appear. In that case we
	 * must re-start the pick_next_entity() loop.
	 */
	if (new_tasks < 0)
		return RETRY_TASK;

	if (new_tasks > 0)
		goto again;
	/*cfs_rq上没有可选择的task，则返回null*/
	return NULL;
}
```
put_prev_task属于调度框架中的函数，最终会调用调度类中的put_prev_task
```c
static inline void put_prev_task(struct rq *rq, struct task_struct *prev)
{
	prev->sched_class->put_prev_task(rq, prev);
}
```

以cfs的put_prev_task为例子：

### put_prev_task_fair

```c
static void put_prev_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se = &prev->se;
	struct cfs_rq *cfs_rq;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		put_prev_entity(cfs_rq, se);
	}
}
```

### put_prev_entity

```c
static void put_prev_entity(struct cfs_rq *cfs_rq, struct sched_entity *prev)
{
	/*
	 * If still on the runqueue then deactivate_task()
	 * was not called and update_curr() has to be done:
	 */
	if (prev->on_rq)
		update_curr(cfs_rq);

	/* throttle cfs_rqs exceeding runtime */
	check_cfs_rq_runtime(cfs_rq);

	check_spread(cfs_rq, prev);
	if (prev->on_rq) {
		update_stats_wait_start(cfs_rq, prev);
		/* Put 'current' back into the tree. */
        /*将prev插入到cfs的rbtree中*/
		__enqueue_entity(cfs_rq, prev);
		/* in !on_rq case, update occurred at dequeue */
		update_entity_load_avg(prev, 1);
	}
	cfs_rq->curr = NULL;/*cfs_rq->curr = NULL,当前cfs_rq上没有调度实体*/
}
```

```c
/*非组调度的时候curr为NULL*/
static struct sched_entity *
pick_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
    /*返回retree最左边的地点对应的调度实体*/
	struct sched_entity *left = __pick_first_entity(cfs_rq);
	struct sched_entity *se;

	/*
	 * If curr is set we have to see if its left of the leftmost entity
	 * still in the tree, provided there was anything in the tree at all.
	 */
	if (!left || (curr && entity_before(curr, left)))
		left = curr;

	se = left; /* ideally we run the leftmost entity */

	/*
	 * Avoid running the skip buddy, if running something else can
	 * be done without getting too unfair.
	 */
	if (cfs_rq->skip == se) {/*cfs_rq->skip在调用yeld会被设置*/
		struct sched_entity *second;
		/* 如果se为curr，即调用yeld的task，调用schedule函数。那么curr就不再rbtree中，所以选择
		 * rbtree中的第一个
		 */
		if (se == curr) {
			second = __pick_first_entity(cfs_rq);
		} else {/*se ！= curr，表示调用yeld的task被被人抢占了。选择先一个调度实体*/
			second = __pick_next_entity(se);
			if (!second || (curr && entity_before(curr, second)))
				second = curr;
		}

		if (second && wakeup_preempt_entity(second, left) < 1)
			se = second;
	}

    /*
     * 以下部分和SCHED_FEAT(NEXT_BUDDY, 0)，SCHED_FEAT(LAST_BUDDY, 1)调度特性相关
     * cfs_rq->last和cfs_rq->next的复制实在check_preempt_wakeup中
     */
	/*
	 * Prefer last buddy, try to return the CPU to a preempted task.
	 */
	if (cfs_rq->last && wakeup_preempt_entity(cfs_rq->last, left) < 1)
		se = cfs_rq->last;

	/*
	 * Someone really wants this to run. If it's not unfair, run it.
	 */
	if (cfs_rq->next && wakeup_preempt_entity(cfs_rq->next, left) < 1)
		se = cfs_rq->next;

	clear_buddies(cfs_rq, se);

	return se;
}
```

### set_next_entity

```c
static void
set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* 'current' is not kept within the tree. */
	if (se->on_rq) {/*如果选择的se已经在rq上则更新统计信息，并且从rq中移除*/
		/*
		 * Any task has to be enqueued before it get to execute on
		 * a CPU. So account for the time it spent waiting on the
		 * runqueue.
		 */
		update_stats_wait_end(cfs_rq, se);
		__dequeue_entity(cfs_rq, se);
	}

	update_stats_curr_start(cfs_rq, se);
	/*设置当前的se为pick的se*/
    cfs_rq->curr = se;

	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}
```

se->on_rq的表示该se已经在run queue上了，在enqueue_entity会设置。

## enqueue_task_fair

```c
static void
enqueue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	for_each_sched_entity(se) {
		if (se->on_rq)
			break;
		cfs_rq = cfs_rq_of(se);
		enqueue_entity(cfs_rq, se, flags);

		/*
		 * end evaluation on encountering a throttled cfs_rq
		 *
		 * note: in the case of encountering a throttled cfs_rq we will
		 * post the final h_nr_running increment below.
		*/
		if (cfs_rq_throttled(cfs_rq))
			break;
		cfs_rq->h_nr_running++;

		flags = ENQUEUE_WAKEUP;
	}

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		cfs_rq->h_nr_running++;

		if (cfs_rq_throttled(cfs_rq))
			break;

		update_cfs_shares(cfs_rq);
		update_entity_load_avg(se, 1);
	}

	if (!se) {
		update_rq_runnable_avg(rq, rq->nr_running);
		add_nr_running(rq, 1);
	}
	hrtick_update(rq);
}

```

### enqueue_entity

```c
static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int flags)
{
	/*
	 * Update the normalized vruntime before updating min_vruntime
	 * through calling update_curr().
	 */
    /*
     *是为了应对se在不同的cpu上进行 迁移
     */
	if (!(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_WAKING))
		se->vruntime += cfs_rq->min_vruntime;

	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);
	enqueue_entity_load_avg(cfs_rq, se, flags & ENQUEUE_WAKEUP);
	account_entity_enqueue(cfs_rq, se);
	update_cfs_shares(cfs_rq);
	/*如果是唤醒进程被加入rq，则会设置ENQUEUE_WAKEUP*/
	if (flags & ENQUEUE_WAKEUP) {
        /*inittal设置为0，该函数会设置se的vruntime*/
		place_entity(cfs_rq, se, 0);
		enqueue_sleeper(cfs_rq, se);
	}

	update_stats_enqueue(cfs_rq, se);
	check_spread(cfs_rq, se);
	if (se != cfs_rq->curr)
        /*将se插入到rbtree中*/
		__enqueue_entity(cfs_rq, se);
	se->on_rq = 1;/*表示该se在run queue上*/

	if (cfs_rq->nr_running == 1) {
		list_add_leaf_cfs_rq(cfs_rq);
		check_enqueue_throttle(cfs_rq);
	}
}

```

在多CPU的系统上，不同的CPU的负载不一样，有的CPU更忙一些，而每个CPU都有自己的运行队列，每个队列中的进程的vruntime也走得有快有慢，比如我们对比每个运行队列的min_vruntime值，都会有不同, 如果一个进程从min_vruntime更小的CPU (A) 上迁移到min_vruntime更大的CPU (B) 上，可能就会占便宜了，因为CPU (B) 的运行队列中进程的vruntime普遍比较大，迁移过来的进程就会获得更多的CPU时间片。这显然不太公平

同样的问题出现在刚创建的进程上, 还没有投入运行, 没有加入到某个就绪队列中, 它以某个就绪队列的min_vruntime为基准设置了虚拟运行时间, 但是进程不一定在当前CPU上运行, 即新创建的进程应该是可以被迁移的.

CFS是这样做的：

- 当进程从一个CPU的运行队列中出来 (dequeue_entity) 的时候，它的vruntime要减去队列的min_vruntime值
- 而当进程加入另一个CPU的运行队列 ( enqueue_entiry) 时，它的vruntime要加上该队列的min_vruntime值
- 当进程刚刚创建以某个cfs_rq的min_vruntime为基准设置其虚拟运行时间后，也要减去队列的min_vruntime值

这样，进程从一个CPU迁移到另一个CPU之后，vruntime保持相对公平。

### account_entity_enqueue

```c
static void
account_entity_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
    /*增加rq上的weight*/
	update_load_add(&cfs_rq->load, se->load.weight);
	if (!parent_entity(se))
		update_load_add(&rq_of(cfs_rq)->load, se->load.weight);
#ifdef CONFIG_SMP
	if (entity_is_task(se)) {
		struct rq *rq = rq_of(cfs_rq);

		account_numa_enqueue(rq, task_of(se));
		list_add(&se->group_node, &rq->cfs_tasks);
	}
#endif
    /*运行队列数据增加*/
	cfs_rq->nr_running++;
}
```

### place_entity

```c
static void
place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int initial)
{
	u64 vruntime = cfs_rq->min_vruntime;

	/*
	 * The 'current' period is already promised to the current tasks,
	 * however the extra weight of the new task will slow them down a
	 * little, place the new task so that it fits in the slot that
	 * stays open at the end.
	 */
	if (initial && sched_feat(START_DEBIT))
		vruntime += sched_vslice(cfs_rq, se);

	/* sleeps up to a single latency don't count. */
	if (!initial) {/*新进程会设置initial为1，休眠被唤醒的进程initial为0*/
		unsigned long thresh = sysctl_sched_latency;

		/*
		 * Halve their sleep time's effect, to allow
		 * for a gentler effect of sleepers:
		 */
		if (sched_feat(GENTLE_FAIR_SLEEPERS))
			thresh >>= 1;
		/*vruntime减去thresh*/
		vruntime -= thresh;
	}

	/* ensure we never gain time by being placed backwards. */
    /*将se->vruntime, vruntime较大的赋值为se->vruntime，可以看到入队的se的runtime最小
     *也为cfs_rq->min_vruntime - thresh
     */
	se->vruntime = max_vruntime(se->vruntime, vruntime);
}
```

## dequeue_task_fair

dequeue_task_fair的动作和enqueue_task_fair基本刚好相反。

