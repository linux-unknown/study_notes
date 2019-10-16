struct phy *phy_create(struct device *dev, struct device_node *node,
		       const struct phy_ops *ops)
{
	int ret;
	int id;
	struct phy *phy;

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	phy = kzalloc(sizeof(*phy), GFP_KERNEL);

	id = ida_simple_get(&phy_ida, 0, 0, GFP_KERNEL);


	device_initialize(&phy->dev);
	mutex_init(&phy->mutex);

	phy->dev.class = phy_class;
	phy->dev.parent = dev;
	phy->dev.of_node = node ?: dev->of_node;
	phy->id = id;
	phy->ops = ops;

	ret = dev_set_name(&phy->dev, "phy-%s.%d", dev_name(dev), id);

	/* phy-supply */
	phy->pwr = regulator_get_optional(&phy->dev, "phy");

	/*将phy注册到device设备模型中*/
	ret = device_add(&phy->dev);

	if (pm_runtime_enabled(dev)) {
		pm_runtime_enable(&phy->dev);
		pm_runtime_no_callbacks(&phy->dev);
	}

	return phy;
}
EXPORT_SYMBOL_GPL(phy_create);


/**
 * 在调用devm_of_phy_provider_register之前会用phy_create创建好phy
 */
#define	devm_of_phy_provider_register(dev, xlate)	\
	__devm_of_phy_provider_register((dev), NULL, THIS_MODULE, (xlate))

struct phy_provider *__devm_of_phy_provider_register(struct device *dev,
	struct device_node *children, struct module *owner,
	struct phy * (*of_xlate)(struct device *dev,
				 struct of_phandle_args *args))
{
	struct phy_provider **ptr, *phy_provider;

	ptr = devres_alloc(devm_phy_provider_release, sizeof(*ptr), GFP_KERNEL);


	phy_provider = __of_phy_provider_register(dev, children, owner,
						  of_xlate);
	return phy_provider;
}
EXPORT_SYMBOL_GPL(__devm_of_phy_provider_register);

struct phy_provider *__of_phy_provider_register(struct device *dev,
	struct device_node *children, struct module *owner,
	struct phy * (*of_xlate)(struct device *dev,
				 struct of_phandle_args *args))
{
	struct phy_provider *phy_provider;

	/*
	 * If specified, the device node containing the children must itself
	 * be the provider's device node or a child (or further descendant)
	 * thereof.
	 */
	if (children) {
		struct device_node *parent = of_node_get(children), *next;

		while (parent) {
			if (parent == dev->of_node)
				break;

			next = of_get_parent(parent);
			of_node_put(parent);
			parent = next;
		}

		if (!parent)
			return ERR_PTR(-EINVAL);

		of_node_put(parent);
	} else {
		children = dev->of_node;
	}

	phy_provider = kzalloc(sizeof(*phy_provider), GFP_KERNEL);


	phy_provider->dev = dev;
	phy_provider->children = of_node_get(children);
	phy_provider->owner = owner;
	/* 获取phy_provider后，可以通过of_xlate得到phy的实例 */
	phy_provider->of_xlate = of_xlate;

	mutex_lock(&phy_provider_mutex);
	/* 将phy_provider加入到链表phy_provider_list中 */
	list_add_tail(&phy_provider->list, &phy_provider_list);
	mutex_unlock(&phy_provider_mutex);

	return phy_provider;
}
EXPORT_SYMBOL_GPL(__of_phy_provider_register);


/* of_phy_provider_lookup会查找phy_provider，从代码看只对比phy_provider->dev->of_node */
static struct phy_provider *of_phy_provider_lookup(struct device_node *node)
{
	struct phy_provider *phy_provider;
	struct device_node *child;

