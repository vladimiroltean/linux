/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2020 NXP Semiconductors
 */
#ifndef _MSCC_FELIX_TAG_8021Q_H
#define _MSCC_FELIX_TAG_8021Q_H

#if IS_ENABLED(CONFIG_NET_DSA_TAG_OCELOT_8021Q)

int felix_setup_8021q_tagging(struct ocelot *ocelot);
bool felix_check_xtr_pkt(struct ocelot *ocelot, unsigned int ptp_type);

#else

static inline int felix_setup_8021q_tagging(struct ocelot *ocelot)
{
	return -EOPNOTSUPP;
}

static inline bool felix_check_xtr_pkt(struct ocelot *ocelot,
				       unsigned int ptp_type)
{
	return false;
}

#endif /* IS_ENABLED(CONFIG_NET_DSA_TAG_OCELOT_8021Q) */

#endif /* _MSCC_FELIX_TAG_8021Q_H */
