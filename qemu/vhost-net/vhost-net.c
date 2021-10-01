int vhost_net_start(VirtIODevice *dev, NetClientState *ncs,
                    int total_queues)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(vbus);
    int r, e, i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    for (i = 0; i < total_queues; i++) {
        struct vhost_net *net;

        net = get_vhost_net(ncs[i].peer);
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

VHostNetState *get_vhost_net(NetClientState *nc)
{
    VHostNetState *vhost_net = 0;vhost_set_vring_call

    if (!nc) {
        return 0;
    }

    switch (nc->info->type) {
    case NET_CLIENT_OPTIONS_KIND_TAP:
        vhost_net = tap_get_vhost_net(nc);
        break;
    case NET_CLIENT_OPTIONS_KIND_VHOST_USER:
        vhost_net = vhost_user_get_vhost_net(nc);
        assert(vhost_net);
        break;
    default:
        break;
    }

    return vhost_net;
}

VHostNetState *tap_get_vhost_net(NetClientState *nc)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    assert(nc->info->type == NET_CLIENT_OPTIONS_KIND_TAP);
    return s->vhost_net;
}

