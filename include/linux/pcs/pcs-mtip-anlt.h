/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2023-2026 NXP */

#ifndef __LINUX_PCS_MTIP_ANLT_H
#define __LINUX_PCS_MTIP_ANLT_H

#include <linux/types.h>

struct device;
struct mtip_anlt;
struct phylink_anlt;

/**
 * struct mtip_anlt_access - access to the registers of the AN block
 * @read: read an Autoneg MMD (MMD 7) register. Returns the value or a
 *	negative error code.
 * @write: write an Autoneg MMD register. Returns 0 or a negative error code.
 * @modify: read-modify-write of an Autoneg MMD register, which must skip the
 *	write if the value does not change. Returns 0 or a negative error code.
 *
 * The registers are numbered as in IEEE 802.3 Clause 45 (for example 0 is
 * the AN control register and 1 the AN status register), so that the PCS
 * driver, which knows how the block is reached, does not have to translate
 * them. The methods are called with the lock of the block held, from process
 * context, and may sleep.
 */
struct mtip_anlt_access {
	int (*read)(void *ctx, int reg);
	int (*write)(void *ctx, int reg, u16 val);
	int (*modify)(void *ctx, int reg, u16 mask, u16 set);
};

struct mtip_anlt *mtip_anlt_create(struct device *dev,
				   const struct mtip_anlt_access *access,
				   void *ctx);
void mtip_anlt_destroy(struct mtip_anlt *mtip);
struct phylink_anlt *mtip_anlt_to_phylink(struct mtip_anlt *mtip);

#endif /* __LINUX_PCS_MTIP_ANLT_H */
