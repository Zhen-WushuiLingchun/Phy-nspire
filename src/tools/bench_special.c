/* Host microbenchmark for the certified complex-special-function path. */
#include <stdio.h>
#include <time.h>

#include "phy/ball.h"
#include "phy/platform.h"

typedef phy_status (*special_fn)(const phy_complex_ball *, uint32_t,
                                 phy_complex_ball *);

static int measure(const char *name, special_fn function,
                   const phy_complex_ball *argument, uint32_t rounds,
                   phy_complex_ball *result)
{
    const clock_t begin = clock();
    const phy_status status = function(argument, rounds, result);
    const clock_t end = clock();
    const double milliseconds =
        1000.0 * (double)(end - begin) / (double)CLOCKS_PER_SEC;
    printf("%-10s rounds=%u status=%s cpu_ms=%.3f\n", name,
           (unsigned)rounds, phy_status_name(status), milliseconds);
    return status == PHY_OK ? 0 : 1;
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) return 2;
    phy_exact_limits limits;
    phy_exact_limits_defaults(&limits);
    limits.max_steps = 32000000u;
    limits.max_bytes = 2u * 1024u * 1024u;
    phy_exact_context *exact = phy_exact_context_create(&limits);
    if (exact == NULL) {
        phy_platform_shutdown();
        return 2;
    }
    phy_complex_ball argument = {0};
    phy_complex_ball result = {0};
    phy_status status = phy_complex_ball_init(exact, &argument);
    if (status == PHY_OK) status = phy_complex_ball_init(exact, &result);
    if (status == PHY_OK) {
        status = phy_complex_ball_set_i64(&argument, 1, 3, 1, 4);
    }
    int failed = status == PHY_OK ? 0 : 1;
    if (!failed) {
        failed |= measure("Erf", phy_complex_ball_erf,
                          &argument, 48u, &result);
        failed |= measure("Gamma", phy_complex_ball_gamma,
                          &argument, 48u, &result);
        failed |= measure("LogGamma", phy_complex_ball_loggamma,
                          &argument, 48u, &result);
        failed |= measure("Digamma", phy_complex_ball_digamma,
                          &argument, 48u, &result);
    }
    phy_complex_ball_destroy(&result);
    phy_complex_ball_destroy(&argument);
    phy_exact_context_destroy(exact);
    phy_platform_shutdown();
    return failed;
}
