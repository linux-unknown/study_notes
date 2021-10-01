

# virtio-net

## virtnet_probe

```c
static int virtnet_probe(struct virtio_device *vdev)
{
	int i, err;
	struct net_device *dev;
	struct virtnet_info *vi;
	u16 max_queue_pairs;
	int mtu;

	/* Find if host supports multiqueue virtio_net device 
	 * 检查是否支持多队列，以及队列数目
	 */
	err = virtio_cread_feature(vdev, VIRTIO_NET_F_MQ, struct virtio_net_config,
				   max_virtqueue_pairs, &max_queue_pairs);

	/* Allocate ourselves a network device with room for our info */
	dev = alloc_etherdev_mq(sizeof(struct virtnet_info), max_queue_pairs);

	/* Set up network device as normal. */
	dev->priv_flags |= IFF_UNICAST_FLT | IFF_LIVE_ADDR_CHANGE;
	dev->netdev_ops = &virtnet_netdev;
	dev->features = NETIF_F_HIGHDMA;

	SET_ETHTOOL_OPS(dev, &virtnet_ethtool_ops);
	SET_NETDEV_DEV(dev, &vdev->dev);

	dev->vlan_features = dev->features;

	/* Set up our device-specific information */
	vi = netdev_priv(dev);
	vi->dev = dev;
	vi->vdev = vdev;
	vdev->priv = vi;
	vi->stats = alloc_percpu(struct virtnet_stats);
	err = -ENOMEM;

	vi->vq_index = alloc_percpu(int);

	INIT_WORK(&vi->config_work, virtnet_config_changed_work);

	/* Enable multiqueue by default */
	if (num_online_cpus() >= max_queue_pairs)
		vi->curr_queue_pairs = max_queue_pairs;
	else
		vi->curr_queue_pairs = num_online_cpus();

	vi->max_queue_pairs = max_queue_pairs;

	/* Allocate/initialize the rx/tx queues, and invoke find_vqs */
	err = init_vqs(vi);

	netif_set_real_num_tx_queues(dev, vi->curr_queue_pairs);
	netif_set_real_num_rx_queues(dev, vi->curr_queue_pairs);
	/* 注册网络设备 */
	err = register_netdev(dev);
	/* 告诉device，driver ok了 */
	virtio_device_ready(vdev);

	/* Last of all, set up some receive buffers. */
	for (i = 0; i < vi->curr_queue_pairs; i++) {
		try_fill_recv(vi, &vi->rq[i], GFP_KERNEL);
	}

	vi->nb.notifier_call = &virtnet_cpu_callback;
	err = register_hotcpu_notifier(&vi->nb);
	rtnl_lock();
	virtnet_set_queues(vi, vi->curr_queue_pairs);
	rtnl_unlock();

	/* Assume link up if device can't report link status,
	   otherwise get link status from config. */
	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_STATUS)) {
		netif_carrier_off(dev);
		schedule_work(&vi->config_work);
	} else {
		vi->status = VIRTIO_NET_S_LINK_UP;
		netif_carrier_on(dev);
	}

	pr_debug("virtnet: registered device %s with %d RX and TX vq's\n", dev->name, max_queue_pairs);

	return 0;
}
```

## init_vqs

```c
static int init_vqs(struct virtnet_info *vi)
{
	int ret;

	/* Allocate send & receive queues */
	ret = virtnet_alloc_queues(vi);

	ret = virtnet_find_vqs(vi);

	get_online_cpus();
	virtnet_set_affinity(vi);
	put_online_cpus();

	return 0;
}
```

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

sg_init_table

```c
void sg_init_table(struct scatterlist *sgl, unsigned int nents)
{
	memset(sgl, 0, sizeof(*sgl) * nents);
#ifdef CONFIG_DEBUG_SG
	{
		unsigned int i;
		for (i = 0; i < nents; i++)
			sgl[i].sg_magic = SG_MAGIC;
	}
#endif
	sg_mark_end(&sgl[nents - 1]);
}
```

### virtnet_find_vqs

