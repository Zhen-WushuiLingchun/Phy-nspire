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

static void check_contains(const phy_real_ball *ball,
                           const phy_bigrat *value)
{
    phy_exact_context *exact = phy_bigrat_numerator(value)->context;
    phy_bigrat lower;
    phy_bigrat upper;
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &upper), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_lower(ball, &lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_upper(ball, &upper), PHY_OK);
    int order = 1;
    PHY_CHECK_EQ_INT(phy_bigrat_compare(&lower, value, &order), PHY_OK);
    PHY_CHECK(order <= 0);
    PHY_CHECK_EQ_INT(phy_bigrat_compare(value, &upper, &order), PHY_OK);
    PHY_CHECK(order <= 0);
    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
}

static void check_inside(const phy_real_ball *ball,
                         const phy_bigrat *outer_lower,
                         const phy_bigrat *outer_upper)
{
    phy_exact_context *exact = phy_bigrat_numerator(outer_lower)->context;
    phy_bigrat lower;
    phy_bigrat upper;
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &upper), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_lower(ball, &lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_upper(ball, &upper), PHY_OK);
    int order = 1;
    PHY_CHECK_EQ_INT(
        phy_bigrat_compare(outer_lower, &lower, &order), PHY_OK);
    PHY_CHECK(order <= 0);
    PHY_CHECK_EQ_INT(
        phy_bigrat_compare(&upper, outer_upper, &order), PHY_OK);
    PHY_CHECK(order <= 0);
    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
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

static void test_certified_elementary_functions(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_exact_context *exact = phy_exact_context_create(NULL);
    PHY_CHECK(exact != NULL);

    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat target;
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &upper), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &target), PHY_OK);

    phy_real_ball input;
    phy_real_ball divisor;
    phy_real_ball angle;
    phy_real_ball result;
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &input), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &divisor), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &angle), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &result), PHY_OK);

    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&input, 1, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_exp(&input, 152u, &result), PHY_OK);
    set_rat(&lower, 271828, 100000);
    set_rat(&upper, 271829, 100000);
    check_inside(&result, &lower, &upper);

    static const char scale[] =
        "10000000000000000000000000000000000000000";
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &lower, "27182818284590452353602874713526624977572", scale),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &upper, "27182818284590452353602874713526624977573", scale),
        PHY_OK);
    check_inside(&result, &lower, &upper);
    PHY_CHECK_EQ_INT(
        phy_real_ball_set_interval(&input, &lower, &upper), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_log(&input, 96u, &result), PHY_OK);
    set_rat(&target, 1, 1);
    check_contains(&result, &target);

    /* A non-special logarithm must deliver the requested high-precision
       enclosure, not merely a broad interval that happens to contain log(2). */
    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&input, 2, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_log(&input, 152u, &result), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &lower, "6931471805599453094172321214581765680755", scale),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &upper, "6931471805599453094172321214581765680756", scale),
        PHY_OK);
    check_inside(&result, &lower, &upper);

    /* The public maximum remains a real supported bound for a representative
       argument; it is not only a syntactic ceiling. */
    PHY_CHECK_EQ_INT(phy_real_ball_log(&input, 256u, &result), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_validate(&result), PHY_OK);

    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &lower, "31415926535897932384626433832795028841971", scale),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &upper, "31415926535897932384626433832795028841972", scale),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_ball_set_interval(&input, &lower, &upper), PHY_OK);

    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&divisor, 6, 1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_ball_divide(&input, &divisor, &angle), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_sin(&angle, 96u, &result), PHY_OK);
    set_rat(&target, 1, 2);
    check_contains(&result, &target);

    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&divisor, 3, 1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_ball_divide(&input, &divisor, &angle), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_cos(&angle, 96u, &result), PHY_OK);
    check_contains(&result, &target);

    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&divisor, 4, 1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_ball_divide(&input, &divisor, &angle), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_tan(&angle, 96u, &result), PHY_OK);
    set_rat(&target, 1, 1);
    check_contains(&result, &target);

    /* Non-special arguments pin useful output width for every elementary
       kernel.  These are one-unit intervals on a 10^-40 grid. */
    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&input, 1, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_sin(&input, 152u, &result), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &lower, "8414709848078965066525023216302989996225", scale),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &upper, "8414709848078965066525023216302989996226", scale),
        PHY_OK);
    check_inside(&result, &lower, &upper);

    PHY_CHECK_EQ_INT(phy_real_ball_cos(&input, 152u, &result), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &lower, "5403023058681397174009366074429766037323", scale),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &upper, "5403023058681397174009366074429766037324", scale),
        PHY_OK);
    check_inside(&result, &lower, &upper);

    PHY_CHECK_EQ_INT(phy_real_ball_tan(&input, 152u, &result), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &lower, "15574077246549022305069748074583601730872", scale),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(
            &upper, "15574077246549022305069748074583601730873", scale),
        PHY_OK);
    check_inside(&result, &lower, &upper);

    set_rat(&lower, -1, 1);
    set_rat(&upper, 1, 1);
    PHY_CHECK_EQ_INT(
        phy_real_ball_set_interval(&input, &lower, &upper), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&result, 7, 1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_ball_log(&input, 96u, &result), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "7");
    PHY_CHECK_EQ_STR(text(&result.radius), "0");

    set_rat(&lower, 1, 1);
    set_rat(&upper, 2, 1);
    PHY_CHECK_EQ_INT(
        phy_real_ball_set_interval(&input, &lower, &upper), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_ball_tan(&input, 96u, &result), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "7");
    PHY_CHECK_EQ_STR(text(&result.radius), "0");
    PHY_CHECK_EQ_INT(
        phy_real_ball_exp(&input, 257u, &result), PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "7");

    PHY_CHECK_EQ_INT(phy_real_ball_validate(&input), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_validate(&divisor), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_validate(&angle), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_validate(&result), PHY_OK);
    phy_real_ball_destroy(&result);
    phy_real_ball_destroy(&angle);
    phy_real_ball_destroy(&divisor);
    phy_real_ball_destroy(&input);
    phy_bigrat_destroy(&target);
    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
    PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);
    phy_exact_context_destroy(exact);
    phy_platform_shutdown();
}

