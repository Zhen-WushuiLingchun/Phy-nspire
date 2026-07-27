/*
 * Expression-consuming component tensor operations.
 *
 * The dense storage layer deliberately contains no scalar arithmetic. This
 * file is the narrow bridge to the native CAS: it folds storage signs, forms
 * bounded sums, and publishes an output tensor only after every component
 * succeeds.
 */
#include "tensor_internal.h"

static bool same_chart(const phy_tensor *left, const phy_tensor *right)
{
    return left != NULL && right != NULL && left->chart == right->chart &&
           left->dimension == right->dimension;
}

static phy_status create_like(const phy_tensor *source, const char *name,
                              unsigned rank,
                              const phy_ir_variance *valence,
                              phy_tensor **out_tensor)
{
    if (source == NULL || name == NULL || out_tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_tensor_create(source->chart, name, rank, valence, out_tensor);
}

phy_status phy_tensor_component_expression(
    phy_cas *cas, const phy_tensor *tensor, const unsigned *indices,
    phy_ir_ref *out_expression)
{
    if (cas == NULL || tensor == NULL || out_expression == NULL ||
        phy_cas_ir(cas) != tensor->chart->ir) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_tensor_component component;
    const phy_status status = phy_tensor_get(tensor, indices, &component);
    if (status != PHY_OK) {
        return status;
    }
    if (component.sign == 0) {
        *out_expression = tensor->zero;
        return PHY_OK;
    }
    if (component.sign > 0) {
        *out_expression = component.ref;
        return PHY_OK;
    }
    return phy_cas_neg(cas, component.ref, out_expression);
}

static phy_status component_at(phy_cas *cas, const phy_tensor *tensor,
                               const unsigned *indices, phy_ir_ref *out)
{
    return phy_tensor_component_expression(cas, tensor, indices, out);
}

static phy_status determinant_recursive(phy_cas *cas,
                                        const phy_ir_ref *matrix,
                                        unsigned dimension, phy_ir_ref *out)
{
    if (dimension == 1u) {
        *out = matrix[0];
        return PHY_OK;
    }

    phy_ir_ref sum = PHY_IR_NULL;
    phy_status status = phy_cas_number(cas, 0, 1, &sum);
    for (unsigned column = 0u; column < dimension && status == PHY_OK;
         ++column) {
        phy_ir_ref minor[PHY_TENSOR_MAX_DIM * PHY_TENSOR_MAX_DIM];
        size_t used = 0u;
        for (unsigned row = 1u; row < dimension; ++row) {
            for (unsigned inner_column = 0u; inner_column < dimension;
                 ++inner_column) {
                if (inner_column != column) {
                    minor[used++] =
                        matrix[row * dimension + inner_column];
                }
            }
        }
        phy_ir_ref minor_det = PHY_IR_NULL;
        phy_ir_ref term = PHY_IR_NULL;
        status = determinant_recursive(
            cas, minor, dimension - 1u, &minor_det);
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {matrix[column], minor_det};
            status = phy_cas_mul(cas, factors, 2u, &term);
        }
        if (status == PHY_OK && (column & 1u) != 0u) {
            status = phy_cas_neg(cas, term, &term);
        }
        if (status == PHY_OK) {
            const phy_ir_ref addends[2] = {sum, term};
            status = phy_cas_add(cas, addends, 2u, &sum);
        }
    }
    if (status == PHY_OK) {
        *out = sum;
    }
    return status;
}

phy_status phy_tensor_determinant(phy_cas *cas, const phy_tensor *matrix,
                                  phy_ir_ref *out_determinant)
{
    if (cas == NULL || matrix == NULL || out_determinant == NULL ||
        phy_cas_ir(cas) != matrix->chart->ir) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (matrix->rank != 2u) {
        return PHY_ERR_TYPE;
    }
    phy_ir_ref entries[PHY_TENSOR_MAX_DIM * PHY_TENSOR_MAX_DIM];
    for (unsigned row = 0u; row < matrix->dimension; ++row) {
        for (unsigned column = 0u; column < matrix->dimension; ++column) {
            const unsigned indices[2] = {row, column};
            const phy_status status = component_at(
                cas, matrix, indices,
                &entries[row * matrix->dimension + column]);
            if (status != PHY_OK) {
                return status;
            }
        }
    }
    return determinant_recursive(
        cas, entries, matrix->dimension, out_determinant);
}

