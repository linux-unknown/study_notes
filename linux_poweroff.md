# Linux power off 

---

 主要分析Linux power off执行过程，着重看设备驱动和电源管理需要完成那些工作

### 系统调用

```c
/*
 * Reboot system call: for obvious reasons only root may call it,
 * and even root needs to set up some magic numbers in the registers
 * so that some mistake won't make this reboot the whole machine.
 * You can also set the meaning of the ctrl-alt-del-key here.
 *
 * reboot doesn't sync: do that yourself before calling this.
 */
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	struct pid_namespace *pid_ns = task_active_pid_ns(current);
	char buffer[256];
	int ret = 0;

	/* We only trust the superuser with rebooting the system. */
	if (!ns_capable(pid_ns->user_ns, CAP_SYS_BOOT))
		return -EPERM;

	/* For safety, we require "magic" arguments. */
	if (magic1 != LINUX_REBOOT_MAGIC1 ||
			(magic2 != LINUX_REBOOT_MAGIC2 &&
			magic2 != LINUX_REBOOT_MAGIC2A &&
			magic2 != LINUX_REBOOT_MAGIC2B &&
			magic2 != LINUX_REBOOT_MAGIC2C))
		return -EINVAL;

	/*
	 * If pid namespaces are enabled and the current task is in a child
	 * pid_namespace, the command is handled by reboot_pid_ns() which will
	 * call do_exit().
	 */
	ret = reboot_pid_ns(pid_ns, cmd);
	if (ret)
		return ret;

	/* Instead of trying to make the power_off code look like
	 * halt when pm_power_off is not set do it the easy way.
	 */
	if ((cmd == LINUX_REBOOT_CMD_POWER_OFF) && !pm_power_off)
		cmd = LINUX_REBOOT_CMD_HALT;

	mutex_lock(&reboot_mutex);
	switch (cmd) {
	case LINUX_REBOOT_CMD_RESTART:
		kernel_restart(NULL);
		break;

	case LINUX_REBOOT_CMD_CAD_ON:
		C_A_D = 1;
		break;

	case LINUX_REBOOT_CMD_CAD_OFF:
		C_A_D = 0;
		break;

	case LINUX_REBOOT_CMD_HALT:
		kernel_halt();
		do_exit(0);
		panic("cannot halt");

	case LINUX_REBOOT_CMD_POWER_OFF:
		kernel_power_off();
		do_exit(0);
		break;

	case LINUX_REBOOT_CMD_RESTART2:
		ret = strncpy_from_user(&buffer[0], arg, sizeof(buffer) - 1);
		if (ret < 0) {
			ret = -EFAULT;
			break;
		}
		buffer[sizeof(buffer) - 1] = '\0';

		kernel_restart(buffer);
		break;

#ifdef CONFIG_KEXEC
	case LINUX_REBOOT_CMD_KEXEC:
		ret = kernel_kexec();
		break;
#endif

#ifdef CONFIG_HIBERNATION
	case LINUX_REBOOT_CMD_SW_SUSPEND:
		ret = hibernate();
		break;
#endif

	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&reboot_mutex);
	return ret;
}
```

从上面我们可以看出：

* 只有超级用户才有可能执行reboot，halt，poweroff等动作

我们的重点在于kernel_power_off

### kernel_power_off

```c
void kernel_power_off(void)
{
	kernel_shutdown_prepare(SYSTEM_POWER_OFF);
	if (pm_power_off_prepare)
		pm_power_off_prepare();
	migrate_to_reboot_cpu();
	syscore_shutdown();
	pr_emerg("Power down\n");
	kmsg_dump(KMSG_DUMP_POWEROFF);
	machine_power_off();
}
EXPORT_SYMBOL_GPL(kernel_power_off);
```
#### kernel_shutdown_prepare
```c
static void kernel_shutdown_prepare(enum system_states state)
{
  	/*调用注册了对SYS_HALT,SYS_POWER_OFF关心的回调函数*/
	blocking_notifier_call_chain(&reboot_notifier_list,
		(state == SYSTEM_HALT) ? SYS_HALT : SYS_POWER_OFF, NULL);
	system_state = state;
  	/*Prevent new helpers from being started*/
	usermodehelper_disable();
	device_shutdown();
}
```

可以使用register_reboot_notifier注册对SYS_HALT,SYS_POWER_OFF关系事件的回调函数

##### device_shutdown

