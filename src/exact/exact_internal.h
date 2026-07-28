/*
 * Internal contracts shared by the exact integer/rational translation units.
 * Nothing outside src/exact may include this header.
 */
#ifndef PHY_EXACT_INTERNAL_H
#define PHY_EXACT_INTERNAL_H

#include <string.h>

#include "phy/exact.h"
#include "phy/platform.h"

#define PHY_EXACT_CONTEXT_MAGIC 0x45584354u /* EXCT */
#define PHY_BIGINT_MAGIC 0x42494749u       /* BIGI */
#define PHY_BIGRAT_MAGIC 0x42524154u       /* BRAT */
#define PHY_EXACT_MAX_LIMBS_CEILING 1048576u

struct phy_exact_context {
    phy_exact_limits limits;
    size_t bytes;
    uint32_t steps;
    uint64_t total_steps;
    unsigned operation_depth;
    bool (*cancelled)(void *user);
    void *cancel_user;
    phy_bigint *values;
    size_t value_count;
    uint32_t magic;
};

bool phy_exact_context_is_valid(const phy_exact_context *context);
phy_status phy_exact_operation_begin(phy_exact_context *context);
phy_status phy_exact_operation_end(phy_exact_context *context,
                                   phy_status status);
phy_status phy_exact_step(phy_exact_context *context, uint32_t amount);

phy_status phy_exact_temp_alloc(phy_exact_context *context, size_t bytes,
                                void **out_pointer);
void phy_exact_temp_free(phy_exact_context *context, void *pointer,
                         size_t bytes);

phy_status phy_bigint_reserve(phy_bigint *value, size_t needed);
void phy_bigint_normalize(phy_bigint *value);
void phy_bigint_swap_payload(phy_bigint *left, phy_bigint *right);
bool phy_bigint_handle_valid(const phy_bigint *value);
bool phy_bigint_shallow_valid(const phy_bigint *value);

phy_status phy_bigint_assign_u64(phy_bigint *value, uint64_t magnitude,
                                 int sign);
phy_status phy_bigint_copy_magnitude(const phy_bigint *source,
                                     phy_bigint *destination);

#endif /* PHY_EXACT_INTERNAL_H */
