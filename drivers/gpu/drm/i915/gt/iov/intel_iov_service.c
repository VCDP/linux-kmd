// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/bitfield.h>

#include "intel_iov_abi.h"
#include "intel_iov_relay.h"
#include "intel_iov_service.h"
#include "intel_iov_types.h"
#include "intel_iov_utils.h"

static void __uncore_read_many(struct intel_uncore *uncore, unsigned int count,
			       const i915_reg_t *regs, u32 *values)
{
	while (count--) {
		*values++ = intel_uncore_read(uncore, *regs++);
	}
}

static const i915_reg_t tgl_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0D00) */
	GEN11_EU_DISABLE,		/* _MMIO(0x9134) */
	GEN11_GT_SLICE_ENABLE,		/* _MMIO(0x9138) */
	GEN12_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913C) */
	GEN11_GT_VEBOX_VDBOX_DISABLE,	/* _MMIO(0x9140) */
	CTC_MODE,			/* _MMIO(0xA26C) */
	GEN9_TIMESTAMP_OVERRIDE,	/* _MMIO(0x44074) */
};

static const i915_reg_t ats_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0D00) */
	GEN11_EU_DISABLE,		/* _MMIO(0x9134) */
	GEN11_GT_SLICE_ENABLE,		/* _MMIO(0x9138) */
	GEN12_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913C) */
	GEN11_GT_VEBOX_VDBOX_DISABLE,	/* _MMIO(0x9140) */
	GEN12_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	CTC_MODE,			/* _MMIO(0xA26C) */
	GEN9_TIMESTAMP_OVERRIDE,	/* _MMIO(0x44074) */
};

static const i915_reg_t pvc_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0D00) */
	ATS_EU_ENABLE,			/* _MMIO(0x9134) */
	GEN11_GT_SLICE_ENABLE,		/* _MMIO(0x9138) */
	GEN12_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913C) */
	GEN11_GT_VEBOX_VDBOX_DISABLE,	/* _MMIO(0x9140) */
	GEN12_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	_MMIO(0x9148),			/* FIXME: GEN12_GT_COMPUTE_DSS_ENABLE_EXT */
	CTC_MODE,			/* _MMIO(0xA26C) */
	GEN9_TIMESTAMP_OVERRIDE,	/* _MMIO(0x44074) */
};

static const i915_reg_t *get_runtime_regs(struct drm_i915_private *i915,
					  unsigned int *size)
{
	const i915_reg_t *regs;

	if (IS_PONTEVECCHIO(i915)) {
		regs = pvc_runtime_regs;
		*size = ARRAY_SIZE(pvc_runtime_regs);
	} else if (IS_ARCTICSOUND(i915)) {
		regs = ats_runtime_regs;
		*size = ARRAY_SIZE(ats_runtime_regs);
	} else if (IS_TIGERLAKE(i915)) {
		regs = tgl_runtime_regs;
		*size = ARRAY_SIZE(tgl_runtime_regs);
	} else {
		MISSING_CASE(INTEL_GEN(i915));
		regs = ERR_PTR(-ENODEV);
		*size = 0;
	}

	return regs;
}

static bool regs_selftest(const i915_reg_t *regs, unsigned int count)
{
	u32 offset = 0;

	while (IS_ENABLED(CPTCFG_DRM_I915_SELFTEST) && count--) {
		if (i915_mmio_reg_offset(*regs) < offset) {
			pr_err("invalid register order: %#x < %#x\n",
				i915_mmio_reg_offset(*regs), offset);
			return false;
		}
		offset = i915_mmio_reg_offset(*regs++);
	}

	return true;
}

