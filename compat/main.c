/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include "backports.h"

#define DRIVER_AUTHOR           "Tungsten Graphics, Inc."
#define DRIVER_DESC             "Intel Graphics"
#define BUILD_DATE "20160105"

extern int sync_debugfs_init(void);

static int __init backport_init(void)
{
//        intel_stolen_init();
	init_xa();
	dma_buf_init();
	/*sync_debugfs_init();*/
	/* idr_init_cache(); */
	printk("Initialized drm/i915 compat module %s\n", BUILD_DATE);
	return 0;
}

static void __exit backport_exit(void)
{
	uninit_xa();
	dma_buf_deinit();
}

module_init(backport_init);
module_exit(backport_exit);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
