QemuOptsList qemu_chardev_opts = {
    .name = "chardev",
    .implied_opt_name = "backend", /* 隐式参数，如果参数没有使用=指定则表示backend= */
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

QemuOptsList qemu_device_opts = {
    .name = "device",
    .implied_opt_name = "driver",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_device_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting device properties
         */
        { /* end of list */ }
    },
};

struct QemuOpts {
    char *id;
    QemuOptsList *list;
    Location loc;
    QTAILQ_HEAD(QemuOptHead, QemuOpt) head;
    QTAILQ_ENTRY(QemuOpts) next;
};

struct QemuOpt {
    char *name;
    char *str;

    const QemuOptDesc *desc;
    union {
        bool boolean;
        uint64_t uint;
    } value;

    QemuOpts     *opts;
    QTAILQ_ENTRY(QemuOpt) next;
};


static QemuOptsList *vm_config_groups[48];


int main(int argc, char * argv [])
{

	qemu_add_opts(&qemu_chardev_opts);
    qemu_add_opts(&qemu_device_opts);

	switch(popt->index) {

	case QEMU_OPTION_chardev:
		opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"), optarg, true);
	break;

	case QEMU_OPTION_device:
		if (!qemu_opts_parse_noisily(qemu_find_opts("device"), optarg, true)) {
			exit(1);
		}
	break;

	}
}

void qemu_add_opts(QemuOptsList *list)
{
    int entries, i;

    entries = ARRAY_SIZE(vm_config_groups);
    entries--; /* keep list NULL terminated */
    for (i = 0; i < entries; i++) {
        if (vm_config_groups[i] == NULL) {
            vm_config_groups[i] = list;
            return;
        }
    }
    fprintf(stderr, "ran out of space in vm_config_groups");
    abort();
}


static QemuOptsList *find_list(QemuOptsList **lists, const char *group,
                               Error **errp)
{
    int i;

    for (i = 0; lists[i] != NULL; i++) {
        if (strcmp(lists[i]->name, group) == 0)
            break;
    }

    return lists[i];
}

QemuOptsList *qemu_find_opts(const char *group)
{
    QemuOptsList *ret;
    Error *local_err = NULL;

	/* 在vm_config_groups 中查找*/
    ret = find_list(vm_config_groups, group, &local_err);

    return ret;
}

QemuOpts *qemu_opts_parse_noisily(QemuOptsList *list, const char *params,
                                  bool permit_abbrev)
{
    Error *err = NULL;
    QemuOpts *opts;

    opts = opts_parse(list, params, permit_abbrev, false, &err);
    if (err) {
        error_report_err(err);
    }
    return opts;
}

static QemuOpts *opts_parse(QemuOptsList *list, const char *params,
                            bool permit_abbrev, bool defaults, Error **errp)
{
    const char *firstname;
    char value[1024], *id = NULL;
    const char *p;
    QemuOpts *opts;
    Error *local_err = NULL;

    assert(!permit_abbrev || list->implied_opt_name);
	/* implied_opt_name 为backend */
    firstname = permit_abbrev ? list->implied_opt_name : NULL;

    if (strncmp(params, "id=", 3) == 0) {
        get_opt_value(value, sizeof(value), params+3);
        id = value;
    } else if ((p = strstr(params, ",id=")) != NULL) {
        get_opt_value(value, sizeof(value), p+4);
        id = value;
    }

    /*
     * This code doesn't work for defaults && !list->merge_lists: when
     * params has no id=, and list has an element with !opts->id, it
     * appends a new element instead of returning the existing opts.
     * However, we got no use for this case.  Guard against possible
     * (if unlikely) future misuse:
     */
    assert(!defaults || list->merge_lists);
    opts = qemu_opts_create(list, id, !defaults, &local_err);

    opts_do_parse(opts, params, firstname, defaults, &local_err);

    return opts;
}

