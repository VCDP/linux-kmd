/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#ifndef OPS_H_INCLUDED
#define OPS_H_INCLUDED

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/bitops.h>

#include "iaf_drv.h"

/* MBOX opcodes */
/* Firmware Load and Generic CSR Access opcodes */
#define MBOX_OP_CODE_FW_VERSION 0
#define MBOX_OP_CODE_CSR_RAW_RD 1
#define MBOX_OP_CODE_CSR_RAW_WR 2
#define MBOX_OP_CODE_FW_START 3

/* Configuration Required opcodes */
#define MBOX_OP_CODE_WALLOC 4
#define MBOX_OP_CODE_FREE 5
#define MBOX_OP_CODE_INI_TABLE_LOAD 6
#define MBOX_OP_CODE_INI_LOADED_SET 7
#define MBOX_OP_CODE_NODE_GUID_GET 8
#define MBOX_OP_CODE_LINK_MGR_PORT_LINK_STATE_SET 9
#define MBOX_OP_CODE_LINK_MGR_PORT_MAJOR_PHYSICAL_STATE_SET 10
#define MBOX_OP_CODE_LINK_MGR_PORT_CSR_RD 11
#define MBOX_OP_CODE_LINK_MGR_PORT_CSR_WR 12

/* System Management Related opcodes */
#define MBOX_OP_CODE_RESET 13
#define MBOX_OP_CODE_ASIC_TEMP_GET 14
#define MBOX_OP_CODE_INI_PORT_TYPE_GET 15
#define MBOX_OP_CODE_QSFP_MGR_POWER_ALLOCATED_GET 16
#define MBOX_OP_CODE_QSFP_MGR_POWER_USED_GET 17
#define MBOX_OP_CODE_QSFP_MGR_TEMPERATURE_ARRAY_ADDRESS_GET 18
#define MBOX_OP_CODE_QSFP_MGR_TEMPERATURE_MAX_DETECTED_GET 19
#define MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ENABLE_GET 20
#define MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ENABLE_SET 21
#define MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_NOTIFICATION 22
#define MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ACKNOWLEDGE 23
#define MBOX_OP_CODE_QSFP_MGR_FAULTED_FIRST 24
#define MBOX_OP_CODE_QSFP_FAULTED 25
#define MBOX_OP_CODE_QSFP_PRESENT 26
#define MBOX_OP_CODE_QSFP_READ 27
#define MBOX_OP_CODE_QSFP_WRITE 28
#define MBOX_OP_CODE_LINK_MGR_PORT_STATES_GET 29
#define MBOX_OP_CODE_LINK_MGR_PORT_BEACON_GET 30
#define MBOX_OP_CODE_LINK_MGR_PORT_BEACON_SET 31
#define MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ENABLE_GET 32
#define MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ENABLE_SET 33
#define MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_NOTIFICATION 34
#define MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ACKNOWLEDGE 35
#define MBOX_OP_CODE_LINK_MGR_SHUTDOWN_ALL_PORTS 36

/* Configuration Related Opcodes to Support Workarounds or Debugging */
#define MBOX_OP_CODE_INI_SYSTEM_TABLE_FIELD_SET 37
#define MBOX_OP_CODE_INI_PORT_TABLE_FIELD_SET 38

/* Firmware and System Related Debugging opcodes */
#define MBOX_OP_CODE_NOOP 39
#define MBOX_OP_CODE_CALL_ROUTINE 40
#define MBOX_OP_CODE_SYSREG_RD 41
#define MBOX_OP_CODE_SYSREG_WR 42
#define MBOX_OP_CODE_INI_SYSTEM_TABLE_FIELD_GET 43
#define MBOX_OP_CODE_INI_PORT_TABLE_FIELD_GET 44
#define MBOX_OP_CODE_INI_TABLES_ADDR_GET 45
#define MBOX_OP_CODE_TASK_START 46
#define MBOX_OP_CODE_UTIL_PROC_CALL_ADDRESS_GET 47
#define MBOX_OP_CODE_QSFP_MGR_FORCE 48
#define MBOX_OP_CODE_LINK_MGR_PORT_DIAGNOSTIC_MODE_GET 49
#define MBOX_OP_CODE_LINK_MGR_PORT_DIAGNOSTIC_MODE_SET 50
#define MBOX_OP_CODE_LINK_MGR_TRACE 51

