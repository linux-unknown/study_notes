struct Netdev {
    char *id;
    NetClientOptions *opts;
};

struct NetClientOptions {
    NetClientOptionsKind type;
    union { /* union tag is @type */
        q_obj_NetdevNoneOptions_wrapper none;
        q_obj_NetLegacyNicOptions_wrapper nic;
        q_obj_NetdevUserOptions_wrapper user;
        q_obj_NetdevTapOptions_wrapper tap;
        q_obj_NetdevL2TPv3Options_wrapper l2tpv3;
        q_obj_NetdevSocketOptions_wrapper socket;
        q_obj_NetdevVdeOptions_wrapper vde;
        q_obj_NetdevDumpOptions_wrapper dump;
        q_obj_NetdevBridgeOptions_wrapper bridge;
        q_obj_NetdevHubPortOptions_wrapper hubport;
        q_obj_NetdevNetmapOptions_wrapper netmap;
        q_obj_NetdevVhostUserOptions_wrapper vhost_user;
    } u;
};

struct q_obj_NetdevVhostUserOptions_wrapper {
    NetdevVhostUserOptions *data;
};

struct NetdevVhostUserOptions {
    char *chardev;
    bool has_vhostforce;
    bool vhostforce;
    bool has_queues;
    int64_t queues;
    char *ifname;
};

typedef enum NetClientOptionsKind {
    NET_CLIENT_OPTIONS_KIND_NONE = 0,
    NET_CLIENT_OPTIONS_KIND_NIC = 1,
    NET_CLIENT_OPTIONS_KIND_USER = 2,
    NET_CLIENT_OPTIONS_KIND_TAP = 3,
    NET_CLIENT_OPTIONS_KIND_L2TPV3 = 4,
    NET_CLIENT_OPTIONS_KIND_SOCKET = 5,
    NET_CLIENT_OPTIONS_KIND_VDE = 6,
    NET_CLIENT_OPTIONS_KIND_DUMP = 7,
    NET_CLIENT_OPTIONS_KIND_BRIDGE = 8,
    NET_CLIENT_OPTIONS_KIND_HUBPORT = 9,
    NET_CLIENT_OPTIONS_KIND_NETMAP = 10,
    NET_CLIENT_OPTIONS_KIND_VHOST_USER = 11,
    NET_CLIENT_OPTIONS_KIND__MAX = 12,
} NetClientOptionsKind;


struct NetdevTapOptions {
    bool has_ifname;
    char *ifname;
    bool has_fd;
    char *fd;
    bool has_fds;
    char *fds;
    bool has_script;
    char *script;
    bool has_downscript;
    char *downscript;
    bool has_helper;
    char *helper;
    bool has_sndbuf;
    uint64_t sndbuf;
    bool has_vnet_hdr;
    bool vnet_hdr;
    bool has_vhost;
    bool vhost;
    bool has_vhostfd;
    char *vhostfd;
    bool has_vhostfds;
    char *vhostfds;
    bool has_vhostforce;
    bool vhostforce;
    bool has_queues;
    uint32_t queues;
    bool has_poll_us;
    uint32_t poll_us;
};


/**
 * -chardev socket,id=charnet0,path=/var/run/vhost_sock 
 * -netdev vhost-user,chardev=charnet0,queues=4,id=hostnet0,ifname=veth_A167681a 
 * -device virtio-net-pci,event_idx=on,mq=on,vectors=10,rx_queue_size=1024,tx_queue_size=1024,netdev=hostnet0,id=net0,mac=52:54:00:a1:67:68,bus=pci.0,addr=0x5
 * chardev 隐式参数为backend=socket
 * netdev 隐式参数为type=vhost-user
 * device 隐式参数为driver=virtio-net-pci
 */

int main(int argc, char **argv, char **envp)
{
	qemu_add_opts(&qemu_netdev_opts);
	if (net_init_clients() < 0) {
        exit(1);
    }

}

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

    net_change_state_entry =
        qemu_add_vm_change_state_handler(net_vm_change_state_handler, NULL);

    QTAILQ_INIT(&net_clients);

    if (qemu_opts_foreach(qemu_find_opts("netdev"),
                          net_init_netdev, NULL, NULL)) {
        return -1;
    }

    if (qemu_opts_foreach(net, net_init_client, NULL, NULL)) {
        return -1;
    }

    return 0;
}

