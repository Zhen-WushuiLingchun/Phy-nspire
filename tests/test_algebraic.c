/*
 * Certified real-algebraic foundation: Sturm counts, primitive defining
 * polynomials, isolating intervals, exact refinement, and safe comparison.
 */
#include <string.h>

#include "phy/algebraic.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy_test.h"

typedef struct {
    phy_algebraic_context *algebraic;
} fixture;

static phy_exact_rational_text rational(const char *numerator,
                                        const char *denominator)
{
    phy_exact_rational_text value = {numerator, denominator};
    return value;
}

static fixture fixture_open(void)
{
    fixture result;
    result.algebraic = NULL;
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    result.algebraic = phy_algebraic_context_create(NULL);
    PHY_CHECK(result.algebraic != NULL);
    return result;
}

static void fixture_close(fixture *value)
{
    phy_algebraic_context_destroy(value->algebraic);
    value->algebraic = NULL;
    phy_platform_shutdown();
}

static const char *lower_text(const phy_real_algebraic *value)
{
    static char buffers[4][4096];
    static unsigned next;
    char *buffer = buffers[next++ & 3u];
    size_t required = 0u;
    const phy_status status = phy_real_algebraic_write_lower(
        value, buffer, sizeof buffers[0], &required);
    return status == PHY_OK ? buffer : "<write failed>";
}

static const char *upper_text(const phy_real_algebraic *value)
{
    static char buffers[4][4096];
    static unsigned next;
    char *buffer = buffers[next++ & 3u];
    size_t required = 0u;
    const phy_status status = phy_real_algebraic_write_upper(
        value, buffer, sizeof buffers[0], &required);
    return status == PHY_OK ? buffer : "<write failed>";
}

static const char *coefficient_text(const phy_real_algebraic *value,
                                    size_t degree)
{
    static char buffers[4][4096];
    static unsigned next;
    char *buffer = buffers[next++ & 3u];
    size_t required = 0u;
    const phy_status status = phy_real_algebraic_write_coefficient(
        value, degree, buffer, sizeof buffers[0], &required);
    return status == PHY_OK ? buffer : "<write failed>";
}

static void test_context_lifecycle(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_algebraic_limits limits;
    phy_algebraic_limits_defaults(&limits);
    PHY_CHECK(limits.max_degree >= 16u);
    PHY_CHECK(limits.max_steps > 0u);
    PHY_CHECK(limits.max_refinements > 0u);
    PHY_CHECK(limits.max_metadata_bytes > 0u);

    phy_algebraic_context *context =
        phy_algebraic_context_create(&limits);
    PHY_CHECK(context != NULL);
    PHY_CHECK(phy_algebraic_metadata_bytes(context) > 0u);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);
    phy_algebraic_context_destroy(context);
    phy_algebraic_context_destroy(NULL);

    limits.max_metadata_bytes = 1u;
    PHY_CHECK(phy_algebraic_context_create(&limits) == NULL);
    phy_platform_shutdown();
}

static void test_sturm_root_counts(void)
{
    fixture f = fixture_open();
    uint32_t roots = 99u;

    static const char *quadratic[] = {"-2", "0", "1"};
    PHY_CHECK_EQ_INT(
        phy_algebraic_count_real_roots(
            f.algebraic, quadratic, 3u, rational("-2", "1"),
            rational("2", "1"), &roots),
        PHY_OK);
    PHY_CHECK_EQ_INT(roots, 2);
    PHY_CHECK_EQ_INT(
        phy_algebraic_count_real_roots(
            f.algebraic, quadratic, 3u, rational("1", "1"),
            rational("2", "1"), &roots),
        PHY_OK);
    PHY_CHECK_EQ_INT(roots, 1);

    static const char *cubic[] = {"0", "-1", "0", "1"};
    PHY_CHECK_EQ_INT(
        phy_algebraic_count_real_roots(
            f.algebraic, cubic, 4u, rational("-2", "1"),
            rational("2", "1"), &roots),
        PHY_OK);
    PHY_CHECK_EQ_INT(roots, 3);

    static const char *no_real[] = {"1", "0", "1"};
    PHY_CHECK_EQ_INT(
        phy_algebraic_count_real_roots(
            f.algebraic, no_real, 3u, rational("-10", "1"),
            rational("10", "1"), &roots),
        PHY_OK);
    PHY_CHECK_EQ_INT(roots, 0);

    /* (x - 1)^2 is not a valid square-free defining polynomial. */
    static const char *repeated[] = {"1", "-2", "1"};
    roots = 77u;
    PHY_CHECK_EQ_INT(
        phy_algebraic_count_real_roots(
            f.algebraic, repeated, 3u, rational("0", "1"),
            rational("2", "1"), &roots),
        PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(roots, 77);

    /* Sturm's open-interval contract rejects an endpoint root. */
    static const char *endpoint[] = {"-1", "0", "1"};
    PHY_CHECK_EQ_INT(
        phy_algebraic_count_real_roots(
            f.algebraic, endpoint, 3u, rational("1", "1"),
            rational("2", "1"), &roots),
        PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);
    fixture_close(&f);
}

static void test_create_normalizes_and_certifies(void)
{
    fixture f = fixture_open();
    /* 4 - 2*x^2 normalizes to x^2 - 2. */
    static const char *scaled[] = {"4", "0", "-2", "0"};
    phy_real_algebraic *root = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, scaled, 4u, rational("1", "1"),
            rational("2", "1"), &root),
        PHY_OK);
    PHY_CHECK(root != NULL);
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(root), 2);
    PHY_CHECK_EQ_STR(coefficient_text(root, 0u), "-2");
    PHY_CHECK_EQ_STR(coefficient_text(root, 1u), "0");
    PHY_CHECK_EQ_STR(coefficient_text(root, 2u), "1");
    PHY_CHECK_EQ_STR(lower_text(root), "1");
    PHY_CHECK_EQ_STR(upper_text(root), "2");
    PHY_CHECK(!phy_real_algebraic_is_rational(root));
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(root), PHY_OK);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);

    phy_real_algebraic *rejected = root;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, scaled, 4u, rational("-1", "1"),
            rational("1", "1"), &rejected),
        PHY_ERR_DOMAIN);
    PHY_CHECK(rejected == NULL);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);

    phy_real_algebraic_destroy(root);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);
    fixture_close(&f);
}

