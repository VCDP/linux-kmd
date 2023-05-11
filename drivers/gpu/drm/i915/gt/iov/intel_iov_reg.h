/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_REG_H__
#define __INTEL_IOV_REG_H__

#include "i915_reg.h"

/* VF_CAPABILITY_REGISTER */
#define GEN12_VF_CAP_REG		_MMIO(0x1901f8)
#define   GEN12_VF			(1 << 0)

/* VIRTUALIZATION CONTROL REGISTER */
#define GEN12_VIRTUAL_CTRL_REG		_MMIO(0x10108C)
#define   GEN12_GUEST_GTT_UPDATE_EN	(1 << 8)

/* ISR */
#define I915_VF_IRQ_STATUS 0x0
/* IIR */
#define I915_VF_IRQ_SOURCE 0x400
/* IMR */
#define I915_VF_IRQ_ENABLE 0x440

#endif /* __INTEL_IOV_REG_H__ */
