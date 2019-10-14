# xlinx usb 注册过程

xlinx usb 使用的是dwc3

## dwc3-of-simple.c 

### dts数据
zynqmp.dtsi

```c
usb0: usb0@ff9d0000 {
			#address-cells = <2>;
			#size-cells = <2>;
			status = "disabled";
			compatible = "xlnx,zynqmp-dwc3";
			reg = <0x0 0xff9d0000 0x0 0x100>;
			clock-names = "bus_clk", "ref_clk";
			power-domains = <&zynqmp_firmware PD_USB_0>;
			ranges;
			nvmem-cells = <&soc_revision>;
			nvmem-cell-names = "soc_revision";

			dwc3_0: dwc3@fe200000 {
				compatible = "snps,dwc3";
				status = "disabled";
				reg = <0x0 0xfe200000 0x0 0x40000>;
				interrupt-parent = <&gic>;
				interrupt-names = "dwc_usb3", "otg", "hiber";
				interrupts = <0 65 4>, <0 69 4>, <0 75 4>;
				#stream-id-cells = <1>;
				iommus = <&smmu 0x860>;
				snps,quirk-frame-length-adjustment = <0x20>;
				snps,refclk_fladj;
				snps,enable_guctl1_resume_quirk;
				snps,enable_guctl1_ipd_quirk;
				snps,xhci-stream-quirk;
				/* dma-coherent; */
				/* snps,enable-hibernation; */
			};
		};

```
```c
serdes: zynqmp_phy@fd400000 {
			compatible = "xlnx,zynqmp-psgtr-v1.1";
			status = "disabled";
			reg = <0x0 0xfd400000 0x0 0x40000>,
			      <0x0 0xfd3d0000 0x0 0x1000>;
			reg-names = "serdes", "siou";
			nvmem-cells = <&soc_revision>;
			nvmem-cell-names = "soc_revision";
			resets = <&zynqmp_reset ZYNQMP_RESET_SATA>,
				 <&zynqmp_reset ZYNQMP_RESET_USB0_CORERESET>,
				 <&zynqmp_reset ZYNQMP_RESET_USB1_CORERESET>,
				 <&zynqmp_reset ZYNQMP_RESET_USB0_HIBERRESET>,
				 <&zynqmp_reset ZYNQMP_RESET_USB1_HIBERRESET>,
				 <&zynqmp_reset ZYNQMP_RESET_USB0_APB>,
				 <&zynqmp_reset ZYNQMP_RESET_USB1_APB>,
				 <&zynqmp_reset ZYNQMP_RESET_DP>,
				 <&zynqmp_reset ZYNQMP_RESET_GEM0>,
				 <&zynqmp_reset ZYNQMP_RESET_GEM1>,
				 <&zynqmp_reset ZYNQMP_RESET_GEM2>,
				 <&zynqmp_reset ZYNQMP_RESET_GEM3>;
			reset-names = "sata_rst", "usb0_crst", "usb1_crst",
				      "usb0_hibrst", "usb1_hibrst", "usb0_apbrst",
				      "usb1_apbrst", "dp_rst", "gem0_rst",
				      "gem1_rst", "gem2_rst", "gem3_rst";
			lane0: lane0 {
				#phy-cells = <4>;
			};
			lane1: lane1 {
				#phy-cells = <4>;
			};
			lane2: lane2 {
				#phy-cells = <4>;
			};
			lane3: lane3 {
				#phy-cells = <4>;
			};
		};

```

zynqmp-zcu102-revA.dts

```c
&usb0 {
	status = "okay";
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_usb0_default>;
};

&dwc3_0 {
	status = "okay";
	dr_mode = "host";
	snps,usb3_lpm_capable;
	phy-names = "usb3-phy";
	phys = <&lane2 PHY_TYPE_USB3 0 2 26000000>;
	maximum-speed = "super-speed";
};
```


### 代码部分

```c
static const struct of_device_id of_dwc3_simple_match[] = {
	{ .compatible = "xlnx,zynqmp-dwc3" },
	......
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_dwc3_simple_match);

static struct platform_driver dwc3_of_simple_driver = {
	.probe		= dwc3_of_simple_probe,
	.remove		= dwc3_of_simple_remove,
	.driver		= {
		.name	= "dwc3-of-simple",
		.of_match_table = of_dwc3_simple_match,
		.pm	= &dwc3_of_simple_dev_pm_ops,
	},
};
```

