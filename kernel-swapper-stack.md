### Kernel swapper进程Stack

---

以init_task为例既swapper进程，为内核的第一个进程
### init_task和init_thread_union

```c
/* Initial task structure */
struct task_struct init_task = INIT_TASK(init_task);
EXPORT_SYMBOL(init_task);

/*
 * Initial thread structure. Alignment of this is handled by a special
 * linker map entry.
 */
/*这种初始化应该也是属于gnu 对C语言的扩展Compound Literals，会自己做类型转换*/ 
union thread_union init_thread_union __init_task_data =
	{ INIT_THREAD_INFO(init_task) };
```

### init_task初始化

```c
#define init_thread_info	(init_thread_union.thread_info)

#define INIT_TASK(tsk)	\
{									\
	.state		= 0,						\
	.stack		= &init_thread_info,				\
	.usage		= ATOMIC_INIT(2),				\
	.flags		= PF_KTHREAD,					\
	.prio		= MAX_PRIO-20,					\
	.static_prio	= MAX_PRIO-20,					\
	.normal_prio	= MAX_PRIO-20,					\
	.policy		= SCHED_NORMAL,					\
	.cpus_allowed	= CPU_MASK_ALL,					\
	.nr_cpus_allowed= NR_CPUS,					\
	.mm		= NULL,						\
	.active_mm	= &init_mm,					\
	.restart_block = {						\
		.fn = do_no_restart_syscall,				\
	},	
}
```
### init_thread_union定义

```c
/* Attach to the init_task data structure for proper alignment */
#define __init_task_data __attribute__((__section__(".data..init_task")))

union thread_union init_thread_union __init_task_data =
	{ INIT_THREAD_INFO(init_task) };
```

__init_task_data表示init_thread_union定义在一个段中。init_thread_union是THREAD_SIZE对齐的。在vmlinux.lds.S中定义。

```assembly
#define INIT_TASK_DATA(align)						\
	. = ALIGN(align);						\
	*(.data..init_task)

#define RW_DATA_SECTION(cacheline, pagealigned, inittask)		\
	. = ALIGN(PAGE_SIZE);						\
	.data : AT(ADDR(.data) - LOAD_OFFSET) {				\
		INIT_TASK_DATA(inittask)				\
		NOSAVE_DATA						\
		PAGE_ALIGNED_DATA(pagealigned)				\
		CACHELINE_ALIGNED_DATA(cacheline)			\
		READ_MOSTLY_DATA(cacheline)				\
		DATA_DATA						\
		CONSTRUCTORS						\
	}

. = ALIGN(PAGE_SIZE);
	_data = .;
	_sdata = .;
	RW_DATA_SECTION(64, PAGE_SIZE, THREAD_SIZE)
	PECOFF_EDATA_PADDING
	_edata = .;
```

###  thread_union结构体

```c
#ifndef CONFIG_ARM64_64K_PAGES
#define THREAD_SIZE_ORDER	2
#endif

#define THREAD_SIZE			16384	/*16K*/
#define THREAD_START_SP		(THREAD_SIZE - 16)

/*
 * low level task data that entry.S needs immediate access to.
 * __switch_to() assumes cpu_context follows immediately after cpu_domain.
 */
struct thread_info {
	unsigned long		flags;			/* low level flags */
	mm_segment_t		addr_limit;		/* address limit */
	struct task_struct	*task;			/* main task structure */
	struct exec_domain	*exec_domain;	 /* execution domain */
	int			preempt_count;			/* 0 => preemptable, <0 => bug */
	int			cpu;				    /* cpu */
};

union thread_union {
	struct thread_info thread_info;
	unsigned long stack[THREAD_SIZE/sizeof(long)];
};
```

### init_thread_union初始化

```c
/* tsk为init_task */
#define INIT_THREAD_INFO(tsk)						\
{									\
	.task		= &tsk,						\
	.exec_domain	= &default_exec_domain,				\
	.flags		= 0,						\
	.preempt_count	= INIT_PREEMPT_COUNT,				\
	.addr_limit	= KERNEL_DS,					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)
```

### 栈顶

```c
#define task_thread_info(task)	((struct thread_info *)(task)->stack)
/*p = init_task*/
static inline unsigned long *end_of_stack(struct task_struct *p)
{
	/*向下增长*/
	return (unsigned long *)(task_thread_info(p) + 1);
}
```

init_task.stack = &init_thread_union.thread_info,init_thread_union为一个联合体，联合体的大小有最大元素值决定。所以联合体的大小为THREAD_SIZE/sizeof(long)，在arm64为16K。

```c
union thread_union {
	struct thread_info thread_info;
	unsigned long stack[THREAD_SIZE/sizeof(long)];
};
```
因为栈是高地址相低地址增长，所以栈低，也就是末端为的地址也等于thread_info的地址。栈顶可以在从汇编跳转到C入口地址给sp寄存器复制时看到：

```
/* arch/arm64/include/asm/thread_info.h*/
#define THREAD_SIZE		16384	/*4个页（如果也为4K）*/
#define THREAD_START_SP		(THREAD_SIZE - 16)

__switch_data:
	/*.quad占用8个字节*/
	.quad	__mmap_switched
	.quad	__bss_start			// x6
	.quad	__bss_stop			// x7
	.quad	processor_id			// x4
	.quad	__fdt_pointer			// x5
	.quad	memstart_addr			// x6
	/*init_thread_union定义在/init/init_task.c */
	.quad	init_thread_union + THREAD_START_SP // sp

/*
 * The following fragment of code is executed with the MMU on in MMU mode, and
 * uses absolute addresses; this is not position independent.
 */
__mmap_switched:
	adr	x3, __switch_data + 8

	ldp	x6, x7, [x3], #16
1:	cmp	x6, x7
	b.hs	2f
	str	xzr, [x6], #8			// Clear BSS
	b	1b
2:
	ldp	x4, x5, [x3], #16
	ldr	x6, [x3], #8
	ldr	x16, [x3]
	mov	sp, x16				//初始化堆栈指针，把init_thread_union + THREAD_START_SP赋值给sp
	str	x22, [x4]			// Save processor ID
	str	x21, [x5]			// Save FDT pointer
	str	x24, [x6]			// Save PHYS_OFFSET
	mov	x29, #0
	b	start_kernel		//跳转到c语言实现的函数start_kernel	
ENDPROC(__mmap_switched)
```

从上面看到会把sp寄存器初始化为init_thread_union + THREAD_START_SP，init_thread_union 为变量的名称，表示变量的地址，THREAD_START_SP为(THREAD_SIZE - 16)。

### tatk_struct和thread_info关系

![kernel-swapper-stack](F:\ARM\arm v8\study_notes\kernel-swapper-stack.jpg)

### current

```c
/*
 * how to get the current stack pointer from C,栈的地址保存在sp寄存器中
 */
register unsigned long current_stack_pointer asm ("sp");

/*
 * how to get the thread information struct from C
 */
static inline struct thread_info *current_thread_info(void) __attribute_const__;

static inline struct thread_info *current_thread_info(void)
{
	/**
	 *将栈的地址按照THREAD_SIZE对齐就可以得到thread_info的指针，因为thread_info起始地址也是
	 *THREAD_SIZE对齐的*
	 /
	return (struct thread_info *)
		(current_stack_pointer & ~(THREAD_SIZE - 1));
}
```

```c
/*current_thread_info()获得thread_info指针，进一步获得tast_struct指针*/
#define get_current() (current_thread_info()->task)
#define current get_current()
```

