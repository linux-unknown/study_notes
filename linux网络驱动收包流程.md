

# linux 内核驱动网络收包流程

以virtio_net驱动为例

# 中断函数

virtio_net接收的中断为`vring_interrupt`

```c
err = request_irq(vp_dev->msix_entries[msix_vec].vector, vring_interrupt, 0, 
                  vp_dev->msix_names[msix_vec], vqs[i]);
```

### vring_interrupt

```c
irqreturn_t vring_interrupt(int irq, void *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	pr_debug("virtqueue callback for %p (%p)\n", vq, vq->vq.callback);
	if (vq->vq.callback)
		vq->vq.callback(&vq->vq); /* 接收时调用skb_recv_done */

	return IRQ_HANDLED;
}
```

#### skb_recv_done

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

#### __napi_schedule

```c
void __napi_schedule(struct napi_struct *n)
{
	unsigned long flags;

	local_irq_save(flags);
	____napi_schedule(this_cpu_ptr(&softnet_data), n);
	local_irq_restore(flags);
}
```

#### \____napi_schedule

```c
/* Called with irq disabled */
static inline void ____napi_schedule(struct softnet_data *sd, struct napi_struct *napi)
{
    /* 将napi->poll_list添加到该cpu的sd->poll_list中 */
	list_add_tail(&napi->poll_list, &sd->poll_list);
    /* 发起软中断 */
	__raise_softirq_irqoff(NET_RX_SOFTIRQ);
}
```

## rx queue napi初始化

### virtnet_alloc_queues

```c
static int virtnet_alloc_queues(struct virtnet_info *vi)
{
	int i;

	vi->sq = kzalloc(sizeof(*vi->sq) * vi->max_queue_pairs, GFP_KERNEL);
	vi->rq = kzalloc(sizeof(*vi->rq) * vi->max_queue_pairs, GFP_KERNEL);

	INIT_DELAYED_WORK(&vi->refill, refill_work);
	for (i = 0; i < vi->max_queue_pairs; i++) {
		vi->rq[i].pages = NULL;
		netif_napi_add(vi->dev, &vi->rq[i].napi, virtnet_poll, napi_weight);

		sg_init_table(vi->rq[i].sg, ARRAY_SIZE(vi->rq[i].sg));
		sg_init_table(vi->sq[i].sg, ARRAY_SIZE(vi->sq[i].sg));
	}
	return 0;
}
```

#### netif_napi_add

```c
void netif_napi_add(struct net_device *dev, struct napi_struct *napi,
		    int (*poll)(struct napi_struct *, int), int weight)
{
	__netif_napi_add(dev, napi, poll, weight, offsetof(struct napi_struct, size));
}
```
#### __netif_napi_add
```c
void __netif_napi_add(struct net_device *dev, struct napi_struct *napi,
		      int (*poll)(struct napi_struct *, int), int weight, size_t size)
{
	if (size > offsetof(struct napi_struct, size)) {
		napi->size = size;
		set_bit(NAPI_STATE_EXT, &napi->state); /* Mark as extended */
	}
	INIT_LIST_HEAD(&napi->poll_list);
	if (NAPI_STRUCT_HAS(napi, timer)) {
		hrtimer_init(&napi->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
		napi->timer.function = napi_watchdog;
	}
    /* napi初始化赋值 */
	napi->gro_count = 0;
	napi->gro_list = NULL;
	napi->skb = NULL;
	napi->poll = poll;
	if (weight > NAPI_POLL_WEIGHT)
		pr_err_once("netif_napi_add() called with weight %d on device %s\n",weight, dev->name);
	napi->weight = weight;
    /* napi->dev_list添加到 dev->napi_list中 */
	list_add(&napi->dev_list, &dev->napi_list);
	napi->dev = dev;
#ifdef CONFIG_NETPOLL
	spin_lock_init(&napi->poll_lock);
	napi->poll_owner = -1;
#endif
	set_bit(NAPI_STATE_SCHED, &napi->state);
	napi_hash_add(napi);
}
```