static void test_all_real_roots_are_isolated_in_order(void)
{
    fixture f = fixture_open();
    static const char *cubic[] = {"0", "-1", "0", "1"};
    phy_real_algebraic *roots[3] = {NULL, NULL, NULL};
    size_t count = 99u;
    PHY_CHECK_EQ_INT(
        phy_algebraic_isolate_real_roots(
            f.algebraic, cubic, 4u, roots, 3u, &count),
        PHY_OK);
    PHY_CHECK_EQ_INT(count, 3);
    for (size_t index = 0u; index < count; ++index) {
        PHY_CHECK(roots[index] != NULL);
        PHY_CHECK_EQ_INT(
            phy_real_algebraic_validate(roots[index]), PHY_OK);
        PHY_CHECK_EQ_INT(phy_real_algebraic_degree(roots[index]), 3);
    }
    int comparison = 0;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_compare(
            roots[0], roots[1], &comparison),
        PHY_OK);
    PHY_CHECK_EQ_INT(comparison, -1);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_compare(
            roots[1], roots[2], &comparison),
        PHY_OK);
    PHY_CHECK_EQ_INT(comparison, -1);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);

    for (size_t index = 0u; index < count; ++index) {
        phy_real_algebraic_destroy(roots[index]);
    }

    static const char *quintic[] = {"-1", "-1", "0", "0", "0", "1"};
    phy_real_algebraic *quintic_roots[5] = {
        NULL, NULL, NULL, NULL, NULL};
    count = 99u;
    PHY_CHECK_EQ_INT(
        phy_algebraic_isolate_real_roots(
            f.algebraic, quintic, 6u, quintic_roots, 5u, &count),
        PHY_OK);
    PHY_CHECK_EQ_INT(count, 1);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_degree(quintic_roots[0]), 5);
    PHY_CHECK_EQ_STR(
        coefficient_text(quintic_roots[0], 0u), "-1");
    PHY_CHECK_EQ_STR(
        coefficient_text(quintic_roots[0], 5u), "1");
    phy_real_algebraic_destroy(quintic_roots[0]);

    static const char *no_real[] = {"1", "0", "1"};
    phy_real_algebraic *none[2] = {roots[0], roots[1]};
    count = 99u;
    PHY_CHECK_EQ_INT(
        phy_algebraic_isolate_real_roots(
            f.algebraic, no_real, 3u, none, 2u, &count),
        PHY_OK);
    PHY_CHECK_EQ_INT(count, 0);
    PHY_CHECK(none[0] == NULL);
    PHY_CHECK(none[1] == NULL);

    phy_real_algebraic *too_small[1] = {roots[0]};
    count = 99u;
    PHY_CHECK_EQ_INT(
        phy_algebraic_isolate_real_roots(
            f.algebraic, cubic, 4u, too_small, 1u, &count),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK(too_small[0] == NULL);
    PHY_CHECK_EQ_INT(count, 99);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);
    fixture_close(&f);
}

static void test_refine_and_safe_compare(void)
{
    fixture f = fixture_open();
    static const char *sqrt2_polynomial[] = {"-2", "0", "1"};
    static const char *sqrt3_polynomial[] = {"-3", "0", "1"};
    phy_real_algebraic *sqrt2 = NULL;
    phy_real_algebraic *sqrt2_again = NULL;
    phy_real_algebraic *sqrt3 = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, sqrt2_polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &sqrt2),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, sqrt2_polynomial, 3u, rational("7", "5"),
            rational("3", "2"), &sqrt2_again),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, sqrt3_polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &sqrt3),
        PHY_OK);

    int comparison = 9;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_compare(
            sqrt2, sqrt2_again, &comparison),
        PHY_OK);
    PHY_CHECK_EQ_INT(comparison, 0);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_compare(sqrt2, sqrt3, &comparison),
        PHY_OK);
    PHY_CHECK_EQ_INT(comparison, -1);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_compare(sqrt3, sqrt2, &comparison),
        PHY_OK);
    PHY_CHECK_EQ_INT(comparison, 1);

    PHY_CHECK_EQ_INT(phy_real_algebraic_refine(sqrt2, 12u), PHY_OK);
    PHY_CHECK(strcmp(lower_text(sqrt2), "1") != 0);
    PHY_CHECK(strcmp(upper_text(sqrt2), "2") != 0);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(sqrt2), PHY_OK);

    static const char *linear[] = {"-1", "1"};
    phy_real_algebraic *one = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, linear, 2u, rational("0", "1"),
            rational("2", "1"), &one),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_refine(one, 1u), PHY_OK);
    PHY_CHECK(phy_real_algebraic_is_rational(one));
    PHY_CHECK_EQ_STR(lower_text(one), "1");
    PHY_CHECK_EQ_STR(upper_text(one), "1");
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(one), PHY_OK);

    phy_real_algebraic *one_interval = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, linear, 2u, rational("1", "2"),
            rational("3", "2"), &one_interval),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_compare(one, one_interval, &comparison),
        PHY_OK);
    PHY_CHECK_EQ_INT(comparison, 0);

    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);
    fixture_close(&f);
}