static void build_minor(const phy_ir_ref *matrix, unsigned dimension,
                        unsigned removed_row, unsigned removed_column,
                        phy_ir_ref *minor)
{
    size_t used = 0u;
    for (unsigned row = 0u; row < dimension; ++row) {
        if (row == removed_row) {
            continue;
        }
        for (unsigned column = 0u; column < dimension; ++column) {
            if (column != removed_column) {
                minor[used++] = matrix[row * dimension + column];
            }
        }
    }
}

phy_status phy_tensor_inverse_metric(phy_cas *cas, const phy_tensor *metric,
                                     const char *name,
                                     phy_tensor **out_inverse)
{
    if (cas == NULL || metric == NULL || name == NULL ||
        out_inverse == NULL || phy_cas_ir(cas) != metric->chart->ir) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (metric->rank != 2u ||
        metric->valence[0] != PHY_IR_INDEX_LOWER ||
        metric->valence[1] != PHY_IR_INDEX_LOWER) {
        return PHY_ERR_TYPE;
    }

    const unsigned dimension = metric->dimension;
    phy_ir_ref matrix[PHY_TENSOR_MAX_DIM * PHY_TENSOR_MAX_DIM];
    for (unsigned row = 0u; row < dimension; ++row) {
        for (unsigned column = 0u; column < dimension; ++column) {
            const unsigned indices[2] = {row, column};
            phy_status status =
                component_at(cas, metric, indices,
                             &matrix[row * dimension + column]);
            if (status != PHY_OK) {
                return status;
            }
        }
    }
    for (unsigned row = 0u; row < dimension; ++row) {
        for (unsigned column = row + 1u; column < dimension; ++column) {
            phy_cas_decision equal = PHY_CAS_UNKNOWN;
            const phy_status status = phy_cas_equivalent(
                cas, matrix[row * dimension + column],
                matrix[column * dimension + row], &equal);
            if (status != PHY_OK) {
                return status;
            }
            if (equal != PHY_CAS_ZERO) {
                return PHY_ERR_ASSUMPTION;
            }
        }
    }

    phy_ir_ref det = PHY_IR_NULL;
    phy_status status =
        determinant_recursive(cas, matrix, dimension, &det);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_decision zero = PHY_CAS_UNKNOWN;
    status = phy_cas_is_zero(cas, det, &zero);
    if (status != PHY_OK) {
        return status;
    }
    if (zero == PHY_CAS_ZERO) {
        return PHY_ERR_DOMAIN;
    }

    static const phy_ir_variance kUpper[2] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_UPPER};
    phy_tensor *inverse = NULL;
    status = create_like(metric, name, 2u, kUpper, &inverse);
    if (status != PHY_OK) {
        return status;
    }

    for (unsigned row = 0u; row < dimension && status == PHY_OK; ++row) {
        for (unsigned column = 0u;
             column < dimension && status == PHY_OK; ++column) {
            phy_ir_ref cofactor;
            if (dimension == 1u) {
                status = phy_cas_number(cas, 1, 1, &cofactor);
            } else {
                phy_ir_ref minor[PHY_TENSOR_MAX_DIM * PHY_TENSOR_MAX_DIM];
                /* adj(g)[row,column] = cofactor[column,row]. */
                build_minor(matrix, dimension, column, row, minor);
                status = determinant_recursive(
                    cas, minor, dimension - 1u, &cofactor);
                if (status == PHY_OK && ((row + column) & 1u) != 0u) {
                    status = phy_cas_neg(cas, cofactor, &cofactor);
                }
            }
            phy_ir_ref value = PHY_IR_NULL;
            if (status == PHY_OK) {
                status = phy_cas_div(cas, cofactor, det, &value);
            }
            if (status == PHY_OK) {
                const unsigned indices[2] = {row, column};
                status = phy_tensor_set(inverse, indices, value);
            }
        }
    }
    if (status != PHY_OK) {
        phy_tensor_destroy(inverse);
        return status;
    }
    *out_inverse = inverse;
    return PHY_OK;
}

