# run_init_process如何返回用户态

[TOC]

run_init_process的调用过程

## rest_init

```c
static noinline void __init_refok rest_init(void)
{
	/*
	 * We need to spawn init first so that it obtains pid 1, however
	 * the init task will end up wanting to create kthreads, which, if
	 * we schedule it before we create kthreadd, will OOPS.
	 */
	kernel_thread(kernel_init, NULL, CLONE_FS);
}

static int __ref kernel_init(void *unused)
{
	if (ramdisk_execute_command) {
		ret = run_init_process(ramdisk_execute_command);
		if (!ret)
			return 0;
		pr_err("Failed to execute %s (error %d)\n",
		       ramdisk_execute_command, ret);
	}

	panic("No working init found.  Try passing init= option to kernel. "
	      "See Linux Documentation/init.txt for guidance.");
}
```

`rest_init`是kernel的0号进程，既swapper进程执行的，然后执行`kernel_thread(kernel_init, NULL, CLONE_FS);`创建内核线程，该线程执行`kernel_init`函数。**这时内核已经可以调度了**

## kernel_thread

```c
/*
 * Create a kernel thread.
 */
pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	return do_fork(flags|CLONE_VM|CLONE_UNTRACED, (unsigned long)fn,
		(unsigned long)arg, NULL, NULL);
}
```

### do_fork

```c
/*
 *  Ok, this is the main fork-routine.
 *
 * It copies the process, and if successful kick-starts
 * it and waits for it to finish using the VM if required.
 */
long do_fork(unsigned long clone_flags, unsigned long stack_start, 
          unsigned long stack_size,
	      int __user *parent_tidptr,
	      int __user *child_tidptr)
{
	struct task_struct *p;
	int trace = 0;
	long nr;

	/* 删掉flag检查 */
	p = copy_process(clone_flags, stack_start, stack_size, child_tidptr, NULL, trace);
	/*
	 * Do this prior waking up the new thread - the thread pointer
	 * might get invalid after that point, if the thread exits quickly.
	 */
	if (!IS_ERR(p)) {
		struct completion vfork;
		struct pid *pid;

		trace_sched_process_fork(current, p);

		pid = get_task_pid(p, PIDTYPE_PID);
		nr = pid_vnr(pid);

		if (clone_flags & CLONE_PARENT_SETTID)
			put_user(nr, parent_tidptr);

		if (clone_flags & CLONE_VFORK) {
			p->vfork_done = &vfork;
			init_completion(&vfork);
			get_task_struct(p);
		}
		/* 唤醒新的内核线程 */
		wake_up_new_task(p);

		/* forking complete and child started to run, tell ptracer */
		if (unlikely(trace))
			ptrace_event_pid(trace, pid);

		if (clone_flags & CLONE_VFORK) {
			if (!wait_for_vfork_done(p, &vfork))
				ptrace_event_pid(PTRACE_EVENT_VFORK_DONE, pid);
		}

		put_pid(pid);
	} else {
		nr = PTR_ERR(p);
	}
	return nr;
}

```

### copy_process

```c
/*
 * This creates a new process as a copy of the old one,
 * but does not actually start it yet.
 *
 * It copies the registers, and all the appropriate
 * parts of the process environment (as per the clone
 * flags). The actual kick-off is left to the caller.
 */
static struct task_struct *copy_process(unsigned long clone_flags,
					unsigned long stack_start,
					unsigned long stack_size,
					int __user *child_tidptr,
					struct pid *pid,
					int trace)
{
	int retval;
	struct task_struct *p;
    /* 删减了很多，主要看 copy_thread*/
	/* dup  struct task_struct */
	p = dup_task_struct(current);

	p->start_time = ktime_get_ns();
	p->real_start_time = ktime_get_boot_ns();
	p->io_context = NULL;
	p->audit_context = NULL;
	
	/* Perform scheduler related setup. Assign this task to a CPU. */
	retval = sched_fork(clone_flags, p);

	retval = copy_mm(clone_flags, p);

	retval = copy_thread(clone_flags, stack_start, stack_size, p);

	if (pid != &init_struct_pid) {
		retval = -ENOMEM;
		pid = alloc_pid(p->nsproxy->pid_ns_for_children);
	
	}

	/* ok, now we should be set up.. */
	p->pid = pid_nr(pid);
	if (clone_flags & CLONE_THREAD) {
		p->exit_signal = -1;
		p->group_leader = current->group_leader;
		p->tgid = current->tgid;
	} else {
		if (clone_flags & CLONE_PARENT)
			p->exit_signal = current->group_leader->exit_signal;
		else
			p->exit_signal = (clone_flags & CSIGNAL);
		p->group_leader = p;
		p->tgid = p->pid;
	}

	p->nr_dirtied = 0;
	p->nr_dirtied_pause = 128 >> (PAGE_SHIFT - 10);
	p->dirty_paused_when = 0;

	p->pdeath_signal = 0;
	INIT_LIST_HEAD(&p->thread_group);
	p->task_works = NULL;

	return p;
}
```

