/** 以jz4740_nand.c为例子进行分析 **/

/**
 * struct nand_ecc_ctrl - Control structure for ECC
 * @mode:	ECC mode
 * @steps:	number of ECC steps per page
 * @size:	data bytes per ECC step
 * @bytes:	ECC bytes per step
 * @strength:	max number of correctible bits per ECC step
 * @total:	total number of ECC bytes per page
 * @prepad:	padding information for syndrome based ECC generators
 * @postpad:	padding information for syndrome based ECC generators
 * @layout:	ECC layout control struct pointer
 * @priv:	pointer to private ECC control data
 * @hwctl:	function to control hardware ECC generator. Must only
 *		be provided if an hardware ECC is available
 * @calculate:	function for ECC calculation or readback from ECC hardware
 * @correct:	function for ECC correction, matching to ECC generator (sw/hw)
 * @read_page_raw:	function to read a raw page without ECC. This function
 *			should hide the specific layout used by the ECC
 *			controller and always return contiguous in-band and
 *			out-of-band data even if they're not stored
 *			contiguously on the NAND chip (e.g.
 *			NAND_ECC_HW_SYNDROME interleaves in-band and
 *			out-of-band data).
 * @write_page_raw:	function to write a raw page without ECC. This function
 *			should hide the specific layout used by the ECC
 *			controller and consider the passed data as contiguous
 *			in-band and out-of-band data. ECC controller is
 *			responsible for doing the appropriate transformations
 *			to adapt to its specific layout (e.g.
 *			NAND_ECC_HW_SYNDROME interleaves in-band and
 *			out-of-band data).
 * @read_page:	function to read a page according to the ECC generator
 *		requirements; returns maximum number of bitflips corrected in
 *		any single ECC step, 0 if bitflips uncorrectable, -EIO hw error
 * @read_subpage:	function to read parts of the page covered by ECC;
 *			returns same as read_page()
 * @write_subpage:	function to write parts of the page covered by ECC.
 * @write_page:	function to write a page according to the ECC generator
 *		requirements.
 * @write_oob_raw:	function to write chip OOB data without ECC
 * @read_oob_raw:	function to read chip OOB data without ECC
 * @read_oob:	function to read chip OOB data
 * @write_oob:	function to write chip OOB data
 */
struct nand_ecc_ctrl {
	nand_ecc_modes_t mode;
	int steps;
	int size;
	int bytes;
	int total;
	int strength;
	int prepad;
	int postpad;
	struct nand_ecclayout	*layout;
	void *priv;
	void (*hwctl)(struct mtd_info *mtd, int mode);
	int (*calculate)(struct mtd_info *mtd, const uint8_t *dat,
			uint8_t *ecc_code);
	int (*correct)(struct mtd_info *mtd, uint8_t *dat, uint8_t *read_ecc,
			uint8_t *calc_ecc);
	int (*read_page_raw)(struct mtd_info *mtd, struct nand_chip *chip,
			uint8_t *buf, int oob_required, int page);
	int (*write_page_raw)(struct mtd_info *mtd, struct nand_chip *chip,
			const uint8_t *buf, int oob_required);
	int (*read_page)(struct mtd_info *mtd, struct nand_chip *chip,
			uint8_t *buf, int oob_required, int page);
	int (*read_subpage)(struct mtd_info *mtd, struct nand_chip *chip,
			uint32_t offs, uint32_t len, uint8_t *buf, int page);
	int (*write_subpage)(struct mtd_info *mtd, struct nand_chip *chip,
			uint32_t offset, uint32_t data_len,
			const uint8_t *data_buf, int oob_required);
	int (*write_page)(struct mtd_info *mtd, struct nand_chip *chip,
			const uint8_t *buf, int oob_required);
	int (*write_oob_raw)(struct mtd_info *mtd, struct nand_chip *chip,
			int page);
	int (*read_oob_raw)(struct mtd_info *mtd, struct nand_chip *chip,
			int page);
	int (*read_oob)(struct mtd_info *mtd, struct nand_chip *chip, int page);
	int (*write_oob)(struct mtd_info *mtd, struct nand_chip *chip,
			int page);
};



/**
 * struct nand_chip - NAND Private Flash Chip Data
 * @IO_ADDR_R:		[BOARDSPECIFIC] address to read the 8 I/O lines of the
 *			flash device
 * @IO_ADDR_W:		[BOARDSPECIFIC] address to write the 8 I/O lines of the
 *			flash device.
 * @read_byte:		[REPLACEABLE] read one byte from the chip
 * @read_word:		[REPLACEABLE] read one word from the chip
 * @write_byte:		[REPLACEABLE] write a single byte to the chip on the
 *			low 8 I/O lines
 * @write_buf:		[REPLACEABLE] write data from the buffer to the chip
 * @read_buf:		[REPLACEABLE] read data from the chip into the buffer
 * @select_chip:	[REPLACEABLE] select chip nr
 * @block_bad:		[REPLACEABLE] check if a block is bad, using OOB markers
 * @block_markbad:	[REPLACEABLE] mark a block bad
 * @cmd_ctrl:		[BOARDSPECIFIC] hardwarespecific function for controlling
 *			ALE/CLE/nCE. Also used to write command and address
 * @init_size:		[BOARDSPECIFIC] hardwarespecific function for setting
 *			mtd->oobsize, mtd->writesize and so on.
 *			@id_data contains the 8 bytes values of NAND_CMD_READID.
 *			Return with the bus width.
 * @dev_ready:		[BOARDSPECIFIC] hardwarespecific function for accessing
 *			device ready/busy line. If set to NULL no access to
 *			ready/busy is available and the ready/busy information
 *			is read from the chip status register.
 * @cmdfunc:		[REPLACEABLE] hardwarespecific function for writing
 *			commands to the chip.
 * @waitfunc:		[REPLACEABLE] hardwarespecific function for wait on
 *			ready.
 * @setup_read_retry:	[FLASHSPECIFIC] flash (vendor) specific function for
 *			setting the read-retry mode. Mostly needed for MLC NAND.
 * @ecc:		[BOARDSPECIFIC] ECC control structure
 * @buffers:		buffer structure for read/write
 * @hwcontrol:		platform-specific hardware control structure
 * @erase:		[REPLACEABLE] erase function
 * @scan_bbt:		[REPLACEABLE] function to scan bad block table
 * @chip_delay:		[BOARDSPECIFIC] chip dependent delay for transferring
 *			data from array to read regs (tR).
 * @state:		[INTERN] the current state of the NAND device
 * @oob_poi:		"poison value buffer," used for laying out OOB data
 *			before writing
 * @page_shift:		[INTERN] number of address bits in a page (column
 *			address bits).
 * @phys_erase_shift:	[INTERN] number of address bits in a physical eraseblock
 * @bbt_erase_shift:	[INTERN] number of address bits in a bbt entry
 * @chip_shift:		[INTERN] number of address bits in one chip
 * @options:		[BOARDSPECIFIC] various chip options. They can partly
 *			be set to inform nand_scan about special functionality.
 *			See the defines for further explanation.
 * @bbt_options:	[INTERN] bad block specific options. All options used
 *			here must come from bbm.h. By default, these options
 *			will be copied to the appropriate nand_bbt_descr's.
 * @badblockpos:	[INTERN] position of the bad block marker in the oob
 *			area.
 * @badblockbits:	[INTERN] minimum number of set bits in a good block's
 *			bad block marker position; i.e., BBM == 11110111b is
 *			not bad when badblockbits == 7
 * @bits_per_cell:	[INTERN] number of bits per cell. i.e., 1 means SLC.
 * @ecc_strength_ds:	[INTERN] ECC correctability from the datasheet.
 *			Minimum amount of bit errors per @ecc_step_ds guaranteed
 *			to be correctable. If unknown, set to zero.
 * @ecc_step_ds:	[INTERN] ECC step required by the @ecc_strength_ds,
 *                      also from the datasheet. It is the recommended ECC step
 *			size, if known; if unknown, set to zero.
 * @onfi_timing_mode_default: [INTERN] default ONFI timing mode. This field is
 *			      either deduced from the datasheet if the NAND
 *			      chip is not ONFI compliant or set to 0 if it is
 *			      (an ONFI chip is always configured in mode 0
 *			      after a NAND reset)
 * @numchips:		[INTERN] number of physical chips
 * @chipsize:		[INTERN] the size of one chip for multichip arrays
 * @pagemask:		[INTERN] page number mask = number of (pages / chip) - 1
 * @pagebuf:		[INTERN] holds the pagenumber which is currently in
 *			data_buf.
 * @pagebuf_bitflips:	[INTERN] holds the bitflip count for the page which is
 *			currently in data_buf.
 * @subpagesize:	[INTERN] holds the subpagesize
 * @onfi_version:	[INTERN] holds the chip ONFI version (BCD encoded),
 *			non 0 if ONFI supported.
 * @jedec_version:	[INTERN] holds the chip JEDEC version (BCD encoded),
 *			non 0 if JEDEC supported.
 * @onfi_params:	[INTERN] holds the ONFI page parameter when ONFI is
 *			supported, 0 otherwise.
 * @jedec_params:	[INTERN] holds the JEDEC parameter page when JEDEC is
 *			supported, 0 otherwise.
 * @read_retries:	[INTERN] the number of read retry modes supported
 * @onfi_set_features:	[REPLACEABLE] set the features for ONFI nand
 * @onfi_get_features:	[REPLACEABLE] get the features for ONFI nand
 * @bbt:		[INTERN] bad block table pointer
 * @bbt_td:		[REPLACEABLE] bad block table descriptor for flash
 *			lookup.
 * @bbt_md:		[REPLACEABLE] bad block table mirror descriptor
 * @badblock_pattern:	[REPLACEABLE] bad block scan pattern used for initial
 *			bad block scan.
 * @controller:		[REPLACEABLE] a pointer to a hardware controller
 *			structure which is shared among multiple independent
 *			devices.
 * @priv:		[OPTIONAL] pointer to private chip data
 * @errstat:		[OPTIONAL] hardware specific function to perform
 *			additional error status checks (determine if errors are
 *			correctable).
 * @write_page:		[REPLACEABLE] High-level page write function
 */

