// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#include <linux/bitfield.h>

#include "csr.h"
#include "ops.h"
#include "parallel.h"
#include "routing_io.h"
#include "routing_pause.h"

#define MAX_UFT_ENTRIES \
	(MBOX_PARAM_AREA_IN_BYTES - sizeof(struct mbdb_op_rpipe_set))

static void block_to_rpipe(u8 *rpipe, u8 *block, u16 len)
{
	u16 i;
	u8 port;

	for (i = 0; i < len; ++i) {
		port = routing_uft_entry_get(block, i);
		rpipe[i] = port == ROUTING_UFT_INVALID_PORT4 ?
			   ROUTING_UFT_INVALID_PORT8 : port;
	}
}

static int rpipe_clear_block(struct fsubdev *sd, u32 port_mask,
			     struct mbdb_op_rpipe_set *set, u16 fid_base,
			     u16 fid_range)
{
	int err;

	sd_dbg(sd, "writing base 0x%04x range %u\n", fid_base,
	       fid_range);

	if (fid_range > MAX_UFT_ENTRIES)
		return -EINVAL;

	set->port_mask = port_mask;
	set->start_index = fid_base;
	set->num_entries = fid_range;
	memset(set->rpipe_data, ROUTING_UFT_INVALID_PORT8, fid_range);

	err = ops_rpipe_set(sd, set, set, NULL, NULL, 0);
	if (err) {
		sd_err(sd, "failed to write set: %d\n", err);
		return err;
	}

	return 0;
}

static int rpipe_write_block(struct fsubdev *sd, u32 port_mask,
			     struct mbdb_op_rpipe_set *set,
			     u8 *block, u16 fid_base, u16 fid_range)
{
	int err;

	sd_dbg(sd, "writing base 0x%04x range %u\n", fid_base,
	       fid_range);

	if (fid_range > MAX_UFT_ENTRIES)
		return -EINVAL;

	set->port_mask = port_mask;
	set->start_index = fid_base;
	set->num_entries = fid_range;

	block_to_rpipe(set->rpipe_data, block, fid_range);

	err = ops_rpipe_set(sd, set, set, NULL, NULL, 0);
	if (err) {
		sd_err(sd, "failed to write set: %d\n", err);
		return err;
	}

	return 0;
}

static bool rpipe_bridge_changed(u8 *block_curr, unsigned long block_idx,
				 struct routing_uft *uft_prev)
{
	u8 *block_prev;

	if (!uft_prev)
		return true;

	block_prev = xa_load(&uft_prev->bridges, block_idx);
	if (!block_prev)
		return true;

	return memcmp(block_curr, block_prev, ROUTING_FID_BLOCK_SIZE) != 0;
}

/**
 * rpipe_write_bridge_update - Finds and writes rpipe entries for blocks in the
 * newly computed UFT if they differ from the previous UFT.
 * @sd: The sd to write to.
 * @rpipe_buf: The mbdb op buffer to use.
 * @port_mask: The mask of ports to write to.
 *
 * Return: Zero on success, non-zero otherwise.
 */
static int rpipe_write_bridge_update(struct fsubdev *sd,
				     struct mbdb_op_rpipe_set *rpipe_buf,
				     u32 port_mask)
{
	u8 *block;
	u16 base;
	unsigned long i;
	int err;

	xa_for_each(&sd->routing.uft_next->bridges, i, block) {
		base = ROUTING_FID_BLOCK_BASE + i * ROUTING_FID_BLOCK_SIZE;

		if (!rpipe_bridge_changed(block, i, sd->routing.uft))
			continue;

		err = rpipe_write_block(sd, port_mask, rpipe_buf, block, base,
					ROUTING_FID_BLOCK_SIZE);
		if  (err) {
			sd_err(sd, "failed to write rpipe bridge block %u: %d",
			       base, err);
			return err;
		}
	}

	return 0;
}

/**
 * rpipe_write_bridge_remove - Finds and clears rpipe entries for blocks in the
 * old/previous UFT that no longer exist in the newly computed UFT.
 * @sd: The sd to write to.
 * @rpipe_buf: The mbdb op buffer to use.
 * @port_mask: The mask of ports to write to.
 *
 * Return: Zero on success, non-zero otherwise.
 */
