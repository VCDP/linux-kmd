// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/bitfield.h>

#include "i915_drv.h"
#include "intel_iov_abi.h"
#include "intel_iov_relay.h"
#include "intel_iov_utils.h"
#include "intel_iov_types.h"

static int __guc_action_get_init_data(struct intel_guc *guc,
				      u32 *data, u32 data_size)
{
	u32 request[] = {
		INTEL_GUC_ACTION_GET_INIT_DATA,
		FIELD_PREP(GUC_GET_INIT_DATA_REQ_0_GUC_MAJOR,
			   guc->fw.major_ver_wanted) |
		FIELD_PREP(GUC_GET_INIT_DATA_REQ_0_GUC_MINOR,
			   guc->fw.minor_ver_wanted),
	};

	/* Can't use generic send(), must go over MMIO */
	return intel_guc_send_mmio(guc, request, ARRAY_SIZE(request),
				   data, data_size);
}

static int vf_handshake_with_guc(struct intel_iov *iov)
{
	struct intel_guc *guc = iov_to_guc(iov);
	u32 data[GUC_GET_INIT_DATA_RESP_LEN];
	u16 major, minor, tiles;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = __guc_action_get_init_data(guc, data, ARRAY_SIZE(data));
	if (unlikely(err))
		return err;

	major = FIELD_GET(GUC_GET_INIT_DATA_RESP_0_GUC_MAJOR, data[0]);
	minor = FIELD_GET(GUC_GET_INIT_DATA_RESP_0_GUC_MINOR, data[0]);

	IOV_DEBUG(iov, "GuC %u.%u\n", major, minor);

	if (HAS_REMOTE_TILES(iov_to_i915(iov))) {
		tiles = FIELD_GET(GUC_GET_INIT_DATA_RESP_1_TILE_MASK, data[1]);
		IOV_DEBUG(iov, "tile mask %#x\n", tiles);
	}

	return intel_uc_fw_set_preloaded(&guc->fw, major, minor);
}

static int __guc_action_get_ggtt_info(struct intel_guc *guc,
				      u32 *data, u32 data_size)
{
	u32 request[] = {
		INTEL_GUC_ACTION_GET_GGTT_INFO,
	};

	/* Can't use generic send(), must go over MMIO */
	return intel_guc_send_mmio(guc, request, ARRAY_SIZE(request),
				   data, data_size);
}

static int vf_get_ggtt_info(struct intel_iov *iov)
{
	struct intel_guc *guc = iov_to_guc(iov);
	u32 data[GUC_GET_GGTT_INFO_RESP_LEN];
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	GEM_BUG_ON(iov->vf.config.ggtt_size);

	err = __guc_action_get_ggtt_info(guc, data, ARRAY_SIZE(data));
	if (unlikely(err))
		return err;

	/* received in 2MB units */
	iov->vf.config.ggtt_size = FIELD_GET(GUC_GET_GGTT_INFO_RESP_0_GGTT_SIZE,
					     data[0]) * SZ_2M;
	iov->vf.config.ggtt_base = FIELD_GET(GUC_GET_GGTT_INFO_RESP_0_GGTT_BASE,
					     data[0]) * SZ_2M;

	IOV_DEBUG(iov, "GGTT %#x-%#x = %uM\n",
		  iov->vf.config.ggtt_base,
		  iov->vf.config.ggtt_base + iov->vf.config.ggtt_size,
		  iov->vf.config.ggtt_size / SZ_1M);

	return iov->vf.config.ggtt_size ? 0 : -ENODATA;
}

static int __guc_action_get_lmem_info(struct intel_guc *guc,
				      u32 *data, u32 data_size)
{
	u32 request[] = {
		INTEL_GUC_ACTION_GET_LMEM_INFO,
	};

	/* Can't use generic send(), must go over MMIO */
	return intel_guc_send_mmio(guc, request, ARRAY_SIZE(request),
				   data, data_size);
}