## net_dev初始化

### net_dev_init

```c
DEFINE_PER_CPU_ALIGNED(struct softnet_data, softnet_data);

static int __init net_dev_init(void)
{
	int i, rc = -ENOMEM;

	if (dev_proc_init())
		goto out;

	if (netdev_kobject_init())
		goto out;

	INIT_LIST_HEAD(&ptype_all);
	for (i = 0; i < PTYPE_HASH_SIZE; i++)
		INIT_LIST_HEAD(&ptype_base[i]);

	INIT_LIST_HEAD(&offload_base);

	if (register_pernet_subsys(&netdev_net_ops))
		goto out;

	/*
	 *	Initialise the packet receive queues.
	 */

	for_each_possible_cpu(i) {
		struct softnet_data *sd = &per_cpu(softnet_data, i);

		memset(sd, 0, sizeof(*sd));
		skb_queue_head_init(&sd->input_pkt_queue);
		skb_queue_head_init(&sd->process_queue);
		sd->completion_queue = NULL;
		INIT_LIST_HEAD(&sd->poll_list);
		sd->output_queue = NULL;
		sd->output_queue_tailp = &sd->output_queue;
#ifdef CONFIG_RPS
        /* ipi处理函数 */
		sd->csd.func = rps_trigger_softirq;
		sd->csd.info = sd;
		sd->csd.flags = 0;
		sd->cpu = i;
#endif
		/* 为每一个cpu的sd->backlog.poll赋值 */
		sd->backlog.poll = process_backlog;
		sd->backlog.weight = weight_p;
		sd->backlog.gro_list = NULL;
		sd->backlog.gro_count = 0;
	}

	dev_boot_phase = 0;

	/* The loopback device is special if any other network devices
	 * is present in a network namespace the loopback device must
	 * be present. Since we now dynamically allocate and free the
	 * loopback device ensure this invariant is maintained by
	 * keeping the loopback device as the first device on the
	 * list of network devices.  Ensuring the loopback devices
	 * is the first device that appears and the last network device
	 * that disappears.
	 */
	if (register_pernet_device(&loopback_net_ops))
		goto out;

	if (register_pernet_device(&default_device_ops))
		goto out;
	/* 注册软中断 */
	open_softirq(NET_TX_SOFTIRQ, net_tx_action);
	open_softirq(NET_RX_SOFTIRQ, net_rx_action);

	hotcpu_notifier(dev_cpu_callback, 0);
	dst_subsys_init();
	rc = 0;
out:
	return rc;
}
```

## 网络软中断函数

### net_rx_action

```c
static void net_rx_action(struct softirq_action *h)
{
    /* 获得该cpu的sd */
	struct softnet_data *sd = this_cpu_ptr(&softnet_data);
	unsigned long time_limit = jiffies + 2;
	int budget = netdev_budget;
	LIST_HEAD(list);
	LIST_HEAD(repoll);

	local_irq_disable();
    /* 将sd->poll_list列表加入到list中 */
	list_splice_init(&sd->poll_list, &list);
	local_irq_enable();

	for (;;) {
		struct napi_struct *n;

		if (list_empty(&list)) {
			if (!sd_has_rps_ipi_waiting(sd) && list_empty(&repoll))
				return;
			break;
		}
		/* 取出list中第一个的元素，元素类型为struct napi_struct 
		 * 中断函数中 ____napi_schedule将struct napi_struct添加到该cpu的sd->poll_list中
		 */
		n = list_first_entry(&list, struct napi_struct, poll_list);
		budget -= napi_poll(n, &repoll);

		/* If softirq window is exhausted then punt.
		 * Allow this to run for 2 jiffies since which will allow
		 * an average latency of 1.5/HZ.
		 */
		if (unlikely(budget <= 0 || time_after_eq(jiffies, time_limit))) {
			sd->time_squeeze++;
			break;
		}
	}

	__kfree_skb_flush();
	local_irq_disable();

	list_splice_tail_init(&sd->poll_list, &list);
	list_splice_tail(&repoll, &list);
	list_splice(&list, &sd->poll_list);
	if (!list_empty(&sd->poll_list))
		__raise_softirq_irqoff(NET_RX_SOFTIRQ);

	net_rps_action_and_irq_enable(sd);
}
```

