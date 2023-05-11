// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/rwsem.h>
#include <linux/xarray.h>
#include <linux/fs.h>
#include <generated/utsrelease.h>

#include <drm/intel_iaf_platform.h>

#include "debugfs.h"
#include "fw.h"
#include "mbdb.h"
#include "mbox.h"
#include "mei_iaf_user.h"
#include "netlink.h"
#include "port.h"
#include "routing_engine.h"
#include "routing_event.h"
#include "routing_p2p.h"
#ifdef SELFTESTS
#include "selftests/selftest.h"
#endif
#include "iaf_drv.h"

#define MODULEDETAILS "Intel Corp. Intel fabric Driver"

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel Corp. Intel fabric Driver");
MODULE_SUPPORTED_DEVICE(DRIVER_NAME);

/* xarray of IAF devices */
static DEFINE_XARRAY_ALLOC(intel_fdevs);

/* Protects routable_list access and data with shared/excl semantics */
DECLARE_RWSEM(routable_lock);
LIST_HEAD(routable_list);

/*
 * Used for creating unique xarray hash.  Products need to be defined
 * in the intel_iaf_platform.h file.
 */
#define PRODUCT_SHIFT 16

#define SUB_DEVICE_NODE "sd.%d"
#define SUB_DEVICE_NODE_SIZE 8
#define FABRIC_ID_NODE_NAME "fabric_id"

#define FW_VERSION_FILE_NAME "fw_version"
#define FW_VERSION_BUF_SIZE 256
#define SWITCHINFO_FILE_NAME "switchinfo"
#define SWITCHINFO_BUF_SIZE 256

#define RELEASE_TIMEOUT (HZ * 60)

static void fdev_release(struct kref *kref)
{
	struct fdev *dev = container_of(kref, struct fdev, refs);

	xa_erase(&intel_fdevs, dev->fabric_id);
	complete(&dev->fdev_released);
}

void fdev_put(struct fdev *dev)
{
	kref_put(&dev->refs, fdev_release);
}

static struct fdev *fdev_get(struct fdev *dev)
{
	if (dev && kref_get_unless_zero(&dev->refs))
		return dev;

	return NULL;
}

int fdev_insert(struct fdev *dev)
{
	int err;

	kref_init(&dev->refs);
	init_completion(&dev->fdev_released);

	err = xa_insert(&intel_fdevs, dev->fabric_id, dev, GFP_KERNEL);
	if (err)
		dev_warn(fdev_dev(dev), "fabric_id 0x%08x already in use\n",
			 dev->fabric_id);

	return err;
}

int fdev_process_each(fdev_process_each_cb_t cb, void *args)
{
	struct fdev *dev;
	unsigned long i;

	xa_lock(&intel_fdevs);
	xa_for_each(&intel_fdevs, i, dev) {
		int ret;

		if (!fdev_get(dev))
			continue;

		xa_unlock(&intel_fdevs);
		ret = cb(dev, args);
		fdev_put(dev);
		if (ret)
			return ret;

		xa_lock(&intel_fdevs);
	}
	xa_unlock(&intel_fdevs);

	return 0;
}

static void fdev_wait_on_release(struct fdev *dev)
{
	fdev_put(dev);

	wait_for_completion_killable_timeout(&dev->fdev_released,
					     RELEASE_TIMEOUT);
}

/*
 * Returns with reference count for the fdev incremented when found.
 * It is the callers responsibility to decrement the reference count.
 */
struct fdev *fdev_find(u32 fabric_id)
{
	struct fdev *dev;

	xa_lock(&intel_fdevs);
	dev = fdev_get(xa_load(&intel_fdevs, fabric_id));
	xa_unlock(&intel_fdevs);

	return dev;
}

struct fdev *fdev_find_by_sd_guid(u64 guid)
{
	struct fdev *dev, *referenced_dev;
	unsigned long d, s;

	xa_lock(&intel_fdevs);

	/* dev becomes NULL if xa_for_each() completes */
	xa_for_each(&intel_fdevs, d, dev)
		for (s = 0; s < dev->pd->sd_cnt; ++s)
			if (dev->sd[s].guid == guid)
				goto end_iteration;

end_iteration:

	referenced_dev = fdev_get(dev);

	xa_unlock(&intel_fdevs);

	return referenced_dev;
}

struct fsubdev *find_sd_id(u32 fabric_id, u8 sd_index)
{
	struct fdev *dev = fdev_find(fabric_id);

