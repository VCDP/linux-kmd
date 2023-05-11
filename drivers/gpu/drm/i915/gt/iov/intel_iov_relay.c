// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "gt/intel_gt.h"
#include "intel_iov.h"
#include "intel_iov_abi.h"
#include "intel_iov_relay.h"
#include "intel_iov_service.h"
#include "intel_iov_utils.h"
#include "intel_runtime_pm.h"
#include "i915_drv.h"
#include "i915_gem.h"

static struct intel_iov *relay_to_iov(struct intel_iov_relay *relay)
{
	return container_of(relay, struct intel_iov, relay);
}

static struct intel_gt *relay_to_gt(struct intel_iov_relay *relay)
{
	return iov_to_gt(relay_to_iov(relay));
}

static struct intel_guc *relay_to_guc(struct intel_iov_relay *relay)
{
	return &relay_to_gt(relay)->uc.guc;
}

static struct drm_i915_private *relay_to_i915(struct intel_iov_relay *relay)
{
	return relay_to_gt(relay)->i915;
}

static struct device *relay_to_dev(struct intel_iov_relay *relay)
{
	return relay_to_i915(relay)->drm.dev;
}

#ifdef CPTCFG_DRM_I915_DEBUG_GUC
#define RELAY_DEBUG(_r, ...) \
	DRM_DEV_DEBUG_DRIVER(relay_to_dev(_r), "RELAY: " __VA_ARGS__)
#define RELAY_ERROR(_r, ...) \
	DRM_DEV_ERROR(relay_to_dev(_r), "RELAY: " __VA_ARGS__)
#else
#define RELAY_DEBUG(...)
#define RELAY_ERROR(_r, ...) \
	dev_notice(relay_to_dev(_r), "RELAY: " __VA_ARGS__)
#endif

/* How long should we wait for the response? */
#define RELAY_TIMEOUT			1000 /* ms */

/**
 * DOC: HOW RELAY WORKS (REQUEST)
 *
 * To send request message to PF (VF only) or VF (PF only) one should use
 * intel_relay_send_req() function and provide request code and optional
 * request payload::
 *
 *               +----------- - -----------+
 *        target |//////request data///////|
 *        & code |/////////////////////////|
 *               +----------- - -----------+
 *
 * We convert above data into H2G format acceptable by intel_guc_send() where
 * we can use H2G INTEL_GUC_ACTION_RELAY_SERVICE_REQUEST action code::
 *
 *      +========+--------+--------+--------+----------- - -----------+
 *      | H2G    | target | relay  | relay  |//////request data///////|
 *      | action |   VF   | fence1 | header |/////////////////////////|
 *      +========+--------+--------+--------+----------- - -----------+
 *                        <----------- RELAY OPAQUE PAYLOAD ---------->
 *
 * Above H2G data be sent to the GuC over H2G CT buffer as::
 *
 *      +========+========+--------+--------+--------+----------- - -----------+
 *      | H2G    | CT     | target | relay  | sub-   |//////request data///////|
 *      | header | fence2 |   VF   | fence1 | header |/////////////////////////|
 *      +========+========+--------+--------+--------+----------- - -----------+
 *                                 <----------- RELAY OPAQUE PAYLOAD ---------->
 *
 * That H2G request message will be acked by the GuC with::
 *
 *      +========+========+--------+
 *      | G2H    | CT     | status |
 *      | header | fence2 |  code  |
 *      +========+========+--------+
 *
 * Then GuC will forward opaque RELAY payload to the specified target VF using
 * new G2H INTEL_GUC_ACTION_RELAY_SERVICE_NOTIFICATION message::
 *
 *      +========+--------+--------+--------+----------- - -----------+
 *      | G2H    | origin | relay  | sub-   |//////request data///////|
 *      | header |   VF   | fence1 | header |/////////////////////////|
 *      +========+--------+--------+--------+----------- - -----------+
 *                        <----------- RELAY OPAQUE PAYLOAD ---------->
 *
 * Actual relay request handler will be called by our framework with origin VF,
 * fence and request code extracted from above message followed by the original
 * request payload data::
 *
 *      +----------- - -----------+
 *      |//////request data///////|
 *      |/////////////////////////|
 *      +----------- - -----------+
 */

