/*
 * Certified complex special functions over rectangular rational balls.
 *
 * The implementation deliberately avoids host floating point.  Erf uses its
 * globally convergent Taylor series and a geometric tail bound.  Gamma,
 * LogGamma and Digamma translate the argument into the right half-plane and
 * use the Stirling series through B_22.  The B_24 remainder is bounded with
 * the right-half-plane specialization of the Olver/Arb bound
 *
 *   |R_n(z)| <= 2 |B_2n| b^(2n) /
 *                 (2n (2n-1) |z|^(2n-1)),
 *
 * where b = sec(arg(z)/2).  Re(z) > 0 implies b <= sqrt(2) and
 * |z| >= Re(z), giving the exact rational majorants used below.  The
 * derivative bound gives the Digamma remainder.
 */
#include "phy/ball.h"

#include <limits.h>
#include <string.h>

#define SPECIAL_MAX_ROUNDS 256u
#define SPECIAL_GUARD_BITS 16u
#define SPECIAL_MAX_ERF_TERMS 2048u
#define SPECIAL_MAX_SHIFT 512u
#define STIRLING_ORDER 12u

static phy_exact_context *special_context(const phy_complex_ball *value)
{
    return phy_bigrat_numerator(&value->real.midpoint)->context;
}

static void destroy_bigrat(phy_bigrat *value)
{
    if (phy_bigint_is_initialized(phy_bigrat_numerator(value))) {
        phy_bigrat_destroy(value);
    }
}

static phy_status dyadic_bound(const phy_bigrat *value, uint32_t bits,
                               bool upper_bound, phy_bigrat *out)
{
    phy_exact_context *context = phy_bigrat_numerator(value)->context;
    phy_bigint two = {0};
    phy_bigint scale = {0};
    phy_bigint scaled_numerator = {0};
    phy_bigint quotient = {0};
    phy_bigint remainder = {0};
    phy_bigint one = {0};
    phy_bigint *integers[] = {&two,      &scale,     &scaled_numerator,
                              &quotient, &remainder, &one};
    size_t initialized = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized < sizeof integers / sizeof integers[0]) {
        status = phy_bigint_init(context, integers[initialized]);
        if (status == PHY_OK) initialized++;
    }
    if (status == PHY_OK) status = phy_bigint_set_i64(&two, 2);
    if (status == PHY_OK) status = phy_bigint_pow_u32(&two, bits, &scale);
    if (status == PHY_OK) {
        status = phy_bigint_multiply(phy_bigrat_numerator(value), &scale,
                                     &scaled_numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divmod(&scaled_numerator,
                                   phy_bigrat_denominator(value), &quotient,
                                   &remainder);
    }
    if (status == PHY_OK) status = phy_bigint_set_i64(&one, 1);
    const int sign = phy_bigrat_sign(value);
    if (status == PHY_OK && phy_bigint_sign(&remainder) != 0 &&
        ((upper_bound && sign > 0) || (!upper_bound && sign < 0))) {
        status = upper_bound ? phy_bigint_add(&quotient, &one, &quotient)
                             : phy_bigint_subtract(&quotient, &one, &quotient);
    }
    phy_bigrat scale_rat = {0};
    if (status == PHY_OK) status = phy_bigrat_init(context, &scale_rat);
    if (status == PHY_OK) status = phy_bigrat_set_bigint(&quotient, out);
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(&scale, &scale_rat);
    }
    if (status == PHY_OK) status = phy_bigrat_divide(out, &scale_rat, out);
    destroy_bigrat(&scale_rat);
    while (initialized != 0u) {
        phy_bigint_destroy(integers[--initialized]);
    }
    return status;
}

static phy_status round_real(const phy_real_ball *value, uint32_t bits,
                             phy_real_ball *out)
{
    phy_exact_context *context =
        phy_bigrat_numerator(&out->midpoint)->context;
    phy_bigrat lower = {0};
    phy_bigrat upper = {0};
    phy_bigrat rounded_lower = {0};
    phy_bigrat rounded_upper = {0};
    phy_bigrat *values[] = {&lower, &upper, &rounded_lower, &rounded_upper};
    size_t initialized = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && initialized < 4u) {
        status = phy_bigrat_init(context, values[initialized]);
        if (status == PHY_OK) initialized++;
    }
    if (status == PHY_OK) status = phy_real_ball_lower(value, &lower);
    if (status == PHY_OK) status = phy_real_ball_upper(value, &upper);
    if (status == PHY_OK) {
        status = dyadic_bound(&lower, bits, false, &rounded_lower);
    }
    if (status == PHY_OK) {
        status = dyadic_bound(&upper, bits, true, &rounded_upper);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_interval(out, &rounded_lower,
                                            &rounded_upper);
    }
    while (initialized != 0u) phy_bigrat_destroy(values[--initialized]);
    return status;
}

