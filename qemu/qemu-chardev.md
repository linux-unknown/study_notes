# qemu chardev

以如下的参数学习qemu chardev的处理

```c
-chardev socket,id=drive-virtio-disk7,path=/var/run/spdk/vhost_blk_socket-1aa6d816-2046-4dfe-80b9-f5740aef8d47-nvme,reconnect=10
-device vhost-user-blk-pci,chardev=drive-virtio-disk7,num-queues=4,bus=pci.0,addr=0x7,id=virtio-disk7
    
chardev的隐式参数为backend=socket
```
## main
```c
int main(int argc, char *argv[]) 
{
    qemu_add_opts(&qemu_chardev_opts);
	if (qemu_opts_foreach(qemu_find_opts("chardev"), chardev_init_func, NULL, NULL)) {
        exit(1);
   	}
}
```

## qemu_chardev_opts

```c
QemuOptsList qemu_chardev_opts = {
    .name = "chardev",
    .implied_opt_name = "backend",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_chardev_opts.head),
    .desc = {
        /* 有删减 */
        {
            .name = "backend",
            .type = QEMU_OPT_STRING,
        },{
            .name = "path",
            .type = QEMU_OPT_STRING,
        },{
            .name = "reconnect",
            .type = QEMU_OPT_NUMBER,
        }
        { /* end of list */ }
    },
};
```

## backend注册

### register_types

```c
static void register_types(void)
{
    register_char_driver("socket", CHARDEV_BACKEND_KIND_SOCKET,qemu_chr_parse_socket, 
    						qmp_chardev_open_socket);
}
type_init(register_types);
```

### register_char_driver

```c
/* GSList是glibc实现的单向链表 */
static GSList *backends;

void register_char_driver(const char *name, ChardevBackendKind kind,
        void (*parse)(QemuOpts *opts, ChardevBackend *backend, Error **errp),
        CharDriverState *(*create)(const char *id, ChardevBackend *backend, ChardevReturn *ret, 
        Error **errp))
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
```

## chardev_init_func

`qemu_find_opts("chardev")`会在vm_config_groups中以chardev为参数进行查找，`qemu_add_opts(&qemu_chardev_opts)`会向vm_config_groups中添加元素。qemu_opts_foreach会对找到的QemuOptsList调用chardev_init_func函数

chardev_init_func-->qemu_chr_new_from_opts

### qemu_chr_new_from_opts

```c
CharDriverState *qemu_chr_new_from_opts(QemuOpts *opts, void (*init)(struct CharDriverState *s),
                                    Error **errp)
{
    Error *local_err = NULL;
    CharDriver *cd;
    CharDriverState *chr;
    GSList *i;
    ChardevReturn *ret = NULL;
    ChardevBackend *backend;
    const char *id = qemu_opts_id(opts); /* 获取opts中的id参数值，即drive-virtio-disk7 */
    char *bid = NULL;

    for (i = backends; i; i = i->next) {
        cd = i->data;
        if (strcmp(cd->name, qemu_opt_get(opts, "backend")) == 0) {
            break;
        }
    }
 
    backend = g_new0(ChardevBackend, 1);
	/* 本例中没有mux参数 */
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
    }

    chr = qemu_chr_find(id);
}
```

#### qemu_chr_parse_socket

```c
/*
 * -chardev socket,id=drive-virtio-disk7,
 * path=/var/run/spdk/vhost_blk_socket-1aa6d816-2046-4dfe-80b9-  *f5740aef8d47-nvme,reconnect=10
 * socket即backend=socket
 */
static void qemu_chr_parse_socket(QemuOpts *opts, ChardevBackend *backend, Error **errp)
{
    bool is_listen      = qemu_opt_get_bool(opts, "server", false); /* 本例中没有 server 参数 */
    bool is_waitconnect = is_listen && qemu_opt_get_bool(opts, "wait", true);/* 本例中没有 wait 参数 */
    bool is_telnet      = qemu_opt_get_bool(opts, "telnet", false);/* 本例中没有 telnet 参数 */
    bool do_nodelay     = !qemu_opt_get_bool(opts, "delay", true);/* 本例中没有 delay 参数 */
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
    if (path) { /* 本例子中path不为NULL */
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

```

### qmp_chardev_add

```c
ChardevReturn *qmp_chardev_add(const char *id, ChardevBackend *backend, Error **errp)
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
    chr->avail_connections = (backend->type == CHARDEV_BACKEND_KIND_MUX) ? MAX_MUX : 1;
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
```

#### qmp_chardev_open_socket

