/* module_pci_driver(virtio_pci_driver);
 * 
 */
struct vring {
	unsigned int num;
	struct vring_desc *desc;
	struct vring_avail *avail;
	struct vring_used *used;
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

struct virtio_pci_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the list node for the virtqueues list */
	struct list_head node;

	/* MSI-X vector (or none) */
	unsigned msix_vector;
};


struct virtio_blk {
	struct virtio_device *vdev;

	/* The disk structure for the kernel. */
	struct gendisk *disk;

	/* Block layer tags. */
	struct blk_mq_tag_set tag_set;

	/* Process context for config space updates */
	struct work_struct config_work;

	/* What host tells us, plus 2 for header & tailer. */
	unsigned int sg_elems;

	/* Ida index - used to track minor number allocations. */
	int index;

	/* num of vqs */
	int num_vqs;
	struct virtio_blk_vq *vqs;
};

/* Common configuration */
#define VIRTIO_PCI_CAP_COMMON_CFG	1
/* Notifications */
#define VIRTIO_PCI_CAP_NOTIFY_CFG	2
/* ISR access */
#define VIRTIO_PCI_CAP_ISR_CFG		3
/* Device specific configuration */
#define VIRTIO_PCI_CAP_DEVICE_CFG	4
/* PCI configuration access */
#define VIRTIO_PCI_CAP_PCI_CFG		5

/* This is the PCI capability header: */
struct virtio_pci_cap {
	__u8 cap_vndr;		/* Generic PCI field: PCI_CAP_ID_VNDR */
	__u8 cap_next;		/* Generic PCI field: next ptr. */
	__u8 cap_len;		/* Generic PCI field: capability length */
	__u8 cfg_type;		/* Identifies the structure. */
	__u8 bar;		/* Where to find it. */
	__u8 padding[3];	/* Pad to full dword. */
	__le32 offset;		/* Offset within bar. */
	__le32 length;		/* Length of the structure, in bytes. */
};

struct virtio_pci_notify_cap {
	struct virtio_pci_cap cap;
	__le32 notify_off_multiplier;	/* Multiplier for queue_notify_off. */
};

/* Fields in VIRTIO_PCI_CAP_COMMON_CFG: */
struct virtio_pci_common_cfg {
	/* About the whole device. */
	__le32 device_feature_select;	/* read-write */
	__le32 device_feature;		/* read-only */
	__le32 guest_feature_select;	/* read-write */
	__le32 guest_feature;		/* read-write */
	__le16 msix_config;		/* read-write */
	__le16 num_queues;		/* read-only */
	__u8 device_status;		/* read-write */
	__u8 config_generation;		/* read-only */

	/* About a specific virtqueue. */
	__le16 queue_select;		/* read-write */
	__le16 queue_size;		/* read-write, power of 2. */
	__le16 queue_msix_vector;	/* read-write */
	__le16 queue_enable;		/* read-write */
	__le16 queue_notify_off;	/* read-only */
	__le32 queue_desc_lo;		/* read-write */
	__le32 queue_desc_hi;		/* read-write */
	__le32 queue_avail_lo;		/* read-write */
	__le32 queue_avail_hi;		/* read-write */
	__le32 queue_used_lo;		/* read-write */
	__le32 queue_used_hi;		/* read-write */
};

/* Our device structure */
struct virtio_pci_device {
	struct virtio_device vdev;
	struct pci_dev *pci_dev;

	/* In legacy mode, these two point to within ->legacy. */
	/* Where to read and clear interrupt */
	u8 __iomem *isr;

	/* Modern only fields */
	/* The IO mapping for the PCI config space (non-legacy mode) */
	struct virtio_pci_common_cfg __iomem *common;
	/* Device-specific data (non-legacy mode)  */
	void __iomem *device;
	/* Base of vq notifications (non-legacy mode). */
	void __iomem *notify_base;

	/* So we can sanity-check accesses. */
	size_t notify_len;
	size_t device_len;

	/* Capability for when we need to map notifications per-vq. */
	int notify_map_cap;

	/* Multiply queue_notify_off by this value. (non-legacy mode). */
	u32 notify_offset_multiplier;

	int modern_bars;

	/* Legacy only field */
	/* the IO mapping for the PCI config space */
	void __iomem *ioaddr;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;

	/* array of all queues for house-keeping */
	struct virtio_pci_vq_info **vqs;

	/* MSI-X support */
	int msix_enabled;
	int intx_enabled;
	cpumask_var_t *msix_affinity_masks;
	/* Name strings for interrupts. This size should be enough,
	 * and I'm too lazy to allocate each name separately. */
	char (*msix_names)[256];
	/* Number of available vectors */
	unsigned msix_vectors;
	/* Vectors allocated, excluding per-vq vectors if any */
	unsigned msix_used_vectors;

