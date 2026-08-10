#ifndef PHY_ABSTRACT_INTERNAL_H
#define PHY_ABSTRACT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "phy/abstract_tensor.h"
#include "phy/platform.h"

typedef struct phy_perm_group phy_perm_group;

typedef struct {
    uint16_t *image;
    size_t image_bytes;
    int8_t sign;
} phy_abstract_generator;

typedef struct {
    const phy_abstract_tensor_head *head;
    size_t index_offset;
    size_t index_count;
} phy_abstract_factor_record;

struct phy_abstract_context {
    phy_cas *cas;
    phy_ir_context *ir;
    phy_abstract_limits limits;
    size_t bytes_used;
    phy_index_space **spaces;
    phy_abstract_tensor_head **heads;
    size_t space_array_bytes;
    size_t head_array_bytes;
    size_t space_count;
    size_t head_count;
    phy_tensor_monomial *monomials;
    phy_tensor_expression *expressions;
};

struct phy_tensor_monomial {
    phy_abstract_context *context;
    phy_tensor_monomial *previous;
    phy_tensor_monomial *next;
    bool linked;
    phy_ir_ref coefficient;
    phy_abstract_factor_record *factors;
    phy_abstract_index *indices;
    phy_abstract_index_use *uses;
    size_t factor_count;
    size_t index_count;
    size_t use_count;
    size_t free_count;
    size_t dummy_count;
    size_t factor_bytes;
    size_t index_bytes;
    size_t use_bytes;
};

struct phy_tensor_expression {
    phy_abstract_context *context;
    phy_tensor_expression *previous;
    phy_tensor_expression *next;
    bool linked;
    phy_tensor_monomial **terms;
    phy_abstract_index_use *free_uses;
    size_t term_count;
    size_t term_capacity;
    size_t term_bytes;
    size_t free_count;
    size_t free_bytes;
};

struct phy_index_space {
    phy_abstract_context *context;
    phy_ir_symbol symbol;
    phy_ir_ref dimension;
    phy_metric_symmetry metric;
};

struct phy_abstract_tensor_head {
    phy_abstract_context *context;
    phy_ir_symbol symbol;
    size_t slot_count;
    const phy_index_space **slot_spaces;
    size_t slot_bytes;
    phy_tensor_commutation commutation;
    phy_abstract_generator *generators;
    size_t generator_count;
    size_t generator_capacity;
    size_t generator_bytes;
    /*
     * The declared Young symmetry, if any.  `young.slots` and
     * `young.row_lengths` point at the two owned copies below so that the
     * caller's arrays need not outlive the declaration.
     */
    bool has_young;
    phy_young_tableau young;
    phy_young_tableau_info young_info;
    uint16_t *young_slots;
    size_t young_slots_bytes;
    uint16_t *young_row_lengths;
    size_t young_row_bytes;
};

/* --------------------------------------------- the canonical form itself */

/*
 * `src/abstract/canonical.c` and `src/abstract/dgs.c` compute the same
 * canonical monomial by two different searches.  The *definition* of that
 * canonical form is shared and lives here, so that only the search differs
 * and a disagreement between the two is evidence about the search rather than
 * about two independently drifting conventions.
 *
 * One slot of a candidate arrangement reduces to this key.  Comparing two
 * arrangements is the lexicographic comparison of their key sequences under
 * `phy_canonical_compare_key`, which realizes the index alphabet
 *
 *     free indices ascending by name, then  d0^, d0_, d1^, d1_, ...
 *
 * that SymPy's `tensor_can` and xPerm both use.  `ordinal` is the dummy pair's
 * number inside its own index space; the space itself is not compared because
 * every declared slot symmetry is an automorphism of the typed slot list, so
 * the index space at a given output slot is the same for every candidate.
 */
typedef struct {
    uint8_t role;        /* phy_abstract_index_role */
    uint8_t orientation; /* phy_canonical_orientation_rank */
    size_t ordinal;      /* dummy pair number within its index space */
    phy_ir_symbol free_name;
} phy_canonical_key;

uint8_t phy_canonical_orientation_rank(phy_ir_variance variance);

/* Census entry for one occupied slot, or SIZE_MAX when the monomial is torn. */
size_t phy_canonical_index_use(const phy_tensor_monomial *monomial,
                               const phy_abstract_index *index);
int phy_canonical_compare_key(phy_ir_context *ir,
                              const phy_canonical_key *left,
                              const phy_canonical_key *right);

/*
 * Deterministic factor order: each maximal run of commuting factors is sorted
 * by head name, and a noncommuting factor is a barrier.  `offsets` receives
 * the first slot of each ordered factor and `out_indices` the whole ordered
 * slot list.
 */
void phy_canonical_factor_order(const phy_tensor_monomial *monomial,
                                size_t *order);
