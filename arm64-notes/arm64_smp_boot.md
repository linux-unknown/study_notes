# arm64 smp boot

[TOC]

## Secondary CPU

在linux smp启动的过程中，一般会先由一个CPU boot起来，该CPU称作boot cpu。boot cpu会启动其他的cpu，其他cpu统称为secondary cpu

## bootloader阶段secondary cpu启动

secondary cpu启动有几种方法：

1. secondary cpu启动之后，会检测一个标志，如果该标志不符合条件则一直循环检查
2. secondary cpu一开始不上电，在boot cpu启动secondary cpu的时候进行上电

### boot-wrapper-aarch64 secondary cpu处理

boot-wrapper-aarch64即作为boot loader，同时在el3中实现了psci功能

```assembly
	mrs	x0, mpidr_el1
	ldr	x1, =MPIDR_ID_BITS
	and	x0, x0, x1
	bl	find_logical_id
	cbnz	x0, spin	/*如果x0不等于0，跳转到spin*/
/*
 * Poll the release table, waiting for a valid address to appear.
 * When a valid address appears, branch to it.
 */
spin:
	mrs	x0, mpidr_el1
	ldr	x1, =MPIDR_ID_BITS
	and	x0, x0, x1
	bl	find_logical_id
	cmp	x0, #-1		/*获取cpu logic id失败，无线循环。正常情况是不会跳转到spin_dead*/
	b.eq	spin_dead

	adr	x1, branch_table
	mov	x3, #ADDR_INVALID
	add	x1, x1, x0, lsl #3   /*x0为core id*/
	/**
	 * 如果不是core 0则会在这里一直循环，在core 0起来后，会在branch_table对应core的地址处写入
	 * core x需要执行的地址
	 */
1:	wfe
	ldr	x2, [x1]
	/*
	 * 比较对应core logic id中的branch_table的入口地址是否-1，core 0会初始化对应的入口地址为	
	 * start_cpu0，secondary core为secondary_entry
	 */
	cmp	x2, x3	/*x3 = ADDR_INVALID*/
	b.eq	1b

	ldr	x0, =SCTLR_EL2_RESET   /*(3 << 28 | 3 << 22 | 1 << 18 | 1 << 16 | 1 << 11 | 3 << 4)*/
	msr	sctlr_el2, x0

	mov	x3, #SPSR_KERNEL	/*异常返回的设置，返回到EL2h*/
	adr	x4, el2_trampoline 	/*el2_trampoline  //异常返回时PC的地址*/
	mov	x0, x2				/*core x在branch_table中对应的跳转地址*/
	
	/*
	*msr	elr_el3, x4 */
	*msr	spsr_el3, x3 
	*eret
	*/
	drop_el	x3, x4 /*drop到EL2*/
	
spin_dead:
	wfe
	b	spin_dead
```

```assembly
el2_trampoline:
    /*
     * x0为从branch_table读出的对应core的跳转地址，对于core 0为start_cpu0地址，
     * 对于其他core 则为kernel启动后写入的地址，为secondary_boot
     */
	mov	x15, x0  
	bl	flush_caches
	br	x15	/*core 0为start_cpu0，其他core为secondary_boot*/
```

## boot cpu 启动

```assembly
start_cpu0:
	bl	ns_init_system /*non-secure初始化，主要是初始化串口和CLCD，不用关心*/
	/*dtb和kernel都是model.lds中定义的*/
	ldr	x0, =dtb
	b	kernel /*跳转到kernel*/
```

## kernel smp 启动过程

```c
asmlinkage __visible void __init start_kernel(void)
{
	char *command_line;
	char *after_dashes;

	smp_setup_processor_id();

	boot_cpu_init();
	pr_notice("%s", linux_banner);
	setup_arch(&command_line);
	mm_init_cpumask(&init_mm);
	setup_command_line(command_line);
	setup_nr_cpu_ids();
	setup_per_cpu_areas();
	smp_prepare_boot_cpu();	/* arch-specific boot-cpu hooks */

	/*
	 * Set up the scheduler prior starting any interrupts (such as the
	 * timer interrupt). Full topology setup happens at smp_init()
	 * time - but meanwhile we still have a functioning scheduler.
	 */
	sched_init();
	/*
	 * Disable preemption - early bootup scheduling is extremely
	 * fragile until we cpu_idle() for the first time.
	 */
	preempt_disable();
	setup_per_cpu_pageset();
	rest_init();
}
```

