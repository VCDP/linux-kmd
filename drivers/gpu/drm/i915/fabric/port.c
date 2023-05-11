// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include "port.h"
#include "ops.h"
#include "routing_engine.h"
#include "routing_event.h"
#include "trace.h"

/**
 * struct fsm_io - Port FSM I/O
 * @deisolating: input - are we deisolating ports?
 * @routing_changed: output - should PM notify routing event manager?
 * @rescan_needed: output - should PM immediately reschedule itself?
 * @request_deisolation: output - should PM request all ports be deisolated?
 *
 * I/O from FSM updates, used to exchange requests with update_ports_work().
 * Before each FSM update, deisolating is set to indicate if a DEISOLATE_EVENT
 * was requested and the three outputs are set to false. The former is used to
 * indicate that ISOLATED ports should be moved to RECHECK and the latter are
 * used to request followup actions.
 */
struct fsm_io {
	bool deisolating;
	bool routing_changed;
	bool rescan_needed;
	bool request_deisolation;
};

/*
 * number of isolated ports in the entire system
 */
static atomic_t isolated_port_cnt = ATOMIC_INIT(0);

static void deisolate_all_ports(void)
				__must_hold(&routable_lock)
{
	struct fsubdev *sd;

	if (atomic_read(&isolated_port_cnt) > 0)
		list_for_each_entry(sd, &routable_list, routable_link) {
			set_bit(DEISOLATE_EVENT, sd->pm_triggers);

			mutex_lock(&sd->pm_work_lock);
			if (sd->ok_to_schedule_pm_work)
				queue_work(system_unbound_wq, &sd->pm_work);
			mutex_unlock(&sd->pm_work_lock);
		}
}

static bool port_manual_bringup;
module_param(port_manual_bringup, bool, 0600);
MODULE_PARM_DESC(port_manual_bringup,
		 "Disable automatic port bringup (default: N)");

/*
 * These will be useful at least during early hardware validation.
 */

static bool port_isolate_loop_pairs;

static int port_isolate_loop_pairs_set(const char *val,
				       const struct kernel_param *kp)
{
	bool was_isolating;
	int err;

	was_isolating = port_isolate_loop_pairs;

	err = param_set_bool(val, kp);
	if (err)
		return err;

	if (was_isolating && !port_isolate_loop_pairs) {
		lock_shared(&routable_lock);
		deisolate_all_ports();
		unlock_shared(&routable_lock);
	}

	return 0;
}

static struct kernel_param_ops port_isolate_loop_pairs_ops = {
	.set = port_isolate_loop_pairs_set,
	.get = param_get_bool,
};

module_param_cb(port_isolate_loop_pairs, &port_isolate_loop_pairs_ops,
		&port_isolate_loop_pairs, 0600);
MODULE_PARM_DESC(port_isolate_loop_pairs,
		 "Isolate port pairs looped to same subsystem (default: N)");

static bool port_allow_direct_loopback;

static int port_allow_direct_loopback_set(const char *val,
					  const struct kernel_param *kp)
{
	bool was_isolating;
	int err;

	was_isolating = !port_allow_direct_loopback;

	err = param_set_bool(val, kp);
	if (err)
		return err;

	if (was_isolating && port_allow_direct_loopback) {
		lock_shared(&routable_lock);
		deisolate_all_ports();
		unlock_shared(&routable_lock);
	}

	return 0;
}

static struct kernel_param_ops port_allow_direct_loopback_ops = {
	.set = port_allow_direct_loopback_set,
	.get = param_get_bool,
};

module_param_cb(port_allow_direct_loopback, &port_allow_direct_loopback_ops,
		&port_allow_direct_loopback, 0600);
MODULE_PARM_DESC(port_allow_direct_loopback,
		 "Do not isolate ports looped to themselves (default: N)");

/*
 * Helpers to update port health
 */

static void set_health_black(struct fport_status *status)
{
	status->health = FPORT_HEALTH_BLACK;
	bitmap_zero(status->issues, NUM_FPORT_ISSUES);
	status->error_reason = FPORT_ERROR_NONE;
}

static void set_health_red(struct fport_status *status,
			   enum fport_error error_reason)
{
	status->health = FPORT_HEALTH_RED;
	bitmap_zero(status->issues, NUM_FPORT_ISSUES);
	status->error_reason = error_reason;
}

static void set_health_yellow(struct fport_status *status,
			      enum fport_issue issue)
{
	status->health = FPORT_HEALTH_YELLOW;
	set_bit(issue, status->issues);
}

static void set_health_green(struct fport_status *status)
{
	status->health = FPORT_HEALTH_GREEN;
	bitmap_zero(status->issues, NUM_FPORT_ISSUES);
	status->error_reason = FPORT_ERROR_NONE;
}

/*
 * FSM Actions
 */

