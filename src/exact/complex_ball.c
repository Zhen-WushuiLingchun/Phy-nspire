#include "phy/ball.h"

#include <string.h>

#define PHY_COMPLEX_BALL_MAGIC UINT32_C(0x43424C31)

static bool complex_initialized(const phy_complex_ball *ball)
{
    return ball != NULL && ball->private_magic == PHY_COMPLEX_BALL_MAGIC &&
           phy_bigint_is_initialized(
               phy_bigrat_numerator(&ball->real.midpoint)) &&
           phy_bigint_is_initialized(
               phy_bigrat_numerator(&ball->real.radius)) &&
           phy_bigint_is_initialized(
               phy_bigrat_numerator(&ball->imaginary.midpoint)) &&
           phy_bigint_is_initialized(
               phy_bigrat_numerator(&ball->imaginary.radius));
}

static phy_exact_context *complex_context(const phy_complex_ball *ball)
{
    return phy_bigrat_numerator(&ball->real.midpoint)->context;
}

static bool compatible(const phy_complex_ball *left,
                       const phy_complex_ball *right,
                       const phy_complex_ball *out)
{
    return complex_initialized(left) && complex_initialized(right) &&
           complex_initialized(out) &&
           complex_context(left) == complex_context(right) &&
           complex_context(left) == complex_context(out);
}

static phy_status publish(phy_complex_ball *temporary, phy_complex_ball *out)
{
    if (!complex_initialized(temporary) || !complex_initialized(out) ||
        complex_context(temporary) != complex_context(out)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_bigrat *temporary_values[] = {
        &temporary->real.midpoint, &temporary->real.radius,
        &temporary->imaginary.midpoint, &temporary->imaginary.radius};
    phy_bigrat *output_values[] = {
        &out->real.midpoint, &out->real.radius,
        &out->imaginary.midpoint, &out->imaginary.radius};
    size_t swapped = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && swapped < 4u) {
        status = phy_bigrat_swap(
            temporary_values[swapped], output_values[swapped]);
        if (status == PHY_OK) swapped++;
    }
    if (status != PHY_OK) {
        while (swapped != 0u) {
            swapped--;
            (void)phy_bigrat_swap(
                temporary_values[swapped], output_values[swapped]);
        }
    }
    return status;
}

phy_status phy_complex_ball_init(phy_exact_context *context,
                                 phy_complex_ball *ball)
{
    if (context == NULL || ball == NULL)
        return PHY_ERR_INVALID_ARGUMENT;
    memset(ball, 0, sizeof *ball);
    phy_status status = phy_real_ball_init(context, &ball->real);
    if (status == PHY_OK) {
        status = phy_real_ball_init(context, &ball->imaginary);
    }
    if (status == PHY_OK) {
        ball->private_magic = PHY_COMPLEX_BALL_MAGIC;
    } else {
        phy_real_ball_destroy(&ball->imaginary);
        phy_real_ball_destroy(&ball->real);
        memset(ball, 0, sizeof *ball);
    }
    return status;
}

void phy_complex_ball_destroy(phy_complex_ball *ball)
{
    if (ball == NULL)
        return;
    if (complex_initialized(ball)) {
        phy_real_ball_destroy(&ball->imaginary);
        phy_real_ball_destroy(&ball->real);
    }
    memset(ball, 0, sizeof *ball);
}

phy_status phy_complex_ball_validate(const phy_complex_ball *ball)
{
    if (!complex_initialized(ball))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_status status = phy_real_ball_validate(&ball->real);
    if (status == PHY_OK)
        status = phy_real_ball_validate(&ball->imaginary);
    if (status == PHY_OK &&
        phy_bigrat_numerator(&ball->real.midpoint)->context !=
            phy_bigrat_numerator(&ball->imaginary.midpoint)->context) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    return status;
}

phy_status phy_complex_ball_set_i64(phy_complex_ball *ball,
                                    int64_t real_numerator,
                                    int64_t real_denominator,
                                    int64_t imaginary_numerator,
                                    int64_t imaginary_denominator)
{
    if (!complex_initialized(ball))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_complex_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    phy_status status =
        phy_complex_ball_init(complex_context(ball), &temporary);
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&temporary.real, real_numerator,
                                       real_denominator);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(
            &temporary.imaginary, imaginary_numerator, imaginary_denominator);
    }
    if (status == PHY_OK)
        status = publish(&temporary, ball);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_set_real(phy_complex_ball *ball,
                                     const phy_real_ball *real)
{
    if (!complex_initialized(ball) || real == NULL)
        return PHY_ERR_INVALID_ARGUMENT;
    phy_status status = phy_real_ball_validate(real);
    if (status != PHY_OK)
        return status;
    if (phy_bigrat_numerator(&real->midpoint)->context !=
        complex_context(ball)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_complex_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    status = phy_complex_ball_init(complex_context(ball), &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(real, &temporary.real);
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&temporary.imaginary, 0, 1);
    }
    if (status == PHY_OK)
        status = publish(&temporary, ball);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_copy(const phy_complex_ball *source,
                                 phy_complex_ball *destination)
{
    if (!compatible(source, source, destination)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_complex_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    phy_status status =
        phy_complex_ball_init(complex_context(destination), &temporary);
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&source->real, &temporary.real);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_copy(&source->imaginary, &temporary.imaginary);
    }
    if (status == PHY_OK)
        status = publish(&temporary, destination);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_contains_zero_checked(const phy_complex_ball *ball,
                                                  bool *out_contains_zero)
{
    if (!complex_initialized(ball) || out_contains_zero == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_contains_zero = true;
    bool real_zero = true;
    bool imaginary_zero = true;
    phy_status status =
        phy_real_ball_contains_zero_checked(&ball->real, &real_zero);
    if (status == PHY_OK) {
        status = phy_real_ball_contains_zero_checked(&ball->imaginary,
                                                     &imaginary_zero);
    }
    if (status == PHY_OK)
        *out_contains_zero = real_zero && imaginary_zero;
    return status;
}

bool phy_complex_ball_contains_zero(const phy_complex_ball *ball)
{
    bool contains = true;
    return phy_complex_ball_contains_zero_checked(ball, &contains) == PHY_OK
               ? contains
               : true;
}

static phy_status real_negate(const phy_real_ball *value, phy_real_ball *out)
{
    phy_exact_context *context = phy_bigrat_numerator(&out->midpoint)->context;
    phy_real_ball minus_one;
    memset(&minus_one, 0, sizeof minus_one);
    phy_status status = phy_real_ball_init(context, &minus_one);
    if (status == PHY_OK)
        status = phy_real_ball_set_i64(&minus_one, -1, 1);
    if (status == PHY_OK)
        status = phy_real_ball_multiply(value, &minus_one, out);
    phy_real_ball_destroy(&minus_one);
    return status;
}

static phy_status real_scale(const phy_real_ball *value, int64_t numerator,
                             int64_t denominator, phy_real_ball *out)
{
    phy_exact_context *context = phy_bigrat_numerator(&out->midpoint)->context;
    phy_real_ball scalar;
    memset(&scalar, 0, sizeof scalar);
    phy_status status = phy_real_ball_init(context, &scalar);
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&scalar, numerator, denominator);
    }
    if (status == PHY_OK)
        status = phy_real_ball_multiply(value, &scalar, out);
    phy_real_ball_destroy(&scalar);
    return status;
}

