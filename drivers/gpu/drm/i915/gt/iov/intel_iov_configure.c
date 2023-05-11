// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "intel_iov.h"
#include "intel_iov_configure.h"
#include "intel_iov_provisioning.h"
#include "intel_iov_utils.h"
#include "gt/intel_gt.h"
#include "gt/uc/intel_guc_ads.h"

static void pf_set_ggtt_ownership(struct intel_iov *iov, u16 num_vfs)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;
	u64 base, size;
	u16 n;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	for (n = 1; n <= num_vfs; n++) {
		/* ACHTUNG: disabled VFs have fake GGTT config */
		if (!pf_is_vf_enabled(iov, n))
			continue;
		base = iov->pf.provisioning->configs[n].ggtt_base;
		size = iov->pf.provisioning->configs[n].ggtt_size;
		i915_ggtt_set_space_owner(ggtt, n, base, size);
	}
}

static void pf_reset_ggtt_ownership(struct intel_iov *iov)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;
	u64 base, size;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	base = iov->pf.provisioning->available.ggtt_base;
	size = iov->pf.provisioning->available.ggtt_size;
	i915_ggtt_set_space_owner(ggtt, 0, base, size);
}

static void pf_free_lmem_objs(struct intel_iov *iov, u16 num_vfs)
{
	struct drm_i915_gem_object *obj;
	unsigned int n;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.lmem_objs)
		return;

	for (n = num_vfs; n > 0; n--) {
		IOV_DEBUG(iov, "free lmem%u\n", n);
		obj = fetch_and_zero(&iov->pf.lmem_objs[n]);
		if (!obj)
			continue;
		i915_gem_object_unpin_pages(obj);
		i915_gem_object_put(obj);
	}

	kfree(iov->pf.lmem_objs);
	iov->pf.lmem_objs = NULL;
}

static int pf_alloc_lmem_objs(struct intel_iov *iov, u16 num_vfs)
{
	struct intel_iov_provisioning *provisioning = iov->pf.provisioning;
	struct drm_i915_gem_object *obj;
	resource_size_t size;
	unsigned int n;
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(num_vfs > pf_get_totalvfs(iov));
	GEM_BUG_ON(iov->pf.lmem_objs);

	if (!HAS_LMEM(iov_to_i915(iov)))
		return 0;

	iov->pf.lmem_objs = kcalloc(1 + num_vfs,
				    sizeof(*iov->pf.lmem_objs), GFP_KERNEL);
	if (unlikely(!iov->pf.lmem_objs))
		return -ENOMEM;

	for (n = 1; n <= num_vfs; n++) {
		size = mul_u32_u32(provisioning->configs[n].lmem_size, SZ_1M);
		IOV_DEBUG(iov, "lmem%u %lluK\n", n, size / SZ_1K);
		if (!size)
			continue;
		GEM_BUG_ON(!IS_ALIGNED(size, SZ_2M));
		obj = intel_lmtt_create_lmem_object(&iov->pf.lmtt, size,
						    I915_BO_ALLOC_VOLATILE |
						    I915_BO_ALLOC_CHUNK_2M);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto fail;
		}
		iov->pf.lmem_objs[n] = obj;
	}

	return 0;

fail:
	pf_free_lmem_objs(iov, --n);
	return err;
}

static int pf_populate_lmtt(struct intel_iov *iov, u16 num_vfs)
{
	if (!HAS_LMEM(iov_to_i915(iov)))
		return 0;
	return intel_lmtt_create_entries(&iov->pf.lmtt, num_vfs);
}

static void pf_reset_lmtt(struct intel_iov *iov, u16 num_vfs)
{
	if (!HAS_LMEM(iov_to_i915(iov)))
		return;
	intel_lmtt_destroy_entries(&iov->pf.lmtt, num_vfs);
}

static int pf_update_guc_clients(struct intel_iov *iov, unsigned int num_vfs)
{
	struct intel_guc *guc = &iov_to_gt(iov)->uc.guc;
	intel_wakeref_t wakeref;
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	with_intel_runtime_pm(&iov_to_i915(iov)->runtime_pm, wakeref)
		err = intel_guc_ads_update_clients(guc, num_vfs);

	return err;
}

/**
 * intel_iov_enable_vfs - Enable VFs.
 * @iov: the IOV struct
 * @num_vfs: number of VFs to enable (shall not be zero)
 *
 * This function will enable specified number of VFs. Note that VFs can be
 * enabled only after successful PF initialization.
 * This function shall be called only on PF.
 *
 * Return: number of configured VFs or a negative error code on failure.
 */
int intel_iov_enable_vfs(struct intel_iov *iov, unsigned int num_vfs)
{
	struct device *dev = iov_to_dev(iov);
	struct intel_gt *gt;
	unsigned int id;
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(!iov_is_root(iov));
	GEM_BUG_ON(!num_vfs);
	IOV_DEBUG(iov, "enabling %u VFs\n", num_vfs);

	/* verify that all initialization was successfully completed */
	err = pf_get_status(iov);
	if (err < 0)
		goto fail;

	for_each_gt(iov_to_i915(iov), id, gt) {
		err = pf_alloc_lmem_objs(&gt->iov, num_vfs);
		if (unlikely(err))
			goto fail;
	}

	for_each_gt(iov_to_i915(iov), id, gt) {
		err = pf_populate_lmtt(&gt->iov, num_vfs);
		if (unlikely(err))
			goto fail_objs;
	}

	for_each_gt(iov_to_i915(iov), id, gt)
		pf_set_ggtt_ownership(&gt->iov, num_vfs);

	for_each_gt(iov_to_i915(iov), id, gt) {
		err = pf_update_guc_clients(iov, num_vfs);
		if (unlikely(err < 0))
			goto fail_ggtt;
	}

	err = pci_enable_sriov(to_pci_dev(dev), num_vfs);
	if (err < 0)
		goto fail_ggtt;

	dev_info(dev, "Enabled %u VFs\n", num_vfs);
	return num_vfs;

fail_ggtt:
	pf_reset_ggtt_ownership(iov);
fail_objs:
	pf_free_lmem_objs(iov, num_vfs);
fail:
	IOV_ERROR(iov, "Failed to enable %u VFs, (error %d)\n",
		  num_vfs, err);
	return err;
}

/**
 * intel_iov_disable_vfs - Disable VFs.
 * @iov: the IOV struct
 *
 * This function will disable all previously enabled VFs.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_disable_vfs(struct intel_iov *iov)
{
	struct device *dev = iov_to_dev(iov);
	struct pci_dev *pdev = to_pci_dev(dev);
	u16 num_vfs = pci_num_vf(pdev);
	u16 vfs_assigned = pci_vfs_assigned(pdev);
	struct intel_gt *gt;
	unsigned int id;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(!iov_is_root(iov));
	IOV_DEBUG(iov, "disabling %u VFs\n", num_vfs);

	if (vfs_assigned) {
		dev_warn(dev, "Can't disable %u VFs, %u are still assigned\n",
			 num_vfs, vfs_assigned);
		return -EPERM;
	}

	if (!num_vfs)
		return 0;

	pci_disable_sriov(pdev);

	for_each_gt(iov_to_i915(iov), id, gt) {
		pf_reset_ggtt_ownership(&gt->iov);
		pf_reset_lmtt(&gt->iov, num_vfs);
		pf_free_lmem_objs(&gt->iov, num_vfs);
	}

	dev_info(dev, "Disabled %u VFs\n", num_vfs);
	return 0;
}