static phy_status round_complex(const phy_complex_ball *value, uint32_t bits,
                                phy_complex_ball *out)
{
    phy_complex_ball temporary = {0};
    phy_status status = phy_complex_ball_init(special_context(out),
                                              &temporary);
    if (status == PHY_OK) {
        status = round_real(&value->real, bits, &temporary.real);
    }
    if (status == PHY_OK) {
        status = round_real(&value->imaginary, bits, &temporary.imaginary);
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&temporary, out);
    phy_complex_ball_destroy(&temporary);
    return status;
}

static phy_status dyadic_epsilon(phy_exact_context *context, uint32_t bits,
                                 phy_bigrat *out)
{
    phy_bigint two = {0};
    phy_bigint scale = {0};
    phy_bigrat scale_rat = {0};
    phy_status status = phy_bigint_init(context, &two);
    if (status == PHY_OK) status = phy_bigint_init(context, &scale);
    if (status == PHY_OK) status = phy_bigrat_init(context, &scale_rat);
    if (status == PHY_OK) status = phy_bigint_set_i64(&two, 2);
    if (status == PHY_OK) status = phy_bigint_pow_u32(&two, bits, &scale);
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(&scale, &scale_rat);
    }
    if (status == PHY_OK) status = phy_bigrat_set_i64(out, 1, 1);
    if (status == PHY_OK) status = phy_bigrat_divide(out, &scale_rat, out);
    destroy_bigrat(&scale_rat);
    if (phy_bigint_is_initialized(&scale)) phy_bigint_destroy(&scale);
    if (phy_bigint_is_initialized(&two)) phy_bigint_destroy(&two);
    return status;
}

static phy_status abs_copy(const phy_bigrat *value, phy_bigrat *out)
{
    return phy_bigrat_sign(value) < 0 ? phy_bigrat_negate(value, out)
                                      : phy_bigrat_copy(value, out);
}

static phy_status real_abs_upper(const phy_real_ball *value,
                                 phy_bigrat *out)
{
    phy_exact_context *context = phy_bigrat_numerator(out)->context;
    phy_bigrat lower = {0};
    phy_bigrat upper = {0};
    phy_bigrat abs_lower = {0};
    phy_bigrat abs_upper = {0};
    phy_bigrat *values[] = {&lower, &upper, &abs_lower, &abs_upper};
    size_t initialized = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && initialized < 4u) {
        status = phy_bigrat_init(context, values[initialized]);
        if (status == PHY_OK) initialized++;
    }
    if (status == PHY_OK) status = phy_real_ball_lower(value, &lower);
    if (status == PHY_OK) status = phy_real_ball_upper(value, &upper);
    if (status == PHY_OK) status = abs_copy(&lower, &abs_lower);
    if (status == PHY_OK) status = abs_copy(&upper, &abs_upper);
    int order = 0;
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&abs_lower, &abs_upper, &order);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(order >= 0 ? &abs_lower : &abs_upper, out);
    }
    while (initialized != 0u) phy_bigrat_destroy(values[--initialized]);
    return status;
}

static phy_status complex_l1_upper(const phy_complex_ball *value,
                                   phy_bigrat *out)
{
    phy_exact_context *context = phy_bigrat_numerator(out)->context;
    phy_bigrat real = {0};
    phy_bigrat imaginary = {0};
    phy_status status = phy_bigrat_init(context, &real);
    if (status == PHY_OK) status = phy_bigrat_init(context, &imaginary);
    if (status == PHY_OK) status = real_abs_upper(&value->real, &real);
    if (status == PHY_OK) {
        status = real_abs_upper(&value->imaginary, &imaginary);
    }
    if (status == PHY_OK) status = phy_bigrat_add(&real, &imaginary, out);
    destroy_bigrat(&imaginary);
    destroy_bigrat(&real);
    return status;
}