static phy_status change_slot(phy_cas *cas, const phy_tensor *tensor,
                              unsigned slot, const phy_tensor *metric,
                              phy_ir_variance expected_input,
                              phy_ir_variance metric_variance,
                              const char *name, phy_tensor **out_tensor)
{
    if (cas == NULL || tensor == NULL || metric == NULL || name == NULL ||
        out_tensor == NULL || slot >= tensor->rank ||
        phy_cas_ir(cas) != tensor->chart->ir || !same_chart(tensor, metric)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (metric->rank != 2u || tensor->valence[slot] != expected_input ||
        metric->valence[0] != metric_variance ||
        metric->valence[1] != metric_variance) {
        return PHY_ERR_TYPE;
    }

    phy_ir_variance valence[PHY_TENSOR_MAX_RANK];
    for (unsigned index = 0u; index < tensor->rank; ++index) {
        valence[index] = tensor->valence[index];
    }
    valence[slot] = metric_variance;

    phy_tensor *result = NULL;
    phy_status status =
        create_like(tensor, name, tensor->rank, valence, &result);
    if (status != PHY_OK) {
        return status;
    }

    unsigned output_indices[PHY_TENSOR_MAX_RANK] = {0u};
    unsigned input_indices[PHY_TENSOR_MAX_RANK] = {0u};
    unsigned metric_indices[2] = {0u};
    for (size_t flat = 0u; flat < result->count && status == PHY_OK; ++flat) {
        phy_tensor_indices_of(result, flat, output_indices);
        for (unsigned index = 0u; index < tensor->rank; ++index) {
            input_indices[index] = output_indices[index];
        }
        metric_indices[0] = output_indices[slot];

        phy_ir_ref sum = PHY_IR_NULL;
        status = phy_cas_number(cas, 0, 1, &sum);
        for (unsigned contracted = 0u;
             contracted < tensor->dimension && status == PHY_OK;
             ++contracted) {
            input_indices[slot] = contracted;
            metric_indices[1] = contracted;
            phy_ir_ref scalar = PHY_IR_NULL;
            phy_ir_ref metric_value = PHY_IR_NULL;
            phy_ir_ref term = PHY_IR_NULL;
            status =
                component_at(cas, tensor, input_indices, &scalar);
            if (status == PHY_OK) {
                status =
                    component_at(cas, metric, metric_indices, &metric_value);
            }
            if (status == PHY_OK) {
                const phy_ir_ref factors[2] = {metric_value, scalar};
                status = phy_cas_mul(cas, factors, 2u, &term);
            }
            if (status == PHY_OK) {
                const phy_ir_ref addends[2] = {sum, term};
                status = phy_cas_add(cas, addends, 2u, &sum);
            }
        }
        if (status == PHY_OK) {
            status = phy_tensor_set(result, output_indices, sum);
        }
    }
    if (status != PHY_OK) {
        phy_tensor_destroy(result);
        return status;
    }
    *out_tensor = result;
    return PHY_OK;
}

phy_status phy_tensor_raise_slot(phy_cas *cas, const phy_tensor *tensor,
                                 unsigned slot,
                                 const phy_tensor *inverse_metric,
                                 const char *name, phy_tensor **out_tensor)
{
    return change_slot(cas, tensor, slot, inverse_metric,
                       PHY_IR_INDEX_LOWER, PHY_IR_INDEX_UPPER, name,
                       out_tensor);
}

phy_status phy_tensor_lower_slot(phy_cas *cas, const phy_tensor *tensor,
                                 unsigned slot, const phy_tensor *metric,
                                 const char *name, phy_tensor **out_tensor)
{
    return change_slot(cas, tensor, slot, metric, PHY_IR_INDEX_UPPER,
                       PHY_IR_INDEX_LOWER, name, out_tensor);
}

phy_status phy_tensor_contract(phy_cas *cas, const phy_tensor *tensor,
                               unsigned slot_a, unsigned slot_b,
                               const char *name, phy_tensor **out_tensor)
{
    if (cas == NULL || tensor == NULL || name == NULL || out_tensor == NULL ||
        phy_cas_ir(cas) != tensor->chart->ir || slot_a >= tensor->rank ||
        slot_b >= tensor->rank || slot_a == slot_b) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (tensor->valence[slot_a] == tensor->valence[slot_b]) {
        return PHY_ERR_TYPE;
    }

    phy_ir_variance valence[PHY_TENSOR_MAX_RANK];
    unsigned used = 0u;
    for (unsigned slot = 0u; slot < tensor->rank; ++slot) {
        if (slot != slot_a && slot != slot_b) {
            valence[used++] = tensor->valence[slot];
        }
    }
    phy_tensor *result = NULL;
    phy_status status =
        create_like(tensor, name, tensor->rank - 2u, valence, &result);
    if (status != PHY_OK) {
        return status;
    }

    unsigned output_indices[PHY_TENSOR_MAX_RANK] = {0u};
    unsigned input_indices[PHY_TENSOR_MAX_RANK] = {0u};
    for (size_t flat = 0u; flat < result->count && status == PHY_OK; ++flat) {
        phy_tensor_indices_of(result, flat, output_indices);
        phy_ir_ref sum = PHY_IR_NULL;
        status = phy_cas_number(cas, 0, 1, &sum);
        for (unsigned contracted = 0u;
             contracted < tensor->dimension && status == PHY_OK;
             ++contracted) {
            unsigned next = 0u;
            for (unsigned slot = 0u; slot < tensor->rank; ++slot) {
                input_indices[slot] =
                    (slot == slot_a || slot == slot_b)
                        ? contracted
                        : output_indices[next++];
            }
            phy_ir_ref value = PHY_IR_NULL;
            status = component_at(cas, tensor, input_indices, &value);
            if (status == PHY_OK) {
                const phy_ir_ref addends[2] = {sum, value};
                status = phy_cas_add(cas, addends, 2u, &sum);
            }
        }
        if (status == PHY_OK) {
            status = phy_tensor_set(result, output_indices, sum);
        }
    }
    if (status != PHY_OK) {
        phy_tensor_destroy(result);
        return status;
    }
    *out_tensor = result;
    return PHY_OK;
}

phy_status phy_tensor_partial(phy_cas *cas, const phy_tensor *tensor,
                              unsigned coordinate_axis, const char *name,
                              phy_tensor **out_tensor)
{
    if (cas == NULL || tensor == NULL || name == NULL || out_tensor == NULL ||
        phy_cas_ir(cas) != tensor->chart->ir ||
        coordinate_axis >= tensor->dimension) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_tensor *result = NULL;
    phy_status status = create_like(tensor, name, tensor->rank,
                                    tensor->valence, &result);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref coordinate =
        phy_chart_coordinate(tensor->chart, coordinate_axis);
    unsigned indices[PHY_TENSOR_MAX_RANK] = {0u};
    for (size_t flat = 0u; flat < tensor->count && status == PHY_OK; ++flat) {
        phy_tensor_indices_of(tensor, flat, indices);
        phy_ir_ref value = PHY_IR_NULL;
        phy_ir_ref derivative = PHY_IR_NULL;
        status = component_at(cas, tensor, indices, &value);
        if (status == PHY_OK) {
            status = phy_cas_diff(cas, value, coordinate, &derivative);
        }
        if (status == PHY_OK) {
            status = phy_tensor_set(result, indices, derivative);
        }
    }
    if (status != PHY_OK) {
        phy_tensor_destroy(result);
        return status;
    }
    *out_tensor = result;
    return PHY_OK;
}
