#include <stdio.h>
#include <string.h>

#include "phy/color.h"
#include "phy/lorentz.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_color *color;
    phy_ir_ref n;
    phy_ir_ref a;
    phy_ir_ref b;
    phy_ir_ref c;
    phy_ir_ref d;
} fixture;

static phy_ir_ref named(phy_ir_context *ir, const char *name)
{
    return phy_ir_symbol_ref(ir, phy_ir_intern(ir, name));
}

static fixture fixture_open_symbolic(void)
{
    fixture f;
    memset(&f, 0, sizeof f);
    f.ir = phy_ir_context_create(NULL);
    f.cas = f.ir != NULL ? phy_cas_create(f.ir, NULL) : NULL;
    PHY_CHECK(f.ir != NULL);
    PHY_CHECK(f.cas != NULL);
    f.n = named(f.ir, "N");
    PHY_CHECK(f.n != PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_color_create(f.cas, f.n, &f.color), PHY_OK);
    PHY_CHECK(f.color != NULL);
    PHY_CHECK_EQ_INT(phy_color_index(f.color, "a", &f.a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_color_index(f.color, "b", &f.b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_color_index(f.color, "c", &f.c), PHY_OK);
    PHY_CHECK_EQ_INT(phy_color_index(f.color, "d", &f.d), PHY_OK);
    return f;
}

static void fixture_close(fixture *f)
{
    PHY_CHECK_EQ_INT(phy_cas_validate(f->cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(f->ir), PHY_OK);
    phy_color_destroy(f->color);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static void expect_equivalent(fixture *f, phy_ir_ref actual,
                              phy_ir_ref expected)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(f->cas, actual, expected, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static void expect_number(fixture *f, phy_ir_ref actual, int64_t numerator,
                          int64_t denominator)
{
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_number(f->cas, numerator, denominator, &expected), PHY_OK);
    expect_equivalent(f, actual, expected);
}

static const char *head_name(const fixture *f, phy_ir_ref expression)
{
    return phy_ir_symbol_name(f->ir, phy_ir_head(f->ir, expression));
}

static void test_symbolic_dimension_and_casimirs(void)
{
    fixture f = fixture_open_symbolic();
    PHY_CHECK(phy_color_cas(f.color) == f.cas);
    PHY_CHECK_EQ_INT(phy_color_n(f.color), f.n);
    PHY_CHECK(phy_color_adjoint_space(f.color) != PHY_IR_NO_SYMBOL);
    PHY_CHECK(phy_color_owns_index(f.color, f.a));

    phy_ir_ref dimension = PHY_IR_NULL;
    phy_ir_ref cf = PHY_IR_NULL;
    phy_ir_ref ca = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_adjoint_dimension(f.color, &dimension), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_casimir_fundamental(f.color, &cf), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_casimir_adjoint(f.color, &ca), PHY_OK);

    const phy_ir_ref two = phy_ir_integer(f.ir, 2);
    const phy_ir_ref one = phy_ir_integer(f.ir, 1);
    phy_ir_ref n_squared = PHY_IR_NULL;
    phy_ir_ref expected_dimension = PHY_IR_NULL;
    phy_ir_ref denominator = PHY_IR_NULL;
    phy_ir_ref expected_cf = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_pow(f.cas, f.n, two, &n_squared), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f.cas, n_squared, one, &expected_dimension), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_mul(
            f.cas, (const phy_ir_ref[2]){two, f.n}, 2u, &denominator),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_div(f.cas, expected_dimension, denominator, &expected_cf),
        PHY_OK);
    expect_equivalent(&f, dimension, expected_dimension);
    expect_equivalent(&f, cf, expected_cf);
    expect_equivalent(&f, ca, f.n);

    /* Presentation remains C_F/C_A until explicitly expanded. */
    phy_ir_ref fundamental = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_fundamental_casimir(f.color, &fundamental), PHY_OK);
    PHY_CHECK(strstr(head_name(&f, fundamental) != NULL
                         ? head_name(&f, fundamental)
                         : "",
                     "C_F") == NULL);
    char text[512];
    size_t needed = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, fundamental, text, sizeof text, &needed), PHY_OK);
    PHY_CHECK(strstr(text, "C_F") != NULL);
    PHY_CHECK(strstr(text, "IdentityFundamental") != NULL);

    phy_ir_ref presentation = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_add(
            f.cas,
            (const phy_ir_ref[2]){phy_color_cf_symbol(f.color),
                                  phy_color_ca_symbol(f.color)},
            2u, &presentation),
        PHY_OK);
    phy_ir_ref expanded = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_expand_casimirs(f.color, presentation, &expanded),
        PHY_OK);
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_add(f.cas, (const phy_ir_ref[2]){expected_cf, f.n}, 2u,
                    &expected),
        PHY_OK);
    expect_equivalent(&f, expanded, expected);
    fixture_close(&f);
}

