/*
 * Phy-nspire — component tensor storage.
 *
 * Dense n^rank storage, as docs/agent-tasks/TENSOR_CORE.md requires: at most
 * 256 components, one allocation, sized at construction and never grown. The
 * kilobyte a packed symmetry-aware layout would have saved is deliberately
 * spent here, so symmetry is a fill-and-check discipline over a flat table
 * rather than an index-mapping layer.
 *
 * Nothing in this file forms an expression. Components move around a symmetry
 * orbit as a shared handle plus a sign, never as a negated value, because
 * negation is a scalar operation and no scalar layer has landed. See the
 * boundary note in include/phy/tensor.h.
 */
#include <string.h>

#include "tensor_internal.h"

static size_t component_count(unsigned dimension, unsigned rank)
{
    size_t count = 1u;
    for (unsigned slot = 0u; slot < rank; ++slot) {
        count *= (size_t)dimension;
    }
    return count;
}

/* --------------------------------------------------------------- lifecycle */

phy_status phy_tensor_create(phy_chart *chart, const char *name, unsigned rank,
                             const phy_ir_variance *valence,
                             phy_tensor **out_tensor)
{
    if (chart == NULL || out_tensor == NULL || name == NULL ||
        name[0] == '\0') {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (rank > PHY_TENSOR_MAX_RANK) {
        return PHY_ERR_UNSUPPORTED;
    }
    if (rank > 0u && valence == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (unsigned slot = 0u; slot < rank; ++slot) {
        if (valence[slot] != PHY_IR_INDEX_LOWER &&
            valence[slot] != PHY_IR_INDEX_UPPER) {
            return PHY_ERR_TYPE;
        }
    }

    /*
     * The IR work first: it allocates inside the context rather than here, so
     * failing at this point leaves nothing of ours to unwind.
     */
    const phy_ir_symbol head = phy_ir_intern(chart->ir, name);
    if (head == PHY_IR_NO_SYMBOL) {
        const phy_status status = phy_ir_last_error(chart->ir);
        return status == PHY_OK ? PHY_ERR_OUT_OF_MEMORY : status;
    }
    const phy_ir_ref zero = phy_ir_integer(chart->ir, 0);
    if (zero == PHY_IR_NULL) {
        const phy_status status = phy_ir_last_error(chart->ir);
        return status == PHY_OK ? PHY_ERR_OUT_OF_MEMORY : status;
    }

    phy_tensor *tensor = phy_alloc(sizeof(*tensor));
    if (tensor == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(tensor, 0, sizeof(*tensor));

    tensor->chart = chart;
    tensor->head = head;
    tensor->rank = rank;
    tensor->dimension = chart->dimension;
    tensor->count = component_count(chart->dimension, rank);
    tensor->zero = zero;
    for (unsigned slot = 0u; slot < rank; ++slot) {
        tensor->valence[slot] = valence[slot];
    }

    /* The identity, always present, is what makes the group non-empty. */
    for (unsigned slot = 0u; slot < rank; ++slot) {
        tensor->group[0].image[slot] = (uint8_t)slot;
    }
    tensor->group[0].sign = 1;
    tensor->group_order = 1u;

    tensor->table_bytes = tensor->count * (sizeof(phy_ir_ref) + 2u);
    tensor->table = phy_alloc(tensor->table_bytes);
    if (tensor->table == NULL) {
        phy_free(tensor, sizeof(*tensor));
        return PHY_ERR_OUT_OF_MEMORY;
    }

    unsigned char *bytes = tensor->table;
    tensor->values = (phy_ir_ref *)(void *)bytes;
    tensor->signs = (int8_t *)(bytes + tensor->count * sizeof(phy_ir_ref));
    tensor->assigned =
        (uint8_t *)(bytes + tensor->count * (sizeof(phy_ir_ref) + 1u));

    phy_tensor_reset_tables(tensor);

    *out_tensor = tensor;
    return PHY_OK;
}

void phy_tensor_destroy(phy_tensor *tensor)
{
    if (tensor == NULL) {
        return;
    }
    phy_free(tensor->table, tensor->table_bytes);
    phy_free(tensor, sizeof(*tensor));
}

/* -------------------------------------------------------------- accessors */

const phy_chart *phy_tensor_chart(const phy_tensor *tensor)
{
    return tensor == NULL ? NULL : tensor->chart;
}

phy_ir_symbol phy_tensor_head(const phy_tensor *tensor)
{
    return tensor == NULL ? PHY_IR_NO_SYMBOL : tensor->head;
}

const char *phy_tensor_name(const phy_tensor *tensor)
{
    if (tensor == NULL) {
        return NULL;
    }
    return phy_ir_symbol_name(tensor->chart->ir, tensor->head);
}

unsigned phy_tensor_rank(const phy_tensor *tensor)
{
    return tensor == NULL ? 0u : tensor->rank;
}

unsigned phy_tensor_dimension(const phy_tensor *tensor)
{
    return tensor == NULL ? 0u : tensor->dimension;
}

size_t phy_tensor_component_count(const phy_tensor *tensor)
{
    return tensor == NULL ? 0u : tensor->count;
}

phy_ir_variance phy_tensor_valence(const phy_tensor *tensor, unsigned slot)
{
    if (tensor == NULL || slot >= tensor->rank) {
        return PHY_IR_INDEX_LOWER;
    }
    return tensor->valence[slot];
}

phy_ir_ref phy_tensor_zero(const phy_tensor *tensor)
{
    return tensor == NULL ? PHY_IR_NULL : tensor->zero;
}

/* ------------------------------------------------------- flat index mapping */

phy_status phy_tensor_flatten(const phy_tensor *tensor,
                              const unsigned *indices, size_t *out_flat)
{
    if (tensor == NULL || out_flat == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_status status = phy_tensor_check_indices(tensor, indices);
    if (status != PHY_OK) {
        return status;
    }
    *out_flat = phy_tensor_flat_of(tensor, indices);
    return PHY_OK;
}

phy_status phy_tensor_unflatten(const phy_tensor *tensor, size_t flat,
                                unsigned *out_indices)
{
    if (tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (flat >= tensor->count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (tensor->rank > 0u && out_indices == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (tensor->rank > 0u) {
        phy_tensor_indices_of(tensor, flat, out_indices);
    }
    return PHY_OK;
}

/* -------------------------------------------------------------- symmetries */

phy_status phy_tensor_declare_permutation(phy_tensor *tensor,
                                          const unsigned *image,
                                          size_t image_count, int sign)
{
    if (tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    /*
     * Declaring after assigning would reinterpret components that were
     * validated against a different group. Refusing is what keeps a fill
     * discipline from quietly becoming a fill bug.
     */
    if (tensor->has_assignment) {
        return PHY_ERR_ASSUMPTION;
    }
    if (image_count != (size_t)tensor->rank) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (sign != 1 && sign != -1) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (tensor->rank > 0u && image == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    bool seen[PHY_TENSOR_MAX_RANK] = {false};
    for (unsigned slot = 0u; slot < tensor->rank; ++slot) {
        if (image[slot] >= tensor->rank || seen[image[slot]]) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
        seen[image[slot]] = true;
    }

    /*
     * A permutation may only exchange slots of equal variance. Swapping an
     * upper slot with a lower one is not a relation between components at
     * all, and declaring it is the way R^a_bcd acquires the symmetries that
     * belong to R_abcd.
     */
    for (unsigned slot = 0u; slot < tensor->rank; ++slot) {
        if (tensor->valence[image[slot]] != tensor->valence[slot]) {
            return PHY_ERR_TYPE;
        }
    }

    uint8_t packed[PHY_TENSOR_MAX_RANK];
    for (unsigned slot = 0u; slot < tensor->rank; ++slot) {
        packed[slot] = (uint8_t)image[slot];
    }

    const phy_status status = phy_tensor_add_generator(tensor, packed, sign);
    if (status != PHY_OK) {
        return status;
    }

    /* A new generator can change which components vanish. */
    phy_tensor_reset_tables(tensor);
    return PHY_OK;
}

phy_status phy_tensor_declare_slot_symmetry(phy_tensor *tensor,
                                            unsigned slot_a, unsigned slot_b,
                                            phy_ir_symmetry symmetry)
{
    if (tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (slot_a == slot_b || slot_a >= tensor->rank || slot_b >= tensor->rank) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    int sign;
    switch (symmetry) {
    case PHY_IR_SYMMETRY_SYMMETRIC:
        sign = 1;
        break;
    case PHY_IR_SYMMETRY_ANTISYMMETRIC:
        sign = -1;
        break;
    case PHY_IR_SYMMETRY_NONE:
    default:
        /* There is no such thing as declaring the absence of a symmetry. */
        return PHY_ERR_INVALID_ARGUMENT;
    }

    unsigned image[PHY_TENSOR_MAX_RANK];
    for (unsigned slot = 0u; slot < tensor->rank; ++slot) {
        image[slot] = slot;
    }
    image[slot_a] = slot_b;
    image[slot_b] = slot_a;

    return phy_tensor_declare_permutation(tensor, image, (size_t)tensor->rank,
                                          sign);
}

phy_status phy_tensor_declare_riemann_symmetry(phy_tensor *tensor)
{
    if (tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (tensor->rank != 4u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    /*
     * Checked here as well as in phy_tensor_declare_permutation, because the
     * restore path below resets the component tables and must never do that
     * to a tensor that already holds assignments.
     */
    if (tensor->has_assignment) {
        return PHY_ERR_ASSUMPTION;
    }

    /*
     * Antisymmetry in each index pair, and exchange of the pairs. The third
     * is not a transposition, which is exactly why the API takes whole
     * permutations rather than only slot pairs.
     */
    static const unsigned kSwapFirst[4] = {1u, 0u, 2u, 3u};
    static const unsigned kSwapSecond[4] = {0u, 1u, 3u, 2u};
    static const unsigned kExchange[4] = {2u, 3u, 0u, 1u};

    /* Restore on failure: three declarations must land as one. */
    phy_tensor_perm saved_group[PHY_TENSOR_MAX_SYMMETRIES];
    const size_t saved_order = tensor->group_order;
    const bool saved_zero = tensor->identically_zero;
    memcpy(saved_group, tensor->group, sizeof(saved_group));

    phy_status status = phy_tensor_declare_permutation(tensor, kSwapFirst, 4u, -1);
    if (status == PHY_OK) {
        status = phy_tensor_declare_permutation(tensor, kSwapSecond, 4u, -1);
    }
    if (status == PHY_OK) {
        status = phy_tensor_declare_permutation(tensor, kExchange, 4u, 1);
    }

    if (status != PHY_OK) {
        memcpy(tensor->group, saved_group, sizeof(saved_group));
        tensor->group_order = saved_order;
        tensor->identically_zero = saved_zero;
        phy_tensor_reset_tables(tensor);
    }
    return status;
}

size_t phy_tensor_symmetry_group_order(const phy_tensor *tensor)
{
    return tensor == NULL ? 0u : tensor->group_order;
}

bool phy_tensor_is_identically_zero(const phy_tensor *tensor)
{
    return tensor == NULL ? false : tensor->identically_zero;
}

/* --------------------------------------------------- canonical components */

phy_status phy_tensor_canonical(const phy_tensor *tensor,
                                const unsigned *indices,
                                unsigned *out_indices, int *out_sign)
{
    if (tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_status status = phy_tensor_check_indices(tensor, indices);
    if (status != PHY_OK) {
        return status;
    }

    size_t representative = 0u;
    int sign = 0;
    phy_tensor_canonical_flat(tensor, phy_tensor_flat_of(tensor, indices),
                              &representative, &sign);

    if (out_indices != NULL && tensor->rank > 0u) {
        phy_tensor_indices_of(tensor, representative, out_indices);
    }
    if (out_sign != NULL) {
        *out_sign = sign;
    }
    return PHY_OK;
}

bool phy_tensor_is_canonical(const phy_tensor *tensor,
                             const unsigned *indices)
{
    if (tensor == NULL ||
        phy_tensor_check_indices(tensor, indices) != PHY_OK) {
        return false;
    }
    const size_t flat = phy_tensor_flat_of(tensor, indices);
    size_t representative = 0u;
    int sign = 0;
    phy_tensor_canonical_flat(tensor, flat, &representative, &sign);
    return representative == flat && sign != 0;
}

size_t phy_tensor_independent_count(const phy_tensor *tensor)
{
    if (tensor == NULL) {
        return 0u;
    }
    size_t independent = 0u;
    for (size_t flat = 0u; flat < tensor->count; ++flat) {
        size_t representative = 0u;
        int sign = 0;
        phy_tensor_canonical_flat(tensor, flat, &representative, &sign);
        if (representative == flat && sign != 0) {
            independent++;
        }
    }
    return independent;
}

/* ---------------------------------------------------- component access */

phy_status phy_tensor_get_flat(const phy_tensor *tensor, size_t flat,
                               phy_tensor_component *out_component)
{
    if (tensor == NULL || out_component == NULL || flat >= tensor->count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    out_component->ref = tensor->values[flat];
    out_component->sign = tensor->signs[flat];
    return PHY_OK;
}

phy_status phy_tensor_get(const phy_tensor *tensor, const unsigned *indices,
                          phy_tensor_component *out_component)
{
    if (tensor == NULL || out_component == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_status status = phy_tensor_check_indices(tensor, indices);
    if (status != PHY_OK) {
        return status;
    }
    return phy_tensor_get_flat(tensor, phy_tensor_flat_of(tensor, indices),
                               out_component);
}

/*
 * Assign one orbit.
 *
 * The declaration T[g . a] = sign(g) * T[a] means every component of the
 * orbit shares the handle just assigned and differs only in the group
 * element's sign, so filling the dependents is a loop over the group rather
 * than any expression work.
 *
 * Validated in full before anything is written, which is what makes a
 * rejected assignment leave the tensor untouched.
 */
static phy_status assign_orbit(phy_tensor *tensor, size_t flat,
                               phy_ir_ref value)
{
    unsigned indices[PHY_TENSOR_MAX_RANK];
    unsigned permuted[PHY_TENSOR_MAX_RANK];

    phy_tensor_indices_of(tensor, flat, indices);

    size_t representative = 0u;
    int sign = 0;
    phy_tensor_canonical_flat(tensor, flat, &representative, &sign);

    if (sign == 0) {
        /*
         * The symmetries force this component to vanish, so the only
         * consistent assignment is zero. Anything else is a contradiction
         * between the value and the declaration, not a value error.
         */
        if (value != tensor->zero) {
            return PHY_ERR_ASSUMPTION;
        }
        for (size_t entry = 0u; entry < tensor->group_order; ++entry) {
            phy_tensor_permute(&tensor->group[entry], indices, tensor->rank,
                               permuted);
            tensor->assigned[phy_tensor_flat_of(tensor, permuted)] = 1u;
        }
        tensor->has_assignment = true;
        return PHY_OK;
    }

    for (size_t entry = 0u; entry < tensor->group_order; ++entry) {
        phy_tensor_permute(&tensor->group[entry], indices, tensor->rank,
                           permuted);
        const size_t target = phy_tensor_flat_of(tensor, permuted);
        if (tensor->assigned[target] != 0u &&
            (tensor->values[target] != value ||
             tensor->signs[target] != tensor->group[entry].sign)) {
            return PHY_ERR_ASSUMPTION;
        }
    }

    for (size_t entry = 0u; entry < tensor->group_order; ++entry) {
        phy_tensor_permute(&tensor->group[entry], indices, tensor->rank,
                           permuted);
        const size_t target = phy_tensor_flat_of(tensor, permuted);
        tensor->values[target] = value;
        tensor->signs[target] = tensor->group[entry].sign;
        tensor->assigned[target] = 1u;
    }

    tensor->has_assignment = true;
    return PHY_OK;
}

phy_status phy_tensor_set_flat(phy_tensor *tensor, size_t flat,
                               phy_ir_ref value)
{
    if (tensor == NULL || flat >= tensor->count || value == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return assign_orbit(tensor, flat, value);
}

phy_status phy_tensor_set(phy_tensor *tensor, const unsigned *indices,
                          phy_ir_ref value)
{
    if (tensor == NULL || value == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_status status = phy_tensor_check_indices(tensor, indices);
    if (status != PHY_OK) {
        return status;
    }
    return assign_orbit(tensor, phy_tensor_flat_of(tensor, indices), value);
}

bool phy_tensor_is_assigned(const phy_tensor *tensor, const unsigned *indices)
{
    if (tensor == NULL ||
        phy_tensor_check_indices(tensor, indices) != PHY_OK) {
        return false;
    }
    return tensor->assigned[phy_tensor_flat_of(tensor, indices)] != 0u;
}

phy_status phy_tensor_clear_component(phy_tensor *tensor,
                                      const unsigned *indices)
{
    if (tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_status status = phy_tensor_check_indices(tensor, indices);
    if (status != PHY_OK) {
        return status;
    }

    unsigned base[PHY_TENSOR_MAX_RANK];
    unsigned permuted[PHY_TENSOR_MAX_RANK];
    phy_tensor_indices_of(tensor, phy_tensor_flat_of(tensor, indices), base);

    for (size_t entry = 0u; entry < tensor->group_order; ++entry) {
        phy_tensor_permute(&tensor->group[entry], base, tensor->rank,
                           permuted);
        const size_t target = phy_tensor_flat_of(tensor, permuted);
        int sign = 0;
        phy_tensor_canonical_flat(tensor, target, NULL, &sign);
        tensor->values[target] = tensor->zero;
        tensor->signs[target] = (sign == 0) ? (int8_t)0 : (int8_t)1;
        tensor->assigned[target] = 0u;
    }

    /* Symmetries may be declared again once nothing is assigned. */
    tensor->has_assignment = false;
    for (size_t flat = 0u; flat < tensor->count; ++flat) {
        if (tensor->assigned[flat] != 0u) {
            tensor->has_assignment = true;
            break;
        }
    }
    return PHY_OK;
}

void phy_tensor_clear(phy_tensor *tensor)
{
    if (tensor == NULL) {
        return;
    }
    phy_tensor_reset_tables(tensor);
}

phy_status phy_tensor_fill_symmetries(phy_tensor *tensor)
{
    if (tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    unsigned base[PHY_TENSOR_MAX_RANK];
    unsigned permuted[PHY_TENSOR_MAX_RANK];

    for (size_t flat = 0u; flat < tensor->count; ++flat) {
        size_t representative = 0u;
        int sign = 0;
        phy_tensor_canonical_flat(tensor, flat, &representative, &sign);
        if (representative != flat || sign == 0 ||
            tensor->assigned[flat] == 0u) {
            continue;
        }

        phy_tensor_indices_of(tensor, flat, base);
        const phy_ir_ref value = tensor->values[flat];
        const int8_t base_sign = tensor->signs[flat];

        for (size_t entry = 0u; entry < tensor->group_order; ++entry) {
            phy_tensor_permute(&tensor->group[entry], base, tensor->rank,
                               permuted);
            const size_t target = phy_tensor_flat_of(tensor, permuted);
            tensor->values[target] = value;
            tensor->signs[target] =
                (int8_t)(tensor->group[entry].sign * base_sign);
            tensor->assigned[target] = 1u;
        }
    }
    return PHY_OK;
}

phy_status phy_tensor_check_symmetries(const phy_tensor *tensor)
{
    if (tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    unsigned indices[PHY_TENSOR_MAX_RANK];
    unsigned permuted[PHY_TENSOR_MAX_RANK];

    for (size_t flat = 0u; flat < tensor->count; ++flat) {
        /* A component marked as vanishing must actually hold zero. */
        if (tensor->signs[flat] == 0 && tensor->values[flat] != tensor->zero) {
            return PHY_ERR_ASSUMPTION;
        }
        if (tensor->identically_zero && tensor->values[flat] != tensor->zero) {
            return PHY_ERR_ASSUMPTION;
        }

        phy_tensor_indices_of(tensor, flat, indices);

        for (size_t entry = 0u; entry < tensor->group_order; ++entry) {
            phy_tensor_permute(&tensor->group[entry], indices, tensor->rank,
                               permuted);
            const size_t target = phy_tensor_flat_of(tensor, permuted);

            /*
             * Both zero is agreement whatever the signs say: an unassigned
             * component carries sign +1 and a forced-vanishing one carries 0,
             * and neither claim contradicts the other when the handle is the
             * canonical zero.
             */
            if (tensor->values[flat] == tensor->zero &&
                tensor->values[target] == tensor->zero) {
                continue;
            }
            if (tensor->values[target] != tensor->values[flat]) {
                return PHY_ERR_ASSUMPTION;
            }
            if (tensor->signs[target] !=
                (int8_t)(tensor->group[entry].sign * tensor->signs[flat])) {
                return PHY_ERR_ASSUMPTION;
            }
        }
    }
    return PHY_OK;
}
