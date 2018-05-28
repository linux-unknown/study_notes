# Linux低精度timer

linux低精度timer实现学习个分析
内核版本 linux 4.0
[TOC]

## timer使用

timer的简单使用
```c
struct timer_list *timer;
timer = kmalloc(siezof(*timer));
init_timer(&st->timer);
timer->expires = jiffies + 10 * HZ;	/*到期时间*/
timer->function = timer_handler;	/*到期的处理函数*/
timer->data = (unsigned long)dev;	/*到期处理函数传入的参数*/
add_timer(timer);
```

## add_timer分析过程

```c
void add_timer(struct timer_list *timer)
{
	mod_timer(timer, timer->expires);
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
 *     del_timer(timer); timer->expires = expires; add_timer(timer);
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
	/*用于对定时精度没有太高要求的tiemr*/
	expires = apply_slack(timer, expires);
    /*
     *mod_timer也可以直接调用，来重启已经到期或未到期的timer，见该函数注释
     *这里用判断如果timer没有被处理，并且到期时间就为现在，则不处理。
     */
	if (timer_pending(timer) && timer->expires == expires)
		return 1;
	return __mod_timer(timer, expires, false, TIMER_NOT_PINNED);
}
```

###  \__mod_timer

```c
static inline int
__mod_timer(struct timer_list *timer, unsigned long expires, bool pending_only, int pinned)
{
	struct tvec_base *base, *new_base;
	unsigned long flags;
	int ret = 0 , cpu;
    /*只关注timer是怎么注册的，略去了cpu hotplug等的处理*/
	......
	timer->expires = expires;
	internal_add_timer(base, timer);
    
	return ret;
}
```

###  internal_add_timer

```c
static void internal_add_timer(struct tvec_base *base, struct timer_list *timer)
{
	(void)catchup_timer_jiffies(base);					/****1****/
	__internal_add_timer(base, timer);					/****2****/
	/*
	 * Update base->active_timers and base->next_timer
	 */
	if (!tbase_get_deferrable(timer->base)) {			 /****3****/
		if (!base->active_timers++ ||
		    time_before(timer->expires, base->next_timer))
			base->next_timer = timer->expires;
	}
	base->all_timers++;
}

```

1.如果base->all_timers为0，则更新base->timer_jiffies = jiffies;同时返回true。

2.timer的注册工作是在__internal_add_timer(base, timer);进行

3.增加base->active_timers计数和base->all_timers计数

##  tvec_base

在分析`__internal_add_timer`之前先看下tvec_base定义

```c
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

static DEFINE_PER_CPU(struct tvec_base *, tvec_bases) = &boot_tvec_bases;
```

其中

```c
struct tvec {
	struct list_head vec[TVN_SIZE];
};
struct tvec_root {
	struct list_head vec[TVR_SIZE];
};


```

宏定义如下

```c
#define TVN_BITS (CONFIG_BASE_SMALL ? 4 : 6)
#define TVR_BITS (CONFIG_BASE_SMALL ? 6 : 8)
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)
#define MAX_TVAL ((unsigned long)((1ULL << (TVR_BITS + 4*TVN_BITS)) - 1))
```

从上面定义看tvec_bases是每个cpu都有一个的。




### \__internal_add_timer

```c



static void
__internal_add_timer(struct tvec_base *base, struct timer_list *timer)
{
	unsigned long expires = timer->expires;
	unsigned long idx = expires - base->timer_jiffies;	  /****1****/
	struct list_head *vec;

	if (idx < TVR_SIZE) {								/****2****/
		int i = expires & TVR_MASK;
		vec = base->tv1.vec + i;
	} else if (idx < 1 << (TVR_BITS + TVN_BITS)) {		  /****3****/
		int i = (expires >> TVR_BITS) & TVN_MASK;
		vec = base->tv2.vec + i;
	} else if (idx < 1 << (TVR_BITS + 2 * TVN_BITS)) {
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
	
	list_add_tail(&timer->entry, vec);
}

```

