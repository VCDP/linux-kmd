// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include "gt/uc/intel_guc.h"
#include "gt/uc/intel_guc_fwif.h"
#include "gt/intel_context.h"
#include "gt/intel_gt.h"
#include "i915_drv.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_mman.h"
#include "intel_pagefault.h"
#include "intel_uncore.h"


struct iommu_cat_err_info {
	u8 engine_class;
	u8 sw_counter;
	u8 engine_instance;
	u16 sw_ctx_id;
};

struct page_fault_info {
	u8 access_type;
	u8 fault_type;
	u8 engine_id;
	u8 source_id;
	u8 fault_lvl;
	u64 address;
};

struct recoverable_page_fault_info {
	u64 page_addr;
	u32 asid;
	u16 pdata;
	u8 vfid;
	u8 access_type;
	u8 fault_type;
	u8 fault_level;
	u8 engine_class;
	u8 engine_instance;
	u8 fault_unsuccessful;
};

enum recoverable_page_fault_type {
	FAULT_READ_NOT_PRESENT = 0x0,
	FAULT_WRITE_NOT_PRESENT = 0x1,
	FAULT_ATOMIC_NOT_PRESENT = 0x2,
	FAULT_WRITE_ACCESS_VIOLATION = 0x5,
	FAULT_ATOMIC_ACCESS_VIOLATION = 0xa,
};

/*
 * Context descriptor masks are prepared for 64 bit structure, but GuC sends
 * only upper half of this structure. So we have to create masks for
 * the 32 bit structure we get from GuC.
 */

#define ENGINE_CLASS_MASK_32 (GENMASK(GEN11_ENGINE_CLASS_SHIFT - 32 + \
				      GEN11_ENGINE_CLASS_WIDTH - 1, \
				      GEN11_ENGINE_CLASS_SHIFT - 32))
#define SW_COUNTER_MASK_32 (GENMASK(GEN11_SW_COUNTER_SHIFT -32 + \
				    GEN11_SW_COUNTER_WIDTH - 1, \
				    GEN11_SW_COUNTER_SHIFT - 32))
#define SW_CTX_ID_MASK_32 (GENMASK(GEN11_SW_CTX_ID_SHIFT - 32 + \
				   GEN11_SW_CTX_ID_WIDTH - 1, \
				   GEN11_SW_CTX_ID_SHIFT - 32))
#define ENGINE_INSTANCE_MASK_32 (GENMASK(GEN11_ENGINE_INSTANCE_SHIFT -32 + \
					 GEN11_ENGINE_INSTANCE_WIDTH - 1, \
					 GEN11_ENGINE_INSTANCE_SHIFT- 32))
#define ATS_CS_GAM_SW_CTX_ID_MASK GENMASK(15, 0)

static u8 __gen11_get_engine_class(u32 ctx_desc)
{
	return FIELD_GET(ENGINE_CLASS_MASK_32, ctx_desc);
}

static u8 __gen11_get_sw_counter(u32 ctx_desc)
{
	return FIELD_GET(SW_COUNTER_MASK_32, ctx_desc);
}

static u8 __gen11_get_engine_instance(u32 ctx_desc)
{
	return FIELD_GET(ENGINE_INSTANCE_MASK_32, ctx_desc);
}

static u16 __gen11_get_sw_ctx_id(u32 ctx_desc)
{
	return FIELD_GET(SW_CTX_ID_MASK_32, ctx_desc);
}

static u16 __ats_get_sw_ctx_id(u32 ctx_desc)
{
	return FIELD_GET(ATS_CS_GAM_SW_CTX_ID_MASK, ctx_desc);
}

static void print_cat_iommu_error(struct drm_printer *p,
				  struct iommu_cat_err_info *info)
{
	drm_printf(p, "Unexpected IOMMU catastrophic error\n"
		      "\tGuC Engine class ID: 0x%x\n"
		      "\tSW Counter: 0x%x\n"
		      "\tEngine ID: 0x%x\n"
		      "\tSW Context ID: 0x%x\n",
		      info->engine_class,
		      info->sw_counter,
		      info->engine_instance,
		      info->sw_ctx_id);
}

