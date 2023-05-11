/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_ABI_H__
#define __INTEL_IOV_ABI_H__

/**
 * DOC: INTEL_GUC_ACTION_RELAY_SERVICE_REQUEST
 *
 * This H2G action is used to relay information between VF and PF
 * using GuC. This action is only supported over CTB.
 * Format of the relay data is opaque to GuC.
 *
 * Note: VFs must always use PF as a target (0).
 *
 * See INTEL_GUC_ACTION_RELAY_SERVICE_NOTIFICATION.
 *
 * H2G request message format::
 *      +===============================================+
 *      | CTB HEADER                                    |
 *      +===============================================+
 *      |   |       | action = 0x5000                   |
 *      |   |       | payload len = 2..n                |
 *      +===============================================+
 *      | CTB PAYLOAD                                   |
 *      +===============================================+
 *      | 0 |  31:3 | reserved = MBZ                    |
 *      |   |   2:0 | target = PF(0)/VF(1..num_vfs)     |
 *      +-----------------------------------------------+
 *      | 1 |  31:0 | relay data                        |
 *      |...|       | relay data                        |
 *      | n |       | relay data                        |
 *      +===============================================+
 * Response::
 *      +===============================================+
 *      | none                                          |
 *      +===============================================+
 */
#define GUC_RELAY_SRV_REQ_0_RESERVED		(0x3FFFFFF << 6)
#define GUC_RELAY_SRV_REQ_0_TARGET		(0x3F << 0)

/**
 * DOC: INTEL_GUC_ACTION_RELAY_SERVICE_NOTIFICATION
 *
 * This G2H action is used by GuC to deliver data relayed between
 * VF and PF. This action is only supported over CTB.
 *
 * See INTEL_GUC_ACTION_RELAY_SERVICE_REQUEST.
 *
 * G2H message format::
 *      +===============================================+
 *      | CTB HEADER                                    |
 *      +===============================================+
 *      |   |       | action = 0x5001                   |
 *      |   |       | payload len = 2..n                |
 *      +===============================================+
 *      | CTB PAYLOAD                                   |
 *      +===============================================+
 *      | 0 |  31:3 | reserved = MBZ                    |
 *      |   |   2:0 | origin = PF(0)/VF(1..num_vfs)     |
 *      +-----------------------------------------------+
 *      | 1 |  31:0 | relay data 0                      |
 *      |...|       | relay data ..                     |
 *      | n |       | relay data n-1                    |
 *      +===============================================+
 */
#define GUC_RELAY_NOTIF_REQ_0_RESERVED		(0x3FFFFFF << 6)
#define GUC_RELAY_NOTIF_REQ_0_ORIGIN		(0x3F << 0)

/**
 * DOC: RELAY MESSAGE FORMAT
 *
 * VF/PF messages are relayed by GuC as opaque "relay data".
 * See INTEL_GUC_ACTION_RELAY_SERVICE_REQUEST.
 * See INTEL_GUC_ACTION_RELAY_SERVICE_NOTIFICATION.
 *
 * Format of the relay message::
 *      +===============================================+
 *      | RELAY DATA                                    |
 *      +===============================================+
 *      |   | RELAY HEADER                              |
 *      +===============================================+
 *      | 0 |  31:0 | relay fence                       |
 *      +-----------------------------------------------+
 *      | 1 | 31:28 | type = REQUEST/RESPONSE           |
 *      |   | 27:16 | data = MBZ                        |
 *      |   |  15:0 | code = SUBACTION/STATUS           |
 *      +===============================================+
 *      |   | RELAY PAYLOAD                             |
 *      +===============================================+
 *      | 2 |       | relay payload 0                   |
 *      |...|       | relay payload ..                  |
 *      | n |       | relay payload n-2                 |
 *      +===============================================+
 */
