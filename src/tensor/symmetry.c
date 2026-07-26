/*
 * Phy-nspire — declared slot symmetries as a signed permutation group.
 *
 * docs/agent-tasks/TENSOR_CORE.md requires symmetry to be a fill-and-check
 * discipline over dense storage rather than a packed layout. That makes the
 * group itself the whole mechanism, so it is worth being precise about what
 * it means.
 *
 * A declaration is a permutation p of the slots and a sign s, read as
 *
 *     T[p . a] = s * T[a]      for every index tuple a,
 *
 * where the action places the index at slot i into slot p[i]:
 *
 *     (p . a)[p[i]] = a[i].
 *
 * That is a left action -- p . (q . a) = (p compose q) . a with
 * (p compose q)[i] = p[q[i]] -- so the declarations generate a group and the
 * signs form a homomorphism onto {+1, -1}. Closing it at declaration time is
 * what makes a contradiction a declaration-time error instead of a
 * fill-time surprise.
 *
 * Two consequences are load-bearing and neither is obvious:
 *
 *   - if some g fixes a tuple and carries sign -1, then T[a] = -T[a], so that
 *     component is forced to vanish. This is why R_aacd is zero and why the
 *     sign of a stored component can be 0.
 *   - if the closure ever reaches the identity with sign -1, the whole tensor
 *     vanishes. That is consistent rather than malformed, so it is reported
 *     rather than rejected.
 *
 * Rank is at most 4, so the group is at most 4! = 24 elements and every loop
 * here is over a fixed, tiny bound. Nothing in this file allocates.
 */
#include <string.h>

#include "tensor_internal.h"

void phy_tensor_permute(const phy_tensor_perm *perm, const unsigned *indices,
                        unsigned rank, unsigned *out_indices)
{
    for (unsigned slot = 0u; slot < rank; ++slot) {
        out_indices[perm->image[slot]] = indices[slot];
    }
}

size_t phy_tensor_flat_of(const phy_tensor *tensor, const unsigned *indices)
{
    size_t flat = 0u;
    for (unsigned slot = 0u; slot < tensor->rank; ++slot) {
        flat = flat * (size_t)tensor->dimension + (size_t)indices[slot];
    }
    return flat;
}

void phy_tensor_indices_of(const phy_tensor *tensor, size_t flat,
                           unsigned *out_indices)
{
    const size_t dimension = (size_t)tensor->dimension;
    unsigned slot = tensor->rank;
    while (slot > 0u) {
        --slot;
        out_indices[slot] = (unsigned)(flat % dimension);
        flat /= dimension;
    }
}

phy_status phy_tensor_check_indices(const phy_tensor *tensor,
                                    const unsigned *indices)
{
    if (tensor->rank == 0u) {
        return PHY_OK; /* the single component is named by no indices */
    }
    if (indices == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (unsigned slot = 0u; slot < tensor->rank; ++slot) {
        if (indices[slot] >= tensor->dimension) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
    }
    return PHY_OK;
}

/* Index of a permutation already in the group, or the group order if absent. */
static size_t find_perm(const phy_tensor *tensor, const uint8_t *image)
{
    for (size_t entry = 0u; entry < tensor->group_order; ++entry) {
        if (memcmp(tensor->group[entry].image, image, tensor->rank) == 0) {
            return entry;
        }
    }
    return tensor->group_order;
}

static phy_status close_group(phy_tensor *tensor)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t left = 0u; left < tensor->group_order; ++left) {
            for (size_t right = 0u; right < tensor->group_order; ++right) {
                uint8_t image[PHY_TENSOR_MAX_RANK];
                for (unsigned slot = 0u; slot < tensor->rank; ++slot) {
                    image[slot] =
                        tensor->group[left].image[tensor->group[right].image[slot]];
                }
                const int sign =
                    tensor->group[left].sign * tensor->group[right].sign;

                const size_t found = find_perm(tensor, image);
                if (found == tensor->group_order) {
                    if (tensor->group_order >= PHY_TENSOR_MAX_SYMMETRIES) {
                        /* Unreachable at rank <= 4; a guard, not a policy. */
                        return PHY_ERR_UNSUPPORTED;
                    }
                    memcpy(tensor->group[tensor->group_order].image, image,
                           tensor->rank);
                    tensor->group[tensor->group_order].sign = (int8_t)sign;
                    tensor->group_order++;
                    changed = true;
                } else if (tensor->group[found].sign != (int8_t)sign) {
                    /*
                     * One permutation with both signs. Composing it with the
                     * inverse of the other reaches the identity at sign -1,
                     * so every component is forced to vanish. Consistent, so
                     * recorded rather than rejected.
                     */
                    tensor->identically_zero = true;
                }
            }
        }
    }
    return PHY_OK;
}

