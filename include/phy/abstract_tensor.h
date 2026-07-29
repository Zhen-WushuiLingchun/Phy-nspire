/*
 * Phy-nspire — coordinate-independent tensor object model.
 *
 * This layer records typed index spaces, tensor heads and signed slot
 * generators.  It never allocates coordinate components; rank and dimension
 * are runtime metadata bounded by explicit resource limits.
 */
#ifndef PHY_ABSTRACT_TENSOR_H
#define PHY_ABSTRACT_TENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_abstract_context phy_abstract_context;
typedef struct phy_index_space phy_index_space;
typedef struct phy_abstract_tensor_head phy_abstract_tensor_head;
typedef struct phy_tensor_monomial phy_tensor_monomial;
typedef struct phy_tensor_expression phy_tensor_expression;

typedef enum {
    PHY_METRIC_NONE = 0,
    PHY_METRIC_SYMMETRIC,
    PHY_METRIC_ANTISYMMETRIC
} phy_metric_symmetry;

typedef enum {
    PHY_TENSOR_COMMUTING = 0,
    PHY_TENSOR_NONCOMMUTING
} phy_tensor_commutation;

typedef struct {
    size_t max_spaces;     /* default 32 */
    size_t max_heads;      /* default 128 */
    size_t max_slots;      /* slots in one factor; default 64 */
    size_t max_generators; /* declared generators per head; default 256 */
    size_t max_factors;    /* factors in one monomial; default 64 */
    size_t max_indices;    /* total slots in one monomial; default 256 */
    size_t max_bytes;      /* persistent abstract metadata; default 512 KiB */
} phy_abstract_limits;

void phy_abstract_limits_defaults(phy_abstract_limits *out_limits);
phy_status phy_abstract_context_create(phy_cas *cas,
                                       const phy_abstract_limits *limits,
                                       phy_abstract_context **out_context);
void phy_abstract_context_destroy(phy_abstract_context *context);

phy_cas *phy_abstract_cas(const phy_abstract_context *context);
size_t phy_abstract_space_count(const phy_abstract_context *context);
size_t phy_abstract_head_count(const phy_abstract_context *context);
size_t phy_abstract_bytes_used(const phy_abstract_context *context);

/*
 * `dimension` is PHY_IR_NULL (unknown), a positive exact integer, or a symbol.
 * Symbolic dimensions remain abstract until a component basis is supplied.
 */
phy_status phy_index_space_create(phy_abstract_context *context,
                                  const char *name, phy_ir_ref dimension,
                                  phy_metric_symmetry metric,
                                  phy_index_space **out_space);
const char *phy_index_space_name(const phy_index_space *space);
phy_abstract_context *phy_index_space_context(
    const phy_index_space *space);
phy_ir_symbol phy_index_space_symbol(const phy_index_space *space);
phy_ir_ref phy_index_space_dimension(const phy_index_space *space);
bool phy_index_space_known_dimension(const phy_index_space *space,
                                     size_t *out_dimension);
phy_metric_symmetry phy_index_space_metric(const phy_index_space *space);

/*
 * A head owns only slot metadata and symmetry generators.  It has no component
 * table.  `slot_spaces` contains one borrowed space from the same context per
 * slot and may be NULL only at rank zero.
 */
phy_status phy_tensor_head_create(
    phy_abstract_context *context, const char *name,
    const phy_index_space *const *slot_spaces, size_t slot_count,
    phy_tensor_commutation commutation, phy_abstract_tensor_head **out_head);
const char *phy_tensor_head_name(const phy_abstract_tensor_head *head);
phy_abstract_context *phy_tensor_head_context(
    const phy_abstract_tensor_head *head);
phy_ir_symbol phy_tensor_head_symbol(const phy_abstract_tensor_head *head);
size_t phy_tensor_head_slot_count(const phy_abstract_tensor_head *head);
const phy_index_space *phy_tensor_head_slot_space(const phy_abstract_tensor_head *head,
                                                  size_t slot);
phy_tensor_commutation phy_tensor_head_commutation(
    const phy_abstract_tensor_head *head);

/*
 * Add a signed generator in image notation: image[i] is the destination of
 * slot i.  `sign` is +1 or -1.  The full symmetry group is deliberately not
 * enumerated here; the bounded BSGS layer consumes these generators.
 */
phy_status phy_tensor_head_add_symmetry(phy_abstract_tensor_head *head,
                                        const uint16_t *image, int sign);

