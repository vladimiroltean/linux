// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/of_net.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/of_mdio.h>
#include <net/switchdev.h>

#include <soc/mscc/ocelot_qsys.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot.h>
#include "ocelot.h"

#define IFH_EXTRACT_BITFIELD64(x, o, w) (((x) >> (o)) & GENMASK_ULL((w) - 1, 0))

#define PCI_DEVICE_ID_OCELOT			0xEEF0

/* Switch register block BAR */
#define OCELOT_SWITCH_BAR			4

static const u32 ocelot_ana_regmap[] = {
	REG(ANA_ADVLEARN,				0x0089a0),
	REG(ANA_VLANMASK,				0x0089a4),
	REG_RESERVED(ANA_PORT_B_DOMAIN),
	REG(ANA_ANAGEFIL,				0x0089ac),
	REG(ANA_ANEVENTS,				0x0089b0),
	REG(ANA_STORMLIMIT_BURST,			0x0089b4),
	REG(ANA_STORMLIMIT_CFG,				0x0089b8),
	REG(ANA_ISOLATED_PORTS,				0x0089c8),
	REG(ANA_COMMUNITY_PORTS,			0x0089cc),
	REG(ANA_AUTOAGE,				0x0089d0),
	REG(ANA_MACTOPTIONS,				0x0089d4),
	REG(ANA_LEARNDISC,				0x0089d8),
	REG(ANA_AGENCTRL,				0x0089dc),
	REG(ANA_MIRRORPORTS,				0x0089e0),
	REG(ANA_EMIRRORPORTS,				0x0089e4),
	REG(ANA_FLOODING,				0x0089e8),
	REG(ANA_FLOODING_IPMC,				0x008a08),
	REG(ANA_SFLOW_CFG,				0x008a0c),
	REG(ANA_PORT_MODE,				0x008a28),
	REG(ANA_CUT_THRU_CFG,				0x008a48),
	REG(ANA_PGID_PGID,				0x008400),
	REG(ANA_TABLES_ANMOVED,				0x007f1c),
	REG(ANA_TABLES_MACHDATA,			0x007f20),
	REG(ANA_TABLES_MACLDATA,			0x007f24),
	REG(ANA_TABLES_STREAMDATA,			0x007f28),
	REG(ANA_TABLES_MACACCESS,			0x007f2c),
	REG(ANA_TABLES_MACTINDX,			0x007f30),
	REG(ANA_TABLES_VLANACCESS,			0x007f34),
	REG(ANA_TABLES_VLANTIDX,			0x007f38),
	REG(ANA_TABLES_ISDXACCESS,			0x007f3c),
	REG(ANA_TABLES_ISDXTIDX,			0x007f40),
	REG(ANA_TABLES_ENTRYLIM,			0x007f00),
	REG(ANA_TABLES_PTP_ID_HIGH,			0x007f44),
	REG(ANA_TABLES_PTP_ID_LOW,			0x007f48),
	REG(ANA_TABLES_STREAMACCESS,			0x007f4c),
	REG(ANA_TABLES_STREAMTIDX,			0x007f50),
	REG(ANA_TABLES_SEQ_HISTORY,			0x007f54),
	REG(ANA_TABLES_SEQ_MASK,			0x007f58),
	REG(ANA_TABLES_SFID_MASK,			0x007f5c),
	REG(ANA_TABLES_SFIDACCESS,			0x007f60),
	REG(ANA_TABLES_SFIDTIDX,			0x007f64),
	REG(ANA_MSTI_STATE,				0x008600),
	REG(ANA_OAM_UPM_LM_CNT,				0x008000),
	REG(ANA_SG_ACCESS_CTRL,				0x008a64),
	REG(ANA_SG_CONFIG_REG_1,			0x007fb0),
	REG(ANA_SG_CONFIG_REG_2,			0x007fb4),
	REG(ANA_SG_CONFIG_REG_3,			0x007fb8),
	REG(ANA_SG_CONFIG_REG_4,			0x007fbc),
	REG(ANA_SG_CONFIG_REG_5,			0x007fc0),
	REG(ANA_SG_GCL_GS_CONFIG,			0x007f80),
	REG(ANA_SG_GCL_TI_CONFIG,			0x007f90),
	REG(ANA_SG_STATUS_REG_1,			0x008980),
	REG(ANA_SG_STATUS_REG_2,			0x008984),
	REG(ANA_SG_STATUS_REG_3,			0x008988),
	REG(ANA_PORT_VLAN_CFG,				0x007800),
	REG(ANA_PORT_DROP_CFG,				0x007804),
	REG(ANA_PORT_QOS_CFG,				0x007808),
	REG(ANA_PORT_VCAP_CFG,				0x00780c),
	REG(ANA_PORT_VCAP_S1_KEY_CFG,			0x007810),
	REG(ANA_PORT_VCAP_S2_CFG,			0x00781c),
	REG(ANA_PORT_PCP_DEI_MAP,			0x007820),
	REG(ANA_PORT_CPU_FWD_CFG,			0x007860),
	REG(ANA_PORT_CPU_FWD_BPDU_CFG,			0x007864),
	REG(ANA_PORT_CPU_FWD_GARP_CFG,			0x007868),
	REG(ANA_PORT_CPU_FWD_CCM_CFG,			0x00786c),
	REG(ANA_PORT_PORT_CFG,				0x007870),
	REG(ANA_PORT_POL_CFG,				0x007874),
	REG(ANA_PORT_PTP_CFG,				0x007878),
	REG(ANA_PORT_PTP_DLY1_CFG,			0x00787c),
	REG(ANA_PORT_PTP_DLY2_CFG,			0x007880),
	REG(ANA_PORT_SFID_CFG,				0x007884),
	REG(ANA_PFC_PFC_CFG,				0x008800),
	REG_RESERVED(ANA_PFC_PFC_TIMER),
	REG_RESERVED(ANA_IPT_OAM_MEP_CFG),
	REG_RESERVED(ANA_IPT_IPT),
	REG_RESERVED(ANA_PPT_PPT),
	REG_RESERVED(ANA_FID_MAP_FID_MAP),
	REG(ANA_AGGR_CFG,				0x008a68),
	REG(ANA_CPUQ_CFG,				0x008a6c),
	REG_RESERVED(ANA_CPUQ_CFG2),
	REG(ANA_CPUQ_8021_CFG,				0x008a74),
	REG(ANA_DSCP_CFG,				0x008ab4),
	REG(ANA_DSCP_REWR_CFG,				0x008bb4),
	REG(ANA_VCAP_RNG_TYPE_CFG,			0x008bf4),
	REG(ANA_VCAP_RNG_VAL_CFG,			0x008c14),
	REG_RESERVED(ANA_VRAP_CFG),
	REG_RESERVED(ANA_VRAP_HDR_DATA),
	REG_RESERVED(ANA_VRAP_HDR_MASK),
	REG(ANA_DISCARD_CFG,				0x008c40),
	REG(ANA_FID_CFG,				0x008c44),
	REG(ANA_POL_PIR_CFG,				0x004000),
	REG(ANA_POL_CIR_CFG,				0x004004),
	REG(ANA_POL_MODE_CFG,				0x004008),
	REG(ANA_POL_PIR_STATE,				0x00400c),
	REG(ANA_POL_CIR_STATE,				0x004010),
	REG_RESERVED(ANA_POL_STATE),
	REG(ANA_POL_FLOWC,				0x008c48),
	REG(ANA_POL_HYST,				0x008cb4),
	REG_RESERVED(ANA_POL_MISC_CFG),
};

static const u32 ocelot_qs_regmap[] = {
	REG(QS_XTR_GRP_CFG,				0x000000),
	REG(QS_XTR_RD,					0x000008),
	REG(QS_XTR_FRM_PRUNING,				0x000010),
	REG(QS_XTR_FLUSH,				0x000018),
	REG(QS_XTR_DATA_PRESENT,			0x00001c),
	REG(QS_XTR_CFG,					0x000020),
	REG(QS_INJ_GRP_CFG,				0x000024),
	REG(QS_INJ_WR,					0x00002c),
	REG(QS_INJ_CTRL,				0x000034),
	REG(QS_INJ_STATUS,				0x00003c),
	REG(QS_INJ_ERR,					0x000040),
	REG_RESERVED(QS_INH_DBG),
};

