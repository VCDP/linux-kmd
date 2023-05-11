/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_H__
#define __INTEL_IOV_H__

#include <linux/build_bug.h>
#include "intel_iov_types.h"
#include "i915_gem.h"

const char *intel_iov_mode_to_string(enum intel_iov_mode mode);

void intel_iov_probe(struct intel_iov *iov);
void intel_iov_release(struct intel_iov *iov);

void intel_iov_init_early(struct intel_iov *iov);
int intel_iov_init_mmio(struct intel_iov *iov);
int intel_iov_init_ggtt(struct intel_iov *iov);
void intel_iov_fini_ggtt(struct intel_iov *iov);
int intel_iov_init(struct intel_iov *iov);
void intel_iov_fini(struct intel_iov *iov);
int intel_iov_init_hw(struct intel_iov *iov);
void intel_iov_fini_hw(struct intel_iov *iov);
int intel_iov_init_late(struct intel_iov *iov);
void intel_iov_fini_late(struct intel_iov *iov);

static inline const char *intel_iov_mode_string(const struct intel_iov *iov)
{
	return intel_iov_mode_to_string(iov->__mode);
}

static inline enum intel_iov_mode intel_iov_mode(const struct intel_iov *iov)
{
	BUILD_BUG_ON(!INTEL_IOV_MODE_NONE);
	BUILD_BUG_ON(!INTEL_IOV_MODE_SRIOV_PF);
	BUILD_BUG_ON(!INTEL_IOV_MODE_SRIOV_VF);
	GEM_BUG_ON(!iov->__mode);
	return iov->__mode;
}

static inline bool intel_iov_is_enabled(const struct intel_iov *iov)
{
	return intel_iov_mode(iov) != INTEL_IOV_MODE_NONE;
}

static inline bool intel_iov_is_pf(const struct intel_iov *iov)
{
#ifdef CONFIG_PCI_IOV
	return intel_iov_mode(iov) == INTEL_IOV_MODE_SRIOV_PF;
#else
	return false;
#endif
}

static inline bool intel_iov_is_vf(const struct intel_iov *iov)
{
	return intel_iov_mode(iov) == INTEL_IOV_MODE_SRIOV_VF;
}

/* forward looking location of the IOV struct */
#define to_intel_iov(i915) (&(i915)->gt.iov)

#define IS_SRIOV_PF(i915) (intel_iov_is_pf(to_intel_iov(i915)))
#define IS_SRIOV_VF(i915) (intel_iov_is_vf(to_intel_iov(i915)))

#endif /* __INTEL_IOV_H__ */
