#ifndef PHY_CAS_COMPLEX_ROOTS_H
#define PHY_CAS_COMPLEX_ROOTS_H

#include <stddef.h>
#include <stdint.h>

#include "phy/ball.h"

/*
 * Isolate every zero of a square-free rational polynomial. Coefficients are
 * ascending (constant first). Returned rectangles are pairwise disjoint and
 * each is certified to contain exactly one zero by a square-boundary
 * Rouché/Pellet test. Approximate Durand-Kerner centers are never trusted
 * without this certificate. If the requested output grid is too coarse to
 * separate clustered roots, the candidate grid is refined through a fixed
 * number of deterministic escalation levels; exhaustion remains a typed
 * resource error and never publishes a partial root list.
 */
phy_status phy_complex_roots_isolate(
    phy_exact_context *exact, const phy_bigrat *coefficients,
    size_t coefficient_count, uint32_t bits, phy_complex_ball *roots,
    size_t root_capacity, size_t *out_root_count);

#endif /* PHY_CAS_COMPLEX_ROOTS_H */
