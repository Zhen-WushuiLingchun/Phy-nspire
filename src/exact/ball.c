#include "phy/ball.h"

#include <string.h>

#define PHY_REAL_BALL_MAGIC UINT32_C(0x42414c4c)

static bool initialized(const phy_real_ball *ball)
{
    return ball != NULL && ball->private_magic == PHY_REAL_BALL_MAGIC &&
           phy_bigint_is_initialized(
               phy_bigrat_numerator(&ball->midpoint)) &&
           phy_bigint_is_initialized(
               phy_bigrat_numerator(&ball->radius));
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
    phy_status status = phy_bigrat_swap(
        &temporary->midpoint, &out->midpoint);
    if (status == PHY_OK) {
        status = phy_bigrat_swap(&temporary->radius, &out->radius);
    }
    return status;
}

phy_status phy_real_ball_init(phy_exact_context *context,
                              phy_real_ball *ball)
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
        if (phy_bigint_is_initialized(
                phy_bigrat_numerator(&ball->radius))) {
            phy_bigrat_destroy(&ball->radius);
        }
        if (phy_bigint_is_initialized(
                phy_bigrat_numerator(&ball->midpoint))) {
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

phy_status phy_real_ball_set_i64(phy_real_ball *ball,
                                 int64_t numerator,
                                 int64_t denominator)
{
    if (!initialized(ball)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_real_ball temporary;
    phy_status status = init_like(ball, &temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(
            &temporary.midpoint, numerator, denominator);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, ball);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

phy_status phy_real_ball_set_exact(phy_real_ball *ball,
                                   const phy_bigrat *value)
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
        status = phy_bigrat_add(
            lower, upper, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(
            &temporary.midpoint, &two, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_subtract(
            upper, lower, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(
            &temporary.radius, &two, &temporary.radius);
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
        status = phy_bigrat_copy(
            &source->midpoint, &temporary.midpoint);
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

phy_status phy_real_ball_lower(const phy_real_ball *ball,
                               phy_bigrat *out_lower)
{
    if (!initialized(ball) || out_lower == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_bigrat_subtract(
        &ball->midpoint, &ball->radius, out_lower);
}

phy_status phy_real_ball_upper(const phy_real_ball *ball,
                               phy_bigrat *out_upper)
{
    if (!initialized(ball) || out_upper == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_bigrat_add(
        &ball->midpoint, &ball->radius, out_upper);
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
                             const phy_real_ball *right,
                             phy_real_ball *out_sum)
{
    if (!initialized(left) || !initialized(right) ||
        !initialized(out_sum)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_real_ball temporary;
    phy_status status = init_like(out_sum, &temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_add(
            &left->midpoint, &right->midpoint, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(
            &left->radius, &right->radius, &temporary.radius);
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
        status = phy_bigrat_subtract(
            &left->midpoint, &right->midpoint, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(
            &left->radius, &right->radius, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = publish(&temporary, out_difference);
    }
    phy_real_ball_destroy(&temporary);
    return status;
}

static phy_status absolute_rational(const phy_bigrat *value,
                                    phy_bigrat *out)
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
    phy_bigrat *values[] = {
        &left_abs, &right_abs, &first, &second, &third};
    size_t initialized_count = 0u;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(
            context_of(out_product), values[initialized_count]);
        if (status == PHY_OK) {
            initialized_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &left->midpoint, &right->midpoint, &temporary.midpoint);
    }
    if (status == PHY_OK) {
        status = absolute_rational(&left->midpoint, &left_abs);
    }
    if (status == PHY_OK) {
        status = absolute_rational(&right->midpoint, &right_abs);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &left_abs, &right->radius, &first);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &right_abs, &left->radius, &second);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &left->radius, &right->radius, &third);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(&first, &second, &temporary.radius);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(
            &temporary.radius, &third, &temporary.radius);
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
    phy_status status = phy_real_ball_contains_zero_checked(
        divisor, &divisor_contains_zero);
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
    phy_bigrat *values[] = {
        &lower, &upper, &reciprocal_lower, &reciprocal_upper};
    status = PHY_OK;
    size_t initialized_count = 0u;
    while (status == PHY_OK &&
           initialized_count < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(
            context_of(out_quotient), values[initialized_count]);
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
        status = phy_real_ball_set_interval(
            &reciprocal, &reciprocal_lower, &reciprocal_upper);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(
            dividend, &reciprocal, out_quotient);
    }
    phy_real_ball_destroy(&reciprocal);
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    return status;
}

phy_status phy_real_ball_pow_i32(const phy_real_ball *base,
                                 int32_t exponent,
                                 phy_real_ball *out_power)
{
    if (!initialized(base) || !initialized(out_power)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (exponent < 0) {
        bool base_contains_zero = true;
        phy_status zero_status = phy_real_ball_contains_zero_checked(
            base, &base_contains_zero);
        if (zero_status != PHY_OK) {
            return zero_status;
        }
        if (base_contains_zero) {
            return PHY_ERR_DOMAIN;
        }
    }
    const uint32_t magnitude = exponent < 0
                                   ? (uint32_t)(-(int64_t)exponent)
                                   : (uint32_t)exponent;
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
            status = phy_real_ball_multiply(
                &result, &factor, &temporary);
            if (status == PHY_OK) {
                status = phy_real_ball_copy(&temporary, &result);
            }
        }
        power >>= 1u;
        if (status == PHY_OK && power != 0u) {
            status = phy_real_ball_multiply(
                &factor, &factor, &temporary);
            if (status == PHY_OK) {
                status = phy_real_ball_copy(&temporary, &factor);
            }
        }
    }
    if (status == PHY_OK && exponent < 0) {
        status = phy_real_ball_set_i64(&factor, 1, 1);
        if (status == PHY_OK) {
            status = phy_real_ball_divide(
                &factor, &result, &temporary);
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

static phy_status sqrt_interval_bound(const phy_bigrat *value,
                                      uint32_t rounds, bool upper_bound,
                                      phy_bigrat *out)
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
        status = order > 0
                     ? phy_bigrat_copy(value, &high)
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

phy_status phy_real_ball_sqrt(const phy_real_ball *argument,
                              uint32_t rounds,
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
        status = phy_real_ball_set_interval(
            out_root, &root_lower, &root_upper);
    }
    while (initialized_count != 0u) {
        phy_bigrat_destroy(values[--initialized_count]);
    }
    return status;
}