static int net_init_netdev(void *dummy, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    ret = net_client_init(opts, 1, &local_err);

    return ret;
}

int net_client_init(QemuOpts *opts, int is_netdev, Error **errp)
{
    void *object = NULL;
    Error *err = NULL;
    int ret = -1;
    OptsVisitor *ov = opts_visitor_new(opts);
    Visitor *v = opts_get_visitor(ov);


    if (is_netdev) {
        visit_type_Netdev(v, NULL, (Netdev **)&object, &err);
    }

    if (!err) {
        ret = net_client_init1(object, is_netdev, &err);
    }

    if (is_netdev) {
        qapi_free_Netdev(object);
    } else {
        qapi_free_NetLegacy(object);
    }

    error_propagate(errp, err);
    opts_visitor_cleanup(ov);
    return ret;
}

OptsVisitor *
opts_visitor_new(const QemuOpts *opts)
{
    OptsVisitor *ov;

    ov = g_malloc0(sizeof *ov);

    ov->visitor.start_struct = &opts_start_struct;
    ov->visitor.end_struct   = &opts_end_struct;

    ov->visitor.start_list = &opts_start_list;
    ov->visitor.next_list  = &opts_next_list;
    ov->visitor.end_list   = &opts_end_list;

    /* input_type_enum() covers both "normal" enums and union discriminators.
     * The union discriminator field is always generated as "type"; it should
     * match the "type" QemuOpt child of any QemuOpts.
     *
     * input_type_enum() will remove the looked-up key from the
     * "unprocessed_opts" hash even if the lookup fails, because the removal is
     * done earlier in opts_type_str(). This should be harmless.
     */
    ov->visitor.type_enum = &input_type_enum;

    ov->visitor.type_int64  = &opts_type_int64;
    ov->visitor.type_uint64 = &opts_type_uint64;
    ov->visitor.type_size   = &opts_type_size;
    ov->visitor.type_bool   = &opts_type_bool;
    ov->visitor.type_str    = &opts_type_str;

    /* type_number() is not filled in, but this is not the first visitor to
     * skip some mandatory methods... */

    ov->visitor.optional = &opts_optional;

    ov->opts_root = opts;

    return ov;
}

static void
opts_start_struct(Visitor *v, const char *name, void **obj,
                  size_t size, Error **errp)
{
    OptsVisitor *ov = to_ov(v);
    const QemuOpt *opt;

    if (obj) {
        *obj = g_malloc0(size > 0 ? size : 1);
    }
    if (ov->depth++ > 0) {
        return;
    }

    ov->unprocessed_opts = g_hash_table_new_full(&g_str_hash, &g_str_equal,
                                                 NULL, &destroy_list);
    QTAILQ_FOREACH(opt, &ov->opts_root->head, next) {
        /* ensured by qemu-option.c::opts_do_parse() */
        assert(strcmp(opt->name, "id") != 0);

        opts_visitor_insert(ov->unprocessed_opts, opt);
    }

    if (ov->opts_root->id != NULL) {
        ov->fake_id_opt = g_malloc0(sizeof *ov->fake_id_opt);

        ov->fake_id_opt->name = g_strdup("id");
        ov->fake_id_opt->str = g_strdup(ov->opts_root->id);
        opts_visitor_insert(ov->unprocessed_opts, ov->fake_id_opt);
    }
}

void input_type_enum(Visitor *v, const char *name, int *obj,
                     const char *const strings[], Error **errp)
{
    Error *local_err = NULL;
    int64_t value = 0;
    char *enum_str;



    visit_type_str(v, name, &enum_str, &local_err);

    while (strings[value] != NULL) {
        if (strcmp(strings[value], enum_str) == 0) {
            break;
        }
        value++;
    }

    g_free(enum_str);
    *obj = value;
}


/* qapi-visit.c 编译生成的 */
void visit_type_Netdev(Visitor *v, const char *name, Netdev **obj, Error **errp)
{
    Error *err = NULL;

    visit_start_struct(v, name, (void **)obj, sizeof(Netdev), &err);

    visit_type_Netdev_members(v, *obj, &err);
}

