# linux mmc初始化过程

以xilinx zynq sdhci-of-arasan.c驱动为例，不涉及mmc硬件以及协议相关

## platform_driver驱动：

```c
static struct platform_driver sdhci_arasan_driver = {
	.driver = {
		.name = "sdhci-arasan",
		.of_match_table = sdhci_arasan_of_match,
		.pm = &sdhci_arasan_dev_pm_ops,
	},
	.probe = sdhci_arasan_probe,
	.remove = sdhci_arasan_remove,
};
```

```c
static const struct of_device_id sdhci_arasan_of_match[] = {
	/* SoC-specific compatible strings w/ soc_ctl_map */
	{
		.compatible = "rockchip,rk3399-sdhci-5.1",
		.data = &rk3399_soc_ctl_map,
	},
	/* Generic compatible below here */
	{ .compatible = "arasan,sdhci-8.9a" },
	{ .compatible = "xlnx,zynqmp-8.9a" },

	{ /* sentinel */ }
};
```

# sdhci_arasan_probe

删去很多实际没有执行的代码

```c
static int sdhci_arasan_probe(struct platform_device *pdev)
{
	int ret;
	const struct of_device_id *match;
	struct device_node *node;
	struct clk *clk_xin;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_arasan_data *sdhci_arasan;
	struct device_node *np = pdev->dev.of_node;
	unsigned int host_quirks2 = 0;
	const struct sdhci_pltfm_data *pdata;

    /* sdhci_arasan_ops定义了自己的sdhci_ops */
	pdata = &sdhci_arasan_pdata;

	if (of_device_is_compatible(pdev->dev.of_node, "xlnx,zynqmp-8.9a")) {
		char *soc_rev;
		/* read Silicon version using nvmem driver */
		soc_rev = zynqmp_nvmem_get_silicon_version(&pdev->dev, "soc_revision");
		/* Set host quirk if the silicon version is v1.0 */
		if (!IS_ERR(soc_rev) && (*soc_rev == ZYNQMP_SILICON_V1))
			host_quirks2 |= SDHCI_QUIRK2_NO_1_8_V;
	}

	/* struct sdhci_host *host */
	host = sdhci_pltfm_init(pdev, pdata, sizeof(*sdhci_arasan));

	/*
	 * pltfm_host = host->private;
	 */
	pltfm_host = sdhci_priv(host);
	sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	sdhci_arasan->host = host;

	match = of_match_node(sdhci_arasan_of_match, pdev->dev.of_node);
	sdhci_arasan->soc_ctl_map = match->data;

	host->quirks2 |= host_quirks2;

	sdhci_arasan->clk_ahb = devm_clk_get(&pdev->dev, "clk_ahb");

	clk_xin = devm_clk_get(&pdev->dev, "clk_xin");

	ret = clk_prepare_enable(sdhci_arasan->clk_ahb);

	ret = clk_prepare_enable(clk_xin);

    /* 获取sdhci相关的属性 */
	sdhci_get_of_property(pdev);

	if (of_property_read_bool(np, "xlnx,fails-without-test-cd"))
		sdhci_arasan->quirks |= SDHCI_ARASAN_QUIRK_FORCE_CDTEST;

	if (of_property_read_bool(np, "xlnx,int-clock-stable-broken"))
		sdhci_arasan->quirks |= SDHCI_ARASAN_QUIRK_CLOCK_UNSTABLE;

	if (of_device_is_compatible(pdev->dev.of_node, "xlnx,versal-8.9a"))
		sdhci_arasan->quirks |= SDHCI_ARASAN_TAPDELAY_REG_LOCAL;

	pltfm_host->clk = clk_xin;

	sdhci_arasan_update_baseclkfreq(host);

	ret = sdhci_arasan_register_sdclk(sdhci_arasan, clk_xin, &pdev->dev);
	/* 获取mmc相关熟悉 */
	ret = mmc_of_parse(host->mmc);

	if (of_device_is_compatible(pdev->dev.of_node, "arasan,sdhci-8.9a")) {
		host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;
		host->quirks2 |= SDHCI_QUIRK2_CLOCK_STANDARD_25_BROKEN;
	}

	if (of_device_is_compatible(pdev->dev.of_node, "xlnx,zynqmp-8.9a")) {
		ret = of_property_read_u32(pdev->dev.of_node, "xlnx,mio_bank",
					   &sdhci_arasan->mio_bank);
		ret = of_property_read_u32(pdev->dev.of_node, "xlnx,device_id",
					   &sdhci_arasan->device_id);
		sdhci_arasan_ops.platform_execute_tuning = arasan_zynqmp_execute_tuning;
	}

    /*  pinctrl初始化 */
	sdhci_arasan->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR(sdhci_arasan->pinctrl)) {
		sdhci_arasan->pins_default = pinctrl_lookup_state( sdhci_arasan->pinctrl,
							PINCTRL_STATE_DEFAULT);

		pinctrl_select_state(sdhci_arasan->pinctrl, sdhci_arasan->pins_default);
	}

	sdhci_arasan->phy = ERR_PTR(-ENODEV);

	ret = sdhci_arasan_add_host(sdhci_arasan);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 2000);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_forbid(&pdev->dev);

	return 0;
}
```

sdhci_arasan_pdata

```c
static struct sdhci_ops sdhci_arasan_ops = {
	.set_clock = sdhci_arasan_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_arasan_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_power = sdhci_arasan_set_power,
};

static const struct sdhci_pltfm_data sdhci_arasan_pdata = {
	.ops = &sdhci_arasan_ops,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN |
			SDHCI_QUIRK2_STOP_WITH_TC,
};

```

## sdhci_pltfm_init

```c
struct sdhci_host *sdhci_pltfm_init(struct platform_device *pdev,
				    const struct sdhci_pltfm_data *pdata,
				    size_t priv_size)
{
	struct sdhci_host *host;
	struct resource *iomem;
	void __iomem *ioaddr;
	int irq, ret;
	/* 获取寄存器地址 */
	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* 寄存器进行映射 */
	ioaddr = devm_ioremap_resource(&pdev->dev, iomem);

	/* 获得irq号 */
	irq = platform_get_irq(pdev, 0);

	/* priv_size =  sizeof(*sdhci_arasan) */
	host = sdhci_alloc_host(&pdev->dev, sizeof(struct sdhci_pltfm_host) + priv_size);

	host->ioaddr = ioaddr;
	host->irq = irq;
	host->hw_name = dev_name(&pdev->dev);
	if (pdata && pdata->ops) /* pdata->ops已经提供 */
		host->ops = pdata->ops;
	else
		host->ops = &sdhci_pltfm_ops;
	if (pdata) {
		host->quirks = pdata->quirks;
		host->quirks2 = pdata->quirks2;
	}

	/* pdev.dev->driver_data = data */
	platform_set_drvdata(pdev, host);

	return host;
}
```

