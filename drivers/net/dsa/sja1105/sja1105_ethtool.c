// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include "sja1105.h"

enum sja1105_counter_index {
	__SJA1105_COUNTER_UNUSED,
	/* MAC */
	N_RUNT,
	N_SOFERR,
	N_ALIGNERR,
	N_MIIERR,
	TYPEERR,
	SIZEERR,
	TCTIMEOUT,
	PRIORERR,
	NOMASTER,
	MEMOV,
	MEMERR,
	INVTYP,
	INTCYOV,
	DOMERR,
	PCFBAGDROP,
	SPCPRIOR,
	AGEPRIOR,
	PORTDROP,
	LENDROP,
	BAGDROP,
	POLICEERR,
	DRPNONA664ERR,
	SPCERR,
	AGEDRP,
	/* HL1 */
	N_N664ERR,
	N_VLANERR,
	N_UNRELEASED,
	N_SIZEERR,
	N_CRCERR,
	N_VLNOTFOUND,
	N_CTPOLERR,
	N_POLERR,
	N_RXFRMSH,
	N_RXFRM,
	N_RXBYTESH,
	N_RXBYTE,
	N_TXFRMSH,
	N_TXFRM,
	N_TXBYTESH,
	N_TXBYTE,
	/* HL2 */
	N_QFULL,
	N_PART_DROP,
	N_EGR_DISABLED,
	N_NOT_REACH,
	__MAX_SJA1105ET_PORT_COUNTER,
	/* P/Q/R/S only */
	/* QLEVEL */
	QLEVEL_HWM0 = __MAX_SJA1105ET_PORT_COUNTER,
	QLEVEL_HWM1,
	QLEVEL_HWM2,
	QLEVEL_HWM3,
	QLEVEL_HWM4,
	QLEVEL_HWM5,
	QLEVEL_HWM6,
	QLEVEL_HWM7,
	QLEVEL0,
	QLEVEL1,
	QLEVEL2,
	QLEVEL3,
	QLEVEL4,
	QLEVEL5,
	QLEVEL6,
	QLEVEL7,
	/* ETHER */
	N_DROPS_NOLEARN,
	N_DROPS_NOROUTE,
	N_DROPS_ILL_DTAG,
	N_DROPS_DTAG,
	N_DROPS_SOTAG,
	N_DROPS_SITAG,
	N_DROPS_UTAG,
	N_TX_BYTES_1024_2047,
	N_TX_BYTES_512_1023,
	N_TX_BYTES_256_511,
	N_TX_BYTES_128_255,
	N_TX_BYTES_65_127,
	N_TX_BYTES_64,
	N_TX_MCAST,
	N_TX_BCAST,
	N_RX_BYTES_1024_2047,
	N_RX_BYTES_512_1023,
	N_RX_BYTES_256_511,
	N_RX_BYTES_128_255,
	N_RX_BYTES_65_127,
	N_RX_BYTES_64,
	N_RX_MCAST,
	N_RX_BCAST,
	__MAX_SJA1105PQRS_PORT_COUNTER,
};

struct sja1105_port_counter {
	enum sja1105_stats_area area;
	const char name[ETH_GSTRING_LEN];
	int offset;
	int start;
	int end;
	enum sja1105_counter_index upper;
};

