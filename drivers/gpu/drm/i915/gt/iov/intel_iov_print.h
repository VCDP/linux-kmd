/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_PRINT_H__
#define __INTEL_IOV_PRINT_H__

struct drm_printer;
struct intel_iov;

void intel_iov_dump(const struct intel_iov *iov, struct drm_printer *p);
void intel_iov_print_all(struct intel_iov *iov, struct drm_printer *p);

#endif /* __INTEL_IOV_PRINT_H__ */
