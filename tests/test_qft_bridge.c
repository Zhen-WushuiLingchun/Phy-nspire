/*
 * Acceptance for the shared QFT abstract/component picture.
 */
#include <stdio.h>

#include "phy/qft_bridge.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_abstract_context *abstract;
} fixture;

static fixture fixture_open(void)
{
    fixture f;
    f.ir = phy_ir_context_create(NULL);
    f.cas = f.ir != NULL ? phy_cas_create(f.ir, NULL) : NULL;
    f.abstract = NULL;
    PHY_CHECK(f.ir != NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(f.cas, NULL, &f.abstract), PHY_OK);
    PHY_CHECK(f.abstract != NULL);
    return f;
}

static void fixture_close(fixture *f)
{
    PHY_CHECK_EQ_INT(phy_cas_validate(f->cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(f->ir), PHY_OK);
    phy_abstract_context_destroy(f->abstract);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static void expect_integer(phy_ir_context *ir, phy_ir_ref ref, int64_t expected)
{
    int64_t value = 0;
    PHY_CHECK(phy_ir_integer_value(ir, ref, &value));
    PHY_CHECK_EQ_INT(value, expected);
}

static void expect_equivalent(phy_cas *cas, phy_ir_ref left, phy_ir_ref right)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(cas, left, right, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static void test_spaces_heads_and_exact_components(void)
{
    fixture f = fixture_open();
    const phy_ir_ref three = phy_ir_integer(f.ir, 3);
    phy_qft_component_view *view = NULL;
    PHY_CHECK_EQ_INT(
        phy_qft_component_view_create(
            f.cas, f.abstract, three, NULL, &view),
        PHY_OK);
    PHY_CHECK(view != NULL);
    PHY_CHECK_EQ_INT(phy_qft_component_view_n(view), three);

    const size_t expected_dimensions[PHY_QFT_SPACE_COUNT] = {4u, 4u, 8u, 3u};
    for (unsigned which = 0u; which < (unsigned)PHY_QFT_SPACE_COUNT;
         ++which) {
        const phy_index_space *space = phy_qft_component_view_space(
            view, (phy_qft_space)which);
        size_t dimension = 0u;
        PHY_CHECK(space != NULL);
        PHY_CHECK_EQ_STR(
            phy_index_space_name(space),
            phy_qft_space_name((phy_qft_space)which));
        PHY_CHECK(phy_index_space_known_dimension(space, &dimension));
        PHY_CHECK_EQ_INT(dimension, expected_dimensions[which]);
        PHY_CHECK(phy_qft_component_view_has_basis(
            view, (phy_qft_space)which));
    }
    PHY_CHECK_EQ_INT(
        phy_index_space_metric(phy_qft_component_view_space(
            view, PHY_QFT_SPACE_LORENTZ)),
        PHY_METRIC_SYMMETRIC);
    PHY_CHECK_EQ_INT(
        phy_index_space_metric(phy_qft_component_view_space(
            view, PHY_QFT_SPACE_SPINOR)),
        PHY_METRIC_NONE);

    for (unsigned quantity = 0u;
         quantity < (unsigned)PHY_QFT_QUANTITY_COUNT; ++quantity) {
        const phy_abstract_tensor_head *head =
            phy_qft_component_view_head(
                view, (phy_qft_quantity)quantity);
        PHY_CHECK(head != NULL);
        PHY_CHECK(phy_qft_quantity_name(
                      (phy_qft_quantity)quantity) != NULL);
    }
    PHY_CHECK_EQ_INT(
        phy_tensor_head_symmetry_count(phy_qft_component_view_head(
            view, PHY_QFT_SUN_F)),
        2);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_commutation(phy_qft_component_view_head(
            view, PHY_QFT_DIRAC_GAMMA)),
        PHY_TENSOR_NONCOMMUTING);
    PHY_CHECK(
        phy_tensor_head_slot_space(
            phy_qft_component_view_head(view, PHY_QFT_FIELD_STRENGTH), 0u) ==
        phy_qft_component_view_space(view, PHY_QFT_SPACE_COLOR_ADJOINT));
    PHY_CHECK(
        phy_tensor_head_slot_space(
            phy_qft_component_view_head(view, PHY_QFT_FIELD_STRENGTH), 1u) ==
        phy_qft_component_view_space(view, PHY_QFT_SPACE_LORENTZ));

    PHY_CHECK(phy_qft_component_view_holds(
        view, PHY_QFT_MINKOWSKI_METRIC));
    PHY_CHECK(phy_qft_component_view_holds(
        view, PHY_QFT_MINKOWSKI_INVERSE));
    PHY_CHECK(phy_qft_component_view_holds(
        view, PHY_QFT_SUN_DELTA));
    PHY_CHECK(!phy_qft_component_view_holds(view, PHY_QFT_SUN_F));
    PHY_CHECK(!phy_qft_component_view_holds(view, PHY_QFT_SUN_D));
    PHY_CHECK(!phy_qft_component_view_holds(
        view, PHY_QFT_DIRAC_GAMMA));

    phy_component_tensor *eta = phy_qft_component_view_tensor(
        view, PHY_QFT_MINKOWSKI_METRIC);
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(
            eta, (const uint32_t[2]){0u, 0u}, &value),
        PHY_OK);
    expect_integer(f.ir, value, 1);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(
            eta, (const uint32_t[2]){1u, 1u}, &value),
        PHY_OK);
    expect_integer(f.ir, value, -1);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(
            eta, (const uint32_t[2]){0u, 1u}, &value),
        PHY_OK);
    expect_integer(f.ir, value, 0);

    /*
     * The expensive SU(3) table is not part of system construction. It is
     * built once, on explicit demand, and repeated demand is idempotent.
     */
    PHY_CHECK_EQ_INT(
        phy_qft_component_view_materialize(view, PHY_QFT_SUN_F), PHY_OK);
    PHY_CHECK(phy_qft_component_view_holds(view, PHY_QFT_SUN_F));
    PHY_CHECK_EQ_INT(
        phy_qft_component_view_materialize(view, PHY_QFT_SUN_F), PHY_OK);
    phy_component_tensor *structure = phy_qft_component_view_tensor(
        view, PHY_QFT_SUN_F);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(
            structure, (const uint32_t[3]){0u, 1u, 2u}, &value),
        PHY_OK);
    expect_integer(f.ir, value, 1);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(
            structure, (const uint32_t[3]){1u, 0u, 2u}, &value),
        PHY_OK);
    expect_integer(f.ir, value, -1);

    phy_component_binding *binding = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(f.abstract, NULL, &binding), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_qft_component_view_bind(view, binding), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_basis_count(binding), PHY_QFT_SPACE_COUNT);
    PHY_CHECK_EQ_INT(phy_component_binding_tensor_count(binding), 4);
    PHY_CHECK_EQ_INT(
        phy_qft_component_view_bind(view, binding), PHY_OK);
    phy_component_binding_destroy(binding);

    /* A duplicate would create two printed Lorentz spaces with distinct
     * identity, so it is refused rather than silently aliased. */
    phy_qft_component_view *duplicate = NULL;
    PHY_CHECK_EQ_INT(
        phy_qft_component_view_create(
            f.cas, f.abstract, three, NULL, &duplicate),
        PHY_ERR_ALREADY_INITIALIZED);
    PHY_CHECK(duplicate == NULL);

    phy_qft_component_view_destroy(view);
    fixture_close(&f);
}