### smp_setup_processor_id

```c
void __init smp_setup_processor_id(void)
{
	u64 mpidr = read_cpuid_mpidr() & MPIDR_HWID_BITMASK;
	cpu_logical_map(0) = mpidr;

	/*
	 * clear __my_cpu_offset on boot CPU to avoid hang caused by
	 * using percpu variable early, for example, lockdep will
	 * access percpu variable inside lock_release
	 */
	set_my_cpu_offset(0);
	pr_info("Booting Linux on physical CPU 0x%lx\n", (unsigned long)mpidr);
}
```

该函数只有boot cpu会之前，一般boot cpu的id为0。

### boot_cpu_init

```c
static void __init boot_cpu_init(void)
{
	int cpu = smp_processor_id();
	/* Mark the boot cpu "present", "online" etc for SMP and UP case */
	set_cpu_online(cpu, true);
	set_cpu_active(cpu, true);
	set_cpu_present(cpu, true);
	set_cpu_possible(cpu, true);
}

```

boot cpu的初始化，会将对应cpu的bit置位

### setup_arch

```c
void __init setup_arch(char **cmdline_p)
{
	psci_init();
	cpu_read_bootcpu_ops();
#ifdef CONFIG_SMP
	smp_init_cpus();
	/*不知道干什么用的*/
	smp_build_mpidr_hash();
#endif
}
```

secondary cpu最终的启动是通过psci来完成的。

#### psci_init

```c
static const struct of_device_id psci_of_match[] __initconst = {
	{ .compatible = "arm,psci",	.data = psci_0_1_init},
	{ .compatible = "arm,psci-0.2",	.data = psci_0_2_init},
	{},
};

int __init psci_init(void)
{
	struct device_node *np;
	const struct of_device_id *matched_np;
	psci_initcall_t init_fn;

	/*qemu为"arm,psci-0.2"*/
	np = of_find_matching_node_and_match(NULL, psci_of_match, &matched_np);

	init_fn = (psci_initcall_t)matched_np->data;
	/*调用psci_0_2_init函数*/
	return init_fn(np);
}
```

#####  psci_0_2_init

```c
/*
 * PSCI Function IDs for v0.2+ are well defined so use
 * standard values.
 */

/**
 * DEN0022C_Power_State_Coordination_Interface.pdf有ID描述
 * 可以在arm官网下载
 */

static struct psci_operations psci_ops;

static int __init psci_0_2_init(struct device_node *np)
{
	int err, ver;
	err = get_set_conduit_method(np);
	ver = psci_get_version();
	if (ver == PSCI_RET_NOT_SUPPORTED) {
		/*错误处理*/
    } else {
		pr_info("PSCIv%d.%d detected in firmware.\n", PSCI_VERSION_MAJOR(ver),
				PSCI_VERSION_MINOR(ver));

		if (PSCI_VERSION_MAJOR(ver) == 0 && PSCI_VERSION_MINOR(ver) < 2) {
			/*错误处理*/
		}
	}
	pr_info("Using standard PSCI v0.2 function IDs\n");
	psci_function_id[PSCI_FN_CPU_SUSPEND] = PSCI_0_2_FN64_CPU_SUSPEND;
	psci_ops.cpu_suspend = psci_cpu_suspend;

	psci_function_id[PSCI_FN_CPU_OFF] = PSCI_0_2_FN_CPU_OFF;
	psci_ops.cpu_off = psci_cpu_off;

	psci_function_id[PSCI_FN_CPU_ON] = PSCI_0_2_FN64_CPU_ON;
	psci_ops.cpu_on = psci_cpu_on;

	arm_pm_restart = psci_sys_reset;
	pm_power_off = psci_sys_poweroff;

out_put_node:
	of_node_put(np);
	return err;
}
```

