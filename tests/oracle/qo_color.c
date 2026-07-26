#include "qo_color.h"

int qo_su_adjoint_dim(int n)
{
    return n * n - 1;
}

qo_cm qo_cm_zero(int n)
{
    qo_cm a;
    a.n = n;
    for (int i = 0; i < QO_COLOR_MAX_N; ++i) {
        for (int j = 0; j < QO_COLOR_MAX_N; ++j) {
            a.m[i][j] = qo_c_zero();
        }
    }
    return a;
}

qo_cm qo_cm_identity(int n)
{
    qo_cm a = qo_cm_zero(n);
    for (int i = 0; i < n; ++i) {
        a.m[i][i] = qo_c_one();
    }
    return a;
}

qo_cm qo_cm_add(qo_cm a, qo_cm b)
{
    qo_cm r = qo_cm_zero(a.n);
    for (int i = 0; i < a.n; ++i) {
        for (int j = 0; j < a.n; ++j) {
            r.m[i][j] = qo_c_add(a.m[i][j], b.m[i][j]);
        }
    }
    return r;
}

qo_cm qo_cm_sub(qo_cm a, qo_cm b)
{
    qo_cm r = qo_cm_zero(a.n);
    for (int i = 0; i < a.n; ++i) {
        for (int j = 0; j < a.n; ++j) {
            r.m[i][j] = qo_c_sub(a.m[i][j], b.m[i][j]);
        }
    }
    return r;
}

qo_cm qo_cm_mul(qo_cm a, qo_cm b)
{
    qo_cm r = qo_cm_zero(a.n);
    for (int i = 0; i < a.n; ++i) {
        for (int j = 0; j < a.n; ++j) {
            qo_c acc = qo_c_zero();
            for (int k = 0; k < a.n; ++k) {
                acc = qo_c_add(acc, qo_c_mul(a.m[i][k], b.m[k][j]));
            }
            r.m[i][j] = acc;
        }
    }
    return r;
}

qo_cm qo_cm_scale(qo_cm a, qo_c s)
{
    qo_cm r = qo_cm_zero(a.n);
    for (int i = 0; i < a.n; ++i) {
        for (int j = 0; j < a.n; ++j) {
            r.m[i][j] = qo_c_mul(a.m[i][j], s);
        }
    }
    return r;
}

qo_cm qo_cm_commutator(qo_cm a, qo_cm b)
{
    return qo_cm_sub(qo_cm_mul(a, b), qo_cm_mul(b, a));
}

qo_cm qo_cm_adjoint(qo_cm a)
{
    qo_cm r = qo_cm_zero(a.n);
    for (int i = 0; i < a.n; ++i) {
        for (int j = 0; j < a.n; ++j) {
            r.m[i][j] = qo_c_conj(a.m[j][i]);
        }
    }
    return r;
}

qo_c qo_cm_trace(qo_cm a)
{
    qo_c t = qo_c_zero();
    for (int i = 0; i < a.n; ++i) {
        t = qo_c_add(t, a.m[i][i]);
    }
    return t;
}

double qo_cm_maxdiff(qo_cm a, qo_cm b)
{
    double worst = 0.0;
    for (int i = 0; i < a.n; ++i) {
        for (int j = 0; j < a.n; ++j) {
            const double d = qo_c_dist(a.m[i][j], b.m[i][j]);
            if (d > worst) {
                worst = d;
            }
        }
    }
    return worst;
}

/*
 * Generalised Gell-Mann ordering. For each column j = 1..N-1 the generators
 * appear as: symmetric(i,j) and antisymmetric(i,j) for i = 0..j-1, then the
 * j-th diagonal generator. For N = 3 this reproduces lambda_1..lambda_8 / 2
 * in the standard order.
 */
qo_cm qo_su_generator(int n, int a)
{
    qo_cm t = qo_cm_zero(n);
    int index = 0;

    for (int j = 1; j < n; ++j) {
        for (int i = 0; i < j; ++i) {
            if (index == a) { /* symmetric: (E_ij + E_ji)/2 */
                t.m[i][j] = qo_c_real(0.5);
                t.m[j][i] = qo_c_real(0.5);
                return t;
            }
            index++;
            if (index == a) { /* antisymmetric: (-i E_ij + i E_ji)/2 */
                t.m[i][j] = qo_c_make(0.0, -0.5);
                t.m[j][i] = qo_c_make(0.0, 0.5);
                return t;
            }
            index++;
        }
        if (index == a) {
            /* diagonal: diag(1,...,1,-j,0,...) / sqrt(2 j (j+1)) */
            const double norm = 1.0 / sqrt(2.0 * (double)j * ((double)j + 1.0));
            for (int i = 0; i < j; ++i) {
                t.m[i][i] = qo_c_real(norm);
            }
            t.m[j][j] = qo_c_real(-(double)j * norm);
            return t;
        }
        index++;
    }
    return t; /* a out of range: zero matrix */
}

double qo_su_f(int n, int a, int b, int c)
{
    const qo_cm ta = qo_su_generator(n, a);
    const qo_cm tb = qo_su_generator(n, b);
    const qo_cm tc = qo_su_generator(n, c);
    const qo_cm comm = qo_cm_commutator(ta, tb);
    const qo_c tr = qo_cm_trace(qo_cm_mul(comm, tc));
    /* f = -2i * tr, and tr is purely imaginary, so the result is real. */
    const qo_c f = qo_c_mul(qo_c_make(0.0, -2.0), tr);
    return f.re;
}

double qo_su_casimir_fundamental(int n)
{
    const double dn = (double)n;
    return (dn * dn - 1.0) / (2.0 * dn);
}

double qo_su_casimir_adjoint(int n)
{
    return (double)n;
}