static void test_symbolic_n_keeps_exact_dimension_relation(void)
{
    fixture f = fixture_open();
    const phy_ir_ref n =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "N"));
    phy_qft_component_view *view = NULL;
    PHY_CHECK_EQ_INT(
        phy_qft_component_view_create(f.cas, f.abstract, n, NULL, &view),
        PHY_OK);
    PHY_CHECK(view != NULL);

    const phy_index_space *adjoint = phy_qft_component_view_space(
        view, PHY_QFT_SPACE_COLOR_ADJOINT);
    size_t dimension = 0u;
    PHY_CHECK(!phy_index_space_known_dimension(adjoint, &dimension));
    const phy_ir_ref two = phy_ir_integer(f.ir, 2);
    const phy_ir_ref one = phy_ir_integer(f.ir, 1);
    phy_ir_ref square = PHY_IR_NULL;
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_pow(f.cas, n, two, &square), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f.cas, square, one, &expected), PHY_OK);
    expect_equivalent(
        f.cas, phy_index_space_dimension(adjoint), expected);

    PHY_CHECK(phy_qft_component_view_has_basis(
        view, PHY_QFT_SPACE_LORENTZ));
    PHY_CHECK(phy_qft_component_view_has_basis(
        view, PHY_QFT_SPACE_SPINOR));
    PHY_CHECK(!phy_qft_component_view_has_basis(
        view, PHY_QFT_SPACE_COLOR_ADJOINT));
    PHY_CHECK(phy_qft_component_view_holds(
        view, PHY_QFT_MINKOWSKI_METRIC));
    PHY_CHECK(!phy_qft_component_view_holds(view, PHY_QFT_SUN_DELTA));
    PHY_CHECK(!phy_qft_component_view_holds(view, PHY_QFT_SUN_F));

    phy_qft_component_view_destroy(view);
    fixture_close(&f);
}

