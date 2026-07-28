/*
 * Phy-nspire — partial differentiation.
 *
 * The rules are the ordinary ones. What is worth reading closely is what
 * happens when a rule does not apply, because that is where a scalar CAS can
 * quietly produce a wrong answer for a physics module.
 *
 * A tensor component may depend on a coordinate. Nothing in the graph says that
 * it does: PHY_IR_TENSOR carries indices, not the functional form of its
 * entries. So "this subtree does not mention theta, therefore its derivative is
 * zero" is valid for a sum of symbols and invalid for a tensor, an operator, or
 * an unknown function. phy_cas_may_depend() draws exactly that line -- it
 * answers false only when the subtree is built entirely from kinds whose
 * dependence is visible -- and everything else differentiates to an
 * unevaluated PHY_IR_DERIVATIVE rather than to zero.
 *
 * An unevaluated derivative is a correct answer that a later layer can refine.
 * A wrong zero is a curvature tensor that vanishes for a spacetime that curves.
 */
#include "cas_internal.h"

/* ------------------------------------------------------------- dependence */

phy_status phy_cas_may_depend(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                              bool *out_depends)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);

    if (kind == PHY_IR_KIND_INVALID) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (expr == var) {
        *out_depends = true;
        return PHY_OK;
    }

    phy_ir_ref cached;
    if (phy_cas_cache_get(cas, PHY_CAS_MEMO_DEPENDS, expr, var, &cached, NULL)) {
        *out_depends = (cached != cas->zero);
        return PHY_OK;
    }
    const phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }

    bool depends;
    if (phy_cas_kind_is_opaque(kind)) {
        /* Not "it depends", but "this layer cannot see that it does not". */
        depends = true;
    } else if (kind == PHY_IR_FUNCTION &&
               phy_cas_known_function(cas, expr) == PHY_IR_NO_SYMBOL) {
        /* An unknown function's arguments are visible, but its body is not: a
           zero-argument f() could still be a function of anything. */
        depends = phy_ir_child_count(ir, expr) == 0u;
        for (size_t i = 0u; i < phy_ir_child_count(ir, expr) && !depends; i++) {
            phy_status inner = phy_cas_may_depend(
                cas, phy_ir_child(ir, expr, i), var, &depends);
            if (inner != PHY_OK) {
                return inner;
            }
        }
    } else {
        depends = false;
        for (size_t i = 0u; i < phy_ir_child_count(ir, expr) && !depends; i++) {
            const phy_status inner = phy_cas_may_depend(
                cas, phy_ir_child(ir, expr, i), var, &depends);
            if (inner != PHY_OK) {
                return inner;
            }
        }
    }

    phy_cas_cache_put(cas, PHY_CAS_MEMO_DEPENDS, expr, var,
                      depends ? cas->one : cas->zero, PHY_IR_NULL);
    *out_depends = depends;
    return PHY_OK;
}

/* ------------------------------------------------------------ known rules */

