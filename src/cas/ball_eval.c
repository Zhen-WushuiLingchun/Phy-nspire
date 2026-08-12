#include "cas_internal.h"

#include <string.h>

#include "phy/algebraic.h"
#include "phy/ball.h"
#include "../exact/complex_roots.h"
#include "sparse_poly.h"

#define BALL_MAX_DECIMAL_DIGITS 36u
#define BALL_DEFAULT_DECIMAL_DIGITS 16u
#define BALL_MAX_POLYNOMIAL_DEGREE 48u
#define BALL_EXACT_STEP_MULTIPLIER 64u
#define BALL_PRECISION_ATTEMPTS 3u

static const char kDecimalScale40[] =
    "10000000000000000000000000000000000000000";

static uint32_t decimal_digits_to_root_bits(unsigned decimal_digits)
{
    /* ceil(log2(10) * digits) plus four certificate guard bits. */
    return (uint32_t)((decimal_digits * 3322u + 999u) / 1000u + 4u);
}

static phy_exact_context *ball_exact_operation_context(phy_cas *cas)
{
    phy_exact_limits limits;
    phy_exact_limits_defaults(&limits);
    limits.max_limbs = UINT32_MAX;
    limits.max_steps =
        cas->limits.max_steps > UINT32_MAX / BALL_EXACT_STEP_MULTIPLIER
            ? UINT32_MAX
            : cas->limits.max_steps * BALL_EXACT_STEP_MULTIPLIER;
    limits.max_bytes = cas->limits.max_bytes;
    phy_exact_context *exact = phy_exact_context_create(&limits);
    if (exact != NULL) {
        phy_exact_set_cancel(exact, cas->cancelled, cas->cancel_user);
    }
    return exact;
}

static void destroy_bigrat(phy_bigrat *value)
{
    if (phy_bigint_is_initialized(phy_bigrat_numerator(value))) {
        phy_bigrat_destroy(value);
    }
}

static phy_status decimal_unit(phy_exact_context *exact, unsigned digits,
                               phy_bigrat *out)
{
    phy_bigint ten = {0};
    phy_bigint power = {0};
    phy_bigrat scale = {0};
    phy_status status = phy_bigint_init(exact, &ten);
    if (status == PHY_OK) status = phy_bigint_init(exact, &power);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &scale);
    if (status == PHY_OK) status = phy_bigint_set_i64(&ten, 10);
    if (status == PHY_OK) status = phy_bigint_pow_u32(&ten, digits, &power);
    if (status == PHY_OK) status = phy_bigrat_set_bigint(&power, &scale);
    if (status == PHY_OK) status = phy_bigrat_set_i64(out, 1, 1);
    if (status == PHY_OK) status = phy_bigrat_divide(out, &scale, out);
    destroy_bigrat(&scale);
    if (phy_bigint_is_initialized(&power)) phy_bigint_destroy(&power);
    if (phy_bigint_is_initialized(&ten)) phy_bigint_destroy(&ten);
    return status;
}

/* Checked mixed significant-digit contract used by every numerical entry
   point: radius <= 10^-digits max(1, |midpoint|).  All comparisons remain in
   Q; this predicate never estimates the accuracy through floating point. */
static phy_status ball_meets_precision(const phy_real_ball *ball,
                                       unsigned digits, bool *out_meets)
{
    if (ball == NULL || out_meets == NULL || digits == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_meets = false;
    phy_exact_context *exact =
        phy_bigrat_numerator(&ball->midpoint)->context;
    phy_bigrat magnitude = {0};
    phy_bigrat one = {0};
    phy_bigrat unit = {0};
    phy_bigrat tolerance = {0};
    phy_status status = phy_bigrat_init(exact, &magnitude);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &one);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &unit);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &tolerance);
    if (status == PHY_OK) {
        status = phy_bigrat_sign(&ball->midpoint) < 0
                     ? phy_bigrat_negate(&ball->midpoint, &magnitude)
                     : phy_bigrat_copy(&ball->midpoint, &magnitude);
    }
    if (status == PHY_OK) status = phy_bigrat_set_i64(&one, 1, 1);
    int order = 0;
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&magnitude, &one, &order);
    }
    if (status == PHY_OK && order < 0) {
        status = phy_bigrat_copy(&one, &magnitude);
    }
    if (status == PHY_OK) status = decimal_unit(exact, digits, &unit);
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&magnitude, &unit, &tolerance);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&ball->radius, &tolerance, &order);
    }
    if (status == PHY_OK) *out_meets = order <= 0;
    destroy_bigrat(&tolerance);
    destroy_bigrat(&unit);
    destroy_bigrat(&one);
    destroy_bigrat(&magnitude);
    return status;
}

static phy_status complex_ball_meets_precision(
    const phy_complex_ball *ball, unsigned digits, bool *out_meets)
{
    bool real_meets = false;
    bool imaginary_meets = false;
    phy_status status = ball_meets_precision(
        &ball->real, digits, &real_meets);
    if (status == PHY_OK) {
        status = ball_meets_precision(
            &ball->imaginary, digits, &imaginary_meets);
    }
    if (status == PHY_OK) *out_meets = real_meets && imaginary_meets;
    return status;
}

static phy_status ball_set_interval_text(phy_real_ball *ball,
                                         const char *lower,
                                         const char *upper)
{
    phy_exact_context *context =
        phy_bigrat_numerator(&ball->midpoint)->context;
    phy_bigrat low;
    phy_bigrat high;
    memset(&low, 0, sizeof low);
    memset(&high, 0, sizeof high);
    phy_status status = phy_bigrat_init(context, &low);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &high);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_read(&low, lower, kDecimalScale40);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_read(&high, upper, kDecimalScale40);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_interval(ball, &low, &high);
    }
    destroy_bigrat(&high);
    destroy_bigrat(&low);
    return status;
}

static phy_status constant_ball(phy_cas *cas, phy_ir_ref expression,
                                phy_real_ball *out, bool *out_matched)
{
    *out_matched = true;
    if (expression == cas->constant_pi) {
        return ball_set_interval_text(
            out, "31415926535897932384626433832795028841971",
            "31415926535897932384626433832795028841972");
    }
    if (expression == cas->constant_e) {
        return ball_set_interval_text(
            out, "27182818284590452353602874713526624977572",
            "27182818284590452353602874713526624977573");
    }
    if (expression == cas->constant_euler_gamma) {
        return ball_set_interval_text(
            out, "5772156649015328606065120900824024310421",
            "5772156649015328606065120900824024310422");
    }
    *out_matched = false;
    return PHY_OK;
}

