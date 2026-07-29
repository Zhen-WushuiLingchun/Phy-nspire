#include "map_internal.h"

#include <string.h>

#define PHY_MAP_DEFAULT_DIMENSION 32u
#define PHY_MAP_DEFAULT_BYTES (128u * 1024u)

static size_t align_up(size_t value, size_t alignment)
{
    const size_t remainder = value % alignment;
    return remainder == 0u ? value : value + alignment - remainder;
}

void phy_map_limits_defaults(phy_map_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_dimension = PHY_MAP_DEFAULT_DIMENSION;
    out_limits->max_bytes = PHY_MAP_DEFAULT_BYTES;
    phy_linear_limits_defaults(&out_limits->linear);
}

static phy_status resolve_limits(const phy_map_limits *requested,
                                 phy_map_limits *out)
{
    phy_map_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_dimension != 0u) {
            out->max_dimension = requested->max_dimension;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
        out->linear = requested->linear;
    }
    if (out->max_dimension == 0u ||
        out->max_bytes < sizeof(phy_coordinate_map)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

static bool coordinate_sets_disjoint(
    const phy_component_basis *source,
    const phy_component_basis *target)
{
    for (size_t i = 0u; i < source->dimension; ++i) {
        for (size_t a = 0u; a < target->dimension; ++a) {
            if (source->coordinate_symbols[i] ==
                target->coordinate_symbols[a]) {
                return false;
            }
        }
    }
    return true;
}

static bool contains_ref(const phy_ir_context *ir, phy_ir_ref expression,
                         const phy_ir_ref *needles, size_t needle_count,
                         unsigned depth)
{
    if (expression == PHY_IR_NULL || depth > 512u) {
        return true;
    }
    for (size_t i = 0u; i < needle_count; ++i) {
        if (expression == needles[i]) {
            return true;
        }
    }
    const size_t children = phy_ir_child_count(ir, expression);
    for (size_t i = 0u; i < children; ++i) {
        if (contains_ref(
                ir, phy_ir_child(ir, expression, i), needles,
                needle_count, depth + 1u)) {
            return true;
        }
    }
    return false;
}

static bool contains_kind(const phy_ir_context *ir, phy_ir_ref expression,
                          phy_ir_kind kind, unsigned depth)
{
    if (expression == PHY_IR_NULL || depth > 512u) {
        return true;
    }
    if (phy_ir_kind_of(ir, expression) == kind) {
        return true;
    }
    const size_t children = phy_ir_child_count(ir, expression);
    for (size_t i = 0u; i < children; ++i) {
        if (contains_kind(
                ir, phy_ir_child(ir, expression, i), kind,
                depth + 1u)) {
            return true;
        }
    }
    return false;
}

static bool scalar_expression(const phy_ir_context *ir, phy_ir_ref ref,
                              unsigned depth)
{
    if (ref == PHY_IR_NULL || depth > 512u) {
        return false;
    }
    const phy_ir_kind kind = phy_ir_kind_of(ir, ref);
    if (kind == PHY_IR_KIND_INVALID || kind == PHY_IR_INDEX ||
        kind == PHY_IR_ERROR || kind == PHY_IR_TENSOR ||
        kind == PHY_IR_OPERATOR || kind == PHY_IR_WEDGE) {
        return false;
    }
    const size_t children = phy_ir_child_count(ir, ref);
    for (size_t i = 0u; i < children; ++i) {
        if (!scalar_expression(
                ir, phy_ir_child(ir, ref, i), depth + 1u)) {
            return false;
        }
    }
    return true;
}

static phy_status allocate_storage(phy_coordinate_map *map,
                                   const phy_ir_ref *components)
{
    const size_t count = map->target->dimension;
    if (count > SIZE_MAX / sizeof(phy_ir_ref) ||
        count > SIZE_MAX / sizeof(phy_cas_rule)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t component_bytes = count * sizeof(phy_ir_ref);
    const size_t rules_offset =
        align_up(component_bytes, sizeof(phy_cas_rule));
    const size_t rule_bytes = count * sizeof(phy_cas_rule);
    if (rules_offset > SIZE_MAX - rule_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    map->storage_bytes = rules_offset + rule_bytes;
    if (map->storage_bytes >
        map->limits.max_bytes - sizeof(*map)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    map->storage = phy_alloc(map->storage_bytes);
    if (map->storage == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(map->storage, 0, map->storage_bytes);
    uint8_t *bytes = map->storage;
    map->components = (phy_ir_ref *)(void *)bytes;
    map->rules =
        (phy_cas_rule *)(void *)(bytes + rules_offset);
    memcpy(map->components, components, component_bytes);
    for (size_t axis = 0u; axis < count; ++axis) {
        map->rules[axis].from = map->target->coordinates[axis];
        map->rules[axis].to = map->components[axis];
    }
    return PHY_OK;
}

static phy_status build_jacobian(phy_coordinate_map *map)
{
    const size_t rows = map->target->dimension;
    const size_t columns = map->source->dimension;
    if (rows > SIZE_MAX / columns ||
        rows * columns > SIZE_MAX / sizeof(phy_ir_ref)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t count = rows * columns;
    const size_t bytes = count * sizeof(phy_ir_ref);
    if (bytes > map->limits.max_bytes - map->storage_bytes -
                    sizeof(*map)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_ir_ref *entries = phy_alloc(bytes);
    if (entries == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_status status = PHY_OK;
    for (size_t row = 0u; row < rows && status == PHY_OK; ++row) {
        for (size_t column = 0u; column < columns; ++column) {
            status = phy_cas_diff(
                map->cas, map->components[row],
                map->source->coordinates[column],
                &entries[row * columns + column]);
            if (status != PHY_OK) {
                break;
            }
            if (contains_kind(
                    map->ir, entries[row * columns + column],
                    PHY_IR_DERIVATIVE, 0u)) {
                status = PHY_ERR_UNSUPPORTED;
                break;
            }
        }
    }
    if (status == PHY_OK) {
        status = phy_matrix_create(
            map->cas, rows, columns, entries, &map->limits.linear,
            &map->jacobian);
    }
    phy_free(entries, bytes);
    return status;
}

phy_status phy_coordinate_map_create(
    const phy_component_basis *source,
    const phy_component_basis *target,
    const phy_ir_ref *components, const phy_map_limits *requested,
    phy_coordinate_map **out_map)
{
    if (source == NULL || target == NULL || components == NULL ||
        out_map == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_map = NULL;
    phy_map_limits limits;
    phy_status status = resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    if (!source->has_coordinates || !target->has_coordinates ||
        source->ir != target->ir ||
        source->dimension > limits.max_dimension ||
        target->dimension > limits.max_dimension) {
        return PHY_ERR_TYPE;
    }
    if (!coordinate_sets_disjoint(source, target)) {
        return PHY_ERR_ASSUMPTION;
    }
    for (size_t axis = 0u; axis < target->dimension; ++axis) {
        if (!scalar_expression(source->ir, components[axis], 0u)) {
            return PHY_ERR_TYPE;
        }
        if (contains_ref(
                source->ir, components[axis], target->coordinates,
                target->dimension, 0u)) {
            return PHY_ERR_TYPE;
        }
    }

    phy_abstract_context *abstract =
        phy_index_space_context(source->space);
    phy_cas *cas = phy_abstract_cas(abstract);
    if (phy_cas_ir(cas) != source->ir) {
        return PHY_ERR_TYPE;
    }
    if (phy_index_space_context(target->space) != abstract) {
        return PHY_ERR_TYPE;
    }

    phy_coordinate_map *map = phy_alloc(sizeof *map);
    if (map == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(map, 0, sizeof *map);
    map->source = source;
    map->target = target;
    map->cas = cas;
    map->ir = source->ir;
    map->limits = limits;
    status = allocate_storage(map, components);
    if (status == PHY_OK) {
        status = build_jacobian(map);
    }
    if (status != PHY_OK) {
        phy_coordinate_map_destroy(map);
        return status;
    }
    *out_map = map;
    return PHY_OK;
}

void phy_coordinate_map_destroy(phy_coordinate_map *map)
{
    if (map == NULL) {
        return;
    }
    phy_matrix_destroy(map->jacobian);
    phy_free(map->storage, map->storage_bytes);
    phy_free(map, sizeof *map);
}

const phy_component_basis *phy_coordinate_map_source(
    const phy_coordinate_map *map)
{
    return map != NULL ? map->source : NULL;
}

const phy_component_basis *phy_coordinate_map_target(
    const phy_coordinate_map *map)
{
    return map != NULL ? map->target : NULL;
}

phy_ir_ref phy_coordinate_map_component(
    const phy_coordinate_map *map, size_t target_axis)
{
    return map != NULL && target_axis < map->target->dimension
               ? map->components[target_axis]
               : PHY_IR_NULL;
}

const phy_matrix *phy_coordinate_map_jacobian(
    const phy_coordinate_map *map)
{
    return map != NULL ? map->jacobian : NULL;
}

phy_status phy_coordinate_map_pullback_scalar(
    const phy_coordinate_map *map, phy_ir_ref target_scalar,
    phy_ir_ref *out_source_scalar)
{
    if (map == NULL || target_scalar == PHY_IR_NULL ||
        out_source_scalar == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_cas_substitute(
        map->cas, target_scalar, map->rules,
        map->target->dimension, out_source_scalar);
}

phy_status phy_coordinate_map_pullback_covector(
    const phy_coordinate_map *map,
    const phy_ir_ref *target_components,
    phy_ir_ref *out_source_components)
{
    if (map == NULL || target_components == NULL ||
        out_source_components == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const size_t source_dimension = map->source->dimension;
    const size_t target_dimension = map->target->dimension;
    if (source_dimension > SIZE_MAX - target_dimension ||
        source_dimension + target_dimension >
            SIZE_MAX / sizeof(phy_ir_ref)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t count = source_dimension + target_dimension;
    const size_t bytes = count * sizeof(phy_ir_ref);
    if (bytes > map->limits.max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_ir_ref *scratch = phy_alloc(bytes);
    if (scratch == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_ir_ref *pulled = scratch;
    phy_ir_ref *result = &scratch[target_dimension];
    phy_status status = PHY_OK;
    for (size_t axis = 0u;
         axis < target_dimension && status == PHY_OK; ++axis) {
        if (!scalar_expression(map->ir, target_components[axis], 0u)) {
            status = PHY_ERR_TYPE;
            break;
        }
        status = phy_cas_substitute(
            map->cas, target_components[axis], map->rules,
            target_dimension, &pulled[axis]);
    }
    phy_ir_ref *terms = NULL;
    const size_t term_bytes =
        target_dimension * sizeof(phy_ir_ref);
    if (status == PHY_OK) {
        terms = phy_alloc(term_bytes);
        if (terms == NULL) {
            status = PHY_ERR_MEMORY_LIMIT;
        }
    }
    for (size_t source_axis = 0u;
         source_axis < source_dimension && status == PHY_OK;
         ++source_axis) {
        for (size_t target_axis = 0u;
             target_axis < target_dimension; ++target_axis) {
            phy_ir_ref derivative = PHY_IR_NULL;
            status = phy_matrix_get(
                map->jacobian, target_axis, source_axis, &derivative);
            if (status != PHY_OK) {
                break;
            }
            const phy_ir_ref product[2] = {
                pulled[target_axis], derivative};
            status = phy_cas_mul(
                map->cas, product, 2u, &terms[target_axis]);
            if (status != PHY_OK) {
                break;
            }
        }
        if (status == PHY_OK) {
            status = phy_cas_add(
                map->cas, terms, target_dimension,
                &result[source_axis]);
        }
    }
    if (status == PHY_OK) {
        memcpy(out_source_components, result,
               source_dimension * sizeof(*out_source_components));
    }
    phy_free(terms, term_bytes);
    phy_free(scratch, bytes);
    return status;
}

static phy_status prove_equal(phy_cas *cas, phy_ir_ref left,
                              phy_ir_ref right)
{
    phy_ir_ref difference = PHY_IR_NULL;
    phy_status status = phy_cas_sub(cas, left, right, &difference);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    if (status == PHY_OK) {
        status = phy_cas_is_zero(cas, difference, &decision);
    }
    if (status != PHY_OK) {
        return status;
    }
    return decision == PHY_CAS_ZERO ? PHY_OK : PHY_ERR_ASSUMPTION;
}

static phy_status verify_inverse_maps(
    const phy_coordinate_map *forward,
    const phy_coordinate_map *inverse)
{
    phy_cas *cas = forward->cas;
    for (size_t axis = 0u;
         axis < forward->source->dimension; ++axis) {
        phy_ir_ref composed = PHY_IR_NULL;
        phy_status status = phy_cas_substitute(
            cas, inverse->components[axis], forward->rules,
            forward->target->dimension, &composed);
        if (status == PHY_OK) {
            status = prove_equal(
                cas, composed, forward->source->coordinates[axis]);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    for (size_t axis = 0u;
         axis < forward->target->dimension; ++axis) {
        phy_ir_ref composed = PHY_IR_NULL;
        phy_status status = phy_cas_substitute(
            cas, forward->components[axis], inverse->rules,
            inverse->target->dimension, &composed);
        if (status == PHY_OK) {
            status = prove_equal(
                cas, composed, forward->target->coordinates[axis]);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

phy_status phy_basis_transition_create(
    const phy_component_basis *source,
    const phy_component_basis *target,
    const phy_ir_ref *target_in_source,
    const phy_ir_ref *source_in_target,
    const phy_map_limits *limits,
    phy_basis_transition **out_transition)
{
    if (source == NULL || target == NULL ||
        target_in_source == NULL || source_in_target == NULL ||
        out_transition == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_transition = NULL;
    if (source->space != target->space ||
        source->dimension != target->dimension) {
        return PHY_ERR_TYPE;
    }
    phy_coordinate_map *forward = NULL;
    phy_coordinate_map *inverse = NULL;
    phy_status status = phy_coordinate_map_create(
        source, target, target_in_source, limits, &forward);
    if (status == PHY_OK) {
        status = phy_coordinate_map_create(
            target, source, source_in_target, limits, &inverse);
    }
    if (status == PHY_OK) {
        status = verify_inverse_maps(forward, inverse);
    }
    if (status != PHY_OK) {
        phy_coordinate_map_destroy(inverse);
        phy_coordinate_map_destroy(forward);
        return status;
    }
    phy_basis_transition *transition =
        phy_alloc(sizeof *transition);
    if (transition == NULL) {
        phy_coordinate_map_destroy(inverse);
        phy_coordinate_map_destroy(forward);
        return PHY_ERR_MEMORY_LIMIT;
    }
    transition->forward = forward;
    transition->inverse = inverse;
    *out_transition = transition;
    return PHY_OK;
}

void phy_basis_transition_destroy(phy_basis_transition *transition)
{
    if (transition == NULL) {
        return;
    }
    phy_coordinate_map_destroy(transition->inverse);
    phy_coordinate_map_destroy(transition->forward);
    phy_free(transition, sizeof *transition);
}

const phy_coordinate_map *phy_basis_transition_forward(
    const phy_basis_transition *transition)
{
    return transition != NULL ? transition->forward : NULL;
}

const phy_coordinate_map *phy_basis_transition_inverse(
    const phy_basis_transition *transition)
{
    return transition != NULL ? transition->inverse : NULL;
}
