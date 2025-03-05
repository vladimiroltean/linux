// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DSA topology handling
 *
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2013 Florian Fainelli <florian@openwrt.org>
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <net/dsa.h>
#include <net/sch_generic.h>

#include "conduit.h"
#include "dsa.h"
#include "port.h"
#include "switch.h"
#include "tag.h"
#include "tree.h"

#define DSA_MAX_NUM_OFFLOADING_BRIDGES		BITS_PER_LONG

struct dsa_tree_bfs_elem {
	struct device_node *dn;
	struct list_head list;
};

LIST_HEAD(dsa_tree_list);

/* Track the bridges with forwarding offload enabled */
static unsigned long dsa_fwd_offloading_bridges;

static void dsa_tree_conduit_state_change(struct dsa_switch_tree *dst,
					  struct net_device *conduit)
{
	struct dsa_notifier_conduit_state_info info;
	struct dsa_port *cpu_dp = conduit->dsa_ptr;

	info.conduit = conduit;
	info.operational = dsa_port_conduit_is_operational(cpu_dp);

	dsa_tree_notify(dst, DSA_NOTIFIER_CONDUIT_STATE_CHANGE, &info);
}

void dsa_tree_conduit_admin_state_change(struct dsa_switch_tree *dst,
					 struct net_device *conduit,
					 bool up)
{
	struct dsa_port *cpu_dp = conduit->dsa_ptr;
	bool notify = false;

	/* Don't keep track of admin state on LAG DSA conduits,
	 * but rather just of physical DSA conduits
	 */
	if (netif_is_lag_master(conduit))
		return;

	if ((dsa_port_conduit_is_operational(cpu_dp)) !=
	    (up && cpu_dp->conduit_oper_up))
		notify = true;

	cpu_dp->conduit_admin_up = up;

	if (notify)
		dsa_tree_conduit_state_change(dst, conduit);
}

void dsa_tree_conduit_oper_state_change(struct dsa_switch_tree *dst,
					struct net_device *conduit,
					bool up)
{
	struct dsa_port *cpu_dp = conduit->dsa_ptr;
	bool notify = false;

	/* Don't keep track of oper state on LAG DSA conduits,
	 * but rather just of physical DSA conduits
	 */
	if (netif_is_lag_master(conduit))
		return;

	if ((dsa_port_conduit_is_operational(cpu_dp)) !=
	    (cpu_dp->conduit_admin_up && up))
		notify = true;

	cpu_dp->conduit_oper_up = up;

	if (notify)
		dsa_tree_conduit_state_change(dst, conduit);
}

static int dsa_tree_bind_tag_proto(struct dsa_switch_tree *dst,
				   const struct dsa_device_ops *tag_ops)
{
	const struct dsa_device_ops *old_tag_ops = dst->tag_ops;
	struct dsa_notifier_tag_proto_info info;
	int err;

	dst->tag_ops = tag_ops;

	/* Notify the switches from this tree about the connection
	 * to the new tagger
	 */
	info.tag_ops = tag_ops;
	err = dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO_CONNECT, &info);
	if (err && err != -EOPNOTSUPP)
		goto out_disconnect;

	/* Notify the old tagger about the disconnection from this tree */
	info.tag_ops = old_tag_ops;
	dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO_DISCONNECT, &info);

	return 0;

out_disconnect:
	info.tag_ops = tag_ops;
	dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO_DISCONNECT, &info);
	dst->tag_ops = old_tag_ops;

	return err;
}

/* Since the dsa/tagging sysfs device attribute is per conduit, the assumption
 * is that all DSA switches within a tree share the same tagger, otherwise
 * they would have formed disjoint trees (different "dsa,member" values).
 */
int dsa_tree_change_tag_proto(struct dsa_switch_tree *dst,
			      const struct dsa_device_ops *tag_ops,
			      const struct dsa_device_ops *old_tag_ops)
{
	struct dsa_notifier_tag_proto_info info;
	struct dsa_port *dp;
	int err = -EBUSY;

	if (!rtnl_trylock())
		return restart_syscall();

	/* At the moment we don't allow changing the tag protocol under
	 * traffic. The rtnl_mutex also happens to serialize concurrent
	 * attempts to change the tagging protocol. If we ever lift the IFF_UP
	 * restriction, there needs to be another mutex which serializes this.
	 */
	dsa_tree_for_each_user_port(dp, dst) {
		if (dsa_port_to_conduit(dp)->flags & IFF_UP)
			goto out_unlock;

		if (dp->user->flags & IFF_UP)
			goto out_unlock;
	}

	/* Notify the tag protocol change */
	info.tag_ops = tag_ops;
	err = dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO, &info);
	if (err)
		goto out_unwind_tagger;

	err = dsa_tree_bind_tag_proto(dst, tag_ops);
	if (err)
		goto out_unwind_tagger;

	rtnl_unlock();

	return 0;

out_unwind_tagger:
	info.tag_ops = old_tag_ops;
	dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO, &info);
out_unlock:
	rtnl_unlock();
	return err;
}

static int dsa_switch_parse_member_of(struct device_node *dn,
				      int *switch_index,
				      int *tree_index)
{
	u32 m[2] = { 0, 0 };
	int sz;

	/* Don't error out if this optional property isn't found */
	sz = of_property_read_variable_u32_array(dn, "dsa,member", m, 2, 2);
	if (sz < 0 && sz != -EINVAL) {
		*tree_index = 0;
		*switch_index = 0;
		return sz;
	}

	*tree_index = m[0];
	*switch_index = m[1];

	return 0;
}

/**
 * dsa_lag_map() - Map LAG structure to a linear LAG array
 * @dst: Tree in which to record the mapping.
 * @lag: LAG structure that is to be mapped to the tree's array.
 *
 * dsa_lag_id/dsa_lag_by_id can then be used to translate between the
 * two spaces. The size of the mapping space is determined by the
 * driver by setting ds->num_lag_ids. It is perfectly legal to leave
 * it unset if it is not needed, in which case these functions become
 * no-ops.
 */
