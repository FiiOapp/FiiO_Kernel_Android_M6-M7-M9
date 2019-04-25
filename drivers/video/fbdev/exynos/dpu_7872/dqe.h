/* linux/drivers/video/fbdev/exynos/dpu/dqe_common.h
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SAMSUNG_DQE_COMMON_H__
#define __SAMSUNG_DQE_COMMON_H__

#include "decon.h"
#include "regs-dqe.h"

extern const u32 gamma_table[][3][65];
extern const u32 cgc_table[][8][3];
extern const u32 she_table[][11];
extern const u32 hsc_table[][35];
extern const u32 aps_table[][18];

static inline u32 dqe_read(u32 reg_id)
{
	struct decon_device *decon = get_decon_drvdata(0);
	return readl(decon->res.regs + DQE_BASE + reg_id);
}

static inline u32 dqe_read_mask(u32 reg_id, u32 mask)
{
	u32 val = dqe_read(reg_id);
	val &= (mask);
	return val;
}

static inline void dqe_write(u32 reg_id, u32 val)
{
	struct decon_device *decon = get_decon_drvdata(0);
	writel(val, decon->res.regs + DQE_BASE + reg_id);
}

static inline void dqe_write_mask(u32 reg_id, u32 val, u32 mask)
{
	struct decon_device *decon = get_decon_drvdata(0);
	u32 old = dqe_read(reg_id);

	val = (val & mask) | (old & ~mask);
	writel(val, decon->res.regs + DQE_BASE + reg_id);
}

/* CAL APIs list */
void dqe_reg_module_on_off(bool en_she, bool en_cgc, bool en_gamma,
		            bool en_hsc, bool en_aps);
void dqe_reg_module_reset(bool en_hsc, bool en_aps, bool en_rst);
void dqe_reg_start(u32 decon_id, u32 width, u32 height);
void dqe_reg_stop(u32 decon_id);

void dqe_reg_en_she(u32 en);
void dqe_reg_en_cgc(u32 en);
void dqe_reg_en_gamma(u32 en);
void dqe_reg_en_hsc(u32 en);
void dqe_reg_en_aps(u32 en);
void dqe_reg_hsc_sw_reset(u32 en);
void dqe_reg_aps_sw_reset(u32 en);
void dqe_reg_reset(u32 en);
void dqe_reg_en_gammagray(u32 en);
void dqe_reg_lpd_mode_exit(u32 en);

void dqe_reg_module_on_off(bool en_she, bool en_cgc, bool en_gamma,
		            bool en_hsc, bool en_aps);
void dqe_reg_module_reset(bool en_hsc, bool en_aps, bool en_rst);

void dqe_reg_set_img_size0(u32 width, u32 height);
void dqe_reg_set_img_size1(u32 width, u32 height);
void dqe_reg_set_img_size2(u32 width, u32 height);

/* DQE_GAMMA register set */
void dqe_reg_set_gamma(u32 offset, u32 mask, u32 val);

/* DQE_HSC register set */
void dqe_reg_set_hsc_ppsc_on(u32 en);
void dqe_reg_set_hsc_ycomp_on(u32 en);
void dqe_reg_set_hsc_tsc_on(u32 en);
void dqe_reg_set_hsc_dither_on(u32 en);
void dqe_reg_set_hsc_pphc_on(u32 en);
void dqe_reg_set_hsc_skin_on(u32 en);
void dqe_reg_set_hsc_ppscgain_rgb(u32 r, u32 g, u32 b);
void dqe_reg_set_hsc_ppsc_gain_cmy(u32 c, u32 m, u32 y);
void dqe_reg_set_hsc_alphascale_shift(u32 alpha_shift1, u32 alpha_shift2,
		                            u32 alpha_scale);
void dqe_reg_set_hsc_poly_curve0(u32 curve1, u32 curve2, u32 curve3, u32 curve4);
void dqe_reg_set_hsc_poly_curve1(u32 curve5, u32 curve6, u32 curve7, u32 curve8);
void dqe_reg_set_hsc_skin(u32 skin_h1, u32 skin_h2, u32 skin_s1, u32 skin_s2);
void dqe_reg_set_hsc_pphcgain_rgb(u32 r, u32 g, u32 b);
void dqe_reg_set_hsc_pphcgain_cmy(u32 c, u32 m, u32 y);
void dqe_reg_set_hsc_tsc_ycomp(u32 ratio, u32 gain);

int decon_create_dqe_interface(struct decon_device *decon);

int dqe_save_context(void);
int dqe_restore_context(void);

#endif
