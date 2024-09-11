/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_DYNAMIC_CONFIG_H
#define _SJA1105_DYNAMIC_CONFIG_H

#include "sja1105.h"
#include <linux/packing.h>

/* Special index that can be used for sja1105_dynamic_config_read */
#define SJA1105_SEARCH		-1

struct sja1105_dyn_cmd;

struct sja1105_dynamic_table_ops {
	void (*entry_pack)(struct sja1105_private *priv, void *buf,
			   const void *entry_ptr);
	void (*entry_unpack)(struct sja1105_private *priv, const void *buf,
			     void *entry_ptr);
	void (*cmd_pack)(void *buf, const struct sja1105_dyn_cmd *cmd);
	void (*cmd_unpack)(const void *buf, struct sja1105_dyn_cmd *cmd);
	size_t max_entry_count;
	size_t packed_size;
	u32 addr;
	u8 access;
};

struct sja1105_mgmt_entry {
	u8 tsreg;
	u8 takets;
	u64 macaddr;
	u8 destports;
	u8 enfport;
	u8 index;
};

extern const struct sja1105_dynamic_table_ops sja1105et_dyn_ops[BLK_IDX_MAX_DYN];
extern const struct sja1105_dynamic_table_ops sja1105pqrs_dyn_ops[BLK_IDX_MAX_DYN];
extern const struct sja1105_dynamic_table_ops sja1110_dyn_ops[BLK_IDX_MAX_DYN];

#endif
