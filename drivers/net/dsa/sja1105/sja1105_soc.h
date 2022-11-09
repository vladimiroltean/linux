/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Sensor-Technik Wiedemann GmbH
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_SOC_H
#define _SJA1105_SOC_H

#include <linux/types.h>

#define SJA1105E_DEVICE_ID		0x9C00000Cull
#define SJA1105T_DEVICE_ID		0x9E00030Eull
#define SJA1105PR_DEVICE_ID		0xAF00030Eull
#define SJA1105QS_DEVICE_ID		0xAE00030Eull
#define SJA1110_DEVICE_ID		0xB700030Full

#define SJA1105ET_PART_NO		0x9A83
#define SJA1105P_PART_NO		0x9A84
#define SJA1105Q_PART_NO		0x9A85
#define SJA1105R_PART_NO		0x9A86
#define SJA1105S_PART_NO		0x9A87
#define SJA1110A_PART_NO		0x1110
#define SJA1110B_PART_NO		0x1111
#define SJA1110C_PART_NO		0x1112
#define SJA1110D_PART_NO		0x1113

#define SJA1105_NUM_PORTS		5
#define SJA1110_NUM_PORTS		11
#define SJA1105_MAX_NUM_PORTS		SJA1110_NUM_PORTS

#define SJA1110_ACU			0x1c4400
#define SJA1110_RGU			0x1c6000
#define SJA1110_CGU			0x1c6400

#define SJA1110_SPI_ADDR(x)		((x) / 4)
#define SJA1110_ACU_ADDR(x)		(SJA1110_ACU + SJA1110_SPI_ADDR(x))
#define SJA1110_CGU_ADDR(x)		(SJA1110_CGU + SJA1110_SPI_ADDR(x))
#define SJA1110_RGU_ADDR(x)		(SJA1110_RGU + SJA1110_SPI_ADDR(x))

#define SJA1105_RSV_ADDR		0xffffffffffffffffull

struct device;
struct device_node;
struct platform_device;
struct ptp_system_timestamp;
struct resource;

typedef enum {
	SPI_READ = 0,
	SPI_WRITE = 1,
} sja1105_spi_rw_mode_t;

enum sja1105_stats_area {
	MAC,
	HL1,
	HL2,
	ETHER,
	__MAX_SJA1105_STATS_AREA,
};

/* Keeps the different addresses between switch generations */
struct sja1105_regs {
	u64 device_id;
	u64 prod_id;
	u64 status;
	u64 port_control;
	u64 rgu;
	u64 vl_status;
	u64 config;
	u64 rmii_pll1;
	u64 ptppinst;
	u64 ptppindur;
	u64 ptp_control;
	u64 ptpclkval;
	u64 ptpclkrate;
	u64 ptpclkcorp;
	u64 ptpsyncts;
	u64 ptpschtm;
	u64 ptpegr_ts[SJA1105_MAX_NUM_PORTS];
	u64 pad_mii_tx[SJA1105_MAX_NUM_PORTS];
	u64 pad_mii_rx[SJA1105_MAX_NUM_PORTS];
	u64 pad_mii_id[SJA1105_MAX_NUM_PORTS];
	u64 cgu_idiv[SJA1105_MAX_NUM_PORTS];
	u64 mii_tx_clk[SJA1105_MAX_NUM_PORTS];
	u64 mii_rx_clk[SJA1105_MAX_NUM_PORTS];
	u64 mii_ext_tx_clk[SJA1105_MAX_NUM_PORTS];
	u64 mii_ext_rx_clk[SJA1105_MAX_NUM_PORTS];
	u64 rgmii_tx_clk[SJA1105_MAX_NUM_PORTS];
	u64 rmii_ref_clk[SJA1105_MAX_NUM_PORTS];
	u64 rmii_ext_tx_clk[SJA1105_MAX_NUM_PORTS];
	u64 stats[__MAX_SJA1105_STATS_AREA][SJA1105_MAX_NUM_PORTS];
	u64 pcs_base[SJA1105_MAX_NUM_PORTS];
};

struct sja1105_soc {
	struct spi_device *spidev;
	size_t max_xfer_len;
	const struct sja1105_regs *regs;
	u32 device_id;
	u32 part_no;
	u32 silicon_rev;
};

int sja1105_xfer_buf(const struct sja1105_soc *soc, sja1105_spi_rw_mode_t rw,
		     u64 reg_addr, u8 *buf, size_t len);
int sja1105_xfer_u32(const struct sja1105_soc *soc, sja1105_spi_rw_mode_t rw,
		     u64 reg_addr, u32 *value,
		     struct ptp_system_timestamp *ptp_sts);
int sja1105_xfer_u64(const struct sja1105_soc *soc, sja1105_spi_rw_mode_t rw,
		     u64 reg_addr, u64 *value,
		     struct ptp_system_timestamp *ptp_sts);

extern const struct sja1105_regs sja1105et_regs;
extern const struct sja1105_regs sja1105pqrs_regs;
extern const struct sja1105_regs sja1110_regs;

struct platform_device *
sja1110_compat_device_create(struct device *parent, struct device_node *np,
			     const char *name, const struct resource *resources,
			     size_t num_resources);

void sja1110_compat_device_destroy(struct platform_device *pdev);

struct regmap *sja1110_create_regmap(struct sja1105_soc *soc,
				     const struct resource *res);

struct sja1105_soc *sja1105_soc_create(struct spi_device *spi,
				       const struct sja1105_regs *regs);
void sja1105_soc_destroy(struct sja1105_soc *soc);

#endif
