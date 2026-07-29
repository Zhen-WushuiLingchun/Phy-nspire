/*
 * Phy-nspire — sparse dynamic components bound to abstract tensor heads.
 *
 * The abstract layer owns index identities and tensor laws.  This layer
 * chooses concrete bases and stores only explicitly assigned canonical
 * components.  Rank and dimension are runtime values bounded by resources;
 * creating a rank-r tensor never allocates the full product of dimensions.
 */
#ifndef PHY_COMPONENT_TENSOR_H
#define PHY_COMPONENT_TENSOR_H

#include <stddef.h>
#include <stdint.h>

#include "phy/abstract_tensor.h"
#include "phy/tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_component_basis phy_component_basis;
typedef struct phy_component_tensor phy_component_tensor;

typedef struct {
    size_t max_dimension; /* default 256 */
    size_t max_bytes;     /* basis metadata and coordinates; default 64 KiB */
} phy_basis_limits;

void phy_basis_limits_defaults(phy_basis_limits *out_limits);

/*
 * Bind one abstract IndexSpace to a concrete basis.  A symbolic abstract
 * dimension accepts any positive concrete dimension within the limit; a known
 * integer dimension must match.  Coordinate names are optional because
 * internal, spinor and colour spaces need bases without chart coordinates.
 */
phy_status phy_component_basis_create(
    const phy_index_space *space, const char *name, size_t dimension,
    const char *const *coordinate_names, const phy_basis_limits *limits,
    phy_component_basis **out_basis);
void phy_component_basis_destroy(phy_component_basis *basis);

const phy_index_space *phy_component_basis_space(
    const phy_component_basis *basis);
const char *phy_component_basis_name(const phy_component_basis *basis);
size_t phy_component_basis_dimension(const phy_component_basis *basis);
bool phy_component_basis_has_coordinates(
    const phy_component_basis *basis);
phy_ir_symbol phy_component_basis_coordinate_symbol(
    const phy_component_basis *basis, size_t axis);
phy_ir_ref phy_component_basis_coordinate(
    const phy_component_basis *basis, size_t axis);

typedef struct {
    size_t max_rank;              /* default 32 */
    size_t max_dimension;         /* each slot; default 256 */
    size_t max_entries;           /* stored sparse components; default 4096 */
    size_t max_generators;        /* slot generators; default 256 */
    size_t max_strong_generators; /* BSGS closure; default 1024 */
    uint32_t max_steps;           /* one component canonicalization; default 2M */
    uint64_t max_candidates;      /* surviving full orbit reps; default 100k */
    size_t max_bytes;             /* group, scratch and sparse table; default 512 KiB */
} phy_component_limits;

void phy_component_limits_defaults(phy_component_limits *out_limits);

/*
 * Instantiate one TensorHead in concrete bases.  `bases[slot]` must bind the
 * IndexSpace declared for that head slot.  Valence remains a property of this
 * component realization rather than of the abstract head.
 */
phy_status phy_component_tensor_create(
    const phy_abstract_tensor_head *head,
    phy_component_basis *const *bases,
    const phy_ir_variance *valence,
    const phy_component_limits *limits,
    phy_component_tensor **out_tensor);
void phy_component_tensor_destroy(phy_component_tensor *tensor);

const phy_abstract_tensor_head *phy_component_tensor_head(
    const phy_component_tensor *tensor);
size_t phy_component_tensor_rank(const phy_component_tensor *tensor);
const phy_component_basis *phy_component_tensor_basis(
    const phy_component_tensor *tensor, size_t slot);
phy_ir_variance phy_component_tensor_valence(
    const phy_component_tensor *tensor, size_t slot);
size_t phy_component_tensor_entry_count(
    const phy_component_tensor *tensor);
uint64_t phy_component_tensor_symmetry_order(
    const phy_component_tensor *tensor);
size_t phy_component_tensor_bytes_used(
    const phy_component_tensor *tensor);

/*
 * Sparse exact access.  Missing canonical entries are zero.  Set/get apply
 * the head's signed slot group, so assigning A[3,1] and reading A[1,3] obeys
 * antisymmetry without duplicating storage.  A component forced to vanish
 * rejects a value not proved zero by the exact CAS.
 */
phy_status phy_component_tensor_set(
    phy_component_tensor *tensor, const uint32_t *indices,
    phy_ir_ref value);
phy_status phy_component_tensor_get(
    phy_component_tensor *tensor, const uint32_t *indices,
    phy_ir_ref *out_value);

phy_status phy_component_tensor_canonical_indices(
    phy_component_tensor *tensor, const uint32_t *indices,
    uint32_t *out_indices, int *out_sign);

