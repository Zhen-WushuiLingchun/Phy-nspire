/*
 * Device link probe for the component tensor core. Not part of the
 * application.
 *
 * Same gap as tests/device/ir_link_probe.c closes for the IR, and for the
 * same reason: the shipped binary does not call the tensor core yet, the
 * device build uses --gc-sections, so every phy_chart_* and phy_tensor_*
 * symbol is discarded before it reaches dist/phy-nspire.tns. The ordinary ARM
 * build therefore proves only that this code *compiles*. A call to a libc
 * function Ndless newlib does not provide, or a relocation the ldscript
 * cannot satisfy, would stay hidden until the curvature pipeline wired it up.
 *
 * This translation unit references every public entry point, so the linker
 * must keep and resolve all of them. tools/tensor-link-check.sh builds it,
 * derives the expected symbol set from include/phy/tensor.h, verifies they
 * survived, verifies no floating-point formatter was dragged in, and packages
 * the result to a real .tns.
 *
 * Kept out of the Makefile's SOURCES on purpose: `make` must not link this,
 * or the size report would measure the probe instead of the product.
 */
#include "phy/platform.h"
#include "phy/tensor.h"

/*
 * Results are funnelled through a volatile sink so the optimizer cannot
 * decide a call is dead and delete the reference the probe exists to make.
 */
volatile unsigned g_phy_tensor_probe_sink;

static void sink(unsigned value)
{
    g_phy_tensor_probe_sink += value;
}

static void probe_chart(phy_ir_context *ctx, phy_chart **out_chart)
{
    static const char *const names[4] = {"t", "r", "theta", "phi"};

    phy_chart *chart = 0;
    sink((unsigned)phy_chart_create(ctx, names, 4u, &chart));
    if (chart == 0) {
        *out_chart = 0;
        return;
    }

    sink(phy_chart_dimension(chart));
    sink(phy_chart_ir(chart) != 0 ? 1u : 0u);
    sink((unsigned)phy_chart_coordinate_symbol(chart, 0u));
    sink((unsigned)phy_chart_coordinate(chart, 1u));
    sink(phy_chart_coordinate_name(chart, 2u) != 0 ? 1u : 0u);
    sink(phy_chart_axis_of(chart, phy_chart_coordinate_symbol(chart, 3u)));

    *out_chart = chart;
}

static void probe_shape(phy_tensor *tensor)
{
    sink(phy_tensor_rank(tensor));
    sink(phy_tensor_dimension(tensor));
    sink((unsigned)phy_tensor_component_count(tensor));
    sink(phy_tensor_chart(tensor) != 0 ? 1u : 0u);
    sink((unsigned)phy_tensor_head(tensor));
    sink(phy_tensor_name(tensor) != 0 ? 1u : 0u);
    sink((unsigned)phy_tensor_valence(tensor, 0u));
    sink((unsigned)phy_tensor_zero(tensor));
}

static void probe_indexing(phy_tensor *tensor)
{
    const unsigned indices[4] = {0u, 1u, 0u, 1u};
    unsigned decoded[4] = {0u, 0u, 0u, 0u};
    size_t flat = 0u;

    sink((unsigned)phy_tensor_flatten(tensor, indices, &flat));
    sink((unsigned)flat);
    sink((unsigned)phy_tensor_unflatten(tensor, flat, decoded));
    sink(decoded[3]);
}

static void probe_symmetries(phy_tensor *tensor)
{
    static const unsigned exchange[4] = {2u, 3u, 0u, 1u};

    sink((unsigned)phy_tensor_declare_permutation(tensor, exchange, 4u, 1));
    sink((unsigned)phy_tensor_declare_slot_symmetry(
        tensor, 0u, 1u, PHY_IR_SYMMETRY_ANTISYMMETRIC));
    sink((unsigned)phy_tensor_declare_riemann_symmetry(tensor));
    sink((unsigned)phy_tensor_symmetry_group_order(tensor));
    sink(phy_tensor_is_identically_zero(tensor) ? 1u : 0u);
}

static void probe_components(phy_ir_context *ctx, phy_tensor *tensor)
{
    const unsigned indices[4] = {0u, 1u, 0u, 1u};
    unsigned representative[4] = {0u, 0u, 0u, 0u};
    phy_tensor_component component = {PHY_IR_NULL, 0};
    int sign = 0;

    sink((unsigned)phy_tensor_canonical(tensor, indices, representative,
                                        &sign));
    sink((unsigned)(sign + 1));
    sink(phy_tensor_is_canonical(tensor, indices) ? 1u : 0u);
    sink((unsigned)phy_tensor_independent_count(tensor));

    sink((unsigned)phy_tensor_set(tensor, indices, phy_ir_integer(ctx, 3)));
    sink((unsigned)phy_tensor_set_flat(tensor, 0u, phy_ir_integer(ctx, 0)));
    sink((unsigned)phy_tensor_get(tensor, indices, &component));
    sink((unsigned)phy_tensor_get_flat(tensor, 0u, &component));
    sink((unsigned)component.ref);
    sink(phy_tensor_is_assigned(tensor, indices) ? 1u : 0u);

    sink((unsigned)phy_tensor_fill_symmetries(tensor));
    sink((unsigned)phy_tensor_check_symmetries(tensor));
    sink((unsigned)phy_tensor_clear_component(tensor, indices));
    phy_tensor_clear(tensor);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        return 1;
    }

    phy_ir_context *ctx = phy_ir_context_create(0);
    if (ctx == 0) {
        phy_platform_shutdown();
        return 1;
    }

    phy_chart *chart = 0;
    probe_chart(ctx, &chart);

    if (chart != 0) {
        static const phy_ir_variance valence[4] = {
            PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
            PHY_IR_INDEX_LOWER};

        phy_tensor *tensor = 0;
        sink((unsigned)phy_tensor_create(chart, "R", 4u, valence, &tensor));
        if (tensor != 0) {
            probe_shape(tensor);
            probe_indexing(tensor);
            probe_symmetries(tensor);
            probe_components(ctx, tensor);
            phy_tensor_destroy(tensor);
        }
        phy_chart_destroy(chart);
    }

    phy_ir_context_destroy(ctx);
    phy_platform_shutdown();
    return (int)(g_phy_tensor_probe_sink & 1u);
}