	if (dev) {
		if (sd_index < dev->pd->sd_cnt)
			return &dev->sd[sd_index];

		fdev_put(dev);
	}

	return ERR_PTR(-ENXIO);
}

struct fsubdev *find_routable_sd(u64 guid) __must_hold(&routable_lock)
{
	struct fsubdev *sd = NULL;

	list_for_each_entry(sd, &routable_list, routable_link)
		if (sd->guid == guid)
			return sd;

	return NULL;
}

static irqreturn_t handle_iaf_irq(int irq, void *arg)
{
	struct fsubdev *sd = arg;

	return mbdb_handle_irq(sd);
}

static struct query_info *handle_query(void *handle, u32 fabric_id)
{
	struct fdev *src, *dst;
	struct query_info *qi;

	/*
	 * src fdev is guaranteed to never be removed during this invocation by
	 * the caller
	 */
	src = handle;

	dst = fdev_find(fabric_id);
	if (!dst)
		return ERR_PTR(-ENODEV);

	qi = kmalloc(struct_size(qi, sd2sd, src->pd->sd_cnt * dst->pd->sd_cnt),
		     GFP_KERNEL);
	if (qi) {
		qi->src_cnt = src->pd->sd_cnt;
		qi->dst_cnt = dst->pd->sd_cnt;
		routing_p2p_lookup(src, dst, qi);
	} else {
		qi = ERR_PTR(-ENOMEM);
	}

	fdev_put(dst);

	return qi;
}

static int handle_iaf_event(void *handle, enum iaf_parent_event event)
{
	return 0;
}

static const struct iaf_ops iaf_ops = {
	.connectivity_query = handle_query,
	.parent_event = handle_iaf_event,
};

static ssize_t display_fw_version(struct file *fp, char __user *user_buffer,
				  size_t count, loff_t *position)
{
	struct fsubdev *sd = fp->private_data;
	struct mbdb_op_fw_version_rsp fw_version;
	int buf_size = FW_VERSION_BUF_SIZE;
	char *buf;
	int buf_offset;
	int ret;

	ret = ops_fw_version(sd, &fw_version, NULL, NULL);
	if (ret)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return 0;

	buf_offset = snprintf(buf, buf_size, "Firmware Version Info:\n");
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "    MBox Version:  %d\n",
			       fw_version.mbox_version);
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "    Environment:   %s%s\n",
			       (fw_version.environment & FW_VERSION_ENV_BIT)
			       ? "run-time" : "bootloader",
			       (fw_version.environment & FW_VERSION_INIT_BIT)
			       ? ", ready" : "");
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "    FW Version:    %s\n",
			       fw_version.fw_version_string);
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "    OPs supported: 0x%llx%016llx%016llx%016llx\n",
			       fw_version.supported_opcodes[3],
			       fw_version.supported_opcodes[2],
			       fw_version.supported_opcodes[1],
			       fw_version.supported_opcodes[0]);

	ret = simple_read_from_buffer(user_buffer, count, position, buf,
				      buf_offset);

	kfree(buf);

	return ret;
}

static const struct file_operations fw_version_fops = {
	.open = simple_open,
	.read = display_fw_version,
	.llseek = default_llseek,
};

static ssize_t display_switchinfo(struct file *fp, char __user *user_buffer,
				  size_t count, loff_t *position)
{
	struct fsubdev *sd = fp->private_data;
	struct mbdb_op_switchinfo switchinfo;
	int buf_size = SWITCHINFO_BUF_SIZE;
	char *buf;
	int buf_offset;
	int ret;

	ret = ops_switchinfo_get(sd, &switchinfo, NULL, NULL);
	if (ret)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return 0;

	buf_offset = snprintf(buf, buf_size, "IAF GUID = 0x%0llx\n",
			      switchinfo.guid);
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "Num Ports = %d\n", switchinfo.num_ports);
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "SLL = %ld\n", switchinfo.slt_psc_ep0 &
			       SLT_PSC_EP0_SWITCH_LIFETIME);
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "portStateChange = %d\n",
			       switchinfo.slt_psc_ep0 &
			       SLT_PSC_EP0_PORT_STATE_CHANGE ? 1 : 0);
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "enhancedPort0 = %d\n",
			       switchinfo.slt_psc_ep0 &
			       SLT_PSC_EP0_ENHANCED_PORT_0 ? 1 : 0);
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "routingModeSupported = %d\n",
			       switchinfo.routing_mode_supported);
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "routingModeEnabled = %d\n",
			       switchinfo.routing_mode_enabled);
	buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
			       "lftTop = 0x%x\n", switchinfo.lft_top);

	ret = simple_read_from_buffer(user_buffer, count, position, buf,
				      buf_offset);

	kfree(buf);

	return ret;
}

