// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019-2020 Intel Corporation.
 *
 */

#include <linux/types.h>
#include <linux/netlink.h>
#include <net/genetlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>

#include "csr.h"
#include "iaf_drv.h"
#include "io.h"
#include "mbdb.h"
#include "netlink.h"
#include "ops.h"
#include "port.h"
#include "routing_engine.h"
#include "routing_event.h"
#include "trace.h"

#define ATTR_MAX (_ATTR_MAX - 1)
#define CMD_MAX (_CMD_MAX - 1)
#define CMD_OP_MAX (_CMD_OP_MAX - 1)

/*
 * Prefix definitions:
 * nla_ - netlink attribute
 * genl_ - generic netlink
 * nl_ - this modules names
 */

typedef int (*nl_process_op_cb_t)(struct sk_buff *msg, struct genl_info *info);

struct cmd_op_table {
	nl_process_op_cb_t func;
};

struct nl_device_enum_cb_args {
	struct sk_buff *msg;
	u16 entries;
};

static struct genl_family nl_iaf_family;
static struct cmd_op_table cmd_ops[];

static DEFINE_MUTEX(nl_lock);

static int nl_add_rsp_attrs(struct sk_buff *msg, struct nlattr *context,
			    s32 result, u8 rsp_type)
{
	if (nla_put_s32(msg, ATTR_CMD_OP_RESULT, result) ||
	    nla_put_u8(msg, ATTR_CMD_OP_RSP_TYPE, rsp_type)) {
		return -EMSGSIZE;
	}

	/* Client need not specify a context attribute */
	if (context)
		if (nla_put_u64_64bit(msg, ATTR_CMD_OP_CONTEXT,
				      nla_get_u64(context), ATTR_PAD))
			return -EMSGSIZE;

	return 0;
}

static int nl_process_op_req(struct genl_info *info, size_t msg_sz, u16 op,
			     nl_process_op_cb_t process_op_cb)
{
	struct sk_buff *msg;
	void *usrhdr;
	int ret;

	trace_nl_rsp(info->genlhdr->cmd, op, msg_sz, info->snd_seq);

	if (!process_op_cb)
		return -EINVAL;

	msg = genlmsg_new(msg_sz, GFP_NOWAIT);
	if (!msg)
		return -ENOMEM;

	usrhdr = genlmsg_put_reply(msg, info, &nl_iaf_family, 0,
				   info->genlhdr->cmd);
	if (!usrhdr) {
		nlmsg_free(msg);
		return -ENOMEM;
	}

	ret = process_op_cb(msg, info);
	if (ret) {
		nlmsg_free(msg);
		return ret;
	}

	genlmsg_end(msg, usrhdr);

	ret = genlmsg_reply(msg, info);
	if (ret) {
		pr_err("Unable to send response msg.\n");
		nlmsg_free(msg);
	}

	return ret;
}

static int nl_parse_query(struct sk_buff *msg, struct genl_info *info)
{
	u16 op;
	int ret = -EINVAL;

	mutex_lock(&nl_lock);

	if (!info->attrs[ATTR_CMD_OP]) {
		ret = -ENODATA;
		goto bail;
	}

	op = nla_get_u16(info->attrs[ATTR_CMD_OP]);

	trace_nl_req(info->genlhdr->cmd, op, info->nlhdr->nlmsg_len,
		     info->snd_seq);

	if (info->genlhdr->version != INTERFACE_VERSION)
		goto bail;

	if (op < _CMD_OP_MAX)
		ret = nl_process_op_req(info, NLMSG_GOODSIZE, op,
					cmd_ops[op].func);
	else
		pr_err("Invalid Command Operation\n");

bail:
	mutex_unlock(&nl_lock);
	return ret;
}

static struct fsubdev *nl_get_sd(struct genl_info *info)
{
	struct nlattr *fabric_id = info->attrs[ATTR_FABRIC_ID];
	struct nlattr *sd_index = info->attrs[ATTR_SD_INDEX];

	if (!fabric_id)
		return ERR_PTR(-ENODATA);

	if (!sd_index)
		return ERR_PTR(-ENODATA);

	return find_sd_id(nla_get_u32(fabric_id), nla_get_u8(sd_index));
}

