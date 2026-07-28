/*
 * Bounded exact truncated Laurent-series substrate.
 *
 * This suite tests the algebra below the reader-facing Series/Limit commands.
 * The substrate is intentionally over Q: exact symbolic coefficients will be
 * added only with a coefficient-domain abstraction, never by silently mixing
 * unevaluated IR into arithmetic that currently proves exactness.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "phy/cas.h"
#include "phy/ir.h"
#include "phy_test.h"
#include "series_internal.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_ir_ref x;
    phy_ir_ref y;
    phy_ir_ref zero;
    phy_ir_ref one;
} fixture;

static fixture open_fixture_with_limits(const phy_cas_limits *limits)
{
    fixture f;
    memset(&f, 0, sizeof f);
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, limits);
    PHY_CHECK(f.cas != NULL);
    f.x = phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "x"));
    f.y = phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "y"));
    f.zero = phy_ir_integer(f.ir, 0);
    f.one = phy_ir_integer(f.ir, 1);
    PHY_CHECK(f.x != PHY_IR_NULL);
    PHY_CHECK(f.y != PHY_IR_NULL);
    PHY_CHECK(f.zero != PHY_IR_NULL);
    PHY_CHECK(f.one != PHY_IR_NULL);
    return f;
}

static fixture open_fixture(void)
{
    return open_fixture_with_limits(NULL);
}

static void close_fixture(fixture *f)
{
    PHY_CHECK_EQ_INT(phy_cas_validate(f->cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(f->ir), PHY_OK);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static phy_ir_ref exact(fixture *f, int numerator, int denominator)
{
    const phy_ir_ref ref =
        phy_ir_rational(f->ir, numerator, denominator);
    PHY_CHECK(ref != PHY_IR_NULL);
    return ref;
}

static void set_integer_series(fixture *f, int valuation, int order,
                               const int *values, phy_series *out)
{
    phy_ir_ref coefficients[PHY_SERIES_MAX_TERMS];
    const size_t count = (size_t)(order - valuation);
    PHY_CHECK(count <= PHY_SERIES_MAX_TERMS);
    for (size_t index = 0u; index < count; ++index) {
        coefficients[index] = phy_ir_integer(f->ir, values[index]);
        PHY_CHECK(coefficients[index] != PHY_IR_NULL);
    }
    PHY_CHECK_EQ_INT(
        phy_series_set(f->cas, f->x, f->zero, valuation, order,
                       coefficients, count, out),
        PHY_OK);
}

static const char *render_ref(fixture *f, phy_ir_ref ref)
{
    static char text[512];
    size_t length = 0u;
    if (phy_ir_write(f->ir, ref, text, sizeof text, &length) != PHY_OK) {
        return "<write failed>";
    }
    return text;
}

static void check_coefficient(fixture *f, const phy_series *series,
                              int exponent, const char *expected)
{
    PHY_CHECK_EQ_STR(
        render_ref(
            f, phy_series_coefficient(f->cas, series, exponent)),
        expected);
}

static void test_storage_and_validation(void)
{
    fixture f = open_fixture();
    const int raw[] = {0, 0, 3, 0};
    phy_series series;
    set_integer_series(&f, -1, 3, raw, &series);
    PHY_CHECK_EQ_INT(series.valuation, 1);
    PHY_CHECK_EQ_INT(series.order, 3);
    PHY_CHECK_EQ_INT(series.count, 2);
    check_coefficient(&f, &series, 0, "0");
    check_coefficient(&f, &series, 1, "3");
    check_coefficient(&f, &series, 2, "0");
    PHY_CHECK_EQ_INT(phy_series_validate(f.cas, &series), PHY_OK);
    PHY_CHECK(!phy_series_is_zero(&series));

    const int zeros[] = {0, 0, 0};
    set_integer_series(&f, 0, 3, zeros, &series);
    PHY_CHECK(phy_series_is_zero(&series));
    PHY_CHECK_EQ_INT(series.valuation, 3);
    PHY_CHECK_EQ_INT(series.count, 0);

    phy_series untouched = series;
    const phy_ir_ref bad[] = {f.one};
    PHY_CHECK_EQ_INT(
        phy_series_set(f.cas, f.one, f.zero, 0, 1, bad, 1u,
                       &untouched),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_series_set(f.cas, f.x, f.x, 0, 1, bad, 1u, &untouched),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_series_set(f.cas, f.x, f.zero, 2, 1, NULL, 0u,
                       &untouched),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_series_set(f.cas, f.x, f.zero, 0, 2, bad, 1u,
                       &untouched),
        PHY_ERR_INVALID_ARGUMENT);

    close_fixture(&f);
}

static void test_add_subtract_and_aliasing(void)
{
    fixture f = open_fixture();
    const int a_values[] = {1, 2, 3, 4};
    const int b_values[] = {-1, 1, 0, 5};
    phy_series a, b, sum, difference;
    set_integer_series(&f, -1, 3, a_values, &a);
    set_integer_series(&f, -1, 3, b_values, &b);

    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(phy_series_add_node(f.cas, &a, &b, &sum),
                     PHY_OK);
    PHY_CHECK_EQ_INT(sum.valuation, 0);
    check_coefficient(&f, &sum, -1, "0");
    check_coefficient(&f, &sum, 0, "3");
    check_coefficient(&f, &sum, 1, "3");
    check_coefficient(&f, &sum, 2, "9");

    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_sub_node(f.cas, &sum, &b, &difference), PHY_OK);
    for (int exponent = -1; exponent < 3; ++exponent) {
        check_coefficient(
            &f, &difference, exponent,
            exponent == -1 ? "1"
                           : exponent == 0 ? "2"
                                           : exponent == 1 ? "3" : "4");
    }

    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(phy_series_add_node(f.cas, &a, &b, &a), PHY_OK);
    PHY_CHECK_EQ_INT(a.valuation, sum.valuation);
    for (int exponent = -1; exponent < 3; ++exponent) {
        PHY_CHECK(
            phy_series_coefficient(f.cas, &a, exponent) ==
            phy_series_coefficient(f.cas, &sum, exponent));
    }

    phy_series other_ring = b;
    other_ring.variable = f.y;
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_add_node(f.cas, &a, &other_ring, &difference),
        PHY_ERR_TYPE);

    close_fixture(&f);
}

static void test_multiply_divide_and_reciprocal(void)
{
    fixture f = open_fixture();
    const int a_values[] = {1, 2, 3, 4};
    const int b_values[] = {2, -1, 1, 0};
    phy_series a, b, product;
    set_integer_series(&f, 0, 4, a_values, &a);
    set_integer_series(&f, 0, 4, b_values, &b);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_mul_node(f.cas, &a, &b, &product), PHY_OK);
    PHY_CHECK_EQ_INT(product.order, 4);
    check_coefficient(&f, &product, 0, "2");
    check_coefficient(&f, &product, 1, "3");
    check_coefficient(&f, &product, 2, "5");
    check_coefficient(&f, &product, 3, "7");

    const int one_plus_x[] = {1, 1, 0, 0, 0};
    phy_series divisor, reciprocal, quotient;
    set_integer_series(&f, 0, 5, one_plus_x, &divisor);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_reciprocal_node(f.cas, &divisor, &reciprocal),
        PHY_OK);
    for (int exponent = 0; exponent < 5; ++exponent) {
        check_coefficient(
            &f, &reciprocal, exponent,
            (exponent & 1) == 0 ? "1" : "-1");
    }

    const int numerator_values[] = {1, 0, 0, 0, 0};
    phy_series numerator;
    set_integer_series(&f, 0, 5, numerator_values, &numerator);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_div_node(f.cas, &numerator, &divisor, &quotient),
        PHY_OK);
    for (int exponent = 0; exponent < 5; ++exponent) {
        PHY_CHECK(
            phy_series_coefficient(f.cas, &quotient, exponent) ==
            phy_series_coefficient(f.cas, &reciprocal, exponent));
    }

    const int laurent_values[] = {1, 1, 0, 0};
    phy_series laurent;
    set_integer_series(&f, 2, 6, laurent_values, &laurent);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_reciprocal_node(f.cas, &laurent, &reciprocal),
        PHY_OK);
    PHY_CHECK_EQ_INT(reciprocal.valuation, -2);
    PHY_CHECK_EQ_INT(reciprocal.order, 2);
    for (int exponent = -2; exponent < 2; ++exponent) {
        check_coefficient(
            &f, &reciprocal, exponent,
            ((exponent + 2) & 1) == 0 ? "1" : "-1");
    }

    phy_series zero;
    PHY_CHECK_EQ_INT(
        phy_series_zero(f.cas, f.x, f.zero, 4, &zero), PHY_OK);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_reciprocal_node(f.cas, &zero, &reciprocal),
        PHY_ERR_DOMAIN);

    close_fixture(&f);
}

static void test_integer_power(void)
{
    fixture f = open_fixture();
    const int values[] = {1, 1, 0, 0, 0};
    phy_series base, power;
    set_integer_series(&f, 0, 5, values, &base);

    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_pow_int_node(f.cas, &base, 3, &power), PHY_OK);
    check_coefficient(&f, &power, 0, "1");
    check_coefficient(&f, &power, 1, "3");
    check_coefficient(&f, &power, 2, "3");
    check_coefficient(&f, &power, 3, "1");
    check_coefficient(&f, &power, 4, "0");

    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_pow_int_node(f.cas, &base, -1, &power), PHY_OK);
    for (int exponent = 0; exponent < 5; ++exponent) {
        check_coefficient(
            &f, &power, exponent,
            (exponent & 1) == 0 ? "1" : "-1");
    }

    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_pow_int_node(f.cas, &base, 0, &power), PHY_OK);
    check_coefficient(&f, &power, 0, "1");
    check_coefficient(&f, &power, 1, "0");

    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_pow_int_node(f.cas, &base, INT_MIN, &power),
        PHY_ERR_TERM_LIMIT);

    close_fixture(&f);
}

static void test_derivative_and_integral(void)
{
    fixture f = open_fixture();
    const int values[] = {2, 3, 4, 5};
    phy_series series, derivative;
    set_integer_series(&f, -1, 3, values, &series);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_derivative_node(f.cas, &series, &derivative),
        PHY_OK);
    PHY_CHECK_EQ_INT(derivative.valuation, -2);
    PHY_CHECK_EQ_INT(derivative.order, 2);
    check_coefficient(&f, &derivative, -2, "-2");
    check_coefficient(&f, &derivative, -1, "0");
    check_coefficient(&f, &derivative, 0, "4");
    check_coefficient(&f, &derivative, 1, "10");

    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_integral_node(f.cas, &series, &derivative),
        PHY_ERR_DOMAIN);

    const int integrable_values[] = {2, 0, 3, 4, 5};
    set_integer_series(&f, -2, 3, integrable_values, &series);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_integral_node(f.cas, &series, &derivative),
        PHY_OK);
    check_coefficient(&f, &derivative, -1, "-2");
    check_coefficient(&f, &derivative, 0, "0");
    check_coefficient(&f, &derivative, 1, "3");
    check_coefficient(&f, &derivative, 2, "2");
    check_coefficient(&f, &derivative, 3, "(rat 5 3)");

    phy_series roundtrip;
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_derivative_node(f.cas, &derivative, &roundtrip),
        PHY_OK);
    for (int exponent = -2; exponent < 3; ++exponent) {
        PHY_CHECK(
            phy_series_coefficient(f.cas, &roundtrip, exponent) ==
            phy_series_coefficient(f.cas, &series, exponent));
    }

    close_fixture(&f);
}

static void test_composition(void)
{
    fixture f = open_fixture();
    phy_ir_ref outer_coefficients[] = {
        exact(&f, 1, 1), exact(&f, 1, 1), exact(&f, 1, 2),
        exact(&f, 1, 6), exact(&f, 1, 24)};
    phy_series outer;
    PHY_CHECK_EQ_INT(
        phy_series_set(f.cas, f.x, f.zero, 0, 5,
                       outer_coefficients, 5u, &outer),
        PHY_OK);
    const int inner_values[] = {1, 1, 0, 0};
    phy_series inner, composition;
    set_integer_series(&f, 1, 5, inner_values, &inner);

    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_compose_node(f.cas, &outer, &inner, &composition),
        PHY_OK);
    check_coefficient(&f, &composition, 0, "1");
    check_coefficient(&f, &composition, 1, "1");
    check_coefficient(&f, &composition, 2, "(rat 3 2)");
    check_coefficient(&f, &composition, 3, "(rat 7 6)");
    check_coefficient(&f, &composition, 4, "(rat 25 24)");

    const int bad_inner_values[] = {1, 1, 0};
    set_integer_series(&f, 0, 3, bad_inner_values, &inner);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_compose_node(f.cas, &outer, &inner, &composition),
        PHY_ERR_DOMAIN);

    close_fixture(&f);
}

static void test_promoted_exact_coefficients(void)
{
    fixture f = open_fixture();
    const char *wide_text =
        "1267650600228229401496703205376"; /* 2^100 */
    const phy_ir_ref wide = phy_ir_integer_text(f.ir, wide_text);
    PHY_CHECK(wide != PHY_IR_NULL);
    const phy_ir_ref coefficients[] = {wide, f.one, f.zero};
    phy_series series, doubled;
    PHY_CHECK_EQ_INT(
        phy_series_set(f.cas, f.x, f.zero, 0, 3, coefficients, 3u,
                       &series),
        PHY_OK);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_add_node(f.cas, &series, &series, &doubled), PHY_OK);
    check_coefficient(
        &f, &doubled, 0, "2535301200456458802993406410752");
    check_coefficient(&f, &doubled, 1, "2");

    close_fixture(&f);
}