```c
enum psci_function {
	PSCI_FN_CPU_SUSPEND,
	PSCI_FN_CPU_ON,
	PSCI_FN_CPU_OFF,
	PSCI_FN_MIGRATE,
	PSCI_FN_AFFINITY_INFO,
	PSCI_FN_MIGRATE_INFO_TYPE,
	PSCI_FN_MAX,
};

/* PSCI v0.2 interface DEN0022C_Power_State_Coordination_Interface.pdf有IDs的描述*/
#define PSCI_0_2_FN_BASE			0x84000000
#define PSCI_0_2_FN(n)				(PSCI_0_2_FN_BASE + (n))
#define PSCI_0_2_64BIT				0x40000000
#define PSCI_0_2_FN64_BASE			(PSCI_0_2_FN_BASE + PSCI_0_2_64BIT)
#define PSCI_0_2_FN64(n)			(PSCI_0_2_FN64_BASE + (n))

#define PSCI_0_2_FN64_CPU_SUSPEND		PSCI_0_2_FN64(1)
#define PSCI_0_2_FN64_CPU_ON			PSCI_0_2_FN64(3)
#define PSCI_0_2_FN64_AFFINITY_INFO		PSCI_0_2_FN64(4)
#define PSCI_0_2_FN64_MIGRATE			PSCI_0_2_FN64(5)
#define PSCI_0_2_FN64_MIGRATE_INFO_UP_CPU	PSCI_0_2_FN64(7)

```

##### get_set_conduit_method

```c
static int get_set_conduit_method(struct device_node *np)
{
	const char *method;

	pr_info("probing for conduit method from DT.\n");

	if (of_property_read_string(np, "method", &method)) {
	}

	/*qemu为hvc, 一般是smc我们boot wrapper中为smc*/
	if (!strcmp("hvc", method)) {
		invoke_psci_fn = __invoke_psci_fn_hvc;
	} else if (!strcmp("smc", method)) {
		invoke_psci_fn = __invoke_psci_fn_smc;
	} else {
		pr_warn("invalid \"method\" property: %s\n", method);
		return -EINVAL;
	}
	return 0;
}
```

##### psci_cpu_on

```c
static int psci_cpu_on(unsigned long cpuid, unsigned long entry_point)
{
	int err;
	u32 fn;

	/*fn(function number)为PSCI_0_2_FN64_CPU_ON*/
	fn = psci_function_id[PSCI_FN_CPU_ON];
	/**
	 * 调用__invoke_psci_fn_hvc或__invoke_psci_fn_smc
	 * 则会进入到el2，或el3，一般是el3中会进行操作
	 */
	err = invoke_psci_fn(fn, cpuid, entry_point, 0);
	return psci_to_linux_errno(err);
}
```

##### invoke_psci_fn

```assembly
/* int __invoke_psci_fn_hvc(u64 function_id, u64 arg0, u64 arg1, u64 arg2) */
ENTRY(__invoke_psci_fn_hvc)
	hvc	#0
	ret
ENDPROC(__invoke_psci_fn_hvc)

/* int __invoke_psci_fn_smc(u64 function_id, u64 arg0, u64 arg1, u64 arg2) */
ENTRY(__invoke_psci_fn_smc)
	smc	#0
	ret
ENDPROC(__invoke_psci_fn_smc)
```

#### smp_init_cpus