1. idx = expires - base->timer_jiffies，在处理timer的时候base->timer_jiffies会加1，base->timer_jiffies记录上次处理完timer之后jiffies的值。在系统没有错过处理timer的情况下，base->timer_jiffies和jiffies是相等的。

   base->timer_jiffies在base->all_timers为0时，赋值为当前的jiffies，在tvec_base初始化的的时候，也赋值为jiffies。

2. TVR_SIZE一般配置为256，i = expires & TVR_MASK;表示到期需要经过jiffies周期的个数。

   如果差值小于256则将该timer挂到tv1.vec 对应的索引上。

   所以tv1.vec的每一个元素的timer相差一个jiffies时间间隔。tv1.vec的每个元素作为链表头，链起来的timer他们的expires是一样的，链表元素的个数没有限制。

3. idx需要小于2^14才会分配到tv2.sec，而且从i = (expires >> TVR_BITS) & TVN_MASK看tv2.vec每个元素之间相差的时间为256个jiffies。
   tv2.vec每个元素作为链表头链接起来的每个timer的expires右移TVR_BITS位后是一样的。

4. 一次类推，tve3，tve4.tve5。

下面一张图描述tiemr在tvec_base的情况

![timer挂载状态](timer.bmp)

   ## 低精度timer的处理过程

## init_timers

系统启动的时候，start_kernel会调用init_timers

```c
void __init init_timers(void)
{
	int err;
	err = timer_cpu_notify(&timers_nb, (unsigned long)CPU_UP_PREPARE,
			       (void *)(long)smp_processor_id());
	register_cpu_notifier(&timers_nb);
	/*注册软中断，软中断在tick中断中都会执行*/
	open_softirq(TIMER_SOFTIRQ, run_timer_softirq);
}
```

### timer_cpu_notify

会做一些初始化工作

```c
base = per_cpu(tvec_bases, cpu);	
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
```

## timer处理过程

timer的处理是在软中断中

update_process_times-->run_local_timers-->raise_softirq(TIMER_SOFTIRQ),然后会调用run_timer_softirq函数处理timer

update_process_times在每次tick中断中都会调用，因此每次tick中断都会调用raise_softirq(TIMER_SOFTIRQ)

### run_timer_softirq

```c
static void run_timer_softirq(struct softirq_action *h)
{
	struct tvec_base *base = __this_cpu_read(tvec_bases);

	/*
	 *如过jiffies大于等于base->timer_jiffies,表示有timer到期,
	 *如果大于则表示有timer在上次没有处理完。
	 *base->timer_jiffies可以理解完上次上次处理完之后的jiffies
	 */
	if (time_after_eq(jiffies, base->timer_jiffies))
		__run_timers(base);
}
```

### \__run_timers

```c
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
		 *用来记录上次处理到期timer时的jiffies值，
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
		 *对应元素中的所有timer
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
```



```c
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
		/* No accounting, while moving them */
		/*
		 * 由于base->timer_jiffies不断增加，所以从cascade调用
		 * __internal_add_timer函数，只会往tv1.vec中添加,因此
		 * timer->expires  - base->timer_jiffies一定小于256.
		 * 只有从add_timer等添加timer的函数中才会王tv2，tv3等中
		 * 添加timer
		 */
		__internal_add_timer(base, timer);
	}
	return index;
}
```

从上面可以看到，每一次tick中断只会处理tv1.vec[i]中的一个链表中的timer，随着jiffies的增加，i会增大。当i大于255的时候，inidex等于0。会把tv2.vec[x]中的链表的timer，挂到tv1.vec的元素中。由于在添加tv2.vec的时候，是不关心低8bit的，tv1.vec全部处理完，刚好经过来 255个jiffies即base->timer_jiffies增加了255，这个时候再用tv2.vec中的timer的低8位判断需要挂到tv1.vec的那个元素中。同理。tv2.vec中的所有timer的处理完，会把tv3.vec的挂到tv1.vec中，