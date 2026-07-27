/*
 * Phy-nspire — exact symbolic SU(N) colour algebra.
 *
 * This is contract Q-6 of docs/agent-tasks/QFT_DIRAC.md.  It implements the
 * fundamental-generator normalisation
 *
 *     Tr(T^a T^b) = delta^(ab) / 2
 *
 * together with the abstract invariant tensors, commutator, and quadratic
 * Casimirs needed by the first QFT layer.  N is an exact IR expression and may
 * remain an unbound symbol.  No floating-point or matrix backend is involved.
 *
 * Scope is deliberately sharp:
 *
 *   - f^(abc) is real and totally antisymmetric;
 *   - d^(abc), needed by a trace of three generators, is totally symmetric;
 *   - traces of more than three generators remain SUNTrace[...] by default;
 *   - exact numerical f components are available for the built-in SU(2) and
 *     SU(3) bases, with public component numbers written one-based;
 *   - the Fierz/completeness identity is not implemented.
 *
 * Every colour index belongs to the "ColorAdjoint" IR bundle.  Lorentz,
 * spinor, fundamental-colour, or generic indices are rejected with
 * PHY_ERR_TYPE rather than silently contracted.
 *
 * A colour context borrows its CAS and IR.  Destroy it before either.
 */
#ifndef PHY_COLOR_H
#define PHY_COLOR_H

#include <stdbool.h>
#include <stddef.h>

#include "phy/cas.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHY_COLOR_MAX_TRACE 16u

typedef struct phy_color phy_color;

/*
 * Create an SU(N) context.
 *
 * `N` may be a symbol or another exact scalar expression.  An exact integer
 * must be at least two; a rational non-integer is rejected because SU(N) is
 * defined here only for integral N.
 */
phy_status phy_color_create(phy_cas *cas, phy_ir_ref n,
                            phy_color **out_color);
void phy_color_destroy(phy_color *color);

phy_cas *phy_color_cas(const phy_color *color);
phy_ir_ref phy_color_n(const phy_color *color);
phy_ir_symbol phy_color_adjoint_space(const phy_color *color);

/* Exact N^2 - 1. */
phy_status phy_color_adjoint_dimension(const phy_color *color,
                                       phy_ir_ref *out_dimension);

/* --------------------------------------------------------------- indices */

/* All public colour-index spellings are adjoint upper indices. */
phy_status phy_color_index(const phy_color *color, const char *name,
                           phy_ir_ref *out_index);
bool phy_color_owns_index(const phy_color *color, phy_ir_ref ref);

/* ----------------------------------------------------- invariant tensors */

/*
 * delta^(ab), f^(abc), and d^(abc) in canonical index order.
 *
 * delta^(aa) is the adjoint dimension.  A repeated index makes f exactly
 * zero.  The sign acquired by sorting f is retained exactly.
 */
phy_status phy_color_delta(const phy_color *color, phy_ir_ref a,
                           phy_ir_ref b, phy_ir_ref *out_delta);
phy_status phy_color_structure_constant(const phy_color *color, phy_ir_ref a,
                                        phy_ir_ref b, phy_ir_ref c,
                                        phy_ir_ref *out_f);
phy_status phy_color_symmetric_tensor(const phy_color *color, phy_ir_ref a,
                                      phy_ir_ref b, phy_ir_ref c,
                                      phy_ir_ref *out_d);

/*
 * Contract delta^(ab) against one expression carrying exactly one of a,b.
 *
 * If neither occurs, the product is left explicit.  If both occur, or one
 * occurs more than once, the contraction would need a general dummy-index
 * canonicalizer and is therefore PHY_ERR_UNSUPPORTED instead of a guess.
 */
phy_status phy_color_delta_contract(const phy_color *color, phy_ir_ref a,
                                    phy_ir_ref b, phy_ir_ref expression,
                                    phy_ir_ref *out_expression);

/*
 * Textbook component f^(abc), with a,b,c one-based.
 *
 * Exact built-in tables exist for concrete N=2 and N=3.  A symbolic N or a
 * larger concrete N returns PHY_ERR_UNSUPPORTED; this is not a numerical
 * generator construction.
 */
phy_status phy_color_structure_constant_component(const phy_color *color,
                                                   unsigned a, unsigned b,
                                                   unsigned c,
                                                   phy_ir_ref *out_value);

/* ---------------------------------------------------------- generators */

/* Fundamental T^a as SUNGenerator[a]. */
phy_status phy_color_generator(const phy_color *color, phy_ir_ref a,
                               phy_ir_ref *out_generator);

/*
 * [T^a,T^b] = I f^(abc) T^c.  The contracted c is a fresh colour index owned
 * by this context; I is the exact symbolic imaginary unit.
 */
phy_status phy_color_commutator(phy_color *color, phy_ir_ref a, phy_ir_ref b,
                                phy_ir_ref *out_commutator);

/*
 * Tr(T^(a1)...T^(an)).
 *
 * n=0 -> N, n=1 -> 0, n=2 -> delta/2,
 * n=3 -> (d + I f)/4, n>3 -> inert SUNTrace[N,...].  The explicit first
 * argument keeps a long trace meaningful after the temporary colour context
 * that produced it has been destroyed.
 */
phy_status phy_color_trace(const phy_color *color,
                           const phy_ir_ref *indices, size_t count,
                           phy_ir_ref *out_trace);

/* ------------------------------------------------------------ Casimirs */

/* Raw exact definitions.  These keep N symbolic. */
phy_status phy_color_casimir_fundamental(const phy_color *color,
                                         phy_ir_ref *out_value);
phy_status phy_color_casimir_adjoint(const phy_color *color,
                                     phy_ir_ref *out_value);

/* Presentation atoms C_F and C_A, matching SUNNToCACF -> True. */
phy_ir_ref phy_color_cf_symbol(const phy_color *color);
phy_ir_ref phy_color_ca_symbol(const phy_color *color);

/* Replace every C_F/C_A presentation atom by its raw N expression. */
phy_status phy_color_expand_casimirs(const phy_color *color,
                                     phy_ir_ref expression,
                                     phy_ir_ref *out_expression);

/* T^a T^a = C_F IdentityFundamental and f^(acd)f^(bcd)=C_A delta^(ab). */
phy_status phy_color_fundamental_casimir(const phy_color *color,
                                         phy_ir_ref *out_expression);
phy_status phy_color_adjoint_casimir(const phy_color *color, phy_ir_ref a,
                                     phy_ir_ref b,
                                     phy_ir_ref *out_expression);

#ifdef __cplusplus
}
#endif

#endif /* PHY_COLOR_H */
