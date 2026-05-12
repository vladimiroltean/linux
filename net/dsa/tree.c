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

static struct dsa_switch_tree *dsa_tree_alloc(int index)
{
	struct dsa_switch_tree *dst;

	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return NULL;

	dst->index = index;

	INIT_LIST_HEAD(&dst->rtable);

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

static struct dsa_link *dsa_link_touch(struct dsa_port *dp,
				       struct dsa_port *link_dp)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_switch_tree *dst;
	struct dsa_link *dl;

	dst = ds->dst;

	list_for_each_entry(dl, &dst->rtable, list)
		if (dl->dp == dp && dl->link_dp == link_dp)
			return dl;

	dl = kzalloc(sizeof(*dl), GFP_KERNEL);
	if (!dl)
		return NULL;

	dl->dp = dp;
	dl->link_dp = link_dp;

	INIT_LIST_HEAD(&dl->list);
	list_add_tail(&dl->list, &dst->rtable);

	return dl;
}

static bool dsa_port_setup_routing_table(struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_switch_tree *dst = ds->dst;
	struct device_node *dn = dp->dn;
	struct of_phandle_iterator it;
	struct dsa_port *link_dp;
	struct dsa_link *dl;
	int err;

	of_for_each_phandle(&it, err, dn, "link", NULL, 0) {
		link_dp = dsa_tree_find_port_by_node(dst, it.node);
		if (!link_dp) {
			of_node_put(it.node);
			return false;
		}

		dl = dsa_link_touch(dp, link_dp);
		if (!dl) {
			of_node_put(it.node);
			return false;
		}
	}

	return true;
}

static bool dsa_tree_setup_routing_table(struct dsa_switch_tree *dst)
{
	bool complete = true;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dsa_port_is_dsa(dp)) {
			complete = dsa_port_setup_routing_table(dp);
			if (!complete)
				break;
		}
	}

	return complete;
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

static void dsa_tree_teardown_routing_table(struct dsa_switch_tree *dst)
{
	struct dsa_link *dl, *next;

	list_for_each_entry_safe(dl, next, &dst->rtable, list) {
		list_del(&dl->list);
		kfree(dl);
	}
}

int dsa_tree_setup(struct dsa_switch_tree *dst)
{
	bool complete;
	int err;

	if (dst->setup) {
		pr_err("DSA: tree %d already setup! Disjoint trees?\n",
		       dst->index);
		return -EEXIST;
	}

	complete = dsa_tree_setup_routing_table(dst);
	if (!complete)
		return 0;

	err = dsa_tree_setup_cpu_ports(dst);
	if (err)
		goto teardown_rtable;

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

	dst->setup = true;

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
teardown_rtable:
	dsa_tree_teardown_routing_table(dst);

	return err;
}

void dsa_tree_teardown(struct dsa_switch_tree *dst)
{
	if (!dst->setup)
		return;

	dsa_tree_teardown_lags(dst);

	dsa_tree_teardown_conduit(dst);

	dsa_tree_teardown_ports(dst);

	dsa_tree_teardown_switches(dst);

	dsa_tree_teardown_cpu_ports(dst);

	dsa_tree_teardown_routing_table(dst);

	pr_info("DSA: tree %d torn down\n", dst->index);

	dst->setup = false;
}

static void dsa_tree_free(struct dsa_switch_tree *dst)
{
	if (dst->tag_ops)
		dsa_tag_driver_put(dst->tag_ops);
	list_del(&dst->list);
	kfree(dst);
}

static void dsa_tree_release(struct kref *ref)
{
	struct dsa_switch_tree *dst;

	dst = container_of(ref, struct dsa_switch_tree, refcount);

	dsa_tree_free(dst);
}

void dsa_tree_put(struct dsa_switch_tree *dst)
{
	if (dst)
		kref_put(&dst->refcount, dsa_tree_release);
}

struct dsa_switch_tree *dsa_tree_get(struct dsa_switch_tree *dst)
{
	if (dst)
		kref_get(&dst->refcount);

	return dst;
}

struct dsa_switch_tree *dsa_tree_touch(int index)
{
	struct dsa_switch_tree *dst;

	dst = dsa_tree_find(index);
	if (dst)
		return dsa_tree_get(dst);
	else
		return dsa_tree_alloc(index);
}
