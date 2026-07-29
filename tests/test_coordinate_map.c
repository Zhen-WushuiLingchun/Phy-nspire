#include "phy/abstract_tensor.h"
#include "phy/cas.h"
#include "phy/component_tensor.h"
#include "phy/ir.h"
#include "phy/map.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_abstract_context *abstract;
    phy_index_space *space;
    phy_component_basis *xy;
    phy_component_basis *uv;
    phy_ir_ref x;
    phy_ir_ref y;
    phy_ir_ref u;
    phy_ir_ref v;
} fixture;

static fixture fixture_open(void)
{
    fixture f = {0};
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(f.cas, NULL, &f.abstract), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 2),
            PHY_METRIC_NONE, &f.space),
        PHY_OK);
    static const char *const xy_names[2] = {"x", "y"};
    static const char *const uv_names[2] = {"u", "v"};
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            f.space, "xy", 2u, xy_names, NULL, &f.xy),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            f.space, "uv", 2u, uv_names, NULL, &f.uv),
        PHY_OK);
    f.x = phy_component_basis_coordinate(f.xy, 0u);
    f.y = phy_component_basis_coordinate(f.xy, 1u);
    f.u = phy_component_basis_coordinate(f.uv, 0u);
    f.v = phy_component_basis_coordinate(f.uv, 1u);
    return f;
}

static void fixture_close(fixture *f)
{
    phy_component_basis_destroy(f->uv);
    phy_component_basis_destroy(f->xy);
    phy_abstract_context_destroy(f->abstract);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
    phy_platform_shutdown();
}

static phy_ir_ref add2(const fixture *f, phy_ir_ref left, phy_ir_ref right)
{
    phy_ir_ref out = PHY_IR_NULL;
    const phy_ir_ref terms[2] = {left, right};
    PHY_CHECK_EQ_INT(phy_cas_add(f->cas, terms, 2u, &out), PHY_OK);
    return out;
}

static phy_ir_ref sub2(const fixture *f, phy_ir_ref left, phy_ir_ref right)
{
    phy_ir_ref out = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f->cas, left, right, &out), PHY_OK);
    return out;
}

static phy_ir_ref half(const fixture *f, phy_ir_ref value)
{
    phy_ir_ref out = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_div(
            f->cas, value, phy_ir_integer(f->ir, 2), &out),
        PHY_OK);
    return out;
}

static void check_equal(const fixture *f, phy_ir_ref actual,
                        phy_ir_ref expected)
{
    phy_ir_ref difference = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f->cas, actual, expected, &difference), PHY_OK);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_is_zero(f->cas, difference, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static void affine_transition_components(
    const fixture *f, phy_ir_ref *forward, phy_ir_ref *inverse)
{
    forward[0] = add2(f, f->x, f->y);
    forward[1] = sub2(f, f->x, f->y);
    inverse[0] = half(f, add2(f, f->u, f->v));
    inverse[1] = half(f, sub2(f, f->u, f->v));
}

static void test_verified_transition_and_jacobian(void)
{
    fixture f = fixture_open();
    phy_ir_ref forward_components[2] = {0};
    phy_ir_ref inverse_components[2] = {0};
    affine_transition_components(
        &f, forward_components, inverse_components);
    phy_basis_transition *transition = NULL;
    PHY_CHECK_EQ_INT(
        phy_basis_transition_create(
            f.xy, f.uv, forward_components, inverse_components, NULL,
            &transition),
        PHY_OK);
    const phy_coordinate_map *forward =
        phy_basis_transition_forward(transition);
    PHY_CHECK(phy_coordinate_map_source(forward) == f.xy);
    PHY_CHECK(phy_coordinate_map_target(forward) == f.uv);
    const phy_matrix *jacobian =
        phy_coordinate_map_jacobian(forward);
    PHY_CHECK_EQ_INT(phy_matrix_rows(jacobian), 2);
    PHY_CHECK_EQ_INT(phy_matrix_columns(jacobian), 2);
    static const int64_t expected[4] = {1, 1, 1, -1};
    for (size_t row = 0u; row < 2u; ++row) {
        for (size_t column = 0u; column < 2u; ++column) {
            phy_ir_ref value = PHY_IR_NULL;
            PHY_CHECK_EQ_INT(
                phy_matrix_get(jacobian, row, column, &value), PHY_OK);
            check_equal(
                &f, value,
                phy_ir_integer(f.ir, expected[row * 2u + column]));
        }
    }
    phy_basis_transition_destroy(transition);
    fixture_close(&f);
}

static void test_scalar_and_covector_pullback(void)
{
    fixture f = fixture_open();
    phy_ir_ref forward_components[2] = {0};
    phy_ir_ref inverse_components[2] = {0};
    affine_transition_components(
        &f, forward_components, inverse_components);
    phy_coordinate_map *map = NULL;
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_create(
            f.xy, f.uv, forward_components, NULL, &map),
        PHY_OK);

    phy_ir_ref u_squared = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_pow(
            f.cas, f.u, phy_ir_integer(f.ir, 2), &u_squared),
        PHY_OK);
    const phy_ir_ref scalar = add2(&f, u_squared, f.v);
    phy_ir_ref pulled_scalar = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_pullback_scalar(
            map, scalar, &pulled_scalar),
        PHY_OK);
    phy_ir_ref xy_sum_squared = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_pow(
            f.cas, forward_components[0],
            phy_ir_integer(f.ir, 2), &xy_sum_squared),
        PHY_OK);
    check_equal(
        &f, pulled_scalar,
        add2(&f, xy_sum_squared, forward_components[1]));

    /* alpha = u dv, so F*alpha = (x+y)(dx-dy). */
    const phy_ir_ref target_covector[2] = {
        phy_ir_integer(f.ir, 0), f.u};
    phy_ir_ref pulled_covector[2] = {PHY_IR_NULL, PHY_IR_NULL};
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_pullback_covector(
            map, target_covector, pulled_covector),
        PHY_OK);
    check_equal(&f, pulled_covector[0], forward_components[0]);
    phy_ir_ref negative = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_neg(f.cas, forward_components[0], &negative), PHY_OK);
    check_equal(&f, pulled_covector[1], negative);
    phy_coordinate_map_destroy(map);
    fixture_close(&f);
}

