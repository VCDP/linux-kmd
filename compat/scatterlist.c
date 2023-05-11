/*
 * Copyright (C) 2012 Avionic Design GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <linux/kmemleak.h>

static int sg_dma_page_count(struct scatterlist *sg)
{
        return PAGE_ALIGN(sg->offset + sg_dma_len(sg)) >> PAGE_SHIFT;
}
#if 0
void __sg_page_iter_start(struct sg_page_iter *piter,
                          struct scatterlist *sglist, unsigned int nents,
                          unsigned long pgoffset)
{
        piter->__pg_advance = 0;
        piter->__nents = nents;

        piter->sg = sglist;
        piter->sg_pgoffset = pgoffset;
}
EXPORT_SYMBOL(__sg_page_iter_start);
#endif
bool __sg_page_iter_dma_next(struct sg_dma_page_iter *dma_iter)
{
	if (!dma_iter)
		return false;

        struct sg_page_iter *piter = &dma_iter->base;

        if (!piter->__nents || !piter->sg)
                return false;

        piter->sg_pgoffset += piter->__pg_advance;
        piter->__pg_advance = 1;

        while (piter->sg_pgoffset >= sg_dma_page_count(piter->sg)) {
                piter->sg_pgoffset -= sg_dma_page_count(piter->sg);
                piter->sg = sg_next(piter->sg);
                if (!--piter->__nents || !piter->sg)
                        return false;
        }

        return true;
}
EXPORT_SYMBOL(__sg_page_iter_dma_next);

