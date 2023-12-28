// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2016-2018 NXP
 * Copyright (c) 2018, Sensor-Technik Wiedemann GmbH
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include <linux/ioport.h>
#include <linux/mfd/core.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/packing.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>

#include "sja1105.h"
#include "sja1105_soc.h"

#define SJA1105_PROD_ID_PART_NO_X(val)		(((val) & GENMASK(19, 4)) >> 4)

struct sja1110_regmap_bus_context {
	unsigned long reg_base;
	struct sja1105_soc *soc;
};

struct sja1105_chunk {
	u8	*buf;
	size_t	len;
	u64	reg_addr;
};

struct sja1105_spi_message {
	u64 access;
	u64 read_count;
	u64 address;
};

static void
sja1105_spi_message_pack(void *buf, const struct sja1105_spi_message *msg)
{
	const int size = SJA1105_SIZE_SPI_MSG_HEADER;

	memset(buf, 0, size);

	sja1105_pack(buf, &msg->access,     31, 31, size);
	sja1105_pack(buf, &msg->read_count, 30, 25, size);
	sja1105_pack(buf, &msg->address,    24,  4, size);
}

/* If @rw is:
 * - SPI_WRITE: creates and sends an SPI write message at absolute
 *		address reg_addr, taking @len bytes from *buf
 * - SPI_READ:  creates and sends an SPI read message from absolute
 *		address reg_addr, writing @len bytes into *buf
 */
static int sja1105_xfer(const struct sja1105_soc *soc, sja1105_spi_rw_mode_t rw,
			u64 reg_addr, u8 *buf, size_t len,
			struct ptp_system_timestamp *ptp_sts)
{
	u8 hdr_buf[SJA1105_SIZE_SPI_MSG_HEADER] = {0};
	struct spi_device *spi = soc->spidev;
	struct spi_transfer xfers[2] = {0};
	struct spi_transfer *chunk_xfer;
	struct spi_transfer *hdr_xfer;
	struct sja1105_chunk chunk;
	int num_chunks;
	int rc, i = 0;

	num_chunks = DIV_ROUND_UP(len, soc->max_xfer_len);

	chunk.reg_addr = reg_addr;
	chunk.buf = buf;
	chunk.len = min_t(size_t, len, soc->max_xfer_len);

	hdr_xfer = &xfers[0];
	chunk_xfer = &xfers[1];

	for (i = 0; i < num_chunks; i++) {
		struct spi_transfer *ptp_sts_xfer;
		struct sja1105_spi_message msg;

		/* Populate the transfer's header buffer */
		msg.address = chunk.reg_addr;
		msg.access = rw;
		if (rw == SPI_READ)
			msg.read_count = chunk.len / 4;
		else
			/* Ignored */
			msg.read_count = 0;
		sja1105_spi_message_pack(hdr_buf, &msg);
		hdr_xfer->tx_buf = hdr_buf;
		hdr_xfer->len = SJA1105_SIZE_SPI_MSG_HEADER;

		/* Populate the transfer's data buffer */
		if (rw == SPI_READ)
			chunk_xfer->rx_buf = chunk.buf;
		else
			chunk_xfer->tx_buf = chunk.buf;
		chunk_xfer->len = chunk.len;

		/* Request timestamping for the transfer. Instead of letting
		 * callers specify which byte they want to timestamp, we can
		 * make certain assumptions:
		 * - A read operation will request a software timestamp when
		 *   what's being read is the PTP time. That is snapshotted by
		 *   the switch hardware at the end of the command portion
		 *   (hdr_xfer).
		 * - A write operation will request a software timestamp on
		 *   actions that modify the PTP time. Taking clock stepping as
		 *   an example, the switch writes the PTP time at the end of
		 *   the data portion (chunk_xfer).
		 */
		if (rw == SPI_READ)
			ptp_sts_xfer = hdr_xfer;
		else
			ptp_sts_xfer = chunk_xfer;
		ptp_sts_xfer->ptp_sts_word_pre = ptp_sts_xfer->len - 1;
		ptp_sts_xfer->ptp_sts_word_post = ptp_sts_xfer->len - 1;
		ptp_sts_xfer->ptp_sts = ptp_sts;

		/* Calculate next chunk */
		chunk.buf += chunk.len;
		chunk.reg_addr += chunk.len / 4;
		chunk.len = min_t(size_t, (ptrdiff_t)(buf + len - chunk.buf),
				  soc->max_xfer_len);

		rc = spi_sync_transfer(spi, xfers, 2);
		if (rc < 0) {
			dev_err(&spi->dev, "SPI transfer failed: %d\n", rc);
			return rc;
		}
	}

