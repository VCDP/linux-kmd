// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 - 2020 Intel Corporation
 */

#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/mfd/core.h>
#include <linux/xarray.h>

#include <drm/intel_iaf_platform.h>

#include "gt/intel_gt.h"
#include "i915_drv.h"
#include "intel_iaf.h"

/* Xarray of fabric devices */
DEFINE_XARRAY_ALLOC(intel_fdevs);

static struct query_info *default_query(void *handle, u32 fabric_id)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static int default_handle_event(void *handle, enum iaf_parent_event event)
{
	return 0;
}

static const struct iaf_ops default_ops = {
	.connectivity_query = default_query,
	.parent_event = default_handle_event,
};

static void register_dev(void *parent, void *handle, u32 fabric_id,
			 const struct iaf_ops *ops)
{
	struct drm_i915_private *i915 = parent;

	WARN(i915->intel_iaf.ops != &default_ops, "IAF: already registered");

	i915->intel_iaf.handle = handle;
	i915->intel_iaf.fabric_id = fabric_id;
	i915->intel_iaf.ops = ops;

	drm_info(&i915->drm, "IAF: registered fabric: %x\n", fabric_id);
}

static void unregister_dev(void *parent, void *handle)
{
	struct drm_i915_private *i915 = parent;

	WARN(i915->intel_iaf.handle != handle, "IAF: invalid handle");

	drm_info(&i915->drm, "IAF: unregistered fabric: %x\n",
		 i915->intel_iaf.fabric_id);
	i915->intel_iaf.handle = NULL;
	i915->intel_iaf.ops = &default_ops;
}

static int dev_event(void *parent, void *handle, enum iaf_dev_event event,
		     void *event_data)
{
	return 0;
}

/**
 * init_pd - Allocate and initialize platform specific data
 * @i915: Valid i915 instance
 *
 * Return:
 * * pd - initialized iaf_pdata,
 * * NULL - Allocation failure
 */
static struct iaf_pdata *init_pd(struct drm_i915_private *i915)
{
	struct iaf_pdata *pd;
	u32 reg;

	pd = kmalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return NULL;

	pd->version = IAF_VERSION;
	pd->parent = i915;
	pd->product = IAF_PONTEVECCHIO;
	pd->index = i915->intel_iaf.index & 0xFFFF;
	pd->sd_cnt = i915->remote_tiles + 1;
	pd->socket_id = i915->intel_iaf.socket_id;
	pd->slot = PCI_SLOT(i915->drm.pdev->devfn);

	pd->register_dev = register_dev;
	pd->unregister_dev = unregister_dev;
	pd->dev_event = dev_event;

	/*
	 * Calculate the actual DPA offset and size (in GB) for the device.
	 * Each tile will have the same amount of memory, so we only need to
	 * read the first one.
	 */
	reg = intel_uncore_read(&i915->uncore, ATS_TILE0_ADDR_RANGE) &
		ATS_TILE_LMEM_RANGE_MASK;

	// FIXME: On some systems, TILE0 is < 8Gb. PVC needs 8GB, so fake it.
	if (reg >> ATS_TILE_LMEM_RANGE_SHIFT < 8) {
		drm_err(&i915->drm, "ATS_TILE0_ADDR_RANGE: %x\n", reg);
		reg = 8 << ATS_TILE_LMEM_RANGE_SHIFT;
	}
	pd->dpa.pkg_offset = (u32)i915->intel_iaf.index * MAX_DPA_SIZE;
	pd->dpa.pkg_size = (reg >> ATS_TILE_LMEM_RANGE_SHIFT) * pd->sd_cnt;

	return pd;
}

/**
 * init_resource - Create the resource array, and apply the appropriate data
 * @i915: valid i915 instance
 * @res_cnt: pointer to return number of allocated resources
 *
 * First resource [0] is for the IRQ(s).  Each device gets 1 IRQ. Subsequent
 * resources describe the IO memory space for the device(s).
 *
 * Make sure to set the gt->iaf_irq value.
 *
 * Return:
 * * res - Initialized resource array
 * * NULL - Allocaction failure
 */
