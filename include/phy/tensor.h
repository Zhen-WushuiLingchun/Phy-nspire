/*
 * Phy-nspire — component tensors on a coordinate chart.
 *
 * The substrate for the Phase 3 curvature pipeline, per
 * docs/agent-tasks/TENSOR_CORE.md. A tensor here is concrete components on a
 * chart of dimension n <= 4, not an abstract-index object: slot position
 * carries meaning, and two components are related only by a declared slot
 * symmetry. Index canonicalization is explicitly not in this layer.
 *
 * WHAT IS AND IS NOT HERE YET
 *
 * This header is the component *storage and symmetry* layer, and it is
 * complete: charts, rank and per-slot valence, dense n^r storage, flat-index
 * encoding, the declared symmetry group, canonical component lookup, fill,
 * and assignment validation. Every entry point below is implemented and
 * tested.
 *
 * The operations that consume expressions -- raising and lowering a slot
 * against a metric, contracting two slots, and componentwise partial
 * differentiation -- are NOT here. They need three capabilities that no
 * shipped header provides: simplification, a decidable zero test, and exact
 * differentiation. docs/agent-tasks/TENSOR_CORE.md originally attributed
 * those to the typed IR; they are not in include/phy/ir.h, which builds and
 * interns expressions but never evaluates them. They are being implemented as
 * the canonical scalar CAS layer, and the index operations land against that
 * header rather than against a stand-in invented here.
 *
 * The boundary is drawn deliberately at negation. Everything this layer does
 * to a component -- storing it, copying it around a symmetry orbit, reporting
 * which independent component it belongs to -- it does without ever forming a
 * new expression. That is why an antisymmetric orbit is represented as a
 * shared handle plus a per-component sign (see phy_tensor_component) instead
 * of as a negated expression: negation is a scalar operation, and this layer
 * has none. See docs/TENSOR.md.
 *
 * ERRORS
 *
 * Typed values, never in-band. The mapping is uniform across the API:
 *
 *   PHY_ERR_INVALID_ARGUMENT  a null pointer, a component index >= dimension,
 *                             a slot >= rank, a repeated slot, a malformed
 *                             permutation, a sign other than +1 or -1;
 *   PHY_ERR_UNSUPPORTED       a well-formed dimension or rank beyond the
 *                             compiled ceilings below -- n = 5 is not wrong,
 *                             it is not implemented;
 *   PHY_ERR_TYPE              a slot's variance rejects the operation;
 *   PHY_ERR_ASSUMPTION        the declared symmetries cannot all hold, or an
 *                             assignment contradicts one;
 *   PHY_ERR_OUT_OF_MEMORY     phy_alloc failed.
 *
 * Every failing call leaves its output parameter untouched and its tensor
 * unmodified. There is no partially-written state to observe after an error.
 */
#ifndef PHY_TENSOR_H
#define PHY_TENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/cas.h"
#include "phy/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compiled ceilings.
 *
 * Both are storage constants rather than tunables. Dimension 4 is the scope
 * of docs/SCIENTIFIC_SCOPE.md; rank 4 is what the Riemann tensor needs. A
 * dense table is therefore at most 4^4 = 256 components, which
 * docs/agent-tasks/TENSOR_CORE.md costs at about 1 KB of handles and
 * deliberately spends rather than building a packed symmetry-aware layout.
 *
 * The symmetry group is bounded by 4! = 24 elements for the same reason, so
 * it is a fixed-size member and needs no allocation.
 */
#define PHY_TENSOR_MAX_DIM 4u
#define PHY_TENSOR_MAX_RANK 4u
#define PHY_TENSOR_MAX_COMPONENTS 256u /* PHY_TENSOR_MAX_DIM^PHY_TENSOR_MAX_RANK */
#define PHY_TENSOR_MAX_SYMMETRIES 24u  /* PHY_TENSOR_MAX_RANK! */

/* Returned by phy_chart_axis_of for a symbol that is not a coordinate. */
#define PHY_CHART_NO_AXIS ((unsigned)-1)