void visit_start_struct(Visitor *v, const char *name, void **obj,
                        size_t size, Error **errp)
{
    v->start_struct(v, name, obj, size, errp);
}

void visit_type_Netdev_members(Visitor *v, Netdev *obj, Error **errp)
{
    Error *err = NULL;

    visit_type_str(v, "id", &obj->id, &err);
    visit_type_NetClientOptions(v, "opts", &obj->opts, &err); 
}

void visit_type_str(Visitor *v, const char *name, char **obj, Error **errp)
{
    v->type_str(v, name, obj, errp);
}


static void opts_type_str(Visitor *v, const char *name, char **obj, Error **errp)
{
    OptsVisitor *ov = to_ov(v);
    const QemuOpt *opt;

    opt = lookup_scalar(ov, name, errp);

    *obj = g_strdup(opt->str ? opt->str : "");
    processed(ov, name);
}

void visit_type_NetClientOptions(Visitor *v, const char *name, NetClientOptions **obj, Error **errp)
{
    Error *err = NULL;

    visit_start_struct(v, name, (void **)obj, sizeof(NetClientOptions), &err);

    visit_type_NetClientOptions_members(v, *obj, &err);
    error_propagate(errp, err);
}

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
	 case NET_CLIENT_OPTIONS_KIND_TAP:
        visit_type_q_obj_NetdevTapOptions_wrapper_members(v, &obj->u.tap, &err);
        break;
        break;
    }
}

const char *const NetClientOptionsKind_lookup[] = {
    [NET_CLIENT_OPTIONS_KIND_NONE] = "none",
    [NET_CLIENT_OPTIONS_KIND_NIC] = "nic",
    [NET_CLIENT_OPTIONS_KIND_USER] = "user",
    [NET_CLIENT_OPTIONS_KIND_TAP] = "tap",
    [NET_CLIENT_OPTIONS_KIND_L2TPV3] = "l2tpv3",
    [NET_CLIENT_OPTIONS_KIND_SOCKET] = "socket",
    [NET_CLIENT_OPTIONS_KIND_VDE] = "vde",
    [NET_CLIENT_OPTIONS_KIND_DUMP] = "dump",
    [NET_CLIENT_OPTIONS_KIND_BRIDGE] = "bridge",
    [NET_CLIENT_OPTIONS_KIND_HUBPORT] = "hubport",
    [NET_CLIENT_OPTIONS_KIND_NETMAP] = "netmap",
    [NET_CLIENT_OPTIONS_KIND_VHOST_USER] = "vhost-user",
    [NET_CLIENT_OPTIONS_KIND__MAX] = NULL,
};

void visit_type_NetClientOptionsKind(Visitor *v, const char *name, NetClientOptionsKind *obj, Error **errp)
{
    int value = *obj;
    visit_type_enum(v, name, &value, NetClientOptionsKind_lookup, errp);
    *obj = value;
}

void visit_type_enum(Visitor *v, const char *name, int *obj,
                     const char *const strings[], Error **errp)
{
    v->type_enum(v, name, obj, strings, errp);
}

void visit_type_q_obj_NetdevVhostUserOptions_wrapper_members(Visitor *v, q_obj_NetdevVhostUserOptions_wrapper *obj, Error **errp)
{
    Error *err = NULL;

    visit_type_NetdevVhostUserOptions(v, "data", &obj->data, &err);

out:
    error_propagate(errp, err);
}

void visit_type_NetdevVhostUserOptions(Visitor *v, const char *name, NetdevVhostUserOptions **obj, Error **errp)
{
    Error *err = NULL;

    visit_start_struct(v, name, (void **)obj, sizeof(NetdevVhostUserOptions), &err);

    visit_type_NetdevVhostUserOptions_members(v, *obj, &err);
    error_propagate(errp, err);
    err = NULL;
}


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

