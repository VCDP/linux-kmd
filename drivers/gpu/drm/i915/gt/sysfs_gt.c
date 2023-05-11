// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_device.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/printk.h>
#include <linux/sysfs.h>

#include "i915_drv.h"
#include "i915_sysfs.h"
#include "intel_gt.h"
#include "intel_gt_types.h"
#include "intel_rc6.h"

#include "sysfs_gt.h"
#include "sysfs_gt_pm.h"

struct intel_gt *intel_gt_sysfs_get_drvdata(struct device *dev,
					    const char *name)
{
	struct kobject *kobj = &dev->kobj;

	/*
	 * We are interested at knowing from where the interface
	 * has been called, whether it's called from gt/ or from
	 * the parent directory.
	 * From the interface position it depends also the value of
	 * the private data.
	 * If the interface is called from gt/ then private data is
	 * of the "struct intel_gt *" type, otherwise it's * a
	 * "struct drm_i915_private *" type.
	 */
	if (!is_object_gt(kobj)) {
		struct drm_i915_private *i915 = kdev_minor_to_i915(dev);

		pr_devel_ratelimited(DEPRECATED
			"%s (pid %d) is trying to access deprecated %s "
			"sysfs control, please use use gt/gt<n>/%s instead\n",
			current->comm, task_pid_nr(current), name, name);
		return &i915->gt;
	}

	return kobj_to_gt(kobj);
}

static struct kobject *gt_get_parent_obj(struct intel_gt *gt)
{
	return &gt->i915->drm.primary->kdev->kobj;
}

static ssize_t id_show(struct device *dev,
		       struct device_attribute *attr,
		       char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);

	return snprintf(buff, PAGE_SIZE, "%u\n", gt->info.id);
}

static DEVICE_ATTR_RO(id);

static struct kobj_type sysfs_gt_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
};

void intel_gt_sysfs_register(struct intel_gt *gt)
{
	struct kobject *parent = kobject_get(gt_get_parent_obj(gt));
	int ret;

	/*
	 * We need to make things right with the
	 * ABI compatibility. The files were originally
	 * generated under the parent directory.
	 *
	 * We generate the files only for gt 0
	 * to avoid duplicates.
	 */
	if (!gt->info.id)
		intel_gt_sysfs_pm_init(gt, parent);

	ret = kobject_init_and_add(&gt->sysfs_root,
				   &sysfs_gt_ktype,
				   &gt->i915->sysfs_gt,
				   "gt%u", gt->info.id);
	if (ret) {
		drm_err(&gt->i915->drm,
			"failed to initialize gt%u sysfs root\n", gt->info.id);
		kobject_put(&gt->sysfs_root);
		return;
	}

	ret = sysfs_create_file(&gt->sysfs_root, &dev_attr_id.attr);
	if (ret)
		drm_err(&gt->i915->drm,
			"failed to create sysfs gt%u info files\n", gt->info.id);

	intel_gt_sysfs_pm_init(gt, &gt->sysfs_root);
}

void intel_gt_sysfs_unregister(struct intel_gt *gt)
{
	struct kobject *parent = gt_get_parent_obj(gt);

	if (gt->sysfs_root.state_initialized)
		kobject_put(&gt->sysfs_root);

	kobject_put(parent);
}
