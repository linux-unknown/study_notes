# TUN

## tun_init

```c
static int __init tun_init(void)
{
	int ret = 0;

	pr_info("%s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	pr_info("%s\n", DRV_COPYRIGHT);
	/* 注册netlink */
	ret = rtnl_link_register(&tun_link_ops);
    /* 注册字符设备 */
	ret = misc_register(&tun_miscdev);
	register_netdevice_notifier_rh(&tun_notifier_block);
}
```

## tun_get_socket

```c
struct socket *tun_get_socket(struct file *file)
{
	struct tun_file *tfile;
	if (file->f_op != &tun_fops)
		return ERR_PTR(-EINVAL);
	tfile = file->private_data;
	if (!tfile)
		return ERR_PTR(-EBADFD);
	return &tfile->socket;
}
```

## tun_miscdev

```c
static struct miscdevice tun_miscdev = {
	.minor = TUN_MINOR,
	.name = "tun",
	.nodename = "net/tun",
	.fops = &tun_fops,
};
```

## tun_fops

```c

static const struct file_operations tun_fops = {
	.owner	= THIS_MODULE,
	.llseek = no_llseek,
	.read  = do_sync_read,
	.aio_read  = tun_chr_aio_read,
	.write = do_sync_write,
	.aio_write = tun_chr_aio_write,
	.poll	= tun_chr_poll,
	.unlocked_ioctl	= tun_chr_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tun_chr_compat_ioctl,
#endif
	.open	= tun_chr_open,
	.release = tun_chr_close,
	.fasync = tun_chr_fasync
};
```

### tun_chr_open

```c
static int tun_chr_open(struct inode *inode, struct file * file)
{
	struct tun_file *tfile;

	DBG1(KERN_INFO, "tunX: tun_chr_open\n");

	tfile = (struct tun_file *)sk_alloc(&init_net, AF_UNSPEC, GFP_KERNEL, &tun_proto);
	rcu_assign_pointer(tfile->tun, NULL);
	tfile->net = get_net(current->nsproxy->net_ns);
	tfile->flags = 0;
	tfile->ifindex = 0;

	rcu_assign_pointer(tfile->socket.wq, &tfile->wq);
	init_waitqueue_head(&tfile->wq.wait);

	tfile->socket.file = file;
	tfile->socket.ops = &tun_socket_ops;

	sock_init_data(&tfile->socket, &tfile->sk);
	sk_change_net(&tfile->sk, tfile->net);

	tfile->sk.sk_write_space = tun_sock_write_space;
	tfile->sk.sk_sndbuf = INT_MAX;
	/* 将 tfile保存到file->private_data中 */
	file->private_data = tfile;
	set_bit(SOCK_EXTERNALLY_ALLOCATED, &tfile->socket.flags);
	INIT_LIST_HEAD(&tfile->next);

	sock_set_flag(&tfile->sk, SOCK_ZEROCOPY);

	return 0;
}
```

### tun_socket_ops

```
/* Ops structure to mimic raw sockets with tun */
static const struct proto_ops tun_socket_ops = {
	.peek_len = tun_peek_len,
	.sendmsg = tun_sendmsg,
	.recvmsg = tun_recvmsg,
	.release = tun_release,
};
```

网卡的创建

ioctl调用TUNSETIFF会创建tun网络设备

tun_chr_ioctl-->__tun_chr_ioctl

```c
static long __tun_chr_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg, int ifreq_len)
{
	struct tun_file *tfile = file->private_data;
	struct tun_struct *tun;
	void __user* argp = (void __user*)arg;
	struct ifreq ifr;
	kuid_t owner;
	kgid_t group;
	int sndbuf;
	int vnet_hdr_sz;
	unsigned int ifindex;
	int le;
	int ret;

	if (cmd == TUNSETIFF || cmd == TUNSETQUEUE || _IOC_TYPE(cmd) == 0x89) {
		if (copy_from_user(&ifr, argp, ifreq_len))
			return -EFAULT;
	} else {
		memset(&ifr, 0, sizeof(ifr));
	}

	ret = 0;
	rtnl_lock();
	tun = __tun_get(tfile);
	if (cmd == TUNSETIFF && !tun) {
		ifr.ifr_name[IFNAMSIZ-1] = '\0';

		ret = tun_set_iff(tfile->net, file, &ifr);

		if (copy_to_user(argp, &ifr, ifreq_len))
			ret = -EFAULT;
		goto unlock;
	}
```



