

# vhost_net_open

```c
enum {
	VHOST_NET_VQ_RX = 0,
	VHOST_NET_VQ_TX = 1,
	VHOST_NET_VQ_MAX = 2,
};

static int vhost_net_open(struct inode *inode, struct file *f)
{
	struct vhost_net *n;
	struct vhost_dev *dev;
	struct vhost_virtqueue **vqs;
	int r, i;

	n = kmalloc(sizeof *n, GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);

	vqs = kmalloc(VHOST_NET_VQ_MAX * sizeof(*vqs), GFP_KERNEL);

	dev = &n->dev;
	vqs[VHOST_NET_VQ_TX] = &n->vqs[VHOST_NET_VQ_TX].vq;
	vqs[VHOST_NET_VQ_RX] = &n->vqs[VHOST_NET_VQ_RX].vq;
	n->vqs[VHOST_NET_VQ_TX].vq.handle_kick = handle_tx_kick;
	n->vqs[VHOST_NET_VQ_RX].vq.handle_kick = handle_rx_kick;
	for (i = 0; i < VHOST_NET_VQ_MAX; i++) {
		n->vqs[i].ubufs = NULL;
		n->vqs[i].ubuf_info = NULL;
		n->vqs[i].upend_idx = 0;
		n->vqs[i].done_idx = 0;
		n->vqs[i].vhost_hlen = 0;
		n->vqs[i].sock_hlen = 0;
	}
	r = vhost_dev_init(dev, vqs, VHOST_NET_VQ_MAX, VHOST_NET_PKT_WEIGHT, VHOST_NET_WEIGHT);

	/* 初始化vhost_net的poll */
	vhost_poll_init(n->poll + VHOST_NET_VQ_TX, handle_tx_net, POLLOUT, dev);
	vhost_poll_init(n->poll + VHOST_NET_VQ_RX, handle_rx_net, POLLIN, dev);
	/* 将 vhost_net保存到 f->private_data 中 */
	f->private_data = n;

	return 0;
}
```

## vhost_dev_init

```c
long vhost_dev_init(struct vhost_dev *dev, struct vhost_virtqueue **vqs, int nvqs,
		    int weight, int byte_weight)
{
	int i;
	/* 初始化vhost_dev */
	dev->vqs = vqs;
	dev->nvqs = nvqs; /* 2 */
	mutex_init(&dev->mutex);
	dev->log_ctx = NULL;
	dev->log_file = NULL;
	dev->umem = NULL;
	dev->iotlb = NULL;
	dev->mm = NULL;
	dev->worker = NULL;
	dev->weight = weight;
	dev->byte_weight = byte_weight;
	init_llist_head(&dev->work_list);
	init_waitqueue_head(&dev->wait);
	INIT_LIST_HEAD(&dev->read_list);
	INIT_LIST_HEAD(&dev->pending_list);
	spin_lock_init(&dev->iotlb_lock);

	for (i = 0; i < dev->nvqs; ++i) {
		dev->vqs[i]->log = NULL;
		dev->vqs[i]->indirect = NULL;
		dev->vqs[i]->heads = NULL;
		dev->vqs[i]->dev = dev;
		mutex_init(&dev->vqs[i]->mutex);
		vhost_vq_reset(dev, dev->vqs[i]);
		if (dev->vqs[i]->handle_kick)
			/* 初始化vqs[i]->poll */
			vhost_poll_init(&dev->vqs[i]->poll, dev->vqs[i]->handle_kick, POLLIN, dev);
	}

	return 0;
}
```

### vhost_poll_init

```c
/* Init poll structure */
void vhost_poll_init(struct vhost_poll *poll, vhost_work_fn_t fn, unsigned long mask, struct vhost_dev *dev)
{
    /* 初始化等待队列的唤醒函数 */
	init_waitqueue_func_entry(&poll->wait, vhost_poll_wakeup);
    /* 初始化poll函数，调用poll时会调用到该函数 */
	init_poll_funcptr(&poll->table, vhost_poll_func);
	poll->mask = mask;
	poll->dev = dev;
	poll->wqh = NULL;

	vhost_work_init(&poll->work, fn);
}
```

### vhost_work_init

```c
void vhost_work_init(struct vhost_work *work, vhost_work_fn_t fn)
{
	clear_bit(VHOST_WORK_QUEUED, &work->flags);
    /* 初始化work->fn */
	work->fn = fn;
	init_waitqueue_head(&work->done);
}
```



# VHOST_SET_OWNER

```c
static long vhost_net_ioctl(struct file *f, unsigned int ioctl,
			    unsigned long arg)
{
	struct vhost_net *n = f->private_data;
	void __user *argp = (void __user *)arg;
	switch (ioctl) {
	case VHOST_SET_OWNER:
		return vhost_net_set_owner(n);
	}
}
```

## vhost_net_set_owner

```c
static long vhost_net_set_owner(struct vhost_net *n)
{
	int r;
	mutex_lock(&n->dev.mutex);
	if (vhost_dev_has_owner(&n->dev)) {}
	r = vhost_net_set_ubuf_info(n);
	r = vhost_dev_set_owner(&n->dev);
out:
	mutex_unlock(&n->dev.mutex);
	return r;
}
```

### vhost_net_set_ubuf_info

```c
int vhost_net_set_ubuf_info(struct vhost_net *n)
{
	bool zcopy;
	int i;
	for (i = 0; i < VHOST_NET_VQ_MAX; ++i) {
		/* 不支持zcopy， init的时候会设置 */
		zcopy = vhost_net_zcopy_mask & (0x1 << i);
		if (!zcopy)
			continue;
		n->vqs[i].ubuf_info = kmalloc(sizeof(*n->vqs[i].ubuf_info) * UIO_MAXIOV, GFP_KERNEL);
	}
	return 0;
}
```

## vhost_dev_set_owner

```c
/* Caller should have device mutex */
long vhost_dev_set_owner(struct vhost_dev *dev)
{
	struct task_struct *worker;
	int err;

	/* Is there an owner already? */
	if (vhost_dev_has_owner(dev)) {};
	/* No owner, become one */
	dev->mm = get_task_mm(current);
	/* 创建内核线程，线程名称vhost-pid */
	worker = kthread_create(vhost_worker, dev, "vhost-%d", current->pid);
	dev->worker = worker;
	/* 唤醒线程 */
	wake_up_process(worker);	/* avoid contributing to loadavg */
	err = vhost_attach_cgroups(dev);
	err = vhost_dev_alloc_iovecs(dev);
	return 0;
}
```

## vhost_dev_alloc_iovecs

```c
/* Helper to allocate iovec buffers for all vqs. 
 * 暂且不知道有什么用
 */
static long vhost_dev_alloc_iovecs(struct vhost_dev *dev)
{
	int i;
	for (i = 0; i < dev->nvqs; ++i) {
		dev->vqs[i]->indirect = kmalloc(sizeof *dev->vqs[i]->indirect * UIO_MAXIOV, GFP_KERNEL);
		dev->vqs[i]->log = kmalloc(sizeof *dev->vqs[i]->log * UIO_MAXIOV, GFP_KERNEL);
		dev->vqs[i]->heads = kmalloc(sizeof *dev->vqs[i]->heads * UIO_MAXIOV, GFP_KERNEL);
	}
	return 0;
}
```

# VHOST_SET_MEM_TABLE