/* PM state and port health are related but there is not a direct mapping.
 *
 * Port health is as follows:
 *
 * BLACK = port disabled by PSC bin or SMI or in initial training
 * RED = port in error or went down (PSC trap)
 * YELLOW = port ACTIVE but at reduced width/speed/quality (LWD/LQI trap)
 * GREEN = port ACTIVE at full width/speed/quality
 *
 * Thus when a port first comes up, it remains in BLACK until it becomes active
 * at which point it becomes GREEN (or YELLOW if RX/TX width < 4 or LQI < 4:
 * LWD/LQI changes cause transition between GREEN/YELLOW based on this also).
 *
 * Ports transition to RED when ISOLATED or if the port goes down and can be
 * transitioned to/from BLACK by SMI port disable/enable requests. Ports may
 * also be marked RED if they are flapping, in which case they would be marked
 * GREEN after remaining ACTIVE for some TBD time period.
 */

static void port_in_error(struct fsubdev *sd, struct fport *p,
			  struct fsm_io *fio) __must_hold(&routable_lock)
{
	u32 op_err;
	u32 state;
	int err;

	if (p->state == PM_PORT_STATE_ISOLATED)
		atomic_dec(&isolated_port_cnt);

	p->state = PM_PORT_STATE_IN_ERROR;
	set_health_red(&sd->next_port_status[p->lpn], FPORT_ERROR_FAILED);

	fio->routing_changed = true;

	state = IB_PORT_PHYS_DISABLED;

	err = ops_linkmgr_port_maj_phystate_set(sd, p->lpn, state, &op_err,
						NULL, NULL);
	if (err)
		fport_err(p, "failed mailbox error\n");
}

static void port_disable(struct fsubdev *sd, struct fport *p,
			 struct fsm_io *fio) __must_hold(&routable_lock)
{
	u32 op_err;
	u32 state;
	int err;

	if (p->state == PM_PORT_STATE_ISOLATED)
		atomic_dec(&isolated_port_cnt);

	state = IB_PORT_PHYS_DISABLED;

	err = ops_linkmgr_port_maj_phystate_set(sd, p->lpn, state, &op_err,
						NULL, NULL);
	if (err || op_err)
		goto failed;

	p->state = PM_PORT_STATE_DISABLED;
	set_health_black(&sd->next_port_status[p->lpn]);

	fio->routing_changed = true;
	fport_info(p, "port disabled\n");
	return;

failed:
	fport_err(p, "disable request failed\n");
	port_in_error(sd, p, fio);
}

static void port_enable_polling(struct fsubdev *sd, struct fport *p,
				struct fsm_io *fio) __must_hold(&routable_lock)
{
	u32 op_err;
	u32 state;
	int err;

	state = IB_PORT_PHYS_POLLING;

	err = ops_linkmgr_port_maj_phystate_set(sd, p->lpn, state, &op_err,
						NULL, NULL);
	if (err || op_err)
		goto failed;

	p->state = PM_PORT_STATE_ENABLED;
	fio->request_deisolation = true;
	fport_info(p, "polling enabled\n");
	return;

failed:
	fport_err(p, "polling request failed\n");
	port_in_error(sd, p, fio);
}

static void port_isolate(struct fsubdev *sd, struct fport *p,
			 struct fsm_io *fio) __must_hold(&routable_lock)
{
	u32 op_err;
	u32 state;
	int err;

	state = IB_PORT_PHYS_DISABLED;

	err = ops_linkmgr_port_maj_phystate_set(sd, p->lpn, state, &op_err,
						NULL, NULL);
	if (err || op_err)
		goto failed;

	p->state = PM_PORT_STATE_ISOLATED;
	set_health_red(&sd->next_port_status[p->lpn], FPORT_ERROR_ISOLATED);

	atomic_inc(&isolated_port_cnt);
	fport_info(p, "isolated\n");

	return;

failed:
	fport_err(p, "isolate request failed\n");
	port_in_error(sd, p, fio);
}

static void port_deisolate(struct fsubdev *sd, struct fport *p,
			   struct fsm_io *fio) __must_hold(&routable_lock)
{
	u32 op_err;
	u32 state;
	int err;

	atomic_dec(&isolated_port_cnt);

	state = IB_PORT_PHYS_POLLING;

	err = ops_linkmgr_port_maj_phystate_set(sd, p->lpn, state, &op_err,
						NULL, NULL);
	if (err || op_err)
		goto failed;

	p->state = PM_PORT_STATE_RECHECK;
	set_health_black(&sd->next_port_status[p->lpn]);

	fport_info(p, "polling enabled\n");

	return;

failed:
	fport_err(p, "deisolate request failed\n");
	port_in_error(sd, p, fio);
}

static void set_active_health(struct fsubdev *sd,
			      struct fport *p) __must_hold(&routable_lock)
{
	struct fport_status *status = &sd->next_port_status[p->lpn];
	struct portinfo *pi = p->portinfo;
	u8 oldr_nn_lqi = pi->oldr_nn_lqi;

	set_health_green(status);

