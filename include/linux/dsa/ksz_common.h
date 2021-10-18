/* SPDX-License-Identifier: GPL-2.0 */
/* Routines shared between drivers/net/dsa/microchip/ksz9477_ptp.h and
 * net/dsa/tag_ksz.c
 *
 * Copyright (C) 2020 ARRI Lighting
 */

#ifndef _NET_DSA_KSZ_COMMON_H_
#define _NET_DSA_KSZ_COMMON_H_

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_classify.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/time64.h>
#include <net/dsa.h>

/* All time stamps from the KSZ consist of 2 bits for seconds and 30 bits for
 * nanoseconds. This is NOT the same as 32 bits for nanoseconds.
 */
#define KSZ_TSTAMP_SEC_MASK  GENMASK(31, 30)
#define KSZ_TSTAMP_NSEC_MASK GENMASK(29, 0)

#define KSZ9477_HWTS_EN  0

struct ksz_device_ptp_shared {
	/* protects ptp_clock_time (user space (various syscalls)
	 * vs. softirq in ksz9477_rcv_timestamp()).
	 */
	spinlock_t ptp_clock_lock;
	/* approximated current time, read once per second from hardware */
	struct timespec64 ptp_clock_time;
	unsigned long state;
	void (*xmit_work_fn)(struct kthread_work *work);
	struct kthread_worker *xmit_worker;
};

struct ksz_deferred_xmit_work {
	struct dsa_port *dp;
	struct sk_buff *skb;
	struct kthread_work work;
};

/* net/dsa/tag_ksz.c */
static inline ktime_t ksz9477_decode_tstamp(u32 tstamp)
{
	u64 ns = FIELD_GET(KSZ_TSTAMP_SEC_MASK, tstamp) * NSEC_PER_SEC +
		 FIELD_GET(KSZ_TSTAMP_NSEC_MASK, tstamp);

	return ns_to_ktime(ns);
}

ktime_t ksz9477_tstamp_reconstruct(struct ksz_device_ptp_shared *ksz,
				   ktime_t tstamp);

struct ksz9477_skb_cb {
	unsigned int ptp_type;
	/* Do not cache pointer to PTP header between ksz9477_ptp_port_txtstamp
	 * and ksz9xxx_xmit() (will become invalid during dsa_realloc_skb()).
	 */
	u8 ptp_msg_type;
};

#define KSZ9477_SKB_CB(skb) \
	((struct ksz9477_skb_cb *)DSA_SKB_CB_PRIV(skb))

#endif /* _NET_DSA_KSZ_COMMON_H_ */