static phy_status unary_call(phy_cas *cas, phy_ir_symbol head,
                             phy_ir_ref argument, phy_ir_ref *out_ref)
{
    if (head == PHY_IR_NO_SYMBOL) {
        return phy_cas_ir_failure(cas);
    }
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    phy_status status = phy_cas_scratch_alloc(cas, 1u, &offset);
    if (status == PHY_OK) {
        phy_cas_scratch_at(cas, offset)[0] = argument;
        status = phy_cas_rebuild_at(
            cas, PHY_IR_FUNCTION, head, offset, 1u, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

/* The derivative of a known function at its argument, before the chain rule
   multiplies in the argument's own derivative. */
static phy_status outer_derivative(phy_cas *cas, phy_ir_symbol head,
                                   phy_ir_ref argument, phy_ir_ref *out_ref)
{
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    phy_status status = phy_cas_scratch_alloc(cas, 1u, &offset);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_scratch_at(cas, offset)[0] = argument;

    phy_ir_ref call = PHY_IR_NULL;
    const phy_cas_function function = phy_cas_function_id(cas, head);
    if (function == PHY_CAS_FN_SIN) {
        /* d sin(u) = cos(u) */
        status = phy_cas_rebuild_at(
            cas, PHY_IR_FUNCTION, cas->functions[PHY_CAS_FN_COS], offset, 1u,
            out_ref);
        goto done;
    }
    if (function == PHY_CAS_FN_COS) {
        /* d cos(u) = -sin(u) */
        status = phy_cas_rebuild_at(
            cas, PHY_IR_FUNCTION, cas->functions[PHY_CAS_FN_SIN], offset, 1u,
            &call);
        if (status == PHY_OK) {
            status = phy_cas_neg_node(cas, call, out_ref);
        }
        goto done;
    }
    if (function == PHY_CAS_FN_TAN) {
        /*
         * d tan(u) = 1/cos(u)^2. Written as a power rather than as
         * 1 + tan(u)^2 so that a derivative of tan lands on the same sin/cos
         * basis the zero decision reduces to; the alternative form would need
         * the identity applied before anything could cancel against it.
         */
        status = phy_cas_rebuild_at(
            cas, PHY_IR_FUNCTION, cas->functions[PHY_CAS_FN_COS], offset, 1u,
            &call);
        if (status == PHY_OK) {
            phy_ir_ref minus_two;
            status = phy_cas_number_node(cas, (phy_cas_rat){-2, 1}, &minus_two);
            if (status == PHY_OK) {
                status = phy_cas_pow_node(cas, call, minus_two, out_ref);
            }
        }
        goto done;
    }
    if (function == PHY_CAS_FN_EXP) {
        /* d exp(u) = exp(u) */
        status = phy_cas_rebuild_at(
            cas, PHY_IR_FUNCTION, cas->functions[PHY_CAS_FN_EXP], offset, 1u,
            out_ref);
        goto done;
    }
    if (function == PHY_CAS_FN_LOG) {
        /* d log(u) = 1/u */
        status = phy_cas_pow_node(cas, argument, cas->minus_one, out_ref);
        goto done;
    }
    if (function == PHY_CAS_FN_SINH || function == PHY_CAS_FN_COSH) {
        const phy_cas_function result_function =
            function == PHY_CAS_FN_SINH ? PHY_CAS_FN_COSH
                                        : PHY_CAS_FN_SINH;
        status = phy_cas_rebuild_at(
            cas, PHY_IR_FUNCTION, cas->functions[result_function], offset, 1u,
            out_ref);
        goto done;
    }
    if (function == PHY_CAS_FN_TANH) {
        status = phy_cas_rebuild_at(
            cas, PHY_IR_FUNCTION, cas->functions[PHY_CAS_FN_COSH], offset, 1u,
            &call);
        if (status == PHY_OK) {
            phy_ir_ref minus_two = PHY_IR_NULL;
            status =
                phy_cas_number_node(cas, (phy_cas_rat){-2, 1}, &minus_two);
            if (status == PHY_OK) {
                status = phy_cas_pow_node(cas, call, minus_two, out_ref);
            }
        }
        goto done;
    }

    /*
     * Inverse-function derivatives. Square roots use the existing power IR,
     * so the same principal-branch and non-integer-power rules govern every
     * layer.
     */
    if (function == PHY_CAS_FN_ASIN || function == PHY_CAS_FN_ACOS ||
        function == PHY_CAS_FN_ATAN || function == PHY_CAS_FN_ASINH ||
        function == PHY_CAS_FN_ACOSH || function == PHY_CAS_FN_ATANH) {
        phy_ir_ref two = PHY_IR_NULL;
        phy_ir_ref square = PHY_IR_NULL;
        status = phy_cas_number_node(cas, (phy_cas_rat){2, 1}, &two);
        if (status == PHY_OK) {
            status = phy_cas_pow_node(cas, argument, two, &square);
        }

        phy_ir_ref denominator = PHY_IR_NULL;
        if (status == PHY_OK && function == PHY_CAS_FN_ACOSH) {
            phy_ir_ref lower = PHY_IR_NULL;
            phy_ir_ref upper = PHY_IR_NULL;
            phy_ir_ref half = PHY_IR_NULL;
            const phy_ir_ref lower_terms[2] = {argument, cas->minus_one};
            const phy_ir_ref upper_terms[2] = {argument, cas->one};
            status = phy_cas_add_node(cas, lower_terms, 2u, &lower);
            if (status == PHY_OK) {
                status = phy_cas_add_node(cas, upper_terms, 2u, &upper);
            }
            if (status == PHY_OK) {
                status =
                    phy_cas_number_node(cas, (phy_cas_rat){1, 2}, &half);
            }
            phy_ir_ref lower_root = PHY_IR_NULL;
            phy_ir_ref upper_root = PHY_IR_NULL;
            if (status == PHY_OK) {
                status = phy_cas_pow_node(cas, lower, half, &lower_root);
            }
            if (status == PHY_OK) {
                status = phy_cas_pow_node(cas, upper, half, &upper_root);
            }
            if (status == PHY_OK) {
                const phy_ir_ref factors[2] = {lower_root, upper_root};
                status =
                    phy_cas_mul_node(cas, factors, 2u, &denominator);
            }
        } else if (status == PHY_OK) {
            const bool plus =
                function == PHY_CAS_FN_ATAN ||
                function == PHY_CAS_FN_ASINH;
            const phy_ir_ref terms[2] = {
                cas->one,
                square,
            };
            if (plus) {
                status = phy_cas_add_node(cas, terms, 2u, &denominator);
            } else {
                phy_ir_ref negated_square = PHY_IR_NULL;
                status = phy_cas_neg_node(cas, square, &negated_square);
                if (status == PHY_OK) {
                    const phy_ir_ref difference[2] = {
                        cas->one, negated_square};
                    status = phy_cas_add_node(cas, difference, 2u,
                                              &denominator);
                }
            }
            if (status == PHY_OK &&
                (function == PHY_CAS_FN_ASIN ||
                 function == PHY_CAS_FN_ACOS ||
                 function == PHY_CAS_FN_ASINH)) {
                phy_ir_ref half = PHY_IR_NULL;
                status =
                    phy_cas_number_node(cas, (phy_cas_rat){1, 2}, &half);
                if (status == PHY_OK) {
                    status =
                        phy_cas_pow_node(cas, denominator, half, &denominator);
                }
            }
        }
        if (status == PHY_OK) {
            status = phy_cas_pow_node(
                cas, denominator, cas->minus_one, out_ref);
        }
        if (status == PHY_OK && function == PHY_CAS_FN_ACOS) {
            status = phy_cas_neg_node(cas, *out_ref, out_ref);
        }
        goto done;
    }
    if (function == PHY_CAS_FN_GAMMA ||
        function == PHY_CAS_FN_LOGGAMMA) {
        phy_ir_ref digamma = PHY_IR_NULL;
        status = unary_call(
            cas, phy_ir_intern(cas->ir, "digamma"), argument, &digamma);
        if (status == PHY_OK && function == PHY_CAS_FN_GAMMA) {
            phy_ir_ref gamma = PHY_IR_NULL;
            status = unary_call(
                cas, cas->functions[PHY_CAS_FN_GAMMA], argument, &gamma);
            if (status == PHY_OK) {
                const phy_ir_ref factors[2] = {digamma, gamma};
                status = phy_cas_mul_node(cas, factors, 2u, out_ref);
            }
        } else if (status == PHY_OK) {
            *out_ref = digamma;
        }
        goto done;
    }
    if (function == PHY_CAS_FN_ERF || function == PHY_CAS_FN_ERFC) {
        phy_ir_ref two = PHY_IR_NULL;
        phy_ir_ref square = PHY_IR_NULL;
        phy_ir_ref negated_square = PHY_IR_NULL;
        phy_ir_ref exponential = PHY_IR_NULL;
        phy_ir_ref minus_half = PHY_IR_NULL;
        phy_ir_ref pi_factor = PHY_IR_NULL;
        status =
            phy_cas_number_node(cas, (phy_cas_rat){2, 1}, &two);
        if (status == PHY_OK) {
            status = phy_cas_pow_node(cas, argument, two, &square);
        }
        if (status == PHY_OK) {
            status =
                phy_cas_neg_node(cas, square, &negated_square);
        }
        if (status == PHY_OK) {
            status = unary_call(
                cas, cas->functions[PHY_CAS_FN_EXP], negated_square,
                &exponential);
        }
        if (status == PHY_OK) {
            status = phy_cas_number_node(
                cas, (phy_cas_rat){-1, 2}, &minus_half);
        }
        if (status == PHY_OK) {
            status = phy_cas_pow_node(
                cas, cas->constant_pi, minus_half, &pi_factor);
        }
        if (status == PHY_OK) {
            const phy_ir_ref factors[3] = {two, pi_factor, exponential};
            status = phy_cas_mul_node(cas, factors, 3u, out_ref);
        }
        if (status == PHY_OK && function == PHY_CAS_FN_ERFC) {
            status = phy_cas_neg_node(cas, *out_ref, out_ref);
        }
        goto done;
    }
    status = PHY_ERR_UNSUPPORTED;

done:
    phy_cas_scratch_release(cas, mark);
    return status;
}

/* The unevaluated partial derivative, which is the honest answer wherever the
   dependence is not visible. An existing derivative gains a variable rather
   than nesting, which is the ordered form phy_ir_derivative documents. */
static phy_status defer(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                        phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;

    if (phy_ir_kind_of(ir, expr) == PHY_IR_DERIVATIVE) {
        const size_t count = phy_ir_child_count(ir, expr);
        const size_t mark = phy_cas_scratch_mark(cas);
        size_t offset;
        phy_status status = phy_cas_scratch_alloc(cas, count + 1u, &offset);
        if (status == PHY_OK) {
            for (size_t i = 0u; i < count; i++) {
                phy_cas_scratch_at(cas, offset)[i] = phy_ir_child(ir, expr, i);
            }
            phy_cas_scratch_at(cas, offset)[count] = var;
            status = phy_cas_rebuild_at(cas, PHY_IR_DERIVATIVE,
                                        PHY_IR_NO_SYMBOL, offset, count + 1u,
                                        out_ref);
        }
        phy_cas_scratch_release(cas, mark);
        return status;
    }

    const phy_ir_ref derivative = phy_ir_derivative(ir, expr, &var, 1u);
    if (derivative == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = derivative;
    return PHY_OK;
}

/* ------------------------------------------------------------------ rules */

static phy_status diff_sum(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                           phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const size_t count = phy_ir_child_count(ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    phy_status status = phy_cas_scratch_alloc(cas, count, &offset);

    for (size_t i = 0u; i < count && status == PHY_OK; i++) {
        phy_ir_ref term;
        status = phy_cas_diff_node(cas, phy_ir_child(ir, expr, i), var, &term);
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, offset)[i] = term;
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_add_at(cas, offset, count, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

/*
 * The product rule over n factors: one term per factor, in which that factor is
 * replaced by its derivative.
 *
 * Quadratic in the factor count, which the term limit bounds. Differentiating a
 * product of a few thousand factors is not a workload this device has, and the
 * logarithmic-derivative alternative would divide by factors that may vanish.
 */
static phy_status diff_product(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                               phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const size_t count = phy_ir_child_count(ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);

    size_t terms = 0u;
    size_t factors = 0u;
    phy_status status = phy_cas_scratch_alloc(cas, count, &terms);
    if (status == PHY_OK) {
        status = phy_cas_scratch_alloc(cas, count, &factors);
    }

    for (size_t i = 0u; i < count && status == PHY_OK; i++) {
        for (size_t j = 0u; j < count && status == PHY_OK; j++) {
            phy_ir_ref entry = phy_ir_child(ir, expr, j);
            if (i == j) {
                status = phy_cas_diff_node(cas, entry, var, &entry);
            }
            if (status == PHY_OK) {
                phy_cas_scratch_at(cas, factors)[j] = entry;
            }
        }
        if (status == PHY_OK) {
            phy_ir_ref term;
            status = phy_cas_mul_at(cas, factors, count, &term);
            if (status == PHY_OK) {
                phy_cas_scratch_at(cas, terms)[i] = term;
            }
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_add_at(cas, terms, count, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

/*
 * d(u^v) = u^v * (v' * log(u) + v * u'/u), specialized where it can be.
 *
 * The general form is only reached when both parts depend on the variable; a
 * constant exponent takes the power rule, which avoids introducing a log of a
 * base that may be negative.
 */
static phy_status diff_power(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                             phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_ref base = phy_ir_child(ir, expr, 0u);
    const phy_ir_ref exponent = phy_ir_child(ir, expr, 1u);

    bool exponent_varies = false;
    phy_status status = phy_cas_may_depend(cas, exponent, var, &exponent_varies);
    if (status != PHY_OK) {
        return status;
    }

    if (!exponent_varies) {
        /* v * u^(v-1) * u' */
        phy_ir_ref lowered, reduced, derivative;
        const phy_ir_ref lower[2] = {exponent, cas->minus_one};
        status = phy_cas_add_node(cas, lower, 2u, &lowered);
        if (status != PHY_OK) {
            return status;
        }
        status = phy_cas_pow_node(cas, base, lowered, &reduced);
        if (status != PHY_OK) {
            return status;
        }
        status = phy_cas_diff_node(cas, base, var, &derivative);
        if (status != PHY_OK) {
            return status;
        }
        const phy_ir_ref factors[3] = {exponent, reduced, derivative};
        return phy_cas_mul_node(cas, factors, 3u, out_ref);
    }

    bool base_varies = false;
    status = phy_cas_may_depend(cas, base, var, &base_varies);
    if (status != PHY_OK) {
        return status;
    }

    /* v' * log(u), plus v * u'/u when the base varies too. */
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    status = phy_cas_scratch_alloc(cas, 2u, &offset);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref exponent_derivative, logarithm = PHY_IR_NULL,
                                    inner = PHY_IR_NULL;
    size_t used = 0u;
    status = phy_cas_diff_node(cas, exponent, var, &exponent_derivative);
    if (status == PHY_OK) {
        const size_t call = phy_cas_scratch_mark(cas);
        size_t argument;
        status = phy_cas_scratch_alloc(cas, 1u, &argument);
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, argument)[0] = base;
            status = phy_cas_rebuild_at(
                cas, PHY_IR_FUNCTION, cas->functions[PHY_CAS_FN_LOG],
                argument, 1u, &logarithm);
        }
        phy_cas_scratch_release(cas, call);
    }
    if (status == PHY_OK) {
        const phy_ir_ref pair[2] = {exponent_derivative, logarithm};
        phy_ir_ref term;
        status = phy_cas_mul_node(cas, pair, 2u, &term);
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, offset)[used++] = term;
        }
    }
    if (status == PHY_OK && base_varies) {
        phy_ir_ref derivative = PHY_IR_NULL;
        phy_ir_ref inverse = PHY_IR_NULL;
        status = phy_cas_diff_node(cas, base, var, &derivative);
        if (status == PHY_OK) {
            status = phy_cas_pow_node(cas, base, cas->minus_one, &inverse);
        }
        if (status == PHY_OK) {
            const phy_ir_ref triple[3] = {exponent, derivative, inverse};
            phy_ir_ref term;
            status = phy_cas_mul_node(cas, triple, 3u, &term);
            if (status == PHY_OK) {
                phy_cas_scratch_at(cas, offset)[used++] = term;
            }
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_add_at(cas, offset, used, &inner);
    }
    phy_cas_scratch_release(cas, mark);
    if (status != PHY_OK) {
        return status;
    }

    const phy_ir_ref product[2] = {expr, inner};
    return phy_cas_mul_node(cas, product, 2u, out_ref);
}

/* ------------------------------------------------------------- the walk */

phy_status phy_cas_diff_node(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                             phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);

    if (kind == PHY_IR_KIND_INVALID) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (expr == var) {
        *out_ref = cas->one;
        return PHY_OK;
    }
    if (phy_cas_cache_get(cas, PHY_CAS_MEMO_DIFF, expr, var, out_ref, NULL)) {
        return PHY_OK;
    }
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }

    if (kind == PHY_IR_ERROR) {
        /*
         * Differentiating a failed cell must not manufacture a plausible zero.
         * Error atoms are values in the IR and propagate unchanged.
         */
        phy_cas_cache_put(cas, PHY_CAS_MEMO_DIFF, expr, var, expr, PHY_IR_NULL);
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
        /* Provably independent, so exactly zero. Real literals land here as
           constants; error values were propagated above. */
        result = cas->zero;
    } else {
        switch (kind) {
        case PHY_IR_ADD:
            status = diff_sum(cas, expr, var, &result);
            break;
        case PHY_IR_MUL:
            status = diff_product(cas, expr, var, &result);
            break;
        case PHY_IR_POW:
            status = diff_power(cas, expr, var, &result);
            break;
        case PHY_IR_EQUATION: {
            /* Both sides, so that d/dx of an equation stays an equation. */
            const size_t mark = phy_cas_scratch_mark(cas);
            size_t offset;
            status = phy_cas_scratch_alloc(cas, 2u, &offset);
            for (size_t i = 0u; i < 2u && status == PHY_OK; i++) {
                phy_ir_ref side;
                status =
                    phy_cas_diff_node(cas, phy_ir_child(ir, expr, i), var, &side);
                if (status == PHY_OK) {
                    phy_cas_scratch_at(cas, offset)[i] = side;
                }
            }
            if (status == PHY_OK) {
                status = phy_cas_rebuild_at(cas, PHY_IR_EQUATION,
                                            PHY_IR_NO_SYMBOL, offset, 2u,
                                            &result);
            }
            phy_cas_scratch_release(cas, mark);
            break;
        }
        case PHY_IR_FUNCTION: {
            const phy_ir_symbol head = phy_cas_known_function(cas, expr);
            if (head == PHY_IR_NO_SYMBOL) {
                status = defer(cas, expr, var, &result);
                break;
            }
            /* Chain rule. */
            const phy_ir_ref argument = phy_ir_child(ir, expr, 0u);
            phy_ir_ref outer = PHY_IR_NULL;
            phy_ir_ref inner = PHY_IR_NULL;
            status = outer_derivative(cas, head, argument, &outer);
            if (status == PHY_OK) {
                status = phy_cas_diff_node(cas, argument, var, &inner);
            }
            if (status == PHY_OK) {
                const phy_ir_ref pair[2] = {outer, inner};
                status = phy_cas_mul_node(cas, pair, 2u, &result);
            }
            break;
        }
        default:
            /* Tensors, operators, noncommutative products, wedges, and existing
               derivatives: the dependence is real but invisible here. */
            status = defer(cas, expr, var, &result);
            break;
        }
    }

    if (status != PHY_OK) {
        return status;
    }
    phy_cas_cache_put(cas, PHY_CAS_MEMO_DIFF, expr, var, result, PHY_IR_NULL);
    *out_ref = result;
    return PHY_OK;
}

phy_status phy_cas_diff(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
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
    /* An index is not a scalar variable: differentiating with respect to one is
       tensor calculus, and this layer would have to guess what it meant. */
    if (phy_ir_kind_of(cas->ir, var) != PHY_IR_SYMBOL) {
        return PHY_ERR_TYPE;
    }
    const phy_ir_symbol variable = phy_ir_head(cas->ir, var);
    if ((phy_ir_assumptions(cas->ir, variable) &
         (uint32_t)PHY_IR_ASSUME_CONSTANT) != 0u) {
        return PHY_ERR_TYPE;
    }

    phy_ir_ref reduced;
    const phy_status status = phy_cas_simplify_node(cas, expr, &reduced);
    return (status != PHY_OK) ? status
                              : phy_cas_diff_node(cas, reduced, var, out_ref);
}
