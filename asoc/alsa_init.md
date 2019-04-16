

### alsa init

```c
#define CONFIG_SND_MAJOR	116	/* standard configuration */
static int major = CONFIG_SND_MAJOR;

static int __init alsa_sound_init(void)
{
	snd_major = major;
	snd_ecards_limit = cards_limit;
	/* 注册名称为alsa的字符设备驱动，主设备号为116 */
	if (register_chrdev(major, "alsa", &snd_fops)) {
	}
	/* 在proc中创建asound的一些节点 */
	if (snd_info_init() < 0) {
	}
	/* 在proc中创建的一些节点 */
	snd_info_minor_register();
#ifndef MODULE
	pr_info("Advanced Linux Sound Architecture Driver Initialized.\n");
#endif
	return 0;
}
```

```c
static const struct file_operations snd_fops =
{
	.owner =	THIS_MODULE,
	.open =		snd_open,
	.llseek =	noop_llseek,
};
```

```c
static int snd_open(struct inode *inode, struct file *file)
{
	/* 获取节点的子设备号，不同的子设备号，有不同的fops 
	 * control，pcm等的设备是根据子设备号进行区分的
	 */
	unsigned int minor = iminor(inode);
	struct snd_minor *mptr = NULL;
	const struct file_operations *new_fops;
	int err = 0;

	mutex_lock(&sound_mutex);
	mptr = snd_minors[minor];
	if (mptr == NULL) {
		mptr = autoload_device(minor);
		if (!mptr) {
			mutex_unlock(&sound_mutex);
			return -ENODEV;
		}
	}
	/* 获得新的fops */
	new_fops = fops_get(mptr->f_ops);
	mutex_unlock(&sound_mutex);
	/* 使用新的fops替换老大fops，后续的read，write，ioctl都是调用新的fops */
	replace_fops(file, new_fops);
	/* 调用新fops的open */
	if (file->f_op->open)
		err = file->f_op->open(inode, file);
	return err;
}
```

