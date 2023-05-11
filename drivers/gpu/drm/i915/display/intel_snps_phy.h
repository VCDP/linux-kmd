// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SNPS_PHY_H__
#define __INTEL_SNPS_PHY_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_encoder;
struct intel_crtc_state;
struct intel_digital_port;
struct intel_mpllb_state;
enum phy;

void intel_snps_phy_wait_for_calibration(struct drm_i915_private *dev_priv);
void intel_snps_phy_update_psr_power_state(struct drm_i915_private *dev_priv,
					   enum phy phy, bool enable);
bool intel_snps_phy_is_dp_alt(struct drm_i915_private *dev_priv, enum phy phy);

int intel_mpllb_calc_state(struct intel_crtc_state *crtc_state,
			   struct intel_encoder *encoder);
void intel_mpllb_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state);
void intel_mpllb_disable(struct intel_encoder *encoder);
void intel_mpllb_readout_hw_state(struct intel_encoder *encoder,
				  struct intel_mpllb_state *pll_state);

int intel_snps_phy_check_hdmi_link_rate(int clock);
void intel_snps_phy_ddi_vswing_sequence(struct intel_encoder *encoder,
					u32 level);

bool intel_snps_phy_connected(struct intel_digital_port *dig_port);
int intel_snps_max_lane_count(struct intel_digital_port *dig_port);

#endif /* __INTEL_SNPS_PHY_H__ */