static phy_status eval_ball_node(phy_cas *cas, phy_exact_context *exact,
                                 phy_ir_ref expression,
                                 phy_ir_ref assigned_variable,
                                 const phy_real_ball *assigned_value,
                                 uint32_t rounds, phy_real_ball *out)
{
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }
    if (expression == assigned_variable && assigned_value != NULL) {
        return phy_real_ball_copy(assigned_value, out);
    }
    if (phy_cas_is_exact(cas, expression)) {
        phy_bigrat value;
        memset(&value, 0, sizeof value);
        status = phy_bigrat_init(exact, &value);
        if (status == PHY_OK) {
            status = phy_cas_exact_load_ref(cas, exact, expression, &value);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_set_exact(out, &value);
        }
        destroy_bigrat(&value);
        return status;
    }
    bool matched = false;
    status = constant_ball(cas, expression, out, &matched);
    if (status != PHY_OK || matched) {
        return status;
    }
    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, expression);
    if (kind == PHY_IR_ADD || kind == PHY_IR_MUL) {
        phy_real_ball accumulator;
        phy_real_ball child;
        phy_real_ball temporary;
        memset(&accumulator, 0, sizeof accumulator);
        memset(&child, 0, sizeof child);
        memset(&temporary, 0, sizeof temporary);
        status = phy_real_ball_init(exact, &accumulator);
        if (status == PHY_OK) {
            status = phy_real_ball_init(exact, &child);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_init(exact, &temporary);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_set_i64(
                &accumulator, kind == PHY_IR_MUL ? 1 : 0, 1);
        }
        const size_t count = phy_ir_child_count(cas->ir, expression);
        for (size_t index = 0u; status == PHY_OK && index < count; ++index) {
            status = eval_ball_node(
                cas, exact, phy_ir_child(cas->ir, expression, index),
                assigned_variable, assigned_value, rounds, &child);
            if (status == PHY_OK) {
                status = kind == PHY_IR_ADD
                             ? phy_real_ball_add(
                                   &accumulator, &child, &temporary)
                             : phy_real_ball_multiply(
                                   &accumulator, &child, &temporary);
            }
            if (status == PHY_OK) {
                status = phy_real_ball_copy(&temporary, &accumulator);
            }
        }
        if (status == PHY_OK) {
            status = phy_real_ball_copy(&accumulator, out);
        }
        phy_real_ball_destroy(&temporary);
        phy_real_ball_destroy(&child);
        phy_real_ball_destroy(&accumulator);
        return status;
    }
    if (kind == PHY_IR_POW) {
        const phy_ir_ref base_ref = phy_ir_child(cas->ir, expression, 0u);
        const phy_ir_ref exponent_ref = phy_ir_child(cas->ir, expression, 1u);
        phy_real_ball base;
        memset(&base, 0, sizeof base);
        status = phy_real_ball_init(exact, &base);
        if (status == PHY_OK) {
            status = eval_ball_node(
                cas, exact, base_ref, assigned_variable, assigned_value,
                rounds, &base);
        }
        int64_t exponent = 0;
        if (status == PHY_OK &&
            phy_ir_integer_value(cas->ir, exponent_ref, &exponent) &&
            exponent >= INT32_MIN && exponent <= INT32_MAX) {
            status = phy_real_ball_pow_i32(
                &base, (int32_t)exponent, out);
        } else if (status == PHY_OK) {
            int64_t numerator = 0;
            int64_t denominator = 0;
            if (phy_ir_rational_value(
                    cas->ir, exponent_ref, &numerator, &denominator) &&
                numerator == 1 && denominator == 2) {
                status = phy_real_ball_sqrt(&base, rounds, out);
            } else {
                status = PHY_ERR_UNSUPPORTED;
            }
        }
        phy_real_ball_destroy(&base);
        return status;
    }
    if (kind == PHY_IR_FUNCTION &&
        phy_ir_child_count(cas->ir, expression) == 1u) {
        const phy_ir_symbol head = phy_ir_head(cas->ir, expression);
        if (head == cas->functions[PHY_CAS_FN_EXP] ||
            head == cas->functions[PHY_CAS_FN_LOG] ||
            head == cas->functions[PHY_CAS_FN_SIN] ||
            head == cas->functions[PHY_CAS_FN_COS] ||
            head == cas->functions[PHY_CAS_FN_TAN] ||
            head == cas->functions[PHY_CAS_FN_SINH] ||
            head == cas->functions[PHY_CAS_FN_COSH] ||
            head == cas->functions[PHY_CAS_FN_TANH] ||
            head == cas->functions[PHY_CAS_FN_ASIN] ||
            head == cas->functions[PHY_CAS_FN_ACOS] ||
            head == cas->functions[PHY_CAS_FN_ATAN] ||
            head == cas->functions[PHY_CAS_FN_ASINH] ||
            head == cas->functions[PHY_CAS_FN_ACOSH] ||
            head == cas->functions[PHY_CAS_FN_ATANH] ||
            head == cas->functions[PHY_CAS_FN_ERF] ||
            head == cas->functions[PHY_CAS_FN_ERFC]) {
            phy_real_ball argument;
            memset(&argument, 0, sizeof argument);
            status = phy_real_ball_init(exact, &argument);
            if (status == PHY_OK) {
                status = eval_ball_node(
                    cas, exact, phy_ir_child(cas->ir, expression, 0u),
                    assigned_variable, assigned_value, rounds, &argument);
            }
            if (status == PHY_OK) {
                if (head == cas->functions[PHY_CAS_FN_EXP]) {
                    status = phy_real_ball_exp(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_LOG]) {
                    status = phy_real_ball_log(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_SIN]) {
                    status = phy_real_ball_sin(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_COS]) {
                    status = phy_real_ball_cos(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_TAN]) {
                    status = phy_real_ball_tan(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_SINH]) {
                    status = phy_real_ball_sinh(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_COSH]) {
                    status = phy_real_ball_cosh(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_TANH]) {
                    status = phy_real_ball_tanh(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_ASIN]) {
                    status = phy_real_ball_asin(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_ACOS]) {
                    status = phy_real_ball_acos(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_ATAN]) {
                    status = phy_real_ball_atan(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_ASINH]) {
                    status = phy_real_ball_asinh(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_ACOSH]) {
                    status = phy_real_ball_acosh(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_ATANH]) {
                    status = phy_real_ball_atanh(&argument, rounds, out);
                } else if (head == cas->functions[PHY_CAS_FN_ERF]) {
                    status = phy_real_ball_erf(&argument, rounds, out);
                } else {
                    status = phy_real_ball_erfc(&argument, rounds, out);
                }
            }
            phy_real_ball_destroy(&argument);
            return status;
        }
    }
    return PHY_ERR_UNSUPPORTED;
}

