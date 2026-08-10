#include "phy/ball.h"

#include <string.h>

#define PHY_REAL_BALL_MAGIC UINT32_C(0x42414c4c)

static bool initialized(const phy_real_ball *ball)
{
    return ball != NULL && ball->private_magic == PHY_REAL_BALL_MAGIC &&
           phy_bigint_is_initialized(phy_bigrat_numerator(&ball->midpoint)) &&
           phy_bigint_is_initialized(phy_bigrat_numerator(&ball->radius));
}

static phy_exact_context *context_of(const phy_real_ball *ball)
{
    return phy_bigrat_numerator(&ball->midpoint)->context;
}

static phy_status init_like(const phy_real_ball *ball, phy_real_ball *out)
{
    return phy_real_ball_init(context_of(ball), out);
}

static phy_status publish(phy_real_ball *temporary, phy_real_ball *out)
{
    phy_status status = phy_bigrat_swap(&temporary->midpoint, &out->midpoint);
    if (status == PHY_OK) {
        status = phy_bigrat_swap(&temporary->radius, &out->radius);
    }
    return status;
}

phy_status phy_real_ball_init(phy_exact_context *context, phy_real_ball *ball)
{
    if (context == NULL || ball == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(ball, 0, sizeof *ball);
    phy_status status = phy_bigrat_init(context, &ball->midpoint);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &ball->radius);
    }
    if (status != PHY_OK) {
        if (phy_bigint_is_initialized(phy_bigrat_numerator(&ball->radius))) {
            phy_bigrat_destroy(&ball->radius);
        }
        if (phy_bigint_is_initialized(phy_bigrat_numerator(&ball->midpoint))) {
            phy_bigrat_destroy(&ball->midpoint);
        }
        memset(ball, 0, sizeof *ball);
        return status;
    }
    ball->private_magic = PHY_REAL_BALL_MAGIC;
    status = phy_bigrat_set_i64(&ball->midpoint, 0, 1);
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&ball->radius, 0, 1);
    }
    if (status != PHY_OK) {
        phy_real_ball_destroy(ball);
    }
    return status;
}

void phy_real_ball_destroy(phy_real_ball *ball)
{
    if (ball == NULL) {
        return;
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&ball->radius))) {
        phy_bigrat_destroy(&ball->radius);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&ball->midpoint))) {
        phy_bigrat_destroy(&ball->midpoint);
    }
    memset(ball, 0, sizeof *ball);
}

