子机提供desci，即填充avail ring

初始化都为0

try_fill_recv-->add_recvbuf_mergeable-->virtqueue_add_inbuf-->virtqueue_add

```c
head = vq->free_head;
descs_used = total_sg;
i = virtio16_to_cpu(_vq->vdev, desc[i].next);
vq->vq.num_free -= descs_used;
vq->free_head = i;

vq->desc_state[head].data = data;

/* 获取avail->idx，avail->idx为该次可以使用的vring的idx索引 */
avail = virtio16_to_cpu(_vq->vdev, vq->vring.avail->idx) & (vq->vring.num - 1);

/* 
 * 将head，即desc开始的索引放到avail的ring数组中，avail->ring[]里面存放的是desc开始的索引
 */
vq->vring.avail->ring[avail] = cpu_to_virtio16(_vq->vdev, head);

/* Descriptors and available array need to be set before we expose the new available array entries. */
virtio_wmb(vq->weak_barriers);

/* 更新avail->idx为当前使用idx+1，即下一次使用的vring idx */
vq->vring.avail->idx = cpu_to_virtio16(_vq->vdev, virtio16_to_cpu(_vq->vdev, vq->vring.avail->idx) + 1);
vq->num_added++;
```



母机

get_rx_bufs

```c
struct vring_used_elem *heads = vq->heads;
int headcount = 0;
unsigned d;
while (datalen > 0 && headcount < quota) {
    /* 返回可用desc开始的索引 */
	r = vhost_get_vq_desc(vq, vq->iov + seg, ARRAY_SIZE(vq->iov) - seg, &out,
				      &in, log, log_num);
	d = r;
    /* 将d设置vring_used_elem的id，id保存的是desc的索引开始id*/
	heads[headcount].id = cpu_to_vhost32(vq, d);
	len = iov_length(vq->iov + seg, in);
	heads[headcount].len = cpu_to_vhost32(vq, len);

	datalen -= len;
	++headcount;
	seg += in;
}

heads[headcount - 1].len = cpu_to_vhost32(vq, len + datalen);
*iovcount = seg;
return headcount;
```



vhost_get_vq_desc

```c
last_avail_idx = vq->last_avail_idx;

/* Grab the next descriptor number they're advertising, and increment the index we've seen. */
/* 
 * 将vq->avail->ring[last_avail_idx & (vq->num - 1)] 读取到ring_head
 */
if (unlikely(vhost_get_avail(vq, ring_head, &vq->avail->ring[last_avail_idx & (vq->num - 1)]))) {}

/* vq->avail->ring[last_avail_idx & (vq->num - 1)]保存的是现在使用的desc的起始索引 */
head = vhost16_to_cpu(vq, ring_head);


/* 调用__copy_from_user，将vq->desc + i复制到desc */
ret = vhost_copy_from_user(vq, &desc, vq->desc + i, sizeof desc);

translate_desc
    
/* On success, increment avail index. */
vq->last_avail_idx++;
返回head，现在可以使用的desc的起始索引
```



填充数据

```
err = sock->ops->recvmsg(NULL, sock, &msg, sock_len, MSG_DONTWAIT | MSG_TRUNC);
```

vhost_add_used_and_signal_n

```c
heads = struct vring_used_elem *heads = vq->heads
count = headcount
start = vq->last_used_idx & (vq->num - 1);

used = vq->used->ring + start;
/* 将heads复制到used中，字节数为count * sizeof *used,heads保存有desc的id和数据长度*/
vhost_copy_to_user(vq, used, heads, count * sizeof *used)
```



子机接收数据

```c
/* 获取 last_used */
last_used = (vq->last_used_idx & (vq->vring.num - 1));

/* 获取used->ring[last_used].id，即desc的索引 */
i = virtio32_to_cpu(_vq->vdev, vq->vring.used->ring[last_used].id);
/* 获取desc的数据的长度 */
*len = virtio32_to_cpu(_vq->vdev, vq->vring.used->ring[last_used].len);

/* detach_buf clears data, so grab it now. */
/* virtqueue_add会给vq->desc_state[i].data赋值 */
ret = vq->desc_state[i].data;
detach_buf(vq, i);
/* last_used_idx增加 */
vq->last_used_idx++;
```

detach_buf

```c
head = i = virtio32_to_cpu(_vq->vdev, vq->vring.used->ring[last_used].id);
vq->desc_state[head].data = NULL;
vq->vq.num_free++;
vq->vring.desc[i].next = cpu_to_virtio16(vq->vq.vdev, vq->free_head);
vq->free_head = head;

/* Plus final descriptor */
vq->vq.num_free++;
```



virtio数据传输协议：



