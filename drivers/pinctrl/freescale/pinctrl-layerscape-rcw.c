// SPDX-License-Identifier: GPL-2.0
/*
 * Layerscape pad muxes: fields of the RCW, changed at runtime.
 *
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#include <linux/bits.h>
#include <linux/fsl/guts.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/seq_file.h>

#include "../core.h"
#include "../pinmux.h"

/*
 * @val and @mask are relative to the shift of the group. A @mask of 0 stands
 * for the field of the group, @width bits wide: that is what a function whose
 * encoding is the whole field has. A function which depends on a second field
 * of the group, or on one only, gives its @mask.
 */
struct rcw_pmux_func {
	const char *name;
	u32 val;
	u32 mask;
};

struct rcw_pmux_group {
	const char *name;
	u8 rcwsr; /* one-based, as in RCWSR12 */
	u8 shift;
	u8 width;
	const struct rcw_pmux_func *funcs;
	u8 nfuncs;
};

/* The pad-mux groups of a SoC, one per field of its RCW */
struct rcw_pmux_soc {
	const struct rcw_pmux_group *groups;
	unsigned int ngroups;
};

#define RCW_PMUX_FUNC(_name, _val)	{ .name = _name, .val = _val }
#define RCW_PMUX_FUNC_MASK(_name, _val, _mask)				\
	{ .name = _name, .val = _val, .mask = _mask }
#define RCW_PMUX_GROUP(_name, _rcwsr, _shift, _width, _funcs)		\
	{								\
		.name = _name, .rcwsr = _rcwsr,				\
		.shift = _shift, .width = _width,			\
		.funcs = _funcs, .nfuncs = ARRAY_SIZE(_funcs),		\
	}

/* Chassis 3.2 (LS1028A, LX2160A): fields with the same encoding */
static const struct rcw_pmux_func fsl_lsch32_iic2_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 2), RCW_PMUX_FUNC("sdhc", 6),
};

static const struct rcw_pmux_func fsl_lsch32_iic1_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func fsl_lsch32_asleep_funcs[] = {
	RCW_PMUX_FUNC("asleep", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("evt", 2),
};

static const struct rcw_pmux_func fsl_lsch32_reset_req_funcs[] = {
	RCW_PMUX_FUNC("reset-req", 0), RCW_PMUX_FUNC("gpio", 1),
};

/* LX2160A: RCWSR12..14 and RCWSR27 */
static const struct rcw_pmux_func lx2160a_iic3_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("can", 2), RCW_PMUX_FUNC("evt", 6),
};

static const struct rcw_pmux_func lx2160a_iic5_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("sdhc-clk", 2), RCW_PMUX_FUNC("spi", 3),
};

static const struct rcw_pmux_func lx2160a_iic6_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("sdhc-clk", 2),
};

static const struct rcw_pmux_func lx2160a_xspi1_funcs[] = {
	RCW_PMUX_FUNC("flexspi", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_sdhc1_base_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 2), RCW_PMUX_FUNC("sdhc-spi", 3),
	RCW_PMUX_FUNC("sdhc-data4", 4),
};

static const struct rcw_pmux_func lx2160a_sdhc1_dir_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 3), RCW_PMUX_FUNC("sdhc-data", 4),
};

static const struct rcw_pmux_func lx2160a_sdhc2_data74_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("i2c", 2), RCW_PMUX_FUNC("flexspi", 6),
};

static const struct rcw_pmux_func lx2160a_sdhc2_base_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 2), RCW_PMUX_FUNC("flexspi", 6),
};

