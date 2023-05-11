// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#include <linux/bitmap.h>
#include <linux/bitfield.h>

#include "csr.h"
#include "routing_logic.h"
#include "trace.h"

/*
 * Returns the C-Port management FID for a sub-device.
 *
 * Valid if the device DPA is valid, undefined otherwise.
 */
static u16 fid_cport(u16 fid_group)
{
	return ROUTING_FID_CPORT_BASE + fid_group;
}

/*
 * Returns the bridge port base FID for a sub-device.
 *
 * Valid if the device DPA is valid, undefined otherwise.
 */
static u16 fid_block_base(u16 fid_group)
{
	return ROUTING_FID_BLOCK_BASE + ROUTING_FID_BLOCK_SIZE * fid_group;
}

/*
 * Returns the bridge endpoint port index corresponding to the FID.
 */
static u8 fid_to_bridge_offset(u16 fid)
{
	return FIELD_GET(ROUTING_FID_MDFI_MASK, fid);
}

/*
 * Returns the cost between the specified source and destination subdevices.
 *
 * Returns ROUTING_COST_INFINITE if the two do not share a plane.
 */
u16 routing_cost_lookup(struct fsubdev *sd_src, struct fsubdev *sd_dst)
{
	int i, j, n;

	if (!sd_src->routing.plane ||
	    sd_src->routing.plane != sd_dst->routing.plane)
		return ROUTING_COST_INFINITE;

	i = sd_src->routing.plane_index;
	j = sd_dst->routing.plane_index;
	n = sd_src->routing.plane->num_subdevs;

	return sd_src->routing.plane->cost[i * n + j];
}

/**
 * init - Perform top-level scan of subdevices and initialize any necessary
 * state.
 * @topo: The topology to operate on.
 */
static void init(struct routing_topology *topo)
{
	struct fsubdev *sd;
	u16 sd_max;

	topo->max_dpa_index = 0;

	for (sd = routing_sd_iter(0); sd; sd = routing_sd_next(sd, 0)) {
		pr_debug("device %u sd %u dpa_base 0x%04x dpa_range 0x%04x\n",
			 sd->fdev->pd->index, sd_index(sd),
			 sd->routing.dpa_idx_base, sd->routing.dpa_idx_range);

		sd_max = sd->routing.dpa_idx_base +
			 sd->routing.dpa_idx_range - 1;

		if (topo->max_dpa_index < sd_max)
			topo->max_dpa_index = sd_max;
	}
}

/*
 * Establishes the cached neighbor pointer for a single fabric port.
 */
static void process_neighbor(struct fsubdev *sd_src,
			     struct fport *port_src)
{
	struct fsubdev *sd_dst;
	struct fport *port_dst;
	u64 nguid;

	port_src->cached_neighbor = NULL;

	if (!port_is_routable(port_src))
		return;

	if (!port_src->portinfo)
		return;

	nguid = port_src->portinfo->neighbor_guid;
	if (!nguid || sd_src->guid == nguid)
		return;

	sd_dst = find_routable_sd(nguid);
	if (!sd_dst)
		return;

	port_dst = get_fport_handle(sd_dst,
				    port_src->portinfo->neighbor_port_number);
	if (!port_dst)
		return;

	if (!port_is_routable(port_dst))
		return;

	if (!port_dst->portinfo)
		return;
	if (port_dst->portinfo->neighbor_guid != sd_src->guid)
		return;
	if (port_dst->portinfo->neighbor_port_number != port_src->lpn)
		return;

	port_src->cached_neighbor = port_dst;
}

/*
 * Establishes the cached neighbor pointers across all sd fabric ports.
 */
static void process_neighbors(void)
{
	struct fsubdev *sd;
	struct fport *port;
	u8 lpn;

	for (sd = routing_sd_iter(0); sd; sd = routing_sd_next(sd, 0)) {
		for_each_fabric_lpn(lpn, sd) {
			port = get_fport_handle(sd, lpn);
			if (!port)
				continue;

			process_neighbor(sd, port);
		}
	}
}

static struct routing_plane *plane_alloc(struct routing_topology *topo)
{
	struct routing_plane *plane = kzalloc(sizeof(*plane), GFP_KERNEL);