typedef phy_status (*elementary_ball_function)(
    const phy_real_ball *, uint32_t, phy_real_ball *);

static void run_elementary_allocation_case(
    elementary_ball_function function, unsigned countdown,
    bool expect_success)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_telemetry before;
    phy_telemetry_get(&before);
    phy_exact_context *exact = phy_exact_context_create(NULL);
    PHY_CHECK(exact != NULL);
    phy_real_ball input;
    phy_real_ball result;
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &input), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &result), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&input, 1, 2), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&result, 7, 1), PHY_OK);

    phy_host_fail_alloc_after(countdown);
    const phy_status status = function(&input, 8u, &result);
    phy_host_fail_alloc_after(0u);
    PHY_CHECK_EQ_INT(status, expect_success ? PHY_OK : PHY_ERR_OUT_OF_MEMORY);
    PHY_CHECK_EQ_INT(phy_real_ball_validate(&input), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_validate(&result), PHY_OK);
    PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);
    if (!expect_success) {
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
}

static void check_elementary_allocation_failures(
    elementary_ball_function function)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_exact_context *measurement_exact = phy_exact_context_create(NULL);
    PHY_CHECK(measurement_exact != NULL);
    phy_real_ball measurement_input;
    phy_real_ball measurement_result;
    PHY_CHECK_EQ_INT(
        phy_real_ball_init(measurement_exact, &measurement_input), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_ball_init(measurement_exact, &measurement_result), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_real_ball_set_i64(&measurement_input, 1, 2), PHY_OK);
    const uint32_t before_attempts = phy_host_alloc_attempts();
    PHY_CHECK_EQ_INT(
        function(&measurement_input, 8u, &measurement_result), PHY_OK);
    const uint32_t attempts = phy_host_alloc_attempts() - before_attempts;
    PHY_CHECK(attempts > 0u);
    phy_real_ball_destroy(&measurement_result);
    phy_real_ball_destroy(&measurement_input);
    phy_exact_context_destroy(measurement_exact);
    phy_platform_shutdown();

    const unsigned exhaustive = attempts < 128u ? attempts : 128u;
    for (unsigned countdown = 1u; countdown <= exhaustive; ++countdown) {
        run_elementary_allocation_case(function, countdown, false);
    }
    for (unsigned checkpoint = 256u; checkpoint < attempts;
         checkpoint *= 2u) {
        run_elementary_allocation_case(function, checkpoint, false);
        if (checkpoint > UINT32_MAX / 2u) {
            break;
        }
    }
    run_elementary_allocation_case(function, attempts, false);
    run_elementary_allocation_case(function, attempts + 1u, true);
}

