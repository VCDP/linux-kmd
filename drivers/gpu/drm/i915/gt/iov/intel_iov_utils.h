/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_UTILS_H__
#define __INTEL_IOV_UTILS_H__

#include "i915_drv.h"

static inline struct intel_gt *iov_to_gt(struct intel_iov *iov)
{
	return container_of(iov, struct intel_gt, iov);
}

static inline struct intel_guc *iov_to_guc(struct intel_iov *iov)
{
	return &iov_to_gt(iov)->uc.guc;
}

static inline struct drm_i915_private *iov_to_i915(struct intel_iov *iov)
{
	return iov_to_gt(iov)->i915;
}

static inline struct device *iov_to_dev(struct intel_iov *iov)
{
	return iov_to_i915(iov)->drm.dev;
}

static inline struct intel_iov *iov_get_root(struct intel_iov *iov)
{
	return &iov_to_i915(iov)->gt.iov;
}

static inline bool iov_is_root(struct intel_iov *iov)
{
	return iov == iov_get_root(iov);
}

static inline bool iov_is_remote(struct intel_iov *iov)
{
	return !iov_is_root(iov);
}

static inline u16 pf_get_totalvfs(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	return pci_sriov_get_totalvfs(to_pci_dev(iov_to_dev(iov)));
}

static inline u64 pf_get_vfs_mask(struct intel_iov *iov)
{
	return i915_modparams.vfs_mask & GENMASK_ULL(pf_get_totalvfs(iov), 1);
}

static inline u16 pf_get_totalvfs_masked(struct intel_iov *iov)
{
	return hweight64(pf_get_vfs_mask(iov));
}

static inline bool pf_is_vf_enabled(struct intel_iov *iov, u16 n)
{
	return pf_get_vfs_mask(iov) & BIT_ULL(n);
}

static inline int pf_get_status(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	return iov->pf.status ?: -EBUSY;
}

static inline bool pf_in_error(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	return iov->pf.status < 0;
}

static inline bool pf_is_admin_only(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	return (i915_modparams.enable_guc & ENABLE_GUC_SUBMISSION) == 0;
}

void pf_update_status(struct intel_iov *iov, int status, const char *reason);

#ifdef CPTCFG_DRM_I915_DEBUG_GUC
#define IOV_DEBUG(_iov, _fmt, ...) \
	DRM_DEV_DEBUG_DRIVER(iov_to_dev(_iov), "IOV: " _fmt, ##__VA_ARGS__)
#define IOV_ERROR(_iov, _fmt, ...) \
	DRM_DEV_ERROR(iov_to_dev(_iov), "IOV: " _fmt, ##__VA_ARGS__)
#else
#define IOV_DEBUG(...)
#define IOV_ERROR(_iov, _fmt, ...) \
	dev_notice(iov_to_dev(_iov), "IOV: " _fmt, ##__VA_ARGS__)
#endif

#endif /* __INTEL_IOV_UTILS_H__ */
