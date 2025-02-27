// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Central DSA module
 *
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2013 Florian Fainelli <florian@openwrt.org>
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/if_hsr.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <net/dsa_stubs.h>

#include "conduit.h"
#include "devlink.h"
#include "dsa.h"
#include "netlink.h"
#include "port.h"
#include "switch.h"
#include "tag.h"
#include "tree.h"
#include "user.h"

static DEFINE_MUTEX(dsa2_mutex);

static struct workqueue_struct *dsa_owq;

bool dsa_schedule_work(struct work_struct *work)
{
	return queue_work(dsa_owq, work);
}

void dsa_flush_workqueue(void)
{
	flush_workqueue(dsa_owq);
}
EXPORT_SYMBOL_GPL(dsa_flush_workqueue);

int dsa_port_setup(struct dsa_port *dp)
{
	bool dsa_port_link_registered = false;
	struct dsa_switch *ds = dp->ds;
	bool dsa_port_enabled = false;
	int err = 0;

	if (dp->setup)
		return 0;

	err = dsa_port_devlink_setup(dp);
	if (err)
		return err;

	switch (dp->type) {
	case DSA_PORT_TYPE_UNUSED:
		dsa_port_disable(dp);
		break;
	case DSA_PORT_TYPE_CPU:
		if (dp->dn) {
			err = dsa_shared_port_link_register_of(dp);
			if (err)
				break;
			dsa_port_link_registered = true;
		} else {
			dev_warn(ds->dev,
				 "skipping link registration for CPU port %d\n",
				 dp->index);
		}

		err = dsa_port_enable(dp, NULL);
		if (err)
			break;
		dsa_port_enabled = true;

		break;
	case DSA_PORT_TYPE_DSA:
		if (dp->dn) {
			err = dsa_shared_port_link_register_of(dp);
			if (err)
				break;
			dsa_port_link_registered = true;
		} else {
			dev_warn(ds->dev,
				 "skipping link registration for DSA port %d\n",
				 dp->index);
		}

		err = dsa_port_enable(dp, NULL);
		if (err)
			break;
		dsa_port_enabled = true;

		break;
	case DSA_PORT_TYPE_USER:
		of_get_mac_address(dp->dn, dp->mac);
		err = dsa_user_create(dp);
		break;
	}

	if (err && dsa_port_enabled)
		dsa_port_disable(dp);
	if (err && dsa_port_link_registered)
		dsa_shared_port_link_unregister_of(dp);
	if (err) {
		dsa_port_devlink_teardown(dp);
		return err;
	}

	dp->setup = true;

	return 0;
}

void dsa_port_teardown(struct dsa_port *dp)
{
	if (!dp->setup)
		return;

	switch (dp->type) {
	case DSA_PORT_TYPE_UNUSED:
		break;
	case DSA_PORT_TYPE_CPU:
		dsa_port_disable(dp);
		if (dp->dn)
			dsa_shared_port_link_unregister_of(dp);
		break;
	case DSA_PORT_TYPE_DSA:
		dsa_port_disable(dp);
		if (dp->dn)
			dsa_shared_port_link_unregister_of(dp);
		break;
	case DSA_PORT_TYPE_USER:
		if (dp->user) {
			dsa_user_destroy(dp->user);
			dp->user = NULL;
		}
		break;
	}

	dsa_port_devlink_teardown(dp);

	dp->setup = false;
}

int dsa_port_setup_as_unused(struct dsa_port *dp)
{
	dp->type = DSA_PORT_TYPE_UNUSED;
	return dsa_port_setup(dp);
}

