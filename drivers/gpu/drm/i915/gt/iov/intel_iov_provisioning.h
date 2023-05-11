/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_PROVISIONING_H__
#define __INTEL_IOV_PROVISIONING_H__

struct intel_iov;
struct intel_iov_config;

void intel_iov_provisioning_init_early(struct intel_iov *iov);
void intel_iov_provisioning_release(struct intel_iov *iov);
int intel_iov_provisioning_init_ggtt(struct intel_iov *iov);
void intel_iov_provisioning_init(struct intel_iov *iov);

const struct intel_iov_config *
intel_iov_provisioning_get_config(struct intel_iov *iov, unsigned int id);

#endif /* __INTEL_IOV_PROVISIONING_H__ */
