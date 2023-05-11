/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_VM_BIND_H
#define __I915_GEM_VM_BIND_H

#include "i915_drv.h"

int i915_gem_vm_bind_obj(struct i915_address_space *vm,
			 struct drm_i915_gem_vm_bind *va,
			 struct drm_file *file);

#endif /* __I915_GEM_VM_BIND_H */
