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
    phy_component_basis *x_chart;
    phy_component_basis *u_chart;
    phy_component_basis *v_chart;
    phy_ir_ref x;
    phy_ir_ref u;
    phy_ir_ref v;
} fixture;

static phy_ir_ref shift(const fixture *f, phy_ir_ref value,
                        int64_t amount)
{
    phy_ir_ref result = PHY_IR_NULL;
    const phy_ir_ref terms[2] = {
        value, phy_ir_integer(f->ir, amount)};
    PHY_CHECK_EQ_INT(
        phy_cas_add(f->cas, terms, 2u, &result), PHY_OK);
    return result;
}

static void check_equal(const fixture *f, phy_ir_ref left,
                        phy_ir_ref right)
{
    phy_ir_ref difference = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f->cas, left, right, &difference), PHY_OK);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_is_zero(f->cas, difference, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static fixture fixture_open(void)
{
    fixture f = {0};
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(f.cas, NULL, &f.abstract),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "TM", phy_ir_integer(f.ir, 1),
            PHY_METRIC_NONE, &f.space),
        PHY_OK);
    static const char *const x_name[1] = {"x"};
    static const char *const u_name[1] = {"u"};
    static const char *const v_name[1] = {"v"};
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            f.space, "X", 1u, x_name, NULL, &f.x_chart),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            f.space, "U", 1u, u_name, NULL, &f.u_chart),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            f.space, "V", 1u, v_name, NULL, &f.v_chart),
        PHY_OK);
    f.x = phy_component_basis_coordinate(f.x_chart, 0u);
    f.u = phy_component_basis_coordinate(f.u_chart, 0u);
    f.v = phy_component_basis_coordinate(f.v_chart, 0u);
    return f;
}

static void fixture_close(fixture *f)
{
    phy_component_basis_destroy(f->v_chart);
    phy_component_basis_destroy(f->u_chart);
    phy_component_basis_destroy(f->x_chart);
    phy_abstract_context_destroy(f->abstract);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static void add_affine_edge(
    const fixture *f, phy_atlas *atlas,
    const phy_component_basis *source,
    const phy_component_basis *target,
    phy_ir_ref source_coordinate, phy_ir_ref target_coordinate,
    int64_t amount, phy_status expected)
{
    const phy_ir_ref forward[1] = {
        shift(f, source_coordinate, amount)};
    const phy_ir_ref inverse[1] = {
        shift(f, target_coordinate, -amount)};
    PHY_CHECK_EQ_INT(
        phy_atlas_add_transition(
            atlas, source, target, forward, inverse, NULL),
        expected);
}

static void test_consistent_triangle_and_lookup(void)
{
    fixture f = fixture_open();
    phy_atlas *atlas = NULL;
    PHY_CHECK_EQ_INT(
        phy_atlas_create(f.x_chart, NULL, &atlas), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, f.u_chart), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, f.v_chart), PHY_OK);
    PHY_CHECK_EQ_INT(phy_atlas_chart_count(atlas), 3);
    PHY_CHECK(phy_atlas_contains_chart(atlas, f.u_chart));

    add_affine_edge(
        &f, atlas, f.x_chart, f.u_chart, f.x, f.u, 1, PHY_OK);
    add_affine_edge(
        &f, atlas, f.u_chart, f.v_chart, f.u, f.v, 1, PHY_OK);
    add_affine_edge(
        &f, atlas, f.x_chart, f.v_chart, f.x, f.v, 2, PHY_OK);
    PHY_CHECK_EQ_INT(phy_atlas_transition_count(atlas), 3);

    size_t checks = 0u;
    PHY_CHECK_EQ_INT(
        phy_atlas_verify_cocycles(atlas, &checks), PHY_OK);
    PHY_CHECK_EQ_INT(checks, 6);
    const phy_coordinate_map *x_to_v =
        phy_atlas_transition_map(atlas, f.x_chart, f.v_chart);
    const phy_coordinate_map *v_to_x =
        phy_atlas_transition_map(atlas, f.v_chart, f.x_chart);
    PHY_CHECK(x_to_v != NULL);
    PHY_CHECK(v_to_x != NULL);
    check_equal(
        &f, phy_coordinate_map_component(x_to_v, 0u),
        shift(&f, f.x, 2));
    check_equal(
        &f, phy_coordinate_map_component(v_to_x, 0u),
        shift(&f, f.v, -2));
    const phy_ir_ref target_vector[1] = {f.v};
    const phy_ir_variance contravariant[1] = {
        PHY_IR_INDEX_UPPER};
    phy_ir_ref source_vector[1] = {PHY_IR_NULL};
    PHY_CHECK_EQ_INT(
        phy_atlas_pullback_tensor(
            atlas, f.x_chart, f.v_chart, 1u, contravariant,
            target_vector, source_vector),
        PHY_OK);
    check_equal(&f, source_vector[0], shift(&f, f.x, 2));
    PHY_CHECK_EQ_INT(
        phy_atlas_pullback_tensor(
            atlas, f.x_chart, f.x_chart, 1u, contravariant,
            target_vector, source_vector),
        PHY_ERR_ASSUMPTION);
    PHY_CHECK(
        phy_atlas_transition_map(
            atlas, f.x_chart, f.x_chart) == NULL);
    PHY_CHECK(phy_atlas_bytes_used(atlas) > 0u);

    phy_atlas_destroy(atlas);
    fixture_close(&f);
}

