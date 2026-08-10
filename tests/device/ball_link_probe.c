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

    phy_bigrat_destroy(&exact_value);
    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
    phy_real_ball_destroy(&result);
    phy_real_ball_destroy(&b);
    phy_real_ball_destroy(&a);
    phy_exact_context_destroy(exact);
    return g_phy_ball_probe_sink != 0u ? 0 : 4;
}
