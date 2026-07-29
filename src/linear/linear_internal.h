#ifndef PHY_LINEAR_INTERNAL_H
#define PHY_LINEAR_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/linear.h"
#include "phy/platform.h"

struct phy_matrix {
    phy_cas *cas;
    size_t rows;
    size_t columns;
    size_t count;
    size_t entries_bytes;
    phy_linear_limits limits;
    phy_ir_ref *entries;
};

typedef struct {
    uint32_t used;
    uint32_t maximum;
} phy_linear_budget;

phy_status phy_linear_resolve_limits(const phy_linear_limits *requested,
                                     phy_linear_limits *out);
phy_status phy_linear_checked_shape(size_t rows, size_t columns,
                                    const phy_linear_limits *limits,
                                    size_t *out_count,
                                    size_t *out_entries_bytes);
phy_status phy_linear_allocate(phy_cas *cas, size_t rows, size_t columns,
                               const phy_linear_limits *limits,
                               phy_matrix **out_matrix);
phy_status phy_linear_compatible(const phy_matrix *left,
                                 const phy_matrix *right);
phy_status phy_linear_step(phy_linear_budget *budget, uint32_t amount);
phy_status phy_linear_zero(phy_cas *cas, phy_ir_ref *out);
phy_status phy_linear_one(phy_cas *cas, phy_ir_ref *out);
phy_status phy_linear_is_zero(phy_cas *cas, phy_ir_ref value,
                              phy_cas_decision *out);

/*
 * In-place Gauss-Jordan reduction of a row-major array.  `pivot_columns` may
 * be NULL; otherwise it receives one column per pivot row.
 */
phy_status phy_linear_rref_array(phy_cas *cas, phy_ir_ref *entries,
                                 size_t rows, size_t columns,
                                 size_t pivot_column_limit,
                                 phy_linear_budget *budget,
                                 size_t *pivot_columns, size_t *out_rank);

#endif /* PHY_LINEAR_INTERNAL_H */

