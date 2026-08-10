#include "series_internal.h"

#include <limits.h>
#include <string.h>

static bool exponent_bounds(int valuation, int order)
{
    return valuation >= PHY_SERIES_MIN_EXPONENT &&
           order <= PHY_SERIES_MAX_EXPONENT &&
           valuation <= order &&
           (size_t)(order - valuation) <= PHY_SERIES_MAX_TERMS;
}

static bool exact_zero(const phy_cas *cas, phy_ir_ref coefficient)
{
    return phy_cas_is_exact(cas, coefficient) &&
           phy_cas_exact_sign_ref(cas, coefficient) == 0;
}

static void trim(phy_cas *cas, phy_series *series)
{
    size_t leading = 0u;
    while (leading < series->count &&
           exact_zero(cas, series->coefficients[leading])) {
        leading++;
    }
    if (leading == series->count) {
        series->valuation = series->order;
        series->count = 0u;
        return;
    }
    if (leading > 0u) {
        memmove(series->coefficients,
                series->coefficients + leading,
                (series->count - leading) *
                    sizeof series->coefficients[0]);
        series->valuation += (int)leading;
        series->count -= leading;
    }
}

static phy_status same_ring(const phy_cas *cas, const phy_series *left,
                            const phy_series *right)
{
    phy_status status = phy_series_validate(cas, left);
    if (status == PHY_OK) {
        status = phy_series_validate(cas, right);
    }
    if (status != PHY_OK) {
        return status;
    }
    return left->variable == right->variable &&
                   left->center == right->center
               ? PHY_OK
               : PHY_ERR_TYPE;
}

