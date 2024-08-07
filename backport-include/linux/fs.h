#ifndef _BACKPORT_LINUX_FS_H
#define _BACKPORT_LINUX_FS_H
#include_next <linux/fs.h>

struct page;
/*
 * pagecache_write_begin/pagecache_write_end must be used by general code
 * to write into the pagecache.
 */
int pagecache_write_begin(struct file *, struct address_space *mapping,
                                loff_t pos, unsigned len, unsigned flags,
                                struct page **pagep, void **fsdata);

int pagecache_write_end(struct file *, struct address_space *mapping,
                                loff_t pos, unsigned len, unsigned copied,
                                struct page *page, void *fsdata);
#endif
