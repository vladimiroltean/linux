/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM	enetc

#if !defined(_NET_ENETC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _NET_ENETC_TRACE_H

#include <linux/bits.h>
#include <linux/skbuff.h>
#include <linux/tracepoint.h>

#define ENETC_TXBD_TXSTART_MASK GENMASK(24, 0)

TRACE_EVENT(enetc_tx_start,

	TP_PROTO(const struct sk_buff *skb, u64 now),

	TP_ARGS(skb, now),

	TP_STRUCT__entry(
		__string(	name,			skb->dev->name	)
		__field(	const void *,		skbaddr		)
		__field(	u64,			skb_mstamp_ns	)
		__field(	u32,			tx_start	)
		__field(	u64,			now		)
	),

	TP_fast_assign(
		__assign_str(name, skb->dev->name);
		__entry->skbaddr = skb;
		__entry->skb_mstamp_ns = skb->skb_mstamp_ns;
		__entry->tx_start = skb->skb_mstamp_ns >> 5 & ENETC_TXBD_TXSTART_MASK;
		__entry->now = now;
	),

	TP_printk("dev=%s skbaddr=%p skb_mstamp_ns=%llu tx_start=%u now=%llu",
		  __get_str(name), __entry->skbaddr, __entry->skb_mstamp_ns,
		  __entry->tx_start, __entry->now)
);

#endif /* _NET_ENETC_TRACE_H */

#include <trace/define_trace.h>
