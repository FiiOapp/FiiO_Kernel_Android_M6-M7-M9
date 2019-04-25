/* drivers/video/exynos/panels/s6e3fa0_mipi_lcd.c
 *
 * Samsung SoC MIPI LCD driver.
 *
 * Copyright (c) 2014 Samsung Electronics
 *
 * Haowei Li, <haowei.li@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/backlight.h>
#include <video/mipi_display.h>
#include <linux/platform_device.h>

#include "s6e3fa0_param.h"
#include "lcd_ctrl.h"
#include "decon_lcd.h"
#include "../dsim.h"

#define MAX_BRIGHTNESS		255
/* set the minimum brightness value to see the screen */
#define MIN_BRIGHTNESS		0
#define DEFAULT_BRIGHTNESS	200
#define CMD_SIZE		34

static int s6e3fa0_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int update_brightness(struct dsim_device *dsim, int brightness)
{
	unsigned int real_brightness = brightness / 2;
	unsigned int id = dsim->id;
	unsigned char gamma_control[CMD_SIZE];
	unsigned char gamma_update[] = { 0xF7, 0x01 };

	memcpy(gamma_control, SEQ_GAMMA_CONTROL_SET_300CD, CMD_SIZE);

	gamma_control[1] = 0;
	gamma_control[3] = 0;
	gamma_control[5] = 0;

	gamma_control[2] = real_brightness * 2;
	gamma_control[4] = real_brightness * 2;
	gamma_control[6] = real_brightness * 2;

	gamma_control[28] = real_brightness;
	gamma_control[29] = real_brightness;
	gamma_control[30] = real_brightness;

	if (dsim_wr_data(id, MIPI_DSI_DCS_LONG_WRITE, (unsigned long)gamma_control,
				ARRAY_SIZE(gamma_control)) < 0)
		dsim_err("fail to send gamma_control command.\n");

	if (dsim_wr_data(id, MIPI_DSI_DCS_LONG_WRITE, (unsigned long)SEQ_GAMMA_UPDATE,
				ARRAY_SIZE(SEQ_GAMMA_UPDATE)) < 0)
		dsim_err("fail to send SEQ_GAMMA_UPDATE command.\n");

	if (dsim_wr_data(id, MIPI_DSI_DCS_LONG_WRITE, (unsigned long)gamma_update,
				ARRAY_SIZE(gamma_update)) < 0)
		dsim_err("fail to send gamma_update command.\n");

	return 0;
}

static int s6e3fa0_set_brightness(struct backlight_device *bd)
{
	struct dsim_device *dsim;
	int brightness = bd->props.brightness;

	dsim = get_dsim_drvdata(0);

	if (brightness < MIN_BRIGHTNESS || brightness > MAX_BRIGHTNESS) {
		printk(KERN_ALERT "Brightness should be in the range of 0 ~ 255\n");
		return -EINVAL;
	}

	update_brightness(dsim, brightness);

	return 1;
}

static const struct backlight_ops s6e3fa0_backlight_ops = {
	.get_brightness = s6e3fa0_get_brightness,
	.update_status = s6e3fa0_set_brightness,
};

static int s6e3fa0_probe(struct dsim_device *dsim)
{
	struct backlight_device *bd;

	bd = backlight_device_register("panel", dsim->dev,
		NULL, &s6e3fa0_backlight_ops, NULL);

	bd->props.max_brightness = MAX_BRIGHTNESS;
	bd->props.brightness = DEFAULT_BRIGHTNESS;

	return 1;
}

static int s6e3fa0_displayon(struct dsim_device *dsim)
{
	lcd_init(dsim->id, &dsim->lcd_info);
	lcd_enable(dsim->id);
	return 1;
}

static int s6e3fa0_suspend(struct dsim_device *dsim)
{
	lcd_disable(dsim->id);
	return 1;
}

static int s6e3fa0_resume(struct dsim_device *dsim)
{
	return 1;
}

struct dsim_lcd_driver s6e3fa0_mipi_lcd_driver = {
	.probe		= s6e3fa0_probe,
	.displayon	= s6e3fa0_displayon,
	.suspend	= s6e3fa0_suspend,
	.resume		= s6e3fa0_resume,
};