#### dwc3_of_simple_probe

```c
static int dwc3_of_simple_probe(struct platform_device *pdev)
{
	struct dwc3_of_simple	*simple;
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node;

	int			ret;
	int			i;
	bool		shared_resets = false;
	
	simple = devm_kzalloc(dev, sizeof(*simple), GFP_KERNEL);
	
	eemi_ops = zynqmp_pm_get_eemi_ops();
	/* 将simple数据保存到pdev中，可以传递给其他地方用 */
	platform_set_drvdata(pdev, simple);
	simple->dev = dev;
	
	if (of_device_is_compatible(pdev->dev.of_node, "xlnx,zynqmp-dwc3") ||
	    of_device_is_compatible(pdev->dev.of_node, "xlnx,versal-dwc3")) {
	
		struct resource		*res;
		void __iomem		*regs;
	
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		regs = devm_ioremap_resource(&pdev->dev, res);
	
		/* Store the usb control regs into simple for further usage */
		simple->regs = regs;
		/*
		 * ZynqMP silicon revision lesser than 4.0 needs to disable
		 * suspend of usb 3.0 phy.
		 */
		ret = dwc3_dis_u3phy_suspend(pdev, simple);
	}
	
	/* Set phy data for future use */
	dwc3_simple_set_phydata(simple);
	
	/*
	 * Some controllers need to toggle the usb3-otg reset before trying to
	 * initialize the PHY, otherwise the PHY times out.
	 */
	if (of_device_is_compatible(np, "rockchip,rk3399-dwc3"))
		simple->need_reset = true;
	
	if (of_device_is_compatible(np, "amlogic,meson-axg-dwc3") ||
	    of_device_is_compatible(np, "amlogic,meson-gxl-dwc3")) {
		shared_resets = true;
		simple->pulse_resets = true;
	}
	/* 本例中没有reset */
	simple->resets = of_reset_control_array_get(np, shared_resets, true);
	
	if (simple->pulse_resets) {
		ret = reset_control_reset(simple->resets);
	} else {
		ret = reset_control_deassert(simple->resets);
	}
	
	ret = dwc3_of_simple_clk_init(simple, of_count_phandle_with_args(np,
						"clocks", "#clock-cells"));

	/* 为np子节点创建platform_devices，即dwc3_0: dwc3@fe200000 
	 * 在device tree初始化的时候，子节点不创建platform_devices吗？
	 */
	ret = of_platform_populate(np, NULL, NULL, dev);
	
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);
	
	return 0;
}
```

##### dwc3_simple_set_phydata

```c
static int dwc3_simple_set_phydata(struct dwc3_of_simple *simple)
{
	struct device		*dev = simple->dev;
	struct device_node	*np = dev->of_node;
	struct phy		*phy;
    /* 子节点为dwc3_0: dwc3@fe200000 */
	np = of_get_next_child(np, NULL);
	if (np) {
		phy = of_phy_get(np, "usb3-phy");/* 获取usb3-phy */
		/* Store phy for future usage */
		simple->phy = phy;
		/* assign USB vendor regs addr to phy platform_data */
		phy->dev.platform_data = simple->regs;
		phy_put(phy);
	} else {
		dev_err(dev, "%s: Can't find child node\n", __func__);
		return -EINVAL;
	}
	return 0;
}
```

###### of_phy_get

```c
struct phy *of_phy_get(struct device_node *np, const char *con_id)
{
	struct phy *phy = NULL;
	int index = 0;

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
```

###### _of_phy_get

```c
static struct phy *_of_phy_get(struct device_node *np, int index)
{
	int ret;
	struct phy_provider *phy_provider;
	struct phy *phy = NULL;
	struct of_phandle_args args;

    /*属性phys 为phandle，args，包含phandle对应的节点还有 phys 后面的参数PHY_TYPE_USB3等
     * phys = <&lane2 PHY_TYPE_USB3 0 2 26000000>;
     * lane2: lane2 {
	 *			#phy-cells = <4>;
	 *		};
     */
	ret = of_parse_phandle_with_args(np, "phys", "#phy-cells",
		index, &args);

	/* This phy type handled by the usb-phy subsystem for now */
    /* 应该返回0 */
	if (of_device_is_compatible(args.np, "usb-nop-xceiv"))
		return ERR_PTR(-ENODEV);

	mutex_lock(&phy_provider_mutex);
    /* 查找phy_provider，np为 
     *	lane2: lane2 {
	 *			#phy-cells = <4>;
	 *		};
     */
	phy_provider = of_phy_provider_lookup(args.np);
	if (IS_ERR(phy_provider) || !try_module_get(phy_provider->owner)) {
	}

	if (!of_device_is_available(args.np)) {
	}
	/* 调用phy_provider->of_xlate函数，即xpsgtr_xlate函数 */
	phy = phy_provider->of_xlate(phy_provider->dev, &args);

	return phy;
}
```

