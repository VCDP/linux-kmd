# SPDX-License-Identifier: GPL-2.0

# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

drm_ukmd-y       :=	drm_auth.o drm_cache.o \
		drm_file.o drm_gem.o drm_ioctl.o drm_irq.o \
		drm_memory.o drm_drv.o \
		drm_sysfs.o drm_hashtab.o drm_mm.o \
		drm_crtc.o drm_fourcc.o drm_modes.o drm_edid.o \
		drm_encoder_slave.o \
		drm_trace_points.o drm_prime.o \
		drm_rect.o drm_vma_manager.o drm_flip_work.o \
		drm_modeset_lock.o drm_atomic.o drm_bridge.o \
		drm_framebuffer.o drm_connector.o drm_blend.o \
		drm_encoder.o drm_mode_object.o drm_property.o \
		drm_plane.o drm_color_mgmt.o drm_print.o \
		drm_dumb_buffers.o drm_mode_config.o drm_vblank.o \
		drm_syncobj.o drm_lease.o drm_writeback.o drm_client.o \
		drm_client_modeset.o drm_atomic_uapi.o drm_hdcp.o

drm_ukmd-$(CPTCFG_DRM_LEGACY) += drm_legacy_misc.o drm_bufs.o drm_context.o drm_dma.o drm_scatter.o drm_lock.o
drm_ukmd-$(CPTCFG_DRM_LIB_RANDOM) += lib/drm_random.o
drm_ukmd-$(CPTCFG_DRM_VM) += drm_vm.o
drm_ukmd-$(CONFIG_COMPAT) += drm_ioc32.o
drm_ukmd-$(CPTCFG_DRM_GEM_CMA_HELPER) += drm_gem_cma_helper.o
drm_ukmd-$(CPTCFG_DRM_GEM_SHMEM_HELPER) += drm_gem_shmem_helper.o
drm_ukmd-$(CPTCFG_DRM_PANEL) += drm_panel.o
drm_ukmd-$(CONFIG_OF) += drm_of.o
drm_ukmd-$(CONFIG_AGP) += drm_agpsupport.o
drm_ukmd-$(CONFIG_PCI) += drm_pci.o
drm_ukmd-$(CONFIG_DEBUG_FS) += drm_debugfs.o drm_debugfs_crc.o
drm_ukmd-$(CPTCFG_DRM_LOAD_EDID_FIRMWARE) += drm_edid_load.o

drm_vram_helper-y := drm_gem_vram_helper.o \
		     drm_vram_helper_common.o
obj-$(CPTCFG_DRM_VRAM_HELPER) += drm_vram_helper.o

drm_ttm_helper-y := drm_gem_ttm_helper.o
obj-$(CPTCFG_DRM_TTM_HELPER) += drm_ttm_helper.o

drm_ukmd_kms_helper-y := drm_crtc_helper.o drm_dp_helper.o drm_dsc.o drm_probe_helper.o \
		drm_plane_helper.o drm_dp_mst_topology.o drm_atomic_helper.o \
		drm_kms_helper_common.o drm_dp_dual_mode_helper.o \
		drm_simple_kms_helper.o drm_modeset_helper.o \
		drm_scdc_helper.o drm_gem_framebuffer_helper.o \
		drm_atomic_state_helper.o drm_damage_helper.o \
		drm_format_helper.o drm_self_refresh_helper.o

drm_ukmd_kms_helper-$(CPTCFG_DRM_PANEL_BRIDGE) += bridge/panel.o
drm_ukmd_kms_helper-$(CPTCFG_DRM_FBDEV_EMULATION) += drm_fb_helper.o
drm_ukmd_kms_helper-$(CPTCFG_DRM_KMS_CMA_HELPER) += drm_fb_cma_helper.o
drm_ukmd_kms_helper-$(CPTCFG_DRM_DP_AUX_CHARDEV) += drm_dp_aux_dev.o
drm_ukmd_kms_helper-$(CPTCFG_DRM_DP_CEC) += drm_dp_cec.o

obj-$(CPTCFG_DRM_KMS_HELPER) += drm_ukmd_kms_helper.o
obj-$(CPTCFG_DRM_DEBUG_SELFTEST) += selftests/