struct nand_chip {
	void __iomem *IO_ADDR_R;
	void __iomem *IO_ADDR_W;

	uint8_t (*read_byte)(struct mtd_info *mtd);
	u16 (*read_word)(struct mtd_info *mtd);
	void (*write_byte)(struct mtd_info *mtd, uint8_t byte);
	void (*write_buf)(struct mtd_info *mtd, const uint8_t *buf, int len);
	void (*read_buf)(struct mtd_info *mtd, uint8_t *buf, int len);
	void (*select_chip)(struct mtd_info *mtd, int chip);
	int (*block_bad)(struct mtd_info *mtd, loff_t ofs, int getchip);
	int (*block_markbad)(struct mtd_info *mtd, loff_t ofs);
	void (*cmd_ctrl)(struct mtd_info *mtd, int dat, unsigned int ctrl);
	int (*init_size)(struct mtd_info *mtd, struct nand_chip *this,
			u8 *id_data);
	int (*dev_ready)(struct mtd_info *mtd);
	void (*cmdfunc)(struct mtd_info *mtd, unsigned command, int column,
			int page_addr);
	int(*waitfunc)(struct mtd_info *mtd, struct nand_chip *this);
	int (*erase)(struct mtd_info *mtd, int page);
	int (*scan_bbt)(struct mtd_info *mtd);
	int (*errstat)(struct mtd_info *mtd, struct nand_chip *this, int state,
			int status, int page);
	int (*write_page)(struct mtd_info *mtd, struct nand_chip *chip,
			uint32_t offset, int data_len, const uint8_t *buf,
			int oob_required, int page, int cached, int raw);
	int (*onfi_set_features)(struct mtd_info *mtd, struct nand_chip *chip,
			int feature_addr, uint8_t *subfeature_para);
	int (*onfi_get_features)(struct mtd_info *mtd, struct nand_chip *chip,
			int feature_addr, uint8_t *subfeature_para);
	int (*setup_read_retry)(struct mtd_info *mtd, int retry_mode);

	int chip_delay;
	unsigned int options;
	unsigned int bbt_options;

	int page_shift;
	int phys_erase_shift;
	int bbt_erase_shift;
	int chip_shift;
	int numchips;
	uint64_t chipsize;
	int pagemask;
	int pagebuf;
	unsigned int pagebuf_bitflips;
	int subpagesize;
	uint8_t bits_per_cell;
	uint16_t ecc_strength_ds;
	uint16_t ecc_step_ds;
	int onfi_timing_mode_default;
	int badblockpos;
	int badblockbits;

	int onfi_version;
	int jedec_version;
	union {
		struct nand_onfi_params	onfi_params;
		struct nand_jedec_params jedec_params;
	};

	int read_retries;

	flstate_t state;

	uint8_t *oob_poi;
	struct nand_hw_control *controller;

	struct nand_ecc_ctrl ecc;
	struct nand_buffers *buffers;
	struct nand_hw_control hwcontrol;

	uint8_t *bbt;
	struct nand_bbt_descr *bbt_td;
	struct nand_bbt_descr *bbt_md;

	struct nand_bbt_descr *badblock_pattern;

	void *priv;
};


struct mtd_info {
	u_char type;
	uint32_t flags;
	uint64_t size;	 // Total size of the MTD

	/* "Major" erase size for the device. Naïve users may take this
	 * to be the only erase size available, or may use the more detailed
	 * information below if they desire
	 */
	uint32_t erasesize;
	/* Minimal writable flash unit size. In case of NOR flash it is 1 (even
	 * though individual bits can be cleared), in case of NAND flash it is
	 * one NAND page (or half, or one-fourths of it), in case of ECC-ed NOR
	 * it is of ECC block size, etc. It is illegal to have writesize = 0.
	 * Any driver registering a struct mtd_info must ensure a writesize of
	 * 1 or larger.
	 */
	uint32_t writesize;

	/*
	 * Size of the write buffer used by the MTD. MTD devices having a write
	 * buffer can write multiple writesize chunks at a time. E.g. while
	 * writing 4 * writesize bytes to a device with 2 * writesize bytes
	 * buffer the MTD driver can (but doesn't have to) do 2 writesize
	 * operations, but not 4. Currently, all NANDs have writebufsize
	 * equivalent to writesize (NAND page size). Some NOR flashes do have
	 * writebufsize greater than writesize.
	 */
	uint32_t writebufsize;

	uint32_t oobsize;   // Amount of OOB data per block (e.g. 16)
	uint32_t oobavail;  // Available OOB bytes per block

	/*
	 * If erasesize is a power of 2 then the shift is stored in
	 * erasesize_shift otherwise erasesize_shift is zero. Ditto writesize.
	 */
	unsigned int erasesize_shift;
	unsigned int writesize_shift;
	/* Masks based on erasesize_shift and writesize_shift */
	unsigned int erasesize_mask;
	unsigned int writesize_mask;

	/*
	 * read ops return -EUCLEAN if max number of bitflips corrected on any
	 * one region comprising an ecc step equals or exceeds this value.
	 * Settable by driver, else defaults to ecc_strength.  User can override
	 * in sysfs.  N.B. The meaning of the -EUCLEAN return code has changed;
	 * see Documentation/ABI/testing/sysfs-class-mtd for more detail.
	 */
	unsigned int bitflip_threshold;

	// Kernel-only stuff starts here.
	const char *name;
	int index;

	/* ECC layout structure pointer - read only! */
	struct nand_ecclayout *ecclayout;

	/* the ecc step size. */
	unsigned int ecc_step_size;

	/* max number of correctible bit errors per ecc step */
	unsigned int ecc_strength;

	/* Data for variable erase regions. If numeraseregions is zero,
	 * it means that the whole device has erasesize as given above.
	 */
	int numeraseregions;
	struct mtd_erase_region_info *eraseregions;

	/*
	 * Do not call via these pointers, use corresponding mtd_*()
	 * wrappers instead.
	 * 有对应的mtd封装函数
	 */
	int (*_erase) (struct mtd_info *mtd, struct erase_info *instr);
	int (*_point) (struct mtd_info *mtd, loff_t from, size_t len,
		       size_t *retlen, void **virt, resource_size_t *phys);
	int (*_unpoint) (struct mtd_info *mtd, loff_t from, size_t len);
	unsigned long (*_get_unmapped_area) (struct mtd_info *mtd,
					     unsigned long len,
					     unsigned long offset,
					     unsigned long flags);
	int (*_read) (struct mtd_info *mtd, loff_t from, size_t len,
		      size_t *retlen, u_char *buf);
	int (*_write) (struct mtd_info *mtd, loff_t to, size_t len,
		       size_t *retlen, const u_char *buf);
	int (*_panic_write) (struct mtd_info *mtd, loff_t to, size_t len,
			     size_t *retlen, const u_char *buf);
	int (*_read_oob) (struct mtd_info *mtd, loff_t from,
			  struct mtd_oob_ops *ops);
	int (*_write_oob) (struct mtd_info *mtd, loff_t to,
			   struct mtd_oob_ops *ops);
	int (*_get_fact_prot_info) (struct mtd_info *mtd, size_t len,
				    size_t *retlen, struct otp_info *buf);
	int (*_read_fact_prot_reg) (struct mtd_info *mtd, loff_t from,
				    size_t len, size_t *retlen, u_char *buf);
	int (*_get_user_prot_info) (struct mtd_info *mtd, size_t len,
				    size_t *retlen, struct otp_info *buf);
	int (*_read_user_prot_reg) (struct mtd_info *mtd, loff_t from,
				    size_t len, size_t *retlen, u_char *buf);
	int (*_write_user_prot_reg) (struct mtd_info *mtd, loff_t to,
				     size_t len, size_t *retlen, u_char *buf);
	int (*_lock_user_prot_reg) (struct mtd_info *mtd, loff_t from,
				    size_t len);
	int (*_writev) (struct mtd_info *mtd, const struct kvec *vecs,
			unsigned long count, loff_t to, size_t *retlen);
	void (*_sync) (struct mtd_info *mtd);
	int (*_lock) (struct mtd_info *mtd, loff_t ofs, uint64_t len);
	int (*_unlock) (struct mtd_info *mtd, loff_t ofs, uint64_t len);
	int (*_is_locked) (struct mtd_info *mtd, loff_t ofs, uint64_t len);
	int (*_block_isreserved) (struct mtd_info *mtd, loff_t ofs);
	int (*_block_isbad) (struct mtd_info *mtd, loff_t ofs);
	int (*_block_markbad) (struct mtd_info *mtd, loff_t ofs);
	int (*_suspend) (struct mtd_info *mtd);
	void (*_resume) (struct mtd_info *mtd);
	void (*_reboot) (struct mtd_info *mtd);
	/*
	 * If the driver is something smart, like UBI, it may need to maintain
	 * its own reference counting. The below functions are only for driver.
	 */
	int (*_get_device) (struct mtd_info *mtd);
	void (*_put_device) (struct mtd_info *mtd);

	/* Backing device capabilities for this device
	 * - provides mmap capabilities
	 */
	struct backing_dev_info *backing_dev_info;

	struct notifier_block reboot_notifier;  /* default mode before reboot */

	/* ECC status information */
	struct mtd_ecc_stats ecc_stats;
	/* Subpage shift (NAND) */
	int subpage_sft;

	void *priv;

	struct module *owner;
	struct device dev;
	int usecount;
};


struct jz_nand {
	struct mtd_info mtd;
	struct nand_chip chip;
	void __iomem *base;
	struct resource *mem;

	unsigned char banks[JZ_NAND_NUM_BANKS];
	void __iomem *bank_base[JZ_NAND_NUM_BANKS];
	struct resource *bank_mem[JZ_NAND_NUM_BANKS];

	int selected_bank;

	struct gpio_desc *busy_gpio;
	bool is_reading;
};


/*
 * Constants for ECC_MODES
 */