phy_status phy_series_set(phy_cas *cas, phy_ir_ref variable,
                          phy_ir_ref center, int valuation, int order,
                          const phy_ir_ref *coefficients, size_t count,
                          phy_series *out_series)
{
    if (cas == NULL || out_series == NULL ||
        phy_ir_kind_of(cas->ir, variable) != PHY_IR_SYMBOL ||
        !phy_cas_is_exact(cas, center) ||
        !exponent_bounds(valuation, order) ||
        count != (size_t)(order - valuation) ||
        (count > 0u && coefficients == NULL)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_series candidate;
    memset(&candidate, 0, sizeof candidate);
    candidate.variable = variable;
    candidate.center = center;
    candidate.valuation = valuation;
    candidate.order = order;
    candidate.count = count;
    for (size_t index = 0u; index < count; ++index) {
        if (!phy_cas_is_exact(cas, coefficients[index])) {
            return PHY_ERR_TYPE;
        }
        candidate.coefficients[index] = coefficients[index];
    }
    trim(cas, &candidate);
    *out_series = candidate;
    return PHY_OK;
}

phy_status phy_series_zero(phy_cas *cas, phy_ir_ref variable,
                           phy_ir_ref center, int order,
                           phy_series *out_series)
{
    return phy_series_set(cas, variable, center, order, order, NULL, 0u,
                          out_series);
}

phy_status phy_series_constant(phy_cas *cas, phy_ir_ref variable,
                               phy_ir_ref center, int order,
                               phy_ir_ref coefficient,
                               phy_series *out_series)
{
    if (order <= 0) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref coefficients[PHY_SERIES_MAX_TERMS];
    const size_t count = (size_t)order;
    if (count > PHY_SERIES_MAX_TERMS) {
        return PHY_ERR_TERM_LIMIT;
    }
    coefficients[0] = coefficient;
    for (size_t index = 1u; index < count; ++index) {
        coefficients[index] = cas != NULL ? cas->zero : PHY_IR_NULL;
    }
    return phy_series_set(cas, variable, center, 0, order, coefficients,
                          count, out_series);
}

phy_status phy_series_validate(const phy_cas *cas,
                               const phy_series *series)
{
    if (cas == NULL || series == NULL ||
        phy_ir_kind_of(cas->ir, series->variable) != PHY_IR_SYMBOL ||
        !phy_cas_is_exact(cas, series->center) ||
        !exponent_bounds(series->valuation, series->order) ||
        series->count !=
            (size_t)(series->order - series->valuation)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t index = 0u; index < series->count; ++index) {
        if (!phy_cas_is_exact(cas, series->coefficients[index])) {
            return PHY_ERR_TYPE;
        }
    }
    if (series->count > 0u &&
        exact_zero(cas, series->coefficients[0])) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    return PHY_OK;
}

bool phy_series_is_zero(const phy_series *series)
{
    return series != NULL && series->count == 0u &&
           series->valuation == series->order;
}

phy_ir_ref phy_series_coefficient(const phy_cas *cas,
                                  const phy_series *series, int exponent)
{
    if (phy_series_validate(cas, series) != PHY_OK ||
        exponent < series->valuation || exponent >= series->order) {
        return cas != NULL ? cas->zero : PHY_IR_NULL;
    }
    return series->coefficients[
        (size_t)(exponent - series->valuation)];
}

static phy_status add_or_sub(phy_cas *cas, const phy_series *left,
                             const phy_series *right, bool subtract,
                             phy_series *out_series)
{
    phy_status status = same_ring(cas, left, right);
    if (status != PHY_OK || out_series == NULL) {
        return status != PHY_OK ? status : PHY_ERR_INVALID_ARGUMENT;
    }
    const int order =
        left->order < right->order ? left->order : right->order;
    int valuation =
        left->valuation < right->valuation ? left->valuation
                                           : right->valuation;
    if (valuation > order) {
        valuation = order;
    }
    if (!exponent_bounds(valuation, order)) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_series candidate;
    memset(&candidate, 0, sizeof candidate);
    candidate.variable = left->variable;
    candidate.center = left->center;
    candidate.valuation = valuation;
    candidate.order = order;
    candidate.count = (size_t)(order - valuation);
    for (int exponent = valuation; exponent < order; ++exponent) {
        status = phy_cas_step(cas);
        if (status != PHY_OK) {
            return status;
        }
        const phy_ir_ref a =
            phy_series_coefficient(cas, left, exponent);
        const phy_ir_ref b =
            phy_series_coefficient(cas, right, exponent);
        status = subtract
                     ? phy_cas_exact_sub_refs(
                           cas, a, b,
                           &candidate.coefficients[
                               (size_t)(exponent - valuation)])
                     : phy_cas_exact_add_refs(
                           cas, a, b,
                           &candidate.coefficients[
                               (size_t)(exponent - valuation)]);
        if (status != PHY_OK) {
            return status;
        }
    }
    trim(cas, &candidate);
    *out_series = candidate;
    return PHY_OK;
}

phy_status phy_series_add_node(phy_cas *cas, const phy_series *left,
                               const phy_series *right,
                               phy_series *out_series)
{
    return add_or_sub(cas, left, right, false, out_series);
}

phy_status phy_series_sub_node(phy_cas *cas, const phy_series *left,
                               const phy_series *right,
                               phy_series *out_series)
{
    return add_or_sub(cas, left, right, true, out_series);
}

static phy_status multiply_to_order(phy_cas *cas, const phy_series *left,
                                    const phy_series *right, int order,
                                    phy_series *out_series)
{
    phy_status status = same_ring(cas, left, right);
    if (status != PHY_OK || out_series == NULL) {
        return status != PHY_OK ? status : PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_series_is_zero(left) || phy_series_is_zero(right)) {
        return phy_series_zero(cas, left->variable, left->center, order,
                               out_series);
    }
    const int valuation = left->valuation + right->valuation;
    if (order < valuation || !exponent_bounds(valuation, order)) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_series candidate;
    memset(&candidate, 0, sizeof candidate);
    candidate.variable = left->variable;
    candidate.center = left->center;
    candidate.valuation = valuation;
    candidate.order = order;
    candidate.count = (size_t)(order - valuation);
    for (size_t index = 0u; index < candidate.count; ++index) {
        candidate.coefficients[index] = cas->zero;
    }
    for (int a_exp = left->valuation; a_exp < left->order; ++a_exp) {
        for (int b_exp = right->valuation; b_exp < right->order;
             ++b_exp) {
            const int exponent = a_exp + b_exp;
            if (exponent >= order) {
                break;
            }
            status = phy_cas_step(cas);
            if (status != PHY_OK) {
                return status;
            }
            phy_ir_ref product = PHY_IR_NULL;
            phy_ir_ref sum = PHY_IR_NULL;
            status = phy_cas_exact_mul_refs(
                cas, phy_series_coefficient(cas, left, a_exp),
                phy_series_coefficient(cas, right, b_exp), &product);
            if (status == PHY_OK) {
                const size_t position =
                    (size_t)(exponent - valuation);
                status = phy_cas_exact_add_refs(
                    cas, candidate.coefficients[position], product, &sum);
                if (status == PHY_OK) {
                    candidate.coefficients[position] = sum;
                }
            }
            if (status != PHY_OK) {
                return status;
            }
        }
    }
    trim(cas, &candidate);
    *out_series = candidate;
    return PHY_OK;
}

phy_status phy_series_mul_node(phy_cas *cas, const phy_series *left,
                               const phy_series *right,
                               phy_series *out_series)
{
    phy_status status = same_ring(cas, left, right);
    if (status != PHY_OK) {
        return status;
    }
    if (phy_series_is_zero(left) || phy_series_is_zero(right)) {
        const int order =
            left->order < right->order ? left->order : right->order;
        return phy_series_zero(cas, left->variable, left->center, order,
                               out_series);
    }
    const int left_bound = left->order + right->valuation;
    const int right_bound = right->order + left->valuation;
    int order =
        left_bound < right_bound ? left_bound : right_bound;
    if (order > PHY_SERIES_MAX_EXPONENT) {
        order = PHY_SERIES_MAX_EXPONENT;
    }
    return multiply_to_order(cas, left, right, order, out_series);
}

phy_status phy_series_reciprocal_node(phy_cas *cas,
                                      const phy_series *series,
                                      phy_series *out_series)
{
    phy_status status = phy_series_validate(cas, series);
    if (status != PHY_OK || out_series == NULL) {
        return status != PHY_OK ? status : PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_series_is_zero(series)) {
        return PHY_ERR_DOMAIN;
    }
    const int valuation = -series->valuation;
    int order = series->order - 2 * series->valuation;
    if (order > PHY_SERIES_MAX_EXPONENT) {
        order = PHY_SERIES_MAX_EXPONENT;
    }
    if (!exponent_bounds(valuation, order)) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_series candidate;
    memset(&candidate, 0, sizeof candidate);
    candidate.variable = series->variable;
    candidate.center = series->center;
    candidate.valuation = valuation;
    candidate.order = order;
    candidate.count = (size_t)(order - valuation);
    const phy_ir_ref leading = series->coefficients[0];
    status = phy_cas_exact_div_refs(
        cas, cas->one, leading, &candidate.coefficients[0]);
    for (size_t n = 1u; n < candidate.count && status == PHY_OK; ++n) {
        phy_ir_ref sum = cas->zero;
        for (size_t k = 1u; k <= n && k < series->count; ++k) {
            status = phy_cas_step(cas);
            phy_ir_ref product = PHY_IR_NULL;
            phy_ir_ref next = PHY_IR_NULL;
            if (status == PHY_OK) {
                status = phy_cas_exact_mul_refs(
                    cas, series->coefficients[k],
                    candidate.coefficients[n - k], &product);
            }
            if (status == PHY_OK) {
                status =
                    phy_cas_exact_add_refs(cas, sum, product, &next);
            }
            if (status != PHY_OK) {
                return status;
            }
            sum = next;
        }
        phy_ir_ref negated = PHY_IR_NULL;
        status =
            phy_cas_exact_sub_refs(cas, cas->zero, sum, &negated);
        if (status == PHY_OK) {
            status = phy_cas_exact_div_refs(
                cas, negated, leading, &candidate.coefficients[n]);
        }
    }
    if (status != PHY_OK) {
        return status;
    }
    trim(cas, &candidate);
    *out_series = candidate;
    return PHY_OK;
}

phy_status phy_series_div_node(phy_cas *cas, const phy_series *numerator,
                               const phy_series *denominator,
                               phy_series *out_series)
{
    phy_series reciprocal;
    phy_status status =
        phy_series_reciprocal_node(cas, denominator, &reciprocal);
    return status == PHY_OK
               ? phy_series_mul_node(
                     cas, numerator, &reciprocal, out_series)
               : status;
}

phy_status phy_series_pow_int_node(phy_cas *cas, const phy_series *series,
                                   int exponent,
                                   phy_series *out_series)
{
    phy_status status = phy_series_validate(cas, series);
    if (status != PHY_OK || out_series == NULL) {
        return status != PHY_OK ? status : PHY_ERR_INVALID_ARGUMENT;
    }
    if (exponent < 0) {
        if (exponent == INT_MIN) {
            return PHY_ERR_TERM_LIMIT;
        }
        phy_series reciprocal;
        status =
            phy_series_reciprocal_node(cas, series, &reciprocal);
        return status == PHY_OK
                   ? phy_series_pow_int_node(
                         cas, &reciprocal, -exponent, out_series)
                   : status;
    }
    phy_series result;
    status = phy_series_constant(
        cas, series->variable, series->center,
        PHY_SERIES_MAX_EXPONENT, cas->one, &result);
    if (status != PHY_OK || exponent == 0) {
        if (status == PHY_OK) {
            *out_series = result;
        }
        return status;
    }
    phy_series base = *series;
    unsigned power = (unsigned)exponent;
    while (power > 0u && status == PHY_OK) {
        if ((power & 1u) != 0u) {
            status =
                phy_series_mul_node(cas, &result, &base, &result);
        }
        power >>= 1u;
        if (power > 0u && status == PHY_OK) {
            status =
                phy_series_mul_node(cas, &base, &base, &base);
        }
    }
    if (status == PHY_OK) {
        *out_series = result;
    }
    return status;
}

phy_status phy_series_derivative_node(phy_cas *cas,
                                      const phy_series *series,
                                      phy_series *out_series)
{
    phy_status status = phy_series_validate(cas, series);
    if (status != PHY_OK || out_series == NULL ||
        series->order <= PHY_SERIES_MIN_EXPONENT) {
        return status != PHY_OK ? status : PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_series_is_zero(series)) {
        return phy_series_zero(cas, series->variable, series->center,
                               series->order - 1, out_series);
    }
    const int valuation = series->valuation - 1;
    const int order = series->order - 1;
    if (!exponent_bounds(valuation, order)) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_series candidate;
    memset(&candidate, 0, sizeof candidate);
    candidate.variable = series->variable;
    candidate.center = series->center;
    candidate.valuation = valuation;
    candidate.order = order;
    candidate.count = series->count;
    for (size_t index = 0u; index < series->count; ++index) {
        const int exponent = series->valuation + (int)index;
        phy_ir_ref exponent_ref = PHY_IR_NULL;
        status = phy_cas_number_node(
            cas, (phy_cas_rat){exponent, 1}, &exponent_ref);
        if (status == PHY_OK) {
            status = phy_cas_exact_mul_refs(
                cas, series->coefficients[index], exponent_ref,
                &candidate.coefficients[index]);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    trim(cas, &candidate);
    *out_series = candidate;
    return PHY_OK;
}

phy_status phy_series_integral_node(phy_cas *cas,
                                    const phy_series *series,
                                    phy_series *out_series)
{
    phy_status status = phy_series_validate(cas, series);
    if (status != PHY_OK || out_series == NULL) {
        return status != PHY_OK ? status : PHY_ERR_INVALID_ARGUMENT;
    }
    if (series->valuation <= -1 && series->order > -1 &&
        !exact_zero(cas, phy_series_coefficient(cas, series, -1))) {
        return PHY_ERR_DOMAIN; /* logarithm, not a Laurent series */
    }
    if (phy_series_is_zero(series)) {
        return phy_series_zero(cas, series->variable, series->center,
                               series->order + 1, out_series);
    }
    const int valuation = series->valuation + 1;
    const int order = series->order + 1;
    if (!exponent_bounds(valuation, order)) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_series candidate;
    memset(&candidate, 0, sizeof candidate);
    candidate.variable = series->variable;
    candidate.center = series->center;
    candidate.valuation = valuation;
    candidate.order = order;
    candidate.count = series->count;
    for (size_t index = 0u; index < series->count; ++index) {
        const int divisor = series->valuation + (int)index + 1;
        if (divisor == 0) {
            /*
             * A nonzero x^-1 coefficient was rejected above. Preserve an
             * internal exact zero without attempting the undefined 0 / 0.
             */
            candidate.coefficients[index] = cas->zero;
            continue;
        }
        phy_ir_ref divisor_ref = PHY_IR_NULL;
        status = phy_cas_number_node(
            cas, (phy_cas_rat){divisor, 1}, &divisor_ref);
        if (status == PHY_OK) {
            status = phy_cas_exact_div_refs(
                cas, series->coefficients[index], divisor_ref,
                &candidate.coefficients[index]);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    trim(cas, &candidate);
    *out_series = candidate;
    return PHY_OK;
}

static phy_status scale_to_order(phy_cas *cas, const phy_series *series,
                                 phy_ir_ref scalar, int order,
                                 phy_series *out_series)
{
    if (!phy_cas_is_exact(cas, scalar) ||
        order > series->order || order < series->valuation) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_series candidate = *series;
    candidate.order = order;
    candidate.count = (size_t)(order - candidate.valuation);
    for (size_t index = 0u; index < candidate.count; ++index) {
        phy_status status = phy_cas_step(cas);
        if (status == PHY_OK) {
            status = phy_cas_exact_mul_refs(
                cas, candidate.coefficients[index], scalar,
                &candidate.coefficients[index]);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    trim(cas, &candidate);
    *out_series = candidate;
    return PHY_OK;
}

phy_status phy_series_compose_node(phy_cas *cas, const phy_series *outer,
                                   const phy_series *inner,
                                   phy_series *out_series)
{
    phy_status status = phy_series_validate(cas, outer);
    if (status == PHY_OK) {
        status = phy_series_validate(cas, inner);
    }
    if (status != PHY_OK || out_series == NULL) {
        return status != PHY_OK ? status : PHY_ERR_INVALID_ARGUMENT;
    }
    if (!phy_cas_is_integer(cas, outer->center, 0) ||
        outer->valuation < 0 || phy_series_is_zero(inner) ||
        inner->valuation <= 0) {
        return PHY_ERR_DOMAIN;
    }
    const int outer_bound = outer->order * inner->valuation;
    const int order =
        outer_bound < inner->order ? outer_bound : inner->order;
    if (order <= 0 || order > PHY_SERIES_MAX_EXPONENT) {
        return PHY_ERR_TERM_LIMIT;
    }

    phy_series result;
    phy_series power;
    status = phy_series_zero(
        cas, inner->variable, inner->center, order, &result);
    if (status == PHY_OK) {
        status = phy_series_constant(
            cas, inner->variable, inner->center, order, cas->one,
            &power);
    }
    for (int exponent = 0; exponent < outer->order && status == PHY_OK;
         ++exponent) {
        const phy_ir_ref coefficient =
            phy_series_coefficient(cas, outer, exponent);
        if (!exact_zero(cas, coefficient)) {
            phy_series term;
            status =
                scale_to_order(cas, &power, coefficient, order, &term);
            if (status == PHY_OK) {
                status =
                    phy_series_add_node(cas, &result, &term, &result);
            }
        }
        if (exponent + 1 < outer->order && status == PHY_OK) {
            status = multiply_to_order(
                cas, &power, inner, order, &power);
        }
    }
    if (status == PHY_OK) {
        *out_series = result;
    }
    return status;
}

/* ------------------------------------------------------- reader expansion */

#define PHY_SERIES_MAX_RECURSION 48u

typedef enum {
    SERIES_ANALYTIC_EXP = 0,
    SERIES_ANALYTIC_SIN,
    SERIES_ANALYTIC_COS,
    SERIES_ANALYTIC_SINH,
    SERIES_ANALYTIC_COSH,
    SERIES_ANALYTIC_ATAN,
    SERIES_ANALYTIC_ASIN,
    SERIES_ANALYTIC_LOG1P
} series_analytic;

static phy_ir_ref constant_coefficient(const phy_cas *cas,
                                       const phy_series *series)
{
    return series->valuation <= 0 && series->order > 0
               ? phy_series_coefficient(cas, series, 0)
               : cas->zero;
}

static phy_status exact_small(phy_cas *cas, int64_t value,
                              phy_ir_ref *out_ref)
{
    return phy_cas_number_node(
        cas, (phy_cas_rat){value, 1}, out_ref);
}

static phy_status make_maclaurin(phy_cas *cas, phy_ir_ref variable,
                                 int order, series_analytic analytic,
                                 phy_series *out_series)
{
    if (cas == NULL || out_series == NULL || order <= 0 ||
        order > PHY_SERIES_MAX_EXPONENT) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref coefficients[PHY_SERIES_MAX_TERMS];
    for (int exponent = 0; exponent < order; ++exponent) {
        coefficients[exponent] = cas->zero;
    }

    phy_status status = PHY_OK;
    phy_ir_ref factorial = cas->one;
    for (int exponent = 0; exponent < order && status == PHY_OK;
         ++exponent) {
        status = phy_cas_step(cas);
        if (status != PHY_OK) {
            break;
        }
        if (exponent > 0) {
            phy_ir_ref exponent_ref = PHY_IR_NULL;
            status = exact_small(cas, exponent, &exponent_ref);
            if (status == PHY_OK) {
                status = phy_cas_exact_mul_refs(
                    cas, factorial, exponent_ref, &factorial);
            }
        }
        if (status != PHY_OK) {
            break;
        }

        bool factorial_term = false;
        bool negative = false;
        switch (analytic) {
        case SERIES_ANALYTIC_EXP:
            factorial_term = true;
            break;
        case SERIES_ANALYTIC_SIN:
            factorial_term = (exponent & 1) != 0;
            negative = factorial_term &&
                       (((exponent - 1) / 2) & 1) != 0;
            break;
        case SERIES_ANALYTIC_COS:
            factorial_term = (exponent & 1) == 0;
            negative = factorial_term &&
                       ((exponent / 2) & 1) != 0;
            break;
        case SERIES_ANALYTIC_SINH:
            factorial_term = (exponent & 1) != 0;
            break;
        case SERIES_ANALYTIC_COSH:
            factorial_term = (exponent & 1) == 0;
            break;
        case SERIES_ANALYTIC_ATAN:
            if ((exponent & 1) != 0) {
                phy_ir_ref denominator = PHY_IR_NULL;
                status = exact_small(cas, exponent, &denominator);
                if (status == PHY_OK) {
                    status = phy_cas_exact_div_refs(
                        cas, cas->one, denominator,
                        &coefficients[exponent]);
                }
                if ((((exponent - 1) / 2) & 1) != 0 &&
                    status == PHY_OK) {
                    status = phy_cas_exact_sub_refs(
                        cas, cas->zero, coefficients[exponent],
                        &coefficients[exponent]);
                }
            }
            continue;
        case SERIES_ANALYTIC_LOG1P:
            if (exponent > 0) {
                phy_ir_ref denominator = PHY_IR_NULL;
                status = exact_small(cas, exponent, &denominator);
                if (status == PHY_OK) {
                    status = phy_cas_exact_div_refs(
                        cas, cas->one, denominator,
                        &coefficients[exponent]);
                }
                if ((exponent & 1) == 0 && status == PHY_OK) {
                    status = phy_cas_exact_sub_refs(
                        cas, cas->zero, coefficients[exponent],
                        &coefficients[exponent]);
                }
            }
            continue;
        case SERIES_ANALYTIC_ASIN:
            /*
             * c_1 = 1 and
             * c_(2k+1) / c_(2k-1) =
             *     (2k-1)^2 / ((2k)(2k+1)).
             */
            if (exponent == 1) {
                coefficients[exponent] = cas->one;
            } else if (exponent > 1 && (exponent & 1) != 0) {
                const int64_t k = (int64_t)(exponent - 1) / 2;
                phy_ir_ref numerator = PHY_IR_NULL;
                phy_ir_ref denominator = PHY_IR_NULL;
                phy_ir_ref scaled = PHY_IR_NULL;
                status = exact_small(
                    cas, (2 * k - 1) * (2 * k - 1), &numerator);
                if (status == PHY_OK) {
                    status = exact_small(
                        cas, (2 * k) * (2 * k + 1), &denominator);
                }
                if (status == PHY_OK) {
                    status = phy_cas_exact_mul_refs(
                        cas, coefficients[exponent - 2], numerator,
                        &scaled);
                }
                if (status == PHY_OK) {
                    status = phy_cas_exact_div_refs(
                        cas, scaled, denominator,
                        &coefficients[exponent]);
                }
            }
            continue;
        }
        if (factorial_term) {
            status = phy_cas_exact_div_refs(
                cas, negative ? cas->minus_one : cas->one, factorial,
                &coefficients[exponent]);
        }
    }
    if (status != PHY_OK) {
        return status;
    }
    return phy_series_set(
        cas, variable, cas->zero, 0, order, coefficients,
        (size_t)order, out_series);
}

static phy_status compose_analytic(phy_cas *cas,
                                   const phy_series *argument,
                                   series_analytic analytic,
                                   phy_series *out_series)
{
    phy_series outer;
    phy_status status = make_maclaurin(
        cas, argument->variable, argument->order, analytic, &outer);
    if (status != PHY_OK) {
        return status;
    }
    if (phy_series_is_zero(argument)) {
        return phy_series_constant(
            cas, argument->variable, argument->center, argument->order,
            constant_coefficient(cas, &outer), out_series);
    }
    return phy_series_compose_node(cas, &outer, argument, out_series);
}

static phy_status binomial_outer(phy_cas *cas, phy_ir_ref variable,
                                 int order, phy_ir_ref exponent,
                                 phy_series *out_series)
{
    phy_ir_ref coefficients[PHY_SERIES_MAX_TERMS];
    coefficients[0] = cas->one;
    phy_status status = PHY_OK;
    for (int n = 1; n < order && status == PHY_OK; ++n) {
        phy_ir_ref n_minus_one = PHY_IR_NULL;
        phy_ir_ref factor = PHY_IR_NULL;
        phy_ir_ref numerator = PHY_IR_NULL;
        phy_ir_ref denominator = PHY_IR_NULL;
        status = exact_small(cas, n - 1, &n_minus_one);
        if (status == PHY_OK) {
            status = phy_cas_exact_sub_refs(
                cas, exponent, n_minus_one, &factor);
        }
        if (status == PHY_OK) {
            status = phy_cas_exact_mul_refs(
                cas, coefficients[n - 1], factor, &numerator);
        }
        if (status == PHY_OK) {
            status = exact_small(cas, n, &denominator);
        }
        if (status == PHY_OK) {
            status = phy_cas_exact_div_refs(
                cas, numerator, denominator, &coefficients[n]);
        }
    }
    return status == PHY_OK
               ? phy_series_set(
                     cas, variable, cas->zero, 0, order, coefficients,
                     (size_t)order, out_series)
               : status;
}

static phy_status series_from_expr(phy_cas *cas, phy_ir_ref expression,
                                   phy_ir_ref variable, phy_ir_ref center,
                                   int order, unsigned depth,
                                   phy_series *out_series)
{
    if (depth >= PHY_SERIES_MAX_RECURSION) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }
    if (phy_cas_is_exact(cas, expression)) {
        return phy_series_constant(
            cas, variable, center, PHY_SERIES_MAX_EXPONENT, expression,
            out_series);
    }
    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, expression);
    if (kind == PHY_IR_SYMBOL) {
        if (expression != variable) {
            return PHY_ERR_UNSUPPORTED;
        }
        phy_ir_ref coefficients[PHY_SERIES_MAX_TERMS];
        for (int exponent = 0; exponent < PHY_SERIES_MAX_EXPONENT;
             ++exponent) {
            coefficients[exponent] = cas->zero;
        }
        coefficients[0] = center;
        coefficients[1] = cas->one;
        return phy_series_set(
            cas, variable, center, 0, PHY_SERIES_MAX_EXPONENT,
            coefficients, PHY_SERIES_MAX_EXPONENT, out_series);
    }
    if (kind == PHY_IR_ADD || kind == PHY_IR_MUL) {
        phy_series result;
        status = kind == PHY_IR_ADD
                     ? phy_series_zero(
                           cas, variable, center,
                           PHY_SERIES_MAX_EXPONENT, &result)
                     : phy_series_constant(
                           cas, variable, center,
                           PHY_SERIES_MAX_EXPONENT, cas->one, &result);
        const size_t count = phy_ir_child_count(cas->ir, expression);
        for (size_t index = 0u; index < count && status == PHY_OK;
             ++index) {
            phy_series child;
            status = series_from_expr(
                cas, phy_ir_child(cas->ir, expression, index), variable,
                center, order, depth + 1u, &child);
            if (status == PHY_OK) {
                status =
                    kind == PHY_IR_ADD
                        ? phy_series_add_node(
                              cas, &result, &child, &result)
                        : phy_series_mul_node(
                              cas, &result, &child, &result);
            }
        }
        if (status == PHY_OK) {
            *out_series = result;
        }
        return status;
    }
    if (kind == PHY_IR_POW) {
        phy_series base;
        status = series_from_expr(
            cas, phy_ir_child(cas->ir, expression, 0u), variable, center,
            order, depth + 1u, &base);
        if (status != PHY_OK) {
            return status;
        }
        const phy_ir_ref exponent =
            phy_ir_child(cas->ir, expression, 1u);
        int64_t integer = 0;
        if (phy_ir_integer_value(cas->ir, exponent, &integer)) {
            if (integer < INT_MIN || integer > INT_MAX) {
                return PHY_ERR_TERM_LIMIT;
            }
            return phy_series_pow_int_node(
                cas, &base, (int)integer, out_series);
        }
        if (!phy_cas_is_exact(cas, exponent) ||
            constant_coefficient(cas, &base) != cas->one) {
            return PHY_ERR_UNSUPPORTED;
        }
        phy_series one;
        phy_series delta;
        phy_series outer;
        status = phy_series_constant(
            cas, variable, center, order, cas->one, &one);
        if (status == PHY_OK) {
            status =
                phy_series_sub_node(cas, &base, &one, &delta);
        }
        if (status == PHY_OK) {
            status = binomial_outer(
                cas, variable, order, exponent, &outer);
        }
        if (status != PHY_OK) {
            return status;
        }
        if (phy_series_is_zero(&delta)) {
            return phy_series_constant(
                cas, variable, center, order, cas->one, out_series);
        }
        return phy_series_compose_node(
            cas, &outer, &delta, out_series);
    }
    if (kind == PHY_IR_FUNCTION &&
        phy_ir_child_count(cas->ir, expression) == 1u) {
        phy_series argument;
        status = series_from_expr(
            cas, phy_ir_child(cas->ir, expression, 0u), variable, center,
            order, depth + 1u, &argument);
        if (status != PHY_OK) {
            return status;
        }
        if (argument.order > order) {
            if (argument.valuation >= order) {
                status = phy_series_zero(
                    cas, variable, center, order, &argument);
            } else {
                status = scale_to_order(
                    cas, &argument, cas->one, order, &argument);
            }
            if (status != PHY_OK) {
                return status;
            }
        }
        const phy_cas_function function =
            phy_cas_function_of(cas, expression);
        if (function == PHY_CAS_FN_LOG) {
            if (constant_coefficient(cas, &argument) != cas->one) {
                return PHY_ERR_UNSUPPORTED;
            }
            phy_series one;
            phy_series delta;
            status = phy_series_constant(
                cas, variable, center, order, cas->one, &one);
            if (status == PHY_OK) {
                status = phy_series_sub_node(
                    cas, &argument, &one, &delta);
            }
            return status == PHY_OK
                       ? compose_analytic(
                             cas, &delta, SERIES_ANALYTIC_LOG1P,
                             out_series)
                       : status;
        }
        if (!exact_zero(
                cas, constant_coefficient(cas, &argument)) ||
            argument.valuation < 0) {
            return PHY_ERR_UNSUPPORTED;
        }
        switch (function) {
        case PHY_CAS_FN_EXP:
            return compose_analytic(
                cas, &argument, SERIES_ANALYTIC_EXP, out_series);
        case PHY_CAS_FN_SIN:
            return compose_analytic(
                cas, &argument, SERIES_ANALYTIC_SIN, out_series);
        case PHY_CAS_FN_COS:
            return compose_analytic(
                cas, &argument, SERIES_ANALYTIC_COS, out_series);
        case PHY_CAS_FN_SINH:
            return compose_analytic(
                cas, &argument, SERIES_ANALYTIC_SINH, out_series);
        case PHY_CAS_FN_COSH:
            return compose_analytic(
                cas, &argument, SERIES_ANALYTIC_COSH, out_series);
        case PHY_CAS_FN_ATAN:
            return compose_analytic(
                cas, &argument, SERIES_ANALYTIC_ATAN, out_series);
        case PHY_CAS_FN_ASIN:
            return compose_analytic(
                cas, &argument, SERIES_ANALYTIC_ASIN, out_series);
        case PHY_CAS_FN_TAN: {
            phy_series numerator;
            phy_series denominator;
            status = compose_analytic(
                cas, &argument, SERIES_ANALYTIC_SIN, &numerator);
            if (status == PHY_OK) {
                status = compose_analytic(
                    cas, &argument, SERIES_ANALYTIC_COS, &denominator);
            }
            return status == PHY_OK
                       ? phy_series_div_node(
                             cas, &numerator, &denominator, out_series)
                       : status;
        }
        case PHY_CAS_FN_TANH: {
            phy_series numerator;
            phy_series denominator;
            status = compose_analytic(
                cas, &argument, SERIES_ANALYTIC_SINH, &numerator);
            if (status == PHY_OK) {
                status = compose_analytic(
                    cas, &argument, SERIES_ANALYTIC_COSH, &denominator);
            }
            return status == PHY_OK
                       ? phy_series_div_node(
                             cas, &numerator, &denominator, out_series)
                       : status;
        }
        default:
            break;
        }
    }
    return PHY_ERR_UNSUPPORTED;
}

phy_status phy_series_expand_node(phy_cas *cas, phy_ir_ref expression,
                                  phy_ir_ref variable, phy_ir_ref center,
                                  int order, phy_series *out_series)
{
    if (cas == NULL || expression == PHY_IR_NULL ||
        phy_ir_kind_of(cas != NULL ? cas->ir : NULL, variable) !=
            PHY_IR_SYMBOL ||
        !phy_cas_is_exact(cas, center) || out_series == NULL ||
        order < 1 || order > PHY_SERIES_MAX_EXPONENT) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return series_from_expr(
        cas, expression, variable, center, order, 0u, out_series);
}

static phy_status series_normal_expression(phy_cas *cas,
                                           const phy_series *series,
                                           phy_ir_ref *out_ref)
{
    phy_status status = phy_series_validate(cas, series);
    if (status != PHY_OK || out_ref == NULL) {
        return status != PHY_OK ? status : PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref negative_center = PHY_IR_NULL;
    phy_ir_ref shift = PHY_IR_NULL;
    status =
        phy_cas_neg_node(cas, series->center, &negative_center);
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {
            series->variable, negative_center};
        status = phy_cas_add_node(cas, terms, 2u, &shift);
    }
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref terms[PHY_SERIES_MAX_TERMS];
    size_t count = 0u;
    for (int exponent = series->valuation;
         exponent < series->order; ++exponent) {
        const phy_ir_ref coefficient =
            phy_series_coefficient(cas, series, exponent);
        if (exact_zero(cas, coefficient)) {
            continue;
        }
        phy_ir_ref term = coefficient;
        if (exponent != 0) {
            phy_ir_ref exponent_ref = PHY_IR_NULL;
            phy_ir_ref power = PHY_IR_NULL;
            status = exact_small(cas, exponent, &exponent_ref);
            if (status == PHY_OK) {
                status =
                    phy_cas_pow_node(cas, shift, exponent_ref, &power);
            }
            if (status == PHY_OK) {
                if (coefficient == cas->one) {
                    term = power;
                } else {
                    const phy_ir_ref factors[2] = {
                        coefficient, power};
                    status = phy_cas_mul_node(
                        cas, factors, 2u, &term);
                }
            }
        }
        if (status != PHY_OK) {
            return status;
        }
        terms[count++] = term;
    }
    if (count == 0u) {
        *out_ref = cas->zero;
        return PHY_OK;
    }
    return phy_cas_add_node(cas, terms, count, out_ref);
}

static phy_status series_data(phy_cas *cas, const phy_series *series,
                              phy_ir_ref *out_ref)
{
    phy_ir_ref valuation = PHY_IR_NULL;
    phy_ir_ref order = PHY_IR_NULL;
    phy_status status =
        exact_small(cas, series->valuation, &valuation);
    if (status == PHY_OK) {
        status = exact_small(cas, series->order, &order);
    }
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_symbol head =
        phy_ir_intern(cas->ir, "SeriesData");
    if (head == PHY_IR_NO_SYMBOL) {
        return phy_cas_ir_failure(cas);
    }
    const phy_ir_symbol list_head =
        phy_ir_intern(cas->ir, "List");
    if (list_head == PHY_IR_NO_SYMBOL) {
        return phy_cas_ir_failure(cas);
    }
    const phy_ir_ref coefficients = phy_ir_function(
        cas->ir, list_head, series->coefficients, series->count);
    if (coefficients == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    const phy_ir_ref children[5] = {
        series->variable, series->center, valuation, order, coefficients};
    const phy_ir_ref result =
        phy_ir_operator(cas->ir, head, children, 5u);
    if (result == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = result;
    return PHY_OK;
}

phy_status phy_cas_series(phy_cas *cas, phy_ir_ref expression,
                          phy_ir_ref variable, phy_ir_ref center,
                          unsigned order, phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL || expression == PHY_IR_NULL ||
        phy_ir_kind_of(cas != NULL ? cas->ir : NULL, variable) !=
            PHY_IR_SYMBOL ||
        !phy_cas_is_exact(cas, center) ||
        order > PHY_CAS_SERIES_MAX_ORDER) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_cas_begin(cas);
    phy_series expansion;
    const int exclusive_order = (int)order + 1;
    phy_status status = phy_series_expand_node(
        cas, expression, variable, center, exclusive_order, &expansion);
    if (status == PHY_OK && expansion.order > exclusive_order) {
        if (expansion.valuation >= exclusive_order) {
            status = phy_series_zero(
                cas, variable, center, exclusive_order, &expansion);
        } else {
            status = scale_to_order(
                cas, &expansion, cas->one, exclusive_order, &expansion);
        }
    } else if (status == PHY_OK && expansion.order < exclusive_order) {
        status = PHY_ERR_TERM_LIMIT;
    }
    if (status == PHY_OK) {
        status = series_data(cas, &expansion, out_ref);
    }
    return status;
}

phy_status phy_cas_series_normal(phy_cas *cas, phy_ir_ref expression,
                                 phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL || expression == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_cas_begin(cas);
    if (phy_ir_kind_of(cas->ir, expression) != PHY_IR_OPERATOR) {
        *out_ref = expression;
        return PHY_OK;
    }
    const char *head =
        phy_ir_symbol_name(cas->ir, phy_ir_head(cas->ir, expression));
    if (head == NULL || strcmp(head, "SeriesData") != 0) {
        *out_ref = expression;
        return PHY_OK;
    }
    if (phy_ir_child_count(cas->ir, expression) != 5u ||
        phy_ir_kind_of(
            cas->ir, phy_ir_child(cas->ir, expression, 0u)) !=
            PHY_IR_SYMBOL ||
        !phy_cas_is_exact(
            cas, phy_ir_child(cas->ir, expression, 1u))) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    int64_t valuation = 0;
    int64_t order = 0;
    if (!phy_ir_integer_value(
            cas->ir, phy_ir_child(cas->ir, expression, 2u),
            &valuation) ||
        !phy_ir_integer_value(
            cas->ir, phy_ir_child(cas->ir, expression, 3u), &order) ||
        valuation < PHY_SERIES_MIN_EXPONENT ||
        order > PHY_SERIES_MAX_EXPONENT || valuation > order ||
        (uint64_t)(order - valuation) > PHY_SERIES_MAX_TERMS) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    const phy_ir_ref coefficients =
        phy_ir_child(cas->ir, expression, 4u);
    const char *list_name =
        phy_ir_symbol_name(cas->ir, phy_ir_head(cas->ir, coefficients));
    const size_t count =
        phy_ir_child_count(cas->ir, coefficients);
    if (phy_ir_kind_of(cas->ir, coefficients) != PHY_IR_FUNCTION ||
        list_name == NULL || strcmp(list_name, "List") != 0 ||
        count != (size_t)(order - valuation)) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    phy_ir_ref coefficient_refs[PHY_SERIES_MAX_TERMS];
    for (size_t index = 0u; index < count; ++index) {
        coefficient_refs[index] =
            phy_ir_child(cas->ir, coefficients, index);
    }
    phy_series series;
    phy_status status = phy_series_set(
        cas, phy_ir_child(cas->ir, expression, 0u),
        phy_ir_child(cas->ir, expression, 1u), (int)valuation,
        (int)order, coefficient_refs, count, &series);
    if (status != PHY_OK) {
        return status == PHY_ERR_TYPE ? PHY_ERR_CORRUPT_DOCUMENT : status;
    }
    return series_normal_expression(cas, &series, out_ref);
}