static int decode_coefficient(unsigned encoded, int index)
{
    for (int skipped = 0; skipped < index; ++skipped) {
        encoded /= 3u;
    }
    return (int)(encoded % 3u) - 1;
}

static void test_exhaustive_small_ring_identities(void)
{
    fixture f = open_fixture();
    for (unsigned encoded_a = 0u; encoded_a < 27u; ++encoded_a) {
        int a_values[3];
        for (int index = 0; index < 3; ++index) {
            a_values[index] = decode_coefficient(encoded_a, index);
        }
        phy_series a;
        set_integer_series(&f, 0, 3, a_values, &a);
        for (unsigned encoded_b = 0u; encoded_b < 27u; ++encoded_b) {
            int b_values[3];
            for (int index = 0; index < 3; ++index) {
                b_values[index] =
                    decode_coefficient(encoded_b, index);
            }
            phy_series b, sum, difference, product;
            set_integer_series(&f, 0, 3, b_values, &b);

            phy_cas_begin(f.cas);
            PHY_CHECK_EQ_INT(
                phy_series_add_node(f.cas, &a, &b, &sum), PHY_OK);
            phy_cas_begin(f.cas);
            PHY_CHECK_EQ_INT(
                phy_series_sub_node(f.cas, &sum, &b, &difference),
                PHY_OK);
            for (int exponent = 0; exponent < 3; ++exponent) {
                PHY_CHECK(
                    phy_series_coefficient(f.cas, &difference, exponent) ==
                    phy_series_coefficient(f.cas, &a, exponent));
            }

            phy_cas_begin(f.cas);
            PHY_CHECK_EQ_INT(
                phy_series_mul_node(f.cas, &a, &b, &product), PHY_OK);
            for (int exponent = 0; exponent < product.order;
                 ++exponent) {
                int expected = 0;
                for (int left_exp = 0; left_exp <= exponent;
                     ++left_exp) {
                    const int right_exp = exponent - left_exp;
                    if (left_exp < 3 && right_exp < 3) {
                        expected +=
                            a_values[left_exp] * b_values[right_exp];
                    }
                }
                int64_t actual = 0;
                PHY_CHECK(phy_ir_integer_value(
                    f.ir,
                    phy_series_coefficient(
                        f.cas, &product, exponent),
                    &actual));
                PHY_CHECK_EQ_INT(actual, expected);
            }
        }
    }
    close_fixture(&f);
}