static const u32 ocelot_qsys_regmap[] = {
	REG(QSYS_PORT_MODE,				0x00f460),
	REG(QSYS_SWITCH_PORT_MODE,			0x00f480),
	REG(QSYS_STAT_CNT_CFG,				0x00f49c),
	REG(QSYS_EEE_CFG,				0x00f4a0),
	REG(QSYS_EEE_THRES,				0x00f4b8),
	REG(QSYS_IGR_NO_SHARING,			0x00f4bc),
	REG(QSYS_EGR_NO_SHARING,			0x00f4c0),
	REG(QSYS_SW_STATUS,				0x00f4c4),
	REG(QSYS_EXT_CPU_CFG,				0x00f4e0),
	REG_RESERVED(QSYS_PAD_CFG),
	REG(QSYS_CPU_GROUP_MAP,				0x00f4e8),
	REG_RESERVED(QSYS_QMAP),
	REG_RESERVED(QSYS_ISDX_SGRP),
	REG_RESERVED(QSYS_TIMED_FRAME_ENTRY),
	REG(QSYS_TFRM_MISC,				0x00f50c),
	REG(QSYS_TFRM_PORT_DLY,				0x00f510),
	REG(QSYS_TFRM_TIMER_CFG_1,			0x00f514),
	REG(QSYS_TFRM_TIMER_CFG_2,			0x00f518),
	REG(QSYS_TFRM_TIMER_CFG_3,			0x00f51c),
	REG(QSYS_TFRM_TIMER_CFG_4,			0x00f520),
	REG(QSYS_TFRM_TIMER_CFG_5,			0x00f524),
	REG(QSYS_TFRM_TIMER_CFG_6,			0x00f528),
	REG(QSYS_TFRM_TIMER_CFG_7,			0x00f52c),
	REG(QSYS_TFRM_TIMER_CFG_8,			0x00f530),
	REG(QSYS_RED_PROFILE,				0x00f534),
	REG(QSYS_RES_QOS_MODE,				0x00f574),
	REG(QSYS_RES_CFG,				0x00c000),
	REG(QSYS_RES_STAT,				0x00c004),
	REG(QSYS_EGR_DROP_MODE,				0x00f578),
	REG(QSYS_EQ_CTRL,				0x00f57c),
	REG_RESERVED(QSYS_EVENTS_CORE),
	REG(QSYS_QMAXSDU_CFG_0,				0x00f584),
	REG(QSYS_QMAXSDU_CFG_1,				0x00f5a0),
	REG(QSYS_QMAXSDU_CFG_2,				0x00f5bc),
	REG(QSYS_QMAXSDU_CFG_3,				0x00f5d8),
	REG(QSYS_QMAXSDU_CFG_4,				0x00f5f4),
	REG(QSYS_QMAXSDU_CFG_5,				0x00f610),
	REG(QSYS_QMAXSDU_CFG_6,				0x00f62c),
	REG(QSYS_QMAXSDU_CFG_7,				0x00f648),
	REG(QSYS_PREEMPTION_CFG,			0x00f664),
	REG(QSYS_CIR_CFG,				0x000000),
	REG(QSYS_EIR_CFG,				0x000004),
	REG(QSYS_SE_CFG,				0x000008),
	REG(QSYS_SE_DWRR_CFG,				0x00000c),
	REG_RESERVED(QSYS_SE_CONNECT),
	REG(QSYS_SE_DLB_SENSE,				0x000040),
	REG(QSYS_CIR_STATE,				0x000044),
	REG(QSYS_EIR_STATE,				0x000048),
	REG_RESERVED(QSYS_SE_STATE),
	REG(QSYS_HSCH_MISC_CFG,				0x00f67c),
	REG(QSYS_TAG_CONFIG,				0x00f680),
	REG(QSYS_TAS_PARAM_CFG_CTRL,			0x00f698),
	REG(QSYS_PORT_MAX_SDU,				0x00f69c),
	REG(QSYS_PARAM_CFG_REG_1,			0x00f440),
	REG(QSYS_PARAM_CFG_REG_2,			0x00f444),
	REG(QSYS_PARAM_CFG_REG_3,			0x00f448),
	REG(QSYS_PARAM_CFG_REG_4,			0x00f44c),
	REG(QSYS_PARAM_CFG_REG_5,			0x00f450),
	REG(QSYS_GCL_CFG_REG_1,				0x00f454),
	REG(QSYS_GCL_CFG_REG_2,				0x00f458),
	REG(QSYS_PARAM_STATUS_REG_1,			0x00f400),
	REG(QSYS_PARAM_STATUS_REG_2,			0x00f404),
	REG(QSYS_PARAM_STATUS_REG_3,			0x00f408),
	REG(QSYS_PARAM_STATUS_REG_4,			0x00f40c),
	REG(QSYS_PARAM_STATUS_REG_5,			0x00f410),
	REG(QSYS_PARAM_STATUS_REG_6,			0x00f414),
	REG(QSYS_PARAM_STATUS_REG_7,			0x00f418),
	REG(QSYS_PARAM_STATUS_REG_8,			0x00f41c),
	REG(QSYS_PARAM_STATUS_REG_9,			0x00f420),
	REG(QSYS_GCL_STATUS_REG_1,			0x00f424),
	REG(QSYS_GCL_STATUS_REG_2,			0x00f428),
};

static const u32 ocelot_rew_regmap[] = {
	REG(REW_PORT_VLAN_CFG,				0x000000),
	REG(REW_TAG_CFG,				0x000004),
	REG(REW_PORT_CFG,				0x000008),
	REG(REW_DSCP_CFG,				0x00000c),
	REG(REW_PCP_DEI_QOS_MAP_CFG,			0x000010),
	REG(REW_PTP_CFG,				0x000050),
	REG(REW_PTP_DLY1_CFG,				0x000054),
	REG(REW_RED_TAG_CFG,				0x000058),
	REG(REW_DSCP_REMAP_DP1_CFG,			0x000410),
	REG(REW_DSCP_REMAP_CFG,				0x000510),
	REG_RESERVED(REW_STAT_CFG),
	REG_RESERVED(REW_REW_STICKY),
	REG_RESERVED(REW_PPT),
};

static const u32 ocelot_sys_regmap[] = {
	REG(SYS_COUNT_RX_OCTETS,			0x000000),
	REG(SYS_COUNT_RX_MULTICAST,			0x000008),
	REG(SYS_COUNT_RX_SHORTS,			0x000010),
	REG(SYS_COUNT_RX_FRAGMENTS,			0x000014),
	REG(SYS_COUNT_RX_JABBERS,			0x000018),
	REG(SYS_COUNT_RX_64,				0x000024),
	REG(SYS_COUNT_RX_65_127,			0x000028),
	REG(SYS_COUNT_RX_128_255,			0x00002c),
	REG(SYS_COUNT_RX_256_1023,			0x000030),
	REG(SYS_COUNT_RX_1024_1526,			0x000034),
	REG(SYS_COUNT_RX_1527_MAX,			0x000038),
	REG(SYS_COUNT_RX_LONGS,				0x000044),
	REG(SYS_COUNT_TX_OCTETS,			0x000200),
	REG(SYS_COUNT_TX_COLLISION,			0x000210),
	REG(SYS_COUNT_TX_DROPS,				0x000214),
	REG(SYS_COUNT_TX_64,				0x00021c),
	REG(SYS_COUNT_TX_65_127,			0x000220),
	REG(SYS_COUNT_TX_128_511,			0x000224),
	REG(SYS_COUNT_TX_512_1023,			0x000228),
	REG(SYS_COUNT_TX_1024_1526,			0x00022c),
	REG(SYS_COUNT_TX_1527_MAX,			0x000230),
	REG(SYS_COUNT_TX_AGING,				0x000278),
	REG(SYS_RESET_CFG,				0x000e00),
	REG(SYS_SR_ETYPE_CFG,				0x000e04),
	REG(SYS_VLAN_ETYPE_CFG,				0x000e08),
	REG(SYS_PORT_MODE,				0x000e0c),
	REG(SYS_FRONT_PORT_MODE,			0x000e2c),
	REG(SYS_FRM_AGING,				0x000e44),
	REG(SYS_STAT_CFG,				0x000e48),
	REG(SYS_SW_STATUS,				0x000e4c),
	REG_RESERVED(SYS_MISC_CFG),
	REG(SYS_REW_MAC_HIGH_CFG,			0x000e6c),
	REG(SYS_REW_MAC_LOW_CFG,			0x000e84),
	REG(SYS_TIMESTAMP_OFFSET,			0x000e9c),
	REG(SYS_PAUSE_CFG,				0x000ea0),
	REG(SYS_PAUSE_TOT_CFG,				0x000ebc),
	REG(SYS_ATOP,					0x000ec0),
	REG(SYS_ATOP_TOT_CFG,				0x000edc),
	REG(SYS_MAC_FC_CFG,				0x000ee0),
	REG(SYS_MMGT,					0x000ef8),
	REG_RESERVED(SYS_MMGT_FAST),
	REG_RESERVED(SYS_EVENTS_DIF),
	REG_RESERVED(SYS_EVENTS_CORE),
	REG_RESERVED(SYS_CNT),
	REG(SYS_PTP_STATUS,				0x000f14),
	REG(SYS_PTP_TXSTAMP,				0x000f18),
	REG(SYS_PTP_NXT,				0x000f1c),
	REG(SYS_PTP_CFG,				0x000f20),
	REG(SYS_RAM_INIT,				0x000f24),
	REG_RESERVED(SYS_CM_ADDR),
	REG_RESERVED(SYS_CM_DATA_WR),
	REG_RESERVED(SYS_CM_DATA_RD),
	REG_RESERVED(SYS_CM_OP),
	REG_RESERVED(SYS_CM_DATA),
};

