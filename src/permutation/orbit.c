#include "permutation_internal.h"

#include <string.h>

static size_t align_up(size_t value, size_t alignment)
{
    const size_t remainder = value % alignment;
    return remainder == 0u ? value : value + alignment - remainder;
}

phy_status phy_perm_group_contains(const phy_perm_group *group,
                                   const uint16_t *image, int sign,
                                   bool *out_member)
{
    if (group == NULL || out_member == NULL || !group->built ||
        (sign != -1 && sign != 1) ||
        !phy_permutation_valid(image, group->degree)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_member = false;
    phy_perm_chain chain;
    phy_status status = phy_perm_chain_allocate(group, &chain);
    if (status != PHY_OK) {
        return status;
    }
    uint32_t steps = 0u;
    status = phy_perm_chain_build(group, &chain, &steps);
    int remainder_sign = 1;
    if (status == PHY_OK) {
        status = phy_perm_sift(
            group, &chain, image, sign, 0u, chain.work[0],
            &remainder_sign, chain.work[1], chain.work[2], &steps);
    }
    if (status == PHY_OK &&
        phy_perm_identity_image(chain.work[0], group->degree)) {
        *out_member =
            remainder_sign > 0 || group->negative_identity;
    }
    phy_perm_chain_free(&chain);
    return status;
}

phy_status phy_perm_group_orbit(const phy_perm_group *group, uint16_t point,
                                uint16_t *out_points, size_t capacity,
                                size_t *out_count)
{
    if (group == NULL || !group->built || out_count == NULL ||
        (size_t)point >= group->degree ||
        (capacity != 0u && out_points == NULL)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    const size_t n = group->degree;
    if (capacity < n) {
        return PHY_ERR_TERM_LIMIT;
    }
    const size_t queue_offset =
        align_up(n * sizeof(uint8_t), sizeof(uint16_t));
    if (n > (SIZE_MAX - queue_offset) / sizeof(uint16_t)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t bytes = queue_offset + n * sizeof(uint16_t);
    if (bytes > group->limits.max_bytes - group->persistent_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    uint8_t *memory = phy_alloc(bytes);
    if (memory == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(memory, 0, bytes);
    uint8_t *seen = memory;
    uint16_t *queue = (uint16_t *)(memory + queue_offset);
    size_t head = 0u;
    size_t tail = 0u;
    uint32_t steps = 0u;
    seen[point] = 1u;
    queue[tail++] = point;
    phy_status status = PHY_OK;
    while (head < tail && status == PHY_OK) {
        const uint16_t current = queue[head++];
        for (size_t generator = 0u;
             generator < group->strong_count; ++generator) {
            status = phy_perm_step(group, &steps, 1u);
            if (status != PHY_OK) {
                break;
            }
            const uint16_t image =
                group->strong[generator * n + current];
            if (seen[image] == 0u) {
                seen[image] = 1u;
                queue[tail++] = image;
            }
        }
    }
    if (status == PHY_OK) {
        memcpy(out_points, queue, tail * sizeof(*out_points));
        *out_count = tail;
    }
    phy_free(memory, bytes);
    return status;
}
