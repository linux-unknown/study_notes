/* Give new task start runnable values to heavy its load in infant time */
void init_task_runnable_average(struct task_struct *p)
{
	u32 slice;

	slice = sched_slice(task_cfs_rq(p), &p->se) >> 10;
	p->se.avg.runnable_avg_sum = slice;
	p->se.avg.runnable_avg_period = slice;
	__update_task_entity_contrib(&p->se);
}


/*所有进程运行一边，需要的时间*/
static u64 __sched_period(unsigned long nr_running)
{
	u64 period = sysctl_sched_latency;
	unsigned long nr_latency = sched_nr_latency;

	if (unlikely(nr_running > nr_latency)) {
		period = sysctl_sched_min_granularity;
		period *= nr_running;
	}

	return period;
}


static u64 __calc_delta(u64 delta_exec, unsigned long weight, struct load_weight *lw)
{
	/**
	 * 该函数的执行结果为delta_exec * weight / lw.weight
	 * weight：se->weitht
	 * lw.weight = cfs_rq->load
	 */
}

static inline void __update_task_entity_contrib(struct sched_entity *se)
{
	u32 contrib;

	/* avoid overflowing a 32-bit type w/ SCHED_LOAD_SCALE */

	/**
	 * contrib = se->avg.runnable_avg_sum * se->load.weight / se->avg.runnable_avg_period
	 */
	contrib = se->avg.runnable_avg_sum * scale_load_down(se->load.weight);
	contrib /= (se->avg.runnable_avg_period + 1);
	se->avg.load_avg_contrib = scale_load(contrib);
}










/* Update a sched_entity's runnable average */
static inline void update_entity_load_avg(struct sched_entity *se,
					  int update_cfs_rq)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	long contrib_delta;
	u64 now;

	/*
	 * For a group entity we need to use their owned cfs_rq_clock_task() in
	 * case they are the parent of a throttled hierarchy.
	 */
	if (entity_is_task(se))
		/*或是当前时间*/
		now = cfs_rq_clock_task(cfs_rq);
	else
		now = cfs_rq_clock_task(group_cfs_rq(se));

	if (!__update_entity_runnable_avg(now, &se->avg, se->on_rq))
		return;

	contrib_delta = __update_entity_load_avg_contrib(se);

	if (!update_cfs_rq)
		return;

	if (se->on_rq)
		cfs_rq->runnable_load_avg += contrib_delta;
	else
		subtract_blocked_load_contrib(cfs_rq, -contrib_delta);
}

static __always_inline int __update_entity_runnable_avg(u64 now,
							struct sched_avg *sa,
							int runnable)
{
	u64 delta, periods;
	u32 runnable_contrib;
	int delta_w, decayed = 0;

	delta = now - sa->last_runnable_update;
	/*
	 * This should only happen when time goes backwards, which it
	 * unfortunately does during sched clock init when we swap over to TSC.
	 */
	if ((s64)delta < 0) {
		sa->last_runnable_update = now;
		return 0;
	}

	/*
	 * Use 1024ns as the unit of measurement since it's a reasonable
	 * approximation of 1us and fast to compute.
	 */
	delta >>= 10;
	if (!delta)
		return 0;
	sa->last_runnable_update = now;

	/* delta_w is the amount already accumulated against our next period */
	delta_w = sa->runnable_avg_period % 1024;
	if (delta + delta_w >= 1024) {
		/* period roll-over */
		decayed = 1;

		/*
		 * Now that we know we're crossing a period boundary, figure
		 * out how much from delta we need to complete the current
		 * period and accrue it.
		 */
		delta_w = 1024 - delta_w;
		if (runnable)
			sa->runnable_avg_sum += delta_w;
		sa->runnable_avg_period += delta_w;

		delta -= delta_w;

		/* Figure out how many additional periods this update spans */
		periods = delta / 1024;
		delta %= 1024;

		sa->runnable_avg_sum = decay_load(sa->runnable_avg_sum,
						  periods + 1);
		sa->runnable_avg_period = decay_load(sa->runnable_avg_period,
						     periods + 1);

		/* Efficiently calculate \sum (1..n_period) 1024*y^i */
		runnable_contrib = __compute_runnable_contrib(periods);
		if (runnable)
			sa->runnable_avg_sum += runnable_contrib;
		sa->runnable_avg_period += runnable_contrib;
	}

	/* Remainder of delta accrued against u_0` */
	if (runnable)
		sa->runnable_avg_sum += delta;
	sa->runnable_avg_period += delta;

	return decayed;
}




static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	u64 slice = __sched_period(cfs_rq->nr_running + !se->on_rq);

	for_each_sched_entity(se) {
		struct load_weight *load;
		struct load_weight lw;

		cfs_rq = cfs_rq_of(se);
		load = &cfs_rq->load;

		if (unlikely(!se->on_rq)) {
			lw = cfs_rq->load;

			update_load_add(&lw, se->load.weight);
			load = &lw;
		}
		slice = __calc_delta(slice, se->load.weight, load);
	}
	return slice;
}