static int dsa_switch_setup_tag_protocol(struct dsa_switch *ds)
{
	const struct dsa_device_ops *tag_ops = ds->dst->tag_ops;
	struct dsa_switch_tree *dst = ds->dst;
	int err;

	if (tag_ops->proto == dst->default_proto)
		goto connect;

	rtnl_lock();
	err = ds->ops->change_tag_protocol(ds, tag_ops->proto);
	rtnl_unlock();
	if (err) {
		dev_err(ds->dev, "Unable to use tag protocol \"%s\": %pe\n",
			tag_ops->name, ERR_PTR(err));
		return err;
	}

connect:
	if (tag_ops->connect) {
		err = tag_ops->connect(ds);
		if (err)
			return err;
	}

	if (ds->ops->connect_tag_protocol) {
		err = ds->ops->connect_tag_protocol(ds, tag_ops->proto);
		if (err) {
			dev_err(ds->dev,
				"Unable to connect to tag protocol \"%s\": %pe\n",
				tag_ops->name, ERR_PTR(err));
			goto disconnect;
		}
	}

	return 0;

disconnect:
	if (tag_ops->disconnect)
		tag_ops->disconnect(ds);

	return err;
}

static void dsa_switch_teardown_tag_protocol(struct dsa_switch *ds)
{
	const struct dsa_device_ops *tag_ops = ds->dst->tag_ops;

	if (tag_ops->disconnect)
		tag_ops->disconnect(ds);
}

int dsa_switch_setup(struct dsa_switch *ds)
{
	int err;

	if (ds->setup)
		return 0;

	/* Initialize ds->phys_mii_mask before registering the user MDIO bus
	 * driver and before ops->setup() has run, since the switch drivers and
	 * the user MDIO bus driver rely on these values for probing PHY
	 * devices or not
	 */
	ds->phys_mii_mask |= dsa_user_ports(ds);

	err = dsa_switch_devlink_alloc(ds);
	if (err)
		return err;

	err = dsa_switch_register_notifier(ds);
	if (err)
		goto devlink_free;

	ds->configure_vlan_while_not_filtering = true;

	err = ds->ops->setup(ds);
	if (err < 0)
		goto unregister_notifier;

	err = dsa_switch_setup_tag_protocol(ds);
	if (err)
		goto teardown;

	if (!ds->user_mii_bus && ds->ops->phy_read) {
		ds->user_mii_bus = mdiobus_alloc();
		if (!ds->user_mii_bus) {
			err = -ENOMEM;
			goto teardown;
		}

		dsa_user_mii_bus_init(ds);

		err = mdiobus_register(ds->user_mii_bus);
		if (err < 0)
			goto free_user_mii_bus;
	}

	dsa_switch_devlink_register(ds);

	ds->setup = true;
	return 0;

free_user_mii_bus:
	if (ds->user_mii_bus && ds->ops->phy_read)
		mdiobus_free(ds->user_mii_bus);
teardown:
	if (ds->ops->teardown)
		ds->ops->teardown(ds);
unregister_notifier:
	dsa_switch_unregister_notifier(ds);
devlink_free:
	dsa_switch_devlink_free(ds);
	return err;
}

void dsa_switch_teardown(struct dsa_switch *ds)
{
	if (!ds->setup)
		return;

	dsa_switch_devlink_unregister(ds);

	if (ds->user_mii_bus && ds->ops->phy_read) {
		mdiobus_unregister(ds->user_mii_bus);
		mdiobus_free(ds->user_mii_bus);
		ds->user_mii_bus = NULL;
	}

	dsa_switch_teardown_tag_protocol(ds);

	if (ds->ops->teardown)
		ds->ops->teardown(ds);

	dsa_switch_unregister_notifier(ds);

	dsa_switch_devlink_free(ds);

	ds->setup = false;
}

static struct dsa_port *dsa_port_touch(struct dsa_switch *ds, int index)
{
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_port *dp;

	dp = kzalloc_obj(*dp);
	if (!dp)
		return NULL;

	dp->ds = ds;
	dp->index = index;

	mutex_init(&dp->addr_lists_lock);
	mutex_init(&dp->vlans_lock);
	INIT_LIST_HEAD(&dp->fdbs);
	INIT_LIST_HEAD(&dp->mdbs);
	INIT_LIST_HEAD(&dp->vlans); /* also initializes &dp->user_vlans */
	INIT_LIST_HEAD(&dp->list);
	list_add_tail(&dp->list, &dst->ports);

	return dp;
}