static phy_status norm_squared_upper(const phy_complex_ball *value,
                                     phy_bigrat *out)
{
    phy_exact_context *context = phy_bigrat_numerator(out)->context;
    phy_real_ball real_square = {0};
    phy_real_ball imaginary_square = {0};
    phy_real_ball sum = {0};
    phy_status status = phy_real_ball_init(context, &real_square);
    if (status == PHY_OK) {
        status = phy_real_ball_init(context, &imaginary_square);
    }
    if (status == PHY_OK) status = phy_real_ball_init(context, &sum);
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(&value->real, &value->real,
                                        &real_square);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(&value->imaginary,
                                        &value->imaginary,
                                        &imaginary_square);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_add(&real_square, &imaginary_square, &sum);
    }
    if (status == PHY_OK) status = phy_real_ball_upper(&sum, out);
    phy_real_ball_destroy(&sum);
    phy_real_ball_destroy(&imaginary_square);
    phy_real_ball_destroy(&real_square);
    return status;
}

static phy_status inflate_complex(const phy_complex_ball *value,
                                  const phy_bigrat *error,
                                  phy_complex_ball *out)
{
    if (phy_bigrat_sign(error) < 0) return PHY_ERR_INVALID_ARGUMENT;
    phy_exact_context *context = special_context(out);
    phy_complex_ball temporary = {0};
    phy_bigrat lower = {0};
    phy_bigrat upper = {0};
    phy_bigrat expanded_lower = {0};
    phy_bigrat expanded_upper = {0};
    phy_bigrat *values[] = {&lower, &upper, &expanded_lower, &expanded_upper};
    size_t initialized = 0u;
    phy_status status = phy_complex_ball_init(context, &temporary);
    while (status == PHY_OK && initialized < 4u) {
        status = phy_bigrat_init(context, values[initialized]);
        if (status == PHY_OK) initialized++;
    }
    const phy_real_ball *components[] = {&value->real, &value->imaginary};
    phy_real_ball *outputs[] = {&temporary.real, &temporary.imaginary};
    for (size_t component = 0u; status == PHY_OK && component < 2u;
         ++component) {
        status = phy_real_ball_lower(components[component], &lower);
        if (status == PHY_OK) {
            status = phy_real_ball_upper(components[component], &upper);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_subtract(&lower, error, &expanded_lower);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_add(&upper, error, &expanded_upper);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_set_interval(outputs[component],
                                                &expanded_lower,
                                                &expanded_upper);
        }
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&temporary, out);
    while (initialized != 0u) phy_bigrat_destroy(values[--initialized]);
    phy_complex_ball_destroy(&temporary);
    return status;
}

static phy_status complex_scale_i64(const phy_complex_ball *value,
                                    int64_t numerator, int64_t denominator,
                                    phy_complex_ball *out)
{
    phy_complex_ball scalar = {0};
    phy_status status = phy_complex_ball_init(special_context(out), &scalar);
    if (status == PHY_OK) {
        status = phy_complex_ball_set_i64(&scalar, numerator, denominator,
                                          0, 1);
    }
    if (status == PHY_OK) status = phy_complex_ball_multiply(value, &scalar,
                                                             out);
    phy_complex_ball_destroy(&scalar);
    return status;
}

static phy_status add_integer(const phy_complex_ball *value, int64_t integer,
                              phy_complex_ball *out)
{
    phy_complex_ball scalar = {0};
    phy_status status = phy_complex_ball_init(special_context(out), &scalar);
    if (status == PHY_OK) {
        status = phy_complex_ball_set_i64(&scalar, integer, 1, 0, 1);
    }
    if (status == PHY_OK) status = phy_complex_ball_add(value, &scalar, out);
    phy_complex_ball_destroy(&scalar);
    return status;
}

static phy_status real_pi(phy_real_ball *out)
{
    static const char scale[] =
        "10000000000000000000000000000000000000000";
    phy_exact_context *context =
        phy_bigrat_numerator(&out->midpoint)->context;
    phy_bigrat lower = {0};
    phy_bigrat upper = {0};
    phy_status status = phy_bigrat_init(context, &lower);
    if (status == PHY_OK) status = phy_bigrat_init(context, &upper);
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
    destroy_bigrat(&upper);
    destroy_bigrat(&lower);
    return status;
}

