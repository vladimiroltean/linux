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

#include "ksz_common.h"

#if IS_ENABLED(CONFIG_NET_DSA_MICROCHIP_KSZ9477_PTP)

int ksz9477_ptp_init(struct ksz_device *dev);
void ksz9477_ptp_deinit(struct ksz_device *dev);

#else

static inline int ksz9477_ptp_init(struct ksz_device *dev) { return 0; }
static inline void ksz9477_ptp_deinit(struct ksz_device *dev) {}

#endif

#endif /* DRIVERS_NET_DSA_MICROCHIP_KSZ9477_PTP_H_ */
