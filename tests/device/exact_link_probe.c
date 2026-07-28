/*
 * Device link probe for every public bigint/bigrat entry point.
 *
 * Host tests prove arithmetic. This probe separately proves that the complete
 * API resolves against the 32-bit Ndless ABI and retains no floating-point
 * runtime dependency.
 */
#include "phy/exact.h"

volatile unsigned g_phy_exact_probe_sink;

static void sink(unsigned value)
{
    g_phy_exact_probe_sink += value;
}

static bool never_cancel(void *user)
{
    (void)user;
    return false;
}

int main(void)
{
    phy_exact_limits limits;
    phy_exact_limits_defaults(&limits);
    phy_exact_context *context = phy_exact_context_create(&limits);
    if (context == 0) {
        return 1;
    }
    phy_exact_set_cancel(context, never_cancel, 0);
    phy_exact_set_cancel(context, 0, 0);
    sink(phy_exact_steps(context));
    sink((unsigned)phy_exact_total_steps(context));
    sink((unsigned)phy_exact_bytes_used(context));
    sink((unsigned)phy_exact_validate(context));

    phy_bigint a;
    phy_bigint b;
    phy_bigint result;
    phy_bigint quotient;
    phy_bigint remainder;
    if (phy_bigint_init(context, &a) != PHY_OK ||
        phy_bigint_init(context, &b) != PHY_OK ||
        phy_bigint_init(context, &result) != PHY_OK ||
        phy_bigint_init(context, &quotient) != PHY_OK ||
        phy_bigint_init(context, &remainder) != PHY_OK) {
        phy_exact_context_destroy(context);
        return 2;
    }
    sink(phy_bigint_is_initialized(&a) ? 1u : 0u);
    sink((unsigned)phy_bigint_validate(&a));
    sink((unsigned)phy_bigint_set_i64(&a, 42));
    sink((unsigned)phy_bigint_read(&b, "18446744073709551616"));
    size_t required = 0u;
    sink((unsigned)phy_bigint_write(&b, 0, 0u, &required));
    sink((unsigned)required);
    sink((unsigned)phy_bigint_copy(&b, &result));
    sink((unsigned)(phy_bigint_sign(&result) + 1));
    sink((unsigned)phy_bigint_bit_length(&result));
    int64_t small = 0;
    sink(phy_bigint_try_i64(&a, &small) ? (unsigned)small : 0u);
    sink((unsigned)(phy_bigint_compare(&a, &b) + 1));
    sink((unsigned)(phy_bigint_compare_abs(&a, &b) + 1));
    sink((unsigned)phy_bigint_negate(&a, &result));
    sink((unsigned)phy_bigint_add(&a, &b, &result));
    sink((unsigned)phy_bigint_subtract(&b, &a, &result));
    sink((unsigned)phy_bigint_multiply(&a, &b, &result));
    sink((unsigned)phy_bigint_pow_u32(&a, 3u, &result));
    sink((unsigned)phy_bigint_divmod(
        &b, &a, &quotient, &remainder));
    sink((unsigned)phy_bigint_divide_exact(&b, &a, &quotient));
    sink((unsigned)phy_bigint_gcd(&a, &b, &result));

    phy_bigrat x;
    phy_bigrat y;
    phy_bigrat z;
    if (phy_bigrat_init(context, &x) != PHY_OK ||
        phy_bigrat_init(context, &y) != PHY_OK ||
        phy_bigrat_init(context, &z) != PHY_OK) {
        phy_exact_context_destroy(context);
        return 3;
    }
    sink((unsigned)phy_bigrat_validate(&x));
    sink((unsigned)phy_bigrat_set_i64(&x, 2, 3));
    sink((unsigned)phy_bigrat_read(
        &y, "18446744073709551616", "5"));
    sink((unsigned)phy_bigrat_write(&y, 0, 0u, &required));
    sink((unsigned)phy_bigrat_copy(&y, &z));
    sink((unsigned)phy_bigrat_add(&x, &y, &z));
    sink((unsigned)phy_bigrat_negate(&x, &z));
    sink((unsigned)phy_bigrat_subtract(&y, &x, &z));
    sink((unsigned)phy_bigrat_multiply(&x, &y, &z));
    sink((unsigned)phy_bigrat_reciprocal(&x, &z));
    sink((unsigned)phy_bigrat_divide(&y, &x, &z));
    sink((unsigned)phy_bigrat_pow_i32(&x, -2, &z));
    int comparison = 0;
    sink((unsigned)phy_bigrat_compare(&x, &y, &comparison));
    sink((unsigned)(comparison + 1));
    sink((unsigned)(phy_bigrat_sign(&x) + 1));
    sink((unsigned)phy_bigrat_set_bigint(&b, &z));
    sink((unsigned)phy_bigrat_swap(&x, &z));
    sink(phy_bigrat_numerator(&x) != 0 ? 1u : 0u);
    sink(phy_bigrat_denominator(&x) != 0 ? 1u : 0u);
    int64_t numerator = 0;
    int64_t denominator = 0;
    sink(phy_bigrat_try_i64(
             &z, &numerator, &denominator)
             ? (unsigned)(numerator + denominator)
             : 0u);

    phy_bigrat_destroy(&z);
    phy_bigrat_destroy(&y);
    phy_bigrat_destroy(&x);
    phy_bigint_destroy(&remainder);
    phy_bigint_destroy(&quotient);
    phy_bigint_destroy(&result);
    phy_bigint_destroy(&b);
    phy_bigint_destroy(&a);
    phy_exact_context_destroy(context);
    return g_phy_exact_probe_sink != 0u ? 0 : 4;
}
