#include "map_internal.h"

#include <string.h>

#define PHY_ATLAS_DEFAULT_CHARTS 16u
#define PHY_ATLAS_DEFAULT_TRANSITIONS 64u
#define PHY_ATLAS_DEFAULT_COCYCLE_CHECKS 4096u
#define PHY_ATLAS_DEFAULT_BYTES (16u * 1024u)

static size_t align_up(size_t value, size_t alignment)
{
    const size_t remainder = value % alignment;
    return remainder == 0u ? value : value + alignment - remainder;
}

void phy_atlas_limits_defaults(phy_atlas_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_charts = PHY_ATLAS_DEFAULT_CHARTS;
    out_limits->max_transitions = PHY_ATLAS_DEFAULT_TRANSITIONS;
    out_limits->max_cocycle_checks =
        PHY_ATLAS_DEFAULT_COCYCLE_CHECKS;
    out_limits->max_bytes = PHY_ATLAS_DEFAULT_BYTES;
}

static phy_status resolve_limits(const phy_atlas_limits *requested,
                                 phy_atlas_limits *out)
{
    phy_atlas_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_charts != 0u) {
            out->max_charts = requested->max_charts;
        }
        if (requested->max_transitions != 0u) {
            out->max_transitions = requested->max_transitions;
        }
        if (requested->max_cocycle_checks != 0u) {
            out->max_cocycle_checks =
                requested->max_cocycle_checks;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
    }
    if (out->max_charts == 0u ||
        out->max_transitions == 0u ||
        out->max_cocycle_checks == 0u ||
        out->max_bytes < sizeof(phy_atlas)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

static bool storage_size(const phy_atlas_limits *limits,
                         size_t *out_chart_offset,
                         size_t *out_transition_offset,
                         size_t *out_bytes)
{
    if (limits->max_charts >
            SIZE_MAX / sizeof(const phy_component_basis *) ||
        limits->max_transitions >
            SIZE_MAX / sizeof(phy_basis_transition *)) {
        return false;
    }
    const size_t chart_bytes =
        limits->max_charts *
        sizeof(const phy_component_basis *);
    const size_t transition_offset =
        align_up(chart_bytes, sizeof(phy_basis_transition *));
    const size_t transition_bytes =
        limits->max_transitions *
        sizeof(phy_basis_transition *);
    if (transition_offset > SIZE_MAX - transition_bytes) {
        return false;
    }
    *out_chart_offset = 0u;
    *out_transition_offset = transition_offset;
    *out_bytes = transition_offset + transition_bytes;
    return true;
}

phy_status phy_atlas_create(
    const phy_component_basis *first_chart,
    const phy_atlas_limits *requested, phy_atlas **out_atlas)
{
    if (first_chart == NULL || out_atlas == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_atlas = NULL;
    if (!phy_component_basis_has_coordinates(first_chart)) {
        return PHY_ERR_TYPE;
    }
    phy_atlas_limits limits;
    phy_status status = resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    size_t chart_offset = 0u;
    size_t transition_offset = 0u;
    size_t storage_bytes = 0u;
    if (!storage_size(
            &limits, &chart_offset, &transition_offset,
            &storage_bytes) ||
        storage_bytes > limits.max_bytes - sizeof(phy_atlas)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_atlas *atlas = phy_alloc(sizeof *atlas);
    if (atlas == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(atlas, 0, sizeof *atlas);
    atlas->storage = phy_alloc(storage_bytes);
    if (atlas->storage == NULL) {
        phy_free(atlas, sizeof *atlas);
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(atlas->storage, 0, storage_bytes);
    atlas->space = phy_component_basis_space(first_chart);
    atlas->dimension =
        phy_component_basis_dimension(first_chart);
    atlas->limits = limits;
    atlas->storage_bytes = storage_bytes;
    uint8_t *bytes = atlas->storage;
    atlas->charts =
        (const phy_component_basis **)(void *)(bytes + chart_offset);
    atlas->transitions =
        (phy_basis_transition **)(void *)(bytes + transition_offset);
    atlas->charts[atlas->chart_count++] = first_chart;
    *out_atlas = atlas;
    return PHY_OK;
}

void phy_atlas_destroy(phy_atlas *atlas)
{
    if (atlas == NULL) {
        return;
    }
    for (size_t which = atlas->transition_count;
         which-- > 0u;) {
        phy_basis_transition_destroy(atlas->transitions[which]);
    }
    phy_free(atlas->storage, atlas->storage_bytes);
    phy_free(atlas, sizeof *atlas);
}

bool phy_atlas_contains_chart(
    const phy_atlas *atlas, const phy_component_basis *chart)
{
    if (atlas == NULL || chart == NULL) {
        return false;
    }
    for (size_t which = 0u; which < atlas->chart_count; ++which) {
        if (atlas->charts[which] == chart) {
            return true;
        }
    }
    return false;
}

phy_status phy_atlas_add_chart(
    phy_atlas *atlas, const phy_component_basis *chart)
{
    if (atlas == NULL || chart == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_atlas_contains_chart(atlas, chart)) {
        return PHY_ERR_ASSUMPTION;
    }
    if (!phy_component_basis_has_coordinates(chart) ||
        phy_component_basis_space(chart) != atlas->space ||
        phy_component_basis_dimension(chart) != atlas->dimension) {
        return PHY_ERR_TYPE;
    }
    if (atlas->chart_count >= atlas->limits.max_charts) {
        return PHY_ERR_TERM_LIMIT;
    }
    atlas->charts[atlas->chart_count++] = chart;
    return PHY_OK;
}

size_t phy_atlas_chart_count(const phy_atlas *atlas)
{
    return atlas != NULL ? atlas->chart_count : 0u;
}

size_t phy_atlas_transition_count(const phy_atlas *atlas)
{
    return atlas != NULL ? atlas->transition_count : 0u;
}

size_t phy_atlas_bytes_used(const phy_atlas *atlas)
{
    return atlas != NULL ? sizeof(*atlas) + atlas->storage_bytes : 0u;
}

const phy_coordinate_map *phy_atlas_transition_map(
    const phy_atlas *atlas, const phy_component_basis *source,
    const phy_component_basis *target)
{
    if (atlas == NULL || source == NULL || target == NULL ||
        source == target) {
        return NULL;
    }
    for (size_t which = 0u; which < atlas->transition_count; ++which) {
        const phy_basis_transition *transition =
            atlas->transitions[which];
        phy_coordinate_map *forward = transition->forward;
        if (phy_coordinate_map_source(forward) == source &&
            phy_coordinate_map_target(forward) == target) {
            return forward;
        }
        phy_coordinate_map *inverse = transition->inverse;
        if (phy_coordinate_map_source(inverse) == source &&
            phy_coordinate_map_target(inverse) == target) {
            return inverse;
        }
    }
    return NULL;
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

phy_status phy_atlas_verify_cocycles(
    const phy_atlas *atlas, size_t *out_checks)
{
    if (atlas == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    size_t checks = 0u;
    for (size_t a = 0u; a < atlas->chart_count; ++a) {
        for (size_t b = 0u; b < atlas->chart_count; ++b) {
            if (b == a) {
                continue;
            }
            const phy_coordinate_map *ab =
                phy_atlas_transition_map(
                    atlas, atlas->charts[a], atlas->charts[b]);
            if (ab == NULL) {
                continue;
            }
            for (size_t c = 0u; c < atlas->chart_count; ++c) {
                if (c == a || c == b) {
                    continue;
                }
                const phy_coordinate_map *bc =
                    phy_atlas_transition_map(
                        atlas, atlas->charts[b], atlas->charts[c]);
                const phy_coordinate_map *ac =
                    phy_atlas_transition_map(
                        atlas, atlas->charts[a], atlas->charts[c]);
                if (bc == NULL || ac == NULL) {
                    continue;
                }
                for (size_t axis = 0u; axis < atlas->dimension;
                     ++axis) {
                    if (checks >=
                        atlas->limits.max_cocycle_checks) {
                        return PHY_ERR_TERM_LIMIT;
                    }
                    phy_ir_ref composed = PHY_IR_NULL;
                    phy_status status = phy_cas_substitute(
                        ab->cas, bc->components[axis], ab->rules,
                        ab->target->dimension, &composed);
                    if (status == PHY_OK) {
                        status = prove_equal(
                            ab->cas, composed,
                            ac->components[axis]);
                    }
                    if (status != PHY_OK) {
                        return status;
                    }
                    ++checks;
                }
            }
        }
    }
    if (out_checks != NULL) {
        *out_checks = checks;
    }
    return PHY_OK;
}

phy_status phy_atlas_add_transition(
    phy_atlas *atlas, const phy_component_basis *source,
    const phy_component_basis *target,
    const phy_ir_ref *target_in_source,
    const phy_ir_ref *source_in_target,
    const phy_map_limits *map_limits)
{
    if (atlas == NULL || source == NULL || target == NULL ||
        target_in_source == NULL || source_in_target == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (source == target ||
        !phy_atlas_contains_chart(atlas, source) ||
        !phy_atlas_contains_chart(atlas, target)) {
        return PHY_ERR_TYPE;
    }
    if (phy_atlas_transition_map(atlas, source, target) != NULL) {
        return PHY_ERR_ASSUMPTION;
    }
    if (atlas->transition_count >=
        atlas->limits.max_transitions) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_basis_transition *transition = NULL;
    phy_status status = phy_basis_transition_create(
        source, target, target_in_source, source_in_target,
        map_limits, &transition);
    if (status != PHY_OK) {
        return status;
    }
    atlas->transitions[atlas->transition_count++] = transition;
    status = phy_atlas_verify_cocycles(atlas, NULL);
    if (status != PHY_OK) {
        --atlas->transition_count;
        atlas->transitions[atlas->transition_count] = NULL;
        phy_basis_transition_destroy(transition);
        return status;
    }
    return PHY_OK;
}

phy_status phy_atlas_pullback_tensor(
    const phy_atlas *atlas, const phy_component_basis *source,
    const phy_component_basis *target, size_t rank,
    const phy_ir_variance *valence,
    const phy_ir_ref *target_components,
    phy_ir_ref *out_source_components)
{
    if (atlas == NULL || source == NULL || target == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t which = 0u; which < atlas->transition_count; ++which) {
        phy_basis_transition *transition =
            atlas->transitions[which];
        phy_coordinate_map *forward = transition->forward;
        if (phy_coordinate_map_source(forward) == source &&
            phy_coordinate_map_target(forward) == target) {
            return phy_basis_transition_pullback_tensor(
                transition, rank, valence, target_components,
                out_source_components);
        }
        phy_coordinate_map *inverse = transition->inverse;
        if (phy_coordinate_map_source(inverse) == source &&
            phy_coordinate_map_target(inverse) == target) {
            phy_basis_transition reversed = {inverse, forward};
            return phy_basis_transition_pullback_tensor(
                &reversed, rank, valence, target_components,
                out_source_components);
        }
    }
    return PHY_ERR_ASSUMPTION;
}
