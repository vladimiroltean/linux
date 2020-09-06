// SPDX-License-Identifier: GPL-2.0
/* Copyright 2020 NXP Semiconductors
 *
 * An implementation of the software-defined tag_8021q.c tagger format, which
 * also preserves full functionality under a vlan_filtering bridge. It does
 * this by using the TCAM engines for:
 * - pushing the RX VLAN as a second, outer tag, on egress towards the CPU port
 * - redirecting towards the correct front port based on TX VLAN and popping
 *   that on egress
 */
#include "../../ethernet/mscc/ocelot_ace.h"
#include <soc/mscc/ocelot_vcap.h>
#include <linux/dsa/8021q.h>
#include "felix.h"

static int felix_tag_8021q_rxvlan_add(struct ocelot *ocelot, int port, u16 vid,
				      bool pvid, bool untagged)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	int cpu = dsa_upstream_port(felix->ds, port);
	struct ocelot_ace_rule *outer_tagging_rule;

	if (!pvid)
		return 0;

	outer_tagging_rule = kzalloc(sizeof(struct ocelot_ace_rule),
				     GFP_KERNEL);
	if (!outer_tagging_rule)
		return -ENOMEM;

	outer_tagging_rule->type = OCELOT_ACE_TYPE_ANY;
	outer_tagging_rule->prio = 2;
	outer_tagging_rule->id = 5000 + port;
	outer_tagging_rule->vcap_id = VCAP_ES0;
	outer_tagging_rule->es0_action.vlan_push_ena = true;
	outer_tagging_rule->es0_action.vid = vid;
	outer_tagging_rule->es0_action.pcp = 5;
	outer_tagging_rule->ingress_port = port;
	outer_tagging_rule->egress_port = cpu;

	return ocelot_ace_rule_offload_add(ocelot, outer_tagging_rule, NULL);
}

static int felix_tag_8021q_txvlan_add(struct ocelot *ocelot, int port, u16 vid,
				      bool pvid, bool untagged)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	int cpu = dsa_upstream_port(felix->ds, port);
	struct ocelot_ace_rule *untagging_rule;
	struct ocelot_ace_rule *redirect_rule;
	int ret;

	if (port == cpu)
		return 0;

	untagging_rule = kzalloc(sizeof(struct ocelot_ace_rule), GFP_KERNEL);
	if (!untagging_rule)
		return -ENOMEM;

	redirect_rule = kzalloc(sizeof(struct ocelot_ace_rule), GFP_KERNEL);
	if (!redirect_rule) {
		kfree(untagging_rule);
		return -ENOMEM;
	}

	untagging_rule->type = OCELOT_ACE_TYPE_ANY;
	untagging_rule->ingress_port_mask = BIT(cpu);
	untagging_rule->vlan.vid.value = vid;
	untagging_rule->vlan.vid.mask = VLAN_VID_MASK;
	untagging_rule->prio = 2;
	untagging_rule->id = 6000 + port;
	untagging_rule->vcap_id = VCAP_IS1;
	untagging_rule->is1_action.pop_cnt = 1;
	untagging_rule->is1_action.pag_override_ena = true;
	untagging_rule->is1_action.pag_override_mask = 0xff;
	untagging_rule->is1_action.pag_val = 10 + port;

	ret = ocelot_ace_rule_offload_add(ocelot, untagging_rule, NULL);
	if (ret) {
		kfree(untagging_rule);
		kfree(redirect_rule);
		return ret;
	}

	redirect_rule->type = OCELOT_ACE_TYPE_ANY;
	redirect_rule->ingress_port_mask = BIT(cpu);
	redirect_rule->pag_mask = 0xff;
	redirect_rule->pag_val = 10 + port;
	redirect_rule->prio = 2;
	redirect_rule->id = 7000 + port;
	redirect_rule->vcap_id = VCAP_IS2;
	redirect_rule->is2_action.redir_ena = true;
	redirect_rule->is2_action.redir_port_mask = BIT(port);

	ret = ocelot_ace_rule_offload_add(ocelot, redirect_rule, NULL);
	if (ret) {
		kfree(untagging_rule);
		kfree(redirect_rule);
		return ret;
	}

	return 0;
}

int felix_tag_8021q_vlan_add(struct ocelot *ocelot, int port, u16 vid,
			     bool pvid, bool untagged)
{
	if (vid_is_dsa_8021q_rxvlan(vid))
		return felix_tag_8021q_rxvlan_add(ocelot, port, vid, pvid,
						  untagged);

	if (vid_is_dsa_8021q_txvlan(vid))
		return felix_tag_8021q_txvlan_add(ocelot, port, vid, pvid,
						  untagged);

	return 0;
}

int felix_setup_8021q_tagging(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct dsa_switch *ds = felix->ds;
	int rc, port;

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		felix->expect_dsa_8021q = true;
		rc = dsa_port_setup_8021q_tagging(ds, port, true);
		felix->expect_dsa_8021q = false;
		if (rc < 0) {
			dev_err(ds->dev,
				"Failed to setup VLAN tagging for port %d: %d\n",
				port, rc);
			return rc;
		}
	}

	return 0;
}