void phy_canonical_arrange_indices(const phy_tensor_monomial *monomial,
                                   const size_t *order, size_t *offsets,
                                   phy_abstract_index *out_indices);

/*
 * Lift every declared slot symmetry into the monomial's slot group and add one
 * exchange generator per adjacent pair of identical commuting factors.
 * `image` is caller-provided scratch of `monomial->index_count` entries.
 */
phy_status phy_canonical_slot_generators(const phy_tensor_monomial *monomial,
                                         const size_t *order,
                                         const size_t *offsets,
                                         phy_perm_group *group,
                                         uint16_t *image);

/*
 * Rename the chosen arrangement's dummies and assemble the result.  `indices`
 * is modified in place; `symbols` and `factors` are caller-provided scratch.
 */
phy_status phy_canonical_build_output(const phy_tensor_monomial *monomial,
                                      const size_t *order,
                                      const size_t *offsets,
                                      phy_abstract_index *indices,
                                      const phy_canonical_key *key,
                                      phy_ir_symbol *symbols, int sign,
                                      bool zero, phy_abstract_factor *factors,
                                      phy_tensor_monomial **out_monomial);

phy_status phy_abstract_resolve_limits(const phy_abstract_limits *requested,
                                       phy_abstract_limits *out);
void *phy_abstract_alloc(phy_abstract_context *context, size_t bytes);
void phy_abstract_free(phy_abstract_context *context, void *pointer,
                       size_t bytes);
bool phy_abstract_name_used(const phy_abstract_context *context,
                            phy_ir_symbol symbol, bool heads);
void phy_abstract_head_destroy(phy_abstract_tensor_head *head);

/* ------------------------------------------------- shared expression build */

phy_status phy_expression_create_with_signature(
    phy_abstract_context *context, size_t capacity,
    const phy_abstract_index_use *free_uses, size_t free_count,
    phy_tensor_expression **out);
phy_status phy_expression_create_like(phy_abstract_context *context,
                                      size_t capacity,
                                      const phy_tensor_monomial *signature,
                                      phy_tensor_expression **out);
/* Takes ownership of `term`, including on failure and on exact cancellation. */
phy_status phy_expression_collect_term(phy_tensor_expression *expression,
                                       phy_tensor_monomial *term);
void phy_expression_sort(phy_tensor_expression *expression);

/* ------------------------------------------------ shared Young shape layer */

/*
 * The row and column block decomposition of one validated tableau, laid out in
 * a single caller-owned allocation.  `column_slots` groups the local slots by
 * column, which is what turns both symmetrizers into the same disjoint-block
 * enumeration.
 */
typedef struct {
    size_t slot_count;
    size_t row_count;
    size_t column_count;
    const uint16_t *row_slots;   /* borrowed: tableau->slots */
    const uint16_t *row_lengths; /* borrowed: tableau->row_lengths */
    size_t *row_offsets;
    uint16_t *column_slots;
    uint16_t *column_lengths;
    size_t *column_offsets;
    uint64_t row_order;
    uint64_t column_order;
    uint64_t hook_product;
    void *storage;
    size_t storage_bytes;
} phy_young_shape;

/*
 * `ceiling` bounds each of the two group orders; `max_bytes` bounds the
 * allocation.  A shape is released with phy_young_shape_release whether or not
 * the build succeeded.
 */
phy_status phy_young_resolve_limits(const phy_young_limits *requested,
                                    phy_young_limits *out);

phy_status phy_young_shape_build(const phy_young_tableau *tableau,
                                 uint64_t ceiling, size_t max_bytes,
                                 phy_young_shape *out_shape);
void phy_young_shape_release(phy_young_shape *shape);

/*
 * Enumerate the direct product of the symmetric groups on a list of disjoint
 * slot blocks, in image notation over `degree` points.  `images` receives
 * `capacity * degree` entries and `signs` receives `capacity` entries, where
 * `capacity` must equal the product of the block factorials.  `alternating`
 * records the permutation sign; otherwise every sign is +1.  `state` and
 * `work` are `degree`-entry scratch arrays.
 */
phy_status phy_young_block_group(size_t degree, const uint16_t *block_slots,
                                 const uint16_t *block_lengths,
                                 const size_t *block_offsets,
                                 size_t block_count, bool alternating,
                                 size_t capacity, uint16_t *images,
                                 int8_t *signs, uint16_t *state,
                                 uint16_t *work);

/*
 * The exact group-algebra test behind a Young declaration: does the signed slot
 * permutation `image` with sign `sign` act as `sign` times the identity on the
 * image of the tableau's Young symmetrizer?  `limits` may be NULL.
 */
phy_status phy_young_generator_is_manifest(
    const phy_young_tableau *tableau, const uint16_t *image, int sign,
    const phy_young_limits *limits, bool *out_manifest);

#endif /* PHY_ABSTRACT_INTERNAL_H */
