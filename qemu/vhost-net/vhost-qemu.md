# qemu vhost-net

qemu参数

```c
/**
 * -netdev tap,fds=80:81:82:83:84:85:86:87,id=hostnet2,vhost=on,vhostfds=88:89:90:91:92:93:94:95 
 * -device virtio-net-pci,event_idx=on,mq=on,vectors=18,rx_queue_size=1024,tx_queue_size=1024,
 * netdev=hostnet2,id=net2,mac=20:90:6f:28:84:f2,bus=pci.0,addr=0x7 
 * tap表示type
 * qemu代码中是device先执行
 */
```

main-->net_init_clients-->net_init_netdev-->net_client_init

## net_client_init

```c
int net_client_init(QemuOpts *opts, int is_netdev, Error **errp)
{
    void *object = NULL;
    Error *err = NULL;
    int ret = -1;
    OptsVisitor *ov = opts_visitor_new(opts);
    Visitor *v = opts_get_visitor(ov);
	/* 解析netdev参数 */
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

visit_type_Netdev-->visit_type_Netdev_members

```c
void visit_type_Netdev_members(Visitor *v, Netdev *obj, Error **errp)
{
    Error *err = NULL;
    visit_type_str(v, "id", &obj->id, &err);	/* 解析id */
    visit_type_NetClientOptions(v, "opts", &obj->opts, &err); /* 解析参数 */
}
```

visit_type_NetClientOptions-->visit_type_NetClientOptions_members

```c
void visit_type_NetClientOptions_members(Visitor *v, NetClientOptions *obj, Error **errp)
{
    Error *err = NULL;
	/* 将type的值保存到obj->type中 */
    visit_type_NetClientOptionsKind(v, "type", &obj->type, &err);

    switch (obj->type) {
	case NET_CLIENT_OPTIONS_KIND_VHOST_USER:
        visit_type_q_obj_NetdevVhostUserOptions_wrapper_members(v, &obj->u.vhost_user, &err);
	 case NET_CLIENT_OPTIONS_KIND_TAP:
        visit_type_q_obj_NetdevTapOptions_wrapper_members(v, &obj->u.tap, &err);
        break;
        break;
    }
}
```

visit_type_q_obj_NetdevTapOptions_wrapper_members-->visit_type_NetdevTapOptions-->visit_type_NetdevTapOptions_members

```c
void visit_type_NetdevTapOptions_members(Visitor *v, NetdevTapOptions *obj, Error **errp)
{
    Error *err = NULL;

    if (visit_optional(v, "fds", &obj->has_fds)) {
        visit_type_str(v, "fds", &obj->fds, &err);
    }
    
    if (visit_optional(v, "vhost", &obj->has_vhost)) {
        visit_type_bool(v, "vhost", &obj->vhost, &err);
    }
    
    if (visit_optional(v, "vhostfds", &obj->has_vhostfds)) {
        visit_type_str(v, "vhostfds", &obj->vhostfds, &err);
    }
 
    if (visit_optional(v, "queues", &obj->has_queues)) {
        visit_type_uint32(v, "queues", &obj->queues, &err);
    }
}
```

## net_client_init1

net_client_init1-->net_init_tap

```c
int net_init_tap(const NetClientOptions *opts, const char *name,
                 NetClientState *peer, Error **errp)
{
    const NetdevTapOptions *tap;
    int fd, vnet_hdr = 0, i = 0, queues;
    /* for the no-fd, no-helper case */
    const char *script = NULL; /* suppress wrong "uninit'd use" gcc warning */
    const char *downscript = NULL;
    Error *err = NULL;
    const char *vhostfdname;
    char ifname[128];

    tap = opts->u.tap.data;
    queues = tap->has_queues ? tap->queues : 1;
    vhostfdname = tap->has_vhostfd ? tap->vhostfd : NULL;

    if (tap->has_fd) {

    } else if (tap->has_fds) {
        char *fds[MAX_TAP_QUEUES];
        char *vhost_fds[MAX_TAP_QUEUES];
        int nfds, nvhosts;


		/* 将tap->fds中以":"进行分割的字符串，复制到fds中 */
        nfds = get_fds(tap->fds, fds, MAX_TAP_QUEUES);
        if (tap->has_vhostfds) {
            nvhosts = get_fds(tap->vhostfds, vhost_fds, MAX_TAP_QUEUES);
        }

        for (i = 0; i < nfds; i++) {
            /* 字符串fd变为int型 */
            fd = monitor_fd_param(cur_mon, fds[i], &err);
   
            fcntl(fd, F_SETFL, O_NONBLOCK);

            if (i == 0) {
                vnet_hdr = tap_probe_vnet_hdr(fd);
            } else if (vnet_hdr != tap_probe_vnet_hdr(fd)) {
 
            }

			/* 对每一个nfd调用  net_init_tap_one     */
            net_init_tap_one(tap, peer, "tap", name, ifname, script, downscript,
                             tap->has_vhostfds ? vhost_fds[i] : NULL, vnet_hdr, fd, &err);
        }
    } else if (tap->has_helper)  else {}

    return 0;
}
```

### net_init_tap_one

```c
#define MAX_TAP_QUEUES 1024

