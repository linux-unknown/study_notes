# qemu参数解析

## qemu_add_opts

```
-chardev socket,id=drive-virtio-disk6,path=/var/run/spdk/vhost_blk_socket-0397bf20-daff-476e-a2dd-564daf1e5f55-nvme,reconnect=10
```

在上面的参数中，-chardev，称为参数的名称，此文将-chardev 后面的参数称作**补充参数**。

在main函数中会调用qemu_add_opts把数据类型为QemuOptsList添加到全局的vm_config_groups中

```c
/* qemu最多支持47个这样的参数，因为最后一个为NULL */
static QemuOptsList *vm_config_groups[48];

void qemu_add_opts(QemuOptsList *list)
{
    int entries, i;

    entries = ARRAY_SIZE(vm_config_groups);
    entries--; /* keep list NULL terminated,确保最后一个是NULL */
    for (i = 0; i < entries; i++) {
        if (vm_config_groups[i] == NULL) {
            vm_config_groups[i] = list;
            return;
        }
    }
}

qemu_add_opts(&qemu_chardev_opts);
```

## QemuOptsList结构体

```c
struct QemuOptsList {
    const char *name;/* 参数名称，在调用qemu中传入的，该参数后面还会有很多参数对该参数进行补充说明 */
    const char *implied_opt_name;/* 补充参数的隐式参数名称，即没有使用=指定参数名称，只有值的 */
    bool merge_lists;  /* Merge multiple uses of option into a single list? */
    QTAILQ_HEAD(, QemuOpts) head; /* 补充说明参数的链表 */
    QemuOptDesc desc[];/* 补充参数的描述 */
};
```

### qemu_chardev_opts

```c
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
```

### chardev参数

```
-chardev socket,id=drive-virtio-disk6,path=/var/run/spdk/vhost_blk_socket-0397bf20-daff-476e-a2dd-564daf1e5f55-nvme,reconnect=10
```

上面是libvirt调用qemu传的chardev参数。

以“-”指定参数名称，以“，”进行补充参数的分割。socket并没有使用“=”进行赋值，则表示其是隐式参数，表示backend=socket，后面的补充说明参数，都是以“=”号进行赋值

## 参数的解析

在main函数中

```c
optind = 1;
for(;;) {
	if (argv[optind][0] != '-') {
		hda_opts = drive_add(IF_DEFAULT, 0, argv[optind++], HD_OPTS);
	} else {
		const QEMUOption *popt;
		popt = lookup_opt(argc, argv, &optarg, &optind);
	}
	switch(popt->index) {
	case QEMU_OPTION_chardev:
		opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"), optarg, true);
		break;
	}
}
```

lookup_opt 会对argv参数进行解析，解析出来一个index。

解析的时候会用到下面的变量

```c
static const QEMUOption qemu_options[] = {
    { "h", 0, QEMU_OPTION_h, QEMU_ARCH_ALL },
#define QEMU_OPTIONS_GENERATE_OPTIONS
    /* qemu-options.hx生成qemu-options.def，qemu-options-wrapper.h是宏定义，然后会包含qemu-options.def 
     * 
     */
#include "qemu-options-wrapper.h"  
    { NULL },
};
```

### qemu-options.def 

```
DEF("chardev", HAS_ARG, QEMU_OPTION_chardev,
"-chardev null,id=id[,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
"-chardev socket,id=id[,host=host],port=port[,to=to][,ipv4][,ipv6][,nodelay][,reconnect=seconds]\n"
"         [,server][,nowait][,telnet][,reconnect=seconds][,mux=on|off]\n"
"         [,logfile=PATH][,logappend=on|off][,tls-creds=ID] (tcp)\n"
"-chardev socket,id=id,path=path[,server][,nowait][,telnet][,reconnect=seconds]\n"
"         [,mux=on|off][,logfile=PATH][,logappend=on|off] (unix)\n"
"-chardev udp,id=id[,host=host],port=port[,localaddr=localaddr]\n"
"         [,localport=localport][,ipv4][,ipv6][,mux=on|off]\n"
"         [,logfile=PATH][,logappend=on|off]\n"
```

```c
#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask)     \
    { option, opt_arg, opt_enum, arch_mask },
```

DEF给结构体qemu_options数组元素赋值。

```c
typedef struct QEMUOption {
    const char *name; 		/* 名称 chardev */
    int flags;		
    int index;			 /* index QEMU_OPTION_chardev */
    uint32_t arch_mask;
} QEMUOption;
```

### qemu_find_opts