static int nl_add_fabric_device_attrs(struct sk_buff *msg, struct fdev *dev)
{
	const struct iaf_pdata *pd = dev->pd;

	if (nla_put_string(msg, ATTR_DEV_NAME,
			   dev_name(&dev->pdev->dev)) ||
	    nla_put_string(msg, ATTR_PARENT_DEV_NAME,
			   dev_name(dev->pdev->dev.parent)) ||

	    nla_put_u8(msg, ATTR_PCI_SLOT_NUM, pd->slot) ||
	    nla_put_u8(msg, ATTR_SOCKET_ID, pd->socket_id) ||
	    nla_put_u8(msg, ATTR_VERSION, pd->version) ||
	    nla_put_u8(msg, ATTR_PRODUCT_TYPE, pd->product) ||
	    nla_put_u8(msg, ATTR_SUBDEVICE_COUNT, pd->sd_cnt))
		return -EMSGSIZE;

	return 0;
}

static int nl_add_fabric_ports(struct sk_buff *msg, struct fsubdev *sd)
{
	u8 lpn;

	for_each_fabric_lpn(lpn, sd) {
		struct nlattr *nested_attr;
		struct fport *port;

		nested_attr = nla_nest_start(msg, ATTR_FABRIC_PORT);
		if (!nested_attr)
			return -EMSGSIZE;

		port = get_fport_handle(sd, lpn);

		if (nla_put_u8(msg, ATTR_FABRIC_PORT_NUMBER, lpn) ||
		    nla_put_u8(msg, ATTR_FABRIC_PORT_TYPE, port->port_type)) {
			nla_nest_cancel(msg, nested_attr);
			return -EMSGSIZE;
		}

		nla_nest_end(msg, nested_attr);
	}

	return 0;
}

static int nl_add_bridge_ports(struct sk_buff *msg, struct fsubdev *sd)
{
	u8 lpn;

	for_each_bridge_lpn(lpn, sd)
		if (nla_put_u8(msg, ATTR_BRIDGE_PORT_NUMBER, lpn))
			return -EMSGSIZE;

	return 0;
}

static int nl_add_sub_device_attrs(struct sk_buff *msg, struct fsubdev *sd)
{
	if (nla_put_u16(msg, ATTR_ID, sd->id) ||
	    nla_put_u64_64bit(msg, ATTR_GUID, sd->guid, ATTR_PAD) ||
	    nla_put_u8(msg, ATTR_EXTENDED_PORT_COUNT, sd->extended_port_cnt) ||
	    nla_put_u8(msg, ATTR_FABRIC_PORT_COUNT, sd->port_cnt) ||
	    nla_put_u8(msg, ATTR_SWITCH_LIFETIME,
		       FIELD_GET(SLT_PSC_EP0_SWITCH_LIFETIME,
				 sd->switchinfo.slt_psc_ep0)) ||
	    nla_put_u8(msg, ATTR_ROUTING_MODE_SUPPORTED,
		       sd->switchinfo.routing_mode_supported) ||
	    nla_put_u8(msg, ATTR_ROUTING_MODE_ENABLED,
		       sd->switchinfo.routing_mode_enabled) ||
	    nla_put_u8(msg, ATTR_EHHANCED_PORT_0_PRESENT,
		       FIELD_GET(SLT_PSC_EP0_ENHANCED_PORT_0,
				 sd->switchinfo.slt_psc_ep0)))
		return -EMSGSIZE;

	if (nl_add_fabric_ports(msg, sd))
		return -EMSGSIZE;

	return nl_add_bridge_ports(msg, sd);
}

static int nl_process_device_enum_cb(struct fdev *dev, void *args)
{
	struct nl_device_enum_cb_args *device_enum_args = args;
	struct sk_buff *msg = device_enum_args->msg;

	struct nlattr *nested_attr =
		nla_nest_start(msg, ATTR_FABRIC_DEVICE);

	if (!nested_attr)
		return -EMSGSIZE;

	if (nla_put_u32(msg, ATTR_FABRIC_ID, dev->fabric_id)) {
		nla_nest_cancel(msg, nested_attr);
		return -EMSGSIZE;
	}

	if (nl_add_fabric_device_attrs(msg, dev)) {
		nla_nest_cancel(msg, nested_attr);
		return -EMSGSIZE;
	}

	nla_nest_end(msg, nested_attr);

	device_enum_args->entries++;

	return 0;
}

