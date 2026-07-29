#ifndef PHY_MAP_INTERNAL_H
#define PHY_MAP_INTERNAL_H

#include "component_internal.h"
#include "phy/map.h"

struct phy_coordinate_map {
    const phy_component_basis *source;
    const phy_component_basis *target;
    phy_cas *cas;
    phy_ir_context *ir;
    phy_map_limits limits;
    phy_ir_ref *components;
    phy_cas_rule *rules;
    void *storage;
    size_t storage_bytes;
    phy_matrix *jacobian;
};

struct phy_basis_transition {
    phy_coordinate_map *forward;
    phy_coordinate_map *inverse;
};

#endif /* PHY_MAP_INTERNAL_H */