static const u32 ocelot_vcap_regmap[] = {
	/* VCAP_CORE_CFG */
	REG(VCAP_CORE_UPDATE_CTRL,			0x000000),
	REG(VCAP_CORE_MV_CFG,				0x000004),
	/* VCAP_CORE_CACHE */
	REG(VCAP_CACHE_ENTRY_DAT,			0x000008),
	REG(VCAP_CACHE_MASK_DAT,			0x000108),
	REG(VCAP_CACHE_ACTION_DAT,			0x000208),
	REG(VCAP_CACHE_CNT_DAT,				0x000308),
	REG(VCAP_CACHE_TG_DAT,				0x000388),
	/* VCAP_CONST */
	REG(VCAP_CONST_VCAP_VER,			0x000398),
	REG(VCAP_CONST_ENTRY_WIDTH,			0x00039c),
	REG(VCAP_CONST_ENTRY_CNT,			0x0003a0),
	REG(VCAP_CONST_ENTRY_SWCNT,			0x0003a4),
	REG(VCAP_CONST_ENTRY_TG_WIDTH,			0x0003a8),
	REG(VCAP_CONST_ACTION_DEF_CNT,			0x0003ac),
	REG(VCAP_CONST_ACTION_WIDTH,			0x0003b0),
	REG(VCAP_CONST_CNT_WIDTH,			0x0003b4),
	REG(VCAP_CONST_CORE_CNT,			0x0003b8),
	REG(VCAP_CONST_IF_CNT,				0x0003bc),
};

static const u32 ocelot_ptp_regmap[] = {
	REG(PTP_PIN_CFG,				0x000000),
	REG(PTP_PIN_TOD_SEC_MSB,			0x000004),
	REG(PTP_PIN_TOD_SEC_LSB,			0x000008),
	REG(PTP_PIN_TOD_NSEC,				0x00000c),
	REG(PTP_PIN_WF_HIGH_PERIOD,			0x000014),
	REG(PTP_PIN_WF_LOW_PERIOD,			0x000018),
	REG(PTP_CFG_MISC,				0x0000a0),
	REG(PTP_CLK_CFG_ADJ_CFG,			0x0000a4),
	REG(PTP_CLK_CFG_ADJ_FREQ,			0x0000a8),
};

static const u32 ocelot_gcb_regmap[] = {
	REG(GCB_SOFT_RST,			0x000004),
};

static const u32 ocelot_dev_gmii_regmap[] = {
	REG(DEV_CLOCK_CFG,				0x0),
	REG(DEV_PORT_MISC,				0x4),
	REG(DEV_EVENTS,					0x8),
	REG(DEV_EEE_CFG,				0xc),
	REG(DEV_RX_PATH_DELAY,				0x10),
	REG(DEV_TX_PATH_DELAY,				0x14),
	REG(DEV_PTP_PREDICT_CFG,			0x18),
	REG(DEV_MAC_ENA_CFG,				0x1c),
	REG(DEV_MAC_MODE_CFG,				0x20),
	REG(DEV_MAC_MAXLEN_CFG,				0x24),
	REG(DEV_MAC_TAGS_CFG,				0x28),
	REG(DEV_MAC_ADV_CHK_CFG,			0x2c),
	REG(DEV_MAC_IFG_CFG,				0x30),
	REG(DEV_MAC_HDX_CFG,				0x34),
	REG(DEV_MAC_DBG_CFG,				0x38),
	REG(DEV_MAC_FC_MAC_LOW_CFG,			0x3c),
	REG(DEV_MAC_FC_MAC_HIGH_CFG,			0x40),
	REG(DEV_MAC_STICKY,				0x44),
	REG_RESERVED(PCS1G_CFG),
	REG_RESERVED(PCS1G_MODE_CFG),
	REG_RESERVED(PCS1G_SD_CFG),
	REG_RESERVED(PCS1G_ANEG_CFG),
	REG_RESERVED(PCS1G_ANEG_NP_CFG),
	REG_RESERVED(PCS1G_LB_CFG),
	REG_RESERVED(PCS1G_DBG_CFG),
	REG_RESERVED(PCS1G_CDET_CFG),
	REG_RESERVED(PCS1G_ANEG_STATUS),
	REG_RESERVED(PCS1G_ANEG_NP_STATUS),
	REG_RESERVED(PCS1G_LINK_STATUS),
	REG_RESERVED(PCS1G_LINK_DOWN_CNT),
	REG_RESERVED(PCS1G_STICKY),
	REG_RESERVED(PCS1G_DEBUG_STATUS),
	REG_RESERVED(PCS1G_LPI_CFG),
	REG_RESERVED(PCS1G_LPI_WAKE_ERROR_CNT),
	REG_RESERVED(PCS1G_LPI_STATUS),
	REG_RESERVED(PCS1G_TSTPAT_MODE_CFG),
	REG_RESERVED(PCS1G_TSTPAT_STATUS),
	REG_RESERVED(DEV_PCS_FX100_CFG),
	REG_RESERVED(DEV_PCS_FX100_STATUS),
};

static const u32 *ocelot_regmap[TARGET_MAX] = {
	[ANA] = ocelot_ana_regmap,
	[QS] = ocelot_qs_regmap,
	[QSYS] = ocelot_qsys_regmap,
	[REW] = ocelot_rew_regmap,
	[SYS] = ocelot_sys_regmap,
	[S0] = ocelot_vcap_regmap,
	[S1] = ocelot_vcap_regmap,
	[S2] = ocelot_vcap_regmap,
	[PTP] = ocelot_ptp_regmap,
	[GCB] = ocelot_gcb_regmap,
	[DEV_GMII] = ocelot_dev_gmii_regmap,
};

/* Addresses are relative to the PCI device's base address */
static const struct resource vsc9959_target_io_res[TARGET_MAX] = {
	[ANA] = {
		.start	= 0x0280000,
		.end	= 0x028ffff,
		.name	= "ana",
	},
	[QS] = {
		.start	= 0x0080000,
		.end	= 0x00800ff,
		.name	= "qs",
	},
	[QSYS] = {
		.start	= 0x0200000,
		.end	= 0x021ffff,
		.name	= "qsys",
	},
	[REW] = {
		.start	= 0x0030000,
		.end	= 0x003ffff,
		.name	= "rew",
	},
	[SYS] = {
		.start	= 0x0010000,
		.end	= 0x001ffff,
		.name	= "sys",
	},
	[S0] = {
		.start	= 0x0040000,
		.end	= 0x00403ff,
		.name	= "s0",
	},
	[S1] = {
		.start	= 0x0050000,
		.end	= 0x00503ff,
		.name	= "s1",
	},
	[S2] = {
		.start	= 0x0060000,
		.end	= 0x00603ff,
		.name	= "s2",
	},
	[PTP] = {
		.start	= 0x0090000,
		.end	= 0x00900cb,
		.name	= "ptp",
	},
	[GCB] = {
		.start	= 0x0070000,
		.end	= 0x00701ff,
		.name	= "devcpu_gcb",
	},
};

static const struct resource vsc9959_port_io_res[] = {
	{
		.start	= 0x0100000,
		.end	= 0x010ffff,
		.name	= "port0",
	},
	{
		.start	= 0x0110000,
		.end	= 0x011ffff,
		.name	= "port1",
	},
	{
		.start	= 0x0120000,
		.end	= 0x012ffff,
		.name	= "port2",
	},
	{
		.start	= 0x0130000,
		.end	= 0x013ffff,
		.name	= "port3",
	},
	{
		.start	= 0x0140000,
		.end	= 0x014ffff,
		.name	= "port4",
	},
	{
		.start	= 0x0150000,
		.end	= 0x015ffff,
		.name	= "port5",
	},
};

/* Port MAC 0 Internal MDIO bus through which the SerDes acting as an
 * SGMII/QSGMII MAC PCS can be found.
 */
static const struct resource ocelot_imdio_res = {
	.start		= 0x8030,
	.end		= 0x8040,
	.name		= "imdio",
};

