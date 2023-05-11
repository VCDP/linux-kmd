/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

#ifndef MBOX_H_INCLUDED
#define MBOX_H_INCLUDED

#ifdef CPTCFG_MBOX_ACCESS
void mbox_term(void);
int mbox_init(void);
#else
static inline void mbox_term(void) {}
static inline int mbox_init(void) { return 0; }
#endif

#endif
