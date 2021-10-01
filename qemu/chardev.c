struct TypeImpl
{
    const char *name;

    size_t class_size;

    size_t instance_size;

    void (*class_init)(ObjectClass *klass, void *data);
    void (*class_base_init)(ObjectClass *klass, void *data);
    void (*class_finalize)(ObjectClass *klass, void *data);

    void *class_data;

    void (*instance_init)(Object *obj);
    void (*instance_post_init)(Object *obj);
    void (*instance_finalize)(Object *obj);

    bool abstract;

    const char *parent;
    TypeImpl *parent_type;

    ObjectClass *class;

    int num_interfaces;
    InterfaceImpl interfaces[MAX_INTERFACES];
};

/**
 * BusState:
 * @hotplug_device: link to a hotplug device associated with bus.
 */
struct BusState {
    Object obj;
    DeviceState *parent;
    const char *name;
    HotplugHandler *hotplug_handler;
    int max_index;
    bool realized;
	/* 链表的元素是BusChild */
    QTAILQ_HEAD(ChildrenHead, BusChild) children;
    QLIST_ENTRY(BusState) sibling;
};

typedef struct BusChild {
    DeviceState *child;
    int index;
    QTAILQ_ENTRY(BusChild) sibling;
} BusChild;

static void register_types(void)
{
    register_char_driver("socket", CHARDEV_BACKEND_KIND_SOCKET,
                         qemu_chr_parse_socket, qmp_chardev_open_socket);
  
    /* this must be done after machine init, since we register FEs with muxes
     * as part of realize functions like serial_isa_realizefn when -nographic
     * is specified
     */
    qemu_add_machine_init_done_notifier(&muxes_realize_notify);
}

type_init(register_types);

void register_char_driver(const char *name, ChardevBackendKind kind,
        void (*parse)(QemuOpts *opts, ChardevBackend *backend, Error **errp),
        CharDriverState *(*create)(const char *id, ChardevBackend *backend,
                                   ChardevReturn *ret, Error **errp))
{
    CharDriver *s;

    s = g_malloc0(sizeof(*s));
    s->name = g_strdup(name);
    s->kind = kind;
    s->parse = parse;
    s->create = create;

	/* 将s添加到backends元素中的data中 */
    backends = g_slist_append(backends, s);
}

/*
 * -chardev socket,id=drive-virtio-disk6,
 * path=/var/run/spdk/vhost_blk_socket-0397bf20-daff-476e-a2dd-564daf1e5f55-nvme,reconnect=10
 * socket即backend=socket
 */
static void qemu_chr_parse_socket(QemuOpts *opts, ChardevBackend *backend, Error **errp)
{
    bool is_listen      = qemu_opt_get_bool(opts, "server", false);
    bool is_waitconnect = is_listen && qemu_opt_get_bool(opts, "wait", true);
    bool is_telnet      = qemu_opt_get_bool(opts, "telnet", false);
    bool do_nodelay     = !qemu_opt_get_bool(opts, "delay", true);
    int64_t reconnect   = 0;
    const char *path = qemu_opt_get(opts, "path");
    const char *host = qemu_opt_get(opts, "host");
    const char *port = qemu_opt_get(opts, "port");
    const char *tls_creds = qemu_opt_get(opts, "tls-creds");
    SocketAddress *addr;
    ChardevSocket *sock;

    if(!is_listen) {
        reconnect = qemu_opt_get_number(opts, "reconnect", 100);
    }

    if (!path) {
        if (!host) {
            error_setg(errp, "chardev: socket: no host given");
            return;
        }
        if (!port) {
            error_setg(errp, "chardev: socket: no port given");
            return;
        }
    } else {
        if (tls_creds) {
            error_setg(errp, "TLS can only be used over TCP socket");
            return;
        }
    }

    sock = backend->u.socket.data = g_new0(ChardevSocket, 1);
    qemu_chr_parse_common(opts, qapi_ChardevSocket_base(sock));

    sock->has_nodelay = true;
    sock->nodelay = do_nodelay;
    sock->has_server = true;
    sock->server = is_listen;
    sock->has_telnet = true;
    sock->telnet = is_telnet;
    sock->has_wait = true;
    sock->wait = is_waitconnect;
    sock->has_reconnect = true;
    sock->reconnect = reconnect;
    sock->tls_creds = g_strdup(tls_creds);

    addr = g_new0(SocketAddress, 1);
    if (path) {
        UnixSocketAddress *q_unix;
        addr->type = SOCKET_ADDRESS_KIND_UNIX;
        q_unix = addr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
        q_unix->path = g_strdup(path);
    } else {
        addr->type = SOCKET_ADDRESS_KIND_INET;
        addr->u.inet.data = g_new(InetSocketAddress, 1);
        *addr->u.inet.data = (InetSocketAddress) {
            .host = g_strdup(host),
            .port = g_strdup(port),
            .has_to = qemu_opt_get(opts, "to"),
            .to = qemu_opt_get_number(opts, "to", 0),
            .has_ipv4 = qemu_opt_get(opts, "ipv4"),
            .ipv4 = qemu_opt_get_bool(opts, "ipv4", 0),
            .has_ipv6 = qemu_opt_get(opts, "ipv6"),
            .ipv6 = qemu_opt_get_bool(opts, "ipv6", 0),
        };
    }
    sock->addr = addr;
}

