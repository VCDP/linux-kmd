// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_snps_phy.h"

/**
 * DOC: Synopsis PHY support
 *
 * Synopsis PHYs are primarily programmed by looking up magic register values
 * in tables rather than calculating the necessary values at runtime.
 *
 * Of special note is that the SNPS PHYs include a dedicated port PLL, known as
 * an "MPLLB."  The MPLLB replaces the shared DPLL functionality used on other
 * platforms and must be programming directly during the modeset sequence
 * since it is not handled by the shared DPLL framework as on other platforms.
 */

void intel_snps_phy_wait_for_calibration(struct drm_i915_private *dev_priv)
{
	enum phy phy;

	for_each_phy_masked(phy, ~0) {
		if (!intel_phy_is_snps(dev_priv, phy))
			continue;

		if (intel_de_wait_for_clear(dev_priv, ICL_PHY_MISC(phy),
					    DG2_PHY_DP_TX_ACK_MASK, 25))
			DRM_ERROR("SNPS PHY %c failed to calibrate after 25ms.\n",
				  phy);
	}
}

void intel_snps_phy_update_psr_power_state(struct drm_i915_private *dev_priv,
					   enum phy phy, bool enable)
{
	u32 val;

	if (!intel_phy_is_snps(dev_priv, phy))
		return;

	val = REG_FIELD_PREP(SNPS_PHY_TX_REQ_LN_DIS_PWR_STATE_PSR,
			     enable ? 2 : 3);
	intel_uncore_rmw(&dev_priv->uncore, SNPS_PHY_TX_REQ(phy),
			 SNPS_PHY_TX_REQ_LN_DIS_PWR_STATE_PSR, val);
}

bool intel_snps_phy_is_dp_alt(struct drm_i915_private *dev_priv, enum phy phy)
{
	u32 val;

	/*
	 * A refclk of 100Mhz indicates that the PHY is configured for native
	 * DP and HDMI rather than DP-alternate mode (which uses a 38.4Mhz
	 * refclk instead).
	 */
	val = I915_READ(SNPS_PHY_REF_CONTROL(phy));
	if (val & SNPS_PHY_REF_CONTROL_REF_CLK_100MHZ)
	    return false;

	return true;
}

static const u32 dg2_ddi_translations[] = {
	/* VS 0, pre-emph 0 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 26),

	/* VS 0, pre-emph 1 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 33) |
		REG_FIELD_PREP(SNPS_PHY_TX_EQ_POST, 6),

	/* VS 0, pre-emph 2 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 38) |
		REG_FIELD_PREP(SNPS_PHY_TX_EQ_POST, 12),

	/* VS 0, pre-emph 3 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 43) |
		REG_FIELD_PREP(SNPS_PHY_TX_EQ_POST, 19),

	/* VS 1, pre-emph 0 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 39),

	/* VS 1, pre-emph 1 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 44) |
		REG_FIELD_PREP(SNPS_PHY_TX_EQ_POST, 8),

	/* VS 1, pre-emph 2 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 47) |
		REG_FIELD_PREP(SNPS_PHY_TX_EQ_POST, 15),

	/* VS 2, pre-emph 0 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 52),

	/* VS 2, pre-emph 1 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 51) |
		REG_FIELD_PREP(SNPS_PHY_TX_EQ_POST, 10),

	/* VS 3, pre-emph 0 */
	REG_FIELD_PREP(SNPS_PHY_TX_EQ_MAIN, 62),
};

void intel_snps_phy_ddi_vswing_sequence(struct intel_encoder *encoder,
					u32 level)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	int n_entries, ln;

	n_entries = ARRAY_SIZE(dg2_ddi_translations);
	if (level >= n_entries)
		level = n_entries - 1;

	for (ln = 0; ln < 4; ln++)
		I915_WRITE(SNPS_PHY_TX_EQ(ln, phy),
			   dg2_ddi_translations[level]);
}

/*
 * Basic DP link rates with 100 MHz reference clock.
 */

static const struct intel_mpllb_state dg2_dp_rbr_100 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 20) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 226),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 5),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 39321) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 3),
};

static const struct intel_mpllb_state dg2_dp_rbr_ssc_100 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 20) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 226),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 5),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 39321) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 3),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 38221),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 49314),
};

static const struct intel_mpllb_state dg2_dp_hbr1_100 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 20) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 184),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
};

static const struct intel_mpllb_state dg2_dp_hbr1_ssc_100 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 20) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 184),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 31850),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 41095),
};

static const struct intel_mpllb_state dg2_dp_hbr2_100 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 20) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 184),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
};

