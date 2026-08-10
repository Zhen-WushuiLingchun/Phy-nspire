/*
 * Phy-nspire — exact bounded discrete special functions.
 *
 * Factorial, Pochhammer and Binomial are small functions with unusually large
 * consequences: series coefficients, perturbative combinatorics and diagram
 * multiplicities all depend on them.  They therefore use the same native
 * arbitrary-precision rational domain as the rest of the CAS.  No floating
 * approximation, host integer truncation or libm fallback is permitted.
 *
 * Exact arguments use a transient bounded exact context and publish one
 * canonical IR atom.  Symbolic finite products are built only below a tighter
 * term ceiling, so a calculator never allocates an accidental thousand-factor
 * expression merely because the notation was compact.
 */
#include <limits.h>
#include <string.h>

#include "cas_internal.h"

#define PHY_CAS_FACTORIAL_MAX 512u
#define PHY_CAS_EXACT_PRODUCT_MAX 512u
#define PHY_CAS_SYMBOLIC_PRODUCT_MAX 64u
#define PHY_CAS_BERNOULLI_MAX 64u
#define PHY_CAS_HARMONIC_MAX 4096u
#define PHY_CAS_RECURRENCE_MAX 64u

static void destroy_bigrat_if_initialized(phy_bigrat *value)
{
    if (phy_bigint_is_initialized(phy_bigrat_numerator(value))) {
        phy_bigrat_destroy(value);
    }
}

static phy_status exact_rising(
    phy_cas *cas, phy_ir_ref start, int64_t count, phy_ir_ref *out_ref)
{
    const uint64_t magnitude = count < 0
                                   ? (uint64_t)(-(count + 1)) + 1u
                                   : (uint64_t)count;
    if (magnitude > PHY_CAS_EXACT_PRODUCT_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_status status = phy_cas_charge(cas, (uint32_t)magnitude);
    if (status != PHY_OK) {
        return status;
    }

    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_bigrat current;
    phy_bigrat result;
    phy_bigrat one;
    memset(&current, 0, sizeof current);
    memset(&result, 0, sizeof result);
    memset(&one, 0, sizeof one);

    status = phy_bigrat_init(exact, &current);
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &result);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &one);
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_load_ref(cas, exact, start, &current);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&result, 1, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&one, 1, 1);
    }

    if (status == PHY_OK && count < 0) {
        status = phy_bigrat_subtract(&current, &one, &current);
    }
    for (uint64_t index = 0u;
         index < magnitude && status == PHY_OK; ++index) {
        status = phy_bigrat_multiply(&result, &current, &result);
        if (status == PHY_OK && index + 1u < magnitude) {
            status = count >= 0
                         ? phy_bigrat_add(&current, &one, &current)
                         : phy_bigrat_subtract(&current, &one, &current);
        }
    }
    if (status == PHY_OK && count < 0) {
        status = phy_bigrat_reciprocal(&result, &result);
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_publish_bigrat(cas, &result, out_ref);
    }

    destroy_bigrat_if_initialized(&one);
    destroy_bigrat_if_initialized(&result);
    destroy_bigrat_if_initialized(&current);
    phy_exact_context_destroy(exact);
    return status;
}

static phy_status exact_binomial(
    phy_cas *cas, phy_ir_ref upper, uint32_t lower, phy_ir_ref *out_ref)
{
    if (lower > PHY_CAS_EXACT_PRODUCT_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_status status = phy_cas_charge(cas, lower);
    if (status != PHY_OK) {
        return status;
    }

    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_bigrat current;
    phy_bigrat result;
    phy_bigrat one;
    phy_bigrat divisor;
    memset(&current, 0, sizeof current);
    memset(&result, 0, sizeof result);
    memset(&one, 0, sizeof one);
    memset(&divisor, 0, sizeof divisor);

    status = phy_bigrat_init(exact, &current);
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &result);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &one);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &divisor);
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_load_ref(cas, exact, upper, &current);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&result, 1, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&one, 1, 1);
    }

    for (uint32_t index = 1u; index <= lower && status == PHY_OK; ++index) {
        status = phy_bigrat_multiply(&result, &current, &result);
        if (status == PHY_OK) {
            status = phy_bigrat_set_i64(&divisor, (int64_t)index, 1);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_divide(&result, &divisor, &result);
        }
        if (status == PHY_OK && index < lower) {
            status = phy_bigrat_subtract(&current, &one, &current);
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_publish_bigrat(cas, &result, out_ref);
    }

    destroy_bigrat_if_initialized(&divisor);
    destroy_bigrat_if_initialized(&one);
    destroy_bigrat_if_initialized(&result);
    destroy_bigrat_if_initialized(&current);
    phy_exact_context_destroy(exact);
    return status;
}