	if (!plane)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&plane->topo_link);
	INIT_LIST_HEAD(&plane->sd_list);
	plane->index = topo->num_planes;
	plane->topo = topo;

	list_add_tail(&plane->topo_link, &topo->plane_list);
	topo->num_planes++;

	return plane;
}

/**
 * routing_plane_destroy - Destroys and deallocs the specified plane.
 * @plane: The plane to destroy.
 *
 * Tiles that were members of the plane have their plane assignment cleared,
 * but otherwise remain in their present state.
 */
void routing_plane_destroy(struct routing_plane *plane)
{
	struct fsubdev *sd, *sd_next;

	list_for_each_entry_safe(sd, sd_next, &plane->sd_list,
				 routing.plane_link) {
		list_del_init(&sd->routing.plane_link);
		sd->routing.plane = NULL;
		sd->routing.plane_index = 0;
	}

	list_del(&plane->topo_link);
	plane->topo->num_planes--;

	kfree(plane->cost);
	kfree(plane);
}

/**
 * plane_fail - Takes all subdevices in a plane to their error state, then
 * destroys the plane.
 * @plane: The plane to operate on.
 */
static void plane_fail(struct routing_plane *plane)
{
	struct fsubdev *sd, *sd_next;

	/* _safe since the error trasitions remove subdevices from the plane */
	list_for_each_entry_safe(sd, sd_next, &plane->sd_list,
				 routing.plane_link)
		routing_sd_transition_error(sd);

	routing_plane_destroy(plane);
}

static void propagate_plane_entry(struct list_head *queue,
				  struct routing_plane *plane,
				  struct fsubdev *sd)
{
	struct fsubdev *sd_neighbor;
	struct fport *port, *port_neighbor;
	u8 lpn;

	list_add_tail(&sd->routing.plane_link, &plane->sd_list);
	sd->routing.plane_index = sd->routing.plane->num_subdevs++;

	for_each_fabric_lpn(lpn, sd) {
		port = get_fport_handle(sd, lpn);

		port_neighbor = neighbor_of(port);
		if (!port_neighbor)
			continue;

		sd_neighbor = port_neighbor->sd;
		if (routing_sd_is_error(sd_neighbor) ||
		    sd_neighbor->routing.plane)
			continue;

		sd_neighbor->routing.plane = plane;
		list_add_tail(&sd_neighbor->routing.plane_link, queue);
	}
}

/**
 * propagate_plane - Propagates a plane to the specified sd's neighbors in a
 * breadth-first manner.
 * @plane: The current plane being searched.
 * @root: The root sd of the search.
 *
 * Uses whether a given sd has a plane assigned as an indicator of whether it
 * has been visited already.
 */
static void propagate_plane(struct routing_plane *plane,
			    struct fsubdev *root)
{
	struct fsubdev *sd;
	LIST_HEAD(queue);

	trace_rt_plane(root);

	list_add_tail(&root->routing.plane_link, &queue);

	while (!list_empty(&queue)) {
		sd = list_first_entry(&queue, struct fsubdev,
				      routing.plane_link);
		list_del_init(&sd->routing.plane_link);

		pr_debug("plane %u device %u sd %u\n",
			 sd->routing.plane->index, sd->fdev->pd->index,
			 sd_index(sd));

		propagate_plane_entry(&queue, plane, sd);
	}
}

/*
 * build_planes - Dynamically calculates the planes that exist on the fabric.
 *
 * It does this by a single linear top-level pass over all subdevices.  If the
 * subdevice is unassigned to a plane, it allocates a new plane, and then
 * propagates that plane to all of that subdevice's neighbors transitively by a
 * breadth-first traversal.
 *
 * After propagating a plane, all subdevices on that plane have been visited
 * and assigned, and all subdevices still unassigned correspond to new planes.
 */
static int build_planes(struct routing_topology *topo)
{
	struct routing_plane *plane, *plane_next;
	struct fsubdev *sd;
	int err;

	list_for_each_entry_safe(plane, plane_next, &topo->plane_list,
				 topo_link)
		routing_plane_destroy(plane);

	WARN_ON(topo->num_planes);

	for (sd = routing_sd_iter(0); sd; sd = routing_sd_next(sd, 0)) {
		if (sd->routing.plane)
			continue;

		sd->routing.plane = plane_alloc(topo);
		if (IS_ERR(sd->routing.plane)) {
			err = PTR_ERR(sd->routing.plane);
			routing_sd_transition_error(sd);
			pr_err("%s: failed to initialize plane: %d\n",
			       __func__, err);
			return err;
		}

		propagate_plane(sd->routing.plane, sd);
	}

	return 0;
}

