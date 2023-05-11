/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#ifndef IAF_DRV_H_INCLUDED
#define IAF_DRV_H_INCLUDED

#include <linux/irqreturn.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/kref.h>

#include <drm/intel_iaf_platform.h>

#include "csr.h"

#define DRIVER_NAME "iaf"

#define SUPPORTED_OPCODES_ARRAY_ELEMENTS 4

/*
 * The maximum nubmer of tiles for the PVC product type.
 *
 * Revisit if future product types or variations are developed that require
 * a product type table.
 */
#define IAF_MAX_SUB_DEVS 2

/*
 * Device, subdevice and port message formats
 *
 * Device expands dev->name, sd/port expand relevant indices
 * ": " separates info. *_FMT ends in ": ", *_ID_FMT does not
 */

#define DEV_ID_FMT "%s"
#define SD_ID_FMT "sd.%d"
#define PORT_ID_FMT "p.%d"

#define DEV_FMT DEV_ID_FMT ": "
#define SD_FMT SD_ID_FMT ": "
#define PORT_FMT PORT_ID_FMT ": "

#define DEV_SD_ID_FMT DEV_FMT "" SD_ID_FMT
#define SD_PORT_ID_FMT SD_FMT "" PORT_ID_FMT

#define DEV_SD_PORT_ID_FMT DEV_FMT "" SD_PORT_ID_FMT

#define DEV_SD_FMT DEV_FMT "" SD_FMT
#define SD_PORT_FMT SD_FMT "" PORT_FMT

#define DEV_SD_PORT_FMT DEV_FMT "" SD_PORT_FMT

/*
 * Subdevice-specific messaging
 */