```c
static CharDriverState *qmp_chardev_open_socket(const char *id, ChardevBackend *backend,
                                                ChardevReturn *ret, Error **errp)
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
	/* 分配 CharDriverState 内存 */
    chr = qemu_chr_alloc(common, errp);

    s = g_new0(TCPCharDriver, 1);

    s->is_unix = addr->type == SOCKET_ADDRESS_KIND_UNIX;
    s->is_listen = is_listen;
    s->is_telnet = is_telnet;
    s->do_nodelay = do_nodelay;
    s->reconnecting = 0;

    qapi_copy_SocketAddress(&s->addr, sock->addr);
	/* chr->features 设置属性 */
    qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_RECONNECTABLE);
    if (s->is_unix) {
        qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_FD_PASS);
    }

    chr->opaque = s; /* TCPCharDriver */
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
	/* 如果 reconnect_time 不为0，则异步连接 */
    if (s->reconnect_time) {
        sioc = qio_channel_socket_new();
        qio_channel_socket_connect_async(sioc, s->addr, qemu_chr_socket_connected, chr, NULL);
    } else { /* 否则同步连接 */
        } else if (qemu_chr_wait_connected(chr, errp) < 0) { /* 我们不是 is_listen */
 
        }
    }

    return chr;
}
```

#### qio_channel_socket_new

```c
QIOChannelSocket *qio_channel_socket_new(void)
{
    QIOChannelSocket *sioc;
    QIOChannel *ioc;

    sioc = QIO_CHANNEL_SOCKET(object_new(TYPE_QIO_CHANNEL_SOCKET));
    sioc->fd = -1;

    ioc = QIO_CHANNEL(sioc);
    ioc->features |= (1 << QIO_CHANNEL_FEATURE_SHUTDOWN);

    return sioc;
}
```

#### qio_channel_socket_connect_async

```c
void qio_channel_socket_connect_async(QIOChannelSocket *ioc, SocketAddress *addr, QIOTaskFunc callback,
                                      gpointer opaque, GDestroyNotify destroy)
{
    QIOTask *task = qio_task_new(OBJECT(ioc), callback, opaque, destroy);
    SocketAddress *addrCopy;

    qapi_copy_SocketAddress(&addrCopy, addr);

    /* socket_connect() does a non-blocking connect(), but it
     * still blocks in DNS lookups, so we must use a thread */
    qio_task_run_in_thread(task, qio_channel_socket_connect_worker, addrCopy,
                           (GDestroyNotify)qapi_free_SocketAddress);
}

QIOTask *qio_task_new(Object *source, QIOTaskFunc func, gpointer opaque, GDestroyNotify destroy)
{
    QIOTask *task;

    task = g_new0(QIOTask, 1);

    task->source = source;
    object_ref(source);
    task->func = func;
    task->opaque = opaque;
    task->destroy = destroy;

    trace_qio_task_new(task, source, func, opaque);

    return task;
}
```

#### qio_task_run_in_thread

```c
void qio_task_run_in_thread(QIOTask *task, QIOTaskWorker worker, gpointer opaque, GDestroyNotify destroy)
{
    struct QIOTaskThreadData *data = g_new0(struct QIOTaskThreadData, 1);
    QemuThread thread;

    data->task = task;
    data->worker = worker;
    data->opaque = opaque;
    data->destroy = destroy;

    trace_qio_task_thread_start(task, worker, opaque);
	/* 创建线程，线程执行函数为 qio_task_thread_worker，参数为data */
    qemu_thread_create(&thread, "io-task-worker", qio_task_thread_worker, data,
                       QEMU_THREAD_DETACHED);
}
```

#### qio_task_thread_worker

```c
static gpointer qio_task_thread_worker(gpointer opaque)
{
    struct QIOTaskThreadData *data = opaque;

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
	/* main_loop_wait 只能在main_loop_wait循环中被调用 */
    g_idle_add(gio_task_thread_result, data);
    return NULL;
}
```

##### qio_channel_socket_connect_worker

```c
static int qio_channel_socket_connect_worker(QIOTask *task, Error **errp, gpointer opaque)
{
    /* qio_task_get_source: task->source */
    QIOChannelSocket *ioc = QIO_CHANNEL_SOCKET(qio_task_get_source(task));
    SocketAddress *addr = opaque;
    int ret;

    ret = qio_channel_socket_connect_sync(ioc, addr, errp);

    object_unref(OBJECT(ioc));
    return ret;
}
```

##### qio_channel_socket_connect_sync

```c
int qio_channel_socket_connect_sync(QIOChannelSocket *ioc, SocketAddress *addr, Error **errp)
{
    int fd;
    fd = socket_connect(addr, errp, NULL, NULL);
    if (qio_channel_socket_set_fd(ioc, fd, errp) < 0) { }
    return 0;
}
```

