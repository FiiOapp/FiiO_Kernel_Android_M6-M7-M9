/*
 * Copyright (C) 2016 Samsung Electronics Co.Ltd
 * http://www.samsung.com
 *
 * USIM Detection driver
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
*/

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/delay.h>

#include <linux/mcu_ipc.h>
#include <linux/usim_det.h>

static struct usim_det_data *g_udd;

static inline void usim_det_set_det0_value(struct usim_det_data *udd, int value)
{
	if (value != udd->usim0_det) {
		mbox_update_value(MCU_CP, udd->mbx_ap_united_status, value,
			udd->sbi_usim0_det_mask, udd->sbi_usim0_det_pos);
		mbox_set_interrupt(MCU_CP, udd->int_usim0_det);
		udd->usim0_det = value;
	}
}

static inline void usim_det_set_det0_init_value(struct usim_det_data *udd)
{
	int value;
	int i;

	for (i = 0; i < USIM_HIGH_DETECT_COUNT; i++) {
		value = gpio_get_value(udd->gpio_usim_det0);
		msleep_interruptible(100);
	}

	pr_err("USIM0_DET INIT: %d\n", value);
	mbox_update_value(MCU_CP, udd->mbx_ap_united_status, value,
		udd->sbi_usim0_det_mask, udd->sbi_usim0_det_pos);
	mbox_set_interrupt(MCU_CP, udd->int_usim0_det);
	udd->usim0_det = value;
}

static void usim_det0_work(struct work_struct *work)
{
	struct usim_det_data *udd =
		container_of(work, struct usim_det_data, usim_det0_work);
	int value;
	int i, flag;

	pr_err("usim_det0_work - GPIO:%d\n", udd->gpio_usim_det0);

	if (udd->usim_init_flag == false) {
		udd->usim_init_flag = true;
		usim_det_set_det0_init_value(udd);
		return;
	}

	value = gpio_get_value(udd->gpio_usim_det0);

	if (value == 0) {
		/* Check HIGH -> LOW */
		flag = 0;
		for (i = 0; i < USIM_LOW_DETECT_COUNT; i++) {
			msleep_interruptible(100);
			value = gpio_get_value(udd->gpio_usim_det0);
			if (value == 0)
				flag++;
			else
				break;
		}
		if (flag == USIM_LOW_DETECT_COUNT) {
			usim_det_set_det0_value(udd, 0);
			pr_err("USIM0_DET: HIGH -> LOW\n");
		}
	} else {
		/* Check LOW -> HIGH */
		flag = 0;
		for (i = 0; i < USIM_HIGH_DETECT_COUNT; i++) {
			msleep_interruptible(100);
			value = gpio_get_value(udd->gpio_usim_det0);
			if (value == 1)
				flag++;
			else
				break;
		}
		if (flag == USIM_HIGH_DETECT_COUNT) {
			usim_det_set_det0_value(udd, 1);
			pr_err("USIM0_DET: LOW -> HIGH\n");
		}
	}
}

#ifdef CONFIG_SHARE_SIM_TRAY_DETECT
void ext_usim_tray_detect(int irq)
{
	struct usim_det_data *udd = g_udd;

	if (unlikely(udd == NULL))
		return;

	if (unlikely(udd->gpio_usim_det0 == USIM_GPIO_UNKNOWN))
		udd->gpio_usim_det0 = irq;

	schedule_work(&udd->usim_det0_work);
}
#else
void ext_usim_tray_detect(int irq)
{
	pr_err("CONFIG_SHARE_SIM_TRAY_DETECT is off : %d\n", value);
}
#endif
EXPORT_SYMBOL(ext_usim_tray_detect)

static int usim_detect_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usim_det_data *udd;
	int err = 0;

	pr_err("%s +++\n", __func__);

	udd = devm_kzalloc(dev, sizeof(struct usim_det_data), GFP_KERNEL);
	if (udd == NULL)
		return -ENOMEM;

	err = of_property_read_u32(dev->of_node, "mif,num_of_usim_det",
			&udd->num_of_usim_det);
	if (err) {
		pr_err("USIM_DET parse error! [num_of_usim_det]\n");
		goto exit_err;
	}

	if (udd->num_of_usim_det == 0 || udd->num_of_usim_det > 2)
		goto exit_err;

	/* USIM0_DET */
	err = of_property_read_u32(dev->of_node,
		"mbx_ap2cp_united_status", &udd->mbx_ap_united_status);
	if (err) {
		pr_err("USIM_DET parse error!\n");
		goto exit_err;
	}

	err = of_property_read_u32(dev->of_node, "mif,int_usim0_det",
			&udd->int_usim0_det);
	if (err) {
		pr_err("USIM_DET parse error!\n");
		goto exit_err;
	}

	err = of_property_read_u32(dev->of_node, "sbi_usim0_det_mask",
			&udd->sbi_usim0_det_mask);
	if (err) {
		pr_err("USIM_DET DTS parse error!\n");
		goto exit_err;
	}

	err = of_property_read_u32(dev->of_node, "sbi_usim0_det_pos",
			&udd->sbi_usim0_det_pos);
	if (err) {
		pr_err("USIM_DET DTS parse error!\n");
		goto exit_err;
	}

	INIT_WORK(&udd->usim_det0_work, usim_det0_work);
	platform_set_drvdata(pdev, udd);

	udd->gpio_usim_det0 = USIM_GPIO_UNKNOWN;
	udd->usim_init_flag = false;
	g_udd = udd;

	pr_err("%s ---\n", __func__);

	return 0;

exit_err:
	pr_err("%s: ERROR\n", __func__);
	devm_kfree(dev, udd);
	return -EINVAL;
}

static int __exit usim_detect_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usim_det_data *udd = dev_get_drvdata(dev);

	devm_kfree(dev, udd);

	return 0;
}

#ifdef CONFIG_PM
static int usim_detect_suspend(struct device *dev)
{
	return 0;
}

static int usim_detect_resume(struct device *dev)
{
	return 0;
}
#else
#define usim_detect_suspend NULL
#define usim_detect_resume NULL
#endif

static const struct dev_pm_ops usim_detect_pm_ops = {
	.suspend = usim_detect_suspend,
	.resume = usim_detect_resume,
};

static const struct of_device_id exynos_usim_detect_dt_match[] = {
		{ .compatible = "samsung,exynos-usim-detect", },
		{},
};
MODULE_DEVICE_TABLE(of, exynos_uart_sel_dt_match);

static struct platform_driver usim_detect_driver = {
	.probe		= usim_detect_probe,
	.remove		= usim_detect_remove,
	.driver		= {
		.name = "usim_detect",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(exynos_usim_detect_dt_match),
		.pm = &usim_detect_pm_ops,
	},
};
module_platform_driver(usim_detect_driver);

MODULE_DESCRIPTION("USIM_DETECT driver");
MODULE_AUTHOR("<tj7.kim@samsung.com>");
MODULE_LICENSE("GPL");
