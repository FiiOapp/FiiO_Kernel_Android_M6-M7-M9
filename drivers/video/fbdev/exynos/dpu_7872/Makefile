#
# Copyright (c) 2013 Samsung Electronics Co., Ltd.
#		http://www.samsung.com
#
# Licensed under GPLv2
#

obj-$(CONFIG_EXYNOS_DPP) += dpp.o
dpp-y := dpp_drv.o
obj-$(CONFIG_EXYNOS_MIPI_DSIM) += dsim.o
dsim-y += dsim_drv.o
obj-$(CONFIG_SOC_EXYNOS7872) += dsim_reg.o dpp_reg.o decon_reg.o
obj-$(CONFIG_EXYNOS_DECON_7872) += decon.o
obj-$(CONFIG_DECON_EVENT_LOG) += event_log.o
decon-y += decon_core.o decon_dsi.o helper.o win_update.o bts.o
obj-y += panels/
obj-$(CONFIG_EXYNOS_DECON_DQE) += dqe_reg.o dqe_drv.o
