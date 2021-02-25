# qemu中chardev和device关联

以下面的参数为例

```c
/* chardev */
-chardev socket,id=drive-virtio-disk7,path=/var/run/spdk/vhost_blk_socket-1aa6d816-2046-4dfe-80b9-f5740aef8d47-nvme,reconnect=10
/* device */
-device vhost-user-blk-pci,chardev=drive-virtio-disk7,num-queues=4,bus=pci.0,addr=0x7,id=virtio-disk7 	 
```

## vhost_user_blk_properties

```c
static Property vhost_user_blk_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBlk, chardev),
    DEFINE_PROP_UINT16("num-queues", VHostUserBlk, num_queues, 1),
    DEFINE_PROP_UINT32("queue-size", VHostUserBlk, queue_size, 128),
    DEFINE_PROP_BIT("config-wce", VHostUserBlk, config_wce, 0, true),
    DEFINE_PROP_BIT("config-ro", VHostUserBlk, config_ro, 0, false),
    DEFINE_PROP_END_OF_LIST(),
};
```

我们主要看` DEFINE_PROP_CHR("chardev", VHostUserBlk, chardev),`的定义

```c++
#define DEFINE_PROP_CHR(_n, _s, _f)             \
    DEFINE_PROP(_n, _s, _f, qdev_prop_chr, CharDriverState*)
```

```c++
#define DEFINE_PROP(_name, _state, _field, _prop, _type) { \
        .name      = (_name),                                    \
        .info      = &(_prop),                                   \
        .offset    = offsetof(_state, _field)                    \
            + type_check(_type, typeof_field(_state, _field)),   \
        }
```

```c
PropertyInfo qdev_prop_chr = {
    .name  = "str",
    .description = "ID of a chardev to use as a backend",
    .get   = get_chr,
    .set   = set_chr,
    .release = release_chr,
};
```

其实就是定义了一个Property

```c
static Property vhost_user_blk_properties[0] = {
	.name = chardev,
	.info = qdev_prop_chr,
	/* chardev在VHostUserBlk中的偏移，chardev类型为CharDriverState* */
	.offset = offsetof(VHostUserBlk, chardev), 
}
```

## vhost_user_blk_class_init

在vhost_user_blk_class_init中会把vhost_user_blk_properties赋值给dc->props

```c
static void vhost_user_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = vhost_user_blk_properties;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_user_blk_device_realize;
}
```

## qdev_alias_all_properties

vhost_user_blk_pci_instance_init-->virtio_instance_init_common-->qdev_alias_all_properties

qdev_alias_all_properties会把dc->props添加到Object中

```c
void qdev_alias_all_properties(DeviceState *target, Object *source)
{
    ObjectClass *class;
    Property *prop;

    class = object_get_class(OBJECT(target));
    do {
        DeviceClass *dc = DEVICE_CLASS(class);

        for (prop = dc->props; prop && prop->name; prop++) {
            /* 添加alias属性 */
            object_property_add_alias(source, prop->name, OBJECT(target), prop->name, &error_abort);
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));
}
```

### object_property_add_alias

这里只是添加了一个alias属性，通过该属性可以调用到target中的同名属性

```c
void object_property_add_alias(Object *obj, const char *name, Object *target_obj, const char *target_name,
                               Error **errp)
{
    AliasProperty *prop;
    ObjectProperty *op;
    ObjectProperty *target_prop;
    gchar *prop_type;
    Error *local_err = NULL;

    target_prop = object_property_find(target_obj, target_name, errp);

    if (object_property_is_child(target_prop)) {
        prop_type = g_strdup_printf("link%s", target_prop->type + strlen("child"));
    } else {
        prop_type = g_strdup(target_prop->type);
    }

    prop = g_malloc(sizeof(*prop));
    prop->target_obj = target_obj;
    prop->target_name = g_strdup(target_name);
	/* 该属性的set函数为property_set_alias */
    op = object_property_add(obj, name, prop_type, property_get_alias, property_set_alias,
                             property_release_alias, prop, &local_err);

    op->resolve = property_resolve_alias;

    object_property_set_description(obj, op->name, target_prop->description, &error_abort);

out:
    g_free(prop_type);
}
```

