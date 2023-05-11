// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 */

#include <linux/device.h>
#include <linux/component.h>
#include <linux/module.h>

#include "iaf_drv.h"
#include "mei_iaf_user.h"

static int bind_to_mei(struct device *dev, struct device *mei_dev, void *data)
{
	struct fdev *fdev = dev_get_drvdata(dev);
	struct mei_iaf_ops *ops = data;

	dev_dbg(dev, "binding to MEI device %s\n", dev_name(mei_dev));

	mutex_lock(&fdev->mei_ops_lock);

	fdev->mei_dev = mei_dev;
	fdev->mei_ops = ops;

	if (fdev->mei_bind_continuation)
		fdev->mei_bind_continuation(fdev);

	fdev->mei_bind_continuation = NULL;

	mutex_unlock(&fdev->mei_ops_lock);

	return 0;
}

static void unbind_from_mei(struct device *dev, struct device *mei_dev,
			    void *data)
{
	struct fdev *fdev = dev_get_drvdata(dev);
	struct mei_iaf_ops *ops = data;

	dev_dbg(dev, "unbinding from MEI device %s\n", dev_name(mei_dev));

	mutex_lock(&fdev->mei_ops_lock);

	if (fdev->mei_bind_continuation)
		dev_err(dev, "MEI unbound while bind continuation pending\n");

	if (fdev->mei_ops != ops)
		dev_warn(dev, "MEI unbound OPs differ from bound OPs\n");

	fdev->mei_bind_continuation = NULL;
	fdev->mei_dev = NULL;

	fdev->mei_ops = NULL;

	mutex_unlock(&fdev->mei_ops_lock);
}

static const struct component_ops bind_ops = {
	.bind = bind_to_mei,
	.unbind = unbind_from_mei,
};

int mei_iaf_user_register(struct device *dev)
{
	dev_dbg(dev, "Registering as MEI_IAF user\n");
	return component_add(dev, &bind_ops);
}

void mei_iaf_user_unregister(struct device *dev)
{
	dev_dbg(dev, "Unregistering as MEI_IAF user\n");
	component_del(dev, &bind_ops);
}
