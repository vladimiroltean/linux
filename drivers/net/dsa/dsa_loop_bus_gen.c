// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/dsa_loop_bus.yaml */
/* YNL-GEN kernel source */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "dsa_loop_bus_gen.h"

#include <uapi/linux/dsa_loop_bus.h>
#include "dsa_loop_bus_main.h"

/* Common nested types */
const struct nla_policy dsa_loop_bus_port_nl_policy[DSA_LOOP_BUS_A_PORT_CONDUIT_IFINDEX + 1] = {
	[DSA_LOOP_BUS_A_PORT_INDEX] = { .type = NLA_U32, },
	[DSA_LOOP_BUS_A_PORT_TYPE] = NLA_POLICY_MAX(NLA_U32, 1),
	[DSA_LOOP_BUS_A_PORT_LABEL] = { .type = NLA_NUL_STRING, },
	[DSA_LOOP_BUS_A_PORT_CONDUIT_IFINDEX] = { .type = NLA_U32, },
};

/* DSA_LOOP_BUS_CMD_NEW - do */
static const struct nla_policy dsa_loop_bus_new_nl_policy[DSA_LOOP_BUS_A_PORT + 1] = {
	[DSA_LOOP_BUS_A_TREE_INDEX] = { .type = NLA_U32, },
	[DSA_LOOP_BUS_A_SWITCH_ID] = { .type = NLA_U32, },
	[DSA_LOOP_BUS_A_TAG_PROTO] = { .type = NLA_NUL_STRING, },
	[DSA_LOOP_BUS_A_PORT] = NLA_POLICY_NESTED(dsa_loop_bus_port_nl_policy),
};

/* DSA_LOOP_BUS_CMD_DEL - do */
static const struct nla_policy dsa_loop_bus_del_nl_policy[DSA_LOOP_BUS_A_SWITCH_ID + 1] = {
	[DSA_LOOP_BUS_A_TREE_INDEX] = { .type = NLA_U32, },
	[DSA_LOOP_BUS_A_SWITCH_ID] = { .type = NLA_U32, },
};

/* Ops table for dsa_loop_bus */
static const struct genl_split_ops dsa_loop_bus_nl_ops[] = {
	{
		.cmd		= DSA_LOOP_BUS_CMD_NEW,
		.doit		= dsa_loop_bus_nl_new_doit,
		.policy		= dsa_loop_bus_new_nl_policy,
		.maxattr	= DSA_LOOP_BUS_A_PORT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DSA_LOOP_BUS_CMD_DEL,
		.doit		= dsa_loop_bus_nl_del_doit,
		.policy		= dsa_loop_bus_del_nl_policy,
		.maxattr	= DSA_LOOP_BUS_A_SWITCH_ID,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
};

static void __dsa_loop_bus_nl_sock_priv_init(void *priv)
{
	dsa_loop_bus_nl_sock_priv_init(priv);
}

static void __dsa_loop_bus_nl_sock_priv_destroy(void *priv)
{
	dsa_loop_bus_nl_sock_priv_destroy(priv);
}

struct genl_family dsa_loop_bus_nl_family __ro_after_init = {
	.name		= DSA_LOOP_BUS_FAMILY_NAME,
	.version	= DSA_LOOP_BUS_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= dsa_loop_bus_nl_ops,
	.n_split_ops	= ARRAY_SIZE(dsa_loop_bus_nl_ops),
	.sock_priv_size	= sizeof(struct dsa_loop_bus_priv),
	.sock_priv_init	= __dsa_loop_bus_nl_sock_priv_init,
	.sock_priv_destroy = __dsa_loop_bus_nl_sock_priv_destroy,
};
