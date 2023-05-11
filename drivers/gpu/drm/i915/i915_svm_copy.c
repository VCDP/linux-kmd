// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "gem/i915_gem_object_blt.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_engine_pool.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"

static struct i915_vma *
intel_emit_svm_copy_blt(struct intel_context *ce,
			u64 src_start, u64 dst_start, u64 buff_size)
{
	struct drm_i915_private *i915 = ce->vm->i915;
	const u32 block_size = SZ_8M; /* ~1ms at 8GiB/s preemption delay */
	struct intel_engine_pool_node *pool;
	struct i915_vma *batch;
	u64 count, rem;
	u32 size, *cmd;
	int err;

	GEM_BUG_ON(intel_engine_is_virtual(ce->engine));
	intel_engine_pm_get(ce->engine);

	if (INTEL_GEN(i915) < 8)
		return ERR_PTR(-ENOTSUPP);

	count = div_u64(round_up(buff_size, block_size), block_size);
	size = (1 + 11 * count) * sizeof(u32);
	size = round_up(size, PAGE_SIZE);
	pool = intel_engine_get_pool(ce->engine, size);
	if (IS_ERR(pool)) {
		err = PTR_ERR(pool);
		goto out_pm;
	}

	cmd = i915_gem_object_pin_map(pool->obj, I915_MAP_FORCE_WC);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto out_put;
	}

	rem = buff_size;
	do {
		size = min_t(u64, rem, block_size);
		GEM_BUG_ON(size >> PAGE_SHIFT > S16_MAX);

		if (INTEL_GEN(i915) >= 9) {
			*cmd++ = GEN9_XY_FAST_COPY_BLT_CMD | (10 - 2);
			*cmd++ = BLT_DEPTH_32 | PAGE_SIZE;
			*cmd++ = 0;
			*cmd++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
			*cmd++ = lower_32_bits(dst_start);
			*cmd++ = upper_32_bits(dst_start);
			*cmd++ = 0;
			*cmd++ = PAGE_SIZE;
			*cmd++ = lower_32_bits(src_start);
			*cmd++ = upper_32_bits(src_start);
		} else if (INTEL_GEN(i915) >= 8) {
			*cmd++ = XY_SRC_COPY_BLT_CMD |
				 BLT_WRITE_RGBA | (10 - 2);
			*cmd++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | PAGE_SIZE;
			*cmd++ = 0;
			*cmd++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
			*cmd++ = lower_32_bits(dst_start);
			*cmd++ = upper_32_bits(dst_start);
			*cmd++ = 0;
			*cmd++ = PAGE_SIZE;
			*cmd++ = lower_32_bits(src_start);
			*cmd++ = upper_32_bits(src_start);
		}

		/* Allow ourselves to be preempted in between blocks */
		*cmd++ = MI_ARB_CHECK;

		src_start += size;
		dst_start += size;
		rem -= size;
	} while (rem);

	*cmd = MI_BATCH_BUFFER_END;
	intel_gt_chipset_flush(ce->vm->gt);

	i915_gem_object_unpin_map(pool->obj);

	batch = i915_vma_instance(pool->obj, ce->vm, NULL);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_put;
	}

	err = i915_vma_pin(batch, 0, 0, PIN_USER);
	if (unlikely(err))
		goto out_put;

	batch->private = pool;
	return batch;

out_put:
	intel_engine_pool_put(pool);
out_pm:
	intel_engine_pm_put(ce->engine);
	return ERR_PTR(err);
}

int i915_svm_copy_blt(struct intel_context *ce,
		      u64 src_start, u64 dst_start, u64 size,
		      struct dma_fence **fence)
{
	struct drm_i915_private *i915 = ce->vm->i915;
	struct i915_request *rq;
	struct i915_vma *batch;
	int err;

	DRM_DEBUG_DRIVER("src_start 0x%llx dst_start 0x%llx size 0x%llx\n",
			 src_start, dst_start, size);
	mutex_lock(&i915->drm.struct_mutex);
	batch = intel_emit_svm_copy_blt(ce, src_start, dst_start, size);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_unlock;
	}

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_batch;
	}

	err = intel_emit_vma_mark_active(batch, rq);
	if (unlikely(err))
		goto out_request;

	if (ce->engine->emit_init_breadcrumb) {
		err = ce->engine->emit_init_breadcrumb(rq);
		if (unlikely(err))
			goto out_request;
	}

	err = rq->engine->emit_bb_start(rq,
					batch->node.start, batch->node.size,
					0);
out_request:
	if (unlikely(err))
		i915_request_skip(rq, err);
	else
		*fence = &rq->fence;

	i915_request_add(rq);
out_batch:
	intel_emit_vma_release(ce, batch);
out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

int i915_svm_copy_blt_wait(struct drm_i915_private *i915,
			   struct dma_fence *fence)
{
	long timeout;

	mutex_lock(&i915->drm.struct_mutex);
	timeout = i915_gem_object_wait_fence(fence,
					     I915_WAIT_INTERRUPTIBLE |
					     I915_WAIT_ALL,
					     MAX_SCHEDULE_TIMEOUT);
	mutex_unlock(&i915->drm.struct_mutex);
	return (timeout < 0) ? timeout : 0;
}
