/* linux/drivers/video/fbdev/exynos/dpu/dqe_reg_8895.c
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "regs-dqe.h"
#include "decon.h"
#include "dqe.h"

/* DQE_CON register set */
void dqe_reg_en_gamma(u32 en)
{
	dqe_write_mask(DQECON, en << DQE_GAMMA_ON, DQE_GAMMA_ON_MASK);
}

void dqe_reg_en_gammagray(u32 en)
{
	dqe_write_mask(DQECON, en << DQE_GAMMAGRAY_UPDATE,
				DQE_GAMMAGRAY_UPDATE_MASK);
}
#if 0
void dqe_reg_module_on_off(bool en_she, bool en_cgc, bool en_gamma,
			bool en_hsc, bool en_aps)
{
	dqe_reg_en_gamma(en_gamma);
}
void dqe_reg_module_reset(bool en_hsc, bool en_aps, bool en_rst)
{
}
#endif
/* DQE_IMG register set
 * 	- img_size0   : DECON
 * 	- img_size1,2 : DPP logic sharing
 */
void dqe_reg_set_img_size0(u32 width, u32 height)
{
	u32 data;

	data = DQE_IMG_VSIZE(height) | DQE_IMG_HSIZE(width);
	dqe_write(DQEIMG_SIZESET, data);
}

/* DQE_GAMMA register set */
void dqe_reg_set_gamma(u32 offset, u32 mask, u32 val)
{
	dqe_write_mask((DQEGAMMALUT_X_Y_BASE + offset), val, mask);
}

void dqe_reg_start(u32 decon_id, u32 width, u32 height)
{
	enum decon_data_path data_path;
	enum decon_enhance_path enhance_path;

	/* DQE located at decon 0 */
	if (decon_id != 0)
		return;

	decon_reg_get_data_path(decon_id, &data_path, &enhance_path);
	decon_reg_set_data_path(decon_id, data_path, ENHANCEPATH_DQE_ON);

	dqe_reg_set_img_size0(width, height);

	decon_reg_update_req_dqe(decon_id);
	decon_reg_update_req_global(decon_id);
}

void dqe_reg_stop(u32 decon_id)
{
	enum decon_data_path data_path;
	enum decon_enhance_path enhance_path;

	/* DQE located at decon 0 */
	if (decon_id != 0)
		return;

	decon_reg_get_data_path(decon_id, &data_path, &enhance_path);
	decon_reg_set_data_path(decon_id, data_path, ENHANCEPATH_ENHANCE_ALL_OFF);
	decon_reg_update_req_global(decon_id);
}

/* LPD ctrl : TODO */
/* partial update(HSC, APS) : TODO */
