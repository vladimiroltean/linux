// SPDX-License-Identifier: GPL-2.0
/*
 * Layerscape pad muxes in SCFG_RCWPMUXCR0, the extension of the RCW.
 *
 * Copyright 2026 NXP
 */

#include <linux/bits.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>

#include "../core.h"
#include "../pinmux.h"

struct scfg_pmux_func {
	const char *name;
	u32 val;
};

/* A group is a pad, which has a selector of its own, at @shift */
struct scfg_pmux_group {
	const char *name;
	u8 shift;
	const struct scfg_pmux_func *funcs;
	u8 nfuncs;
};

struct scfg_pmux_soc {
	const struct scfg_pmux_group *groups;
	unsigned int ngroups;
};

#define SCFG_PMUX_SEL_MASK	GENMASK(2, 0)

#define SCFG_PMUX_FUNC(_name, _val)	{ .name = _name, .val = _val }
#define SCFG_PMUX_GROUP(_name, _shift, _funcs)				\
	{								\
		.name = _name, .shift = _shift,				\
		.funcs = _funcs, .nfuncs = ARRAY_SIZE(_funcs),		\
	}

static const struct scfg_pmux_func ls1046a_funcs[] = {
	SCFG_PMUX_FUNC("i2c", 0), SCFG_PMUX_FUNC("gpio", 1),
	SCFG_PMUX_FUNC("evt", 2), SCFG_PMUX_FUNC("usb", 3),
	SCFG_PMUX_FUNC("flextimer", 5),
};

static const struct scfg_pmux_group ls1046a_groups[] = {
	SCFG_PMUX_GROUP("iic3-scl", 12, ls1046a_funcs),
	SCFG_PMUX_GROUP("iic3-sda", 8, ls1046a_funcs),
	SCFG_PMUX_GROUP("iic4-scl", 4, ls1046a_funcs),
	SCFG_PMUX_GROUP("iic4-sda", 0, ls1046a_funcs),
};

static const struct scfg_pmux_soc ls1046a_soc = {
	.groups = ls1046a_groups,
	.ngroups = ARRAY_SIZE(ls1046a_groups),
};

struct scfg_pmux {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct pinctrl_desc desc;
	const struct scfg_pmux_soc *soc;
	struct pinctrl_pin_desc *pins;
	struct regmap *regmap;
	u32 offset;
};

static u32 scfg_pmux_group_mask(const struct scfg_pmux_group *group)
{
	return SCFG_PMUX_SEL_MASK << group->shift;
}

static const struct scfg_pmux_func *
scfg_pmux_find_func(const struct scfg_pmux_group *group, const char *name)
{
	unsigned int i;

	for (i = 0; i < group->nfuncs; i++) {
		if (!strcmp(group->funcs[i].name, name))
			return &group->funcs[i];
	}

	return NULL;
}

static void scfg_pmux_pin_dbg_show(struct pinctrl_dev *pctldev,
				   struct seq_file *s, unsigned int pin)
{
	struct scfg_pmux *pmux = pinctrl_dev_get_drvdata(pctldev);
	const struct scfg_pmux_group *group = &pmux->soc->groups[pin];
	unsigned int i;
	u32 val;

	if (regmap_read(pmux->regmap, pmux->offset, &val))
		return;

	val = (val & scfg_pmux_group_mask(group)) >> group->shift;
	for (i = 0; i < group->nfuncs; i++) {
		if (group->funcs[i].val == val) {
			seq_puts(s, group->funcs[i].name);
			return;
		}
	}
	seq_printf(s, "0x%x", val);
}

static const struct pinctrl_ops scfg_pmux_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.pin_dbg_show = scfg_pmux_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int scfg_pmux_set_mux(struct pinctrl_dev *pctldev,
			     unsigned int fsel, unsigned int gsel)
{
	struct scfg_pmux *pmux = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *func;
	const struct scfg_pmux_group *group;
	const struct scfg_pmux_func *mux;
	const struct group_desc *grp;

	func = pinmux_generic_get_function(pctldev, fsel);
	grp = pinctrl_generic_get_group(pctldev, gsel);
	if (!func || !grp)
		return -EINVAL;
	group = grp->data;

	mux = scfg_pmux_find_func(group, func->func->name);
	if (!mux) {
		dev_err(pmux->dev, "%s has no %s function\n",
			group->name, func->func->name);
		return -EINVAL;
	}

	return regmap_update_bits(pmux->regmap, pmux->offset,
				  scfg_pmux_group_mask(group),
				  mux->val << group->shift);
}

