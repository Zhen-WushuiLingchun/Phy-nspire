/*
 * Device-only retention probe for include/phy/qft_bridge.h.
 */
#include "phy/qft_bridge.h"
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
    phy_qft_component_view *view = NULL;
    if (cas != NULL) {
        sink((unsigned)phy_abstract_context_create(
            cas, NULL, &abstract));
    }

    phy_qft_bridge_limits limits;
    phy_qft_bridge_limits_defaults(&limits);
    sink((unsigned)limits.max_bytes);
    sink(phy_qft_space_name(PHY_QFT_SPACE_LORENTZ) != NULL ? 1u : 0u);
    sink(phy_qft_quantity_name(PHY_QFT_SUN_F) != NULL ? 1u : 0u);
    if (abstract != NULL) {
        sink((unsigned)phy_qft_component_view_create(
            cas, abstract, phy_ir_integer(ir, 3), &limits, &view));
    }
    sink((unsigned)phy_qft_component_view_n(view));
    sink(phy_qft_component_view_space(
             view, PHY_QFT_SPACE_LORENTZ) != NULL
             ? 1u
             : 0u);
    sink(phy_qft_component_view_head(view, PHY_QFT_SUN_F) != NULL ? 1u
                                                                 : 0u);
    sink(phy_qft_component_view_has_basis(
             view, PHY_QFT_SPACE_COLOR_ADJOINT)
             ? 1u
             : 0u);
    sink(phy_qft_component_view_basis(
             view, PHY_QFT_SPACE_COLOR_ADJOINT) != NULL
             ? 1u
             : 0u);
    sink(phy_qft_component_view_holds(view, PHY_QFT_SUN_F) ? 1u : 0u);
    sink(phy_qft_component_view_tensor(view, PHY_QFT_SUN_F) != NULL ? 1u
                                                                   : 0u);

    phy_component_binding *binding = NULL;
    if (abstract != NULL) {
        sink((unsigned)phy_component_binding_create(
            abstract, NULL, &binding));
    }
    if (view != NULL && binding != NULL) {
        sink((unsigned)phy_qft_component_view_bind(view, binding));
    }
    phy_component_binding_destroy(binding);
    phy_qft_component_view_destroy(view);
    phy_abstract_context_destroy(abstract);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
    return (int)(probe_sink & 1u);
}
