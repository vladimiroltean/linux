// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Distributed Switch Architecture loopback driver
 *
 * Copyright (C) 2016, Florian Fainelli <f.fainelli@gmail.com>
 */

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/export.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/types.h>
#include <net/dsa.h>

#include "dsa_loop.h"
#include "dsa_loop_bus_main.h"

#define DSA_LOOP_NUM_PORTS	6
#define DSA_LOOP_CPU_PORT	(DSA_LOOP_NUM_PORTS - 1)
#define NUM_FIXED_PHYS		(DSA_LOOP_NUM_PORTS - 2)

struct dsa_loop_mib_entry {
	char name[ETH_GSTRING_LEN];
	unsigned long val;
};

enum dsa_loop_mib_counters {
	DSA_LOOP_PHY_READ_OK,
	DSA_LOOP_PHY_READ_ERR,
	DSA_LOOP_PHY_WRITE_OK,
	DSA_LOOP_PHY_WRITE_ERR,
	__DSA_LOOP_CNT_MAX,
};

struct dsa_loop_port {
	struct dsa_loop_mib_entry mib[__DSA_LOOP_CNT_MAX];
	u16 pvid;
	int mtu;
};

struct dsa_loop_priv {
	struct mii_bus	*bus;
	unsigned int	port_base;
	struct dsa_loop_port ports[DSA_MAX_PORTS];
};

static struct dsa_loop_mib_entry dsa_loop_mibs[] = {
	[DSA_LOOP_PHY_READ_OK]	= { "phy_read_ok", },
	[DSA_LOOP_PHY_READ_ERR]	= { "phy_read_err", },
	[DSA_LOOP_PHY_WRITE_OK] = { "phy_write_ok", },
	[DSA_LOOP_PHY_WRITE_ERR] = { "phy_write_err", },
};

static struct phy_device *phydevs[PHY_MAX_ADDR];

static enum dsa_tag_protocol dsa_loop_get_protocol(struct dsa_switch *ds,
						   int port,
						   enum dsa_tag_protocol mp)
{
	struct dsa_loop_device *dld = to_dsa_loop_device(ds->dev);
	const struct dsa_loop_pdata *pdata = dld->dev.platform_data;

	return pdata->tag_proto;
}

static int dsa_loop_setup(struct dsa_switch *ds)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	for (i = 0; i < ds->num_ports; i++)
		memcpy(ps->ports[i].mib, dsa_loop_mibs,
		       sizeof(dsa_loop_mibs));

	dev_dbg(ds->dev, "%s\n", __func__);

	return 0;
}

static int dsa_loop_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS && sset != ETH_SS_PHY_STATS)
		return 0;

	return __DSA_LOOP_CNT_MAX;
}

static void dsa_loop_get_strings(struct dsa_switch *ds, int port,
				 u32 stringset, uint8_t *data)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	if (stringset != ETH_SS_STATS && stringset != ETH_SS_PHY_STATS)
		return;

	for (i = 0; i < __DSA_LOOP_CNT_MAX; i++)
		ethtool_puts(&data, ps->ports[port].mib[i].name);
}

static void dsa_loop_get_ethtool_stats(struct dsa_switch *ds, int port,
				       uint64_t *data)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	for (i = 0; i < __DSA_LOOP_CNT_MAX; i++)
		data[i] = ps->ports[port].mib[i].val;
}

static int dsa_loop_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	int ret;

	ret = mdiobus_read_nested(bus, ps->port_base + port, regnum);
	if (ret < 0)
		ps->ports[port].mib[DSA_LOOP_PHY_READ_ERR].val++;
	else
		ps->ports[port].mib[DSA_LOOP_PHY_READ_OK].val++;

	return ret;
}

static int dsa_loop_phy_write(struct dsa_switch *ds, int port,
			      int regnum, u16 value)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	int ret;

	ret = mdiobus_write_nested(bus, ps->port_base + port, regnum, value);
	if (ret < 0)
		ps->ports[port].mib[DSA_LOOP_PHY_WRITE_ERR].val++;
	else
		ps->ports[port].mib[DSA_LOOP_PHY_WRITE_OK].val++;

	return ret;
}

static int dsa_loop_port_change_mtu(struct dsa_switch *ds, int port,
				    int new_mtu)
{
	struct dsa_loop_priv *priv = ds->priv;

	priv->ports[port].mtu = new_mtu;

	return 0;
}

static int dsa_loop_port_max_mtu(struct dsa_switch *ds, int port)
{
	return ETH_MAX_MTU;
}