void dsa_lag_map(struct dsa_switch_tree *dst, struct dsa_lag *lag)
{
	unsigned int id;

	for (id = 1; id <= dst->lags_len; id++) {
		if (!dsa_lag_by_id(dst, id)) {
			dst->lags[id - 1] = lag;
			lag->id = id;
			return;
		}
	}

	/* No IDs left, which is OK. Some drivers do not need it. The
	 * ones that do, e.g. mv88e6xxx, will discover that dsa_lag_id
	 * returns an error for this device when joining the LAG. The
	 * driver can then return -EOPNOTSUPP back to DSA, which will
	 * fall back to a software LAG.
	 */
}

/**
 * dsa_lag_unmap() - Remove a LAG ID mapping
 * @dst: Tree in which the mapping is recorded.
 * @lag: LAG structure that was mapped.
 *
 * As there may be multiple users of the mapping, it is only removed
 * if there are no other references to it.
 */
void dsa_lag_unmap(struct dsa_switch_tree *dst, struct dsa_lag *lag)
{
	unsigned int id;

	dsa_lags_foreach_id(id, dst) {
		if (dsa_lag_by_id(dst, id) == lag) {
			dst->lags[id - 1] = NULL;
			lag->id = 0;
			break;
		}
	}
}

struct dsa_lag *dsa_tree_lag_find(struct dsa_switch_tree *dst,
				  const struct net_device *lag_dev)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_lag_dev_get(dp) == lag_dev)
			return dp->lag;

	return NULL;
}

struct dsa_bridge *dsa_tree_bridge_find(struct dsa_switch_tree *dst,
					const struct net_device *br)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_bridge_dev_get(dp) == br)
			return dp->bridge;

	return NULL;
}

static int dsa_bridge_num_find(const struct net_device *bridge_dev)
{
	struct dsa_switch_tree *dst;

	list_for_each_entry(dst, &dsa_tree_list, list) {
		struct dsa_bridge *bridge;

		bridge = dsa_tree_bridge_find(dst, bridge_dev);
		if (bridge)
			return bridge->num;
	}

	return 0;
}

unsigned int dsa_bridge_num_get(const struct net_device *bridge_dev, int max)
{
	unsigned int bridge_num = dsa_bridge_num_find(bridge_dev);

	/* Switches without FDB isolation support don't get unique
	 * bridge numbering
	 */
	if (!max)
		return 0;

	if (!bridge_num) {
		/* First port that requests FDB isolation or TX forwarding
		 * offload for this bridge
		 */
		bridge_num = find_next_zero_bit(&dsa_fwd_offloading_bridges,
						DSA_MAX_NUM_OFFLOADING_BRIDGES,
						1);
		if (bridge_num > max)
			return 0;

		set_bit(bridge_num, &dsa_fwd_offloading_bridges);
	}

	return bridge_num;
}

void dsa_bridge_num_put(const struct net_device *bridge_dev,
			unsigned int bridge_num)
{
	/* Since we refcount bridges, we know that when we call this function
	 * it is no longer in use, so we can just go ahead and remove it from
	 * the bit mask.
	 */
	clear_bit(bridge_num, &dsa_fwd_offloading_bridges);
}

struct dsa_switch *dsa_switch_find(int tree_index, int sw_index)
{
	struct dsa_switch_tree *dst;
	struct dsa_port *dp;