copy_thread

```c
int copy_thread(unsigned long clone_flags, unsigned long stack_start,
		unsigned long stk_sz, struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);
	unsigned long tls = p->thread.tp_value;

	memset(&p->thread.cpu_context, 0, sizeof(struct cpu_context));

	if (likely(!(p->flags & PF_KTHREAD))) { /* 如果不是内核线程 */
		*childregs = *current_pt_regs();
        /* 用户进程的话，子进程返回0，所以regs[0]设置成0- */
		childregs->regs[0] = 0;
		if (is_compat_thread(task_thread_info(p))) {
			if (stack_start)
				childregs->compat_sp = stack_start;
		} else {
			/*
			 * Read the current TLS pointer from tpidr_el0 as it may be
			 * out-of-sync with the saved value.
			 */
			asm("mrs %0, tpidr_el0" : "=r" (tls));
			if (stack_start) {
				/* 16-byte aligned stack mandatory on AArch64 */
				if (stack_start & 15)
					return -EINVAL;
				childregs->sp = stack_start;
			}
		}
		/*
		 * If a TLS pointer was passed to clone (4th argument), use it
		 * for the new thread.
		 */
		if (clone_flags & CLONE_SETTLS)
			tls = childregs->regs[3];
	} else {/* 如果是内核线程 */
		memset(childregs, 0, sizeof(struct pt_regs));
  		/* PSR_MODE_EL1h表示为EL1模式，既内核模式 */
		childregs->pstate = PSR_MODE_EL1h;
        /* 内核线程时stack_start表示执行函数的指针 */
		p->thread.cpu_context.x19 = stack_start;
		p->thread.cpu_context.x20 = stk_sz;
	}
    /* 把pc设置为ret_from_fork函数指针，该函数为汇编编写 */
	p->thread.cpu_context.pc = (unsigned long)ret_from_fork;
    /* 栈顶存放的是struct pt_regs结构体，该结构体是架构相关的 */
	p->thread.cpu_context.sp = (unsigned long)childregs;
	p->thread.tp_value = tls;

	ptrace_hw_copy_thread(p);

	return 0;
}
```

`p->thread.cpu_context.pc = (unsigned long)ret_from_fork;`这样在调度到`kernel_init`线程的时候将开始从`ret_from_fork`开始执行。

#### ret_from_fork

```assembly
/*
 * This is how we return from a fork.
 */
/* 
 *在执行cpu_switch_to的时候会将thread.cpu_context中的x19，x20赋值到x19和x20寄存器
 *
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
	cbz	x19, 1f				// not a kernel thread,如果x19寄存器为0，则跳转到1f标签，
	mov	x0, x20				//x20为线程的args指针，x19为线程函数的指针。
	//跳转到kernel_init开始执行，此处使用blr，表示还会跳转回来，blr将pc + 4写入到x30寄存器
	blr	x19					
1:	get_thread_info tsk
	b	ret_to_user
ENDPROC(ret_from_fork)
```

### run_init_process

`kernel_init`执行`run_init_process`

```c
static int run_init_process(const char *init_filename)
{
	argv_init[0] = init_filename;
	return do_execve(getname_kernel(init_filename),
		(const char __user *const __user *)argv_init,
		(const char __user *const __user *)envp_init);
}
```

#### do_execve

```
int do_execve(struct filename *filename,
	const char __user *const __user *__argv,
	const char __user *const __user *__envp)
{
	struct user_arg_ptr argv = { .ptr.native = __argv };
	struct user_arg_ptr envp = { .ptr.native = __envp };
	return do_execveat_common(AT_FDCWD, filename, argv, envp, 0);
}
```

