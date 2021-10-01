/* Internal representation of a send virtqueue */
struct send_queue {
	/* Virtqueue associated with this send _queue */
	struct virtqueue *vq;

	/* TX: fragments + linear part + virtio header */
	struct scatterlist sg[MAX_SKB_FRAGS + 2];

	/* Name of the send queue: output.$index */
	char name[40];
};

/* Internal representation of a receive virtqueue */
struct receive_queue {
	/* Virtqueue associated with this receive_queue */
	struct virtqueue *vq;

	struct napi_struct napi;

	/* Number of input buffers, and max we've ever had. */
	unsigned int num, max;

	/* Chain pages by the private ptr. */
	struct page *pages;

	/* RX: fragments + linear part + virtio header */
	struct scatterlist sg[MAX_SKB_FRAGS + 2];

	/* Name of this receive queue: input.$index */
	char name[40];
};


struct virtnet_info {
	struct virtio_device *vdev;
	struct virtqueue *cvq;
	struct net_device *dev;
	struct send_queue *sq;
	struct receive_queue *rq;
	unsigned int status;

	/* Max # of queue pairs supported by the device */
	u16 max_queue_pairs;

	/* # of queue pairs currently used by the driver */
	u16 curr_queue_pairs;

	/* I like... big packets and I cannot lie! */
	bool big_packets;

	/* Host will merge rx buffers for big packets (shake it! shake it!) */
	bool mergeable_rx_bufs;

	/* Has control virtqueue */
	bool has_cvq;

	/* Host can handle any s/g split between our header and packet data */
	bool any_header_sg;

	/* Packet virtio header size */
	u8 hdr_len;

	/* Active statistics */
	struct virtnet_stats __percpu *stats;

	/* Work struct for refilling if we run low on memory. */
	struct delayed_work refill;

	/* Work struct for config space updates */
	struct work_struct config_work;

	/* Does the affinity hint is set for virtqueues? */
	bool affinity_hint_set;

	/* Per-cpu variable to show the mapping from CPU to virtqueue */
	int __percpu *vq_index;

	/* CPU hot plug notifier */
	struct notifier_block nb;

	/* Maximum allowed MTU */
	u16 max_mtu;
};

struct virtqueue {
	struct list_head list;
	void (*callback)(struct virtqueue *vq);
	const char *name;
	struct virtio_device *vdev;
	unsigned int index;
	unsigned int num_free;
	void *priv;
};

struct vring_virtqueue {
	struct virtqueue vq;

	/* Actual memory layout for this queue */
	struct vring vring;

	/* Can we use weak barriers? */
	bool weak_barriers;

	/* Other side has made a mess, don't try any more. */
	bool broken;

	/* Host supports indirect buffers */
	bool indirect;

	/* Host publishes avail event idx */
	bool event;

	/* Head of free buffer list. */
	unsigned int free_head;
	/* Number we've added since last sync. */
	unsigned int num_added;

	/* Last used index we've seen. */
	u16 last_used_idx;

	/* How to notify other side. FIXME: commonalize hcalls! */
	bool (*notify)(struct virtqueue *vq);

	/* DMA, allocation, and size information */
	bool we_own_ring;
	size_t queue_size_in_bytes;
	dma_addr_t queue_dma_addr;

#ifdef DEBUG
	/* They're supposed to lock for us. */
	unsigned int in_use;

	/* Figure out if their kicks are too delayed. */
	bool last_add_time_valid;
	ktime_t last_add_time;
#endif

	/* Per-descriptor state. */
	struct vring_desc_state desc_state[];
};

#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)


#define VIRTIO_RING_F_EVENT_IDX		29

/* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
struct vring_desc {
	/* Address (guest-physical). */
	__virtio64 addr;
	/* Length. */
	__virtio32 len;
	/* The flags as indicated above. */
	__virtio16 flags;
	/* We chain unused descriptors via this, too */
	__virtio16 next;
};

struct vring_avail {
	__virtio16 flags;
	__virtio16 idx;
	__virtio16 ring[];
};

