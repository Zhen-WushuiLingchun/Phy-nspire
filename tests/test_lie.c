#include <stdio.h>

#include "phy/lie.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
} fixture;

static fixture fixture_open(void)
{
    fixture value;
    value.ir = phy_ir_context_create(NULL);
    value.cas = phy_cas_create(value.ir, NULL);
    PHY_CHECK(value.ir != NULL);
    PHY_CHECK(value.cas != NULL);
    return value;
}

static void fixture_close(fixture *value)
{
    phy_cas_destroy(value->cas);
    phy_ir_context_destroy(value->ir);
}

static void expect_integer(phy_cas *cas, phy_ir_ref actual, int64_t expected)
{
    phy_ir_ref wanted = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(cas, expected, 1, &wanted), PHY_OK);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(cas, actual, wanted, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static phy_ir_ref read_expr(phy_ir_context *ir, const char *text)
{
    phy_ir_ref expression = PHY_IR_NULL;
    size_t offset = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_read(ir, text, &expression, &offset), PHY_OK);
    return expression;
}

static void expect_expression(fixture *f, phy_ir_ref actual,
                              const char *expected)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f->cas, actual, read_expr(f->ir, expected), &decision),
        PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static void test_builtin_catalog_and_metadata(void)
{
    fixture f = fixture_open();
    static const struct {
        phy_lie_group_kind kind;
        const char *name;
        unsigned algebra_dimension;
        unsigned representation_dimension;
        bool compact;
    } cases[] = {
        {PHY_LIE_GROUP_U1, "U(1)", 1u, 1u, true},
        {PHY_LIE_GROUP_SU2, "SU(2)", 3u, 2u, true},
        {PHY_LIE_GROUP_SO3, "SO(3)", 3u, 3u, true},
        {PHY_LIE_GROUP_SU3, "SU(3)", 8u, 3u, true},
        {PHY_LIE_GROUP_SO13, "SO(1,3)", 6u, 4u, false},
    };
    for (size_t index = 0u; index < sizeof cases / sizeof cases[0];
         ++index) {
        phy_lie_group *group = NULL;
        PHY_CHECK_EQ_INT(
            phy_lie_group_builtin(f.cas, cases[index].kind, &group), PHY_OK);
        PHY_CHECK(group != NULL);
        if (group == NULL) {
            continue;
        }
        PHY_CHECK_EQ_STR(phy_lie_group_name(group), cases[index].name);
        PHY_CHECK_EQ_INT(
            phy_lie_group_representation_dimension(group),
            cases[index].representation_dimension);
        PHY_CHECK_EQ_INT(
            phy_lie_group_is_compact(group), cases[index].compact);
        const phy_lie_algebra *algebra = phy_lie_group_algebra(group);
        PHY_CHECK(algebra != NULL);
        PHY_CHECK_EQ_INT(
            phy_lie_algebra_dimension(algebra),
            cases[index].algebra_dimension);
        PHY_CHECK_EQ_INT(
            phy_lie_algebra_validate(algebra), PHY_OK);
        phy_lie_group_destroy(group);
    }
    fixture_close(&f);
}

static void test_su2_bracket_and_killing_form(void)
{
    fixture f = fixture_open();
    phy_lie_group *group = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_group_builtin(f.cas, PHY_LIE_GROUP_SU2, &group), PHY_OK);
    const phy_lie_algebra *algebra = phy_lie_group_algebra(group);
    phy_lie_element *t1 = NULL;
    phy_lie_element *t2 = NULL;
    phy_lie_element *t3 = NULL;
    phy_lie_element *bracket = NULL;
    PHY_CHECK_EQ_INT(phy_lie_basis_element(algebra, 0u, &t1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lie_basis_element(algebra, 1u, &t2), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lie_basis_element(algebra, 2u, &t3), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lie_bracket(t1, t2, &bracket), PHY_OK);
    expect_integer(f.cas, phy_lie_element_coefficient(bracket, 0u), 0);
    expect_integer(f.cas, phy_lie_element_coefficient(bracket, 1u), 0);
    expect_integer(f.cas, phy_lie_element_coefficient(bracket, 2u), 1);

    phy_lie_element *reverse = NULL;
    PHY_CHECK_EQ_INT(phy_lie_bracket(t2, t1, &reverse), PHY_OK);
    expect_integer(f.cas, phy_lie_element_coefficient(reverse, 2u), -1);

    for (unsigned a = 0u; a < 3u; ++a) {
        for (unsigned b = 0u; b < 3u; ++b) {
            phy_ir_ref value = PHY_IR_NULL;
            PHY_CHECK_EQ_INT(
                phy_lie_killing_component(algebra, a, b, &value),
                PHY_OK);
            expect_integer(f.cas, value, a == b ? -2 : 0);
        }
    }

    PHY_CHECK_EQ_INT(
        phy_lie_adjoint_component(algebra, 0u, 2u, 1u),
        phy_lie_structure_constant(algebra, 0u, 1u, 2u));

    phy_lie_element_destroy(reverse);
    phy_lie_element_destroy(bracket);
    phy_lie_element_destroy(t3);
    phy_lie_element_destroy(t2);
    phy_lie_element_destroy(t1);
    phy_lie_group_destroy(group);
    fixture_close(&f);
}