```c
static long vhost_set_memory(struct vhost_dev *d, struct vhost_memory __user *m)
{
	struct vhost_memory mem, *newmem;
	struct vhost_memory_region *region;
	struct vhost_umem *newumem, *oldumem;
	unsigned long size = offsetof(struct vhost_memory, regions);
	int i;

	if (copy_from_user(&mem, m, size))
		return -EFAULT;
	if (mem.padding)
		return -EOPNOTSUPP;
	if (mem.nregions > max_mem_regions)
		return -E2BIG;
 
	newmem = vhost_kvzalloc(size + mem.nregions * sizeof(*m->regions));
	memcpy(newmem, &mem, size);
    
	if (copy_from_user(newmem->regions, m->regions, mem.nregions * sizeof *m->regions)) {}
	
    newumem = vhost_umem_alloc();
    
	for (region = newmem->regions; region < newmem->regions + mem.nregions; region++) {
		if (vhost_new_umem_range(newumem, region->guest_phys_addr, region->memory_size,
					 region->guest_phys_addr + region->memory_size - 1, region->userspace_addr,
					 VHOST_ACCESS_RW))
			goto err;
	}

	if (!memory_access_ok(d, newumem, 0))
		goto err;

	oldumem = d->umem;
	d->umem = newumem;

	/* All memory accesses are done under some VQ mutex. */
	for (i = 0; i < d->nvqs; ++i) {
		mutex_lock(&d->vqs[i]->mutex);
		d->vqs[i]->umem = newumem;
		mutex_unlock(&d->vqs[i]->mutex);
	}

	kvfree(newmem);
	vhost_umem_clean(oldumem);
	return 0;
}

```

## vhost_umem_alloc

```c
static struct vhost_umem *vhost_umem_alloc(void)
{
	struct vhost_umem *umem = vhost_kvzalloc(sizeof(*umem));

	umem->umem_tree = RB_ROOT;
	umem->numem = 0;
	INIT_LIST_HEAD(&umem->umem_list);

	return umem;
}
```

## vhost_new_umem_range

```c
static int vhost_new_umem_range(struct vhost_umem *umem, u64 start, u64 size, u64 end,
				u64 userspace_addr, int perm)
{
	struct vhost_umem_node *tmp, *node = kmalloc(sizeof(*node), GFP_ATOMIC);

	if (umem->numem == max_iotlb_entries) {
		tmp = list_first_entry(&umem->umem_list, typeof(*tmp), link);
		vhost_umem_free(umem, tmp);
	}

	node->start = start;
	node->size = size;
	node->last = end;
	node->userspace_addr = userspace_addr;
	node->perm = perm;
	INIT_LIST_HEAD(&node->link);
    /* 插入umem->umem_list链表头 */
	list_add_tail(&node->link, &umem->umem_list);
	vhost_umem_interval_tree_insert(node, &umem->umem_tree);
	umem->numem++;

	return 0;
}
```

# VHOST_SET_VRING_ADDR

```c
long vhost_vring_ioctl(struct vhost_dev *d, int ioctl, void __user *argp)
{
	struct file *eventfp, *filep = NULL;
	bool pollstart = false, pollstop = false;
	struct eventfd_ctx *ctx = NULL;
	u32 __user *idxp = argp;
	struct vhost_virtqueue *vq;
	struct vhost_vring_state s;
	struct vhost_vring_file f;
	struct vhost_vring_addr a;
	u32 idx;
	long r;
	/* 读取index，vhost_vring_addr的第一个元素就是index，是读还是写vq */
	r = get_user(idx, idxp);

	vq = d->vqs[idx];

	mutex_lock(&vq->mutex);

	switch (ioctl) {
	case VHOST_SET_VRING_NUM:
		if (copy_from_user(&s, argp, sizeof s)) { }
		if (!s.num || s.num > 0xffff || (s.num & (s.num - 1))) { }
		vq->num = s.num;
		break;
	case VHOST_SET_VRING_BASE:
		if (copy_from_user(&s, argp, sizeof s)) {
		}
		vq->last_avail_idx = s.num;
		/* Forget the cached index value. */
		vq->avail_idx = vq->last_avail_idx;
		break;
	case VHOST_GET_VRING_BASE:
		s.index = idx;
		s.num = vq->last_avail_idx;
		if (copy_to_user(argp, &s, sizeof s))
			r = -EFAULT;
		break;
	case VHOST_SET_VRING_ADDR:
		if (copy_from_user(&a, argp, sizeof a)) { }
		if (a.flags & ~(0x1 << VHOST_VRING_F_LOG)) { }
		/* 赋值vring的地址 */
		vq->log_used = !!(a.flags & (0x1 << VHOST_VRING_F_LOG));
		vq->desc = (void __user *)(unsigned long)a.desc_user_addr;
		vq->avail = (void __user *)(unsigned long)a.avail_user_addr;
		vq->log_addr = a.log_guest_addr;
		vq->used = (void __user *)(unsigned long)a.used_user_addr;
		break;
	case VHOST_SET_VRING_KICK:
		if (copy_from_user(&f, argp, sizeof f)) { }
		eventfp = f.fd == -1 ? NULL : eventfd_fget(f.fd);
	
		if (eventfp != vq->kick) {
			pollstop = (filep = vq->kick) != NULL;
			pollstart = (vq->kick = eventfp) != NULL;
		} else
			filep = eventfp;
		break;
	case VHOST_SET_VRING_CALL:
		if (copy_from_user(&f, argp, sizeof f)) { }
		eventfp = f.fd == -1 ? NULL : eventfd_fget(f.fd);
	
		if (eventfp != vq->call) {
			filep = vq->call;
			ctx = vq->call_ctx;
			vq->call = eventfp;
			vq->call_ctx = eventfp ?
				eventfd_ctx_fileget(eventfp) : NULL;
		} else
			filep = eventfp;
		break;

	default:
		r = -ENOIOCTLCMD;
	}

	if (pollstop && vq->handle_kick)
		vhost_poll_stop(&vq->poll);

	if (ctx)
		eventfd_ctx_put(ctx);
	if (filep)
		fput(filep);

	if (pollstart && vq->handle_kick)
        /* poll host_notiffer fd, guest准备好会通知host */
		r = vhost_poll_start(&vq->poll, vq->kick);

	mutex_unlock(&vq->mutex);

	if (pollstop && vq->handle_kick)
		vhost_poll_flush(&vq->poll);
	return r;
}
```

# VHOST_SET_VRING_KICK

```c
struct file *eventfp, *filep = NULL;
case VHOST_SET_VRING_KICK:
		if (copy_from_user(&f, argp, sizeof f)) { }
		eventfp = f.fd == -1 ? NULL : eventfd_fget(f.fd);

		if (eventfp != vq->kick) {
			pollstop = (filep = vq->kick) != NULL;
			pollstart = (vq->kick = eventfp) != NULL;
		} else
			filep = eventfp;
		break;
```

# VHOST_NET_SET_BACKEND

```c
static long vhost_net_set_backend(struct vhost_net *n, unsigned index, int fd)
{
	struct socket *sock, *oldsock;
	struct vhost_virtqueue *vq;
	struct vhost_net_virtqueue *nvq;
	struct vhost_net_ubuf_ref *ubufs, *oldubufs = NULL;
	int r;

	mutex_lock(&n->dev.mutex);
	r = vhost_dev_check_owner(&n->dev);

	vq = &n->vqs[index].vq;
	nvq = &n->vqs[index];
	mutex_lock(&vq->mutex);


	/* fd为tun的socket */
	sock = get_socket(fd);

	/* start polling new socket */
	oldsock = vq->private_data;
	if (sock != oldsock) {
		/* ubufs为NULL */
		ubufs = vhost_net_ubuf_alloc(vq, sock && vhost_sock_zcopy(sock));

		vhost_net_disable_vq(n, vq);
		vq->private_data = sock;
		r = vhost_init_used(vq);
	
		r = vhost_net_enable_vq(n, vq);

		oldubufs = nvq->ubufs;
		nvq->ubufs = ubufs;

		n->tx_packets = 0;
		n->tx_zcopy_err = 0;
		n->tx_flush = false;
	}

	mutex_unlock(&vq->mutex);

	if (oldubufs) {
		vhost_net_ubuf_put_wait_and_free(oldubufs);
		mutex_lock(&vq->mutex);
		vhost_zerocopy_signal_used(n, vq);
		mutex_unlock(&vq->mutex);
	}

	if (oldsock) {
		vhost_net_flush_vq(n, index);
		fput(oldsock->file);
	}

	mutex_unlock(&n->dev.mutex);
	return 0;
}
```

