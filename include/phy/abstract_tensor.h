/*
 * Phy-nspire — coordinate-independent tensor object model.
 *
 * This layer records typed index spaces, tensor heads and signed slot
 * generators.  It never allocates coordinate components; rank and dimension
 * are runtime metadata bounded by explicit resource limits.
 */
#ifndef PHY_ABSTRACT_TENSOR_H
#define PHY_ABSTRACT_TENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_abstract_context phy_abstract_context;
typedef struct phy_index_space phy_index_space;
typedef struct phy_tensor_head phy_tensor_head;
typedef struct phy_tensor_monomial phy_tensor_monomial;
typedef struct phy_tensor_expression phy_tensor_expression;

typedef enum {
    PHY_METRIC_NONE = 0,
    PHY_METRIC_SYMMETRIC,
    PHY_METRIC_ANTISYMMETRIC
} phy_metric_symmetry;

typedef enum {
    PHY_TENSOR_COMMUTING = 0,
    PHY_TENSOR_NONCOMMUTING
} phy_tensor_commutation;

typedef struct {
    size_t max_spaces;     /* default 32 */
    size_t max_heads;      /* default 128 */
    size_t max_slots;      /* slots in one factor; default 64 */
    size_t max_generators; /* declared generators per head; default 256 */
    size_t max_bytes;      /* persistent abstract metadata; default 512 KiB */
} phy_abstract_limits;

void phy_abstract_limits_defaults(phy_abstract_limits *out_limits);
phy_status phy_abstract_context_create(phy_cas *cas,
                                       const phy_abstract_limits *limits,
                                       phy_abstract_context **out_context);
void phy_abstract_context_destroy(phy_abstract_context *context);

phy_cas *phy_abstract_cas(const phy_abstract_context *context);
size_t phy_abstract_space_count(const phy_abstract_context *context);
size_t phy_abstract_head_count(const phy_abstract_context *context);
size_t phy_abstract_bytes_used(const phy_abstract_context *context);

/*
 * `dimension` is PHY_IR_NULL (unknown), a positive exact integer, or a symbol.
 * Symbolic dimensions remain abstract until a component basis is supplied.
 */
phy_status phy_index_space_create(phy_abstract_context *context,
                                  const char *name, phy_ir_ref dimension,
                                  phy_metric_symmetry metric,
                                  phy_index_space **out_space);
const char *phy_index_space_name(const phy_index_space *space);
phy_ir_symbol phy_index_space_symbol(const phy_index_space *space);
phy_ir_ref phy_index_space_dimension(const phy_index_space *space);
bool phy_index_space_known_dimension(const phy_index_space *space,
                                     size_t *out_dimension);
phy_metric_symmetry phy_index_space_metric(const phy_index_space *space);

/*
 * A head owns only slot metadata and symmetry generators.  It has no component
 * table.  `slot_spaces` contains one borrowed space from the same context per
 * slot and may be NULL only at rank zero.
 */
phy_status phy_tensor_head_create(
    phy_abstract_context *context, const char *name,
    const phy_index_space *const *slot_spaces, size_t slot_count,
    phy_tensor_commutation commutation, phy_tensor_head **out_head);
const char *phy_tensor_head_name(const phy_tensor_head *head);
phy_ir_symbol phy_tensor_head_symbol(const phy_tensor_head *head);
size_t phy_tensor_head_slot_count(const phy_tensor_head *head);
const phy_index_space *phy_tensor_head_slot_space(const phy_tensor_head *head,
                                                  size_t slot);
phy_tensor_commutation phy_tensor_head_commutation(
    const phy_tensor_head *head);

/*
 * Add a signed generator in image notation: image[i] is the destination of
 * slot i.  `sign` is +1 or -1.  The full symmetry group is deliberately not
 * enumerated here; the bounded BSGS layer consumes these generators.
 */
phy_status phy_tensor_head_add_symmetry(phy_tensor_head *head,
                                        const uint16_t *image, int sign);
size_t phy_tensor_head_symmetry_count(const phy_tensor_head *head);
phy_status phy_tensor_head_symmetry(const phy_tensor_head *head, size_t which,
                                    const uint16_t **out_image,
                                    int *out_sign);

typedef struct {
    const phy_index_space *space;
    phy_ir_symbol name;
    phy_ir_variance variance;
} phy_abstract_index;

phy_status phy_abstract_index_make(const phy_index_space *space,
                                   const char *name,
                                   phy_ir_variance variance,
                                   phy_abstract_index *out_index);

/*
 * Validate slot spaces and lower this one abstract factor to the typed IR.
 * This is structural lowering, not component expansion.
 */
phy_status phy_tensor_head_apply(const phy_tensor_head *head,
                                 const phy_abstract_index *indices,
                                 size_t index_count, phy_ir_ref *out_ref);

#ifdef __cplusplus
}
#endif

#endif /* PHY_ABSTRACT_TENSOR_H */