	return 0;
}

int sja1105_xfer_buf(const struct sja1105_soc *soc, sja1105_spi_rw_mode_t rw,
		     u64 reg_addr, u8 *buf, size_t len)
{
	return sja1105_xfer(soc, rw, reg_addr, buf, len, NULL);
}

/* If @rw is:
 * - SPI_WRITE: creates and sends an SPI write message at absolute
 *		address reg_addr
 * - SPI_READ:  creates and sends an SPI read message from absolute
 *		address reg_addr
 *
 * The u64 *value is unpacked, meaning that it's stored in the native
 * CPU endianness and directly usable by software running on the core.
 */
int sja1105_xfer_u64(const struct sja1105_soc *soc, sja1105_spi_rw_mode_t rw,
		     u64 reg_addr, u64 *value,
		     struct ptp_system_timestamp *ptp_sts)
{
	u8 packed_buf[8];
	int rc;

	if (rw == SPI_WRITE)
		sja1105_pack(packed_buf, value, 63, 0, 8);

	rc = sja1105_xfer(soc, rw, reg_addr, packed_buf, 8, ptp_sts);

	if (rw == SPI_READ)
		sja1105_unpack(packed_buf, value, 63, 0, 8);

	return rc;
}

/* Same as above, but transfers only a 4 byte word */
int sja1105_xfer_u32(const struct sja1105_soc *soc, sja1105_spi_rw_mode_t rw,
		     u64 reg_addr, u32 *value,
		     struct ptp_system_timestamp *ptp_sts)
{
	u8 packed_buf[4];
	u64 tmp;
	int rc;

	if (rw == SPI_WRITE) {
		/* The packing API only supports u64 as CPU word size,
		 * so we need to convert.
		 */
		tmp = *value;
		sja1105_pack(packed_buf, &tmp, 31, 0, 4);
	}

	rc = sja1105_xfer(soc, rw, reg_addr, packed_buf, 4, ptp_sts);

	if (rw == SPI_READ) {
		sja1105_unpack(packed_buf, &tmp, 31, 0, 4);
		*value = tmp;
	}

	return rc;
}

static int sja1110_regmap_bus_reg_read(void *context, unsigned int reg,
				       unsigned int *val)
{
	struct sja1110_regmap_bus_context *ctx = context;
	struct sja1105_soc *soc = ctx->soc;
	u32 tmp;
	int rc;

	reg += ctx->reg_base;

	rc = sja1105_xfer_u32(soc, SPI_READ, SJA1110_SPI_ADDR(reg), &tmp,
			      NULL);
	if (rc)
		return rc;

	*val = tmp;

	return 0;
}

static int sja1110_regmap_bus_reg_write(void *context, unsigned int reg,
					unsigned int val)
{
	struct sja1110_regmap_bus_context *ctx = context;
	struct sja1105_soc *soc = ctx->soc;
	u32 tmp = val;

	reg += ctx->reg_base;

	return sja1105_xfer_u32(soc, SPI_WRITE, SJA1110_SPI_ADDR(reg), &tmp,
				NULL);
}

