#include "linear_internal.h"

#include <string.h>

static phy_status find_pivot(phy_cas *cas, const phy_ir_ref *entries,
                             size_t rows, size_t columns, size_t first_row,
                             size_t column, size_t *out_row)
{
    const size_t no_row = SIZE_MAX;
    size_t pivot = no_row;
    bool saw_unknown = false;
    for (size_t row = first_row; row < rows; ++row) {
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        const phy_status status = phy_linear_is_zero(
            cas, entries[row * columns + column], &decision);
        if (status != PHY_OK) {
            return status;
        }
        if (decision == PHY_CAS_NONZERO) {
            pivot = row;
            break;
        }
        saw_unknown = saw_unknown || decision == PHY_CAS_UNKNOWN;
    }
    if (pivot == no_row && saw_unknown) {
        return PHY_ERR_UNSUPPORTED;
    }
    *out_row = pivot;
    return PHY_OK;
}

static void swap_rows(phy_ir_ref *entries, size_t columns, size_t left,
                      size_t right)
{
    if (left == right) {
        return;
    }
    for (size_t column = 0u; column < columns; ++column) {
        const size_t a = left * columns + column;
        const size_t b = right * columns + column;
        const phy_ir_ref temporary = entries[a];
        entries[a] = entries[b];
        entries[b] = temporary;
    }
}

