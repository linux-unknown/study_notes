# 母机中子机串口log文件分析

## 子机中串口信息

```c
[root@VM-146-254-centos ~]# cat /proc/tty/driver/serial 
serinfo:1.0 driver revision:
0: uart:16550A port:000003F8 irq:4 tx:19493 rx:0 RTS|CTS|DTR|DSR|CD
1: uart:unknown port:000002F8 irq:3
2: uart:unknown port:000003E8 irq:4
3: uart:unknown port:000002E8 irq:3
```

通过端口3f8进行串口的操作

## qemu 串口参数

```
-chardev pty,id=charserial0 
-device isa-serial,chardev=charserial0,id=serial0
```

## chardev处理

### register_types

```c
static void register_types(void)
{
	register_char_driver("pty", CHARDEV_BACKEND_KIND_PTY, NULL,
						qemu_chr_open_pty);
}
```

qeme启动的时候，在main函数中会解析参数，然后会调用到`qemu_chr_open_pty`

### qemu_chr_open_pty

```c
static CharDriverState *qemu_chr_open_pty(const char *id,
                                          ChardevBackend *backend,
                                          ChardevReturn *ret,
                                          Error **errp)
{
    CharDriverState *chr;
    PtyCharDriver *s;
    int master_fd, slave_fd;
    char pty_name[PATH_MAX];
    ChardevCommon *common = backend->u.pty.data;

    master_fd = qemu_openpty_raw(&slave_fd, pty_name);
   
    close(slave_fd);
    qemu_set_nonblock(master_fd);

    chr = qemu_chr_alloc(common, errp);

    chr->filename = g_strdup_printf("pty:%s", pty_name);
    ret->pty = g_strdup(pty_name);
    ret->has_pty = true;

    fprintf(stderr, "char device redirected to %s (label %s)\n",
            pty_name, id);

    s = g_new0(PtyCharDriver, 1);
    chr->opaque = s;
    chr->chr_write = pty_chr_write;
    chr->chr_update_read_handler = pty_chr_update_read_handler;
    chr->chr_close = pty_chr_close;
    chr->chr_add_watch = pty_chr_add_watch;
    chr->explicit_be_open = true;

    s->ioc = QIO_CHANNEL(qio_channel_file_new_fd(master_fd));
    s->timer_tag = 0;
    guest_log_open(s, O_RDWR|O_APPEND|O_CREAT);

    return chr;
}
```

### guest_log_open

```c
#define GUEST_LOG_DIR "/usr/local/var/log/libvirt/qemu"

static int guest_log_open(PtyCharDriver *s, int flags)
{
    int ret = 0;
    mode_t omask = 0;

    omask = umask(0);
    ret = mkdir(GUEST_LOG_DIR, S_IRWXU | S_IRWXG | S_IRWXO);
    if (ret < 0 && errno == EEXIST) {
    	ret = 0;
    }
    if (ret < 0) {
    	fprintf(stderr, "mkdir[%s] failed, errno[%d]\n", GUEST_LOG_DIR, errno);
    } else {
        char log_file[200];
        UuidInfo *info;
        char uuid[100];
        int fd;

        info = qmp_query_uuid(NULL);
        snprintf(uuid, sizeof(uuid), "%s", info->UUID);
        qapi_free_UuidInfo(info);
        snprintf(log_file, sizeof(log_file), "%s/%s_serial.log", GUEST_LOG_DIR, uuid);
        /* flags为 O_RDWR|O_APPEND|O_CREAT */
        fd = open(log_file, flags, S_IRWXU | S_IRWXG | S_IRWXO);
        if (fd > 0) {
            s->log_fd = fd;
        } else {
            s->log_fd = -1;
            ret = -1;
            fprintf(stderr, "open[%s] failed, errno[%d]\n", log_file, errno);
        }
    }
    umask(omask);

    return ret;
}
```

该函数会创建/usr/local/var/log/libvirt/qemu文件夹，并且在该目录下以uuid创建uuid-_serial.log文件。

## device处理

### serial_isa_info

```c
#define TYPE_ISA_SERIAL "isa-serial"

static const TypeInfo serial_isa_info = {
    .name          = TYPE_ISA_SERIAL,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISASerialState),
    .class_init    = serial_isa_class_initfn,
};
```

### serial_isa_class_initfn

```c
static void serial_isa_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = serial_isa_realizefn;
    dc->vmsd = &vmstate_isa_serial;
    dc->props = serial_isa_properties;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}
```

### serial_isa_realizefn