obj-$(CPTCFG_DRM)	+= drm_ukmd.o
obj-$(CPTCFG_DRM_MIPI_DBI) += drm_mipi_dbi.o
obj-$(CPTCFG_DRM_MIPI_DSI) += drm_mipi_dsi.o
drm_ukmd_panel_orientation_quirks-$(CPTCFG_DRM_PANEL_ORIENTATION_QUIRKS) += drm_panel_orientation_quirks.o
obj-$(CPTCFG_DRM_PANEL_ORIENTATION_QUIRKS) += drm_ukmd_panel_orientation_quirks.o
obj-y			+= arm/
obj-$(CPTCFG_DRM_TTM)	+= ttm/
obj-$(CPTCFG_DRM_SCHED)	+= scheduler/
obj-$(CPTCFG_DRM_TDFX)	+= tdfx/
obj-$(CPTCFG_DRM_R128)	+= r128/
obj-$(CPTCFG_DRM_RADEON)+= radeon/
obj-$(CPTCFG_DRM_AMDGPU)+= amd/amdgpu/
obj-$(CPTCFG_DRM_MGA)	+= mga/
obj-$(CPTCFG_DRM_I810)	+= i810/
obj-$(CPTCFG_DRM_I915)	+= i915/
obj-$(CPTCFG_DRM_MGAG200) += mgag200/
obj-$(CPTCFG_DRM_V3D)  += v3d/
obj-$(CPTCFG_DRM_VC4)  += vc4/
obj-$(CPTCFG_DRM_CIRRUS_QEMU) += cirrus/
obj-$(CPTCFG_DRM_SIS)   += sis/
obj-$(CPTCFG_DRM_SAVAGE)+= savage/
obj-$(CPTCFG_DRM_VMWGFX)+= vmwgfx/
obj-$(CPTCFG_DRM_VIA)	+=via/
obj-$(CPTCFG_DRM_VGEM)	+= vgem/
obj-$(CPTCFG_DRM_VKMS)	+= vkms/
#obj-$(CPTCFG_DRM_NOUVEAU +=nouveau/
obj-$(CPTCFG_DRM_EXYNOS) +=exynos/
obj-$(CPTCFG_DRM_ROCKCHIP) +=rockchip/
obj-$(CPTCFG_DRM_GMA500) += gma500/
obj-$(CPTCFG_DRM_UDL) += udl/
obj-$(CPTCFG_DRM_AST) += ast/
obj-$(CPTCFG_DRM_ARMADA) += armada/
obj-$(CPTCFG_DRM_ATMEL_HLCDC)	+= atmel-hlcdc/
obj-y			+= rcar-du/
obj-$(CPTCFG_DRM_SHMOBILE) +=shmobile/
obj-y			+= omapdrm/
obj-$(CPTCFG_DRM_SUN4I) += sun4i/
obj-y			+= tilcdc/
obj-$(CPTCFG_DRM_QXL) += qxl/
obj-$(CPTCFG_DRM_BOCHS) += bochs/
obj-$(CPTCFG_DRM_VIRTIO_GPU) += virtio/
obj-$(CPTCFG_DRM_MSM) += msm/
obj-$(CPTCFG_DRM_TEGRA) += tegra/
obj-$(CPTCFG_DRM_STM) += stm/
obj-$(CPTCFG_DRM_STI) += sti/
obj-$(CPTCFG_DRM_IMX) += imx/
obj-$(CPTCFG_DRM_INGENIC) += ingenic/
obj-$(CPTCFG_DRM_MEDIATEK) += mediatek/
obj-$(CPTCFG_DRM_MESON)	+= meson/
obj-y			+= i2c/
obj-y			+= panel/
obj-y			+= bridge/
obj-$(CPTCFG_DRM_FSL_DCU) += fsl-dcu/
obj-$(CPTCFG_DRM_ETNAVIV) += etnaviv/
obj-$(CPTCFG_DRM_ARCPGU)+= arc/
obj-y			+= hisilicon/
obj-$(CPTCFG_DRM_ZTE)	+= zte/
obj-$(CPTCFG_DRM_MXSFB)	+= mxsfb/
obj-y			+= tiny/
obj-$(CPTCFG_DRM_PL111) += pl111/
obj-$(CPTCFG_DRM_TVE200) += tve200/
obj-$(CPTCFG_DRM_XEN) += xen/
obj-$(CPTCFG_DRM_VBOXVIDEO) += vboxvideo/
obj-$(CPTCFG_DRM_LIMA)  += lima/
obj-$(CPTCFG_DRM_PANFROST) += panfrost/
obj-$(CPTCFG_DRM_ASPEED_GFX) += aspeed/
obj-$(CPTCFG_DRM_MCDE) += mcde/
