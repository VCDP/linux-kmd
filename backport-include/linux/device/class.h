#ifndef _BACKPORT_LINUX_CLASS_H
#define _BACKPORT_LINUX_CLASS_H
#include_next <linux/device/class.h>

#define class_create(owner, name)               \
({                                              \
        class_create(name);                     \
})

#endif