typedef struct phy_chart phy_chart;
typedef struct phy_tensor phy_tensor;

/*
 * A stored component.
 *
 * The component's value is `sign * ref`, and `sign` is one of -1, 0, +1.
 *
 * A sign of 0 means the component is forced to vanish by the declared
 * symmetries -- an index tuple whose stabilizer contains an odd element, such
 * as R_aacd when the first slot pair is antisymmetric. `ref` is then the
 * canonical zero handle.
 *
 * A sign of -1 is how an antisymmetric orbit is stored without negating
 * anything: every component of the orbit shares one handle and differs only
 * in this field. Folding the sign into the expression is a scalar operation
 * and belongs to the caller once a scalar layer exists; doing it here would
 * mean inventing an arithmetic this layer cannot test.
 */
typedef struct {
    phy_ir_ref ref;
    int8_t sign;
} phy_tensor_component;

/* ------------------------------------------------------------------ charts */

/*
 * A coordinate chart: a dimension and one coordinate symbol per axis.
 *
 * `coordinate_names` supplies `dimension` NUL-terminated names, interned into
 * `ir`. Names must be non-empty and distinct; a repeat is
 * PHY_ERR_INVALID_ARGUMENT, because an axis that cannot be told apart from
 * another cannot be differentiated against.
 *
 * `ir` must outlive the chart. The chart holds symbols and refs from it, and
 * those are meaningless anywhere else -- see the note on phy_ir_ref.
 */
phy_status phy_chart_create(phy_ir_context *ir,
                            const char *const *coordinate_names,
                            unsigned dimension, phy_chart **out_chart);
void phy_chart_destroy(phy_chart *chart);

phy_ir_context *phy_chart_ir(const phy_chart *chart);
unsigned phy_chart_dimension(const phy_chart *chart);

/* PHY_IR_NO_SYMBOL / PHY_IR_NULL / NULL for an axis >= dimension. */
phy_ir_symbol phy_chart_coordinate_symbol(const phy_chart *chart,
                                          unsigned axis);
phy_ir_ref phy_chart_coordinate(const phy_chart *chart, unsigned axis);
const char *phy_chart_coordinate_name(const phy_chart *chart, unsigned axis);

/*
 * Axis carrying this symbol, or PHY_CHART_NO_AXIS. This is what a
 * componentwise derivative will use to turn a differentiation variable into
 * an axis, so it is here rather than left to the caller to search.
 */
unsigned phy_chart_axis_of(const phy_chart *chart, phy_ir_symbol symbol);

/* ----------------------------------------------------------------- tensors */

/*
 * A rank-`rank` component tensor on `chart`, with `valence[i]` the variance
 * of slot i. Rank 0 is a scalar and has one component; `valence` may then be
 * NULL.
 *
 * `name` is interned as the tensor's head symbol. It is metadata: this layer
 * never rewrites with it, and two tensors sharing a name are still unrelated
 * objects. It exists so a component tensor can be reported, and so the head
 * is available to the later abstract-index layer that does consume it.
 *
 * Every component starts as the canonical zero handle with sign +1. The dense
 * table is sized from `chart` and `rank` at construction and never grows, so
 * no component loop in this layer allocates.
 *
 * `chart` must outlive the tensor, and the chart's IR context must therefore
 * outlive both. The tensor stores the chart pointer; it does not copy or own
 * the chart.
 */
phy_status phy_tensor_create(phy_chart *chart, const char *name, unsigned rank,
                             const phy_ir_variance *valence,
                             phy_tensor **out_tensor);
void phy_tensor_destroy(phy_tensor *tensor);

phy_chart *phy_tensor_chart(const phy_tensor *tensor);
phy_ir_symbol phy_tensor_head(const phy_tensor *tensor);
const char *phy_tensor_name(const phy_tensor *tensor);
unsigned phy_tensor_rank(const phy_tensor *tensor);
unsigned phy_tensor_dimension(const phy_tensor *tensor);
size_t phy_tensor_component_count(const phy_tensor *tensor); /* n^rank */