/* u32 is used here for ids for padding reasons. */
struct vring_used_elem {
	/* Index of start of used descriptor chain. */
	__virtio32 id;
	/* Total length of the descriptor chain which was used (written to) */
	__virtio32 len;
};

struct vring_used {
	__virtio16 flags;
	__virtio16 idx;
	struct vring_used_elem ring[];
};

struct vring {
	unsigned int num;

	struct vring_desc *desc;

	struct vring_avail *avail;

	struct vring_used *used;
};


static const struct net_device_ops virtnet_netdev = {
	.ndo_open            = virtnet_open,
	.ndo_stop   	     = virtnet_close,
	.ndo_start_xmit      = start_xmit,
	.ndo_validate_addr   = eth_validate_addr,
	.ndo_set_mac_address = virtnet_set_mac_address,
	.ndo_set_rx_mode     = virtnet_set_rx_mode,
	.ndo_change_mtu	     = virtnet_change_mtu,
	.ndo_get_stats64     = virtnet_stats,
	.ndo_vlan_rx_add_vid = virtnet_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = virtnet_vlan_rx_kill_vid,
	.ndo_select_queue     = virtnet_select_queue,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = virtnet_netpoll,
#endif
	.ndo_features_check	= passthru_features_check,
};


static int virtnet_probe(struct virtio_device *vdev)
{
	int i, err;
	struct net_device *dev;
	struct virtnet_info *vi;
	u16 max_queue_pairs;
	int mtu;

	/* Find if host supports multiqueue virtio_net device */
	err = virtio_cread_feature(vdev, VIRTIO_NET_F_MQ,
				   struct virtio_net_config,
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

	err = register_netdev(dev);

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

	pr_debug("virtnet: registered device %s with %d RX and TX vq's\n",
		 dev->name, max_queue_pairs);

	return 0;
}

static inline
void virtio_device_ready(struct virtio_device *dev)
{
	unsigned status = dev->config->get_status(dev);

	BUG_ON(status & VIRTIO_CONFIG_S_DRIVER_OK);
	dev->config->set_status(dev, status | VIRTIO_CONFIG_S_DRIVER_OK);
}

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
	total_vqs = vi->max_queue_pairs * 2 +
		    virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VQ);

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
	ret = vi->vdev->config->find_vqs(vi->vdev, total_vqs, vqs, callbacks,
					 names);

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

static int txq2vq(int txq)
{
	return txq * 2 + 1;
}

static int rxq2vq(int rxq)
{
	return rxq * 2;
}

static int vp_modern_find_vqs(struct virtio_device *vdev, unsigned nvqs,
			      struct virtqueue *vqs[],
			      vq_callback_t *callbacks[],
			      const char * const names[])
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

/* the config->find_vqs() implementation */
int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		struct virtqueue *vqs[],
		vq_callback_t *callbacks[],
		const char * const names[])
{
	int err;

	/* Try MSI-X with one vector per queue. */
	err = vp_try_to_find_vqs(vdev, nvqs, vqs, callbacks, names, true, true);
	if (!err)
		return 0;
	/* Fallback: MSI-X with one vector for config, one shared for queues. */
	err = vp_try_to_find_vqs(vdev, nvqs, vqs, callbacks, names,
				 true, false);
	if (!err)
		return 0;
	/* Finally fall back to regular interrupts. */
	return vp_try_to_find_vqs(vdev, nvqs, vqs, callbacks, names,
				  false, false);
}

static int vp_try_to_find_vqs(struct virtio_device *vdev, unsigned nvqs,
			      struct virtqueue *vqs[],
			      vq_callback_t *callbacks[],
			      const char * const names[],
			      bool use_msix,
			      bool per_vq_vectors)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u16 msix_vec;
	int i, err, nvectors, allocated_vectors;

	vp_dev->vqs = kmalloc(nvqs * sizeof *vp_dev->vqs, GFP_KERNEL);


	if (!use_msix) {
		/* Old style: one normal interrupt for change and all vqs. */
		err = vp_request_intx(vdev);
		if (err)
			goto error_find;
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
		/* 控制vq   name 为NULL */
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
		err = request_irq(vp_dev->msix_entries[msix_vec].vector,
				  vring_interrupt, 0, vp_dev->msix_names[msix_vec],
				  vqs[i]);
	}
	return 0;
}