/**
 * assign_fid_sd - Allocate FIDs.
 * @topo: The topology to operate on.
 * @sd: The subdevice to operate on.
 *
 * Allocates a single FID allocated to the management port.
 * Allocates a base FID for a contiguous block allocated to the bridge.
 */
static void assign_fid_sd(struct routing_topology *topo, struct fsubdev *sd)
{
	sd->routing.fid_group = sd->fdev->pd->index * IAF_MAX_SUB_DEVS +
				sd_index(sd);
	sd->routing.fid_mgmt = fid_cport(sd->routing.fid_group);
	sd->routing.fid_base = fid_block_base(sd->routing.fid_group);

	pr_debug("device %u sd %u fid_mgmt 0x%04x fid_base 0x%04x\n",
		 sd->fdev->pd->index, sd_index(sd),
		 sd->routing.fid_mgmt, sd->routing.fid_base);

	trace_rt_assign_fids(sd);
}

/**
 * assign_fids - Allocate FIDs to each sd.
 * @topo: The topology to operate on.
 */
static void assign_fids(struct routing_topology *topo)
{
	struct fsubdev *sd;

	for (sd = routing_sd_iter(0); sd; sd = routing_sd_next(sd, 0))
		assign_fid_sd(topo, sd);
}

/**
 * compute_cost_for_plane - Computes a cost matrix for the given plane.
 * @plane: The plane to operate on.
 *
 * This is an N^2 matrix that encodes the cost between any source and
 * destination sd by "plane index", or the per-plane index assigned to
 * each sd during plane creation.
 *
 * Cost is currently hop distance, but could in the future be weighted
 * by link width/speed, LQI, or other factors.
 *
 * Return: Zero on success, non-zero otherwise.
 */
static int compute_cost_for_plane(struct routing_plane *plane)
{
	struct fsubdev *sd;
	struct fport *port, *port_neighbor;
	int n = plane->num_subdevs;
	size_t cost_size = sizeof(*plane->cost) * n * n;
	int i, j, k;
	int ij, ik, kj;
	u16 value;
	u8 lpn;

	plane->cost = kmalloc(cost_size, GFP_KERNEL);
	if (!plane->cost)
		return -ENOMEM;

	memset(plane->cost, 0xff, cost_size);

	/* generate a single-hop cost matrix */
	list_for_each_entry(sd, &plane->sd_list, routing.plane_link) {
		i = sd->routing.plane_index;
		plane->cost[i * n + i] = 0;
		for_each_fabric_lpn(lpn, sd) {
			port = get_fport_handle(sd, lpn);

			port_neighbor = neighbor_of(port);
			if (!port_neighbor)
				continue;

			j = port_neighbor->sd->routing.plane_index;
			plane->cost[i * n + j] = 1;
		}
	}

	/*
	 * relax single-hop to multi-hop all-pairs shortest-path via
	 * floyd-warshall
	 */
	for (k = 0; k < n; ++k) {
		for (i = 0; i < n - 1; ++i) {
			if (i == k)
				continue;
			ik = i * n + k;
			if (plane->cost[ik] == ROUTING_COST_INFINITE)
				continue;
			for (j = i + 1; j < n; ++j) {
				if (j == k)
					continue;
				ij = i * n + j;
				kj = k * n + j;
				value = plane->cost[ik] + plane->cost[kj];
				if (plane->cost[ij] > value) {
					plane->cost[ij] = value;
					plane->cost[j * n + i] = value;
				}
			}
		}
	}

	return 0;
}

static void compute_costs(struct routing_topology *topo)
{
	struct routing_plane *plane, *plane_next;
	int err;

	list_for_each_entry_safe(plane, plane_next, &topo->plane_list,
				 topo_link) {
		err = compute_cost_for_plane(plane);
		if (err) {
			pr_err("%s: failed to compute cost matrix for plane %u: %d\n",
			       __func__, plane->index, err);
			plane_fail(plane);
		}
	}
}