/*
 * Transactional constructor for a fully declared head. `images[g]` is one
 * signed generator in the same image notation as
 * phy_tensor_head_add_symmetry. If any generator is invalid or any allocation
 * fails, no head remains registered in `context`.
 */
phy_status phy_tensor_head_create_with_symmetries(
    phy_abstract_context *context, const char *name,
    const phy_index_space *const *slot_spaces, size_t slot_count,
    phy_tensor_commutation commutation,
    const uint16_t *const *images, const int *signs,
    size_t generator_count, phy_abstract_tensor_head **out_head);

size_t phy_tensor_head_symmetry_count(const phy_abstract_tensor_head *head);
phy_status phy_tensor_head_symmetry(const phy_abstract_tensor_head *head, size_t which,
                                    const uint16_t **out_image,
                                    int *out_sign);

typedef struct {
    const phy_index_space *space;
    phy_ir_symbol name;
    phy_ir_variance variance;
} phy_abstract_index;

phy_status phy_abstract_index_make(const phy_index_space *space,
                                   const char *name,
                                   phy_ir_variance variance,
                                   phy_abstract_index *out_index);

/*
 * Validate slot spaces and lower this one abstract factor to the typed IR.
 * This is structural lowering, not component expansion.
 */
phy_status phy_tensor_head_apply(const phy_abstract_tensor_head *head,
                                 const phy_abstract_index *indices,
                                 size_t index_count, phy_ir_ref *out_ref);

typedef struct {
    const phy_abstract_tensor_head *head;
    const phy_abstract_index *indices;
    size_t index_count;
} phy_abstract_factor;

typedef enum {
    PHY_ABSTRACT_INDEX_FREE = 0,
    PHY_ABSTRACT_INDEX_DUMMY
} phy_abstract_index_role;

typedef struct {
    const phy_index_space *space;
    phy_ir_symbol name;
    uint16_t lower_count;
    uint16_t upper_count;
    phy_abstract_index_role role;
} phy_abstract_index_use;

/*
 * Build one coefficient-times-tensor-product monomial and perform the complete
 * Einstein-index census.  A name is scoped by its index space.  One occurrence
 * is free; exactly one lower and one upper occurrence is dummy; every other
 * multiplicity is rejected as ambiguous.
 */
phy_status phy_tensor_monomial_create(
    phy_abstract_context *context, phy_ir_ref coefficient,
    const phy_abstract_factor *factors, size_t factor_count,
    phy_tensor_monomial **out_monomial);
void phy_tensor_monomial_destroy(phy_tensor_monomial *monomial);

phy_ir_ref phy_tensor_monomial_coefficient(
    const phy_tensor_monomial *monomial);
phy_abstract_context *phy_tensor_monomial_context(
    const phy_tensor_monomial *monomial);
size_t phy_tensor_monomial_factor_count(
    const phy_tensor_monomial *monomial);
phy_status phy_tensor_monomial_factor(
    const phy_tensor_monomial *monomial, size_t which,
    const phy_abstract_tensor_head **out_head,
    const phy_abstract_index **out_indices, size_t *out_index_count);
size_t phy_tensor_monomial_index_use_count(
    const phy_tensor_monomial *monomial);
size_t phy_tensor_monomial_free_count(
    const phy_tensor_monomial *monomial);
size_t phy_tensor_monomial_dummy_count(
    const phy_tensor_monomial *monomial);
phy_status phy_tensor_monomial_index_use(
    const phy_tensor_monomial *monomial, size_t which,
    phy_abstract_index_use *out_use);

/*
 * Exact monoterm canonicalization under:
 *
 *   - every declared signed slot symmetry;
 *   - exchange of identical commuting tensor factors;
 *   - alpha-renaming of dummy pairs; and
 *   - upper/lower exchange of a dummy pair when its index space owns a
 *     symmetric or antisymmetric metric.
 *
 * The input is never modified.  A sign produced by slot or metric symmetry is
 * folded into the scalar coefficient.  If the same canonical index
 * configuration is reachable with both signs, the result is the scalar-zero
 * monomial.  Reaching any configured ceiling fails without returning a
 * partially canonical result.
 *
 * `max_degree` is a resource ceiling, not a tensor-rank semantic limit.
 * Zero-valued fields select the device-oriented defaults.
 */
