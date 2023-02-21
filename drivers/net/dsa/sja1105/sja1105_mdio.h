/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2022 NXP
 */
#ifndef _SJA1105_MDIO_H
#define _SJA1105_MDIO_H

struct dsa_switch;
struct mii_bus;

int sja1105_mdiobus_register(struct dsa_switch *ds);
void sja1105_mdiobus_unregister(struct dsa_switch *ds);
int sja1105_pcs_mdio_read_c45(struct mii_bus *bus, int phy, int mmd, int reg);
int sja1105_pcs_mdio_write_c45(struct mii_bus *bus, int phy, int mmd, int reg,
			       u16 val);
int sja1110_pcs_mdio_read_c45(struct mii_bus *bus, int phy, int mmd, int reg);
int sja1110_pcs_mdio_write_c45(struct mii_bus *bus, int phy, int mmd, int reg,
			       u16 val);

#endif