phy_status phy_real_ball_validate(const phy_real_ball *ball)
{
    if (!initialized(ball)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_bigrat_validate(&ball->midpoint);
    if (status == PHY_OK) {
        status = phy_bigrat_validate(&ball->radius);
    }
    if (status == PHY_OK && phy_bigrat_sign(&ball->radius) < 0) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    return status;
}

phy_status phy_real_ball_set_i64(phy_real_ball *ball, int64_t numerator,
                                 int64_t denominator)
{
    if (!initialized(ball)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_real_ball temporary;
    phy_status status = init_like(ball, &temporary);
    if (status == PHY_OK) {
        status =
            phy_bigrat_set_i64(&temporary.midpoint, numerator, denominator);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, ball);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_set_exact(phy_real_ball *ball, const phy_bigrat *value)
{
    if (!initialized(ball) || value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_real_ball temporary;
    phy_status status = init_like(ball, &temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_copy(value, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, ball);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_set_interval(phy_real_ball *ball,
                                      const phy_bigrat *lower,
                                      const phy_bigrat *upper)
{
    if (!initialized(ball) || lower == NULL || upper == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    int order = 0;
    phy_status status = phy_bigrat_compare(lower, upper, &order);
    if (status != PHY_OK || order > 0) {
        return status == PHY_OK ? PHY_ERR_DOMAIN : status;
    }
    phy_real_ball temporary;
    phy_bigrat two;
    memset(&temporary, 0, sizeof temporary);
    memset(&two, 0, sizeof two);
    status = init_like(ball, &temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context_of(ball), &two);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&two, 2, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(lower, upper, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status =
            phy_bigrat_divide(&temporary.midpoint, &two, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_subtract(upper, lower, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(&temporary.radius, &two, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, ball);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&two))) {
        phy_bigrat_destroy(&two);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_copy(const phy_real_ball *source,
                              phy_real_ball *destination)
{
    if (!initialized(source) || !initialized(destination)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_real_ball temporary;
    phy_status status = init_like(destination, &temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_copy(&source->midpoint, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(&source->radius, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, destination);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_lower(const phy_real_ball *ball, phy_bigrat *out_lower)
{
    if (!initialized(ball) || out_lower == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_bigrat_subtract(&ball->midpoint, &ball->radius, out_lower);
}

phy_status phy_real_ball_upper(const phy_real_ball *ball, phy_bigrat *out_upper)
{
    if (!initialized(ball) || out_upper == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_bigrat_add(&ball->midpoint, &ball->radius, out_upper);
}

phy_status phy_real_ball_contains_zero_checked(const phy_real_ball *ball,
                                               bool *out_contains_zero)
{
    if (!initialized(ball) || out_contains_zero == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_contains_zero = true;
    phy_bigrat magnitude;
    memset(&magnitude, 0, sizeof magnitude);
    phy_status status = phy_bigrat_init(context_of(ball), &magnitude);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_bigrat_copy(&ball->midpoint, &magnitude);
    if (status == PHY_OK && phy_bigrat_sign(&magnitude) < 0) {
        status = phy_bigrat_negate(&magnitude, &magnitude);
    }
    int order = 1;
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&magnitude, &ball->radius, &order);
    }
    phy_bigrat_destroy(&magnitude);
    if (status == PHY_OK) {
        *out_contains_zero = order <= 0;
    }
    return status;
}

bool phy_real_ball_contains_zero(const phy_real_ball *ball)
{
    bool contains_zero = true;
    return phy_real_ball_contains_zero_checked(ball, &contains_zero) == PHY_OK
               ? contains_zero
               : true;
}

phy_status phy_real_ball_add(const phy_real_ball *left,
                             const phy_real_ball *right, phy_real_ball *out_sum)
{
    if (!initialized(left) || !initialized(right) || !initialized(out_sum)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_real_ball temporary;
    phy_status status = init_like(out_sum, &temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_add(&left->midpoint, &right->midpoint,
                                &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status =
            phy_bigrat_add(&left->radius, &right->radius, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, out_sum);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_subtract(const phy_real_ball *left,
                                  const phy_real_ball *right,
                                  phy_real_ball *out_difference)
{
    if (!initialized(left) || !initialized(right) ||
        !initialized(out_difference)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_real_ball temporary;
    phy_status status = init_like(out_difference, &temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_subtract(&left->midpoint, &right->midpoint,
                                     &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status =
            phy_bigrat_add(&left->radius, &right->radius, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, out_difference);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

static phy_status absolute_rational(const phy_bigrat *value, phy_bigrat *out)
{
    phy_status status = phy_bigrat_copy(value, out);
    if (status == PHY_OK && phy_bigrat_sign(out) < 0) {
        status = phy_bigrat_negate(out, out);
    }
    return status;
}

phy_status phy_real_ball_multiply(const phy_real_ball *left,
                                  const phy_real_ball *right,
                                  phy_real_ball *out_product)
{
    if (!initialized(left) || !initialized(right) ||
        !initialized(out_product)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_real_ball temporary;
    phy_bigrat left_abs;
    phy_bigrat right_abs;
    phy_bigrat first;
    phy_bigrat second;
    phy_bigrat third;
    memset(&temporary, 0, sizeof temporary);
    memset(&left_abs, 0, sizeof left_abs);
    memset(&right_abs, 0, sizeof right_abs);
    memset(&first, 0, sizeof first);
    memset(&second, 0, sizeof second);
    memset(&third, 0, sizeof third);
    phy_status status = init_like(out_product, &temporary);
    phy_bigrat *values[] = {&left_abs, &right_abs, &first, &second, &third};
    size_t initialized_count = 0u;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status =
            phy_bigrat_init(context_of(out_product), values[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&left->midpoint, &right->midpoint,
                                     &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = absolute_rational(&left->midpoint, &left_abs);
    }
    if (status == PHY_OK) {
        status = absolute_rational(&right->midpoint, &right_abs);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&left_abs, &right->radius, &first);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&right_abs, &left->radius, &second);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&left->radius, &right->radius, &third);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(&first, &second, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(&temporary.radius, &third, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, out_product);
    }
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_divide(const phy_real_ball *dividend,
                                const phy_real_ball *divisor,
                                phy_real_ball *out_quotient)
{
    if (!initialized(dividend) || !initialized(divisor) ||
        !initialized(out_quotient)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    bool divisor_contains_zero = true;
    phy_status status =
        phy_real_ball_contains_zero_checked(divisor, &divisor_contains_zero);
    if (status != PHY_OK) {
        return status;
    }
    if (divisor_contains_zero) {
        return PHY_ERR_DOMAIN;
    }
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat reciprocal_lower;
    phy_bigrat reciprocal_upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    memset(&reciprocal_lower, 0, sizeof reciprocal_lower);
    memset(&reciprocal_upper, 0, sizeof reciprocal_upper);
    phy_bigrat *values[] = {&lower, &upper, &reciprocal_lower,
                            &reciprocal_upper};
    status = PHY_OK;
    size_t initialized_count = 0u;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(context_of(out_quotient),
                                 values[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_lower(divisor, &lower);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_upper(divisor, &upper);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_reciprocal(&upper, &reciprocal_lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_reciprocal(&lower, &reciprocal_upper);
    }
    phy_real_ball reciprocal;
    memset(&reciprocal, 0, sizeof reciprocal);
    if (status == PHY_OK) {
        status = init_like(out_quotient, &reciprocal);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_interval(&reciprocal, &reciprocal_lower,
                                            &reciprocal_upper);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(dividend, &reciprocal, out_quotient);
    }
    phy_real_ball_destroy(&reciprocal);
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    return status;
}

phy_status phy_real_ball_pow_i32(const phy_real_ball *base, int32_t exponent,
                                 phy_real_ball *out_power)
{
    if (!initialized(base) || !initialized(out_power)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (exponent < 0) {
        bool base_contains_zero = true;
        phy_status zero_status =
            phy_real_ball_contains_zero_checked(base, &base_contains_zero);
        if (zero_status != PHY_OK) {
            return zero_status;
        }
        if (base_contains_zero) {
            return PHY_ERR_DOMAIN;
        }
    }
    const uint32_t magnitude =
        exponent < 0 ? (uint32_t)(-(int64_t)exponent) : (uint32_t)exponent;
    phy_real_ball result;
    phy_real_ball factor;
    phy_real_ball temporary;
    memset(&result, 0, sizeof result);
    memset(&factor, 0, sizeof factor);
    memset(&temporary, 0, sizeof temporary);
    phy_status status = init_like(out_power, &result);
    if (status == PHY_OK) {
        status = init_like(out_power, &factor);
    }
    if (status == PHY_OK) {
        status = init_like(out_power, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&result, 1, 1);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(base, &factor);
    }
    uint32_t power = magnitude;
    while (status == PHY_OK && power != 0u) {
        if ((power & 1u) != 0u) {
            status = phy_real_ball_multiply(&result, &factor, &temporary);
            if (status == PHY_OK) {
                status = phy_real_ball_copy(&temporary, &result);
            }
        }
        power >>= 1u;
        if (status == PHY_OK && power != 0u) {
            status = phy_real_ball_multiply(&factor, &factor, &temporary);
            if (status == PHY_OK) {
                status = phy_real_ball_copy(&temporary, &factor);
            }
        }
    }
    if (status == PHY_OK && exponent < 0) {
        status = phy_real_ball_set_i64(&factor, 1, 1);
        if (status == PHY_OK) {
            status = phy_real_ball_divide(&factor, &result, &temporary);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_copy(&temporary, &result);
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&result, out_power);
    }
    phy_real_ball_destroy(&temporary);
    phy_real_ball_destroy(&factor);
    phy_real_ball_destroy(&result);
    return status;
}

static phy_status sqrt_interval_bound(const phy_bigrat *value, uint32_t rounds,
                                      bool upper_bound, phy_bigrat *out)
{
    if (phy_bigrat_sign(value) < 0) {
        return PHY_ERR_DOMAIN;
    }
    phy_exact_context *context = phy_bigrat_numerator(value)->context;
    phy_bigrat low;
    phy_bigrat high;
    phy_bigrat one;
    phy_bigrat two;
    phy_bigrat midpoint;
    phy_bigrat square;
    memset(&low, 0, sizeof low);
    memset(&high, 0, sizeof high);
    memset(&one, 0, sizeof one);
    memset(&two, 0, sizeof two);
    memset(&midpoint, 0, sizeof midpoint);
    memset(&square, 0, sizeof square);
    phy_bigrat *values[] = {&low, &high, &one, &two, &midpoint, &square};
    phy_status status = PHY_OK;
    size_t initialized_count = 0u;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(context, values[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&low, 0, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&one, 1, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&two, 2, 1);
    }
    int order = 0;
    if (status == PHY_OK) {
        status = phy_bigrat_compare(value, &one, &order);
    }
    if (status == PHY_OK) {
        status = order > 0 ? phy_bigrat_copy(value, &high)
                           : phy_bigrat_copy(&one, &high);
    }
    for (uint32_t round = 0u; status == PHY_OK && round < rounds; ++round) {
        status = phy_bigrat_add(&low, &high, &midpoint);
        if (status == PHY_OK) {
            status = phy_bigrat_divide(&midpoint, &two, &midpoint);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_multiply(&midpoint, &midpoint, &square);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_compare(&square, value, &order);
        }
        if (status == PHY_OK && order == 0) {
            status = phy_bigrat_copy(&midpoint, &low);
            if (status == PHY_OK) {
                status = phy_bigrat_copy(&midpoint, &high);
            }
            break;
        }
        if (status == PHY_OK && order < 0) {
            status = phy_bigrat_copy(&midpoint, &low);
        } else if (status == PHY_OK) {
            status = phy_bigrat_copy(&midpoint, &high);
        }
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(upper_bound ? &high : &low, out);
    }
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    return status;
}

phy_status phy_real_ball_sqrt(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_root)
{
    if (!initialized(argument) || !initialized(out_root)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = context_of(out_root);
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat root_lower;
    phy_bigrat root_upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    memset(&root_lower, 0, sizeof root_lower);
    memset(&root_upper, 0, sizeof root_upper);
    phy_bigrat *values[] = {&lower, &upper, &root_lower, &root_upper};
    phy_status status = PHY_OK;
    size_t initialized_count = 0u;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(context, values[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_lower(argument, &lower);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_upper(argument, &upper);
    }
    if (status == PHY_OK && phy_bigrat_sign(&lower) < 0) {
        status = PHY_ERR_DOMAIN;
    }
    if (status == PHY_OK) {
        status = sqrt_interval_bound(&lower, rounds, false, &root_lower);
    }
    if (status == PHY_OK) {
        status = sqrt_interval_bound(&upper, rounds, true, &root_upper);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_interval(out_root, &root_lower, &root_upper);
    }
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    return status;
}

/* ------------------------------------------------ certified elementary pack */

#define PHY_REAL_BALL_MAX_SERIES_ROUNDS 256u
#define PHY_REAL_BALL_MAX_REDUCTIONS 32u
#define PHY_REAL_BALL_MAX_SERIES_TERMS 68u
#define PHY_REAL_BALL_MAX_LOG_TERMS 64u

static uint32_t elementary_series_terms(uint32_t rounds)
{
    /* With |x| <= 1/16, the deliberately simple exp tail majorant
       3*|x|^terms gains four binary bits per term.  Two guard terms plus a
       ceiling division therefore make the analytic tail tighter than the
       dyadic rounding grid requested by `rounds`.  Trigonometric factorial
       tails are smaller still, so the same bound is safe for all three
       series while keeping one auditable resource contract. */
    uint32_t terms = 2u + (rounds + 3u) / 4u;
    if (terms > PHY_REAL_BALL_MAX_SERIES_TERMS) {
        terms = PHY_REAL_BALL_MAX_SERIES_TERMS;
    }
    return terms;
}

static uint32_t logarithm_series_terms(uint32_t rounds)
{
    /*
     * After square-root reduction the atanh argument satisfies |y| <= 1/4.
     * Each additional term then gains at least four binary bits in the tail.
     * Keep a guard margin for the outward dyadic roundings performed by every
     * arithmetic step, while retaining an absolute resource bound.
     */
    uint32_t terms = 8u + rounds / 4u;
    if (terms > PHY_REAL_BALL_MAX_LOG_TERMS) {
        terms = PHY_REAL_BALL_MAX_LOG_TERMS;
    }
    return terms;
}

static phy_status ball_magnitude_upper(const phy_real_ball *ball,
                                       phy_bigrat *out)
{
    phy_status status = absolute_rational(&ball->midpoint, out);
    if (status == PHY_OK) {
        status = phy_bigrat_add(out, &ball->radius, out);
    }
    return status;
}

static phy_status ball_scale_i64(const phy_real_ball *value, int64_t numerator,
                                 int64_t denominator, phy_real_ball *out)
{
    phy_real_ball scalar;
    memset(&scalar, 0, sizeof scalar);
    phy_status status = init_like(out, &scalar);
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&scalar, numerator, denominator);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(value, &scalar, out);
    }
    phy_real_ball_destroy(&scalar);
    return status;
}

static phy_status ball_add_symmetric_error(const phy_real_ball *value,
                                           const phy_bigrat *error,
                                           phy_real_ball *out)
{
    if (phy_bigrat_sign(error) < 0) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_real_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    phy_status status = init_like(out, &temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_copy(&value->midpoint, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(&value->radius, error, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, out);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

static phy_status ball_clamp_unit(const phy_real_ball *value,
                                  phy_real_ball *out)
{
    phy_exact_context *context = context_of(out);
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat minus_one;
    phy_bigrat one;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    memset(&minus_one, 0, sizeof minus_one);
    memset(&one, 0, sizeof one);
    phy_bigrat *values[] = {&lower, &upper, &minus_one, &one};
    size_t initialized_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(context, values[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_lower(value, &lower);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_upper(value, &upper);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&minus_one, -1, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&one, 1, 1);
    }
    int order = 0;
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&lower, &minus_one, &order);
    }
    if (status == PHY_OK && order < 0) {
        status = phy_bigrat_copy(&minus_one, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&upper, &one, &order);
    }
    if (status == PHY_OK && order > 0) {
        status = phy_bigrat_copy(&one, &upper);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&lower, &upper, &order);
    }
    if (status == PHY_OK && order > 0) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_interval(out, &lower, &upper);
    }
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    return status;
}

static phy_status ball_clamp_nonnegative(const phy_real_ball *value,
                                         phy_real_ball *out)
{
    phy_exact_context *context = context_of(out);
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat zero;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    memset(&zero, 0, sizeof zero);
    phy_bigrat *values[] = {&lower, &upper, &zero};
    size_t initialized_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(context, values[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_lower(value, &lower);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_upper(value, &upper);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&zero, 0, 1);
    }
    if (status == PHY_OK && phy_bigrat_sign(&upper) < 0) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    if (status == PHY_OK && phy_bigrat_sign(&lower) < 0) {
        status = phy_bigrat_copy(&zero, &lower);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_interval(out, &lower, &upper);
    }
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    return status;
}

static phy_status dyadic_bound(const phy_bigrat *value, uint32_t bits,
                               bool upper_bound, phy_bigrat *out)
{
    phy_exact_context *context = phy_bigrat_numerator(value)->context;
    phy_bigint two;
    phy_bigint scale;
    phy_bigint scaled_numerator;
    phy_bigint quotient;
    phy_bigint remainder;
    phy_bigint one;
    memset(&two, 0, sizeof two);
    memset(&scale, 0, sizeof scale);
    memset(&scaled_numerator, 0, sizeof scaled_numerator);
    memset(&quotient, 0, sizeof quotient);
    memset(&remainder, 0, sizeof remainder);
    memset(&one, 0, sizeof one);
    phy_bigint *integers[] = {&two,      &scale,     &scaled_numerator,
                              &quotient, &remainder, &one};
    size_t initialized_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_count < sizeof integers / sizeof integers[0]) {
        status = phy_bigint_init(context, integers[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_bigint_set_i64(&two, 2);
    }
    if (status == PHY_OK) {
        status = phy_bigint_pow_u32(&two, bits, &scale);
    }
    if (status == PHY_OK) {
        status = phy_bigint_multiply(phy_bigrat_numerator(value), &scale,
                                     &scaled_numerator);
    }
    if (status == PHY_OK) {
        status =
            phy_bigint_divmod(&scaled_numerator, phy_bigrat_denominator(value),
                              &quotient, &remainder);
    }
    if (status == PHY_OK) {
        status = phy_bigint_set_i64(&one, 1);
    }
    const int sign = phy_bigrat_sign(value);
    if (status == PHY_OK && phy_bigint_sign(&remainder) != 0 &&
        ((upper_bound && sign > 0) || (!upper_bound && sign < 0))) {
        status = upper_bound ? phy_bigint_add(&quotient, &one, &quotient)
                             : phy_bigint_subtract(&quotient, &one, &quotient);
    }
    phy_bigrat scale_rat;
    memset(&scale_rat, 0, sizeof scale_rat);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &scale_rat);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(&quotient, out);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(&scale, &scale_rat);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(out, &scale_rat, out);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&scale_rat))) {
        phy_bigrat_destroy(&scale_rat);
    }
    while (initialized_count != 0u) {
        phy_bigint_destroy(integers[--initialized_count]);
    }
    return status;
}

static phy_status ball_round_dyadic(const phy_real_ball *value, uint32_t bits,
                                    phy_real_ball *out)
{
    phy_exact_context *context = context_of(out);
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat rounded_lower;
    phy_bigrat rounded_upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    memset(&rounded_lower, 0, sizeof rounded_lower);
    memset(&rounded_upper, 0, sizeof rounded_upper);
    phy_bigrat *values[] = {&lower, &upper, &rounded_lower, &rounded_upper};
    size_t initialized_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(context, values[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_lower(value, &lower);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_upper(value, &upper);
    }
    if (status == PHY_OK) {
        status = dyadic_bound(&lower, bits, false, &rounded_lower);
    }
    if (status == PHY_OK) {
        status = dyadic_bound(&upper, bits, true, &rounded_upper);
    }
    if (status == PHY_OK) {
        status =
            phy_real_ball_set_interval(out, &rounded_lower, &rounded_upper);
    }
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    return status;
}

static phy_status reciprocal_factorial(phy_exact_context *context, uint32_t n,
                                       phy_bigrat *out)
{
    phy_bigrat divisor;
    memset(&divisor, 0, sizeof divisor);
    phy_status status = phy_bigrat_init(context, &divisor);
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(out, 1, 1);
    }
    for (uint32_t value = 2u; status == PHY_OK && value <= n; ++value) {
        status = phy_bigrat_set_i64(&divisor, (int64_t)value, 1);
        if (status == PHY_OK) {
            status = phy_bigrat_divide(out, &divisor, out);
        }
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&divisor))) {
        phy_bigrat_destroy(&divisor);
    }
    return status;
}

static phy_status halve_to_unit(const phy_real_ball *argument,
                                uint32_t precision_bits,
                                phy_real_ball *out_reduced,
                                uint32_t *out_reductions)
{
    if (precision_bits < 16u) {
        precision_bits = 16u;
    }
    phy_exact_context *context = context_of(out_reduced);
    phy_bigrat magnitude;
    phy_bigrat threshold;
    memset(&magnitude, 0, sizeof magnitude);
    memset(&threshold, 0, sizeof threshold);
    phy_status status = phy_bigrat_init(context, &magnitude);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &threshold);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&threshold, 1, 16);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(argument, out_reduced);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(out_reduced, precision_bits, out_reduced);
    }
    phy_real_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    if (status == PHY_OK) {
        status = init_like(out_reduced, &temporary);
    }
    uint32_t reductions = 0u;
    int order = 0;
    while (status == PHY_OK) {
        status = ball_magnitude_upper(out_reduced, &magnitude);
        if (status == PHY_OK) {
            status = phy_bigrat_compare(&magnitude, &threshold, &order);
        }
        if (status != PHY_OK || order <= 0) {
            break;
        }
        if (reductions == PHY_REAL_BALL_MAX_REDUCTIONS) {
            status = PHY_ERR_TERM_LIMIT;
            break;
        }
        status = ball_scale_i64(out_reduced, 1, 2, &temporary);
        if (status == PHY_OK) {
            status = ball_round_dyadic(&temporary, precision_bits, out_reduced);
        }
        reductions++;
    }
    if (status == PHY_OK) {
        *out_reductions = reductions;
    }
    phy_real_ball_destroy(&temporary);
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&threshold))) {
        phy_bigrat_destroy(&threshold);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&magnitude))) {
        phy_bigrat_destroy(&magnitude);
    }
    return status;
}

static phy_status trig_taylor_reduced(const phy_real_ball *argument,
                                      uint32_t terms, uint32_t precision_bits,
                                      phy_real_ball *out_sine,
                                      phy_real_ball *out_cosine)
{
    phy_real_ball x_squared;
    phy_real_ball sine_term;
    phy_real_ball cosine_term;
    phy_real_ball sine_sum;
    phy_real_ball cosine_sum;
    phy_real_ball scalar;
    phy_real_ball temporary;
    phy_real_ball next;
    phy_real_ball *balls[] = {&x_squared,  &sine_term, &cosine_term, &sine_sum,
                              &cosine_sum, &scalar,    &temporary,   &next};
    memset(&x_squared, 0, sizeof x_squared);
    memset(&sine_term, 0, sizeof sine_term);
    memset(&cosine_term, 0, sizeof cosine_term);
    memset(&sine_sum, 0, sizeof sine_sum);
    memset(&cosine_sum, 0, sizeof cosine_sum);
    memset(&scalar, 0, sizeof scalar);
    memset(&temporary, 0, sizeof temporary);
    memset(&next, 0, sizeof next);
    size_t initialized_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_count < sizeof balls / sizeof balls[0]) {
        status = init_like(out_sine, balls[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(argument, argument, &x_squared);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&x_squared, precision_bits, &x_squared);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(argument, &sine_term);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(argument, &sine_sum);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&cosine_term, 1, 1);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&cosine_sum, 1, 1);
    }
    for (uint32_t index = 1u; status == PHY_OK && index < terms; ++index) {
        const int64_t sine_denominator =
            (int64_t)(2u * index) * (int64_t)(2u * index + 1u);
        status = phy_real_ball_multiply(&sine_term, &x_squared, &temporary);
        if (status == PHY_OK) {
            status = phy_real_ball_set_i64(&scalar, -1, sine_denominator);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_multiply(&temporary, &scalar, &sine_term);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&sine_term, precision_bits, &sine_term);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_add(&sine_sum, &sine_term, &next);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&next, precision_bits, &sine_sum);
        }

        const int64_t cosine_denominator =
            (int64_t)(2u * index - 1u) * (int64_t)(2u * index);
        if (status == PHY_OK) {
            status =
                phy_real_ball_multiply(&cosine_term, &x_squared, &temporary);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_set_i64(&scalar, -1, cosine_denominator);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_multiply(&temporary, &scalar, &cosine_term);
        }
        if (status == PHY_OK) {
            status =
                ball_round_dyadic(&cosine_term, precision_bits, &cosine_term);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_add(&cosine_sum, &cosine_term, &next);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&next, precision_bits, &cosine_sum);
        }
    }
    phy_exact_context *context = context_of(out_sine);
    phy_bigrat sine_error;
    phy_bigrat cosine_error;
    phy_bigrat magnitude;
    phy_bigrat magnitude_power;
    memset(&sine_error, 0, sizeof sine_error);
    memset(&cosine_error, 0, sizeof cosine_error);
    memset(&magnitude, 0, sizeof magnitude);
    memset(&magnitude_power, 0, sizeof magnitude_power);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &sine_error);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &cosine_error);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &magnitude);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &magnitude_power);
    }
    if (status == PHY_OK) {
        /* Range reduction has already proved |argument| <= 1/16.  Using
           that fixed dyadic majorant keeps the certificate denominator
           bounded instead of raising a 100-bit input endpoint to a high
           Taylor power. */
        status = phy_bigrat_set_i64(&magnitude, 1, 16);
    }
    if (status == PHY_OK) {
        status = reciprocal_factorial(context, 2u * terms + 1u, &sine_error);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_pow_i32(&magnitude, (int32_t)(2u * terms + 1u),
                                    &magnitude_power);
    }
    if (status == PHY_OK) {
        status =
            phy_bigrat_multiply(&sine_error, &magnitude_power, &sine_error);
    }
    if (status == PHY_OK) {
        status = reciprocal_factorial(context, 2u * terms, &cosine_error);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_pow_i32(&magnitude, (int32_t)(2u * terms),
                                    &magnitude_power);
    }
    if (status == PHY_OK) {
        status =
            phy_bigrat_multiply(&cosine_error, &magnitude_power, &cosine_error);
    }
    if (status == PHY_OK) {
        status = ball_add_symmetric_error(&sine_sum, &sine_error, &sine_sum);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&sine_sum, precision_bits, &sine_sum);
    }
    if (status == PHY_OK) {
        status =
            ball_add_symmetric_error(&cosine_sum, &cosine_error, &cosine_sum);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&cosine_sum, precision_bits, &cosine_sum);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&sine_sum, out_sine);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&cosine_sum, out_cosine);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&cosine_error))) {
        phy_bigrat_destroy(&cosine_error);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&sine_error))) {
        phy_bigrat_destroy(&sine_error);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&magnitude_power))) {
        phy_bigrat_destroy(&magnitude_power);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&magnitude))) {
        phy_bigrat_destroy(&magnitude);
    }
    while (initialized_count != 0u) {
        phy_real_ball_destroy(balls[--initialized_count]);
    }
    return status;
}

