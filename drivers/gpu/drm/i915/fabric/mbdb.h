/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#ifndef MBDB_H_INCLUDED
#define MBDB_H_INCLUDED

#include <linux/types.h>

#include "iaf_drv.h"
#include "ops.h"

#define MBOX_CW_OPCODE_MASK 0x00000000000000FFULL
#define MBOX_CW_OPCODE_SHIFT 0
#define MBOX_CW_IS_REQ_MASK 0x0000000000000001ULL
#define MBOX_CW_IS_REQ_SHIFT 8
#define MBOX_CW_POSTED_MASK 0x0000000000000001ULL
#define MBOX_CW_POSTED_SHIFT 9
#define MBOX_CW_SEQ_NO_MASK 0x000000000000003FULL
#define MBOX_CW_SEQ_NO_SHIFT 10
#define MBOX_CW_PARAMS_LEN_MASK 0x0000000000000FFFULL
#define MBOX_CW_PARAMS_LEN_SHIFT 16
#define MBOX_CW_RSP_STATUS_MASK 0x000000000000000FULL
#define MBOX_CW_RSP_STATUS_SHIFT 28
#define MBOX_CW_TID_MASK 0x00000000FFFFFFFFULL
#define MBOX_CW_TID_SHIFT 32

#define CP_ADDR_MBDB_BASE 0x6000

/* Outbox related */
u8 mbdb_outbox_seqno(struct fsubdev *sd);
struct mbox_msg __iomem *mbdb_outbox_acquire(struct fsubdev *sd, u64 *cw);
void mbdb_outbox_release(struct fsubdev *sd);

/* ibox related (i.e. virtual inbox) */
struct mbdb_ibox *mbdb_ibox_acquire(struct fsubdev *sd, u8 op_code,
				    void *response, u32 rsp_len, mbox_cb cb,
				       void *context, bool posted);

void mbdb_ibox_release(struct mbdb_ibox *ibox);
int mbdb_ibox_wait(struct mbdb_ibox *ibox);

void destroy_mbdb(struct fsubdev *sd);
int create_mbdb(struct fsubdev *sd, struct dentry *sd_dir_node);

irqreturn_t mbdb_handle_irq(struct fsubdev *sd);

static inline u64 build_cw(u8 op_code, enum mbdb_msg_type req_rsp,
			   enum posted is_posted, u16 seq_no, u16 length,
			   u32 tid)
{
	return (u64)
		(op_code & MBOX_CW_OPCODE_MASK) << MBOX_CW_OPCODE_SHIFT |
		(req_rsp & MBOX_CW_IS_REQ_MASK) << MBOX_CW_IS_REQ_SHIFT |
		(is_posted & MBOX_CW_POSTED_MASK) << MBOX_CW_POSTED_SHIFT |
		(seq_no & MBOX_CW_SEQ_NO_MASK) << MBOX_CW_SEQ_NO_SHIFT |
		(length & MBOX_CW_PARAMS_LEN_MASK) << MBOX_CW_PARAMS_LEN_SHIFT |
		(tid & MBOX_CW_TID_MASK) << MBOX_CW_TID_SHIFT;
}

static inline u8 mbdb_mbox_op_code(u64 cw)
{
	return (u8)(cw >> MBOX_CW_OPCODE_SHIFT) & MBOX_CW_OPCODE_MASK;
}

static inline enum mbdb_msg_type mbdb_mbox_msg_type(u64 cw)
{
	return (cw >> MBOX_CW_IS_REQ_SHIFT) & MBOX_CW_IS_REQ_MASK;
}

static inline enum posted mbdb_mbox_is_posted(u64 cw)
{
	return (cw >> MBOX_CW_POSTED_SHIFT) & MBOX_CW_POSTED_MASK;
}

static inline u8 mbdb_mbox_seq_no(u64 cw)
{
	return (u8)(cw >> MBOX_CW_SEQ_NO_SHIFT) & MBOX_CW_SEQ_NO_MASK;
}

static inline u8 mbdb_mbox_seqno_next(u8 seqno)
{
	return (seqno + 1) & MBOX_CW_SEQ_NO_MASK;
}

static inline bool mbdb_mbox_seqno_error(u8 seqno, u8 expected_seqno)
{
	return seqno != expected_seqno;
}

static inline u16 mbdb_mbox_params_len(u64 cw)
{
	return (u16)(cw >> MBOX_CW_PARAMS_LEN_SHIFT) & MBOX_CW_PARAMS_LEN_MASK;
}

static inline u32 mbdb_mbox_tid(u64 cw)
{
	return (u32)(cw >> MBOX_CW_TID_SHIFT) & MBOX_CW_TID_MASK;
}

static inline u32 mbdb_ibox_tid(struct mbdb_ibox *ibox)
{
	return ibox ? ibox->tid : 0;
}

static inline u8 mbdb_mbox_rsp_status(u64 cw)
{
	return (u8)(cw >> MBOX_CW_RSP_STATUS_SHIFT) & MBOX_CW_RSP_STATUS_MASK;
}

#endif