static void test_arbitrary_precision_coefficients_and_intervals(void)
{
    fixture f = fixture_open();
    /* x^2 - 2^128, whose positive root is the exact integer 2^64. */
    static const char *polynomial[] = {
        "-340282366920938463463374607431768211456", "0", "1"};
    phy_real_algebraic *root = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, polynomial, 3u,
            rational("18446744073709551615", "1"),
            rational("18446744073709551617", "1"), &root),
        PHY_OK);
    PHY_CHECK_EQ_STR(
        coefficient_text(root, 0u),
        "-340282366920938463463374607431768211456");
    PHY_CHECK_EQ_INT(phy_real_algebraic_refine(root, 1u), PHY_OK);
    PHY_CHECK(phy_real_algebraic_is_rational(root));
    PHY_CHECK_EQ_STR(lower_text(root), "18446744073709551616");
    PHY_CHECK_EQ_STR(upper_text(root), "18446744073709551616");
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);
    fixture_close(&f);
}

static void test_exact_rational_transforms(void)
{
    fixture f = fixture_open();
    static const char *sqrt2_polynomial[] = {"-2", "0", "1"};
    phy_real_algebraic *sqrt2 = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, sqrt2_polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &sqrt2),
        PHY_OK);

    phy_real_algebraic *translated = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_translate_rational(
            sqrt2, rational("1", "2"), &translated),
        PHY_OK);
    PHY_CHECK_EQ_STR(coefficient_text(translated, 0u), "-7");
    PHY_CHECK_EQ_STR(coefficient_text(translated, 1u), "-4");
    PHY_CHECK_EQ_STR(coefficient_text(translated, 2u), "4");
    PHY_CHECK_EQ_STR(lower_text(translated), "3/2");
    PHY_CHECK_EQ_STR(upper_text(translated), "5/2");

    phy_real_algebraic *scaled = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_scale_rational(
            sqrt2, rational("-3", "2"), &scaled),
        PHY_OK);
    PHY_CHECK_EQ_STR(coefficient_text(scaled, 0u), "-9");
    PHY_CHECK_EQ_STR(coefficient_text(scaled, 1u), "0");
    PHY_CHECK_EQ_STR(coefficient_text(scaled, 2u), "2");
    PHY_CHECK_EQ_STR(lower_text(scaled), "-3");
    PHY_CHECK_EQ_STR(upper_text(scaled), "-3/2");

    phy_real_algebraic *inverse = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_reciprocal(sqrt2, &inverse), PHY_OK);
    PHY_CHECK_EQ_STR(coefficient_text(inverse, 0u), "-1");
    PHY_CHECK_EQ_STR(coefficient_text(inverse, 1u), "0");
    PHY_CHECK_EQ_STR(coefficient_text(inverse, 2u), "2");
    PHY_CHECK_EQ_STR(lower_text(inverse), "1/2");
    PHY_CHECK_EQ_STR(upper_text(inverse), "1");

    phy_real_algebraic *zero = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_scale_rational(
            sqrt2, rational("0", "17"), &zero),
        PHY_OK);
    PHY_CHECK(phy_real_algebraic_is_rational(zero));
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(zero), 1);
    PHY_CHECK_EQ_STR(coefficient_text(zero, 0u), "0");
    PHY_CHECK_EQ_STR(coefficient_text(zero, 1u), "1");
    PHY_CHECK_EQ_STR(lower_text(zero), "0");
    PHY_CHECK_EQ_STR(upper_text(zero), "0");

    phy_real_algebraic *huge = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_translate_rational(
            sqrt2, rational("18446744073709551616", "1"), &huge),
        PHY_OK);
    PHY_CHECK_EQ_STR(
        coefficient_text(huge, 0u),
        "340282366920938463463374607431768211454");
    PHY_CHECK_EQ_STR(
        coefficient_text(huge, 1u), "-36893488147419103232");
    PHY_CHECK_EQ_STR(coefficient_text(huge, 2u), "1");
    PHY_CHECK_EQ_STR(lower_text(huge), "18446744073709551617");
    PHY_CHECK_EQ_STR(upper_text(huge), "18446744073709551618");

    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(translated), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(scaled), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(inverse), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(zero), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(huge), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(sqrt2), PHY_OK);
    PHY_CHECK_EQ_STR(lower_text(sqrt2), "1");
    PHY_CHECK_EQ_STR(upper_text(sqrt2), "2");
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);

    phy_real_algebraic_destroy(huge);
    phy_real_algebraic_destroy(zero);
    phy_real_algebraic_destroy(inverse);
    phy_real_algebraic_destroy(scaled);
    phy_real_algebraic_destroy(translated);
    phy_real_algebraic_destroy(sqrt2);
    fixture_close(&f);
}