#define sd_emerg(__sd, _fmt, ...) \
		do { \
			struct fsubdev *_sd = (__sd); \
			dev_emerg(sd_dev(_sd), SD_FMT _fmt, \
				  sd_index(_sd), ##__VA_ARGS__); \
		} while (0)

#define sd_alert(__sd, _fmt, ...) \
		do { \
			struct fsubdev *_sd = (__sd); \
			dev_alert(sd_dev(_sd), SD_FMT _fmt, \
				  sd_index(_sd), ##__VA_ARGS__); \
		} while (0)

#define sd_crit(__sd, _fmt, ...) \
		do { \
			struct fsubdev *_sd = (__sd); \
			dev_crit(sd_dev(_sd), SD_FMT _fmt, \
				 sd_index(_sd), ##__VA_ARGS__); \
		} while (0)

#define sd_err(__sd, _fmt, ...) \
		do { \
			struct fsubdev *_sd = (__sd); \
			dev_err(sd_dev(_sd), SD_FMT _fmt, \
				sd_index(_sd), ##__VA_ARGS__); \
		} while (0)

#define sd_warn(__sd, _fmt, ...) \
		do { \
			struct fsubdev *_sd = (__sd); \
			dev_warn(sd_dev(_sd), SD_FMT _fmt, \
				 sd_index(_sd), ##__VA_ARGS__); \
		} while (0)

#define sd_notice(__sd, _fmt, ...) \
		do { \
			struct fsubdev *_sd = (__sd); \
			dev_notice(sd_dev(_sd), SD_FMT _fmt, \
				   sd_index(_sd), ##__VA_ARGS__); \
		} while (0)

#define sd_info(__sd, _fmt, ...) \
		do { \
			struct fsubdev *_sd = (__sd); \
			dev_info(sd_dev(_sd), SD_FMT _fmt, \
				 sd_index(_sd), ##__VA_ARGS__); \
		} while (0)

#define sd_dbg(__sd, _fmt, ...) \
		do { \
			struct fsubdev *_sd = (__sd); \
			dev_dbg(sd_dev(_sd), SD_FMT _fmt, \
				sd_index(_sd), ##__VA_ARGS__); \
		} while (0)

/*
 * Port-specific messaging
 */

#define fport_emerg(__p, _fmt, ...) \
		do { \
			struct fport *_p = (__p); \
			dev_emerg(fport_dev(_p), SD_PORT_FMT _fmt, \
				  sd_index(_p->sd), _p->lpn, ##__VA_ARGS__); \
		} while (0)

#define fport_alert(__p, _fmt, ...) \
		do { \
			struct fport *_p = (__p); \
			dev_alert(fport_dev(_p), SD_PORT_FMT _fmt, \
				  sd_index(_p->sd), _p->lpn, ##__VA_ARGS__); \
		} while (0)

#define fport_crit(__p, _fmt, ...) \
		do { \
			struct fport *_p = (__p); \
			dev_crit(fport_dev(_p), SD_PORT_FMT _fmt, \
				 sd_index(_p->sd), _p->lpn, ##__VA_ARGS__); \
		} while (0)

#define fport_err(__p, _fmt, ...) \
		do { \
			struct fport *_p = (__p); \
			dev_err(fport_dev(_p), SD_PORT_FMT _fmt, \
				sd_index(_p->sd), _p->lpn, ##__VA_ARGS__); \
		} while (0)

#define fport_warn(__p, _fmt, ...) \
		do { \
			struct fport *_p = (__p); \
			dev_warn(fport_dev(_p), SD_PORT_FMT _fmt, \
				 sd_index(_p->sd), _p->lpn, ##__VA_ARGS__); \
		} while (0)

#define fport_notice(__p, _fmt, ...) \
		do { \
			struct fport *_p = (__p); \
			dev_notice(fport_dev(_p), SD_PORT_FMT _fmt, \
				   sd_index(_p->sd), _p->lpn, ##__VA_ARGS__); \
		} while (0)

#define fport_info(__p, _fmt, ...) \
		do { \
			struct fport *_p = (__p); \
			dev_info(fport_dev(_p), SD_PORT_FMT _fmt, \
				 sd_index(_p->sd), _p->lpn, ##__VA_ARGS__); \
		} while (0)

#define fport_dbg(__p, _fmt, ...) \
		do { \
			struct fport *_p = (__p); \
			dev_dbg(fport_dev(_p), SD_PORT_FMT _fmt, \
				sd_index(_p->sd), _p->lpn, ##__VA_ARGS__); \
		} while (0)

/**
 * struct mbdb_op_switchinfo - Per-IAF subdevice information
 * @guid: RO, from FUSES
 * @num_ports: RO, fixed, 8, 12, or 13 (TBD)
 * @slt_psc_ep0: RW, bit 0-4: max in-switch packet lifetime
 *		 RO, bit 5: redundant to PSC notification
 *		 RO, bit 6: Q7 presence?
 * @routing_mode_supported: RO, hierarchical/linear routing enumeration
 * @routing_mode_enabled: RW, hierarchical/linear routing enumeration
 * @lft_top: RW, max FID value
 *
 * Used with ops_switchinfo_set() and ops_switchinfo_get() to access
 * switch information for the entire subdevice. A copy is maintained in
 * &struct fsubdev.
 */
struct mbdb_op_switchinfo {
	u64 guid;
	u8 num_ports;
#define SLT_PSC_EP0_SWITCH_LIFETIME	GENMASK(4, 0)
#define SLT_PSC_EP0_PORT_STATE_CHANGE	BIT(5)
#define SLT_PSC_EP0_ENHANCED_PORT_0	BIT(6)
	u8 slt_psc_ep0;
	u8 routing_mode_supported;
	u8 routing_mode_enabled;
	u32 lft_top;
};

/**
 * struct portinfo - per-port information
 * @fid: RW, port Fabric ID set by routing logic
 * @link_down_count: RO, counter (saturating?, clearable), check at PSC
 * @neighbor_guid: RO, from neighbor, check valid at link up
 * @port_error_action: RW, policy bitmask, which errors cause port to bounce
 * @neighbor_port_number: RO, from neighbor, check valid at link up
 * @port_type: RO, disabled/QSFP/fixed/variable/SiPho/etc.? from INIBIN
 * @port_link_mode_active: RO, enumeration, fabric (STL)/bridge port (FLIT BUS)
 * @neighbor_link_down_reason: RO, link_down_reason from neighbor, check when
 *			       working link goes down
 * @h_o_q_lifetime: RW, head of queue lifetime
 * @vl_cap: RO, max VL supported
 * @operational_vls: RW, how many VLs active, write once, routing sets??
 * @neighbor_mtu: RW, neighbor MTU, routing sets (default should be OK)
 * @ltp_crc_mode_supported: RO, bitfield of modes supported
 * @ltp_crc_mode_enabled: RW, bitfield of driver requested modes
 * @ltp_crc_mode_active: RO, 1-hot bitfield of mode in use, valid after link-up
 * @reserved1: reserved
 * @link_width_supported: RO, bitfield of initial link width supported
 * @link_width_enabled: RW, bitfield of driver-requested initial link width
 * @link_width_active: RO, 1-hot bitfield of link width, valid after link-up
 * @reserved2: reserved
 * @link_speed_supported: RO, bitfield of link speed supported
 * @link_speed_enabled: RW, bitfield of driver-requested link speed
 * @link_speed_active: RO, 1-hot bitfield of link speed in use
 * @reserved3: reserved
 * @link_width_downgrade_rx_active: RO, current active downgraded RX link
 *				    width (format TBD)
 * @link_width_downgrade_tx_active: RO, current active downgraded TX link
 *				    width (format TBD)
 * @link_init_reason: RW?, ?
 * @link_down_reason: RW, why shutting link down, enumeration, set before link
 *		      brought down (can be overwritten by FW)
 * @port_state_port_physical_state: RW, bit 0-3: port logical state (look for
 *				    transition to INIT, set CPORT to Down
 *				    to bounce)
 *				    RW, bit 4-7: port physical state (set to
 *				    POLLING to start training process)
 * @oldr_nn_lqi: RO?, bit 0-3: bitfield/enumeration (TBD) why disabling a link
 *		 RO, bit 4: whether neighbor is armed, used by routing
 *		 RO, bit 5-7: 0-5 link quality, should be 5, 3 is in error
 * @reserved4: reserved
 * @???: RW, policy config for max allowable downgrade
 *
 * Included in &struct mbdb_op_portinfo.
 */
struct portinfo {
	u32 fid;
	u32 link_down_count;
	u64 neighbor_guid;
	u32 port_error_action;
	u8 neighbor_port_number;
	u8 port_type;
	u8 port_link_mode_active;
	u8 neighbor_link_down_reason;
	u8 h_o_q_lifetime;
	u8 vl_cap;
	u8 operational_vls;
	u8 neighbor_mtu;
	u8 ltp_crc_mode_supported;
	u8 ltp_crc_mode_enabled;
	u8 ltp_crc_mode_active;
	u8 reserved1;
	u8 link_width_supported;
	u8 link_width_enabled;
	u8 link_width_active;
	u8 reserved2;
	u8 link_speed_supported;
	u8 link_speed_enabled;
	u8 link_speed_active;
	u8 reserved3;
	u8 link_width_downgrade_rx_active;
	u8 link_width_downgrade_tx_active;
	u8 link_init_reason;
	u8 link_down_reason;
#define PS_PPS_PORT_STATE	GENMASK(3, 0)
#define PS_PPS_PHYSICAL_STATE	GENMASK(7, 4)
	u8 port_state_port_physical_state;
#define OLDR_NN_LQI_OFFLINE_DISABLED_REASON	GENMASK(3, 0)
#define OLDR_NN_LQI_NEIGHBOR_NORMAL		BIT(4)
#define OLDR_NN_LQI_LINK_QUALITY_INDICATOR	GENMASK(7, 5)
	u8 oldr_nn_lqi;
	u16 reserved4;
};

/*
 * These definitions are from ifs-all/IbAccess/Common/Inc
 *
 */

/* from stl_sm_types.h: */
#define STL_PORT_TYPE_UNKNOWN (0)
#define STL_PORT_TYPE_DISCONNECTED (1)
#define STL_PORT_TYPE_FIXED (2)
#define STL_PORT_TYPE_VARIABLE (3)
#define STL_PORT_TYPE_STANDARD (4)
#define STL_PORT_TYPE_SI_PHOTONICS (5)

/* from ib_sm_types.h: */
#define IB_PORT_NOP (0)
#define IB_PORT_DOWN (1)
#define IB_PORT_INIT (2)
#define IB_PORT_ARMED (3)
#define IB_PORT_ACTIVE (4)

/* from ib_sm_types.h: */
#define IB_PORT_PHYS_NOP (0)
#define IB_PORT_PHYS_POLLING (2)
#define IB_PORT_PHYS_DISABLED (3)
#define IB_PORT_PHYS_TRAINING (4)
#define IB_PORT_PHYS_LINKUP (5)
#define IB_PORT_PHYS_LINK_ERROR_RECOVERY (6)
/* from stl_sm_types.h: */
#define STL_PORT_PHYS_OFFLINE (9)
#define STL_PORT_PHYS_TEST (11)

/* from stl_sm_types.h: */
#define STL_PORT_LINK_MODE_NOP (0)
#define STL_PORT_LINK_MODE_FLIT_BUS (2)
#define STL_PORT_LINK_MODE_STL (4)

enum pm_port_state {
	PM_PORT_STATE_DISABLED,
	PM_PORT_STATE_ENABLED,
	PM_PORT_STATE_IN_ERROR,
	PM_PORT_STATE_ISOLATED,
	PM_PORT_STATE_RECHECK,
	PM_PORT_STATE_INIT,
	PM_PORT_STATE_ACTIVE
};

#define LINK_SPEED_12G  1
#define LINK_SPEED_25G  2
#define LINK_SPEED_50G  4
#define LINK_SPEED_100G 8

#define LINK_WIDTH_1X 1
#define LINK_WIDTH_2X 2
#define LINK_WIDTH_3X 4
#define LINK_WIDTH_4X 8

struct fsubdev; /* from this file */

/**
 * enum PORT_CONTROL - control port behavior
 *
 * @PORT_CONTROL_ENABLED: port enabled for PM to set up
 * @PORT_CONTROL_ROUTABLE: port enabled for routing
 * @PORT_CONTROL_BEACONING: beaconing requested (flash LEDs)
 * @PORT_CONTROL_CLEAR_ERROR: take port out of error state
 * @NUM_PORT_CONTROLS: number of controls (always last)
 *
 * Each maps to a bit accessed atomically in &struct fport->controls
 */
enum PORT_CONTROL {
	PORT_CONTROL_ENABLED,
	PORT_CONTROL_ROUTABLE,
	PORT_CONTROL_BEACONING,
	PORT_CONTROL_CLEAR_ERROR,
	NUM_PORT_CONTROLS
};

/**
 * struct fport - Per-port state, used for fabric ports only
 * @sd: link to containing subdevice
 * @portinfo: link to relevant &struct fsubdev.portinfo.per_portinfo
 * @state: driver-abstracted high level view of port state
 * @controls: atomically-accessed control bits to enable/disable features
 * @routed: indicates whether this port was included in the routing logic
 * for the most recent successful sweep
 * @lpn: logical port number in firmware
 * @port_type: type of port (hardwired, QFSP, etc.)
 * @log_state: firmware logical state (DOWN, INIT, ARMED, ACTIVE)
 * @phys_state: firmware physical state (DISABLED, POLLING, ..., LINKUP)
 * @cached_neighbor: temporary reference to neighbor while routing lock held
 *
 * Used throughout the driver (mostly by the port manager and routing engine)
 * to maintain information about a given port.
 */
struct fport {
	struct fsubdev *sd;
	struct portinfo *portinfo;
	enum pm_port_state state;
	DECLARE_BITMAP(controls, NUM_PORT_CONTROLS);
	atomic_t routed;
	u8 lpn;
	u8 port_type;
	u8 log_state;
	u8 phys_state;
	/* can cache port neighbor here as long as "routing" lock is held */
	struct fport *cached_neighbor;
};

/**
 * struct mbdb_op_portinfo - Consolidation of per-port information
 * @port_mask: bitmask of logical ports, 0=CPORT, 1-12=fabric/bridge (TBD)
 * @per_portinfo: one &struct portinfo entry for each bit set in
 *		  &mbdb_op_portinfo.port_mask
 *
 * Used with ops_portinfo_set() and ops_portinfo_get() to access
 * information about sets of ports on a subdevice. A copy is maintained in
 * &struct fsubdev.
 */
struct mbdb_op_portinfo {
	u32 port_mask;
	struct portinfo per_portinfo[0];
};

struct mbdb; /* from mbdb.c */
struct fdev; /* from this file */

struct routing_plane; /* from routing_topology.h */
struct routing_fidgen; /* from routing_topology.h */
struct routing_uft;

/**
 * enum pm_trigger_reasons - Cause for PM thread trigger
 * @INIT_EVENT: Initialization of PM subsystem for this sd
 * @DEISOLATE_EVENT: Request to deisolate all ports (possible fabric change)
 * @PSC_TRAP: Port state change reported
 * @LWD_TRAP: Link width degrade reported
 * @LQI_TRAP: Link quality change reported
 * @QSFP_PRESENCE_TRAP: QSFP presence change reported
 * @QSFP_FAULT_TRAP: QSFP fault reported
 * @RESCAN_EVENT: PM-initiated trigger to rescan ports
 * @NL_PM_CMD_EVENT: Netlink request affecting PM processed
 * @NUM_PM_TRIGGERS: Number of trigger reasons (always last)
 *
 * Used as bit index into &fsubdev.pm_triggers
 */
enum pm_trigger_reasons {
	INIT_EVENT,
	DEISOLATE_EVENT,
	PSC_TRAP,
	LWD_TRAP,
	LQI_TRAP,
	QSFP_PRESENCE_TRAP,
	QSFP_FAULT_TRAP,
	RESCAN_EVENT,
	NL_PM_CMD_EVENT,
	NUM_PM_TRIGGERS
};

/**
 * enum fport_health - Port health indicator
 * @FPORT_HEALTH_BLACK: disabled/not present
 * @FPORT_HEALTH_RED: not functional
 * @FPORT_HEALTH_YELLOW: functional, degraded
 * @FPORT_HEALTH_GREEN: functional, healthy
 */
enum fport_health {
	FPORT_HEALTH_BLACK,
	FPORT_HEALTH_RED,
	FPORT_HEALTH_YELLOW,
	FPORT_HEALTH_GREEN
};

/**
 * enum fport_issue - Cause(s) for link degradation (YELLOW)
 * @FPORT_ISSUE_LQI: too many link errors (link quality < 4)
 * @FPORT_ISSUE_LWD: link width degraded
 * @FPORT_ISSUE_RATE: bit rate degraded
 * @NUM_FPORT_ISSUES: Number of FPORT degradation reasons (always last)
 *
 * Used as bit index into &fport_status.issues
 */
enum fport_issue {
	FPORT_ISSUE_LQI,
	FPORT_ISSUE_LWD,
	FPORT_ISSUE_RATE,
	NUM_FPORT_ISSUES
};

/**
 * enum fport_error - Cause for link failure (RED)
 * @FPORT_ERROR_NONE: No error, port is functioning
 * @FPORT_ERROR_FAILED: Driver operation on port failed
 * @FPORT_ERROR_ISOLATED: Invalid neighbor GUID, isolated by driver
 * @FPORT_ERROR_FLAPPING: Driver detected link flapping
 * @FPORT_ERROR_LINK_DOWN: Firmware reported link down (PSC)
 * @FPORT_ERROR_DID_NOT_TRAIN: Driver timed out link training
 */
enum fport_error {
	FPORT_ERROR_NONE,
	FPORT_ERROR_FAILED,
	FPORT_ERROR_ISOLATED,
	FPORT_ERROR_FLAPPING,
	FPORT_ERROR_LINK_DOWN,
	FPORT_ERROR_DID_NOT_TRAIN,
};

/**
 * struct fport_status - Logical port state for external query
 * @health: stoplight-style health indicator
 * @issues: causes for link degradation
 * @error_reason: cause for link error
 */
struct fport_status {
	enum fport_health health;
	DECLARE_BITMAP(issues, NUM_FPORT_ISSUES);
	enum fport_error error_reason;
};

/**
 * struct fsubdev_routing_info - Tracks per-sd routing state.
 * @topo: the topology context that tracks sweep state
 * @plane_link: entry in plane list
 * @plane: pointer to plane
 * @state: routing-level subdevice state
 * @uft: currently active routing tables
 * @uft_next: next uft being built by active sweep
 * @fidgen: currently active bridge fidgen data
 * @fidgen_next:  next fidgen being built by active sweep
 * @dpa_idx_base: dpa lookup table index base
 * @dpa_idx_range: dpa lookup table range
 * @fid_group: abstract fid "group" which determines assigned fids
 * @fid_mgmt: management fid for cport
 * @fid_base: base fid of the bridge's fid block
 * @plane_index: per-plane index of this subdevice
 */
struct fsubdev_routing_info {
	struct routing_topology *topo;
	struct list_head plane_link;
	struct routing_plane *plane;
	enum fsubdev_routing_info_state {
		TILE_ROUTING_STATE_ERROR = 0,
		TILE_ROUTING_STATE_VALID,
	} state;
	struct routing_uft *uft;
	struct routing_uft *uft_next;
	struct routing_fidgen *fidgen;
	struct routing_fidgen *fidgen_next;
	u16 dpa_idx_base;
	u16 dpa_idx_range;
	u16 fid_group;
	u16 fid_mgmt;
	u16 fid_base;
	u16 plane_index;
};

/**
 * struct fsubdev - Per-subdevice state
 * @fw_work: workitem for firmware programming
 * @pm_work: workitem for port management
 * @fdev: link to containing device
 * @csr_base: base address of this subdevice's memory
 * @irq: assigned interrupt
 * @id: generic identifier
 * @mbdb: link to dedicated mailbox struct
 * @supported_opcodes: bitmask of opcodes supported by current FW
 * @pm_work_lock: protects ok_to_schedule_pm_work
 * @ok_to_schedule_pm_work: indicates it is OK to request port management
 * @pm_triggers: event triggering for port management
 * @routable_link: link in global routable_list
 * @guid: GUID retrieved from firmware
 * @switchinfo: switch information read directly from firmware
 * @extended_port_cnt: count of all ports including CPORT and bridge ports
 * @port_cnt: count of all fabric ports
 * @port: internal port state, includes references into @portinfo
 * @lpn_fport_map: map of logical port number (lpn) to fabric port state
 * @fport_lpns: bitmap of which logical ports are fabric ports
 * @bport_lpns: bitmap of which logical ports are bridge ports
 * @portinfo: set of all port information read directly from firmware
 * @_per_portinfo: to reserve space, always use @portinfo.per_portinfo instead
 * @_portstatus: logical port status, access via @port_status/@next_port_status
 * @port_status: for querying port status via RCU (organized by lpn)
 * @next_port_status: for updating port status via RCU (organized by lpn)
 * @max_lpn: largest logical port index
 * @routing: routing information
 *
 * Used throughout the driver to maintain information about a given subdevice.
 */
struct fsubdev {
	struct work_struct fw_work;
	struct work_struct pm_work;
	struct fdev *fdev;
	void __iomem *csr_base;
	int irq;
	/*
	 * id is unique across all subdevices in the system, not just the
	 * device, suitable for use as a dense lookup key
	 *
	 * treat this as opaque.  use sd_index() helper to retrieve the index
	 * of this sd within its parent device.
	 *
	 */
	u16 id;
	struct mbdb *mbdb;
	u64 supported_opcodes[SUPPORTED_OPCODES_ARRAY_ELEMENTS];
	/* protects ok_to_schedule_pm_work */
	struct mutex pm_work_lock;
	bool ok_to_schedule_pm_work;
	DECLARE_BITMAP(pm_triggers, NUM_PM_TRIGGERS);

	/*
	 * must exclusively hold routable lock to change these (except before
	 * being added to routable list) and must hold routable lock (shared
	 * OK) to read them
	 */
	struct list_head routable_link;
	u64 guid;
	struct mbdb_op_switchinfo switchinfo;
	u8 extended_port_cnt;	/* includes CPORT + bridge ports */
	u8 port_cnt;

	/*
	 * must exclusively hold routable lock to read these and must hold
	 * routable lock (shared OK) to write them
	 */
	struct fport port[PORT_FABRIC_COUNT];
	struct fport *lpn_fport_map[PORT_COUNT];
	DECLARE_BITMAP(fport_lpns, PORT_COUNT);
	DECLARE_BITMAP(bport_lpns, PORT_COUNT);
	struct mbdb_op_portinfo portinfo;
	/* for allocation only, always access through portinfo.per_portinfo */
	struct portinfo _per_portinfo[PORT_COUNT];

	struct fport_status _portstatus[2 * PORT_COUNT];
	struct fport_status __rcu *port_status;
	struct fport_status *next_port_status;

	int max_lpn;

	struct fsubdev_routing_info routing;
};

/* to iterate over all fabric ports on a subdevice by logical port number */
#define for_each_fabric_lpn(bit, sd)	\
	for_each_set_bit(bit, (sd)->fport_lpns, PORT_COUNT)

/* to iterate over all bridge ports on a subdevice by logical port number */
#define for_each_bridge_lpn(bit, sd)	\
	for_each_set_bit(bit, (sd)->bport_lpns, PORT_COUNT)

/**
 * get_fport_handle - Returns the fport for a given logical port number.
 * @sd: subdevice pointer
 * @lpn: logical port number
 *
 * Return: The fport pointer at the lpn if valid, otherwise NULL.
 */
static inline struct fport *get_fport_handle(struct fsubdev *sd, u8 lpn)
{
	return lpn < PORT_COUNT ? sd->lpn_fport_map[lpn] : NULL;
}

/**
 * struct fdev - Device structure for IAF/fabric device component
 * @sd: subdevice structures
 * @fwinit_refcnt: number of subdevices needing firmware initialization
 * @pdev: platform device passed in probe
 * @pd: platform specific data
 * @fabric_id: xarray index based on parent index and product type
 * @fw_name: name of firmware loaded to this device
 * @fw: used for interacting with FW API
 * @psc_as_fw: used to access PSC override via FW API
 * @psc_size: size of PSC data, from MEI or FW API
 * @psc_data: PSC data, from MEI or FW API
 * @psc_work: work struct for MEI completiong
 * @mei_ops_lock: mutex lock for mei_ops/dev/bind_continuation/work
 * @mei_ops: bound MEI operation functions (if not NULL)
 * @mei_dev: device to use with @mei_ops
 * @mei_bind_continuation: set when unbound @mei_ops needed, called by bind
 * @continuation_timer: used to timeout receipt of bind continuation
 * @p2p: the active cached peer connectivity results (rcu protected)
 * @p2p_next: the pending peer connectivity results
 * @dir_node: debugfs directory node for this device
 * @refs: references on this instance
 * @fdev_released: signals fdev has been erased from the xarray
 *
 * Used throughout the driver to maintain information about a given device.
 */
struct fdev {
	struct fsubdev sd[IAF_MAX_SUB_DEVS];
	atomic_t fwinit_refcnt;
	struct platform_device *pdev;
	const struct iaf_pdata *pd;
	u32 fabric_id;
	const char *fw_name;
	const struct firmware *fw;
	const struct firmware *psc_as_fw;
	size_t psc_size;
	const u8 *psc_data;
	struct work_struct psc_work;

	/* protects mei_ops/dev/bind_continuation */
	struct mutex mei_ops_lock;
	const struct mei_iaf_ops *mei_ops;
	struct device *mei_dev;
	void (*mei_bind_continuation)(struct fdev *fdev);
	struct timer_list continuation_timer;
	struct routing_p2p_entry __rcu *p2p;
	struct routing_p2p_entry *p2p_next;
	struct dentry *dir_node;

	struct kref refs;
	struct completion fdev_released;
};

void fdev_put(struct fdev *dev);
int fdev_insert(struct fdev *dev);

/*
 * This is the fdev_process_each callback function signature
 * Returning 0 indicates continue
 * Any other return value indicates terminate
 */
typedef int (*fdev_process_each_cb_t)(struct fdev *dev, void *args);

int fdev_process_each(fdev_process_each_cb_t cb, void *args);

struct fdev *fdev_find(u32 fabric_id);

/*
 * Builds a sd identifier from a device identifier and sd index/offset
 * within the device.
 */
static inline u16 build_sd_id(u8 dev_id, u8 sd_index)
{
	return dev_id << 8 | sd_index;
}

/*
 * Returns the sd index/offset relative to its device.
 */
static inline u8 sd_index(struct fsubdev *sd)
{
	return sd - sd->fdev->sd;
}

/*
 * Returns the fabric port index/offset relative to its subdevice.
 */
static inline u8 fport_index(struct fport *port)
{
	return port - port->sd->port;
}

/*
 * The routing lock protects data structures that must not change during
 * routing, specifically all routing fields of all elements in the routable
 * list.
 *
 * Individual port manager instances can update state FOR THEIR TILE ONLY while
 * holding a shared lock, since the routing engine will always use an exclusive
 * lock.
 */
extern struct rw_semaphore routable_lock;
extern struct list_head routable_list;

/*
 * The following map r/w semaphore operations to exclusive/shared lock
 * operations. Thus, the routing engine and any agent that is updating the
 * overall structure of routing-affecting data structures must protect them
 * with:
 *	lock_exclusive(&routable_lock);
 * and
 *	unlock_exclusive(&routable_lock);
 *
 * Readers of these data structures and port manager agents that are writing
 * port state data solely for consumption by the routing engine can instead
 * use:
 *	lock_shared(&routable_lock);
 * and
 *	unlock_shared(&routable_lock);
 *
 * Trylock versions are available as well as an operation that can downgrade
 * the lock from exclusive to shared. The latter prevents the structure from
 * being changed (e.g., addition or removal of subdevices or devices) but
 * allows other agents to get the shared lock.
 */

#define lock_exclusive			down_write
#define lock_exclusive_trylock		down_write_trylock
#define lock_shared			down_read
#define lock_shared_trylock		down_read_trylock
#define lock_downgrade_to_shared	downgrade_write
#define unlock_exclusive		up_write
#define unlock_shared			up_read

static inline struct device *sd_dev(const struct fsubdev *sd)
{
	return &sd->fdev->pdev->dev;
}

static inline struct device *fdev_dev(const struct fdev *dev)
{
	return &dev->pdev->dev;
}

static inline struct device *fport_dev(const struct fport *port)
{
	return &port->sd->fdev->pdev->dev;
}

struct sd_id {
	u32 fabric_id;
	u8 sd_index;
	u8 port;
};

/* The following two functions increase device reference count: */
struct fdev *fdev_find_by_sd_guid(u64 guid);
struct fsubdev *find_sd_id(u32 fabric_id, u8 sd_index);

/* routable_lock must be held across this (shared OK) */
struct fsubdev *find_routable_sd(u64 guid);

#endif
