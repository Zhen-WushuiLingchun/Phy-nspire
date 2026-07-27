/*
 * Phy-nspire — exterior calculus.
 *
 * The four operations of docs/SCIENTIFIC_SCOPE.md section 3 that need no
 * connection: the wedge product, the exterior derivative, the interior product
 * with a contravariant vector field, and the Hodge dual. Pullback is not here;
 * include/phy/geom.h records what a safe coordinate-map abstraction would have
 * to supply first.
 *
 * All four are exact. Every scalar is built by phy_cas_*, which means the
 * step budget, the cancellation hook, the exact-arithmetic overflow status and
 * the memoization all apply to a curvature loop through this file without it
 * knowing they exist. Nothing here estimates, rounds, or touches a double.
 *
 * All four are also bounded by construction rather than by a budget. Dimension
 * is at most four, so a result has at most six components and each is a sum of
 * at most six terms; the accumulators are automatic arrays of
 * PHY_GEOM_MAX_TERMS and no loop in this file allocates.
 *
 * All four are transactional. The result form is created first and filled
 * afterwards, and any failure destroys it and leaves *out_form untouched and
 * every operand unmodified.
 */
#include <string.h>

#include "geom_internal.h"

/*
 * The increasing complement of `chosen` in 0..n-1.
 *
 * Both the wedge and the Hodge dual need it, and both need it to come out
 * increasing so that it addresses a stored component directly.
 */
static void complement_of(unsigned n, const unsigned *chosen, unsigned count,
                          unsigned *out_rest)
{
    unsigned written = 0u;
    for (unsigned value = 0u; value < n; ++value) {
        bool taken = false;
        for (unsigned slot = 0u; slot < count; ++slot) {
            if (chosen[slot] == value) {
                taken = true;
                break;
            }
        }
        if (!taken) {
            out_rest[written++] = value;
        }
    }
}

/* --------------------------------------------------------- wedge product */