```c
void __init smp_init_cpus(void)
{
	struct device_node *dn = NULL;
	unsigned int i, cpu = 1;
	bool bootcpu_valid = false;

	/*查找节点中有device_type属性，并且属性值为cpu的节点*/
	while ((dn = of_find_node_by_type(dn, "cpu"))) {
		const u32 *cell;
		u64 hwid;
		/*
		 * A cpu node with missing "reg" property is
		 * considered invalid to build a cpu_logical_map
		 * entry.
		 */
		/**
		 *reg表示cpu的id
		 */
		cell = of_get_property(dn, "reg", NULL);
		if (!cell) {
			pr_err("%s: missing reg property\n", dn->full_name);
			goto next;
		}
		/*获取reg属性的值*/
		hwid = of_read_number(cell, of_n_addr_cells(dn));

		/*
		 * Non affinity bits must be set to 0 in the DT
		 */
		if (hwid & ~MPIDR_HWID_BITMASK) {
			pr_err("%s: invalid reg property\n", dn->full_name);
			goto next;
		}

		/*
		 * Duplicate MPIDRs are a recipe for disaster. Scan
		 * all initialized entries and check for
		 * duplicates. If any is found just ignore the cpu.
		 * cpu_logical_map was initialized to INVALID_HWID to
		 * avoid matching valid MPIDR values.
		 */
		/**
		 *检查是否有重复的
		 */
		for (i = 1; (i < cpu) && (i < NR_CPUS); i++) {
			if (cpu_logical_map(i) == hwid) {
				pr_err("%s: duplicate cpu reg properties in the DT\n",
					dn->full_name);
				goto next;
			}
		}

		/*
		 * The numbering scheme requires that the boot CPU
		 * must be assigned logical id 0. Record it so that
		 * the logical map built from DT is validated and can
		 * be used.
		 */
		/**
		 *如果是boot cpu则跳过
		 */
		if (hwid == cpu_logical_map(0)) {
			if (bootcpu_valid) {
				pr_err("%s: duplicate boot cpu reg property in DT\n",
					dn->full_name);
				goto next;
			}

			bootcpu_valid = true;
			/*
			 * cpu_logical_map has already been
			 * initialized and the boot cpu doesn't need
			 * the enable-method so continue without
			 * incrementing cpu.
			 */
			continue;
		}

		if (cpu >= NR_CPUS)
			goto next;

		/*给cpu_ops[cpu]赋值
		 *我们分析的为cpu_psci_ops
		 */
		if (cpu_read_ops(dn, cpu) != 0)
			goto next;

		/*arm64 cpu_init直接返回*/
		if (cpu_ops[cpu]->cpu_init(dn, cpu))
			goto next;

		pr_debug("cpu logical map 0x%llx\n", hwid);
		/*标志该cpu已经scan过了*/
		cpu_logical_map(cpu) = hwid;
next:
		cpu++;
	}

	/* sanity check */
	if (cpu > NR_CPUS)
		pr_warning("no. of cores (%d) greater than configured maximum of %d - clipping\n",
			   cpu, NR_CPUS);

	if (!bootcpu_valid) {
		pr_err("DT missing boot CPU MPIDR, not enabling secondaries\n");
		return;
	}

	/*
	 * All the cpus that made it to the cpu_logical_map have been
	 * validated so set them as possible cpus.
	 */
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_logical_map(i) != INVALID_HWID)
			set_cpu_possible(i, true);
}
```

##### cpu_read_ops

```c
/*
 * Read a cpu's enable method from the device tree and record it in cpu_ops.
 */
int __init cpu_read_ops(struct device_node *dn, int cpu)
{
	const char *enable_method = of_get_property(dn, "enable-method", NULL);
    if (!enable_method) {
		/*
		 * The boot CPU may not have an enable method (e.g. when
		 * spin-table is used for secondaries). Don't warn spuriously.
		 */
		if (cpu != 0)
			pr_err("%s: missing enable-method property\n", dn->full_name);
		return -ENOENT;
	}
    /*给cpu_ops进行赋值为cpu_psci_ops*/
	cpu_ops[cpu] = cpu_get_ops(enable_method);

	return 0;
}
```

##### cpu_get_ops

```c
static const struct cpu_operations * __init cpu_get_ops(const char *name)
{
	const struct cpu_operations **ops = supported_cpu_ops;
	while (*ops) {
		if (!strcmp(name, (*ops)->name))
			return *ops;
		ops++;
	}

	return NULL;
}
```

```c
static const struct cpu_operations *supported_cpu_ops[] __initconst = {
#ifdef CONFIG_SMP
	&smp_spin_table_ops,
#endif
    /*qemu使用的是psci*/
	&cpu_psci_ops,
	NULL,
};

```
##### cpu_psci_ops