static void test_invariant_tensor_symmetries(void)
{
    fixture f = fixture_open_symbolic();
    phy_ir_ref delta_ab = PHY_IR_NULL;
    phy_ir_ref delta_ba = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_delta(f.color, f.a, f.b, &delta_ab), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_delta(f.color, f.b, f.a, &delta_ba), PHY_OK);
    PHY_CHECK_EQ_INT(delta_ab, delta_ba);
    PHY_CHECK_EQ_STR(head_name(&f, delta_ab), "SUNDelta");

    phy_ir_ref delta_aa = PHY_IR_NULL;
    phy_ir_ref dimension = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_delta(f.color, f.a, f.a, &delta_aa), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_adjoint_dimension(f.color, &dimension), PHY_OK);
    expect_equivalent(&f, delta_aa, dimension);

    phy_ir_ref f_abc = PHY_IR_NULL;
    phy_ir_ref f_bac = PHY_IR_NULL;
    phy_ir_ref f_bca = PHY_IR_NULL;
    phy_ir_ref negative = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant(f.color, f.a, f.b, f.c, &f_abc),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant(f.color, f.b, f.a, f.c, &f_bac),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant(f.color, f.b, f.c, f.a, &f_bca),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_neg(f.cas, f_abc, &negative), PHY_OK);
    expect_equivalent(&f, f_bac, negative);
    expect_equivalent(&f, f_bca, f_abc);
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant(f.color, f.a, f.a, f.c, &f_bac),
        PHY_OK);
    expect_number(&f, f_bac, 0, 1);

    phy_ir_ref d_abc = PHY_IR_NULL;
    phy_ir_ref d_cba = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_symmetric_tensor(f.color, f.a, f.b, f.c, &d_abc),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_symmetric_tensor(f.color, f.c, f.b, f.a, &d_cba),
        PHY_OK);
    PHY_CHECK_EQ_INT(d_abc, d_cba);
    PHY_CHECK_EQ_STR(head_name(&f, d_abc), "SUND");
    fixture_close(&f);
}

static void test_delta_contraction(void)
{
    fixture f = fixture_open_symbolic();
    phy_ir_ref generator_b = PHY_IR_NULL;
    phy_ir_ref generator_a = PHY_IR_NULL;
    phy_ir_ref contracted = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_generator(f.color, f.b, &generator_b), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_generator(f.color, f.a, &generator_a), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_delta_contract(
            f.color, f.a, f.b, generator_b, &contracted),
        PHY_OK);
    PHY_CHECK_EQ_INT(contracted, generator_a);

    phy_ir_ref untouched = named(f.ir, "x");
    PHY_CHECK_EQ_INT(
        phy_color_delta_contract(f.color, f.a, f.b, untouched, &contracted),
        PHY_OK);
    char text[512];
    size_t needed = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, contracted, text, sizeof text, &needed), PHY_OK);
    PHY_CHECK(strstr(text, "SUNDelta") != NULL);
    PHY_CHECK(strstr(text, "x") != NULL);

    /* Ambiguous multiple use is deliberately not guessed. */
    phy_ir_ref double_generator = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_mul(
            f.cas, (const phy_ir_ref[2]){generator_b, generator_b}, 2u,
            &double_generator),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_delta_contract(
            f.color, f.a, f.b, double_generator, &contracted),
        PHY_ERR_UNSUPPORTED);
    fixture_close(&f);
}