/* Packet over MBOX related opcodes */
#define MBOX_OP_CODE_PACKET_BUFFER_INFO 52

/* Fabric Discovery, Configuration and basic Monitoring  Related Opcodes */

#define MBOX_OP_CODE_SWITCHINFO_GET 53
#define MBOX_OP_CODE_SWITCHINFO_SET 54
#define MBOX_OP_CODE_PORTINFO_GET 55
#define MBOX_OP_CODE_PORTINFO_SET 56
#define MBOX_OP_CODE_PORTSTATEINFO_GET 57
#define MBOX_OP_CODE_PORTSTATEINFO_SET 58
#define MBOX_OP_CODE_RPIPE_GET 59
#define MBOX_OP_CODE_RPIPE_SET 60
#define MBOX_OP_CODE_CLEAR_PORT_STATUS 61
#define MBOX_OP_CODE_CLEAR_PORT_ERRORINFO 62

/* trap related mbox messages relating to link health */

#define MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ENABLE_GET 63
#define MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ENABLE_SET 64
#define MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_NOTIFICATION 65
#define MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ACKNOWLEDGE 66
#define MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ENABLE_GET 67
#define MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ENABLE_SET 68
#define MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_NOTIFICATION 69
#define MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ACKNOWLEDGE 70

#define MBOX_RSP_STATUS_OK            0
#define MBOX_RSP_STATUS_SEQ_NO_ERROR  1
#define MBOX_RSP_STATUS_OP_CODE_ERROR 2
#define MBOX_RSP_STATUS_LOGICAL_ERROR 3
#define MBOX_RSP_STATUS_RETRY         4
#define MBOX_RSP_STATUS_DENIED        5 // access was denied

// MBOX declarations
#define MBDB_GP_DATA_U64_LOCATIONS 512
#define MBOX_SIZE_IN_BYTES (MBDB_GP_DATA_U64_LOCATIONS * sizeof(u64) / 2)
#define MBOX_SIZE_IN_QWORDS (MBDB_GP_DATA_U64_LOCATIONS / 2)
#define MBOX_CW_FIELD_TYPE u64
#define MBOX_CW_SIZE_IN_BYTES sizeof(MBOX_CW_FIELD_TYPE)
#define MBOX_CW_SIZE_IN_QWORDS 1
#define MBOX_PARAM_AREA_IN_BYTES (MBOX_SIZE_IN_BYTES - MBOX_CW_SIZE_IN_BYTES)
#define MBOX_PARAM_AREA_IN_QWORDS (MBOX_SIZE_IN_QWORDS - MBOX_CW_SIZE_IN_QWORDS)
#define MBOX_READ_DATA_SIZE_IN_BYTES MBOX_PARAM_AREA_IN_BYTES
/* The first 8 bytes (u64) is the address where the data is to be written */
#define MBOX_WRITE_DATA_SIZE_IN_BYTES (MBOX_PARAM_AREA_IN_BYTES - sizeof(u64))

enum mbdb_msg_type {
	MBOX_RESPONSE = 0,
	MBOX_REQUEST  = 1
};

enum posted {
	MBOX_RESPONSE_REQUESTED    = 0,
	MBOX_NO_RESPONSE_REQUESTED = 1
};

struct mbox_msg {
	u64 cw;
	u64 param_area[MBOX_PARAM_AREA_IN_QWORDS];
};

struct mbdb_op_fw_version_rsp {
	/* this version will be 0 */
	u8 mbox_version;
	/* 0 = bootloader, 1 = run-time firmware */
	u8 environment;
	/*  ASCII-NUL terminated version string,
	 * e.g "w.x.y.z" 255.255.255.255
	 */
	u8 fw_version_string[22];
	/* bit mask of opcodes that the environment supports as
	 * requests from the host
	 */
	u64 supported_opcodes[SUPPORTED_OPCODES_ARRAY_ELEMENTS];
};

struct mbdb_op_ini_table_load_req {
	u32 header1;
	u32 header2;
	u32 address;
	u32 crc;
};

struct mbdb_op_call_routine_req {
	u32 proc;
	u32 param[4];
};