static const struct sja1105_port_counter sja1105_port_counters[] = {
	/* MAC-Level Diagnostic Counters */
	[N_RUNT] = {
		.area = MAC,
		.name = "n_runt",
		.offset = 0,
		.start = 31,
		.end = 24,
	},
	[N_SOFERR] = {
		.area = MAC,
		.name = "n_soferr",
		.offset = 0x0,
		.start = 23,
		.end = 16,
	},
	[N_ALIGNERR] = {
		.area = MAC,
		.name = "n_alignerr",
		.offset = 0x0,
		.start = 15,
		.end = 8,
	},
	[N_MIIERR] = {
		.area = MAC,
		.name = "n_miierr",
		.offset = 0x0,
		.start = 7,
		.end = 0,
	},
	/* MAC-Level Diagnostic Flags */
	[TYPEERR] = {
		.area = MAC,
		.name = "typeerr",
		.offset = 0x1,
		.start = 27,
		.end = 27,
	},
	[SIZEERR] = {
		.area = MAC,
		.name = "sizeerr",
		.offset = 0x1,
		.start = 26,
		.end = 26,
	},
	[TCTIMEOUT] = {
		.area = MAC,
		.name = "tctimeout",
		.offset = 0x1,
		.start = 25,
		.end = 25,
	},
	[PRIORERR] = {
		.area = MAC,
		.name = "priorerr",
		.offset = 0x1,
		.start = 24,
		.end = 24,
	},
	[NOMASTER] = {
		.area = MAC,
		.name = "nomaster",
		.offset = 0x1,
		.start = 23,
		.end = 23,
	},
	[MEMOV] = {
		.area = MAC,
		.name = "memov",
		.offset = 0x1,
		.start = 22,
		.end = 22,
	},
	[MEMERR] = {
		.area = MAC,
		.name = "memerr",
		.offset = 0x1,
		.start = 21,
		.end = 21,
	},
	[INVTYP] = {
		.area = MAC,
		.name = "invtyp",
		.offset = 0x1,
		.start = 19,
		.end = 19,
	},
	[INTCYOV] = {
		.area = MAC,
		.name = "intcyov",
		.offset = 0x1,
		.start = 18,
		.end = 18,
	},
	[DOMERR] = {
		.area = MAC,
		.name = "domerr",
		.offset = 0x1,
		.start = 17,
		.end = 17,
	},
	[PCFBAGDROP] = {
		.area = MAC,
		.name = "pcfbagdrop",
		.offset = 0x1,
		.start = 16,
		.end = 16,
	},
	[SPCPRIOR] = {
		.area = MAC,
		.name = "spcprior",
		.offset = 0x1,
		.start = 15,
		.end = 12,
	},
	[AGEPRIOR] = {
		.area = MAC,
		.name = "ageprior",
		.offset = 0x1,
		.start = 11,
		.end = 8,
	},
	[PORTDROP] = {
		.area = MAC,
		.name = "portdrop",
		.offset = 0x1,
		.start = 6,
		.end = 6,
	},
	[LENDROP] = {
		.area = MAC,
		.name = "lendrop",
		.offset = 0x1,
		.start = 5,
		.end = 5,
	},
	[BAGDROP] = {
		.area = MAC,
		.name = "bagdrop",
		.offset = 0x1,
		.start = 4,
		.end = 4,
	},
	[POLICEERR] = {
		.area = MAC,
		.name = "policeerr",
		.offset = 0x1,
		.start = 3,
		.end = 3,
	},
	[DRPNONA664ERR] = {
		.area = MAC,
		.name = "drpnona664err",
		.offset = 0x1,
		.start = 2,
		.end = 2,
	},
	[SPCERR] = {
		.area = MAC,
		.name = "spcerr",
		.offset = 0x1,
		.start = 1,
		.end = 1,
	},
	[AGEDRP] = {
		.area = MAC,
		.name = "agedrp",
		.offset = 0x1,
		.start = 0,
		.end = 0,
	},
	/* High-Level Diagnostic Counters */
	[N_N664ERR] = {
		.area = HL1,
		.name = "n_n664err",
		.offset = 0xF,
		.start = 31,
		.end = 0,
	},
	[N_VLANERR] = {
		.area = HL1,
		.name = "n_vlanerr",
		.offset = 0xE,
		.start = 31,
		.end = 0,
	},
	[N_UNRELEASED] = {
		.area = HL1,
		.name = "n_unreleased",
		.offset = 0xD,
		.start = 31,
		.end = 0,
	},
	[N_SIZEERR] = {
		.area = HL1,
		.name = "n_sizeerr",
		.offset = 0xC,
		.start = 31,
		.end = 0,
	},
	[N_CRCERR] = {
		.area = HL1,
		.name = "n_crcerr",
		.offset = 0xB,
		.start = 31,
		.end = 0,
	},
	[N_VLNOTFOUND] = {
		.area = HL1,
		.name = "n_vlnotfound",
		.offset = 0xA,
		.start = 31,
		.end = 0,
	},
	[N_CTPOLERR] = {
		.area = HL1,
		.name = "n_ctpolerr",
		.offset = 0x9,
		.start = 31,
		.end = 0,
	},
	[N_POLERR] = {
		.area = HL1,
		.name = "n_polerr",
		.offset = 0x8,
		.start = 31,
		.end = 0,
	},
	[N_RXFRM] = {
		.area = HL1,
		.name = "n_rxfrm",
		.offset = 0x6,
		.start = 31,
		.end = 0,
		.upper = N_RXFRMSH,
	},
	[N_RXFRMSH] = {
		.area = HL1,
		.offset = 0x7,
		.start = 31,
		.end = 0,
	},
	[N_RXBYTE] = {
		.area = HL1,
		.name = "n_rxbyte",
		.offset = 0x4,
		.start = 31,
		.end = 0,
		.upper = N_RXBYTESH,
	},
	[N_RXBYTESH] = {
		.area = HL1,
		.offset = 0x5,
		.start = 31,
		.end = 0,
	},
	[N_TXFRM] = {
		.area = HL1,
		.name = "n_txfrm",
		.offset = 0x2,
		.start = 31,
		.end = 0,
		.upper = N_TXFRMSH,
	},
	[N_TXFRMSH] = {
		.area = HL1,
		.offset = 0x3,
		.start = 31,
		.end = 0,
	},
	[N_TXBYTE] = {
		.area = HL1,
		.name = "n_txbyte",
		.offset = 0x0,
		.start = 31,
		.end = 0,
		.upper = N_TXBYTESH,
	},
	[N_TXBYTESH] = {
		.area = HL1,
		.offset = 0x1,
		.start = 31,
		.end = 0,
	},
	[N_QFULL] = {
		.area = HL2,
		.name = "n_qfull",
		.offset = 0x3,
		.start = 31,
		.end = 0,
	},
	[N_PART_DROP] = {
		.area = HL2,
		.name = "n_part_drop",
		.offset = 0x2,
		.start = 31,
		.end = 0,
	},
	[N_EGR_DISABLED] = {
		.area = HL2,
		.name = "n_egr_disabled",
		.offset = 0x1,
		.start = 31,
		.end = 0,
	},
	[N_NOT_REACH] = {
		.area = HL2,
		.name = "n_not_reach",
		.offset = 0x0,
		.start = 31,
		.end = 0,
	},
	/* Queue Levels */
	[QLEVEL_HWM0] = {
		.area = QLEVEL,
		.name = "qlevel_hwm_0",
		.offset = 0x0,
		.start = 24,
		.end = 16,
	},
	[QLEVEL_HWM1] = {
		.area = QLEVEL,
		.name = "qlevel_hwm_1",
		.offset = 0x1,
		.start = 24,
		.end = 16,
	},
	[QLEVEL_HWM2] = {
		.area = QLEVEL,
		.name = "qlevel_hwm_2",
		.offset = 0x2,
		.start = 24,
		.end = 16,
	},
	[QLEVEL_HWM3] = {
		.area = QLEVEL,
		.name = "qlevel_hwm_3",
		.offset = 0x3,
		.start = 24,
		.end = 16,
	},
	[QLEVEL_HWM4] = {
		.area = QLEVEL,
		.name = "qlevel_hwm_4",
		.offset = 0x4,
		.start = 24,
		.end = 16,
	},
	[QLEVEL_HWM5] = {
		.area = QLEVEL,
		.name = "qlevel_hwm_5",
		.offset = 0x5,
		.start = 24,
		.end = 16,
	},
	[QLEVEL_HWM6] = {
		.area = QLEVEL,
		.name = "qlevel_hwm_6",
		.offset = 0x6,
		.start = 24,
		.end = 16,
	},
	[QLEVEL_HWM7] = {
		.area = QLEVEL,
		.name = "qlevel_hwm_7",
		.offset = 0x7,
		.start = 24,
		.end = 16,
	},
	[QLEVEL0] = {
		.area = QLEVEL,
		.name = "qlevel_0",
		.offset = 0x0,
		.start = 8,
		.end = 0,
	},
	[QLEVEL1] = {
		.area = QLEVEL,
		.name = "qlevel_1",
		.offset = 0x1,
		.start = 8,
		.end = 0,
	},
	[QLEVEL2] = {
		.area = QLEVEL,
		.name = "qlevel_2",
		.offset = 0x2,
		.start = 8,
		.end = 0,
	},
	[QLEVEL3] = {
		.area = QLEVEL,
		.name = "qlevel_3",
		.offset = 0x3,
		.start = 8,
		.end = 0,
	},
	[QLEVEL4] = {
		.area = QLEVEL,
		.name = "qlevel_4",
		.offset = 0x4,
		.start = 8,
		.end = 0,
	},
	[QLEVEL5] = {
		.area = QLEVEL,
		.name = "qlevel_5",
		.offset = 0x5,
		.start = 8,
		.end = 0,
	},
	[QLEVEL6] = {
		.area = QLEVEL,
		.name = "qlevel_6",
		.offset = 0x6,
		.start = 8,
		.end = 0,
	},
	[QLEVEL7] = {
		.area = QLEVEL,
		.name = "qlevel_7",
		.offset = 0x7,
		.start = 8,
		.end = 0,
	},
	/* Ether Stats */
	[N_DROPS_NOLEARN] = {
		.area = ETHER,
		.name = "n_drops_nolearn",
		.offset = 0x16,
		.start = 31,
		.end = 0,
	},
	[N_DROPS_NOROUTE] = {
		.area = ETHER,
		.name = "n_drops_noroute",
		.offset = 0x15,
		.start = 31,
		.end = 0,
	},
	[N_DROPS_ILL_DTAG] = {
		.area = ETHER,
		.name = "n_drops_ill_dtag",
		.offset = 0x14,
		.start = 31,
		.end = 0,
	},
	[N_DROPS_DTAG] = {
		.area = ETHER,
		.name = "n_drops_dtag",
		.offset = 0x13,
		.start = 31,
		.end = 0,
	},
	[N_DROPS_SOTAG] = {
		.area = ETHER,
		.name = "n_drops_sotag",
		.offset = 0x12,
		.start = 31,
		.end = 0,
	},
	[N_DROPS_SITAG] = {
		.area = ETHER,
		.name = "n_drops_sitag",
		.offset = 0x11,
		.start = 31,
		.end = 0,
	},
	[N_DROPS_UTAG] = {
		.area = ETHER,
		.name = "n_drops_utag",
		.offset = 0x10,
		.start = 31,
		.end = 0,
	},
	[N_TX_BYTES_1024_2047] = {
		.area = ETHER,
		.name = "n_tx_bytes_1024_2047",
		.offset = 0x0F,
		.start = 31,
		.end = 0,
	},
	[N_TX_BYTES_512_1023] = {
		.area = ETHER,
		.name = "n_tx_bytes_512_1023",
		.offset = 0x0E,
		.start = 31,
		.end = 0,
	},
	[N_TX_BYTES_256_511] = {
		.area = ETHER,
		.name = "n_tx_bytes_256_511",
		.offset = 0x0D,
		.start = 31,
		.end = 0,
	},
	[N_TX_BYTES_128_255] = {
		.area = ETHER,
		.name = "n_tx_bytes_128_255",
		.offset = 0x0C,
		.start = 31,
		.end = 0,
	},
	[N_TX_BYTES_65_127] = {
		.area = ETHER,
		.name = "n_tx_bytes_65_127",
		.offset = 0x0B,
		.start = 31,
		.end = 0,
	},
	[N_TX_BYTES_64] = {
		.area = ETHER,
		.name = "n_tx_bytes_64",
		.offset = 0x0A,
		.start = 31,
		.end = 0,
	},
	[N_TX_MCAST] = {
		.area = ETHER,
		.name = "n_tx_mcast",
		.offset = 0x09,
		.start = 31,
		.end = 0,
	},
	[N_TX_BCAST] = {
		.area = ETHER,
		.name = "n_tx_bcast",
		.offset = 0x08,
		.start = 31,
		.end = 0,
	},
	[N_RX_BYTES_1024_2047] = {
		.area = ETHER,
		.name = "n_rx_bytes_1024_2047",
		.offset = 0x07,
		.start = 31,
		.end = 0,
	},
	[N_RX_BYTES_512_1023] = {
		.area = ETHER,
		.name = "n_rx_bytes_512_1023",
		.offset = 0x06,
		.start = 31,
		.end = 0,
	},
	[N_RX_BYTES_256_511] = {
		.area = ETHER,
		.name = "n_rx_bytes_256_511",
		.offset = 0x05,
		.start = 31,
		.end = 0,
	},
	[N_RX_BYTES_128_255] = {
		.area = ETHER,
		.name = "n_rx_bytes_128_255",
		.offset = 0x04,
		.start = 31,
		.end = 0,
	},
	[N_RX_BYTES_65_127] = {
		.area = ETHER,
		.name = "n_rx_bytes_65_127",
		.offset = 0x03,
		.start = 31,
		.end = 0,
	},
	[N_RX_BYTES_64] = {
		.area = ETHER,
		.name = "n_rx_bytes_64",
		.offset = 0x02,
		.start = 31,
		.end = 0,
	},
	[N_RX_MCAST] = {
		.area = ETHER,
		.name = "n_rx_mcast",
		.offset = 0x01,
		.start = 31,
		.end = 0,
	},
	[N_RX_BCAST] = {
		.area = ETHER,
		.name = "n_rx_bcast",
		.offset = 0x00,
		.start = 31,
		.end = 0,
	},
};