```c
static int tun_set_iff(struct net *net, struct file *file, struct ifreq *ifr)
{
	struct tun_struct *tun;
	struct tun_file *tfile = file->private_data;
	struct net_device *dev;
	int err;

	if (tfile->detached)
		return -EINVAL;

	dev = __dev_get_by_name(net, ifr->ifr_name);
	if (dev) { /* 如果设备已经存在 */
		
	} else {
		char *name;
		unsigned long flags = 0;
		int queues = ifr->ifr_flags & IFF_MULTI_QUEUE ? MAX_TAP_QUEUES : 1;
		/* 权限检查 */
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			return -EPERM;
		err = security_tun_dev_create();
		
		/* Set dev type，我们是 IFF_TAP */
		if (ifr->ifr_flags & IFF_TUN) {
			/* TUN device */
			flags |= IFF_TUN;
			name = "tun%d";
		} else if (ifr->ifr_flags & IFF_TAP) {
			/* TAP device */
			flags |= IFF_TAP;
			name = "tap%d";
		} else
			return -EINVAL;
		/* libvirt指定了ifr->ifr_name名称 */
		if (*ifr->ifr_name)
			name = ifr->ifr_name;
		/* 创建网路设备 */
		dev = alloc_netdev_mqs(sizeof(struct tun_struct), name, tun_setup, queues, queues);

		if (!dev)
			return -ENOMEM;

		dev_net_set(dev, net);
		dev->rtnl_link_ops = &tun_link_ops;
		dev->ifindex = tfile->ifindex;

		tun = netdev_priv(dev);
		tun->dev = dev;
		tun->flags = flags;
		tun->txflt.count = 0;
		tun->vnet_hdr_sz = sizeof(struct virtio_net_hdr);

		tun->align = NET_SKB_PAD;
		tun->filter_attached = false;
		tun->sndbuf = tfile->socket.sk->sk_sndbuf;
		tun->rx_batched = 0;

		tun->pcpu_stats = netdev_alloc_pcpu_stats(struct tun_pcpu_stats);
	

		spin_lock_init(&tun->lock);

		err = security_tun_dev_alloc_security(&tun->security);

		tun_net_init(dev);

		err = tun_flow_init(tun);
	
		dev->hw_features = NETIF_F_SG | NETIF_F_FRAGLIST | TUN_USER_FEATURES | NETIF_F_HW_VLAN_CTAG_TX |
				   NETIF_F_HW_VLAN_STAG_TX;
		dev->features = dev->hw_features | NETIF_F_LLTX;
		dev->vlan_features = dev->features & ~(NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_STAG_TX);

		INIT_LIST_HEAD(&tun->disabled);
		err = tun_attach(tun, file, false);

		/* 注册网路设备 */
		err = register_netdevice(tun->dev);

		if (device_create_file(&tun->dev->dev, &dev_attr_tun_flags) ||
		    device_create_file(&tun->dev->dev, &dev_attr_owner) ||
		    device_create_file(&tun->dev->dev, &dev_attr_group))
			pr_err("Failed to create tun sysfs files\n");
	}

	netif_carrier_on(tun->dev);

	tun_debug(KERN_INFO, tun, "tun_set_iff\n");

	tun->flags = (tun->flags & ~TUN_FEATURES) | (ifr->ifr_flags & TUN_FEATURES);

	/* Make sure persistent devices do not get stuck in
	 * xoff state.
	 */
	if (netif_running(tun->dev))
		netif_tx_wake_all_queues(tun->dev);

	strcpy(ifr->ifr_name, tun->dev->name);
	return 0;

}
```



