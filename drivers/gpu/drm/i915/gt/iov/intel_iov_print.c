// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_print.h>

#include "intel_iov.h"
#include "intel_iov_utils.h"

/**
 * intel_iov_dump - Dump data from the IOV struct.
 * @iov: the IOV struct
 * @p: the DRM printer
 *
 * Dump data from IOV struct into provided printer.
 */
void intel_iov_dump(const struct intel_iov *iov, struct drm_printer *p)
{
	drm_printf(p, "mode: %s\n", intel_iov_mode_string(iov));
}

/**
 * intel_iov_print_all - Print all info about I/O virtualization.
 * @iov: the IOV struct
 * @p: the DRM printer
 *
 * Print all I/O Virtualization related info into provided printer.
 */
void intel_iov_print_all(struct intel_iov *iov, struct drm_printer *p)
{
	struct device *dev = iov_to_dev(iov);
	struct pci_dev *pdev = to_pci_dev(dev);

	drm_printf(p, "virtualization: %s\n", HAS_IOV(iov_to_i915(iov)) ?
		   enableddisabled(intel_iov_is_enabled(iov)) : yesno(false));

	if (!intel_iov_is_enabled(iov))
		return;

	intel_iov_dump(iov, p);

	if (intel_iov_is_pf(iov)) {
		drm_printf(p, "admin_only: %s\n", yesno(pf_is_admin_only(iov)));
		drm_printf(p, "num_vfs: %u\n", pci_num_vf(pdev));
		drm_printf(p, "total_vfs: %u\n", pci_sriov_get_totalvfs(pdev));
		drm_printf(p, "assigned_vfs: %u\n", pci_vfs_assigned(pdev));
	}

	/* XXX igt is looking for 'sriov: VF' */
	if (intel_iov_is_vf(iov))
		drm_printf(p, "sriov: VF\n");
}
