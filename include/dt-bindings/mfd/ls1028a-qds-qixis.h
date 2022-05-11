/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2022 NXP */

#ifndef __DT_BINDINGS_LS1028A_QDS_QIXIS_H
#define __DT_BINDINGS_LS1028A_QDS_QIXIS_H

/* QIXIS register map for LS1028A-QDS board */
#define QIXIS_ID			0x0
#define QIXIS_BRDVER			0x1
#define QIXIS_QVER			0x2
#define QIXIS_MODEL			0x3
#define QIXIS_IMAGE_TAG			0x4
#define QIXIS_CTL			0x5
#define QIXIS_AUX1			0x6
#define QIXIS_STAT_DUT			0x8
#define QIXIS_STAT_SYS			0x9
#define QIXIS_ALARM			0xa
#define QIXIS_STAT_PRES1		0xb
#define QIXIS_STAT_PRES2		0xc
#define QIXIS_RCW_CTL			0xd
#define QIXIS_LED_CTL			0xe
#define QIXIS_QMODE			0xf
#define QIXIS_RCFG_CTL			0x10
#define QIXIS_RCFG_STAT			0x11
#define QIXIS_DCM_ADDR			0x12
#define QIXIS_DCM_DATA			0x13
#define QIXIS_DCM_CMD			0x14
#define QIXIS_DCM_MSG			0x15
#define QIXIS_GMS_DEBUG_CTL		0x16
#define QIXIS_GMS_DEBUG_DATA		0x17
#define QIXIS_MSG_ACK			0x18
#define QIXIS_SDHC1			0x1a
#define QIXIS_SDHC2			0x1b
#define QIXIS_LOS_STAT			0x1d
#define QIXIS_USB_CTL			0x1e
#define QIXIS_WATCHDOG			0x1f
#define QIXIS_PWR_CTL1			0x20
#define QIXIS_PWR_CTL2			0x21
#define QIXIS_PWR_EVENT			0x22
#define QIXIS_PWR_MSTAT			0x24
#define QIXIS_PWR_STAT1			0x25
#define QIXIS_PWR_STAT2			0x26
#define QIXIS_PWR_STAT3			0x27
#define QIXIS_CLK_SPD1			0x30
#define QIXIS_BLK_CTL			0x38
#define QIXIS_BLK_STAT			0x39
#define QIXIS_RST_CTL			0x40
#define QIXIS_RST_STAT			0x41
#define QIXIS_RST_REASON		0x42
#define QIXIS_RST_FORCE1		0x43
#define QIXIS_RST_FORCE2		0x44
#define QIXIS_RST_FORCE3		0x45
#define QIXIS_RST_TRACK			0x48
#define QIXIS_RST_MASK1			0x4b
#define QIXIS_RST_MASK2			0x4c
#define QIXIS_RST_MASK3			0x4d
#define QIXIS_BRDCFG(n)			(0x50 + (n))
#define QIXIS_DUTCFG(n)			(0x60 + (n))
#define QIXIS_POST_CTL			0x78
#define QIXIS_POST_STAT			0x79
#define QIXIS_POST_DATA(n)		(0x7a + (n))
#define QIXIS_POST_CMD(n)		(0x7c + (n))
#define QIXIS_GPIO1_IO(n)		(0x80 + (n))
#define QIXIS_GPIO1_DIR(n)		(0x84 + (n))
#define QIXIS_IRQSTAT0			0x90
#define QIXIS_IRQSTAT1			0x91
#define QIXIS_IRQSTAT2			0x92
#define QIXIS_IRQDRV5			0x9d
#define QIXIS_TRIG_SRC(n)		(0xa0 + (n))
#define QIXIS_TRIG_DEST(n)		(0xa4 + (n))
#define QIXIS_TRIG_STAT			0xa8
#define QIXIS_TRIG_CTL			0xaa
#define QIXIS_TRIG_DLY1			0xac
#define QIXIS_TRIG_DLY2			0xad
#define QIXIS_TRIG_DLY3			0xae
#define QIXIS_TRIG_DLY4			0xaf
#define QIXIS_IEEE1588_TTR		0xb0
#define QIXIS_IEEE1588_TPR		0xb1
#define QIXIS_IEEE1588_TAR		0xb2
#define QIXIS_TEST_PLL_CFG(n)		(0xb8 + (n))
#define QIXIS_CLK_FREQA(n)		(0xc0 + (n))
#define QIXIS_CM_STAT			0xcc
#define QIXIS_CM_CTL			0xcd
#define QIXIS_I2C_FAULT_CTL		0xd0
#define QIXIS_I2C_FAULT_STAT		0xd1
#define QIXIS_SSPI_CTL			0xd4
#define QIXIS_SSPI_STAT			0xd5
#define QIXIS_SSPI_DATA			0xd6
#define QIXIS_CMSA			0xd8
#define QIXIS_CMSD			0xd9
#define QIXIS_SWS_CTL			0xdc
#define QIXIS_SWS_STAT			0xdd
#define QIXIS_AUX2(n)			(0xe0 + (n))
#define QIXIS_AUX_SRAM_ADDR		0xee
#define QIXIS_AUX_SRAM_DATA		0xef
#define QIXIS_BLK_OPS			0xf0
#define QIXIS_BLK_ADDR(n)		(0xf1 + (n))
#define QIXIS_BLK_DATA(n)		(0xf1 + (n))
#define QIXIS_BLK_SRAM			0xf8
#define QIXIS_BLK_INFO(n)		(0xf9 + (n))
#define QIXIS_BLK_TCNT			0xfe
#define QIXIS_BLK_ERRS			0xff
#define QIXIS_RCW(n)			(0x100 + (n))
#define QIXIS_QXCNT			0xf00
#define QIXIS_QXCTL			0xf01
#define QIXIS_QXSTAT			0xf02
#define QIXIS_QXDEVA			0xf03
#define QIXIS_QXBC			0xf04
#define QIXIS_QXDATA(n)			(0xf05 + (n))

#endif /* __DT_BINDINGS_LS1028A_QDS_QIXIS_H */