typedef enum {
	NAND_ECC_NONE,
	NAND_ECC_SOFT,
	NAND_ECC_HW,
	NAND_ECC_HW_SYNDROME,
	NAND_ECC_HW_OOB_FIRST,
	NAND_ECC_SOFT_BCH,
} nand_ecc_modes_t;


#define MTD_MAX_OOBFREE_ENTRIES_LARGE	32
#define MTD_MAX_ECCPOS_ENTRIES_LARGE	640


/*
 * Internal ECC layout control structure. For historical reasons, there is a
 * similar, smaller struct nand_ecclayout_user (in mtd-abi.h) that is retained
 * for export to user-space via the ECCGETLAYOUT ioctl.
 * nand_ecclayout should be expandable in the future simply by the above macros.
 */
struct nand_ecclayout {
	__u32 eccbytes;
	__u32 eccpos[MTD_MAX_ECCPOS_ENTRIES_LARGE];
	__u32 oobavail;
	struct nand_oobfree oobfree[MTD_MAX_OOBFREE_ENTRIES_LARGE];
};


static int jz_nand_probe(struct platform_device *pdev)
{
	int ret;
	struct jz_nand *nand;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	struct jz_nand_platform_data *pdata = dev_get_platdata(&pdev->dev);
	size_t chipnr, bank_idx;
	uint8_t nand_maf_id = 0, nand_dev_id = 0;

	nand = kzalloc(sizeof(*nand), GFP_KERNEL);

	ret = jz_nand_ioremap_resource(pdev, "mmio", &nand->mem, &nand->base);

	nand->busy_gpio = devm_gpiod_get_optional(&pdev->dev, "busy", GPIOD_IN);

	mtd		= &nand->mtd;
	chip		= &nand->chip;
	/* nand_chip作为mtd_info的private成员，
	 * mtd层是不会调用nand_chip中的东西的
	 * nand设备只是借用了mtd框架而已，mtd还用于其他的驱动，如nor flash
	 * spi-nand, spi-nor等，这个时候mtd_info就是其他对应的数据结构了
	 */
	mtd->priv	= chip;
	mtd->owner	= THIS_MODULE;
	mtd->name	= "jz4740-nand";

	/* ECC用于进行数据校验的 */
	chip->ecc.hwctl		= jz_nand_hwctl;
	chip->ecc.calculate	= jz_nand_calculate_ecc_rs;
	chip->ecc.correct	= jz_nand_correct_ecc_rs;
	chip->ecc.mode		= NAND_ECC_HW_OOB_FIRST;
	chip->ecc.size		= 512;
	chip->ecc.bytes		= 9;
	chip->ecc.strength	= 4;

	/* pdata是platform_device中的赋值的，如果使用device            tree则该值为null    */
	if (pdata)
		chip->ecc.layout = pdata->ecc_layout;

	chip->chip_delay = 50;
	chip->cmd_ctrl = jz_nand_cmd_ctrl;
	chip->select_chip = jz_nand_select_chip;

	if (nand->busy_gpio)
		chip->dev_ready = jz_nand_dev_ready;

	platform_set_drvdata(pdev, nand);

	/* 其实这里可以调用nand_scan，nand_scan
	 * 会调用nand_scan_ident和nand_scan_tail
	 * 很多驱动会分别调用，进行一些定制，下面
	 * 分析下nand_scan
	 */
	ret = nand_scan_tail(mtd);
	if (ret) {
		dev_err(&pdev->dev,  "Failed to scan NAND\n");
		goto err_unclaim_banks;
	}

	/* platform_device的pdata中会写分区信息，如果在devicetree中
	 * 指定分区可以在第三个参数中设置of_node,第二个参数是解析
	 * 分区的类型
	 */
	ret = mtd_device_parse_register(mtd, NULL, NULL,
					pdata ? pdata->partitions : NULL,
					pdata ? pdata->num_partitions : 0);

	if (ret) {
		dev_err(&pdev->dev, "Failed to add mtd device\n");
		goto err_nand_release;
	}

	dev_info(&pdev->dev, "Successfully registered JZ4740 NAND driver\n");

	return 0;
}

/**
 * nand_scan - [NAND Interface] Scan for the NAND device
 * @mtd: MTD device structure
 * @maxchips: number of chips to scan for
 *
 * This fills out all the uninitialized function pointers with the defaults.
 * The flash ID is read and the mtd/chip structures are filled with the
 * appropriate values. The mtd->owner field must be set to the module of the
 * caller.
 */
 /* maxchips一般为1 */
int nand_scan(struct mtd_info *mtd, int maxchips)
{
	int ret;

	/* Many callers got this wrong, so check for it for a while... */
	if (!mtd->owner && caller_is_module()) {
		pr_crit("%s called with NULL mtd->owner!\n", __func__);
		BUG();
	}
	/* 在定制的时候最后一个table参数可以自己指定 */
	ret = nand_scan_ident(mtd, maxchips, NULL);
	if (!ret)
		ret = nand_scan_tail(mtd);
	return ret;
}

/**
 * nand_scan_ident - [NAND Interface] Scan for the NAND device
 * @mtd: MTD device structure
 * @maxchips: number of chips to scan for
 * @table: alternative NAND ID table
 *
 * This is the first phase of the normal nand_scan() function. It reads the
 * flash ID and sets up MTD fields accordingly.
 *
 * The mtd->owner field must be set to the module of the caller.
 */
int nand_scan_ident(struct mtd_info *mtd, int maxchips,
		    struct nand_flash_dev *table)
{
	int i, nand_maf_id, nand_dev_id;
	struct nand_chip *chip = mtd->priv;
	struct nand_flash_dev *type;

	/* Set the default functions */
	nand_set_defaults(chip, chip->options & NAND_BUSWIDTH_16);

	/* Read the flash type */
	type = nand_get_flash_type(mtd, chip, &nand_maf_id, &nand_dev_id, table);


	chip->select_chip(mtd, -1);
	

	/* Check for a chip array */
	for (i = 1; i < maxchips; i++) {
		chip->select_chip(mtd, i);
		/* See comment in nand_get_flash_type for reset */
		chip->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);
		/* Send the command for reading device ID */
		chip->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);
		/* Read manufacturer and device IDs */
		if (nand_maf_id != chip->read_byte(mtd) ||
		    nand_dev_id != chip->read_byte(mtd)) {
			chip->select_chip(mtd, -1);
			break;
		}
		chip->select_chip(mtd, -1);
	}
	if (i > 1)
		pr_info("%d chips detected\n", i);

	/* Store the number of chips and calc total size for mtd */
	chip->numchips = i;
	mtd->size = i * chip->chipsize;

	return 0;
}
EXPORT_SYMBOL(nand_scan_ident);

/* Set default functions,赋值默认的函数 */
static void nand_set_defaults(struct nand_chip *chip, int busw)
{

	/**
	 * 如果没有一些特殊的定制，可以使用nand               标准的命令,
	 * 从而会  调用select_chip和cmd_ctrl函数来发送标准命令
	 */
	/* check for proper chip_delay setup, set 20us if not */
	if (!chip->chip_delay)
		chip->chip_delay = 20;

	/* check, if a user supplied command function given */
	if (chip->cmdfunc == NULL)
		chip->cmdfunc = nand_command;

	/* check, if a user supplied wait function given */
	if (chip->waitfunc == NULL)
		chip->waitfunc = nand_wait;

	if (!chip->select_chip)
		chip->select_chip = nand_select_chip;

	/* set for ONFI nand */
	if (!chip->onfi_set_features)
		chip->onfi_set_features = nand_onfi_set_features;
	if (!chip->onfi_get_features)
		chip->onfi_get_features = nand_onfi_get_features;

	/* If called twice, pointers that depend on busw may need to be reset */
	if (!chip->read_byte || chip->read_byte == nand_read_byte)
		chip->read_byte = busw ? nand_read_byte16 : nand_read_byte;
	if (!chip->read_word)
		chip->read_word = nand_read_word;
	if (!chip->block_bad)
		chip->block_bad = nand_block_bad;
	if (!chip->block_markbad)
		chip->block_markbad = nand_default_block_markbad;
	if (!chip->write_buf || chip->write_buf == nand_write_buf)
		chip->write_buf = busw ? nand_write_buf16 : nand_write_buf;
	if (!chip->write_byte || chip->write_byte == nand_write_byte)
		chip->write_byte = busw ? nand_write_byte16 : nand_write_byte;
	if (!chip->read_buf || chip->read_buf == nand_read_buf)
		chip->read_buf = busw ? nand_read_buf16 : nand_read_buf;
	if (!chip->scan_bbt)
		chip->scan_bbt = nand_default_bbt;

	if (!chip->controller) {
		chip->controller = &chip->hwcontrol;
		spin_lock_init(&chip->controller->lock);
		init_waitqueue_head(&chip->controller->wq);
	}

}

/**
 * struct nand_flash_dev - NAND Flash Device ID Structure
 * @name: a human-readable name of the NAND chip
 * @dev_id: the device ID (the second byte of the full chip ID array)
 * @mfr_id: manufecturer ID part of the full chip ID array (refers the same
 *          memory address as @id[0])
 * @dev_id: device ID part of the full chip ID array (refers the same memory
 *          address as @id[1])
 * @id: full device ID array
 * @pagesize: size of the NAND page in bytes; if 0, then the real page size (as
 *            well as the eraseblock size) is determined from the extended NAND
 *            chip ID array)
 * @chipsize: total chip size in MiB
 * @erasesize: eraseblock size in bytes (determined from the extended ID if 0)
 * @options: stores various chip bit options
 * @id_len: The valid length of the @id.
 * @oobsize: OOB size
 * @ecc: ECC correctability and step information from the datasheet.
 * @ecc.strength_ds: The ECC correctability from the datasheet, same as the
 *                   @ecc_strength_ds in nand_chip{}.
 * @ecc.step_ds: The ECC step required by the @ecc.strength_ds, same as the
 *               @ecc_step_ds in nand_chip{}, also from the datasheet.
 *               For example, the "4bit ECC for each 512Byte" can be set with
 *               NAND_ECC_INFO(4, 512).
 * @onfi_timing_mode_default: the default ONFI timing mode entered after a NAND
 *			      reset. Should be deduced from timings described
 *			      in the datasheet.
 *
 */
