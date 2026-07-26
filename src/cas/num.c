/*
 * Phy-nspire — exact rational arithmetic for the scalar CAS.
 *
 * int64 numerator over int64 denominator, reduced, denominator positive. No
 * IR, no allocation, no floating point: this file is pure arithmetic and is the
 * only place in the layer where an overflow can be introduced, which is why it
 * is the only place that has to be careful about one.
 *
 * Every operation reports overflow instead of wrapping. That is not caution for
 * its own sake -- a wrapped coefficient would make the zero decision in
 * normal.c report ZERO for an expression that is not zero, turning a proof into
 * a wrong answer. Refusing is the only sound alternative to arbitrary precision,
 * and docs/IR.md already records where a bignum would slot in.
 *
 * The checks are written in terms of unsigned magnitudes rather than compiler
 * builtins, because the host build also runs under MSVC and signed overflow
 * would be undefined behaviour if it were allowed to happen at all.
 */
#include "cas_internal.h"

/* Defined for every input, INT64_MIN included, unlike -value. */
static uint64_t magnitude(int64_t value)
{
    return (value < 0) ? (~(uint64_t)value + 1u) : (uint64_t)value;
}

static bool from_magnitude(uint64_t value, bool negative, int64_t *out)
{
    const uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u
                                    : (uint64_t)INT64_MAX;
    if (value > limit) {
        return false;
    }
    if (!negative) {
        *out = (int64_t)value;
    } else if (value == (uint64_t)INT64_MAX + 1u) {
        *out = INT64_MIN;
    } else {
        *out = -(int64_t)value;
    }
    return true;
}

static bool add_i64(int64_t a, int64_t b, int64_t *out)
{
    if (b > 0 && a > INT64_MAX - b) {
        return false;
    }
    if (b < 0 && a < INT64_MIN - b) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a != 0u && b > (uint64_t)-1 / a) {
        return false;
    }
    *out = a * b;
    return true;
}

static bool mul_i64(int64_t a, int64_t b, int64_t *out)
{
    uint64_t product;
    if (!mul_u64(magnitude(a), magnitude(b), &product)) {
        return false;
    }
    return from_magnitude(product, (a < 0) != (b < 0) && product != 0u, out);
}

static uint64_t gcd_u64(uint64_t a, uint64_t b)
{
    while (b != 0u) {
        const uint64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

/*
 * Reduce in place. The denominator is known positive on entry, so only the
 * common factor moves; a zero numerator collapses to 0/1 so that zero has one
 * spelling.
 */
static bool reduce(int64_t num, int64_t den, phy_cas_rat *out)
{
    if (den == 0) {
        return false;
    }
    if (num == 0) {
        out->num = 0;
        out->den = 1;
        return true;
    }

    const bool negative = (num < 0) != (den < 0);
    uint64_t n = magnitude(num);
    uint64_t d = magnitude(den);
    const uint64_t divisor = gcd_u64(n, d);
    n /= divisor;
    d /= divisor;

    /* Only the numerator can still be out of range, and only for INT64_MIN
       over an even denominator. */
    if (d > (uint64_t)INT64_MAX) {
        return false;
    }
    if (!from_magnitude(n, negative, &out->num)) {
        return false;
    }
    out->den = (int64_t)d;
    return true;
}

bool phy_cas_rat_add(phy_cas_rat a, phy_cas_rat b, phy_cas_rat *out)
{
    /*
     * Cross-multiplying the raw denominators overflows far earlier than it needs
     * to: 1/2 + 1/2 would form 2/4. Dividing out the common factor first keeps
     * sums of like fractions -- which is what collecting a polynomial's
     * coefficients produces -- inside the range.
     */
    const uint64_t common = gcd_u64((uint64_t)a.den, (uint64_t)b.den);
    const int64_t a_scale = b.den / (int64_t)common;
    const int64_t b_scale = a.den / (int64_t)common;

    int64_t left, right, den, num;
    if (!mul_i64(a.num, a_scale, &left) || !mul_i64(b.num, b_scale, &right) ||
        !add_i64(left, right, &num) || !mul_i64(a.den, a_scale, &den)) {
        return false;
    }
    return reduce(num, den, out);
}

bool phy_cas_rat_mul(phy_cas_rat a, phy_cas_rat b, phy_cas_rat *out)
{
    /*
     * Cancel crosswise before multiplying, for the same reason: 2/3 * 3/2 must
     * not need six-digit intermediates to reach 1, and a product of collected
     * coefficients is full of such pairs.
     */
    const int64_t left = (int64_t)gcd_u64(magnitude(a.num), (uint64_t)b.den);
    const int64_t right = (int64_t)gcd_u64(magnitude(b.num), (uint64_t)a.den);

    /* Both gcds divide a positive denominator, so neither is zero and neither
       exceeds INT64_MAX; the four divisions below are exact. */
    const int64_t an = a.num / left;
    const int64_t bd = b.den / left;
    const int64_t bn = b.num / right;
    const int64_t ad = a.den / right;

    int64_t num, den;
    if (!mul_i64(an, bn, &num) || !mul_i64(ad, bd, &den)) {
        return false;
    }
    return reduce(num, den, out);
}

bool phy_cas_rat_pow(phy_cas_rat base, int64_t exponent, phy_cas_rat *out)
{
    if (exponent == 0) {
        /* 0^0 is a domain question, decided by the caller before it gets here. */
        out->num = 1;
        out->den = 1;
        return true;
    }
    if (base.num == 0) {
        if (exponent < 0) {
            return false; /* 1/0; the caller reports the domain error */
        }
        out->num = 0;
        out->den = 1;
        return true;
    }

    /* A negative exponent inverts, so the sign of the exponent never reaches
       the loop and INT64_MIN cannot be negated into a trap. */
    uint64_t times = magnitude(exponent);
    phy_cas_rat factor = base;
    if (exponent < 0) {
        factor.num = base.den;
        factor.den = base.num;
        if (!reduce(factor.num, factor.den, &factor)) {
            return false;
        }
    }

    /* Square-and-multiply: a linear loop would spend 10^18 iterations before
       discovering that 2^(10^18) overflows. This discovers it in 60. */
    phy_cas_rat result = {1, 1};
    while (times != 0u) {
        if ((times & 1u) != 0u && !phy_cas_rat_mul(result, factor, &result)) {
            return false;
        }
        times >>= 1;
        if (times != 0u && !phy_cas_rat_mul(factor, factor, &factor)) {
            return false;
        }
    }
    *out = result;
    return true;
}

int phy_cas_rat_cmp_int(phy_cas_rat a, int64_t value)
{
    /* a.num/a.den - value, by sign of a.num - value*a.den. The product can
       overflow, in which case the magnitudes already decide the order. */
    int64_t scaled;
    if (!mul_i64(value, a.den, &scaled)) {
        return (value < 0) ? 1 : -1;
    }
    return (a.num > scaled) - (a.num < scaled);
}