### sdhci_alloc_host

```c
struct sdhci_host *sdhci_alloc_host(struct device *dev, size_t priv_size)
{
	struct mmc_host *mmc;
	struct sdhci_host *host;

	/* priv_size = sizeof(struct sdhci_pltfm_host) + sizeof(*sdhci_arasan) 
	 * 查看kenre其他驱动，对于不支持sdhci的，可以直接调用mmc_alloc_host，只支持emmc.
	 */
	mmc = mmc_alloc_host(sizeof(struct sdhci_host) + priv_size, dev);

    /* struct sdhci_host = struct mmc_host的private  */
	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->mmc_host_ops = sdhci_ops;
    /* mmc->ops =  sdhci_ops */
	mmc->ops = &host->mmc_host_ops;

	host->flags = SDHCI_SIGNALING_330;

	host->cqe_ier     = SDHCI_CQE_INT_MASK;
	host->cqe_err_ier = SDHCI_CQE_INT_ERR_MASK;

	host->tuning_delay = -1;

	host->sdma_boundary = SDHCI_DEFAULT_BOUNDARY_ARG;

	return host;
}
```

sdhci_ops

```c
static const struct mmc_host_ops sdhci_ops = {
	.request	= sdhci_request,
	.post_req	= sdhci_post_req,
	.pre_req	= sdhci_pre_req,
	.set_ios	= sdhci_set_ios,
	.get_cd		= sdhci_get_cd,
	.get_ro		= sdhci_get_ro,
	.hw_reset	= sdhci_hw_reset,
	.enable_sdio_irq = sdhci_enable_sdio_irq,
	.start_signal_voltage_switch	= sdhci_start_signal_voltage_switch,
	.prepare_hs400_tuning		= sdhci_prepare_hs400_tuning,
	.execute_tuning			= sdhci_execute_tuning,
	.card_event			= sdhci_card_event,
	.card_busy	= sdhci_card_busy,
};
```

#### mmc_alloc_host

```c
static DEFINE_IDA(mmc_host_ida);

static struct class mmc_host_class = {
	.name		= "mmc_host",
	.dev_release	= mmc_host_classdev_release,
};
/**
 *	mmc_alloc_host - initialise the per-host structure.
 *	@extra: sizeof private data structure
 *	@dev: pointer to host device model structure
 *
 *	Initialise the per-host structure.
 */
struct mmc_host *mmc_alloc_host(int extra, struct device *dev)
{
	int err;
	struct mmc_host *host;

	/* extra = sizeof(struct sdhci_pltfm_host) + sizeof(*sdhci_arasan) +  
     * sizeof(struct sdhci_host)
     */
	host = kzalloc(sizeof(struct mmc_host) + extra, GFP_KERNEL);

	/* scanning will be enabled when we're ready */
	host->rescan_disable = 1;

	err = ida_simple_get(&mmc_host_ida, 0, 0, GFP_KERNEL);

	host->index = err;

	dev_set_name(&host->class_dev, "mmc%d", host->index);

	host->parent = dev;
	host->class_dev.parent = dev;
	host->class_dev.class = &mmc_host_class;
	device_initialize(&host->class_dev);
	device_enable_async_suspend(&host->class_dev);

	if (mmc_gpio_alloc(host)) {
	}

	spin_lock_init(&host->lock);
	init_waitqueue_head(&host->wq);
    /* mmc_rescan用来scan设备 */
	INIT_DELAYED_WORK(&host->detect, mmc_rescan);
	INIT_DELAYED_WORK(&host->sdio_irq_work, sdio_irq_work);
	timer_setup(&host->retune_timer, mmc_retune_timer, 0);

	/*
	 * By default, hosts do not support SGIO or large requests.
	 * They have to set these according to their abilities.
	 */
	host->max_segs = 1;
	host->max_seg_size = PAGE_SIZE;

	host->max_req_size = PAGE_SIZE;
	host->max_blk_size = 512;
	host->max_blk_count = PAGE_SIZE / 512;

	host->fixed_drv_type = -EINVAL;
	host->ios.power_delay_ms = 10;

	return host;
}
```

mmc_gpio_alloc

```c
struct mmc_gpio {
	struct gpio_desc *ro_gpio;
	struct gpio_desc *cd_gpio;
	bool override_ro_active_level;
	bool override_cd_active_level;
	irqreturn_t (*cd_gpio_isr)(int irq, void *dev_id);
	char *ro_label;
	u32 cd_debounce_delay_ms;
	char cd_label[];
};

int mmc_gpio_alloc(struct mmc_host *host)
{
	size_t len = strlen(dev_name(host->parent)) + 4;
	struct mmc_gpio *ctx = devm_kzalloc(host->parent, sizeof(*ctx) + 2 * len,
    						GFP_KERNEL);

	if (ctx) {
		ctx->ro_label = ctx->cd_label + len;
		ctx->cd_debounce_delay_ms = 200;
		snprintf(ctx->cd_label, len, "%s cd", dev_name(host->parent));
		snprintf(ctx->ro_label, len, "%s ro", dev_name(host->parent));
		host->slot.handler_priv = ctx;
		host->slot.cd_irq = -EINVAL;
	}

	return ctx ? 0 : -ENOMEM;
}
```

## sdhci_get_of_property

```c
void sdhci_get_of_property(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	u32 bus_width;

	if (of_get_property(np, "sdhci,auto-cmd12", NULL))
		host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;

	if (of_get_property(np, "sdhci,1-bit-only", NULL) ||
	    (of_property_read_u32(np, "bus-width", &bus_width) == 0 &&
	    bus_width == 1))
		host->quirks |= SDHCI_QUIRK_FORCE_1_BIT_DATA;

	if (sdhci_of_wp_inverted(np))
		host->quirks |= SDHCI_QUIRK_INVERTED_WRITE_PROTECT;

	if (of_get_property(np, "broken-cd", NULL))
		host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;

	if (of_get_property(np, "no-1-8-v", NULL))
		host->quirks2 |= SDHCI_QUIRK2_NO_1_8_V;

	if (of_device_is_compatible(np, "fsl,p2020-rev1-esdhc"))
		host->quirks |= SDHCI_QUIRK_BROKEN_DMA;

	if (of_device_is_compatible(np, "fsl,p2020-esdhc") ||
	    of_device_is_compatible(np, "fsl,p1010-esdhc") ||
	    of_device_is_compatible(np, "fsl,t4240-esdhc") ||
	    of_device_is_compatible(np, "fsl,mpc8536-esdhc"))
		host->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;

	if (of_get_property(np, "broken-mmc-highspeed", NULL))
		host->quirks |= SDHCI_QUIRK_NO_HISPD_BIT;

	of_property_read_u32(np, "clock-frequency", &pltfm_host->clock);

	if (of_find_property(np, "keep-power-in-suspend", NULL))
		host->mmc->pm_caps |= MMC_PM_KEEP_POWER;

	if (of_property_read_bool(np, "wakeup-source") ||
	    of_property_read_bool(np, "enable-sdio-wakeup")) /* legacy */
		host->mmc->pm_caps |= MMC_PM_WAKE_SDIO_IRQ;
}
```

