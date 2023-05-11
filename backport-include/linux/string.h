#ifndef _BACKPORT_LINUX_STRING_H
#define _BACKPORT_LINUX_STRING_H
#include <linux/version.h>
#include_next <linux/string.h>


static __always_inline size_t str_has_prefix(const char *str, const char *prefix)
{
        size_t len = strlen(prefix);
        return strncmp(str, prefix, len) == 0 ? len : 0;
}

#endif
