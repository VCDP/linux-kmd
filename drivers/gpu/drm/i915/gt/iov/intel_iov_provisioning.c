// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "intel_iov.h"
#include "intel_iov_provisioning.h"
#include "intel_iov_utils.h"
#include "gt/intel_gt.h"
#include "gt/uc/intel_guc.h"

/*
 * Resource configuration for VFs provisioning is maintained in the
 * flexible array where:
 *   - entry [0] contains fixed resource config reserved for the PF,
 *   - entries [1..n] contain provisioning configs for VF1..n::
 *
 *       <--------------------------- 1 + total_vfs ----------->
 *      +-------+-------+-------+-----------------------+-------+
 *      |   0   |   1   |   2   |                       |   n   |
 *      +-------+-------+-------+-----------------------+-------+
 *      |  PF   |  VF1  |  VF2  |      ...     ...      |  VFn  |
 *      +-------+-------+-------+-----------------------+-------+
 */

/**
 * intel_iov_provisioning_init_early - Allocate structures for provisioning.
 * @iov: the IOV struct
 *
 * VFs provisioning requires some data to be stored on the PF. Allocate
 * flexible structures to hold all required information for every possible
 * VF. In case of allocation failure PF will be in error state and will not
 * be able to create VFs.
 *
 * This function can only be called on PF.
 */
void intel_iov_provisioning_init_early(struct intel_iov *iov)
{
	struct intel_iov_provisioning *provisioning;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(!iov_is_root(iov));
	GEM_BUG_ON(iov->pf.provisioning);

	provisioning = kzalloc(sizeof(struct intel_iov_provisioning) +
			       sizeof(struct intel_iov_config) *
			       (1 + pf_get_totalvfs(iov)), GFP_KERNEL);
	if (unlikely(!provisioning)) {
		pf_update_status(iov, -ENOMEM, "provisioning");
		return;
	}

	iov->pf.provisioning = provisioning;
}

/**
 * intel_iov_provisioning_release - Release structures used for provisioning.
 * @iov: the IOV struct
 *
 * Release structures used for provisioning.
 * This function can only be called on PF.
 */
void intel_iov_provisioning_release(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(!iov_is_root(iov));

	kfree(iov->pf.provisioning);
	iov->pf.provisioning = NULL;
}

static u32 pf_repartition_resource_fair(struct intel_iov *iov, u16 num_vfs,
					const char *name, u32 available)
{
	u32 fair;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(num_vfs > pf_get_totalvfs(iov));
	GEM_WARN_ON(!available);

	fair = num_vfs ? available / num_vfs : 0;

	IOV_DEBUG(iov, "config: %s %u * %u fair + %u free\n",
		  name, num_vfs, fair, available - num_vfs * fair);

	return fair;
}

static void pf_repartition_ctxs_fair(struct intel_iov *iov, u16 num_vfs)
{
	struct intel_iov_provisioning *provisioning = iov->pf.provisioning;
	u16 n, total_vfs = pf_get_totalvfs(iov);
	u32 fair;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(num_vfs > total_vfs);

	fair = pf_repartition_resource_fair(iov, num_vfs, "contexts",
					    provisioning->available.num_ctxs);

	for (n = 1; n <= total_vfs; n++)
		provisioning->configs[n].num_ctxs = pf_is_vf_enabled(iov, n) ? fair : 0;
	for (; n <= total_vfs; n++)
		provisioning->configs[n].num_ctxs = 0;
}

static void pf_partition_ctxs_once(struct intel_iov *iov)
{
	struct intel_iov_provisioning *provisioning = iov->pf.provisioning;
	u16 total_vfs = pf_get_totalvfs_masked(iov);
	u16 total_ctxs = GUC_MAX_LRC_DESCRIPTORS;
	u16 pf_ctxs;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(!total_vfs);
	GEM_BUG_ON(provisioning->available.num_ctxs);
	GEM_BUG_ON(provisioning->configs[0].num_ctxs);

	/*
	 * Initial GuC context partitioning:
	 *
	 * If PF is allowed to use GuC submission then do fair partitioning
	 * that counts PF. Otherwise, we will limit PF to use only XXX contexts.
	 * All remaining contexts will be assigned for use by VFs.
	 */
	if (pf_is_admin_only(iov)) {
		pf_ctxs = I915_NUM_ENGINES; // XXX
	} else {
		pf_ctxs = total_ctxs / (1 + total_vfs);
	}

	IOV_DEBUG(iov, "config: %s %u = %u pf + %u available\n",
		  "contexts", total_ctxs, pf_ctxs, total_ctxs - pf_ctxs);

	provisioning->configs[0].num_ctxs = pf_ctxs;
	provisioning->available.num_ctxs = total_ctxs - pf_ctxs;

	pf_repartition_ctxs_fair(iov, total_vfs);
}