	list_for_each_entry(dst, &dsa_tree_list, list) {
		if (dst->index != tree_index)
			continue;

		list_for_each_entry(dp, &dst->ports, list) {
			if (dp->ds->index != sw_index)
				continue;

			return dp->ds;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(dsa_switch_find);

static struct dsa_switch_tree *dsa_tree_find(int index)
{
	struct dsa_switch_tree *dst;

	list_for_each_entry(dst, &dsa_tree_list, list)
		if (dst->index == index)
			return dst;

	return NULL;
}

static int dsa_bfs_elem_queue(struct list_head *to_explore,
			      struct device_node *dn)
{
	struct dsa_tree_bfs_elem *elem;

	elem = kzalloc(sizeof(*elem), GFP_KERNEL);
	if (!elem)
		return -ENOMEM;

	elem->dn = of_node_get(dn);
	list_add_tail(&elem->list, to_explore);

	return 0;
}

/* Caller is responsible for calling dsa_bfs_elem_free() when done */
static struct dsa_tree_bfs_elem *
dsa_bfs_elem_dequeue(struct list_head *to_explore, struct list_head *explored)
{
	struct dsa_tree_bfs_elem *elem;

	elem = list_first_entry(to_explore, struct dsa_tree_bfs_elem, list);
	list_del_init(&elem->list);
	list_add_tail(&elem->list, explored);

	return elem;
}

static bool dsa_bfs_elem_visited(struct device_node *dn,
				 struct list_head *to_explore,
				 struct list_head *explored)
{
	struct dsa_tree_bfs_elem *elem;

	list_for_each_entry(elem, to_explore, list)
		if (elem->dn == dn)
			return true;

	list_for_each_entry(elem, explored, list)
		if (elem->dn == dn)
			return true;

	return false;
}

static void dsa_bfs_elem_free(struct dsa_tree_bfs_elem *elem)
{
	of_node_put(elem->dn);
	kfree(elem);
}

static void dsa_bfs_queue_free(struct list_head *queue)
{
	struct dsa_tree_bfs_elem *elem, *next;

	list_for_each_entry_safe(elem, next, queue, list) {
		list_del(&elem->list);
		dsa_bfs_elem_free(elem);
	}
}

static int dsa_port_node_get_parents(struct device_node *port_dn,
				     struct device_node **ports_dn,
				     struct device_node **switch_dn)
{
	struct device_node *ports, *switch_node;
	int err = 0;

	/* port_dn should be a port node */
	if (of_node_is_root(port_dn)) {
		pr_err("DSA: port node %pOF should not be root\n", port_dn);
		return -EINVAL;
	}

	ports = of_get_parent(port_dn);

	/* ports should be the 'ports'/'ethernet-ports' container node */
	if (of_node_is_root(ports)) {
		pr_err("DSA: container node for port %pOF should not be root\n",
		       port_dn);
		err = -EINVAL;
		goto out_put_ports;
	}
	if (!of_node_name_eq(ports, "ports") &&
	    !of_node_name_eq(ports, "ethernet-ports")) {
		pr_err("DSA: ports node %pOF should be named \"ports\" or \"ethernet-ports\"\n",
		       ports);
		err = -EINVAL;
		goto out_put_ports;
	}

	/* switch_node should be the 'switch'/'ethernet-switch' container node */
	switch_node = of_get_parent(ports);
	if (of_node_is_root(switch_node)) {
		pr_err("DSA: switch node %pOF should not be root\n", switch_node);
		err = -EINVAL;
		goto out_put_switch;
	}

	if (ports_dn)
		*ports_dn = of_node_get(ports);
	if (switch_dn)
		*switch_dn = of_node_get(switch_node);

out_put_switch:
	of_node_put(switch_node);
out_put_ports:
	of_node_put(ports);

	return err;
}

static bool dsa_port_node_is_available(struct device_node *port_dn)
{
	struct device_node *switch_dn;
	bool available;

	if (!of_device_is_available(port_dn))
		return false;

	if (dsa_port_node_get_parents(port_dn, NULL, &switch_dn))
		return false;

	available = of_device_is_available(switch_dn);
	of_node_put(switch_dn);

	return available;
}

static int dsa_tree_explore_switch_by_link(struct device_node *dn,
					   struct list_head *to_explore,
					   struct list_head *explored)
{
	struct device_node *switch_dn, *ports, *port;
	int err = 0;

	if (!of_device_is_available(dn))
		return 0;

	err = dsa_port_node_get_parents(dn, &ports, &switch_dn);
	if (err)
		return err;

	if (!of_device_is_available(switch_dn))
		goto out_put_parents;

	for_each_available_child_of_node(ports, port) {
		if (!of_property_present(port, "link") &&
		    !of_property_present(port, "cascade"))
			continue;

		if (dsa_bfs_elem_visited(switch_dn, to_explore, explored))
			continue;

		err = dsa_bfs_elem_queue(to_explore, switch_dn);
		if (err) {
			of_node_put(port);
			goto out_put_parents;
		}
	}

out_put_parents:
	of_node_put(switch_dn);
	of_node_put(ports);

	return err;
}

static int dsa_tree_explore_switch_ports_phandle(struct dsa_switch_tree *dst,
						 struct device_node *dn,
						 struct list_head *to_explore,
						 struct list_head *explored,
						 bool *has_link, bool *has_cascade)
{
	struct device_node *ports, *port;
	struct of_phandle_iterator it;
	int err = 0;
	int ret;

	ports = of_get_child_by_name(dn, "ports");
	if (!ports) {
		/* The second possibility is "ethernet-ports" */
		ports = of_get_child_by_name(dn, "ethernet-ports");
		if (!ports) {
			pr_err("no ports child node found for %pOF\n", dn);
			return -EINVAL;
		}
	}

	for_each_available_child_of_node(ports, port) {
		struct device_node *cascade;

		of_for_each_phandle(&it, ret, port, "link", NULL, 0) {
			if (*has_cascade) {
				pr_err("DSA tree %d cannot combine \"link\" and \"cascade\" specifications\n",
				       dst->index);
				of_node_put(port);
				err = -EINVAL;
				goto out;
			}

			err = dsa_tree_explore_switch_by_link(it.node,
							      to_explore,
							      explored);
			if (err) {
				pr_err("Failed to explore link %pOF\n", it.node);
				of_node_put(port);
				goto out;
			}

			*has_link = true;
		}

		cascade = of_parse_phandle(port, "cascade", 0);
		if (cascade) {
			if (*has_link) {
				pr_err("DSA tree %d cannot combine \"link\" and \"cascade\" specifications\n",
				       dst->index);
				of_node_put(cascade);
				of_node_put(port);
				err = -EINVAL;
				goto out;
			}

			err = dsa_tree_explore_switch_by_link(cascade,
							      to_explore,
							      explored);
			of_node_put(cascade);
			if (err) {
				pr_err("DSA tree %d failed to explore cascade %pOF\n",
				       dst->index, cascade);
				of_node_put(port);
				goto out;
			}

			*has_cascade = true;
		}
	}

out:
	of_node_put(ports);
	return err;
}

static int dsa_tree_component_add_cascade(struct dsa_tree_component *component,
					  struct device_node *port)
{
	struct device_node *dest_port_dn;
	struct dsa_tree_cascade *cascade;

	dest_port_dn = of_parse_phandle(port, "cascade", 0);
	if (!dest_port_dn)
		return 0;

	if (!dsa_port_node_is_available(dest_port_dn)) {
		pr_warn("DSA: port %pOF has cascade towards disabled port %pOF, but is not disabled!\n",
			port, dest_port_dn);
		of_node_put(dest_port_dn);
		return 0;
	}

	cascade = kzalloc(sizeof(*cascade), GFP_KERNEL);
	if (!cascade) {
		of_node_put(dest_port_dn);
		return -ENOMEM;
	}

	cascade->source_port_dn = of_node_get(port);
	/* Refcount already increased by of_parse_phandle() */
	cascade->dest_port_dn = dest_port_dn;
	list_add_tail(&cascade->list, &component->cascades);

	return 0;
}

static int dsa_tree_component_add_links(struct dsa_tree_component *component,
					struct device_node *port)
{
	struct dsa_tree_cascade *link;
	struct of_phandle_iterator it;
	int err;

	of_for_each_phandle(&it, err, port, "link", NULL, 0) {
		if (!dsa_port_node_is_available(it.node)) {
			pr_warn("DSA: port %pOF has link towards disabled port %pOF, but is not disabled!\n",
				port, it.node);
			continue;
		}

		link = kzalloc(sizeof(*link), GFP_KERNEL);
		if (!link) {
			of_node_put(it.node);
			return -ENOMEM;
		}

		link->source_port_dn = of_node_get(port);
		/* Refcount already increased by of_parse_phandle() */
		link->dest_port_dn = of_node_get(it.node);
		list_add_tail(&link->list, &component->links);
	}

	return 0;
}

static void dsa_tree_component_free_cascades(struct dsa_tree_component *component)
{
	struct dsa_tree_cascade *cascade, *next;

	list_for_each_entry_safe(cascade, next, &component->cascades, list) {
		of_node_put(cascade->source_port_dn);
		of_node_put(cascade->dest_port_dn);
		list_del(&cascade->list);
		kfree(cascade);
	}
}

static int dsa_tree_component_add_port(struct dsa_tree_component *component,
				       struct device_node *port)
{
	struct dsa_tree_component_port *component_port;

	component_port = kzalloc(sizeof(*component_port), GFP_KERNEL);
	if (!component_port)
		return -ENOMEM;

	if (!of_device_is_available(port))
		component_port->type = DSA_PORT_TYPE_UNUSED;
	else if (of_property_present(port, "ethernet"))
		component_port->type = DSA_PORT_TYPE_CPU;
	else if (of_property_present(port, "link") ||
		 of_property_present(port, "cascade"))
		component_port->type = DSA_PORT_TYPE_DSA;
	else
		component_port->type = DSA_PORT_TYPE_USER;

	component_port->dn = of_node_get(port);
	list_add_tail(&component_port->list, &component->ports);

	return 0;
}

static void dsa_tree_component_free_ports(struct dsa_tree_component *component)
{
	struct dsa_tree_component_port *port, *next;

	list_for_each_entry_safe(port, next, &component->ports, list) {
		of_node_put(port->dn);
		list_del(&port->list);
		kfree(port);
	}
}

static int dsa_tree_component_parse(struct dsa_tree_component *component)
{
	struct device_node *switch_dn = component->switch_dn;
	struct device_node *ports, *port;
	int err = 0;

	INIT_LIST_HEAD(&component->ports);
	INIT_LIST_HEAD(&component->cascades);

	ports = of_get_child_by_name(switch_dn, "ports");
	if (!ports)
		ports = of_get_child_by_name(switch_dn, "ethernet-ports");

	for_each_available_child_of_node(ports, port) {
		err = dsa_tree_component_add_port(component, port);
		if (err)
			goto err_free;

		err = dsa_tree_component_add_cascade(component, port);
		if (err)
			goto err_free;

		err = dsa_tree_component_add_links(component, port);
		if (err)
			goto err_free;
	}

	of_node_put(ports);

	return 0;

err_free:
	of_node_put(port);
	dsa_tree_component_free_ports(component);
	dsa_tree_component_free_cascades(component);
	of_node_put(ports);
	return err;
}

static int dsa_tree_component_add(struct dsa_switch_tree *dst,
				  struct device_node *switch_dn)
{
	struct dsa_tree_component *component;
	int switch_index, tree_index;
	int err;

	err = dsa_switch_parse_member_of(switch_dn, &switch_index,
					 &tree_index);
	if (err)
		return err;

	if (tree_index != dst->index) {
		pr_err("Tree %d identified link towards component %pOF of tree %d\n",
		       dst->index, switch_dn, tree_index);
		return -EINVAL;
	}

	component = kzalloc(sizeof(*component), GFP_KERNEL);
	if (!component)
		return -ENOMEM;

	component->index = switch_index;
	component->switch_dn = of_node_get(switch_dn);

	err = dsa_tree_component_parse(component);
	if (err) {
		of_node_put(switch_dn);
		kfree(component);
		return err;
	}

	list_add_tail(&component->list, &dst->components);
	pr_info("Added component %pOF\n", switch_dn);

	return 0;
}

static void dsa_tree_components_free(struct dsa_switch_tree *dst)
{
	struct dsa_tree_component *component, *next;

	list_for_each_entry_safe(component, next, &dst->components, list) {
		dsa_tree_component_free_cascades(component);
		of_node_put(component->switch_dn);
		list_del(&component->list);
		kfree(component);
	}
}

static struct dsa_tree_component *
dsa_tree_find_component_by_node(struct dsa_switch_tree *dst,
				struct device_node *switch_dn)
{
	struct dsa_tree_component *component;

	list_for_each_entry(component, &dst->components, list)
		if (component->switch_dn == switch_dn)
			return component;

	return NULL;
}

static struct dsa_tree_component *
dsa_tree_find_component(struct dsa_switch_tree *dst, struct dsa_switch *ds)
{
	return dsa_tree_find_component_by_node(dst, dev_of_node(ds->dev));
}

/* Breadth-first search starting from the OF node of any switch retrieves the
 * skeleton of the entire tree, saved into the dst->components list.
 */
static int dsa_tree_explore_device_tree(struct dsa_switch_tree *dst,
					struct device_node *dn, bool *has_link,
					bool *has_cascade)
{
	struct dsa_tree_bfs_elem *elem;
	int err, num_components = 0;
	LIST_HEAD(to_explore);
	LIST_HEAD(explored);

	err = dsa_bfs_elem_queue(&to_explore, dn);
	if (err)
		return err;

	while (!list_empty(&to_explore)) {
		elem = dsa_bfs_elem_dequeue(&to_explore, &explored);

		err = dsa_tree_component_add(dst, elem->dn);
		if (err)
			goto free_lists;

		num_components++;

		err = dsa_tree_explore_switch_ports_phandle(dst, elem->dn,
							    &to_explore,
							    &explored,
							    has_link,
							    has_cascade);
		if (err)
			goto free_lists;
	}

	dsa_bfs_queue_free(&explored);

	/* Only trees with more than 2 switches need to use "cascade" to
	 * undoubtebly contain adjacency information. With 2 switches or less,
	 * adjacency is implicit even with "link".
	 *
	 * NOTE: Because they are in a union, anything added to
	 * &component->links (by dsa_tree_component_add_links()) is
	 * also visible in &component->cascades (as if it were added by
	 * dsa_tree_component_add_cascade()).
	 */
	if (has_cascade || num_components <= 2)
		dst->has_adjacency = true;

	return 0;

free_lists:
	dsa_bfs_queue_free(&explored);
	dsa_bfs_queue_free(&to_explore);
	dsa_tree_components_free(dst);

	return err;
}

static struct class dsa_switch_tree_class = {
	.name = "dsa_switch_tree",
};

static int dsa_tree_create_dev(struct dsa_switch_tree *dst,
			       struct device_node *dn,
			       struct dsa_chip_data *pdata)
{
	struct device *dev;

	dev = device_create(&dsa_switch_tree_class, NULL, 0, NULL,
			    "dsa_switch_tree.%d", dst->index);
	if (!dev)
		return -ENOMEM;

	dst->dev = dev;

	return 0;
}

static struct dsa_switch_tree *dsa_tree_alloc(int index)
{
	struct dsa_switch_tree *dst;

	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return NULL;

	dst->index = index;

	INIT_LIST_HEAD(&dst->rtable);
	INIT_LIST_HEAD(&dst->components);
	INIT_LIST_HEAD(&dst->ports);

	INIT_LIST_HEAD(&dst->list);
	list_add_tail(&dst->list, &dsa_tree_list);

	kref_init(&dst->refcount);

	return dst;
}

static struct dsa_port *dsa_tree_find_port_by_node(struct dsa_switch_tree *dst,
						   struct device_node *dn)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dp->dn == dn)
			return dp;

	return NULL;
}

static struct dsa_port *dsa_tree_find_first_cpu(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_is_cpu(dp))
			return dp;

