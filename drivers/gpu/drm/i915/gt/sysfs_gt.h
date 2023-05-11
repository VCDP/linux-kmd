// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __SYSFS_GT_H__
#define __SYSFS_GT_H__

#include <linux/ctype.h>

#include "intel_gt_types.h"

struct intel_gt;

static inline struct intel_gt *kobj_to_gt(struct kobject *kobj)
{
	return container_of(kobj, struct intel_gt, sysfs_root);
}

static inline bool is_object_gt(struct kobject *kobj)
{
	bool b = !strncmp(kobj->name, "gt", 2);

	GEM_BUG_ON(b && !isdigit(kobj->name[2]));

	return b;
}

void intel_gt_sysfs_register(struct intel_gt *gt);
void intel_gt_sysfs_unregister(struct intel_gt *gt);
struct intel_gt *intel_gt_sysfs_get_drvdata(struct device *dev,
					    const char *name);

#endif /* SYSFS_GT_H */
