#include <stdio.h>

#include "phy/platform.h"
#include "phy/yang_mills.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_chart *chart;
    phy_manifold *manifold;
    phy_lie_group *group;
} fixture;

static phy_ir_ref read_expr(phy_ir_context *ir, const char *text)
{
    phy_ir_ref expression = PHY_IR_NULL;
    size_t offset = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_read(ir, text, &expression, &offset), PHY_OK);
    return expression;
}

static fixture open_fixture(phy_lie_group_kind kind)
{
    fixture f = {0};
    f.ir = phy_ir_context_create(NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    static const char *const coordinates[3] = {"x", "y", "z"};
    PHY_CHECK_EQ_INT(
        phy_chart_create(f.ir, coordinates, 3u, &f.chart), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_manifold_create(
            f.ir, "M3", 3u, PHY_ORIENTATION_POSITIVE, NULL,
            &f.manifold),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_manifold_add_chart(f.manifold, f.chart, NULL), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_group_builtin(f.cas, kind, &f.group), PHY_OK);
    return f;
}

static void close_fixture(fixture *f)
{
    PHY_CHECK_EQ_INT(phy_cas_validate(f->cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(f->ir), PHY_OK);
    phy_lie_group_destroy(f->group);
    phy_manifold_destroy(f->manifold);
    phy_chart_destroy(f->chart);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static void put(fixture *f, phy_lie_form *form, unsigned color,
                size_t position, const char *expression)
{
    PHY_CHECK_EQ_INT(
        phy_form_set_at(
            f->cas, phy_lie_form_component_mut(form, color), position,
            read_expr(f->ir, expression)),
        PHY_OK);
}

static phy_ir_ref get(const phy_lie_form *form, unsigned color,
                      size_t position)
{
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_form_get_at(
            phy_lie_form_component(form, color), position, &value),
        PHY_OK);
    return value;
}

static void expect_equivalent(fixture *f, phy_ir_ref actual,
                              const char *expected)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f->cas, actual, read_expr(f->ir, expected), &decision),
        PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static phy_tensor *identity_metric(fixture *f)
{
    static const phy_ir_variance lower[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_tensor *metric = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_create(f->chart, "g", 2u, lower, &metric), PHY_OK);
    for (unsigned row = 0u; row < 3u; ++row) {
        for (unsigned column = 0u; column < 3u; ++column) {
            const unsigned indices[2] = {row, column};
            PHY_CHECK_EQ_INT(
                phy_tensor_set(
                    metric, indices,
                    phy_ir_integer(f->ir, row == column ? 1 : 0)),
                PHY_OK);
        }
    }
    return metric;
}

static void test_u1_curvature_bianchi_and_density(void)
{
    fixture f = open_fixture(PHY_LIE_GROUP_U1);
    const phy_lie_algebra *algebra = phy_lie_group_algebra(f.group);
    phy_lie_form *connection = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_form_create(
            algebra, f.manifold, 0u, 1u, &connection),
        PHY_OK);
    /* A = x dy, hence F = dx wedge dy. */
    put(&f, connection, 0u, 1u, "x");
    const phy_ir_ref coupling = read_expr(f.ir, "g");

    phy_lie_form *curvature = NULL;
    PHY_CHECK_EQ_INT(
        phy_yang_mills_field_strength(
            f.cas, connection, coupling, &curvature),
        PHY_OK);
    expect_equivalent(&f, get(curvature, 0u, 0u), "1");
    expect_equivalent(&f, get(curvature, 0u, 1u), "0");
    expect_equivalent(&f, get(curvature, 0u, 2u), "0");

    phy_lie_form *residual = NULL;
    PHY_CHECK_EQ_INT(
        phy_yang_mills_bianchi(
            f.cas, connection, coupling, &residual),
        PHY_OK);
    phy_cas_decision zero = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_lie_form_is_zero(f.cas, residual, &zero), PHY_OK);
    PHY_CHECK_EQ_INT(zero, PHY_CAS_ZERO);

    phy_tensor *metric = identity_metric(&f);
    const phy_ir_ref bilinear[1] = {read_expr(f.ir, "1")};
    phy_form *lagrangian = NULL;
    PHY_CHECK_EQ_INT(
        phy_yang_mills_lagrangian(
            f.cas, curvature, metric, bilinear, &lagrangian),
        PHY_OK);
    phy_ir_ref density = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_form_get_at(lagrangian, 0u, &density), PHY_OK);
    expect_equivalent(&f, density, "(rat -1 2)");

    phy_form_destroy(lagrangian);
    phy_tensor_destroy(metric);
    phy_lie_form_destroy(residual);
    phy_lie_form_destroy(curvature);
    phy_lie_form_destroy(connection);
    close_fixture(&f);
}