static phy_status trig_core(const phy_real_ball *argument, uint32_t rounds,
                            phy_real_ball *out_sine, phy_real_ball *out_cosine)
{
    const uint32_t precision_bits = rounds < 16u ? 16u : rounds;
    phy_real_ball reduced;
    phy_real_ball sine;
    phy_real_ball cosine;
    phy_real_ball next_sine;
    phy_real_ball next_cosine;
    phy_real_ball product;
    phy_real_ball sine_square;
    phy_real_ball cosine_square;
    phy_real_ball *balls[] = {&reduced,     &sine,         &cosine,
                              &next_sine,   &next_cosine,  &product,
                              &sine_square, &cosine_square};
    memset(&reduced, 0, sizeof reduced);
    memset(&sine, 0, sizeof sine);
    memset(&cosine, 0, sizeof cosine);
    memset(&next_sine, 0, sizeof next_sine);
    memset(&next_cosine, 0, sizeof next_cosine);
    memset(&product, 0, sizeof product);
    memset(&sine_square, 0, sizeof sine_square);
    memset(&cosine_square, 0, sizeof cosine_square);
    size_t initialized_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_count < sizeof balls / sizeof balls[0]) {
        status = init_like(out_sine, balls[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    uint32_t reductions = 0u;
    if (status == PHY_OK) {
        status = halve_to_unit(argument, precision_bits, &reduced, &reductions);
    }
    if (status == PHY_OK) {
        status = trig_taylor_reduced(&reduced, elementary_series_terms(rounds),
                                     precision_bits, &sine, &cosine);
    }
    for (uint32_t index = 0u; status == PHY_OK && index < reductions; ++index) {
        status = phy_real_ball_multiply(&sine, &cosine, &product);
        if (status == PHY_OK) {
            status = ball_scale_i64(&product, 2, 1, &next_sine);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_multiply(&sine, &sine, &sine_square);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_multiply(&cosine, &cosine, &cosine_square);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_subtract(&cosine_square, &sine_square,
                                            &next_cosine);
        }
        if (status == PHY_OK) {
            status = ball_clamp_unit(&next_sine, &next_sine);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&next_sine, precision_bits, &next_sine);
        }
        if (status == PHY_OK) {
            status = ball_clamp_unit(&next_cosine, &next_cosine);
        }
        if (status == PHY_OK) {
            status =
                ball_round_dyadic(&next_cosine, precision_bits, &next_cosine);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_copy(&next_sine, &sine);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_copy(&next_cosine, &cosine);
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&sine, out_sine);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&cosine, out_cosine);
    }
    while (initialized_count != 0u) {
        phy_real_ball_destroy(balls[--initialized_count]);
    }
    return status;
}

