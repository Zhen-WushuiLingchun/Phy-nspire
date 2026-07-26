/*
 * Dirac algebra oracle: explicit 4x4 matrices, no symbolic layer.
 *
 * Purpose. The identities recorded in docs/references/QFT_GAUGE.md are the
 * contract that the Phase 5 symbolic engine must satisfy. Transcribing them
 * from a textbook is how sign errors enter a project and never leave. This
 * module instead computes both sides numerically from an explicit matrix
 * representation, so every identity in that document is machine-checked
 * before it is written down.
 *
 * Scope is deliberately the MVP set only: Clifford algebra, index contraction,
 * and traces of gamma strings with no gamma5. gamma5, Fierz rearrangement,
 * spinors, polarization sums and loop kinematics are out of scope and are
 * listed as deferred in the reference document.
 *
 * Two independent representations (Dirac and Weyl) are provided so that every
 * trace result can be shown to be representation-independent. A result that
 * differs between the two is an artefact of the chosen matrices, not an
 * identity.
 */
#ifndef QO_DIRAC_H
#define QO_DIRAC_H

#include "qo_complex.h"

/* Longest gamma string the oracle will build. Traces of 2n matrices expand
 * into (2n-1)!! metric terms, so 12 already means 10395 terms; see
 * docs/references/QFT_GAUGE.md, "Resource behaviour". */
#define QO_MAX_GAMMAS 12

/* Contravariant Minkowski 4-vector, components p^mu with mu = 0..3. */
typedef struct {
    double c[4];
} qo_v4;

/* 4x4 complex matrix acting on Dirac spinor space. */
typedef struct {
    qo_c m[4][4];
} qo_m4;

typedef enum {
    QO_BASIS_DIRAC = 0,
    QO_BASIS_WEYL = 1,
    QO_BASIS_COUNT = 2
} qo_basis;

/*
 * Metric. Pinned to the particle-physics signature (+,-,-,-), which is
 * FeynCalc's and Peskin's. g^{mu nu} and g_{mu nu} are numerically equal in
 * this signature, so one accessor serves both.
 */
double qo_metric(int mu, int nu);

qo_v4 qo_v4_make(double e, double x, double y, double z);

/* p.q = g_{mu nu} p^mu q^nu. */
double qo_v4_dot(qo_v4 p, qo_v4 q);

qo_v4 qo_v4_add(qo_v4 p, qo_v4 q);
qo_v4 qo_v4_sub(qo_v4 p, qo_v4 q);
qo_v4 qo_v4_neg(qo_v4 p);

qo_m4 qo_m4_zero(void);
qo_m4 qo_m4_identity(void);
qo_m4 qo_m4_add(qo_m4 a, qo_m4 b);
qo_m4 qo_m4_sub(qo_m4 a, qo_m4 b);
qo_m4 qo_m4_mul(qo_m4 a, qo_m4 b);
qo_m4 qo_m4_scale(qo_m4 a, qo_c s);
qo_c qo_m4_trace(qo_m4 a);

/* Largest elementwise absolute difference; the comparison used by every
 * matrix-valued check. */
double qo_m4_maxdiff(qo_m4 a, qo_m4 b);

/* gamma^mu in the requested representation. */
qo_m4 qo_gamma(qo_basis basis, int mu);

/* gamma_mu = g_{mu nu} gamma^nu. */
qo_m4 qo_gamma_lower(qo_basis basis, int mu);

/* Feynman slash, p_mu gamma^mu. */
qo_m4 qo_slash(qo_basis basis, qo_v4 p);

/* gamma^{mu[0]} gamma^{mu[1]} ... gamma^{mu[n-1]}; n == 0 gives the identity. */
qo_m4 qo_gamma_product(qo_basis basis, const int *mu, int n);

/*
 * Trace of gamma^{mu[0]}...gamma^{mu[n-1]} evaluated by the metric recursion
 *
 *   Tr[g^{m1} ... g^{m2n}] = sum_{j=2}^{2n} (-1)^j g^{m1 mj}
 *                            Tr[g^{m2} ... g^{m(j-1)} g^{m(j+1)} ... g^{m2n}]
 *
 * with Tr[1] = 4 and odd-length traces zero. This is the algorithm the Phase 5
 * engine is specified to implement, so checking it against explicit matrices
 * validates the algorithm and not merely a table of results.
 */
double qo_trace_recursive(const int *mu, int n);

#endif /* QO_DIRAC_H */