struct nand_flash_dev {
	char *name;
	union {
		struct {
			uint8_t mfr_id;
			uint8_t dev_id;
		};
		uint8_t id[NAND_MAX_ID_LEN];
	};
	unsigned int pagesize;
	unsigned int chipsize;
	unsigned int erasesize;
	unsigned int options;
	uint16_t id_len;
	uint16_t oobsize;
	struct {
		uint16_t strength_ds;
		uint16_t step_ds;
	} ecc;
	int onfi_timing_mode_default;
};


struct nand_flash_dev nand_flash_ids[] = {
	/*
	 * Some incompatible NAND chips share device ID's and so must be
	 * listed by full ID. We list them first so that we can easily identify
	 * the most specific match.
	 */
	{"TC58NVG2S0F 4G 3.3V 8-bit",
		{ .id = {0x98, 0xdc, 0x90, 0x26, 0x76, 0x15, 0x01, 0x08} },
		  SZ_4K, SZ_512, SZ_256K, 0, 8, 224, NAND_ECC_INFO(4, SZ_512) },
	{"TC58NVG3S0F 8G 3.3V 8-bit",
		{ .id = {0x98, 0xd3, 0x90, 0x26, 0x76, 0x15, 0x02, 0x08} },
		  SZ_4K, SZ_1K, SZ_256K, 0, 8, 232, NAND_ECC_INFO(4, SZ_512) },
	{"TC58NVG5D2 32G 3.3V 8-bit",
		{ .id = {0x98, 0xd7, 0x94, 0x32, 0x76, 0x56, 0x09, 0x00} },
		  SZ_8K, SZ_4K, SZ_1M, 0, 8, 640, NAND_ECC_INFO(40, SZ_1K) },
	{"TC58NVG6D2 64G 3.3V 8-bit",
		{ .id = {0x98, 0xde, 0x94, 0x82, 0x76, 0x56, 0x04, 0x20} },
		  SZ_8K, SZ_8K, SZ_2M, 0, 8, 640, NAND_ECC_INFO(40, SZ_1K) },
	{"SDTNRGAMA 64G 3.3V 8-bit",
		{ .id = {0x45, 0xde, 0x94, 0x93, 0x76, 0x50} },
		  SZ_16K, SZ_8K, SZ_4M, 0, 6, 1280, NAND_ECC_INFO(40, SZ_1K) },
	{"H27UCG8T2ATR-BC 64G 3.3V 8-bit",
		{ .id = {0xad, 0xde, 0x94, 0xda, 0x74, 0xc4} },
		  SZ_8K, SZ_8K, SZ_2M, 0, 6, 640, NAND_ECC_INFO(40, SZ_1K),
		  4 },

};

/* Manufacturer IDs */
struct nand_manufacturers nand_manuf_ids[] = {
	{NAND_MFR_TOSHIBA, "Toshiba"},
	{NAND_MFR_SAMSUNG, "Samsung"},
	{NAND_MFR_FUJITSU, "Fujitsu"},
	{NAND_MFR_NATIONAL, "National"},
	{NAND_MFR_RENESAS, "Renesas"},
	{NAND_MFR_STMICRO, "ST Micro"},
	{NAND_MFR_HYNIX, "Hynix"},
	{NAND_MFR_MICRON, "Micron"},
	{NAND_MFR_AMD, "AMD/Spansion"},
	{NAND_MFR_MACRONIX, "Macronix"},
	{NAND_MFR_EON, "Eon"},
	{NAND_MFR_SANDISK, "SanDisk"},
	{NAND_MFR_INTEL, "Intel"},
	{NAND_MFR_ATO, "ATO"},
	{0x0, "Unknown"}
};

/*
 * Get the flash and manufacturer id and lookup if the type is supported.
 */
static struct nand_flash_dev *nand_get_flash_type(struct mtd_info *mtd,
						  struct nand_chip *chip,
						  int *maf_id, int *dev_id,
						  struct nand_flash_dev *type)
{
	int busw;
	int i, maf_idx;
	u8 id_data[8];

	/* Select the device */
	chip->select_chip(mtd, 0);

	/*
	 * Reset the chip, required by some chips (e.g. Micron MT29FxGxxxxx)
	 * after power-up.
	 */
	chip->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);

	/* Send the command for reading device ID */
	chip->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);

	/* Read manufacturer and device IDs */
	*maf_id = chip->read_byte(mtd);
	*dev_id = chip->read_byte(mtd);

	/*
	 * Try again to make sure, as some systems the bus-hold or other
	 * interface concerns can cause random data which looks like a
	 * possibly credible NAND flash to appear. If the two results do
	 * not match, ignore the device completely.
	 */

	chip->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);

	/* Read entire ID string */
	for (i = 0; i < 8; i++)
		id_data[i] = chip->read_byte(mtd);

	if (id_data[0] != *maf_id || id_data[1] != *dev_id) {
		pr_info("second ID read did not match %02x,%02x against %02x,%02x\n",
			*maf_id, *dev_id, id_data[0], id_data[1]);
		return ERR_PTR(-ENODEV);
	}

	if (!type)
		type = nand_flash_ids;/* 中保存了对应nand       id的信息*/

	for (; type->name != NULL; type++) {
		if (is_full_id_nand(type)) {
			if (find_full_id_nand(mtd, chip, type, id_data, &busw))
				goto ident_done;
		} else if (*dev_id == type->dev_id) {
				break;
		}
	}

	chip->onfi_version = 0;
	if (!type->name || !type->pagesize) {
		/* Check if the chip is ONFI compliant */
		/* 通过onfi标准解析nand       flash参数 */
		if (nand_flash_detect_onfi(mtd, chip, &busw))
			goto ident_done;

		/* Check if the chip is JEDEC compliant */
		/* 通过jedec标准解析nand       flash参数 */
		if (nand_flash_detect_jedec(mtd, chip, &busw))
			goto ident_done;
	}

	if (!type->name)
		return ERR_PTR(-ENODEV);

	if (!mtd->name)
		mtd->name = type->name;

	chip->chipsize = (uint64_t)type->chipsize << 20;

	if (!type->pagesize && chip->init_size) {
		/* Set the pagesize, oobsize, erasesize by the driver */
		busw = chip->init_size(mtd, chip, id_data);
	} else if (!type->pagesize) {
		/* Decode parameters from extended ID */
		nand_decode_ext_id(mtd, chip, id_data, &busw);
	} else {
		nand_decode_id(mtd, chip, type, id_data, &busw);
	}
	/* Get chip options */
	chip->options |= type->options;

	/*
	 * Check if chip is not a Samsung device. Do not clear the
	 * options for chips which do not have an extended id.
	 */
	if (*maf_id != NAND_MFR_SAMSUNG && !type->pagesize)
		chip->options &= ~NAND_SAMSUNG_LP_OPTIONS;
ident_done:

	/* Try to identify manufacturer */
	for (maf_idx = 0; nand_manuf_ids[maf_idx].id != 0x0; maf_idx++) {
		if (nand_manuf_ids[maf_idx].id == *maf_id)
			break;
	}

	if (chip->options & NAND_BUSWIDTH_AUTO) {
		WARN_ON(chip->options & NAND_BUSWIDTH_16);
		chip->options |= busw;
		nand_set_defaults(chip, busw);
	} else if (busw != (chip->options & NAND_BUSWIDTH_16)) {
		/*
		 * Check, if buswidth is correct. Hardware drivers should set
		 * chip correct!
		 */
		pr_info("device found, Manufacturer ID: 0x%02x, Chip ID: 0x%02x\n",
			*maf_id, *dev_id);
		pr_info("%s %s\n", nand_manuf_ids[maf_idx].name, mtd->name);
		pr_warn("bus width %d instead %d bit\n",
			   (chip->options & NAND_BUSWIDTH_16) ? 16 : 8,
			   busw ? 16 : 8);
		return ERR_PTR(-EINVAL);
	}

	nand_decode_bbm_options(mtd, chip, id_data);

	/* Calculate the address shift from the page size */
	chip->page_shift = ffs(mtd->writesize) - 1;
	/* Convert chipsize to number of pages per chip -1 */
	chip->pagemask = (chip->chipsize >> chip->page_shift) - 1;

	chip->bbt_erase_shift = chip->phys_erase_shift =
		ffs(mtd->erasesize) - 1;
	if (chip->chipsize & 0xffffffff)
		chip->chip_shift = ffs((unsigned)chip->chipsize) - 1;
	else {
		chip->chip_shift = ffs((unsigned)(chip->chipsize >> 32));
		chip->chip_shift += 32 - 1;
	}

	chip->badblockbits = 8;
	chip->erase = single_erase;

	/* Do not replace user supplied command function! */
	if (mtd->writesize > 512 && chip->cmdfunc == nand_command)
		chip->cmdfunc = nand_command_lp;

	pr_info("device found, Manufacturer ID: 0x%02x, Chip ID: 0x%02x\n",
		*maf_id, *dev_id);

	if (chip->onfi_version)
		pr_info("%s %s\n", nand_manuf_ids[maf_idx].name,
				chip->onfi_params.model);
	else if (chip->jedec_version)
		pr_info("%s %s\n", nand_manuf_ids[maf_idx].name,
				chip->jedec_params.model);
	else
		pr_info("%s %s\n", nand_manuf_ids[maf_idx].name,
				type->name);

	pr_info("%d MiB, %s, erase size: %d KiB, page size: %d, OOB size: %d\n",
		(int)(chip->chipsize >> 20), nand_is_slc(chip) ? "SLC" : "MLC",
		mtd->erasesize >> 10, mtd->writesize, mtd->oobsize);
	return type;
}

static inline bool is_full_id_nand(struct nand_flash_dev *type)
{
	return type->id_len;
}