static void test_traces_and_commutator(void)
{
    fixture f = fixture_open_symbolic();
    phy_ir_ref trace = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_color_trace(f.color, NULL, 0u, &trace), PHY_OK);
    PHY_CHECK_EQ_INT(trace, f.n);
    PHY_CHECK_EQ_INT(
        phy_color_trace(f.color, (const phy_ir_ref[1]){f.a}, 1u, &trace),
        PHY_OK);
    expect_number(&f, trace, 0, 1);

    PHY_CHECK_EQ_INT(
        phy_color_trace(
            f.color, (const phy_ir_ref[2]){f.a, f.b}, 2u, &trace),
        PHY_OK);
    phy_ir_ref delta = PHY_IR_NULL;
    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_delta(f.color, f.a, f.b, &delta), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 1, 2, &half), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_mul(
            f.cas, (const phy_ir_ref[2]){half, delta}, 2u, &expected),
        PHY_OK);
    expect_equivalent(&f, trace, expected);

    PHY_CHECK_EQ_INT(
        phy_color_trace(
            f.color, (const phy_ir_ref[3]){f.a, f.b, f.c}, 3u, &trace),
        PHY_OK);
    char text[1024];
    size_t needed = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, trace, text, sizeof text, &needed), PHY_OK);
    PHY_CHECK(strstr(text, "SUND") != NULL);
    PHY_CHECK(strstr(text, "SUNF") != NULL);
    PHY_CHECK(strstr(text, "I") != NULL);

    PHY_CHECK_EQ_INT(
        phy_color_trace(
            f.color,
            (const phy_ir_ref[4]){f.a, f.b, f.c, f.d}, 4u, &trace),
        PHY_OK);
    PHY_CHECK_EQ_STR(head_name(&f, trace), "SUNTrace");
    PHY_CHECK_EQ_INT(phy_ir_child_count(f.ir, trace), 5);
    PHY_CHECK_EQ_INT(phy_ir_child(f.ir, trace, 0u), f.n);

    phy_ir_ref commutator = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_commutator(f.color, f.a, f.b, &commutator), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, commutator, text, sizeof text, &needed), PHY_OK);
    PHY_CHECK(strstr(text, "I") != NULL);
    PHY_CHECK(strstr(text, "SUNF") != NULL);
    PHY_CHECK(strstr(text, "SUNGenerator") != NULL);
    PHY_CHECK(strstr(text, "ColorAdjoint") != NULL);

    PHY_CHECK_EQ_INT(
        phy_color_commutator(f.color, f.a, f.a, &commutator), PHY_OK);
    expect_number(&f, commutator, 0, 1);
    fixture_close(&f);
}

static void test_exact_su2_su3_components(void)
{
    fixture f = fixture_open_symbolic();
    phy_color_destroy(f.color);
    f.color = NULL;

    phy_ir_ref n2 = phy_ir_integer(f.ir, 2);
    PHY_CHECK_EQ_INT(phy_color_create(f.cas, n2, &f.color), PHY_OK);
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant_component(
            f.color, 1u, 2u, 3u, &value),
        PHY_OK);
    expect_number(&f, value, 1, 1);
    phy_color_destroy(f.color);
    f.color = NULL;

    phy_ir_ref n3 = phy_ir_integer(f.ir, 3);
    PHY_CHECK_EQ_INT(phy_color_create(f.cas, n3, &f.color), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant_component(
            f.color, 1u, 2u, 3u, &value),
        PHY_OK);
    expect_number(&f, value, 1, 1);
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant_component(
            f.color, 1u, 4u, 7u, &value),
        PHY_OK);
    expect_number(&f, value, 1, 2);
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant_component(
            f.color, 4u, 5u, 8u, &value),
        PHY_OK);
    phy_ir_ref three = phy_ir_integer(f.ir, 3);
    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref root = PHY_IR_NULL;
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 1, 2, &half), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_pow(f.cas, three, half, &root), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_mul(
            f.cas, (const phy_ir_ref[2]){half, root}, 2u, &expected),
        PHY_OK);
    expect_equivalent(&f, value, expected);

    phy_ir_ref cf = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_casimir_fundamental(f.color, &cf), PHY_OK);
    expect_number(&f, cf, 4, 3);
    fixture_close(&f);
}

