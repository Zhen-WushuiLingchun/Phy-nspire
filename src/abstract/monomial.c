#include "abstract_internal.h"

#include <string.h>

static bool scalar_expression(const phy_ir_context *ir, phy_ir_ref ref,
                              unsigned depth)
{
    if (ref == PHY_IR_NULL || depth > 512u) {
        return false;
    }
    const phy_ir_kind kind = phy_ir_kind_of(ir, ref);
    if (kind == PHY_IR_KIND_INVALID || kind == PHY_IR_INDEX ||
        kind == PHY_IR_ERROR || kind == PHY_IR_TENSOR ||
        kind == PHY_IR_OPERATOR || kind == PHY_IR_WEDGE) {
        return false;
    }
    const size_t count = phy_ir_child_count(ir, ref);
    for (size_t i = 0u; i < count; ++i) {
        if (!scalar_expression(ir, phy_ir_child(ir, ref, i), depth + 1u)) {
            return false;
        }
    }
    return true;
}

static void unlink_monomial(phy_tensor_monomial *monomial)
{
    if (!monomial->linked) {
        return;
    }
    if (monomial->previous != NULL) {
        monomial->previous->next = monomial->next;
    } else {
        monomial->context->monomials = monomial->next;
    }
    if (monomial->next != NULL) {
        monomial->next->previous = monomial->previous;
    }
    monomial->linked = false;
}

void phy_tensor_monomial_destroy(phy_tensor_monomial *monomial)
{
    if (monomial == NULL) {
        return;
    }
    phy_abstract_context *context = monomial->context;
    unlink_monomial(monomial);
    phy_abstract_free(
        context, monomial->uses, monomial->use_bytes);
    phy_abstract_free(
        context, monomial->indices, monomial->index_bytes);
    phy_abstract_free(
        context, monomial->factors, monomial->factor_bytes);
    phy_abstract_free(context, monomial, sizeof *monomial);
}

static phy_status count_indices(
    const phy_abstract_context *context,
    const phy_abstract_factor *factors, size_t factor_count,
    size_t *out_count)
{
    size_t count = 0u;
    for (size_t factor = 0u; factor < factor_count; ++factor) {
        const phy_tensor_head *head = factors[factor].head;
        if (head == NULL || head->context != context ||
            factors[factor].index_count != head->slot_count ||
            (head->slot_count != 0u &&
             factors[factor].indices == NULL)) {
            return PHY_ERR_TYPE;
        }
        if (count > SIZE_MAX - factors[factor].index_count) {
            return PHY_ERR_TERM_LIMIT;
        }
        count += factors[factor].index_count;
    }
    if (count > context->limits.max_indices) {
        return PHY_ERR_TERM_LIMIT;
    }
    *out_count = count;
    return PHY_OK;
}

