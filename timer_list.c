#define TVN_BITS (CONFIG_BASE_SMALL ? 4 : 6)
#define TVR_BITS (CONFIG_BASE_SMALL ? 6 : 8)
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)
#define MAX_TVAL ((unsigned long)((1ULL << (TVR_BITS + 4*TVN_BITS)) - 1))

/*N+1的tv有没有到期的timer*/
#define INDEX(N) ((base->timer_jiffies >> (TVR_BITS + (N) * TVN_BITS)) & TVN_MASK)

struct tvec {
	struct list_head vec[TVN_SIZE];
};

struct tvec_root {
	struct list_head vec[TVR_SIZE];
};

struct tvec_base {
	spinlock_t lock;
	struct timer_list *running_timer;
	unsigned long timer_jiffies;
	unsigned long next_timer;
	unsigned long active_timers;
	unsigned long all_timers;
	int cpu;
	struct tvec_root tv1;
	struct tvec tv2;
	struct tvec tv3;
	struct tvec tv4;
	struct tvec tv5;
} ____cacheline_aligned;

struct tvec_base boot_tvec_bases;
EXPORT_SYMBOL(boot_tvec_bases);
static DEFINE_PER_CPU(struct tvec_base *, tvec_bases) = &boot_tvec_bases;


/**
 * add_timer - start a timer
 * @timer: the timer to be added
 *
 * The kernel will do a ->function(->data) callback from the
 * timer interrupt at the ->expires point in the future. The
 * current time is 'jiffies'.
 *
 * The timer's ->expires, ->function (and if the handler uses it, ->data)
 * fields must be set prior calling this function.
 *
 * Timers with an ->expires field in the past will be executed in the next
 * timer tick.
 */
void add_timer(struct timer_list *timer)
{
	BUG_ON(timer_pending(timer));
	mod_timer(timer, timer->expires);
}
EXPORT_SYMBOL(add_timer);

/*
 * If the list is empty, catch up ->timer_jiffies to the current time.
 * The caller must hold the tvec_base lock.  Returns true if the list
 * was empty and therefore ->timer_jiffies was updated.
 */
static bool catchup_timer_jiffies(struct tvec_base *base)
{
	if (!base->all_timers) {
		/*base->timer_jiffies初始化的时候就等于当时的jiffies*/
		base->timer_jiffies = jiffies;
		return true;
	}
	return false;
}


/**
 * timer_pending - is a timer pending?
 * @timer: the timer in question
 *
 * timer_pending will tell whether a given timer is currently pending,
 * or not. Callers must ensure serialization wrt. other operations done
 * to this timer, eg. interrupt contexts, or other CPUs on SMP.
 *
 * return value: 1 if the timer is pending, 0 if not.
 */
static inline int timer_pending(const struct timer_list * timer)
{
	return timer->entry.next != NULL;
}


/**
 * mod_timer - modify a timer's timeout
 * @timer: the timer to be modified
 * @expires: new timeout in jiffies
 *
 * mod_timer() is a more efficient way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 *
 * mod_timer(timer, expires) is equivalent to:
 *
 *     del_timer(timer); timer->expires = expires; add_timer(timer);
 *
 * Note that if there are multiple unserialized concurrent users of the
 * same timer, then mod_timer() is the only safe way to modify the timeout,
 * since add_timer() cannot modify an already running timer.
 *
 * The function returns whether it has modified a pending timer or not.
 * (ie. mod_timer() of an inactive timer returns 0, mod_timer() of an
 * active timer returns 1.)
 */
int mod_timer(struct timer_list *timer, unsigned long expires)
{
	/**
	 *对于调用了set_timer_slack的timer，使用该函数计算实际的expires值
	 *用于对时间不是很敏感的场景
	 */
	expires = apply_slack(timer, expires);

	/*
	 * This is a common optimization triggered by the
	 * networking code - if the timer is re-modified
	 * to be the same thing then just return:
	 */
	/*对于还没有执行，并且超时时间相同的timer直接返回*/
	if (timer_pending(timer) && timer->expires == expires)
		return 1;

	return __mod_timer(timer, expires, false, TIMER_NOT_PINNED);
}
EXPORT_SYMBOL(mod_timer);

static inline void detach_timer(struct timer_list *timer, bool clear_pending)
{
	struct list_head *entry = &timer->entry;

	debug_deactivate(timer);

	__list_del(entry->prev, entry->next);
	if (clear_pending)
		entry->next = NULL;
	entry->prev = LIST_POISON2;
}