```c
/* Initialize net device. */
static void tun_net_init(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);

	switch (tun->flags & TUN_TYPE_MASK) {
	case IFF_TUN:
		dev->netdev_ops = &tun_netdev_ops;

		/* Point-to-Point TUN Device */
		dev->hard_header_len = 0;
		dev->addr_len = 0;
		dev->mtu = 1500;

		/* Zero header length */
		dev->type = ARPHRD_NONE;
		dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
		break;

	case IFF_TAP:
		dev->netdev_ops = &tap_netdev_ops;
		/* Ethernet TAP Device */
		ether_setup(dev);
		dev->priv_flags &= ~IFF_TX_SKB_SHARING;
		dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

		eth_hw_addr_random(dev);

		break;
	}
}
```

### tap_netdev_ops

```c
static const struct net_device_ops tap_netdev_ops = {
	.ndo_uninit		= tun_net_uninit,
	.ndo_open		= tun_net_open,
	.ndo_stop		= tun_net_close,
	.ndo_start_xmit		= tun_net_xmit,
	.ndo_change_mtu		= tun_net_change_mtu,
	.ndo_fix_features	= tun_net_fix_features,
	.ndo_set_rx_mode	= tun_net_mclist,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_select_queue	= tun_select_queue,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= tun_poll_controller,
#endif
	.ndo_size		= sizeof(struct net_device_ops),
	.extended.ndo_set_rx_headroom	= tun_set_headroom,
	.ndo_get_stats64	= tun_net_get_stats64,
};
```

## TAP数据收发

### tun_net_xmit

```c
/* Net device start xmit */
static netdev_tx_t tun_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);
	int txq = skb->queue_mapping;
	struct tun_file *tfile;
	u32 numqueues = 0;

	rcu_read_lock();
	tfile = rcu_dereference(tun->tfiles[txq]);
	numqueues = ACCESS_ONCE(tun->numqueues);

	/* Drop packet if interface is not attached */
	if (txq >= numqueues)
		goto drop;

	tun_debug(KERN_INFO, tun, "tun_net_xmit %d\n", skb->len);

	/* Drop if the filter does not like it.
	 * This is a noop if the filter is disabled.
	 * Filter can be enabled only for the TAP devices. */
	if (!check_filter(&tun->txflt, skb))
		goto drop;

	if (tfile->socket.sk->sk_filter &&
	    sk_filter(tfile->socket.sk, skb))
		goto drop;

	/* Orphan the skb - required as we might hang on to it
	 * for indefinite time. */
	if (unlikely(skb_orphan_frags(skb, GFP_ATOMIC)))
		goto drop;
	skb_orphan(skb);

	nf_reset(skb);
	/* 将skb放在了ring bufer里面 */
	if (skb_array_produce(&tfile->tx_array, skb))
		goto drop;

	/* Notify and wake up reader process，唤醒reader */
	if (tfile->flags & TUN_FASYNC)
		kill_fasync(&tfile->fasync, SIGIO, POLL_IN);
    /* 唤醒等待队列 */
	wake_up_interruptible_poll(&tfile->wq.wait, POLLIN | POLLRDNORM | POLLRDBAND);

	rcu_read_unlock();
	return NETDEV_TX_OK;
}
```

tun_net_xmit并不实际发送数据

### tun_chr_poll

```c
/* Poll */
static unsigned int tun_chr_poll(struct file *file, poll_table *wait)
{
	struct tun_file *tfile = file->private_data;
	struct tun_struct *tun = __tun_get(tfile);
	struct sock *sk;
	unsigned int mask = 0;

	if (!tun)
		return POLLERR;

	sk = tfile->socket.sk;

	tun_debug(KERN_INFO, tun, "tun_chr_poll\n");
	/* 添加到tfile->wq.wait 等待队列头中 */
	poll_wait(file, &tfile->wq.wait, wait);

	if (!skb_array_empty(&tfile->tx_array))
		mask |= POLLIN | POLLRDNORM;

	if (sock_writeable(sk) ||
	    (!test_and_set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags) &&
	     sock_writeable(sk)))
		mask |= POLLOUT | POLLWRNORM;

	if (tun->dev->reg_state != NETREG_REGISTERED)
		mask = POLLERR;

	tun_put(tun);
	return mask;
}
```

