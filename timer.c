static unsigned arch_timers_present;
static int arch_timer_ppi[MAX_TIMER_PPI];

static struct clock_event_device __percpu *arch_timer_evt;
static bool arch_timer_use_virtual = true;
static bool arch_timer_c3stop;
static u32 arch_timer_rate;

DEFINE_PER_CPU(struct tick_device, tick_cpu_device);


/*
 * tick_do_timer_cpu is a timer core internal variable which holds the CPU NR
 * which is responsible for calling do_timer(), i.e. the timekeeping stuff. This
 * variable has two functions:
 *
 * 1) Prevent a thundering herd issue of a gazillion of CPUs trying to grab the
 *    timekeeping lock all at once. Only the CPU which is assigned to do the
 *    update is handling it.
 *
 * 2) Hand off the duty in the NOHZ idle case by setting the value to
 *    TICK_DO_TIMER_NONE, i.e. a non existing CPU. So the next cpu which looks
 *    at it will take over and keep the time keeping alive.  The handover
 *    procedure also covers cpu hotplug.
 */
int tick_do_timer_cpu __read_mostly = TICK_DO_TIMER_BOOT;



static void
arch_timer_detect_rate(void __iomem *cntbase, struct device_node *np)
{
	/* Who has more than one independent system counter? */
	if (arch_timer_rate)
		return;

	/* Try to determine the frequency from the device tree or CNTFRQ 
	 * 没有定义clock-frequency
	 */
	if (of_property_read_u32(np, "clock-frequency", &arch_timer_rate)) {
		if (cntbase)
			arch_timer_rate = readl_relaxed(cntbase + CNTFRQ);
		else /*cntbase为NULL*/
			arch_timer_rate = arch_timer_get_cntfrq();
	}

	/* Check the timer frequency. */
	if (arch_timer_rate == 0)
		pr_warn("Architected timer frequency not available\n");
}

void clockevents_config(struct clock_event_device *dev, u32 freq)
{
	u64 sec;

	if (!(dev->features & CLOCK_EVT_FEAT_ONESHOT))
		return;

	/*
	 * Calculate the maximum number of seconds we can sleep. Limit
	 * to 10 minutes for hardware which can program more than
	 * 32bit ticks so we still get reasonable conversion values.
	 */
	sec = dev->max_delta_ticks;
	do_div(sec, freq);
	if (!sec)
		sec = 1;
	else if (sec > 600 && dev->max_delta_ticks > UINT_MAX)
		sec = 600;

	clockevents_calc_mult_shift(dev, freq, sec);
	dev->min_delta_ns = cev_delta2ns(dev->min_delta_ticks, dev, false);
	dev->max_delta_ns = cev_delta2ns(dev->max_delta_ticks, dev, true);
}

/**
 * clockevents_exchange_device - release and request clock devices
 * @old:	device to release (can be NULL)
 * @new:	device to request (can be NULL)
 *
 * Called from the notifier chain. clockevents_lock is held already
 */
void clockevents_exchange_device(struct clock_event_device *old,
				 struct clock_event_device *new)
{
	unsigned long flags;

	local_irq_save(flags);
	/*
	 * Caller releases a clock event device. We queue it into the
	 * released list and do a notify add later.
	 */
	if (old) {/*第一次old为null*/
		module_put(old->owner);
		clockevents_set_mode(old, CLOCK_EVT_MODE_UNUSED);
		/*将该event从clockevent_devices中删除*/
		list_del(&old->list);
		/*添加到clockevents_released，在后面调用clockevents_notify_released会用到，
		*会进行device exchange
		*/
		list_add(&old->list, &clockevents_released);
	}

	if (new) {
		BUG_ON(new->mode != CLOCK_EVT_MODE_UNUSED);
		clockevents_shutdown(new);
	}
	local_irq_restore(flags);
}

/*
 * Event handler for periodic ticks
 */