static void test_su2_nonlinear_curvature(void)
{
    fixture f = open_fixture(PHY_LIE_GROUP_SU2);
    const phy_lie_algebra *algebra = phy_lie_group_algebra(f.group);
    phy_lie_form *connection = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_form_create(
            algebra, f.manifold, 0u, 1u, &connection),
        PHY_OK);
    /* A^1=dx, A^2=dy, A^3=dz in the built-in epsilon basis. */
    put(&f, connection, 0u, 0u, "1");
    put(&f, connection, 1u, 1u, "1");
    put(&f, connection, 2u, 2u, "1");
    const phy_ir_ref coupling = read_expr(f.ir, "g");

    phy_lie_form *curvature = NULL;
    PHY_CHECK_EQ_INT(
        phy_yang_mills_field_strength(
            f.cas, connection, coupling, &curvature),
        PHY_OK);
    /* positions of a 2-form in 3D: xy, xz, yz. */
    expect_equivalent(&f, get(curvature, 0u, 0u), "0");
    expect_equivalent(&f, get(curvature, 0u, 1u), "0");
    expect_equivalent(&f, get(curvature, 0u, 2u), "g");
    expect_equivalent(&f, get(curvature, 1u, 0u), "0");
    expect_equivalent(&f, get(curvature, 1u, 1u), "(* -1 g)");
    expect_equivalent(&f, get(curvature, 1u, 2u), "0");
    expect_equivalent(&f, get(curvature, 2u, 0u), "g");
    expect_equivalent(&f, get(curvature, 2u, 1u), "0");
    expect_equivalent(&f, get(curvature, 2u, 2u), "0");

    phy_lie_form *residual = NULL;
    PHY_CHECK_EQ_INT(
        phy_yang_mills_bianchi(
            f.cas, connection, coupling, &residual),
        PHY_OK);
    phy_cas_decision zero = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_lie_form_is_zero(f.cas, residual, &zero), PHY_OK);
    PHY_CHECK_EQ_INT(zero, PHY_CAS_ZERO);

    phy_lie_form_destroy(residual);
    phy_lie_form_destroy(curvature);
    phy_lie_form_destroy(connection);
    close_fixture(&f);
}

static void test_gauge_variations_and_type_checks(void)
{
    fixture f = open_fixture(PHY_LIE_GROUP_SU2);
    const phy_lie_algebra *algebra = phy_lie_group_algebra(f.group);
    phy_lie_form *connection = NULL;
    phy_lie_form *parameter = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_form_create(
            algebra, f.manifold, 0u, 1u, &connection),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_form_create(
            algebra, f.manifold, 0u, 0u, &parameter),
        PHY_OK);
    put(&f, connection, 0u, 0u, "1");
    put(&f, parameter, 1u, 0u, "a");

    phy_lie_form *variation = NULL;
    PHY_CHECK_EQ_INT(
        phy_yang_mills_gauge_variation_connection(
            f.cas, connection, read_expr(f.ir, "g"), parameter,
            &variation),
        PHY_OK);
    expect_equivalent(&f, get(variation, 2u, 0u), "(* a g)");

    phy_lie_form *sentinel = NULL;
    PHY_CHECK_EQ_INT(
        phy_yang_mills_gauge_variation_connection(
            f.cas, connection, read_expr(f.ir, "g"), connection,
            &sentinel),
        PHY_ERR_TYPE);
    PHY_CHECK(sentinel == NULL);

    phy_lie_form_destroy(variation);
    phy_lie_form_destroy(parameter);
    phy_lie_form_destroy(connection);
    close_fixture(&f);
}