static void test_elementary_allocation_failures_are_transactional(void)
{
    check_elementary_allocation_failures(phy_real_ball_exp);
    check_elementary_allocation_failures(phy_real_ball_log);
    check_elementary_allocation_failures(phy_real_ball_sin);
    check_elementary_allocation_failures(phy_real_ball_cos);
    check_elementary_allocation_failures(phy_real_ball_tan);
}

static void test_certified_inverse_hyperbolic_and_special_functions(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_exact_context *exact = phy_exact_context_create(NULL);
    PHY_CHECK(exact != NULL);
    phy_real_ball input;
    phy_real_ball result;
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &input), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_init(exact, &result), PHY_OK);
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat zero;
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &upper), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &zero), PHY_OK);
    set_rat(&zero, 0, 1);

    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&input, 0, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_sinh(&input, 96u, &result), PHY_OK);
    check_contains(&result, &zero);
    PHY_CHECK_EQ_INT(phy_real_ball_cosh(&input, 96u, &result), PHY_OK);
    set_rat(&lower, 1, 1);
    check_contains(&result, &lower);
    PHY_CHECK_EQ_INT(phy_real_ball_tanh(&input, 96u, &result), PHY_OK);
    check_contains(&result, &zero);

    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&input, 1, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_atan(&input, 96u, &result), PHY_OK);
    set_rat(&lower, 785398, 1000000);
    set_rat(&upper, 785399, 1000000);
    check_inside(&result, &lower, &upper);
    PHY_CHECK_EQ_INT(phy_real_ball_asinh(&input, 96u, &result), PHY_OK);
    set_rat(&lower, 881373, 1000000);
    set_rat(&upper, 881374, 1000000);
    check_inside(&result, &lower, &upper);

    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&input, 1, 2), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_asin(&input, 96u, &result), PHY_OK);
    set_rat(&lower, 523598, 1000000);
    set_rat(&upper, 523599, 1000000);
    check_inside(&result, &lower, &upper);
    PHY_CHECK_EQ_INT(phy_real_ball_acos(&input, 96u, &result), PHY_OK);
    set_rat(&lower, 1047197, 1000000);
    set_rat(&upper, 1047198, 1000000);
    check_inside(&result, &lower, &upper);
    PHY_CHECK_EQ_INT(phy_real_ball_atanh(&input, 96u, &result), PHY_OK);
    set_rat(&lower, 549306, 1000000);
    set_rat(&upper, 549307, 1000000);
    check_inside(&result, &lower, &upper);
    PHY_CHECK_EQ_INT(phy_real_ball_erf(&input, 96u, &result), PHY_OK);
    set_rat(&lower, 520499, 1000000);
    set_rat(&upper, 520500, 1000000);
    check_inside(&result, &lower, &upper);
    PHY_CHECK_EQ_INT(phy_real_ball_erfc(&input, 96u, &result), PHY_OK);
    set_rat(&lower, 479500, 1000000);
    set_rat(&upper, 479501, 1000000);
    check_inside(&result, &lower, &upper);

    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&input, 2, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_acosh(&input, 96u, &result), PHY_OK);
    set_rat(&lower, 1316957, 1000000);
    set_rat(&upper, 1316958, 1000000);
    check_inside(&result, &lower, &upper);

    PHY_CHECK_EQ_INT(phy_real_ball_set_i64(&result, 7, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_real_ball_asin(&input, 96u, &result), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "7");
    PHY_CHECK_EQ_INT(phy_real_ball_atanh(&input, 96u, &result), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "7");
    PHY_CHECK_EQ_INT(phy_real_ball_erf(&input, 96u, &result), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_STR(text(&result.midpoint), "7");

    phy_bigrat_destroy(&zero);
    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
    phy_real_ball_destroy(&result);
    phy_real_ball_destroy(&input);
    PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);
    phy_exact_context_destroy(exact);
    phy_platform_shutdown();
}

