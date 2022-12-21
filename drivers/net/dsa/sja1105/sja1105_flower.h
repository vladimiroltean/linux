// SPDX-License-Identifier: GPL-2.0
/* Copyright 2020 NXP
 */
#ifndef _SJA1105_FLOWER_H
#define _SJA1105_FLOWER_H

#include <linux/types.h>

struct dsa_switch;
struct flow_cls_offload;
struct sja1105_private;

enum sja1105_key_type {
	SJA1105_KEY_BCAST,
	SJA1105_KEY_TC,
	SJA1105_KEY_VLAN_UNAWARE_VL,
	SJA1105_KEY_VLAN_AWARE_VL,
};

struct sja1105_key {
	enum sja1105_key_type type;

	union {
		/* SJA1105_KEY_TC */
		struct {
			int pcp;
		} tc;

		/* SJA1105_KEY_VLAN_UNAWARE_VL */
		/* SJA1105_KEY_VLAN_AWARE_VL */
		struct {
			u64 dmac;
			u16 vid;
			u16 pcp;
		} vl;
	};
};

enum sja1105_rule_type {
	SJA1105_RULE_BCAST_POLICER,
	SJA1105_RULE_TC_POLICER,
	SJA1105_RULE_VL,
};

int sja1105_cls_flower_del(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress);
int sja1105_cls_flower_add(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress);
int sja1105_cls_flower_stats(struct dsa_switch *ds, int port,
			     struct flow_cls_offload *cls, bool ingress);
void sja1105_flower_setup(struct dsa_switch *ds);
void sja1105_flower_teardown(struct dsa_switch *ds);
struct sja1105_rule *sja1105_rule_find(struct sja1105_private *priv,
				       unsigned long cookie);

#endif