typedef struct {
    size_t max_degree;            /* default 32 */
    size_t max_generators;        /* slot/factor generators; default 256 */
    size_t max_strong_generators; /* BSGS closure; default 1024 */
    uint32_t max_steps;           /* BSGS plus orbit traversal; default 2M */
    uint64_t max_candidates;      /* exact orbit representatives; default 100k */
    size_t max_bytes;             /* group plus canonical scratch; default 1 MiB */
} phy_tensor_canonical_limits;

typedef struct {
    size_t degree;
    uint64_t slot_group_order;
    uint64_t candidates_visited;
    bool zero_by_symmetry;
} phy_tensor_canonical_stats;

void phy_tensor_canonical_limits_defaults(
    phy_tensor_canonical_limits *out_limits);
phy_status phy_tensor_monomial_canonicalize(
    const phy_tensor_monomial *monomial,
    const phy_tensor_canonical_limits *limits,
    phy_tensor_monomial **out_monomial,
    phy_tensor_canonical_stats *out_stats);

/* ---------------------------------------- explicit D g S double coset */

/*
 * The same canonical monomial as phy_tensor_monomial_canonicalize, obtained by
 * enumerating the Butler-Portugal double coset instead of searching it.
 *
 * Write the monomial as a labelled slot arrangement `g`, mapping each slot to
 * an index space, a label and a variance.  Two groups act on it, and they
 * commute because one moves slots while the other renames labels:
 *
 *   S — the slot group.  Generated by every declared slot symmetry of every
 *       factor, lifted to the monomial's slots, plus one exchange generator
 *       per adjacent pair of identical commuting factors.  Every element
 *       carries the +/- character of the symmetries it is built from, and it
 *       acts on the right: (g s)[slot] = g[s[slot]].
 *
 *   D — the dummy group.  Independently inside each index space: every
 *       renumbering of that space's contracted pairs, together with the
 *       exchange of the two members of a pair when the space owns a metric.
 *       It acts on the left, on labels rather than on slots.  A renumbering
 *       carries +1; a member exchange carries the metric's own symmetry, so
 *       +1 across a symmetric metric and -1 across an antisymmetric one.
 *       Without a metric the two orientations are distinct index values and
 *       the exchange is not a group element at all.
 *
 * The canonical form is the least element of `D g S` under the index alphabet
 * that phy_tensor_monomial_canonicalize also uses — free indices ascending by
 * name, then d0^, d0_, d1^, d1_, ... — and the monomial is zero exactly when
 * two elements of that double coset reach the same arrangement with opposite
 * signs.  This entry point builds S and D in full and visits every one of the
 * |D| * |S| products.  It prunes nothing and it has no base or strong
 * generating set.
 *
 * It exists to be checked against rather than to be fast.  |S| and |D| both
 * grow factorially, so these ceilings are far below the production ones and
 * reaching one is PHY_ERR_TERM_LIMIT, never a slower answer.
 * phy_tensor_monomial_canonicalize remains the production entry point and
 * stays BSGS-guided and pruned.
 *
 * Zero-valued fields select the defaults.
 */
typedef struct {
    size_t max_degree;              /* slots; default 12 */
    uint64_t max_slot_group_order;  /* enumerated |S|; default 5040 */
    uint64_t max_dummy_group_order; /* enumerated |D|; default 5040 */
    uint64_t max_products;          /* |D| * |S| visited; default 1M */
    size_t max_bytes;               /* whole enumeration; default 1 MiB */
} phy_tensor_dgs_limits;

typedef struct {
    size_t degree;
    size_t dummy_pair_count;
    uint64_t slot_group_order;
    uint64_t dummy_group_order;
    uint64_t products_visited;
    bool zero_by_symmetry;
} phy_tensor_dgs_stats;

void phy_tensor_dgs_limits_defaults(phy_tensor_dgs_limits *out_limits);
phy_status phy_tensor_monomial_canonicalize_dgs(
    const phy_tensor_monomial *monomial,
    const phy_tensor_dgs_limits *limits,
    phy_tensor_monomial **out_monomial, phy_tensor_dgs_stats *out_stats);

/* ------------------------------------------------------ multi-term Young layer */

/*
 * Which of the two symmetrizers is applied last:
 *
 *   PHY_YOUNG_ROW_SYMMETRY_LAST         P_T = a_T b_T / hook(T)
 *   PHY_YOUNG_COLUMN_ANTISYMMETRY_LAST  P_T = b_T a_T / hook(T)
 *
 * with a_T the row symmetrizer and b_T the signed column antisymmetrizer.
 * Both are idempotent and both have an image isomorphic to the Schur module of
 * the shape, but they are different subspaces of the same slot space. Slot
 * declarations act on the right in this API. Consequently `a_T b_T` has the
 * column antisymmetries manifest and is the Riemann convention for tableau
 * [[0,2],[1,3]]; `b_T a_T` has the row symmetries manifest.
 */
