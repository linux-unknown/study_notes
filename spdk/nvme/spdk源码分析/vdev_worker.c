
/* 响应虚拟机的请求 */
static int vdev_worker(void *arg)
{
	struct spdk_vhost_blk_dev *bvdev = arg;
	uint16_t q_idx;

	for (q_idx = 0; q_idx < bvdev->vdev.max_queues; q_idx++) {
		process_vq(bvdev, &bvdev->vdev.virtqueue[q_idx]);
	}

	spdk_vhost_dev_used_signal(&bvdev->vdev);

	return -1;
}

/* VirtIO ring descriptors: 16 bytes.
 * These can chain together via "next". */
struct vring_desc {
	uint64_t addr;  /*  Address (guest-physical). */
	uint32_t len;   /* Length. */
	uint16_t flags; /* The flags as indicated above. */
	uint16_t next;  /* We chain unused descriptors via this. */
};

struct vring_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[0];
};

/* id is a 16bit index. uint32_t is used here for ids for padding reasons. */
struct vring_used_elem {
	/* Index of start of used descriptor chain. */
	uint32_t id;
	/* Total length of the descriptor chain which was written to. */
	uint32_t len;
};

struct vring_used {
	uint16_t flags;
	volatile uint16_t idx;
	struct vring_used_elem ring[0];
};

struct vring {
	unsigned int num;
	struct vring_desc  *desc;
	struct vring_avail *avail;
	struct vring_used  *used;
};


struct rte_vhost_vring {
	struct vring_desc	*desc;
	struct vring_avail	*avail;
	struct vring_used	*used;
	uint64_t		log_guest_addr;

	int			callfd;
	int			kickfd;
	uint16_t		size;

	uint16_t		last_avail_idx;
	uint16_t		last_used_idx;
};

struct spdk_vhost_blk_task {
	struct spdk_bdev_io *bdev_io;
	struct spdk_vhost_blk_dev *bvdev;
	struct spdk_vhost_virtqueue *vq;

	volatile uint8_t *status;

	uint16_t req_idx;

	/* If set, the task is currently used for I/O processing. */
	bool used;

	/** Number of bytes that were written. */
	uint32_t used_len;
	uint16_t iovcnt;
	void *aligned_vector;
	struct iovec iovs[SPDK_VHOST_IOVS_MAX];
};

struct spdk_vhost_virtqueue {
	struct rte_vhost_vring vring;
	void *tasks;

	/* Request count from last stats check */
	uint32_t req_cnt;

	/* Request count from last event */
	uint16_t used_req_cnt;

	/* How long interrupt is delayed */
	uint32_t irq_delay_time;

	/* Next time when we need to send event */
	uint64_t next_event_time;

	uint64_t 					counter;
	struct virtq_inflight 		*inflight;
	struct virtq_inflight_desc	*resubmit_list;
	uint16_t 					resubmit_num;
	/* Is support VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD */
	uint16_t		is_inflight;

} __attribute((aligned(SPDK_CACHE_LINE_SIZE)));

/*
 * This comes first in the read scatter-gather list.
 * For legacy virtio, if VIRTIO_F_ANY_LAYOUT is not negotiated,
 * this is the first element of the read scatter-gather list.
 */
struct virtio_blk_outhdr {
	/* VIRTIO_BLK_T* */
	__virtio32 type;
	/* io priority. */
	__virtio32 ioprio;
	/* Sector (ie. 512 byte offset) */
	__virtio64 sector;
};

struct spdk_bdev_io {
	/** The block device that this I/O belongs to. */
	struct spdk_bdev *bdev;

	/** The bdev I/O channel that this was handled on. */
	struct spdk_bdev_channel *ch;

	/** The bdev I/O channel that this was submitted on. */
	struct spdk_bdev_channel *io_submit_ch;

	/** User function that will be called when this completes */
	spdk_bdev_io_completion_cb cb;

	/** Context that will be passed to the completion callback */
	void *caller_ctx;

	/** Current tsc at submit time. Used to calculate latency at completion. */
	uint64_t submit_tsc;

	/**
	 * Set to true while the bdev module submit_request function is in progress.
	 *
	 * This is used to decide whether spdk_bdev_io_complete() can complete the I/O directly
	 * or if completion must be deferred via an event.
	 */
	bool in_submit_request;

	/** Status for the IO */
	int8_t status;

	/** Error information from a device */
	union {
		/** Only valid when status is SPDK_BDEV_IO_STATUS_NVME_ERROR */
		struct {
			/** NVMe status code type */
			uint8_t sct;
			/** NVMe status code */
			uint8_t sc;
		} nvme;
		/** Only valid when status is SPDK_BDEV_IO_STATUS_SCSI_ERROR */
		struct {
			/** SCSI status code */
			uint8_t sc;
			/** SCSI sense key */
			uint8_t sk;
			/** SCSI additional sense code */
			uint8_t asc;
			/** SCSI additional sense code qualifier */
			uint8_t ascq;
		} scsi;
	} error;

	/** Enumerated value representing the I/O type. */
	uint8_t type;

	union {
		struct {
			/** For basic IO case, use our own iovec element. */
			struct iovec iov;

			/** For SG buffer cases, array of iovecs to transfer. */
			struct iovec *iovs;

			/** For SG buffer cases, number of iovecs in iovec array. */
			int iovcnt;

			/** Total size of data to be transferred. */
			uint64_t num_blocks;

			/** Starting offset (in blocks) of the bdev for this I/O. */
			uint64_t offset_blocks;

			/** stored user callback in case we split the I/O and use a temporary callback */
			spdk_bdev_io_completion_cb stored_user_cb;

			/** number of blocks remaining in a split i/o */
			uint64_t split_remaining_num_blocks;

			/** current offset of the split I/O in the bdev */
			uint64_t split_current_offset_blocks;
		} bdev;
		struct {
			/** Channel reference held while messages for this reset are in progress. */
			struct spdk_io_channel *ch_ref;
		} reset;
		struct {
			/* The NVMe command to execute */
			struct spdk_nvme_cmd cmd;

			/* The data buffer to transfer */
			void *buf;

			/* The number of bytes to transfer */
			size_t nbytes;

			/* The meta data buffer to transfer */
			void *md_buf;

			/* meta data buffer size to transfer */
			size_t md_len;
		} nvme_passthru;
	} u;

	/** bdev allocated memory associated with this request */
	void *buf;

	/** requested size of the buffer associated with this I/O */
	uint64_t buf_len;

	/** Callback for when buf is allocated */
	spdk_bdev_io_get_buf_cb get_buf_cb;

	/** Entry to the list need_buf of struct spdk_bdev. */
	STAILQ_ENTRY(spdk_bdev_io) buf_link;

	/** Member used for linking child I/Os together. */
	TAILQ_ENTRY(spdk_bdev_io) link;

	/** It may be used by modules to put the bdev_io into its own list. */
	TAILQ_ENTRY(spdk_bdev_io) module_link;

	/**
	 * Per I/O context for use by the bdev module.
	 */
	uint8_t driver_ctx[0];

	/* No members may be added after driver_ctx! */
};

struct spdk_nvme_cmd {
	/* dword 0 */
	uint16_t opc	:  8;	/* opcode */
	uint16_t fuse	:  2;	/* fused operation */
	uint16_t rsvd1	:  4;
	uint16_t psdt	:  2;
	uint16_t cid;		/* command identifier */

	/* dword 1 */
	uint32_t nsid;		/* namespace identifier */

	/* dword 2-3 */
	uint32_t rsvd2;
	uint32_t rsvd3;

	/* dword 4-5 */
	uint64_t mptr;		/* metadata pointer */

	/* dword 6-9: data pointer */
	union {
		struct {
			uint64_t prp1;		/* prp entry 1 */
			uint64_t prp2;		/* prp entry 2 */
		} prp;

		struct spdk_nvme_sgl_descriptor sgl1;
	} dptr;

	/* dword 10-15 */
	uint32_t cdw10;		/* command-specific */
	uint32_t cdw11;		/* command-specific */
	uint32_t cdw12;		/* command-specific */
	uint32_t cdw13;		/* command-specific */
	uint32_t cdw14;		/* command-specific */
	uint32_t cdw15;		/* command-specific */
};


#define SPDK_COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

static void
process_vq(struct spdk_vhost_blk_dev *bvdev, struct spdk_vhost_virtqueue *vq)
{
	struct spdk_vhost_blk_task *task;
	int rc;
	uint16_t reqs[32];
	uint16_t reqs_cnt, i;

	reqs_cnt = spdk_vhost_vq_avail_ring_get(vq, reqs, SPDK_COUNTOF(reqs));
	if (!reqs_cnt) {
		return;
	}

	for (i = 0; i < reqs_cnt; i++) {
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "====== Starting processing request idx %"PRIu16"======\n",
			      reqs[i]);

		if (spdk_unlikely(reqs[i] >= vq->vring.size)) {
			SPDK_ERRLOG("%s: request idx '%"PRIu16"' exceeds virtqueue size (%"PRIu16").\n",
				    bvdev->vdev.name, reqs[i], vq->vring.size);
			spdk_vhost_vq_used_ring_enqueue(&bvdev->vdev, vq, reqs[i], 0);
			continue;
		}

		task = &((struct spdk_vhost_blk_task *)vq->tasks)[reqs[i]];
		if (spdk_unlikely(task->used)) {
			SPDK_ERRLOG("%s: request with idx '%"PRIu16"' is already pending.\n",
				    bvdev->vdev.name, reqs[i]);
			spdk_vhost_vq_used_ring_enqueue(&bvdev->vdev, vq, reqs[i], 0);
			continue;
		}

		bvdev->vdev.task_cnt++;

		task->used = true;
		task->iovcnt = SPDK_COUNTOF(task->iovs);
		task->status = NULL;
		task->used_len = 0;
		task->aligned_vector = NULL;

		rc = process_blk_request(task, bvdev, vq);
		if (rc == 0) {
			SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "====== Task %p req_idx %d submitted ======\n", task,
				      reqs[i]);
		} else {
			SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "====== Task %p req_idx %d failed ======\n", task, reqs[i]);
		}
	}
}


/*
 * Get available requests from avail ring.
 */