static int pf_alloc_runtime_info(struct intel_iov *iov)
{
	const i915_reg_t *regs;
	unsigned int size;
	u32 *values;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(iov->pf.service.runtime.size);
	GEM_BUG_ON(iov->pf.service.runtime.regs);
	GEM_BUG_ON(iov->pf.service.runtime.values);

	regs = get_runtime_regs(iov_to_i915(iov), &size);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	if (unlikely(!size))
		return 0;

	if (unlikely(!regs_selftest(regs, size)))
		return -EBADSLT;

	values = kcalloc(size, sizeof(u32), GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	iov->pf.service.runtime.size = size;
	iov->pf.service.runtime.regs = regs;
	iov->pf.service.runtime.values = values;

	return 0;
}

static void pf_release_runtime_info(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	kfree(iov->pf.service.runtime.values);
	iov->pf.service.runtime.values = NULL;
	iov->pf.service.runtime.regs = NULL;
	iov->pf.service.runtime.size = 0;
}

static void pf_prepare_runtime_info(struct intel_iov *iov)
{
	const i915_reg_t *regs;
	unsigned int size;
	u32 *values;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.service.runtime.size)
		return;

	size = iov->pf.service.runtime.size;
	regs = iov->pf.service.runtime.regs;
	values = iov->pf.service.runtime.values;

	__uncore_read_many(iov_to_gt(iov)->uncore, size, regs, values);

	while (size--) {
		IOV_DEBUG(iov, "reg[%#x] = %#x\n",
			  i915_mmio_reg_offset(*regs++), *values++);
	}
}

static void pf_reset_runtime_info(struct intel_iov *iov)
{
	unsigned int size;
	u32 *values;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.service.runtime.size)
		return;

	size = iov->pf.service.runtime.size;
	values = iov->pf.service.runtime.values;

	while (size--)
		*values++ = 0;
}

/**
 * intel_iov_service_init_early - Early initialization of the PF IOV services.
 * @iov: the IOV struct
 *
 * Performs early initialization of the IOV PF services, including preparation
 * of the runtime info that will be shared with VFs.
 *
 * This function can only be called on PF.
 */
void intel_iov_service_init_early(struct intel_iov *iov)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	err = pf_alloc_runtime_info(iov);
	if (unlikely(err))
		pf_update_status(iov, err, "runtime");
}

/**
 * intel_iov_service_release - Cleanup PF IOV services.
 * @iov: the IOV struct
 *
 * Releases any data allocated during initialization.
 *
 * This function can only be called on PF.
 */
void intel_iov_service_release(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	pf_release_runtime_info(iov);
}

/* XXX: support for passing fake VF regs over modparam */
static void pf_prepare_fake_runtime_info(struct intel_iov *iov)
{
	unsigned int i;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.service.runtime.size)
		return;

	if (i915_modparams.num_fake_vf_regs) {
		IOV_DEBUG(iov, "fake vf_regs already defined %u\n",
			  i915_modparams.num_fake_vf_regs);
		return;
	}

	for (i = 0; i < iov->pf.service.runtime.size; i++) {
		if (WARN_ON(i >= ARRAY_SIZE(i915_modparams.fake_vf_regs) / 2))
			break;
		i915_modparams.fake_vf_regs[2 * i + 0] =
			i915_mmio_reg_offset(iov->pf.service.runtime.regs[i]);
		i915_modparams.fake_vf_regs[2 * i + 1] =
			iov->pf.service.runtime.values[i];
	}
	i915_modparams.num_fake_vf_regs = 2 * i;
	IOV_DEBUG(iov, "defined %u fake vf_regs pairs\n",
		  i915_modparams.num_fake_vf_regs / 2);
}

/**
 * intel_iov_service_update - Update PF IOV services.
 * @iov: the IOV struct
 *
 * Updates runtime data shared with VFs.
 *
 * This function can be called more than once.
 * This function can only be called on PF.
 */
void intel_iov_service_update(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	pf_prepare_runtime_info(iov);
	pf_prepare_fake_runtime_info(iov);
}

/**
 * intel_iov_service_reset - Update PF IOV services.
 * @iov: the IOV struct
 *
 * Resets runtime data to avoid sharing stale info with VFs.
 *
 * This function can be called more than once.
 * This function can only be called on PF.
 */
void intel_iov_service_reset(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	pf_reset_runtime_info(iov);
}

