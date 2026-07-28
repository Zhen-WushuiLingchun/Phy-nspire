/*
 * Phy-nspire — expansion, rational normal form, and the zero decision.
 *
 * This file is where "is this component zero?" becomes a proof rather than an
 * opinion. The chain is four steps, and each one exists because the next one
 * needs it:
 *
 *   1. simplify          collect and fold, so equal terms are equal refs;
 *   2. trigonometry      put tan and multiple angles on one basis, so that
 *                        sin(2*u)/(2*tan(u)) and cos(u)^2 are the same thing;
 *   3. rational form     one expanded numerator over one expanded denominator;
 *   4. cos reduction     cos(u)^2 -> 1 - sin(u)^2, leaving degree at most one
 *                        in each cos.
 *
 * What makes the answer exact is the IR, not this file: a fully expanded,
 * collected polynomial over a fixed set of generators is canonical, and the IR
 * interns it. So two equal polynomials are literally the same ref, and "is the
 * numerator zero" is `num == integer 0`. No tolerance, no sampling, no
 * heuristic -- the property docs/agent-tasks/TENSOR_CORE.md calls load-bearing.
 *
 * Steps 2 and 4 live here rather than in simplify.c on purpose. A notebook cell
 * that reads 1/tan(theta) should still read 1/tan(theta) after simplification;
 * it is only the decision procedure that needs everything on one basis.
 */
#include "cas_internal.h"

/* ------------------------------------------------------------------ expand */

/*
 * Distribute one factor over an accumulated list of terms.
 *
 * `terms` and the result both live in the arena, and the result is a fresh
 * region above it: an in-place update would need the old and new lists at once
 * anyway, and growth would move whichever one was held by pointer.
 */
static phy_status distribute(phy_cas *cas, size_t terms, size_t count,
                             phy_ir_ref factor, size_t *out_offset,
                             size_t *out_count)
{
    phy_ir_context *ir = cas->ir;
    const bool sum = phy_ir_kind_of(ir, factor) == PHY_IR_ADD;
    const size_t parts = sum ? phy_ir_child_count(ir, factor) : 1u;

    if (parts != 0u && count > (size_t)-1 / parts) {
        return PHY_ERR_TERM_LIMIT;
    }
    const size_t product_count = count * parts;
    size_t products;
    phy_status status =
        phy_cas_scratch_alloc(cas, product_count, &products);
    if (status != PHY_OK) {
        return status;
    }

    size_t used = 0u;
    for (size_t i = 0u; i < count; i++) {
        for (size_t j = 0u; j < parts; j++) {
            const phy_ir_ref left = phy_cas_scratch_at(cas, terms)[i];
            const phy_ir_ref right =
                sum ? phy_ir_child(ir, factor, j) : factor;
            const phy_ir_ref pair[2] = {left, right};
            phy_ir_ref product;
            status = phy_cas_mul_node(cas, pair, 2u, &product);
            if (status != PHY_OK) {
                return status;
            }
            phy_cas_scratch_at(cas, products)[used++] = product;
        }
    }
    *out_offset = products;
    *out_count = used;
    return PHY_OK;
}

/*
 * Collect an accumulated term list back into a list of distinct terms.
 *
 * Run between distribution rounds, and it is not an optimization to skip: the
 * cross product of one round is the input to the next, so an uncollected list
 * grows multiplicatively while the collected polynomial does not.
 * (a+b+c+d)^8 has 165 terms, but distributing eight rounds without collecting
 * forms 4^8 = 65,536 of them -- which reaches the term limit and fails an
 * expansion that comfortably fits.
 */
static phy_status recollect(phy_cas *cas, size_t offset, size_t count,
                            size_t *out_offset, size_t *out_count)
{
    phy_ir_ref sum;
    phy_status status = phy_cas_add_at(cas, offset, count, &sum);
    if (status != PHY_OK) {
        return status;
    }

    const bool split = phy_ir_kind_of(cas->ir, sum) == PHY_IR_ADD;
    const size_t terms = split ? phy_ir_child_count(cas->ir, sum) : 1u;
    size_t collected;
    status = phy_cas_scratch_alloc(cas, terms, &collected);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 0u; i < terms; i++) {
        phy_cas_scratch_at(cas, collected)[i] =
            split ? phy_ir_child(cas->ir, sum, i) : sum;
    }
    *out_offset = collected;
    *out_count = terms;
    return PHY_OK;
}

static phy_status expand_walk(phy_cas *cas, phy_ir_ref expr,
                              bool denominators, phy_ir_ref *out_ref);

static phy_status expand_product(phy_cas *cas, phy_ir_ref expr,
                                 bool denominators, phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const size_t count = phy_ir_child_count(ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);

    size_t terms;
    phy_status status = phy_cas_scratch_alloc(cas, 1u, &terms);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_scratch_at(cas, terms)[0] = cas->one;
    size_t remaining = 1u;

    for (size_t i = 0u; i < count; i++) {
        phy_ir_ref factor;
        status = expand_walk(cas, phy_ir_child(ir, expr, i), denominators,
                             &factor);
        if (status != PHY_OK) {
            goto done;
        }
        /*
         * The IR's term limit is the bound on explosion: a product of sums that
         * would exceed it fails as PHY_ERR_TERM_LIMIT here rather than after
         * the device has spent a minute building it.
         */
        status = distribute(cas, terms, remaining, factor, &terms, &remaining);
        if (status != PHY_OK) {
            goto done;
        }
        status = recollect(cas, terms, remaining, &terms, &remaining);
        if (status != PHY_OK) {
            goto done;
        }
    }
    status = phy_cas_add_at(cas, terms, remaining, out_ref);

done:
    phy_cas_scratch_release(cas, mark);
    return status;
}