	/* Whether we have vector per vq */
	bool per_vq_vectors;

	struct virtqueue *(*setup_vq)(struct virtio_pci_device *vp_dev,
				      struct virtio_pci_vq_info *info,
				      unsigned idx,
				      void (*callback)(struct virtqueue *vq),
				      const char *name,
				      bool ctx,
				      u16 msix_vec);
	void (*del_vq)(struct virtio_pci_vq_info *info);

	u16 (*config_vector)(struct virtio_pci_device *vp_dev, u16 vector);
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

	/* Last written value to avail->flags */
	u16 avail_flags_shadow;

	/* Last written value to avail->idx in guest byte order */
	u16 avail_idx_shadow;

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


/* pci driver */
/* Qumranet donated their vendor ID for devices 0x1000 thru 0x10FF. */
static const struct pci_device_id virtio_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REDHAT_QUMRANET, PCI_ANY_ID) },
	{ 0 }
};

static struct pci_driver virtio_pci_driver = {
	.name		= "virtio-pci",
	.id_table	= virtio_pci_id_table,
	.probe		= virtio_pci_probe,
	.remove		= virtio_pci_remove,
#ifdef CONFIG_PM_SLEEP
	.driver.pm	= &virtio_pci_pm_ops,
#endif
};

module_pci_driver(virtio_pci_driver);

static int virtio_pci_probe(struct pci_dev *pci_dev,
			    const struct pci_device_id *id)
{
	struct virtio_pci_device *vp_dev;
	int rc;

	/* allocate our structure and fill it out */
	vp_dev = kzalloc(sizeof(struct virtio_pci_device), GFP_KERNEL);

	pci_set_drvdata(pci_dev, vp_dev);
	vp_dev->vdev.dev.parent = &pci_dev->dev;
	vp_dev->vdev.dev.release = virtio_pci_release_dev;
	vp_dev->pci_dev = pci_dev;
	INIT_LIST_HEAD(&vp_dev->virtqueues);
	spin_lock_init(&vp_dev->lock);

	/* enable the device */
	rc = pci_enable_device(pci_dev);

	if (force_legacy) {
		rc = virtio_pci_legacy_probe(vp_dev);
	} else {
		rc = virtio_pci_modern_probe(vp_dev);
	}

	pci_set_master(pci_dev);

	/* 注册virtio device */
	rc = register_virtio_device(&vp_dev->vdev);

	return 0;

}

static void *vring_alloc_queue(struct virtio_device *vdev, size_t size,
			      dma_addr_t *dma_handle, gfp_t flag)
{
	if (vring_use_dma_api(vdev)) {
		return dma_alloc_coherent(vdev->dev.parent, size,
					  dma_handle, flag);
	} else {
		void *queue = alloc_pages_exact(PAGE_ALIGN(size), flag);
		if (queue) {
			phys_addr_t phys_addr = virt_to_phys(queue);
			*dma_handle = (dma_addr_t)phys_addr;

			/*
			 * Sanity check: make sure we dind't truncate
			 * the address.  The only arches I can find that
			 * have 64-bit phys_addr_t but 32-bit dma_addr_t
			 * are certain non-highmem MIPS and x86
			 * configurations, but these configurations
			 * should never allocate physical pages above 32
			 * bits, so this is fine.  Just in case, throw a
			 * warning and abort if we end up with an
			 * unrepresentable address.
			 */
			if (WARN_ON_ONCE(*dma_handle != phys_addr)) {
				free_pages_exact(queue, PAGE_ALIGN(size));
				return NULL;
			}
		}
		return queue;
	}
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

struct virtqueue *__vring_new_virtqueue(unsigned int index,
					struct vring vring,
					struct virtio_device *vdev,
					bool weak_barriers,
					bool context,
					bool (*notify)(struct virtqueue *),
					void (*callback)(struct virtqueue *),
					const char *name)
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
	vq->avail_flags_shadow = 0;
	vq->avail_idx_shadow = 0;
	vq->num_added = 0;
	list_add_tail(&vq->vq.list, &vdev->vqs);
#ifdef DEBUG
	vq->in_use = false;
	vq->last_add_time_valid = false;
#endif
	/* 支持这个特性，context 为1 */
	vq->indirect = virtio_has_feature(vdev, VIRTIO_RING_F_INDIRECT_DESC) && !context;
	/* 不支持这个特性 */
	vq->event = virtio_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX);

	/* No callback?  Tell other side not to bother us. */
	if (!callback) {
		vq->avail_flags_shadow |= VRING_AVAIL_F_NO_INTERRUPT;
		if (!vq->event)
			vq->vring.avail->flags = cpu_to_virtio16(vdev, vq->avail_flags_shadow);
	}

	/* Put everything in free lists. */
	vq->free_head = 0;
	for (i = 0; i < vring.num - 1; i++)
		vq->vring.desc[i].next = cpu_to_virtio16(vdev, i + 1);/* 将desc链起来 */

	memset(vq->desc_state, 0, vring.num * sizeof(struct vring_desc_state));

	return &vq->vq;
}

