// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#include "csr.h"
#include "iaf_drv.h"
#include "io.h"
#include "ops.h"
#include "mbdb.h"
#include "trace.h"

struct mbdb_ibox *
mbdb_op_build_cw_and_acquire_ibox(struct fsubdev *sd, u8 op_code,
				  size_t parm_len, void *response, u32 rsp_len,
				  u64 *cw, mbox_cb cb, void *context,
				  bool posted)
{
	struct mbdb_ibox *ibox;

	ibox = mbdb_ibox_acquire(sd, op_code, response, rsp_len, cb, context,
				 posted);
	if (IS_ERR(ibox)) {
		sd_err(sd, "%s: Unable to acquire ibox.\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	*cw = build_cw(op_code, MBOX_REQUEST,
		       posted ? MBOX_NO_RESPONSE_REQUESTED
			      : MBOX_RESPONSE_REQUESTED,
		       0, parm_len, mbdb_ibox_tid(ibox));

	return ibox;
}

int ops_linkmgr_port_lqi_trap_ack(struct fsubdev *sd, mbox_cb cb, void *context,
				  bool posted)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ACKNOWLEDGE,
		 0, NULL, 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_linkmgr_port_lqi_trap_ena_set(struct fsubdev *sd, u32 enabled,
				      mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ENABLE_SET,
		 sizeof(enabled), NULL, 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(enabled);
	op_param.data = &enabled;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_port_lqi_trap_ena_get(struct fsubdev *sd, u32 *result,
				      mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ENABLE_GET, 0, result,
		 sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_linkmgr_port_lwd_trap_ack(struct fsubdev *sd, mbox_cb cb, void *context,
				  bool posted)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ACKNOWLEDGE,
		 0, NULL, 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_linkmgr_port_lwd_trap_ena_set(struct fsubdev *sd, u32 enabled,
				      mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ENABLE_SET,
		 sizeof(enabled), NULL, 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(enabled);
	op_param.data = &enabled;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_port_lwd_trap_ena_get(struct fsubdev *sd, u32 *result,
				      mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ENABLE_GET, 0, result,
		 sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_clear_port_errorinfo(struct fsubdev *sd,
			     struct mbdb_op_clear_port_errorinfo *req,
			     struct mbdb_op_clear_port_errorinfo *rsp,
			     mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;
	size_t req_len;
	size_t rsp_len;

	if (!req || !rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	req_len = sizeof(*req);
	rsp_len = sizeof(*rsp);

	ibox = mbdb_op_build_cw_and_acquire_ibox
			(sd, MBOX_OP_CODE_CLEAR_PORT_ERRORINFO, req_len, rsp,
			 rsp_len, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = req_len;
	op_param.data = req;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_clear_port_status(struct fsubdev *sd,
			  struct mbdb_op_clear_port_status *req,
			  struct mbdb_op_clear_port_status *rsp, mbox_cb cb,
			  void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;
	size_t req_len;
	size_t rsp_len;

	if (!req || !rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	req_len = sizeof(*req);
	rsp_len = sizeof(*rsp);

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_CLEAR_PORT_STATUS,
						 req_len, rsp, rsp_len, &cw,
						 cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = req_len;
	op_param.data = req;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_rpipe_set(struct fsubdev *sd, struct mbdb_op_rpipe_set *req,
		  struct mbdb_op_rpipe_set *rsp, mbox_cb cb, void *context,
		  bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;
	size_t req_len;
	size_t rsp_len;

	if (!req || !rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	req_len = sizeof(*req) + (req->num_entries * sizeof(u8));
	rsp_len = sizeof(*rsp) + (req->num_entries * sizeof(u8));
	if (req_len > MBOX_PARAM_AREA_IN_BYTES ||
	    rsp_len > MBOX_PARAM_AREA_IN_BYTES) {
		sd_err(sd, "%s: Too many entries specified.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_RPIPE_SET,
						 req_len, rsp, rsp_len, &cw, cb,
						 context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = req_len;
	op_param.data = req;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_rpipe_get(struct fsubdev *sd, struct mbdb_op_rpipe_get *req,
		  struct mbdb_op_rpipe_get *rsp, mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;
	size_t req_len;
	size_t rsp_len;

	if (!req || !rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	req_len = sizeof(*req);
	rsp_len = sizeof(*rsp) + (req->num_entries * sizeof(u8));
	if (rsp_len > MBOX_PARAM_AREA_IN_BYTES) {
		sd_err(sd, "%s: Too many entries specified.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_RPIPE_GET,
						 req_len, rsp, rsp_len, &cw, cb,
						 context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = req_len;
	op_param.data = req;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_portstateinfo_set(struct fsubdev *sd, struct mbdb_op_portstateinfo *req,
			  struct mbdb_op_portstateinfo *rsp, mbox_cb cb,
			  void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;
	size_t req_len;
	size_t rsp_len;

	if (!req || !rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	req_len = sizeof(*req) + (hweight_long(req->port_mask) *
				  sizeof(struct portstateinfo));
	rsp_len = sizeof(*rsp) + (hweight_long(req->port_mask) *
				  sizeof(struct portstateinfo));
	if (req_len > MBOX_PARAM_AREA_IN_BYTES ||
	    rsp_len > MBOX_PARAM_AREA_IN_BYTES) {
		sd_err(sd, "%s: Too many ports specified.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_PORTSTATEINFO_SET,
						 req_len, rsp, rsp_len, &cw,
						 cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = req_len;
	op_param.data = req;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_portstateinfo_get(struct fsubdev *sd, u32 port_mask,
			  struct mbdb_op_portstateinfo *rsp, mbox_cb cb,
			  void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;
	size_t req_len;
	size_t rsp_len;

	if (!rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	req_len = sizeof(port_mask);
	rsp_len = sizeof(*rsp) + (hweight_long(port_mask) *
				  sizeof(struct portstateinfo));
	if (rsp_len > MBOX_PARAM_AREA_IN_BYTES) {
		sd_err(sd, "%s: Too many ports specified.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_PORTSTATEINFO_GET,
						 req_len, rsp, rsp_len, &cw, cb,
						 context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = req_len;
	op_param.data = &port_mask;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_portinfo_set(struct fsubdev *sd, struct mbdb_op_portinfo *req,
		     struct mbdb_op_portinfo *rsp, mbox_cb cb, void *context,
		     bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;
	size_t req_len;
	size_t rsp_len;

	if (!req || !rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	req_len = sizeof(*req) + (hweight_long(req->port_mask) *
				  sizeof(struct portinfo));
	rsp_len = sizeof(*rsp) + (hweight_long(req->port_mask) *
				  sizeof(struct portinfo));
	if (req_len > MBOX_PARAM_AREA_IN_BYTES ||
	    rsp_len > MBOX_PARAM_AREA_IN_BYTES) {
		sd_err(sd, "%s: Too many ports specified.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_PORTINFO_SET,
						 req_len, rsp, rsp_len, &cw,
						 cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = req_len;
	op_param.data = req;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_portinfo_get(struct fsubdev *sd, u32 port_mask,
		     struct mbdb_op_portinfo *rsp, mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;
	size_t req_len;
	size_t rsp_len;

	if (!rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	req_len = sizeof(port_mask);
	rsp_len = sizeof(*rsp) + (hweight_long(port_mask) *
				  sizeof(struct portinfo));
	if (rsp_len > MBOX_PARAM_AREA_IN_BYTES) {
		sd_err(sd, "%s: Too many ports specified.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_PORTINFO_GET,
						 req_len, rsp, rsp_len, &cw, cb,
						 context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = req_len;
	op_param.data = &port_mask;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_switchinfo_set(struct fsubdev *sd, struct mbdb_op_switchinfo *req,
		       struct mbdb_op_switchinfo *rsp, mbox_cb cb,
		       void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;
	size_t req_len;
	size_t rsp_len;

	if (!req || !rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	req_len = sizeof(*req);
	rsp_len = sizeof(*rsp);

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_SWITCHINFO_SET,
						 req_len, rsp, rsp_len, &cw,
						 cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = req_len;
	op_param.data = req;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_switchinfo_get(struct fsubdev *sd, struct mbdb_op_switchinfo *rsp,
		       mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;
	size_t rsp_len;

	if (!rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	rsp_len = sizeof(*rsp);

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_SWITCHINFO_GET, 0,
						 rsp, rsp_len, &cw, cb, context,
						 false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_packet_buffer_info(struct fsubdev *sd,
			   struct mbdb_op_packet_buffer_info_rsp *rsp,
			   mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!rsp) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_PACKET_BUFFER_INFO, 0, rsp, sizeof(*rsp),
		 &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_linkmgr_trace(struct fsubdev *sd, u32 port, u32 addr, u32 param3,
		      mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data[2];
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_TRACE,
		 sizeof(port) + sizeof(addr) + sizeof(param3), NULL, 0, &cw, cb,
		 context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data[0] = port | (u64)addr << 32;
	data[1] = param3;
	op_param.len = sizeof(port) + sizeof(addr) + sizeof(param3);
	op_param.data = data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_port_diag_mode_set(struct fsubdev *sd, u32 port, u32 enable,
				   mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_DIAGNOSTIC_MODE_SET,
		 sizeof(port) + sizeof(enable), NULL, 0, &cw, cb, context,
		 posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data = port | (u64)enable << 32;
	op_param.len = sizeof(port) + sizeof(enable);
	op_param.data = &data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_port_diag_mode_get(struct fsubdev *sd, u32 port, u32 *result,
				   mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_DIAGNOSTIC_MODE_GET,
		 sizeof(port), result, sizeof(*result), &cw, cb, context,
		 false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(port);
	op_param.data = &port;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_qsfp_mgr_force(struct fsubdev *sd, u32 port, u32 how, mbox_cb cb,
		       void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_QSFP_MGR_FORCE, sizeof(port) + sizeof(how),
		 NULL, 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data = port | (u64)how << 32;
	op_param.len = sizeof(port) + sizeof(how);
	op_param.data = &data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_util_proc_call_address_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
				   void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_UTIL_PROC_CALL_ADDRESS_GET, 0, result,
		 sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_task_start(struct fsubdev *sd,
		   struct mbdb_op_task_start_req *task_start_req, u32 *result,
		   mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!task_start_req || !result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_TASK_START,
						 sizeof(*task_start_req),
						 result, sizeof(*result), &cw,
						 cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(*task_start_req);
	op_param.data = task_start_req;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_ini_tables_addr_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
			    void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_INI_TABLES_ADDR_GET, 0, result,
		sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_ini_port_table_field_get(struct fsubdev *sd, u32 port, u32 field,
				 u32 *result, mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_INI_PORT_TABLE_FIELD_GET,
		 sizeof(port) + sizeof(field), result, sizeof(*result), &cw, cb,
		 context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data = port | (u64)field << 32;
	op_param.len = sizeof(port) + sizeof(field);
	op_param.data = &data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_ini_system_table_field_get(struct fsubdev *sd, u32 field, u32 *result,
				   mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_INI_SYSTEM_TABLE_FIELD_GET,
		 sizeof(field), result, sizeof(*result), &cw, cb, context,
		 false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(field);
	op_param.data = &field;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_sysreg_wr(struct fsubdev *sd, u32 sys_reg, u32 value, mbox_cb cb,
		  void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_SYSREG_WR, sizeof(sys_reg) + sizeof(value),
		 NULL, 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data = sys_reg | (u64)value << 32;
	op_param.len = sizeof(sys_reg) + sizeof(value);
	op_param.data = &data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_sysreg_rd(struct fsubdev *sd, u32 sys_reg, u32 *result, mbox_cb cb,
		  void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_SYSREG_RD, sizeof(sys_reg),
		 result, sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(sys_reg);
	op_param.data = &sys_reg;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_call_routine(struct fsubdev *sd,
		     struct mbdb_op_call_routine_req *call_req, mbox_cb cb,
		     void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!call_req) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_CALL_ROUTINE,
						 sizeof(*call_req), NULL, 0,
						 &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(*call_req);
	op_param.data = call_req;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_noop(struct fsubdev *sd, mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_NOOP, 0, NULL,
						 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_ini_port_table_field_set(struct fsubdev *sd, u32 port, u32 field,
				 u32 value, mbox_cb cb, void *context,
				 bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data[2];
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_INI_PORT_TABLE_FIELD_SET,
		 sizeof(port) + sizeof(field) + sizeof(value), NULL, 0, &cw,
		 cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data[0] = port | (u64)field << 32;
	data[1] = value;
	op_param.len = sizeof(port) + sizeof(field) + sizeof(value);
	op_param.data = data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_ini_system_table_field_set(struct fsubdev *sd, u32 field, u32 value,
				   mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_INI_SYSTEM_TABLE_FIELD_SET,
		 sizeof(field) + sizeof(value), NULL, 0, &cw, cb, context,
		 posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data = field | (u64)value << 32;
	op_param.len = sizeof(field) + sizeof(value);
	op_param.data = &data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_shutdown_all_ports(struct fsubdev *sd, mbox_cb cb,
				   void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_SHUTDOWN_ALL_PORTS, 0, NULL, 0, &cw,
		 cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_linkmgr_psc_trap_ack(struct fsubdev *sd, mbox_cb cb, void *context,
			     bool posted)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ACKNOWLEDGE,
		 0, NULL, 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_linkmgr_psc_trap_ena_set(struct fsubdev *sd, u32 enabled, mbox_cb cb,
				 void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ENABLE_SET,
		 sizeof(enabled), NULL, 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(enabled);
	op_param.data = &enabled;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_psc_trap_ena_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
				 void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ENABLE_GET,
		 0, result, sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_linkmgr_port_beacon_set(struct fsubdev *sd, u32 port, bool enable,
				mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	u64 params;
	struct mbdb_op_param op_param;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_BEACON_SET, sizeof(params),
		 NULL, 0, &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	params = port | (u64)enable << 32;
	op_param.len = sizeof(params);
	op_param.data = &params;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_port_beacon_get(struct fsubdev *sd, u32 port, u32 *result,
				mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_BEACON_GET, sizeof(port),
		 result, sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(port);
	op_param.data = &port;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_ps_get(struct fsubdev *sd, u32 port, u32 *result, mbox_cb cb,
		       void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_STATES_GET, sizeof(port),
		 result, sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(port);
	op_param.data = &port;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_qsfp_write(struct fsubdev *sd, u32 port, u32 bytes,
		   u32 offset, u32 addr, u32 *result, mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data[2];
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_QSFP_WRITE,
						 sizeof(port) + sizeof(bytes) +
						 sizeof(offset) + sizeof(addr),
						 result, sizeof(*result), &cw,
						 cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data[0] = port | (u64)bytes << 32;
	data[1] = offset | (u64)addr << 32;
	op_param.len = sizeof(port) + sizeof(bytes) + sizeof(offset) +
			sizeof(addr);
	op_param.data = data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_qsfp_read(struct fsubdev *sd, u32 port, u32 bytes, u32 offset, u32 addr,
		  u32 *result, mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data[2];
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_QSFP_READ,
						 sizeof(port) + sizeof(bytes) +
						 sizeof(offset) + sizeof(addr),
						 result, sizeof(*result), &cw,
						 cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data[0] = port | (u64)bytes << 32;
	data[1] = offset | (u64)addr << 32;
	op_param.len = sizeof(port) + sizeof(bytes) + sizeof(offset) +
			sizeof(addr);
	op_param.data = data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_qsfp_present(struct fsubdev *sd, u32 port, u32 *result, mbox_cb cb,
		     void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_QSFP_PRESENT,
						 sizeof(port), result,
						 sizeof(*result), &cw, cb,
						 context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(port);
	op_param.data = &port;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_qsfp_faulted(struct fsubdev *sd, u32 port, u32 *result, mbox_cb cb,
		     void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_QSFP_FAULTED,
						 sizeof(port), result,
						 sizeof(*result), &cw, cb,
						 context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(port);
	op_param.data = &port;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_qsfpmgr_faulted_first(struct fsubdev *sd, u32 *result, mbox_cb cb,
			      void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_QSFP_MGR_FAULTED_FIRST,
		 0, result, sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_qsfpmgr_fault_trap_ack(struct fsubdev *sd, mbox_cb cb, void *context,
			       bool posted)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ACKNOWLEDGE, 0, NULL, 0,
		 &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_qsfpmgr_fault_trap_ena_set(struct fsubdev *sd, u32 enabled, mbox_cb cb,
				   void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ENABLE_SET,
		 sizeof(enabled), NULL, 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(enabled);
	op_param.data = &enabled;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_qsfpmgr_fault_trap_ena_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
				   void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ENABLE_GET,
		 0, result, sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_qsfpmgr_temp_max_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
			     void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_QSFP_MGR_TEMPERATURE_MAX_DETECTED_GET,
		 0, result, sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_qsfpmgr_temp_array_addr_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
				    void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_QSFP_MGR_TEMPERATURE_ARRAY_ADDRESS_GET,
		 0, result, sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_qsfpmgr_power_used_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
			       void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_QSFP_MGR_POWER_USED_GET, 0, result,
		 sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_qsfpmgr_power_alloc_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
				void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_QSFP_MGR_POWER_ALLOCATED_GET, 0, result,
		 sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_ini_port_type_get(struct fsubdev *sd, u32 port, u32 *result, mbox_cb cb,
			  void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_INI_PORT_TYPE_GET, sizeof(port), result,
		 sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(port);
	op_param.data = &port;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_asic_temp_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
		      void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_ASIC_TEMP_GET,
						 0, result, sizeof(*result),
						 &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_reset(struct fsubdev *sd, mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_RESET, 0,
						 NULL, 0, &cw, cb, context,
						 posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_linkmgr_port_csr_wr(struct fsubdev *sd, u32 port, u32 offset, u64 val,
			    mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data[2];
	u64 cw;
	u32 port_base = get_link_mgr_port_base(port);

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_CSR_WR,
		 sizeof(port) + sizeof(offset), NULL,
		 0, &cw, cb, context, posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data[0] = port | (u64)(port_base + offset) << 32;
	data[1] = val;
	op_param.len = sizeof(port) + sizeof(offset) + sizeof(val);
	op_param.data = data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_port_csr_rd(struct fsubdev *sd, u32 port, u32 offset,
			    u64 *result, mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data;
	u64 cw;
	u32 port_base = get_link_mgr_port_base(port);

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_CSR_RD,
		 sizeof(port) + sizeof(offset), result,
		 sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data = port | (u64)(port_base + offset) << 32;
	op_param.len = sizeof(port) + sizeof(offset);
	op_param.data = &data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_port_maj_phystate_set(struct fsubdev *sd, u32 port,
				      u32 new_state, u32 *result, mbox_cb cb,
				      void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_MAJOR_PHYSICAL_STATE_SET,
		 sizeof(port) + sizeof(new_state), result,
		 sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data = port | (u64)new_state << 32;
	op_param.len = sizeof(port) + sizeof(new_state);
	op_param.data = &data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_linkmgr_port_link_state_set(struct fsubdev *sd, u32 port, u32 new_state,
				    u32 *result, mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 data;
	u64 cw;

	if (!result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox
		(sd, MBOX_OP_CODE_LINK_MGR_PORT_LINK_STATE_SET,
		 sizeof(port) + sizeof(new_state), result,
		 sizeof(*result), &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	data = port | (u64)new_state << 32;
	op_param.len = sizeof(port) + sizeof(new_state);
	op_param.data = &data;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_node_guid_get(struct fsubdev *sd, u64 *node_guid, mbox_cb cb,
		      void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;

	if (!node_guid) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_NODE_GUID_GET, 0,
						 node_guid, sizeof(*node_guid),
						 &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_ini_loaded_set(struct fsubdev *sd, mbox_cb cb, void *context,
		       bool posted)
{
	struct mbdb_ibox *ibox = NULL;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_INI_LOADED_SET, 0,
						 NULL, 0, &cw, cb, context,
						 posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_ini_table_load(struct fsubdev *sd,
		       struct mbdb_op_ini_table_load_req *info, u32 *result,
		       mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!info || !result) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd,
						 MBOX_OP_CODE_INI_TABLE_LOAD,
						 sizeof(*info), result,
						 sizeof(*result), &cw, cb,
						 context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(*info);
	op_param.data = info;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_free(struct fsubdev *sd, u32 addr, mbox_cb cb, void *context,
	     bool posted)
{
	struct mbdb_ibox *ibox = NULL;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!addr) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_FREE,
						 sizeof(addr),
						 NULL, 0, &cw, cb, context,
						 posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(addr);
	op_param.data = &addr;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_walloc(struct fsubdev *sd, u16 dwords, u32 *addr, mbox_cb cb,
	       void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!addr) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_WALLOC,
						 sizeof(dwords), addr,
						 sizeof(*addr), &cw, cb,
						 context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(dwords);
	op_param.data = &dwords;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_fw_start(struct fsubdev *sd, mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	u32 result;
	u64 cw;

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_FW_START,
						 0, &result, sizeof(result),
						 &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	return ops_execute(sd, &cw, 0, NULL, ibox);
}

int ops_csr_raw_write(struct fsubdev *sd, u32 addr, const void *data, u32 len,
		      mbox_cb cb, void *context, bool posted)
{
	struct mbdb_ibox *ibox = NULL;
	struct mbdb_op_param op_param[2];
	u64 cw;
	u64 addr_64;

	if (!data) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	if (len > MBOX_WRITE_DATA_SIZE_IN_BYTES) {
		sd_err(sd, "%s: Mail Box size exceeded. len %d\n", __func__,
		       len);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_CSR_RAW_WR,
						 len, NULL, 0, &cw, cb, context,
						 posted);
	if (IS_ERR(ibox))
		return -ENOMEM;

	/* Actual parameter area is 32 bits addr/32 bits reserved */
	addr_64 = addr;

	op_param[0].len = sizeof(addr_64);
	op_param[0].data = &addr_64;
	op_param[1].len = len;
	op_param[1].data = data;

	return ops_execute(sd, &cw, 2, op_param, ibox);
}

int ops_csr_raw_read(struct fsubdev *sd, u32 addr, size_t read_len, void *data,
		     mbox_cb cb, void *context)
{
	struct mbdb_ibox *ibox;
	struct mbdb_op_param op_param;
	u64 cw;

	if (!data) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	if (read_len > MBOX_READ_DATA_SIZE_IN_BYTES) {
		sd_err(sd, "%s: Mail Box size exceeded. read_len %ld\n",
		       __func__, read_len);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_CSR_RAW_RD,
						 sizeof(addr), data, read_len,
						 &cw, cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	op_param.len = sizeof(addr);
	op_param.data = &addr;

	return ops_execute(sd, &cw, 1, &op_param, ibox);
}

int ops_fw_version(struct fsubdev *sd,
		   struct mbdb_op_fw_version_rsp *fw_version, mbox_cb cb,
		   void *context)
{
	struct mbdb_ibox *ibox;
	u64 cw;
	int ret;

	if (!fw_version) {
		sd_err(sd, "%s: Missing parameter.\n", __func__);
		return -EINVAL;
	}

	ibox = mbdb_op_build_cw_and_acquire_ibox(sd, MBOX_OP_CODE_FW_VERSION,
						 0, fw_version,
						 sizeof(*fw_version), &cw,
						 cb, context, false);
	if (IS_ERR(ibox))
		return -ENOMEM;

	ret = ops_execute(sd, &cw, 0, NULL, ibox);
	if (!ret)
		memcpy(sd->supported_opcodes, fw_version->supported_opcodes,
		       sizeof(fw_version->supported_opcodes));

	return ret;
}

int ops_mem_posted_wr(struct fsubdev *sd, u32 addr, const u8 *data, u32 len)
{
	int ret;

	while (len) {
		int xfer_len = len <= MBOX_WRITE_DATA_SIZE_IN_BYTES
			       ? len : MBOX_WRITE_DATA_SIZE_IN_BYTES;
		ret = ops_csr_raw_write(sd, addr, data, xfer_len, NULL, NULL,
					true);
		if (ret)
			break;

		len -= xfer_len;
		addr += xfer_len;
		data += xfer_len;
	}

	return ret;
}

int ops_execute(struct fsubdev *sd, u64 *cw, u8 op_param_count,
		struct mbdb_op_param *op_param, struct mbdb_ibox *ibox)
{
	struct mbox_msg __iomem *mbox_msg;
	u64 __iomem *mbox_msg_param_area;
	int i;
	int ret = 0;

	mbox_msg = mbdb_outbox_acquire(sd, cw);
	if (IS_ERR(mbox_msg))
		return PTR_ERR(mbox_msg);

	writeq(*cw, &mbox_msg->cw);

	mbox_msg_param_area = mbox_msg->param_area;

	for (i = 0; i < op_param_count; i++) {
		io_write(mbox_msg_param_area, op_param[i].data,
			 op_param[i].len);
		mbox_msg_param_area += op_param[i].len >> 3;
	}

	trace_iaf_mbx_exec(sd->fdev->pd->index, sd_index(sd),
			   mbdb_mbox_op_code(*cw), mbdb_mbox_seq_no(*cw),
			   mbdb_mbox_tid(*cw), *cw);

	mbdb_outbox_release(sd);

	if (ibox && !ibox->cb) {
		ret = mbdb_ibox_wait(ibox);
		if (ret > 0) {
			*cw = ibox->cw;
			ret = ibox->rsp_status;
		}

		mbdb_ibox_release(ibox);
	}

	return ret;
}
