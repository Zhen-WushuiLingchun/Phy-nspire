#ifndef PHY_PERMUTATION_INTERNAL_H
#define PHY_PERMUTATION_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/permutation.h"
#include "phy/platform.h"

struct phy_perm_group {
    size_t degree;
    phy_perm_limits limits;
    void *storage;
    size_t storage_bytes;
    size_t persistent_bytes;

    uint16_t *generators;
    int8_t *generator_signs;
    size_t generator_count;

    uint16_t *strong;
    int8_t *strong_signs;
    uint16_t *strong_levels;
    size_t strong_count;

    bool built;
    bool negative_identity;
    uint64_t order;
};

typedef struct {
    size_t degree;
    uint8_t *valid;       /* [level][point] */
    int8_t *sign;         /* sign of transversal [level][point] */
    uint16_t *transversal;/* permutation [level][point][image] */
    size_t *orbit_count;  /* one per level */
    uint16_t *queue;
    uint16_t *work[6];
    void *memory;
    size_t bytes;
} phy_perm_chain;

phy_status phy_perm_resolve_limits(const phy_perm_limits *requested,
                                   phy_perm_limits *out);
size_t phy_perm_level(const uint16_t *image, size_t degree);
bool phy_perm_identity_image(const uint16_t *image, size_t degree);
void phy_perm_compose_unchecked(const uint16_t *left,
                                const uint16_t *right, size_t degree,
                                uint16_t *out);
void phy_perm_inverse_unchecked(const uint16_t *image, size_t degree,
                                uint16_t *out);
phy_status phy_perm_step(const phy_perm_group *group, uint32_t *used,
                         uint32_t amount);
phy_status phy_perm_add_strong(phy_perm_group *group,
                               const uint16_t *image, int sign,
                               bool *out_changed);

phy_status phy_perm_chain_allocate(const phy_perm_group *group,
                                   phy_perm_chain *out_chain);
void phy_perm_chain_free(phy_perm_chain *chain);
phy_status phy_perm_chain_build(const phy_perm_group *group,
                                phy_perm_chain *chain, uint32_t *steps);
phy_status phy_perm_sift(const phy_perm_group *group,
                         const phy_perm_chain *chain,
                         const uint16_t *image, int sign, size_t start_level,
                         uint16_t *out_remainder, int *out_sign,
                         uint16_t *scratch_inverse,
                         uint16_t *scratch_composed,
                         uint32_t *steps);

#endif /* PHY_PERMUTATION_INTERNAL_H */

