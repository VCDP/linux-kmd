// SPDX-License-Identifier: GPL-2.0
/* Various workarounds for chipset bugs.
   This code runs very early and can't use the regular PCI subsystem
   The entries are keyed to PCI bridges which usually identify chipsets
   uniquely.
   This is only for whole classes of chipsets with specific problems which
   need early invasive action (e.g. before the timers are initialized).
   Most PCI device specific workarounds can be done later and should be
   in standard PCI quirks
   Mainboard specific bugs should be handled by DMI entries.
   CPU specific bugs in setup.c */

#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/pci_ids.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>
#include <linux/platform_data/x86/apple.h>
#include <drm/i915_drm.h>
#include <asm/pci-direct.h>
#include <asm/dma.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/hpet.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/irq_remapping.h>
#include <asm/early_ioremap.h>
#include <asm/pci-direct.h>
#include <asm/io.h>
#include <asm/pci_x86.h>

/* Direct PCI access. This is used for PCI accesses in early boot before
 *    the PCI subsystem works. */

u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset)
{
        u32 v;
        outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
        v = inl(0xcfc);
        return v;
}

u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset)
{
        u8 v;
        outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
        v = inb(0xcfc + (offset&3));
        return v;
}

u16 read_pci_config_16(u8 bus, u8 slot, u8 func, u8 offset)
{
        u16 v;
        outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
        v = inw(0xcfc + (offset&2));
        return v;
}

/*
 * Systems with Intel graphics controllers set aside memory exclusively
 * for gfx driver use.  This memory is not marked in the E820 as reserved
 * or as RAM, and so is subject to overlap from E820 manipulation later
 * in the boot process.  On some systems, MMIO space is allocated on top,
 * despite the efforts of the "RAM buffer" approach, which simply rounds
 * memory boundaries up to 64M to try to catch space that may decode
 * as RAM and so is not suitable for MMIO.
 */

#define KB(x)	((x) * 1024UL)
#define MB(x)	(KB (KB (x)))

static resource_size_t  i830_tseg_size(void)
{
	u8 esmramc = read_pci_config_byte(0, 0, 0, I830_ESMRAMC);

	if (!(esmramc & TSEG_ENABLE))
		return 0;

	if (esmramc & I830_TSEG_SIZE_1M)
		return MB(1);
	else
		return KB(512);
}

static resource_size_t  i845_tseg_size(void)
{
	u8 esmramc = read_pci_config_byte(0, 0, 0, I845_ESMRAMC);
	u8 tseg_size = esmramc & I845_TSEG_SIZE_MASK;

	if (!(esmramc & TSEG_ENABLE))
		return 0;

	switch (tseg_size) {
	case I845_TSEG_SIZE_512K:	return KB(512);
	case I845_TSEG_SIZE_1M:		return MB(1);
	default:
		WARN(1, "Unknown ESMRAMC value: %x!\n", esmramc);
	}
	return 0;
}

static resource_size_t  i85x_tseg_size(void)
{
	u8 esmramc = read_pci_config_byte(0, 0, 0, I85X_ESMRAMC);

	if (!(esmramc & TSEG_ENABLE))
		return 0;

	return MB(1);
}

static resource_size_t  i830_mem_size(void)
{
	return read_pci_config_byte(0, 0, 0, I830_DRB3) * MB(32);
}

static resource_size_t  i85x_mem_size(void)
{
	return read_pci_config_byte(0, 0, 1, I85X_DRB3) * MB(32);
}

/*
 * On 830/845/85x the stolen memory base isn't available in any
 * register. We need to calculate it as TOM-TSEG_SIZE-stolen_size.
 */
static resource_size_t  i830_stolen_base(int num, int slot, int func,
					       resource_size_t stolen_size)
{
	return i830_mem_size() - i830_tseg_size() - stolen_size;
}

static resource_size_t  i845_stolen_base(int num, int slot, int func,
					       resource_size_t stolen_size)
{
	return i830_mem_size() - i845_tseg_size() - stolen_size;
}

static resource_size_t  i85x_stolen_base(int num, int slot, int func,
					       resource_size_t stolen_size)
{
	return i85x_mem_size() - i85x_tseg_size() - stolen_size;
}

static resource_size_t  i865_stolen_base(int num, int slot, int func,
					       resource_size_t stolen_size)
{
	u16 toud = 0;

	toud = read_pci_config_16(0, 0, 0, I865_TOUD);

	return toud * KB(64) + i845_tseg_size();
}

