## 各种上下文

### preempt_count

各种上下问的判断都是通过进程中的preempt_count变量进行判断的,如下：

```c
/*
 * Are we doing bottom half or hardware interrupt processing?
 * Are we in a softirq context? Interrupt context?
 * in_softirq - Are we currently processing softirq or have bh disabled?
 * in_serving_softirq - Are we currently processing softirq?
 */
#define in_irq()				(hardirq_count())	/*硬中断上下文*/
#define in_softirq()			(softirq_count())	/*软中断上下文*/
#define in_interrupt()			(irq_count())		/*中断上下文，包括硬中断和软中断*/
#define in_serving_softirq()	 (softirq_count() & SOFTIRQ_OFFSET)

/*
 * Are we in NMI context?
 */
#define in_nmi()	(preempt_count() & NMI_MASK)	/*NMI上下文*/
```

如果preempt_count对应bit不为0则表示在对应的上下文。下面是内核总对preempt_count不同bit的说明

```
/*
 * We put the hardirq and softirq counter into the preemption
 * counter. The bitmask has the following meaning:
 *
 * - bits 0-7 are the preemption count (max preemption depth: 256)
 * - bits 8-15 are the softirq count (max # of softirqs: 256)
 *
 * The hardirq count could in theory be the same as the number of
 * interrupts in the system, but we run all interrupt handlers with
 * interrupts disabled, so we cannot have nesting interrupts. Though
 * there are a few palaeontologic drivers which reenable interrupts in
 * the handler, so we need more than one bit here.
 *
 * PREEMPT_MASK:	0x000000ff
 * SOFTIRQ_MASK:	0x0000ff00
 * HARDIRQ_MASK:	0x000f0000
 *     NMI_MASK:	0x00100000
 * PREEMPT_ACTIVE:	0x00200000
 */
```

### hardirq上下文

中断处理的入口函数会调用到__handle_domain_irq

```c
int __handle_domain_irq(struct irq_domain *domain, unsigned int hwirq,
			bool lookup, struct pt_regs *regs)
{
	irq_enter();
#ifdef CONFIG_IRQ_DOMAIN
	if (lookup)
		irq = irq_find_mapping(domain, hwirq);
#endif

	/*
	 * Some hardware gives randomly wrong interrupts.  Rather
	 * than crashing, do something sensible.
	 */
	if (unlikely(!irq || irq >= nr_irqs)) {
		ack_bad_irq(irq);
		ret = -EINVAL;
	} else {
		generic_handle_irq(irq);
	}

	irq_exit();
	set_irq_regs(old_regs);
	return ret;
}
```

```c
/*
 * Enter an interrupt context.
 */
void irq_enter(void)
{
	rcu_irq_enter();
	......
	__irq_enter();
}
```

```c
#define __irq_enter()					\
	do {						\
		account_irq_enter_time(current);	\
		preempt_count_add(HARDIRQ_OFFSET);	\
		trace_hardirq_enter();			\
	} while (0)
```

```c
static __always_inline int *preempt_count_ptr(void)
{
	return &current_thread_info()->preempt_count;
}
static __always_inline void __preempt_count_add(int val)
{
	*preempt_count_ptr() += val;
}

#define preempt_count_add(val)	__preempt_count_add(val)
#define HARDIRQ_OFFSET	(1UL << HARDIRQ_SHIFT)
```

最终就是给进程的preempt_count加上(1UL << HARDIRQ_SHIFT)。因此可以看打在进入硬件中断的时候就进入了硬中断上下文。

### softirq 上下文

在硬件处理完之后，会调用irq_exit

```c
/*
 * Exit an interrupt context. Process softirqs if needed and possible:
 */
void irq_exit(void)
{
	preempt_count_sub(HARDIRQ_OFFSET);
	if (!in_interrupt() && local_softirq_pending())
		invoke_softirq();
}
```

preempt_count_sub(HARDIRQ_OFFSET);给进程的preempt_count减去(1UL << HARDIRQ_SHIFT)，即推出硬件中断上下文。

```c
static inline void invoke_softirq(void)
{
	if (!force_irqthreads) {
#ifdef CONFIG_HAVE_IRQ_EXIT_ON_IRQ_STACK
		/*
		 * We can safely execute softirq on the current stack if
		 * it is the irq stack, because it should be near empty
		 * at this stage.
		 */
		__do_softirq();
#else
		/*
		 * Otherwise, irq_exit() is called on the task stack that can
		 * be potentially deep already. So call softirq in its own stack
		 * to prevent from any overrun.
		 */
		do_softirq_own_stack();
#endif
	} else {
		wakeup_softirqd();
	}
}
```

```c
asmlinkage __visible void __do_softirq(void)
{
	__local_bh_disable_ip(_RET_IP_, SOFTIRQ_OFFSET);
	软中断处理函数
	__local_bh_enable(SOFTIRQ_OFFSET);
}
```

```c
#define SOFTIRQ_OFFSET	(1UL << SOFTIRQ_SHIFT)
static __always_inline void __local_bh_disable_ip(unsigned long ip, unsigned int cnt)
{
	preempt_count_add(cnt);
	barrier();
}

static void __local_bh_enable(unsigned int cnt)
{
	preempt_count_sub(cnt);
}
```

从上面看到softirq上下问就是处理软中断的过程中，同时也可以看到在处理软中断的时候是**禁止下半部**处理的。而且在**调用软中断处理函数的时候中的是使能**的。
