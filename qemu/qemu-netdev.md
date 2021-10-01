# vhost-user netdev

9.178.87.156

qemu网络的参数

```
-chardev socket,id=charnet0,path=/var/run/vhost_sock 
-netdev vhost-user,chardev=charnet0,queues=4,id=hostnet0,ifname=veth_A167681a 
-device virtio-net-pci,event_idx=on,mq=on,vectors=10,rx_queue_size=1024,tx_queue_size=1024,netdev=hostnet0,
id=net0,mac=52:54:00:a1:67:68,bus=pci.0,addr=0x5

 chardev 隐式参数为backend=socket
 netdev 隐式参数为type=vhost-user
 device 隐式参数为driver=virtio-net-pci
```

# netdev 初始化

## main

```c
QemuOptsList qemu_netdev_opts = {
    .name = "netdev",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_netdev_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    },
};

int main(int argc, char **argv, char **envp)
{
	qemu_add_opts(&qemu_netdev_opts);
	if (net_init_clients() < 0) { exit(1); }
}
```

### net_init_clients

```c
int net_init_clients(void)
{
    QemuOptsList *net = qemu_find_opts("net");

    if (default_net) {
        /* if no clients, we use a default config */
        qemu_opts_set(net, NULL, "type", "nic", &error_abort);
#ifdef CONFIG_SLIRP
        qemu_opts_set(net, NULL, "type", "user", &error_abort);
#endif
    }

    net_change_state_entry = qemu_add_vm_change_state_handler(net_vm_change_state_handler, NULL);

    QTAILQ_INIT(&net_clients);

    if (qemu_opts_foreach(qemu_find_opts("netdev"), net_init_netdev, NULL, NULL)) {
        return -1;
    }

    if (qemu_opts_foreach(net, net_init_client, NULL, NULL)) {
        return -1;
    }

    return 0;
}
```

net_init_netdev-->net_client_init

```c
int net_client_init(QemuOpts *opts, int is_netdev, Error **errp)
{
    void *object = NULL;
    Error *err = NULL;
    int ret = -1;
    OptsVisitor *ov = opts_visitor_new(opts); /* 将ndevdev 的opts转换为 OptsVisitor */
    Visitor *v = opts_get_visitor(ov);
	/* is_netdev为1 */
    if (is_netdev) {
        visit_type_Netdev(v, NULL, (Netdev **)&object, &err);
    }

    if (!err) {
        ret = net_client_init1(object, is_netdev, &err);
    }
    return ret;
}

```

### visit_type_Netdev

```c
void visit_type_Netdev(Visitor *v, const char *name, Netdev **obj, Error **errp)
{
    Error *err = NULL;
    /* visit_start_struct会分配Netdev内存 */
    visit_start_struct(v, name, (void **)obj, sizeof(Netdev), &err);
    visit_type_Netdev_members(v, *obj, &err);
    error_propagate(errp, err);
    err = NULL;
}
```

### visit_type_Netdev_members

```c
void visit_type_Netdev_members(Visitor *v, Netdev *obj, Error **errp)
{
    Error *err = NULL;
	/* 将id的值赋值给obj->id */
    visit_type_str(v, "id", &obj->id, &err);
    visit_type_NetClientOptions(v, "opts", &obj->opts, &err); 
}
```

```c
void visit_type_NetClientOptions(Visitor *v, const char *name, NetClientOptions **obj, Error **errp)
{
    Error *err = NULL;
    /* 分配NetClientOptions内存 */
    visit_start_struct(v, name, (void **)obj, sizeof(NetClientOptions), &err);
    visit_type_NetClientOptions_members(v, *obj, &err);
}
```