```c
/**
 * device_shutdown - call ->shutdown() on each device to shutdown.
 */
void device_shutdown(void)
{
	struct device *dev, *parent;

	spin_lock(&devices_kset->list_lock);
	/*
	 * Walk the devices list backward, shutting down each in turn.
	 * Beware that device unplug events may also start pulling
	 * devices offline, even as the system is shutting down.
	 */
	while (!list_empty(&devices_kset->list)) {
 		/*list为双向链表，从最后一个网前开始查找*/
		dev = list_entry(devices_kset->list.prev, struct device,
				kobj.entry);

		/*
		 * hold reference count of device's parent to
		 * prevent it from being freed because parent's
		 * lock is to be held
		 */
		parent = get_device(dev->parent);/*增加引用计数，反之设备被释放*/
		get_device(dev);
		/*
		 * Make sure the device is off the kset list, in the
		 * event that dev->*->shutdown() doesn't remove it.
		 */
		list_del_init(&dev->kobj.entry);/*从链表上删除该成员，并重新初始化该成员的链表头*/
		spin_unlock(&devices_kset->list_lock);

		/* hold lock to avoid race with probe/release */
		if (parent)
			device_lock(parent);
		device_lock(dev);

		/* Don't allow any more runtime suspends */
		pm_runtime_get_noresume(dev);
		pm_runtime_barrier(dev);
      
      	/**
 		 *bus所能关闭的电源智能是bus上的从设备，bus控制器并不在该bus的管理范围内。
 		 *bus控制器的device和driver通常是挂载platform虚拟总线上的类型的。
 		 *从内核的代码看，并没有多少驱动实现shotdown函数，应该是关机的时序不是很重
 		 *最后的总闸关了就可以了。
		 *调用bus的shutdown，以i2c bus为例，进行说明
		 */
		if (dev->bus && dev->bus->shutdown) {
			if (initcall_debug)
				dev_info(dev, "shutdown\n");
			dev->bus->shutdown(dev);
		} else if (dev->driver && dev->driver->shutdown) {
			if (initcall_debug)
				dev_info(dev, "shutdown\n");
			dev->driver->shutdown(dev);
		}

		device_unlock(dev);
		if (parent)
			device_unlock(parent);
		/*减少引用计数*/
		put_device(dev);
		put_device(parent);

		spin_lock(&devices_kset->list_lock);
	}
	spin_unlock(&devices_kset->list_lock);
}
```

每一个先注册的device设备都会加入到devices_kset->list中,以属性的platform_device_register为例：

```c
int platform_device_register(struct platform_device *pdev)
{
	device_initialize(&pdev->dev);
	arch_setup_pdev_archdata(pdev);
	return platform_device_add(pdev);
}
EXPORT_SYMBOL_GPL(platform_device_register);
```

在device_initialize中会把dev的kobj.kset赋值为devices_kset。
```c
void device_initialize(struct device *dev)
{
	dev->kobj.kset = devices_kset;
	kobject_init(&dev->kobj, &device_ktype);
	......
}
EXPORT_SYMBOL_GPL(device_initialize);
```

platform_device_add(pdev)-->device_add(&pdev->dev)-->kobject_add(&dev->kobj, dev->kobj.parent, NULL)

-->kobject_add_varg(kobj, parent, fmt, args)

```c
static int kobject_add_varg(struct kobject *kobj, struct kobject *parent,
			    const char *fmt, va_list vargs)
{
	int retval;
	retval = kobject_set_name_vargs(kobj, fmt, vargs);
	kobj->parent = parent;
	return kobject_add_internal(kobj);
}
```

```c
static int kobject_add_internal(struct kobject *kobj)
{
	int error = 0;
	struct kobject *parent;
	......
	parent = kobject_get(kobj->parent);

	/* join kset if set, use it as parent if we do not already have one */
	if (kobj->kset) {
		if (!parent)
			parent = kobject_get(&kobj->kset->kobj);/*如果没有父设备，则把kset作为其父设备*/
		kobj_kset_join(kobj);
		kobj->parent = parent;
	}
	......
	return error;
}

```

```c
/* add the kobject to its kset's list */
static void kobj_kset_join(struct kobject *kobj)
{
	kset_get(kobj->kset);
	spin_lock(&kobj->kset->list_lock);
	/* 这里的kobj->kset则为执行了device_initialize之后的devices_kset*/
	list_add_tail(&kobj->entry, &kobj->kset->list);
	spin_unlock(&kobj->kset->list_lock);
}
```
##### i2c_bus_type
```c
struct bus_type i2c_bus_type = {

	.name		= "i2c",
	.match		= i2c_device_match,
	.probe		= i2c_device_probe,
	.remove		= i2c_device_remove,
	.shutdown	= i2c_device_shutdown,
};
```
###### i2c_device_shutdown
```c
/**
 *bus所能关闭的电源智能是bus上的从设备，bus控制器并不在该bus的管理范围内
 *bus控制器的device和driver通常是platform类型的。
 */
static void i2c_device_shutdown(struct device *dev)
{
	struct i2c_client *client = i2c_verify_client(dev);
	struct i2c_driver *driver;
  	/*不同的设备因为dev的地址不一样，所以可以匹配到对应的驱动*/
	driver = to_i2c_driver(dev->driver);
	if (driver->shutdown)
		driver->shutdown(client);
}
```

#### syscore_shutdown

---

syscore_shutdown应该使系统核心设备，所以关闭比较靠后，从内核代码来看，多试一些clock。

#### machine_power_off

```c
void machine_power_off(void)
{
	local_irq_disable();
	smp_send_stop();
  	/*对于arm64 pm_power_off=psci_sys_poweroff*/
	if (pm_power_off)
		pm_power_off();
}

```