static void test_general_form_pullback(void)
{
    fixture f = fixture_open();
    phy_ir_ref forward_components[2] = {0};
    phy_ir_ref inverse_components[2] = {0};
    affine_transition_components(
        &f, forward_components, inverse_components);
    phy_coordinate_map *map = NULL;
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_create(
            f.xy, f.uv, forward_components, NULL, &map),
        PHY_OK);

    size_t target_count = 0u;
    size_t source_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_form_component_counts(
            map, 2u, &target_count, &source_count),
        PHY_OK);
    PHY_CHECK_EQ_INT(target_count, 1);
    PHY_CHECK_EQ_INT(source_count, 1);

    /* omega = u du^dv; det(d(u,v)/d(x,y)) = -2. */
    const phy_ir_ref omega[1] = {f.u};
    phy_ir_ref pulled[1] = {PHY_IR_NULL};
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_pullback_form(
            map, 2u, omega, pulled),
        PHY_OK);
    phy_ir_ref expected = PHY_IR_NULL;
    const phy_ir_ref factors[2] = {
        phy_ir_integer(f.ir, -2), forward_components[0]};
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, factors, 2u, &expected), PHY_OK);
    check_equal(&f, pulled[0], expected);

    /* Degree zero shares the exact scalar substitution path. */
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_form_component_counts(
            map, 0u, &target_count, &source_count),
        PHY_OK);
    PHY_CHECK_EQ_INT(target_count, 1);
    PHY_CHECK_EQ_INT(source_count, 1);
    const phy_ir_ref scalar[1] = {add2(&f, f.u, f.v)};
    phy_ir_ref pulled_scalar[1] = {PHY_IR_NULL};
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_pullback_form(
            map, 0u, scalar, pulled_scalar),
        PHY_OK);
    check_equal(
        &f, pulled_scalar[0],
        add2(&f, forward_components[0], forward_components[1]));

    phy_coordinate_map_destroy(map);
    fixture_close(&f);
}

