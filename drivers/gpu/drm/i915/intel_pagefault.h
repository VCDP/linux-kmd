// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _INTEL_PAGEFAULT_H
#define _INTEL_PAGEFAULT_H

#include <linux/types.h>

struct drm_i915_gem_object;
struct drm_i915_private;
struct intel_guc;

int intel_pagefault_process_cat_error_msg(struct drm_i915_private *i915,
					  const u32 *payload, u32 len);
int intel_pagefault_process_page_fault_msg(struct drm_i915_private *i915,
					   const u32 *payload, u32 len);
int intel_pagefault_req_process_msg(struct intel_guc *guc, const u32 *payload,
				    u32 len);
void
intel_recoverable_page_fault_legacy_init(struct drm_i915_private *dev_priv);

#endif