static CharDriverState *qmp_chardev_open_socket(const char *id,
                                                ChardevBackend *backend,
                                                ChardevReturn *ret,
                                                Error **errp)
{
    CharDriverState *chr;
    TCPCharDriver *s;
    ChardevSocket *sock = backend->u.socket.data;
    SocketAddress *addr = sock->addr;
    bool do_nodelay     = sock->has_nodelay ? sock->nodelay : false;
    bool is_listen      = sock->has_server  ? sock->server  : true;
    bool is_telnet      = sock->has_telnet  ? sock->telnet  : false;
    bool is_waitconnect = sock->has_wait    ? sock->wait    : false;
    int64_t reconnect   = sock->has_reconnect ? sock->reconnect : 0;
    ChardevCommon *common = qapi_ChardevSocket_base(sock);
    QIOChannelSocket *sioc = NULL;

    chr = qemu_chr_alloc(common, errp);

    s = g_new0(TCPCharDriver, 1);

    s->is_unix = addr->type == SOCKET_ADDRESS_KIND_UNIX;
    s->is_listen = is_listen;
    s->is_telnet = is_telnet;
    s->do_nodelay = do_nodelay;
    s->reconnecting = 0;
    if (sock->tls_creds) {
    }

    qapi_copy_SocketAddress(&s->addr, sock->addr);

    qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_RECONNECTABLE);
    if (s->is_unix) {
        qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_FD_PASS);
    }

    chr->opaque = s;
    chr->chr_wait_connected = tcp_chr_wait_connected;
    chr->chr_write = tcp_chr_write;
    chr->chr_sync_read = tcp_chr_sync_read;
    chr->chr_close = tcp_chr_close;
    chr->chr_disconnect = tcp_chr_disconnect;
    chr->get_msgfds = tcp_get_msgfds;
    chr->set_msgfds = tcp_set_msgfds;
    chr->chr_add_client = tcp_chr_add_client;
    chr->chr_add_watch = tcp_chr_add_watch;
    chr->chr_update_read_handler = tcp_chr_update_read_handler;
    /* be isn't opened until we get a connection */
    chr->explicit_be_open = true;

    chr->filename = SocketAddress_to_str("disconnected:",
                                         addr, is_listen, is_telnet);

    if (is_listen) {
        if (is_telnet) {
            s->do_telnetopt = 1;
        }
    } else if (reconnect > 0) {
        s->reconnect_time = reconnect;
    }

    if (s->reconnect_time) {
        sioc = qio_channel_socket_new();
        qio_channel_socket_connect_async(sioc, s->addr,
                                         qemu_chr_socket_connected,
                                         chr, NULL);
    } else {
        if (s->is_listen) {
            sioc = qio_channel_socket_new();
            if (qio_channel_socket_listen_sync(sioc, s->addr, errp) < 0) {
                goto error;
            }
            s->listen_ioc = sioc;
            if (is_waitconnect &&
                qemu_chr_wait_connected(chr, errp) < 0) {
                goto error;
            }
            if (!s->ioc) {
                s->listen_tag = qio_channel_add_watch(
                    QIO_CHANNEL(s->listen_ioc), G_IO_IN,
                    tcp_chr_accept, chr, NULL);
            }
        } else if (qemu_chr_wait_connected(chr, errp) < 0) {
            goto error;
        }
    }

    return chr;
}

QIOChannelSocket *
qio_channel_socket_new(void)
{
    QIOChannelSocket *sioc;
    QIOChannel *ioc;

    sioc = QIO_CHANNEL_SOCKET(object_new(TYPE_QIO_CHANNEL_SOCKET));
    sioc->fd = -1;

    ioc = QIO_CHANNEL(sioc);
    ioc->features |= (1 << QIO_CHANNEL_FEATURE_SHUTDOWN);

#ifdef WIN32
    ioc->event = CreateEvent(NULL, FALSE, FALSE, NULL);
#endif

    trace_qio_channel_socket_new(sioc);

    return sioc;
}

void qio_channel_socket_connect_async(QIOChannelSocket *ioc,
                                      SocketAddress *addr,
                                      QIOTaskFunc callback,
                                      gpointer opaque,
                                      GDestroyNotify destroy)
{
    QIOTask *task = qio_task_new(OBJECT(ioc), callback, opaque, destroy);
    SocketAddress *addrCopy;

    qapi_copy_SocketAddress(&addrCopy, addr);

    /* socket_connect() does a non-blocking connect(), but it
     * still blocks in DNS lookups, so we must use a thread */
    trace_qio_channel_socket_connect_async(ioc, addr);
    qio_task_run_in_thread(task,
                           qio_channel_socket_connect_worker,
                           addrCopy,
                           (GDestroyNotify)qapi_free_SocketAddress);
}

void qio_task_run_in_thread(QIOTask *task,
                            QIOTaskWorker worker,
                            gpointer opaque,
                            GDestroyNotify destroy)
{
    struct QIOTaskThreadData *data = g_new0(struct QIOTaskThreadData, 1);
    QemuThread thread;

    data->task = task;
    data->worker = worker;
    data->opaque = opaque;
    data->destroy = destroy;

    trace_qio_task_thread_start(task, worker, opaque);
	/* 创建线程，线程执行函数为 qio_task_thread_worker，参数为data */
    qemu_thread_create(&thread,
                       "io-task-worker",
                       qio_task_thread_worker,
                       data,
                       QEMU_THREAD_DETACHED);
}

