// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 NXP
 *
 * NXP SJA1110 System Level Interrupt Request (SLIR)
 */
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define SLIR_NUM_IRQ		32

#define SLIR_STAT		0x0
#define SLIR_INTENSET		0x4
#define SLIR_INTENCLR		0x8
#define SLIR_INTSTAT		0xc

struct slir {
	struct device *dev;
	struct regmap *regmap;
	struct irq_domain *domain;
	struct mutex domain_lock;
	u32 intenset;
	u32 intenclr;
	int irq;
	char parent_irq_name[64];
};

static irqreturn_t slir_irq(int irq, void *data)
{
	struct slir *slir = data;
	bool handled = false;
	unsigned int virq;
	u32 intstat;
	int i, err;

	err = regmap_read(slir->regmap, SLIR_INTSTAT, &intstat);
	if (err) {
		dev_err_ratelimited(slir->dev,
				    "Failed to read INTSTAT register: %pe\n",
				    ERR_PTR(err));
		return IRQ_NONE;
	}

	for (i = 0; i < SLIR_NUM_IRQ; i++) {
		if (!(intstat & BIT(i)))
			continue;

		virq = irq_find_mapping(slir->domain, i);
		handle_nested_irq(virq);

		handled = true;
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static void slir_mask_irq(struct irq_data *d)
{
	struct slir *slir = irq_data_get_irq_chip_data(d);
	unsigned int irq = BIT(d->hwirq);

	slir->intenclr |= irq;
}

static void slir_unmask_irq(struct irq_data *d)
{
	struct slir *slir = irq_data_get_irq_chip_data(d);
	unsigned int irq = BIT(d->hwirq);

	slir->intenset |= irq;
}

static void slir_irq_bus_lock(struct irq_data *d)
{
	struct slir *slir = irq_data_get_irq_chip_data(d);

	mutex_lock(&slir->domain_lock);

	slir->intenset = 0;
	slir->intenclr = 0;
}

static void slir_irq_bus_sync_unlock(struct irq_data *d)
{
	struct slir *slir = irq_data_get_irq_chip_data(d);
	int err;

	if (slir->intenclr) {
		err = regmap_write(slir->regmap, SLIR_INTENCLR, slir->intenclr);
		if (err) {
			dev_err(slir->dev, "failed to mask IRQs 0x%x: %pe\n",
				slir->intenclr, ERR_PTR(err));
		}
	}

	if (slir->intenset) {
		err = regmap_write(slir->regmap, SLIR_INTENSET, slir->intenset);
		if (err) {
			dev_err(slir->dev, "failed to unmask IRQs 0x%x: %pe\n",
				slir->intenset, ERR_PTR(err));
		}
	}

	mutex_unlock(&slir->domain_lock);
}

static struct irq_chip slir_chip = {
	.name = "slir",
	.irq_mask = slir_mask_irq,
	.irq_unmask = slir_unmask_irq,
	.irq_bus_lock = slir_irq_bus_lock,
	.irq_bus_sync_unlock = slir_irq_bus_sync_unlock,
};

static int slir_irq_map(struct irq_domain *domain, unsigned int irq,
			irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, domain->host_data);
	irq_set_chip_and_handler(irq, &slir_chip, handle_level_irq);
	irq_set_nested_thread(irq, true);
	irq_set_noprobe(irq);

	return 0;
}

static void slir_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_nested_thread(irq, 0);
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops slir_irqdomain_ops = {
	.map = slir_irq_map,
	.unmap = slir_irq_unmap,
	.xlate = irq_domain_xlate_onecell,
};

static int slir_irq_domain_setup(struct slir *slir, struct device_node *dn)
{
	struct device *dev = slir->dev;
	struct irq_domain *domain;
	unsigned int virq;
	int i, err;

	domain = irq_domain_add_linear(dn, SLIR_NUM_IRQ,
				       &slir_irqdomain_ops, slir);
	if (!domain) {
		dev_err(dev, "failed to add irq domain\n");
		return -ENOMEM;
	}

	for (i = 0; i < SLIR_NUM_IRQ; i++) {
		virq = irq_create_mapping(domain, i);
		if (!virq) {
			for (; i >= 0; i--)  {
				virq = irq_find_mapping(domain, i);
				irq_dispose_mapping(virq);
			}

			dev_err(dev, "failed to create irq domain mapping\n");
			err = -EINVAL;
			goto err_remove_irqdomain;
		}

		irq_set_parent(virq, slir->irq);
	}

	slir->domain = domain;

	return 0;

err_remove_irqdomain:
	irq_domain_remove(domain);

	return err;
}

static void slir_irq_domain_teardown(struct slir *slir)
{
	struct irq_domain *domain = slir->domain;
	unsigned int virq;
	int i;

	for (i = 0; i < SLIR_NUM_IRQ; i++) {
		virq = irq_find_mapping(domain, i);
		irq_dispose_mapping(virq);
	}

	irq_domain_remove(domain);
}

static int slir_parent_irq_setup(struct slir *slir, struct device_node *dn)
{
	int err, irq;

	irq = of_irq_get(dn, 0);
	if (irq <= 0)
		return irq;

	snprintf(slir->parent_irq_name, sizeof(slir->parent_irq_name),
		 "%s:slir", dev_name(slir->dev));

	err = request_threaded_irq(irq, NULL, slir_irq, IRQF_ONESHOT |
				   IRQF_SHARED, slir->parent_irq_name, slir);
	if (err)
		return err;

	slir->irq = irq;

	return 0;
}

static void slir_parent_irq_teardown(struct slir *slir)
{
	free_irq(slir->irq, slir);
	slir->irq = 0;
}

static int slir_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct resource *res;
	struct slir *slir;
	int err;

	if (!dev->of_node || !dev->parent)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (!res)
		return -ENODEV;

	regmap = dev_get_regmap(dev->parent, res->name);
	if (!regmap)
		return -ENODEV;

	slir = kzalloc(sizeof(*slir), GFP_KERNEL);
	if (!slir)
		return -ENOMEM;

	mutex_init(&slir->domain_lock);
	slir->dev = dev;
	slir->regmap = regmap;

	err = slir_parent_irq_setup(slir, dev->of_node);
	if (err)
		goto err_free_slir;

	err = slir_irq_domain_setup(slir, dev->of_node);
	if (err)
		goto err_teardown_parent_irq;

	platform_set_drvdata(pdev, slir);

	return 0;

err_teardown_parent_irq:
	slir_parent_irq_teardown(slir);
err_free_slir:
	kfree(slir);
	return err;
}

static int slir_remove(struct platform_device *pdev)
{
	struct slir *slir = platform_get_drvdata(pdev);

	slir_irq_domain_teardown(slir);
	slir_parent_irq_teardown(slir);
	kfree(slir);

	return 0;
}

static const struct of_device_id slir_of_match[] = {
	{ .compatible = "nxp,sja1110-acu-slir" },
	{}
};
MODULE_DEVICE_TABLE(of, slir_of_match);

static struct platform_driver slir_driver = {
	.probe = slir_probe,
	.remove = slir_remove,
	.driver = {
		.name = "sja1110-slir",
		.of_match_table = slir_of_match,
	}
};
module_platform_driver(slir_driver);

MODULE_DESCRIPTION("NXP SJA1110 Interrupt Controller Driver");
MODULE_AUTHOR("Vladimir Oltean <vladimir.oltean@nxp.com>");
MODULE_LICENSE("GPL");
