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
typedef struct phy_tensor_head phy_tensor_head;
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
    phy_tensor_commutation commutation, phy_tensor_head **out_head);
const char *phy_tensor_head_name(const phy_tensor_head *head);
phy_ir_symbol phy_tensor_head_symbol(const phy_tensor_head *head);
size_t phy_tensor_head_slot_count(const phy_tensor_head *head);
const phy_index_space *phy_tensor_head_slot_space(const phy_tensor_head *head,
                                                  size_t slot);
phy_tensor_commutation phy_tensor_head_commutation(
    const phy_tensor_head *head);

/*
 * Add a signed generator in image notation: image[i] is the destination of
 * slot i.  `sign` is +1 or -1.  The full symmetry group is deliberately not
 * enumerated here; the bounded BSGS layer consumes these generators.
 */
phy_status phy_tensor_head_add_symmetry(phy_tensor_head *head,
                                        const uint16_t *image, int sign);
size_t phy_tensor_head_symmetry_count(const phy_tensor_head *head);
phy_status phy_tensor_head_symmetry(const phy_tensor_head *head, size_t which,
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
phy_status phy_tensor_head_apply(const phy_tensor_head *head,
                                 const phy_abstract_index *indices,
                                 size_t index_count, phy_ir_ref *out_ref);

typedef struct {
    const phy_tensor_head *head;
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
size_t phy_tensor_monomial_factor_count(
    const phy_tensor_monomial *monomial);
phy_status phy_tensor_monomial_factor(
    const phy_tensor_monomial *monomial, size_t which,
    const phy_tensor_head **out_head,
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

/* ------------------------------------------------------ multi-term Young layer */

/*
 * A standard Young tableau over the slots of one tensor factor.
 *
 * `row_lengths` is a non-increasing partition of `slot_count`; `slots`
 * contains each local slot exactly once in row-major tableau order.  Keeping
 * the slot permutation explicit allows tableaux such as [[0,2],[1,3]]
 * without changing the tensor head's declared slot order.
 */
typedef struct {
    const uint16_t *slots;
    size_t slot_count;
    const uint16_t *row_lengths;
    size_t row_count;
} phy_young_tableau;

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
 * Apply the normalized Young symmetrizer
 *
 *     P_T = (row symmetrizer)(column antisymmetrizer) / hook(T)
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

void phy_tensor_expression_destroy(phy_tensor_expression *expression);
size_t phy_tensor_expression_term_count(
    const phy_tensor_expression *expression);
const phy_tensor_monomial *phy_tensor_expression_term(
    const phy_tensor_expression *expression, size_t which);

#ifdef __cplusplus
}
#endif

#endif /* PHY_ABSTRACT_TENSOR_H */
