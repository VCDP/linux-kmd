// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "intel_iov_sysfs.h"
#include "intel_iov_types.h"
#include "intel_iov_utils.h"

struct iov_kobj {
	struct kobject base;
	unsigned int id;
};
#define to_iov_kobj(x) container_of(x, struct iov_kobj, base)

struct iov_attr {
	struct attribute attr;
	ssize_t (*show)(struct intel_iov *iov, unsigned int id, char *buf);
	ssize_t (*store)(struct intel_iov *iov, unsigned int id,
			 const char *buf, size_t count);
};
#define to_iov_attr(x) container_of(x, struct iov_attr, attr)

static ssize_t iov_attr_show(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	struct iov_kobj *iov_kobj = to_iov_kobj(kobj);
	struct iov_attr *iov_attr = to_iov_attr(attr);
	struct device *dev = kobj_to_dev(kobj->parent->parent);
	struct intel_iov *iov = &kdev_to_i915(dev)->gt.iov;
	unsigned int id = iov_kobj->id;
	ssize_t ret = -EIO;

	if (iov_attr->show) {
		ret = iov_attr->show(iov, id, buf);
		GEM_BUG_ON(ret >= (ssize_t)PAGE_SIZE);
	}
	return ret;
}

static ssize_t iov_attr_store(struct kobject *kobj,
			      struct attribute *attr,
			      const char *buf, size_t count)
{
	struct iov_kobj *iov_kobj = to_iov_kobj(kobj);
	struct iov_attr *iov_attr = to_iov_attr(attr);
	struct device *dev = kobj_to_dev(kobj->parent->parent);
	struct intel_iov *iov = &kdev_to_i915(dev)->gt.iov;
	unsigned int id = iov_kobj->id;
	ssize_t ret = -EIO;

	if (iov_attr->store)
		ret = iov_attr->store(iov, id, buf, count);
	return ret;
}

static const struct sysfs_ops iov_sysfs_ops = {
	.show = iov_attr_show,
	.store = iov_attr_store,
};

static struct kobj_type iov_ktype = {
	.sysfs_ops = &iov_sysfs_ops,
};

#define IOV_ATTR_CONFIG_SHOW_UNIT(name, unit) \
static ssize_t name##_config_attr_show(struct intel_iov *iov,		\
				       unsigned int id, char *buf)	\
{									\
	u32 val = iov->pf.provisioning->configs[id].name;		\
									\
	return snprintf(buf, PAGE_SIZE, "%u\n", val);			\
}
#define IOV_ATTR_CONFIG_SHOW(name) IOV_ATTR_CONFIG_SHOW_UNIT(name, 1)

IOV_ATTR_CONFIG_SHOW(ggtt_size);
IOV_ATTR_CONFIG_SHOW(num_ctxs);
IOV_ATTR_CONFIG_SHOW(num_dbs);
IOV_ATTR_CONFIG_SHOW_UNIT(lmem_size, SZ_1M);

#define IOV_ATTR_CONFIG(name) \
static struct iov_attr name##_config_attr = \
	__ATTR(name, 0444, name##_config_attr_show, NULL)

IOV_ATTR_CONFIG(ggtt_size);
IOV_ATTR_CONFIG(num_ctxs);
IOV_ATTR_CONFIG(num_dbs);
IOV_ATTR_CONFIG(lmem_size);

static umode_t iov_lmem_attr_group_is_visible(struct kobject *kobj,
					      struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj->parent->parent);
	struct drm_i915_private *i915 = kdev_to_i915(dev);

	if (!HAS_LMEM(i915))
		return 0;

	return attr->mode;
}

static struct attribute *iov_config_attrs[] = {
	&ggtt_size_config_attr.attr,
	&num_ctxs_config_attr.attr,
	&num_dbs_config_attr.attr,
	NULL
};

static const struct attribute_group iov_attr_group = {
	.attrs = iov_config_attrs,
};

static struct attribute *iov_lmem_attrs[] = {
	&lmem_size_config_attr.attr,
	NULL
};

static const struct attribute_group iov_lmem_attr_group = {
	.is_visible = iov_lmem_attr_group_is_visible,
	.attrs = iov_lmem_attrs,
};

static const struct attribute_group *iov_attr_groups[] = {
	&iov_attr_group,
	&iov_lmem_attr_group,
	NULL,
};

