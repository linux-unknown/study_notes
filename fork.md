#  fork

```
schedule_preempt_disabled-->schedule-->__schedule-->context_switch-->switch_to-->__switch_to-->cpu_switch_to
```
```assembly
ENTRY(cpu_switch_to)
	add	x8, x0, #THREAD_CPU_CONTEXT
	mov	x9, sp
	stp	x19, x20, [x8], #16		// store callee-saved registers
	stp	x21, x22, [x8], #16
	stp	x23, x24, [x8], #16
	stp	x25, x26, [x8], #16
	stp	x27, x28, [x8], #16
	stp	x29, x9, [x8], #16
	str	lr, [x8]
	add	x8, x1, #THREAD_CPU_CONTEXT
	ldp	x19, x20, [x8], #16		// restore callee-saved registers
	ldp	x21, x22, [x8], #16
	ldp	x23, x24, [x8], #16
	ldp	x25, x26, [x8], #16
	ldp	x27, x28, [x8], #16
	ldp	x29, x9, [x8], #16
	ldr	lr, [x8]
	mov	sp, x9
	ret  /*返回到ret_from_fork*/
ENDPROC(cpu_switch_to)
```
arch\arm64\kernel\entry.S
```assembly
/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
	cbz	x19, 1f				// not a kernel thread,如果不是kernel thread，跳转到标号1
	mov	x0, x20	
    /*从这调转到新的kernel thread，对于内核刚启动则是kernel_init，
     *该线程的名称和init_task一样都是swapper
     */
	blr	x19	
1:	get_thread_info tsk
	b	ret_to_user
ENDPROC(ret_from_fork)
```

ret_from_fork是在调用fork的时候设置的

