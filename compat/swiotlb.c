// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dynamic DMA mapping support.
 *
 * This implementation is a fallback for platforms that do not support
 * I/O TLBs (aka DMA address translation hardware).
 * Copyright (C) 2000 Asit Mallick <Asit.K.Mallick@intel.com>
 * Copyright (C) 2000 Goutham Rao <goutham.rao@intel.com>
 * Copyright (C) 2000, 2003 Hewlett-Packard Co
 *      David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 03/05/07 davidm      Switch from PCI-DMA to generic device DMA API.
 * 00/12/13 davidm      Rename to swiotlb.c and add mark_clean() to avoid
 *                      unnecessary i-cache flushing.
 * 04/07/.. ak          Better overflow handling. Assorted fixes.
 * 05/09/10 linville    Add support for syncing ranges, support syncing for
 *                      DMA_BIDIRECTIONAL mappings, miscellaneous cleanup.
 * 08/12/11 beckyb      Add highmem support
 */

#include <linux/cache.h>
#include <linux/cc_platform.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/iommu-helper.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/pfn.h>
#include <linux/scatterlist.h>
#include <linux/set_memory.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/swiotlb.h>
#include <linux/types.h>

#undef is_swiotlb_active
bool backport_is_swiotlb_active(struct device *dev)
{
    struct io_tlb_mem *mem = dev->dma_io_tlb_mem;
    return mem && mem->nslabs;
}
#define is_swiotlb_active backport_is_swiotlb_active
EXPORT_SYMBOL_GPL(is_swiotlb_active);
#if 0
extern struct io_tlb_mem io_tlb_default_mem;
unsigned int swiotlb_max_segment(void)
{
        if (!io_tlb_default_mem.nslabs)
                return 0;
        return rounddown(io_tlb_default_mem.nslabs << IO_TLB_SHIFT, PAGE_SIZE);
}
EXPORT_SYMBOL_GPL(swiotlb_max_segment);
#endif
