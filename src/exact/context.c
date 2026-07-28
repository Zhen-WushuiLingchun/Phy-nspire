/*
 * Bounded allocation, lifecycle, cancellation, and operation accounting for
 * the native exact-number domain.
 */
#include "exact_internal.h"

static bool checked_bytes(size_t count, size_t element, size_t *out_bytes)
{
    if (count != 0u && element > (size_t)-1 / count) {
        return false;
    }
    *out_bytes = count * element;
    return true;
}

static bool charge(phy_exact_context *context, size_t bytes)
{
    if (bytes > context->limits.max_bytes - context->bytes) {
        return false;
    }
    context->bytes += bytes;
    return true;
}

static void discharge(phy_exact_context *context, size_t bytes)
{
    context->bytes -= (bytes <= context->bytes) ? bytes : context->bytes;
}

void phy_exact_limits_defaults(phy_exact_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_limbs = 256u;
    out_limits->max_steps = 4000000u;
    out_limits->max_bytes = 256u * 1024u;
}

static phy_exact_limits resolve_limits(const phy_exact_limits *requested)
{
    phy_exact_limits defaults;
    phy_exact_limits_defaults(&defaults);
    phy_exact_limits resolved = requested != NULL ? *requested : defaults;
    if (resolved.max_limbs == 0u) {
        resolved.max_limbs = defaults.max_limbs;
    }
    if (resolved.max_steps == 0u) {
        resolved.max_steps = defaults.max_steps;
    }
    if (resolved.max_bytes == 0u) {
        resolved.max_bytes = defaults.max_bytes;
    }
    if (resolved.max_limbs > PHY_EXACT_MAX_LIMBS_CEILING) {
        resolved.max_limbs = PHY_EXACT_MAX_LIMBS_CEILING;
    }
    return resolved;
}

phy_exact_context *phy_exact_context_create(const phy_exact_limits *limits)
{
    const phy_exact_limits resolved = resolve_limits(limits);
    if (resolved.max_limbs == 0u || resolved.max_steps == 0u ||
        resolved.max_bytes < sizeof(phy_exact_context)) {
        return NULL;
    }
    phy_exact_context *context =
        (phy_exact_context *)phy_alloc(sizeof *context);
    if (context == NULL) {
        return NULL;
    }
    memset(context, 0, sizeof *context);
    context->limits = resolved;
    context->bytes = sizeof *context;
    context->magic = PHY_EXACT_CONTEXT_MAGIC;
    return context;
}

bool phy_exact_context_is_valid(const phy_exact_context *context)
{
    return context != NULL && context->magic == PHY_EXACT_CONTEXT_MAGIC;
}

void phy_exact_context_destroy(phy_exact_context *context)
{
    if (!phy_exact_context_is_valid(context)) {
        return;
    }
    phy_bigint *value = context->values;
    while (value != NULL) {
        phy_bigint *next = value->next;
        if (value->limbs != NULL) {
            const size_t bytes = value->capacity * sizeof(uint32_t);
            phy_free(value->limbs, bytes);
            discharge(context, bytes);
        }
        memset(value, 0, sizeof *value);
        value = next;
    }
    context->values = NULL;
    context->value_count = 0u;
    context->magic = 0u;
    phy_free(context, sizeof *context);
}

void phy_exact_set_cancel(phy_exact_context *context,
                          bool (*cancelled)(void *user), void *user)
{
    if (!phy_exact_context_is_valid(context)) {
        return;
    }
    context->cancelled = cancelled;
    context->cancel_user = user;
}

uint32_t phy_exact_steps(const phy_exact_context *context)
{
    return phy_exact_context_is_valid(context) ? context->steps : 0u;
}

uint64_t phy_exact_total_steps(const phy_exact_context *context)
{
    return phy_exact_context_is_valid(context) ? context->total_steps : 0u;
}

size_t phy_exact_bytes_used(const phy_exact_context *context)
{
    return phy_exact_context_is_valid(context) ? context->bytes : 0u;
}

bool phy_bigint_handle_valid(const phy_bigint *value)
{
    if (value == NULL || value->private_magic != PHY_BIGINT_MAGIC ||
        !phy_exact_context_is_valid(value->context)) {
        return false;
    }
    return true;
}

bool phy_bigint_shallow_valid(const phy_bigint *value)
{
    if (!phy_bigint_handle_valid(value) ||
        value->count > value->capacity ||
        value->capacity > value->context->limits.max_limbs ||
        (value->capacity == 0u) != (value->limbs == NULL) ||
        value->sign < -1 || value->sign > 1) {
        return false;
    }
    if (value->count == 0u) {
        return value->sign == 0;
    }
    return value->sign != 0 && value->limbs[value->count - 1u] != 0u;
}

phy_status phy_exact_validate(const phy_exact_context *context)
{
    if (!phy_exact_context_is_valid(context) ||
        context->bytes < sizeof *context ||
        context->bytes > context->limits.max_bytes) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    size_t count = 0u;
    size_t bytes = sizeof *context;
    const phy_bigint *previous = NULL;
    for (const phy_bigint *value = context->values; value != NULL;
         value = value->next) {
        if (!phy_bigint_shallow_valid(value) ||
            value->previous != previous ||
            value->context != context ||
            count >= context->value_count) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        size_t limb_bytes = 0u;
        if (!checked_bytes(value->capacity, sizeof(uint32_t), &limb_bytes) ||
            limb_bytes > (size_t)-1 - bytes) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        bytes += limb_bytes;
        previous = value;
        count++;
    }
    return count == context->value_count && bytes == context->bytes
               ? PHY_OK
               : PHY_ERR_CORRUPT_DOCUMENT;
}