static phy_status validate_request(const phy_complex_ball *argument,
                                   uint32_t rounds,
                                   const phy_complex_ball *out)
{
    if (argument == NULL || out == NULL || rounds < 8u ||
        rounds > SPECIAL_MAX_ROUNDS) {
        return rounds > SPECIAL_MAX_ROUNDS ? PHY_ERR_TERM_LIMIT
                                           : PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_complex_ball_validate(argument);
    if (status == PHY_OK) status = phy_complex_ball_validate(out);
    if (status == PHY_OK && special_context(argument) != special_context(out)) {
        status = PHY_ERR_INVALID_ARGUMENT;
    }
    return status;
}

phy_status phy_complex_ball_erf(const phy_complex_ball *argument,
                                uint32_t rounds, phy_complex_ball *out_value)
{
    phy_status status = validate_request(argument, rounds, out_value);
    if (status != PHY_OK) return status;
    phy_exact_context *context = special_context(out_value);
    const uint32_t precision_bits = rounds + SPECIAL_GUARD_BITS;
    const uint32_t max_terms =
        rounds > (SPECIAL_MAX_ERF_TERMS - 128u) / 12u
            ? SPECIAL_MAX_ERF_TERMS
            : rounds * 12u + 128u;
    phy_complex_ball z_squared = {0};
    phy_complex_ball term = {0};
    phy_complex_ball sum = {0};
    phy_complex_ball next = {0};
    phy_complex_ball scaled = {0};
    phy_complex_ball scalar = {0};
    phy_complex_ball inflated = {0};
    phy_complex_ball *complex_values[] = {
        &z_squared, &term, &sum, &next, &scaled, &scalar, &inflated};
    size_t complex_count = 0u;
    while (status == PHY_OK && complex_count < 7u) {
        status = phy_complex_ball_init(context,
                                       complex_values[complex_count]);
        if (status == PHY_OK) complex_count++;
    }
    phy_bigrat norm2 = {0};
    phy_bigrat tail = {0};
    phy_bigrat epsilon = {0};
    phy_bigrat ratio_left = {0};
    phy_bigrat ratio_right = {0};
    phy_bigrat *rationals[] = {
        &norm2, &tail, &epsilon, &ratio_left, &ratio_right};
    size_t rational_count = 0u;
    while (status == PHY_OK && rational_count < 5u) {
        status = phy_bigrat_init(context, rationals[rational_count]);
        if (status == PHY_OK) rational_count++;
    }
    if (status == PHY_OK) status = norm_squared_upper(argument, &norm2);
    if (status == PHY_OK) status = dyadic_epsilon(context, rounds, &epsilon);
    if (status == PHY_OK) {
        status = phy_complex_ball_multiply(argument, argument, &z_squared);
    }
    if (status == PHY_OK) {
        status = round_complex(&z_squared, precision_bits, &z_squared);
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(argument, &term);
    if (status == PHY_OK) status = phy_complex_ball_copy(argument, &sum);

    bool certified = false;
    for (uint32_t n = 0u; status == PHY_OK && n < max_terms; ++n) {
        status = phy_complex_ball_multiply(&term, &z_squared, &next);
        const uint64_t denominator =
            (uint64_t)(n + 1u) * (uint64_t)(2u * n + 3u);
        if (status == PHY_OK && denominator > INT64_MAX) {
            status = PHY_ERR_TERM_LIMIT;
        }
        if (status == PHY_OK) {
            status = complex_scale_i64(
                &next, -(int64_t)(2u * n + 1u), (int64_t)denominator,
                &next);
        }
        if (status == PHY_OK) {
            status = round_complex(&next, precision_bits, &term);
        }
        if (status == PHY_OK) status = phy_complex_ball_add(&sum, &term, &sum);
        if (status == PHY_OK) {
            status = round_complex(&sum, precision_bits, &sum);
        }
        if (status == PHY_OK) status = complex_l1_upper(&term, &tail);
        if (status == PHY_OK) status = phy_bigrat_add(&tail, &tail, &tail);
        if (status == PHY_OK) {
            status = phy_bigrat_add(&norm2, &norm2, &ratio_left);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_set_i64(&ratio_right, (int64_t)n + 2, 1);
        }
        int ratio_order = 1;
        int tail_order = 1;
        if (status == PHY_OK) {
            status = phy_bigrat_compare(&ratio_left, &ratio_right,
                                        &ratio_order);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_compare(&tail, &epsilon, &tail_order);
        }
        if (status == PHY_OK && ratio_order <= 0 && tail_order <= 0) {
            certified = true;
            break;
        }
    }
    if (status == PHY_OK && !certified) status = PHY_ERR_TERM_LIMIT;
    if (status == PHY_OK) status = inflate_complex(&sum, &tail, &inflated);

    phy_real_ball pi = {0};
    phy_real_ball sqrt_pi = {0};
    phy_real_ball two = {0};
    phy_real_ball factor = {0};
    if (status == PHY_OK) status = phy_real_ball_init(context, &pi);
    if (status == PHY_OK) status = phy_real_ball_init(context, &sqrt_pi);
    if (status == PHY_OK) status = phy_real_ball_init(context, &two);
    if (status == PHY_OK) status = phy_real_ball_init(context, &factor);
    if (status == PHY_OK) status = real_pi(&pi);
    if (status == PHY_OK) {
        status = phy_real_ball_sqrt(&pi, rounds, &sqrt_pi);
    }
    if (status == PHY_OK) status = phy_real_ball_set_i64(&two, 2, 1);
    if (status == PHY_OK) {
        status = phy_real_ball_divide(&two, &sqrt_pi, &factor);
    }
    if (status == PHY_OK) status = phy_complex_ball_set_real(&scalar, &factor);
    if (status == PHY_OK) {
        status = phy_complex_ball_multiply(&inflated, &scalar, &scaled);
    }
    if (status == PHY_OK) {
        status = round_complex(&scaled, precision_bits, &scaled);
    }
    if (status == PHY_OK &&
        phy_bigrat_sign(&argument->imaginary.midpoint) == 0 &&
        phy_bigrat_sign(&argument->imaginary.radius) == 0) {
        status = phy_real_ball_set_i64(&scaled.imaginary, 0, 1);
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&scaled, out_value);
    phy_real_ball_destroy(&factor);
    phy_real_ball_destroy(&two);
    phy_real_ball_destroy(&sqrt_pi);
    phy_real_ball_destroy(&pi);
    while (rational_count != 0u) {
        phy_bigrat_destroy(rationals[--rational_count]);
    }
    while (complex_count != 0u) {
        phy_complex_ball_destroy(complex_values[--complex_count]);
    }
    return status;
}

phy_status phy_complex_ball_erfc(const phy_complex_ball *argument,
                                 uint32_t rounds,
                                 phy_complex_ball *out_value)
{
    phy_status status = validate_request(argument, rounds, out_value);
    if (status != PHY_OK) return status;
    phy_complex_ball one = {0};
    phy_complex_ball erf_value = {0};
    phy_complex_ball result = {0};
    phy_exact_context *context = special_context(out_value);
    status = phy_complex_ball_init(context, &one);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &erf_value);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &result);
    if (status == PHY_OK) {
        status = phy_complex_ball_set_i64(&one, 1, 1, 0, 1);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_erf(argument, rounds, &erf_value);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_subtract(&one, &erf_value, &result);
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&result, out_value);
    phy_complex_ball_destroy(&result);
    phy_complex_ball_destroy(&erf_value);
    phy_complex_ball_destroy(&one);
    return status;
}

