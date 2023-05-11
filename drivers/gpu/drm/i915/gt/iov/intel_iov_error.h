/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_ERROR_H__
#define __INTEL_IOV_ERROR_H__

#include <linux/types.h>

struct intel_iov;

int intel_iov_error_process_msg(struct intel_iov *iov,
				const u32 *data, u32 len);

#endif /* __INTEL_IOV_ERROR_H__ */
