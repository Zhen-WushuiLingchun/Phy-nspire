/*
 * Phy-nspire — validated coordinate maps, chart transitions and pullbacks.
 */
#ifndef PHY_MAP_H
#define PHY_MAP_H

#include <stddef.h>

#include "phy/component_tensor.h"
#include "phy/linear.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_coordinate_map phy_coordinate_map;
typedef struct phy_basis_transition phy_basis_transition;
typedef struct phy_atlas phy_atlas;

typedef struct {
    size_t max_dimension; /* source or target; default 32 */
    size_t max_bytes;     /* map metadata; default 128 KiB */
    size_t max_form_components; /* one exterior-power basis; default 4096 */
    uint32_t max_pullback_terms; /* minors accumulated; default 250000 */
    size_t max_tensor_rank;       /* general tensor slots; default 32 */
    size_t max_tensor_components; /* dense change-of-basis side; default 4096 */
    uint64_t max_transform_terms; /* component pairs; default 250000 */
    phy_linear_limits linear;
} phy_map_limits;

void phy_map_limits_defaults(phy_map_limits *out_limits);

/*
 * Create F: source -> target from target-coordinate components
 *
 *     y^a = components[a](x).
 *
 * Both bases must carry coordinates and live in the same IR/CAS context.
 * Source and target coordinate symbol sets must be disjoint.  A component may
 * depend on source coordinates and symbolic parameters, but direct target
 * coordinate occurrences are rejected before differentiation.
 */
phy_status phy_coordinate_map_create(
    const phy_component_basis *source,
    const phy_component_basis *target,
    const phy_ir_ref *components,
    const phy_map_limits *limits,
    phy_coordinate_map **out_map);
void phy_coordinate_map_destroy(phy_coordinate_map *map);

const phy_component_basis *phy_coordinate_map_source(
    const phy_coordinate_map *map);
const phy_component_basis *phy_coordinate_map_target(
    const phy_coordinate_map *map);
phy_ir_ref phy_coordinate_map_component(
    const phy_coordinate_map *map, size_t target_axis);
const phy_matrix *phy_coordinate_map_jacobian(
    const phy_coordinate_map *map); /* J[a,i] = d y^a / d x^i */

/*
 * Report target/source component counts for an alternating p-form.
 * Components use increasing index tuples in lexicographic order. Degree zero
 * has one scalar component. If degree exceeds a basis dimension, that side has
 * zero components.
 */
phy_status phy_coordinate_map_form_component_counts(
    const phy_coordinate_map *map, size_t degree,
    size_t *out_target_count, size_t *out_source_count);

/*
 * Pull back an alternating p-form through exact Jacobian minors. The array
 * lengths are reported by phy_coordinate_map_form_component_counts.
 * `out_source_components` may be NULL only when its reported count is zero.
 */
phy_status phy_coordinate_map_pullback_form(
    const phy_coordinate_map *map, size_t degree,
    const phy_ir_ref *target_components,
    phy_ir_ref *out_source_components);

/* F* f and F* alpha remain convenient degree-zero/one wrappers. */
phy_status phy_coordinate_map_pullback_scalar(
    const phy_coordinate_map *map, phy_ir_ref target_scalar,
    phy_ir_ref *out_source_scalar);
phy_status phy_coordinate_map_pullback_covector(
    const phy_coordinate_map *map,
    const phy_ir_ref *target_components,
    phy_ir_ref *out_source_components);

/*
 * Push a source vector forward along F. The result is expressed over source
 * coordinates along the image; it is not a global target vector field away
 * from F(source).
 */
phy_status phy_coordinate_map_pushforward_vector_along(
    const phy_coordinate_map *map,
    const phy_ir_ref *source_components,
    phy_ir_ref *out_target_components);

/*
 * A verified chart transition is a pair of coordinate maps proved inverse by
 * exact substitution and zero decision in both directions.
 */