static int dsa_port_parse_user(struct dsa_port *dp, const char *name)
{
	dp->type = DSA_PORT_TYPE_USER;
	dp->name = name;

	return 0;
}

static int dsa_port_parse_dsa(struct dsa_port *dp)
{
	dp->type = DSA_PORT_TYPE_DSA;

	return 0;
}

static enum dsa_tag_protocol dsa_get_tag_protocol(struct dsa_port *dp,
						  struct net_device *conduit)
{
	enum dsa_tag_protocol tag_protocol = DSA_TAG_PROTO_NONE;
	struct dsa_switch *mds, *ds = dp->ds;
	unsigned int mdp_upstream;
	struct dsa_port *mdp;

	/* It is possible to stack DSA switches onto one another when that
	 * happens the switch driver may want to know if its tagging protocol
	 * is going to work in such a configuration.
	 */
	if (dsa_user_dev_check(conduit)) {
		mdp = dsa_user_to_port(conduit);
		mds = mdp->ds;
		mdp_upstream = dsa_upstream_port(mds, mdp->index);
		tag_protocol = mds->ops->get_tag_protocol(mds, mdp_upstream,
							  DSA_TAG_PROTO_NONE);
	}

	/* If the conduit device is not itself a DSA user in a disjoint DSA
	 * tree, then return immediately.
	 */
	return ds->ops->get_tag_protocol(ds, dp->index, tag_protocol);
}

static int dsa_port_parse_cpu(struct dsa_port *dp, struct net_device *conduit,
			      const char *user_protocol)
{
	const struct dsa_device_ops *tag_ops = NULL;
	struct dsa_switch *ds = dp->ds;
	struct dsa_switch_tree *dst = ds->dst;
	enum dsa_tag_protocol default_proto;

	/* Find out which protocol the switch would prefer. */
	default_proto = dsa_get_tag_protocol(dp, conduit);
	if (dst->default_proto) {
		if (dst->default_proto != default_proto) {
			dev_err(ds->dev,
				"A DSA switch tree can have only one tagging protocol\n");
			return -EINVAL;
		}
	} else {
		dst->default_proto = default_proto;
	}

	/* See if the user wants to override that preference. */
	if (user_protocol) {
		if (!ds->ops->change_tag_protocol) {
			dev_err(ds->dev, "Tag protocol cannot be modified\n");
			return -EINVAL;
		}

		tag_ops = dsa_tag_driver_get_by_name(user_protocol);
		if (IS_ERR(tag_ops)) {
			dev_warn(ds->dev,
				 "Failed to find a tagging driver for protocol %s, using default\n",
				 user_protocol);
			tag_ops = NULL;
		}
	}

	if (!tag_ops)
		tag_ops = dsa_tag_driver_get_by_id(default_proto);

	if (IS_ERR(tag_ops)) {
		if (PTR_ERR(tag_ops) == -ENOPROTOOPT)
			return -EPROBE_DEFER;

		dev_warn(ds->dev, "No tagger for this switch\n");
		return PTR_ERR(tag_ops);
	}

	if (dst->tag_ops) {
		if (dst->tag_ops != tag_ops) {
			dev_err(ds->dev,
				"A DSA switch tree can have only one tagging protocol\n");

			dsa_tag_driver_put(tag_ops);
			return -EINVAL;
		}

		/* In the case of multiple CPU ports per switch, the tagging
		 * protocol is still reference-counted only per switch tree.
		 */
		dsa_tag_driver_put(tag_ops);
	} else {
		dst->tag_ops = tag_ops;
	}

	dp->conduit = conduit;
	dp->type = DSA_PORT_TYPE_CPU;
	dsa_port_set_tag_protocol(dp, dst->tag_ops);
	dp->dst = dst;

	/* At this point, the tree may be configured to use a different
	 * tagger than the one chosen by the switch driver during
	 * .setup, in the case when a user selects a custom protocol
	 * through the DT.
	 *
	 * This is resolved by syncing the driver with the tree in
	 * dsa_switch_setup_tag_protocol once .setup has run and the
	 * driver is ready to accept calls to .change_tag_protocol. If
	 * the driver does not support the custom protocol at that
	 * point, the tree is wholly rejected, thereby ensuring that the
	 * tree and driver are always in agreement on the protocol to
	 * use.
	 */
	return 0;
}