```c
static QemuOptsList *find_list(QemuOptsList **lists, const char *group, Error **errp)
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
	/* 在vm_config_groups 中查找QemuOptsList */
    ret = find_list(vm_config_groups, group, &local_err);
    return ret;
}
```

### qemu_opts_parse_noisily

以下面的参数为例子

```
-chardev socket,id=drive-virtio-disk6,path=/var/run/spdk/vhost_blk_socket-0397bf20-daff-476e-a2dd-564daf1e5f55-nvme,reconnect=10
```

```c
QemuOpts *qemu_opts_parse_noisily(QemuOptsList *list, const char *params, bool permit_abbrev)
{
    Error *err = NULL;
    QemuOpts *opts;
    opts = opts_parse(list, params, permit_abbrev, false, &err);
    return opts;
}
```

### opts_parse
```c
static QemuOpts *opts_parse(QemuOptsList *list, const char *params,
                            bool permit_abbrev, bool defaults, Error **errp)
{
    const char *firstname;
    char value[1024], *id = NULL;
    const char *p;
    QemuOpts *opts;
    Error *local_err = NULL;

	/* permit_abbrev:是否允许简写。implied_opt_name 为backend */
    firstname = permit_abbrev ? list->implied_opt_name : NULL;
	/* 查找id */
    if (strncmp(params, "id=", 3) == 0) {
        get_opt_value(value, sizeof(value), params+3);/* 将id=后面的值赋值给value */
        id = value;
    } else if ((p = strstr(params, ",id=")) != NULL) { /* 我们的属于这一招 */
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
    opts = qemu_opts_create(list, id, !defaults, &local_err);
	/* firstname即隐式参数 */
    opts_do_parse(opts, params, firstname, defaults, &local_err);
    return opts;
}
```

### qemu_opts_create

```c
QemuOpts *qemu_opts_find(QemuOptsList *list, const char *id)
{
    QemuOpts *opts;
    QTAILQ_FOREACH(opts, &list->head, next) {
        if (!opts->id && !id) {
            return opts;
        }
        if (opts->id && id && !strcmp(opts->id, id)) {
            return opts;
        }
    }
    return NULL;
}

QemuOpts *qemu_opts_create(QemuOptsList *list, const char *id, int fail_if_exists, Error **errp)
{
    QemuOpts *opts = NULL;
    if (id) {
        /* 查找list中的head链表中查找id是否存在，QemuOptsList的head链表用来保存补充说明参数 */
        opts = qemu_opts_find(list, id);
        if (opts != NULL) {/* 相同的参数，id补充参数需要唯一 */
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
```



```c
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
            /* 将参数保存到value中 */
            p = get_opt_value(value, sizeof(value), p);
        }
        if (strcmp(option, "id") != 0) {
			/* 如果不是id option，id已经被解析过了 */
            /* store and parse */
            opt_set(opts, option, value, prepend, &local_err);
        }
        if (*p != ',') {
            break;
        }
    }
}
```

### opt_set

```c
static const QemuOptDesc *find_desc_by_name(const QemuOptDesc *desc, const char *name)
{
    int i;
    for (i = 0; desc[i].name != NULL; i++) {
        if (strcmp(desc[i].name, name) == 0) {
            return &desc[i];
        }
    }
    return NULL;
}

static void opt_set(QemuOpts *opts, const char *name, const char *value, bool prepend, Error **errp)
{
    QemuOpt *opt;
    const QemuOptDesc *desc;
    Error *local_err = NULL;
	/* 在opts->list->des中查找补充参数的名称 */
    desc = find_desc_by_name(opts->list->desc, name);
	/* 创建opt */
    opt = g_malloc0(sizeof(*opt));
    opt->name = g_strdup(name);
    opt->opts = opts;
	/* 将opt添加到opts的链表中，opts就是id对应的opts */
    if (prepend) {
        QTAILQ_INSERT_HEAD(&opts->head, opt, next);
    } else {
        QTAILQ_INSERT_TAIL(&opts->head, opt, next);
    }
    opt->desc = desc;
	/* 字符串形式的参数值 */
    opt->str = g_strdup(value);
	/* 解析opt参数的值,将字符串形式的值解析为对应类型的值 */
    qemu_opt_parse(opt, &local_err);
}
```

### qemu_opt_parse

```c
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
```

通过上面的分析可以看到qemu中对参数的保存如下：

QemuOptsList-->head--->opts(id=xxxx)-------------------------->opts(id=xxxy)

​												\\-->opt(path=)-->opt(reconnect)      \\-->opt(path=)-->opt(reconnect)