#define RELAY_DATA_MIN_LEN			2u
#define RELAY_DATA_0_MESSAGE_FENCE		(0xFFFFFFFF << 0)
#define RELAY_DATA_1_MESSAGE_TYPE		(0xF << 28)
#define   RELAY_MESSAGE_TYPE_REQUEST		0x0
#define   RELAY_MESSAGE_TYPE_ONEWAY		0x1
#define   RELAY_MESSAGE_TYPE_ERROR		0xE
#define   RELAY_MESSAGE_TYPE_REPLY		0xF
#define RELAY_DATA_1_MBZ			(0xFFF << 16)
#define RELAY_DATA_1_MESSAGE_FENCE		(0xF << 24)
#define RELAY_DATA_1_MESSAGE_LEN		(0xF << 16)
#define RELAY_DATA_1_MESSAGE_CODE		(0xFFFF << 0)

/**
 * DOC: VFPF_ABI_VERSION
 *
 * VF/PF VF/PF API Version number rules are:
 *  * PF/VF are compatible if the version number matches.
 *  * VF/PF version shall be incremented on each change in the ABI.
 *  * Interface changes must be additive.
 *  * If a change in command format would break ABI compatibility
 *    a new command must be added instead.
 */
#define VFPF_ABI_VERSION_MAJOR			1
#define VFPF_ABI_VERSION_MINOR			0

/* List of supported VF/PF requests */
#define VFPF_GET_VERSION_REQUEST		0x0000
#define VFPF_GET_RUNTIME_REQUEST		0x0001
#define VFPF_PF_ACTION_REQUEST			0x0002

/**
 * DOC: VFPF_GET_VERSION_REQUEST
 *
 * Request::
 *      +===============================================+
 *      |   | RELAY PAYLOAD                             |
 *      +===============================================+
 *      |   | none                                      |
 *      +===============================================+
 *
 * Response::
 *      +===============================================+
 *      |   | RELAY PAYLOAD                             |
 *      +===============================================+
 *      | 0 | 31:27 | undefined                         |
 *      |   | 26:23 | major version number              |
 *      |   | 22:16 | minor version number              |
 *      |   |  15:0 | reserved                          |
 *      +===============================================+
 */
#define VFPF_GET_VERSION_REQ_LEN		0u
#define VFPF_GET_VERSION_RESP_LEN		1u
#define VFPF_GET_VERSION_RESP_0_MAJOR		(0xF << 23)
#define VFPF_GET_VERSION_RESP_0_MINOR		(0x3F << 16)

/**
 * DOC: VFPF_GET_RUNTIME_REQUEST
 *
 * Request::
 *      +===============================================+
 *      |   | RELAY PAYLOAD                             |
 *      +===============================================+
 *      | 0 |  31:0 | start index                       |
 *      +===============================================+
 *
 * Response::
 *      +===============================================+
 *      |   | RELAY PAYLOAD                             |
 *      +===============================================+
 *      | 0 |  31:0 | remaining entries                 |
 *      +-----------------------------------------------+
 *      | 1 |  31:0 | register[start+0] offset          |
 *      | 2 |  31:0 | register[start+0] value           |
 *      +-----------------------------------------------+
 *      | 3 |  31:0 | register[start+1] offset          |
 *      | 4 |  31:0 | register[start+1] value           |
 *      +-----------------------------------------------+
 *      |...|                                           |
 *      +-----------------------------------------------+
 *      |n-1|  31:0 | register[start+n/2-1] offset      |
 *      | n |  31:0 | register[start+n/2-1] value       |
 *      +===============================================+
 */
#define VFPF_GET_RUNTIME_REQ_LEN		1u
#define VFPF_GET_RUNTIME_REQ_0_START		(0xFFFFFFFF << 0)
#define VFPF_GET_RUNTIME_RESP_LEN_MIN		1u
#define VFPF_GET_RUNTIME_RESP_LEN_MAX		15u
#define VFPF_GET_RUNTIME_RESP_0_REMAINING	(0xFFFFFFFF << 0)
#define VFPF_GET_RUNTIME_RESP_1_OFFSET0		(0xFFFFFFFF << 0)
#define VFPF_GET_RUNTIME_RESP_2_VALUE0		(0xFFFFFFFF << 0)