## mmc_of_parse

```c
/**
 *	mmc_of_parse() - parse host's device-tree node
 *	@host: host whose node should be parsed.
 *
 * To keep the rest of the MMC subsystem unaware of whether DT has been
 * used to to instantiate and configure this host instance or not, we
 * parse the properties and set respective generic mmc-host flags and
 * parameters.
 */
int mmc_of_parse(struct mmc_host *host)
{
	struct device *dev = host->parent;
	u32 bus_width, drv_type, cd_debounce_delay_ms;
	int ret;
	bool cd_cap_invert, cd_gpio_invert = false;
	bool ro_cap_invert, ro_gpio_invert = false;

	/* "bus-width" is translated to MMC_CAP_*_BIT_DATA flags */
	if (device_property_read_u32(dev, "bus-width", &bus_width) < 0) {
		dev_dbg(host->parent,
			"\"bus-width\" property is missing, assuming 1 bit.\n");
		bus_width = 1;
	}

	switch (bus_width) {
	case 8:
		host->caps |= MMC_CAP_8_BIT_DATA;
		/* Hosts capable of 8-bit transfers can also do 4 bits */
	case 4:
		host->caps |= MMC_CAP_4_BIT_DATA;
		break;
	case 1:
		break;
	default:
		dev_err(host->parent,
			"Invalid \"bus-width\" value %u!\n", bus_width);
		return -EINVAL;
	}

	/* f_max is obtained from the optional "max-frequency" property */
	device_property_read_u32(dev, "max-frequency", &host->f_max);

	/*
	 * Configure CD and WP pins. They are both by default active low to
	 * match the SDHCI spec. If GPIOs are provided for CD and / or WP, the
	 * mmc-gpio helpers are used to attach, configure and use them. If
	 * polarity inversion is specified in DT, one of MMC_CAP2_CD_ACTIVE_HIGH
	 * and MMC_CAP2_RO_ACTIVE_HIGH capability-2 flags is set. If the
	 * "broken-cd" property is provided, the MMC_CAP_NEEDS_POLL capability
	 * is set. If the "non-removable" property is found, the
	 * MMC_CAP_NONREMOVABLE capability is set and no card-detection
	 * configuration is performed.
	 */

	/* Parse Card Detection */
	if (device_property_read_bool(dev, "non-removable")) {
		host->caps |= MMC_CAP_NONREMOVABLE;
	} else {
		cd_cap_invert = device_property_read_bool(dev, "cd-inverted");

		if (device_property_read_u32(dev, "cd-debounce-delay-ms",
					     &cd_debounce_delay_ms))
			cd_debounce_delay_ms = 200;

		if (device_property_read_bool(dev, "broken-cd"))
			host->caps |= MMC_CAP_NEEDS_POLL;

		ret = mmc_gpiod_request_cd(host, "cd", 0, true,
					   cd_debounce_delay_ms * 1000,
					   &cd_gpio_invert);
		if (!ret)
			dev_info(host->parent, "Got CD GPIO\n");
		else if (ret != -ENOENT && ret != -ENOSYS)
			return ret;

		/*
		 * There are two ways to flag that the CD line is inverted:
		 * through the cd-inverted flag and by the GPIO line itself
		 * being inverted from the GPIO subsystem. This is a leftover
		 * from the times when the GPIO subsystem did not make it
		 * possible to flag a line as inverted.
		 *
		 * If the capability on the host AND the GPIO line are
		 * both inverted, the end result is that the CD line is
		 * not inverted.
		 */
		if (cd_cap_invert ^ cd_gpio_invert)
			host->caps2 |= MMC_CAP2_CD_ACTIVE_HIGH;
	}

	/* Parse Write Protection */
	ro_cap_invert = device_property_read_bool(dev, "wp-inverted");

	ret = mmc_gpiod_request_ro(host, "wp", 0, false, 0, &ro_gpio_invert);
	if (!ret)
		dev_info(host->parent, "Got WP GPIO\n");
	else if (ret != -ENOENT && ret != -ENOSYS)
		return ret;

	if (device_property_read_bool(dev, "disable-wp"))
		host->caps2 |= MMC_CAP2_NO_WRITE_PROTECT;

	/* See the comment on CD inversion above */
	if (ro_cap_invert ^ ro_gpio_invert)
		host->caps2 |= MMC_CAP2_RO_ACTIVE_HIGH;

	if (device_property_read_bool(dev, "cap-sd-highspeed"))
		host->caps |= MMC_CAP_SD_HIGHSPEED;
	if (device_property_read_bool(dev, "cap-mmc-highspeed"))
		host->caps |= MMC_CAP_MMC_HIGHSPEED;
	if (device_property_read_bool(dev, "sd-uhs-sdr12"))
		host->caps |= MMC_CAP_UHS_SDR12;
	if (device_property_read_bool(dev, "sd-uhs-sdr25"))
		host->caps |= MMC_CAP_UHS_SDR25;
	if (device_property_read_bool(dev, "sd-uhs-sdr50"))
		host->caps |= MMC_CAP_UHS_SDR50;
	if (device_property_read_bool(dev, "sd-uhs-sdr104"))
		host->caps |= MMC_CAP_UHS_SDR104;
	if (device_property_read_bool(dev, "sd-uhs-ddr50"))
		host->caps |= MMC_CAP_UHS_DDR50;
	if (device_property_read_bool(dev, "cap-power-off-card"))
		host->caps |= MMC_CAP_POWER_OFF_CARD;
	if (device_property_read_bool(dev, "cap-mmc-hw-reset"))
		host->caps |= MMC_CAP_HW_RESET;
	if (device_property_read_bool(dev, "cap-sdio-irq"))
		host->caps |= MMC_CAP_SDIO_IRQ;
	if (device_property_read_bool(dev, "full-pwr-cycle"))
		host->caps2 |= MMC_CAP2_FULL_PWR_CYCLE;
	if (device_property_read_bool(dev, "keep-power-in-suspend"))
		host->pm_caps |= MMC_PM_KEEP_POWER;
	if (device_property_read_bool(dev, "wakeup-source") ||
	    device_property_read_bool(dev, "enable-sdio-wakeup")) /* legacy */
		host->pm_caps |= MMC_PM_WAKE_SDIO_IRQ;
	if (device_property_read_bool(dev, "mmc-ddr-3_3v"))
		host->caps |= MMC_CAP_3_3V_DDR;
	if (device_property_read_bool(dev, "mmc-ddr-1_8v"))
		host->caps |= MMC_CAP_1_8V_DDR;
	if (device_property_read_bool(dev, "mmc-ddr-1_2v"))
		host->caps |= MMC_CAP_1_2V_DDR;
	if (device_property_read_bool(dev, "mmc-hs200-1_8v"))
		host->caps2 |= MMC_CAP2_HS200_1_8V_SDR;
	if (device_property_read_bool(dev, "mmc-hs200-1_2v"))
		host->caps2 |= MMC_CAP2_HS200_1_2V_SDR;
	if (device_property_read_bool(dev, "mmc-hs400-1_8v"))
		host->caps2 |= MMC_CAP2_HS400_1_8V | MMC_CAP2_HS200_1_8V_SDR;
	if (device_property_read_bool(dev, "mmc-hs400-1_2v"))
		host->caps2 |= MMC_CAP2_HS400_1_2V | MMC_CAP2_HS200_1_2V_SDR;
	if (device_property_read_bool(dev, "mmc-hs400-enhanced-strobe"))
		host->caps2 |= MMC_CAP2_HS400_ES;
	if (device_property_read_bool(dev, "no-sdio"))
		host->caps2 |= MMC_CAP2_NO_SDIO;
	if (device_property_read_bool(dev, "no-sd"))
		host->caps2 |= MMC_CAP2_NO_SD;
	if (device_property_read_bool(dev, "no-mmc"))
		host->caps2 |= MMC_CAP2_NO_MMC;

	/* Must be after "non-removable" check */
	if (device_property_read_u32(dev, "fixed-emmc-driver-type", &drv_type) == 0) {
		if (host->caps & MMC_CAP_NONREMOVABLE)
			host->fixed_drv_type = drv_type;
		else
			dev_err(host->parent,
				"can't use fixed driver type, media is removable\n");
	}

	host->dsr_req = !device_property_read_u32(dev, "dsr", &host->dsr);
	if (host->dsr_req && (host->dsr & ~0xffff)) {
		dev_err(host->parent,
			"device tree specified broken value for DSR: 0x%x, ignoring\n",
			host->dsr);
		host->dsr_req = 0;
	}

	device_property_read_u32(dev, "post-power-on-delay-ms",
				 &host->ios.power_delay_ms);

	return mmc_pwrseq_alloc(host);
}
```

