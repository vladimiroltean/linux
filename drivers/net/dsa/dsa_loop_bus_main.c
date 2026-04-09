// SPDX-License-Identifier: GPL-2.0
/*
 * DSA loopback bus and genetlink family
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <net/genetlink.h>
#include <net/sock.h>
#include <uapi/linux/dsa_loop_bus.h>

#include "dsa_loop_bus_main.h"
#include "dsa_loop_bus_gen.h"
#include "dsa_loop.h"

static int dsa_loop_bus_port_event(struct dsa_loop_device *dld, int port,
				    u8 cmd)
{
	struct sk_buff *msg;
	struct nlattr *at;
	void *hdr;
	int ret;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &dsa_loop_bus_nl_family, 0, cmd);
	if (!hdr) {
		nlmsg_free(msg);
		return -EMSGSIZE;
	}

	if (nla_put_u32(msg, DSA_LOOP_BUS_A_TREE_INDEX, dld->pdata.cd.tree) ||
	    nla_put_u32(msg, DSA_LOOP_BUS_A_SWITCH_ID, dld->pdata.cd.index))
		goto nla_put_failure;

	at = nla_nest_start(msg, DSA_LOOP_BUS_A_PORT);
	if (!at)
		goto nla_put_failure;

	if (nla_put_u32(msg, DSA_LOOP_BUS_A_PORT_INDEX, port))
		goto nla_put_failure;

	nla_nest_end(msg, at);

	genlmsg_end(msg, hdr);

	ret = genlmsg_unicast(&init_net, msg, dld->portid);

	return ret;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int dsa_loop_bus_port_enable(struct dsa_loop_device *dld, int port)
{
	return dsa_loop_bus_port_event(dld, port, DSA_LOOP_BUS_CMD_PORT_ENABLE);
}
EXPORT_SYMBOL_GPL(dsa_loop_bus_port_enable);

int dsa_loop_bus_port_disable(struct dsa_loop_device *dld, int port)
{
	return dsa_loop_bus_port_event(dld, port, DSA_LOOP_BUS_CMD_PORT_DISABLE);
}
EXPORT_SYMBOL_GPL(dsa_loop_bus_port_disable);

static int dsa_loop_bus_probe(struct device *dev)
{
	struct dsa_loop_driver *ddrv = to_dsa_loop_driver(dev->driver);
	struct dsa_loop_device *dld = to_dsa_loop_device(dev);

	if (ddrv->probe)
		return ddrv->probe(dld);

	return 0;
}

static void dsa_loop_bus_remove(struct device *dev)
{
	struct dsa_loop_driver *ddrv = to_dsa_loop_driver(dev->driver);
	struct dsa_loop_device *dld = to_dsa_loop_device(dev);

	if (ddrv->remove)
		ddrv->remove(dld);
}

static void dsa_loop_bus_shutdown(struct device *dev)
{
	struct dsa_loop_driver *ddrv = to_dsa_loop_driver(dev->driver);
	struct dsa_loop_device *dld = to_dsa_loop_device(dev);

	if (ddrv->shutdown)
		ddrv->shutdown(dld);
}

static const struct bus_type dsa_loop_bus_type = {
	.name		= "dsa-loop",
	.dev_name	= "dsa-loop.",
	.probe		= dsa_loop_bus_probe,
	.remove		= dsa_loop_bus_remove,
	.shutdown	= dsa_loop_bus_shutdown,
};

int dsa_loop_driver_register(struct dsa_loop_driver *drv)
{
	drv->driver.bus = &dsa_loop_bus_type;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(dsa_loop_driver_register);

void dsa_loop_driver_unregister(struct dsa_loop_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(dsa_loop_driver_unregister);

static void dsa_loop_device_put_ports(struct dsa_loop_device *dld)
{
	struct dsa_loop_pdata *pdata = &dld->pdata;
	int i;

	for (i = 0; i < DSA_MAX_PORTS; i++) {
		if (dld->conduits[i]) {
			netdev_put(dld->conduits[i], &dld->trackers[i]);
			dld->conduits[i] = NULL;
		}
		if (pdata->cd.port_names[i]) {
			kfree(pdata->cd.port_names[i]);
			pdata->cd.port_names[i] = NULL;
		}
	}
	kfree(pdata->netdev);
	pdata->netdev = NULL;
}

static void dsa_loop_device_release(struct device *dev)
{
	struct dsa_loop_device *dld = to_dsa_loop_device(dev);

	dsa_loop_device_put_ports(dld);
	kfree(dld);
}

static int dsa_loop_device_get_ports(struct dsa_loop_device *dld,
				     struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	unsigned int num_ports = 0;
	bool cpu_port_seen = false;
	struct nlattr *attr, *nla;
	int rem;

	nla_for_each_attr_type(attr, DSA_LOOP_BUS_A_PORT,
			       genlmsg_data(info->genlhdr),
			       genlmsg_len(info->genlhdr), rem) {
		struct nlattr *tb[DSA_LOOP_BUS_A_PORT_MAX + 1];
		struct net *net = genl_info_net(info);
		unsigned int ifindex, index, type;
		struct net_device *conduit;
		int ret;

		ret = nla_parse_nested(tb, DSA_LOOP_BUS_A_PORT_MAX, attr,
				       dsa_loop_bus_port_nl_policy, extack);
		if (ret)
			return ret;

		nla = tb[DSA_LOOP_BUS_A_PORT_TYPE];
		if (!nla)
			return -EINVAL;
		type = nla_get_u32(nla);

		nla = tb[DSA_LOOP_BUS_A_PORT_INDEX];
		if (!nla)
			return -EINVAL;
		index = nla_get_u32(nla);

		if (index >= DSA_MAX_PORTS)
			return -ERANGE;

		if (dld->pdata.enabled_ports & BIT(index)) {
			NL_SET_ERR_MSG_MOD(extack, "Duplicated port entry");
			return -EINVAL;
		}

		switch (type) {
		case DSA_LOOP_BUS_PORT_TYPE_USER:
			nla = tb[DSA_LOOP_BUS_A_PORT_LABEL];
			if (nla) {
				char *name = nla_strdup(nla, GFP_KERNEL);

				if (!name)
					return -ENOMEM;

				dld->pdata.cd.port_names[index] = name;
			}
			break;
		case DSA_LOOP_BUS_PORT_TYPE_CPU:
			if (cpu_port_seen) {
				NL_SET_ERR_MSG_MOD(extack, "Single CPU port supported");
				return -EINVAL;
			}

			nla = tb[DSA_LOOP_BUS_A_PORT_CONDUIT_IFINDEX];
			if (!nla)
				return -EINVAL;

			ifindex = nla_get_u32(nla);
			conduit = netdev_get_by_index(net, ifindex,
						      &dld->trackers[index],
						      GFP_KERNEL);
			if (!conduit)
				return -ENODEV;

			if (!conduit->dev.parent) {
				NL_SET_ERR_MSG_MOD(extack, "Conduit must have a parent device");
				netdev_put(conduit, &dld->trackers[index]);
				return -EINVAL;
			}

			dld->conduits[index] = conduit;
			dld->pdata.cd.netdev[index] = &conduit->dev;
			dld->pdata.cd.port_names[index] = kstrdup("cpu", GFP_KERNEL);

			dld->pdata.netdev = kstrdup(conduit->name, GFP_KERNEL);
			cpu_port_seen = true;
		}

		dld->pdata.enabled_ports |= BIT(index);
		num_ports++;
	}

	dld->pdata.num_ports = num_ports;

	return 0;
}

static int dsa_loop_device_parse_identifiers(struct dsa_loop_device *dld,
					     struct genl_info *info)
{
	struct nlattr *nla;

	nla = info->attrs[DSA_LOOP_BUS_A_TREE_INDEX];
	if (!nla)
		return -EINVAL;
	dld->pdata.cd.tree = nla_get_u32(nla);

	nla = info->attrs[DSA_LOOP_BUS_A_SWITCH_ID];
	if (!nla)
		return -EINVAL;
	dld->pdata.cd.index = nla_get_u32(nla);

	return 0;
}

static int dsa_loop_device_parse_proto(struct dsa_loop_device *dld,
				       struct genl_info *info)
{
	struct dsa_loop_pdata *pdata = &dld->pdata;
	struct nlattr *nla;
	const char *name;
	int ret;

	nla = info->attrs[DSA_LOOP_BUS_A_TAG_PROTO];
	if (!nla)
		return -EINVAL;

	name = nla_memdup(nla, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	ret = dsa_tag_protocol_by_name(name, &pdata->tag_proto);
	kfree(name);

	return ret;
}

int dsa_loop_bus_nl_new_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct dsa_loop_bus_priv *priv;
	struct dsa_loop_device *dld;
	int ret;

	priv = genl_sk_priv_get(&dsa_loop_bus_nl_family, NETLINK_CB(skb).sk);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	if (priv->dld) {
		NL_SET_ERR_MSG_MOD(extack, "Device already exists");
		return -EBUSY;
	}

	dld = kzalloc(sizeof(*dld), GFP_KERNEL);
	if (!dld)
		return -ENOMEM;

	dld->dev.platform_data = &dld->pdata;

	ret = dsa_loop_device_get_ports(dld, info);
	if (ret)
		goto put_ports;

	ret = dsa_loop_device_parse_identifiers(dld, info);
	if (ret)
		goto put_ports;

	ret = dsa_loop_device_parse_proto(dld, info);
	if (ret)
		goto put_ports;

	dld->dev.id = info->snd_portid;
	dld->portid = info->snd_portid;
	dld->dev.bus = &dsa_loop_bus_type;
	dld->dev.release = dsa_loop_device_release;

	ret = device_register(&dld->dev);
	if (ret)
		goto out_put_device;

	priv->dld = dld;

	return 0;

out_put_device:
	put_device(&dld->dev);
put_ports:
	dsa_loop_device_put_ports(dld);
	kfree(dld);

	return ret;
}

int dsa_loop_bus_nl_del_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct dsa_loop_bus_priv *priv;

	priv = genl_sk_priv_get(&dsa_loop_bus_nl_family, NETLINK_CB(skb).sk);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	if (!priv->dld)
		return -ENOENT;

	device_unregister(&priv->dld->dev);
	priv->dld = NULL;

	return 0;
}

void dsa_loop_bus_nl_sock_priv_destroy(struct dsa_loop_bus_priv *priv)
{
	if (priv->dld)
		device_unregister(&priv->dld->dev);
}

void dsa_loop_bus_nl_sock_priv_init(struct dsa_loop_bus_priv *priv)
{
}

static int __init dsa_loop_bus_init(void)
{
	int ret;

	ret = bus_register(&dsa_loop_bus_type);
	if (ret)
		return ret;

	ret = genl_register_family(&dsa_loop_bus_nl_family);
	if (ret)
		bus_unregister(&dsa_loop_bus_type);

	return ret;
}

static void __exit dsa_loop_bus_exit(void)
{
	genl_unregister_family(&dsa_loop_bus_nl_family);
	bus_unregister(&dsa_loop_bus_type);
}

subsys_initcall(dsa_loop_bus_init);
module_exit(dsa_loop_bus_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DSA loopback bus and genetlink family");
