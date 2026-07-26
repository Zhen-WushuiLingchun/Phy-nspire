#include "qo_dirac.h"

double qo_metric(int mu, int nu)
{
    if (mu != nu) {
        return 0.0;
    }
    return (mu == 0) ? 1.0 : -1.0;
}

qo_v4 qo_v4_make(double e, double x, double y, double z)
{
    qo_v4 p;
    p.c[0] = e;
    p.c[1] = x;
    p.c[2] = y;
    p.c[3] = z;
    return p;
}

double qo_v4_dot(qo_v4 p, qo_v4 q)
{
    return p.c[0] * q.c[0] - p.c[1] * q.c[1] - p.c[2] * q.c[2] - p.c[3] * q.c[3];
}

qo_v4 qo_v4_add(qo_v4 p, qo_v4 q)
{
    qo_v4 r;
    for (int i = 0; i < 4; ++i) {
        r.c[i] = p.c[i] + q.c[i];
    }
    return r;
}

qo_v4 qo_v4_sub(qo_v4 p, qo_v4 q)
{
    qo_v4 r;
    for (int i = 0; i < 4; ++i) {
        r.c[i] = p.c[i] - q.c[i];
    }
    return r;
}

qo_v4 qo_v4_neg(qo_v4 p)
{
    qo_v4 r;
    for (int i = 0; i < 4; ++i) {
        r.c[i] = -p.c[i];
    }
    return r;
}

qo_m4 qo_m4_zero(void)
{
    qo_m4 a;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            a.m[i][j] = qo_c_zero();
        }
    }
    return a;
}

qo_m4 qo_m4_identity(void)
{
    qo_m4 a = qo_m4_zero();
    for (int i = 0; i < 4; ++i) {
        a.m[i][i] = qo_c_one();
    }
    return a;
}

qo_m4 qo_m4_add(qo_m4 a, qo_m4 b)
{
    qo_m4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = qo_c_add(a.m[i][j], b.m[i][j]);
        }
    }
    return r;
}

qo_m4 qo_m4_sub(qo_m4 a, qo_m4 b)
{
    qo_m4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = qo_c_sub(a.m[i][j], b.m[i][j]);
        }
    }
    return r;
}

qo_m4 qo_m4_mul(qo_m4 a, qo_m4 b)
{
    qo_m4 r = qo_m4_zero();
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            qo_c acc = qo_c_zero();
            for (int k = 0; k < 4; ++k) {
                acc = qo_c_add(acc, qo_c_mul(a.m[i][k], b.m[k][j]));
            }
            r.m[i][j] = acc;
        }
    }
    return r;
}

qo_m4 qo_m4_scale(qo_m4 a, qo_c s)
{
    qo_m4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = qo_c_mul(a.m[i][j], s);
        }
    }
    return r;
}

qo_c qo_m4_trace(qo_m4 a)
{
    qo_c t = qo_c_zero();
    for (int i = 0; i < 4; ++i) {
        t = qo_c_add(t, a.m[i][i]);
    }
    return t;
}

double qo_m4_maxdiff(qo_m4 a, qo_m4 b)
{
    double worst = 0.0;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const double d = qo_c_dist(a.m[i][j], b.m[i][j]);
            if (d > worst) {
                worst = d;
            }
        }
    }
    return worst;
}

/*
 * Pauli matrices, the 2x2 building blocks of both representations.
 * sigma[k] for k = 0,1,2 is sigma^{k+1}.
 */
static qo_c pauli(int k, int i, int j)
{
    switch (k) {
    case 0: /* sigma^1 = [[0,1],[1,0]] */
        return (i != j) ? qo_c_one() : qo_c_zero();
    case 1: /* sigma^2 = [[0,-i],[i,0]] */
        if (i == 0 && j == 1) {
            return qo_c_make(0.0, -1.0);
        }
        if (i == 1 && j == 0) {
            return qo_c_make(0.0, 1.0);
        }
        return qo_c_zero();
    default: /* sigma^3 = [[1,0],[0,-1]] */
        if (i != j) {
            return qo_c_zero();
        }
        return (i == 0) ? qo_c_one() : qo_c_make(-1.0, 0.0);
    }
}