### tun_chr_aio_read

```c
static ssize_t tun_chr_aio_read(struct kiocb *iocb, const struct iovec *iv,
			    unsigned long count, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct tun_file *tfile = file->private_data;
	struct tun_struct *tun = __tun_get(tfile);
	ssize_t len, ret;

	len = iov_length(iv, count);


	ret = tun_do_read(tun, tfile, iocb, iv, len, file->f_flags & O_NONBLOCK);
	ret = min_t(ssize_t, ret, len);
out:
	tun_put(tun);
	return ret;
}
```

#### tun_do_read

```c
static ssize_t tun_do_read(struct tun_struct *tun, struct tun_file *tfile,
			   struct kiocb *iocb, const struct iovec *iv,
			   ssize_t len, int noblock)
{
	struct sk_buff *skb;
	ssize_t ret;
	int err;

	tun_debug(KERN_INFO, tun, "tun_do_read\n");

	if (!len)
		return 0;

	/* Read frames from ring */
	skb = tun_ring_recv(tun, tfile, noblock, &err);
	if (!skb)
		return err;

	//record tun_do_read
	tvpc_record_ping_trace(skb, 0, ktime_get(), "tun_do_read");
	/* 将数据复制到用户态 */
	ret = tun_put_user(tun, tfile, skb, iv, len);
	if (unlikely(ret < 0))
		kfree_skb(skb);
	else
		consume_skb(skb);

	return ret;
}
```

#### tun_ring_recv

```c
static struct sk_buff *tun_ring_recv(struct tun_struct *tun, 
			struct tun_file *tfile, int noblock, int *err)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sk_buff *skb = NULL;
	int error = 0;

	skb = skb_array_consume(&tfile->tx_array);
    /* 如果有数据就退出 */
	if (skb)
		goto out;
	if (noblock) { /* 如果是非阻塞访问，退出 */
		error = -EAGAIN;
		goto out;
	}
	/* 添进程加到等待队列里面 */
	add_wait_queue(&tfile->wq.wait, &wait);
	current->state = TASK_INTERRUPTIBLE;

	while (1) {
		skb = skb_array_consume(&tfile->tx_array);
		if (skb)
			break;
		if (signal_pending(current)) {
			error = -ERESTARTSYS;
			break;
		}

		if (tun->dev->reg_state != NETREG_REGISTERED) {
			error = -EFAULT;
			break;
		}
		/* 睡眠 */
		schedule();
	}
	
	current->state = TASK_RUNNING;
	remove_wait_queue(&tfile->wq.wait, &wait);

out:
	*err = error;
	return skb;
}
```

从上面可以看出，tun_chr_aio_read会读取到tun_net_xmit发送的数据

### tun_chr_aio_write

```c
static ssize_t tun_chr_aio_write(struct kiocb *iocb, const struct iovec *iv,unsigned long count, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct tun_struct *tun = tun_get(file);
	struct tun_file *tfile = file->private_data;
	ssize_t result;

	tun_debug(KERN_INFO, tun, "tun_chr_write %ld\n", count);

	result = tun_get_user(tun, tfile, NULL, iv, iov_length(iv, count),
			      count, file->f_flags & O_NONBLOCK, false);

	tun_put(tun);
	return result;
}
```

#### tun_get_user

