#ifndef PHY_COMPONENT_INTERNAL_H
#define PHY_COMPONENT_INTERNAL_H

#include "phy/component_tensor.h"
#include "phy/platform.h"
#include "permutation_internal.h"

struct phy_component_basis {
    const phy_index_space *space;
    phy_ir_context *ir;
    phy_ir_symbol name;
    size_t dimension;
    bool has_coordinates;
    phy_ir_symbol *coordinate_symbols;
    phy_ir_ref *coordinates;
    void *storage;
    size_t storage_bytes;
};

struct phy_component_tensor {
    const phy_tensor_head *head;
    phy_cas *cas;
    phy_ir_context *ir;
    phy_component_limits limits;
    size_t rank;

    phy_component_basis **bases;
    phy_ir_variance *valence;
    void *metadata;
    size_t metadata_bytes;

    phy_perm_group *group;
    phy_perm_chain chain;
    uint16_t *permutation_stack;
    uint16_t *choice_permutation;
    uint8_t *processed_points;
    uint32_t *best_indices;
    size_t scratch_bytes;

    void *entries;
    uint32_t *entry_indices;
    phy_ir_ref *entry_values;
    size_t entry_count;
    size_t entry_capacity;
    size_t entry_bytes;

    size_t other_budget;
    size_t bytes_used;
    phy_ir_ref zero;
};

#endif /* PHY_COMPONENT_INTERNAL_H */
