// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "intel_lmtt.h"

#include "i915_drv.h"
#include "i915_scatterlist.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_object_blt.h"
#include "gem/i915_gem_region.h"
#include "gt/intel_gt.h"

static struct intel_gt *lmtt_to_gt(struct intel_lmtt *lmtt)
{
	return container_of(lmtt, struct intel_gt, iov.pf.lmtt);
}

static struct drm_i915_gem_object *
gt_object_create_lmem_fixed(struct intel_gt *gt,
			    resource_size_t size,
			    unsigned int flags)
{
	struct drm_i915_gem_object *obj;
	int err;

	size = ALIGN(size, I915_GTT_PAGE_SIZE);
	obj = intel_gt_object_create_lmem(gt, size, flags);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto err;
	}

	err = i915_gem_object_pin_pages(obj);
	if (unlikely(err))
		goto err_put;

	err = i915_gem_object_fill_blt(obj,
				       gt->engine[BCS0]->kernel_context,
				       0);
	if (unlikely(err))
		goto err_unpin;

	i915_gem_object_lock(obj);
	err = i915_gem_object_set_to_cpu_domain(obj, false);
	i915_gem_object_unlock(obj);
	if (unlikely(err))
		goto err_unpin;

	return obj;

err_unpin:
	i915_gem_object_unpin_pages(obj);
err_put:
	i915_gem_object_put(obj);
err:
	DRM_DEV_DEBUG_DRIVER(gt->i915->drm.dev, "failed %d\n", err);
	return ERR_PTR(err);
}

struct drm_i915_gem_object *
intel_lmtt_create_lmem_object(struct intel_lmtt *lmtt,
			      resource_size_t size,
			      unsigned int flags)
{
	return gt_object_create_lmem_fixed(lmtt_to_gt(lmtt), size, flags);

}

static struct intel_lmtt_pt *
lmtt_pt_alloc(struct intel_lmtt *lmtt, unsigned int level)
{
	resource_size_t pt_size = lmtt->ops->pte_size(level) *
				  lmtt->ops->pte_num(level);
	struct intel_lmtt_pt *pt;
	int err;

	pt = kzalloc(sizeof(*pt), GFP_KERNEL);
	if (!pt) {
		err = -ENOMEM;
		goto out;
	}

	if (level > 0) {
		pt->entry = kcalloc(lmtt->ops->pte_num(level), sizeof(pt),
				    GFP_KERNEL);
		if (!pt->entry) {
			err = -ENOMEM;
			goto out_pt;
		}
	}

	pt->obj = gt_object_create_lmem_fixed(lmtt_to_gt(lmtt), pt_size,
					      I915_BO_ALLOC_CONTIGUOUS |
					      I915_BO_ALLOC_VOLATILE);
	if (IS_ERR(pt->obj)) {
		err = PTR_ERR(pt->obj);
		goto out_entry;
	}

	return pt;

out_entry:
	kfree(pt->entry);
out_pt:
	kfree(pt);
out:
	return ERR_PTR(err);
}

static void
lmtt_pt_free(struct intel_lmtt_pt *pt)
{
	struct drm_i915_gem_object *obj;

	obj = fetch_and_zero(&pt->obj);
	i915_gem_object_unpin_pages(obj);
	i915_gem_object_put(obj);

	kfree(pt->entry);
	kfree(pt);
}