static int (* const net_client_init_fun[NET_CLIENT_OPTIONS_KIND__MAX])(
    const NetClientOptions *opts,
    const char *name,
    NetClientState *peer, Error **errp) = {
        [NET_CLIENT_OPTIONS_KIND_NIC]       = net_init_nic,
#ifdef CONFIG_SLIRP
        [NET_CLIENT_OPTIONS_KIND_USER]      = net_init_slirp,
#endif
        [NET_CLIENT_OPTIONS_KIND_TAP]       = net_init_tap,
        [NET_CLIENT_OPTIONS_KIND_SOCKET]    = net_init_socket,
#ifdef CONFIG_VDE
        [NET_CLIENT_OPTIONS_KIND_VDE]       = net_init_vde,
#endif
#ifdef CONFIG_NETMAP
        [NET_CLIENT_OPTIONS_KIND_NETMAP]    = net_init_netmap,
#endif
        [NET_CLIENT_OPTIONS_KIND_DUMP]      = net_init_dump,
#ifdef CONFIG_NET_BRIDGE
        [NET_CLIENT_OPTIONS_KIND_BRIDGE]    = net_init_bridge,
#endif
        [NET_CLIENT_OPTIONS_KIND_HUBPORT]   = net_init_hubport,
#ifdef CONFIG_VHOST_NET_USED
        [NET_CLIENT_OPTIONS_KIND_VHOST_USER] = net_init_vhost_user,
#endif
#ifdef CONFIG_L2TPV3
        [NET_CLIENT_OPTIONS_KIND_L2TPV3]    = net_init_l2tpv3,
#endif
};

static int net_client_init1(const void *object, int is_netdev, Error **errp)
{
    const NetClientOptions *opts;
    const char *name;
    NetClientState *peer = NULL;

    if (is_netdev) {
        const Netdev *netdev = object;
        opts = netdev->opts;
        name = netdev->id;
    }
	/* 执行 net_init_vhost_user */
    if (net_client_init_fun[opts->type](opts, name, peer, errp) < 0) {
        /* FIXME drop when all init functions store an Error */
        if (errp && !*errp) {
            error_setg(errp, QERR_DEVICE_INIT_FAILED,
                       NetClientOptionsKind_lookup[opts->type]);
        }
        return -1;
    }
    return 0;
}


typedef struct VhostUserState {
    NetClientState nc;
    CharDriverState *chr;
    VHostNetState *vhost_net;
    int watch;
    uint64_t acked_features;
    bool started;
#define VHOST_USER_IFNAME_MAX_LENGTH 128
    char ifname[VHOST_USER_IFNAME_MAX_LENGTH];
} VhostUserState;


int net_init_vhost_user(const NetClientOptions *opts, const char *name,
                        NetClientState *peer, Error **errp)
{
    int queues;
    const NetdevVhostUserOptions *vhost_user_opts;
    CharDriverState *chr;
    char *ifname;

    vhost_user_opts = opts->u.vhost_user.data;

    chr = net_vhost_claim_chardev(vhost_user_opts, errp);

    /* verify net frontend */
    if (qemu_opts_foreach(qemu_find_opts("device"), net_vhost_check_net,
                          (char *)name, errp)) {
        return -1;
    }

    queues = vhost_user_opts->has_queues ? vhost_user_opts->queues : 1;
 
    ifname = vhost_user_opts->ifname;

    return net_vhost_user_init(peer, "vhost_user", name, chr, queues, ifname);
}

static NetClientInfo net_vhost_user_info = {
        .type = NET_CLIENT_OPTIONS_KIND_VHOST_USER,
        .size = sizeof(VhostUserState),
        .receive = vhost_user_receive,
        .cleanup = vhost_user_cleanup,
        .has_vnet_hdr = vhost_user_has_vnet_hdr,
        .has_ufo = vhost_user_has_ufo,
};


static int net_vhost_user_init(NetClientState *peer, const char *device,
                               const char *name, CharDriverState *chr,
                               int queues, char *ifname)
{
    NetClientState *nc, *nc0 = NULL;
    VhostUserState *s;
    int i;


    for (i = 0; i < queues; i++) {
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
        qemu_chr_add_handlers(chr, NULL, NULL,
                              net_vhost_user_event, nc0->name);
    } while (!s->started);

    return 0;
}

NetClientState *qemu_new_net_client(NetClientInfo *info,
                                    NetClientState *peer,
                                    const char *model,
                                    const char *name)
{
    NetClientState *nc;

	/* sizeof(VhostUserState) */
    nc = g_malloc0(info->size);
    qemu_net_client_setup(nc, info, peer, model, name, qemu_net_client_destructor);

    return nc;
}