/* PHY_IR_INDEX_LOWER for a slot >= rank; check the rank first. */
phy_ir_variance phy_tensor_valence(const phy_tensor *tensor, unsigned slot);

/*
 * The canonical zero handle of the tensor's context. Every vanishing
 * component shares it, which is what keeps a dense table cheap: 256 slots of
 * a mostly-zero tensor cost one interned node, not 256.
 */
phy_ir_ref phy_tensor_zero(const phy_tensor *tensor);

/* ------------------------------------------------------- flat index mapping */

/*
 * Row-major, slot 0 most significant: flat = ((i0 * n) + i1) * n + ...
 *
 * Both directions validate. phy_tensor_flatten rejects any index >=
 * dimension; phy_tensor_unflatten rejects `flat` >= n^rank. At rank 0 the
 * only valid flat index is 0 and `indices` may be NULL.
 *
 * These are public because the curvature pipeline iterates components far
 * more often than it names them, and open-coding the arithmetic at each call
 * site is exactly how an off-by-one reaches a golden comparison.
 */
phy_status phy_tensor_flatten(const phy_tensor *tensor,
                              const unsigned *indices, size_t *out_flat);
phy_status phy_tensor_unflatten(const phy_tensor *tensor, size_t flat,
                                unsigned *out_indices);

/* -------------------------------------------------------------- symmetries */

/*
 * Declare that permuting the slots by `image` reproduces the tensor up to
 * `sign`.
 *
 * `image` is in images notation, matching the convention fixed in
 * docs/references/TENSOR_GEOMETRY.md: image[i] is the slot that slot i maps
 * to. It must be a permutation of 0..rank-1 and `image_count` must equal the
 * rank. `sign` must be +1 or -1.
 *
 * The meaning is: the component whose index tuple is `a` permuted by `image`
 * equals `sign` times the component at `a`. For a rank-2 transposition
 * image = {1, 0} that reads T[a1,a0] = sign * T[a0,a1], which is symmetry at
 * +1 and antisymmetry at -1.
 *
 * Declarations accumulate and the group they generate is closed immediately,
 * so a contradiction is reported at declaration rather than at first use. If
 * the closure contains the identity permutation with sign -1, every component
 * is forced to zero; that is not rejected -- it is a consistent, if useless,
 * tensor -- but phy_tensor_is_identically_zero then reports it.
 *
 * Declaring symmetries after components have been assigned is
 * PHY_ERR_ASSUMPTION: the existing assignments were validated against a
 * different group, and silently reinterpreting them is how a fill discipline
 * turns into a fill bug. Declare first, then assign.
 */
phy_status phy_tensor_declare_permutation(phy_tensor *tensor,
                                          const unsigned *image,
                                          size_t image_count, int sign);

/* Convenience for the overwhelmingly common two-slot case. */
phy_status phy_tensor_declare_slot_symmetry(phy_tensor *tensor,
                                            unsigned slot_a, unsigned slot_b,
                                            phy_ir_symmetry symmetry);

/*
 * The three generators of the Riemann slot group, at rank 4: antisymmetry in
 * (0,1), antisymmetry in (2,3), and exchange of the two pairs. Rank other
 * than 4 is PHY_ERR_INVALID_ARGUMENT.
 *
 * Note this is the *slot* group only. The first Bianchi identity is a cyclic
 * relation among three different components, not a slot permutation, so it is
 * not a symmetry in this sense and does not reduce the independent count
 * here. See docs/TENSOR.md.
 */
phy_status phy_tensor_declare_riemann_symmetry(phy_tensor *tensor);

/* Order of the generated group; 1 when nothing has been declared. */
size_t phy_tensor_symmetry_group_order(const phy_tensor *tensor);

/* True when the declared symmetries force every component to vanish. */
bool phy_tensor_is_identically_zero(const phy_tensor *tensor);

/* ------------------------------------------------- canonical components */