static phy_status real_clamp_nonnegative(const phy_real_ball *value,
                                         phy_real_ball *out)
{
    phy_exact_context *context = phy_bigrat_numerator(&out->midpoint)->context;
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat zero;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    memset(&zero, 0, sizeof zero);
    phy_status status = phy_bigrat_init(context, &lower);
    if (status == PHY_OK)
        status = phy_bigrat_init(context, &upper);
    if (status == PHY_OK)
        status = phy_bigrat_init(context, &zero);
    if (status == PHY_OK)
        status = phy_real_ball_lower(value, &lower);
    if (status == PHY_OK)
        status = phy_real_ball_upper(value, &upper);
    if (status == PHY_OK)
        status = phy_bigrat_set_i64(&zero, 0, 1);
    if (status == PHY_OK && phy_bigrat_sign(&upper) < 0) {
        status = PHY_ERR_DOMAIN;
    }
    if (status == PHY_OK && phy_bigrat_sign(&lower) < 0) {
        status = phy_bigrat_copy(&zero, &lower);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_interval(out, &lower, &upper);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&zero))) {
        phy_bigrat_destroy(&zero);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&upper))) {
        phy_bigrat_destroy(&upper);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&lower))) {
        phy_bigrat_destroy(&lower);
    }
    return status;
}

static phy_status real_square(const phy_real_ball *value, phy_real_ball *out)
{
    phy_real_ball product;
    memset(&product, 0, sizeof product);
    phy_exact_context *context = phy_bigrat_numerator(&out->midpoint)->context;
    phy_status status = phy_real_ball_init(context, &product);
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(value, value, &product);
    }
    if (status == PHY_OK)
        status = real_clamp_nonnegative(&product, out);
    phy_real_ball_destroy(&product);
    return status;
}

static phy_status real_pi(phy_real_ball *out)
{
    static const char scale[] = "10000000000000000000000000000000000000000";
    phy_exact_context *context = phy_bigrat_numerator(&out->midpoint)->context;
    phy_bigrat lower;
    phy_bigrat upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    phy_status status = phy_bigrat_init(context, &lower);
    if (status == PHY_OK)
        status = phy_bigrat_init(context, &upper);
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

static phy_status real_signs(const phy_real_ball *value, int *out_lower_sign,
                             int *out_upper_sign, bool *out_exact_zero)
{
    phy_exact_context *context =
        phy_bigrat_numerator(&value->midpoint)->context;
    phy_bigrat lower;
    phy_bigrat upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    phy_status status = phy_bigrat_init(context, &lower);
    if (status == PHY_OK)
        status = phy_bigrat_init(context, &upper);
    if (status == PHY_OK)
        status = phy_real_ball_lower(value, &lower);
    if (status == PHY_OK)
        status = phy_real_ball_upper(value, &upper);
    if (status == PHY_OK) {
        *out_lower_sign = phy_bigrat_sign(&lower);
        *out_upper_sign = phy_bigrat_sign(&upper);
        *out_exact_zero = *out_lower_sign == 0 && *out_upper_sign == 0;
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&upper))) {
        phy_bigrat_destroy(&upper);
    }
    if (phy_bigint_is_initialized(phy_bigrat_numerator(&lower))) {
        phy_bigrat_destroy(&lower);
    }
    return status;
}

static phy_status real_within_unit(const phy_real_ball *value,
                                   bool *out_within)
{
    if (out_within == NULL) return PHY_ERR_INVALID_ARGUMENT;
    *out_within = false;
    phy_exact_context *context =
        phy_bigrat_numerator(&value->midpoint)->context;
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat minus_one;
    phy_bigrat one;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    memset(&minus_one, 0, sizeof minus_one);
    memset(&one, 0, sizeof one);
    phy_bigrat *values[] = {&lower, &upper, &minus_one, &one};
    size_t count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && count < 4u) {
        status = phy_bigrat_init(context, values[count]);
        if (status == PHY_OK) count++;
    }
    if (status == PHY_OK) status = phy_real_ball_lower(value, &lower);
    if (status == PHY_OK) status = phy_real_ball_upper(value, &upper);
    if (status == PHY_OK) status = phy_bigrat_set_i64(&minus_one, -1, 1);
    if (status == PHY_OK) status = phy_bigrat_set_i64(&one, 1, 1);
    int lower_order = -1;
    int upper_order = 1;
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&lower, &minus_one, &lower_order);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&upper, &one, &upper_order);
    }
    if (status == PHY_OK) {
        *out_within = lower_order >= 0 && upper_order <= 0;
    }
    while (count != 0u) phy_bigrat_destroy(values[--count]);
    return status;
}

