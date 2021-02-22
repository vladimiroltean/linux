// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2013 Florian Fainelli <florian@openwrt.org>
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 * Copyright (c) 2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 * Copyright (c) 2021 Vladimir Oltean <vladimir.oltean@nxp.com>
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

#define WARN(condition, format...) ({					\
	int __ret_warn_on = !!(condition);				\
	if (__ret_warn_on)						\
		printf(format);						\
	__ret_warn_on;							\
})
#define WARN_ON(x) WARN((x), "%s", "WARN_ON(" __stringify(x) ")")

STAILQ_HEAD(dst_head, dsa_switch_tree) dsa_tree_list = STAILQ_HEAD_INITIALIZER(dsa_tree_list);

struct dsa_port {
	enum {
		DSA_PORT_TYPE_UNUSED = 0,
		DSA_PORT_TYPE_CPU,
		DSA_PORT_TYPE_DSA,
		DSA_PORT_TYPE_USER,
	} type;

	struct dsa_switch	*ds;
	unsigned int		index;
	struct dsa_port		*cpu_dp;

	STAILQ_ENTRY(dsa_port)	list;
};

/* TODO: ideally DSA ports would have a single dp->link_dp member,
 * and no dst->rtable nor this struct dsa_link would be needed,
 * but this would require some more complex tree walking,
 * so keep it stupid at the moment and list them all.
 */
struct dsa_link {
	struct dsa_port		*dp;
	struct dsa_port		*link_dp;
	STAILQ_ENTRY(dsa_link)	list;
};

struct dsa_switch_tree {
	/* List of switch ports */
	STAILQ_HEAD(ports_head, dsa_port) ports;

	/* List of switches (for notifiers) */
	STAILQ_HEAD(switches_head, dsa_switch) switches;

	/* List of DSA links composing the routing table */
	STAILQ_HEAD(rtable_head, dsa_link) rtable;

	STAILQ_ENTRY(dsa_switch_tree) list;

	int index;
};

struct dsa_switch {
	/*
	 * Parent switch tree, and switch index.
	 */
	struct dsa_switch_tree		*dst;
	unsigned int			index;

	STAILQ_ENTRY(dsa_switch)	list;

	bool				*heat_map;

	size_t				num_ports;
};

static const char dsa_port_type[][16] = {
	[DSA_PORT_TYPE_UNUSED]	= "unused ",
	[DSA_PORT_TYPE_CPU]	= "  cpu  ",
	[DSA_PORT_TYPE_DSA]	= "  dsa  ",
	[DSA_PORT_TYPE_USER]	= "  user ",
};

static struct dsa_port *dsa_to_port(struct dsa_switch *ds, int p)
{
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_port *dp;

	STAILQ_FOREACH(dp, &dst->ports, list)
		if (dp->ds == ds && dp->index == p)
			return dp;

	return NULL;
}

static bool dsa_is_unused_port(struct dsa_switch *ds, int p)
{
	return dsa_to_port(ds, p)->type == DSA_PORT_TYPE_UNUSED;
}

static bool dsa_is_cpu_port(struct dsa_switch *ds, int p)
{
	return dsa_to_port(ds, p)->type == DSA_PORT_TYPE_CPU;
}

static bool dsa_is_dsa_port(struct dsa_switch *ds, int p)
{
	return dsa_to_port(ds, p)->type == DSA_PORT_TYPE_DSA;
}

static bool dsa_is_user_port(struct dsa_switch *ds, int p)
{
	return dsa_to_port(ds, p)->type == DSA_PORT_TYPE_USER;
}

/* Return the local port used to reach an arbitrary switch device */
static unsigned int dsa_routing_port(struct dsa_switch *ds, int device)
{
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_link *dl;

	STAILQ_FOREACH(dl, &dst->rtable, list) {
		if (dl->dp->ds == ds && dl->link_dp->ds->index == device)
			return dl->dp->index;
	}

	return ds->num_ports;
}

/* Return the local port used to reach an arbitrary switch port */
static unsigned int dsa_towards_port(struct dsa_switch *ds, int device,
				     int port)
{
	if (device == ds->index)
		return port;
	else
		return dsa_routing_port(ds, device);
}

/* Return the local port used to reach the dedicated CPU port */
static unsigned int dsa_upstream_port(struct dsa_switch *ds, int port)
{
	const struct dsa_port *dp = dsa_to_port(ds, port);
	const struct dsa_port *cpu_dp = dp->cpu_dp;

	if (!cpu_dp)
		return port;

	return dsa_towards_port(ds, cpu_dp->ds->index, cpu_dp->index);
}

static bool dsa_is_upstream_port(struct dsa_switch *ds, int port)
{
	if (dsa_is_unused_port(ds, port))
		return false;

	return port == dsa_upstream_port(ds, port);
}

static bool dsa_switch_is_upstream_of(struct dsa_switch *upstream_ds,
				      struct dsa_switch *downstream_ds)
{
	int routing_port;

	if (upstream_ds == downstream_ds)
		return false;

	routing_port = dsa_routing_port(downstream_ds, upstream_ds->index);

	return dsa_is_upstream_port(downstream_ds, routing_port);
}