void tick_handle_periodic(struct clock_event_device *dev)
{
	int cpu = smp_processor_id();
	ktime_t next = dev->next_event;

	tick_periodic(cpu);

	if (dev->mode != CLOCK_EVT_MODE_ONESHOT)
		return;
	for (;;) {
		/*
		 * Setup the next period for devices, which do not have
		 * periodic mode:
		 */
		next = ktime_add(next, tick_period);

		if (!clockevents_program_event(dev, next, false))
			return;
		/*
		 * Have to be careful here. If we're in oneshot mode,
		 * before we call tick_periodic() in a loop, we need
		 * to be sure we're using a real hardware clocksource.
		 * Otherwise we could get trapped in an infinite
		 * loop, as the tick_periodic() increments jiffies,
		 * which then will increment time, possibly causing
		 * the loop to trigger again and again.
		 */
		if (timekeeping_valid_for_hres())
			tick_periodic(cpu);
	}
}


void tick_set_periodic_handler(struct clock_event_device *dev, int broadcast)
{
	if (!broadcast) /*broadcast 为 0*/
		dev->event_handler = tick_handle_periodic;
	else
		dev->event_handler = tick_handle_periodic_broadcast;
}


/*
 * Setup the device for a periodic tick
 */
void tick_setup_periodic(struct clock_event_device *dev, int broadcast)
{
	tick_set_periodic_handler(dev, broadcast);

	/* Broadcast setup ? */
	if (!tick_device_is_functional(dev))
		return;

	if ((dev->features & CLOCK_EVT_FEAT_PERIODIC) &&
	    !tick_broadcast_oneshot_active()) {
		clockevents_set_mode(dev, CLOCK_EVT_MODE_PERIODIC);
	} else {
		unsigned long seq;
		ktime_t next;

		do {
			seq = read_seqbegin(&jiffies_lock);
			next = tick_next_period;
		} while (read_seqretry(&jiffies_lock, seq));

		/*设置为CLOCK_EVT_MODE_ONESHOT模式*/
		clockevents_set_mode(dev, CLOCK_EVT_MODE_ONESHOT);

		for (;;) {
			/*编程下一次到期时间*/
			if (!clockevents_program_event(dev, next, false))
				return;
			next = ktime_add(next, tick_period);
		}
	}
}

ktime_t ktime_get(void)
{
	/**
	 *启动的时候timekeeper的clock source为clocksource_jiffies
	 *timekeeping_init中会进行初始化
	 */
	struct timekeeper *tk = &tk_core.timekeeper;
	unsigned int seq;
	ktime_t base;
	s64 nsecs;

	do {
		seq = read_seqcount_begin(&tk_core.seq);
		base = tk->tkr.base_mono;
		nsecs = timekeeping_get_ns(&tk->tkr);

	} while (read_seqcount_retry(&tk_core.seq, seq));

	return ktime_add_ns(base, nsecs);
}
EXPORT_SYMBOL_GPL(ktime_get);

/*arm64*/

static void gic_raise_softirq(const struct cpumask *mask, unsigned int irq)
{
	int cpu;

	if (WARN_ON(irq >= 16))
		return;

	/*
	 * Ensure that stores to Normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	smp_wmb();

	for_each_cpu_mask(cpu, *mask) {
		u64 cluster_id = cpu_logical_map(cpu) & ~0xffUL;
		u16 tlist;

		tlist = gic_compute_target_list(&cpu, mask, cluster_id);
		gic_send_sgi(cluster_id, tlist, irq);
	}

	/* Force the above writes to ICC_SGI1R_EL1 to be executed */
	isb();
}

static void gic_smp_init(void)
{
	set_smp_cross_call(gic_raise_softirq);
	register_cpu_notifier(&gic_cpu_notifier);
}


void (*__smp_cross_call)(const struct cpumask *, unsigned int);

void __init set_smp_cross_call(void (*fn)(const struct cpumask *, unsigned int))
{
	__smp_cross_call = fn;
}


static void smp_cross_call(const struct cpumask *target, unsigned int ipinr)
{
	trace_ipi_raise(target, ipi_types[ipinr]);
	__smp_cross_call(target, ipinr);
}


#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
void tick_broadcast(const struct cpumask *mask)
{
	smp_cross_call(mask, IPI_TIMER);
}
#endif


static void tick_device_setup_broadcast_func(struct clock_event_device *dev)
{
	if (!dev->broadcast)
		dev->broadcast = tick_broadcast;

	/*tick_broadcast可能被定义为空*/
	if (!dev->broadcast) {
		pr_warn_once("%s depends on broadcast, but no broadcast function available\n",
			     dev->name);
		dev->broadcast = err_broadcast;
	}
}


/*
 * Check, if the device is functional or a dummy for broadcast
 */