static void dsa_loop_phylink_get_caps(struct dsa_switch *dsa, int port,
				      struct phylink_config *config)
{
	bitmap_fill(config->supported_interfaces, PHY_INTERFACE_MODE_MAX);
	__clear_bit(PHY_INTERFACE_MODE_NA, config->supported_interfaces);
	config->mac_capabilities = ~0;
}

static int dsa_loop_port_enable(struct dsa_switch *ds, int port,
				struct phy_device *phy)
{
	struct dsa_loop_device *dld = to_dsa_loop_device(ds->dev);

	return dsa_loop_bus_port_enable(dld, port);
}

static void dsa_loop_port_disable(struct dsa_switch *ds, int port)
{
	struct dsa_loop_device *dld = to_dsa_loop_device(ds->dev);

	dsa_loop_bus_port_disable(dld, port);
}

static const struct dsa_switch_ops dsa_loop_ops = {
	.get_tag_protocol	= dsa_loop_get_protocol,
	.setup			= dsa_loop_setup,
	.get_strings		= dsa_loop_get_strings,
	.get_ethtool_stats	= dsa_loop_get_ethtool_stats,
	.get_sset_count		= dsa_loop_get_sset_count,
	.get_ethtool_phy_stats	= dsa_loop_get_ethtool_stats,
	.phy_read		= dsa_loop_phy_read,
	.phy_write		= dsa_loop_phy_write,
	.port_enable		= dsa_loop_port_enable,
	.port_disable		= dsa_loop_port_disable,
	.port_change_mtu	= dsa_loop_port_change_mtu,
	.port_max_mtu		= dsa_loop_port_max_mtu,
	.phylink_get_caps	= dsa_loop_phylink_get_caps,
};

static int dsa_loop_drv_probe(struct dsa_loop_device *dld)
{
	const struct dsa_loop_pdata *pdata = dld->dev.platform_data;
	struct dsa_loop_priv *ps;
	struct dsa_switch *ds;
	int ret;

	ds = devm_kzalloc(&dld->dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	ds->dev = &dld->dev;
	ds->num_ports = pdata->num_ports;

	ps = devm_kzalloc(&dld->dev, sizeof(*ps), GFP_KERNEL);
	if (!ps)
		return -ENOMEM;

	ds->dev = &dld->dev;
	ds->ops = &dsa_loop_ops;
	ds->priv = ps;
	ps->bus = mdio_find_bus("fixed-0");
	if (!ps->bus)
		return -EPROBE_DEFER;

	dev_set_drvdata(&dld->dev, ds);

	ret = dsa_register_switch(ds);
	if (ret)
		put_device(&ps->bus->dev);

	return ret;
}

static void dsa_loop_drv_remove(struct dsa_loop_device *dld)
{
	struct dsa_switch *ds = dev_get_drvdata(&dld->dev);
	struct dsa_loop_priv *ps;

	if (!ds)
		return;

	ps = ds->priv;

	dsa_unregister_switch(ds);
	put_device(&ps->bus->dev);
}

static void dsa_loop_drv_shutdown(struct dsa_loop_device *dld)
{
	struct dsa_switch *ds = dev_get_drvdata(&dld->dev);

	if (!ds)
		return;

	dsa_switch_shutdown(ds);

	dev_set_drvdata(&dld->dev, NULL);
}

static struct dsa_loop_driver dsa_loop_drv = {
	.driver	= {
		.name	= "dsa-loop",
	},
	.probe	= dsa_loop_drv_probe,
	.remove	= dsa_loop_drv_remove,
	.shutdown = dsa_loop_drv_shutdown,
};

static void dsa_loop_phydevs_unregister(void)
{
	for (int i = 0; i < NUM_FIXED_PHYS; i++) {
		if (!IS_ERR(phydevs[i]))
			fixed_phy_unregister(phydevs[i]);
	}
}

static int __init dsa_loop_init(void)
{
	unsigned int i;
	int ret;

	for (i = 0; i < NUM_FIXED_PHYS; i++)
		phydevs[i] = fixed_phy_register_100fd();

	ret = dsa_loop_driver_register(&dsa_loop_drv);
	if (ret) {
		pr_err("Failed to register dsa-loop driver: %pe\n",
		       ERR_PTR(ret));
		dsa_loop_phydevs_unregister();
	}

	return ret;
}
module_init(dsa_loop_init);

static void __exit dsa_loop_exit(void)
{
	dsa_loop_driver_unregister(&dsa_loop_drv);
	dsa_loop_phydevs_unregister();
}
module_exit(dsa_loop_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Fainelli");
MODULE_DESCRIPTION("DSA loopback driver");
