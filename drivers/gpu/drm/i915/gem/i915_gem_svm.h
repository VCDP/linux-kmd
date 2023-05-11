/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_SVM_H
#define __I915_GEM_SVM_H

#include "i915_drv.h"

#if defined(CPTCFG_DRM_I915_SVM)
int i915_gem_vm_bind_svm_obj(struct i915_address_space *vm,
			     struct drm_i915_gem_vm_bind_va *va,
			     struct drm_file *file);
#else
static inline int i915_gem_vm_bind_svm_obj(struct i915_address_space *vm,
					   struct drm_i915_gem_vm_bind_va *va,
					   struct drm_file *file)
{ return -ENOTSUPP; }
#endif

#endif /* __I915_GEM_SVM_H */
