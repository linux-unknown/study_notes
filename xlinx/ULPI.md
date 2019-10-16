# ULPI

## ulpi.c

### ulpi_init
```c
static int __init ulpi_init(void)
{
	return bus_register(&ulpi_bus);
}
subsys_initcall(ulpi_init);
```
### ulpi_bus
```c
static struct bus_type ulpi_bus = {
	.name = "ulpi",
	.match = ulpi_match,
	.uevent = ulpi_uevent,
	.probe = ulpi_probe,
	.remove = ulpi_remove,
};
```

### ulpi_match

```c
static int ulpi_match(struct device *dev, struct device_driver *driver)
{
	struct ulpi_driver *drv = to_ulpi_driver(driver);
	struct ulpi *ulpi = to_ulpi_dev(dev);
	const struct ulpi_device_id *id;

	/* Some ULPI devices don't have a vendor id so rely on OF match */
    /* 如果ULPI devices没有vendor id，则是通过devie tree进行匹配 */
	if (ulpi->id.vendor == 0)
		return of_driver_match_device(dev, driver);

	/* 和driver中的id_table进行比较 */
	for (id = drv->id_table; id->vendor; id++)
		if (id->vendor == ulpi->id.vendor &&
		    id->product == ulpi->id.product)
			return 1;

	return 0;
}
```