static const struct intel_mpllb_state dg2_dp_hbr2_ssc_100 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 20) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 184),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 31850),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 41095),
};

static const struct intel_mpllb_state dg2_dp_hbr3_100 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 19) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 292),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
};

static const struct intel_mpllb_state dg2_dp_hbr3_ssc_100 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 19) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 292),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 47776),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 61642),
};

/*
 * Basic DP link rates with 38.4 MHz reference clock.
 */

static const struct intel_mpllb_state dg2_dp_rbr_38_4 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 1),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 25) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 304),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 49152),
};

static const struct intel_mpllb_state dg2_dp_rbr_ssc_38_4 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 1),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 25) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 304),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 49152),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 49766),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 83608),
};

static const struct intel_mpllb_state dg2_dp_hbr1_38_4 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 1),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 25) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 248),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 40960),
};

static const struct intel_mpllb_state dg2_dp_hbr1_ssc_38_4 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 1),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 25) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 248),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 40960),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 41472),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 69673),
};

static const struct intel_mpllb_state dg2_dp_hbr2_38_4 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 1),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 25) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 248),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 40960),
};

static const struct intel_mpllb_state dg2_dp_hbr2_ssc_38_4 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 1),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 25) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 248),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 40960),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 41472),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 69673),
};

static const struct intel_mpllb_state dg2_dp_hbr3_38_4 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 1),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 6) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 26) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 388),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 61440),
};

static const struct intel_mpllb_state dg2_dp_hbr3_ssc_38_4 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 1),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 6) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 26) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 388),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 1),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 61440),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 62208),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 104509),
};

/*
 * eDP link rates with 100 MHz reference clock.
 */

static const struct intel_mpllb_state dg2_edp_r216 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 19) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 312),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 5),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 52428) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 4),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 50961),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 65752),
};

static const struct intel_mpllb_state dg2_edp_r243 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 20) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 356),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 5),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 26214) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 2),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 57331),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 73971),
};

static const struct intel_mpllb_state dg2_edp_r324 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 20) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 226),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 5),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 39321) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 3),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 38221),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 49314),
};

static const struct intel_mpllb_state dg2_edp_r432 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 19) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 65) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 127),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 312),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 5),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 52428) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 4),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_PEAK, 50961),
	.mpllb_sscstep =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_STEPSIZE, 65752),
};

/*
 * HDMI link rates with 100 MHz reference clock.
 */

static const struct intel_mpllb_state dg2_hdmi_25_175 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 15) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 64) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 124),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 128) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_HDMI_DIV, 1),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 143),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 36663) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 71),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_UP_SPREAD, 1),
};

static const struct intel_mpllb_state dg2_hdmi_27_0 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 15) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 64) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 124),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 5) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 140) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_HDMI_DIV, 1),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 5),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 26214) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 2),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_UP_SPREAD, 1),
};

static const struct intel_mpllb_state dg2_hdmi_74_25 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 15) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 64) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 124),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 3) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 86) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_HDMI_DIV, 1),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 5),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 26214) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 2),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_UP_SPREAD, 1),
};

static const struct intel_mpllb_state dg2_hdmi_148_5 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 15) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 64) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 124),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, 3),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 86) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_HDMI_DIV, 1),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 5),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 26214) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 2),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_UP_SPREAD, 1),
};

static const struct intel_mpllb_state dg2_hdmi_59_4 = {
	.ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, 3),
	.mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, 14) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, 64) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, 124),
	.mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, 4) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, 2),
	.mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, 158) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_HDMI_DIV, 1),
	.mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, 25),
	.mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, 2621) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, 11),
	.mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_UP_SPREAD, 1),
};