struct mbdb_op_task_start_req {
	u32 proc;
	u32 stack_size;
	u32 priority;
	u32 task_param;
};

#define DB0_BUFFERS_MASK 0x000000FFUL
#define DB0_BUFFERS_SHIFT 24
#define DB1_BUFFERS_MASK 0x000000FFUL
#define DB1_BUFFERS_SHIFT 16
#define DB2_BUFFERS_MASK 0x000000FFUL
#define DB2_BUFFERS_SHIFT 8
#define DB3_BUFFERS_MASK 0x000000FFUL
#define DB3_BUFFERS_SHIFT 0
struct mbdb_op_packet_buffer_info_rsp {
	u32 buffer_size_in_bytes;
	u32 db_buffers;
	u32 db0_buffer_base_addr;
	u32 db1_buffer_base_addr;
	u32 db2_buffer_base_addr;
	u32 db3_buffer_base_addr;
};

struct portstateinfo {
	u8 link_speed_active;
	u8 link_width_active;
	u8 link_width_downgrade_rx_active;
	u8 link_width_downgrade_tx_active;
	u8 port_state_port_phys_state;
	u8 oldr_nn_lqi;
};

struct mbdb_op_portstateinfo {
	u32 port_mask;
	struct portstateinfo per_portstateinfo[0];
};

struct mbdb_op_rpipe_get {
	u32 port_number;
	u16 start_index;
	u16 num_entries;
	u8 rpipe_data[0];
};

struct mbdb_op_rpipe_set {
	u32 port_mask;
	u16 start_index;
	u16 num_entries;
	u8 rpipe_data[0];
};

struct mbdb_op_clear_port_status {
	u32 port_mask;
	u32 counter_select_mask;
};

struct mbdb_op_clear_port_errorinfo {
	u32 port_mask;
	u32 error_info_select_mask;
};

typedef void (*mbox_cb)(void *response, u16 rsp_len, void *context, u8 status);

struct mbdb_op_param {
	size_t len;
	const void *data;
};

struct mbdb_ibox {
	struct work_struct work;
	struct list_head ibox_list_link;
	struct mbdb *mbdb;
	struct completion ibox_full;
	mbox_cb cb;
	void *context;
	u64 cw;
	u32 tid;
	u16 rsp_len;
	void *response;
	int rsp_status;
	u8 op_code;
};

static inline bool mbdb_opcode_supported(const struct fsubdev *sd, u8 opcode)
{
	/* opcode / 64 & (1 << (opcode % 64)) */
	return sd->supported_opcodes[opcode >> 6] & (1Ull << (opcode & 0x3f));
}

struct mbdb_ibox *
mbdb_op_build_cw_and_acquire_ibox(struct fsubdev *sd, u8 op_code,
				  size_t parm_len, void *response, u32 rsp_len,
				  u64 *cw, mbox_cb cb, void *context,
				  bool posted);

int ops_linkmgr_port_lqi_trap_ack(struct fsubdev *sd, mbox_cb cb, void *context,
				  bool posted);

int ops_linkmgr_port_lqi_trap_ena_set(struct fsubdev *sd, u32 enabled,
				      mbox_cb cb, void *context, bool posted);

int ops_linkmgr_port_lqi_trap_ena_get(struct fsubdev *sd, u32 *result,
				      mbox_cb cb, void *context);

int ops_linkmgr_port_lwd_trap_ack(struct fsubdev *sd, mbox_cb cb, void *context,
				  bool posted);

int ops_linkmgr_port_lwd_trap_ena_set(struct fsubdev *sd, u32 enabled,
				      mbox_cb cb, void *context, bool posted);

int ops_linkmgr_port_lwd_trap_ena_get(struct fsubdev *sd, u32 *result,
				      mbox_cb cb, void *context);

int ops_clear_port_errorinfo(struct fsubdev *sd,
			     struct mbdb_op_clear_port_errorinfo *req,
			     struct mbdb_op_clear_port_errorinfo *rsp,
			     mbox_cb cb, void *context, bool posted);

int ops_clear_port_status(struct fsubdev *sd,
			  struct mbdb_op_clear_port_status *req,
			  struct mbdb_op_clear_port_status *rsp, mbox_cb cb,
			  void *context, bool posted);