	list_for_each_entry(phy_provider, &phy_provider_list, list) {
		if (phy_provider->dev->of_node == node)
			return phy_provider;

		for_each_child_of_node(phy_provider->children, child)
			if (child == node)
				return phy_provider;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static struct phy *_of_phy_get(struct device_node *np, int index)
{
	int ret;
	struct phy_provider *phy_provider;
	struct phy *phy = NULL;
	struct of_phandle_args args;

	/* 通过phys属性解析phandle，并且把解析出的参数保存到args中 
	 * #phy-cells 表示phys 属性后面参数的个数，不包括phandle，如
	 * phys = <&lane2 PHY_TYPE_USB3 0 2 26000000>;
	 * 表示除了&lane2 后面参数的个数
	 */
	ret = of_parse_phandle_with_args(np, "phys", "#phy-cells",
		index, &args);
	if (ret)
		return ERR_PTR(-ENODEV);

	/* This phy type handled by the usb-phy subsystem for now */
	if (of_device_is_compatible(args.np, "usb-nop-xceiv"))
		return ERR_PTR(-ENODEV);

	mutex_lock(&phy_provider_mutex);
	phy_provider = of_phy_provider_lookup(args.np);
	if (IS_ERR(phy_provider) || !try_module_get(phy_provider->owner)) {
		phy = ERR_PTR(-EPROBE_DEFER);
		goto out_unlock;
	}

	if (!of_device_is_available(args.np)) {
		dev_warn(phy_provider->dev, "Requested PHY is disabled\n");
		phy = ERR_PTR(-ENODEV);
		goto out_put_module;
	}
	/* 调用of_xlate获取phy的实例 */
	phy = phy_provider->of_xlate(phy_provider->dev, &args);

out_put_module:
	module_put(phy_provider->owner);

out_unlock:
	mutex_unlock(&phy_provider_mutex);
	of_node_put(args.np);

	return phy;
}

/**
 * 下面的函数回直接或间接调用_of_phy_get，从而获取phy_provider 
 * 进而调用phy_provider->of_xlate，从而获取phy
 * 从下面的函数也可以看出phy_provider机制适用于devicetree机制的
 */
struct phy *of_phy_get(struct device_node *np, const char *con_id)
{
	struct phy *phy = NULL;
	int index = 0;

	/**
	 * np节点有phy-names和phys两个属性，例如：
	 * phy-names = "usb3-phy";
	 * phys = <&lane2 PHY_TYPE_USB3 0 2 26000000>;
	 * 从phy-names获取到对应con_id的index，然后从phy属性
	 * 的phand找到phy对应的np
	 * 还有例子，如下：
	 * phys = <&ep_phy0 &ep_phy1>;
	 * phy-names = "pcie-lane0","pcie-lane1";
	 */
	if (con_id)
		index = of_property_match_string(np, "phy-names", con_id);

	phy = _of_phy_get(np, index);
	if (IS_ERR(phy))
		return phy;

	if (!try_module_get(phy->ops->owner))
		return ERR_PTR(-EPROBE_DEFER);

	get_device(&phy->dev);

	return phy;
}
EXPORT_SYMBOL_GPL(of_phy_get);

struct phy *phy_get(struct device *dev, const char *string)
{
	int index = 0;
	struct phy *phy;

	if (string == NULL) {
		dev_WARN(dev, "missing string\n");
		return ERR_PTR(-EINVAL);
	}

	if (dev->of_node) {
		index = of_property_match_string(dev->of_node, "phy-names",
			string);
		phy = _of_phy_get(dev->of_node, index);
	} else {
		phy = phy_find(dev, string);
	}
	if (IS_ERR(phy))
		return phy;

	if (!try_module_get(phy->ops->owner))
		return ERR_PTR(-EPROBE_DEFER);

	get_device(&phy->dev);

	return phy;
}
EXPORT_SYMBOL_GPL(phy_get);

struct phy *devm_of_phy_get(struct device *dev, struct device_node *np,
			    const char *con_id)
{
	struct phy **ptr, *phy;

	ptr = devres_alloc(devm_phy_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy = of_phy_get(np, con_id);
	if (!IS_ERR(phy)) {
		*ptr = phy;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return phy;
}
EXPORT_SYMBOL_GPL(devm_of_phy_get);

struct phy *devm_of_phy_get_by_index(struct device *dev, struct device_node *np,
				     int index)
{
	struct phy **ptr, *phy;

	ptr = devres_alloc(devm_phy_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy = _of_phy_get(np, index);
	if (IS_ERR(phy)) {
		devres_free(ptr);
		return phy;
	}

	if (!try_module_get(phy->ops->owner)) {
		devres_free(ptr);
		return ERR_PTR(-EPROBE_DEFER);
	}

	get_device(&phy->dev);

	*ptr = phy;
	devres_add(dev, ptr);

	return phy;
}
EXPORT_SYMBOL_GPL(devm_of_phy_get_by_index);



/* 调用phy_create_lookup之前，先调用phy_create创建好phy */
int phy_create_lookup(struct phy *phy, const char *con_id, const char *dev_id)
{
	struct phy_lookup *pl;

	if (!phy || !dev_id || !con_id)
		return -EINVAL;

	pl = kzalloc(sizeof(*pl), GFP_KERNEL);
	if (!pl)
		return -ENOMEM;

	pl->dev_id = dev_id;
	pl->con_id = con_id;
	pl->phy = phy;

	mutex_lock(&phy_provider_mutex);
	/* 将phy添加到phys链表中,找到phys链表的phy调用phy_find函数 */
	list_add_tail(&pl->node, &phys);
	mutex_unlock(&phy_provider_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(phy_create_lookup);

/**
 * phy_get函数中会调用phy_find
 */
static struct phy *phy_find(struct device *dev, const char *con_id)
{
	const char *dev_id = dev_name(dev);
	struct phy_lookup *p, *pl = NULL;

	mutex_lock(&phy_provider_mutex);
	list_for_each_entry(p, &phys, node)
		if (!strcmp(p->dev_id, dev_id) && !strcmp(p->con_id, con_id)) {
			pl = p;
			break;
		}
	mutex_unlock(&phy_provider_mutex);

	return pl ? pl->phy : ERR_PTR(-ENODEV);
}

