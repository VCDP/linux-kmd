// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_params.h"

static const char * const presi_mode_names[] = {
	[I915_PRESI_MODE_UNKNOWN] = "unknown",
	[I915_PRESI_MODE_NONE] = "none (silicon)",
	[I915_PRESI_MODE_SIMULATOR] = "simulation",
	[I915_PRESI_MODE_EMULATOR_PIPEGT] = "emulation pipeGT",
	[I915_PRESI_MODE_EMULATOR_PIPE2D] = "emulation pipe2D",
	[I915_PRESI_MODE_EMULATOR_MEDIA_SUPERPIPE] = "emulation media superpipe",
};

/**
 * intel_detect_presi_mode - autodetect the pre-si mode (silicon, sim or emu)
 * @i915:	i915 device
 *
 * This function autodetects the pre-silicon mode (sim or emu) based on the
 * information provided by HAS status register:
 *  - bit 31 indicates that we're in a HAS pre-si environment
 *  - bit 29 tells us if we're in emulation
 *  - the lower 4 bits of the status indicate HAS_MODE. BIT(3), which indicates
 *    the VM mode used in HAS3+, is always set for our usecases, so we can
 *    ignore it. The lower 3 bits can be set to a value via the simics start
 *    script, following a table of recommended setting. The only one we're
 *    currently interested in is HASMODE_PIPE2D (5)
 *
 * The offset is unused on real silicon, so no risk of mismatching the data
 */
#define HAS_IS_ACTIVE BIT(31)
#define EMU_IS_ACTIVE BIT(29)
#define HAS_MODE_MASK 0x7
#define HAS_MODE_PIPE2D 5
#define HAS_STATUS_VALID(status) (((status) != ~0U) && ((status) & HAS_IS_ACTIVE))
#define HAS_IS_PIPE2D(status) (((status) & HAS_MODE_MASK) == HAS_MODE_PIPE2D)
static enum intel_presi_mode
intel_detect_presi_mode(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;
	const u32 has_status_reg = 0x180008;
	void __iomem *has_status_reg_map;
	u32 has_status;

	/* we do not support any pre-gen11 pre-si environments */
	if (INTEL_GEN(i915) < 11)
		return I915_PRESI_MODE_NONE;

	has_status_reg_map = pci_iomap_range(pdev, 0, has_status_reg, sizeof(u32));
	has_status = readl(has_status_reg_map);
	pci_iounmap(pdev, has_status_reg_map);

	/* If we're not on HAS we're done... */
	if (!HAS_STATUS_VALID(has_status))
		return I915_PRESI_MODE_NONE;

	/* ... while if we're on HAS we need to detect the presi mode */
	if (!(has_status & EMU_IS_ACTIVE))
		return I915_PRESI_MODE_SIMULATOR;
	else if (HAS_IS_PIPE2D(has_status))
		return I915_PRESI_MODE_EMULATOR_PIPE2D;
	else
		return I915_PRESI_MODE_EMULATOR_PIPEGT;
}

/**
 * intel_presi_init - checks the pre-si modparam and acts on it
 * @i915:	i915 device
 *
 * presi_mode is only updated if the modparam is set to a valid value. An
 * error is logged if the modparam is set incorrectly
 */
void intel_presi_init(struct drm_i915_private *i915)
{
	int mode = MODPARAM_TO_PRESI_MODE(i915_modparams.presi_mode);

	BUILD_BUG_ON(I915_PRESI_MODE_UNKNOWN); /* unknown needs to be 0 */
	BUILD_BUG_ON(I915_PRESI_MODE_AUTODETECT >= 0);
	GEM_BUG_ON(i915->presi_info.mode != I915_PRESI_MODE_UNKNOWN);

	if (mode >= I915_PRESI_MODE_NONE && mode <= I915_MAX_PRESI_MODE) {
		DRM_DEBUG_DRIVER("using pre-silicon mode from modparam: %s\n",
				 presi_mode_names[mode]);
		i915->presi_info.mode = mode;
	} else if (mode != I915_PRESI_MODE_AUTODETECT) {
		DRM_ERROR("invalid pre-silicon mode %d selected in "
			  "modparam! defaulting to silicon mode\n",
			  i915_modparams.presi_mode);
		i915->presi_info.mode = I915_PRESI_MODE_NONE;
	} else {
		i915->presi_info.mode = intel_detect_presi_mode(i915);
		DRM_DEBUG_DRIVER("auto-detected pre-si mode: %s\n",
				 presi_mode_names[i915->presi_info.mode]);
	}
}