/*
 * DOC: INTEL_GUC_ACTION_IOMMU_CAT_ERROR_NOTIFICATION
 * For platforms: ICL, LKFR, TGLLP, SVL, RYF, DG1, RKLC, RKLGM, ADLS, ADL
 * Bspec: 18920
 *
 *      +==========================================================+
 *      | G2H REPORT IOMMU CAT ERROR MESSAGE PAYLOAD               |
 *      +==========================================================+
 *      | 0 | 31:29 |GuC engine class id                           |
 *      |   |-------+----------------------------------------------|
 *      |   | 28:23 |SW counter                                    |
 *      |   |-------+----------------------------------------------|
 *      |   |   22  |Reserved                                      |
 *      |   |-------+----------------------------------------------|
 *      |   | 21:16 |Engine instance                               |
 *      |   |-------+----------------------------------------------|
 *      |   |  15:5 |SW context id                                 |
 *      |   |-------+----------------------------------------------|
 *      |   |   4:0 |VF id                                         |
 *      +==========================================================+
 *
 * DOC: INTEL_GUC_ACTION_IOMMU_CAT_ERROR_NOTIFICATION
 * For platforms: ATS and later
 * Bspec: 54004
 *
 *      +==========================================================+
 *      | G2H REPORT IOMMU CAT ERROR MESSAGE PAYLOAD               |
 *      +==========================================================+
 *      | 0 | 31:29 |GuC engine class id                           |
 *      |   |-------+----------------------------------------------|
 *      |   | 28:23 |SW counter                                    |
 *      |   |-------+----------------------------------------------|
 *      |   |   22  |Reserved                                      |
 *      |   |-------+----------------------------------------------|
 *      |   | 21:16 |Engine instance                               |
 *      |   |-------+----------------------------------------------|
 *      |   |  15:0 |SW context id                                 |
 *      +==========================================================+
 *
 */
int intel_pagefault_process_cat_error_msg(struct drm_i915_private *i915,
					  const u32 *payload, u32 len)
{
	struct iommu_cat_err_info info = {};
	struct drm_printer p = drm_info_printer(i915->drm.dev);

	if (len < 1)
		return -EPROTO;

	info.engine_class = __gen11_get_engine_class(payload[0]);
	info.sw_counter = __gen11_get_sw_counter(payload[0]);
	info.engine_instance = __gen11_get_engine_instance(payload[0]);
	info.sw_ctx_id = HAS_DEVICE_CONTEXT_ATS(i915) ?
			 __ats_get_sw_ctx_id(payload[0]) :
			 __gen11_get_sw_ctx_id(payload[0]);

	print_cat_iommu_error(&p, &info);

	return 0;
}

static u64 __get_address(u32 fault_data0, u32 fault_data1)
{
	return ((u64)(fault_data1 & FAULT_VA_HIGH_BITS) << 44) |
	       ((u64)fault_data0 << 12);
}

static u8 __get_engine_id(u32 fault_reg_data)
{
	return GEN8_RING_FAULT_ENGINE_ID(fault_reg_data);
}

static u8 __get_source_id(u32 fault_reg_data)
{
	return RING_FAULT_SRCID(fault_reg_data);
}

static u8 __get_access_type(u32 fault_reg_data)
{
	return !!(fault_reg_data & GEN12_RING_FAULT_ACCESS_TYPE);
}

static u8 __get_fault_lvl(u32 fault_reg_data)
{
	return RING_FAULT_LEVEL(fault_reg_data);
}

static u8 __get_fault_type(u32 fault_reg_data)
{
	return GEN12_RING_FAULT_FAULT_TYPE(fault_reg_data);
}

static void print_page_fault(struct drm_printer *p,
			     struct page_fault_info *info)
{
	drm_printf(p, "Unexpected fault\n"
		      "\tAddr: 0x%08x_%08x\n"
		      "\tEngine ID: %d\n"
		      "\tSource ID: %d\n"
		      "\tType: %d\n"
		      "\tFault Level: %d\n"
		      "\tAccess type: %s\n",
		      upper_32_bits(info->address),
		      lower_32_bits(info->address),
		      info->engine_id,
		      info->source_id,
		      info->fault_type,
		      info->fault_lvl,
		      info->access_type ?
		      "Write" : "Read");
}

