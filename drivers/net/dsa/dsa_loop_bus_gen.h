/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/dsa_loop_bus.yaml */
/* YNL-GEN kernel header */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _LINUX_DSA_LOOP_BUS_GEN_H
#define _LINUX_DSA_LOOP_BUS_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/dsa_loop_bus.h>
#include "dsa_loop_bus_main.h"

/* Common nested types */
extern const struct nla_policy dsa_loop_bus_port_nl_policy[DSA_LOOP_BUS_A_PORT_CONDUIT_IFINDEX + 1];

int dsa_loop_bus_nl_new_doit(struct sk_buff *skb, struct genl_info *info);
int dsa_loop_bus_nl_del_doit(struct sk_buff *skb, struct genl_info *info);

extern struct genl_family dsa_loop_bus_nl_family;

void dsa_loop_bus_nl_sock_priv_init(struct dsa_loop_bus_priv *priv);
void dsa_loop_bus_nl_sock_priv_destroy(struct dsa_loop_bus_priv *priv);

#endif /* _LINUX_DSA_LOOP_BUS_GEN_H */
