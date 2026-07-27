#include <stdio.h>

#include "phy/gr.h"
#include "phy/platform.h"
#include "phy_test.h"

static phy_ir_ref read_expr(phy_ir_context *ir, const char *text)
{
    phy_ir_ref expression = PHY_IR_NULL;
    size_t offset = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_read(ir, text, &expression, &offset), PHY_OK);
    return expression;
}

static void expect_equivalent(phy_cas *cas, phy_ir_ref actual,
                              phy_ir_ref expected)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(cas, actual, expected, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static phy_ir_ref tensor_component(phy_cas *cas, const phy_tensor *tensor,
                                   const unsigned *indices)
{
    phy_ir_ref result = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_component_expression(cas, tensor, indices, &result),
        PHY_OK);
    return result;
}

static phy_tensor *make_metric(phy_chart *chart, phy_ir_context *ir,
                               unsigned dimension,
                               const char *const *entries)
{
    static const phy_ir_variance lower[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_tensor *metric = NULL;
    if (phy_tensor_create(chart, "g", 2u, lower, &metric) != PHY_OK) {
        return NULL;
    }
    for (unsigned row = 0u; row < dimension; ++row) {
        for (unsigned column = 0u; column < dimension; ++column) {
            const unsigned indices[2] = {row, column};
            const phy_ir_ref value =
                read_expr(ir, entries[row * dimension + column]);
            if (value == PHY_IR_NULL ||
                phy_tensor_set(metric, indices, value) != PHY_OK) {
                phy_tensor_destroy(metric);
                return NULL;
            }
        }
    }
    return metric;
}

static void expect_tensor_zero(phy_cas *cas, const phy_tensor *tensor)
{
    unsigned indices[PHY_TENSOR_MAX_RANK] = {0u};
    const size_t count = phy_tensor_component_count(tensor);
    for (size_t flat = 0u; flat < count; ++flat) {
        PHY_CHECK_EQ_INT(
            phy_tensor_unflatten(tensor, flat, indices), PHY_OK);
        const phy_ir_ref value = tensor_component(cas, tensor, indices);
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        PHY_CHECK_EQ_INT(
            phy_cas_is_zero(cas, value, &decision), PHY_OK);
        PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
    }
}

static void test_minkowski_cartesian_is_flat(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);
    static const char *const coordinates[4] = {"t", "x", "y", "z"};
    phy_chart *chart = NULL;
    PHY_CHECK_EQ_INT(
        phy_chart_create(ir, coordinates, 4u, &chart), PHY_OK);
    static const char *const entries[16] = {
        "-1", "0", "0", "0",
        "0", "1", "0", "0",
        "0", "0", "1", "0",
        "0", "0", "0", "1",
    };
    phy_tensor *metric = make_metric(chart, ir, 4u, entries);
    PHY_CHECK(metric != NULL);

    phy_gr_result *result = NULL;
    PHY_CHECK_EQ_INT(phy_gr_compute(cas, metric, &result), PHY_OK);
    PHY_CHECK(result != NULL);
    expect_tensor_zero(cas, phy_gr_christoffel(result));
    expect_tensor_zero(cas, phy_gr_riemann_mixed(result));
    expect_tensor_zero(cas, phy_gr_riemann_covariant(result));
    expect_tensor_zero(cas, phy_gr_ricci(result));
    expect_equivalent(cas, phy_gr_scalar_curvature(result),
                      phy_ir_integer(ir, 0));
    expect_tensor_zero(cas, phy_gr_einstein(result));
    const phy_tensor *weyl = NULL;
    PHY_CHECK_EQ_INT(phy_gr_weyl(cas, result, &weyl), PHY_OK);
    PHY_CHECK(weyl != NULL);
    expect_tensor_zero(cas, weyl);
    const phy_tensor *weyl_again = NULL;
    PHY_CHECK_EQ_INT(phy_gr_weyl(cas, result, &weyl_again), PHY_OK);
    PHY_CHECK(weyl_again == weyl);
    phy_ir_ref weyl_square = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_gr_weyl_squared(cas, result, &weyl_square), PHY_OK);
    expect_equivalent(cas, weyl_square, phy_ir_integer(ir, 0));

    phy_gr_result_destroy(result);
    phy_tensor_destroy(metric);
    phy_chart_destroy(chart);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