static int sja1105_port_counter_read(struct sja1105_private *priv, int port,
				     enum sja1105_counter_index idx, u64 *ctr)
{
	int offset = sja1105_port_counters[idx].offset;
	int upper = sja1105_port_counters[idx].upper;
	int start = sja1105_port_counters[idx].start;
	int area = sja1105_port_counters[idx].area;
	int end = sja1105_port_counters[idx].end;
	u8 buf[4] = {0};
	u64 regs;
	int rc;

	regs = priv->info->regs->stats[area][port];

	rc = sja1105_xfer_buf(priv, SPI_READ, regs + offset, buf, 4);
	if (rc)
		return rc;

	sja1105_unpack(buf, ctr, start, end, 4);

	if (upper) {
		u64 tmp;

		rc = sja1105_port_counter_read(priv, port, upper, &tmp);
		if (rc)
			return rc;

		*ctr += tmp << (start + 1);
	}

	return 0;
}

void sja1105_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data)
{
	struct sja1105_private *priv = ds->priv;
	enum sja1105_counter_index max_ctr, i;
	int rc, k = 0;

	if (priv->info->device_id == SJA1105E_DEVICE_ID ||
	    priv->info->device_id == SJA1105T_DEVICE_ID)
		max_ctr = __MAX_SJA1105ET_PORT_COUNTER;
	else
		max_ctr = __MAX_SJA1105PQRS_PORT_COUNTER;

	for (i = 0; i < max_ctr; i++) {
		/* Skip the 32-bit counters without a name, they are just
		 * upper halves of another counter that _is_ reported as a
		 * 64-bit counter.
		 */
		if (!strlen(sja1105_port_counters[i].name))
			continue;

		rc = sja1105_port_counter_read(priv, port, i, &data[k++]);
		if (rc) {
			dev_err(ds->dev,
				"Failed to read port %d counters: %d\n",
				port, rc);
			break;
		}
	}
}