static int
__lmtt_alloc_range(struct intel_lmtt *lmtt, struct intel_lmtt_pt *pd,
		   unsigned int pd_level, u64 start, u64 end)
{
	unsigned int pt_level = pd_level - 1;
	u64 pte_addr_shift = 1ULL << lmtt->ops->pte_shift(pt_level);
	int ret = 0;
	void *vaddr;
	u64 offset;

	GEM_BUG_ON(pd_level == 0);

	vaddr = i915_gem_object_pin_map(pd->obj, I915_MAP_WC);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto out;
	}

	offset = start;
	while (offset < end) {
		struct intel_lmtt_pt *pt;
		u64 next, pde, pt_offset;
		unsigned int idx;

		pt = lmtt_pt_alloc(lmtt, pt_level);
		if (IS_ERR(pt)) {
			ret = PTR_ERR(pt);
			goto out_unpin;
		}
		pt_offset = i915_gem_object_lmem_offset(pt->obj);

		idx = lmtt->ops->pte_idx(offset, pd_level);
		pde = lmtt->ops->pte_encode(pt_offset,
					    LMTT_PTE_VALID,
					    pd_level);
		lmtt->ops->pte_write(vaddr, pde, idx, pd_level);
		pd->entry[idx] = pt;

		next = min(end, round_up(offset + 1, pte_addr_shift));

		if (pt_level != 0) {
			ret = __lmtt_alloc_range(lmtt, pt, pt_level,
						 offset, next);
			if (ret)
				goto out_unpin;
		}

		offset = next;
	}

out_unpin:
	i915_gem_object_unpin_map(pd->obj);
out:
	return ret;
}

static int
lmtt_alloc_range(struct intel_lmtt *lmtt, unsigned int vf, u64 offset, u64 size)
{
	struct intel_lmtt_pt *pt, *pd = lmtt->pd;
	unsigned int root_pd_level = lmtt->ops->root_pd_level();
	unsigned int pt_level = root_pd_level - 1;
	void *vaddr;
	u64 pde;
	int err;

	pt = lmtt_pt_alloc(lmtt, pt_level);
	if (IS_ERR(pt))
		return PTR_ERR(pt);

	vaddr = i915_gem_object_pin_map(pd->obj, I915_MAP_WC);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	pde = lmtt->ops->pte_encode(i915_gem_object_lmem_offset(pt->obj),
				    LMTT_PTE_VALID,
				    root_pd_level);
	lmtt->ops->pte_write(vaddr, pde, vf, root_pd_level);
	pd->entry[vf] = pt;
	i915_gem_object_unpin_map(pd->obj);

	if (pt_level != 0) {
		err = __lmtt_alloc_range(lmtt, pt, pt_level,
					 offset, offset + size);
		if (err)
			return err;
	}

	return 0;
}

static void
__lmtt_clear(struct intel_lmtt *lmtt, struct intel_lmtt_pt *pd,
	     unsigned int level)
{
	struct intel_lmtt_pt *pt;
	unsigned int i;

	if (level == 0)
		return;

	for (i = 0; i < lmtt->ops->pte_num(level); i++) {
		pt = fetch_and_zero(&pd->entry[i]);
		if (!pt)
			continue;

		__lmtt_clear(lmtt, pt, level - 1);
		lmtt_pt_free(pt);
	}
}

static void
lmtt_clear(struct intel_lmtt *lmtt, unsigned int vf)
{
	struct intel_lmtt_pt *pt, *pd = lmtt->pd;
	unsigned int root_pd_level = lmtt->ops->root_pd_level();
	unsigned int pt_level = root_pd_level - 1;
	void *vaddr;
	u64 pde = lmtt->ops->pte_encode(0, 0, root_pd_level);

	vaddr = i915_gem_object_pin_map(pd->obj, I915_MAP_WC);
	lmtt->ops->pte_write(vaddr, pde, vf, root_pd_level);
	i915_gem_object_unpin_map(pd->obj);

	pt = fetch_and_zero(&pd->entry[vf]);
	if (!pt)
		return;

	__lmtt_clear(lmtt, pt, pt_level);
	lmtt_pt_free(pt);
}

/*
 * TODO: Pull offset from iov configuration if needed (currently it's RSVD)
 */
static u64 __lmtt_offset(struct intel_lmtt *lmtt, unsigned int vf)
{
	return 0;
}

