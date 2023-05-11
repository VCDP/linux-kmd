/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2020 Intel Corporation.
 *
 */

#ifndef IAF_CSR_H_INCLUDED
#define IAF_CSR_H_INCLUDED

#include <linux/types.h>

#define PORT_COUNT            13
#define PORT_MASK         0x1fff
#define PORT_EXT_MASK     0x1ffe /* external ports only */

#define PORT_CPORT_COUNT       1
#define PORT_CPORT_START       0
#define PORT_CPORT_END         0
#define PORT_CPORT_MASK   0x0001

#define PORT_FABRIC_COUNT      8
#define PORT_FABRIC_START      1
#define PORT_FABRIC_END        8
#define PORT_FABRIC_MASK  0x01fe

#define PORT_BRIDGE_COUNT      4
#define PORT_BRIDGE_START      9
#define PORT_BRIDGE_END       12
#define PORT_BRIDGE_MASK  0x1e00

#define CSR_PORTS_BASE 0x10000000

/*
 * Each port is at a 4MB offset from CSR_PORTS_BASE, index by physical port
 * number.
 */
#define CSR_PORT_OFFSET 0x400000

/*
 * Fabric ports are in memory region 0, while bridge ports are in region 1.
 *
 * When referencing bridge port addresses, both when using the raw CSR ops
 * as well as the LINK_MGR_PORT_CSR ops, you must add this offset to
 * addresses.
 */
#define CSR_REGION1_OFFSET 0x20000000

static inline u32 get_raw_port_base(u8 port)
{
	u32 region = port >= PORT_BRIDGE_START && port <= PORT_BRIDGE_END ?
		     CSR_REGION1_OFFSET : 0;

	return CSR_PORTS_BASE + ((port) - 1) * CSR_PORT_OFFSET + region;
}

/* Link manager computes the base for us, sans the region offset. */
static inline u32 get_link_mgr_port_base(u8 port)
{
	u32 region = port >= PORT_BRIDGE_START && port <= PORT_BRIDGE_END ?
		     CSR_REGION1_OFFSET : 0;

	return region;
}

#define CSR_FIDGEN_BASE 0

#define CSR_FIDGEN_MASK_A     (CSR_FIDGEN_BASE + 0x00)
#define CSR_FIDGEN_SHIFT_A    (CSR_FIDGEN_BASE + 0x08)
#define CSR_FIDGEN_MASK_B     (CSR_FIDGEN_BASE + 0x10)
#define CSR_FIDGEN_SHIFT_B    (CSR_FIDGEN_BASE + 0x18)
#define CSR_FIDGEN_RSVD0      (CSR_FIDGEN_BASE + 0x20)
#define CSR_FIDGEN_RSVD1      (CSR_FIDGEN_BASE + 0x28)
#define CSR_FIDGEN_MASK_H     (CSR_FIDGEN_BASE + 0x30)
#define CSR_FIDGEN_SHIFT_H    (CSR_FIDGEN_BASE + 0x38)
#define CSR_FIDGEN_MODULO     (CSR_FIDGEN_BASE + 0x40)
#define CSR_FIDGEN_WHOAMI     (CSR_FIDGEN_BASE + 0x48)
#define CSR_FIDGEN_MASK_D     (CSR_FIDGEN_BASE + 0x50)
#define CSR_FIDGEN_STATIC_RND (CSR_FIDGEN_BASE + 0x58)

#define MASK_FIDGEN_MASK_D GENMASK_ULL(39, 0)

/*
 * These apply to all of the shift registers.  Bit 6 is the direction of
 * shift, while bits 5 through 0 are the amount to shift by.
 */
#define MASK_FIDGEN_SHIFT_BY    GENMASK(5, 0)
#define MASK_FIDGEN_SHIFT_RIGHT GENMASK(6, 6)

/*
 * The LUT contains 8192 64-bit registers, each mapping a lookup index
 * to a 20-bit DFID.
 */
#define CSR_FIDGEN_LUT_BASE (CSR_FIDGEN_BASE + 0x10000)

#define CSR_FIDGEN_LUT_INDEX_WIDTH 13

#define CSR_BRIDGE_TOP_BASE 0x30000

#define CSR_BT_CTX_TRACKER_CFG CSR_BRIDGE_TOP_BASE
#define MASK_BT_CTC_ENABLE_SRC          GENMASK(0, 0)
#define MASK_BT_CTC_ENABLE_DST          GENMASK(1, 1)
#define MASK_BT_CTC_DISABLE_TIMEOUT_SRC GENMASK(2, 2)
#define MASK_BT_CTC_DISABLE_TIMEOUT_DST GENMASK(3, 3)

#define CSR_BT_PORT_CTRL (CSR_BRIDGE_TOP_BASE + 0x08)
#define MASK_BT_POC_DROP_FBRC_REQ     GENMASK(0, 0)
#define MASK_BT_POC_DROP_FBRC_REQ_ERR GENMASK(1, 1)
#define MASK_BT_POC_DROP_MDFI_REQ     GENMASK(2, 2)
#define MASK_BT_POC_DROP_MDFI_REQ_ERR GENMASK(3, 3)
#define MASK_BT_POC_DROP_FBRC_RSP     GENMASK(4, 4)
#define MASK_BT_POC_DROP_MDFI_RSP     GENMASK(5, 5)

#define CSR_BT_OUTSTANDING_TX (CSR_BRIDGE_TOP_BASE + 0x10)
#define MASK_BT_OT_FBRC GENMASK(15,  0)
#define MASK_BT_OT_MDFI GENMASK(31, 16)

#define CSR_BT_FLUSH_CONTEXT (CSR_BRIDGE_TOP_BASE + 0x18)
#define MASK_BT_FC_INITIATE    GENMASK(0, 0)
#define MASK_BT_FC_IN_PROGRESS GENMASK(1, 1)

#define CSR_BT_PAUSE_CTRL (CSR_BRIDGE_TOP_BASE + 0x20)
#define MASK_BT_PAC_FBRC_REQ GENMASK(0, 0)
#define MASK_BT_PAC_MDFI_REQ GENMASK(1, 1)
#define MASK_BT_PAC_FBRC_RSP GENMASK(2, 2)
#define MASK_BT_PAC_MDFI_RSP GENMASK(3, 3)

#define CSR_BT_PKG_ADDR_RANGE (CSR_BRIDGE_TOP_BASE + 0x28)
#define MASK_BT_PAR_BASE  GENMASK(18, 1)
#define MASK_BT_PAR_RANGE GENMASK(29, 20)

/* csr encodes address bits [45:32] in register bits [13:0] */
#define BT_ADDR_RANGE_SHIFT 32

#define CSR_BT_VC2SC_MAP (CSR_BRIDGE_TOP_BASE + 0x50)

#define CSR_BT_TILE0_RANGE (CSR_BRIDGE_TOP_BASE + 0x58)
#define MASK_BT_T0_VALID GENMASK(0, 0)
#define MASK_BT_T0_BASE  GENMASK(7, 1)
#define MASK_BT_T0_RANGE GENMASK(14, 8)

#define CSR_BT_TILE1_RANGE (CSR_BRIDGE_TOP_BASE + 0x60)
#define MASK_BT_T1_VALID GENMASK(0, 0)
#define MASK_BT_T1_BASE  GENMASK(7, 1)
#define MASK_BT_T1_RANGE GENMASK(14, 8)

#endif /* IAF_CSR_H_INCLUDED */