static void qemu_net_client_setup(NetClientState *nc,
                                  NetClientInfo *info,
                                  NetClientState *peer,
                                  const char *model,
                                  const char *name,
                                  NetClientDestructor *destructor)
{
    nc->info = info;
    nc->model = g_strdup(model);
    if (name) {
        nc->name = g_strdup(name);
    } else {
        nc->name = assign_name(nc, model);
    }

    if (peer) {
        assert(!peer->peer);
        nc->peer = peer;
        peer->peer = nc;
    }
    QTAILQ_INSERT_TAIL(&net_clients, nc, next);

    nc->incoming_queue = qemu_new_net_queue(qemu_deliver_packet_iov, nc);
    nc->destructor = destructor;
    QTAILQ_INIT(&nc->filters);
}

static char *assign_name(NetClientState *nc1, const char *model)
{
    NetClientState *nc;
    int id = 0;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (nc == nc1) {
            continue;
        }
        if (strcmp(nc->model, model) == 0) {
            id++;
        }
    }

    return g_strdup_printf("%s.%d", model, id);
}

int qemu_chr_wait_connected(CharDriverState *chr, Error **errp)
{
    if (chr->chr_wait_connected) {
        return chr->chr_wait_connected(chr, errp);
    }

    return 0;
}

void qemu_chr_add_handlers(CharDriverState *s,
                           IOCanReadHandler *fd_can_read,
                           IOReadHandler *fd_read,
                           IOEventHandler *fd_event,
                           void *opaque)
{
    int fe_open;

    if (!opaque && !fd_can_read && !fd_read && !fd_event) {
        fe_open = 0;
        remove_fd_in_watch(s);
    } else {
        fe_open = 1;
    }
    s->chr_can_read = fd_can_read;
    s->chr_read = fd_read;
    s->chr_event = fd_event;
    s->handler_opaque = opaque;
    if (fe_open && s->chr_update_read_handler)
        s->chr_update_read_handler(s);

    if (!s->explicit_fe_open) {
        qemu_chr_fe_set_open(s, fe_open);
    }

    /* We're connecting to an already opened device, so let's make sure we
       also get the open event */
    if (fe_open && s->be_open) {
        qemu_chr_be_generic_open(s);
    }
}

static void net_vhost_user_event(void *opaque, int event)
{
    const char *name = opaque;
    NetClientState *ncs[MAX_QUEUE_NUM];
    VhostUserState *s;
    Error *err = NULL;
    int queues;

    queues = qemu_find_net_clients_except(name, ncs,
                                          NET_CLIENT_OPTIONS_KIND_NIC,
                                          MAX_QUEUE_NUM);

	/* qemu_find_net_clients_except会给ncs赋值 */
    s = DO_UPCAST(VhostUserState, nc, ncs[0]);
    trace_vhost_user_event(s->chr->label, event);
    switch (event) {
    case CHR_EVENT_OPENED:
        s->watch = qemu_chr_fe_add_watch(s->chr, G_IO_HUP,
                                         net_vhost_user_watch, s);
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

    if (err) {
        error_report_err(err);
    }
}

int qemu_find_net_clients_except(const char *id, NetClientState **ncs,
                                 NetClientOptionsKind type, int max)
{
    NetClientState *nc;
    int ret = 0;

	/* qemu_net_client_setup 会向net_clients添加，每一个queue都会添加进来
	 * nc->info->type是NET_CLIENT_OPTIONS_KIND_VHOST_USER
	 */
    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (nc->info->type == type) {
            continue;
        }
        if (!id || !strcmp(nc->name, id)) {
            if (ret < max) {
                ncs[ret] = nc;/*  给ncs赋值 */
            }
            ret++;
        }
    }

    return ret;
}

typedef enum VhostBackendType {
    VHOST_BACKEND_TYPE_NONE = 0,
    VHOST_BACKEND_TYPE_KERNEL = 1,
    VHOST_BACKEND_TYPE_USER = 2,
    VHOST_BACKEND_TYPE_MAX = 3,
} VhostBackendType;