static inline int tick_device_is_functional(struct clock_event_device *dev)
{
	return !(dev->features & CLOCK_EVT_FEAT_DUMMY);
}

/*
 * Check, if the device is disfunctional and a place holder, which
 * needs to be handled by the broadcast device.
 */
int tick_device_uses_broadcast(struct clock_event_device *dev, int cpu)
{
	struct clock_event_device *bc = tick_broadcast_device.evtdev;
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&tick_broadcast_lock, flags);

	/*
	 * Devices might be registered with both periodic and oneshot
	 * mode disabled. This signals, that the device needs to be
	 * operated from the broadcast device and is a placeholder for
	 * the cpu local device.
	 */
	if (!tick_device_is_functional(dev)) {
		dev->event_handler = tick_handle_periodic;
		tick_device_setup_broadcast_func(dev);
		cpumask_set_cpu(cpu, tick_broadcast_mask);
		if (tick_broadcast_device.mode == TICKDEV_MODE_PERIODIC)
			tick_broadcast_start_periodic(bc);
		else
			tick_broadcast_setup_oneshot(bc);
		ret = 1;
	} else {
		/*
		 * Clear the broadcast bit for this cpu if the
		 * device is not power state affected.
		 */
		/**
		 *如果不支持CLOCK_EVT_FEAT_C3STOP状态，则清楚tick_broadcast_mask
		 *tick_broadcast的tick会stop，不支持CLOCK_EVT_FEAT_C3STOP，则表示该
		 *tick会一直运行
		 */
		if (!(dev->features & CLOCK_EVT_FEAT_C3STOP))
			cpumask_clear_cpu(cpu, tick_broadcast_mask);
		else
			tick_device_setup_broadcast_func(dev);

		/*
		 * Clear the broadcast bit if the CPU is not in
		 * periodic broadcast on state.
		 */
		if (!cpumask_test_cpu(cpu, tick_broadcast_on))
			cpumask_clear_cpu(cpu, tick_broadcast_mask);

		switch (tick_broadcast_device.mode) {
		case TICKDEV_MODE_ONESHOT:
			/*
			 * If the system is in oneshot mode we can
			 * unconditionally clear the oneshot mask bit,
			 * because the CPU is running and therefore
			 * not in an idle state which causes the power
			 * state affected device to stop. Let the
			 * caller initialize the device.
			 */
			tick_broadcast_clear_oneshot(cpu);
			ret = 0;
			break;

		case TICKDEV_MODE_PERIODIC:/*默认为TICKDEV_MODE_PERIODIC*/
			/*
			 * If the system is in periodic mode, check
			 * whether the broadcast device can be
			 * switched off now.
			 */
			if (cpumask_empty(tick_broadcast_mask) && bc)
				clockevents_shutdown(bc);
			/*
			 * If we kept the cpu in the broadcast mask,
			 * tell the caller to leave the per cpu device
			 * in shutdown state. The periodic interrupt
			 * is delivered by the broadcast device.
			 */
			ret = cpumask_test_cpu(cpu, tick_broadcast_mask);/*返回0*/
			break;
		default:
			/* Nothing to do */
			ret = 0;
			break;
		}
	}
	raw_spin_unlock_irqrestore(&tick_broadcast_lock, flags);
	return ret;
}


/*
 * Setup the tick device
 */
