/*
 * Certified real algebraic numbers over the native exact-number kernel.
 *
 * A value is a real root selected by:
 *   - a primitive, square-free integer defining polynomial; and
 *   - an exact rational interval containing exactly one real root.
 *
 * "Defining polynomial" is deliberate. Until irreducibility is certified by
 * the general factorization layer, this API does not call it a minimal
 * polynomial and does not claim canonical equality across unrelated
 * polynomials.
 */
#ifndef PHY_ALGEBRAIC_H
#define PHY_ALGEBRAIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/exact.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_algebraic_context phy_algebraic_context;
typedef struct phy_real_algebraic phy_real_algebraic;

typedef struct {
    const char *numerator;
    const char *denominator;
} phy_exact_rational_text;

typedef struct {
    phy_exact_limits exact;
    uint32_t max_degree;       /* defining polynomial degree; default 32 */
    uint32_t max_steps;        /* polynomial operations per call */
    uint32_t max_refinements;  /* compare/refine bisections; default 128 */
    size_t max_metadata_bytes; /* handles/arrays, excluding exact limbs */
} phy_algebraic_limits;

void phy_algebraic_limits_defaults(phy_algebraic_limits *out_limits);
phy_algebraic_context *phy_algebraic_context_create(
    const phy_algebraic_limits *limits);
void phy_algebraic_context_destroy(phy_algebraic_context *context);
void phy_algebraic_set_cancel(phy_algebraic_context *context,
                              bool (*cancelled)(void *user), void *user);
uint32_t phy_algebraic_steps(const phy_algebraic_context *context);
uint64_t phy_algebraic_total_steps(const phy_algebraic_context *context);
size_t phy_algebraic_metadata_bytes(
    const phy_algebraic_context *context);
phy_status phy_algebraic_validate(
    const phy_algebraic_context *context);

/*
 * Count real roots in the open interval (lower, upper) by a Sturm chain.
 * Coefficients are signed decimal integers in increasing degree order:
 * {a0, a1, ..., an}. They are normalized to primitive positive-leading form.
 *
 * The polynomial must have degree >= 1 and be square-free. Interval endpoints
 * may not themselves be roots. On success, out_count is exact.
 */
phy_status phy_algebraic_count_real_roots(
    phy_algebraic_context *context,
    const char *const *coefficients, size_t coefficient_count,
    phy_exact_rational_text lower, phy_exact_rational_text upper,
    uint32_t *out_count);

/*
 * Isolate every real root of one primitive square-free integer polynomial.
 *
 * On success, out_values[0..*out_count) are newly owned algebraic values in
 * strictly increasing real order. Each has a disjoint exact rational interval
 * containing exactly one root. The caller destroys the values individually,
 * or destroys the context to reclaim all of them.
 *
 * The routine constructs one Sturm chain and bisects inside a certified
 * Cauchy bound. It never samples floating point. `value_capacity` must be at
 * least the number of real roots; otherwise PHY_ERR_TERM_LIMIT is returned.
 * Failure is transactional: no value is published and every output slot is
 * left NULL.
 */
phy_status phy_algebraic_isolate_real_roots(
    phy_algebraic_context *context,
    const char *const *coefficients, size_t coefficient_count,
    phy_real_algebraic **out_values, size_t value_capacity,
    size_t *out_count);

/*
 * Construct a certified real algebraic value. The interval must contain
 * exactly one root. On failure *out_value remains NULL and no value is linked
 * into the context.
 */
phy_status phy_real_algebraic_create(
    phy_algebraic_context *context,
    const char *const *coefficients, size_t coefficient_count,
    phy_exact_rational_text lower, phy_exact_rational_text upper,
    phy_real_algebraic **out_value);
void phy_real_algebraic_destroy(phy_real_algebraic *value);
phy_status phy_real_algebraic_validate(
    const phy_real_algebraic *value);

size_t phy_real_algebraic_degree(const phy_real_algebraic *value);
phy_status phy_real_algebraic_write_coefficient(
    const phy_real_algebraic *value, size_t degree, char *buffer,
    size_t capacity, size_t *out_required);
phy_status phy_real_algebraic_write_lower(
    const phy_real_algebraic *value, char *buffer, size_t capacity,
    size_t *out_required);
phy_status phy_real_algebraic_write_upper(
    const phy_real_algebraic *value, char *buffer, size_t capacity,
    size_t *out_required);
bool phy_real_algebraic_is_rational(
    const phy_real_algebraic *value);

/*
 * Exact rational transforms.
 *
 * Each operation constructs a new independently owned certificate in the
 * source value's context. The defining polynomial is transformed over Z[x],
 * normalized to primitive positive-leading form, and the transformed interval
 * is re-certified before publication. On failure *out_value remains NULL and
 * the source value is unchanged.
 */
phy_status phy_real_algebraic_translate_rational(
    const phy_real_algebraic *value, phy_exact_rational_text translation,
    phy_real_algebraic **out_value);
phy_status phy_real_algebraic_scale_rational(
    const phy_real_algebraic *value, phy_exact_rational_text factor,
    phy_real_algebraic **out_value);
phy_status phy_real_algebraic_reciprocal(
    const phy_real_algebraic *value, phy_real_algebraic **out_value);

/*
 * Resultant-closed exact real algebraic arithmetic.
 *
 * Both operands must belong to the same context. Non-rational pairs are
 * eliminated exactly, the resultant is reduced to its square-free primitive
 * part, and interval refinement selects one certified real root. Degree,
 * coefficient, step, refinement, cancellation, and memory ceilings remain
 * those of the algebraic/exact contexts. Failure is transactional.
 */
phy_status phy_real_algebraic_add(
    const phy_real_algebraic *left, const phy_real_algebraic *right,
    phy_real_algebraic **out_value);
phy_status phy_real_algebraic_subtract(
    const phy_real_algebraic *left, const phy_real_algebraic *right,
    phy_real_algebraic **out_value);
phy_status phy_real_algebraic_multiply(
    const phy_real_algebraic *left, const phy_real_algebraic *right,
    phy_real_algebraic **out_value);
phy_status phy_real_algebraic_divide(
    const phy_real_algebraic *left, const phy_real_algebraic *right,
    phy_real_algebraic **out_value);
phy_status phy_real_algebraic_pow_i32(
    const phy_real_algebraic *base, int32_t exponent,
    phy_real_algebraic **out_value);

/*
 * Bisect the certified interval. If a midpoint is the exact root, the interval
 * becomes that rational point. Failure leaves the old interval unchanged.
 */
phy_status phy_real_algebraic_refine(
    phy_real_algebraic *value, uint32_t rounds);

/*
 * Safe comparison. Disjoint certified intervals decide immediately; otherwise
 * the values are refined up to max_refinements. Equal roots of the same
 * defining polynomial are recognized by a Sturm certificate. If distinct
 * defining polynomials remain inseparable without a resultant/minimal-
 * polynomial proof, the function returns PHY_ERR_UNSUPPORTED rather than
 * guessing. Comparison may refine both operands.
 */
phy_status phy_real_algebraic_compare(
    phy_real_algebraic *left, phy_real_algebraic *right,
    int *out_comparison);

#ifdef __cplusplus
}
#endif

#endif /* PHY_ALGEBRAIC_H */