#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)


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


static inline unsigned vring_size(unsigned int num, unsigned long align)
{
	return ((sizeof(struct vring_desc) * num + sizeof(__virtio16) * (3 + num)
		 + align - 1) & ~(align - 1))
		+ sizeof(__virtio16) * 3 + sizeof(struct vring_used_elem) * num;
}

struct virtqueue *vring_create_virtqueue(
	unsigned int index,
	unsigned int num,
	unsigned int vring_align,
	struct virtio_device *vdev,
	bool weak_barriers,
	bool may_reduce_num,
	bool context,
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
	/* virtio queue的深度是2的指数大小 */
	if (num & (num - 1)) {
		dev_warn(&vdev->dev, "Bad virtqueue length %u\n", num);
		return NULL;
	}

	/* TODO: allocate each queue chunk individually */
	/* 分配vring */
	for (; num && vring_size(num, vring_align) > PAGE_SIZE; num /= 2) {
		/* dma_addr是物理地址。queue是虚拟地址 */
		queue = vring_alloc_queue(vdev, vring_size(num, vring_align),
					  &dma_addr, GFP_KERNEL|__GFP_NOWARN|__GFP_ZERO);
		if (queue)
			break;
	}

	queue_size_in_bytes = vring_size(num, vring_align);
	vring_init(&vring, num, queue, vring_align);

	vq = __vring_new_virtqueue(index, vring, vdev, weak_barriers, context,
				   notify, callback, name);

	to_vvq(vq)->queue_dma_addr = dma_addr;
	to_vvq(vq)->queue_size_in_bytes = queue_size_in_bytes;
	to_vvq(vq)->we_own_ring = true;

	return vq;
}

/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq)
{
	/* we write the queue's selector into the notification register to
	 * signal the other end */
	iowrite16(vq->index, (void __iomem *)vq->priv);
	return true;
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

	if (index >= vp_ioread16(&cfg->num_queues))
		return ERR_PTR(-ENOENT);

	/* index 从0开始 */
	/* Select the queue we're interested in */
	vp_iowrite16(index, &cfg->queue_select);

	/* Check if queue is either not available or already active. */
	/* queue 的大小（深度） */
	num = vp_ioread16(&cfg->queue_size);

	/* get offset of notification word for this vq */
	off = vp_ioread16(&cfg->queue_notify_off);

	info->msix_vector = msix_vec;

	/* create the vring */
	vq = vring_create_virtqueue(index, num, SMP_CACHE_BYTES, &vp_dev->vdev,
				    true, true, vp_notify, callback, name);

	/* activate the queue */
	vp_iowrite16(virtqueue_get_vring_size(vq), &cfg->queue_size);
	vp_iowrite64_twopart(virtqueue_get_desc_addr(vq), &cfg->queue_desc_lo, &cfg->queue_desc_hi);
	vp_iowrite64_twopart(virtqueue_get_avail_addr(vq), &cfg->queue_avail_lo, &cfg->queue_avail_hi);
	vp_iowrite64_twopart(virtqueue_get_used_addr(vq), &cfg->queue_used_lo, &cfg->queue_used_hi);

	if (vp_dev->notify_base) {
		/* offset should not wrap */
		if ((u64)off * vp_dev->notify_offset_multiplier + 2 > vp_dev->notify_len) {
			dev_warn(&vp_dev->pci_dev->dev,
				 "bad notification offset %u (x %u) ""for queue %u > %zd",
				 off, vp_dev->notify_offset_multiplier, index, vp_dev->notify_len);
			err = -EINVAL;
			goto err_map_notify;
		}
		vq->priv = (void __force *)vp_dev->notify_base + off * vp_dev->notify_offset_multiplier;
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


static void del_vq(struct virtio_pci_vq_info *info)
{
	struct virtqueue *vq = info->vq;
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);

	vp_iowrite16(vq->index, &vp_dev->common->queue_select);

	if (vp_dev->msix_enabled) {
		vp_iowrite16(VIRTIO_MSI_NO_VECTOR,
			     &vp_dev->common->queue_msix_vector);
		/* Flush the write out to device */
		vp_ioread16(&vp_dev->common->queue_msix_vector);
	}

	if (!vp_dev->notify_base)
		pci_iounmap(vp_dev->pci_dev, (void __force __iomem *)vq->priv);

	vring_del_virtqueue(vq);
}


static u16 vp_config_vector(struct virtio_pci_device *vp_dev, u16 vector)
{
	/* Setup the vector used for configuration events */
	vp_iowrite16(vector, &vp_dev->common->msix_config);
	/* Verify we had enough resources to assign the vector */
	/* Will also flush the write out to device */
	return vp_ioread16(&vp_dev->common->msix_config);
}

static int vp_request_msix_vectors(struct virtio_device *vdev, int nvectors,
				   bool per_vq_vectors, struct irq_affinity *desc)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	const char *name = dev_name(&vp_dev->vdev.dev);
	unsigned flags = PCI_IRQ_MSIX;
	unsigned i, v;
	int err = -ENOMEM;

	vp_dev->msix_vectors = nvectors;

	vp_dev->msix_names = kmalloc(nvectors * sizeof *vp_dev->msix_names, GFP_KERNEL);
	
	vp_dev->msix_affinity_masks = kzalloc(nvectors * sizeof *vp_dev->msix_affinity_masks,
			  GFP_KERNEL);
	
	for (i = 0; i < nvectors; ++i)
		if (!alloc_cpumask_var(&vp_dev->msix_affinity_masks[i],
					GFP_KERNEL))
			goto error;

	if (desc) {
		flags |= PCI_IRQ_AFFINITY;
		desc->pre_vectors++; /* virtio config vector */
	}

	/* 分配irq vectors*/
	err = pci_alloc_irq_vectors_affinity(vp_dev->pci_dev, nvectors,
					     nvectors, flags, desc);

	vp_dev->msix_enabled = 1;

	/* Set the vector used for configuration */
	v = vp_dev->msix_used_vectors;
	snprintf(vp_dev->msix_names[v], sizeof *vp_dev->msix_names, "%s-config", name);
	/* 第一个中断是给config change用，每一个设备一个 */
	err = request_irq(pci_irq_vector(vp_dev->pci_dev, v),
			  vp_config_changed, 0, vp_dev->msix_names[v],
			  vp_dev);

	++vp_dev->msix_used_vectors;

	/*调用 vp_config_vector */
	v = vp_dev->config_vector(vp_dev, v);

	/* Verify we had enough resources to assign the vector */
	if (v == VIRTIO_MSI_NO_VECTOR) {
		err = -EBUSY;
		goto error;
	}

	/* 每个queue 一个irq，所以不走这里 */
	if (!per_vq_vectors) {
		/* Shared vector for all VQs */
		v = vp_dev->msix_used_vectors;
		snprintf(vp_dev->msix_names[v], sizeof *vp_dev->msix_names,
			 "%s-virtqueues", name);
		err = request_irq(pci_irq_vector(vp_dev->pci_dev, v),
				  vp_vring_interrupt, 0, vp_dev->msix_names[v],
				  vp_dev);
		if (err)
			goto error;
		++vp_dev->msix_used_vectors;
	}
	return 0;
error:
	return err;
}

