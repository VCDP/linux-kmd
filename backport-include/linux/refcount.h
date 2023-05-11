#ifndef _BACKPORT_LINUX_REFCOUNT_H
#define _BACKPORT_LINUX_REFCOUNT_H
#include <linux/version.h>
#include_next <linux/refcount.h>
bool refcount_dec_and_lock_irqsave(refcount_t *r, spinlock_t *lock,
                                   unsigned long *flags);

#endif
