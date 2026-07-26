/*
 * Golden cases for the QFT/gauge MVP.
 *
 * Every identity asserted in docs/references/QFT_GAUGE.md is checked here
 * against an explicit matrix representation. The reference document cites this
 * suite; if an identity is edited there without a corresponding check landing
 * here, the claim is unverified and must be marked as such.
 *
 * Deferred deliberately, and therefore absent: gamma5, Fierz/completeness,
 * loop kinematics, diagram generation, spinors and squared amplitudes.
 */
#include <math.h>
#include <stdlib.h>

#include "phy_test.h"
#include "qo_color.h"
#include "qo_dirac.h"

/* Every quantity here is a small rational; this budget is pure accumulated
 * rounding from at most twelve 4x4 complex multiplications. */
#define TOL 1e-9

static const qo_basis kBases[QO_BASIS_COUNT] = {QO_BASIS_DIRAC, QO_BASIS_WEYL};

static const char *basis_name(qo_basis b)
{
    return (b == QO_BASIS_DIRAC) ? "Dirac" : "Weyl";
}

/* ---------------------------------------------------------------- Clifford */

/* {gamma^mu, gamma^nu} = 2 g^{mu nu} I. G-1 in the reference document. */
static void test_clifford_algebra(void)
{
    for (int b = 0; b < QO_BASIS_COUNT; ++b) {
        const qo_basis basis = kBases[b];
        for (int mu = 0; mu < 4; ++mu) {
            for (int nu = 0; nu < 4; ++nu) {
                const qo_m4 gm = qo_gamma(basis, mu);
                const qo_m4 gn = qo_gamma(basis, nu);
                const qo_m4 anti =
                    qo_m4_add(qo_m4_mul(gm, gn), qo_m4_mul(gn, gm));
                const qo_m4 want = qo_m4_scale(
                    qo_m4_identity(), qo_c_real(2.0 * qo_metric(mu, nu)));
                if (qo_m4_maxdiff(anti, want) > TOL) {
                    fprintf(stderr, "  basis=%s mu=%d nu=%d\n", basis_name(basis),
                            mu, nu);
                }
                PHY_CHECK(qo_m4_maxdiff(anti, want) <= TOL);
            }
        }
    }
}

/* --------------------------------------------------------- Lorentz contraction */

/*
 * The four MVP contraction identities at D = 4. These are FORM's trace rules 1
 * to 4 specialised to a single contracted pair, and the D = 4 case of the
 * identities quoted in SymPy's kahane_simplify docstring. G-2 to G-5.
 */
static void test_contraction_identities(void)
{
    for (int b = 0; b < QO_BASIS_COUNT; ++b) {
        const qo_basis basis = kBases[b];

        /* gamma^mu gamma_mu = 4 I */
        qo_m4 sum = qo_m4_zero();
        for (int mu = 0; mu < 4; ++mu) {
            sum = qo_m4_add(
                sum, qo_m4_mul(qo_gamma(basis, mu), qo_gamma_lower(basis, mu)));
        }
        PHY_CHECK(qo_m4_maxdiff(sum, qo_m4_scale(qo_m4_identity(),
                                                 qo_c_real(4.0))) <= TOL);

        /* gamma^mu gamma^nu gamma_mu = -2 gamma^nu */
        for (int nu = 0; nu < 4; ++nu) {
            qo_m4 acc = qo_m4_zero();
            for (int mu = 0; mu < 4; ++mu) {
                qo_m4 term = qo_m4_mul(qo_gamma(basis, mu), qo_gamma(basis, nu));
                term = qo_m4_mul(term, qo_gamma_lower(basis, mu));
                acc = qo_m4_add(acc, term);
            }
            const qo_m4 want =
                qo_m4_scale(qo_gamma(basis, nu), qo_c_real(-2.0));
            PHY_CHECK(qo_m4_maxdiff(acc, want) <= TOL);
        }

        /* gamma^mu gamma^nu gamma^rho gamma_mu = 4 g^{nu rho} I */
        for (int nu = 0; nu < 4; ++nu) {
            for (int rho = 0; rho < 4; ++rho) {
                qo_m4 acc = qo_m4_zero();
                for (int mu = 0; mu < 4; ++mu) {
                    qo_m4 term =
                        qo_m4_mul(qo_gamma(basis, mu), qo_gamma(basis, nu));
                    term = qo_m4_mul(term, qo_gamma(basis, rho));
                    term = qo_m4_mul(term, qo_gamma_lower(basis, mu));
                    acc = qo_m4_add(acc, term);
                }
                const qo_m4 want = qo_m4_scale(
                    qo_m4_identity(), qo_c_real(4.0 * qo_metric(nu, rho)));
                PHY_CHECK(qo_m4_maxdiff(acc, want) <= TOL);
            }
        }

        /* gamma^mu gamma^nu gamma^rho gamma^sigma gamma_mu
         *     = -2 gamma^sigma gamma^rho gamma^nu   (order reversed) */
        for (int nu = 0; nu < 4; ++nu) {
            for (int rho = 0; rho < 4; ++rho) {
                for (int sig = 0; sig < 4; ++sig) {
                    qo_m4 acc = qo_m4_zero();
                    for (int mu = 0; mu < 4; ++mu) {
                        qo_m4 term =
                            qo_m4_mul(qo_gamma(basis, mu), qo_gamma(basis, nu));
                        term = qo_m4_mul(term, qo_gamma(basis, rho));
                        term = qo_m4_mul(term, qo_gamma(basis, sig));
                        term = qo_m4_mul(term, qo_gamma_lower(basis, mu));
                        acc = qo_m4_add(acc, term);
                    }
                    const int reversed[3] = {sig, rho, nu};
                    const qo_m4 want = qo_m4_scale(
                        qo_gamma_product(basis, reversed, 3), qo_c_real(-2.0));
                    PHY_CHECK(qo_m4_maxdiff(acc, want) <= TOL);
                }
            }
        }
    }
}