### napi_poll

```c
static int napi_poll(struct napi_struct *n, struct list_head *repoll)
{
	void *have;
	int work, weight;
	/* 删除该元素 */
	list_del_init(&n->poll_list);

	have = netpoll_poll_lock(n);

	weight = n->weight;

	/* This NAPI_STATE_SCHED test is for avoiding a race
	 * with netpoll's poll_napi().  Only the entity which
	 * obtains the lock and sees NAPI_STATE_SCHED set will
	 * actually make the ->poll() call.  Therefore we avoid
	 * accidentally calling ->poll() when NAPI is not scheduled.
	 */
	work = 0;
	if (test_bit(NAPI_STATE_SCHED, &n->state)) {
        /* 调用virtnet_poll函数，该函数是在virtnet_alloc_queues中添加的 */
		work = n->poll(n, weight);
		trace_napi_poll(n);
	}

	WARN_ON_ONCE(work > weight);

	if (likely(work < weight))
		goto out_unlock;

	/* Drivers must not modify the NAPI state if they
	 * consume the entire weight.  In such cases this code
	 * still "owns" the NAPI instance and therefore can
	 * move the instance around on the list at-will.
	 */
	if (unlikely(napi_disable_pending(n))) {
		napi_complete(n);
		goto out_unlock;
	}

	if (n->gro_list) {
		/* flush too old packets
		 * If HZ < 1000, flush all packets.
		 */
		napi_gro_flush(n, HZ >= 1000);
	}

	/* Some drivers may have called napi_schedule
	 * prior to exhausting their budget.
	 */
	if (unlikely(!list_empty(&n->poll_list))) {
		pr_warn_once("%s: Budget exhausted after napi rescheduled\n", n->dev ? n->dev->name : "backlog");
		goto out_unlock;
	}

	list_add_tail(&n->poll_list, repoll);

out_unlock:
	netpoll_poll_unlock(have);

	return work;
}
```

### virtnet_poll

```c
static int virtnet_poll(struct napi_struct *napi, int budget)
{
	struct receive_queue *rq = container_of(napi, struct receive_queue, napi);
	struct virtnet_info *vi = rq->vq->vdev->priv;
	void *buf;
	unsigned int r, len, received = 0;

	while (received < budget && (buf = virtqueue_get_buf(rq->vq, &len)) != NULL) {
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
static void receive_buf(struct virtnet_info *vi, struct receive_queue *rq, void *buf, unsigned int len)
{
	struct net_device *dev = vi->dev;
	struct virtnet_stats *stats = this_cpu_ptr(vi->stats);
	struct sk_buff *skb;
	struct virtio_net_hdr_mrg_rxbuf *hdr;

    /* 从vring中获取buffer数据 */
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
		if (!skb_partial_csum_set(skb, virtio16_to_cpu(vi->vdev, hdr->hdr.csum_start),
			  virtio16_to_cpu(vi->vdev, hdr->hdr.csum_offset)))
			goto frame_err;
	} else if (hdr->hdr.flags & VIRTIO_NET_HDR_F_DATA_VALID) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	/* 确定包的协议类型 */
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

	napi_gro_receive(&rq->napi, skb);
	return;
}
```

### napi_gro_receive

```c
gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	trace_napi_gro_receive_entry(skb);

	skb_mark_napi_id(skb, napi);

	skb_gro_reset_offset(skb);
	
	return napi_skb_finish(dev_gro_receive(napi, skb), skb);
}
```