###### of_phy_provider_lookup

```c
static struct phy_provider *of_phy_provider_lookup(struct device_node *node)
{
	struct phy_provider *phy_provider;
	struct device_node *child;

    /* 本例中的phy_provider由phy-zynqmp.c注册 */
	list_for_each_entry(phy_provider, &phy_provider_list, list) {
		if (phy_provider->dev->of_node == node)
			return phy_provider;

		for_each_child_of_node(phy_provider->children, child)
			if (child == node)
				return phy_provider;
	}
	return ERR_PTR(-EPROBE_DEFER);
}
```

##### of_platform_populate

```c
int of_platform_populate(struct device_node *root,
			const struct of_device_id *matches,
			const struct of_dev_auxdata *lookup,
			struct device *parent)
{
	struct device_node *child;
	int rc = 0;

	root = root ? of_node_get(root) : of_find_node_by_path("/");
	if (!root)
		return -EINVAL;

	pr_debug("%s()\n", __func__);
	pr_debug(" starting at: %pOF\n", root);

	for_each_child_of_node(root, child) {
		rc = of_platform_bus_create(child, matches, lookup, parent, true);
		if (rc) {
			of_node_put(child);
			break;
		}
	}
	of_node_set_flag(root, OF_POPULATED_BUS);

	of_node_put(root);
	return rc;
}
```

###### of_platform_bus_create

```c
static int of_platform_bus_create(struct device_node *bus,
				  const struct of_device_id *matches,
				  const struct of_dev_auxdata *lookup,
				  struct device *parent, bool strict)
{
	const struct of_dev_auxdata *auxdata;
	struct device_node *child;
	struct platform_device *dev;
	const char *bus_id = NULL;
	void *platform_data = NULL;
	int rc = 0;

	/* Make sure it has a compatible property */
	if (strict && (!of_get_property(bus, "compatible", NULL))) {
		pr_debug("%s() - skipping %pOF, no compatible prop\n",
			 __func__, bus);
		return 0;
	}

	/* Skip nodes for which we don't want to create devices */
	if (unlikely(of_match_node(of_skipped_node_table, bus))) {
		pr_debug("%s() - skipping %pOF node\n", __func__, bus);
		return 0;
	}

	if (of_node_check_flag(bus, OF_POPULATED_BUS)) {
		pr_debug("%s() - skipping %pOF, already populated\n",
			__func__, bus);
		return 0;
	}

	auxdata = of_dev_lookup(lookup, bus);
	if (auxdata) {
		bus_id = auxdata->name;
		platform_data = auxdata->platform_data;
	}

	if (of_device_is_compatible(bus, "arm,primecell")) {
		/*
		 * Don't return an error here to keep compatibility with older
		 * device tree files.
		 */
		of_amba_device_create(bus, bus_id, platform_data, parent);
		return 0;
	}

	dev = of_platform_device_create_pdata(bus, bus_id, platform_data, parent);
	if (!dev || !of_match_node(matches, bus))
		return 0;

	for_each_child_of_node(bus, child) {
		pr_debug("   create child: %pOF\n", child);
		rc = of_platform_bus_create(child, matches, lookup, &dev->dev, strict);
		if (rc) {
			of_node_put(child);
			break;
		}
	}
	of_node_set_flag(bus, OF_POPULATED_BUS);
	return rc;
}

```

###### of_platform_device_create_pdata