static int vhost_user_start(int queues, NetClientState *ncs[])
{
    VhostNetOptions options;
    struct vhost_net *net = NULL;
    VhostUserState *s;
    int max_queues;
    int i;

    options.backend_type = VHOST_BACKEND_TYPE_USER;

    for (i = 0; i < queues; i++) {

        s = DO_UPCAST(VhostUserState, nc, ncs[i]);

        options.net_backend = ncs[i];
        options.opaque      = s->chr;
        options.busyloop_timeout = 0;
        net = vhost_net_init(&options);

        if (i == 0) {
			/* net->dev.max_queues */
            max_queues = vhost_net_get_max_queues(net);
        }

        if (s->vhost_net) {
            vhost_net_cleanup(s->vhost_net);
            g_free(s->vhost_net);
        }
        s->vhost_net = net;
    }

    return 0;
}

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
		r = vhost_net_get_fd(options->net_backend);

		net->dev.backend_features = qemu_has_vnet_hdr(options->net_backend) ? 0 : (1ULL << VHOST_NET_F_VIRTIO_NET_HDR);
		net->backend = r;
		net->dev.protocol_features = 0;		
    } else {
        net->dev.backend_features = 0;
        net->dev.protocol_features = 0;
        net->backend = -1;

        /* vhost-user needs vq_index to initiate a specific queue pair 
		 * 每次循环net->dev.vq_index的值为0，2，4，6
		 * 每次循环net->nc->queue_index的值为0，1，2，3
		 */
        net->dev.vq_index = net->nc->queue_index * net->dev.nvqs;
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

int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, uint32_t busyloop_timeout)
{
    uint64_t features;
    int i, r, n_initialized_vqs = 0;

    hdev->vdev = NULL;
    hdev->migration_blocker = NULL;

    r = vhost_set_backend_type(hdev, backend_type);

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

const VhostOps user_ops = {
        .backend_type = VHOST_BACKEND_TYPE_USER,
        .vhost_backend_init = vhost_user_init,
        .vhost_backend_cleanup = vhost_user_cleanup,
        .vhost_backend_memslots_limit = vhost_user_memslots_limit,
        .vhost_set_log_base = vhost_user_set_log_base,
        .vhost_set_mem_table = vhost_user_set_mem_table,
        .vhost_set_vring_addr = vhost_user_set_vring_addr,
        .vhost_set_vring_endian = vhost_user_set_vring_endian,
        .vhost_set_vring_num = vhost_user_set_vring_num,
        .vhost_set_vring_base = vhost_user_set_vring_base,
        .vhost_get_vring_base = vhost_user_get_vring_base,
        .vhost_set_vring_kick = vhost_user_set_vring_kick,
        .vhost_set_vring_call = vhost_user_set_vring_call,
        .vhost_set_features = vhost_user_set_features,
        .vhost_get_features = vhost_user_get_features,
        .vhost_set_owner = vhost_user_set_owner,
        .vhost_reset_device = vhost_user_reset_device,
        .vhost_get_vq_index = vhost_user_get_vq_index,
        .vhost_set_vring_enable = vhost_user_set_vring_enable,
        .vhost_requires_shm_log = vhost_user_requires_shm_log,
        .vhost_migration_done = vhost_user_migration_done,
        .vhost_backend_can_merge = vhost_user_can_merge,
        .vhost_backend_set_ifname = vhost_user_set_ifname,
        .vhost_get_config = vhost_user_get_config,
        .vhost_set_config = vhost_user_set_config,
        .vhost_backend_mem_section_filter = vhost_user_mem_section_filter,
        .vhost_get_inflight_fd = vhost_user_get_inflight_fd,
        .vhost_set_inflight_fd = vhost_user_set_inflight_fd,
        .vhost_live_io_pending = vhost_user_live_io_pending,
        .vhost_live_io_down = vhost_user_live_io_down,
        .vhost_live_io_cleanup = vhost_user_live_io_cleanup,
        .vhost_live_io_final = vhost_user_live_io_final,
};


int vhost_set_backend_type(struct vhost_dev *dev, VhostBackendType backend_type)
{
    int r = 0;

    switch (backend_type) {
    case VHOST_BACKEND_TYPE_KERNEL:
        dev->vhost_ops = &kernel_ops;
        break;
    case VHOST_BACKEND_TYPE_USER:
        dev->vhost_ops = &user_ops;
        break;
    default:
        error_report("Unknown vhost backend type");
        r = -1;
    }

    return r;
}


static int vhost_virtqueue_init(struct vhost_dev *dev,
                                struct vhost_virtqueue *vq, int n)
{
	/* 返回n */
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, n);
    struct vhost_vring_file file = {
        .index = vhost_vq_index,
    };
    int r = event_notifier_init(&vq->masked_notifier, 0);

    file.fd = event_notifier_get_fd(&vq->masked_notifier);

    r = dev->vhost_ops->vhost_set_vring_call(dev, &file);

    return 0;
}