## sdhci_arasan_add_host

```c
static int sdhci_arasan_add_host(struct sdhci_arasan_data *sdhci_arasan)
{
	struct sdhci_host *host = sdhci_arasan->host;

	/* has_cqe为false */
	if (!sdhci_arasan->has_cqe)
		return sdhci_add_host(host);
}
```

### sdhci_add_host

```c
int sdhci_add_host(struct sdhci_host *host)
{
	int ret;
	ret = sdhci_setup_host(host);
	ret = __sdhci_add_host(host);
	return 0;
}
```

### sdhci_setup_host

```c
int sdhci_setup_host(struct sdhci_host *host)
{
	struct mmc_host *mmc;
	u32 max_current_caps;
	unsigned int ocr_avail;
	unsigned int override_timeout_clk;
	u32 max_clk;
	int ret;

	mmc = host->mmc;

	/*
	 * If there are external regulators, get them. Note this must be done
	 * early before resetting the host and reading the capabilities so that
	 * the host can take the appropriate action if regulators are not
	 * available.
	 */
    /* 获取vmmc和vqmmc regulotor,获取不到也没有关系 */
	ret = mmc_regulator_get_supply(mmc);

	DBG("Version:   0x%08x | Present:  0x%08x\n",
	    sdhci_readw(host, SDHCI_HOST_VERSION),
	    sdhci_readl(host, SDHCI_PRESENT_STATE));
	DBG("Caps:      0x%08x | Caps_1:   0x%08x\n",
	    sdhci_readl(host, SDHCI_CAPABILITIES),
	    sdhci_readl(host, SDHCI_CAPABILITIES_1));

	sdhci_read_caps(host);

	override_timeout_clk = host->timeout_clk;

	if (host->quirks & SDHCI_QUIRK_FORCE_DMA)
		host->flags |= SDHCI_USE_SDMA;
	else if (!(host->caps & SDHCI_CAN_DO_SDMA))
		DBG("Controller doesn't have SDMA capability\n");
	else
		host->flags |= SDHCI_USE_SDMA;

	if ((host->quirks & SDHCI_QUIRK_BROKEN_DMA) &&
		(host->flags & SDHCI_USE_SDMA)) {
		DBG("Disabling DMA as it is marked broken\n");
		host->flags &= ~SDHCI_USE_SDMA;
	}

	if ((host->version >= SDHCI_SPEC_200) &&
		(host->caps & SDHCI_CAN_DO_ADMA2))
		host->flags |= SDHCI_USE_ADMA;

	if ((host->quirks & SDHCI_QUIRK_BROKEN_ADMA) &&
		(host->flags & SDHCI_USE_ADMA)) {
		DBG("Disabling ADMA as it is marked broken\n");
		host->flags &= ~SDHCI_USE_ADMA;
	}

	/*
	 * It is assumed that a 64-bit capable device has set a 64-bit DMA mask
	 * and *must* do 64-bit DMA.  A driver has the opportunity to change
	 * that during the first call to ->enable_dma().  Similarly
	 * SDHCI_QUIRK2_BROKEN_64_BIT_DMA must be left to the drivers to
	 * implement.
	 */
	if (host->caps & SDHCI_CAN_64BIT)
		host->flags |= SDHCI_USE_64_BIT_DMA;

	if (host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)) {
		ret = sdhci_set_dma_mask(host);

		if (!ret && host->ops->enable_dma)
			ret = host->ops->enable_dma(host);
	}

	/* SDMA does not support 64-bit DMA */
	if (host->flags & SDHCI_USE_64_BIT_DMA)
		host->flags &= ~SDHCI_USE_SDMA;

	if (host->flags & SDHCI_USE_ADMA) {
		dma_addr_t dma;
		void *buf;

		/*
		 * The DMA descriptor table size is calculated as the maximum
		 * number of segments times 2, to allow for an alignment
		 * descriptor for each segment, plus 1 for a nop end descriptor,
		 * all multipled by the descriptor size.
		 */
		if (host->flags & SDHCI_USE_64_BIT_DMA) {
			host->adma_table_sz = (SDHCI_MAX_SEGS * 2 + 1) *
					      SDHCI_ADMA2_64_DESC_SZ;
			host->desc_sz = SDHCI_ADMA2_64_DESC_SZ;
		} else {
			host->adma_table_sz = (SDHCI_MAX_SEGS * 2 + 1) *
					      SDHCI_ADMA2_32_DESC_SZ;
			host->desc_sz = SDHCI_ADMA2_32_DESC_SZ;
		}

		host->align_buffer_sz = SDHCI_MAX_SEGS * SDHCI_ADMA2_ALIGN;
		buf = dma_alloc_coherent(mmc_dev(mmc), host->align_buffer_sz +
					 host->adma_table_sz, &dma, GFP_KERNEL);
		if (!buf) {
			pr_warn("%s: Unable to allocate ADMA buffers - falling back to standard DMA\n",
				mmc_hostname(mmc));
			host->flags &= ~SDHCI_USE_ADMA;
		} else if ((dma + host->align_buffer_sz) &
			   (SDHCI_ADMA2_DESC_ALIGN - 1)) {
			pr_warn("%s: unable to allocate aligned ADMA descriptor\n",
				mmc_hostname(mmc));
			host->flags &= ~SDHCI_USE_ADMA;
			dma_free_coherent(mmc_dev(mmc), host->align_buffer_sz +
					  host->adma_table_sz, buf, dma);
		} else {
			host->align_buffer = buf;
			host->align_addr = dma;

			host->adma_table = buf + host->align_buffer_sz;
			host->adma_addr = dma + host->align_buffer_sz;
		}
	}

	/*
	 * If we use DMA, then it's up to the caller to set the DMA
	 * mask, but PIO does not need the hw shim so we set a new
	 * mask here in that case.
	 */
	if (!(host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA))) {
		host->dma_mask = DMA_BIT_MASK(64);
		mmc_dev(mmc)->dma_mask = &host->dma_mask;
	}

	if (host->version >= SDHCI_SPEC_300)
		host->max_clk = (host->caps & SDHCI_CLOCK_V3_BASE_MASK)
			>> SDHCI_CLOCK_BASE_SHIFT;
	else
		host->max_clk = (host->caps & SDHCI_CLOCK_BASE_MASK)
			>> SDHCI_CLOCK_BASE_SHIFT;

	host->max_clk *= 1000000;
	if (host->max_clk == 0 || host->quirks &
			SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN) {
		if (!host->ops->get_max_clock) {
			pr_err("%s: Hardware doesn't specify base clock frequency.\n",
			       mmc_hostname(mmc));
			ret = -ENODEV;
			goto undma;
		}
		host->max_clk = host->ops->get_max_clock(host);
	}

	/*
	 * In case of Host Controller v3.00, find out whether clock
	 * multiplier is supported.
	 */
	host->clk_mul = (host->caps1 & SDHCI_CLOCK_MUL_MASK) >> SDHCI_CLOCK_MUL_SHIFT;

	/*
	 * In case the value in Clock Multiplier is 0, then programmable
	 * clock mode is not supported, otherwise the actual clock
	 * multiplier is one more than the value of Clock Multiplier
	 * in the Capabilities Register.
	 */
	if (host->clk_mul)
		host->clk_mul += 1;

	/*
	 * Set host parameters.
	 */
	max_clk = host->max_clk;

	if (host->ops->get_min_clock)
		mmc->f_min = host->ops->get_min_clock(host);
	else if (host->version >= SDHCI_SPEC_300) {
		if (host->clk_mul) {
			mmc->f_min = (host->max_clk * host->clk_mul) / 1024;
			max_clk = host->max_clk * host->clk_mul;
		} else
			mmc->f_min = host->max_clk / SDHCI_MAX_DIV_SPEC_300;
	} else
		mmc->f_min = host->max_clk / SDHCI_MAX_DIV_SPEC_200;

	if (!mmc->f_max || mmc->f_max > max_clk)
		mmc->f_max = max_clk;

	if (!(host->quirks & SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK)) {
		host->timeout_clk = (host->caps & SDHCI_TIMEOUT_CLK_MASK) >>
					SDHCI_TIMEOUT_CLK_SHIFT;

		if (host->caps & SDHCI_TIMEOUT_CLK_UNIT)
			host->timeout_clk *= 1000;

		if (host->timeout_clk == 0) {
			if (!host->ops->get_timeout_clock) {
				pr_err("%s: Hardware doesn't specify timeout clock frequency.\n",
					mmc_hostname(mmc));
				ret = -ENODEV;
				goto undma;
			}

			host->timeout_clk =
				DIV_ROUND_UP(host->ops->get_timeout_clock(host),
					     1000);
		}

		if (override_timeout_clk)
			host->timeout_clk = override_timeout_clk;

		mmc->max_busy_timeout = host->ops->get_max_timeout_count ?
			host->ops->get_max_timeout_count(host) : 1 << 27;
		mmc->max_busy_timeout /= host->timeout_clk;
	}

	if (host->quirks2 & SDHCI_QUIRK2_DISABLE_HW_TIMEOUT &&
	    !host->ops->get_max_timeout_count)
		mmc->max_busy_timeout = 0;

	mmc->caps |= MMC_CAP_SDIO_IRQ | MMC_CAP_ERASE | MMC_CAP_CMD23;
	mmc->caps2 |= MMC_CAP2_SDIO_IRQ_NOTHREAD;

	if (host->quirks & SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12)
		host->flags |= SDHCI_AUTO_CMD12;

	/* Auto-CMD23 stuff only works in ADMA or PIO. */
	if ((host->version >= SDHCI_SPEC_300) &&
	    ((host->flags & SDHCI_USE_ADMA) ||
	     !(host->flags & SDHCI_USE_SDMA)) &&
	     !(host->quirks2 & SDHCI_QUIRK2_ACMD23_BROKEN)) {
		host->flags |= SDHCI_AUTO_CMD23;
		DBG("Auto-CMD23 available\n");
	} else {
		DBG("Auto-CMD23 unavailable\n");
	}

	/*
	 * A controller may support 8-bit width, but the board itself
	 * might not have the pins brought out.  Boards that support
	 * 8-bit width must set "mmc->caps |= MMC_CAP_8_BIT_DATA;" in
	 * their platform code before calling sdhci_add_host(), and we
	 * won't assume 8-bit width for hosts without that CAP.
	 */
	if (!(host->quirks & SDHCI_QUIRK_FORCE_1_BIT_DATA))
		mmc->caps |= MMC_CAP_4_BIT_DATA;

	if (host->quirks2 & SDHCI_QUIRK2_HOST_NO_CMD23)
		mmc->caps &= ~MMC_CAP_CMD23;

	if ((host->caps & SDHCI_CAN_DO_HISPD) &&
		!(host->quirks & SDHCI_QUIRK_NO_HISPD_BIT))
		mmc->caps |= MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED;

	if ((host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION) &&
	    mmc_card_is_removable(mmc) &&
	    mmc_gpio_get_cd(host->mmc) < 0)
		mmc->caps |= MMC_CAP_NEEDS_POLL;

	if (!IS_ERR(mmc->supply.vqmmc)) {
		ret = regulator_enable(mmc->supply.vqmmc);

		/* If vqmmc provides no 1.8V signalling, then there's no UHS */
		if (!regulator_is_supported_voltage(mmc->supply.vqmmc, 1700000,
						    1950000))
			host->caps1 &= ~(SDHCI_SUPPORT_SDR104 |
					 SDHCI_SUPPORT_SDR50 |
					 SDHCI_SUPPORT_DDR50);

		/* In eMMC case vqmmc might be a fixed 1.8V regulator */
		if (!regulator_is_supported_voltage(mmc->supply.vqmmc, 2700000,
						    3600000))
			host->flags &= ~SDHCI_SIGNALING_330;
	}

	if (host->quirks2 & SDHCI_QUIRK2_NO_1_8_V) {
		host->caps1 &= ~(SDHCI_SUPPORT_SDR104 | SDHCI_SUPPORT_SDR50 |
				 SDHCI_SUPPORT_DDR50);
		/*
		 * The SDHCI controller in a SoC might support HS200/HS400
		 * (indicated using mmc-hs200-1_8v/mmc-hs400-1_8v dt property),
		 * but if the board is modeled such that the IO lines are not
		 * connected to 1.8v then HS200/HS400 cannot be supported.
		 * Disable HS200/HS400 if the board does not have 1.8v connected
		 * to the IO lines. (Applicable for other modes in 1.8v)
		 */
		mmc->caps2 &= ~(MMC_CAP2_HSX00_1_8V | MMC_CAP2_HS400_ES);
		mmc->caps &= ~(MMC_CAP_1_8V_DDR | MMC_CAP_UHS);
	}

	/* Any UHS-I mode in caps implies SDR12 and SDR25 support. */
	if (host->caps1 & (SDHCI_SUPPORT_SDR104 | SDHCI_SUPPORT_SDR50 |
			   SDHCI_SUPPORT_DDR50))
		mmc->caps |= MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25;

	/* SDR104 supports also implies SDR50 support */
	if (host->caps1 & SDHCI_SUPPORT_SDR104) {
		mmc->caps |= MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_SDR50;
		/* SD3.0: SDR104 is supported so (for eMMC) the caps2
		 * field can be promoted to support HS200.
		 */
		if (!(host->quirks2 & SDHCI_QUIRK2_BROKEN_HS200))
			mmc->caps2 |= MMC_CAP2_HS200;
	} else if (host->caps1 & SDHCI_SUPPORT_SDR50) {
		mmc->caps |= MMC_CAP_UHS_SDR50;
	}

	if (host->quirks2 & SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400 &&
	    (host->caps1 & SDHCI_SUPPORT_HS400))
		mmc->caps2 |= MMC_CAP2_HS400;

	if ((mmc->caps2 & MMC_CAP2_HSX00_1_2V) &&
	    (IS_ERR(mmc->supply.vqmmc) ||
	     !regulator_is_supported_voltage(mmc->supply.vqmmc, 1100000,
					     1300000)))
		mmc->caps2 &= ~MMC_CAP2_HSX00_1_2V;

	if ((host->caps1 & SDHCI_SUPPORT_DDR50) &&
	    !(host->quirks2 & SDHCI_QUIRK2_BROKEN_DDR50))
		mmc->caps |= MMC_CAP_UHS_DDR50;

	/* Does the host need tuning for SDR50? */
	if (host->caps1 & SDHCI_USE_SDR50_TUNING)
		host->flags |= SDHCI_SDR50_NEEDS_TUNING;

	/* Driver Type(s) (A, C, D) supported by the host */
	if (host->caps1 & SDHCI_DRIVER_TYPE_A)
		mmc->caps |= MMC_CAP_DRIVER_TYPE_A;
	if (host->caps1 & SDHCI_DRIVER_TYPE_C)
		mmc->caps |= MMC_CAP_DRIVER_TYPE_C;
	if (host->caps1 & SDHCI_DRIVER_TYPE_D)
		mmc->caps |= MMC_CAP_DRIVER_TYPE_D;

	/* Initial value for re-tuning timer count */
	host->tuning_count = (host->caps1 & SDHCI_RETUNING_TIMER_COUNT_MASK) >>
			     SDHCI_RETUNING_TIMER_COUNT_SHIFT;

	/*
	 * In case Re-tuning Timer is not disabled, the actual value of
	 * re-tuning timer will be 2 ^ (n - 1).
	 */
	if (host->tuning_count)
		host->tuning_count = 1 << (host->tuning_count - 1);

	/* Re-tuning mode supported by the Host Controller */
	host->tuning_mode = (host->caps1 & SDHCI_RETUNING_MODE_MASK) >>
			     SDHCI_RETUNING_MODE_SHIFT;

	ocr_avail = 0;

	/*
	 * According to SD Host Controller spec v3.00, if the Host System
	 * can afford more than 150mA, Host Driver should set XPC to 1. Also
	 * the value is meaningful only if Voltage Support in the Capabilities
	 * register is set. The actual current value is 4 times the register
	 * value.
	 */
	max_current_caps = sdhci_readl(host, SDHCI_MAX_CURRENT);
	if (!max_current_caps && !IS_ERR(mmc->supply.vmmc)) {
		int curr = regulator_get_current_limit(mmc->supply.vmmc);
		if (curr > 0) {

			/* convert to SDHCI_MAX_CURRENT format */
			curr = curr/1000;  /* convert to mA */
			curr = curr/SDHCI_MAX_CURRENT_MULTIPLIER;

			curr = min_t(u32, curr, SDHCI_MAX_CURRENT_LIMIT);
			max_current_caps =
				(curr << SDHCI_MAX_CURRENT_330_SHIFT) |
				(curr << SDHCI_MAX_CURRENT_300_SHIFT) |
				(curr << SDHCI_MAX_CURRENT_180_SHIFT);
		}
	}

	if (host->caps & SDHCI_CAN_VDD_330) {
		ocr_avail |= MMC_VDD_32_33 | MMC_VDD_33_34;

		mmc->max_current_330 = ((max_current_caps & SDHCI_MAX_CURRENT_330_MASK) >>
				   SDHCI_MAX_CURRENT_330_SHIFT) *
				   SDHCI_MAX_CURRENT_MULTIPLIER;
	}
	if (host->caps & SDHCI_CAN_VDD_300) {
		ocr_avail |= MMC_VDD_29_30 | MMC_VDD_30_31;

		mmc->max_current_300 = ((max_current_caps & SDHCI_MAX_CURRENT_300_MASK) >>
				   SDHCI_MAX_CURRENT_300_SHIFT) *
				   SDHCI_MAX_CURRENT_MULTIPLIER;
	}
	if (host->caps & SDHCI_CAN_VDD_180) {
		ocr_avail |= MMC_VDD_165_195;

		mmc->max_current_180 = ((max_current_caps & SDHCI_MAX_CURRENT_180_MASK) >>
				   SDHCI_MAX_CURRENT_180_SHIFT) *
				   SDHCI_MAX_CURRENT_MULTIPLIER;
	}

	/* If OCR set by host, use it instead. */
	if (host->ocr_mask)
		ocr_avail = host->ocr_mask;

	/* If OCR set by external regulators, give it highest prio. */
	if (mmc->ocr_avail)
		ocr_avail = mmc->ocr_avail;

	mmc->ocr_avail = ocr_avail;
	mmc->ocr_avail_sdio = ocr_avail;
	if (host->ocr_avail_sdio)
		mmc->ocr_avail_sdio &= host->ocr_avail_sdio;
	mmc->ocr_avail_sd = ocr_avail;
	if (host->ocr_avail_sd)
		mmc->ocr_avail_sd &= host->ocr_avail_sd;
	else /* normal SD controllers don't support 1.8V */
		mmc->ocr_avail_sd &= ~MMC_VDD_165_195;
	mmc->ocr_avail_mmc = ocr_avail;
	if (host->ocr_avail_mmc)
		mmc->ocr_avail_mmc &= host->ocr_avail_mmc;

	if (mmc->ocr_avail == 0) {
		pr_err("%s: Hardware doesn't report any support voltages.\n",
		       mmc_hostname(mmc));
		ret = -ENODEV;
		goto unreg;
	}

	if ((mmc->caps & (MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
			  MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104 |
			  MMC_CAP_UHS_DDR50 | MMC_CAP_1_8V_DDR)) ||
	    (mmc->caps2 & (MMC_CAP2_HS200_1_8V_SDR | MMC_CAP2_HS400_1_8V)))
		host->flags |= SDHCI_SIGNALING_180;

	if (mmc->caps2 & MMC_CAP2_HSX00_1_2V)
		host->flags |= SDHCI_SIGNALING_120;

	spin_lock_init(&host->lock);

	/*
	 * Maximum number of sectors in one transfer. Limited by SDMA boundary
	 * size (512KiB). Note some tuning modes impose a 4MiB limit, but this
	 * is less anyway.
	 */
	mmc->max_req_size = 524288;

	/*
	 * Maximum number of segments. Depends on if the hardware
	 * can do scatter/gather or not.
	 */
	if (host->flags & SDHCI_USE_ADMA) {
		mmc->max_segs = SDHCI_MAX_SEGS;
	} else if (host->flags & SDHCI_USE_SDMA) {
		mmc->max_segs = 1;
		if (swiotlb_max_segment()) {
			unsigned int max_req_size = (1 << IO_TLB_SHIFT) *
						IO_TLB_SEGSIZE;
			mmc->max_req_size = min(mmc->max_req_size,
						max_req_size);
		}
	} else { /* PIO */
		mmc->max_segs = SDHCI_MAX_SEGS;
	}

	/*
	 * Maximum segment size. Could be one segment with the maximum number
	 * of bytes. When doing hardware scatter/gather, each entry cannot
	 * be larger than 64 KiB though.
	 */
	if (host->flags & SDHCI_USE_ADMA) {
		if (host->quirks & SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC)
			mmc->max_seg_size = 65535;
		else
			mmc->max_seg_size = 65536;
	} else {
		mmc->max_seg_size = mmc->max_req_size;
	}

	/*
	 * Maximum block size. This varies from controller to controller and
	 * is specified in the capabilities register.
	 */
	if (host->quirks & SDHCI_QUIRK_FORCE_BLK_SZ_2048) {
		mmc->max_blk_size = 2;
	} else {
		mmc->max_blk_size = (host->caps & SDHCI_MAX_BLOCK_MASK) >>
				SDHCI_MAX_BLOCK_SHIFT;
		if (mmc->max_blk_size >= 3) {
			pr_warn("%s: Invalid maximum block size, assuming 512 bytes\n",
				mmc_hostname(mmc));
			mmc->max_blk_size = 0;
		}
	}

	mmc->max_blk_size = 512 << mmc->max_blk_size;

	/*
	 * Maximum block count.
	 */
	mmc->max_blk_count = (host->quirks & SDHCI_QUIRK_NO_MULTIBLOCK) ? 1 : 65535;

	if (mmc->max_segs == 1) {
		/* This may alter mmc->*_blk_* parameters */
		ret = sdhci_allocate_bounce_buffer(host);
		if (ret)
			return ret;
	}

	return 0;
}
```
#### mmc_regulator_get_supply
```c
/**
 * mmc_regulator_get_supply - try to get VMMC and VQMMC regulators for a host
 * @mmc: the host to regulate
 *
 * Returns 0 or errno. errno should be handled, it is either a critical error
 * or -EPROBE_DEFER. 0 means no critical error but it does not mean all
 * regulators have been found because they all are optional. If you require
 * certain regulators, you need to check separately in your driver if they got
 * populated after calling this function.
 */
int mmc_regulator_get_supply(struct mmc_host *mmc)
{
	struct device *dev = mmc_dev(mmc);
	int ret;

	mmc->supply.vmmc = devm_regulator_get_optional(dev, "vmmc");
	mmc->supply.vqmmc = devm_regulator_get_optional(dev, "vqmmc");

	if (IS_ERR(mmc->supply.vmmc)) {
		if (PTR_ERR(mmc->supply.vmmc) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_dbg(dev, "No vmmc regulator found\n");
	} else {
		ret = mmc_regulator_get_ocrmask(mmc->supply.vmmc);
		if (ret > 0)
			mmc->ocr_avail = ret;
		else
			dev_warn(dev, "Failed getting OCR mask: %d\n", ret);
	}

	if (IS_ERR(mmc->supply.vqmmc)) {
		if (PTR_ERR(mmc->supply.vqmmc) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_dbg(dev, "No vqmmc regulator found\n");
	}

	return 0;
}
```