static int dsa_register_switch(struct dsa_switch_tree *dst, int index,
			       int num_ports)
{
	struct dsa_switch *ds;
	struct dsa_port *dp;
	int err, port;

	ds = calloc(1, sizeof(*ds));
	if (!ds)
		return -ENOMEM;

	ds->heat_map = calloc(num_ports, sizeof(bool));
	if (!ds->heat_map) {
		free(ds);
		return -ENOMEM;
	}

	ds->num_ports = num_ports;
	ds->index = index;
	ds->dst = dst;

	for (port = 0; port < num_ports; port++) {
		dp = calloc(1, sizeof(*dp));
		if (!dp) {
			err = -ENOMEM;
			goto out_free_ports;
		}

		dp->ds = ds;
		dp->index = port;
		STAILQ_INSERT_TAIL(&dst->ports, dp, list);
	}

	STAILQ_INSERT_TAIL(&dst->switches, ds, list);

	return 0;

out_free_ports:
	while ((dp = STAILQ_FIRST(&dst->ports))) {
		STAILQ_REMOVE_HEAD(&dst->ports, list);
		free(dp);
	}

	free(ds);
	return err;
}

static void dsa_tree_teardown(struct dsa_switch_tree *dst)
{
	struct dsa_switch *ds;
	struct dsa_port *dp;
	struct dsa_link *dl;

	while (dl = STAILQ_FIRST(&dst->rtable)) {
		STAILQ_REMOVE_HEAD(&dst->rtable, list);
		free(dl);
	}

	while (dp = STAILQ_FIRST(&dst->ports)) {
		STAILQ_REMOVE_HEAD(&dst->ports, list);
		free(dp);
	}

	while (ds = STAILQ_FIRST(&dst->switches)) {
		STAILQ_REMOVE_HEAD(&dst->switches, list);
		free(ds->heat_map);
		free(ds);
	}
}

static struct dsa_port *dsa_tree_find_port_by_index(struct dsa_switch_tree *dst,
						    int sw_index, int port)
{
	struct dsa_port *dp;

	STAILQ_FOREACH(dp, &dst->ports, list)
		if (dp->ds->index == sw_index && dp->index == port)
			return dp;

	return NULL;
}

static struct dsa_switch *dsa_switch_find(int tree_index, int sw_index)
{
	struct dsa_switch_tree *dst;
	struct dsa_port *dp;

	STAILQ_FOREACH(dst, &dsa_tree_list, list) {
		if (dst->index != tree_index)
			continue;

		STAILQ_FOREACH(dp, &dst->ports, list) {
			if (dp->ds->index != sw_index)
				continue;

			return dp->ds;
		}
	}

	return NULL;
}

static int dsa_setup_link(struct dsa_switch_tree *dst, int from_sw_index,
			  int from_port, int to_sw_index, int to_port)
{
	struct dsa_port *dp, *link_dp;
	struct dsa_link *dl;

	dp = dsa_tree_find_port_by_index(dst, from_sw_index, from_port);
	if (!dp) {
		fprintf(stderr, "failed to find sw%dp%d\n", from_sw_index,
			from_port);
		return -ENODEV;
	}

	link_dp = dsa_tree_find_port_by_index(dst, to_sw_index, to_port);
	if (!link_dp) {
		fprintf(stderr, "failed to find sw%dp%d\n", to_sw_index,
			to_port);
		return -ENODEV;
	}

	dl = calloc(1, sizeof(*dl));
	if (!dl)
		return -ENOMEM;

	dl->dp = dp;
	dp->type = DSA_PORT_TYPE_DSA;
	dl->link_dp = link_dp;
	link_dp->type = DSA_PORT_TYPE_DSA;

	STAILQ_INSERT_HEAD(&dst->rtable, dl, list);

	return 0;
}

static int dsa_tree_setup_default_cpu(struct dsa_switch_tree *dst,
				      int sw_index, int port)
{
	struct dsa_port *dp, *cpu_dp;

	cpu_dp = dsa_tree_find_port_by_index(dst, sw_index, port);
	if (!cpu_dp) {
		fprintf(stderr, "failed to find sw%dp%d\n", sw_index, port);
		return -ENODEV;
	}

	cpu_dp->type = DSA_PORT_TYPE_CPU;

	STAILQ_FOREACH(dp, &dst->ports, list)
		if (dsa_is_user_port(dp->ds, dp->index) ||
		    dsa_is_dsa_port(dp->ds, dp->index))
			dp->cpu_dp = cpu_dp;

	return 0;
}

static void
dsa_tree_convert_all_unused_ports_to_user(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	STAILQ_FOREACH(dp, &dst->ports, list)
		if (dsa_is_unused_port(dp->ds, dp->index))
			dp->type = DSA_PORT_TYPE_USER;
}

/*  CPU
 *   |
 * sw0p0 sw0p1 sw0p2 sw0p3 sw0p4
 *                     |
 *                     | DSA link
 *                     +-----+
 *                           |
 *                           |
 * sw1p0 sw1p1 sw1p2 sw1p3 sw1p4
 *                     |
 *                     | DSA link
 *                     +-----+
 *                           |
 *                           |
 * sw2p0 sw2p1 sw2p2 sw2p3 sw2p4
 */
