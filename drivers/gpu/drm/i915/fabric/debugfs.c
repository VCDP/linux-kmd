// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

#include <linux/debugfs.h>
#include <linux/dcache.h>
#include <linux/xarray.h>

#include "iaf_drv.h"
#include "debugfs.h"

#define ROOT_NODE DRIVER_NAME

static struct dentry *root_node;

struct dentry *debugfs_get_root_node(void)
{
	return root_node;
}

void debugfs_del_node(struct dentry **node)
{
	if (root_node) {
		struct dentry *local_node = *node;

		*node = NULL;
		debugfs_remove_recursive(local_node);
	}
}

struct dentry *debugfs_add_file_node(const char *name, umode_t mode,
				     struct dentry *parent, void *data,
				     const struct file_operations *fops)
{
	if (IS_ERR_OR_NULL(parent))
		return ERR_PTR(-ENODEV);

	return debugfs_create_file(name, mode, parent, data, fops);
}

struct dentry *debugfs_add_dir_node(const char *name,  struct dentry *parent)
{
	if (IS_ERR_OR_NULL(parent))
		return ERR_PTR(-ENODEV);

	return debugfs_create_dir(name, parent);
}

void debugfs_add_x32_node(const char *name, umode_t mode, struct dentry *parent,
			  void *data)
{
	if (IS_ERR_OR_NULL(parent))
		return;

	debugfs_create_x32(name, mode, parent, data);
}

void debugfs_add_u32_node(const char *name, umode_t mode, struct dentry *parent,
			  void *data)
{
	if (IS_ERR_OR_NULL(parent))
		return;

	debugfs_create_u32(name, mode, parent, data);
}

void debugfs_add_u64_node(const char *name, umode_t mode, struct dentry *parent,
			  void *data)
{
	if (IS_ERR_OR_NULL(parent))
		return;

	debugfs_create_u64(name, mode, parent, data);
}

void debugfs_add_ulong_node(const char *name, umode_t mode,
			    struct dentry *parent, void *data)
{
	if (IS_ERR_OR_NULL(parent))
		return;

	debugfs_create_ulong(name, mode, parent, data);
}

void debugfs_term(void)
{
	debugfs_del_node(&root_node);
}

void debugfs_init(void)
{
	root_node = debugfs_create_dir(ROOT_NODE, NULL);
	if (IS_ERR(root_node))
		root_node = NULL;
}