static struct virtqueue *vp_setup_vq(struct virtio_device *vdev, unsigned index,
				     void (*callback)(struct virtqueue *vq),
				     const char *name,
				     u16 msix_vec)
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

static struct virtqueue *setup_vq(struct virtio_pci_device *vp_dev,
				  struct virtio_pci_vq_info *info,
				  unsigned index,
				  void (*callback)(struct virtqueue *vq),
				  const char *name,
				  u16 msix_vec)
{
	struct virtio_pci_common_cfg __iomem *cfg = vp_dev->common;
	struct virtqueue *vq;
	u16 num, off;
	int err;


	/* Select the queue we're interested in */
	vp_iowrite16(index, &cfg->queue_select);

	/* Check if queue is either not available or already active. */
	num = vp_ioread16(&cfg->queue_size);


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


	/* activate the queue */
	vp_iowrite16(virtqueue_get_vring_size(vq), &cfg->queue_size);
	vp_iowrite64_twopart(virtqueue_get_desc_addr(vq),
			     &cfg->queue_desc_lo, &cfg->queue_desc_hi);
	vp_iowrite64_twopart(virtqueue_get_avail_addr(vq),
			     &cfg->queue_avail_lo, &cfg->queue_avail_hi);
	vp_iowrite64_twopart(virtqueue_get_used_addr(vq),
			     &cfg->queue_used_lo, &cfg->queue_used_hi);

	if (vp_dev->notify_base) {
		/* offset should not wrap */
		if ((u64)off * vp_dev->notify_offset_multiplier + 2 > vp_dev->notify_len) {
			dev_warn(&vp_dev->pci_dev->dev,
				 "bad notification offset %u (x %u) "
				 "for queue %u > %zd",
				 off, vp_dev->notify_offset_multiplier,
				 index, vp_dev->notify_len);
			err = -EINVAL;
			goto err_map_notify;
		}
		vq->priv = (void __force *)vp_dev->notify_base +
			off * vp_dev->notify_offset_multiplier;
	} else {
		vq->priv = (void __force *)map_capability(vp_dev->pci_dev,
					  vp_dev->notify_map_cap, 2, 2,
					  off * vp_dev->notify_offset_multiplier, 2,
					  NULL);
	}


	if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
		vp_iowrite16(msix_vec, &cfg->queue_msix_vector);
		msix_vec = vp_ioread16(&cfg->queue_msix_vector);
		if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
			err = -EBUSY;
			goto err_assign_vector;
		}
	}

	return vq;
}

struct virtqueue *vring_create_virtqueue(
	unsigned int index,
	unsigned int num,
	unsigned int vring_align,
	struct virtio_device *vdev,
	bool weak_barriers,
	bool may_reduce_num,
	bool (*notify)(struct virtqueue *),
	void (*callback)(struct virtqueue *),
	const char *name)
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
		queue = vring_alloc_queue(vdev, vring_size(num, vring_align),
					  &dma_addr, GFP_KERNEL|__GFP_NOWARN|__GFP_ZERO);
		if (queue)
			break;
	}


	if (!queue) {
		/* Try to get a single page. You are my only hope! */
		queue = vring_alloc_queue(vdev, vring_size(num, vring_align),
					  &dma_addr, GFP_KERNEL|__GFP_ZERO);
	}

	queue_size_in_bytes = vring_size(num, vring_align);
	vring_init(&vring, num, queue, vring_align);

	vq = __vring_new_virtqueue(index, vring, vdev, weak_barriers,
				   notify, callback, name);

	to_vvq(vq)->queue_dma_addr = dma_addr;
	to_vvq(vq)->queue_size_in_bytes = queue_size_in_bytes;
	to_vvq(vq)->we_own_ring = true;

	return vq;
}

static inline unsigned vring_size(unsigned int num, unsigned long align)
{
	return ((sizeof(struct vring_desc) * num + sizeof(__virtio16) * (3 + num)
		 + align - 1) & ~(align - 1))
		+ sizeof(__virtio16) * 3 + sizeof(struct vring_used_elem) * num;
}