#### do_execveat_common

```c
/*
 * sys_execve() executes a new program.
 */
static int do_execveat_common(int fd, struct filename *filename,
			      struct user_arg_ptr argv,
			      struct user_arg_ptr envp,
			      int flags)
{
	char *pathbuf = NULL;
	struct linux_binprm *bprm;
	struct file *file;
	struct files_struct *displaced;
	int retval;
	/*
	 * We move the actual failure in case of RLIMIT_NPROC excess from
	 * set*uid() to execve() because too many poorly written programs
	 * don't check setuid() return code.  Here we additionally recheck
	 * whether NPROC limit is still exceeded.
	 */
	if ((current->flags & PF_NPROC_EXCEEDED) &&
	    atomic_read(&current_user()->processes) > rlimit(RLIMIT_NPROC)) {
		retval = -EAGAIN;
		goto out_ret;
	}

	/* We're below the limit (still or again), so we don't want to make
	 * further execve() calls fail. */
	current->flags &= ~PF_NPROC_EXCEEDED;

	retval = unshare_files(&displaced);


	retval = -ENOMEM;
	bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);

	retval = prepare_bprm_creds(bprm);


	check_unsafe_exec(bprm);
	current->in_execve = 1;
	/* 打开文件 */
	file = do_open_execat(fd, filename, flags);

	sched_exec();

	bprm->file = file;
	if (fd == AT_FDCWD || filename->name[0] == '/') {
		bprm->filename = filename->name;
	} else {
		if (filename->name[0] == '\0')
			pathbuf = kasprintf(GFP_TEMPORARY, "/dev/fd/%d", fd);
		else
			pathbuf = kasprintf(GFP_TEMPORARY, "/dev/fd/%d/%s",
					    fd, filename->name);
		if (!pathbuf) {
			retval = -ENOMEM;
			goto out_unmark;
		}
		/*
		 * Record that a name derived from an O_CLOEXEC fd will be
		 * inaccessible after exec. Relies on having exclusive access to
		 * current->files (due to unshare_files above).
		 */
		if (close_on_exec(fd, rcu_dereference_raw(current->files->fdt)))
			bprm->interp_flags |= BINPRM_FLAGS_PATH_INACCESSIBLE;
		bprm->filename = pathbuf;
	}
	bprm->interp = bprm->filename;

	retval = bprm_mm_init(bprm);

	bprm->argc = count(argv, MAX_ARG_STRINGS);

	bprm->envc = count(envp, MAX_ARG_STRINGS);

	retval = prepare_binprm(bprm);

	retval = copy_strings_kernel(1, &bprm->filename, bprm);
	
	bprm->exec = bprm->p;
	retval = copy_strings(bprm->envc, envp, bprm);

	retval = copy_strings(bprm->argc, argv, bprm);
	
	retval = exec_binprm(bprm);

	/* execve succeeded */
	current->fs->in_exec = 0;
	current->in_execve = 0;
	acct_update_integrals(current);
	task_numa_free(current);
	free_bprm(bprm);
	kfree(pathbuf);
	putname(filename);
	if (displaced)
		put_files_struct(displaced);
	return retval;
}
```

#### exec_binprm

```c
static int exec_binprm(struct linux_binprm *bprm)
{
	pid_t old_pid, old_vpid;
	int ret;

	/* Need to fetch pid before load_binary changes it */
	old_pid = current->pid;
	rcu_read_lock();
	old_vpid = task_pid_nr_ns(current, task_active_pid_ns(current->parent));
	rcu_read_unlock();

	ret = search_binary_handler(bprm);
	if (ret >= 0) {
		audit_bprm(bprm);
		trace_sched_process_exec(current, old_pid, bprm);
		ptrace_event(PTRACE_EVENT_EXEC, old_vpid);
		proc_exec_connector(current);
	}

	return ret;
}
```

#### search_binary_handler