void sja1105_get_strings(struct dsa_switch *ds, int port,
			 u32 stringset, u8 *data)
{
	struct sja1105_private *priv = ds->priv;
	enum sja1105_counter_index max_ctr, i;
	char *p = data;

	if (stringset != ETH_SS_STATS)
		return;

	if (priv->info->device_id == SJA1105E_DEVICE_ID ||
	    priv->info->device_id == SJA1105T_DEVICE_ID)
		max_ctr = __MAX_SJA1105ET_PORT_COUNTER;
	else
		max_ctr = __MAX_SJA1105PQRS_PORT_COUNTER;

	for (i = 0; i < max_ctr; i++) {
		/* Skip the 32-bit counters without a name, they are just
		 * upper halves of another counter that _is_ reported as a
		 * 64-bit counter.
		 */
		if (!strlen(sja1105_port_counters[i].name))
			continue;

		strlcpy(p, sja1105_port_counters[i].name, ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
	}
}

int sja1105_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct sja1105_private *priv = ds->priv;
	enum sja1105_counter_index max_ctr, i;
	int sset_count = 0;

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	if (priv->info->device_id == SJA1105E_DEVICE_ID ||
	    priv->info->device_id == SJA1105T_DEVICE_ID)
		max_ctr = __MAX_SJA1105ET_PORT_COUNTER;
	else
		max_ctr = __MAX_SJA1105PQRS_PORT_COUNTER;

	for (i = 0; i < max_ctr; i++) {
		if (!strlen(sja1105_port_counters[i].name))
			continue;

		sset_count++;
	}

	return sset_count;
}