```c
static struct platform_device *of_platform_device_create_pdata(
					struct device_node *np,
					const char *bus_id,
					void *platform_data,
					struct device *parent)
{
	struct platform_device *dev;

	if (!of_device_is_available(np) ||
	    of_node_test_and_set_flag(np, OF_POPULATED))
		return NULL;

	dev = of_device_alloc(np, bus_id, parent);

	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if (!dev->dev.dma_mask)
		dev->dev.dma_mask = &dev->dev.coherent_dma_mask;
	dev->dev.bus = &platform_bus_type;
	dev->dev.platform_data = platform_data;
	of_msi_configure(&dev->dev, dev->dev.of_node);

	if (of_device_add(dev) != 0) {
		platform_device_put(dev);
		goto err_clear_flag;
	}
	return dev;
}
```

###### of_device_alloc

```c
struct platform_device *of_device_alloc(struct device_node *np,
				  const char *bus_id,
				  struct device *parent)
{
	struct platform_device *dev;
	int rc, i, num_reg = 0, num_irq;
	struct resource *res, temp_res;

	dev = platform_device_alloc("", PLATFORM_DEVID_NONE);
	if (!dev)
		return NULL;

	/* count the io and irq resources */
	while (of_address_to_resource(np, num_reg, &temp_res) == 0)
		num_reg++;
	num_irq = of_irq_count(np);

	/* Populate the resource table */
	if (num_irq || num_reg) {
		res = kcalloc(num_irq + num_reg, sizeof(*res), GFP_KERNEL);
		if (!res) {
			platform_device_put(dev);
			return NULL;
		}

		dev->num_resources = num_reg + num_irq;
		dev->resource = res;
		for (i = 0; i < num_reg; i++, res++) {
			rc = of_address_to_resource(np, i, res);
			WARN_ON(rc);
		}
		if (of_irq_to_resource_table(np, res, num_irq) != num_irq)
			pr_debug("not all legacy IRQ resources mapped for %s\n",
				 np->name);
	}

	dev->dev.of_node = of_node_get(np);
	dev->dev.fwnode = &np->fwnode;
	dev->dev.parent = parent ? : &platform_bus;

	if (bus_id)
		dev_set_name(&dev->dev, "%s", bus_id);
	else
		of_device_make_bus_id(&dev->dev);

	return dev;
}
```



## phy-zynqmp.c

```c
/* Match table for of_platform binding */
static const struct of_device_id xpsgtr_of_match[] = {
	{ .compatible = "xlnx,zynqmp-psgtr", },
	{ .compatible = "xlnx,zynqmp-psgtr-v1.1", },
	{},
};
MODULE_DEVICE_TABLE(of, xpsgtr_of_match);

static struct platform_driver xpsgtr_driver = {
	.probe = xpsgtr_probe,
	.driver = {
		.name = "xilinx-psgtr",
		.of_match_table	= xpsgtr_of_match,
		.pm =  &xpsgtr_pm_ops,
	},
};

module_platform_driver(xpsgtr_driver);
```

### xpsgtr_probe

```c
/**
 * xpsgtr_probe - The device probe function for driver initialization.
 * @pdev: pointer to the platform device structure.
 *
 * Return: 0 for success and error value on failure
 */
static int xpsgtr_probe(struct platform_device *pdev)
{
	struct device_node *child, *np = pdev->dev.of_node;
	struct xpsgtr_dev *gtr_dev;
	struct phy_provider *provider;
	struct phy *phy;
	struct resource *res;
	char *soc_rev;
	int lanecount, port = 0, index = 0;
	int err;

	if (of_device_is_compatible(np, "xlnx,zynqmp-psgtr"))
		dev_warn(&pdev->dev, "This binding is deprecated, please use new compatible binding\n");

	eemi_ops = zynqmp_pm_get_eemi_ops();
	gtr_dev = devm_kzalloc(&pdev->dev, sizeof(*gtr_dev), GFP_KERNEL);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "serdes");
	gtr_dev->serdes = devm_ioremap_resource(&pdev->dev, res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "siou");
	gtr_dev->siou = devm_ioremap_resource(&pdev->dev, res);

	lanecount = of_get_child_count(np);
	if (lanecount > MAX_LANES || lanecount == 0)
		return -EINVAL;

	gtr_dev->phys = devm_kzalloc(&pdev->dev, sizeof(phy) * lanecount,
				     GFP_KERNEL);

	gtr_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, gtr_dev);
	mutex_init(&gtr_dev->gtr_mutex);

	/* Deferred probe is also handled if nvmem is not ready */
	soc_rev = zynqmp_nvmem_get_silicon_version(&pdev->dev,
						   "soc_revision");

	if (*soc_rev == ZYNQMP_SILICON_V1)
		gtr_dev->tx_term_fix = true;

	kfree(soc_rev);

	err = xpsgtr_get_resets(gtr_dev);

	for_each_child_of_node(np, child) {
        /* 为每一个子节点创建一个phy，子节点为
         * lane0: lane0 {
		 *		#phy-cells = <4>;
		 *	};
		 *	lane1: lane1 {
		 *		#phy-cells = <4>;
		 *	};
         */
		struct xpsgtr_phy *gtr_phy;
		gtr_phy = devm_kzalloc(&pdev->dev, sizeof(*gtr_phy),
				       GFP_KERNEL);
		/* Assign lane number to gtr_phy instance */
		gtr_phy->lane = index;
		/* Disable lane sharing as default */
		gtr_phy->share_laneclk = -1;

		gtr_dev->phys[port] = gtr_phy;
		phy = devm_phy_create(&pdev->dev, child, &xpsgtr_phyops);

		gtr_dev->phys[port]->phy = phy;
		phy_set_drvdata(phy, gtr_dev->phys[port]);
		gtr_phy->data = gtr_dev;
		port++;
		index++;
	}
	provider = devm_of_phy_provider_register(&pdev->dev, xpsgtr_xlate);
	return 0;
}

```