uint16_t
spdk_vhost_vq_avail_ring_get(struct spdk_vhost_virtqueue *virtqueue, uint16_t *reqs,
			     uint16_t reqs_len)
{
	struct rte_vhost_vring *vring = &virtqueue->vring;
	struct vring_avail *avail = vring->avail;
	uint16_t size_mask = vring->size - 1;
	uint16_t last_idx = vring->last_avail_idx, avail_idx = avail->idx;
	uint16_t count, i;

	if (spdk_unlikely(virtqueue->resubmit_list && virtqueue->resubmit_num > 0)) {
		int resubmit_count = spdk_vhost_vq_resubmit_desc_get(virtqueue, reqs, reqs_len);
		if (resubmit_count > 0) {
			return resubmit_count;
		}
	}

	/* 表示有数据 */
	count = avail_idx - last_idx;
	if (spdk_likely(count == 0)) {
		return 0;
	}

	if (spdk_unlikely(count > vring->size)) {
		/* TODO: the queue is unrecoverably broken and should be marked so.
		 * For now we will fail silently and report there are no new avail entries.
		 */
		return 0;
	}

	count = spdk_min(count, reqs_len);
	/* vring->last_avail_idx增加 */
	vring->last_avail_idx += count;
	for (i = 0; i < count; i++) {
		reqs[i] = vring->avail->ring[(last_idx + i) & size_mask];
		spdk_vhost_inflight_get(virtqueue, reqs[i]);
	}

	SPDK_DEBUGLOG(SPDK_LOG_VHOST_RING,
		      "AVAIL: last_idx=%"PRIu16" avail_idx=%"PRIu16" count=%"PRIu16"\n",
		      last_idx, avail_idx, count);

	return count;
}

static int
process_blk_request(struct spdk_vhost_blk_task *task, struct spdk_vhost_blk_dev *bvdev,
		    struct spdk_vhost_virtqueue *vq)
{
	const struct virtio_blk_outhdr *req;
	struct iovec *iov;
	struct iovec *data_iov = NULL;
	int iov_cnt;
	uint32_t type;
	uint32_t payload_len;
	int rc;

	/* task->req_idx会在alloc_task_pool赋值 */
	if (blk_iovs_setup(&bvdev->vdev, vq, task->req_idx, task->iovs, &task->iovcnt, &payload_len)) {
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Invalid request (req_idx = %"PRIu16").\n", task->req_idx);
		/* Only READ and WRITE are supported for now. */
		invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
		return -1;
	}

	/* task->iovs[0]应该是struct virtio_blk_outhdr *req */
	iov = &task->iovs[0];
	if (spdk_unlikely(iov->iov_len != sizeof(*req))) {
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK,
			      "First descriptor size is %zu but expected %zu (req_idx = %"PRIu16").\n",
			      iov->iov_len, sizeof(*req), task->req_idx);
		invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
		return -1;
	}

	req = iov->iov_base;
	/* 倒数第二个iovs的长度应该为1 */
	iov = &task->iovs[task->iovcnt - 1];
	if (spdk_unlikely(iov->iov_len != 1)) {
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK,
			      "Last descriptor size is %zu but expected %d (req_idx = %"PRIu16").\n",
			      iov->iov_len, 1, task->req_idx);
		invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
		return -1;
	}

	task->status = iov->iov_base;
	/* payload是不包含req和status的 */
	payload_len -= sizeof(*req) + sizeof(*task->status);
	task->iovcnt -= 2;

	type = req->type;
#ifdef VIRTIO_BLK_T_BARRIER
	/* Don't care about barier for now (as QEMU's virtio-blk do). */
	type &= ~VIRTIO_BLK_T_BARRIER;
#endif

	switch (type) {
	case VIRTIO_BLK_T_IN:
	case VIRTIO_BLK_T_OUT:
		if (is_vector_aligned(&task->iovs[1], task->iovcnt)) {
			data_iov = &task->iovs[1];
			iov_cnt = task->iovcnt;
		} else {
			struct aligned_vector *aligned_iov;

			if (get_vector_size(&task->iovs[1], task->iovcnt) != payload_len) {
				SPDK_ERRLOG("task io vector size mismatch.\n");
				invalid_blk_request(task, VIRTIO_BLK_S_IOERR);
				return -1;
			}

			aligned_iov = malloc_aligned_vector(payload_len);
			if (aligned_iov == NULL) {
				SPDK_ERRLOG("malloc new aligned vector failed.\n");
				invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
				return -1;
			}

			task->aligned_vector = aligned_iov;
			data_iov = aligned_iov->iov;
			iov_cnt = aligned_iov->iov_cnt;

			/* write operation, copy data before passthrough */
			if (type == VIRTIO_BLK_T_OUT) {
				copy_vector(data_iov, &task->iovs[1], payload_len);
			}
		}

		if (type == VIRTIO_BLK_T_IN) {
			task->used_len = payload_len + sizeof(*task->status);
			rc = spdk_bdev_readv(bvdev->bdev_desc, bvdev->bdev_io_channel,
					     data_iov, iov_cnt, req->sector * 512,
					     payload_len, blk_request_complete_cb, task);
		} else if (!bvdev->readonly) {
			task->used_len = sizeof(*task->status);
			rc = spdk_bdev_writev(bvdev->bdev_desc, bvdev->bdev_io_channel,
					      data_iov, iov_cnt, req->sector * 512,
					      payload_len, blk_request_complete_cb, task);
		} else {
			SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Device is in read-only mode!\n");
			rc = -1;
		}

		if (rc) {
			invalid_blk_request(task, VIRTIO_BLK_S_IOERR);
			return -1;
		}
		break;
	case VIRTIO_BLK_T_GET_ID:
		if (!task->iovcnt || !payload_len) {
			invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
			return -1;
		}
		task->used_len = spdk_min((size_t)VIRTIO_BLK_ID_BYTES, task->iovs[1].iov_len);
		spdk_strcpy_pad(task->iovs[1].iov_base, spdk_bdev_get_product_name(bvdev->bdev),
				task->used_len, ' ');
		blk_request_finish(true, task);
		break;
	default:
		SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Not supported request type '%"PRIu32"'.\n", type);
		invalid_blk_request(task, VIRTIO_BLK_S_UNSUPP);
		return -1;
	}

	return 0;
}


static void
blk_request_complete_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
	struct spdk_vhost_blk_task *task = cb_arg;
	struct aligned_vector *aligned_iov = task->aligned_vector;

	if (aligned_iov) {
		struct iovec *iov;
		struct virtio_blk_outhdr *req;

		iov = &task->iovs[0];
		req = iov->iov_base;

		/* read operation, copy data back to src vector buffer */
		if (req->type == VIRTIO_BLK_T_IN) {
			copy_vector(&task->iovs[1], aligned_iov->iov, aligned_iov->size);
		}

		free_vector(task->aligned_vector);
		task->aligned_vector = NULL;
	}

	spdk_bdev_free_io(bdev_io);
	blk_request_finish(success, task);
}
		
/*
 * Process task's descriptor chain and setup data related fields.
 * Return
 *   total size of suplied buffers
 *
 *   FIXME: Make this function return to rd_cnt and wr_cnt
 */
static int
blk_iovs_setup(struct spdk_vhost_dev *vdev, struct spdk_vhost_virtqueue *vq, uint16_t req_idx,
	       struct iovec *iovs, uint16_t *iovs_cnt, uint32_t *length)
{
	struct vring_desc *desc, *desc_table;
	uint16_t out_cnt = 0, cnt = 0;
	uint32_t desc_table_size, len = 0;
	int rc;

	rc = spdk_vhost_vq_get_desc(vdev, vq, req_idx, &desc, &desc_table, &desc_table_size);


	while (1) {
		if (spdk_unlikely(spdk_vhost_vring_desc_to_iov(vdev, iovs, &cnt, desc))) {
			SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Invalid descriptor %" PRIu16" (req_idx = %"PRIu16").\n",
				      req_idx, cnt);
			return -1;
		}

		len += desc->len;

		/* 如果desc为写则返回1 */
		out_cnt += spdk_vhost_vring_desc_is_wr(desc);

		rc = spdk_vhost_vring_desc_get_next(&desc, desc_table, desc_table_size);
		if (rc != 0) {
			SPDK_ERRLOG("%s: Descriptor chain at index %"PRIu16" terminated unexpectedly.\n", vdev->name, req_idx);
			return -1;
		} else if (desc == NULL) {/* desc为NULL表示desc chain已经到了末尾了 */
			break;
		}
	}

	/*
	 * There must be least two descriptors.
	 * First contain request so it must be readable.
	 * Last descriptor contain buffer for response so it must be writable.
	 */
	if (spdk_unlikely(out_cnt == 0 || cnt < 2)) {
		return -1;
	}

	*length = len;
	*iovs_cnt = cnt;
	return 0;
}

int
spdk_vhost_vq_get_desc(struct spdk_vhost_dev *vdev, struct spdk_vhost_virtqueue *virtqueue,
		       uint16_t req_idx, struct vring_desc **desc, struct vring_desc **desc_table,
		       uint32_t *desc_table_size)
{
	*desc = &virtqueue->vring.desc[req_idx];

	/* 不是indirect */
	if (spdk_vhost_vring_desc_is_indirect(*desc)) {
		*desc_table_size = (*desc)->len / sizeof(**desc);
		*desc_table = spdk_vhost_gpa_to_vva(vdev, (*desc)->addr,
						    sizeof(**desc) * *desc_table_size);
		*desc = *desc_table;
		if (*desc == NULL) {
			return -1;
		}

		return 0;
	}

	*desc_table = virtqueue->vring.desc;
	*desc_table_size = virtqueue->vring.size;

	return 0;
}

static bool
spdk_vhost_vring_desc_is_indirect(struct vring_desc *cur_desc)
{
	return !!(cur_desc->flags & VRING_DESC_F_INDIRECT);
}

#define _2MB_OFFSET(ptr)	((ptr) & (0x200000 - 1)

int
spdk_vhost_vring_desc_to_iov(struct spdk_vhost_dev *vdev, struct iovec *iov,
			     uint16_t *iov_index, const struct vring_desc *desc)
{
	uint32_t remaining = desc->len;
	uint32_t to_boundary;
	uint32_t len;
	uintptr_t payload = desc->addr;
	uintptr_t vva;

	while (remaining) {
		/* 根据desc->addr找到在spdk中对应的映射地址 */
		vva = (uintptr_t)rte_vhost_gpa_to_vva(vdev->mem, payload);

		to_boundary = 0x200000 - _2MB_OFFSET(payload);
		if (spdk_likely(remaining <= to_boundary)) {
			len = remaining;
		} else {
			/*
			 * Descriptor crosses a 2MB hugepage boundary.  vhost memory regions are allocated
			 *  from hugepage memory, so this means this descriptor may be described by
			 *  discontiguous vhost memory regions.  Do not blindly split on the 2MB boundary,
			 *  only split it if the two sides of the boundary do not map to the same vhost
			 *  memory region.  This helps ensure we do not exceed the max number of IOVs
			 *  defined by SPDK_VHOST_IOVS_MAX.
			 */
			len = to_boundary;
			while (len < remaining) {
				if (vva + len != (uintptr_t)rte_vhost_gpa_to_vva(vdev->mem, payload + len)) {
					break;
				}
				len += spdk_min(remaining - len, 0x200000);
			}
		}
		/* 设置iov的base和len */
		iov[*iov_index].iov_base = (void *)vva;
		iov[*iov_index].iov_len = len;
		remaining -= len;
		payload += len;
		(*iov_index)++;
	}

	return 0;
}