static const struct reg_field ocelot_regfields[REGFIELD_MAX] = {
	[ANA_ADVLEARN_VLAN_CHK] = REG_FIELD(ANA_ADVLEARN, 6, 6),
	[ANA_ADVLEARN_LEARN_MIRROR] = REG_FIELD(ANA_ADVLEARN, 0, 5),
	[ANA_ANEVENTS_FLOOD_DISCARD] = REG_FIELD(ANA_ANEVENTS, 30, 30),
	[ANA_ANEVENTS_AUTOAGE] = REG_FIELD(ANA_ANEVENTS, 26, 26),
	[ANA_ANEVENTS_STORM_DROP] = REG_FIELD(ANA_ANEVENTS, 24, 24),
	[ANA_ANEVENTS_LEARN_DROP] = REG_FIELD(ANA_ANEVENTS, 23, 23),
	[ANA_ANEVENTS_AGED_ENTRY] = REG_FIELD(ANA_ANEVENTS, 22, 22),
	[ANA_ANEVENTS_CPU_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 21, 21),
	[ANA_ANEVENTS_AUTO_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 20, 20),
	[ANA_ANEVENTS_LEARN_REMOVE] = REG_FIELD(ANA_ANEVENTS, 19, 19),
	[ANA_ANEVENTS_AUTO_LEARNED] = REG_FIELD(ANA_ANEVENTS, 18, 18),
	[ANA_ANEVENTS_AUTO_MOVED] = REG_FIELD(ANA_ANEVENTS, 17, 17),
	[ANA_ANEVENTS_CLASSIFIED_DROP] = REG_FIELD(ANA_ANEVENTS, 15, 15),
	[ANA_ANEVENTS_CLASSIFIED_COPY] = REG_FIELD(ANA_ANEVENTS, 14, 14),
	[ANA_ANEVENTS_VLAN_DISCARD] = REG_FIELD(ANA_ANEVENTS, 13, 13),
	[ANA_ANEVENTS_FWD_DISCARD] = REG_FIELD(ANA_ANEVENTS, 12, 12),
	[ANA_ANEVENTS_MULTICAST_FLOOD] = REG_FIELD(ANA_ANEVENTS, 11, 11),
	[ANA_ANEVENTS_UNICAST_FLOOD] = REG_FIELD(ANA_ANEVENTS, 10, 10),
	[ANA_ANEVENTS_DEST_KNOWN] = REG_FIELD(ANA_ANEVENTS, 9, 9),
	[ANA_ANEVENTS_BUCKET3_MATCH] = REG_FIELD(ANA_ANEVENTS, 8, 8),
	[ANA_ANEVENTS_BUCKET2_MATCH] = REG_FIELD(ANA_ANEVENTS, 7, 7),
	[ANA_ANEVENTS_BUCKET1_MATCH] = REG_FIELD(ANA_ANEVENTS, 6, 6),
	[ANA_ANEVENTS_BUCKET0_MATCH] = REG_FIELD(ANA_ANEVENTS, 5, 5),
	[ANA_ANEVENTS_CPU_OPERATION] = REG_FIELD(ANA_ANEVENTS, 4, 4),
	[ANA_ANEVENTS_DMAC_LOOKUP] = REG_FIELD(ANA_ANEVENTS, 3, 3),
	[ANA_ANEVENTS_SMAC_LOOKUP] = REG_FIELD(ANA_ANEVENTS, 2, 2),
	[ANA_ANEVENTS_SEQ_GEN_ERR_0] = REG_FIELD(ANA_ANEVENTS, 1, 1),
	[ANA_ANEVENTS_SEQ_GEN_ERR_1] = REG_FIELD(ANA_ANEVENTS, 0, 0),
	[ANA_TABLES_MACACCESS_B_DOM] = REG_FIELD(ANA_TABLES_MACACCESS, 16, 16),
	[ANA_TABLES_MACTINDX_BUCKET] = REG_FIELD(ANA_TABLES_MACTINDX, 11, 12),
	[ANA_TABLES_MACTINDX_M_INDEX] = REG_FIELD(ANA_TABLES_MACTINDX, 0, 10),
	[SYS_RESET_CFG_CORE_ENA] = REG_FIELD(SYS_RESET_CFG, 0, 0),
	[GCB_SOFT_RST_SWC_RST] = REG_FIELD(GCB_SOFT_RST, 0, 0),
	/* Replicated per number of ports (7), register size 4 per port */
	[QSYS_SWITCH_PORT_MODE_PORT_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 14, 14, 7, 4),
	[QSYS_SWITCH_PORT_MODE_SCH_NEXT_CFG] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 11, 13, 7, 4),
	[QSYS_SWITCH_PORT_MODE_YEL_RSRVD] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 10, 10, 7, 4),
	[QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 9, 9, 7, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 1, 8, 7, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 0, 0, 7, 4),
	[SYS_PORT_MODE_DATA_WO_TS] = REG_FIELD_ID(SYS_PORT_MODE, 5, 6, 7, 4),
	[SYS_PORT_MODE_INCL_INJ_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 3, 4, 7, 4),
	[SYS_PORT_MODE_INCL_XTR_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 1, 2, 7, 4),
	[SYS_PORT_MODE_INCL_HDR_ERR] = REG_FIELD_ID(SYS_PORT_MODE, 0, 0, 7, 4),
	[SYS_PAUSE_CFG_PAUSE_START] = REG_FIELD_ID(SYS_PAUSE_CFG, 10, 18, 7, 4),
	[SYS_PAUSE_CFG_PAUSE_STOP] = REG_FIELD_ID(SYS_PAUSE_CFG, 1, 9, 7, 4),
	[SYS_PAUSE_CFG_PAUSE_ENA] = REG_FIELD_ID(SYS_PAUSE_CFG, 0, 1, 7, 4),
};

static const struct ocelot_stat_layout ocelot_stats_layout[] = {
	{ .offset = 0x00,	.name = "rx_octets", },
	{ .offset = 0x01,	.name = "rx_unicast", },
	{ .offset = 0x02,	.name = "rx_multicast", },
	{ .offset = 0x03,	.name = "rx_broadcast", },
	{ .offset = 0x04,	.name = "rx_shorts", },
	{ .offset = 0x05,	.name = "rx_fragments", },
	{ .offset = 0x06,	.name = "rx_jabbers", },
	{ .offset = 0x07,	.name = "rx_crc_align_errs", },
	{ .offset = 0x08,	.name = "rx_sym_errs", },
	{ .offset = 0x09,	.name = "rx_frames_below_65_octets", },
	{ .offset = 0x0A,	.name = "rx_frames_65_to_127_octets", },
	{ .offset = 0x0B,	.name = "rx_frames_128_to_255_octets", },
	{ .offset = 0x0C,	.name = "rx_frames_256_to_511_octets", },
	{ .offset = 0x0D,	.name = "rx_frames_512_to_1023_octets", },
	{ .offset = 0x0E,	.name = "rx_frames_1024_to_1526_octets", },
	{ .offset = 0x0F,	.name = "rx_frames_over_1526_octets", },
	{ .offset = 0x10,	.name = "rx_pause", },
	{ .offset = 0x11,	.name = "rx_control", },
	{ .offset = 0x12,	.name = "rx_longs", },
	{ .offset = 0x13,	.name = "rx_classified_drops", },
	{ .offset = 0x14,	.name = "rx_red_prio_0", },
	{ .offset = 0x15,	.name = "rx_red_prio_1", },
	{ .offset = 0x16,	.name = "rx_red_prio_2", },
	{ .offset = 0x17,	.name = "rx_red_prio_3", },
	{ .offset = 0x18,	.name = "rx_red_prio_4", },
	{ .offset = 0x19,	.name = "rx_red_prio_5", },
	{ .offset = 0x1A,	.name = "rx_red_prio_6", },
	{ .offset = 0x1B,	.name = "rx_red_prio_7", },
	{ .offset = 0x1C,	.name = "rx_yellow_prio_0", },
	{ .offset = 0x1D,	.name = "rx_yellow_prio_1", },
	{ .offset = 0x1E,	.name = "rx_yellow_prio_2", },
	{ .offset = 0x1F,	.name = "rx_yellow_prio_3", },
	{ .offset = 0x20,	.name = "rx_yellow_prio_4", },
	{ .offset = 0x21,	.name = "rx_yellow_prio_5", },
	{ .offset = 0x22,	.name = "rx_yellow_prio_6", },
	{ .offset = 0x23,	.name = "rx_yellow_prio_7", },
	{ .offset = 0x24,	.name = "rx_green_prio_0", },
	{ .offset = 0x25,	.name = "rx_green_prio_1", },
	{ .offset = 0x26,	.name = "rx_green_prio_2", },
	{ .offset = 0x27,	.name = "rx_green_prio_3", },
	{ .offset = 0x28,	.name = "rx_green_prio_4", },
	{ .offset = 0x29,	.name = "rx_green_prio_5", },
	{ .offset = 0x2A,	.name = "rx_green_prio_6", },
	{ .offset = 0x2B,	.name = "rx_green_prio_7", },
	{ .offset = 0x80,	.name = "tx_octets", },
	{ .offset = 0x81,	.name = "tx_unicast", },
	{ .offset = 0x82,	.name = "tx_multicast", },
	{ .offset = 0x83,	.name = "tx_broadcast", },
	{ .offset = 0x84,	.name = "tx_collision", },
	{ .offset = 0x85,	.name = "tx_drops", },
	{ .offset = 0x86,	.name = "tx_pause", },
	{ .offset = 0x87,	.name = "tx_frames_below_65_octets", },
	{ .offset = 0x88,	.name = "tx_frames_65_to_127_octets", },
	{ .offset = 0x89,	.name = "tx_frames_128_255_octets", },
	{ .offset = 0x8B,	.name = "tx_frames_256_511_octets", },
	{ .offset = 0x8C,	.name = "tx_frames_1024_1526_octets", },
	{ .offset = 0x8D,	.name = "tx_frames_over_1526_octets", },
	{ .offset = 0x8E,	.name = "tx_yellow_prio_0", },
	{ .offset = 0x8F,	.name = "tx_yellow_prio_1", },
	{ .offset = 0x90,	.name = "tx_yellow_prio_2", },
	{ .offset = 0x91,	.name = "tx_yellow_prio_3", },
	{ .offset = 0x92,	.name = "tx_yellow_prio_4", },
	{ .offset = 0x93,	.name = "tx_yellow_prio_5", },
	{ .offset = 0x94,	.name = "tx_yellow_prio_6", },
	{ .offset = 0x95,	.name = "tx_yellow_prio_7", },
	{ .offset = 0x96,	.name = "tx_green_prio_0", },
	{ .offset = 0x97,	.name = "tx_green_prio_1", },
	{ .offset = 0x98,	.name = "tx_green_prio_2", },
	{ .offset = 0x99,	.name = "tx_green_prio_3", },
	{ .offset = 0x9A,	.name = "tx_green_prio_4", },
	{ .offset = 0x9B,	.name = "tx_green_prio_5", },
	{ .offset = 0x9C,	.name = "tx_green_prio_6", },
	{ .offset = 0x9D,	.name = "tx_green_prio_7", },
	{ .offset = 0x9E,	.name = "tx_aged", },
	{ .offset = 0x100,	.name = "drop_local", },
	{ .offset = 0x101,	.name = "drop_tail", },
	{ .offset = 0x102,	.name = "drop_yellow_prio_0", },
	{ .offset = 0x103,	.name = "drop_yellow_prio_1", },
	{ .offset = 0x104,	.name = "drop_yellow_prio_2", },
	{ .offset = 0x105,	.name = "drop_yellow_prio_3", },
	{ .offset = 0x106,	.name = "drop_yellow_prio_4", },
	{ .offset = 0x107,	.name = "drop_yellow_prio_5", },
	{ .offset = 0x108,	.name = "drop_yellow_prio_6", },
	{ .offset = 0x109,	.name = "drop_yellow_prio_7", },
	{ .offset = 0x10A,	.name = "drop_green_prio_0", },
	{ .offset = 0x10B,	.name = "drop_green_prio_1", },
	{ .offset = 0x10C,	.name = "drop_green_prio_2", },
	{ .offset = 0x10D,	.name = "drop_green_prio_3", },
	{ .offset = 0x10E,	.name = "drop_green_prio_4", },
	{ .offset = 0x10F,	.name = "drop_green_prio_5", },
	{ .offset = 0x110,	.name = "drop_green_prio_6", },
	{ .offset = 0x111,	.name = "drop_green_prio_7", },
};