```c
const struct cpu_operations cpu_psci_ops = {
	.name		= "psci",
#ifdef CONFIG_CPU_IDLE
	.cpu_init_idle	= cpu_psci_cpu_init_idle,
	.cpu_suspend	= cpu_psci_cpu_suspend,
#endif
#ifdef CONFIG_SMP
	.cpu_init	= cpu_psci_cpu_init,
	.cpu_prepare	= cpu_psci_cpu_prepare,
	.cpu_boot	= cpu_psci_cpu_boot,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable	= cpu_psci_cpu_disable,
	.cpu_die	= cpu_psci_cpu_die,
	.cpu_kill	= cpu_psci_cpu_kill,
#endif
#endif
};
```
##### cpu_psci_cpu_boot

```c
static int cpu_psci_cpu_boot(unsigned int cpu)
{
	/* 调用psci_cpu_on
	 * secondary_entry，汇编代码，
	 * secondary cpu内核的入口地址是secondary_entry
	 * psci_ops是一个static的全局变量
	 */
	int err = psci_ops.cpu_on(cpu_logical_map(cpu), __pa(secondary_entry));
	if (err)
		pr_err("failed to boot CPU%d (%d)\n", cpu, err);

	return err;
}
```

### reset_init

```c
static noinline void __init_refok rest_init(void)
{
	int pid;

	rcu_scheduler_starting();
	/*
	 * We need to spawn init first so that it obtains pid 1, however
	 * the init task will end up wanting to create kthreads, which, if
	 * we schedule it before we create kthreadd, will OOPS.
	 */
	kernel_thread(kernel_init, NULL, CLONE_FS);
	numa_default_policy();
	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	rcu_read_lock();
	kthreadd_task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();
	complete(&kthreadd_done);

	/*
	 * The boot idle thread must execute schedule()
	 * at least once to get things moving:
	 */
	init_idle_bootup_task(current);
	schedule_preempt_disabled();
	/* Call into cpu_idle with preempt disabled */
	cpu_startup_entry(CPUHP_ONLINE);
}

```

#### kernel_init

```c
static int __ref kernel_init(void *unused)
{
	int ret;

	kernel_init_freeable();
	/* need to finish all async __init code before freeing the memory */
}
```

#### kernel_init_freeable

```c
static noinline void __init kernel_init_freeable(void)
{
	/*初始化cpu的拓扑结构*/
	smp_prepare_cpus(setup_max_cpus);
	/*调用使用early_initcall修饰的函数*/
	do_pre_smp_initcalls();
	smp_init();
	sched_init_smp();
	do_basic_setup();
}

```

#### smp_init

```c
/* Called by boot processor to activate the rest. */
void __init smp_init(void)
{
	unsigned int cpu;
	/*为每个cpu初始化idle线程*/
	idle_threads_init();

	/* FIXME: This should be done in userspace --RR */
	for_each_present_cpu(cpu) {
		if (num_online_cpus() >= setup_max_cpus)
			break;
		if (!cpu_online(cpu))
			cpu_up(cpu);
	}

	/* Any cleanup work */
	smp_announce();
	smp_cpus_done(setup_max_cpus);
}
```

#### cpu_up

```c
int cpu_up(unsigned int cpu)
{
	int err = 0;
	/*应该是和memory hotplug相关的*/
	err = try_online_node(cpu_to_node(cpu));
	/*持锁*/
	cpu_maps_update_begin();
	err = _cpu_up(cpu, 0);

out:
	/*释放锁*/
	cpu_maps_update_done();
	return err;
}
EXPORT_SYMBOL_GPL(cpu_up);
```

#### _cpu_up

```c
/* Requires cpu_add_remove_lock to be held */
static int _cpu_up(unsigned int cpu, int tasks_frozen)
{
	int ret, nr_calls = 0;
	void *hcpu = (void *)(long)cpu;
	/*tasks_frozen = 0*/
	unsigned long mod = tasks_frozen ? CPU_TASKS_FROZEN : 0; /*tasks_frozen=0*/
	struct task_struct *idle;

	cpu_hotplug_begin();

	idle = idle_thread_get(cpu);


	ret = smpboot_create_threads(cpu);

	/*调用回调函数*/
	ret = __cpu_notify(CPU_UP_PREPARE | mod, hcpu, -1, &nr_calls);

	/* Arch-specific enabling code. */
	ret = __cpu_up(cpu, idle);

	/* Wake the per cpu threads */
	smpboot_unpark_threads(cpu);

	/* Now call notifier in preparation. */
	cpu_notify(CPU_ONLINE | mod, hcpu);

out_notify:
	if (ret != 0)
		__cpu_notify(CPU_UP_CANCELED | mod, hcpu, nr_calls, NULL);
out:
	cpu_hotplug_done();

	return ret;
}

```