static int reply_get_version(struct intel_iov *iov, u32 origin, u32 fence,
			     const u32 *req_data, u32 req_len)
{
	struct intel_iov_relay *relay = &iov->relay;
	u32 response[VFPF_GET_VERSION_RESP_LEN];

	if (unlikely(req_len != VFPF_GET_VERSION_REQ_LEN))
		return -EPROTO;

	response[0] = FIELD_PREP(VFPF_GET_VERSION_RESP_0_MAJOR,
				 VFPF_ABI_VERSION_MAJOR) |
		      FIELD_PREP(VFPF_GET_VERSION_RESP_0_MINOR,
				 VFPF_ABI_VERSION_MINOR);

	return intel_iov_relay_send_response(relay, origin, fence,
					     response, ARRAY_SIZE(response));
}

static int reply_runtime_query(struct intel_iov *iov, u32 origin, u32 fence,
			       const u32 *data, u32 data_len)
{
	struct intel_iov_runtime_regs *runtime = &iov->pf.service.runtime;
	u32 response[VFPF_GET_RUNTIME_RESP_LEN_MAX];
	const u32 max_chunk = (ARRAY_SIZE(response) - 1) / 2;
	u32 start, chunk, i;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (unlikely(data_len != VFPF_GET_RUNTIME_REQ_LEN))
		return -EPROTO;

	start = FIELD_GET(VFPF_GET_RUNTIME_REQ_0_START, data[0]);
	if (unlikely(start > runtime->size))
		return -EINVAL;

	chunk = min_t(u32, runtime->size - start, max_chunk);

	response[0] = runtime->size - start - chunk;

	for (i = 0; i < chunk; ++i) {
		i915_reg_t reg = runtime->regs[start + i];
		u32 offset = i915_mmio_reg_offset(reg);
		u32 value = runtime->values[start + i];

		response[1 + 2 * i] = offset;
		response[1 + 2 * i + 1] = value;
	}

	return intel_iov_relay_send_response(&iov->relay, origin, fence,
					     response, 1 + 2 * chunk);
}

static void pf_action_gtcr(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);

	intel_uncore_write(gt->uncore, GEN12_GUC_TLB_INV_CR,
			   GEN12_GUC_TLB_INV_CR_INVALIDATE);
}

static void pf_action_gfx_flsh_cnt(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);

	if (!IS_TIGERLAKE(gt->i915))
		return;

	intel_uncore_write(gt->uncore, GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
}

static int perform_action(struct intel_iov *iov, u32 origin, u32 fence,
			  const u32 *data, u32 data_len)
{
	u32 action;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (unlikely(data_len != VFPF_PF_ACTION_REQ_LEN))
		return -EPROTO;

	action = FIELD_GET(VFPF_PF_ACTION_REQ_0_ID, data[0]);
	switch (action) {
	case VFPF_PF_ACTION_GTCR:
		pf_action_gtcr(iov);
		break;
	case VFPF_PF_ACTION_GFX_FLSH_CNT:
		pf_action_gfx_flsh_cnt(iov);
		break;
	default:
		return -EINVAL;
	}

	return intel_iov_relay_send_status(&iov->relay, origin, fence, 0);
}

/**
 * intel_iov_service_request_handler - TBD.
 * @iov: the IOV struct
 * @origin: origin VF number
 * @fence: request fence
 * @action: request action ID
 * @payload: request payload data
 * @len: length of the payload data (in dwords)
 *
 * TBD.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_service_request_handler(struct intel_iov *iov, u32 origin,
				      u32 fence, u32 action,
				      const u32 *payload, u32 len)
{
	int err = -EOPNOTSUPP;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	switch (action) {
	case VFPF_GET_VERSION_REQUEST:
		err = reply_get_version(iov, origin, fence, payload, len);
		break;
	case VFPF_GET_RUNTIME_REQUEST:
		err = reply_runtime_query(iov, origin, fence, payload, len);
		break;
	case VFPF_PF_ACTION_REQUEST:
		err = perform_action(iov, origin, fence, payload, len);
		break;
	default:
		break;
	}

	return err;
}
