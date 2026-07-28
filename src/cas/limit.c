/*
 * Phy-nspire — exact finite and rational-infinity limits.
 *
 * This file deliberately has no numerical fallback. Regular expressions are
 * handled by certified symbolic continuity and exact substitution; singular
 * expressions use the leading term of the bounded Laurent-series engine.
 */
#include "series_internal.h"

#include <stdint.h>
#include <string.h>

#define PHY_LIMIT_SERIES_ORDER 16
#define PHY_LIMIT_TEMP_ATTEMPTS 8u

static bool symbol_named(const phy_cas *cas, phy_ir_ref ref,
                         const char *name)
{
    if (phy_ir_kind_of(cas->ir, ref) != PHY_IR_SYMBOL) {
        return false;
    }
    const char *actual =
        phy_ir_symbol_name(cas->ir, phy_ir_head(cas->ir, ref));
    return actual != NULL && strcmp(actual, name) == 0;
}

/* +1, -1, or 0 when the point is not a canonical directed infinity. */
static int infinity_sign(const phy_cas *cas, phy_ir_ref point)
{
    if (symbol_named(cas, point, "Infinity")) {
        return 1;
    }
    if (phy_ir_kind_of(cas->ir, point) != PHY_IR_MUL ||
        phy_ir_child_count(cas->ir, point) != 2u) {
        return 0;
    }
    const phy_ir_ref left = phy_ir_child(cas->ir, point, 0u);
    const phy_ir_ref right = phy_ir_child(cas->ir, point, 1u);
    if ((phy_cas_is_integer(cas, left, -1) &&
         symbol_named(cas, right, "Infinity")) ||
        (phy_cas_is_integer(cas, right, -1) &&
         symbol_named(cas, left, "Infinity"))) {
        return -1;
    }
    return 0;
}

static phy_status infinity_node(phy_cas *cas, int sign,
                                phy_ir_ref *out_ref)
{
    const phy_ir_symbol name = phy_ir_intern(cas->ir, "Infinity");
    if (name == PHY_IR_NO_SYMBOL) {
        return phy_cas_ir_failure(cas);
    }
    const uint32_t assumptions =
        (uint32_t)PHY_IR_ASSUME_CONSTANT |
        (uint32_t)PHY_IR_ASSUME_REAL |
        (uint32_t)PHY_IR_ASSUME_POSITIVE |
        (uint32_t)PHY_IR_ASSUME_NONZERO;
    phy_status status = phy_ir_assume(cas->ir, name, assumptions);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref positive = phy_ir_symbol_ref(cas->ir, name);
    if (positive == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    if (sign > 0) {
        *out_ref = positive;
        return PHY_OK;
    }
    return phy_cas_neg_node(cas, positive, out_ref);
}

/*
 * A deliberately small structural continuity proof. A subtree independent of
 * x is constant in x even when its internal head is opaque. Dependent
 * polynomials/rational functions and real-entire elementary heads are safe;
 * branch-sensitive heads are left to the Laurent engine.
 */
static phy_status continuity_safe(phy_cas *cas, phy_ir_ref expression,
                                  phy_ir_ref variable, bool *out_safe)
{
    bool depends = true;
    phy_status status =
        phy_cas_may_depend(cas, expression, variable, &depends);
    if (status != PHY_OK) {
        return status;
    }
    if (!depends) {
        *out_safe = true;
        return PHY_OK;
    }
    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, expression);
    if (expression == variable) {
        *out_safe = true;
        return PHY_OK;
    }
    if (kind == PHY_IR_ADD || kind == PHY_IR_MUL) {
        const size_t count = phy_ir_child_count(cas->ir, expression);
        for (size_t index = 0u; index < count; ++index) {
            bool child_safe = false;
            status = continuity_safe(
                cas, phy_ir_child(cas->ir, expression, index), variable,
                &child_safe);
            if (status != PHY_OK || !child_safe) {
                *out_safe = false;
                return status;
            }
        }
        *out_safe = true;
        return PHY_OK;
    }
    if (kind == PHY_IR_POW) {
        int64_t exponent = 0;
        if (!phy_ir_integer_value(
                cas->ir, phy_ir_child(cas->ir, expression, 1u),
                &exponent)) {
            *out_safe = false;
            return PHY_OK;
        }
        (void)exponent;
        return continuity_safe(
            cas, phy_ir_child(cas->ir, expression, 0u), variable,
            out_safe);
    }
    if (kind == PHY_IR_FUNCTION &&
        phy_ir_child_count(cas->ir, expression) == 1u) {
        const phy_cas_function function =
            phy_cas_function_of(cas, expression);
        switch (function) {
        case PHY_CAS_FN_SIN:
        case PHY_CAS_FN_COS:
        case PHY_CAS_FN_EXP:
        case PHY_CAS_FN_SINH:
        case PHY_CAS_FN_COSH:
        case PHY_CAS_FN_ATAN:
        case PHY_CAS_FN_ASINH:
            return continuity_safe(
                cas, phy_ir_child(cas->ir, expression, 0u), variable,
                out_safe);
        default:
            break;
        }
    }
    *out_safe = false;
    return PHY_OK;
}

/* Every negative integer power in a substituted expression needs proof that
   its base is nonzero. */
