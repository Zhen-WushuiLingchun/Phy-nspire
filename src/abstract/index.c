#include "abstract_internal.h"

#include <limits.h>
#include <string.h>

#define PHY_ABSTRACT_DEFAULT_SPACES 32u
#define PHY_ABSTRACT_DEFAULT_HEADS 128u
#define PHY_ABSTRACT_DEFAULT_SLOTS 64u
#define PHY_ABSTRACT_DEFAULT_GENERATORS 256u
#define PHY_ABSTRACT_DEFAULT_BYTES (512u * 1024u)

void phy_abstract_limits_defaults(phy_abstract_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_spaces = PHY_ABSTRACT_DEFAULT_SPACES;
    out_limits->max_heads = PHY_ABSTRACT_DEFAULT_HEADS;
    out_limits->max_slots = PHY_ABSTRACT_DEFAULT_SLOTS;
    out_limits->max_generators = PHY_ABSTRACT_DEFAULT_GENERATORS;
    out_limits->max_bytes = PHY_ABSTRACT_DEFAULT_BYTES;
}

phy_status phy_abstract_resolve_limits(const phy_abstract_limits *requested,
                                       phy_abstract_limits *out)
{
    if (out == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_abstract_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_spaces != 0u) {
            out->max_spaces = requested->max_spaces;
        }
        if (requested->max_heads != 0u) {
            out->max_heads = requested->max_heads;
        }
        if (requested->max_slots != 0u) {
            out->max_slots = requested->max_slots;
        }
        if (requested->max_generators != 0u) {
            out->max_generators = requested->max_generators;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
    }
    if (out->max_spaces == 0u || out->max_heads == 0u ||
        out->max_slots == 0u || out->max_slots > UINT16_MAX ||
        out->max_generators == 0u ||
        out->max_bytes < sizeof(phy_abstract_context)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (out->max_spaces > SIZE_MAX / sizeof(phy_index_space *) ||
        out->max_heads > SIZE_MAX / sizeof(phy_tensor_head *)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    return PHY_OK;
}

void *phy_abstract_alloc(phy_abstract_context *context, size_t bytes)
{
    if (context == NULL || bytes == 0u ||
        bytes > context->limits.max_bytes - context->bytes_used) {
        return NULL;
    }
    void *memory = phy_alloc(bytes);
    if (memory != NULL) {
        context->bytes_used += bytes;
    }
    return memory;
}

void phy_abstract_free(phy_abstract_context *context, void *pointer,
                       size_t bytes)
{
    if (context == NULL || pointer == NULL) {
        return;
    }
    phy_free(pointer, bytes);
    context->bytes_used -= bytes;
}

phy_status phy_abstract_context_create(phy_cas *cas,
                                       const phy_abstract_limits *limits,
                                       phy_abstract_context **out_context)
{
    if (cas == NULL || out_context == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_context = NULL;
    phy_abstract_limits resolved;
    phy_status status = phy_abstract_resolve_limits(limits, &resolved);
    if (status != PHY_OK) {
        return status;
    }

    phy_abstract_context *context = phy_alloc(sizeof *context);
    if (context == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(context, 0, sizeof *context);
    context->cas = cas;
    context->ir = phy_cas_ir(cas);
    context->limits = resolved;
    context->bytes_used = sizeof *context;
    context->space_array_bytes =
        resolved.max_spaces * sizeof(*context->spaces);
    context->head_array_bytes =
        resolved.max_heads * sizeof(*context->heads);

    context->spaces =
        phy_abstract_alloc(context, context->space_array_bytes);
    if (context->spaces != NULL) {
        memset(context->spaces, 0, context->space_array_bytes);
        context->heads =
            phy_abstract_alloc(context, context->head_array_bytes);
    }
    if (context->heads == NULL) {
        phy_abstract_context_destroy(context);
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(context->heads, 0, context->head_array_bytes);
    *out_context = context;
    return PHY_OK;
}

void phy_abstract_context_destroy(phy_abstract_context *context)
{
    if (context == NULL) {
        return;
    }
    for (size_t i = 0u; i < context->head_count; ++i) {
        phy_abstract_head_destroy(context->heads[i]);
    }
    for (size_t i = 0u; i < context->space_count; ++i) {
        phy_abstract_free(
            context, context->spaces[i], sizeof(*context->spaces[i]));
    }
    phy_abstract_free(
        context, context->heads, context->head_array_bytes);
    phy_abstract_free(
        context, context->spaces, context->space_array_bytes);
    phy_free(context, sizeof *context);
}

phy_cas *phy_abstract_cas(const phy_abstract_context *context)
{
    return context != NULL ? context->cas : NULL;
}

size_t phy_abstract_space_count(const phy_abstract_context *context)
{
    return context != NULL ? context->space_count : 0u;
}

size_t phy_abstract_head_count(const phy_abstract_context *context)
{
    return context != NULL ? context->head_count : 0u;
}

size_t phy_abstract_bytes_used(const phy_abstract_context *context)
{
    return context != NULL ? context->bytes_used : 0u;
}

bool phy_abstract_name_used(const phy_abstract_context *context,
                            phy_ir_symbol symbol, bool heads)
{
    if (context == NULL || symbol == PHY_IR_NO_SYMBOL) {
        return false;
    }
    if (heads) {
        for (size_t i = 0u; i < context->head_count; ++i) {
            if (context->heads[i]->symbol == symbol) {
                return true;
            }
        }
    } else {
        for (size_t i = 0u; i < context->space_count; ++i) {
            if (context->spaces[i]->symbol == symbol) {
                return true;
            }
        }
    }
    return false;
}

static phy_status validate_dimension(phy_ir_context *ir, phy_ir_ref dimension)
{
    if (dimension == PHY_IR_NULL) {
        return PHY_OK;
    }
    const phy_ir_kind kind = phy_ir_kind_of(ir, dimension);
    if (kind == PHY_IR_SYMBOL) {
        return PHY_OK;
    }
    if (kind != PHY_IR_INTEGER) {
        return PHY_ERR_TYPE;
    }
    int64_t value = 0;
    if (!phy_ir_integer_value(ir, dimension, &value)) {
        return PHY_ERR_UNSUPPORTED;
    }
    return value > 0 ? PHY_OK : PHY_ERR_DOMAIN;
}

phy_status phy_index_space_create(phy_abstract_context *context,
                                  const char *name, phy_ir_ref dimension,
                                  phy_metric_symmetry metric,
                                  phy_index_space **out_space)
{
    if (context == NULL || name == NULL || name[0] == '\0' ||
        out_space == NULL || metric < PHY_METRIC_NONE ||
        metric > PHY_METRIC_ANTISYMMETRIC) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_space = NULL;
    phy_status status = validate_dimension(context->ir, dimension);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_symbol symbol = phy_ir_intern(context->ir, name);
    if (symbol == PHY_IR_NO_SYMBOL) {
        return phy_ir_last_error(context->ir);
    }
    if (phy_abstract_name_used(context, symbol, false)) {
        return PHY_ERR_ALREADY_INITIALIZED;
    }
    if (context->space_count >= context->limits.max_spaces) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_index_space *space =
        phy_abstract_alloc(context, sizeof *space);
    if (space == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    space->context = context;
    space->symbol = symbol;
    space->dimension = dimension;
    space->metric = metric;
    context->spaces[context->space_count++] = space;
    *out_space = space;
    return PHY_OK;
}

const char *phy_index_space_name(const phy_index_space *space)
{
    return space != NULL
               ? phy_ir_symbol_name(space->context->ir, space->symbol)
               : NULL;
}

phy_ir_symbol phy_index_space_symbol(const phy_index_space *space)
{
    return space != NULL ? space->symbol : PHY_IR_NO_SYMBOL;
}

phy_ir_ref phy_index_space_dimension(const phy_index_space *space)
{
    return space != NULL ? space->dimension : PHY_IR_NULL;
}

bool phy_index_space_known_dimension(const phy_index_space *space,
                                     size_t *out_dimension)
{
    if (space == NULL || out_dimension == NULL) {
        return false;
    }
    int64_t dimension = 0;
    if (!phy_ir_integer_value(
            space->context->ir, space->dimension, &dimension) ||
        dimension <= 0 ||
        (uint64_t)dimension > (uint64_t)SIZE_MAX) {
        return false;
    }
    *out_dimension = (size_t)dimension;
    return true;
}

phy_metric_symmetry phy_index_space_metric(const phy_index_space *space)
{
    return space != NULL ? space->metric : PHY_METRIC_NONE;
}

phy_status phy_abstract_index_make(const phy_index_space *space,
                                   const char *name,
                                   phy_ir_variance variance,
                                   phy_abstract_index *out_index)
{
    if (space == NULL || name == NULL || name[0] == '\0' ||
        out_index == NULL ||
        (variance != PHY_IR_INDEX_LOWER &&
         variance != PHY_IR_INDEX_UPPER)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_ir_symbol symbol =
        phy_ir_intern(space->context->ir, name);
    if (symbol == PHY_IR_NO_SYMBOL) {
        return phy_ir_last_error(space->context->ir);
    }
    out_index->space = space;
    out_index->name = symbol;
    out_index->variance = variance;
    return PHY_OK;
}