static int vf_get_lmem_info(struct intel_iov *iov)
{
	struct intel_guc *guc = iov_to_guc(iov);
	u32 data[GUC_GET_LMEM_INFO_RESP_LEN];
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	GEM_BUG_ON(iov->vf.config.lmem_size);

	err = __guc_action_get_lmem_info(guc, data, ARRAY_SIZE(data));
	if (unlikely(err))
		return err;

	/* received in 2MB units but stored in 1MB */
	iov->vf.config.lmem_size = FIELD_GET(GUC_GET_LMEM_INFO_RESP_0_LMEM_SIZE,
					     data[0]) * 2;

	IOV_DEBUG(iov, "LMEM %uM\n", iov->vf.config.lmem_size);

	return iov->vf.config.lmem_size ? 0 : -ENODATA;
}

static int __guc_action_get_submission_cfg(struct intel_guc *guc,
					   u32 *data, u32 data_size)
{
	u32 request[] = {
		INTEL_GUC_ACTION_GET_SUBMISSION_CFG,
	};

	/* Can't use generic send(), must go over MMIO */
	return intel_guc_send_mmio(guc, request, ARRAY_SIZE(request),
				   data, data_size);
}

static int vf_get_submission_cfg(struct intel_iov *iov)
{
	struct intel_guc *guc = iov_to_guc(iov);
	u32 data[GUC_GET_SUBMISSION_CFG_RESP_LEN];
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	GEM_BUG_ON(iov->vf.config.num_ctxs);

	err = __guc_action_get_submission_cfg(guc, data, ARRAY_SIZE(data));
	if (unlikely(err))
		return err;

	iov->vf.config.num_ctxs =
		FIELD_GET(GUC_GET_SUBMISSION_CFG_RESP_0_NUM_CTXS, data[0]);
	iov->vf.config.num_dbs =
		FIELD_GET(GUC_GET_SUBMISSION_CFG_RESP_0_NUM_DBS, data[0]);

	IOV_DEBUG(iov, "CTXS %u DBS %u\n",
		  iov->vf.config.num_ctxs, iov->vf.config.num_dbs);

	return iov->vf.config.num_ctxs ? 0 : -ENODATA;
}

/**
 * intel_iov_query_bootstrap - Query IOV boot data over MMIO.
 * @iov: the IOV struct
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_query_bootstrap(struct intel_iov *iov)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = vf_handshake_with_guc(iov);
	if (unlikely(err))
		return err;

	err = vf_get_ggtt_info(iov);
	if (unlikely(err))
		return err;

	if (HAS_LMEM(iov_to_i915(iov))) {
		err = vf_get_lmem_info(iov);
		if (unlikely(err))
			return err;
	}

	err = vf_get_submission_cfg(iov);
	if (unlikely(err))
		return err;

	return 0;
}

module_param_array_named(vf_regs, i915_modparams.fake_vf_regs, uint,
			 &i915_modparams.num_fake_vf_regs, S_IRUGO);
MODULE_PARM_DESC(vf_regs, "VF runtime regs: offset1,value1,offset2,value2,...");

static u32 __lookup_fake_vf_reg(u32 offset)
{
	int i;

	for (i = 0; i + 1 < ARRAY_SIZE(i915_modparams.fake_vf_regs); i += 2) {
		if (i915_modparams.fake_vf_regs[i] == offset)
			return i915_modparams.fake_vf_regs[i + 1];
	}

	return 0;
}

static const i915_reg_t tgl_early_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0D00) */
	GEN11_EU_DISABLE,		/* _MMIO(0x9134) */
	GEN11_GT_SLICE_ENABLE,		/* _MMIO(0x9138) */
	GEN12_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913C) */
	GEN11_GT_VEBOX_VDBOX_DISABLE,	/* _MMIO(0x9140) */
	CTC_MODE,			/* _MMIO(0xA26C) */
};

