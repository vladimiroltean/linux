/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DSA_LOOP_H
#define DSA_LOOP_H

#include <net/dsa.h>

struct dsa_loop_pdata {
	/* Must be first, such that dsa_register_switch() can access this
	 * without gory pointer manipulations
	 */
	struct dsa_chip_data cd;
	const char *name;
	unsigned int num_ports;
	unsigned int enabled_ports;
	const char *netdev;
};

#endif /* DSA_LOOP_H */