```c
/*
 * cycle the list of binary formats handler, until one recognizes the image
 */
int search_binary_handler(struct linux_binprm *bprm)
{
	bool need_retry = IS_ENABLED(CONFIG_MODULES);
	struct linux_binfmt *fmt;
	int retval;

	/* This allows 4 levels of binfmt rewrites before failing hard. */
	if (bprm->recursion_depth > 5)
		return -ELOOP;

	retval = security_bprm_check(bprm);
	if (retval)
		return retval;

	retval = -ENOENT;
 retry:
	read_lock(&binfmt_lock);
	list_for_each_entry(fmt, &formats, lh) {
		if (!try_module_get(fmt->module))
			continue;
		read_unlock(&binfmt_lock);
		bprm->recursion_depth++;
        /* 以elf的load_binary为例 */
		retval = fmt->load_binary(bprm);
		read_lock(&binfmt_lock);
		put_binfmt(fmt);
		bprm->recursion_depth--;
		if (retval < 0 && !bprm->mm) {
			/* we got to flush_old_exec() and failed after it */
			read_unlock(&binfmt_lock);
			force_sigsegv(SIGSEGV, current);
			return retval;
		}
		if (retval != -ENOEXEC || !bprm->file) {
			read_unlock(&binfmt_lock);
			return retval;
		}
	}
	read_unlock(&binfmt_lock);

	if (need_retry) {
		if (printable(bprm->buf[0]) && printable(bprm->buf[1]) &&
		    printable(bprm->buf[2]) && printable(bprm->buf[3]))
			return retval;
		if (request_module("binfmt-%04x", *(ushort *)(bprm->buf + 2)) < 0)
			return retval;
		need_retry = false;
		goto retry;
	}

	return retval;
}
```

#### elf_format

```c
static struct linux_binfmt elf_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_elf_binary,
	.load_shlib	= load_elf_library,
	.core_dump	= elf_core_dump,
	.min_coredump	= ELF_EXEC_PAGESIZE,
};
```

##### load_elf_binary

```c
static int load_elf_binary(struct linux_binprm *bprm)
{
	struct file *interpreter = NULL; /* to shut gcc up */
 	unsigned long load_addr = 0, load_bias = 0;
	int load_addr_set = 0;
	char * elf_interpreter = NULL;
	unsigned long error;
	struct elf_phdr *elf_ppnt, *elf_phdata, *interp_elf_phdata = NULL;
	unsigned long elf_bss, elf_brk;
	int retval, i;
	unsigned long elf_entry;
	unsigned long interp_load_addr = 0;
	unsigned long start_code, end_code, start_data, end_data;
	unsigned long reloc_func_desc __maybe_unused = 0;
	int executable_stack = EXSTACK_DEFAULT;
	
    /* regs保存在栈顶 */
	struct pt_regs *regs = current_pt_regs();

    /* 删掉了解析elf文件的代码 */
	struct arch_elf_state arch_state = INIT_ARCH_ELF_STATE;
	
	current->mm->end_code = end_code;
	current->mm->start_code = start_code;
	current->mm->start_data = start_data;
	current->mm->end_data = end_data;
	current->mm->start_stack = bprm->p;

	/* elf_entry为elf入口地址，bprm->p为栈指针 */
	start_thread(regs, elf_entry, bprm->p);

	return retval;
}
```

###### start_thread

```c
static inline void start_thread(struct pt_regs *regs, unsigned long pc,
				unsigned long sp)
{
	start_thread_common(regs, pc);
    /* 将pstate设置为用户模式 */
	regs->pstate = PSR_MODE_EL0t;
	regs->sp = sp;
}
```

###### start_thread_common

```c
static inline void start_thread_common(struct pt_regs *regs, unsigned long pc)
{
	memset(regs, 0, sizeof(*regs));
	regs->syscallno = ~0UL;
    /* 设置regs->pc为elf文件入口地址 */
	regs->pc = pc;
}
```

函数的层层返回，返回到kernel_init，然后继续返回，那么就返回到了`ret_from_fork`中，接下来执行的语句为

```assembly
/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
	cbz	x19, 1f				// not a kernel thread
	mov	x0, x20
	blr	x19
1:	get_thread_info tsk
	b	ret_to_user
ENDPROC(ret_from_fork)
```

###### get_thread_info

```assembly
tsk	.req	x28		// current thread_info

.macro	get_thread_info, rd
mov	\rd, sp
and	\rd, \rd, #~(THREAD_SIZE - 1)	// top of stack
.endm

最终get_thread_info展开后为
/* 将栈指针赋值给x28 */
mov	x28, sp
/* x28与上#~(THREAD_SIZE - 1)则得到栈顶 */
and	x28, x28, #~(THREAD_SIZE - 1)	// top of stack，栈从高向低增长
```

