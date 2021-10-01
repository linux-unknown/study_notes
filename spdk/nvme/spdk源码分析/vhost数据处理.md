# vhost数据处理

## 响应虚拟机的请求

函数入口为`vdev_worker`,`vdev_worker`注册的路径如下：

`fdset_event_dispatch->vhost_user_read_cb-->vhost_user_msg_handler-->spdk_vhost_blk_start`

```c
bvdev->requestq_poller = spdk_poller_register(bvdev->bdev ? vdev_worker : no_bdev_vdev_worker,bvdev, 0);
```

`fdset_event_dispatch` 是一个线程，一直poll fd当qemu和vhost_spdk通过socket通信的时候，就会调用vhost_user_read_cb

### vdev_worker

处理每一个queue

```c
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
```

#### process_vq

```c
static void process_vq(struct spdk_vhost_blk_dev *bvdev, struct spdk_vhost_virtqueue *vq)
{
	struct spdk_vhost_blk_task *task;
	int rc;
	uint16_t reqs[32];
	uint16_t reqs_cnt, i;
	/* 查看avail ring，有多少个rqes需要处理 */
	reqs_cnt = spdk_vhost_vq_avail_ring_get(vq, reqs, SPDK_COUNTOF(reqs));
	if (!reqs_cnt) {
		return;
	}
	/* 处理每一个req */
	for (i = 0; i < reqs_cnt; i++) {
		task = &((struct spdk_vhost_blk_task *)vq->tasks)[reqs[i]];
		bvdev->vdev.task_cnt++;

		task->used = true;
		task->iovcnt = SPDK_COUNTOF(task->iovs);
		task->status = NULL;
		task->used_len = 0;
		task->aligned_vector = NULL;

		rc = process_blk_request(task, bvdev, vq);
	}
}
```

#### process_blk_request

```c
static int process_blk_request(struct spdk_vhost_blk_task *task, struct spdk_vhost_blk_dev *bvdev,
		    struct spdk_vhost_virtqueue *vq)
{
	const struct virtio_blk_outhdr *req;
	struct iovec *iov;
	struct iovec *data_iov = NULL;
	int iov_cnt;
	uint32_t type;
	uint32_t payload_len;
	int rc;

	/* task->req_idx会在alloc_task_pool赋值 
	 * blk_iovs_setup中会处理desc
	 */
	if (blk_iovs_setup(&bvdev->vdev, vq, task->req_idx, task->iovs, &task->iovcnt, &payload_len)) {
	}

	/* task->iovs[0]应该是struct virtio_blk_outhdr *req */
	iov = &task->iovs[0];
	req = iov->iov_base;
	/* 倒数第二个iovs的长度应该为1 */
	iov = &task->iovs[task->iovcnt - 1];

	task->status = iov->iov_base;
	/* payload是不包含req和status的 */
	payload_len -= sizeof(*req) + sizeof(*task->status);
	task->iovcnt -= 2;

	type = req->type;

	switch (type) {
	case VIRTIO_BLK_T_IN:
	case VIRTIO_BLK_T_OUT:
		if (is_vector_aligned(&task->iovs[1], task->iovcnt)) {
			data_iov = &task->iovs[1];
			iov_cnt = task->iovcnt;
		} 
		if (type == VIRTIO_BLK_T_IN) {
			task->used_len = payload_len + sizeof(*task->status);
			rc = spdk_bdev_readv(bvdev->bdev_desc, bvdev->bdev_io_channel,
					     data_iov, iov_cnt, req->sector * 512,
					     payload_len, blk_request_complete_cb, task);
		} else if (!bvdev->readonly) {
			task->used_len = sizeof(*task->status);
            /* 以写为例，blk_request_complete_cb为回调函数  */
			rc = spdk_bdev_writev(bvdev->bdev_desc, bvdev->bdev_io_channel,
					      data_iov, iov_cnt, req->sector * 512,
					      payload_len, blk_request_complete_cb, task);
		} 
	}
	return 0;
}
```
#### spdk_bdev_writev
```c
int spdk_bdev_writev(struct spdk_bdev_desc *desc, struct spdk_io_channel *ch, struct iovec *iov, int iovcnt,
		 uint64_t offset, uint64_t len, spdk_bdev_io_completion_cb cb, void *cb_arg)
{
	uint64_t offset_blocks, num_blocks;

	if (spdk_bdev_bytes_to_blocks(desc->bdev, offset, &offset_blocks, len, &num_blocks) != 0) {}
	return spdk_bdev_writev_blocks(desc, ch, iov, iovcnt, offset_blocks, num_blocks, cb, cb_arg);
}
```