static int vhost_user_set_vring_call(struct vhost_dev *dev,
                                     struct vhost_vring_file *file)
{
	/* 像dpdk 发送    VHOST_USER_SET_VRING_CALL */
    return vhost_set_vring_file(dev, VHOST_USER_SET_VRING_CALL, file);
}

static int vhost_set_vring_file(struct vhost_dev *dev,
                                VhostUserRequest request,
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
	/* 通过socket 发送给dpdk      */
    if (vhost_user_write(dev, &msg, fds, fd_num) < 0) {
        return -1;
    }

    return 0;
}

static int vhost_user_init(struct vhost_dev *dev, void *opaque)
{
    uint64_t features;
    struct vhost_user *u;
    int err;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    u = g_new0(struct vhost_user, 1);
    u->chr = opaque;
    u->slave_fd = -1;
    u->dev = dev;
    dev->opaque = u;

    err = vhost_user_get_features(dev, &features);
 

    if (virtio_has_feature(features, VHOST_USER_F_PROTOCOL_FEATURES)) {
        dev->backend_features |= 1ULL << VHOST_USER_F_PROTOCOL_FEATURES;

        err = vhost_user_get_u64(dev, VHOST_USER_GET_PROTOCOL_FEATURES, &features);
      
        dev->protocol_features = features & VHOST_USER_PROTOCOL_FEATURE_MASK;
        err = vhost_user_set_protocol_features(dev, dev->protocol_features);
     
        /* query the max queues we support if backend supports Multiple Queue 
		 * 获取最带的queues，并赋值给dev->max_queues
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


void visit_type_q_obj_NetdevTapOptions_wrapper_members(Visitor *v, q_obj_NetdevTapOptions_wrapper *obj, Error **errp)
{
    Error *err = NULL;

    visit_type_NetdevTapOptions(v, "data", &obj->data, &err);
}

void visit_type_NetdevTapOptions(Visitor *v, const char *name, NetdevTapOptions **obj, Error **errp)
{
    Error *err = NULL;

    visit_start_struct(v, name, (void **)obj, sizeof(NetdevTapOptions), &err);
    visit_type_NetdevTapOptions_members(v, *obj, &err);
}

void visit_type_NetdevTapOptions_members(Visitor *v, NetdevTapOptions *obj, Error **errp)
{
    Error *err = NULL;

    if (visit_optional(v, "ifname", &obj->has_ifname)) {
        visit_type_str(v, "ifname", &obj->ifname, &err);
    }
    if (visit_optional(v, "fd", &obj->has_fd)) {
        visit_type_str(v, "fd", &obj->fd, &err);
    }
    if (visit_optional(v, "fds", &obj->has_fds)) {
        visit_type_str(v, "fds", &obj->fds, &err);
    }
    if (visit_optional(v, "script", &obj->has_script)) {
        visit_type_str(v, "script", &obj->script, &err);
    }
    if (visit_optional(v, "downscript", &obj->has_downscript)) {
        visit_type_str(v, "downscript", &obj->downscript, &err);
    }
    if (visit_optional(v, "helper", &obj->has_helper)) {
        visit_type_str(v, "helper", &obj->helper, &err);
    }
    if (visit_optional(v, "sndbuf", &obj->has_sndbuf)) {
        visit_type_size(v, "sndbuf", &obj->sndbuf, &err);
    }
    if (visit_optional(v, "vnet_hdr", &obj->has_vnet_hdr)) {
        visit_type_bool(v, "vnet_hdr", &obj->vnet_hdr, &err);
    }
    if (visit_optional(v, "vhost", &obj->has_vhost)) {
        visit_type_bool(v, "vhost", &obj->vhost, &err);
    }
    if (visit_optional(v, "vhostfd", &obj->has_vhostfd)) {
        visit_type_str(v, "vhostfd", &obj->vhostfd, &err);
    }
    if (visit_optional(v, "vhostfds", &obj->has_vhostfds)) {
        visit_type_str(v, "vhostfds", &obj->vhostfds, &err);
    }
    if (visit_optional(v, "vhostforce", &obj->has_vhostforce)) {
        visit_type_bool(v, "vhostforce", &obj->vhostforce, &err);
    }
    if (visit_optional(v, "queues", &obj->has_queues)) {
        visit_type_uint32(v, "queues", &obj->queues, &err);
    }
    if (visit_optional(v, "poll-us", &obj->has_poll_us)) {
        visit_type_uint32(v, "poll-us", &obj->poll_us, &err);
    }

}

static int vhost_net_get_fd(NetClientState *backend)
{
    switch (backend->info->type) {
    case NET_CLIENT_OPTIONS_KIND_TAP:
        return tap_get_fd(backend);
    default:
        fprintf(stderr, "vhost-net requires tap backend\n");
        return -EBADFD;
    }
}


int tap_get_fd(NetClientState *nc)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    return s->fd;
}

static const VhostOps kernel_ops = {
        .backend_type = VHOST_BACKEND_TYPE_KERNEL,
        .vhost_backend_init = vhost_kernel_init,
        .vhost_backend_cleanup = vhost_kernel_cleanup,
        .vhost_backend_memslots_limit = vhost_kernel_memslots_limit,
        .vhost_net_set_backend = vhost_kernel_net_set_backend,
        .vhost_scsi_set_endpoint = vhost_kernel_scsi_set_endpoint,
        .vhost_scsi_clear_endpoint = vhost_kernel_scsi_clear_endpoint,
        .vhost_scsi_get_abi_version = vhost_kernel_scsi_get_abi_version,
        .vhost_set_log_base = vhost_kernel_set_log_base,
        .vhost_set_mem_table = vhost_kernel_set_mem_table,
        .vhost_set_vring_addr = vhost_kernel_set_vring_addr,
        .vhost_set_vring_endian = vhost_kernel_set_vring_endian,
        .vhost_set_vring_num = vhost_kernel_set_vring_num,
        .vhost_set_vring_base = vhost_kernel_set_vring_base,
        .vhost_get_vring_base = vhost_kernel_get_vring_base,
        .vhost_set_vring_kick = vhost_kernel_set_vring_kick,
        .vhost_set_vring_call = vhost_kernel_set_vring_call,
        .vhost_set_vring_busyloop_timeout =
                                vhost_kernel_set_vring_busyloop_timeout,
        .vhost_set_features = vhost_kernel_set_features,
        .vhost_get_features = vhost_kernel_get_features,
        .vhost_set_owner = vhost_kernel_set_owner,
        .vhost_reset_device = vhost_kernel_reset_device,
        .vhost_get_vq_index = vhost_kernel_get_vq_index,
};

static int vhost_kernel_init(struct vhost_dev *dev, void *opaque)
{
	/* opaque为vhost的fd */
    dev->opaque = opaque;

    return 0;
}

static int vhost_kernel_set_owner(struct vhost_dev *dev)
{
    return vhost_kernel_call(dev, VHOST_SET_OWNER, NULL);
}

static int vhost_kernel_call(struct vhost_dev *dev, unsigned long int request,
                             void *arg)
{
    int fd = (uintptr_t) dev->opaque;

    return ioctl(fd, request, arg);
}


static int vhost_kernel_set_vring_call(struct vhost_dev *dev,
                                       struct vhost_vring_file *file)
{
    return vhost_kernel_call(dev, VHOST_SET_VRING_CALL, file);
}

int event_notifier_init(EventNotifier *e, int active)
{
    int fds[2];
    int ret;

#ifdef CONFIG_EVENTFD
    ret = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#endif
    if (ret >= 0) {
        e->rfd = e->wfd = ret;
    }

    if (active) {
        event_notifier_set(e);
    }
    return 0;
}

int event_notifier_get_fd(const EventNotifier *e)
{
    return e->rfd;
}