static void test_rational_transform_edge_cases(void)
{
    fixture f = fixture_open();
    static const char *one_polynomial[] = {"-1", "1"};
    phy_real_algebraic *one = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, one_polynomial, 2u, rational("0", "1"),
            rational("2", "1"), &one),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_refine(one, 1u), PHY_OK);
    PHY_CHECK(phy_real_algebraic_is_rational(one));

    phy_real_algebraic *negative_two = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_scale_rational(
            one, rational("-2", "1"), &negative_two),
        PHY_OK);
    PHY_CHECK(phy_real_algebraic_is_rational(negative_two));
    PHY_CHECK_EQ_STR(coefficient_text(negative_two, 0u), "2");
    PHY_CHECK_EQ_STR(coefficient_text(negative_two, 1u), "1");
    PHY_CHECK_EQ_STR(lower_text(negative_two), "-2");
    PHY_CHECK_EQ_STR(upper_text(negative_two), "-2");

    phy_real_algebraic *one_inverse = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_reciprocal(one, &one_inverse), PHY_OK);
    PHY_CHECK(phy_real_algebraic_is_rational(one_inverse));
    PHY_CHECK_EQ_STR(coefficient_text(one_inverse, 0u), "-1");
    PHY_CHECK_EQ_STR(coefficient_text(one_inverse, 1u), "1");

    static const char *zero_polynomial[] = {"0", "1"};
    phy_real_algebraic *zero = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, zero_polynomial, 2u, rational("-1", "1"),
            rational("1", "1"), &zero),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_refine(zero, 1u), PHY_OK);
    PHY_CHECK(phy_real_algebraic_is_rational(zero));
    phy_real_algebraic *rejected = one;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_reciprocal(zero, &rejected),
        PHY_ERR_DOMAIN);
    PHY_CHECK(rejected == NULL);

    rejected = one;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_translate_rational(
            one, rational("1", "0"), &rejected),
        PHY_ERR_DOMAIN);
    PHY_CHECK(rejected == NULL);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(one), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(zero), PHY_OK);

    static const char *sqrt2_polynomial[] = {"-2", "0", "1"};
    phy_real_algebraic *wide_sqrt2 = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, sqrt2_polynomial, 3u, rational("-1", "1"),
            rational("2", "1"), &wide_sqrt2),
        PHY_OK);
    phy_real_algebraic *wide_inverse = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_reciprocal(
            wide_sqrt2, &wide_inverse),
        PHY_OK);
    PHY_CHECK_EQ_STR(lower_text(wide_inverse), "1/2");
    PHY_CHECK_EQ_STR(upper_text(wide_inverse), "2");
    PHY_CHECK_EQ_STR(lower_text(wide_sqrt2), "-1");
    PHY_CHECK_EQ_STR(upper_text(wide_sqrt2), "2");
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_validate(wide_sqrt2), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_validate(wide_inverse), PHY_OK);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);

    phy_real_algebraic_destroy(wide_inverse);
    phy_real_algebraic_destroy(wide_sqrt2);
    phy_real_algebraic_destroy(zero);
    phy_real_algebraic_destroy(one_inverse);
    phy_real_algebraic_destroy(negative_two);
    phy_real_algebraic_destroy(one);
    fixture_close(&f);
}

static bool cancel_now(void *user);

static void test_resultant_arithmetic_closure(void)
{
    fixture f = fixture_open();
    static const char *sqrt2_polynomial[] = {"-2", "0", "1"};
    static const char *sqrt3_polynomial[] = {"-3", "0", "1"};
    static const char *cbrt2_polynomial[] = {"-2", "0", "0", "1"};
    phy_real_algebraic *sqrt2 = NULL;
    phy_real_algebraic *sqrt3 = NULL;
    phy_real_algebraic *cbrt2 = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, sqrt2_polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &sqrt2),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, sqrt3_polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &sqrt3),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, cbrt2_polynomial, 4u, rational("1", "1"),
            rational("2", "1"), &cbrt2),
        PHY_OK);

    phy_real_algebraic *sum = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_add(sqrt2, sqrt3, &sum), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(sum), 4);
    PHY_CHECK_EQ_STR(coefficient_text(sum, 0u), "1");
    PHY_CHECK_EQ_STR(coefficient_text(sum, 1u), "0");
    PHY_CHECK_EQ_STR(coefficient_text(sum, 2u), "-10");
    PHY_CHECK_EQ_STR(coefficient_text(sum, 3u), "0");
    PHY_CHECK_EQ_STR(coefficient_text(sum, 4u), "1");

    phy_real_algebraic *difference = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_subtract(sqrt2, sqrt3, &difference), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(difference), 4);
    int order = 0;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_compare(difference, sqrt2, &order), PHY_OK);
    PHY_CHECK(order < 0);

    phy_real_algebraic *product = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_multiply(sqrt2, sqrt3, &product), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(product), 2);
    PHY_CHECK_EQ_STR(coefficient_text(product, 0u), "-6");
    PHY_CHECK_EQ_STR(coefficient_text(product, 1u), "0");
    PHY_CHECK_EQ_STR(coefficient_text(product, 2u), "1");

    phy_real_algebraic *quotient = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_divide(sqrt2, sqrt3, &quotient), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(quotient), 2);
    PHY_CHECK_EQ_STR(coefficient_text(quotient, 0u), "-2");
    PHY_CHECK_EQ_STR(coefficient_text(quotient, 1u), "0");
    PHY_CHECK_EQ_STR(coefficient_text(quotient, 2u), "3");

    phy_real_algebraic *square = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_multiply(sqrt2, sqrt2, &square), PHY_OK);
    phy_real_algebraic *zero = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_scale_rational(
            sqrt2, rational("0", "1"), &zero),
        PHY_OK);
    phy_real_algebraic *two = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_translate_rational(
            zero, rational("2", "1"), &two),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_compare(square, two, &order), PHY_OK);
    PHY_CHECK_EQ_INT(order, 0);

    phy_real_algebraic *inverse_square = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_pow_i32(sqrt2, -2, &inverse_square), PHY_OK);
    phy_real_algebraic *half = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_translate_rational(
            zero, rational("1", "2"), &half),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_compare(inverse_square, half, &order), PHY_OK);
    PHY_CHECK_EQ_INT(order, 0);

    /*
     * Mixed degrees exercise the generic Sylvester/interpolation path rather
     * than a quadratic-radical coincidence.
     */
    phy_real_algebraic *mixed_sum = NULL;
    phy_real_algebraic *mixed_difference = NULL;
    phy_real_algebraic *mixed_product = NULL;
    phy_real_algebraic *mixed_quotient = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_add(cbrt2, sqrt3, &mixed_sum), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_subtract(cbrt2, sqrt3, &mixed_difference),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_multiply(cbrt2, sqrt3, &mixed_product), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_divide(cbrt2, sqrt3, &mixed_quotient), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(mixed_sum), 6);
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(mixed_difference), 6);
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(mixed_product), 6);
    PHY_CHECK_EQ_INT(phy_real_algebraic_degree(mixed_quotient), 6);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(mixed_sum), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_validate(mixed_difference), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(mixed_product), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(mixed_quotient), PHY_OK);

    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(sum), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(difference), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(product), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(quotient), PHY_OK);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);
    phy_real_algebraic_destroy(mixed_quotient);
    phy_real_algebraic_destroy(mixed_product);
    phy_real_algebraic_destroy(mixed_difference);
    phy_real_algebraic_destroy(mixed_sum);
    phy_real_algebraic_destroy(half);
    phy_real_algebraic_destroy(inverse_square);
    phy_real_algebraic_destroy(two);
    phy_real_algebraic_destroy(zero);
    phy_real_algebraic_destroy(square);
    phy_real_algebraic_destroy(quotient);
    phy_real_algebraic_destroy(product);
    phy_real_algebraic_destroy(difference);
    phy_real_algebraic_destroy(sum);
    phy_real_algebraic_destroy(cbrt2);
    phy_real_algebraic_destroy(sqrt3);
    phy_real_algebraic_destroy(sqrt2);
    fixture_close(&f);
}

