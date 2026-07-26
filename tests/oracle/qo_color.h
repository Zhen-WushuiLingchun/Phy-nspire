/*
 * SU(N) colour algebra oracle: explicit fundamental generators.
 *
 * Generators are built in generalised Gell-Mann order, so that N = 2 gives
 * exactly the Pauli matrices over two and N = 3 gives exactly the Gell-Mann
 * matrices over two, in the textbook labelling. That makes the N = 3
 * structure constants directly comparable with published values
 * (f^{123} = 1, f^{147} = 1/2, f^{458} = sqrt(3)/2) instead of only being
 * checkable up to a basis rotation.
 *
 * Scope is the MVP colour set: normalisation, commutators, structure
 * constants and the two quadratic Casimirs. The Fierz/completeness identity
 * is deferred; see docs/references/QFT_GAUGE.md.
 */
#ifndef QO_COLOR_H
#define QO_COLOR_H

#include "qo_complex.h"

#define QO_COLOR_MAX_N 6
#define QO_COLOR_MAX_ADJ (QO_COLOR_MAX_N * QO_COLOR_MAX_N - 1)

/* N x N complex matrix; only the leading n rows and columns are meaningful. */
typedef struct {
    int n;
    qo_c m[QO_COLOR_MAX_N][QO_COLOR_MAX_N];
} qo_cm;

/* dim of the adjoint representation, N^2 - 1. */
int qo_su_adjoint_dim(int n);

qo_cm qo_cm_zero(int n);
qo_cm qo_cm_identity(int n);
qo_cm qo_cm_add(qo_cm a, qo_cm b);
qo_cm qo_cm_sub(qo_cm a, qo_cm b);
qo_cm qo_cm_mul(qo_cm a, qo_cm b);
qo_cm qo_cm_scale(qo_cm a, qo_c s);
qo_cm qo_cm_commutator(qo_cm a, qo_cm b);
qo_cm qo_cm_adjoint(qo_cm a);
qo_c qo_cm_trace(qo_cm a);
double qo_cm_maxdiff(qo_cm a, qo_cm b);

/*
 * Fundamental generator T^a for a in [0, N^2-2], normalised to
 * Tr[T^a T^b] = delta^{ab} / 2, i.e. T_F = 1/2.
 */
qo_cm qo_su_generator(int n, int a);

/* f^{abc} = -2i Tr([T^a, T^b] T^c), which follows from [T^a,T^b] = i f^{abc} T^c
 * together with the T_F = 1/2 normalisation. Returns a real number. */
double qo_su_f(int n, int a, int b, int c);

/* Quadratic Casimirs for the pinned normalisation. */
double qo_su_casimir_fundamental(int n); /* C_F = (N^2-1)/(2N) */
double qo_su_casimir_adjoint(int n);     /* C_A = N */

#endif /* QO_COLOR_H */
