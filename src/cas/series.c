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
    const int order =
        left_bound < right_bound ? left_bound : right_bound;
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
    const int order = series->order - 2 * series->valuation;
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
        cas, series->variable, series->center, series->order, cas->one,
        &result);
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