/**
 * DOC: VFPF_PF_ACTION_REQUEST
 *
 * Request::
 *      +===============================================+
 *      |   | RELAY PAYLOAD                             |
 *      +===============================================+
 *      | 0 |  31:0 | PF action ID                      |
 *      +===============================================+
 *
 * Response::
 *      +===============================================+
 *      |   | RELAY PAYLOAD                             |
 *      +===============================================+
 *      | none                                          |
 *      +===============================================+
 */
#define VFPF_PF_ACTION_REQ_LEN			1u
#define VFPF_PF_ACTION_REQ_0_ID			(0xFFFFFFFF << 0)
#define   VFPF_PF_ACTION_GTCR			0
#define   VFPF_PF_ACTION_GFX_FLSH_CNT		1
#define VFPF_PF_ACTION_RESP_LEN			0u

/**
 * DOC: INTEL_GUC_ACTION_GET_INIT_DATA
 *
 * This H2G action is for VFs only and available over MMIO.
 *
 * Request::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = REQUEST(0)                 |
 *      |   | 27:16 | data = MBZ(0)                     |
 *      |   |  15:0 | code = GET_INIT_DATA(5B00)        |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | 0 | 31:24 | RESERVED                          |
 *      |   | 23:16 | Supported GuC version major       |
 *      |   |  15:8 | Supported GuC version minor       |
 *      |   |   7:0 | RESERVED                          |
 *      +===============================================+
 *
 * Response::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = RESPONSE(F)                |
 *      |   | 27:16 | data = MBZ(0)                     |
 *      |   |  15:0 | code = SUCCESS(0)                 |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | 0 | 31:24 | RESERVED                          |
 *      |   | 23:16 | GuC version major                 |
 *      |   |  15:8 | GuC version minor                 |
 *      |   |   7:0 | RESERVED                          |
 *      +---+-------+-----------------------------------+
 *      | 1 |  31:8 | RESERVED                          |
 *      |   |   7:0 | Tile Mask                         |
 *      +===============================================+
 */
#define GUC_GET_INIT_DATA_REQ_LEN			1u
#define GUC_GET_INIT_DATA_REQ_0_GUC_MAJOR		(0xFF << 16)
#define GUC_GET_INIT_DATA_REQ_0_GUC_MINOR		(0xFF << 8)
#define GUC_GET_INIT_DATA_RESP_LEN			2u
#define GUC_GET_INIT_DATA_RESP_0_GUC_MAJOR		(0xFF << 16)
#define GUC_GET_INIT_DATA_RESP_0_GUC_MINOR		(0xFF << 8)
#define GUC_GET_INIT_DATA_RESP_1_TILE_MASK		(0xFF << 0)

/**
 * DOC: INTEL_GUC_ACTION_GET_GGTT_INFO
 *
 * This H2G action is for VFs only and available over MMIO.
 *
 * Request::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = REQUEST(0)                 |
 *      |   | 27:16 | data = MBZ(0)                     |
 *      |   |  15:0 | code = GET_GGTT_INFO(5B02)        |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | none                                          |
 *      +===============================================+
 *
 * Response::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = RESPONSE(F)                |
 *      |   | 27:16 | data = MBZ(0)                     |
 *      |   |  15:0 | code = SUCCESS(0)                 |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | 0 | 31:16 | GGTT size [in 2M units]           |
 *      |   |  15:0 | GGTT base [in 2M units]           |
 *      +===============================================+
 */
#define GUC_GET_GGTT_INFO_REQ_LEN			0u
#define GUC_GET_GGTT_INFO_RESP_LEN			1u
#define GUC_GET_GGTT_INFO_RESP_0_GGTT_SIZE		(0xFFFF << 16)
#define GUC_GET_GGTT_INFO_RESP_0_GGTT_BASE		(0xFFFF << 0)

/**
 * DOC: INTEL_GUC_ACTION_GET_LMEM_INFO
 *
 * This action is for VFs use only and is only available over MMIO.
 *
 * Request::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = REQUEST(0)                 |
 *      |   | 27:16 | data = MBZ(0)                     |
 *      |   |  15:0 | code = GET_LMEM_INFO(5B03)        |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | none                                          |
 *      +===============================================+
 *
 * Response::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = RESPONSE(F)                |
 *      |   | 27:16 | data = MBZ(0)                     |
 *      |   |  15:0 | code = SUCCESS(0)                 |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | 0 |  31:0 | LMEM size [in 2M units]           |
 *      +-----------------------------------------------+
 *      | 1 |  31:0 | LMEM offset                       |
 *      +===============================================+
 */
