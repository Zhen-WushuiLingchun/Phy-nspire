/*
 * Device link probe for the manifold and differential-form layer. Not part of
 * the application.
 *
 * Same gap as tests/device/tensor_link_probe.c closes for the tensor core, and
 * for the same reason: the shipped binary does not call this layer yet, the
 * device build uses --gc-sections, so every phy_manifold_* and phy_form_*
 * symbol is discarded before it reaches dist/phy-nspire.tns. The ordinary ARM
 * build therefore proves only that this code *compiles*.
 *
 * This layer has a sharper reason to be checked than the ones below it. It is
 * the first to call the CAS from a physics module, and the CAS is the layer
 * whose whole design is about not acquiring a floating-point dependency. If a
 * form operation reached one -- through a libm call the ARM toolchain resolves
 * differently, or a soft-float helper -- the host build would never say so.
 *
 * This translation unit references every public entry point, so the linker
 * must keep and resolve all of them. tools/link-check.sh geom builds it,
 * derives the expected symbol set from include/phy/geom.h, verifies they
 * survived, verifies no float formatter, libm call or soft-float helper was
 * dragged in, and packages the result to a real .tns.
 *
 * Kept out of the Makefile's SOURCES on purpose: `make` must not link this, or
 * the size report would measure the probe instead of the product.
 */
#include "phy/cas.h"
#include "phy/geom.h"
#include "phy/platform.h"
#include "phy/tensor.h"

/*
 * Results are funnelled through a volatile sink so the optimizer cannot decide
 * a call is dead and delete the reference the probe exists to make.
 */
volatile unsigned g_phy_geom_probe_sink;

static void sink(unsigned value)
{
    g_phy_geom_probe_sink += value;
}

static void probe_manifold(phy_manifold *manifold, phy_chart *chart)
{
    unsigned index = 0u;

    sink((unsigned)phy_manifold_add_chart(manifold, chart, &index));
    sink(phy_manifold_ir(manifold) != 0 ? 1u : 0u);
    sink((unsigned)phy_manifold_head(manifold));
    sink(phy_manifold_name(manifold) != 0 ? 1u : 0u);
    sink(phy_manifold_dimension(manifold));
    sink((unsigned)phy_manifold_orientation(manifold));
    sink((unsigned)(phy_manifold_signature(manifold, 0u) + 1));
    sink(phy_manifold_negative_count(manifold));
    sink((unsigned)(phy_manifold_metric_sign(manifold) + 1));
    sink(phy_manifold_chart_count(manifold));
    sink(phy_manifold_chart(manifold, 0u) != 0 ? 1u : 0u);
}

static void probe_shape(const phy_form *form)
{
    sink(phy_form_manifold(form) != 0 ? 1u : 0u);
    sink(phy_form_chart(form) != 0 ? 1u : 0u);
    sink(phy_form_chart_index(form));
    sink((unsigned)phy_form_head(form));
    sink(phy_form_name(form) != 0 ? 1u : 0u);
    sink(phy_form_degree(form));
    sink(phy_form_dimension(form));
    sink((unsigned)phy_form_component_count(form));
    sink((unsigned)phy_form_zero(form));
}

static void probe_components(phy_cas *cas, phy_form *form)
{
    unsigned indices[PHY_FORM_MAX_DEGREE] = {0u, 0u, 0u, 0u};
    phy_ir_ref value = PHY_IR_NULL;
    size_t position = 0u;
    int sign = 0;

    sink((unsigned)phy_form_position_indices(form, 0u, indices));
    sink((unsigned)phy_form_position_of(form, indices, &position, &sign));
    sink((unsigned)position + (unsigned)(sign + 1));

    sink((unsigned)phy_form_set_at(cas, form, 0u,
                                   phy_ir_integer(phy_cas_ir(cas), 2)));
    sink((unsigned)phy_form_get_at(form, 0u, &value));
    sink((unsigned)phy_form_set(cas, form, indices, value));
    sink((unsigned)phy_form_get(cas, form, indices, &value));
    sink((unsigned)value);
}

static void probe_algebra(phy_cas *cas, phy_form *form)
{
    phy_form *copy = 0;
    phy_form *sum = 0;
    phy_form *difference = 0;
    phy_form *scaled = 0;
    phy_cas_decision decision = PHY_CAS_UNKNOWN;

    sink((unsigned)phy_form_copy(form, &copy));
    if (copy == 0) {
        return;
    }
    sink((unsigned)phy_form_add(cas, form, copy, &sum));
    sink((unsigned)phy_form_sub(cas, form, copy, &difference));
    sink((unsigned)phy_form_scale(cas, form,
                                  phy_ir_integer(phy_cas_ir(cas), 3), &scaled));
    sink((unsigned)phy_form_is_zero(cas, form, &decision));
    sink((unsigned)phy_form_equivalent(cas, form, copy, &decision));
    sink((unsigned)decision);

    phy_form_destroy(scaled);
    phy_form_destroy(difference);
    phy_form_destroy(sum);
    phy_form_destroy(copy);
}