static phy_status complex_norm_squared(const phy_complex_ball *value,
                                       phy_real_ball *out)
{
    phy_exact_context *context = complex_context(value);
    phy_real_ball real_square_value;
    phy_real_ball imaginary_square_value;
    memset(&real_square_value, 0, sizeof real_square_value);
    memset(&imaginary_square_value, 0, sizeof imaginary_square_value);
    phy_status status = phy_real_ball_init(context, &real_square_value);
    if (status == PHY_OK) {
        status = phy_real_ball_init(context, &imaginary_square_value);
    }
    if (status == PHY_OK)
        status = real_square(&value->real, &real_square_value);
    if (status == PHY_OK) {
        status = real_square(&value->imaginary, &imaginary_square_value);
    }
    if (status == PHY_OK) {
        status =
            phy_real_ball_add(&real_square_value, &imaginary_square_value, out);
    }
    phy_real_ball_destroy(&imaginary_square_value);
    phy_real_ball_destroy(&real_square_value);
    return status;
}

phy_status phy_complex_ball_add(const phy_complex_ball *left,
                                const phy_complex_ball *right,
                                phy_complex_ball *out_sum)
{
    if (!compatible(left, right, out_sum))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_complex_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    phy_status status =
        phy_complex_ball_init(complex_context(out_sum), &temporary);
    if (status == PHY_OK) {
        status = phy_real_ball_add(&left->real, &right->real, &temporary.real);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_add(&left->imaginary, &right->imaginary,
                                   &temporary.imaginary);
    }
    if (status == PHY_OK)
        status = publish(&temporary, out_sum);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_subtract(const phy_complex_ball *left,
                                     const phy_complex_ball *right,
                                     phy_complex_ball *out_difference)
{
    if (!compatible(left, right, out_difference)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_complex_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    phy_status status =
        phy_complex_ball_init(complex_context(out_difference), &temporary);
    if (status == PHY_OK) {
        status =
            phy_real_ball_subtract(&left->real, &right->real, &temporary.real);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_subtract(&left->imaginary, &right->imaginary,
                                        &temporary.imaginary);
    }
    if (status == PHY_OK)
        status = publish(&temporary, out_difference);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_multiply(const phy_complex_ball *left,
                                     const phy_complex_ball *right,
                                     phy_complex_ball *out_product)
{
    if (!compatible(left, right, out_product))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *context = complex_context(out_product);
    phy_complex_ball temporary;
    phy_real_ball ac, bd, ad, bc;
    memset(&temporary, 0, sizeof temporary);
    memset(&ac, 0, sizeof ac);
    memset(&bd, 0, sizeof bd);
    memset(&ad, 0, sizeof ad);
    memset(&bc, 0, sizeof bc);
    phy_status status = phy_complex_ball_init(context, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &ac);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &bd);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &ad);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &bc);
    if (status == PHY_OK)
        status = phy_real_ball_multiply(&left->real, &right->real, &ac);
    if (status == PHY_OK)
        status =
            phy_real_ball_multiply(&left->imaginary, &right->imaginary, &bd);
    if (status == PHY_OK)
        status = phy_real_ball_multiply(&left->real, &right->imaginary, &ad);
    if (status == PHY_OK)
        status = phy_real_ball_multiply(&left->imaginary, &right->real, &bc);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(&ac, &bd, &temporary.real);
    if (status == PHY_OK)
        status = phy_real_ball_add(&ad, &bc, &temporary.imaginary);
    if (status == PHY_OK)
        status = publish(&temporary, out_product);
    phy_real_ball_destroy(&bc);
    phy_real_ball_destroy(&ad);
    phy_real_ball_destroy(&bd);
    phy_real_ball_destroy(&ac);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_divide(const phy_complex_ball *dividend,
                                   const phy_complex_ball *divisor,
                                   phy_complex_ball *out_quotient)
{
    if (!compatible(dividend, divisor, out_quotient)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = complex_context(out_quotient);
    phy_complex_ball temporary;
    phy_real_ball denominator, ac, bd, bc, ad, numerator;
    memset(&temporary, 0, sizeof temporary);
    memset(&denominator, 0, sizeof denominator);
    memset(&ac, 0, sizeof ac);
    memset(&bd, 0, sizeof bd);
    memset(&bc, 0, sizeof bc);
    memset(&ad, 0, sizeof ad);
    memset(&numerator, 0, sizeof numerator);
    phy_status status = phy_complex_ball_init(context, &temporary);
    phy_real_ball *balls[] = {&denominator, &ac, &bd, &bc, &ad, &numerator};
    size_t count = 0u;
    while (status == PHY_OK && count < sizeof balls / sizeof balls[0]) {
        status = phy_real_ball_init(context, balls[count]);
        if (status == PHY_OK)
            count++;
    }
    if (status == PHY_OK)
        status = complex_norm_squared(divisor, &denominator);
    bool contains_zero = true;
    if (status == PHY_OK)
        status =
            phy_real_ball_contains_zero_checked(&denominator, &contains_zero);
    if (status == PHY_OK && contains_zero)
        status = PHY_ERR_DOMAIN;
    if (status == PHY_OK)
        status = phy_real_ball_multiply(&dividend->real, &divisor->real, &ac);
    if (status == PHY_OK)
        status = phy_real_ball_multiply(&dividend->imaginary,
                                        &divisor->imaginary, &bd);
    if (status == PHY_OK)
        status = phy_real_ball_add(&ac, &bd, &numerator);
    if (status == PHY_OK)
        status =
            phy_real_ball_divide(&numerator, &denominator, &temporary.real);
    if (status == PHY_OK)
        status =
            phy_real_ball_multiply(&dividend->imaginary, &divisor->real, &bc);
    if (status == PHY_OK)
        status =
            phy_real_ball_multiply(&dividend->real, &divisor->imaginary, &ad);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(&bc, &ad, &numerator);
    if (status == PHY_OK)
        status = phy_real_ball_divide(&numerator, &denominator,
                                      &temporary.imaginary);
    if (status == PHY_OK)
        status = publish(&temporary, out_quotient);
    while (count != 0u)
        phy_real_ball_destroy(balls[--count]);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_conjugate(const phy_complex_ball *argument,
                                      phy_complex_ball *out_value)
{
    if (!compatible(argument, argument, out_value))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_complex_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    phy_status status =
        phy_complex_ball_init(complex_context(out_value), &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&argument->real, &temporary.real);
    if (status == PHY_OK)
        status = real_negate(&argument->imaginary, &temporary.imaginary);
    if (status == PHY_OK)
        status = publish(&temporary, out_value);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_pow_i32(const phy_complex_ball *base,
                                    int32_t exponent,
                                    phy_complex_ball *out_power)
{
    if (!compatible(base, base, out_power))
        return PHY_ERR_INVALID_ARGUMENT;
    if (exponent < 0 && phy_complex_ball_contains_zero(base))
        return PHY_ERR_DOMAIN;
    phy_exact_context *context = complex_context(out_power);
    phy_complex_ball result, factor, next, one;
    memset(&result, 0, sizeof result);
    memset(&factor, 0, sizeof factor);
    memset(&next, 0, sizeof next);
    memset(&one, 0, sizeof one);
    phy_complex_ball *balls[] = {&result, &factor, &next, &one};
    size_t count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && count < 4u) {
        status = phy_complex_ball_init(context, balls[count]);
        if (status == PHY_OK)
            count++;
    }
    if (status == PHY_OK)
        status = phy_complex_ball_set_i64(&result, 1, 1, 0, 1);
    if (status == PHY_OK)
        status = phy_complex_ball_copy(base, &factor);
    uint32_t power =
        exponent < 0 ? (uint32_t)(-(int64_t)exponent) : (uint32_t)exponent;
    while (status == PHY_OK && power != 0u) {
        if ((power & 1u) != 0u) {
            status = phy_complex_ball_multiply(&result, &factor, &next);
            if (status == PHY_OK)
                status = phy_complex_ball_copy(&next, &result);
        }
        power >>= 1u;
        if (status == PHY_OK && power != 0u) {
            status = phy_complex_ball_multiply(&factor, &factor, &next);
            if (status == PHY_OK)
                status = phy_complex_ball_copy(&next, &factor);
        }
    }
    if (status == PHY_OK && exponent < 0) {
        status = phy_complex_ball_set_i64(&one, 1, 1, 0, 1);
        if (status == PHY_OK)
            status = phy_complex_ball_divide(&one, &result, &next);
        if (status == PHY_OK)
            status = phy_complex_ball_copy(&next, &result);
    }
    if (status == PHY_OK)
        status = phy_complex_ball_copy(&result, out_power);
    while (count != 0u)
        phy_complex_ball_destroy(balls[--count]);
    return status;
}

static phy_status complex_scale(const phy_complex_ball *value,
                                int64_t numerator, int64_t denominator,
                                phy_complex_ball *out)
{
    if (!compatible(value, value, out))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_complex_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    phy_status status = phy_complex_ball_init(complex_context(out), &temporary);
    if (status == PHY_OK)
        status =
            real_scale(&value->real, numerator, denominator, &temporary.real);
    if (status == PHY_OK)
        status = real_scale(&value->imaginary, numerator, denominator,
                            &temporary.imaginary);
    if (status == PHY_OK)
        status = publish(&temporary, out);
    phy_complex_ball_destroy(&temporary);
    return status;
}

static phy_status multiply_i(const phy_complex_ball *value, bool negative_i,
                             phy_complex_ball *out)
{
    if (!compatible(value, value, out))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_complex_ball temporary;
    memset(&temporary, 0, sizeof temporary);
    phy_status status = phy_complex_ball_init(complex_context(out), &temporary);
    if (negative_i) {
        if (status == PHY_OK)
            status = phy_real_ball_copy(&value->imaginary, &temporary.real);
        if (status == PHY_OK)
            status = real_negate(&value->real, &temporary.imaginary);
    } else {
        if (status == PHY_OK)
            status = real_negate(&value->imaginary, &temporary.real);
        if (status == PHY_OK)
            status = phy_real_ball_copy(&value->real, &temporary.imaginary);
    }
    if (status == PHY_OK)
        status = publish(&temporary, out);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_sqrt(const phy_complex_ball *argument,
                                 uint32_t rounds, phy_complex_ball *out_value)
{
    if (!compatible(argument, argument, out_value))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *context = complex_context(out_value);
    phy_complex_ball temporary;
    phy_real_ball norm2, r, uarg, varg, u, v;
    memset(&temporary, 0, sizeof temporary);
    memset(&norm2, 0, sizeof norm2);
    memset(&r, 0, sizeof r);
    memset(&uarg, 0, sizeof uarg);
    memset(&varg, 0, sizeof varg);
    memset(&u, 0, sizeof u);
    memset(&v, 0, sizeof v);
    phy_status status = phy_complex_ball_init(context, &temporary);
    phy_real_ball *balls[] = {&norm2, &r, &uarg, &varg, &u, &v};
    size_t count = 0u;
    while (status == PHY_OK && count < 6u) {
        status = phy_real_ball_init(context, balls[count]);
        if (status == PHY_OK)
            count++;
    }
    if (status == PHY_OK)
        status = complex_norm_squared(argument, &norm2);
    if (status == PHY_OK)
        status = phy_real_ball_sqrt(&norm2, rounds, &r);
    if (status == PHY_OK)
        status = phy_real_ball_add(&r, &argument->real, &uarg);
    if (status == PHY_OK)
        status = real_scale(&uarg, 1, 2, &uarg);
    if (status == PHY_OK)
        status = real_clamp_nonnegative(&uarg, &uarg);
    if (status == PHY_OK)
        status = phy_real_ball_sqrt(&uarg, rounds, &u);
    if (status == PHY_OK)
        status = phy_real_ball_subtract(&r, &argument->real, &varg);
    if (status == PHY_OK)
        status = real_scale(&varg, 1, 2, &varg);
    if (status == PHY_OK)
        status = real_clamp_nonnegative(&varg, &varg);
    if (status == PHY_OK)
        status = phy_real_ball_sqrt(&varg, rounds, &v);
    int lower_sign = 0, upper_sign = 0;
    bool exact_zero = false;
    if (status == PHY_OK)
        status = real_signs(&argument->imaginary, &lower_sign, &upper_sign,
                            &exact_zero);
    if (status == PHY_OK)
        status = phy_real_ball_copy(&u, &temporary.real);
    if (status == PHY_OK && upper_sign < 0)
        status = real_negate(&v, &temporary.imaginary);
    else if (status == PHY_OK && (lower_sign > 0 || exact_zero))
        status = phy_real_ball_copy(&v, &temporary.imaginary);
    else if (status == PHY_OK) {
        phy_bigrat upper, lower;
        memset(&upper, 0, sizeof upper);
        memset(&lower, 0, sizeof lower);
        status = phy_bigrat_init(context, &upper);
        if (status == PHY_OK)
            status = phy_bigrat_init(context, &lower);
        if (status == PHY_OK)
            status = phy_real_ball_upper(&v, &upper);
        if (status == PHY_OK)
            status = phy_bigrat_negate(&upper, &lower);
        if (status == PHY_OK)
            status = phy_real_ball_set_interval(&temporary.imaginary, &lower,
                                                &upper);
        if (phy_bigint_is_initialized(phy_bigrat_numerator(&lower)))
            phy_bigrat_destroy(&lower);
        if (phy_bigint_is_initialized(phy_bigrat_numerator(&upper)))
            phy_bigrat_destroy(&upper);
    }
    if (status == PHY_OK)
        status = publish(&temporary, out_value);
    while (count != 0u)
        phy_real_ball_destroy(balls[--count]);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_exp(const phy_complex_ball *argument,
                                uint32_t rounds, phy_complex_ball *out_value)
{
    if (!compatible(argument, argument, out_value))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *context = complex_context(out_value);
    phy_complex_ball temporary;
    phy_real_ball ex, cy, sy;
    memset(&temporary, 0, sizeof temporary);
    memset(&ex, 0, sizeof ex);
    memset(&cy, 0, sizeof cy);
    memset(&sy, 0, sizeof sy);
    phy_status status = phy_complex_ball_init(context, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &ex);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &cy);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &sy);
    if (status == PHY_OK)
        status = phy_real_ball_exp(&argument->real, rounds, &ex);
    if (status == PHY_OK)
        status = phy_real_ball_cos(&argument->imaginary, rounds, &cy);
    if (status == PHY_OK)
        status = phy_real_ball_sin(&argument->imaginary, rounds, &sy);
    if (status == PHY_OK)
        status = phy_real_ball_multiply(&ex, &cy, &temporary.real);
    if (status == PHY_OK)
        status = phy_real_ball_multiply(&ex, &sy, &temporary.imaginary);
    if (status == PHY_OK)
        status = publish(&temporary, out_value);
    phy_real_ball_destroy(&sy);
    phy_real_ball_destroy(&cy);
    phy_real_ball_destroy(&ex);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_log(const phy_complex_ball *argument,
                                uint32_t rounds, phy_complex_ball *out_value)
{
    if (!compatible(argument, argument, out_value))
        return PHY_ERR_INVALID_ARGUMENT;
    bool contains = true;
    phy_status status =
        phy_complex_ball_contains_zero_checked(argument, &contains);
    if (status == PHY_OK && contains)
        status = PHY_ERR_DOMAIN;
    phy_exact_context *context = complex_context(out_value);
    phy_complex_ball temporary;
    phy_real_ball norm2, ratio, angle, pi, halfpi;
    memset(&temporary, 0, sizeof temporary);
    memset(&norm2, 0, sizeof norm2);
    memset(&ratio, 0, sizeof ratio);
    memset(&angle, 0, sizeof angle);
    memset(&pi, 0, sizeof pi);
    memset(&halfpi, 0, sizeof halfpi);
    if (status == PHY_OK)
        status = phy_complex_ball_init(context, &temporary);
    phy_real_ball *balls[] = {&norm2, &ratio, &angle, &pi, &halfpi};
    size_t count = 0u;
    while (status == PHY_OK && count < 5u) {
        status = phy_real_ball_init(context, balls[count]);
        if (status == PHY_OK)
            count++;
    }
    if (status == PHY_OK)
        status = complex_norm_squared(argument, &norm2);
    if (status == PHY_OK)
        status = phy_real_ball_log(&norm2, rounds, &temporary.real);
    if (status == PHY_OK)
        status = real_scale(&temporary.real, 1, 2, &temporary.real);
    int xl = 0, xu = 0, yl = 0, yu = 0;
    bool xzero = false, yzero = false;
    if (status == PHY_OK)
        status = real_signs(&argument->real, &xl, &xu, &xzero);
    if (status == PHY_OK)
        status = real_signs(&argument->imaginary, &yl, &yu, &yzero);
    if (status == PHY_OK)
        status = real_pi(&pi);
    if (status == PHY_OK)
        status = real_scale(&pi, 1, 2, &halfpi);
    if (status == PHY_OK && xl > 0) {
        status =
            phy_real_ball_divide(&argument->imaginary, &argument->real, &ratio);
        if (status == PHY_OK)
            status = phy_real_ball_atan(&ratio, rounds, &angle);
    } else if (status == PHY_OK && xu < 0) {
        if (yzero || yl >= 0) {
            status = phy_real_ball_divide(&argument->imaginary, &argument->real,
                                          &ratio);
            if (status == PHY_OK)
                status = phy_real_ball_atan(&ratio, rounds, &angle);
            if (status == PHY_OK)
                status = phy_real_ball_add(&angle, &pi, &angle);
        } else if (yu <= 0) {
            status = phy_real_ball_divide(&argument->imaginary, &argument->real,
                                          &ratio);
            if (status == PHY_OK)
                status = phy_real_ball_atan(&ratio, rounds, &angle);
            if (status == PHY_OK)
                status = phy_real_ball_subtract(&angle, &pi, &angle);
        } else
            status = PHY_ERR_DOMAIN;
    } else if (status == PHY_OK && yl > 0) {
        status =
            phy_real_ball_divide(&argument->real, &argument->imaginary, &ratio);
        if (status == PHY_OK)
            status = phy_real_ball_atan(&ratio, rounds, &angle);
        if (status == PHY_OK)
            status = phy_real_ball_subtract(&halfpi, &angle, &angle);
    } else if (status == PHY_OK && yu < 0) {
        status =
            phy_real_ball_divide(&argument->real, &argument->imaginary, &ratio);
        if (status == PHY_OK)
            status = phy_real_ball_atan(&ratio, rounds, &angle);
        if (status == PHY_OK)
            status = real_negate(&halfpi, &temporary.imaginary);
        if (status == PHY_OK)
            status =
                phy_real_ball_subtract(&temporary.imaginary, &angle, &angle);
    } else if (status == PHY_OK)
        status = PHY_ERR_DOMAIN;
    if (status == PHY_OK)
        status = phy_real_ball_copy(&angle, &temporary.imaginary);
    if (status == PHY_OK)
        status = publish(&temporary, out_value);
    while (count != 0u)
        phy_real_ball_destroy(balls[--count]);
    phy_complex_ball_destroy(&temporary);
    return status;
}

static phy_status complex_trig(const phy_complex_ball *argument,
                               uint32_t rounds, bool cosine,
                               phy_complex_ball *out)
{
    if (!compatible(argument, argument, out))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *context = complex_context(out);
    phy_complex_ball temporary;
    phy_real_ball sx, cx, shy, chy;
    memset(&temporary, 0, sizeof temporary);
    memset(&sx, 0, sizeof sx);
    memset(&cx, 0, sizeof cx);
    memset(&shy, 0, sizeof shy);
    memset(&chy, 0, sizeof chy);
    phy_status status = phy_complex_ball_init(context, &temporary);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &sx);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &cx);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &shy);
    if (status == PHY_OK)
        status = phy_real_ball_init(context, &chy);
    if (status == PHY_OK)
        status = phy_real_ball_sin(&argument->real, rounds, &sx);
    if (status == PHY_OK)
        status = phy_real_ball_cos(&argument->real, rounds, &cx);
    if (status == PHY_OK)
        status = phy_real_ball_sinh(&argument->imaginary, rounds, &shy);
    if (status == PHY_OK)
        status = phy_real_ball_cosh(&argument->imaginary, rounds, &chy);
    if (!cosine) {
        if (status == PHY_OK)
            status = phy_real_ball_multiply(&sx, &chy, &temporary.real);
        if (status == PHY_OK)
            status = phy_real_ball_multiply(&cx, &shy, &temporary.imaginary);
    } else {
        if (status == PHY_OK)
            status = phy_real_ball_multiply(&cx, &chy, &temporary.real);
        if (status == PHY_OK)
            status = phy_real_ball_multiply(&sx, &shy, &temporary.imaginary);
        if (status == PHY_OK)
            status = real_negate(&temporary.imaginary, &temporary.imaginary);
    }
    if (status == PHY_OK)
        status = publish(&temporary, out);
    phy_real_ball_destroy(&chy);
    phy_real_ball_destroy(&shy);
    phy_real_ball_destroy(&cx);
    phy_real_ball_destroy(&sx);
    phy_complex_ball_destroy(&temporary);
    return status;
}

phy_status phy_complex_ball_sin(const phy_complex_ball *a, uint32_t r,
                                phy_complex_ball *o)
{
    return complex_trig(a, r, false, o);
}
phy_status phy_complex_ball_cos(const phy_complex_ball *a, uint32_t r,
                                phy_complex_ball *o)
{
    return complex_trig(a, r, true, o);
}
phy_status phy_complex_ball_tan(const phy_complex_ball *a, uint32_t r,
                                phy_complex_ball *o)
{
    if (!compatible(a, a, o))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_complex_ball s, c, t;
    memset(&s, 0, sizeof s);
    memset(&c, 0, sizeof c);
    memset(&t, 0, sizeof t);
    phy_status status = phy_complex_ball_init(complex_context(o), &s);
    if (status == PHY_OK)
        status = phy_complex_ball_init(complex_context(o), &c);
    if (status == PHY_OK)
        status = phy_complex_ball_init(complex_context(o), &t);
    if (status == PHY_OK)
        status = phy_complex_ball_sin(a, r, &s);
    if (status == PHY_OK)
        status = phy_complex_ball_cos(a, r, &c);
    if (status == PHY_OK)
        status = phy_complex_ball_divide(&s, &c, &t);
    if (status == PHY_OK)
        status = phy_complex_ball_copy(&t, o);
    phy_complex_ball_destroy(&t);
    phy_complex_ball_destroy(&c);
    phy_complex_ball_destroy(&s);
    return status;
}

static phy_status complex_hyperbolic(const phy_complex_ball *argument,
                                     uint32_t rounds, bool cosine,
                                     phy_complex_ball *out)
{
    if (!compatible(argument, argument, out)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = complex_context(out);
    phy_complex_ball negative_argument;
    phy_complex_ball positive_exp;
    phy_complex_ball negative_exp;
    phy_complex_ball combined;
    memset(&negative_argument, 0, sizeof negative_argument);
    memset(&positive_exp, 0, sizeof positive_exp);
    memset(&negative_exp, 0, sizeof negative_exp);
    memset(&combined, 0, sizeof combined);
    phy_complex_ball *balls[] = {
        &negative_argument, &positive_exp, &negative_exp, &combined};
    size_t count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && count < 4u) {
        status = phy_complex_ball_init(context, balls[count]);
        if (status == PHY_OK) count++;
    }
    if (status == PHY_OK) {
        status = complex_scale(argument, -1, 1, &negative_argument);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_exp(argument, rounds, &positive_exp);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_exp(
            &negative_argument, rounds, &negative_exp);
    }
    if (status == PHY_OK) {
        status = cosine
                     ? phy_complex_ball_add(
                           &positive_exp, &negative_exp, &combined)
                     : phy_complex_ball_subtract(
                           &positive_exp, &negative_exp, &combined);
    }
    if (status == PHY_OK) status = complex_scale(&combined, 1, 2, out);
    while (count != 0u) phy_complex_ball_destroy(balls[--count]);
    return status;
}

phy_status phy_complex_ball_sinh(const phy_complex_ball *argument,
                                 uint32_t rounds,
                                 phy_complex_ball *out_value)
{
    return complex_hyperbolic(argument, rounds, false, out_value);
}

phy_status phy_complex_ball_cosh(const phy_complex_ball *argument,
                                 uint32_t rounds,
                                 phy_complex_ball *out_value)
{
    return complex_hyperbolic(argument, rounds, true, out_value);
}

phy_status phy_complex_ball_tanh(const phy_complex_ball *argument,
                                 uint32_t rounds,
                                 phy_complex_ball *out_value)
{
    if (!compatible(argument, argument, out_value)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = complex_context(out_value);
    phy_complex_ball sine;
    phy_complex_ball cosine;
    phy_complex_ball quotient;
    memset(&sine, 0, sizeof sine);
    memset(&cosine, 0, sizeof cosine);
    memset(&quotient, 0, sizeof quotient);
    phy_status status = phy_complex_ball_init(context, &sine);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &cosine);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &quotient);
    if (status == PHY_OK) {
        status = phy_complex_ball_sinh(argument, rounds, &sine);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_cosh(argument, rounds, &cosine);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_divide(&sine, &cosine, &quotient);
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&quotient, out_value);
    phy_complex_ball_destroy(&quotient);
    phy_complex_ball_destroy(&cosine);
    phy_complex_ball_destroy(&sine);
    return status;
}