static void test_round_two_sphere_curvature(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);
    static const char *const coordinates[2] = {"theta", "phi"};
    phy_chart *chart = NULL;
    PHY_CHECK_EQ_INT(
        phy_chart_create(ir, coordinates, 2u, &chart), PHY_OK);
    static const char *const entries[4] = {
        "(^ a 2)",
        "0",
        "0",
        "(* (^ a 2) (^ (fn sin theta) 2))",
    };
    phy_tensor *metric = make_metric(chart, ir, 2u, entries);
    PHY_CHECK(metric != NULL);

    phy_gr_result *result = NULL;
    PHY_CHECK_EQ_INT(phy_gr_compute(cas, metric, &result), PHY_OK);
    PHY_CHECK(result != NULL);

    const unsigned gamma_theta_phiphi[3] = {0u, 1u, 1u};
    expect_equivalent(
        cas,
        tensor_component(cas, phy_gr_christoffel(result),
                         gamma_theta_phiphi),
        read_expr(ir, "(* -1 (fn cos theta) (fn sin theta))"));
    const unsigned gamma_phi_thetaphi[3] = {1u, 0u, 1u};
    expect_equivalent(
        cas,
        tensor_component(cas, phy_gr_christoffel(result),
                         gamma_phi_thetaphi),
        read_expr(
            ir, "(* (fn cos theta) (^ (fn sin theta) -1))"));

    const unsigned riemann_theta_phi_theta_phi[4] = {0u, 1u, 0u, 1u};
    expect_equivalent(
        cas,
        tensor_component(cas, phy_gr_riemann_covariant(result),
                         riemann_theta_phi_theta_phi),
        read_expr(ir, "(* (^ a 2) (^ (fn sin theta) 2))"));
    expect_equivalent(
        cas, phy_gr_scalar_curvature(result),
        read_expr(ir, "(* 2 (^ a -2))"));
    expect_tensor_zero(cas, phy_gr_einstein(result));
    const phy_tensor *weyl = NULL;
    PHY_CHECK_EQ_INT(phy_gr_weyl(cas, result, &weyl), PHY_OK);
    expect_tensor_zero(cas, weyl);
    phy_ir_ref weyl_square = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_gr_weyl_squared(cas, result, &weyl_square), PHY_OK);
    expect_equivalent(cas, weyl_square, phy_ir_integer(ir, 0));

    phy_gr_result_destroy(result);
    phy_tensor_destroy(metric);
    phy_chart_destroy(chart);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

static void test_three_dimensional_weyl_cancellation(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);
    static const char *const coordinates[3] = {"theta", "phi", "z"};
    phy_chart *chart = NULL;
    PHY_CHECK_EQ_INT(
        phy_chart_create(ir, coordinates, 3u, &chart), PHY_OK);
    /* S^2(a) x R has nonzero Ricci curvature, but every 3D Weyl tensor is 0. */
    static const char *const entries[9] = {
        "(^ a 2)", "0", "0",
        "0", "(* (^ a 2) (^ (fn sin theta) 2))", "0",
        "0", "0", "1",
    };
    phy_tensor *metric = make_metric(chart, ir, 3u, entries);
    PHY_CHECK(metric != NULL);
    phy_gr_result *result = NULL;
    PHY_CHECK_EQ_INT(phy_gr_compute(cas, metric, &result), PHY_OK);

    const phy_tensor *weyl = NULL;
    PHY_CHECK_EQ_INT(phy_gr_weyl(cas, result, &weyl), PHY_OK);
    expect_tensor_zero(cas, weyl);
    phy_ir_ref square = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_gr_weyl_squared(cas, result, &square), PHY_OK);
    expect_equivalent(cas, square, phy_ir_integer(ir, 0));
    phy_ir_ref square_again = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_gr_weyl_squared(cas, result, &square_again), PHY_OK);
    PHY_CHECK(square_again == square);

    phy_gr_result_destroy(result);
    phy_tensor_destroy(metric);
    phy_chart_destroy(chart);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