static void test_resultant_limits_and_cancellation_are_typed(void)
{
    static const char *sqrt2_polynomial[] = {"-2", "0", "1"};
    static const char *sqrt3_polynomial[] = {"-3", "0", "1"};
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_algebraic_limits limits;
    phy_algebraic_limits_defaults(&limits);
    limits.max_degree = 3u;
    phy_algebraic_context *context =
        phy_algebraic_context_create(&limits);
    PHY_CHECK(context != NULL);
    phy_real_algebraic *sqrt2 = NULL;
    phy_real_algebraic *sqrt3 = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            context, sqrt2_polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &sqrt2),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            context, sqrt3_polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &sqrt3),
        PHY_OK);
    phy_real_algebraic *result = sqrt2;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_add(sqrt2, sqrt3, &result),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK(result == NULL);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(sqrt2), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(sqrt3), PHY_OK);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);
    phy_real_algebraic_destroy(sqrt3);
    phy_real_algebraic_destroy(sqrt2);
    phy_algebraic_context_destroy(context);
    phy_platform_shutdown();

    fixture f = fixture_open();
    sqrt2 = NULL;
    sqrt3 = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, sqrt2_polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &sqrt2),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, sqrt3_polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &sqrt3),
        PHY_OK);
    unsigned calls = 0u;
    phy_algebraic_set_cancel(f.algebraic, cancel_now, &calls);
    result = sqrt2;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_multiply(sqrt2, sqrt3, &result),
        PHY_ERR_INTERRUPTED);
    PHY_CHECK(result == NULL);
    PHY_CHECK(calls > 0u);
    phy_algebraic_set_cancel(f.algebraic, NULL, NULL);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(sqrt2), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(sqrt3), PHY_OK);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_multiply(sqrt2, sqrt3, &result), PHY_OK);
    PHY_CHECK(result != NULL);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(result), PHY_OK);
    phy_real_algebraic_destroy(result);
    phy_real_algebraic_destroy(sqrt3);
    phy_real_algebraic_destroy(sqrt2);
    fixture_close(&f);
}

static void test_resultant_allocation_failures_are_transactional(void)
{
    static const char *sqrt2_polynomial[] = {"-2", "0", "1"};
    static const char *sqrt3_polynomial[] = {"-3", "0", "1"};

    fixture calibration = fixture_open();
    phy_real_algebraic *sqrt2 = NULL;
    phy_real_algebraic *sqrt3 = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            calibration.algebraic, sqrt2_polynomial, 3u,
            rational("1", "1"), rational("2", "1"), &sqrt2),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            calibration.algebraic, sqrt3_polynomial, 3u,
            rational("1", "1"), rational("2", "1"), &sqrt3),
        PHY_OK);
    const uint32_t attempts_before = phy_host_alloc_attempts();
    phy_real_algebraic *result = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_add(sqrt2, sqrt3, &result), PHY_OK);
    const uint32_t allocations =
        phy_host_alloc_attempts() - attempts_before;
    PHY_CHECK(allocations > 40u);
    phy_real_algebraic_destroy(result);
    phy_real_algebraic_destroy(sqrt3);
    phy_real_algebraic_destroy(sqrt2);
    fixture_close(&calibration);

    const uint32_t probes[] = {
        1u,
        2u,
        allocations / 4u,
        allocations / 2u,
        (allocations * 3u) / 4u,
        allocations > 1u ? allocations - 1u : allocations,
        allocations};
    unsigned failures = 0u;
    for (size_t probe = 0u;
         probe < sizeof probes / sizeof probes[0]; ++probe) {
        fixture f = fixture_open();
        sqrt2 = NULL;
        sqrt3 = NULL;
        PHY_CHECK_EQ_INT(
            phy_real_algebraic_create(
                f.algebraic, sqrt2_polynomial, 3u,
                rational("1", "1"), rational("2", "1"), &sqrt2),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_real_algebraic_create(
                f.algebraic, sqrt3_polynomial, 3u,
                rational("1", "1"), rational("2", "1"), &sqrt3),
            PHY_OK);

        phy_host_fail_alloc_after(probes[probe]);
        result = sqrt2;
        const phy_status status =
            phy_real_algebraic_add(sqrt2, sqrt3, &result);
        phy_host_fail_alloc_after(0u);
        PHY_CHECK(
            status == PHY_OK ||
            status == PHY_ERR_OUT_OF_MEMORY ||
            status == PHY_ERR_MEMORY_LIMIT);
        if (status == PHY_OK) {
            phy_real_algebraic_destroy(result);
        } else {
            failures++;
            PHY_CHECK(result == NULL);
        }
        PHY_CHECK_EQ_INT(phy_real_algebraic_validate(sqrt2), PHY_OK);
        PHY_CHECK_EQ_INT(phy_real_algebraic_validate(sqrt3), PHY_OK);
        PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);

        result = NULL;
        PHY_CHECK_EQ_INT(
            phy_real_algebraic_add(sqrt2, sqrt3, &result), PHY_OK);
        PHY_CHECK(result != NULL);
        PHY_CHECK_EQ_INT(phy_real_algebraic_validate(result), PHY_OK);
        phy_real_algebraic_destroy(result);
        phy_real_algebraic_destroy(sqrt3);
        phy_real_algebraic_destroy(sqrt2);
        fixture_close(&f);

        phy_telemetry telemetry;
        phy_telemetry_get(&telemetry);
        PHY_CHECK_EQ_INT(telemetry.bytes_live, 0);
    }
    PHY_CHECK(failures >= 5u);
    phy_host_fail_alloc_after(0u);
}