phy_status phy_form_wedge(phy_cas *cas, const phy_form *left,
                          const phy_form *right, phy_form **out_form)
{
    if (out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_pair(left, right);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_geom_check_cas(cas, left->manifold);
    if (status != PHY_OK) {
        return status;
    }

    const unsigned n = left->dimension;
    const unsigned p = left->degree;
    const unsigned q = right->degree;
    const unsigned total = p + q;

    /*
     * The product vanishes identically above the dimension. It is not
     * undefined, and this is not a ceiling that a larger device would lift:
     * Lambda^{p+q} is the zero space, so there is no object to return.
     */
    if (total > n) {
        return PHY_ERR_DOMAIN;
    }

    phy_form *result = NULL;
    status = phy_geom_form_like(left, total, &result);
    if (status != PHY_OK) {
        return status;
    }

    const size_t shuffles = phy_geom_binomial(total, p);

    for (size_t target = 0u; target < result->count; ++target) {
        unsigned indices[PHY_FORM_MAX_DEGREE];
        phy_geom_combination_at(n, total, target, indices);

        phy_ir_ref terms[PHY_GEOM_MAX_TERMS];
        size_t term_count = 0u;

        for (size_t shuffle = 0u; shuffle < shuffles; ++shuffle) {
            /*
             * Choose which p of the target's slots the left factor takes. The
             * chosen slots and their complement are both increasing, so the
             * two index tuples they select are stored components rather than
             * tuples needing a sort.
             */
            unsigned chosen[PHY_FORM_MAX_DEGREE];
            unsigned rest[PHY_FORM_MAX_DEGREE];
            phy_geom_combination_at(total, p, shuffle, chosen);
            complement_of(total, chosen, p, rest);

            unsigned left_indices[PHY_FORM_MAX_DEGREE];
            unsigned right_indices[PHY_FORM_MAX_DEGREE];
            for (unsigned slot = 0u; slot < p; ++slot) {
                left_indices[slot] = indices[chosen[slot]];
            }
            for (unsigned slot = 0u; slot < q; ++slot) {
                right_indices[slot] = indices[rest[slot]];
            }

            size_t left_position = 0u;
            size_t right_position = 0u;
            if (!phy_geom_combination_index(n, p, left_indices,
                                            &left_position) ||
                !phy_geom_combination_index(n, q, right_indices,
                                            &right_position)) {
                phy_form_destroy(result);
                return PHY_ERR_CORRUPT_DOCUMENT;
            }

            const phy_ir_ref left_value = left->components[left_position];
            const phy_ir_ref right_value = right->components[right_position];
            if (left_value == left->zero || right_value == right->zero) {
                continue;
            }

            /*
             * The shuffle sign. Sorting the concatenated *slot positions*
             * gives the same parity as sorting the index values they select,
             * because the target tuple is increasing and the map from slot to
             * value therefore order-preserving.
             */
            unsigned permutation[PHY_FORM_MAX_DEGREE];
            for (unsigned slot = 0u; slot < p; ++slot) {
                permutation[slot] = chosen[slot];
            }
            for (unsigned slot = 0u; slot < q; ++slot) {
                permutation[p + slot] = rest[slot];
            }
            const int sign = phy_geom_sort_parity(permutation, total);

            const phy_ir_ref factors[2] = {left_value, right_value};
            phy_ir_ref product = PHY_IR_NULL;
            status = phy_cas_mul(cas, factors, 2u, &product);
            if (status == PHY_OK) {
                status = phy_geom_apply_sign(cas, product, sign, &product);
            }
            if (status != PHY_OK) {
                phy_form_destroy(result);
                return status;
            }
            terms[term_count++] = product;
        }

        phy_ir_ref value = PHY_IR_NULL;
        status = phy_geom_sum(cas, result, terms, term_count, &value);
        if (status != PHY_OK) {
            phy_form_destroy(result);
            return status;
        }
        result->components[target] = value;
    }

    *out_form = result;
    return PHY_OK;
}

/* ---------------------------------------------------- exterior derivative */

phy_status phy_form_exterior_derivative(phy_cas *cas, const phy_form *form,
                                        phy_form **out_form)
{
    if (form == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_cas(cas, form->manifold);
    if (status != PHY_OK) {
        return status;
    }

    const unsigned n = form->dimension;
    const unsigned p = form->degree;
    if (p >= n) {
        /* d of an n-form vanishes; see the note at phy_form_wedge. */
        return PHY_ERR_DOMAIN;
    }

    phy_form *result = NULL;
    status = phy_geom_form_like(form, p + 1u, &result);
    if (status != PHY_OK) {
        return status;
    }

    for (size_t target = 0u; target < result->count; ++target) {
        unsigned indices[PHY_FORM_MAX_DEGREE];
        phy_geom_combination_at(n, p + 1u, target, indices);

        phy_ir_ref terms[PHY_GEOM_MAX_TERMS];
        size_t term_count = 0u;

        for (unsigned omitted = 0u; omitted <= p; ++omitted) {
            /* The remaining indices stay increasing, so this addresses a
             * stored component with no sort. */
            unsigned remaining[PHY_FORM_MAX_DEGREE];
            unsigned written = 0u;
            for (unsigned slot = 0u; slot <= p; ++slot) {
                if (slot != omitted) {
                    remaining[written++] = indices[slot];
                }
            }

            size_t source = 0u;
            if (!phy_geom_combination_index(n, p, remaining, &source)) {
                phy_form_destroy(result);
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
            const phy_ir_ref component = form->components[source];
            if (component == form->zero) {
                continue;
            }

            const phy_ir_ref coordinate =
                phy_chart_coordinate(form->chart, indices[omitted]);
            if (coordinate == PHY_IR_NULL) {
                phy_form_destroy(result);
                return PHY_ERR_INVALID_ARGUMENT;
            }

            phy_ir_ref derivative = PHY_IR_NULL;
            status = phy_cas_diff(cas, component, coordinate, &derivative);
            if (status != PHY_OK) {
                phy_form_destroy(result);
                return status;
            }
            if (derivative == form->zero) {
                continue;
            }

            status = phy_geom_apply_sign(cas, derivative,
                                         (omitted % 2u == 0u) ? 1 : -1,
                                         &derivative);
            if (status != PHY_OK) {
                phy_form_destroy(result);
                return status;
            }
            terms[term_count++] = derivative;
        }

        phy_ir_ref value = PHY_IR_NULL;
        status = phy_geom_sum(cas, result, terms, term_count, &value);
        if (status != PHY_OK) {
            phy_form_destroy(result);
            return status;
        }
        result->components[target] = value;
    }

    *out_form = result;
    return PHY_OK;
}

/* ------------------------------------------------------ interior product */

phy_status phy_form_interior_product(phy_cas *cas, const phy_form *form,
                                     const phy_tensor *vector,
                                     phy_form **out_form)
{
    if (form == NULL || vector == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_cas(cas, form->manifold);
    if (status != PHY_OK) {
        return status;
    }

    /*
     * A vector field is a rank-1 contravariant tensor, and reusing phy_tensor
     * rather than inventing a second vector type is what keeps its storage,
     * its zero handle and its validation shared. What must be checked is that
     * this one is that, and that it lives on the same chart -- nothing here
     * relates two charts.
     */
    if (phy_tensor_rank(vector) != 1u ||
        phy_tensor_valence(vector, 0u) != PHY_IR_INDEX_UPPER ||
        phy_tensor_chart(vector) != form->chart) {
        return PHY_ERR_TYPE;
    }

    const unsigned n = form->dimension;
    const unsigned p = form->degree;
    if (p == 0u) {
        /* iota_v of a 0-form vanishes; see the note at phy_form_wedge. */
        return PHY_ERR_DOMAIN;
    }

    phy_form *result = NULL;
    status = phy_geom_form_like(form, p - 1u, &result);
    if (status != PHY_OK) {
        return status;
    }

    const phy_ir_ref vector_zero = phy_tensor_zero(vector);

    for (size_t target = 0u; target < result->count; ++target) {
        unsigned rest[PHY_FORM_MAX_DEGREE];
        phy_geom_combination_at(n, p - 1u, target, rest);

        phy_ir_ref terms[PHY_GEOM_MAX_TERMS];
        size_t term_count = 0u;

        for (unsigned axis = 0u; axis < n; ++axis) {
            unsigned indices[PHY_FORM_MAX_DEGREE];
            indices[0] = axis;
            for (unsigned slot = 0u; slot < p - 1u; ++slot) {
                indices[slot + 1u] = rest[slot];
            }

            const int sort_sign = phy_geom_sort_parity(indices, p);
            if (sort_sign == 0) {
                /* The axis already appears in the target tuple. */
                continue;
            }

            size_t source = 0u;
            if (!phy_geom_combination_index(n, p, indices, &source)) {
                phy_form_destroy(result);
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
            const phy_ir_ref component = form->components[source];
            if (component == form->zero) {
                continue;
            }

            phy_tensor_component entry;
            status = phy_tensor_get(vector, &axis, &entry);
            if (status != PHY_OK) {
                phy_form_destroy(result);
                return status;
            }
            if (entry.sign == 0 || entry.ref == vector_zero) {
                continue;
            }

            const phy_ir_ref factors[2] = {entry.ref, component};
            phy_ir_ref product = PHY_IR_NULL;
            status = phy_cas_mul(cas, factors, 2u, &product);
            if (status == PHY_OK) {
                status = phy_geom_apply_sign(cas, product,
                                             sort_sign * entry.sign, &product);
            }
            if (status != PHY_OK) {
                phy_form_destroy(result);
                return status;
            }
            terms[term_count++] = product;
        }

        phy_ir_ref value = PHY_IR_NULL;
        status = phy_geom_sum(cas, result, terms, term_count, &value);
        if (status != PHY_OK) {
            phy_form_destroy(result);
            return status;
        }
        result->components[target] = value;
    }

    *out_form = result;
    return PHY_OK;
}

/* ------------------------------------------------------------ Hodge dual */

/* +1 or -1 for a declared orientation, 0 for an unoriented manifold. */
static int orientation_sign(const phy_manifold *manifold)
{
    switch (phy_manifold_orientation(manifold)) {
    case PHY_ORIENTATION_POSITIVE:
        return 1;
    case PHY_ORIENTATION_NEGATIVE:
        return -1;
    case PHY_ORIENTATION_NONE:
    default:
        return 0;
    }
}

phy_status phy_form_hodge(phy_cas *cas, const phy_form *form,
                          phy_form **out_form)
{
    if (form == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_cas(cas, form->manifold);
    if (status != PHY_OK) {
        return status;
    }

    const int orientation = orientation_sign(form->manifold);
    if (orientation == 0) {
        /*
         * *alpha is not merely unknown on an unoriented manifold; it does not
         * exist. A caller that wants one must declare which volume form it
         * means.
         */
        return PHY_ERR_ASSUMPTION;
    }

    const unsigned n = form->dimension;
    const unsigned p = form->degree;
    const unsigned dual = n - p;

    phy_form *result = NULL;
    status = phy_geom_form_like(form, dual, &result);
    if (status != PHY_OK) {
        return status;
    }

    for (size_t target = 0u; target < result->count; ++target) {
        unsigned dual_indices[PHY_FORM_MAX_DEGREE];
        unsigned source_indices[PHY_FORM_MAX_DEGREE];
        phy_geom_combination_at(n, dual, target, dual_indices);
        complement_of(n, dual_indices, dual, source_indices);

        size_t source = 0u;
        if (!phy_geom_combination_index(n, p, source_indices, &source)) {
            phy_form_destroy(result);
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        const phy_ir_ref component = form->components[source];
        if (component == form->zero) {
            continue;
        }

        /*
         * epsilon_{I J}: the parity of (I, J) read as a permutation of
         * 0..n-1, with epsilon_{0 1 ... n-1} = +1.
         */
        unsigned permutation[PHY_MANIFOLD_MAX_DIM];
        for (unsigned slot = 0u; slot < p; ++slot) {
            permutation[slot] = source_indices[slot];
        }
        for (unsigned slot = 0u; slot < dual; ++slot) {
            permutation[p + slot] = dual_indices[slot];
        }
        int sign = phy_geom_sort_parity(permutation, n);

        /*
         * Raising the source indices. The metric is diagonal, so
         * alpha^{i_1...i_p} = sigma_{i_1} ... sigma_{i_p} alpha_{i_1...i_p}
         * with no sum, and the whole of index raising is a sign. This is the
         * one place the orthonormal-coframe restriction is used.
         */
        for (unsigned slot = 0u; slot < p; ++slot) {
            sign *= phy_manifold_signature(form->manifold,
                                           source_indices[slot]);
        }
        sign *= orientation;

        phy_ir_ref value = PHY_IR_NULL;
        status = phy_geom_apply_sign(cas, component, sign, &value);
        if (status != PHY_OK) {
            phy_form_destroy(result);
            return status;
        }
        result->components[target] = value;
    }

    *out_form = result;
    return PHY_OK;
}

phy_status phy_form_volume(phy_cas *cas, phy_manifold *manifold,
                           unsigned chart_index, phy_form **out_form)
{
    if (manifold == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_cas(cas, manifold);
    if (status != PHY_OK) {
        return status;
    }

    const int orientation = orientation_sign(manifold);
    if (orientation == 0) {
        return PHY_ERR_ASSUMPTION;
    }

    phy_form *result = NULL;
    status = phy_form_create(manifold, chart_index, NULL,
                             phy_manifold_dimension(manifold), &result);
    if (status != PHY_OK) {
        return status;
    }

    /*
     * sqrt(|det g|) is 1 in an orthonormal coframe, so the single component is
     * the orientation itself. This is *1, which the test suite checks rather
     * than assumes.
     */
    phy_ir_ref value = PHY_IR_NULL;
    status = phy_cas_number(cas, (int64_t)orientation, 1, &value);
    if (status == PHY_OK) {
        status = phy_form_set_at(cas, result, 0u, value);
    }
    if (status != PHY_OK) {
        phy_form_destroy(result);
        return status;
    }

    *out_form = result;
    return PHY_OK;
}