static phy_status complex_constant_ball(phy_cas *cas,
                                        phy_ir_ref expression,
                                        phy_complex_ball *out,
                                        bool *out_matched)
{
    *out_matched = true;
    if (expression == cas->constant_i) {
        return phy_complex_ball_set_i64(out, 0, 1, 1, 1);
    }
    phy_exact_context *exact =
        phy_bigrat_numerator(&out->real.midpoint)->context;
    phy_real_ball real;
    memset(&real, 0, sizeof real);
    phy_status status = phy_real_ball_init(exact, &real);
    bool real_matched = false;
    if (status == PHY_OK) {
        status = constant_ball(cas, expression, &real, &real_matched);
    }
    if (status == PHY_OK && real_matched) {
        status = phy_complex_ball_set_real(out, &real);
    }
    phy_real_ball_destroy(&real);
    *out_matched = real_matched;
    return status;
}

static phy_status eval_complex_ball_node(
    phy_cas *cas, phy_exact_context *exact, phy_ir_ref expression,
    phy_ir_ref assigned_variable, const phy_complex_ball *assigned_value,
    uint32_t rounds, phy_complex_ball *out)
{
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) return status;
    if (expression == assigned_variable && assigned_value != NULL) {
        return phy_complex_ball_copy(assigned_value, out);
    }
    if (phy_cas_is_exact(cas, expression)) {
        phy_bigrat value;
        phy_real_ball real;
        memset(&value, 0, sizeof value);
        memset(&real, 0, sizeof real);
        status = phy_bigrat_init(exact, &value);
        if (status == PHY_OK) status = phy_real_ball_init(exact, &real);
        if (status == PHY_OK) {
            status = phy_cas_exact_load_ref(cas, exact, expression, &value);
        }
        if (status == PHY_OK) status = phy_real_ball_set_exact(&real, &value);
        if (status == PHY_OK) status = phy_complex_ball_set_real(out, &real);
        phy_real_ball_destroy(&real);
        destroy_bigrat(&value);
        return status;
    }
    bool matched = false;
    status = complex_constant_ball(cas, expression, out, &matched);
    if (status != PHY_OK || matched) return status;
    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, expression);
    if (kind == PHY_IR_ADD || kind == PHY_IR_MUL) {
        phy_complex_ball accumulator;
        phy_complex_ball child;
        phy_complex_ball temporary;
        memset(&accumulator, 0, sizeof accumulator);
        memset(&child, 0, sizeof child);
        memset(&temporary, 0, sizeof temporary);
        status = phy_complex_ball_init(exact, &accumulator);
        if (status == PHY_OK) status = phy_complex_ball_init(exact, &child);
        if (status == PHY_OK) status = phy_complex_ball_init(exact, &temporary);
        if (status == PHY_OK) {
            status = phy_complex_ball_set_i64(
                &accumulator, kind == PHY_IR_MUL ? 1 : 0, 1, 0, 1);
        }
        const size_t count = phy_ir_child_count(cas->ir, expression);
        for (size_t index = 0u; status == PHY_OK && index < count; ++index) {
            status = eval_complex_ball_node(
                cas, exact, phy_ir_child(cas->ir, expression, index),
                assigned_variable, assigned_value, rounds, &child);
            if (status == PHY_OK) {
                status = kind == PHY_IR_ADD
                             ? phy_complex_ball_add(
                                   &accumulator, &child, &temporary)
                             : phy_complex_ball_multiply(
                                   &accumulator, &child, &temporary);
            }
            if (status == PHY_OK) {
                status = phy_complex_ball_copy(&temporary, &accumulator);
            }
        }
        if (status == PHY_OK) status = phy_complex_ball_copy(&accumulator, out);
        phy_complex_ball_destroy(&temporary);
        phy_complex_ball_destroy(&child);
        phy_complex_ball_destroy(&accumulator);
        return status;
    }
    if (kind == PHY_IR_POW) {
        const phy_ir_ref base_ref = phy_ir_child(cas->ir, expression, 0u);
        const phy_ir_ref exponent_ref = phy_ir_child(cas->ir, expression, 1u);
        phy_complex_ball base;
        memset(&base, 0, sizeof base);
        status = phy_complex_ball_init(exact, &base);
        if (status == PHY_OK) {
            status = eval_complex_ball_node(
                cas, exact, base_ref, assigned_variable, assigned_value,
                rounds, &base);
        }
        int64_t exponent = 0;
        if (status == PHY_OK &&
            phy_ir_integer_value(cas->ir, exponent_ref, &exponent) &&
            exponent >= INT32_MIN && exponent <= INT32_MAX) {
            status = phy_complex_ball_pow_i32(
                &base, (int32_t)exponent, out);
        } else if (status == PHY_OK) {
            int64_t numerator = 0;
            int64_t denominator = 0;
            if (phy_ir_rational_value(
                    cas->ir, exponent_ref, &numerator, &denominator) &&
                numerator == 1 && denominator == 2) {
                status = phy_complex_ball_sqrt(&base, rounds, out);
            } else {
                status = PHY_ERR_UNSUPPORTED;
            }
        }
        phy_complex_ball_destroy(&base);
        return status;
    }
    if (kind == PHY_IR_FUNCTION &&
        phy_ir_child_count(cas->ir, expression) == 1u) {
        const phy_ir_symbol head = phy_ir_head(cas->ir, expression);
        phy_complex_ball argument;
        memset(&argument, 0, sizeof argument);
        status = phy_complex_ball_init(exact, &argument);
        if (status == PHY_OK) {
            status = eval_complex_ball_node(
                cas, exact, phy_ir_child(cas->ir, expression, 0u),
                assigned_variable, assigned_value, rounds, &argument);
        }
        if (status == PHY_OK) {
            if (head == cas->functions[PHY_CAS_FN_EXP]) {
                status = phy_complex_ball_exp(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_LOG]) {
                status = phy_complex_ball_log(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_SIN]) {
                status = phy_complex_ball_sin(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_COS]) {
                status = phy_complex_ball_cos(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_TAN]) {
                status = phy_complex_ball_tan(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_SINH]) {
                status = phy_complex_ball_sinh(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_COSH]) {
                status = phy_complex_ball_cosh(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_TANH]) {
                status = phy_complex_ball_tanh(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_ASIN]) {
                status = phy_complex_ball_asin(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_ACOS]) {
                status = phy_complex_ball_acos(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_ATAN]) {
                status = phy_complex_ball_atan(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_ASINH]) {
                status = phy_complex_ball_asinh(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_ACOSH]) {
                status = phy_complex_ball_acosh(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_ATANH]) {
                status = phy_complex_ball_atanh(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_ERF]) {
                status = phy_complex_ball_erf(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_ERFC]) {
                status = phy_complex_ball_erfc(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_GAMMA]) {
                status = phy_complex_ball_gamma(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_LOGGAMMA]) {
                status = phy_complex_ball_loggamma(&argument, rounds, out);
            } else if (head == cas->functions[PHY_CAS_FN_DIGAMMA]) {
                status = phy_complex_ball_digamma(&argument, rounds, out);
            } else {
                status = PHY_ERR_UNSUPPORTED;
            }
        }
        phy_complex_ball_destroy(&argument);
        return status;
    }
    return PHY_ERR_UNSUPPORTED;
}