## vhost_init_used

```c
int vhost_init_used(struct vhost_virtqueue *vq)
{
	__virtio16 last_used_idx;
	int r;

	vhost_init_is_le(vq);

	r = vhost_update_used_flags(vq);
	vq->signalled_used_valid = false;
    /* 将vq->used->idx赋值给last_used_idx */
	r = vhost_get_used(vq, last_used_idx, &vq->used->idx);
	vq->last_used_idx = vhost16_to_cpu(vq, last_used_idx);
	return 0;
}
```

## vhost_net_enable_vq

```c
static int vhost_net_enable_vq(struct vhost_net *n, struct vhost_virtqueue *vq)
{
	struct vhost_net_virtqueue *nvq = container_of(vq, struct vhost_net_virtqueue, vq);
	struct vhost_poll *poll = n->poll + (nvq - n->vqs);
	struct socket *sock;

	sock = vq->private_data;

	return vhost_poll_start(poll, sock->file);
}
```

### vhost_poll_start

```c
int vhost_poll_start(struct vhost_poll *poll, struct file *file)
{
	unsigned long mask;
	int ret = 0;
	/* vhost_poll_init会给poll->wqh赋值为NULL */
	if (poll->wqh)
		return 0;
	/* poll tun */
	mask = file->f_op->poll(file, &poll->table);
	if (mask)
		vhost_poll_wakeup(&poll->wait, 0, 0, (void *)mask);
	if (mask & POLLERR) {
		if (poll->wqh)
			remove_wait_queue(poll->wqh, &poll->wait);
		ret = -EINVAL;
	}

	return ret;
}
```

### vhost_poll_wakeup

```c
static int vhost_poll_wakeup(wait_queue_t *wait, unsigned mode, int sync,
			     void *key)
{
	struct vhost_poll *poll = container_of(wait, struct vhost_poll, wait);

	if (!((unsigned long)key & poll->mask))
		return 0;

	vhost_poll_queue(poll);
	return 0;
}
```

### vhost_poll_queue

```c
void vhost_poll_queue(struct vhost_poll *poll)
{
	vhost_work_queue(poll->dev, &poll->work);
}
```

### vhost_work_queue

```c
void vhost_work_queue(struct vhost_dev *dev, struct vhost_work *work)
{
	if (!dev->worker)
		return;

	if (!test_and_set_bit(VHOST_WORK_QUEUED, &work->flags)) {
		/* We can only add the work to the list after we're
		 * sure it was not in the list.
		 */
		smp_mb();
        /* work->node添加到dev->work_list中 */
		llist_add(&work->node, &dev->work_list);
        /* 唤醒vhost_worker线程 */
		wake_up_process(dev->worker);
	}
}
```

## vhost_worker

```c
static int vhost_worker(void *data)
{
	struct vhost_dev *dev = data;
	struct vhost_work *work, *work_next;
	struct llist_node *node;
	mm_segment_t oldfs = get_fs();

	set_fs(USER_DS);
	use_mm(dev->mm);

	for (;;) {
		/* mb paired w/ kthread_stop */
		set_current_state(TASK_INTERRUPTIBLE);

		if (kthread_should_stop()) {
			__set_current_state(TASK_RUNNING);
			break;
		}

		node = llist_del_all(&dev->work_list);
		if (!node)
			schedule();/* 如果node为空则进入睡眠 */

		node = llist_reverse_order(node);
		/* make sure flag is seen after deletion */
		smp_wmb();
		llist_for_each_entry_safe(work, work_next, node, node) {
			clear_bit(VHOST_WORK_QUEUED, &work->flags);
			__set_current_state(TASK_RUNNING);
            /* 调用处理函数 */
			work->fn(work);
			if (need_resched())
				schedule();
		}
	}
	unuse_mm(dev->mm);
	set_fs(oldfs);
	return 0;
}

```



# guset收发数据

## guest发送数据

子机调用start_xmit进行发送，将数据放入的vring avail中，然后调用virtqueue_kick通知host，host会poll notiffer，当子机通知后会唤醒vhost_work线程，vhost_work调用work->fn，即handle_tx_kick

### handle_tx_kick

```c
static void handle_tx_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue, poll.work);
	struct vhost_net *net = container_of(vq->dev, struct vhost_net, dev);

	handle_tx(net);
}
```

### handle_tx

```c
static void handle_tx(struct vhost_net *net)
{
	struct vhost_net_virtqueue *nvq = &net->vqs[VHOST_NET_VQ_TX];
	struct vhost_virtqueue *vq = &nvq->vq;
	unsigned out, in, s;
	int head;
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_iov = vq->iov,
		.msg_flags = MSG_DONTWAIT,
	};
	size_t len, total_len = 0;
	int err;
	size_t hdr_size;
	struct socket *sock;
	struct vhost_net_ubuf_ref *uninitialized_var(ubufs);
	bool zcopy, zcopy_used;
	int sent_pkts = 0;

	mutex_lock(&vq->mutex);
	sock = vq->private_data;

	if (!vq_iotlb_prefetch(vq))
		goto out;

	vhost_disable_notify(&net->dev, vq);

	hdr_size = nvq->vhost_hlen;
	zcopy = nvq->ubufs;

	do {
		/* Release DMAs done buffers first */
		if (zcopy)
			vhost_zerocopy_signal_used(net, vq);

		/* If more outstanding DMAs, queue the work. Handle upend_idx wrap around */
		if (unlikely(vhost_exceeds_maxpend(net)))
			break;
		/* 将数据拷贝到msg的msg_iov中 */
		head = vhost_net_tx_get_vq_desc(net, vq, vq->iov, ARRAY_SIZE(vq->iov), &out, &in);
		/* On error, stop handling until the next kick. */
		if (unlikely(head < 0))
			break;
		/* Nothing new?  Wait for eventfd to tell us they refilled. */
		if (head == vq->num) {
			if (unlikely(vhost_enable_notify(&net->dev, vq))) {
				vhost_disable_notify(&net->dev, vq);
				continue;
			}
			break;
		}
		if (in) {
			vq_err(vq, "Unexpected descriptor format for TX: out %d, int %d\n", out, in);
			break;
		}
		/* Skip header. TODO: support TSO. */
		s = move_iovec_hdr(vq->iov, nvq->hdr, hdr_size, out);
		msg.msg_iovlen = out;
		len = iov_length(vq->iov, out);
	
		zcopy_used = zcopy && (len >= VHOST_GOODCOPY_LEN || nvq->upend_idx != nvq->done_idx);

		/* use msg_control to pass vhost zerocopy ubuf info to skb,我们没有使用 zcopy_used */
		if (zcopy_used) {
			vq->heads[nvq->upend_idx].id = cpu_to_vhost32(vq, head);
			if (!vhost_net_tx_select_zcopy(net) || len < VHOST_GOODCOPY_LEN) {
				/* copy don't need to wait for DMA done */
				vq->heads[nvq->upend_idx].len =
							VHOST_DMA_DONE_LEN;
				msg.msg_control = NULL;
				msg.msg_controllen = 0;
				ubufs = NULL;
			} else {
				struct ubuf_info *ubuf;
				ubuf = nvq->ubuf_info + nvq->upend_idx;

				vq->heads[nvq->upend_idx].len =
					VHOST_DMA_IN_PROGRESS;
				ubuf->callback = vhost_zerocopy_callback;
				ubuf->ctx = nvq->ubufs;
				ubuf->desc = nvq->upend_idx;
				msg.msg_control = ubuf;
				msg.msg_controllen = sizeof(ubuf);
				ubufs = nvq->ubufs;
				atomic_inc(&ubufs->refcount);
			}
			nvq->upend_idx = (nvq->upend_idx + 1) % UIO_MAXIOV;
		} else
			msg.msg_control = NULL;

		total_len += len;
		if (total_len < VHOST_NET_WEIGHT && !vhost_vq_avail_empty(&net->dev, vq) &&
		    likely(!vhost_exceeds_maxpend(net))) {
			msg.msg_flags |= MSG_MORE;
		} else {
			msg.msg_flags &= ~MSG_MORE;
		}

		/* TODO: Check specific error and bomb out unless ENOBUFS? 
		 * 调用tun的sendmsg将数据发送出去
		 */
		err = sock->ops->sendmsg(NULL, sock, &msg, len);

		if (err != len)
			pr_debug("Truncated TX packet: len %d != %zd\n", err, len);
		if (!zcopy_used)
            /* 每使用一个desc就通知对方 */
			vhost_add_used_and_signal(&net->dev, vq, head, 0);
		else
			vhost_zerocopy_signal_used(net, vq);
		vhost_net_tx_packet(net);
	} while (likely(!vhost_exceeds_weight(vq, ++sent_pkts, total_len)));
out:
	mutex_unlock(&vq->mutex);
}

```