static int ocelot_chip_init(struct ocelot *ocelot, const struct ocelot_ops *ops)
{
	int ret;

	ocelot->map = ocelot_regmap;
	ocelot->stats_layout = ocelot_stats_layout;
	ocelot->num_stats = ARRAY_SIZE(ocelot_stats_layout);
	ocelot->num_mact_rows = 2048;
	ocelot->ops = ops;

	ret = ocelot_regfields_init(ocelot, ocelot_regfields);
	if (ret)
		return ret;

	eth_random_addr(ocelot->base_mac);
	ocelot->base_mac[5] &= 0xf0;

	return 0;
}

static int ocelot_parse_ifh(u32 *_ifh, struct frame_info *info)
{
	u8 llen, wlen;
	u64 ifh[2];

	ifh[0] = be64_to_cpu(((__force __be64 *)_ifh)[0]);
	ifh[1] = be64_to_cpu(((__force __be64 *)_ifh)[1]);

	wlen = IFH_EXTRACT_BITFIELD64(ifh[0], 7,  8);
	llen = IFH_EXTRACT_BITFIELD64(ifh[0], 15,  6);

	info->len = OCELOT_BUFFER_CELL_SZ * wlen + llen - 80;

	info->timestamp = IFH_EXTRACT_BITFIELD64(ifh[0], 21, 32);

	info->port = IFH_EXTRACT_BITFIELD64(ifh[1], 43, 4);

	info->tag_type = IFH_EXTRACT_BITFIELD64(ifh[1], 16,  1);
	info->vid = IFH_EXTRACT_BITFIELD64(ifh[1], 0,  12);

	return 0;
}

static int ocelot_rx_frame_word(struct ocelot *ocelot, u8 grp, bool ifh,
				u32 *rval)
{
	u32 val;
	u32 bytes_valid;

	val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
	if (val == XTR_NOT_READY) {
		if (ifh)
			return -EIO;

		do {
			val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		} while (val == XTR_NOT_READY);
	}

	switch (val) {
	case XTR_ABORT:
		return -EIO;
	case XTR_EOF_0:
	case XTR_EOF_1:
	case XTR_EOF_2:
	case XTR_EOF_3:
	case XTR_PRUNED:
		bytes_valid = XTR_VALID_BYTES(val);
		val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		if (val == XTR_ESCAPE)
			*rval = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		else
			*rval = val;

		return bytes_valid;
	case XTR_ESCAPE:
		*rval = ocelot_read_rix(ocelot, QS_XTR_RD, grp);

		return 4;
	default:
		*rval = val;

		return 4;
	}
}

static irqreturn_t ocelot_xtr_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;
	int i = 0, grp = 0;
	int err = 0;

	if (!(ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp)))
		return IRQ_NONE;

	do {
		struct skb_shared_hwtstamps *shhwtstamps;
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;
		u64 tod_in_ns, full_ts_in_ns;
		struct frame_info info = {};
		struct net_device *dev;
		u32 ifh[4], val, *buf;
		struct timespec64 ts;
		int sz, len, buf_len;
		struct sk_buff *skb;

		for (i = 0; i < OCELOT_TAG_LEN / 4; i++) {
			err = ocelot_rx_frame_word(ocelot, grp, true, &ifh[i]);
			if (err != 4)
				break;
		}

		if (err != 4)
			break;

		/* At this point the IFH was read correctly, so it is safe to
		 * presume that there is no error. The err needs to be reset
		 * otherwise a frame could come in CPU queue between the while
		 * condition and the check for error later on. And in that case
		 * the new frame is just removed and not processed.
		 */
		err = 0;

		ocelot_parse_ifh(ifh, &info);

		ocelot_port = ocelot->ports[info.port];
		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);
		dev = priv->dev;

		skb = netdev_alloc_skb(dev, info.len);

		if (unlikely(!skb)) {
			netdev_err(dev, "Unable to allocate sk_buff\n");
			err = -ENOMEM;
			break;
		}
		buf_len = info.len - ETH_FCS_LEN;
		buf = (u32 *)skb_put(skb, buf_len);

		len = 0;
		do {
			sz = ocelot_rx_frame_word(ocelot, grp, false, &val);
			*buf++ = val;
			len += sz;
		} while (len < buf_len);

		/* Read the FCS */
		sz = ocelot_rx_frame_word(ocelot, grp, false, &val);
		/* Update the statistics if part of the FCS was read before */
		len -= ETH_FCS_LEN - sz;

		if (unlikely(dev->features & NETIF_F_RXFCS)) {
			buf = (u32 *)skb_put(skb, ETH_FCS_LEN);
			*buf = val;
		}

		if (sz < 0) {
			err = sz;
			break;
		}

		if (ocelot->ptp) {
			ocelot_ptp_gettime64(&ocelot->ptp_info, &ts);

			tod_in_ns = ktime_set(ts.tv_sec, ts.tv_nsec);
			if ((tod_in_ns & 0xffffffff) < info.timestamp)
				full_ts_in_ns = (((tod_in_ns >> 32) - 1) << 32) |
						info.timestamp;
			else
				full_ts_in_ns = (tod_in_ns & GENMASK_ULL(63, 32)) |
						info.timestamp;

			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
			shhwtstamps->hwtstamp = full_ts_in_ns;
		}

		/* Everything we see on an interface that is in the HW bridge
		 * has already been forwarded.
		 */
		if (ocelot->bridge_mask & BIT(info.port))
			skb->offload_fwd_mark = 1;

		skb->protocol = eth_type_trans(skb, dev);
		if (!skb_defer_rx_timestamp(skb))
			netif_rx(skb);
		dev->stats.rx_bytes += len;
		dev->stats.rx_packets++;
	} while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp));

	if (err)
		while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp))
			ocelot_read_rix(ocelot, QS_XTR_RD, grp);

	return IRQ_HANDLED;
}

static irqreturn_t ocelot_ptp_rdy_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;

	ocelot_get_txtstamp(ocelot);

	return IRQ_HANDLED;
}

#define OCELOT_INIT_TIMEOUT	50000
#define OCELOT_GCB_RST_SLEEP	100
#define OCELOT_SYS_RAMINIT_SLEEP	80

