/*
 * Fence mechanism for dma-buf and to allow for asynchronous dma access
 *
 * Copyright (C) 2012 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/atomic.h>
#include <linux/dma-fence.h>

static DEFINE_SPINLOCK(dma_fence_stub_lock);
static struct dma_fence dma_fence_stub;


/**
 * DOC: DMA fences overview
 *
 * DMA fences, represented by &struct dma_fence, are the kernel internal
 * synchronization primitive for DMA operations like GPU rendering, video
 * encoding/decoding, or displaying buffers on a screen.
 *
 * A fence is initialized using dma_fence_init() and completed using
 * dma_fence_signal(). Fences are associated with a context, allocated through
 * dma_fence_context_alloc(), and all fences on the same context are
 * fully ordered.
 *
 * Since the purposes of fences is to facilitate cross-device and
 * cross-application synchronization, there's multiple ways to use one:
 *
 * - Individual fences can be exposed as a &sync_file, accessed as a file
 *   descriptor from userspace, created by calling sync_file_create(). This is
 *   called explicit fencing, since userspace passes around explicit
 *   synchronization points.
 *
 * - Some subsystems also have their own explicit fencing primitives, like
 *   &drm_syncobj. Compared to &sync_file, a &drm_syncobj allows the underlying
 *   fence to be updated.
 *
 * - Then there's also implicit fencing, where the synchronization points are
 *   implicitly passed around as part of shared &dma_buf instances. Such
 *   implicit fences are stored in &struct reservation_object through the
 *   &dma_buf.resv pointer.
 */

static const char *dma_fence_stub_get_name(struct dma_fence *fence)
{
        return "stub";
}


static bool dma_fence_default_enable_signaling(struct dma_fence *fence)
{
        return true;
}

static const struct dma_fence_ops dma_fence_stub_ops = {
        .get_driver_name = dma_fence_stub_get_name,
        .get_timeline_name = dma_fence_stub_get_name,
        .enable_signaling = dma_fence_default_enable_signaling,
        .wait = dma_fence_default_wait,
};

/**
 * dma_fence_get_stub - return a signaled fence
 *
 * Return a stub fence which is already signaled.
 */
struct dma_fence *dma_fence_get_stub(void)
{
        spin_lock(&dma_fence_stub_lock);
        if (!dma_fence_stub.ops) {
                dma_fence_init(&dma_fence_stub,
                               &dma_fence_stub_ops,
                               &dma_fence_stub_lock,
                               0, 0);
                dma_fence_signal_locked(&dma_fence_stub);
        }
        spin_unlock(&dma_fence_stub_lock);

        return dma_fence_get(&dma_fence_stub);
}
EXPORT_SYMBOL(dma_fence_get_stub);