	if (FIELD_GET(OLDR_NN_LQI_LINK_QUALITY_INDICATOR, oldr_nn_lqi) < 4)
		set_health_yellow(status, FPORT_ISSUE_LQI);

	if (fls(pi->link_width_active) < fls(pi->link_width_enabled) ||
	    pi->link_width_downgrade_rx_active != pi->link_width_active ||
	    pi->link_width_downgrade_tx_active != pi->link_width_active)
		set_health_yellow(status, FPORT_ISSUE_LWD);

	if (fls(pi->link_speed_enabled) < fls(pi->link_speed_active))
		set_health_yellow(status, FPORT_ISSUE_RATE);
}

static void port_activate(struct fsubdev *sd, struct fport *p,
			  struct fsm_io *fio) __must_hold(&routable_lock)
{
	u32 op_err;
	u32 state;
	int err;

	state = IB_PORT_ACTIVE;

	err = ops_linkmgr_port_link_state_set(sd, p->lpn, state, &op_err,
					      NULL, NULL);
	if (err || op_err)
		goto failed;

	p->state = PM_PORT_STATE_ACTIVE;

	set_active_health(sd, p);

	fio->routing_changed = true;

	fport_info(p, "ACTIVE\n");

	return;

failed:
	fport_err(p, "activate request failed\n");
	port_in_error(sd, p, fio);
}

/**
 * valid_neighbor() - Is this connection to a valid neighbor?
 * @sd: subdevice being checked
 * @p: port being checked
 *
 * Indicates whether the connection is to a valid neighbor. To be a valid
 * neighbor, the neighbor GUID must correspond to a known device. If it is a
 * loopback (connected to same device), then whether it is valid is controlled
 * by module parameters port_allow_direct_loopback and port_isolate_loop_pairs
 * (based on whether the loopback is to the same port).
 *
 * return: true if this is a valid neighbor, false if it should be isolated
 */
static bool valid_neighbor(struct fsubdev *sd,
			   struct fport *p) __must_hold(&routable_lock)
{
	struct fdev *neighbor_dev;

	neighbor_dev = fdev_find_by_sd_guid(p->portinfo->neighbor_guid);

	if (!neighbor_dev)
		return false;

	/*
	 * It's safe to decrement neighbor_dev's reference count immediately
	 * since we're only testing whether it's our own device pointer
	 */
	fdev_put(neighbor_dev);

	if (neighbor_dev != sd->fdev)
		return true;

	/* Check whether applicable loopback case is supported */
	if (p->portinfo->neighbor_guid == sd->guid &&
	    p->portinfo->neighbor_port_number == p->lpn)
		return port_allow_direct_loopback;
	else
		return !port_isolate_loop_pairs;
}

static void port_arm(struct fsubdev *sd, struct fport *p,
		     struct fsm_io *fio) __must_hold(&routable_lock)
{
	u8 oldr_nn_lqi;
	u32 op_err;
	u32 state;
	int err;

	state = IB_PORT_ARMED;

	err = ops_linkmgr_port_link_state_set(sd, p->lpn, state, &op_err, NULL,
					      NULL);
	if (err || op_err)
		goto failed;

	p->state = PM_PORT_STATE_INIT;

	oldr_nn_lqi = p->portinfo->oldr_nn_lqi;

	if (FIELD_GET(OLDR_NN_LQI_NEIGHBOR_NORMAL, oldr_nn_lqi))
		port_activate(sd, p, fio);

	return;

failed:
	fport_err(p, "arm request failed\n");
	port_in_error(sd, p, fio);
}

static void port_arm_or_isolate(struct fsubdev *sd, struct fport *p,
				struct fsm_io *fio) __must_hold(&routable_lock)
{
	if (valid_neighbor(sd, p))
		port_arm(sd, p, fio);
	else
		port_isolate(sd, p, fio);
}

static void port_retry(struct fsubdev *sd, struct fport *p,
		       struct fsm_io *fio) __must_hold(&routable_lock)
{
	fport_info(p, "in error, reinitializing\n");

	set_health_red(&sd->next_port_status[p->lpn], FPORT_ERROR_LINK_DOWN);

	p->state = PM_PORT_STATE_ENABLED;
}

static void port_rearm(struct fsubdev *sd, struct fport *p,
		       struct fsm_io *fio) __must_hold(&routable_lock)
{
	fport_info(p, "in error, rearming\n");

	set_health_red(&sd->next_port_status[p->lpn], FPORT_ERROR_LINK_DOWN);

	port_arm(sd, p, fio);
}

static void port_retry_active(struct fsubdev *sd, struct fport *p,
			      struct fsm_io *fio) __must_hold(&routable_lock)
{
	port_retry(sd, p, fio);
	fio->routing_changed = true;
}

static void port_rearm_active(struct fsubdev *sd, struct fport *p,
			      struct fsm_io *fio) __must_hold(&routable_lock)
{
	port_rearm(sd, p, fio);
	fio->routing_changed = true;
}

/*
 * These are some various notes used for the initial implementation
 *
 */