static gpointer qio_task_thread_worker(gpointer opaque)
{
    struct QIOTaskThreadData *data = opaque;

    trace_qio_task_thread_run(data->task);
	/* 调用 qio_channel_socket_connect_worker */
    data->ret = data->worker(data->task, &data->err, data->opaque);
    if (data->ret < 0 && data->err == NULL) {
        error_setg(&data->err, "Task worker failed but did not set an error");
    }

    /* We're running in the background thread, and must only
     * ever report the task results in the main event loop
     * thread. So we schedule an idle callback to report
     * the worker results
     */
    trace_qio_task_thread_exit(data->task);
	/* main_loop_wait 只能在main_loop_wait循环中被调用 */
    g_idle_add(gio_task_thread_result, data);
    return NULL;
}

static int qio_channel_socket_connect_worker(QIOTask *task,
                                             Error **errp,
                                             gpointer opaque)
{
    QIOChannelSocket *ioc = QIO_CHANNEL_SOCKET(qio_task_get_source(task));
    SocketAddress *addr = opaque;
    int ret;

    ret = qio_channel_socket_connect_sync(ioc, addr, errp);

    object_unref(OBJECT(ioc));
    return ret;
}

int qio_channel_socket_connect_sync(QIOChannelSocket *ioc,
                                    SocketAddress *addr,
                                    Error **errp)
{
    int fd;

    trace_qio_channel_socket_connect_sync(ioc, addr);
    fd = socket_connect(addr, errp, NULL, NULL);

    trace_qio_channel_socket_connect_complete(ioc, fd);
    if (qio_channel_socket_set_fd(ioc, fd, errp) < 0) {
        close(fd);
        return -1;
    }

    return 0;
}

int socket_connect(SocketAddress *addr, Error **errp,
                   NonBlockingConnectHandler *callback, void *opaque)
{
    int fd;

    switch (addr->type) {
    case SOCKET_ADDRESS_KIND_INET:
        fd = inet_connect_saddr(addr->u.inet.data, errp, callback, opaque);
        break;

    case SOCKET_ADDRESS_KIND_UNIX:
        fd = unix_connect_saddr(addr->u.q_unix.data, errp, callback, opaque);
        break;

    case SOCKET_ADDRESS_KIND_FD:
        fd = monitor_get_fd(cur_mon, addr->u.fd.data->str, errp);
        if (fd >= 0 && callback) {
            qemu_set_nonblock(fd);
            callback(fd, NULL, opaque);
        }
        break;

    default:
        abort();
    }
    return fd;
}

static int unix_connect_saddr(UnixSocketAddress *saddr, Error **errp,
                              NonBlockingConnectHandler *callback, void *opaque)
{
    struct sockaddr_un un;
    ConnectState *connect_state = NULL;
    int sock, rc;

   

    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
   
    if (callback != NULL) {
        connect_state = g_malloc0(sizeof(*connect_state));
        connect_state->callback = callback;
        connect_state->opaque = opaque;
        qemu_set_nonblock(sock);
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", saddr->path);

    /* connect to peer */
    do {
        rc = 0;
        if (connect(sock, (struct sockaddr *) &un, sizeof(un)) < 0) {
            rc = -errno;
        }
    } while (rc == -EINTR);

    if (connect_state != NULL && QEMU_SOCKET_RC_INPROGRESS(rc)) {
        connect_state->fd = sock;
        qemu_set_fd_handler(sock, NULL, wait_for_connect, connect_state);
        return sock;
    } else if (rc >= 0) {
        /* non blocking socket immediate success, call callback */
        if (callback != NULL) {
            callback(sock, NULL, opaque);
        }
    }


    g_free(connect_state);
    return sock;
}

static int
qio_channel_socket_set_fd(QIOChannelSocket *sioc,
                          int fd,
                          Error **errp)
{
    int val;
    socklen_t len = sizeof(val);
    sioc->fd = fd;
    sioc->remoteAddrLen = sizeof(sioc->remoteAddr);
    sioc->localAddrLen = sizeof(sioc->localAddr);


    if (getpeername(fd, (struct sockaddr *)&sioc->remoteAddr,
                    &sioc->remoteAddrLen) < 0) {
        if (errno == ENOTCONN) {
            memset(&sioc->remoteAddr, 0, sizeof(sioc->remoteAddr));
            sioc->remoteAddrLen = sizeof(sioc->remoteAddr);
        } else {
            error_setg_errno(errp, errno,
                             "Unable to query remote socket address");
            goto error;
        }
    }

    if (getsockname(fd, (struct sockaddr *)&sioc->localAddr,
                    &sioc->localAddrLen) < 0) {
        error_setg_errno(errp, errno,
                         "Unable to query local socket address");
        goto error;
    }

    if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &val, &len) == 0 && val) {
        QIOChannel *ioc = QIO_CHANNEL(sioc);
        ioc->features |= (1 << QIO_CHANNEL_FEATURE_LISTEN);
    }

    return 0;
}

static gboolean gio_task_thread_result(gpointer opaque)
{
    struct QIOTaskThreadData *data = opaque;

    trace_qio_task_thread_result(data->task);
    if (data->ret == 0) {
        qio_task_complete(data->task);
    } else {
        qio_task_abort(data->task, data->err);
    }

    error_free(data->err);
    if (data->destroy) {
        data->destroy(data->opaque);
    }

    g_free(data);

    return FALSE;
}

void qio_task_complete(QIOTask *task)
{
	/* 调用 qemu_chr_socket_connected */
    task->func(task->source, NULL, task->opaque);
    trace_qio_task_complete(task);
    qio_task_free(task);
}