static resource_size_t  gen3_stolen_base(int num, int slot, int func,
					       resource_size_t stolen_size)
{
	u32 bsm;

	/* Almost universally we can find the Graphics Base of Stolen Memory
	 * at register BSM (0x5c) in the igfx configuration space. On a few
	 * (desktop) machines this is also mirrored in the bridge device at
	 * different locations, or in the MCHBAR.
	 */
	bsm = read_pci_config(num, slot, func, INTEL_BSM);

	return bsm & INTEL_BSM_MASK;
}

static resource_size_t  gen11_stolen_base(int num, int slot, int func,
						resource_size_t stolen_size)
{
	u64 bsm;

	bsm = read_pci_config(num, slot, func, INTEL_GEN11_BSM_DW0);
	bsm &= INTEL_BSM_MASK;
	bsm |= (u64)read_pci_config(num, slot, func, INTEL_GEN11_BSM_DW1) << 32;

	return bsm;
}

static resource_size_t  i830_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;
	u16 gms;

	gmch_ctrl = read_pci_config_16(0, 0, 0, I830_GMCH_CTRL);
	gms = gmch_ctrl & I830_GMCH_GMS_MASK;

	switch (gms) {
	case I830_GMCH_GMS_STOLEN_512:	return KB(512);
	case I830_GMCH_GMS_STOLEN_1024:	return MB(1);
	case I830_GMCH_GMS_STOLEN_8192:	return MB(8);
	/* local memory isn't part of the normal address space */
	case I830_GMCH_GMS_LOCAL:	return 0;
	default:
		WARN(1, "Unknown GMCH_CTRL value: %x!\n", gmch_ctrl);
	}

	return 0;
}

static resource_size_t  gen3_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;
	u16 gms;

	gmch_ctrl = read_pci_config_16(0, 0, 0, I830_GMCH_CTRL);
	gms = gmch_ctrl & I855_GMCH_GMS_MASK;

	switch (gms) {
	case I855_GMCH_GMS_STOLEN_1M:	return MB(1);
	case I855_GMCH_GMS_STOLEN_4M:	return MB(4);
	case I855_GMCH_GMS_STOLEN_8M:	return MB(8);
	case I855_GMCH_GMS_STOLEN_16M:	return MB(16);
	case I855_GMCH_GMS_STOLEN_32M:	return MB(32);
	case I915_GMCH_GMS_STOLEN_48M:	return MB(48);
	case I915_GMCH_GMS_STOLEN_64M:	return MB(64);
	case G33_GMCH_GMS_STOLEN_128M:	return MB(128);
	case G33_GMCH_GMS_STOLEN_256M:	return MB(256);
	case INTEL_GMCH_GMS_STOLEN_96M:	return MB(96);
	case INTEL_GMCH_GMS_STOLEN_160M:return MB(160);
	case INTEL_GMCH_GMS_STOLEN_224M:return MB(224);
	case INTEL_GMCH_GMS_STOLEN_352M:return MB(352);
	default:
		WARN(1, "Unknown GMCH_CTRL value: %x!\n", gmch_ctrl);
	}

	return 0;
}

static resource_size_t  gen6_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;
	u16 gms;

	gmch_ctrl = read_pci_config_16(num, slot, func, SNB_GMCH_CTRL);
	gms = (gmch_ctrl >> SNB_GMCH_GMS_SHIFT) & SNB_GMCH_GMS_MASK;

	return gms * MB(32);
}

static resource_size_t  gen8_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;
	u16 gms;

	gmch_ctrl = read_pci_config_16(num, slot, func, SNB_GMCH_CTRL);
	gms = (gmch_ctrl >> BDW_GMCH_GMS_SHIFT) & BDW_GMCH_GMS_MASK;

	return gms * MB(32);
}

static resource_size_t  chv_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;
	u16 gms;

	gmch_ctrl = read_pci_config_16(num, slot, func, SNB_GMCH_CTRL);
	gms = (gmch_ctrl >> SNB_GMCH_GMS_SHIFT) & SNB_GMCH_GMS_MASK;

	/*
	 * 0x0  to 0x10: 32MB increments starting at 0MB
	 * 0x11 to 0x16: 4MB increments starting at 8MB
	 * 0x17 to 0x1d: 4MB increments start at 36MB
	 */
	if (gms < 0x11)
		return gms * MB(32);
	else if (gms < 0x17)
		return (gms - 0x11) * MB(4) + MB(8);
	else
		return (gms - 0x17) * MB(4) + MB(36);
}

