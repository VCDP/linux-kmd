// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#define HAS_GUC_MMIO_DB(i915) IS_DGFX(i915)
#define HAS_GUC_DIST_DB(i915) (INTEL_GEN(i915) >= 12 && !HAS_GUC_MMIO_DB(i915))

#define GUC_NUM_HW_DOORBELLS 256

#define GUC_MMIO_DB_BAR_OFFSET SZ_4M
#define GUC_MMIO_DB_BAR_SIZE SZ_4M

/*
 * we allocate a dummy context for GuC to look into. We only need enough memory
 * for GuC to check the engine HEAD and TAIL and find out that they're both 0
 * (i.e. no work to be done for this context). In the same object we allocate
 * space for the workqueue, the memory doorbell and the process descriptor.
 */
#define FAKE_CTX_ID(db_id) (GUC_MAX_LRC_DESCRIPTORS - 1 - (db_id))
#define FAKE_CTX_SIZE	((LRC_STATE_PN + 1) * PAGE_SIZE)

struct live_doorbells {
	struct i915_vma *vma;
	struct test_objects {
		u8 doorbell[PAGE_SIZE / 2];
		u8 lrc_desc[PAGE_SIZE / 4];
		u8 proc_desc[PAGE_SIZE / 4];
		u8 workqueue[PAGE_SIZE];
		u8 fake_ctx[FAKE_CTX_SIZE];
	} *vaddr;
	void *doorbell;
};

static inline struct guc_process_desc *get_pdesc(struct live_doorbells *arg)
{
	return (struct guc_process_desc *)arg->vaddr->proc_desc;
}

/*
 * Tell the GuC to allocate or deallocate a specific doorbell
 */