static int dsa_port_parse_of(struct dsa_port *dp, struct device_node *dn)
{
	struct device_node *ethernet = of_parse_phandle(dn, "ethernet", 0);
	const char *name = of_get_property(dn, "label", NULL);
	bool link = of_property_present(dn, "link");

	dp->dn = dn;

	if (ethernet) {
		struct net_device *conduit;
		const char *user_protocol;
		int err;

		rtnl_lock();
		conduit = of_find_net_device_by_node(ethernet);
		of_node_put(ethernet);
		if (!conduit) {
			rtnl_unlock();
			return -EPROBE_DEFER;
		}

		netdev_hold(conduit, &dp->conduit_tracker, GFP_KERNEL);
		put_device(&conduit->dev);
		rtnl_unlock();

		user_protocol = of_get_property(dn, "dsa-tag-protocol", NULL);
		err = dsa_port_parse_cpu(dp, conduit, user_protocol);
		if (err)
			netdev_put(conduit, &dp->conduit_tracker);
		return err;
	}

	if (link)
		return dsa_port_parse_dsa(dp);

	return dsa_port_parse_user(dp, name);
}

static int dsa_switch_parse_ports_of(struct dsa_switch *ds,
				     struct device_node *dn)
{
	struct device_node *ports, *port;
	struct dsa_port *dp;
	int err = 0;
	u32 reg;

	ports = of_get_child_by_name(dn, "ports");
	if (!ports) {
		/* The second possibility is "ethernet-ports" */
		ports = of_get_child_by_name(dn, "ethernet-ports");
		if (!ports) {
			dev_err(ds->dev, "no ports child node found\n");
			return -EINVAL;
		}
	}

	for_each_available_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &reg);
		if (err) {
			of_node_put(port);
			goto out_put_node;
		}

		if (reg >= ds->num_ports) {
			dev_err(ds->dev, "port %pOF index %u exceeds num_ports (%u)\n",
				port, reg, ds->num_ports);
			of_node_put(port);
			err = -EINVAL;
			goto out_put_node;
		}

		dp = dsa_to_port(ds, reg);

		err = dsa_port_parse_of(dp, port);
		if (err) {
			of_node_put(port);
			goto out_put_node;
		}
	}

out_put_node:
	of_node_put(ports);
	return err;
}

static int dsa_switch_touch_ports(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	int port;

	for (port = 0; port < ds->num_ports; port++) {
		dp = dsa_port_touch(ds, port);
		if (!dp)
			return -ENOMEM;
	}

	return 0;
}

static void dsa_switch_release_ports(struct dsa_switch *ds)
{
	struct dsa_mac_addr *a, *tmp;
	struct dsa_port *dp, *next;
	struct dsa_vlan *v, *n;

	dsa_switch_for_each_port_safe(dp, next, ds) {
		if (dsa_port_is_cpu(dp) && dp->conduit)
			netdev_put(dp->conduit, &dp->conduit_tracker);

		/* These are either entries that upper layers lost track of
		 * (probably due to bugs), or installed through interfaces
		 * where one does not necessarily have to remove them, like
		 * ndo_dflt_fdb_add().
		 */
		list_for_each_entry_safe(a, tmp, &dp->fdbs, list) {
			dev_info(ds->dev,
				 "Cleaning up unicast address %pM vid %u from port %d\n",
				 a->addr, a->vid, dp->index);
			list_del(&a->list);
			kfree(a);
		}

		list_for_each_entry_safe(a, tmp, &dp->mdbs, list) {
			dev_info(ds->dev,
				 "Cleaning up multicast address %pM vid %u from port %d\n",
				 a->addr, a->vid, dp->index);
			list_del(&a->list);
			kfree(a);
		}

		/* These are entries that upper layers have lost track of,
		 * probably due to bugs, but also due to dsa_port_do_vlan_del()
		 * having failed and the VLAN entry still lingering on.
		 */
		list_for_each_entry_safe(v, n, &dp->vlans, list) {
			dev_info(ds->dev,
				 "Cleaning up vid %u from port %d\n",
				 v->vid, dp->index);
			list_del(&v->list);
			kfree(v);
		}

		list_del(&dp->list);
		kfree(dp);
	}
}