```c
void visit_type_NetClientOptions_members(Visitor *v, NetClientOptions *obj, Error **errp)
{
    Error *err = NULL;
	/* 将type的值保存到obj->type中 */
    visit_type_NetClientOptionsKind(v, "type", &obj->type, &err);

    switch (obj->type) {
    case NET_CLIENT_OPTIONS_KIND_NONE:
        visit_type_q_obj_NetdevNoneOptions_wrapper_members(v, &obj->u.none, &err);
        break;
	case NET_CLIENT_OPTIONS_KIND_VHOST_USER:
        visit_type_q_obj_NetdevVhostUserOptions_wrapper_members(v, &obj->u.vhost_user, &err);
        break;
    }
}
```

visit_type_q_obj_NetdevVhostUserOptions_wrapper_members-->visit_type_NetdevVhostUserOptions

```c
void visit_type_NetdevVhostUserOptions(Visitor *v, const char *name, NetdevVhostUserOptions **obj, Error **errp)
{
    Error *err = NULL;
	/* 分配NetdevVhostUserOptions内存 */
    visit_start_struct(v, name, (void **)obj, sizeof(NetdevVhostUserOptions), &err);
    visit_type_NetdevVhostUserOptions_members(v, *obj, &err);
    error_propagate(errp, err);
    err = NULL;
}
```

#### visit_type_NetdevVhostUserOptions_members

```c
void visit_type_NetdevVhostUserOptions_members(Visitor *v, NetdevVhostUserOptions *obj, Error **errp)
{
    Error *err = NULL;
	/* 将chardev的值保存到obj->chardev */
    visit_type_str(v, "chardev", &obj->chardev, &err);

    if (visit_optional(v, "vhostforce", &obj->has_vhostforce)) {
        visit_type_bool(v, "vhostforce", &obj->vhostforce, &err);
    }
	/* 将queues的值保存到obj->queues */
    if (visit_optional(v, "queues", &obj->has_queues)) {
        visit_type_int(v, "queues", &obj->queues, &err);
    }
	/* 将ifname的值保存到obj->ifname */
    visit_type_str(v, "ifname", &obj->ifname, &err);
}
```

最终，netdev的参数都保存到了struct Netdev结构体之中

### net_client_init1

```c
static int net_client_init1(const void *object, int is_netdev, Error **errp)
{
    const NetClientOptions *opts;
    const char *name;
    NetClientState *peer = NULL;

    if (is_netdev) {
        const Netdev *netdev = object;
        opts = netdev->opts;
        name = netdev->id;/* hostnet0 */
    }
	/* 执行 net_init_vhost_user */
    if (net_client_init_fun[opts->type](opts, name, peer, errp) < 0) {
    }
    return 0;
}
```

#### net_init_vhost_user

```c
int net_init_vhost_user(const NetClientOptions *opts, const char *name,
                        NetClientState *peer, Error **errp)
{
    int queues;
    const NetdevVhostUserOptions *vhost_user_opts;
    CharDriverState *chr;
    char *ifname;

    vhost_user_opts = opts->u.vhost_user.data;
    /* 返回CharDriverState，这个是根据chardev参数生成的结构，会进行socket的初始化 */
    chr = net_vhost_claim_chardev(vhost_user_opts, errp);

    /* verify net frontend */
    if (qemu_opts_foreach(qemu_find_opts("device"), net_vhost_check_net, (char *)name, errp)) {
        return -1;
    }
    /* queue数目 */
    queues = vhost_user_opts->has_queues ? vhost_user_opts->queues : 1;
 	/* ifname */
    ifname = vhost_user_opts->ifname;

    return net_vhost_user_init(peer, "vhost_user", name, chr, queues, ifname);
}
```