static void test_step_budget(void)
{
    phy_cas_limits limits;
    memset(&limits, 0, sizeof limits);
    limits.max_steps = 1u;
    fixture f = open_fixture_with_limits(&limits);
    const int values[] = {1, 2, 3, 4};
    phy_series a, out;
    set_integer_series(&f, 0, 4, values, &a);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_add_node(f.cas, &a, &a, &out), PHY_ERR_TIMEOUT);
    PHY_CHECK_EQ_INT(phy_cas_steps(f.cas), 1);
    close_fixture(&f);
}

static bool cancel_immediately(void *user)
{
    unsigned *polls = (unsigned *)user;
    (*polls)++;
    return true;
}

static void test_cancellation_is_transactional(void)
{
    fixture f = open_fixture();
    const int values[] = {7, 11, 13, 17};
    phy_series a;
    set_integer_series(&f, 0, 4, values, &a);
    phy_series out = a;
    unsigned polls = 0u;
    phy_cas_set_cancel(f.cas, cancel_immediately, &polls);
    phy_cas_begin(f.cas);
    PHY_CHECK_EQ_INT(
        phy_series_mul_node(f.cas, &a, &a, &out),
        PHY_ERR_INTERRUPTED);
    PHY_CHECK(polls > 0u);
    PHY_CHECK_EQ_INT(out.valuation, a.valuation);
    PHY_CHECK_EQ_INT(out.order, a.order);
    for (int exponent = 0; exponent < 4; ++exponent) {
        PHY_CHECK(
            phy_series_coefficient(f.cas, &out, exponent) ==
            phy_series_coefficient(f.cas, &a, exponent));
    }
    phy_cas_set_cancel(f.cas, NULL, NULL);
    close_fixture(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_storage_and_validation);
    PHY_TEST_CASE(test_add_subtract_and_aliasing);
    PHY_TEST_CASE(test_multiply_divide_and_reciprocal);
    PHY_TEST_CASE(test_integer_power);
    PHY_TEST_CASE(test_derivative_and_integral);
    PHY_TEST_CASE(test_composition);
    PHY_TEST_CASE(test_promoted_exact_coefficients);
    PHY_TEST_CASE(test_exhaustive_small_ring_identities);
    PHY_TEST_CASE(test_step_budget);
    PHY_TEST_CASE(test_cancellation_is_transactional);
    return PHY_TEST_REPORT("test_series");
}