/*
 * An expanded sum raised to a positive integer power.
 *
 * The terms are distributed one copy of the base at a time, never by forming
 * the product and expanding that. Forming it would not terminate: product
 * collection merges equal factors, so `result * base` with result still equal
 * to base comes back as base^2, and expanding base^2 arrives here again with
 * the arguments it started with.
 */
static phy_status expand_power(phy_cas *cas, phy_ir_ref base, int64_t exponent,
                               phy_ir_ref *out_ref)
{
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t terms;
    phy_status status = phy_cas_scratch_alloc(cas, 1u, &terms);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_scratch_at(cas, terms)[0] = cas->one;
    size_t remaining = 1u;

    for (int64_t i = 0; i < exponent; i++) {
        status = phy_cas_step(cas);
        if (status != PHY_OK) {
            goto done;
        }
        status = distribute(cas, terms, remaining, base, &terms, &remaining);
        if (status != PHY_OK) {
            goto done;
        }
        status = recollect(cas, terms, remaining, &terms, &remaining);
        if (status != PHY_OK) {
            goto done;
        }
    }
    status = phy_cas_add_at(cas, terms, remaining, out_ref);

done:
    phy_cas_scratch_release(cas, mark);
    return status;
}

static phy_status expand_walk(phy_cas *cas, phy_ir_ref expr,
                              bool denominators, phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);
    const phy_cas_memo tag = denominators
                                 ? PHY_CAS_MEMO_EXPAND
                                 : PHY_CAS_MEMO_EXPAND_FACTORED;

    if (kind == PHY_IR_KIND_INVALID) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_cas_cache_get(cas, tag, expr, PHY_IR_NULL, out_ref, NULL)) {
        return PHY_OK;
    }
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }
    if ((phy_ir_kind_flags(kind) & PHY_IR_KIND_ATOM) != 0u) {
        *out_ref = expr;
        phy_cas_cache_put(cas, tag, expr, PHY_IR_NULL, expr, PHY_IR_NULL);
        return PHY_OK;
    }

    phy_ir_ref result = PHY_IR_NULL;

    if (kind == PHY_IR_MUL) {
        status = expand_product(cas, expr, denominators, &result);
    } else if (kind == PHY_IR_POW) {
        phy_ir_ref base;
        status = expand_walk(cas, phy_ir_child(ir, expr, 0u), denominators,
                             &base);
        if (status != PHY_OK) {
            return status;
        }
        int64_t exponent;
        if (phy_ir_kind_of(ir, base) == PHY_IR_ADD &&
            phy_ir_integer_value(ir, phy_ir_child(ir, expr, 1u), &exponent) &&
            exponent != INT64_MIN &&
            (exponent >= 2 || (denominators && exponent <= -2))) {
            /*
             * A negative power expands its magnitude and then inverts, so
             * (x+1)^(-2) becomes 1/(x^2 + 2*x + 1): the rational form wants an
             * expanded polynomial in the denominator just as much as in the
             * numerator.
             */
            const int64_t magnitude = (exponent > 0) ? exponent : -exponent;
            phy_ir_ref power;
            status = expand_power(cas, base, magnitude, &power);
            if (status != PHY_OK) {
                return status;
            }
            if (exponent < 0) {
                status = phy_cas_pow_node(cas, power, cas->minus_one, &result);
            } else {
                result = power;
            }
        } else {
            status = phy_cas_pow_node(cas, base,
                                      phy_ir_child(ir, expr, 1u), &result);
        }
    } else {
        /* Sums, functions, and the opaque kinds: expand the operands and
           rebuild. Function arguments are expanded too, so that sin(x*(y+1))
           and sin(x*y+x) are one generator rather than two. */
        const size_t count = phy_ir_child_count(ir, expr);
        const size_t mark = phy_cas_scratch_mark(cas);
        size_t offset;
        status = phy_cas_scratch_alloc(cas, count, &offset);
        if (status == PHY_OK) {
            for (size_t i = 0u; i < count && status == PHY_OK; i++) {
                phy_ir_ref child;
                status = expand_walk(cas, phy_ir_child(ir, expr, i),
                                     denominators, &child);
                if (status == PHY_OK) {
                    phy_cas_scratch_at(cas, offset)[i] = child;
                }
            }
            if (status == PHY_OK) {
                status = phy_cas_rebuild_at(cas, kind, phy_ir_head(ir, expr),
                                            offset, count, &result);
            }
        }
        phy_cas_scratch_release(cas, mark);
    }

    if (status != PHY_OK) {
        return status;
    }
    phy_cas_cache_put(cas, tag, expr, PHY_IR_NULL, result, PHY_IR_NULL);
    *out_ref = result;
    return PHY_OK;
}

phy_status phy_cas_expand_node(phy_cas *cas, phy_ir_ref expr,
                               phy_ir_ref *out_ref)
{
    return expand_walk(cas, expr, true, out_ref);
}

phy_status phy_cas_expand_factored_node(phy_cas *cas, phy_ir_ref expr,
                                        phy_ir_ref *out_ref)
{
    return expand_walk(cas, expr, false, out_ref);
}

/* -------------------------------------------------------- trigonometry */