static phy_status shifted_factor(
    phy_cas *cas, phy_ir_ref base, int64_t shift, phy_ir_ref *out_ref)
{
    if (shift == 0) {
        *out_ref = base;
        return PHY_OK;
    }
    phy_ir_ref offset = PHY_IR_NULL;
    phy_status status =
        phy_cas_number_node(cas, (phy_cas_rat){shift, 1}, &offset);
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {base, offset};
        status = phy_cas_add_node(cas, terms, 2u, out_ref);
    }
    return status;
}

static phy_status divide_symbolic(
    phy_cas *cas, phy_ir_ref numerator, phy_ir_ref denominator,
    phy_ir_ref *out_ref)
{
    if (phy_cas_is_integer(cas, denominator, 0)) {
        return PHY_ERR_DOMAIN;
    }
    phy_ir_ref reciprocal = PHY_IR_NULL;
    phy_status status =
        phy_cas_pow_node(cas, denominator, cas->minus_one, &reciprocal);
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {numerator, reciprocal};
        status = phy_cas_mul_node(cas, factors, 2u, out_ref);
    }
    return status;
}

static phy_status symbolic_rising(
    phy_cas *cas, phy_ir_ref start, int64_t count, phy_ir_ref *out_ref)
{
    const uint64_t magnitude = count < 0
                                   ? (uint64_t)(-(count + 1)) + 1u
                                   : (uint64_t)count;
    if (magnitude > PHY_CAS_SYMBOLIC_PRODUCT_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_status status = phy_cas_charge(cas, (uint32_t)magnitude);
    phy_ir_ref product = cas->one;
    for (uint64_t index = 0u;
         index < magnitude && status == PHY_OK; ++index) {
        const int64_t shift = count >= 0
                                  ? (int64_t)index
                                  : -(int64_t)(index + 1u);
        phy_ir_ref factor = PHY_IR_NULL;
        status = shifted_factor(cas, start, shift, &factor);
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {product, factor};
            status = phy_cas_mul_node(cas, factors, 2u, &product);
        }
    }
    if (status == PHY_OK && count < 0) {
        status = divide_symbolic(cas, cas->one, product, &product);
    }
    if (status == PHY_OK) {
        *out_ref = product;
    }
    return status;
}

static phy_status symbolic_binomial(
    phy_cas *cas, phy_ir_ref upper, uint32_t lower, phy_ir_ref *out_ref)
{
    if (lower > PHY_CAS_SYMBOLIC_PRODUCT_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_status status = phy_cas_charge(cas, lower);
    phy_ir_ref numerator = cas->one;
    for (uint32_t index = 0u; index < lower && status == PHY_OK; ++index) {
        phy_ir_ref factor = PHY_IR_NULL;
        status = shifted_factor(cas, upper, -(int64_t)index, &factor);
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {numerator, factor};
            status = phy_cas_mul_node(cas, factors, 2u, &numerator);
        }
    }
    phy_ir_ref denominator = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = exact_rising(cas, cas->one, (int64_t)lower, &denominator);
    }
    if (status == PHY_OK) {
        status = divide_symbolic(cas, numerator, denominator, out_ref);
    }
    return status;
}