static const int64_t kBernoulliNumerator[STIRLING_ORDER] = {
    0, 1, -1, 1, -1, 5, -691, 7, -3617, 43867, -174611, 854513};
static const int64_t kBernoulliDenominator[STIRLING_ORDER] = {
    1, 6, 30, 42, 30, 66, 2730, 6, 510, 798, 330, 138};

static phy_status stirling_remainder(const phy_bigrat *real_lower,
                                     bool derivative, phy_bigrat *out)
{
    phy_exact_context *context = phy_bigrat_numerator(out)->context;
    if (phy_bigrat_sign(real_lower) <= 0) return PHY_ERR_DOMAIN;
    phy_bigrat bernoulli = {0};
    phy_bigrat factor = {0};
    phy_bigrat power = {0};
    phy_status status = phy_bigrat_init(context, &bernoulli);
    if (status == PHY_OK) status = phy_bigrat_init(context, &factor);
    if (status == PHY_OK) status = phy_bigrat_init(context, &power);
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&bernoulli, 236364091, 2730);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(
            &factor, 8192, derivative ? 12 : (24 * 23));
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&bernoulli, &factor, out);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_pow_i32(real_lower, derivative ? 24 : 23,
                                    &power);
    }
    if (status == PHY_OK) status = phy_bigrat_divide(out, &power, out);
    destroy_bigrat(&power);
    destroy_bigrat(&factor);
    destroy_bigrat(&bernoulli);
    return status;
}