static phy_status publish_ball(phy_cas *cas, const phy_real_ball *ball,
                               unsigned decimal_digits,
                               phy_ir_ref *out_ref)
{
    phy_ir_ref arguments[3] = {PHY_IR_NULL, PHY_IR_NULL, PHY_IR_NULL};
    phy_status status = phy_cas_exact_publish_bigrat(
        cas, &ball->midpoint, &arguments[0]);
    if (status == PHY_OK) {
        status = phy_cas_exact_publish_bigrat(
            cas, &ball->radius, &arguments[1]);
    }
    if (status == PHY_OK) {
        arguments[2] = phy_ir_integer(cas->ir, (int64_t)decimal_digits);
        if (arguments[2] == PHY_IR_NULL) status = phy_cas_ir_failure(cas);
    }
    if (status == PHY_OK) {
        const phy_ir_symbol around = phy_ir_intern(cas->ir, "Around");
        *out_ref = phy_ir_function(cas->ir, around, arguments, 3u);
        if (*out_ref == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
        }
    }
    return status;
}

static phy_status publish_complex_ball(phy_cas *cas,
                                       const phy_complex_ball *ball,
                                       unsigned decimal_digits,
                                       phy_ir_ref *out_ref)
{
    if (phy_bigrat_sign(&ball->imaginary.midpoint) == 0 &&
        phy_bigrat_sign(&ball->imaginary.radius) == 0) {
        return publish_ball(cas, &ball->real, decimal_digits, out_ref);
    }
    phy_ir_ref arguments[2] = {PHY_IR_NULL, PHY_IR_NULL};
    phy_status status = publish_ball(
        cas, &ball->real, decimal_digits, &arguments[0]);
    if (status == PHY_OK) {
        status = publish_ball(
            cas, &ball->imaginary, decimal_digits, &arguments[1]);
    }
    if (status == PHY_OK) {
        const phy_ir_symbol complex_around =
            phy_ir_intern(cas->ir, "ComplexAround");
        *out_ref = phy_ir_function(
            cas->ir, complex_around, arguments, 2u);
        if (*out_ref == PHY_IR_NULL) status = phy_cas_ir_failure(cas);
    }
    return status;
}

phy_status phy_cas_n(phy_cas *cas, phy_ir_ref expression,
                     unsigned decimal_digits, phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL || expression == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    if (decimal_digits == 0u) {
        decimal_digits = BALL_DEFAULT_DECIMAL_DIGITS;
    }
    if (decimal_digits > BALL_MAX_DECIMAL_DIGITS) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_ir_ref simplified = PHY_IR_NULL;
    phy_status status = phy_cas_full_simplify(
        cas, expression, &simplified);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_begin(cas);
    phy_exact_context *exact = ball_exact_operation_context(cas);
    if (exact == NULL) return PHY_ERR_OUT_OF_MEMORY;
    uint32_t rounds = decimal_digits * 4u + 8u;
    bool precise = false;
    for (unsigned attempt = 0u;
         status == PHY_OK && attempt < BALL_PRECISION_ATTEMPTS && !precise;
         ++attempt) {
        phy_real_ball value = {0};
        status = phy_real_ball_init(exact, &value);
        if (status == PHY_OK) {
            status = eval_ball_node(
                cas, exact, simplified, PHY_IR_NULL, NULL, rounds, &value);
        }
        if (status == PHY_OK) {
            status = ball_meets_precision(&value, decimal_digits, &precise);
        }
        if (status == PHY_OK && precise) {
            status = publish_ball(
                cas, &value, decimal_digits, out_ref);
        }
        phy_real_ball_destroy(&value);
        if (status == PHY_OK && !precise) {
            rounds = rounds > UINT32_MAX / 2u ? UINT32_MAX : rounds * 2u;
        }
    }
    phy_exact_context_destroy(exact);
    if (status == PHY_OK && !precise) status = PHY_ERR_TERM_LIMIT;
    if (status == PHY_ERR_UNSUPPORTED || status == PHY_ERR_DOMAIN) {
        exact = ball_exact_operation_context(cas);
        if (exact == NULL) return PHY_ERR_OUT_OF_MEMORY;
        status = PHY_OK;
        precise = false;
        rounds = decimal_digits * 4u + 8u;
        for (unsigned attempt = 0u;
             status == PHY_OK && attempt < BALL_PRECISION_ATTEMPTS &&
             !precise; ++attempt) {
            phy_complex_ball complex_value = {0};
            status = phy_complex_ball_init(exact, &complex_value);
            if (status == PHY_OK) {
                status = eval_complex_ball_node(
                    cas, exact, simplified, PHY_IR_NULL, NULL,
                    rounds, &complex_value);
            }
            if (status == PHY_OK) {
                status = complex_ball_meets_precision(
                    &complex_value, decimal_digits, &precise);
            }
            if (status == PHY_OK && precise) {
                status = publish_complex_ball(
                    cas, &complex_value, decimal_digits, out_ref);
            }
            phy_complex_ball_destroy(&complex_value);
            if (status == PHY_OK && !precise) {
                rounds = rounds > UINT32_MAX / 2u
                             ? UINT32_MAX : rounds * 2u;
            }
        }
        phy_exact_context_destroy(exact);
        if (status == PHY_OK && !precise) status = PHY_ERR_TERM_LIMIT;
    }
    return status;
}