phy_status phy_exact_operation_begin(phy_exact_context *context)
{
    if (!phy_exact_context_is_valid(context)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (context->operation_depth == 0u) {
        context->steps = 0u;
    }
    context->operation_depth++;
    return PHY_OK;
}

phy_status phy_exact_operation_end(phy_exact_context *context,
                                   phy_status status)
{
    if (phy_exact_context_is_valid(context) &&
        context->operation_depth != 0u) {
        context->operation_depth--;
    }
    return status;
}

phy_status phy_exact_step(phy_exact_context *context, uint32_t amount)
{
    if (!phy_exact_context_is_valid(context)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (amount > context->limits.max_steps - context->steps) {
        return PHY_ERR_TIMEOUT;
    }
    context->steps += amount;
    context->total_steps += amount;
    if (context->cancelled != NULL &&
        context->cancelled(context->cancel_user)) {
        return PHY_ERR_INTERRUPTED;
    }
    return PHY_OK;
}

phy_status phy_exact_temp_alloc(phy_exact_context *context, size_t bytes,
                                void **out_pointer)
{
    if (!phy_exact_context_is_valid(context) || out_pointer == NULL ||
        bytes == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_pointer = NULL;
    if (!charge(context, bytes)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    void *pointer = phy_alloc(bytes);
    if (pointer == NULL) {
        discharge(context, bytes);
        return PHY_ERR_OUT_OF_MEMORY;
    }
    *out_pointer = pointer;
    return PHY_OK;
}

void phy_exact_temp_free(phy_exact_context *context, void *pointer,
                         size_t bytes)
{
    if (!phy_exact_context_is_valid(context) || pointer == NULL ||
        bytes == 0u) {
        return;
    }
    phy_free(pointer, bytes);
    discharge(context, bytes);
}

phy_status phy_bigint_init(phy_exact_context *context, phy_bigint *value)
{
    if (!phy_exact_context_is_valid(context) || value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(value, 0, sizeof *value);
    value->context = context;
    value->private_magic = PHY_BIGINT_MAGIC;
    value->next = context->values;
    if (context->values != NULL) {
        context->values->previous = value;
    }
    context->values = value;
    context->value_count++;
    return PHY_OK;
}

bool phy_bigint_is_initialized(const phy_bigint *value)
{
    return phy_bigint_handle_valid(value);
}

phy_status phy_bigint_validate(const phy_bigint *value)
{
    return phy_bigint_shallow_valid(value) ? PHY_OK
                                           : PHY_ERR_CORRUPT_DOCUMENT;
}

void phy_bigint_destroy(phy_bigint *value)
{
    /*
     * Destruction deliberately checks the handle, not the normalized numeric
     * invariant. A cancelled or failed in-place helper may leave a temporary
     * between limb updates; it must still be unlinked and freed.
     */
    if (!phy_bigint_handle_valid(value)) {
        return;
    }
    phy_exact_context *context = value->context;
    if (value->previous != NULL) {
        value->previous->next = value->next;
    } else {
        context->values = value->next;
    }
    if (value->next != NULL) {
        value->next->previous = value->previous;
    }
    if (value->limbs != NULL) {
        const size_t bytes = value->capacity * sizeof(uint32_t);
        phy_free(value->limbs, bytes);
        discharge(context, bytes);
    }
    context->value_count--;
    memset(value, 0, sizeof *value);
}

phy_status phy_bigint_reserve(phy_bigint *value, size_t needed)
{
    if (!phy_bigint_shallow_valid(value)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (needed <= value->capacity) {
        return PHY_OK;
    }
    phy_exact_context *context = value->context;
    if (needed > context->limits.max_limbs) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    size_t capacity = value->capacity != 0u ? value->capacity : 1u;
    while (capacity < needed) {
        const size_t doubled =
            capacity <= context->limits.max_limbs / 2u
                ? capacity * 2u
                : context->limits.max_limbs;
        if (doubled == capacity) {
            return PHY_ERR_MEMORY_LIMIT;
        }
        capacity = doubled;
    }
    size_t bytes = 0u;
    if (!checked_bytes(capacity, sizeof(uint32_t), &bytes) ||
        !charge(context, bytes)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    uint32_t *limbs = (uint32_t *)phy_alloc(bytes);
    if (limbs == NULL) {
        discharge(context, bytes);
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(limbs, 0, bytes);
    if (value->count != 0u) {
        memcpy(limbs, value->limbs, value->count * sizeof(uint32_t));
    }
    if (value->limbs != NULL) {
        const size_t old_bytes = value->capacity * sizeof(uint32_t);
        phy_free(value->limbs, old_bytes);
        discharge(context, old_bytes);
    }
    value->limbs = limbs;
    value->capacity = capacity;
    return PHY_OK;
}

void phy_bigint_normalize(phy_bigint *value)
{
    while (value->count != 0u && value->limbs[value->count - 1u] == 0u) {
        value->count--;
    }
    if (value->count == 0u) {
        value->sign = 0;
    }
}

void phy_bigint_swap_payload(phy_bigint *left, phy_bigint *right)
{
    uint32_t *limbs = left->limbs;
    const size_t count = left->count;
    const size_t capacity = left->capacity;
    const int sign = left->sign;
    left->limbs = right->limbs;
    left->count = right->count;
    left->capacity = right->capacity;
    left->sign = right->sign;
    right->limbs = limbs;
    right->count = count;
    right->capacity = capacity;
    right->sign = sign;
}