static const struct pinmux_ops scfg_pmux_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = scfg_pmux_set_mux,
	.strict = true,
};

static int scfg_pmux_add_functions(struct scfg_pmux *pmux)
{
	const struct scfg_pmux_soc *soc = pmux->soc;
	const struct scfg_pmux_group *group;
	const char **groups;
	unsigned int i, j, k, n;
	int ret;

	for (i = 0; i < soc->ngroups; i++) {
		group = &soc->groups[i];
		for (j = 0; j < group->nfuncs; j++) {
			const char *name = group->funcs[j].name;

			/* already added from an earlier group? */
			for (k = 0; k < i; k++) {
				if (scfg_pmux_find_func(&soc->groups[k], name))
					break;
			}
			if (k < i)
				continue;

			groups = devm_kcalloc(pmux->dev, soc->ngroups,
					      sizeof(*groups), GFP_KERNEL);
			if (!groups)
				return -ENOMEM;
			n = 0;
			for (k = i; k < soc->ngroups; k++) {
				if (scfg_pmux_find_func(&soc->groups[k], name))
					groups[n++] = soc->groups[k].name;
			}
			ret = pinmux_generic_add_function(pmux->pctl, name,
							  groups, n, NULL);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int scfg_pmux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct scfg_pmux_soc *soc;
	struct scfg_pmux *pmux;
	unsigned int i;
	int ret;

	soc = device_get_match_data(dev);
	if (!soc)
		return -ENODEV;

	pmux = devm_kzalloc(dev, sizeof(*pmux), GFP_KERNEL);
	if (!pmux)
		return -ENOMEM;
	pmux->dev = dev;
	pmux->soc = soc;

	pmux->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(pmux->regmap))
		return dev_err_probe(dev, PTR_ERR(pmux->regmap),
				     "no syscon regmap in the parent\n");

	ret = of_property_read_u32(dev->of_node, "reg", &pmux->offset);
	if (ret)
		return dev_err_probe(dev, ret, "no reg\n");

	pmux->pins = devm_kcalloc(dev, soc->ngroups, sizeof(*pmux->pins),
				  GFP_KERNEL);
	if (!pmux->pins)
		return -ENOMEM;

	for (i = 0; i < soc->ngroups; i++) {
		pmux->pins[i].number = i;
		pmux->pins[i].name = soc->groups[i].name;
	}
	pmux->desc.name = dev_name(dev);
	pmux->desc.pins = pmux->pins;
	pmux->desc.npins = soc->ngroups;
	pmux->desc.pctlops = &scfg_pmux_pctlops;
	pmux->desc.pmxops = &scfg_pmux_pmxops;
	pmux->desc.owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(dev, &pmux->desc, pmux,
					     &pmux->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "pinctrl registration failed\n");

	for (i = 0; i < soc->ngroups; i++) {
		ret = pinctrl_generic_add_group(pmux->pctl, soc->groups[i].name,
						&pmux->pins[i].number, 1,
						(void *)&soc->groups[i]);
		if (ret < 0)
			return ret;
	}

	ret = scfg_pmux_add_functions(pmux);
	if (ret)
		return ret;

	return pinctrl_enable(pmux->pctl);
}

static const struct of_device_id scfg_pmux_of_match[] = {
	{ .compatible = "fsl,ls1046a-scfg-pinmux", .data = &ls1046a_soc },
	{}
};
MODULE_DEVICE_TABLE(of, scfg_pmux_of_match);

static struct platform_driver scfg_pmux_driver = {
	.driver = {
		.name = "layerscape-scfg-pinmux",
		.of_match_table = scfg_pmux_of_match,
	},
	.probe = scfg_pmux_probe,
};
module_platform_driver(scfg_pmux_driver);

MODULE_AUTHOR("Vladimir Oltean <vladimir.oltean@nxp.com>");
MODULE_DESCRIPTION("NXP Layerscape SCFG pad-mux driver");
MODULE_LICENSE("GPL");
