/*
 * Phy-nspire — manifold and differential-form internals.
 *
 * Shared by the src/geom translation units only. The public contract is
 * include/phy/geom.h; nothing outside src/geom may include this.
 */
#ifndef PHY_GEOM_INTERNAL_H
#define PHY_GEOM_INTERNAL_H

#include "phy/geom.h"
#include "phy/platform.h"

/*
 * The largest number of scalar terms any operation in this layer sums into
 * one component.
 *
 *   wedge               C(p+q, p) <= C(4,2) = 6 shuffles;
 *   exterior derivative p + 1     <= 4 omitted slots;
 *   interior product    n         <= 4 contracted axes.
 *
 * So six, and the bound is structural rather than a budget: at dimension four
 * no loop here can produce a seventh term. That is what lets every operation
 * accumulate into a fixed automatic array and allocate nothing per component.
 */
#define PHY_GEOM_MAX_TERMS 6u

struct phy_manifold {
    phy_ir_context *ir;
    phy_ir_symbol head;
    unsigned dimension;
    phy_orientation orientation;

    /* Diagonal metric entries, +1 or -1, in the coordinate coframe. */
    int8_t signature[PHY_MANIFOLD_MAX_DIM];
    unsigned negative_count; /* q in the inertia (p, q) */

    /* Borrowed, never owned, and never related to one another. */
    phy_chart *charts[PHY_MANIFOLD_MAX_CHARTS];
    unsigned chart_count;

    phy_ir_ref zero;
};

struct phy_form {
    phy_manifold *manifold;
    phy_chart *chart;
    unsigned chart_index;

    phy_ir_symbol head; /* PHY_IR_NO_SYMBOL when unnamed */
    unsigned degree;
    unsigned dimension;
    size_t count; /* C(dimension, degree) */

    phy_ir_ref zero;

    /*
     * Strictly increasing components in lexicographic order. Inline, because
     * max_p C(4, p) = 6 and a fixed six-handle table is smaller than the
     * bookkeeping a separate allocation would need.
     */
    phy_ir_ref components[PHY_FORM_MAX_COMPONENTS];
};

/* ---- form.c: combinatorics over strictly increasing index tuples ---- */

/* C(n, k); 0 when k > n. Exact in size_t for every n <= 4. */
size_t phy_geom_binomial(unsigned n, unsigned k);

/*
 * The `position`-th strictly increasing k-tuple from 0..n-1 in lexicographic
 * order. `position` must be < C(n, k); k of 0 writes nothing.
 */
void phy_geom_combination_at(unsigned n, unsigned k, size_t position,
                             unsigned *out_indices);

/*
 * Inverse of the above for an already sorted, strictly increasing tuple.
 * Returns false when the tuple is not one, which callers treat as a bug rather
 * than as input: every tuple reaching here has been through
 * phy_geom_sort_parity.
 */
bool phy_geom_combination_index(unsigned n, unsigned k,
                                const unsigned *sorted_indices,
                                size_t *out_position);

/*
 * Sort `values` ascending in place and return the parity of the permutation
 * that did it: +1 for even, -1 for odd, and 0 when a value repeats.
 *
 * Zero is the antisymmetry statement, not an error: alpha_{aab} vanishes, and
 * so does the shuffle sign of a tuple that is not a shuffle. Insertion sort,
 * because count <= 4 and the swap count is the parity.
 */
int phy_geom_sort_parity(unsigned *values, unsigned count);

/* ---- form.c: shared validation, so every entry point rejects alike ---- */

/* NULL, or a CAS over another IR context. */
phy_status phy_geom_check_cas(const phy_cas *cas, const phy_manifold *manifold);

/* Two forms must name the same manifold and the same chart. */
phy_status phy_geom_check_pair(const phy_form *left, const phy_form *right);

/*
 * Create an unnamed result form of `degree` on the same manifold and chart as
 * `like`. Every operation builds its result through this, so no operation
 * needs to know how a form is allocated.
 */
phy_status phy_geom_form_like(const phy_form *like, unsigned degree,
                              phy_form **out_form);

/*
 * Sum `count` terms into `*out_ref`, or the canonical zero when count is 0.
 * `count` must be <= PHY_GEOM_MAX_TERMS.
 */
phy_status phy_geom_sum(phy_cas *cas, const phy_form *form,
                        const phy_ir_ref *terms, size_t count,
                        phy_ir_ref *out_ref);

/* `value` negated when `sign` is negative, returned unchanged otherwise. */
phy_status phy_geom_apply_sign(phy_cas *cas, phy_ir_ref value, int sign,
                               phy_ir_ref *out_ref);

#endif /* PHY_GEOM_INTERNAL_H */
