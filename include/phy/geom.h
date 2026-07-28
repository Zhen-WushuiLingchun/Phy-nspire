/*
 * Phy-nspire — manifolds and differential forms.
 *
 * The differential-geometry half of docs/SCIENTIFIC_SCOPE.md section 3:
 * typed manifolds carrying orientation and signature, differential forms in a
 * chart's coordinate coframe, and the four exterior operations — wedge
 * product, exterior derivative, interior product, and Hodge dual.
 *
 * This layer sits on three that already exist and introduces no fourth. A
 * manifold borrows a phy_chart from include/phy/tensor.h; a chart borrows a
 * phy_ir_context; every scalar this file produces is built by phy_cas_* from
 * include/phy/cas.h. There is no expression system here, no arithmetic of its
 * own, and no floating point: a form component is a phy_ir_ref like any other,
 * and "the wedge product is exact" means only that the CAS that computed it
 * is.
 *
 * WHAT A FORM IS HERE
 *
 * A degree-p form on an n-dimensional chart, stored as its C(n,p) strictly
 * increasing components in the coordinate coframe:
 *
 *     alpha = sum over i_0 < ... < i_{p-1} of alpha_{i_0...i_{p-1}}
 *             dx^{i_0} ^ ... ^ dx^{i_{p-1}}
 *
 * Antisymmetry is therefore not a declared symmetry that a fill discipline
 * maintains, as it is for phy_tensor: it is the storage. A component with a
 * repeated index does not exist, and a component whose indices are out of
 * order is reached through phy_form_get, which applies the sort parity. At
 * n = 4 the largest table is C(4,2) = 6 handles, so it is a fixed inline
 * member and a form is exactly one allocation.
 *
 * THE COFRAME, AND WHAT THE SIGNATURE MEANS
 *
 * Components are in the *coordinate* coframe dx^0 ... dx^{n-1} of the named
 * chart. `phy_form_hodge` is the cheap orthonormal-coframe operation for the
 * diagonal signature declared by the manifold,
 *
 *     g = sum_a sigma_a (dx^a ⊗ dx^a),   sigma_a in {-1, +1},
 *
 * which is what phy_form_hodge raises indices with. For a general symmetric
 * coordinate metric, `phy_form_hodge_metric` uses the tensor layer's exact
 * inverse and determinant and retains sqrt(|det g|) symbolically. The two APIs
 * keep the flat/orthonormal fast path while making curvilinear GR charts and
 * Yang--Mills action densities first-class.
 *
 * The wedge product, the exterior derivative and the interior product need no
 * metric at all and carry no such restriction.
 *
 * NOT HERE: PULLBACK
 *
 * phy_manifold registers more than one chart, but it does not relate them.
 * There are no transition maps, and there is therefore no pullback: F*(dy^a)
 * is sum_j (d phi^a / d x^j) dx^j, which needs a phi that is a *validated*
 * map object rather than a bag of expressions. Two requirements make that a
 * real design rather than a call to phy_cas_substitute:
 *
 *   - the source and target coordinate symbols must be provably disjoint, or
 *     substituting the target coordinates captures the source ones and
 *     silently returns a wrong answer;
 *   - the map's components must be validated against the source chart before
 *     any derivative is taken, so that a malformed map is a typed error and
 *     not an unevaluated PHY_IR_DERIVATIVE hiding in a result.
 *
 * Until a phy_map abstraction supplies both, every operation below requires
 * its operands to name the same chart, and mixing charts is PHY_ERR_TYPE
 * rather than an implicit and unjustified identification. See docs/GEOMETRY.md.
 *
 * ERRORS
 *
 * Typed values, never in-band, and uniform across the API:
 *
 *   PHY_ERR_INVALID_ARGUMENT  a null pointer, an axis or position out of
 *                             range, a signature entry other than +1 or -1, an
 *                             unknown orientation, a chart already registered,
 *                             a chart from another IR context, a CAS over
 *                             another IR context, a degree greater than the
 *                             dimension;
 *   PHY_ERR_UNSUPPORTED       a well-formed dimension or chart count beyond
 *                             the compiled ceilings below;
 *   PHY_ERR_TYPE              operands disagree on manifold, chart, or degree,
 *                             or a vector field is not a rank-1 contravariant
 *                             tensor;
 *   PHY_ERR_DOMAIN            the result's degree leaves 0..n. The value is
 *                             zero, not undefined — see phy_form_wedge;
 *   PHY_ERR_ASSUMPTION        the Hodge dual or volume form was asked of an
 *                             unoriented manifold;
 *   PHY_ERR_OUT_OF_MEMORY     phy_alloc failed;
 *   PHY_ERR_TIMEOUT,
 *   PHY_ERR_INTERRUPTED,
 *   PHY_ERR_OVERFLOW, ...     propagated unchanged from the CAS.
 *
 * Every failing call leaves its output parameter untouched, destroys any
 * partially built result, and modifies no operand. There is no half-written
 * form to observe after an error.
 *
 * OWNERSHIP
 *
 * A form borrows its manifold, a manifold borrows its charts, and a chart
 * borrows its IR context. Destroy forms first, then manifolds, then charts,
 * then the context. A phy_cas is passed per call and is never retained.
 */