static const i915_reg_t ats_early_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0D00) */
	GEN11_EU_DISABLE,		/* _MMIO(0x9134) */
	GEN11_GT_SLICE_ENABLE,		/* _MMIO(0x9138) */
	GEN12_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913C) */
	GEN11_GT_VEBOX_VDBOX_DISABLE,	/* _MMIO(0x9140) */
	GEN12_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	CTC_MODE,			/* _MMIO(0xA26C) */
};

static const i915_reg_t pvc_early_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0D00) */
	GEN11_EU_DISABLE,		/* _MMIO(0x9134) */
	GEN11_GT_SLICE_ENABLE,		/* _MMIO(0x9138) */
	GEN12_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913C) */
	GEN11_GT_VEBOX_VDBOX_DISABLE,	/* _MMIO(0x9140) */
	GEN12_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	_MMIO(0x9148),			/* FIXME: GEN12_GT_COMPUTE_DSS_ENABLE_EXT */
	CTC_MODE,			/* _MMIO(0xA26C) */
};

static const i915_reg_t *get_early_regs(struct drm_i915_private *i915,
					unsigned int *size)
{
	const i915_reg_t *regs;

	if (IS_PONTEVECCHIO(i915)) {
		regs = pvc_early_regs;
		*size = ARRAY_SIZE(pvc_early_regs);
	} else if (IS_ARCTICSOUND(i915)) {
		regs = ats_early_regs;
		*size = ARRAY_SIZE(ats_early_regs);
	} else if (IS_TIGERLAKE(i915)) {
		regs = tgl_early_regs;
		*size = ARRAY_SIZE(tgl_early_regs);
	} else {
		MISSING_CASE(INTEL_GEN(i915));
		regs = ERR_PTR(-ENODEV);
		*size = 0;
	}

	return regs;
}

static void vf_cleanup_runtime_info(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_vf(iov));

	kfree(iov->vf.runtime.regs);
	iov->vf.runtime.regs = NULL;
	iov->vf.runtime.regs_size = 0;
}

static int vf_get_runtime_info_mmio(struct intel_iov *iov)
{
	struct intel_guc *guc = iov_to_guc(iov);
	u32 action[1 + GUC_GET_RUNTIME_INFO_REQ_LEN] = {
		INTEL_GUC_ACTION_GET_INIT_DATA, /* XXX GuC 40.x only */
	};
	u32 response[GUC_GET_RUNTIME_INFO_RESP_LEN];
	struct vf_runtime_reg *vf_regs;
	const i915_reg_t *regs;
	unsigned int size, size_up, i;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	GEM_BUG_ON(iov->vf.runtime.regs);

	regs = get_early_regs(iov_to_i915(iov), &size);
	if (IS_ERR(regs)) {
		err = PTR_ERR(regs);
		goto failed;
	}
	if (!size)
		return 0;

	size_up = roundup(size, GUC_GET_RUNTIME_INFO_REQ_LEN);
	vf_regs = kcalloc(size_up, sizeof(struct vf_runtime_reg), GFP_KERNEL);
	if (unlikely(!vf_regs)) {
		err = -ENOMEM;
		goto failed;
	}

	for (i = 0; i < size; i++)
		vf_regs[i].offset = i915_mmio_reg_offset(regs[i]);

	for (i = 0; i < size_up; i += GUC_GET_RUNTIME_INFO_REQ_LEN) {
		action[1] = FIELD_PREP(GUC_GET_RUNTIME_INFO_REQ_0_OFFSET1,
				       vf_regs[i].offset);
		action[2] = FIELD_PREP(GUC_GET_RUNTIME_INFO_REQ_1_OFFSET2,
				       vf_regs[i + 1].offset);
		action[3] = FIELD_PREP(GUC_GET_RUNTIME_INFO_REQ_2_OFFSET3,
				       vf_regs[i + 2].offset);
		err = intel_guc_send_mmio(guc, action, ARRAY_SIZE(action),
					  response, ARRAY_SIZE(response));
		if (unlikely(err))
			goto failed;

		vf_regs[i + 0].value = FIELD_GET(GUC_GET_RUNTIME_INFO_RESP_0_VALUE1,
						 response[0]);
		vf_regs[i + 1].value = FIELD_GET(GUC_GET_RUNTIME_INFO_RESP_1_VALUE2,
						 response[1]);
		vf_regs[i + 2].value = FIELD_GET(GUC_GET_RUNTIME_INFO_RESP_2_VALUE3,
						 response[2]);
	}

	iov->vf.runtime.regs_size = size;
	iov->vf.runtime.regs = vf_regs;

	/* XXX: above MMIO action was fake, use values from modparam */
	for (i = 0; i < size; i++)
		vf_regs[i].value = __lookup_fake_vf_reg(vf_regs[i].offset);

	for (;size--; vf_regs++) {
		IOV_DEBUG(iov, "early reg[%#x] = %#x\n",
			  vf_regs->offset, vf_regs->value);
	}

	return 0;

failed:
	vf_cleanup_runtime_info(iov);
	return err;
}