static const struct file_operations switchinfo_fops = {
	.open = simple_open,
	.read = display_switchinfo,
	.llseek = default_llseek,
};

static int add_subdevice(struct fsubdev *sd, struct fdev *dev, int index)
{
	struct resource *res;
	int err;
	char name[SUB_DEVICE_NODE_SIZE];
	struct dentry *sd_dir_node;

	sd->fdev = dev;

	res = platform_get_resource(dev->pdev, IORESOURCE_MEM, index);
	sd->csr_base = ioremap_nocache(res->start, resource_size(res));
	if (!sd->csr_base) {
		sd_err(sd, "Unable to map resource %pR to kvirt\n", res);
		return -ENOMEM;
	}
	sd_info(sd, "mapped resource %pR to mb_base %p\n", res, sd->csr_base);

	sd->id = build_sd_id(dev->pd->index, index);

	mutex_init(&sd->pm_work_lock);
	sd->ok_to_schedule_pm_work = false;

	res = platform_get_resource(dev->pdev, IORESOURCE_IRQ, index);
	sd_info(sd, "IRQ resource %pR\n", res);

	sd->irq = res->start;
	err = request_irq(sd->irq, handle_iaf_irq, 0, "intel_sd_irq", sd);
	if (err) {
		sd_err(sd, "failed to request_irq %d  err: %d\n", sd->irq, err);
		iounmap(sd->csr_base);
		return err;
	}

	snprintf(name, sizeof(name), SUB_DEVICE_NODE, sd_index(sd));
	sd_dir_node = debugfs_add_dir_node(name, dev->dir_node);
	debugfs_add_file_node(FW_VERSION_FILE_NAME, 0400, sd_dir_node, sd,
			      &fw_version_fops);
	debugfs_add_file_node(SWITCHINFO_FILE_NAME, 0400, sd_dir_node, sd,
			      &switchinfo_fops);

	err = create_mbdb(sd, sd_dir_node);

	if (err) {
		sd_err(sd, "Failed to create mailbox\n");
		free_irq(sd->irq, sd);
		iounmap(sd->csr_base);
		return err;
	}

	routing_sd_init(sd);

	sd_info(sd, "Adding IAF subdevice: %d\n", index);

	INIT_LIST_HEAD(&sd->routable_link);

	return 0;
}

static void remove_subdevice(struct fsubdev *sd)
{
	destroy_fports(sd);
	cancel_work_sync(&sd->fw_work);
	destroy_mbdb(sd);
	free_irq(sd->irq, sd);
	iounmap(sd->csr_base);
	sd_info(sd, "Removed IAF resource: %p\n", sd->csr_base);
	sd->csr_base = NULL;
}

static ssize_t iaf_fabric_id_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev;
	struct fdev *fdev;

	pdev = container_of(dev, struct platform_device, dev);
	fdev = platform_get_drvdata(pdev);

	return snprintf(buf, PAGE_SIZE, "0x%x\n", fdev->fabric_id);
}

static DEVICE_ATTR_RO(iaf_fabric_id);

static const struct attribute *iaf_attrs[] = {
	&dev_attr_iaf_fabric_id.attr,
};

static int iaf_remove(struct platform_device *pdev)
{
	struct iaf_pdata *pd = dev_get_platdata(&pdev->dev);
	struct fdev *dev = platform_get_drvdata(pdev);
	int i;

	dev_info(&pdev->dev, "Removing %s\n", dev_name(&pdev->dev));

	debugfs_del_node(&dev->dir_node);

	sysfs_remove_files(&pdev->dev.kobj, iaf_attrs);

	cancel_any_outstanding_fw_initializations(dev);

	mei_iaf_user_unregister(&pdev->dev);

	pd->unregister_dev(pd->parent, dev);

	fdev_wait_on_release(dev);

	for (i = 0; i < pd->sd_cnt; i++)
		remove_subdevice(&dev->sd[i]);

	routing_p2p_clear(dev);

	WARN(kref_read(&dev->refs), "fabric_id 0x%08x has %u references",
	     dev->fabric_id, kref_read(&dev->refs));

	kfree(dev);

	return 0;
}

