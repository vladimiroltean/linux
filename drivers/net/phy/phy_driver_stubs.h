#include <linux/mod_devicetable.h>
#include <linux/module.h>

int phy_driver_stubs_register(const struct mdio_device_id *device_tbl,
			      const char *kconfig_name,
			      const char *kconfig_status);

#define PHY_DRIVER_STUBS(device_id_tbl, kconfig_name)			\
static int __init phy_driver_register_stubs(void)			\
{									\
	return phy_driver_stubs_register(device_id_tbl,			\
					 __stringify(kconfig_name),	\
					 IS_MODULE(kconfig_name) ? "compiled as module" : \
					 IS_BUILTIN(kconfig_name) ? "built into the kernel" : \
					 "disabled");			\
}									\
module_init(phy_driver_register_stubs)