/* sin(u) or cos(u) as a call, through the function rules. */
static phy_status trig_call(phy_cas *cas, phy_ir_symbol head, phy_ir_ref argument,
                            phy_ir_ref *out_ref)
{
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    phy_status status = phy_cas_scratch_alloc(cas, 1u, &offset);
    if (status == PHY_OK) {
        phy_cas_scratch_at(cas, offset)[0] = argument;
        status = phy_cas_rebuild_at(cas, PHY_IR_FUNCTION, head, offset, 1u,
                                    out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

static phy_status difference(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                             phy_ir_ref *out_ref)
{
    phy_ir_ref negated;
    const phy_status status = phy_cas_neg_node(cas, right, &negated);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref terms[2] = {left, negated};
    return phy_cas_add_node(cas, terms, 2u, out_ref);
}

/*
 * sin(k*u) and cos(k*u) as polynomials in sin(u) and cos(u), for k >= 2.
 *
 * The angle-addition recurrence, one step per unit of k:
 *
 *     sin((n+1)u) = sin(nu) cos(u) + cos(nu) sin(u)
 *     cos((n+1)u) = cos(nu) cos(u) - sin(nu) sin(u)
 *
 * Chebyshev polynomials in closed form would need binomial coefficients and an
 * alternating sign convention; the recurrence needs neither, and at the k the
 * corpus asks for -- 2 -- it is two multiplications. Each step expands, so the
 * intermediate stays a collected polynomial rather than a nest of products.
 */
static phy_status multiple_angle(phy_cas *cas, phy_ir_symbol head, int64_t k,
                                 phy_ir_ref unit, phy_ir_ref *out_ref)
{
    phy_ir_ref sine, cosine;
    phy_status status = trig_call(cas, cas->fn_sin, unit, &sine);
    if (status != PHY_OK) {
        return status;
    }
    status = trig_call(cas, cas->fn_cos, unit, &cosine);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref sin_n = sine;
    phy_ir_ref cos_n = cosine;
    for (int64_t n = 2; n <= k; n++) {
        status = phy_cas_step(cas);
        if (status != PHY_OK) {
            return status;
        }

        const phy_ir_ref sc[2] = {sin_n, cosine};
        const phy_ir_ref cs[2] = {cos_n, sine};
        const phy_ir_ref cc[2] = {cos_n, cosine};
        const phy_ir_ref ss[2] = {sin_n, sine};

        phy_ir_ref a, b, c, d, next_sin, next_cos;
        if ((status = phy_cas_mul_node(cas, sc, 2u, &a)) != PHY_OK ||
            (status = phy_cas_mul_node(cas, cs, 2u, &b)) != PHY_OK ||
            (status = phy_cas_mul_node(cas, cc, 2u, &c)) != PHY_OK ||
            (status = phy_cas_mul_node(cas, ss, 2u, &d)) != PHY_OK) {
            return status;
        }
        const phy_ir_ref sum[2] = {a, b};
        if ((status = phy_cas_add_node(cas, sum, 2u, &next_sin)) != PHY_OK ||
            (status = difference(cas, c, d, &next_cos)) != PHY_OK ||
            (status = phy_cas_expand_node(cas, next_sin, &sin_n)) != PHY_OK ||
            (status = phy_cas_expand_node(cas, next_cos, &cos_n)) != PHY_OK) {
            return status;
        }
    }

    *out_ref = (head == cas->fn_sin) ? sin_n : cos_n;
    return PHY_OK;
}

/* sin or cos of an argument, expanded when the argument is an integer
   multiple of something. */
static phy_status reduce_angle(phy_cas *cas, phy_ir_symbol head,
                               phy_ir_ref argument, phy_ir_ref *out_ref)
{
    phy_ir_ref coefficient, unit;
    phy_status status =
        phy_cas_split_coefficient(cas, argument, &coefficient, &unit);
    if (status != PHY_OK) {
        return status;
    }

    int64_t k;
    if (!phy_ir_integer_value(cas->ir, coefficient, &k) || k == INT64_MIN) {
        return trig_call(cas, head, argument, out_ref);
    }

    /* Parity has normally folded a negative multiple out already; handling it
       here as well costs one branch and removes the need to rely on that. */
    const bool negate = k < 0 && head == cas->fn_sin;
    const int64_t magnitude = (k < 0) ? -k : k;
    if (magnitude < 2 || magnitude > PHY_CAS_MAX_MULTIPLE_ANGLE) {
        return trig_call(cas, head, argument, out_ref);
    }

    phy_ir_ref expanded;
    status = multiple_angle(cas, head, magnitude, unit, &expanded);
    if (status != PHY_OK) {
        return status;
    }
    if (negate) {
        return phy_cas_neg_node(cas, expanded, out_ref);
    }
    *out_ref = expanded;
    return PHY_OK;
}

/*
 * Put every trigonometric application on the sin/cos basis of a unit argument.
 *
 * tan is rewritten as a quotient, so that the denominator it hides becomes a
 * denominator the rational form can see and cancel: 1/tan(theta) has to reach
 * cos(theta)/sin(theta), which is what the sphere_2d connection in the corpus
 * is compared against.
 */
static phy_status trig_reduce(phy_cas *cas, phy_ir_ref expr, phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);

    if (kind == PHY_IR_KIND_INVALID) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_cas_cache_get(cas, PHY_CAS_MEMO_TRIG, expr, PHY_IR_NULL, out_ref,
                          NULL)) {
        return PHY_OK;
    }
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }
    if ((phy_ir_kind_flags(kind) & PHY_IR_KIND_ATOM) != 0u) {
        *out_ref = expr;
        phy_cas_cache_put(cas, PHY_CAS_MEMO_TRIG, expr, PHY_IR_NULL, expr,
                          PHY_IR_NULL);
        return PHY_OK;
    }

    const size_t count = phy_ir_child_count(ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    status = phy_cas_scratch_alloc(cas, count, &offset);
    if (status != PHY_OK) {
        return status;
    }

    const phy_ir_symbol head = phy_ir_head(ir, expr);
    phy_ir_ref result = PHY_IR_NULL;
    for (size_t i = 0u; i < count; i++) {
        phy_ir_ref child;
        status = trig_reduce(cas, phy_ir_child(ir, expr, i), &child);
        if (status != PHY_OK) {
            goto done;
        }
        phy_cas_scratch_at(cas, offset)[i] = child;
    }

    if (kind == PHY_IR_FUNCTION && count == 1u &&
        (head == cas->fn_sin || head == cas->fn_cos || head == cas->fn_tan)) {
        const phy_ir_ref argument = phy_cas_scratch_at(cas, offset)[0];
        if (head == cas->fn_tan) {
            phy_ir_ref sine, cosine, inverse;
            if ((status = reduce_angle(cas, cas->fn_sin, argument, &sine)) !=
                    PHY_OK ||
                (status = reduce_angle(cas, cas->fn_cos, argument, &cosine)) !=
                    PHY_OK ||
                (status = phy_cas_pow_node(cas, cosine, cas->minus_one,
                                           &inverse)) != PHY_OK) {
                goto done;
            }
            const phy_ir_ref quotient[2] = {sine, inverse};
            status = phy_cas_mul_node(cas, quotient, 2u, &result);
        } else {
            status = reduce_angle(cas, head, argument, &result);
        }
    } else {
        status = phy_cas_rebuild_at(cas, kind, head, offset, count, &result);
    }
    if (status != PHY_OK) {
        goto done;
    }

    phy_cas_cache_put(cas, PHY_CAS_MEMO_TRIG, expr, PHY_IR_NULL, result,
                      PHY_IR_NULL);
    *out_ref = result;

done:
    phy_cas_scratch_release(cas, mark);
    return status;
}

/*
 * cos(u)^k, k >= 2, as (1 - sin(u)^2)^(k/2) * cos(u)^(k mod 2).
 *
 * Applied to an already expanded polynomial, one pass is enough. Each monomial
 * holds a single power of any one cos, because the product collection merged
 * them, so after substitution no monomial has a cos above the first; and
 * re-expanding (1 - sin^2)^m introduces sines only. There is no fixpoint to
 * iterate towards.
 */
static phy_status reduce_cos_powers(phy_cas *cas, phy_ir_ref expr,
                                    phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);

    if ((phy_ir_kind_flags(kind) & PHY_IR_KIND_ATOM) != 0u) {
        *out_ref = expr;
        return PHY_OK;
    }
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }

    if (kind == PHY_IR_POW) {
        const phy_ir_ref base = phy_ir_child(ir, expr, 0u);
        int64_t exponent;
        if (phy_cas_known_function(cas, base) == cas->fn_cos &&
            phy_ir_integer_value(ir, phy_ir_child(ir, expr, 1u), &exponent) &&
            exponent >= 2) {
            phy_ir_ref sine, square, pythagorean, core;
            status = trig_call(cas, cas->fn_sin, phy_ir_child(ir, base, 0u),
                               &sine);
            if (status != PHY_OK) {
                return status;
            }
            phy_ir_ref two;
            status = phy_cas_number_node(cas, (phy_cas_rat){2, 1}, &two);
            if (status != PHY_OK) {
                return status;
            }
            status = phy_cas_pow_node(cas, sine, two, &square);
            if (status != PHY_OK) {
                return status;
            }
            status = difference(cas, cas->one, square, &pythagorean);
            if (status != PHY_OK) {
                return status;
            }
            phy_ir_ref half;
            status = phy_cas_number_node(cas, (phy_cas_rat){exponent / 2, 1},
                                         &half);
            if (status != PHY_OK) {
                return status;
            }
            status = phy_cas_pow_node(cas, pythagorean, half, &core);
            if (status != PHY_OK) {
                return status;
            }
            if ((exponent % 2) != 0) {
                const phy_ir_ref pair[2] = {core, base};
                return phy_cas_mul_node(cas, pair, 2u, out_ref);
            }
            *out_ref = core;
            return PHY_OK;
        }
    }

    const size_t count = phy_ir_child_count(ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    status = phy_cas_scratch_alloc(cas, count, &offset);
    if (status == PHY_OK) {
        for (size_t i = 0u; i < count && status == PHY_OK; i++) {
            phy_ir_ref child;
            status = reduce_cos_powers(cas, phy_ir_child(ir, expr, i), &child);
            if (status == PHY_OK) {
                phy_cas_scratch_at(cas, offset)[i] = child;
            }
        }
        if (status == PHY_OK) {
            status = phy_cas_rebuild_at(cas, kind, phy_ir_head(ir, expr), offset,
                                        count, out_ref);
        }
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

/* A polynomial part of the rational form: expanded, cos-reduced, expanded. */
static phy_status polish(phy_cas *cas, phy_ir_ref poly, phy_ir_ref *out_ref)
{
    phy_ir_ref expanded, reduced;
    phy_status status = phy_cas_expand_node(cas, poly, &expanded);
    if (status != PHY_OK) {
        return status;
    }
    status = reduce_cos_powers(cas, expanded, &reduced);
    if (status != PHY_OK) {
        return status;
    }
    return phy_cas_expand_node(cas, reduced, out_ref);
}

/* --------------------------------------------------------- rational form */

/* n1/d1 + n2/d2 and n1/d1 * n2/d2, with both parts kept expanded. */
static phy_status combine(phy_cas *cas, bool sum, phy_ir_ref n1, phy_ir_ref d1,
                          phy_ir_ref n2, phy_ir_ref d2, phy_ir_ref *out_num,
                          phy_ir_ref *out_den)
{
    phy_status status;
    phy_ir_ref numerator;

    if (sum && d1 == d2) {
        /*
         * Already over a common denominator, so use it rather than its square.
         *
         * Not an optimization. A curvature component is a sum of many terms
         * that came from the same metric and therefore carry the same
         * denominator; multiplying it in once per term raises the degree by a
         * factor of the term count and the coefficients with it, and the exact
         * int64 arithmetic then overflows deciding an expression whose real
         * denominator never grew at all. Interning makes "the same
         * denominator" a ref comparison, so this costs nothing to ask.
         */
        const phy_ir_ref terms[2] = {n1, n2};
        status = phy_cas_add_node(cas, terms, 2u, &numerator);
        if (status != PHY_OK) {
            return status;
        }
        status = phy_cas_expand_node(cas, numerator, out_num);
        if (status != PHY_OK) {
            return status;
        }
        *out_den = d1;
        return PHY_OK;
    }

    if (sum) {
        /*
         * Different denominators still share almost every factor when the
         * terms came from one metric: combining over the least common
         * denominator instead of the product is what keeps a seventeen-term
         * curvature invariant at the degree of its true denominator instead
         * of the sum of all of them.
         */
        return phy_cas_combine_sum_lcd(cas, n1, d1, n2, d2, out_num,
                                       out_den);
    } else {
        const phy_ir_ref factors[2] = {n1, n2};
        status = phy_cas_mul_node(cas, factors, 2u, &numerator);
    }
    if (status != PHY_OK) {
        return status;
    }

    const phy_ir_ref denominators[2] = {d1, d2};
    phy_ir_ref denominator;
    status = phy_cas_mul_node(cas, denominators, 2u, &denominator);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_cas_expand_node(cas, numerator, out_num);
    if (status != PHY_OK) {
        return status;
    }
    /* The product already folded equal bases; keep it factored. */
    *out_den = denominator;
    return PHY_OK;
}

static phy_status rational_walk(phy_cas *cas, phy_ir_ref expr,
                                phy_ir_ref *out_num, phy_ir_ref *out_den)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);

    if (kind == PHY_IR_KIND_INVALID) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_cas_cache_get(cas, PHY_CAS_MEMO_RATIONAL, expr, PHY_IR_NULL, out_num,
                          out_den)) {
        return PHY_OK;
    }
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref numerator = expr;
    phy_ir_ref denominator = cas->one;

    int64_t rational_numerator, rational_denominator;
    if (phy_ir_rational_value(ir, expr, &rational_numerator,
                              &rational_denominator)) {
        /* An exact fraction is already a quotient; splitting it here keeps the
           numerator polynomial over the integers. */
        numerator = phy_ir_integer(ir, rational_numerator);
        denominator = phy_ir_integer(ir, rational_denominator);
        if (numerator == PHY_IR_NULL || denominator == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
    } else if (kind == PHY_IR_ADD || kind == PHY_IR_MUL) {
        const size_t count = phy_ir_child_count(ir, expr);
        numerator = (kind == PHY_IR_ADD) ? cas->zero : cas->one;
        for (size_t i = 0u; i < count; i++) {
            phy_ir_ref child_num, child_den;
            status = rational_walk(cas, phy_ir_child(ir, expr, i), &child_num,
                                   &child_den);
            if (status != PHY_OK) {
                return status;
            }
            status = combine(cas, kind == PHY_IR_ADD, numerator, denominator,
                             child_num, child_den, &numerator, &denominator);
            if (status != PHY_OK) {
                return status;
            }
        }
    } else if (kind == PHY_IR_POW) {
        int64_t exponent;
        if (phy_ir_integer_value(ir, phy_ir_child(ir, expr, 1u), &exponent) &&
            exponent != INT64_MIN) {
            phy_ir_ref base_num, base_den;
            status = rational_walk(cas, phy_ir_child(ir, expr, 0u), &base_num,
                                   &base_den);
            if (status != PHY_OK) {
                return status;
            }
            /* A negative exponent is the same work with the parts swapped,
               which is the only place a denominator is created. */
            const bool inverted = exponent < 0;
            const int64_t magnitude = inverted ? -exponent : exponent;
            phy_ir_ref power;
            status = phy_cas_number_node(cas, (phy_cas_rat){magnitude, 1},
                                         &power);
            if (status != PHY_OK) {
                return status;
            }
            phy_ir_ref raised_num, raised_den;
            if ((status = phy_cas_pow_node(cas, inverted ? base_den : base_num,
                                           power, &raised_num)) != PHY_OK ||
                (status = phy_cas_pow_node(cas, inverted ? base_num : base_den,
                                           power, &raised_den)) != PHY_OK) {
                return status;
            }
            if ((status = phy_cas_expand_node(cas, raised_num, &numerator)) !=
                PHY_OK) {
                return status;
            }
            /*
             * The raised denominator stays a power of its base. Expanding
             * (r-rs)^3 into a cubic makes it a different polynomial from the
             * (r-rs)^2 of the next term, and the least common denominator
             * then degenerates back into the product of both.
             */
            denominator = raised_den;
        }
        /*
         * A non-integer exponent stays whole: it is a generator. INT64_MIN
         * does too, because its positive magnitude is not representable in
         * int64_t; keeping the original power is exact and avoids undefined
         * signed negation.
         */
    }
    /* Everything else -- symbols, functions, tensors, reals, error values -- is
       a generator, and generators are their own numerator. */

    phy_cas_cache_put(cas, PHY_CAS_MEMO_RATIONAL, expr, PHY_IR_NULL, numerator,
                      denominator);
    *out_num = numerator;
    *out_den = denominator;
    return PHY_OK;
}