#### sdhci_read_caps

```c
static inline void sdhci_read_caps(struct sdhci_host *host)
{
	__sdhci_read_caps(host, NULL, NULL, NULL);
}
```

##### __sdhci_read_caps

```c
void __sdhci_read_caps(struct sdhci_host *host, u16 *ver, u32 *caps, u32 *caps1)
{
	u16 v;
	u64 dt_caps_mask = 0;
	u64 dt_caps = 0;

	if (host->read_caps)
		return;

	host->read_caps = true;

	if (debug_quirks)
		host->quirks = debug_quirks;

	if (debug_quirks2)
		host->quirks2 = debug_quirks2;

	sdhci_do_reset(host, SDHCI_RESET_ALL);

	of_property_read_u64(mmc_dev(host->mmc)->of_node,
			     "sdhci-caps-mask", &dt_caps_mask);
	of_property_read_u64(mmc_dev(host->mmc)->of_node,
			     "sdhci-caps", &dt_caps);

	v = ver ? *ver : sdhci_readw(host, SDHCI_HOST_VERSION);
	host->version = (v & SDHCI_SPEC_VER_MASK) >> SDHCI_SPEC_VER_SHIFT;

	if (host->quirks & SDHCI_QUIRK_MISSING_CAPS)
		return;

	if (caps) {
		host->caps = *caps;
	} else {
		host->caps = sdhci_readl(host, SDHCI_CAPABILITIES);
		host->caps &= ~lower_32_bits(dt_caps_mask);
		host->caps |= lower_32_bits(dt_caps);
	}

	if (host->version < SDHCI_SPEC_300)
		return;

	if (caps1) {
		host->caps1 = *caps1;
	} else {
		host->caps1 = sdhci_readl(host, SDHCI_CAPABILITIES_1);
		host->caps1 &= ~upper_32_bits(dt_caps_mask);
		host->caps1 |= upper_32_bits(dt_caps);
	}
}
```

