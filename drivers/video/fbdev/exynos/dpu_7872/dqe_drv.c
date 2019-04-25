/*
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * Samsung EXYNOS8 SoC series DPP driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/stat.h>
#include <linux/device.h>
#include <linux/fs.h>

#include "dqe.h"
#include "regs-dqe.h"
#include "decon.h"

struct dqe_reg_dump {
	unsigned int addr;
	unsigned int val;
};

static struct dqe_saved_ctx {
	unsigned int gamma_en;
	struct dqe_reg_dump dqe_gamma_dump[DQEGAMMALUT_MAX];
} dqe_saved_ctx;

static int dqe_init_saved_ctx(void)
{
	int i;

	for (i = 0; i < DQEGAMMALUT_MAX; i++) {
		dqe_saved_ctx.dqe_gamma_dump[i].addr = DQEGAMMALUT_X_Y_BASE + (i * 4);
		dqe_saved_ctx.dqe_gamma_dump[i].val = dqe_read(dqe_saved_ctx.dqe_gamma_dump[i].addr);
	}

	dqe_saved_ctx.gamma_en = 0;

	return 0;
}

int dqe_save_context(void)
{
	int i;

	for (i = 0; i < DQEGAMMALUT_MAX; i++)
		dqe_saved_ctx.dqe_gamma_dump[i].val = dqe_read(dqe_saved_ctx.dqe_gamma_dump[i].addr);

	dqe_saved_ctx.gamma_en = dqe_read_mask(DQECON, DQE_GAMMA_ON_MASK);

	return 0;
}

int dqe_restore_context(void)
{
	int i;

	for (i = 0; i < DQEGAMMALUT_MAX; i++)
		dqe_write(dqe_saved_ctx.dqe_gamma_dump[i].addr, dqe_saved_ctx.dqe_gamma_dump[i].val);

	if (dqe_saved_ctx.gamma_en)
		dqe_write_mask(DQECON, ~0, DQE_GAMMA_ON_MASK);

	return 0;
}

#if 0	/* enabling gamma is included in gamma_chunk function, This is not necessary */
static ssize_t decon_dqe_gamma_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	int ret = 0;
	int val;
	u32 length = 0;
	const char *str_format = NULL;
	const char *str_num = NULL;

	struct decon_device *decon = get_decon_drvdata(0);

	if (count > 32 || count == 0)
		return -1;

	if (decon) {
		if (decon->state != DECON_STATE_ON) {
			decon_err("decon is NULL!\n");
			ret = -1;
			goto gamma_err;
		}
	} else {
		decon_err("decon is NULL!\n");
		ret = -1;
		goto gamma_err;
	}

	if (buffer) {
		str_format = buffer;
		str_num = buffer + 2;
		length = count - 2;

		switch (str_format[0]) {
			case 'O':
				if (kstrtoint(str_num, 16, &val)) {
					decon_err("copy_from_user() failed!\n");
					ret = -EFAULT;
					goto gamma_err;
				}
				dqe_reg_en_gamma(val);
				decon_reg_update_req_dqe(decon->id);
				goto gamma_done;
			default:
				break;
		}
	} else {
		ret = -1;
		goto gamma_err;
	}

gamma_done:
	decon_info("%s : gamma_done\n", __func__);

	return count;
gamma_err:
	decon_err("%s : gamma_err\n", __func__);

	return ret;
}
#endif

static int gamma_lut_reg_set(u32 (*gamma_lut)[65])
{
	int i, j;
	u32 mask = 0, offset_in = 0, offset_ex = 0, gamma = 0;

	for (j = 0; j < 3; j++) {
		for (i = 0; i < 65; i++) {
			if ((i % 2) == 0) {
				if (i >= 2)
					offset_in += 4;
				mask = DQEGAMMALUT_Y_MASK;
				gamma = DQEGAMMALUT_Y(gamma_lut[j][i]);
			} else {
				mask = DQEGAMMALUT_X_MASK;
				gamma = DQEGAMMALUT_X(gamma_lut[j][i]);
			}
			dqe_reg_set_gamma(offset_in + offset_ex, mask, gamma);
			mask = 0;
			gamma = 0;
			if (i == 64)
				offset_in = 0;
		}
		offset_ex += DQEGAMMA_OFFSET;
	}

	return 0;
}

static ssize_t decon_dqe_gamma_chunk_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	int ret = 0;
	u32 gamma_lut[3][65];
	int i, j;
	char *head = NULL, *ptr = NULL;

	struct decon_device *decon = get_decon_drvdata(0);

	if (count <= 0) {
		decon_err("gamma chunk write count error\n");
		ret = -1;
		goto gamma_err;
	}

	if (decon) {
		if (decon->state != DECON_STATE_ON) {
			decon_err(" decon is not enabled!\n");
			ret = -1;
			goto gamma_err;
		}
	} else {
		decon_err(" decon is NULL!\n");
		ret = -1;
		goto gamma_err;
	}
	head = (char *)buffer;
	if  (*head != 0) {
		for (i = 0; i < 3; i++) {
			for (j = 0; j < 65; j++) {
				ptr = strchr(head, ',');
				if (ptr == NULL){
					ret = -EFAULT;
					goto gamma_err;
				}
				*ptr = 0;
				ret = kstrtou32(head, 0, &gamma_lut[i][j]);
				if (ret) {
					ret = -EFAULT;
					goto gamma_err;
				}

				head = ptr + 1;
			}
		}
	}
	for (i = 0; i < 3; i++) {
		printk("%s : ", __func__);
		for (j = 0; j < 65; j++)
			printk("%d ", gamma_lut[i][j]);
		printk("\n");
	}

	gamma_lut_reg_set(gamma_lut);
	dqe_reg_en_gamma(1);
	decon_reg_update_req_dqe(decon->id);
	return count;

gamma_err:
	decon_err("%s : gamma_err\n", __func__);
	return ret;
}

#if 0	/* enabling gamma is included in gamma_chunk function, This is not necessary */
static DEVICE_ATTR(gamma, S_IWUSR, NULL, decon_dqe_gamma_store);
#endif
static DEVICE_ATTR(gamma_chunk, S_IWUSR, NULL, decon_dqe_gamma_chunk_store);

int decon_create_dqe_interface(struct decon_device *decon)
{
	int ret = 0;

#if 0	/* enabling gamma is included in gamma_chunk function, This is not necessary */
 	device_create_file(decon->dev, &dev_attr_gamma);
#endif
	device_create_file(decon->dev, &dev_attr_gamma_chunk);

	dqe_init_saved_ctx();

	return ret;
}