static void test_certified_complex_ball_principal_branches(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_exact_context *exact = phy_exact_context_create(NULL);
    PHY_CHECK(exact != NULL);
    phy_complex_ball a;
    phy_complex_ball b;
    phy_complex_ball result;
    PHY_CHECK_EQ_INT(phy_complex_ball_init(exact, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_init(exact, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_init(exact, &result), PHY_OK);
    phy_bigrat target;
    phy_bigrat lower;
    phy_bigrat upper;
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &target), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &upper), PHY_OK);

    PHY_CHECK_EQ_INT(phy_complex_ball_set_i64(&a, 1, 1, 2, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_set_i64(&b, 3, 1, -1, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_multiply(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(text(&result.real.midpoint), "5");
    PHY_CHECK_EQ_STR(text(&result.imaginary.midpoint), "5");
    PHY_CHECK_EQ_INT(phy_complex_ball_divide(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(text(&result.real.midpoint), "1/10");
    PHY_CHECK_EQ_STR(text(&result.imaginary.midpoint), "7/10");

    PHY_CHECK_EQ_INT(phy_complex_ball_set_i64(&a, -1, 1, 0, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_sqrt(&a, 96u, &result), PHY_OK);
    set_rat(&target, 0, 1);
    check_contains(&result.real, &target);
    set_rat(&target, 1, 1);
    check_contains(&result.imaginary, &target);
    PHY_CHECK_EQ_INT(phy_complex_ball_log(&a, 96u, &result), PHY_OK);
    set_rat(&target, 0, 1);
    check_contains(&result.real, &target);
    set_rat(&lower, 3141592, 1000000);
    set_rat(&upper, 3141593, 1000000);
    check_inside(&result.imaginary, &lower, &upper);

    PHY_CHECK_EQ_INT(phy_complex_ball_set_i64(&a, 0, 1, 1, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_exp(&a, 96u, &result), PHY_OK);
    set_rat(&lower, 540302, 1000000);
    set_rat(&upper, 540303, 1000000);
    check_inside(&result.real, &lower, &upper);
    set_rat(&lower, 841470, 1000000);
    set_rat(&upper, 841471, 1000000);
    check_inside(&result.imaginary, &lower, &upper);

    PHY_CHECK_EQ_INT(phy_complex_ball_set_i64(&a, 2, 1, 0, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_asin(&a, 96u, &result), PHY_OK);
    set_rat(&lower, 1570796, 1000000);
    set_rat(&upper, 1570797, 1000000);
    check_inside(&result.real, &lower, &upper);
    set_rat(&lower, -1316960, 1000000);
    set_rat(&upper, -1316957, 1000000);
    check_inside(&result.imaginary, &lower, &upper);

    PHY_CHECK_EQ_INT(phy_complex_ball_validate(&a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_validate(&b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_validate(&result), PHY_OK);
    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
    phy_bigrat_destroy(&target);
    phy_complex_ball_destroy(&result);
    phy_complex_ball_destroy(&b);
    phy_complex_ball_destroy(&a);
    PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);
    phy_exact_context_destroy(exact);
    phy_platform_shutdown();
}

static void test_complex_ball_allocation_failure_is_transactional(void)
{
    bool reached_success = false;
    for (unsigned countdown = 1u; countdown <= 512u; ++countdown) {
        PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
        phy_telemetry before;
        phy_telemetry_get(&before);
        phy_exact_context *exact = phy_exact_context_create(NULL);
        PHY_CHECK(exact != NULL);
        phy_complex_ball left = {0};
        phy_complex_ball right = {0};
        phy_complex_ball result = {0};
        PHY_CHECK_EQ_INT(phy_complex_ball_init(exact, &left), PHY_OK);
        PHY_CHECK_EQ_INT(phy_complex_ball_init(exact, &right), PHY_OK);
        PHY_CHECK_EQ_INT(phy_complex_ball_init(exact, &result), PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_complex_ball_set_i64(&left, 1, 1, 2, 1), PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_complex_ball_set_i64(&right, 3, 1, -1, 1), PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_complex_ball_set_i64(&result, 7, 1, 9, 1), PHY_OK);

        phy_host_fail_alloc_after(countdown);
        const phy_status status =
            phy_complex_ball_multiply(&left, &right, &result);
        phy_host_fail_alloc_after(0u);
        PHY_CHECK(status == PHY_OK || status == PHY_ERR_OUT_OF_MEMORY);
        PHY_CHECK_EQ_INT(phy_complex_ball_validate(&left), PHY_OK);
        PHY_CHECK_EQ_INT(phy_complex_ball_validate(&right), PHY_OK);
        PHY_CHECK_EQ_INT(phy_complex_ball_validate(&result), PHY_OK);
        PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);
        if (status == PHY_OK) {
            reached_success = true;
        } else {
            PHY_CHECK_EQ_STR(text(&result.real.midpoint), "7");
            PHY_CHECK_EQ_STR(text(&result.imaginary.midpoint), "9");
        }

        phy_complex_ball_destroy(&result);
        phy_complex_ball_destroy(&right);
        phy_complex_ball_destroy(&left);
        phy_exact_context_destroy(exact);
        phy_telemetry after;
        phy_telemetry_get(&after);
        PHY_CHECK_EQ_INT(after.bytes_live, before.bytes_live);
        phy_platform_shutdown();
        if (reached_success) break;
    }
    PHY_CHECK(reached_success);
}

static void test_certified_complex_special_functions(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_exact_limits limits;
    phy_exact_limits_defaults(&limits);
    limits.max_steps = 32000000u;
    limits.max_bytes = 2u * 1024u * 1024u;
    phy_exact_context *exact = phy_exact_context_create(&limits);
    PHY_CHECK(exact != NULL);
    phy_complex_ball argument = {0};
    phy_complex_ball result = {0};
    PHY_CHECK_EQ_INT(phy_complex_ball_init(exact, &argument), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_init(exact, &result), PHY_OK);
    phy_bigrat lower = {0};
    phy_bigrat upper = {0};
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &lower), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(exact, &upper), PHY_OK);

    PHY_CHECK_EQ_INT(
        phy_complex_ball_set_i64(&argument, 1, 1, 1, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_complex_ball_erf(&argument, 80u, &result), PHY_OK);
    set_rat(&lower, 13161512816LL, 10000000000LL);
    set_rat(&upper, 13161512818LL, 10000000000LL);
    check_inside(&result.real, &lower, &upper);
    set_rat(&lower, 1904534691LL, 10000000000LL);
    set_rat(&upper, 1904534693LL, 10000000000LL);
    check_inside(&result.imaginary, &lower, &upper);

    PHY_CHECK_EQ_INT(
        phy_complex_ball_set_i64(&argument, 1, 3, 1, 4), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_complex_ball_loggamma(&argument, 80u, &result), PHY_OK);
    set_rat(&lower, 7283877795LL, 10000000000LL);
    set_rat(&upper, 7283877796LL, 10000000000LL);
    check_inside(&result.real, &lower, &upper);
    set_rat(&lower, -6736360632LL, 10000000000LL);
    set_rat(&upper, -6736360631LL, 10000000000LL);
    check_inside(&result.imaginary, &lower, &upper);

    PHY_CHECK_EQ_INT(
        phy_complex_ball_gamma(&argument, 80u, &result), PHY_OK);
    set_rat(&lower, 16191843930LL, 10000000000LL);
    set_rat(&upper, 16191843932LL, 10000000000LL);
    check_inside(&result.real, &lower, &upper);
    set_rat(&lower, -12924161400LL, 10000000000LL);
    set_rat(&upper, -12924161390LL, 10000000000LL);
    check_inside(&result.imaginary, &lower, &upper);

    PHY_CHECK_EQ_INT(
        phy_complex_ball_digamma(&argument, 80u, &result), PHY_OK);
    set_rat(&lower, -20179324940LL, 10000000000LL);
    set_rat(&upper, -20179324938LL, 10000000000LL);
    check_inside(&result.real, &lower, &upper);
    set_rat(&lower, 17083870710LL, 10000000000LL);
    set_rat(&upper, 17083870711LL, 10000000000LL);
    check_inside(&result.imaginary, &lower, &upper);

    /* Analytic continuation across Re[z] < 0, away from Gamma poles. */
    PHY_CHECK_EQ_INT(
        phy_complex_ball_set_i64(&argument, -1, 2, 1, 4), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_complex_ball_gamma(&argument, 48u, &result), PHY_OK);
    set_rat(&lower, -2754727, 1000000);
    set_rat(&upper, -2754726, 1000000);
    check_inside(&result.real, &lower, &upper);
    set_rat(&lower, -31001, 1000000);
    set_rat(&upper, -31000, 1000000);
    check_inside(&result.imaginary, &lower, &upper);

    PHY_CHECK_EQ_INT(
        phy_complex_ball_loggamma(&argument, 48u, &result), PHY_OK);
    set_rat(&lower, 1013381, 1000000);
    set_rat(&upper, 1013382, 1000000);
    check_inside(&result.real, &lower, &upper);
    set_rat(&lower, -3130340, 1000000);
    set_rat(&upper, -3130339, 1000000);
    check_inside(&result.imaginary, &lower, &upper);

    PHY_CHECK_EQ_INT(
        phy_complex_ball_digamma(&argument, 48u, &result), PHY_OK);
    set_rat(&lower, 61838, 1000000);
    set_rat(&upper, 61839, 1000000);
    check_inside(&result.real, &lower, &upper);
    set_rat(&lower, 1830119, 1000000);
    set_rat(&upper, 1830120, 1000000);
    check_inside(&result.imaginary, &lower, &upper);

    PHY_CHECK_EQ_INT(
        phy_complex_ball_set_i64(&argument, 0, 1, 0, 1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_complex_ball_gamma(&argument, 80u, &result), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(phy_complex_ball_validate(&result), PHY_OK);

    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
    phy_complex_ball_destroy(&result);
    phy_complex_ball_destroy(&argument);
    PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);
    phy_exact_context_destroy(exact);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_certified_ball_arithmetic);
    PHY_TEST_CASE(test_ball_allocation_failures_are_transactional);
    PHY_TEST_CASE(test_certified_elementary_functions);
    PHY_TEST_CASE(test_elementary_allocation_failures_are_transactional);
    PHY_TEST_CASE(test_certified_inverse_hyperbolic_and_special_functions);
    PHY_TEST_CASE(test_certified_complex_ball_principal_branches);
    PHY_TEST_CASE(test_complex_ball_allocation_failure_is_transactional);
    PHY_TEST_CASE(test_certified_complex_special_functions);
    return PHY_TEST_REPORT("test_ball");
}
