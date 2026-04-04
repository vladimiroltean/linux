/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/dsa_loop_bus.yaml */
/* YNL-GEN uapi header */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _UAPI_LINUX_DSA_LOOP_BUS_H
#define _UAPI_LINUX_DSA_LOOP_BUS_H

#define DSA_LOOP_BUS_FAMILY_NAME	"dsa-loop-bus"
#define DSA_LOOP_BUS_FAMILY_VERSION	1

enum dsa_loop_bus_port_type {
	DSA_LOOP_BUS_PORT_TYPE_USER,
	DSA_LOOP_BUS_PORT_TYPE_CPU,
};

enum {
	DSA_LOOP_BUS_A_TREE_INDEX = 1,
	DSA_LOOP_BUS_A_SWITCH_ID,
	DSA_LOOP_BUS_A_TAG_PROTO,
	DSA_LOOP_BUS_A_PORT,

	__DSA_LOOP_BUS_A_MAX,
	DSA_LOOP_BUS_A_MAX = (__DSA_LOOP_BUS_A_MAX - 1)
};

enum {
	DSA_LOOP_BUS_A_PORT_INDEX = 1,
	DSA_LOOP_BUS_A_PORT_TYPE,
	DSA_LOOP_BUS_A_PORT_LABEL,
	DSA_LOOP_BUS_A_PORT_CONDUIT_IFINDEX,

	__DSA_LOOP_BUS_A_PORT_MAX,
	DSA_LOOP_BUS_A_PORT_MAX = (__DSA_LOOP_BUS_A_PORT_MAX - 1)
};

enum {
	DSA_LOOP_BUS_CMD_NEW = 1,
	DSA_LOOP_BUS_CMD_DEL,

	__DSA_LOOP_BUS_CMD_MAX,
	DSA_LOOP_BUS_CMD_MAX = (__DSA_LOOP_BUS_CMD_MAX - 1)
};

#endif /* _UAPI_LINUX_DSA_LOOP_BUS_H */
