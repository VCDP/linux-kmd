// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 *
 * Functions for PSMI capture support.
 *
 * NOT_UPSTREAM: for internal use only
 */
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "i915_drv.h"

static int psmi_resize_object(struct drm_i915_private *, size_t);

/*
 * Returns an address for the capture tool to use to find start of
 * capture buffer. Capture tool requires the capability to have a
 * buffer allocated per each SMEM or LMEM region, thus we return an
 * address for each SMEM and LMEM region.
 * SMEM: capture tool reads SMEM using /dev/mem
 * LMEM: capture tool reads LMEM using /sys/class/drm/cardX/device/resourceX,
 *       where resourceX depends on which tile is used (capture_region_mask).
 */
static int psmi_debugfs_capture_addr_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *i915 = m->private;
	struct drm_i915_gem_object *obj;
	struct intel_memory_region *mr;
	enum intel_region_id id;
	struct page *page;
	u64 val;

	for_each_memory_region(mr, i915, id) {
		obj = i915->psmi.capture_obj[id];
		if (!obj)
			continue;

		if (i915_gem_object_has_struct_page(obj)) {
			page = i915_gem_object_get_page(obj, 0);
			val = page ? page_to_phys(page) : 0;
		} else {
			val = i915_gem_object_get_dma_address(obj, 0);
		}
		seq_printf(m, "%d: 0x%llx\n", id, val);
	}

	return 0;
}

/*
 * Return capture buffer size, using the size from first allocated object
 * that is found. This works because all objects must be of the same size.
 */
static int psmi_debugfs_capture_size_get(void *data, u64 *val)
{
	struct drm_i915_private *i915 = data;
	unsigned long region_id, region_mask;
	struct drm_i915_gem_object *obj;

	region_mask = i915->psmi.region_mask;
	for_each_set_bit(region_id, &region_mask,
			 ARRAY_SIZE(i915->psmi.capture_obj)) {
		obj = i915->psmi.capture_obj[region_id];
		if (obj) {
			*val = obj->base.size;
			return 0;
		}
	}

	/* no capture objects are allocated */
	*val = 0;
	return 0;
}

/*
 * Set size of PSMI capture buffer. This triggers the allocation of
 * capture buffer in each memory region as specified with prior write
 * to capture_region_mask.
 */
static int psmi_debugfs_capture_size_set(void *data, u64 val)
{
	struct drm_i915_private *i915 = data;

	/* GuC is required along with PSMI feature flag */
	if (!intel_uc_uses_guc(&i915->gt.uc) || !i915_modparams.enable_psmi)
		return -ENODEV;

	/* user must have specified at least one region */
	if (!i915->psmi.region_mask)
		return -EINVAL;

	return psmi_resize_object(i915, val);
}

static int psmi_debugfs_capture_region_mask_get(void *data, u64 *val)
{
	struct drm_i915_private *i915 = data;

	*val = i915->psmi.region_mask;
	return 0;
}

/*
 * Select LMEM regions for multi-tile devices, only allowed when buffer is
 * not currently allocated.
 */
