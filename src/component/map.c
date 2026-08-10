#include "map_internal.h"

#include <string.h>

#define PHY_MAP_DEFAULT_DIMENSION 32u
#define PHY_MAP_DEFAULT_BYTES (128u * 1024u)
#define PHY_MAP_DEFAULT_FORM_COMPONENTS 4096u
#define PHY_MAP_DEFAULT_PULLBACK_TERMS 250000u
#define PHY_MAP_DEFAULT_TENSOR_RANK 32u
#define PHY_MAP_DEFAULT_TENSOR_COMPONENTS 4096u
#define PHY_MAP_DEFAULT_TRANSFORM_TERMS 250000u

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
    out_limits->max_form_components =
        PHY_MAP_DEFAULT_FORM_COMPONENTS;
    out_limits->max_pullback_terms =
        PHY_MAP_DEFAULT_PULLBACK_TERMS;
    out_limits->max_tensor_rank =
        PHY_MAP_DEFAULT_TENSOR_RANK;
    out_limits->max_tensor_components =
        PHY_MAP_DEFAULT_TENSOR_COMPONENTS;
    out_limits->max_transform_terms =
        PHY_MAP_DEFAULT_TRANSFORM_TERMS;
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
        if (requested->max_form_components != 0u) {
            out->max_form_components =
                requested->max_form_components;
        }
        if (requested->max_pullback_terms != 0u) {
            out->max_pullback_terms =
                requested->max_pullback_terms;
        }
        if (requested->max_tensor_rank != 0u) {
            out->max_tensor_rank = requested->max_tensor_rank;
        }
        if (requested->max_tensor_components != 0u) {
            out->max_tensor_components =
                requested->max_tensor_components;
        }
        if (requested->max_transform_terms != 0u) {
            out->max_transform_terms =
                requested->max_transform_terms;
        }
        out->linear = requested->linear;
    }
    if (out->max_dimension == 0u ||
        out->max_bytes < sizeof(phy_coordinate_map) ||
        out->max_form_components == 0u ||
        out->max_pullback_terms == 0u ||
        out->max_tensor_rank == 0u ||
        out->max_tensor_components == 0u ||
        out->max_transform_terms == 0u) {
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

static size_t size_gcd(size_t left, size_t right)
{
    while (right != 0u) {
        const size_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

static phy_status exterior_component_count(
    size_t dimension, size_t degree, size_t limit, size_t *out_count)
{
    if (out_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (degree > dimension) {
        *out_count = 0u;
        return PHY_OK;
    }
    size_t choose = degree;
    if (choose > dimension - choose) {
        choose = dimension - choose;
    }
    size_t count = 1u;
    for (size_t step = 1u; step <= choose; ++step) {
        size_t numerator = dimension - choose + step;
        size_t denominator = step;
        size_t divisor = size_gcd(numerator, denominator);
        numerator /= divisor;
        denominator /= divisor;
        divisor = size_gcd(count, denominator);
        count /= divisor;
        denominator /= divisor;
        if (denominator != 1u ||
            (numerator != 0u && count > SIZE_MAX / numerator)) {
            return PHY_ERR_MEMORY_LIMIT;
        }
        count *= numerator;
        if (count > limit) {
            return PHY_ERR_TERM_LIMIT;
        }
    }
    *out_count = count;
    return PHY_OK;
}

phy_status phy_coordinate_map_form_component_counts(
    const phy_coordinate_map *map, size_t degree,
    size_t *out_target_count, size_t *out_source_count)
{
    if (map == NULL || out_target_count == NULL ||
        out_source_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = exterior_component_count(
        map->target->dimension, degree,
        map->limits.max_form_components, out_target_count);
    if (status == PHY_OK) {
        status = exterior_component_count(
            map->source->dimension, degree,
            map->limits.max_form_components, out_source_count);
    }
    return status;
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

static void fill_combinations(size_t dimension, size_t degree,
                              size_t count, size_t *out)
{
    if (degree == 0u || count == 0u) {
        return;
    }
    for (size_t axis = 0u; axis < degree; ++axis) {
        out[axis] = axis;
    }
    for (size_t row = 1u; row < count; ++row) {
        size_t *previous = &out[(row - 1u) * degree];
        size_t *current = &out[row * degree];
        memcpy(current, previous, degree * sizeof(*current));
        size_t axis = degree;
        while (axis > 0u) {
            --axis;
            const size_t ceiling = dimension - degree + axis;
            if (current[axis] < ceiling) {
                ++current[axis];
                for (size_t tail = axis + 1u;
                     tail < degree; ++tail) {
                    current[tail] = current[tail - 1u] + 1u;
                }
                break;
            }
        }
    }
}

static bool add_scratch_bytes(size_t count, size_t element_size,
                              size_t *total)
{
    if (count != 0u && element_size > SIZE_MAX / count) {
        return false;
    }
    const size_t bytes = count * element_size;
    if (*total > SIZE_MAX - bytes) {
        return false;
    }
    *total += bytes;
    return true;
}

phy_status phy_coordinate_map_pullback_form(
    const phy_coordinate_map *map, size_t degree,
    const phy_ir_ref *target_components,
    phy_ir_ref *out_source_components)
{
    if (map == NULL || target_components == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    size_t target_count = 0u;
    size_t source_count = 0u;
    phy_status status = phy_coordinate_map_form_component_counts(
        map, degree, &target_count, &source_count);
    if (status != PHY_OK) {
        return status;
    }
    if (degree > map->target->dimension) {
        return PHY_ERR_DOMAIN;
    }
    if (source_count != 0u && out_source_components == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (source_count == 0u) {
        return PHY_OK;
    }
    if (degree == 0u) {
        return phy_coordinate_map_pullback_scalar(
            map, target_components[0], &out_source_components[0]);
    }
    if (target_count != 0u &&
        source_count >
            map->limits.max_pullback_terms / target_count) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (target_count > SIZE_MAX / degree ||
        source_count > SIZE_MAX / degree ||
        degree > SIZE_MAX / degree) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t target_index_count = target_count * degree;
    const size_t source_index_count = source_count * degree;
    const size_t minor_entry_count = degree * degree;
    size_t scratch_bytes = 0u;
    if (!add_scratch_bytes(
            target_count, sizeof(phy_ir_ref), &scratch_bytes) ||
        !add_scratch_bytes(
            target_count, sizeof(phy_ir_ref), &scratch_bytes) ||
        !add_scratch_bytes(
            source_count, sizeof(phy_ir_ref), &scratch_bytes) ||
        !add_scratch_bytes(
            target_index_count, sizeof(size_t), &scratch_bytes) ||
        !add_scratch_bytes(
            source_index_count, sizeof(size_t), &scratch_bytes) ||
        !add_scratch_bytes(
            minor_entry_count, sizeof(phy_ir_ref), &scratch_bytes) ||
        scratch_bytes > map->limits.max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }

    phy_ir_ref *pulled =
        phy_alloc(target_count * sizeof(*pulled));
    phy_ir_ref *terms =
        phy_alloc(target_count * sizeof(*terms));
    phy_ir_ref *result =
        phy_alloc(source_count * sizeof(*result));
    size_t *target_indices =
        phy_alloc(target_index_count * sizeof(*target_indices));
    size_t *source_indices =
        phy_alloc(source_index_count * sizeof(*source_indices));
    phy_ir_ref *minor_entries =
        phy_alloc(minor_entry_count * sizeof(*minor_entries));
    if (pulled == NULL || terms == NULL || result == NULL ||
        target_indices == NULL || source_indices == NULL ||
        minor_entries == NULL) {
        status = PHY_ERR_MEMORY_LIMIT;
    }

    for (size_t axis = 0u;
         axis < target_count && status == PHY_OK; ++axis) {
        if (!scalar_expression(
                map->ir, target_components[axis], 0u)) {
            status = PHY_ERR_TYPE;
            break;
        }
        status = phy_cas_substitute(
            map->cas, target_components[axis], map->rules,
            map->target->dimension, &pulled[axis]);
    }
    if (status == PHY_OK) {
        fill_combinations(
            map->target->dimension, degree, target_count,
            target_indices);
        fill_combinations(
            map->source->dimension, degree, source_count,
            source_indices);
    }

    phy_matrix *minor = NULL;
    if (status == PHY_OK) {
        status = phy_matrix_create(
            map->cas, degree, degree, NULL, &map->limits.linear,
            &minor);
    }
    for (size_t source_component = 0u;
         source_component < source_count && status == PHY_OK;
         ++source_component) {
        const size_t *source_axes =
            &source_indices[source_component * degree];
        for (size_t target_component = 0u;
             target_component < target_count && status == PHY_OK;
             ++target_component) {
            const size_t *target_axes =
                &target_indices[target_component * degree];
            for (size_t row = 0u;
                 row < degree && status == PHY_OK; ++row) {
                for (size_t column = 0u;
                     column < degree; ++column) {
                    status = phy_matrix_get(
                        map->jacobian, target_axes[row],
                        source_axes[column],
                        &minor_entries[row * degree + column]);
                    if (status == PHY_OK) {
                        status = phy_matrix_set(
                            minor, row, column,
                            minor_entries[row * degree + column]);
                    }
                    if (status != PHY_OK) {
                        break;
                    }
                }
            }
            phy_ir_ref determinant = PHY_IR_NULL;
            if (status == PHY_OK) {
                status = phy_matrix_determinant(
                    minor, &determinant);
            }
            const phy_ir_ref product[2] = {
                pulled[target_component], determinant};
            if (status == PHY_OK) {
                status = phy_cas_mul(
                    map->cas, product, 2u,
                    &terms[target_component]);
            }
        }
        if (status == PHY_OK) {
            status = phy_cas_add(
                map->cas, terms, target_count,
                &result[source_component]);
        }
    }
    if (status == PHY_OK) {
        memcpy(out_source_components, result,
               source_count * sizeof(*out_source_components));
    }
    phy_matrix_destroy(minor);
    phy_free(
        minor_entries,
        minor_entry_count * sizeof(*minor_entries));
    phy_free(
        source_indices,
        source_index_count * sizeof(*source_indices));
    phy_free(
        target_indices,
        target_index_count * sizeof(*target_indices));
    phy_free(result, source_count * sizeof(*result));
    phy_free(terms, target_count * sizeof(*terms));
    phy_free(pulled, target_count * sizeof(*pulled));
    return status;
}

phy_status phy_coordinate_map_pullback_covector(
    const phy_coordinate_map *map,
    const phy_ir_ref *target_components,
    phy_ir_ref *out_source_components)
{
    return phy_coordinate_map_pullback_form(
        map, 1u, target_components, out_source_components);
}

phy_status phy_coordinate_map_pushforward_vector_along(
    const phy_coordinate_map *map,
    const phy_ir_ref *source_components,
    phy_ir_ref *out_target_components)
{
    if (map == NULL || source_components == NULL ||
        out_target_components == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const size_t source_dimension = map->source->dimension;
    const size_t target_dimension = map->target->dimension;
    if (source_dimension > SIZE_MAX - target_dimension ||
        source_dimension + target_dimension >
            map->limits.max_bytes / sizeof(phy_ir_ref)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t term_bytes =
        source_dimension * sizeof(phy_ir_ref);
    const size_t result_bytes =
        target_dimension * sizeof(phy_ir_ref);
    phy_ir_ref *terms = phy_alloc(term_bytes);
    phy_ir_ref *result = phy_alloc(result_bytes);
    if (terms == NULL || result == NULL) {
        phy_free(result, result_bytes);
        phy_free(terms, term_bytes);
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_status status = PHY_OK;
    for (size_t source_axis = 0u;
         source_axis < source_dimension; ++source_axis) {
        if (!scalar_expression(
                map->ir, source_components[source_axis], 0u) ||
            contains_ref(
                map->ir, source_components[source_axis],
                map->target->coordinates, target_dimension, 0u)) {
            status = PHY_ERR_TYPE;
            break;
        }
    }
    for (size_t target_axis = 0u;
         target_axis < target_dimension && status == PHY_OK;
         ++target_axis) {
        for (size_t source_axis = 0u;
             source_axis < source_dimension; ++source_axis) {
            phy_ir_ref derivative = PHY_IR_NULL;
            status = phy_matrix_get(
                map->jacobian, target_axis, source_axis,
                &derivative);
            if (status != PHY_OK) {
                break;
            }
            const phy_ir_ref product[2] = {
                derivative, source_components[source_axis]};
            status = phy_cas_mul(
                map->cas, product, 2u, &terms[source_axis]);
            if (status != PHY_OK) {
                break;
            }
        }
        if (status == PHY_OK) {
            status = phy_cas_add(
                map->cas, terms, source_dimension,
                &result[target_axis]);
        }
    }
    if (status == PHY_OK) {
        memcpy(out_target_components, result, result_bytes);
    }
    phy_free(result, result_bytes);
    phy_free(terms, term_bytes);
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

static phy_status tensor_component_count(
    size_t dimension, size_t rank, size_t limit, size_t *out_count)
{
    size_t count = 1u;
    for (size_t slot = 0u; slot < rank; ++slot) {
        if (dimension != 0u && count > limit / dimension) {
            return PHY_ERR_TERM_LIMIT;
        }
        count *= dimension;
    }
    *out_count = count;
    return PHY_OK;
}

static bool add_tensor_scratch(size_t count, size_t element_size,
                               size_t *total)
{
    if (count != 0u && element_size > SIZE_MAX / count) {
        return false;
    }
    const size_t bytes = count * element_size;
    if (*total > SIZE_MAX - bytes) {
        return false;
    }
    *total += bytes;
    return true;
}

phy_status phy_basis_transition_pullback_tensor(
    const phy_basis_transition *transition, size_t rank,
    const phy_ir_variance *valence,
    const phy_ir_ref *target_components,
    phy_ir_ref *out_source_components)
{
    if (transition == NULL || target_components == NULL ||
        out_source_components == NULL ||
        (rank != 0u && valence == NULL)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_coordinate_map *forward = transition->forward;
    const phy_coordinate_map *inverse = transition->inverse;
    if (rank > forward->limits.max_tensor_rank) {
        return PHY_ERR_TERM_LIMIT;
    }
    const size_t dimension = forward->source->dimension;
    size_t component_count = 0u;
    phy_status status = tensor_component_count(
        dimension, rank, forward->limits.max_tensor_components,
        &component_count);
    if (status != PHY_OK) {
        return status;
    }
    if (component_count != 0u &&
        (uint64_t)component_count >
            forward->limits.max_transform_terms /
                (uint64_t)component_count) {
        return PHY_ERR_TERM_LIMIT;
    }
    for (size_t slot = 0u; slot < rank; ++slot) {
        if (valence[slot] != PHY_IR_INDEX_LOWER &&
            valence[slot] != PHY_IR_INDEX_UPPER) {
            return PHY_ERR_TYPE;
        }
    }
    if (rank == 0u) {
        return phy_coordinate_map_pullback_scalar(
            forward, target_components[0],
            &out_source_components[0]);
    }

    const size_t jacobian_count = dimension * dimension;
    size_t scratch_bytes = 0u;
    if (dimension != 0u &&
        jacobian_count / dimension != dimension) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    if (!add_tensor_scratch(
            component_count, sizeof(phy_ir_ref),
            &scratch_bytes) ||
        !add_tensor_scratch(
            component_count, sizeof(phy_ir_ref),
            &scratch_bytes) ||
        !add_tensor_scratch(
            component_count, sizeof(phy_ir_ref),
            &scratch_bytes) ||
        !add_tensor_scratch(
            jacobian_count, sizeof(phy_ir_ref),
            &scratch_bytes) ||
        !add_tensor_scratch(
            rank + 1u, sizeof(phy_ir_ref), &scratch_bytes) ||
        scratch_bytes > forward->limits.max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    uint8_t *scratch = phy_alloc(scratch_bytes);
    if (scratch == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_ir_ref *substituted = (phy_ir_ref *)(void *)scratch;
    phy_ir_ref *terms = substituted + component_count;
    phy_ir_ref *results = terms + component_count;
    phy_ir_ref *inverse_jacobian = results + component_count;
    phy_ir_ref *factors = inverse_jacobian + jacobian_count;

    for (size_t flat = 0u;
         flat < component_count && status == PHY_OK; ++flat) {
        if (!scalar_expression(
                forward->ir, target_components[flat], 0u) ||
            contains_ref(
                forward->ir, target_components[flat],
                forward->source->coordinates, dimension, 0u)) {
            status = PHY_ERR_TYPE;
            break;
        }
        status = phy_coordinate_map_pullback_scalar(
            forward, target_components[flat],
            &substituted[flat]);
    }
    for (size_t source_axis = 0u;
         source_axis < dimension && status == PHY_OK;
         ++source_axis) {
        for (size_t target_axis = 0u;
             target_axis < dimension; ++target_axis) {
            phy_ir_ref entry = PHY_IR_NULL;
            status = phy_matrix_get(
                inverse->jacobian, source_axis, target_axis,
                &entry);
            if (status == PHY_OK) {
                status = phy_coordinate_map_pullback_scalar(
                    forward, entry,
                    &inverse_jacobian[
                        source_axis * dimension + target_axis]);
            }
            if (status != PHY_OK) {
                break;
            }
        }
    }

    for (size_t source_flat = 0u;
         source_flat < component_count && status == PHY_OK;
         ++source_flat) {
        for (size_t target_flat = 0u;
             target_flat < component_count; ++target_flat) {
            factors[0] = substituted[target_flat];
            size_t source_digits = source_flat;
            size_t target_digits = target_flat;
            for (size_t reversed = rank; reversed-- > 0u;) {
                const size_t source_axis =
                    source_digits % dimension;
                const size_t target_axis =
                    target_digits % dimension;
                source_digits /= dimension;
                target_digits /= dimension;
                if (valence[reversed] == PHY_IR_INDEX_LOWER) {
                    status = phy_matrix_get(
                        forward->jacobian, target_axis,
                        source_axis, &factors[reversed + 1u]);
                } else {
                    factors[reversed + 1u] =
                        inverse_jacobian[
                            source_axis * dimension + target_axis];
                }
                if (status != PHY_OK) {
                    break;
                }
            }
            if (status == PHY_OK) {
                status = phy_cas_mul(
                    forward->cas, factors, rank + 1u,
                    &terms[target_flat]);
            }
            if (status != PHY_OK) {
                break;
            }
        }
        if (status == PHY_OK) {
            status = phy_cas_add(
                forward->cas, terms, component_count,
                &results[source_flat]);
        }
    }
    if (status == PHY_OK) {
        memcpy(
            out_source_components, results,
            component_count * sizeof(*results));
    }
    phy_free(scratch, scratch_bytes);
    return status;
}