static phy_status choose_shift(const phy_complex_ball *argument,
                               uint32_t rounds, bool derivative,
                               uint32_t *out_shift,
                               phy_bigrat *out_remainder)
{
    phy_exact_context *context = special_context(argument);
    phy_bigrat base_lower = {0};
    phy_bigrat candidate = {0};
    phy_bigrat shift_value = {0};
    phy_bigrat target = {0};
    phy_bigrat epsilon = {0};
    phy_status status = phy_bigrat_init(context, &base_lower);
    if (status == PHY_OK) status = phy_bigrat_init(context, &candidate);
    if (status == PHY_OK) status = phy_bigrat_init(context, &shift_value);
    if (status == PHY_OK) status = phy_bigrat_init(context, &target);
    if (status == PHY_OK) status = phy_bigrat_init(context, &epsilon);
    if (status == PHY_OK) {
        status = phy_real_ball_lower(&argument->real, &base_lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(
            &target, (int64_t)(rounds / 2u + SPECIAL_GUARD_BITS / 2u), 1);
    }
    if (status == PHY_OK) status = dyadic_epsilon(context, rounds, &epsilon);
    uint32_t shift = 0u;
    int target_order = -1;
    int error_order = 1;
    for (; status == PHY_OK && shift <= SPECIAL_MAX_SHIFT; ++shift) {
        status = phy_bigrat_set_i64(&shift_value, (int64_t)shift, 1);
        if (status == PHY_OK) {
            status = phy_bigrat_add(&base_lower, &shift_value, &candidate);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_compare(&candidate, &target, &target_order);
        }
        if (status == PHY_OK && target_order >= 0) {
            status = stirling_remainder(&candidate, derivative,
                                        out_remainder);
            if (status == PHY_OK) {
                status = phy_bigrat_compare(out_remainder, &epsilon,
                                            &error_order);
            }
            if (status == PHY_OK && error_order <= 0) break;
        }
    }
    if (status == PHY_OK && shift > SPECIAL_MAX_SHIFT) {
        status = PHY_ERR_TERM_LIMIT;
    }
    if (status == PHY_OK) *out_shift = shift;
    destroy_bigrat(&epsilon);
    destroy_bigrat(&target);
    destroy_bigrat(&shift_value);
    destroy_bigrat(&candidate);
    destroy_bigrat(&base_lower);
    return status;
}

static phy_status half_log_two_pi(phy_exact_context *context,
                                  uint32_t rounds,
                                  phy_complex_ball *out)
{
    phy_real_ball pi = {0};
    phy_real_ball two = {0};
    phy_real_ball two_pi = {0};
    phy_real_ball logarithm = {0};
    phy_real_ball half = {0};
    phy_status status = phy_real_ball_init(context, &pi);
    if (status == PHY_OK) status = phy_real_ball_init(context, &two);
    if (status == PHY_OK) status = phy_real_ball_init(context, &two_pi);
    if (status == PHY_OK) status = phy_real_ball_init(context, &logarithm);
    if (status == PHY_OK) status = phy_real_ball_init(context, &half);
    if (status == PHY_OK) status = real_pi(&pi);
    if (status == PHY_OK) status = phy_real_ball_set_i64(&two, 2, 1);
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(&two, &pi, &two_pi);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_log(&two_pi, rounds, &logarithm);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_i64(&two, 1, 2);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_multiply(&logarithm, &two, &half);
    }
    if (status == PHY_OK) status = phy_complex_ball_set_real(out, &half);
    phy_real_ball_destroy(&half);
    phy_real_ball_destroy(&logarithm);
    phy_real_ball_destroy(&two_pi);
    phy_real_ball_destroy(&two);
    phy_real_ball_destroy(&pi);
    return status;
}

