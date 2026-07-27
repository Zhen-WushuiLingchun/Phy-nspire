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

#ifdef __cplusplus
}
#endif

#endif /* PHY_GR_H */