#### spdk_bdev_writev_blocks

```c
int spdk_bdev_writev_blocks(struct spdk_bdev_desc *desc, struct spdk_io_channel *ch,
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
	/* bdev_io->cb = cb */
	spdk_bdev_io_submit(bdev_io);
	return 0;
}
```

#### spdk_bdev_io_submit

```c
static void spdk_bdev_io_submit(struct spdk_bdev_io *bdev_io)
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
```

#### _spdk_bdev_io_submit

```c
static void _spdk_bdev_io_submit(void *ctx)
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
	} 
	bdev_io->in_submit_request = false;
}
```

#### bdev_nvme_submit_request

```c
static void bdev_nvme_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
	int rc = _bdev_nvme_submit_request(ch, bdev_io);
}
```

#### _bdev_nvme_submit_request

```c
static int _bdev_nvme_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
	struct nvme_io_channel *nvme_ch = spdk_io_channel_get_ctx(ch);

	switch (bdev_io->type) {
	case SPDK_BDEV_IO_TYPE_READ:
		spdk_bdev_io_get_buf(bdev_io, bdev_nvme_get_buf_cb,
				     bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen);
		return 0;
	/* 分析write */
	case SPDK_BDEV_IO_TYPE_WRITE:
		return bdev_nvme_writev((struct nvme_bdev *)bdev_io->bdev->ctxt,
					ch, (struct nvme_bdev_io *)bdev_io->driver_ctx, bdev_io->u.bdev.iovs,
					bdev_io->u.bdev.iovcnt, bdev_io->u.bdev.num_blocks, bdev_io->u.bdev.offset_blocks);
	}
	return 0;
}
```

#### bdev_nvme_writev

```c
static int bdev_nvme_writev(struct nvme_bdev *nbdev, struct spdk_io_channel *ch, struct nvme_bdev_io *bio,
		 struct iovec *iov, int iovcnt, uint64_t lba_count, uint64_t lba)
{
	struct nvme_io_channel *nvme_ch = spdk_io_channel_get_ctx(ch);
	return bdev_nvme_queue_cmd(nbdev, nvme_ch->qpair, bio, BDEV_DISK_WRITE, iov, iovcnt, lba_count, lba);
}
```

#### bdev_nvme_queue_cmd

```c
static int bdev_nvme_queue_cmd(struct nvme_bdev *bdev, struct spdk_nvme_qpair *qpair,
		    struct nvme_bdev_io *bio, int direction, struct iovec *iov, int iovcnt, 
		    uint64_t lba_count, uint64_t lba)
{
	int rc;
	bio->iovs = iov;
	bio->iovcnt = iovcnt;
	bio->iovpos = 0;
	bio->iov_offset = 0;

	if (direction == BDEV_DISK_READ) {
		rc = spdk_nvme_ns_cmd_readv(bdev->ns, qpair, lba, lba_count, bdev_nvme_queued_done, bio, 0,
					    bdev_nvme_queued_reset_sgl, bdev_nvme_queued_next_sge);
	} else {
        /* bdev_nvme_queued_done也是回调函数 */
		rc = spdk_nvme_ns_cmd_writev(bdev->ns, qpair, lba, lba_count, bdev_nvme_queued_done, bio, 0,
					     bdev_nvme_queued_reset_sgl, bdev_nvme_queued_next_sge);
	}
	return rc;
}
```

#### spdk_nvme_ns_cmd_writev