static phy_status factorial(
    phy_cas *cas, phy_ir_ref argument, phy_ir_ref *out_ref,
    bool *out_matched)
{
    if (phy_ir_kind_of(cas->ir, argument) != PHY_IR_INTEGER) {
        return PHY_OK;
    }
    *out_matched = true;
    const int sign = phy_cas_exact_sign_ref(cas, argument);
    if (sign < 0) {
        return PHY_ERR_DOMAIN;
    }
    int64_t value = 0;
    if (!phy_ir_integer_value(cas->ir, argument, &value) ||
        value < 0 || (uint64_t)value > PHY_CAS_FACTORIAL_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    return exact_rising(cas, cas->one, value, out_ref);
}

static phy_status pochhammer(
    phy_cas *cas, phy_ir_ref start, phy_ir_ref count_ref,
    phy_ir_ref *out_ref, bool *out_matched)
{
    if (phy_ir_kind_of(cas->ir, count_ref) != PHY_IR_INTEGER) {
        return PHY_OK;
    }
    *out_matched = true;
    int64_t count = 0;
    if (!phy_ir_integer_value(cas->ir, count_ref, &count)) {
        return PHY_ERR_TERM_LIMIT;
    }
    return phy_cas_is_exact(cas, start)
               ? exact_rising(cas, start, count, out_ref)
               : symbolic_rising(cas, start, count, out_ref);
}

static phy_status binomial(
    phy_cas *cas, phy_ir_ref upper, phy_ir_ref lower_ref,
    phy_ir_ref *out_ref, bool *out_matched)
{
    if (phy_ir_kind_of(cas->ir, lower_ref) != PHY_IR_INTEGER) {
        return PHY_OK;
    }
    *out_matched = true;
    int64_t lower_i64 = 0;
    if (!phy_ir_integer_value(cas->ir, lower_ref, &lower_i64)) {
        if (phy_cas_exact_sign_ref(cas, lower_ref) < 0) {
            *out_ref = cas->zero;
            return PHY_OK;
        }
        return PHY_ERR_TERM_LIMIT;
    }
    if (lower_i64 < 0) {
        *out_ref = cas->zero;
        return PHY_OK;
    }
    uint64_t lower = (uint64_t)lower_i64;

    if (phy_ir_kind_of(cas->ir, upper) == PHY_IR_INTEGER) {
        int64_t upper_i64 = 0;
        if (phy_ir_integer_value(cas->ir, upper, &upper_i64) &&
            upper_i64 >= 0) {
            if (lower > (uint64_t)upper_i64) {
                *out_ref = cas->zero;
                return PHY_OK;
            }
            const uint64_t reflected = (uint64_t)upper_i64 - lower;
            if (reflected < lower) {
                lower = reflected;
            }
        }
    }
    if (lower > UINT32_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    return phy_cas_is_exact(cas, upper)
               ? exact_binomial(cas, upper, (uint32_t)lower, out_ref)
               : symbolic_binomial(cas, upper, (uint32_t)lower, out_ref);
}

static phy_status bernoulli(
    phy_cas *cas, phy_ir_ref argument, phy_ir_ref *out_ref,
    bool *out_matched)
{
    if (phy_ir_kind_of(cas->ir, argument) != PHY_IR_INTEGER) {
        return PHY_OK;
    }
    *out_matched = true;
    int64_t order = 0;
    if (!phy_ir_integer_value(cas->ir, argument, &order)) {
        return phy_cas_exact_sign_ref(cas, argument) < 0
                   ? PHY_ERR_DOMAIN
                   : PHY_ERR_TERM_LIMIT;
    }
    if (order < 0) {
        return PHY_ERR_DOMAIN;
    }
    if ((uint64_t)order > PHY_CAS_BERNOULLI_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (order > 1 && (order & 1) != 0) {
        *out_ref = cas->zero;
        return PHY_OK;
    }
    phy_ir_ref values[PHY_CAS_BERNOULLI_MAX + 1u];
    values[0] = cas->one;
    for (int64_t m = 1; m <= order; ++m) {
        phy_ir_ref sum = cas->zero;
        phy_ir_ref upper = phy_ir_integer(cas->ir, m + 1);
        if (upper == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
        for (uint32_t k = 0u; k < (uint32_t)m; ++k) {
            phy_ir_ref coefficient = PHY_IR_NULL;
            phy_ir_ref term = PHY_IR_NULL;
            phy_status status = exact_binomial(
                cas, upper, k, &coefficient);
            if (status == PHY_OK) {
                status = phy_cas_exact_mul_refs(
                    cas, coefficient, values[k], &term);
            }
            if (status == PHY_OK) {
                status = phy_cas_exact_add_refs(cas, sum, term, &sum);
            }
            if (status != PHY_OK) {
                return status;
            }
        }
        phy_ir_ref negative = PHY_IR_NULL;
        phy_ir_ref divisor = phy_ir_integer(cas->ir, m + 1);
        phy_status status = divisor == PHY_IR_NULL
                                ? phy_cas_ir_failure(cas)
                                : phy_cas_exact_sub_refs(
                                      cas, cas->zero, sum, &negative);
        if (status == PHY_OK) {
            status = phy_cas_exact_div_refs(
                cas, negative, divisor, &values[m]);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    *out_ref = values[order];
    return PHY_OK;
}

static phy_status exact_harmonic(phy_cas *cas, uint32_t order,
                                 phy_ir_ref *out_ref)
{
    if (order > PHY_CAS_HARMONIC_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_status status = phy_cas_charge(cas, order);
    if (status != PHY_OK) {
        return status;
    }
    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_bigrat sum;
    phy_bigrat term;
    memset(&sum, 0, sizeof sum);
    memset(&term, 0, sizeof term);
    status = phy_bigrat_init(exact, &sum);
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &term);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&sum, 0, 1);
    }
    for (uint32_t denominator = 1u;
         status == PHY_OK && denominator <= order; ++denominator) {
        status = phy_bigrat_set_i64(
            &term, 1, (int64_t)denominator);
        if (status == PHY_OK) {
            status = phy_bigrat_add(&sum, &term, &sum);
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_publish_bigrat(cas, &sum, out_ref);
    }
    destroy_bigrat_if_initialized(&term);
    destroy_bigrat_if_initialized(&sum);
    phy_exact_context_destroy(exact);
    return status;
}

static phy_status harmonic(
    phy_cas *cas, phy_ir_ref argument, phy_ir_ref *out_ref,
    bool *out_matched)
{
    if (phy_ir_kind_of(cas->ir, argument) != PHY_IR_INTEGER) {
        return PHY_OK;
    }
    *out_matched = true;
    int64_t order = 0;
    if (!phy_ir_integer_value(cas->ir, argument, &order)) {
        return phy_cas_exact_sign_ref(cas, argument) < 0
                   ? PHY_ERR_DOMAIN
                   : PHY_ERR_TERM_LIMIT;
    }
    if (order < 0) {
        return PHY_ERR_DOMAIN;
    }
    if ((uint64_t)order > UINT32_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    return exact_harmonic(cas, (uint32_t)order, out_ref);
}

static phy_status split_integer_shift(phy_cas *cas, phy_ir_ref argument,
                                      phy_ir_ref *out_base,
                                      int64_t *out_shift,
                                      bool *out_matched)
{
    *out_matched = false;
    if (phy_ir_kind_of(cas->ir, argument) != PHY_IR_ADD) {
        return PHY_OK;
    }
    const size_t count = phy_ir_child_count(cas->ir, argument);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    phy_status status = phy_cas_scratch_alloc(cas, count, &offset);
    int64_t shift = 0;
    size_t base_count = 0u;
    for (size_t index = 0u; status == PHY_OK && index < count; ++index) {
        const phy_ir_ref child = phy_ir_child(cas->ir, argument, index);
        if (phy_ir_kind_of(cas->ir, child) == PHY_IR_INTEGER) {
            int64_t value = 0;
            if (!phy_ir_integer_value(cas->ir, child, &value)) {
                status = PHY_ERR_TERM_LIMIT;
                break;
            }
            if ((value > 0 && shift > INT64_MAX - value) ||
                (value < 0 && shift < INT64_MIN - value)) {
                status = PHY_ERR_TERM_LIMIT;
                break;
            }
            shift += value;
        } else {
            phy_cas_scratch_at(cas, offset)[base_count++] = child;
        }
    }
    if (status == PHY_OK && shift != 0 && base_count != 0u) {
        status = phy_cas_add_at(cas, offset, base_count, out_base);
        if (status == PHY_OK) {
            *out_shift = shift;
            *out_matched = true;
        }
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

static phy_status logarithm_of_two(phy_cas *cas, phy_ir_ref *out_ref)
{
    const phy_ir_ref two = phy_ir_integer(cas->ir, 2);
    if (two == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = phy_ir_function(
        cas->ir, cas->functions[PHY_CAS_FN_LOG], &two, 1u);
    return *out_ref == PHY_IR_NULL ? phy_cas_ir_failure(cas) : PHY_OK;
}

static phy_status digamma_half_integer(phy_cas *cas, int64_t numerator,
                                       phy_ir_ref *out_ref)
{
    if (numerator <= 0 || (numerator & 1) == 0) {
        return PHY_ERR_UNSUPPORTED;
    }
    const uint64_t n = (uint64_t)(numerator - 1) / 2u;
    if (n > PHY_CAS_HARMONIC_MAX / 2u) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_ir_ref h_twice = PHY_IR_NULL;
    phy_ir_ref h_once = PHY_IR_NULL;
    phy_status status = exact_harmonic(cas, (uint32_t)(2u * n), &h_twice);
    if (status == PHY_OK) {
        status = exact_harmonic(cas, (uint32_t)n, &h_once);
    }
    phy_ir_ref twice_h = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref two = phy_ir_integer(cas->ir, 2);
        status = two == PHY_IR_NULL
                     ? phy_cas_ir_failure(cas)
                     : phy_cas_exact_mul_refs(cas, two, h_twice, &twice_h);
    }
    phy_ir_ref rational = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_exact_sub_refs(cas, twice_h, h_once, &rational);
    }
    phy_ir_ref log_two = PHY_IR_NULL;
    phy_ir_ref negative_gamma = PHY_IR_NULL;
    phy_ir_ref negative_log = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = logarithm_of_two(cas, &log_two);
    }
    if (status == PHY_OK) {
        status = phy_cas_neg_node(
            cas, cas->constant_euler_gamma, &negative_gamma);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {
            phy_ir_integer(cas->ir, -2), log_two};
        status = factors[0] == PHY_IR_NULL
                     ? phy_cas_ir_failure(cas)
                     : phy_cas_mul_node(cas, factors, 2u, &negative_log);
    }
    if (status == PHY_OK) {
        const phy_ir_ref terms[3] = {
            rational, negative_gamma, negative_log};
        status = phy_cas_add_node(cas, terms, 3u, out_ref);
    }
    return status;
}

static phy_status recurrence_sum(phy_cas *cas, phy_ir_ref base,
                                 int64_t shift, phy_ir_ref *out_ref)
{
    const uint64_t magnitude = shift < 0
                                   ? (uint64_t)(-(shift + 1)) + 1u
                                   : (uint64_t)shift;
    if (magnitude > PHY_CAS_RECURRENCE_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_ir_ref sum = cas->zero;
    for (uint64_t index = 0u; index < magnitude; ++index) {
        const int64_t offset = shift > 0
                                   ? (int64_t)index
                                   : -(int64_t)(index + 1u);
        phy_ir_ref denominator = PHY_IR_NULL;
        phy_status status = shifted_factor(
            cas, base, offset, &denominator);
        phy_ir_ref reciprocal = PHY_IR_NULL;
        if (status == PHY_OK) {
            status = phy_cas_pow_node(
                cas, denominator, cas->minus_one, &reciprocal);
        }
        if (status == PHY_OK && shift < 0) {
            status = phy_cas_neg_node(cas, reciprocal, &reciprocal);
        }
        if (status == PHY_OK) {
            const phy_ir_ref terms[2] = {sum, reciprocal};
            status = phy_cas_add_node(cas, terms, 2u, &sum);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    *out_ref = sum;
    return PHY_OK;
}

static phy_status digamma(
    phy_cas *cas, phy_ir_ref argument, phy_ir_ref *out_ref,
    bool *out_matched)
{
    if (phy_ir_kind_of(cas->ir, argument) == PHY_IR_INTEGER) {
        int64_t value = 0;
        if (!phy_ir_integer_value(cas->ir, argument, &value)) {
            if (phy_cas_exact_sign_ref(cas, argument) > 0) {
                return PHY_ERR_TERM_LIMIT;
            }
            return PHY_OK; /* the descriptor reports the pole */
        }
        if (value <= 0) {
            return PHY_OK;
        }
        phy_ir_ref harmonic_value = PHY_IR_NULL;
        phy_status status = exact_harmonic(
            cas, (uint32_t)(value - 1), &harmonic_value);
        phy_ir_ref negative_gamma = PHY_IR_NULL;
        if (status == PHY_OK) {
            status = phy_cas_neg_node(
                cas, cas->constant_euler_gamma, &negative_gamma);
        }
        if (status == PHY_OK) {
            const phy_ir_ref terms[2] = {
                harmonic_value, negative_gamma};
            status = phy_cas_add_node(cas, terms, 2u, out_ref);
        }
        if (status == PHY_OK) {
            *out_matched = true;
        }
        return status;
    }
    phy_cas_rat rational;
    if (phy_cas_exact_value(cas, argument, &rational) &&
        rational.den == 2 && rational.num > 0 &&
        (rational.num & 1) != 0) {
        phy_status status = digamma_half_integer(
            cas, rational.num, out_ref);
        if (status == PHY_OK) {
            *out_matched = true;
        }
        return status;
    }
    phy_ir_ref base = PHY_IR_NULL;
    int64_t shift = 0;
    bool shifted = false;
    phy_status status = split_integer_shift(
        cas, argument, &base, &shift, &shifted);
    if (status != PHY_OK || !shifted) {
        return status;
    }
    const uint64_t magnitude = shift < 0
                                   ? (uint64_t)(-(shift + 1)) + 1u
                                   : (uint64_t)shift;
    if (magnitude > PHY_CAS_RECURRENCE_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_ir_ref base_call = phy_ir_function(
        cas->ir, cas->functions[PHY_CAS_FN_DIGAMMA], &base, 1u);
    phy_ir_ref correction = PHY_IR_NULL;
    if (base_call == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    status = recurrence_sum(cas, base, shift, &correction);
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {base_call, correction};
        status = phy_cas_add_node(cas, terms, 2u, out_ref);
    }
    if (status == PHY_OK) {
        *out_matched = true;
    }
    return status;
}

static phy_status gamma_recurrence(
    phy_cas *cas, phy_ir_ref argument, phy_ir_ref *out_ref,
    bool *out_matched)
{
    int64_t integer = 0;
    if (phy_ir_kind_of(cas->ir, argument) == PHY_IR_INTEGER &&
        phy_ir_integer_value(cas->ir, argument, &integer) && integer > 0) {
        if ((uint64_t)(integer - 1) > PHY_CAS_EXACT_PRODUCT_MAX) {
            return PHY_ERR_TERM_LIMIT;
        }
        phy_status status = exact_rising(
            cas, cas->one, integer - 1, out_ref);
        if (status == PHY_OK) {
            *out_matched = true;
        }
        return status;
    }
    phy_cas_rat rational;
    if (phy_cas_exact_value(cas, argument, &rational) &&
        rational.den == 2 && rational.num > 0 &&
        (rational.num & 1) != 0) {
        const int64_t count = (rational.num - 1) / 2;
        phy_ir_ref half = PHY_IR_NULL;
        phy_ir_ref coefficient = PHY_IR_NULL;
        phy_ir_ref sqrt_pi = PHY_IR_NULL;
        phy_status status = phy_cas_number_node(
            cas, (phy_cas_rat){1, 2}, &half);
        if (status == PHY_OK) {
            status = exact_rising(cas, half, count, &coefficient);
        }
        if (status == PHY_OK) {
            status = phy_cas_pow_node(
                cas, cas->constant_pi, half, &sqrt_pi);
        }
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {coefficient, sqrt_pi};
            status = phy_cas_mul_node(cas, factors, 2u, out_ref);
        }
        if (status == PHY_OK) {
            *out_matched = true;
        }
        return status;
    }

    phy_ir_ref base = PHY_IR_NULL;
    int64_t shift = 0;
    bool shifted = false;
    phy_status status = split_integer_shift(
        cas, argument, &base, &shift, &shifted);
    if (status != PHY_OK || !shifted) {
        return status;
    }
    const uint64_t magnitude = shift < 0
                                   ? (uint64_t)(-(shift + 1)) + 1u
                                   : (uint64_t)shift;
    if (magnitude > PHY_CAS_RECURRENCE_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_ir_ref base_gamma = phy_ir_function(
        cas->ir, cas->functions[PHY_CAS_FN_GAMMA], &base, 1u);
    if (base_gamma == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    if (shift > 0) {
        phy_ir_ref rising = PHY_IR_NULL;
        status = symbolic_rising(cas, base, shift, &rising);
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {base_gamma, rising};
            status = phy_cas_mul_node(cas, factors, 2u, out_ref);
        }
    } else {
        phy_ir_ref denominator = PHY_IR_NULL;
        status = symbolic_rising(cas, argument, -shift, &denominator);
        if (status == PHY_OK) {
            status = divide_symbolic(
                cas, base_gamma, denominator, out_ref);
        }
    }
    if (status == PHY_OK) {
        *out_matched = true;
    }
    return status;
}

static phy_status pochhammer_recurrence(
    phy_cas *cas, phy_ir_ref start, phy_ir_ref count,
    phy_ir_ref *out_ref, bool *out_matched)
{
    phy_ir_ref base_count = PHY_IR_NULL;
    int64_t shift = 0;
    bool shifted = false;
    phy_status status = split_integer_shift(
        cas, count, &base_count, &shift, &shifted);
    if (status != PHY_OK || !shifted) {
        return status;
    }
    const uint64_t magnitude = shift < 0
                                   ? (uint64_t)(-(shift + 1)) + 1u
                                   : (uint64_t)shift;
    if (magnitude > PHY_CAS_RECURRENCE_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    const phy_ir_ref base_arguments[2] = {start, base_count};
    phy_ir_ref base_call = phy_ir_function(
        cas->ir, cas->functions[PHY_CAS_FN_POCHHAMMER],
        base_arguments, 2u);
    if (base_call == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    phy_ir_ref recurrence_start = PHY_IR_NULL;
    if (shift > 0) {
        const phy_ir_ref terms[2] = {start, base_count};
        status = phy_cas_add_node(
            cas, terms, 2u, &recurrence_start);
    } else {
        const phy_ir_ref terms[2] = {start, count};
        status = phy_cas_add_node(
            cas, terms, 2u, &recurrence_start);
    }
    phy_ir_ref finite = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = symbolic_rising(
            cas, recurrence_start, (int64_t)magnitude, &finite);
    }
    if (status == PHY_OK && shift > 0) {
        const phy_ir_ref factors[2] = {base_call, finite};
        status = phy_cas_mul_node(cas, factors, 2u, out_ref);
    } else if (status == PHY_OK) {
        status = divide_symbolic(cas, base_call, finite, out_ref);
    }
    if (status == PHY_OK) {
        *out_matched = true;
    }
    return status;
}

phy_status phy_cas_discrete_function(
    phy_cas *cas, phy_cas_function function,
    const phy_ir_ref *arguments, size_t count,
    phy_ir_ref *out_ref, bool *out_matched)
{
    if (cas == NULL || arguments == NULL || out_ref == NULL ||
        out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;

    /* Copy before any nested builder can grow and move the CAS scratch arena. */
    const phy_ir_ref first = count > 0u ? arguments[0] : PHY_IR_NULL;
    const phy_ir_ref second = count > 1u ? arguments[1] : PHY_IR_NULL;
    if (function == PHY_CAS_FN_FACTORIAL && count == 1u) {
        return factorial(cas, first, out_ref, out_matched);
    }
    if (function == PHY_CAS_FN_POCHHAMMER && count == 2u) {
        phy_status status =
            pochhammer(cas, first, second, out_ref, out_matched);
        if (status != PHY_OK || *out_matched) {
            return status;
        }
        return pochhammer_recurrence(
            cas, first, second, out_ref, out_matched);
    }
    if (function == PHY_CAS_FN_BINOMIAL && count == 2u) {
        return binomial(cas, first, second, out_ref, out_matched);
    }
    if (function == PHY_CAS_FN_BERNOULLI && count == 1u) {
        return bernoulli(cas, first, out_ref, out_matched);
    }
    if (function == PHY_CAS_FN_HARMONIC && count == 1u) {
        return harmonic(cas, first, out_ref, out_matched);
    }
    if (function == PHY_CAS_FN_DIGAMMA && count == 1u) {
        return digamma(cas, first, out_ref, out_matched);
    }
    if (function == PHY_CAS_FN_GAMMA && count == 1u) {
        return gamma_recurrence(cas, first, out_ref, out_matched);
    }
    return PHY_OK;
}