static struct resource *init_resource(struct drm_i915_private *i915,
				      u32 *res_cnt)
{
	struct intel_gt *gt;
	struct resource *res_base, *res;
	u32 cnt = (i915->remote_tiles + 1) * 2;
	int i;

	/* Each sd gets one resource for IRQ and one for MEM */
	res_base = kcalloc(cnt, sizeof(*res_base), GFP_KERNEL);
	if (!res_base)
		return NULL;

	res = res_base;
	for_each_gt(i915, i, gt) {
		res->start = i;
		res->end = i;
		res->flags = IORESOURCE_IRQ;
		res++;

		res->start = gt->phys_addr + CD_BASE_OFFSET;
		res->end = res->start + CD_BAR_SIZE - 1;
		res->flags = IORESOURCE_MEM;
		drm_info(&i915->drm, "IAF: mfd_resource = %pR\n", res);
		res++;

		gt->iaf_irq = i915->intel_iaf.irq_base + i;
	}

	*res_cnt = cnt;
	return res_base;
}

/**
 * iaf_irq_mask - Null callback.  Masking/unmasking happens in the parent
 * driver
 * @d: Valid irq_data information
 */
static void iaf_irq_mask(struct irq_data *d)
{
}

static void iaf_irq_unmask(struct irq_data *d)
{
}

static struct irq_chip iaf_irq_chip = {
	.name = "iaf_irq_chip",
	.irq_mask = iaf_irq_mask,
	.irq_unmask = iaf_irq_unmask,
};

/**
 * init_irq_desc - Allocate IRQ descriptors to use for the iaf
 * @i915: Valid i915 instance
 *
 * Allocate the required IRQ descriptor(s) and initialize the
 * appropriate state.
 *
 * Return:
 * * 0 - Success
 * * errno - Error that occurred
 */
static int init_irq_desc(struct drm_i915_private *i915)
{
	unsigned int num_subdevs = i915->remote_tiles + 1;
	int err;
	int irq;
	int irq_base;

	irq_base = irq_alloc_descs(-1, 0, num_subdevs, 0);
	if (irq_base < 0) {
		err = irq_base;
		goto cleanup;
	}

	err = 0;
	for (irq = irq_base; !err && irq < irq_base + num_subdevs; irq++) {
		irq_set_chip_and_handler_name(irq, &iaf_irq_chip,
					      handle_simple_irq,
					      "iaf_irq_handler");
		err = irq_set_chip_data(irq, i915);
	}

	if (err) {
		irq_free_descs(irq_base, num_subdevs);
		goto cleanup;
	}

	drm_info(&i915->drm, "IAF: IRQ base: %d  cnt: %d\n", irq_base,
		 num_subdevs);

	i915->intel_iaf.irq_base = irq_base;

	return 0;

cleanup:
	i915->intel_iaf.index = err;
	drm_err(&i915->drm, "IAF: Failed to allocate IRQ data: %d\n", err);
	return err;
}

/**
 * intel_iaf_init_early - Set the iaf info to the defaults
 * @i915: valid i915 instance
 *
 * index is set to ENODEV to show that, by default, there is no device.
 * If any of the initialization steps fail, it will be set to the appropriate
 * errno value.
 */
void intel_iaf_init_early(struct drm_i915_private *i915)
{
	i915->intel_iaf.ops = &default_ops;
	i915->intel_iaf.index = -ENODEV;
}

/**
 * intel_iaf_init_mmio - check if iaf is available via MMIO
 * @i915: valid i915 instance
 *
 * Read the relevant regs to check for IAF availability and get the socket id
 */
void intel_iaf_init_mmio(struct drm_i915_private *i915)
{
	u32 iaf_info;

	if (!HAS_IAF(i915) || !i915_modparams.enable_iaf || IS_SRIOV_VF(i915))
		return;

	iaf_info = intel_uncore_read(&i915->uncore, PUNIT_MMIO_CR_POC_STRAPS);

	i915->intel_iaf.socket_id = REG_FIELD_GET(SOCKET_ID_MASK, iaf_info);

	if (!iaf_info)
		DRM_ERROR("iaf_info = %u\n", iaf_info);
	if (REG_FIELD_GET(CD_ALIVE, iaf_info)) {
		DRM_INFO("IAF available\n");
		i915->intel_iaf.present = true;
	}
}

/**
 * intel_iaf_init - Complete resource allocation and initial HW setup
 * @i915: valid device instance
 *
 * index indicates that the IAF was either:
 *   not found (ENODEV),
 *   IS_SRIOV_VF, (the IAF is not supported by the VF)
 *   or that IRQ and/or index information was not available (err).
 *
 */