```c
/* Get packet from user space buffer */
static ssize_t tun_get_user(struct tun_struct *tun, struct tun_file *tfile,
			    void *msg_control, const struct iovec *iv,
			    size_t total_len, size_t count, int noblock, bool more)
{
	struct tun_pi pi = { 0, cpu_to_be16(ETH_P_IP) };
	struct sk_buff *skb;
	size_t len = total_len, align = tun->align, linear;
	struct virtio_net_hdr gso = { 0 };
	struct tun_pcpu_stats *stats;
	int good_linear;
	int offset = 0;
	int copylen;
	bool zerocopy = false;
	int err;
	u32 rxhash;

	if (!(tun->flags & IFF_NO_PI)) {
		if (len < sizeof(pi))
			return -EINVAL;
		len -= sizeof(pi);

		if (memcpy_fromiovecend((void *)&pi, iv, 0, sizeof(pi)))
			return -EFAULT;
		offset += sizeof(pi);
	}

	if (tun->flags & IFF_VNET_HDR) {
		if (len < tun->vnet_hdr_sz)
			return -EINVAL;
		len -= tun->vnet_hdr_sz;

		if (memcpy_fromiovecend((void *)&gso, iv, offset, sizeof(gso)))
			return -EFAULT;

		if ((gso.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) &&
		    tun16_to_cpu(tun, gso.csum_start) + tun16_to_cpu(tun, gso.csum_offset) + 2 > tun16_to_cpu(tun, gso.hdr_len))
			gso.hdr_len = cpu_to_tun16(tun, tun16_to_cpu(tun, gso.csum_start) + tun16_to_cpu(tun, gso.csum_offset) + 2);

		if (tun16_to_cpu(tun, gso.hdr_len) > len)
			return -EINVAL;
		offset += tun->vnet_hdr_sz;
	}

	if ((tun->flags & TUN_TYPE_MASK) == IFF_TAP) {
		align += NET_IP_ALIGN;
		
	}

	good_linear = SKB_MAX_HEAD(align);

	if (msg_control) {
		/* There are 256 bytes to be copied in skb, so there is
		 * enough room for skb expand head in case it is used.
		 * The rest of the buffer is mapped from userspace.
		 */
		copylen = gso.hdr_len ? tun16_to_cpu(tun, gso.hdr_len) : GOODCOPY_LEN;
		if (copylen > good_linear)
			copylen = good_linear;
		linear = copylen;
		if (iov_pages(iv, offset + copylen, count) <= MAX_SKB_FRAGS)
			zerocopy = true;
	}

	if (!zerocopy) {
		copylen = len;
		if (tun16_to_cpu(tun, gso.hdr_len) > good_linear)
			linear = good_linear;
		else
			linear = tun16_to_cpu(tun, gso.hdr_len);
	}
	/* 分配sdk_buff */
	skb = tun_alloc_skb(tfile, align, copylen, linear, noblock);

	if (zerocopy)
		err = zerocopy_sg_from_iovec(skb, iv, offset, count);
	else {
		err = skb_copy_datagram_from_iovec(skb, 0, iv, offset, len);
		if (!err && msg_control) {
			struct ubuf_info *uarg = msg_control;
			uarg->callback(uarg, false);
		}
	}


	if (gso.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
		if (!skb_partial_csum_set(skb, tun16_to_cpu(tun, gso.csum_start),
					  tun16_to_cpu(tun, gso.csum_offset))) {
			this_cpu_inc(tun->pcpu_stats->rx_frame_errors);
			kfree_skb(skb);
			return -EINVAL;
		}
	}

	switch (tun->flags & TUN_TYPE_MASK) {
	case IFF_TUN:
		if (tun->flags & IFF_NO_PI) {
			switch (skb->data[0] & 0xf0) {
			case 0x40:
				pi.proto = htons(ETH_P_IP);
				break;
			case 0x60:
				pi.proto = htons(ETH_P_IPV6);
				break;
			default:
				this_cpu_inc(tun->pcpu_stats->rx_dropped);
				kfree_skb(skb);
				return -EINVAL;
			}
		}

		skb_reset_mac_header(skb);
		skb->protocol = pi.proto;
		skb->dev = tun->dev;
		break;
	case IFF_TAP:
		skb->protocol = eth_type_trans(skb, tun->dev);
		break;
	}

	if (gso.gso_type != VIRTIO_NET_HDR_GSO_NONE) {
		pr_debug("GSO!\n");
		switch (gso.gso_type & ~VIRTIO_NET_HDR_GSO_ECN) {
		case VIRTIO_NET_HDR_GSO_TCPV4:
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
			break;
		case VIRTIO_NET_HDR_GSO_TCPV6:
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
			break;
		case VIRTIO_NET_HDR_GSO_UDP:
			skb_shinfo(skb)->gso_type = SKB_GSO_UDP;
			break;
		default:
			this_cpu_inc(tun->pcpu_stats->rx_frame_errors);
			kfree_skb(skb);
			return -EINVAL;
		}

		if (gso.gso_type & VIRTIO_NET_HDR_GSO_ECN)
			skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;

		skb_shinfo(skb)->gso_size = tun16_to_cpu(tun, gso.gso_size);
		if (skb_shinfo(skb)->gso_size == 0) {
			this_cpu_inc(tun->pcpu_stats->rx_frame_errors);
			kfree_skb(skb);
			return -EINVAL;
		}

		/* Header must be checked, and gso_segs computed. */
		skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
		skb_shinfo(skb)->gso_segs = 0;
	}

	/* copy skb_ubuf_info for callback when skb has no error */
	if (zerocopy) {
		skb_shinfo(skb)->destructor_arg = msg_control;
		skb_shinfo(skb)->tx_flags |= SKBTX_DEV_ZEROCOPY;
		skb_shinfo(skb)->tx_flags |= SKBTX_SHARED_FRAG;
	}

	skb_reset_network_header(skb);
	skb_probe_transport_header(skb, 0);

	//record function
	tvpc_record_ping_trace(skb, 0, ktime_get(), "tun_get_user");

	rxhash = __skb_get_hash_symmetric(skb);
    /* 将skb送入内核协议栈 */
#ifndef CONFIG_4KSTACKS
	tun_rx_batched(tun, tfile, skb, more);
#else
	netif_rx_ni(skb);
#endif

	stats = get_cpu_ptr(tun->pcpu_stats);
	u64_stats_update_begin(&stats->syncp);
	stats->rx_packets++;
	stats->rx_bytes += len;
	u64_stats_update_end(&stats->syncp);
	put_cpu_ptr(stats);

	tun_flow_update(tun, rxhash, tfile);
	return total_len;
}
```