/*
 * Valid states:
 *
 * DISABLED/DOWN
 * POLLING (training states)/DOWN
 * OFFLINE/DOWN
 * LINKUP/not DOWN
 * LINKUP/DOWN (intermediate bounce step) (or requested by other agent?)
 */

/*
 * Initial error cases:
 *
 * Inconsistent reported logical/physical state (e.g, DISABLED/not DOWN):
 *	disable port to errored
 * Port is DISABLED by type or override and any state other than DISABLED:
 *	disable port to disabled
 * Port is ENABLED and initial physical state POLLING:
 *	mark state enabled
 * Initial physical state TRAINING:
 *	mark state enabled(?)
 * Initial physical state LINKUP:
 *	bounce link by setting to POLLING, mark enabled
 * Initial physical state LINK_ERROR_RECOVERY:
 *	TBD
 * Initial physical state OFFLINE:
 *	mark state disabled (not present)
 * Initial physical state TEST:
 *	TBD
 * Initial physical state other than DISABLED:
 *	disable port to disabled, kick FSM
 *
 * For now only need to deal with these:
 *
 * Port logically not DOWN and want disabled:
 *	bounce link by setting to DISABLED, mark disabled
 * Port logically not DOWN and want enabled:
 *	bounce link by setting to POLLING, mark enabled
 */

/*
 * Armed->Active error cases:
 *
 * NN polling failed (never completes):
 *	disable port to errored
 * link bounce to LINKUP/init:
 *	go to Init state, retry ARM
 * too many ARM requests:
 *	not defined, probably disabled to errored
 * link bounce to DISABLED/down:
 *	(admin intervention, maybe go to DISABLED, log event)
 * link bounce to ANY_OTHER/down:
 *	go to Enabled, wait for PSC
 */

/*
 * State Diagram from IAF Routing Specification
 *
 * Start -> Disabled
 * Any -> ErrorCondition -> Errored (only SMI can fix)
 * Any -> OnPresenceLow || OnForceUnused -> Disabled
 * Disabled -> OnDriverInit || OnPresenceHigh IF (Used && Presence) -> Enabled
 * Enabled -> OnPortStateChange -> Init
 * Init -> InvalidNeighborGuid -> Isolated
 * Isolated -> OnAnyDeviceAdd || OnAnyPresenceHigh -> Check
 * Check -> OnTimeout -> Isolated
 * Check -> OnPortStateChange -> Init
 * Init -> OnRoutingEngineDone -> Active
 */

/*
 * FSM States
 */

static void fsm_DISABLED(struct fsubdev *sd, struct fport *p,
			 struct fsm_io *fio) __must_hold(&routable_lock)
{
	if (test_bit(PORT_CONTROL_ENABLED, p->controls))
		port_enable_polling(sd, p, fio);
}

static void fsm_ENABLED(struct fsubdev *sd, struct fport *p,
			struct fsm_io *fio) __must_hold(&routable_lock)
{
	if (!test_bit(PORT_CONTROL_ENABLED, p->controls)) {
		port_disable(sd, p, fio);
		return;
	}

	switch (p->log_state) {
	case IB_PORT_DOWN:
		break;

	case IB_PORT_INIT:
		port_arm_or_isolate(sd, p, fio);
		break;

	case IB_PORT_ARMED:
	case IB_PORT_ACTIVE:
	default:
		port_in_error(sd, p, fio);
		break;
	}
}

static void fsm_IN_ERROR(struct fsubdev *sd, struct fport *p,
			 struct fsm_io *fio) __must_hold(&routable_lock)
{
	/* normally a terminal state */
	if (test_and_clear_bit(PORT_CONTROL_CLEAR_ERROR, p->controls)) {
		p->state = PM_PORT_STATE_DISABLED;
		fio->rescan_needed = true;
	}
}

static void fsm_ISOLATED(struct fsubdev *sd, struct fport *p,
			 struct fsm_io *fio) __must_hold(&routable_lock)
{
	if (!test_bit(PORT_CONTROL_ENABLED, p->controls)) {
		port_disable(sd, p, fio);
		return;
	}

	if (fio->deisolating)
		port_deisolate(sd, p, fio);
}

static void fsm_RECHECK(struct fsubdev *sd, struct fport *p,
			struct fsm_io *fio) __must_hold(&routable_lock)
{
	if (!test_bit(PORT_CONTROL_ENABLED, p->controls)) {
		port_disable(sd, p, fio);
		return;
	}

	switch (p->log_state) {
	case IB_PORT_DOWN:
		/*
		 * if (timer_expired)
		 *	port_isolate(sd, p, fio);
		 */
		break;

	case IB_PORT_INIT:
		port_arm_or_isolate(sd, p, fio);
		break;

	case IB_PORT_ARMED:
	case IB_PORT_ACTIVE:
	default:
		port_in_error(sd, p, fio);
		break;
	}
}