typedef enum {
    TRANSFORM_TRANSLATE,
    TRANSFORM_SCALE,
    TRANSFORM_RECIPROCAL
} transform_kind;

static phy_status run_transform(
    transform_kind kind, const phy_real_algebraic *source,
    phy_real_algebraic **out_value)
{
    if (kind == TRANSFORM_TRANSLATE) {
        return phy_real_algebraic_translate_rational(
            source,
            rational("18446744073709551616", "3"),
            out_value);
    }
    if (kind == TRANSFORM_SCALE) {
        return phy_real_algebraic_scale_rational(
            source,
            rational("-18446744073709551617", "5"),
            out_value);
    }
    return phy_real_algebraic_reciprocal(source, out_value);
}

static void test_transform_allocation_failures_are_transactional(void)
{
    static const char *polynomial[] = {"-2", "0", "1"};
    for (int operation = (int)TRANSFORM_TRANSLATE;
         operation <= (int)TRANSFORM_RECIPROCAL; ++operation) {
        fixture calibration = fixture_open();
        phy_real_algebraic *source = NULL;
        PHY_CHECK_EQ_INT(
            phy_real_algebraic_create(
                calibration.algebraic, polynomial, 3u,
                rational("1", "1"), rational("2", "1"), &source),
            PHY_OK);
        const uint32_t attempts_before = phy_host_alloc_attempts();
        phy_real_algebraic *result = NULL;
        PHY_CHECK_EQ_INT(
            run_transform(
                (transform_kind)operation, source, &result),
            PHY_OK);
        const uint32_t allocations =
            phy_host_alloc_attempts() - attempts_before;
        PHY_CHECK(allocations > 20u);
        phy_real_algebraic_destroy(result);
        phy_real_algebraic_destroy(source);
        fixture_close(&calibration);

        unsigned failures = 0u;
        for (uint32_t nth = 1u; nth <= allocations; ++nth) {
            fixture f = fixture_open();
            source = NULL;
            PHY_CHECK_EQ_INT(
                phy_real_algebraic_create(
                    f.algebraic, polynomial, 3u,
                    rational("1", "1"), rational("2", "1"),
                    &source),
                PHY_OK);
            phy_host_fail_alloc_after(nth);
            result = NULL;
            const phy_status status = run_transform(
                (transform_kind)operation, source, &result);
            phy_host_fail_alloc_after(0u);
            PHY_CHECK(
                status == PHY_OK ||
                status == PHY_ERR_OUT_OF_MEMORY ||
                status == PHY_ERR_MEMORY_LIMIT);
            if (status != PHY_OK) {
                failures++;
                PHY_CHECK(result == NULL);
            } else {
                phy_real_algebraic_destroy(result);
            }
            PHY_CHECK_EQ_INT(
                phy_real_algebraic_validate(source), PHY_OK);
            PHY_CHECK_EQ_STR(lower_text(source), "1");
            PHY_CHECK_EQ_STR(upper_text(source), "2");
            PHY_CHECK_EQ_INT(
                phy_algebraic_validate(f.algebraic), PHY_OK);

            result = NULL;
            PHY_CHECK_EQ_INT(
                run_transform(
                    (transform_kind)operation, source, &result),
                PHY_OK);
            PHY_CHECK(result != NULL);
            PHY_CHECK_EQ_INT(
                phy_real_algebraic_validate(result), PHY_OK);
            PHY_CHECK_EQ_INT(
                phy_algebraic_validate(f.algebraic), PHY_OK);
            phy_real_algebraic_destroy(result);
            phy_real_algebraic_destroy(source);
            fixture_close(&f);

            phy_telemetry telemetry;
            phy_telemetry_get(&telemetry);
            PHY_CHECK_EQ_INT(telemetry.bytes_live, 0);
        }
        PHY_CHECK(failures > 10u);
    }
    phy_host_fail_alloc_after(0u);
}

