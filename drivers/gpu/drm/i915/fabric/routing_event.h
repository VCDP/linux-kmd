/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#ifndef ROUTING_EVENT_H_INCLUDED
#define ROUTING_EVENT_H_INCLUDED

void rem_init(void);
void rem_destroy(void);
void rem_request(void);
int rem_route_start(void);
void rem_route_finish(void);

#endif /* ROUTING_EVENT_H_INCLUDED */