typedef enum {
    PHY_YOUNG_ROW_SYMMETRY_LAST = 0,
    PHY_YOUNG_COLUMN_ANTISYMMETRY_LAST
} phy_young_order;

/*
 * A Young tableau over the slots of one tensor factor.
 *
 * `row_lengths` is a non-increasing partition of `slot_count`; `slots`
 * contains each local slot exactly once in row-major tableau order.  Keeping
 * the slot permutation explicit allows tableaux such as [[0,2],[1,3]]
 * without changing the tensor head's declared slot order. A zero `order`
 * selects `a_T b_T`, preserving the original projector convention.
 */
typedef struct {
    const uint16_t *slots;
    size_t slot_count;
    const uint16_t *row_lengths;
    size_t row_count;
    phy_young_order order;
} phy_young_tableau;

/*
 * Everything general validation can decide about a shape and its filling.
 *
 * `standard` is true when the slot labels increase left to right along every
 * row and top to bottom down every column, which is the classical standard
 * tableau condition; it is reported, never required.  `hook_product` is the
 * product of the hook lengths, so `standard_tableau_count` is the hook-length
 * formula f^lambda = n! / hook(lambda) — the number of standard tableaux of
 * the shape and the rank of the projector on distinctly labelled slots.
 */
typedef struct {
    size_t slot_count;
    size_t row_count;
    size_t column_count;
    uint64_t row_group_order;
    uint64_t column_group_order;
    uint64_t hook_product;
    uint64_t standard_tableau_count;
    bool standard;
} phy_young_tableau_info;

/*
 * General tableau validation, independent of any tensor.  Rejects a shape that
 * is not a non-increasing partition of `slot_count`, a `slots` array that is
 * not a permutation of the local slots, and a factorial or hook product that
 * leaves the exact 64-bit range.
 */
phy_status phy_young_tableau_validate(const phy_young_tableau *tableau,
                                      phy_young_tableau_info *out_info);

/*
 * The conjugate partition: `out_lengths[c]` is the number of rows reaching
 * column `c`.  `capacity` counts entries, not bytes.
 */
phy_status phy_young_tableau_column_lengths(
    const phy_young_tableau *tableau, uint16_t *out_lengths,
    size_t capacity, size_t *out_count);

/*
 * Validation plus the typing rule that makes the symmetrizers well formed: the
 * tableau must cover exactly the head's slots, and every row and every column
 * must be homogeneous in its index space, because a symmetrizer that mixed two
 * index spaces would produce an ill-typed application.
 */
phy_status phy_young_tableau_check_head(
    const phy_abstract_tensor_head *head,
    const phy_young_tableau *tableau, phy_young_tableau_info *out_info);

/*
 * Enumerate the standard Young tableaux of a shape in lexicographic order of
 * their row-major cell labels.  Each tableau occupies `slot_count` entries of
 * `out_entries`; `capacity` counts entries.  `out_entries` may be NULL to
 * count only.  The count is checked against the hook-length formula, so this
 * and phy_young_tableau_validate are two independent computations of the same
 * number. Enumeration is device-bounded to 4096 tableaux; larger shapes return
 * PHY_ERR_TERM_LIMIT without partially filling the caller's buffer.
 */
phy_status phy_young_standard_tableaux(
    const uint16_t *row_lengths, size_t row_count, uint16_t *out_entries,
    size_t capacity, size_t *out_count);

/*
 * Dimension of the Schur module S_shape(V) from the hook-content formula.
 *
 * This is the number of independent components after all Young relations, not
 * merely the number left by monoterm slot symmetries. For shape (2,2) in
 * dimension four it is 20, whereas the Riemann slot group alone leaves 21.
 * The exact result is bounded to uint64_t; overflow is reported.
 */
phy_status phy_young_gl_dimension(const phy_young_tableau *tableau,
                                  size_t dimension,
                                  uint64_t *out_dimension);

typedef struct {
    size_t max_generated_terms; /* row group x column group; default 4096 */
    size_t max_result_terms;    /* after canonical collection; default 256 */
    size_t max_bytes;           /* temporary projector storage; default 512 KiB */
    phy_tensor_canonical_limits canonical;
} phy_young_limits;