	return NULL;
}

struct net_device *dsa_tree_find_first_conduit(struct dsa_switch_tree *dst)
{
	struct device_node *ethernet;
	struct net_device *conduit;
	struct dsa_port *cpu_dp;

	cpu_dp = dsa_tree_find_first_cpu(dst);
	ethernet = of_parse_phandle(cpu_dp->dn, "ethernet", 0);
	conduit = of_find_net_device_by_node(ethernet);
	of_node_put(ethernet);

	return conduit;
}

/* Assign the default CPU port (the first one in the tree) to all ports of the
 * fabric which don't already have one as part of their own switch.
 */
static int dsa_tree_setup_default_cpu(struct dsa_switch_tree *dst)
{
	struct dsa_port *cpu_dp, *dp;

	cpu_dp = dsa_tree_find_first_cpu(dst);
	if (!cpu_dp) {
		pr_err("DSA: tree %d has no CPU port\n", dst->index);
		return -EINVAL;
	}

	list_for_each_entry(dp, &dst->ports, list) {
		if (dp->cpu_dp)
			continue;

		if (dsa_port_is_user(dp) || dsa_port_is_dsa(dp))
			dp->cpu_dp = cpu_dp;
	}

	return 0;
}

static struct dsa_port *
dsa_switch_preferred_default_local_cpu_port(struct dsa_switch *ds)
{
	struct dsa_port *cpu_dp;

	if (!ds->ops->preferred_default_local_cpu_port)
		return NULL;

	cpu_dp = ds->ops->preferred_default_local_cpu_port(ds);
	if (!cpu_dp)
		return NULL;

	if (WARN_ON(!dsa_port_is_cpu(cpu_dp) || cpu_dp->ds != ds))
		return NULL;

	return cpu_dp;
}

