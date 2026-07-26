/*
 * Complex scalar arithmetic for the QFT oracle.
 *
 * The `qo_` prefix marks host-only verification code. Nothing under
 * tests/oracle/ is portable core: it never links into phy_core and never
 * reaches the device binary, exactly as phy_host_* must not (docs/BUILD.md,
 * "device symbol check"). The oracle exists to certify the golden corpus in
 * research/corpus/qft, not to ship.
 *
 * C99 _Complex is deliberately avoided: CMakeLists.txt keeps an MSVC branch
 * alive, and MSVC has no _Complex. A two-double struct costs nothing here and
 * keeps every host compiler in scope.
 */
#ifndef QO_COMPLEX_H
#define QO_COMPLEX_H

#include <math.h>

typedef struct {
    double re;
    double im;
} qo_c;

static inline qo_c qo_c_make(double re, double im)
{
    qo_c z;
    z.re = re;
    z.im = im;
    return z;
}

static inline qo_c qo_c_real(double re)
{
    return qo_c_make(re, 0.0);
}

static inline qo_c qo_c_zero(void)
{
    return qo_c_make(0.0, 0.0);
}

static inline qo_c qo_c_one(void)
{
    return qo_c_make(1.0, 0.0);
}

/* The imaginary unit. Named to avoid colliding with <complex.h>'s I macro. */
static inline qo_c qo_c_i(void)
{
    return qo_c_make(0.0, 1.0);
}

static inline qo_c qo_c_add(qo_c a, qo_c b)
{
    return qo_c_make(a.re + b.re, a.im + b.im);
}

static inline qo_c qo_c_sub(qo_c a, qo_c b)
{
    return qo_c_make(a.re - b.re, a.im - b.im);
}

static inline qo_c qo_c_neg(qo_c a)
{
    return qo_c_make(-a.re, -a.im);
}

static inline qo_c qo_c_mul(qo_c a, qo_c b)
{
    return qo_c_make(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

static inline qo_c qo_c_scale(qo_c a, double s)
{
    return qo_c_make(a.re * s, a.im * s);
}

static inline qo_c qo_c_conj(qo_c a)
{
    return qo_c_make(a.re, -a.im);
}

static inline double qo_c_abs2(qo_c a)
{
    return a.re * a.re + a.im * a.im;
}

static inline double qo_c_abs(qo_c a)
{
    return sqrt(qo_c_abs2(a));
}

static inline qo_c qo_c_div(qo_c a, qo_c b)
{
    const double d = qo_c_abs2(b);
    return qo_c_make((a.re * b.re + a.im * b.im) / d,
                     (a.im * b.re - a.re * b.im) / d);
}

/* Absolute difference. The oracle compares against exact rationals, so every
 * tolerance in the suites is an accumulated-rounding budget, never a fudge
 * factor hiding a wrong identity. */
static inline double qo_c_dist(qo_c a, qo_c b)
{
    return qo_c_abs(qo_c_sub(a, b));
}

static inline int qo_c_close(qo_c a, qo_c b, double tol)
{
    return qo_c_dist(a, b) <= tol;
}

#endif /* QO_COMPLEX_H */
