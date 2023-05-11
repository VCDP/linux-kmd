// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#include "selftests/routing_mock.h"
#include "routing_engine.h"
#include "routing_logic.h"
#include "routing_p2p.h"

struct spec {
	u16 num_devices;
	u8 num_subdevs;
};

static void routing_mock_populate_a21_spec(struct spec *spec)
{
	spec->num_devices = 6;
	spec->num_subdevs = 2;
}

static void init_port_maps(struct fsubdev *sd)
{
	u8 lpn;

	for (lpn = PORT_FABRIC_START; lpn <= PORT_FABRIC_END; ++lpn) {
		sd->lpn_fport_map[lpn] = sd->port + lpn - 1;
		__set_bit(lpn, sd->fport_lpns);
	}

	for (lpn = PORT_BRIDGE_START; lpn <= PORT_BRIDGE_END; ++lpn)
		__set_bit(lpn, sd->bport_lpns);
}

static int routing_mock_sd(struct spec *spec,
			   struct routing_topology *topo,
			   struct fdev *dev, int dev_id,
			   struct fsubdev *sd, int sd_index)
{
	struct portinfo *pi;
	u8 i, lpn;

	WARN_ON(dev_id >= spec->num_devices);
	WARN_ON(sd_index >= spec->num_subdevs);

	sd->id = build_sd_id(dev_id, sd_index);
	sd->fdev = dev;
	sd->port_cnt = PORT_FABRIC_COUNT;
	sd->guid = (0xffull << 56) | sd->id;
	sd->switchinfo.guid = sd->guid;

	init_port_maps(sd);

	routing_sd_init(sd);

	list_add_tail(&sd->routable_link, &routable_list);

	for (i = 0; i < PORT_FABRIC_COUNT; ++i) {
		lpn = i + 1;

		pi = &sd->portinfo.per_portinfo[lpn];
		pi->link_speed_active = LINK_SPEED_100G;
		pi->link_width_active = LINK_WIDTH_4X;
		pi->link_width_downgrade_rx_active = LINK_WIDTH_4X;
		pi->link_width_downgrade_tx_active = LINK_WIDTH_4X;

		sd->port[i].sd = sd;
		sd->port[i].lpn = lpn;
		sd->port[i].portinfo = pi;
		set_bit(PORT_CONTROL_ROUTABLE, sd->port[i].controls);
		if (i < spec->num_devices - 1) {
			/* initialize first N-1 ports as ISLs */
			sd->port[i].state = PM_PORT_STATE_ACTIVE;
			sd->port[i].phys_state = IB_PORT_PHYS_LINKUP;
			sd->port[i].log_state = IB_PORT_ACTIVE;
		} else {
			/* rest of ports are offline */
			sd->port[i].state = PM_PORT_STATE_DISABLED;
			sd->port[i].phys_state = IB_PORT_PHYS_DISABLED;
			sd->port[i].log_state = IB_PORT_DOWN;
		}
	}

	return 0;
}

static int routing_mock_device(struct spec *spec,
			       struct routing_topology *topo,
			       struct fdev *dev, int dev_id)
{
	int sd_index;
	int err;

	WARN_ON(dev_id >= spec->num_devices);

	for (sd_index = 0; sd_index < spec->num_subdevs; ++sd_index) {
		err = routing_mock_sd(spec, topo, dev, dev_id,
				      &dev->sd[sd_index], sd_index);
		if (err)
			return err;
	}

	return 0;
}

struct port_vec {
	struct fdev *dev;
	int sd;
	u8 lpn;
};

static void routing_mock_interconnect_port(struct spec *spec,
					   struct routing_topology *topo,
					   struct port_vec *src)
{
	struct port_vec dst;
	struct portinfo *pi_src;

	/*
	 * target device is chosen by contiguous assignment toward increasing
	 * indices (port 1 to src.d+1, port 2 to src.d+2, etc.)
	 */
	dst.dev = fdev_find((src->dev->pd->index + src->lpn) %
			    spec->num_devices);

	/* target sd is always the same as the source sd */
	dst.sd = src->sd;

	/*
	 * target port uses the same assignment rule, which ends up counting
	 * backwards relative to the source (1->5, 2->4, 3->3, etc.)
	 */
	dst.lpn = spec->num_devices - src->lpn;

