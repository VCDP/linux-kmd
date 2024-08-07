#ifndef _BACKPORT_LINUX_SWIOTLB_H
#define _BACKPORT_LINUX_SWIOTLB_H
#include_next <linux/swiotlb.h>

bool backport_is_swiotlb_active(struct device *dev);
#define is_swiotlb_active backport_is_swiotlb_active

#endif
