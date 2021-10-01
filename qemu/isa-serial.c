#define TYPE_ISA_SERIAL "isa-serial"


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


static gboolean serial_xmit(GIOChannel *chan, GIOCondition cond, void *opaque)
{
    SerialState *s = opaque;

    do {
        if (s->tsr_retry <= 0) {


            if (s->fcr & UART_FCR_FE) {
                s->tsr = fifo8_pop(&s->xmit_fifo);
                if (!s->xmit_fifo.num) {
                    s->lsr |= UART_LSR_THRE;
                }
            } else {
                s->tsr = s->thr;
                s->lsr |= UART_LSR_THRE;
            }
            if ((s->lsr & UART_LSR_THRE) && !s->thr_ipending) {
                s->thr_ipending = 1;
                serial_update_irq(s);
            }
        }

        if (s->mcr & UART_MCR_LOOP) {
            /* in loopback mode, say that we just received a char */
            serial_receive1(s, &s->tsr, 1);
        } else if (qemu_chr_fe_write(s->chr, &s->tsr, 1) != 1) {
            if (s->tsr_retry >= 0 && s->tsr_retry < MAX_XMIT_RETRY &&
                qemu_chr_fe_add_watch(s->chr, G_IO_OUT|G_IO_HUP,
                                      serial_xmit, s) > 0) {
                s->tsr_retry++;
                return FALSE;
            }
            s->tsr_retry = 0;
        } else {
            s->tsr_retry = 0;
        }

        /* Transmit another byte if it is already available. It is only
           possible when FIFO is enabled and not empty. */
    } while (!(s->lsr & UART_LSR_THRE));

    s->last_xmit_ts = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->lsr |= UART_LSR_TEMT;

    return FALSE;
}


static void serial_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    SerialState *s = opaque;

    addr &= 7;
    DPRINTF("write addr=0x%" HWADDR_PRIx " val=0x%" PRIx64 "\n", addr, val);
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0xff00) | val;
            serial_update_parameters(s);
        } else {
            s->thr = (uint8_t) val;
            if(s->fcr & UART_FCR_FE) {
                /* xmit overruns overwrite data, so make space if needed */
                if (fifo8_is_full(&s->xmit_fifo)) {
                    fifo8_pop(&s->xmit_fifo);
                }
                fifo8_push(&s->xmit_fifo, s->thr);
            }
            s->thr_ipending = 0;
            s->lsr &= ~UART_LSR_THRE;
            s->lsr &= ~UART_LSR_TEMT;
            serial_update_irq(s);
            if (s->tsr_retry <= 0) {
                serial_xmit(NULL, G_IO_OUT, s);
            }
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0x00ff) | (val << 8);
            serial_update_parameters(s);
        } else {
            uint8_t changed = (s->ier ^ val) & 0x0f;
            s->ier = val & 0x0f;
            /* If the backend device is a real serial port, turn polling of the modem
             * status lines on physical port on or off depending on UART_IER_MSI state.
             */
            if ((changed & UART_IER_MSI) && s->poll_msl >= 0) {
                if (s->ier & UART_IER_MSI) {
                     s->poll_msl = 1;
                     serial_update_msl(s);
                } else {
                     timer_del(s->modem_status_poll);
                     s->poll_msl = 0;
                }
            }

            /* Turning on the THRE interrupt on IER can trigger the interrupt
             * if LSR.THRE=1, even if it had been masked before by reading IIR.
             * This is not in the datasheet, but Windows relies on it.  It is
             * unclear if THRE has to be resampled every time THRI becomes
             * 1, or only on the rising edge.  Bochs does the latter, and Windows
             * always toggles IER to all zeroes and back to all ones, so do the
             * same.
             *
             * If IER.THRI is zero, thr_ipending is not used.  Set it to zero
             * so that the thr_ipending subsection is not migrated.
             */
            if (changed & UART_IER_THRI) {
                if ((s->ier & UART_IER_THRI) && (s->lsr & UART_LSR_THRE)) {
                    s->thr_ipending = 1;
                } else {
                    s->thr_ipending = 0;
                }
            }

            if (changed) {
                serial_update_irq(s);
            }
        }
        break;
    case 2:
        /* Did the enable/disable flag change? If so, make sure FIFOs get flushed */
        if ((val ^ s->fcr) & UART_FCR_FE) {
            val |= UART_FCR_XFR | UART_FCR_RFR;
        }

        /* FIFO clear */

        if (val & UART_FCR_RFR) {
            s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
            timer_del(s->fifo_timeout_timer);
            s->timeout_ipending = 0;
            fifo8_reset(&s->recv_fifo);
        }

        if (val & UART_FCR_XFR) {
            s->lsr |= UART_LSR_THRE;
            s->thr_ipending = 1;
            fifo8_reset(&s->xmit_fifo);
        }

        serial_write_fcr(s, val & 0xC9);
        serial_update_irq(s);
        break;
    case 3:
        {
            int break_enable;
            s->lcr = val;
            serial_update_parameters(s);
            break_enable = (val >> 6) & 1;
            if (break_enable != s->last_break_enable) {
                s->last_break_enable = break_enable;
                qemu_chr_fe_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_BREAK,
                               &break_enable);
            }
        }
        break;
    case 4:
        {
            int flags;
            int old_mcr = s->mcr;
            s->mcr = val & 0x1f;
            if (val & UART_MCR_LOOP)
                break;

            if (s->poll_msl >= 0 && old_mcr != s->mcr) {

                qemu_chr_fe_ioctl(s->chr,CHR_IOCTL_SERIAL_GET_TIOCM, &flags);

                flags &= ~(CHR_TIOCM_RTS | CHR_TIOCM_DTR);

                if (val & UART_MCR_RTS)
                    flags |= CHR_TIOCM_RTS;
                if (val & UART_MCR_DTR)
                    flags |= CHR_TIOCM_DTR;

                qemu_chr_fe_ioctl(s->chr,CHR_IOCTL_SERIAL_SET_TIOCM, &flags);
                /* Update the modem status after a one-character-send wait-time, since there may be a response
                   from the device/computer at the other end of the serial line */
                timer_mod(s->modem_status_poll, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->char_transmit_time);
            }
        }
        break;
    case 5:
        break;
    case 6:
        break;
    case 7:
        s->scr = val;
        break;
    }
}