static struct virtqueue *vp_setup_vq(struct virtio_device *vdev, unsigned index,
				     void (*callback)(struct virtqueue *vq),
				     const char *name,
				     bool ctx,
				     u16 msix_vec)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_vq_info *info = kmalloc(sizeof *info, GFP_KERNEL);
	struct virtqueue *vq;
	unsigned long flags;

	/* fill out our structure that represents an active queue */
	/* 调用 setup_vq */
	vq = vp_dev->setup_vq(vp_dev, info, index, callback, name, ctx, msix_vec);
	
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

out_info:
	kfree(info);
	return vq;
}


static int vp_find_vqs_msix(struct virtio_device *vdev, unsigned nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], bool per_vq_vectors,
		const bool *ctx,
		struct irq_affinity *desc)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u16 msix_vec;
	int i, err, nvectors, allocated_vectors;

	vp_dev->vqs = kcalloc(nvqs, sizeof(*vp_dev->vqs), GFP_KERNEL);

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

	err = vp_request_msix_vectors(vdev, nvectors, per_vq_vectors, per_vq_vectors ? desc : NULL);

	vp_dev->per_vq_vectors = per_vq_vectors;
	allocated_vectors = vp_dev->msix_used_vectors;

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		if (!callbacks[i])
			msix_vec = VIRTIO_MSI_NO_VECTOR;
		else if (vp_dev->per_vq_vectors)
			msix_vec = allocated_vectors++;
		else
			msix_vec = VP_MSIX_VQ_VECTOR;

		vqs[i] = vp_setup_vq(vdev, i, callbacks[i], names[i],
				     ctx ? ctx[i] : false, msix_vec);
	
		if (!vp_dev->per_vq_vectors || msix_vec == VIRTIO_MSI_NO_VECTOR)
			continue;

		/* allocate per-vq irq if available and necessary */
		snprintf(vp_dev->msix_names[msix_vec], sizeof *vp_dev->msix_names,
			 "%s-%s", dev_name(&vp_dev->vdev.dev), names[i]);
		/* 注册中断 */
		err = request_irq(pci_irq_vector(vp_dev->pci_dev, msix_vec), vring_interrupt, 0,
				  vp_dev->msix_names[msix_vec], vqs[i]);
	}
	return 0;
}