static void test_su2_bianchi_uses_symbolic_cancellation(void)
{
    fixture f = open_fixture(PHY_LIE_GROUP_SU2);
    const phy_lie_algebra *algebra = phy_lie_group_algebra(f.group);
    phy_lie_form *connection = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_form_create(
            algebra, f.manifold, 0u, 1u, &connection),
        PHY_OK);

    static const char *const coefficients[9] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "10",
    };
    for (unsigned color = 0u; color < 3u; ++color) {
        for (size_t position = 0u; position < 3u; ++position) {
            put(&f, connection, color, position,
                coefficients[(size_t)color * 3u + position]);
        }
    }

    phy_lie_form *residual = NULL;
    PHY_CHECK_EQ_INT(
        phy_yang_mills_bianchi(
            f.cas, connection, read_expr(f.ir, "g"), &residual),
        PHY_OK);
    phy_cas_decision zero = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_lie_form_is_zero(f.cas, residual, &zero), PHY_OK);
    PHY_CHECK_EQ_INT(zero, PHY_CAS_ZERO);

    phy_lie_form_destroy(residual);
    phy_lie_form_destroy(connection);
    close_fixture(&f);
}

static void test_infinitesimal_curvature_covariance(void)
{
    fixture f = open_fixture(PHY_LIE_GROUP_SU2);
    const phy_lie_algebra *algebra = phy_lie_group_algebra(f.group);
    phy_lie_form *connection = NULL;
    phy_lie_form *parameter = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_form_create(
            algebra, f.manifold, 0u, 1u, &connection),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_form_create(
            algebra, f.manifold, 0u, 0u, &parameter),
        PHY_OK);
    /* A^1=dy and alpha^2=x force d(D alpha) to cancel [A,D alpha]. */
    put(&f, connection, 0u, 1u, "1");
    put(&f, parameter, 1u, 0u, "x");
    const phy_ir_ref coupling = read_expr(f.ir, "g");

    phy_lie_form *curvature = NULL;
    phy_lie_form *direct = NULL;
    phy_lie_form *delta_connection = NULL;
    phy_lie_form *derivative = NULL;
    phy_lie_form *left_bracket = NULL;
    phy_lie_form *right_bracket = NULL;
    phy_lie_form *bracket_sum = NULL;
    phy_lie_form *interaction = NULL;
    phy_lie_form *linearized = NULL;

    PHY_CHECK_EQ_INT(
        phy_yang_mills_field_strength(
            f.cas, connection, coupling, &curvature),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_yang_mills_gauge_variation_curvature(
            f.cas, curvature, coupling, parameter, &direct),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_yang_mills_gauge_variation_connection(
            f.cas, connection, coupling, parameter, &delta_connection),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_form_exterior_derivative(
            f.cas, delta_connection, &derivative),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_form_bracket_wedge(
            f.cas, delta_connection, connection, &left_bracket),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_form_bracket_wedge(
            f.cas, connection, delta_connection, &right_bracket),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_form_add(
            f.cas, left_bracket, right_bracket, &bracket_sum),
        PHY_OK);
    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref half_coupling = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 1, 2, &half), PHY_OK);
    const phy_ir_ref factors[2] = {half, coupling};
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, factors, 2u, &half_coupling), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_form_scale(
            f.cas, bracket_sum, half_coupling, &interaction),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_form_add(
            f.cas, derivative, interaction, &linearized),
        PHY_OK);

    phy_cas_decision equivalent = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_lie_form_equivalent(
            f.cas, direct, linearized, &equivalent),
        PHY_OK);
    PHY_CHECK_EQ_INT(equivalent, PHY_CAS_ZERO);

    phy_lie_form_destroy(linearized);
    phy_lie_form_destroy(interaction);
    phy_lie_form_destroy(bracket_sum);
    phy_lie_form_destroy(right_bracket);
    phy_lie_form_destroy(left_bracket);
    phy_lie_form_destroy(derivative);
    phy_lie_form_destroy(delta_connection);
    phy_lie_form_destroy(direct);
    phy_lie_form_destroy(curvature);
    phy_lie_form_destroy(parameter);
    phy_lie_form_destroy(connection);
    close_fixture(&f);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }
    PHY_TEST_CASE(test_u1_curvature_bianchi_and_density);
    PHY_TEST_CASE(test_su2_nonlinear_curvature);
    PHY_TEST_CASE(test_gauge_variations_and_type_checks);
    PHY_TEST_CASE(test_su2_bianchi_uses_symbolic_cancellation);
    PHY_TEST_CASE(test_infinitesimal_curvature_covariance);
    const int result = PHY_TEST_REPORT("test_yang_mills");
    phy_platform_shutdown();
    return result;
}