static resource_size_t __lmtt_size(struct intel_lmtt *lmtt, unsigned int vf)
{
	struct intel_gt *_gt, *gt = lmtt_to_gt(lmtt);
	struct drm_i915_gem_object *obj;
	resource_size_t size = 0;
	unsigned int id;

	for_each_gt(gt->i915, id, _gt) {
		obj = _gt->iov.pf.lmem_objs[vf];
		if (!obj)
			continue;

		size += obj->base.size;
	}
	GEM_BUG_ON(!IS_ALIGNED(size, 1ULL << lmtt->ops->pte_shift(0)));

	return size;
}

static int
__lmtt_insert_entries(struct intel_lmtt *lmtt, unsigned int vf,
		      u64 start, struct sg_table *sg)
{
	struct intel_lmtt_pt *pt, *_pt = NULL;
	struct sgt_iter iter;
	void *vaddr;
	u64 offset;

	GEM_BUG_ON(!IS_ALIGNED(start, 1ULL << lmtt->ops->pte_shift(0)));

	pt = lmtt->ops->leaf_pt(lmtt, start, vf);

	vaddr = i915_gem_object_pin_map(pt->obj, I915_MAP_WC);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	__for_each_sgt_daddr(offset, iter, sg, (1 << lmtt->ops->pte_shift(0))) {
		_pt = lmtt->ops->leaf_pt(lmtt, start, vf);
		if (pt != _pt) {
			i915_gem_object_unpin_map(pt->obj);
			pt = _pt;
			vaddr = i915_gem_object_pin_map(pt->obj, I915_MAP_WC);
			if (IS_ERR(vaddr))
				return PTR_ERR(vaddr);
		}
		lmtt->ops->pte_write(vaddr,
				lmtt->ops->pte_encode(offset,
						      LMTT_PTE_VALID, 0),
				lmtt->ops->pte_idx(start, 0),
				0);
		start += (1 << lmtt->ops->pte_shift(0));
	}
	i915_gem_object_unpin_map(pt->obj);

	return 0;
}

static int
lmtt_insert_entries(struct intel_lmtt *lmtt, unsigned int vf, u64 start)
{
	struct intel_gt *_gt, *gt = lmtt_to_gt(lmtt);
	unsigned int id;
	int err;

	/*
	 * Each tile has its own LMTT, and we need to make all objects (which
	 * are also per-tile) available.
	 */
	for_each_gt(gt->i915, id, _gt) {
		struct drm_i915_gem_object *obj = _gt->iov.pf.lmem_objs[vf];
		struct sg_table *sg;

		if (!obj)
			continue;

		sg = obj->mm.pages;

		err = __lmtt_insert_entries(lmtt, vf, start, sg);
		if (err)
			return err;

		start += obj->base.size;
	}
	GEM_BUG_ON(start != __lmtt_offset(lmtt, vf) + __lmtt_size(lmtt, vf));

	return 0;
}

static void gt_set_lmtt_dir_ptr(struct intel_gt *gt, unsigned long offset)
{
	u32 lmem_cfg = intel_uncore_read(gt->uncore, GEN12_LMEM_CFG_ADDR);

	DRM_DEV_DEBUG_DRIVER(gt->i915->drm.dev, "lmem_cfg => %#x\n", lmem_cfg);

	/* in multiples of 64KB */
	GEM_BUG_ON(!IS_ALIGNED(offset, SZ_64K));
	lmem_cfg &= ~LMTT_DIR_PTR;
	lmem_cfg |= REG_FIELD_PREP(LMTT_DIR_PTR, offset / SZ_64K);

	/*
	 * XXX LMEM_EN bit is not visible by read. Not only multigt.
	 * Will be fixed in further HW revisions, but no harm in just
	 * setting in everywhere.
	 * https://hsdes.intel.com/appstore/article/#/1808546409
	 */
	lmem_cfg |= LMEM_ENABLE;

	DRM_DEV_DEBUG_DRIVER(gt->i915->drm.dev, "lmem_cfg <= %#x\n", lmem_cfg);
	intel_uncore_write(gt->uncore, GEN12_LMEM_CFG_ADDR, lmem_cfg);
}

