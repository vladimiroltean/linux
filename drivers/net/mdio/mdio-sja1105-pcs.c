// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 NXP
 *
 * NXP SJA1105 PCS MDIO bus driver
 */
#include <linux/mdio/mdio-sja1105-pcs.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/pcs/pcs-xpcs.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

static int sja1105_pcs_mdio_read_c45(struct mii_bus *bus, int phy, int mmd,
				     int reg)
{
	struct regmap *regmap = bus->priv;
	unsigned int addr;
	u32 val;
	int err;

	if (mmd != MDIO_MMD_VEND1 && mmd != MDIO_MMD_VEND2)
		return 0xffff;

	if (mmd == MDIO_MMD_VEND2 && reg == MII_PHYSID1)
		return NXP_SJA1105_XPCS_ID >> 16;
	if (mmd == MDIO_MMD_VEND2 && reg == MII_PHYSID2)
		return NXP_SJA1105_XPCS_ID & GENMASK(15, 0);

	addr = ((mmd - MDIO_MMD_VEND1) << 16) | reg;

	err = regmap_read(regmap, addr, &val);
	if (err)
		return err;

	return val & 0xffff;
}

static int sja1105_pcs_mdio_write_c45(struct mii_bus *bus, int phy, int mmd,
				      int reg, u16 val)
{
	struct regmap *regmap = bus->priv;
	unsigned int addr;

	if (mmd != MDIO_MMD_VEND1 && mmd != MDIO_MMD_VEND2)
		return -EINVAL;

	addr = ((mmd - MDIO_MMD_VEND1) << 16) | reg;

	return regmap_write(regmap, addr, val);
}

static int sja1105_pcs_mdio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct resource *res;
	struct mii_bus *bus;
	int err;

	if (!dev->of_node || !dev->parent)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (!res)
		return -ENODEV;

	regmap = dev_get_regmap(dev->parent, res->name);
	if (!regmap)
		return -ENODEV;

	bus = mdiobus_alloc_size(0);
	if (!bus)
		return -ENOMEM;

	bus->name = "SJA1105 PCS MDIO bus";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));
	bus->read_c45 = sja1105_pcs_mdio_read_c45;
	bus->write_c45 = sja1105_pcs_mdio_write_c45;
	bus->parent = dev;
	bus->priv = regmap;

	err = of_mdiobus_register(bus, dev->of_node);
	if (err)
		goto err_free_bus;

	platform_set_drvdata(pdev, bus);

	return 0;

err_free_bus:
	mdiobus_free(bus);

	return err;
}

static int sja1105_pcs_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus);
	mdiobus_free(bus);

	return 0;
}

static struct platform_driver sja1105_pcs_mdio_driver = {
	.probe = sja1105_pcs_mdio_probe,
	.remove = sja1105_pcs_mdio_remove,
	.driver = {
		.name = MDIO_SJA1105_PCS_NAME,
	},
};

module_platform_driver(sja1105_pcs_mdio_driver);

MODULE_DESCRIPTION("NXP SJA1105 PCS MDIO bus driver");
MODULE_AUTHOR("Vladimir Oltean <vladimir.oltean@nxp.com>");
MODULE_LICENSE("GPL");
