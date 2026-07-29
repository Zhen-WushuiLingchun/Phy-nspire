/*
 * Phy-nspire — bounded signed permutation groups.
 *
 * Permutations use image notation: p[i] is the image of i, and composition
 * p*q means p(q(i)).  A separate +/- sign represents tensor antisymmetry.
 */
#ifndef PHY_PERMUTATION_H
#define PHY_PERMUTATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_perm_group phy_perm_group;

typedef struct {
    size_t max_degree;            /* default 32 */
    size_t max_generators;        /* input generators; default 256 */
    size_t max_strong_generators; /* Schreier generators; default 1024 */
    uint32_t max_steps;           /* one BSGS/membership operation */
    size_t max_bytes;             /* persistent plus BSGS scratch */
} phy_perm_limits;

void phy_perm_limits_defaults(phy_perm_limits *out_limits);

bool phy_permutation_valid(const uint16_t *image, size_t degree);
phy_status phy_permutation_identity(uint16_t *out_image, size_t degree);
phy_status phy_permutation_compose(const uint16_t *left,
                                   const uint16_t *right, size_t degree,
                                   uint16_t *out_image);
phy_status phy_permutation_inverse(const uint16_t *image, size_t degree,
                                   uint16_t *out_inverse);

phy_status phy_perm_group_create(size_t degree,
                                 const phy_perm_limits *limits,
                                 phy_perm_group **out_group);
void phy_perm_group_destroy(phy_perm_group *group);
size_t phy_perm_group_degree(const phy_perm_group *group);

phy_status phy_perm_group_add_generator(phy_perm_group *group,
                                        const uint16_t *image, int sign);
size_t phy_perm_group_generator_count(const phy_perm_group *group);

phy_status phy_perm_group_build_bsgs(phy_perm_group *group);
bool phy_perm_group_is_built(const phy_perm_group *group);
size_t phy_perm_group_base_size(const phy_perm_group *group);
size_t phy_perm_group_strong_generator_count(const phy_perm_group *group);
uint64_t phy_perm_group_order(const phy_perm_group *group);
bool phy_perm_group_has_negative_identity(const phy_perm_group *group);

phy_status phy_perm_group_contains(const phy_perm_group *group,
                                   const uint16_t *image, int sign,
                                   bool *out_member);
phy_status phy_perm_group_orbit(const phy_perm_group *group, uint16_t point,
                                uint16_t *out_points, size_t capacity,
                                size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* PHY_PERMUTATION_H */

