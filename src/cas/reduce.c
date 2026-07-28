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
#define REDUCE_MAX_INTEGER_COEFFICIENT 1000000
#define REDUCE_MAX_DIVISORS 256u
#define REDUCE_MAX_ROOT_TRIALS 4096u
#define REDUCE_MAX_FACTOR_COEFFICIENTS (2u * REDUCE_MAX_DEGREE)

typedef struct {
    phy_ir_ref coefficient;
    size_t count;
    phy_ir_ref bases[REDUCE_MAX_FACTORS];
    int64_t exponents[REDUCE_MAX_FACTORS];
} reduce_factors;

static bool coefficient_is_zero(const phy_cas *cas, phy_ir_ref coefficient);
static bool coefficient_small_value(const phy_cas *cas,
                                    phy_ir_ref coefficient,
                                    int64_t *out_numerator,
                                    int64_t *out_denominator);
static phy_status coefficient_add(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out);
static phy_status coefficient_subtract(phy_cas *cas, phy_ir_ref left,
                                       phy_ir_ref right, phy_ir_ref *out);
static phy_status coefficient_multiply(phy_cas *cas, phy_ir_ref left,
                                       phy_ir_ref right, phy_ir_ref *out);
static phy_status coefficient_divide(phy_cas *cas, phy_ir_ref dividend,
                                     phy_ir_ref divisor, phy_ir_ref *out);

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

static phy_status factors_push_node(phy_cas *cas, reduce_factors *factors,
                                    phy_ir_ref node)
{
    if (phy_cas_is_exact(cas, node)) {
        return coefficient_multiply(
            cas, factors->coefficient, node, &factors->coefficient);
    }
    if (phy_ir_kind_of(cas->ir, node) == PHY_IR_POW) {
        int64_t exponent = 0;
        if (phy_ir_integer_value(cas->ir,
                                 phy_ir_child(cas->ir, node, 1u),
                                 &exponent) &&
            exponent > 0) {
            return factors_push(
                       factors, phy_ir_child(cas->ir, node, 0u),
                       exponent)
                       ? PHY_OK
                       : PHY_ERR_TERM_LIMIT;
        }
    }
    return factors_push(factors, node, 1) ? PHY_OK : PHY_ERR_TERM_LIMIT;
}

/*
 * A denominator produced by the rational walk: an exact coefficient times a
 * product of positive integer powers of expanded polynomial bases.
 */