static const struct rcw_pmux_func lx2160a_uart_funcs[] = {
	RCW_PMUX_FUNC("uart", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_uart1_rtscts_funcs[] = {
	RCW_PMUX_FUNC("uart", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("uart3", 2),
};

static const struct rcw_pmux_func lx2160a_uart2_rtscts_funcs[] = {
	RCW_PMUX_FUNC("uart", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("uart4", 2),
};

static const struct rcw_pmux_func lx2160a_irq_funcs[] = {
	RCW_PMUX_FUNC("irq", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_irq_flextimer_funcs[] = {
	RCW_PMUX_FUNC("irq", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 2),
};

static const struct rcw_pmux_func lx2160a_evt_funcs[] = {
	RCW_PMUX_FUNC("evt", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 2),
};

static const struct rcw_pmux_func lx2160a_clk_out_funcs[] = {
	RCW_PMUX_FUNC("clk-out", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 2),
};

static const struct rcw_pmux_func lx2160a_ec1_funcs[] = {
	RCW_PMUX_FUNC("rgmii", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_ec2_funcs[] = {
	RCW_PMUX_FUNC("rgmii", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("tsec-1588", 2),
};

static const struct rcw_pmux_func lx2160a_usb_ext_funcs[] = {
	RCW_PMUX_FUNC("usb", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_gtx_clk_funcs[] = {
	RCW_PMUX_FUNC("gtx-clk", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_sdhc1_ds_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 2),
};

static const struct rcw_pmux_group lx2160a_groups[] = {
	/* RCWSR12 */
	RCW_PMUX_GROUP("iic2", 12, 0, 3, fsl_lsch32_iic2_funcs),
	RCW_PMUX_GROUP("iic3", 12, 3, 3, lx2160a_iic3_funcs),
	RCW_PMUX_GROUP("iic4", 12, 6, 3, lx2160a_iic3_funcs),
	RCW_PMUX_GROUP("iic5", 12, 9, 3, lx2160a_iic5_funcs),
	RCW_PMUX_GROUP("iic6", 12, 12, 3, lx2160a_iic6_funcs),
	RCW_PMUX_GROUP("xspi1-data74", 12, 15, 3, lx2160a_xspi1_funcs),
	RCW_PMUX_GROUP("xspi1-data30", 12, 18, 3, lx2160a_xspi1_funcs),
	RCW_PMUX_GROUP("xspi1-base", 12, 21, 3, lx2160a_xspi1_funcs),
	RCW_PMUX_GROUP("sdhc1-base", 12, 24, 3, lx2160a_sdhc1_base_funcs),
	RCW_PMUX_GROUP("sdhc1-dir", 12, 27, 3, lx2160a_sdhc1_dir_funcs),
	/* RCWSR13 */
	RCW_PMUX_GROUP("sdhc2-data74", 13, 0, 3, lx2160a_sdhc2_data74_funcs),
	RCW_PMUX_GROUP("sdhc2-base", 13, 3, 3, lx2160a_sdhc2_base_funcs),
	RCW_PMUX_GROUP("uart1-soutsin", 13, 6, 3, lx2160a_uart_funcs),
	RCW_PMUX_GROUP("uart1-rtscts", 13, 9, 3, lx2160a_uart1_rtscts_funcs),
	RCW_PMUX_GROUP("uart2-soutsin", 13, 12, 3, lx2160a_uart_funcs),
	RCW_PMUX_GROUP("uart2-rtscts", 13, 15, 3, lx2160a_uart2_rtscts_funcs),
	RCW_PMUX_GROUP("irq11-8", 13, 18, 3, lx2160a_irq_funcs),
	RCW_PMUX_GROUP("irq7-4", 13, 21, 3, lx2160a_irq_flextimer_funcs),
	RCW_PMUX_GROUP("irq3-0", 13, 24, 3, lx2160a_irq_flextimer_funcs),
	RCW_PMUX_GROUP("evt4-3", 13, 27, 3, lx2160a_evt_funcs),
	/* RCWSR14 */
	RCW_PMUX_GROUP("evt2-0", 14, 0, 3, lx2160a_evt_funcs),
	RCW_PMUX_GROUP("clk-out", 14, 3, 3, lx2160a_clk_out_funcs),
	RCW_PMUX_GROUP("asleep", 14, 6, 3, fsl_lsch32_asleep_funcs),
	RCW_PMUX_GROUP("reset-req", 14, 9, 1, fsl_lsch32_reset_req_funcs),
	RCW_PMUX_GROUP("iic1", 14, 10, 1, fsl_lsch32_iic1_funcs),
	/* RCWSR27 */
	RCW_PMUX_GROUP("ec1", 27, 0, 2, lx2160a_ec1_funcs),
	RCW_PMUX_GROUP("ec2", 27, 2, 2, lx2160a_ec2_funcs),
	RCW_PMUX_GROUP("usb-ext", 27, 4, 1, lx2160a_usb_ext_funcs),
	RCW_PMUX_GROUP("gtx-clk", 27, 5, 1, lx2160a_gtx_clk_funcs),
	RCW_PMUX_GROUP("sdhc1-ds", 27, 6, 2, lx2160a_sdhc1_ds_funcs),
};

static const struct rcw_pmux_soc lx2160a_soc = {
	.groups = lx2160a_groups,
	.ngroups = ARRAY_SIZE(lx2160a_groups),
};

/* LS1028A: RCWSR12..14 and RCWSR27 */
static const struct rcw_pmux_func ls1028a_iic3_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("can", 2), RCW_PMUX_FUNC("lpuart", 3),
	RCW_PMUX_FUNC("flextimer", 4), RCW_PMUX_FUNC("evt", 6),
};

static const struct rcw_pmux_func ls1028a_iic5_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("sdhc-clk", 2), RCW_PMUX_FUNC("evt", 6),
};

static const struct rcw_pmux_func ls1028a_iic6_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("sdhc-clk", 2), RCW_PMUX_FUNC("usb", 3),
	RCW_PMUX_FUNC("evt", 6),
};

static const struct rcw_pmux_func ls1028a_xspi1_data_funcs[] = {
	RCW_PMUX_FUNC("flexspi", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("lpuart", 3), RCW_PMUX_FUNC("flextimer", 4),
};

static const struct rcw_pmux_func ls1028a_xspi1_base_funcs[] = {
	RCW_PMUX_FUNC("flexspi", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 4),
};

static const struct rcw_pmux_func ls1028a_sdhc1_base_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 2), RCW_PMUX_FUNC("sai-tx", 3),
	RCW_PMUX_FUNC("sai-rx", 4),
};

static const struct rcw_pmux_func ls1028a_sdhc2_data74_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("i2c", 2), RCW_PMUX_FUNC("lpuart", 3),
	RCW_PMUX_FUNC("flextimer", 4), RCW_PMUX_FUNC("flexspi", 6),
};

static const struct rcw_pmux_func ls1028a_sdhc2_base_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 2), RCW_PMUX_FUNC("lpuart", 3),
	RCW_PMUX_FUNC("flextimer", 4), RCW_PMUX_FUNC("flexspi", 6),
};

static const struct rcw_pmux_func ls1028a_uart_funcs[] = {
	RCW_PMUX_FUNC("uart", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("lpuart", 3), RCW_PMUX_FUNC("flextimer", 4),
};

static const struct rcw_pmux_func ls1028a_clk_out_funcs[] = {
	RCW_PMUX_FUNC("clk-out", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 2), RCW_PMUX_FUNC("evt", 6),
};

static const struct rcw_pmux_func ls1028a_ec1_funcs[] = {
	RCW_PMUX_FUNC("rgmii", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("sai-tx", 2), RCW_PMUX_FUNC("sai-rx", 3),
	RCW_PMUX_FUNC("flextimer", 4), RCW_PMUX_FUNC("ec1-1588", 5),
};

static const struct rcw_pmux_func ls1028a_usb_funcs[] = {
	RCW_PMUX_FUNC("usb", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 2),
};

static const struct rcw_pmux_func ls1028a_spi3_funcs[] = {
	RCW_PMUX_FUNC("spi", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("ec1-1588", 2), RCW_PMUX_FUNC("switch-1588", 3),
};

static const struct rcw_pmux_func ls1028a_gtx_clk125_funcs[] = {
	RCW_PMUX_FUNC("gtx-clk", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("ec1-1722", 2),
};

static const struct rcw_pmux_group ls1028a_groups[] = {
	/* RCWSR12 */
	RCW_PMUX_GROUP("iic2", 12, 0, 3, fsl_lsch32_iic2_funcs),
	RCW_PMUX_GROUP("iic3", 12, 3, 3, ls1028a_iic3_funcs),
	RCW_PMUX_GROUP("iic4", 12, 6, 3, ls1028a_iic3_funcs),
	RCW_PMUX_GROUP("iic5", 12, 9, 3, ls1028a_iic5_funcs),
	RCW_PMUX_GROUP("iic6", 12, 12, 3, ls1028a_iic6_funcs),
	RCW_PMUX_GROUP("xspi1-data74", 12, 15, 3, ls1028a_xspi1_data_funcs),
	RCW_PMUX_GROUP("xspi1-data30", 12, 18, 3, ls1028a_xspi1_data_funcs),
	RCW_PMUX_GROUP("xspi1-base", 12, 21, 3, ls1028a_xspi1_base_funcs),
	RCW_PMUX_GROUP("sdhc1-base", 12, 24, 3, ls1028a_sdhc1_base_funcs),
	/* RCWSR13 */
	RCW_PMUX_GROUP("sdhc2-data74", 13, 0, 3, ls1028a_sdhc2_data74_funcs),
	RCW_PMUX_GROUP("sdhc2-base", 13, 3, 3, ls1028a_sdhc2_base_funcs),
	RCW_PMUX_GROUP("uart1-soutsin", 13, 6, 3, ls1028a_uart_funcs),
	RCW_PMUX_GROUP("uart2-soutsin", 13, 12, 3, ls1028a_uart_funcs),
	/* RCWSR14 */
	RCW_PMUX_GROUP("clk-out", 14, 3, 3, ls1028a_clk_out_funcs),
	RCW_PMUX_GROUP("asleep", 14, 6, 3, fsl_lsch32_asleep_funcs),
	RCW_PMUX_GROUP("reset-req", 14, 9, 1, fsl_lsch32_reset_req_funcs),
	RCW_PMUX_GROUP("iic1", 14, 10, 1, fsl_lsch32_iic1_funcs),
	/* RCWSR27 */
	RCW_PMUX_GROUP("ec1-sai4-5", 27, 1, 3, ls1028a_ec1_funcs),
	RCW_PMUX_GROUP("ec1-sai3-6", 27, 4, 3, ls1028a_ec1_funcs),
	RCW_PMUX_GROUP("usb-drvvbus", 27, 8, 2, ls1028a_usb_funcs),
	RCW_PMUX_GROUP("usb-pwrfault", 27, 10, 2, ls1028a_usb_funcs),
	RCW_PMUX_GROUP("spi3", 27, 26, 2, ls1028a_spi3_funcs),
	RCW_PMUX_GROUP("gtx-clk125", 27, 28, 2, ls1028a_gtx_clk125_funcs),
};

static const struct rcw_pmux_soc ls1028a_soc = {
	.groups = ls1028a_groups,
	.ngroups = ARRAY_SIZE(ls1028a_groups),
};

/*
 * Chassis 2 (LS1043A, LS1046A): RCWSR12..14
 *
 * Most groups are muxed by a BASE field and an EXT one. The functions which
 * the EXT field selects only set it, the BASE field decides on the pads that
 * EXT does not take over. The functions of the BASE field need EXT to be 0.
 * The values and masks below are relative to the shift of the group.
 */

/* UART group: UART_BASE at 0, UART_EXT at 12 */
#define LSCH2_UART_BASE		GENMASK(2, 0)
#define LSCH2_UART_EXT		GENMASK(14, 12)
#define LSCH2_UART_ALL		(LSCH2_UART_BASE | LSCH2_UART_EXT)
static const struct rcw_pmux_func fsl_lsch2_uart_funcs[] = {
	RCW_PMUX_FUNC_MASK("gpio", 0, LSCH2_UART_ALL),
	RCW_PMUX_FUNC_MASK("uart1", 3, LSCH2_UART_ALL),
	RCW_PMUX_FUNC_MASK("uart1-rtscts", 4, LSCH2_UART_ALL),
	RCW_PMUX_FUNC_MASK("uart1-2", 5, LSCH2_UART_ALL),
	RCW_PMUX_FUNC_MASK("uart1-2-rtscts", 6, LSCH2_UART_ALL),
	RCW_PMUX_FUNC_MASK("uart1-4", 7, LSCH2_UART_ALL),
	RCW_PMUX_FUNC_MASK("lpuart1-2", 1 << 12, LSCH2_UART_EXT),
	RCW_PMUX_FUNC_MASK("lpuart1-2-4", 2 << 12, LSCH2_UART_EXT),
	RCW_PMUX_FUNC_MASK("flextimer", 3 << 12, LSCH2_UART_EXT),
};

/* IRQ group: IRQ_BASE at 0, one bit per IRQ3..IRQ11, and IRQ_EXT at 22 */
#define LSCH2_IRQ_BASE		GENMASK(8, 0)
#define LSCH2_IRQ_EXT		GENMASK(24, 22)
#define LSCH2_IRQ_ALL		(LSCH2_IRQ_BASE | LSCH2_IRQ_EXT)

/* SPI group: SPI_BASE at 0, SPI_EXT at 21 */
#define LSCH2_SPI_BASE		GENMASK(1, 0)
#define LSCH2_SPI_EXT		GENMASK(23, 21)
#define LSCH2_SPI_ALL		(LSCH2_SPI_BASE | LSCH2_SPI_EXT)
static const struct rcw_pmux_func fsl_lsch2_spi_funcs[] = {
	RCW_PMUX_FUNC_MASK("spi", 0, LSCH2_SPI_ALL),
	RCW_PMUX_FUNC_MASK("sdhc", 1, LSCH2_SPI_ALL),
	RCW_PMUX_FUNC_MASK("gpio", 2, LSCH2_SPI_ALL),
	RCW_PMUX_FUNC_MASK("sdhc-dir", 3, LSCH2_SPI_ALL),
	RCW_PMUX_FUNC_MASK("sdhc-clk-sync", 1 << 21, LSCH2_SPI_EXT),
	RCW_PMUX_FUNC_MASK("spi-no-sck", 2 << 21, LSCH2_SPI_EXT),
};

/* SDHC group: SDHC_BASE at 0, SDHC_EXT at 6 */
#define LSCH2_SDHC_BASE		BIT(0)
#define LSCH2_SDHC_EXT		GENMASK(8, 6)
#define LSCH2_SDHC_ALL		(LSCH2_SDHC_BASE | LSCH2_SDHC_EXT)
static const struct rcw_pmux_func fsl_lsch2_sdhc_funcs[] = {
	RCW_PMUX_FUNC_MASK("sdhc", 0, LSCH2_SDHC_ALL),
	RCW_PMUX_FUNC_MASK("gpio", 1, LSCH2_SDHC_ALL),
	RCW_PMUX_FUNC_MASK("lpuart2-3", 1 << 6, LSCH2_SDHC_EXT),
	RCW_PMUX_FUNC_MASK("lpuart3-5-6", 2 << 6, LSCH2_SDHC_EXT),
	RCW_PMUX_FUNC_MASK("flextimer", 3 << 6, LSCH2_SDHC_EXT),
};

static const struct rcw_pmux_func fsl_lsch2_asleep_funcs[] = {
	RCW_PMUX_FUNC("asleep", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func fsl_lsch2_rtc_funcs[] = {
	RCW_PMUX_FUNC("rtc", 0), RCW_PMUX_FUNC("gpio", 1),
};

/* IFC group A: IFC_GRP_A_BASE at 0, IFC_GRP_A_EXT at 27 */
#define LSCH2_IFC_A_BASE	GENMASK(1, 0)
#define LSCH2_IFC_A_EXT		GENMASK(29, 27)
#define LSCH2_IFC_A_ALL		(LSCH2_IFC_A_BASE | LSCH2_IFC_A_EXT)
static const struct rcw_pmux_func fsl_lsch2_ifc_a_funcs[] = {
	RCW_PMUX_FUNC_MASK("ifc", 0, LSCH2_IFC_A_ALL),
	RCW_PMUX_FUNC_MASK("gpio", 1, LSCH2_IFC_A_ALL),
	RCW_PMUX_FUNC_MASK("ifc-rb", 2, LSCH2_IFC_A_ALL),
	RCW_PMUX_FUNC_MASK("qspi", 1 << 27, LSCH2_IFC_A_EXT),
	RCW_PMUX_FUNC_MASK("flextimer", 2 << 27, LSCH2_IFC_A_EXT),
	RCW_PMUX_FUNC_MASK("ifc-cs", 4 << 27, LSCH2_IFC_A_EXT),
};

/* IFC group D: IFC_GRP_D_BASE at 0, IFC_GRP_D_EXT at 12 */
#define LSCH2_IFC_D_BASE	BIT(0)
#define LSCH2_IFC_D_EXT		GENMASK(14, 12)
#define LSCH2_IFC_D_ALL		(LSCH2_IFC_D_BASE | LSCH2_IFC_D_EXT)
static const struct rcw_pmux_func fsl_lsch2_ifc_d_funcs[] = {
	RCW_PMUX_FUNC_MASK("ifc", 0, LSCH2_IFC_D_ALL),
	RCW_PMUX_FUNC_MASK("gpio", 1, LSCH2_IFC_D_ALL),
	RCW_PMUX_FUNC_MASK("qspi", 1 << 12, LSCH2_IFC_D_EXT),
	RCW_PMUX_FUNC_MASK("flextimer", 2 << 12, LSCH2_IFC_D_EXT),
};

/* IFC group E1: IFC_GRP_E1_BASE at 0, IFC_GRP_E1_EXT at 7 */
#define LSCH2_IFC_E1_BASE	BIT(0)
#define LSCH2_IFC_E1_EXT	GENMASK(9, 7)
#define LSCH2_IFC_E1_ALL	(LSCH2_IFC_E1_BASE | LSCH2_IFC_E1_EXT)
static const struct rcw_pmux_func fsl_lsch2_ifc_e1_funcs[] = {
	RCW_PMUX_FUNC_MASK("ifc", 0, LSCH2_IFC_E1_ALL),
	RCW_PMUX_FUNC_MASK("gpio", 1, LSCH2_IFC_E1_ALL),
	RCW_PMUX_FUNC_MASK("qspi", 1 << 7, LSCH2_IFC_E1_EXT),
	RCW_PMUX_FUNC_MASK("flextimer", 2 << 7, LSCH2_IFC_E1_EXT),
};

/* IFC group F: IFC_A_22_24 at 0, IFC_GRP_F_EXT at 14 */
#define LSCH2_IFC_F_WP		BIT(0)
#define LSCH2_IFC_F_EXT		GENMASK(16, 14)
#define LSCH2_IFC_F_ALL		(LSCH2_IFC_F_WP | LSCH2_IFC_F_EXT)
static const struct rcw_pmux_func fsl_lsch2_ifc_f_funcs[] = {
	RCW_PMUX_FUNC_MASK("ifc", 0, LSCH2_IFC_F_ALL),
	RCW_PMUX_FUNC_MASK("ifc-wp", 1, LSCH2_IFC_F_ALL),
	RCW_PMUX_FUNC_MASK("qspi", 1 << 14, LSCH2_IFC_F_EXT),
};

static const struct rcw_pmux_func fsl_lsch2_ec1_funcs[] = {
	RCW_PMUX_FUNC("rgmii", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 5),
};

static const struct rcw_pmux_func fsl_lsch2_ec2_funcs[] = {
	RCW_PMUX_FUNC("rgmii", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("tsec-1588", 2), RCW_PMUX_FUNC("flextimer", 5),
};

static const struct rcw_pmux_func fsl_lsch2_mdio_funcs[] = {
	RCW_PMUX_FUNC("mdio", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func fsl_lsch2_usb_funcs[] = {
	RCW_PMUX_FUNC("usb", 0), RCW_PMUX_FUNC("gpio", 1),
};

/* LS1046A: RCWSR12..14 */
static const struct rcw_pmux_func ls1046a_irq_funcs[] = {
	RCW_PMUX_FUNC_MASK("irq", 0, LSCH2_IRQ_ALL),
	RCW_PMUX_FUNC_MASK("gpio", LSCH2_IRQ_BASE, LSCH2_IRQ_ALL),
	RCW_PMUX_FUNC_MASK("flextimer", 3 << 22, LSCH2_IRQ_EXT),
};

static const struct rcw_pmux_func ls1046a_iic2_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("sdhc", 1),
	RCW_PMUX_FUNC("gpio", 2), RCW_PMUX_FUNC("flextimer", 3),
};

static const struct rcw_pmux_group ls1046a_groups[] = {
	/* RCWSR12 */
	RCW_PMUX_GROUP("uart", 12, 15, 3, fsl_lsch2_uart_funcs),
	RCW_PMUX_GROUP("irq", 12, 2, 9, ls1046a_irq_funcs),
	RCW_PMUX_GROUP("spi", 12, 0, 2, fsl_lsch2_spi_funcs),
	RCW_PMUX_GROUP("sdhc", 12, 12, 1, fsl_lsch2_sdhc_funcs),
	RCW_PMUX_GROUP("asleep", 12, 14, 1, fsl_lsch2_asleep_funcs),
	RCW_PMUX_GROUP("rtc", 12, 13, 1, fsl_lsch2_rtc_funcs),
	/* RCWSR13 */
	RCW_PMUX_GROUP("ifc-a", 13, 2, 2, fsl_lsch2_ifc_a_funcs),
	RCW_PMUX_GROUP("ifc-d", 13, 8, 1, fsl_lsch2_ifc_d_funcs),
	RCW_PMUX_GROUP("ifc-e1", 13, 10, 1, fsl_lsch2_ifc_e1_funcs),
	RCW_PMUX_GROUP("ifc-f", 13, 0, 1, fsl_lsch2_ifc_f_funcs),
	/* RCWSR14 */
	RCW_PMUX_GROUP("ec1", 14, 29, 3, fsl_lsch2_ec1_funcs),
	RCW_PMUX_GROUP("ec2", 14, 26, 3, fsl_lsch2_ec2_funcs),
	RCW_PMUX_GROUP("em1", 14, 22, 1, fsl_lsch2_mdio_funcs),
	RCW_PMUX_GROUP("em2", 14, 21, 1, fsl_lsch2_mdio_funcs),
	RCW_PMUX_GROUP("usb-drvvbus", 14, 18, 1, fsl_lsch2_usb_funcs),
	RCW_PMUX_GROUP("usb-pwrfault", 14, 17, 1, fsl_lsch2_usb_funcs),
	RCW_PMUX_GROUP("iic2", 14, 0, 3, ls1046a_iic2_funcs),
};

static const struct rcw_pmux_soc ls1046a_soc = {
	.groups = ls1046a_groups,
	.ngroups = ARRAY_SIZE(ls1046a_groups),
};

struct rcw_pmux {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct pinctrl_desc desc;
	const struct rcw_pmux_soc *soc;
	struct pinctrl_pin_desc *pins;
};

static u32 rcw_pmux_func_mask(const struct rcw_pmux_group *group,
			      const struct rcw_pmux_func *func)
{
	u32 mask = func->mask ?: GENMASK(group->width - 1, 0);

	return mask << group->shift;
}

static const struct rcw_pmux_func *
rcw_pmux_find_func(const struct rcw_pmux_group *group, const char *name)
{
	unsigned int i;

	for (i = 0; i < group->nfuncs; i++) {
		if (!strcmp(group->funcs[i].name, name))
			return &group->funcs[i];
	}

	return NULL;
}

static void rcw_pmux_pin_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *s, unsigned int pin)
{
	struct rcw_pmux *pmux = pinctrl_dev_get_drvdata(pctldev);
	const struct rcw_pmux_group *group = &pmux->soc->groups[pin];
	unsigned int i;
	u32 val;

	if (fsl_guts_pmux_read(group->rcwsr, &val))
		return;

	for (i = 0; i < group->nfuncs; i++) {
		const struct rcw_pmux_func *func = &group->funcs[i];

		if ((val & rcw_pmux_func_mask(group, func)) ==
		    func->val << group->shift) {
			seq_puts(s, func->name);
			return;
		}
	}
	seq_printf(s, "0x%x", val >> group->shift);
}

static const struct pinctrl_ops rcw_pmux_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.pin_dbg_show = rcw_pmux_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int rcw_pmux_set_mux(struct pinctrl_dev *pctldev,
			    unsigned int fsel, unsigned int gsel)
{
	struct rcw_pmux *pmux = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *func;
	const struct rcw_pmux_group *group;
	const struct rcw_pmux_func *mux;
	const struct group_desc *grp;

	func = pinmux_generic_get_function(pctldev, fsel);
	grp = pinctrl_generic_get_group(pctldev, gsel);
	if (!func || !grp)
		return -EINVAL;
	group = grp->data;

	mux = rcw_pmux_find_func(group, func->func->name);
	if (!mux) {
		dev_err(pmux->dev, "%s has no %s function\n",
			group->name, func->func->name);
		return -EINVAL;
	}

	return fsl_guts_pmux_override(group->rcwsr, mux->val << group->shift,
				      rcw_pmux_func_mask(group, mux));
}

static const struct pinmux_ops rcw_pmux_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = rcw_pmux_set_mux,
	.strict = true,
};

static int rcw_pmux_add_functions(struct rcw_pmux *pmux)
{
	const struct rcw_pmux_soc *soc = pmux->soc;
	const struct rcw_pmux_group *group;
	const char **groups;
	unsigned int i, j, k, n;
	int ret;

	for (i = 0; i < soc->ngroups; i++) {
		group = &soc->groups[i];
		for (j = 0; j < group->nfuncs; j++) {
			const char *name = group->funcs[j].name;

			/* already added from an earlier group? */
			for (k = 0; k < i; k++) {
				if (rcw_pmux_find_func(&soc->groups[k], name))
					break;
			}
			if (k < i)
				continue;

			groups = devm_kcalloc(pmux->dev, soc->ngroups,
					      sizeof(*groups), GFP_KERNEL);
			if (!groups)
				return -ENOMEM;
			n = 0;
			for (k = i; k < soc->ngroups; k++) {
				if (rcw_pmux_find_func(&soc->groups[k], name))
					groups[n++] = soc->groups[k].name;
			}
			ret = pinmux_generic_add_function(pmux->pctl, name,
							  groups, n, NULL);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int rcw_pmux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct rcw_pmux_soc *soc;
	struct rcw_pmux *pmux;
	unsigned int i;
	u32 val;
	int ret;

	/* The override needs the DCSR view of DCFG, mapped by fsl-guts */
	if (!fsl_guts_pmux_override_supported())
		return dev_err_probe(dev, -ENODEV,
				     "no RCW override through fsl-guts: the dcfg node needs its DCSR reg\n");

	soc = device_get_match_data(dev);
	if (!soc)
		return -ENODEV;

	pmux = devm_kzalloc(dev, sizeof(*pmux), GFP_KERNEL);
	if (!pmux)
		return -ENOMEM;
	pmux->dev = dev;
	pmux->soc = soc;

	pmux->pins = devm_kcalloc(dev, soc->ngroups, sizeof(*pmux->pins),
				  GFP_KERNEL);
	if (!pmux->pins)
		return -ENOMEM;

	for (i = 0; i < soc->ngroups; i++) {
		pmux->pins[i].number = i;
		pmux->pins[i].name = soc->groups[i].name;
	}
	pmux->desc.name = dev_name(dev);
	pmux->desc.pins = pmux->pins;
	pmux->desc.npins = soc->ngroups;
	pmux->desc.pctlops = &rcw_pmux_pctlops;
	pmux->desc.pmxops = &rcw_pmux_pmxops;
	pmux->desc.owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(dev, &pmux->desc, pmux,
					     &pmux->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "pinctrl registration failed\n");

	for (i = 0; i < soc->ngroups; i++) {
		ret = pinctrl_generic_add_group(pmux->pctl, soc->groups[i].name,
						&pmux->pins[i].number, 1,
						(void *)&soc->groups[i]);
		if (ret < 0)
			return ret;
	}

	ret = rcw_pmux_add_functions(pmux);
	if (ret)
		return ret;

	for (i = 0; i < soc->ngroups; i++) {
		u8 rcwsr = soc->groups[i].rcwsr;

		if (i && rcwsr == soc->groups[i - 1].rcwsr)
			continue;
		if (!fsl_guts_pmux_read(rcwsr, &val))
			dev_dbg(dev, "pad muxes of RCWSR%u = 0x%08x\n", rcwsr,
				val);
	}

	return pinctrl_enable(pmux->pctl);
}

static const struct of_device_id rcw_pmux_of_match[] = {
	{ .compatible = "fsl,ls1028a-rcw-pinmux", .data = &ls1028a_soc },
	{ .compatible = "fsl,ls1046a-rcw-pinmux", .data = &ls1046a_soc },
	{ .compatible = "fsl,lx2160a-rcw-pinmux", .data = &lx2160a_soc },
	{ }
};
MODULE_DEVICE_TABLE(of, rcw_pmux_of_match);

static struct platform_driver rcw_pmux_driver = {
	.driver = {
		.name = "layerscape-rcw-pinmux",
		.of_match_table = rcw_pmux_of_match,
	},
	.probe = rcw_pmux_probe,
};
module_platform_driver(rcw_pmux_driver);

MODULE_AUTHOR("Vincent Jardin <vjardin@free.fr>");
MODULE_DESCRIPTION("NXP Layerscape RCW pad-mux driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("FSL_GUTS");
