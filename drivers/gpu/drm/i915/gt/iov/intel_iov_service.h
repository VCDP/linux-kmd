/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_SERVICE_H__
#define __INTEL_IOV_SERVICE_H__

#include <linux/types.h>

struct intel_iov;

void intel_iov_service_init_early(struct intel_iov *iov);
void intel_iov_service_update(struct intel_iov *iov);
void intel_iov_service_reset(struct intel_iov *iov);
void intel_iov_service_release(struct intel_iov *iov);

int intel_iov_service_request_handler(struct intel_iov *iov, u32 origin,
				      u32 fence, u32 action,
				      const u32 *payload, u32 len);

#endif /* __INTEL_IOV_SERVICE_H__ */