static void fsm_INIT(struct fsubdev *sd, struct fport *p,
		     struct fsm_io *fio) __must_hold(&routable_lock)
{
	u8 oldr_nn_lqi = p->portinfo->oldr_nn_lqi;

	if (!test_bit(PORT_CONTROL_ENABLED, p->controls)) {
		port_disable(sd, p, fio);
		return;
	}

	switch (p->log_state) {
	case IB_PORT_DOWN:
		port_retry(sd, p, fio);
		break;

	case IB_PORT_INIT:
		port_rearm(sd, p, fio);
		break;

	case IB_PORT_ARMED:
	case IB_PORT_ACTIVE:
	default:
		if (FIELD_GET(OLDR_NN_LQI_NEIGHBOR_NORMAL, oldr_nn_lqi))
			port_activate(sd, p, fio);
		break;
	}
}

static void fsm_ACTIVE(struct fsubdev *sd, struct fport *p,
		       struct fsm_io *fio) __must_hold(&routable_lock)
{
	if (!test_bit(PORT_CONTROL_ENABLED, p->controls)) {
		port_disable(sd, p, fio);
		return;
	}

	switch (p->log_state) {
	case IB_PORT_DOWN:
		port_retry_active(sd, p, fio);
		break;

	case IB_PORT_INIT:
		port_rearm_active(sd, p, fio);
		break;

	case IB_PORT_ARMED:
	case IB_PORT_ACTIVE:
	default:
		set_active_health(sd, p);
		break;
	}
}

/*
 * FSM driver function
 */

static void kick_fsms(struct fsubdev *sd,
		      struct fsm_io *fio) __must_hold(&routable_lock)
{
	struct fport *p;
	int i;

	for (p = sd->port, i = 0; i < sd->port_cnt; ++i, ++p) {
		fport_dbg(p, "state: %u\n", p->state);
		switch (p->state) {
		case PM_PORT_STATE_DISABLED:
			fsm_DISABLED(sd, p, fio);
			break;

		case PM_PORT_STATE_ENABLED:
			fsm_ENABLED(sd, p, fio);
			break;

		case PM_PORT_STATE_IN_ERROR:
			fsm_IN_ERROR(sd, p, fio);
			break;

		case PM_PORT_STATE_ISOLATED:
			fsm_ISOLATED(sd, p, fio);
			break;

		case PM_PORT_STATE_RECHECK:
			fsm_RECHECK(sd, p, fio);
			break;

		case PM_PORT_STATE_INIT:
			fsm_INIT(sd, p, fio);
			break;

		case PM_PORT_STATE_ACTIVE:
			fsm_ACTIVE(sd, p, fio);
			break;

		default:
			port_in_error(sd, p, fio);
			break;
		}
		fport_dbg(p, "new state: %u\n", p->state);
	}
}

/*
 * Module service functions
 */

static int read_portinfo(struct fsubdev *sd)
{
	u32 port_mask;

	/* Fetch all ports including CPORT */
	port_mask = ~(~0UL << sd->extended_port_cnt);

	return ops_portinfo_get(sd, port_mask, &sd->portinfo, NULL, NULL);
}

static void publish_status_updates(struct fsubdev *sd)
{
	struct fport_status *old_unpublished = sd->next_port_status;
	struct fport_status *new_unpublished;
	int i;

	/* Swap published state pointer */
	rcu_swap_protected(sd->port_status, sd->next_port_status, true);
	synchronize_rcu();

	/* copy new unpublished data */
	new_unpublished = sd->next_port_status;

	for (i = PORT_FABRIC_START; i <= sd->max_lpn; ++i)
		new_unpublished[i] = old_unpublished[i];
}