```c
static int virtnet_find_vqs(struct virtnet_info *vi)
{
	vq_callback_t **callbacks;
	struct virtqueue **vqs;
	int ret = -ENOMEM;
	int i, total_vqs;
	const char **names;

	/* We expect 1 RX virtqueue followed by 1 TX virtqueue, followed by
	 * possible N-1 RX/TX queue pairs used in multiqueue mode, followed by
	 * possible control vq.
	 */
	/* 如果有控制queue则加1 */
	total_vqs = vi->max_queue_pairs * 2 + virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VQ);

	/* Allocate space for find_vqs parameters */
	vqs = kzalloc(total_vqs * sizeof(*vqs), GFP_KERNEL);

	callbacks = kmalloc(total_vqs * sizeof(*callbacks), GFP_KERNEL);

	names = kmalloc(total_vqs * sizeof(*names), GFP_KERNEL);


	/* Parameters for control virtqueue, if any */
	if (vi->has_cvq) {
		callbacks[total_vqs - 1] = NULL;
		names[total_vqs - 1] = "control";
	}

	/* Allocate/initialize parameters for send/receive virtqueues */
	for (i = 0; i < vi->max_queue_pairs; i++) {
		callbacks[rxq2vq(i)] = skb_recv_done;
		callbacks[txq2vq(i)] = skb_xmit_done;
		sprintf(vi->rq[i].name, "input.%d", i);
		sprintf(vi->sq[i].name, "output.%d", i);
		names[rxq2vq(i)] = vi->rq[i].name;
		names[txq2vq(i)] = vi->sq[i].name;
	}

	/* vp_modern_find_vqs */
	ret = vi->vdev->config->find_vqs(vi->vdev, total_vqs, vqs, callbacks, names);

	if (vi->has_cvq) {
		vi->cvq = vqs[total_vqs - 1];
		if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VLAN))
			vi->dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
	}

	for (i = 0; i < vi->max_queue_pairs; i++) {
		vi->rq[i].vq = vqs[rxq2vq(i)];
		vi->sq[i].vq = vqs[txq2vq(i)];
	}

	kfree(names);
	kfree(callbacks);
	kfree(vqs);

	return 0;
}
```

### vp_modern_find_vqs

```c
static int vp_modern_find_vqs(struct virtio_device *vdev, unsigned nvqs, struct virtqueue *vqs[],
			      vq_callback_t *callbacks[], const char * const names[])
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtqueue *vq;
	int rc = vp_find_vqs(vdev, nvqs, vqs, callbacks, names);

	/* Select and activate all queues. Has to be done last: once we do
	 * this, there's no way to go back except reset.
	 */
	list_for_each_entry(vq, &vdev->vqs, list) {
		vp_iowrite16(vq->index, &vp_dev->common->queue_select);
		vp_iowrite16(1, &vp_dev->common->queue_enable);
	}

	return 0;
}
```




```c
/* the config->find_vqs() implementation */
int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs, struct virtqueue *vqs[],
		vq_callback_t *callbacks[], const char * const names[])
{
	int err;

	/* Try MSI-X with one vector per queue. 每个队列一个中断 */
	err = vp_try_to_find_vqs(vdev, nvqs, vqs, callbacks, names, true, true);
	if (!err)
		return 0;
	/* Fallback: MSI-X with one vector for config, one shared for queues. */
	err = vp_try_to_find_vqs(vdev, nvqs, vqs, callbacks, names, true, false);
	if (!err)
		return 0;
	/* Finally fall back to regular interrupts. */
	return vp_try_to_find_vqs(vdev, nvqs, vqs, callbacks, names, false, false);
}
```
### vp_try_to_find_vqs

