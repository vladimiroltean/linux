#include "bcm84881_stubs.h"
#include "phy_driver_stubs.h"

/* FIXME: module auto-loading for Clause 45 PHYs seems non-functional */
struct mdio_device_id __maybe_unused bcm84881_tbl[] = {
	{ 0xae025150, 0xfffffff0 },
	{ },
};

PHY_DRIVER_STUBS(bcm84881_tbl, CONFIG_BCM84881_PHY);