/*
 * The reduced rational pair: polished numerator over the factored
 * denominator that remains after exact cancellation. The denominator keeps
 * its factored shape -- rq^6 rather than the expanded polynomial -- because
 * that is both what cancellation consumes and what a reader wants shown.
 */
phy_status phy_cas_rational_reduced_node(phy_cas *cas, phy_ir_ref expr,
                                         phy_ir_ref *out_numerator,
                                         phy_ir_ref *out_denominator)
{
    phy_ir_ref reduced, based;
    phy_status status = phy_cas_simplify_node(cas, expr, &reduced);
    if (status != PHY_OK) {
        return status;
    }
    status = trig_reduce(cas, reduced, &based);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref numerator, denominator;
    status = rational_walk(cas, based, &numerator, &denominator);
    if (status != PHY_OK) {
        return status;
    }
    status = polish(cas, numerator, &numerator);
    if (status != PHY_OK) {
        return status;
    }
    return phy_cas_cancel_known_factors(cas, numerator, denominator,
                                        out_numerator, out_denominator);
}

phy_status phy_cas_rational_node(phy_cas *cas, phy_ir_ref expr,
                                 phy_ir_ref *out_numerator,
                                 phy_ir_ref *out_denominator)
{
    phy_ir_ref numerator, denominator;
    phy_status status = phy_cas_rational_reduced_node(cas, expr, &numerator,
                                                      &denominator);
    if (status != PHY_OK) {
        return status;
    }
    *out_numerator = numerator;
    status = polish(cas, denominator, out_denominator);
    if (status != PHY_OK) {
        return status;
    }

    /*
     * A denominator that reduces to zero means the expression is defined
     * nowhere -- 1/(sin(u)^2 + cos(u)^2 - 1) is the compact example. That is a
     * domain error and not an answer about the numerator.
     */
    if (phy_cas_is_integer(cas, *out_denominator, 0)) {
        return PHY_ERR_DOMAIN;
    }
    return PHY_OK;
}