tun_chr_aio_write会向内核发送数据。



## tun_sendmsg

```c
static int tun_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *m, size_t total_len)
{
	int ret;
	struct tun_file *tfile = container_of(sock, struct tun_file, socket);
	struct tun_struct *tun = __tun_get(tfile);
	/* 将数据发送到内核协议栈 */
	ret = tun_get_user(tun, tfile, m->msg_control, m->msg_iov, total_len,
			   m->msg_iovlen, m->msg_flags & MSG_DONTWAIT,
			   m->msg_flags & MSG_MORE);
	tun_put(tun);
	return ret;
}
```

## tun_recvmsg

```c
static int tun_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *m, size_t total_len,
		       int flags)
{
	struct tun_file *tfile = container_of(sock, struct tun_file, socket);
	struct tun_struct *tun = __tun_get(tfile);
	int ret;


	if (flags & ~(MSG_DONTWAIT|MSG_TRUNC)) {
		ret = -EINVAL;
		goto out;
	}
    /* 会将数据拷贝到m->msg_iov中 */
	ret = tun_do_read(tun, tfile, iocb, m->msg_iov, total_len, flags & MSG_DONTWAIT);
	if (ret > total_len) {
		m->msg_flags |= MSG_TRUNC;
		ret = flags & MSG_TRUNC ? ret : total_len;
	}
out:
	tun_put(tun);
	return ret;
}
```