static void qemu_chr_socket_connected(Object *src, Error *err, void *opaque)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(src);
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;

    if (err) {
        check_report_connect_error(chr, err);
        object_unref(src);
        s->reconnecting = 0;
        return;
    }

    s->connect_err_reported = false;
    tcp_chr_new_client(chr, sioc);
    object_unref(OBJECT(sioc));
    s->reconnecting = 0;
}

static int tcp_chr_new_client(CharDriverState *chr, QIOChannelSocket *sioc)
{
    TCPCharDriver *s = chr->opaque;
    if (s->ioc != NULL) {
	return -1;
    }

    s->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(sioc));
    s->sioc = sioc;
    object_ref(OBJECT(sioc));

    qio_channel_set_blocking(s->ioc, false, NULL);

    if (s->do_nodelay) {
        qio_channel_set_delay(s->ioc, false);
    }
    if (s->listen_tag) {
        g_source_remove(s->listen_tag);
        s->listen_tag = 0;
    }

    if (s->tls_creds) {
        tcp_chr_tls_init(chr);
    } else {
        if (s->do_telnetopt) {
            tcp_chr_telnet_init(chr);
        } else {
            tcp_chr_connect(chr);
        }
    }

    return 0;
}

static void tcp_chr_connect(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;

    g_free(chr->filename);
    chr->filename = sockaddr_to_str(
        &s->sioc->localAddr, s->sioc->localAddrLen,
        &s->sioc->remoteAddr, s->sioc->remoteAddrLen,
        s->is_listen, s->is_telnet);

    s->connected = 1;
    if (s->ioc) {
        chr->fd_in_tag = io_add_watch_poll(s->ioc,
                                           tcp_chr_read_poll,
                                           tcp_chr_read, chr);
    }
    qemu_chr_be_generic_open(chr);
}

void qemu_chr_be_generic_open(CharDriverState *s)
{
    qemu_chr_be_event(s, CHR_EVENT_OPENED);
}

void qemu_chr_be_event(CharDriverState *s, int event)
{
    /* Keep track if the char device is open */
    switch (event) {
        case CHR_EVENT_OPENED:
            s->be_open = 1;
            break;
        case CHR_EVENT_CLOSED:
            s->be_open = 0;
            break;
    }

    if (!s->chr_event)
        return;
    s->chr_event(s->handler_opaque, event);
}