```c
int spdk_nvme_ns_cmd_writev(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair, uint64_t lba, 
			uint32_t lba_count, spdk_nvme_cmd_cb cb_fn, void *cb_arg, uint32_t io_flags,
			spdk_nvme_req_reset_sgl_cb reset_sgl_fn, spdk_nvme_req_next_sge_cb next_sge_fn)
{
	struct nvme_request *req;
	struct nvme_payload payload;

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
```

##### _nvme_ns_cmd_rw

```c
static struct nvme_request * _nvme_ns_cmd_rw(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
		const struct nvme_payload *payload, uint32_t payload_offset, uint32_t md_offset,
		uint64_t lba, uint32_t lba_count, spdk_nvme_cmd_cb cb_fn, void *cb_arg, uint32_t opc,
		uint32_t io_flags, uint16_t apptag_mask, uint16_t apptag, bool check_sgl)
{
	struct nvme_request	*req;
	uint32_t		sector_size;
	uint32_t		sectors_per_max_io;
	uint32_t		sectors_per_stripe;


	sector_size = ns->extended_lba_size;
	sectors_per_max_io = ns->sectors_per_max_io;
	sectors_per_stripe = ns->sectors_per_stripe;

	req = nvme_allocate_request(qpair, payload, lba_count * sector_size, cb_fn, cb_arg);

	req->payload_offset = payload_offset;
	req->md_offset = md_offset;
	_nvme_ns_cmd_setup_request(ns, req, opc, lba, lba_count, io_flags, apptag_mask, apptag);
	return req;
}
```

###### nvme_allocate_request

```c
struct nvme_request * nvme_allocate_request(struct spdk_nvme_qpair *qpair,
		      const struct nvme_payload *payload, uint32_t payload_size,
		      spdk_nvme_cmd_cb cb_fn, void *cb_arg)
{
	struct nvme_request *req;
	req = STAILQ_FIRST(&qpair->free_req);

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
	req->cb_fn = cb_fn; /* 赋值bdev_nvme_queued_done给req cb_fn */
	req->cb_arg = cb_arg;
	req->payload = *payload;
	req->payload_size = payload_size;
	req->qpair = qpair;
	req->pid = g_pid;
	return req;
}
```

###### _nvme_ns_cmd_setup_request

```c
static void _nvme_ns_cmd_setup_request(struct spdk_nvme_ns *ns, struct nvme_request *req, uint32_t opc, 					uint64_t lba, uint32_t lba_count, uint32_t io_flags, uint16_t apptag_mask, uint16_t apptag)
{
	struct spdk_nvme_cmd	*cmd;

	cmd = &req->cmd;
	/* 填充cmd */
	cmd->opc = opc; /* SPDK_NVME_OPC_WRITE */
	cmd->nsid = ns->id;
	*(uint64_t *)&cmd->cdw10 = lba;
	cmd->cdw12 = lba_count - 1;
	cmd->cdw12 |= io_flags;

	cmd->cdw15 = apptag_mask;
	cmd->cdw15 = (cmd->cdw15 << 16 | apptag);
}
```

#### nvme_qpair_submit_request

```c
int nvme_qpair_submit_request(struct spdk_nvme_qpair *qpair, struct nvme_request *req)
{
	int			rc = 0;
	struct nvme_request	*child_req, *tmp;
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;
	bool			child_req_failed = false;

	return nvme_transport_qpair_submit_request(qpair, req);
}
```

#### nvme_transport_qpair_submit_request

```c
int nvme_transport_qpair_submit_request(struct spdk_nvme_qpair *qpair, struct nvme_request *req)
{
	/* 调用 nvme_pcie_qpair_submit_request */
	NVME_TRANSPORT_CALL(qpair->trtype, qpair_submit_request, (qpair, req));
}
```



```c
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

	TAILQ_REMOVE(&pqpair->free_tr, tr, tq_list); /* remove tr from free_tr */
	TAILQ_INSERT_TAIL(&pqpair->outstanding_tr, tr, tq_list);
	tr->req = req;
	req->cmd.cid = tr->cid;

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
	} 

	nvme_pcie_qpair_submit_tracker(qpair, tr);

	return rc;
}
```