/* Perform initial assignment of CPU ports to user ports and DSA links in the
 * fabric, giving preference to CPU ports local to each switch. Default to
 * using the first CPU port in the switch tree if the port does not have a CPU
 * port local to this switch.
 */
static int dsa_tree_setup_cpu_ports(struct dsa_switch_tree *dst)
{
	struct dsa_port *preferred_cpu_dp, *cpu_dp, *dp;

	list_for_each_entry(cpu_dp, &dst->ports, list) {
		if (!dsa_port_is_cpu(cpu_dp))
			continue;

		preferred_cpu_dp = dsa_switch_preferred_default_local_cpu_port(cpu_dp->ds);
		if (preferred_cpu_dp && preferred_cpu_dp != cpu_dp)
			continue;

		/* Prefer a local CPU port */
		dsa_switch_for_each_port(dp, cpu_dp->ds) {
			/* Prefer the first local CPU port found */
			if (dp->cpu_dp)
				continue;

			if (dsa_port_is_user(dp) || dsa_port_is_dsa(dp))
				dp->cpu_dp = cpu_dp;
		}
	}

	return dsa_tree_setup_default_cpu(dst);
}

static void dsa_tree_teardown_cpu_ports(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_is_user(dp) || dsa_port_is_dsa(dp))
			dp->cpu_dp = NULL;
}

/* First tear down the non-shared, then the shared ports. This ensures that
 * all work items scheduled by our switchdev handlers for user ports have
 * completed before we destroy the refcounting kept on the shared ports.
 */
static void dsa_tree_teardown_ports(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_is_user(dp) || dsa_port_is_unused(dp))
			dsa_port_teardown(dp);

	dsa_flush_workqueue();

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_is_dsa(dp) || dsa_port_is_cpu(dp))
			dsa_port_teardown(dp);
}

static void dsa_tree_teardown_switches(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		dsa_switch_teardown(dp->ds);
}

/* Bring shared ports up first, then non-shared ports */
static int dsa_tree_setup_ports(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;
	int err = 0;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dsa_port_is_dsa(dp) || dsa_port_is_cpu(dp)) {
			err = dsa_port_setup(dp);
			if (err)
				goto teardown;
		}
	}

	list_for_each_entry(dp, &dst->ports, list) {
		if (dsa_port_is_user(dp) || dsa_port_is_unused(dp)) {
			err = dsa_port_setup(dp);
			if (err) {
				err = dsa_port_setup_as_unused(dp);
				if (err)
					goto teardown;
			}
		}
	}

	return 0;

teardown:
	dsa_tree_teardown_ports(dst);

	return err;
}

static int dsa_tree_setup_switches(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;
	int err = 0;

	list_for_each_entry(dp, &dst->ports, list) {
		err = dsa_switch_setup(dp->ds);
		if (err) {
			dsa_tree_teardown_switches(dst);
			break;
		}
	}

	return err;
}

static int dsa_tree_setup_conduit(struct dsa_switch_tree *dst)
{
	struct dsa_port *cpu_dp;
	int err = 0;

	rtnl_lock();

	dsa_tree_for_each_cpu_port(cpu_dp, dst) {
		struct net_device *conduit = cpu_dp->conduit;
		bool admin_up = (conduit->flags & IFF_UP) &&
				!qdisc_tx_is_noop(conduit);

		err = dsa_conduit_setup(conduit, cpu_dp);
		if (err)
			break;

		/* Replay conduit state event */
		dsa_tree_conduit_admin_state_change(dst, conduit, admin_up);
		dsa_tree_conduit_oper_state_change(dst, conduit,
						   netif_oper_up(conduit));
	}

	rtnl_unlock();

	return err;
}

