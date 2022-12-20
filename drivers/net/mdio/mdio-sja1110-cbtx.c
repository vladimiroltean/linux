// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 NXP
 *
 * NXP SJA1110 100BASE-T1 MDIO bus driver
 */
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct sja1110_base_tx_private {
	struct regmap *regmap;
	struct mii_bus *bus;
};

static int sja1110_base_tx_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct sja1110_base_tx_private *priv = bus->priv;
	struct regmap *regmap = priv->regmap;
	u32 val;
	int err;

	err = regmap_read(regmap, reg * 4, &val);
	if (err)
		return err;

	return val & 0xffff;
}

static int sja1110_base_tx_mdio_write(struct mii_bus *bus, int phy, int reg,
				      u16 val)
{
	struct sja1110_base_tx_private *priv = bus->priv;
	struct regmap *regmap = priv->regmap;

	return regmap_write(regmap, reg * 4, val);
}

static int sja1110_base_tx_mdio_probe(struct platform_device *pdev)
{
	struct sja1110_base_tx_private *priv;
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

	bus = mdiobus_alloc_size(sizeof(*priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "SJA1110 100base-TX MDIO bus";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));
	bus->read = sja1110_base_tx_mdio_read;
	bus->write = sja1110_base_tx_mdio_write;
	bus->parent = dev;
	priv = bus->priv;
	priv->regmap = regmap;

	err = of_mdiobus_register(bus, dev->of_node);
	if (err)
		goto err_free_bus;

	priv->bus = bus;
	platform_set_drvdata(pdev, priv);

	return 0;

err_free_bus:
	mdiobus_free(bus);

	return err;
}

static int sja1110_base_tx_mdio_remove(struct platform_device *pdev)
{
	struct sja1110_base_tx_private *priv = platform_get_drvdata(pdev);

	mdiobus_unregister(priv->bus);
	mdiobus_free(priv->bus);

	return 0;
}

static const struct of_device_id sja1110_base_tx_mdio_match[] = {
	{ .compatible = "nxp,sja1110-base-tx-mdio", },
	{},
};
MODULE_DEVICE_TABLE(of, sja1110_base_tx_mdio_match);

static struct platform_driver sja1110_base_tx_mdio_driver = {
	.probe = sja1110_base_tx_mdio_probe,
	.remove = sja1110_base_tx_mdio_remove,
	.driver = {
		.name = "sja1110-base-tx-mdio",
		.of_match_table = sja1110_base_tx_mdio_match,
	},
};

module_platform_driver(sja1110_base_tx_mdio_driver);

MODULE_DESCRIPTION("NXP SJA1110 100BASE-TX MDIO bus driver");
MODULE_AUTHOR("Vladimir Oltean <vladimir.oltean@nxp.com>");
MODULE_LICENSE("GPL");