static void net_init_tap_one(const NetdevTapOptions *tap, NetClientState *peer,
                             const char *model, const char *name,
                             const char *ifname, const char *script,
                             const char *downscript, const char *vhostfdname,
                             int vnet_hdr, int fd, Error **errp)
{
    Error *err = NULL;
    /* 主要是将fd赋值给s->fd = fd */
    TAPState *s = net_tap_fd_init(peer, model, name, fd, vnet_hdr);
    int vhostfd;

    tap_set_sndbuf(s->fd, tap, &err);
  

    if (tap->has_fd || tap->has_fds) {
        snprintf(s->nc.info_str, sizeof(s->nc.info_str), "fd=%d", fd);
    } 

    if (tap->has_vhost ? tap->vhost :
        vhostfdname || (tap->has_vhostforce && tap->vhostforce)) {
        VhostNetOptions options;

        options.backend_type = VHOST_BACKEND_TYPE_KERNEL;
        options.net_backend = &s->nc;
        if (tap->has_poll_us) {
            options.busyloop_timeout = tap->poll_us;
        } else {
            options.busyloop_timeout = 0;
        }

        if (vhostfdname) { /* libvirt已经创建好，传递过来 */
            vhostfd = monitor_fd_param(cur_mon, vhostfdname, &err);
        } else {
            vhostfd = open("/dev/vhost-net", O_RDWR);
        }
        options.opaque = (void *)(uintptr_t)vhostfd;

        s->vhost_net = vhost_net_init(&options);
        
    } 
}
```

### vhost_net_init

```c
struct vhost_net *vhost_net_init(VhostNetOptions *options)
{
    int r;
	/* 我们是vhost-user,   或者vhost-kernel */
    bool backend_kernel = options->backend_type == VHOST_BACKEND_TYPE_KERNEL;
    struct vhost_net *net = g_new0(struct vhost_net, 1);/* 分配vhost_net */
    uint64_t features = 0;

    net->nc = options->net_backend;

    net->dev.max_queues = 1;
    net->dev.nvqs = 2;
    net->dev.vqs = net->vqs;

    if (backend_kernel) {
        /* 获取tun的fd */
		r = vhost_net_get_fd(options->net_backend);
		net->dev.backend_features = qemu_has_vnet_hdr(options->net_backend) ? 0 : (1ULL << VHOST_NET_F_VIRTIO_NET_HDR);
        /* r是tun的fd */
		net->backend = r;
		net->dev.protocol_features = 0;		
    } else {
   
    }

    r = vhost_dev_init(&net->dev, options->opaque,
                       options->backend_type, options->busyloop_timeout);

    /* Set sane init value. Override when guest acks. */
    if (net->nc->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER) {
        features = vhost_user_get_acked_features(net->nc);
    }

    vhost_net_ack_features(net, features);