static phy_status allocate_arrays(phy_tensor_monomial *monomial)
{
    phy_abstract_context *context = monomial->context;
    if (monomial->factor_count >
            SIZE_MAX / sizeof(*monomial->factors) ||
        monomial->index_count >
            SIZE_MAX / sizeof(*monomial->indices) ||
        monomial->index_count > SIZE_MAX / sizeof(*monomial->uses)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    monomial->factor_bytes =
        monomial->factor_count * sizeof(*monomial->factors);
    monomial->index_bytes =
        monomial->index_count * sizeof(*monomial->indices);
    monomial->use_bytes =
        monomial->index_count * sizeof(*monomial->uses);
    if (monomial->factor_bytes != 0u) {
        monomial->factors =
            phy_abstract_alloc(context, monomial->factor_bytes);
        if (monomial->factors == NULL) {
            return PHY_ERR_MEMORY_LIMIT;
        }
    }
    if (monomial->index_bytes != 0u) {
        monomial->indices =
            phy_abstract_alloc(context, monomial->index_bytes);
        if (monomial->indices == NULL) {
            return PHY_ERR_MEMORY_LIMIT;
        }
        monomial->uses =
            phy_abstract_alloc(context, monomial->use_bytes);
        if (monomial->uses == NULL) {
            return PHY_ERR_MEMORY_LIMIT;
        }
        memset(monomial->uses, 0, monomial->use_bytes);
    }
    return PHY_OK;
}

static phy_status copy_factors(phy_tensor_monomial *monomial,
                               const phy_abstract_factor *factors)
{
    size_t offset = 0u;
    for (size_t factor = 0u; factor < monomial->factor_count; ++factor) {
        monomial->factors[factor].head = factors[factor].head;
        monomial->factors[factor].index_offset = offset;
        monomial->factors[factor].index_count =
            factors[factor].index_count;
        for (size_t slot = 0u; slot < factors[factor].index_count; ++slot) {
            const phy_abstract_index index = factors[factor].indices[slot];
            if (index.space !=
                    factors[factor].head->slot_spaces[slot] ||
                index.name == PHY_IR_NO_SYMBOL ||
                (index.variance != PHY_IR_INDEX_LOWER &&
                 index.variance != PHY_IR_INDEX_UPPER)) {
                return PHY_ERR_TYPE;
            }
            monomial->indices[offset++] = index;
        }
    }
    return PHY_OK;
}

static size_t find_use(const phy_tensor_monomial *monomial,
                       const phy_abstract_index *index)
{
    for (size_t i = 0u; i < monomial->use_count; ++i) {
        if (monomial->uses[i].space == index->space &&
            monomial->uses[i].name == index->name) {
            return i;
        }
    }
    return SIZE_MAX;
}

static phy_status build_census(phy_tensor_monomial *monomial)
{
    for (size_t i = 0u; i < monomial->index_count; ++i) {
        const phy_abstract_index *index = &monomial->indices[i];
        size_t found = find_use(monomial, index);
        if (found == SIZE_MAX) {
            found = monomial->use_count++;
            monomial->uses[found].space = index->space;
            monomial->uses[found].name = index->name;
        }
        phy_abstract_index_use *use = &monomial->uses[found];
        uint16_t *count =
            index->variance == PHY_IR_INDEX_LOWER
                ? &use->lower_count
                : &use->upper_count;
        if (*count == UINT16_MAX) {
            return PHY_ERR_TERM_LIMIT;
        }
        (*count)++;
    }

    for (size_t i = 0u; i < monomial->use_count; ++i) {
        phy_abstract_index_use *use = &monomial->uses[i];
        const unsigned total =
            (unsigned)use->lower_count + (unsigned)use->upper_count;
        if (total == 1u) {
            use->role = PHY_ABSTRACT_INDEX_FREE;
            monomial->free_count++;
        } else if (total == 2u && use->lower_count == 1u &&
                   use->upper_count == 1u) {
            use->role = PHY_ABSTRACT_INDEX_DUMMY;
            monomial->dummy_count++;
        } else {
            return PHY_ERR_TYPE;
        }
    }
    return PHY_OK;
}

phy_status phy_tensor_monomial_create(
    phy_abstract_context *context, phy_ir_ref coefficient,
    const phy_abstract_factor *factors, size_t factor_count,
    phy_tensor_monomial **out_monomial)
{
    if (context == NULL || coefficient == PHY_IR_NULL ||
        out_monomial == NULL ||
        (factor_count != 0u && factors == NULL)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_monomial = NULL;
    if (factor_count > context->limits.max_factors) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (!scalar_expression(context->ir, coefficient, 0u)) {
        return PHY_ERR_TYPE;
    }

    size_t index_count = 0u;
    phy_status status =
        count_indices(context, factors, factor_count, &index_count);
    if (status != PHY_OK) {
        return status;
    }
    phy_tensor_monomial *monomial =
        phy_abstract_alloc(context, sizeof *monomial);
    if (monomial == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(monomial, 0, sizeof *monomial);
    monomial->context = context;
    monomial->coefficient = coefficient;
    monomial->factor_count = factor_count;
    monomial->index_count = index_count;
    status = allocate_arrays(monomial);
    if (status == PHY_OK) {
        status = copy_factors(monomial, factors);
    }
    if (status == PHY_OK) {
        status = build_census(monomial);
    }
    if (status != PHY_OK) {
        phy_tensor_monomial_destroy(monomial);
        return status;
    }

    monomial->next = context->monomials;
    if (monomial->next != NULL) {
        monomial->next->previous = monomial;
    }
    context->monomials = monomial;
    monomial->linked = true;
    *out_monomial = monomial;
    return PHY_OK;
}

phy_ir_ref phy_tensor_monomial_coefficient(
    const phy_tensor_monomial *monomial)
{
    return monomial != NULL ? monomial->coefficient : PHY_IR_NULL;
}

size_t phy_tensor_monomial_factor_count(
    const phy_tensor_monomial *monomial)
{
    return monomial != NULL ? monomial->factor_count : 0u;
}

phy_status phy_tensor_monomial_factor(
    const phy_tensor_monomial *monomial, size_t which,
    const phy_tensor_head **out_head,
    const phy_abstract_index **out_indices, size_t *out_index_count)
{
    if (monomial == NULL || which >= monomial->factor_count ||
        out_head == NULL || out_indices == NULL ||
        out_index_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_abstract_factor_record *factor =
        &monomial->factors[which];
    *out_head = factor->head;
    *out_indices =
        factor->index_count != 0u
            ? &monomial->indices[factor->index_offset]
            : NULL;
    *out_index_count = factor->index_count;
    return PHY_OK;
}

size_t phy_tensor_monomial_index_use_count(
    const phy_tensor_monomial *monomial)
{
    return monomial != NULL ? monomial->use_count : 0u;
}

size_t phy_tensor_monomial_free_count(
    const phy_tensor_monomial *monomial)
{
    return monomial != NULL ? monomial->free_count : 0u;
}

size_t phy_tensor_monomial_dummy_count(
    const phy_tensor_monomial *monomial)
{
    return monomial != NULL ? monomial->dummy_count : 0u;
}

phy_status phy_tensor_monomial_index_use(
    const phy_tensor_monomial *monomial, size_t which,
    phy_abstract_index_use *out_use)
{
    if (monomial == NULL || which >= monomial->use_count ||
        out_use == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_use = monomial->uses[which];
    return PHY_OK;
}