### __sdhci_add_host

```c
int __sdhci_add_host(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	int ret;

	/*
	 * Init tasklets.
	 */
	tasklet_init(&host->finish_tasklet, sdhci_tasklet_finish, (unsigned long)host);

	timer_setup(&host->timer, sdhci_timeout_timer, 0);
	timer_setup(&host->data_timer, sdhci_timeout_data_timer, 0);

	init_waitqueue_head(&host->buf_ready_int);

	sdhci_init(host, 0);
	/* 注册中断 */
	ret = request_threaded_irq(host->irq, sdhci_irq, sdhci_thread_irq,
				   IRQF_SHARED,	mmc_hostname(mmc), host);
	/* 注册led，暂不分析 */
	ret = sdhci_led_register(host);

	mmiowb();

	ret = mmc_add_host(mmc);

	pr_info("%s: SDHCI controller on %s [%s] using %s\n",
		mmc_hostname(mmc), host->hw_name, dev_name(mmc_dev(mmc)),
		(host->flags & SDHCI_USE_ADMA) ?
		(host->flags & SDHCI_USE_64_BIT_DMA) ? "ADMA 64-bit" : "ADMA" :
		(host->flags & SDHCI_USE_SDMA) ? "DMA" : "PIO");

	sdhci_enable_card_detection(host);

	return 0;
}
```

