/*
 * Native arbitrary-precision integer and rational foundation.
 *
 * These tests deliberately use decimal strings rather than a host bignum
 * library. The expected values are fixed golden arithmetic, so the same suite
 * compiles unchanged under MSVC, sanitizers, and the Ndless ARM toolchain.
 */
#include <string.h>

#include "phy/exact.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy_test.h"

typedef struct {
    phy_exact_context *exact;
} fixture;

static fixture fixture_open(void)
{
    fixture f;
    f.exact = NULL;
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    f.exact = phy_exact_context_create(NULL);
    PHY_CHECK(f.exact != NULL);
    return f;
}

static void fixture_close(fixture *f)
{
    phy_exact_context_destroy(f->exact);
    f->exact = NULL;
    phy_platform_shutdown();
}

static const char *integer_text(const phy_bigint *value)
{
    static char buffers[4][2048];
    static unsigned next;
    char *buffer = buffers[next++ & 3u];
    size_t required = 0u;
    const phy_status status =
        phy_bigint_write(value, buffer, sizeof buffers[0], &required);
    if (status != PHY_OK) {
        return "<write failed>";
    }
    return buffer;
}

static const char *rational_text(const phy_bigrat *value)
{
    static char buffers[4][4096];
    static unsigned next;
    char *buffer = buffers[next++ & 3u];
    size_t required = 0u;
    const phy_status status =
        phy_bigrat_write(value, buffer, sizeof buffers[0], &required);
    if (status != PHY_OK) {
        return "<write failed>";
    }
    return buffer;
}

static void read_integer(phy_bigint *value, const char *text)
{
    PHY_CHECK_EQ_INT(phy_bigint_read(value, text), PHY_OK);
}

static void read_rational(phy_bigrat *value, const char *numerator,
                          const char *denominator)
{
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(value, numerator, denominator), PHY_OK);
}

static bool cancel_now(void *user)
{
    unsigned *calls = (unsigned *)user;
    (*calls)++;
    return true;
}

static void test_context_lifecycle_and_limits(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_telemetry before;
    phy_telemetry_get(&before);

    phy_exact_limits defaults;
    phy_exact_limits_defaults(&defaults);
    PHY_CHECK(defaults.max_limbs >= 2u);
    PHY_CHECK(defaults.max_steps > 0u);
    PHY_CHECK(defaults.max_bytes > 0u);

    phy_exact_context *exact = phy_exact_context_create(NULL);
    PHY_CHECK(exact != NULL);
    PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);
    PHY_CHECK(phy_exact_bytes_used(exact) > 0u);
    PHY_CHECK_EQ_INT(phy_exact_steps(exact), 0);
    PHY_CHECK_EQ_INT(phy_exact_total_steps(exact), 0);

    phy_bigint value;
    PHY_CHECK_EQ_INT(phy_bigint_init(exact, &value), PHY_OK);
    read_integer(&value, "18446744073709551616");
    PHY_CHECK_EQ_STR(integer_text(&value), "18446744073709551616");
    PHY_CHECK_EQ_INT(phy_bigint_validate(&value), PHY_OK);

    phy_telemetry during;
    phy_telemetry_get(&during);
    PHY_CHECK(during.bytes_live > before.bytes_live);

    /* Context destruction owns and invalidates any still-live value. */
    phy_exact_context_destroy(exact);
    PHY_CHECK(!phy_bigint_is_initialized(&value));

    phy_telemetry after;
    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(after.bytes_live, before.bytes_live);

    phy_exact_limits tiny = defaults;
    tiny.max_bytes = 16u;
    PHY_CHECK(phy_exact_context_create(&tiny) == NULL);
    phy_exact_context_destroy(NULL);
    phy_bigint_destroy(NULL);

    phy_platform_shutdown();
}