static int rpipe_write_bridge_remove(struct fsubdev *sd,
				     struct mbdb_op_rpipe_set *rpipe_buf,
				     u32 port_mask)
{
	u8 *block;
	u16 base;
	unsigned long i;
	int err;

	xa_for_each(&sd->routing.uft->bridges, i, block) {
		if (xa_load(&sd->routing.uft_next->bridges, i))
			continue;

		base = ROUTING_FID_BLOCK_BASE + i * ROUTING_FID_BLOCK_SIZE;

		err = rpipe_clear_block(sd, port_mask, rpipe_buf, base,
					ROUTING_FID_BLOCK_SIZE);
		if  (err) {
			sd_err(sd, "failed to write rpipe bridge block %u: %d",
			       base, err);
			return err;
		}
	}

	return 0;
}

static int rpipe_write(struct fsubdev *sd)
{
	struct fport *port;
	struct mbdb_op_rpipe_set *rpipe_buf;
	size_t max_block_size = max(ROUTING_MAX_DEVICES,
				    ROUTING_FID_BLOCK_SIZE);
	size_t rpipe_size = struct_size(rpipe_buf, rpipe_data, max_block_size);
	u32 port_mask = PORT_CPORT_MASK | PORT_BRIDGE_MASK;
	int err;
	u8 lpn;

	sd_dbg(sd, "enter\n");

	rpipe_buf = kmalloc(rpipe_size, GFP_KERNEL);
	if (!rpipe_buf)
		return -ENOMEM;

	for_each_fabric_lpn(lpn, sd) {
		port = get_fport_handle(sd, lpn);
		if (!port)
			continue;

		if (port->log_state >= IB_PORT_INIT)
			port_mask |= lpn;
	}

	err = rpipe_write_block(sd, port_mask, rpipe_buf,
				sd->routing.uft_next->mgmt,
				ROUTING_FID_CPORT_BASE, ROUTING_MAX_DEVICES);
	if (err) {
		sd_err(sd, "failed to write rpipe mgmt block: %d\n", err);
		goto exit;
	}

	err = rpipe_write_bridge_update(sd, rpipe_buf, port_mask);
	if (err)
		goto exit;

	if (sd->routing.uft) {
		err = rpipe_write_bridge_remove(sd, rpipe_buf, port_mask);
		if (err)
			goto exit;
	}

exit:
	kfree(rpipe_buf);
	return err;
}

static int fidgen_write_port_pkgaddr(struct fsubdev *sd, u8 port, u32 port_base)
{
	u64 addr_base = sd->fdev->pd->dpa.pkg_offset;
	u64 addr_range = sd->fdev->pd->dpa.pkg_size;
	u64 addr_csr = FIELD_PREP(MASK_BT_PAR_BASE, addr_base) |
		       FIELD_PREP(MASK_BT_PAR_RANGE, addr_range);
	int err;

	err = ops_csr_raw_write(sd, port_base + CSR_BT_PKG_ADDR_RANGE,
				&addr_csr, sizeof(addr_csr), NULL, NULL, false);
	if (err) {
		dev_err(sd_dev(sd), "failed to set package address range: %d\n",
			err);
		return err;
	}

	return 0;
}

#define FIDGEN_BLOCK_SIZE 12

/*
 * Note that the FID isn't specific to current/next because it's invariant.
 */
static void build_fidgen_block(struct fsubdev *sd,
			       struct routing_fidgen *fidgen, u64 *data)
{
	data[0] = fidgen->mask_a;
	data[1] = fidgen->shift_a;
	data[2] = fidgen->mask_b;
	data[3] = fidgen->shift_b;
	data[4] = 0; /* rsvd0 */
	data[5] = 0; /* rsvd1 */
	data[6] = fidgen->mask_h;
	data[7] = fidgen->shift_h;
	data[8] = fidgen->modulo;
	data[9] = sd->routing.fid_base >> ROUTING_DPA_DFID_MAP_SHIFT;
	data[10] = fidgen->mask_d;
	data[11] = 0; /* static random */
}

static int fidgen_write_port(struct routing_topology *topo,
			     struct fsubdev *sd, u8 port)
{
	u32 port_base = get_raw_port_base(port);
	u64 data[FIDGEN_BLOCK_SIZE];
	u64 data_next[FIDGEN_BLOCK_SIZE];
	int len = sizeof(data);
	bool delta = true;
	int err;