###### ret_to_user

跳转到ret_to_user

```assembly
/*
 * "slow" syscall return path.
 */
ret_to_user:
	disable_irq				// disable interrupts
	ldr	x1, [tsk, #TI_FLAGS] //DEFINE(TI_FLAGS,	offsetof(struct thread_info, flags));
	
	/* work to do on interrupt/exception return */
	/* #define _TIF_WORK_MASK							\
	 * (0x0000FFFF &							\
	 * ~(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|			\
	 *  _TIF_SINGLESTEP|_TIF_SECCOMP|_TIF_SYSCALL_EMU))
	 */
	and	x2, x1, #_TIF_WORK_MASK
	cbnz	x2, work_pending /* 第二次进来则 */
	enable_step_tsk x1, x2 /*应该是和debug有关，先不管*/
no_work_pending:
	kernel_exit 0, ret = 0
ENDPROC(ret_to_user)


.macro  disable_irq
	msr     daifset, #2
.endm

.macro  enable_irq
	msr     daifclr, #2
.endm

```

###### work_pending

```assembly
/*
 * Ok, we need to do extra processing, enter the slow path.
 */
fast_work_pending:
	str	x0, [sp, #S_X0]			// returned x0
work_pending:
	/* 是否需要重新调度 
	 * 如果x1的TIF_NEED_RESCHED位不是0，则跳转到work_resched */
	 */
	tbnz	x1, #TIF_NEED_RESCHED, work_resched 
	/* TIF_SIGPENDING, TIF_NOTIFY_RESUME or TIF_FOREIGN_FPSTATE case */
	/* DEFINE(S_PSTATE,		offsetof(struct pt_regs, pstate)); */
	ldr	x2, [sp, #S_PSTATE]
	mov	x0, sp				// 'regs'
	/* x2的PSR_MODE_MASK bit是不是都是0，都是0则是PSR_MODE_EL0t，既user模式  */
	tst	x2, #PSR_MODE_MASK		// user mode regs?
	b.ne	no_work_pending			// returning to kernel
	enable_irq				// enable interrupts for do_notify_resume()
	bl	do_notify_resume	// 先不关心
	b	ret_to_user			// 继续回到ret_to_user
work_resched:
	bl	schedule
```

###### kernel_exit

```
el = 0
.macro	kernel_exit, el, ret = 0
	/* DEFINE(S_PC,	offsetof(struct pt_regs, pc)); */
	ldp	x21, x22, [sp, #S_PC]		// load ELR, SPSR，pc保存到x21中
	.if	\el == 0
	ct_user_enter				// debug用，不用管
	ldr	x23, [sp, #S_SP]		// load return stack pointer
	msr	sp_el0, x23				// 用户空间的栈指针
	.endif
	msr	elr_el1, x21			// set up the return data，返回地址为用户空间入口地址
	msr	spsr_el1, x22			/* x22保存为pstate */
	.if	\ret
	ldr	x1, [sp, #S_X1]			// preserve x0 (syscall return)
	.else
	ldp	x0, x1, [sp, #16 * 0]
	.endif
	ldp	x2, x3, [sp, #16 * 1]
	ldp	x4, x5, [sp, #16 * 2]
	ldp	x6, x7, [sp, #16 * 3]
	ldp	x8, x9, [sp, #16 * 4]
	ldp	x10, x11, [sp, #16 * 5]
	ldp	x12, x13, [sp, #16 * 6]
	ldp	x14, x15, [sp, #16 * 7]
	ldp	x16, x17, [sp, #16 * 8]
	ldp	x18, x19, [sp, #16 * 9]
	ldp	x20, x21, [sp, #16 * 10]
	ldp	x22, x23, [sp, #16 * 11]
	ldp	x24, x25, [sp, #16 * 12]
	ldp	x26, x27, [sp, #16 * 13]
	ldp	x28, x29, [sp, #16 * 14]
	ldr	lr, [sp, #S_LR]
	add	sp, sp, #S_FRAME_SIZE		// restore sp
	eret					// return to kernel，就返回到了用户空间
.endm
```

