# TypeInfo

在qemu中由很多设备的继承关系就是通过TypeInfo进行继承的

## TYPE_OBJECT

作为祖先的TypeInfo， **TYPE_OBJECT**
### register_types
```c
#define TYPE_OBJECT "object"

typedef struct TypeImpl *Type;
/* Type的具体实现？ */
struct TypeImpl
{
    const char *name;
	/* 类的大小 */
    size_t class_size;
	/* 实例的大小 */
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

struct ObjectClass
{
    /*< private >*/
    Type type;
    GSList *interfaces;

    const char *object_cast_cache[OBJECT_CLASS_CAST_CACHE];
    const char *class_cast_cache[OBJECT_CLASS_CAST_CACHE];

    ObjectUnparent *unparent;

    GHashTable *properties;
};
/* Object和内核的object一样，所有的都必须内嵌一个该结构体 */
struct Object
{
    /*< private >*/
    ObjectClass *class;
    ObjectFree *free;
    GHashTable *properties;
    uint32_t ref;
    Object *parent;
};

static void object_instance_init(Object *obj)
{
    /* 增加了一个名为type的属性，读取该属性会调用qdev_get_type函数 */
    object_property_add_str(obj, "type", qdev_get_type, NULL, NULL);
}

static void register_types(void)
{
	/* 最终的父TYPE_OBJECT */
    static TypeInfo object_info = {
        .name = TYPE_OBJECT,
        .instance_size = sizeof(Object),
        /* 实例初始化 */
        .instance_init = object_instance_init,
        /* 表示该 TypeInfo是一个抽象设备，必须被其他人继承 */
        .abstract = true,
    };
   type_register_internal(&object_info);
}
type_init(register_types);/* type_init 修饰的函数会在main之前调用 */
```

### type_register_internal

```c
static TypeImpl *type_register_internal(const TypeInfo *info)
{
    TypeImpl *ti;
    /* 根据TypeInfo创建出TypeImpl */
    ti = type_new(info);
    type_table_add(ti);
    return ti;
}
```

### type_new

```c
static TypeImpl *type_new(const TypeInfo *info)
{
    TypeImpl *ti = g_malloc0(sizeof(*ti));
    int i;
	/* 不能重复 */
    if (type_table_lookup(info->name) != NULL) {
        fprintf(stderr, "Registering `%s' which already exists\n", info->name);
        abort();
    }
	/* 初始化 TypeImpl，最终其实使用的是TypeImpl，TypeInfo只是用来注册 */
    ti->name = g_strdup(info->name);
    ti->parent = g_strdup(info->parent);

    ti->class_size = info->class_size;
    ti->instance_size = info->instance_size;

    ti->class_init = info->class_init;
    ti->class_base_init = info->class_base_init;
    ti->class_finalize = info->class_finalize;
    ti->class_data = info->class_data;

    ti->instance_init = info->instance_init;
    ti->instance_post_init = info->instance_post_init;
    ti->instance_finalize = info->instance_finalize;

    ti->abstract = info->abstract;

    for (i = 0; info->interfaces && info->interfaces[i].type; i++) {
        ti->interfaces[i].typename = g_strdup(info->interfaces[i].type);
    }
    ti->num_interfaces = i;

    return ti;
}
```

### type_table_add

```c
static GHashTable *type_table_get(void)
{
    static GHashTable *type_table;
	/* 如果为NULL，则创建一个 */
    if (type_table == NULL) {
        type_table = g_hash_table_new(g_str_hash, g_str_equal);
    }
    return type_table;
}

static void type_table_add(TypeImpl *ti)
{
	/* 将ti添加到hash表中 */
    g_hash_table_insert(type_table_get(), (void *)ti->name, ti);
}
```

## TYPE_DEVICE

TYPE_DEVICE是所有device的最终父device

```c
static const TypeInfo device_type_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_OBJECT, /* 父设备为 TYPE_OBJECT */
    .instance_size = sizeof(DeviceState),
    /* object_init_with_type 会递归调用父的device_initfn，首先调用根的instance_init */
    .instance_init = device_initfn,
    .instance_post_init = device_post_init,
    .instance_finalize = device_finalize,
    .class_base_init = device_class_base_init,
    .class_init = device_class_init,
    .abstract = true,
    .class_size = sizeof(DeviceClass),
};

