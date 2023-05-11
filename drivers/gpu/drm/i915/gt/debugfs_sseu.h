/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef DEBUGFS_SSEU_H
#define DEBUGFS_SSEU_H

struct intel_gt;
struct dentry;
struct sseu_dev_info;

void debugfs_gt_register_sseu(struct intel_gt *gt, struct dentry *root);

#endif /* DEBUGFS_SSEU_H */