phy_status phy_complex_ball_asin(const phy_complex_ball *z, uint32_t r,
                                 phy_complex_ball *o)
{
    if (!compatible(z, z, o))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *c = complex_context(o);
    phy_complex_ball one, z2, inside, root, iz, sum, logged, result;
    phy_complex_ball *b[] = {&one, &z2,  &inside, &root,
                             &iz,  &sum, &logged, &result};
    for (size_t i = 0; i < 8u; i++)
        memset(b[i], 0, sizeof *b[i]);
    size_t n = 0u;
    phy_status s = PHY_OK;
    while (s == PHY_OK && n < 8u) {
        s = phy_complex_ball_init(c, b[n]);
        if (s == PHY_OK)
            n++;
    }
    if (s == PHY_OK)
        s = phy_complex_ball_set_i64(&one, 1, 1, 0, 1);
    if (s == PHY_OK)
        s = phy_complex_ball_multiply(z, z, &z2);
    if (s == PHY_OK)
        s = phy_complex_ball_subtract(&one, &z2, &inside);
    if (s == PHY_OK)
        s = phy_complex_ball_sqrt(&inside, r, &root);
    if (s == PHY_OK)
        s = multiply_i(z, false, &iz);
    if (s == PHY_OK)
        s = phy_complex_ball_add(&iz, &root, &sum);
    if (s == PHY_OK)
        s = phy_complex_ball_log(&sum, r, &logged);
    if (s == PHY_OK)
        s = multiply_i(&logged, true, &result);
    if (s == PHY_OK)
        s = phy_complex_ball_copy(&result, o);
    while (n != 0u)
        phy_complex_ball_destroy(b[--n]);
    return s;
}