static int pf_setup_provisioning(struct intel_iov *iov)
{
	struct device *dev = iov_to_dev(iov);
	struct kobject *dir;
	struct iov_kobj *iov_kobj, *iov_kobjs;
	unsigned int total = 1 + pf_get_totalvfs(iov);
	unsigned int n;
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(iov->pf.provisioning->sysfs.dir);
	GEM_BUG_ON(iov->pf.provisioning->sysfs.entries);

	err = i915_inject_probe_error(iov_to_i915(iov), -ENOMEM);
	if (unlikely(err))
		goto failed;

	dir = kobject_create_and_add("sriov_provisioning", &dev->kobj);
	if (unlikely(!dir)) {
		err = -ENOMEM;
		goto failed;
	}

	iov_kobjs = kcalloc(total, sizeof(*iov_kobjs), GFP_KERNEL);
	if (unlikely(!iov_kobjs)) {
		err = -ENOMEM;
		goto failed_kobjs;
	}

	for (n = 0, iov_kobj = iov_kobjs; n < total; n++, iov_kobj++) {
		iov_kobj->id = n;
		if (n) {
			err = kobject_init_and_add(&iov_kobj->base, &iov_ktype,
						   dir, "VF%u", n);
		} else {
			err = kobject_init_and_add(&iov_kobj->base, &iov_ktype,
						   dir, "PF");
		}
		if (unlikely(err))
			goto failed_kobj_n;

		err = i915_inject_probe_error(iov_to_i915(iov), -EEXIST);
		if (unlikely(err))
			goto failed_kobj_n;

		err = sysfs_create_groups(&iov_kobj->base, iov_attr_groups);
		if (unlikely(err))
			goto failed_kobj_n;
	}

	if (IS_ENABLED(CPTCFG_DRM_I915_DEBUG_GUC)) {
		const char *path = kobject_get_path(dir, GFP_KERNEL);
		IOV_DEBUG(iov, "provisioning available at %s\n", path);
		kfree(path);
	}

	iov->pf.provisioning->sysfs.dir = dir;
	iov->pf.provisioning->sysfs.entries = iov_kobjs;

	return 0;

failed_kobj_n:
	kobject_put(&iov_kobj->base);
	while (iov_kobj--, n--) {
		sysfs_remove_groups(&iov_kobj->base, iov_attr_groups);
		kobject_put(&iov_kobj->base);
	}
	kfree(iov_kobjs);
failed_kobjs:
	kobject_put(dir);
failed:
	i915_probe_error(iov_to_i915(iov),
			 "IOV sysfs provisioning setup failed! %d\n", err);
	return err;
}

static void pf_teardown_provisioning(struct intel_iov *iov)
{
	struct kobject *dir;
	struct iov_kobj *iov_kobj, *iov_kobjs;
	unsigned int total = 1 + pf_get_totalvfs(iov);
	unsigned int n;

	dir = fetch_and_zero(&iov->pf.provisioning->sysfs.dir);
	if (!dir)
		return;

	iov_kobjs = fetch_and_zero(&iov->pf.provisioning->sysfs.entries);
	for (n = 0, iov_kobj = iov_kobjs; n < total; n++, iov_kobj++) {
		sysfs_remove_groups(&iov_kobj->base, iov_attr_groups);
		kobject_put(&iov_kobj->base);
	}
	kfree(iov_kobjs);
	kobject_put(dir);
}

/**
 * intel_iov_sysfs_setup - TBD
 * @iov: the IOV struct
 *
 * TBD
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_sysfs_setup(struct intel_iov *iov)
{
	int err;

	if (!intel_iov_is_pf(iov))
		return 0;

	if (unlikely(pf_in_error(iov)))
		return 0;

	err = pf_setup_provisioning(iov);
	if (unlikely(err))
		goto failed;

	return 0;

failed:
	i915_probe_error(iov_to_i915(iov), "IOV sysfs setup failed, %d\n", err);
	return err;
}

/**
 * intel_iov_sysfs_teardown - TBD
 * @iov: the IOV struct
 *
 * TBD
 */
void intel_iov_sysfs_teardown(struct intel_iov *iov)
{
	if (!intel_iov_is_pf(iov))
		return;

	pf_teardown_provisioning(iov);
}
