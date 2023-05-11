/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_TYPES_H__
#define __INTEL_IOV_TYPES_H__

#include <linux/types.h>
#include <linux/spinlock.h>
#include "intel_lmtt.h"
#include "i915_reg.h"

/**
 * enum intel_iov_mode - I/O Virtualization mode.
 */
enum intel_iov_mode {
	INTEL_IOV_MODE_NONE = 1,
	INTEL_IOV_MODE_SRIOV_PF,
	INTEL_IOV_MODE_SRIOV_VF,
};

/**
 * struct intel_iov_config - IOV configuration data.
 * @num_ctxs: number of GuC submission contexts.
 * @num_dbs: number of GuC doorbells.
 * @ggtt_base: base of GGTT region.
 * @ggtt_size: size of GGTT region.
 * @lmem_size: LMEM size (in MB).
 */
struct intel_iov_config {
	u16 num_ctxs;
	u16 num_dbs;
	u32 ggtt_base;
	u32 ggtt_size;
	u32 lmem_size;
};

/**
 * struct intel_iov_provisioning - IOV provisioning data.
 * @available: resources available for VFs provisioning.
 * @configs: flexible array with configuration data for PF and VFs.
 */
struct intel_iov_provisioning {
	struct {
		struct kobject *dir;
		struct iov_kobj *entries;
	} sysfs;
	struct intel_iov_config available;
	struct intel_iov_config configs[];
};

/**
 * struct intel_iov_runtime_regs - Register runtime info shared with VFs.
 * @size: size of the regs and value arrays.
 * @regs: pointer to static array with register offsets.
 * @values: pointer to array with captured register values.
 */
struct intel_iov_runtime_regs {
	u32 size;
	const i915_reg_t *regs;
	u32 *values;
};

/**
 * struct intel_iov_service - Placeholder for service data shared with VFs.
 * @runtime: register runtime info shared with VFs.
 */
struct intel_iov_service {
	struct intel_iov_runtime_regs runtime;
};

/**
 * struct intel_iov_vf_runtime - Placeholder for the VF runtime data.
 * @regs_size: size of runtime register array.
 * @regs: pointer to array of register offset/value pairs.
 */
struct intel_iov_vf_runtime {
	u32 regs_size;
	struct vf_runtime_reg {
		u32 offset;
		u32 value;
	} *regs;
};

/**
 * struct intel_iov_memirq - IOV interrupts data.
 * @obj: GEM object with memory interrupt data.
 * @vma: VMA of the object.
 * @vaddr: pointer to memory interrupt data.
 */
struct intel_iov_memirq {
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	void *vaddr;
};

/**
 * struct intel_iov_relay - IOV Relay Communication data.
 * @lock: protects #pending_relays and #last_fence.
 * @pending_relays: list of relay requests that await a response.
 * @last_fence: fence used with last message.
 */
struct intel_iov_relay {
	spinlock_t lock;
	struct list_head pending_relays;
	u32 last_fence;
};

/**
 * struct intel_iov - I/O Virtualization related data.
 * @__mode: actual I/O virtualization mode.
 * @pf.status: status of the PF. Must be > 0 to configure VFs.
 * @pf.provisioning: pointer to provisioning data.
 * @pf.service: placeholder for service data.
 * @pf.lmem_objs: pointer to array with LMEM allocations for VFs.
 * @pf.lmtt: local memory translation tables for VFs.
 * @vf.config: configuration of the resources assigned to VF.
 * @vf.runtime: retrieved runtime info.
 * @vf.irq: Memory based interrupts data.
 * @relay: data related to VF/PF communication based on GuC Relay messages.
 */
struct intel_iov {
	enum intel_iov_mode __mode;

	union {
		struct {
			int status;
			struct intel_iov_provisioning *provisioning;
			struct intel_iov_service service;
			struct drm_i915_gem_object **lmem_objs;
			struct intel_lmtt lmtt;
		} pf;

		struct {
			struct intel_iov_config config;
			struct intel_iov_vf_runtime runtime;
			struct intel_iov_memirq irq;
		} vf;
	};

	struct intel_iov_relay relay;
};

#endif /* __INTEL_IOV_TYPES_H__ */