typedef struct {
    uint64_t row_group_order;
    uint64_t column_group_order;
    uint64_t generated_terms;
    size_t collected_terms;
    uint64_t hook_product;
} phy_young_stats;

void phy_young_limits_defaults(phy_young_limits *out_limits);

/*
 * Apply the normalized Young symmetrizer selected by `tableau->order`:
 *
 *     a_T b_T / hook(T), or b_T a_T / hook(T),
 *
 * to one factor of a monomial.  Each generated term first passes through the
 * monoterm canonicalizer above; structurally equal terms are then collected
 * by the exact scalar CAS.  This API is intentionally a linear-combination
 * layer rather than pretending a multi-term Garnir identity is a signed slot
 * permutation.
 */
phy_status phy_tensor_monomial_young_project(
    const phy_tensor_monomial *monomial, size_t factor,
    const phy_young_tableau *tableau, const phy_young_limits *limits,
    phy_tensor_expression **out_expression, phy_young_stats *out_stats);

/*
 * Declare that every tensor with this head lies in the image of P_T.
 *
 * This is strictly stronger than the signed slot generators, and the two must
 * agree: each declared generator g with sign s is accepted only after the
 * exact group-algebra identity c_T * g = s * c_T is verified term by term over
 * the |R_T| * |C_T| elements of the Young symmetrizer.  A generator that is
 * not a manifest symmetry of the image is rejected with PHY_ERR_TYPE and the
 * head is left unchanged, so a declaration can never quietly turn every tensor
 * with this head into zero.
 *
 * The converse is deliberately not folded in.  The multi-term content of the
 * declaration — the Garnir relations, of which the Riemann case is the first
 * Bianchi identity — is never added to the signed slot group, because it is
 * not a signed slot permutation.  Monoterm canonicalization keeps exactly the
 * strength it had; the multi-term content is applied only by the reducer
 * below.
 *
 * Adding a slot generator after a Young declaration re-runs the same check.
 */
phy_status phy_tensor_head_set_young_symmetry(
    phy_abstract_tensor_head *head, const phy_young_tableau *tableau,
    const phy_young_limits *limits);
bool phy_tensor_head_has_young_symmetry(
    const phy_abstract_tensor_head *head);
/*
 * The declared tableau, borrowed from the head, plus its validated info.
 * Either output may be NULL.
 */
phy_status phy_tensor_head_young_symmetry(
    const phy_abstract_tensor_head *head,
    phy_young_tableau *out_tableau, phy_young_tableau_info *out_info);

/* ------------------------------------------------------------ Garnir layer */

/*
 * The Garnir relation set of a tableau.
 *
 * Relation (j, r) antisymmetrizes over the slots of column j in rows >= r
 * together with the slots of column j+1 in rows <= r, for every column j and
 * every 0 <= r < (length of column j+1).  Each such set has exactly
 * (length of column j) + 1 slots, one more than the column it over-fills,
 * which is the classical Garnir condition.  Relations are numbered in
 * increasing (j, r) order.
 *
 * These are an explicit presentation of the relation module.  They are not
 * what the reducer runs on: the reducer applies the idempotent projector,
 * whose completeness does not depend on the Garnir lemma.  The test suite
 * checks the two against each other.
 */
size_t phy_young_garnir_count(const phy_young_tableau *tableau);
phy_status phy_young_garnir_slots(const phy_young_tableau *tableau,
                                  size_t which, uint16_t *out_slots,
                                  size_t capacity, size_t *out_count);

typedef struct {
    size_t slot_count;    /* |X|, the antisymmetrized slot set */
    uint64_t group_order; /* |X|! */
    uint64_t generated_terms;
    size_t collected_terms;
} phy_garnir_stats;

/*
 * Build relation `which` of the head declared on `factor`, as the normalized
 * antisymmetrization (1/|X|!) sum_{sigma in S_X} sgn(sigma) sigma applied to
 * that factor. Every generated term is canonicalized and collected exactly.
 * The returned expression is a readable representative of the relation; pass
 * it to phy_tensor_expression_young_reduce() to prove that it is zero modulo
 * the head's declared Young symmetry.
 *
 * The factor's head must carry a Young declaration; without one there is no
 * Garnir relation to state and the call is PHY_ERR_TYPE.
 */
phy_status phy_tensor_monomial_garnir_relation(
    const phy_tensor_monomial *monomial, size_t factor, size_t which,
    const phy_young_limits *limits,
    phy_tensor_expression **out_expression, phy_garnir_stats *out_stats);