static void test_transition_rejects_false_inverse_and_capture(void)
{
    fixture f = fixture_open();
    phy_ir_ref forward_components[2] = {0};
    phy_ir_ref inverse_components[2] = {0};
    affine_transition_components(
        &f, forward_components, inverse_components);
    inverse_components[1] = inverse_components[0];
    phy_basis_transition *transition = NULL;
    PHY_CHECK_EQ_INT(
        phy_basis_transition_create(
            f.xy, f.uv, forward_components, inverse_components, NULL,
            &transition),
        PHY_ERR_ASSUMPTION);
    PHY_CHECK(transition == NULL);

    static const char *const captured_names[2] = {"x", "w"};
    phy_component_basis *captured = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            f.space, "captured", 2u, captured_names, NULL, &captured),
        PHY_OK);
    phy_coordinate_map *map = NULL;
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_create(
            f.xy, captured, forward_components, NULL, &map),
        PHY_ERR_ASSUMPTION);
    PHY_CHECK(map == NULL);
    phy_component_basis_destroy(captured);

    const phy_ir_ref self_reference[2] = {f.u, f.v};
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_create(
            f.xy, f.uv, self_reference, NULL, &map),
        PHY_ERR_TYPE);
    PHY_CHECK(map == NULL);
    fixture_close(&f);
}

static void test_rectangular_map_pullback(void)
{
    fixture f = fixture_open();
    phy_index_space *curve = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "Curve", phy_ir_integer(f.ir, 1),
            PHY_METRIC_NONE, &curve),
        PHY_OK);
    static const char *const t_name[1] = {"t"};
    phy_component_basis *t_basis = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            curve, "tchart", 1u, t_name, NULL, &t_basis),
        PHY_OK);
    const phy_ir_ref t =
        phy_component_basis_coordinate(t_basis, 0u);
    phy_ir_ref t_squared = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_pow(
            f.cas, t, phy_ir_integer(f.ir, 2), &t_squared),
        PHY_OK);
    const phy_ir_ref embedding[2] = {t, t_squared};
    phy_coordinate_map *map = NULL;
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_create(
            t_basis, f.xy, embedding, NULL, &map),
        PHY_OK);
    const phy_matrix *jacobian =
        phy_coordinate_map_jacobian(map);
    PHY_CHECK_EQ_INT(phy_matrix_rows(jacobian), 2);
    PHY_CHECK_EQ_INT(phy_matrix_columns(jacobian), 1);

    /* Pull back x dy to the parabola (t,t^2): 2 t^2 dt. */
    const phy_ir_ref covector[2] = {
        phy_ir_integer(f.ir, 0), f.x};
    phy_ir_ref pulled[1] = {PHY_IR_NULL};
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_pullback_covector(map, covector, pulled),
        PHY_OK);
    phy_ir_ref expected = PHY_IR_NULL;
    const phy_ir_ref factors[2] = {
        phy_ir_integer(f.ir, 2), t_squared};
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, factors, 2u, &expected), PHY_OK);
    check_equal(&f, pulled[0], expected);

    /* A two-form pulls back to zero on a one-dimensional source. */
    size_t target_count = 0u;
    size_t source_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_form_component_counts(
            map, 2u, &target_count, &source_count),
        PHY_OK);
    PHY_CHECK_EQ_INT(target_count, 1);
    PHY_CHECK_EQ_INT(source_count, 0);
    const phy_ir_ref area_form[1] = {
        phy_ir_integer(f.ir, 1)};
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_pullback_form(
            map, 2u, area_form, NULL),
        PHY_OK);

    /* Push d/dt along (t,t^2): d/dx + 2t d/dy. */
    const phy_ir_ref tangent[1] = {
        phy_ir_integer(f.ir, 1)};
    phy_ir_ref pushed[2] = {PHY_IR_NULL, PHY_IR_NULL};
    PHY_CHECK_EQ_INT(
        phy_coordinate_map_pushforward_vector_along(
            map, tangent, pushed),
        PHY_OK);
    check_equal(&f, pushed[0], phy_ir_integer(f.ir, 1));
    const phy_ir_ref twice_t_factors[2] = {
        phy_ir_integer(f.ir, 2), t};
    phy_ir_ref twice_t = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_mul(
            f.cas, twice_t_factors, 2u, &twice_t),
        PHY_OK);
    check_equal(&f, pushed[1], twice_t);

    phy_coordinate_map_destroy(map);
    phy_component_basis_destroy(t_basis);
    fixture_close(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_verified_transition_and_jacobian);
    PHY_TEST_CASE(test_scalar_and_covector_pullback);
    PHY_TEST_CASE(test_general_form_pullback);
    PHY_TEST_CASE(test_transition_rejects_false_inverse_and_capture);
    PHY_TEST_CASE(test_rectangular_map_pullback);
    return PHY_TEST_REPORT("coordinate_map");
}