napi_skb_finish-->netif_receive_skb_internal

### netif_receive_skb_internal

```c
static int netif_receive_skb_internal(struct sk_buff *skb)
{
	int ret;

	net_timestamp_check(netdev_tstamp_prequeue, skb);

	if (skb_defer_rx_timestamp(skb))
		return NET_RX_SUCCESS;

	rcu_read_lock();

#ifdef CONFIG_RPS
    /* rps相关的 */
	if (static_key_false(&rps_needed)) {
		struct rps_dev_flow voidflow, *rflow = &voidflow;
		int cpu = get_rps_cpu(skb->dev, skb, &rflow);

		if (cpu >= 0) {
			ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);
			rcu_read_unlock();
			return ret;
		}
	}
#endif
    /* 将包传递给上层进行处理 */
	ret = __netif_receive_skb(skb);
	rcu_read_unlock();
	return ret;
}
```

如果配置有rps的话，且cpu > 0,netif_receive_skb_internal就不会执行__netif_receive_skb函数

## rps

netif_receive_skb_internal中下面的代码用来处理RPS

```c
#ifdef CONFIG_RPS
	if (static_key_false(&rps_needed)) {
		struct rps_dev_flow voidflow, *rflow = &voidflow;
        /* 得到要eneuque的cpu */
		int cpu = get_rps_cpu(skb->dev, skb, &rflow);

		if (cpu >= 0) {
			ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);
			rcu_read_unlock();
			return ret;
		}
	}
#endif
```

### get_rps_cpu

```c
/*
 * get_rps_cpu is called from netif_receive_skb and returns the target
 * CPU from the RPS map of the receiving queue for a given skb.
 * rcu_read_lock must be held on entry.
 */
static int get_rps_cpu(struct net_device *dev, struct sk_buff *skb,
		       struct rps_dev_flow **rflowp)
{
	struct netdev_rx_queue *rxqueue;
	struct rps_map *map;
	struct rps_dev_flow_table *flow_table;
	struct rps_sock_flow_table *sock_flow_table;
	int cpu = -1;
	u16 tcpu;
	u32 hash;
	struct iphdr *ip, _iph;
	bool gre_proto = 0;

	if (skb->protocol == __constant_htons(ETH_P_IP)) {
		ip = skb_header_pointer(skb, 0, sizeof(struct iphdr), &_iph);
		if (ip == NULL)
			goto done;
		if (ip->protocol == IPPROTO_GRE &&
		    /* skip fragmented ip segments */
		    ((ntohs(ip->frag_off) & IP_OFFSET) == 0))
			gre_proto = 1;
	}

	if (skb_rx_queue_recorded(skb) && !gre_proto) {
		u16 index = skb_get_rx_queue(skb);
		if (unlikely(index >= dev->real_num_rx_queues)) {
			WARN_ONCE(dev->real_num_rx_queues > 1,
				  "%s received packet on queue %u, but number "
				  "of RX queues is %u\n",
				  dev->name, index, dev->real_num_rx_queues);
			goto done;
		}
		rxqueue = dev->_rx + index;
	} else
		rxqueue = dev->_rx;
	/* 获取队列的rps_map,该map可以通过/sys/class/net/ethX/queue/rx-X/rps进行读取和设置 */
	map = rcu_dereference(rxqueue->rps_map);
	if (map) {
		if (map->len == 1 && !rcu_access_pointer(rxqueue->rps_flow_table)) {
			tcpu = map->cpus[0];
			if (cpu_online(tcpu))
				cpu = tcpu;
			goto done;
		}
	} else if (!rcu_access_pointer(rxqueue->rps_flow_table)) {
		goto done;
	}

	skb_reset_network_header(skb);
	hash = skb_get_hash(skb);
	if (!hash)
		goto done;
	
    /* RFS相关 */
	flow_table = rcu_dereference(rxqueue->rps_flow_table);
	sock_flow_table = rcu_dereference(rps_sock_flow_table);
	if (flow_table && sock_flow_table) {
		u16 next_cpu;
		struct rps_dev_flow *rflow;

		rflow = &flow_table->flows[hash & flow_table->mask];
		tcpu = rflow->cpu;

		next_cpu = sock_flow_table->ents[hash & sock_flow_table->mask];

		/*
		 * If the desired CPU (where last recvmsg was done) is
		 * different from current CPU (one in the rx-queue flow
		 * table entry), switch if one of the following holds:
		 *   - Current CPU is unset (equal to RPS_NO_CPU).
		 *   - Current CPU is offline.
		 *   - The current CPU's queue tail has advanced beyond the
		 *     last packet that was enqueued using this table entry.
		 *     This guarantees that all previous packets for the flow
		 *     have been dequeued, thus preserving in order delivery.
		 */
		if (unlikely(tcpu != next_cpu) && (tcpu == RPS_NO_CPU || !cpu_online(tcpu) ||
		     ((int)(per_cpu(softnet_data, tcpu).input_queue_head - rflow->last_qtail)) >= 0)) {
			tcpu = next_cpu;
			rflow = set_rps_cpu(dev, skb, rflow, next_cpu);
		}

		if (tcpu != RPS_NO_CPU && cpu_online(tcpu)) {
			*rflowp = rflow;
			cpu = tcpu;
			goto done;
		}
	}

	if (map) {
		tcpu = map->cpus[reciprocal_scale(hash, map->len)];
		if (cpu_online(tcpu)) {
			cpu = tcpu;
			goto done;
		}
	}

done:
	return cpu;
}
```