int ops_rpipe_set(struct fsubdev *sd, struct mbdb_op_rpipe_set *req,
		  struct mbdb_op_rpipe_set *rsp, mbox_cb cb, void *context,
		  bool posted);

int ops_rpipe_get(struct fsubdev *sd, struct mbdb_op_rpipe_get *req,
		  struct mbdb_op_rpipe_get *rsp, mbox_cb cb, void *context);

int ops_portstateinfo_set(struct fsubdev *sd, struct mbdb_op_portstateinfo *req,
			  struct mbdb_op_portstateinfo *rsp, mbox_cb cb,
			  void *context, bool posted);

int ops_portstateinfo_get(struct fsubdev *sd, u32 port_mask,
			  struct mbdb_op_portstateinfo *rsp, mbox_cb cb,
			  void *context);

int ops_portinfo_set(struct fsubdev *sd, struct mbdb_op_portinfo *req,
		     struct mbdb_op_portinfo *rsp, mbox_cb cb, void *context,
		     bool posted);

int ops_portinfo_get(struct fsubdev *sd, u32 port_mask,
		     struct mbdb_op_portinfo *rsp, mbox_cb cb, void *context);

int ops_switchinfo_set(struct fsubdev *sd, struct mbdb_op_switchinfo *req,
		       struct mbdb_op_switchinfo *rsp, mbox_cb cb,
		       void *context, bool posted);

int ops_switchinfo_get(struct fsubdev *sd, struct mbdb_op_switchinfo *rsp,
		       mbox_cb cb, void *context);

int ops_packet_buffer_info(struct fsubdev *sd,
			   struct mbdb_op_packet_buffer_info_rsp *rsp,
			   mbox_cb cb, void *context);

int ops_linkmgr_trace(struct fsubdev *sd, u32 port, u32 addr, u32 param3,
		      mbox_cb cb, void *context, bool posted);

int ops_linkmgr_port_diag_mode_set(struct fsubdev *sd, u32 port, u32 enable,
				   mbox_cb cb, void *context, bool posted);

int ops_linkmgr_port_diag_mode_get(struct fsubdev *sd, u32 port, u32 *result,
				   mbox_cb cb, void *context);

int ops_qsfp_mgr_force(struct fsubdev *sd, u32 port, u32 how, mbox_cb cb,
		       void *context, bool posted);

int ops_util_proc_call_address_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
				   void *context);

int ops_task_start(struct fsubdev *sd,
		   struct mbdb_op_task_start_req *task_start_req, u32 *result,
		   mbox_cb cb, void *context);

int ops_ini_tables_addr_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
			    void *context);

int ops_ini_port_table_field_get(struct fsubdev *sd, u32 port, u32 field,
				 u32 *result, mbox_cb cb, void *context);

int ops_ini_system_table_field_get(struct fsubdev *sd, u32 field, u32 *result,
				   mbox_cb cb, void *context);

int ops_sysreg_wr(struct fsubdev *sd, u32 sys_reg, u32 value, mbox_cb cb,
		  void *context, bool posted);

int ops_sysreg_rd(struct fsubdev *sd, u32 sys_reg, u32 *result, mbox_cb cb,
		  void *context);

int ops_call_routine(struct fsubdev *sd,
		     struct mbdb_op_call_routine_req *call_req, mbox_cb cb,
		     void *context, bool posted);

int ops_noop(struct fsubdev *sd, mbox_cb cb, void *context, bool posted);

int ops_ini_port_table_field_set(struct fsubdev *sd, u32 port, u32 field,
				 u32 value, mbox_cb cb, void *context,
				 bool posted);

int ops_ini_system_table_field_set(struct fsubdev *sd, u32 field, u32 value,
				   mbox_cb cb, void *context, bool posted);

int ops_linkmgr_shutdown_all_ports(struct fsubdev *sd, mbox_cb cb,
				   void *context, bool posted);

int ops_linkmgr_psc_trap_ack(struct fsubdev *sd, mbox_cb cb, void *context,
			     bool posted);

int ops_linkmgr_psc_trap_ena_set(struct fsubdev *sd, u32 enabled, mbox_cb cb,
				 void *context, bool posted);

int ops_linkmgr_psc_trap_ena_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
				 void *context);

int ops_linkmgr_port_beacon_set(struct fsubdev *sd, u32 port, bool enabled,
				mbox_cb cb, void *context);

