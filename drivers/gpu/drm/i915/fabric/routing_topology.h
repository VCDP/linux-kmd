/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#ifndef ROUTING_TOPO_H_INCLUDED
#define ROUTING_TOPO_H_INCLUDED

#include <linux/sizes.h>
#include <linux/bitfield.h>
#include <linux/xarray.h>

#include "csr.h"
#include "iaf_drv.h"

#define ROUTING_UFT_SIZE (48u * SZ_1K)

/* the alignment of the top-level devices in the DPA space */
#define ROUTING_DEV_DPA_ALIGNMENT (128ull * SZ_1G)

/* the minimum size of the dpa range for a subdevice */
#define ROUTING_MIN_DPA_PER_SD (8ull * SZ_1G)

/*
 * FID Structure
 *
 * [19:6] base fid
 *          - pulled from the whoami register (source) or the bridge fid
 *            lookup table (destination)
 * [ 5:3] path id
 *          - pulled from the hash/modulo logic applied to the destination
 *            physical address
 *          - note that this is configurable from 0 to 3 bits
 * [ 2:1] mdfi channel
 *          - set by the originator... source and destination always use
 *            the same mdfi channel
 * [ 0:0] host flag
 *          - not currently used; spec'd for a method of indication that
 *            this traffic is bound for a host memory interface
 *
 * Due to the host flag being unused, all odd FIDs are invalid within a
 * block.
 */
#define ROUTING_FID_BASE_MASK GENMASK(19, 6)
#define ROUTING_FID_PATH_MASK GENMASK(5, 3)
#define ROUTING_FID_MDFI_MASK GENMASK(2, 1)
#define ROUTING_FID_HOST_MASK GENMASK(0, 0)

static inline bool is_host_fid(u16 fid)
{
	return FIELD_GET(ROUTING_FID_HOST_MASK, fid);
}

/*
 * FID Address Space
 *
 * The following shows the layout of the FID address space.
 *
 * Tiles are assumed to be 64 GB aligned; thus the upper bits of the DPA
 * starting at bit 36 determine the address range index.  This index then maps
 * twice into the FID space: once for the management FID, and once for the
 * contiguous block of FIDs assigned to the bridge endpoints.
 *
 * The first 64 FIDs are reserved to ensure any that any a DFID lookup that
 * emits 0 can not overlap any valid FIDs.
 *
 * The second reserved block merely pads to the next by-64 alignment to begin
 * addressing bridge blocks.
 *
 * ---------
 *      0 : reserved
 *    ...
 *     63 : reserved
 * ---------
 *     64 : Mgmt FID for sd with DPA index 0 (0 GB)
 *     65 : Mgmt FID for sd with DPA index 1 (64 GB)
 *     66 : Mgmt FID for sd with DPA index 2 (128 GB)
 *    ...
 *    818 : Mgmt FID for sd with DPA index 755 (48320 GB)
 * ---------
 *    819 : reserved
 *    ...
 *    831 : reserved
 * ---------
 *    832 : Starting bridge FID for sd with DPA index 0
 *    896 : Starting bridge FID for sd with DPA index 1
 *    960 : Starting bridge FID for sd with DPA index 2
 *    ...
 *  49088 : Starting bridge FID for sd with DPA index 755
 * ---------
 */

/* The size of a FID block assigned to a sd for bridge ports. */
#define ROUTING_FID_BLOCK_SIZE 64

/*
 * Generates a FID mask for a FID block consisting of only valid DFIDs
 * (those without the host flag set).
 */
#define ROUTING_FID_BLOCK_MASK 0x5555555555555555ull

/* The number of valid (non-host) FIDs per block. */
#define ROUTING_VALID_FIDS_PER_BLOCK 32

/* Limit on maximum path length through a fabric. */
#define ROUTING_HOP_LIMIT 64

/*
 * The starting FID for management port assignment.
 *
 * Defaulted to the block size so that all FIDs in the "zero" block are
 * unused.  This allows a zero output in the DPA->DFID LUT to refer
 * to an entire invalid block, rather than just an invalid base FID.
 */
#define ROUTING_FID_CPORT_BASE ROUTING_FID_BLOCK_SIZE

