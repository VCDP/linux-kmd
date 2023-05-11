// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include <drm/intel_iaf_platform.h>

#include "iaf_drv.h"

DEFINE_XARRAY_ALLOC(intel_fdevs);

/*
 * Used for creating unique xarray hash.  Products need to be defined
 * in the intel_iaf_platform.h file.
 */
#define PRODUCT_SHIFT 16

static irqreturn_t handle_iaf_irq(int irq, void *arg)
{
	return IRQ_HANDLED;
}

static struct query_info *handle_query(void *handle, u32 fabric_id)
{
	return ERR_PTR(-ENOTSUPP);
}

static int handle_iaf_event(void *handle, enum iaf_parent_event event)
{
	return 0;
}

static const struct iaf_ops iaf_ops = {
	.connectivity_query = handle_query,
	.parent_event = handle_iaf_event,
};

static int add_subdevice(struct fsubdev *sd, struct fdev *dev, int index)
{
	struct resource *res;
	int err;

	sd->fdev = dev;

	res = platform_get_resource(dev->pdev, IORESOURCE_MEM, index);
	sd->csr_base = ioremap_nocache(res->start, resource_size(res));
	if (!sd->csr_base) {
		dev_err(sd_dev(sd), "Unable to map resource %pR\n", res);
		return -ENOMEM;
	}

	dev_info(sd_dev(sd), "mapped resource %pR to mb_base 0x%llx\n",
		 res, (u64)sd->csr_base);

	res = platform_get_resource(dev->pdev, IORESOURCE_IRQ, index);
	dev_info(sd_dev(sd), "IRQ resource %pR\n", res);

	sd->irq = res->start;
	err = request_irq(sd->irq, handle_iaf_irq, 0, "intel_sd_irq", sd);
	if (err) {
		dev_err(sd_dev(sd), "failed to request_irq %d  err: %d\n",
			sd->irq, err);
		iounmap(sd->csr_base);
		return err;
	}

	dev_info(sd_dev(sd), "Added IAF subdevice: %d\n", index);

	return 0;
}

static void remove_subdevice(struct fsubdev *sd)
{
	dev_info(sd_dev(sd), "Unmapping IAF resource: 0x%llx\n",
		 (u64)sd->csr_base);

	free_irq(sd->irq, sd);
	iounmap(sd->csr_base);
	sd->csr_base = NULL;
}

static int iaf_remove(struct platform_device *pdev)
{
	struct iaf_pdata *pd = dev_get_platdata(&pdev->dev);
	struct fdev *dev = platform_get_drvdata(pdev);
	int i;

	dev_info(&pdev->dev, "Removing %s\n", dev_name(&pdev->dev));

	pd->unregister_dev(pd->parent, dev);
	xa_erase(&intel_fdevs, dev->fabric_id);

	for (i = 0; i < pd->sd_cnt; i++)
		remove_subdevice(&dev->sd[i]);

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
	int err;
	int i;

	dev_info(&pdev->dev, "Probing\n");

	if (pd->version != IAF_VERSION) {
		dev_err(&pdev->dev, "Invalid version: %d\n", pd->version);
		return -EPERM;
	}

	if (!pd->register_dev | !pd->unregister_dev) {
		dev_err(&pdev->dev, "API missing\n");
		return -EINVAL;
	}

	if (pd->sd_cnt > MAX_SUBDEVS) {
		dev_err(&pdev->dev, "Too many subdevices %d\n", pd->sd_cnt);
		return -EINVAL;
	}

	dev_info(&pdev->dev, "DPA offset: %dGB  size: %dGB\n",
		 pd->dpa.pkg_offset, pd->dpa.pkg_size);

	dev = kzalloc(sizeof(*dev), GFP_ATOMIC);
	if (!dev)
		return -ENOMEM;

	dev->fabric_id = dev_fabric_id(pd);
	dev->pd = pd;
	dev->pdev = pdev;

	platform_set_drvdata(pdev, dev);

	err = xa_insert(&intel_fdevs, dev->fabric_id, dev, GFP_KERNEL);
	if (err) {
		dev_err(&pdev->dev, "Failed to add device: %d  error: %d\n",
			dev->fabric_id, err);
		kfree(dev);
		return err;
	}

	pd->register_dev(pd->parent, dev, dev->fabric_id, &iaf_ops);

	for (i = 0; i < pd->sd_cnt; i++) {
		err = add_subdevice(&dev->sd[i], dev, i);
		if (err) {
			pd->unregister_dev(pd->parent, dev);
			xa_erase(&intel_fdevs, dev->fabric_id);
			kfree(dev);
			return err;
		}
	}

	return 0;
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
	platform_driver_unregister(&iaf_driver);
}
module_exit(iaf_unload_module);

static int __init iaf_load_module(void)
{
	return platform_driver_register(&iaf_driver);
}
module_init(iaf_load_module);

MODULE_AUTHOR("Intel Corporation");

MODULE_DESCRIPTION("Intel fabric");
MODULE_LICENSE("GPL and additional rights");
MODULE_ALIAS("platform:iaf");
