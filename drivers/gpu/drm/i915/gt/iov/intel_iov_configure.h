/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_CONFIGURE_H__
#define __INTEL_IOV_CONFIGURE_H__

struct intel_iov;

int intel_iov_enable_vfs(struct intel_iov *iov, unsigned int num_vfs);
int intel_iov_disable_vfs(struct intel_iov *iov);

#endif /* __INTEL_IOV_CONFIGURE_H__ */