phy_status phy_tensor_add_generator(phy_tensor *tensor, const uint8_t *image,
                                    int sign)
{
    /*
     * Snapshot first. A declaration that cannot be honoured must leave the
     * tensor exactly as it was, and the closure below mutates the group in
     * place as it discovers products.
     */
    phy_tensor_perm saved_group[PHY_TENSOR_MAX_SYMMETRIES];
    const size_t saved_order = tensor->group_order;
    const bool saved_zero = tensor->identically_zero;
    memcpy(saved_group, tensor->group, sizeof(saved_group));

    const size_t found = find_perm(tensor, image);
    if (found == tensor->group_order) {
        if (tensor->group_order >= PHY_TENSOR_MAX_SYMMETRIES) {
            return PHY_ERR_UNSUPPORTED;
        }
        memcpy(tensor->group[tensor->group_order].image, image, tensor->rank);
        tensor->group[tensor->group_order].sign = (int8_t)sign;
        tensor->group_order++;
    } else if (tensor->group[found].sign != (int8_t)sign) {
        /* The same permutation declared with both signs. */
        tensor->identically_zero = true;
    }

    const phy_status status = close_group(tensor);
    if (status != PHY_OK) {
        memcpy(tensor->group, saved_group, sizeof(saved_group));
        tensor->group_order = saved_order;
        tensor->identically_zero = saved_zero;
        return status;
    }
    return PHY_OK;
}

void phy_tensor_canonical_flat(const phy_tensor *tensor, size_t flat,
                               size_t *out_representative, int *out_sign)
{
    unsigned indices[PHY_TENSOR_MAX_RANK];
    unsigned permuted[PHY_TENSOR_MAX_RANK];

    phy_tensor_indices_of(tensor, flat, indices);

    size_t best = flat;
    int best_sign = 1;
    bool forced_zero = tensor->identically_zero;

    for (size_t entry = 0u; entry < tensor->group_order; ++entry) {
        phy_tensor_permute(&tensor->group[entry], indices, tensor->rank,
                           permuted);
        const size_t candidate = phy_tensor_flat_of(tensor, permuted);

        if (candidate == flat && tensor->group[entry].sign < 0) {
            /* An odd element of the stabilizer: T[a] = -T[a]. */
            forced_zero = true;
        }
        if (candidate < best) {
            best = candidate;
            best_sign = tensor->group[entry].sign;
        }
    }

    if (out_representative != NULL) {
        *out_representative = best;
    }
    if (out_sign != NULL) {
        /*
         * T[rep] = sign(g) * T[flat] for the g that reaches the
         * representative, and a sign is its own inverse, so
         * T[flat] = sign(g) * T[rep].
         */
        *out_sign = forced_zero ? 0 : best_sign;
    }
}

void phy_tensor_reset_tables(phy_tensor *tensor)
{
    for (size_t flat = 0u; flat < tensor->count; ++flat) {
        int sign = 0;
        phy_tensor_canonical_flat(tensor, flat, NULL, &sign);

        tensor->values[flat] = tensor->zero;
        /*
         * An unassigned component holds the canonical zero handle, so +1 and
         * -1 would describe it equally well; +1 keeps the table uniform. A
         * forced-vanishing component keeps sign 0, because that is a property
         * of the declared group rather than of any assignment.
         */
        tensor->signs[flat] = (sign == 0) ? (int8_t)0 : (int8_t)1;
        tensor->assigned[flat] = 0u;
    }
    tensor->has_assignment = false;
}