#ifndef PHY_GEOM_H
#define PHY_GEOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/phy.h"
#include "phy/tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compiled ceilings.
 *
 * The dimension ceiling is the chart's, restated rather than re-chosen: a
 * manifold's charts are phy_chart objects and cannot exceed it.
 *
 * PHY_FORM_MAX_COMPONENTS is max over p of C(4,p) = C(4,2) = 6. It is what
 * makes the component table a fixed inline member: a form is one allocation
 * of a fixed size, and no operation in this layer allocates per component.
 *
 * The chart ceiling is a storage constant, not a policy. Charts are registered
 * and validated, never related, so more of them buys nothing until a map
 * abstraction exists.
 */
#define PHY_MANIFOLD_MAX_DIM PHY_TENSOR_MAX_DIM /* 4 */
#define PHY_MANIFOLD_MAX_CHARTS 4u
#define PHY_FORM_MAX_DEGREE PHY_MANIFOLD_MAX_DIM
#define PHY_FORM_MAX_COMPONENTS 6u /* max_p C(PHY_MANIFOLD_MAX_DIM, p) */

typedef struct phy_manifold phy_manifold;
typedef struct phy_form phy_form;

/*
 * Whether a volume form has been chosen, and which one.
 *
 * PHY_ORIENTATION_POSITIVE declares dx^0 ^ ... ^ dx^{n-1} positively oriented
 * in every registered chart; PHY_ORIENTATION_NEGATIVE declares the opposite.
 * PHY_ORIENTATION_NONE is an unoriented manifold, and it is zero-valued for
 * the same reason PHY_CAS_UNKNOWN is: a zeroed structure must mean "nothing
 * declared", never "positively oriented by default".
 *
 * The Hodge dual and the volume form are the only operations that read this.
 * Both are PHY_ERR_ASSUMPTION on an unoriented manifold, because *alpha is not
 * merely unknown there — it does not exist.
 */
typedef enum {
    PHY_ORIENTATION_NONE = 0,
    PHY_ORIENTATION_POSITIVE,
    PHY_ORIENTATION_NEGATIVE
} phy_orientation;

/* ------------------------------------------------------------- manifolds */

/*
 * Create a manifold.
 *
 * `name` is interned as the manifold's head symbol and is metadata: nothing
 * here rewrites with it, and two manifolds sharing a name stay unrelated
 * objects.
 *
 * `signature` supplies `dimension` entries, each +1 or -1, giving the
 * diagonal of the metric in the coordinate coframe of every registered chart.
 * NULL means all +1, the Riemannian case. Anything other than +1 or -1 is
 * PHY_ERR_INVALID_ARGUMENT: this layer represents a signature, not a metric,
 * and a signature entry has no other admissible value.
 *
 * Two properties of the signature are worth separating, because they are not
 * equally strong. The *inertia* (p, q) — how many entries are positive and how
 * many negative — is genuine manifold metadata and is chart-independent by
 * Sylvester's law. The claim that a particular chart's coordinate coframe
 * realizes it as a diagonal of exactly these signs, in this axis order, is a
 * caller contract asserted at phy_manifold_add_chart and, with no transition
 * maps in this layer, one that cannot be verified here. State it truthfully;
 * only phy_form_hodge and phy_form_volume depend on it.
 *
 * The manifold registers no chart. Add at least one before creating a form.
 */