static int nl_process_device_enum(struct sk_buff *msg, struct genl_info *info)
{
	struct nlattr *context = info->attrs[ATTR_CMD_OP_CONTEXT];
	struct nl_device_enum_cb_args device_enum_args;
	int ret;

	device_enum_args.msg = msg;
	device_enum_args.entries = 0;

	/* Get each fdev from xarray and fill in nested information */
	ret = fdev_process_each(nl_process_device_enum_cb, &device_enum_args);
	if (ret)
		return ret;

	if (nla_put_u16(msg, ATTR_ENTRIES, device_enum_args.entries))
		return -EMSGSIZE;

	return nl_add_rsp_attrs(msg, context, 0, RESPONSE);
}

static int nl_port_bitmap(struct fsubdev *sd, struct genl_info *info,
			  unsigned long *port_mask)
{
	struct nlattr *nla;
	int remaining;

	nlmsg_for_each_attr(nla, info->nlhdr, GENL_HDRLEN, remaining)
		if (nla_type(nla) == ATTR_FABRIC_PORT_NUMBER) {
			u8 lpn = nla_get_u8(nla);

			if (!get_fport_handle(sd, lpn))
				return -EINVAL;

			set_bit(lpn, port_mask);
		}

	return 0;
}

static int nl_process_set_port_state(struct sk_buff *msg,
				     struct genl_info *info)
{
	struct nlattr *context = info->attrs[ATTR_CMD_OP_CONTEXT];
	struct fsubdev *sd;
	DECLARE_BITMAP(port_mask, PORT_COUNT) = {};
	int err;

	/* Get the sd to access */
	sd = nl_get_sd(info);
	if (IS_ERR(sd))
		return nl_add_rsp_attrs(msg, context, PTR_ERR(sd), RESPONSE);

	err = nl_port_bitmap(sd, info, port_mask);
	if (!err) {
		switch (nla_get_u16(info->attrs[ATTR_CMD_OP])) {
		case OP_PORT_ENABLE:
			err = enable_fports(sd, port_mask[0]);
			break;
		case OP_PORT_DISABLE:
			err = disable_fports(sd, port_mask[0]);
			break;
		case OP_PORT_USAGE_ENABLE:
			err = enable_usage_fports(sd, port_mask[0]);
			break;
		case OP_PORT_USAGE_DISABLE:
			err = disable_usage_fports(sd, port_mask[0]);
			break;
		default:
			err = -EINVAL;
			break;
		}
	}

	fdev_put(sd->fdev);

	return nl_add_rsp_attrs(msg, context, err, RESPONSE);
}

static int nl_set_port_beacon(struct sk_buff *msg, struct fsubdev *sd,
			      u8 lpn, bool enable)
{
	struct fport *port = get_fport_handle(sd, lpn);
	bool beacon_enabled = test_bit(PORT_CONTROL_BEACONING, port->controls);
	int err;

	if ((enable && beacon_enabled) || (!enable && !beacon_enabled))
		return 0;

	err = ops_linkmgr_port_beacon_set(sd, lpn, enable, NULL, NULL);
	if (err)
		return err;

	change_bit(PORT_CONTROL_BEACONING, port->controls);

	return 0;
}

static int nl_process_set_port_beacon_state(struct sk_buff *msg,
					    struct genl_info *info)
{
	struct nlattr *context = info->attrs[ATTR_CMD_OP_CONTEXT];
	bool enable = nla_get_u16(info->attrs[ATTR_CMD_OP]) ==
		OP_PORT_BEACON_ENABLE;
	struct fsubdev *sd;
	DECLARE_BITMAP(port_mask, PORT_COUNT) = {};
	int err;
	u8 lpn;

	/* Get the sd to access */
	sd = nl_get_sd(info);
	if (IS_ERR(sd))
		return nl_add_rsp_attrs(msg, context, PTR_ERR(sd), RESPONSE);

