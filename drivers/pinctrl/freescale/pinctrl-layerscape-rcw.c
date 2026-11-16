// SPDX-License-Identifier: GPL-2.0
/*
 * Layerscape pad muxes: fields of the RCW, changed at runtime.
 *
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/fsl/guts.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/seq_file.h>

#include "../core.h"
#include "../pinmux.h"

struct rcw_pmux_func {
	const char *name;
	u8 val;
};

struct rcw_pmux_group {
	const char *name;
	u8 rcwsr; /* one-based, as in RCWSR12 */
	u8 shift;
	u8 width;
	const struct rcw_pmux_func *funcs;
	u8 nfuncs;
};

/* The pad-mux groups of a SoC, one per field of its RCW */
struct rcw_pmux_soc {
	const struct rcw_pmux_group *groups;
	unsigned int ngroups;
};

#define RCW_PMUX_FUNC(_name, _val)	{ .name = _name, .val = _val }
#define RCW_PMUX_GROUP(_name, _rcwsr, _shift, _width, _funcs)		\
	{								\
		.name = _name, .rcwsr = _rcwsr,				\
		.shift = _shift, .width = _width,			\
		.funcs = _funcs, .nfuncs = ARRAY_SIZE(_funcs),		\
	}

/* Chassis 3.2 (LS1028A, LX2160A): fields with the same encoding */
static const struct rcw_pmux_func fsl_lsch32_iic2_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 2), RCW_PMUX_FUNC("sdhc", 6),
};

static const struct rcw_pmux_func fsl_lsch32_iic1_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func fsl_lsch32_asleep_funcs[] = {
	RCW_PMUX_FUNC("asleep", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("evt", 2),
};

static const struct rcw_pmux_func fsl_lsch32_reset_req_funcs[] = {
	RCW_PMUX_FUNC("reset-req", 0), RCW_PMUX_FUNC("gpio", 1),
};

/* LX2160A: RCWSR12..14 and RCWSR27 */
static const struct rcw_pmux_func lx2160a_iic3_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("can", 2), RCW_PMUX_FUNC("evt", 6),
};

static const struct rcw_pmux_func lx2160a_iic5_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("sdhc-clk", 2), RCW_PMUX_FUNC("spi", 3),
};

static const struct rcw_pmux_func lx2160a_iic6_funcs[] = {
	RCW_PMUX_FUNC("i2c", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("sdhc-clk", 2),
};

static const struct rcw_pmux_func lx2160a_xspi1_funcs[] = {
	RCW_PMUX_FUNC("flexspi", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_sdhc1_base_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 2), RCW_PMUX_FUNC("sdhc-spi", 3),
	RCW_PMUX_FUNC("sdhc-data4", 4),
};

static const struct rcw_pmux_func lx2160a_sdhc1_dir_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 3), RCW_PMUX_FUNC("sdhc-data", 4),
};

static const struct rcw_pmux_func lx2160a_sdhc2_data74_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("i2c", 2), RCW_PMUX_FUNC("flexspi", 6),
};

static const struct rcw_pmux_func lx2160a_sdhc2_base_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 2), RCW_PMUX_FUNC("flexspi", 6),
};