/**
 * DOC: HOW RELAY WORKS (RESPONSE)
 *
 * To send success response message back to PF (VF only) or VF (PF only) one
 * should use intel_iov_relay_send_response() function. Providing request fence
 * is mandatory, only response payload is optional::
 *
 *               +----------- - -----------+
 *        target |\\\\\\response data\\\\\\|
 *       & fence |\\\\\\\\\\\\\\\\\\\\\\\\\|
 *               +----------- - -----------+
 *
 * We convert above into H2G data format acceptable by intel_guc_send()
 * where we can use INTEL_GUC_ACTION_RELAY_SERVICE_REQUEST action code::
 *
 *      +========+--------+--------+--------+----------- - -----------+
 *      | H2G    | target | relay  | relay  |\\\\\\response data\\\\\\|
 *      | action |   VF   | fence1 | header |\\\\\\\\\\\\\\\\\\\\\\\\\|
 *      +========+--------+--------+--------+----------- - -----------+
 *                        <----------- RELAY OPAQUE PAYLOAD ---------->
 *
 * Above H2G data be sent to the GuC over H2G CT buffer as::
 *
 *      +========+========+--------+--------+--------+----------- - -----------+
 *      | H2G    | CT     | target | relay  | sub-   |\\\\\\response data\\\\\\|
 *      | header | fence3 |   VF   | fence1 | header |\\\\\\\\\\\\\\\\\\\\\\\\\|
 *      +========+========+--------+--------+--------+----------- - -----------+
 *                                 <----------- RELAY OPAQUE PAYLOAD ---------->
 *
 * That H2G request message will be acked by the GuC with::
 *
 *      +========+========+--------+
 *      | G2H    | CT     | status |
 *      | header | fence3 |  code  |
 *      +========+========+--------+
 *
 * Then GuC will forward opaque RELAY payload to the specified target VF using
 * new G2H INTEL_GUC_ACTION_RELAY_SERVICE_NOTIFICATION message::
 *
 *      +========+--------+--------+--------+----------- - -----------+
 *      | G2H    | origin | relay  | sub-   |\\\\\\response data\\\\\\|
 *      | header |   VF   | fence1 | header |\\\\\\\\\\\\\\\\\\\\\\\\\|
 *      +========+--------+--------+--------+----------- - -----------+
 *                        <----------- RELAY OPAQUE PAYLOAD ---------->
 *
 * Actual relay response handler will be called by our framework with response
 * code extracted from that message followed by optional payload data::
 *
 *      +----------- - -----------+
 *      |\\\\\\response data\\\\\\|
 *      |\\\\\\\\\\\\\\\\\\\\\\\\\|
 *      +----------- - -----------+
 *
 * That response data will be copied directly to the buffer provided by the
 * caller that initiated RELAY request sequence.
 */

static u32 relay_get_next_fence(struct intel_iov_relay *relay)
{
	u32 fence;

	spin_lock(&relay->lock);
	fence = ++relay->last_fence;
	if (unlikely(!fence))
		fence = relay->last_fence = 1;
	spin_unlock(&relay->lock);
	return fence;
}

struct pending_relay {
	struct list_head link;
	struct completion done;
	u32 fence;
	u32 status;
	u32 *response; /* can't be null */
	u32 response_size;
};

static int __relay_send(struct intel_iov_relay *relay, u32 target, u32 fence,
			u32 type, u32 code, const u32 *data, u32 data_len)
{
	u32 action[GUC_CT_MSG_LEN_MASK+1] = {
		[0] = INTEL_GUC_ACTION_RELAY_SERVICE_REQUEST,
		[1] = FIELD_PREP(GUC_RELAY_SRV_REQ_0_TARGET, target),
		[2] = FIELD_PREP(RELAY_DATA_0_MESSAGE_FENCE, fence),
		[3] = FIELD_PREP(RELAY_DATA_1_MESSAGE_TYPE, type) |
		      FIELD_PREP(RELAY_DATA_1_MESSAGE_CODE, code),
	};
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(relay_to_i915(relay)) &&
		   !IS_SRIOV_VF(relay_to_i915(relay)));
	GEM_BUG_ON(target && !IS_SRIOV_PF(relay_to_i915(relay)));
	GEM_BUG_ON(!FIELD_FIT(RELAY_DATA_1_MESSAGE_CODE, code));
	GEM_BUG_ON(data_len + 4 > GUC_CT_MSG_LEN_MASK);

	memcpy(&action[4], data, 4 * data_len);

	err = intel_guc_send(relay_to_guc(relay), action, 4 + data_len);
	if (unlikely(err < 0))
		RELAY_ERROR(relay, "Failed to send %u/%u %#x:%#x %*ph (%d)\n",
			    target, fence, type, code, 4 * data_len, data, err);
	return err;
}