phy_status phy_manifold_create(phy_ir_context *ir, const char *name,
                               unsigned dimension, phy_orientation orientation,
                               const int8_t *signature,
                               phy_manifold **out_manifold);
void phy_manifold_destroy(phy_manifold *manifold);

phy_ir_context *phy_manifold_ir(const phy_manifold *manifold);
phy_ir_symbol phy_manifold_head(const phy_manifold *manifold);
const char *phy_manifold_name(const phy_manifold *manifold);
unsigned phy_manifold_dimension(const phy_manifold *manifold);
phy_orientation phy_manifold_orientation(const phy_manifold *manifold);

/* Diagonal metric entry on `axis`: +1 or -1, or 0 for an axis >= dimension. */
int phy_manifold_signature(const phy_manifold *manifold, unsigned axis);

/* q, the number of negative signature entries. 0 is Riemannian, 1 Lorentzian. */
unsigned phy_manifold_negative_count(const phy_manifold *manifold);

/*
 * sign(det g) = (-1)^q. This is the factor that makes the Hodge dual square to
 * +1 on 1-forms in Lorentzian 2D and to -1 in Euclidean 2D, so it is exposed
 * rather than left for a caller to recompute from the signature.
 */
int phy_manifold_metric_sign(const phy_manifold *manifold);

/*
 * Register a chart. The chart is borrowed and must outlive the manifold.
 *
 * Validated: the chart's dimension must equal the manifold's, its IR context
 * must be the manifold's, and it must not already be registered. Registering
 * asserts that the chart's coordinate coframe is orthonormal for the declared
 * signature; see phy_manifold_create.
 *
 * `out_index` receives the chart's index, which is what a form names. It may
 * be NULL.
 */
phy_status phy_manifold_add_chart(phy_manifold *manifold, phy_chart *chart,
                                  unsigned *out_index);

unsigned phy_manifold_chart_count(const phy_manifold *manifold);
phy_chart *phy_manifold_chart(const phy_manifold *manifold, unsigned index);

/* --------------------------------------------------------------- forms */

/*
 * A degree-`degree` form on chart `chart_index` of `manifold`.
 *
 * `degree` runs 0..dimension. Beyond that Lambda^p is the zero vector space
 * and this layer has no object for it, so a larger degree is
 * PHY_ERR_INVALID_ARGUMENT rather than a form that can hold nothing.
 *
 * `name` may be NULL, and is NULL for every form an operation below returns.
 * That is deliberate: a curvature loop forming thousands of intermediates must
 * not intern thousands of head symbols into a context that never collects
 * them. A name is metadata for forms a caller builds and reports.
 *
 * Every component starts as the canonical zero handle.
 */
phy_status phy_form_create(phy_manifold *manifold, unsigned chart_index,
                           const char *name, unsigned degree,
                           phy_form **out_form);
void phy_form_destroy(phy_form *form);

const phy_manifold *phy_form_manifold(const phy_form *form);
const phy_chart *phy_form_chart(const phy_form *form);
unsigned phy_form_chart_index(const phy_form *form);
phy_ir_symbol phy_form_head(const phy_form *form); /* PHY_IR_NO_SYMBOL if unnamed */
const char *phy_form_name(const phy_form *form);   /* NULL if unnamed */
unsigned phy_form_degree(const phy_form *form);
unsigned phy_form_dimension(const phy_form *form);

/* C(n, degree); 1 at degree 0 and at degree n. */
size_t phy_form_component_count(const phy_form *form);

/* The canonical zero handle every unassigned component shares. */
phy_ir_ref phy_form_zero(const phy_form *form);