/*
 * Regression for the ordering trap that SymPy shipped as issue #23823:
 * a contracted pair removed from the *end* of a string must not reverse the
 * leading free matrices. Both orderings must give 4 gamma^rho gamma^sigma.
 */
static void test_contraction_preserves_leading_order(void)
{
    for (int b = 0; b < QO_BASIS_COUNT; ++b) {
        const qo_basis basis = kBases[b];
        for (int rho = 0; rho < 4; ++rho) {
            for (int sig = 0; sig < 4; ++sig) {
                const int free_idx[2] = {rho, sig};
                const qo_m4 want = qo_m4_scale(
                    qo_gamma_product(basis, free_idx, 2), qo_c_real(4.0));

                /* gamma^mu gamma_mu gamma^rho gamma^sigma */
                qo_m4 leading = qo_m4_zero();
                /* gamma^rho gamma^sigma gamma^mu gamma_mu */
                qo_m4 trailing = qo_m4_zero();

                for (int mu = 0; mu < 4; ++mu) {
                    const qo_m4 pair = qo_m4_mul(qo_gamma(basis, mu),
                                                 qo_gamma_lower(basis, mu));
                    leading = qo_m4_add(
                        leading,
                        qo_m4_mul(pair, qo_gamma_product(basis, free_idx, 2)));
                    trailing = qo_m4_add(
                        trailing,
                        qo_m4_mul(qo_gamma_product(basis, free_idx, 2), pair));
                }
                PHY_CHECK(qo_m4_maxdiff(leading, want) <= TOL);
                PHY_CHECK(qo_m4_maxdiff(trailing, want) <= TOL);
            }
        }
    }
}

/* -------------------------------------------------------------------- traces */

/* Odd-length gamma strings are traceless. FORM trace rule 0. G-6. */
static void test_odd_traces_vanish(void)
{
    int mu[QO_MAX_GAMMAS];
    for (int b = 0; b < QO_BASIS_COUNT; ++b) {
        const qo_basis basis = kBases[b];
        for (int n = 1; n <= 5; n += 2) {
            const int total = 1 << (2 * n); /* 4^n index assignments */
            for (int code = 0; code < total; ++code) {
                int c = code;
                for (int i = 0; i < n; ++i) {
                    mu[i] = c & 3;
                    c >>= 2;
                }
                const qo_c tr = qo_m4_trace(qo_gamma_product(basis, mu, n));
                PHY_CHECK(qo_c_abs(tr) <= TOL);
                PHY_CHECK(fabs(qo_trace_recursive(mu, n)) <= TOL);
            }
        }
    }
}

