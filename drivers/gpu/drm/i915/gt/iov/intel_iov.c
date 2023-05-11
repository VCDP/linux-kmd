// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "intel_iov.h"
#include "intel_iov_abi.h"
#include "intel_iov_memirq.h"
#include "intel_iov_provisioning.h"
#include "intel_iov_query.h"
#include "intel_iov_reg.h"
#include "intel_iov_relay.h"
#include "intel_iov_service.h"
#include "intel_iov_utils.h"

const char *intel_iov_mode_to_string(enum intel_iov_mode mode)
{
	switch (mode) {
	case INTEL_IOV_MODE_NONE:
		return "none";
	case INTEL_IOV_MODE_SRIOV_PF:
		return "SR-IOV PF";
	case INTEL_IOV_MODE_SRIOV_VF:
		return "SR-IOV VF";
	default:
		return "<invalid>";
	}
}

void pf_update_status(struct intel_iov *iov, int status, const char *reason)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(!status);
	GEM_BUG_ON(iov->pf.status);

	IOV_DEBUG(iov, "%ps PF status=%d %s\n", __builtin_return_address(0),
		  status, reason);
	iov->pf.status = status;
}

static void remote_iov_init_early(struct intel_iov *iov)
{
	struct intel_iov *root = iov_get_root(iov);

	GEM_BUG_ON(!iov_is_remote(iov));
	GEM_BUG_ON(iov->__mode);
	iov->__mode = intel_iov_mode(root);
	GEM_BUG_ON(!iov->__mode);

	if (intel_iov_is_pf(iov)) {
		GEM_BUG_ON(iov->pf.provisioning);
		GEM_BUG_ON(!root->pf.provisioning);
		iov->pf.provisioning = root->pf.provisioning;
	}
}

void intel_iov_init_early(struct intel_iov *iov)
{
	GEM_BUG_ON(iov->__mode);

	if (iov_is_remote(iov))
		remote_iov_init_early(iov);

	intel_iov_relay_init_early(&iov->relay);
}

/* for usage before we have completed setup the register access via uncore */
static u32 __peek_mmio_read32(struct pci_dev *pdev, i915_reg_t reg)
{
	unsigned long offset = i915_mmio_reg_offset(reg);
	void __iomem *addr;
	u32 value;

	addr = pci_iomap_range(pdev, 0, offset, sizeof(u32));
	if (WARN(!addr, "Failed to map MMIO at %#lx\n", offset))
		return 0;

	value = readl(addr);
	pci_iounmap(pdev, addr);

	return value;
}

/* can be called without register access via uncore */
static bool __gen12_is_vf(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;
	u32 vf_cap = __peek_mmio_read32(pdev, GEN12_VF_CAP_REG);

	return vf_cap & GEN12_VF;
}

#ifdef CONFIG_PCI_IOV
static bool __wants_pf(void)
{
	/* we don't want to enable SRIOV in auto-mode */
	if (i915_modparams.enable_guc == -1)
		return false;

	return i915_modparams.enable_guc & ENABLE_GUC_SRIOV_PF;
}
#endif

static enum intel_iov_mode __detect_mode(struct intel_iov *iov)
{
	struct drm_i915_private *i915 = iov_to_i915(iov);

	if (HAS_IOV(i915)) {
		if (__gen12_is_vf(i915))
			return INTEL_IOV_MODE_SRIOV_VF;
#ifdef CONFIG_PCI_IOV
		if (dev_is_pf(iov_to_dev(iov)) && __wants_pf())
			return INTEL_IOV_MODE_SRIOV_PF;
#endif
	}
	return INTEL_IOV_MODE_NONE;
}

/**
 * intel_iov_probe - Probe I/O Virtualization mode.
 * @iov: the IOV struct
 *
 * This function should be called once and as soon as possible during
 * driver probe to detect whether we are driving a PF or VF device.
 * IOV mode detection is using MMIO register read.
 *
 * This function must be called before any calls to IS_SRIOV().
 */