static phy_status exp_core(const phy_real_ball *argument, uint32_t rounds,
                           phy_real_ball *out)
{
    const uint32_t precision_bits = rounds < 16u ? 16u : rounds;
    phy_real_ball reduced;
    phy_real_ball sum;
    phy_real_ball term;
    phy_real_ball scalar;
    phy_real_ball temporary;
    phy_real_ball next;
    phy_real_ball *balls[] = {&reduced, &sum,       &term,
                              &scalar,  &temporary, &next};
    memset(&reduced, 0, sizeof reduced);
    memset(&sum, 0, sizeof sum);
    memset(&term, 0, sizeof term);
    memset(&scalar, 0, sizeof scalar);
    memset(&temporary, 0, sizeof temporary);
    memset(&next, 0, sizeof next);
    size_t initialized_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_count < sizeof balls / sizeof balls[0]) {
        status = init_like(out, balls[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    uint32_t reductions = 0u;
    if (status == PHY_OK) {
        status = halve_to_unit(argument, precision_bits, &reduced, &reductions);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&sum, 1, 1);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&term, 1, 1);
    }
    const uint32_t terms = elementary_series_terms(rounds);
    for (uint32_t index = 1u; status == PHY_OK && index < terms; ++index) {
        status = phy_real_ball_multiply(&term, &reduced, &temporary);
        if (status == PHY_OK) {
            status = phy_real_ball_set_i64(&scalar, 1, (int64_t)index);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_multiply(&temporary, &scalar, &term);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&term, precision_bits, &term);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_add(&sum, &term, &next);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&next, precision_bits, &sum);
        }
    }
    phy_bigrat error;
    phy_bigrat magnitude;
    phy_bigrat magnitude_power;
    memset(&error, 0, sizeof error);
    memset(&magnitude, 0, sizeof magnitude);
    memset(&magnitude_power, 0, sizeof magnitude_power);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context_of(out), &error);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context_of(out), &magnitude);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context_of(out), &magnitude_power);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&magnitude, 1, 16);
    }
    if (status == PHY_OK) {
        status = reciprocal_factorial(context_of(out), terms, &error);
    }
    if (status == PHY_OK) {
        status =
            phy_bigrat_pow_i32(&magnitude, (int32_t)terms, &magnitude_power);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&error, &magnitude_power, &error);
    }
    if (status == PHY_OK) {
        phy_bigrat three;
        memset(&three, 0, sizeof three);
        status = phy_bigrat_init(context_of(out), &three);
        if (status == PHY_OK) {
            status = phy_bigrat_set_i64(&three, 3, 1);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_multiply(&error, &three, &error);
        }
        if (phy_bigint_is_initialized(phy_bigrat_numerator(&three))) {
            phy_bigrat_destroy(&three);
        }
    }
    if (status == PHY_OK) {
        status = ball_add_symmetric_error(&sum, &error, &sum);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&sum, precision_bits, &sum);
    }
    for (uint32_t index = 0u; status == PHY_OK && index < reductions; ++index) {
        status = phy_real_ball_multiply(&sum, &sum, &temporary);
        if (status == PHY_OK) {
            status = ball_clamp_nonnegative(&temporary, &next);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&next, precision_bits, &next);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_copy(&next, &sum);
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&sum, out);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&error))) {
        phy_bigrat_destroy(&error);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&magnitude_power))) {
        phy_bigrat_destroy(&magnitude_power);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&magnitude))) {
        phy_bigrat_destroy(&magnitude);
    }
    while (initialized_count != 0u) {
        phy_real_ball_destroy(balls[--initialized_count]);
    }
    return status;
}