static void dsa_tree_teardown_conduit(struct dsa_switch_tree *dst)
{
	struct dsa_port *cpu_dp;

	rtnl_lock();

	dsa_tree_for_each_cpu_port(cpu_dp, dst) {
		struct net_device *conduit = cpu_dp->conduit;

		/* Synthesizing an "admin down" state is sufficient for
		 * the switches to get a notification if the conduit is
		 * currently up and running.
		 */
		dsa_tree_conduit_admin_state_change(dst, conduit, false);

		dsa_conduit_teardown(conduit);
	}

	rtnl_unlock();
}

static int dsa_tree_setup_lags(struct dsa_switch_tree *dst)
{
	unsigned int len = 0;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dp->ds->num_lag_ids > len)
			len = dp->ds->num_lag_ids;
	}

	if (!len)
		return 0;

	dst->lags = kzalloc_objs(*dst->lags, len);
	if (!dst->lags)
		return -ENOMEM;

	dst->lags_len = len;
	return 0;
}

static void dsa_tree_teardown_lags(struct dsa_switch_tree *dst)
{
	kfree(dst->lags);
}

static bool dsa_tree_complete(struct dsa_switch_tree *dst)
{
	struct dsa_tree_component *component;

	/* pdata has a single switch, and we don't create components for it */
	if (dst->probing_mode == DSA_TREE_PROBING_PDATA)
		return true;

	list_for_each_entry(component, &dst->components, list)
		if (component->state == DSA_TREE_COMPONENT_UNBOUND)
			return false;

	return true;
}

int dsa_tree_setup(struct dsa_switch_tree *dst)
{
	int err;

	err = dsa_tree_setup_cpu_ports(dst);
	if (err)
		return err;

	err = dsa_tree_setup_switches(dst);
	if (err)
		goto teardown_cpu_ports;

	err = dsa_tree_setup_ports(dst);
	if (err)
		goto teardown_switches;

	err = dsa_tree_setup_conduit(dst);
	if (err)
		goto teardown_ports;

	err = dsa_tree_setup_lags(dst);
	if (err)
		goto teardown_conduit;

	pr_info("DSA: tree %d setup\n", dst->index);

	return 0;

teardown_conduit:
	dsa_tree_teardown_conduit(dst);
teardown_ports:
	dsa_tree_teardown_ports(dst);
teardown_switches:
	dsa_tree_teardown_switches(dst);
teardown_cpu_ports:
	dsa_tree_teardown_cpu_ports(dst);

	return err;
}

void dsa_tree_teardown(struct dsa_switch_tree *dst)
{
	dsa_tree_teardown_lags(dst);

	dsa_tree_teardown_conduit(dst);

	dsa_tree_teardown_ports(dst);

	dsa_tree_teardown_switches(dst);

	dsa_tree_teardown_cpu_ports(dst);

	pr_info("DSA: tree %d torn down\n", dst->index);
}

static void dsa_tree_free(struct dsa_switch_tree *dst)
{
	if (dst->tag_ops)
		dsa_tag_driver_put(dst->tag_ops);
	list_del(&dst->list);
	/* No components created for pdata, the tree is always single-switch */
	if (dst->probing_mode == DSA_TREE_PROBING_OF)
		dsa_tree_components_free(dst);
	device_unregister(dst->dev);
	kfree(dst);
}

static void dsa_tree_release(struct kref *ref)
{
	struct dsa_switch_tree *dst;

	dst = container_of(ref, struct dsa_switch_tree, refcount);

	dsa_tree_free(dst);
}

static void dsa_tree_put(struct dsa_switch_tree *dst)
{
	if (dst)
		kref_put(&dst->refcount, dsa_tree_release);
}

static int dsa_link_create(struct dsa_switch_tree *dst,
			   struct device_node *source_port_dn,
			   struct device_node *dest_port_dn)
{
	struct dsa_link *dl;

	dl = kzalloc(sizeof(*dl), GFP_KERNEL);
	if (!dl)
		return -ENOMEM;

	dl->source_port_dn = of_node_get(source_port_dn);
	dl->dest_port_dn = of_node_get(dest_port_dn);
	list_add_tail(&dl->list, &dst->rtable);

	return 0;
}

static void dsa_link_free(struct dsa_link *dl)
{
	of_node_put(dl->source_port_dn);
	of_node_put(dl->dest_port_dn);
	kfree(dl);
}

/* Run BFS from cascade->source_port_dn */
static int dsa_tree_create_rtable_for_cascade(struct dsa_switch_tree *dst,
					      struct dsa_tree_cascade *cascade)
{
	struct device_node *dest_switch_dn, *dest_ports_dn;
	struct dsa_tree_component *other_component;
	struct dsa_tree_cascade *other_cascade;
	struct dsa_tree_bfs_elem *elem;
	LIST_HEAD(to_explore);
	LIST_HEAD(explored);
	int err;

	err = dsa_bfs_elem_queue(&to_explore, cascade->dest_port_dn);
	if (err)
		return err;

	while (!list_empty(&to_explore)) {
		elem = dsa_bfs_elem_dequeue(&to_explore, &explored);

		err = dsa_link_create(dst, cascade->source_port_dn, elem->dn);
		if (err)
			goto free_lists;

		dest_ports_dn = of_get_parent(elem->dn);
		dest_switch_dn = of_get_parent(dest_ports_dn);
		of_node_put(dest_ports_dn);
		other_component = dsa_tree_find_component_by_node(dst, dest_switch_dn);
		of_node_put(dest_switch_dn);

		if (!other_component) {
			pr_err("DSA: port %pOF has cascade towards %pOF which is not a component of this tree\n",
			       cascade->source_port_dn, elem->dn);
			err = -EINVAL;
			goto free_lists;
		}

		list_for_each_entry(other_cascade, &other_component->cascades, list) {
			if (dsa_bfs_elem_visited(other_cascade->dest_port_dn,
						 &to_explore, &explored))
				continue;

			/* The source port is special because we never "visit" it per se */
			if (other_cascade->dest_port_dn == cascade->source_port_dn)
				continue;

			err = dsa_bfs_elem_queue(&to_explore, other_cascade->dest_port_dn);
			if (err)
				goto free_lists;
		}
	}

	dsa_bfs_queue_free(&explored);