/*
 * The metric recursion reproduces the explicit matrix trace for every index
 * assignment, exhaustively for n = 2, 4, 6 and on a deterministic stride for
 * n = 8. This validates the algorithm the Phase 5 engine must implement, not
 * just a handful of tabulated results. G-7.
 */
static void test_trace_recursion_matches_matrices(void)
{
    int mu[QO_MAX_GAMMAS];
    for (int b = 0; b < QO_BASIS_COUNT; ++b) {
        const qo_basis basis = kBases[b];

        PHY_CHECK(fabs(qo_c_abs(qo_m4_trace(qo_m4_identity())) - 4.0) <= TOL);

        for (int n = 2; n <= 8; n += 2) {
            const long total = 1L << (2 * n);
            const long stride = (n == 8) ? 37 : 1;
            for (long code = 0; code < total; code += stride) {
                long c = code;
                for (int i = 0; i < n; ++i) {
                    mu[i] = (int)(c & 3);
                    c >>= 2;
                }
                const qo_c explicit_tr =
                    qo_m4_trace(qo_gamma_product(basis, mu, n));
                const double recursive_tr = qo_trace_recursive(mu, n);

                /* The trace of a gamma string without gamma5 is real. */
                PHY_CHECK(fabs(explicit_tr.im) <= TOL);
                PHY_CHECK(fabs(explicit_tr.re - recursive_tr) <= TOL);
            }
        }
    }
}

/* Tr[g^mu g^nu] = 4 g^{mu nu}, written out rather than inferred. G-8. */
static void test_trace_two_gammas(void)
{
    for (int mu = 0; mu < 4; ++mu) {
        for (int nu = 0; nu < 4; ++nu) {
            const int idx[2] = {mu, nu};
            PHY_CHECK(fabs(qo_trace_recursive(idx, 2) -
                           4.0 * qo_metric(mu, nu)) <= TOL);
        }
    }
}

/* Tr[g^mu g^nu g^rho g^sigma]
 *   = 4 (g^{mu nu} g^{rho sigma} - g^{mu rho} g^{nu sigma}
 *        + g^{mu sigma} g^{nu rho}). G-9. */