/**
 * Convert guest physical address to host virtual address
 *
 * @param mem
 *  the guest memory regions
 * @param gpa
 *  the guest physical address for querying
 * @return
 *  the host virtual address on success, 0 on failure
 */
static inline uint64_t __attribute__((always_inline))
rte_vhost_gpa_to_vva(struct rte_vhost_memory *mem, uint64_t gpa)
{
	struct rte_vhost_mem_region *reg;
	uint32_t i;

	for (i = 0; i < mem->nregions; i++) {
		reg = &mem->regions[i];
		if (gpa >= reg->guest_phys_addr &&
		    gpa <  reg->guest_phys_addr + reg->size) {
			return gpa - reg->guest_phys_addr +
			       reg->host_user_addr;
		}
	}

	return 0;
}

bool
spdk_vhost_vring_desc_is_wr(struct vring_desc *cur_desc)
{
	return !!(cur_desc->flags & VRING_DESC_F_WRITE);
}

int
spdk_vhost_vring_desc_get_next(struct vring_desc **desc,
			       struct vring_desc *desc_table, uint32_t desc_table_size)
{
	struct vring_desc *old_desc = *desc;
	uint16_t next_idx;

	/* VRING_DESC_F_NEXT表示desc为chain的 */
	if ((old_desc->flags & VRING_DESC_F_NEXT) == 0) {
		*desc = NULL;
		return 0;
	}

	next_idx = old_desc->next;
	if (spdk_unlikely(next_idx >= desc_table_size)) {
		*desc = NULL;
		return -1;
	}

	*desc = &desc_table[next_idx];
	return 0;
}

int
spdk_bdev_writev(struct spdk_bdev_desc *desc, struct spdk_io_channel *ch,
		 struct iovec *iov, int iovcnt,
		 uint64_t offset, uint64_t len,
		 spdk_bdev_io_completion_cb cb, void *cb_arg)
{
	uint64_t offset_blocks, num_blocks;

	if (spdk_bdev_bytes_to_blocks(desc->bdev, offset, &offset_blocks, len, &num_blocks) != 0) {
		return -EINVAL;
	}

	return spdk_bdev_writev_blocks(desc, ch, iov, iovcnt, offset_blocks, num_blocks, cb, cb_arg);
}

/*
 * Convert I/O offset and length from bytes to blocks.
 *
 * Returns zero on success or non-zero if the byte parameters aren't divisible by the block size.
 */
static uint64_t
spdk_bdev_bytes_to_blocks(struct spdk_bdev *bdev, uint64_t offset_bytes, uint64_t *offset_blocks,
			  uint64_t num_bytes, uint64_t *num_blocks)
{
	uint32_t block_size = bdev->blocklen;

	*offset_blocks = offset_bytes / block_size;
	*num_blocks = num_bytes / block_size;

	return (offset_bytes % block_size) | (num_bytes % block_size);
}

int
spdk_bdev_writev_blocks(struct spdk_bdev_desc *desc, struct spdk_io_channel *ch,
			struct iovec *iov, int iovcnt,
			uint64_t offset_blocks, uint64_t num_blocks,
			spdk_bdev_io_completion_cb cb, void *cb_arg)
{
	struct spdk_bdev *bdev = desc->bdev;
	struct spdk_bdev_io *bdev_io;
	struct spdk_bdev_channel *channel = spdk_io_channel_get_ctx(ch);
	/* 获取bdev_io内存 */
	bdev_io = spdk_bdev_get_io(channel);

	bdev_io->ch = channel;
	bdev_io->type = SPDK_BDEV_IO_TYPE_WRITE;
	bdev_io->u.bdev.iovs = iov;
	bdev_io->u.bdev.iovcnt = iovcnt;
	bdev_io->u.bdev.num_blocks = num_blocks;
	bdev_io->u.bdev.offset_blocks = offset_blocks;
	spdk_bdev_io_init(bdev_io, bdev, cb_arg, cb);

	spdk_bdev_io_submit(bdev_io);
	return 0;
}

/**
 * Get the context buffer associated with an I/O channel.
 *
 * \param ch I/O channel.
 *
 * \return a pointer to the context buffer.
 */
static inline void *
spdk_io_channel_get_ctx(struct spdk_io_channel *ch)
{
	return (uint8_t *)ch + sizeof(*ch);
}

static struct spdk_bdev_io *
spdk_bdev_get_io(struct spdk_bdev_channel *channel)
{
	struct spdk_bdev_mgmt_channel *ch = channel->module_ch->mgmt_ch;
	struct spdk_bdev_io *bdev_io;

	/* 如果有cache就从cache里面获取内存 */
	if (ch->per_thread_cache_count > 0) {
		bdev_io = STAILQ_FIRST(&ch->per_thread_cache);
		STAILQ_REMOVE_HEAD(&ch->per_thread_cache, buf_link);
		ch->per_thread_cache_count--;
	} else {
		bdev_io = spdk_mempool_get(g_bdev_mgr.bdev_io_pool);
	}

	return bdev_io;
}

static void
spdk_bdev_io_init(struct spdk_bdev_io *bdev_io,
		  struct spdk_bdev *bdev, void *cb_arg,
		  spdk_bdev_io_completion_cb cb)
{
	bdev_io->bdev = bdev;
	bdev_io->caller_ctx = cb_arg;
	bdev_io->cb = cb;
	bdev_io->status = SPDK_BDEV_IO_STATUS_PENDING;
	bdev_io->in_submit_request = false;
	bdev_io->buf = NULL;
	bdev_io->io_submit_ch = NULL;
	memset(bdev_io->driver_ctx, 0, g_bdev_mgr.max_ctx_size);
}

static void
spdk_bdev_io_submit(struct spdk_bdev_io *bdev_io)
{
	struct spdk_bdev *bdev = bdev_io->bdev;

	if (bdev_io->ch->flags & BDEV_CH_QOS_ENABLED) {
		bdev_io->io_submit_ch = bdev_io->ch;
		bdev_io->ch = bdev->qos.ch;
		spdk_thread_send_msg(bdev->qos.thread, _spdk_bdev_io_submit, bdev_io);
	} else {
		_spdk_bdev_io_submit(bdev_io);
	}
}

static void
_spdk_bdev_io_submit(void *ctx)
{
	struct spdk_bdev_io *bdev_io = ctx;
	struct spdk_bdev *bdev = bdev_io->bdev;
	struct spdk_bdev_channel *bdev_ch = bdev_io->ch;
	struct spdk_io_channel *ch = bdev_ch->channel;
	struct spdk_bdev_module_channel	*module_ch = bdev_ch->module_ch;

	bdev_io->submit_tsc = spdk_get_ticks();
	bdev_ch->io_outstanding++;
	module_ch->io_outstanding++;
	bdev_io->in_submit_request = true;
	if (spdk_likely(bdev_ch->flags == 0)) {
		if (spdk_likely(TAILQ_EMPTY(&module_ch->nomem_io))) {
			/* 调用nvmelib_fn_table的bdev_nvme_submit_request */
			bdev->fn_table->submit_request(ch, bdev_io);
		} else {
			bdev_ch->io_outstanding--;
			module_ch->io_outstanding--;
			TAILQ_INSERT_TAIL(&module_ch->nomem_io, bdev_io, link);
		}
	} else if (bdev_ch->flags & BDEV_CH_RESET_IN_PROGRESS) {
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
	} else if (bdev_ch->flags & BDEV_CH_QOS_ENABLED) {
		bdev_ch->io_outstanding--;
		module_ch->io_outstanding--;
		TAILQ_INSERT_TAIL(&bdev->qos.queued, bdev_io, link);
		_spdk_bdev_qos_io_submit(bdev_ch);
	} else {
		SPDK_ERRLOG("unknown bdev_ch flag %x found\n", bdev_ch->flags);
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
	}
	bdev_io->in_submit_request = false;
}

static void
bdev_nvme_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
	int rc = _bdev_nvme_submit_request(ch, bdev_io);

	if (spdk_unlikely(rc != 0)) {
		if (rc == -ENOMEM) {
			spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_NOMEM);
		} else {
			spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
		}
	}
}


static int
_bdev_nvme_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
	struct nvme_io_channel *nvme_ch = spdk_io_channel_get_ctx(ch);
	if (nvme_ch->qpair == NULL) {
		/* The device is currently resetting */
		return -1;
	}

	switch (bdev_io->type) {
	case SPDK_BDEV_IO_TYPE_READ:
		spdk_bdev_io_get_buf(bdev_io, bdev_nvme_get_buf_cb,
				     bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen);
		return 0;
	/* 分析write */
	case SPDK_BDEV_IO_TYPE_WRITE:
		return bdev_nvme_writev((struct nvme_bdev *)bdev_io->bdev->ctxt,
					ch,
					(struct nvme_bdev_io *)bdev_io->driver_ctx,
					bdev_io->u.bdev.iovs,
					bdev_io->u.bdev.iovcnt,
					bdev_io->u.bdev.num_blocks,
					bdev_io->u.bdev.offset_blocks);

	case SPDK_BDEV_IO_TYPE_WRITE_ZEROES:
		return bdev_nvme_unmap((struct nvme_bdev *)bdev_io->bdev->ctxt,
				       ch,
				       (struct nvme_bdev_io *)bdev_io->driver_ctx,
				       bdev_io->u.bdev.offset_blocks,
				       bdev_io->u.bdev.num_blocks);

	case SPDK_BDEV_IO_TYPE_UNMAP:
		return bdev_nvme_unmap((struct nvme_bdev *)bdev_io->bdev->ctxt,
				       ch,
				       (struct nvme_bdev_io *)bdev_io->driver_ctx,
				       bdev_io->u.bdev.offset_blocks,
				       bdev_io->u.bdev.num_blocks);

	case SPDK_BDEV_IO_TYPE_RESET:
		return bdev_nvme_reset((struct nvme_bdev *)bdev_io->bdev->ctxt,
				       (struct nvme_bdev_io *)bdev_io->driver_ctx);

	case SPDK_BDEV_IO_TYPE_FLUSH:
		return bdev_nvme_flush((struct nvme_bdev *)bdev_io->bdev->ctxt,
				       (struct nvme_bdev_io *)bdev_io->driver_ctx,
				       bdev_io->u.bdev.offset_blocks,
				       bdev_io->u.bdev.num_blocks);

	case SPDK_BDEV_IO_TYPE_NVME_ADMIN:
		return bdev_nvme_admin_passthru((struct nvme_bdev *)bdev_io->bdev->ctxt,
						ch,
						(struct nvme_bdev_io *)bdev_io->driver_ctx,
						&bdev_io->u.nvme_passthru.cmd,
						bdev_io->u.nvme_passthru.buf,
						bdev_io->u.nvme_passthru.nbytes);

	case SPDK_BDEV_IO_TYPE_NVME_IO:
		return bdev_nvme_io_passthru((struct nvme_bdev *)bdev_io->bdev->ctxt,
					     ch,
					     (struct nvme_bdev_io *)bdev_io->driver_ctx,
					     &bdev_io->u.nvme_passthru.cmd,
					     bdev_io->u.nvme_passthru.buf,
					     bdev_io->u.nvme_passthru.nbytes);

	case SPDK_BDEV_IO_TYPE_NVME_IO_MD:
		return bdev_nvme_io_passthru_md((struct nvme_bdev *)bdev_io->bdev->ctxt,
						ch,
						(struct nvme_bdev_io *)bdev_io->driver_ctx,
						&bdev_io->u.nvme_passthru.cmd,
						bdev_io->u.nvme_passthru.buf,
						bdev_io->u.nvme_passthru.nbytes,
						bdev_io->u.nvme_passthru.md_buf,
						bdev_io->u.nvme_passthru.md_len);

	default:
		return -EINVAL;
	}
	return 0;
}