static inline void vring_init(struct vring *vr, unsigned int num, void *p,
			      unsigned long align)
{
	vr->num = num;
	vr->desc = p;
	/* vring_desc后面是avail，  */
	vr->avail = p + num * sizeof(struct vring_desc);
	vr->used = (void *)(((uintptr_t)&vr->avail->ring[num] + sizeof(__virtio16)
		+ align - 1) & ~(align - 1));
}

#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)

struct virtqueue *__vring_new_virtqueue(unsigned int index,
					struct vring vring,
					struct virtio_device *vdev,
					bool weak_barriers,
					bool (*notify)(struct virtqueue *),
					void (*callback)(struct virtqueue *),
					const char *name)
{
	unsigned int i;
	struct vring_virtqueue *vq;

	vq = kmalloc(sizeof(*vq) + vring.num * sizeof(struct vring_desc_state),
		     GFP_KERNEL);
	if (!vq)
		return NULL;

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
	for (i = 0; i < vring.num-1; i++)
		vq->vring.desc[i].next = cpu_to_virtio16(vdev, i + 1);

	memset(vq->desc_state, 0, vring.num * sizeof(struct vring_desc_state));

	return &vq->vq;
}

static inline u16 skb_get_queue_mapping(const struct sk_buff *skb)
{
	return skb->queue_mapping;
}

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
		virtqueue_kick(sq->vq);

	return NETDEV_TX_OK;
}

static void free_old_xmit_skbs(struct send_queue *sq)
{
	struct sk_buff *skb;
	unsigned int len;
	struct virtnet_info *vi = sq->vq->vdev->priv;
	struct virtnet_stats *stats = this_cpu_ptr(vi->stats);

	while ((skb = virtqueue_get_buf(sq->vq, &len)) != NULL) {
		pr_debug("Sent skb %p\n", skb);

		u64_stats_update_begin(&stats->tx_syncp);
		stats->tx_bytes += skb->len;
		stats->tx_packets++;
		u64_stats_update_end(&stats->tx_syncp);

		dev_kfree_skb_any(skb);
	}
}

/**
 * virtqueue_get_buf - get the next used buffer
 * @vq: the struct virtqueue we're talking about.
 * @len: the length written into the buffer
 *
 * If the driver wrote data into the buffer, @len will be set to the
 * amount written.  This means you don't need to clear the buffer
 * beforehand to ensure there's no data leakage in the case of short
 * writes.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 *
 * Returns NULL if there are no used buffers, or the "data" token
 * handed to virtqueue_add_*().
 */
void *virtqueue_get_buf(struct virtqueue *_vq, unsigned int *len)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	void *ret;
	unsigned int i;
	u16 last_used;

	START_USE(vq);

	if (unlikely(vq->broken)) {
		END_USE(vq);
		return NULL;
	}

	if (!more_used(vq)) {
		pr_debug("No more buffers in queue\n");
		END_USE(vq);
		return NULL;
	}

	/* Only get used array entries after they have been exposed by host. */
	virtio_rmb(vq->weak_barriers);

	last_used = (vq->last_used_idx & (vq->vring.num - 1));
	i = virtio32_to_cpu(_vq->vdev, vq->vring.used->ring[last_used].id);
	*len = virtio32_to_cpu(_vq->vdev, vq->vring.used->ring[last_used].len);

	if (unlikely(i >= vq->vring.num)) {
		BAD_RING(vq, "id %u out of range\n", i);
		return NULL;
	}
	if (unlikely(!vq->desc_state[i].data)) {
		BAD_RING(vq, "id %u is not a head!\n", i);
		return NULL;
	}

	/* detach_buf clears data, so grab it now. */
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