static int psmi_debugfs_capture_region_mask_set(void *data, u64 val)
{
	unsigned long region_id, region_mask = val;
	struct drm_i915_private *i915 = data;
	u64 size = 0;

	/* GuC is required along with PSMI feature flag */
	if (!intel_uc_uses_guc(&i915->gt.uc) || !i915_modparams.enable_psmi)
		return -ENODEV;

	/* input bitmask should only contain SMEM or LMEM regions */
	if (region_mask >= REGION_STOLEN_SMEM)
		return -EINVAL;

	/* for convenience, omit regions if requested but not present */
	for_each_set_bit(region_id, &region_mask,
			 ARRAY_SIZE(i915->psmi.capture_obj))
		if (!i915->mm.regions[region_id])
			region_mask &= ~BIT(region_id);

	/* omit SMEM for devices with LMEM, as state is captured into LMEM */
	if (HAS_LMEM(i915))
		region_mask &= ~REGION_SMEM;

	/* verify that user has at least one region still set */
	if (!region_mask)
		return -EINVAL;

	/* only allow setting mask if buffer is not yet allocated */
	psmi_debugfs_capture_size_get(i915, &size);
	if (size)
		return -EBUSY;

	i915->psmi.region_mask = region_mask;
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(psmi_debugfs_capture_addr);

DEFINE_DEBUGFS_ATTRIBUTE(psmi_debugfs_capture_region_mask_fops,
			 psmi_debugfs_capture_region_mask_get,
			 psmi_debugfs_capture_region_mask_set,
			 "0x%llx\n");

DEFINE_DEBUGFS_ATTRIBUTE(psmi_debugfs_capture_size_fops,
			 psmi_debugfs_capture_size_get,
			 psmi_debugfs_capture_size_set,
			 "%lld\n");

void intel_psmi_debugfs_create(struct drm_i915_private *i915,
			       struct dentry *fs_root)
{
	debugfs_create_file("psmi_capture_addr",
			    0400, fs_root, i915,
			    &psmi_debugfs_capture_addr_fops);

	debugfs_create_file("psmi_capture_region_mask",
			    0600, fs_root, i915,
			    &psmi_debugfs_capture_region_mask_fops);

	debugfs_create_file("psmi_capture_size",
			    0600, fs_root, i915,
			    &psmi_debugfs_capture_size_fops);
}

static void psmi_free_object(struct drm_i915_gem_object *obj)
{
	if (i915_gem_object_has_pinned_pages(obj))
		i915_gem_object_unpin_pages(obj);
	__i915_gem_object_put_pages(obj);
	i915_gem_object_put(obj);
}

/*
 * Allocate GEM object for the PSMI capture buffer.
 * @obj_size: size in bytes (and is a power of 2)
 *
 * Note, we don't write any registers as the capture tool is already
 * configuring all PSMI registers itself via mmio space.
 */
static struct drm_i915_gem_object *
psmi_alloc_object(struct drm_i915_private *i915, unsigned region_id,
		  size_t obj_size)
{
	struct drm_i915_gem_object *obj;
	struct intel_memory_region *mr;
	int err;

	if (!obj_size)
		return NULL;

	/* Allocate GEM object for the capture buffer */
	if (region_id) {
		mr = i915->mm.regions[region_id];
		obj = i915_gem_object_create_region(mr, obj_size,
						    I915_BO_ALLOC_CONTIGUOUS);
	} else {
		obj = i915_gem_object_create_internal(i915, obj_size);
	}
	if (IS_ERR_OR_NULL(obj))
		goto err;
	/*
	 * object_create_internal() doesn't take flags, insert here as
	 * flag is used within pin_pages/get_pages() below
	 */
	if (!region_id)
		obj->flags |= I915_BO_ALLOC_CONTIGUOUS;

	/* Buffer written by HW, ensure stays resident */
	err = i915_gem_object_pin_pages(obj);
	if (err) {
		psmi_free_object(obj);
		obj = ERR_PTR(err);
		goto err;
	}

err:
	DRM_INFO("PSMI capture size requested: %zu bytes, allocated: %zu (%d:%u)\n",
		 obj_size, IS_ERR_OR_NULL(obj) ? 0 : obj->base.size,
		 (bool)HAS_LMEM(i915), region_id);

	return obj;
}

/*
 * Allocate PSMI capture buffer objects (via debugfs set function),
 * based on which regions the user has selected in region_mask.
 *
 * Always release/free the current buffer objects before attempting to
 * allocate new ones.  Size == 0 will free all current buffers.
 */
static int psmi_resize_object(struct drm_i915_private *i915, size_t size)
{
	unsigned long region_id, region_mask;
	struct drm_i915_gem_object *obj;
	int err = 0;

	/* page allocation order needs to be power of 2 */
	if (size && !is_power_of_2(size))
		return -EINVAL;

	intel_psmi_cleanup(i915);

	if (size) {
		region_mask = i915->psmi.region_mask;
		for_each_set_bit(region_id, &region_mask,
				 ARRAY_SIZE(i915->psmi.capture_obj)) {
			obj = psmi_alloc_object(i915, region_id, size);
			if (IS_ERR(obj)) {
				err = PTR_ERR(obj);
				break;
			}
			i915->psmi.capture_obj[region_id] = obj;
		}

		/* on error, reverse what was allocated */
		if (err)
			intel_psmi_cleanup(i915);
	}

	return err;
}

/*
 * Free PSMI capture buffer objects.
 */
void intel_psmi_cleanup(struct drm_i915_private *i915)
{
	struct drm_i915_gem_object *obj;
	struct intel_memory_region *mr;
	enum intel_region_id region_id;

	/*
	 * For total guarantee that we free all objects, iterate over known
	 * regions instead of using region_mask here.
	 */
	for_each_memory_region(mr, i915, region_id) {
		obj = i915->psmi.capture_obj[region_id];
		if (obj) {
			psmi_free_object(obj);
			i915->psmi.capture_obj[region_id] = NULL;
		}
	}
}
