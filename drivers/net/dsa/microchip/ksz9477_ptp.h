/* SPDX-License-Identifier: GPL-2.0
 *
 * Microchip KSZ9477 switch driver PTP routines
 *
 * Author: Christian Eggers <ceggers@arri.de>
 *
 * Copyright (c) 2020 ARRI Lighting
 */

#ifndef DRIVERS_NET_DSA_MICROCHIP_KSZ9477_PTP_H_
#define DRIVERS_NET_DSA_MICROCHIP_KSZ9477_PTP_H_

#include <linux/types.h>

#include "ksz_common.h"

#if IS_ENABLED(CONFIG_NET_DSA_MICROCHIP_KSZ9477_PTP)

int ksz9477_ptp_init(struct ksz_device *dev);
void ksz9477_ptp_deinit(struct ksz_device *dev);

int ksz9477_ptp_get_ts_info(struct dsa_switch *ds, int port,
			    struct ethtool_ts_info *ts);
int ksz9477_ptp_port_hwtstamp_get(struct dsa_switch *ds, int port,
				  struct ifreq *ifr);
int ksz9477_ptp_port_hwtstamp_set(struct dsa_switch *ds, int port,
				  struct ifreq *ifr);

#else

static inline int ksz9477_ptp_init(struct ksz_device *dev) { return 0; }
static inline void ksz9477_ptp_deinit(struct ksz_device *dev) {}

static inline int ksz9477_ptp_get_ts_info(struct dsa_switch *ds, int port,
					  struct ethtool_ts_info *ts)
{ return -EOPNOTSUPP; }

static inline int ksz9477_ptp_port_hwtstamp_get(struct dsa_switch *ds, int port,
						struct ifreq *ifr)
{ return -EOPNOTSUPP; }

static inline int ksz9477_ptp_port_hwtstamp_set(struct dsa_switch *ds, int port,
						struct ifreq *ifr)
{ return -EOPNOTSUPP; }

#endif

#endif /* DRIVERS_NET_DSA_MICROCHIP_KSZ9477_PTP_H_ */
