// SPDX-License-Identifier: GPL-2.0+
/*
 * net/dsa/tag_ksz.c - Microchip KSZ Switch tag format handling
 * Copyright (c) 2017 Microchip Technology
 */

#include <asm/unaligned.h>
#include <linux/dsa/ksz_common.h>
#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/ptp_classify.h>
#include <linux/slab.h>
#include <net/dsa.h>
#include <net/checksum.h>
#include "dsa_priv.h"

/* Typically only one byte is used for tail tag. */
#define KSZ_EGRESS_TAG_LEN		1
#define KSZ_INGRESS_TAG_LEN		1

static struct sk_buff *ksz_common_rcv(struct sk_buff *skb,
				      struct net_device *dev,
				      unsigned int port, unsigned int len)
{
	skb->dev = dsa_master_find_slave(dev, 0, port);
	if (!skb->dev)
		return NULL;

	pskb_trim_rcsum(skb, skb->len - len);

	skb->offload_fwd_mark = true;

	return skb;
}

/*
 * For Ingress (Host -> KSZ8795), 1 byte is added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|tag(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * tag : each bit represents port (eg, 0x01=port1, 0x02=port2, 0x10=port5)
 *
 * For Egress (KSZ8795 -> Host), 1 byte is added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|tag0(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * tag0 : zero-based value represents port
 *	  (eg, 0x00=port1, 0x02=port3, 0x06=port7)
 */

#define KSZ8795_TAIL_TAG_OVERRIDE	BIT(6)
#define KSZ8795_TAIL_TAG_LOOKUP		BIT(7)

static struct sk_buff *ksz8795_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	u8 *tag;
	u8 *addr;

	/* Tag encoding */
	tag = skb_put(skb, KSZ_INGRESS_TAG_LEN);
	addr = skb_mac_header(skb);

	*tag = 1 << dp->index;
	if (is_link_local_ether_addr(addr))
		*tag |= KSZ8795_TAIL_TAG_OVERRIDE;

	return skb;
}

static struct sk_buff *ksz8795_rcv(struct sk_buff *skb, struct net_device *dev,
				  struct packet_type *pt)
{
	u8 *tag = skb_tail_pointer(skb) - KSZ_EGRESS_TAG_LEN;

	return ksz_common_rcv(skb, dev, tag[0] & 7, KSZ_EGRESS_TAG_LEN);
}

static const struct dsa_device_ops ksz8795_netdev_ops = {
	.name	= "ksz8795",
	.proto	= DSA_TAG_PROTO_KSZ8795,
	.xmit	= ksz8795_xmit,
	.rcv	= ksz8795_rcv,
	.overhead = KSZ_INGRESS_TAG_LEN,
	.tail_tag = true,
};

DSA_TAG_DRIVER(ksz8795_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_KSZ8795);

/*
 * For Ingress (Host -> KSZ9477), 2 bytes are added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|ts(4bytes)|tag0(1byte)|tag1(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * ts   : time stamp (only present if PTP is enabled on the hardware).
 * tag0 : Prioritization (not used now)
 * tag1 : each bit represents port (eg, 0x01=port1, 0x02=port2, 0x10=port5)
 *
 * For Egress (KSZ9477 -> Host), 1/4 bytes are added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|ts(4bytes)|tag0(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * ts   : time stamp (only present of bit 7 in tag0 is set
 * tag0 : zero-based value represents port
 *	  (eg, 0x00=port1, 0x02=port3, 0x06=port7)
 */

#define KSZ9477_INGRESS_TAG_LEN		2
#define KSZ9477_PTP_TAG_LEN		4
#define KSZ9477_PTP_TAG_INDICATION	BIT(7)

#define KSZ9477_TAIL_TAG_OVERRIDE	BIT(9)
#define KSZ9477_TAIL_TAG_LOOKUP		BIT(10)