```c
static int vp_try_to_find_vqs(struct virtio_device *vdev, unsigned nvqs, struct virtqueue *vqs[],
			      vq_callback_t *callbacks[], const char * const names[], bool use_msix,
			      bool per_vq_vectors)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u16 msix_vec;
	int i, err, nvectors, allocated_vectors;

	vp_dev->vqs = kmalloc(nvqs * sizeof *vp_dev->vqs, GFP_KERNEL);


	if (!use_msix) {
		/* Old style: one normal interrupt for change and all vqs. */
		err = vp_request_intx(vdev);
	} else {
		if (per_vq_vectors) {
			/* Best option: one for change interrupt, one per vq. */
			nvectors = 1;
			for (i = 0; i < nvqs; ++i)
				if (callbacks[i])
					++nvectors;
		} else {
			/* Second best: one for change, shared for all vqs. */
			nvectors = 2;
		}
		err = vp_request_msix_vectors(vdev, nvectors, per_vq_vectors);
	}

	vp_dev->per_vq_vectors = per_vq_vectors;
	allocated_vectors = vp_dev->msix_used_vectors;
	for (i = 0; i < nvqs; ++i) {
		/* 控制vq  name 为NULL */
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		} else if (!callbacks[i] || !vp_dev->msix_enabled)
			msix_vec = VIRTIO_MSI_NO_VECTOR;
		else if (vp_dev->per_vq_vectors)
			msix_vec = allocated_vectors++;
		else
			msix_vec = VP_MSIX_VQ_VECTOR;
	
		vqs[i] = vp_setup_vq(vdev, i, callbacks[i], names[i], msix_vec);

		if (!vp_dev->per_vq_vectors || msix_vec == VIRTIO_MSI_NO_VECTOR)
			continue;

		/* allocate per-vq irq if available and necessary */
		snprintf(vp_dev->msix_names[msix_vec], sizeof *vp_dev->msix_names,
			 "%s-%s", dev_name(&vp_dev->vdev.dev), names[i]);

		/* 注册中断 */
		err = request_irq(vp_dev->msix_entries[msix_vec].vector, vring_interrupt, 0, 
                          vp_dev->msix_names[msix_vec], vqs[i]);
	}
	return 0;
}
```

### vp_setup_vq

```c
static struct virtqueue *vp_setup_vq(struct virtio_device *vdev, unsigned index,
				     void (*callback)(struct virtqueue *vq), const char *name, u16 msix_vec)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_vq_info *info = kmalloc(sizeof *info, GFP_KERNEL);
	struct virtqueue *vq;
	unsigned long flags;

	/* 调用setup_vq */
	vq = vp_dev->setup_vq(vp_dev, info, index, callback, name, msix_vec);

	info->vq = vq;
	if (callback) {
		spin_lock_irqsave(&vp_dev->lock, flags);
		list_add(&info->node, &vp_dev->virtqueues);
		spin_unlock_irqrestore(&vp_dev->lock, flags);
	} else {
		INIT_LIST_HEAD(&info->node);
	}

	vp_dev->vqs[index] = info;
	return vq;
}
```

### setup_vq

```c
static struct virtqueue *setup_vq(struct virtio_pci_device *vp_dev,
				  struct virtio_pci_vq_info *info, unsigned index,
				  void (*callback)(struct virtqueue *vq), const char *name, u16 msix_vec)
{
	struct virtio_pci_common_cfg __iomem *cfg = vp_dev->common;
	struct virtqueue *vq;
	u16 num, off;
	int err;

	/* Select the queue we're interested in */
	vp_iowrite16(index, &cfg->queue_select);

	/* Check if queue is either not available or already active. */
	num = vp_ioread16(&cfg->queue_size);

	/* num必须是2的倍数 */
	if (num & (num - 1)) {
		dev_warn(&vp_dev->pci_dev->dev, "bad queue size %u", num);
		return ERR_PTR(-EINVAL);
	}

	/* get offset of notification word for this vq */
	off = vp_ioread16(&cfg->queue_notify_off);

	info->msix_vector = msix_vec;

	/* create the vring */
	vq = vring_create_virtqueue(index, num, SMP_CACHE_BYTES, &vp_dev->vdev,
				    true, true, vp_notify, callback, name);

	/* activate the queue,将vring的queue size，地址写入pci寄存器中，这样qemu就知道gpa */
	vp_iowrite16(virtqueue_get_vring_size(vq), &cfg->queue_size);
	vp_iowrite64_twopart(virtqueue_get_desc_addr(vq), &cfg->queue_desc_lo, &cfg->queue_desc_hi);
	vp_iowrite64_twopart(virtqueue_get_avail_addr(vq), &cfg->queue_avail_lo, &cfg->queue_avail_hi);
	vp_iowrite64_twopart(virtqueue_get_used_addr(vq), &cfg->queue_used_lo, &cfg->queue_used_hi);

	if (vp_dev->notify_base) {
		vq->priv = (void __force *)vp_dev->notify_base + off * vp_dev->notify_offset_multiplier;
	} else {
		vq->priv = (void __force *)map_capability(vp_dev->pci_dev,
					  vp_dev->notify_map_cap, 2, 2, off * vp_dev->notify_offset_multiplier, 2, NULL);
	}

	if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
		vp_iowrite16(msix_vec, &cfg->queue_msix_vector);
		msix_vec = vp_ioread16(&cfg->queue_msix_vector);
	}

	return vq;
}
```