void intel_iov_probe(struct intel_iov *iov)
{
	GEM_BUG_ON(iov->__mode);
	iov->__mode = __detect_mode(iov);
	GEM_BUG_ON(!iov->__mode);

	if (!intel_iov_is_enabled(iov))
		return;

	dev_info(iov_to_dev(iov), "Running in %s mode\n",
		 intel_iov_mode_string(iov));

	if (intel_iov_is_pf(iov)) {
		intel_iov_provisioning_init_early(iov);
		intel_iov_service_init_early(iov);
	}
}

/**
 * intel_iov_release - Cleanup I/O Virtualization data.
 * @iov: the IOV struct
 *
 * This function cleanup any data prepared in intel_iov_probe.
 */
void intel_iov_release(struct intel_iov *iov)
{
	if (intel_iov_is_pf(iov)) {
		intel_iov_service_release(iov);
		intel_iov_provisioning_release(iov);
	}
}

static void vf_tweak_device_info(struct intel_iov *iov)
{
	struct drm_i915_private *i915 = iov_to_i915(iov);
	struct intel_device_info *info = mkwrite_device_info(i915);

	/* Force PCH_NOOP. We have no access to display */
	i915->pch_type = PCH_NOP;
	info->pipe_mask = 0;
	info->display.has_fbc = 0;
	info->display.has_csr = 0;
	info->memory_regions &= ~(REGION_STOLEN_SMEM |
				  REGION_STOLEN_LMEM |
				  REGION_STOLEN_LMEM1 |
				  REGION_STOLEN_LMEM2 |
				  REGION_STOLEN_LMEM3);
}

/**
 * intel_iov_init_mmio - Initialize IOV based on MMIO data.
 * @iov: the IOV struct
 *
 * On VF this function will read SR-IOV INIT message from GuC.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init_mmio(struct intel_iov *iov)
{
	int ret;

	if (intel_iov_is_vf(iov)) {
		ret = intel_iov_query_bootstrap(iov);
		if (unlikely(ret))
			return ret;
		ret = intel_iov_query_runtime(iov, true);
		if (unlikely(ret))
			return ret;
		vf_tweak_device_info(iov);
	}

	return 0;
}

/**
 * intel_iov_init - Initialize IOV.
 * @iov: the IOV struct
 *
 * On PF this function performs initial partitioning of the shared resources
 * (GuC submission contexts, GuC doorbells, etc) to allow early PF provisioning
 * (that can't be changed later) and prepare starting values for delayed VFs
 * provisioning (that can be tweaked later).
 *
 * On VF this function will initialize data used by memory based interrupts.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init(struct intel_iov *iov)
{
	int err = 0;

	if (intel_iov_is_pf(iov))
		intel_iov_provisioning_init(iov);

	if (intel_iov_is_vf(iov)) {
		err = intel_iov_memirq_init(iov);
		if (unlikely(err))
			return err;
	}

	return err;
}

/**
 * intel_iov_fini - Cleanup IOV.
 * @iov: the IOV struct
 *
 * On VF this function will release data used by memory based interrupts.
 */
void intel_iov_fini(struct intel_iov *iov)
{
	if (intel_iov_is_vf(iov))
		intel_iov_memirq_fini(iov);
}

static int pf_balloon_ggtt(struct intel_iov *iov)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;
	u32 base, size;
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	base = iov->pf.provisioning->available.ggtt_base;
	size = iov->pf.provisioning->available.ggtt_size;

	/*
	 * Reserve GGTT space that was partitioned for use by VFs.
	 *
	 *      |<------------- total GGTT size ------------------------->|
	 *
	 *      +----------+--------------------------------------+-------+
	 *      | GUC | PF |//////////////////////////////////////|  GUC  |
	 *      +----------+--------------------------------------+-------+
	 *
	 *      base ----->|<---- GGTT size available for VFs --->|
	 */

	err = i915_ggtt_balloon(ggtt, base, base + size, &ggtt->balloon[0]);
	return err;
}