/* ------------------------------------------------- canonical components */

/*
 * Positions enumerate the strictly increasing index tuples in lexicographic
 * order: at n = 3, degree 2, position 0 is (0,1), position 1 is (0,2), and
 * position 2 is (1,2).
 *
 * These are public for the same reason phy_tensor_flatten is: a caller
 * iterates components far more often than it names them, and open-coding the
 * enumeration at each call site is how an off-by-one reaches a golden
 * comparison.
 */
phy_status phy_form_position_indices(const phy_form *form, size_t position,
                                     unsigned *out_indices);

/*
 * Position of an arbitrary index tuple, and the sort parity relating them:
 * alpha_{indices} == sign * alpha_{sorted}.
 *
 * `out_sign` receives 0 when an index repeats, which is the one case where the
 * component vanishes identically rather than naming a stored slot; *out_position
 * is then left untouched. Either output may be NULL.
 */
phy_status phy_form_position_of(const phy_form *form, const unsigned *indices,
                                size_t *out_position, int *out_sign);

/* -------------------------------------------------------- component access */

/*
 * Read and write a stored component by position. `value` is simplified before
 * it is stored, so every component of a form is in the CAS normal form and
 * phy_form_equivalent below compares like with like.
 */
phy_status phy_form_get_at(const phy_form *form, size_t position,
                           phy_ir_ref *out_ref);
phy_status phy_form_set_at(phy_cas *cas, phy_form *form, size_t position,
                           phy_ir_ref value);

/*
 * Read and write by index tuple, applying the sort parity.
 *
 * Writing through a repeated index is PHY_ERR_ASSUMPTION unless the value
 * simplifies to zero: alpha_{aab} is zero by antisymmetry, so an assignment
 * that says otherwise contradicts the storage rather than filling it. Reading
 * one yields the canonical zero.
 */
phy_status phy_form_get(phy_cas *cas, const phy_form *form,
                        const unsigned *indices, phy_ir_ref *out_ref);
phy_status phy_form_set(phy_cas *cas, phy_form *form, const unsigned *indices,
                        phy_ir_ref value);

/* Return every component to the canonical zero handle. */
void phy_form_clear(phy_form *form);

/* An independent copy on the same manifold and chart. Unnamed. */
phy_status phy_form_copy(const phy_form *form, phy_form **out_form);

/* ----------------------------------------------------- linear structure */

/*
 * alpha + beta and s * alpha, componentwise and simplified. Operands must
 * agree on manifold, chart, and degree; `scalar` is any expression in the
 * manifold's context.
 */
phy_status phy_form_add(phy_cas *cas, const phy_form *left,
                        const phy_form *right, phy_form **out_form);
phy_status phy_form_sub(phy_cas *cas, const phy_form *left,
                        const phy_form *right, phy_form **out_form);
phy_status phy_form_scale(phy_cas *cas, const phy_form *form,
                          phy_ir_ref scalar, phy_form **out_form);

/* ------------------------------------------------------- the decisions */

/*
 * Decide whether every component vanishes, and whether two forms are equal,
 * through phy_cas_is_zero and phy_cas_equivalent respectively.
 *
 * The combination rule is the conservative one. PHY_CAS_ZERO means every
 * component was *proved* zero (respectively equal); PHY_CAS_NONZERO means at
 * least one was proved otherwise; PHY_CAS_UNKNOWN means no component was
 * proved nonzero but at least one left the decidable class. A form is never
 * reported as vanishing because a component could not be decided.
 *
 * phy_form_equivalent follows phy_cas_equivalent's spelling, where
 * PHY_CAS_ZERO is the affirmative answer: it decides left - right.
 */
phy_status phy_form_is_zero(phy_cas *cas, const phy_form *form,
                            phy_cas_decision *out_decision);
phy_status phy_form_equivalent(phy_cas *cas, const phy_form *left,
                               const phy_form *right,
                               phy_cas_decision *out_decision);

/* ------------------------------------------------ exterior calculus */