	return 0;

free_lists:
	dsa_bfs_queue_free(&explored);
	dsa_bfs_queue_free(&to_explore);

	return err;
}

static void dsa_tree_destroy_rtable(struct dsa_switch_tree *dst)
{
	struct dsa_link *dl, *next;

	list_for_each_entry_safe(dl, next, &dst->rtable, list) {
		list_del(&dl->list);
		dsa_link_free(dl);
	}
}

static int dsa_tree_create_rtable_from_links(struct dsa_switch_tree *dst)
{
	struct dsa_tree_component *component;
	struct dsa_tree_cascade *link;
	int err;

	list_for_each_entry(component, &dst->components, list) {
		list_for_each_entry(link, &component->links, list) {
			err = dsa_link_create(dst, link->source_port_dn,
					      link->dest_port_dn);
			if (err) {
				dsa_tree_destroy_rtable(dst);
				return err;
			}
		}
	}

	return 0;
}

static int dsa_tree_create_rtable(struct dsa_switch_tree *dst, bool has_link,
				  bool has_cascade)
{
	struct dsa_tree_component *component;
	struct dsa_tree_cascade *cascade;
	int err = 0;

	INIT_LIST_HEAD(&dst->rtable);

	if (has_link)
		return dsa_tree_create_rtable_from_links(dst);

	if (!has_cascade)
		return 0;

	list_for_each_entry(component, &dst->components, list) {
		list_for_each_entry(cascade, &component->cascades, list) {
			err = dsa_tree_create_rtable_for_cascade(dst, cascade);
			if (err) {
				dsa_tree_destroy_rtable(dst);
				return err;
			}
		}
	}

	return 0;
}

static struct dsa_switch_tree *dsa_tree_get(int index, struct device_node *dn,
					    struct dsa_chip_data *pdata)
{
	struct dsa_switch_tree *dst;
	bool has_cascade = false;
	bool has_link = false;
	int err;

	dst = dsa_tree_find(index);
	if (dst) {
		kref_get(&dst->refcount);
		return dst;
	}

	dst = dsa_tree_alloc(index);
	if (!dst)
		return ERR_PTR(-ENOMEM);

	if (dn)
		dst->probing_mode = DSA_TREE_PROBING_OF;
	else
		dst->probing_mode = DSA_TREE_PROBING_PDATA;

	if (dst->probing_mode == DSA_TREE_PROBING_OF) {
		err = dsa_tree_explore_device_tree(dst, dn, &has_link,
						   &has_cascade);
		if (err)
			goto err_free_tree;
	}

	err = dsa_tree_create_rtable(dst, has_link, has_cascade);
	if (err)
		goto err_free_components;

	err = dsa_tree_create_dev(dst, dn, pdata);
	if (err)
		goto err_destroy_rtable;

	return dst;

err_destroy_rtable:
	dsa_tree_destroy_rtable(dst);
err_free_components:
	if (dst->probing_mode == DSA_TREE_PROBING_OF)
		dsa_tree_components_free(dst);
err_free_tree:
	list_del(&dst->list);
	kfree(dst);
	return ERR_PTR(err);
}

static void dsa_tree_update_rtable(struct dsa_switch_tree *dst)
{
	struct dsa_link *dl;

	list_for_each_entry(dl, &dst->rtable, list) {
		dl->dp = dsa_tree_find_port_by_node(dst, dl->source_port_dn);
		dl->link_dp = dsa_tree_find_port_by_node(dst, dl->dest_port_dn);
	}
}

static void dsa_tree_update_adjacency(struct dsa_switch_tree *dst)
{
	struct dsa_tree_component *component;
	struct dsa_tree_cascade *cascade;
	struct dsa_port *dp;

	if (!dst->has_adjacency)
		return;

	list_for_each_entry(component, &dst->components, list) {
		list_for_each_entry(cascade, &component->cascades, list) {
			dp = dsa_tree_find_port_by_node(dst, cascade->source_port_dn);
			if (!dp)
				continue;

			dp->link_dp = dsa_tree_find_port_by_node(dst, cascade->dest_port_dn);
		}
	}
}

/* Add this switch's ports to the tree's port list */
static void dsa_switch_touch_ports(struct dsa_switch *ds)
{
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_port *dp;
	int port;

	for (port = 0; port < ds->num_ports; port++) {
		dp = &ds->ports[port];
		list_add_tail(&dp->list, &dst->ports);
	}

	dsa_tree_update_rtable(dst);
	dsa_tree_update_adjacency(dst);
}

/* Remove this switch's ports from the tree's port list */
static void dsa_switch_release_ports(struct dsa_switch *ds)
{
	struct dsa_switch_tree *dst = ds->dst;
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
	}

	dsa_tree_update_rtable(dst);
	dsa_tree_update_adjacency(dst);
}

static int dsa_tree_bind_switch(struct dsa_switch_tree *dst,
				struct dsa_switch *ds)
{
	const struct dsa_device_ops *old_tag_ops;
	enum dsa_tag_protocol old_default_proto;
	int err;

	if (dsa_switch_find(dst->index, ds->index)) {
		dev_err(ds->dev,
			"A DSA switch with index %d already exists in tree %d\n",
			ds->index, dst->index);
		return -EEXIST;
	}

	/* If this isn't the first switch bound to the tree, make sure that its
	 * probing mode coincides with the way in which the tree was created
	 */
	if (dst->probing_mode == DSA_TREE_PROBING_OF && !dev_of_node(ds->dev)) {
		dev_err(ds->dev, "Cannot bind OF-based switch to pdata-based tree\n");
		return -EINVAL;
	}

	if (dst->probing_mode == DSA_TREE_PROBING_PDATA && !ds->cd) {
		dev_err(ds->dev, "Cannot bind pdata-based switch to OF-based tree\n");
		return -EINVAL;
	}

	old_default_proto = dst->default_proto;
	old_tag_ops = dst->tag_ops;

	for (int port = 0; port < ds->num_ports; port++) {
		struct dsa_port *dp = &ds->ports[port];

		if (dsa_port_is_cpu(dp)) {
			err = dsa_port_resolve_tag_protocol(dp, dst);
			if (err)
				goto restore_tag_proto;
		}
	}

