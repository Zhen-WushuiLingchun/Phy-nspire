/*
 * Certified real ball arithmetic over the native arbitrary-precision
 * rational kernel.  A ball denotes the closed interval midpoint +/- radius;
 * radius is always nonnegative.  No binary floating point is used.
 */
#ifndef PHY_BALL_H
#define PHY_BALL_H

#include <stdbool.h>
#include <stdint.h>

#include "phy/exact.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    phy_bigrat midpoint;
    phy_bigrat radius;
    uint32_t private_magic;
} phy_real_ball;

phy_status phy_real_ball_init(phy_exact_context *context, phy_real_ball *ball);
void phy_real_ball_destroy(phy_real_ball *ball);
phy_status phy_real_ball_validate(const phy_real_ball *ball);

phy_status phy_real_ball_set_i64(phy_real_ball *ball, int64_t numerator,
                                 int64_t denominator);
phy_status phy_real_ball_set_exact(phy_real_ball *ball,
                                   const phy_bigrat *value);
phy_status phy_real_ball_set_interval(phy_real_ball *ball,
                                      const phy_bigrat *lower,
                                      const phy_bigrat *upper);
phy_status phy_real_ball_copy(const phy_real_ball *source,
                              phy_real_ball *destination);

phy_status phy_real_ball_lower(const phy_real_ball *ball,
                               phy_bigrat *out_lower);
phy_status phy_real_ball_upper(const phy_real_ball *ball,
                               phy_bigrat *out_upper);
phy_status phy_real_ball_contains_zero_checked(const phy_real_ball *ball,
                                               bool *out_contains_zero);
/* Conservative convenience wrapper: invalid/OOM state is treated as zero. */
bool phy_real_ball_contains_zero(const phy_real_ball *ball);

phy_status phy_real_ball_add(const phy_real_ball *left,
                             const phy_real_ball *right,
                             phy_real_ball *out_sum);
phy_status phy_real_ball_subtract(const phy_real_ball *left,
                                  const phy_real_ball *right,
                                  phy_real_ball *out_difference);
phy_status phy_real_ball_multiply(const phy_real_ball *left,
                                  const phy_real_ball *right,
                                  phy_real_ball *out_product);
phy_status phy_real_ball_divide(const phy_real_ball *dividend,
                                const phy_real_ball *divisor,
                                phy_real_ball *out_quotient);
phy_status phy_real_ball_pow_i32(const phy_real_ball *base, int32_t exponent,
                                 phy_real_ball *out_power);
/* Monotone rational bisection; rounds is an explicit work/width bound. */
phy_status phy_real_ball_sqrt(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_root);

/*
 * Certified elementary real functions.  `rounds` controls the bounded
 * rational range-reduction/Taylor work; callers normally use roughly four
 * rounds per requested decimal digit.  Every routine is transactional and
 * returns PHY_ERR_DOMAIN when the whole input ball is not inside the real
 * function's domain.  Excessive range reduction returns PHY_ERR_TERM_LIMIT.
 */
