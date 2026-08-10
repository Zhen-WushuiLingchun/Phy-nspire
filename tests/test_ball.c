#include "phy/ball.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy_test.h"

static const char *text(const phy_bigrat *value)
{
    static char buffers[8][512];
    static unsigned next;
    char *buffer = buffers[next++ & 7u];
    size_t required = 0u;
    if (phy_bigrat_write(value, buffer, sizeof buffers[0], &required) !=
        PHY_OK) {
        return "<write failed>";
    }
    return buffer;
}

static void set_rat(phy_bigrat *value, int64_t numerator,
                    int64_t denominator)
{
    PHY_CHECK_EQ_INT(
        phy_bigrat_set_i64(value, numerator, denominator), PHY_OK);
}

static void test_certified_ball_arithmetic(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_exact_context *exact = phy_exact_context_create(NULL);
    PHY_CHECK(exact != NULL);

    phy_bigrat one;
    phy_bigrat two;
    phy_bigrat three;
    phy_bigrat four;
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &one), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &two), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &three), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &four), PHY_OK);
    set_rat(&one, 1, 1);
    set_rat(&two, 2, 1);
    set_rat(&three, 3, 1);
    set_rat(&four, 4, 1);

    phy_real_ball a;
    phy_real_ball b;
    phy_real_ball result;
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &result), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_set_interval(&a, &one, &two), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_set_interval(&b, &three, &four), PHY_OK);
    PHY_CHECK_EQ_STR(text(&a.midpoint), "3/2");
    PHY_CHECK_EQ_STR(text(&a.radius), "1/2");

    PHY_CHECK_EQ_INT(phy_real_ball_add(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "5");
    PHY_CHECK_EQ_STR(text(&result.radius), "1");

    PHY_CHECK_EQ_INT(phy_real_ball_subtract(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "-2");
    PHY_CHECK_EQ_STR(text(&result.radius), "1");

    PHY_CHECK_EQ_INT(phy_real_ball_multiply(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "21/4");
    PHY_CHECK_EQ_STR(text(&result.radius), "11/4");

    PHY_CHECK_EQ_INT(phy_real_ball_divide(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "7/16");
    PHY_CHECK_EQ_STR(text(&result.radius), "11/48");

    PHY_CHECK_EQ_INT(phy_real_ball_pow_i32(&a, 2, &result), PHY_OK);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "9/4");
    PHY_CHECK_EQ_STR(text(&result.radius), "7/4");
    PHY_CHECK(!phy_real_ball_contains_zero(&a));
    bool contains_zero = true;
    PHY_CHECK_EQ_INT(
        phy_real_ball_contains_zero_checked(&a, &contains_zero), PHY_OK);
    PHY_CHECK(!contains_zero);
    contains_zero = false;
    phy_host_fail_alloc_after(1u);
    const phy_status zero_status =
        phy_real_ball_contains_zero_checked(&a, &contains_zero);
    phy_host_fail_alloc_after(0u);
    PHY_CHECK(zero_status == PHY_ERR_OUT_OF_MEMORY || zero_status == PHY_OK);
    if (zero_status != PHY_OK) {
        PHY_CHECK(contains_zero); /* fail closed */
    }

    phy_bigrat minus_one;
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &minus_one), PHY_OK);
    set_rat(&minus_one, -1, 1);
    PHY_CHECK_EQ_INT(
        phy_real_ball_set_interval(&b, &minus_one, &one), PHY_OK);
    PHY_CHECK(phy_real_ball_contains_zero(&b));
    PHY_CHECK_EQ_INT(
        phy_real_ball_divide(&a, &b, &result), PHY_ERR_DOMAIN);

    PHY_CHECK_EQ_INT(phy_real_ball_set_exact(&a, &one), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_negate(&four, &four), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_negate(&three, &three), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_ball_set_interval(&b, &four, &three), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_divide(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "-7/24");
    PHY_CHECK_EQ_STR(text(&result.radius), "1/24");
    PHY_CHECK_EQ_INT(phy_bigrat_negate(&four, &four), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_negate(&three, &three), PHY_OK);

    PHY_CHECK_EQ_INT(phy_real_ball_set_exact(&a, &two), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_sqrt(&a, 40u, &result), PHY_OK);
    phy_bigrat root_lower;
    phy_bigrat root_upper;
    phy_bigrat square;
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &root_lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &root_upper), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &square), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_lower(&result, &root_lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_upper(&result, &root_upper), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_multiply(&root_lower, &root_lower, &square), PHY_OK);
    int order = 1;
    PHY_CHECK_EQ_INT(phy_bigrat_compare(&square, &two, &order), PHY_OK);
    PHY_CHECK(order <= 0);
    PHY_CHECK_EQ_INT(
        phy_bigrat_multiply(&root_upper, &root_upper, &square), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_compare(&square, &two, &order), PHY_OK);
    PHY_CHECK(order >= 0);
    PHY_CHECK(phy_bigrat_sign(&result.radius) > 0);
    phy_bigrat_destroy(&square);
    phy_bigrat_destroy(&root_upper);
    phy_bigrat_destroy(&root_lower);

    PHY_CHECK_EQ_INT(phy_real_ball_validate(&a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_validate(&b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_validate(&result), PHY_OK);
    phy_real_ball_destroy(&result);
    phy_real_ball_destroy(&b);
    phy_real_ball_destroy(&a);
    phy_bigrat_destroy(&minus_one);
    phy_bigrat_destroy(&four);
    phy_bigrat_destroy(&three);
    phy_bigrat_destroy(&two);
    phy_bigrat_destroy(&one);
    PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);
    phy_exact_context_destroy(exact);
    phy_platform_shutdown();
}

static void test_ball_allocation_failures_are_transactional(void)
{
    bool reached_success = false;
    for (unsigned countdown = 1u; countdown <= 2048u; ++countdown) {
        PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
        phy_telemetry before;
        phy_telemetry_get(&before);
        phy_exact_context *exact = phy_exact_context_create(NULL);
        PHY_CHECK(exact != NULL);
        phy_real_ball input;
        phy_real_ball result;
        PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &input), PHY_OK);
        PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &result), PHY_OK);
        PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&input, 2, 1), PHY_OK);
        PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&result, 7, 1), PHY_OK);

        phy_host_fail_alloc_after(countdown);
        const phy_status status =
            phy_real_ball_sqrt(&input, 8u, &result);
        phy_host_fail_alloc_after(0u);
        PHY_CHECK(status == PHY_OK || status == PHY_ERR_OUT_OF_MEMORY);
        PHY_CHECK_EQ_INT(phy_real_ball_validate(&input), PHY_OK);
        PHY_CHECK_EQ_INT(phy_real_ball_validate(&result), PHY_OK);
        PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);
        if (status == PHY_OK) {
            reached_success = true;
        } else {
            PHY_CHECK_EQ_STR(text(&result.midpoint), "7");
            PHY_CHECK_EQ_STR(text(&result.radius), "0");
        }

        phy_real_ball_destroy(&result);
        phy_real_ball_destroy(&input);
        phy_exact_context_destroy(exact);
        phy_telemetry after;
        phy_telemetry_get(&after);
        PHY_CHECK_EQ_INT(after.bytes_live, before.bytes_live);
        phy_platform_shutdown();
        if (reached_success) {
            break;
        }
    }
    PHY_CHECK(reached_success);
}

int main(void)
{
    PHY_TEST_CASE(test_certified_ball_arithmetic);
    PHY_TEST_CASE(test_ball_allocation_failures_are_transactional);
    return PHY_TEST_REPORT("test_ball");
}