static int __guc_allocate_doorbell(struct intel_guc *guc, u32 ctx_id, u16 db_id,
				   u64 gpa, u32 gfx_addr)
{
	u32 action[] = {
		INTEL_GUC_ACTION_ALLOCATE_DOORBELL,
		ctx_id,
		db_id,
		lower_32_bits(gpa),
		upper_32_bits(gpa),
		gfx_addr
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

static int __guc_deallocate_doorbell(struct intel_guc *guc, u32 ctx_id)
{
	u32 action[] = {
		INTEL_GUC_ACTION_DEALLOCATE_DOORBELL,
		ctx_id
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

static int
__guc_register_context(struct intel_guc *guc, u32 ctx_id, u32 offset)
{
	u32 action[] = {
		INTEL_GUC_ACTION_REGISTER_CONTEXT,
		ctx_id,
		offset,
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

static int ct_rcv_ctx_dereg(struct intel_guc_ct *ct, const u32 *msg)
{
	u16 code = msg[0] >> 16;

	if (code != INTEL_GUC_ACTION_DEREGISTER_CONTEXT_DONE)
		return -ENOTSUPP; /* fall back to default handler*/

	ct->rcv_override = NULL;

	return 0;
}

static int __guc_deregister_context(struct intel_guc *guc,u32 ctx_id)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct intel_guc_ct *ct = &guc->ct;
	int ret = 0;
	u32 action[] = {
		INTEL_GUC_ACTION_DEREGISTER_CONTEXT,
		ctx_id,
	};

	if (guc->ct.rcv_override)
		return -EEXIST;

	guc->ct.rcv_override = ct_rcv_ctx_dereg;

	ret = intel_guc_send(guc, action, ARRAY_SIZE(action));
	if (ret)
		return ret;

	return wait_for(READ_ONCE(ct->rcv_override) == NULL,
			PRESI_UC_MS_TIMEOUT(i915, 1));
}

static void mark_doorbell_as_mop(struct intel_guc *guc, u64 gpa, bool owned)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	if (!IS_PRESILICON(i915) || HAS_GUC_MMIO_DB(i915))
		return;

	intel_mark_page_as_mop(i915, gpa, owned);
}

static int
__register_context(struct intel_guc *guc, struct live_doorbells *arg, u32 ctx_id)
{
	u32 base = intel_guc_ggtt_offset(guc, arg->vma);
	struct guc_lrc_desc *desc = (struct guc_lrc_desc *)arg->vaddr->lrc_desc;
	struct guc_process_desc *pdesc = get_pdesc(arg);

	BUILD_BUG_ON(sizeof(struct guc_lrc_desc) > PAGE_SIZE / 4);
	BUILD_BUG_ON(sizeof(struct guc_process_desc) > PAGE_SIZE / 4);

	/* hardcode to RCS0 */
	desc->engine_class = GUC_RENDER_ENGINE;
	desc->engine_submit_mask = 1;

	/* fill info about lrc, proc desc and workqueue */
	desc->hw_context_desc = (base + ptr_offset(arg->vaddr, fake_ctx)) | GEN8_CTX_VALID;
	desc->priority = GUC_CLIENT_PRIORITY_KMD_NORMAL;
	desc->context_flags = CONTEXT_FLAG_KMD;

	desc->process_desc = base + ptr_offset(arg->vaddr, proc_desc);
	desc->wq_addr = base + ptr_offset(arg->vaddr, workqueue);
	desc->wq_size = PAGE_SIZE;

	/* update proc desc */
	pdesc->stage_id = ctx_id;
	pdesc->wq_base_addr = desc->wq_addr;
	pdesc->wq_size_bytes = desc->wq_size;
	pdesc->priority = GUC_CLIENT_PRIORITY_KMD_NORMAL;
	pdesc->wq_status = WQ_STATUS_ACTIVE;

	return __guc_register_context(guc, ctx_id,
				      base + ptr_offset(arg->vaddr, lrc_desc));
}

static int __deregister_context(struct intel_guc *guc, u32 ctx_id)
{
	return __guc_deregister_context(guc, ctx_id);
}

void init_doorbell(struct intel_guc *guc, void *vaddr)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	/* GuC does the initialization with distributed and MMIO doorbells */
	if (!HAS_GUC_DIST_DB(i915) && !HAS_GUC_MMIO_DB(i915)) {
		struct guc_doorbell_info *db = vaddr;
		db->db_status = GUC_DOORBELL_ENABLED;
		db->cookie = 0;
	}
}

static void fini_doorbell(struct intel_guc *guc, void *vaddr)
{
	if (!HAS_GUC_MMIO_DB(guc_to_gt(guc)->i915)) {
		struct guc_doorbell_info *db = vaddr;
		db->db_status = GUC_DOORBELL_DISABLED;
	}
}

void *create_doorbell(struct intel_guc *guc, struct live_doorbells *arg,
		      u32 ctx_id, u16 db_id)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = gt->i915;
	void *vaddr;
	u64 gpa;
	u32 gfx_addr;
	int ret;

	if (HAS_GUC_MMIO_DB(i915)) {
		/*
		 * MMIO doorbells are accessible via the same bar as mmio
		 * registers and GGTT, in the 4MB-8MB range. we use the first
		 * page in the range.
		 */
		vaddr = ioremap_nocache(gt->phys_addr + GUC_MMIO_DB_BAR_OFFSET,
					PAGE_SIZE);
		if (!vaddr)
			return ERR_PTR(-EIO);

		gpa = GUC_MMIO_DB_BAR_OFFSET;
		gfx_addr = 0;
	} else {
		vaddr = arg->vaddr->doorbell;

		GEM_BUG_ON(ptr_offset(arg->vaddr, doorbell));
		gpa = sg_dma_address(arg->vma->pages->sgl);
		gfx_addr = intel_guc_ggtt_offset(guc, arg->vma);
	}

	init_doorbell(guc, vaddr);

	ret = __guc_allocate_doorbell(guc, ctx_id, db_id, gpa, gfx_addr);
	if (ret < 0) {
		fini_doorbell(guc, vaddr);
		pr_err("Couldn't create doorbell: %d\n", ret);
		return ERR_PTR(ret);
	}

	/*
	 * In distributed doorbells, guc is returning the cacheline selected
	 * by HW as part of the 7bit data from the allocate doorbell command:
	 *  bit [6]   - Cacheline allocated
	 *  bit [5:0] - Cacheline offset address
	 * (bit 5 must be zero, or our assumption of only using half a page is
	 * no longer correct).
	 */
	if (HAS_GUC_DIST_DB(i915)) {
		u32 dd_cacheline_info;
		struct guc_doorbell_info *db;

		GEM_BUG_ON(!(ret & BIT(6)));

		dd_cacheline_info = ret & ~BIT(6);
		GEM_BUG_ON(dd_cacheline_info & BIT(5));

		vaddr += dd_cacheline_info * cache_line_size();

		/* and verify db status was updated correctly by the guc fw */
		db = (struct guc_doorbell_info *)vaddr;
		GEM_BUG_ON(db->db_status != GUC_DOORBELL_ENABLED);
	}

	return vaddr;
}

static int
destroy_doorbell(struct intel_guc *guc, u32 ctx_id, u16 db_id, void *doorbell)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int ret = 0;

	fini_doorbell(guc, doorbell);

	ret = __guc_deallocate_doorbell(guc, ctx_id);
	if (ret)
		DRM_ERROR("Couldn't destroy doorbell: %d\n", ret);

	if (HAS_GUC_MMIO_DB(i915))
		iounmap((void __iomem *)doorbell);

	return ret;
}

/* Construct a Work Item and append it to the GuC's Work Queue */
static int guc_wq_item_append(struct live_doorbells *arg)
{
	/* wqi_len is in DWords, and does not include the one-word header */
	struct guc_process_desc *desc = get_pdesc(arg);
	u32 wq_off = READ_ONCE(desc->tail);
	u32 *wq = (u32 *)(arg->vaddr->workqueue + wq_off);

	/* We expect the WQ to be active if we're appending items to it */
	if (desc->wq_status != WQ_STATUS_ACTIVE) {
		pr_err("workqueue in error state!\n");
		return -EIO;
	}

	/* we're not submitting enough items to cause a wrap */
	GEM_BUG_ON(wq_off >= sizeof(arg->vaddr->workqueue) - sizeof(u32));

	*wq = WQ_TYPE_NOOP;

	/* Make the update visible to GuC */
	WRITE_ONCE(desc->tail, wq_off + sizeof(u32));

	return 0;
}

static int guc_wq_item_wait(struct live_doorbells *arg)
{
	struct drm_i915_private *i915 = arg->vma->vm->i915;
	struct guc_process_desc *desc = get_pdesc(arg);

#define done ({ intel_presi_flush_memory(i915); (READ_ONCE(desc->head) == READ_ONCE(desc->tail)); })
	return wait_for(done, PRESI_UC_MS_TIMEOUT(i915, 1));
}

static void guc_ring_memory_doorbell(void *db_vaddr)
{
	struct guc_doorbell_info *db;
	u32 cookie;

	/* pointer of current doorbell cacheline */
	db = db_vaddr;

	/*
	 * We're not expecting the doorbell cookie to change behind our back,
	 * we also need to treat 0 as a reserved value.
	 */
	cookie = READ_ONCE(db->cookie);
	WARN_ON_ONCE(xchg(&db->cookie, cookie + 1 ?: cookie + 2) != cookie);

	/* XXX: doorbell was lost and need to acquire it again */
	GEM_BUG_ON(db->db_status != GUC_DOORBELL_ENABLED);
}

#define GUC_MMIO_DOORBELL_RING_ACK	0xACEDBEEF
#define GUC_MMIO_DOORBELL_RING_NACK	0xDEADBEEF
static void guc_ring_mmio_doorbell(void *db_vaddr)
{
	u32 db_value;

	db_value = ioread32((void __iomem *)db_vaddr);

	/*
	 * The read from the doorbell page will return ack/nack. We don't remove
	 * doorbells from active clients so we don't expect to ever get a nack.
	 * XXX: if doorbell is lost, re-acquire it?
	 */
	WARN_ON(db_value == GUC_MMIO_DOORBELL_RING_NACK);
	WARN_ON(db_value != GUC_MMIO_DOORBELL_RING_ACK);
}

static void guc_ring_doorbell(struct intel_guc *guc, void* db_vaddr)
{
	if (HAS_GUC_MMIO_DB(guc_to_gt(guc)->i915))
		guc_ring_mmio_doorbell(db_vaddr);
	else
		guc_ring_memory_doorbell(db_vaddr);
}

static uint get_num_doorbells(struct intel_gt *gt)
{
	uint num_doorbells;

	if (IS_SRIOV_PF(gt->i915)) {
		num_doorbells = gt->iov.pf.provisioning->configs[0].num_dbs;
	} else if (IS_SRIOV_VF(gt->i915)) {
		num_doorbells = gt->iov.vf.config.num_dbs;
	} else if (HAS_GUC_DIST_DB(gt->i915)) {
		u32 distdbreg = intel_uncore_read(gt->uncore,
						  GEN12_DIST_DBS_POPULATED);

		u32 num_sqidi = hweight32(distdbreg & GEN12_SQIDIS_DOORBELL_EXIST);
		u32 doorbells_per_sqidi =
			((distdbreg >> GEN12_DOORBELLS_PER_SQIDI_SHIFT) &
			 GEN12_DOORBELLS_PER_SQIDI) + 1;

		num_doorbells = num_sqidi * doorbells_per_sqidi;
	} else {
		num_doorbells = GUC_NUM_HW_DOORBELLS;
	}

	return num_doorbells;
}

static void *doorbell_setup(struct intel_guc *guc, struct live_doorbells *arg,
			    u32 ctx_id, u16 db_id)
{
	void *doorbell;
	int ret;

	memset(arg->vaddr, 0, sizeof(struct test_objects));

	ret = __register_context(guc, arg, ctx_id);
	if (ret) {
		pr_err("failed to register fake ctx for db %u\n", db_id);
		return ERR_PTR(ret);
	}

	doorbell = create_doorbell(guc, arg, ctx_id, db_id);
	if (IS_ERR(doorbell)) {
		pr_err("failed to create db %u\n", db_id);
		__deregister_context(guc, db_id);
	}

	return doorbell;
}

static int
doorbell_cleanup(struct intel_guc *guc, u32 ctx_id, u16 db_id, void *doorbell)
{
	int ret, err;

	ret = destroy_doorbell(guc, ctx_id, db_id, doorbell);
	if (ret)
		pr_err("failed to destroy db %u\n", db_id);

	err = __deregister_context(guc, ctx_id);
	if (err) {
		pr_err("failed to unregister fake ctx\n");
		if (!ret)
			ret = err;
	}

	return ret;
}


static int test_doorbell(struct intel_guc *guc, struct live_doorbells *arg,\
			 u32 ctx_id, u16 db_id)
{
	void *doorbell;
	int ret;

	doorbell = doorbell_setup(guc, arg, ctx_id, db_id);
	if (IS_ERR(doorbell)) {
		ret = PTR_ERR(doorbell);
		return ret;
	}

	ret = guc_wq_item_append(arg);
	if (ret)
		goto out_cleanup;

	mark_doorbell_as_mop(guc, sg_dma_address(arg->vma->pages->sgl), true);
	guc_ring_doorbell(guc, doorbell);
	mark_doorbell_as_mop(guc, sg_dma_address(arg->vma->pages->sgl), false);

	ret = guc_wq_item_wait(arg);
	if (ret) {
		pr_err("GuC failed to process wq for db %u\n", db_id);
		goto out_cleanup;
	}

	return doorbell_cleanup(guc, ctx_id, db_id, doorbell);

out_cleanup:
	doorbell_cleanup(guc, ctx_id, db_id, doorbell);
	return ret;
}

static int live_doorbells_loop(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_guc *guc = &gt->uc.guc;
	intel_wakeref_t wakeref;
	uint num_doorbells;
	uint i;
	int ret = 0;

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	num_doorbells = get_num_doorbells(gt);
	if (num_doorbells > GUC_NUM_HW_DOORBELLS) {
		pr_err("Invalid number of doorbells: %u (HW MAX: %u)\n",
		       num_doorbells, GUC_NUM_HW_DOORBELLS);
		goto out;
	}

	for (i = 0; i < num_doorbells; i++) {
		struct live_doorbells arg;

		ret = intel_guc_allocate_and_map_vma(guc,
						     sizeof(struct test_objects),
						     &arg.vma,
						     (void **)&arg.vaddr);
		if (ret) {
			pr_err("failed to allocate vma for db test\n");
			goto out;
		}

		ret = test_doorbell(guc, &arg, FAKE_CTX_ID(i), i);

		i915_vma_unpin_and_release(&arg.vma, I915_VMA_RELEASE_MAP);

		if (ret)
			goto out;
	}

	pr_info("Successfully tested %u doorbells\n", i);

out:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	return ret;
}

int intel_guc_doorbells_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_doorbells_loop),
	};
	struct intel_gt *gt;
	unsigned int i;
	int ret = 0;

	for_each_gt(i915, i, gt) {
		if (intel_gt_is_wedged(gt))
			continue;

		if (!intel_uc_uses_guc_submission(&gt->uc))
			continue;

		ret = intel_gt_live_subtests(tests, gt);
		if (ret)
			break;
	}

	return ret;
}

