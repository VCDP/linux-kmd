#ifndef _BACKPORT_LINUX_FB_H
#define _BACKPORT_LINUX_FB_H
#include <linux/version.h>
#include_next <linux/fb.h>
#define FBINFO_HIDE_SMEM_START  0x200000
#endif