static void tick_setup_device(struct tick_device *td,
			      struct clock_event_device *newdev, int cpu,
			      const struct cpumask *cpumask)
{
	ktime_t next_event;
	void (*handler)(struct clock_event_device *) = NULL;

	/*
	 * First device setup ?
	 */
	if (!td->evtdev) { /*第一次注册*/
		/*
		 * If no cpu took the do_timer update, assign it to
		 * this cpu:
		 */
		/**
		 *tick_do_timer_cpu表示那一个timer用来调用do_timer函数
		 *需要有一个global的tick device管理全局的jiffies等时间信息
		 */
		if (tick_do_timer_cpu == TICK_DO_TIMER_BOOT) {
			if (!tick_nohz_full_cpu(cpu))/*分析的代码中该函数直接返回false*/
				tick_do_timer_cpu = cpu;
			else
				tick_do_timer_cpu = TICK_DO_TIMER_NONE;
			tick_next_period = ktime_get();/**/
			tick_period = ktime_set(0, NSEC_PER_SEC / HZ);/*tick_period赋值*/
		}

		/*
		 * Startup in periodic mode first.
		 */
		td->mode = TICKDEV_MODE_PERIODIC;
	} else {
		handler = td->evtdev->event_handler;
		next_event = td->evtdev->next_event;
		td->evtdev->event_handler = clockevents_handle_noop;
	}

	td->evtdev = newdev;

	/*
	 * When the device is not per cpu, pin the interrupt to the
	 * current cpu:
	 */
	if (!cpumask_equal(newdev->cpumask, cpumask))
		irq_set_affinity(newdev->irq, cpumask);

	/*
	 * When global broadcasting is active, check if the current
	 * device is registered as a placeholder for broadcast mode.
	 * This allows us to handle this x86 misfeature in a generic
	 * way. This function also returns !=0 when we keep the
	 * current active broadcast state for this CPU.
	 */
	if (tick_device_uses_broadcast(newdev, cpu)) /*返回0*/
		return;

	/*初始的td mode为TICKDEV_MODE_PERIODIC*/
	if (td->mode == TICKDEV_MODE_PERIODIC)
		tick_setup_periodic(newdev, 0);
	else
		tick_setup_oneshot(newdev, handler, next_event);
}


/*
 * Async notification about clock event changes
 */
/**
 *在软中断run_timer_softirq中会进行检查ts->check_clocks是否置位，
 如果置位则会调用tick_check_oneshot_change切换到oneshot状态
 */
void tick_oneshot_notify(void)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);
	/*bit 0置1*/
	set_bit(0, &ts->check_clocks);
}


/*
 * Check, if the new registered device should be used. Called with
 * clockevents_lock held and interrupts disabled.
 */
void tick_check_new_device(struct clock_event_device *newdev)
{
	struct clock_event_device *curdev;
	struct tick_device *td;
	int cpu;

	cpu = smp_processor_id();


	td = &per_cpu(tick_cpu_device, cpu);
	curdev = td->evtdev;

	/* cpu local device ? */
	if (!tick_check_percpu(curdev, newdev, cpu))
		goto out_bc;

	/* Preference decision */
	if (!tick_check_preferred(curdev, newdev))
		goto out_bc;

	if (!try_module_get(newdev->owner))
		return;

	/*
	 * Replace the eventually existing device by the new
	 * device. If the current device is the broadcast device, do
	 * not give it back to the clockevents layer !
	 */
	if (tick_is_broadcast_device(curdev)) {/*不是broadcast_device*/
		clockevents_shutdown(curdev);
		curdev = NULL;
	}
	clockevents_exchange_device(curdev, newdev);
	tick_setup_device(td, newdev, cpu, cpumask_of(cpu));
	if (newdev->features & CLOCK_EVT_FEAT_ONESHOT)
		tick_oneshot_notify();
	return;

out_bc:
	/*
	 * Can the new device be used as a broadcast device ?
	 */
	tick_install_broadcast_device(newdev);
}


void clockevents_register_device(struct clock_event_device *dev)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&clockevents_lock, flags);

	/*添加到clockevent_devices链表中*/
	list_add(&dev->list, &clockevent_devices);
	tick_check_new_device(dev);
	clockevents_notify_released();

	raw_spin_unlock_irqrestore(&clockevents_lock, flags);
}


void clockevents_config_and_register(struct clock_event_device *dev,
				     u32 freq, unsigned long min_delta,
				     unsigned long max_delta)
{
	dev->min_delta_ticks = min_delta;
	dev->max_delta_ticks = max_delta;
	clockevents_config(dev, freq);
	clockevents_register_device(dev);
}


static void __arch_timer_setup(unsigned type,
			       struct clock_event_device *clk)
{
	clk->features = CLOCK_EVT_FEAT_ONESHOT;

	if (type == ARCH_CP15_TIMER) {
		if (arch_timer_c3stop) /*arch_timer_c3stop false*/
			clk->features |= CLOCK_EVT_FEAT_C3STOP;

		clk->name = "arch_sys_timer";
		clk->rating = 450;
		clk->cpumask = cpumask_of(smp_processor_id());
		if (arch_timer_use_virtual) { /*arch_timer_use_virtual = true*/
			clk->irq = arch_timer_ppi[VIRT_PPI];
			clk->set_mode = arch_timer_set_mode_virt;
			clk->set_next_event = arch_timer_set_next_event_virt;
		}
	} 