int intel_mpllb_calc_state(struct intel_crtc_state *crtc_state,
			   struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(encoder);

	if (encoder->type == INTEL_OUTPUT_EDP) {
		if (crtc_state->port_clock > 324000)
			crtc_state->mpllb_state = dg2_edp_r432;
		else if (crtc_state->port_clock > 243000)
			crtc_state->mpllb_state = dg2_edp_r324;
		else if (crtc_state->port_clock > 216000)
			crtc_state->mpllb_state = dg2_edp_r243;
		else
			crtc_state->mpllb_state = dg2_edp_r216;
	} else if (encoder->type == INTEL_OUTPUT_DP) {
		/*
		 * Outputs A-D always use a 100 MHz "Display Filter PLL" as
		 * an input.  The TC1 output uses this same 100 MHz clock when
		 * running in native/legacy mode, but uses a different 38.4 MHz
		 * "Filter PLL" when setup for Type-C DP alternate mode.
		 */
		if (intel_port_to_phy(i915, encoder->port) == PHY_E &&
		    intel_dig_port->tc_mode == TC_PORT_DP_ALT) {
			if (crtc_state->port_clock > 540000)
				crtc_state->mpllb_state = dg2_dp_hbr3_38_4;
			else if (crtc_state->port_clock > 270000)
				crtc_state->mpllb_state = dg2_dp_hbr2_38_4;
			else if (crtc_state->port_clock > 162000)
				crtc_state->mpllb_state = dg2_dp_hbr1_38_4;
			else
				crtc_state->mpllb_state = dg2_dp_rbr_38_4;
		} else {
			if (crtc_state->port_clock > 540000)
				crtc_state->mpllb_state = dg2_dp_hbr3_100;
			else if (crtc_state->port_clock > 270000)
				crtc_state->mpllb_state = dg2_dp_hbr2_100;
			else if (crtc_state->port_clock > 162000)
				crtc_state->mpllb_state = dg2_dp_hbr1_100;
			else
				crtc_state->mpllb_state = dg2_dp_rbr_100;
		}
	} else if (encoder->type == INTEL_OUTPUT_HDMI) {
		if (crtc_state->port_clock == 148500) {
			crtc_state->mpllb_state = dg2_hdmi_148_5;
		} else if (crtc_state->port_clock == 74250) {
			crtc_state->mpllb_state = dg2_hdmi_74_25;
		} else if (crtc_state->port_clock == 59400) {
			crtc_state->mpllb_state = dg2_hdmi_59_4;
		} else if (crtc_state->port_clock == 27000) {
			crtc_state->mpllb_state = dg2_hdmi_27_0;
		} else if (crtc_state->port_clock == 25175) {
			crtc_state->mpllb_state = dg2_hdmi_25_175;
		} else {
			/*
			 * FIXME: Can only support fixed HDMI frequencies
			 * until we have a proper algorithm under a valid
			 * license.  See also hdmi_port_clock_valid().
			 */
			DRM_DEBUG_KMS("Can't support HDMI link rate %d\n",
				      crtc_state->port_clock);
			return -EINVAL;
		}
	} else {
		MISSING_CASE(encoder->type);
		return -EINVAL;
	}

	return 0;
}

void intel_mpllb_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	const struct intel_mpllb_state *pll_state = &crtc_state->mpllb_state;
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	i915_reg_t enable_reg = (phy <= PHY_D ?
				 CNL_DPLL_ENABLE(phy) : MG_PLL_ENABLE(0));

	/*
	 * 3. Software programs the following PLL registers for the desired
	 * frequency.
	 */
	I915_WRITE(SNPS_PHY_MPLLB_CP(phy), pll_state->mpllb_cp);
	I915_WRITE(SNPS_PHY_MPLLB_DIV(phy), pll_state->mpllb_div);
	I915_WRITE(SNPS_PHY_MPLLB_DIV2(phy), pll_state->mpllb_div2);
	I915_WRITE(SNPS_PHY_MPLLB_SSCEN(phy), pll_state->mpllb_sscen);
	I915_WRITE(SNPS_PHY_MPLLB_SSCSTEP(phy), pll_state->mpllb_sscstep);
	I915_WRITE(SNPS_PHY_MPLLB_FRACN1(phy), pll_state->mpllb_fracn1);
	I915_WRITE(SNPS_PHY_MPLLB_FRACN2(phy), pll_state->mpllb_fracn2);

	/*
	 * 4. If the frequency will result in a change to the voltage
	 * requirement, follow the Display Voltage Frequency Switching -
	 * Sequence Before Frequency Change.
	 *
	 * We handle this step in bxt_set_cdclk().
	 */

	/* 5. Software sets DPLL_ENABLE [PLL Enable] to "1". */
	intel_uncore_rmw(&dev_priv->uncore, enable_reg, 0, PLL_ENABLE);

	/*
	 * 9. Software sets SNPS_PHY_MPLLB_DIV dp_mpllb_force_en to "1". This
	 * will keep the PLL running during the DDI lane programming and any
	 * typeC DP cable disconnect. Do not set the force before enabling the
	 * PLL because that will start the PLL before it has sampled the
	 * divider values.
	 */
	I915_WRITE(SNPS_PHY_MPLLB_DIV(phy),
		   pll_state->mpllb_div | SNPS_PHY_MPLLB_FORCE_EN);

	/*
	 * 10. Software polls on register DPLL_ENABLE [PLL Lock] to confirm PLL
	 * is locked at new settings. This register bit is sampling PHY
	 * dp_mpllb_state interface signal.
	 */
	if (intel_de_wait_for_set(dev_priv, enable_reg, PLL_LOCK, 5))
		DRM_ERROR("Port %c PLL not locked\n", phy_name(phy));

	/*
	 * 11. If the frequency will result in a change to the voltage
	 * requirement, follow the Display Voltage Frequency Switching -
	 * Sequence After Frequency Change.
	 *
	 * We handle this step in bxt_set_cdclk().
	 */
}