static phy_status stirling_loggamma(const phy_complex_ball *argument,
                                    uint32_t rounds,
                                    const phy_bigrat *remainder,
                                    phy_complex_ball *out)
{
    phy_exact_context *context = special_context(out);
    const uint32_t bits = rounds + SPECIAL_GUARD_BITS;
    phy_complex_ball half = {0};
    phy_complex_ball shifted = {0};
    phy_complex_ball logarithm = {0};
    phy_complex_ball product = {0};
    phy_complex_ball sum = {0};
    phy_complex_ball constant = {0};
    phy_complex_ball power = {0};
    phy_complex_ball term = {0};
    phy_complex_ball scalar = {0};
    phy_complex_ball inflated = {0};
    phy_complex_ball *values[] = {&half, &shifted, &logarithm, &product,
                                  &sum,  &constant, &power,     &term,
                                  &scalar, &inflated};
    size_t initialized = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && initialized < 10u) {
        status = phy_complex_ball_init(context, values[initialized]);
        if (status == PHY_OK) initialized++;
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_set_i64(&half, 1, 2, 0, 1);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_subtract(argument, &half, &shifted);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_log(argument, rounds, &logarithm);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_multiply(&shifted, &logarithm, &product);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_subtract(&product, argument, &sum);
    }
    if (status == PHY_OK) status = half_log_two_pi(context, rounds, &constant);
    if (status == PHY_OK) status = phy_complex_ball_add(&sum, &constant, &sum);
    for (uint32_t k = 1u; status == PHY_OK && k < STIRLING_ORDER; ++k) {
        status = phy_complex_ball_pow_i32(argument, -(int32_t)(2u * k - 1u),
                                          &power);
        const int64_t denominator =
            kBernoulliDenominator[k] * (int64_t)(2u * k) *
            (int64_t)(2u * k - 1u);
        if (status == PHY_OK) {
            status = phy_complex_ball_set_i64(
                &scalar, kBernoulliNumerator[k], denominator, 0, 1);
        }
        if (status == PHY_OK) {
            status = phy_complex_ball_multiply(&power, &scalar, &term);
        }
        if (status == PHY_OK) status = phy_complex_ball_add(&sum, &term, &sum);
        if (status == PHY_OK) status = round_complex(&sum, bits, &sum);
    }
    if (status == PHY_OK) {
        status = inflate_complex(&sum, remainder, &inflated);
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&inflated, out);
    while (initialized != 0u) {
        phy_complex_ball_destroy(values[--initialized]);
    }
    return status;
}

static phy_status stirling_digamma(const phy_complex_ball *argument,
                                   uint32_t rounds,
                                   const phy_bigrat *remainder,
                                   phy_complex_ball *out)
{
    phy_exact_context *context = special_context(out);
    const uint32_t bits = rounds + SPECIAL_GUARD_BITS;
    phy_complex_ball sum = {0};
    phy_complex_ball power = {0};
    phy_complex_ball scalar = {0};
    phy_complex_ball term = {0};
    phy_complex_ball inflated = {0};
    phy_complex_ball *values[] = {&sum, &power, &scalar, &term, &inflated};
    size_t initialized = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && initialized < 5u) {
        status = phy_complex_ball_init(context, values[initialized]);
        if (status == PHY_OK) initialized++;
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_log(argument, rounds, &sum);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_pow_i32(argument, -1, &power);
    }
    if (status == PHY_OK) {
        status = complex_scale_i64(&power, -1, 2, &term);
    }
    if (status == PHY_OK) status = phy_complex_ball_add(&sum, &term, &sum);
    for (uint32_t k = 1u; status == PHY_OK && k < STIRLING_ORDER; ++k) {
        status = phy_complex_ball_pow_i32(argument, -(int32_t)(2u * k),
                                          &power);
        const int64_t denominator =
            kBernoulliDenominator[k] * (int64_t)(2u * k);
        if (status == PHY_OK) {
            status = phy_complex_ball_set_i64(
                &scalar, -kBernoulliNumerator[k], denominator, 0, 1);
        }
        if (status == PHY_OK) {
            status = phy_complex_ball_multiply(&power, &scalar, &term);
        }
        if (status == PHY_OK) status = phy_complex_ball_add(&sum, &term, &sum);
        if (status == PHY_OK) status = round_complex(&sum, bits, &sum);
    }
    if (status == PHY_OK) {
        status = inflate_complex(&sum, remainder, &inflated);
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&inflated, out);
    while (initialized != 0u) {
        phy_complex_ball_destroy(values[--initialized]);
    }
    return status;
}

static phy_status shifted_argument(const phy_complex_ball *argument,
                                   uint32_t shift,
                                   phy_complex_ball *out)
{
    return add_integer(argument, (int64_t)shift, out);
}

