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

phy_status phy_real_ball_init(phy_exact_context *context,
                              phy_real_ball *ball);
void phy_real_ball_destroy(phy_real_ball *ball);
phy_status phy_real_ball_validate(const phy_real_ball *ball);

phy_status phy_real_ball_set_i64(phy_real_ball *ball,
                                 int64_t numerator,
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
phy_status phy_real_ball_pow_i32(const phy_real_ball *base,
                                 int32_t exponent,
                                 phy_real_ball *out_power);
/* Monotone rational bisection; rounds is an explicit work/width bound. */
phy_status phy_real_ball_sqrt(const phy_real_ball *argument,
                              uint32_t rounds,
                              phy_real_ball *out_root);

/*
 * Certified elementary real functions.  `rounds` controls the bounded
 * rational range-reduction/Taylor work; callers normally use roughly four
 * rounds per requested decimal digit.  Every routine is transactional and
 * returns PHY_ERR_DOMAIN when the whole input ball is not inside the real
 * function's domain.  Excessive range reduction returns PHY_ERR_TERM_LIMIT.
 */
phy_status phy_real_ball_exp(const phy_real_ball *argument,
                             uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_log(const phy_real_ball *argument,
                             uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_sin(const phy_real_ball *argument,
                             uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_cos(const phy_real_ball *argument,
                             uint32_t rounds,
                             phy_real_ball *out_value);
phy_status phy_real_ball_tan(const phy_real_ball *argument,
                             uint32_t rounds,
                             phy_real_ball *out_value);

#ifdef __cplusplus
}
#endif

#endif /* PHY_BALL_H */