##### socket_connect

```c
int socket_connect(SocketAddress *addr, Error **errp, NonBlockingConnectHandler *callback, void *opaque)
{
    int fd;

    switch (addr->type) {
    case SOCKET_ADDRESS_KIND_INET:
        fd = inet_connect_saddr(addr->u.inet.data, errp, callback, opaque);
        break;
    case SOCKET_ADDRESS_KIND_UNIX:
        fd = unix_connect_saddr(addr->u.q_unix.data, errp, callback, opaque);
        break;
    default:
        abort();
    }
    return fd;
}
```

##### unix_connect_saddr

```c
static int unix_connect_saddr(UnixSocketAddress *saddr, Error **errp,
                              NonBlockingConnectHandler *callback, void *opaque)
{
    struct sockaddr_un un;
    ConnectState *connect_state = NULL;
    int sock, rc;
    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0); /* 创建socket */
   
    if (callback != NULL) { /* callbac为NULL */
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
        /* non blocking socket immediate success, call callback， callback为NULL */
        if (callback != NULL) {
            callback(sock, NULL, opaque);
        }
    }
    g_free(connect_state);
    return sock; /* 返回fd */
}
```

##### qio_channel_socket_set_fd

```c
static int qio_channel_socket_set_fd(QIOChannelSocket *sioc, int fd, Error **errp)
{
    int val;
    socklen_t len = sizeof(val);
    sioc->fd = fd;
    sioc->remoteAddrLen = sizeof(sioc->remoteAddr);
    sioc->localAddrLen = sizeof(sioc->localAddr);


    if (getpeername(fd, (struct sockaddr *)&sioc->remoteAddr,  &sioc->remoteAddrLen) < 0) {
        if (errno == ENOTCONN) {
            memset(&sioc->remoteAddr, 0, sizeof(sioc->remoteAddr));
            sioc->remoteAddrLen = sizeof(sioc->remoteAddr);
        } else {
            error_setg_errno(errp, errno, "Unable to query remote socket address");
            goto error;
        }
    }

    if (getsockname(fd, (struct sockaddr *)&sioc->localAddr, &sioc->localAddrLen) < 0) {
        error_setg_errno(errp, errno, "Unable to query local socket address");
        goto error;
    }

    if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &val, &len) == 0 && val) {
        QIOChannel *ioc = QIO_CHANNEL(sioc);
        ioc->features |= (1 << QIO_CHANNEL_FEATURE_LISTEN);
    }

    return 0;
}
```

#### gio_task_thread_result

```c
static gboolean gio_task_thread_result(gpointer opaque)
{
    struct QIOTaskThreadData *data = opaque;

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
```

##### qio_task_complete

```c
void qio_task_complete(QIOTask *task)
{
	/* 调用 qemu_chr_socket_connected */
    task->func(task->source, NULL, task->opaque);
    trace_qio_task_complete(task);
    qio_task_free(task);
}
```

##### qemu_chr_socket_connected

```c
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
```
tcp_chr_new_client-->tcp_chr_connect-->qemu_chr_be_generic_open

##### qemu_chr_be_generic_open

```c
void qemu_chr_be_generic_open(CharDriverState *s)
{
    qemu_chr_be_event(s, CHR_EVENT_OPENED);
}
```

##### qemu_chr_be_event

```c
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
    /* qemu_chr_add_handlers 可以设置CharDriverState的函数 */
    s->chr_event(s->handler_opaque, event);
}
```

#### qemu_chr_wait_connected

```c
int qemu_chr_wait_connected(CharDriverState *chr, Error **errp)
{
    if (chr->chr_wait_connected) {
    	/* 调用 tcp_chr_wait_connected */
        return chr->chr_wait_connected(chr, errp);
    }

    return 0;
}
```

##### tcp_chr_wait_connected

```c
static int tcp_chr_wait_connected(CharDriverState *chr, Error **errp)
{
    TCPCharDriver *s = chr->opaque;
    QIOChannelSocket *sioc;

    /* It can't wait on s->connected, since it is set asynchronously
     * in TLS and telnet cases, only wait for an accepted socket */
    while (!s->ioc) {
        if (s->is_listen) {
            fprintf(stderr, "QEMU waiting for connection on: %s\n", chr->filename);
            qio_channel_set_blocking(QIO_CHANNEL(s->listen_ioc), true, NULL);
            tcp_chr_accept(QIO_CHANNEL(s->listen_ioc), G_IO_IN, chr);
            qio_channel_set_blocking(QIO_CHANNEL(s->listen_ioc), false, NULL);
        } else { /* spdk和dpdk，qemu是client，下面的就和异步连接一样了 */
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
```