void intel_iaf_init(struct drm_i915_private *i915)
{
	void __iomem *const regs = i915->uncore.regs;
	int err;
	u32 base;
	u32 last_id;
	u32 index = 0;

	if (!HAS_IAF(i915) || IS_SRIOV_VF(i915))
		return;

	if (i915->intel_iaf.present) {
		err = init_irq_desc(i915);
		if (err)
			goto set_range;

		/*
		 * NOTE: index is only updated on success i.e. >= 0
		 * < 0 err, 0 ok, > 0 wrapped
		 */
		err = xa_alloc_cyclic(&intel_fdevs, &index, i915,
				      XA_LIMIT(0, MAX_DEVICE_COUNT - 1),
				      &last_id, GFP_KERNEL);
		if (err < 0) {
			i915->intel_iaf.index = err;
			drm_err(&i915->drm,
				"IAF: Failed to allocate fabric index: %d\n",
				err);
			irq_free_descs(i915->intel_iaf.irq_base,
				       i915->remote_tiles + 1);
			goto set_range;
		}
		i915->intel_iaf.index = index;
		i915->intel_iaf.dpa = (u64)index * MAX_DPA_SIZE * SZ_1G;
		drm_info(&i915->drm, "IAF: [dpa 0x%llx-0x%llx]\n",
			 i915->intel_iaf.dpa,
			 ((u64)index + 1) * MAX_DPA_SIZE * SZ_1G - 1);
	}

	/*
	 * Set range has to be done for all devices that support device
	 * address space, regardless of presence or error.
	 */
set_range:
	/* Set RANGE and BASE registers */
	base = index * MAX_DPA_SIZE << PKG_ADDR_RANGE_BASE_SHIFT;
	base |= MAX_DPA_SIZE << PKG_ADDR_RANGE_RANGE_SHIFT;
	base |= PKG_ADDR_RANGE_ENABLE;

	raw_reg_write(regs, PKG_ADDR_RANGE, base);

	base = index * MAX_DPA_SIZE << PKG_ADDR_BASE_BASE_SHIFT;
	base |= MAX_DPA_SIZE << PKG_ADDR_BASE_RANGE_SHIFT;
	base |= PKG_ADDR_BASE_ENABLE;

	raw_reg_write(regs, PKG_ADDR_BASE, base);

	// FIXME:  On some systems, PKG_ADDR is not sticking.  Notice when.
	base = 0;
	base = raw_reg_read(regs, PKG_ADDR_RANGE);
	if (!base)
		drm_err(&i915->drm, "PKG_ADDR_RANGE: %x\n", base);
}

/**
 * intel_iaf_init_mfd - Initialize resources and add MFD interface
 * @i915: valid i915 instance
 *
 * NOTE: MFD allows irq_base to be specified.  It will update the
 * IORESOURCE_IRQ with the base + instance.
 *
 */
void intel_iaf_init_mfd(struct drm_i915_private *i915)
{
	struct mfd_cell *fcell = &i915->intel_iaf.cell;
	struct device *dev = &i915->drm.pdev->dev;
	struct resource *res = NULL;
	struct iaf_pdata *pd;
	int err = -ENOMEM;
	u32 res_cnt;

	if (!i915->intel_iaf.present)
		return;

	if (i915->intel_iaf.index < 0) {
		err = i915->intel_iaf.index;
		goto exunt;
	}

	WARN(IS_SRIOV_VF(i915), "Intel IAF doesn't support VF\n");

	pd = init_pd(i915);
	if (!pd)
		goto cleanup;

	res = init_resource(i915, &res_cnt);
	if (!res)
		goto cleanup;

	fcell->name = "iaf";
	fcell->platform_data = pd;
	fcell->pdata_size = sizeof(*pd);
	fcell->resources = res;
	fcell->num_resources = res_cnt;
#ifdef SUSPEND_RESUME
	fcell->suspend = iaf_suspend;
	fcell->resume = iaf_resume;
#endif

	err = mfd_add_devices(dev, pd->index, fcell, 1, 0,
			      i915->intel_iaf.irq_base, NULL);
	if (err)
		goto cleanup;

	return;

cleanup:
	xa_erase(&intel_fdevs, i915->intel_iaf.index);
	irq_free_descs(i915->intel_iaf.irq_base, i915->remote_tiles + 1);
	kfree(res);
	kfree(pd);
	i915->intel_iaf.index = err;
exunt:
	drm_err(&i915->drm, "IAF: Failed to initialize fabric: %d\n", err);
}

void intel_iaf_remove(struct drm_i915_private *i915)
{
	struct mfd_cell *fcell = &i915->intel_iaf.cell;
	struct iaf_pdata *pd = fcell->platform_data;

	if (i915->intel_iaf.index < 0)
		return;

	xa_erase(&intel_fdevs, pd->index);
	irq_free_descs(i915->intel_iaf.irq_base, i915->remote_tiles + 1);

	i915->intel_iaf.ops = &default_ops;

	kfree(fcell->platform_data);
	kfree(fcell->resources);
}
