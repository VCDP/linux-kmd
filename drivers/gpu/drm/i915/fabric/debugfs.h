/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

#ifndef DEBUGFS_H_INCLUDED
#define DEBUGFS_H_INCLUDED

#include <linux/types.h>
#include <linux/fs.h>

struct dentry *debugfs_get_root_node(void);

void debugfs_del_node(struct dentry **node);

struct dentry *debugfs_add_file_node(const char *name, umode_t mode,
				     struct dentry *parent, void *data,
				     const struct file_operations *fops);
struct dentry *debugfs_add_dir_node(const char *name,  struct dentry *parent);
void debugfs_add_x32_node(const char *name, umode_t mode, struct dentry *parent,
			  void *data);
void debugfs_add_u32_node(const char *name, umode_t mode, struct dentry *parent,
			  void *data);
void debugfs_add_u64_node(const char *name, umode_t mode, struct dentry *parent,
			  void *data);
void debugfs_add_ulong_node(const char *name, umode_t mode,
			    struct dentry *parent, void *data);

void debugfs_term(void);
void debugfs_init(void);

#endif