static int
bdev_nvme_writev(struct nvme_bdev *nbdev, struct spdk_io_channel *ch,
		 struct nvme_bdev_io *bio,
		 struct iovec *iov, int iovcnt, uint64_t lba_count, uint64_t lba)
{
	struct nvme_io_channel *nvme_ch = spdk_io_channel_get_ctx(ch);

	SPDK_DEBUGLOG(SPDK_LOG_BDEV_NVME, "write %lu blocks with offset %#lx\n",
		      lba_count, lba);

	return bdev_nvme_queue_cmd(nbdev, nvme_ch->qpair, bio, BDEV_DISK_WRITE,
				   iov, iovcnt, lba_count, lba);
}

static int
bdev_nvme_queue_cmd(struct nvme_bdev *bdev, struct spdk_nvme_qpair *qpair,
		    struct nvme_bdev_io *bio,
		    int direction, struct iovec *iov, int iovcnt, uint64_t lba_count,
		    uint64_t lba)
{
	int rc;

	bio->iovs = iov;
	bio->iovcnt = iovcnt;
	bio->iovpos = 0;
	bio->iov_offset = 0;

	if (direction == BDEV_DISK_READ) {
		rc = spdk_nvme_ns_cmd_readv(bdev->ns, qpair, lba,
					    lba_count, bdev_nvme_queued_done, bio, 0,
					    bdev_nvme_queued_reset_sgl, bdev_nvme_queued_next_sge);
	} else {
		rc = spdk_nvme_ns_cmd_writev(bdev->ns, qpair, lba,
					     lba_count, bdev_nvme_queued_done, bio, 0,
					     bdev_nvme_queued_reset_sgl, bdev_nvme_queued_next_sge);
	}

	if (rc != 0 && rc != -ENOMEM) {
		SPDK_ERRLOG("%s failed: rc = %d\n", direction == BDEV_DISK_READ ? "readv" : "writev", rc);
	}
	return rc;
}


int
spdk_nvme_ns_cmd_writev(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			uint64_t lba, uint32_t lba_count,
			spdk_nvme_cmd_cb cb_fn, void *cb_arg, uint32_t io_flags,
			spdk_nvme_req_reset_sgl_cb reset_sgl_fn,
			spdk_nvme_req_next_sge_cb next_sge_fn)
{
	struct nvme_request *req;
	struct nvme_payload payload;

	if (reset_sgl_fn == NULL || next_sge_fn == NULL) {
		return -EINVAL;
	}

	payload.type = NVME_PAYLOAD_TYPE_SGL;
	payload.md = NULL;
	payload.u.sgl.reset_sgl_fn = reset_sgl_fn;
	payload.u.sgl.next_sge_fn = next_sge_fn;
	payload.u.sgl.cb_arg = cb_arg;

	/* cb_fn: bdev_nvme_queued_done */
	req = _nvme_ns_cmd_rw(ns, qpair, &payload, 0, 0, lba, lba_count, cb_fn, cb_arg, SPDK_NVME_OPC_WRITE,
			      io_flags, 0, 0, true);
	if (req != NULL) {
		return nvme_qpair_submit_request(qpair, req);
	} else {
		return -ENOMEM;
	}
}

static struct nvme_request *
_nvme_ns_cmd_rw(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
		const struct nvme_payload *payload, uint32_t payload_offset, uint32_t md_offset,
		uint64_t lba, uint32_t lba_count, spdk_nvme_cmd_cb cb_fn, void *cb_arg, uint32_t opc,
		uint32_t io_flags, uint16_t apptag_mask, uint16_t apptag, bool check_sgl)
{
	struct nvme_request	*req;
	uint32_t		sector_size;
	uint32_t		sectors_per_max_io;
	uint32_t		sectors_per_stripe;

	if (io_flags & 0xFFFF) {
		/* The bottom 16 bits must be empty */
		return NULL;
	}

	sector_size = ns->extended_lba_size;
	sectors_per_max_io = ns->sectors_per_max_io;
	sectors_per_stripe = ns->sectors_per_stripe;

	if ((io_flags & SPDK_NVME_IO_FLAGS_PRACT) &&
	    (ns->flags & SPDK_NVME_NS_EXTENDED_LBA_SUPPORTED) &&
	    (ns->flags & SPDK_NVME_NS_DPS_PI_SUPPORTED) &&
	    (ns->md_size == 8)) {
		sector_size -= 8;
	}

	req = nvme_allocate_request(qpair, payload, lba_count * sector_size, cb_fn, cb_arg);

	req->payload_offset = payload_offset;
	req->md_offset = md_offset;

	/*
	 * Intel DC P3*00 NVMe controllers benefit from driver-assisted striping.
	 * If this controller defines a stripe boundary and this I/O spans a stripe
	 *  boundary, split the request into multiple requests and submit each
	 *  separately to hardware.
	 */
	if (sectors_per_stripe > 0 &&
	    (((lba & (sectors_per_stripe - 1)) + lba_count) > sectors_per_stripe)) {

		return _nvme_ns_cmd_split_request(ns, qpair, payload, payload_offset, md_offset, lba, lba_count,
						  cb_fn,
						  cb_arg, opc,
						  io_flags, req, sectors_per_stripe, sectors_per_stripe - 1, apptag_mask, apptag);
	} else if (lba_count > sectors_per_max_io) {
		return _nvme_ns_cmd_split_request(ns, qpair, payload, payload_offset, md_offset, lba, lba_count,
						  cb_fn,
						  cb_arg, opc,
						  io_flags, req, sectors_per_max_io, 0, apptag_mask, apptag);
	} else if (req->payload.type == NVME_PAYLOAD_TYPE_SGL && check_sgl) {
		if (ns->ctrlr->flags & SPDK_NVME_CTRLR_SGL_SUPPORTED) {
			return _nvme_ns_cmd_split_request_sgl(ns, qpair, payload, payload_offset, md_offset,
							      lba, lba_count, cb_fn, cb_arg, opc, io_flags,
							      req, apptag_mask, apptag);
		} else {
			return _nvme_ns_cmd_split_request_prp(ns, qpair, payload, payload_offset, md_offset,
							      lba, lba_count, cb_fn, cb_arg, opc, io_flags,
							      req, apptag_mask, apptag);
		}
	}

	_nvme_ns_cmd_setup_request(ns, req, opc, lba, lba_count, io_flags, apptag_mask, apptag);
	return req;
}

struct nvme_request *
nvme_allocate_request(struct spdk_nvme_qpair *qpair,
		      const struct nvme_payload *payload, uint32_t payload_size,
		      spdk_nvme_cmd_cb cb_fn, void *cb_arg)
{
	struct nvme_request *req;

	req = STAILQ_FIRST(&qpair->free_req);
	if (req == NULL) {
		return req;
	}

	STAILQ_REMOVE_HEAD(&qpair->free_req, stailq);

	/*
	 * Only memset up to (but not including) the children
	 *  TAILQ_ENTRY.  children, and following members, are
	 *  only used as part of I/O splitting so we avoid
	 *  memsetting them until it is actually needed.
	 *  They will be initialized in nvme_request_add_child()
	 *  if the request is split.
	 */
	memset(req, 0, offsetof(struct nvme_request, children));
	req->cb_fn = cb_fn; /* 赋值给req cb_fn */
	req->cb_arg = cb_arg;
	req->payload = *payload;
	req->payload_size = payload_size;
	req->qpair = qpair;
	req->pid = g_pid;

	return req;
}

tatic void
_nvme_ns_cmd_setup_request(struct spdk_nvme_ns *ns, struct nvme_request *req,
			   uint32_t opc, uint64_t lba, uint32_t lba_count,
			   uint32_t io_flags, uint16_t apptag_mask, uint16_t apptag)
{
	struct spdk_nvme_cmd	*cmd;

	cmd = &req->cmd;
	cmd->opc = opc; /* SPDK_NVME_OPC_WRITE */
	cmd->nsid = ns->id;

	*(uint64_t *)&cmd->cdw10 = lba;

	if (ns->flags & SPDK_NVME_NS_DPS_PI_SUPPORTED) {
		switch (ns->pi_type) {
		case SPDK_NVME_FMT_NVM_PROTECTION_TYPE1:
		case SPDK_NVME_FMT_NVM_PROTECTION_TYPE2:
			cmd->cdw14 = (uint32_t)lba;
			break;
		}
	}

	cmd->cdw12 = lba_count - 1;
	cmd->cdw12 |= io_flags;

	cmd->cdw15 = apptag_mask;
	cmd->cdw15 = (cmd->cdw15 << 16 | apptag);
}