phy_status phy_complex_ball_acos(const phy_complex_ball *z, uint32_t r,
                                 phy_complex_ball *o)
{
    if (!compatible(z, z, o))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *c = complex_context(o);
    phy_complex_ball halfpi, asinv, result;
    memset(&halfpi, 0, sizeof halfpi);
    memset(&asinv, 0, sizeof asinv);
    memset(&result, 0, sizeof result);
    phy_status s = phy_complex_ball_init(c, &halfpi);
    if (s == PHY_OK)
        s = phy_complex_ball_init(c, &asinv);
    if (s == PHY_OK)
        s = phy_complex_ball_init(c, &result);
    if (s == PHY_OK)
        s = real_pi(&halfpi.real);
    if (s == PHY_OK)
        s = real_scale(&halfpi.real, 1, 2, &halfpi.real);
    if (s == PHY_OK)
        s = phy_complex_ball_asin(z, r, &asinv);
    if (s == PHY_OK)
        s = phy_complex_ball_subtract(&halfpi, &asinv, &result);
    if (s == PHY_OK)
        s = phy_complex_ball_copy(&result, o);
    phy_complex_ball_destroy(&result);
    phy_complex_ball_destroy(&asinv);
    phy_complex_ball_destroy(&halfpi);
    return s;
}