	err = nl_port_bitmap(sd, info, port_mask);
	if (!err) {
		/*
		 * When the caller does not specify any ports, all lpns are
		 * walked and it's corresponding control_bit setting is set to
		 * "enable" for that port's lpn.
		 * Otherwise, only lpns specified in the message are examined
		 * and set to "enable".
		 */
		if (bitmap_empty(port_mask, PORT_COUNT)) {
			for_each_fabric_lpn(lpn, sd) {
				err = nl_set_port_beacon(msg, sd, lpn, enable);
				if (err)
					break;
			}
		} else {
			for_each_set_bit(lpn, port_mask, PORT_COUNT) {
				err = nl_set_port_beacon(msg, sd, lpn, enable);
				if (err)
					break;
			}
		}
	}

	fdev_put(sd->fdev);

	return nl_add_rsp_attrs(msg, context, err, RESPONSE);
}

static int nl_add_enabled_state(struct sk_buff *msg, struct fsubdev *sd, u8 lpn,
				bool enabled)
{
	struct nlattr *nested_attr;

	/* The caller guarantees the lpn is a valid fport and won't be NULL */

	nested_attr = nla_nest_start(msg, ATTR_FABRIC_PORT);
	if (!nested_attr)
		return -EMSGSIZE;

	if (nla_put_u8(msg, ATTR_FABRIC_PORT_NUMBER, lpn) ||
	    nla_put_u8(msg, ATTR_ENABLED_STATE, enabled)) {
		nla_nest_cancel(msg, nested_attr);
		return -EMSGSIZE;
	}

	nla_nest_end(msg, nested_attr);

	return 0;
}

static int nl_process_query_port_control_state(struct sk_buff *msg,
					       struct genl_info *info)
{
	struct nlattr *context = info->attrs[ATTR_CMD_OP_CONTEXT];
	struct fsubdev *sd;
	u8 bit;
	DECLARE_BITMAP(port_mask, PORT_COUNT) = {};
	unsigned long *mask;
	int err;
	u8 lpn;

	/* Get the sd to access */
	sd = nl_get_sd(info);
	if (IS_ERR(sd))
		return nl_add_rsp_attrs(msg, context, PTR_ERR(sd), RESPONSE);

	err = nl_port_bitmap(sd, info, port_mask);
	if (err)
		goto exit;

	switch (nla_get_u16(info->attrs[ATTR_CMD_OP])) {
	case OP_PORT_STATE_QUERY:
		bit = PORT_CONTROL_ENABLED;
		break;
	case OP_PORT_USAGE_STATE_QUERY:
		bit = PORT_CONTROL_ROUTABLE;
		break;
	case OP_PORT_BEACON_STATE_QUERY:
		bit = PORT_CONTROL_BEACONING;
		break;
	default:
		err = -EINVAL;
		goto exit;
	}

	/*
	 * When the caller does not specify any ports, all lpns are
	 * walked and it's corresponding control_bit setting is added to
	 * the response for that port's lpn.
	 * Otherwise, only lpns specified in the message are examined
	 * and added to the response.
	 */
	if (bitmap_empty(port_mask, PORT_COUNT)) {
		for_each_fabric_lpn(lpn, sd) {
			mask = get_fport_handle(sd, lpn)->controls;

			err = nl_add_enabled_state(msg, sd, lpn,
						   test_bit(bit, mask));
			if (err)
				goto exit;
		}
	} else {
		for_each_set_bit(lpn, port_mask, PORT_COUNT) {
			mask = get_fport_handle(sd, lpn)->controls;

			err = nl_add_enabled_state(msg, sd, lpn,
						   test_bit(bit, mask));
			if (err)
				goto exit;
		}
	}

	err = nl_add_rsp_attrs(msg, context, err, RESPONSE);

exit:
	fdev_put(sd->fdev);
	return err;
}

static int nl_process_port_routed_query(struct sk_buff *msg,
					struct genl_info *info)
{
	struct nlattr *context = info->attrs[ATTR_CMD_OP_CONTEXT];
	struct fsubdev *sd;
	DECLARE_BITMAP(port_mask, PORT_COUNT) = {};
	DECLARE_BITMAP(usage_mask, PORT_COUNT) = {};
	u8 lpn;
	int err;

	/* Get the sd to access */
	sd = nl_get_sd(info);
	if (IS_ERR(sd))
		return nl_add_rsp_attrs(msg, context, PTR_ERR(sd), RESPONSE);