static void test_index_spaces_and_errors_are_typed(void)
{
    fixture f = fixture_open_symbolic();
    phy_lorentz_metric *metric = NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_metric_create(
            f.cas, "g", "Lorentz", 4u, PHY_LORENTZ_MOSTLY_MINUS,
            &metric),
        PHY_OK);
    phy_ir_ref mu = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_index(metric, "mu", PHY_IR_INDEX_UPPER, &mu), PHY_OK);
    PHY_CHECK(!phy_color_owns_index(f.color, mu));
    phy_ir_ref result = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_delta(f.color, f.a, mu, &result), PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant(f.color, f.a, f.b, mu, &result),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_color_trace(f.color, &mu, 1u, &result), PHY_ERR_TYPE);
    phy_lorentz_metric_destroy(metric);

    phy_color *bad = NULL;
    PHY_CHECK_EQ_INT(
        phy_color_create(f.cas, phy_ir_integer(f.ir, 1), &bad),
        PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(
        phy_color_create(f.cas, phy_ir_rational(f.ir, 5, 2), &bad),
        PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(
        phy_color_create(f.cas, phy_ir_real(f.ir, 3.0), &bad),
        PHY_ERR_TYPE);
    phy_ir_ref inexact_sum = phy_ir_add(
        f.ir,
        (const phy_ir_ref[2]){f.n, phy_ir_real(f.ir, 0.5)}, 2u);
    PHY_CHECK_EQ_INT(
        phy_color_create(f.cas, inexact_sum, &bad), PHY_ERR_TYPE);
    phy_ir_ref delta = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_delta(f.color, f.a, f.b, &delta), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_create(f.cas, delta, &bad), PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_color_trace(
            f.color,
            (const phy_ir_ref[17]){
                f.a, f.a, f.a, f.a, f.a, f.a, f.a, f.a, f.a,
                f.a, f.a, f.a, f.a, f.a, f.a, f.a, f.a},
            17u, &result),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(
        phy_color_structure_constant_component(
            f.color, 1u, 2u, 3u, &result),
        PHY_ERR_UNSUPPORTED);
    fixture_close(&f);
}

static void test_adjoint_casimir_presentation(void)
{
    fixture f = fixture_open_symbolic();
    phy_ir_ref expression = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_adjoint_casimir(f.color, f.a, f.b, &expression),
        PHY_OK);
    char text[512];
    size_t needed = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, expression, text, sizeof text, &needed), PHY_OK);
    PHY_CHECK(strstr(text, "C_A") != NULL);
    PHY_CHECK(strstr(text, "SUNDelta") != NULL);

    phy_ir_ref expanded = PHY_IR_NULL;
    phy_ir_ref delta = PHY_IR_NULL;
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_color_expand_casimirs(f.color, expression, &expanded), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_color_delta(f.color, f.a, f.b, &delta), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_mul(
            f.cas, (const phy_ir_ref[2]){f.n, delta}, 2u, &expected),
        PHY_OK);
    expect_equivalent(&f, expanded, expected);
    fixture_close(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_symbolic_dimension_and_casimirs);
    PHY_TEST_CASE(test_invariant_tensor_symmetries);
    PHY_TEST_CASE(test_delta_contraction);
    PHY_TEST_CASE(test_traces_and_commutator);
    PHY_TEST_CASE(test_exact_su2_su3_components);
    PHY_TEST_CASE(test_index_spaces_and_errors_are_typed);
    PHY_TEST_CASE(test_adjoint_casimir_presentation);
    return PHY_TEST_REPORT("test_color");
}