int sja1105_port_get_stats64(struct dsa_switch *ds, int port,
			     struct rtnl_link_stats64 *stats)
{
	u64 alignerr, egr_disabled, not_reach, qfull, unreleased;
	u64 vlanerr, polerr, part_drop, crcerr, sizeerr;
	u64 rxfrm, txfrm, rxbyte, txbyte, rx_mcast;
	struct sja1105_private *priv = ds->priv;
	int rc;

	rc = sja1105_port_counter_read(priv, port, N_RXFRM, &rxfrm);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_TXFRM, &txfrm);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_RXBYTE, &rxbyte);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_TXBYTE, &txbyte);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_POLERR, &polerr);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_PART_DROP, &part_drop);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_CRCERR, &crcerr);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_SIZEERR, &sizeerr);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_RX_MCAST, &rx_mcast);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_ALIGNERR, &alignerr);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_NOT_REACH, &not_reach);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_UNRELEASED, &unreleased);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_QFULL, &qfull);
	if (rc)
		return rc;
	rc = sja1105_port_counter_read(priv, port, N_EGR_DISABLED,
				       &egr_disabled);
	if (rc)
		return rc;

	/* detailed rx_errors: */
	stats->rx_length_errors = sizeerr;
	stats->rx_over_errors = sizeerr;
	stats->rx_crc_errors = crcerr;
	stats->rx_frame_errors = alignerr;
	stats->rx_missed_errors = part_drop;

	stats->rx_packets = rxfrm;
	stats->tx_packets = txfrm;
	stats->rx_bytes = rxbyte;
	stats->tx_bytes = txbyte;
	stats->rx_dropped = vlanerr + polerr + part_drop + crcerr + sizeerr;
	stats->tx_dropped = egr_disabled + not_reach + qfull + unreleased;
	stats->rx_errors = crcerr;
	stats->multicast = rx_mcast;

	return 0;
}
