/*
 * Phy-nspire — dynamic exact vectors and matrices.
 *
 * Matrix entries are context-local phy_ir_ref values and every arithmetic
 * operation is delegated to the borrowed scalar CAS.  Shapes are runtime
 * values: there is no dimension-four ceiling.  Configured entry, byte and step
 * budgets make the same API safe on Ndless.
 */
#ifndef PHY_LINEAR_H
#define PHY_LINEAR_H

#include <stddef.h>
#include <stdint.h>

#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_matrix phy_matrix;
typedef phy_matrix phy_vector;

typedef struct {
    size_t max_entries; /* one matrix; default 4096 */
    size_t max_bytes;   /* one matrix allocation set; default 256 KiB */
    uint32_t max_steps; /* one compound operation; default 250000 */
} phy_linear_limits;

void phy_linear_limits_defaults(phy_linear_limits *out_limits);

/*
 * Create a rows-by-columns matrix.  Both extents must be non-zero.  `entries`
 * is row-major; NULL constructs an exact zero matrix.  The matrix borrows the
 * CAS, which must outlive it.
 */
phy_status phy_matrix_create(phy_cas *cas, size_t rows, size_t columns,
                             const phy_ir_ref *entries,
                             const phy_linear_limits *limits,
                             phy_matrix **out_matrix);
phy_status phy_matrix_clone(const phy_matrix *matrix, phy_matrix **out_matrix);
void phy_matrix_destroy(phy_matrix *matrix);

phy_cas *phy_matrix_cas(const phy_matrix *matrix);
size_t phy_matrix_rows(const phy_matrix *matrix);
size_t phy_matrix_columns(const phy_matrix *matrix);
size_t phy_matrix_entry_count(const phy_matrix *matrix);

phy_status phy_matrix_get(const phy_matrix *matrix, size_t row, size_t column,
                          phy_ir_ref *out_value);
phy_status phy_matrix_set(phy_matrix *matrix, size_t row, size_t column,
                          phy_ir_ref value);

phy_status phy_matrix_add(const phy_matrix *left, const phy_matrix *right,
                          phy_matrix **out_matrix);
phy_status phy_matrix_scale(const phy_matrix *matrix, phy_ir_ref scalar,
                            phy_matrix **out_matrix);
phy_status phy_matrix_multiply(const phy_matrix *left,
                               const phy_matrix *right,
                               phy_matrix **out_matrix);
phy_status phy_matrix_transpose(const phy_matrix *matrix,
                                phy_matrix **out_matrix);

phy_status phy_matrix_determinant(const phy_matrix *matrix,
                                  phy_ir_ref *out_value);
phy_status phy_matrix_rref(const phy_matrix *matrix, phy_matrix **out_matrix,
                           size_t *out_rank);
phy_status phy_matrix_rank(const phy_matrix *matrix, size_t *out_rank);
phy_status phy_matrix_inverse(const phy_matrix *matrix,
                              phy_matrix **out_matrix);

/*
 * Solve A X = B exactly.  The first implementation requires square,
 * nonsingular A and accepts any positive number of right-hand sides.
 */
phy_status phy_matrix_solve(const phy_matrix *a, const phy_matrix *b,
                            phy_matrix **out_solution);

/*
 * Return a matrix whose columns are a deterministic null-space basis.
 * A full-column-rank input returns `*out_basis == NULL` and nullity zero.
 */
phy_status phy_matrix_null_space(const phy_matrix *matrix,
                                 phy_matrix **out_basis,
                                 size_t *out_nullity);

phy_status phy_vector_create(phy_cas *cas, size_t length,
                             const phy_ir_ref *entries,
                             const phy_linear_limits *limits,
                             phy_vector **out_vector);
size_t phy_vector_length(const phy_vector *vector);
phy_status phy_vector_get(const phy_vector *vector, size_t index,
                          phy_ir_ref *out_value);
phy_status phy_vector_set(phy_vector *vector, size_t index, phy_ir_ref value);
phy_status phy_vector_dot(const phy_vector *left, const phy_vector *right,
                          phy_ir_ref *out_value);

#ifdef __cplusplus
}
#endif

#endif /* PHY_LINEAR_H */