static int ocelot_gcb_soft_rst_status(struct ocelot *ocelot)
{
	int val;

	regmap_field_read(ocelot->regfields[GCB_SOFT_RST_SWC_RST], &val);
	return val;
}

static int ocelot_sys_ram_init_status(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, SYS_RAM_INIT);
}

static int ocelot_reset(struct ocelot *ocelot)
{
	int val, err;

	/* soft-reset the switch core */
	regmap_field_write(ocelot->regfields[GCB_SOFT_RST_SWC_RST], 1);

	err = readx_poll_timeout(ocelot_gcb_soft_rst_status, ocelot, val, !val,
				 OCELOT_GCB_RST_SLEEP, OCELOT_INIT_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "timeout: switch core reset\n");
		return err;
	}

	/* initialize switch mem ~40us */
	ocelot_write(ocelot, SYS_RAM_INIT_RAM_INIT, SYS_RAM_INIT);
	err = readx_poll_timeout(ocelot_sys_ram_init_status, ocelot, val, !val,
				 OCELOT_SYS_RAMINIT_SLEEP, OCELOT_INIT_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "timeout: switch sram init\n");
		return err;
	}

	/* enable switch core */
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_CORE_ENA], 1);

	return 0;
}

/* Watermark encode
 * Bit 8:   Unit; 0:1, 1:16
 * Bit 7-0: Value to be multiplied with unit
 */
static u16 ocelot_wm_enc(u16 value)
{
	WARN_ON(value >= 16 * BIT(8));

	if (value >= BIT(8))
		return BIT(8) | (value / 16);

	return value;
}

static u16 ocelot_wm_dec(u16 wm)
{
	if (wm & BIT(8))
		return (wm & GENMASK(7, 0)) * 16;

	return wm;
}

static void ocelot_wm_stat(u32 val, u32 *inuse, u32 *maxuse)
{
	*inuse = (val & GENMASK(23, 12)) >> 12;
	*maxuse = val & GENMASK(11, 0);
}

static const struct ocelot_ops ocelot_ops = {
	.reset			= ocelot_reset,
	.wm_enc			= ocelot_wm_enc,
	.wm_dec			= ocelot_wm_dec,
	.wm_stat		= ocelot_wm_stat,
	.port_to_netdev		= ocelot_port_to_netdev,
	.netdev_to_port		= ocelot_netdev_to_port,
};

static const struct vcap_field vsc7514_vcap_es0_keys[] = {
	[VCAP_ES0_EGR_PORT]			= {  0,  3},
	[VCAP_ES0_IGR_PORT]			= {  3,  3},
	[VCAP_ES0_RSV]				= {  6,  2},
	[VCAP_ES0_L2_MC]			= {  8,  1},
	[VCAP_ES0_L2_BC]			= {  9,  1},
	[VCAP_ES0_VID]				= { 10, 12},
	[VCAP_ES0_DP]				= { 22,  1},
	[VCAP_ES0_PCP]				= { 23,  3},
};

static const struct vcap_field vsc7514_vcap_es0_actions[] = {
	[VCAP_ES0_ACT_PUSH_OUTER_TAG]		= {  0,  2},
	[VCAP_ES0_ACT_PUSH_INNER_TAG]		= {  2,  1},
	[VCAP_ES0_ACT_TAG_A_TPID_SEL]		= {  3,  2},
	[VCAP_ES0_ACT_TAG_A_VID_SEL]		= {  5,  1},
	[VCAP_ES0_ACT_TAG_A_PCP_SEL]		= {  6,  2},
	[VCAP_ES0_ACT_TAG_A_DEI_SEL]		= {  8,  2},
	[VCAP_ES0_ACT_TAG_B_TPID_SEL]		= { 10,  2},
	[VCAP_ES0_ACT_TAG_B_VID_SEL]		= { 12,  1},
	[VCAP_ES0_ACT_TAG_B_PCP_SEL]		= { 13,  2},
	[VCAP_ES0_ACT_TAG_B_DEI_SEL]		= { 15,  2},
	[VCAP_ES0_ACT_VID_A_VAL]		= { 17, 12},
	[VCAP_ES0_ACT_PCP_A_VAL]		= { 29,  3},
	[VCAP_ES0_ACT_DEI_A_VAL]		= { 32,  1},
	[VCAP_ES0_ACT_VID_B_VAL]		= { 33, 12},
	[VCAP_ES0_ACT_PCP_B_VAL]		= { 45,  3},
	[VCAP_ES0_ACT_DEI_B_VAL]		= { 48,  1},
	[VCAP_ES0_ACT_RSV]			= { 49, 23},
	[VCAP_ES0_ACT_HIT_STICKY]		= { 72,  1},
};

static const struct vcap_field vsc7514_vcap_is1_keys[] = {
	[VCAP_IS1_HK_TYPE]			= {  0,   1},
	[VCAP_IS1_HK_LOOKUP]			= {  1,   2},
	[VCAP_IS1_HK_IGR_PORT_MASK]		= {  3,   7},
	[VCAP_IS1_HK_RSV]			= { 10,   9},
	[VCAP_IS1_HK_OAM_Y1731]			= { 19,   1},
	[VCAP_IS1_HK_L2_MC]			= { 20,   1},
	[VCAP_IS1_HK_L2_BC]			= { 21,   1},
	[VCAP_IS1_HK_IP_MC]			= { 22,   1},
	[VCAP_IS1_HK_VLAN_TAGGED]		= { 23,   1},
	[VCAP_IS1_HK_VLAN_DBL_TAGGED]		= { 24,   1},
	[VCAP_IS1_HK_TPID]			= { 25,   1},
	[VCAP_IS1_HK_VID]			= { 26,  12},
	[VCAP_IS1_HK_DEI]			= { 38,   1},
	[VCAP_IS1_HK_PCP]			= { 39,   3},
	/* Specific Fields for IS1 Half Key S1_NORMAL */
	[VCAP_IS1_HK_L2_SMAC]			= { 42,  48},
	[VCAP_IS1_HK_ETYPE_LEN]			= { 90,   1},
	[VCAP_IS1_HK_ETYPE]			= { 91,  16},
	[VCAP_IS1_HK_IP_SNAP]			= {107,   1},
	[VCAP_IS1_HK_IP4]			= {108,   1},
	/* Layer-3 Information */
	[VCAP_IS1_HK_L3_FRAGMENT]		= {109,   1},
	[VCAP_IS1_HK_L3_FRAG_OFS_GT0]		= {110,   1},
	[VCAP_IS1_HK_L3_OPTIONS]		= {111,   1},
	[VCAP_IS1_HK_L3_DSCP]			= {112,   6},
	[VCAP_IS1_HK_L3_IP4_SIP]		= {118,  32},
	/* Layer-4 Information */
	[VCAP_IS1_HK_TCP_UDP]			= {150,   1},
	[VCAP_IS1_HK_TCP]			= {151,   1},
	[VCAP_IS1_HK_L4_SPORT]			= {152,  16},
	[VCAP_IS1_HK_L4_RNG]			= {168,   8},
	/* Specific Fields for IS1 Half Key S1_5TUPLE_IP4 */
	[VCAP_IS1_HK_IP4_INNER_TPID]            = { 42,   1},
	[VCAP_IS1_HK_IP4_INNER_VID]		= { 43,  12},
	[VCAP_IS1_HK_IP4_INNER_DEI]		= { 55,   1},
	[VCAP_IS1_HK_IP4_INNER_PCP]		= { 56,   3},
	[VCAP_IS1_HK_IP4_IP4]			= { 59,   1},
	[VCAP_IS1_HK_IP4_L3_FRAGMENT]		= { 60,   1},
	[VCAP_IS1_HK_IP4_L3_FRAG_OFS_GT0]	= { 61,   1},
	[VCAP_IS1_HK_IP4_L3_OPTIONS]		= { 62,   1},
	[VCAP_IS1_HK_IP4_L3_DSCP]		= { 63,   6},
	[VCAP_IS1_HK_IP4_L3_IP4_DIP]		= { 69,  32},
	[VCAP_IS1_HK_IP4_L3_IP4_SIP]		= {101,  32},
	[VCAP_IS1_HK_IP4_L3_PROTO]		= {133,   8},
	[VCAP_IS1_HK_IP4_TCP_UDP]		= {141,   1},
	[VCAP_IS1_HK_IP4_TCP]			= {142,   1},
	[VCAP_IS1_HK_IP4_L4_RNG]		= {143,   8},
	[VCAP_IS1_HK_IP4_IP_PAYLOAD_S1_5TUPLE]	= {151,  32},
};