phy_status phy_complex_ball_atan(const phy_complex_ball *z, uint32_t r,
                                 phy_complex_ball *o)
{
    if (!compatible(z, z, o))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *c = complex_context(o);
    phy_complex_ball one, iz, plus, minus, lp, lm, diff, result;
    phy_complex_ball *b[] = {&one, &iz, &plus, &minus,
                             &lp,  &lm, &diff, &result};
    for (size_t i = 0; i < 8u; i++)
        memset(b[i], 0, sizeof *b[i]);
    size_t n = 0u;
    phy_status s = PHY_OK;
    while (s == PHY_OK && n < 8u) {
        s = phy_complex_ball_init(c, b[n]);
        if (s == PHY_OK)
            n++;
    }
    if (s == PHY_OK)
        s = phy_complex_ball_set_i64(&one, 1, 1, 0, 1);
    if (s == PHY_OK)
        s = multiply_i(z, false, &iz);
    if (s == PHY_OK)
        s = phy_complex_ball_add(&one, &iz, &plus);
    if (s == PHY_OK)
        s = phy_complex_ball_subtract(&one, &iz, &minus);
    if (s == PHY_OK)
        s = phy_complex_ball_log(&plus, r, &lp);
    if (s == PHY_OK)
        s = phy_complex_ball_log(&minus, r, &lm);
    if (s == PHY_OK)
        s = phy_complex_ball_subtract(&lp, &lm, &diff);
    if (s == PHY_OK)
        s = multiply_i(&diff, true, &result);
    if (s == PHY_OK)
        s = complex_scale(&result, 1, 2, &result);
    if (s == PHY_OK)
        s = phy_complex_ball_copy(&result, o);
    while (n != 0u)
        phy_complex_ball_destroy(b[--n]);
    return s;
}