#define GUC_GET_LMEM_INFO_REQ_LEN			0u
#define GUC_GET_LMEM_INFO_RESP_LEN			2u
#define GUC_GET_LMEM_INFO_RESP_0_LMEM_SIZE		(0xFFFFFFFF << 0)
#define GUC_GET_LMEM_INFO_RESP_1_TBD			(0xFFFFFFFF << 0)

/**
 * DOC: INTEL_GUC_ACTION_GET_SUBMISSION_CFG
 *
 * This H2G action is for VFs only and available over MMIO.
 *
 * Request::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = REQUEST(0)                 |
 *      |   | 27:16 | data = MBZ(0)                     |
 *      |   |  15:0 | code = GET_SUBMISSION_CFG(5B04)   |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | none                                          |
 *      +===============================================+
 *
 * Response::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = RESPONSE(F)                |
 *      |   | 27:16 | data = MBZ(0)                     |
 *      |   |  15:0 | code = SUCCESS(0)                 |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | 0 | 31:24 | reserved                          |
 *      |   |  23:8 | number of contexts                |
 *      |   |   7:0 | number of doorbells               |
 *      +===============================================+
 */
#define GUC_GET_SUBMISSION_CFG_REQ_LEN			0u
#define GUC_GET_SUBMISSION_CFG_RESP_LEN			1u
#define GUC_GET_SUBMISSION_CFG_RESP_0_NUM_CTXS		(0xFFFF << 8)
#define GUC_GET_SUBMISSION_CFG_RESP_0_NUM_DBS		(0xFF << 0)

/**
 * DOC: INTEL_GUC_ACTION_GET_RUNTIME_INFO
 *
 * This H2G action is for VFs only and available over MMIO.
 *
 * Request::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = REQUEST(0)                 |
 *      |   | 27:16 | data = MBZ(0)                     |
 *      |   |  15:0 | code = GET_RUNTIME_INFO(TBD)      |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | 0 |  31:0 | register1 offset to query (or 0)  |
 *      +-----------------------------------------------+
 *      | 1 |  31:0 | register2 offset to query (or 0)  |
 *      +-----------------------------------------------+
 *      | 2 |  31:0 | register3 offset to query (or 0)  |
 *      +===============================================+
 *
 * Response::
 *      header
 *      +===============================================+
 *      | 0 | 31:28 | type = RESPONSE(F)                |
 *      |   |  27:0 | data = MBZ(0)                     |
 *      |   |  15:0 | data = MBZ(0)                     |
 *      +===============================================+
 *      payload
 *      +===============================================+
 *      | 0 |  31:0 | register1 value (0 if no offset)  |
 *      +-----------------------------------------------+
 *      | 1 |  31:0 | register2 value (0 if no offset)  |
 *      +-----------------------------------------------+
 *      | 2 |  31:0 | register3 value (0 if no offset)  |
 *      +===============================================+
 */
#define GUC_GET_RUNTIME_INFO_REQ_LEN			3u
#define GUC_GET_RUNTIME_INFO_REQ_0_OFFSET1		(0xFFFFFFFF << 0)
#define GUC_GET_RUNTIME_INFO_REQ_1_OFFSET2		(0xFFFFFFFF << 0)
#define GUC_GET_RUNTIME_INFO_REQ_2_OFFSET3		(0xFFFFFFFF << 0)
#define GUC_GET_RUNTIME_INFO_RESP_LEN			3u
#define GUC_GET_RUNTIME_INFO_RESP_0_VALUE1		(0xFFFFFFFF << 0)
#define GUC_GET_RUNTIME_INFO_RESP_1_VALUE2		(0xFFFFFFFF << 0)
#define GUC_GET_RUNTIME_INFO_RESP_2_VALUE3		(0xFFFFFFFF << 0)

#endif /* __INTEL_IOV_ABI_H__ */
