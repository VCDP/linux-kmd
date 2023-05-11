#ifndef _BACKPORT_LINUX_BITMAP_H
#define _BACKPORT_LINUX_BITMAP_H
#include_next <linux/bitmap.h>

extern unsigned long *bitmap_zalloc(unsigned int nbits, gfp_t flags);


extern void bitmap_free(const unsigned long *bitmap);

#endif