static const struct vcap_field vsc7514_vcap_is1_actions[] = {
	[VCAP_IS1_ACT_DSCP_ENA]			= {  0,  1},
	[VCAP_IS1_ACT_DSCP_VAL]			= {  1,  6},
	[VCAP_IS1_ACT_QOS_ENA]			= {  7,  1},
	[VCAP_IS1_ACT_QOS_VAL]			= {  8,  3},
	[VCAP_IS1_ACT_DP_ENA]			= { 11,  1},
	[VCAP_IS1_ACT_DP_VAL]			= { 12,  1},
	[VCAP_IS1_ACT_PAG_OVERRIDE_MASK]	= { 13,  8},
	[VCAP_IS1_ACT_PAG_VAL]			= { 21,  8},
	[VCAP_IS1_ACT_RSV]			= { 29,  9},
	/* The fields below are incorrectly shifted by 2 in the manual */
	[VCAP_IS1_ACT_VID_REPLACE_ENA]		= { 38,  1},
	[VCAP_IS1_ACT_VID_ADD_VAL]		= { 39, 12},
	[VCAP_IS1_ACT_FID_SEL]			= { 51,  2},
	[VCAP_IS1_ACT_FID_VAL]			= { 53, 13},
	[VCAP_IS1_ACT_PCP_DEI_ENA]		= { 66,  1},
	[VCAP_IS1_ACT_PCP_VAL]			= { 67,  3},
	[VCAP_IS1_ACT_DEI_VAL]			= { 70,  1},
	[VCAP_IS1_ACT_VLAN_POP_CNT_ENA]		= { 71,  1},
	[VCAP_IS1_ACT_VLAN_POP_CNT]		= { 72,  2},
	[VCAP_IS1_ACT_CUSTOM_ACE_TYPE_ENA]	= { 74,  4},
	[VCAP_IS1_ACT_HIT_STICKY]		= { 78,  1},
};

static const struct vcap_field vsc7514_vcap_is2_keys[] = {
	/* Common: 41 bits */
	[VCAP_IS2_TYPE]				= {  0,   4},
	[VCAP_IS2_HK_FIRST]			= {  4,   1},
	[VCAP_IS2_HK_PAG]			= {  5,   8},
	[VCAP_IS2_HK_IGR_PORT_MASK]		= { 13,   7},
	[VCAP_IS2_HK_RSV2]			= { 20,   1},
	[VCAP_IS2_HK_HOST_MATCH]		= { 21,   1},
	[VCAP_IS2_HK_L2_MC]			= { 22,   1},
	[VCAP_IS2_HK_L2_BC]			= { 23,   1},
	[VCAP_IS2_HK_VLAN_TAGGED]		= { 24,   1},
	[VCAP_IS2_HK_VID]			= { 25,  12},
	[VCAP_IS2_HK_DEI]			= { 37,   1},
	[VCAP_IS2_HK_PCP]			= { 38,   3},
	/* MAC_ETYPE / MAC_LLC / MAC_SNAP / OAM common */
	[VCAP_IS2_HK_L2_DMAC]			= { 41,  48},
	[VCAP_IS2_HK_L2_SMAC]			= { 89,  48},
	/* MAC_ETYPE (TYPE=000) */
	[VCAP_IS2_HK_MAC_ETYPE_ETYPE]		= {137,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD0]	= {153,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD1]	= {169,   8},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD2]	= {177,   3},
	/* MAC_LLC (TYPE=001) */
	[VCAP_IS2_HK_MAC_LLC_L2_LLC]		= {137,  40},
	/* MAC_SNAP (TYPE=010) */
	[VCAP_IS2_HK_MAC_SNAP_L2_SNAP]		= {137,  40},
	/* MAC_ARP (TYPE=011) */
	[VCAP_IS2_HK_MAC_ARP_SMAC]		= { 41,  48},
	[VCAP_IS2_HK_MAC_ARP_ADDR_SPACE_OK]	= { 89,   1},
	[VCAP_IS2_HK_MAC_ARP_PROTO_SPACE_OK]	= { 90,   1},
	[VCAP_IS2_HK_MAC_ARP_LEN_OK]		= { 91,   1},
	[VCAP_IS2_HK_MAC_ARP_TARGET_MATCH]	= { 92,   1},
	[VCAP_IS2_HK_MAC_ARP_SENDER_MATCH]	= { 93,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE_UNKNOWN]	= { 94,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE]		= { 95,   2},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_DIP]	= { 97,  32},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_SIP]	= {129,  32},
	[VCAP_IS2_HK_MAC_ARP_DIP_EQ_SIP]	= {161,   1},
	/* IP4_TCP_UDP / IP4_OTHER common */
	[VCAP_IS2_HK_IP4]			= { 41,   1},
	[VCAP_IS2_HK_L3_FRAGMENT]		= { 42,   1},
	[VCAP_IS2_HK_L3_FRAG_OFS_GT0]		= { 43,   1},
	[VCAP_IS2_HK_L3_OPTIONS]		= { 44,   1},
	[VCAP_IS2_HK_IP4_L3_TTL_GT0]		= { 45,   1},
	[VCAP_IS2_HK_L3_TOS]			= { 46,   8},
	[VCAP_IS2_HK_L3_IP4_DIP]		= { 54,  32},
	[VCAP_IS2_HK_L3_IP4_SIP]		= { 86,  32},
	[VCAP_IS2_HK_DIP_EQ_SIP]		= {118,   1},
	/* IP4_TCP_UDP (TYPE=100) */
	[VCAP_IS2_HK_TCP]			= {119,   1},
	[VCAP_IS2_HK_L4_DPORT]			= {120,  16},
	[VCAP_IS2_HK_L4_SPORT]			= {136,  16},
	[VCAP_IS2_HK_L4_RNG]			= {152,   8},
	[VCAP_IS2_HK_L4_SPORT_EQ_DPORT]		= {160,   1},
	[VCAP_IS2_HK_L4_SEQUENCE_EQ0]		= {161,   1},
	[VCAP_IS2_HK_L4_FIN]			= {162,   1},
	[VCAP_IS2_HK_L4_SYN]			= {163,   1},
	[VCAP_IS2_HK_L4_RST]			= {164,   1},
	[VCAP_IS2_HK_L4_PSH]			= {165,   1},
	[VCAP_IS2_HK_L4_ACK]			= {166,   1},
	[VCAP_IS2_HK_L4_URG]			= {167,   1},
	[VCAP_IS2_HK_L4_1588_DOM]		= {168,   8},
	[VCAP_IS2_HK_L4_1588_VER]		= {176,   4},
	/* IP4_OTHER (TYPE=101) */
	[VCAP_IS2_HK_IP4_L3_PROTO]		= {119,   8},
	[VCAP_IS2_HK_L3_PAYLOAD]		= {127,  56},
	/* IP6_STD (TYPE=110) */
	[VCAP_IS2_HK_IP6_L3_TTL_GT0]		= { 41,   1},
	[VCAP_IS2_HK_L3_IP6_SIP]		= { 42, 128},
	[VCAP_IS2_HK_IP6_L3_PROTO]		= {170,   8},
	/* OAM (TYPE=111) */
	[VCAP_IS2_HK_OAM_MEL_FLAGS]		= {137,   7},
	[VCAP_IS2_HK_OAM_VER]			= {144,   5},
	[VCAP_IS2_HK_OAM_OPCODE]		= {149,   8},
	[VCAP_IS2_HK_OAM_FLAGS]			= {157,   8},
	[VCAP_IS2_HK_OAM_MEPID]			= {165,  16},
	[VCAP_IS2_HK_OAM_CCM_CNTS_EQ0]		= {181,   1},
	[VCAP_IS2_HK_OAM_IS_Y1731]		= {182,   1},
};

static const struct vcap_field vsc7514_vcap_is2_actions[] = {
	[VCAP_IS2_ACT_HIT_ME_ONCE]		= {  0,  1},
	[VCAP_IS2_ACT_CPU_COPY_ENA]		= {  1,  1},
	[VCAP_IS2_ACT_CPU_QU_NUM]		= {  2,  3},
	[VCAP_IS2_ACT_MASK_MODE]		= {  5,  2},
	[VCAP_IS2_ACT_MIRROR_ENA]		= {  7,  1},
	[VCAP_IS2_ACT_LRN_DIS]			= {  8,  1},
	[VCAP_IS2_ACT_POLICE_ENA]		= {  9,  1},
	[VCAP_IS2_ACT_POLICE_IDX]		= { 10,  9},
	[VCAP_IS2_ACT_POLICE_VCAP_ONLY]		= { 19,  1},
	[VCAP_IS2_ACT_PORT_MASK]		= { 20,  6},
	[VCAP_IS2_ACT_REW_OP]			= { 26,  9},
	[VCAP_IS2_ACT_SMAC_REPLACE_ENA]		= { 35,  1},
	[VCAP_IS2_ACT_RSV]			= { 36,  2},
	[VCAP_IS2_ACT_ACL_ID]			= { 38,  6},
	[VCAP_IS2_ACT_HIT_CNT]			= { 44, 32},
};

