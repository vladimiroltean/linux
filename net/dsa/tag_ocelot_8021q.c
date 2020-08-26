// SPDX-License-Identifier: GPL-2.0
/* Copyright 2020 NXP Semiconductors
 */
#include <linux/dsa/8021q.h>
#include <soc/mscc/ocelot.h>
#include <soc/mscc/ocelot_ptp.h>
#include "dsa_priv.h"

static struct sk_buff *ocelot_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	struct dsa_port *dp = dsa_slave_to_port(netdev);
	u16 tx_vid = dsa_8021q_tx_vid(dp->ds, dp->index);
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u8 pcp = netdev_txq_to_tc(netdev, queue_mapping);
	struct sk_buff *clone = DSA_SKB_CB(skb)->clone;

	/* TX timestamping was requested, so inject through MMIO */
	if (clone) {
		struct ocelot *ocelot = dp->ds->priv;
		struct ocelot_port *ocelot_port;
		int port = dp->index;
		u32 rew_op;

		ocelot_port = ocelot->ports[port];
		rew_op = ocelot_port->ptp_cmd;

		/* Retrieve timestamp ID populated inside skb->cb[0] of the
		 * clone by ocelot_port_add_txtstamp_skb
		 */
		if (ocelot_port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP)
			rew_op |= clone->cb[0] << 3;

		ocelot_port_inject_frame(ocelot, dp->index, 0, rew_op, skb);

		return NULL;
	}

	return dsa_8021q_xmit(skb, netdev, ETH_P_8021Q,
			      ((pcp << VLAN_PRIO_SHIFT) | tx_vid));
}

static struct sk_buff *ocelot_rcv(struct sk_buff *skb,
				  struct net_device *netdev,
				  struct packet_type *pt)
{
	int src_port, switch_id, qos_class;
	u16 vid, tci;

	skb_push_rcsum(skb, ETH_HLEN);
	if (skb_vlan_tag_present(skb)) {
		tci = skb_vlan_tag_get(skb);
		__vlan_hwaccel_clear_tag(skb);
	} else {
		__skb_vlan_pop(skb, &tci);
	}
	skb_pull_rcsum(skb, ETH_HLEN);

	vid = tci & VLAN_VID_MASK;
	src_port = dsa_8021q_rx_source_port(vid);
	switch_id = dsa_8021q_rx_switch_id(vid);
	qos_class = (tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;

	skb->dev = dsa_master_find_slave(netdev, switch_id, src_port);
	if (!skb->dev)
		return NULL;

	skb->offload_fwd_mark = 1;
	skb->priority = qos_class;

	return skb;
}

static struct dsa_device_ops ocelot_netdev_ops = {
	.name			= "ocelot",
	.proto			= DSA_TAG_PROTO_OCELOT,
	.xmit			= ocelot_xmit,
	.rcv			= ocelot_rcv,
	.overhead		= VLAN_HLEN,
	.promisc_on_master	= true,
};

MODULE_LICENSE("GPL v2");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_OCELOT);

module_dsa_tag_driver(ocelot_netdev_ops);
