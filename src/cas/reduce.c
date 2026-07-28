/*
 * Phy-nspire — least common denominators and exact cancellation.
 *
 * The rational walk in normal.c turns an expression into one numerator over
 * one denominator. Two things make that pair fit a calculator instead of a
 * workstation:
 *
 *   1. Sums combine over the least common denominator, not the product of
 *      denominators. Seventeen curvature terms over powers of r and (r - rs)
 *      share almost every factor; multiplying all seventeen denominators
 *      produced degree-135 polynomials whose coefficients left int64.
 *
 *   2. The numerator is divided exactly by the denominator's known factors.
 *      The denominator arrives factored, so no factorization is ever
 *      attempted: each factor is tried by synchronous polynomial division
 *      and kept only if the remainder vanishes. This is the bounded core of
 *      what a desktop CAS calls Cancel; full multivariate GCD is not needed
 *      to reduce R_{abcd}R^{abcd} to 12 rs^2/r^6.
 *
 * Division picks a main variable whose leading coefficient in the divisor is
 * an exact number, so coefficient division is exact rational arithmetic and
 * never recurses into polynomial division of coefficients.
 */
#include "cas_internal.h"

#define REDUCE_MAX_FACTORS 24u
#define REDUCE_MAX_DEGREE 48u
#define REDUCE_MAX_CANDIDATES 8u

typedef struct {
    phy_cas_rat coefficient;
    size_t count;
    phy_ir_ref bases[REDUCE_MAX_FACTORS];
    int64_t exponents[REDUCE_MAX_FACTORS];
} reduce_factors;

