#ifndef PHY_ABSTRACT_INTERNAL_H
#define PHY_ABSTRACT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "phy/abstract_tensor.h"
#include "phy/platform.h"

typedef struct {
    uint16_t *image;
    size_t image_bytes;
    int8_t sign;
} phy_abstract_generator;

typedef struct {
    const phy_abstract_tensor_head *head;
    size_t index_offset;
    size_t index_count;
} phy_abstract_factor_record;

struct phy_abstract_context {
    phy_cas *cas;
    phy_ir_context *ir;
    phy_abstract_limits limits;
    size_t bytes_used;
    phy_index_space **spaces;
    phy_abstract_tensor_head **heads;
    size_t space_array_bytes;
    size_t head_array_bytes;
    size_t space_count;
    size_t head_count;
    phy_tensor_monomial *monomials;
    phy_tensor_expression *expressions;
};

struct phy_tensor_monomial {
    phy_abstract_context *context;
    phy_tensor_monomial *previous;
    phy_tensor_monomial *next;
    bool linked;
    phy_ir_ref coefficient;
    phy_abstract_factor_record *factors;
    phy_abstract_index *indices;
    phy_abstract_index_use *uses;
    size_t factor_count;
    size_t index_count;
    size_t use_count;
    size_t free_count;
    size_t dummy_count;
    size_t factor_bytes;
    size_t index_bytes;
    size_t use_bytes;
};

struct phy_tensor_expression {
    phy_abstract_context *context;
    phy_tensor_expression *previous;
    phy_tensor_expression *next;
    bool linked;
    phy_tensor_monomial **terms;
    phy_abstract_index_use *free_uses;
    size_t term_count;
    size_t term_capacity;
    size_t term_bytes;
    size_t free_count;
    size_t free_bytes;
};

struct phy_index_space {
    phy_abstract_context *context;
    phy_ir_symbol symbol;
    phy_ir_ref dimension;
    phy_metric_symmetry metric;
};

struct phy_abstract_tensor_head {
    phy_abstract_context *context;
    phy_ir_symbol symbol;
    size_t slot_count;
    const phy_index_space **slot_spaces;
    size_t slot_bytes;
    phy_tensor_commutation commutation;
    phy_abstract_generator *generators;
    size_t generator_count;
    size_t generator_capacity;
    size_t generator_bytes;
};

phy_status phy_abstract_resolve_limits(const phy_abstract_limits *requested,
                                       phy_abstract_limits *out);
void *phy_abstract_alloc(phy_abstract_context *context, size_t bytes);
void phy_abstract_free(phy_abstract_context *context, void *pointer,
                       size_t bytes);
bool phy_abstract_name_used(const phy_abstract_context *context,
                            phy_ir_symbol symbol, bool heads);
void phy_abstract_head_destroy(phy_abstract_tensor_head *head);

#endif /* PHY_ABSTRACT_INTERNAL_H */
