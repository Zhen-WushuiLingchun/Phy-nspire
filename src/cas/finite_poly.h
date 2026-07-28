/*
 * Phy-nspire — bounded dense polynomials over a small prime field.
 *
 * This is an internal CAS kernel, shared by modular integer-polynomial
 * factorization and later modular multivariate GCD. It deliberately owns no
 * heap memory: calculator resource use is a fixed stack footprint plus an
 * explicit operation counter.
 */
#ifndef PHY_CAS_FINITE_POLY_H
#define PHY_CAS_FINITE_POLY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/phy.h"

#define PHY_FPOLY_HARD_MAX_DEGREE 64u
#define PHY_FPOLY_MAX_PRIME 65521u

typedef struct {
    uint32_t max_degree;
    uint32_t max_steps;
} phy_fpoly_limits;

typedef struct {
    phy_fpoly_limits limits;
    uint32_t prime;
    uint32_t steps;
    bool (*cancelled)(void *user);
    void *cancel_user;
    uint32_t private_magic;
} phy_fpoly_context;

typedef struct {
    phy_fpoly_context *context;
    size_t length;
    uint32_t coefficients[PHY_FPOLY_HARD_MAX_DEGREE + 1u];
    uint32_t private_magic;
} phy_fpoly;

typedef struct {
    phy_fpoly_context *context;
    size_t count;
    phy_fpoly factors[PHY_FPOLY_HARD_MAX_DEGREE];
    uint32_t private_magic;
} phy_fpoly_factorization;

void phy_fpoly_limits_defaults(phy_fpoly_limits *out_limits);
phy_status phy_fpoly_context_init(phy_fpoly_context *context, uint32_t prime,
                                  const phy_fpoly_limits *limits);
void phy_fpoly_context_set_cancel(phy_fpoly_context *context,
                                  bool (*cancelled)(void *user), void *user);
uint32_t phy_fpoly_context_steps(const phy_fpoly_context *context);

phy_status phy_fpoly_init(phy_fpoly_context *context, phy_fpoly *polynomial);
phy_status phy_fpoly_validate(const phy_fpoly *polynomial);
phy_status phy_fpoly_set(phy_fpoly *polynomial,
                         const uint32_t *coefficients, size_t length);
phy_status phy_fpoly_copy(const phy_fpoly *source, phy_fpoly *destination);

int phy_fpoly_degree(const phy_fpoly *polynomial);
uint32_t phy_fpoly_coefficient(const phy_fpoly *polynomial, size_t degree);
bool phy_fpoly_is_zero(const phy_fpoly *polynomial);
bool phy_fpoly_is_one(const phy_fpoly *polynomial);
bool phy_fpoly_equal(const phy_fpoly *left, const phy_fpoly *right);

phy_status phy_fpoly_add(const phy_fpoly *left, const phy_fpoly *right,
                         phy_fpoly *out_sum);
phy_status phy_fpoly_subtract(const phy_fpoly *left,
                              const phy_fpoly *right,
                              phy_fpoly *out_difference);
phy_status phy_fpoly_multiply(const phy_fpoly *left,
                              const phy_fpoly *right,
                              phy_fpoly *out_product);
phy_status phy_fpoly_mulmod(const phy_fpoly *left,
                            const phy_fpoly *right,
                            const phy_fpoly *modulus,
                            phy_fpoly *out_remainder);
phy_status phy_fpoly_derivative(const phy_fpoly *polynomial,
                                phy_fpoly *out_derivative);

/*
 * Euclidean division in F_p[x]. Outputs may alias inputs but must be distinct
 * from each other. The divisor must be nonzero.
 */
phy_status phy_fpoly_divrem(const phy_fpoly *dividend,
                            const phy_fpoly *divisor,
                            phy_fpoly *out_quotient,
                            phy_fpoly *out_remainder);

/* Monic GCD, including gcd(0,0)=0. */
phy_status phy_fpoly_gcd(const phy_fpoly *left, const phy_fpoly *right,
                         phy_fpoly *out_gcd);

/*
 * Extended Euclidean algorithm with a monic certificate:
 *
 *   out_gcd = out_left_cofactor * left + out_right_cofactor * right.
 *
 * gcd(0,0)=0 with both cofactors zero. Outputs may alias inputs but must be
 * pairwise distinct.
 */
phy_status phy_fpoly_xgcd(const phy_fpoly *left, const phy_fpoly *right,
                          phy_fpoly *out_gcd,
                          phy_fpoly *out_left_cofactor,
                          phy_fpoly *out_right_cofactor);

/* Binary exponentiation followed by reduction after every multiply. */
phy_status phy_fpoly_powmod(const phy_fpoly *base, uint64_t exponent,
                            const phy_fpoly *modulus,
                            phy_fpoly *out_remainder);

/* Exact square-free decision through gcd(f,f'). */
phy_status phy_fpoly_is_square_free(const phy_fpoly *polynomial,
                                    bool *out_square_free);

phy_status phy_fpoly_factorization_init(
    phy_fpoly_context *context, phy_fpoly_factorization *factorization);
phy_status phy_fpoly_factorization_validate(
    const phy_fpoly_factorization *factorization);

/*
 * Deterministic Berlekamp factorization of a monic, nonconstant, square-free
 * polynomial. Factors are monic, irreducible, and sorted deterministically.
 * Repeated finite-field factors are handled by the caller's square-free
 * decomposition because integer Zassenhaus selects a square-free image.
 */
phy_status phy_fpoly_factor_square_free(
    const phy_fpoly *polynomial, phy_fpoly_factorization *out_factors);

#endif /* PHY_CAS_FINITE_POLY_H */
