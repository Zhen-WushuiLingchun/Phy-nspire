/*
 * Device link-retention probe for include/phy/yang_mills.h.
 * It is never part of the product binary.
 */
#include "phy/platform.h"
#include "phy/yang_mills.h"

volatile unsigned g_phy_yang_mills_probe_sink;

static void sink(unsigned value)
{
    g_phy_yang_mills_probe_sink += value;
}

static void probe_shape(phy_lie_form *form)
{
    sink(phy_lie_form_algebra(form) != 0 ? 1u : 0u);
    sink(phy_lie_form_manifold(form) != 0 ? 1u : 0u);
    sink(phy_lie_form_chart_index(form));
    sink(phy_lie_form_degree(form));
    sink(phy_lie_form_algebra_dimension(form));
    sink(phy_lie_form_component(form, 0u) != 0 ? 1u : 0u);
    sink(phy_lie_form_component_mut(form, 0u) != 0 ? 1u : 0u);
}

static void probe_operations(phy_cas *cas, phy_lie_form *connection,
                             phy_lie_form *parameter, phy_tensor *metric)
{
    const phy_ir_ref coupling =
        phy_ir_symbol_ref(phy_cas_ir(cas),
                          phy_ir_intern(phy_cas_ir(cas), "g"));
    phy_lie_form *copy = 0;
    phy_lie_form *sum = 0;
    phy_lie_form *scaled = 0;
    phy_lie_form *bracket = 0;
    phy_lie_form *derivative = 0;
    phy_lie_form *covariant = 0;
    phy_lie_form *curvature = 0;
    phy_lie_form *delta_a = 0;
    phy_lie_form *delta_f = 0;
    phy_lie_form *bianchi = 0;
    phy_form *lagrangian = 0;
    phy_cas_decision decision = PHY_CAS_UNKNOWN;

    sink((unsigned)phy_lie_form_copy(connection, &copy));
    sink((unsigned)phy_lie_form_add(cas, connection, copy, &sum));
    sink((unsigned)phy_lie_form_scale(
        cas, connection, coupling, &scaled));
    sink((unsigned)phy_lie_form_is_zero(cas, connection, &decision));
    sink((unsigned)phy_lie_form_equivalent(
        cas, connection, copy, &decision));
    sink((unsigned)phy_lie_form_bracket_wedge(
        cas, connection, connection, &bracket));
    sink((unsigned)phy_lie_form_exterior_derivative(
        cas, connection, &derivative));
    sink((unsigned)phy_yang_mills_covariant_derivative(
        cas, connection, coupling, parameter, &covariant));
    sink((unsigned)phy_yang_mills_field_strength(
        cas, connection, coupling, &curvature));
    sink((unsigned)phy_yang_mills_gauge_variation_connection(
        cas, connection, coupling, parameter, &delta_a));
    if (curvature != 0) {
        sink((unsigned)phy_yang_mills_gauge_variation_curvature(
            cas, curvature, coupling, parameter, &delta_f));
        sink((unsigned)phy_yang_mills_lagrangian(
            cas, curvature, metric, 0, &lagrangian));
    }
    sink((unsigned)phy_yang_mills_bianchi(
        cas, connection, coupling, &bianchi));
    sink((unsigned)decision);

    phy_form_destroy(lagrangian);
    phy_lie_form_destroy(bianchi);
    phy_lie_form_destroy(delta_f);
    phy_lie_form_destroy(delta_a);
    phy_lie_form_destroy(curvature);
    phy_lie_form_destroy(covariant);
    phy_lie_form_destroy(derivative);
    phy_lie_form_destroy(bracket);
    phy_lie_form_destroy(scaled);
    phy_lie_form_destroy(sum);
    phy_lie_form_destroy(copy);
}

int main(void)
{
    static const char *const coordinates[3] = {"x", "y", "z"};
    static const phy_ir_variance lower[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    if (phy_platform_init() != PHY_OK) {
        return 1;
    }
    phy_ir_context *ir = phy_ir_context_create(0);
    phy_cas *cas = ir != 0 ? phy_cas_create(ir, 0) : 0;
    phy_chart *chart = 0;
    phy_manifold *manifold = 0;
    phy_lie_group *group = 0;
    phy_tensor *metric = 0;
    phy_lie_form *connection = 0;
    phy_lie_form *parameter = 0;

    if (cas != 0) {
        sink((unsigned)phy_chart_create(
            ir, coordinates, 3u, &chart));
    }
    if (chart != 0) {
        sink((unsigned)phy_manifold_create(
            ir, "M", 3u, PHY_ORIENTATION_POSITIVE, 0, &manifold));
    }
    if (manifold != 0) {
        sink((unsigned)phy_manifold_add_chart(manifold, chart, 0));
    }
    if (cas != 0) {
        sink((unsigned)phy_lie_group_builtin(
            cas, PHY_LIE_GROUP_SU2, &group));
    }
    if (group != 0 && manifold != 0) {
        const phy_lie_algebra *algebra = phy_lie_group_algebra(group);
        sink((unsigned)phy_lie_form_create(
            algebra, manifold, 0u, 1u, &connection));
        sink((unsigned)phy_lie_form_create(
            algebra, manifold, 0u, 0u, &parameter));
        sink((unsigned)phy_tensor_create(
            chart, "g", 2u, lower, &metric));
    }
    if (metric != 0) {
        for (unsigned row = 0u; row < 3u; ++row) {
            for (unsigned column = 0u; column < 3u; ++column) {
                const unsigned indices[2] = {row, column};
                sink((unsigned)phy_tensor_set(
                    metric, indices,
                    phy_ir_integer(ir, row == column ? 1 : 0)));
            }
        }
    }
    if (connection != 0 && parameter != 0 && metric != 0) {
        probe_shape(connection);
        probe_operations(cas, connection, parameter, metric);
    }

    phy_lie_form_destroy(parameter);
    phy_lie_form_destroy(connection);
    phy_tensor_destroy(metric);
    phy_lie_group_destroy(group);
    phy_manifold_destroy(manifold);
    phy_chart_destroy(chart);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
    return (int)(g_phy_yang_mills_probe_sink & 1u);
}