/* ------------------------------------------------------------- decisions */

/*
 * Rational zero testing needs to know whether a denominator is identically
 * zero, not to expand every positive power in that denominator.  Walk the
 * product structure and polish only an irreducible base.  This preserves the
 * 1/(sin(x)^2+cos(x)^2-1) domain check while avoiding degree explosions such
 * as (r^2-2 M r+Q^2)^16 in a curvature calculation.
 */
static phy_status denominator_zero(phy_cas *cas, phy_ir_ref expr,
                                   bool *out_zero)
{
    phy_ir_context *ir = cas->ir;
    phy_ir_ref reduced = PHY_IR_NULL;
    phy_status status = phy_cas_simplify_node(cas, expr, &reduced);
    if (status != PHY_OK) {
        return status;
    }
    if (phy_cas_is_integer(cas, reduced, 0)) {
        *out_zero = true;
        return PHY_OK;
    }
    const phy_ir_kind kind = phy_ir_kind_of(ir, reduced);
    if (kind == PHY_IR_MUL) {
        const size_t count = phy_ir_child_count(ir, reduced);
        for (size_t index = 0u; index < count; ++index) {
            bool child_zero = false;
            status = denominator_zero(cas, phy_ir_child(ir, reduced, index),
                                      &child_zero);
            if (status != PHY_OK || child_zero) {
                *out_zero = child_zero;
                return status;
            }
        }
        *out_zero = false;
        return PHY_OK;
    }
    if (kind == PHY_IR_POW) {
        int64_t exponent = 0;
        if (phy_ir_integer_value(ir, phy_ir_child(ir, reduced, 1u),
                                 &exponent) &&
            exponent > 0) {
            return denominator_zero(cas, phy_ir_child(ir, reduced, 0u),
                                    out_zero);
        }
    }

    phy_ir_ref polished = PHY_IR_NULL;
    status = polish(cas, reduced, &polished);
    if (status == PHY_OK) {
        *out_zero = phy_cas_is_integer(cas, polished, 0);
    }
    return status;
}