/*
 * DOC: INTEL_GUC_ACTION_PAGE_FAULT_NOTIFICATION
 *
 *      +==========================================================+
 *      | G2H REPORT PAGE FAULT MESSAGE PAYLOAD                    |
 *      +==========================================================+
 *      | 0 | 31:30 |Fault response:                               |
 *      |   |       | 00 - fault successful resolved               |
 *      |   |       | 01 - fault resolution is unsuccessful        |
 *      |   |-------+----------------------------------------------|
 *      |   | 29:20 |Reserved                                      |
 *      |   |-------+----------------------------------------------|
 *      |   | 19:18 |Fault type:                                   |
 *      |   |       | 00 - page not present                        |
 *      |   |       | 01 - write access violation                  |
 *      |   |-------+----------------------------------------------|
 *      |   |   17  |Access type of the memory request that fault  |
 *      |   |       | 0 - faulted access is a read request         |
 *      |   |       | 1 = faulted access is a write request        |
 *      |   |-------+----------------------------------------------|
 *      |   | 16:12 |Engine Id of the faulted memory cycle         |
 *      |   |-------+----------------------------------------------|
 *      |   |   11  |Reserved                                      |
 *      |   |-------+----------------------------------------------|
 *      |   |  10:3 |Source ID of the faulted memory cycle         |
 *      |   |-------+----------------------------------------------|
 *      |   |   2:1 |Fault level:                                  |
 *      |   |       | 00 - PTE                                     |
 *      |   |       | 01 - PDE                                     |
 *      |   |       | 10 - PDP                                     |
 *      |   |       | 11 - PML4                                    |
 *      |   |-------+----------------------------------------------|
 *      |   |     0 |Valid bit                                     |
 *      +---+-------+----------------------------------------------+
 *      | 1 |  31:0 |Fault cycle virtual address [43:12]           |
 *      +---+-------+----------------------------------------------+
 *      | 2 |  31:4 |Reserved                                      |
 *      |   |-------+----------------------------------------------|
 *      |   |   3:0 |Fault cycle virtual address [47:44]           |
 *      +==========================================================+
 */
int intel_pagefault_process_page_fault_msg(struct drm_i915_private *i915,
					   const u32 *payload, u32 len)
{
	struct page_fault_info info = {};
	struct drm_printer p = drm_info_printer(i915->drm.dev);

	if (len < 3)
		return -EPROTO;

	info.address = __get_address(payload[1], payload[2]);
	info.engine_id = __get_engine_id(payload[0]);
	info.source_id = __get_source_id(payload[0]);
	info.access_type = __get_access_type(payload[0]);
	info.fault_lvl = __get_fault_lvl(payload[0]);
	info.fault_type = __get_fault_type(payload[0]);

	print_page_fault(&p, &info);

	return 0;
}

static void print_recoverable_fault(struct drm_printer *p,
				    struct recoverable_page_fault_info *info)
{
	drm_printf(p, "\n\tASID: %d\n"
		      "\tVFID: %d\n"
		      "\tPDATA: 0x%04x\n"
		      "\tFaulted Address: 0x%08x_%08x\n"
		      "\tFaultType: %d\n"
		      "\tAccessType: %d\n"
		      "\tFaultLevel: %d\n"
		      "\tEngineClass: %d\n"
		      "\tEngineInstance: %d\n",
		      info->asid,
		      info->vfid,
		      info->pdata,
		      upper_32_bits(info->page_addr),
		      lower_32_bits(info->page_addr),
		      info->fault_type,
		      info->access_type,
		      info->fault_level,
		      info->engine_class,
		      info->engine_instance);
}

static int vma_await_bind(struct i915_active *ref)
{
	int ret = 0;

	if (rcu_access_pointer(ref->excl.fence)) {
		struct dma_fence *fence;

		rcu_read_lock();
		fence = dma_fence_get_rcu_safe(&ref->excl.fence);
		rcu_read_unlock();
		if (fence) {
			ret = dma_fence_wait(fence, true);
			if (!ret)
				ret = fence->error;
			dma_fence_put(fence);
		}
	}

	return ret;
}

static int migrate_to_lmem(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct intel_context *ce = dev_priv->engine[BCS0]->kernel_context;
	int err;
	/*
	 * FIXME: Move this to BUG_ON later when uapi enforces object alignment
	 * to 64K for objects that can reside on both SMEM and LMEM.
	 */
	if (HAS_64K_PAGES(dev_priv) &&
	    !IS_ALIGNED(obj->base.size, I915_GTT_PAGE_SIZE_64K)) {
		DRM_DEBUG_DRIVER("Cannot migrate objects of different page sizes\n");
		return -ENOTSUPP;
	}

	mutex_lock(&dev_priv->drm.struct_mutex);

	i915_gem_object_release_mmap(obj);
	GEM_BUG_ON(obj->mm.mapping);
	GEM_BUG_ON(obj->base.filp && mapping_mapped(obj->base.filp->f_mapping));

	err = i915_gem_object_migrate(obj, ce, INTEL_REGION_LMEM, true);

	mutex_unlock(&dev_priv->drm.struct_mutex);
	return err;
}