	clk->set_mode(CLOCK_EVT_MODE_SHUTDOWN, clk);

	clockevents_config_and_register(clk, arch_timer_rate, 0xf, 0x7fffffff);
}


static int arch_timer_setup(struct clock_event_device *clk)
{
	__arch_timer_setup(ARCH_CP15_TIMER, clk);

	/*arch_timer_use_virtual为true*/
	if (arch_timer_use_virtual)
		enable_percpu_irq(arch_timer_ppi[VIRT_PPI], 0);
	else {
		enable_percpu_irq(arch_timer_ppi[PHYS_SECURE_PPI], 0);
		if (arch_timer_ppi[PHYS_NONSECURE_PPI])
			enable_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI], 0);
	}

	arch_counter_set_user_access();
	if (IS_ENABLED(CONFIG_ARM_ARCH_TIMER_EVTSTREAM))
		arch_timer_configure_evtstream();

	return 0;
}


static int arch_timer_cpu_notify(struct notifier_block *self,
					   unsigned long action, void *hcpu)
{
	/*
	 * Grab cpu pointer in each case to avoid spurious
	 * preemptible warnings
	 */
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		arch_timer_setup(this_cpu_ptr(arch_timer_evt));
		break;
	case CPU_DYING:
		arch_timer_stop(this_cpu_ptr(arch_timer_evt));
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block arch_timer_cpu_nb = {
	.notifier_call = arch_timer_cpu_notify,
};


static __always_inline irqreturn_t timer_handler(const int access,
					struct clock_event_device *evt)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, evt);
	if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
		ctrl |= ARCH_TIMER_CTRL_IT_MASK;
		arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, evt);
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}


static irqreturn_t arch_timer_handler_virt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_VIRT_ACCESS, evt);
}



static int __init arch_timer_register(void)
{
	int err;
	int ppi;

	arch_timer_evt = alloc_percpu(struct clock_event_device);

	/*arch_timer_use_virtual为true*/
	if (arch_timer_use_virtual) {
		ppi = arch_timer_ppi[VIRT_PPI];
		err = request_percpu_irq(ppi, arch_timer_handler_virt,
					 "arch_timer", arch_timer_evt);
	} 

	/*配置其他cpu*/
	err = register_cpu_notifier(&arch_timer_cpu_nb);

	err = arch_timer_cpu_pm_init();

	/* Immediately configure the timer on the boot CPU */
	arch_timer_setup(this_cpu_ptr(arch_timer_evt));

	return 0;

}








static void arch_timer_banner(unsigned type)
{
	pr_info("Architected %s%s%s timer(s) running at %lu.%02luMHz (%s%s%s).\n",
		     type & ARCH_CP15_TIMER ? "cp15" : "",
		     type == (ARCH_CP15_TIMER | ARCH_MEM_TIMER) ?  " and " : "",
		     type & ARCH_MEM_TIMER ? "mmio" : "",
		     (unsigned long)arch_timer_rate / 1000000,
		     (unsigned long)(arch_timer_rate / 10000) % 100,
		     type & ARCH_CP15_TIMER ?
			arch_timer_use_virtual ? "virt" : "phys" :
			"",
		     type == (ARCH_CP15_TIMER | ARCH_MEM_TIMER) ?  "/" : "",
		     type & ARCH_MEM_TIMER ?
			arch_timer_mem_use_virtual ? "virt" : "phys" :
			"");
}

/*
 * Default to cp15 based access because arm64 uses this function for
 * sched_clock() before DT is probed and the cp15 method is guaranteed
 * to exist on arm64. arm doesn't use this before DT is probed so even
 * if we don't have the cp15 accessors we won't have a problem.
 */
u64 (*arch_timer_read_counter)(void) = arch_counter_get_cntvct;

static cycle_t arch_counter_read(struct clocksource *cs)
{
	return arch_timer_read_counter();
}


static struct clocksource clocksource_counter = {
	.name	= "arch_sys_counter",
	.rating	= 400,
	.read	= arch_counter_read,
	.mask	= CLOCKSOURCE_MASK(56),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_SUSPEND_NONSTOP,
};