static bool first_negative_power(const phy_ir_context *ir, phy_ir_ref expr,
                                 phy_ir_ref *out_base)
{
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);
    if (kind == PHY_IR_POW) {
        int64_t exponent = 0;
        if (phy_ir_integer_value(ir, phy_ir_child(ir, expr, 1u), &exponent) &&
            exponent < 0 && exponent != INT64_MIN) {
            *out_base = phy_ir_child(ir, expr, 0u);
            return true;
        }
        return false;
    }
    if (kind != PHY_IR_ADD && kind != PHY_IR_MUL) {
        return false;
    }
    const size_t count = phy_ir_child_count(ir, expr);
    for (size_t index = 0u; index < count; ++index) {
        if (first_negative_power(ir, phy_ir_child(ir, expr, index),
                                 out_base)) {
            return true;
        }
    }
    return false;
}

static void minimum_power_of(const phy_ir_context *ir, phy_ir_ref expr,
                             phy_ir_ref base, int64_t *minimum)
{
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);
    if (kind == PHY_IR_POW &&
        phy_ir_child(ir, expr, 0u) == base) {
        int64_t exponent = 0;
        if (phy_ir_integer_value(ir, phy_ir_child(ir, expr, 1u), &exponent) &&
            exponent < *minimum) {
            *minimum = exponent;
        }
        return;
    }
    if (kind != PHY_IR_ADD && kind != PHY_IR_MUL) {
        return;
    }
    const size_t count = phy_ir_child_count(ir, expr);
    for (size_t index = 0u; index < count; ++index) {
        minimum_power_of(ir, phy_ir_child(ir, expr, index), base, minimum);
    }
}