/**
 * intel_presi_flush_memory - Force pre-si memory flush
 * @i915:	i915 device
 *
 * Pre-si env might cache memory and other components might see stale values.
 * Pre-si triggers memory flush on some scenarios (like TLB modifications),
 * but sometimes this is not sufficient.
 *
 * Use this function to force memory flush (artificial side-effect of read
 * of the VF_CAP register).
 */
void intel_presi_flush_memory(struct drm_i915_private *i915)
{
	const i915_reg_t vfcap = _MMIO(0x1901f8);
	intel_wakeref_t wakeref;

	if (IS_SIMULATOR(i915))
		with_intel_runtime_pm(&i915->runtime_pm, wakeref)
			intel_uncore_read(&i915->uncore, vfcap);
}

/*
 * Marks a physical page to be model-owned, which means that access to this
 * page triggers callback on HAS side and thanks to it we can inform fulsim that
 * something was changed and it may need to be processed.
 * The communication is done by writing a 64 bit value to mmio offset 0x180000,
 * which is unused in real HW. HAS will trap writes to this offset and use the
 * value internally according to the following encoding:
 *
 * Bit 0: 0 – delete mop page; 1 – add mop page
 * Bits 1-11: reserved
 * Bits 12-63: page number
 */
int intel_mark_page_as_mop(struct drm_i915_private *dev_priv,
			   u64 address, bool owned)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	const i915_reg_t mop_reg = _MMIO(0x180000);

	// The cake is a lie!
	GEM_BUG_ON(!IS_PRESILICON(dev_priv));
	GEM_BUG_ON(address & (PAGE_SIZE - 1));

	if (owned)
		address |= 1;

	intel_uncore_write64_fw(uncore, mop_reg, address);

	return 0;
}

int intel_mark_all_pages_as_mop(struct drm_i915_private *dev_priv,
				struct sg_table *pages, bool owned)
{
	struct sg_dma_page_iter sg_iter;
	int ret;

	for_each_sg_dma_page(pages->sgl, &sg_iter, sg_nents(pages->sgl), 0) {
		ret = intel_mark_page_as_mop(dev_priv,
				sg_page_iter_dma_address(&sg_iter.base), owned);
		if (ret)
			return ret;
	}

	return 0;
}

#define SIM_MEMORY_FLUSH_TIMER_INTERVAL 4000

static void presi_timer_fn(struct timer_list *t)
{
	struct drm_i915_private *i915 = from_timer(i915, t, presi_info.timer);

	intel_presi_flush_memory(i915);
	mod_timer(&i915->presi_info.timer,
		  jiffies + msecs_to_jiffies(SIM_MEMORY_FLUSH_TIMER_INTERVAL));
}

void intel_presi_start_timer(struct drm_i915_private *i915)
{
	if (!IS_SIMULATOR(i915))
		return;

	timer_setup(&i915->presi_info.timer, presi_timer_fn, 0);
	mod_timer(&i915->presi_info.timer,
		  jiffies + msecs_to_jiffies(SIM_MEMORY_FLUSH_TIMER_INTERVAL));
}

void intel_presi_stop_timer(struct drm_i915_private *i915)
{
	if (!IS_SIMULATOR(i915))
		return;

	if (i915->presi_info.timer.function)
		del_timer_sync(&i915->presi_info.timer);
}