static void test_transform_limits_and_cancellation_are_typed(void)
{
    static const char *polynomial[] = {"-2", "0", "1"};
    fixture calibration = fixture_open();
    phy_real_algebraic *source = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            calibration.algebraic, polynomial, 3u,
            rational("1", "1"), rational("2", "1"), &source),
        PHY_OK);
    const uint32_t creation_steps =
        phy_algebraic_steps(calibration.algebraic);
    phy_real_algebraic *result = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_translate_rational(
            source, rational("1", "2"), &result),
        PHY_OK);
    const uint32_t transform_steps =
        phy_algebraic_steps(calibration.algebraic);
    PHY_CHECK(transform_steps > creation_steps + 1u);
    phy_real_algebraic_destroy(result);
    phy_real_algebraic_destroy(source);
    fixture_close(&calibration);

    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_algebraic_limits limits;
    phy_algebraic_limits_defaults(&limits);
    limits.max_steps =
        creation_steps + (transform_steps - creation_steps) / 2u;
    phy_algebraic_context *context =
        phy_algebraic_context_create(&limits);
    PHY_CHECK(context != NULL);
    source = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            context, polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &source),
        PHY_OK);
    result = source;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_translate_rational(
            source, rational("1", "2"), &result),
        PHY_ERR_TIMEOUT);
    PHY_CHECK(result == NULL);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(source), PHY_OK);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);
    phy_real_algebraic_destroy(source);
    phy_algebraic_context_destroy(context);
    phy_platform_shutdown();

    fixture f = fixture_open();
    source = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            f.algebraic, polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &source),
        PHY_OK);
    unsigned calls = 0u;
    phy_algebraic_set_cancel(f.algebraic, cancel_now, &calls);
    result = source;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_scale_rational(
            source, rational("-3", "2"), &result),
        PHY_ERR_INTERRUPTED);
    PHY_CHECK(result == NULL);
    PHY_CHECK(calls > 0u);
    phy_algebraic_set_cancel(f.algebraic, NULL, NULL);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(source), PHY_OK);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(f.algebraic), PHY_OK);
    result = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_scale_rational(
            source, rational("-3", "2"), &result),
        PHY_OK);
    PHY_CHECK(result != NULL);
    PHY_CHECK_EQ_INT(phy_real_algebraic_validate(result), PHY_OK);
    phy_real_algebraic_destroy(result);
    phy_real_algebraic_destroy(source);
    fixture_close(&f);
}

static bool cancel_now(void *user)
{
    unsigned *calls = (unsigned *)user;
    (*calls)++;
    return true;
}

static void test_limits_and_cancellation_are_typed(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_algebraic_limits limits;
    phy_algebraic_limits_defaults(&limits);
    limits.max_steps = 3u;
    phy_algebraic_context *context =
        phy_algebraic_context_create(&limits);
    PHY_CHECK(context != NULL);
    static const char *polynomial[] = {"0", "-1", "0", "1"};
    uint32_t roots = 0u;
    PHY_CHECK_EQ_INT(
        phy_algebraic_count_real_roots(
            context, polynomial, 4u, rational("-2", "1"),
            rational("2", "1"), &roots),
        PHY_ERR_TIMEOUT);
    phy_real_algebraic *isolated[3] = {NULL, NULL, NULL};
    size_t isolated_count = 77u;
    PHY_CHECK_EQ_INT(
        phy_algebraic_isolate_real_roots(
            context, polynomial, 4u, isolated, 3u,
            &isolated_count),
        PHY_ERR_TIMEOUT);
    PHY_CHECK_EQ_INT(isolated_count, 77);
    PHY_CHECK(isolated[0] == NULL);
    PHY_CHECK(isolated[1] == NULL);
    PHY_CHECK(isolated[2] == NULL);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);
    phy_algebraic_context_destroy(context);

    phy_algebraic_limits_defaults(&limits);
    context = phy_algebraic_context_create(&limits);
    PHY_CHECK(context != NULL);
    unsigned calls = 0u;
    phy_algebraic_set_cancel(context, cancel_now, &calls);
    PHY_CHECK_EQ_INT(
        phy_algebraic_count_real_roots(
            context, polynomial, 4u, rational("-2", "1"),
            rational("2", "1"), &roots),
        PHY_ERR_INTERRUPTED);
    PHY_CHECK(calls > 0u);
    calls = 0u;
    isolated_count = 77u;
    PHY_CHECK_EQ_INT(
        phy_algebraic_isolate_real_roots(
            context, polynomial, 4u, isolated, 3u,
            &isolated_count),
        PHY_ERR_INTERRUPTED);
    PHY_CHECK(calls > 0u);
    PHY_CHECK_EQ_INT(isolated_count, 77);
    PHY_CHECK(isolated[0] == NULL);
    PHY_CHECK(isolated[1] == NULL);
    PHY_CHECK(isolated[2] == NULL);
    phy_algebraic_set_cancel(context, NULL, NULL);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);
    phy_algebraic_context_destroy(context);
    phy_platform_shutdown();
}