#### smpboot_create_threads

```c
int smpboot_create_threads(unsigned int cpu)
{
	struct smp_hotplug_thread *cur;
	int ret = 0;

	mutex_lock(&smpboot_threads_lock);
	/**
	 * 执行smpboot_register_percpu_thread函数，会向hotplug_threads链表上挂载元素
	 * 在这里重新执行，应该是之前执行的时候只有online cpu只有boot cpu，现在对每一个
	 * cpu进行注册
	 */
	list_for_each_entry(cur, &hotplug_threads, list) {
		ret = __smpboot_create_thread(cur, cpu);
	}
	mutex_unlock(&smpboot_threads_lock);
	return ret;
}

```

__smpboot_create_thread 会创建一个线程，该线程的执行函数为smpboot_thread_fn，线程的名称有执行smpboot_register_percpu_thread函数执行是传递的参数决定。

#### smpboot_thread_fn

smpboot_thread_fn会根据传递的不同参数，执行不同的动作,进而执行ht中不同的函数

```c
static int smpboot_thread_fn(void *data)
{
	struct smpboot_thread_data *td = data;
	struct smp_hotplug_thread *ht = td->ht;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		preempt_disable();
		if (kthread_should_stop()) {
			__set_current_state(TASK_RUNNING);
			preempt_enable();
			if (ht->cleanup)
				ht->cleanup(td->cpu, cpu_online(td->cpu));
			kfree(td);
			return 0;
		}

		if (kthread_should_park()) {
			__set_current_state(TASK_RUNNING);
			preempt_enable();
			if (ht->park && td->status == HP_THREAD_ACTIVE) {
				BUG_ON(td->cpu != smp_processor_id());
				ht->park(td->cpu);
				td->status = HP_THREAD_PARKED;
			}
			kthread_parkme();
			/* We might have been woken for stop */
			continue;
		}

		BUG_ON(td->cpu != smp_processor_id());

		/* Check for state change setup */
		switch (td->status) {
		case HP_THREAD_NONE:
			__set_current_state(TASK_RUNNING);
			preempt_enable();
			if (ht->setup)
				ht->setup(td->cpu);
			td->status = HP_THREAD_ACTIVE;
			continue;

		case HP_THREAD_PARKED:
			__set_current_state(TASK_RUNNING);
			preempt_enable();
			if (ht->unpark)
				ht->unpark(td->cpu);
			td->status = HP_THREAD_ACTIVE;
			continue;
		}

		if (!ht->thread_should_run(td->cpu)) {
			preempt_enable_no_resched();
			schedule();
		} else {
			__set_current_state(TASK_RUNNING);
			preempt_enable();
			ht->thread_fn(td->cpu);
		}
	}
}

```

#### cpu_stop_init

```c
static struct smp_hotplug_thread cpu_stop_threads = {
	.store			= &cpu_stopper_task,
	.thread_should_run	= cpu_stop_should_run,
	.thread_fn		= cpu_stopper_thread,
	.thread_comm		= "migration/%u",
	.create			= cpu_stop_create,
	.setup			= cpu_stop_unpark,
	.park			= cpu_stop_park,
	.pre_unpark		= cpu_stop_unpark,
	.selfparking		= true,
};

static int __init cpu_stop_init(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);

		spin_lock_init(&stopper->lock);
		INIT_LIST_HEAD(&stopper->works);
	}

	BUG_ON(smpboot_register_percpu_thread(&cpu_stop_threads));
	stop_machine_initialized = true;
	return 0;
}
early_initcall(cpu_stop_init);
```

#### __cpu_up