```c
static int net_vhost_user_init(NetClientState *peer, const char *device,
                               const char *name, CharDriverState *chr,
                               int queues, char *ifname)
{
    NetClientState *nc, *nc0 = NULL;
    VhostUserState *s;
    int i;

    for (i = 0; i < queues; i++) {
        /* qemu_new_net_client会分配VhostUserState内存，VhostUserState包含NetClientState
         * 给nc赋值	
         */
        nc = qemu_new_net_client(&net_vhost_user_info, peer, device, name);
        if (!nc0) {
            nc0 = nc;
        }

        snprintf(nc->info_str, sizeof(nc->info_str), "vhost-user%d to %s", i, chr->label);
		/* queue_index 增加 */
        nc->queue_index = i;
		/* qemu_new_net_client 会分配VhostUserState内存 */
        s = DO_UPCAST(VhostUserState, nc, nc);
        s->chr = chr;
        strncpy(s->ifname, ifname, VHOST_USER_IFNAME_MAX_LENGTH);
    }

    s = DO_UPCAST(VhostUserState, nc, nc0);
    do {
        if (qemu_chr_wait_connected(chr, &err) < 0) { }
		/* qemu_chr_wait_connected连接之后会调用net_vhost_user_event
		 * CHR_EVENT_OPENED事件类型为CHR_EVENT_OPENED
		 */
        qemu_chr_add_handlers(chr, NULL, NULL, net_vhost_user_event, nc0->name);
    } while (!s->started);

    return 0;
}
```

#### net_vhost_user_event

```c
static void net_vhost_user_event(void *opaque, int event)
{
    const char *name = opaque;
    NetClientState *ncs[MAX_QUEUE_NUM];
    VhostUserState *s;
    Error *err = NULL;
    int queues;

    queues = qemu_find_net_clients_except(name, ncs, NET_CLIENT_OPTIONS_KIND_NIC, MAX_QUEUE_NUM);

	/* qemu_find_net_clients_except会给ncs赋值 */
    s = DO_UPCAST(VhostUserState, nc, ncs[0]);

    switch (event) {
    case CHR_EVENT_OPENED:
        s->watch = qemu_chr_fe_add_watch(s->chr, G_IO_HUP, net_vhost_user_watch, s);
        if (vhost_user_start(queues, ncs) < 0) {
            qemu_chr_disconnect(s->chr);
            return;
        }

        error_report("vhost net set ifname %s",  s->ifname);

        if (vhost_net_set_ifname(ncs[0], s->ifname) < 0) {
            error_report("vhost net set ifname failed");
            return;
        }
        qmp_set_link(name, true, &err);
        s->started = true;
        break;
    case CHR_EVENT_CLOSED:
        qmp_set_link(name, false, &err);
        vhost_user_stop(queues, ncs);
        g_source_remove(s->watch);
        s->watch = 0;
        break;
    }
}

```

#### vhost_user_start

```c
static int vhost_user_start(int queues, NetClientState *ncs[])
{
    VhostNetOptions options;
    struct vhost_net *net = NULL;
    VhostUserState *s;
    int max_queues;
    int i;

    options.backend_type = VHOST_BACKEND_TYPE_USER;
	/* 每个queu都执行vhost_net_init */
    for (i = 0; i < queues; i++) {
        s = DO_UPCAST(VhostUserState, nc, ncs[i]);
        options.net_backend = ncs[i];
        options.opaque      = s->chr;
        options.busyloop_timeout = 0;
        net = vhost_net_init(&options);
        if (i == 0) {
            /* net->dev.max_queues，vhost_net_init中会初始化该值 */
            max_queues = vhost_net_get_max_queues(net);
        }
        s->vhost_net = net;
    }

    return 0;
}

```

#### vhost_net_init

```c
struct vhost_net *vhost_net_init(VhostNetOptions *options)
{
    int r;
	/* 我们是vhost-user */
    bool backend_kernel = options->backend_type == VHOST_BACKEND_TYPE_KERNEL;
    struct vhost_net *net = g_new0(struct vhost_net, 1);/* 分配vhost_net */
    uint64_t features = 0;

    net->nc = options->net_backend;

    net->dev.max_queues = 1;
    net->dev.nvqs = 2;
    net->dev.vqs = net->vqs;

    if (backend_kernel) {
		
    } else {
        net->dev.backend_features = 0;
        net->dev.protocol_features = 0;
        net->backend = -1;

        /* vhost-user needs vq_index to initiate a specific queue pair */
        net->dev.vq_index = net->nc->queue_index * net->dev.nvqs;
    }

    r = vhost_dev_init(&net->dev, options->opaque, options->backend_type, options->busyloop_timeout);

    /* Set sane init value. Override when guest acks. */
    if (net->nc->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER) {
        features = vhost_user_get_acked_features(net->nc);
    }

    vhost_net_ack_features(net, features);

    return net;
}
```