### vring_create_virtqueue

```c
struct virtqueue *vring_create_virtqueue( unsigned int index, unsigned int num, unsigned int vring_align,
	struct virtio_device *vdev, bool weak_barriers, bool may_reduce_num,
	bool (*notify)(struct virtqueue *), void (*callback)(struct virtqueue *), const char *name)
{
	struct virtqueue *vq;
	void *queue = NULL;
	dma_addr_t dma_addr;
	size_t queue_size_in_bytes;
	struct vring vring;

	/* We assume num is a power of 2. */
	if (num & (num - 1)) {
		dev_warn(&vdev->dev, "Bad virtqueue length %u\n", num);
		return NULL;
	}

	/* TODO: allocate each queue chunk individually */
	for (; num && vring_size(num, vring_align) > PAGE_SIZE; num /= 2) {
		/* 分配的是子机的物理地址 */
		queue = vring_alloc_queue(vdev, vring_size(num, vring_align), &dma_addr,
                                  GFP_KERNEL|__GFP_NOWARN|__GFP_ZERO);
		if (queue)
			break;
	}


	if (!queue) {
		/* Try to get a single page. You are my only hope! */
		queue = vring_alloc_queue(vdev, vring_size(num, vring_align), &dma_addr, GFP_KERNEL|__GFP_ZERO);
	}

	queue_size_in_bytes = vring_size(num, vring_align);
	vring_init(&vring, num, queue, vring_align);

	vq = __vring_new_virtqueue(index, vring, vdev, weak_barriers, notify, callback, name);

	to_vvq(vq)->queue_dma_addr = dma_addr;
	to_vvq(vq)->queue_size_in_bytes = queue_size_in_bytes;
	to_vvq(vq)->we_own_ring = true;

	return vq;
}

static inline unsigned vring_size(unsigned int num, unsigned long align)
{
	return ((sizeof(struct vring_desc) * num + sizeof(__virtio16) * (3 + num) + align - 1) & ~(align - 1))
		+ sizeof(__virtio16) * 3 + sizeof(struct vring_used_elem) * num;
}

static inline void vring_init(struct vring *vr, unsigned int num, void *p, unsigned long align)
{
	vr->num = num;
	vr->desc = p;
	/* vring_desc后面是avail，  */
	vr->avail = p + num * sizeof(struct vring_desc);
	vr->used = (void *)(((uintptr_t)&vr->avail->ring[num] + sizeof(__virtio16) + align - 1) & ~(align - 1));
}

```

### __vring_new_virtqueue