int
nvme_qpair_submit_request(struct spdk_nvme_qpair *qpair, struct nvme_request *req)
{
	int			rc = 0;
	struct nvme_request	*child_req, *tmp;
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;
	bool			child_req_failed = false;

	if (ctrlr->is_failed) {
		nvme_free_request(req);
		return -ENXIO;
	}

	if (req->num_children) {
		/*
		 * This is a split (parent) request. Submit all of the children but not the parent
		 * request itself, since the parent is the original unsplit request.
		 */
		TAILQ_FOREACH_SAFE(child_req, &req->children, child_tailq, tmp) {
			if (!child_req_failed) {
				rc = nvme_qpair_submit_request(qpair, child_req);
				if (rc != 0) {
					child_req_failed = true;
				}
			} else { /* free remaining child_reqs since one child_req fails */
				nvme_request_remove_child(req, child_req);
				nvme_free_request(child_req);
			}
		}

		return rc;
	}

	return nvme_transport_qpair_submit_request(qpair, req);
}

int
nvme_transport_qpair_submit_request(struct spdk_nvme_qpair *qpair, struct nvme_request *req)
{
	/* 调用 nvme_pcie_qpair_submit_request */
	NVME_TRANSPORT_CALL(qpair->trtype, qpair_submit_request, (qpair, req));
}

int
nvme_pcie_qpair_submit_request(struct spdk_nvme_qpair *qpair, struct nvme_request *req)
{
	struct nvme_tracker	*tr;
	int			rc = 0;
	void			*md_payload;
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);

	nvme_pcie_qpair_check_enabled(qpair);

	if (nvme_qpair_is_admin_queue(qpair)) {
		nvme_robust_mutex_lock(&ctrlr->ctrlr_lock);
	}

	tr = TAILQ_FIRST(&pqpair->free_tr);

	if (tr == NULL || !pqpair->is_enabled) {
		/*
		 * No tracker is available, or the qpair is disabled due to
		 *  an in-progress controller-level reset.
		 *
		 * Put the request on the qpair's request queue to be
		 *  processed when a tracker frees up via a command
		 *  completion or when the controller reset is
		 *  completed.
		 */
		STAILQ_INSERT_TAIL(&qpair->queued_req, req, stailq);
		goto exit;
	}

	TAILQ_REMOVE(&pqpair->free_tr, tr, tq_list); /* remove tr from free_tr */
	TAILQ_INSERT_TAIL(&pqpair->outstanding_tr, tr, tq_list);
	tr->req = req;
	req->cmd.cid = tr->cid;

	if (req->payload_size && req->payload.md) {
		md_payload = req->payload.md + req->md_offset;
		tr->req->cmd.mptr = spdk_vtophys(md_payload);
		if (tr->req->cmd.mptr == SPDK_VTOPHYS_ERROR) {
			nvme_pcie_fail_request_bad_vtophys(qpair, tr);
			rc = -EINVAL;
			goto exit;
		}
	}

	/* 下面的代码会构建cmd->dptr */
	if (req->payload_size == 0) {
		/* Null payload - leave PRP fields zeroed */
		rc = 0;
	} else if (req->payload.type == NVME_PAYLOAD_TYPE_CONTIG) {
		rc = nvme_pcie_qpair_build_contig_request(qpair, req, tr);
	} else if (req->payload.type == NVME_PAYLOAD_TYPE_SGL) {
		if (ctrlr->flags & SPDK_NVME_CTRLR_SGL_SUPPORTED) {
			rc = nvme_pcie_qpair_build_hw_sgl_request(qpair, req, tr);
		} else {
			rc = nvme_pcie_qpair_build_prps_sgl_request(qpair, req, tr);
		}
	} else {
		assert(0);
		nvme_pcie_fail_request_bad_vtophys(qpair, tr);
		rc = -EINVAL;
	}

	if (rc < 0) {
		goto exit;
	}

	nvme_pcie_qpair_submit_tracker(qpair, tr);

exit:
	if (nvme_qpair_is_admin_queue(qpair)) {
		nvme_robust_mutex_unlock(&ctrlr->ctrlr_lock);
	}

	return rc;
}

static inline bool
nvme_pcie_qpair_check_enabled(struct spdk_nvme_qpair *qpair)
{
	struct nvme_pcie_qpair *pqpair = nvme_pcie_qpair(qpair);

	if (!pqpair->is_enabled &&
	    !qpair->ctrlr->is_resetting) {
		nvme_qpair_enable(qpair);
	}
	return pqpair->is_enabled;
}

static inline struct nvme_pcie_qpair *
nvme_pcie_qpair(struct spdk_nvme_qpair *qpair)
{
	return (struct nvme_pcie_qpair *)((uintptr_t)qpair - offsetof(struct nvme_pcie_qpair, qpair));
}

void
nvme_qpair_enable(struct spdk_nvme_qpair *qpair)
{
	if (nvme_qpair_is_io_queue(qpair)) {
		_nvme_io_qpair_enable(qpair);
	}

	nvme_transport_qpair_enable(qpair);
}

static inline bool
nvme_qpair_is_io_queue(struct spdk_nvme_qpair *qpair)
{
	return qpair->id != 0;
}

int
nvme_transport_qpair_enable(struct spdk_nvme_qpair *qpair)
{
	/* 调用 nvme_pcie_qpair_enable */
	NVME_TRANSPORT_CALL(qpair->trtype, qpair_enable, (qpair));
}

int
nvme_pcie_qpair_enable(struct spdk_nvme_qpair *qpair)
{
	struct nvme_pcie_qpair *pqpair = nvme_pcie_qpair(qpair);

	pqpair->is_enabled = true;
	if (nvme_qpair_is_io_queue(qpair)) {
		nvme_pcie_io_qpair_enable(qpair);
	} else {
		nvme_pcie_admin_qpair_enable(qpair);
	}

	return 0;
}

static inline bool
nvme_qpair_is_admin_queue(struct spdk_nvme_qpair *qpair)
{
	return qpair->id == 0;
}

static void
nvme_pcie_qpair_submit_tracker(struct spdk_nvme_qpair *qpair, struct nvme_tracker *tr)
{
	struct nvme_request	*req;
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_pcie_ctrlr	*pctrlr = nvme_pcie_ctrlr(qpair->ctrlr);

	tr->timed_out = 0;
	if (spdk_unlikely(qpair->active_proc && qpair->active_proc->timeout_cb_fn != NULL)) {
		tr->submit_tick = spdk_get_ticks();
	}

	req = tr->req;
	pqpair->tr[tr->cid].active = true;

	/* Copy the command from the tracker to the submission queue. */
	/* 在admine创建io    SQ的时候已经把  pqpair->cmd的物理地址告诉ssd了 */
	nvme_pcie_copy_command(&pqpair->cmd[pqpair->sq_tail], &req->cmd);

	/* 表示到了队列的最大值了 */
	if (++pqpair->sq_tail == pqpair->num_entries) {
		pqpair->sq_tail = 0;
	}

	if (pqpair->sq_tail == pqpair->sq_head) {
		SPDK_ERRLOG("sq_tail is passing sq_head!\n");
	}

	spdk_wmb();
	g_thread_mmio_ctrlr = pctrlr;
	if (spdk_likely(nvme_pcie_qpair_update_mmio_required(qpair,
			pqpair->sq_tail,
			pqpair->sq_shadow_tdbl,
			pqpair->sq_eventidx))) {
		/* 将pqpair->sq_tail写道pqpair->sq_tdbl地址中 
		 * pqpair->sq_tdbl为bar映射的地址
		 */
		spdk_mmio_write_4(pqpair->sq_tdbl, pqpair->sq_tail);
	}
	g_thread_mmio_ctrlr = NULL;
}

static inline struct nvme_pcie_ctrlr *
nvme_pcie_ctrlr(struct spdk_nvme_ctrlr *ctrlr)
{
	return (struct nvme_pcie_ctrlr *)((uintptr_t)ctrlr - offsetof(struct nvme_pcie_ctrlr, ctrlr));
}

static inline void
spdk_mmio_write_4(volatile uint32_t *addr, uint32_t val)
{
	spdk_compiler_barrier();
	*addr = val;
}


/* 响应了虚拟机的请求之后，然后像虚拟机返回请求结果
 * 会在poller里面处理 
 */
static int
bdev_nvme_poll(void *arg)
{
	struct nvme_io_channel *ch = arg;
	int32_t num_completions;

	if (ch->collect_spin_stat && ch->start_ticks == 0) {
		ch->start_ticks = spdk_get_ticks();
	}

	num_completions = spdk_nvme_qpair_process_completions(ch->qpair, 0);

	if (ch->collect_spin_stat) {
		if (num_completions > 0) {
			if (ch->end_ticks != 0) {
				ch->spin_ticks += (ch->end_ticks - ch->start_ticks);
				ch->end_ticks = 0;
			}
			ch->start_ticks = 0;
		} else {
			ch->end_ticks = spdk_get_ticks();
		}
	}

	return num_completions;
}

int32_t
spdk_nvme_qpair_process_completions(struct spdk_nvme_qpair *qpair, uint32_t max_completions)
{
	int32_t ret;

	if (qpair->ctrlr->is_failed) {
		nvme_qpair_fail(qpair);
		return 0;
	}

	qpair->in_completion_context = 1;
	ret = nvme_transport_qpair_process_completions(qpair, max_completions);
	qpair->in_completion_context = 0;
	if (qpair->delete_after_completion_context) {
		/*
		 * A request to delete this qpair was made in the context of this completion
		 *  routine - so it is safe to delete it now.
		 */
		spdk_nvme_ctrlr_free_io_qpair(qpair);
	}
	return ret;
}

int32_t
nvme_transport_qpair_process_completions(struct spdk_nvme_qpair *qpair, uint32_t max_completions)
{
	/* 调用 pcie_nvme_qpair_process_completions */
	NVME_TRANSPORT_CALL(qpair->trtype, qpair_process_completions, (qpair, max_completions));
}