### vhost_net_tx_get_vq_desc

```c
static int vhost_net_tx_get_vq_desc(struct vhost_net *net, struct vhost_virtqueue *vq,
				    struct iovec iov[], unsigned int iov_size,unsigned int *out_num, unsigned int *in_num)
{
	unsigned long uninitialized_var(endtime);
	int r = vhost_get_vq_desc(vq, vq->iov, ARRAY_SIZE(vq->iov), out_num, in_num, NULL, NULL);

	if (r == vq->num && vq->busyloop_timeout) {	/* 应该表示vq已经满了，如果可以busyloop，则等待一会 */
		preempt_disable();
		endtime = busy_clock() + vq->busyloop_timeout;
		while (vhost_can_busy_poll(vq->dev, endtime) && vhost_vq_avail_empty(vq->dev, vq))
			cpu_relax();
		preempt_enable();
		r = vhost_get_vq_desc(vq, vq->iov, ARRAY_SIZE(vq->iov), out_num, in_num, NULL, NULL);
	}

	return r;
}
```

### vhost_get_vq_desc

```c
int vhost_get_vq_desc(struct vhost_virtqueue *vq, struct iovec iov[], unsigned int iov_size,
		      unsigned int *out_num, unsigned int *in_num, struct vhost_log *log, unsigned int *log_num)
{
	struct vring_desc desc = {0};
	unsigned int i, head, found = 0;
	u16 last_avail_idx;
	__virtio16 avail_idx;
	__virtio16 ring_head;
	int ret, access;

	/* Check it isn't doing very strange things with descriptor numbers. */
	last_avail_idx = vq->last_avail_idx;
    /* 读取vq->avail->idx赋值给avail_idx */
	if (unlikely(vhost_get_avail(vq, avail_idx, &vq->avail->idx))) {
		vq_err(vq, "Failed to access avail idx at %p\n", &vq->avail->idx);
		return -EFAULT;
	}
    /* 大小端转换 */
	vq->avail_idx = vhost16_to_cpu(vq, avail_idx);
	/* 应该是超出了vq的大小 */
	if (unlikely((u16)(vq->avail_idx - last_avail_idx) > vq->num)) {
		vq_err(vq, "Guest moved used index from %u to %u", last_avail_idx, vq->avail_idx);
		return -EFAULT;
	}

	/* If there's nothing new since last we looked, return invalid. */
	if (vq->avail_idx == last_avail_idx)
		return vq->num;

	/* Only get avail ring entries after they have been exposed by guest. */
	smp_rmb();

	/* Grab the next descriptor number they're advertising, and increment the index we've seen. */
    /* 将vq->avail->ring[last_avail_idx & (vq->num - 1)] 读取到ring_head */
	if (unlikely(vhost_get_avail(vq, ring_head, &vq->avail->ring[last_avail_idx & (vq->num - 1)]))) {
		vq_err(vq, "Failed to read head: idx %d address %p\n", last_avail_idx,
		       &vq->avail->ring[last_avail_idx % vq->num]);
		return -EFAULT;
	}
	/* vq->avail->ring[last_avail_idx & (vq->num - 1)]保存的是现在使用的desc的起始索引 */
	head = vhost16_to_cpu(vq, ring_head);

	/* If their number is silly, that's an error. */
	if (unlikely(head >= vq->num)) {
		vq_err(vq, "Guest says index %u > %u is available", head, vq->num);
		return -EINVAL;
	}

	/* When we start there are none of either input nor output. */
	*out_num = *in_num = 0;
	if (unlikely(log))
		*log_num = 0;

	i = head;
	do {
		unsigned iov_count = *in_num + *out_num;
		if (unlikely(i >= vq->num)) {
			vq_err(vq, "Desc index is %u > %u, head = %u", i, vq->num, head);
			return -EINVAL;
		}
		if (unlikely(++found > vq->num)) {
			vq_err(vq, "Loop detected: last one at %u ""vq size %u head %u\n", i, vq->num, head);
			return -EINVAL;
		}
        /* 调用__copy_from_user，将vq->desc + i复制到desc */
		ret = vhost_copy_from_user(vq, &desc, vq->desc + i, sizeof desc);
		/* 没有启用VRING_DESC_F_INDIRECT */
		if (desc.flags & cpu_to_vhost16(vq, VRING_DESC_F_INDIRECT)) {
			ret = get_indirect(vq, iov, iov_size, out_num, in_num, log, log_num, &desc);
			continue;
		}
		/* 判断权限，发送的是和guest设置为VRING_DESC_F_NEXT */
		if (desc.flags & cpu_to_vhost16(vq, VRING_DESC_F_WRITE))
			access = VHOST_ACCESS_WO;
		else
			access = VHOST_ACCESS_RO;
        /* 将desc的数据转成iov */
		ret = translate_desc(vq, vhost64_to_cpu(vq, desc.addr), vhost32_to_cpu(vq, desc.len), 
                             iov + iov_count, iov_size - iov_count, access);

		if (access == VHOST_ACCESS_WO) {
			/* If this is an input descriptor, increment that count. */
			*in_num += ret;
		} else {
			/* If it's an output descriptor, they're all supposed
			 * to come before any input descriptors. */
			*out_num += ret;
		}
	} while ((i = next_desc(vq, &desc)) != -1);

	/* On success, increment avail index. */
	vq->last_avail_idx++;

	/* Assume notifications from guest are disabled at this point,
	 * if they aren't we would need to update avail_event index. */
	BUG_ON(!(vq->used_flags & VRING_USED_F_NO_NOTIFY));
	return head;
}
```

#### translate_desc