static void pf_repartition_dbs_fair(struct intel_iov *iov, u16 num_vfs)
{
	struct intel_iov_provisioning *provisioning = iov->pf.provisioning;
	u16 n, total_vfs = pf_get_totalvfs(iov);
	u16 fair;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(num_vfs > total_vfs);

	fair = pf_repartition_resource_fair(iov, num_vfs, "doorbells",
					    provisioning->available.num_dbs);

	for (n = 1; n <= num_vfs; n++)
		provisioning->configs[n].num_dbs = fair;
	for (; n <= total_vfs; n++)
		provisioning->configs[n].num_dbs = 0;
}

/*
 * Initial GuC doorbells partitioning:
 *
 * PF does not use GuC doorbells right now.
 * Assign all doorbells as available for use by VFs.
 */
static void pf_partition_dbs_once(struct intel_iov *iov)
{
	struct intel_iov_provisioning *provisioning = iov->pf.provisioning;
	u16 num_vfs = pf_get_totalvfs(iov);
	u16 total_dbs = GUC_NUM_DOORBELLS;
	u16 pf_dbs = 0;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(provisioning->available.num_dbs);
	GEM_BUG_ON(provisioning->configs[0].num_dbs);

	IOV_DEBUG(iov,"config: %s %u = %u pf + %u available\n",
		  "doorbells", total_dbs, pf_dbs, total_dbs - pf_dbs);

	provisioning->configs[0].num_dbs = pf_dbs;
	provisioning->available.num_dbs = total_dbs - pf_dbs;

	pf_repartition_dbs_fair(iov, num_vfs);
}

static void pf_repartition_lmem_fair(struct intel_iov *iov, u16 num_vfs)
{
	struct intel_iov_provisioning *provisioning;
	u16 n, total_vfs = pf_get_totalvfs(iov);
	u32 available_lmem, fair; /* in MB */

	GEM_BUG_ON(!HAS_LMEM(iov_to_i915(iov)));
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(num_vfs > total_vfs);

	provisioning = iov->pf.provisioning;
	available_lmem = provisioning->available.lmem_size;
	GEM_WARN_ON(!available_lmem);

	fair = rounddown_pow_of_two(num_vfs ? available_lmem / num_vfs : 0);
	GEM_BUG_ON(num_vfs * fair > available_lmem);

	IOV_DEBUG(iov, "config: %s %u * %u fair + %u free\n",
		  "lmem [MB]", num_vfs, fair, available_lmem - num_vfs * fair);

	for (n = 1; n <= total_vfs; n++)
		provisioning->configs[n].lmem_size = pf_is_vf_enabled(iov, n) ? fair : 0;
	for (; n <= total_vfs; n++)
		provisioning->configs[n].lmem_size = 0;
}

static void pf_partition_lmem_once(struct intel_iov *iov)
{
	struct intel_iov_provisioning *provisioning = iov->pf.provisioning;
	u16 total_vfs = pf_get_totalvfs_masked(iov);
	u32 total_lmem, required_lmem, pf_lmem; /* in MB */
	struct intel_gt *gt = iov_to_gt(iov);
	struct intel_uc *uc = &gt->uc;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(!HAS_LMEM(iov_to_i915(iov)));
	GEM_BUG_ON(provisioning->available.lmem_size);
	GEM_BUG_ON(provisioning->configs[0].lmem_size);
	GEM_BUG_ON(gt->lmem->avail / SZ_1M >= U32_MAX);

	/*
	 * Initial LMEM partitioning:
	 *
	 * Some of the LMEM must be assigned to PF for mandatory GuC data.
	 * All remaining LMEM is assigned for use by VFs, unless PF is not
	 * admin-only, then additional fair chunk of LMEM is assigned to PF.
	 */
	total_lmem = gt->lmem->avail / SZ_1M;
	required_lmem = (intel_uc_estimate_fw_storage(uc) +
			 intel_guc_estimate_storage(&uc->guc) +
			 intel_engines_estimate_storage(gt)) / SZ_1M;
	required_lmem = min_t(u32, required_lmem, total_lmem);

	pf_lmem = required_lmem;
	if (!pf_is_admin_only(iov))
		pf_lmem += (total_lmem - required_lmem) / (1 + total_vfs);

	IOV_DEBUG(iov, "config: %s %u = %u pf + %u available\n",
		  "lmem [MB]", total_lmem, pf_lmem, total_lmem - pf_lmem);

	provisioning->configs[0].lmem_size = pf_lmem;
	provisioning->available.lmem_size = total_lmem - pf_lmem;

	pf_repartition_lmem_fair(iov, total_vfs);
}

