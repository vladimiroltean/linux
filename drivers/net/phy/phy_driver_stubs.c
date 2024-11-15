#include <linux/phy.h>

#include "phy_driver_stubs.h"

struct phy_driver_stub {
	u32 phy_id;
	u32 phy_id_mask;
	const char *kconfig_name;
	const char *kconfig_status;
	struct list_head list;
};

static LIST_HEAD(phy_driver_stubs);
static DEFINE_MUTEX(phy_driver_stubs_lock);

int phy_driver_stubs_register(const struct mdio_device_id *device_tbl,
			      const char *kconfig_name,
			      const char *kconfig_status)
{
	size_t i;

	mutex_lock(&phy_driver_stubs_lock);

	for (i = 0; device_tbl[i].phy_id_mask; i++) {
		struct phy_driver_stub *stub;

		stub = kzalloc(sizeof(*stub), GFP_KERNEL);
		if (!stub) {
			while (--i >= 0) {
				stub = list_last_entry(&phy_driver_stubs,
						       struct phy_driver_stub,
						       list);
				list_del(&stub->list);
				kfree(stub);
			}
			mutex_unlock(&phy_driver_stubs_lock);
			return -ENOMEM;
		}

		stub->phy_id = device_tbl[i].phy_id;
		stub->phy_id_mask = device_tbl[i].phy_id_mask;
		stub->kconfig_name = kconfig_name;
		stub->kconfig_status = kconfig_status;
		list_add_tail(&stub->list, &phy_driver_stubs);
	}

	mutex_unlock(&phy_driver_stubs_lock);

	return 0;
}

const char *phy_library_driver_kconfig_name(u32 phy_id)
{
	const char *kconfig_name = NULL;
	struct phy_driver_stub *stub;

	mutex_lock(&phy_driver_stubs_lock);

	list_for_each_entry(stub, &phy_driver_stubs, list) {
		if ((phy_id & stub->phy_id_mask) != stub->phy_id)
			continue;

		/* Safe to return this pointer after unlocking the mutex,
		 * because we never remove the stubs from memory
		 */
		kconfig_name = stub->kconfig_name;
		break;
	}

	mutex_unlock(&phy_driver_stubs_lock);

	return kconfig_name;
}

const char *phy_library_driver_kconfig_status(u32 phy_id)
{
	const char *kconfig_status = NULL;
	struct phy_driver_stub *stub;

	mutex_lock(&phy_driver_stubs_lock);

	list_for_each_entry(stub, &phy_driver_stubs, list) {
		if ((phy_id & stub->phy_id_mask) != stub->phy_id)
			continue;

		/* Safe to return this pointer after unlocking the mutex,
		 * because we never remove the stubs from memory
		 */
		kconfig_status = stub->kconfig_status;
		break;
	}

	mutex_unlock(&phy_driver_stubs_lock);

	return kconfig_status;
}