```c
struct virtqueue *__vring_new_virtqueue(unsigned int index, struct vring vring, struct virtio_device *vdev,
					bool weak_barriers, bool (*notify)(struct virtqueue *),
					void (*callback)(struct virtqueue *), const char *name)
{
	unsigned int i;
	struct vring_virtqueue *vq;

	vq = kmalloc(sizeof(*vq) + vring.num * sizeof(struct vring_desc_state), GFP_KERNEL);

	vq->vring = vring;
	vq->vq.callback = callback;
	vq->vq.vdev = vdev;
	vq->vq.name = name;
	vq->vq.num_free = vring.num;
	vq->vq.index = index;
	vq->we_own_ring = false;
	vq->queue_dma_addr = 0;
	vq->queue_size_in_bytes = 0;
	vq->notify = notify;
	vq->weak_barriers = weak_barriers;
	vq->broken = false;
	vq->last_used_idx = 0;
	vq->num_added = 0;
	list_add_tail(&vq->vq.list, &vdev->vqs);
#ifdef DEBUG
	vq->in_use = false;
	vq->last_add_time_valid = false;
#endif

	vq->indirect = virtio_has_feature(vdev, VIRTIO_RING_F_INDIRECT_DESC);
	vq->event = virtio_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX);

	/* No callback?  Tell other side not to bother us. */
	if (!callback)
		vq->vring.avail->flags |= cpu_to_virtio16(vdev, VRING_AVAIL_F_NO_INTERRUPT);

	/* Put everything in free lists. */
	vq->free_head = 0;
    /* 初始化desc，使desc[i].next指向下一个 */
	for (i = 0; i < vring.num-1; i++)
		vq->vring.desc[i].next = cpu_to_virtio16(vdev, i + 1);

	memset(vq->desc_state, 0, vring.num * sizeof(struct vring_desc_state));

	return &vq->vq;
}
```

## virtio_device_ready

```c
static inline void virtio_device_ready(struct virtio_device *dev)
{
	unsigned status = dev->config->get_status(dev);
    /* 写pci寄存器 */
	dev->config->set_status(dev, status | VIRTIO_CONFIG_S_DRIVER_OK);
}
```

## 发包

### start_xmit

```c
static netdev_tx_t start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int qnum = skb_get_queue_mapping(skb);
	struct send_queue *sq = &vi->sq[qnum];
	int err;
	struct netdev_queue *txq = netdev_get_tx_queue(dev, qnum);
	bool kick = !skb->xmit_more;

	/* Free up any pending old buffers before queueing new ones. */
	free_old_xmit_skbs(sq);

	/* Try to transmit */
	err = xmit_skb(sq, skb);


	/* Don't wait up for transmitted skbs to be freed. */
	skb_orphan(skb);
	nf_reset(skb);

	/* Apparently nice girls don't return TX_BUSY; stop the queue
	 * before it gets out of hand.  Naturally, this wastes entries. */
	if (sq->vq->num_free < 2+MAX_SKB_FRAGS) {
		netif_stop_subqueue(dev, qnum);
		if (unlikely(!virtqueue_enable_cb_delayed(sq->vq))) {
			/* More just got used, free them then recheck. */
			free_old_xmit_skbs(sq);
			if (sq->vq->num_free >= 2+MAX_SKB_FRAGS) {
				netif_start_subqueue(dev, qnum);
				virtqueue_disable_cb(sq->vq);
			}
		}
	}

	if (kick || netif_xmit_stopped(txq))
		virtqueue_kick(sq->vq); /* 通知host */

	return NETDEV_TX_OK;
}
```

#### xmit_skb