QemuOptsList qemu_chardev_opts = {
    .name = "chardev",
    .implied_opt_name = "backend", /* 隐式的参数名称,即backend= */
    .head = QTAILQ_HEAD_INITIALIZER(qemu_chardev_opts.head),
    .desc = {
        {
            .name = "backend",
            .type = QEMU_OPT_STRING,
        },{
            .name = "path",
            .type = QEMU_OPT_STRING,
        },{
            .name = "host",
            .type = QEMU_OPT_STRING,
        },{
            .name = "port",
            .type = QEMU_OPT_STRING,
        },{
            .name = "localaddr",
            .type = QEMU_OPT_STRING,
        },{
            .name = "localport",
            .type = QEMU_OPT_STRING,
        },{
            .name = "to",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "wait",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "server",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "delay",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "reconnect",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "telnet",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "tls-creds",
            .type = QEMU_OPT_STRING,
        },{
            .name = "width",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "height",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "cols",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "rows",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "mux",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "signal",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "name",
            .type = QEMU_OPT_STRING,
        },{
            .name = "debug",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "size",
            .type = QEMU_OPT_SIZE,
        },{
            .name = "chardev",
            .type = QEMU_OPT_STRING,
        },{
            .name = "append",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "logfile",
            .type = QEMU_OPT_STRING,
        },{
            .name = "logappend",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

/**
 * For each member of @list, call @func(@opaque, member, @errp).
 * Call it with the current location temporarily set to the member's.
 * @func() may store an Error through @errp, but must return non-zero then.
 * When @func() returns non-zero, break the loop and return that value.
 * Return zero when the loop completes.
 */
int qemu_opts_foreach(QemuOptsList *list, qemu_opts_loopfunc func,
                      void *opaque, Error **errp)
{
    Location loc;
    QemuOpts *opts;
    int rc = 0;

    loc_push_none(&loc);
    QTAILQ_FOREACH(opts, &list->head, next) {
        loc_restore(&opts->loc);
        rc = func(opaque, opts, errp);
        if (rc) {
            break;
        }
        assert(!errp || !*errp);
    }
    loc_pop(&loc);
    return rc;
}


int main(int argc, char *argv[]) 
{
	/**
	 * -chardev socket,id=drive-virtio-disk7,path=/var/run/spdk/vhost_blk_socket-1aa6d816-2046-4dfe-80b9-f5740aef8d47-nvme,reconnect=10 
	 * -device vhost-user-blk-pci,chardev=drive-virtio-disk7,num-queues=4,bus=pci.0,addr=0x7,id=virtio-disk7 
	 */
	if (qemu_opts_foreach(qemu_find_opts("chardev"),
                          chardev_init_func, NULL, NULL)) {
        exit(1);
   	}

	if (qemu_opts_foreach(qemu_find_opts("device"),
						  device_init_func, NULL, NULL)) {
		exit(1);
    }
}

static int chardev_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;

    qemu_chr_new_from_opts(opts, NULL, &local_err);

    return 0;
}

const char *qemu_opts_id(QemuOpts *opts)
{
    return opts->id;
}

/* register_char_driver会向backends链表中添加元素 */
static GSList *backends;

CharDriverState *qemu_chr_new_from_opts(QemuOpts *opts,
                                    void (*init)(struct CharDriverState *s),
                                    Error **errp)
{
    Error *local_err = NULL;
    CharDriver *cd;
    CharDriverState *chr;
    GSList *i;
    ChardevReturn *ret = NULL;
    ChardevBackend *backend;
    const char *id = qemu_opts_id(opts);
    char *bid = NULL;


    for (i = backends; i; i = i->next) {
        cd = i->data;

        if (strcmp(cd->name, qemu_opt_get(opts, "backend")) == 0) {
            break;
        }
    }
 
    backend = g_new0(ChardevBackend, 1);

    if (qemu_opt_get_bool(opts, "mux", 0)) {
        bid = g_strdup_printf("%s-base", id);
    }

    chr = NULL;
    backend->type = cd->kind;
    if (cd->parse) {
		/* 调用 qemu_chr_parse_socket */
        cd->parse(opts, backend, &local_err);
 
    } else {
        ChardevCommon *cc = g_new0(ChardevCommon, 1);
        qemu_chr_parse_common(opts, cc);
        backend->u.null.data = cc; /* Any ChardevCommon member would work */
    }

    ret = qmp_chardev_add(bid ? bid : id, backend, errp);


    if (bid) {
        qapi_free_ChardevBackend(backend);
        qapi_free_ChardevReturn(ret);
        backend = g_new0(ChardevBackend, 1);
        backend->u.mux.data = g_new0(ChardevMux, 1);
        backend->type = CHARDEV_BACKEND_KIND_MUX;
        backend->u.mux.data->chardev = g_strdup(bid);
        ret = qmp_chardev_add(id, backend, errp);
        if (!ret) {
            chr = qemu_chr_find(bid);
            qemu_chr_delete(chr);
            chr = NULL;
            goto qapi_out;
        }
    }

    chr = qemu_chr_find(id);
}

CharDriverState *qemu_chr_find(const char *name)
{
    CharDriverState *chr;

    QTAILQ_FOREACH(chr, &chardevs, next) {
        if (strcmp(chr->label, name) != 0)
            continue;
        return chr;
    }
    return NULL;
}


ChardevReturn *qmp_chardev_add(const char *id, ChardevBackend *backend,
                               Error **errp)
{
    ChardevReturn *ret = g_new0(ChardevReturn, 1);
    CharDriverState *chr = NULL;
    Error *local_err = NULL;
    GSList *i;
    CharDriver *cd;

    chr = qemu_chr_find(id);
    if (chr) {
        error_setg(errp, "Chardev '%s' already exists", id);
        g_free(ret);
        return NULL;
    }

    for (i = backends; i; i = i->next) {
        cd = i->data;

        if (cd->kind == backend->type) {
			/* 调用 qmp_chardev_open_socket */
            chr = cd->create(id, backend, ret, &local_err);
            break;
        }
    }

    chr->label = g_strdup(id);
    chr->avail_connections =
        (backend->type == CHARDEV_BACKEND_KIND_MUX) ? MAX_MUX : 1;
    if (!chr->filename) {
        chr->filename = g_strdup(ChardevBackendKind_lookup[backend->type]);
    }
    if (!chr->explicit_be_open) {
        qemu_chr_be_event(chr, CHR_EVENT_OPENED);
    }
	/* 将 chr添加到 chardevs 链表中 */
    QTAILQ_INSERT_TAIL(&chardevs, chr, next);
    return ret;
}


static int device_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    Error *err = NULL;
    DeviceState *dev;

    dev = qdev_device_add(opts, &err);

    object_unref(OBJECT(dev));
    return 0;
}


DeviceState *qdev_device_add(QemuOpts *opts, Error **errp)
{
    DeviceClass *dc;
    const char *driver, *path, *id;
    DeviceState *dev;
    BusState *bus = NULL;
    Error *err = NULL;

	/**
	 * -device vhost-user-blk-pci,chardev=drive-virtio-disk5,num-queues=4,
	 * bus=pci.0,addr=0x5,id=virtio-disk5
	 * driver是隐式参数，不带=就表示是driver的参数，即driver=vhost-user-blk-pci
	 */
    driver = qemu_opt_get(opts, "driver");

    /* find driver */
	/* 返回 */
    dc = qdev_get_device_class(&driver, errp);


    /* find bus，如果在参数中指定了，则进行查找 */
    path = qemu_opt_get(opts, "bus");
    if (path != NULL) {
		/* 后面在分析 */
        bus = qbus_find(path, errp);

    } else if (dc->bus_type != NULL) { /* 参数中没有指定，使用bus_type进行查找 */
        bus = qbus_find_recursive(sysbus_get_default(), NULL, dc->bus_type);

    }
    if (qdev_hotplug && bus && !qbus_is_hotpluggable(bus)) {
        error_setg(errp, QERR_BUS_NO_HOTPLUG, bus->name);
        return NULL;
    }

    /* create device */
    dev = DEVICE(object_new(driver));

    if (bus) {
        qdev_set_parent_bus(dev, bus);
    }

    id = qemu_opts_id(opts);
    if (id) {
        dev->id = id;
    }

    if (dev->id) {
        object_property_add_child(qdev_get_peripheral(), dev->id,
                                  OBJECT(dev), NULL);
    } else {
        static int anon_count;
        gchar *name = g_strdup_printf("device[%d]", anon_count++);
        object_property_add_child(qdev_get_peripheral_anon(), name,
                                  OBJECT(dev), NULL);
        g_free(name);
    }

    /* set properties 对opts中的没个ops参数，调用 set_property 函数 */
    if (qemu_opt_foreach(opts, set_property, dev, &err)) {
        error_propagate(errp, err);
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        return NULL;
    }

    dev->opts = opts;
    object_property_set_bool(OBJECT(dev), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        dev->opts = NULL;
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        return NULL;
    }
    return dev;
}

void object_property_set_bool(Object *obj, bool value,
                              const char *name, Error **errp)
{
    QBool *qbool = qbool_from_bool(value);
    object_property_set_qobject(obj, QOBJECT(qbool), name, errp);

    QDECREF(qbool);
}

void object_property_set_qobject(Object *obj, QObject *value,
                                 const char *name, Error **errp)
{
    QmpInputVisitor *qiv;
    qiv = qmp_input_visitor_new(value);
    object_property_set(obj, qmp_input_get_visitor(qiv), name, errp);

    qmp_input_visitor_cleanup(qiv);
}

void object_property_set(Object *obj, Visitor *v, const char *name,
                         Error **errp)
{
    ObjectProperty *prop = object_property_find(obj, name, errp);
    if (prop == NULL) {
        return;
    }

    if (!prop->set) {
        error_setg(errp, QERR_PERMISSION_DENIED);
    } else {
    	/* 调用 property_set_bool->device_set_realized->virtio_pci_dc_realize
		* pci_qdev_realize->virtio_pci_realize->vhost_user_blk_pci_realize
		* object_property_set->property_set_bool->device_set_realized->
		* virtio_device_realize->vhost_user_blk_device_realize
		*/
        prop->set(obj, v, name, prop->opaque, errp);
    }
}


const char *qemu_opt_get(QemuOpts *opts, const char *name)
{
    QemuOpt *opt;

    opt = qemu_opt_find(opts, name);
    if (!opt) {
        const QemuOptDesc *desc = find_desc_by_name(opts->list->desc, name);
        if (desc && desc->def_value_str) {
            return desc->def_value_str;
        }
    }
    return opt ? opt->str : NULL;
}

QemuOpt *qemu_opt_find(QemuOpts *opts, const char *name)
{
    QemuOpt *opt;

    QTAILQ_FOREACH_REVERSE(opt, &opts->head, QemuOptHead, next) {
        if (strcmp(opt->name, name) != 0)
            continue;
        return opt;
    }
    return NULL;
}

ObjectClass *object_class_by_name(const char *typename)
{
    TypeImpl *type = type_get_by_name(typename);

    if (!type) {
        return NULL;
    }

    type_initialize(type);

    return type->class;
}

static TypeImpl *type_get_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    return type_table_lookup(name);
}