```c
/* addr即desc.addr */
static int translate_desc(struct vhost_virtqueue *vq, u64 addr, u32 len,
			  struct iovec iov[], int iov_size, int access)
{
	const struct vhost_umem_node *node;
	struct vhost_dev *dev = vq->dev;
	struct vhost_umem *umem = dev->iotlb ? dev->iotlb : dev->umem;
	struct iovec *_iov;
	u64 s = 0;
	int ret = 0;

	while ((u64)len > s) {
		u64 size;
		if (unlikely(ret >= iov_size)) {
			ret = -ENOBUFS;
			break;
		}
		/* 在umem->umem_tree中查找node */
		node = vhost_umem_interval_tree_iter_first(&umem->umem_tree, addr, addr + len - 1);

		_iov = iov + ret;
		size = node->size - addr + node->start;
		_iov->iov_len = min((u64)len - s, size);
        /* 将addr转换成host的虚拟地址，这里的addr是guset的物理地址，
         * 设置VHOST_SET_MEM_TABLE的时候会把子机的GPA和HVA的对应关系保存到struct vhost_umem_node结构中
         */
		_iov->iov_base = (void __user *)(unsigned long) (node->userspace_addr + addr - node->start);
		s += size;
		addr += size;
		++ret;
	}

	if (ret == -EAGAIN)
		vhost_iotlb_miss(vq, addr, access);
    /* ret表示成功了几个iov */
	return ret;
}
```

#### next_desc

```c
/* Each buffer in the virtqueues is actually a chain of descriptors.  This
 * function returns the next descriptor in the chain, or -1U if we're at the end. */
static unsigned next_desc(struct vhost_virtqueue *vq, struct vring_desc *desc)
{
	unsigned int next;

	/* If this descriptor says it doesn't chain, we're done. */
	if (!(desc->flags & cpu_to_vhost16(vq, VRING_DESC_F_NEXT)))
		return -1U;

	/* Check they're not leading us off end of descriptors. */
	next = vhost16_to_cpu(vq, desc->next);
	/* Make sure compiler knows to grab that: we don't want it changing! */
	/* We will use the result as an index in an array, so most
	 * architectures only need a compiler barrier here. */
	read_barrier_depends();

	return next;
}
```

#### vhost_add_used_and_signal

```c
/* And here's the combo meal deal.  Supersize me! */
void vhost_add_used_and_signal(struct vhost_dev *dev, struct vhost_virtqueue *vq,
			       unsigned int head, int len)
{
    /* len为0 */
	vhost_add_used(vq, head, len);
	vhost_signal(dev, vq);
}
```

#### vhost_add_used

```c
/* After we've used one of their buffers, we tell them about it.  We'll then
 * want to notify the guest, using eventfd. */
int vhost_add_used(struct vhost_virtqueue *vq, unsigned int head, int len)
{
	struct vring_used_elem heads = {
		cpu_to_vhost32(vq, head),
		cpu_to_vhost32(vq, len)
	};

	return vhost_add_used_n(vq, &heads, 1);
}
```

#### vhost_add_used_n

```c
/* After we've used one of their buffers, we tell them about it.  We'll then
 * want to notify the guest, using eventfd. */
int vhost_add_used_n(struct vhost_virtqueue *vq, struct vring_used_elem *heads, unsigned count)
{
	int start, n, r;

	start = vq->last_used_idx & (vq->num - 1);
	n = vq->num - start;
	if (n < count) {
		r = __vhost_add_used_n(vq, heads, n);
		if (r < 0)
			return r;
		heads += n;
		count -= n;
	}
	r = __vhost_add_used_n(vq, heads, count);

	/* Make sure buffer is written before we update index. */
	smp_wmb();
	if (vhost_put_user(vq, cpu_to_vhost16(vq, vq->last_used_idx), &vq->used->idx)) {
		vq_err(vq, "Failed to increment used idx");
		return -EFAULT;
	}
	if (unlikely(vq->log_used)) {
		/* Log used index update. */
		log_write(vq->log_base, vq->log_addr + offsetof(struct vring_used, idx), sizeof vq->used->idx);
		if (vq->log_ctx)
			eventfd_signal(vq->log_ctx, 1);
	}
	return r;
}
```

#### __vhost_add_used_n

```c
static int __vhost_add_used_n(struct vhost_virtqueue *vq, struct vring_used_elem *heads,
			    unsigned count)
{
	struct vring_used_elem __user *used;
	u16 old, new;
	int start;

	start = vq->last_used_idx & (vq->num - 1);
	used = vq->used->ring + start;
	if (count == 1) {
        /* 将 heads[0].id赋值给used->id，heads[0].id就是avail->vring保存的desc的idx */
		if (vhost_put_user(vq, heads[0].id, &used->id)) {
			vq_err(vq, "Failed to write used id");
			return -EFAULT;
		}
		if (vhost_put_user(vq, heads[0].len, &used->len)) {
			vq_err(vq, "Failed to write used len");
			return -EFAULT;
		}
	} else if (vhost_copy_to_user(vq, used, heads, count * sizeof *used)) {
		vq_err(vq, "Failed to write used");
		return -EFAULT;
	}
	if (unlikely(vq->log_used)) {
		/* Make sure data is seen before log. */
		smp_wmb();
		/* Log used ring entry write. */
		log_write(vq->log_base, vq->log_addr +
			   ((void __user *)used - (void __user *)vq->used), count * sizeof *used);
	}
	old = vq->last_used_idx;
    /* 更新vq->last_used_idx */
	new = (vq->last_used_idx += count);
	/* If the driver never bothers to signal in a very long while,
	 * used index might wrap around. If that happens, invalidate
	 * signalled_used index we stored. TODO: make sure driver
	 * signals at least once in 2^16 and remove this. */
	if (unlikely((u16)(new - vq->signalled_used) < (u16)(new - old)))
		vq->signalled_used_valid = false;
	return 0;
}
```

#### vhost_signal

```c
/* This actually signals the guest, using eventfd. */
void vhost_signal(struct vhost_dev *dev, struct vhost_virtqueue *vq)
{
	/* Signal the Guest tell them we used something up. */
	if (vq->call_ctx && vhost_notify(dev, vq))
		eventfd_signal(vq->call_ctx, 1);/* 通知子机 */
}
```

#### vhost_vq_avail_empty

```c
/* return true if we're sure that avaiable ring is empty */
bool vhost_vq_avail_empty(struct vhost_dev *dev, struct vhost_virtqueue *vq)
{
	__virtio16 avail_idx;
	int r;
	
    /* 现在的avail_idx和上次的一样说明没有数据更新 */
	if (vq->avail_idx != vq->last_avail_idx)
		return false;
	/* 读取vq->avail->idx赋值给avail_idx */
	r = vhost_get_avail(vq, avail_idx, &vq->avail->idx);
	if (unlikely(r))
		return false;
    /* 大小端转换 */
	vq->avail_idx = vhost16_to_cpu(vq, avail_idx);
	/* 因为是guest那边idx先加1，所以guet的avail->idx在发送数据的时候总是会比last_avail_idx大
     * 在没有回环的情况下
     */
	return vq->avail_idx == vq->last_avail_idx;
}
```

## guest接收数据

vhost_net_set_backend-->vhost_net_enable_vq

```c
static int vhost_net_enable_vq(struct vhost_net *n, struct vhost_virtqueue *vq)
{
	struct vhost_net_virtqueue *nvq = container_of(vq, struct vhost_net_virtqueue, vq);
	struct vhost_poll *poll = n->poll + (nvq - n->vqs);
	struct socket *sock;

	sock = vq->private_data;
	/* 这里poll的是tun设备的file */
	return vhost_poll_start(poll, sock->file);
}
```

当通过tun的网卡发送数据，就会调用tun_net_xmit，tun_net_xmit会唤醒poll的进程，vhost_poll_start然后唤醒vhost-worker，vhost-worker会执行handle_rx_net

### handle_rx_net

