#include "linear_internal.h"

#include <string.h>

#define PHY_LINEAR_DEFAULT_MAX_ENTRIES 4096u
#define PHY_LINEAR_DEFAULT_MAX_BYTES (256u * 1024u)
#define PHY_LINEAR_DEFAULT_MAX_STEPS 250000u

void phy_linear_limits_defaults(phy_linear_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_entries = PHY_LINEAR_DEFAULT_MAX_ENTRIES;
    out_limits->max_bytes = PHY_LINEAR_DEFAULT_MAX_BYTES;
    out_limits->max_steps = PHY_LINEAR_DEFAULT_MAX_STEPS;
}

phy_status phy_linear_resolve_limits(const phy_linear_limits *requested,
                                     phy_linear_limits *out)
{
    if (out == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_linear_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_entries != 0u) {
            out->max_entries = requested->max_entries;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
        if (requested->max_steps != 0u) {
            out->max_steps = requested->max_steps;
        }
    }
    if (out->max_entries == 0u || out->max_bytes < sizeof(phy_matrix) ||
        out->max_steps == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

phy_status phy_linear_checked_shape(size_t rows, size_t columns,
                                    const phy_linear_limits *limits,
                                    size_t *out_count,
                                    size_t *out_entries_bytes)
{
    if (rows == 0u || columns == 0u || limits == NULL ||
        out_count == NULL || out_entries_bytes == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (rows > SIZE_MAX / columns) {
        return PHY_ERR_TERM_LIMIT;
    }
    const size_t count = rows * columns;
    if (count > limits->max_entries) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (count > SIZE_MAX / sizeof(phy_ir_ref)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t entries_bytes = count * sizeof(phy_ir_ref);
    if (entries_bytes > limits->max_bytes ||
        sizeof(phy_matrix) > limits->max_bytes - entries_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    *out_count = count;
    *out_entries_bytes = entries_bytes;
    return PHY_OK;
}

phy_status phy_linear_allocate(phy_cas *cas, size_t rows, size_t columns,
                               const phy_linear_limits *limits,
                               phy_matrix **out_matrix)
{
    if (cas == NULL || limits == NULL || out_matrix == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matrix = NULL;
    size_t count = 0u;
    size_t entries_bytes = 0u;
    phy_status status = phy_linear_checked_shape(
        rows, columns, limits, &count, &entries_bytes);
    if (status != PHY_OK) {
        return status;
    }

    phy_matrix *matrix = phy_alloc(sizeof *matrix);
    if (matrix == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(matrix, 0, sizeof *matrix);
    matrix->entries = phy_alloc(entries_bytes);
    if (matrix->entries == NULL) {
        phy_free(matrix, sizeof *matrix);
        return PHY_ERR_MEMORY_LIMIT;
    }
    matrix->cas = cas;
    matrix->rows = rows;
    matrix->columns = columns;
    matrix->count = count;
    matrix->entries_bytes = entries_bytes;
    matrix->limits = *limits;
    *out_matrix = matrix;
    return PHY_OK;
}

phy_status phy_matrix_create(phy_cas *cas, size_t rows, size_t columns,
                             const phy_ir_ref *entries,
                             const phy_linear_limits *limits,
                             phy_matrix **out_matrix)
{
    if (out_matrix == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matrix = NULL;
    phy_linear_limits resolved;
    phy_status status = phy_linear_resolve_limits(limits, &resolved);
    if (status != PHY_OK) {
        return status;
    }
    phy_matrix *matrix = NULL;
    status =
        phy_linear_allocate(cas, rows, columns, &resolved, &matrix);
    if (status != PHY_OK) {
        return status;
    }

    if (entries != NULL) {
        for (size_t i = 0u; i < matrix->count; ++i) {
            if (entries[i] == PHY_IR_NULL) {
                phy_matrix_destroy(matrix);
                return PHY_ERR_INVALID_ARGUMENT;
            }
        }
        memcpy(matrix->entries, entries, matrix->entries_bytes);
    } else {
        phy_ir_ref zero = PHY_IR_NULL;
        status = phy_linear_zero(cas, &zero);
        if (status != PHY_OK) {
            phy_matrix_destroy(matrix);
            return status;
        }
        for (size_t i = 0u; i < matrix->count; ++i) {
            matrix->entries[i] = zero;
        }
    }
    *out_matrix = matrix;
    return PHY_OK;
}

phy_status phy_matrix_clone(const phy_matrix *matrix, phy_matrix **out_matrix)
{
    if (matrix == NULL || out_matrix == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_matrix_create(
        matrix->cas, matrix->rows, matrix->columns, matrix->entries,
        &matrix->limits, out_matrix);
}

void phy_matrix_destroy(phy_matrix *matrix)
{
    if (matrix == NULL) {
        return;
    }
    phy_free(matrix->entries, matrix->entries_bytes);
    phy_free(matrix, sizeof *matrix);
}

phy_cas *phy_matrix_cas(const phy_matrix *matrix)
{
    return matrix != NULL ? matrix->cas : NULL;
}

size_t phy_matrix_rows(const phy_matrix *matrix)
{
    return matrix != NULL ? matrix->rows : 0u;
}

size_t phy_matrix_columns(const phy_matrix *matrix)
{
    return matrix != NULL ? matrix->columns : 0u;
}

size_t phy_matrix_entry_count(const phy_matrix *matrix)
{
    return matrix != NULL ? matrix->count : 0u;
}

phy_status phy_matrix_get(const phy_matrix *matrix, size_t row, size_t column,
                          phy_ir_ref *out_value)
{
    if (matrix == NULL || out_value == NULL || row >= matrix->rows ||
        column >= matrix->columns) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = matrix->entries[row * matrix->columns + column];
    return PHY_OK;
}

phy_status phy_matrix_set(phy_matrix *matrix, size_t row, size_t column,
                          phy_ir_ref value)
{
    if (matrix == NULL || value == PHY_IR_NULL || row >= matrix->rows ||
        column >= matrix->columns) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    matrix->entries[row * matrix->columns + column] = value;
    return PHY_OK;
}

phy_status phy_linear_compatible(const phy_matrix *left,
                                 const phy_matrix *right)
{
    if (left == NULL || right == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_cas_ir(left->cas) == phy_cas_ir(right->cas)
               ? PHY_OK
               : PHY_ERR_TYPE;
}

phy_status phy_linear_step(phy_linear_budget *budget, uint32_t amount)
{
    if (budget == NULL || amount > budget->maximum - budget->used) {
        return PHY_ERR_TIMEOUT;
    }
    budget->used += amount;
    return PHY_OK;
}

phy_status phy_linear_zero(phy_cas *cas, phy_ir_ref *out)
{
    return phy_cas_number(cas, 0, 1, out);
}

phy_status phy_linear_one(phy_cas *cas, phy_ir_ref *out)
{
    return phy_cas_number(cas, 1, 1, out);
}

phy_status phy_linear_is_zero(phy_cas *cas, phy_ir_ref value,
                              phy_cas_decision *out)
{
    return phy_cas_is_zero(cas, value, out);
}

static phy_status allocate_result(const phy_matrix *source, size_t rows,
                                  size_t columns, phy_matrix **out)
{
    return phy_linear_allocate(
        source->cas, rows, columns, &source->limits, out);
}

phy_status phy_matrix_add(const phy_matrix *left, const phy_matrix *right,
                          phy_matrix **out_matrix)
{
    if (out_matrix == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matrix = NULL;
    phy_status status = phy_linear_compatible(left, right);
    if (status != PHY_OK) {
        return status;
    }
    if (left->rows != right->rows || left->columns != right->columns) {
        return PHY_ERR_TYPE;
    }
    phy_matrix *result = NULL;
    status = allocate_result(left, left->rows, left->columns, &result);
    if (status != PHY_OK) {
        return status;
    }
    phy_linear_budget budget = {0u, left->limits.max_steps};
    for (size_t i = 0u; i < left->count; ++i) {
        status = phy_linear_step(&budget, 1u);
        if (status == PHY_OK) {
            const phy_ir_ref terms[2] = {
                left->entries[i], right->entries[i]};
            status =
                phy_cas_add(left->cas, terms, 2u, &result->entries[i]);
        }
        if (status != PHY_OK) {
            phy_matrix_destroy(result);
            return status;
        }
    }
    *out_matrix = result;
    return PHY_OK;
}

phy_status phy_matrix_scale(const phy_matrix *matrix, phy_ir_ref scalar,
                            phy_matrix **out_matrix)
{
    if (matrix == NULL || scalar == PHY_IR_NULL || out_matrix == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matrix = NULL;
    phy_matrix *result = NULL;
    phy_status status =
        allocate_result(matrix, matrix->rows, matrix->columns, &result);
    if (status != PHY_OK) {
        return status;
    }
    phy_linear_budget budget = {0u, matrix->limits.max_steps};
    for (size_t i = 0u; i < matrix->count; ++i) {
        status = phy_linear_step(&budget, 1u);
        if (status == PHY_OK) {
            const phy_ir_ref factors[2] = {scalar, matrix->entries[i]};
            status =
                phy_cas_mul(matrix->cas, factors, 2u, &result->entries[i]);
        }
        if (status != PHY_OK) {
            phy_matrix_destroy(result);
            return status;
        }
    }
    *out_matrix = result;
    return PHY_OK;
}

phy_status phy_matrix_multiply(const phy_matrix *left,
                               const phy_matrix *right,
                               phy_matrix **out_matrix)
{
    if (out_matrix == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matrix = NULL;
    phy_status status = phy_linear_compatible(left, right);
    if (status != PHY_OK) {
        return status;
    }
    if (left->columns != right->rows) {
        return PHY_ERR_TYPE;
    }
    phy_matrix *result = NULL;
    status =
        allocate_result(left, left->rows, right->columns, &result);
    if (status != PHY_OK) {
        return status;
    }

    phy_linear_budget budget = {0u, left->limits.max_steps};
    phy_ir_ref zero = PHY_IR_NULL;
    status = phy_linear_zero(left->cas, &zero);
    for (size_t row = 0u; status == PHY_OK && row < result->rows; ++row) {
        for (size_t column = 0u;
             status == PHY_OK && column < result->columns; ++column) {
            phy_ir_ref sum = zero;
            for (size_t inner = 0u; inner < left->columns; ++inner) {
                status = phy_linear_step(&budget, 2u);
                if (status != PHY_OK) {
                    break;
                }
                const phy_ir_ref factors[2] = {
                    left->entries[row * left->columns + inner],
                    right->entries[inner * right->columns + column]};
                phy_ir_ref product = PHY_IR_NULL;
                status =
                    phy_cas_mul(left->cas, factors, 2u, &product);
                if (status == PHY_OK) {
                    const phy_ir_ref terms[2] = {sum, product};
                    status =
                        phy_cas_add(left->cas, terms, 2u, &sum);
                }
            }
            if (status == PHY_OK) {
                result->entries[row * result->columns + column] = sum;
            }
        }
    }
    if (status != PHY_OK) {
        phy_matrix_destroy(result);
        return status;
    }
    *out_matrix = result;
    return PHY_OK;
}

phy_status phy_matrix_transpose(const phy_matrix *matrix,
                                phy_matrix **out_matrix)
{
    if (matrix == NULL || out_matrix == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matrix = NULL;
    phy_matrix *result = NULL;
    phy_status status =
        allocate_result(matrix, matrix->columns, matrix->rows, &result);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t row = 0u; row < matrix->rows; ++row) {
        for (size_t column = 0u; column < matrix->columns; ++column) {
            result->entries[column * result->columns + row] =
                matrix->entries[row * matrix->columns + column];
        }
    }
    *out_matrix = result;
    return PHY_OK;
}

phy_status phy_vector_create(phy_cas *cas, size_t length,
                             const phy_ir_ref *entries,
                             const phy_linear_limits *limits,
                             phy_vector **out_vector)
{
    return phy_matrix_create(
        cas, length, 1u, entries, limits, out_vector);
}

size_t phy_vector_length(const phy_vector *vector)
{
    return vector != NULL && vector->columns == 1u ? vector->rows : 0u;
}

phy_status phy_vector_get(const phy_vector *vector, size_t index,
                          phy_ir_ref *out_value)
{
    if (vector == NULL || vector->columns != 1u) {
        return PHY_ERR_TYPE;
    }
    return phy_matrix_get(vector, index, 0u, out_value);
}

phy_status phy_vector_set(phy_vector *vector, size_t index, phy_ir_ref value)
{
    if (vector == NULL || vector->columns != 1u) {
        return PHY_ERR_TYPE;
    }
    return phy_matrix_set(vector, index, 0u, value);
}

phy_status phy_vector_dot(const phy_vector *left, const phy_vector *right,
                          phy_ir_ref *out_value)
{
    if (left == NULL || right == NULL || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = PHY_IR_NULL;
    phy_status status = phy_linear_compatible(left, right);
    if (status != PHY_OK) {
        return status;
    }
    if (left->columns != 1u || right->columns != 1u ||
        left->rows != right->rows) {
        return PHY_ERR_TYPE;
    }
    phy_linear_budget budget = {0u, left->limits.max_steps};
    phy_ir_ref sum = PHY_IR_NULL;
    status = phy_linear_zero(left->cas, &sum);
    for (size_t i = 0u; status == PHY_OK && i < left->rows; ++i) {
        status = phy_linear_step(&budget, 2u);
        if (status != PHY_OK) {
            break;
        }
        const phy_ir_ref factors[2] = {
            left->entries[i], right->entries[i]};
        phy_ir_ref product = PHY_IR_NULL;
        status = phy_cas_mul(left->cas, factors, 2u, &product);
        if (status == PHY_OK) {
            const phy_ir_ref terms[2] = {sum, product};
            status = phy_cas_add(left->cas, terms, 2u, &sum);
        }
    }
    if (status == PHY_OK) {
        *out_value = sum;
    }
    return status;
}