static TypeImpl *type_table_lookup(const char *name)
{
    return g_hash_table_lookup(type_table_get(), name);
}

static void type_initialize(TypeImpl *ti)
{
    TypeImpl *parent;

    if (ti->class) {
        return;
    }

	/* 如果该ti的class_size为0，则返回父ti的class_size */
    ti->class_size = type_class_get_size(ti);
    ti->instance_size = type_object_get_size(ti);

    ti->class = g_malloc0(ti->class_size);

    parent = type_get_parent(ti);
    if (parent) {
		/* 递归初始化父 type */
        type_initialize(parent);
        GSList *e;
        int i;

        g_assert_cmpint(parent->class_size, <=, ti->class_size);
		/* 将父 class 拷贝到该 class中， 跟class是没有parent的，
		 * type_initialize首先分配class_size内存，class_size包含
		 * 父class的大小，所以在class_init中可以直接使用DEVICE_CLASS宏进行转换
		 */
        memcpy(ti->class, parent->class, parent->class_size);
        ti->class->interfaces = NULL;
		/* 添加属性 */
        ti->class->properties = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free, object_property_free);

        for (e = parent->class->interfaces; e; e = e->next) {
            InterfaceClass *iface = e->data;
            ObjectClass *klass = OBJECT_CLASS(iface);

            type_initialize_interface(ti, iface->interface_type, klass->type);
        }

        for (i = 0; i < ti->num_interfaces; i++) {
            TypeImpl *t = type_get_by_name(ti->interfaces[i].typename);
            for (e = ti->class->interfaces; e; e = e->next) {
                TypeImpl *target_type = OBJECT_CLASS(e->data)->type;

                if (type_is_ancestor(target_type, t)) {
                    break;
                }
            }

            if (e) {
                continue;
            }

            type_initialize_interface(ti, t, t);
        }
    } else {
        ti->class->properties = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free, object_property_free);
    }

    ti->class->type = ti;

    while (parent) {
        if (parent->class_base_init) {
            parent->class_base_init(ti->class, ti->class_data);
        }
        parent = type_get_parent(parent);
    }

    if (ti->class_init) {
		/* 调用class_init */
        ti->class_init(ti->class, ti->class_data);
    }
}

static TypeImpl *type_get_parent(TypeImpl *type)
{
	/* 没有初始化之前type->parent_type为NULL
	 * type->parent是通过代码定义的
	 */
    if (!type->parent_type && type->parent) {
		/* 将父 type赋值给 parent_type */
        type->parent_type = type_get_by_name(type->parent);
        g_assert(type->parent_type != NULL);
    }

    return type->parent_type;
}