static void qdev_register_types(void)
{
    type_register_static(&bus_info);
    type_register_static(&device_type_info);
}

type_init(qdev_register_types);
```

### device_initfn

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

	/* 添加 realized 属性，当执行 
	* object_property_set_bool(OBJECT(dev), true, "realized", &err); 
	* 就会调用obj中的realized属性对应的set函数device_set_realized
	*/
    object_property_add_bool(obj, "realized", device_get_realized, device_set_realized, NULL);
    object_property_add_bool(obj, "hotpluggable", device_get_hotpluggable, NULL, NULL);
    object_property_add_bool(obj, "hotplugged", device_get_hotplugged, device_set_hotplugged,
                             &error_abort);

    class = object_get_class(OBJECT(dev));
    do {
		/* 添加ObjectClass冲的属性 */
        for (prop = DEVICE_CLASS(class)->props; prop && prop->name; prop++) {
            qdev_property_add_legacy(dev, prop, &error_abort);
            qdev_property_add_static(dev, prop, &error_abort);
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));

    object_property_add_link(OBJECT(dev), "parent_bus", TYPE_BUS,
                             (Object **)&dev->parent_bus, NULL, 0,
                             &error_abort);
    QLIST_INIT(&dev->gpios);
}
```

#### object_property_add_bool

```c
void object_property_add_bool(Object *obj, const char *name, bool (*get)(Object *, Error **),
                              void (*set)(Object *, bool, Error **), Error **errp)
{
    Error *local_err = NULL;
    /* 分配一个prop内存 */
    BoolProperty *prop = g_malloc0(sizeof(*prop));
	/* 设置get和set函数 */
    prop->get = get;
    prop->set = set;

    object_property_add(obj, name, "bool", get ? property_get_bool : NULL, set ? property_set_bool : NULL,
                        property_release_bool, prop, &local_err);
}
```

#### object_property_add

```c
ObjectProperty *
object_property_add(Object *obj, const char *name, const char *type, ObjectPropertyAccessor *get,
                    ObjectPropertyAccessor *set, ObjectPropertyRelease *release,
                    void *opaque, Error **errp)
{
    ObjectProperty *prop;
    size_t name_len = strlen(name);

    if (name_len >= 3 && !memcmp(name + name_len - 3, "[*]", 4)) {
    }
	/* 分配ObjectProperty内存 */
    prop = g_malloc0(sizeof(*prop));

    prop->name = g_strdup(name);
    prop->type = g_strdup(type);
	/* 设置get set函数 */
    prop->get = get;
    prop->set = set;
    prop->release = release;
    prop->opaque = opaque; /* opaque就是BoolProperty */
	/* 将prop添加到obj的properties链表中 */
    g_hash_table_insert(obj->properties, prop->name, prop);
    return prop;
}
```

##### property_set_bool

```c
static void property_set_bool(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    BoolProperty *prop = opaque;
    bool value;
    Error *local_err = NULL;

    visit_type_bool(v, name, &value, &local_err);
	/* 最终调用 device_set_realized */
    prop->set(obj, value, errp);
}
```

## qdev_device_add

qdev_device_add会根据-device参数添加device

```c
-device vhost-user-blk-pci,chardev=drive-virtio-disk5,num-queues=4, bus=pci.0,addr=0x5,id=virtio-disk5
driver是隐式参数，不带=就表示是driver的参数，即driver=vhost-user-blk-pci
```
在main函数中寻找device参数，对该参数调用device_init_func
```c
if (qemu_opts_foreach(qemu_find_opts("device"),
	device_init_func, NULL, NULL)) {
	exit(1);
 }
```