static int dev_is_class(struct device *dev, const void *class)
{
	if (dev->class != NULL && !strcmp(dev->class->name, class))
		return 1;

	return 0;
}

static struct device *dev_find_class(struct device *parent, char *class)
{
	if (dev_is_class(parent, class)) {
		get_device(parent);
		return parent;
	}

	return device_find_child(parent, class, dev_is_class);
}

static int dsa_port_parse(struct dsa_port *dp, const char *name,
			  struct device *dev)
{
	if (!strcmp(name, "cpu")) {
		struct net_device *conduit;
		struct device *d;
		int err;

		rtnl_lock();
		d = dev_find_class(dev, "net");
		if (!d) {
			rtnl_unlock();
			return -EPROBE_DEFER;
		}

		conduit = to_net_dev(d);
		netdev_hold(conduit, &dp->conduit_tracker, GFP_KERNEL);
		put_device(d);
		rtnl_unlock();

		err = dsa_port_parse_cpu(dp, conduit, NULL);
		if (err)
			netdev_put(conduit, &dp->conduit_tracker);
		return err;
	}

	if (!strcmp(name, "dsa"))
		return dsa_port_parse_dsa(dp);

	return dsa_port_parse_user(dp, name);
}

static int dsa_switch_parse_ports(struct dsa_switch *ds,
				  struct dsa_chip_data *cd)
{
	bool valid_name_found = false;
	struct dsa_port *dp;
	struct device *dev;
	const char *name;
	unsigned int i;
	int err;

	for (i = 0; i < DSA_MAX_PORTS; i++) {
		name = cd->port_names[i];
		dev = cd->netdev[i];
		dp = dsa_to_port(ds, i);

		if (!name)
			continue;

		err = dsa_port_parse(dp, name, dev);
		if (err)
			return err;

		valid_name_found = true;
	}

	if (!valid_name_found && i == DSA_MAX_PORTS)
		return -EINVAL;

	return 0;
}

static int dsa_switch_probe(struct dsa_switch *ds)
{
	struct dsa_chip_data *pdata;
	struct device_node *np;
	int err;

	if (!ds->dev)
		return -ENODEV;

	pdata = ds->dev->platform_data;
	np = ds->dev->of_node;

	if (!ds->num_ports)
		return -EINVAL;

	if (!pdata && !np)
		return -ENODEV;

	if (pdata)
		ds->cd = pdata;

	ds->dst = dsa_switch_get_tree(ds);
	if (IS_ERR(ds->dst))
		return PTR_ERR(ds->dst);

	err = dsa_switch_touch_ports(ds);
	if (err)
		goto out_put_tree;

	if (np)
		err = dsa_switch_parse_ports_of(ds, np);
	else
		err = dsa_switch_parse_ports(ds, pdata);
	if (err)
		goto out_release_ports;

	err = dsa_tree_setup(ds->dst);
	if (err)
		goto out_release_ports;

	return 0;

out_release_ports:
	dsa_switch_release_ports(ds);
out_put_tree:
	dsa_switch_put_tree(ds);

	return err;
}

int dsa_register_switch(struct dsa_switch *ds)
{
	int err;

	mutex_lock(&dsa2_mutex);
	err = dsa_switch_probe(ds);
	mutex_unlock(&dsa2_mutex);

	return err;
}
EXPORT_SYMBOL_GPL(dsa_register_switch);

static void dsa_switch_remove(struct dsa_switch *ds)
{
	struct dsa_switch_tree *dst = ds->dst;

	dsa_tree_teardown(dst);
	dsa_switch_release_ports(ds);
	dsa_switch_put_tree(ds);
}