static int detach_if_pending(struct timer_list *timer, struct tvec_base *base,
			     bool clear_pending)
{
	/*对于刚添加的timer，在该函数就会返回。已经添加的timer，会返回1*/
	if (!timer_pending(timer))
		return 0;

	/*将该timer总链表中删除*/
	detach_timer(timer, clear_pending);
	/*如果不是deferrable timer*/
	if (!tbase_get_deferrable(timer->base)) {
		base->active_timers--;
		if (timer->expires == base->next_timer)
			/*base->next_timer 下一个timer的到期时间*/
			base->next_timer = base->timer_jiffies;
	}
	base->all_timers--;
	(void)catchup_timer_jiffies(base);
	return 1;
}


static inline int
__mod_timer(struct timer_list *timer, unsigned long expires,
						bool pending_only, int pinned)
{
	struct tvec_base *base, *new_base;
	unsigned long flags;
	int ret = 0 , cpu;

	/*应该是个调试有关*/
	timer_stats_timer_set_start_info(timer);

	/**
	 *获得tvec_base，该相适合CPU相关的，每一个cpu有一个tvec_base，因为cpu houplug
	 *timer的base可能会变
	 */
	base = lock_timer_base(timer, &flags);

	/*0,表示没有pending*/
	ret = detach_if_pending(timer, base, false);
	if (!ret && pending_only)/**/
		goto out_unlock;

	debug_activate(timer, expires);

	cpu = get_nohz_timer_target(pinned);
	new_base = per_cpu(tvec_bases, cpu);

	if (base != new_base) {
		/*
		 * We are trying to schedule the timer on the local CPU.
		 * However we can't change timer's base while it is running,
		 * otherwise del_timer_sync() can't detect that the timer's
		 * handler yet has not finished. This also guarantees that
		 * the timer is serialized wrt itself.
		 */
		if (likely(base->running_timer != timer)) {
			/* See the comment in lock_timer_base() */
			timer_set_base(timer, NULL);
			spin_unlock(&base->lock);
			base = new_base;
			spin_lock(&base->lock);
			timer_set_base(timer, base);
		}
	}

	timer->expires = expires;
	internal_add_timer(base, timer);

out_unlock:
	spin_unlock_irqrestore(&base->lock, flags);

	return ret;
}

static void internal_add_timer(struct tvec_base *base, struct timer_list *timer)
{
	(void)catchup_timer_jiffies(base);
	__internal_add_timer(base, timer);
	/*
	 * Update base->active_timers and base->next_timer
	 */
	if (!tbase_get_deferrable(timer->base)) {
		if (!base->active_timers++ ||
		    time_before(timer->expires, base->next_timer))
			base->next_timer = timer->expires;
	}
	base->all_timers++;

	/*
	 * Check whether the other CPU is in dynticks mode and needs
	 * to be triggered to reevaluate the timer wheel.
	 * We are protected against the other CPU fiddling
	 * with the timer by holding the timer base lock. This also
	 * makes sure that a CPU on the way to stop its tick can not
	 * evaluate the timer wheel.
	 *
	 * Spare the IPI for deferrable timers on idle targets though.
	 * The next busy ticks will take care of it. Except full dynticks
	 * require special care against races with idle_cpu(), lets deal
	 * with that later.
	 */
	if (!tbase_get_deferrable(base) || tick_nohz_full_cpu(base->cpu))
		wake_up_nohz_cpu(base->cpu);
}