void phy_tensor_expression_destroy(phy_tensor_expression *expression);
phy_abstract_context *phy_tensor_expression_context(
    const phy_tensor_expression *expression);
size_t phy_tensor_expression_term_count(
    const phy_tensor_expression *expression);
const phy_tensor_monomial *phy_tensor_expression_term(
    const phy_tensor_expression *expression, size_t which);
size_t phy_tensor_expression_free_count(
    const phy_tensor_expression *expression);
phy_status phy_tensor_expression_free_use(
    const phy_tensor_expression *expression, size_t which,
    phy_abstract_index_use *out_use);

typedef struct {
    size_t max_generated_terms; /* distributive products; default 4096 */
    size_t max_result_terms; /* after canonical collection; default 256 */
    size_t max_bytes;        /* temporary rebuild storage; default 512 KiB */
    phy_tensor_canonical_limits canonical;
} phy_tensor_algebra_limits;

void phy_tensor_algebra_limits_defaults(
    phy_tensor_algebra_limits *out_limits);

/*
 * Linear algebra on typed abstract tensor expressions.
 *
 * Every incoming monomial is canonicalized and structurally equal terms are
 * collected with the exact scalar CAS. Addition requires the same typed set
 * of free indices, although census order may differ between expressions.
 * The result owns cloned monomials and never aliases an input expression.
 */
phy_status phy_tensor_expression_from_monomial(
    const phy_tensor_monomial *monomial,
    const phy_tensor_algebra_limits *limits,
    phy_tensor_expression **out_expression);
phy_status phy_tensor_expression_add(
    const phy_tensor_expression *left,
    const phy_tensor_expression *right,
    const phy_tensor_algebra_limits *limits,
    phy_tensor_expression **out_expression);
phy_status phy_tensor_expression_scale(
    const phy_tensor_expression *expression, phy_ir_ref scalar,
    const phy_tensor_algebra_limits *limits,
    phy_tensor_expression **out_expression);
phy_status phy_tensor_expression_multiply(
    const phy_tensor_expression *left,
    const phy_tensor_expression *right,
    const phy_tensor_algebra_limits *limits,
    phy_tensor_expression **out_expression);

/*
 * Apply P_T to the same factor position of every term.  Each term must have
 * that position and the same head there, otherwise the request names no single
 * tensor factor and is PHY_ERR_TYPE.  Statistics are cumulative over terms.
 */
phy_status phy_tensor_expression_young_project(
    const phy_tensor_expression *expression, size_t factor,
    const phy_young_tableau *tableau, const phy_young_limits *limits,
    phy_tensor_expression **out_expression, phy_young_stats *out_stats);

/* ------------------------------------------- multi-term Young reduction */

typedef struct {
    size_t max_generated_terms; /* projector images per input term; default 4096 */
    size_t max_result_terms;    /* after canonical collection; default 256 */
    size_t max_bytes;           /* temporary projector storage; default 512 KiB */
    uint32_t max_steps;         /* generated terms over the whole call; default 1M */
    phy_tensor_canonical_limits canonical;
} phy_young_reduce_limits;

typedef struct {
    size_t input_terms;
    size_t collected_terms;
    uint64_t projected_factors; /* factors carrying a Young declaration */
    uint64_t generated_terms;
    uint32_t steps;
} phy_young_reduce_stats;

void phy_young_reduce_limits_defaults(phy_young_reduce_limits *out_limits);

/*
 * Reduce an expression modulo the multi-term relations of every declared Young
 * symmetry it mentions.
 *
 * Every factor of every term whose head carries a declaration is projected,
 * all such factors of one term simultaneously, and the images are canonicalized
 * and collected exactly. The result is the image under the declared
 * idempotent projectors and is therefore an exact equality gate for the
 * quotient by their kernels. A Riemann-type declaration imposes the first
 * Bianchi identity automatically: the cyclic sum reduces to the zero
 * expression, which keeps its typed free-index signature.
 *
 * A term with no declared factor is canonicalized and passed through.
 * Exceeding any ceiling returns without a partially reduced result.
 */
phy_status phy_tensor_expression_young_reduce(
    const phy_tensor_expression *expression,
    const phy_young_reduce_limits *limits,
    phy_tensor_expression **out_expression,
    phy_young_reduce_stats *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* PHY_ABSTRACT_TENSOR_H */
