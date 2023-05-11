/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#ifndef IO_H_INCLUDED
#define IO_H_INCLUDED

#include <linux/types.h>
#include <linux/stddef.h>

#include "iaf_drv.h"

#define LEN_BYTES_SHIFT 3
#define LEN_BYTES_MASK 0x0000000000000007ULL

static inline
void io_write(u64 __iomem *addr, const u8 *data, size_t len)
{
	size_t qwords = len >> LEN_BYTES_SHIFT;
	u64 *qw_data = (u64 *)data;

	for ( ; qwords; qwords--, addr++, qw_data++)
		writeq(*qw_data, addr);

	if (len & LEN_BYTES_MASK) {
		u64 remaining_data = 0;

		memcpy(&remaining_data, qw_data, len & LEN_BYTES_MASK);
		writeq(remaining_data, addr);
	}
}

static inline
void io_read(u64 __iomem *addr, u8 *data, size_t len)
{
	size_t qwords = len >> LEN_BYTES_SHIFT;
	u64 *qw_data = (u64 *)data;

	for ( ; qwords; qwords--, addr++, qw_data++)
		*qw_data = readq(addr);

	if (len & LEN_BYTES_MASK) {
		u64 remaining_data = readq(addr);

		memcpy(qw_data, &remaining_data, len & LEN_BYTES_MASK);
	}
}

#endif