```
static void property_set_alias(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    AliasProperty *prop = opaque;
    object_property_set(prop->target_obj, v, prop->target_name, errp);
}
```
#### object_property_set
```c
void object_property_set(Object *obj, Visitor *v, const char *name, Error **errp)
{
    /* 获取target_obj的属性，然后执行该属性的set函数，target的属性是在device_initfn的时候添加的，
     * 即instance_init的时候 
     */
    ObjectProperty *prop = object_property_find(obj, name, errp);
    if (!prop->set) {
        error_setg(errp, QERR_PERMISSION_DENIED);
    } else {
        prop->set(obj, v, name, prop->opaque, errp);
    }
}
```

## device_initfn

```c
static void device_initfn(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    ObjectClass *class;
    Property *prop;

    if (qdev_hotplug) {
        dev->hotplugged = 1;
        qdev_hot_added = true;
    }

    dev->instance_id_alias = -1;
    dev->realized = false;

	/* 添加 realized 属性 */
    object_property_add_bool(obj, "realized", device_get_realized, device_set_realized, NULL);
    object_property_add_bool(obj, "hotpluggable", device_get_hotpluggable, NULL, NULL);
    object_property_add_bool(obj, "hotplugged", device_get_hotplugged, device_set_hotplugged, &error_abort);

    /* class_init会先执行，然后在执行instance_init */
    class = object_get_class(OBJECT(dev));
    do {
        for (prop = DEVICE_CLASS(class)->props; prop && prop->name; prop++) {
            qdev_property_add_legacy(dev, prop, &error_abort);
            qdev_property_add_static(dev, prop, &error_abort);
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));

    object_property_add_link(OBJECT(dev), "parent_bus", TYPE_BUS, (Object **)&dev->parent_bus, NULL, 0,
                             &error_abort);
    QLIST_INIT(&dev->gpios);
}

```

### qdev_property_add_static

```c
void qdev_property_add_static(DeviceState *dev, Property *prop,
                              Error **errp)
{
    Error *local_err = NULL;
    Object *obj = OBJECT(dev);
	/*
     * 根据 DEFINE_PROP_CHR("chardev", VHostUserBlk, chardev),定义
	 * 属性的名称是chardev，type是prop->info->name，即str 
	 */
    object_property_add(obj, prop->name, prop->info->name, prop->info->get, prop->info->set,
                        prop->info->release, prop, &local_err);

    object_property_set_description(obj, prop->name, prop->info->description, &error_abort);

    if (prop->qtype == QTYPE_NONE) {
        return;
    }

    if (prop->qtype == QTYPE_QBOOL) {
        object_property_set_bool(obj, prop->defval, prop->name, &error_abort);
    } else if (prop->info->enum_table) {
        object_property_set_str(obj, prop->info->enum_table[prop->defval],
                                prop->name, &error_abort);
    } else if (prop->qtype == QTYPE_QINT) {
        object_property_set_int(obj, prop->defval, prop->name, &error_abort);
    }
}
```

## device_init_func

device_init_func-->qdev_device_add->

```c
/* set properties 对opts中的没个ops参数，调用 set_property 函数 */
if (qemu_opt_foreach(opts, set_property, dev, &err)) {
    error_propagate(errp, err);
    object_unparent(OBJECT(dev));
    object_unref(OBJECT(dev));
    return NULL;
}
```
### set_property
```c
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
	*	};
	* qdev_alias_all_properties中会把dc->props的属性添加到obj中
	*/
    object_property_parse(obj, value, name, &err);

    return 0;
}
```
#### object_property_parse
```c
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
```

#### set_chr

```c
static void set_chr(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    set_pointer(obj, v, opaque, parse_chr, name, errp);
}

static void set_pointer(Object *obj, Visitor *v, Property *prop,
                        void (*parse)(DeviceState *dev, const char *str, void **ptr, const char *propname,
                                      Error **errp), const char *name, Error **errp)
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

    /* 调用 parse_chr */
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
```

#### parse_chr

```c
static void parse_chr(DeviceState *dev, const char *str, void **ptr, const char *propname, Error **errp)
{
	/* 根据chardev=drive-virtio-disk6的drive-virtio-disk6查找CharDriverState */
    CharDriverState *chr = qemu_chr_find(str);
  
   /* 将chr 赋值给 VHostUserBlk->chardev 
	* 这样CharDriverState和VHostUserBlk就联系起来了
	*/
    *ptr = chr;
}
```