/**
 * intel_iov_provisioning_init - Perform initial provisioning of the resources.
 * @iov: the IOV struct
 *
 * Some resources shared between PF and VFs need to partitioned early, as PF
 * allocation can't be changed later, only VFs allocations can be modified until
 * all VFs are enabled. Perform initial partitioning to get fixed PF resources
 * and then run early provisioning for all VFs to get fair resources for VFs.
 *
 * This function can only be called on PF.
 */
void intel_iov_provisioning_init(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov_is_root(iov))
		return;

	if (unlikely(pf_in_error(iov)))
		return;

	pf_partition_ctxs_once(iov);
	pf_partition_dbs_once(iov);

	if (HAS_LMEM(iov_to_i915(iov)))
		pf_partition_lmem_once(iov);
}

static void pf_repartition_ggtt_fair(struct intel_iov *iov, u16 num_vfs)
{
	struct intel_iov_provisioning *provisioning = iov->pf.provisioning;
	u16 n, total_vfs = pf_get_totalvfs(iov);
	u32 available_ggtt, fair;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(num_vfs > total_vfs);

	GEM_BUG_ON(!provisioning->available.ggtt_base);
	GEM_BUG_ON(!provisioning->available.ggtt_size);
	GEM_BUG_ON(provisioning->available.ggtt_base +
		   provisioning->available.ggtt_size > GUC_GGTT_TOP);
	GEM_BUG_ON(provisioning->available.ggtt_base !=
		   provisioning->configs[0].ggtt_base +
		   provisioning->configs[0].ggtt_size);

	/*
	 * Fair GGTT partitioning:
	 *
	 * After initial partitioning, we can only partition part of the GGTT
	 * that was made available for VFs provisioning. We also must take into
	 * account GGTT block size requirements defined by the GuC ABI, where
	 * each block start and size must be aligned to 2MB::
	 *
	 *      |<----------------- total GGTT size --------------------->|
	 *                       |<---------- available --------->|
	 *
	 *      +----------------+--------------------------------+-------+
	 *      |////////////////| fair | fair |  ....  | fair |xx|\\\\\\\|
	 *      +----------------+--------------------------------+-------+
	 */

	available_ggtt = provisioning->available.ggtt_size;
	fair =  round_down(num_vfs ? available_ggtt / num_vfs : 0, SZ_2M);

	IOV_DEBUG(iov, "config: GGTT %u * %uM fair, %uM free\n", num_vfs,
		  fair / SZ_1M, (available_ggtt - num_vfs * fair) / SZ_1M);

	for (n = 1; n <= total_vfs; n++) {
		/* no holes, this config starts where previous config ends */
		provisioning->configs[n].ggtt_base =
			provisioning->configs[n - 1].ggtt_base +
			provisioning->configs[n - 1].ggtt_size;
		provisioning->configs[n].ggtt_size = fair;

		/* ACHTUNG: disabled VFs must have fake GGTT config to make GuC happy */
		if (!pf_is_vf_enabled(iov, n)) {
			provisioning->configs[n].ggtt_base = provisioning->configs[n - 1].ggtt_base;
			provisioning->configs[n].ggtt_size = provisioning->configs[n - 1].ggtt_size;
		}

		GEM_BUG_ON(provisioning->configs[n].ggtt_base +
			   provisioning->configs[n].ggtt_size >
			   provisioning->available.ggtt_base +
			   provisioning->available.ggtt_size);
	}
	for (; n <= total_vfs; n++) {
		provisioning->configs[n].ggtt_base = 0;
		provisioning->configs[n].ggtt_size = 0;
	}
}