/*
 * Exterior product, degree p + q.
 *
 * Convention, fixed here so that this layer, the tests, and any oracle cannot
 * disagree silently: on strictly increasing components,
 *
 *     (alpha ^ beta)_K = sum over the shuffles I ⊔ J = K of
 *                        sgn(I,J) alpha_I beta_J
 *
 * with I increasing of length p, J increasing of length q, and sgn(I,J) the
 * parity of the permutation carrying the concatenation (I, J) to K. This is
 * the determinant convention — (alpha ^ beta)_{01} = alpha_0 beta_1 -
 * alpha_1 beta_0 for two 1-forms — and it is the one SageManifolds and Cadabra
 * use, so a disagreement with either is a real bug rather than a normalization
 * mismatch.
 *
 * It makes the product associative and graded-commutative,
 * alpha ^ beta = (-1)^{pq} beta ^ alpha, both of which the test suite checks
 * rather than assumes.
 *
 * p + q > n is PHY_ERR_DOMAIN. The product is *zero* there, not undefined:
 * Lambda^{p+q} is the zero space, this layer has no object of that degree, and
 * a caller is told rather than handed a degenerate one. Compare the degrees
 * against phy_form_dimension first if that case is reachable.
 */
phy_status phy_form_wedge(phy_cas *cas, const phy_form *left,
                          const phy_form *right, phy_form **out_form);

/*
 * Exterior derivative, degree p + 1:
 *
 *     (d alpha)_{i_0...i_p} = sum_m (-1)^m d/dx^{i_m} alpha_{i_0...^i_m...i_p}
 *
 * Components are differentiated with phy_cas_diff against the chart's
 * coordinate symbols, so a component the CAS cannot see through becomes an
 * unevaluated PHY_IR_DERIVATIVE rather than a wrong zero — the distinction
 * include/phy/cas.h draws, propagated unchanged.
 *
 * WHERE d(d alpha) = 0 HOLDS, AND WHERE IT DOES NOT. It holds exactly where
 * the CAS can take the mixed partials: components built from exact numbers,
 * coordinates, +, *, integer powers, and any scalar function with an exact
 * derivative rule in include/phy/cas.h. There d^2 alpha is componentwise zero
 * on the nose, and the test suite checks it at every degree from n = 2 to
 * n = 4.
 *
 * It does NOT hold for a component the CAS defers on — an unknown function, a
 * tensor, an operator. phy_cas_diff appends variables to a PHY_IR_DERIVATIVE
 * in the order they are applied and the IR preserves that order, by design:
 * mixed partials commute only for sufficiently smooth expressions, and
 * include/phy/ir.h places that assumption in the rewriter rather than in
 * construction. So d^2 of such a component is a difference of two derivative
 * nodes that differ only in variable order, which is not structurally zero and
 * which phy_form_is_zero reports as PHY_CAS_UNKNOWN. That is the honest
 * answer, not a bug in this layer, and it is what the suite pins. Closing it
 * needs a declared smoothness assumption and a canonical variable order in the
 * CAS; neither exists yet, and neither belongs here.
 *
 * degree == n is PHY_ERR_DOMAIN, for the reason given at phy_form_wedge.
 */
phy_status phy_form_exterior_derivative(phy_cas *cas, const phy_form *form,
                                        phy_form **out_form);

/*
 * Interior product with a contravariant vector field, degree p - 1:
 *
 *     (iota_v alpha)_{j_1...j_{p-1}} = v^i alpha_{i j_1 ... j_{p-1}}
 *
 * `vector` is a rank-1 phy_tensor with an upper slot on the *same chart*;
 * anything else is PHY_ERR_TYPE. Reusing phy_tensor rather than inventing a
 * vector type is what keeps its component storage, its zero handle, and its
 * validation shared — a vector field is a rank-1 tensor and this layer has no
 * reason to disagree.
 *
 * The result is an antiderivation of degree -1: iota_v iota_v = 0 and
 * iota_v(alpha ^ beta) = (iota_v alpha) ^ beta + (-1)^p alpha ^ (iota_v beta).
 *
 * degree == 0 is PHY_ERR_DOMAIN.
 */