static int lmtt_pd_init(struct intel_lmtt *lmtt)
{
	struct intel_lmtt_pt *pd;

	GEM_BUG_ON(lmtt->ops->root_pd_level() == 0);

	pd = lmtt_pt_alloc(lmtt, lmtt->ops->root_pd_level());
	if (IS_ERR(pd))
		return PTR_ERR(pd);
	lmtt->pd = pd;

	gt_set_lmtt_dir_ptr(lmtt_to_gt(lmtt),
			    i915_gem_object_lmem_offset(lmtt->pd->obj));

	return 0;
}

static void lmtt_pd_fini(struct intel_lmtt *lmtt)
{
	if (lmtt->pd)
		lmtt_pt_free(lmtt->pd);
}

/**
 * intel_lmtt_init - Initalize LMTT allocations.
 * @lmtt: the LMTT struct
 *
 * This function allocates empty LMTT Page Directory and
 * registers it for use by GT hardware.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_lmtt_init(struct intel_lmtt *lmtt)
{
	struct intel_gt *gt = lmtt_to_gt(lmtt);
	int err;

	if (!HAS_LMEM(gt->i915))
		return 0;

	if (IS_PONTEVECCHIO(gt->i915))
		lmtt->ops = &pvc_lmtt_ops;
	else
		lmtt->ops = &ats_lmtt_ops;

	err = lmtt_pd_init(lmtt);
	if (unlikely(err))
		return err;

	return 0;
}

/**
 * intel_lmtt_fini - Cleanup LMTT allocations.
 * @lmtt: the LMTT struct
 *
 * This function shall be called only on PF.
 */
void intel_lmtt_fini(struct intel_lmtt *lmtt)
{
	struct intel_gt *gt = lmtt_to_gt(lmtt);

	if (!HAS_LMEM(gt->i915))
		return;

	lmtt_pd_fini(lmtt);
}

/**
 * intel_lmtt_create_entries - Create LMTT entries.
 * @lmtt: the LMTT struct
 * @num_vfs: number of VFs
 *
 * This function fills LMTT entries for given number of VFs.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_lmtt_create_entries(struct intel_lmtt *lmtt, unsigned int num_vfs)
{
	struct intel_gt *gt = lmtt_to_gt(lmtt);
	struct drm_i915_private *i915 = gt->i915;
	struct intel_runtime_pm *rpm = &i915->runtime_pm;
	intel_wakeref_t wakeref;
	unsigned int i;
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	GEM_BUG_ON(!HAS_LMEM(i915));

	wakeref = intel_runtime_pm_get(rpm);

	for (i = 1; i <= num_vfs; i++) {
		err = lmtt_alloc_range(lmtt, i,
				       __lmtt_offset(lmtt, i),
				       __lmtt_size(lmtt, i));
		if (err)
			goto unwind;

		err = lmtt_insert_entries(lmtt, i, __lmtt_offset(lmtt, i));
		if (err)
			goto unwind;
	}

	intel_runtime_pm_put(rpm, wakeref);
	return 0;

unwind:
	while (i--)
		lmtt_clear(lmtt, i);

	intel_runtime_pm_put(rpm, wakeref);
	return err;
}

/**
 * intel_lmtt_destroy_entries - Removed LMTT entries.
 * @lmtt: the LMTT struct
 * @num_vfs: number of VFs
 *
 * This function shall be called only on PF.
 */
void intel_lmtt_destroy_entries(struct intel_lmtt *lmtt, unsigned int num_vfs)
{
	unsigned int i;

	GEM_BUG_ON(!IS_SRIOV_PF(lmtt_to_gt(lmtt)->i915));
	GEM_BUG_ON(!HAS_LMEM(lmtt_to_gt(lmtt)->i915));

	for (i = 1; i <= num_vfs; i++)
		lmtt_clear(lmtt, i);
}