	if (dst->probing_mode == DSA_TREE_PROBING_OF) {
		struct dsa_tree_component *component;

		component = dsa_tree_find_component(dst, ds);
		if (!component) {
			dev_err(ds->dev, "OF node %pOF unrecognized by tree %s\n",
				dev_of_node(ds->dev), dev_name(dst->dev));
			err = -ENODEV;
			goto restore_tag_proto;
		}

		component->ds = ds;
		component->state = DSA_TREE_COMPONENT_BOUND;
	}

	if (dst->last_switch < ds->index)
		dst->last_switch = ds->index;

	ds->dst = dst;

	dsa_switch_touch_ports(ds);

	return 0;

restore_tag_proto:
	dst->default_proto = old_default_proto;
	dst->tag_ops = old_tag_ops;

	return err;
}

static void dsa_tree_unbind_switch(struct dsa_switch_tree *dst,
				   struct dsa_switch *ds)
{
	struct dsa_tree_component *component;
	int last_switch = -1;

	dsa_switch_release_ports(ds);

	if (dst->probing_mode != DSA_TREE_PROBING_OF)
		return;

	component = dsa_tree_find_component(dst, ds);
	if (WARN_ON(!component))
		return;
	if (WARN_ON(component->state == DSA_TREE_COMPONENT_UNBOUND))
		return;

	component->ds = NULL;
	component->state = DSA_TREE_COMPONENT_UNBOUND;

	/* Recalculate the index of the last switch, needed in other places */
	list_for_each_entry(component, &dst->components, list)
		if (component->ds && last_switch < component->ds->index)
			last_switch = ds->index;

	dst->last_switch = last_switch;
}

int dsa_switch_get_tree(struct dsa_switch *ds)
{
	struct device_node *switch_dn;
	int tree_index, switch_index;
	struct dsa_switch_tree *dst;
	struct dsa_chip_data *pdata;
	int err;

	pdata = ds->dev->platform_data;
	switch_dn = dev_of_node(ds->dev);

	if (switch_dn) {
		err = dsa_switch_parse_member_of(switch_dn, &switch_index,
						 &tree_index);
		if (err)
			return err;
	} else {
		/* We don't support interconnected switches nor multiple trees
		 * via platform data, so this is the unique switch of the tree.
		 */
		tree_index = 0;
		switch_index = 0;
	}

	ds->index = switch_index;

	dst = dsa_tree_get(tree_index, switch_dn, pdata);
	if (IS_ERR(dst))
		return PTR_ERR(dst);

	err = dsa_tree_bind_switch(dst, ds);
	if (err)
		goto out_put_tree;

	if (dsa_tree_complete(dst)) {
		err = dsa_tree_setup(dst);
		if (err)
			goto out_unbind_switch;
	}

	return 0;

out_unbind_switch:
	dsa_tree_unbind_switch(dst, ds);
out_put_tree:
	dsa_tree_put(dst);
	return err;
}

void dsa_switch_put_tree(struct dsa_switch *ds)
{
	struct dsa_switch_tree *dst = ds->dst;

	if (dsa_tree_complete(dst))
		dsa_tree_teardown(dst);
	dsa_tree_unbind_switch(dst, ds);
	dsa_tree_put(dst);
}

/* Return the local port used to reach an arbitrary switch device */
unsigned int dsa_routing_port(struct dsa_switch *ds, int device)
{
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_link *dl;

	list_for_each_entry(dl, &dst->rtable, list)
		if (dl->dp->ds == ds && dl->link_dp->ds->index == device)
			return dl->dp->index;

	return ds->num_ports;
}
EXPORT_SYMBOL_GPL(dsa_routing_port);

/* Return the local port used to reach an arbitrary switch port */
unsigned int dsa_towards_port(struct dsa_switch *ds, int device, int port)
{
	if (device == ds->index)
		return port;
	else
		return dsa_routing_port(ds, device);
}
EXPORT_SYMBOL_GPL(dsa_towards_port);

/* Return the local port used to reach the dedicated CPU port */
unsigned int dsa_upstream_port(struct dsa_switch *ds, int port)
{
	const struct dsa_port *dp = dsa_to_port(ds, port);
	const struct dsa_port *cpu_dp = dp->cpu_dp;

	if (!cpu_dp)
		return port;

	return dsa_towards_port(ds, cpu_dp->ds->index, cpu_dp->index);
}
EXPORT_SYMBOL_GPL(dsa_upstream_port);

/* Return true if this is the local port used to reach the CPU port */
bool dsa_is_upstream_port(struct dsa_switch *ds, int port)
{
	if (dsa_is_unused_port(ds, port))
		return false;

	return port == dsa_upstream_port(ds, port);
}
EXPORT_SYMBOL_GPL(dsa_is_upstream_port);

/* Return true if this is a DSA port leading away from the CPU */
bool dsa_is_downstream_port(struct dsa_switch *ds, int port)
{
	return dsa_is_dsa_port(ds, port) && !dsa_is_upstream_port(ds, port);
}
EXPORT_SYMBOL_GPL(dsa_is_downstream_port);

/* Return the local port used to reach the CPU port */
unsigned int dsa_switch_upstream_port(struct dsa_switch *ds)
{
	struct dsa_port *dp;

	dsa_switch_for_each_available_port(dp, ds) {
		return dsa_upstream_port(ds, dp->index);
	}

	return ds->num_ports;
}
EXPORT_SYMBOL_GPL(dsa_switch_upstream_port);

/* Return true if @upstream_ds is an upstream switch of @downstream_ds, meaning
 * that the routing port from @downstream_ds to @upstream_ds is also the port
 * which @downstream_ds uses to reach its dedicated CPU.
 */
bool dsa_switch_is_upstream_of(struct dsa_switch *upstream_ds,
			       struct dsa_switch *downstream_ds)
{
	int routing_port;

	if (upstream_ds == downstream_ds)
		return true;

	routing_port = dsa_routing_port(downstream_ds, upstream_ds->index);

	return dsa_is_upstream_port(downstream_ds, routing_port);
}

int dsa_tree_class_register(void)
{
	return class_register(&dsa_switch_tree_class);
}

void dsa_tree_class_unregister(void)
{
	class_unregister(&dsa_switch_tree_class);
}
