#include "cas_internal.h"

#include <string.h>

#include "phy/algebraic.h"
#include "phy/ball.h"
#include "sparse_poly.h"

#define BALL_MAX_DECIMAL_DIGITS 36u
#define BALL_DEFAULT_DECIMAL_DIGITS 16u
#define BALL_MAX_POLYNOMIAL_DEGREE 48u

static const char kDecimalScale40[] =
    "10000000000000000000000000000000000000000";

static void destroy_bigrat(phy_bigrat *value)
{
    if (phy_bigint_is_initialized(phy_bigrat_numerator(value))) {
        phy_bigrat_destroy(value);
    }
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
    return PHY_ERR_UNSUPPORTED;
}

static phy_status publish_ball(phy_cas *cas, const phy_real_ball *ball,
                               phy_ir_ref *out_ref)
{
    phy_ir_ref arguments[2] = {PHY_IR_NULL, PHY_IR_NULL};
    phy_status status = phy_cas_exact_publish_bigrat(
        cas, &ball->midpoint, &arguments[0]);
    if (status == PHY_OK) {
        status = phy_cas_exact_publish_bigrat(
            cas, &ball->radius, &arguments[1]);
    }
    if (status == PHY_OK) {
        const phy_ir_symbol around = phy_ir_intern(cas->ir, "Around");
        *out_ref = phy_ir_function(cas->ir, around, arguments, 2u);
        if (*out_ref == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
        }
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
    phy_cas_begin(cas);
    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_real_ball value;
    memset(&value, 0, sizeof value);
    phy_status status = phy_real_ball_init(exact, &value);
    if (status == PHY_OK) {
        status = eval_ball_node(
            cas, exact, expression, PHY_IR_NULL, NULL,
            decimal_digits * 4u + 8u, &value);
    }
    if (status == PHY_OK) {
        status = publish_ball(cas, &value, out_ref);
    }
    phy_real_ball_destroy(&value);
    phy_exact_context_destroy(exact);
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
        exact = phy_cas_exact_operation_context(cas);
        if (exact == NULL) {
            status = PHY_ERR_OUT_OF_MEMORY;
        }
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
        if (status == PHY_OK) {
            status = algebraic_value_ball(
                cas, roots[index], exact, decimal_digits, &root_ball);
        }
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
            status = publish_ball(cas, &root_ball, &around);
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