```c
static void handle_rx_net(struct vhost_work *work)
{
	struct vhost_net *net = container_of(work, struct vhost_net, poll[VHOST_NET_VQ_RX].work);
	handle_rx(net);
}
```

### handle_rx

```c
/* Expects to be always run from workqueue - which acts as
 * read-size critical section for our kind of RCU. */
static void handle_rx(struct vhost_net *net)
{
	struct vhost_net_virtqueue *nvq = &net->vqs[VHOST_NET_VQ_RX];
	struct vhost_virtqueue *vq = &nvq->vq;
	unsigned uninitialized_var(in), log;
	struct vhost_log *vq_log;
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_control = NULL, /* FIXME: get and handle RX aux data. */
		.msg_controllen = 0,
		.msg_iov = vq->iov,
		.msg_flags = MSG_DONTWAIT,
	};
	struct virtio_net_hdr_mrg_rxbuf hdr = {
		.hdr.flags = 0,
		.hdr.gso_type = VIRTIO_NET_HDR_GSO_NONE
	};
	size_t total_len = 0;
	int err, mergeable;
	s16 headcount;
	size_t vhost_hlen, sock_hlen;
	size_t vhost_len, sock_len;
	struct socket *sock;
	int recv_pkts = 0;

	mutex_lock(&vq->mutex);
	sock = vq->private_data;


	if (!vq_iotlb_prefetch(vq))
		goto out;

	vhost_disable_notify(&net->dev, vq);

	vhost_hlen = nvq->vhost_hlen;
	sock_hlen = nvq->sock_hlen;

	vq_log = unlikely(vhost_has_feature(vq, VHOST_F_LOG_ALL)) ? vq->log : NULL;
	mergeable = vhost_has_feature(vq, VIRTIO_NET_F_MRG_RXBUF);

	do {
		sock_len = vhost_net_rx_peek_head_len(net, sock->sk);

		if (!sock_len)
			break;
		sock_len += sock_hlen;
		vhost_len = sock_len + vhost_hlen;
		headcount = get_rx_bufs(vq, vq->heads, vhost_len, &in, vq_log, &log,
					likely(mergeable) ? UIO_MAXIOV : 1);

		/* On overrun, truncate and discard */
		if (unlikely(headcount > UIO_MAXIOV)) {
			msg.msg_iovlen = 1;
			err = sock->ops->recvmsg(NULL, sock, &msg, 1, MSG_DONTWAIT | MSG_TRUNC);
			pr_debug("Discarded rx packet: len %zd\n", sock_len);
			continue;
		}
		/* OK, now we need to know about added descriptors. */
		if (!headcount) {
			if (unlikely(vhost_enable_notify(&net->dev, vq))) {
				/* They have slipped one in as we were doing that: check again. */
				vhost_disable_notify(&net->dev, vq);
				continue;
			}
			/* Nothing new?  Wait for eventfd to tell us
			 * they refilled. */
			break;
		}
		/* We don't need to be notified again. */
		if (unlikely((vhost_hlen)))
			/* Skip header. TODO: support TSO. */
			move_iovec_hdr(vq->iov, nvq->hdr, vhost_hlen, in);
		else
			/* Copy the header for use in VIRTIO_NET_F_MRG_RXBUF:
			 * needed because recvmsg can modify msg_iov. */
			copy_iovec_hdr(vq->iov, nvq->hdr, sock_hlen, in);
		msg.msg_iovlen = in;
        /* get_rx_bufs函数中会将vq->iov设置为子机desc中的addr对应的host虚拟地址，
         * 因为msg.msg_iov = vq->iov，所以recvmsg向msg中填充数据，就填充到了desc的addr中了
         */
		err = sock->ops->recvmsg(NULL, sock, &msg, sock_len, MSG_DONTWAIT | MSG_TRUNC);
		/* Userspace might have consumed the packet meanwhile:
		 * it's not supposed to do this usually, but might be hard
		 * to prevent. Discard data we got (if any) and keep going. */
		if (unlikely(err != sock_len)) {
			pr_debug("Discarded rx packet: len %d, expected %zd\n", err, sock_len);
			vhost_discard_vq_desc(vq, headcount);
			continue;
		}
		if (unlikely(vhost_hlen) &&
		    memcpy_toiovecend(nvq->hdr, (unsigned char *)&hdr, 0, vhost_hlen)) {
			vq_err(vq, "Unable to write vnet_hdr at addr %p\n", vq->iov->iov_base);
			break;
		}
		/* TODO: Should check and handle checksum. */
		hdr.num_buffers = cpu_to_vhost16(vq, headcount);
		if (likely(mergeable) && memcpy_toiovecend(nvq->hdr, (void *)&hdr.num_buffers,
				      offsetof(typeof(hdr), num_buffers), sizeof hdr.num_buffers)) {
			vq_err(vq, "Failed num_buffers write");
			vhost_discard_vq_desc(vq, headcount);
			break;
		}
		vhost_add_used_and_signal_n(&net->dev, vq, vq->heads, headcount);
		if (unlikely(vq_log))
			vhost_log_write(vq, vq_log, log, vhost_len);
		total_len += vhost_len;
	} while (likely(!vhost_exceeds_weight(vq, ++recv_pkts, total_len)));
out:
	mutex_unlock(&vq->mutex);
}
```

### get_rx_bufs

```c
static int get_rx_bufs(struct vhost_virtqueue *vq, struct vring_used_elem *heads,int datalen,
                       unsigned *iovcount, struct vhost_log *log, unsigned *log_num, unsigned int quota)
{
	unsigned int out, in;
	int seg = 0;
	int headcount = 0;
	unsigned d;
	int r, nlogs = 0;
	/* len is always initialized before use since we are always called with
	 * datalen > 0.
	 */
	u32 uninitialized_var(len);

	while (datalen > 0 && headcount < quota) {
        /* 该函数中会把vq->iov设置为guset的GAP对应的物理地址 
         * vhost_get_vq_desc函数里面会获取vring->avail[],这个是谁提供的呢
         * 其实是virtio_net驱动的try_fill_recv提供的desc。
         */
		r = vhost_get_vq_desc(vq, vq->iov + seg, ARRAY_SIZE(vq->iov) - seg, &out,
				      &in, log, log_num);
		d = r;

		if (unlikely(log)) {
			nlogs += *log_num;
			log += *log_num;
		}
        /* 更新id */
		heads[headcount].id = cpu_to_vhost32(vq, d);
		len = iov_length(vq->iov + seg, in);
        /* 更新len */
		heads[headcount].len = cpu_to_vhost32(vq, len);
		datalen -= len;
		++headcount;
		seg += in;
	}
	heads[headcount - 1].len = cpu_to_vhost32(vq, len + datalen);
	*iovcount = seg;
	if (unlikely(log))
		*log_num = nlogs;

	return headcount;
}
```

### vhost_add_used_and_signal_n

```c
void vhost_add_used_and_signal_n(struct vhost_dev *dev, struct vhost_virtqueue *vq,
				 struct vring_used_elem *heads, unsigned count)
{
    /* 更新使用的desc到used ring中，count是使用desc的个数 */
	vhost_add_used_n(vq, heads, count);
    /* 通知子机 */
	vhost_signal(dev, vq);
}
```

## try_fill_recv

try_fill_recv在virtio_net 中调用的地方：

virtnet_probe-->try_fill_recv

virtnet_open-->try_fill_recv

virtnet_poll-->try_fill_recvl 

virtnet_poll是收包函数，每次收包的时候会try_fill_recv调用进行avail->desc填充

try_fill_recv-->add_recvbuf_mergeable