phy_status phy_basis_transition_create(
    const phy_component_basis *source,
    const phy_component_basis *target,
    const phy_ir_ref *target_in_source,
    const phy_ir_ref *source_in_target,
    const phy_map_limits *limits,
    phy_basis_transition **out_transition);
void phy_basis_transition_destroy(phy_basis_transition *transition);
const phy_coordinate_map *phy_basis_transition_forward(
    const phy_basis_transition *transition);
const phy_coordinate_map *phy_basis_transition_inverse(
    const phy_basis_transition *transition);

/*
 * Pull a general target-chart tensor back to the source chart of a verified
 * transition. Components are dense lexicographic arrays of dimension^rank.
 * Lower slots use the forward Jacobian; upper slots use the inverse Jacobian
 * evaluated in source coordinates. Rank zero is scalar substitution.
 */
phy_status phy_basis_transition_pullback_tensor(
    const phy_basis_transition *transition, size_t rank,
    const phy_ir_variance *valence,
    const phy_ir_ref *target_components,
    phy_ir_ref *out_source_components);

typedef struct {
    size_t max_charts;          /* default 16 */
    size_t max_transitions;     /* undirected chart pairs; default 64 */
    uint32_t max_cocycle_checks; /* exact component identities; default 4096 */
    size_t max_bytes;           /* atlas registry only; default 16 KiB */
} phy_atlas_limits;

void phy_atlas_limits_defaults(phy_atlas_limits *out_limits);

/*
 * Create an atlas containing `first_chart`. Every later chart must bind the
 * same IndexSpace at the same concrete dimension. The atlas borrows charts,
 * so it must be destroyed before their component bases.
 */
phy_status phy_atlas_create(
    const phy_component_basis *first_chart,
    const phy_atlas_limits *limits, phy_atlas **out_atlas);
void phy_atlas_destroy(phy_atlas *atlas);

phy_status phy_atlas_add_chart(
    phy_atlas *atlas, const phy_component_basis *chart);
bool phy_atlas_contains_chart(
    const phy_atlas *atlas, const phy_component_basis *chart);
size_t phy_atlas_chart_count(const phy_atlas *atlas);
size_t phy_atlas_transition_count(const phy_atlas *atlas);
size_t phy_atlas_bytes_used(const phy_atlas *atlas);

/*
 * Construct and register one verified two-way chart transition. Registration
 * is transactional: every newly closed triangle must satisfy
 *
 *     g_ac = g_bc o g_ab
 *
 * componentwise by exact substitution and zero decision. Unknown equality is
 * rejected as PHY_ERR_ASSUMPTION; an inconsistent edge is destroyed and the
 * previous atlas is unchanged.
 */
phy_status phy_atlas_add_transition(
    phy_atlas *atlas, const phy_component_basis *source,
    const phy_component_basis *target,
    const phy_ir_ref *target_in_source,
    const phy_ir_ref *source_in_target,
    const phy_map_limits *map_limits);

/*
 * Return a registered directed map, or NULL for an identity/missing edge.
 * Both directions of every registered transition are available.
 */
const phy_coordinate_map *phy_atlas_transition_map(
    const phy_atlas *atlas, const phy_component_basis *source,
    const phy_component_basis *target);

/* Recheck every closed chart triangle and report component identities tested. */
phy_status phy_atlas_verify_cocycles(
    const phy_atlas *atlas, size_t *out_checks);

/*
 * Directed atlas wrapper around the verified general tensor transformation.
 * The input is expressed in `target`, the result in `source`.
 */
phy_status phy_atlas_pullback_tensor(
    const phy_atlas *atlas, const phy_component_basis *source,
    const phy_component_basis *target, size_t rank,
    const phy_ir_variance *valence,
    const phy_ir_ref *target_components,
    phy_ir_ref *out_source_components);

#ifdef __cplusplus
}
#endif

#endif /* PHY_MAP_H */
