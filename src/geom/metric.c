/*
 * Hodge dual for a general symmetric coordinate metric.
 *
 * This is deliberately separate from exterior.c's orthonormal fast path.  The
 * scalar and tensor layers now provide exact determinant/inversion, so there
 * is no duplicate metric algebra here and no floating-point sign sampling.
 */
#include "geom_internal.h"

static int metric_orientation_sign(const phy_manifold *manifold)
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

static void complement_indices(unsigned dimension, const unsigned *indices,
                               unsigned count, unsigned *out_complement)
{
    unsigned used = 0u;
    unsigned source = 0u;
    for (unsigned axis = 0u; axis < dimension; ++axis) {
        if (source < count && indices[source] == axis) {
            ++source;
        } else {
            out_complement[used++] = axis;
        }
    }
}

static size_t tuple_count(unsigned dimension, unsigned rank)
{
    size_t count = 1u;
    for (unsigned slot = 0u; slot < rank; ++slot) {
        count *= dimension;
    }
    return count;
}

static void tuple_at(unsigned dimension, unsigned rank, size_t flat,
                     unsigned *out_indices)
{
    for (unsigned slot = rank; slot > 0u; --slot) {
        out_indices[slot - 1u] = (unsigned)(flat % dimension);
        flat /= dimension;
    }
}

static phy_status raised_component(
    phy_cas *cas, const phy_form *form, const phy_tensor *inverse,
    const unsigned *upper_indices, phy_ir_ref *out_component)
{
    const unsigned dimension = form->dimension;
    const unsigned degree = form->degree;
    const size_t count = tuple_count(dimension, degree);
    phy_ir_ref sum = form->zero;

    for (size_t flat = 0u; flat < count; ++flat) {
        unsigned lower_indices[PHY_FORM_MAX_DEGREE] = {0u};
        tuple_at(dimension, degree, flat, lower_indices);

        phy_ir_ref component = PHY_IR_NULL;
        phy_status status =
            phy_form_get(cas, form, lower_indices, &component);
        if (status != PHY_OK) {
            return status;
        }
        if (component == form->zero) {
            continue;
        }

        phy_ir_ref term = component;
        for (unsigned slot = 0u; slot < degree; ++slot) {
            const unsigned metric_indices[2] = {
                upper_indices[slot], lower_indices[slot]};
            phy_ir_ref metric_component = PHY_IR_NULL;
            status = phy_tensor_component_expression(
                cas, inverse, metric_indices, &metric_component);
            if (status != PHY_OK) {
                return status;
            }
            const phy_ir_ref factors[2] = {term, metric_component};
            status = phy_cas_mul(cas, factors, 2u, &term);
            if (status != PHY_OK) {
                return status;
            }
        }

        const phy_ir_ref addends[2] = {sum, term};
        status = phy_cas_add(cas, addends, 2u, &sum);
        if (status != PHY_OK) {
            return status;
        }
    }
    *out_component = sum;
    return PHY_OK;
}

phy_status phy_form_hodge_metric(phy_cas *cas, const phy_form *form,
                                 const phy_tensor *metric,
                                 phy_form **out_form)
{
    if (form == NULL || metric == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_cas(cas, form->manifold);
    if (status != PHY_OK) {
        return status;
    }
    if (metric_orientation_sign(form->manifold) == 0) {
        return PHY_ERR_ASSUMPTION;
    }
    if (phy_tensor_chart(metric) != form->chart ||
        phy_tensor_dimension(metric) != form->dimension ||
        phy_tensor_rank(metric) != 2u ||
        phy_tensor_valence(metric, 0u) != PHY_IR_INDEX_LOWER ||
        phy_tensor_valence(metric, 1u) != PHY_IR_INDEX_LOWER) {
        return PHY_ERR_TYPE;
    }

    phy_tensor *inverse = NULL;
    status =
        phy_tensor_inverse_metric(cas, metric, "HodgeInverse", &inverse);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref determinant = PHY_IR_NULL;
    status = phy_tensor_determinant(cas, metric, &determinant);
    phy_ir_ref absolute_determinant = determinant;
    if (status == PHY_OK &&
        phy_manifold_metric_sign(form->manifold) < 0) {
        status = phy_cas_neg(cas, determinant, &absolute_determinant);
    }
    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref root_determinant = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_number(cas, 1, 2, &half);
    }
    if (status == PHY_OK) {
        status = phy_cas_pow(
            cas, absolute_determinant, half, &root_determinant);
    }
    if (status != PHY_OK) {
        phy_tensor_destroy(inverse);
        return status;
    }

    const unsigned dimension = form->dimension;
    const unsigned degree = form->degree;
    const unsigned dual_degree = dimension - degree;
    phy_form *result = NULL;
    status = phy_geom_form_like(form, dual_degree, &result);
    if (status != PHY_OK) {
        phy_tensor_destroy(inverse);
        return status;
    }

    for (size_t position = 0u; position < result->count; ++position) {
        unsigned dual_indices[PHY_FORM_MAX_DEGREE] = {0u};
        unsigned source_indices[PHY_FORM_MAX_DEGREE] = {0u};
        phy_geom_combination_at(
            dimension, dual_degree, position, dual_indices);
        complement_indices(
            dimension, dual_indices, dual_degree, source_indices);

        phy_ir_ref raised = PHY_IR_NULL;
        status = raised_component(
            cas, form, inverse, source_indices, &raised);
        if (status != PHY_OK) {
            break;
        }

        unsigned permutation[PHY_MANIFOLD_MAX_DIM] = {0u};
        for (unsigned slot = 0u; slot < degree; ++slot) {
            permutation[slot] = source_indices[slot];
        }
        for (unsigned slot = 0u; slot < dual_degree; ++slot) {
            permutation[degree + slot] = dual_indices[slot];
        }
        int sign = phy_geom_sort_parity(permutation, dimension);
        sign *= metric_orientation_sign(form->manifold);

        const phy_ir_ref factors[2] = {root_determinant, raised};
        phy_ir_ref value = PHY_IR_NULL;
        status = phy_cas_mul(cas, factors, 2u, &value);
        if (status == PHY_OK) {
            status = phy_geom_apply_sign(cas, value, sign, &value);
        }
        if (status == PHY_OK) {
            status = phy_form_set_at(cas, result, position, value);
        }
        if (status != PHY_OK) {
            break;
        }
    }
    phy_tensor_destroy(inverse);
    if (status != PHY_OK) {
        phy_form_destroy(result);
        return status;
    }
    *out_form = result;
    return PHY_OK;
}

phy_status phy_form_volume_metric(phy_cas *cas, phy_manifold *manifold,
                                  unsigned chart_index,
                                  const phy_tensor *metric,
                                  phy_form **out_form)
{
    if (manifold == NULL || metric == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_form *one = NULL;
    phy_status status =
        phy_form_create(manifold, chart_index, NULL, 0u, &one);
    phy_ir_ref scalar_one = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_number(cas, 1, 1, &scalar_one);
    }
    if (status == PHY_OK) {
        status = phy_form_set_at(cas, one, 0u, scalar_one);
    }
    phy_form *volume = NULL;
    if (status == PHY_OK) {
        status = phy_form_hodge_metric(cas, one, metric, &volume);
    }
    phy_form_destroy(one);
    if (status != PHY_OK) {
        phy_form_destroy(volume);
        return status;
    }
    *out_form = volume;
    return PHY_OK;
}
