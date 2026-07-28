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
    phy_algebraic_set_cancel(context, NULL, NULL);
    PHY_CHECK_EQ_INT(phy_algebraic_validate(context), PHY_OK);
    phy_algebraic_context_destroy(context);
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
    PHY_TEST_CASE(test_refine_and_safe_compare);
    PHY_TEST_CASE(test_arbitrary_precision_coefficients_and_intervals);
    PHY_TEST_CASE(test_limits_and_cancellation_are_typed);
    PHY_TEST_CASE(test_allocation_failure_is_transactional);
    return PHY_TEST_REPORT("test_algebraic");
}
