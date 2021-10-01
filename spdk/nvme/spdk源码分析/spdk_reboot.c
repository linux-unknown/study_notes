/* 
vhost_user_read_cb
	vhost_destroy_device
		dev->notify_ops->destroy_device
		cleanup_device(dev, 1);
		free_device(dev);
*/

static void
stop_device(int vid)
{
	struct spdk_vhost_dev *vdev;
	struct spdk_vhost_virtqueue *vq;
	struct rte_vhost_vring *q;
	int rc;
	uint16_t i;

	pthread_mutex_lock(&g_spdk_vhost_mutex);
	vdev = spdk_vhost_dev_find_by_vid(vid);

	/* spdk_vhost_blk_stop spdk_poller_unregister(&bvdev->requestq_poller); */
	rc = spdk_vhost_event_send(vdev, vdev->backend->stop_device, 3, "stop device");

	for (i = 0; i < vdev->max_queues; i++) {
		vq = &vdev->virtqueue[i]; 
		q = &vdev->virtqueue[i].vring;
		if (q->desc == NULL) {
			continue;
		}
		rte_vhost_set_vhost_vring_last_idx(vdev->vid, i, q->last_avail_idx, q->last_used_idx);

		if (vq->resubmit_list) {
			free(vq->resubmit_list);
			vq->resubmit_list = NULL;
		}

	}

	spdk_vhost_dev_mem_unregister(vdev);
	free(vdev->mem);
	spdk_vhost_free_reactor(vdev->lcore);
	vdev->lcore = -1;
	pthread_mutex_unlock(&g_spdk_vhost_mutex);
}

static int
spdk_vhost_blk_stop(struct spdk_vhost_dev *vdev, void *event_ctx)
{
	struct spdk_vhost_blk_dev *bvdev;
	struct spdk_vhost_dev_destroy_ctx *destroy_ctx;

	bvdev = to_blk_dev(vdev);

	destroy_ctx = spdk_dma_zmalloc(sizeof(*destroy_ctx), SPDK_CACHE_LINE_SIZE, NULL);
	

	destroy_ctx->bvdev = bvdev;
	destroy_ctx->event_ctx = event_ctx;

	spdk_poller_unregister(&bvdev->requestq_poller);
	
	destroy_ctx->poller = spdk_poller_register(destroy_device_poller_cb,
			      destroy_ctx, 1000);
	return 0;

}


void
spdk_poller_unregister(struct spdk_poller **ppoller)
{
	struct spdk_thread *thread;
	struct spdk_poller *poller;

	poller = *ppoller;
	if (poller == NULL) {
		return;
	}

	*ppoller = NULL;

	thread = spdk_get_thread();

	if (thread) {
		/* _spdk_reactor_stop_poller */
		thread->stop_poller_fn(poller, thread->thread_ctx);
	}
}

static void
_spdk_reactor_stop_poller(struct spdk_poller *poller, void *thread_ctx)
{
	struct spdk_reactor *reactor;

	reactor = thread_ctx;

	assert(poller->lcore == spdk_env_get_current_core());

	if (poller->state == SPDK_POLLER_STATE_RUNNING) {
		/*
		 * We are being called from the poller_fn, so set the state to unregistered
		 * and let the reactor loop free the poller.
		 */
		poller->state = SPDK_POLLER_STATE_UNREGISTERED;
	} else {
		/* Poller is not running currently, so just free it. */
		if (poller->period_ticks) {
			TAILQ_REMOVE(&reactor->timer_pollers, poller, tailq);
		} else {
			TAILQ_REMOVE(&reactor->active_pollers, poller, tailq);
		}

		free(poller);
	}
}