```c
int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	int ret;

	/*
	 * We need to tell the secondary core where to find its stack and the
	 * page tables. 设置secondary cpu的栈地址
	 */
	secondary_data.stack = task_stack_page(idle) + THREAD_START_SP;
	__flush_dcache_area(&secondary_data, sizeof(secondary_data));

	/*
	 * Now bring the CPU into our world.
	 */
	ret = boot_secondary(cpu, idle);
	if (ret == 0) {
		/*
		 * CPU was successfully started, wait for it to come online or
		 * time out.
		 */
		wait_for_completion_timeout(&cpu_running, msecs_to_jiffies(1000));
		}
	} else {
		pr_err("CPU%u: failed to boot: %d\n", cpu, ret);
	}

	secondary_data.stack = NULL;

	return ret;
}

```

#### boot_secondary

```c
static int boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	if (cpu_ops[cpu]->cpu_boot)
		/*调用cpu_psci_cpu_boot*/
		return cpu_ops[cpu]->cpu_boot(cpu);

	return -EOPNOTSUPP;
}
```

cou_ops为cpu_psci_ops，cpu_booot函数为cpu_psci_cpu_boot。cpu_psci_cpu_boot函数需要传递cpu id和secondary cpu入口地址的物理地址

```
psci_ops.cpu_on(cpu_logical_map(cpu), __pa(secondary_entry));
```

psci_cpu_on-->invoke_psci_fn,invoke_psci_fn函数会跳转到el3或这el2，退跳转到对应的异常向量表中，对于boot wrapper是psci_call64

#### psci_call64

```assembly
psci_call64:
	/*在执行smc命令的时候，x0=smc functions id，x1为cpu id， x2为跳转地址*/
	ldr	x7, =PSCI_CPU_OFF
	cmp	x0, x7
	b.eq	psci_cpu_off

	ldr	x7, =PSCI_CPU_ON /*我们只分析PSCI_CPU_ON*/
	cmp	x0, x7
	b.eq	psci_cpu_on

	mov	x0, PSCI_RET_NOT_IMPL
	eret
```

#### psci_cpu_on

```assembly
*
 * x1 - target cpu
 * x2 - address
 */
psci_cpu_on:
	/*保存x30的值，x30保存有kernel调用__invoke_psci_fn_smc的返回地址*/
	mov	x15, x30
	mov	x14, x2
	mov	x0, x1

	bl	find_logical_id
	cmp	x0, #-1
	b.eq	1f

	adr	x3, branch_table
	add	x3, x3, x0, lsl #3 /*x0为cpu的ID*/

	ldr	x4, =ADDR_INVALID

	ldxr	x5, [x3]
	cmp	x4, x5			/*初始都被初始化为-1*/
	b.ne	1f			/*如果不等于则表示已经写入了*/

	/*
	*把x14即secondary_entry的物理地址写入branch_table对应的core id中
	*其他的cpu core在等待event事件，等到后会检查branch_table中对应core id的跳转地址是否为-1
	*如果不是-1，字执行和core 0一样的动作，陷入EL2然后执行el2_trampoline，
	*/
	stxr	w4, x14, [x3] 
	cbnz	w4, 1f

	dsb	ishst
	sev					/*发送event唤醒其他cpu core*/

	mov	x0, #PSCI_RET_SUCCESS
	mov	x30, x15
	eret	/*返回异常*/

1:	mov	x0, #PSCI_RET_DENIED
	mov	x30, x15
	/*elr_el3保存有调用smc的下一条之前的地址，spsr_el3保存有调用smc之前的PSTATE状态*/
	eret
```

#### secondary cpu boot

在boot  wrapper中，secondary cpu会一直循环读取branh_table的值，如果不为ADDR_INVALID则跳转到该地址执行。

```assembly
adr	x1, branch_table
	mov	x3, #ADDR_INVALID
	add	x1, x1, x0, lsl #3   /*x0为core id*/
	/**
	 * 如果不是core 0则会在这里一直循环，在core 0起来后，会在branch_table对应core的地址处写入
	 * core x需要执行的地址
	 */
1:	wfe
	ldr	x2, [x1]
	/*
	 * 比较对应core logic id中的branch_table的入口地址是否-1，core 0会初始化对应的入口地址为	
	 * start_cpu0，secondary core为secondary_entry
	 */
	cmp	x2, x3	/*x3 = ADDR_INVALID*/
	b.eq	1b	/*如果不等于ADDR_INVALID，则继续执行，跳转到branch_table中对应的地址*/
```

