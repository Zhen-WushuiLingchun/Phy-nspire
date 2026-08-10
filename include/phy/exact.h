/*
 * Phy-nspire — native bounded-memory arbitrary-precision exact arithmetic.
 *
 * Values have no fixed mathematical word size. Their storage and work are
 * bounded by a context so a calculator cell fails with a typed status rather
 * than exhausting the device or silently truncating. Small values will remain
 * the IR/CAS fast path; this layer is the promotion domain.
 */
#ifndef PHY_EXACT_H
#define PHY_EXACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_exact_context phy_exact_context;
typedef struct phy_bigint phy_bigint;

typedef struct {
    uint32_t max_limbs; /* base-2^32 limbs per integer; default 256 */
    uint32_t max_steps; /* limb operations per outer call; default 4,000,000 */
    size_t max_bytes;   /* context-owned limb/scratch bytes; default 256 KiB */
} phy_exact_limits;

void phy_exact_limits_defaults(phy_exact_limits *out_limits);
phy_exact_context *phy_exact_context_create(const phy_exact_limits *limits);
void phy_exact_context_destroy(phy_exact_context *context);

void phy_exact_set_cancel(phy_exact_context *context,
                          bool (*cancelled)(void *user), void *user);
uint32_t phy_exact_steps(const phy_exact_context *context);
uint64_t phy_exact_total_steps(const phy_exact_context *context);
size_t phy_exact_bytes_used(const phy_exact_context *context);
phy_status phy_exact_validate(const phy_exact_context *context);

/*
 * A stack-embeddable handle. The fields are public only so C callers can
 * allocate the handle without a second heap object; they are private state and
 * must never be changed directly.
 *
 * Initialize once, destroy once. Context destruction also frees and
 * invalidates every still-live handle registered with that context.
 */
struct phy_bigint {
    phy_exact_context *context;
    uint32_t *limbs;
    size_t count;
    size_t capacity;
    phy_bigint *previous;
    phy_bigint *next;
    int sign;
    uint32_t private_magic;
};

phy_status phy_bigint_init(phy_exact_context *context, phy_bigint *value);
void phy_bigint_destroy(phy_bigint *value);
bool phy_bigint_is_initialized(const phy_bigint *value);
phy_status phy_bigint_validate(const phy_bigint *value);

phy_status phy_bigint_set_i64(phy_bigint *value, int64_t integer);
phy_status phy_bigint_read(phy_bigint *value, const char *decimal);
phy_status phy_bigint_write(const phy_bigint *value, char *buffer,
                            size_t capacity, size_t *out_required);
phy_status phy_bigint_copy(const phy_bigint *source, phy_bigint *destination);

int phy_bigint_sign(const phy_bigint *value);
size_t phy_bigint_bit_length(const phy_bigint *value);
bool phy_bigint_try_i64(const phy_bigint *value, int64_t *out_value);
int phy_bigint_compare(const phy_bigint *left, const phy_bigint *right);
int phy_bigint_compare_abs(const phy_bigint *left, const phy_bigint *right);

phy_status phy_bigint_negate(const phy_bigint *value, phy_bigint *out_value);
phy_status phy_bigint_add(const phy_bigint *left, const phy_bigint *right,
                          phy_bigint *out_sum);
phy_status phy_bigint_subtract(const phy_bigint *left,
                               const phy_bigint *right,
                               phy_bigint *out_difference);
phy_status phy_bigint_multiply(const phy_bigint *left,
                               const phy_bigint *right,
                               phy_bigint *out_product);
phy_status phy_bigint_pow_u32(const phy_bigint *base, uint32_t exponent,
                              phy_bigint *out_power);

/*
 * Truncating signed division:
 *   dividend = quotient * divisor + remainder,
 *   |remainder| < |divisor|,
 * and a nonzero remainder has the dividend's sign.
 *
 * The two outputs must be distinct, but either may alias an input.
 */
phy_status phy_bigint_divmod(const phy_bigint *dividend,
                             const phy_bigint *divisor,
                             phy_bigint *out_quotient,
                             phy_bigint *out_remainder);
phy_status phy_bigint_divide_exact(const phy_bigint *dividend,
                                   const phy_bigint *divisor,
                                   phy_bigint *out_quotient);
phy_status phy_bigint_gcd(const phy_bigint *left, const phy_bigint *right,
                          phy_bigint *out_gcd);

/*
 * Euclidean remainder in one machine word. The result is always in
 * [0, modulus), including for a negative input. This is the bridge used by
 * modular polynomial algorithms; it never truncates the bigint first.
 */
phy_status phy_bigint_mod_u32(const phy_bigint *value, uint32_t modulus,
                              uint32_t *out_remainder);

typedef struct {
    phy_bigint numerator;
    phy_bigint denominator;
    uint32_t private_magic;
} phy_bigrat;

phy_status phy_bigrat_init(phy_exact_context *context, phy_bigrat *value);
void phy_bigrat_destroy(phy_bigrat *value);
phy_status phy_bigrat_validate(const phy_bigrat *value);