	build_fidgen_block(sd, sd->routing.fidgen_next, data_next);

	if (sd->routing.fidgen) {
		build_fidgen_block(sd, sd->routing.fidgen, data);
		delta = memcmp(data, data_next, len);
	}

	if (delta) {
		err = ops_csr_raw_write(sd, port_base + CSR_FIDGEN_MASK_A,
					data_next, len, NULL, NULL, false);
		if (err) {
			dev_err(sd_dev(sd),
				"failed to write fidgen block: %d\n", err);
			return err;
		}
	}

	err = fidgen_write_port_pkgaddr(sd, port, port_base);
	if (err)
		return err;

	return 0;
}

static int activate_port(struct routing_topology *topo,
			 struct fsubdev *sd, u8 port)
{
	int err;
	u32 new_state;

	/* activate the port */
	err = ops_linkmgr_port_link_state_set(sd, port, IB_PORT_ACTIVE,
					      &new_state, NULL, NULL);
	if (err)
		dev_err(sd_dev(sd), "failed to set port state active: %d\n",
			err);

	return err;
}

static int fidgen_write_port_table(struct routing_topology *topo,
				   struct fsubdev *sd, u8 port)
{
	u32 addr = get_raw_port_base(port) + CSR_FIDGEN_LUT_BASE;
	u8 *data = sd->routing.fidgen ? (u8 *)sd->routing.fidgen->map : NULL;
	u8 *data_next = (u8 *)sd->routing.fidgen_next->map;
	u32 remaining = (topo->max_dpa_index + 1) * sizeof(u64);
	u32 len;
	int err;

	while (remaining) {
		len = min_t(u32, remaining, MBOX_WRITE_DATA_SIZE_IN_BYTES);

		if (!data || memcmp(data, data_next, len)) {
			err = ops_csr_raw_write(sd, addr, data_next, len,
						NULL, NULL, false);
			if (err) {
				dev_err(sd_dev(sd),
					"failed to write fidgen top csrs: %d\n",
					err);
				return err;
			}
		}

		addr += len;
		data_next += len;
		remaining -= len;

		if (data)
			data += len;
	}

	return 0;
}

static int bridge_write(struct routing_topology *topo, struct fsubdev *sd)
{
	int err;
	u8 lpn;

	if (!sd->routing.fidgen_next) {
		sd_err(sd, "invalid fidgen");
		return -EINVAL;
	}

	for_each_bridge_lpn(lpn, sd) {
		err = fidgen_write_port(topo, sd, lpn);
		if (err)
			return err;

		err = fidgen_write_port_table(topo, sd, lpn);
		if (err)
			return err;

		err = activate_port(topo, sd, lpn);
		if (err)
			return err;
	}

	return 0;
}

static int switchinfo_write(struct fsubdev *sd)
{
	struct mbdb_op_switchinfo si = sd->switchinfo;
	int err;

	si.lft_top = ROUTING_UFT_SIZE - 1;
	err = ops_switchinfo_set(sd, &si, &si, NULL, NULL, false);
	if (err) {
		sd_err(sd, "failed to set switchinfo: %d\n", err);
		return err;
	}

	sd->switchinfo = si;

	return 0;
}

static void io_work_fn(void *ctx)
{
	struct fsubdev *sd = ctx;
	int err;

	err = switchinfo_write(sd);
	if (err)
		goto err;

	err = rpipe_write(sd);
	if (err)
		goto err;

	err = bridge_write(sd->routing.topo, sd);
	if (err)
		goto err;

	routing_update(sd);
	return;

err:
	routing_sd_transition_error(sd);
}

int routing_io_run(struct routing_topology *topo)
{
	struct routing_pause_ctx *quiesce_ctx;
	struct par_group group;
	struct fsubdev *sd;

	quiesce_ctx = routing_pause_init();
	if (!quiesce_ctx) {
		pr_err("unable to initialize quiesce context; abandoning fabric programming\n");
		return -EIO;
	}

	routing_pause_start(quiesce_ctx);

	par_start(&group);

	for (sd = routing_sd_iter(0); sd; sd = routing_sd_next(sd, 0))
		par_work_queue(&group, io_work_fn, sd);

	par_wait(&group);

	routing_pause_end(quiesce_ctx);

	return 0;
}