```c
static int xmit_skb(struct send_queue *sq, struct sk_buff *skb)
{
	struct virtio_net_hdr_mrg_rxbuf *hdr;
	const unsigned char *dest = ((struct ethhdr *)skb->data)->h_dest;
	struct virtnet_info *vi = sq->vq->vdev->priv;
	unsigned num_sg;
	unsigned hdr_len = vi->hdr_len;
	bool can_push;

	pr_debug("%s: xmit %p %pM\n", vi->dev->name, skb, dest);

	can_push = vi->any_header_sg &&
		!((unsigned long)skb->data & (__alignof__(*hdr) - 1)) &&
		!skb_header_cloned(skb) && skb_headroom(skb) >= hdr_len;
	/* Even if we can, don't push here yet as this would skew
	 * csum_start offset below. */
	if (can_push)
		hdr = (struct virtio_net_hdr_mrg_rxbuf *)(skb->data - hdr_len);
	else
		hdr = skb_vnet_hdr(skb);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		hdr->hdr.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
		hdr->hdr.csum_start = cpu_to_virtio16(vi->vdev, skb_checksum_start_offset(skb));
		hdr->hdr.csum_offset = cpu_to_virtio16(vi->vdev, skb->csum_offset);
	} else {
		hdr->hdr.flags = 0;
		hdr->hdr.csum_offset = hdr->hdr.csum_start = 0;
	}

	if (skb_is_gso(skb)) {
		hdr->hdr.hdr_len = cpu_to_virtio16(vi->vdev, skb_headlen(skb));
		hdr->hdr.gso_size = cpu_to_virtio16(vi->vdev, skb_shinfo(skb)->gso_size);
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_UDP;
		else
			BUG();
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCP_ECN)
			hdr->hdr.gso_type |= VIRTIO_NET_HDR_GSO_ECN;
	} else {
		hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_NONE;
		hdr->hdr.gso_size = hdr->hdr.hdr_len = 0;
	}

	if (vi->mergeable_rx_bufs)
		hdr->num_buffers = 0;

	sg_init_table(sq->sg, MAX_SKB_FRAGS + 2);
	if (can_push) {
		__skb_push(skb, hdr_len);
		num_sg = skb_to_sgvec(skb, sq->sg, 0, skb->len);
		/* Pull header back to avoid skew in tx bytes calculations. */
		__skb_pull(skb, hdr_len);
	} else {
		sg_set_buf(sq->sg, hdr, hdr_len);
		num_sg = skb_to_sgvec(skb, sq->sg + 1, 0, skb->len) + 1;
	}
    /* 将初始化的sg放入vring中 */
	return virtqueue_add_outbuf(sq->vq, sq->sg, num_sg, skb, GFP_ATOMIC);
}
```

#### skb_to_sgvec

```c
int skb_to_sgvec(struct sk_buff *skb, struct scatterlist *sg, int offset, int len)
{
	int nsg = __skb_to_sgvec(skb, sg, offset, len);

	sg_mark_end(&sg[nsg - 1]);

	return nsg;
}
```

#### __skb_to_sgvec

将skb转换成sgvec

```c
static int __skb_to_sgvec(struct sk_buff *skb, struct scatterlist *sg, int offset, int len)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	struct sk_buff *frag_iter;
	int elt = 0;

	if (copy > 0) {
		if (copy > len)
			copy = len;
		sg_set_buf(sg, skb->data + offset, copy);
		elt++;
		if ((len -= copy) == 0)
			return elt;
		offset += copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(&skb_shinfo(skb)->frags[i]);
		if ((copy = end - offset) > 0) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			if (copy > len)
				copy = len;
			sg_set_page(&sg[elt], skb_frag_page(frag), copy, frag->page_offset+offset-start);
			elt++;
			if (!(len -= copy))
				return elt;
			offset += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			if (copy > len)
				copy = len;
			elt += __skb_to_sgvec(frag_iter, sg+elt, offset - start,
					      copy);
			if ((len -= copy) == 0)
				return elt;
			offset += copy;
		}
		start = end;
	}
	BUG_ON(len);
	return elt;
}
```

#### sg_set_page

```c
static inline void sg_set_page(struct scatterlist *sg, struct page *page, unsigned int len, unsigned int offset)
{
	sg_assign_page(sg, page);
	sg->offset = offset;
	sg->length = len;
}
```

### virtqueue_add_outbuf

```c
int virtqueue_add_outbuf(struct virtqueue *vq, struct scatterlist *sg, unsigned int num,
			 void *data, gfp_t gfp)
{
    /* 只有out_sgs */
	return virtqueue_add(vq, &sg, num, 1, 0, data, gfp);
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

#ifdef DEBUG
	{
		ktime_t now = ktime_get();

		/* No kick or get, with .1 second between?  Warn. */
		if (vq->last_add_time_valid)
			WARN_ON(ktime_to_ms(ktime_sub(now, vq->last_add_time)) > 100);
		vq->last_add_time = now;
		vq->last_add_time_valid = true;
	}
#endif

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

	/* 处理out_sgs，此处out_sgs为1 */
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
	/* 处理in_sgs，此处in_sgs为0 */
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
	/* 获取avail->idx */
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

