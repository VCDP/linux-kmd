// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "intel_iov.h"
#include "intel_iov_error.h"
#include "intel_iov_utils.h"
#include "gem/i915_gem_object_blt.h"

/*
 * DOC: INTEL_GUC_ACTION_VF_ERROR_NOTIFICATION
 *
 *      +=========================================+
 *      | G2H REPORT VF ERROR MESSAGE PAYLOAD     |
 *      +=========================================+
 *      | 0 |  31:0 |VF number                    |
 *      +-----------------------------------------+
 *      | 1 |  31:0 |error code:                  |
 *      |   |       | - NO_ERROR(0)               |
 *      |   |       | - OTHER(1)                  |
 *      |   |       | - FLR_REQUESTED(2)          |
 *      |   |       | - BAD_REQUEST(3)            |
 *      |   |       | - FLR_COMPLETED(0x100)      |
 *      +-----------------------------------------+
 *      | 2 |  31:0 |fallback:                    |
 *      |   |       | - NONE(0)                   |
 *      |   |       | - SUSPENDED(1)              |
 *      |   |       | - STOPPED(2)                |
 *      +-----------------------------------------+
 *      | 3 |  31:0 |error data (optional)        |
 *      +=========================================+
 */

/* G2H Report VF Error sub-actions codes */
#define INTEL_GUC_VF_NO_ERROR			0x0
#define INTEL_GUC_VF_ERROR_OTHER		0x1
#define INTEL_GUC_VF_ERROR_FLR_REQUESTED	0x2
#define INTEL_GUC_VF_ERROR_BAD_REQUEST		0x3
#define INTEL_GUC_VF_ERROR_CAT_FAULT		0x4
#define INTEL_GUC_VF_ERROR_FLR_COMPLETED	0x100
#define INTEL_GUC_VF_ERROR_SUSPENDED		0x101
#define INTEL_GUC_VF_ERROR_STOPPED		0x103
#define INTEL_GUC_VF_ERROR_RESUMED		0x102

/* G2H Report VF Error sub-actions fallbacks */
#define INTEL_GUC_FALLBACK_NONE			0x0
#define INTEL_GUC_FALLBACK_VF_SUSPENDED		0x1
#define INTEL_GUC_FALLBACK_VF_STOPPED		0x2

static void pf_clear_vf_gtt_entries(struct intel_iov *iov, u16 vfid)
{
	struct intel_gt *gt = iov_to_gt(iov);
	u64 base, size;

	base = iov->pf.provisioning->configs[vfid].ggtt_base;
	size = iov->pf.provisioning->configs[vfid].ggtt_size;

	i915_ggtt_set_space_owner(gt->ggtt, vfid, base, size);
}

static void pf_clear_vf_lmem_obj(struct intel_iov *iov, u16 vfid)
{
	struct intel_gt *gt = iov_to_gt(iov);
	struct intel_context *ce = gt->engine[BCS0]->kernel_context;
	struct drm_i915_gem_object *obj;
	int err;

	obj = iov->pf.lmem_objs[vfid];
	if (!obj)
		return;

	err = i915_gem_object_fill_blt(obj, ce, 0);
	if (unlikely(err))
		IOV_ERROR(iov, "Failed to clear VF%u LMEM, %d\n", vfid, err);
}

static void pf_handle_vf_flr_completed(struct intel_iov *iov, u16 vfid)
{
	struct device *dev = iov_to_dev(iov);
	u16 num_vfs = pci_num_vf(to_pci_dev(dev));

	if (unlikely(vfid > num_vfs)) {
		IOV_DEBUG(iov, "ignored bogus/stale VF%u FLR\n", vfid);
		return;
	}

	dev_info(dev, "VF%u FLR\n", vfid);
	pf_clear_vf_gtt_entries(iov, vfid);

	if (HAS_LMEM(iov_to_i915(iov)))
		pf_clear_vf_lmem_obj(iov, vfid);
}

static int pf_handle_vf_error(struct intel_iov *iov, u16 vfid,
			      u32 error, u32 fallback, u32 data)
{
	struct device *dev = iov_to_dev(iov);

	switch (error) {
	case INTEL_GUC_VF_NO_ERROR:
		dev_info(dev, "VF%u no error (%u)\n", vfid, fallback);
		break;
	case INTEL_GUC_VF_ERROR_FLR_COMPLETED:
		pf_handle_vf_flr_completed(iov, vfid);
		break;
	default:
		dev_warn(dev, "VF%u error %u fallback %u data %#x\n",
			 vfid, error, fallback, data);
		break;
	}

	return 0;
}

/**
 * intel_iov_error_process_msg - Handle VF error messages reported by GuC.
 * @iov: the IOV struct
 * @msg: message from the GuC
 * @len: length of the message
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_error_process_msg(struct intel_iov *iov,
				const u32 *msg, u32 len)
{
	if (unlikely(!intel_iov_is_pf(iov)))
		return -EPROTO;

	if (unlikely(len < 3 || len > 4))
		return -EPROTO;

	return pf_handle_vf_error(iov, msg[0], msg[1], msg[2],
				  len > 3 ? msg[3] : 0);
}