static void set_dpa_lut(struct fsubdev *sd_src, struct fsubdev *sd_dst,
			u16 dfid)
{
	int i;
	int start = sd_dst->routing.dpa_idx_base;
	int end = start + sd_dst->routing.dpa_idx_range;

	for (i = start; i < end; ++i) {
		sd_src->routing.fidgen_next->map[i] =
			dfid >> ROUTING_DPA_DFID_MAP_SHIFT;
		pr_debug("device %u sd %u to device %u sd %u: dpa_idx %d dfid 0x%04x\n",
			 sd_src->fdev->pd->index, sd_index(sd_src),
			 sd_dst->fdev->pd->index, sd_index(sd_dst), i, dfid);
	}
}

static void map_addresses_for_sd_dst(struct fsubdev *sd_src,
				     struct fsubdev *sd_dst)
{
	struct fdev *dev_dst;
	struct fsubdev *neighbor;
	int i;

	/*
	 * same-package addresses are invalid.
	 * hw has no support for bridge->bridge data path, and we do not
	 * support routing tiles within a package.
	 */
	if (sd_src->fdev == sd_dst->fdev) {
		set_dpa_lut(sd_src, sd_dst, 0);
		return;
	}

	/* directly accessibile addresses route normally */
	if (routing_cost_lookup(sd_src, sd_dst) == 1) {
		set_dpa_lut(sd_src, sd_dst, sd_dst->routing.fid_base);
		return;
	}

	/*
	 * indirectly accessibile dpas route via directly acessible
	 * subdevices on the same package
	 */
	dev_dst = sd_dst->fdev;
	for (i = 0; i < dev_dst->pd->sd_cnt; ++i) {
		neighbor = &dev_dst->sd[i];
		if (sd_dst == neighbor)
			continue;
		if (routing_sd_is_error(neighbor))
			continue;
		if (routing_cost_lookup(sd_src, neighbor) != 1)
			continue;

		set_dpa_lut(sd_src, sd_dst, neighbor->routing.fid_base);
		return;
	}
}

/**
 * fidgen_alloc - Allocates and initializes FIDGEN registers.
 * @topo: The topology to operate on.
 *
 * Return: A pointer to the fidgen struct, or NULL otherwise.
 */
static struct routing_fidgen *fidgen_alloc(struct routing_topology *topo)
{
	struct routing_fidgen *fidgen;
	u16 map_size = topo->max_dpa_index + 1;

	fidgen = kzalloc(struct_size(fidgen, map, map_size), GFP_KERNEL);
	if (!fidgen)
		return NULL;

	fidgen->map_size = map_size;

	return fidgen;
}

/**
 * fidgen_init - Fills out the bridge FIDGEN registers for DFID lookup.
 * @fidgen: The fidgen struct to operate on.
 *
 * Mask A selects tile DPA ranges as indexes into the LUT.
 *
 * Mask B is unused.
 *
 * Mask D selects all hashable bits as hash input.
 *
 * Mask H selects all hashed path bits as modulo input.
 *
 * Modulo selects the maximum number of alternate paths from H.
 *
 * Net result is that we map each DPA range into 64 DFIDs.
 */
static void fidgen_init(struct routing_fidgen *fidgen)
{
	static const u8 dpa_range_bits = order_base_2(ROUTING_MIN_DPA_PER_SD);
	static const u8 dpa_index_bits = CSR_FIDGEN_LUT_INDEX_WIDTH;
	static const u64 index_mask = GENMASK_ULL(dpa_index_bits, 0);
	static const u64 hash_mask = GENMASK_ULL(63, 0);

	fidgen->mask_a = index_mask << dpa_range_bits;
	fidgen->shift_a = dpa_range_bits |
			  FIELD_PREP(MASK_FIDGEN_SHIFT_RIGHT, 1);

	/* fidgen->mask_b is unused; default to 0 */

	fidgen->mask_h = hash_mask;
	/* fidgen->shift_h = 0; */

	fidgen->mask_d = FIELD_GET(MASK_FIDGEN_MASK_D, ~0ull);

	fidgen->modulo = 7; /* fid block size hard-coded to maximum */
}