static void test_integer_parse_write_and_transactionality(void)
{
    fixture f = fixture_open();
    phy_bigint value;
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &value), PHY_OK);

    static const char *const canonical[][2] = {
        {"0", "0"},
        {"000000", "0"},
        {"+00000123", "123"},
        {"-00000123", "-123"},
        {"9223372036854775807", "9223372036854775807"},
        {"9223372036854775808", "9223372036854775808"},
        {"-9223372036854775809", "-9223372036854775809"},
        {"18446744073709551616", "18446744073709551616"},
        {"340282366920938463463374607431768211456",
         "340282366920938463463374607431768211456"},
    };
    for (size_t i = 0u; i < sizeof canonical / sizeof canonical[0]; ++i) {
        read_integer(&value, canonical[i][0]);
        PHY_CHECK_EQ_STR(integer_text(&value), canonical[i][1]);
        PHY_CHECK_EQ_INT(phy_bigint_validate(&value), PHY_OK);
    }
    PHY_CHECK_EQ_INT(phy_bigint_bit_length(&value), 129);
    int64_t small = 0;
    PHY_CHECK(!phy_bigint_try_i64(&value, &small));
    read_integer(&value, "-9223372036854775808");
    PHY_CHECK(phy_bigint_try_i64(&value, &small));
    PHY_CHECK_EQ_INT(small, INT64_MIN);
    read_integer(&value, "9223372036854775808");
    PHY_CHECK(!phy_bigint_try_i64(&value, &small));

    read_integer(&value, "777");
    static const char *const invalid[] = {"", "+", "-", " 1", "1 ", "12x"};
    for (size_t i = 0u; i < sizeof invalid / sizeof invalid[0]; ++i) {
        PHY_CHECK_EQ_INT(phy_bigint_read(&value, invalid[i]), PHY_ERR_PARSE);
        PHY_CHECK_EQ_STR(integer_text(&value), "777");
    }

    char tiny[3] = {'x', 'x', '\0'};
    size_t required = 0u;
    PHY_CHECK_EQ_INT(
        phy_bigint_write(&value, tiny, sizeof tiny, &required),
        PHY_ERR_MEMORY_LIMIT);
    PHY_CHECK_EQ_INT(required, 4);
    PHY_CHECK_EQ_STR(tiny, "xx");
    PHY_CHECK_EQ_INT(
        phy_bigint_write(&value, NULL, 0u, &required), PHY_OK);
    PHY_CHECK_EQ_INT(required, 4);

    phy_bigint_destroy(&value);
    fixture_close(&f);
}