static size_t type_class_get_size(TypeImpl *ti)
{
    if (ti->class_size) {
        return ti->class_size;
    }

    if (type_has_parent(ti)) {
        return type_class_get_size(type_get_parent(ti));
    }

    return sizeof(ObjectClass);
}

static size_t type_object_get_size(TypeImpl *ti)
{
    if (ti->instance_size) {
        return ti->instance_size;
    }

    if (type_has_parent(ti)) {
        return type_object_get_size(type_get_parent(ti));
    }

    return 0;
}

static DeviceClass *qdev_get_device_class(const char **driver, Error **errp)
{
    ObjectClass *oc;
    DeviceClass *dc;
    const char *original_name = *driver;

    oc = object_class_by_name(*driver);
    if (!oc) {
        const char *typename = find_typename_by_alias(*driver);

        if (typename) {
            *driver = typename;
            oc = object_class_by_name(*driver);
        }
    }

    dc = DEVICE_CLASS(oc);

    return dc;
}

bool object_class_is_abstract(ObjectClass *klass)
{
    return klass->type->abstract;
}

Object *object_new(const char *typename)
{
    TypeImpl *ti = type_get_by_name(typename);
	/* 递归调用，分配instance_size，然后调用 instance_init */
    return object_new_with_type(ti);
}

Object *object_new_with_type(Type type)
{
    Object *obj;

    g_assert(type != NULL);
	/* 会调用vhost_user_blk_pci_class_init */
    type_initialize(type);

    obj = g_malloc(type->instance_size);
    object_initialize_with_type(obj, type->instance_size, type);
    obj->free = g_free;

    return obj;
}

void object_initialize_with_type(void *data, size_t size, TypeImpl *type)
{
    Object *obj = data;

    g_assert(type != NULL);
	/* 递归调用class_init */
    type_initialize(type);


    memset(obj, 0, type->instance_size);
	/* obj和objclass联系起来 */
    obj->class = type->class;
    object_ref(obj);
    obj->properties = g_hash_table_new_full(g_str_hash, g_str_equal,
                                            NULL, object_property_free);
	/* 递归调用父 instance_init */
    object_init_with_type(obj, type);
    object_post_init_with_type(obj, type);
}

static void object_init_with_type(Object *obj, TypeImpl *ti)
{
    if (type_has_parent(ti)) {
        object_init_with_type(obj, type_get_parent(ti));
    }

    if (ti->instance_init) {
        ti->instance_init(obj);
    }
}

void qdev_set_parent_bus(DeviceState *dev, BusState *bus)
{
    dev->parent_bus = bus;
    object_ref(OBJECT(bus));
    bus_add_child(bus, dev);
}

static void bus_add_child(BusState *bus, DeviceState *child)
{
    char name[32];
    BusChild *kid = g_malloc0(sizeof(*kid));

    kid->index = bus->max_index++;
    kid->child = child;
    object_ref(OBJECT(kid->child));

    QTAILQ_INSERT_HEAD(&bus->children, kid, sibling);

    /* This transfers ownership of kid->child to the property.  */
    snprintf(name, sizeof(name), "child[%d]", kid->index);
    object_property_add_link(OBJECT(bus), name,
                             object_get_typename(OBJECT(child)),
                             (Object **)&kid->child,
                             NULL, /* read-only property */
                             0, /* return ownership on prop deletion */
                             NULL);
}

/**
 * For each member of @opts, call @func(@opaque, name, value, @errp).
 * @func() may store an Error through @errp, but must return non-zero then.
 * When @func() returns non-zero, break the loop and return that value.
 * Return zero when the loop completes.
 */
int qemu_opt_foreach(QemuOpts *opts, qemu_opt_loopfunc func, void *opaque,
                     Error **errp)
{
    QemuOpt *opt;
    int rc;
	/* 对opts中的没个ops参数，调用 set_property 函数 */
    QTAILQ_FOREACH(opt, &opts->head, next) {
    	/* opaque为dev，opt->name为参数的名字, opt->str为参数的字符串形式的值 */
        rc = func(opaque, opt->name, opt->str, errp);
        if (rc) {
            return rc;
        }
        assert(!errp || !*errp);
    }
    return 0;
}

static int set_property(void *opaque, const char *name, const char *value,
                        Error **errp)
{
    Object *obj = opaque;
    Error *err = NULL;

    if (strcmp(name, "driver") == 0)
        return 0;
    if (strcmp(name, "bus") == 0)
        return 0;


	/* vhost_user_blk_class_init有添加下面的属性
	* static Property vhost_user_blk_properties[] = {
	*		DEFINE_PROP_CHR("chardev", VHostUserBlk, chardev),
	*		DEFINE_PROP_UINT16("num-queues", VHostUserBlk, num_queues, 1),
	*		DEFINE_PROP_UINT32("queue-size", VHostUserBlk, queue_size, 128),
	*		DEFINE_PROP_BIT("config-wce", VHostUserBlk, config_wce, 0, true),
	*		DEFINE_PROP_BIT("config-ro", VHostUserBlk, config_ro, 0, false),
	*		DEFINE_PROP_END_OF_LIST(),
	*	};
	* qdev_alias_all_properties中会把dc->props的属性添加到obj中
	*/
    object_property_parse(obj, value, name, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return -1;
    }
    return 0;
}

