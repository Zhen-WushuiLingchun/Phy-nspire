#include "abstract_internal.h"

#include <string.h>

static bool valid_commutation(phy_tensor_commutation commutation)
{
    return commutation == PHY_TENSOR_COMMUTING ||
           commutation == PHY_TENSOR_NONCOMMUTING;
}

phy_status phy_tensor_head_create(
    phy_abstract_context *context, const char *name,
    const phy_index_space *const *slot_spaces, size_t slot_count,
    phy_tensor_commutation commutation, phy_tensor_head **out_head)
{
    if (context == NULL || name == NULL || name[0] == '\0' ||
        out_head == NULL || !valid_commutation(commutation) ||
        (slot_count != 0u && slot_spaces == NULL)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_head = NULL;
    if (slot_count > context->limits.max_slots) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (context->head_count >= context->limits.max_heads) {
        return PHY_ERR_TERM_LIMIT;
    }
    const phy_ir_symbol symbol = phy_ir_intern(context->ir, name);
    if (symbol == PHY_IR_NO_SYMBOL) {
        return phy_ir_last_error(context->ir);
    }
    if (phy_abstract_name_used(context, symbol, true)) {
        return PHY_ERR_ALREADY_INITIALIZED;
    }
    for (size_t slot = 0u; slot < slot_count; ++slot) {
        if (slot_spaces[slot] == NULL ||
            slot_spaces[slot]->context != context) {
            return PHY_ERR_TYPE;
        }
    }
    if (slot_count > SIZE_MAX / sizeof(*slot_spaces)) {
        return PHY_ERR_MEMORY_LIMIT;
    }

    phy_tensor_head *head =
        phy_abstract_alloc(context, sizeof *head);
    if (head == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(head, 0, sizeof *head);
    head->context = context;
    head->symbol = symbol;
    head->slot_count = slot_count;
    head->commutation = commutation;
    head->slot_bytes = slot_count * sizeof(*head->slot_spaces);
    if (head->slot_bytes != 0u) {
        head->slot_spaces =
            phy_abstract_alloc(context, head->slot_bytes);
        if (head->slot_spaces == NULL) {
            phy_abstract_free(context, head, sizeof *head);
            return PHY_ERR_MEMORY_LIMIT;
        }
        memcpy(head->slot_spaces, slot_spaces, head->slot_bytes);
    }
    context->heads[context->head_count++] = head;
    *out_head = head;
    return PHY_OK;
}

void phy_abstract_head_destroy(phy_tensor_head *head)
{
    if (head == NULL) {
        return;
    }
    phy_abstract_context *context = head->context;
    for (size_t i = 0u; i < head->generator_count; ++i) {
        phy_abstract_free(context, head->generators[i].image,
                          head->generators[i].image_bytes);
    }
    phy_abstract_free(
        context, head->generators, head->generator_bytes);
    phy_abstract_free(context, head->slot_spaces, head->slot_bytes);
    phy_abstract_free(context, head, sizeof *head);
}

const char *phy_tensor_head_name(const phy_tensor_head *head)
{
    return head != NULL
               ? phy_ir_symbol_name(head->context->ir, head->symbol)
               : NULL;
}

phy_abstract_context *phy_tensor_head_context(
    const phy_tensor_head *head)
{
    return head != NULL ? head->context : NULL;
}

phy_ir_symbol phy_tensor_head_symbol(const phy_tensor_head *head)
{
    return head != NULL ? head->symbol : PHY_IR_NO_SYMBOL;
}

size_t phy_tensor_head_slot_count(const phy_tensor_head *head)
{
    return head != NULL ? head->slot_count : 0u;
}

const phy_index_space *phy_tensor_head_slot_space(const phy_tensor_head *head,
                                                  size_t slot)
{
    return head != NULL && slot < head->slot_count
               ? head->slot_spaces[slot]
               : NULL;
}

phy_tensor_commutation phy_tensor_head_commutation(
    const phy_tensor_head *head)
{
    return head != NULL ? head->commutation : PHY_TENSOR_COMMUTING;
}

static bool valid_image(const phy_tensor_head *head, const uint16_t *image)
{
    if (head->slot_count != 0u && image == NULL) {
        return false;
    }
    for (size_t i = 0u; i < head->slot_count; ++i) {
        if ((size_t)image[i] >= head->slot_count) {
            return false;
        }
        for (size_t prior = 0u; prior < i; ++prior) {
            if (image[prior] == image[i]) {
                return false;
            }
        }
        /*
         * A slot symmetry is an automorphism of the typed slot list.  Allowing
         * it to exchange different index spaces would make a well-typed head
         * produce an ill-typed application during canonicalization.
         */
        if (head->slot_spaces[i] != head->slot_spaces[image[i]]) {
            return false;
        }
    }
    return true;
}

static bool same_image(const phy_abstract_generator *generator,
                       const uint16_t *image, size_t slot_count)
{
    return slot_count == 0u ||
           memcmp(generator->image, image,
                  slot_count * sizeof(*image)) == 0;
}

phy_status phy_tensor_head_add_symmetry(phy_tensor_head *head,
                                        const uint16_t *image, int sign)
{
    if (head == NULL || (sign != -1 && sign != 1)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!valid_image(head, image)) {
        return PHY_ERR_TYPE;
    }
    for (size_t i = 0u; i < head->generator_count; ++i) {
        if (head->generators[i].sign == sign &&
            same_image(&head->generators[i], image, head->slot_count)) {
            return PHY_OK;
        }
    }
    if (head->generator_count >= head->context->limits.max_generators) {
        return PHY_ERR_TERM_LIMIT;
    }

    const size_t image_bytes = head->slot_count * sizeof(*image);
    uint16_t *stored_image = NULL;
    if (image_bytes != 0u) {
        stored_image =
            phy_abstract_alloc(head->context, image_bytes);
        if (stored_image == NULL) {
            return PHY_ERR_MEMORY_LIMIT;
        }
        memcpy(stored_image, image, image_bytes);
    }

    if (head->generator_count == head->generator_capacity) {
        size_t capacity = 4u;
        if (head->generator_capacity != 0u) {
            capacity =
                head->generator_capacity >
                        head->context->limits.max_generators / 2u
                    ? head->context->limits.max_generators
                    : head->generator_capacity * 2u;
        }
        if (capacity > head->context->limits.max_generators) {
            capacity = head->context->limits.max_generators;
        }
        if (capacity > SIZE_MAX / sizeof(*head->generators)) {
            phy_abstract_free(head->context, stored_image, image_bytes);
            return PHY_ERR_MEMORY_LIMIT;
        }
        const size_t bytes = capacity * sizeof(*head->generators);
        phy_abstract_generator *grown =
            phy_abstract_alloc(head->context, bytes);
        if (grown == NULL) {
            phy_abstract_free(head->context, stored_image, image_bytes);
            return PHY_ERR_MEMORY_LIMIT;
        }
        if (head->generator_count != 0u) {
            memcpy(grown, head->generators,
                   head->generator_count * sizeof(*head->generators));
        }
        phy_abstract_free(head->context, head->generators,
                          head->generator_bytes);
        head->generators = grown;
        head->generator_capacity = capacity;
        head->generator_bytes = bytes;
    }

    phy_abstract_generator *generator =
        &head->generators[head->generator_count++];
    generator->image = stored_image;
    generator->image_bytes = image_bytes;
    generator->sign = (int8_t)sign;
    return PHY_OK;
}

size_t phy_tensor_head_symmetry_count(const phy_tensor_head *head)
{
    return head != NULL ? head->generator_count : 0u;
}

phy_status phy_tensor_head_symmetry(const phy_tensor_head *head, size_t which,
                                    const uint16_t **out_image,
                                    int *out_sign)
{
    if (head == NULL || out_image == NULL || out_sign == NULL ||
        which >= head->generator_count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_image = head->generators[which].image;
    *out_sign = head->generators[which].sign;
    return PHY_OK;
}

phy_status phy_tensor_head_apply(const phy_tensor_head *head,
                                 const phy_abstract_index *indices,
                                 size_t index_count, phy_ir_ref *out_ref)
{
    if (head == NULL || out_ref == NULL ||
        index_count != head->slot_count ||
        (index_count != 0u && indices == NULL)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    const size_t refs_bytes = index_count * sizeof(phy_ir_ref);
    phy_ir_ref *refs = NULL;
    if (refs_bytes != 0u) {
        refs = phy_abstract_alloc(head->context, refs_bytes);
        if (refs == NULL) {
            return PHY_ERR_MEMORY_LIMIT;
        }
    }
    phy_status status = PHY_OK;
    for (size_t slot = 0u; slot < index_count; ++slot) {
        if (indices[slot].space != head->slot_spaces[slot] ||
            indices[slot].name == PHY_IR_NO_SYMBOL ||
            (indices[slot].variance != PHY_IR_INDEX_LOWER &&
             indices[slot].variance != PHY_IR_INDEX_UPPER)) {
            status = PHY_ERR_TYPE;
            break;
        }
        refs[slot] = phy_ir_index_in_space(
            head->context->ir, indices[slot].name,
            indices[slot].variance, indices[slot].space->symbol);
        if (refs[slot] == PHY_IR_NULL) {
            status = phy_ir_last_error(head->context->ir);
            break;
        }
    }
    if (status == PHY_OK) {
        *out_ref =
            head->commutation == PHY_TENSOR_COMMUTING
                ? phy_ir_tensor(
                      head->context->ir, head->symbol, refs, index_count)
                : phy_ir_operator(
                      head->context->ir, head->symbol, refs, index_count);
        if (*out_ref == PHY_IR_NULL) {
            status = phy_ir_last_error(head->context->ir);
        }
    }
    phy_abstract_free(head->context, refs, refs_bytes);
    return status;
}