phy_status phy_complex_ball_asinh(const phy_complex_ball *z, uint32_t r,
                                  phy_complex_ball *o)
{
    if (!compatible(z, z, o))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *c = complex_context(o);
    phy_complex_ball one, z2, inside, root, sum, result;
    phy_complex_ball *b[] = {&one, &z2, &inside, &root, &sum, &result};
    for (size_t i = 0; i < 6u; i++)
        memset(b[i], 0, sizeof *b[i]);
    size_t n = 0u;
    phy_status s = PHY_OK;
    while (s == PHY_OK && n < 6u) {
        s = phy_complex_ball_init(c, b[n]);
        if (s == PHY_OK)
            n++;
    }
    if (s == PHY_OK)
        s = phy_complex_ball_set_i64(&one, 1, 1, 0, 1);
    if (s == PHY_OK)
        s = phy_complex_ball_multiply(z, z, &z2);
    if (s == PHY_OK)
        s = phy_complex_ball_add(&z2, &one, &inside);
    if (s == PHY_OK)
        s = phy_complex_ball_sqrt(&inside, r, &root);
    if (s == PHY_OK)
        s = phy_complex_ball_add(z, &root, &sum);
    if (s == PHY_OK)
        s = phy_complex_ball_log(&sum, r, &result);
    if (s == PHY_OK)
        s = phy_complex_ball_copy(&result, o);
    while (n != 0u)
        phy_complex_ball_destroy(b[--n]);
    return s;
}