/* the config->find_vqs() implementation */
int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], const bool *ctx,
		struct irq_affinity *desc)
{
	int err;

	/* Try MSI-X with one vector per queue. */
	/* config 一个中断，per queu 一个中断 */
	err = vp_find_vqs_msix(vdev, nvqs, vqs, callbacks, names, true, ctx, desc);
	if (!err)
		return 0;

	/* Fallback: MSI-X with one vector for config, one shared for queues. */
	err = vp_find_vqs_msix(vdev, nvqs, vqs, callbacks, names, false, ctx, desc);
	if (!err)
		return 0;

	/* Finally fall back to regular interrupts. */
	return vp_find_vqs_intx(vdev, nvqs, vqs, callbacks, names, ctx);
}


static int vp_modern_find_vqs(struct virtio_device *vdev, unsigned nvqs,
			      struct virtqueue *vqs[],
			      vq_callback_t *callbacks[],
			      const char * const names[], const bool *ctx,
			      struct irq_affinity *desc)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtqueue *vq;
	int rc = vp_find_vqs(vdev, nvqs, vqs, callbacks, names, ctx, desc);

	/* Select and activate all queues. Has to be done last: once we do
	 * this, there's no way to go back except reset.
	 */
	list_for_each_entry(vq, &vdev->vqs, list) {
		vp_iowrite16(vq->index, &vp_dev->common->queue_select);
		vp_iowrite16(1, &vp_dev->common->queue_enable);
	}

	return 0;
}

static const struct virtio_config_ops virtio_pci_config_nodev_ops = {
	.get		= NULL,
	.set		= NULL,
	.generation	= vp_generation,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vqs	= vp_modern_find_vqs,
	.del_vqs	= vp_del_vqs,
	.get_features	= vp_get_features,
	.finalize_features = vp_finalize_features,
	.bus_name	= vp_bus_name,
	.set_vq_affinity = vp_set_vq_affinity,
};

static const struct virtio_config_ops virtio_pci_config_ops = {
	.get		= vp_get,
	.set		= vp_set,
	.generation	= vp_generation,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vqs	= vp_modern_find_vqs,
	.del_vqs	= vp_del_vqs,
	.get_features	= vp_get_features,
	.finalize_features = vp_finalize_features,
	.bus_name	= vp_bus_name,
	.set_vq_affinity = vp_set_vq_affinity,
	.get_vq_affinity = vp_get_vq_affinity,
};


/* the PCI probing function */
int virtio_pci_modern_probe(struct virtio_pci_device *vp_dev)
{
	struct pci_dev *pci_dev = vp_dev->pci_dev;
	int err, common, isr, notify, device;
	u32 notify_length;
	u32 notify_offset;

	check_offsets();

	/* We only own devices >= 0x1000 and <= 0x107f: leave the rest. */
	if (pci_dev->device < 0x1000 || pci_dev->device > 0x107f)
		return -ENODEV;

	if (pci_dev->device < 0x1040) {
		/* Transitional devices: use the PCI subsystem device id as
		 * virtio device id, same as legacy driver always did.
		 */
		vp_dev->vdev.id.device = pci_dev->subsystem_device;
	} else {
		/* Modern devices: simply use PCI device id, but start from 0x1040. */
		vp_dev->vdev.id.device = pci_dev->device - 0x1040;
	}
	vp_dev->vdev.id.vendor = pci_dev->subsystem_vendor;

	/*  
	 * virtio over pci通过各种pci capability进行配置，描述
	 * virtio 的每一个pci capability结构是一样的
	 * pci capability可以通过bar进行访问
	 */
	/* check for a common config: if not, use legacy mode (bar 0). */
	common = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_COMMON_CFG,
					    IORESOURCE_IO | IORESOURCE_MEM,
					    &vp_dev->modern_bars);

	/* If common is there, these should be too... */
	isr = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_ISR_CFG,
					 IORESOURCE_IO | IORESOURCE_MEM,
					 &vp_dev->modern_bars);

	notify = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_NOTIFY_CFG,
					    IORESOURCE_IO | IORESOURCE_MEM,
					    &vp_dev->modern_bars);


	err = dma_set_mask_and_coherent(&pci_dev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pci_dev->dev,
						DMA_BIT_MASK(32));
	if (err)
		dev_warn(&pci_dev->dev, 
			"Failed to enable 64-bit or 32-bit DMA.Trying to continue, but this might not work.\n");

	/* Device capability is only mandatory for devices that have
	 * device-specific configuration.
	 */
	/* 最终调用的是request_region或者request_mem_region */
	device = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_DEVICE_CFG,
					    IORESOURCE_IO | IORESOURCE_MEM,
					    &vp_dev->modern_bars);

	err = pci_request_selected_regions(pci_dev, vp_dev->modern_bars, "virtio-pci-modern");

	err = -EINVAL;
	/* 最终执行ioport_map，或者ioremap */
	vp_dev->common = map_capability(pci_dev, common, sizeof(struct virtio_pci_common_cfg), 4,
					0, sizeof(struct virtio_pci_common_cfg), NULL);

	vp_dev->isr = map_capability(pci_dev, isr, sizeof(u8), 1, 0, 1, NULL);


	/* Read notify_off_multiplier from config space. */
	pci_read_config_dword(pci_dev, notify + offsetof(struct virtio_pci_notify_cap, notify_off_multiplier), 
						&vp_dev->notify_offset_multiplier);

	/* Read notify length and offset from config space. */
	pci_read_config_dword(pci_dev, notify + offsetof(struct virtio_pci_notify_cap, cap.length), 
						&notify_length);

	pci_read_config_dword(pci_dev, notify + offsetof(struct virtio_pci_notify_cap, cap.offset), 
						&notify_offset);

	/* We don't know how many VQs we'll map, ahead of the time.
	 * If notify length is small, map it all now.
	 * Otherwise, map each VQ individually later.
	 */
	if ((u64)notify_length + (notify_offset % PAGE_SIZE) <= PAGE_SIZE) {
		vp_dev->notify_base = map_capability(pci_dev, notify, 2, 2, 0, notify_length, &vp_dev->notify_len);
	} else {
		vp_dev->notify_map_cap = notify;
	}

	/* Again, we don't know how much we should map, but PAGE_SIZE
	 * is more than enough for all existing devices.
	 */
	if (device) {
		vp_dev->device = map_capability(pci_dev, device, 0, 4, 0, PAGE_SIZE, &vp_dev->device_len);
		vp_dev->vdev.config = &virtio_pci_config_ops;
	} else {
		vp_dev->vdev.config = &virtio_pci_config_nodev_ops;
	}

	vp_dev->config_vector = vp_config_vector;
	vp_dev->setup_vq = setup_vq;
	vp_dev->del_vq = del_vq;

	return 0;
}