phy_status phy_real_ball_exp(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_log(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_sin(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_cos(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_tan(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_sinh(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value);
phy_status phy_real_ball_cosh(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value);
phy_status phy_real_ball_tanh(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value);
phy_status phy_real_ball_atan(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value);
phy_status phy_real_ball_asin(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value);
phy_status phy_real_ball_acos(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value);
phy_status phy_real_ball_asinh(const phy_real_ball *argument, uint32_t rounds,
                               phy_real_ball *out_value);
phy_status phy_real_ball_acosh(const phy_real_ball *argument, uint32_t rounds,
                               phy_real_ball *out_value);
phy_status phy_real_ball_atanh(const phy_real_ball *argument, uint32_t rounds,
                               phy_real_ball *out_value);
/* Certified on input balls wholly contained in [-1,1]. */
phy_status phy_real_ball_erf(const phy_real_ball *argument, uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_erfc(const phy_real_ball *argument, uint32_t rounds,
                              phy_real_ball *out_value);

/*
 * A certified complex rectangle: real + I imaginary.  Both components share
 * one exact context.  Principal-branch routines return PHY_ERR_DOMAIN when a
 * rectangle straddles an unresolved pole or branch cut.
 */
typedef struct {
    phy_real_ball real;
    phy_real_ball imaginary;
    uint32_t private_magic;
} phy_complex_ball;

phy_status phy_complex_ball_init(phy_exact_context *context,
                                 phy_complex_ball *ball);
void phy_complex_ball_destroy(phy_complex_ball *ball);
phy_status phy_complex_ball_validate(const phy_complex_ball *ball);
phy_status phy_complex_ball_set_i64(phy_complex_ball *ball,
                                    int64_t real_numerator,
                                    int64_t real_denominator,
                                    int64_t imaginary_numerator,
                                    int64_t imaginary_denominator);
phy_status phy_complex_ball_set_real(phy_complex_ball *ball,
                                     const phy_real_ball *real);
phy_status phy_complex_ball_copy(const phy_complex_ball *source,
                                 phy_complex_ball *destination);
phy_status phy_complex_ball_contains_zero_checked(const phy_complex_ball *ball,
                                                  bool *out_contains_zero);
bool phy_complex_ball_contains_zero(const phy_complex_ball *ball);
phy_status phy_complex_ball_add(const phy_complex_ball *left,
                                const phy_complex_ball *right,
                                phy_complex_ball *out_sum);
phy_status phy_complex_ball_subtract(const phy_complex_ball *left,
                                     const phy_complex_ball *right,
                                     phy_complex_ball *out_difference);
phy_status phy_complex_ball_multiply(const phy_complex_ball *left,
                                     const phy_complex_ball *right,
                                     phy_complex_ball *out_product);
phy_status phy_complex_ball_divide(const phy_complex_ball *dividend,
                                   const phy_complex_ball *divisor,
                                   phy_complex_ball *out_quotient);
phy_status phy_complex_ball_conjugate(const phy_complex_ball *argument,
                                      phy_complex_ball *out_value);
phy_status phy_complex_ball_pow_i32(const phy_complex_ball *base,
                                    int32_t exponent,
                                    phy_complex_ball *out_power);
phy_status phy_complex_ball_sqrt(const phy_complex_ball *argument,
                                 uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_exp(const phy_complex_ball *argument,
                                uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_log(const phy_complex_ball *argument,
                                uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_sin(const phy_complex_ball *argument,
                                uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_cos(const phy_complex_ball *argument,
                                uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_tan(const phy_complex_ball *argument,
                                uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_sinh(const phy_complex_ball *argument,
                                 uint32_t rounds,
                                 phy_complex_ball *out_value);
phy_status phy_complex_ball_cosh(const phy_complex_ball *argument,
                                 uint32_t rounds,
                                 phy_complex_ball *out_value);
phy_status phy_complex_ball_tanh(const phy_complex_ball *argument,
                                 uint32_t rounds,
                                 phy_complex_ball *out_value);
phy_status phy_complex_ball_asin(const phy_complex_ball *argument,
                                 uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_acos(const phy_complex_ball *argument,
                                 uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_atan(const phy_complex_ball *argument,
                                 uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_asinh(const phy_complex_ball *argument,
                                  uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_acosh(const phy_complex_ball *argument,
                                  uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_atanh(const phy_complex_ball *argument,
                                  uint32_t rounds, phy_complex_ball *out_value);

/*
 * Certified complex special functions.  Erf is entire and uses a globally
 * convergent, resource-bounded series.  Gamma/LogGamma/Digamma use exact
 * recurrence into the right half-plane followed by a Stirling expansion with
 * an explicit Bernoulli remainder enclosure.  A rectangle containing a pole,
 * or a request exceeding the bounded shift/series ceilings, fails closed.
 */
phy_status phy_complex_ball_erf(const phy_complex_ball *argument,
                                uint32_t rounds, phy_complex_ball *out_value);
phy_status phy_complex_ball_erfc(const phy_complex_ball *argument,
                                 uint32_t rounds,
                                 phy_complex_ball *out_value);
phy_status phy_complex_ball_loggamma(const phy_complex_ball *argument,
                                     uint32_t rounds,
                                     phy_complex_ball *out_value);
phy_status phy_complex_ball_gamma(const phy_complex_ball *argument,
                                  uint32_t rounds,
                                  phy_complex_ball *out_value);
phy_status phy_complex_ball_digamma(const phy_complex_ball *argument,
                                    uint32_t rounds,
                                    phy_complex_ball *out_value);

#ifdef __cplusplus
}
#endif

#endif /* PHY_BALL_H */
