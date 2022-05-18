// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 NXP
 *
 * Interrupt controller driver for NXP QIXIS FPGA
 */

#include <dt-bindings/mfd/ls1028a-rdb-qixis.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define INTC_IE 0x00
#define INTC_IP 0x01
#define QIXIS_ID_LS1028A_RDB	0x47

#define QIXIS_MINOR_DATE	0x8

#define LS1028ARDB_REGMAP_IRQ_REG(_irq)					\
	[(_irq)] = {							\
		.reg_offset = 0,					\
		.mask = BIT(_irq),					\
		.type = {						\
			.types_supported = IRQ_TYPE_EDGE_FALLING |	\
					   IRQ_TYPE_EDGE_RISING |	\
					   IRQ_TYPE_LEVEL_HIGH |	\
					   IRQ_TYPE_LEVEL_LOW,		\
		},							\
	}

static const struct regmap_irq ls1028a_rdb_qixis_irqs[] = {
	LS1028ARDB_REGMAP_IRQ_REG(LS1028ARDB_IRQ_ETH_B),
	LS1028ARDB_REGMAP_IRQ_REG(LS1028ARDB_IRQ_QSGMII_B),
	LS1028ARDB_REGMAP_IRQ_REG(LS1028ARDB_IRQ_UBUS1),
	LS1028ARDB_REGMAP_IRQ_REG(LS1028ARDB_IRQ_UBUS2),
	LS1028ARDB_REGMAP_IRQ_REG(LS1028ARDB_IRQ_RTC_B),
};

struct qixis_intc_data {
	const struct regmap_irq_chip *chip;
	long min_year;
	int min_month;
	int min_day;
	int id;
};

static const struct regmap_irq_chip ls1028a_rdb_irq_chip = {
	.name = "ls1028a-rdb-qixis-intc",
	.irqs = ls1028a_rdb_qixis_irqs,
	.num_irqs = ARRAY_SIZE(ls1028a_rdb_qixis_irqs),
	.num_regs = 1,
	.status_base = INTC_IP,
	.mask_base = INTC_IE,
	.mask_invert = true,
	.ack_base = INTC_IP,
};

static const struct qixis_intc_data qixis_intc_data[] = {
	{
		.id = QIXIS_ID_LS1028A_RDB,
		.min_year = 2020,
		.min_month = 2,
		.min_day = 28,
		.chip = &ls1028a_rdb_irq_chip,
	},
};

struct qixis_intc {
	struct regmap *regmap;
	const struct qixis_intc_data *intc_data;
	struct regmap_irq_chip_data *irq_data;
};

static int qixis_read_build_date(struct device *dev, struct regmap *regmap,
				 struct tm *tm)
{
	time64_t tv_sec = 0;
	unsigned int val;
	int i, ret;

	/* The build timestamp is in 32-bit big endian format */
	for (i = 0; i < 4; i++) {
		ret = regmap_write(regmap, QIXIS_MINOR, QIXIS_MINOR_DATE + i);
		if (ret) {
			dev_err(dev, "Failed to write QIXIS_MINOR: %pe\n",
				ERR_PTR(ret));
			return ret;
		}

		ret = regmap_read(regmap, QIXIS_MINOR, &val);
		if (ret) {
			dev_err(dev, "Failed to read QIXIS_MINOR: %pe\n",
				ERR_PTR(ret));
			return ret;
		}

		tv_sec = (tv_sec << 8) + val;
	}

	time64_to_tm(tv_sec, 0, tm);

	return 0;
}

static int
qixis_intc_validate_requirements(struct device *dev,
				 struct regmap *regmap,
				 const struct qixis_intc_data *intc_data)
{
	struct tm tm;
	int ret;

	ret = qixis_read_build_date(dev, regmap, &tm);
	if (ret)
		return ret;

	dev_info(dev, "QIXIS build date: %ld/%02d/%02d, %02d:%02d\n",
		 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		 tm.tm_hour, tm.tm_min);

	if (tm.tm_year + 1900 < intc_data->min_year ||
	    tm.tm_mon + 1 < intc_data->min_month ||
	    tm.tm_mday < intc_data->min_day) {
		dev_err(dev,
			"Driver requires build date later than %ld/%02d/%02d\n",
			intc_data->min_year, intc_data->min_month,
			intc_data->min_day);
		return -EINVAL;
	}

	return 0;
}

static const struct qixis_intc_data *qixis_intc_detect(struct device *dev,
						       struct regmap *regmap)
{
	int i, id, ret;

	ret = regmap_read(regmap, QIXIS_ID, &id);
	if (ret) {
		dev_err(dev, "Failed to read QIXIS_ID: %pe\n", ERR_PTR(ret));
		return ERR_PTR(ret);
	}

	for (i = 0; i < ARRAY_SIZE(qixis_intc_data); i++)
		if (qixis_intc_data[i].id == id)
			return &qixis_intc_data[i];

	return ERR_PTR(-ENODEV);
}

static int qixis_intc_probe(struct platform_device *pdev)
{
	const struct qixis_intc_data *intc_data;
	struct device *dev = &pdev->dev;
	struct qixis_intc *irqchip;
	struct regmap *regmap;
	int irq, ret;

	if (!dev->parent)
		return -ENODEV;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	intc_data = qixis_intc_detect(dev, regmap);
	if (IS_ERR(intc_data))
		return PTR_ERR(intc_data);

	ret = qixis_intc_validate_requirements(dev, regmap, intc_data);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	irqchip = devm_kzalloc(dev, sizeof(*irqchip), GFP_KERNEL);
	if (!irqchip)
		return -ENOMEM;

	irqchip->regmap = regmap;
	irqchip->intc_data = intc_data;

	return devm_regmap_add_irq_chip_fwnode(dev, dev_fwnode(dev),
					       irqchip->regmap, irq,
					       IRQF_SHARED | IRQF_ONESHOT, 0,
					       irqchip->intc_data->chip,
					       &irqchip->irq_data);
}

static const struct of_device_id qixis_intc_of_match[] = {
	{ .compatible = "fsl,ls1028a-rdb-qixis-intc" },
	{}
};
MODULE_DEVICE_TABLE(of, qixis_intc_of_match);

static struct platform_driver qixis_intc_driver = {
	.probe = qixis_intc_probe,
	.driver = {
		.name = "qixis-intc",
		.of_match_table = qixis_intc_of_match,
	}
};
module_platform_driver(qixis_intc_driver);

MODULE_DESCRIPTION("NXP QIXIS Interrupt Controller Driver");
MODULE_AUTHOR("Vladimir Oltean <vladimir.oltean@nxp.com>");
MODULE_LICENSE("GPL");