#### sdhci_init

```c
static void sdhci_init(struct sdhci_host *host, int soft)
{
	struct mmc_host *mmc = host->mmc;

	if (soft)
		sdhci_do_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
	else
		sdhci_do_reset(host, SDHCI_RESET_ALL);

	sdhci_set_default_irqs(host);

	host->cqe_on = false;

	if (soft) {
		/* force clock reconfiguration */
		host->clock = 0;
		mmc->ops->set_ios(mmc, &mmc->ios);
	}
}
```

##### sdhci_set_default_irqs

```c
static void sdhci_set_default_irqs(struct sdhci_host *host)
{
	host->ier = SDHCI_INT_BUS_POWER | SDHCI_INT_DATA_END_BIT |
		    SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_TIMEOUT |
		    SDHCI_INT_INDEX | SDHCI_INT_END_BIT | SDHCI_INT_CRC |
		    SDHCI_INT_TIMEOUT | SDHCI_INT_DATA_END |
		    SDHCI_INT_RESPONSE;

	if (host->tuning_mode == SDHCI_TUNING_MODE_2 ||
	    host->tuning_mode == SDHCI_TUNING_MODE_3)
		host->ier |= SDHCI_INT_RETUNE;
	/* 使能中断 */
	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}
```

#### mmc_add_host

