/*
 * -netdev tap,fds=80:81:82:83:84:85:86:87,id=hostnet2,vhost=on,vhostfds=88:89:90:91:92:93:94:95 
 * -device virtio-net-pci,event_idx=on,mq=on,vectors=18,rx_queue_size=1024,tx_queue_size=1024,netdev=hostnet2,id=net2,mac=20:90:6f:28:84:f2,bus=pci.0,addr=0x7 
 */
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

    /* QEMU vlans does not support multiqueue tap, in this case peer is set.
     * For -netdev, peer is always NULL. */
    if (peer && (tap->has_queues || tap->has_fds || tap->has_vhostfds)) {
        error_setg(errp, "Multiqueue tap cannot be used with QEMU vlans");
        return -1;
    }

    if (tap->has_fd) {
		/* 
        if (tap->has_ifname || tap->has_script || tap->has_downscript ||
            tap->has_vnet_hdr || tap->has_helper || tap->has_queues ||
            tap->has_fds || tap->has_vhostfds) {
            error_setg(errp, "ifname=, script=, downscript=, vnet_hdr=, "
                       "helper=, queues=, fds=, and vhostfds= "
                       "are invalid with fd=");
            return -1;
        }

        fd = monitor_fd_param(cur_mon, tap->fd, &err);
    
        fcntl(fd, F_SETFL, O_NONBLOCK);

        vnet_hdr = tap_probe_vnet_hdr(fd);

        net_init_tap_one(tap, peer, "tap", name, NULL, script, downscript,
                         vhostfdname, vnet_hdr, fd, &err);
        if (err) {
            error_propagate(errp, err);
            return -1;
        }
        */
    } else if (tap->has_fds) {
        char *fds[MAX_TAP_QUEUES];
        char *vhost_fds[MAX_TAP_QUEUES];
        int nfds, nvhosts;

        if (tap->has_ifname || tap->has_script || tap->has_downscript ||
            tap->has_vnet_hdr || tap->has_helper || tap->has_queues ||
            tap->has_vhostfd) {
            error_setg(errp, "ifname=, script=, downscript=, vnet_hdr=, "
                       "helper=, queues=, and vhostfd= "
                       "are invalid with fds=");
            return -1;
        }
		/* 将  tap->fds中以":"进行分割的字符串，复制到fds中 */
        nfds = get_fds(tap->fds, fds, MAX_TAP_QUEUES);
        if (tap->has_vhostfds) {
            nvhosts = get_fds(tap->vhostfds, vhost_fds, MAX_TAP_QUEUES);
        }

        for (i = 0; i < nfds; i++) {
            fd = monitor_fd_param(cur_mon, fds[i], &err);
   

            fcntl(fd, F_SETFL, O_NONBLOCK);

            if (i == 0) {
                vnet_hdr = tap_probe_vnet_hdr(fd);
            } else if (vnet_hdr != tap_probe_vnet_hdr(fd)) {
 
            }

			/* 对每一个nfd调用  net_init_tap_one     */
            net_init_tap_one(tap, peer, "tap", name, ifname,
                             script, downscript,
                             tap->has_vhostfds ? vhost_fds[i] : NULL,
                             vnet_hdr, fd, &err);
        }
    } else if (tap->has_helper)  else {}

    return 0;
}

int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp)
{
    int fd;
    Error *local_err = NULL;

    if (!qemu_isdigit(fdname[0]) && mon) {
        fd = monitor_get_fd(mon, fdname, &local_err);
    } else {
    	/* 将字符串转换成int */
        fd = qemu_parse_fd(fdname);
    }


    return fd;
}

#define MAX_TAP_QUEUES 1024

static void net_init_tap_one(const NetdevTapOptions *tap, NetClientState *peer,
                             const char *model, const char *name,
                             const char *ifname, const char *script,
                             const char *downscript, const char *vhostfdname,
                             int vnet_hdr, int fd, Error **errp)
{
    Error *err = NULL;
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

        if (vhostfdname) {
            vhostfd = monitor_fd_param(cur_mon, vhostfdname, &err);
        } else {
            vhostfd = open("/dev/vhost-net", O_RDWR);
        }
        options.opaque = (void *)(uintptr_t)vhostfd;

        s->vhost_net = vhost_net_init(&options);
        
    } 
}

/* fd support */

static NetClientInfo net_tap_info = {
    .type = NET_CLIENT_OPTIONS_KIND_TAP,
    .size = sizeof(TAPState),
    .receive = tap_receive,
    .receive_raw = tap_receive_raw,
    .receive_iov = tap_receive_iov,
    .poll = tap_poll,
    .cleanup = tap_cleanup,
    .has_ufo = tap_has_ufo,
    .has_vnet_hdr = tap_has_vnet_hdr,
    .has_vnet_hdr_len = tap_has_vnet_hdr_len,
    .using_vnet_hdr = tap_using_vnet_hdr,
    .set_offload = tap_set_offload,
    .set_vnet_hdr_len = tap_set_vnet_hdr_len,
    .set_vnet_le = tap_set_vnet_le,
    .set_vnet_be = tap_set_vnet_be,
};


static TAPState *net_tap_fd_init(NetClientState *peer,
                                 const char *model,
                                 const char *name,
                                 int fd,
                                 int vnet_hdr)
{
    NetClientState *nc;
    TAPState *s;

    nc = qemu_new_net_client(&net_tap_info, peer, model, name);

    s = DO_UPCAST(TAPState, nc, nc);

    s->fd = fd;
    s->host_vnet_hdr_len = vnet_hdr ? sizeof(struct virtio_net_hdr) : 0;
    s->using_vnet_hdr = false;
    s->has_ufo = tap_probe_has_ufo(s->fd);
    s->enabled = true;
    tap_set_offload(&s->nc, 0, 0, 0, 0, 0);
    /*
     * Make sure host header length is set correctly in tap:
     * it might have been modified by another instance of qemu.
     */
    if (tap_probe_vnet_hdr_len(s->fd, s->host_vnet_hdr_len)) {
        tap_fd_set_vnet_hdr_len(s->fd, s->host_vnet_hdr_len);
    }
    tap_read_poll(s, true);
    s->vhost_net = NULL;

    s->exit.notify = tap_exit_notify;
    qemu_add_exit_notifier(&s->exit);

    return s;
}

