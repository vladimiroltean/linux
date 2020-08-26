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
#include <soc/mscc/ocelot_vcap.h>
#include <linux/dsa/8021q.h>
#include <linux/if_bridge.h>
#include "felix.h"
#include "felix_tag_8021q.h"

static int felix_tag_8021q_rxvlan_add(struct ocelot *ocelot, int port, u16 vid,
				      bool pvid, bool untagged)
{
	const struct vcap_props *vcap = &ocelot->vcap[VCAP_ES0];
	int key_length = vcap->keys[VCAP_ES0_IGR_PORT].length;
	struct ocelot_vcap_filter *outer_tagging_rule;

	/* We don't need to install the rxvlan into the other ports' filtering
	 * tables, because we're just pushing the rxvlan when sending towards
	 * the CPU
	 */
	if (!pvid)
		return 0;

	outer_tagging_rule = kzalloc(sizeof(struct ocelot_vcap_filter),
				     GFP_KERNEL);
	if (!outer_tagging_rule)
		return -ENOMEM;

	outer_tagging_rule->key_type = OCELOT_VCAP_KEY_ANY;
	outer_tagging_rule->prio = 1;
	outer_tagging_rule->id.cookie = port;
	outer_tagging_rule->id.tc_offload = false;
	outer_tagging_rule->block_id = VCAP_ES0;
	outer_tagging_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	outer_tagging_rule->lookup = 0;
	outer_tagging_rule->ingress_port.value = port;
	outer_tagging_rule->ingress_port.mask = GENMASK(key_length - 1, 0);
	outer_tagging_rule->egress_port.value = ocelot->dsa_8021q_cpu;
	outer_tagging_rule->egress_port.mask = GENMASK(key_length - 1, 0);
	outer_tagging_rule->action.push_outer_tag = OCELOT_ES0_TAG;
	outer_tagging_rule->action.tag_a_tpid_sel = OCELOT_TAG_TPID_SEL_8021AD;
	outer_tagging_rule->action.tag_a_vid_sel = 1;
	outer_tagging_rule->action.vid_a_val = vid;

	return ocelot_vcap_filter_add(ocelot, outer_tagging_rule, NULL);
}

static int felix_tag_8021q_txvlan_add(struct ocelot *ocelot, int port, u16 vid,
				      bool pvid, bool untagged)
{
	struct ocelot_vcap_filter *untagging_rule;
	struct ocelot_vcap_filter *redirect_rule;
	int ret;

	/* tag_8021q.c assumes we are implementing this via port VLAN
	 * membership, which we aren't. So we don't need to add any VCAP filter
	 * for the CPU port.
	 */
	if (port == ocelot->dsa_8021q_cpu)
		return 0;

	untagging_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!untagging_rule)
		return -ENOMEM;

	redirect_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!redirect_rule) {
		kfree(untagging_rule);
		return -ENOMEM;
	}

	untagging_rule->key_type = OCELOT_VCAP_KEY_ANY;
	untagging_rule->ingress_port_mask = BIT(ocelot->dsa_8021q_cpu);
	untagging_rule->vlan.vid.value = vid;
	untagging_rule->vlan.vid.mask = VLAN_VID_MASK;
	untagging_rule->prio = 1;
	untagging_rule->id.cookie = port;
	untagging_rule->id.tc_offload = false;
	untagging_rule->block_id = VCAP_IS1;
	untagging_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	untagging_rule->lookup = 0;
	untagging_rule->action.vlan_pop_cnt_ena = true;
	untagging_rule->action.vlan_pop_cnt = 1;
	untagging_rule->action.pag_override_mask = 0xff;
	untagging_rule->action.pag_val = port;

	ret = ocelot_vcap_filter_add(ocelot, untagging_rule, NULL);
	if (ret) {
		kfree(untagging_rule);
		kfree(redirect_rule);
		return ret;
	}

	redirect_rule->key_type = OCELOT_VCAP_KEY_ANY;
	redirect_rule->ingress_port_mask = BIT(ocelot->dsa_8021q_cpu);
	redirect_rule->pag = port;
	redirect_rule->prio = 1;
	redirect_rule->id.cookie = port;
	redirect_rule->id.tc_offload = false;
	redirect_rule->block_id = VCAP_IS2;
	redirect_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	redirect_rule->lookup = 0;
	redirect_rule->action.mask_mode = OCELOT_MASK_MODE_REDIRECT;
	redirect_rule->action.port_mask = BIT(port);

	ret = ocelot_vcap_filter_add(ocelot, redirect_rule, NULL);
	if (ret) {
		ocelot_vcap_filter_del(ocelot, untagging_rule);
		kfree(redirect_rule);
		return ret;
	}

	return 0;
}