#### secondary_entry

```assembly
ENTRY(secondary_entry)
	bl	el2_setup			// Drop to EL1
	bl	__calc_phys_offset		// x24=PHYS_OFFSET, x28=PHYS_OFFSET-PAGE_OFFSET
	bl	set_cpu_boot_mode_flag
	b	secondary_startup
ENDPROC(secondary_entry)
```

#### secondary_startup

```assembly
ENTRY(secondary_startup)
	/*
	 * Common entry point for secondary CPUs.
	 */
	mrs	x22, midr_el1			// x22=cpuid
	mov	x0, x22
	bl	lookup_processor_type

	/*x23 为cpu_table的指针*/
	mov	x23, x0				// x23=current cpu_table
	cbz	x23, __error_p			// invalid processor (x23=0)?

	/*x28为物理地址和虚拟地址的偏移*/
	pgtbl	x25, x26, x28			// x25=TTBR0, x26=TTBR1
	ldr	x12, [x23, #CPU_INFO_SETUP]
	add	x12, x12, x28			// __virt_to_phys
	/*调用__cpu_setup*/
	blr	x12				// initialise processor

	ldr	x21, =secondary_data
	ldr	x27, =__secondary_switched	// address to jump to after enabling the MMU
	b	__enable_mmu
ENDPROC(secondary_startup)
```

#### __secondary_switched

```assembly
ENTRY(__secondary_switched)
	ldr	x0, [x21]			// get secondary_data.stack
	mov	sp, x0
	mov	x29, #0
	b	secondary_start_kernel
ENDPROC(__secondary_switched)
```

#### secondary_start_kernel

```c
asmlinkage void secondary_start_kernel(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

	/*
	 * All kernel threads share the same mm context; grab a
	 * reference and switch to it.
	 */
	atomic_inc(&mm->mm_count);
	current->active_mm = mm;
	cpumask_set_cpu(cpu, mm_cpumask(mm));

	set_my_cpu_offset(per_cpu_offset(smp_processor_id()));
	printk("CPU%u: Booted secondary processor\n", cpu);

	/*
	 * TTBR0 is only used for the identity mapping at this stage. Make it
	 * point to zero page to avoid speculatively fetching new entries.
	 */
	cpu_set_reserved_ttbr0();
	flush_tlb_all();

	preempt_disable();
	trace_hardirqs_off();

	if (cpu_ops[cpu]->cpu_postboot)
		cpu_ops[cpu]->cpu_postboot();

	/*
	 * Log the CPU info before it is marked online and might get read.
	 */
	cpuinfo_store_cpu();

	/*
	 * Enable GIC and timers.
	 */
	notify_cpu_starting(cpu);

	smp_store_cpu_info(cpu);

	/*
	 * OK, now it's safe to let the boot CPU continue.  Wait for
	 * the CPU migration code to notice that the CPU is online
	 * before we continue.
	 */
	set_cpu_online(cpu, true);
	complete(&cpu_running);

	local_dbg_enable();
	local_irq_enable();
	local_async_enable();

	/*
	 * OK, it's off to the idle thread for us
	 */
	cpu_startup_entry(CPUHP_ONLINE);
}
```

#### cpu_startup_entry

```c
void cpu_startup_entry(enum cpuhp_state state)
{
	/*
	 * This #ifdef needs to die, but it's too late in the cycle to
	 * make this generic (arm and sh have never invoked the canary
	 * init for the non boot cpus!). Will be fixed in 3.11
	 */
	arch_cpu_idle_prepare();
	cpu_idle_loop();/*执行idle进程*/
}
```

所以在kernel启动的过程中是不会出现驱动并行加载的，驱动的加载在kernel_init进程中，多核之间的负载均衡是以进程（这里描述不是很准确，应该是调度实体）为最单位的。secondary cpu只会执行其他的进程。