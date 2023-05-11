#ifndef _BACKPORT_LINUX_SCATEERLIST_H
#define _BACKPORT_LINUX_SCATEERLIST_H
#include <linux/version.h>
#include_next <linux/scatterlist.h>

#define SG_CHAIN    0x01UL
#define SCATTERLIST_MAX_SEGMENT (UINT_MAX & PAGE_MASK)

struct sg_dma_page_iter {
        struct sg_page_iter base;
};

extern bool __sg_page_iter_dma_next(struct sg_dma_page_iter *dma_iter);
#ifndef for_each_sg_page
#define for_each_sg_page(sglist, piter, nents, pgoffset)                   \
        for (__sg_page_iter_start((piter), (sglist), (nents), (pgoffset)); \
             __sg_page_iter_next(piter);)
#endif
#ifndef for_each_sg_dma_page
#define for_each_sg_dma_page(sglist, dma_iter, dma_nents, pgoffset)            \
        for (__sg_page_iter_start(&(dma_iter)->base, sglist, dma_nents,        \
                                  pgoffset);                                   \
             __sg_page_iter_dma_next(dma_iter);)
#endif
#endif