/*
 * The starting FID for bridge port assignment.
 *
 * Must be aligned to the block size.
 */
#define ROUTING_FID_BLOCK_BASE 832

/*
 * The maximum number of supported devices.
 *
 * A device with a DPA range index less than this is guaranteed to receive a
 * FID and be mappable by the DFID LUT.
 *
 * Determined by the minimum of:
 *   - the number of CPORT FIDs that fit into the FID address space
 *   - the number of bridge FID blocks that fit into the FID address space
 *   - the number of entries available in the DPA->DFID lookup table
 */
#define ROUTING_MAX_DEVICES 755

/*
 * The number of entries in the bridge DFID lookup table.
 */
#define ROUTING_DPA_DFID_MAP_SIZE 8192

/*
 * The FIDGEN LUT maps DPA indexes to only the upper bits of the DFID, which
 * varies based on the modulo in use.  Since we're statically configuring a
 * maximum modulo, this is statically 6 bits of shift to convert between
 * LUT entries and DFIDs.
 *
 * DFID == (LUT[i] << 6) | (PATH << 3) | (MDFI << 1) | HOST
 */
#define ROUTING_DPA_DFID_MAP_SHIFT 6

struct routing_topology;

struct routing_fidgen {
	/*
	 * mask over the full physical address (NOT 46-bit bridge address
	 * line)
	 *   [51:0] address mask
	 */
	u64 mask_a;
	u64 mask_b;
	u64 mask_d;
	u64 mask_h;

	/* [6:6] shift right flag */
	/* [5:0] shift amount */
	u8 shift_a;
	u8 shift_b;
	u8 shift_h;

	/* [2:0] hash mask modulo operand */
	u8 modulo;

	/*
	 * each entry:
	 *   [19:0] base DFID (lower bits driven by hash/modulo mask and mdfi
	 *            channel)
	 */
	u16 map_size;
	u64 map[];
};

/*
 * invalid port value for the uncompressed 8-bit hw rpipe used by the mbdb
 * mailbox operations
 *
 * don't set the upper 'X' bit used for override tables or adaptive routing,
 * as these features are disabled by ANR HW
 */
#define ROUTING_UFT_INVALID_PORT8 0x7f

/*
 * invalid port value for the compressed 4-bit driver uft structure used by
 * the in-memory driver structures
 */
#define ROUTING_UFT_INVALID_PORT4 0xf

/*
 * Routing Table Structure
 *
 * A unicast forwarding table (UFT) is a mapping from destination FID (DFID)
 * to output port.
 *
 * On devices, this is stored in the route pipe IP block (RPIPE), with 48k
 * entries, indexed by DFID, each mapping to an 8-bit port.
 *
 * In the driver, we allocate UFTs sparsely to save space in the common case
 * of fabrics much smaller than hardware allows.  We allocate individual
 * contiguous subsets (blocks) of the UFT to either:
 *
 *   1. mgmt ports, each of which gets 1 FID for port 0, and
 *
 *   2. sd bridges, each of which gets a contiguous block of FIDs to address
 *      its bridge endpoints, allowing for multiple endpoints and paths.
 *
 * The block of management FIDs is allocated as a single contiguous block.
 * Each sd is additionally allocated a single contiguous block for its bridge
 * FIDs, which is stored in an xarray.
 *
 * In the driver, to reduce storage requirements, routing blocks store each
 * DFID->port mapping in 4 bits, so there ends up being two mappings in each
 * byte, with odd indices stored in the low nibbles, as per the following
 * memory layout:
 *
 *             ---------------------------------------------------
 *        byte |        0|        1|        2|        3|      ...|
 *             ---------------------------------------------------
 *         bit |7654|3210|7654|3210|7654|3210|7654|3210|7654|3210|
 *             ---------------------------------------------------
 *  fid offset |   0|   1|   2|   3|   4|   5|   6|   7|   8| ...|
 *             ---------------------------------------------------
 *
 * The routing_entry_* functions abstract away this encoding.
 */

/**
 * struct routing_uft - Sparsely stores all of the unicast forwarding table
 * (UFT) entries for a sd.
 * @mgmt: A single UFT block mapping mgmt DFIDs to ports.
 * @bridges: An xarray of UFT blocks mapping bridge DFIDs to ports, one block
 *   per destination sd, indexed by DPA index.  Entries are allocated
 *   on demand during routing.
 */
