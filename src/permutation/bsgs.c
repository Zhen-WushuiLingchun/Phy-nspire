#include "permutation_internal.h"

#include <string.h>

static size_t align_up(size_t value, size_t alignment)
{
    const size_t remainder = value % alignment;
    return remainder == 0u ? value : value + alignment - remainder;
}

static bool add_bytes(size_t *total, size_t count, size_t element)
{
    if (count > SIZE_MAX / element) {
        return false;
    }
    const size_t bytes = count * element;
    if (*total > SIZE_MAX - bytes) {
        return false;
    }
    *total += bytes;
    return true;
}

phy_status phy_perm_chain_allocate(const phy_perm_group *group,
                                   phy_perm_chain *out_chain)
{
    if (group == NULL || out_chain == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(out_chain, 0, sizeof *out_chain);
    const size_t n = group->degree;
    if (n > SIZE_MAX / n) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t n2 = n * n;
    if (n2 > SIZE_MAX / n) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t n3 = n2 * n;

    size_t offset = 0u;
    const size_t valid_offset = offset;
    if (!add_bytes(&offset, n2, sizeof(uint8_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t sign_offset = offset;
    if (!add_bytes(&offset, n2, sizeof(int8_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    offset = align_up(offset, sizeof(uint16_t));
    const size_t transversal_offset = offset;
    if (!add_bytes(&offset, n3, sizeof(uint16_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    offset = align_up(offset, sizeof(size_t));
    const size_t orbit_count_offset = offset;
    if (!add_bytes(&offset, n, sizeof(size_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    offset = align_up(offset, sizeof(uint16_t));
    const size_t queue_offset = offset;
    if (!add_bytes(&offset, n, sizeof(uint16_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    size_t work_offset[6];
    for (size_t i = 0u; i < 6u; ++i) {
        work_offset[i] = offset;
        if (!add_bytes(&offset, n, sizeof(uint16_t))) {
            return PHY_ERR_MEMORY_LIMIT;
        }
    }
    if (offset > group->limits.max_bytes - group->persistent_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    void *memory = phy_alloc(offset);
    if (memory == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(memory, 0, offset);
    uint8_t *bytes = memory;
    out_chain->degree = n;
    out_chain->valid = bytes + valid_offset;
    out_chain->sign = (int8_t *)(bytes + sign_offset);
    out_chain->transversal =
        (uint16_t *)(bytes + transversal_offset);
    out_chain->orbit_count = (size_t *)(bytes + orbit_count_offset);
    out_chain->queue = (uint16_t *)(bytes + queue_offset);
    for (size_t i = 0u; i < 6u; ++i) {
        out_chain->work[i] = (uint16_t *)(bytes + work_offset[i]);
    }
    out_chain->memory = memory;
    out_chain->bytes = offset;
    return PHY_OK;
}

void phy_perm_chain_free(phy_perm_chain *chain)
{
    if (chain == NULL || chain->memory == NULL) {
        return;
    }
    phy_free(chain->memory, chain->bytes);
    memset(chain, 0, sizeof *chain);
}

static uint16_t *transversal_at(phy_perm_chain *chain, size_t level,
                                size_t point)
{
    return &chain->transversal[
        (level * chain->degree + point) * chain->degree];
}

static const uint16_t *const_transversal_at(const phy_perm_chain *chain,
                                            size_t level, size_t point)
{
    return &chain->transversal[
        (level * chain->degree + point) * chain->degree];
}

phy_status phy_perm_chain_build(const phy_perm_group *group,
                                phy_perm_chain *chain, uint32_t *steps)
{
    if (group == NULL || chain == NULL || steps == NULL ||
        chain->degree != group->degree) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const size_t n = group->degree;
    memset(chain->valid, 0, n * n * sizeof(*chain->valid));
    memset(chain->sign, 0, n * n * sizeof(*chain->sign));
    for (size_t level = 0u; level < n; ++level) {
        size_t head = 0u;
        size_t tail = 0u;
        const uint16_t base = (uint16_t)level;
        chain->valid[level * n + base] = 1u;
        chain->sign[level * n + base] = 1;
        uint16_t *identity = transversal_at(chain, level, base);
        phy_permutation_identity(identity, n);
        chain->queue[tail++] = base;

        while (head < tail) {
            const uint16_t point = chain->queue[head++];
            const uint16_t *representative =
                const_transversal_at(chain, level, point);
            const int representative_sign =
                chain->sign[level * n + point];
            for (size_t generator = 0u;
                 generator < group->strong_count; ++generator) {
                if ((size_t)group->strong_levels[generator] < level) {
                    continue;
                }
                phy_status status = phy_perm_step(group, steps, 1u);
                if (status != PHY_OK) {
                    return status;
                }
                const uint16_t *permutation =
                    &group->strong[generator * n];
                const uint16_t image = permutation[point];
                if (chain->valid[level * n + image] != 0u) {
                    continue;
                }
                uint16_t *target =
                    transversal_at(chain, level, image);
                phy_perm_compose_unchecked(
                    permutation, representative, n, target);
                chain->sign[level * n + image] =
                    (int8_t)(group->strong_signs[generator] *
                             representative_sign);
                chain->valid[level * n + image] = 1u;
                chain->queue[tail++] = image;
            }
        }
        chain->orbit_count[level] = tail;
    }
    return PHY_OK;
}

phy_status phy_perm_sift(const phy_perm_group *group,
                         const phy_perm_chain *chain,
                         const uint16_t *image, int sign, size_t start_level,
                         uint16_t *out_remainder, int *out_sign,
                         uint16_t *scratch_inverse,
                         uint16_t *scratch_composed,
                         uint32_t *steps)
{
    if (group == NULL || chain == NULL || image == NULL ||
        out_remainder == NULL || out_sign == NULL ||
        scratch_inverse == NULL || scratch_composed == NULL ||
        steps == NULL || start_level > group->degree) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const size_t n = group->degree;
    memcpy(out_remainder, image, n * sizeof(*image));
    int remainder_sign = sign;
    for (size_t level = start_level; level < n; ++level) {
        const uint16_t image_of_base = out_remainder[level];
        if (chain->valid[level * n + image_of_base] == 0u) {
            *out_sign = remainder_sign;
            return PHY_OK;
        }
        phy_status status = phy_perm_step(group, steps, 2u);
        if (status != PHY_OK) {
            return status;
        }
        const uint16_t *transversal =
            const_transversal_at(chain, level, image_of_base);
        phy_perm_inverse_unchecked(transversal, n, scratch_inverse);
        phy_perm_compose_unchecked(
            scratch_inverse, out_remainder, n, scratch_composed);
        memcpy(
            out_remainder, scratch_composed, n * sizeof(*out_remainder));
        remainder_sign *= chain->sign[level * n + image_of_base];
    }
    *out_sign = remainder_sign;
    return PHY_OK;
}

static phy_status initialize_strong_generators(phy_perm_group *group)
{
    group->strong_count = 0u;
    group->negative_identity = false;
    for (size_t i = 0u; i < group->generator_count; ++i) {
        bool changed = false;
        const phy_status status = phy_perm_add_strong(
            group, &group->generators[i * group->degree],
            group->generator_signs[i], &changed);
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static phy_status add_schreier_generators(phy_perm_group *group,
                                          const phy_perm_chain *chain,
                                          uint32_t *steps,
                                          bool *out_changed)
{
    const size_t n = group->degree;
    /*
     * The transversals describe exactly this generator snapshot.  Remainders
     * discovered below join the next Schreier pass; using one immediately
     * could address an orbit point absent from the current chain.
     */
    const size_t strong_at_start = group->strong_count;
    *out_changed = false;
    for (size_t level = 0u; level < n; ++level) {
        for (size_t point = 0u; point < n; ++point) {
            if (chain->valid[level * n + point] == 0u) {
                continue;
            }
            const uint16_t *tx =
                const_transversal_at(chain, level, point);
            const int tx_sign = chain->sign[level * n + point];
            for (size_t generator = 0u;
                 generator < strong_at_start; ++generator) {
                if ((size_t)group->strong_levels[generator] < level) {
                    continue;
                }
                phy_status status = phy_perm_step(group, steps, 4u);
                if (status != PHY_OK) {
                    return status;
                }
                const uint16_t *s = &group->strong[generator * n];
                const uint16_t y = s[point];
                const uint16_t *ty =
                    const_transversal_at(chain, level, y);
                const int ty_sign = chain->sign[level * n + y];
                uint16_t *temp = chain->work[0];
                uint16_t *inverse = chain->work[1];
                uint16_t *schreier = chain->work[2];
                uint16_t *remainder = chain->work[3];
                phy_perm_compose_unchecked(s, tx, n, temp);
                phy_perm_inverse_unchecked(ty, n, inverse);
                phy_perm_compose_unchecked(
                    inverse, temp, n, schreier);
                const int schreier_sign =
                    ty_sign * group->strong_signs[generator] * tx_sign;
                int remainder_sign = 1;
                status = phy_perm_sift(
                    group, chain, schreier, schreier_sign, level + 1u,
                    remainder, &remainder_sign, chain->work[4],
                    chain->work[5], steps);
                if (status != PHY_OK) {
                    return status;
                }
                bool changed = false;
                status = phy_perm_add_strong(
                    group, remainder, remainder_sign, &changed);
                if (status != PHY_OK) {
                    return status;
                }
                *out_changed = *out_changed || changed;
            }
        }
    }
    return PHY_OK;
}

static phy_status compute_order(phy_perm_group *group,
                                const phy_perm_chain *chain)
{
    uint64_t order = group->negative_identity ? 2u : 1u;
    for (size_t level = 0u; level < group->degree; ++level) {
        const uint64_t factor = (uint64_t)chain->orbit_count[level];
        if (factor != 0u && order > UINT64_MAX / factor) {
            return PHY_ERR_OVERFLOW;
        }
        order *= factor;
    }
    group->order = order;
    return PHY_OK;
}

phy_status phy_perm_group_build_bsgs(phy_perm_group *group)
{
    if (group == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    group->built = false;
    group->order = 0u;
    phy_status status = initialize_strong_generators(group);
    if (status != PHY_OK) {
        return status;
    }
    phy_perm_chain chain;
    status = phy_perm_chain_allocate(group, &chain);
    if (status != PHY_OK) {
        return status;
    }
    uint32_t steps = 0u;
    bool changed = true;
    while (changed) {
        status = phy_perm_chain_build(group, &chain, &steps);
        if (status != PHY_OK) {
            break;
        }
        status =
            add_schreier_generators(group, &chain, &steps, &changed);
        if (status != PHY_OK) {
            break;
        }
    }
    if (status == PHY_OK) {
        status = phy_perm_chain_build(group, &chain, &steps);
    }
    if (status == PHY_OK) {
        status = compute_order(group, &chain);
    }
    phy_perm_chain_free(&chain);
    if (status == PHY_OK) {
        group->built = true;
    }
    return status;
}