/* Time stamp tag is only inserted if PTP is enabled in hardware. */
static void ksz9477_xmit_timestamp(struct sk_buff *skb)
{
	struct sk_buff *clone = DSA_SKB_CB(skb)->clone;
	struct ptp_header *ptp_hdr;
	unsigned int ptp_type;
	u32 tstamp_raw = 0;
	u8 ptp_msg_type;
	s64 correction;

	if (!clone)
		goto out_put_tag;

	/* Use cached PTP type from ksz9477_ptp_port_txtstamp().  */
	ptp_type = KSZ9477_SKB_CB(clone)->ptp_type;
	if (ptp_type == PTP_CLASS_NONE)
		goto out_put_tag;

	ptp_hdr = ptp_parse_header(skb, ptp_type);
	if (!ptp_hdr)
		goto out_put_tag;

	ptp_msg_type = KSZ9477_SKB_CB(clone)->ptp_msg_type;
	if (ptp_msg_type != PTP_MSGTYPE_PDELAY_RESP)
		goto out_put_tag;

	correction = (s64)get_unaligned_be64(&ptp_hdr->correction);

	/* For PDelay_Resp messages we will likely have a negative value in the
	 * correction field (see ksz9477_rcv()). The switch hardware cannot
	 * correctly update such values (produces an off by one error in the UDP
	 * checksum), so it must be moved to the time stamp field in the tail
	 * tag.
	 */
	if (correction < 0) {
		struct timespec64 ts;

		/* Move ingress time stamp from PTP header's correction field to
		 * tail tag. Format of the correction filed is 48 bit ns + 16
		 * bit fractional ns.
		 */
		ts = ns_to_timespec64(-correction >> 16);
		tstamp_raw = ((ts.tv_sec & 3) << 30) | ts.tv_nsec;

		/* Set correction field to 0 and update UDP checksum.  */
		ptp_header_update_correction(skb, ptp_type, ptp_hdr, 0);
	}

	/* For PDelay_Resp messages, the clone is not required in
	 * skb_complete_tx_timestamp() and should be freed here.
	 */
	kfree_skb(clone);
	DSA_SKB_CB(skb)->clone = NULL;

out_put_tag:
	put_unaligned_be32(tstamp_raw, skb_put(skb, KSZ9477_PTP_TAG_LEN));
}

/* Defer transmit if waiting for egress time stamp is required.  */
static struct sk_buff *ksz9477_defer_xmit(struct dsa_port *dp,
					  struct sk_buff *skb)
{
	struct ksz_port_ptp_shared *ptp_shared = dp->priv;
	struct sk_buff *clone = DSA_SKB_CB(skb)->clone;
	u8 ptp_msg_type;

	if (!clone)
		return skb;  /* no deferred xmit for this packet */

	/* Use cached PTP msg type from ksz9477_ptp_port_txtstamp().  */
	ptp_msg_type = KSZ9477_SKB_CB(clone)->ptp_msg_type;
	if (ptp_msg_type != PTP_MSGTYPE_DELAY_REQ &&
	    ptp_msg_type != PTP_MSGTYPE_PDELAY_REQ)
		goto out_free_clone;  /* only PDelay_Req is deferred */

	/* Increase refcount so the kfree_skb in dsa_slave_xmit
	 * won't really free the packet.
	 */
	skb_queue_tail(&ptp_shared->xmit_queue, skb_get(skb));
	kthread_queue_work(ptp_shared->xmit_worker, &ptp_shared->xmit_work);

	return NULL;

out_free_clone:
	kfree_skb(clone);
	DSA_SKB_CB(skb)->clone = NULL;
	return skb;
}

ktime_t ksz9477_tstamp_reconstruct(struct ksz_device_ptp_shared *ksz,
				   ktime_t tstamp)
{
	struct timespec64 ts = ktime_to_timespec64(tstamp);
	struct timespec64 ptp_clock_time;
	struct timespec64 diff;

	spin_lock_bh(&ksz->ptp_clock_lock);
	ptp_clock_time = ksz->ptp_clock_time;
	spin_unlock_bh(&ksz->ptp_clock_lock);

	/* calculate full time from partial time stamp */
	ts.tv_sec = (ptp_clock_time.tv_sec & ~3) | ts.tv_sec;

	/* find nearest possible point in time */
	diff = timespec64_sub(ts, ptp_clock_time);
	if (diff.tv_sec > 2)
		ts.tv_sec -= 4;
	else if (diff.tv_sec < -2)
		ts.tv_sec += 4;

	return timespec64_to_ktime(ts);
}
EXPORT_SYMBOL(ksz9477_tstamp_reconstruct);

static void ksz9477_rcv_timestamp(struct sk_buff *skb, u8 *tag,
				  struct net_device *dev, unsigned int port)
{
	struct skb_shared_hwtstamps *hwtstamps = skb_hwtstamps(skb);
	struct dsa_switch *ds = dev->dsa_ptr->ds;
	struct ksz_port_ptp_shared *port_ptp_shared;
	u8 *tstamp_raw = tag - KSZ9477_PTP_TAG_LEN;
	struct ptp_header *ptp_hdr;
	unsigned int ptp_type;
	u8 ptp_msg_type;
	ktime_t tstamp;
	s64 correction;

	port_ptp_shared = dsa_to_port(ds, port)->priv;
	if (!port_ptp_shared)
		return;

	/* convert time stamp and write to skb */
	tstamp = ksz9477_decode_tstamp(get_unaligned_be32(tstamp_raw));
	memset(hwtstamps, 0, sizeof(*hwtstamps));
	hwtstamps->hwtstamp = ksz9477_tstamp_reconstruct(port_ptp_shared->dev,
							 tstamp);

	/* For PDelay_Req messages, user space (ptp4l) expects that the hardware
	 * subtracts the ingress time stamp from the correction field.  The
	 * separate hw time stamp from the sk_buff struct will not be used in
	 * this case.
	 */