static void test_integer_add_subtract_and_aliasing(void)
{
    fixture f = fixture_open();
    phy_bigint a, b, result;
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &result), PHY_OK);

    read_integer(&a, "999999999999999999999999999999");
    read_integer(&b, "1");
    PHY_CHECK_EQ_INT(phy_bigint_add(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(integer_text(&result),
                     "1000000000000000000000000000000");
    PHY_CHECK_EQ_INT(phy_bigint_subtract(&result, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(integer_text(&result),
                     "999999999999999999999999999999");

    read_integer(&b, "-1000000000000000000000000000001");
    PHY_CHECK_EQ_INT(phy_bigint_add(&a, &b, &a), PHY_OK);
    PHY_CHECK_EQ_STR(integer_text(&a), "-2");
    PHY_CHECK_EQ_INT(phy_bigint_subtract(&a, &a, &a), PHY_OK);
    PHY_CHECK_EQ_STR(integer_text(&a), "0");
    PHY_CHECK_EQ_INT(phy_bigint_sign(&a), 0);

    phy_bigint_destroy(&result);
    phy_bigint_destroy(&b);
    phy_bigint_destroy(&a);
    fixture_close(&f);
}

static void test_integer_multiply_and_power(void)
{
    fixture f = fixture_open();
    phy_bigint a, b, result;
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &result), PHY_OK);

    read_integer(&a, "1234567890123456789012345678901234567890");
    read_integer(&b, "98765432109876543210987654321");
    PHY_CHECK_EQ_INT(phy_bigint_multiply(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(
        integer_text(&result),
        "121932631137021795226185032733744855963362292333223746380111126352690");

    PHY_CHECK_EQ_INT(phy_bigint_set_i64(&a, 2), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_pow_u32(&a, 256u, &a), PHY_OK);
    PHY_CHECK_EQ_STR(
        integer_text(&a),
        "115792089237316195423570985008687907853269984665640564039457584007913129639936");
    PHY_CHECK_EQ_INT(phy_bigint_bit_length(&a), 257);

    PHY_CHECK_EQ_INT(phy_bigint_pow_u32(&b, 0u, &result), PHY_OK);
    PHY_CHECK_EQ_STR(integer_text(&result), "1");

    /* The default 256-limb ceiling admits 2^8191 but not one more bit. */
    PHY_CHECK_EQ_INT(phy_bigint_set_i64(&a, 2), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_pow_u32(&a, 8191u, &result), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_bit_length(&result), 8192);
    size_t required = 0u;
    PHY_CHECK_EQ_INT(
        phy_bigint_write(&result, NULL, 0u, &required), PHY_OK);
    PHY_CHECK(required > 2400u);
    PHY_CHECK_EQ_INT(
        phy_bigint_multiply(&result, &a, &result), PHY_ERR_MEMORY_LIMIT);
    PHY_CHECK_EQ_INT(phy_bigint_bit_length(&result), 8192);

    phy_bigint_destroy(&result);
    phy_bigint_destroy(&b);
    phy_bigint_destroy(&a);
    fixture_close(&f);
}

static void test_integer_division_and_gcd(void)
{
    fixture f = fixture_open();
    phy_bigint a, b, q, r, gcd, recomposed, product;
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &q), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &r), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &gcd), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &recomposed), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &product), PHY_OK);

    read_integer(&a, "1234567890123456789012345678901234567890");
    read_integer(&b, "98765432109876543210987654321");
    PHY_CHECK_EQ_INT(phy_bigint_divmod(&a, &b, &q, &r), PHY_OK);
    PHY_CHECK_EQ_STR(integer_text(&q), "12499999886");
    PHY_CHECK_EQ_STR(integer_text(&r), "9259259400925925941327160484");
    PHY_CHECK_EQ_INT(phy_bigint_multiply(&q, &b, &product), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_add(&product, &r, &recomposed), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_compare(&recomposed, &a), 0);
    PHY_CHECK(phy_bigint_compare_abs(&r, &b) < 0);

    PHY_CHECK_EQ_INT(phy_bigint_negate(&a, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_divmod(&a, &b, &q, &r), PHY_OK);
    PHY_CHECK_EQ_STR(integer_text(&q), "-12499999886");
    PHY_CHECK_EQ_STR(integer_text(&r), "-9259259400925925941327160484");

    PHY_CHECK_EQ_INT(phy_bigint_negate(&a, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_negate(&b, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_divmod(&a, &b, &q, &r), PHY_OK);
    PHY_CHECK_EQ_STR(integer_text(&q), "-12499999886");
    PHY_CHECK_EQ_STR(integer_text(&r), "9259259400925925941327160484");

    PHY_CHECK_EQ_INT(phy_bigint_negate(&b, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_gcd(&a, &b, &gcd), PHY_OK);
    PHY_CHECK_EQ_STR(integer_text(&gcd), "9");

    read_integer(&q, "111");
    read_integer(&r, "222");
    PHY_CHECK_EQ_INT(phy_bigint_set_i64(&b, 0), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_divmod(&a, &b, &q, &r), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_STR(integer_text(&q), "111");
    PHY_CHECK_EQ_STR(integer_text(&r), "222");

    phy_bigint_destroy(&product);
    phy_bigint_destroy(&recomposed);
    phy_bigint_destroy(&gcd);
    phy_bigint_destroy(&r);
    phy_bigint_destroy(&q);
    phy_bigint_destroy(&b);
    phy_bigint_destroy(&a);
    fixture_close(&f);
}

static int64_t model_gcd(int64_t left, int64_t right)
{
    left = left < 0 ? -left : left;
    right = right < 0 ? -right : right;
    while (right != 0) {
        const int64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

static void test_integer_small_model_exhaustive(void)
{
    fixture f = fixture_open();
    phy_bigint a, b, result, quotient, remainder;
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &result), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &quotient), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(f.exact, &remainder), PHY_OK);

    for (int64_t left = -32; left <= 32; ++left) {
        PHY_CHECK_EQ_INT(phy_bigint_set_i64(&a, left), PHY_OK);
        for (int64_t right = -32; right <= 32; ++right) {
            PHY_CHECK_EQ_INT(phy_bigint_set_i64(&b, right), PHY_OK);
            int64_t actual = 0;

            PHY_CHECK_EQ_INT(
                phy_bigint_add(&a, &b, &result), PHY_OK);
            PHY_CHECK(phy_bigint_try_i64(&result, &actual));
            PHY_CHECK_EQ_INT(actual, left + right);

            PHY_CHECK_EQ_INT(
                phy_bigint_subtract(&a, &b, &result), PHY_OK);
            PHY_CHECK(phy_bigint_try_i64(&result, &actual));
            PHY_CHECK_EQ_INT(actual, left - right);

            PHY_CHECK_EQ_INT(
                phy_bigint_multiply(&a, &b, &result), PHY_OK);
            PHY_CHECK(phy_bigint_try_i64(&result, &actual));
            PHY_CHECK_EQ_INT(actual, left * right);

            PHY_CHECK_EQ_INT(
                phy_bigint_gcd(&a, &b, &result), PHY_OK);
            PHY_CHECK(phy_bigint_try_i64(&result, &actual));
            PHY_CHECK_EQ_INT(actual, model_gcd(left, right));

            if (right != 0) {
                PHY_CHECK_EQ_INT(
                    phy_bigint_divmod(
                        &a, &b, &quotient, &remainder),
                    PHY_OK);
                PHY_CHECK(phy_bigint_try_i64(&quotient, &actual));
                PHY_CHECK_EQ_INT(actual, left / right);
                PHY_CHECK(phy_bigint_try_i64(&remainder, &actual));
                PHY_CHECK_EQ_INT(actual, left % right);
            }
        }
    }

    phy_bigint_destroy(&remainder);
    phy_bigint_destroy(&quotient);
    phy_bigint_destroy(&result);
    phy_bigint_destroy(&b);
    phy_bigint_destroy(&a);
    fixture_close(&f);
}

static void test_integer_resource_and_cancel_contracts(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_exact_limits limits;
    phy_exact_limits_defaults(&limits);
    limits.max_steps = 8u;
    limits.max_limbs = 64u;
    phy_exact_context *exact = phy_exact_context_create(&limits);
    PHY_CHECK(exact != NULL);

    phy_bigint a, result;
    PHY_CHECK_EQ_INT(phy_bigint_init(exact, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(exact, &result), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_set_i64(&a, 2), PHY_OK);
    read_integer(&result, "77");
    PHY_CHECK_EQ_INT(
        phy_bigint_pow_u32(&a, 1024u, &result), PHY_ERR_TIMEOUT);
    PHY_CHECK_EQ_STR(integer_text(&result), "77");
    PHY_CHECK_EQ_INT(phy_bigint_validate(&a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_validate(&result), PHY_OK);

    phy_bigint_destroy(&result);
    phy_bigint_destroy(&a);
    phy_exact_context_destroy(exact);

    phy_exact_limits_defaults(&limits);
    exact = phy_exact_context_create(&limits);
    PHY_CHECK(exact != NULL);
    PHY_CHECK_EQ_INT(phy_bigint_init(exact, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_init(exact, &result), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigint_set_i64(&a, 2), PHY_OK);
    read_integer(&result, "88");
    unsigned calls = 0u;
    phy_exact_set_cancel(exact, cancel_now, &calls);
    PHY_CHECK_EQ_INT(
        phy_bigint_pow_u32(&a, 1024u, &result), PHY_ERR_INTERRUPTED);
    PHY_CHECK(calls > 0u);
    phy_exact_set_cancel(exact, NULL, NULL);
    PHY_CHECK_EQ_STR(integer_text(&result), "88");

    phy_bigint_destroy(&result);
    phy_bigint_destroy(&a);
    phy_exact_context_destroy(exact);
    phy_platform_shutdown();
}

static void test_rational_normalization_and_arithmetic(void)
{
    fixture f = fixture_open();
    phy_bigrat a, b, result;
    PHY_CHECK_EQ_INT(phy_bigrat_init(f.exact, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(f.exact, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_init(f.exact, &result), PHY_OK);

    read_rational(&a, "123456789012345678901234567890",
                  "98765432109876543210");
    PHY_CHECK_EQ_STR(
        rational_text(&a),
        "1371742100137174210013717421/1097393690109739369");
    PHY_CHECK_EQ_INT(phy_bigrat_validate(&a), PHY_OK);

    read_rational(&b, "5", "7");
    PHY_CHECK_EQ_INT(phy_bigrat_add(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(
        rational_text(&result),
        "9602194706447187920644718792/7681755830768175583");
    PHY_CHECK_EQ_INT(phy_bigrat_multiply(&a, &b, &result), PHY_OK);
    PHY_CHECK_EQ_STR(
        rational_text(&result),
        "979815785812267292866941015/1097393690109739369");

    PHY_CHECK_EQ_INT(phy_bigrat_reciprocal(&a, &result), PHY_OK);
    PHY_CHECK_EQ_INT(phy_bigrat_multiply(&a, &result, &result), PHY_OK);
    PHY_CHECK_EQ_STR(rational_text(&result), "1");

    read_rational(&a, "-111111111111111111111111111111",
                  "-222222222222222222222222222222");
    PHY_CHECK_EQ_STR(rational_text(&a), "1/2");
    PHY_CHECK_EQ_INT(phy_bigrat_pow_i32(&a, -3, &a), PHY_OK);
    PHY_CHECK_EQ_STR(rational_text(&a), "8");
    int comparison = 0;
    PHY_CHECK_EQ_INT(phy_bigrat_compare(&a, &b, &comparison), PHY_OK);
    PHY_CHECK_EQ_INT(comparison, 1);
    int64_t numerator = 0;
    int64_t denominator = 0;
    PHY_CHECK(phy_bigrat_try_i64(
        &a, &numerator, &denominator));
    PHY_CHECK_EQ_INT(numerator, 8);
    PHY_CHECK_EQ_INT(denominator, 1);

    read_rational(&result, "7", "9");
    PHY_CHECK_EQ_INT(
        phy_bigrat_read(&result, "1", "0"), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_STR(rational_text(&result), "7/9");

    phy_bigrat_destroy(&result);
    phy_bigrat_destroy(&b);
    phy_bigrat_destroy(&a);
    fixture_close(&f);
}

static void test_allocation_failure_is_transactional(void)
{
    bool reached_success = false;
    for (unsigned countdown = 1u; countdown <= 32u; ++countdown) {
        PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
        phy_telemetry before;
        phy_telemetry_get(&before);

        phy_exact_context *exact = phy_exact_context_create(NULL);
        PHY_CHECK(exact != NULL);
        phy_bigint a, b, result;
        PHY_CHECK_EQ_INT(phy_bigint_init(exact, &a), PHY_OK);
        PHY_CHECK_EQ_INT(phy_bigint_init(exact, &b), PHY_OK);
        PHY_CHECK_EQ_INT(phy_bigint_init(exact, &result), PHY_OK);
        read_integer(&a, "1234567890123456789012345678901234567890");
        read_integer(&b, "98765432109876543210987654321");
        read_integer(&result, "123");

        phy_host_fail_alloc_after(countdown);
        const phy_status status = phy_bigint_multiply(&a, &b, &result);
        PHY_CHECK(status == PHY_OK || status == PHY_ERR_OUT_OF_MEMORY);
        PHY_CHECK_EQ_INT(phy_bigint_validate(&a), PHY_OK);
        PHY_CHECK_EQ_INT(phy_bigint_validate(&b), PHY_OK);
        PHY_CHECK_EQ_INT(phy_bigint_validate(&result), PHY_OK);
        if (status == PHY_OK) {
            reached_success = true;
        } else {
            PHY_CHECK_EQ_STR(integer_text(&result), "123");
        }
        PHY_CHECK_EQ_INT(phy_exact_validate(exact), PHY_OK);

        phy_bigint_destroy(&result);
        phy_bigint_destroy(&b);
        phy_bigint_destroy(&a);
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
    PHY_TEST_CASE(test_context_lifecycle_and_limits);
    PHY_TEST_CASE(test_integer_parse_write_and_transactionality);
    PHY_TEST_CASE(test_integer_add_subtract_and_aliasing);
    PHY_TEST_CASE(test_integer_multiply_and_power);
    PHY_TEST_CASE(test_integer_division_and_gcd);
    PHY_TEST_CASE(test_integer_small_model_exhaustive);
    PHY_TEST_CASE(test_integer_resource_and_cancel_contracts);
    PHY_TEST_CASE(test_rational_normalization_and_arithmetic);
    PHY_TEST_CASE(test_allocation_failure_is_transactional);
    return PHY_TEST_REPORT("test_exact");
}