static int64_t positive_gcd(int64_t a, int64_t b)
{
    while (b != 0) {
        const int64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

static bool checked_gcd_lcm(int64_t a, int64_t b, int64_t *out_lcm)
{
    if (a <= 0 || b <= 0) {
        return false;
    }
    const int64_t reduced = a / positive_gcd(a, b);
    if (reduced > INT64_MAX / b) {
        return false;
    }
    *out_lcm = reduced * b;
    return true;
}

/* a / b as an exact rational, with the sign carried by the numerator. */
static bool rat_divide(phy_cas_rat a, phy_cas_rat b, phy_cas_rat *out)
{
    if (b.num == 0) {
        return false;
    }
    const phy_cas_rat reciprocal = {
        b.num > 0 ? b.den : -b.den,
        b.num > 0 ? b.num : -b.num,
    };
    return phy_cas_rat_mul(a, reciprocal, out);
}

static bool factors_push(reduce_factors *factors, phy_ir_ref base,
                         int64_t exponent)
{
    for (size_t i = 0u; i < factors->count; ++i) {
        if (factors->bases[i] == base) {
            if (factors->exponents[i] > INT64_MAX - exponent) {
                return false;
            }
            factors->exponents[i] += exponent;
            return true;
        }
    }
    if (factors->count >= REDUCE_MAX_FACTORS) {
        return false;
    }
    factors->bases[factors->count] = base;
    factors->exponents[factors->count] = exponent;
    factors->count++;
    return true;
}

static bool factors_push_node(phy_cas *cas, reduce_factors *factors,
                              phy_ir_ref node)
{
    phy_cas_rat value;
    if (phy_cas_exact_value(cas, node, &value)) {
        return phy_cas_rat_mul(factors->coefficient, value,
                               &factors->coefficient);
    }
    if (phy_ir_kind_of(cas->ir, node) == PHY_IR_POW) {
        int64_t exponent = 0;
        if (phy_ir_integer_value(cas->ir,
                                 phy_ir_child(cas->ir, node, 1u),
                                 &exponent) &&
            exponent > 0) {
            return factors_push(
                factors, phy_ir_child(cas->ir, node, 0u), exponent);
        }
    }
    return factors_push(factors, node, 1);
}

/*
 * A denominator produced by the rational walk: an exact coefficient times a
 * product of positive integer powers of expanded polynomial bases.
 */
static bool factors_of(phy_cas *cas, phy_ir_ref denominator,
                       reduce_factors *out_factors)
{
    out_factors->coefficient = (phy_cas_rat){1, 1};
    out_factors->count = 0u;
    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, denominator);
    if (kind != PHY_IR_MUL) {
        return factors_push_node(cas, out_factors, denominator);
    }
    const size_t count = phy_ir_child_count(cas->ir, denominator);
    for (size_t i = 0u; i < count; ++i) {
        if (!factors_push_node(cas, out_factors,
                               phy_ir_child(cas->ir, denominator,
                                            i))) {
            return false;
        }
    }
    return true;
}

static int64_t factors_exponent_of(const reduce_factors *factors,
                                   phy_ir_ref base)
{
    for (size_t i = 0u; i < factors->count; ++i) {
        if (factors->bases[i] == base) {
            return factors->exponents[i];
        }
    }
    return 0;
}

static phy_status factors_build(phy_cas *cas, const reduce_factors *factors,
                                bool invert, phy_ir_ref *out_ref)
{
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    phy_status status =
        phy_cas_scratch_alloc(cas, factors->count + 1u, &offset);
    if (status != PHY_OK) {
        return status;
    }
    size_t used = 0u;
    phy_cas_rat coefficient = factors->coefficient;
    if (invert) {
        const phy_cas_rat flipped = {coefficient.den, coefficient.num};
        coefficient = flipped;
    }
    phy_ir_ref number = PHY_IR_NULL;
    status = phy_cas_number_node(cas, coefficient, &number);
    if (status == PHY_OK && !phy_cas_is_integer(cas, number, 1)) {
        phy_cas_scratch_at(cas, offset)[used++] = number;
    }
    for (size_t i = 0u; i < factors->count && status == PHY_OK; ++i) {
        if (factors->exponents[i] == 0) {
            continue;
        }
        phy_ir_ref exponent = PHY_IR_NULL;
        status = phy_cas_number_node(
            cas,
            (phy_cas_rat){invert ? -factors->exponents[i]
                                 : factors->exponents[i],
                          1},
            &exponent);
        if (status != PHY_OK) {
            break;
        }
        phy_ir_ref power = PHY_IR_NULL;
        status = phy_cas_pow_node(cas, factors->bases[i], exponent, &power);
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, offset)[used++] = power;
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_mul_at(cas, offset, used, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

/* --------------------------------------------------- least common form */

phy_status phy_cas_combine_sum_lcd(phy_cas *cas, phy_ir_ref n1, phy_ir_ref d1,
                                   phy_ir_ref n2, phy_ir_ref d2,
                                   phy_ir_ref *out_num, phy_ir_ref *out_den)
{
    reduce_factors left;
    reduce_factors right;
    if (!factors_of(cas, d1, &left) || !factors_of(cas, d2, &right)) {
        return PHY_ERR_TERM_LIMIT;
    }

    /*
     * The union keeps each base at its larger exponent; each side's
     * multiplier carries only what its own denominator is missing.
     */
    reduce_factors common = left;
    for (size_t i = 0u; i < right.count; ++i) {
        const int64_t have = factors_exponent_of(&common, right.bases[i]);
        if (right.exponents[i] > have &&
            !factors_push(&common, right.bases[i],
                          right.exponents[i] - have)) {
            return PHY_ERR_TERM_LIMIT;
        }
    }
    int64_t lcm = 0;
    if (!checked_gcd_lcm(left.coefficient.num > 0 ? left.coefficient.num
                                                  : -left.coefficient.num,
                         right.coefficient.num > 0 ? right.coefficient.num
                                                   : -right.coefficient.num,
                         &lcm)) {
        return PHY_ERR_OVERFLOW;
    }
    common.coefficient = (phy_cas_rat){
        lcm, positive_gcd(left.coefficient.den, right.coefficient.den)};

    reduce_factors fill = common;
    phy_ir_ref multiplier_left = PHY_IR_NULL;
    phy_ir_ref multiplier_right = PHY_IR_NULL;
    for (size_t i = 0u; i < fill.count; ++i) {
        fill.exponents[i] =
            common.exponents[i] - factors_exponent_of(&left, fill.bases[i]);
    }
    if (!rat_divide(common.coefficient, left.coefficient,
                    &fill.coefficient)) {
        return PHY_ERR_OVERFLOW;
    }
    phy_status status = factors_build(cas, &fill, false, &multiplier_left);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 0u; i < fill.count; ++i) {
        fill.exponents[i] =
            common.exponents[i] - factors_exponent_of(&right, fill.bases[i]);
    }
    if (!rat_divide(common.coefficient, right.coefficient,
                    &fill.coefficient)) {
        return PHY_ERR_OVERFLOW;
    }
    status = factors_build(cas, &fill, false, &multiplier_right);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref scaled_left = PHY_IR_NULL;
    phy_ir_ref scaled_right = PHY_IR_NULL;
    const phy_ir_ref left_pair[2] = {n1, multiplier_left};
    const phy_ir_ref right_pair[2] = {n2, multiplier_right};
    if ((status = phy_cas_mul_node(cas, left_pair, 2u, &scaled_left)) !=
            PHY_OK ||
        (status = phy_cas_mul_node(cas, right_pair, 2u, &scaled_right)) !=
            PHY_OK) {
        return status;
    }
    const phy_ir_ref terms[2] = {scaled_left, scaled_right};
    phy_ir_ref numerator = PHY_IR_NULL;
    status = phy_cas_add_node(cas, terms, 2u, &numerator);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_cas_expand_node(cas, numerator, out_num);
    if (status != PHY_OK) {
        return status;
    }
    return factors_build(cas, &common, false, out_den);
}

/* ------------------------------------------------------ exact division */

/*
 * The exponent of `variable` in one expanded term, and the term with that
 * power removed.
 */
static phy_status term_power_of(phy_cas *cas, phy_ir_ref term,
                                phy_ir_ref variable, int64_t *out_exponent,
                                phy_ir_ref *out_rest)
{
    phy_ir_context *ir = cas->ir;
    *out_exponent = 0;
    *out_rest = term;
    if (term == variable) {
        *out_exponent = 1;
        *out_rest = cas->one;
        return PHY_OK;
    }
    const phy_ir_kind kind = phy_ir_kind_of(ir, term);
    if (kind == PHY_IR_POW) {
        int64_t exponent = 0;
        if (phy_ir_child(ir, term, 0u) == variable &&
            phy_ir_integer_value(ir, phy_ir_child(ir, term, 1u), &exponent) &&
            exponent > 0) {
            *out_exponent = exponent;
            *out_rest = cas->one;
        }
        return PHY_OK;
    }
    if (kind != PHY_IR_MUL) {
        return PHY_OK;
    }
    const size_t count = phy_ir_child_count(ir, term);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    phy_status status = phy_cas_scratch_alloc(cas, count, &offset);
    if (status != PHY_OK) {
        return status;
    }
    size_t kept = 0u;
    for (size_t i = 0u; i < count; ++i) {
        const phy_ir_ref factor = phy_ir_child(ir, term, i);
        if (factor == variable) {
            *out_exponent = 1;
            continue;
        }
        int64_t exponent = 0;
        if (phy_ir_kind_of(ir, factor) == PHY_IR_POW &&
            phy_ir_child(ir, factor, 0u) == variable &&
            phy_ir_integer_value(ir, phy_ir_child(ir, factor, 1u),
                                 &exponent) &&
            exponent > 0) {
            *out_exponent = exponent;
            continue;
        }
        phy_cas_scratch_at(cas, offset)[kept++] = factor;
    }
    status = phy_cas_mul_at(cas, offset, kept, out_rest);
    phy_cas_scratch_release(cas, mark);
    return status;
}

/*
 * An expanded polynomial as coefficients of powers of `variable`. Buckets are
 * accumulated with the simplifying adder, so each one stays collected.
 */
static phy_status collect_powers(phy_cas *cas, phy_ir_ref poly,
                                 phy_ir_ref variable,
                                 phy_ir_ref buckets[REDUCE_MAX_DEGREE + 1u],
                                 int64_t *out_degree, bool *out_fits)
{
    phy_ir_context *ir = cas->ir;
    for (size_t i = 0u; i <= REDUCE_MAX_DEGREE; ++i) {
        buckets[i] = cas->zero;
    }
    *out_degree = 0;
    *out_fits = true;

    const bool split = phy_ir_kind_of(ir, poly) == PHY_IR_ADD;
    const size_t count = split ? phy_ir_child_count(ir, poly) : 1u;
    for (size_t i = 0u; i < count; ++i) {
        const phy_ir_ref term = split ? phy_ir_child(ir, poly, i) : poly;
        int64_t exponent = 0;
        phy_ir_ref rest = PHY_IR_NULL;
        phy_status status =
            term_power_of(cas, term, variable, &exponent, &rest);
        if (status != PHY_OK) {
            return status;
        }
        if (exponent < 0 || exponent > (int64_t)REDUCE_MAX_DEGREE) {
            *out_fits = false;
            return PHY_OK;
        }
        const phy_ir_ref pair[2] = {buckets[exponent], rest};
        status = phy_cas_add_node(cas, pair, 2u, &buckets[exponent]);
        if (status != PHY_OK) {
            return status;
        }
        if (exponent > *out_degree) {
            *out_degree = exponent;
        }
    }
    return PHY_OK;
}

/*
 * Candidate main variables of a divisor: the distinct non-numeric bases of
 * its first few terms, in canonical order.
 */
static size_t divisor_candidates(phy_cas *cas, phy_ir_ref divisor,
                                 phy_ir_ref out[REDUCE_MAX_CANDIDATES])
{
    phy_ir_context *ir = cas->ir;
    size_t found = 0u;
    const bool split = phy_ir_kind_of(ir, divisor) == PHY_IR_ADD;
    const size_t terms = split ? phy_ir_child_count(ir, divisor) : 1u;
    for (size_t t = 0u; t < terms && found < REDUCE_MAX_CANDIDATES; ++t) {
        const phy_ir_ref term = split ? phy_ir_child(ir, divisor, t) : divisor;
        const phy_ir_kind kind = phy_ir_kind_of(ir, term);
        const size_t factors =
            kind == PHY_IR_MUL ? phy_ir_child_count(ir, term) : 1u;
        for (size_t f = 0u;
             f < factors && found < REDUCE_MAX_CANDIDATES; ++f) {
            phy_ir_ref base =
                kind == PHY_IR_MUL ? phy_ir_child(ir, term, f) : term;
            if (phy_ir_kind_of(ir, base) == PHY_IR_POW) {
                base = phy_ir_child(ir, base, 0u);
            }
            if (phy_cas_is_exact(cas, base)) {
                continue;
            }
            bool seen = false;
            for (size_t i = 0u; i < found; ++i) {
                if (out[i] == base) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                out[found++] = base;
            }
        }
    }
    return found;
}

/*
 * poly / divisor when the division is exact, attempted in one variable whose
 * leading divisor coefficient is an exact number. Inexact division is a
 * result (`*out_divides = false`), not an error.
 */
static phy_status divide_in_variable(phy_cas *cas, phy_ir_ref poly,
                                     phy_ir_ref divisor, phy_ir_ref variable,
                                     phy_ir_ref *out_quotient,
                                     bool *out_divides)
{
    *out_divides = false;
    phy_ir_ref divisor_c[REDUCE_MAX_DEGREE + 1u];
    phy_ir_ref remainder[REDUCE_MAX_DEGREE + 1u];
    phy_ir_ref quotient[REDUCE_MAX_DEGREE + 1u];
    int64_t divisor_degree = 0;
    int64_t degree = 0;
    bool fits = false;
    phy_status status = collect_powers(cas, divisor, variable, divisor_c,
                                       &divisor_degree, &fits);
    if (status != PHY_OK || !fits || divisor_degree == 0) {
        return status;
    }
    phy_cas_rat leading;
    if (!phy_cas_exact_value(cas, divisor_c[divisor_degree], &leading) ||
        leading.num == 0) {
        return PHY_OK;
    }
    status = collect_powers(cas, poly, variable, remainder, &degree, &fits);
    if (status != PHY_OK || !fits || degree < divisor_degree) {
        return status;
    }
    const phy_cas_rat inverse = {leading.den, leading.num};
    phy_ir_ref inverse_node = PHY_IR_NULL;
    status = phy_cas_number_node(cas, inverse, &inverse_node);
    if (status != PHY_OK) {
        return status;
    }

    for (int64_t i = 0; i <= degree; ++i) {
        quotient[i] = cas->zero;
    }
    for (int64_t i = degree; i >= divisor_degree; --i) {
        status = phy_cas_step(cas);
        if (status != PHY_OK) {
            return status;
        }
        const phy_ir_ref scaled[2] = {remainder[i], inverse_node};
        phy_ir_ref term = PHY_IR_NULL;
        status = phy_cas_mul_node(cas, scaled, 2u, &term);
        if (status != PHY_OK) {
            return status;
        }
        status = phy_cas_expand_node(cas, term, &term);
        if (status != PHY_OK) {
            return status;
        }
        quotient[i - divisor_degree] = term;
        if (phy_cas_is_integer(cas, term, 0)) {
            continue;
        }
        for (int64_t j = 0; j < divisor_degree; ++j) {
            const phy_ir_ref product[2] = {term, divisor_c[j]};
            phy_ir_ref subtracted = PHY_IR_NULL;
            phy_ir_ref negated = PHY_IR_NULL;
            if ((status = phy_cas_mul_node(cas, product, 2u, &subtracted)) !=
                    PHY_OK ||
                (status = phy_cas_neg_node(cas, subtracted, &negated)) !=
                    PHY_OK) {
                return status;
            }
            const phy_ir_ref sum[2] = {remainder[i - divisor_degree + j],
                                       negated};
            phy_ir_ref updated = PHY_IR_NULL;
            if ((status = phy_cas_add_node(cas, sum, 2u, &updated)) !=
                    PHY_OK ||
                (status = phy_cas_expand_node(
                     cas, updated,
                     &remainder[i - divisor_degree + j])) != PHY_OK) {
                return status;
            }
        }
    }
    for (int64_t j = 0; j < divisor_degree; ++j) {
        if (!phy_cas_is_integer(cas, remainder[j], 0)) {
            return PHY_OK;
        }
    }

    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    status = phy_cas_scratch_alloc(
        cas, (size_t)(degree - divisor_degree) + 1u, &offset);
    if (status != PHY_OK) {
        return status;
    }
    size_t used = 0u;
    for (int64_t k = 0; k <= degree - divisor_degree; ++k) {
        if (phy_cas_is_integer(cas, quotient[k], 0)) {
            continue;
        }
        phy_ir_ref exponent = PHY_IR_NULL;
        phy_ir_ref power = PHY_IR_NULL;
        phy_ir_ref scaled = PHY_IR_NULL;
        if ((status = phy_cas_number_node(cas, (phy_cas_rat){k, 1},
                                          &exponent)) != PHY_OK ||
            (status = phy_cas_pow_node(cas, variable, exponent, &power)) !=
                PHY_OK) {
            break;
        }
        const phy_ir_ref pair[2] = {quotient[k], power};
        status = phy_cas_mul_node(cas, pair, 2u, &scaled);
        if (status != PHY_OK) {
            break;
        }
        phy_cas_scratch_at(cas, offset)[used++] = scaled;
    }
    if (status == PHY_OK) {
        status = phy_cas_add_at(cas, offset, used, out_quotient);
    }
    if (status == PHY_OK) {
        status = phy_cas_expand_node(cas, *out_quotient, out_quotient);
    }
    phy_cas_scratch_release(cas, mark);
    if (status == PHY_OK) {
        *out_divides = true;
    }
    return status;
}

static phy_status divide_exact(phy_cas *cas, phy_ir_ref poly,
                               phy_ir_ref divisor, phy_ir_ref *out_quotient,
                               bool *out_divides)
{
    *out_divides = false;
    if (phy_cas_is_integer(cas, poly, 0)) {
        return PHY_OK;
    }
    if (phy_ir_kind_of(cas->ir, divisor) != PHY_IR_ADD) {
        /* An atomic base divides exactly when every term carries it. */
        return divide_in_variable(cas, poly, divisor, divisor, out_quotient,
                                  out_divides);
    }
    phy_ir_ref candidates[REDUCE_MAX_CANDIDATES];
    const size_t count = divisor_candidates(cas, divisor, candidates);
    for (size_t i = 0u; i < count; ++i) {
        const phy_status status = divide_in_variable(
            cas, poly, divisor, candidates[i], out_quotient, out_divides);
        if (status != PHY_OK || *out_divides) {
            return status;
        }
    }
    return PHY_OK;
}

/* ------------------------------------------ univariate polynomial GCD over Q */

typedef struct {
    int64_t degree;
    phy_cas_rat coefficients[REDUCE_MAX_DEGREE + 1u];
} rational_poly;

static bool rat_subtract(phy_cas_rat left, phy_cas_rat right,
                         phy_cas_rat *out)
{
    phy_cas_rat negated;
    return phy_cas_rat_mul(right, (phy_cas_rat){-1, 1}, &negated) &&
           phy_cas_rat_add(left, negated, out);
}

static void poly_zero(rational_poly *poly)
{
    poly->degree = 0;
    for (size_t index = 0u; index <= REDUCE_MAX_DEGREE; ++index) {
        poly->coefficients[index] = (phy_cas_rat){0, 1};
    }
}

static void poly_trim(rational_poly *poly)
{
    while (poly->degree > 0 &&
           poly->coefficients[poly->degree].num == 0) {
        poly->degree--;
    }
}

static bool poly_is_zero(const rational_poly *poly)
{
    return poly->degree == 0 && poly->coefficients[0].num == 0;
}

/*
 * Read a polynomial in one explicit symbol.  Coefficients must be exact
 * rationals: a second symbol means multivariate algebra, which this first GCD
 * milestone deliberately leaves untouched rather than treating as a number.
 */
static phy_status poly_from_ir(phy_cas *cas, phy_ir_ref expression,
                               phy_ir_ref variable, rational_poly *out_poly,
                               bool *out_fits)
{
    phy_ir_ref buckets[REDUCE_MAX_DEGREE + 1u];
    int64_t degree = 0;
    bool fits = false;
    phy_status status =
        collect_powers(cas, expression, variable, buckets, &degree, &fits);
    if (status != PHY_OK || !fits) {
        *out_fits = false;
        return status;
    }

    poly_zero(out_poly);
    out_poly->degree = degree;
    for (int64_t index = 0; index <= degree; ++index) {
        if (!phy_cas_exact_value(cas, buckets[index],
                                 &out_poly->coefficients[index])) {
            *out_fits = false;
            return PHY_OK;
        }
    }
    poly_trim(out_poly);
    *out_fits = true;
    return PHY_OK;
}

static phy_status poly_make_monic(rational_poly *poly)
{
    if (poly_is_zero(poly)) {
        return PHY_OK;
    }
    const phy_cas_rat leading = poly->coefficients[poly->degree];
    for (int64_t index = 0; index <= poly->degree; ++index) {
        phy_cas_rat divided;
        if (!rat_divide(poly->coefficients[index], leading, &divided)) {
            return PHY_ERR_OVERFLOW;
        }
        poly->coefficients[index] = divided;
    }
    return PHY_OK;
}

static phy_status poly_remainder(phy_cas *cas, const rational_poly *dividend,
                                 const rational_poly *divisor,
                                 rational_poly *out_remainder)
{
    if (poly_is_zero(divisor)) {
        return PHY_ERR_DOMAIN;
    }
    *out_remainder = *dividend;
    while (!poly_is_zero(out_remainder) &&
           out_remainder->degree >= divisor->degree) {
        phy_status status = phy_cas_step(cas);
        if (status != PHY_OK) {
            return status;
        }
        const int64_t shift =
            out_remainder->degree - divisor->degree;
        phy_cas_rat factor;
        if (!rat_divide(
                out_remainder->coefficients[out_remainder->degree],
                divisor->coefficients[divisor->degree], &factor)) {
            return PHY_ERR_OVERFLOW;
        }
        for (int64_t index = 0; index <= divisor->degree; ++index) {
            phy_cas_rat product;
            phy_cas_rat updated;
            if (!phy_cas_rat_mul(factor, divisor->coefficients[index],
                                 &product) ||
                !rat_subtract(
                    out_remainder->coefficients[index + shift], product,
                    &updated)) {
                return PHY_ERR_OVERFLOW;
            }
            out_remainder->coefficients[index + shift] = updated;
        }
        poly_trim(out_remainder);
    }
    return PHY_OK;
}

static phy_status poly_gcd(phy_cas *cas, const rational_poly *left,
                           const rational_poly *right,
                           rational_poly *out_gcd)
{
    rational_poly a = *left;
    rational_poly b = *right;
    phy_status status = poly_make_monic(&a);
    if (status == PHY_OK) {
        status = poly_make_monic(&b);
    }
    while (status == PHY_OK && !poly_is_zero(&b)) {
        rational_poly remainder;
        poly_zero(&remainder);
        status = poly_remainder(cas, &a, &b, &remainder);
        if (status == PHY_OK) {
            status = poly_make_monic(&remainder);
        }
        a = b;
        b = remainder;
    }
    if (status != PHY_OK) {
        return status;
    }
    *out_gcd = a;
    return PHY_OK;
}

static phy_status poly_divide_exact(const rational_poly *dividend,
                                    const rational_poly *divisor,
                                    rational_poly *out_quotient,
                                    bool *out_exact)
{
    *out_exact = false;
    if (poly_is_zero(divisor)) {
        return PHY_ERR_DOMAIN;
    }
    rational_poly remainder = *dividend;
    poly_zero(out_quotient);
    if (remainder.degree >= divisor->degree) {
        out_quotient->degree = remainder.degree - divisor->degree;
    }
    while (!poly_is_zero(&remainder) &&
           remainder.degree >= divisor->degree) {
        const int64_t shift = remainder.degree - divisor->degree;
        phy_cas_rat factor;
        if (!rat_divide(remainder.coefficients[remainder.degree],
                        divisor->coefficients[divisor->degree], &factor)) {
            return PHY_ERR_OVERFLOW;
        }
        out_quotient->coefficients[shift] = factor;
        for (int64_t index = 0; index <= divisor->degree; ++index) {
            phy_cas_rat product;
            phy_cas_rat updated;
            if (!phy_cas_rat_mul(factor, divisor->coefficients[index],
                                 &product) ||
                !rat_subtract(remainder.coefficients[index + shift], product,
                              &updated)) {
                return PHY_ERR_OVERFLOW;
            }
            remainder.coefficients[index + shift] = updated;
        }
        poly_trim(&remainder);
    }
    poly_trim(out_quotient);
    *out_exact = poly_is_zero(&remainder);
    return PHY_OK;
}

static phy_status poly_to_ir(phy_cas *cas, const rational_poly *poly,
                             phy_ir_ref variable, phy_ir_ref *out_ref)
{
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    phy_status status = phy_cas_scratch_alloc(
        cas, (size_t)poly->degree + 1u, &offset);
    if (status != PHY_OK) {
        return status;
    }
    size_t used = 0u;
    for (int64_t degree = 0; degree <= poly->degree && status == PHY_OK;
         ++degree) {
        const phy_cas_rat coefficient = poly->coefficients[degree];
        if (coefficient.num == 0) {
            continue;
        }
        phy_ir_ref coefficient_ref = PHY_IR_NULL;
        phy_ir_ref exponent = PHY_IR_NULL;
        phy_ir_ref power = PHY_IR_NULL;
        phy_ir_ref term = PHY_IR_NULL;
        status =
            phy_cas_number_node(cas, coefficient, &coefficient_ref);
        if (status == PHY_OK) {
            status = phy_cas_number_node(
                cas, (phy_cas_rat){degree, 1}, &exponent);
        }
        if (status == PHY_OK) {
            status = phy_cas_pow_node(cas, variable, exponent, &power);
        }
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {coefficient_ref, power};
            status = phy_cas_mul_node(cas, factors, 2u, &term);
        }
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, offset)[used++] = term;
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_add_at(cas, offset, used, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

/*
 * Cancel a hidden common factor when both sides are univariate polynomials
 * over Q.  Multivariate coefficients, non-symbol generators, and degree above
 * REDUCE_MAX_DEGREE leave the pair unchanged.
 */
static phy_status cancel_univariate_gcd(phy_cas *cas, phy_ir_ref numerator,
                                        phy_ir_ref denominator,
                                        phy_ir_ref *out_num,
                                        phy_ir_ref *out_den)
{
    *out_num = numerator;
    *out_den = denominator;

    phy_ir_ref denominator_coefficient = PHY_IR_NULL;
    phy_ir_ref denominator_polynomial = PHY_IR_NULL;
    phy_status status = phy_cas_split_coefficient(
        cas, denominator, &denominator_coefficient,
        &denominator_polynomial);
    if (status != PHY_OK) {
        return status;
    }
    if (phy_ir_kind_of(cas->ir, denominator_polynomial) != PHY_IR_ADD) {
        return PHY_OK;
    }

    phy_cas_rat denominator_scale;
    if (!phy_cas_exact_value(cas, denominator_coefficient,
                             &denominator_scale) ||
        denominator_scale.num == 0) {
        return PHY_OK;
    }
    phy_cas_rat inverse_scale;
    if (!rat_divide((phy_cas_rat){1, 1}, denominator_scale,
                    &inverse_scale)) {
        return PHY_ERR_OVERFLOW;
    }
    phy_ir_ref scale = PHY_IR_NULL;
    phy_ir_ref scaled_numerator = PHY_IR_NULL;
    status = phy_cas_number_node(cas, inverse_scale, &scale);
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {scale, numerator};
        status = phy_cas_mul_node(cas, factors, 2u, &scaled_numerator);
    }
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref expanded_numerator = PHY_IR_NULL;
    phy_ir_ref expanded_denominator = PHY_IR_NULL;
    status =
        phy_cas_expand_node(cas, scaled_numerator, &expanded_numerator);
    if (status == PHY_OK) {
        status =
            phy_cas_expand_node(cas, denominator_polynomial,
                                &expanded_denominator);
    }
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref candidates[REDUCE_MAX_CANDIDATES];
    const size_t count =
        divisor_candidates(cas, expanded_denominator, candidates);
    for (size_t index = 0u; index < count; ++index) {
        const phy_ir_ref variable = candidates[index];
        if (phy_ir_kind_of(cas->ir, variable) != PHY_IR_SYMBOL) {
            continue;
        }
        rational_poly num_poly;
        rational_poly den_poly;
        bool numerator_fits = false;
        bool denominator_fits = false;
        status = poly_from_ir(
            cas, expanded_numerator, variable, &num_poly, &numerator_fits);
        if (status != PHY_OK) {
            return status;
        }
        status = poly_from_ir(
            cas, expanded_denominator, variable, &den_poly,
            &denominator_fits);
        if (status != PHY_OK) {
            return status;
        }
        if (!numerator_fits || !denominator_fits ||
            poly_is_zero(&num_poly)) {
            continue;
        }

        rational_poly gcd;
        status = poly_gcd(cas, &num_poly, &den_poly, &gcd);
        if (status != PHY_OK) {
            return status;
        }
        if (gcd.degree == 0) {
            return PHY_OK;
        }

        rational_poly num_quotient;
        rational_poly den_quotient;
        poly_zero(&num_quotient);
        poly_zero(&den_quotient);
        bool num_exact = false;
        bool den_exact = false;
        status = poly_divide_exact(
            &num_poly, &gcd, &num_quotient, &num_exact);
        if (status == PHY_OK) {
            status = poly_divide_exact(
                &den_poly, &gcd, &den_quotient, &den_exact);
        }
        if (status != PHY_OK) {
            return status;
        }
        if (!num_exact || !den_exact) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }

        /* A unique reduced pair: make the denominator monic. */
        const phy_cas_rat denominator_lead =
            den_quotient.coefficients[den_quotient.degree];
        for (int64_t degree = 0; degree <= num_quotient.degree; ++degree) {
            phy_cas_rat normalized;
            if (!rat_divide(num_quotient.coefficients[degree],
                            denominator_lead, &normalized)) {
                return PHY_ERR_OVERFLOW;
            }
            num_quotient.coefficients[degree] = normalized;
        }
        for (int64_t degree = 0; degree <= den_quotient.degree; ++degree) {
            phy_cas_rat normalized;
            if (!rat_divide(den_quotient.coefficients[degree],
                            denominator_lead, &normalized)) {
                return PHY_ERR_OVERFLOW;
            }
            den_quotient.coefficients[degree] = normalized;
        }

        status = poly_to_ir(cas, &num_quotient, variable, out_num);
        return status == PHY_OK
                   ? poly_to_ir(cas, &den_quotient, variable, out_den)
                   : status;
    }
    return PHY_OK;
}

/* ---------------------------------------------------------- cancellation */

phy_status phy_cas_cancel_known_factors(phy_cas *cas, phy_ir_ref numerator,
                                        phy_ir_ref denominator,
                                        phy_ir_ref *out_num,
                                        phy_ir_ref *out_den)
{
    *out_num = numerator;
    *out_den = denominator;
    reduce_factors factors;
    if (!factors_of(cas, denominator, &factors)) {
        return PHY_OK; /* an unrecognized denominator shape stays as-is */
    }
    phy_ir_ref current = numerator;
    bool changed = false;
    for (size_t i = 0u; i < factors.count; ++i) {
        while (factors.exponents[i] > 0) {
            phy_ir_ref quotient = PHY_IR_NULL;
            bool divides = false;
            const phy_status status = divide_exact(
                cas, current, factors.bases[i], &quotient, &divides);
            if (status != PHY_OK) {
                /* A resource limit mid-trial leaves the pair unreduced. */
                return status;
            }
            if (!divides) {
                break;
            }
            current = quotient;
            factors.exponents[i]--;
            changed = true;
        }
    }
    phy_ir_ref remaining_denominator = denominator;
    if (changed) {
        const phy_status status =
            factors_build(cas, &factors, false, &remaining_denominator);
        if (status != PHY_OK) {
            return status;
        }
    }
    return cancel_univariate_gcd(cas, current, remaining_denominator,
                                 out_num, out_den);
}

/* --------------------------------------------------------- public surface */

phy_status phy_cas_reduce(phy_cas *cas, phy_ir_ref expr, phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_cas_begin(cas);

    phy_ir_ref numerator = PHY_IR_NULL;
    phy_ir_ref denominator = PHY_IR_NULL;
    phy_status status = phy_cas_rational_reduced_node(cas, expr, &numerator,
                                                      &denominator);
    if (status != PHY_OK) {
        return status;
    }
    if (phy_cas_is_integer(cas, denominator, 1)) {
        *out_ref = numerator;
        return PHY_OK;
    }
    reduce_factors factors;
    phy_ir_ref inverse = PHY_IR_NULL;
    if (!factors_of(cas, denominator, &factors)) {
        return PHY_ERR_TERM_LIMIT;
    }
    status = factors_build(cas, &factors, true, &inverse);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref pair[2] = {numerator, inverse};
    return phy_cas_mul_node(cas, pair, 2u, out_ref);
}