device_init_func-->qdev_device_add

```c
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
	/* 返回DeviceClass，该函数会执行class_init函数 */
    dc = qdev_get_device_class(&driver, errp);

    /* find bus */
    path = qemu_opt_get(opts, "bus");
    if (path != NULL) {
		/* 后面在分析 */
        bus = qbus_find(path, errp);

        if (!object_dynamic_cast(OBJECT(bus), dc->bus_type)) {
            error_setg(errp, "Device '%s' can't go on %s bus",
                       driver, object_get_typename(OBJECT(bus)));
            return NULL;
        }
    } else if (dc->bus_type != NULL) {
        bus = qbus_find_recursive(sysbus_get_default(), NULL, dc->bus_type);
        if (!bus || qbus_is_full(bus)) {
            error_setg(errp, "No '%s' bus found for device '%s'",
                       dc->bus_type, driver);
            return NULL;
        }
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
        object_property_add_child(qdev_get_peripheral(), dev->id, OBJECT(dev), NULL);
    } else {
        static int anon_count;
        gchar *name = g_strdup_printf("device[%d]", anon_count++);
        object_property_add_child(qdev_get_peripheral_anon(), name, OBJECT(dev), NULL);
        g_free(name);
    }

    /* set properties */
    if (qemu_opt_foreach(opts, set_property, dev, &err)) {
        error_propagate(errp, err);
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        return NULL;
    }

    dev->opts = opts;
    object_property_set_bool(OBJECT(dev), true, "realized", &err);
   
    return dev;
}
```

### class初始化

```c
static DeviceClass *qdev_get_device_class(const char **driver, Error **errp)
{
    ObjectClass *oc;
    DeviceClass *dc;
    const char *original_name = *driver;
	/* 根据driver名字进行查找 */
    oc = object_class_by_name(*driver);
    if (!oc) {
        const char *typename = find_typename_by_alias(*driver);
        if (typename) {
            *driver = typename;
            oc = object_class_by_name(*driver);
        }
    }

    if (!object_class_dynamic_cast(oc, TYPE_DEVICE)) {
        if (*driver != original_name) {
            error_setg(errp, "'%s' (alias '%s') is not a valid device model"
                       " name", original_name, *driver);
        } else {
            error_setg(errp, "'%s' is not a valid device model name", *driver);
        }
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver", "non-abstract device type");
        return NULL;
    }

    dc = DEVICE_CLASS(oc);
    if (dc->cannot_instantiate_with_device_add_yet || (qdev_hotplug && !dc->hotpluggable)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver", "pluggable device type");
        return NULL;
    }

    return dc;
}
```

### object_class_by_name

```c
ObjectClass *object_class_by_name(const char *typename)
{
    /* g_hash_table_lookup(type_table_get(), name);根据名称在哈希表中进行查找 */
    TypeImpl *type = type_get_by_name(typename);
	/* type初始化 */
    type_initialize(type);

    return type->class;
}
```

### type_initialize

```c
static void type_initialize(TypeImpl *ti)
{
    TypeImpl *parent;
	/* 如果已经初始话就返回 */
    if (ti->class) { return; }
	/* 如果该ti的class_size为0，则返回父ti的class_size */
    ti->class_size = type_class_get_size(ti);
    /* 和class_size类似 */
    ti->instance_size = type_object_get_size(ti);
	/* 分配class内存 */
    ti->class = g_malloc0(ti->class_size);
	/* 获取父ti */
    parent = type_get_parent(ti);
    if (parent) {
		/* 递归初始化父 type */
        type_initialize(parent);
        /* 递归调用返回，从这里开始一层层返回执行 */
        GSList *e;
        int i;
  
		/* 将父 class 拷贝到该 class中， 根class是没有parent的，
		 * type_initialize首先分配class_size内存，class_size包含
		 * 父class的大小，所以在class_init中可以直接使用DEVICE_CLASS宏进行转换
		 * memcpy之后只有最后的一个儿子的class_size有用，父的class_size都没有用了
		 * 父ti先执行class_init，然后将父初始化过的class数据在拷贝到子的class中
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
    } else { /* parent为NULL */
        ti->class->properties = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free, object_property_free);
    }
	/* class和ti联系起来 */
    ti->class->type = ti;

    while (parent) {
        if (parent->class_base_init) {
            parent->class_base_init(ti->class, ti->class_data);
        }
        parent = type_get_parent(parent);
    }

    if (ti->class_init) {
		/* 调用class_init，递归调用都会调用该函数。因为已经将父class_size进行memcpy
         * 所以可以在class_init中进行类型转换
         */
        ti->class_init(ti->class, ti->class_data);
    }
}
```

