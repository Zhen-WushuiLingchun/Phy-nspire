/*
 * Native general-relativity curvature pipeline.
 *
 * Conventions are fixed in docs/references/GENERAL_RELATIVITY.md:
 * mostly-plus signature and MTW (+,+,+) signs.  The result owns every output
 * tensor but borrows the caller's metric, chart, IR, and CAS.
 */
#ifndef PHY_GR_H
#define PHY_GR_H

#include "phy/tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_gr_result phy_gr_result;

phy_status phy_gr_compute(phy_cas *cas, const phy_tensor *metric,
                          phy_gr_result **out_result);
void phy_gr_result_destroy(phy_gr_result *result);

const phy_tensor *phy_gr_inverse_metric(const phy_gr_result *result);
const phy_tensor *phy_gr_christoffel(const phy_gr_result *result);
const phy_tensor *phy_gr_riemann_mixed(const phy_gr_result *result);
const phy_tensor *phy_gr_riemann_covariant(const phy_gr_result *result);
const phy_tensor *phy_gr_ricci(const phy_gr_result *result);
phy_ir_ref phy_gr_scalar_curvature(const phy_gr_result *result);
const phy_tensor *phy_gr_einstein(const phy_gr_result *result);

/*
 * Kretschmann invariant K = R_abcd R^abcd.
 *
 * Deliberately NOT part of phy_gr_compute(). It raises three indices over the
 * whole Riemann tensor -- n^4 components each summing over n at every raise --
 * and every caller that only wants the six curvature quantities would pay for
 * it. Ask for it when it is wanted.
 *
 * Computed on the first call and cached in `result`, so repeated calls are
 * free and the fully contravariant Riemann tensor stays available through
 * phy_gr_riemann_contravariant(). A failed call leaves `result` exactly as it
 * was, with no partial contravariant tensor to observe.
 *
 * `cas` must be the CAS the result was computed with, or at least one over the
 * same IR context; anything else is PHY_ERR_INVALID_ARGUMENT.
 */
phy_status phy_gr_kretschmann(phy_cas *cas, phy_gr_result *result,
                              phy_ir_ref *out_scalar);

/* R^abcd, or NULL until phy_gr_kretschmann() has succeeded on `result`. */
const phy_tensor *phy_gr_riemann_contravariant(const phy_gr_result *result);

/*
 * Covariant Weyl tensor C_abcd, cached in `result`.
 *
 * For n >= 3 this uses
 *
 *   C_abcd = R_abcd
 *     - (g_ac R_db - g_ad R_cb - g_bc R_da + g_bd R_ca)/(n-2)
 *     + R (g_ac g_db - g_ad g_cb)/((n-1)(n-2)).
 *
 * In dimensions below three the conformal curvature is identically zero and
 * this API returns a zero rank-4 tensor rather than dividing by n-2.
 * The returned tensor is borrowed from `result`.
 */
phy_status phy_gr_weyl(phy_cas *cas, phy_gr_result *result,
                       const phy_tensor **out_tensor);

/*
 * Exact invariant C_abcd C^abcd, cached in `result`.
 *
 * It is evaluated through the contraction identity
 *
 *   C^2 = Riemann^2 - 4 Ricci^2/(n-2)
 *                      + 2 R^2/((n-1)(n-2)),
 *
 * which is substantially cheaper on the calculator than raising all four
 * Weyl slots. Dimensions below three return exact zero.
 */
phy_status phy_gr_weyl_squared(phy_cas *cas, phy_gr_result *result,
                               phy_ir_ref *out_scalar);

/*
 * Right-hand side of the affinely parametrized geodesic equation:
 *
 *   d^2 x^mu/dlambda^2 = -Gamma^mu_nu_rho v^nu v^rho.
 *
 * `velocity` is a rank-1 contravariant tensor on the result's chart. The
 * caller owns the returned rank-1 contravariant tensor.
 */
phy_status phy_gr_geodesic_acceleration(
    phy_cas *cas, const phy_gr_result *result,
    const phy_tensor *velocity, const char *name,
    phy_tensor **out_tensor);

/*
 * Covariant derivative against the Levi-Civita connection of `result`:
 *
 *   (nabla T)_{c a1..ar} = d_c T_{a1..ar}
 *                          + sum over upper slots  Gamma^{as}_{ce} T_{..e..}
 *                          - sum over lower slots  Gamma^{e}_{c as} T_{..e..}
 *
 * The derivative slot is prepended, so the output has rank r+1 with slot 0
 * lower. Rank r+1 above PHY_TENSOR_MAX_RANK is PHY_ERR_UNSUPPORTED, which
 * bounds the input at rank 3.
 *
 * The output carries no declared slot symmetry even when the input does. The
 * symmetries of a covariant derivative are not the symmetries of its argument,
 * and asserting a group this layer has not proved is how a fill discipline
 * turns into a fill bug; a caller that knows better may check the components.
 *
 * `tensor` must live on the same chart as the metric the result came from. The
 * caller owns the output and destroys it with phy_tensor_destroy().
 */
phy_status phy_gr_covariant_derivative(phy_cas *cas,
                                       const phy_gr_result *result,
                                       const phy_tensor *tensor,
                                       const char *name,
                                       phy_tensor **out_tensor);

#ifdef __cplusplus
}
#endif

#endif /* PHY_GR_H */
