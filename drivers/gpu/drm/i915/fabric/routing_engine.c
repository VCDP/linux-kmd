// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#include <linux/sched/clock.h>

#include "routing_engine.h"
#include "routing_event.h"
#include "routing_logic.h"
#include "routing_io.h"
#include "routing_p2p.h"
#include "selftests/routing_mock.h"

/**
 * struct routing_context - tracks routing engine state
 * @topo: the topology structure tracking per-sweep state
 * @gen_start: the generation counter incremented at sweep start
 * @gen_end: the generation counter incremented at sweep end
 *
 * The start generation counter is incremented at the beginning of every
 * sweep attempt, regardless of whether any previous sweep passed or failed.
 *
 * The end generation counter is incremented to match start at the end of a
 * successful sweep attempt.
 *
 * For clients, if they make changes and then observe start == N, their changes
 * may have raced with sweep N.  To confirm that their changes have taken
 * effect, they want to wait until end > N, i.e. "at least one new sweep was
 * scheduled and completed successfully after sweep N."
 */
struct routing_context {
	struct routing_topology topo;
	atomic_t gen_start;
	atomic_t gen_end;
};

static struct routing_context routing_context;

static void topology_destroy(struct routing_topology *topo)
{
	struct routing_plane *plane, *plane_tmp;
	struct fsubdev *sd;

	list_for_each_entry(sd, &routable_list, routable_link)
		routing_sd_destroy(sd);

	list_for_each_entry_safe(plane, plane_tmp, &topo->plane_list, topo_link)
		routing_plane_destroy(plane);
}

/**
 * routing_init - Initializes the routing engine.
 */
void routing_init(void)
{
	struct routing_topology *topo = &routing_context.topo;

	pr_debug("init\n");

	INIT_LIST_HEAD(&topo->plane_list);

	routing_p2p_init();
}

static void print_status(int err, u64 logic_nsecs, u64 io_nsecs, u64 p2p_nsecs)
{
	struct fsubdev *sd;
	int i;
	u32 good_subdevs = 0;
	u32 total_subdevs = 0;
	u32 used_ports = 0;
	u32 up_ports = 0;
	u32 total_ports = 0;

	list_for_each_entry(sd, &routable_list, routable_link) {
		pr_debug("routing status: device %u sd %u state %u\n",
			 sd->fdev->pd->index, sd_index(sd),
			 sd->routing.state);

		++total_subdevs;

		if (routing_sd_is_error(sd))
			continue;

		++good_subdevs;

		for (i = 0; i < sd->port_cnt; ++i) {
			struct fport *port = sd->port + i;
			bool up = port_is_routable(port);
			bool used = sd->port[i].cached_neighbor;

			++total_ports;
			if (up)
				++up_ports;
			if (used)
				++used_ports;

			pr_debug("routing status: device %u sd %u port %u state %u up %u used %u\n",
				 sd->fdev->pd->index, sd_index(sd),
				 port->lpn, port->state, up, used);
		}
	}

	pr_info("routing status: gen %d %s: sds %u/%u ports %u/%u/%u planes %u logic %llu io %llu p2p %llu\n",
		atomic_read(&routing_context.gen_end), err ? "fail" : "pass",
		good_subdevs, total_subdevs, used_ports, up_ports, total_ports,
		routing_context.topo.num_planes,
		logic_nsecs / 1000, io_nsecs / 1000, p2p_nsecs / 1000);
}

/**
 * cleanup_next - Cleanup the "next" versions of data structures generated
 * during a sweep (generally when we're on an error path and won't be
 * activating them).
 */
static void cleanup_next(void)
{
	struct fsubdev *sd;

	for (sd = routing_sd_iter(0); sd; sd = routing_sd_next(sd, 0)) {
		routing_uft_destroy(sd->routing.uft_next);
		sd->routing.uft_next = NULL;

		kfree(sd->routing.fidgen_next);
		sd->routing.fidgen_next = NULL;
	}
}

/*
 * update_routed_status - Updates the "routed" field of all ports to reflect
 * whether they are in use.
 *
 * Assumes sweep success.
 *
 * If a device is in the error state, its "routed" status will be forced to
 * false here.  We don't do this at the moment of error transition to prevent
 * the previous status from changing until a sweep has succeeded, ensuring the
 * status correctly reflects that the fabric-wide routing is no longer using
 * it.
 */