static void test_inconsistent_cocycle_rolls_back(void)
{
    fixture f = fixture_open();
    phy_atlas *atlas = NULL;
    PHY_CHECK_EQ_INT(
        phy_atlas_create(f.x_chart, NULL, &atlas), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, f.u_chart), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, f.v_chart), PHY_OK);
    add_affine_edge(
        &f, atlas, f.x_chart, f.u_chart, f.x, f.u, 1, PHY_OK);
    add_affine_edge(
        &f, atlas, f.u_chart, f.v_chart, f.u, f.v, 1, PHY_OK);
    add_affine_edge(
        &f, atlas, f.x_chart, f.v_chart, f.x, f.v, 3,
        PHY_ERR_ASSUMPTION);
    PHY_CHECK_EQ_INT(phy_atlas_transition_count(atlas), 2);
    PHY_CHECK(
        phy_atlas_transition_map(
            atlas, f.x_chart, f.v_chart) == NULL);

    phy_atlas_destroy(atlas);
    fixture_close(&f);
}

static void test_cocycle_budget_rolls_back(void)
{
    fixture f = fixture_open();
    phy_atlas_limits limits = {0};
    limits.max_charts = 3u;
    limits.max_transitions = 3u;
    limits.max_cocycle_checks = 5u;
    limits.max_bytes = 1024u;
    phy_atlas *atlas = NULL;
    PHY_CHECK_EQ_INT(
        phy_atlas_create(f.x_chart, &limits, &atlas), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, f.u_chart), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, f.v_chart), PHY_OK);
    add_affine_edge(
        &f, atlas, f.x_chart, f.u_chart, f.x, f.u, 1, PHY_OK);
    add_affine_edge(
        &f, atlas, f.u_chart, f.v_chart, f.u, f.v, 1, PHY_OK);
    add_affine_edge(
        &f, atlas, f.x_chart, f.v_chart, f.x, f.v, 2,
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(phy_atlas_transition_count(atlas), 2);
    PHY_CHECK(
        phy_atlas_transition_map(
            atlas, f.x_chart, f.v_chart) == NULL);

    phy_atlas_destroy(atlas);
    fixture_close(&f);
}

static void test_chart_and_resource_contracts(void)
{
    fixture f = fixture_open();
    phy_atlas_limits limits = {0};
    limits.max_charts = 2u;
    limits.max_transitions = 1u;
    limits.max_cocycle_checks = 1u;
    limits.max_bytes = 1024u;
    phy_atlas *atlas = NULL;
    PHY_CHECK_EQ_INT(
        phy_atlas_create(f.x_chart, &limits, &atlas), PHY_OK);

    phy_index_space *other_space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "Other", phy_ir_integer(f.ir, 1),
            PHY_METRIC_NONE, &other_space),
        PHY_OK);
    static const char *const z_name[1] = {"z"};
    phy_component_basis *z_chart = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            other_space, "Z", 1u, z_name, NULL, &z_chart),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, z_chart), PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, f.u_chart), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, f.v_chart), PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(
        phy_atlas_add_chart(atlas, f.u_chart), PHY_ERR_ASSUMPTION);

    phy_component_basis_destroy(z_chart);
    phy_atlas_destroy(atlas);
    fixture_close(&f);
}

int main(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    PHY_TEST_CASE(test_consistent_triangle_and_lookup);
    PHY_TEST_CASE(test_inconsistent_cocycle_rolls_back);
    PHY_TEST_CASE(test_cocycle_budget_rolls_back);
    PHY_TEST_CASE(test_chart_and_resource_contracts);
    const int result = PHY_TEST_REPORT("atlas");
    phy_platform_shutdown();
    return result;
}