static phy_status log_core(const phy_real_ball *argument, uint32_t rounds,
                           phy_real_ball *out)
{
    const uint32_t precision_bits = rounds < 16u ? 16u : rounds;
    phy_exact_context *context = context_of(out);
    phy_bigrat lower;
    phy_bigrat q;
    phy_bigrat threshold;
    memset(&lower, 0, sizeof lower);
    memset(&q, 0, sizeof q);
    memset(&threshold, 0, sizeof threshold);
    phy_bigrat *rationals[] = {&lower, &q, &threshold};
    size_t rational_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           rational_count < sizeof rationals / sizeof rationals[0]) {
        status = phy_bigrat_init(context, rationals[rational_count]);
        if (status == PHY_OK) {
            rational_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_lower(argument, &lower);
    }
    if (status == PHY_OK && phy_bigrat_sign(&lower) <= 0) {
        status = PHY_ERR_DOMAIN;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&threshold, 1, 4);
    }

    phy_real_ball reduced;
    phy_real_ball one;
    phy_real_ball numerator;
    phy_real_ball denominator;
    phy_real_ball y;
    phy_real_ball y_squared;
    phy_real_ball term;
    phy_real_ball sum;
    phy_real_ball scaled;
    phy_real_ball scalar;
    phy_real_ball temporary;
    phy_real_ball next;
    phy_real_ball *balls[] = {&reduced, &one,       &numerator, &denominator,
                              &y,       &y_squared, &term,      &sum,
                              &scaled,  &scalar,    &temporary, &next};
    memset(&reduced, 0, sizeof reduced);
    memset(&one, 0, sizeof one);
    memset(&numerator, 0, sizeof numerator);
    memset(&denominator, 0, sizeof denominator);
    memset(&y, 0, sizeof y);
    memset(&y_squared, 0, sizeof y_squared);
    memset(&term, 0, sizeof term);
    memset(&sum, 0, sizeof sum);
    memset(&scaled, 0, sizeof scaled);
    memset(&scalar, 0, sizeof scalar);
    memset(&temporary, 0, sizeof temporary);
    memset(&next, 0, sizeof next);
    size_t initialized_count = 0u;
    while (status == PHY_OK &&
           initialized_count < sizeof balls / sizeof balls[0]) {
        status = init_like(out, balls[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(argument, &reduced);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&reduced, precision_bits, &reduced);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&one, 1, 1);
    }

    uint32_t reductions = 0u;
    int order = 0;
    while (status == PHY_OK) {
        status = phy_real_ball_subtract(&reduced, &one, &numerator);
        if (status == PHY_OK) {
            status = phy_real_ball_add(&reduced, &one, &denominator);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_divide(&numerator, &denominator, &y);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&y, precision_bits, &y);
        }
        if (status == PHY_OK) {
            status = ball_magnitude_upper(&y, &q);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_compare(&q, &threshold, &order);
        }
        if (status != PHY_OK || order <= 0) {
            break;
        }
        if (reductions == PHY_REAL_BALL_MAX_REDUCTIONS) {
            status = PHY_ERR_TERM_LIMIT;
            break;
        }
        const uint32_t sqrt_rounds = precision_bits;
        status = phy_real_ball_sqrt(&reduced, sqrt_rounds, &temporary);
        if (status == PHY_OK) {
            status = ball_round_dyadic(&temporary, precision_bits, &reduced);
        }
        reductions++;
    }

    const uint32_t terms = logarithm_series_terms(rounds);
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(&y, &y, &y_squared);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&y_squared, precision_bits, &y_squared);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&y, &term);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&y, &sum);
    }
    for (uint32_t index = 1u; status == PHY_OK && index < terms; ++index) {
        status = phy_real_ball_multiply(&term, &y_squared, &temporary);
        if (status == PHY_OK) {
            status = ball_round_dyadic(&temporary, precision_bits, &term);
        }
        if (status == PHY_OK) {
            status =
                phy_real_ball_set_i64(&scalar, 1, (int64_t)(2u * index + 1u));
        }
        if (status == PHY_OK) {
            status = phy_real_ball_multiply(&term, &scalar, &scaled);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&scaled, precision_bits, &scaled);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_add(&sum, &scaled, &next);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&next, precision_bits, &sum);
        }
    }
    if (status == PHY_OK) {
        status = ball_scale_i64(&sum, 2, 1, &sum);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&sum, precision_bits, &sum);
    }

    phy_bigrat q_power;
    phy_bigrat q_squared;
    phy_bigrat one_rat;
    phy_bigrat tail_denominator;
    phy_bigrat degree;
    phy_bigrat error;
    phy_bigrat two;
    memset(&q_power, 0, sizeof q_power);
    memset(&q_squared, 0, sizeof q_squared);
    memset(&one_rat, 0, sizeof one_rat);
    memset(&tail_denominator, 0, sizeof tail_denominator);
    memset(&degree, 0, sizeof degree);
    memset(&error, 0, sizeof error);
    memset(&two, 0, sizeof two);
    phy_bigrat *tail_values[] = {
        &q_power, &q_squared, &one_rat, &tail_denominator,
        &degree,  &error,     &two};
    size_t tail_count = 0u;
    while (status == PHY_OK &&
           tail_count < sizeof tail_values / sizeof tail_values[0]) {
        status = phy_bigrat_init(context, tail_values[tail_count]);
        if (status == PHY_OK) {
            tail_count++;
        }
    }
    const uint32_t tail_degree = 2u * terms + 1u;
    if (status == PHY_OK) {
        /* The loop above proves |y| <= 1/4.  Use the fixed dyadic
           majorant in the geometric tail so its exponent cannot inherit
           the full input denominator. */
        status = phy_bigrat_copy(&threshold, &q);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_pow_i32(&q, (int32_t)tail_degree, &q_power);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&q, &q, &q_squared);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&one_rat, 1, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_subtract(&one_rat, &q_squared, &tail_denominator);
    }
    if (status == PHY_OK && phy_bigrat_sign(&tail_denominator) <= 0) {
        status = PHY_ERR_TERM_LIMIT;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&degree, (int64_t)tail_degree, 1);
    }
    if (status == PHY_OK) {
        status =
            phy_bigrat_multiply(&tail_denominator, &degree, &tail_denominator);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(&q_power, &tail_denominator, &error);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&two, 2, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&error, &two, &error);
    }
    if (status == PHY_OK) {
        status = ball_add_symmetric_error(&sum, &error, &sum);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&sum, precision_bits, &sum);
    }
    if (status == PHY_OK) {
        const int64_t scale = INT64_C(1) << reductions;
        status = ball_scale_i64(&sum, scale, 1, &sum);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&sum, precision_bits, &sum);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&sum, out);
    }
    while (tail_count != 0u) {
        phy_bigrat_destroy(tail_values[--tail_count]);
    }
    while (initialized_count != 0u) {
        phy_real_ball_destroy(balls[--initialized_count]);
    }
    while (rational_count != 0u) {
        phy_bigrat_destroy(rationals[--rational_count]);
    }
    return status;
}

