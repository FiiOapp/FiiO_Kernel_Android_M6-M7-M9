/*
 * Samsung EXYNOS SoC series USB DRD PHY DebugFS file
 *
 * Phy provider for USB 3.0 DRD controller on Exynos SoC series
 *
 * Copyright (C) 2016 Samsung Electronics Co., Ltd.
 * Author: Kyounghye Yun <k-hye.yun@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/regmap.h>

#include <linux/usb/ch9.h>
#include "phy-exynos-usbdrd.h"
#include "phy-exynos-debug.h"

struct exynos_debugfs_prvdata *prvdata;

static const struct debugfs_reg32 exynos_usb3drd_regs[] = {
	dump_register(LINKSYSTEM),
	dump_register(PHYUTMI),
	dump_register(PHYCLKRST),
	dump_register(PHYREG0),
	dump_register(PHYPARAM0),
	dump_register(PHYPARAM1),
	dump_register(PHYTEST),
	dump_register(PHYRESUME),
	dump_register(PHYPCSVAL),
	dump_register(LINKPORT),
	dump_register(PHYPARAM2),
};

static const struct debugfs_reg32 exynos_usb2drd_regs[] = {
	dump_register(LINKSYSTEM),
	dump_register(PHYUTMI),
	dump_register(PHYCLKRST),
	dump_register(PHYPARAM0),
	dump_register(PHYPOWERDOWN),
	dump_register(PHYRESUME),
	dump_register(LINKPORT),
	dump_register(HSPHYCTRL),
	dump_register(HSPHYPLLTUNE),
};

static const struct debugfs_regmap32 exynos_usb2drd_regmap[] = {
	dump_regmap(LINKSYSTEM, HOST_SYSTEM_ERR, 31),
	dump_regmap(LINKSYSTEM, PHY_POWER_DOWN, 30),
	dump_regmap(LINKSYSTEM, PHY_SW_RESET, 29),
	dump_regmap(LINKSYSTEM, LINK_SW_RESET, 28),
	dump_regmap(LINKSYSTEM, XHCI_VERSION_CONTROL, 27),
	dump_regmap(LINKSYSTEM, FORCE_VBUSVALID, 8),
	dump_regmap(LINKSYSTEM, FORCE_BVALID, 7),
	dump_regmap_mask(LINKSYSTEM, FLADJ, 1),
	dump_regmap(PHYUTMI, UTMI_SUSPEND_COM_N, 12),
	dump_regmap(PHYUTMI, UTMI_L1_SUSPEND_COM_N, 11),
	dump_regmap(PHYUTMI, VBUSVLDEXTSEL, 10),
	dump_regmap(PHYUTMI, VBUSVLDEXT, 9),
	dump_regmap(PHYUTMI, TXBITSTUFFENH, 8),
	dump_regmap(PHYUTMI, TXBITSTUFFEN, 7),
	dump_regmap(PHYUTMI, OTGDISABLE, 6),
	dump_regmap(PHYUTMI, IDPULLUP, 5),
	dump_regmap(PHYUTMI, DRVVBUS, 4),
	dump_regmap(PHYUTMI, DPPULLDOWN, 3),
	dump_regmap(PHYUTMI, DMPULLDOWN, 2),
	dump_regmap(PHYUTMI, FORCESUSPEND, 1),
	dump_regmap(PHYUTMI, FORCESLEEP, 0),
	dump_regmap(PHYCLKRST, EN_UTMISUSPEND, 31),
	dump_regmap_mask(PHYCLKRST, SSC_REFCLKSEL, 23),
	dump_regmap_mask(PHYCLKRST, SSC_RANGE, 21),
	dump_regmap(PHYCLKRST, SSC_EN, 20),
	dump_regmap(PHYCLKRST, REF_SSP_EN, 19),
	dump_regmap(PHYCLKRST, REF_CLKDIV2, 18),
	dump_regmap_mask(PHYCLKRST, MPLL_MULTIPLIER, 11),
	dump_regmap_mask(PHYCLKRST, FSEL, 5),
	dump_regmap(PHYCLKRST, RETENABLEN, 4),
	dump_regmap_mask(PHYCLKRST, REFCLKSEL, 2),
	dump_regmap(PHYCLKRST, PORTRESET, 1),
	dump_regmap(PHYCLKRST, COMMONONN, 0),
	dump_regmap_mask(PHYPARAM0, TXVREFTUNE, 22),
	dump_regmap_mask(PHYPARAM0, TXRISETUNE, 20),
	dump_regmap_mask(PHYPARAM0, TXRESTUNE, 18),
	dump_regmap(PHYPARAM0, TXPREEMPPULSETUNE, 17),
	dump_regmap_mask(PHYPARAM0, TXPREEMPAMPTUNE, 15),
	dump_regmap_mask(PHYPARAM0, TXHSXVTUNE, 13),
	dump_regmap_mask(PHYPARAM0, TXFSLSTUNE, 9),
	dump_regmap_mask(PHYPARAM0, SQRXTUNE, 6),
	dump_regmap_mask(PHYPARAM0, OTGTUNE, 3),
	dump_regmap_mask(PHYPARAM0, COMPDISTUNE, 0),
	dump_regmap(PHYPOWERDOWN, VATESTENB, 6),
	dump_regmap_mask(PHYPOWERDOWN, TEST_BURNIN, 4),
	dump_regmap(PHYRESUME, BYPASS_SEL, 4),
	dump_regmap(PHYRESUME, BYPASS_DM_EN, 3),
	dump_regmap(PHYRESUME, BYPASS_DP_EN, 2),
	dump_regmap(PHYRESUME, BYPASS_DM_DATA, 1),
	dump_regmap(PHYRESUME, BYPASS_DP_DATA, 0),
	dump_regmap_mask(LINKPORT, HOST_NUM_U2_PORT, 9),
	dump_regmap(LINKPORT, HOST_U2_PORT_DISABLE, 7),
	dump_regmap(LINKPORT, PORT_POWER_CONTROL, 6),
	dump_regmap(LINKPORT, HOST_PORT_OVCR_U2, 4),
	dump_regmap(LINKPORT, HOST_PORT_OVCR_U2_SEL, 2),
	dump_regmap(LINKPORT, PERM_ATTACH_U2, 0),
	dump_regmap(HSPHYCTRL, PHYSWRSTALL, 31),
	dump_regmap(HSPHYCTRL, SIDDQ, 6),
	dump_regmap(HSPHYCTRL, PHYSWRST, 0),
	dump_regmap(HSPHYPLLTUNE, PLL_B_TUNE, 6),
	dump_regmap_mask(HSPHYPLLTUNE, PLL_I_TUNE, 4),
	dump_regmap_mask(HSPHYPLLTUNE, PLL_P_TUNE, 0),
};

static const struct debugfs_regmap32 exynos_usb3drd_regmap[] = {
	dump_regmap(LINKSYSTEM, HOST_SYSTEM_ERR, 31),
	dump_regmap(LINKSYSTEM, PHY_POWER_DOWN, 30),
	dump_regmap(LINKSYSTEM, XHCI_VERSION_CONTROL, 27),
	dump_regmap(LINKSYSTEM, FORCE_VBUSVALID, 8),
	dump_regmap(LINKSYSTEM, FORCE_BVALID, 7),
	dump_regmap_mask(LINKSYSTEM, FLADJ, 1),
	dump_regmap(PHYUTMI, VBUSVLDEXTSEL, 10),
	dump_regmap(PHYUTMI, VBUSVLDEXT, 9),
	dump_regmap(PHYUTMI, TXBITSTUFFENH, 8),
	dump_regmap(PHYUTMI, TXBITSTUFFEN, 7),
	dump_regmap(PHYUTMI, OTGDISABLE, 6),
	dump_regmap(PHYUTMI, IDPULLUP, 5),
	dump_regmap(PHYUTMI, DRVVBUS, 4),
	dump_regmap(PHYUTMI, DPPULLDOWN, 3),
	dump_regmap(PHYUTMI, DMPULLDOWN, 2),
	dump_regmap(PHYUTMI, FORCESUSPEND, 1),
	dump_regmap(PHYUTMI, FORCESLEEP, 0),
	dump_regmap(PHYPIPE, PHY_CLOCK_SEL, 4),
	dump_regmap(PHYCLKRST, EN_UTMISUSPEND, 31),
	dump_regmap(PHYCLKRST, SSC_EN, 20),
	dump_regmap(PHYCLKRST, REF_SSP_EN, 19),
	dump_regmap(PHYCLKRST, REF_CLKDIV2, 18),
	dump_regmap_mask(PHYCLKRST, MPLL_MULTIPLIER, 11),
	dump_regmap_mask(PHYCLKRST, FSEL, 5),
	dump_regmap(PHYCLKRST, RETENABLEN, 4),
	dump_regmap_mask(PHYCLKRST, REFCLKSEL, 2),
	dump_regmap(PHYCLKRST, PORTRESET, 1),
	dump_regmap(PHYCLKRST, COMMONONN, 0),
	dump_regmap_mask(PHYREG0, SSC_REFCLKSEL, 23),
	dump_regmap_mask(PHYREG0, SSC_RANGE, 20),
	dump_regmap(PHYPARAM0, REF_USE_PAD, 31),
	dump_regmap_mask(PHYPARAM0, REF_LOSLEVEL, 26),
	dump_regmap_mask(PHYPARAM0, TXVREFTUNE, 22),
	dump_regmap_mask(PHYPARAM0, TXRISETUNE, 20),
	dump_regmap_mask(PHYPARAM0, TXRESTUNE, 18),
	dump_regmap(PHYPARAM0, TXPREEMPPULSETUNE, 17),
	dump_regmap_mask(PHYPARAM0, TXPREEMPAMPTUNE, 15),
	dump_regmap_mask(PHYPARAM0, TXHSXVTUNE, 13),
	dump_regmap_mask(PHYPARAM0, TXFSLSTUNE, 9),
	dump_regmap_mask(PHYPARAM0, SQRXTUNE, 6),
	dump_regmap_mask(PHYPARAM0, OTGTUNE, 3),
	dump_regmap_mask(PHYPARAM0, COMPDISTUNE, 0),
	dump_regmap_mask(PHYPARAM1, TX0_TERM_OFFSET, 26),
	dump_regmap_mask(PHYPARAM1, PCS_TXSWING_FULL, 12),
	dump_regmap_mask(PHYPARAM1, PCS_TXDEEMPH_3P5DB, 0),
	dump_regmap(PHYTEST, POWERDOWN_SSP, 3),
	dump_regmap(PHYTEST, POWERDOWN_HSP, 2),
	dump_regmap(PHYRESUME, BYPASS_SEL, 4),
	dump_regmap(PHYRESUME, BYPASS_DM_EN, 3),
	dump_regmap(PHYRESUME, BYPASS_DP_EN, 2),
	dump_regmap(PHYRESUME, BYPASS_DM_DATA, 1),
	dump_regmap(PHYRESUME, BYPASS_DP_DATA, 0),
	dump_regmap_mask(PHYPCSVAL, PCS_RX_LOS_MASK_VAL, 0),
	dump_regmap_mask(LINKPORT, HOST_NUM_U3_PORT, 13),
	dump_regmap_mask(LINKPORT, HOST_NUM_U2_PORT, 9),
	dump_regmap(LINKPORT, HOST_U3_PORT_DISABLE, 8),
	dump_regmap(LINKPORT, HOST_U2_PORT_DISABLE, 7),
	dump_regmap(LINKPORT, PORT_POWER_CONTROL, 6),
	dump_regmap(LINKPORT, HOST_PORT_OVCR_U3, 5),
	dump_regmap(LINKPORT, HOST_PORT_OVCR_U2, 4),
	dump_regmap(LINKPORT, HOST_PORT_OVCR_U3_SEL, 3),
	dump_regmap(LINKPORT, HOST_PORT_OVCR_U2_SEL, 2),
	dump_regmap(LINKPORT, PERM_ATTACH_U2, 0),
	dump_regmap_mask(PHYPARAM2, TX_VBOOST_LVL, 3),
	dump_regmap_mask(PHYPARAM2, LOS_BIAS, 0),
};

int debugfs_phy_power_state(struct exynos_usbdrd_phy *phy_drd, int phy_index)
{
	struct regmap *reg_pmu;
	u32 pmu_offset;
	int phy_on;
	int ret;

	reg_pmu = phy_drd->phys[phy_index].reg_pmu;
	pmu_offset = phy_drd->phys[phy_index].pmu_offset;
	ret = regmap_read(reg_pmu, pmu_offset, &phy_on);
	if (ret) {
		dev_err(phy_drd->dev, "Can't read 0x%x\n", pmu_offset);
		return ret;
	}
	phy_on &= phy_drd->phys[phy_index].pmu_mask;

	return phy_on;
}

int debugfs_print_regmap(struct seq_file *s, const struct debugfs_regmap32 *regs,
				int nregs, void __iomem *base,
				const struct debugfs_reg32 *parent)
{
	int i, j= 0;
	int bit = 0;
	unsigned int bitmask;
	int max_string = 24;
	int calc_tab;
	u32 bit_value, reg_value;

	reg_value = readl(base + parent->offset);
	seq_printf(s, "%s (0x%04lx) : 0x%08x\n", parent->name,
							parent->offset, reg_value);
	for (i = 0; i < nregs; i++, regs++) {
		if (!strcmp(regs->name, parent->name)) {
			bit_value = (reg_value & regs->bitmask) >> regs->bitoffset;

			seq_printf(s, "\t%s", regs->bitname);
			calc_tab = max_string/8 - strlen(regs->bitname)/8;
			for (j = 0 ; j < calc_tab; j++)
				seq_printf(s, "\t");

			if (regs->mask) {
				bitmask = regs->bitmask;
				bitmask = bitmask >> regs->bitoffset;
				while (bitmask) {
					bitmask = bitmask >> 1;
					bit++;
				}
				seq_printf(s, "[%d:%d]\t: 0x%x\n", (int)regs->bitoffset,
						((int)regs->bitoffset + bit - 1), bit_value);
				bit = 0;
			} else {
				seq_printf(s, "[%d]\t: 0x%x\n", (int)regs->bitoffset,
										bit_value);
			}
		}
	}
	return 0;

}

static int debugfs_show_regmap(struct seq_file *s, void *data)
{
	struct exynos_debugfs_prvdata *prvdata = s->private;
	struct debugfs_regset_map *regmap = prvdata->regmap;
	struct debugfs_regset32 *regset = prvdata->regset;
	const struct debugfs_reg32 *regs = regset->regs;
	int phy_on, ret, i;

	phy_on = debugfs_phy_power_state(prvdata->phy_drd, 0);
	if (phy_on < 0) {
		seq_printf(s, "can't read PHY register, error : %d\n", phy_on);
		return ret;
	}
	if (!phy_on) {
		seq_printf(s, "can't get PHY register, PHY%d : Power OFF\n", i);
		return 0;
	}
	for (i = 0; i < regset->nregs; i++, regs++) {
		debugfs_print_regmap(s, regmap->regs, regmap->nregs,
						regset->base, regs);
	}

	return 0;
}

static int debugfs_open_regmap(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_show_regmap, inode->i_private);
}

static const struct file_operations fops_regmap = {
	.open =		debugfs_open_regmap,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	single_release,
};

int debugfs_print_regdump(struct seq_file *s, struct exynos_usbdrd_phy *phy_drd,
				const struct debugfs_reg32 *regs, int nregs,
				void __iomem *base)
{
	int phy_on;
	int i;

	for (i = 0; i < EXYNOS_DRDPHYS_NUM; i++) {
		phy_on = debugfs_phy_power_state(phy_drd, i);
		if (phy_on < 0) {
			seq_printf(s, "can't read PHY register, error : %d \n", phy_on);
			return phy_on;
		}
		if (!phy_on) {
			seq_printf(s, "can't get PHY register, PHY%d : Power OFF\n", i);
			continue;
		}

		for (i = 0; i < nregs; i++, regs++) {
			seq_printf(s, "%s", regs->name);
			if (strlen(regs->name) < 8)
				seq_printf(s, "\t\t");
			else
				seq_printf(s, "\t");

			seq_printf(s, "= 0x%08x\n", readl(base + regs->offset));
		}
	}

	return 0;
}
static int debugfs_show_regdump(struct seq_file *s, void *data)
{
	struct exynos_debugfs_prvdata *prvdata = s->private;
	struct debugfs_regset32 *regset = prvdata->regset;
	int ret;

	ret = debugfs_print_regdump(s, prvdata->phy_drd, regset->regs,
					regset->nregs, regset->base);
	if (ret < 0)
		return ret;

	return 0;
}

static int debugfs_open_regdump(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_show_regdump, inode->i_private);
}

static const struct file_operations fops_regdump = {
	.open =		debugfs_open_regdump,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	single_release,
};
static int debugfs_show_bitset(struct seq_file *s, void *data)
{
	char *b_name = s->private;
	struct debugfs_regset_map *regmap = prvdata->regmap;
	const struct debugfs_regmap32 *cmp = regmap->regs;
	const struct debugfs_regmap32 *regs;
	unsigned int bitmask;
	int i, bit = 0;
	u32 reg_value, bit_value;

	for (i = 0; i < regmap->nregs; i++, cmp++) {
		if (!strcmp(cmp->bitname, b_name)) {
			regs = cmp;
			break;
		}
	}

	reg_value = readl(prvdata->regset->base + regs->offset);
	bit_value = (reg_value & regs->bitmask) >> regs->bitoffset;
	if (regs->mask) {
		bitmask = regs->bitmask;
		bitmask = bitmask >> regs->bitoffset;
		while (bitmask) {
			bitmask = bitmask >> 1;
			bit++;
		}
		seq_printf(s, "%s [%d:%d] = 0x%x\n", regs->name,
				(int)regs->bitoffset,
				((int)regs->bitoffset + bit - 1), bit_value);
	} else {
		seq_printf(s, "%s [%d] = 0x%x\n", regs->name,
				(int)regs->bitoffset, bit_value);
	}
	return 0;
}
static ssize_t debugfs_write_regset(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	char *reg_name = s->private;
	struct debugfs_regset32 *regset = prvdata->regset;
	const struct debugfs_reg32 *regs = regset->regs;
	unsigned long value;
	char buf[8];
	int i;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	value = simple_strtol(buf, NULL, 16);

	for (i = 0; i < regset->nregs; i++, regs++) {
		if (!strcmp(regs->name, reg_name)) {
			break;
		}
	}

	writel(value, regset->base + regs->offset);

	return count;
}
static ssize_t debugfs_write_bitset(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	char *b_name = s->private;
	struct debugfs_regset_map *regmap = prvdata->regmap;
	const struct debugfs_regmap32 *regs = regmap->regs;
	unsigned long value;
	char buf[32];
	int i;
	u32 reg_value;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		seq_printf(s, "%s, write error \n", __func__);
		return -EFAULT;
	}
	value = simple_strtol(buf, NULL, 2);

	for (i = 0; i < regmap->nregs; i++, regs++) {
		if (!strcmp(regs->bitname, b_name))
			break;
	}

	value = value << regs->bitoffset;
	reg_value = readl(prvdata->regset->base + regs->offset);
	reg_value &= ~(regs->bitmask);
	reg_value |= value;
	writel(reg_value, prvdata->regset->base + regs->offset);

	return count;
}

static int debugfs_open_bitset(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_show_bitset, inode->i_private);
}

static int debugfs_show_regset(struct seq_file *s, void *data)
{
	char *p_name = s->private;
	struct debugfs_regset32 *regset = prvdata->regset;
	struct debugfs_regset_map *regmap = prvdata->regmap;
	const struct debugfs_reg32 *regs = regset->regs;
	const struct debugfs_reg32 *parents;
	int i;

	for (i = 0; i < regset->nregs; i++, regs++) {
		if (!strcmp(regs->name, p_name)) {
			parents = regs;
			break;
		}
	}

	debugfs_print_regmap(s, prvdata->regmap->regs, regmap->nregs,
						regset->base, parents);

	return 0;
}

static int debugfs_open_regset(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_show_regset, inode->i_private);
}

static const struct file_operations fops_regset = {
	.open =		debugfs_open_regset,
	.write =	debugfs_write_regset,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	single_release,
};

static const struct file_operations fops_bitset = {
	.open =		debugfs_open_bitset,
	.write =	debugfs_write_bitset,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	single_release,
};

static int debugfs_create_regfile(struct exynos_debugfs_prvdata *prvdata,
					const struct debugfs_reg32 *parents,
					struct dentry *root)
{
	struct debugfs_regset_map *regmap = prvdata->regmap;
	const struct debugfs_regmap32 *regs = regmap->regs;
	struct dentry *file;
	int i, ret;

	file = debugfs_create_file(parents->name, S_IRUGO | S_IWUGO, root,
							parents->name, &fops_regset);
	if (!file) {
		ret = -ENOMEM;
		return ret;
	}
	for (i = 0; i < regmap->nregs; i++, regs++) {
		if (!strcmp(regs->name, parents->name)) {
			file = debugfs_create_file(regs->bitname, S_IRUGO | S_IWUGO,
					root, regs->bitname, &fops_bitset);
			if (!file) {
				ret = -ENOMEM;
				return ret;
			}
		}
	}

	return 0;
}

static int debugfs_create_regdir(struct exynos_debugfs_prvdata *prvdata,
						struct dentry *root)
{
	struct exynos_usbdrd_phy *phy_drd = prvdata->phy_drd;
	struct debugfs_regset32 *regset = prvdata->regset;
	const struct debugfs_reg32 *regs = regset->regs;
	struct dentry	*dir;
	int ret, i;

	for (i = 0; i < regset->nregs; i++, regs++) {
		dir = debugfs_create_dir(regs->name, root);
		if (!dir) {
			dev_err(phy_drd->dev, "failed to create '%s' reg dir",
								regs->name);
			return -ENOMEM;
		}
		ret = debugfs_create_regfile(prvdata, regs, dir);
		if (ret < 0) {
			dev_err(phy_drd->dev, "failed to create bitfile for %s, error : %d\n",
						regs->name, ret);
			return ret;
		}
	}

	return 0;
}
int exynos_usbdrd_debugfs_init(struct exynos_usbdrd_phy *phy_drd)
{
	struct device	*dev = phy_drd->dev;
	struct dentry	*root;
	struct dentry	*dir;
	struct dentry	*file;
	u32 version = phy_drd->usbphy_info.version;
	int		ret;


	root = debugfs_create_dir(dev_name(dev), NULL);
	if (!root) {
		dev_err(dev, "failed to create root directory for USBPHY debugfs");
		ret = -ENOMEM;
		goto err0;
	}

	prvdata = devm_kmalloc(dev, sizeof(struct exynos_debugfs_prvdata *), GFP_KERNEL);
	if (!prvdata) {
		dev_err(dev, "failed to alloc private data for debugfs");
		ret = -ENOMEM;
		goto err1;
	}
	prvdata->root = root;
	prvdata->phy_drd = phy_drd;

	prvdata->regset = devm_kmalloc(dev, sizeof(*prvdata->regset), GFP_KERNEL);
	if (!prvdata->regset) {
		dev_err(dev, "failed to alloc regmap");
		ret = -ENOMEM;
		goto err1;
	}

	if ((EXYNOS_USBCON_VER_02_0_0 <= version) &&
			(version <= EXYNOS_USBCON_VER_02_MAX)) {
		/* for USB2PHY */
		prvdata->regset->regs = exynos_usb2drd_regs;
		prvdata->regset->nregs = ARRAY_SIZE(exynos_usb2drd_regs);
	} else {
		/* for USB3PHY */
		prvdata->regset->regs = exynos_usb3drd_regs;
		prvdata->regset->nregs = ARRAY_SIZE(exynos_usb3drd_regs);
	}
	prvdata->regset->base = phy_drd->reg_phy;

	prvdata->regmap = devm_kmalloc(dev, sizeof(*prvdata->regmap), GFP_KERNEL);
	if (!prvdata->regmap) {
		dev_err(dev, "failed to alloc regmap");
		ret = -ENOMEM;
		goto err1;
	}

	if ((EXYNOS_USBCON_VER_02_0_0 <= version) &&
		/* for USB2PHY */
		(version <= EXYNOS_USBCON_VER_02_MAX)) {
		prvdata->regmap->regs = exynos_usb2drd_regmap;
		prvdata->regmap->nregs = ARRAY_SIZE(exynos_usb2drd_regmap);
	} else {
		/* for USB3PHY */
		prvdata->regmap->regs = exynos_usb3drd_regmap;
		prvdata->regmap->nregs = ARRAY_SIZE(exynos_usb3drd_regmap);
	}

	file = debugfs_create_file("regdump", S_IRUGO, root, prvdata, &fops_regdump);
	if (!file) {
		dev_err(dev, "failed to create file for register dump");
		ret = -ENOMEM;
		goto err1;
	}

	file = debugfs_create_file("regmap", S_IRUGO, root, prvdata, &fops_regmap);
	if (!file) {
		dev_err(dev, "failed to create file for register dump");
		ret = -ENOMEM;
		goto err1;
	}

	dir = debugfs_create_dir("regset", root);
	if (!dir) {
		ret = -ENOMEM;
		goto err1;
	}

	ret = debugfs_create_regdir(prvdata, dir);
	if (ret < 0) {
		dev_err(dev, "failed to create regfile, error = %d\n", ret);
		goto err1;
	}


	return 0;

err1:
	debugfs_remove_recursive(root);
err0:
	return ret;
}