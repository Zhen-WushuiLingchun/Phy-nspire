#include "permutation_internal.h"

#include <limits.h>
#include <string.h>

#define PHY_PERM_DEFAULT_DEGREE 32u
#define PHY_PERM_DEFAULT_GENERATORS 256u
#define PHY_PERM_DEFAULT_STRONG 1024u
#define PHY_PERM_DEFAULT_STEPS 2000000u
#define PHY_PERM_DEFAULT_BYTES (768u * 1024u)

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

void phy_perm_limits_defaults(phy_perm_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_degree = PHY_PERM_DEFAULT_DEGREE;
    out_limits->max_generators = PHY_PERM_DEFAULT_GENERATORS;
    out_limits->max_strong_generators = PHY_PERM_DEFAULT_STRONG;
    out_limits->max_steps = PHY_PERM_DEFAULT_STEPS;
    out_limits->max_bytes = PHY_PERM_DEFAULT_BYTES;
}

phy_status phy_perm_resolve_limits(const phy_perm_limits *requested,
                                   phy_perm_limits *out)
{
    if (out == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_perm_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_degree != 0u) {
            out->max_degree = requested->max_degree;
        }
        if (requested->max_generators != 0u) {
            out->max_generators = requested->max_generators;
        }
        if (requested->max_strong_generators != 0u) {
            out->max_strong_generators =
                requested->max_strong_generators;
        }
        if (requested->max_steps != 0u) {
            out->max_steps = requested->max_steps;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
    }
    if (out->max_degree == 0u || out->max_degree > UINT16_MAX ||
        out->max_generators == 0u ||
        out->max_strong_generators == 0u || out->max_steps == 0u ||
        out->max_bytes < sizeof(phy_perm_group)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

bool phy_permutation_valid(const uint16_t *image, size_t degree)
{
    if (image == NULL || degree == 0u || degree > UINT16_MAX) {
        return false;
    }
    for (size_t i = 0u; i < degree; ++i) {
        if ((size_t)image[i] >= degree) {
            return false;
        }
        for (size_t prior = 0u; prior < i; ++prior) {
            if (image[prior] == image[i]) {
                return false;
            }
        }
    }
    return true;
}

phy_status phy_permutation_identity(uint16_t *out_image, size_t degree)
{
    if (out_image == NULL || degree == 0u || degree > UINT16_MAX) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0u; i < degree; ++i) {
        out_image[i] = (uint16_t)i;
    }
    return PHY_OK;
}

void phy_perm_compose_unchecked(const uint16_t *left,
                                const uint16_t *right, size_t degree,
                                uint16_t *out)
{
    for (size_t i = 0u; i < degree; ++i) {
        out[i] = left[right[i]];
    }
}

phy_status phy_permutation_compose(const uint16_t *left,
                                   const uint16_t *right, size_t degree,
                                   uint16_t *out_image)
{
    if (out_image == NULL || !phy_permutation_valid(left, degree) ||
        !phy_permutation_valid(right, degree) ||
        out_image == left || out_image == right) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_perm_compose_unchecked(left, right, degree, out_image);
    return PHY_OK;
}

void phy_perm_inverse_unchecked(const uint16_t *image, size_t degree,
                                uint16_t *out)
{
    for (size_t i = 0u; i < degree; ++i) {
        out[image[i]] = (uint16_t)i;
    }
}

phy_status phy_permutation_inverse(const uint16_t *image, size_t degree,
                                   uint16_t *out_inverse)
{
    if (out_inverse == NULL || !phy_permutation_valid(image, degree) ||
        out_inverse == image) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_perm_inverse_unchecked(image, degree, out_inverse);
    return PHY_OK;
}

size_t phy_perm_level(const uint16_t *image, size_t degree)
{
    for (size_t i = 0u; i < degree; ++i) {
        if ((size_t)image[i] != i) {
            return i;
        }
    }
    return degree;
}

bool phy_perm_identity_image(const uint16_t *image, size_t degree)
{
    return phy_perm_level(image, degree) == degree;
}

phy_status phy_perm_group_create(size_t degree,
                                 const phy_perm_limits *limits,
                                 phy_perm_group **out_group)
{
    if (out_group == NULL || degree == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_group = NULL;
    phy_perm_limits resolved;
    phy_status status = phy_perm_resolve_limits(limits, &resolved);
    if (status != PHY_OK) {
        return status;
    }
    if (degree > resolved.max_degree) {
        return PHY_ERR_TERM_LIMIT;
    }

    size_t offset = 0u;
    if (resolved.max_generators > SIZE_MAX / degree ||
        resolved.max_strong_generators > SIZE_MAX / degree) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t generator_images = resolved.max_generators * degree;
    const size_t strong_images =
        resolved.max_strong_generators * degree;
    offset = align_up(offset, sizeof(uint16_t));
    const size_t generators_offset = offset;
    if (!add_bytes(&offset, generator_images, sizeof(uint16_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t generator_signs_offset = offset;
    if (!add_bytes(&offset, resolved.max_generators, sizeof(int8_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    offset = align_up(offset, sizeof(uint16_t));
    const size_t strong_offset = offset;
    if (!add_bytes(&offset, strong_images, sizeof(uint16_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t strong_signs_offset = offset;
    if (!add_bytes(
            &offset, resolved.max_strong_generators, sizeof(int8_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    offset = align_up(offset, sizeof(uint16_t));
    const size_t levels_offset = offset;
    if (!add_bytes(
            &offset, resolved.max_strong_generators, sizeof(uint16_t))) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    if (offset > resolved.max_bytes - sizeof(phy_perm_group)) {
        return PHY_ERR_MEMORY_LIMIT;
    }

    phy_perm_group *group = phy_alloc(sizeof *group);
    if (group == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(group, 0, sizeof *group);
    group->storage = phy_alloc(offset);
    if (group->storage == NULL) {
        phy_free(group, sizeof *group);
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(group->storage, 0, offset);
    uint8_t *bytes = group->storage;
    group->degree = degree;
    group->limits = resolved;
    group->storage_bytes = offset;
    group->persistent_bytes = sizeof *group + offset;
    group->generators = (uint16_t *)(bytes + generators_offset);
    group->generator_signs = (int8_t *)(bytes + generator_signs_offset);
    group->strong = (uint16_t *)(bytes + strong_offset);
    group->strong_signs = (int8_t *)(bytes + strong_signs_offset);
    group->strong_levels = (uint16_t *)(bytes + levels_offset);
    *out_group = group;
    return PHY_OK;
}

void phy_perm_group_destroy(phy_perm_group *group)
{
    if (group == NULL) {
        return;
    }
    phy_free(group->storage, group->storage_bytes);
    phy_free(group, sizeof *group);
}

size_t phy_perm_group_degree(const phy_perm_group *group)
{
    return group != NULL ? group->degree : 0u;
}

phy_status phy_perm_group_add_generator(phy_perm_group *group,
                                        const uint16_t *image, int sign)
{
    if (group == NULL || (sign != -1 && sign != 1) ||
        !phy_permutation_valid(image, group->degree)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0u; i < group->generator_count; ++i) {
        if (group->generator_signs[i] == sign &&
            memcmp(&group->generators[i * group->degree], image,
                   group->degree * sizeof(*image)) == 0) {
            return PHY_OK;
        }
    }
    if (group->generator_count >= group->limits.max_generators) {
        return PHY_ERR_TERM_LIMIT;
    }
    memcpy(
        &group->generators[group->generator_count * group->degree], image,
        group->degree * sizeof(*image));
    group->generator_signs[group->generator_count] = (int8_t)sign;
    group->generator_count++;
    group->built = false;
    group->order = 0u;
    return PHY_OK;
}

size_t phy_perm_group_generator_count(const phy_perm_group *group)
{
    return group != NULL ? group->generator_count : 0u;
}

phy_status phy_perm_step(const phy_perm_group *group, uint32_t *used,
                         uint32_t amount)
{
    if (group == NULL || used == NULL ||
        amount > group->limits.max_steps - *used) {
        return PHY_ERR_TIMEOUT;
    }
    *used += amount;
    return PHY_OK;
}

phy_status phy_perm_add_strong(phy_perm_group *group,
                               const uint16_t *image, int sign,
                               bool *out_changed)
{
    *out_changed = false;
    if (phy_perm_identity_image(image, group->degree)) {
        if (sign < 0 && !group->negative_identity) {
            group->negative_identity = true;
            *out_changed = true;
        }
        return PHY_OK;
    }
    for (size_t i = 0u; i < group->strong_count; ++i) {
        if (memcmp(&group->strong[i * group->degree], image,
                   group->degree * sizeof(*image)) != 0) {
            continue;
        }
        if (group->strong_signs[i] != sign &&
            !group->negative_identity) {
            group->negative_identity = true;
            *out_changed = true;
        }
        return PHY_OK;
    }
    if (group->strong_count >= group->limits.max_strong_generators) {
        return PHY_ERR_TERM_LIMIT;
    }
    memcpy(&group->strong[group->strong_count * group->degree], image,
           group->degree * sizeof(*image));
    group->strong_signs[group->strong_count] = (int8_t)sign;
    group->strong_levels[group->strong_count] =
        (uint16_t)phy_perm_level(image, group->degree);
    group->strong_count++;
    *out_changed = true;
    return PHY_OK;
}

bool phy_perm_group_is_built(const phy_perm_group *group)
{
    return group != NULL && group->built;
}

size_t phy_perm_group_base_size(const phy_perm_group *group)
{
    return group != NULL && group->built ? group->degree : 0u;
}

size_t phy_perm_group_strong_generator_count(const phy_perm_group *group)
{
    return group != NULL && group->built ? group->strong_count : 0u;
}

uint64_t phy_perm_group_order(const phy_perm_group *group)
{
    return group != NULL && group->built ? group->order : 0u;
}

bool phy_perm_group_has_negative_identity(const phy_perm_group *group)
{
    return group != NULL && group->built && group->negative_identity;
}