/*
 * Lift a legacy dense chart tensor into the dynamic sparse component layer.
 *
 * The caller supplies the abstract head and one concrete basis per slot.
 * Rank, dimension, IR context, slot IndexSpace, coordinate names, and valence
 * are checked before allocation. The import is then verified over every dense
 * source component against the head's signed slot group. If the head declares
 * a Young symmetry, the bridge additionally proves P_T(T) = T component by
 * component. A source that violates either stronger abstract symmetry is
 * rejected transactionally.
 *
 * This is the explicit bridge used while GR and the differential-geometric
 * frontend still produce the proven legacy component objects. It never
 * guesses a basis; the optional Young proof is evaluated through an explicit
 * monomial over the caller-supplied bases.
 */
phy_status phy_component_tensor_import_legacy(
    const phy_tensor *source, const phy_abstract_tensor_head *head,
    phy_component_basis *const *bases,
    const phy_component_limits *limits,
    phy_component_tensor **out_tensor);

/* ------------------------------------------- abstract/component bridge */

/*
 * A binding is one explicit component picture: one borrowed basis per
 * IndexSpace and one borrowed realization per TensorHead.  Borrowed objects
 * must outlive the binding.
 */
typedef struct phy_component_binding phy_component_binding;

typedef struct {
    size_t max_bases;    /* bound index spaces; default 32 */
    size_t max_tensors;  /* bound tensor heads; default 64 */
    size_t max_free;     /* free indices in one monomial; default 16 */
    size_t max_dummy;    /* contracted pairs in one monomial; default 16 */
    size_t max_terms;    /* nonzero products accumulated; default 65536 */
    uint32_t max_steps;  /* dummy-tree edges visited; default 2M */
    size_t max_bytes;    /* binding plus one evaluation; default 64 KiB */
} phy_bridge_limits;

typedef struct {
    size_t free_count;
    size_t dummy_count;
    uint64_t assignments; /* complete, non-pruned dummy assignments */
    uint64_t pruned;      /* branches cut by an exactly zero component */
    uint64_t terms;       /* nonzero products accumulated */
    size_t bytes_used;    /* binding metadata plus evaluation scratch */
    uint32_t steps;       /* dummy-tree edges visited */
} phy_bridge_stats;

void phy_bridge_limits_defaults(phy_bridge_limits *out_limits);
phy_status phy_component_binding_create(
    phy_abstract_context *context, const phy_bridge_limits *limits,
    phy_component_binding **out_binding);
void phy_component_binding_destroy(phy_component_binding *binding);

/*
 * Adding the same pointer again is idempotent.  Binding a different basis to
 * an already-bound space, or a different realization to an already-bound
 * head, returns PHY_ERR_ALREADY_INITIALIZED and leaves the binding unchanged.
 * A tensor may be added only after all of its slot spaces have bases.
 */
phy_status phy_component_binding_add_basis(
    phy_component_binding *binding, phy_component_basis *basis);
phy_status phy_component_binding_add_tensor(
    phy_component_binding *binding, phy_component_tensor *tensor);

size_t phy_component_binding_basis_count(
    const phy_component_binding *binding);
size_t phy_component_binding_tensor_count(
    const phy_component_binding *binding);
phy_component_basis *phy_component_binding_basis(
    const phy_component_binding *binding, const phy_index_space *space);
phy_component_tensor *phy_component_binding_tensor(
    const phy_component_binding *binding,
    const phy_abstract_tensor_head *head);

/*
 * Report free indices in monomial census order (first occurrence while
 * scanning factors and slots).  This is exactly the coordinate order consumed
 * by phy_component_value_monomial.
 */
phy_status phy_component_value_free_slots(
    const phy_tensor_monomial *monomial, phy_abstract_index_use *out_uses,
    size_t capacity, size_t *out_count);

/*
 * Evaluate one component of one coefficient-times-tensor-product monomial.
 * Only dummy indices are enumerated.  A dummy is already one upper plus one
 * lower occurrence, so the bridge never inserts a metric or changes valence.
 * Missing or mismatched bindings are typed failures.
 *
 * On every failure *out_value is PHY_IR_NULL and out_stats is zeroed.  No
 * dense product of slot dimensions is allocated.
 */
phy_status phy_component_value_monomial(
    phy_component_binding *binding, const phy_tensor_monomial *monomial,
    const uint32_t *free_indices, size_t free_count,
    phy_ir_ref *out_value, phy_bridge_stats *out_stats);

/*
 * Evaluate a collected abstract expression in one component picture.
 *
 * `free_indices` follows the first term's free-index census. Every later term
 * must carry the same typed free-index set; its local order is remapped by
 * (IndexSpace, name, variance) before monomial evaluation. This is essential
 * for Young projections, where a slot permutation can change census order
 * without changing the tensor represented.
 *
 * Term products, dummy-tree steps, and temporary bytes share the binding's
 * configured limits across the whole expression. Statistics are cumulative;
 * `dummy_count` and `bytes_used` are the peak values over one term.
 */
phy_status phy_component_value_expression(
    phy_component_binding *binding,
    const phy_tensor_expression *expression,
    const uint32_t *free_indices, size_t free_count,
    phy_ir_ref *out_value, phy_bridge_stats *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* PHY_COMPONENT_TENSOR_H */
