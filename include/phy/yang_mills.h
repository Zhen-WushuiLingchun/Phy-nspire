/*
 * Exact, bounded Lie-algebra-valued differential forms and Yang--Mills
 * operations.
 *
 * A phy_lie_form is a dense vector of ordinary differential forms in a fixed
 * Lie-algebra basis.  The exterior algebra, coordinate derivatives, and
 * scalar coefficients are therefore shared with the GR/differential-geometry
 * layer rather than reimplemented here.
 */
#ifndef PHY_YANG_MILLS_H
#define PHY_YANG_MILLS_H

#include "phy/geom.h"
#include "phy/lie.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_lie_form phy_lie_form;

/*
 * Create an algebra-valued p-form on one registered chart.
 *
 * The algebra, manifold, chart, CAS, and every scalar coefficient must share
 * one IR context.  The returned value owns one ordinary p-form per algebra
 * basis element and borrows the algebra and manifold.
 */
phy_status phy_lie_form_create(const phy_lie_algebra *algebra,
                               phy_manifold *manifold,
                               unsigned chart_index, unsigned degree,
                               phy_lie_form **out_form);
void phy_lie_form_destroy(phy_lie_form *form);

const phy_lie_algebra *phy_lie_form_algebra(const phy_lie_form *form);
const phy_manifold *phy_lie_form_manifold(const phy_lie_form *form);
unsigned phy_lie_form_chart_index(const phy_lie_form *form);
unsigned phy_lie_form_degree(const phy_lie_form *form);
unsigned phy_lie_form_algebra_dimension(const phy_lie_form *form);

/*
 * Borrow a color component.  The mutable accessor is intentional: callers
 * construct A^a_mu with the ordinary phy_form_set_at/phy_form_set API.
 * The pointer remains owned by `form`.
 */
const phy_form *phy_lie_form_component(const phy_lie_form *form,
                                       unsigned basis);
phy_form *phy_lie_form_component_mut(phy_lie_form *form, unsigned basis);

phy_status phy_lie_form_copy(const phy_lie_form *form,
                             phy_lie_form **out_form);
phy_status phy_lie_form_add(phy_cas *cas, const phy_lie_form *left,
                            const phy_lie_form *right,
                            phy_lie_form **out_form);
phy_status phy_lie_form_scale(phy_cas *cas, const phy_lie_form *form,
                              phy_ir_ref scalar,
                              phy_lie_form **out_form);
phy_status phy_lie_form_is_zero(phy_cas *cas, const phy_lie_form *form,
                                phy_cas_decision *out_decision);
phy_status phy_lie_form_equivalent(phy_cas *cas,
                                   const phy_lie_form *left,
                                   const phy_lie_form *right,
                                   phy_cas_decision *out_decision);

/*
 * Graded Lie bracket
 *
 *   [alpha,beta]^a = f[b,c,a] alpha^b wedge beta^c .
 *
 * The structure-constant order matches phy_lie_structure_constant.  No 1/2
 * is hidden here; callers forming curvature use (g/2)[A,A].
 */
phy_status phy_lie_form_bracket_wedge(phy_cas *cas,
                                      const phy_lie_form *left,
                                      const phy_lie_form *right,
                                      phy_lie_form **out_form);

/* Componentwise exterior derivative. */
phy_status phy_lie_form_exterior_derivative(phy_cas *cas,
                                             const phy_lie_form *form,
                                             phy_lie_form **out_form);

/*
 * Gauge convention pinned by this API:
 *
 *   D_A omega = d omega + g [A,omega]
 *   F_A       = dA + (g/2) [A,A]
 *   delta A   = D_A alpha
 *   delta F   = g [F,alpha]
 *
 * A is a degree-1 algebra-valued form; alpha is degree 0.
 */
phy_status phy_yang_mills_covariant_derivative(
    phy_cas *cas, const phy_lie_form *connection, phy_ir_ref coupling,
    const phy_lie_form *form, phy_lie_form **out_form);
phy_status phy_yang_mills_field_strength(
    phy_cas *cas, const phy_lie_form *connection, phy_ir_ref coupling,
    phy_lie_form **out_curvature);
phy_status phy_yang_mills_gauge_variation_connection(
    phy_cas *cas, const phy_lie_form *connection, phy_ir_ref coupling,
    const phy_lie_form *parameter, phy_lie_form **out_variation);
phy_status phy_yang_mills_gauge_variation_curvature(
    phy_cas *cas, const phy_lie_form *curvature, phy_ir_ref coupling,
    const phy_lie_form *parameter, phy_lie_form **out_variation);

/*
 * Return D_A F_A as an explicit algebra-valued 3-form.  For coefficients in
 * the scalar CAS decision class, phy_lie_form_is_zero proves the Bianchi
 * identity rather than merely tagging it as true.
 */
phy_status phy_yang_mills_bianchi(
    phy_cas *cas, const phy_lie_form *connection, phy_ir_ref coupling,
    phy_lie_form **out_residual);

/*
 * Invariant quadratic density
 *
 *   L = -1/2 h_ab F^a wedge star_g(F^b)
 *
 * as a top-degree ordinary form.  `bilinear` is a dense row-major d*d table
 * of exact scalar coefficients.  Passing NULL selects the Lie algebra's
 * Killing form; this is intentionally zero for U(1), so Abelian callers
 * normally provide their representation trace form explicitly.
 *
 * For compact non-Abelian algebras in the anti-Hermitian real basis used by
 * this library the Killing form is negative definite (for example,
 * K_ab=-2 delta_ab for SU(2)).  Therefore the NULL default fixes a precise
 * algebraic convention but does not automatically equal the positive
 * representation-trace normalization often used in physics.  Pass the
 * desired invariant trace form explicitly when matching an action
 * convention.
 *
 * The metric is a symmetric covariant rank-2 tensor on the same chart.
 */
phy_status phy_yang_mills_lagrangian(
    phy_cas *cas, const phy_lie_form *curvature, const phy_tensor *metric,
    const phy_ir_ref *bilinear, phy_form **out_lagrangian);

#ifdef __cplusplus
}
#endif

#endif /* PHY_YANG_MILLS_H */
