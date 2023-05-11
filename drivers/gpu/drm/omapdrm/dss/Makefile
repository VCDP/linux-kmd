# SPDX-License-Identifier: GPL-2.0
obj-$(CPTCFG_OMAP2_DSS_INIT) += omapdss-boot-init.o

obj-$(CPTCFG_OMAP_DSS_BASE) += omapdss-base.o
omapdss-base-y := base.o display.o dss-of.o output.o

obj-$(CPTCFG_OMAP2_DSS) += omapdss.o
# Core DSS files
omapdss-y := dss.o dispc.o dispc_coefs.o \
	pll.o video-pll.o
omapdss-$(CPTCFG_OMAP2_DSS_DPI) += dpi.o
omapdss-$(CPTCFG_OMAP2_DSS_VENC) += venc.o
omapdss-$(CPTCFG_OMAP2_DSS_SDI) += sdi.o
omapdss-$(CPTCFG_OMAP2_DSS_DSI) += dsi.o
omapdss-$(CPTCFG_OMAP2_DSS_HDMI_COMMON) += hdmi_common.o hdmi_wp.o hdmi_pll.o \
	hdmi_phy.o
omapdss-$(CPTCFG_OMAP4_DSS_HDMI) += hdmi4.o hdmi4_core.o
omapdss-$(CPTCFG_OMAP4_DSS_HDMI_CEC) += hdmi4_cec.o
omapdss-$(CPTCFG_OMAP5_DSS_HDMI) += hdmi5.o hdmi5_core.o
ccflags-$(CPTCFG_OMAP2_DSS_DEBUG) += -DDEBUG