/**
 * map_addresses_for_sd - Build the DPA-to-DFID map.
 * @topo: The topology to operate on.
 * @sd_src: The source sd.
 *
 * For each source/dest pair, if the destination is accessible according to
 * the routing rules, map the destination's address to the destination's DFID.
 * If the destination is not accessible, attempt to find the first sd on the
 * destination device that is, and use it instead.
 *
 * Currently specific to point-to-point without route-through, where
 * "accessible" is defined as reachable in a single hop.
 */
static void map_addresses_for_sd(struct routing_topology *topo,
				 struct fsubdev *sd_src)
{
	struct fsubdev *sd_dst;
	struct routing_fidgen *fidgen;

	fidgen = fidgen_alloc(topo);
	if (!fidgen) {
		routing_sd_transition_error(sd_src);
		return;
	}

	fidgen_init(fidgen);
	sd_src->routing.fidgen_next = fidgen;

	for (sd_dst = routing_sd_iter(0); sd_dst;
	     sd_dst = routing_sd_next(sd_dst, 0))
		map_addresses_for_sd_dst(sd_src, sd_dst);
}

/**
 * map_addresses - Build the DPA-to-DFID maps for all devices.
 * @topo: The topology to operate on.
 */
static void map_addresses(struct routing_topology *topo)
{
	struct fsubdev *sd;

	for (sd = routing_sd_iter(0); sd; sd = routing_sd_next(sd, 0))
		map_addresses_for_sd(topo, sd);
}

/**
 * unicast_route_pair_via - Route all remote DFIDs of the dest device via a
 * specific list of egress ports.
 * @uft: The unicast forwarding table to update.
 * @sd_src: The source sd being routed.
 * @sd_dst: The destination sd being routed.
 * @ports: The array of outbound ports on the source that point to the
 *         destination.
 * @num_ports: The size of the ports array.
 */
static void unicast_route_pair_via(struct routing_uft *uft,
				   struct fsubdev *sd_src,
				   struct fsubdev *sd_dst,
				   u8 *ports, u8 num_ports)
{
	u8 *block;
	u16 fid;
	u16 mod = 0;
	u8 port;

	WARN_ON(!num_ports);

	/* don't load balance mgmt fids; just use the first available port */
	routing_uft_entry_set(uft->mgmt, sd_dst->routing.fid_mgmt -
			      ROUTING_FID_CPORT_BASE, ports[0]);

	pr_debug("uft: device %u sd %u fid 0x%04x port %u\n",
		 sd_src->fdev->pd->index, sd_index(sd_src),
		 sd_dst->routing.fid_mgmt, ports[0]);

	block = routing_uft_bridge_get(uft, sd_dst);
	if (IS_ERR(block)) {
		routing_sd_transition_error(sd_src);
		return;
	}

	for (fid = 0; fid < ROUTING_FID_BLOCK_SIZE; ++fid) {
		if (is_host_fid(fid)) {
			/* we don't use the host bit; invalidate these fids */
			routing_uft_entry_set(block, fid,
					      ROUTING_UFT_INVALID_PORT4);
			continue;
		}

		port = ports[mod++ % num_ports];

		routing_uft_entry_set(block, fid, port);

		pr_debug("uft: device %u sd %u fid 0x%04x port %u\n",
			 sd_src->fdev->pd->index, sd_index(sd_src),
			 sd_dst->routing.fid_base + fid, port);

		trace_rt_pair(sd_src->fdev->pd->index, sd_index(sd_src),
			      fid, port);
	}
}

/**
 * unicast_route_pair - Route all remote DFIDs of the dest device over fabric
 * ports of the source device.
 * @uft: The unicast forwarding table to update.
 * @sd_src: The source sd being routed.
 * @sd_dst: The destination sd being routed.
 */
static void unicast_route_pair(struct routing_uft *uft,
			       struct fsubdev *sd_src,
			       struct fsubdev *sd_dst)
{
	struct fport *port_src;
	struct fport *port_dst;
	u8 ports[PORT_FABRIC_COUNT] = {};
	u8 num_ports = 0;
	u8 lpn;