static struct vcap_props vsc7514_vcap_props[] = {
	[VCAP_ES0] = {
		.action_type_width = 0,
		.action_table = {
			[ES0_ACTION_TYPE_NORMAL] = {
				.width = 72, /* HIT_STICKY not included */
				.count = 1,
			},
		},
		.target = S0,
		.keys = vsc7514_vcap_es0_keys,
		.actions = vsc7514_vcap_es0_actions,
	},
	[VCAP_IS1] = {
		.action_type_width = 0,
		.action_table = {
			[IS1_ACTION_TYPE_NORMAL] = {
				.width = 78, /* HIT_STICKY not included */
				.count = 4,
			},
		},
		.target = S1,
		.keys = vsc7514_vcap_is1_keys,
		.actions = vsc7514_vcap_is1_actions,
	},
	[VCAP_IS2] = {
		.action_type_width = 1,
		.action_table = {
			[IS2_ACTION_TYPE_NORMAL] = {
				.width = 44,
				.count = 2
			},
			[IS2_ACTION_TYPE_SMAC_SIP] = {
				.width = 6,
				.count = 4
			},
		},
		.target = S2,
		.keys = vsc7514_vcap_is2_keys,
		.actions = vsc7514_vcap_is2_actions,
	},
};

static struct ptp_clock_info ocelot_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "ocelot ptp",
	.max_adj	= 0x7fffffff,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= OCELOT_PTP_PINS_NUM,
	.n_pins		= OCELOT_PTP_PINS_NUM,
	.pps		= 0,
	.gettime64	= ocelot_ptp_gettime64,
	.settime64	= ocelot_ptp_settime64,
	.adjtime	= ocelot_ptp_adjtime,
	.adjfine	= ocelot_ptp_adjfine,
	.verify		= ocelot_ptp_verify,
	.enable		= ocelot_ptp_enable,
};

static void mscc_ocelot_release_ports(struct ocelot *ocelot)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;

		ocelot_port = ocelot->ports[port];
		if (!ocelot_port)
			continue;

		ocelot_deinit_port(ocelot, port);

		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);

		unregister_netdev(priv->dev);
		free_netdev(priv->dev);
	}
}

static int mscc_ocelot_init_ports(struct pci_dev *pdev,
				  struct device_node *ports)
{
	struct ocelot *ocelot = pci_get_drvdata(pdev);
	struct device_node *portnp;
	resource_size_t base;
	int err;

	ocelot->ports = devm_kcalloc(ocelot->dev, ocelot->num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);
	if (!ocelot->ports)
		return -ENOMEM;

	base = pci_resource_start(pdev, OCELOT_SWITCH_BAR);

	for_each_available_child_of_node(ports, portnp) {
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;
		struct device_node *phy_node;
		struct resource res = {};
		phy_interface_t phy_mode;
		struct phy_device *phy;
		struct regmap *target;
		u32 port;

		if (of_property_read_u32(portnp, "reg", &port))
			continue;

		memcpy(&res, &vsc9959_port_io_res[port], sizeof(res));
		res.flags = IORESOURCE_MEM;
		res.start += base;
		res.end += base;

		target = ocelot_regmap_init(ocelot, &res);
		if (IS_ERR(target))
			continue;

		phy_node = of_parse_phandle(portnp, "phy-handle", 0);

		/* In the case of a fixed PHY, the DT node associated
		 * to the PHY is the Ethernet MAC DT node.
		 */
		if (!phy_node && of_phy_is_fixed_link(portnp)) {
			err = of_phy_register_fixed_link(portnp);
			if (err)
				goto out_put_ports;

			phy_node = of_node_get(portnp);
		}

		if (!phy_node)
			continue;

		phy = of_phy_find_device(phy_node);
		of_node_put(phy_node);
		if (!phy)
			continue;

		err = ocelot_probe_port(ocelot, port, target, phy);
		if (err) {
			of_node_put(portnp);
			goto out_put_ports;
		}

		ocelot_port = ocelot->ports[port];
		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);

		of_get_phy_mode(portnp, &phy_mode);

		ocelot_port->phy_mode = phy_mode;
	}

	return 0;

out_put_ports:
	of_node_put(ports);
	mscc_ocelot_release_ports(ocelot);
	return err;
}

static int mscc_ocelot_devlink_setup(struct ocelot *ocelot)
{
	int err;

	err = ocelot_devlink_init(ocelot);
	if (err)
		return err;

	err = ocelot_devlink_sb_register(ocelot);
	if (err) {
		ocelot_devlink_teardown(ocelot);
		return err;
	}

	return 0;
}

static void mscc_ocelot_devlink_cleanup(struct ocelot *ocelot)
{
	ocelot_devlink_sb_unregister(ocelot);
	ocelot_devlink_teardown(ocelot);
}

static int mscc_ocelot_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	struct device_node *np = pdev->dev.of_node;
	int err, irq_xtr, irq_ptp_rdy;
	struct device_node *ports;
	struct ocelot *ocelot;
	resource_size_t base;
	unsigned int i;
	size_t len;

	if (!np && !pdev->dev.platform_data)
		return -ENODEV;

	ocelot = devm_kzalloc(&pdev->dev, sizeof(*ocelot), GFP_KERNEL);
	if (!ocelot)
		return -ENOMEM;

	pci_set_drvdata(pdev, ocelot);
	ocelot->dev = &pdev->dev;

	base = pci_resource_start(pdev, OCELOT_SWITCH_BAR);

	len = pci_resource_len(pdev, OCELOT_SWITCH_BAR);
	if (!len)
		return -EINVAL;

	for (i = 0; i < TARGET_MAX; i++) {
		struct regmap *target;
		struct resource res;

		if (!vsc9959_target_io_res[i].name)
			continue;

		memcpy(&res, &vsc9959_target_io_res[i], sizeof(res));
		res.flags = IORESOURCE_MEM;
		res.start += base;
		res.end += base;

		target = ocelot_regmap_init(ocelot, &res);
		if (IS_ERR(target)) {
			dev_err(ocelot->dev,
				"Failed to map device memory space\n");
			return PTR_ERR(target);
		}

		ocelot->targets[i] = target;
	}

	err = ocelot_chip_init(ocelot, &ocelot_ops);
	if (err)
		return err;

	err = devm_request_threaded_irq(&pdev->dev, pdev->irq, NULL,
					ocelot_ptp_rdy_irq_handler, IRQF_ONESHOT,
					"felix-intb", ocelot);
	if (err)
		return err;

	ports = of_get_child_by_name(np, "ports");
	if (!ports) {
		dev_err(ocelot->dev, "no ethernet-ports child node found\n");
		return -ENODEV;
	}

	ocelot->num_phys_ports = of_get_child_count(ports);
	ocelot->num_flooding_pgids = 8;
	ocelot->vcap = vsc7514_vcap_props;
	ocelot->inj_prefix = OCELOT_TAG_PREFIX_NONE;
	ocelot->xtr_prefix = OCELOT_TAG_PREFIX_NONE;
	ocelot->npi = -1;

	err = pci_enable_device(pdev);
	if (err)
		goto out_put_ports;

	pci_set_master(pdev);

	err = ocelot_init(ocelot);
	if (err) {
		pci_disable_device(pdev);
		goto out_put_ports;
	}

	err = mscc_ocelot_init_ports(pdev, ports);
	if (err) {
		pci_disable_device(pdev);
		goto out_ocelot_deinit;
	}

	if (ocelot->ptp) {
		err = ocelot_init_timestamp(ocelot, &ocelot_ptp_clock_info);
		if (err) {
			dev_err(ocelot->dev,
				"Timestamp initialization failed\n");
			ocelot->ptp = 0;
		}
	}

	err = mscc_ocelot_devlink_setup(ocelot);
	if (err) {
		mscc_ocelot_release_ports(ocelot);
		goto out_ocelot_deinit;
	}

	register_netdevice_notifier(&ocelot_netdevice_nb);
	register_switchdev_notifier(&ocelot_switchdev_nb);
	register_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);

	of_node_put(ports);

	dev_info(&pdev->dev, "Ocelot switch probed\n");

	return 0;

out_ocelot_deinit:
	ocelot_deinit(ocelot);
out_put_ports:
	of_node_put(ports);
	return err;
}

static void mscc_ocelot_remove(struct pci_dev *pdev)
{
	struct ocelot *ocelot = pci_get_drvdata(pdev);

	mscc_ocelot_devlink_cleanup(ocelot);
	ocelot_deinit_timestamp(ocelot);
	mscc_ocelot_release_ports(ocelot);
	ocelot_deinit(ocelot);
	unregister_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);
	unregister_switchdev_notifier(&ocelot_switchdev_nb);
	unregister_netdevice_notifier(&ocelot_netdevice_nb);

	pci_disable_device(pdev);
}

static struct pci_device_id ocelot_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, PCI_DEVICE_ID_OCELOT) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ocelot_ids);

static struct pci_driver ocelot_pci_driver = {
	.name = "mscc_felix",
	.id_table = ocelot_ids,
	.probe = mscc_ocelot_probe,
	.remove = mscc_ocelot_remove,
};

module_pci_driver(ocelot_pci_driver);

MODULE_DESCRIPTION("Microsemi Ocelot switch driver");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_LICENSE("Dual MIT/GPL");