static bool find_full_id_nand(struct mtd_info *mtd, struct nand_chip *chip,
		   struct nand_flash_dev *type, u8 *id_data, int *busw)
{
	if (!strncmp(type->id, id_data, type->id_len)) {
		mtd->writesize = type->pagesize;
		mtd->erasesize = type->erasesize;
		mtd->oobsize = type->oobsize;

		chip->bits_per_cell = nand_get_bits_per_cell(id_data[2]);
		chip->chipsize = (uint64_t)type->chipsize << 20;
		chip->options |= type->options;
		chip->ecc_strength_ds = NAND_ECC_STRENGTH(type);
		chip->ecc_step_ds = NAND_ECC_STEP(type);
		chip->onfi_timing_mode_default =
					type->onfi_timing_mode_default;

		*busw = type->options & NAND_BUSWIDTH_16;

		if (!mtd->name)
			mtd->name = type->name;

		return true;
	}
	return false;
}

/**
 * nand_scan_tail - [NAND Interface] Scan for the NAND device
 * @mtd: MTD device structure
 *
 * This is the second phase of the normal nand_scan() function. It fills out
 * all the uninitialized function pointers with the defaults and scans for a
 * bad block table if appropriate.
 */
int nand_scan_tail(struct mtd_info *mtd)
{
	int i;
	struct nand_chip *chip = mtd->priv;
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	struct nand_buffers *nbuf;

	/* New bad blocks should be marked in OOB, flash-based BBT, or both */
	BUG_ON((chip->bbt_options & NAND_BBT_NO_OOB_BBM) &&
			!(chip->bbt_options & NAND_BBT_USE_FLASH));

	if (!(chip->options & NAND_OWN_BUFFERS)) {
		nbuf = kzalloc(sizeof(*nbuf) + mtd->writesize
				+ mtd->oobsize * 3, GFP_KERNEL);
		if (!nbuf)
			return -ENOMEM;
		nbuf->ecccalc = (uint8_t *)(nbuf + 1);
		nbuf->ecccode = nbuf->ecccalc + mtd->oobsize;
		nbuf->databuf = nbuf->ecccode + mtd->oobsize;

		chip->buffers = nbuf;
	} else {
		if (!chip->buffers)
			return -ENOMEM;
	}

	/* Set the internal oob buffer location, just after the page data */
	chip->oob_poi = chip->buffers->databuf + mtd->writesize;

	/*
	 * If no default placement scheme is given, select an appropriate one.
	 */
	if (!ecc->layout && (ecc->mode != NAND_ECC_SOFT_BCH)) {
		switch (mtd->oobsize) {
		case 8:
			ecc->layout = &nand_oob_8;
			break;
		case 16:
			ecc->layout = &nand_oob_16;
			break;
		case 64:
			ecc->layout = &nand_oob_64;
			break;
		case 128:
			ecc->layout = &nand_oob_128;
			break;
		default:
			pr_warn("No oob scheme defined for oobsize %d\n",
				   mtd->oobsize);
			BUG();
		}
	}

	if (!chip->write_page)
		chip->write_page = nand_write_page;

	/*
	 * Check ECC mode, default to software if 3byte/512byte hardware ECC is
	 * selected and we have 256 byte pagesize fallback to software ECC
	 */

	switch (ecc->mode) {
	case NAND_ECC_HW_OOB_FIRST:
		/* Similar to NAND_ECC_HW, but a separate read_page handle */
		if (!ecc->calculate || !ecc->correct || !ecc->hwctl) {
			pr_warn("No ECC functions supplied; hardware ECC not possible\n");
			BUG();
		}
		if (!ecc->read_page)
			ecc->read_page = nand_read_page_hwecc_oob_first;

	case NAND_ECC_HW:
		/* Use standard hwecc read page function? */
		if (!ecc->read_page)
			ecc->read_page = nand_read_page_hwecc;
		if (!ecc->write_page)
			ecc->write_page = nand_write_page_hwecc;
		if (!ecc->read_page_raw)
			ecc->read_page_raw = nand_read_page_raw;
		if (!ecc->write_page_raw)
			ecc->write_page_raw = nand_write_page_raw;
		if (!ecc->read_oob)
			ecc->read_oob = nand_read_oob_std;
		if (!ecc->write_oob)
			ecc->write_oob = nand_write_oob_std;
		if (!ecc->read_subpage)
			ecc->read_subpage = nand_read_subpage;
		if (!ecc->write_subpage)
			ecc->write_subpage = nand_write_subpage_hwecc;

	case NAND_ECC_HW_SYNDROME:
		if ((!ecc->calculate || !ecc->correct || !ecc->hwctl) &&
		    (!ecc->read_page ||
		     ecc->read_page == nand_read_page_hwecc ||
		     !ecc->write_page ||
		     ecc->write_page == nand_write_page_hwecc)) {
			pr_warn("No ECC functions supplied; hardware ECC not possible\n");
			BUG();
		}
		/* Use standard syndrome read/write page function? */
		if (!ecc->read_page)
			ecc->read_page = nand_read_page_syndrome;
		if (!ecc->write_page)
			ecc->write_page = nand_write_page_syndrome;
		if (!ecc->read_page_raw)
			ecc->read_page_raw = nand_read_page_raw_syndrome;
		if (!ecc->write_page_raw)
			ecc->write_page_raw = nand_write_page_raw_syndrome;
		if (!ecc->read_oob)
			ecc->read_oob = nand_read_oob_syndrome;
		if (!ecc->write_oob)
			ecc->write_oob = nand_write_oob_syndrome;

		if (mtd->writesize >= ecc->size) {
			if (!ecc->strength) {
				pr_warn("Driver must set ecc.strength when using hardware ECC\n");
				BUG();
			}
			break;
		}
		pr_warn("%d byte HW ECC not possible on %d byte page size, fallback to SW ECC\n",
			ecc->size, mtd->writesize);
		ecc->mode = NAND_ECC_SOFT;

	case NAND_ECC_SOFT:
		ecc->calculate = nand_calculate_ecc;
		ecc->correct = nand_correct_data;
		ecc->read_page = nand_read_page_swecc;
		ecc->read_subpage = nand_read_subpage;
		ecc->write_page = nand_write_page_swecc;
		ecc->read_page_raw = nand_read_page_raw;
		ecc->write_page_raw = nand_write_page_raw;
		ecc->read_oob = nand_read_oob_std;
		ecc->write_oob = nand_write_oob_std;
		if (!ecc->size)
			ecc->size = 256;
		ecc->bytes = 3;
		ecc->strength = 1;
		break;

	case NAND_ECC_SOFT_BCH:
		if (!mtd_nand_has_bch()) {
			pr_warn("CONFIG_MTD_NAND_ECC_BCH not enabled\n");
			BUG();
		}
		ecc->calculate = nand_bch_calculate_ecc;
		ecc->correct = nand_bch_correct_data;
		ecc->read_page = nand_read_page_swecc;
		ecc->read_subpage = nand_read_subpage;
		ecc->write_page = nand_write_page_swecc;
		ecc->read_page_raw = nand_read_page_raw;
		ecc->write_page_raw = nand_write_page_raw;
		ecc->read_oob = nand_read_oob_std;
		ecc->write_oob = nand_write_oob_std;
		/*
		 * Board driver should supply ecc.size and ecc.strength values
		 * to select how many bits are correctable. Otherwise, default
		 * to 4 bits for large page devices.
		 */
		if (!ecc->size && (mtd->oobsize >= 64)) {
			ecc->size = 512;
			ecc->strength = 4;
		}

		/* See nand_bch_init() for details. */
		ecc->bytes = DIV_ROUND_UP(
				ecc->strength * fls(8 * ecc->size), 8);
		ecc->priv = nand_bch_init(mtd, ecc->size, ecc->bytes,
					       &ecc->layout);
		if (!ecc->priv) {
			pr_warn("BCH ECC initialization failed!\n");
			BUG();
		}
		break;

	case NAND_ECC_NONE:
		pr_warn("NAND_ECC_NONE selected by board driver. This is not recommended!\n");
		ecc->read_page = nand_read_page_raw;
		ecc->write_page = nand_write_page_raw;
		ecc->read_oob = nand_read_oob_std;
		ecc->read_page_raw = nand_read_page_raw;
		ecc->write_page_raw = nand_write_page_raw;
		ecc->write_oob = nand_write_oob_std;
		ecc->size = mtd->writesize;
		ecc->bytes = 0;
		ecc->strength = 0;
		break;

	default:
		pr_warn("Invalid NAND_ECC_MODE %d\n", ecc->mode);
		BUG();
	}

	/* For many systems, the standard OOB write also works for raw */
	if (!ecc->read_oob_raw)
		ecc->read_oob_raw = ecc->read_oob;
	if (!ecc->write_oob_raw)
		ecc->write_oob_raw = ecc->write_oob;

	/*
	 * The number of bytes available for a client to place data into
	 * the out of band area.
	 */
	ecc->layout->oobavail = 0;
	for (i = 0; ecc->layout->oobfree[i].length
			&& i < ARRAY_SIZE(ecc->layout->oobfree); i++)
		ecc->layout->oobavail += ecc->layout->oobfree[i].length;
	mtd->oobavail = ecc->layout->oobavail;

	/* ECC sanity check: warn if it's too weak */
	if (!nand_ecc_strength_good(mtd))
		pr_warn("WARNING: %s: the ECC used on your system is too weak compared to the one required by the NAND chip\n",
			mtd->name);

	/*
	 * Set the number of read / write steps for one page depending on ECC
	 * mode.
	 */
	ecc->steps = mtd->writesize / ecc->size;
	if (ecc->steps * ecc->size != mtd->writesize) {
		pr_warn("Invalid ECC parameters\n");
		BUG();
	}
	ecc->total = ecc->steps * ecc->bytes;

	/* Allow subpage writes up to ecc.steps. Not possible for MLC flash */
	if (!(chip->options & NAND_NO_SUBPAGE_WRITE) && nand_is_slc(chip)) {
		switch (ecc->steps) {
		case 2:
			mtd->subpage_sft = 1;
			break;
		case 4:
		case 8:
		case 16:
			mtd->subpage_sft = 2;
			break;
		}
	}
	chip->subpagesize = mtd->writesize >> mtd->subpage_sft;

	/* Initialize state */
	chip->state = FL_READY;

	/* Invalidate the pagebuffer reference */
	chip->pagebuf = -1;

	/* Large page NAND with SOFT_ECC should support subpage reads */
	switch (ecc->mode) {
	case NAND_ECC_SOFT:
	case NAND_ECC_SOFT_BCH:
		if (chip->page_shift > 9)
			chip->options |= NAND_SUBPAGE_READ;
		break;

	default:
		break;
	}

	/** 给mtd函数赋值 **/
	/* Fill in remaining MTD driver data */
	mtd->type = nand_is_slc(chip) ? MTD_NANDFLASH : MTD_MLCNANDFLASH;
	mtd->flags = (chip->options & NAND_ROM) ? MTD_CAP_ROM :
						MTD_CAP_NANDFLASH;
	mtd->_erase = nand_erase;
	mtd->_point = NULL;
	mtd->_unpoint = NULL;
	mtd->_read = nand_read;
	mtd->_write = nand_write;
	mtd->_panic_write = panic_nand_write;
	mtd->_read_oob = nand_read_oob;
	mtd->_write_oob = nand_write_oob;
	mtd->_sync = nand_sync;
	mtd->_lock = NULL;
	mtd->_unlock = NULL;
	mtd->_suspend = nand_suspend;
	mtd->_resume = nand_resume;
	mtd->_reboot = nand_shutdown;
	mtd->_block_isreserved = nand_block_isreserved;
	mtd->_block_isbad = nand_block_isbad;
	mtd->_block_markbad = nand_block_markbad;
	mtd->writebufsize = mtd->writesize;

	/* propagate ecc info to mtd_info */
	mtd->ecclayout = ecc->layout;
	mtd->ecc_strength = ecc->strength;
	mtd->ecc_step_size = ecc->size;
	/*
	 * Initialize bitflip_threshold to its default prior scan_bbt() call.
	 * scan_bbt() might invoke mtd_read(), thus bitflip_threshold must be
	 * properly set.
	 */
	if (!mtd->bitflip_threshold)
		mtd->bitflip_threshold = DIV_ROUND_UP(mtd->ecc_strength * 3, 4);

	/* Check, if we should skip the bad block table scan */
	if (chip->options & NAND_SKIP_BBTSCAN)
		return 0;

	/* Build bad block table */
	return chip->scan_bbt(mtd);
}
EXPORT_SYMBOL(nand_scan_tail);