static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_BLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features_legacy[] = {
	VIRTIO_BLK_F_SEG_MAX, VIRTIO_BLK_F_SIZE_MAX, VIRTIO_BLK_F_GEOMETRY,
	VIRTIO_BLK_F_RO, VIRTIO_BLK_F_BLK_SIZE, VIRTIO_BLK_F_SCSI,
	VIRTIO_BLK_F_WCE, VIRTIO_BLK_F_TOPOLOGY, VIRTIO_BLK_F_CONFIG_WCE,
	VIRTIO_BLK_F_MQ,
}
;
static unsigned int features[] = {
	VIRTIO_BLK_F_SEG_MAX, VIRTIO_BLK_F_SIZE_MAX, VIRTIO_BLK_F_GEOMETRY,
	VIRTIO_BLK_F_RO, VIRTIO_BLK_F_BLK_SIZE,
	VIRTIO_BLK_F_WCE, VIRTIO_BLK_F_TOPOLOGY, VIRTIO_BLK_F_CONFIG_WCE,
	VIRTIO_BLK_F_MQ,
};

/* driver驱动 */
static struct virtio_driver virtio_blk = {
	.feature_table			= features,
	.feature_table_size		= ARRAY_SIZE(features),
	.feature_table_legacy		= features_legacy,
	.feature_table_size_legacy	= ARRAY_SIZE(features_legacy),
	.driver.name			= KBUILD_MODNAME,
	.driver.owner			= THIS_MODULE,
	.id_table			= id_table,
	.probe				= virtblk_probe,
	.remove				= virtblk_remove,
	.config_changed			= virtblk_config_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze				= virtblk_freeze,
	.restore			= virtblk_restore,
#endif
};


static int __init init(void)
{
	int error;

	virtblk_wq = alloc_workqueue("virtio-blk", 0, 0);

	major = register_blkdev(0, "virtblk");

	error = register_virtio_driver(&virtio_blk);
	return 0;
}

module_init(init);


static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_BLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};