void object_property_parse(Object *obj, const char *string,
                           const char *name, Error **errp)
{
    StringInputVisitor *siv;
    siv = string_input_visitor_new(string);
	/* 主要看 chardev这个属性， 则name为chardev，object_property_set中会调用
	* set_chr->set_pointer->parse_chr
	*/
    object_property_set(obj, string_input_get_visitor(siv), name, errp);

    string_input_visitor_cleanup(siv);
}

static void set_chr(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    set_pointer(obj, v, opaque, parse_chr, name, errp);
}

static void set_pointer(Object *obj, Visitor *v, Property *prop,
                        void (*parse)(DeviceState *dev, const char *str,
                                      void **ptr, const char *propname,
                                      Error **errp),
                        const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Error *local_err = NULL;
    void **ptr = qdev_get_prop_ptr(dev, prop);
    char *str;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, name, &str, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (!*str) {
        g_free(str);
        *ptr = NULL;
        return;
    }
    parse(dev, str, ptr, prop->name, errp);
    g_free(str);
}

void *qdev_get_prop_ptr(DeviceState *dev, Property *prop)
{
    void *ptr = dev;
    ptr += prop->offset;
	/* 这里的ptr就是VHostUserBlk->chardev的指针，VHostUserBlk包含DeviceState */
    return ptr;
}


static void parse_chr(DeviceState *dev, const char *str, void **ptr,
                      const char *propname, Error **errp)
{
	/* 根据chardev=drive-virtio-disk6的drive-virtio-disk6查找CharDriverState */
    CharDriverState *chr = qemu_chr_find(str);
    if (chr == NULL) {
        error_setg(errp, "Property '%s.%s' can't find value '%s'",
                   object_get_typename(OBJECT(dev)), propname, str);
        return;
    }
    if (qemu_chr_fe_claim(chr) != 0) {
        error_setg(errp, "Property '%s.%s' can't take value '%s', it's in use",
                  object_get_typename(OBJECT(dev)), propname, str);
        return;
    }
	/* 将chr 赋值给 VHostUserBlk->chardev 
	* 这样CharDriverState和VHostUserBlk就联系起来了
	*/
    *ptr = chr;
}

 
static BusState *qbus_find(const char *path, Error **errp)
{
    DeviceState *dev;
    BusState *bus;
    char elem[128];
    int pos, len;

    /* find start element */
    if (path[0] == '/') {
        bus = sysbus_get_default();
        pos = 0;
    } else { /* path不以'/'开头 */
        if (sscanf(path, "%127[^/]%n", elem, &len) != 1) {
            elem[0] = len = 0;
        }
        bus = qbus_find_recursive(sysbus_get_default(), elem, NULL);

        pos = len;
    }

    for (;;) {

        while (path[pos] == '/') {
            pos++;
        }
        if (path[pos] == '\0') {
            break;
        }

        /* find device,命令行中bus后面还会指定device的地址等
		 * 比如：bus=pci.0,addr=0x7
         */
        if (sscanf(path+pos, "%127[^/]%n", elem, &len) != 1) {
            g_assert_not_reached();
            elem[0] = len = 0;
        }
        pos += len;
        dev = qbus_find_dev(bus, elem);

        while (path[pos] == '/') {
            pos++;
        }
      
        /* find bus */
        if (sscanf(path+pos, "%127[^/]%n", elem, &len) != 1) {
            g_assert_not_reached();
            elem[0] = len = 0;
        }
        pos += len;
        bus = qbus_find_bus(dev, elem);
    }

    return bus;
}

static BusState *qbus_find_recursive(BusState *bus, const char *name,
                                     const char *bus_typename)
{
    BusChild *kid;
    BusState *pick, *child, *ret;
    bool match;

	/* 第一次调用，bus为root        bus , name为bus的名称 */
    if (name) {
        match = !strcmp(bus->name, name);
    } else {
        match = !!object_dynamic_cast(OBJECT(bus), bus_typename);
    }
	/* 如果名称匹配就返回该bus */
    if (match && !qbus_is_full(bus)) {
        return bus;             /* root matches and isn't full */
    }

    pick = match ? bus : NULL;

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            ret = qbus_find_recursive(child, name, bus_typename);
            if (ret && !qbus_is_full(ret)) {
                return ret;     /* a descendant matches and isn't full */
            }
            if (ret && !pick) {
                pick = ret;
            }
        }
    }

    /* root or a descendant matches, but is full */
    return pick;
}

static int tcp_chr_wait_connected(CharDriverState *chr, Error **errp)
{
    TCPCharDriver *s = chr->opaque;
    QIOChannelSocket *sioc;

    /* It can't wait on s->connected, since it is set asynchronously
     * in TLS and telnet cases, only wait for an accepted socket 
     */

	/* tcp_chr_new_client中会给 s->ioc 负责      */
    while (!s->ioc) {
        if (s->is_listen) {
            fprintf(stderr, "QEMU waiting for connection on: %s\n",
                    chr->filename);
            qio_channel_set_blocking(QIO_CHANNEL(s->listen_ioc), true, NULL);
            tcp_chr_accept(QIO_CHANNEL(s->listen_ioc), G_IO_IN, chr);
            qio_channel_set_blocking(QIO_CHANNEL(s->listen_ioc), false, NULL);
        } else {
            sioc = qio_channel_socket_new();
            if (qio_channel_socket_connect_sync(sioc, s->addr, errp) < 0) {
                object_unref(OBJECT(sioc));
                return -1;
            }
            tcp_chr_new_client(chr, sioc);
            object_unref(OBJECT(sioc));
        }
    }

    return 0;
}

