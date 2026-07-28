/*
 * Phy-nspire — bounded exact truncated Laurent series.
 *
 * Internal CAS substrate. Coefficients are canonical exact IR atoms and the
 * represented value is
 *
 *   sum(coefficients[e-valuation] * (x-center)^e,
 *       e = valuation .. order-1) + O((x-center)^order).
 *
 * `order` is exclusive. A zero series has valuation == order and no stored
 * coefficients. Operations below do not reset the CAS operation budget; the
 * reader-facing Series/Limit entry point owns that reset.
 */
#ifndef PHY_SERIES_INTERNAL_H
#define PHY_SERIES_INTERNAL_H

#include "cas_internal.h"

#define PHY_SERIES_MIN_EXPONENT (-32)
#define PHY_SERIES_MAX_EXPONENT 64
#define PHY_SERIES_MAX_TERMS 97u

typedef struct {
    phy_ir_ref variable;
    phy_ir_ref center;
    int valuation;
    int order;
    size_t count;
    phy_ir_ref coefficients[PHY_SERIES_MAX_TERMS];
} phy_series;

phy_status phy_series_set(phy_cas *cas, phy_ir_ref variable,
                          phy_ir_ref center, int valuation, int order,
                          const phy_ir_ref *coefficients, size_t count,
                          phy_series *out_series);
phy_status phy_series_zero(phy_cas *cas, phy_ir_ref variable,
                           phy_ir_ref center, int order,
                           phy_series *out_series);
phy_status phy_series_constant(phy_cas *cas, phy_ir_ref variable,
                               phy_ir_ref center, int order,
                               phy_ir_ref coefficient,
                               phy_series *out_series);
phy_status phy_series_validate(const phy_cas *cas,
                               const phy_series *series);
bool phy_series_is_zero(const phy_series *series);
phy_ir_ref phy_series_coefficient(const phy_cas *cas,
                                  const phy_series *series, int exponent);

phy_status phy_series_add_node(phy_cas *cas, const phy_series *left,
                               const phy_series *right,
                               phy_series *out_series);
phy_status phy_series_sub_node(phy_cas *cas, const phy_series *left,
                               const phy_series *right,
                               phy_series *out_series);
phy_status phy_series_mul_node(phy_cas *cas, const phy_series *left,
                               const phy_series *right,
                               phy_series *out_series);
phy_status phy_series_reciprocal_node(phy_cas *cas,
                                      const phy_series *series,
                                      phy_series *out_series);
phy_status phy_series_div_node(phy_cas *cas, const phy_series *numerator,
                               const phy_series *denominator,
                               phy_series *out_series);
phy_status phy_series_pow_int_node(phy_cas *cas, const phy_series *series,
                                   int exponent,
                                   phy_series *out_series);
phy_status phy_series_derivative_node(phy_cas *cas,
                                      const phy_series *series,
                                      phy_series *out_series);
phy_status phy_series_integral_node(phy_cas *cas,
                                    const phy_series *series,
                                    phy_series *out_series);
phy_status phy_series_compose_node(phy_cas *cas, const phy_series *outer,
                                   const phy_series *inner,
                                   phy_series *out_series);

/*
 * Expand one expression without resetting the public-operation budget.
 * `order` is exclusive. This is shared by Series and Limit so the two
 * commands cannot disagree about valuation or the leading coefficient.
 */
phy_status phy_series_expand_node(phy_cas *cas, phy_ir_ref expression,
                                  phy_ir_ref variable, phy_ir_ref center,
                                  int order, phy_series *out_series);

#endif /* PHY_SERIES_INTERNAL_H */