static void test_builtin_normalizations_are_pinned(void)
{
    fixture f = fixture_open();
    phy_lie_group *su3 = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_group_builtin(f.cas, PHY_LIE_GROUP_SU3, &su3), PHY_OK);
    const phy_lie_algebra *su3_algebra = phy_lie_group_algebra(su3);

    /* [T4,T5] = 1/2 T3 + sqrt(3)/2 T8 in the Gell-Mann basis. */
    expect_expression(
        &f, phy_lie_structure_constant(su3_algebra, 3u, 4u, 2u),
        "(rat 1 2)");
    expect_expression(
        &f, phy_lie_structure_constant(su3_algebra, 3u, 4u, 7u),
        "(* (rat 1 2) (^ 3 (rat 1 2)))");
    expect_expression(
        &f, phy_lie_structure_constant(su3_algebra, 4u, 3u, 7u),
        "(* (rat -1 2) (^ 3 (rat 1 2)))");
    phy_lie_group_destroy(su3);

    phy_lie_group *lorentz = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_group_builtin(
            f.cas, PHY_LIE_GROUP_SO13, &lorentz),
        PHY_OK);
    const phy_lie_algebra *lorentz_algebra =
        phy_lie_group_algebra(lorentz);
    /* [K1,K2] = -J3 and [J1,K2] = K3. */
    expect_integer(
        f.cas,
        phy_lie_structure_constant(lorentz_algebra, 3u, 4u, 2u),
        -1);
    expect_integer(
        f.cas,
        phy_lie_structure_constant(lorentz_algebra, 0u, 4u, 5u),
        1);
    phy_lie_group_destroy(lorentz);
    fixture_close(&f);
}

static void test_symbolic_bilinearity(void)
{
    fixture f = fixture_open();
    phy_lie_group *group = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_group_builtin(f.cas, PHY_LIE_GROUP_SU2, &group), PHY_OK);
    const phy_lie_algebra *algebra = phy_lie_group_algebra(group);
    phy_ir_ref x = phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "x"));
    phy_ir_ref y = phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "y"));
    phy_ir_ref zero = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 0, 1, &zero), PHY_OK);
    const phy_ir_ref left_coefficients[3] = {x, zero, zero};
    const phy_ir_ref right_coefficients[3] = {zero, y, zero};
    phy_lie_element *left = NULL;
    phy_lie_element *right = NULL;
    phy_lie_element *bracket = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_element_create(algebra, left_coefficients, &left), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lie_element_create(algebra, right_coefficients, &right), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lie_bracket(left, right, &bracket), PHY_OK);
    phy_ir_ref xy = PHY_IR_NULL;
    const phy_ir_ref factors[2] = {x, y};
    PHY_CHECK_EQ_INT(phy_cas_mul(f.cas, factors, 2u, &xy), PHY_OK);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f.cas, phy_lie_element_coefficient(bracket, 2u), xy,
            &decision),
        PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
    phy_lie_element_destroy(bracket);
    phy_lie_element_destroy(right);
    phy_lie_element_destroy(left);
    phy_lie_group_destroy(group);
    fixture_close(&f);
}

static void test_generic_noncommutative_commutator(void)
{
    fixture f = fixture_open();
    const phy_ir_ref a =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "A"));
    const phy_ir_ref b =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "B"));
    phy_ir_ref actual = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_commutator_ir(f.cas, a, b, &actual), PHY_OK);
    const phy_ir_ref ab_factors[2] = {a, b};
    const phy_ir_ref ba_factors[2] = {b, a};
    const phy_ir_ref ab = phy_ir_ncmul(f.ir, ab_factors, 2u);
    const phy_ir_ref ba = phy_ir_ncmul(f.ir, ba_factors, 2u);
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f.cas, ab, ba, &expected), PHY_OK);
    PHY_CHECK_EQ_INT(actual, expected);
    fixture_close(&f);
}

static void test_rejects_non_lie_structure_constants(void)
{
    fixture f = fixture_open();
    static const char *const basis[3] = {"e1", "e2", "e3"};
    phy_ir_ref constants[27];
    phy_ir_ref zero = PHY_IR_NULL;
    phy_ir_ref one = PHY_IR_NULL;
    phy_ir_ref minus_one = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 0, 1, &zero), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 1, 1, &one), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, -1, 1, &minus_one), PHY_OK);
    for (size_t index = 0u; index < 27u; ++index) {
        constants[index] = zero;
    }
#define C(a, b, c) constants[((a) * 3u + (b)) * 3u + (c)]
    C(0u, 1u, 0u) = one;
    C(1u, 0u, 0u) = minus_one;
    C(1u, 2u, 1u) = one;
    C(2u, 1u, 1u) = minus_one;
    C(2u, 0u, 2u) = one;
    C(0u, 2u, 2u) = minus_one;
#undef C
    phy_lie_algebra *algebra = NULL;
    PHY_CHECK_EQ_INT(
        phy_lie_algebra_create(
            f.cas, "notLie", basis, 3u, constants, &algebra),
        PHY_ERR_ASSUMPTION);
    PHY_CHECK(algebra == NULL);
    fixture_close(&f);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }
    PHY_TEST_CASE(test_builtin_catalog_and_metadata);
    PHY_TEST_CASE(test_su2_bracket_and_killing_form);
    PHY_TEST_CASE(test_builtin_normalizations_are_pinned);
    PHY_TEST_CASE(test_symbolic_bilinearity);
    PHY_TEST_CASE(test_generic_noncommutative_commutator);
    PHY_TEST_CASE(test_rejects_non_lie_structure_constants);
    const int result = PHY_TEST_REPORT("test_lie");
    phy_platform_shutdown();
    return result;
}