void dsa_unregister_switch(struct dsa_switch *ds)
{
	mutex_lock(&dsa2_mutex);
	dsa_switch_remove(ds);
	mutex_unlock(&dsa2_mutex);
}
EXPORT_SYMBOL_GPL(dsa_unregister_switch);

/* If the DSA conduit chooses to unregister its net_device on .shutdown, DSA is
 * blocking that operation from completion, due to the dev_hold taken inside
 * netdev_upper_dev_link. Unlink the DSA user interfaces from being uppers of
 * the DSA conduit, so that the system can reboot successfully.
 */
void dsa_switch_shutdown(struct dsa_switch *ds)
{
	struct net_device *conduit, *user_dev;
	LIST_HEAD(close_list);
	struct dsa_port *dp;

	mutex_lock(&dsa2_mutex);

	if (!ds->setup)
		goto out;

	rtnl_lock();

	dsa_switch_for_each_cpu_port(dp, ds)
		list_add(&dp->conduit->close_list, &close_list);

	netif_close_many(&close_list, true);

	dsa_switch_for_each_user_port(dp, ds) {
		conduit = dsa_port_to_conduit(dp);
		user_dev = dp->user;

		netif_device_detach(user_dev);
		netdev_upper_dev_unlink(conduit, user_dev);
	}

	/* Disconnect from further netdevice notifiers on the conduit,
	 * since netdev_uses_dsa() will now return false.
	 */
	dsa_switch_for_each_cpu_port(dp, ds) {
		dp->conduit->dsa_ptr = NULL;
		netdev_put(dp->conduit, &dp->conduit_tracker);
	}

	rtnl_unlock();
out:
	mutex_unlock(&dsa2_mutex);
}
EXPORT_SYMBOL_GPL(dsa_switch_shutdown);

#ifdef CONFIG_PM_SLEEP
static bool dsa_port_is_initialized(const struct dsa_port *dp)
{
	return dp->type == DSA_PORT_TYPE_USER && dp->user;
}

int dsa_switch_suspend(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	int ret = 0;

	/* Suspend user network devices */
	dsa_switch_for_each_port(dp, ds) {
		if (!dsa_port_is_initialized(dp))
			continue;

		ret = dsa_user_suspend(dp->user);
		if (ret)
			return ret;
	}

	if (ds->ops->suspend)
		ret = ds->ops->suspend(ds);

	return ret;
}
EXPORT_SYMBOL_GPL(dsa_switch_suspend);