static phy_status elementary_output_init(const phy_real_ball *argument,
                                         uint32_t rounds,
                                         phy_real_ball *out_value,
                                         phy_real_ball *temporary)
{
    if (!initialized(argument) || !initialized(out_value)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (rounds > PHY_REAL_BALL_MAX_SERIES_ROUNDS) {
        return PHY_ERR_TERM_LIMIT;
    }
    memset(temporary, 0, sizeof *temporary);
    return init_like(out_value, temporary);
}

phy_status phy_real_ball_exp(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    if (status == PHY_OK) {
        status = exp_core(argument, rounds, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&temporary, out_value);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_log(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    if (status == PHY_OK) {
        status = log_core(argument, rounds, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&temporary, out_value);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_sin(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball cosine;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&cosine, 0, sizeof cosine);
    if (status == PHY_OK) {
        status = init_like(out_value, &cosine);
    }
    if (status == PHY_OK) {
        status = trig_core(argument, rounds, &temporary, &cosine);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&temporary, out_value);
    }
    phy_real_ball_destroy(&cosine);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_cos(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball sine;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&sine, 0, sizeof sine);
    if (status == PHY_OK) {
        status = init_like(out_value, &sine);
    }
    if (status == PHY_OK) {
        status = trig_core(argument, rounds, &sine, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&temporary, out_value);
    }
    phy_real_ball_destroy(&sine);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_tan(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value)
{
    phy_real_ball sine;
    phy_real_ball cosine;
    phy_real_ball temporary;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&sine, 0, sizeof sine);
    memset(&cosine, 0, sizeof cosine);
    if (status == PHY_OK) {
        status = init_like(out_value, &sine);
    }
    if (status == PHY_OK) {
        status = init_like(out_value, &cosine);
    }
    if (status == PHY_OK) {
        status = trig_core(argument, rounds, &sine, &cosine);
    }
    bool contains_zero = true;
    if (status == PHY_OK) {
        status = phy_real_ball_contains_zero_checked(&cosine, &contains_zero);
    }
    if (status == PHY_OK && contains_zero) {
        status = PHY_ERR_DOMAIN;
    }
    if (status == PHY_OK) {
        status = phy_real_ball_divide(&sine, &cosine, &temporary);
    }
    if (status == PHY_OK) {
        const uint32_t precision_bits = rounds < 16u ? 16u : rounds;
        status = ball_round_dyadic(&temporary, precision_bits, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&temporary, out_value);
    }
    phy_real_ball_destroy(&cosine);
    phy_real_ball_destroy(&sine);
    phy_real_ball_destroy(&temporary);
    return status;
}

static phy_status ball_negate(const phy_real_ball *value, phy_real_ball *out)
{
    return ball_scale_i64(value, -1, 1, out);
}

static phy_status ball_square_nonnegative(const phy_real_ball *value,
                                          phy_real_ball *out)
{
    phy_real_ball product;
    memset(&product, 0, sizeof product);
    phy_status status = init_like(out, &product);
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(value, value, &product);
    }
    if (status == PHY_OK) {
        status = ball_clamp_nonnegative(&product, out);
    }
    phy_real_ball_destroy(&product);
    return status;
}

static phy_status ball_pi(phy_real_ball *out)
{
    static const char scale[] = "10000000000000000000000000000000000000000";
    phy_exact_context *context = context_of(out);
    phy_bigrat lower;
    phy_bigrat upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    phy_status status = phy_bigrat_init(context, &lower);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &upper);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_read(
            &lower, "31415926535897932384626433832795028841971", scale);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_read(
            &upper, "31415926535897932384626433832795028841972", scale);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_interval(out, &lower, &upper);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&upper))) {
        phy_bigrat_destroy(&upper);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&lower))) {
        phy_bigrat_destroy(&lower);
    }
    return status;
}

static phy_status ball_bounds_within_i64(const phy_real_ball *value,
                                         int64_t lower_limit,
                                         int64_t upper_limit, bool strict,
                                         bool *out_within)
{
    if (out_within == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_within = false;
    phy_exact_context *context = context_of(value);
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat minimum;
    phy_bigrat maximum;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    memset(&minimum, 0, sizeof minimum);
    memset(&maximum, 0, sizeof maximum);
    phy_bigrat *values[] = {&lower, &upper, &minimum, &maximum};
    size_t initialized_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(context, values[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_lower(value, &lower);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_upper(value, &upper);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&minimum, lower_limit, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&maximum, upper_limit, 1);
    }
    int lower_order = -1;
    int upper_order = 1;
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&lower, &minimum, &lower_order);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&upper, &maximum, &upper_order);
    }
    if (status == PHY_OK) {
        *out_within = strict ? lower_order > 0 && upper_order < 0
                             : lower_order >= 0 && upper_order <= 0;
    }
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    return status;
}

static phy_status ball_lower_at_least_i64(const phy_real_ball *value,
                                          int64_t minimum_value,
                                          bool *out_within)
{
    if (out_within == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_within = false;
    phy_exact_context *context = context_of(value);
    phy_bigrat lower;
    phy_bigrat minimum;
    memset(&lower, 0, sizeof lower);
    memset(&minimum, 0, sizeof minimum);
    phy_status status = phy_bigrat_init(context, &lower);
    if (status == PHY_OK)
        status = phy_bigrat_init(context, &minimum);
    if (status == PHY_OK)
        status = phy_real_ball_lower(value, &lower);
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&minimum, minimum_value, 1);
    }
    int order = -1;
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&lower, &minimum, &order);
    }
    if (status == PHY_OK)
        *out_within = order >= 0;
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&minimum))) {
        phy_bigrat_destroy(&minimum);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&lower))) {
        phy_bigrat_destroy(&lower);
    }
    return status;
}

static phy_status dyadic_error(phy_exact_context *context, uint32_t bits,
                               phy_bigrat *out)
{
    phy_bigint two;
    phy_bigint scale;
    phy_bigrat one;
    phy_bigrat scale_rat;
    memset(&two, 0, sizeof two);
    memset(&scale, 0, sizeof scale);
    memset(&one, 0, sizeof one);
    memset(&scale_rat, 0, sizeof scale_rat);
    phy_status status = phy_bigint_init(context, &two);
    if (status == PHY_OK) status = phy_bigint_init(context, &scale);
    if (status == PHY_OK) status = phy_bigrat_init(context, &one);
    if (status == PHY_OK) status = phy_bigrat_init(context, &scale_rat);
    if (status == PHY_OK) status = phy_bigint_set_i64(&two, 2);
    if (status == PHY_OK) status = phy_bigint_pow_u32(&two, bits, &scale);
    if (status == PHY_OK) status = phy_bigrat_set_i64(&one, 1, 1);
    if (status == PHY_OK) status = phy_bigrat_set_bigint(&scale, &scale_rat);
    if (status == PHY_OK) status = phy_bigrat_divide(&one, &scale_rat, out);
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&scale_rat))) {
        phy_bigrat_destroy(&scale_rat);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&one))) {
        phy_bigrat_destroy(&one);
    }
    if (phy_bigint_is_initialized(&scale)) phy_bigint_destroy(&scale);
    if (phy_bigint_is_initialized(&two)) phy_bigint_destroy(&two);
    return status;
}

static phy_status atan_reduced_series(const phy_real_ball *argument,
                                      uint32_t rounds, phy_real_ball *out)
{
    const uint32_t precision_bits = rounds < 16u ? 16u : rounds;
    const uint32_t terms = 8u + (precision_bits + 1u) / 2u;
    phy_exact_context *context = context_of(out);
    phy_real_ball reduced;
    phy_real_ball square;
    phy_real_ball one;
    phy_real_ball root;
    phy_real_ball denominator;
    phy_real_ball term;
    phy_real_ball sum;
    phy_real_ball scalar;
    phy_real_ball temporary;
    phy_real_ball next;
    phy_real_ball *balls[] = {&reduced,     &square, &one, &root,
                              &denominator, &term,   &sum, &scalar,
                              &temporary,   &next};
    memset(balls[0], 0, sizeof reduced);
    memset(balls[1], 0, sizeof square);
    memset(balls[2], 0, sizeof one);
    memset(balls[3], 0, sizeof root);
    memset(balls[4], 0, sizeof denominator);
    memset(balls[5], 0, sizeof term);
    memset(balls[6], 0, sizeof sum);
    memset(balls[7], 0, sizeof scalar);
    memset(balls[8], 0, sizeof temporary);
    memset(balls[9], 0, sizeof next);
    size_t initialized_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_count < sizeof balls / sizeof balls[0]) {
        status = init_like(out, balls[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(argument, &reduced);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&one, 1, 1);
    }
    phy_bigrat magnitude;
    phy_bigrat quarter;
    memset(&magnitude, 0, sizeof magnitude);
    memset(&quarter, 0, sizeof quarter);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &magnitude);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &quarter);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&quarter, 1, 2);
    }
    uint32_t reductions = 0u;
    while (status == PHY_OK) {
        status = ball_magnitude_upper(&reduced, &magnitude);
        int order = 0;
        if (status == PHY_OK) {
            status = phy_bigrat_compare(&magnitude, &quarter, &order);
        }
        if (status != PHY_OK || order <= 0) {
            break;
        }
        if (reductions >= 30u) {
            status = PHY_ERR_TERM_LIMIT;
            break;
        }
        status = ball_square_nonnegative(&reduced, &square);
        if (status == PHY_OK) {
            status = phy_real_ball_add(&one, &square, &temporary);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_sqrt(&temporary, rounds, &root);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_add(&one, &root, &denominator);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_divide(&reduced, &denominator, &next);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_copy(&next, &reduced);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(
                &reduced, precision_bits, &reduced);
        }
        reductions++;
    }
    if (status == PHY_OK) {
        status = ball_square_nonnegative(&reduced, &square);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&reduced, &term);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&reduced, &sum);
    }
    for (uint32_t index = 1u; status == PHY_OK && index < terms; ++index) {
        status = phy_real_ball_multiply(&term, &square, &temporary);
        if (status == PHY_OK) {
            status = ball_negate(&temporary, &term);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&term, precision_bits, &term);
        }
        if (status == PHY_OK) {
            status =
                phy_real_ball_set_i64(&scalar, 1, (int64_t)(2u * index + 1u));
        }
        if (status == PHY_OK) {
            status = phy_real_ball_multiply(&term, &scalar, &temporary);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_add(&sum, &temporary, &next);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_copy(&next, &sum);
        }
        if (status == PHY_OK) {
            status = ball_round_dyadic(&sum, precision_bits, &sum);
        }
    }
    phy_bigrat error;
    memset(&error, 0, sizeof error);
    bool error_initialized = false;
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &error);
        error_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        /* With |x| <= 1/2, the first omitted atan term and its
           geometric tail are strictly below 2^(-precision_bits-8). */
        status = dyadic_error(context, precision_bits + 8u, &error);
    }
    if (status == PHY_OK) {
        status = ball_add_symmetric_error(&sum, &error, &sum);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&sum, precision_bits, &sum);
    }
    if (status == PHY_OK) {
        const int64_t scale = INT64_C(1) << reductions;
        status = ball_scale_i64(&sum, scale, 1, &sum);
    }
    if (status == PHY_OK) {
        status = ball_round_dyadic(&sum, precision_bits, &sum);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&sum, out);
    }
    if (error_initialized) phy_bigrat_destroy(&error);
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&quarter))) {
        phy_bigrat_destroy(&quarter);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&magnitude))) {
        phy_bigrat_destroy(&magnitude);
    }
    while (initialized_count != 0u) {
        phy_real_ball_destroy(balls[--initialized_count]);
    }
    return status;
}