static void pf_deballoon_ggtt(struct intel_iov *iov)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	i915_ggtt_deballoon(ggtt, &ggtt->balloon[0]);
}

static void pf_limit_mappable_ggtt(struct intel_iov *iov)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;
	u32 host_end;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	host_end = iov->pf.provisioning->available.ggtt_base;

	ggtt->mappable_end = min_t(u64, ggtt->mappable_end, host_end);
}

static int vf_balloon_ggtt(struct intel_iov *iov)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	/*
	 * We can only use part of the GGTT as allocated by PF.
	 *
	 *      |<------------ Total GGTT size ------------------>|
	 *      |<-- VF GGTT base -->|<- size ->|
	 *
	 *      +--------------------+----------+-----------------+
	 *      |////////////////////|   block  |\\\\\\\\\\\\\\\\\|
	 *      +--------------------+----------+-----------------+
	 *
	 *      |<----- balloon ---->|<-- VF -->|<--- balloon --->|
	 */

	err = i915_ggtt_balloon(ggtt, 0, iov->vf.config.ggtt_base,
				&ggtt->balloon[0]);
	if (unlikely(err))
		return err;

	// XXX: we may want to balloon also upper range to GUC_GGTT_TOP
	//      but this means that probe should not trim ggtt.size
	//err = i915_ggtt_balloon(ggtt, iov->vf.ggtt_base + iov->vf.ggtt_size,
	//			  GUC_GGTT_TOP, &ggtt->balloon[1]);
	return 0;
}

static void vf_deballoon_ggtt(struct intel_iov *iov)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;

	i915_ggtt_deballoon(ggtt, &ggtt->balloon[1]);
	i915_ggtt_deballoon(ggtt, &ggtt->balloon[0]);
}

/**
 * intel_iov_init_ggtt - Initialize GGTT for SR-IOV.
 * @iov: the IOV struct
 *
 * On the PF this function will partition the GGTT for shared use by PF and VFs.
 * Then portion of the GGTT which was assigned as available for use by VFs will
 * marked as unavailable for PF.
 *
 * In unlikely case of any earlier or current PF setup failure, we can continue
 * but we have to make sure to update PF status to disallow creations of VFs.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init_ggtt(struct intel_iov *iov)
{
	int err;

	if (intel_iov_is_pf(iov)) {
		if (unlikely(pf_in_error(iov)))
			return 0;

		err = intel_iov_provisioning_init_ggtt(iov);
		if (unlikely(err)) {
			pf_update_status(iov, err, "ggtt");
			return 0;
		}
		err = pf_balloon_ggtt(iov);
		if (unlikely(err)) {
			pf_update_status(iov, err, "balloon");
			return 0;
		}
		pf_limit_mappable_ggtt(iov);
	}

	if (intel_iov_is_vf(iov)) {
		err = vf_balloon_ggtt(iov);
		if (unlikely(err))
			return err;
	}

	return 0;
}

/**
 * intel_iov_fini_ggtt - Cleanup SR-IOV hardware support.
 * @iov: the IOV struct
 */
void intel_iov_fini_ggtt(struct intel_iov *iov)
{
	if (intel_iov_is_pf(iov))
		pf_deballoon_ggtt(iov);

	if (intel_iov_is_vf(iov))
		vf_deballoon_ggtt(iov);
}

static void pf_enable_ggtt_guest_update(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);

	/* Guest Direct GGTT Update Enable */
	intel_uncore_write(gt->uncore, GEN12_VIRTUAL_CTRL_REG,
			   GEN12_GUEST_GTT_UPDATE_EN);
}