static resource_size_t  gen9_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;
	u16 gms;

	gmch_ctrl = read_pci_config_16(num, slot, func, SNB_GMCH_CTRL);
	gms = (gmch_ctrl >> BDW_GMCH_GMS_SHIFT) & BDW_GMCH_GMS_MASK;

	/* 0x0  to 0xef: 32MB increments starting at 0MB */
	/* 0xf0 to 0xfe: 4MB increments starting at 4MB */
	if (gms < 0xf0)
		return gms * MB(32);
	else
		return (gms - 0xf0) * MB(4) + MB(4);
}

struct intel_early_ops {
	resource_size_t (*stolen_size)(int num, int slot, int func);
	resource_size_t (*stolen_base)(int num, int slot, int func,
				       resource_size_t size);
};

static const struct intel_early_ops i830_early_ops  = {
	.stolen_base = i830_stolen_base,
	.stolen_size = i830_stolen_size,
};

static const struct intel_early_ops i845_early_ops  = {
	.stolen_base = i845_stolen_base,
	.stolen_size = i830_stolen_size,
};

static const struct intel_early_ops i85x_early_ops  = {
	.stolen_base = i85x_stolen_base,
	.stolen_size = gen3_stolen_size,
};

static const struct intel_early_ops i865_early_ops  = {
	.stolen_base = i865_stolen_base,
	.stolen_size = gen3_stolen_size,
};

static const struct intel_early_ops gen3_early_ops  = {
	.stolen_base = gen3_stolen_base,
	.stolen_size = gen3_stolen_size,
};

static const struct intel_early_ops gen6_early_ops  = {
	.stolen_base = gen3_stolen_base,
	.stolen_size = gen6_stolen_size,
};

static const struct intel_early_ops gen8_early_ops  = {
	.stolen_base = gen3_stolen_base,
	.stolen_size = gen8_stolen_size,
};

static const struct intel_early_ops gen9_early_ops  = {
	.stolen_base = gen3_stolen_base,
	.stolen_size = gen9_stolen_size,
};

static const struct intel_early_ops chv_early_ops  = {
	.stolen_base = gen3_stolen_base,
	.stolen_size = chv_stolen_size,
};

static const struct intel_early_ops gen11_early_ops  = {
	.stolen_base = gen11_stolen_base,
	.stolen_size = gen9_stolen_size,
};

static const struct pci_device_id intel_early_ids[] = {
	INTEL_I830_IDS(&i830_early_ops),
	INTEL_I845G_IDS(&i845_early_ops),
	INTEL_I85X_IDS(&i85x_early_ops),
	INTEL_I865G_IDS(&i865_early_ops),
	INTEL_I915G_IDS(&gen3_early_ops),
	INTEL_I915GM_IDS(&gen3_early_ops),
	INTEL_I945G_IDS(&gen3_early_ops),
	INTEL_I945GM_IDS(&gen3_early_ops),
	INTEL_VLV_IDS(&gen6_early_ops),
	INTEL_PINEVIEW_G_IDS(&gen3_early_ops),
	INTEL_PINEVIEW_M_IDS(&gen3_early_ops),
	INTEL_I965G_IDS(&gen3_early_ops),
	INTEL_G33_IDS(&gen3_early_ops),
	INTEL_I965GM_IDS(&gen3_early_ops),
	INTEL_GM45_IDS(&gen3_early_ops),
	INTEL_G45_IDS(&gen3_early_ops),
	INTEL_IRONLAKE_D_IDS(&gen3_early_ops),
	INTEL_IRONLAKE_M_IDS(&gen3_early_ops),
	INTEL_SNB_D_IDS(&gen6_early_ops),
	INTEL_SNB_M_IDS(&gen6_early_ops),
	INTEL_IVB_M_IDS(&gen6_early_ops),
	INTEL_IVB_D_IDS(&gen6_early_ops),
	INTEL_HSW_IDS(&gen6_early_ops),
	INTEL_BDW_IDS(&gen8_early_ops),
	INTEL_CHV_IDS(&chv_early_ops),
	INTEL_SKL_IDS(&gen9_early_ops),
	INTEL_BXT_IDS(&gen9_early_ops),
	INTEL_KBL_IDS(&gen9_early_ops),
	INTEL_CFL_IDS(&gen9_early_ops),
	INTEL_GLK_IDS(&gen9_early_ops),
	INTEL_CNL_IDS(&gen9_early_ops),
	INTEL_ICL_11_IDS(&gen11_early_ops),
	INTEL_EHL_IDS(&gen11_early_ops),
	INTEL_TGL_12_IDS(&gen11_early_ops),
};

