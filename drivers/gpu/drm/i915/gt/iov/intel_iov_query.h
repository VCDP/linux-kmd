/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_QUERY_H__
#define __INTEL_IOV_QUERY_H__

#include <linux/types.h>

struct intel_iov;

int intel_iov_query_bootstrap(struct intel_iov *iov);
int intel_iov_query_runtime(struct intel_iov *iov, bool early);
void intel_iov_query_fini(struct intel_iov *iov);

#endif /* __INTEL_IOV_QUERY_H__ */