static const struct rcw_pmux_func lx2160a_uart_funcs[] = {
	RCW_PMUX_FUNC("uart", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_uart1_rtscts_funcs[] = {
	RCW_PMUX_FUNC("uart", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("uart3", 2),
};

static const struct rcw_pmux_func lx2160a_uart2_rtscts_funcs[] = {
	RCW_PMUX_FUNC("uart", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("uart4", 2),
};

static const struct rcw_pmux_func lx2160a_irq_funcs[] = {
	RCW_PMUX_FUNC("irq", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_irq_flextimer_funcs[] = {
	RCW_PMUX_FUNC("irq", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 2),
};

static const struct rcw_pmux_func lx2160a_evt_funcs[] = {
	RCW_PMUX_FUNC("evt", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 2),
};

static const struct rcw_pmux_func lx2160a_clk_out_funcs[] = {
	RCW_PMUX_FUNC("clk-out", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("flextimer", 2),
};

static const struct rcw_pmux_func lx2160a_ec1_funcs[] = {
	RCW_PMUX_FUNC("rgmii", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_ec2_funcs[] = {
	RCW_PMUX_FUNC("rgmii", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("tsec-1588", 2),
};

static const struct rcw_pmux_func lx2160a_usb_ext_funcs[] = {
	RCW_PMUX_FUNC("usb", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_gtx_clk_funcs[] = {
	RCW_PMUX_FUNC("gtx-clk", 0), RCW_PMUX_FUNC("gpio", 1),
};

static const struct rcw_pmux_func lx2160a_sdhc1_ds_funcs[] = {
	RCW_PMUX_FUNC("sdhc", 0), RCW_PMUX_FUNC("gpio", 1),
	RCW_PMUX_FUNC("spi", 2),
};

static const struct rcw_pmux_group lx2160a_groups[] = {
	/* RCWSR12 */
	RCW_PMUX_GROUP("iic2", 12, 0, 3, fsl_lsch32_iic2_funcs),
	RCW_PMUX_GROUP("iic3", 12, 3, 3, lx2160a_iic3_funcs),
	RCW_PMUX_GROUP("iic4", 12, 6, 3, lx2160a_iic3_funcs),
	RCW_PMUX_GROUP("iic5", 12, 9, 3, lx2160a_iic5_funcs),
	RCW_PMUX_GROUP("iic6", 12, 12, 3, lx2160a_iic6_funcs),
	RCW_PMUX_GROUP("xspi1-data74", 12, 15, 3, lx2160a_xspi1_funcs),
	RCW_PMUX_GROUP("xspi1-data30", 12, 18, 3, lx2160a_xspi1_funcs),
	RCW_PMUX_GROUP("xspi1-base", 12, 21, 3, lx2160a_xspi1_funcs),
	RCW_PMUX_GROUP("sdhc1-base", 12, 24, 3, lx2160a_sdhc1_base_funcs),
	RCW_PMUX_GROUP("sdhc1-dir", 12, 27, 3, lx2160a_sdhc1_dir_funcs),
	/* RCWSR13 */
	RCW_PMUX_GROUP("sdhc2-data74", 13, 0, 3, lx2160a_sdhc2_data74_funcs),
	RCW_PMUX_GROUP("sdhc2-base", 13, 3, 3, lx2160a_sdhc2_base_funcs),
	RCW_PMUX_GROUP("uart1-soutsin", 13, 6, 3, lx2160a_uart_funcs),
	RCW_PMUX_GROUP("uart1-rtscts", 13, 9, 3, lx2160a_uart1_rtscts_funcs),
	RCW_PMUX_GROUP("uart2-soutsin", 13, 12, 3, lx2160a_uart_funcs),
	RCW_PMUX_GROUP("uart2-rtscts", 13, 15, 3, lx2160a_uart2_rtscts_funcs),
	RCW_PMUX_GROUP("irq11-8", 13, 18, 3, lx2160a_irq_funcs),
	RCW_PMUX_GROUP("irq7-4", 13, 21, 3, lx2160a_irq_flextimer_funcs),
	RCW_PMUX_GROUP("irq3-0", 13, 24, 3, lx2160a_irq_flextimer_funcs),
	RCW_PMUX_GROUP("evt4-3", 13, 27, 3, lx2160a_evt_funcs),
	/* RCWSR14 */
	RCW_PMUX_GROUP("evt2-0", 14, 0, 3, lx2160a_evt_funcs),
	RCW_PMUX_GROUP("clk-out", 14, 3, 3, lx2160a_clk_out_funcs),
	RCW_PMUX_GROUP("asleep", 14, 6, 3, fsl_lsch32_asleep_funcs),
	RCW_PMUX_GROUP("reset-req", 14, 9, 1, fsl_lsch32_reset_req_funcs),
	RCW_PMUX_GROUP("iic1", 14, 10, 1, fsl_lsch32_iic1_funcs),
	/* RCWSR27 */
	RCW_PMUX_GROUP("ec1", 27, 0, 2, lx2160a_ec1_funcs),
	RCW_PMUX_GROUP("ec2", 27, 2, 2, lx2160a_ec2_funcs),
	RCW_PMUX_GROUP("usb-ext", 27, 4, 1, lx2160a_usb_ext_funcs),
	RCW_PMUX_GROUP("gtx-clk", 27, 5, 1, lx2160a_gtx_clk_funcs),
	RCW_PMUX_GROUP("sdhc1-ds", 27, 6, 2, lx2160a_sdhc1_ds_funcs),
};

static const struct rcw_pmux_soc lx2160a_soc = {
	.groups = lx2160a_groups,
	.ngroups = ARRAY_SIZE(lx2160a_groups),
};

struct rcw_pmux {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct pinctrl_desc desc;
	const struct rcw_pmux_soc *soc;
	struct pinctrl_pin_desc *pins;
};

static u32 rcw_pmux_group_mask(const struct rcw_pmux_group *group)
{
	return GENMASK(group->shift + group->width - 1, group->shift);
}

static const struct rcw_pmux_func *
rcw_pmux_find_func(const struct rcw_pmux_group *group, const char *name)
{
	unsigned int i;

	for (i = 0; i < group->nfuncs; i++) {
		if (!strcmp(group->funcs[i].name, name))
			return &group->funcs[i];
	}

	return NULL;
}

static void rcw_pmux_pin_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *s, unsigned int pin)
{
	struct rcw_pmux *pmux = pinctrl_dev_get_drvdata(pctldev);
	const struct rcw_pmux_group *group = &pmux->soc->groups[pin];
	unsigned int i;
	u32 val;

	if (fsl_guts_pmux_read(group->rcwsr, &val))
		return;

	val = field_get(rcw_pmux_group_mask(group), val);
	for (i = 0; i < group->nfuncs; i++) {
		if (group->funcs[i].val == val) {
			seq_puts(s, group->funcs[i].name);
			return;
		}
	}
	seq_printf(s, "0x%x", val);
}

static const struct pinctrl_ops rcw_pmux_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.pin_dbg_show = rcw_pmux_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int rcw_pmux_set_mux(struct pinctrl_dev *pctldev,
			    unsigned int fsel, unsigned int gsel)
{
	struct rcw_pmux *pmux = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *func;
	const struct rcw_pmux_group *group;
	const struct rcw_pmux_func *mux;
	const struct group_desc *grp;
	u32 mask;

	func = pinmux_generic_get_function(pctldev, fsel);
	grp = pinctrl_generic_get_group(pctldev, gsel);
	if (!func || !grp)
		return -EINVAL;
	group = grp->data;

	mux = rcw_pmux_find_func(group, func->func->name);
	if (!mux) {
		dev_err(pmux->dev, "%s has no %s function\n",
			group->name, func->func->name);
		return -EINVAL;
	}

	mask = rcw_pmux_group_mask(group);

	return fsl_guts_pmux_override(group->rcwsr, field_prep(mask, mux->val),
				      mask);
}

static const struct pinmux_ops rcw_pmux_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = rcw_pmux_set_mux,
	.strict = true,
};

static int rcw_pmux_add_functions(struct rcw_pmux *pmux)
{
	const struct rcw_pmux_soc *soc = pmux->soc;
	const struct rcw_pmux_group *group;
	const char **groups;
	unsigned int i, j, k, n;
	int ret;

	for (i = 0; i < soc->ngroups; i++) {
		group = &soc->groups[i];
		for (j = 0; j < group->nfuncs; j++) {
			const char *name = group->funcs[j].name;

			/* already added from an earlier group? */
			for (k = 0; k < i; k++) {
				if (rcw_pmux_find_func(&soc->groups[k], name))
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
				if (rcw_pmux_find_func(&soc->groups[k], name))
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

static int rcw_pmux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct rcw_pmux_soc *soc;
	struct rcw_pmux *pmux;
	unsigned int i;
	u32 val;
	int ret;

	/* The override needs the DCSR view of DCFG, mapped by fsl-guts */
	if (!fsl_guts_pmux_override_supported())
		return dev_err_probe(dev, -ENODEV,
				     "no RCW override through fsl-guts: the dcfg node needs its DCSR reg\n");

	soc = device_get_match_data(dev);
	if (!soc)
		return -ENODEV;

	pmux = devm_kzalloc(dev, sizeof(*pmux), GFP_KERNEL);
	if (!pmux)
		return -ENOMEM;
	pmux->dev = dev;
	pmux->soc = soc;

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
	pmux->desc.pctlops = &rcw_pmux_pctlops;
	pmux->desc.pmxops = &rcw_pmux_pmxops;
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

	ret = rcw_pmux_add_functions(pmux);
	if (ret)
		return ret;

	for (i = 0; i < soc->ngroups; i++) {
		u8 rcwsr = soc->groups[i].rcwsr;

		if (i && rcwsr == soc->groups[i - 1].rcwsr)
			continue;
		if (!fsl_guts_pmux_read(rcwsr, &val))
			dev_dbg(dev, "pad muxes of RCWSR%u = 0x%08x\n", rcwsr,
				val);
	}

	return pinctrl_enable(pmux->pctl);
}

static const struct of_device_id rcw_pmux_of_match[] = {
	{ .compatible = "fsl,lx2160a-rcw-pinmux", .data = &lx2160a_soc },
	{ }
};
MODULE_DEVICE_TABLE(of, rcw_pmux_of_match);

static struct platform_driver rcw_pmux_driver = {
	.driver = {
		.name = "layerscape-rcw-pinmux",
		.of_match_table = rcw_pmux_of_match,
	},
	.probe = rcw_pmux_probe,
};
module_platform_driver(rcw_pmux_driver);

MODULE_AUTHOR("Vincent Jardin <vjardin@free.fr>");
MODULE_DESCRIPTION("NXP Layerscape RCW pad-mux driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("FSL_GUTS");