static inline bool more_used(const struct vring_virtqueue *vq)
{
	return vq->last_used_idx != virtio16_to_cpu(vq->vq.vdev, vq->vring.used->idx);
}

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
		hdr->hdr.csum_start = cpu_to_virtio16(vi->vdev,
						skb_checksum_start_offset(skb));
		hdr->hdr.csum_offset = cpu_to_virtio16(vi->vdev,
							 skb->csum_offset);
	} else {
		hdr->hdr.flags = 0;
		hdr->hdr.csum_offset = hdr->hdr.csum_start = 0;
	}

	if (skb_is_gso(skb)) {
		hdr->hdr.hdr_len = cpu_to_virtio16(vi->vdev, skb_headlen(skb));
		hdr->hdr.gso_size = cpu_to_virtio16(vi->vdev,
						    skb_shinfo(skb)->gso_size);
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
	return virtqueue_add_outbuf(sq->vq, sq->sg, num_sg, skb, GFP_ATOMIC);
}

int virtqueue_add_outbuf(struct virtqueue *vq,
			 struct scatterlist *sg, unsigned int num,
			 void *data,
			 gfp_t gfp)
{
	return virtqueue_add(vq, &sg, num, 1, 0, data, gfp);
}

			 
static inline int virtqueue_add(struct virtqueue *_vq,
				struct scatterlist *sgs[],
				unsigned int total_sg,
				unsigned int out_sgs,
				unsigned int in_sgs,
				void *data,
				gfp_t gfp)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	struct scatterlist *sg;
	struct vring_desc *desc;
	unsigned int i, n, avail, descs_used, uninitialized_var(prev), err_idx;
	int head;
	bool indirect;

	START_USE(vq);


	if (unlikely(vq->broken)) {
		END_USE(vq);
		return -EIO;
	}

#ifdef DEBUG
	{
		ktime_t now = ktime_get();

		/* No kick or get, with .1 second between?  Warn. */
		if (vq->last_add_time_valid)
			WARN_ON(ktime_to_ms(ktime_sub(now, vq->last_add_time))
					    > 100);
		vq->last_add_time = now;
		vq->last_add_time_valid = true;
	}
#endif

	BUG_ON(total_sg > vq->vring.num);
	BUG_ON(total_sg == 0);

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

	if (vq->vq.num_free < descs_used) {
		pr_debug("Can't add buf len %i - avail = %i\n",
			 descs_used, vq->vq.num_free);
		/* FIXME: for historical reasons, we force a notify here if
		 * there are outgoing parts to the buffer.  Presumably the
		 * host should service the ring ASAP. */
		if (out_sgs)
			vq->notify(&vq->vq);
		END_USE(vq);
		return -ENOSPC;
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

	/* Last one doesn't continue. */
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
	/* 将head，即desc开始的索引放到avail的ring数组中
	* avail->ring[]里面存放的是desc开始的索引
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

static struct vring_desc *alloc_indirect(struct virtqueue *_vq,
					 unsigned int total_sg, gfp_t gfp)
{
	struct vring_desc *desc;
	unsigned int i;

	/*
	 * We require lowmem mappings for the descriptors because
	 * otherwise virt_to_phys will give us bogus addresses in the
	 * virtqueue.
	 */
	gfp &= ~(__GFP_HIGHMEM | __GFP_HIGH);

	desc = kmalloc(total_sg * sizeof(struct vring_desc), gfp);
	if (!desc)
		return NULL;

	for (i = 0; i < total_sg; i++)
		desc[i].next = cpu_to_virtio16(_vq->vdev, i + 1);
	return desc;
}

/* Map one sg entry. */
static dma_addr_t vring_map_one_sg(const struct vring_virtqueue *vq,
				   struct scatterlist *sg,
				   enum dma_data_direction direction)
{
	if (!vring_use_dma_api(vq->vq.vdev))
		return (dma_addr_t)sg_phys(sg);

	/*
	 * We can't use dma_map_sg, because we don't use scatterlists in
	 * the way it expects (we don't guarantee that the scatterlist
	 * will exist for the lifetime of the mapping).
	 */
	return dma_map_page(vring_dma_dev(vq),
			    sg_page(sg), sg->offset, sg->length,
			    direction);
}

static inline dma_addr_t sg_phys(struct scatterlist *sg)
{
	return page_to_phys(sg_page(sg)) + sg->offset;
}