phy_status phy_form_interior_product(phy_cas *cas, const phy_form *form,
                                     const phy_tensor *vector,
                                     phy_form **out_form);

/*
 * Lie derivative of a differential form along a vector field, evaluated
 * through Cartan's formula:
 *
 *     L_v alpha = d(iota_v alpha) + iota_v(d alpha).
 *
 * At degree zero the first term is identically absent; at top degree the
 * second is identically absent. Handling those boundary degrees explicitly is
 * important because this API represents no degree -1 or n+1 zero object.
 *
 * The vector contract is the same as phy_form_interior_product: a rank-1
 * contravariant tensor on the form's chart. The result has the same degree as
 * the input and is exact wherever the scalar CAS can differentiate the
 * components. A deferred derivative stays deferred rather than being guessed
 * to commute or vanish.
 */
phy_status phy_form_lie_derivative(phy_cas *cas, const phy_form *form,
                                   const phy_tensor *vector,
                                   phy_form **out_form);

/*
 * Hodge dual, degree n - p, with respect to the declared signature and
 * orientation:
 *
 *     (*alpha)_J = orientation * sgn(I,J) * (prod_{i in I} sigma_i) * alpha_I
 *
 * where J is a strictly increasing (n-p)-tuple, I its increasing complement in
 * 0..n-1, and sgn(I,J) the parity of (I, J) as a permutation of (0,...,n-1).
 * The sigma factors are the index raising alpha^I = (prod sigma_i) alpha_I that
 * a diagonal metric makes a per-index sign; sqrt(|det g|) is 1 in an
 * orthonormal coframe and does not appear.
 *
 * The consequence worth testing, and the one the suite does test in both
 * Euclidean and Lorentzian 2D, is
 *
 *     ** = (-1)^{p(n-p)} sign(det g)   on degree-p forms,
 *
 * with sign(det g) = phy_manifold_metric_sign. Orientation cancels, as it must.
 *
 * PHY_ERR_ASSUMPTION on an unoriented manifold. This operation, and only this
 * operation together with phy_form_volume, depends on the coframe being
 * orthonormal for the declared signature; see the note at
 * phy_manifold_create.
 */
phy_status phy_form_hodge(phy_cas *cas, const phy_form *form,
                          phy_form **out_form);

/*
 * General coordinate-metric Hodge dual:
 *
 *     (*alpha)_J = orientation * sqrt(|det g|) * epsilon_{I J} alpha^I
 *     alpha^I    = g^{i_1 k_1} ... g^{i_p k_p} alpha_K .
 *
 * `metric` must be a symmetric rank-2 covariant tensor on the form's chart.
 * Its signature is the manifold's declared inertia; that declaration supplies
 * sign(det g), so |det g| is formed exactly without a numerical sign guess.
 * A singular metric is PHY_ERR_DOMAIN, a metric not proved symmetric is
 * PHY_ERR_ASSUMPTION, and an unoriented manifold is PHY_ERR_ASSUMPTION.
 *
 * No radicals are approximated. sqrt(|det g|) remains an exact rational power
 * when the scalar CAS cannot reduce it.
 */
phy_status phy_form_hodge_metric(phy_cas *cas, const phy_form *form,
                                 const phy_tensor *metric,
                                 phy_form **out_form);

/*
 * The Riemannian/pseudo-Riemannian volume form of degree n:
 * orientation * dx^0 ^ ... ^ dx^{n-1}, which is *1.
 *
 * PHY_ERR_ASSUMPTION on an unoriented manifold.
 */
phy_status phy_form_volume(phy_cas *cas, phy_manifold *manifold,
                           unsigned chart_index, phy_form **out_form);

/* General-metric volume form: orientation * sqrt(|det g|) d^n x. */
phy_status phy_form_volume_metric(phy_cas *cas, phy_manifold *manifold,
                                  unsigned chart_index,
                                  const phy_tensor *metric,
                                  phy_form **out_form);

#ifdef __cplusplus
}
#endif

#endif /* PHY_GEOM_H */
