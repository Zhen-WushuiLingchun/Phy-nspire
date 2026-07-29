/*
 * Device-only retention probe for the dynamic component and bridge API.
 * It is never part of the product; the companion script links it with
 * --gc-sections so every public entry point must genuinely resolve on ARM.
 */
#include "phy/abstract_tensor.h"
#include "phy/cas.h"
#include "phy/component_tensor.h"
#include "phy/ir.h"
#include "phy/platform.h"

static volatile unsigned probe_sink;

static void sink(unsigned value)
{
    probe_sink += value;
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        return 1;
    }
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = ir != NULL ? phy_cas_create(ir, NULL) : NULL;
    phy_abstract_context *abstract = NULL;
    if (cas == NULL ||
        phy_abstract_context_create(cas, NULL, &abstract) != PHY_OK) {
        phy_cas_destroy(cas);
        phy_ir_context_destroy(ir);
        phy_platform_shutdown();
        return 1;
    }

    phy_index_space *space = NULL;
    sink((unsigned)phy_index_space_create(
        abstract, "M", phy_ir_integer(ir, 2), PHY_METRIC_NONE,
        &space));

    phy_basis_limits basis_limits;
    phy_basis_limits_defaults(&basis_limits);
    phy_component_basis *basis = NULL;
    if (space != NULL) {
        sink((unsigned)phy_component_basis_create(
            space, "e", 2u, NULL, &basis_limits, &basis));
    }
    sink(phy_component_basis_space(basis) != NULL ? 1u : 0u);
    sink(phy_component_basis_name(basis) != NULL ? 1u : 0u);
    sink((unsigned)phy_component_basis_dimension(basis));
    sink(phy_component_basis_has_coordinates(basis) ? 1u : 0u);
    sink((unsigned)phy_component_basis_coordinate_symbol(basis, 0u));
    sink((unsigned)phy_component_basis_coordinate(basis, 0u));

    const phy_index_space *slots[1] = {space};
    phy_abstract_tensor_head *head = NULL;
    if (space != NULL) {
        sink((unsigned)phy_tensor_head_create(
            abstract, "V", slots, 1u, PHY_TENSOR_COMMUTING, &head));
    }
    phy_component_limits component_limits;
    phy_component_limits_defaults(&component_limits);
    phy_component_tensor *tensor = NULL;
    phy_component_basis *bases[1] = {basis};
    const phy_ir_variance valence[1] = {PHY_IR_INDEX_LOWER};
    if (head != NULL && basis != NULL) {
        sink((unsigned)phy_component_tensor_create(
            head, bases, valence, &component_limits, &tensor));
    }
    sink(phy_component_tensor_head(tensor) != NULL ? 1u : 0u);
    sink((unsigned)phy_component_tensor_rank(tensor));
    sink(phy_component_tensor_basis(tensor, 0u) != NULL ? 1u : 0u);
    sink((unsigned)phy_component_tensor_valence(tensor, 0u));
    sink((unsigned)phy_component_tensor_entry_count(tensor));
    sink((unsigned)phy_component_tensor_symmetry_order(tensor));
    sink((unsigned)phy_component_tensor_bytes_used(tensor));

    const uint32_t coordinate[1] = {0u};
    uint32_t canonical[1] = {0u};
    int sign = 0;
    phy_ir_ref value = PHY_IR_NULL;
    if (tensor != NULL) {
        sink((unsigned)phy_component_tensor_canonical_indices(
            tensor, coordinate, canonical, &sign));
        sink((unsigned)phy_component_tensor_set(
            tensor, coordinate, phy_ir_integer(ir, 3)));
        sink((unsigned)phy_component_tensor_get(
            tensor, coordinate, &value));
    }
    sink((unsigned)sign);
    sink((unsigned)value);

    phy_bridge_limits bridge_limits;
    phy_bridge_limits_defaults(&bridge_limits);
    phy_component_binding *binding = NULL;
    sink((unsigned)phy_component_binding_create(
        abstract, &bridge_limits, &binding));
    if (binding != NULL && basis != NULL) {
        sink((unsigned)phy_component_binding_add_basis(binding, basis));
    }
    if (binding != NULL && tensor != NULL) {
        sink((unsigned)phy_component_binding_add_tensor(binding, tensor));
    }
    sink((unsigned)phy_component_binding_basis_count(binding));
    sink((unsigned)phy_component_binding_tensor_count(binding));
    sink(phy_component_binding_basis(binding, space) != NULL ? 1u : 0u);
    sink(phy_component_binding_tensor(binding, head) != NULL ? 1u : 0u);

    phy_abstract_index index = {0};
    phy_tensor_monomial *monomial = NULL;
    if (space != NULL && head != NULL) {
        sink((unsigned)phy_abstract_index_make(
            space, "a", PHY_IR_INDEX_LOWER, &index));
        const phy_abstract_factor factor = {head, &index, 1u};
        sink((unsigned)phy_tensor_monomial_create(
            abstract, phy_ir_integer(ir, 1), &factor, 1u,
            &monomial));
    }
    size_t free_count = 0u;
    phy_abstract_index_use free_use = {0};
    if (monomial != NULL) {
        sink((unsigned)phy_component_value_free_slots(
            monomial, &free_use, 1u, &free_count));
        phy_bridge_stats stats = {0};
        sink((unsigned)phy_component_value_monomial(
            binding, monomial, coordinate, 1u, &value, &stats));
        sink((unsigned)stats.bytes_used);
    }

    phy_tensor_monomial_destroy(monomial);
    phy_component_binding_destroy(binding);
    phy_component_tensor_destroy(tensor);
    phy_component_basis_destroy(basis);
    phy_abstract_context_destroy(abstract);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
    return (int)(probe_sink & 1u);
}
