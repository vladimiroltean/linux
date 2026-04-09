/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DSA_LOOP_BUS_H
#define DSA_LOOP_BUS_H

#include <linux/device.h>
#include <linux/netdevice.h>
#include <net/dsa.h>

#include "dsa_loop.h"

struct dsa_loop_device {
	struct device dev;
	u32 portid;
	struct dsa_loop_pdata pdata;
	struct net_device *conduits[DSA_MAX_PORTS];
	netdevice_tracker trackers[DSA_MAX_PORTS];
};

#define to_dsa_loop_device(d) container_of(d, struct dsa_loop_device, dev)

struct dsa_loop_driver {
	struct device_driver driver;
	int (*probe)(struct dsa_loop_device *ddev);
	void (*remove)(struct dsa_loop_device *ddev);
	void (*shutdown)(struct dsa_loop_device *ddev);
};

#define to_dsa_loop_driver(d) container_of(d, struct dsa_loop_driver, driver)

struct dsa_loop_bus_priv {
	struct dsa_loop_device *dld;
};

int dsa_loop_driver_register(struct dsa_loop_driver *drv);
void dsa_loop_driver_unregister(struct dsa_loop_driver *drv);

int dsa_loop_bus_port_enable(struct dsa_loop_device *dld, int port);
int dsa_loop_bus_port_disable(struct dsa_loop_device *dld, int port);

#endif /* DSA_LOOP_BUS_H */