#### vhost_dev_init

```c
int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, uint32_t busyloop_timeout)
{
    uint64_t features;
    int i, r, n_initialized_vqs = 0;

    hdev->vdev = NULL;
    hdev->migration_blocker = NULL;

	/* dev->vhost_ops = &user_ops */
    r = vhost_set_backend_type(hdev, backend_type);
	/* 执行vhost_user_init */
    r = hdev->vhost_ops->vhost_backend_init(hdev, opaque);
 
    r = hdev->vhost_ops->vhost_set_owner(hdev);

    r = hdev->vhost_ops->vhost_get_features(hdev, &features);

    for (i = 0; i < hdev->nvqs; ++i, ++n_initialized_vqs) {
        r = vhost_virtqueue_init(hdev, hdev->vqs + i, hdev->vq_index + i);
    }

    hdev->features = features;

    hdev->memory_listener = (MemoryListener) {
        .begin = vhost_begin,
        .commit = vhost_commit,
        .region_add = vhost_region_add,
        .region_del = vhost_region_del,
        .region_nop = vhost_region_nop,
        .log_start = vhost_log_start,
        .log_stop = vhost_log_stop,
        .log_sync = vhost_log_sync,
        .log_global_start = vhost_log_global_start,
        .log_global_stop = vhost_log_global_stop,
        .eventfd_add = vhost_eventfd_add,
        .eventfd_del = vhost_eventfd_del,
        .priority = 10
    };

    if (hdev->migration_blocker == NULL) {
        if (!(hdev->features & (0x1ULL << VHOST_F_LOG_ALL))) {
            error_setg(&hdev->migration_blocker,
                       "Migration disabled: vhost lacks VHOST_F_LOG_ALL feature.");
        } else if (!qemu_memfd_check()) {
            error_setg(&hdev->migration_blocker,
                       "Migration disabled: failed to allocate shared memory");
        }
    }

    if (hdev->migration_blocker != NULL) {
        migrate_add_blocker(hdev->migration_blocker);
    }

    hdev->mem = g_malloc0(offsetof(struct vhost_memory, regions));
    hdev->n_mem_sections = 0;
    hdev->mem_sections = NULL;
    hdev->log = NULL;
    hdev->log_size = 0;
    hdev->log_enabled = false;
    hdev->started = false;
    hdev->memory_changed = false;
    memory_listener_register(&hdev->memory_listener, &address_space_memory);
    QLIST_INSERT_HEAD(&vhost_devices, hdev, entry);
    return 0;

}

```

#### vhost_user_init

```c
static int vhost_user_init(struct vhost_dev *dev, void *opaque)
{
    uint64_t features;
    struct vhost_user *u;
    int err;

    u = g_new0(struct vhost_user, 1);
    u->chr = opaque;
    u->slave_fd = -1;
    u->dev = dev;
    dev->opaque = u;
	/* 从获取dpdk  features */
    err = vhost_user_get_features(dev, &features);
 
    if (virtio_has_feature(features, VHOST_USER_F_PROTOCOL_FEATURES)) {
        dev->backend_features |= 1ULL << VHOST_USER_F_PROTOCOL_FEATURES;
		/* 获取 VHOST_USER_GET_PROTOCOL_FEATURES */
        err = vhost_user_get_u64(dev, VHOST_USER_GET_PROTOCOL_FEATURES, &features);
      
        dev->protocol_features = features & VHOST_USER_PROTOCOL_FEATURE_MASK;
        err = vhost_user_set_protocol_features(dev, dev->protocol_features);
     
        /* query the max queues we support if backend supports Multiple Queue 
		 * 获取最大的queues，并赋值给dev->max_queues
		 */
        if (dev->protocol_features & (1ULL << VHOST_USER_PROTOCOL_F_MQ)) {
            err = vhost_user_get_u64(dev, VHOST_USER_GET_QUEUE_NUM, &dev->max_queues);
        }
    }

    if (dev->migration_blocker == NULL && !virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_LOG_SHMFD)) {
        error_setg(&dev->migration_blocker,
                   "Migration disabled: vhost-user backend lacks "
                   "VHOST_USER_PROTOCOL_F_LOG_SHMFD feature.");
    }

    return 0;
}
```