phy_status phy_linear_rref_array(phy_cas *cas, phy_ir_ref *entries,
                                 size_t rows, size_t columns,
                                 size_t pivot_column_limit,
                                 phy_linear_budget *budget,
                                 size_t *pivot_columns, size_t *out_rank)
{
    if (cas == NULL || entries == NULL || rows == 0u || columns == 0u ||
        pivot_column_limit > columns || budget == NULL || out_rank == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    size_t rank = 0u;
    for (size_t column = 0u;
         column < pivot_column_limit && rank < rows; ++column) {
        size_t pivot = SIZE_MAX;
        phy_status status = find_pivot(
            cas, entries, rows, columns, rank, column, &pivot);
        if (status != PHY_OK) {
            return status;
        }
        if (pivot == SIZE_MAX) {
            continue;
        }
        swap_rows(entries, columns, rank, pivot);

        const phy_ir_ref divisor = entries[rank * columns + column];
        for (size_t entry = 0u; entry < columns; ++entry) {
            status = phy_linear_step(budget, 1u);
            if (status != PHY_OK) {
                return status;
            }
            phy_ir_ref quotient = PHY_IR_NULL;
            status = phy_cas_div(
                cas, entries[rank * columns + entry], divisor, &quotient);
            if (status != PHY_OK) {
                return status;
            }
            entries[rank * columns + entry] = quotient;
        }

        for (size_t row = 0u; row < rows; ++row) {
            if (row == rank) {
                continue;
            }
            const phy_ir_ref factor = entries[row * columns + column];
            phy_cas_decision zero = PHY_CAS_UNKNOWN;
            status = phy_linear_is_zero(cas, factor, &zero);
            if (status != PHY_OK) {
                return status;
            }
            if (zero == PHY_CAS_ZERO) {
                continue;
            }
            if (zero == PHY_CAS_UNKNOWN) {
                return PHY_ERR_UNSUPPORTED;
            }
            for (size_t entry = 0u; entry < columns; ++entry) {
                status = phy_linear_step(budget, 2u);
                if (status != PHY_OK) {
                    return status;
                }
                const phy_ir_ref factors[2] = {
                    factor, entries[rank * columns + entry]};
                phy_ir_ref product = PHY_IR_NULL;
                status = phy_cas_mul(cas, factors, 2u, &product);
                if (status == PHY_OK) {
                    status = phy_cas_sub(
                        cas, entries[row * columns + entry], product,
                        &entries[row * columns + entry]);
                }
                if (status != PHY_OK) {
                    return status;
                }
            }
        }
        if (pivot_columns != NULL) {
            pivot_columns[rank] = column;
        }
        rank++;
    }
    *out_rank = rank;
    return PHY_OK;
}

phy_status phy_matrix_rref(const phy_matrix *matrix, phy_matrix **out_matrix,
                           size_t *out_rank)
{
    if (matrix == NULL || out_matrix == NULL || out_rank == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matrix = NULL;
    *out_rank = 0u;
    phy_matrix *result = NULL;
    phy_status status = phy_matrix_clone(matrix, &result);
    if (status != PHY_OK) {
        return status;
    }
    phy_linear_budget budget = {0u, matrix->limits.max_steps};
    status = phy_linear_rref_array(
        matrix->cas, result->entries, result->rows, result->columns,
        result->columns, &budget, NULL, out_rank);
    if (status != PHY_OK) {
        phy_matrix_destroy(result);
        return status;
    }
    *out_matrix = result;
    return PHY_OK;
}

phy_status phy_matrix_rank(const phy_matrix *matrix, size_t *out_rank)
{
    if (matrix == NULL || out_rank == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_matrix *reduced = NULL;
    const phy_status status =
        phy_matrix_rref(matrix, &reduced, out_rank);
    phy_matrix_destroy(reduced);
    return status;
}

phy_status phy_matrix_determinant(const phy_matrix *matrix,
                                  phy_ir_ref *out_value)
{
    if (matrix == NULL || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = PHY_IR_NULL;
    if (matrix->rows != matrix->columns) {
        return PHY_ERR_TYPE;
    }
    if (matrix->rows == 1u) {
        *out_value = matrix->entries[0];
        return PHY_OK;
    }

    phy_matrix *work = NULL;
    phy_status status = phy_matrix_clone(matrix, &work);
    if (status != PHY_OK) {
        return status;
    }
    phy_linear_budget budget = {0u, matrix->limits.max_steps};
    phy_ir_ref previous = PHY_IR_NULL;
    status = phy_linear_one(matrix->cas, &previous);
    bool negate = false;

    for (size_t column = 0u;
         status == PHY_OK && column + 1u < work->columns; ++column) {
        size_t pivot_row = SIZE_MAX;
        status = find_pivot(
            matrix->cas, work->entries, work->rows, work->columns,
            column, column, &pivot_row);
        if (status != PHY_OK) {
            break;
        }
        if (pivot_row == SIZE_MAX) {
            status = phy_linear_zero(matrix->cas, out_value);
            phy_matrix_destroy(work);
            return status;
        }
        if (pivot_row != column) {
            swap_rows(work->entries, work->columns, pivot_row, column);
            negate = !negate;
        }
        const phy_ir_ref pivot =
            work->entries[column * work->columns + column];
        for (size_t row = column + 1u; row < work->rows; ++row) {
            for (size_t entry = column + 1u;
                 entry < work->columns; ++entry) {
                status = phy_linear_step(&budget, 4u);
                if (status != PHY_OK) {
                    break;
                }
                const phy_ir_ref diagonal_product[2] = {
                    work->entries[row * work->columns + entry], pivot};
                const phy_ir_ref cross_product[2] = {
                    work->entries[row * work->columns + column],
                    work->entries[column * work->columns + entry]};
                phy_ir_ref diagonal = PHY_IR_NULL;
                phy_ir_ref cross = PHY_IR_NULL;
                phy_ir_ref numerator = PHY_IR_NULL;
                status = phy_cas_mul(
                    matrix->cas, diagonal_product, 2u, &diagonal);
                if (status == PHY_OK) {
                    status = phy_cas_mul(
                        matrix->cas, cross_product, 2u, &cross);
                }
                if (status == PHY_OK) {
                    status = phy_cas_sub(
                        matrix->cas, diagonal, cross, &numerator);
                }
                if (status == PHY_OK) {
                    status = phy_cas_div(
                        matrix->cas, numerator, previous,
                        &work->entries[row * work->columns + entry]);
                }
            }
        }
        previous = pivot;
    }

    if (status == PHY_OK) {
        phy_ir_ref determinant =
            work->entries[(work->rows - 1u) * work->columns +
                          (work->columns - 1u)];
        if (negate) {
            status =
                phy_cas_neg(matrix->cas, determinant, out_value);
        } else {
            *out_value = determinant;
        }
    }
    phy_matrix_destroy(work);
    return status;
}

phy_status phy_matrix_characteristic_polynomial(
    const phy_matrix *matrix, phy_ir_ref variable,
    phy_ir_ref *out_polynomial)
{
    if (matrix == NULL || out_polynomial == NULL ||
        matrix->rows != matrix->columns ||
        phy_ir_kind_of(phy_cas_ir(matrix->cas), variable) !=
            PHY_IR_SYMBOL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_polynomial = PHY_IR_NULL;
    if (matrix->rows > SIZE_MAX / sizeof(phy_ir_ref) - 1u) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t coefficient_bytes =
        (matrix->rows + 1u) * sizeof(phy_ir_ref);
    phy_ir_ref *coefficients = phy_alloc(coefficient_bytes);
    if (coefficients == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_status status = phy_linear_one(matrix->cas, &coefficients[0]);
    phy_matrix *b = NULL;
    if (status == PHY_OK) {
        status = phy_matrix_create(
            matrix->cas, matrix->rows, matrix->columns, NULL,
            &matrix->limits, &b);
    }
    for (size_t row = 0u;
         status == PHY_OK && row < matrix->rows; ++row) {
        status = phy_matrix_set(b, row, row, coefficients[0]);
    }

    /* Division-free in the spectral variable and pivot-free in the matrix:
       Faddeev--LeVerrier computes the coefficients of det(x I - A) over the
       characteristic-zero exact CAS. It avoids treating x-a as a provably
       nonzero numerical pivot. */
    for (size_t order = 1u;
         status == PHY_OK && order <= matrix->rows; ++order) {
        phy_matrix *product = NULL;
        status = phy_matrix_multiply(matrix, b, &product);
        phy_ir_ref trace = PHY_IR_NULL;
        if (status == PHY_OK) {
            status = phy_linear_zero(matrix->cas, &trace);
        }
        for (size_t diagonal = 0u;
             status == PHY_OK && diagonal < matrix->rows; ++diagonal) {
            phy_ir_ref entry = PHY_IR_NULL;
            status = phy_matrix_get(
                product, diagonal, diagonal, &entry);
            if (status == PHY_OK) {
                const phy_ir_ref terms[2] = {trace, entry};
                status = phy_cas_add(
                    matrix->cas, terms, 2u, &trace);
            }
        }
        phy_ir_ref negative_trace = PHY_IR_NULL;
        phy_ir_ref divisor = PHY_IR_NULL;
        if (status == PHY_OK) {
            status = phy_cas_neg(
                matrix->cas, trace, &negative_trace);
        }
        if (status == PHY_OK) {
            status = phy_cas_number(
                matrix->cas, (int64_t)order, 1, &divisor);
        }
        if (status == PHY_OK) {
            status = phy_cas_div(
                matrix->cas, negative_trace, divisor,
                &coefficients[order]);
        }
        for (size_t diagonal = 0u;
             status == PHY_OK && diagonal < matrix->rows; ++diagonal) {
            phy_ir_ref entry = PHY_IR_NULL;
            status = phy_matrix_get(
                product, diagonal, diagonal, &entry);
            if (status == PHY_OK) {
                const phy_ir_ref terms[2] = {
                    entry, coefficients[order]};
                status = phy_cas_add(
                    matrix->cas, terms, 2u, &entry);
            }
            if (status == PHY_OK) {
                status = phy_matrix_set(product, diagonal, diagonal, entry);
            }
        }
        phy_matrix_destroy(b);
        b = product;
        if (status != PHY_OK) {
            phy_matrix_destroy(product);
            b = NULL;
        }
    }

    phy_ir_ref sum = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_linear_zero(matrix->cas, &sum);
    }
    for (size_t order = 0u;
         status == PHY_OK && order <= matrix->rows; ++order) {
        const size_t exponent = matrix->rows - order;
        phy_ir_ref power = coefficients[0];
        if (exponent == 1u) {
            power = variable;
        } else if (exponent > 1u) {
            phy_ir_ref exponent_ref = PHY_IR_NULL;
            status = phy_cas_number(
                matrix->cas, (int64_t)exponent, 1, &exponent_ref);
            if (status == PHY_OK) {
                status = phy_cas_pow(
                    matrix->cas, variable, exponent_ref, &power);
            }
        }
        phy_ir_ref term = coefficients[order];
        if (status == PHY_OK && exponent != 0u) {
            const phy_ir_ref factors[2] = {coefficients[order], power};
            status = phy_cas_mul(matrix->cas, factors, 2u, &term);
        }
        if (status == PHY_OK) {
            const phy_ir_ref terms[2] = {sum, term};
            status = phy_cas_add(matrix->cas, terms, 2u, &sum);
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_expand(matrix->cas, sum, out_polynomial);
    }
    phy_matrix_destroy(b);
    phy_free(coefficients, coefficient_bytes);
    return status;
}

phy_status phy_matrix_eigenvalues(
    const phy_matrix *matrix, phy_ir_ref variable,
    phy_ir_ref *out_values)
{
    if (out_values == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_values = PHY_IR_NULL;
    phy_ir_ref polynomial = PHY_IR_NULL;
    phy_status status = phy_matrix_characteristic_polynomial(
        matrix, variable, &polynomial);
    return status == PHY_OK
        ? phy_cas_polynomial_roots(
              matrix->cas, polynomial, variable, true, out_values)
        : status;
}

static phy_status augmented_rref(const phy_matrix *left,
                                 const phy_matrix *right,
                                 phy_matrix **out_augmented, size_t *out_rank,
                                 size_t *pivot_columns)
{
    if (left == NULL || right == NULL || out_augmented == NULL ||
        out_rank == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_augmented = NULL;
    phy_status status = phy_linear_compatible(left, right);
    if (status != PHY_OK) {
        return status;
    }
    if (left->rows != right->rows ||
        left->columns > SIZE_MAX - right->columns) {
        return PHY_ERR_TYPE;
    }
    const size_t columns = left->columns + right->columns;
    phy_matrix *augmented = NULL;
    status = phy_linear_allocate(
        left->cas, left->rows, columns, &left->limits, &augmented);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t row = 0u; row < left->rows; ++row) {
        memcpy(&augmented->entries[row * columns],
               &left->entries[row * left->columns],
               left->columns * sizeof(phy_ir_ref));
        memcpy(&augmented->entries[row * columns + left->columns],
               &right->entries[row * right->columns],
               right->columns * sizeof(phy_ir_ref));
    }
    phy_linear_budget budget = {0u, left->limits.max_steps};
    status = phy_linear_rref_array(
        left->cas, augmented->entries, augmented->rows, augmented->columns,
        left->columns, &budget, pivot_columns, out_rank);
    if (status != PHY_OK) {
        phy_matrix_destroy(augmented);
        return status;
    }
    *out_augmented = augmented;
    return PHY_OK;
}

phy_status phy_matrix_inverse(const phy_matrix *matrix,
                              phy_matrix **out_matrix)
{
    if (matrix == NULL || out_matrix == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matrix = NULL;
    if (matrix->rows != matrix->columns) {
        return PHY_ERR_TYPE;
    }
    phy_matrix *identity = NULL;
    phy_status status = phy_linear_allocate(
        matrix->cas, matrix->rows, matrix->columns, &matrix->limits,
        &identity);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref zero = PHY_IR_NULL;
    phy_ir_ref one = PHY_IR_NULL;
    status = phy_linear_zero(matrix->cas, &zero);
    if (status == PHY_OK) {
        status = phy_linear_one(matrix->cas, &one);
    }
    for (size_t row = 0u; status == PHY_OK && row < identity->rows; ++row) {
        for (size_t column = 0u; column < identity->columns; ++column) {
            identity->entries[row * identity->columns + column] =
                row == column ? one : zero;
        }
    }

    phy_matrix *augmented = NULL;
    size_t rank = 0u;
    if (status == PHY_OK) {
        status = augmented_rref(
            matrix, identity, &augmented, &rank, NULL);
    }
    phy_matrix_destroy(identity);
    if (status != PHY_OK) {
        return status;
    }
    if (rank != matrix->rows) {
        phy_matrix_destroy(augmented);
        return PHY_ERR_DOMAIN;
    }

    phy_matrix *inverse = NULL;
    status = phy_linear_allocate(
        matrix->cas, matrix->rows, matrix->columns, &matrix->limits,
        &inverse);
    if (status == PHY_OK) {
        for (size_t row = 0u; row < matrix->rows; ++row) {
            memcpy(&inverse->entries[row * inverse->columns],
                   &augmented->entries[
                       row * augmented->columns + matrix->columns],
                   matrix->columns * sizeof(phy_ir_ref));
        }
    }
    phy_matrix_destroy(augmented);
    if (status != PHY_OK) {
        phy_matrix_destroy(inverse);
        return status;
    }
    *out_matrix = inverse;
    return PHY_OK;
}

phy_status phy_matrix_solve(const phy_matrix *a, const phy_matrix *b,
                            phy_matrix **out_solution)
{
    if (a == NULL || b == NULL || out_solution == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_solution = NULL;
    if (a->rows != a->columns || b->rows != a->rows) {
        return PHY_ERR_TYPE;
    }
    phy_matrix *augmented = NULL;
    size_t rank = 0u;
    phy_status status =
        augmented_rref(a, b, &augmented, &rank, NULL);
    if (status != PHY_OK) {
        return status;
    }
    if (rank != a->columns) {
        phy_matrix_destroy(augmented);
        return PHY_ERR_DOMAIN;
    }
    phy_matrix *solution = NULL;
    status = phy_linear_allocate(
        a->cas, a->columns, b->columns, &a->limits, &solution);
    if (status == PHY_OK) {
        for (size_t row = 0u; row < a->columns; ++row) {
            memcpy(&solution->entries[row * solution->columns],
                   &augmented->entries[
                       row * augmented->columns + a->columns],
                   b->columns * sizeof(phy_ir_ref));
        }
    }
    phy_matrix_destroy(augmented);
    if (status != PHY_OK) {
        phy_matrix_destroy(solution);
        return status;
    }
    *out_solution = solution;
    return PHY_OK;
}

phy_status phy_matrix_null_space(const phy_matrix *matrix,
                                 phy_matrix **out_basis,
                                 size_t *out_nullity)
{
    if (matrix == NULL || out_basis == NULL || out_nullity == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_basis = NULL;
    *out_nullity = 0u;
    phy_matrix *reduced = NULL;
    phy_status status = phy_matrix_clone(matrix, &reduced);
    if (status != PHY_OK) {
        return status;
    }
    if (matrix->rows > SIZE_MAX / sizeof(size_t)) {
        phy_matrix_destroy(reduced);
        return PHY_ERR_MEMORY_LIMIT;
    }
    size_t *pivots =
        phy_alloc(matrix->rows * sizeof(size_t));
    if (pivots == NULL) {
        phy_matrix_destroy(reduced);
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_linear_budget budget = {0u, matrix->limits.max_steps};
    size_t rank = 0u;
    status = phy_linear_rref_array(
        matrix->cas, reduced->entries, reduced->rows, reduced->columns,
        reduced->columns, &budget, pivots, &rank);
    if (status != PHY_OK) {
        phy_free(pivots, matrix->rows * sizeof(size_t));
        phy_matrix_destroy(reduced);
        return status;
    }
    const size_t nullity = matrix->columns - rank;
    if (nullity == 0u) {
        phy_free(pivots, matrix->rows * sizeof(size_t));
        phy_matrix_destroy(reduced);
        return PHY_OK;
    }

    phy_matrix *basis = NULL;
    status = phy_linear_allocate(
        matrix->cas, matrix->columns, nullity, &matrix->limits, &basis);
    phy_ir_ref zero = PHY_IR_NULL;
    phy_ir_ref one = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_linear_zero(matrix->cas, &zero);
    }
    if (status == PHY_OK) {
        status = phy_linear_one(matrix->cas, &one);
    }
    if (status == PHY_OK) {
        for (size_t i = 0u; i < basis->count; ++i) {
            basis->entries[i] = zero;
        }
        size_t basis_column = 0u;
        size_t pivot_cursor = 0u;
        for (size_t column = 0u; column < matrix->columns; ++column) {
            if (pivot_cursor < rank && pivots[pivot_cursor] == column) {
                pivot_cursor++;
                continue;
            }
            basis->entries[column * nullity + basis_column] = one;
            for (size_t row = 0u; row < rank; ++row) {
                status = phy_cas_neg(
                    matrix->cas,
                    reduced->entries[row * reduced->columns + column],
                    &basis->entries[pivots[row] * nullity + basis_column]);
                if (status != PHY_OK) {
                    break;
                }
            }
            if (status != PHY_OK) {
                break;
            }
            basis_column++;
        }
    }
    phy_free(pivots, matrix->rows * sizeof(size_t));
    phy_matrix_destroy(reduced);
    if (status != PHY_OK) {
        phy_matrix_destroy(basis);
        return status;
    }
    *out_basis = basis;
    *out_nullity = nullity;
    return PHY_OK;
}