static int validate_fault(struct i915_vma *vma, enum recoverable_page_fault_type err_code)
{
	int err = 0;

	switch (err_code & 0xF) {
	case FAULT_READ_NOT_PRESENT:
		break;
	case FAULT_WRITE_NOT_PRESENT:
		if (i915_gem_object_is_readonly(vma->obj))
			err = -EACCES;
		break;
	case FAULT_ATOMIC_NOT_PRESENT:
		break;
	case FAULT_WRITE_ACCESS_VIOLATION:
		pr_err("Write Access Violation\n");
		err = -EACCES;
		break;
	case FAULT_ATOMIC_ACCESS_VIOLATION:
		pr_err("Atomic Access Violation\n");
		err = -EACCES;
		break;
	default:
		pr_err("Undefined Fault Type\n");
		err = -EACCES;
		break;
	}

	return err;
}

static struct i915_address_space *faulted_vm(struct intel_guc *guc, u32 asid)
{
	struct i915_address_space *vm;


	if (HAS_UM_QUEUES(guc_to_gt(guc)->i915)) {
		if (GEM_WARN_ON(asid >= I915_MAX_ASID))
			return NULL;
		vm = xa_load(&guc_to_gt(guc)->i915->asid_resv.xa, asid);
	} else {
		struct intel_context *ce;

		if (GEM_WARN_ON(asid >= GUC_MAX_LRC_DESCRIPTORS))
			return NULL;
		ce = xa_load(&guc->context_lookup, asid);
		if (GEM_WARN_ON(!ce))
			return NULL;
		vm = ce->vm;
	}

	return vm;
}

static int handle_i915_mm_fault(struct intel_guc *guc,
				struct recoverable_page_fault_info *info)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct i915_vma_work *work = NULL;
	struct intel_engine_cs *engine;
	struct i915_address_space *vm;
	struct i915_vma *vma = NULL;
	int err;

	vm = faulted_vm(guc, info->asid);
	if (GEM_WARN_ON(!vm))
		return -ENOENT;

	vma = i915_find_vma(vm, info->page_addr);
	if (GEM_WARN_ON(!vma))
		return -ENOENT;

	if (!i915_vma_is_persistent(vma))
		GEM_BUG_ON(!i915_vma_is_active(vma));

	err = validate_fault(vma,  (info->fault_type << 2) | info->access_type);
	if (err)
		return err;

	GEM_BUG_ON(info->engine_class > MAX_ENGINE_CLASS ||
		   info->engine_instance > MAX_ENGINE_INSTANCE);
	engine = gt->engine_class[info->engine_class][info->engine_instance];
	/*
	 * FIXME: This is under the assumption that, for platforms with multiple
	 * Blitter engines, we would reserve, BCS0 only for kernel to perform
	 * migration. We neither expect a pagefault or user work being
	 * scheduled in BCS0. This also means that, platform with single blitter
	 * engine, we should fall back to memcpy for migration. While, we
	 * finalize the design, allow migration on platforms with multiple
	 * blitter engines.
	 * FIXME: We shouldn't migrate always to lmem. There will be migration
	 * hints associated with the object or heuristics(access counter) says
	 * if we need to migrate.
	 * FIXME: We donot migrate object which has vma bound. We should support
	 * this by unbinding all vma's and later allow page fault to bind them
	 * again.
	 */
	if (!i915_gem_object_is_lmem(vma->obj) &&
	    !atomic_read(&vma->obj->bind_count) &&
	    engine->id != BCS0) {
		err = migrate_to_lmem(vma->obj);
		GEM_BUG_ON(err && i915_gem_object_is_lmem(vma->obj));
		if (i915_gem_object_is_lmem(vma->obj))
			DRM_DEBUG_DRIVER("Migrated object to LMEM\n");
		else
			DRM_DEBUG_DRIVER("Cannot migrate object to LMEM\n");
	}

	err = vma_get_pages(vma);
	if (err)
		return err;

	work = i915_vma_work();
	if (!work) {
		err = -ENOMEM;
		goto err_exit;
	}
	err = mutex_lock_interruptible(&vma->vm->mutex);
	if (err)
		goto err_fence;

	if (i915_vma_is_bound(vma, PIN_USER))
		goto err_unlock;

	atomic_inc(&vma->obj->bind_count);
	GEM_BUG_ON(!vma->pages);

	err = i915_active_acquire(&vma->active);
	if (err)
		goto err_unlock;

	err = i915_vma_bind(vma, vma->obj->cache_level, PIN_USER, work);
	if (err)
		goto err_active;

	atomic_add(I915_VMA_PAGES_ACTIVE, &vma->pages_count);
	i915_vma_unset_devicefault(vma);
	GEM_BUG_ON(!i915_vma_is_bound(vma, PIN_USER));

err_active:
	i915_active_release(&vma->active);
err_unlock:
	mutex_unlock(&vma->vm->mutex);
err_fence:
	i915_dma_fence_work_commit(work);
