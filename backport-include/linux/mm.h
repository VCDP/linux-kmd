#ifndef _BACKPORT_LINUX_MM_H
#define _BACKPORT_LINUX_MM_H
#include_next <linux/mm.h>

int backport_register_shrinker(struct shrinker *shrinker);
#define register_shrinker backport_register_shrinker

#endif