/**
 * struct mtd_part_parser_data - used to pass data to MTD partition parsers.
 * @origin: for RedBoot, start address of MTD device
 * @of_node: for OF parsers, device node containing partitioning information
 */
struct mtd_part_parser_data {
	unsigned long origin;
	struct device_node *of_node;
};

/*
 * Partition definition structure:
 *
 * An array of struct partition is passed along with a MTD object to
 * mtd_device_register() to create them.
 *
 * For each partition, these fields are available:
 * name: string that will be used to label the partition's MTD device.
 * size: the partition size; if defined as MTDPART_SIZ_FULL, the partition
 * 	will extend to the end of the master MTD device.
 * offset: absolute starting position within the master MTD device; if
 * 	defined as MTDPART_OFS_APPEND, the partition will start where the
 *	previous one ended; if MTDPART_OFS_NXTBLK, at the next erase block;
 *	if MTDPART_OFS_RETAIN, consume as much as possible, leaving size
 *	after the end of partition.
 * mask_flags: contains flags that have to be masked (removed) from the
 * 	master MTD flag set for the corresponding MTD partition.
 * 	For example, to force a read-only partition, simply adding
 * 	MTD_WRITEABLE to the mask_flags will do the trick.
 *
 * Note: writeable partitions require their size and offset be
 * erasesize aligned (e.g. use MTDPART_OFS_NEXTBLK).
 */

struct mtd_partition {
	const char *name;		/* identifier string */
	uint64_t size;			/* partition size */
	uint64_t offset;		/* offset within the master MTD space */
	uint32_t mask_flags;		/* master MTD flags to mask out for this partition */
	struct nand_ecclayout *ecclayout;	/* out of band layout for this partition (NAND only) */
};



/**
 * mtd_device_parse_register - parse partitions and register an MTD device.
 *
 * @mtd: the MTD device to register
 * @types: the list of MTD partition probes to try, see
 *         'parse_mtd_partitions()' for more information
 * @parser_data: MTD partition parser-specific data
 * @parts: fallback partition information to register, if parsing fails;
 *         only valid if %nr_parts > %0
 * @nr_parts: the number of partitions in parts, if zero then the full
 *            MTD device is registered if no partition info is found
 *
 * This function aggregates MTD partitions parsing (done by
 * 'parse_mtd_partitions()') and MTD device and partitions registering. It
 * basically follows the most common pattern found in many MTD drivers:
 *
 * * It first tries to probe partitions on MTD device @mtd using parsers
 *   specified in @types (if @types is %NULL, then the default list of parsers
 *   is used, see 'parse_mtd_partitions()' for more information). If none are
 *   found this functions tries to fallback to information specified in
 *   @parts/@nr_parts.
 * * If any partitioning info was found, this function registers the found
 *   partitions.
 * * If no partitions were found this function just registers the MTD device
 *   @mtd and exits.
 *
 * Returns zero in case of success and a negative error code in case of failure.
 */
int mtd_device_parse_register(struct mtd_info *mtd, const char * const *types,
			      struct mtd_part_parser_data *parser_data,
			      const struct mtd_partition *parts,
			      int nr_parts)
{
	int err;
	struct mtd_partition *real_parts;

	err = parse_mtd_partitions(mtd, types, &real_parts, parser_data);
	if (err <= 0 && nr_parts && parts) {
		real_parts = kmemdup(parts, sizeof(*parts) * nr_parts,
				     GFP_KERNEL);
		if (!real_parts)
			err = -ENOMEM;
		else
			err = nr_parts;
	}

	if (err > 0) {
		err = add_mtd_partitions(mtd, real_parts, err);
		kfree(real_parts);
	} else if (err == 0) {
		err = add_mtd_device(mtd);
		if (err == 1)
			err = -ENODEV;
	}

	/*
	 * FIXME: some drivers unfortunately call this function more than once.
	 * So we have to check if we've already assigned the reboot notifier.
	 *
	 * Generally, we can make multiple calls work for most cases, but it
	 * does cause problems with parse_mtd_partitions() above (e.g.,
	 * cmdlineparts will register partitions more than once).
	 */
	if (mtd->_reboot && !mtd->reboot_notifier.notifier_call) {
		mtd->reboot_notifier.notifier_call = mtd_reboot_notifier;
		register_reboot_notifier(&mtd->reboot_notifier);
	}

	return err;
}
EXPORT_SYMBOL_GPL(mtd_device_parse_register);

static const char * const default_mtd_part_types[] = {
	"cmdlinepart",
	"ofpart",
	NULL
};


/**
 * parse_mtd_partitions - parse MTD partitions
 * @master: the master partition (describes whole MTD device)
 * @types: names of partition parsers to try or %NULL
 * @pparts: array of partitions found is returned here
 * @data: MTD partition parser-specific data
 *
 * This function tries to find partition on MTD device @master. It uses MTD
 * partition parsers, specified in @types. However, if @types is %NULL, then
 * the default list of parsers is used. The default list contains only the
 * "cmdlinepart" and "ofpart" parsers ATM.
 * Note: If there are more then one parser in @types, the kernel only takes the
 * partitions parsed out by the first parser.
 *
 * This function may return:
 * o a negative error code in case of failure
 * o zero if no partitions were found
 * o a positive number of found partitions, in which case on exit @pparts will
 *   point to an array containing this number of &struct mtd_info objects.
 */
int parse_mtd_partitions(struct mtd_info *master, const char *const *types,
			 struct mtd_partition **pparts,
			 struct mtd_part_parser_data *data)
{
	struct mtd_part_parser *parser;
	int ret = 0;

	if (!types)
		types = default_mtd_part_types;

	for ( ; ret <= 0 && *types; types++) {
		parser = get_partition_parser(*types);
		if (!parser && !request_module("%s", *types))
			parser = get_partition_parser(*types);
		if (!parser)
			continue;
		/* 调用 parse_fn函数，解析分区信息 */
		ret = (*parser->parse_fn)(master, pparts, data);
		put_partition_parser(parser);
		if (ret > 0) {
			printk(KERN_NOTICE "%d %s partitions found on MTD device %s\n",
			       ret, parser->name, master->name);
			break;
		}
	}
	return ret;
}

static LIST_HEAD(part_parsers);

static struct mtd_part_parser *get_partition_parser(const char *name)
{
	struct mtd_part_parser *p, *ret = NULL;

	spin_lock(&part_parser_lock);
	/* 调用register_mtd_parser会往part_parsers链表中添加 */
	list_for_each_entry(p, &part_parsers, list)
		/* 如果名称一样，则返回 */
		if (!strcmp(p->name, name) && try_module_get(p->owner)) {
			ret = p;
			break;
		}

	spin_unlock(&part_parser_lock);

	return ret;
}


/*
 * This function, given a master MTD object and a partition table, creates
 * and registers slave MTD objects which are bound to the master according to
 * the partition definitions.
 *
 * We don't register the master, or expect the caller to have done so,
 * for reasons of data integrity.
 */

int add_mtd_partitions(struct mtd_info *master,
		       const struct mtd_partition *parts,
		       int nbparts)
{
	struct mtd_part *slave;
	uint64_t cur_offset = 0;
	int i;

	printk(KERN_NOTICE "Creating %d MTD partitions on \"%s\":\n", nbparts, master->name);

	for (i = 0; i < nbparts; i++) {
		slave = allocate_partition(master, parts + i, i, cur_offset);

		mutex_lock(&mtd_partitions_mutex);
		/** 添加到mtd_partitions链表中 **/
		list_add(&slave->list, &mtd_partitions);
		mutex_unlock(&mtd_partitions_mutex);

		add_mtd_device(&slave->mtd);

		cur_offset = slave->offset + slave->mtd.size;
	}

	return 0;
}

