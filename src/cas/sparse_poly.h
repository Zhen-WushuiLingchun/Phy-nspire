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

/*
 * Certified bounded polynomial operations over Q.  Variables are explicit so
 * the monomial order is never inferred from symbol spelling: the first
 * variable is the most significant one in lexicographic order.
 */
phy_status phy_sparse_resultant(phy_cas *cas, phy_ir_ref left,
                                phy_ir_ref right, phy_ir_ref variable,
                                phy_ir_ref *out_ref);
phy_status phy_sparse_discriminant(phy_cas *cas, phy_ir_ref expression,
                                   phy_ir_ref variable,
                                   phy_ir_ref *out_ref);
phy_status phy_sparse_groebner_basis(
    phy_cas *cas, const phy_ir_ref *expressions, size_t expression_count,
    const phy_ir_ref *variables, size_t variable_count,
    phy_ir_ref *out_ref);

/*
 * Exact zero-dimensional rational polynomial systems.  This is deliberately
 * separate from the linear solver: it first certifies a lex Groebner basis,
 * extracts a triangular rational branch set, and verifies every branch in the
 * original equations before publication.
 */
phy_status phy_sparse_solve_polynomial_system(
    phy_cas *cas, const phy_ir_ref *equations, size_t equation_count,
    const phy_ir_ref *variables, size_t variable_count,
    phy_ir_ref *out_ref);

/* Square-free Q[x] coefficients in increasing degree order as List. */
phy_status phy_sparse_univariate_squarefree_coefficients(
    phy_cas *cas, phy_ir_ref expression, phy_ir_ref variable,
    phy_ir_ref *out_coefficients);

#endif