static int virtblk_probe(struct virtio_device *vdev)
{
	struct virtio_blk *vblk;
	struct request_queue *q;
	int err, index;

	u64 cap;
	u32 v, blk_size, sg_elems, opt_io_size;
	u16 min_io_size;
	u8 physical_block_exp, alignment_offset;

	err = ida_simple_get(&vd_index_ida, 0, minor_to_index(1 << MINORBITS), GFP_KERNEL);
	index = err;

	/* 检查有么有对应的feature，然后从virtio pci的device cap中读出来 */
	/* We need to know how many segments before we allocate. */
	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_SEG_MAX, struct virtio_blk_config, seg_max, &sg_elems);

	/* We need at least one SG element, whatever they say. */
	if (err || !sg_elems)
		sg_elems = 1;

	/* We need an extra sg elements at head and tail. */
	sg_elems += 2;
	vdev->priv = vblk = kmalloc(sizeof(*vblk), GFP_KERNEL);

	vblk->vdev = vdev;
	vblk->sg_elems = sg_elems;

	INIT_WORK(&vblk->config_work, virtblk_config_changed_work);

	err = init_vq(vblk);

	/* FIXME: How many partitions?  How long is a piece of string? */
	vblk->disk = alloc_disk(1 << PART_BITS);

	/* Default queue sizing is to fill the ring. */
	if (!virtblk_queue_depth) {
		virtblk_queue_depth = vblk->vqs[0].vq->num_free;
		/* ... but without indirect descs, we use 2 descs per req */
		if (!virtio_has_feature(vdev, VIRTIO_RING_F_INDIRECT_DESC))
			virtblk_queue_depth /= 2;
	}

	memset(&vblk->tag_set, 0, sizeof(vblk->tag_set));
	vblk->tag_set.ops = &virtio_mq_ops;
	vblk->tag_set.queue_depth = virtblk_queue_depth;
	vblk->tag_set.numa_node = NUMA_NO_NODE;
	vblk->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	vblk->tag_set.cmd_size = sizeof(struct virtblk_req) + sizeof(struct scatterlist) * sg_elems;
	vblk->tag_set.driver_data = vblk;
	vblk->tag_set.nr_hw_queues = vblk->num_vqs;

	err = blk_mq_alloc_tag_set(&vblk->tag_set);

	q = blk_mq_init_queue(&vblk->tag_set);
	vblk->disk->queue = q;

	q->queuedata = vblk;

	virtblk_name_format("vd", index, vblk->disk->disk_name, DISK_NAME_LEN);

	vblk->disk->major = major;
	vblk->disk->first_minor = index_to_minor(index);
	vblk->disk->private_data = vblk;
	vblk->disk->fops = &virtblk_fops;
	vblk->disk->flags |= GENHD_FL_EXT_DEVT;
	vblk->index = index;

	/* configure queue flush support */
	virtblk_update_cache_mode(vdev);

	/* If disk is read-only in the host, the guest should obey */
	if (virtio_has_feature(vdev, VIRTIO_BLK_F_RO))
		set_disk_ro(vblk->disk, 1);

	/* Host must always specify the capacity. */
	virtio_cread(vdev, struct virtio_blk_config, capacity, &cap);

	/* If capacity is too big, truncate with warning. */
	if ((sector_t)cap != cap) {
		dev_warn(&vdev->dev, "Capacity %llu too large: truncating\n",
			 (unsigned long long)cap);
		cap = (sector_t)-1;
	}
	set_capacity(vblk->disk, cap);

	/* We can handle whatever the host told us to handle. */
	blk_queue_max_segments(q, vblk->sg_elems-2);

	/* No real sector limit. */
	blk_queue_max_hw_sectors(q, -1U);

	/* Host can optionally specify maximum segment size and number of
	 * segments. */
	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_SIZE_MAX,
				   struct virtio_blk_config, size_max, &v);
	if (!err)
		blk_queue_max_segment_size(q, v);
	else
		blk_queue_max_segment_size(q, -1U);

	/* Host can optionally specify the block size of the device */
	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_BLK_SIZE,
				   struct virtio_blk_config, blk_size,
				   &blk_size);
	if (!err)
		blk_queue_logical_block_size(q, blk_size);
	else
		blk_size = queue_logical_block_size(q);

	/* Use topology information if available */
	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_TOPOLOGY,
				   struct virtio_blk_config, physical_block_exp,
				   &physical_block_exp);
	if (!err && physical_block_exp)
		blk_queue_physical_block_size(q,
				blk_size * (1 << physical_block_exp));

	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_TOPOLOGY,
				   struct virtio_blk_config, alignment_offset,
				   &alignment_offset);
	if (!err && alignment_offset)
		blk_queue_alignment_offset(q, blk_size * alignment_offset);

	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_TOPOLOGY,
				   struct virtio_blk_config, min_io_size,
				   &min_io_size);
	if (!err && min_io_size)
		blk_queue_io_min(q, blk_size * min_io_size);

	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_TOPOLOGY,
				   struct virtio_blk_config, opt_io_size,
				   &opt_io_size);
	if (!err && opt_io_size)
		blk_queue_io_opt(q, blk_size * opt_io_size);

	virtio_device_ready(vdev);

	device_add_disk(&vdev->dev, vblk->disk);
	err = device_create_file(disk_to_dev(vblk->disk), &dev_attr_serial);

	if (virtio_has_feature(vdev, VIRTIO_BLK_F_CONFIG_WCE))
		err = device_create_file(disk_to_dev(vblk->disk),
					 &dev_attr_cache_type_rw);
	else
		err = device_create_file(disk_to_dev(vblk->disk),
					 &dev_attr_cache_type_ro);
	return 0;
}