    return net;
}
```

### vhost_dev_init

```c
int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, uint32_t busyloop_timeout)
{
    uint64_t features;
    int i, r, n_initialized_vqs = 0;

    hdev->vdev = NULL;
    hdev->migration_blocker = NULL;
	/* dev->vhost_ops = &kernel_ops */
    r = vhost_set_backend_type(hdev, backend_type);
	/* dev->opaque = opaque */
    r = hdev->vhost_ops->vhost_backend_init(hdev, opaque);
 
    r = hdev->vhost_ops->vhost_set_owner(hdev);

    r = hdev->vhost_ops->vhost_get_features(hdev, &features);

	/* hdev->nvqs = 2 */
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

### vhost_virtqueue_init

```c
static int vhost_virtqueue_init(struct vhost_dev *dev, struct vhost_virtqueue *vq, int n)
{
	/* 返回idx - dev->vq_index */
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, n);
    struct vhost_vring_file file = {
        .index = vhost_vq_index,
    };
    int r = event_notifier_init(&vq->masked_notifier, 0);

    file.fd = event_notifier_get_fd(&vq->masked_notifier);

    r = dev->vhost_ops->vhost_set_vring_call(dev, &file);

    return 0;
}
```

## virtio_net_set_status

virtio_pci_common_write-->virtio_set_status-->virtio_net_set_status-->virtio_net_vhost_status

```c
static void virtio_net_vhost_status(VirtIONet *n, uint8_t status)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    NetClientState *nc = qemu_get_queue(n->nic);
  
    int queues = n->multiqueue ? n->max_queues : 1;

    if (!n->vhost_started) {
        int r, i;

        /* Any packets outstanding? Purge them to avoid touching rings
         * when vhost is running.
         */
        for (i = 0;  i < queues; i++) {
            NetClientState *qnc = qemu_get_subqueue(n->nic, i);
            /* Purge both directions: TX and RX. */
            qemu_net_queue_purge(qnc->peer->incoming_queue, qnc);
            qemu_net_queue_purge(qnc->incoming_queue, qnc->peer);
        }

        n->vhost_started = 1;
        r = vhost_net_start(vdev, n->nic->ncs, queues);
 
    } else {
        vhost_net_stop(vdev, n->nic->ncs, queues);
        n->vhost_started = 0;
    }
}

```

### vhost_net_start

```c
int vhost_net_start(VirtIODevice *dev, NetClientState *ncs, int total_queues)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(vbus);
    int r, e, i;

    for (i = 0; i < total_queues; i++) {
        struct vhost_net *net;

        net = get_vhost_net(ncs[i].peer);
        /* net->dev.vq_index = vq_index 
         * tx / rx 各一个,所以乘以2
         */
        vhost_net_set_vq_index(net, i * 2);

        /* Suppress the masking guest notifiers on vhost user
         * because vhost user doesn't interrupt masking/unmasking
         * properly.
         */
        if (net->nc->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER) {
                dev->use_guest_notifier_mask = false;
        }
     }

    r = k->set_guest_notifiers(qbus->parent, total_queues * 2, true);

    for (i = 0; i < total_queues; i++) {
        r = vhost_net_start_one(get_vhost_net(ncs[i].peer), dev);

        if (ncs[i].peer->vring_enable) {
            /* restore vring enable state */
            r = vhost_set_vring_enable(ncs[i].peer, ncs[i].peer->vring_enable);

        }
    }
    return 0;
}
```

### vhost_net_start_one

```c
static int vhost_net_start_one(struct vhost_net *net, VirtIODevice *dev)
{
    struct vhost_vring_file file = { };
    int r;

    net->dev.nvqs = 2;
    net->dev.vqs = net->vqs;

    r = vhost_dev_enable_notifiers(&net->dev, dev);

    r = vhost_dev_start(&net->dev, dev);

	/* tap_poll, 为NULL */
    if (net->nc->info->poll) {
        net->nc->info->poll(net->nc, false);
    }

    if (net->nc->info->type == NET_CLIENT_OPTIONS_KIND_TAP) {
        qemu_set_fd_handler(net->backend, NULL, NULL, NULL);
		/* net->backend即tun的fd */
        file.fd = net->backend;
        for (file.index = 0; file.index < net->dev.nvqs; ++file.index) {
			/* 每个vq对应一个tun fd */
            r = vhost_net_set_backend(&net->dev, &file);
        }
    }
    return 0;
}

```

### vhost_dev_start

```c
/* Host notifiers must be enabled at this point. */
int vhost_dev_start(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    int i, r;
    hdev->started = true;
    hdev->vdev = vdev;

    r = vhost_dev_set_features(hdev, hdev->log_enabled);
 
    r = hdev->vhost_ops->vhost_set_mem_table(hdev, hdev->mem);

    for (i = 0; i < hdev->nvqs; ++i) {
        /* hdev->vhost_ops->vhost_net_set_backend */
        r = vhost_virtqueue_start(hdev, vdev, hdev->vqs + i, hdev->vq_index + i);
    }

    return 0;
}

```

### vhost_virtqueue_start

```c
tatic int vhost_virtqueue_start(struct vhost_dev *dev, struct VirtIODevice *vdev,
                                struct vhost_virtqueue *vq, unsigned idx)
{
    hwaddr s, l, a;
    int r;
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, idx);
    struct vhost_vring_file file = {
        .index = vhost_vq_index
    };
    struct vhost_vring_state state = {
        .index = vhost_vq_index
    };
    struct VirtQueue *vvq = virtio_get_queue(vdev, idx);
	/* vdev->vq[n].vring.desc;子机获取vring的gpa后会写到pci的寄存器中 */
    a = virtio_queue_get_desc_addr(vdev, idx);

    vq->num = state.num = virtio_queue_get_num(vdev, idx);
    r = dev->vhost_ops->vhost_set_vring_num(dev, &state);

    state.num = virtio_queue_get_last_avail_idx(vdev, idx);
    r = dev->vhost_ops->vhost_set_vring_base(dev, &state);

    s = l = virtio_queue_get_desc_size(vdev, idx);
    a = virtio_queue_get_desc_addr(vdev, idx);
    /* 把物理地址映射成host的虚拟地址 */
    vq->desc = cpu_physical_memory_map(a, &l, 0);

    s = l = virtio_queue_get_avail_size(vdev, idx);
    a = virtio_queue_get_avail_addr(vdev, idx);
    vq->avail = cpu_physical_memory_map(a, &l, 0);

    vq->used_size = s = l = virtio_queue_get_used_size(vdev, idx);
    vq->used_phys = a = virtio_queue_get_used_addr(vdev, idx);
    vq->used = cpu_physical_memory_map(a, &l, 1);
 

    vq->ring_size = s = l = virtio_queue_get_ring_size(vdev, idx);
    vq->ring_phys = a = virtio_queue_get_ring_addr(vdev, idx);
    vq->ring = cpu_physical_memory_map(a, &l, 1);


    r = vhost_virtqueue_set_addr(dev, vq, vhost_vq_index, dev->log_enabled);


    file.fd = event_notifier_get_fd(virtio_queue_get_host_notifier(vvq));
    r = dev->vhost_ops->vhost_set_vring_kick(dev, &file);
 
    /* Clear and discard previous events if any. */
    event_notifier_test_and_clear(&vq->masked_notifier);

    /* Init vring in unmasked state, unless guest_notifier_mask
     * will do it later.
     */
    if (!vdev->use_guest_notifier_mask) {
        /* TODO: check and handle errors. */
        vhost_virtqueue_mask(dev, vdev, idx, false);
    }

    return 0;
}

```

### vhost_virtqueue_set_addr

```c
static int vhost_virtqueue_set_addr(struct vhost_dev *dev, struct vhost_virtqueue *vq,
                                    unsigned idx, bool enable_log)
{
    struct vhost_vring_addr addr = {
        .index = idx,
        .desc_user_addr = (uint64_t)(unsigned long)vq->desc,
        .avail_user_addr = (uint64_t)(unsigned long)vq->avail,
        .used_user_addr = (uint64_t)(unsigned long)vq->used,
        .log_guest_addr = vq->used_phys,
        .flags = enable_log ? (1 << VHOST_VRING_F_LOG) : 0,
    };
    int r = dev->vhost_ops->vhost_set_vring_addr(dev, &addr);
    return 0;
}
```



## netdev和device联系起来

## virtio_net_class_init

```c
static void virtio_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
	/* 重新设置 dc->props */
    dc->props = virtio_net_properties;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    vdc->realize = virtio_net_device_realize;
    vdc->unrealize = virtio_net_device_unrealize;
    vdc->get_config = virtio_net_get_config;
    vdc->set_config = virtio_net_set_config;
    vdc->get_features = virtio_net_get_features;
    vdc->set_features = virtio_net_set_features;
    vdc->bad_features = virtio_net_bad_features;
    vdc->reset = virtio_net_reset;
    vdc->set_status = virtio_net_set_status;
    vdc->guest_notifier_mask = virtio_net_guest_notifier_mask;
    vdc->guest_notifier_pending = virtio_net_guest_notifier_pending;
    vdc->load = virtio_net_load_device;
    vdc->save = virtio_net_save_device;
}
```

### virtio_net_properties

```c
static Property virtio_net_properties[] = {
    DEFINE_PROP_BIT("csum", VirtIONet, host_features, VIRTIO_NET_F_CSUM, true),
    DEFINE_PROP_BIT("guest_csum", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_CSUM, true),
    DEFINE_NIC_PROPERTIES(VirtIONet, nic_conf),
    DEFINE_PROP_END_OF_LIST(),
};

```

### DEFINE_NIC_PROPERTIES

```c
PropertyInfo qdev_prop_netdev = {
    .name  = "str",
    .description = "ID of a netdev to use as a backend",
    .get   = get_netdev,
    .set   = set_netdev,
};

#define DEFINE_PROP(_name, _state, _field, _prop, _type) { \
			.name	   = (_name),									 \
			.info	   = &(_prop),									 \
			.offset    = offsetof(_state, _field)					 \
				+ type_check(_type, typeof_field(_state, _field)),	 \
			}

#define DEFINE_PROP_NETDEV(_n, _s, _f)             \
		DEFINE_PROP(_n, _s, _f, qdev_prop_netdev, NICPeers)

#define DEFINE_NIC_PROPERTIES(_state, _conf)                            \
    DEFINE_PROP_MACADDR("mac",   _state, _conf.macaddr),                \
    DEFINE_PROP_VLAN("vlan",     _state, _conf.peers),                   \
    DEFINE_PROP_NETDEV("netdev", _state, _conf.peers)
	/* netdev属性用于将virtio-net和netdev联系起来 */
```

会调用set_netdev然后将device和netdev联系起来