/*
 * The canonical representative of the orbit containing `indices`, and the
 * sign relating them: component[indices] == sign * component[canonical].
 *
 * The representative is the lexicographically least tuple in the orbit, which
 * for row-major storage is the least flat index. `out_sign` receives 0 when
 * the component is forced to vanish.
 *
 * `out_indices` may be NULL if only the sign is wanted, and vice versa.
 */
phy_status phy_tensor_canonical(const phy_tensor *tensor,
                                const unsigned *indices,
                                unsigned *out_indices, int *out_sign);

/* True when `indices` is its own orbit representative and does not vanish. */
bool phy_tensor_is_canonical(const phy_tensor *tensor,
                             const unsigned *indices);

/*
 * How many independent components the declared symmetries leave: the number
 * of orbits that are not forced to vanish.
 *
 * For a tensor carrying the Riemann slot symmetries this is N(N+1)/2 with
 * N = n(n-1)/2 -- 21 at n = 4, and 1 at n = 2. The familiar
 * n^2(n^2-1)/12 = 20 additionally uses the first Bianchi identity, which is
 * not a slot symmetry; the two agree at n = 2 and differ at n = 4.
 */
size_t phy_tensor_independent_count(const phy_tensor *tensor);

/* ---------------------------------------------------- component access */

/*
 * Read a component. The value is `out_component->sign * out_component->ref`;
 * see phy_tensor_component.
 */
phy_status phy_tensor_get(const phy_tensor *tensor, const unsigned *indices,
                          phy_tensor_component *out_component);
phy_status phy_tensor_get_flat(const phy_tensor *tensor, size_t flat,
                               phy_tensor_component *out_component);

/*
 * Assign a component, filling its whole symmetry orbit.
 *
 * `value` must be a live ref in the chart's context; PHY_IR_NULL is
 * PHY_ERR_INVALID_ARGUMENT. Every component of the orbit is written with the
 * same handle and its own sign, so one call populates all the dependents that
 * docs/agent-tasks/TENSOR_CORE.md requires a fill discipline to produce.
 *
 * Validation. Assigning into an orbit that already holds an assignment is
 * PHY_ERR_ASSUMPTION unless the result is identical, and assigning a non-zero
 * handle to a component the symmetries force to vanish is PHY_ERR_ASSUMPTION.
 * The whole orbit is checked before anything is written, so a rejected
 * assignment leaves the tensor exactly as it was.
 *
 * "Identical" here is structural: the IR interns, so equal handles are exact
 * structural equality. It is not equality up to simplification, which needs
 * the scalar layer; two spellings of the same value are currently a conflict.
 * Clear the component to reassign it.
 *
 * A phy_ir_ref is context-local, but its compact numeric representation does
 * not carry a context identity. Passing a ref from another context is
 * therefore a caller-contract violation that this API cannot reliably detect.
 */
phy_status phy_tensor_set(phy_tensor *tensor, const unsigned *indices,
                          phy_ir_ref value);
phy_status phy_tensor_set_flat(phy_tensor *tensor, size_t flat,
                               phy_ir_ref value);

/* True once the orbit containing `indices` has been assigned. */
bool phy_tensor_is_assigned(const phy_tensor *tensor,
                            const unsigned *indices);

/* Return an orbit to the canonical zero handle and mark it unassigned. */
phy_status phy_tensor_clear_component(phy_tensor *tensor,
                                      const unsigned *indices);

/* Return every component to the canonical zero handle. Symmetries survive. */
void phy_tensor_clear(phy_tensor *tensor);

/*
 * Re-derive every dependent component from its orbit representative.
 *
 * Assignment already fills, so this is a no-op on a tensor built through
 * phy_tensor_set. It exists for the test suite, which needs to be able to
 * assert that the redundant entries of a dense table agree rather than to
 * take it on trust, and for a later loader that populates independent
 * components directly.
 */
phy_status phy_tensor_fill_symmetries(phy_tensor *tensor);

/*
 * Check that every component of the dense table agrees with every declared
 * symmetry. PHY_ERR_ASSUMPTION on the first disagreement.
 *
 * O(group order * n^rank), so at most 24 * 256 comparisons. This is the
 * "symmetry fill agreement" check of docs/agent-tasks/TENSOR_CORE.md, and it
 * is an API rather than test-local code because a caller that populates
 * components by hand needs the same gate.
 */