```c
static int add_recvbuf_mergeable(struct receive_queue *rq, gfp_t gfp)
{
	struct page *page;
	int err;
	/* 获取一页内存 */
	page = get_a_page(rq, gfp);
    /* 初始化sg */
	sg_init_one(rq->sg, page_address(page), PAGE_SIZE);
	err = virtqueue_add_inbuf(rq->vq, rq->sg, 1, page, gfp);
	return err;
}
```

### virtqueue_add_inbuf

```c
int virtqueue_add_inbuf(struct virtqueue *vq, struct scatterlist *sg, unsigned int num, void *data,
			gfp_t gfp)
{	
    /* 只增加in的sg，没有out的sg，sg总共1个 */
	return virtqueue_add(vq, &sg, num, 0, 1, data, gfp);
}
```

### virtqueue_add

```c
static inline int virtqueue_add(struct virtqueue *_vq, struct scatterlist *sgs[],
				unsigned int total_sg, unsigned int out_sgs, unsigned int in_sgs,
				void *data, gfp_t gfp)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	struct scatterlist *sg;
	struct vring_desc *desc;
	unsigned int i, n, avail, descs_used, uninitialized_var(prev), err_idx;
	int head;
	bool indirect;

	START_USE(vq);

	head = vq->free_head;

	/* If the host supports indirect descriptor tables, and we have multiple
	 * buffers, then go indirect. FIXME: tune this threshold 
	 */
	if (vq->indirect && total_sg > 1 && vq->vq.num_free)
		desc = alloc_indirect(_vq, total_sg, gfp);
	else
		desc = NULL;

	if (desc) {
		/* Use a single buffer which doesn't continue */
		indirect = true;
		/* Set up rest to use this indirect table. */
		i = 0;
		descs_used = 1;
	} else {
		indirect = false;
		desc = vq->vring.desc;
		i = head;
		descs_used = total_sg;
	}

	/* 处理out_sgs，此处out_sgs为0 */
	for (n = 0; n < out_sgs; n++) {
		for (sg = sgs[n]; sg; sg = sg_next(sg)) {
			dma_addr_t addr = vring_map_one_sg(vq, sg, DMA_TO_DEVICE);
			if (vring_mapping_error(vq, addr))
				goto unmap_release;

			/* 默认device为只读，VRING_DESC_F_NEXT表示desc连续 
			* 一个sg一个desc
			*/
			desc[i].flags = cpu_to_virtio16(_vq->vdev, VRING_DESC_F_NEXT);
			desc[i].addr = cpu_to_virtio64(_vq->vdev, addr);
			desc[i].len = cpu_to_virtio32(_vq->vdev, sg->length);
			prev = i;
			i = virtio16_to_cpu(_vq->vdev, desc[i].next);
		}
	}
	/* 处理in_sgs，此处in_sgs为1 */
	for (; n < (out_sgs + in_sgs); n++) {
		for (sg = sgs[n]; sg; sg = sg_next(sg)) {
			/* 返回sg对应的物理地址 */
			dma_addr_t addr = vring_map_one_sg(vq, sg, DMA_FROM_DEVICE);
			if (vring_mapping_error(vq, addr))
				goto unmap_release;
			/* 标记为device可以写入 */
			desc[i].flags = cpu_to_virtio16(_vq->vdev, VRING_DESC_F_NEXT | VRING_DESC_F_WRITE);
			desc[i].addr = cpu_to_virtio64(_vq->vdev, addr);
			desc[i].len = cpu_to_virtio32(_vq->vdev, sg->length);
			prev = i;
			i = virtio16_to_cpu(_vq->vdev, desc[i].next);
		}
	}

	/* Last one doesn't continue.设置最后一个不连续 */
	desc[prev].flags &= cpu_to_virtio16(_vq->vdev, ~VRING_DESC_F_NEXT);

	if (indirect) {
		/* Now that the indirect table is filled in, map it. */
		dma_addr_t addr = vring_map_single(
			vq, desc, total_sg * sizeof(struct vring_desc),
			DMA_TO_DEVICE);
		if (vring_mapping_error(vq, addr))
			goto unmap_release;

		vq->vring.desc[head].flags = cpu_to_virtio16(_vq->vdev, VRING_DESC_F_INDIRECT);
		vq->vring.desc[head].addr = cpu_to_virtio64(_vq->vdev, addr);
		vq->vring.desc[head].len = cpu_to_virtio32(_vq->vdev, total_sg * sizeof(struct vring_desc));
	}

	/* We're using some buffers from the free list. 减去已经用掉的*/
	vq->vq.num_free -= descs_used;

	/* Update free pointer */
	if (indirect)
		vq->free_head = virtio16_to_cpu(_vq->vdev, vq->vring.desc[head].next);
	else
		vq->free_head = i;/* 更新free_head,i为最后一个desc的中next的索引 */

	/* Store token and indirect buffer state. */
	vq->desc_state[head].data = data;
	if (indirect)
		vq->desc_state[head].indir_desc = desc;

	/* Put entry in available array (but don't update avail->idx until they
	 * do sync). 
	 */
	/* 获取avail->idx，avail->idx为该次可以使用的vring的idx索引 */
	avail = virtio16_to_cpu(_vq->vdev, vq->vring.avail->idx) & (vq->vring.num - 1);
	/* 
	 * 将head，即desc开始的索引放到avail的ring数组中，avail->ring[]里面存放的是desc开始的索引
	 */
	vq->vring.avail->ring[avail] = cpu_to_virtio16(_vq->vdev, head);

	/* Descriptors and available array need to be set before we expose the
	 * new available array entries. */
	virtio_wmb(vq->weak_barriers);
	/* 更新avail->idx为当前使用idx+1，即下一次使用的vring idx */
	vq->vring.avail->idx = cpu_to_virtio16(_vq->vdev, virtio16_to_cpu(_vq->vdev, vq->vring.avail->idx) + 1);
	vq->num_added++;

	pr_debug("Added buffer head %i to %p\n", head, vq);
	END_USE(vq);

	/* This is very unlikely, but theoretically possible.  Kick
	 * just in case. */
	if (unlikely(vq->num_added == (1 << 16) - 1))
		virtqueue_kick(_vq);

	return 0;
}
```



## 子机收包

收到包后会调用中断函数vring_interrupt

### vring_interrupt

```c
irqreturn_t vring_interrupt(int irq, void *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	pr_debug("virtqueue callback for %p (%p)\n", vq, vq->vq.callback);
	if (vq->vq.callback)
		vq->vq.callback(&vq->vq);/* 调用skb_recv_done */

	return IRQ_HANDLED;
}
```

### skb_recv_done

```c
static void skb_recv_done(struct virtqueue *rvq)
{
	struct virtnet_info *vi = rvq->vdev->priv;
	struct receive_queue *rq = &vi->rq[vq2rxq(rvq)];

	/* Schedule NAPI, Suppress further interrupts if successful. */
	if (napi_schedule_prep(&rq->napi)) {
		virtqueue_disable_cb(rvq);
		__napi_schedule(&rq->napi);
	}
}
```

### __napi_schedule

```c
void __napi_schedule(struct napi_struct *n)
{
	unsigned long flags;
	local_irq_save(flags);
	____napi_schedule(this_cpu_ptr(&softnet_data), n);
	local_irq_restore(flags);
}
```

### \____napi_schedule

```c
static inline void ____napi_schedule(struct softnet_data *sd,
				     struct napi_struct *napi)
{
	/* napi->poll_list挂到该cpu的sd->poll_list中 */
	list_add_tail(&napi->poll_list, &sd->poll_list);
	/* 发起软中断 */
	__raise_softirq_irqoff(NET_RX_SOFTIRQ);
}
```

最终会调用到virtnet_poll

### virtnet_poll

