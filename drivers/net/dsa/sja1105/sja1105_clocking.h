/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_CLOCKING_H
#define _SJA1105_CLOCKING_H

struct sja1105_private;

typedef enum {
	XMII_MAC = 0,
	XMII_PHY = 1,
} sja1105_mii_role_t;

typedef enum {
	XMII_MODE_MII		= 0,
	XMII_MODE_RMII		= 1,
	XMII_MODE_RGMII		= 2,
	XMII_MODE_SGMII		= 3,
} sja1105_phy_interface_t;

int sja1105pqrs_setup_rgmii_delay(const void *ctx, int port);
int sja1110_setup_rgmii_delay(const void *ctx, int port);
int sja1105_clocking_setup_port(struct sja1105_private *priv, int port);
int sja1105_clocking_setup(struct sja1105_private *priv);
int sja1110_disable_microcontroller(struct sja1105_private *priv);

#endif