static void
__internal_add_timer(struct tvec_base *base, struct timer_list *timer)
{
	unsigned long expires = timer->expires;
	/*
	 *base->timer_jiffies在base->all_timers为0时，赋值为当前的jiffies
	 *在tvec_base初始化的的时候，也赋值为jiffies，
	 *在处理到期的timer时，该值会增加
	 */
	unsigned long idx = expires - base->timer_jiffies;
	struct list_head *vec;

	if (idx < TVR_SIZE) { /*TVR_SIZE:255*/
		/*
		 *如果差值小于255则将将该timer挂到tv1.vec 对应的索引上
		 *所以tv1.vec的每一个元素的timer相差一个jiffies时间间隔
		 *tv1.vec的每个元素作为链表头，链起来的timer他们的expires是一样的，链表元素的个数没有限制。
		 */
		int i = expires & TVR_MASK;
		vec = base->tv1.vec + i;
	} else if (idx < 1 << (TVR_BITS + TVN_BITS)) { /* 1 << (8 +6) */
		/*
		 *idx需要小于2^14才会分配到tv2.sec
		 *而且从i = (expires >> TVR_BITS) & TVN_MASK看
		 *tv2.vec每个元素之间相差的时间为256个jiffies
		 *tv2.vec每个元素作为链表头链接起来的每个timer的expires右移TVR_BITS位后是一样的
		 */
		int i = (expires >> TVR_BITS) & TVN_MASK;
		vec = base->tv2.vec + i;
	} else if (idx < 1 << (TVR_BITS + 2 * TVN_BITS)) {
		/*
		 *idx < 2 << 8 + 2 * 16 才分配到tv3.vec
		 *从i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK看
		 *tv3.vec每个元素之间相差1 << (8 + 6)个jiffie
		 **tv3.vec每个元素作为链表头链接起来的每个timer的expires右移(TVR_BITS + TVN_BITS)位后是一样的
		 */
		int i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
		vec = base->tv3.vec + i;
	} else if (idx < 1 << (TVR_BITS + 3 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
		vec = base->tv4.vec + i;
	} else if ((signed long) idx < 0) {
		/*
		 * Can happen if you add a timer with expires == jiffies,
		 * or you set a timer to go off in the past
		 */
		vec = base->tv1.vec + (base->timer_jiffies & TVR_MASK);
	} else {
		int i;
		/* If the timeout is larger than MAX_TVAL (on 64-bit
		 * architectures or with CONFIG_BASE_SMALL=1) then we
		 * use the maximum timeout.
		 */
		if (idx > MAX_TVAL) {
			idx = MAX_TVAL;
			expires = idx + base->timer_jiffies;
		}
		i = (expires >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
		vec = base->tv5.vec + i;
	}
	/*
	 * Timers are FIFO:
	 */
	/*将timer挂到对应timer的链表末尾*/
	list_add_tail(&timer->entry, vec);
}




asmlinkage __visible void __init start_kernel(void)
{
	init_timers();
}

static int init_timers_cpu(int cpu)
{
	int j;
	struct tvec_base *base;
	static char tvec_base_done[NR_CPUS];


	if (!tvec_base_done[cpu]) { /*第一次肯定进该分支*/
		static char boot_done;

		if (boot_done) {
			/*
			 * The APs use this path later in boot
			 */
			base = kzalloc_node(sizeof(*base), GFP_KERNEL,
					    cpu_to_node(cpu));
			if (!base)
				return -ENOMEM;

			/* Make sure tvec_base has TIMER_FLAG_MASK bits free */
			if (WARN_ON(base != tbase_get_base(base))) {
				kfree(base);
				return -ENOMEM;
			}
			per_cpu(tvec_bases, cpu) = base;
		} else {
			/*
			 * This is for the boot CPU - we use compile-time
			 * static initialisation because per-cpu memory isn't
			 * ready yet and because the memory allocators are not
			 * initialised either.
			 */
			boot_done = 1;
			base = &boot_tvec_bases;
		}
		spin_lock_init(&base->lock);
		tvec_base_done[cpu] = 1;
		base->cpu = cpu;
	} else {
		base = per_cpu(tvec_bases, cpu);
	}


	for (j = 0; j < TVN_SIZE; j++) {
		INIT_LIST_HEAD(base->tv5.vec + j);
		INIT_LIST_HEAD(base->tv4.vec + j);
		INIT_LIST_HEAD(base->tv3.vec + j);
		INIT_LIST_HEAD(base->tv2.vec + j);
	}
	for (j = 0; j < TVR_SIZE; j++)
		INIT_LIST_HEAD(base->tv1.vec + j);

	base->timer_jiffies = jiffies;
	base->next_timer = base->timer_jiffies;
	base->active_timers = 0;
	base->all_timers = 0;
	return 0;
}


static int timer_cpu_notify(struct notifier_block *self,
				unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	int err;

	switch(action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		err = init_timers_cpu(cpu);
		if (err < 0)
			return notifier_from_errno(err);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		migrate_timers(cpu);
		break;
#endif
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block timers_nb = {
	.notifier_call	= timer_cpu_notify,
};


void __init init_timers(void)
{
	int err;


	err = timer_cpu_notify(&timers_nb, (unsigned long)CPU_UP_PREPARE,
			       (void *)(long)smp_processor_id());

	register_cpu_notifier(&timers_nb);
	/*注册软中断，软中断在tick中断中都会执行*/
	open_softirq(TIMER_SOFTIRQ, run_timer_softirq);
}

/*
 * This function runs timers and the timer-tq in bottom half context.
 */
static void run_timer_softirq(struct softirq_action *h)
{
	struct tvec_base *base = __this_cpu_read(tvec_bases);

	/*先不关系*/
	hrtimer_run_pending();

	/*
	 *如过jiffies大于等于base->timer_jiffies,表示有timer到期
	 *base->timer_jiffies可以理解完上次上次处理完之后的jiffies
	 *有jiffies小于base->timer_jiffies的情况吗
	 */
	if (time_after_eq(jiffies, base->timer_jiffies))
		__run_timers(base);
}


/**
 * __run_timers - run all expired timers (if any) on this CPU.
 * @base: the timer vector to be processed.
 *
 * This function cascades all vectors and executes all expired timer
 * vectors.
 */
static inline void __run_timers(struct tvec_base *base)
{
	struct timer_list *timer;

	spin_lock_irq(&base->lock);
	if (catchup_timer_jiffies(base)) {
		/*返回true表示没有timer需要处理*/
		spin_unlock_irq(&base->lock);
		return;
	}
	/*如果系统繁忙错过了timer处理，jiffies会大于base->timer_jiffies
	 *正常应该相等
	 */
	while (time_after_eq(jiffies, base->timer_jiffies)) {
		struct list_head work_list;
		struct list_head *head = &work_list;

		/*
		 *tv1.vec是不是有到期的timer
		 *base->timer_jiffies为上次处理完到期timer的jiffie值。
		 *用来记录上次处理有到期timer时的jiffies值，
		 */
		int index = base->timer_jiffies & TVR_MASK;

		/*
		 * Cascade timers:
		 */
		/*index为0表示tv1.vec没有到期的timer，将tv2等的timer进行移动*/ 
		if (!index && 
			/*index为0，tv1没有到期的timer，将tv2的移到tv1
			 *tv1.vec没有到期表示jiffies已经增加了256个，所以把
			 *tv2.vec移到tv1.vec
			 *同理如果tv2.vec的已经处理完了，则把tv3.vec移到到tv1.vec
			 */
			(!cascade(base, &base->tv2, INDEX(0))) &&
				(!cascade(base, &base->tv3, INDEX(1))) &&
					!cascade(base, &base->tv4, INDEX(2)))
			cascade(base, &base->tv5, INDEX(3));

		/*
		 *base->timer_jiffies自加1表示处理了tv1.vec + index
		 *对应元素中的所有timer，
		 */
		++base->timer_jiffies;
		
		/*将base->tv1.vec + index的链表头替换为head*/
		list_replace_init(base->tv1.vec + index, head);

		/*处理对应tv1.vec中对应元素中到期时间相同的timer*/
		while (!list_empty(head)) {
			void (*fn)(unsigned long);
			unsigned long data;
			bool irqsafe;

			timer = list_first_entry(head, struct timer_list,entry);
			fn = timer->function;
			data = timer->data;
			irqsafe = tbase_get_irqsafe(timer->base);

			timer_stats_account_timer(timer);

			base->running_timer = timer;
			/*讲该timer中链表中删除，同时清楚pendding状态*/
			detach_expired_timer(timer, base);

			if (irqsafe) {
				spin_unlock(&base->lock);
				call_timer_fn(timer, fn, data);/*调用到期处理函数*/
				spin_lock(&base->lock);
			} else {
				spin_unlock_irq(&base->lock);
				call_timer_fn(timer, fn, data);/*调用到期处理函数*/
				spin_lock_irq(&base->lock);
			}
		}
	}
	base->running_timer = NULL;
	spin_unlock_irq(&base->lock);
}

static int cascade(struct tvec_base *base, struct tvec *tv, int index)
{
	/* cascade all the timers from tv up one level */
	struct timer_list *timer, *tmp;
	struct list_head tv_list;

	list_replace_init(tv->vec + index, &tv_list);

	/*
	 * We are removing _all_ timers from the list, so we
	 * don't have to detach them individually.
	 */
	list_for_each_entry_safe(timer, tmp, &tv_list, entry) {
		BUG_ON(tbase_get_base(timer->base) != base);
		/* No accounting, while moving them */
		/*
		 * 由于base->timer_jiffies不断增加，所以从cascade调用
		 * __internal_add_timer函数，只会往tv1.vec中添加
		 * 只有从add_timer等添加timer的函数中才会王tv2，tv3等中
		 * 添加timer
		 */
		__internal_add_timer(base, timer);
	}

	return index;
}


static inline void
detach_expired_timer(struct timer_list *timer, struct tvec_base *base)
{
	detach_timer(timer, true);
	if (!tbase_get_deferrable(timer->base))
		base->active_timers--;
	base->all_timers--;
	(void)catchup_timer_jiffies(base);
}