phy_status phy_bigrat_set_i64(phy_bigrat *value, int64_t numerator,
                              int64_t denominator);
phy_status phy_bigrat_read(phy_bigrat *value, const char *numerator,
                           const char *denominator);
phy_status phy_bigrat_write(const phy_bigrat *value, char *buffer,
                            size_t capacity, size_t *out_required);
phy_status phy_bigrat_copy(const phy_bigrat *source,
                           phy_bigrat *destination);

phy_status phy_bigrat_add(const phy_bigrat *left, const phy_bigrat *right,
                          phy_bigrat *out_sum);
phy_status phy_bigrat_negate(const phy_bigrat *value,
                             phy_bigrat *out_negated);
phy_status phy_bigrat_subtract(const phy_bigrat *left,
                               const phy_bigrat *right,
                               phy_bigrat *out_difference);
phy_status phy_bigrat_multiply(const phy_bigrat *left,
                               const phy_bigrat *right,
                               phy_bigrat *out_product);
phy_status phy_bigrat_reciprocal(const phy_bigrat *value,
                                 phy_bigrat *out_reciprocal);
phy_status phy_bigrat_divide(const phy_bigrat *dividend,
                             const phy_bigrat *divisor,
                             phy_bigrat *out_quotient);
phy_status phy_bigrat_pow_i32(const phy_bigrat *base, int32_t exponent,
                              phy_bigrat *out_power);
phy_status phy_bigrat_compare(const phy_bigrat *left,
                              const phy_bigrat *right, int *out_comparison);
int phy_bigrat_sign(const phy_bigrat *value);

/* Set a rational to an integer already held in the same exact context. */
phy_status phy_bigrat_set_bigint(const phy_bigint *integer,
                                 phy_bigrat *out_value);

/* No-fail payload exchange; both values must be valid and share a context. */
phy_status phy_bigrat_swap(phy_bigrat *left, phy_bigrat *right);

const phy_bigint *phy_bigrat_numerator(const phy_bigrat *value);
const phy_bigint *phy_bigrat_denominator(const phy_bigrat *value);
bool phy_bigrat_try_i64(const phy_bigrat *value, int64_t *out_numerator,
                        int64_t *out_denominator);

/*
 * Exact Gaussian rationals Q(i).
 *
 * This stack-embeddable pair owns two canonical arbitrary-precision rationals.
 * Arithmetic is transactional and alias-safe: on failure an already-valid
 * output retains its previous value. Both parts share one exact context, so
 * the existing limb, byte, step, and cancellation ceilings apply.
 */
typedef struct {
    phy_bigrat real;
    phy_bigrat imaginary;
    uint32_t private_magic;
} phy_gaussian;

phy_status phy_gaussian_init(phy_exact_context *context,
                             phy_gaussian *value);
void phy_gaussian_destroy(phy_gaussian *value);
phy_status phy_gaussian_validate(const phy_gaussian *value);

phy_status phy_gaussian_set_i64(
    phy_gaussian *value, int64_t real_numerator,
    int64_t real_denominator, int64_t imaginary_numerator,
    int64_t imaginary_denominator);
phy_status phy_gaussian_read(
    phy_gaussian *value, const char *real_numerator,
    const char *real_denominator, const char *imaginary_numerator,
    const char *imaginary_denominator);
phy_status phy_gaussian_copy(const phy_gaussian *source,
                             phy_gaussian *destination);

phy_status phy_gaussian_add(const phy_gaussian *left,
                            const phy_gaussian *right,
                            phy_gaussian *out_sum);
phy_status phy_gaussian_negate(const phy_gaussian *value,
                               phy_gaussian *out_negated);
phy_status phy_gaussian_subtract(const phy_gaussian *left,
                                 const phy_gaussian *right,
                                 phy_gaussian *out_difference);
phy_status phy_gaussian_multiply(const phy_gaussian *left,
                                 const phy_gaussian *right,
                                 phy_gaussian *out_product);
phy_status phy_gaussian_reciprocal(const phy_gaussian *value,
                                   phy_gaussian *out_reciprocal);
phy_status phy_gaussian_divide(const phy_gaussian *dividend,
                               const phy_gaussian *divisor,
                               phy_gaussian *out_quotient);
phy_status phy_gaussian_conjugate(const phy_gaussian *value,
                                  phy_gaussian *out_conjugate);
phy_status phy_gaussian_norm(const phy_gaussian *value,
                             phy_bigrat *out_norm);
phy_status phy_gaussian_pow_i32(const phy_gaussian *base, int32_t exponent,
                                phy_gaussian *out_power);

const phy_bigrat *phy_gaussian_real(const phy_gaussian *value);
const phy_bigrat *phy_gaussian_imaginary(const phy_gaussian *value);

#ifdef __cplusplus
}
#endif

#endif /* PHY_EXACT_H */