### enqueue_to_backlog

```c
/*
 * enqueue_to_backlog is called to queue an skb to a per CPU backlog
 * queue (may be a remote CPU queue).
 */
static int enqueue_to_backlog(struct sk_buff *skb, int cpu, unsigned int *qtail)
{
	struct softnet_data *sd;
	unsigned long flags;
	/* 获取该cpu的sd */
	sd = &per_cpu(softnet_data, cpu);

	local_irq_save(flags);

	rps_lock(sd);
	if (!netif_running(skb->dev))
		goto drop;
	if (skb_queue_len(&sd->input_pkt_queue) <= netdev_max_backlog) {
		if (skb_queue_len(&sd->input_pkt_queue)) {
enqueue:
            /* 将skb添加到sd->input_pkt_queue队列 */
			__skb_queue_tail(&sd->input_pkt_queue, skb);
  
            /* *qtail = ++sd->input_queue_tail; */
			input_queue_tail_incr_save(sd, qtail);
			rps_unlock(sd);
			local_irq_restore(flags);
			return NET_RX_SUCCESS;
		}

		/* Schedule NAPI for backlog device
		 * We can use non atomic operation since we own the queue lock
		 */
		if (!__test_and_set_bit(NAPI_STATE_SCHED, &sd->backlog.state)) {
			if (!rps_ipi_queued(sd))
				____napi_schedule(sd, &sd->backlog);
		}
		goto enqueue;
	}
}
```

#### rps_ipi_queued

```c
 * Check if this softnet_data structure is another cpu one
 * If yes, queue it to our IPI list and return 1
 * If no, return 0
 */
static int rps_ipi_queued(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	struct softnet_data *mysd = this_cpu_ptr(&softnet_data);

	if (sd != mysd) {
		sd->rps_ipi_next = mysd->rps_ipi_list;
        /* 将目标cpu的sd放入执行软中断cpu的rps_ipi_list中 */
		mysd->rps_ipi_list = sd;

		__raise_softirq_irqoff(NET_RX_SOFTIRQ);
		return 1;
	}
#endif /* CONFIG_RPS */
	return 0;
}
```

## 发送rps的ipi中断