static struct regmap_bus sja1110_regmap_bus = {
	.reg_read = sja1110_regmap_bus_reg_read,
	.reg_write = sja1110_regmap_bus_reg_write,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

static const struct regmap_config sja1110_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

struct regmap *sja1110_create_regmap(struct sja1105_soc *soc,
				     const struct resource *res)
{
	struct device *dev = &soc->spidev->dev;
	struct sja1110_regmap_bus_context *ctx;
	struct regmap_config regmap_config;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->reg_base = res->start;
	ctx->soc = soc;

	memcpy(&regmap_config, &sja1110_regmap_config, sizeof(regmap_config));

	regmap_config.name = res->name;
	regmap_config.max_register = resource_size(res) - 1;

	return devm_regmap_init(dev, &sja1110_regmap_bus, ctx, &regmap_config);
}

struct platform_device *
sja1110_compat_device_create(struct device *parent, struct device_node *np,
			     const char *name, const struct resource *resources,
			     size_t num_resources)
{
	struct platform_device *pdev;
	int err = -ENOMEM;

	pdev = platform_device_alloc(name, PLATFORM_DEVID_NONE);
	if (!pdev)
		goto out;

	pdev->dev.parent = parent;
	pdev->dev.of_node = np;
	pdev->dev.fwnode = &np->fwnode;

	err = platform_device_add_resources(pdev, resources, num_resources);
	if (err)
		goto out_free_pdev;

	err = platform_device_add(pdev);
	if (err)
		goto out_free_pdev;

	return pdev;

out_free_pdev:
	platform_device_put(pdev);
out:
	return ERR_PTR(err);
}

void sja1110_compat_device_destroy(struct platform_device *pdev)
{
	platform_device_unregister(pdev);
}

const struct sja1105_regs sja1105et_regs = {
	.device_id = 0x0,
	.prod_id = 0x100BC3,
	.status = 0x1,
	.port_control = 0x11,
	.vl_status = 0x10000,
	.config = 0x020000,
	.rgu = 0x100440,
	/* UM10944.pdf, Table 86, ACU Register overview */
	.pad_mii_tx = {0x100800, 0x100802, 0x100804, 0x100806, 0x100808},
	.pad_mii_rx = {0x100801, 0x100803, 0x100805, 0x100807, 0x100809},
	.rmii_pll1 = 0x10000A,
	.cgu_idiv = {0x10000B, 0x10000C, 0x10000D, 0x10000E, 0x10000F},
	.stats[MAC] = {0x200, 0x202, 0x204, 0x206, 0x208},
	.stats[HL1] = {0x400, 0x410, 0x420, 0x430, 0x440},
	.stats[HL2] = {0x600, 0x610, 0x620, 0x630, 0x640},
	/* UM10944.pdf, Table 78, CGU Register overview */
	.mii_tx_clk = {0x100013, 0x10001A, 0x100021, 0x100028, 0x10002F},
	.mii_rx_clk = {0x100014, 0x10001B, 0x100022, 0x100029, 0x100030},
	.mii_ext_tx_clk = {0x100018, 0x10001F, 0x100026, 0x10002D, 0x100034},
	.mii_ext_rx_clk = {0x100019, 0x100020, 0x100027, 0x10002E, 0x100035},
	.rgmii_tx_clk = {0x100016, 0x10001D, 0x100024, 0x10002B, 0x100032},
	.rmii_ref_clk = {0x100015, 0x10001C, 0x100023, 0x10002A, 0x100031},
	.rmii_ext_tx_clk = {0x100018, 0x10001F, 0x100026, 0x10002D, 0x100034},
	.ptpegr_ts = {0xC0, 0xC2, 0xC4, 0xC6, 0xC8},
	.ptpschtm = 0x12, /* Spans 0x12 to 0x13 */
	.ptppinst = 0x14,
	.ptppindur = 0x16,
	.ptp_control = 0x17,
	.ptpclkval = 0x18, /* Spans 0x18 to 0x19 */
	.ptpclkrate = 0x1A,
	.ptpclkcorp = 0x1D,
};

const struct sja1105_regs sja1105pqrs_regs = {
	.device_id = 0x0,
	.prod_id = 0x100BC3,
	.status = 0x1,
	.port_control = 0x12,
	.vl_status = 0x10000,
	.config = 0x020000,
	.rgu = 0x100440,
	/* UM10944.pdf, Table 86, ACU Register overview */
	.pad_mii_tx = {0x100800, 0x100802, 0x100804, 0x100806, 0x100808},
	.pad_mii_rx = {0x100801, 0x100803, 0x100805, 0x100807, 0x100809},
	.pad_mii_id = {0x100810, 0x100811, 0x100812, 0x100813, 0x100814},
	.rmii_pll1 = 0x10000A,
	.cgu_idiv = {0x10000B, 0x10000C, 0x10000D, 0x10000E, 0x10000F},
	.stats[MAC] = {0x200, 0x202, 0x204, 0x206, 0x208},
	.stats[HL1] = {0x400, 0x410, 0x420, 0x430, 0x440},
	.stats[HL2] = {0x600, 0x610, 0x620, 0x630, 0x640},
	.stats[ETHER] = {0x1400, 0x1418, 0x1430, 0x1448, 0x1460},
	/* UM11040.pdf, Table 114 */
	.mii_tx_clk = {0x100013, 0x100019, 0x10001F, 0x100025, 0x10002B},
	.mii_rx_clk = {0x100014, 0x10001A, 0x100020, 0x100026, 0x10002C},
	.mii_ext_tx_clk = {0x100017, 0x10001D, 0x100023, 0x100029, 0x10002F},
	.mii_ext_rx_clk = {0x100018, 0x10001E, 0x100024, 0x10002A, 0x100030},
	.rgmii_tx_clk = {0x100016, 0x10001C, 0x100022, 0x100028, 0x10002E},
	.rmii_ref_clk = {0x100015, 0x10001B, 0x100021, 0x100027, 0x10002D},
	.rmii_ext_tx_clk = {0x100017, 0x10001D, 0x100023, 0x100029, 0x10002F},
	.ptpegr_ts = {0xC0, 0xC4, 0xC8, 0xCC, 0xD0},
	.ptpschtm = 0x13, /* Spans 0x13 to 0x14 */
	.ptppinst = 0x15,
	.ptppindur = 0x17,
	.ptp_control = 0x18,
	.ptpclkval = 0x19,
	.ptpclkrate = 0x1B,
	.ptpclkcorp = 0x1E,
	.ptpsyncts = 0x1F,
};

const struct sja1105_regs sja1110_regs = {
	.device_id = SJA1110_SPI_ADDR(0x0),
	.prod_id = SJA1110_ACU_ADDR(0xf00),
	.status = SJA1110_SPI_ADDR(0x4),
	.port_control = SJA1110_SPI_ADDR(0x50), /* actually INHIB_TX */
	.vl_status = 0x10000,
	.config = 0x020000,
	.rgu = SJA1110_RGU_ADDR(0x100), /* Reset Control Register 0 */
	/* Ports 2 and 3 are capable of xMII, but there isn't anything to
	 * configure in the CGU/ACU for them.
	 */
	.pad_mii_tx = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR},
	.pad_mii_rx = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR},
	.pad_mii_id = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1110_ACU_ADDR(0x18), SJA1110_ACU_ADDR(0x28),
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR},
	.rmii_pll1 = SJA1105_RSV_ADDR,
	.cgu_idiv = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.stats[MAC] = {0x200, 0x202, 0x204, 0x206, 0x208, 0x20a,
		       0x20c, 0x20e, 0x210, 0x212, 0x214},
	.stats[HL1] = {0x400, 0x410, 0x420, 0x430, 0x440, 0x450,
		       0x460, 0x470, 0x480, 0x490, 0x4a0},
	.stats[HL2] = {0x600, 0x610, 0x620, 0x630, 0x640, 0x650,
		       0x660, 0x670, 0x680, 0x690, 0x6a0},
	.stats[ETHER] = {0x1400, 0x1418, 0x1430, 0x1448, 0x1460, 0x1478,
			 0x1490, 0x14a8, 0x14c0, 0x14d8, 0x14f0},
	.mii_tx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.mii_rx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.mii_ext_tx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.mii_ext_rx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.rgmii_tx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.rmii_ref_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.rmii_ext_tx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR},
	.ptpschtm = SJA1110_SPI_ADDR(0x54),
	.ptppinst = SJA1110_SPI_ADDR(0x5c),
	.ptppindur = SJA1110_SPI_ADDR(0x64),
	.ptp_control = SJA1110_SPI_ADDR(0x68),
	.ptpclkval = SJA1110_SPI_ADDR(0x6c),
	.ptpclkrate = SJA1110_SPI_ADDR(0x74),
	.ptpclkcorp = SJA1110_SPI_ADDR(0x80),
	.ptpsyncts = SJA1110_SPI_ADDR(0x84),
	.pcs_base = {SJA1105_RSV_ADDR, 0x1c1400, 0x1c1800, 0x1c1c00, 0x1c2000,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
};