int dsa_switch_resume(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	int ret = 0;

	if (ds->ops->resume)
		ret = ds->ops->resume(ds);

	if (ret)
		return ret;

	/* Resume user network devices */
	dsa_switch_for_each_port(dp, ds) {
		if (!dsa_port_is_initialized(dp))
			continue;

		ret = dsa_user_resume(dp->user);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_switch_resume);
#endif

struct dsa_port *dsa_port_from_netdev(struct net_device *netdev)
{
	if (!netdev || !dsa_user_dev_check(netdev))
		return ERR_PTR(-ENODEV);

	return dsa_user_to_port(netdev);
}
EXPORT_SYMBOL_GPL(dsa_port_from_netdev);

bool dsa_db_equal(const struct dsa_db *a, const struct dsa_db *b)
{
	if (a->type != b->type)
		return false;

	switch (a->type) {
	case DSA_DB_PORT:
		return a->dp == b->dp;
	case DSA_DB_LAG:
		return a->lag.dev == b->lag.dev;
	case DSA_DB_BRIDGE:
		return a->bridge.num == b->bridge.num;
	default:
		WARN_ON(1);
		return false;
	}
}

bool dsa_fdb_present_in_other_db(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid,
				 struct dsa_db db)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct dsa_mac_addr *a;

	lockdep_assert_held(&dp->addr_lists_lock);

	list_for_each_entry(a, &dp->fdbs, list) {
		if (!ether_addr_equal(a->addr, addr) || a->vid != vid)
			continue;

		if (a->db.type == db.type && !dsa_db_equal(&a->db, &db))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(dsa_fdb_present_in_other_db);

bool dsa_mdb_present_in_other_db(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 struct dsa_db db)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct dsa_mac_addr *a;

	lockdep_assert_held(&dp->addr_lists_lock);

	list_for_each_entry(a, &dp->mdbs, list) {
		if (!ether_addr_equal(a->addr, mdb->addr) || a->vid != mdb->vid)
			continue;

		if (a->db.type == db.type && !dsa_db_equal(&a->db, &db))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(dsa_mdb_present_in_other_db);

/* Helpers for switches without specific HSR offloads, but which can implement
 * NETIF_F_HW_HSR_DUP because their tagger uses dsa_xmit_port_mask()
 */
int dsa_port_simple_hsr_validate(struct dsa_switch *ds, int port,
				 struct net_device *hsr,
				 struct netlink_ext_ack *extack)
{
	enum hsr_port_type type;
	int err;

	err = hsr_get_port_type(hsr, dsa_to_port(ds, port)->user, &type);
	if (err)
		return err;

	if (type != HSR_PT_SLAVE_A && type != HSR_PT_SLAVE_B) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only HSR slave ports can be offloaded");
		return -EOPNOTSUPP;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_port_simple_hsr_validate);

int dsa_port_simple_hsr_join(struct dsa_switch *ds, int port,
			     struct net_device *hsr,
			     struct netlink_ext_ack *extack)
{
	struct dsa_port *dp = dsa_to_port(ds, port), *other_dp;
	int err;

	err = dsa_port_simple_hsr_validate(ds, port, hsr, extack);
	if (err)
		return err;

	dsa_hsr_foreach_port(other_dp, ds, hsr) {
		if (other_dp != dp) {
			dp->user->features |= NETIF_F_HW_HSR_DUP;
			other_dp->user->features |= NETIF_F_HW_HSR_DUP;
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_port_simple_hsr_join);

int dsa_port_simple_hsr_leave(struct dsa_switch *ds, int port,
			      struct net_device *hsr)
{
	struct dsa_port *dp = dsa_to_port(ds, port), *other_dp;

	dsa_hsr_foreach_port(other_dp, ds, hsr) {
		if (other_dp != dp) {
			dp->user->features &= ~NETIF_F_HW_HSR_DUP;
			other_dp->user->features &= ~NETIF_F_HW_HSR_DUP;
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_port_simple_hsr_leave);

static const struct dsa_stubs __dsa_stubs = {
	.conduit_hwtstamp_validate = __dsa_conduit_hwtstamp_validate,
};

static void dsa_register_stubs(void)
{
	dsa_stubs = &__dsa_stubs;
}

static void dsa_unregister_stubs(void)
{
	dsa_stubs = NULL;
}

static int __init dsa_init_module(void)
{
	int rc;

	rc = dsa_tree_class_register();
	if (rc)
		return rc;

	dsa_owq = alloc_ordered_workqueue("dsa_ordered",
					  WQ_MEM_RECLAIM);
	if (!dsa_owq) {
		rc = -ENOMEM;
		goto alloc_owq_fail;
	}

	rc = dsa_user_register_notifier();
	if (rc)
		goto register_notifier_fail;

	dev_add_pack(&dsa_pack_type);

	rc = rtnl_link_register(&dsa_link_ops);
	if (rc)
		goto netlink_register_fail;

	dsa_register_stubs();

	return 0;

netlink_register_fail:
	dsa_user_unregister_notifier();
	dev_remove_pack(&dsa_pack_type);
register_notifier_fail:
	destroy_workqueue(dsa_owq);
alloc_owq_fail:
	dsa_tree_class_unregister();

	return rc;
}
module_init(dsa_init_module);

static void __exit dsa_cleanup_module(void)
{
	dsa_unregister_stubs();

	rtnl_link_unregister(&dsa_link_ops);

	dsa_user_unregister_notifier();
	dev_remove_pack(&dsa_pack_type);
	destroy_workqueue(dsa_owq);
	dsa_tree_class_unregister();
}
module_exit(dsa_cleanup_module);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Distributed Switch Architecture switch chips");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dsa");
MODULE_IMPORT_NS("NETDEV_INTERNAL");
