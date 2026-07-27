/*
 * Device link-retention probe for include/phy/color.h.
 *
 * Every public Q-6 entry point is touched so --gc-sections cannot hide an
 * unresolved Ndless dependency.  This probe is never linked into the product.
 */
#include "phy/color.h"
#include "phy/platform.h"

volatile unsigned g_phy_color_probe_sink;

static void sink(unsigned value)
{
    g_phy_color_probe_sink += value;
}

static void probe_color(phy_cas *cas, phy_ir_context *ir)
{
    phy_color *color = 0;
    phy_ir_ref n = phy_ir_integer(ir, 3);
    phy_ir_ref a = PHY_IR_NULL;
    phy_ir_ref b = PHY_IR_NULL;
    phy_ir_ref c = PHY_IR_NULL;
    phy_ir_ref value = PHY_IR_NULL;
    phy_ir_ref indices[4];

    sink((unsigned)phy_color_create(cas, n, &color));
    if (color == 0) {
        return;
    }
    sink(phy_color_cas(color) != 0 ? 1u : 0u);
    sink((unsigned)phy_color_n(color));
    sink((unsigned)phy_color_adjoint_space(color));
    sink((unsigned)phy_color_adjoint_dimension(color, &value));
    sink((unsigned)phy_color_index(color, "a", &a));
    sink((unsigned)phy_color_index(color, "b", &b));
    sink((unsigned)phy_color_index(color, "c", &c));
    sink(phy_color_owns_index(color, a) ? 1u : 0u);
    indices[0] = a;
    indices[1] = b;
    indices[2] = c;
    indices[3] = a;

    sink((unsigned)phy_color_delta(color, a, b, &value));
    sink((unsigned)phy_color_structure_constant(color, a, b, c, &value));
    sink((unsigned)phy_color_symmetric_tensor(color, a, b, c, &value));
    sink((unsigned)phy_color_generator(color, b, &value));
    sink((unsigned)phy_color_delta_contract(color, a, b, value, &value));
    sink((unsigned)phy_color_structure_constant_component(
        color, 1u, 2u, 3u, &value));
    sink((unsigned)phy_color_commutator(color, a, b, &value));
    sink((unsigned)phy_color_trace(color, indices, 4u, &value));
    sink((unsigned)phy_color_casimir_fundamental(color, &value));
    sink((unsigned)phy_color_casimir_adjoint(color, &value));
    sink((unsigned)phy_color_cf_symbol(color));
    sink((unsigned)phy_color_ca_symbol(color));
    sink((unsigned)phy_color_expand_casimirs(color, value, &value));
    sink((unsigned)phy_color_fundamental_casimir(color, &value));
    sink((unsigned)phy_color_adjoint_casimir(color, a, b, &value));
    phy_color_destroy(color);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        return 1;
    }
    phy_ir_context *ir = phy_ir_context_create(0);
    phy_cas *cas = ir != 0 ? phy_cas_create(ir, 0) : 0;
    if (cas != 0) {
        probe_color(cas, ir);
    }
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
    return (int)(g_phy_color_probe_sink & 1u);
}