static inline
int virtio_find_vqs(struct virtio_device *vdev, unsigned nvqs,
			struct virtqueue *vqs[], vq_callback_t *callbacks[],
			const char * const names[],
			struct irq_affinity *desc)
{
	/* 调用 vp_modern_find_vqs */
	return vdev->config->find_vqs(vdev, nvqs, vqs, callbacks, names, NULL, desc);
}

struct virtio_blk_config {
	/* The capacity (in 512-byte sectors). */
	__u64 capacity;
	/* The maximum segment size (if VIRTIO_BLK_F_SIZE_MAX) */
	__u32 size_max;
	/* The maximum number of segments (if VIRTIO_BLK_F_SEG_MAX) */
	__u32 seg_max;
	/* geometry of the device (if VIRTIO_BLK_F_GEOMETRY) */
	struct virtio_blk_geometry {
		__u16 cylinders;
		__u8 heads;
		__u8 sectors;
	} geometry;

	/* block size of device (if VIRTIO_BLK_F_BLK_SIZE) */
	__u32 blk_size;

	/* the next 4 entries are guarded by VIRTIO_BLK_F_TOPOLOGY  */
	/* exponent for physical block per logical block. */
	__u8 physical_block_exp;
	/* alignment offset in logical blocks. */
	__u8 alignment_offset;
	/* minimum I/O size without performance penalty in logical blocks. */
	__u16 min_io_size;
	/* optimal sustained I/O size in logical blocks. */
	__u32 opt_io_size;

	/* writeback mode (if VIRTIO_BLK_F_CONFIG_WCE) */
	__u8 wce;
	__u8 unused;

	/* number of vqs, only available when VIRTIO_BLK_F_MQ is set */
	__u16 num_queues;
} __attribute__((packed));

static void virtblk_done(struct virtqueue *vq)
{
	struct virtio_blk *vblk = vq->vdev->priv;
	bool req_done = false;
	int qid = vq->index;
	struct virtblk_req *vbr;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&vblk->vqs[qid].lock, flags);
	do {
		virtqueue_disable_cb(vq);
		while ((vbr = virtqueue_get_buf(vblk->vqs[qid].vq, &len)) != NULL) {
			blk_mq_complete_request(vbr->req, vbr->req->errors);
			req_done = true;
		}
		if (unlikely(virtqueue_is_broken(vq)))
			break;
	} while (!virtqueue_enable_cb(vq));

	/* In case queue is stopped waiting for more buffers. */
	if (req_done)
		blk_mq_start_stopped_hw_queues(vblk->disk->queue, true);
	spin_unlock_irqrestore(&vblk->vqs[qid].lock, flags);
}


static int init_vq(struct virtio_blk *vblk)
{
	int err;
	int i;
	vq_callback_t **callbacks;
	const char **names;
	struct virtqueue **vqs;
	unsigned short num_vqs;
	struct virtio_device *vdev = vblk->vdev;
	struct irq_affinity desc = { 0, };

	/* 获取有多少个virtio queue */
	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_MQ, struct virtio_blk_config, num_queues, &num_vqs);
	if (err)
		num_vqs = 1;

	vblk->vqs = kmalloc_array(num_vqs, sizeof(*vblk->vqs), GFP_KERNEL);

	names = kmalloc_array(num_vqs, sizeof(*names), GFP_KERNEL);
	callbacks = kmalloc_array(num_vqs, sizeof(*callbacks), GFP_KERNEL);
	vqs = kmalloc_array(num_vqs, sizeof(*vqs), GFP_KERNEL);

	for (i = 0; i < num_vqs; i++) {
		callbacks[i] = virtblk_done;
		snprintf(vblk->vqs[i].name, VQ_NAME_LEN, "req.%d", i);
		names[i] = vblk->vqs[i].name;
	}

	/* Discover virtqueues and write information to configuration.  */
	err = virtio_find_vqs(vdev, num_vqs, vqs, callbacks, names, &desc);

	for (i = 0; i < num_vqs; i++) {
		spin_lock_init(&vblk->vqs[i].lock);
		vblk->vqs[i].vq = vqs[i];
	}
	vblk->num_vqs = num_vqs;
}

static const struct blk_mq_ops virtio_mq_ops = {
	.queue_rq	= virtio_queue_rq,
	.complete	= virtblk_request_done,
	.init_request	= virtblk_init_request,
#ifdef CONFIG_VIRTIO_BLK_SCSI
	.initialize_rq_fn = virtblk_initialize_rq,
#endif
	.map_queues	= virtblk_map_queues,
};



