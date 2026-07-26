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

static phy_status expand_product(phy_cas *cas, phy_ir_ref expr,
                                 phy_ir_ref *out_ref)
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
        status = phy_cas_expand_node(cas, phy_ir_child(ir, expr, i), &factor);
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

phy_status phy_cas_expand_node(phy_cas *cas, phy_ir_ref expr,
                               phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);

    if (kind == PHY_IR_KIND_INVALID) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_cas_cache_get(cas, PHY_CAS_MEMO_EXPAND, expr, PHY_IR_NULL, out_ref,
                          NULL)) {
        return PHY_OK;
    }
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }
    if ((phy_ir_kind_flags(kind) & PHY_IR_KIND_ATOM) != 0u) {
        *out_ref = expr;
        phy_cas_cache_put(cas, PHY_CAS_MEMO_EXPAND, expr, PHY_IR_NULL, expr,
                          PHY_IR_NULL);
        return PHY_OK;
    }

    phy_ir_ref result = PHY_IR_NULL;

    if (kind == PHY_IR_MUL) {
        status = expand_product(cas, expr, &result);
    } else if (kind == PHY_IR_POW) {
        phy_ir_ref base;
        status = phy_cas_expand_node(cas, phy_ir_child(ir, expr, 0u), &base);
        if (status != PHY_OK) {
            return status;
        }
        int64_t exponent;
        if (phy_ir_kind_of(ir, base) == PHY_IR_ADD &&
            phy_ir_integer_value(ir, phy_ir_child(ir, expr, 1u), &exponent) &&
            exponent != INT64_MIN &&
            (exponent >= 2 || exponent <= -2)) {
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
                status = phy_cas_expand_node(cas, phy_ir_child(ir, expr, i),
                                             &child);
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
    phy_cas_cache_put(cas, PHY_CAS_MEMO_EXPAND, expr, PHY_IR_NULL, result,
                      PHY_IR_NULL);
    *out_ref = result;
    return PHY_OK;
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

    if (sum) {
        const phy_ir_ref left[2] = {n1, d2};
        const phy_ir_ref right[2] = {n2, d1};
        phy_ir_ref a, b;
        if ((status = phy_cas_mul_node(cas, left, 2u, &a)) != PHY_OK ||
            (status = phy_cas_mul_node(cas, right, 2u, &b)) != PHY_OK) {
            return status;
        }
        const phy_ir_ref terms[2] = {a, b};
        status = phy_cas_add_node(cas, terms, 2u, &numerator);
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
    return phy_cas_expand_node(cas, denominator, out_den);
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
                    PHY_OK ||
                (status = phy_cas_expand_node(cas, raised_den, &denominator)) !=
                    PHY_OK) {
                return status;
            }
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

phy_status phy_cas_rational_node(phy_cas *cas, phy_ir_ref expr,
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
    status = polish(cas, numerator, out_numerator);
    if (status != PHY_OK) {
        return status;
    }
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

static phy_status decide(phy_cas *cas, phy_ir_ref expr,
                         phy_cas_decision *out_decision)
{
    phy_ir_ref numerator, denominator;
    const phy_status status =
        phy_cas_rational_node(cas, expr, &numerator, &denominator);
    if (status != PHY_OK) {
        return status;
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