	for_each_fabric_lpn(lpn, sd_src) {
		port_src = get_fport_handle(sd_src, lpn);
		if (!port_src)
			continue;

		port_dst = neighbor_of(port_src);
		if (!port_dst)
			continue;

		if (port_dst->sd != sd_dst)
			continue;

		ports[num_ports++] = lpn;
	}

	if (!num_ports)
		return;

	unicast_route_pair_via(uft, sd_src, sd_dst, ports, num_ports);
}

/**
 * unicast_route_local - Route the FIDs local to the sd via the bridge ports.
 * @uft: The unicast forwarding table to update.
 * @sd_src: The source sd being routed.
 */
static void unicast_route_local(struct routing_uft *uft,
				struct fsubdev *sd_src)
{
	u8 *block;
	u16 fid;
	u8 base;
	u8 bridge_offset;
	u8 port;

	routing_uft_entry_set(uft->mgmt, sd_src->routing.fid_mgmt -
			      ROUTING_FID_CPORT_BASE, 0);

	pr_debug("uft: device %u sd %u fid 0x%04x port 0\n",
		 sd_src->fdev->pd->index, sd_index(sd_src),
		 sd_src->routing.fid_mgmt);

	trace_rt_local(sd_src->fdev->pd->index, sd_index(sd_src),
		       sd_src->routing.fid_mgmt, 0);

	block = routing_uft_bridge_get(uft, sd_src);
	if (IS_ERR(block)) {
		routing_sd_transition_error(sd_src);
		return;
	}

	for (fid = 0; fid < ROUTING_FID_BLOCK_SIZE; ++fid) {
		if (is_host_fid(fid)) {
			/* we don't use the host bit; invalidate these fids */
			routing_uft_entry_set(block, fid,
					      ROUTING_UFT_INVALID_PORT4);
			continue;
		}

		base = sd_src->routing.fid_base;
		bridge_offset = fid_to_bridge_offset(base + fid);
		port = PORT_BRIDGE_START + bridge_offset;

		routing_uft_entry_set(block, fid, port);

		pr_debug("uft: device %u sd %u fid 0x%04x port %u\n",
			 sd_src->fdev->pd->index, sd_index(sd_src),
			 sd_src->routing.fid_base + fid, port);

		trace_rt_local(sd_src->fdev->pd->index,
			       sd_index(sd_src), fid, port);
	}
}

/**
 * unicast_route_src - Route all FIDs accessible from a given sd.
 * @sd_src: The source sd being routed.
 */
static void unicast_route_src(struct fsubdev *sd_src)
{
	struct fsubdev *sd_dst;

	sd_src->routing.uft_next = routing_uft_alloc();
	if (IS_ERR(sd_src->routing.uft_next)) {
		routing_sd_transition_error(sd_src);
		return;
	}

	for (sd_dst = routing_sd_iter(0); sd_dst;
	     sd_dst = routing_sd_next(sd_dst, 0)) {
		if (routing_cost_lookup(sd_src, sd_dst) != 1)
			continue;
		unicast_route_pair(sd_src->routing.uft_next, sd_src, sd_dst);
	}

	unicast_route_local(sd_src->routing.uft_next, sd_src);
}

/**
 * unicast_routing - Route all FIDs across all devices.
 */
static void unicast_routing(void)
{
	struct fsubdev *sd;

	for (sd = routing_sd_iter(0); sd; sd = routing_sd_next(sd, 0))
		unicast_route_src(sd);
}

/**
 * routing_logic_run - Fully programs/routes the specified topology in the
 * manner of a one-time programming.
 * @topo: The topology to program/route.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int routing_logic_run(struct routing_topology *topo)
{
	int err;

	pr_debug("initialize sweep\n");
	init(topo);

	pr_debug("process neighbors\n");
	process_neighbors();

	pr_debug("build planes\n");
	err = build_planes(topo);
	if (err) {
		pr_err("%s: failed to build planes: %d\n", __func__, err);
		return err;
	}

	pr_debug("assign fids\n");
	assign_fids(topo);

	pr_debug("compute costs\n");
	compute_costs(topo);

	pr_debug("unicast routing\n");
	unicast_routing();

	pr_debug("map addresses\n");
	map_addresses(topo);

	return 0;
}
