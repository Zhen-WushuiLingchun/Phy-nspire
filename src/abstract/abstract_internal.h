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

#endif /* PHY_ABSTRACT_INTERNAL_H */