/**
 * intel_iov_relay_send_response - Send response message to VF/PF.
 * @relay: the Relay struct
 * @target: target VF number (0 means PF)
 * @fence: relay fence (must match fence from the request)
 * @data: response data (can't be NULL if data_len is not 0)
 * @data_len: length of the response data (in dwords, can be 0)
 *
 * This function will encode provided parameters into RELAY message
 * and send it to the GuC to relay to final destination (target VF).
 *
 * This function can only be used if driver is running in SR-IOV mode.
 * Note that only PF driver can specify VF as a target (target != 0).
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_relay_send_response(struct intel_iov_relay *relay, u32 target,
				  u32 fence, const u32 *data, u32 data_len)
{
	RELAY_DEBUG(relay, "%u/%u sending reply %*ph\n",
		    target, fence, 4 * data_len, data);

	return __relay_send(relay, target, fence, RELAY_MESSAGE_TYPE_REPLY,
			    0 /* success */, data, data_len);
}

/**
 * intel_iov_relay_send_status - Send RELAY response message with status only.
 * @relay: the Relay struct
 * @target: target VF number (0 means PF)
 * @fence: relay fence
 * @code: status code (0 means success)
 *
 * This utility function will prepare RELAY response message with given error
 * code and send it to the GuC to relay to final destination (target VF)
 * without appending any additional payload data.
 *
 * This function can only be used if driver is running in SR-IOV mode.
 * Note that only PF driver can specify VF as a target (target != 0).
 * Note that PF will never report actual error code.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_relay_send_status(struct intel_iov_relay *relay, u32 target,
				u32 fence, u32 code)
{
	RELAY_DEBUG(relay, "%u/%u sending status %#x\n", target, fence, code);

	/* XXX TBD if generic error codes will be allowed */
	if (code && IS_SRIOV_PF(relay_to_i915(relay)))
		code = 0xFFFF;

	return __relay_send(relay, target, fence, RELAY_MESSAGE_TYPE_ERROR,
			    code, NULL, 0);
}

/**
 * intel_iov_relay_send_request - Send request message to PF/VF.
 * @relay: the Relay struct
 * @target: target VF number (0 means PF)
 * @code: request sub-action code
 * @data: request payload data
 * @dat_len: length of the payload data (in dwords, can be 0)
 * @buf: placeholder for the response message
 * @buf_size: size of the response message placeholder (in dwords)
 *
 * This function will encode provided parameters into RELAY message and send
 * it to the GuC to relay payload to the final destination (PF or VF).
 * This function will wait #RELAY_TIMEOUT until GuC relays back message with
 * the response received from PF/VF.
 *
 * This function can only be used if driver is running in SR-IOV mode.
 * Note that only PF driver can specify VF as a target (target != 0).
 * VFs may only use PF as a target (target == 0).
 *
 * Return: Non-negative response length (in dwords) or
 *         a negative error code on failure.
 */
int intel_iov_relay_send_request(struct intel_iov_relay *relay, u32 target,
				 u32 code, const u32 *data, u32 data_len,
				 u32 *buf, u32 buf_size)
{
	unsigned long timeout = msecs_to_jiffies(PRESI_WAIT_MS_TIMEOUT(relay_to_i915(relay), RELAY_TIMEOUT));
	struct pending_relay pending;
	u32 fence;
	int ret;
	long n;

	fence = relay_get_next_fence(relay);
	RELAY_DEBUG(relay, "%u/%u sending request %#x %*ph\n",
		    target, fence, code, 4 * data_len, data);

	init_completion(&pending.done);
	pending.fence = fence;
	pending.status = ENOMSG;
	pending.response = buf;
	pending.response_size = buf_size;

	/* list ordering does not need to match fence ordering */
	spin_lock(&relay->lock);
	list_add_tail(&pending.link, &relay->pending_relays);
	spin_unlock(&relay->lock);

	ret = __relay_send(relay, target, fence,
			   RELAY_MESSAGE_TYPE_REQUEST,
			   code, data, data_len);
	if (unlikely(ret < 0))
		goto unlink;

	n = wait_for_completion_timeout(&pending.done, timeout);
	RELAY_DEBUG(relay, "%u/%u wait n=%ld\n", target, fence, n);
	if (unlikely(n == 0)) {
		ret = -ETIME;
		goto unlink;
	}

	RELAY_DEBUG(relay, "%u/%u status=%#x\n", target, fence, pending.status);
	if (unlikely(pending.status)) {
		ret = -pending.status;
		goto unlink;
	}

	GEM_BUG_ON(pending.response_size > buf_size);
	ret = pending.response_size;
	RELAY_DEBUG(relay, "%u/%u response %*ph\n", target, fence, 4*ret, buf);

unlink:
	spin_lock(&relay->lock);
	list_del(&pending.link);
	spin_unlock(&relay->lock);

	if (unlikely(ret < 0)) {
		RELAY_ERROR(relay, "Failed to send request %#x to %s%u (%d)\n",
			    code, target ? "VF" : "PF", target, ret);
	}

	return ret;
}