static void test_trace_four_gammas(void)
{
    for (int mu = 0; mu < 4; ++mu) {
        for (int nu = 0; nu < 4; ++nu) {
            for (int rho = 0; rho < 4; ++rho) {
                for (int sig = 0; sig < 4; ++sig) {
                    const int idx[4] = {mu, nu, rho, sig};
                    const double want =
                        4.0 * (qo_metric(mu, nu) * qo_metric(rho, sig) -
                               qo_metric(mu, rho) * qo_metric(nu, sig) +
                               qo_metric(mu, sig) * qo_metric(nu, rho));
                    PHY_CHECK(fabs(qo_trace_recursive(idx, 4) - want) <= TOL);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------- slashes */

/* p-slash q-slash + q-slash p-slash = 2 (p.q) I. G-10. */
static void test_slash_anticommutator(void)
{
    const qo_v4 p = qo_v4_make(7.0, 1.0, -2.0, 3.0);
    const qo_v4 q = qo_v4_make(-5.0, 4.0, 0.5, -1.5);

    for (int b = 0; b < QO_BASIS_COUNT; ++b) {
        const qo_basis basis = kBases[b];
        const qo_m4 ps = qo_slash(basis, p);
        const qo_m4 qs = qo_slash(basis, q);
        const qo_m4 anti = qo_m4_add(qo_m4_mul(ps, qs), qo_m4_mul(qs, ps));
        const qo_m4 want =
            qo_m4_scale(qo_m4_identity(), qo_c_real(2.0 * qo_v4_dot(p, q)));
        PHY_CHECK(qo_m4_maxdiff(anti, want) <= TOL);
    }
}

/*
 * Tr[p1-slash p2-slash p3-slash p4-slash]
 *   = 4 [ (p1.p2)(p3.p4) - (p1.p3)(p2.p4) + (p1.p4)(p2.p3) ].
 * This is the contracted form the Phase 5 engine produces after Lorentz
 * contraction, and the shape every 2 -> 2 golden case reduces to. G-11.
 */
static void test_trace_four_slashes(void)
{
    const qo_v4 p1 = qo_v4_make(3.0, 1.0, 0.0, 2.0);
    const qo_v4 p2 = qo_v4_make(-1.0, 2.0, 5.0, -3.0);
    const qo_v4 p3 = qo_v4_make(4.0, -2.0, 1.0, 0.5);
    const qo_v4 p4 = qo_v4_make(0.5, 3.0, -1.0, 6.0);

    const double want = 4.0 * (qo_v4_dot(p1, p2) * qo_v4_dot(p3, p4) -
                               qo_v4_dot(p1, p3) * qo_v4_dot(p2, p4) +
                               qo_v4_dot(p1, p4) * qo_v4_dot(p2, p3));

    for (int b = 0; b < QO_BASIS_COUNT; ++b) {
        const qo_basis basis = kBases[b];
        qo_m4 prod = qo_m4_mul(qo_slash(basis, p1), qo_slash(basis, p2));
        prod = qo_m4_mul(prod, qo_slash(basis, p3));
        prod = qo_m4_mul(prod, qo_slash(basis, p4));
        const qo_c tr = qo_m4_trace(prod);
        PHY_CHECK(fabs(tr.im) <= TOL);
        PHY_CHECK(fabs(tr.re - want) <= TOL);
    }
}

/* --------------------------------------------------------------- Mandelstam */

/*
 * s + t + u = sum of the four squared masses, verified in both momentum
 * conventions the upstreams use. The formulas differ in sign because the
 * momentum routing differs; the invariants agree. G-12.
 *
 * Kinematics: massless pair annihilating into a mass-3 pair, E = 5, |k| = 4,
 * cos(theta) = 0.6, all components exactly representable.
 */
static void test_mandelstam_sum_rule(void)
{
    /* Peskin routing: p1 + p2 -> p3 + p4, all momenta physical/outgoing. */
    const qo_v4 p1 = qo_v4_make(5.0, 0.0, 0.0, 5.0);
    const qo_v4 p2 = qo_v4_make(5.0, 0.0, 0.0, -5.0);
    const qo_v4 p3 = qo_v4_make(5.0, 3.2, 0.0, 2.4);
    const qo_v4 p4 = qo_v4_make(5.0, -3.2, 0.0, -2.4);

    /* On-shell: two massless, two of mass 3. */
    PHY_CHECK(fabs(qo_v4_dot(p1, p1) - 0.0) <= TOL);
    PHY_CHECK(fabs(qo_v4_dot(p2, p2) - 0.0) <= TOL);
    PHY_CHECK(fabs(qo_v4_dot(p3, p3) - 9.0) <= TOL);
    PHY_CHECK(fabs(qo_v4_dot(p4, p4) - 9.0) <= TOL);

    /* Momentum conservation. */
    const qo_v4 in = qo_v4_add(p1, p2);
    const qo_v4 out = qo_v4_add(p3, p4);
    for (int i = 0; i < 4; ++i) {
        PHY_CHECK(fabs(in.c[i] - out.c[i]) <= TOL);
    }

    const double s_peskin = qo_v4_dot(qo_v4_add(p1, p2), qo_v4_add(p1, p2));
    const double t_peskin = qo_v4_dot(qo_v4_sub(p1, p3), qo_v4_sub(p1, p3));
    const double u_peskin = qo_v4_dot(qo_v4_sub(p1, p4), qo_v4_sub(p1, p4));

    PHY_CHECK(fabs(s_peskin - 100.0) <= TOL);
    PHY_CHECK(fabs(t_peskin - (-17.0)) <= TOL);
    PHY_CHECK(fabs(u_peskin - (-65.0)) <= TOL);
    PHY_CHECK(fabs(s_peskin + t_peskin + u_peskin - 18.0) <= TOL);

    /*
     * FeynCalc SetMandelstam routing: every momentum incoming, so
     * q1 + q2 + q3 + q4 = 0 and t, u use a plus sign.
     */
    const qo_v4 q1 = p1;
    const qo_v4 q2 = p2;
    const qo_v4 q3 = qo_v4_neg(p3);
    const qo_v4 q4 = qo_v4_neg(p4);

    qo_v4 total = qo_v4_add(qo_v4_add(q1, q2), qo_v4_add(q3, q4));
    for (int i = 0; i < 4; ++i) {
        PHY_CHECK(fabs(total.c[i]) <= TOL);
    }

    const double s_fc = qo_v4_dot(qo_v4_add(q1, q2), qo_v4_add(q1, q2));
    const double t_fc = qo_v4_dot(qo_v4_add(q1, q3), qo_v4_add(q1, q3));
    const double u_fc = qo_v4_dot(qo_v4_add(q1, q4), qo_v4_add(q1, q4));

    /* Same invariants, despite the sign difference in the defining formula. */
    PHY_CHECK(fabs(s_fc - s_peskin) <= TOL);
    PHY_CHECK(fabs(t_fc - t_peskin) <= TOL);
    PHY_CHECK(fabs(u_fc - u_peskin) <= TOL);
    PHY_CHECK(fabs(s_fc + t_fc + u_fc - 18.0) <= TOL);
}

/* ------------------------------------------------------------ SU(N) colour */

/* Generators are hermitian and traceless. C-1. */
static void test_su_generators_hermitian_traceless(void)
{
    for (int n = 2; n <= QO_COLOR_MAX_N; ++n) {
        for (int a = 0; a < qo_su_adjoint_dim(n); ++a) {
            const qo_cm t = qo_su_generator(n, a);
            PHY_CHECK(qo_cm_maxdiff(t, qo_cm_adjoint(t)) <= TOL);
            PHY_CHECK(qo_c_abs(qo_cm_trace(t)) <= TOL);
        }
    }
}

/* Tr[T^a T^b] = T_F delta^{ab} with T_F = 1/2. C-2. */
static void test_su_normalisation(void)
{
    for (int n = 2; n <= QO_COLOR_MAX_N; ++n) {
        const int dim = qo_su_adjoint_dim(n);
        for (int a = 0; a < dim; ++a) {
            for (int b = 0; b < dim; ++b) {
                const qo_c tr = qo_cm_trace(
                    qo_cm_mul(qo_su_generator(n, a), qo_su_generator(n, b)));
                const double want = (a == b) ? 0.5 : 0.0;
                PHY_CHECK(qo_c_dist(tr, qo_c_real(want)) <= TOL);
            }
        }
    }
}

/* [T^a, T^b] = i f^{abc} T^c, and f is totally antisymmetric and real. C-3. */
static void test_su_structure_constants(void)
{
    for (int n = 2; n <= 4; ++n) {
        const int dim = qo_su_adjoint_dim(n);
        for (int a = 0; a < dim; ++a) {
            for (int b = 0; b < dim; ++b) {
                qo_cm rhs = qo_cm_zero(n);
                for (int c = 0; c < dim; ++c) {
                    const double f = qo_su_f(n, a, b, c);
                    if (f == 0.0) {
                        continue;
                    }
                    rhs = qo_cm_add(
                        rhs, qo_cm_scale(qo_su_generator(n, c),
                                         qo_c_make(0.0, f)));
                }
                const qo_cm lhs = qo_cm_commutator(qo_su_generator(n, a),
                                                   qo_su_generator(n, b));
                PHY_CHECK(qo_cm_maxdiff(lhs, rhs) <= TOL);

                /* Antisymmetry in the first two slots and in the last two. */
                for (int c = 0; c < dim; ++c) {
                    PHY_CHECK(fabs(qo_su_f(n, a, b, c) +
                                   qo_su_f(n, b, a, c)) <= TOL);
                    PHY_CHECK(fabs(qo_su_f(n, a, b, c) -
                                   qo_su_f(n, b, c, a)) <= TOL);
                }
            }
        }
    }
}

/* T^a T^a = C_F I with C_F = (N^2-1)/(2N). C-4. */
static void test_su_casimir_fundamental(void)
{
    for (int n = 2; n <= QO_COLOR_MAX_N; ++n) {
        qo_cm acc = qo_cm_zero(n);
        for (int a = 0; a < qo_su_adjoint_dim(n); ++a) {
            const qo_cm t = qo_su_generator(n, a);
            acc = qo_cm_add(acc, qo_cm_mul(t, t));
        }
        const qo_cm want = qo_cm_scale(
            qo_cm_identity(n), qo_c_real(qo_su_casimir_fundamental(n)));
        PHY_CHECK(qo_cm_maxdiff(acc, want) <= TOL);
    }

    PHY_CHECK(fabs(qo_su_casimir_fundamental(3) - 4.0 / 3.0) <= TOL);
}

/* f^{acd} f^{bcd} = C_A delta^{ab} with C_A = N. C-5. */
static void test_su_casimir_adjoint(void)
{
    for (int n = 2; n <= 4; ++n) {
        const int dim = qo_su_adjoint_dim(n);
        for (int a = 0; a < dim; ++a) {
            for (int b = 0; b < dim; ++b) {
                double acc = 0.0;
                for (int c = 0; c < dim; ++c) {
                    for (int d = 0; d < dim; ++d) {
                        acc += qo_su_f(n, a, c, d) * qo_su_f(n, b, c, d);
                    }
                }
                const double want =
                    (a == b) ? qo_su_casimir_adjoint(n) : 0.0;
                PHY_CHECK(fabs(acc - want) <= 1e-8);
            }
        }
    }
}

/* Jacobi identity for the structure constants. C-6. */
static void test_su_jacobi(void)
{
    const int n = 3;
    const int dim = qo_su_adjoint_dim(n);
    for (int a = 0; a < dim; ++a) {
        for (int b = 0; b < dim; ++b) {
            for (int c = 0; c < dim; ++c) {
                for (int d = 0; d < dim; ++d) {
                    double acc = 0.0;
                    for (int e = 0; e < dim; ++e) {
                        acc += qo_su_f(n, a, b, e) * qo_su_f(n, e, c, d) +
                               qo_su_f(n, b, c, e) * qo_su_f(n, e, a, d) +
                               qo_su_f(n, c, a, e) * qo_su_f(n, e, b, d);
                    }
                    PHY_CHECK(fabs(acc) <= 1e-8);
                }
            }
        }
    }
}

/*
 * The generic construction reproduces the textbook bases exactly, not merely
 * up to a basis rotation: N = 2 gives the Pauli matrices over two, and N = 3
 * gives the published SU(3) structure constants. C-7.
 */
static void test_su_matches_textbook_bases(void)
{
    /* SU(2): T^a = sigma^a / 2, so f^{abc} = epsilon^{abc}. */
    PHY_CHECK(fabs(qo_su_f(2, 0, 1, 2) - 1.0) <= TOL);
    PHY_CHECK(fabs(qo_su_f(2, 1, 2, 0) - 1.0) <= TOL);
    PHY_CHECK(fabs(qo_su_f(2, 0, 2, 1) + 1.0) <= TOL);

    /* SU(3) in Gell-Mann labelling (1-based in the literature, 0-based here):
     * f^{123} = 1, f^{147} = 1/2, f^{458} = sqrt(3)/2. */
    PHY_CHECK(fabs(qo_su_f(3, 0, 1, 2) - 1.0) <= TOL);
    PHY_CHECK(fabs(qo_su_f(3, 0, 3, 6) - 0.5) <= TOL);
    PHY_CHECK(fabs(qo_su_f(3, 3, 4, 7) - sqrt(3.0) / 2.0) <= TOL);
}

int main(void)
{
    PHY_TEST_CASE(test_clifford_algebra);
    PHY_TEST_CASE(test_contraction_identities);
    PHY_TEST_CASE(test_contraction_preserves_leading_order);
    PHY_TEST_CASE(test_odd_traces_vanish);
    PHY_TEST_CASE(test_trace_recursion_matches_matrices);
    PHY_TEST_CASE(test_trace_two_gammas);
    PHY_TEST_CASE(test_trace_four_gammas);
    PHY_TEST_CASE(test_slash_anticommutator);
    PHY_TEST_CASE(test_trace_four_slashes);
    PHY_TEST_CASE(test_mandelstam_sum_rule);
    PHY_TEST_CASE(test_su_generators_hermitian_traceless);
    PHY_TEST_CASE(test_su_normalisation);
    PHY_TEST_CASE(test_su_structure_constants);
    PHY_TEST_CASE(test_su_casimir_fundamental);
    PHY_TEST_CASE(test_su_casimir_adjoint);
    PHY_TEST_CASE(test_su_jacobi);
    PHY_TEST_CASE(test_su_matches_textbook_bases);
    return PHY_TEST_REPORT("test_qo_golden");
}