phy_status phy_real_ball_sinh(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball positive;
    phy_real_ball negative_argument;
    phy_real_ball negative;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&positive, 0, sizeof positive);
    memset(&negative_argument, 0, sizeof negative_argument);
    memset(&negative, 0, sizeof negative);
    if (status == PHY_OK)
        status = init_like(out_value, &positive);
    if (status == PHY_OK)
        status = init_like(out_value, &negative_argument);
    if (status == PHY_OK)
        status = init_like(out_value, &negative);
    if (status == PHY_OK)
        status = phy_real_ball_exp(argument, rounds, &positive);
    if (status == PHY_OK)
        status = ball_negate(argument, &negative_argument);
    if (status == PHY_OK)
        status = phy_real_ball_exp(&negative_argument, rounds, &negative);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(&positive, &negative, &temporary);
    if (status == PHY_OK)
        status = ball_scale_i64(&temporary, 1, 2, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    phy_real_ball_destroy(&negative);
    phy_real_ball_destroy(&negative_argument);
    phy_real_ball_destroy(&positive);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_cosh(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball positive;
    phy_real_ball negative_argument;
    phy_real_ball negative;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&positive, 0, sizeof positive);
    memset(&negative_argument, 0, sizeof negative_argument);
    memset(&negative, 0, sizeof negative);
    if (status == PHY_OK)
        status = init_like(out_value, &positive);
    if (status == PHY_OK)
        status = init_like(out_value, &negative_argument);
    if (status == PHY_OK)
        status = init_like(out_value, &negative);
    if (status == PHY_OK)
        status = phy_real_ball_exp(argument, rounds, &positive);
    if (status == PHY_OK)
        status = ball_negate(argument, &negative_argument);
    if (status == PHY_OK)
        status = phy_real_ball_exp(&negative_argument, rounds, &negative);
    if (status == PHY_OK)
        status = phy_real_ball_add(&positive, &negative, &temporary);
    if (status == PHY_OK)
        status = ball_scale_i64(&temporary, 1, 2, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    phy_real_ball_destroy(&negative);
    phy_real_ball_destroy(&negative_argument);
    phy_real_ball_destroy(&positive);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_tanh(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball sine;
    phy_real_ball cosine;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&sine, 0, sizeof sine);
    memset(&cosine, 0, sizeof cosine);
    if (status == PHY_OK)
        status = init_like(out_value, &sine);
    if (status == PHY_OK)
        status = init_like(out_value, &cosine);
    if (status == PHY_OK)
        status = phy_real_ball_sinh(argument, rounds, &sine);
    if (status == PHY_OK)
        status = phy_real_ball_cosh(argument, rounds, &cosine);
    if (status == PHY_OK)
        status = phy_real_ball_divide(&sine, &cosine, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    phy_real_ball_destroy(&cosine);
    phy_real_ball_destroy(&sine);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_atan(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    if (status == PHY_OK) {
        status = atan_reduced_series(argument, rounds, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&temporary, out_value);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_asin(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball one;
    phy_real_ball square;
    phy_real_ball radicand;
    phy_real_ball root;
    phy_real_ball denominator;
    phy_real_ball ratio;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&one, 0, sizeof one);
    memset(&square, 0, sizeof square);
    memset(&radicand, 0, sizeof radicand);
    memset(&root, 0, sizeof root);
    memset(&denominator, 0, sizeof denominator);
    memset(&ratio, 0, sizeof ratio);
    phy_real_ball *balls[] = {&one,  &square,      &radicand,
                              &root, &denominator, &ratio};
    size_t count = 0u;
    while (status == PHY_OK && count < sizeof balls / sizeof balls[0]) {
        status = init_like(out_value, balls[count]);
        if (status == PHY_OK)
            count++;
    }
    bool in_domain = false;
    if (status == PHY_OK) {
        status = ball_bounds_within_i64(argument, -1, 1, false, &in_domain);
    }
    if (status == PHY_OK && !in_domain)
        status = PHY_ERR_DOMAIN;
    if (status == PHY_OK)
        status = phy_real_ball_set_i64(&one, 1, 1);
    if (status == PHY_OK)
        status = ball_square_nonnegative(argument, &square);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(&one, &square, &radicand);
    if (status == PHY_OK)
        status = ball_clamp_nonnegative(&radicand, &radicand);
    if (status == PHY_OK)
        status = phy_real_ball_sqrt(&radicand, rounds, &root);
    if (status == PHY_OK)
        status = phy_real_ball_add(&one, &root, &denominator);
    if (status == PHY_OK)
        status = phy_real_ball_divide(argument, &denominator, &ratio);
    if (status == PHY_OK)
        status = phy_real_ball_atan(&ratio, rounds, &temporary);
    if (status == PHY_OK)
        status = ball_scale_i64(&temporary, 2, 1, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    while (count != 0u)
        phy_real_ball_destroy(balls[--count]);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_acos(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball inverse_sine;
    phy_real_ball pi;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&inverse_sine, 0, sizeof inverse_sine);
    memset(&pi, 0, sizeof pi);
    if (status == PHY_OK)
        status = init_like(out_value, &inverse_sine);
    if (status == PHY_OK)
        status = init_like(out_value, &pi);
    if (status == PHY_OK)
        status = phy_real_ball_asin(argument, rounds, &inverse_sine);
    if (status == PHY_OK)
        status = ball_pi(&pi);
    if (status == PHY_OK)
        status = ball_scale_i64(&pi, 1, 2, &pi);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(&pi, &inverse_sine, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    phy_real_ball_destroy(&pi);
    phy_real_ball_destroy(&inverse_sine);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_asinh(const phy_real_ball *argument, uint32_t rounds,
                               phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball square;
    phy_real_ball one;
    phy_real_ball root;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&square, 0, sizeof square);
    memset(&one, 0, sizeof one);
    memset(&root, 0, sizeof root);
    if (status == PHY_OK)
        status = init_like(out_value, &square);
    if (status == PHY_OK)
        status = init_like(out_value, &one);
    if (status == PHY_OK)
        status = init_like(out_value, &root);
    if (status == PHY_OK)
        status = ball_square_nonnegative(argument, &square);
    if (status == PHY_OK)
        status = phy_real_ball_set_i64(&one, 1, 1);
    if (status == PHY_OK)
        status = phy_real_ball_add(&square, &one, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_sqrt(&temporary, rounds, &root);
    if (status == PHY_OK)
        status = phy_real_ball_add(argument, &root, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_log(&temporary, rounds, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    phy_real_ball_destroy(&root);
    phy_real_ball_destroy(&one);
    phy_real_ball_destroy(&square);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_acosh(const phy_real_ball *argument, uint32_t rounds,
                               phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball one;
    phy_real_ball minus;
    phy_real_ball plus;
    phy_real_ball root_minus;
    phy_real_ball root_plus;
    phy_real_ball product;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&one, 0, sizeof one);
    memset(&minus, 0, sizeof minus);
    memset(&plus, 0, sizeof plus);
    memset(&root_minus, 0, sizeof root_minus);
    memset(&root_plus, 0, sizeof root_plus);
    memset(&product, 0, sizeof product);
    phy_real_ball *balls[] = {&one,        &minus,     &plus,
                              &root_minus, &root_plus, &product};
    size_t count = 0u;
    while (status == PHY_OK && count < sizeof balls / sizeof balls[0]) {
        status = init_like(out_value, balls[count]);
        if (status == PHY_OK)
            count++;
    }
    bool in_domain = false;
    if (status == PHY_OK)
        status = ball_lower_at_least_i64(argument, 1, &in_domain);
    if (status == PHY_OK && !in_domain)
        status = PHY_ERR_DOMAIN;
    if (status == PHY_OK)
        status = phy_real_ball_set_i64(&one, 1, 1);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(argument, &one, &minus);
    if (status == PHY_OK)
        status = phy_real_ball_add(argument, &one, &plus);
    if (status == PHY_OK)
        status = phy_real_ball_sqrt(&minus, rounds, &root_minus);
    if (status == PHY_OK)
        status = phy_real_ball_sqrt(&plus, rounds, &root_plus);
    if (status == PHY_OK)
        status = phy_real_ball_multiply(&root_minus, &root_plus, &product);
    if (status == PHY_OK)
        status = phy_real_ball_add(argument, &product, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_log(&temporary, rounds, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    while (count != 0u)
        phy_real_ball_destroy(balls[--count]);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_atanh(const phy_real_ball *argument, uint32_t rounds,
                               phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball one;
    phy_real_ball plus;
    phy_real_ball minus;
    phy_real_ball log_plus;
    phy_real_ball log_minus;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&one, 0, sizeof one);
    memset(&plus, 0, sizeof plus);
    memset(&minus, 0, sizeof minus);
    memset(&log_plus, 0, sizeof log_plus);
    memset(&log_minus, 0, sizeof log_minus);
    phy_real_ball *balls[] = {&one, &plus, &minus, &log_plus, &log_minus};
    size_t count = 0u;
    while (status == PHY_OK && count < sizeof balls / sizeof balls[0]) {
        status = init_like(out_value, balls[count]);
        if (status == PHY_OK)
            count++;
    }
    bool in_domain = false;
    if (status == PHY_OK)
        status = ball_bounds_within_i64(argument, -1, 1, true, &in_domain);
    if (status == PHY_OK && !in_domain)
        status = PHY_ERR_DOMAIN;
    if (status == PHY_OK)
        status = phy_real_ball_set_i64(&one, 1, 1);
    if (status == PHY_OK)
        status = phy_real_ball_add(&one, argument, &plus);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(&one, argument, &minus);
    if (status == PHY_OK)
        status = phy_real_ball_log(&plus, rounds, &log_plus);
    if (status == PHY_OK)
        status = phy_real_ball_log(&minus, rounds, &log_minus);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(&log_plus, &log_minus, &temporary);
    if (status == PHY_OK)
        status = ball_scale_i64(&temporary, 1, 2, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    while (count != 0u)
        phy_real_ball_destroy(balls[--count]);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_erf(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    bool in_domain = false;
    if (status == PHY_OK)
        status = ball_bounds_within_i64(argument, -1, 1, false, &in_domain);
    if (status == PHY_OK && !in_domain)
        status = PHY_ERR_DOMAIN;
    phy_real_ball square, term, sum, scalar, next, pi, root_pi, two, factor;
    memset(&square, 0, sizeof square);
    memset(&term, 0, sizeof term);
    memset(&sum, 0, sizeof sum);
    memset(&scalar, 0, sizeof scalar);
    memset(&next, 0, sizeof next);
    memset(&pi, 0, sizeof pi);
    memset(&root_pi, 0, sizeof root_pi);
    memset(&two, 0, sizeof two);
    memset(&factor, 0, sizeof factor);
    phy_real_ball *balls[] = {&square, &term,    &sum, &scalar, &next,
                              &pi,     &root_pi, &two, &factor};
    size_t count = 0u;
    while (status == PHY_OK && count < sizeof balls / sizeof balls[0]) {
        status = init_like(out_value, balls[count]);
        if (status == PHY_OK)
            count++;
    }
    const uint32_t precision_bits = rounds < 16u ? 16u : rounds;
    const uint32_t terms = 8u + precision_bits / 4u;
    if (status == PHY_OK)
        status = ball_square_nonnegative(argument, &square);
    if (status == PHY_OK)
        status = phy_real_ball_copy(argument, &term);
    if (status == PHY_OK)
        status = phy_real_ball_copy(argument, &sum);
    for (uint32_t n = 1u; status == PHY_OK && n < terms; ++n) {
        status = phy_real_ball_multiply(&term, &square, &next);
        if (status == PHY_OK)
            status = phy_real_ball_set_i64(&scalar, -(int64_t)(2u * n - 1u),
                                           (int64_t)(n * (2u * n + 1u)));
        if (status == PHY_OK)
            status = phy_real_ball_multiply(&next, &scalar, &term);
        if (status == PHY_OK)
            status = ball_round_dyadic(&term, precision_bits, &term);
        if (status == PHY_OK)
            status = phy_real_ball_add(&sum, &term, &next);
        if (status == PHY_OK)
            status = phy_real_ball_copy(&next, &sum);
        if (status == PHY_OK)
            status = ball_round_dyadic(&sum, precision_bits, &sum);
    }
    phy_exact_context *context = context_of(out_value);
    phy_bigrat q, q_power, factorial, integer, denominator, error;
    memset(&q, 0, sizeof q);
    memset(&q_power, 0, sizeof q_power);
    memset(&factorial, 0, sizeof factorial);
    memset(&integer, 0, sizeof integer);
    memset(&denominator, 0, sizeof denominator);
    memset(&error, 0, sizeof error);
    phy_bigrat *rats[] = {&q,       &q_power,     &factorial,
                          &integer, &denominator, &error};
    size_t rat_count = 0u;
    while (status == PHY_OK && rat_count < sizeof rats / sizeof rats[0]) {
        status = phy_bigrat_init(context, rats[rat_count]);
        if (status == PHY_OK)
            rat_count++;
    }
    if (status == PHY_OK)
        status = ball_magnitude_upper(argument, &q);
    if (status == PHY_OK)
        status = phy_bigrat_set_i64(&q_power, 1, 1);
    for (uint32_t i = 0u; status == PHY_OK && i < 2u * terms + 1u; ++i)
        status = phy_bigrat_multiply(&q_power, &q, &q_power);
    if (status == PHY_OK)
        status = phy_bigrat_set_i64(&factorial, 1, 1);
    for (uint32_t i = 2u; status == PHY_OK && i <= terms; ++i) {
        status = phy_bigrat_set_i64(&integer, (int64_t)i, 1);
        if (status == PHY_OK)
            status = phy_bigrat_multiply(&factorial, &integer, &factorial);
    }
    if (status == PHY_OK)
        status = phy_bigrat_set_i64(&integer, (int64_t)(2u * terms + 1u), 1);
    if (status == PHY_OK)
        status = phy_bigrat_multiply(&factorial, &integer, &denominator);
    if (status == PHY_OK)
        status = phy_bigrat_divide(&q_power, &denominator, &error);
    if (status == PHY_OK)
        status = phy_bigrat_set_i64(&integer, 4, 1);
    if (status == PHY_OK)
        status = phy_bigrat_multiply(&error, &integer, &error);
    if (status == PHY_OK)
        status = ball_add_symmetric_error(&sum, &error, &sum);
    if (status == PHY_OK)
        status = ball_pi(&pi);
    if (status == PHY_OK)
        status = phy_real_ball_sqrt(&pi, rounds, &root_pi);
    if (status == PHY_OK)
        status = phy_real_ball_set_i64(&two, 2, 1);
    if (status == PHY_OK)
        status = phy_real_ball_divide(&two, &root_pi, &factor);
    if (status == PHY_OK)
        status = phy_real_ball_multiply(&sum, &factor, &temporary);
    if (status == PHY_OK)
        status = ball_clamp_unit(&temporary, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    while (rat_count != 0u)
        phy_bigrat_destroy(rats[--rat_count]);
    while (count != 0u)
        phy_real_ball_destroy(balls[--count]);
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_erfc(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value)
{
    phy_real_ball temporary;
    phy_real_ball erf_value;
    phy_real_ball one;
    phy_status status =
        elementary_output_init(argument, rounds, out_value, &temporary);
    memset(&erf_value, 0, sizeof erf_value);
    memset(&one, 0, sizeof one);
    if (status == PHY_OK)
        status = init_like(out_value, &erf_value);
    if (status == PHY_OK)
        status = init_like(out_value, &one);
    if (status == PHY_OK)
        status = phy_real_ball_erf(argument, rounds, &erf_value);
    if (status == PHY_OK)
        status = phy_real_ball_set_i64(&one, 1, 1);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(&one, &erf_value, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&temporary, out_value);
    phy_real_ball_destroy(&one);
    phy_real_ball_destroy(&erf_value);
    phy_real_ball_destroy(&temporary);
    return status;
}
