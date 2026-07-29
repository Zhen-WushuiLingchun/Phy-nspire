/*
 * Bounded exact antiderivatives for the scalar CAS.
 *
 * This is deliberately a decision procedure for a documented class rather
 * than a table of numerical guesses. When no rule proves an antiderivative,
 * the result is the typed function Integrate[expr,var]. A later rule pack can
 * refine that value without changing the IR or notebook protocol.
 */
#include "cas_internal.h"

static phy_status defer_integral(phy_cas *cas, phy_ir_ref expr,
                                 phy_ir_ref var, phy_ir_ref *out_ref)
{
    const phy_ir_ref arguments[2] = {expr, var};
    const phy_ir_ref deferred =
        phy_ir_function(cas->ir, cas->fn_integrate, arguments, 2u);
    if (deferred == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = deferred;
    return PHY_OK;
}

static phy_status call_one(phy_cas *cas, phy_ir_symbol head,
                           phy_ir_ref argument, phy_ir_ref *out_ref)
{
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    phy_status status = phy_cas_scratch_alloc(cas, 1u, &offset);
    if (status == PHY_OK) {
        phy_cas_scratch_at(cas, offset)[0] = argument;
        status = phy_cas_rebuild_at(cas, PHY_IR_FUNCTION, head, offset, 1u,
                                    out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

static phy_status divide_node(phy_cas *cas, phy_ir_ref numerator,
                              phy_ir_ref denominator, phy_ir_ref *out_ref)
{
    phy_ir_ref inverse = PHY_IR_NULL;
    phy_status status =
        phy_cas_pow_node(cas, denominator, cas->minus_one, &inverse);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref factors[2] = {numerator, inverse};
    return phy_cas_mul_node(cas, factors, 2u, out_ref);
}

/*
 * Reports an inner derivative only when it is independent of var and nonzero.
 * That is the exact condition behind the u-substitution rules below.
 */
static phy_status constant_inner_derivative(phy_cas *cas, phy_ir_ref inner,
                                            phy_ir_ref var,
                                            phy_ir_ref *out_derivative,
                                            bool *out_usable)
{
    phy_ir_ref derivative = PHY_IR_NULL;
    phy_status status = phy_cas_diff_node(cas, inner, var, &derivative);
    if (status != PHY_OK) {
        return status;
    }
    bool varies = false;
    status = phy_cas_may_depend(cas, derivative, var, &varies);
    if (status != PHY_OK) {
        return status;
    }
    *out_derivative = derivative;
    *out_usable = !varies && !phy_cas_is_integer(cas, derivative, 0);
    return PHY_OK;
}

static phy_status integrate_sum(phy_cas *cas, phy_ir_ref expr,
                                phy_ir_ref var, phy_ir_ref *out_ref)
{
    const size_t count = phy_ir_child_count(cas->ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    phy_status status = phy_cas_scratch_alloc(cas, count, &offset);
    for (size_t index = 0u; index < count && status == PHY_OK; ++index) {
        phy_ir_ref term = PHY_IR_NULL;
        status = phy_cas_integrate_node(
            cas, phy_ir_child(cas->ir, expr, index), var, &term);
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, offset)[index] = term;
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_add_at(cas, offset, count, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

static bool monomial_degree(phy_cas *cas, phy_ir_ref expression,
                            phy_ir_ref var, unsigned *out_degree)
{
    if (expression == var) {
        *out_degree = 1u;
        return true;
    }
    int64_t exponent = 0;
    if (phy_ir_kind_of(cas->ir, expression) == PHY_IR_POW &&
        phy_ir_child(cas->ir, expression, 0u) == var &&
        phy_ir_integer_value(
            cas->ir, phy_ir_child(cas->ir, expression, 1u),
            &exponent) &&
        exponent >= 0 && exponent <= 12) {
        *out_degree = (unsigned)exponent;
        return true;
    }
    return false;
}

static bool integration_by_parts_function(phy_cas *cas,
                                          phy_ir_ref expression)
{
    const phy_cas_function function =
        phy_cas_function_of(cas, expression);
    return function == PHY_CAS_FN_EXP ||
           function == PHY_CAS_FN_SIN ||
           function == PHY_CAS_FN_COS ||
           function == PHY_CAS_FN_SINH ||
           function == PHY_CAS_FN_COSH;
}

/*
 * Repeated exact integration by parts:
 *   integral x^n f dx = x^n A - n integral x^(n-1) A dx,
 * where A is the already-certified primitive of f.  The elementary families
 * admitted above stay in the same finite cycle, so n is a strict recursion
 * bound rather than an open-ended heuristic.
 */
static phy_status integrate_monomial_times_elementary(
    phy_cas *cas, unsigned degree, phy_ir_ref dependent,
    phy_ir_ref var, phy_ir_ref *out_ref)
{
    phy_ir_ref primitive = PHY_IR_NULL;
    phy_status status =
        phy_cas_integrate_node(cas, dependent, var, &primitive);
    if (status != PHY_OK) {
        return status;
    }
    if (degree == 0u) {
        *out_ref = primitive;
        return PHY_OK;
    }

    phy_ir_ref degree_ref =
        phy_ir_integer(cas->ir, (int64_t)degree);
    if (degree_ref == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    phy_ir_ref power = PHY_IR_NULL;
    status = phy_cas_pow_node(cas, var, degree_ref, &power);
    phy_ir_ref leading = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {power, primitive};
        status = phy_cas_mul_node(cas, factors, 2u, &leading);
    }
    phy_ir_ref tail = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = integrate_monomial_times_elementary(
            cas, degree - 1u, primitive, var, &tail);
    }
    phy_ir_ref scaled_tail = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {degree_ref, tail};
        status = phy_cas_mul_node(
            cas, factors, 2u, &scaled_tail);
    }
    if (status == PHY_OK) {
        phy_ir_ref negative = PHY_IR_NULL;
        status = phy_cas_neg_node(cas, scaled_tail, &negative);
        if (status == PHY_OK) {
            const phy_ir_ref terms[2] = {leading, negative};
            status = phy_cas_add_node(cas, terms, 2u, out_ref);
        }
    }
    return status;
}

static phy_status integrate_product(phy_cas *cas, phy_ir_ref expr,
                                    phy_ir_ref var, phy_ir_ref *out_ref)
{
    const size_t count = phy_ir_child_count(cas->ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t constants = 0u;
    phy_status status = phy_cas_scratch_alloc(cas, count, &constants);
    size_t constant_count = 0u;
    phy_ir_ref dependent[2] = {PHY_IR_NULL, PHY_IR_NULL};
    size_t dependent_count = 0u;

    for (size_t index = 0u; index < count && status == PHY_OK; ++index) {
        const phy_ir_ref factor = phy_ir_child(cas->ir, expr, index);
        bool varies = false;
        status = phy_cas_may_depend(cas, factor, var, &varies);
        if (status != PHY_OK) {
            break;
        }
        if (varies) {
            if (dependent_count < 2u) {
                dependent[dependent_count] = factor;
            }
            dependent_count++;
        } else {
            phy_cas_scratch_at(cas, constants)[constant_count++] = factor;
        }
    }

    if (status == PHY_OK && dependent_count == 1u) {
        phy_ir_ref coefficient = PHY_IR_NULL;
        phy_ir_ref integrated = PHY_IR_NULL;
        status = phy_cas_mul_at(cas, constants, constant_count, &coefficient);
        if (status == PHY_OK) {
            status =
            phy_cas_integrate_node(cas, dependent[0], var, &integrated);
        }
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {coefficient, integrated};
            status = phy_cas_mul_node(cas, factors, 2u, out_ref);
        }
    } else if (status == PHY_OK && dependent_count == 2u) {
        unsigned degree = 0u;
        phy_ir_ref elementary = PHY_IR_NULL;
        if (monomial_degree(cas, dependent[0], var, &degree) &&
            integration_by_parts_function(cas, dependent[1])) {
            elementary = dependent[1];
        } else if (monomial_degree(
                       cas, dependent[1], var, &degree) &&
                   integration_by_parts_function(cas, dependent[0])) {
            elementary = dependent[0];
        }
        if (elementary != PHY_IR_NULL) {
            phy_ir_ref integrated = PHY_IR_NULL;
            status = integrate_monomial_times_elementary(
                cas, degree, elementary, var, &integrated);
            if (status == PHY_OK) {
                phy_ir_ref coefficient = PHY_IR_NULL;
                status = phy_cas_mul_at(
                    cas, constants, constant_count, &coefficient);
                if (status == PHY_OK) {
                    const phy_ir_ref factors[2] = {
                        coefficient, integrated};
                    status = phy_cas_mul_node(
                        cas, factors, 2u, out_ref);
                }
            }
        } else {
            status = defer_integral(cas, expr, var, out_ref);
        }
    } else if (status == PHY_OK) {
        status = defer_integral(cas, expr, var, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

static uint64_t integer_square_root(uint64_t value)
{
    uint64_t root = 0u;
    for (uint64_t low = 0u, high = 3037000500u; low <= high;) {
        const uint64_t middle = low + (high - low) / 2u;
        if (middle != 0u && middle > value / middle) {
            high = middle - 1u;
        } else {
            root = middle;
            low = middle + 1u;
        }
    }
    return root;
}

static bool exact_positive_square_root(phy_cas_rat value,
                                       phy_cas_rat *out_root)
{
    if (value.num <= 0 || value.den <= 0) {
        return false;
    }
    const uint64_t numerator = (uint64_t)value.num;
    const uint64_t denominator = (uint64_t)value.den;
    const uint64_t numerator_root = integer_square_root(numerator);
    const uint64_t denominator_root = integer_square_root(denominator);
    if (numerator_root * numerator_root != numerator ||
        denominator_root * denominator_root != denominator ||
        numerator_root > (uint64_t)INT64_MAX ||
        denominator_root > (uint64_t)INT64_MAX) {
        return false;
    }
    *out_root = (phy_cas_rat){
        (int64_t)numerator_root, (int64_t)denominator_root};
    return true;
}

/*
 * Recognize exactly 1 + u^2 or 1 - u^2 after scalar normalization.  This is
 * intentionally structural: accepting a merely similar expression would turn
 * a bounded integration rule into an unsound heuristic.
 */
static phy_status quadratic_kernel(phy_cas *cas, phy_ir_ref base,
                                   phy_ir_ref *out_inner, int *out_sign,
                                   bool *out_matched)
{
    *out_inner = PHY_IR_NULL;
    *out_sign = 0;
    *out_matched = false;
    if (phy_ir_kind_of(cas->ir, base) != PHY_IR_ADD ||
        phy_ir_child_count(cas->ir, base) != 2u) {
        return PHY_OK;
    }

    phy_ir_ref quadratic = PHY_IR_NULL;
    bool found_one = false;
    for (size_t index = 0u; index < 2u; ++index) {
        const phy_ir_ref term = phy_ir_child(cas->ir, base, index);
        if (phy_cas_is_integer(cas, term, 1)) {
            found_one = true;
        } else {
            quadratic = term;
        }
    }
    if (!found_one || quadratic == PHY_IR_NULL) {
        return PHY_OK;
    }

    phy_ir_ref coefficient = PHY_IR_NULL;
    phy_ir_ref rest = PHY_IR_NULL;
    phy_status status =
        phy_cas_split_coefficient(cas, quadratic, &coefficient, &rest);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_rat value;
    if (!phy_cas_exact_value(cas, coefficient, &value) ||
        value.num == INT64_MIN ||
        phy_ir_kind_of(cas->ir, rest) != PHY_IR_POW ||
        !phy_cas_is_integer(cas, phy_ir_child(cas->ir, rest, 1u), 2)) {
        return PHY_OK;
    }

    phy_cas_rat magnitude = value;
    if (magnitude.num < 0) {
        magnitude.num = -magnitude.num;
    }
    phy_cas_rat root;
    if (!exact_positive_square_root(magnitude, &root)) {
        return PHY_OK;
    }

    const phy_ir_ref unit = phy_ir_child(cas->ir, rest, 0u);
    if (phy_cas_rat_cmp_int(root, 1) == 0) {
        *out_inner = unit;
    } else {
        phy_ir_ref scale = PHY_IR_NULL;
        status = phy_cas_number_node(cas, root, &scale);
        if (status != PHY_OK) {
            return status;
        }
        const phy_ir_ref factors[2] = {scale, unit};
        status = phy_cas_mul_node(cas, factors, 2u, out_inner);
        if (status != PHY_OK) {
            return status;
        }
    }
    *out_sign = value.num > 0 ? 1 : -1;
    *out_matched = true;
    return PHY_OK;
}

static phy_status integrate_power(phy_cas *cas, phy_ir_ref expr,
                                  phy_ir_ref var, phy_ir_ref *out_ref)
{
    const phy_ir_ref base = phy_ir_child(cas->ir, expr, 0u);
    const phy_ir_ref exponent = phy_ir_child(cas->ir, expr, 1u);
    phy_cas_rat power;
    if (!phy_cas_exact_value(cas, exponent, &power)) {
        return defer_integral(cas, expr, var, out_ref);
    }

    const bool inverse_kernel = phy_cas_rat_cmp_int(power, -1) == 0;
    const bool root_kernel = power.num == -1 && power.den == 2;
    if (inverse_kernel || root_kernel) {
        phy_ir_ref inner = PHY_IR_NULL;
        int sign = 0;
        bool matched = false;
        phy_status status =
            quadratic_kernel(cas, base, &inner, &sign, &matched);
        if (status != PHY_OK) {
            return status;
        }
        if (matched) {
            phy_ir_ref inner_derivative = PHY_IR_NULL;
            bool usable = false;
            status = constant_inner_derivative(
                cas, inner, var, &inner_derivative, &usable);
            if (status != PHY_OK) {
                return status;
            }
            if (usable) {
                const phy_cas_function primitive =
                    inverse_kernel
                        ? (sign > 0 ? PHY_CAS_FN_ATAN
                                    : PHY_CAS_FN_ATANH)
                        : (sign > 0 ? PHY_CAS_FN_ASINH
                                    : PHY_CAS_FN_ASIN);
                phy_ir_ref call = PHY_IR_NULL;
                status = call_one(cas, cas->functions[primitive], inner,
                                  &call);
                return status == PHY_OK
                           ? divide_node(cas, call, inner_derivative, out_ref)
                           : status;
            }
        }
    }

    phy_ir_ref inner_derivative = PHY_IR_NULL;
    bool usable = false;
    phy_status status = constant_inner_derivative(
        cas, base, var, &inner_derivative, &usable);
    if (status != PHY_OK) {
        return status;
    }
    if (!usable) {
        return defer_integral(cas, expr, var, out_ref);
    }

    if (phy_cas_rat_cmp_int(power, -1) == 0) {
        phy_ir_ref logarithm = PHY_IR_NULL;
        status = call_one(cas, cas->functions[PHY_CAS_FN_LOG], base,
                          &logarithm);
        return status == PHY_OK
                   ? divide_node(cas, logarithm, inner_derivative, out_ref)
                   : status;
    }

    phy_ir_ref raised_exponent = PHY_IR_NULL;
    const phy_ir_ref addends[2] = {exponent, cas->one};
    status = phy_cas_add_node(cas, addends, 2u, &raised_exponent);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref raised = PHY_IR_NULL;
    status = phy_cas_pow_node(cas, base, raised_exponent, &raised);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref denominator = PHY_IR_NULL;
    const phy_ir_ref factors[2] = {raised_exponent, inner_derivative};
    status = phy_cas_mul_node(cas, factors, 2u, &denominator);
    return status == PHY_OK ? divide_node(cas, raised, denominator, out_ref)
                            : status;
}

static phy_status gaussian_parts(phy_cas *cas, phy_ir_ref argument,
                                 phy_ir_ref *out_exponential,
                                 phy_ir_ref *out_pi_inverse_sqrt)
{
    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref square = PHY_IR_NULL;
    phy_ir_ref negated = PHY_IR_NULL;
    phy_ir_ref minus_half = PHY_IR_NULL;
    phy_status status =
        phy_cas_number_node(cas, (phy_cas_rat){2, 1}, &two);
    if (status == PHY_OK) {
        status = phy_cas_pow_node(cas, argument, two, &square);
    }
    if (status == PHY_OK) {
        status = phy_cas_neg_node(cas, square, &negated);
    }
    if (status == PHY_OK) {
        status = call_one(cas, cas->functions[PHY_CAS_FN_EXP], negated,
                          out_exponential);
    }
    if (status == PHY_OK) {
        status = phy_cas_number_node(
            cas, (phy_cas_rat){-1, 2}, &minus_half);
    }
    if (status == PHY_OK) {
        status = phy_cas_pow_node(
            cas, cas->constant_pi, minus_half, out_pi_inverse_sqrt);
    }
    return status;
}

static phy_status integrate_gaussian(phy_cas *cas, phy_ir_ref argument,
                                     phy_ir_ref var, phy_ir_ref *out_ref,
                                     bool *out_matched)
{
    *out_matched = false;
    phy_ir_ref coefficient = PHY_IR_NULL;
    phy_ir_ref square = PHY_IR_NULL;
    phy_status status =
        phy_cas_split_coefficient(cas, argument, &coefficient, &square);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_rat coefficient_value;
    if (!phy_cas_exact_value(cas, coefficient, &coefficient_value) ||
        coefficient_value.num >= 0 ||
        coefficient_value.num == INT64_MIN ||
        phy_ir_kind_of(cas->ir, square) != PHY_IR_POW ||
        !phy_cas_is_integer(cas, phy_ir_child(cas->ir, square, 1u), 2)) {
        return PHY_OK;
    }
    coefficient_value.num = -coefficient_value.num;
    phy_cas_rat scale_value;
    if (!exact_positive_square_root(coefficient_value, &scale_value)) {
        return PHY_OK;
    }
    const phy_ir_ref unit = phy_ir_child(cas->ir, square, 0u);
    phy_ir_ref inner = unit;
    if (phy_cas_rat_cmp_int(scale_value, 1) != 0) {
        phy_ir_ref scale = PHY_IR_NULL;
        status = phy_cas_number_node(cas, scale_value, &scale);
        if (status != PHY_OK) {
            return status;
        }
        const phy_ir_ref factors[2] = {scale, unit};
        status = phy_cas_mul_node(cas, factors, 2u, &inner);
        if (status != PHY_OK) {
            return status;
        }
    }
    phy_ir_ref inner_derivative = PHY_IR_NULL;
    bool usable = false;
    status = constant_inner_derivative(
        cas, inner, var, &inner_derivative, &usable);
    if (status != PHY_OK || !usable) {
        return status;
    }

    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref pi_half = PHY_IR_NULL;
    phy_ir_ref error_function = PHY_IR_NULL;
    phy_ir_ref numerator = PHY_IR_NULL;
    status = phy_cas_number_node(cas, (phy_cas_rat){1, 2}, &half);
    if (status == PHY_OK) {
        status =
            phy_cas_pow_node(cas, cas->constant_pi, half, &pi_half);
    }
    if (status == PHY_OK) {
        status = call_one(cas, cas->functions[PHY_CAS_FN_ERF], inner,
                          &error_function);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[3] = {half, pi_half, error_function};
        status = phy_cas_mul_node(cas, factors, 3u, &numerator);
    }
    if (status == PHY_OK) {
        status =
            divide_node(cas, numerator, inner_derivative, out_ref);
    }
    if (status == PHY_OK) {
        *out_matched = true;
    }
    return status;
}

static phy_status integrate_function(phy_cas *cas, phy_ir_ref expr,
                                     phy_ir_ref var, phy_ir_ref *out_ref)
{
    const phy_ir_symbol head = phy_cas_known_function(cas, expr);
    if (head == PHY_IR_NO_SYMBOL) {
        return defer_integral(cas, expr, var, out_ref);
    }
    const phy_ir_ref argument = phy_ir_child(cas->ir, expr, 0u);
    const phy_cas_function function = phy_cas_function_id(cas, head);
    if (function == PHY_CAS_FN_EXP) {
        bool matched = false;
        phy_status status =
            integrate_gaussian(cas, argument, var, out_ref, &matched);
        if (status != PHY_OK || matched) {
            return status;
        }
    }
    phy_ir_ref inner_derivative = PHY_IR_NULL;
    bool usable = false;
    phy_status status = constant_inner_derivative(
        cas, argument, var, &inner_derivative, &usable);
    if (status != PHY_OK) {
        return status;
    }
    if (!usable) {
        return defer_integral(cas, expr, var, out_ref);
    }

    phy_ir_ref primitive = PHY_IR_NULL;
    if (function == PHY_CAS_FN_SIN) {
        status = call_one(cas, cas->functions[PHY_CAS_FN_COS], argument,
                          &primitive);
        if (status == PHY_OK) {
            status = phy_cas_neg_node(cas, primitive, &primitive);
        }
    } else if (function == PHY_CAS_FN_COS) {
        status = call_one(cas, cas->functions[PHY_CAS_FN_SIN], argument,
                          &primitive);
    } else if (function == PHY_CAS_FN_EXP) {
        primitive = expr;
    } else if (function == PHY_CAS_FN_TAN) {
        phy_ir_ref cosine = PHY_IR_NULL;
        status = call_one(cas, cas->functions[PHY_CAS_FN_COS], argument,
                          &cosine);
        if (status == PHY_OK) {
            status = call_one(cas, cas->functions[PHY_CAS_FN_LOG], cosine,
                              &primitive);
        }
        if (status == PHY_OK) {
            status = phy_cas_neg_node(cas, primitive, &primitive);
        }
    } else if (function == PHY_CAS_FN_LOG) {
        phy_ir_ref logarithm = PHY_IR_NULL;
        status = call_one(cas, cas->functions[PHY_CAS_FN_LOG], argument,
                          &logarithm);
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {argument, logarithm};
            status = phy_cas_mul_node(cas, factors, 2u, &primitive);
        }
        if (status == PHY_OK) {
            phy_ir_ref negated = PHY_IR_NULL;
            status = phy_cas_neg_node(cas, argument, &negated);
            if (status == PHY_OK) {
                const phy_ir_ref addends[2] = {primitive, negated};
                status = phy_cas_add_node(cas, addends, 2u, &primitive);
            }
        }
    } else if (function == PHY_CAS_FN_SINH) {
        status = call_one(cas, cas->functions[PHY_CAS_FN_COSH], argument,
                          &primitive);
    } else if (function == PHY_CAS_FN_COSH) {
        status = call_one(cas, cas->functions[PHY_CAS_FN_SINH], argument,
                          &primitive);
    } else if (function == PHY_CAS_FN_TANH) {
        phy_ir_ref cosine = PHY_IR_NULL;
        status = call_one(cas, cas->functions[PHY_CAS_FN_COSH], argument,
                          &cosine);
        if (status == PHY_OK) {
            status = call_one(cas, cas->functions[PHY_CAS_FN_LOG], cosine,
                              &primitive);
        }
    } else if (function == PHY_CAS_FN_ERF ||
               function == PHY_CAS_FN_ERFC) {
        phy_ir_ref exponential = PHY_IR_NULL;
        phy_ir_ref pi_inverse_sqrt = PHY_IR_NULL;
        status = gaussian_parts(
            cas, argument, &exponential, &pi_inverse_sqrt);
        phy_ir_ref tail = PHY_IR_NULL;
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {
                exponential, pi_inverse_sqrt};
            status = phy_cas_mul_node(cas, factors, 2u, &tail);
        }
        if (status == PHY_OK && function == PHY_CAS_FN_ERFC) {
            status = phy_cas_neg_node(cas, tail, &tail);
        }
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {argument, expr};
            phy_ir_ref leading = PHY_IR_NULL;
            status = phy_cas_mul_node(cas, factors, 2u, &leading);
            if (status == PHY_OK) {
                const phy_ir_ref terms[2] = {leading, tail};
                status = phy_cas_add_node(cas, terms, 2u, &primitive);
            }
        }
    } else {
        return defer_integral(cas, expr, var, out_ref);
    }
    return status == PHY_OK
               ? divide_node(cas, primitive, inner_derivative, out_ref)
               : status;
}

phy_status phy_cas_integrate_node(phy_cas *cas, phy_ir_ref expr,
                                  phy_ir_ref var, phy_ir_ref *out_ref)
{
    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, expr);
    if (kind == PHY_IR_KIND_INVALID) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_cas_cache_get(cas, PHY_CAS_MEMO_INTEGRATE, expr, var, out_ref,
                          NULL)) {
        return PHY_OK;
    }
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }
    if (kind == PHY_IR_ERROR) {
        *out_ref = expr;
        return PHY_OK;
    }

    bool varies = false;
    status = phy_cas_may_depend(cas, expr, var, &varies);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref result = PHY_IR_NULL;
    if (!varies) {
        const phy_ir_ref factors[2] = {expr, var};
        status = phy_cas_mul_node(cas, factors, 2u, &result);
    } else if (expr == var) {
        phy_ir_ref two = PHY_IR_NULL;
        phy_ir_ref squared = PHY_IR_NULL;
        status = phy_cas_number_node(cas, (phy_cas_rat){2, 1}, &two);
        if (status == PHY_OK) {
            status = phy_cas_pow_node(cas, var, two, &squared);
        }
        if (status == PHY_OK) {
            status = divide_node(cas, squared, two, &result);
        }
    } else {
        switch (kind) {
        case PHY_IR_ADD:
            status = integrate_sum(cas, expr, var, &result);
            break;
        case PHY_IR_MUL:
            status = integrate_product(cas, expr, var, &result);
            break;
        case PHY_IR_POW:
            status = integrate_power(cas, expr, var, &result);
            break;
        case PHY_IR_FUNCTION:
            status = integrate_function(cas, expr, var, &result);
            break;
        default:
            status = defer_integral(cas, expr, var, &result);
            break;
        }
    }
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_cache_put(cas, PHY_CAS_MEMO_INTEGRATE, expr, var, result,
                      PHY_IR_NULL);
    *out_ref = result;
    return PHY_OK;
}

static bool contains_deferred_integral(const phy_cas *cas, phy_ir_ref expr)
{
    if (phy_ir_kind_of(cas->ir, expr) == PHY_IR_FUNCTION &&
        phy_ir_head(cas->ir, expr) == cas->fn_integrate) {
        return true;
    }
    const size_t count = phy_ir_child_count(cas->ir, expr);
    for (size_t index = 0u; index < count; ++index) {
        if (contains_deferred_integral(
                cas, phy_ir_child(cas->ir, expr, index))) {
            return true;
        }
    }
    return false;
}

phy_status phy_cas_integrate(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                             phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_cas_begin(cas);
    if (phy_ir_kind_of(cas->ir, expr) == PHY_IR_KIND_INVALID) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_ir_kind_of(cas->ir, var) != PHY_IR_SYMBOL) {
        return PHY_ERR_TYPE;
    }
    const phy_ir_symbol variable = phy_ir_head(cas->ir, var);
    if ((phy_ir_assumptions(cas->ir, variable) &
         (uint32_t)PHY_IR_ASSUME_CONSTANT) != 0u) {
        return PHY_ERR_TYPE;
    }
    phy_ir_ref reduced = PHY_IR_NULL;
    phy_status status =
        phy_cas_simplify_node(cas, expr, &reduced);
    phy_ir_ref candidate = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_integrate_node(
            cas, reduced, var, &candidate);
    }
    if (status != PHY_OK) {
        return status;
    }
    if (!contains_deferred_integral(cas, candidate)) {
        phy_ir_ref derivative = PHY_IR_NULL;
        phy_ir_ref negative_integrand = PHY_IR_NULL;
        phy_ir_ref difference = PHY_IR_NULL;
        status = phy_cas_diff_node(cas, candidate, var, &derivative);
        if (status == PHY_OK) {
            status = phy_cas_neg_node(
                cas, reduced, &negative_integrand);
        }
        if (status == PHY_OK) {
            const phy_ir_ref terms[2] = {
                derivative, negative_integrand};
            status = phy_cas_add_node(
                cas, terms, 2u, &difference);
        }
        phy_cas_decision verified = PHY_CAS_UNKNOWN;
        if (status == PHY_OK) {
            status = phy_cas_decide_zero_node(
                cas, difference, &verified);
        }
        if (status != PHY_OK) {
            return status;
        }
        if (verified != PHY_CAS_ZERO) {
            return verified == PHY_CAS_UNKNOWN
                       ? PHY_ERR_UNSUPPORTED
                       : PHY_ERR_CORRUPT_DOCUMENT;
        }
    }
    *out_ref = candidate;
    return PHY_OK;
}
