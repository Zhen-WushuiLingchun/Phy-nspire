/*
 * Phy-nspire — component tensor internals.
 *
 * Shared by the src/tensor translation units only. The public contract is
 * include/phy/tensor.h; nothing outside src/tensor may include this.
 */
#ifndef PHY_TENSOR_INTERNAL_H
#define PHY_TENSOR_INTERNAL_H

#include "phy/platform.h"
#include "phy/tensor.h"

/*
 * A signed slot permutation, in images notation: image[i] is the slot that
 * slot i maps to, matching docs/references/TENSOR_GEOMETRY.md.
 *
 * Five bytes, and the whole group is at most 24 of them, so a tensor carries
 * its symmetry group inline and declaring one allocates nothing.
 */
typedef struct {
    uint8_t image[PHY_TENSOR_MAX_RANK];
    int8_t sign; /* +1 or -1 */
} phy_tensor_perm;

struct phy_chart {
    phy_ir_context *ir;
    unsigned dimension;
    phy_ir_symbol symbols[PHY_TENSOR_MAX_DIM];
    phy_ir_ref refs[PHY_TENSOR_MAX_DIM];
};

struct phy_tensor {
    phy_chart *chart;
    phy_ir_symbol head;
    unsigned rank;
    unsigned dimension;
    size_t count; /* dimension^rank; 1 at rank 0 */

    phy_ir_variance valence[PHY_TENSOR_MAX_RANK];

    /*
     * Every vanishing component shares this handle, so a mostly-zero dense
     * table costs one interned node rather than count of them.
     */
    phy_ir_ref zero;

    phy_tensor_perm group[PHY_TENSOR_MAX_SYMMETRIES];
    size_t group_order;
    bool identically_zero; /* the closure contains identity with sign -1 */
    bool has_assignment;   /* gates declaring symmetries after the fact */

    /*
     * One allocation, carved into three dense count-length tables. Sized from
     * the chart and the rank at construction and never grown, which is what
     * lets every component loop in this layer be allocation-free.
     *
     * The component at flat index i is `signs[i] * values[i]`. A sign of 0
     * marks a component the declared symmetries force to vanish.
     */
    phy_ir_ref *values;
    int8_t *signs;
    uint8_t *assigned;
    void *table;
    size_t table_bytes;
};

/* ---- symmetry.c ---- */

/* out[image[i]] = indices[i]. `out` must not alias `indices`. */
void phy_tensor_permute(const phy_tensor_perm *perm, const unsigned *indices,
                        unsigned rank, unsigned *out_indices);

/*
 * Add a generator and re-close the group.
 *
 * Sets identically_zero when a permutation is reached carrying both signs.
 * Returns PHY_ERR_UNSUPPORTED if the closure would exceed
 * PHY_TENSOR_MAX_SYMMETRIES, which rank <= 4 makes unreachable and which is
 * therefore a guard rather than a policy.
 *
 * The group is restored exactly on failure, so a rejected declaration leaves
 * no trace. The caller is responsible for resetting the component tables
 * afterwards, since a new generator can change which components vanish.
 */
phy_status phy_tensor_add_generator(phy_tensor *tensor, const uint8_t *image,
                                    int sign);

/*
 * Reset every component to the canonical zero handle, mark the whole table
 * unassigned, and recompute the per-component signs from the current group:
 * 0 for a component the symmetries force to vanish, +1 otherwise.
 */
void phy_tensor_reset_tables(phy_tensor *tensor);

/* Flat index of a validated index tuple, and its inverse. */
size_t phy_tensor_flat_of(const phy_tensor *tensor, const unsigned *indices);
void phy_tensor_indices_of(const phy_tensor *tensor, size_t flat,
                           unsigned *out_indices);

/*
 * Orbit representative of `flat` and the sign relating them:
 * component[flat] == sign * component[representative].
 *
 * `out_sign` receives 0 when the component is forced to vanish, which happens
 * when the stabilizer of the index tuple contains an odd permutation.
 */
void phy_tensor_canonical_flat(const phy_tensor *tensor, size_t flat,
                               size_t *out_representative, int *out_sign);

/* Shared validation, so every entry point rejects the same things. */
phy_status phy_tensor_check_indices(const phy_tensor *tensor,
                                    const unsigned *indices);

#endif /* PHY_TENSOR_INTERNAL_H */