err_exit:
	vma_put_pages(vma);

	if (!err)
		err = vma_await_bind(&vma->active);
	GEM_WARN_ON(err);

	return err;
}

void get_fault_info(struct intel_guc_pagefault_desc *desc,
		    struct recoverable_page_fault_info *info)
{
	info->fault_level = FIELD_GET(PAGE_FAULT_DESC_FAULT_LEVEL,
				      desc->dw0);

	info->engine_class = FIELD_GET(PAGE_FAULT_DESC_ENG_CLASS,
				       desc->dw0);

	info->engine_instance = FIELD_GET(PAGE_FAULT_DESC_ENG_INSTANCE,
					  desc->dw0);

	info->pdata = FIELD_GET(PAGE_FAULT_DESC_PDATA_HI,
				desc->dw1) << PAGE_FAULT_DESC_PDATA_HI_SHIFT;
	info->pdata |= FIELD_GET(PAGE_FAULT_DESC_PDATA_LO,
				 desc->dw0);

	info->asid =  FIELD_GET(PAGE_FAULT_DESC_ASID,
				desc->dw1);

	info->vfid =  FIELD_GET(PAGE_FAULT_DESC_VFID,
				desc->dw2);

	info->access_type = FIELD_GET(PAGE_FAULT_DESC_ACCESS_TYPE,
				      desc->dw2);

	info->fault_type = FIELD_GET(PAGE_FAULT_DESC_FAULT_TYPE,
				     desc->dw2);

	info->page_addr = (u64)(FIELD_GET(PAGE_FAULT_DESC_VIRTUAL_ADDR_HI,
					  desc->dw3)) << PAGE_FAULT_DESC_VIRTUAL_ADDR_HI_SHIFT;
	info->page_addr |= FIELD_GET(PAGE_FAULT_DESC_VIRTUAL_ADDR_LO,
				     desc->dw2) << PAGE_FAULT_DESC_VIRTUAL_ADDR_LO_SHIFT;
}

int intel_pagefault_req_process_msg(struct intel_guc *guc,
				    const u32 *payload,
				    u32 len)
{
	struct drm_printer p = drm_info_printer(guc_to_gt(guc)->i915->drm.dev);
	struct intel_guc_pagefault_reply reply = {};
	struct recoverable_page_fault_info info = {};
	struct intel_guc_pagefault_desc *desc;
	int ret;

	if (unlikely(len != 4))
		return -EPROTO;

	desc = (struct intel_guc_pagefault_desc *)payload;
	get_fault_info(desc, &info);
	print_recoverable_fault(&p, &info);

	/* Wa_1409502670:ats (pre-prod) */
	if (IS_ATS_REVID(guc_to_gt(guc)->i915, ATS_REVID_A0, ATS_REVID_A0) &&
	    info.page_addr < I915_GTT_PAGE_SIZE_64K)
		goto reply_exit;

	ret = handle_i915_mm_fault(guc, &info);
	if (ret)
		info.fault_unsuccessful = 1;

	DRM_DEBUG_DRIVER("Fault response: %s\n",
			 info.fault_unsuccessful ?
			 "Unsuccessful" : "Successful");

reply_exit:
	reply.dw0 = FIELD_PREP(PAGE_FAULT_REPLY_VALID, 1) |
		FIELD_PREP(PAGE_FAULT_REPLY_SUCCESS, info.fault_unsuccessful) |
		FIELD_PREP(PAGE_FAULT_REPLY_REPLY, PAGE_FAULT_REPLY_ACCESS) |
		FIELD_PREP(PAGE_FAULT_REPLY_DESC_TYPE, FAULT_RESPONSE_DESC) |
		FIELD_PREP(PAGE_FAULT_REPLY_ASID, info.asid);

	reply.dw1 =  FIELD_PREP(PAGE_FAULT_REPLY_VFID, info.vfid) |
		FIELD_PREP(PAGE_FAULT_REPLY_ENG_INSTANCE, info.engine_instance) |
		FIELD_PREP(PAGE_FAULT_REPLY_ENG_CLASS, info.engine_class) |
		FIELD_PREP(PAGE_FAULT_REPLY_PDATA, info.pdata);

	return intel_guc_send_pagefault_reply(guc, &reply);
}

void intel_recoverable_page_fault_legacy_init(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	intel_uncore_write(uncore,
			   GEN12_MAIN_GRAPHIC_ECO_CHICKEN,
			   LEGACY_FAULT_REPORT_BLOCKING |
			   ENABLE_PAGE_FAULT_REPORT |
			   ENABLE_PAGE_FAULT_INTR_TO_GUC |
			   ENABLE_PAGE_FAULT_REPAIR);
}