phy_status phy_complex_ball_loggamma(const phy_complex_ball *argument,
                                     uint32_t rounds,
                                     phy_complex_ball *out_value)
{
    phy_status status = validate_request(argument, rounds, out_value);
    if (status != PHY_OK) return status;
    phy_exact_context *context = special_context(out_value);
    phy_bigrat remainder = {0};
    status = phy_bigrat_init(context, &remainder);
    uint32_t shift = 0u;
    if (status == PHY_OK) {
        status = choose_shift(argument, rounds, false, &shift, &remainder);
    }
    phy_complex_ball shifted = {0};
    phy_complex_ball result = {0};
    phy_complex_ball factor = {0};
    phy_complex_ball logarithm = {0};
    if (status == PHY_OK) status = phy_complex_ball_init(context, &shifted);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &result);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &factor);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &logarithm);
    if (status == PHY_OK) status = shifted_argument(argument, shift, &shifted);
    if (status == PHY_OK) {
        status = stirling_loggamma(&shifted, rounds, &remainder, &result);
    }
    for (uint32_t k = 0u; status == PHY_OK && k < shift; ++k) {
        status = add_integer(argument, (int64_t)k, &factor);
        bool contains_zero = true;
        if (status == PHY_OK) {
            status = phy_complex_ball_contains_zero_checked(&factor,
                                                             &contains_zero);
        }
        if (status == PHY_OK && contains_zero) status = PHY_ERR_DOMAIN;
        if (status == PHY_OK) {
            status = phy_complex_ball_log(&factor, rounds, &logarithm);
        }
        if (status == PHY_OK) {
            status = phy_complex_ball_subtract(&result, &logarithm, &result);
        }
        if (status == PHY_OK) {
            status = round_complex(&result, rounds + SPECIAL_GUARD_BITS,
                                   &result);
        }
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&result, out_value);
    phy_complex_ball_destroy(&logarithm);
    phy_complex_ball_destroy(&factor);
    phy_complex_ball_destroy(&result);
    phy_complex_ball_destroy(&shifted);
    destroy_bigrat(&remainder);
    return status;
}

phy_status phy_complex_ball_gamma(const phy_complex_ball *argument,
                                  uint32_t rounds,
                                  phy_complex_ball *out_value)
{
    phy_status status = validate_request(argument, rounds, out_value);
    if (status != PHY_OK) return status;
    phy_exact_context *context = special_context(out_value);
    phy_complex_ball loggamma = {0};
    phy_complex_ball result = {0};
    status = phy_complex_ball_init(context, &loggamma);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &result);
    if (status == PHY_OK) {
        status = phy_complex_ball_loggamma(argument, rounds, &loggamma);
    }
    if (status == PHY_OK) {
        status = phy_complex_ball_exp(&loggamma, rounds, &result);
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&result, out_value);
    phy_complex_ball_destroy(&result);
    phy_complex_ball_destroy(&loggamma);
    return status;
}

phy_status phy_complex_ball_digamma(const phy_complex_ball *argument,
                                    uint32_t rounds,
                                    phy_complex_ball *out_value)
{
    phy_status status = validate_request(argument, rounds, out_value);
    if (status != PHY_OK) return status;
    phy_exact_context *context = special_context(out_value);
    phy_bigrat remainder = {0};
    status = phy_bigrat_init(context, &remainder);
    uint32_t shift = 0u;
    if (status == PHY_OK) {
        status = choose_shift(argument, rounds, true, &shift, &remainder);
    }
    phy_complex_ball shifted = {0};
    phy_complex_ball result = {0};
    phy_complex_ball factor = {0};
    phy_complex_ball reciprocal = {0};
    if (status == PHY_OK) status = phy_complex_ball_init(context, &shifted);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &result);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &factor);
    if (status == PHY_OK) status = phy_complex_ball_init(context, &reciprocal);
    if (status == PHY_OK) status = shifted_argument(argument, shift, &shifted);
    if (status == PHY_OK) {
        status = stirling_digamma(&shifted, rounds, &remainder, &result);
    }
    for (uint32_t k = 0u; status == PHY_OK && k < shift; ++k) {
        status = add_integer(argument, (int64_t)k, &factor);
        bool contains_zero = true;
        if (status == PHY_OK) {
            status = phy_complex_ball_contains_zero_checked(&factor,
                                                             &contains_zero);
        }
        if (status == PHY_OK && contains_zero) status = PHY_ERR_DOMAIN;
        if (status == PHY_OK) {
            status = phy_complex_ball_pow_i32(&factor, -1, &reciprocal);
        }
        if (status == PHY_OK) {
            status = phy_complex_ball_subtract(&result, &reciprocal, &result);
        }
        if (status == PHY_OK) {
            status = round_complex(&result, rounds + SPECIAL_GUARD_BITS,
                                   &result);
        }
    }
    if (status == PHY_OK) status = phy_complex_ball_copy(&result, out_value);
    phy_complex_ball_destroy(&reciprocal);
    phy_complex_ball_destroy(&factor);
    phy_complex_ball_destroy(&result);
    phy_complex_ball_destroy(&shifted);
    destroy_bigrat(&remainder);
    return status;
}
