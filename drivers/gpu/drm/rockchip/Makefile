# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

rockchipdrm-y := rockchip_drm_drv.o rockchip_drm_fb.o \
		rockchip_drm_gem.o rockchip_drm_vop.o rockchip_vop_reg.o
rockchipdrm-$(CPTCFG_DRM_FBDEV_EMULATION) += rockchip_drm_fbdev.o

rockchipdrm-$(CPTCFG_ROCKCHIP_ANALOGIX_DP) += analogix_dp-rockchip.o
rockchipdrm-$(CPTCFG_ROCKCHIP_CDN_DP) += cdn-dp-core.o cdn-dp-reg.o
rockchipdrm-$(CPTCFG_ROCKCHIP_DW_HDMI) += dw_hdmi-rockchip.o
rockchipdrm-$(CPTCFG_ROCKCHIP_DW_MIPI_DSI) += dw-mipi-dsi-rockchip.o
rockchipdrm-$(CPTCFG_ROCKCHIP_INNO_HDMI) += inno_hdmi.o
rockchipdrm-$(CPTCFG_ROCKCHIP_LVDS) += rockchip_lvds.o
rockchipdrm-$(CPTCFG_ROCKCHIP_RGB) += rockchip_rgb.o
rockchipdrm-$(CPTCFG_ROCKCHIP_RK3066_HDMI) += rk3066_hdmi.o

obj-$(CPTCFG_DRM_ROCKCHIP) += rockchipdrm.o
