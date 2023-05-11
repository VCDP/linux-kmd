/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#ifndef SELFTESTS_ROUTING_MOCK_H_INCLUDED
#define SELFTESTS_ROUTING_MOCK_H_INCLUDED

#ifdef SELFTESTS
#include "routing_topology.h"

int routing_mock_create_topology(struct routing_topology *topo);
void routing_mock_destroy(struct routing_topology *topo);
#endif

#endif /* SELFTESTS_ROUTING_MOCK_H_INCLUDED */
