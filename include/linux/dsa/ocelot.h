/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020 Vladimir Oltean <vladimir.oltean@nxp.com>
 */

#ifndef _NET_DSA_OCELOT_H
#define _NET_DSA_OCELOT_H

#include <net/dsa.h>

struct ocelot_skb_cb {
	u8 tstamp_id;
};

#define OCELOT_SKB_CB(skb) \
	((struct ocelot_skb_cb *)DSA_SKB_CB_PRIV(skb))

#endif /* _NET_DSA_OCELOT_H */
