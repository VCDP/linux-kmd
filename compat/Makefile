ccflags-y += -I$(src)
ifeq ($(CONFIG_BACKPORT_INTEGRATE),)
obj-m += drm_ukmd_compat.o
else
obj-y += drm_ukmd_compat.o
endif
drm_ukmd_compat-y += main.o

drm_ukmd_compat-y += ../drivers/dma-buf/dma-resv.o ../drivers/dma-buf/dma-fence.o ../drivers/dma-buf/seqno-fence.o ../drivers/dma-buf/sync_file.o ../drivers/dma-buf/dma-buf.o ../drivers/dma-buf/dma-fence-array.o ../drivers/dma-buf/dma-fence-chain.o xarray.o  hdmi.o vmscan.o drm_mipi_dsi.o
drm_ukmd_compat-$(CPTCFG_KERNEL_5_0) += backport-5.0.o