static void test_component_availability_and_limits_are_explicit(void)
{
    {
        fixture f = fixture_open();
        phy_qft_component_view *view = NULL;
        PHY_CHECK_EQ_INT(
            phy_qft_component_view_create(
                f.cas, f.abstract, phy_ir_integer(f.ir, 4), NULL, &view),
            PHY_OK);
        PHY_CHECK(view != NULL);
        PHY_CHECK(phy_qft_component_view_holds(
            view, PHY_QFT_SUN_DELTA));
        PHY_CHECK(!phy_qft_component_view_holds(view, PHY_QFT_SUN_F));
        PHY_CHECK_EQ_INT(
            phy_qft_component_view_materialize(view, PHY_QFT_SUN_F),
            PHY_ERR_NOT_INITIALIZED);
        PHY_CHECK_EQ_INT(
            phy_component_basis_dimension(
                phy_qft_component_view_basis(
                    view, PHY_QFT_SPACE_COLOR_ADJOINT)),
            15);
        phy_qft_component_view_destroy(view);
        fixture_close(&f);
    }
    {
        fixture f = fixture_open();
        phy_qft_component_view *view = NULL;
        PHY_CHECK_EQ_INT(
            phy_qft_component_view_create(
                f.cas, f.abstract, phy_ir_integer(f.ir, 17), NULL, &view),
            PHY_OK);
        PHY_CHECK(view != NULL);
        PHY_CHECK(!phy_qft_component_view_has_basis(
            view, PHY_QFT_SPACE_COLOR_ADJOINT));
        PHY_CHECK(!phy_qft_component_view_holds(
            view, PHY_QFT_SUN_DELTA));
        phy_qft_component_view_destroy(view);
        fixture_close(&f);
    }
    {
        fixture f = fixture_open();
        phy_qft_component_view *view =
            (phy_qft_component_view *)(uintptr_t)1u;
        PHY_CHECK_EQ_INT(
            phy_qft_component_view_create(
                f.cas, f.abstract, phy_ir_rational(f.ir, 3, 2), NULL,
                &view),
            PHY_ERR_DOMAIN);
        PHY_CHECK(view == NULL);

        phy_qft_bridge_limits limits;
        phy_qft_bridge_limits_defaults(&limits);
        limits.max_bytes = 1u;
        PHY_CHECK_EQ_INT(
            phy_qft_component_view_create(
                f.cas, f.abstract, phy_ir_integer(f.ir, 3), &limits,
                &view),
            PHY_ERR_MEMORY_LIMIT);
        PHY_CHECK(view == NULL);
        PHY_CHECK_EQ_INT(phy_abstract_space_count(f.abstract), 0);
        fixture_close(&f);
    }
}

int main(void)
{
    PHY_TEST_CASE(test_spaces_heads_and_exact_components);
    PHY_TEST_CASE(test_symbolic_n_keeps_exact_dimension_relation);
    PHY_TEST_CASE(test_component_availability_and_limits_are_explicit);
    return PHY_TEST_REPORT("qft_bridge");
}