static int felix_tag_8021q_vlan_add(struct dsa_switch *ds, int port, u16 vid,
				    u16 flags)
{
	bool untagged = flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = flags & BRIDGE_VLAN_INFO_PVID;
	struct ocelot *ocelot = ds->priv;

	if (vid_is_dsa_8021q_rxvlan(vid))
		return felix_tag_8021q_rxvlan_add(ocelot, port, vid, pvid,
						  untagged);

	if (vid_is_dsa_8021q_txvlan(vid))
		return felix_tag_8021q_txvlan_add(ocelot, port, vid, pvid,
						  untagged);

	return 0;
}

static const struct dsa_8021q_ops felix_tag_8021q_ops = {
	.vlan_add	= felix_tag_8021q_vlan_add,
};

/* Set up a VCAP IS2 rule for delivering PTP frames to the CPU port module.
 * If the NET_DSA_TAG_OCELOT_QUIRK_NO_XTR_IRQ is in place, then also copy those
 * PTP frames to the tag_8021q CPU port.
 */
static int felix_setup_mmio_filtering(struct felix *felix)
{
	struct ocelot_vcap_filter *redirect_rule;
	struct ocelot_vcap_filter *tagging_rule;
	struct ocelot *ocelot = &felix->ocelot;
	unsigned long ingress_port_mask;
	int cpu = ocelot->dsa_8021q_cpu;
	int ret;

	tagging_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!tagging_rule)
		return -ENOMEM;

	redirect_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!redirect_rule) {
		kfree(tagging_rule);
		return -ENOMEM;
	}

	ingress_port_mask = GENMASK(ocelot->num_phys_ports - 1, 0) & ~BIT(cpu);

	tagging_rule->key_type = OCELOT_VCAP_KEY_ETYPE;
	*(u16 *)tagging_rule->key.etype.etype.value = htons(ETH_P_1588);
	*(u16 *)tagging_rule->key.etype.etype.mask = 0xffff;
	tagging_rule->ingress_port_mask = ingress_port_mask;
	tagging_rule->prio = 1;
	tagging_rule->id.cookie = ocelot->num_phys_ports;
	tagging_rule->id.tc_offload = false;
	tagging_rule->block_id = VCAP_IS1;
	tagging_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	tagging_rule->lookup = 0;
	tagging_rule->action.pag_override_mask = 0xff;
	tagging_rule->action.pag_val = ocelot->num_phys_ports;

	ret = ocelot_vcap_filter_add(ocelot, tagging_rule, NULL);
	if (ret) {
		kfree(tagging_rule);
		kfree(redirect_rule);
		return ret;
	}

	redirect_rule->key_type = OCELOT_VCAP_KEY_ANY;
	redirect_rule->ingress_port_mask = ingress_port_mask;
	redirect_rule->pag = ocelot->num_phys_ports;
	redirect_rule->prio = 1;
	redirect_rule->id.cookie = ocelot->num_phys_ports;
	redirect_rule->id.tc_offload = false;
	redirect_rule->block_id = VCAP_IS2;
	redirect_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	redirect_rule->lookup = 0;
	redirect_rule->action.cpu_copy_ena = true;
	if (felix->info->quirk_no_xtr_irq) {
		/* Redirect to the tag_8021q CPU but also copy PTP packets to
		 * the CPU port module
		 */
		redirect_rule->action.mask_mode = OCELOT_MASK_MODE_REDIRECT;
		redirect_rule->action.port_mask = BIT(cpu);
	} else {
		/* Trap PTP packets only to the CPU port module (which is
		 * redirected to the NPI port)
		 */
		redirect_rule->action.mask_mode = OCELOT_MASK_MODE_PERMIT_DENY;
		redirect_rule->action.port_mask = 0;
	}

	ret = ocelot_vcap_filter_add(ocelot, redirect_rule, NULL);
	if (ret) {
		ocelot_vcap_filter_del(ocelot, tagging_rule);
		kfree(redirect_rule);
		return ret;
	}

	return 0;
}

int felix_setup_8021q_tagging(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct dsa_switch *ds = felix->ds;
	int ret;

	felix->dsa_8021q_ctx = devm_kzalloc(ds->dev,
					    sizeof(*felix->dsa_8021q_ctx),
					    GFP_KERNEL);
	if (!felix->dsa_8021q_ctx)
		return -ENOMEM;

	felix->dsa_8021q_ctx->ops = &felix_tag_8021q_ops;
	felix->dsa_8021q_ctx->proto = htons(ETH_P_8021AD);
	felix->dsa_8021q_ctx->ds = ds;

	rtnl_lock();
	ret = dsa_8021q_setup(felix->dsa_8021q_ctx, true);
	rtnl_unlock();
	if (ret)
		return ret;

	return felix_setup_mmio_filtering(felix);
}