	err = nl_port_bitmap(sd, info, port_mask);
	if (!err) {
		routing_port_routed_query(sd, port_mask, usage_mask);

		for_each_set_bit(lpn, port_mask, PORT_COUNT) {
			err = nl_add_enabled_state(msg, sd, lpn,
						   test_bit(lpn, usage_mask));
			if (err)
				goto exit;
		}
	}

	err = nl_add_rsp_attrs(msg, context, err, RESPONSE);

exit:
	fdev_put(sd->fdev);
	return err;
}

static int nl_process_rem_request(struct sk_buff *msg, struct genl_info *info)
{
	struct nlattr *context = info->attrs[ATTR_CMD_OP_CONTEXT];

	rem_request();

	return nl_add_rsp_attrs(msg, context, 0, RESPONSE);
}

static int nl_process_routing_gen_query(struct sk_buff *msg,
					struct genl_info *info)
{
	struct nlattr *context = info->attrs[ATTR_CMD_OP_CONTEXT];
	u32 counter_start, counter_end;
	int err;

	routing_generation_read(&counter_start, &counter_end);

	err = nla_put_u32(msg, ATTR_ROUTING_GEN_START, counter_start);
	if (err)
		return -EMSGSIZE;

	err = nla_put_u32(msg, ATTR_ROUTING_GEN_END, counter_end);
	if (err)
		return -EMSGSIZE;

	return nl_add_rsp_attrs(msg, context, 0, RESPONSE);
}

static int nl_process_fabric_device_properties(struct sk_buff *msg,
					       struct genl_info *info)
{
	struct nlattr *context = info->attrs[ATTR_CMD_OP_CONTEXT];
	struct nlattr *fabric_id = info->attrs[ATTR_FABRIC_ID];
	struct fdev *dev;
	int err;

	if (!fabric_id)
		return -ENODATA;

	dev = fdev_find(nla_get_u32(fabric_id));
	if (!dev)
		return nl_add_rsp_attrs(msg, context, -ENXIO, RESPONSE);

	err = nl_add_fabric_device_attrs(msg, dev);

	fdev_put(dev);

	if (err)
		return err;

	return nl_add_rsp_attrs(msg, context, 0, RESPONSE);
}

static int nl_process_sub_device_properties_get(struct sk_buff *msg,
						struct genl_info *info)
{
	struct nlattr *context = info->attrs[ATTR_CMD_OP_CONTEXT];
	struct fsubdev *sd;
	int err;

	/* Get the sd to access */
	sd = nl_get_sd(info);
	if (IS_ERR(sd))
		return nl_add_rsp_attrs(msg, context, PTR_ERR(sd), RESPONSE);

	err = nl_add_sub_device_attrs(msg, sd);

	fdev_put(sd->fdev);

	if (err)
		return err;

	return nl_add_rsp_attrs(msg, context, 0, RESPONSE);
}

static struct nla_policy nl_iaf_policy[ATTR_MAX + 1] = {
	[ATTR_CMD_OP] = { .type = NLA_U16 },
	[ATTR_CMD_OP_CONTEXT] = { .type = NLA_U64 },
	[ATTR_CMD_OP_RESULT] = { .type = NLA_S32 },
	[ATTR_CMD_OP_RSP_TYPE] = { .type = NLA_U8 },

	[ATTR_FABRIC_ID] = { .type = NLA_U32 },
	[ATTR_SD_INDEX] = { .type = NLA_U8 },

	[ATTR_ENTRIES] = { .type = NLA_U16 },

	[ATTR_FABRIC_DEVICE] = { .type = NLA_NESTED },
	[ATTR_DEV_NAME] = { .type = NLA_NUL_STRING },
	[ATTR_PARENT_DEV_NAME] = { .type = NLA_NUL_STRING },
	[ATTR_PCI_SLOT_NUM] = { .type = NLA_U8 },
	[ATTR_SOCKET_ID] = { .type = NLA_U8 },
	[ATTR_SUBDEVICE_COUNT] = { .type = NLA_U8 },
	[ATTR_VERSION] = { .type = NLA_U8 },
	[ATTR_PRODUCT_TYPE] = { .type = NLA_U8 },

