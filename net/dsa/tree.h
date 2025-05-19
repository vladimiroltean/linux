/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_TREE_H
#define __DSA_TREE_H

struct dsa_device_ops;
struct dsa_lag;
struct dsa_switch;
struct dsa_switch_tree;
struct net_device;

extern struct list_head dsa_tree_list;

void dsa_lag_map(struct dsa_switch_tree *dst, struct dsa_lag *lag);
void dsa_lag_unmap(struct dsa_switch_tree *dst, struct dsa_lag *lag);
struct dsa_lag *dsa_tree_lag_find(struct dsa_switch_tree *dst,
				  const struct net_device *lag_dev);
struct net_device *dsa_tree_find_first_conduit(struct dsa_switch_tree *dst);
int dsa_tree_change_tag_proto(struct dsa_switch_tree *dst,
			      const struct dsa_device_ops *tag_ops,
			      const struct dsa_device_ops *old_tag_ops);
void dsa_tree_conduit_admin_state_change(struct dsa_switch_tree *dst,
					 struct net_device *conduit,
					 bool up);
void dsa_tree_conduit_oper_state_change(struct dsa_switch_tree *dst,
					struct net_device *conduit,
					bool up);
unsigned int dsa_bridge_num_get(const struct net_device *bridge_dev, int max);
void dsa_bridge_num_put(const struct net_device *bridge_dev,
			unsigned int bridge_num);
struct dsa_bridge *dsa_tree_bridge_find(struct dsa_switch_tree *dst,
					const struct net_device *br);

int dsa_tree_setup(struct dsa_switch_tree *dst);
void dsa_tree_teardown(struct dsa_switch_tree *dst);

int dsa_switch_get_tree(struct dsa_switch *ds);
void dsa_switch_put_tree(struct dsa_switch *ds);

int dsa_tree_class_register(void);
void dsa_tree_class_unregister(void);

#endif