static void test_polar_geodesic_acceleration(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);
    static const char *const coordinates[2] = {"r", "theta"};
    phy_chart *chart = NULL;
    PHY_CHECK_EQ_INT(
        phy_chart_create(ir, coordinates, 2u, &chart), PHY_OK);
    static const char *const entries[4] = {
        "1", "0", "0", "(^ r 2)"};
    phy_tensor *metric = make_metric(chart, ir, 2u, entries);
    PHY_CHECK(metric != NULL);
    phy_gr_result *result = NULL;
    PHY_CHECK_EQ_INT(phy_gr_compute(cas, metric, &result), PHY_OK);

    static const phy_ir_variance upper[1] = {PHY_IR_INDEX_UPPER};
    phy_tensor *velocity = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_create(chart, "v", 1u, upper, &velocity), PHY_OK);
    {
        const unsigned radial = 0u;
        const unsigned angular = 1u;
        PHY_CHECK_EQ_INT(
            phy_tensor_set(
                velocity, &radial, read_expr(ir, "vr")),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_tensor_set(
                velocity, &angular, read_expr(ir, "vtheta")),
            PHY_OK);
    }

    phy_tensor *acceleration = NULL;
    PHY_CHECK_EQ_INT(
        phy_gr_geodesic_acceleration(
            cas, result, velocity, "GeodesicAcceleration", &acceleration),
        PHY_OK);
    const unsigned radial = 0u;
    const unsigned angular = 1u;
    expect_equivalent(
        cas, tensor_component(cas, acceleration, &radial),
        read_expr(ir, "(* r (^ vtheta 2))"));
    expect_equivalent(
        cas, tensor_component(cas, acceleration, &angular),
        read_expr(ir, "(* -2 (^ r -1) vr vtheta)"));

    static const phy_ir_variance lower[1] = {PHY_IR_INDEX_LOWER};
    phy_tensor *covector = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_create(chart, "w", 1u, lower, &covector), PHY_OK);
    phy_tensor *sentinel = NULL;
    PHY_CHECK_EQ_INT(
        phy_gr_geodesic_acceleration(
            cas, result, covector, "bad", &sentinel),
        PHY_ERR_TYPE);
    PHY_CHECK(sentinel == NULL);

    phy_tensor_destroy(covector);
    phy_tensor_destroy(acceleration);
    phy_tensor_destroy(velocity);
    phy_gr_result_destroy(result);
    phy_tensor_destroy(metric);
    phy_chart_destroy(chart);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

static void test_gr_rejects_non_metric_shape(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);
    static const char *const coordinates[2] = {"x", "y"};
    phy_chart *chart = NULL;
    PHY_CHECK_EQ_INT(
        phy_chart_create(ir, coordinates, 2u, &chart), PHY_OK);
    static const phy_ir_variance upper[1] = {PHY_IR_INDEX_UPPER};
    phy_tensor *vector = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_create(chart, "v", 1u, upper, &vector), PHY_OK);
    phy_gr_result *result = NULL;
    PHY_CHECK_EQ_INT(phy_gr_compute(cas, vector, &result), PHY_ERR_TYPE);
    PHY_CHECK(result == NULL);

    phy_tensor_destroy(vector);
    phy_chart_destroy(chart);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }
    PHY_TEST_CASE(test_minkowski_cartesian_is_flat);
    PHY_TEST_CASE(test_round_two_sphere_curvature);
    PHY_TEST_CASE(test_three_dimensional_weyl_cancellation);
    PHY_TEST_CASE(test_polar_geodesic_acceleration);
    PHY_TEST_CASE(test_gr_rejects_non_metric_shape);
    const int result = PHY_TEST_REPORT("test_gr");
    phy_platform_shutdown();
    return result;
}
