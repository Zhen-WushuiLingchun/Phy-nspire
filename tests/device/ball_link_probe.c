/* Device link probe for every certified real-ball entry point. */
#include "phy/ball.h"

volatile unsigned g_phy_ball_probe_sink;

static void sink(unsigned value)
{
    g_phy_ball_probe_sink += value;
}

int main(void)
{
    phy_exact_context *exact = phy_exact_context_create(0);
    if (exact == 0) {
        return 1;
    }
    phy_real_ball a;
    phy_real_ball b;
    phy_real_ball result;
    if (phy_real_ball_init(exact, &a) != PHY_OK ||
        phy_real_ball_init(exact, &b) != PHY_OK ||
        phy_real_ball_init(exact, &result) != PHY_OK) {
        phy_exact_context_destroy(exact);
        return 2;
    }
    sink((unsigned)phy_real_ball_validate(&a));
    sink((unsigned)phy_real_ball_set_i64(&a, 1, 2));
    sink((unsigned)phy_real_ball_set_i64(&b, 3, 2));

    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat exact_value;
    if (phy_bigrat_init(exact, &lower) != PHY_OK ||
        phy_bigrat_init(exact, &upper) != PHY_OK ||
        phy_bigrat_init(exact, &exact_value) != PHY_OK) {
        phy_real_ball_destroy(&result);
        phy_real_ball_destroy(&b);
        phy_real_ball_destroy(&a);
        phy_exact_context_destroy(exact);
        return 3;
    }
    sink((unsigned)phy_bigrat_set_i64(&lower, 1, 1));
    sink((unsigned)phy_bigrat_set_i64(&upper, 2, 1));
    sink((unsigned)phy_bigrat_set_i64(&exact_value, 2, 1));
    sink((unsigned)phy_real_ball_set_interval(&a, &lower, &upper));
    sink((unsigned)phy_real_ball_set_exact(&b, &exact_value));
    sink((unsigned)phy_real_ball_copy(&a, &result));
    sink((unsigned)phy_real_ball_lower(&a, &lower));
    sink((unsigned)phy_real_ball_upper(&a, &upper));
    bool contains_zero = true;
    sink((unsigned)phy_real_ball_contains_zero_checked(
        &a, &contains_zero));
    sink(phy_real_ball_contains_zero(&a) ? 1u : 0u);
    sink((unsigned)phy_real_ball_add(&a, &b, &result));
    sink((unsigned)phy_real_ball_subtract(&a, &b, &result));
    sink((unsigned)phy_real_ball_multiply(&a, &b, &result));
    sink((unsigned)phy_real_ball_divide(&a, &b, &result));
    sink((unsigned)phy_real_ball_pow_i32(&a, 2, &result));
    sink((unsigned)phy_real_ball_sqrt(&b, 8u, &result));
    sink((unsigned)phy_real_ball_exp(&a, 8u, &result));
    sink((unsigned)phy_real_ball_log(&b, 8u, &result));
    sink((unsigned)phy_real_ball_sin(&a, 8u, &result));
    sink((unsigned)phy_real_ball_cos(&a, 8u, &result));
    sink((unsigned)phy_real_ball_tan(&a, 8u, &result));
    sink((unsigned)phy_real_ball_sinh(&a, 8u, &result));
    sink((unsigned)phy_real_ball_cosh(&a, 8u, &result));
    sink((unsigned)phy_real_ball_tanh(&a, 8u, &result));
    sink((unsigned)phy_real_ball_asin(&a, 8u, &result));
    sink((unsigned)phy_real_ball_acos(&a, 8u, &result));
    sink((unsigned)phy_real_ball_atan(&a, 8u, &result));
    sink((unsigned)phy_real_ball_asinh(&a, 8u, &result));
    sink((unsigned)phy_real_ball_acosh(&b, 8u, &result));
    sink((unsigned)phy_real_ball_atanh(&a, 8u, &result));
    sink((unsigned)phy_real_ball_erf(&a, 8u, &result));
    sink((unsigned)phy_real_ball_erfc(&a, 8u, &result));

    phy_complex_ball z = {0};
    phy_complex_ball w = {0};
    phy_complex_ball complex_result = {0};
    if (phy_complex_ball_init(exact, &z) != PHY_OK ||
        phy_complex_ball_init(exact, &w) != PHY_OK ||
        phy_complex_ball_init(exact, &complex_result) != PHY_OK) {
        phy_complex_ball_destroy(&complex_result);
        phy_complex_ball_destroy(&w);
        phy_complex_ball_destroy(&z);
        phy_bigrat_destroy(&exact_value);
        phy_bigrat_destroy(&upper);
        phy_bigrat_destroy(&lower);
        phy_real_ball_destroy(&result);
        phy_real_ball_destroy(&b);
        phy_real_ball_destroy(&a);
        phy_exact_context_destroy(exact);
        return 4;
    }
    sink((unsigned)phy_complex_ball_validate(&z));
    sink((unsigned)phy_complex_ball_set_i64(&z, 1, 1, 1, 1));
    sink((unsigned)phy_complex_ball_set_real(&w, &b));
    sink((unsigned)phy_complex_ball_copy(&z, &complex_result));
    sink((unsigned)phy_complex_ball_contains_zero_checked(
        &z, &contains_zero));
    sink(phy_complex_ball_contains_zero(&z) ? 1u : 0u);
    sink((unsigned)phy_complex_ball_add(&z, &w, &complex_result));
    sink((unsigned)phy_complex_ball_subtract(&z, &w, &complex_result));
    sink((unsigned)phy_complex_ball_multiply(&z, &w, &complex_result));
    sink((unsigned)phy_complex_ball_divide(&z, &w, &complex_result));
    sink((unsigned)phy_complex_ball_conjugate(&z, &complex_result));
    sink((unsigned)phy_complex_ball_pow_i32(&z, 2, &complex_result));
    sink((unsigned)phy_complex_ball_sqrt(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_exp(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_log(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_sin(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_cos(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_tan(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_sinh(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_cosh(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_tanh(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_asin(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_acos(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_atan(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_asinh(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_acosh(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_atanh(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_erf(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_erfc(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_loggamma(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_gamma(&z, 8u, &complex_result));
    sink((unsigned)phy_complex_ball_digamma(&z, 8u, &complex_result));
    phy_complex_ball_destroy(&complex_result);
    phy_complex_ball_destroy(&w);
    phy_complex_ball_destroy(&z);

    phy_bigrat_destroy(&exact_value);
    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
    phy_real_ball_destroy(&result);
    phy_real_ball_destroy(&b);
    phy_real_ball_destroy(&a);
    phy_exact_context_destroy(exact);
    return g_phy_ball_probe_sink != 0u ? 0 : 5;
}