	if (skb_headroom(skb) < ETH_HLEN)
		return;

	__skb_push(skb, ETH_HLEN);
	ptp_type = ptp_classify_raw(skb);
	__skb_pull(skb, ETH_HLEN);

	if (ptp_type == PTP_CLASS_NONE)
		return;

	ptp_hdr = ptp_parse_header(skb, ptp_type);
	if (!ptp_hdr)
		return;

	ptp_msg_type = ptp_get_msgtype(ptp_hdr, ptp_type);
	if (ptp_msg_type != PTP_MSGTYPE_PDELAY_REQ)
		return;

	/* Only subtract the partial time stamp from the correction field.  When
	 * the hardware adds the egress time stamp to the correction field of
	 * the PDelay_Resp message on tx, also only the partial time stamp will
	 * be added.
	 */
	correction = (s64)get_unaligned_be64(&ptp_hdr->correction);
	correction -= ktime_to_ns(tstamp) << 16;

	ptp_header_update_correction(skb, ptp_type, ptp_hdr, correction);
}

static struct sk_buff *ksz9477_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct ksz_port_ptp_shared *port_ptp_shared = dp->priv;
	struct ksz_device_ptp_shared *ptp_shared = port_ptp_shared->dev;
	__be16 *tag;
	u8 *addr;
	u16 val;

	/* Tag encoding */
	if (test_bit(KSZ9477_HWTS_EN, &ptp_shared->state))
		ksz9477_xmit_timestamp(skb);
	tag = skb_put(skb, KSZ9477_INGRESS_TAG_LEN);
	addr = skb_mac_header(skb);

	val = BIT(dp->index);

	if (is_link_local_ether_addr(addr))
		val |= KSZ9477_TAIL_TAG_OVERRIDE;

	*tag = cpu_to_be16(val);

	return ksz9477_defer_xmit(dp, skb);
}

static struct sk_buff *ksz9477_rcv(struct sk_buff *skb, struct net_device *dev,
				   struct packet_type *pt)
{
	/* Tag decoding */
	u8 *tag = skb_tail_pointer(skb) - KSZ_EGRESS_TAG_LEN;
	unsigned int port = tag[0] & 7;
	unsigned int len = KSZ_EGRESS_TAG_LEN;

	/* Extra 4-bytes PTP timestamp */
	if (tag[0] & KSZ9477_PTP_TAG_INDICATION) {
		ksz9477_rcv_timestamp(skb, tag, dev, port);
		len += KSZ9477_PTP_TAG_LEN;
	}

	return ksz_common_rcv(skb, dev, port, len);
}

static const struct dsa_device_ops ksz9477_netdev_ops = {
	.name	= "ksz9477",
	.proto	= DSA_TAG_PROTO_KSZ9477,
	.xmit	= ksz9477_xmit,
	.rcv	= ksz9477_rcv,
	.overhead = KSZ9477_INGRESS_TAG_LEN + KSZ9477_PTP_TAG_LEN,
	.tail_tag = true,
};

DSA_TAG_DRIVER(ksz9477_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_KSZ9477);

#define KSZ9893_TAIL_TAG_OVERRIDE	BIT(5)
#define KSZ9893_TAIL_TAG_LOOKUP		BIT(6)

static struct sk_buff *ksz9893_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct ksz_port_ptp_shared *port_ptp_shared = dp->priv;
	struct ksz_device_ptp_shared *ptp_shared = port_ptp_shared->dev;
	u8 *addr;
	u8 *tag;

	/* Tag encoding */
	if (test_bit(KSZ9477_HWTS_EN, &ptp_shared->state))
		ksz9477_xmit_timestamp(skb);
	tag = skb_put(skb, KSZ_INGRESS_TAG_LEN);
	addr = skb_mac_header(skb);

	*tag = BIT(dp->index);

	if (is_link_local_ether_addr(addr))
		*tag |= KSZ9893_TAIL_TAG_OVERRIDE;

	return ksz9477_defer_xmit(dp, skb);
}

static const struct dsa_device_ops ksz9893_netdev_ops = {
	.name	= "ksz9893",
	.proto	= DSA_TAG_PROTO_KSZ9893,
	.xmit	= ksz9893_xmit,
	.rcv	= ksz9477_rcv,
	.overhead = KSZ_INGRESS_TAG_LEN + KSZ9477_PTP_TAG_LEN,
	.tail_tag = true,
};

DSA_TAG_DRIVER(ksz9893_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_KSZ9893);

static struct dsa_tag_driver *dsa_tag_driver_array[] = {
	&DSA_TAG_DRIVER_NAME(ksz8795_netdev_ops),
	&DSA_TAG_DRIVER_NAME(ksz9477_netdev_ops),
	&DSA_TAG_DRIVER_NAME(ksz9893_netdev_ops),
};

module_dsa_tag_drivers(dsa_tag_driver_array);

MODULE_LICENSE("GPL");