static void probe_exterior(phy_cas *cas, phy_manifold *manifold,
                           phy_chart *chart, phy_form *form)
{
    static const phy_ir_variance upper[1] = {PHY_IR_INDEX_UPPER};
    const unsigned axis = 0u;

    phy_form *product = 0;
    phy_form *derivative = 0;
    phy_form *contracted = 0;
    phy_form *lie_derivative = 0;
    phy_form *dual = 0;
    phy_form *metric_dual = 0;
    phy_form *volume = 0;
    phy_form *metric_volume = 0;
    phy_tensor *vector = 0;
    phy_tensor *metric = 0;

    sink((unsigned)phy_form_wedge(cas, form, form, &product));
    sink((unsigned)phy_form_exterior_derivative(cas, form, &derivative));
    sink((unsigned)phy_form_hodge(cas, form, &dual));
    sink((unsigned)phy_form_volume(cas, manifold, 0u, &volume));

    {
        static const phy_ir_variance lower[2] = {
            PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
        sink((unsigned)phy_tensor_create(
            chart, "g", 2u, lower, &metric));
        if (metric != 0) {
            const unsigned dimension = phy_chart_dimension(chart);
            for (unsigned row = 0u; row < dimension; ++row) {
                for (unsigned column = 0u; column < dimension; ++column) {
                    const unsigned indices[2] = {row, column};
                    sink((unsigned)phy_tensor_set(
                        metric, indices,
                        phy_ir_integer(
                            phy_cas_ir(cas), row == column ? 1 : 0)));
                }
            }
            sink((unsigned)phy_form_hodge_metric(
                cas, form, metric, &metric_dual));
            sink((unsigned)phy_form_volume_metric(
                cas, manifold, 0u, metric, &metric_volume));
        }
    }

    sink((unsigned)phy_tensor_create(chart, "v", 1u, upper, &vector));
    if (vector != 0) {
        sink((unsigned)phy_tensor_set(vector, &axis,
                                      phy_ir_integer(phy_cas_ir(cas), 1)));
        sink((unsigned)phy_form_interior_product(cas, form, vector,
                                                 &contracted));
        sink((unsigned)phy_form_lie_derivative(cas, form, vector,
                                               &lie_derivative));
        phy_tensor_destroy(vector);
    }

    phy_form_destroy(volume);
    phy_form_destroy(metric_volume);
    phy_form_destroy(dual);
    phy_form_destroy(metric_dual);
    phy_form_destroy(lie_derivative);
    phy_form_destroy(contracted);
    phy_form_destroy(derivative);
    phy_form_destroy(product);
    phy_tensor_destroy(metric);
}

int main(void)
{
    static const char *const names[4] = {"t", "x", "y", "z"};
    static const int8_t signature[4] = {-1, 1, 1, 1};

    if (phy_platform_init() != PHY_OK) {
        return 1;
    }

    phy_ir_context *ctx = phy_ir_context_create(0);
    phy_cas *cas = (ctx == 0) ? 0 : phy_cas_create(ctx, 0);
    if (ctx == 0 || cas == 0) {
        phy_cas_destroy(cas);
        phy_ir_context_destroy(ctx);
        phy_platform_shutdown();
        return 1;
    }

    phy_chart *chart = 0;
    sink((unsigned)phy_chart_create(ctx, names, 4u, &chart));

    phy_manifold *manifold = 0;
    sink((unsigned)phy_manifold_create(ctx, "M", 4u, PHY_ORIENTATION_POSITIVE,
                                       signature, &manifold));

    if (chart != 0 && manifold != 0) {
        probe_manifold(manifold, chart);

        phy_form *form = 0;
        sink((unsigned)phy_form_create(manifold, 0u, "alpha", 1u, &form));
        if (form != 0) {
            probe_shape(form);
            probe_components(cas, form);
            probe_algebra(cas, form);
            probe_exterior(cas, manifold, chart, form);
            phy_form_clear(form);
            phy_form_destroy(form);
        }
    }

    phy_manifold_destroy(manifold);
    phy_chart_destroy(chart);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ctx);
    phy_platform_shutdown();
    return (int)(g_phy_geom_probe_sink & 1u);
}
