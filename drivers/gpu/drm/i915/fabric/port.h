/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 */
#ifndef PORT_H_
#define PORT_H_

#include "iaf_drv.h"

void initialize_fports(struct fsubdev *sd);
void destroy_fports(struct fsubdev *sd);

int enable_fports(struct fsubdev *sd, unsigned long lpnmask);
int disable_fports(struct fsubdev *sd, unsigned long lpnmask);
int enable_usage_fports(struct fsubdev *sd, unsigned long lpnmask);
int disable_usage_fports(struct fsubdev *sd, unsigned long lpnmask);
int get_fport_status(struct fsubdev *sd, u8 lpn, struct fport_status *status);
void port_state_change_trap_handler(struct fsubdev *sd);
void port_link_width_degrade_trap_handler(struct fsubdev *sd);
void port_link_quality_indicator_trap_handler(struct fsubdev *sd);
void port_qsfp_presence_trap_handler(struct fsubdev *sd);
void port_qsfp_fault_trap_handler(struct fsubdev *sd);

#endif /* end of header file */
