#include "component_internal.h"

#include <limits.h>
#include <string.h>

#define PHY_BASIS_DEFAULT_DIMENSION 256u
#define PHY_BASIS_DEFAULT_BYTES (64u * 1024u)

static size_t align_up(size_t value, size_t alignment)
{
    const size_t remainder = value % alignment;
    return remainder == 0u ? value : value + alignment - remainder;
}

void phy_basis_limits_defaults(phy_basis_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_dimension = PHY_BASIS_DEFAULT_DIMENSION;
    out_limits->max_bytes = PHY_BASIS_DEFAULT_BYTES;
}

static phy_status resolve_limits(const phy_basis_limits *requested,
                                 phy_basis_limits *out)
{
    phy_basis_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_dimension != 0u) {
            out->max_dimension = requested->max_dimension;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
    }
    if (out->max_dimension == 0u ||
        out->max_dimension > UINT32_MAX ||
        out->max_bytes < sizeof(phy_component_basis)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

static phy_status validate_dimension(const phy_index_space *space,
                                     size_t dimension,
                                     const phy_basis_limits *limits)
{
    if (dimension == 0u || dimension > limits->max_dimension ||
        dimension > UINT32_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    size_t abstract_dimension = 0u;
    if (phy_index_space_known_dimension(space, &abstract_dimension) &&
        abstract_dimension != dimension) {
        return PHY_ERR_DOMAIN;
    }
    return PHY_OK;
}

static bool duplicate_coordinate(const char *const *names, size_t which)
{
    for (size_t prior = 0u; prior < which; ++prior) {
        if (strcmp(names[prior], names[which]) == 0) {
            return true;
        }
    }
    return false;
}

phy_status phy_component_basis_create(
    const phy_index_space *space, const char *name, size_t dimension,
    const char *const *coordinate_names,
    const phy_basis_limits *requested,
    phy_component_basis **out_basis)
{
    if (space == NULL || name == NULL || name[0] == '\0' ||
        out_basis == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_basis = NULL;
    phy_basis_limits limits;
    phy_status status = resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    status = validate_dimension(space, dimension, &limits);
    if (status != PHY_OK) {
        return status;
    }
    if (coordinate_names != NULL) {
        for (size_t axis = 0u; axis < dimension; ++axis) {
            if (coordinate_names[axis] == NULL ||
                coordinate_names[axis][0] == '\0' ||
                duplicate_coordinate(coordinate_names, axis)) {
                return PHY_ERR_INVALID_ARGUMENT;
            }
        }
    }

    phy_abstract_context *abstract = phy_index_space_context(space);
    phy_cas *cas = phy_abstract_cas(abstract);
    phy_ir_context *ir = phy_cas_ir(cas);
    const phy_ir_symbol basis_name = phy_ir_intern(ir, name);
    if (basis_name == PHY_IR_NO_SYMBOL) {
        return phy_ir_last_error(ir);
    }

    size_t storage_bytes = 0u;
    size_t refs_offset = 0u;
    if (coordinate_names != NULL) {
        if (dimension > SIZE_MAX / sizeof(phy_ir_symbol)) {
            return PHY_ERR_MEMORY_LIMIT;
        }
        storage_bytes = dimension * sizeof(phy_ir_symbol);
        refs_offset = align_up(storage_bytes, sizeof(phy_ir_ref));
        if (dimension > (SIZE_MAX - refs_offset) / sizeof(phy_ir_ref)) {
            return PHY_ERR_MEMORY_LIMIT;
        }
        storage_bytes =
            refs_offset + dimension * sizeof(phy_ir_ref);
    }
    if (storage_bytes >
        limits.max_bytes - sizeof(phy_component_basis)) {
        return PHY_ERR_MEMORY_LIMIT;
    }

    phy_component_basis *basis = phy_alloc(sizeof *basis);
    if (basis == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(basis, 0, sizeof *basis);
    basis->space = space;
    basis->ir = ir;
    basis->name = basis_name;
    basis->dimension = dimension;
    basis->has_coordinates = coordinate_names != NULL;
    basis->storage_bytes = storage_bytes;
    if (storage_bytes != 0u) {
        basis->storage = phy_alloc(storage_bytes);
        if (basis->storage == NULL) {
            phy_free(basis, sizeof *basis);
            return PHY_ERR_MEMORY_LIMIT;
        }
        memset(basis->storage, 0, storage_bytes);
        uint8_t *bytes = basis->storage;
        basis->coordinate_symbols = (phy_ir_symbol *)bytes;
        basis->coordinates =
            (phy_ir_ref *)(void *)(bytes + refs_offset);
        for (size_t axis = 0u; axis < dimension; ++axis) {
            basis->coordinate_symbols[axis] =
                phy_ir_intern(ir, coordinate_names[axis]);
            if (basis->coordinate_symbols[axis] == PHY_IR_NO_SYMBOL) {
                status = phy_ir_last_error(ir);
                phy_component_basis_destroy(basis);
                return status;
            }
            basis->coordinates[axis] = phy_ir_symbol_ref(
                ir, basis->coordinate_symbols[axis]);
            if (basis->coordinates[axis] == PHY_IR_NULL) {
                status = phy_ir_last_error(ir);
                phy_component_basis_destroy(basis);
                return status;
            }
        }
    }
    *out_basis = basis;
    return PHY_OK;
}

void phy_component_basis_destroy(phy_component_basis *basis)
{
    if (basis == NULL) {
        return;
    }
    phy_free(basis->storage, basis->storage_bytes);
    phy_free(basis, sizeof *basis);
}

const phy_index_space *phy_component_basis_space(
    const phy_component_basis *basis)
{
    return basis != NULL ? basis->space : NULL;
}

const char *phy_component_basis_name(const phy_component_basis *basis)
{
    return basis != NULL ? phy_ir_symbol_name(basis->ir, basis->name)
                         : NULL;
}

size_t phy_component_basis_dimension(const phy_component_basis *basis)
{
    return basis != NULL ? basis->dimension : 0u;
}

bool phy_component_basis_has_coordinates(
    const phy_component_basis *basis)
{
    return basis != NULL && basis->has_coordinates;
}

phy_ir_symbol phy_component_basis_coordinate_symbol(
    const phy_component_basis *basis, size_t axis)
{
    return basis != NULL && basis->has_coordinates &&
                   axis < basis->dimension
               ? basis->coordinate_symbols[axis]
               : PHY_IR_NO_SYMBOL;
}

phy_ir_ref phy_component_basis_coordinate(
    const phy_component_basis *basis, size_t axis)
{
    return basis != NULL && basis->has_coordinates &&
                   axis < basis->dimension
               ? basis->coordinates[axis]
               : PHY_IR_NULL;
}
