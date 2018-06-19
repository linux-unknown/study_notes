# linux 进程优先级

[TOC]

## 用户空间

### nice

用户空间nice值的范围为-20到19，值越小优先级越高

## kernel 空间

kernel空间进程的优先级范围为

0到140，其中0到99为RT进程的优先级，即实时优先级，100到130为SCHED_NORMAL/SCHED_BATCH进程的优先级。下面是内核中的定义

### nice范围

```c
#define MAX_NICE	19
#define MIN_NICE	-20
#define NICE_WIDTH	(MAX_NICE - MIN_NICE + 1)	/*40*/
```

###  RT优先级

```c
#define MAX_USER_RT_PRIO	100
#define MAX_RT_PRIO		MAX_USER_RT_PRIO	/*100*/
```

### 最大优先级

```c
#define MAX_PRIO		(MAX_RT_PRIO + NICE_WIDTH) /*100 + 40 = 140*/
```

### nice值在kerne空间的表示

在kernel中将nice值-20到19映射到100到139的优先级范围内，该优先级在kernel中称为**静态优先级**。

```c
#define DEFAULT_PRIO		(MAX_RT_PRIO + NICE_WIDTH / 2)/* 100 + 40/2 = 120 */
/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to static priority [ MAX_RT_PRIO..MAX_PRIO-1 ],
 * and back.
 */
#define NICE_TO_PRIO(nice)	((nice) + DEFAULT_PRIO)
#define PRIO_TO_NICE(prio)	((prio) - DEFAULT_PRIO)
```

### User priority

在kernel中User priority表示nice值经过转换后的优先级。User priority的转换方式如下：

```c
/*User priority的范围为100-139*/
#define USER_PRIO(p)		((p)-MAX_RT_PRIO)
#define TASK_USER_PRIO(p)	USER_PRIO((p)->static_prio)
```



