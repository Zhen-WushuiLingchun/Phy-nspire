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

typedef struct {
    size_t max_dimension; /* source or target; default 32 */
    size_t max_bytes;     /* map metadata; default 128 KiB */
    size_t max_form_components; /* one exterior-power basis; default 4096 */
    uint32_t max_pullback_terms; /* minors accumulated; default 250000 */
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

#ifdef __cplusplus
}
#endif

#endif /* PHY_MAP_H */