static phy_status factors_of(phy_cas *cas, phy_ir_ref denominator,
                             reduce_factors *out_factors)
{
    out_factors->coefficient = cas->one;
    out_factors->count = 0u;
    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, denominator);
    if (kind != PHY_IR_MUL) {
        return factors_push_node(cas, out_factors, denominator);
    }
    const size_t count = phy_ir_child_count(cas->ir, denominator);
    for (size_t i = 0u; i < count; ++i) {
        const phy_status status =
            factors_push_node(cas, out_factors,
                              phy_ir_child(cas->ir, denominator, i));
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
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
    phy_ir_ref coefficient = factors->coefficient;
    if (invert) {
        status =
            coefficient_divide(cas, cas->one, coefficient, &coefficient);
    }
    if (status == PHY_OK && !phy_cas_is_integer(cas, coefficient, 1)) {
        phy_cas_scratch_at(cas, offset)[used++] = coefficient;
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
    reduce_factors left = {0};
    reduce_factors right = {0};
    phy_status status = factors_of(cas, d1, &left);
    if (status == PHY_OK) {
        status = factors_of(cas, d2, &right);
    }
    if (status != PHY_OK) {
        return status;
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
    int64_t left_num = 0;
    int64_t left_den = 0;
    int64_t right_num = 0;
    int64_t right_den = 0;
    int64_t lcm = 0;
    if (coefficient_small_value(
            cas, left.coefficient, &left_num, &left_den) &&
        coefficient_small_value(
            cas, right.coefficient, &right_num, &right_den) &&
        left_num > 0 && right_num > 0 &&
        checked_gcd_lcm(left_num, right_num, &lcm)) {
        status = phy_cas_number_node(
            cas,
            (phy_cas_rat){
                lcm, positive_gcd(left_den, right_den)},
            &common.coefficient);
    } else {
        /*
         * Any nonzero product is a valid common rational multiple. Keep the
         * historical least common coefficient on the inline fast path; when
         * either coefficient is promoted, use the exact product and normalize
         * its sign. This removes the int64 ceiling without pretending that a
         * bigint rational LCM has already been exposed as a public primitive.
         */
        status = coefficient_multiply(
            cas, left.coefficient, right.coefficient,
            &common.coefficient);
        if (status == PHY_OK &&
            phy_cas_exact_sign_ref(cas, common.coefficient) < 0) {
            status = coefficient_subtract(
                cas, cas->zero, common.coefficient,
                &common.coefficient);
        }
    }
    if (status != PHY_OK) {
        return status;
    }

    reduce_factors fill = common;
    phy_ir_ref multiplier_left = PHY_IR_NULL;
    phy_ir_ref multiplier_right = PHY_IR_NULL;
    for (size_t i = 0u; i < fill.count; ++i) {
        fill.exponents[i] =
            common.exponents[i] - factors_exponent_of(&left, fill.bases[i]);
    }
    status = coefficient_divide(
        cas, common.coefficient, left.coefficient, &fill.coefficient);
    if (status != PHY_OK) {
        return status;
    }
    status = factors_build(cas, &fill, false, &multiplier_left);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 0u; i < fill.count; ++i) {
        fill.exponents[i] =
            common.exponents[i] - factors_exponent_of(&right, fill.bases[i]);
    }
    status = coefficient_divide(
        cas, common.coefficient, right.coefficient, &fill.coefficient);
    if (status != PHY_OK) {
        return status;
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
    phy_ir_ref coefficients[REDUCE_MAX_DEGREE + 1u];
} rational_poly;

static bool coefficient_is_zero(const phy_cas *cas, phy_ir_ref coefficient)
{
    return phy_cas_is_integer(cas, coefficient, 0);
}

static bool coefficient_small_value(const phy_cas *cas,
                                    phy_ir_ref coefficient,
                                    int64_t *out_numerator,
                                    int64_t *out_denominator)
{
    if (phy_ir_integer_value(
            cas->ir, coefficient, out_numerator)) {
        *out_denominator = 1;
        return true;
    }
    return phy_ir_rational_value(
        cas->ir, coefficient, out_numerator, out_denominator);
}

static phy_status coefficient_add(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out)
{
    phy_cas_rat a;
    phy_cas_rat b;
    phy_cas_rat result;
    if (phy_cas_exact_value(cas, left, &a) &&
        phy_cas_exact_value(cas, right, &b) &&
        phy_cas_rat_add(a, b, &result)) {
        return phy_cas_number_node(cas, result, out);
    }
    return phy_cas_exact_add_refs(cas, left, right, out);
}

static phy_status coefficient_subtract(phy_cas *cas, phy_ir_ref left,
                                       phy_ir_ref right, phy_ir_ref *out)
{
    phy_cas_rat a;
    phy_cas_rat b;
    phy_cas_rat negated;
    phy_cas_rat result;
    if (phy_cas_exact_value(cas, left, &a) &&
        phy_cas_exact_value(cas, right, &b) &&
        phy_cas_rat_mul(b, (phy_cas_rat){-1, 1}, &negated) &&
        phy_cas_rat_add(a, negated, &result)) {
        return phy_cas_number_node(cas, result, out);
    }
    return phy_cas_exact_sub_refs(cas, left, right, out);
}

static phy_status coefficient_multiply(phy_cas *cas, phy_ir_ref left,
                                       phy_ir_ref right, phy_ir_ref *out)
{
    phy_cas_rat a;
    phy_cas_rat b;
    phy_cas_rat result;
    if (phy_cas_exact_value(cas, left, &a) &&
        phy_cas_exact_value(cas, right, &b) &&
        phy_cas_rat_mul(a, b, &result)) {
        return phy_cas_number_node(cas, result, out);
    }
    return phy_cas_exact_mul_refs(cas, left, right, out);
}

static phy_status coefficient_divide(phy_cas *cas, phy_ir_ref dividend,
                                     phy_ir_ref divisor, phy_ir_ref *out)
{
    phy_cas_rat a;
    phy_cas_rat b;
    phy_cas_rat result;
    if (phy_cas_exact_value(cas, dividend, &a) &&
        phy_cas_exact_value(cas, divisor, &b)) {
        if (b.num == 0) {
            return PHY_ERR_DOMAIN;
        }
        if (rat_divide(a, b, &result)) {
            return phy_cas_number_node(cas, result, out);
        }
    }
    return phy_cas_exact_div_refs(cas, dividend, divisor, out);
}

static void poly_zero(phy_cas *cas, rational_poly *poly)
{
    poly->degree = 0;
    for (size_t index = 0u; index <= REDUCE_MAX_DEGREE; ++index) {
        poly->coefficients[index] = cas->zero;
    }
}

static void poly_trim(const phy_cas *cas, rational_poly *poly)
{
    while (poly->degree > 0 &&
           coefficient_is_zero(cas, poly->coefficients[poly->degree])) {
        poly->degree--;
    }
}

static bool poly_is_zero(const phy_cas *cas, const rational_poly *poly)
{
    return poly->degree == 0 &&
           coefficient_is_zero(cas, poly->coefficients[0]);
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

    poly_zero(cas, out_poly);
    out_poly->degree = degree;
    for (int64_t index = 0; index <= degree; ++index) {
        if (!phy_cas_is_exact(cas, buckets[index])) {
            *out_fits = false;
            return PHY_OK;
        }
        out_poly->coefficients[index] = buckets[index];
    }
    poly_trim(cas, out_poly);
    *out_fits = true;
    return PHY_OK;
}

static phy_status poly_make_monic(phy_cas *cas, rational_poly *poly)
{
    if (poly_is_zero(cas, poly)) {
        return PHY_OK;
    }
    const phy_ir_ref leading = poly->coefficients[poly->degree];
    for (int64_t index = 0; index <= poly->degree; ++index) {
        phy_ir_ref divided = PHY_IR_NULL;
        const phy_status status = coefficient_divide(
            cas, poly->coefficients[index], leading, &divided);
        if (status != PHY_OK) {
            return status;
        }
        poly->coefficients[index] = divided;
    }
    return PHY_OK;
}

static phy_status poly_remainder(phy_cas *cas, const rational_poly *dividend,
                                 const rational_poly *divisor,
                                 rational_poly *out_remainder)
{
    if (poly_is_zero(cas, divisor)) {
        return PHY_ERR_DOMAIN;
    }
    *out_remainder = *dividend;
    while (!poly_is_zero(cas, out_remainder) &&
           out_remainder->degree >= divisor->degree) {
        phy_status status = phy_cas_step(cas);
        if (status != PHY_OK) {
            return status;
        }
        const int64_t shift =
            out_remainder->degree - divisor->degree;
        phy_ir_ref factor = PHY_IR_NULL;
        status = coefficient_divide(
            cas,
            out_remainder->coefficients[out_remainder->degree],
            divisor->coefficients[divisor->degree], &factor);
        if (status != PHY_OK) {
            return status;
        }
        for (int64_t index = 0; index <= divisor->degree; ++index) {
            phy_ir_ref product = PHY_IR_NULL;
            phy_ir_ref updated = PHY_IR_NULL;
            status = coefficient_multiply(
                cas, factor, divisor->coefficients[index], &product);
            if (status == PHY_OK) {
                status = coefficient_subtract(
                    cas,
                    out_remainder->coefficients[index + shift],
                    product, &updated);
            }
            if (status != PHY_OK) {
                return status;
            }
            out_remainder->coefficients[index + shift] = updated;
        }
        poly_trim(cas, out_remainder);
    }
    return PHY_OK;
}

static phy_status poly_gcd(phy_cas *cas, const rational_poly *left,
                           const rational_poly *right,
                           rational_poly *out_gcd)
{
    rational_poly a = *left;
    rational_poly b = *right;
    phy_status status = poly_make_monic(cas, &a);
    if (status == PHY_OK) {
        status = poly_make_monic(cas, &b);
    }
    while (status == PHY_OK && !poly_is_zero(cas, &b)) {
        rational_poly remainder;
        poly_zero(cas, &remainder);
        status = poly_remainder(cas, &a, &b, &remainder);
        if (status == PHY_OK) {
            status = poly_make_monic(cas, &remainder);
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

static phy_status poly_divide_exact(phy_cas *cas,
                                    const rational_poly *dividend,
                                    const rational_poly *divisor,
                                    rational_poly *out_quotient,
                                    bool *out_exact)
{
    *out_exact = false;
    if (poly_is_zero(cas, divisor)) {
        return PHY_ERR_DOMAIN;
    }
    rational_poly remainder = *dividend;
    poly_zero(cas, out_quotient);
    if (remainder.degree >= divisor->degree) {
        out_quotient->degree = remainder.degree - divisor->degree;
    }
    while (!poly_is_zero(cas, &remainder) &&
           remainder.degree >= divisor->degree) {
        const int64_t shift = remainder.degree - divisor->degree;
        phy_ir_ref factor = PHY_IR_NULL;
        phy_status status = coefficient_divide(
            cas, remainder.coefficients[remainder.degree],
            divisor->coefficients[divisor->degree], &factor);
        if (status != PHY_OK) {
            return status;
        }
        out_quotient->coefficients[shift] = factor;
        for (int64_t index = 0; index <= divisor->degree; ++index) {
            phy_ir_ref product = PHY_IR_NULL;
            phy_ir_ref updated = PHY_IR_NULL;
            status = coefficient_multiply(
                cas, factor, divisor->coefficients[index], &product);
            if (status == PHY_OK) {
                status = coefficient_subtract(
                    cas, remainder.coefficients[index + shift],
                    product, &updated);
            }
            if (status != PHY_OK) {
                return status;
            }
            remainder.coefficients[index + shift] = updated;
        }
        poly_trim(cas, &remainder);
    }
    poly_trim(cas, out_quotient);
    *out_exact = poly_is_zero(cas, &remainder);
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
        const phy_ir_ref coefficient_ref = poly->coefficients[degree];
        if (coefficient_is_zero(cas, coefficient_ref)) {
            continue;
        }
        phy_ir_ref exponent = PHY_IR_NULL;
        phy_ir_ref power = PHY_IR_NULL;
        phy_ir_ref term = PHY_IR_NULL;
        status = phy_cas_number_node(
            cas, (phy_cas_rat){degree, 1}, &exponent);
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

/* ------------------------------------------------ exact Q[x] factorization */

typedef struct {
    int64_t degree;
    unsigned multiplicity;
    size_t coefficient_offset;
} factor_record;

typedef struct {
    size_t count;
    size_t coefficient_count;
    factor_record records[REDUCE_MAX_DEGREE];
    phy_ir_ref coefficients[REDUCE_MAX_FACTOR_COEFFICIENTS];
} factor_workspace;

static bool poly_is_one(const phy_cas *cas, const rational_poly *poly)
{
    return poly->degree == 0 &&
           phy_cas_is_integer(cas, poly->coefficients[0], 1);
}

static phy_status poly_derivative(phy_cas *cas,
                                  const rational_poly *poly,
                                  rational_poly *out_derivative)
{
    poly_zero(cas, out_derivative);
    if (poly->degree == 0) {
        return PHY_OK;
    }
    out_derivative->degree = poly->degree - 1;
    for (int64_t degree = 1; degree <= poly->degree; ++degree) {
        phy_ir_ref degree_ref = PHY_IR_NULL;
        phy_status status = phy_cas_number_node(
            cas, (phy_cas_rat){degree, 1}, &degree_ref);
        if (status == PHY_OK) {
            status = coefficient_multiply(
                cas, poly->coefficients[degree], degree_ref,
                &out_derivative->coefficients[degree - 1]);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    poly_trim(cas, out_derivative);
    return PHY_OK;
}

/*
 * Clear rational denominators and remove the integer content. The resulting
 * primitive integer polynomial has exactly the same rational roots. Keeping
 * this conversion bounded prevents the rational-root theorem from turning one
 * calculator cell into billions of divisor trials.
 */
static phy_status poly_primitive_integers(
    phy_cas *cas,
    const rational_poly *poly,
    int64_t out_coefficients[REDUCE_MAX_DEGREE + 1u])
{
    int64_t denominator_lcm = 1;
    for (int64_t degree = 0; degree <= poly->degree; ++degree) {
        int64_t numerator = 0;
        int64_t denominator = 0;
        if (!coefficient_small_value(
                cas, poly->coefficients[degree],
                &numerator, &denominator)) {
            return PHY_ERR_UNSUPPORTED;
        }
        if (!checked_gcd_lcm(denominator_lcm,
                             denominator,
                             &denominator_lcm)) {
            return PHY_ERR_OVERFLOW;
        }
    }

    int64_t content = 0;
    for (int64_t degree = 0; degree <= poly->degree; ++degree) {
        int64_t numerator = 0;
        int64_t denominator = 0;
        if (!coefficient_small_value(
                cas, poly->coefficients[degree],
                &numerator, &denominator)) {
            return PHY_ERR_UNSUPPORTED;
        }
        const int64_t scale = denominator_lcm / denominator;
        if (numerator > REDUCE_MAX_INTEGER_COEFFICIENT / scale ||
            numerator < -REDUCE_MAX_INTEGER_COEFFICIENT / scale) {
            return PHY_ERR_TERM_LIMIT;
        }
        const int64_t integer = numerator * scale;
        out_coefficients[degree] = integer;
        const int64_t magnitude = integer < 0 ? -integer : integer;
        if (magnitude != 0) {
            content =
                content == 0 ? magnitude : positive_gcd(content, magnitude);
        }
    }
    if (content == 0) {
        return PHY_OK;
    }
    for (int64_t degree = 0; degree <= poly->degree; ++degree) {
        out_coefficients[degree] /= content;
    }
    return PHY_OK;
}

static phy_status integer_divisors(phy_cas *cas, int64_t value,
                                   int64_t out[REDUCE_MAX_DIVISORS],
                                   size_t *out_count)
{
    *out_count = 0u;
    if (value <= 0 || value > REDUCE_MAX_INTEGER_COEFFICIENT) {
        return PHY_ERR_TERM_LIMIT;
    }
    for (int64_t divisor = 1; divisor <= value / divisor; ++divisor) {
        phy_status status = phy_cas_step(cas);
        if (status != PHY_OK) {
            return status;
        }
        if (value % divisor != 0) {
            continue;
        }
        if (*out_count >= REDUCE_MAX_DIVISORS) {
            return PHY_ERR_TERM_LIMIT;
        }
        out[(*out_count)++] = divisor;
        const int64_t paired = value / divisor;
        if (paired != divisor) {
            if (*out_count >= REDUCE_MAX_DIVISORS) {
                return PHY_ERR_TERM_LIMIT;
            }
            out[(*out_count)++] = paired;
        }
    }
    return PHY_OK;
}

static phy_status poly_has_value(phy_cas *cas, const rational_poly *poly,
                                 phy_cas_rat value, bool *out_zero)
{
    phy_ir_ref value_ref = PHY_IR_NULL;
    phy_status status = phy_cas_number_node(cas, value, &value_ref);
    phy_ir_ref accumulated = poly->coefficients[poly->degree];
    for (int64_t degree = poly->degree; degree > 0; --degree) {
        if (status == PHY_OK) {
            status = phy_cas_step(cas);
        }
        if (status != PHY_OK) {
            return status;
        }
        phy_ir_ref product = PHY_IR_NULL;
        status = coefficient_multiply(
            cas, accumulated, value_ref, &product);
        if (status == PHY_OK) {
            status = coefficient_add(
                cas, product, poly->coefficients[degree - 1],
                &accumulated);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    *out_zero = coefficient_is_zero(cas, accumulated);
    return PHY_OK;
}

/*
 * Find one rational root, or prove that no rational root exists after
 * exhausting the primitive integer polynomial's numerator/denominator
 * divisors. If the bounded search cannot be exhausted, it returns a resource
 * error rather than falsely declaring the polynomial root-free.
 */
static phy_status poly_rational_root(phy_cas *cas,
                                     const rational_poly *poly,
                                     phy_ir_ref *out_root,
                                     bool *out_found)
{
    *out_found = false;
    if (poly->degree == 0) {
        return PHY_OK;
    }
    if (poly->degree == 1) {
        phy_ir_ref negated = PHY_IR_NULL;
        phy_status status = coefficient_subtract(
            cas, cas->zero, poly->coefficients[0], &negated);
        if (status == PHY_OK) {
            status = coefficient_divide(
                cas, negated, poly->coefficients[1], out_root);
        }
        *out_found = status == PHY_OK;
        return status;
    }

    /*
     * Test the universal small roots before requesting an int64 primitive
     * image.  These tests remain exact for arbitrary-precision coefficients
     * and keep common factors such as (B*x + y), B > INT64_MAX, available to
     * the verified Kronecker decoder.
     */
    if (coefficient_is_zero(cas, poly->coefficients[0])) {
        *out_root = cas->zero;
        *out_found = true;
        return PHY_OK;
    }
    static const phy_cas_rat universal_candidates[] = {
        {-1, 1},
        {1, 1},
    };
    for (size_t index = 0u;
         index < sizeof(universal_candidates) /
                     sizeof(universal_candidates[0]);
         ++index) {
        bool zero = false;
        phy_status status =
            poly_has_value(cas, poly, universal_candidates[index], &zero);
        if (status != PHY_OK) {
            return status;
        }
        if (zero) {
            status = phy_cas_number_node(
                cas, universal_candidates[index], out_root);
            if (status == PHY_OK) {
                *out_found = true;
            }
            return status;
        }
    }

    int64_t integers[REDUCE_MAX_DEGREE + 1u];
    phy_status status =
        poly_primitive_integers(cas, poly, integers);
    if (status != PHY_OK) {
        return status;
    }

    const int64_t constant =
        integers[0] < 0 ? -integers[0] : integers[0];
    const int64_t leading =
        integers[poly->degree] < 0 ? -integers[poly->degree]
                                  : integers[poly->degree];
    int64_t numerators[REDUCE_MAX_DIVISORS];
    int64_t denominators[REDUCE_MAX_DIVISORS];
    size_t numerator_count = 0u;
    size_t denominator_count = 0u;
    status =
        integer_divisors(cas, constant, numerators, &numerator_count);
    if (status == PHY_OK) {
        status = integer_divisors(
            cas, leading, denominators, &denominator_count);
    }
    if (status != PHY_OK) {
        return status;
    }

    size_t trials = 0u;
    for (size_t p = 0u; p < numerator_count; ++p) {
        for (size_t q = 0u; q < denominator_count; ++q) {
            phy_cas_rat positive;
            if (!rat_divide((phy_cas_rat){numerators[p], 1},
                            (phy_cas_rat){denominators[q], 1},
                            &positive)) {
                return PHY_ERR_OVERFLOW;
            }
            for (unsigned sign = 0u; sign < 2u; ++sign) {
                if (++trials > REDUCE_MAX_ROOT_TRIALS) {
                    return PHY_ERR_TERM_LIMIT;
                }
                phy_cas_rat candidate = positive;
                if (sign != 0u) {
                    candidate.num = -candidate.num;
                }
                bool zero = false;
                status = poly_has_value(cas, poly, candidate, &zero);
                if (status != PHY_OK) {
                    return status;
                }
                if (zero) {
                    status =
                        phy_cas_number_node(cas, candidate, out_root);
                    if (status != PHY_OK) {
                        return status;
                    }
                    *out_found = true;
                    return PHY_OK;
                }
            }
        }
    }
    return PHY_OK;
}

static phy_status poly_divide_linear(phy_cas *cas,
                                     const rational_poly *poly,
                                     phy_ir_ref root,
                                     rational_poly *out_quotient)
{
    rational_poly divisor;
    poly_zero(cas, &divisor);
    divisor.degree = 1;
    phy_status status = coefficient_subtract(
        cas, cas->zero, root, &divisor.coefficients[0]);
    if (status != PHY_OK) {
        return status;
    }
    divisor.coefficients[1] = cas->one;
    bool exact = false;
    status =
        poly_divide_exact(cas, poly, &divisor, out_quotient, &exact);
    if (status != PHY_OK) {
        return status;
    }
    return exact ? PHY_OK : PHY_ERR_CORRUPT_DOCUMENT;
}

static phy_status factor_record_push(phy_cas *cas,
                                     factor_workspace *workspace,
                                     const rational_poly *poly,
                                     unsigned multiplicity)
{
    if (poly_is_one(cas, poly)) {
        return PHY_OK;
    }
    const size_t coefficient_count = (size_t)poly->degree + 1u;
    if (workspace->count >= REDUCE_MAX_DEGREE || multiplicity == 0u ||
        workspace->coefficient_count >
            REDUCE_MAX_FACTOR_COEFFICIENTS - coefficient_count) {
        return PHY_ERR_TERM_LIMIT;
    }
    factor_record *record = &workspace->records[workspace->count++];
    record->degree = poly->degree;
    record->multiplicity = multiplicity;
    record->coefficient_offset = workspace->coefficient_count;
    for (size_t index = 0u; index < coefficient_count; ++index) {
        workspace->coefficients[workspace->coefficient_count++] =
            poly->coefficients[index];
    }
    return PHY_OK;
}

/*
 * A square-free polynomial over Q is completely factorable here when repeated
 * rational-root extraction leaves degree at most three. A quadratic or cubic
 * over Q with no rational root is irreducible; the same statement is false in
 * degree four, which is why that boundary returns UNSUPPORTED.
 */
static phy_status factor_square_free(
    phy_cas *cas, const rational_poly *poly, unsigned multiplicity,
    factor_workspace *workspace)
{
    rational_poly remaining = *poly;
    while (remaining.degree > 0) {
        phy_ir_ref root = PHY_IR_NULL;
        bool found = false;
        phy_status status =
            poly_rational_root(cas, &remaining, &root, &found);
        if (status != PHY_OK) {
            return status;
        }
        if (!found) {
            return remaining.degree <= 3
                       ? factor_record_push(
                             cas, workspace, &remaining, multiplicity)
                       : PHY_ERR_UNSUPPORTED;
        }

        rational_poly linear;
        poly_zero(cas, &linear);
        linear.degree = 1;
        status = coefficient_subtract(
            cas, cas->zero, root, &linear.coefficients[0]);
        if (status != PHY_OK) {
            return status;
        }
        linear.coefficients[1] = cas->one;
        status =
            factor_record_push(cas, workspace, &linear, multiplicity);
        if (status != PHY_OK) {
            return status;
        }
        rational_poly quotient;
        status =
            poly_divide_linear(cas, &remaining, root, &quotient);
        if (status != PHY_OK) {
            return status;
        }
        remaining = quotient;
    }
    return PHY_OK;
}

/*
 * Yun's characteristic-zero square-free decomposition. It separates repeated
 * irreducible factors before the rational-root pass, so (x^2+1)^2 is proved
 * and displayed as such even though x^2+1 has no rational root.
 */
static phy_status factor_complete(
    phy_cas *cas, const rational_poly *monic,
    factor_workspace *workspace)
{
    workspace->count = 0u;
    workspace->coefficient_count = 0u;
    rational_poly derivative;
    rational_poly repeated;
    rational_poly square_free_product;
    poly_zero(cas, &derivative);
    poly_zero(cas, &repeated);
    poly_zero(cas, &square_free_product);
    phy_status status = poly_derivative(cas, monic, &derivative);
    if (status == PHY_OK) {
        status = poly_gcd(cas, monic, &derivative, &repeated);
    }
    bool exact = false;
    if (status == PHY_OK) {
        status = poly_divide_exact(
            cas, monic, &repeated, &square_free_product, &exact);
    }
    if (status != PHY_OK) {
        return status;
    }
    if (!exact) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    rational_poly remaining_repeated = repeated;
    rational_poly layer = square_free_product;
    unsigned multiplicity = 1u;
    while (!poly_is_one(cas, &layer)) {
        if (multiplicity > REDUCE_MAX_DEGREE) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        rational_poly shared;
        rational_poly component;
        rational_poly next_repeated;
        poly_zero(cas, &shared);
        poly_zero(cas, &component);
        poly_zero(cas, &next_repeated);
        status = poly_gcd(cas, &layer, &remaining_repeated, &shared);
        if (status == PHY_OK) {
            status =
                poly_divide_exact(
                    cas, &layer, &shared, &component, &exact);
        }
        if (status == PHY_OK && exact) {
            status = poly_divide_exact(
                cas, &remaining_repeated, &shared, &next_repeated,
                &exact);
        }
        if (status != PHY_OK) {
            return status;
        }
        if (!exact) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        if (!poly_is_one(cas, &component)) {
            status = factor_square_free(
                cas, &component, multiplicity, workspace);
            if (status != PHY_OK) {
                return status;
            }
        }
        layer = shared;
        remaining_repeated = next_repeated;
        multiplicity++;
    }
    return poly_is_one(cas, &remaining_repeated) ? PHY_OK
                                            : PHY_ERR_CORRUPT_DOCUMENT;
}

static phy_status collect_factor_variables(
    phy_cas *cas, phy_ir_ref expression,
    phy_ir_ref out_variables[REDUCE_MAX_CANDIDATES], size_t *in_out_count)
{
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }
    if (phy_ir_kind_of(cas->ir, expression) == PHY_IR_SYMBOL) {
        const phy_ir_symbol symbol = phy_ir_head(cas->ir, expression);
        if ((phy_ir_assumptions(cas->ir, symbol) &
             (uint32_t)PHY_IR_ASSUME_CONSTANT) != 0u) {
            return PHY_OK;
        }
        for (size_t index = 0u; index < *in_out_count; ++index) {
            if (out_variables[index] == expression) {
                return PHY_OK;
            }
        }
        if (*in_out_count >= REDUCE_MAX_CANDIDATES) {
            return PHY_ERR_UNSUPPORTED;
        }
        out_variables[(*in_out_count)++] = expression;
        return PHY_OK;
    }
    const size_t children = phy_ir_child_count(cas->ir, expression);
    for (size_t index = 0u; index < children; ++index) {
        status = collect_factor_variables(
            cas, phy_ir_child(cas->ir, expression, index), out_variables,
            in_out_count);
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static phy_status build_factorization(
    phy_cas *cas, phy_ir_ref variable, phy_ir_ref leading,
    const factor_workspace *workspace,
    phy_ir_ref *out_ref)
{
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    phy_status status =
        phy_cas_scratch_alloc(cas, workspace->count + 1u, &offset);
    if (status != PHY_OK) {
        return status;
    }
    size_t used = 0u;
    if (!phy_cas_is_integer(cas, leading, 1)) {
        phy_cas_scratch_at(cas, offset)[used++] = leading;
    }
    for (size_t index = 0u;
         index < workspace->count && status == PHY_OK; ++index) {
        const factor_record *record = &workspace->records[index];
        rational_poly polynomial;
        poly_zero(cas, &polynomial);
        polynomial.degree = record->degree;
        for (int64_t degree = 0; degree <= record->degree; ++degree) {
            polynomial.coefficients[degree] =
                workspace->coefficients[
                    record->coefficient_offset + (size_t)degree];
        }
        phy_ir_ref factor = PHY_IR_NULL;
        status = poly_to_ir(cas, &polynomial, variable, &factor);
        if (status == PHY_OK && record->multiplicity > 1u) {
            phy_ir_ref exponent = PHY_IR_NULL;
            status = phy_cas_number_node(
                cas,
                (phy_cas_rat){(int64_t)record->multiplicity, 1},
                &exponent);
            if (status == PHY_OK) {
                status = phy_cas_pow_node(cas, factor, exponent, &factor);
            }
        }
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, offset)[used++] = factor;
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_mul_at(cas, offset, used, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

static void sort_variables(phy_cas *cas, phy_ir_ref *variables, size_t count)
{
    for (size_t index = 1u; index < count; ++index) {
        const phy_ir_ref key = variables[index];
        size_t position = index;
        while (position > 0u &&
               phy_ir_compare(
                   cas->ir, key, variables[position - 1u]) < 0) {
            variables[position] = variables[position - 1u];
            position--;
        }
        variables[position] = key;
    }
}

static phy_status kronecker_term(
    phy_cas *cas, phy_ir_ref term, const phy_ir_ref *variables,
    size_t variable_count, uint32_t *out_exponents,
    phy_ir_ref *out_coefficient, bool *out_fits)
{
    phy_ir_ref rest = term;
    *out_fits = true;
    for (size_t variable = 0u; variable < variable_count; ++variable) {
        int64_t exponent = 0;
        phy_ir_ref next = PHY_IR_NULL;
        const phy_status status = term_power_of(
            cas, rest, variables[variable], &exponent, &next);
        if (status != PHY_OK) {
            return status;
        }
        if (exponent < 0 ||
            exponent > (int64_t)REDUCE_MAX_DEGREE) {
            *out_fits = false;
            return PHY_OK;
        }
        out_exponents[variable] = (uint32_t)exponent;
        rest = next;
    }
    if (!phy_cas_is_exact(cas, rest)) {
        *out_fits = false;
        return PHY_OK;
    }
    *out_coefficient = rest;
    return PHY_OK;
}

static phy_status kronecker_degrees(
    phy_cas *cas, phy_ir_ref expression, const phy_ir_ref *variables,
    size_t variable_count, uint32_t *in_out_degrees, bool *out_fits)
{
    const bool split =
        phy_ir_kind_of(cas->ir, expression) == PHY_IR_ADD;
    const size_t count =
        split ? phy_ir_child_count(cas->ir, expression) : 1u;
    *out_fits = true;
    for (size_t term_index = 0u; term_index < count; ++term_index) {
        const phy_ir_ref term =
            split ? phy_ir_child(cas->ir, expression, term_index)
                  : expression;
        uint32_t exponents[REDUCE_MAX_CANDIDATES] = {0u};
        phy_ir_ref coefficient = PHY_IR_NULL;
        bool fits = false;
        const phy_status status = kronecker_term(
            cas, term, variables, variable_count, exponents,
            &coefficient, &fits);
        (void)coefficient;
        if (status != PHY_OK || !fits) {
            *out_fits = fits;
            return status;
        }
        for (size_t variable = 0u; variable < variable_count; ++variable) {
            if (exponents[variable] > in_out_degrees[variable]) {
                in_out_degrees[variable] = exponents[variable];
            }
        }
    }
    return PHY_OK;
}

static bool kronecker_radices(const uint32_t *degrees, size_t variable_count,
                              uint32_t *out_radices,
                              uint32_t *out_strides)
{
    uint32_t stride = 1u;
    for (size_t variable = 0u; variable < variable_count; ++variable) {
        if (degrees[variable] >= REDUCE_MAX_DEGREE) {
            return false;
        }
        out_radices[variable] = degrees[variable] + 1u;
        out_strides[variable] = stride;
        if (variable + 1u < variable_count) {
            if (stride >
                (uint32_t)REDUCE_MAX_DEGREE /
                    out_radices[variable]) {
                return false;
            }
            stride *= out_radices[variable];
        }
    }
    return true;
}

static phy_status kronecker_poly_from_ir(
    phy_cas *cas, phy_ir_ref expression, const phy_ir_ref *variables,
    size_t variable_count, const uint32_t *strides,
    rational_poly *out_polynomial, bool *out_fits)
{
    poly_zero(cas, out_polynomial);
    *out_fits = true;
    const bool split =
        phy_ir_kind_of(cas->ir, expression) == PHY_IR_ADD;
    const size_t count =
        split ? phy_ir_child_count(cas->ir, expression) : 1u;
    for (size_t term_index = 0u; term_index < count; ++term_index) {
        const phy_ir_ref term =
            split ? phy_ir_child(cas->ir, expression, term_index)
                  : expression;
        uint32_t exponents[REDUCE_MAX_CANDIDATES] = {0u};
        phy_ir_ref coefficient = PHY_IR_NULL;
        bool fits = false;
        phy_status status = kronecker_term(
            cas, term, variables, variable_count, exponents,
            &coefficient, &fits);
        if (status != PHY_OK || !fits) {
            *out_fits = fits;
            return status;
        }
        uint32_t encoded = 0u;
        for (size_t variable = 0u; variable < variable_count; ++variable) {
            const uint32_t contribution =
                exponents[variable] * strides[variable];
            if (contribution > (uint32_t)REDUCE_MAX_DEGREE - encoded) {
                *out_fits = false;
                return PHY_OK;
            }
            encoded += contribution;
        }
        phy_ir_ref updated = PHY_IR_NULL;
        status = coefficient_add(
            cas, out_polynomial->coefficients[encoded],
            coefficient, &updated);
        if (status != PHY_OK) {
            return status;
        }
        out_polynomial->coefficients[encoded] = updated;
        if ((int64_t)encoded > out_polynomial->degree) {
            out_polynomial->degree = (int64_t)encoded;
        }
    }
    poly_trim(cas, out_polynomial);
    return PHY_OK;
}

static phy_status kronecker_poly_to_ir(
    phy_cas *cas, const rational_poly *polynomial,
    const phy_ir_ref *variables, size_t variable_count,
    const uint32_t *radices, phy_ir_ref *out_expression,
    bool *out_fits)
{
    *out_fits = true;
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    phy_status status = phy_cas_scratch_alloc(
        cas, (size_t)polynomial->degree + 1u, &offset);
    if (status != PHY_OK) {
        return status;
    }
    size_t used = 0u;
    for (int64_t encoded = 0;
         encoded <= polynomial->degree && status == PHY_OK; ++encoded) {
        const phy_ir_ref coefficient =
            polynomial->coefficients[encoded];
        if (coefficient_is_zero(cas, coefficient)) {
            continue;
        }
        phy_ir_ref factors[REDUCE_MAX_CANDIDATES + 1u];
        size_t factor_count = 0u;
        if (!phy_cas_is_integer(cas, coefficient, 1)) {
            factors[factor_count++] = coefficient;
        }
        uint32_t remaining = (uint32_t)encoded;
        for (size_t variable = 0u; variable < variable_count; ++variable) {
            const uint32_t exponent = remaining % radices[variable];
            remaining /= radices[variable];
            if (exponent == 0u) {
                continue;
            }
            phy_ir_ref factor = variables[variable];
            if (exponent > 1u) {
                phy_ir_ref exponent_ref = PHY_IR_NULL;
                status = phy_cas_number_node(
                    cas, (phy_cas_rat){(int64_t)exponent, 1},
                    &exponent_ref);
                if (status == PHY_OK) {
                    status = phy_cas_pow_node(
                        cas, factor, exponent_ref, &factor);
                }
            }
            if (status != PHY_OK) {
                break;
            }
            factors[factor_count++] = factor;
        }
        if (remaining != 0u) {
            *out_fits = false;
            status = PHY_OK;
            break;
        }
        phy_ir_ref term = PHY_IR_NULL;
        status =
            phy_cas_mul_node(cas, factors, factor_count, &term);
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, offset)[used++] = term;
        }
    }
    if (status == PHY_OK && *out_fits) {
        status = phy_cas_add_at(cas, offset, used, out_expression);
    }
    if (status == PHY_OK && *out_fits) {
        status =
            phy_cas_expand_node(cas, *out_expression, out_expression);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

static phy_status kronecker_product_matches(
    phy_cas *cas, phy_ir_ref factor, phy_ir_ref quotient,
    phy_ir_ref expected, bool *out_matches)
{
    const phy_ir_ref pair[2] = {factor, quotient};
    phy_ir_ref product = PHY_IR_NULL;
    phy_status status = phy_cas_mul_node(cas, pair, 2u, &product);
    if (status == PHY_OK) {
        status = phy_cas_expand_node(cas, product, &product);
    }
    if (status == PHY_OK) {
        *out_matches = product == expected;
    }
    return status;
}

static phy_status poly_multiply_exact(phy_cas *cas,
                                      const rational_poly *left,
                                      const rational_poly *right,
                                      rational_poly *out_product)
{
    poly_zero(cas, out_product);
    if (poly_is_zero(cas, left) || poly_is_zero(cas, right)) {
        return PHY_OK;
    }
    if (left->degree > REDUCE_MAX_DEGREE - right->degree) {
        return PHY_ERR_TERM_LIMIT;
    }
    out_product->degree = left->degree + right->degree;
    for (int64_t left_degree = 0; left_degree <= left->degree;
         ++left_degree) {
        for (int64_t right_degree = 0; right_degree <= right->degree;
             ++right_degree) {
            phy_ir_ref product = PHY_IR_NULL;
            phy_ir_ref sum = PHY_IR_NULL;
            phy_status status = coefficient_multiply(
                cas, left->coefficients[left_degree],
                right->coefficients[right_degree], &product);
            if (status == PHY_OK) {
                status = coefficient_add(
                    cas,
                    out_product->coefficients[
                        left_degree + right_degree],
                    product, &sum);
            }
            if (status != PHY_OK) {
                return status;
            }
            out_product->coefficients[left_degree + right_degree] = sum;
        }
    }
    poly_trim(cas, out_product);
    return PHY_OK;
}

static void factor_record_polynomial(
    phy_cas *cas, const factor_workspace *workspace, size_t index,
    rational_poly *out_polynomial)
{
    poly_zero(cas, out_polynomial);
    const factor_record *record = &workspace->records[index];
    out_polynomial->degree = record->degree;
    for (int64_t degree = 0; degree <= record->degree; ++degree) {
        out_polynomial->coefficients[degree] =
            workspace->coefficients[
                record->coefficient_offset + (size_t)degree];
    }
}

static phy_status try_kronecker_candidate(
    phy_cas *cas, const rational_poly *numerator_poly,
    const rational_poly *denominator_poly,
    const rational_poly *candidate,
    const phy_ir_ref *variables, size_t variable_count,
    const uint32_t *radices, phy_ir_ref expected_numerator,
    phy_ir_ref expected_denominator, phy_ir_ref *out_numerator,
    phy_ir_ref *out_denominator, bool *out_accepted)
{
    *out_accepted = false;
    rational_poly numerator_quotient;
    rational_poly denominator_quotient;
    rational_poly scaled_candidate = *candidate;
    poly_zero(cas, &numerator_quotient);
    poly_zero(cas, &denominator_quotient);
    bool numerator_exact = false;
    bool denominator_exact = false;
    phy_status status = poly_divide_exact(
        cas, numerator_poly, candidate, &numerator_quotient,
        &numerator_exact);
    if (status == PHY_OK) {
        status = poly_divide_exact(
            cas, denominator_poly, candidate, &denominator_quotient,
            &denominator_exact);
    }
    if (status != PHY_OK) {
        return status;
    }
    if (!numerator_exact || !denominator_exact ||
        poly_is_zero(cas, &denominator_quotient)) {
        return PHY_OK;
    }

    const phy_ir_ref denominator_lead =
        denominator_quotient.coefficients[denominator_quotient.degree];
    for (int64_t degree = 0;
         degree <= numerator_quotient.degree; ++degree) {
        status = coefficient_divide(
            cas, numerator_quotient.coefficients[degree],
            denominator_lead,
            &numerator_quotient.coefficients[degree]);
        if (status != PHY_OK) {
            return status;
        }
    }
    for (int64_t degree = 0;
         degree <= denominator_quotient.degree; ++degree) {
        status = coefficient_divide(
            cas, denominator_quotient.coefficients[degree],
            denominator_lead,
            &denominator_quotient.coefficients[degree]);
        if (status != PHY_OK) {
            return status;
        }
    }
    for (int64_t degree = 0; degree <= scaled_candidate.degree; ++degree) {
        status = coefficient_multiply(
            cas, scaled_candidate.coefficients[degree], denominator_lead,
            &scaled_candidate.coefficients[degree]);
        if (status != PHY_OK) {
            return status;
        }
    }

    bool fits = false;
    phy_ir_ref decoded_candidate = PHY_IR_NULL;
    phy_ir_ref decoded_numerator = PHY_IR_NULL;
    phy_ir_ref decoded_denominator = PHY_IR_NULL;
    status = kronecker_poly_to_ir(
        cas, &scaled_candidate, variables, variable_count, radices,
        &decoded_candidate, &fits);
    if (status == PHY_OK && fits) {
        status = kronecker_poly_to_ir(
            cas, &numerator_quotient, variables, variable_count,
            radices, &decoded_numerator, &fits);
    }
    if (status == PHY_OK && fits) {
        status = kronecker_poly_to_ir(
            cas, &denominator_quotient, variables, variable_count,
            radices, &decoded_denominator, &fits);
    }
    if (status != PHY_OK || !fits) {
        return status;
    }

    bool numerator_matches = false;
    bool denominator_matches = false;
    status = kronecker_product_matches(
        cas, decoded_candidate, decoded_numerator, expected_numerator,
        &numerator_matches);
    if (status == PHY_OK) {
        status = kronecker_product_matches(
            cas, decoded_candidate, decoded_denominator,
            expected_denominator, &denominator_matches);
    }
    if (status != PHY_OK) {
        return status;
    }
    if (numerator_matches && denominator_matches) {
        *out_numerator = decoded_numerator;
        *out_denominator = decoded_denominator;
        *out_accepted = true;
    }
    return PHY_OK;
}

/*
 * Kronecker images often acquire the universal linear factors t, t-1, or
 * t+1 from unrelated multivariate factors.  Strip every bounded combination
 * of those factors and retain only a cofactor whose decoded products verify.
 * This also works when the remaining exact coefficients do not fit int64.
 */
static phy_status try_kronecker_universal_cofactors(
    phy_cas *cas, const rational_poly *numerator_poly,
    const rational_poly *denominator_poly, const rational_poly *gcd,
    const phy_ir_ref *variables, size_t variable_count,
    const uint32_t *radices, phy_ir_ref expected_numerator,
    phy_ir_ref expected_denominator, phy_ir_ref *out_numerator,
    phy_ir_ref *out_denominator, bool *out_accepted)
{
    static const phy_cas_rat roots[] = {
        {0, 1},
        {-1, 1},
        {1, 1},
    };
    rational_poly factors[sizeof(roots) / sizeof(roots[0])];
    unsigned multiplicities[sizeof(roots) / sizeof(roots[0])] = {0u};
    size_t combinations = 1u;
    *out_accepted = false;

    for (size_t index = 0u; index < sizeof(roots) / sizeof(roots[0]);
         ++index) {
        phy_ir_ref root = PHY_IR_NULL;
        phy_status status = phy_cas_number_node(cas, roots[index], &root);
        poly_zero(cas, &factors[index]);
        factors[index].degree = 1;
        factors[index].coefficients[1] = cas->one;
        if (status == PHY_OK) {
            status = coefficient_subtract(
                cas, cas->zero, root, &factors[index].coefficients[0]);
        }
        if (status != PHY_OK) {
            return status;
        }

        rational_poly remaining = *gcd;
        while (remaining.degree > 0) {
            rational_poly quotient;
            bool exact = false;
            status = poly_divide_exact(
                cas, &remaining, &factors[index], &quotient, &exact);
            if (status != PHY_OK) {
                return status;
            }
            if (!exact) {
                break;
            }
            ++multiplicities[index];
            remaining = quotient;
        }
        const size_t choices = (size_t)multiplicities[index] + 1u;
        if (combinations > REDUCE_MAX_ROOT_TRIALS / choices) {
            return PHY_OK;
        }
        combinations *= choices;
    }

    int64_t best_degree = 0;
    phy_ir_ref best_numerator = PHY_IR_NULL;
    phy_ir_ref best_denominator = PHY_IR_NULL;
    for (size_t code = 1u; code < combinations; ++code) {
        size_t digits = code;
        rational_poly candidate = *gcd;
        phy_status status = PHY_OK;
        for (size_t index = 0u;
             index < sizeof(roots) / sizeof(roots[0]); ++index) {
            const unsigned removed =
                (unsigned)(digits % ((size_t)multiplicities[index] + 1u));
            digits /= (size_t)multiplicities[index] + 1u;
            for (unsigned power = 0u; power < removed; ++power) {
                rational_poly quotient;
                bool exact = false;
                status = poly_divide_exact(
                    cas, &candidate, &factors[index], &quotient, &exact);
                if (status != PHY_OK) {
                    return status;
                }
                if (!exact) {
                    return PHY_ERR_CORRUPT_DOCUMENT;
                }
                candidate = quotient;
            }
        }
        if (candidate.degree <= best_degree) {
            continue;
        }

        bool accepted = false;
        phy_ir_ref candidate_numerator = PHY_IR_NULL;
        phy_ir_ref candidate_denominator = PHY_IR_NULL;
        status = try_kronecker_candidate(
            cas, numerator_poly, denominator_poly, &candidate, variables,
            variable_count, radices, expected_numerator,
            expected_denominator, &candidate_numerator,
            &candidate_denominator, &accepted);
        if (status != PHY_OK) {
            return status;
        }
        if (accepted) {
            best_degree = candidate.degree;
            best_numerator = candidate_numerator;
            best_denominator = candidate_denominator;
        }
    }
    if (best_degree > 0) {
        *out_numerator = best_numerator;
        *out_denominator = best_denominator;
        *out_accepted = true;
    }
    return PHY_OK;
}

/*
 * Mixed-radix Kronecker substitution is a fast exact candidate generator for
 * bounded multivariate GCD. A univariate image may have spurious factors, so
 * no result is published until the decoded GCD and both decoded quotients
 * multiply back to the original expanded polynomials.
 */
static phy_status cancel_multivariate_gcd(
    phy_cas *cas, phy_ir_ref numerator, phy_ir_ref denominator,
    phy_ir_ref *out_num, phy_ir_ref *out_den)
{
    phy_ir_ref variables[REDUCE_MAX_CANDIDATES];
    size_t variable_count = 0u;
    phy_status status = collect_factor_variables(
        cas, numerator, variables, &variable_count);
    if (status == PHY_OK) {
        status = collect_factor_variables(
            cas, denominator, variables, &variable_count);
    }
    if (status != PHY_OK) {
        return status == PHY_ERR_UNSUPPORTED ? PHY_OK : status;
    }
    if (variable_count < 2u) {
        return PHY_OK;
    }
    sort_variables(cas, variables, variable_count);

    uint32_t degrees[REDUCE_MAX_CANDIDATES] = {0u};
    bool fits = false;
    status = kronecker_degrees(
        cas, numerator, variables, variable_count, degrees, &fits);
    if (status == PHY_OK && fits) {
        status = kronecker_degrees(
            cas, denominator, variables, variable_count, degrees, &fits);
    }
    if (status != PHY_OK || !fits) {
        return status;
    }
    uint32_t radices[REDUCE_MAX_CANDIDATES] = {0u};
    uint32_t strides[REDUCE_MAX_CANDIDATES] = {0u};
    if (!kronecker_radices(
            degrees, variable_count, radices, strides)) {
        return PHY_OK;
    }

    rational_poly numerator_poly;
    rational_poly denominator_poly;
    status = kronecker_poly_from_ir(
        cas, numerator, variables, variable_count, strides,
        &numerator_poly, &fits);
    if (status == PHY_OK && fits) {
        status = kronecker_poly_from_ir(
            cas, denominator, variables, variable_count, strides,
            &denominator_poly, &fits);
    }
    if (status != PHY_OK || !fits ||
        poly_is_zero(cas, &numerator_poly)) {
        return status;
    }

    rational_poly gcd;
    status = poly_gcd(
        cas, &numerator_poly, &denominator_poly, &gcd);
    if (status != PHY_OK || gcd.degree == 0) {
        return status;
    }
    bool accepted = false;
    status = try_kronecker_candidate(
        cas, &numerator_poly, &denominator_poly, &gcd, variables,
        variable_count, radices, numerator, denominator, out_num, out_den,
        &accepted);
    if (status != PHY_OK || accepted) {
        return status;
    }

    status = try_kronecker_universal_cofactors(
        cas, &numerator_poly, &denominator_poly, &gcd, variables,
        variable_count, radices, numerator, denominator, out_num, out_den,
        &accepted);
    if (status != PHY_OK || accepted) {
        return status;
    }

    /*
     * The full univariate GCD can contain substitution artefacts. Factor it
     * completely on the currently certified Q[x] class and search its divisor
     * lattice. The largest decoded divisor that passes both product checks is
     * the true multivariate GCD within this bounded image.
     */
    factor_workspace workspace;
    status = factor_complete(cas, &gcd, &workspace);
    if (status == PHY_ERR_UNSUPPORTED || status == PHY_ERR_TERM_LIMIT ||
        status == PHY_ERR_OVERFLOW) {
        return PHY_OK;
    }
    if (status != PHY_OK) {
        return status;
    }
    size_t combinations = 1u;
    for (size_t index = 0u; index < workspace.count; ++index) {
        const size_t choices =
            (size_t)workspace.records[index].multiplicity + 1u;
        if (choices == 0u ||
            combinations > REDUCE_MAX_ROOT_TRIALS / choices) {
            return PHY_OK;
        }
        combinations *= choices;
    }

    int64_t best_degree = 0;
    phy_ir_ref best_numerator = PHY_IR_NULL;
    phy_ir_ref best_denominator = PHY_IR_NULL;
    for (size_t code = 1u; code + 1u < combinations; ++code) {
        size_t digits = code;
        rational_poly candidate;
        poly_zero(cas, &candidate);
        candidate.coefficients[0] = cas->one;
        for (size_t index = 0u;
             index < workspace.count && status == PHY_OK; ++index) {
            const unsigned multiplicity =
                workspace.records[index].multiplicity;
            const unsigned selected =
                (unsigned)(digits % ((size_t)multiplicity + 1u));
            digits /= (size_t)multiplicity + 1u;
            rational_poly factor;
            factor_record_polynomial(cas, &workspace, index, &factor);
            for (unsigned power = 0u; power < selected; ++power) {
                rational_poly product;
                status = poly_multiply_exact(
                    cas, &candidate, &factor, &product);
                candidate = product;
            }
        }
        if (status != PHY_OK) {
            return status;
        }
        if (candidate.degree <= best_degree) {
            continue;
        }
        phy_ir_ref candidate_numerator = PHY_IR_NULL;
        phy_ir_ref candidate_denominator = PHY_IR_NULL;
        accepted = false;
        status = try_kronecker_candidate(
            cas, &numerator_poly, &denominator_poly, &candidate,
            variables, variable_count, radices, numerator, denominator,
            &candidate_numerator, &candidate_denominator, &accepted);
        if (status != PHY_OK) {
            return status;
        }
        if (accepted) {
            best_degree = candidate.degree;
            best_numerator = candidate_numerator;
            best_denominator = candidate_denominator;
        }
    }
    if (best_degree > 0) {
        *out_num = best_numerator;
        *out_den = best_denominator;
    }
    return PHY_OK;
}

/*
 * Prefer the direct univariate Q[x] path, then offer the expanded pair to the
 * separately verified bounded multivariate candidate path. Non-polynomial
 * generators and images above REDUCE_MAX_DEGREE leave the pair unchanged.
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

    if (!phy_cas_is_exact(cas, denominator_coefficient) ||
        phy_cas_exact_sign_ref(cas, denominator_coefficient) == 0) {
        return PHY_OK;
    }
    phy_ir_ref scale = PHY_IR_NULL;
    phy_ir_ref scaled_numerator = PHY_IR_NULL;
    status = coefficient_divide(
        cas, cas->one, denominator_coefficient, &scale);
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
            poly_is_zero(cas, &num_poly)) {
            continue;
        }

        rational_poly gcd;
        status = poly_gcd(cas, &num_poly, &den_poly, &gcd);
        if (status != PHY_OK) {
            return status;
        }
        if (gcd.degree == 0) {
            continue;
        }

        rational_poly num_quotient;
        rational_poly den_quotient;
        poly_zero(cas, &num_quotient);
        poly_zero(cas, &den_quotient);
        bool num_exact = false;
        bool den_exact = false;
        status = poly_divide_exact(
            cas, &num_poly, &gcd, &num_quotient, &num_exact);
        if (status == PHY_OK) {
            status = poly_divide_exact(
                cas, &den_poly, &gcd, &den_quotient, &den_exact);
        }
        if (status != PHY_OK) {
            return status;
        }
        if (!num_exact || !den_exact) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }

        /* A unique reduced pair: make the denominator monic. */
        const phy_ir_ref denominator_lead =
            den_quotient.coefficients[den_quotient.degree];
        for (int64_t degree = 0; degree <= num_quotient.degree; ++degree) {
            phy_ir_ref normalized = PHY_IR_NULL;
            status = coefficient_divide(
                cas, num_quotient.coefficients[degree],
                denominator_lead, &normalized);
            if (status != PHY_OK) {
                return status;
            }
            num_quotient.coefficients[degree] = normalized;
        }
        for (int64_t degree = 0; degree <= den_quotient.degree; ++degree) {
            phy_ir_ref normalized = PHY_IR_NULL;
            status = coefficient_divide(
                cas, den_quotient.coefficients[degree],
                denominator_lead, &normalized);
            if (status != PHY_OK) {
                return status;
            }
            den_quotient.coefficients[degree] = normalized;
        }

        status = poly_to_ir(cas, &num_quotient, variable, out_num);
        return status == PHY_OK
                   ? poly_to_ir(cas, &den_quotient, variable, out_den)
                   : status;
    }
    return cancel_multivariate_gcd(
        cas, expanded_numerator, expanded_denominator, out_num, out_den);
}

/* ---------------------------------------------------------- cancellation */

phy_status phy_cas_cancel_known_factors(phy_cas *cas, phy_ir_ref numerator,
                                        phy_ir_ref denominator,
                                        phy_ir_ref *out_num,
                                        phy_ir_ref *out_den)
{
    *out_num = numerator;
    *out_den = denominator;
    reduce_factors factors = {0};
    phy_status factors_status = factors_of(cas, denominator, &factors);
    if (factors_status == PHY_ERR_TERM_LIMIT) {
        return PHY_OK; /* an unrecognized denominator shape stays as-is */
    }
    if (factors_status != PHY_OK) {
        return factors_status;
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
    reduce_factors factors = {0};
    phy_ir_ref inverse = PHY_IR_NULL;
    status = factors_of(cas, denominator, &factors);
    if (status != PHY_OK) {
        return status;
    }
    status = factors_build(cas, &factors, true, &inverse);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref pair[2] = {numerator, inverse};
    return phy_cas_mul_node(cas, pair, 2u, out_ref);
}

phy_status phy_cas_factor(phy_cas *cas, phy_ir_ref expr,
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

    phy_ir_ref simplified = PHY_IR_NULL;
    phy_ir_ref expanded = PHY_IR_NULL;
    phy_status status = phy_cas_simplify_node(cas, expr, &simplified);
    if (status == PHY_OK) {
        status = phy_cas_expand_node(cas, simplified, &expanded);
    }
    if (status != PHY_OK) {
        return status;
    }
    if (phy_cas_is_exact(cas, expanded)) {
        *out_ref = expanded;
        return PHY_OK;
    }

    phy_ir_ref variables[REDUCE_MAX_CANDIDATES];
    size_t variable_count = 0u;
    status = collect_factor_variables(
        cas, expanded, variables, &variable_count);
    if (status != PHY_OK) {
        return status;
    }
    if (variable_count == 0u) {
        *out_ref = expanded;
        return PHY_OK;
    }
    if (variable_count != 1u) {
        return PHY_ERR_UNSUPPORTED;
    }

    rational_poly polynomial;
    bool fits = false;
    status = poly_from_ir(
        cas, expanded, variables[0], &polynomial, &fits);
    if (status != PHY_OK) {
        return status;
    }
    if (!fits) {
        return PHY_ERR_UNSUPPORTED;
    }
    if (poly_is_zero(cas, &polynomial)) {
        *out_ref = cas->zero;
        return PHY_OK;
    }
    if (polynomial.degree == 0) {
        *out_ref = expanded;
        return PHY_OK;
    }

    const phy_ir_ref leading =
        polynomial.coefficients[polynomial.degree];
    status = poly_make_monic(cas, &polynomial);
    if (status != PHY_OK) {
        return status;
    }

    const size_t bytes = sizeof(factor_workspace);
    factor_workspace *workspace = NULL;
    status = phy_cas_temp_alloc(cas, bytes, (void **)&workspace);
    if (status != PHY_OK) {
        return status;
    }
    status = factor_complete(cas, &polynomial, workspace);
    if (status == PHY_OK) {
        status = build_factorization(
            cas, variables[0], leading, workspace, out_ref);
    }
    phy_cas_temp_free(cas, workspace, bytes);
    return status;
}