static void update_routed_status(void)
{
	struct fsubdev *sd;
	int i;

	/* enumerate ALL devices to handle forcing failed devices to false */
	for (sd = routing_sd_iter(1); sd; sd = routing_sd_next(sd, 1)) {
		for (i = 0; i < sd->port_cnt; ++i) {
			struct fport *port = sd->port + i;
			bool routed = !routing_sd_is_error(sd) &&
				      port->cached_neighbor;

			atomic_set(&port->routed, routed);
		}
	}
}

/**
 * routing_sweep - Perform routing calculation and programming.
 */
void routing_sweep(void)
{
	u64 ref;
	u64 logic_nsecs = 0;
	u64 io_nsecs = 0;
	u64 p2p_nsecs = 0;
	int gen_start;
	int err;

	lock_exclusive(&routable_lock);

	gen_start = atomic_inc_return(&routing_context.gen_start);
	pr_debug("routing start: gen %u\n", gen_start);

	routing_topo_reset_sd_error(&routing_context.topo);

	ref = local_clock();
	err = routing_logic_run(&routing_context.topo);
	logic_nsecs = local_clock() - ref;

	lock_downgrade_to_shared(&routable_lock);

	if (err) {
		pr_err("failed to route fabric: %d\n", err);
		cleanup_next();
		goto finalize;
	}

	ref = local_clock();
	err = routing_io_run(&routing_context.topo);
	io_nsecs = local_clock() - ref;

	if (err) {
		pr_err("failed to program fabric: %d\n", err);
		cleanup_next();
		goto finalize;
	}

	ref = local_clock();
	routing_p2p_cache();
	p2p_nsecs = local_clock() - ref;

finalize:
	/*
	 * If any device transitioned to error, we likely have an inconsistent
	 * fabric programmed, or at least may see a recovery on a resweep
	 * due to the device being excluded.  Explicitly reschedule routing.
	 *
	 * Otherwise, device-agnostic routing errors with no change in device
	 * error states are unlikely to be recoverable; skip explicit resweep
	 * and just wait for normal event driven sweep signals.
	 */
	if (routing_topo_check_sd_error(&routing_context.topo)) {
		pr_warn("device error during routing; scheduling resweep\n");
		rem_request();
	} else if (!err) {
		update_routed_status();
		atomic_set(&routing_context.gen_end,
			   atomic_read(&routing_context.gen_start));
	}

	print_status(err, logic_nsecs, io_nsecs, p2p_nsecs);
	unlock_shared(&routable_lock);
}

/**
 * routing_destroy - Tears down the routing engine.
 */
void routing_destroy(void)
{
	topology_destroy(&routing_context.topo);
}

/**
 * routing_sd_init - Initializes the routing-specific data within a sd.
 * @sd: The sd to initialize.
 */
void routing_sd_init(struct fsubdev *sd)
{
	/* shift from 1GB space in platform device to routing block size */
	static const int shift = order_base_2(ROUTING_MIN_DPA_PER_SD) - 30;

	u16 sd_size = sd->fdev->pd->dpa.pkg_size / sd->fdev->pd->sd_cnt;

	WARN(sd_size & ((1u << shift) - 1),
	     "package size not a multiple of the minimum DPA block size");

	sd->routing.topo = &routing_context.topo;
	sd->routing.state = TILE_ROUTING_STATE_VALID;
	INIT_LIST_HEAD(&sd->routing.plane_link);

	sd->routing.dpa_idx_base = (sd->fdev->pd->dpa.pkg_offset >> shift) +
				   sd_index(sd);

	sd->routing.dpa_idx_range = sd_size >> shift;
}

/**
 * routing_sd_destroy - Uninitializes the routing-specific data within a sd.
 * @sd: The sd to destroy.
 */
void routing_sd_destroy(struct fsubdev *sd)
{
	routing_sd_transition_error(sd);
	memset(&sd->routing, 0, sizeof(sd->routing));
}

/**
 * routing_port_routed_query - Returns port usage status.
 * @sd: sd to operate on
 * @port_mask: mask of ports to query
 * @usage_mask: mask of usage by port
 *
 * The bits in both masks are logical port numbers, inclusive of port 0, and
 * must be long enough to include the maximum fabric port number.
 */
void routing_port_routed_query(struct fsubdev *sd, unsigned long *port_mask,
			       unsigned long *usage_mask)
{
	u8 lpn;

	for_each_set_bit(lpn, port_mask, PORT_COUNT) {
		struct fport *port = get_fport_handle(sd, lpn);

		if (port && atomic_read(&port->routed))
			__set_bit(port->lpn, usage_mask);
	}
}

void routing_generation_read(u32 *counter_start, u32 *counter_end)
{
	*counter_start = (u32)atomic_read(&routing_context.gen_start);
	*counter_end = (u32)atomic_read(&routing_context.gen_end);
}