static phy_status integer_coefficient_strings(
    phy_cas *cas, phy_exact_context *exact, phy_ir_ref coefficients,
    const char **out_strings, size_t count, char **out_storage,
    size_t *out_storage_bytes)
{
    phy_bigrat values[BALL_MAX_POLYNOMIAL_DEGREE + 1u];
    memset(values, 0, sizeof values);
    size_t initialized = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && initialized < count) {
        status = phy_bigrat_init(exact, &values[initialized]);
        if (status == PHY_OK) {
            initialized++;
            status = phy_cas_exact_load_ref(
                cas, exact,
                phy_ir_child(cas->ir, coefficients, initialized - 1u),
                &values[initialized - 1u]);
        }
    }
    phy_bigint lcm;
    phy_bigint gcd;
    phy_bigint quotient;
    phy_bigint product;
    phy_bigint scaled;
    memset(&lcm, 0, sizeof lcm);
    memset(&gcd, 0, sizeof gcd);
    memset(&quotient, 0, sizeof quotient);
    memset(&product, 0, sizeof product);
    memset(&scaled, 0, sizeof scaled);
    phy_bigint *integers[] = {&lcm, &gcd, &quotient, &product, &scaled};
    size_t integer_count = 0u;
    while (status == PHY_OK &&
           integer_count < sizeof integers / sizeof integers[0]) {
        status = phy_bigint_init(exact, integers[integer_count]);
        if (status == PHY_OK) {
            integer_count++;
        }
    }
    if (status == PHY_OK) {
        status = phy_bigint_set_i64(&lcm, 1);
    }
    for (size_t index = 0u; status == PHY_OK && index < count; ++index) {
        status = phy_bigint_gcd(
            &lcm, phy_bigrat_denominator(&values[index]), &gcd);
        if (status == PHY_OK) {
            status = phy_bigint_divide_exact(&lcm, &gcd, &quotient);
        }
        if (status == PHY_OK) {
            status = phy_bigint_multiply(
                &quotient, phy_bigrat_denominator(&values[index]), &product);
        }
        if (status == PHY_OK) {
            status = phy_bigint_copy(&product, &lcm);
        }
    }
    size_t lengths[BALL_MAX_POLYNOMIAL_DEGREE + 1u];
    size_t total = 0u;
    for (size_t index = 0u; status == PHY_OK && index < count; ++index) {
        status = phy_bigint_divide_exact(
            &lcm, phy_bigrat_denominator(&values[index]), &quotient);
        if (status == PHY_OK) {
            status = phy_bigint_multiply(
                phy_bigrat_numerator(&values[index]), &quotient, &scaled);
        }
        if (status == PHY_OK) {
            status = phy_bigint_write(
                &scaled, NULL, 0u, &lengths[index]);
        }
        if (status == PHY_OK && lengths[index] > (size_t)-1 - total) {
            status = PHY_ERR_MEMORY_LIMIT;
        } else if (status == PHY_OK) {
            total += lengths[index];
        }
    }
    char *storage = NULL;
    if (status == PHY_OK) {
        status = phy_cas_temp_alloc(cas, total, (void **)&storage);
    }
    size_t at = 0u;
    for (size_t index = 0u; status == PHY_OK && index < count; ++index) {
        status = phy_bigint_divide_exact(
            &lcm, phy_bigrat_denominator(&values[index]), &quotient);
        if (status == PHY_OK) {
            status = phy_bigint_multiply(
                phy_bigrat_numerator(&values[index]), &quotient, &scaled);
        }
        if (status == PHY_OK) {
            status = phy_bigint_write(
                &scaled, storage + at, lengths[index], &lengths[index]);
        }
        if (status == PHY_OK) {
            out_strings[index] = storage + at;
            at += lengths[index];
        }
    }
    if (status == PHY_OK) {
        *out_storage = storage;
        *out_storage_bytes = total;
    } else if (storage != NULL) {
        phy_cas_temp_free(cas, storage, total);
    }
    while (integer_count != 0u) {
        phy_bigint_destroy(integers[--integer_count]);
    }
    while (initialized != 0u) {
        phy_bigrat_destroy(&values[--initialized]);
    }
    return status;
}

static phy_status parse_rational_text(phy_bigrat *out, char *text);

static phy_status algebraic_value_ball(
    phy_cas *cas, phy_real_algebraic *value, phy_exact_context *exact,
    unsigned decimal_digits, phy_real_ball *out)
{
    phy_status status = phy_real_algebraic_refine(
        value, decimal_digits * 4u + 8u);
    size_t lower_required = 0u;
    size_t upper_required = 0u;
    if (status == PHY_OK) {
        status = phy_real_algebraic_write_lower(
            value, NULL, 0u, &lower_required);
    }
    if (status == PHY_OK) {
        status = phy_real_algebraic_write_upper(
            value, NULL, 0u, &upper_required);
    }
    const size_t total = lower_required + upper_required;
    char *storage = NULL;
    if (status == PHY_OK) {
        status = phy_cas_temp_alloc(cas, total, (void **)&storage);
    }
    if (status == PHY_OK) {
        status = phy_real_algebraic_write_lower(
            value, storage, lower_required, &lower_required);
    }
    if (status == PHY_OK) {
        status = phy_real_algebraic_write_upper(
            value, storage + lower_required, upper_required,
            &upper_required);
    }
    phy_bigrat lower;
    phy_bigrat upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &upper);
    }
    if (status == PHY_OK) {
        status = parse_rational_text(&lower, storage);
    }
    char *upper_text = storage == NULL ? NULL : storage + lower_required;
    if (status == PHY_OK) {
        status = parse_rational_text(&upper, upper_text);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_interval(out, &lower, &upper);
    }
    destroy_bigrat(&upper);
    destroy_bigrat(&lower);
    if (storage != NULL) {
        phy_cas_temp_free(cas, storage, total);
    }
    return status;
}

