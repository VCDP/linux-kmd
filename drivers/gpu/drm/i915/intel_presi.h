/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef _INTEL_PRESI_H_
#define _INTEL_PRESI_H_

#include <linux/types.h>
#include <linux/timer.h>

struct drm_i915_private;
struct sg_table;

/*
 * We support different pre-silicon modes:
 * - simulation: GPU is simulated. Model is functionally accurate but
 * 		 implementation does not necessarily match HW.
 * - emulation pipeGT: GT RTL is booted on FPGA, while the rest of the HW
 * 		       is simulated.
 * - emulation pipe2D: Display and Gunit RTL is booted on FPGA, while the rest
 * 		       of the HW is simulated.
 *
 * Note: the enum values for detected envs are equal to the modparam values + 1
 */
struct intel_presi_info {
	enum intel_presi_mode {
		I915_PRESI_MODE_AUTODETECT = -1,
		I915_PRESI_MODE_UNKNOWN = 0, /* aka not detected yet */
		I915_PRESI_MODE_NONE = 1, /* aka SILICON */
		I915_PRESI_MODE_SIMULATOR = 2,
		I915_PRESI_MODE_EMULATOR_PIPEGT = 3,
		I915_PRESI_MODE_EMULATOR_PIPE2D = 4,
		I915_PRESI_MODE_EMULATOR_MEDIA_SUPERPIPE = 5,
		I915_MAX_PRESI_MODE = I915_PRESI_MODE_EMULATOR_MEDIA_SUPERPIPE
	} mode;

	struct timer_list timer;
};

#define MODPARAM_TO_PRESI_MODE(x) ({ \
	int val__ = (x); \
	val__ > 0 ? val__ + 1 : val__; \
})

#define IS_PRESI_MODE(i915, x) ({ \
	GEM_BUG_ON((i915)->presi_info.mode == I915_PRESI_MODE_UNKNOWN); \
	(i915)->presi_info.mode == I915_PRESI_MODE_##x; \
})

#define IS_PRESILICON(i915) (!IS_PRESI_MODE(i915, NONE))
#define IS_SIMULATOR(i915) (IS_PRESI_MODE(i915, SIMULATOR))
#define IS_PIPEGT_EMULATOR(i915) (IS_PRESI_MODE(i915, EMULATOR_PIPEGT))
#define IS_PIPE2D_EMULATOR(i915) (IS_PRESI_MODE(i915, EMULATOR_PIPE2D))
#define IS_MEDIA_SUPERPIPE_EMULATOR(__i915) \
	(IS_PRESI_MODE(__i915, EMULATOR_MEDIA_SUPERPIPE))
#define IS_EMULATOR(i915) (IS_PIPEGT_EMULATOR(i915) || \
			   IS_PIPE2D_EMULATOR(i915) || \
			   IS_MEDIA_SUPERPIPE_EMULATOR(i915))


#define INTEL_PCH_HAS4_DEVICE_ID_TYPE  0x3a00 /* simics + HAS */
static inline bool is_presi_pch(unsigned short id)
{
	return id == INTEL_PCH_HAS4_DEVICE_ID_TYPE;
}

/*
 * pre-si environments are slower, so we need to bump timeouts. Using a
 * large multiplier to rule out any environment speed issues when timeouts
 * occur.
 */
#define PRESI_UC_MS_TIMEOUT(i915__, timeout__) ({ \
	u32 multiplier__ = 1; \
	BUILD_BUG_ON(!__builtin_constant_p(timeout__)); \
	GEM_BUG_ON(timeout__ > 100); \
	if (i915_modparams.presi_timeout_multiplier) \
		multiplier__ = i915_modparams.presi_timeout_multiplier; \
	else if (IS_SIMULATOR(i915__)) \
		multiplier__ = 100; \
	else if (IS_EMULATOR(i915__)) \
		multiplier__ = 1000; \
	timeout__ * multiplier__; \
})

/* If max timeout, no need to have a multiplier (e.g. set to 1) */
#define PRESI_GET_MULTIPLIER(i915__, timeout__) ({ \
	u32 multiplier__ = 1; \
	struct drm_i915_private *i915___ = (i915__); \
	if (timeout__ != MAX_SCHEDULE_TIMEOUT) { \
		if (i915_modparams.presi_timeout_multiplier) \
			multiplier__ = i915_modparams.presi_timeout_multiplier; \
		else if (IS_SIMULATOR(i915___)) \
			multiplier__ = 100; \
		else if (IS_EMULATOR(i915___)) \
			multiplier__ = 1000; \
	} \
	multiplier__; \
})

#define PRESI_WAIT_MS_TIMEOUT(i915__, timeout__) ({ \
	u32 multiplier__ = 1; \
	struct drm_i915_private *i915___ = (i915__); \
	BUILD_BUG_ON(!__builtin_constant_p(timeout__)); \
	BUILD_BUG_ON(timeout__ >= (MAX_SCHEDULE_TIMEOUT / 100)); \
	if (i915_modparams.presi_timeout_multiplier) \
		multiplier__ = i915_modparams.presi_timeout_multiplier; \
	else if (IS_SIMULATOR(i915___)) \
		multiplier__ = 100; \
	else if (IS_EMULATOR(i915___)) \
		multiplier__ = 1000; \
	timeout__ * multiplier__; \
})

void intel_presi_init(struct drm_i915_private *i915);
void intel_presi_flush_memory(struct drm_i915_private *i915);

int intel_mark_page_as_mop(struct drm_i915_private *dev_priv,
			   u64 address, bool owned);
int intel_mark_all_pages_as_mop(struct drm_i915_private *dev_priv,
				struct sg_table *pages, bool owned);

void intel_presi_start_timer(struct drm_i915_private *i915);
void intel_presi_stop_timer(struct drm_i915_private *i915);

#endif
