/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_H
#define __DSA_H

#include <linux/list.h>
#include <linux/types.h>

struct dsa_port;
struct dsa_switch;
struct dsa_switch_tree;
struct dsa_db;
struct work_struct;

bool dsa_db_equal(const struct dsa_db *a, const struct dsa_db *b);
bool dsa_schedule_work(struct work_struct *work);

int dsa_port_setup(struct dsa_port *dp);
int dsa_port_setup_as_unused(struct dsa_port *dp);
void dsa_port_teardown(struct dsa_port *dp);
int dsa_port_resolve_tag_protocol(struct dsa_port *dp,
				  struct dsa_switch_tree *dst);

int dsa_switch_setup(struct dsa_switch *ds);
void dsa_switch_teardown(struct dsa_switch *ds);

#endif