static phy_status parse_rational_text(phy_bigrat *out, char *text)
{
    char *slash = strchr(text, '/');
    if (slash == NULL) {
        return phy_bigrat_read(out, text, "1");
    }
    *slash = '\0';
    return phy_bigrat_read(out, text, slash + 1);
}

static phy_status nsolve_complex_quadratic(
    phy_cas *cas, phy_exact_context *exact, phy_ir_ref coefficients,
    phy_ir_ref denominator_expression, phy_ir_ref variable,
    unsigned decimal_digits, phy_ir_ref *out_ref, bool *out_matched)
{
    *out_matched = false;
    if (phy_ir_child_count(cas->ir, coefficients) != 3u) return PHY_OK;
    phy_bigrat coefficient[3];
    phy_bigrat b_squared;
    phy_bigrat ac;
    phy_bigrat four;
    phy_bigrat discriminant;
    phy_bigrat negative_b;
    phy_bigrat two;
    phy_bigrat denominator;
    phy_bigrat real_part;
    memset(coefficient, 0, sizeof coefficient);
    memset(&b_squared, 0, sizeof b_squared);
    memset(&ac, 0, sizeof ac);
    memset(&four, 0, sizeof four);
    memset(&discriminant, 0, sizeof discriminant);
    memset(&negative_b, 0, sizeof negative_b);
    memset(&two, 0, sizeof two);
    memset(&denominator, 0, sizeof denominator);
    memset(&real_part, 0, sizeof real_part);
    phy_bigrat *values[] = {
        &coefficient[0], &coefficient[1], &coefficient[2],
        &b_squared, &ac, &four, &discriminant, &negative_b,
        &two, &denominator, &real_part};
    size_t initialized = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized < sizeof values / sizeof values[0]) {
        status = phy_bigrat_init(exact, values[initialized]);
        if (status == PHY_OK) initialized++;
    }
    for (size_t index = 0u; status == PHY_OK && index < 3u; ++index) {
        status = phy_cas_exact_load_ref(
            cas, exact, phy_ir_child(cas->ir, coefficients, index),
            &coefficient[index]);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &coefficient[1], &coefficient[1], &b_squared);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &coefficient[2], &coefficient[0], &ac);
    }
    if (status == PHY_OK) status = phy_bigrat_set_i64(&four, 4, 1);
    if (status == PHY_OK) status = phy_bigrat_multiply(&ac, &four, &ac);
    if (status == PHY_OK) {
        status = phy_bigrat_subtract(&b_squared, &ac, &discriminant);
    }
    if (status == PHY_OK && phy_bigrat_sign(&discriminant) >= 0) {
        while (initialized != 0u) {
            phy_bigrat_destroy(values[--initialized]);
        }
        return PHY_OK;
    }
    if (status == PHY_OK) *out_matched = true;
    if (status == PHY_OK) {
        status = phy_bigrat_negate(&coefficient[1], &negative_b);
    }
    if (status == PHY_OK) status = phy_bigrat_set_i64(&two, 2, 1);
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &two, &coefficient[2], &denominator);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(&negative_b, &denominator, &real_part);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_negate(&discriminant, &discriminant);
    }

    phy_real_ball radicand;
    phy_real_ball imaginary_part;
    phy_real_ball denominator_ball;
    memset(&radicand, 0, sizeof radicand);
    memset(&imaginary_part, 0, sizeof imaginary_part);
    memset(&denominator_ball, 0, sizeof denominator_ball);
    if (status == PHY_OK) status = phy_real_ball_init(exact, &radicand);
    if (status == PHY_OK) status = phy_real_ball_init(exact, &imaginary_part);
    if (status == PHY_OK) {
        status = phy_real_ball_init(exact, &denominator_ball);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_exact(&radicand, &discriminant);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_sqrt(
            &radicand, decimal_digits * 4u + 8u, &imaginary_part);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_set_exact(&denominator_ball, &denominator);
    }
    if (status == PHY_OK) {
        status = phy_real_ball_divide(
            &imaginary_part, &denominator_ball, &imaginary_part);
    }

    phy_complex_ball roots[2];
    memset(roots, 0, sizeof roots);
    size_t root_count = 0u;
    while (status == PHY_OK && root_count < 2u) {
        status = phy_complex_ball_init(exact, &roots[root_count]);
        if (status == PHY_OK) root_count++;
    }
    for (size_t index = 0u; status == PHY_OK && index < 2u; ++index) {
        status = phy_real_ball_set_exact(&roots[index].real, &real_part);
        if (status == PHY_OK && index == 0u) {
            status = phy_real_ball_copy(
                &imaginary_part, &roots[index].imaginary);
        } else if (status == PHY_OK) {
            phy_real_ball minus_one;
            memset(&minus_one, 0, sizeof minus_one);
            status = phy_real_ball_init(exact, &minus_one);
            if (status == PHY_OK) {
                status = phy_real_ball_set_i64(&minus_one, -1, 1);
            }
            if (status == PHY_OK) {
                status = phy_real_ball_multiply(
                    &imaginary_part, &minus_one,
                    &roots[index].imaginary);
            }
            phy_real_ball_destroy(&minus_one);
        }
    }

    phy_ir_ref branches[2] = {PHY_IR_NULL, PHY_IR_NULL};
    const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
    const phy_ir_symbol rule = phy_ir_intern(cas->ir, "Rule");
    for (size_t index = 0u; status == PHY_OK && index < 2u; ++index) {
        phy_complex_ball denominator_value;
        memset(&denominator_value, 0, sizeof denominator_value);
        status = phy_complex_ball_init(exact, &denominator_value);
        if (status == PHY_OK) {
            status = eval_complex_ball_node(
                cas, exact, denominator_expression, variable, &roots[index],
                decimal_digits * 4u + 8u, &denominator_value);
        }
        bool denominator_zero = true;
        if (status == PHY_OK) {
            status = phy_complex_ball_contains_zero_checked(
                &denominator_value, &denominator_zero);
        }
        if (status == PHY_OK && denominator_zero) {
            status = PHY_ERR_UNSUPPORTED;
        }
        phy_ir_ref around = PHY_IR_NULL;
        if (status == PHY_OK) {
            bool precise = false;
            status = complex_ball_meets_precision(
                &roots[index], decimal_digits, &precise);
            if (status == PHY_OK && !precise) status = PHY_ERR_TERM_LIMIT;
            if (status == PHY_OK) {
                status = publish_complex_ball(
                    cas, &roots[index], decimal_digits, &around);
            }
        }
        if (status == PHY_OK) {
            const phy_ir_ref rule_arguments[2] = {variable, around};
            const phy_ir_ref one_rule =
                phy_ir_function(cas->ir, rule, rule_arguments, 2u);
            branches[index] = one_rule == PHY_IR_NULL
                                  ? PHY_IR_NULL
                                  : phy_ir_function(
                                        cas->ir, list, &one_rule, 1u);
            if (branches[index] == PHY_IR_NULL) {
                status = phy_cas_ir_failure(cas);
            }
        }
        phy_complex_ball_destroy(&denominator_value);
    }
    if (status == PHY_OK) {
        *out_ref = phy_ir_function(cas->ir, list, branches, 2u);
        if (*out_ref == PHY_IR_NULL) status = phy_cas_ir_failure(cas);
    }
    while (root_count != 0u) {
        phy_complex_ball_destroy(&roots[--root_count]);
    }
    phy_real_ball_destroy(&denominator_ball);
    phy_real_ball_destroy(&imaginary_part);
    phy_real_ball_destroy(&radicand);
    while (initialized != 0u) {
        phy_bigrat_destroy(values[--initialized]);
    }
    return status;
}