net_rx_action-->net_rps_action_and_irq_enable

```c
/*
 * net_rps_action sends any pending IPI's for rps.
 * Note: called with local irq disabled, but exits with local irq enabled.
 */
static void net_rps_action_and_irq_enable(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	struct softnet_data *remsd = sd->rps_ipi_list;

	if (remsd) {
		sd->rps_ipi_list = NULL;

		local_irq_enable();

		/* Send pending IPI's to kick RPS processing on remote cpus. */
		while (remsd) {
			struct softnet_data *next = remsd->rps_ipi_next;

			if (cpu_online(remsd->cpu))
				smp_call_function_single_async(remsd->cpu, &remsd->csd);
			remsd = next;
		}
	} else
#endif
		local_irq_enable();
}

```

#### smp_call_function_single_async

```c
int smp_call_function_single_async(int cpu, struct call_single_data *csd)
{
	int err = 0;

	preempt_disable();
	err = generic_exec_single(cpu, csd, csd->func, csd->info, 0);
	preempt_enable();

	return err;
}
```

 csd->func为处理还ipi中断的函数，该函数为rps_trigger_softirq，是在net_dev_init为每个cpu进行赋值的

### rps_trigger_softirq

```c
static void rps_trigger_softirq(void *data)
{
	struct softnet_data *sd = data;
	/* 触发软中断，此时将sd->backlog添加到sd->poll_list，在
     * net_rx_action-->napi_poll将执行process_backlog
     */
	____napi_schedule(sd, &sd->backlog);
	sd->received_rps++;
}
```

process_backlog中将会执行__netif_receive_skb，将包送往上层进行处理。



## 注意：

1. 从get_rps_cpu看，我们在关闭rps的时候，除了将`/sys/class/net/ethX/queues/rx-x/rps_cpus`设置为0外，最好也将`/sys/class/net/ethX/queues/rx-x/rps_flow_cnt`设置为0。
2. 我们在有的时候已经将rps关闭了，但是在抓perf中依然能看到有process_backlog等信息，其中一个原因是loopback设备上有数据

loopback_xmit-->netif_rx-->netif_rx_internal

```c
static int netif_rx_internal(struct sk_buff *skb)
{
	int ret;

	net_timestamp_check(netdev_tstamp_prequeue, skb);

	trace_netif_rx(skb);
#ifdef CONFIG_RPS
	if (static_key_false(&rps_needed)) {
		struct rps_dev_flow voidflow, *rflow = &voidflow;
		int cpu;

		preempt_disable();
		rcu_read_lock();

		cpu = get_rps_cpu(skb->dev, skb, &rflow);
		if (cpu < 0)
			cpu = smp_processor_id();

		ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);

		rcu_read_unlock();
		preempt_enable();
	} else
#endif
	{
		unsigned int qtail;
		ret = enqueue_to_backlog(skb, get_cpu(), &qtail);
		put_cpu();
	}
	return ret;
}
```

可以看到`netif_rx_internal`无论如何都会调用`enqueue_to_backlog`



## 总结：

- 在关闭rps的情况下(没有配置rps或者map设置为0)，virtio_net收包的流程为

  ```
  vring_interrupt-->skb_recv_done-->__napi_schedule-->net_rx_action-->napi_poll-->virtnet_poll-->receive_buf-->napi_gro_receive-->napi_skb_finish-->netif_receive_skb_internal-->__netif_receive_skb
  ```

- 开启rps的情况
  
  ```c
  接收中断cpu：
  vring_interrupt-->skb_recv_done-->__napi_schedule-->net_rx_action-->napi_poll-->virtnet_poll-->receive_buf-->napi_gro_receive-->napi_skb_finish-->netif_receive_skb_internal-->enqueue_to_backlog
  
  rps cpu:
  rps_trigger_softirq-->net_rx_action-->napi_poll-->process_backlog-->__netif_receive_skb
  ```
  
  
  
  
  
  

