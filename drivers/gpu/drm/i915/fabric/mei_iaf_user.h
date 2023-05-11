/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#ifndef _MEI_IAF_USER_H_
#define _MEI_IAF_USER_H_

#include <linux/device.h>

/* register/unregister for MEI access using component system */
int mei_iaf_user_register(struct device *dev);
void mei_iaf_user_unregister(struct device *dev);

#endif