static struct mtd_part *allocate_partition(struct mtd_info *master,
			const struct mtd_partition *part, int partno,
			uint64_t cur_offset)
{
	struct mtd_part *slave;
	char *name;

	/* allocate the partition structure */
	slave = kzalloc(sizeof(*slave), GFP_KERNEL);
	name = kstrdup(part->name, GFP_KERNEL);

	/* set up the MTD object for this partition */
	slave->mtd.type = master->type;
	slave->mtd.flags = master->flags & ~part->mask_flags;
	slave->mtd.size = part->size;
	slave->mtd.writesize = master->writesize;
	slave->mtd.writebufsize = master->writebufsize;
	slave->mtd.oobsize = master->oobsize;
	slave->mtd.oobavail = master->oobavail;
	slave->mtd.subpage_sft = master->subpage_sft;

	slave->mtd.name = name;
	slave->mtd.owner = master->owner;

	/* NOTE:  we don't arrange MTDs as a tree; it'd be error-prone
	 * to have the same data be in two different partitions.
	 */
	slave->mtd.dev.parent = master->dev.parent;

	slave->mtd._read = part_read;
	slave->mtd._write = part_write;

	if (master->_panic_write)
		slave->mtd._panic_write = part_panic_write;

	if (master->_point && master->_unpoint) {
		slave->mtd._point = part_point;
		slave->mtd._unpoint = part_unpoint;
	}

	if (master->_get_unmapped_area)
		slave->mtd._get_unmapped_area = part_get_unmapped_area;
	if (master->_read_oob)
		slave->mtd._read_oob = part_read_oob;
	if (master->_write_oob)
		slave->mtd._write_oob = part_write_oob;
	if (master->_read_user_prot_reg)
		slave->mtd._read_user_prot_reg = part_read_user_prot_reg;
	if (master->_read_fact_prot_reg)
		slave->mtd._read_fact_prot_reg = part_read_fact_prot_reg;
	if (master->_write_user_prot_reg)
		slave->mtd._write_user_prot_reg = part_write_user_prot_reg;
	if (master->_lock_user_prot_reg)
		slave->mtd._lock_user_prot_reg = part_lock_user_prot_reg;
	if (master->_get_user_prot_info)
		slave->mtd._get_user_prot_info = part_get_user_prot_info;
	if (master->_get_fact_prot_info)
		slave->mtd._get_fact_prot_info = part_get_fact_prot_info;
	if (master->_sync)
		slave->mtd._sync = part_sync;
	if (!partno && !master->dev.class && master->_suspend &&
	    master->_resume) {
			slave->mtd._suspend = part_suspend;
			slave->mtd._resume = part_resume;
	}
	if (master->_writev)
		slave->mtd._writev = part_writev;
	if (master->_lock)
		slave->mtd._lock = part_lock;
	if (master->_unlock)
		slave->mtd._unlock = part_unlock;
	if (master->_is_locked)
		slave->mtd._is_locked = part_is_locked;
	if (master->_block_isreserved)
		slave->mtd._block_isreserved = part_block_isreserved;
	if (master->_block_isbad)
		slave->mtd._block_isbad = part_block_isbad;
	if (master->_block_markbad)
		slave->mtd._block_markbad = part_block_markbad;
	slave->mtd._erase = part_erase;
	slave->master = master;
	slave->offset = part->offset;

	if (slave->offset == MTDPART_OFS_APPEND)
		slave->offset = cur_offset;
	if (slave->offset == MTDPART_OFS_NXTBLK) {
		slave->offset = cur_offset;
		if (mtd_mod_by_eb(cur_offset, master) != 0) {
			/* Round up to next erasesize */
			slave->offset = (mtd_div_by_eb(cur_offset, master) + 1) * master->erasesize;
			printk(KERN_NOTICE "Moving partition %d: "
			       "0x%012llx -> 0x%012llx\n", partno,
			       (unsigned long long)cur_offset, (unsigned long long)slave->offset);
		}
	}
	if (slave->offset == MTDPART_OFS_RETAIN) {
		slave->offset = cur_offset;
		if (master->size - slave->offset >= slave->mtd.size) {
			slave->mtd.size = master->size - slave->offset
							- slave->mtd.size;
		} else {
			printk(KERN_ERR "mtd partition \"%s\" doesn't have enough space: %#llx < %#llx, disabled\n",
				part->name, master->size - slave->offset,
				slave->mtd.size);
			/* register to preserve ordering */
			goto out_register;
		}
	}
	if (slave->mtd.size == MTDPART_SIZ_FULL)
		slave->mtd.size = master->size - slave->offset;

	printk(KERN_NOTICE "0x%012llx-0x%012llx : \"%s\"\n", (unsigned long long)slave->offset,
		(unsigned long long)(slave->offset + slave->mtd.size), slave->mtd.name);

	/* let's do some sanity checks */
	if (slave->offset >= master->size) {
		/* let's register it anyway to preserve ordering */
		slave->offset = 0;
		slave->mtd.size = 0;
		printk(KERN_ERR"mtd: partition \"%s\" is out of reach -- disabled\n",
			part->name);
		goto out_register;
	}
	if (slave->offset + slave->mtd.size > master->size) {
		slave->mtd.size = master->size - slave->offset;
		printk(KERN_WARNING"mtd: partition \"%s\" extends beyond the end of device \"%s\" -- size truncated to %#llx\n",
			part->name, master->name, (unsigned long long)slave->mtd.size);
	}
	if (master->numeraseregions > 1) {
		/* Deal with variable erase size stuff */
		int i, max = master->numeraseregions;
		u64 end = slave->offset + slave->mtd.size;
		struct mtd_erase_region_info *regions = master->eraseregions;

		/* Find the first erase regions which is part of this
		 * partition. */
		for (i = 0; i < max && regions[i].offset <= slave->offset; i++)
			;
		/* The loop searched for the region _behind_ the first one */
		if (i > 0)
			i--;

		/* Pick biggest erasesize */
		for (; i < max && regions[i].offset < end; i++) {
			if (slave->mtd.erasesize < regions[i].erasesize) {
				slave->mtd.erasesize = regions[i].erasesize;
			}
		}
		BUG_ON(slave->mtd.erasesize == 0);
	} else {
		/* Single erase size */
		slave->mtd.erasesize = master->erasesize;
	}

	if ((slave->mtd.flags & MTD_WRITEABLE) &&
	    mtd_mod_by_eb(slave->offset, &slave->mtd)) {
		/* Doesn't start on a boundary of major erase size */
		/* FIXME: Let it be writable if it is on a boundary of
		 * _minor_ erase size though */
		slave->mtd.flags &= ~MTD_WRITEABLE;
		printk(KERN_WARNING"mtd: partition \"%s\" doesn't start on an erase block boundary -- force read-only\n",
			part->name);
	}
	if ((slave->mtd.flags & MTD_WRITEABLE) &&
	    mtd_mod_by_eb(slave->mtd.size, &slave->mtd)) {
		slave->mtd.flags &= ~MTD_WRITEABLE;
		printk(KERN_WARNING"mtd: partition \"%s\" doesn't end on an erase block -- force read-only\n",
			part->name);
	}

	slave->mtd.ecclayout = master->ecclayout;
	slave->mtd.ecc_step_size = master->ecc_step_size;
	slave->mtd.ecc_strength = master->ecc_strength;
	slave->mtd.bitflip_threshold = master->bitflip_threshold;

	if (master->_block_isbad) {
		uint64_t offs = 0;

		while (offs < slave->mtd.size) {
			if (mtd_block_isreserved(master, offs + slave->offset))
				slave->mtd.ecc_stats.bbtblocks++;
			else if (mtd_block_isbad(master, offs + slave->offset))
				slave->mtd.ecc_stats.badblocks++;
			offs += slave->mtd.erasesize;
		}
	}

out_register:
	return slave;
}

#define MTD_CHAR_MAJOR		90
#define MTD_DEVT(index) MKDEV(MTD_CHAR_MAJOR, (index)*2)


/**
 *	add_mtd_device - register an MTD device
 *	@mtd: pointer to new MTD device info structure
 *
 *	Add a device to the list of MTD devices present in the system, and
 *	notify each currently active MTD 'user' of its arrival. Returns
 *	zero on success or 1 on failure, which currently will only happen
 *	if there is insufficient memory or a sysfs error.
 */

int add_mtd_device(struct mtd_info *mtd)
{
	struct mtd_notifier *not;
	int i, error;

	mtd->backing_dev_info = &mtd_bdi;

	BUG_ON(mtd->writesize == 0);
	mutex_lock(&mtd_table_mutex);

	i = idr_alloc(&mtd_idr, mtd, 0, 0, GFP_KERNEL);
	if (i < 0)
		goto fail_locked;

	mtd->index = i;
	mtd->usecount = 0;

	/* default value if not set by driver */
	if (mtd->bitflip_threshold == 0)
		mtd->bitflip_threshold = mtd->ecc_strength;

	if (is_power_of_2(mtd->erasesize))
		mtd->erasesize_shift = ffs(mtd->erasesize) - 1;
	else
		mtd->erasesize_shift = 0;

	if (is_power_of_2(mtd->writesize))
		mtd->writesize_shift = ffs(mtd->writesize) - 1;
	else
		mtd->writesize_shift = 0;

	mtd->erasesize_mask = (1 << mtd->erasesize_shift) - 1;
	mtd->writesize_mask = (1 << mtd->writesize_shift) - 1;

	/* Some chips always power up locked. Unlock them now */
	if ((mtd->flags & MTD_WRITEABLE) && (mtd->flags & MTD_POWERUP_LOCK)) {
		error = mtd_unlock(mtd, 0, mtd->size);
		if (error && error != -EOPNOTSUPP)
			printk(KERN_WARNING
			       "%s: unlock failed, writes may not work\n",
			       mtd->name);
	}

	/* Caller should have set dev.parent to match the
	 * physical device.
	 */
	mtd->dev.type = &mtd_devtype;
	mtd->dev.class = &mtd_class;
	mtd->dev.devt = MTD_DEVT(i);
	dev_set_name(&mtd->dev, "mtd%d", i);
	dev_set_drvdata(&mtd->dev, mtd);
	/* 注册字符设备驱动，已经注册了MTD_CHAR_MAJOR为主设备号的字符驱动
	 * 在dev下为mtdX
	 */
	if (device_register(&mtd->dev) != 0)
		goto fail_added;

	/* 在dev下为mtdXro */
	device_create(&mtd_class, mtd->dev.parent, MTD_DEVT(i) + 1, NULL,
		      "mtd%dro", i);

	pr_debug("mtd: Giving out device %d to %s\n", i, mtd->name);
	/* No need to get a refcount on the module containing
	   the notifier, since we hold the mtd_table_mutex */

	/* register_mtd_user会往mtd_notifiers添加 */
	list_for_each_entry(not, &mtd_notifiers, list)
		not->add(mtd);

	mutex_unlock(&mtd_table_mutex);
	/* We _know_ we aren't being removed, because
	   our caller is still holding us here. So none
	   of this try_ nonsense, and no bitching about it
	   either. :) */
	__module_get(THIS_MODULE);
	return 0;
}