### devm_of_phy_provider_register

```c
#define	devm_of_phy_provider_register(dev, xlate)	\
	__devm_of_phy_provider_register((dev), NULL, THIS_MODULE, (xlate))

struct phy_provider *__devm_of_phy_provider_register(struct device *dev,
	struct device_node *children, struct module *owner,
	struct phy * (*of_xlate)(struct device *dev,
				 struct of_phandle_args *args))
{
	struct phy_provider **ptr, *phy_provider;

	ptr = devres_alloc(devm_phy_provider_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy_provider = __of_phy_provider_register(dev, children, owner,
						  of_xlate);
	if (!IS_ERR(phy_provider)) {
		*ptr = phy_provider;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return phy_provider;
}
```

### __of_phy_provider_register

```c
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
	phy_provider->of_xlate = of_xlate;

	/* 添加到phy_provider_list列表中 */
	mutex_lock(&phy_provider_mutex);
	list_add_tail(&phy_provider->list, &phy_provider_list);
	mutex_unlock(&phy_provider_mutex);

	return phy_provider;
}
```

### xpsgtr_xlate

```c
/* args从phandle对应的属性中解析出来的 */
static struct phy *xpsgtr_xlate(struct device *dev,
				struct of_phandle_args *args)
{
	struct xpsgtr_dev *gtr_dev = dev_get_drvdata(dev);
	struct xpsgtr_phy *gtr_phy = NULL;
	struct device_node *phynode = args->np;
	int index;
	int i;
	u8 controller;
	u8 instance_num;

	if (args->args_count != 4) {
		dev_err(dev, "Invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}
	if (!of_device_is_available(phynode)) {
		dev_warn(dev, "requested PHY is disabled\n");
		return ERR_PTR(-ENODEV);
	}
	for (index = 0; index < of_get_child_count(dev->of_node); index++) {
		if (phynode == gtr_dev->phys[index]->phy->dev.of_node) {
			gtr_phy = gtr_dev->phys[index];
			break;
		}
	}
	/* get type of controller from phys */
	controller = args->args[0];
	/* get controller instance number */
	instance_num = args->args[1];
	/* Check if lane sharing is required */
	gtr_phy->share_laneclk = args->args[2];
	/* get the required clk rate for controller from phys */
	gtr_phy->refclk_rate = args->args[3];

	/* derive lane type */
	if (xpsgtr_set_lanetype(gtr_phy, controller, instance_num) < 0) {
		dev_err(gtr_dev->dev, "Invalid lane type\n");
		return ERR_PTR(-EINVAL);
	}

	/* configures SSC settings for a lane */
	if (xpsgtr_configure_lane(gtr_phy) < 0) {
		dev_err(gtr_dev->dev, "Invalid clock rate: %d\n",
			gtr_phy->refclk_rate);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Check Interconnect Matrix is obeyed i.e, given lane type
	 * is allowed to operate on the lane.
	 */
	for (i = 0; i < CONTROLLERS_PER_LANE; i++) {
		if (icm_matrix[index][i] == gtr_phy->type)
			return gtr_phy->phy;/* 返回phy */
	}

	/* Should not reach here */
	return ERR_PTR(-EINVAL);
}
```



