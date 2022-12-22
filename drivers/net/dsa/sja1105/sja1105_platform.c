// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 NXP
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/module.h>
#include "sja1105.h"

static int sja1105_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sja1105_soc *soc;

	if (!dev->parent || !dev->of_node)
		return -ENODEV;

	soc = dev_get_drvdata(dev->parent);

	return sja1105_switch_probe(dev, soc);
}

static int sja1105_platform_remove(struct platform_device *pdev)
{
	sja1105_switch_remove(&pdev->dev);
}

static void sja1105_platform_shutdown(struct platform_device *pdev)
{
	sja1105_switch_shutdown(&pdev->dev);
}

static const struct of_device_id sja1105_platform_of_match[] = {
	{ .compatible = "nxp,sja1105e" },
	{ .compatible = "nxp,sja1105t" },
	{ .compatible = "nxp,sja1105p" },
	{ .compatible = "nxp,sja1105q" },
	{ .compatible = "nxp,sja1105r" },
	{ .compatible = "nxp,sja1105s" },
	{ .compatible = "nxp,sja1110a" },
	{ .compatible = "nxp,sja1110b" },
	{ .compatible = "nxp,sja1110c" },
	{ .compatible = "nxp,sja1110d" },
	{ },
};
MODULE_DEVICE_TABLE(of, sja1105_platform_of_match);

static struct platform_driver sja1105_platform_driver = {
	.probe		= sja1105_platform_probe,
	.remove		= sja1105_platform_remove,
	.shutdown	= sja1105_platform_shutdown,
	.driver = {
		.name		= "sja1105-switch",
		.of_match_table	= of_match_ptr(sja1105_platform_of_match),
	},
};
module_platform_driver(sja1105_platform_driver);

MODULE_AUTHOR("Vladimir Oltean <vladimir.oltean@nxp.com>");
MODULE_DESCRIPTION("SJA1105 Switch driver");
MODULE_LICENSE("GPL v2");