static phy_status nsolve_general_complex(
    phy_cas *cas, phy_exact_context *exact, phy_ir_ref coefficients_ref,
    phy_ir_ref denominator_expression, phy_ir_ref variable,
    unsigned decimal_digits, phy_ir_ref *out_ref)
{
    const size_t coefficient_count =
        phy_ir_child_count(cas->ir, coefficients_ref);
    const size_t degree = coefficient_count - 1u;
    phy_bigrat coefficients[BALL_MAX_POLYNOMIAL_DEGREE + 1u];
    memset(coefficients, 0, sizeof coefficients);
    size_t initialized_coefficients = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK &&
           initialized_coefficients < coefficient_count) {
        status = phy_bigrat_init(
            exact, &coefficients[initialized_coefficients]);
        if (status == PHY_OK) initialized_coefficients++;
    }
    for (size_t index = 0u; status == PHY_OK && index < coefficient_count;
         ++index) {
        status = phy_cas_exact_load_ref(
            cas, exact, phy_ir_child(cas->ir, coefficients_ref, index),
            &coefficients[index]);
    }
    phy_complex_ball roots[BALL_MAX_POLYNOMIAL_DEGREE];
    memset(roots, 0, sizeof roots);
    size_t initialized_roots = 0u;
    while (status == PHY_OK && initialized_roots < degree) {
        status = phy_complex_ball_init(exact, &roots[initialized_roots]);
        if (status == PHY_OK) initialized_roots++;
    }
    size_t root_count = 0u;
    if (status == PHY_OK) {
        status = phy_complex_roots_isolate(
            exact, coefficients, coefficient_count,
            decimal_digits_to_root_bits(decimal_digits), roots,
            BALL_MAX_POLYNOMIAL_DEGREE, &root_count);
    }
    phy_ir_ref branches[BALL_MAX_POLYNOMIAL_DEGREE];
    size_t branch_count = 0u;
    const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
    const phy_ir_symbol rule = phy_ir_intern(cas->ir, "Rule");
    for (size_t index = 0u; status == PHY_OK && index < root_count; ++index) {
        phy_complex_ball denominator_value = {0};
        status = phy_complex_ball_init(exact, &denominator_value);
        if (status == PHY_OK) {
            status = eval_complex_ball_node(
                cas, exact, denominator_expression, variable, &roots[index],
                decimal_digits * 4u + 8u, &denominator_value);
        }
        bool denominator_zero = true;
        if (status == PHY_OK) {
            status = phy_complex_ball_contains_zero_checked(
                &denominator_value, &denominator_zero);
        }
        if (status == PHY_OK && denominator_zero) {
            status = PHY_ERR_UNSUPPORTED;
        }
        phy_ir_ref around = PHY_IR_NULL;
        if (status == PHY_OK) {
            bool precise = false;
            status = complex_ball_meets_precision(
                &roots[index], decimal_digits, &precise);
            if (status == PHY_OK && !precise) status = PHY_ERR_TERM_LIMIT;
            if (status == PHY_OK) {
                status = publish_complex_ball(
                    cas, &roots[index], decimal_digits, &around);
            }
        }
        if (status == PHY_OK) {
            const phy_ir_ref rule_arguments[2] = {variable, around};
            const phy_ir_ref one_rule =
                phy_ir_function(cas->ir, rule, rule_arguments, 2u);
            branches[branch_count] =
                one_rule == PHY_IR_NULL
                    ? PHY_IR_NULL
                    : phy_ir_function(cas->ir, list, &one_rule, 1u);
            if (branches[branch_count] == PHY_IR_NULL) {
                status = phy_cas_ir_failure(cas);
            } else {
                branch_count++;
            }
        }
        phy_complex_ball_destroy(&denominator_value);
    }
    if (status == PHY_OK) {
        *out_ref = phy_ir_function(cas->ir, list, branches, branch_count);
        if (*out_ref == PHY_IR_NULL) status = phy_cas_ir_failure(cas);
    }
    while (initialized_roots != 0u) {
        phy_complex_ball_destroy(&roots[--initialized_roots]);
    }
    while (initialized_coefficients != 0u) {
        phy_bigrat_destroy(&coefficients[--initialized_coefficients]);
    }
    return status;
}