/*
 * Multiply a sum by one exact factor without expanding the factor itself.
 *
 * This is deliberately narrower than expand_factored().  The latter expands
 * positive powers, so using it to clear B^-k with B^k first turns the
 * clearing factor into an expanded polynomial and loses the structural base
 * equality needed by the product collector.  Distributing only over the
 * outer sum lets B^-k * B^k cancel before any polynomial expansion.
 */
static phy_status multiply_sum_factored(phy_cas *cas, phy_ir_ref expr,
                                        phy_ir_ref factor,
                                        phy_ir_ref *out_ref)
{
    const bool sum = phy_ir_kind_of(cas->ir, expr) == PHY_IR_ADD;
    if (!sum) {
        const phy_ir_ref factors[2] = {expr, factor};
        return phy_cas_mul_node(cas, factors, 2u, out_ref);
    }

    const size_t count = phy_ir_child_count(cas->ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t terms = 0u;
    phy_status status = phy_cas_scratch_alloc(cas, count, &terms);
    for (size_t index = 0u; index < count && status == PHY_OK; ++index) {
        const phy_ir_ref factors[2] = {
            phy_ir_child(cas->ir, expr, index), factor};
        phy_ir_ref product = PHY_IR_NULL;
        status = phy_cas_mul_node(cas, factors, 2u, &product);
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, terms)[index] = product;
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_add_at(cas, terms, count, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

/*
 * Resource-bounded fallback for rational identities.
 *
 * Instead of constructing one enormous common denominator, clear one
 * denominator base at a time.  expand_factored distributes the positive
 * clearing factor while retaining every unrelated negative power, so each
 * intermediate is collected before the next base is introduced.
 */
static phy_status decide_by_clearing_denominators(
    phy_cas *cas, phy_ir_ref expr, phy_cas_decision *out_decision)
{
    phy_ir_ref current = PHY_IR_NULL;
    phy_status status =
        phy_cas_expand_factored_node(cas, expr, &current);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref previous = PHY_IR_NULL;
    for (unsigned pass = 0u; pass < 32u; ++pass) {
        /*
         * A pass that leaves the expression untouched will leave every later
         * pass untouched too; burning the remaining budget on it would only
         * delay the same honest give-up.
         */
        if (current == previous) {
            break;
        }
        previous = current;
        phy_ir_ref base = PHY_IR_NULL;
        if (!first_negative_power(cas->ir, current, &base)) {
            phy_ir_ref expanded = PHY_IR_NULL;
            status = polish(cas, current, &expanded);
            if (status != PHY_OK) {
                return status;
            }
            if (phy_cas_is_integer(cas, expanded, 0)) {
                *out_decision = PHY_CAS_ZERO;
            } else if (phy_cas_known_nonzero(cas, expanded)) {
                *out_decision = PHY_CAS_NONZERO;
            } else {
                *out_decision = PHY_CAS_UNKNOWN;
            }
            return PHY_OK;
        }
        bool base_zero = false;
        status = denominator_zero(cas, base, &base_zero);
        if (status != PHY_OK) {
            return status;
        }
        if (base_zero) {
            return PHY_ERR_DOMAIN;
        }

        int64_t minimum = 0;
        minimum_power_of(cas->ir, current, base, &minimum);
        if (minimum >= 0 || minimum == INT64_MIN) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        phy_ir_ref exponent = PHY_IR_NULL;
        status = phy_cas_number_node(
            cas, (phy_cas_rat){-minimum, 1}, &exponent);
        if (status != PHY_OK) {
            return status;
        }
        phy_ir_ref clearing = PHY_IR_NULL;
        status = phy_cas_pow_node(cas, base, exponent, &clearing);
        if (status != PHY_OK) {
            return status;
        }
        status = multiply_sum_factored(cas, current, clearing, &current);
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_ERR_TERM_LIMIT;
}

static bool decision_resource_failure(phy_status status)
{
    return status == PHY_ERR_OVERFLOW ||
           status == PHY_ERR_TIMEOUT ||
           status == PHY_ERR_NODE_LIMIT ||
           status == PHY_ERR_DEPTH_LIMIT ||
           status == PHY_ERR_TERM_LIMIT ||
           status == PHY_ERR_MEMORY_LIMIT ||
           status == PHY_ERR_OUT_OF_MEMORY;
}

static phy_status decide(phy_cas *cas, phy_ir_ref expr,
                         phy_cas_decision *out_decision)
{
    phy_ir_ref reduced = PHY_IR_NULL;
    phy_ir_ref based = PHY_IR_NULL;
    phy_ir_ref numerator = PHY_IR_NULL;
    phy_ir_ref denominator = PHY_IR_NULL;
    phy_status status = phy_cas_simplify_node(cas, expr, &reduced);
    if (status == PHY_OK) {
        status = trig_reduce(cas, reduced, &based);
    }
    /*
     * A factored quotient tries the cheap proof first: clearing the visible
     * denominator factors one at a time allocates little and settles most
     * curvature components. Only a question it leaves undecided -- its
     * clearing loop is guarded against treading water -- is worth the
     * common-denominator walk below, which is stronger but interns its
     * intermediate polynomials in an IR that cannot reclaim them.
     */
    phy_ir_ref negative_base = PHY_IR_NULL;
    if (status == PHY_OK &&
        first_negative_power(cas->ir, based, &negative_base)) {
        const phy_status cleared =
            decide_by_clearing_denominators(cas, based, out_decision);
        if (cleared == PHY_OK && *out_decision != PHY_CAS_UNKNOWN) {
            return PHY_OK;
        }
        if (cleared != PHY_OK && !decision_resource_failure(cleared)) {
            return cleared;
        }
        *out_decision = PHY_CAS_UNKNOWN;
        /*
         * Escalating interns every intermediate polynomial permanently, so a
         * question too large to answer would still leave its working set in
         * the IR and starve whatever runs after it. Size-gate the escalation
         * and let a big undecided expression stay honestly undecided.
         */
        char probe[1];
        size_t length = 0u;
        (void)phy_ir_write(cas->ir, based, probe, sizeof probe, &length);
        if (length > 512u) {
            return PHY_OK;
        }
        phy_cas_cache_clear(cas);
    }
    if (status == PHY_OK) {
        status = rational_walk(cas, based, &numerator, &denominator);
    }
    if (status == PHY_OK) {
        status = polish(cas, numerator, &numerator);
    }
    if (status != PHY_OK) {
        if (!decision_resource_failure(status)) {
            return status;
        }
        if (based == PHY_IR_NULL) {
            return status;
        }
        phy_cas_cache_clear(cas);
        return decide_by_clearing_denominators(cas, based, out_decision);
    }
    bool denominator_is_zero = false;
    status = denominator_zero(cas, denominator, &denominator_is_zero);
    if (status != PHY_OK) {
        if (decision_resource_failure(status) && based != PHY_IR_NULL) {
            phy_cas_cache_clear(cas);
            return decide_by_clearing_denominators(cas, based, out_decision);
        }
        return status;
    }
    if (denominator_is_zero) {
        return PHY_ERR_DOMAIN;
    }

    if (phy_cas_is_integer(cas, numerator, 0)) {
        *out_decision = PHY_CAS_ZERO;
    } else if (phy_cas_known_nonzero(cas, numerator)) {
        /* Exact nonzero numbers, declared-nonzero symbols, products of those,
           and exp() -- see phy_cas_known_nonzero. */
        *out_decision = PHY_CAS_NONZERO;
    } else {
        *out_decision = PHY_CAS_UNKNOWN;
    }
    return PHY_OK;
}

/* --------------------------------------------------------- public surface */

phy_status phy_cas_expand(phy_cas *cas, phy_ir_ref expr, phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_cas_begin(cas);

    phy_ir_ref reduced;
    const phy_status status = phy_cas_simplify_node(cas, expr, &reduced);
    return (status != PHY_OK) ? status
                              : phy_cas_expand_node(cas, reduced, out_ref);
}

phy_status phy_cas_expand_factored(phy_cas *cas, phy_ir_ref expr,
                                   phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_cas_begin(cas);

    phy_ir_ref reduced;
    const phy_status status = phy_cas_simplify_node(cas, expr, &reduced);
    return (status != PHY_OK)
               ? status
               : phy_cas_expand_factored_node(cas, reduced, out_ref);
}

phy_status phy_cas_rational_form(phy_cas *cas, phy_ir_ref expr,
                                 phy_ir_ref *out_numerator,
                                 phy_ir_ref *out_denominator)
{
    if (cas == NULL || out_numerator == NULL || out_denominator == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_numerator = PHY_IR_NULL;
    *out_denominator = PHY_IR_NULL;
    phy_cas_begin(cas);
    return phy_cas_rational_node(cas, expr, out_numerator, out_denominator);
}

phy_status phy_cas_is_zero(phy_cas *cas, phy_ir_ref expr,
                           phy_cas_decision *out_decision)
{
    if (cas == NULL || out_decision == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_decision = PHY_CAS_UNKNOWN;
    phy_cas_begin(cas);
    return decide(cas, expr, out_decision);
}

phy_status phy_cas_equivalent(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                              phy_cas_decision *out_decision)
{
    if (cas == NULL || out_decision == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_decision = PHY_CAS_UNKNOWN;
    phy_cas_begin(cas);

    phy_ir_ref reduced_left, reduced_right, gap;
    phy_status status = phy_cas_simplify_node(cas, left, &reduced_left);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_cas_simplify_node(cas, right, &reduced_right);
    if (status != PHY_OK) {
        return status;
    }
    /* Interning gives the common case for free: expressions that normalized to
       the same node are equal without a rational form. */
    if (reduced_left == reduced_right) {
        *out_decision = PHY_CAS_ZERO;
        return PHY_OK;
    }
    status = difference(cas, reduced_left, reduced_right, &gap);
    return (status != PHY_OK) ? status : decide(cas, gap, out_decision);
}