/* Main PM thread runs on WQ, max of one running and one queued at a time */
static void update_ports_work(struct work_struct *work)
{
	struct fsubdev *sd = container_of(work, struct fsubdev, pm_work);
	bool initializing = false;
	bool port_change = false;
	bool qsfp_change = false;
	bool rescanning = false;
	bool force_routing = false;
	struct fsm_io fio;
	struct fport *p;
	int err;
	int i;

	/* FSM-affecting state */
	fio.routing_changed = false;
	fio.rescan_needed = false;
	fio.request_deisolation = false;
	fio.deisolating = false;

	/* lock may take a while to acquire if routing is running */
	lock_shared(&routable_lock);

	/*
	 * Note that it is possible that update_ports could be queued again
	 * while it was waiting to get the lock. If so it will rerun and
	 * discover that nothing has changed.
	 *
	 * In that case, we could skip the kick_fsms() call since they
	 * shouldn't have any effect.
	 *
	 * Also, the odds of that happening could have been greatly reduced by
	 * calling cancel_work() here, though that function was removed from
	 * kernel 4.16 when (according to the commit msg) it was discovered by
	 * accident that it wasn't being used. It might be worth investigating
	 * alternatives in the future, since the underlying support functions
	 * appear to still support it.
	 */

	if (test_and_clear_bit(INIT_EVENT, sd->pm_triggers)) {
		initializing = true;
		port_change = true; /* might have missed PSC trap */
		qsfp_change = true;
	}

	if (test_and_clear_bit(DEISOLATE_EVENT, sd->pm_triggers)) {
		for (p = sd->port, i = 0; i < sd->port_cnt; ++i, ++p)
			if (p->state == PM_PORT_STATE_ISOLATED)
				fio.deisolating = true;

		if (fio.deisolating) {
			rescanning = true;
			port_change = true;
			qsfp_change = true;
		}
	}

	if (test_and_clear_bit(PSC_TRAP, sd->pm_triggers)) {
		port_change = true;
		err = ops_linkmgr_psc_trap_ack(sd, NULL, NULL, false);
		if (err)
			sd_err(sd, "failed to ACK PSC trap\n");
	}

	if (test_and_clear_bit(LWD_TRAP, sd->pm_triggers)) {
		port_change = true;
		err = ops_linkmgr_port_lwd_trap_ack(sd, NULL, NULL, false);
		if (err)
			sd_err(sd, "failed to ACK PSC trap\n");
	}

	if (test_and_clear_bit(LQI_TRAP, sd->pm_triggers)) {
		port_change = true;
		err = ops_linkmgr_port_lqi_trap_ack(sd, NULL, NULL, false);
		if (err)
			sd_err(sd, "failed to ACK PSC trap\n");
	}

	if (test_and_clear_bit(QSFP_PRESENCE_TRAP, sd->pm_triggers)) {
		qsfp_change = true;
		/*
		 * Not implemented yet:
		 *
		 * err = ops_qsfp_mgr_fault_trap_acknowledge(sd, 0, 0, false);
		 * if (err)
		 *	sd_err(sd, "failed to ACK PSC trap\n");
		 */
	}

	if (test_and_clear_bit(QSFP_FAULT_TRAP, sd->pm_triggers)) {
		qsfp_change = true;
		err = ops_qsfpmgr_fault_trap_ack(sd, NULL, NULL, false);
		if (err)
			sd_err(sd, "failed to ACK PSC trap\n");
	}

	if (test_and_clear_bit(RESCAN_EVENT, sd->pm_triggers)) {
		rescanning = true;
		port_change = true;
		qsfp_change = true;
	}

	if (test_and_clear_bit(NL_PM_CMD_EVENT, sd->pm_triggers))
		rescanning = true;

	sd_dbg(sd, "Updating ports:%s%s%s%s\n",
	       initializing ? " INIT" : "",
	       port_change ? " PSC" : "",
	       qsfp_change ? " QSFP" : "",
	       rescanning ? " RESCAN" : "");

	/*
	 * Read state as needed
	 */
	if (port_change) {
		err = read_portinfo(sd);
		if (err)
			sd_err(sd, "failed to update portinfo\n");

		for (p = sd->port, i = 0; i < sd->port_cnt; ++i, ++p) {
			u8 pspps = p->portinfo->port_state_port_physical_state;

			p->log_state = FIELD_GET(PS_PPS_PORT_STATE, pspps);
			p->phys_state = FIELD_GET(PS_PPS_PHYSICAL_STATE, pspps);

			fport_dbg(p,
				  "info: type %u LMA %u PPS %x OLDRNNLQI %x neighbor %016llx/%u/%u/%u LIR %u LDR %u FID %08x LDC %u PEA %u\n",
				  p->portinfo->port_type,
				  p->portinfo->port_link_mode_active,
				  p->portinfo->port_state_port_physical_state,
				  p->portinfo->oldr_nn_lqi,
				  p->portinfo->neighbor_guid,
				  p->portinfo->neighbor_port_number,
				  p->portinfo->neighbor_link_down_reason,
				  p->portinfo->neighbor_mtu,
				  p->portinfo->link_init_reason,
				  p->portinfo->link_down_reason,
				  p->portinfo->fid,
				  p->portinfo->link_down_count,
				  p->portinfo->port_error_action);

			trace_pm_psc(sd->id, i, p->portinfo);
		}
	}

	/*
	 * Not implemented yet:
	 *
	 * if (qsfp_change)
	 *	read_qsfp_data;
	 */

	/* Kick the FSMs if any request was seen */
	if (initializing || port_change || qsfp_change || rescanning)
		kick_fsms(sd, &fio);

	if (fio.request_deisolation)
		deisolate_all_ports();

	unlock_shared(&routable_lock);

	publish_status_updates(sd);

	if (fio.routing_changed || force_routing) {
		sd_info(sd, "Routing changed\n");
		rem_request();
	}

	if (fio.rescan_needed) {
		set_bit(RESCAN_EVENT, sd->pm_triggers);
		mutex_lock(&sd->pm_work_lock);
		if (sd->ok_to_schedule_pm_work)
			queue_work(system_unbound_wq, &sd->pm_work);
		mutex_unlock(&sd->pm_work_lock);
	}
}