```c
static int virtnet_poll(struct napi_struct *napi, int budget)
{
	struct receive_queue *rq = container_of(napi, struct receive_queue, napi);
	struct virtnet_info *vi = rq->vq->vdev->priv;
	void *buf;
	unsigned int r, len, received = 0;

	while (received < budget &&
	       (buf = virtqueue_get_buf(rq->vq, &len)) != NULL) {
		receive_buf(vi, rq, buf, len);
		--rq->num;
		received++;
	}

	if (rq->num < rq->max / 2) {
		if (!try_fill_recv(vi, rq, GFP_ATOMIC))
			schedule_delayed_work(&vi->refill, 0);
	}

	/* Out of packets? */
	if (received < budget) {
		r = virtqueue_enable_cb_prepare(rq->vq);
		napi_complete_done(napi, received);
		if (unlikely(virtqueue_poll(rq->vq, r)) &&
		    napi_schedule_prep(napi)) {
			virtqueue_disable_cb(rq->vq);
			__napi_schedule(napi);
		}
	}

	return received;
}
```

### receive_buf

```c
static void receive_buf(struct virtnet_info *vi, struct receive_queue *rq,
			void *buf, unsigned int len)
{
	struct net_device *dev = vi->dev;
	struct virtnet_stats *stats = this_cpu_ptr(vi->stats);
	struct sk_buff *skb;
	struct virtio_net_hdr_mrg_rxbuf *hdr;

	if (unlikely(len < vi->hdr_len + ETH_HLEN)) {
		pr_debug("%s: short packet %i\n", dev->name, len);
		dev->stats.rx_length_errors++;
		if (vi->mergeable_rx_bufs || vi->big_packets)
			give_pages(rq, buf);
		else
			dev_kfree_skb(buf);
		return;
	}
	if (vi->mergeable_rx_bufs)
		skb = receive_mergeable(dev, vi, rq, buf, len);
	else if (vi->big_packets)
		skb = receive_big(dev, vi, rq, buf, len);
	else
		skb = receive_small(vi, buf, len);

	hdr = skb_vnet_hdr(skb);

	u64_stats_update_begin(&stats->rx_syncp);
	stats->rx_bytes += skb->len;
	stats->rx_packets++;
	u64_stats_update_end(&stats->rx_syncp);

	if (hdr->hdr.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
		pr_debug("Needs csum!\n");
	} else if (hdr->hdr.flags & VIRTIO_NET_HDR_F_DATA_VALID) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	skb->protocol = eth_type_trans(skb, dev);
	pr_debug("Receiving skb proto 0x%04x len %i type %i\n", ntohs(skb->protocol), skb->len, skb->pkt_type);

	if (hdr->hdr.gso_type != VIRTIO_NET_HDR_GSO_NONE) {
		pr_debug("GSO!\n");
		switch (hdr->hdr.gso_type & ~VIRTIO_NET_HDR_GSO_ECN) {
		case VIRTIO_NET_HDR_GSO_TCPV4:
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
			break;
		case VIRTIO_NET_HDR_GSO_UDP:
			skb_shinfo(skb)->gso_type = SKB_GSO_UDP;
			break;
		case VIRTIO_NET_HDR_GSO_TCPV6:
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
			break;
		default:
			net_warn_ratelimited("%s: bad gso type %u.\n", dev->name, hdr->hdr.gso_type);
			goto frame_err;
		}

		if (hdr->hdr.gso_type & VIRTIO_NET_HDR_GSO_ECN)
			skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;

		skb_shinfo(skb)->gso_size = virtio16_to_cpu(vi->vdev, hdr->hdr.gso_size);


		/* Header must be checked, and gso_segs computed. */
		skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
		skb_shinfo(skb)->gso_segs = 0;
	}
	/* 将包放入协议栈 */
	napi_gro_receive(&rq->napi, skb);
	return;
}
```

### receive_mergeable

```c
static struct sk_buff *receive_mergeable(struct net_device *dev, struct virtnet_info *vi,
					 struct receive_queue *rq, void *buf, unsigned int len)
{
	struct virtio_net_hdr_mrg_rxbuf *hdr = page_address(buf);
	u16 num_buf = virtio16_to_cpu(rq->vq->vdev, hdr->num_buffers);
	struct page *page = buf;
	struct sk_buff *skb = page_to_skb(vi, rq, page, len);
	int i;

	while (--num_buf) {
		i = skb_shinfo(skb)->nr_frags;

		page = virtqueue_get_buf(rq->vq, &len);

		if (len > PAGE_SIZE)
			len = PAGE_SIZE;

		set_skb_frag(skb, page, 0, &len);

		--rq->num;
	}
	return skb;
}
```

### virtqueue_get_buf

```c
void *virtqueue_get_buf(struct virtqueue *_vq, unsigned int *len)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	void *ret;
	unsigned int i;
	u16 last_used;

	START_USE(vq);

	/* Only get used array entries after they have been exposed by host. */
	virtio_rmb(vq->weak_barriers);

    /* 获取 last_used */
	last_used = (vq->last_used_idx & (vq->vring.num - 1));
    /* 获取used->ring[last_used].id，即desc的索引 */
	i = virtio32_to_cpu(_vq->vdev, vq->vring.used->ring[last_used].id);
	*len = virtio32_to_cpu(_vq->vdev, vq->vring.used->ring[last_used].len);

	/* detach_buf clears data, so grab it now. */
    /* virtqueue_add会给vq->desc_state[i].data赋值 */
	ret = vq->desc_state[i].data;
	detach_buf(vq, i);
	vq->last_used_idx++;
	/* If we expect an interrupt for the next entry, tell host
	 * by writing event index and flush out the write before
	 * the read in the next get_buf call. */
	if (!(vq->vring.avail->flags & cpu_to_virtio16(_vq->vdev, VRING_AVAIL_F_NO_INTERRUPT))) {
		vring_used_event(&vq->vring) = cpu_to_virtio16(_vq->vdev, vq->last_used_idx);
		virtio_mb(vq->weak_barriers);
	}

#ifdef DEBUG
	vq->last_add_time_valid = false;
#endif

	END_USE(vq);
	return ret;
}
```

### detach_buf

```c
static void detach_buf(struct vring_virtqueue *vq, unsigned int head)
{
	unsigned int i, j;
	u16 nextflag = cpu_to_virtio16(vq->vq.vdev, VRING_DESC_F_NEXT);

	/* Clear data ptr. */
	vq->desc_state[head].data = NULL;

	/* Put back on free list: unmap first-level descriptors and find end */
	i = head;
	/* 将用过的desc释放 */
	while (vq->vring.desc[i].flags & nextflag) {
		vring_unmap_one(vq, &vq->vring.desc[i]);
		i = virtio16_to_cpu(vq->vq.vdev, vq->vring.desc[i].next);
		vq->vq.num_free++;
	}

	vring_unmap_one(vq, &vq->vring.desc[i]);
	vq->vring.desc[i].next = cpu_to_virtio16(vq->vq.vdev, vq->free_head);
	vq->free_head = head;

	/* Plus final descriptor */
	vq->vq.num_free++;

	/* Free the indirect table, if any, now that it's unmapped. */
	if (vq->desc_state[head].indir_desc) {
		struct vring_desc *indir_desc = vq->desc_state[head].indir_desc;
		u32 len = virtio32_to_cpu(vq->vq.vdev, vq->vring.desc[head].len);

		for (j = 0; j < len / sizeof(struct vring_desc); j++)
			vring_unmap_one(vq, &indir_desc[j]);

		kfree(vq->desc_state[head].indir_desc);
		vq->desc_state[head].indir_desc = NULL;
	}
}
```