static int relay_handle_response(struct intel_iov_relay *relay,
				 u32 fence, u32 status,
				 const u32 *payload, u32 len)
{
	struct pending_relay *pending;
	int err = -ESRCH;

	spin_lock(&relay->lock);
	list_for_each_entry(pending, &relay->pending_relays, link) {
		if (pending->fence != fence) {
			RELAY_DEBUG(relay, "#%u still awaits response\n",
				    pending->fence);
			continue;
		}
		err = 0;
		if (status == 0) {
			if (unlikely(len > pending->response_size)) {
				status = ENOBUFS;
				err = -ENOBUFS;
			} else {
				memcpy(pending->response, payload, 4*len);
				pending->response_size = len;
			}
		}
		pending->status = status;
		complete_all(&pending->done);
		break;
	}
	spin_unlock(&relay->lock);

	return err;
}

static int relay_handle_request(struct intel_iov_relay *relay,
				u32 origin, u32 fence, u32 action,
				const u32 *payload, u32 len)
{
	struct intel_iov *iov = relay_to_iov(relay);
	int err = -EOPNOTSUPP;

	RELAY_DEBUG(relay, "%u/%u received request %#x %*ph\n",
		    origin, fence, action, 4 * len, payload);

	if (intel_iov_is_pf(iov))
		err = intel_iov_service_request_handler(iov, origin, fence,
							action, payload, len);

	if (unlikely(err < 0)) {
		RELAY_ERROR(relay, "Request %#x from %s%u/%u failed (%d)\n",
			    action, origin ? "VF" : "PF", origin, fence, err);
		intel_iov_relay_send_status(relay, origin, fence, -err);
	}

	return err;
}

/**
 * intel_iov_relay_process_msg - Handle RELAY message from the GuC.
 * @relay: the Relay struct
 * @msg: message to be handled
 * @len: length of the message (in dwords)
 *
 * This function will handle RELAY messages received from the GuC.
 * Received RELAY messages can be either requests or responses.
 * For requests we will grab wakeref as it will be needed for a reply.
 *
 * This function can only be used if driver is running in SR-IOV mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_relay_process_msg(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	struct drm_i915_private *i915 = relay_to_i915(relay);
	u32 origin, fence, type, code;
	intel_wakeref_t wakeref;
	int err = -EPROTO;

	if (unlikely(!IS_SRIOV_PF(i915) && !IS_SRIOV_VF(i915)))
		return -EPERM;
	if (unlikely(len < 3))
		return -EPROTO;

	origin = FIELD_GET(GUC_RELAY_NOTIF_REQ_0_ORIGIN, msg[0]);
	fence = FIELD_GET(RELAY_DATA_0_MESSAGE_FENCE, msg[1]);
	type = FIELD_GET(RELAY_DATA_1_MESSAGE_TYPE, msg[2]);
	code = FIELD_GET(RELAY_DATA_1_MESSAGE_CODE, msg[2]);

	if (type == RELAY_MESSAGE_TYPE_REQUEST) {
		with_intel_runtime_pm(&i915->runtime_pm, wakeref)
			err = relay_handle_request(relay, origin, fence, code,
						   msg + 3, len - 3);
	} else if (type == RELAY_MESSAGE_TYPE_REPLY ||
		   type == RELAY_MESSAGE_TYPE_ERROR) {
		err = relay_handle_response(relay, fence, code,
					    msg + 3, len - 3);
	}

	return err;
}