/**
 * intel_iov_init_hw - Initialize SR-IOV hardware support.
 * @iov: the IOV struct
 *
 * TBD
 * On PF this function updates runtime info (snapshot of registers values)
 * that will be shared with VFs.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init_hw(struct intel_iov *iov)
{
	int err;

	if (intel_iov_is_pf(iov)) {
		pf_enable_ggtt_guest_update(iov);
		intel_iov_service_update(iov);
	}

	if (intel_iov_is_vf(iov)) {
		err = intel_iov_query_runtime(iov, false);
		if (unlikely(err))
			return -EIO;
	}

	return 0;
}

/**
 * intel_iov_fini_hw - Cleanup data initialized in iov_init_hw.
 * @iov: the IOV struct
 */
void intel_iov_fini_hw(struct intel_iov *iov)
{
	if (intel_iov_is_pf(iov))
		intel_iov_service_reset(iov);

	if (intel_iov_is_vf(iov))
		intel_iov_query_fini(iov);
}

static int vf_invoke_pf_action(struct intel_iov *iov, u32 action)
{
	u32 request[VFPF_PF_ACTION_REQ_LEN];
	u32 response[VFPF_PF_ACTION_RESP_LEN];
	int ret;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	request[0] = FIELD_PREP(VFPF_PF_ACTION_REQ_0_ID, action);
	ret = intel_iov_relay_send_request(&iov->relay, 0,
					   VFPF_PF_ACTION_REQUEST,
					   request, ARRAY_SIZE(request),
					   response, ARRAY_SIZE(response));
	return ret;
}

static void __gen12vf_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	struct intel_gt *gt = ggtt->vm.gt;

	vf_invoke_pf_action(&gt->iov, VFPF_PF_ACTION_GFX_FLSH_CNT);
	vf_invoke_pf_action(&gt->iov, VFPF_PF_ACTION_GTCR);
}

static void vf_replace_ggtt_invalidate_proc(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);

	gt->ggtt->invalidate = __gen12vf_ggtt_invalidate;
}

/**
 * intel_iov_init_late - Late initialization of SR-IOV support.
 * @iov: the IOV struct
 *
 * This function continues necessary initialization of the SR-IOV
 * support in the driver and the hardware.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init_late(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);
	int err;

	if (intel_iov_is_pf(iov)) {
		err = intel_lmtt_init(&iov->pf.lmtt);
		if (unlikely(err)) {
			pf_update_status(iov, err, "lmtt");
			return 0;
		}
		/*
		 * GuC submission must be working on PF to allow VFs to work.
		 * If unavailable, mark as PF error, but it's safe to continue.
		 */
		if (unlikely(!intel_uc_uses_guc_submission(&gt->uc))) {
			pf_update_status(iov, -EIO, "GuC");
			return 0;
		}
		/*
		 * While HuC is optional for PF, it's part of the SLA for VFs.
		 * If unavailable, mark as PF error, but it's safe to continue.
		 */
		if (unlikely(!intel_uc_uses_huc(&gt->uc))) {
			pf_update_status(iov, -EIO, "HuC");
			return 0;
		}
		pf_update_status(iov, 1 + pf_get_totalvfs(iov), "READY");
	}

	if (intel_iov_is_vf(iov)) {
		/*
		 * If we try to start VF driver without GuC submission enabled,
		 * then use -EIO error to keep driver alive but without GEM.
		 */
		if (!intel_uc_uses_guc_submission(&gt->uc)) {
			dev_warn(gt->i915->drm.dev, "GuC submission is %s\n",
				 enableddisabled(false));
			return -EIO;
		}
		vf_replace_ggtt_invalidate_proc(iov);
	}

	return 0;
}

/**
 * intel_iov_fini_late - TBD.
 * @iov: the IOV struct
 *
 * TBD.
 */
void intel_iov_fini_late(struct intel_iov *iov)
{
	if (intel_iov_is_pf(iov))
		intel_lmtt_fini(&iov->pf.lmtt);
}