	[ATTR_SUB_DEVICE] = { .type = NLA_NESTED },

	[ATTR_ID] = { .type = NLA_U16 },
	[ATTR_GUID] = { .type = NLA_U64 },
	[ATTR_EXTENDED_PORT_COUNT] = { .type = NLA_U8 },
	[ATTR_FABRIC_PORT_COUNT] = { .type = NLA_U8 },
	[ATTR_SWITCH_LIFETIME] = { .type = NLA_U8 },
	[ATTR_ROUTING_MODE_SUPPORTED] = { .type = NLA_U8 },
	[ATTR_ROUTING_MODE_ENABLED] = { .type = NLA_U8 },
	[ATTR_EHHANCED_PORT_0_PRESENT] = { .type = NLA_U8 },

	[ATTR_FABRIC_PORT] = { .type = NLA_NESTED },

	[ATTR_FABRIC_PORT_NUMBER] = { .type = NLA_U8 },
	[ATTR_FABRIC_PORT_TYPE] = { .type = NLA_U8 },

	[ATTR_BRIDGE_PORT_NUMBER] = { .type = NLA_U8 },

	[ATTR_ENABLED_STATE] = { .type = NLA_U8 },

	[ATTR_ROUTING_GEN_START] = { .type = NLA_U32 },
	[ATTR_ROUTING_GEN_END] = { .type = NLA_U32 },
};

static struct genl_ops nl_iaf_cmds[CMD_MAX] = {
	{
		.cmd = CMD_L0_SMI,
		.doit = nl_parse_query,
		.flags = GENL_UNS_ADMIN_PERM,
	},
};

static struct genl_family nl_iaf_family = {
	.name = "iaf_ze",
	.version = INTERFACE_VERSION,
	.maxattr = ATTR_MAX,
	.ops = nl_iaf_cmds,
	.n_ops = ARRAY_SIZE(nl_iaf_cmds),
//	.policy = nl_iaf_policy,
	.parallel_ops = true,
};

/* For received requests "func" is executed based on the CMD_OP's index*/
static struct cmd_op_table cmd_ops[CMD_OP_MAX + 1] = {
	[OP_DEVICE_ENUM] = { .func = nl_process_device_enum, },

	[OP_PORT_ENABLE] = { .func = nl_process_set_port_state, },
	[OP_PORT_DISABLE] = { .func = nl_process_set_port_state, },
	[OP_PORT_USAGE_ENABLE] = { .func = nl_process_set_port_state, },
	[OP_PORT_USAGE_DISABLE] = { .func = nl_process_set_port_state, },

	[OP_PORT_BEACON_ENABLE] = {
		.func = nl_process_set_port_beacon_state, },
	[OP_PORT_BEACON_DISABLE] = {
		.func = nl_process_set_port_beacon_state, },

	[OP_PORT_STATE_QUERY] = {
		.func = nl_process_query_port_control_state, },
	[OP_PORT_USAGE_STATE_QUERY] = {
		.func = nl_process_query_port_control_state, },
	[OP_PORT_BEACON_STATE_QUERY] = {
		.func = nl_process_query_port_control_state, },

	[OP_PORT_ROUTED_QUERY] = { .func = nl_process_port_routed_query, },
	[OP_REM_REQUEST] = { .func = nl_process_rem_request, },
	[OP_ROUTING_GEN_QUERY] = { .func = nl_process_routing_gen_query, },
	[OP_FABRIC_DEVICE_PROPERTIES] = {
		.func = nl_process_fabric_device_properties, },
	[OP_SUB_DEVICE_PROPERTIES_GET] = {
		.func = nl_process_sub_device_properties_get, },
};

/**
 * nl_term - Unregisters the interface between kernel and user space.
 */
void nl_term(void)
{
	if (genl_unregister_family(&nl_iaf_family))
		pr_err("Unable to unregister family\n");
}

/**
 * nl_init - Registers the interface between kernel and user space.
 *
 * Return: Zero on success, -EIO when unsuccessful.
 */
int nl_init(void)
{
	if (genl_register_family(&nl_iaf_family)) {
		pr_err("Cannot register iaf family\n");
		return -EIO;
	}

	return 0;
}
