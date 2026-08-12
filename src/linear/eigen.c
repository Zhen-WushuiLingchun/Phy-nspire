/* Exact eigenspaces and Jordan chains over reader-facing algebraic roots. */
#include <string.h>

#include "linear_internal.h"

static phy_status square_arguments(const phy_matrix *matrix)
{
    return matrix == NULL ? PHY_ERR_INVALID_ARGUMENT
         : matrix->rows != matrix->columns ? PHY_ERR_TYPE
         : PHY_OK;
}

static phy_status shifted_matrix(const phy_matrix *matrix,
                                 phy_ir_ref eigenvalue,
                                 phy_matrix **out_shifted)
{
    if (out_shifted == NULL || eigenvalue == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_shifted = NULL;
    phy_status status = square_arguments(matrix);
    phy_matrix *shifted = NULL;
    if (status == PHY_OK) {
        status = phy_matrix_clone(matrix, &shifted);
    }
    for (size_t index = 0u;
         status == PHY_OK && index < matrix->rows; ++index) {
        status = phy_cas_sub(
            matrix->cas,
            shifted->entries[index * shifted->columns + index],
            eigenvalue,
            &shifted->entries[index * shifted->columns + index]);
    }
    if (status != PHY_OK) {
        phy_matrix_destroy(shifted);
        return status;
    }
    *out_shifted = shifted;
    return PHY_OK;
}

static phy_status identity_matrix(const phy_matrix *source,
                                  phy_matrix **out_identity)
{
    *out_identity = NULL;
    phy_matrix *identity = NULL;
    phy_status status = phy_linear_allocate(
        source->cas, source->rows, source->columns,
        &source->limits, &identity);
    phy_ir_ref zero = PHY_IR_NULL;
    phy_ir_ref one = PHY_IR_NULL;
    if (status == PHY_OK) status = phy_linear_zero(source->cas, &zero);
    if (status == PHY_OK) status = phy_linear_one(source->cas, &one);
    for (size_t row = 0u; status == PHY_OK && row < source->rows; ++row) {
        for (size_t column = 0u; column < source->columns; ++column) {
            identity->entries[row * identity->columns + column] =
                row == column ? one : zero;
        }
    }
    if (status != PHY_OK) {
        phy_matrix_destroy(identity);
        return status;
    }
    *out_identity = identity;
    return PHY_OK;
}

static phy_status matrix_power_u(const phy_matrix *matrix, size_t exponent,
                                 phy_matrix **out_power)
{
    if (out_power == NULL) return PHY_ERR_INVALID_ARGUMENT;
    *out_power = NULL;
    phy_matrix *power = NULL;
    phy_status status = identity_matrix(matrix, &power);
    for (size_t step = 0u; status == PHY_OK && step < exponent; ++step) {
        phy_matrix *next = NULL;
        status = phy_matrix_multiply(power, matrix, &next);
        if (status == PHY_OK) {
            phy_matrix_destroy(power);
            power = next;
        }
    }
    if (status != PHY_OK) {
        phy_matrix_destroy(power);
        return status;
    }
    *out_power = power;
    return PHY_OK;
}

static phy_status matrix_column(const phy_matrix *matrix, size_t column,
                                phy_matrix **out_vector)
{
    if (matrix == NULL || out_vector == NULL || column >= matrix->columns) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_vector = NULL;
    phy_matrix *vector = NULL;
    phy_status status = phy_linear_allocate(
        matrix->cas, matrix->rows, 1u, &matrix->limits, &vector);
    for (size_t row = 0u; status == PHY_OK && row < matrix->rows; ++row) {
        vector->entries[row] = matrix->entries[row * matrix->columns + column];
    }
    if (status != PHY_OK) {
        phy_matrix_destroy(vector);
        return status;
    }
    *out_vector = vector;
    return PHY_OK;
}

static void set_column(phy_matrix *matrix, size_t column,
                       const phy_matrix *vector)
{
    for (size_t row = 0u; row < matrix->rows; ++row) {
        matrix->entries[row * matrix->columns + column] =
            vector->entries[row];
    }
}

static phy_status rank_of_prefix_with_column(
    const phy_matrix *columns, size_t used,
    const phy_matrix *candidate, bool *out_increases)
{
    if (columns == NULL || candidate == NULL || out_increases == NULL ||
        columns->rows != candidate->rows || candidate->columns != 1u ||
        used >= columns->columns) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_matrix *probe = NULL;
    phy_status status = phy_linear_allocate(
        columns->cas, columns->rows, used + 1u,
        &columns->limits, &probe);
    if (status == PHY_OK) {
        for (size_t row = 0u; row < columns->rows; ++row) {
            for (size_t column = 0u; column < used; ++column) {
                probe->entries[row * probe->columns + column] =
                    columns->entries[row * columns->columns + column];
            }
            probe->entries[row * probe->columns + used] =
                candidate->entries[row];
        }
    }
    size_t rank = 0u;
    if (status == PHY_OK) status = phy_matrix_rank(probe, &rank);
    phy_matrix_destroy(probe);
    if (status == PHY_OK) *out_increases = rank == used + 1u;
    return status;
}

phy_status phy_matrix_eigenspace(
    const phy_matrix *matrix, phy_ir_ref eigenvalue,
    phy_matrix **out_basis, size_t *out_dimension)
{
    if (out_basis == NULL || out_dimension == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_basis = NULL;
    *out_dimension = 0u;
    phy_matrix *shifted = NULL;
    phy_status status = shifted_matrix(matrix, eigenvalue, &shifted);
    if (status == PHY_OK) {
        status = phy_matrix_null_space(
            shifted, out_basis, out_dimension);
    }
    phy_matrix_destroy(shifted);
    return status;
}

phy_status phy_matrix_generalized_eigenspace(
    const phy_matrix *matrix, phy_ir_ref eigenvalue,
    phy_matrix **out_basis, size_t *out_dimension)
{
    if (out_basis == NULL || out_dimension == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_basis = NULL;
    *out_dimension = 0u;
    phy_matrix *shifted = NULL;
    phy_matrix *power = NULL;
    phy_status status = shifted_matrix(matrix, eigenvalue, &shifted);
    if (status == PHY_OK) {
        status = matrix_power_u(shifted, matrix->rows, &power);
    }
    if (status == PHY_OK) {
        status = phy_matrix_null_space(power, out_basis, out_dimension);
    }
    phy_matrix_destroy(power);
    phy_matrix_destroy(shifted);
    return status;
}

static phy_status eigenvalue_list(const phy_matrix *matrix,
                                  phy_ir_ref variable,
                                  phy_ir_ref *out_values)
{
    phy_status status = square_arguments(matrix);
    if (status != PHY_OK) return status;
    status = phy_matrix_eigenvalues(matrix, variable, out_values);
    if (status != PHY_OK) return status;
    const phy_ir_symbol list = phy_ir_intern(phy_cas_ir(matrix->cas), "List");
    return phy_ir_kind_of(phy_cas_ir(matrix->cas), *out_values) ==
                   PHY_IR_FUNCTION &&
               phy_ir_head(phy_cas_ir(matrix->cas), *out_values) == list &&
               phy_ir_child_count(phy_cas_ir(matrix->cas), *out_values) ==
                   matrix->rows
        ? PHY_OK : PHY_ERR_CORRUPT_DOCUMENT;
}

phy_status phy_matrix_eigenvectors(
    const phy_matrix *matrix, phy_ir_ref variable,
    phy_matrix **out_vectors)
{
    if (out_vectors == NULL) return PHY_ERR_INVALID_ARGUMENT;
    *out_vectors = NULL;
    phy_ir_ref values = PHY_IR_NULL;
    phy_status status = eigenvalue_list(matrix, variable, &values);
    phy_matrix *vectors = NULL;
    if (status == PHY_OK) {
        status = phy_linear_allocate(
            matrix->cas, matrix->rows, matrix->columns,
            &matrix->limits, &vectors);
    }
    phy_ir_ref zero = PHY_IR_NULL;
    if (status == PHY_OK) status = phy_linear_zero(matrix->cas, &zero);
    if (status == PHY_OK) {
        for (size_t index = 0u; index < vectors->count; ++index) {
            vectors->entries[index] = zero;
        }
    }

    phy_ir_context *ir = phy_cas_ir(matrix->cas);
    for (size_t start = 0u; status == PHY_OK && start < matrix->rows;) {
        const phy_ir_ref eigenvalue = phy_ir_child(ir, values, start);
        size_t end = start + 1u;
        while (end < matrix->rows &&
               phy_ir_child(ir, values, end) == eigenvalue) {
            ++end;
        }
        phy_matrix *basis = NULL;
        size_t dimension = 0u;
        status = phy_matrix_eigenspace(
            matrix, eigenvalue, &basis, &dimension);
        if (status == PHY_OK && (dimension == 0u || dimension > end - start)) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        }
        for (size_t column = 0u;
             status == PHY_OK && column < dimension; ++column) {
            for (size_t row = 0u; row < matrix->rows; ++row) {
                vectors->entries[row * vectors->columns + start + column] =
                    basis->entries[row * basis->columns + column];
            }
        }
        phy_matrix_destroy(basis);
        start = end;
    }
    if (status != PHY_OK) {
        phy_matrix_destroy(vectors);
        return status;
    }
    *out_vectors = vectors;
    return PHY_OK;
}

static phy_status matrices_exactly_equal(const phy_matrix *left,
                                         const phy_matrix *right,
                                         bool *out_equal)
{
    *out_equal = false;
    if (left->rows != right->rows || left->columns != right->columns) {
        return PHY_ERR_TYPE;
    }
    for (size_t index = 0u; index < left->count; ++index) {
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        const phy_status status = phy_cas_equivalent(
            left->cas, left->entries[index], right->entries[index],
            &decision);
        if (status != PHY_OK) return status;
        if (decision != PHY_CAS_ZERO) return PHY_OK;
    }
    *out_equal = true;
    return PHY_OK;
}

phy_status phy_matrix_jordan_decomposition(
    const phy_matrix *matrix, phy_ir_ref variable,
    phy_matrix **out_transform, phy_matrix **out_jordan)
{
    if (out_transform == NULL || out_jordan == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_transform = NULL;
    *out_jordan = NULL;
    phy_ir_ref values = PHY_IR_NULL;
    phy_status status = eigenvalue_list(matrix, variable, &values);
    const size_t n = status == PHY_OK ? matrix->rows : 0u;
    if (status != PHY_OK || n > SIZE_MAX / sizeof(phy_matrix *) ||
        n > SIZE_MAX / sizeof(size_t)) {
        return status != PHY_OK ? status : PHY_ERR_MEMORY_LIMIT;
    }

    phy_matrix *transform = NULL;
    phy_matrix *jordan = NULL;
    status = phy_linear_allocate(
        matrix->cas, n, n, &matrix->limits, &transform);
    if (status == PHY_OK) {
        status = phy_linear_allocate(
            matrix->cas, n, n, &matrix->limits, &jordan);
    }
    phy_ir_ref zero = PHY_IR_NULL;
    phy_ir_ref one = PHY_IR_NULL;
    if (status == PHY_OK) status = phy_linear_zero(matrix->cas, &zero);
    if (status == PHY_OK) status = phy_linear_one(matrix->cas, &one);
    if (status == PHY_OK) {
        for (size_t index = 0u; index < n * n; ++index) {
            transform->entries[index] = zero;
            jordan->entries[index] = zero;
        }
    }

    phy_matrix **kernels = phy_alloc((n + 1u) * sizeof(*kernels));
    size_t *nullities = phy_alloc((n + 1u) * sizeof(*nullities));
    phy_matrix **tops = phy_alloc(n * sizeof(*tops));
    size_t *lengths = phy_alloc(n * sizeof(*lengths));
    if (status == PHY_OK &&
        (kernels == NULL || nullities == NULL || tops == NULL ||
         lengths == NULL)) {
        status = PHY_ERR_MEMORY_LIMIT;
    }
    if (kernels != NULL) memset(kernels, 0, (n + 1u) * sizeof(*kernels));
    if (nullities != NULL) memset(nullities, 0, (n + 1u) * sizeof(*nullities));
    if (tops != NULL) memset(tops, 0, n * sizeof(*tops));
    if (lengths != NULL) memset(lengths, 0, n * sizeof(*lengths));

    phy_ir_context *ir = phy_cas_ir(matrix->cas);
    size_t output_column = 0u;
    for (size_t start = 0u; status == PHY_OK && start < n;) {
        const phy_ir_ref eigenvalue = phy_ir_child(ir, values, start);
        size_t end = start + 1u;
        while (end < n && phy_ir_child(ir, values, end) == eigenvalue) ++end;
        const size_t multiplicity = end - start;

        phy_matrix *shifted = NULL;
        status = shifted_matrix(matrix, eigenvalue, &shifted);
        phy_matrix *power = NULL;
        if (status == PHY_OK) status = identity_matrix(matrix, &power);
        for (size_t k = 1u; status == PHY_OK && k <= multiplicity; ++k) {
            phy_matrix *next = NULL;
            status = phy_matrix_multiply(power, shifted, &next);
            if (status == PHY_OK) {
                phy_matrix_destroy(power);
                power = next;
                status = phy_matrix_null_space(
                    power, &kernels[k], &nullities[k]);
            }
        }
        if (status == PHY_OK && nullities[multiplicity] != multiplicity) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        }

        phy_matrix *bottoms = NULL;
        if (status == PHY_OK) {
            status = phy_linear_allocate(
                matrix->cas, n, multiplicity,
                &matrix->limits, &bottoms);
        }
        size_t chain_count = 0u;
        for (size_t k = multiplicity;
             status == PHY_OK && k > 0u; --k) {
            const size_t blocks_at_least_k = nullities[k] - nullities[k - 1u];
            const size_t blocks_at_least_next =
                k == multiplicity ? 0u : nullities[k + 1u] - nullities[k];
            const size_t wanted = blocks_at_least_k - blocks_at_least_next;
            size_t selected = 0u;
            phy_matrix *previous_power = NULL;
            if (wanted != 0u) {
                status = matrix_power_u(shifted, k - 1u, &previous_power);
            }
            for (size_t candidate_column = 0u;
                 status == PHY_OK && selected < wanted &&
                 candidate_column < nullities[k]; ++candidate_column) {
                phy_matrix *candidate = NULL;
                phy_matrix *bottom = NULL;
                status = matrix_column(
                    kernels[k], candidate_column, &candidate);
                if (status == PHY_OK) {
                    status = phy_matrix_multiply(
                        previous_power, candidate, &bottom);
                }
                bool increases = false;
                if (status == PHY_OK) {
                    status = rank_of_prefix_with_column(
                        bottoms, chain_count, bottom, &increases);
                }
                if (status == PHY_OK && increases) {
                    tops[chain_count] = candidate;
                    candidate = NULL;
                    lengths[chain_count] = k;
                    set_column(bottoms, chain_count, bottom);
                    ++chain_count;
                    ++selected;
                }
                phy_matrix_destroy(bottom);
                phy_matrix_destroy(candidate);
            }
            phy_matrix_destroy(previous_power);
            if (status == PHY_OK && selected != wanted) {
                status = PHY_ERR_CORRUPT_DOCUMENT;
            }
        }

        size_t group_columns = 0u;
        for (size_t chain = 0u;
             status == PHY_OK && chain < chain_count; ++chain) {
            const size_t length = lengths[chain];
            phy_matrix *current = NULL;
            status = phy_matrix_clone(tops[chain], &current);
            for (size_t position = length;
                 status == PHY_OK && position > 0u; --position) {
                const size_t column = output_column + group_columns + position - 1u;
                set_column(transform, column, current);
                jordan->entries[column * jordan->columns + column] = eigenvalue;
                if (position < length) {
                    jordan->entries[column * jordan->columns + column + 1u] = one;
                }
                if (position > 1u) {
                    phy_matrix *next = NULL;
                    status = phy_matrix_multiply(shifted, current, &next);
                    if (status == PHY_OK) {
                        phy_matrix_destroy(current);
                        current = next;
                    }
                }
            }
            phy_matrix_destroy(current);
            group_columns += length;
        }
        if (status == PHY_OK && group_columns != multiplicity) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        }
        output_column += group_columns;

        for (size_t index = 0u; index < chain_count; ++index) {
            phy_matrix_destroy(tops[index]);
            tops[index] = NULL;
        }
        for (size_t k = 1u; k <= multiplicity; ++k) {
            phy_matrix_destroy(kernels[k]);
            kernels[k] = NULL;
            nullities[k] = 0u;
        }
        phy_matrix_destroy(bottoms);
        phy_matrix_destroy(power);
        phy_matrix_destroy(shifted);
        start = end;
    }

    if (status == PHY_OK && output_column != n) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    size_t transform_rank = 0u;
    if (status == PHY_OK) status = phy_matrix_rank(transform, &transform_rank);
    if (status == PHY_OK && transform_rank != n) status = PHY_ERR_CORRUPT_DOCUMENT;
    phy_matrix *left = NULL;
    phy_matrix *right = NULL;
    if (status == PHY_OK) status = phy_matrix_multiply(matrix, transform, &left);
    if (status == PHY_OK) status = phy_matrix_multiply(transform, jordan, &right);
    bool invariant = false;
    if (status == PHY_OK) status = matrices_exactly_equal(left, right, &invariant);
    if (status == PHY_OK && !invariant) status = PHY_ERR_CORRUPT_DOCUMENT;
    phy_matrix_destroy(right);
    phy_matrix_destroy(left);

    if (tops != NULL) {
        for (size_t index = 0u; index < n; ++index) {
            phy_matrix_destroy(tops[index]);
        }
    }
    if (kernels != NULL) {
        for (size_t index = 0u; index <= n; ++index) {
            phy_matrix_destroy(kernels[index]);
        }
    }
    if (lengths != NULL) phy_free(lengths, n * sizeof(*lengths));
    if (tops != NULL) phy_free(tops, n * sizeof(*tops));
    if (nullities != NULL) phy_free(nullities, (n + 1u) * sizeof(*nullities));
    if (kernels != NULL) phy_free(kernels, (n + 1u) * sizeof(*kernels));
    if (status != PHY_OK) {
        phy_matrix_destroy(jordan);
        phy_matrix_destroy(transform);
        return status;
    }
    *out_transform = transform;
    *out_jordan = jordan;
    return PHY_OK;
}