int ops_linkmgr_port_beacon_get(struct fsubdev *sd, u32 port, u32 *result,
				mbox_cb cb, void *context);

int ops_linkmgr_ps_get(struct fsubdev *sd, u32 port, u32 *result, mbox_cb cb,
		       void *context);

int ops_qsfp_write(struct fsubdev *sd, u32 port, u32 bytes, u32 offset,
		   u32 addr, u32 *result, mbox_cb cb, void *context);

int ops_qsfp_read(struct fsubdev *sd, u32 port, u32 bytes, u32 offset, u32 addr,
		  u32 *result, mbox_cb cb, void *context);

int ops_qsfp_present(struct fsubdev *sd, u32 port, u32 *result, mbox_cb cb,
		     void *context);

int ops_qsfp_faulted(struct fsubdev *sd, u32 port, u32 *result, mbox_cb cb,
		     void *context);

int ops_qsfpmgr_faulted_first(struct fsubdev *sd, u32 *result, mbox_cb cb,
			      void *context);

int ops_qsfpmgr_fault_trap_ack(struct fsubdev *sd, mbox_cb cb, void *context,
			       bool posted);

int ops_qsfpmgr_fault_trap_ena_set(struct fsubdev *sd, u32 enabled, mbox_cb cb,
				   void *context, bool posted);

int ops_qsfpmgr_fault_trap_ena_get(struct fsubdev *sd, u32 *result,
				   mbox_cb cb, void *context);

int ops_qsfpmgr_temp_max_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
			     void *context);

int ops_qsfpmgr_temp_array_addr_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
				    void *context);

int ops_qsfpmgr_power_used_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
			       void *context);

int ops_qsfpmgr_power_alloc_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
				void *context);

int ops_ini_port_type_get(struct fsubdev *sd, u32 port, u32 *result, mbox_cb cb,
			  void *context);

int ops_asic_temp_get(struct fsubdev *sd, u32 *result, mbox_cb cb,
		      void *context);

int ops_reset(struct fsubdev *sd, mbox_cb cb, void *context, bool posted);

int ops_linkmgr_port_csr_wr(struct fsubdev *sd, u32 port, u32 offset, u64 val,
			    mbox_cb cb, void *context, bool posted);

int ops_linkmgr_port_csr_rd(struct fsubdev *sd, u32 port, u32 offset,
			    u64 *result, mbox_cb cb, void *context);

int ops_linkmgr_port_maj_phystate_set(struct fsubdev *sd, u32 port,
				      u32 new_state, u32 *result, mbox_cb cb,
				      void *context);

int ops_linkmgr_port_link_state_set(struct fsubdev *sd, u32 port, u32 new_state,
				    u32 *result, mbox_cb cb, void *context);

int ops_node_guid_get(struct fsubdev *sd, u64 *node_guid, mbox_cb cb,
		      void *context);

int ops_ini_loaded_set(struct fsubdev *sd, mbox_cb cb, void *context,
		       bool posted);

int ops_ini_table_load(struct fsubdev *sd,
		       struct mbdb_op_ini_table_load_req *info, u32 *result,
		       mbox_cb cb, void *context);

int ops_free(struct fsubdev *sd, u32 addr, mbox_cb cb, void *context,
	     bool posted);

int ops_walloc(struct fsubdev *sd, u16 dwords, u32 *addr, mbox_cb cb,
	       void *context);

int ops_fw_start(struct fsubdev *sd, mbox_cb cb, void *context);

int ops_csr_raw_write(struct fsubdev *sd, u32 addr, const void *data, u32 len,
		      mbox_cb cb, void *context, bool posted);

int ops_csr_raw_read(struct fsubdev *sd, u32 addr, size_t read_len, void *data,
		     mbox_cb cb, void *context);

int ops_fw_version(struct fsubdev *sd,
		   struct mbdb_op_fw_version_rsp *fw_version, mbox_cb cb,
		   void *context);

int ops_mem_posted_wr(struct fsubdev *sd, u32 addr, const u8 *data, u32 len);

int ops_execute(struct fsubdev *sd, u64 *cw, u8 op_param_count,
		struct mbdb_op_param *op_param, struct mbdb_ibox *ibox);

#endif