```c
int mmc_add_host(struct mmc_host *host)
{
	int err;

	err = device_add(&host->class_dev);

	led_trigger_register_simple(dev_name(&host->class_dev), &host->led);

#ifdef CONFIG_DEBUG_FS
	mmc_add_host_debugfs(host);
#endif

	mmc_start_host(host);
	mmc_register_pm_notifier(host);

	return 0;
}
```

##### mmc_start_host

```c
void mmc_start_host(struct mmc_host *host)
{
	host->f_init = max(freqs[0], host->f_min);
	host->rescan_disable = 0;
	host->ios.power_mode = MMC_POWER_UNDEFINED;

	if (!(host->caps2 & MMC_CAP2_NO_PRESCAN_POWERUP)) {
		mmc_claim_host(host);
		/* 上电 */
		mmc_power_up(host, host->ocr_avail);
		mmc_release_host(host);
	}

	mmc_gpiod_request_cd_irq(host);
	_mmc_detect_change(host, 0, false);
}
```

###### mmc_gpiod_request_cd_irq

```c
void mmc_gpiod_request_cd_irq(struct mmc_host *host)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;
	int irq = -EINVAL;
	int ret;
	/*
	 * Do not use IRQ if the platform prefers to poll, e.g., because that
	 * IRQ number is already used by another unit and cannot be shared.
	 */
	if (!(host->caps & MMC_CAP_NEEDS_POLL))
		irq = gpiod_to_irq(ctx->cd_gpio);

	if (irq >= 0) {
		if (!ctx->cd_gpio_isr)
			ctx->cd_gpio_isr = mmc_gpio_cd_irqt;
		ret = devm_request_threaded_irq(host->parent, irq,
			NULL, ctx->cd_gpio_isr,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			ctx->cd_label, host);
		if (ret < 0)
			irq = ret;
	}

	host->slot.cd_irq = irq;

	if (irq < 0)
		host->caps |= MMC_CAP_NEEDS_POLL;
}

```

#### _mmc_detect_change

```c
static void _mmc_detect_change(struct mmc_host *host, unsigned long delay,
				bool cd_irq)
{
	/*
	 * If the device is configured as wakeup, we prevent a new sleep for
	 * 5 s to give provision for user space to consume the event.
	 */
	if (cd_irq && !(host->caps & MMC_CAP_NEEDS_POLL) &&
		device_can_wakeup(mmc_dev(host)))
		pm_wakeup_event(mmc_dev(host), 5000);

	host->detect_change = 1;
	/* 执行mmc_rescan工作队列, 稍后在看 */
	mmc_schedule_delayed_work(&host->detect, delay);
}
```

#### sdhci_enable_card_detection

```c
static void sdhci_enable_card_detection(struct sdhci_host *host)
{
	sdhci_set_card_detection(host, true);
}

static void sdhci_set_card_detection(struct sdhci_host *host, bool enable)
{
	u32 present;

	if ((host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION) ||
	    !mmc_card_is_removable(host->mmc))
		return;

	if (enable) {
		present = sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT;
		/* 使card 在位检测，这个和cd pin有什么不同，还不清楚 */
		host->ier |= present ? SDHCI_INT_CARD_REMOVE : SDHCI_INT_CARD_INSERT;
	} else {
		host->ier &= ~(SDHCI_INT_CARD_REMOVE | SDHCI_INT_CARD_INSERT);
	}

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}
```