static int pf_partition_ggtt_once(struct intel_iov *iov)
{
	struct intel_iov_provisioning *provisioning = iov->pf.provisioning;
	struct intel_gt *gt = iov_to_gt(iov);
	struct intel_guc *guc = &gt->uc.guc;
	struct i915_ggtt *ggtt = gt->ggtt;
	u16 total_vfs = pf_get_totalvfs_masked(iov);
	u32 wopcm_size = intel_wopcm_guc_size(&gt->i915->wopcm);
	u64 required, usable;
	u32 fair, pf_ggtt;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(!total_vfs);
	GEM_BUG_ON(!ggtt->vm.total);

	GEM_BUG_ON(provisioning->available.ggtt_base);
	GEM_BUG_ON(provisioning->available.ggtt_size);
	GEM_BUG_ON(provisioning->configs[0].ggtt_base);
	GEM_BUG_ON(provisioning->configs[0].ggtt_size);

	/*
	 * Initial GGTT partitioning:
	 *
	 * We want to partition GGTT in such a way that VFs could fully use it
	 * with the GuC. However, due to GuC address space limitation, not all
	 * GGTT is accessible for GuC. We need to exclude WOPCM and GUC_TOP.
	 *
	 * Additionally, part of the GGTT will be used by the PF to hold some
	 * required by GuC data structures (ADS, log, CTBs). We will not count
	 * that size during first initial fair partitioning of usable between
	 * PF and VFs and always explicitly add that space for PF::
	 *
	 *      |<----------------- total GGTT size --------------------->|
	 *      |Wopcm|<------------------ usable --------------->|GUC_TOP|
	 *                     <--- available for provisioning -->
	 *
	 *      +-----+-------+-----------------------------------+-------+
	 *      |/////|GuC ADS|                                   |\\\\\\\|
	 *      +-----+-------+-----------------------------------+-------+
	 *
	 * Finally, due to GGTT block size requirements defined by the GuC ABI,
	 * where each block start and size must be aligned to 2MB, and to get
	 * optimized fair VF's block size, we may make PF block slightly smaller
	 * (depending on PF use model).
	 *
	 *             <---- PF ----->|<--- available for VFs --->
	 *      +-----+---------------+---------------------------+-------+
	 *      |/////|GuC ADS + fair |                           |\\\\\\\|
	 *      +-----+---------------+---------------------------+-------+
	 */

	/* We need WOPCM size, but it won't be set if firmware is missing */
	if (unlikely(!wopcm_size))
		return -ENODATA;

	/* We must skip WOPCM, add ADS/LOG/CTB plus some safe buffer */
	required = wopcm_size +
		   intel_guc_estimate_storage(guc) +
		   intel_engines_estimate_storage(gt);

	GEM_BUG_ON(required > GUC_GGTT_TOP);
	usable = GUC_GGTT_TOP - round_up(required, SZ_2M);

	if (pf_is_admin_only(iov)) {
		fair = div_u64(usable, total_vfs);
		pf_ggtt = round_up(required, SZ_2M);
	} else {
		fair = div_u64(usable, 1 + total_vfs);
		pf_ggtt = round_down(required + fair, SZ_2M);
	}

	IOV_DEBUG(iov, "config: GGTT %uM required, %uM usable\n",
		  (u32)required / SZ_1M, (u32)usable / SZ_1M);
	IOV_DEBUG(iov, "config: GGTT %uM pf, %uM available\n",
		  pf_ggtt / SZ_1M, (GUC_GGTT_TOP - pf_ggtt) / SZ_1M);

	/* for PF we must explicitly skip WOPCM region */
	provisioning->configs[0].ggtt_base = wopcm_size;
	provisioning->configs[0].ggtt_size = pf_ggtt - wopcm_size;

	/* we can only provision VFs up to GUC_GGTT_TOP */
	provisioning->available.ggtt_base = pf_ggtt;
	provisioning->available.ggtt_size = GUC_GGTT_TOP - pf_ggtt;

	pf_repartition_ggtt_fair(iov, total_vfs);
	return 0;
}

/**
 * intel_iov_provisioning_init_ggtt - Perform initial provisioning of the GGTT
 * @iov: the IOV struct
 *
 * GGTT is shared between PF and VFs and need to partitioned early, as PF GGTT
 * allocation can't be changed later, only VFs GGTT allocations can be modified
 * until all VFs are enabled. Perform initial GGTT partitioning to get fixed PF
 * partition and then run early GGTT provisioning for all VFs to get fair GGTT
 * for VFs.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_provisioning_init_ggtt(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(pf_in_error(iov));

	if (!iov_is_root(iov))
		return 0;

	return pf_partition_ggtt_once(iov);
}

/*
 * intel_iov_provisioning_get_config - Get provisioning configuration.
 * @iov: the IOV struct
 * @id: VF identifier (0=PF, 1=VF1, 2=VF2,...)
 *
 * Provisioning configurations are maintained locally by the PF.
 * Use this function to get pointer to the configuration of the specific VF.
 *
 * This function can only be called on PF.
 *
 * Return: pointer to configuration struct or ERR_PTR on failure:
 * * -ESTALE when PF is already in error state
 * * -ENODATA when returned client configuration is empty
 */
const struct intel_iov_config *
intel_iov_provisioning_get_config(struct intel_iov *iov, unsigned int id)
{
	struct intel_iov_config *config;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(id > pf_get_totalvfs(iov));

	if (unlikely(pf_in_error(iov)))
		return ERR_PTR(-ESTALE);

	config = &iov->pf.provisioning->configs[id];

	if (!config->ggtt_size)
		config = ERR_PTR(-ENODATA);

	return config;
}
