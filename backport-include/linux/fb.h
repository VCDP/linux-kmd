#ifndef _BACKPORT_LINUX_FB_H
#define _BACKPORT_LINUX_FB_H
#include <linux/version.h>
#include_next <linux/fb.h>
#define FBINFO_HIDE_SMEM_START  0x200000
#define FBINFO_MISC_USEREVENT   0x10000

extern int remove_conflicting_framebuffers(struct apertures_struct *a,
                                           const char *name, bool primary);
extern void fb_cleanup_device(struct fb_info *head);
#endif