int32_t
nvme_pcie_qpair_process_completions(struct spdk_nvme_qpair *qpair, uint32_t max_completions)
{
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_pcie_ctrlr	*pctrlr = nvme_pcie_ctrlr(qpair->ctrlr);
	struct nvme_tracker	*tr;
	struct spdk_nvme_cpl	*cpl;
	uint32_t		 num_completions = 0;
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;

	if (spdk_unlikely(!nvme_pcie_qpair_check_enabled(qpair))) {
		/*
		 * qpair is not enabled, likely because a controller reset is
		 *  is in progress.  Ignore the interrupt - any I/O that was
		 *  associated with this interrupt will get retried when the
		 *  reset is complete.
		 */
		return 0;
	}

	if (spdk_unlikely(nvme_qpair_is_admin_queue(qpair))) {
		nvme_robust_mutex_lock(&ctrlr->ctrlr_lock);
	}

	if (max_completions == 0 || max_completions > pqpair->max_completions_cap) {
		/*
		 * max_completions == 0 means unlimited, but complete at most
		 * max_completions_cap batch of I/O at a time so that the completion
		 * queue doorbells don't wrap around.
		 */
		max_completions = pqpair->max_completions_cap;
	}

	while (1) {
		/* 从complete queue head读取数据 */
		cpl = &pqpair->cpl[pqpair->cq_head];

		/* p不能与pqpair->phase表示新的cpl */
		if (cpl->status.p != pqpair->phase) {
			break;
		}

		tr = &pqpair->tr[cpl->cid];
		pqpair->sq_head = cpl->sqhd;

		if (tr->active) {
			nvme_pcie_qpair_complete_tracker(qpair, tr, cpl, true);
		} else {
			SPDK_ERRLOG("cpl does not map to outstanding cmd\n");
			nvme_qpair_print_completion(qpair, cpl);
			assert(0);
		}

		if (spdk_unlikely(++pqpair->cq_head == pqpair->num_entries)) {
			pqpair->cq_head = 0;
			pqpair->phase = !pqpair->phase;
		}

		if (++num_completions == max_completions) {
			break;
		}
	}

	if (num_completions > 0) {
		g_thread_mmio_ctrlr = pctrlr;
		if (spdk_likely(nvme_pcie_qpair_update_mmio_required(qpair, pqpair->cq_head,
				pqpair->cq_shadow_hdbl,
				pqpair->cq_eventidx))) {
			/* 将pqpair->cq_head写入pqpair->cq_hdbl */
			spdk_mmio_write_4(pqpair->cq_hdbl, pqpair->cq_head);
		}
		g_thread_mmio_ctrlr = NULL;
	}

	/* We don't want to expose the admin queue to the user,
	 * so when we're timing out admin commands set the
	 * qpair to NULL.
	 */
	if (!nvme_qpair_is_admin_queue(qpair) && spdk_unlikely(qpair->active_proc->timeout_cb_fn != NULL) &&
	    qpair->ctrlr->state == NVME_CTRLR_STATE_READY) {
		/*
		 * User registered for timeout callback
		 */
		nvme_pcie_qpair_check_timeout(qpair);
	}

	/* Before returning, complete any pending admin request. */
	if (spdk_unlikely(nvme_qpair_is_admin_queue(qpair))) {
		nvme_pcie_qpair_complete_pending_admin_request(qpair);

		nvme_robust_mutex_unlock(&ctrlr->ctrlr_lock);
	}

	return num_completions;
}

static void
nvme_pcie_qpair_complete_tracker(struct spdk_nvme_qpair *qpair, struct nvme_tracker *tr,
				 struct spdk_nvme_cpl *cpl, bool print_on_error)
{
	struct nvme_pcie_qpair		*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_request		*req;
	bool				retry, error, was_active;
	bool				req_from_current_proc = true;

	req = tr->req;

	assert(req != NULL);

	error = spdk_nvme_cpl_is_error(cpl);
	retry = error && nvme_completion_is_retry(cpl) &&
		req->retries < spdk_nvme_retry_count;

	if (error && print_on_error) {
		nvme_qpair_print_command(qpair, &req->cmd);
		nvme_qpair_print_completion(qpair, cpl);
	}

	was_active = pqpair->tr[cpl->cid].active;
	pqpair->tr[cpl->cid].active = false;

	assert(cpl->cid == req->cmd.cid);

	if (retry) {
		req->retries++;
		nvme_pcie_qpair_submit_tracker(qpair, tr);
	} else {
		if (was_active) {
			/* Only check admin requests from different processes. */
			if (nvme_qpair_is_admin_queue(qpair) && req->pid != getpid()) {
				req_from_current_proc = false;
				nvme_pcie_qpair_insert_pending_admin_request(qpair, req, cpl);
			} else {
				if (req->cb_fn) {
					/* 调用 bdev_nvme_queued_done */
					req->cb_fn(req->cb_arg, cpl);
				}
			}
		}

		if (req_from_current_proc == true) {
			nvme_free_request(req);
		}

		tr->req = NULL;

		TAILQ_REMOVE(&pqpair->outstanding_tr, tr, tq_list);
		TAILQ_INSERT_HEAD(&pqpair->free_tr, tr, tq_list);

		/*
		 * If the controller is in the middle of resetting, don't
		 *  try to submit queued requests here - let the reset logic
		 *  handle that instead.
		 */
		if (!STAILQ_EMPTY(&qpair->queued_req) &&
		    !qpair->ctrlr->is_resetting) {
			req = STAILQ_FIRST(&qpair->queued_req);
			STAILQ_REMOVE_HEAD(&qpair->queued_req, stailq);
			nvme_qpair_submit_request(qpair, req);
		}
	}
}

static void
bdev_nvme_queued_done(void *ref, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_bdev_io *bdev_io = spdk_bdev_io_from_ctx((struct nvme_bdev_io *)ref);

	spdk_bdev_io_complete_nvme_status(bdev_io, cpl->status.sct, cpl->status.sc);
}

static inline struct spdk_bdev_io *
spdk_bdev_io_from_ctx(void *ctx)
{
	return (struct spdk_bdev_io *)
	       ((uintptr_t)ctx - offsetof(struct spdk_bdev_io, driver_ctx));
}

void
spdk_bdev_io_complete_nvme_status(struct spdk_bdev_io *bdev_io, int sct, int sc)
{
	if (sct == SPDK_NVME_SCT_GENERIC && sc == SPDK_NVME_SC_SUCCESS) {
		bdev_io->status = SPDK_BDEV_IO_STATUS_SUCCESS;
	} else {
		bdev_io->error.nvme.sct = sct;
		bdev_io->error.nvme.sc = sc;
		bdev_io->status = SPDK_BDEV_IO_STATUS_NVME_ERROR;
	}

	spdk_bdev_io_complete(bdev_io, bdev_io->status);
}

void
spdk_bdev_io_complete(struct spdk_bdev_io *bdev_io, enum spdk_bdev_io_status status)
{
	struct spdk_bdev *bdev = bdev_io->bdev;
	struct spdk_bdev_channel *bdev_ch = bdev_io->ch;
	struct spdk_bdev_module_channel	*module_ch = bdev_ch->module_ch;

	bdev_io->status = status;

	if (spdk_unlikely(bdev_io->type == SPDK_BDEV_IO_TYPE_RESET)) {
		bool unlock_channels = false;

		if (status == SPDK_BDEV_IO_STATUS_NOMEM) {
			SPDK_ERRLOG("NOMEM returned for reset\n");
		}
		pthread_mutex_lock(&bdev->mutex);
		if (bdev_io == bdev->reset_in_progress) {
			bdev->reset_in_progress = NULL;
			unlock_channels = true;
		}
		pthread_mutex_unlock(&bdev->mutex);

		if (unlock_channels) {
			/* Explicitly handle the QoS bdev channel as no IO channel associated */
			if (bdev->qos.enabled && bdev->qos.thread) {
				spdk_thread_send_msg(bdev->qos.thread,
						     _spdk_bdev_unfreeze_qos_channel, bdev);
			}

			spdk_for_each_channel(__bdev_to_io_dev(bdev), _spdk_bdev_unfreeze_channel,
					      bdev_io, _spdk_bdev_reset_complete);
			return;
		}
	} else {
		assert(bdev_ch->io_outstanding > 0);
		assert(module_ch->io_outstanding > 0);
		bdev_ch->io_outstanding--;
		module_ch->io_outstanding--;

		if (spdk_unlikely(status == SPDK_BDEV_IO_STATUS_NOMEM)) {
			TAILQ_INSERT_HEAD(&module_ch->nomem_io, bdev_io, link);
			/*
			 * Wait for some of the outstanding I/O to complete before we
			 *  retry any of the nomem_io.  Normally we will wait for
			 *  NOMEM_THRESHOLD_COUNT I/O to complete but for low queue
			 *  depth channels we will instead wait for half to complete.
			 */
			module_ch->nomem_threshold = spdk_max((int64_t)module_ch->io_outstanding / 2,
							      (int64_t)module_ch->io_outstanding - NOMEM_THRESHOLD_COUNT);

			/*
			 *  When last IO complete with NOMEM and module_ch->io_outstanding is 0(no inflght IO),
			 *  it means the IOs on nomem_io list would not be processed any more. 
			 *  Only would be happen when work with multi-bdev.
			 *
			 *  So we need to force retry one IO which on nomem_io list to trigger the list can be 
			 *  processed again.
			 */
			if (!module_ch->io_outstanding && (module_ch->nomem_io_trigger_poller == NULL)) {
				module_ch->nomem_io_trigger_poller = spdk_poller_register(spdk_bdev_nomem_io_trigger,
								bdev_ch, 0);
			}
			return;
		}

		if ((module_ch->nomem_io_abort == false) && spdk_unlikely(!TAILQ_EMPTY(&module_ch->nomem_io))) {
			_spdk_bdev_ch_retry_io(bdev_ch, false);
		}
	}

	_spdk_bdev_io_complete(bdev_io);
}

static inline void
_spdk_bdev_io_complete(void *ctx)
{
	struct spdk_bdev_io *bdev_io = ctx;

	if (spdk_unlikely(bdev_io->in_submit_request || bdev_io->io_submit_ch)) {
		/*
		 * Send the completion to the thread that originally submitted the I/O,
		 * which may not be the current thread in the case of QoS.
		 */
		if (bdev_io->io_submit_ch) {
			bdev_io->ch = bdev_io->io_submit_ch;
			bdev_io->io_submit_ch = NULL;
		}

		/*
		 * Defer completion to avoid potential infinite recursion if the
		 * user's completion callback issues a new I/O.
		 */
		spdk_thread_send_msg(spdk_io_channel_get_thread(bdev_io->ch->channel),
				     _spdk_bdev_io_complete, bdev_io);
		return;
	}

	if (bdev_io->status == SPDK_BDEV_IO_STATUS_SUCCESS) {
		switch (bdev_io->type) {
		case SPDK_BDEV_IO_TYPE_READ:
			bdev_io->ch->stat.bytes_read += bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen;
			bdev_io->ch->stat.num_read_ops++;
			bdev_io->ch->stat.read_latency_ticks += (spdk_get_ticks() - bdev_io->submit_tsc);
			break;
		case SPDK_BDEV_IO_TYPE_WRITE:
			bdev_io->ch->stat.bytes_written += bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen;
			bdev_io->ch->stat.num_write_ops++;
			bdev_io->ch->stat.write_latency_ticks += (spdk_get_ticks() - bdev_io->submit_tsc);
			break;
		default:
			break;
		}
	}

#ifdef SPDK_CONFIG_VTUNE
	uint64_t now_tsc = spdk_get_ticks();
	if (now_tsc > (bdev_io->ch->start_tsc + bdev_io->ch->interval_tsc)) {
		uint64_t data[5];

		data[0] = bdev_io->ch->stat.num_read_ops;
		data[1] = bdev_io->ch->stat.bytes_read;
		data[2] = bdev_io->ch->stat.num_write_ops;
		data[3] = bdev_io->ch->stat.bytes_written;
		data[4] = bdev_io->bdev->fn_table->get_spin_time ?
			  bdev_io->bdev->fn_table->get_spin_time(bdev_io->ch->channel) : 0;

		__itt_metadata_add(g_bdev_mgr.domain, __itt_null, bdev_io->ch->handle,
				   __itt_metadata_u64, 5, data);

		memset(&bdev_io->ch->stat, 0, sizeof(bdev_io->ch->stat));
		bdev_io->ch->start_tsc = now_tsc;
	}
#endif

	if (bdev_io->cb == NULL) {
		/*
		 * In some case, eg. vhost blk dev destroy and wait timeout for inflight io complete,
		 * the mapped guest memory would be unmaped and the task pool would be freed at first.
		 *
		 * If so, don't do completet_cb in case mem access segfault, just free bdev_io.
		 */
		spdk_bdev_free_io(bdev_io);
		return;
	}

	assert(bdev_io->cb != NULL);
	assert(spdk_get_thread() == spdk_io_channel_get_thread(bdev_io->ch->channel));

	/* 调用 blk_request_complete_cb */
	bdev_io->cb(bdev_io, bdev_io->status == SPDK_BDEV_IO_STATUS_SUCCESS,
		    bdev_io->caller_ctx);
}