phy_status phy_complex_ball_acosh(const phy_complex_ball *z, uint32_t r,
                                  phy_complex_ball *o)
{
    if (!compatible(z, z, o))
        return PHY_ERR_INVALID_ARGUMENT;
    int imaginary_lower = 0;
    int imaginary_upper = 0;
    bool imaginary_zero = false;
    phy_status s = real_signs(
        &z->imaginary, &imaginary_lower, &imaginary_upper,
        &imaginary_zero);
    bool within_unit = false;
    if (s == PHY_OK && imaginary_zero) {
        s = real_within_unit(&z->real, &within_unit);
    }
    if (s == PHY_OK && imaginary_zero && within_unit) {
        phy_complex_ball temporary;
        memset(&temporary, 0, sizeof temporary);
        s = phy_complex_ball_init(complex_context(o), &temporary);
        if (s == PHY_OK) {
            s = phy_real_ball_acos(&z->real, r, &temporary.imaginary);
        }
        if (s == PHY_OK) s = publish(&temporary, o);
        phy_complex_ball_destroy(&temporary);
        return s;
    }
    if (s != PHY_OK) return s;
    phy_exact_context *c = complex_context(o);
    phy_complex_ball one, minus, plus, rm, rp, product, sum, result;
    phy_complex_ball *b[] = {&one, &minus,   &plus, &rm,
                             &rp,  &product, &sum,  &result};
    for (size_t i = 0; i < 8u; i++)
        memset(b[i], 0, sizeof *b[i]);
    size_t n = 0u;
    s = PHY_OK;
    while (s == PHY_OK && n < 8u) {
        s = phy_complex_ball_init(c, b[n]);
        if (s == PHY_OK)
            n++;
    }
    if (s == PHY_OK)
        s = phy_complex_ball_set_i64(&one, 1, 1, 0, 1);
    if (s == PHY_OK)
        s = phy_complex_ball_subtract(z, &one, &minus);
    if (s == PHY_OK)
        s = phy_complex_ball_add(z, &one, &plus);
    if (s == PHY_OK)
        s = phy_complex_ball_sqrt(&minus, r, &rm);
    if (s == PHY_OK)
        s = phy_complex_ball_sqrt(&plus, r, &rp);
    if (s == PHY_OK)
        s = phy_complex_ball_multiply(&rm, &rp, &product);
    if (s == PHY_OK)
        s = phy_complex_ball_add(z, &product, &sum);
    if (s == PHY_OK)
        s = phy_complex_ball_log(&sum, r, &result);
    if (s == PHY_OK)
        s = phy_complex_ball_copy(&result, o);
    while (n != 0u)
        phy_complex_ball_destroy(b[--n]);
    return s;
}

phy_status phy_complex_ball_atanh(const phy_complex_ball *z, uint32_t r,
                                  phy_complex_ball *o)
{
    if (!compatible(z, z, o))
        return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *c = complex_context(o);
    phy_complex_ball one, plus, minus, lp, lm, diff, result;
    phy_complex_ball *b[] = {&one, &plus, &minus, &lp, &lm, &diff, &result};
    for (size_t i = 0; i < 7u; i++)
        memset(b[i], 0, sizeof *b[i]);
    size_t n = 0u;
    phy_status s = PHY_OK;
    while (s == PHY_OK && n < 7u) {
        s = phy_complex_ball_init(c, b[n]);
        if (s == PHY_OK)
            n++;
    }
    if (s == PHY_OK)
        s = phy_complex_ball_set_i64(&one, 1, 1, 0, 1);
    if (s == PHY_OK)
        s = phy_complex_ball_add(&one, z, &plus);
    if (s == PHY_OK)
        s = phy_complex_ball_subtract(&one, z, &minus);
    if (s == PHY_OK)
        s = phy_complex_ball_log(&plus, r, &lp);
    if (s == PHY_OK)
        s = phy_complex_ball_log(&minus, r, &lm);
    if (s == PHY_OK)
        s = phy_complex_ball_subtract(&lp, &lm, &diff);
    if (s == PHY_OK)
        s = complex_scale(&diff, 1, 2, &result);
    if (s == PHY_OK)
        s = phy_complex_ball_copy(&result, o);
    while (n != 0u)
        phy_complex_ball_destroy(b[--n]);
    return s;
}