```c
/* 和子机中看到的端口信息也可以对上 */
static const int isa_serial_io[MAX_SERIAL_PORTS] = {
    0x3f8, 0x2f8, 0x3e8, 0x2e8
};
static const int isa_serial_irq[MAX_SERIAL_PORTS] = {
    4, 3, 4, 3
};

static void serial_isa_realizefn(DeviceState *dev, Error **errp)
{
    static int index;
    ISADevice *isadev = ISA_DEVICE(dev);
    ISASerialState *isa = ISA_SERIAL(dev);
    SerialState *s = &isa->state;

    /* 我们的参数中没有指定iobase和isairq，因此都有默认的 */
    if (isa->iobase == -1) {
        isa->iobase = isa_serial_io[isa->index];
    }
    if (isa->isairq == -1) {
        isa->isairq = isa_serial_irq[isa->index];
    }
    index++;

    s->baudbase = 115200;
    isa_init_irq(isadev, &s->irq, isa->isairq);
    serial_realize_core(s, errp);
    qdev_set_legacy_instance_id(dev, isa->iobase, 3);

    memory_region_init_io(&s->io, OBJECT(isa), &serial_io_ops, s, "serial", 8);
    isa_register_ioport(isadev, &s->io, isa->iobase);
}
```

#### isa_register_ioport

```c
void isa_register_ioport(ISADevice *dev, MemoryRegion *io, uint16_t start)
{
    memory_region_add_subregion(isabus->address_space_io, start, io);
    isa_init_ioport(dev, start);
}
```

serial_isa_realizefn函数中给s->io mr注册了ops：serial_io_ops

isa_register_ioport中会注册添加s->io mr到isabus->address_space_io中

## serial_io_ops

```c
const MemoryRegionOps serial_io_ops = {
    .read = serial_ioport_read,
    .write = serial_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};
```

### serial_ioport_write

模拟串口的寄存器

serial_ioport_write-->serial_xmit-->qemu_chr_fe_write


#### qemu_chr_fe_write

```c
int qemu_chr_fe_write(CharDriverState *s, const uint8_t *buf, int len)
{
    int ret;

    if (s->replay && replay_mode == REPLAY_MODE_PLAY) {
        int offset;
        replay_char_write_event_load(&ret, &offset);
        assert(offset <= len);
        qemu_chr_fe_write_buffer(s, buf, offset, &offset);
        return ret;
    }

    qemu_mutex_lock(&s->chr_write_lock);
	/* 调用 pty_chr_write */
    ret = s->chr_write(s, buf, len);

    if (ret > 0) {
        qemu_chr_fe_write_log(s, buf, ret);
    }

    qemu_mutex_unlock(&s->chr_write_lock);
    
    if (s->replay && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_write_event_save(ret, ret < 0 ? 0 : ret);
    }
    
    return ret;
}
```

#### pty_chr_write

```c
/* Called with chr_write_lock held.  */
static int pty_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    PtyCharDriver *s = chr->opaque;

    guest_log_write(s, buf, len);
    if (!s->connected) {
        /* guest sends data, check for (re-)connect */
        pty_chr_update_read_handler_locked(chr);
        if (!s->connected) {
            return 0;
        }
    }
    return io_channel_send(s->ioc, buf, len);
}
```

#### guest_log_write

```c
#define LOG_MAX_SIZE 2048000
#define SIZE_AJUST_PERIOD 256 
#define LOG_BUF_SIZE 200
static void guest_log_write(PtyCharDriver *s, const uint8_t *buf, int len)
{
    int ret = 0;

    if (s->log_fd > 0) {
        /*to improve perf, each SIZE_AJUST_PERIOD to recount the size*/
        s->period_count++;
        if ((s->period_count % SIZE_AJUST_PERIOD) == 0) {
            struct stat sb;
            int size;
            fstat(s->log_fd, &sb);
            size = sb.st_size;
            /* 如果超过最大值就重新打开,并且长度设置为0 */
            if (unlikely(size > LOG_MAX_SIZE)) {
                close(s->log_fd);
                s->log_fd = -1;
                ret = guest_log_open(s, O_RDWR|O_CREAT|O_TRUNC);
            }
        }
        if (ret == 0) {
            if (log_cur == 0) {
                time_t t; struct tm tm; char time_desc[50] = {0};
                time(&t); localtime_r(&t, &tm);
                strftime(time_desc, sizeof(time_desc), "[%Y-%m-%d %H:%M:%S]", &tm);
                log_cur += snprintf(log_buf, LOG_BUF_SIZE, "%s", time_desc);
            }
            if (unlikely(len > LOG_BUF_SIZE - log_cur - 1)) {
                len = LOG_BUF_SIZE - log_cur - 1;
            }
            memcpy(&log_buf[log_cur], buf, len);
            log_cur += len;
            /* when log_buf full or '\n' write to file direct 
             * log_buf满了或者buf中有\n,才会将buf写入文件
             */
            if ((log_cur >= LOG_BUF_SIZE - 1) || (buf[0] == '\n')) {
                /* 将buf写入到log文件中 */
                if (write(s->log_fd, log_buf, log_cur) < 0) {
                    fprintf(stderr, "write fd[%d] failed, errno[%d]\n", s->log_fd, errno);
                }
                log_cur = 0;
            }
        }
    }

    return;
}

```