#### nvme_pcie_qpair_submit_tracker

```c
static void nvme_pcie_qpair_submit_tracker(struct spdk_nvme_qpair *qpair, struct nvme_tracker *tr)
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
	/* 在admine创建io  SQ的时候已经把  pqpair->cmd的物理地址告诉ssd了 */
	nvme_pcie_copy_command(&pqpair->cmd[pqpair->sq_tail], &req->cmd);

	/* 表示到了队列的最大值了 */
	if (++pqpair->sq_tail == pqpair->num_entries) {
		pqpair->sq_tail = 0;
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
```

## 返回虚拟机结果

`bdev_nvme_poll`最终会返回结果给虚拟机，`bdev_nvme_poll`也是一个poller，注册路径如下：

`fdset_event_dispatch->vhost_user_read_cb-->vhost_user_msg_handler-->spdk_vhost_blk_start-->`

`spdk_bdev_get_io_channel-->spdk_get_io_channel-->bdev_nvme_create_cb`

bdev_nvme_create_cb

```c
static int bdev_nvme_create_cb(void *io_device, void *ctx_buf)
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
```

### bdev_nvme_poll

```c
static int bdev_nvme_poll(void *arg)
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
```

#### spdk_nvme_qpair_process_completions

```c
int32_t spdk_nvme_qpair_process_completions(struct spdk_nvme_qpair *qpair, uint32_t max_completions)
{
	int32_t ret;
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
```

#### nvme_transport_qpair_process_completions

```c
int32_t nvme_transport_qpair_process_completions(struct spdk_nvme_qpair *qpair, uint32_t max_completions)
{
	/* 调用 pcie_nvme_qpair_process_completions */
	NVME_TRANSPORT_CALL(qpair->trtype, qpair_process_completions, (qpair, max_completions));
}
```

#### nvme_pcie_qpair_process_completions

```c
int32_t nvme_pcie_qpair_process_completions(struct spdk_nvme_qpair *qpair, uint32_t max_completions)
{
	struct nvme_pcie_qpair	*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_pcie_ctrlr	*pctrlr = nvme_pcie_ctrlr(qpair->ctrlr);
	struct nvme_tracker	*tr;
	struct spdk_nvme_cpl	*cpl;
	uint32_t		 num_completions = 0;
	struct spdk_nvme_ctrlr	*ctrlr = qpair->ctrlr;

	while (1) {
		/* 从complete queue head读取数据 */
		cpl = &pqpair->cpl[pqpair->cq_head];

		/* p等于pqpair->phase表示新的cpl */
		if (cpl->status.p != pqpair->phase) {
			break;
		}

		tr = &pqpair->tr[cpl->cid];
		pqpair->sq_head = cpl->sqhd;

		if (tr->active) {
			nvme_pcie_qpair_complete_tracker(qpair, tr, cpl, true);
		} else {
		}

		if (++num_completions == max_completions) {
			break;
		}
	}

	if (num_completions > 0) {
		g_thread_mmio_ctrlr = pctrlr;
		if (spdk_likely(nvme_pcie_qpair_update_mmio_required(qpair, pqpair->cq_head, pqpair->cq_shadow_hdbl,
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

	return num_completions;
}
```

#### nvme_pcie_qpair_complete_tracker

```c
static void nvme_pcie_qpair_complete_tracker(struct spdk_nvme_qpair *qpair, struct nvme_tracker *tr,
				 struct spdk_nvme_cpl *cpl, bool print_on_error)
{
	struct nvme_pcie_qpair		*pqpair = nvme_pcie_qpair(qpair);
	struct nvme_request		*req;
	bool				retry, error, was_active;
	bool				req_from_current_proc = true;

	req = tr->req;

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
		if (!STAILQ_EMPTY(&qpair->queued_req) && !qpair->ctrlr->is_resetting) {
			req = STAILQ_FIRST(&qpair->queued_req);
			STAILQ_REMOVE_HEAD(&qpair->queued_req, stailq);
			nvme_qpair_submit_request(qpair, req);
		}
	}
}
```