static bool denominators_certified(phy_cas *cas, phy_ir_ref expression)
{
    if (phy_ir_kind_of(cas->ir, expression) == PHY_IR_POW) {
        int64_t exponent = 0;
        if (phy_ir_integer_value(
                cas->ir, phy_ir_child(cas->ir, expression, 1u),
                &exponent) &&
            exponent < 0 &&
            !phy_cas_known_nonzero(
                cas, phy_ir_child(cas->ir, expression, 0u))) {
            return false;
        }
    }
    const size_t count = phy_ir_child_count(cas->ir, expression);
    for (size_t index = 0u; index < count; ++index) {
        if (!denominators_certified(
                cas, phy_ir_child(cas->ir, expression, index))) {
            return false;
        }
    }
    return true;
}

static phy_status result_from_series(
    phy_cas *cas, phy_ir_ref expression, phy_ir_ref variable,
    phy_ir_ref center, phy_cas_limit_direction direction,
    phy_ir_ref *out_ref)
{
    phy_cas_begin(cas);
    phy_series series;
    phy_status status = phy_series_expand_node(
        cas, expression, variable, center, PHY_LIMIT_SERIES_ORDER,
        &series);
    if (status != PHY_OK) {
        return status;
    }
    if (phy_series_is_zero(&series) || series.valuation > 0) {
        *out_ref = cas->zero;
        return PHY_OK;
    }
    if (series.valuation == 0) {
        *out_ref = phy_series_coefficient(cas, &series, 0);
        return PHY_OK;
    }

    const phy_ir_ref leading =
        phy_series_coefficient(cas, &series, series.valuation);
    int above_sign = phy_cas_exact_sign_ref(cas, leading);
    int below_sign = above_sign;
    if (((-series.valuation) % 2) != 0) {
        below_sign = -below_sign;
    }
    int selected = above_sign;
    if (direction == PHY_CAS_LIMIT_FROM_BELOW) {
        selected = below_sign;
    } else if (direction == PHY_CAS_LIMIT_TWO_SIDED &&
               above_sign != below_sign) {
        return PHY_ERR_DOMAIN;
    }
    return infinity_node(cas, selected, out_ref);
}

static phy_status temporary_variable(phy_cas *cas, phy_ir_ref expression,
                                     phy_ir_ref *out_variable)
{
    for (unsigned attempt = 0u; attempt < PHY_LIMIT_TEMP_ATTEMPTS;
         ++attempt) {
        char name[] = "$LimitT0";
        name[7] = (char)('0' + attempt);
        const phy_ir_symbol symbol = phy_ir_intern(cas->ir, name);
        if (symbol == PHY_IR_NO_SYMBOL) {
            return phy_cas_ir_failure(cas);
        }
        const phy_ir_ref candidate =
            phy_ir_symbol_ref(cas->ir, symbol);
        if (candidate == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
        bool depends = true;
        const phy_status status =
            phy_cas_may_depend(cas, expression, candidate, &depends);
        if (status != PHY_OK) {
            return status;
        }
        if (!depends) {
            *out_variable = candidate;
            return PHY_OK;
        }
    }
    return PHY_ERR_TERM_LIMIT;
}

static phy_status limit_at_infinity(phy_cas *cas, phy_ir_ref expression,
                                    phy_ir_ref variable, int point_sign,
                                    phy_ir_ref *out_ref)
{
    phy_cas_begin(cas);
    phy_ir_ref t = PHY_IR_NULL;
    phy_status status = temporary_variable(cas, expression, &t);
    phy_ir_ref reciprocal = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_pow_node(
            cas, t, cas->minus_one, &reciprocal);
    }
    if (status == PHY_OK && point_sign < 0) {
        status = phy_cas_neg_node(cas, reciprocal, &reciprocal);
    }
    if (status != PHY_OK) {
        return status;
    }
    const phy_cas_rule rule = {variable, reciprocal};
    phy_ir_ref transformed = PHY_IR_NULL;
    status =
        phy_cas_substitute_node(cas, expression, &rule, 1u, &transformed);
    if (status != PHY_OK) {
        return status;
    }
    return result_from_series(
        cas, transformed, t, cas->zero, PHY_CAS_LIMIT_FROM_ABOVE,
        out_ref);
}

phy_status phy_cas_limit(phy_cas *cas, phy_ir_ref expression,
                         phy_ir_ref variable, phy_ir_ref point,
                         phy_cas_limit_direction direction,
                         phy_ir_ref *out_ref)
{
    if (cas == NULL || expression == PHY_IR_NULL ||
        phy_ir_kind_of(cas != NULL ? cas->ir : NULL, variable) !=
            PHY_IR_SYMBOL ||
        point == PHY_IR_NULL || out_ref == NULL ||
        direction > PHY_CAS_LIMIT_FROM_BELOW) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const int at_infinity = infinity_sign(cas, point);
    if (at_infinity != 0) {
        if (direction != PHY_CAS_LIMIT_TWO_SIDED) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
        return limit_at_infinity(
            cas, expression, variable, at_infinity, out_ref);
    }
    if (!phy_cas_is_exact(cas, point)) {
        return PHY_ERR_TYPE;
    }

    phy_cas_begin(cas);
    bool safe = false;
    phy_status status =
        continuity_safe(cas, expression, variable, &safe);
    if (status != PHY_OK) {
        return status;
    }
    if (safe) {
        const phy_cas_rule rule = {variable, point};
        phy_ir_ref substituted = PHY_IR_NULL;
        status = phy_cas_substitute_node(
            cas, expression, &rule, 1u, &substituted);
        if (status == PHY_OK &&
            denominators_certified(cas, substituted)) {
            *out_ref = substituted;
            return PHY_OK;
        }
        if (status != PHY_OK && status != PHY_ERR_DOMAIN &&
            status != PHY_ERR_UNSUPPORTED) {
            return status;
        }
    }
    return result_from_series(
        cas, expression, variable, point, direction, out_ref);
}