static void
blk_request_finish(bool success, struct spdk_vhost_blk_task *task)
{
	*task->status = success ? VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR;
	spdk_vhost_vq_used_ring_enqueue(&task->bvdev->vdev, task->vq, task->req_idx,
					task->used_len);
	SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Finished task (%p) req_idx=%d\n status: %s\n", task,
		      task->req_idx, success ? "OK" : "FAIL");
	blk_task_finish(task);
}

/*
 * Enqueue id and len to used ring.
 */
void
spdk_vhost_vq_used_ring_enqueue(struct spdk_vhost_dev *vdev, struct spdk_vhost_virtqueue *virtqueue,
				uint16_t id, uint32_t len)
{
	struct rte_vhost_vring *vring = &virtqueue->vring;
	struct vring_used *used = vring->used;
	uint16_t last_idx = vring->last_used_idx & (vring->size - 1);

	SPDK_DEBUGLOG(SPDK_LOG_VHOST_RING,
		      "Queue %td - USED RING: last_idx=%"PRIu16" req id=%"PRIu16" len=%"PRIu32"\n",
		      virtqueue - vdev->virtqueue, vring->last_used_idx, id, len);

	spdk_vhost_inflight_pre_set(virtqueue, id);
	spdk_vhost_log_req_desc(vdev, virtqueue, id);

	vring->last_used_idx++;
	used->ring[last_idx].id = id;
	used->ring[last_idx].len = len;

	/* Ensure the used ring is updated before we log it or increment used->idx. */
	spdk_smp_wmb();

	spdk_vhost_log_used_vring_elem(vdev, virtqueue, last_idx);
	* (volatile uint16_t *) &used->idx = vring->last_used_idx;
	spdk_vhost_log_used_vring_idx(vdev, virtqueue);

	/* Ensure all our used ring changes are visible to the guest at the time
	 * of interrupt.
	 * TODO: this is currently an sfence on x86. For other architectures we
	 * will most likely need an smp_mb(), but smp_mb() is an overkill for x86.
	 */
	spdk_wmb();

	virtqueue->used_req_cnt++;

	spdk_vhost_inflight_post_set(virtqueue, id);
}

static void
blk_task_finish(struct spdk_vhost_blk_task *task)
{
	assert(task->bvdev->vdev.task_cnt > 0);
	task->bvdev->vdev.task_cnt--;
	task->used = false;
}


/* io sq和cq创建 */
static int
bdev_nvme_create_cb(void *io_device, void *ctx_buf)
{
	struct spdk_nvme_ctrlr *ctrlr = io_device;
	struct nvme_io_channel *ch = ctx_buf;

#ifdef SPDK_CONFIG_VTUNE
	ch->collect_spin_stat = true;
#else
	ch->collect_spin_stat = false;
#endif

	ch->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ctrlr, NULL, 0);

	ch->poller = spdk_poller_register(bdev_nvme_poll, ch, 0);
	return 0;
}

struct spdk_nvme_qpair *
spdk_nvme_ctrlr_alloc_io_qpair(struct spdk_nvme_ctrlr *ctrlr,
			       const struct spdk_nvme_io_qpair_opts *user_opts,
			       size_t opts_size)
{
	uint32_t				qid;
	struct spdk_nvme_qpair			*qpair;
	union spdk_nvme_cc_register		cc;
	struct spdk_nvme_io_qpair_opts		opts;


	/*
	 * Get the default options, then overwrite them with the user-provided options
	 * up to opts_size.
	 *
	 * This allows for extensions of the opts structure without breaking
	 * ABI compatibility.
	 */
	spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr, &opts, sizeof(opts));
	/* user_opts为NULL */
	if (user_opts) {
		memcpy(&opts, user_opts, spdk_min(sizeof(opts), opts_size));
	}

	if (nvme_ctrlr_get_cc(ctrlr, &cc)) {
		SPDK_ERRLOG("get_cc failed\n");
		return NULL;
	}

	/* Only the low 2 bits (values 0, 1, 2, 3) of QPRIO are valid. */
	if ((opts.qprio & 3) != opts.qprio) {
		return NULL;
	}

	/*
	 * Only value SPDK_NVME_QPRIO_URGENT(0) is valid for the
	 * default round robin arbitration method.
	 */
	if ((cc.bits.ams == SPDK_NVME_CC_AMS_RR) && (opts.qprio != SPDK_NVME_QPRIO_URGENT)) {
		SPDK_ERRLOG("invalid queue priority for default round robin arbitration method\n");
		return NULL;
	}

	nvme_robust_mutex_lock(&ctrlr->ctrlr_lock);

	/*
	 * Get the first available I/O queue ID.
	 * 分配 IO queue ID
	 */
	qid = spdk_bit_array_find_first_set(ctrlr->free_io_qids, 1);
	if (qid > ctrlr->opts.num_io_queues) {
		SPDK_ERRLOG("No free I/O queue IDs\n");
		nvme_robust_mutex_unlock(&ctrlr->ctrlr_lock);
		return NULL;
	}

	qpair = nvme_transport_ctrlr_create_io_qpair(ctrlr, qid, &opts);

	spdk_bit_array_clear(ctrlr->free_io_qids, qid);
	TAILQ_INSERT_TAIL(&ctrlr->active_io_qpairs, qpair, tailq);

	nvme_ctrlr_proc_add_io_qpair(qpair);

	nvme_robust_mutex_unlock(&ctrlr->ctrlr_lock);

	if (ctrlr->quirks & NVME_QUIRK_DELAY_AFTER_QUEUE_ALLOC) {
		spdk_delay_us(100);
	}

	return qpair;
}

void
spdk_nvme_ctrlr_get_default_io_qpair_opts(struct spdk_nvme_ctrlr *ctrlr,
		struct spdk_nvme_io_qpair_opts *opts,
		size_t opts_size)
{
	memset(opts, 0, opts_size);

#define FIELD_OK(field) \
	offsetof(struct spdk_nvme_io_qpair_opts, field) + sizeof(opts->field) <= opts_size

	if (FIELD_OK(qprio)) {
		opts->qprio = SPDK_NVME_QPRIO_URGENT;
	}

	if (FIELD_OK(io_queue_size)) {
		opts->io_queue_size = ctrlr->opts.io_queue_size;
	}

	if (FIELD_OK(io_queue_requests)) {
		opts->io_queue_requests = ctrlr->opts.io_queue_requests;
	}

#undef FIELD_OK
}

struct spdk_nvme_qpair *
nvme_transport_ctrlr_create_io_qpair(struct spdk_nvme_ctrlr *ctrlr, uint16_t qid,
				     const struct spdk_nvme_io_qpair_opts *opts)
{
	/* 调用 pcie_nvme_ctrlr_create_io_qpair */
	NVME_TRANSPORT_CALL(ctrlr->trid.trtype, ctrlr_create_io_qpair, (ctrlr, qid, opts));
}

struct spdk_nvme_qpair *
nvme_pcie_ctrlr_create_io_qpair(struct spdk_nvme_ctrlr *ctrlr, uint16_t qid,
				const struct spdk_nvme_io_qpair_opts *opts)
{
	struct nvme_pcie_qpair *pqpair;
	struct spdk_nvme_qpair *qpair;
	int rc;

	pqpair = spdk_dma_zmalloc(sizeof(*pqpair), 64, NULL);


	pqpair->num_entries = opts->io_queue_size;

	qpair = &pqpair->qpair;

	rc = nvme_qpair_init(qpair, qid, ctrlr, opts->qprio, opts->io_queue_requests);

	rc = nvme_pcie_qpair_construct(qpair);

	rc = _nvme_pcie_ctrlr_create_io_qpair(ctrlr, qpair, qid);

	return qpair;
}

int
nvme_qpair_init(struct spdk_nvme_qpair *qpair, uint16_t id,
		struct spdk_nvme_ctrlr *ctrlr,
		enum spdk_nvme_qprio qprio,
		uint32_t num_requests)
{
	size_t req_size_padded;
	uint32_t i;

	qpair->id = id;
	qpair->qprio = qprio;

	qpair->in_completion_context = 0;
	qpair->delete_after_completion_context = 0;
	qpair->no_deletion_notification_needed = 0;

	qpair->ctrlr = ctrlr;
	qpair->trtype = ctrlr->trid.trtype;

	STAILQ_INIT(&qpair->free_req);
	STAILQ_INIT(&qpair->queued_req);

	req_size_padded = (sizeof(struct nvme_request) + 63) & ~(size_t)63;

	qpair->req_buf = spdk_dma_zmalloc(req_size_padded * num_requests, 64, NULL);

	for (i = 0; i < num_requests; i++) {
		struct nvme_request *req = qpair->req_buf + i * req_size_padded;
		/* 将req添加到qpair->free_req链表中 */
		STAILQ_INSERT_HEAD(&qpair->free_req, req, stailq);
	}

	return 0;
}

