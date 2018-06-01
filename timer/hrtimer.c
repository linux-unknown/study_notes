
/*
 * Async notification about clock event changes
 */

/**
 *tick_periodic-->update_process_times-->run_local_timers
 *-->raise_softirq
 *run_timer_softirq-->hrtimer_run_pending
 *在软中断run_timer_softirq中会进行检查ts->check_clocks是否置位，
 *如果置位则会调用tick_check_oneshot_change切换到oneshot状态
 *
 *在timer的每次中断中都会调用run_timer_softirq
 */

/*
 *在注册clock_event_device的时候，如果dev满足
 *newdev->features & CLOCK_EVT_FEAT_ONESHOT
 *则会调用该函数，置ts->check_clocks 位0为1
 *
 *系统启动刚开刚开始的时候，使用的是低精度timer
 *在run_timer_softirq-->hrtimer_run_pending会检测
 *check_clocks是否置位，如果置位则会切换到高精度timer
 */
void tick_oneshot_notify(void)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);
	/*bit 0置1*/
	set_bit(0, &ts->check_clocks);
}



/**
 *在系统启动之后，换选择最后的clocksource
 *clocksource_done_booting-->
 *	clocksource_select-->
 *		__clocksource_select-->
 *			timekeeping_notify
 */

/**
 * Async notification about clocksource changes
 */
void tick_clock_notify(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		set_bit(0, &per_cpu(tick_cpu_sched, cpu).check_clocks);
}



/*
 * This function runs timers and the timer-tq in bottom half context.
 */
static void run_timer_softirq(struct softirq_action *h)
{
	struct tvec_base *base = __this_cpu_read(tvec_bases);

	hrtimer_run_pending();

	if (time_after_eq(jiffies, base->timer_jiffies))
		__run_timers(base);
}

/*
 * Called from timer softirq every jiffy, expire hrtimers:
 *
 * For HRT its the fall back code to run the softirq in the timer
 * softirq context in case the hrtimer initialization failed or has
 * not been done yet.
 */
void hrtimer_run_pending(void)
{
	/*如何hrtimer已经active，则返回*/
	if (hrtimer_hres_active())
		return;

	/*
	 * This _is_ ugly: We have to check in the softirq context,
	 * whether we can switch to highres and / or nohz mode. The
	 * clocksource switch happens in the timer interrupt with
	 * xtime_lock held. Notification from there only sets the
	 * check bit in the tick_oneshot code, otherwise we might
	 * deadlock vs. xtime_lock.
	 */
	
	if (tick_check_oneshot_change(!hrtimer_is_hres_enabled()))
		hrtimer_switch_to_hres();
}

static inline int hrtimer_is_hres_enabled(void)
{
	/*hrtimer_hres_enabled默认是使能的，可以通过cmdline进行关闭*/
	return hrtimer_hres_enabled;
}

/**
 * Check, if a change happened, which makes oneshot possible.
 *
 * Called cyclic from the hrtimer softirq (driven by the timer
 * softirq) allow_nohz signals, that we can switch into low-res nohz
 * mode, because high resolution timers are disabled (either compile
 * or runtime).
 */
int tick_check_oneshot_change(int allow_nohz)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);

	/*返回1表示已经置位*/
	if (!test_and_clear_bit(0, &ts->check_clocks))
		return 0;

	/*
	 *默认情况下nohz_mode为NOHZ_MODE_INACTIVE，
	 *不为NOHZ_MODE_INACTIVE，表示已经切换到了NOHZ模式
	 */
	if (ts->nohz_mode != NOHZ_MODE_INACTIVE)
		return 0;

	/*返回0表示不支持高精度或oneshot*/
	if (!timekeeping_valid_for_hres() || !tick_is_oneshot_available())
		return 0;

	/*allow_nohz为0，如果为1，表示高精度定时器被禁止*/
	if (!allow_nohz)
		return 1;

	/*如果上面的条件都不满足，则切换到低精度的nohz模式*/
	tick_nohz_switch_to_nohz();
	return 0;
}

/*
 * Switch to high resolution mode
 */
static int hrtimer_switch_to_hres(void)
{
	int i, cpu = smp_processor_id();
	struct hrtimer_cpu_base *base = &per_cpu(hrtimer_bases, cpu);
	unsigned long flags;

	if (base->hres_active)
		return 1;

	local_irq_save(flags);

	if (tick_init_highres()) {
		local_irq_restore(flags);
		printk(KERN_WARNING "Could not switch to high resolution "
				    "mode on CPU %d\n", cpu);
		return 0;
	}
	base->hres_active = 1;
	for (i = 0; i < HRTIMER_MAX_CLOCK_BASES; i++)
		base->clock_base[i].resolution = KTIME_HIGH_RES;

	tick_setup_sched_timer();
	/* "Retrigger" the interrupt to get things going */
	retrigger_next_event(NULL);
	local_irq_restore(flags);
	return 1;
}

/**
 * tick_init_highres - switch to high resolution mode
 *
 * Called with interrupts disabled.
 */
int tick_init_highres(void)
{
	/*会设置clock_event_device的handler为hrtimer_interrupt*/
	return tick_switch_to_oneshot(hrtimer_interrupt);
}

/**
 * tick_switch_to_oneshot - switch to oneshot mode
 */
int tick_switch_to_oneshot(void (*handler)(struct clock_event_device *))
{
	struct tick_device *td = this_cpu_ptr(&tick_cpu_device);
	struct clock_event_device *dev = td->evtdev;

	if (!dev || !(dev->features & CLOCK_EVT_FEAT_ONESHOT) ||
		    !tick_device_is_functional(dev)) {

		printk(KERN_INFO "Clockevents: "
		       "could not switch to one-shot mode:");
		if (!dev) {
			printk(" no tick device\n");
		} else {
			if (!tick_device_is_functional(dev))
				printk(" %s is not functional.\n", dev->name);
			else
				printk(" %s does not support one-shot mode.\n",
				       dev->name);
		}
		return -EINVAL;
	}

	td->mode = TICKDEV_MODE_ONESHOT;
	dev->event_handler = handler;
	clockevents_set_mode(dev, CLOCK_EVT_MODE_ONESHOT);
	tick_broadcast_switch_to_oneshot();
	return 0;
}


/**
 * tick_setup_sched_timer - setup the tick emulation timer
 */
void tick_setup_sched_timer(void)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);
	ktime_t now = ktime_get();

	/*
	 * Emulate tick processing via per-CPU hrtimers:
	 */

	/*配置一个高精度timer用来模拟jiffies tick*/
	hrtimer_init(&ts->sched_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	ts->sched_timer.function = tick_sched_timer;

	/* Get the next period (per cpu) */
	hrtimer_set_expires(&ts->sched_timer, tick_init_jiffy_update());

	/* Offset the tick to avert jiffies_lock contention. */
	if (sched_skew_tick) {
		u64 offset = ktime_to_ns(tick_period) >> 1;
		do_div(offset, num_possible_cpus());
		offset *= smp_processor_id();
		hrtimer_add_expires_ns(&ts->sched_timer, offset);
	}

	for (;;) {
		hrtimer_forward(&ts->sched_timer, now, tick_period);
		hrtimer_start_expires(&ts->sched_timer,
				      HRTIMER_MODE_ABS_PINNED);
		/* Check, if the timer was already in the past */
		if (hrtimer_active(&ts->sched_timer))
			break;
		now = ktime_get();
	}

#ifdef CONFIG_NO_HZ_COMMON
	if (tick_nohz_enabled) {
		ts->nohz_mode = NOHZ_MODE_HIGHRES;
		tick_nohz_active = 1;
	}
#endif
}