const MemoryRegionOps serial_io_ops = {
    .read = serial_ioport_read,
    .write = serial_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void serial_isa_realizefn(DeviceState *dev, Error **errp)
{
    static int index;
    ISADevice *isadev = ISA_DEVICE(dev);
    ISASerialState *isa = ISA_SERIAL(dev);
    SerialState *s = &isa->state;

    if (isa->index == -1) {
        isa->index = index;
    }
    if (isa->index >= MAX_SERIAL_PORTS) {
        error_setg(errp, "Max. supported number of ISA serial ports is %d.",
                   MAX_SERIAL_PORTS);
        return;
    }
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


static void serial_isa_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = serial_isa_realizefn;
    dc->vmsd = &vmstate_isa_serial;
    dc->props = serial_isa_properties;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}



static const TypeInfo serial_isa_info = {
    .name          = TYPE_ISA_SERIAL,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISASerialState),
    .class_init    = serial_isa_class_initfn,
};

static void serial_register_types(void)
{
    type_register_static(&serial_isa_info);
}

type_init(serial_register_types)


/* pc_basic_device_init 会调用serial_hds_isa_init */

static void serial_isa_init(ISABus *bus, int index, CharDriverState *chr)
{
    DeviceState *dev;
    ISADevice *isadev;

    isadev = isa_create(bus, TYPE_ISA_SERIAL);
    dev = DEVICE(isadev);
    qdev_prop_set_uint32(dev, "index", index);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
}

void serial_hds_isa_init(ISABus *bus, int n)
{
    int i;

    for (i = 0; i < n; ++i) {

		/**
		 * if (foreach_device_config(DEV_SERIAL, serial_parse) < 0)
		 * serial_parse会给serial_hds赋值
		 */
        if (serial_hds[i]) {
            serial_isa_init(bus, i, serial_hds[i]);
        }
    }
}

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
            /*when log_buf full or '\n' write to file direct*/
            if ((log_cur >= LOG_BUF_SIZE - 1) || (buf[0] == '\n')) {
                if (write(s->log_fd, log_buf, log_cur) < 0) {
                    fprintf(stderr, "write fd[%d] failed, errno[%d]\n", s->log_fd, errno);
                }
                log_cur = 0;
            }
        }
    }

    return;
}


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


static void register_types(void)
{


	register_char_driver("pty", CHARDEV_BACKEND_KIND_PTY, NULL,
					 qemu_chr_open_pty);
}
