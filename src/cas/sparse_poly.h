#ifndef PHY_CAS_SPARSE_POLY_H
#define PHY_CAS_SPARSE_POLY_H

#include "cas_internal.h"

/*
 * Try exact sparse Q[x1,...,xn] cancellation. `matched` is false for
 * non-polynomials and coprime inputs; resource and arithmetic failures remain
 * typed errors. Published quotients have been exactly divided and recomposed.
 */
phy_status phy_sparse_cancel_gcd(phy_cas *cas, phy_ir_ref numerator,
                                 phy_ir_ref denominator,
                                 phy_ir_ref *out_numerator,
                                 phy_ir_ref *out_denominator,
                                 bool *out_matched);

#endif
