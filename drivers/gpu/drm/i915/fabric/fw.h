/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

#ifndef _FW_H_
#define _FW_H_

#include "iaf_drv.h"

#define FW_VERSION_INIT_BIT	BIT(1)
#define FW_VERSION_ENV_BIT	BIT(0)

int load_and_init_fw(struct fdev *dev);
void cancel_any_outstanding_fw_initializations(struct fdev *dev);

#endif