static void __init arch_counter_register(unsigned type)
{
	u64 start_count;

	/* Register the CP15 based counter if we have one */
	if (type & ARCH_CP15_TIMER) {
		if (IS_ENABLED(CONFIG_ARM64) || arch_timer_use_virtual)
			arch_timer_read_counter = arch_counter_get_cntvct;
		else
			arch_timer_read_counter = arch_counter_get_cntpct;
	} else {
		arch_timer_read_counter = arch_counter_get_cntvct_mem;

		/* If the clocksource name is "arch_sys_counter" the
		 * VDSO will attempt to read the CP15-based counter.
		 * Ensure this does not happen when CP15-based
		 * counter is not available.
		 */
		clocksource_counter.name = "arch_mem_counter";
	}

	start_count = arch_timer_read_counter();
	/*clocksource_register注册*/
	clocksource_register_hz(&clocksource_counter, arch_timer_rate);
	cyclecounter.mult = clocksource_counter.mult;
	cyclecounter.shift = clocksource_counter.shift;
	timecounter_init(&timecounter, &cyclecounter, start_count);

	/* 56 bits minimum, so we assume worst case rollover */
	sched_clock_register(arch_timer_read_counter, 56, arch_timer_rate);
}


static void __init arch_timer_common_init(void)
{
	unsigned mask = ARCH_CP15_TIMER | ARCH_MEM_TIMER;

	/* Wait until both nodes are probed if we have two timers */
	if ((arch_timers_present & mask) != mask) {
		if (!arch_timer_probed(ARCH_MEM_TIMER, arch_timer_mem_of_match))
			return;
		if (!arch_timer_probed(ARCH_CP15_TIMER, arch_timer_of_match))
			return;
	}

	arch_timer_banner(arch_timers_present);
	arch_counter_register(arch_timers_present);
	/*arm64该函数为null*/
	arch_timer_arch_init();
}


static void __init arch_timer_init(struct device_node *np)
{
	int i;

	if (arch_timers_present & ARCH_CP15_TIMER) {
		pr_warn("arch_timer: multiple nodes in dt, skipping\n");
		return;
	}

	arch_timers_present |= ARCH_CP15_TIMER;
	for (i = PHYS_SECURE_PPI; i < MAX_TIMER_PPI; i++)
		arch_timer_ppi[i] = irq_of_parse_and_map(np, i);
	arch_timer_detect_rate(NULL, np);

	/*
	* If we cannot rely on firmware initializing the timer registers then
	* we should use the physical timers instead.
	*/
	if (IS_ENABLED(CONFIG_ARM) &&
		of_property_read_bool(np, "arm,cpu-registers-not-fw-configured"))
		arch_timer_use_virtual = false;

	/*
	* If HYP mode is available, we know that the physical timer
	* has been configured to be accessible from PL1. Use it, so
	* that a guest can use the virtual timer instead.
	*
	* If no interrupt provided for virtual timer, we'll have to
	* stick to the physical timer. It'd better be accessible...
	*/
	/*没有在 hyp模式*/


	/*arch_timer_c3stop为false*/
	arch_timer_c3stop = !of_property_read_bool(np, "always-on");

	/*最终会注册clock_device*/
	arch_timer_register();
	/*最终会注册clock cource*/
	arch_timer_common_init();
}

CLOCKSOURCE_OF_DECLARE(armv8_arch_timer, "arm,armv8-timer", arch_timer_init);

void __init clocksource_of_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	of_init_fn_1 init_func;
	unsigned clocksources = 0;

	/*使用CLOCKSOURCE_OF_DECLARE声明的会被放到__clksrc_of_table段*/
	for_each_matching_node_and_match(np, __clksrc_of_table, &match) {
		if (!of_device_is_available(np))
			continue;

		/*对于qemu arm64 init_func = arch_timer_init*/
		init_func = match->data;
		init_func(np);
		clocksources++;
	}
	if (!clocksources)
		pr_crit("%s: no matching clocksources found\n", __func__);
}


void __init time_init(void)
{
	u32 arch_timer_rate;

	/*pll 时钟相关*/
	of_clk_init(NULL);
	clocksource_of_init();

	tick_setup_hrtimer_broadcast();

	arch_timer_rate = arch_timer_get_rate();
	if (!arch_timer_rate)
		panic("Unable to initialise architected timer.\n");

	/* Calibrate the delay loop directly */
	lpj_fine = arch_timer_rate / HZ;
}


asmlinkage __visible void __init start_kernel(void)
{
	time_init();
}