####  vhost_virtqueue_init

```c
static int vhost_virtqueue_init(struct vhost_dev *dev, struct vhost_virtqueue *vq, int n)
{
	/* 返回n */
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, n);
    struct vhost_vring_file file = {
        .index = vhost_vq_index,
    };
    int r = event_notifier_init(&vq->masked_notifier, 0);
	
    file.fd = event_notifier_get_fd(&vq->masked_notifier);
	/* 调用 vhost_user_set_vring_call */
    r = dev->vhost_ops->vhost_set_vring_call(dev, &file);

    return 0;
}
```

#### vhost_user_set_vring_call

```c
static int vhost_user_set_vring_call(struct vhost_dev *dev, struct vhost_vring_file *file)
{
	/* 向 dpdk 发送 VHOST_USER_SET_VRING_CALL */
    return vhost_set_vring_file(dev, VHOST_USER_SET_VRING_CALL, file);
}
```

#### vhost_set_vring_file

```c
static int vhost_set_vring_file(struct vhost_dev *dev, VhostUserRequest request,
                                struct vhost_vring_file *file)
{
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    size_t fd_num = 0;
    VhostUserMsg msg = {
        .request = request,
        .flags = VHOST_USER_VERSION,
        /* vring idx */
        .payload.u64 = file->index & VHOST_USER_VRING_IDX_MASK,
        .size = sizeof(msg.payload.u64),
    };

    if (ioeventfd_enabled() && file->fd > 0) {
        fds[fd_num++] = file->fd;
    } else {
        msg.payload.u64 |= VHOST_USER_VRING_NOFD_MASK;
    }
	/* 通过socket 发送给dpdk */
    if (vhost_user_write(dev, &msg, fds, fd_num) < 0) {
        return -1;
    }

    return 0;
}
```

## dpdk中对VHOST_USER_SET_VRING_CALL的处理

vhost_user_msg_handler-->vhost_user_check_and_alloc_queue_pair

### vhost_user_check_and_alloc_queue_pair

```c
static int vhost_user_check_and_alloc_queue_pair(struct virtio_net *dev, VhostUserMsg *msg)
{
	uint16_t vring_idx;

	switch (msg->request.master) {
	case VHOST_USER_SET_VRING_KICK:
	case VHOST_USER_SET_VRING_CALL:
	case VHOST_USER_SET_VRING_ERR:
		vring_idx = msg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
		break;
	case VHOST_USER_SET_VRING_NUM:
	case VHOST_USER_SET_VRING_BASE:
	case VHOST_USER_SET_VRING_ENABLE:
		vring_idx = msg->payload.state.index;
		break;
	case VHOST_USER_SET_VRING_ADDR:
		vring_idx = msg->payload.addr.index;
		break;
	default:
		return 0;
	}
	/* 如果该virtqueue已经处理 */
	if (dev->virtqueue[vring_idx])
		return 0;

	return alloc_vring_queue(dev, vring_idx);
}

```

### alloc_vring_queue

```c
int
alloc_vring_queue(struct virtio_net *dev, uint32_t vring_idx)
{
	struct vhost_virtqueue *vq;
	vq = rte_malloc(NULL, sizeof(struct vhost_virtqueue), 0);/* 分配 vhost_virtqueue内存 */
	dev->virtqueue[vring_idx] = vq;
	init_vring_queue(dev, vring_idx);
	/* nr_vring加1 */
	dev->nr_vring += 1;

	return 0;
}
```