##### bdev_nvme_queued_done

```c
static void bdev_nvme_queued_done(void *ref, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_bdev_io *bdev_io = spdk_bdev_io_from_ctx((struct nvme_bdev_io *)ref);
	spdk_bdev_io_complete_nvme_status(bdev_io, cpl->status.sct, cpl->status.sc);
}
```

##### spdk_bdev_io_complete_nvme_status

```
void spdk_bdev_io_complete_nvme_status(struct spdk_bdev_io *bdev_io, int sct, int sc)
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
```

##### spdk_bdev_io_complete

```c
void spdk_bdev_io_complete(struct spdk_bdev_io *bdev_io, enum spdk_bdev_io_status status)
{
	struct spdk_bdev *bdev = bdev_io->bdev;
	struct spdk_bdev_channel *bdev_ch = bdev_io->ch;
	struct spdk_bdev_module_channel	*module_ch = bdev_ch->module_ch;
	bdev_io->status = status;
	_spdk_bdev_io_complete(bdev_io);
}
```

##### _spdk_bdev_io_complete

```c
static inline void _spdk_bdev_io_complete(void *ctx)
{
	struct spdk_bdev_io *bdev_io = ctx;
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

	/* 调用 blk_request_complete_cb */
	bdev_io->cb(bdev_io, bdev_io->status == SPDK_BDEV_IO_STATUS_SUCCESS, bdev_io->caller_ctx);
}
```

##### blk_request_complete_cb

```c
static void blk_request_complete_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
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
```

##### blk_request_finish

```
static void blk_request_finish(bool success, struct spdk_vhost_blk_task *task)
{
	*task->status = success ? VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR;
	spdk_vhost_vq_used_ring_enqueue(&task->bvdev->vdev, task->vq, task->req_idx, task->used_len);
	SPDK_DEBUGLOG(SPDK_LOG_VHOST_BLK, "Finished task (%p) req_idx=%d\n status: %s\n", task,
		      task->req_idx, success ? "OK" : "FAIL");
	blk_task_finish(task);
}
```

###### spdk_vhost_vq_used_ring_enqueue

```c
/*
 * Enqueue id and len to used ring.
 */
void spdk_vhost_vq_used_ring_enqueue(struct spdk_vhost_dev *vdev, struct spdk_vhost_virtqueue *virtqueue,
				uint16_t id, uint32_t len)
{
	struct rte_vhost_vring *vring = &virtqueue->vring;
	struct vring_used *used = vring->used;
	uint16_t last_idx = vring->last_used_idx & (vring->size - 1);

	spdk_vhost_inflight_pre_set(virtqueue, id);
	spdk_vhost_log_req_desc(vdev, virtqueue, id);

	vring->last_used_idx++;
	used->ring[last_idx].id = id;
	used->ring[last_idx].len = len;

	/* Ensure the used ring is updated before we log it or increment used->idx. */
	spdk_smp_wmb();

    /* 更新used idx */
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
```

#### spdk_vhost_dev_used_signal

vdev_worker处理完avail ring之后调用`spdk_vhost_dev_used_signal`

```c
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
```

#### spdk_vhost_vq_used_signal

```c
int spdk_vhost_vq_used_signal(struct spdk_vhost_dev *vdev, struct spdk_vhost_virtqueue *virtqueue)
{
	if (virtqueue->used_req_cnt == 0) {
		return 0;
	}

	virtqueue->req_cnt += virtqueue->used_req_cnt;
	virtqueue->used_req_cnt = 0;

	SPDK_DEBUGLOG(SPDK_LOG_VHOST_RING,
		      "Queue %td - USED RING: sending IRQ: last used %"PRIu16"\n",
		      virtqueue - vdev->virtqueue, virtqueue->vring.last_used_idx);

	eventfd_write(virtqueue->vring.callfd, (eventfd_t)1);
	return 1;
}
```