static int initial_switchinfo_state(struct fsubdev *sd)
{
	/* num_ports does not include CPORT, which is always present */
	sd->extended_port_cnt = sd->switchinfo.num_ports + 1;

	sd_info(sd, "Detected %d ports\n", sd->extended_port_cnt);

	if (sd->guid != sd->switchinfo.guid)
		sd_err(sd, "GUID mismatch: %016llx != %016llx\n", sd->guid,
		       sd->switchinfo.guid);

	if (sd->extended_port_cnt > PORT_COUNT) {
		sd_err(sd, "Too many overall ports detected\n");
		return -ENOENT;
	}

	return 0;
}

/*
 * Module Entry points
 */

/*
 * Go through extended port structures, identifying all fabric ports. Set up
 * ports array (e.g., refer to relevant portinfo pointers) and set port_cnt to
 * number of fabric ports.
 */
static int initial_port_state(struct fsubdev *sd)
{
	struct fport_status *init_status;
	struct portinfo *curr_portinfo;
	struct fport *p;
	int port_cnt;
	int i;

	p = sd->port;
	port_cnt = 0;

	init_status = sd->_portstatus;
	sd->max_lpn = 0;

	/*
	 * This code is relying on the fact that the subdev structure is
	 * initially zeroed (otherwise zero sd->lpn_fport_map[] here)
	 */

	bitmap_zero(sd->fport_lpns, PORT_COUNT);
	bitmap_zero(sd->bport_lpns, PORT_COUNT);

	for (i = PORT_FABRIC_START,
	     curr_portinfo = &sd->portinfo.per_portinfo[PORT_FABRIC_START];
	     i < sd->extended_port_cnt;
	     ++i, ++curr_portinfo) {
		u8 mode = curr_portinfo->port_link_mode_active;
		u8 type = curr_portinfo->port_type;

		sd_info(sd, PORT_FMT "mode %u type %u\n", i, mode, type);

		if (mode == STL_PORT_LINK_MODE_STL) {
			if (port_cnt >= PORT_FABRIC_COUNT) {
				sd_err(sd, "Too many fabric ports detected\n");
				return -ENOENT;
			}

			p->lpn = i;
			p->portinfo = curr_portinfo;
			p->sd = sd;

			p->port_type = type;

			bitmap_zero(p->controls, NUM_PORT_CONTROLS);
			/* bool check should not require kernel_param_lock() */
			if (type == STL_PORT_TYPE_FIXED && !port_manual_bringup)
				set_bit(PORT_CONTROL_ENABLED, p->controls);

			set_bit(PORT_CONTROL_ROUTABLE, p->controls);

			p->state = PM_PORT_STATE_DISABLED;

			sd->max_lpn = i;
			sd->port_cnt = ++port_cnt;

			set_health_black(&init_status[i]);
			set_bit(i, sd->fport_lpns);
			sd->lpn_fport_map[i] = p;

			++p;
		} else {
			set_bit(i, sd->bport_lpns);
		}
	}

	/* published and unpublished queryable port state */

	rcu_assign_pointer(sd->port_status, init_status);

	sd->next_port_status = &sd->_portstatus[PORT_COUNT];
	for (i = PORT_FABRIC_START; i <= sd->max_lpn; ++i)
		sd->next_port_status[i] = init_status[i];

	return 0;
}

void initialize_fports(struct fsubdev *sd)
{
	int err;

	sd->pm_triggers[0] = 0;

	mutex_lock(&sd->pm_work_lock);
	INIT_WORK(&sd->pm_work, update_ports_work);
	sd->ok_to_schedule_pm_work = true;
	mutex_unlock(&sd->pm_work_lock);

	err = ops_switchinfo_get(sd, &sd->switchinfo, NULL, NULL);
	if (err)
		goto init_failed;

	err = initial_switchinfo_state(sd);
	if (err)
		goto init_failed;

	err = read_portinfo(sd);
	if (err)
		goto init_failed;

	err = initial_port_state(sd);
	if (err)
		goto init_failed;

	err = ops_linkmgr_psc_trap_ena_set(sd, 1, NULL, NULL, false);
	if (err)
		goto init_failed;
#undef ENABLE_LWD_LQI
#ifdef ENABLE_LWD_LQI
	/*
	 * Enable when possible. Currently this causes the module_load_unload
	 * test to fail, possibly due to an L8sim issue on cleanup:
	 */
	err = ops_linkmgr_port_lwd_trap_ena_set(sd, 1, NULL, NULL, false);
	if (err)
		goto init_failed;
	err = ops_linkmgr_port_lqi_trap_ena_set(sd, 1, NULL, NULL, false);
	if (err)
		goto init_failed;
#endif

	lock_exclusive(&routable_lock);
	list_add(&sd->routable_link, &routable_list);
	unlock_exclusive(&routable_lock);

	set_bit(INIT_EVENT, sd->pm_triggers);
	mutex_lock(&sd->pm_work_lock);
	if (sd->ok_to_schedule_pm_work)
		queue_work(system_unbound_wq, &sd->pm_work);
	mutex_unlock(&sd->pm_work_lock);

	return;

init_failed:

	sd_err(sd, "Could not initialize ports\n");
}