struct resource intel_graphics_stolen_res  = DEFINE_RES_MEM(0, 0);
EXPORT_SYMBOL(intel_graphics_stolen_res);

static void 
intel_graphics_stolen(int num, int slot, int func,
		      const struct intel_early_ops *early_ops)
{
	resource_size_t base, size;
	resource_size_t end;

	size = early_ops->stolen_size(num, slot, func);
	base = early_ops->stolen_base(num, slot, func, size);

	if (!size || !base)
		return;

	end = base + size - 1;

	intel_graphics_stolen_res.start = base;
	intel_graphics_stolen_res.end = end;

	printk(KERN_INFO "Reserving Intel graphics memory at %pR\n",
	       &intel_graphics_stolen_res);

	/* Mark this space as reserved */
//	e820__range_add(base, size, E820_TYPE_RESERVED);
//	e820__update_table(e820_table);
}

static void  intel_graphics_quirks(int num, int slot, int func)
{
	const struct intel_early_ops *early_ops;
	u16 device;
	int i;

	device = read_pci_config_16(num, slot, func, PCI_DEVICE_ID);

	for (i = 0; i < ARRAY_SIZE(intel_early_ids); i++) {
		kernel_ulong_t driver_data = intel_early_ids[i].driver_data;

		if (intel_early_ids[i].device != device)
			continue;

		early_ops = (typeof(early_ops))driver_data;

		intel_graphics_stolen(num, slot, func, early_ops);

		return;
	}
}

#define QFLAG_APPLY_ONCE 	0x1
#define QFLAG_APPLIED		0x2
#define QFLAG_DONE		(QFLAG_APPLY_ONCE|QFLAG_APPLIED)
struct chipset {
	u32 vendor;
	u32 device;
	u32 class;
	u32 class_mask;
	u32 flags;
	void (*f)(int num, int slot, int func);
};

static struct chipset early_qrk[] = {
	{ PCI_VENDOR_ID_INTEL, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA, PCI_ANY_ID,
	  QFLAG_APPLY_ONCE, intel_graphics_quirks },
	{}
};

static void  early_pci_scan_bus(int bus);

/**
 * check_dev_quirk - apply early quirks to a given PCI device
 * @num: bus number
 * @slot: slot number
 * @func: PCI function
 *
 * Check the vendor & device ID against the early quirks table.
 *
 * If the device is single function, let early_pci_scan_bus() know so we don't
 * poke at this device again.
 */
static int  check_dev_quirk(int num, int slot, int func)
{
	u16 class;
	u16 vendor;
	u16 device;
	u8 type;
	u8 sec;
	int i;

	class = read_pci_config_16(num, slot, func, PCI_CLASS_DEVICE);

	if (class == 0xffff)
		return -1; /* no class, treat as single function */

	vendor = read_pci_config_16(num, slot, func, PCI_VENDOR_ID);

	device = read_pci_config_16(num, slot, func, PCI_DEVICE_ID);

	for (i = 0; early_qrk[i].f != NULL; i++) {
		if (((early_qrk[i].vendor == PCI_ANY_ID) ||
			(early_qrk[i].vendor == vendor)) &&
			((early_qrk[i].device == PCI_ANY_ID) ||
			(early_qrk[i].device == device)) &&
			(!((early_qrk[i].class ^ class) &
			    early_qrk[i].class_mask))) {
				if ((early_qrk[i].flags &
				     QFLAG_DONE) != QFLAG_DONE)
					early_qrk[i].f(num, slot, func);
				early_qrk[i].flags |= QFLAG_APPLIED;
			}
	}

	type = read_pci_config_byte(num, slot, func,
				    PCI_HEADER_TYPE);

	if ((type & 0x7f) == PCI_HEADER_TYPE_BRIDGE) {
		sec = read_pci_config_byte(num, slot, func, PCI_SECONDARY_BUS);
		if (sec > num)
			early_pci_scan_bus(sec);
	}

	if (!(type & 0x80))
		return -1;

	return 0;
}

static void  early_pci_scan_bus(int bus)
{
	int slot, func;

	/* Poor man's PCI discovery */
	for (slot = 0; slot < 32; slot++)
		for (func = 0; func < 8; func++) {
			/* Only probe function 0 on single fn devices */
			if (check_dev_quirk(bus, slot, func))
				break;
		}
}

void  intel_stolen_init(void)
{
	early_pci_scan_bus(0);
}
