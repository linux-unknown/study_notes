	/*
	 * Secondary entry point that jumps straight into the kernel. Only to
	 * be used where CPUs are brought online dynamically by the kernel.
	 */
ENTRY(secondary_entry)
	bl	el2_setup			// Drop to EL1
	bl	__calc_phys_offset		// x24=PHYS_OFFSET, x28=PHYS_OFFSET-PAGE_OFFSET
	bl	set_cpu_boot_mode_flag
	b	secondary_startup
ENDPROC(secondary_entry)


.macro	pgtbl, ttb0, ttb1, virt_to_phys
ldr \ttb1, =swapper_pg_dir
ldr \ttb0, =idmap_pg_dir
add \ttb1, \ttb1, \virt_to_phys
add \ttb0, \ttb0, \virt_to_phys
.endm



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


__enable_mmu:
	ldr	x5, =vectors
	/*配置中断向量表*/
	msr	vbar_el1, x5
	/*配置页表地址*/
	msr	ttbr0_el1, x25			// load TTBR0
	msr	ttbr1_el1, x26			// load TTBR1
	isb
	b	__turn_mmu_on
ENDPROC(__enable_mmu)


__turn_mmu_on:
	msr	sctlr_el1, x0
	isb
	/*跳转到__secondary_switched*/
	br	x27 
ENDPROC(__turn_mmu_on)

ENTRY(__secondary_switched)
	ldr	x0, [x21]			// get secondary_data.stack
	mov	sp, x0
	mov	x29, #0
	b	secondary_start_kernel
ENDPROC(__secondary_switched)


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

void cpu_startup_entry(enum cpuhp_state state)
{
	/*
	 * This #ifdef needs to die, but it's too late in the cycle to
	 * make this generic (arm and sh have never invoked the canary
	 * init for the non boot cpus!). Will be fixed in 3.11
	 */
#ifdef CONFIG_X86
	/*
	 * If we're the non-boot CPU, nothing set the stack canary up
	 * for us. The boot CPU already has it initialized but no harm
	 * in doing it again. This is a good place for updating it, as
	 * we wont ever return from this function (so the invalid
	 * canaries already on the stack wont ever trigger).
	 */
	boot_init_stack_canary();
#endif
	arch_cpu_idle_prepare();
	cpu_idle_loop();
}