static int vf_get_runtime_info_relay(struct intel_iov *iov)
{
	struct drm_i915_private *i915 = iov_to_i915(iov);
	u32 request[1];
	u32 response[VFPF_GET_RUNTIME_RESP_LEN_MAX];
	u32 start = 0;
	u32 remaining, num, i;
	int ret;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	assert_rpm_wakelock_held(&i915->runtime_pm);

repeat:
	request[0] = FIELD_PREP(VFPF_GET_RUNTIME_REQ_0_START, start);
	ret = intel_iov_relay_send_request(&iov->relay, 0,
					   VFPF_GET_RUNTIME_REQUEST,
					   request, ARRAY_SIZE(request),
					   response, ARRAY_SIZE(response));
	if (unlikely(ret < 0))
		goto failed;

	if (unlikely(ret < VFPF_GET_RUNTIME_RESP_LEN_MIN)) {
		ret = -EPROTO;
		goto failed;
	}
	if (unlikely((ret - 1) % 2)) {
		ret = -EPROTO;
		goto failed;
	}

	remaining = FIELD_GET(VFPF_GET_RUNTIME_RESP_0_REMAINING,
			      response[0]);
	num = (ret - 1) / 2;

	if (start == 0) {
		GEM_BUG_ON(iov->vf.runtime.regs);
		iov->vf.runtime.regs_size = num + remaining;
		iov->vf.runtime.regs = kcalloc(num + remaining,
						 sizeof(struct vf_runtime_reg),
						 GFP_KERNEL);
		if (!iov->vf.runtime.regs) {
			ret = -ENOMEM;
			goto failed;
		}
	} else if (unlikely(start + num > iov->vf.runtime.regs_size)) {
		ret = -EPROTO;
		goto failed;
	}

	for (i = 0; i < num; ++i) {
		struct vf_runtime_reg *reg = &iov->vf.runtime.regs[start + i];

		reg->offset = response[1 + 2 * i];
		reg->value = response[1 + 2 * i + 1];
		IOV_DEBUG(iov, "RUNTIME %u: %#x = %#x\n",
			  start + i, reg->offset, reg->value);
	}

	if (remaining) {
		start += num;
		goto repeat;
	}

	return 0;

failed:
	vf_cleanup_runtime_info(iov);
	return ret;
}

/**
 * intel_iov_query_runtime - Query IOV runtime data.
 * @iov: the IOV struct
 * @early: use early MMIO access
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_query_runtime(struct intel_iov *iov, bool early)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	vf_cleanup_runtime_info(iov);
	if (early)
		err = vf_get_runtime_info_mmio(iov);
	else
		err = vf_get_runtime_info_relay(iov);
	if (unlikely(err))
		goto failed;

	return 0;

failed:
	IOV_ERROR(iov, "Failed to get runtime info, %d\n", err);
	return err;
}

/**
 * intel_iov_query_fini - Cleanup all queried IOV data.
 * @iov: the IOV struct
 *
 * This function is for VF use only.
 */
void intel_iov_query_fini(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_vf(iov));

	vf_cleanup_runtime_info(iov);
}