static int
nvme_pcie_qpair_construct(struct spdk_nvme_qpair *qpair)
{
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;
	struct nvme_pcie_ctrlr	*pctrlr = nvme_pcie_ctrlr(ctrlr);
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_tracker	*tr;
	uint16_t		i;
	volatile uint32_t	*doorbell_base;
	uint64_t		phys_addr = 0;
	uint64_t		offset;
	uint16_t		num_trackers;
	size_t			page_size = sysconf(_SC_PAGESIZE);

	/*
	 * Limit the maximum number of completions to return per call to prevent wraparound,
	 * and calculate how many trackers can be submitted at once without overflowing the
	 * completion queue.
	 */
	pqpair->max_completions_cap = pqpair->num_entries / 4;
	pqpair->max_completions_cap = spdk_max(pqpair->max_completions_cap, NVME_MIN_COMPLETIONS);
	pqpair->max_completions_cap = spdk_min(pqpair->max_completions_cap, NVME_MAX_COMPLETIONS);
	num_trackers = pqpair->num_entries - pqpair->max_completions_cap;

	SPDK_INFOLOG(SPDK_LOG_NVME, "max_completions_cap = %" PRIu16 " num_trackers = %" PRIu16 "\n",
		     pqpair->max_completions_cap, num_trackers);

	pqpair->sq_in_cmb = false;

	/* cmd and cpl rings must be aligned on page size boundaries. */
	/* use_cmb_sqs表示cq的buffer在ssd     controller中 */
	if (ctrlr->opts.use_cmb_sqs) {
		if (nvme_pcie_ctrlr_alloc_cmb(ctrlr, pqpair->num_entries * sizeof(struct spdk_nvme_cmd),
					      page_size, &offset) == 0) {
			pqpair->cmd = pctrlr->cmb_bar_virt_addr + offset;
			pqpair->cmd_bus_addr = pctrlr->cmb_bar_phys_addr + offset;
			pqpair->sq_in_cmb = true;
		}
	}
	/* sq_in_cmb表示sq在ssd controller中 */
	if (pqpair->sq_in_cmb == false) {
		pqpair->cmd = spdk_dma_zmalloc(pqpair->num_entries * sizeof(struct spdk_nvme_cmd),
					       page_size,
					       &pqpair->cmd_bus_addr);
	}

	pqpair->cpl = spdk_dma_zmalloc(pqpair->num_entries * sizeof(struct spdk_nvme_cpl),
				       page_size,
				       &pqpair->cpl_bus_addr);

	/* pctrlr->regs->doorbell[0].sq_tdbl是bar的地址映射到spdk中的地址 */
	doorbell_base = &pctrlr->regs->doorbell[0].sq_tdbl;
	/* 设置sq   qeue tail doorbell */
	pqpair->sq_tdbl = doorbell_base + (2 * qpair->id + 0) * pctrlr->doorbell_stride_u32;
	/* 设置cq  head doorbell地址 */
	pqpair->cq_hdbl = doorbell_base + (2 * qpair->id + 1) * pctrlr->doorbell_stride_u32;

	/*
	 * Reserve space for all of the trackers in a single allocation.
	 *   struct nvme_tracker must be padded so that its size is already a power of 2.
	 *   This ensures the PRP list embedded in the nvme_tracker object will not span a
	 *   4KB boundary, while allowing access to trackers in tr[] via normal array indexing.
	 */
	pqpair->tr = spdk_dma_zmalloc(num_trackers * sizeof(*tr), sizeof(*tr), &phys_addr);

	TAILQ_INIT(&pqpair->free_tr);
	TAILQ_INIT(&pqpair->outstanding_tr);

	for (i = 0; i < num_trackers; i++) {
		tr = &pqpair->tr[i];
		nvme_qpair_construct_tracker(tr, i, phys_addr);
		TAILQ_INSERT_HEAD(&pqpair->free_tr, tr, tq_list);
		phys_addr += sizeof(struct nvme_tracker);
	}

	nvme_pcie_qpair_reset(qpair);

	return 0;
}

static int
_nvme_pcie_ctrlr_create_io_qpair(struct spdk_nvme_ctrlr *ctrlr, struct spdk_nvme_qpair *qpair,
				 uint16_t qid)
{
	struct nvme_pcie_ctrlr	*pctrlr = nvme_pcie_ctrlr(ctrlr);
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_completion_poll_status	status;
	int					rc;

	status.done = false;
	rc = nvme_pcie_ctrlr_cmd_create_io_cq(ctrlr, qpair, nvme_completion_poll_cb, &status);
	if (rc != 0) {
		return rc;
	}

	while (status.done == false) {
		spdk_nvme_qpair_process_completions(ctrlr->adminq, 0);
	}
	if (spdk_nvme_cpl_is_error(&status.cpl)) {
		SPDK_ERRLOG("nvme_create_io_cq failed!\n");
		return -1;
	}

	status.done = false;
	rc = nvme_pcie_ctrlr_cmd_create_io_sq(qpair->ctrlr, qpair, nvme_completion_poll_cb, &status);
	if (rc != 0) {
		return rc;
	}

	while (status.done == false) {
		spdk_nvme_qpair_process_completions(ctrlr->adminq, 0);
	}
	if (spdk_nvme_cpl_is_error(&status.cpl)) {
		SPDK_ERRLOG("nvme_create_io_sq failed!\n");
		/* Attempt to delete the completion queue */
		status.done = false;
		rc = nvme_pcie_ctrlr_cmd_delete_io_cq(qpair->ctrlr, qpair, nvme_completion_poll_cb, &status);
		if (rc != 0) {
			return -1;
		}
		while (status.done == false) {
			spdk_nvme_qpair_process_completions(ctrlr->adminq, 0);
		}
		return -1;
	}

	if (ctrlr->shadow_doorbell) {
		pqpair->sq_shadow_tdbl = ctrlr->shadow_doorbell + (2 * qpair->id + 0) * pctrlr->doorbell_stride_u32;
		pqpair->cq_shadow_hdbl = ctrlr->shadow_doorbell + (2 * qpair->id + 1) * pctrlr->doorbell_stride_u32;
		pqpair->sq_eventidx = ctrlr->eventidx + (2 * qpair->id + 0) * pctrlr->doorbell_stride_u32;
		pqpair->cq_eventidx = ctrlr->eventidx + (2 * qpair->id + 1) * pctrlr->doorbell_stride_u32;
	}
	nvme_pcie_qpair_reset(qpair);

	return 0;
}

static int
nvme_pcie_ctrlr_cmd_create_io_cq(struct spdk_nvme_ctrlr *ctrlr,
				 struct spdk_nvme_qpair *io_que, spdk_nvme_cmd_cb cb_fn,
				 void *cb_arg)
{
	struct nvme_pcie_qpair *pqpair = nvme_pcie_qpair(io_que);
	struct nvme_request *req;
	struct spdk_nvme_cmd *cmd;

	req = nvme_allocate_request_null(ctrlr->adminq, cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = SPDK_NVME_OPC_CREATE_IO_CQ;

	/*
	 * TODO: create a create io completion queue command data
	 *  structure.
	 */
	cmd->cdw10 = ((pqpair->num_entries - 1) << 16) | io_que->id;
	/*
	 * 0x2 = interrupts enabled
	 * 0x1 = physically contiguous
	 */
	cmd->cdw11 = 0x1;
	/* 设置cq的物理地址 */
	cmd->dptr.prp.prp1 = pqpair->cpl_bus_addr;

	return nvme_ctrlr_submit_admin_request(ctrlr, req);
}

struct nvme_request *
nvme_allocate_request_null(struct spdk_nvme_qpair *qpair, spdk_nvme_cmd_cb cb_fn, void *cb_arg)
{
	return nvme_allocate_request_contig(qpair, NULL, 0, cb_fn, cb_arg);
}

struct nvme_request *
nvme_allocate_request_contig(struct spdk_nvme_qpair *qpair,
			     void *buffer, uint32_t payload_size,
			     spdk_nvme_cmd_cb cb_fn, void *cb_arg)
{
	struct nvme_payload payload;

	payload.type = NVME_PAYLOAD_TYPE_CONTIG;
	payload.u.contig = buffer;
	payload.md = NULL;

	return nvme_allocate_request(qpair, &payload, payload_size, cb_fn, cb_arg);
}

int
nvme_ctrlr_submit_admin_request(struct spdk_nvme_ctrlr *ctrlr,
				struct nvme_request *req)
{
	return nvme_qpair_submit_request(ctrlr->adminq, req);
}

void
nvme_completion_poll_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
	struct nvme_completion_poll_status	*status = arg;

	/*
	 * Copy status into the argument passed by the caller, so that
	 *  the caller can check the status to determine if the
	 *  the request passed or failed.
	 */
	memcpy(&status->cpl, cpl, sizeof(*cpl));
	status->done = true;
}

static int
nvme_pcie_ctrlr_cmd_create_io_sq(struct spdk_nvme_ctrlr *ctrlr,
				 struct spdk_nvme_qpair *io_que, spdk_nvme_cmd_cb cb_fn, void *cb_arg)
{
	struct nvme_pcie_qpair *pqpair = nvme_pcie_qpair(io_que);
	struct nvme_request *req;
	struct spdk_nvme_cmd *cmd;

	req = nvme_allocate_request_null(ctrlr->adminq, cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = SPDK_NVME_OPC_CREATE_IO_SQ;

	/*
	 * TODO: create a create io submission queue command data
	 *  structure.
	 */
	cmd->cdw10 = ((pqpair->num_entries - 1) << 16) | io_que->id;
	/* 0x1 = physically contiguous */
	cmd->cdw11 = (io_que->id << 16) | (io_que->qprio << 1) | 0x1;
	/* 设置sq  的物理地址 */
	cmd->dptr.prp.prp1 = pqpair->cmd_bus_addr;

	return nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
spdk_vhost_dev_used_signal(struct spdk_vhost_dev *vdev)
{
	struct spdk_vhost_virtqueue *virtqueue;
	uint64_t now;
	uint16_t q_idx;

	if (vdev->coalescing_delay_time_base == 0) {
		for (q_idx = 0; q_idx < vdev->max_queues; q_idx++) {
			virtqueue = &vdev->virtqueue[q_idx];

			if (virtqueue->vring.desc == NULL ||
			    (virtqueue->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
				continue;
			}

			spdk_vhost_vq_used_signal(vdev, virtqueue);
		}
	} else {
		now = spdk_get_ticks();
		check_dev_io_stats(vdev, now);

		for (q_idx = 0; q_idx < vdev->max_queues; q_idx++) {
			virtqueue = &vdev->virtqueue[q_idx];

			/* No need for event right now */
			if (now < virtqueue->next_event_time ||
			    (virtqueue->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
				continue;
			}

			if (!spdk_vhost_vq_used_signal(vdev, virtqueue)) {
				continue;
			}

			/* Syscall is quite long so update time */
			now = spdk_get_ticks();
			virtqueue->next_event_time = now + virtqueue->irq_delay_time;
		}
	}
}