int register_mtd_blktrans(struct mtd_blktrans_ops *tr)
{
	struct mtd_info *mtd;
	int ret;

	/* Register the notifier if/when the first device type is
	   registered, to prevent the link/init ordering from fucking
	   us over. */
	if (!blktrans_notifier.list.next)
		register_mtd_user(&blktrans_notifier);


	mutex_lock(&mtd_table_mutex);

	ret = register_blkdev(tr->major, tr->name);

	if (ret)
		tr->major = ret;

	tr->blkshift = ffs(tr->blksize) - 1;

	INIT_LIST_HEAD(&tr->devs);
	list_add(&tr->list, &blktrans_majors);

	mtd_for_each_device(mtd)
		if (mtd->type != MTD_ABSENT)
			tr->add_mtd(tr, mtd);

	mutex_unlock(&mtd_table_mutex);
	return 0;
}


static void blktrans_notify_add(struct mtd_info *mtd)
{
	struct mtd_blktrans_ops *tr;

	if (mtd->type == MTD_ABSENT)
		return;

	list_for_each_entry(tr, &blktrans_majors, list)
		tr->add_mtd(tr, mtd);
}

static struct mtd_notifier blktrans_notifier = {
	.add = blktrans_notify_add,
	.remove = blktrans_notify_remove,
};

static void mtdblock_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
	struct mtdblk_dev *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	if (!dev)
		return;

	dev->mbd.mtd = mtd;
	dev->mbd.devnum = mtd->index;

	dev->mbd.size = mtd->size >> 9;
	dev->mbd.tr = tr;

	if (!(mtd->flags & MTD_WRITEABLE))
		dev->mbd.readonly = 1;

	if (add_mtd_blktrans_dev(&dev->mbd))
		kfree(dev);
}


int add_mtd_blktrans_dev(struct mtd_blktrans_dev *new)
{
	struct mtd_blktrans_ops *tr = new->tr;
	struct mtd_blktrans_dev *d;
	int last_devnum = -1;
	struct gendisk *gd;
	int ret;

	if (mutex_trylock(&mtd_table_mutex)) {
		mutex_unlock(&mtd_table_mutex);
		BUG();
	}

	mutex_lock(&blktrans_ref_mutex);
	list_for_each_entry(d, &tr->devs, list) {
		if (new->devnum == -1) {
			/* Use first free number */
			if (d->devnum != last_devnum+1) {
				/* Found a free devnum. Plug it in here */
				new->devnum = last_devnum+1;
				list_add_tail(&new->list, &d->list);
				goto added;
			}
		} else if (d->devnum == new->devnum) {
			/* Required number taken */
			mutex_unlock(&blktrans_ref_mutex);
			return -EBUSY;
		} else if (d->devnum > new->devnum) {
			/* Required number was free */
			list_add_tail(&new->list, &d->list);
			goto added;
		}
		last_devnum = d->devnum;
	}

	ret = -EBUSY;
	if (new->devnum == -1)
		new->devnum = last_devnum+1;

	/* Check that the device and any partitions will get valid
	 * minor numbers and that the disk naming code below can cope
	 * with this number. */
	if (new->devnum > (MINORMASK >> tr->part_bits) ||
	    (tr->part_bits && new->devnum >= 27 * 26)) {
		mutex_unlock(&blktrans_ref_mutex);
		goto error1;
	}

	list_add_tail(&new->list, &tr->devs);
 added:
	mutex_unlock(&blktrans_ref_mutex);

	mutex_init(&new->lock);
	kref_init(&new->ref);
	if (!tr->writesect)
		new->readonly = 1;

	/* Create gendisk */
	/* 注册块设备驱动 */
	ret = -ENOMEM;
	gd = alloc_disk(1 << tr->part_bits);

	new->disk = gd;
	gd->private_data = new;
	gd->major = tr->major;
	gd->first_minor = (new->devnum) << tr->part_bits;
	gd->fops = &mtd_block_ops;

	if (tr->part_bits)
		if (new->devnum < 26)
			snprintf(gd->disk_name, sizeof(gd->disk_name),
				 "%s%c", tr->name, 'a' + new->devnum);
		else
			snprintf(gd->disk_name, sizeof(gd->disk_name),
				 "%s%c%c", tr->name,
				 'a' - 1 + new->devnum / 26,
				 'a' + new->devnum % 26);
	else
		snprintf(gd->disk_name, sizeof(gd->disk_name),
			 "%s%d", tr->name, new->devnum);

	set_capacity(gd, (new->size * tr->blksize) >> 9);

	/* Create the request queue */
	spin_lock_init(&new->queue_lock);
	new->rq = blk_init_queue(mtd_blktrans_request, &new->queue_lock);

	if (tr->flush)
		blk_queue_flush(new->rq, REQ_FLUSH);

	new->rq->queuedata = new;
	blk_queue_logical_block_size(new->rq, tr->blksize);

	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, new->rq);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, new->rq);

	if (tr->discard) {
		queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, new->rq);
		new->rq->limits.max_discard_sectors = UINT_MAX;
	}

	gd->queue = new->rq;

	/* Create processing workqueue */
	new->wq = alloc_workqueue("%s%d", 0, 0,
				  tr->name, new->mtd->index);

	INIT_WORK(&new->work, mtd_blktrans_work);

	gd->driverfs_dev = &new->mtd->dev;

	if (new->readonly)
		set_disk_ro(gd, 1);

	add_disk(gd);

	if (new->disk_attributes) {
		ret = sysfs_create_group(&disk_to_dev(gd)->kobj,
					new->disk_attributes);
		WARN_ON(ret);
	}
	return 0;
}


static struct mtd_blktrans_ops mtdblock_tr = {
	.name		= "mtdblock",
	.major		= MTD_BLOCK_MAJOR,
	.part_bits	= 0,
	.blksize 	= 512,
	.readsect	= mtdblock_readsect,
	.writesect	= mtdblock_writesect,
	.add_mtd	= mtdblock_add_mtd,
	.remove_dev	= mtdblock_remove_dev,
	.owner		= THIS_MODULE,
};

static int __init mtdblock_init(void)
{
	return register_mtd_blktrans(&mtdblock_tr);
}


static int __init init_mtd(void)
{
	int ret;

	ret = class_register(&mtd_class);

	ret = mtd_bdi_init(&mtd_bdi, "mtd");

	proc_mtd = proc_create("mtd", 0, NULL, &mtd_proc_ops);

	ret = init_mtdchar();

	return 0;
}

module_init(init_mtd);

int __init init_mtdchar(void)
{
	int ret;

	ret = __register_chrdev(MTD_CHAR_MAJOR, 0, 1 << MINORBITS,
				   "mtd", &mtd_fops);

	return ret;
}


static int parse_ofpart_partitions(struct mtd_info *master,
				   struct mtd_partition **pparts,
				   struct mtd_part_parser_data *data)
{
	struct device_node *node;
	const char *partname;
	struct device_node *pp;
	int nr_parts, i;


	if (!data)
		return 0;

	node = data->of_node;
	if (!node)
		return 0;

	/* First count the subnodes */
	nr_parts = 0;
	for_each_child_of_node(node,  pp) {
		if (node_has_compatible(pp))
			continue;

		nr_parts++;
	}

	if (nr_parts == 0)
		return 0;

	*pparts = kzalloc(nr_parts * sizeof(**pparts), GFP_KERNEL);
	if (!*pparts)
		return -ENOMEM;

	i = 0;
	for_each_child_of_node(node,  pp) {
		const __be32 *reg;
		int len;
		int a_cells, s_cells;

		if (node_has_compatible(pp))
			continue;

		reg = of_get_property(pp, "reg", &len);
		if (!reg) {
			nr_parts--;
			continue;
		}

		a_cells = of_n_addr_cells(pp);
		s_cells = of_n_size_cells(pp);
		(*pparts)[i].offset = of_read_number(reg, a_cells);
		(*pparts)[i].size = of_read_number(reg + a_cells, s_cells);

		partname = of_get_property(pp, "label", &len);
		if (!partname)
			partname = of_get_property(pp, "name", &len);
		(*pparts)[i].name = partname;

		if (of_get_property(pp, "read-only", &len))
			(*pparts)[i].mask_flags |= MTD_WRITEABLE;

		if (of_get_property(pp, "lock", &len))
			(*pparts)[i].mask_flags |= MTD_POWERUP_LOCK;

		i++;
	}

	if (!i) {
		of_node_put(pp);
		pr_err("No valid partition found on %s\n", node->full_name);
		kfree(*pparts);
		*pparts = NULL;
		return -EINVAL;
	}
	/* 返回分区的数目 */
	return nr_parts;
}


static struct mtd_part_parser ofpart_parser = {
	.owner = THIS_MODULE,
	.parse_fn = parse_ofpart_partitions,
	.name = "ofpart",
};

static int __init ofpart_parser_init(void)
{
	register_mtd_parser(&ofpart_parser);
	register_mtd_parser(&ofoldpart_parser);
	return 0;
}

