/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_RELAY_H__
#define __INTEL_RELAY_H__

#include "intel_iov_types.h"

static inline void intel_iov_relay_init_early(struct intel_iov_relay *relay)
{
	spin_lock_init(&relay->lock);
	INIT_LIST_HEAD(&relay->pending_relays);
}

int intel_iov_relay_send_request(struct intel_iov_relay *relay, u32 target,
				 u32 code, const u32 *data, u32 data_len,
				 u32 *buf, u32 buf_size);
int intel_iov_relay_send_response(struct intel_iov_relay *relay, u32 target,
				  u32 fence, const u32 *data, u32 data_len);
int intel_iov_relay_send_status(struct intel_iov_relay *relay, u32 target,
				u32 fence, u32 code);
int intel_iov_relay_process_msg(struct intel_iov_relay *relay, const u32 *msg,
				u32 len);

#endif /* __INTEL_RELAY_H__ */