struct routing_uft {
	u8 *mgmt;
	struct xarray bridges;
};

u8 *routing_uft_block_alloc(size_t len);
u8 *routing_uft_bridge_get(struct routing_uft *uft, struct fsubdev *sd_dst);

struct routing_uft *routing_uft_alloc(void);
void routing_uft_destroy(struct routing_uft *uft);
void routing_uft_update(struct fsubdev *sd);

void routing_fidgen_update(struct fsubdev *sd);

static inline void routing_update(struct fsubdev *sd)
{
	routing_uft_update(sd);
	routing_fidgen_update(sd);
}

/**
 * routing_uft_entry_set - Sets the port at the specified FID specified as an
 * offset relative to the base of the specified UFT block.
 * @block: The block to update.
 * @fid_offset: The zero-based offset into the block.
 * @port: The egress port.
 */
static inline void routing_uft_entry_set(u8 *block, u16 fid_offset, u8 port)
{
	u16 i = fid_offset >> 1;

	if (fid_offset & 1)
		block[i] = (block[i] & 0xf0) | (port & 0xf);
	else
		block[i] = (port << 4) | (block[i] & 0xf);
}

/**
 * routing_uft_entry_get - Returns the port at the specified FID specified as
 * an offset relative to the base of the specified UFT block.
 * @block: The block to update.
 * @fid_offset: The zero-based offset into the block.
 *
 * Return: The egress port.
 */
static inline u8 routing_uft_entry_get(u8 *block, u16 fid_offset)
{
	return (block[fid_offset >> 1] >> (fid_offset & 1 ? 0 : 4)) & 0xf;
}

static inline int routing_sd_is_error(struct fsubdev *sd)
{
	return sd->routing.state == TILE_ROUTING_STATE_ERROR;
}

#define ROUTING_COST_INFINITE 0xffff

struct routing_plane {
	struct list_head topo_link;
	struct list_head sd_list;
	struct routing_topology *topo;
	u16 index;
	u16 num_subdevs;
	u16 *cost;
};

/**
 * struct routing_topology - Top-level fabric context maintained during a
 * routing sweep.
 * @plane_list: list of planes discovered
 * @num_planes: number of planes in plane_list
 * @max_dpa_index: maximum DPA index (inclusive)
 * @sd_error_signal: true if an sd transitioned to error during the sweep
 * @fid_groups: bit mask of in-use fid groups
 */
struct routing_topology {
	struct list_head plane_list;
	u16 num_planes;
	u16 max_dpa_index;
	bool sd_error_signal;
	unsigned long fid_groups[BITS_TO_LONGS(ROUTING_MAX_DEVICES)];
};

/*
 * The following API methods wrap the error flag in a tiny API that clarifies
 * its usage: to allow the topology context for a sweep to be signalled of any
 * subdevices transitioning to error, which guides error recovery behavior in
 * the routing engine.
 */

static inline void routing_topo_reset_sd_error(struct routing_topology *topo)
{
	topo->sd_error_signal = false;
}

static inline void routing_topo_signal_sd_error(struct routing_topology *topo)
{
	topo->sd_error_signal = true;
}

static inline bool routing_topo_check_sd_error(struct routing_topology *topo)
{
	return topo->sd_error_signal;
}

/**
 * neighbor_of - Returns the neighbor port associated with the specified port.
 * @port: The local port to query the neighbor of.
 *
 * Only valid if the cached_neighbor field has been populated, and only remains
 * valid as long as the routing lock is held.
 *
 * Return: The port neighboring the specified port.
 */
static inline struct fport *neighbor_of(struct fport *port)
{
	return port->cached_neighbor;
}

static inline bool port_is_routable(struct fport *port)
{
	return port->state == PM_PORT_STATE_ACTIVE &&
			test_bit(PORT_CONTROL_ROUTABLE, port->controls);
}

void routing_sd_transition_error(struct fsubdev *sd);

struct fsubdev *routing_sd_iter(int all);
struct fsubdev *routing_sd_next(struct fsubdev *sd, int all);

#endif /* ROUTING_TOPO_H_INCLUDED */