static void test_isolation_allocation_failure_is_transactional(void)
{
    static const char *polynomial[] = {
        "-1", "-1", "0", "0", "0", "1"};
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);

    const uint32_t attempts_before = phy_host_alloc_attempts();
    phy_algebraic_context *calibration =
        phy_algebraic_context_create(NULL);
    PHY_CHECK(calibration != NULL);
    phy_real_algebraic *calibration_roots[5] = {
        NULL, NULL, NULL, NULL, NULL};
    size_t calibration_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_algebraic_isolate_real_roots(
            calibration, polynomial, 6u, calibration_roots, 5u,
            &calibration_count),
        PHY_OK);
    PHY_CHECK_EQ_INT(calibration_count, 1);
    const uint32_t allocations =
        phy_host_alloc_attempts() - attempts_before;
    phy_algebraic_context_destroy(calibration);
    PHY_CHECK(allocations > 40u);

    unsigned failures = 0u;
    for (uint32_t nth = 1u; nth <= allocations; ++nth) {
        phy_host_fail_alloc_after(nth);
        phy_algebraic_context *context =
            phy_algebraic_context_create(NULL);
        if (context == NULL) {
            phy_host_fail_alloc_after(0u);
            continue;
        }
        phy_real_algebraic *values[5] = {
            NULL, NULL, NULL, NULL, NULL};
        size_t count = 91u;
        const phy_status status =
            phy_algebraic_isolate_real_roots(
                context, polynomial, 6u, values, 5u, &count);
        phy_host_fail_alloc_after(0u);
        PHY_CHECK(status == PHY_OK ||
                  status == PHY_ERR_OUT_OF_MEMORY ||
                  status == PHY_ERR_MEMORY_LIMIT);
        if (status != PHY_OK) {
            failures++;
            PHY_CHECK_EQ_INT(count, 91);
            for (size_t index = 0u; index < 5u; ++index) {
                PHY_CHECK(values[index] == NULL);
            }
        } else {
            PHY_CHECK_EQ_INT(count, 1);
            phy_real_algebraic_destroy(values[0]);
        }
        PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);

        memset(values, 0, sizeof values);
        count = 0u;
        PHY_CHECK_EQ_INT(
            phy_algebraic_isolate_real_roots(
                context, polynomial, 6u, values, 5u, &count),
            PHY_OK);
        PHY_CHECK_EQ_INT(count, 1);
        PHY_CHECK_EQ_INT(
            phy_real_algebraic_validate(values[0]), PHY_OK);
        PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);
        phy_algebraic_context_destroy(context);

        phy_telemetry telemetry;
        phy_telemetry_get(&telemetry);
        PHY_CHECK_EQ_INT(telemetry.bytes_live, 0);
    }
    PHY_CHECK(failures > 20u);
    phy_host_fail_alloc_after(0u);
    phy_platform_shutdown();
}

static void test_allocation_failure_is_transactional(void)
{
    static const char *polynomial[] = {"-2", "0", "1"};
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);

    const uint32_t attempts_before = phy_host_alloc_attempts();
    phy_algebraic_context *calibration =
        phy_algebraic_context_create(NULL);
    PHY_CHECK(calibration != NULL);
    phy_real_algebraic *calibration_value = NULL;
    PHY_CHECK_EQ_INT(
        phy_real_algebraic_create(
            calibration, polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &calibration_value),
        PHY_OK);
    const uint32_t allocations =
        phy_host_alloc_attempts() - attempts_before;
    phy_algebraic_context_destroy(calibration);
    PHY_CHECK(allocations > 20u);

    unsigned failures = 0u;
    for (uint32_t nth = 1u; nth <= allocations; ++nth) {
        phy_host_fail_alloc_after(nth);
        phy_algebraic_context *context =
            phy_algebraic_context_create(NULL);
        if (context == NULL) {
            phy_host_fail_alloc_after(0u);
            continue;
        }
        phy_real_algebraic *value = NULL;
        const phy_status status = phy_real_algebraic_create(
            context, polynomial, 3u, rational("1", "1"),
            rational("2", "1"), &value);
        phy_host_fail_alloc_after(0u);
        PHY_CHECK(status == PHY_OK ||
                  status == PHY_ERR_OUT_OF_MEMORY);
        if (status != PHY_OK) {
            failures++;
            PHY_CHECK(value == NULL);
        }
        PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);

        if (value != NULL) {
            phy_real_algebraic_destroy(value);
        }
        value = NULL;
        PHY_CHECK_EQ_INT(
            phy_real_algebraic_create(
                context, polynomial, 3u, rational("1", "1"),
                rational("2", "1"), &value),
            PHY_OK);
        PHY_CHECK_EQ_INT(phy_real_algebraic_validate(value), PHY_OK);
        PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);
        phy_algebraic_context_destroy(context);

        phy_telemetry telemetry;
        phy_telemetry_get(&telemetry);
        PHY_CHECK_EQ_INT(telemetry.bytes_live, 0);
    }
    PHY_CHECK(failures > 10u);
    phy_host_fail_alloc_after(0u);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_context_lifecycle);
    PHY_TEST_CASE(test_sturm_root_counts);
    PHY_TEST_CASE(test_create_normalizes_and_certifies);
    PHY_TEST_CASE(test_all_real_roots_are_isolated_in_order);
    PHY_TEST_CASE(test_refine_and_safe_compare);
    PHY_TEST_CASE(test_arbitrary_precision_coefficients_and_intervals);
    PHY_TEST_CASE(test_exact_rational_transforms);
    PHY_TEST_CASE(test_rational_transform_edge_cases);
    PHY_TEST_CASE(test_resultant_arithmetic_closure);
    PHY_TEST_CASE(test_resultant_limits_and_cancellation_are_typed);
    PHY_TEST_CASE(test_resultant_allocation_failures_are_transactional);
    PHY_TEST_CASE(test_transform_allocation_failures_are_transactional);
    PHY_TEST_CASE(test_transform_limits_and_cancellation_are_typed);
    PHY_TEST_CASE(test_limits_and_cancellation_are_typed);
    PHY_TEST_CASE(test_isolation_allocation_failure_is_transactional);
    PHY_TEST_CASE(test_allocation_failure_is_transactional);
    return PHY_TEST_REPORT("test_algebraic");
}