static int sja1105_soc_read_device_id(struct sja1105_soc *soc)
{
	const struct sja1105_regs *regs = soc->regs;
	u32 val;
	int rc;

	rc = sja1105_xfer_u32(soc, SPI_READ, regs->device_id, &soc->device_id,
			      NULL);
	if (rc < 0)
		return rc;

	rc = sja1105_xfer_u32(soc, SPI_READ, regs->prod_id, &val, NULL);
	if (rc < 0)
		return rc;

	soc->part_no = SJA1105_PROD_ID_PART_NO_X(val);

	return 0;
}

/* Configure the optional reset pin and bring up switch */
static int sja1105_soc_hw_reset(struct device *dev, unsigned int pulse_len,
				unsigned int startup_delay)
{
	struct gpio_desc *gpio;

	gpio = gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	if (!gpio)
		return 0;

	gpiod_set_value_cansleep(gpio, 1);
	/* Wait for minimum reset pulse length */
	msleep(pulse_len);
	gpiod_set_value_cansleep(gpio, 0);
	/* Wait until chip is ready after reset */
	msleep(startup_delay);

	gpiod_put(gpio);

	return 0;
}

struct sja1105_soc *sja1105_soc_create(struct spi_device *spi,
				       const struct sja1105_regs *regs)
{
	struct device *dev = &spi->dev;
	size_t max_xfer, max_msg;
	struct sja1105_soc *soc;
	int rc;

	if (!dev->of_node) {
		rc = -ENODEV;
		goto out;
	}

	rc = sja1105_soc_hw_reset(dev, 1, 1);
	if (rc)
		goto out;

	soc = kzalloc(sizeof(*soc), GFP_KERNEL);
	if (!soc) {
		rc = -ENOMEM;
		goto out;
	}

	soc->spidev = spi;
	soc->regs = regs;

	/* Configure the SPI bus */
	spi->bits_per_word = 8;
	rc = spi_setup(spi);
	if (rc < 0) {
		dev_err(dev, "Could not init SPI\n");
		goto out_free_soc;
	}

	/* In sja1105_xfer, we send spi_messages composed of two spi_transfers:
	 * a small one for the message header and another one for the current
	 * chunk of the packed buffer.
	 * Check that the restrictions imposed by the SPI controller are
	 * respected: the chunk buffer is smaller than the max transfer size,
	 * and the total length of the chunk plus its message header is smaller
	 * than the max message size.
	 * We do that during probe time since the maximum transfer size is a
	 * runtime invariant.
	 */
	max_xfer = spi_max_transfer_size(spi);
	max_msg = spi_max_message_size(spi);

	/* We need to send at least one 64-bit word of SPI payload per message
	 * in order to be able to make useful progress.
	 */
	if (max_msg < SJA1105_SIZE_SPI_MSG_HEADER + 8) {
		dev_err(dev, "SPI master cannot send large enough buffers, aborting\n");
		rc = -EINVAL;
		goto out_free_soc;
	}

	soc->max_xfer_len = SJA1105_SIZE_SPI_MSG_MAXLEN;
	if (soc->max_xfer_len > max_xfer)
		soc->max_xfer_len = max_xfer;
	if (soc->max_xfer_len > max_msg - SJA1105_SIZE_SPI_MSG_HEADER)
		soc->max_xfer_len = max_msg - SJA1105_SIZE_SPI_MSG_HEADER;

	/* Detect hardware device */
	rc = sja1105_soc_read_device_id(soc);
	if (rc < 0) {
		dev_err(dev, "Device ID check failed: %d\n", rc);
		goto out_free_soc;
	}

	return soc;

out_free_soc:
	kfree(soc);
out:
	return ERR_PTR(rc);
}

void sja1105_soc_destroy(struct sja1105_soc *soc)
{
	kfree(soc);
}
