// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_print.h>

#include "gt/debugfs_gt.h"
#include "debugfs_guc_log.h"
#include "intel_guc.h"
#include "gt/uc/intel_guc_ct.h"
#include "gt/uc/intel_guc_submission.h"

static int guc_info_show(struct seq_file *m, void *data)
{
	struct intel_guc *guc = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_guc_is_supported(guc))
		return -ENODEV;

	intel_guc_load_status(guc, &p);
	drm_puts(&p, "\n");
	intel_guc_log_info(&guc->log, &p);

	if (!intel_guc_submission_is_used(guc))
		return 0;

	intel_guc_log_ct_info(&guc->ct, &p);
	intel_guc_log_submission_info(guc, &p);

	return 0;
}
DEFINE_GT_DEBUGFS_ATTRIBUTE(guc_info);

static int guc_registered_contexts_show(struct seq_file *m, void *data)
{
	struct intel_guc *guc = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_guc_submission_is_used(guc))
		return -ENODEV;

	intel_guc_log_context_info(guc, &p);

	return 0;
}
DEFINE_GT_DEBUGFS_ATTRIBUTE(guc_registered_contexts);

static int guc_stall_get(void *data, u64 *val)
{
	struct intel_guc *guc = data;

	if (!intel_guc_submission_is_used(guc))
		return -ENODEV;

	*val = guc->stall_ms;

	return 0;
}

static int guc_stall_set(void *data, u64 val)
{
#define INTEL_GUC_STALL_MAX 60000 /* in milliseconds */
	struct intel_guc *guc = data;
	enum intel_guc_scheduler_mode mode;

	if (!intel_guc_submission_is_used(guc))
		return -ENODEV;

	if (val > INTEL_GUC_STALL_MAX) {
		DRM_DEBUG_DRIVER("GuC Scheduler request delay = %lld > %d, "
				 "setting delay = %d\n",
				 val, INTEL_GUC_STALL_MAX, INTEL_GUC_STALL_MAX);
		val = INTEL_GUC_STALL_MAX;
	}
	guc->stall_ms = val;

	if (val)
		mode = INTEL_GUC_SCHEDULER_MODE_STALL_IMMEDIATE;
	else
		mode = INTEL_GUC_SCHEDULER_MODE_NORMAL;

	DRM_DEBUG_DRIVER("GuC Scheduler Stall Mode = %s (%d ms delay)\n",
			 mode == INTEL_GUC_SCHEDULER_MODE_STALL_IMMEDIATE ?
			 "Immediate" : "Normal", guc->stall_ms);

	return intel_guc_set_schedule_mode(guc, mode, val);
}
DEFINE_SIMPLE_ATTRIBUTE(guc_stall_fops, guc_stall_get, guc_stall_set, "%lld\n");

void intel_guc_debugfs_register(struct intel_guc *guc, struct dentry *root)
{
	static const struct debugfs_gt_file files[] = {
		{ "guc_info", &guc_info_fops, NULL },
		{ "guc_registered_contexts", &guc_registered_contexts_fops, NULL },
		{ "guc_stall_ms", &guc_stall_fops, NULL },
	};

	if (!intel_guc_is_supported(guc))
		return;

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), guc);
	intel_guc_log_debugfs_register(&guc->log, root);
}