## dwc3/core.c

```
static struct platform_driver dwc3_driver = {
	.probe		= dwc3_probe,
	.remove		= dwc3_remove,
	.driver		= {
		.name	= "dwc3",
		.of_match_table	= of_match_ptr(of_dwc3_match),
		.acpi_match_table = ACPI_PTR(dwc3_acpi_match),
		.pm	= &dwc3_dev_pm_ops,
	},
};
module_platform_driver(dwc3_driver);
```

### dwc3_probe

```
static int dwc3_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct resource		*res, dwc_res;
	struct dwc3		*dwc;
	int			ret;
	u32			mdwidth;
	void __iomem		*regs;

	dwc = devm_kzalloc(dev, sizeof(*dwc), GFP_KERNEL);

	dwc->clks = devm_kmemdup(dev, dwc3_core_clks, sizeof(dwc3_core_clks), GFP_KERNEL);
	dwc->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	dwc->xhci_resources[0].start = res->start;
	dwc->xhci_resources[0].end = dwc->xhci_resources[0].start + DWC3_XHCI_REGS_END;
	dwc->xhci_resources[0].flags = res->flags;
	dwc->xhci_resources[0].name = res->name;

	/*
	 * Request memory region but exclude xHCI regs,
	 * since it will be requested by the xhci-plat driver.
	 */
	dwc_res = *res;
	dwc_res.start += DWC3_GLOBALS_REGS_START;

	regs = devm_ioremap_resource(dev, &dwc_res);
	dwc->regs	= regs;
	dwc->regs_size	= resource_size(&dwc_res);

	dwc3_get_properties(dwc);

	dwc->reset = devm_reset_control_get_optional_shared(dev, NULL);

	if (dev->of_node) {
		dwc->num_clks = ARRAY_SIZE(dwc3_core_clks);

		ret = clk_bulk_get(dev, dwc->num_clks, dwc->clks);
		if (ret == -EPROBE_DEFER)
			return ret;
		/*
		 * Clocks are optional, but new DT platforms should support all
		 * clocks as required by the DT-binding.
		 */
		if (ret)
			dwc->num_clks = 0;
	}

	ret = reset_control_deassert(dwc->reset);


	ret = clk_bulk_prepare(dwc->num_clks, dwc->clks);


	ret = clk_bulk_enable(dwc->num_clks, dwc->clks);


	platform_set_drvdata(pdev, dwc);
	dwc3_cache_hwparams(dwc);

	spin_lock_init(&dwc->lock);

	/* Set dma coherent mask to DMA BUS data width */
	mdwidth = DWC3_GHWPARAMS0_MDWIDTH(dwc->hwparams.hwparams0);
	dev_dbg(dev, "Enabling %d-bit DMA addresses.\n", mdwidth);
	dma_set_coherent_mask(dev, DMA_BIT_MASK(mdwidth));

	pm_runtime_set_active(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, DWC3_DEFAULT_AUTOSUSPEND_DELAY);
	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	pm_runtime_forbid(dev);

	ret = dwc3_alloc_event_buffers(dwc, DWC3_EVENT_BUFFERS_SIZE);
	ret = dwc3_get_dr_mode(dwc);


	ret = dwc3_core_init(dwc);
	if (ret) {
		dev_err(dev, "failed to initialize core\n");
		goto err4;
	}

	dwc3_check_params(dwc);

	ret = dwc3_core_init_mode(dwc);
	if (ret)
		goto err5;

	dwc3_debugfs_init(dwc);
	pm_runtime_put(dev);

	return 0;

err5:
	dwc3_event_buffers_cleanup(dwc);

err4:
	dwc3_free_scratch_buffers(dwc);

err3:
	dwc3_free_event_buffers(dwc);

err2:
	pm_runtime_allow(&pdev->dev);

err1:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	clk_bulk_disable(dwc->num_clks, dwc->clks);
unprepare_clks:
	clk_bulk_unprepare(dwc->num_clks, dwc->clks);
assert_reset:
	reset_control_assert(dwc->reset);
put_clks:
	clk_bulk_put(dwc->num_clks, dwc->clks);

	return ret;
}

```