在定义TypeInfo的时候，该TypeInfo的class_size会包含父class_size，这样一层层包含上去，最终的class_size只包含ObjectClass，这样就可以通过ObjectClass进行地址转换，实现类似与面对对象的继承。比如下面的结构体，VirtioPCIClass继承PCIDeviceClass继承PCIDeviceClass，每个结构体都将父结构体作为该结构体的第一个成员。所以TYPE_VIRTIO_PCI的class_size就包含了所有父类，通过ObjectClass根据进行指针类型转换就可以访问到不同的父 class。

```c
typedef struct DeviceClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/s;
} DeviceClass;

typedef struct PCIDeviceClass {
    DeviceClass parent_class;
} PCIDeviceClass;

typedef struct VirtioPCIClass {
    PCIDeviceClass parent_class;
} VirtioPCIClass

```

#### type_class_get_size

```c
static size_t type_class_get_size(TypeImpl *ti)
{
    if (ti->class_size) {
        return ti->class_size;
    }
 	/* 如果该ti class_size为0，如果有父type，递归调用 */
    if (type_has_parent(ti)) { 
        return type_class_get_size(type_get_parent(ti));
    }
    return sizeof(ObjectClass);
}
```

### object_new即device创建

`dev = DEVICE(object_new(driver));`创建device

driver在本例中为driver=vhost-user-blk-pci

```c
Object *object_new(const char *typename)
{
    /* 在哈希表中寻找 g_hash_table_lookup(type_table_get(), name) */
    TypeImpl *ti = type_get_by_name(typename);
	/* 递归调用，分配instance_size，然后调用 instance_init */
    return object_new_with_type(ti);
}
```

### object_new_with_type

```c
Object *object_new_with_type(Type type)
{
    Object *obj;
	/* 会递归调用class_init函数 */
    type_initialize(type);
	/* 分配instance_size内存， instance_size只分配一次 */
    obj = g_malloc(type->instance_size);
    object_initialize_with_type(obj, type->instance_size, type);
    obj->free = g_free;

    return obj;
}
```

### object_initialize_with_type

```c
void object_initialize_with_type(void *data, size_t size, TypeImpl *type)
{
    Object *obj = data;
	/* 递归调用class_init */
    type_initialize(type);

    memset(obj, 0, type->instance_size);
    /* class通过ObjectClass进行父子继承关系转换， device通过Object进行父子关系转换
     * 将type的class赋值给obj的class，两者通过class关联起来 
     */
    obj->class = type->class;
    object_ref(obj);
    obj->properties = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, object_property_free);
	/* 递归调用父 instance_init */
    object_init_with_type(obj, type);
    object_post_init_with_type(obj, type);
}
```

### object_init_with_type

```c
static void object_init_with_type(Object *obj, TypeImpl *ti)
{
    /* 如果有父TypeImpl，递归调用 */
    if (type_has_parent(ti)) {
        object_init_with_type(obj, type_get_parent(ti));
    }
	/* 调用instance_init，先调用父的instance_init */
    if (ti->instance_init) {
        ti->instance_init(obj);
    }
}
```



type_initialize 递归的调用class_init

object_new会创建调用type_initialize 和 object_new_with_type，object_new_with_type会递归调用instance_init