	pi_src = src->dev->sd[src->sd].lpn_fport_map[src->lpn]->portinfo;
	pi_src->neighbor_guid = dst.dev->sd[dst.sd].switchinfo.guid;
	pi_src->neighbor_port_number = dst.lpn;

	fdev_put(dst.dev);
}

/*
 * Connects devices in an all-to-all topology.
 *
 * Each individual device assigns outbound port 1 to their first neighbor
 * to the right, then port 2 to their second neighbor to the right, and
 * so on.
 *
 * So given N devices, the jth port of the ith device goes to device
 * ((i + j) % N).
 */
static void routing_mock_interconnect(struct spec *spec,
				      struct routing_topology *topo)
{
	struct port_vec src;
	int dev_id;

	for (dev_id = 0; dev_id < spec->num_devices; ++dev_id) {
		src.dev = fdev_find(dev_id);
		for (src.sd = 0; src.sd < spec->num_subdevs; ++src.sd)
			for (src.lpn = PORT_FABRIC_START;
			     src.lpn < spec->num_devices; ++src.lpn)
				routing_mock_interconnect_port(spec, topo,
							       &src);
		fdev_put(src.dev);
	}
}

static struct iaf_pdata *routing_mock_pd_create(struct spec *spec, int dev_id)
{
	struct iaf_pdata *pd;
	u64 dpa_per_sd;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	pd->index = dev_id;
	pd->sd_cnt = spec->num_subdevs;

	/* initially determine maximum DPA per SD... */
	dpa_per_sd = ROUTING_DEV_DPA_ALIGNMENT / pd->sd_cnt;

	/*
	 * ...then use as much memory as available to exercise logic for
	 * multiple dpa->fid lut entries, but reduced by one so that we also
	 * have to deal with holes
	 */
	dpa_per_sd = max(ROUTING_MIN_DPA_PER_SD,
			 dpa_per_sd - ROUTING_MIN_DPA_PER_SD);

	pd->dpa.pkg_offset = dev_id * ROUTING_DEV_DPA_ALIGNMENT / SZ_1G;
	pd->dpa.pkg_size = dpa_per_sd * pd->sd_cnt / SZ_1G;

	return pd;
}

int routing_mock_create_topology(struct routing_topology *topo)
{
	struct spec spec;
	struct fdev *dev;
	int dev_id;
	int err;

	routing_mock_populate_a21_spec(&spec);

	if (spec.num_devices > ROUTING_MAX_DEVICES) {
		pr_err("%s: invalid topology spec: devices exceeds limit: %u > %u",
		       __func__, spec.num_devices, ROUTING_MAX_DEVICES);
		return -EINVAL;
	}

	memset(topo, 0, sizeof(*topo));
	INIT_LIST_HEAD(&topo->plane_list);

	for (dev_id = 0; dev_id < spec.num_devices; ++dev_id) {
		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (!dev)
			return -ENOMEM;

		dev->pd = routing_mock_pd_create(&spec, dev_id);
		if (IS_ERR(dev->pd)) {
			err = PTR_ERR(dev->pd);
			kfree(dev);
			return err;
		}

		err = routing_mock_device(&spec, topo, dev, dev_id);
		if (err) {
			kfree(dev->pd);
			kfree(dev);
			return err;
		}

		dev->fabric_id = dev->pd->index;
		err = fdev_insert(dev);
		if (err) {
			kfree(dev->pd);
			kfree(dev);
			return err;
		}
	}

	routing_mock_interconnect(&spec, topo);

	return 0;
}

static int routing_mock_destroy_cb(struct fdev *dev, void *args)
{
	struct fsubdev *sd;
	int j;

	for (j = 0; j < dev->pd->sd_cnt; ++j) {
		sd = &dev->sd[j];
		list_del(&sd->routable_link);
		routing_sd_destroy(sd);
	}

	fdev_put(dev);
	routing_p2p_clear(dev);
	kfree(dev->pd);
	kfree(dev);

	return 0;
}

void routing_mock_destroy(struct routing_topology *topo)
{
	struct routing_plane *plane, *plane_tmp;

	fdev_process_each(routing_mock_destroy_cb, NULL);

	list_for_each_entry_safe(plane, plane_tmp, &topo->plane_list, topo_link)
		routing_plane_destroy(plane);
}