static u32 dev_fabric_id(struct iaf_pdata *pd)
{
	return pd->product << PRODUCT_SHIFT | pd->index;
}

static int iaf_probe(struct platform_device *pdev)
{
	struct iaf_pdata *pd = dev_get_platdata(&pdev->dev);
	struct fdev *dev;
	int current_sd = -1;
	int err;

	dev_info(&pdev->dev, "Probing %s\n", dev_name(&pdev->dev));
	if (pdev->dev.parent)
		dev_info(&pdev->dev, "connection point for %s\n",
			 dev_name(pdev->dev.parent));

	if (pd->version != IAF_VERSION)
		return -EPERM;

	if (!pd->register_dev || !pd->unregister_dev)
		return -EINVAL;

	if (pd->sd_cnt > IAF_MAX_SUB_DEVS)
		return -EINVAL;

	dev_info(&pdev->dev, "DPA offset: %dGB  size: %dGB\n",
		 pd->dpa.pkg_offset, pd->dpa.pkg_size);

	dev = kzalloc(sizeof(*dev), GFP_ATOMIC);
	if (!dev)
		return -ENOMEM;

	dev->fabric_id = dev_fabric_id(pd);
	dev->pd = pd;
	dev->pdev = pdev;
	mutex_init(&dev->mei_ops_lock);

	dev_info(&pdev->dev, "fabric_id 0x%08x\n", dev->fabric_id);

	platform_set_drvdata(pdev, dev);

	err = fdev_insert(dev);
	if (err) {
		kfree(dev);
		return err;
	}

	err = sysfs_create_files(&pdev->dev.kobj, iaf_attrs);
	if (err) {
		dev_warn(&pdev->dev, "Failed to add sysfs\n");
		goto sysfs_error;
	}

	pd->register_dev(pd->parent, dev, dev->fabric_id, &iaf_ops);

	dev->dir_node =
		debugfs_add_dir_node(dev_name(&dev->pdev->dev),
				     debugfs_get_root_node());
	debugfs_add_x32_node(FABRIC_ID_NODE_NAME, 0400, dev->dir_node,
			     &dev->fabric_id);

	for (current_sd = 0; current_sd < pd->sd_cnt; ++current_sd) {
		err = add_subdevice(&dev->sd[current_sd], dev, current_sd);
		if (err)
			goto add_error;
	}

	err = mei_iaf_user_register(&pdev->dev);
	if (err)
		goto add_error;

	err = load_and_init_fw(dev);
	if (err)
		goto load_error;

	return 0;

load_error:
	cancel_any_outstanding_fw_initializations(dev);
	mei_iaf_user_unregister(&pdev->dev);

add_error:
	sysfs_remove_files(&pdev->dev.kobj, iaf_attrs);
	for (--current_sd; current_sd >= 0 ; --current_sd)
		remove_subdevice(&dev->sd[current_sd]);

	pd->unregister_dev(pd->parent, dev);

sysfs_error:
	debugfs_del_node(&dev->dir_node);
	fdev_wait_on_release(dev);
	kfree(dev);
	return err;
}

static struct platform_driver iaf_driver = {
	.driver = {
		.name           = DRIVER_NAME,
	},
	.probe                  = iaf_probe,
	.remove                 = iaf_remove,
};

static void __exit iaf_unload_module(void)
{
	pr_notice("Unloading %s\n", MODULEDETAILS);

	debugfs_term();

	mbox_term();
	nl_term();

	flush_workqueue(system_unbound_wq);

	platform_driver_unregister(&iaf_driver);

	rem_destroy();

	routing_destroy();

	pr_notice("%s Unloaded\n", MODULEDETAILS);
}
module_exit(iaf_unload_module);

/*
 * \brief Loads the module into kernel space and does global initializations.
 * Called by insmod or modprobe.
 */
static int __init iaf_load_module(void)
{
	pr_notice("Initializing %s\n", MODULEDETAILS);
	pr_notice("Built for Linux Kernel %s\n", UTS_RELEASE);

	debugfs_init();

#ifdef SELFTESTS
	if (selftests_run()) {
		debugfs_term();
		return -ENODEV;
	}
#endif

	routing_init();

	rem_init();

	mbox_init();
	nl_init();

	platform_driver_register(&iaf_driver);

	return 0;
}
module_init(iaf_load_module);

MODULE_DESCRIPTION("Intel fabric");
MODULE_LICENSE("GPL and additional rights");
MODULE_ALIAS("platform:iaf");