void intel_mpllb_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	i915_reg_t enable_reg = (phy <= PHY_D ?
				 CNL_DPLL_ENABLE(phy) : MG_PLL_ENABLE(0));

	/*
	 * 1. If the frequency will result in a change to the voltage
	 * requirement, follow the Display Voltage Frequency Switching -
	 * Sequence Before Frequency Change.
	 *
	 * We handle this step in bxt_set_cdclk().
	 */

	/* 2. Software programs DPLL_ENABLE [PLL Enable] to "0" */
	intel_uncore_rmw(&dev_priv->uncore, enable_reg, PLL_ENABLE, 0);

	/*
	 * 4. Software programs SNPS_PHY_MPLLB_DIV dp_mpllb_force_en to "0".
	 * This will allow the PLL to stop running.
	 */
	intel_uncore_rmw(&dev_priv->uncore, SNPS_PHY_MPLLB_DIV(phy),
			 SNPS_PHY_MPLLB_FORCE_EN, 0);

	/*
	 * 5. Software polls DPLL_ENABLE [PLL Lock] for PHY acknowledgement
	 * (dp_txX_ack) that the new transmitter setting request is completed.
	 */
	if (intel_de_wait_for_clear(dev_priv, enable_reg, PLL_LOCK, 5))
		DRM_ERROR("Port %c PLL not locked\n", phy_name(phy));

	/*
	 * 6. If the frequency will result in a change to the voltage
	 * requirement, follow the Display Voltage Frequency Switching -
	 * Sequence After Frequency Change.
	 *
	 * We handle this step in bxt_set_cdclk().
	 */
}

void intel_mpllb_readout_hw_state(struct intel_encoder *encoder,
				  struct intel_mpllb_state *pll_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);

	pll_state->ref_control = I915_READ(SNPS_PHY_MPLLB_CP(phy));
	pll_state->mpllb_cp = I915_READ(SNPS_PHY_MPLLB_CP(phy));
	pll_state->mpllb_div = I915_READ(SNPS_PHY_MPLLB_DIV(phy));
	pll_state->mpllb_div2 = I915_READ(SNPS_PHY_MPLLB_DIV2(phy));
	pll_state->mpllb_sscen = I915_READ(SNPS_PHY_MPLLB_SSCEN(phy));
	pll_state->mpllb_sscstep = I915_READ(SNPS_PHY_MPLLB_SSCSTEP(phy));
	pll_state->mpllb_fracn1 = I915_READ(SNPS_PHY_MPLLB_FRACN1(phy));
	pll_state->mpllb_fracn2 = I915_READ(SNPS_PHY_MPLLB_FRACN2(phy));

	/*
	 * MPLLB_DIV is programmed twice, once with the software-computed
	 * state, then again with the MPLLB_FORCE_EN bit added.  Drop that
	 * extra bit during readout so that we return the actual expected
	 * software state.
	 */
	pll_state->mpllb_div &= ~SNPS_PHY_MPLLB_FORCE_EN;
}

int intel_snps_phy_check_hdmi_link_rate(int clock)
{
	switch(clock) {
	case 25175:
	case 27000:
	case 59400:
	case 74250:
	case 148500:
		return MODE_OK;
	default:
		return MODE_CLOCK_RANGE;
	}
}

/*
 * The extra Type-C connection handling we have to do on earlier platforms is
 * not necessary here since firmware handles the TC connect sequences without
 * driver involvement.  We just need to check the north display ISR (for
 * outputs running in DP-alt mode) or the south display ISR (for all other
 * outputs) to determine the live status.
 */
bool intel_snps_phy_connected(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);

	/* Non-DP-alt outputs use the combo PHY connected function */
	WARN_ON(dig_port->tc_mode != TC_PORT_DP_ALT);

	return I915_READ(GEN11_DE_HPD_ISR) & GEN11_TC_HOTPLUG(tc_port);
}

int intel_snps_max_lane_count(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, dig_port->base.port);
	u32 typec_status;

	if (dig_port->tc_mode != TC_PORT_DP_ALT)
		return 4;

	typec_status = I915_READ(SNPS_PHY_TYPEC_STATUS(phy));
	return typec_status & SNPS_PHY_DPALT_DP4 ? 4 : 2;
}