phy_status phy_tensor_check_symmetries(const phy_tensor *tensor);

/* ------------------------------------------------ symbolic index operations */

/* Fold the storage sign into one simplified scalar expression. */
phy_status phy_tensor_component_expression(
    phy_cas *cas, const phy_tensor *tensor, const unsigned *indices,
    phy_ir_ref *out_expression);

/* Exact determinant of the component matrix of a rank-2 tensor. */
phy_status phy_tensor_determinant(phy_cas *cas, const phy_tensor *matrix,
                                  phy_ir_ref *out_determinant);

/*
 * Deterministic cofactor/adjugate inverse of a symmetric lower metric.
 * A determinant proved zero is PHY_ERR_DOMAIN. An undecidable symbolic
 * determinant is accepted under the CAS generic-domain convention.
 */
phy_status phy_tensor_inverse_metric(phy_cas *cas, const phy_tensor *metric,
                                     const char *name,
                                     phy_tensor **out_inverse);

phy_status phy_tensor_raise_slot(phy_cas *cas, const phy_tensor *tensor,
                                 unsigned slot,
                                 const phy_tensor *inverse_metric,
                                 const char *name, phy_tensor **out_tensor);

phy_status phy_tensor_lower_slot(phy_cas *cas, const phy_tensor *tensor,
                                 unsigned slot, const phy_tensor *metric,
                                 const char *name, phy_tensor **out_tensor);

/* Contract one upper and one lower slot, reducing rank by two. */
phy_status phy_tensor_contract(phy_cas *cas, const phy_tensor *tensor,
                               unsigned slot_a, unsigned slot_b,
                               const char *name, phy_tensor **out_tensor);

/* Differentiate every component with respect to one chart coordinate. */
phy_status phy_tensor_partial(phy_cas *cas, const phy_tensor *tensor,
                              unsigned coordinate_axis, const char *name,
                              phy_tensor **out_tensor);

/*
 * Put every component in normal form for the next stage to read, and report
 * how many were proved zero through `out_zeroed` (which may be NULL).
 *
 * Two exact steps per component, in order:
 *
 *   1. phy_cas_expand_factored() -- distribute, collect, and leave the
 *      denominators factored, so the stored form is a sum of products of
 *      powers rather than the nest of quotients the arithmetic built;
 *   2. phy_cas_is_zero() -- a component PROVED zero is replaced by the
 *      canonical zero handle. This is a proof, not a heuristic: a component
 *      the decision answers UNKNOWN or NONZERO on keeps its expanded value.
 *
 * Neither step changes what a component denotes. Both are what
 * docs/agent-tasks/GR_CURVATURE.md means by simplifying after each stage
 * rather than at the end, and between them they are the difference between
 * the golden corpus reproducing and the pipeline overflowing on it:
 *
 *   - a component that EQUALS zero is not the same as one that IS the zero
 *     handle. The inverse of a diagonal metric has off-diagonal cofactors that
 *     reduce to zero but arrive as sizeable rational expressions, and every
 *     later contraction sums and multiplies them;
 *   - a component carried as an unexpanded nest re-derives that nest at every
 *     later stage. Reissner-Nordström, the corpus's sizing case, does not
 *     complete without step 1.
 *
 * Deliberately NOT folded into raise/lower/contract/partial: it costs an
 * expansion and a zero decision per component, and a caller doing one
 * operation should not pay for it silently. The curvature pipeline calls it
 * explicitly after every stage.
 *
 * A failed decision -- budget, overflow, memory -- is returned unchanged.
 * Components normalized before the failure keep their normalized value, which
 * denotes what it did before; there is no partial state that means something
 * else.
 */
phy_status phy_tensor_normalize(phy_cas *cas, phy_tensor *tensor,
                                size_t *out_zeroed);

#ifdef __cplusplus
}
#endif

#endif /* PHY_TENSOR_H */
