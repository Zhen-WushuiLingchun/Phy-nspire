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

static phy_status integrate_product(phy_cas *cas, phy_ir_ref expr,
                                    phy_ir_ref var, phy_ir_ref *out_ref)
{
    const size_t count = phy_ir_child_count(cas->ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t constants = 0u;
    phy_status status = phy_cas_scratch_alloc(cas, count, &constants);
    size_t constant_count = 0u;
    phy_ir_ref dependent = PHY_IR_NULL;
    size_t dependent_count = 0u;

    for (size_t index = 0u; index < count && status == PHY_OK; ++index) {
        const phy_ir_ref factor = phy_ir_child(cas->ir, expr, index);
        bool varies = false;
        status = phy_cas_may_depend(cas, factor, var, &varies);
        if (status != PHY_OK) {
            break;
        }
        if (varies) {
            dependent = factor;
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
                phy_cas_integrate_node(cas, dependent, var, &integrated);
        }
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {coefficient, integrated};
            status = phy_cas_mul_node(cas, factors, 2u, out_ref);
        }
    } else if (status == PHY_OK) {
        status = defer_integral(cas, expr, var, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
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
        status = call_one(cas, cas->fn_log, base, &logarithm);
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

static phy_status integrate_function(phy_cas *cas, phy_ir_ref expr,
                                     phy_ir_ref var, phy_ir_ref *out_ref)
{
    const phy_ir_symbol head = phy_cas_known_function(cas, expr);
    if (head == PHY_IR_NO_SYMBOL) {
        return defer_integral(cas, expr, var, out_ref);
    }
    const phy_ir_ref argument = phy_ir_child(cas->ir, expr, 0u);
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
    if (head == cas->fn_sin) {
        status = call_one(cas, cas->fn_cos, argument, &primitive);
        if (status == PHY_OK) {
            status = phy_cas_neg_node(cas, primitive, &primitive);
        }
    } else if (head == cas->fn_cos) {
        status = call_one(cas, cas->fn_sin, argument, &primitive);
    } else if (head == cas->fn_exp) {
        primitive = expr;
    } else if (head == cas->fn_tan) {
        phy_ir_ref cosine = PHY_IR_NULL;
        status = call_one(cas, cas->fn_cos, argument, &cosine);
        if (status == PHY_OK) {
            status = call_one(cas, cas->fn_log, cosine, &primitive);
        }
        if (status == PHY_OK) {
            status = phy_cas_neg_node(cas, primitive, &primitive);
        }
    } else if (head == cas->fn_log) {
        phy_ir_ref logarithm = PHY_IR_NULL;
        status = call_one(cas, cas->fn_log, argument, &logarithm);
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
    phy_ir_ref reduced = PHY_IR_NULL;
    const phy_status status =
        phy_cas_simplify_node(cas, expr, &reduced);
    return status == PHY_OK
               ? phy_cas_integrate_node(cas, reduced, var, out_ref)
               : status;
}