static int dsa_setup_tree(struct dsa_switch_tree *dst)
{
	int err;

	STAILQ_INIT(&dst->ports);
	STAILQ_INIT(&dst->switches);
	STAILQ_INIT(&dst->rtable);

	err = dsa_register_switch(dst, 0, 5);
	if (err)
		return err;

	err = dsa_register_switch(dst, 1, 5);
	if (err)
		return err;

	err = dsa_register_switch(dst, 2, 5);
	if (err)
		return err;

	/* sw0p3 to sw1p4 */
	err = dsa_setup_link(dst, 0, 3, 1, 4);
	if (err)
		return err;

	/* sw0p3 to sw2p4 */
	err = dsa_setup_link(dst, 0, 3, 2, 4);
	if (err)
		return err;

	/* sw1p4 to sw0p3 */
	err = dsa_setup_link(dst, 1, 4, 0, 3);
	if (err)
		return err;

	/* sw1p3 to sw2p4 */
	err = dsa_setup_link(dst, 1, 3, 2, 4);
	if (err)
		return err;

	/* sw2p4 to sw0p3 */
	err = dsa_setup_link(dst, 2, 4, 0, 3);
	if (err)
		return err;

	/* sw2p4 to sw1p3 */
	err = dsa_setup_link(dst, 2, 4, 1, 3);
	if (err)
		return err;

	dsa_tree_convert_all_unused_ports_to_user(dst);

	err = dsa_tree_setup_default_cpu(dst, 0, 0);
	if (err)
		return err;

	STAILQ_INSERT_TAIL(&dsa_tree_list, dst, list);

	return 0;
}

enum {
	DSA_NOTIFIER_TEST,
};

struct dsa_notifier_test_info {
	bool propagate_upstream;
	int sw_index;
	int port;
};

static int dsa_switch_test_match(struct dsa_switch *ds, int port,
				 struct dsa_notifier_test_info *info)
{
	if (ds->index == info->sw_index)
		return (port == info->port) || dsa_is_dsa_port(ds, port);

	if (!info->propagate_upstream)
		return false;

	if (dsa_is_dsa_port(ds, port) || dsa_is_cpu_port(ds, port))
		return true;
}

static int dsa_switch_test(struct dsa_switch *ds,
			   struct dsa_notifier_test_info *info)
{
	int port;

	for (port = 0; port < ds->num_ports; port++)
		if (dsa_switch_test_match(ds, port, info))
			ds->heat_map[port] = true;

	return 0;
}

static int dsa_switch_event(struct dsa_switch *ds, unsigned long event,
			    void *info)
{
	int err;

	switch (event) {
	case DSA_NOTIFIER_TEST:
		err = dsa_switch_test(ds, info);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int dsa_tree_notify(struct dsa_switch_tree *dst, unsigned long e, void *v)
{
	struct dsa_switch *ds;
	int err;

	STAILQ_FOREACH(ds, &dst->switches, list) {
		err = dsa_switch_event(ds, e, v);
		if (err)
			return err;
	}

	return 0;
}

static int dsa_port_notify(const struct dsa_port *dp, unsigned long e, void *v)
{
	return dsa_tree_notify(dp->ds->dst, e, v);
}

static int dsa_test_notify(struct dsa_switch_tree *dst, int sw_index, int port)
{
	struct dsa_notifier_test_info info = {
		.sw_index = sw_index,
		.port = port,
		.propagate_upstream = true,
	};
	struct dsa_switch *ds;
	struct dsa_port *dp;
	int err;

	dp = dsa_tree_find_port_by_index(dst, sw_index, port);
	if (!dp) {
		fprintf(stderr, "failed to find sw%dp%d\n", sw_index, port);
		return -ENODEV;
	}

	err = dsa_port_notify(dp, DSA_NOTIFIER_TEST, &info);
	if (err)
		return err;

	printf("Heat map for test notifier emitted on sw%dp%d:\n\n",
	       sw_index, port);

	STAILQ_FOREACH(ds, &dst->switches, list) {
		for (port = 0; port < ds->num_ports; port++)
			printf("   sw%dp%d  ", ds->index, port);
		printf("\n");
		for (port = 0; port < ds->num_ports; port++)
			printf("[%s] ", dsa_port_type[dsa_to_port(ds, port)->type]);
		printf("\n");
		for (port = 0; port < ds->num_ports; port++) {
			if (ds->heat_map[port])
				printf("[   x   ] ");
			else
				printf("[       ] ");
		}
		printf("\n\n");
		memset(ds->heat_map, 0, sizeof(bool) * ds->num_ports);
	}
}

int main(void)
{
	struct dsa_switch_tree dst = {0};
	struct dsa_port *dp;
	int err;

	err = dsa_setup_tree(&dst);
	if (err)
		goto out;

	err = dsa_test_notify(&dst, 2, 1);
	if (err)
		goto out;

	err = dsa_test_notify(&dst, 1, 0);
	if (err)
		goto out;

out:
	dsa_tree_teardown(&dst);

	return err;
}