/* Writes the 2x2 block at (row_block, col_block) of a 4x4 matrix. */
static void set_block(qo_m4 *a, int row_block, int col_block, int k, double scale)
{
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            const qo_c v = pauli(k, i, j);
            a->m[row_block * 2 + i][col_block * 2 + j] = qo_c_scale(v, scale);
        }
    }
}

static void set_block_identity(qo_m4 *a, int row_block, int col_block, double scale)
{
    for (int i = 0; i < 2; ++i) {
        a->m[row_block * 2 + i][col_block * 2 + i] = qo_c_real(scale);
    }
}

qo_m4 qo_gamma(qo_basis basis, int mu)
{
    qo_m4 g = qo_m4_zero();

    if (basis == QO_BASIS_DIRAC) {
        if (mu == 0) {
            /* gamma^0 = diag(1,1,-1,-1) */
            set_block_identity(&g, 0, 0, 1.0);
            set_block_identity(&g, 1, 1, -1.0);
        } else {
            /* gamma^i = [[0, sigma^i], [-sigma^i, 0]] */
            set_block(&g, 0, 1, mu - 1, 1.0);
            set_block(&g, 1, 0, mu - 1, -1.0);
        }
        return g;
    }

    /* Weyl (chiral) representation. */
    if (mu == 0) {
        /* gamma^0 = [[0, 1], [1, 0]] */
        set_block_identity(&g, 0, 1, 1.0);
        set_block_identity(&g, 1, 0, 1.0);
    } else {
        /* gamma^i = [[0, sigma^i], [-sigma^i, 0]] */
        set_block(&g, 0, 1, mu - 1, 1.0);
        set_block(&g, 1, 0, mu - 1, -1.0);
    }
    return g;
}

qo_m4 qo_gamma_lower(qo_basis basis, int mu)
{
    return qo_m4_scale(qo_gamma(basis, mu), qo_c_real(qo_metric(mu, mu)));
}

qo_m4 qo_slash(qo_basis basis, qo_v4 p)
{
    qo_m4 r = qo_m4_zero();
    for (int mu = 0; mu < 4; ++mu) {
        /* p_mu gamma^mu = g_{mu nu} p^nu gamma^mu */
        const double lowered = qo_metric(mu, mu) * p.c[mu];
        r = qo_m4_add(r, qo_m4_scale(qo_gamma(basis, mu), qo_c_real(lowered)));
    }
    return r;
}

qo_m4 qo_gamma_product(qo_basis basis, const int *mu, int n)
{
    qo_m4 r = qo_m4_identity();
    for (int i = 0; i < n; ++i) {
        r = qo_m4_mul(r, qo_gamma(basis, mu[i]));
    }
    return r;
}

double qo_trace_recursive(const int *mu, int n)
{
    if (n % 2 != 0) {
        return 0.0;
    }
    if (n == 0) {
        return 4.0;
    }

    int rest[QO_MAX_GAMMAS];
    double sum = 0.0;

    for (int j = 1; j < n; ++j) {
        const double g = qo_metric(mu[0], mu[j]);
        if (g == 0.0) {
            continue;
        }
        /* The recursion carries (-1)^j with j counted from 1; the loop index
         * is zero-based, so position j here is textbook position j+1. */
        const double sign = (((j + 1) % 2) == 0) ? 1.0 : -1.0;

        int k = 0;
        for (int i = 1; i < n; ++i) {
            if (i != j) {
                rest[k++] = mu[i];
            }
        }
        sum += sign * g * qo_trace_recursive(rest, n - 2);
    }
    return sum;
}
