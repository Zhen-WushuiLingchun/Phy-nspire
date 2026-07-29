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
    const phy_tensor_head *head,
    phy_component_basis *const *bases,
    const phy_ir_variance *valence,
    const phy_component_limits *limits,
    phy_component_tensor **out_tensor);
void phy_component_tensor_destroy(phy_component_tensor *tensor);

const phy_tensor_head *phy_component_tensor_head(
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

#ifdef __cplusplus
}
#endif

#endif /* PHY_COMPONENT_TENSOR_H */