void destroy_fports(struct fsubdev *sd)
{
	struct fport *p;
	int i;

	mutex_lock(&sd->pm_work_lock);
	if (sd->ok_to_schedule_pm_work) {
		sd->ok_to_schedule_pm_work = false;
		cancel_work_sync(&sd->pm_work);
	}
	mutex_unlock(&sd->pm_work_lock);

	/* ignore MBDB errors here */
#ifdef ENABLE_LWD_LQI
	ops_linkmgr_port_lqi_trap_ena_set(sd, 0, NULL, NULL, false);
	ops_linkmgr_port_lwd_trap_ena_set(sd, 0, NULL, NULL, false);
#endif
	ops_linkmgr_psc_trap_ena_set(sd, 0, NULL, NULL, true);

	lock_exclusive(&routable_lock);
	list_del_init(&sd->routable_link);
	routing_sd_destroy(sd);
	unlock_exclusive(&routable_lock);

	for (p = sd->port, i = 0; i < sd->port_cnt; ++i, ++p)
		if (p->state == PM_PORT_STATE_ISOLATED)
			atomic_dec(&isolated_port_cnt);

	rem_request();
}

static int signal_pm_thread(struct fsubdev *sd, enum pm_trigger_reasons event)
{
	int err = 0;

	set_bit(event, sd->pm_triggers);

	mutex_lock(&sd->pm_work_lock);
	if (sd->ok_to_schedule_pm_work)
		queue_work(system_unbound_wq, &sd->pm_work);
	else
		err = -EAGAIN;
	mutex_unlock(&sd->pm_work_lock);

	return err;
}

int enable_fports(struct fsubdev *sd, unsigned long lpnmask)
{
	int unchanged = 1;
	struct fport *p;
	u8 lpn;

	for_each_set_bit(lpn, &lpnmask, PORT_COUNT) {
		p = get_fport_handle(sd, lpn);
		if (p)
			unchanged &= test_and_set_bit(PORT_CONTROL_ENABLED,
						      p->controls);
	}

	return unchanged ? 0 : signal_pm_thread(sd, NL_PM_CMD_EVENT);
}

int disable_fports(struct fsubdev *sd, unsigned long lpnmask)
{
	int changed = 0;
	struct fport *p;
	u8 lpn;

	for_each_set_bit(lpn, &lpnmask, PORT_COUNT) {
		p = get_fport_handle(sd, lpn);
		if (p)
			changed |= test_and_clear_bit(PORT_CONTROL_ENABLED,
						      p->controls);
	}

	return changed ? signal_pm_thread(sd, NL_PM_CMD_EVENT) : 0;
}

int enable_usage_fports(struct fsubdev *sd, unsigned long lpnmask)
{
	int unchanged = 1;
	struct fport *p;
	u8 lpn;

	for_each_set_bit(lpn, &lpnmask, PORT_COUNT) {
		p = get_fport_handle(sd, lpn);
		if (p)
			unchanged &= test_and_set_bit(PORT_CONTROL_ROUTABLE,
						      p->controls);
	}

	if (!unchanged)
		rem_request();

	return 0;
}

int disable_usage_fports(struct fsubdev *sd, unsigned long lpnmask)
{
	int changed = 0;
	struct fport *p;
	u8 lpn;

	for_each_set_bit(lpn, &lpnmask, PORT_COUNT) {
		p = get_fport_handle(sd, lpn);
		if (p)
			changed |= test_and_clear_bit(PORT_CONTROL_ROUTABLE,
						      p->controls);
	}

	if (changed)
		rem_request();

	return 0;
}

int get_fport_status(struct fsubdev *sd, u8 lpn, struct fport_status *status)
{
	struct fport_status *curr_status;
	int err = 0;

	if (!get_fport_handle(sd, lpn))
		return -EINVAL;

	rcu_read_lock();
	curr_status = rcu_dereference(sd->port_status);

	if (curr_status)
		*status = curr_status[lpn];
	else
		err = -EINVAL;

	rcu_read_unlock();
	return err;
}

void port_state_change_trap_handler(struct fsubdev *sd)
{
	signal_pm_thread(sd, PSC_TRAP);
}

void port_link_width_degrade_trap_handler(struct fsubdev *sd)
{
	signal_pm_thread(sd, LWD_TRAP);
}

void port_link_quality_indicator_trap_handler(struct fsubdev *sd)
{
	signal_pm_thread(sd, LQI_TRAP);
}

void port_qsfp_presence_trap_handler(struct fsubdev *sd)
{
	signal_pm_thread(sd, QSFP_PRESENCE_TRAP);
}

void port_qsfp_fault_trap_handler(struct fsubdev *sd)
{
	signal_pm_thread(sd, QSFP_FAULT_TRAP);
}