QemuOpts *qemu_opts_create(QemuOptsList *list, const char *id,
                           int fail_if_exists, Error **errp)
{
    QemuOpts *opts = NULL;

    if (id) {
        if (!id_wellformed(id)) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "id",
                       "an identifier");
            error_append_hint(errp, "Identifiers consist of letters, digits, "
                              "'-', '.', '_', starting with a letter.\n");
            return NULL;
        }
        opts = qemu_opts_find(list, id);
        if (opts != NULL) {
            if (fail_if_exists && !list->merge_lists) {
                error_setg(errp, "Duplicate ID '%s' for %s", id, list->name);
                return NULL;
            } else {
                return opts;
            }
        }
    } else if (list->merge_lists) {
        opts = qemu_opts_find(list, NULL);
        if (opts) {
            return opts;
        }
    }
	/* 创建 opts */
    opts = g_malloc0(sizeof(*opts));
    opts->id = g_strdup(id);
    opts->list = list;
    loc_save(&opts->loc);
    QTAILQ_INIT(&opts->head);
	/* 将 opts 添加到 QemuOptsList 对应的链表中 */
    QTAILQ_INSERT_TAIL(&list->head, opts, next);
    return opts;
}

static void opts_do_parse(QemuOpts *opts, const char *params,
                          const char *firstname, bool prepend, Error **errp)
{
    char option[128], value[1024];
    const char *p,*pe,*pc;
    Error *local_err = NULL;

    for (p = params; *p != '\0'; p++) {
        pe = strchr(p, '=');
        pc = strchr(p, ',');
        if (!pe || (pc && pc < pe)) {
            /* found "foo,more" */
            if (p == params && firstname) {
                /* implicitly named first option */
                pstrcpy(option, sizeof(option), firstname);
                p = get_opt_value(value, sizeof(value), p);
            } else {
                /* option without value, probably a flag */
                p = get_opt_name(option, sizeof(option), p, ',');
                if (strncmp(option, "no", 2) == 0) {
                    memmove(option, option+2, strlen(option+2)+1);
                    pstrcpy(value, sizeof(value), "off");
                } else {
                    pstrcpy(value, sizeof(value), "on");
                }
            }
        } else {
            /* found "foo=bar,more" */
            p = get_opt_name(option, sizeof(option), p, '=');
            if (*p != '=') {
                break;
            }
            p++;
            p = get_opt_value(value, sizeof(value), p);
        }
        if (strcmp(option, "id") != 0) {
			/* 如果不是id option，id 已经被解析过了 */
            /* store and parse */
            opt_set(opts, option, value, prepend, &local_err);
            if (local_err) {
                error_propagate(errp, local_err);
                return;
            }
        }
        if (*p != ',') {
            break;
        }
    }
}

static bool opts_accepts_any(const QemuOpts *opts)
{
    return opts->list->desc[0].name == NULL;
}

static void opt_set(QemuOpts *opts, const char *name, const char *value,
                    bool prepend, Error **errp)
{
    QemuOpt *opt;
    const QemuOptDesc *desc;
    Error *local_err = NULL;

    desc = find_desc_by_name(opts->list->desc, name);
    if (!desc && !opts_accepts_any(opts)) {
        error_setg(errp, QERR_INVALID_PARAMETER, name);
        return;
    }

	/* 创建opt */
    opt = g_malloc0(sizeof(*opt));
    opt->name = g_strdup(name);
    opt->opts = opts;
	/* 将opt添加到opts的链表中，opts的名字是id */
    if (prepend) {
        QTAILQ_INSERT_HEAD(&opts->head, opt, next);
    } else {
        QTAILQ_INSERT_TAIL(&opts->head, opt, next);
    }
    opt->desc = desc;
	/* 字符串形式的参数值 */
    opt->str = g_strdup(value);
	/* 解析opt参数的值 */
    qemu_opt_parse(opt, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        qemu_opt_del(opt);
    }
}

static void qemu_opt_parse(QemuOpt *opt, Error **errp)
{
    if (opt->desc == NULL)
        return;

    switch (opt->desc->type) {
    case QEMU_OPT_STRING:
        /* nothing */
        return;
    case QEMU_OPT_BOOL:
        parse_option_bool(opt->name, opt->str, &opt->value.boolean, errp);
        break;
    case QEMU_OPT_NUMBER:
        parse_option_number(opt->name, opt->str, &opt->value.uint, errp);
        break;
    case QEMU_OPT_SIZE:
        parse_option_size(opt->name, opt->str, &opt->value.uint, errp);
        break;
    default:
        abort();
    }
}