phy_status phy_cas_nsolve(phy_cas *cas, phy_ir_ref equation,
                          phy_ir_ref variable, unsigned decimal_digits,
                          phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL || equation == PHY_IR_NULL ||
        phy_ir_kind_of(cas->ir, variable) != PHY_IR_SYMBOL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    if (decimal_digits == 0u) {
        decimal_digits = BALL_DEFAULT_DECIMAL_DIGITS;
    }
    if (decimal_digits > BALL_MAX_DECIMAL_DIGITS) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (phy_ir_kind_of(cas->ir, equation) != PHY_IR_EQUATION) {
        return PHY_ERR_TYPE;
    }
    phy_cas_begin(cas);
    phy_ir_ref negative = PHY_IR_NULL;
    phy_status status = phy_cas_neg_node(
        cas, phy_ir_child(cas->ir, equation, 1u), &negative);
    phy_ir_ref difference = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {
            phy_ir_child(cas->ir, equation, 0u), negative};
        status = phy_cas_add_node(cas, terms, 2u, &difference);
    }
    phy_ir_ref numerator = PHY_IR_NULL;
    phy_ir_ref denominator = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_rational_form(
            cas, difference, &numerator, &denominator);
    }
    phy_ir_ref coefficients = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_sparse_univariate_squarefree_coefficients(
            cas, numerator, variable, &coefficients);
    }
    const size_t coefficient_count = status == PHY_OK
        ? phy_ir_child_count(cas->ir, coefficients) : 0u;
    if (status == PHY_OK &&
        (coefficient_count < 2u ||
         coefficient_count > BALL_MAX_POLYNOMIAL_DEGREE + 1u)) {
        status = PHY_ERR_TERM_LIMIT;
    }
    phy_exact_context *exact = NULL;
    if (status == PHY_OK) {
        exact = ball_exact_operation_context(cas);
        if (exact == NULL) {
            status = PHY_ERR_OUT_OF_MEMORY;
        }
    }
    bool complex_quadratic = false;
    if (status == PHY_OK) {
        status = nsolve_complex_quadratic(
            cas, exact, coefficients, denominator, variable,
            decimal_digits, out_ref, &complex_quadratic);
    }
    if (complex_quadratic) {
        if (exact != NULL) phy_exact_context_destroy(exact);
        return status;
    }
    const char *coefficient_text[BALL_MAX_POLYNOMIAL_DEGREE + 1u];
    char *coefficient_storage = NULL;
    size_t coefficient_storage_bytes = 0u;
    if (status == PHY_OK) {
        status = integer_coefficient_strings(
            cas, exact, coefficients, coefficient_text, coefficient_count,
            &coefficient_storage, &coefficient_storage_bytes);
    }
    phy_algebraic_context *algebraic = NULL;
    phy_real_algebraic *roots[BALL_MAX_POLYNOMIAL_DEGREE];
    memset(roots, 0, sizeof roots);
    size_t root_count = 0u;
    if (status == PHY_OK) {
        phy_algebraic_limits limits;
        phy_algebraic_limits_defaults(&limits);
        limits.max_degree = BALL_MAX_POLYNOMIAL_DEGREE;
        limits.max_refinements = decimal_digits * 4u + 32u;
        algebraic = phy_algebraic_context_create(&limits);
        if (algebraic == NULL) {
            status = PHY_ERR_OUT_OF_MEMORY;
        }
    }
    if (status == PHY_OK) {
        status = phy_algebraic_isolate_real_roots(
            algebraic, coefficient_text, coefficient_count, roots,
            BALL_MAX_POLYNOMIAL_DEGREE, &root_count);
    }

    const size_t polynomial_degree = coefficient_count - 1u;
    const bool has_complex_roots =
        status == PHY_OK && root_count < polynomial_degree;
    if (has_complex_roots) {
        status = nsolve_general_complex(
            cas, exact, coefficients, denominator, variable,
            decimal_digits, out_ref);
        if (algebraic != NULL) {
            phy_algebraic_context_destroy(algebraic);
        }
        if (coefficient_storage != NULL) {
            phy_cas_temp_free(cas, coefficient_storage,
                              coefficient_storage_bytes);
        }
        if (exact != NULL) phy_exact_context_destroy(exact);
        return status;
    }

    phy_ir_ref branches[BALL_MAX_POLYNOMIAL_DEGREE];
    size_t branch_count = 0u;
    const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
    const phy_ir_symbol rule = phy_ir_intern(cas->ir, "Rule");
    for (size_t index = 0u; status == PHY_OK && index < root_count; ++index) {
        phy_real_ball root_ball;
        phy_real_ball denominator_ball;
        memset(&root_ball, 0, sizeof root_ball);
        memset(&denominator_ball, 0, sizeof denominator_ball);
        status = phy_real_ball_init(exact, &root_ball);
        if (status == PHY_OK) {
            status = phy_real_ball_init(exact, &denominator_ball);
        }
        bool precise = false;
        for (unsigned attempt = 0u;
             status == PHY_OK && attempt < BALL_PRECISION_ATTEMPTS &&
             !precise; ++attempt) {
            status = algebraic_value_ball(
                cas, roots[index], exact, decimal_digits, &root_ball);
            if (status == PHY_OK) {
                status = ball_meets_precision(
                    &root_ball, decimal_digits, &precise);
            }
        }
        if (status == PHY_OK && !precise) status = PHY_ERR_TERM_LIMIT;
        if (status == PHY_OK) {
            status = eval_ball_node(
                cas, exact, denominator, variable, &root_ball,
                decimal_digits * 4u + 8u, &denominator_ball);
        }
        bool denominator_contains_zero = true;
        if (status == PHY_OK) {
            status = phy_real_ball_contains_zero_checked(
                &denominator_ball, &denominator_contains_zero);
        }
        if (status == PHY_OK && denominator_contains_zero) {
            status = PHY_ERR_UNSUPPORTED;
        }
        phy_ir_ref around = PHY_IR_NULL;
        if (status == PHY_OK) {
            status = publish_ball(
                cas, &root_ball, decimal_digits, &around);
        }
        if (status == PHY_OK) {
            const phy_ir_ref rule_arguments[2] = {variable, around};
            const phy_ir_ref one_rule =
                phy_ir_function(cas->ir, rule, rule_arguments, 2u);
            branches[branch_count] =
                one_rule == PHY_IR_NULL
                    ? PHY_IR_NULL
                    : phy_ir_function(cas->ir, list, &one_rule, 1u);
            if (branches[branch_count] == PHY_IR_NULL) {
                status = phy_cas_ir_failure(cas);
            } else {
                branch_count++;
            }
        }
        phy_real_ball_destroy(&denominator_ball);
        phy_real_ball_destroy(&root_ball);
    }
    if (status == PHY_OK) {
        *out_ref = phy_ir_function(
            cas->ir, list, branches, branch_count);
        if (*out_ref == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
        }
    }
    if (algebraic != NULL) {
        phy_algebraic_context_destroy(algebraic);
    }
    if (coefficient_storage != NULL) {
        phy_cas_temp_free(
            cas, coefficient_storage, coefficient_storage_bytes);
    }
    if (exact != NULL) {
        phy_exact_context_destroy(exact);
    }
    return status;
}
