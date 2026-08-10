#include "abstract_internal.h"

#include <limits.h>
#include <string.h>

#include "phy/permutation.h"

#define PHY_YOUNG_DEFAULT_GENERATED 4096u
#define PHY_YOUNG_DEFAULT_RESULT 256u
#define PHY_YOUNG_DEFAULT_BYTES (512u * 1024u)
#define PHY_TENSOR_ALGEBRA_DEFAULT_RESULT 256u
#define PHY_TENSOR_ALGEBRA_DEFAULT_GENERATED 4096u
#define PHY_TENSOR_ALGEBRA_DEFAULT_BYTES (512u * 1024u)

void phy_young_limits_defaults(phy_young_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_generated_terms = PHY_YOUNG_DEFAULT_GENERATED;
    out_limits->max_result_terms = PHY_YOUNG_DEFAULT_RESULT;
    out_limits->max_bytes = PHY_YOUNG_DEFAULT_BYTES;
    phy_tensor_canonical_limits_defaults(&out_limits->canonical);
}

phy_status phy_young_resolve_limits(const phy_young_limits *requested,
                                    phy_young_limits *out)
{
    phy_young_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_generated_terms != 0u) {
            out->max_generated_terms =
                requested->max_generated_terms;
        }
        if (requested->max_result_terms != 0u) {
            out->max_result_terms = requested->max_result_terms;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
        out->canonical = requested->canonical;
    }
    if (out->max_generated_terms == 0u ||
        out->max_result_terms == 0u || out->max_bytes < 4096u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

static void unlink_expression(phy_tensor_expression *expression)
{
    if (!expression->linked) {
        return;
    }
    if (expression->previous != NULL) {
        expression->previous->next = expression->next;
    } else {
        expression->context->expressions = expression->next;
    }
    if (expression->next != NULL) {
        expression->next->previous = expression->previous;
    }
    expression->linked = false;
}

void phy_tensor_expression_destroy(phy_tensor_expression *expression)
{
    if (expression == NULL) {
        return;
    }
    phy_abstract_context *context = expression->context;
    unlink_expression(expression);
    for (size_t i = 0u; i < expression->term_count; ++i) {
        phy_tensor_monomial_destroy(expression->terms[i]);
    }
    phy_abstract_free(
        context, expression->free_uses, expression->free_bytes);
    phy_abstract_free(
        context, expression->terms, expression->term_bytes);
    phy_abstract_free(context, expression, sizeof *expression);
}

phy_abstract_context *phy_tensor_expression_context(
    const phy_tensor_expression *expression)
{
    return expression != NULL ? expression->context : NULL;
}

size_t phy_tensor_expression_term_count(
    const phy_tensor_expression *expression)
{
    return expression != NULL ? expression->term_count : 0u;
}

const phy_tensor_monomial *phy_tensor_expression_term(
    const phy_tensor_expression *expression, size_t which)
{
    return expression != NULL && which < expression->term_count
               ? expression->terms[which]
               : NULL;
}

size_t phy_tensor_expression_free_count(
    const phy_tensor_expression *expression)
{
    return expression != NULL ? expression->free_count : 0u;
}

phy_status phy_tensor_expression_free_use(
    const phy_tensor_expression *expression, size_t which,
    phy_abstract_index_use *out_use)
{
    if (expression == NULL || out_use == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (which >= expression->free_count) {
        return PHY_ERR_DOMAIN;
    }
    *out_use = expression->free_uses[which];
    return PHY_OK;
}

phy_status phy_expression_create_with_signature(
    phy_abstract_context *context, size_t capacity,
    const phy_abstract_index_use *free_uses, size_t free_count,
    phy_tensor_expression **out)
{
    if (capacity > SIZE_MAX / sizeof(phy_tensor_monomial *)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_tensor_expression *expression =
        phy_abstract_alloc(context, sizeof *expression);
    if (expression == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(expression, 0, sizeof *expression);
    expression->context = context;
    expression->term_capacity = capacity;
    expression->term_bytes = capacity * sizeof(*expression->terms);
    expression->terms =
        phy_abstract_alloc(context, expression->term_bytes);
    if (expression->terms == NULL) {
        phy_abstract_free(context, expression, sizeof *expression);
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(expression->terms, 0, expression->term_bytes);
    expression->free_count = free_count;
    if (expression->free_count >
        SIZE_MAX / sizeof(*expression->free_uses)) {
        phy_abstract_free(
            context, expression->terms, expression->term_bytes);
        phy_abstract_free(context, expression, sizeof *expression);
        return PHY_ERR_MEMORY_LIMIT;
    }
    expression->free_bytes =
        expression->free_count * sizeof(*expression->free_uses);
    if (expression->free_bytes != 0u) {
        expression->free_uses =
            phy_abstract_alloc(context, expression->free_bytes);
        if (expression->free_uses == NULL) {
            phy_abstract_free(
                context, expression->terms, expression->term_bytes);
            phy_abstract_free(context, expression, sizeof *expression);
            return PHY_ERR_MEMORY_LIMIT;
        }
        memcpy(expression->free_uses, free_uses,
               expression->free_bytes);
    }
    expression->next = context->expressions;
    if (expression->next != NULL) {
        expression->next->previous = expression;
    }
    context->expressions = expression;
    expression->linked = true;
    *out = expression;
    return PHY_OK;
}

phy_status phy_expression_create_like(
    phy_abstract_context *context, size_t capacity,
    const phy_tensor_monomial *signature,
    phy_tensor_expression **out)
{
    if (signature->free_count == 0u) {
        return phy_expression_create_with_signature(
            context, capacity, NULL, 0u, out);
    }
    const size_t bytes =
        signature->free_count * sizeof(phy_abstract_index_use);
    phy_abstract_index_use *free_uses = phy_alloc(bytes);
    if (free_uses == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    size_t free_ordinal = 0u;
    for (size_t use = 0u; use < signature->use_count; ++use) {
        if (signature->uses[use].role == PHY_ABSTRACT_INDEX_FREE) {
            free_uses[free_ordinal++] = signature->uses[use];
        }
    }
    const phy_status status =
        free_ordinal == signature->free_count
            ? phy_expression_create_with_signature(
                  context, capacity, free_uses, free_ordinal, out)
            : PHY_ERR_CORRUPT_DOCUMENT;
    phy_free(free_uses, bytes);
    return status;
}

static int compare_monomial_structure(const phy_tensor_monomial *left,
                                      const phy_tensor_monomial *right)
{
    if (left->factor_count != right->factor_count) {
        return left->factor_count < right->factor_count ? -1 : 1;
    }
    for (size_t factor = 0u; factor < left->factor_count; ++factor) {
        const phy_abstract_tensor_head *left_head = left->factors[factor].head;
        const phy_abstract_tensor_head *right_head = right->factors[factor].head;
        if (left_head != right_head) {
            const int head_order = strcmp(
                phy_tensor_head_name(left_head),
                phy_tensor_head_name(right_head));
            if (head_order != 0) {
                return head_order;
            }
        }
        const size_t count = left->factors[factor].index_count;
        if (count != right->factors[factor].index_count) {
            return count < right->factors[factor].index_count ? -1 : 1;
        }
        for (size_t slot = 0u; slot < count; ++slot) {
            const phy_abstract_index *left_index =
                &left->indices[left->factors[factor].index_offset + slot];
            const phy_abstract_index *right_index =
                &right->indices[
                    right->factors[factor].index_offset + slot];
            if (left_index->space != right_index->space) {
                const int space_order = strcmp(
                    phy_index_space_name(left_index->space),
                    phy_index_space_name(right_index->space));
                if (space_order != 0) {
                    return space_order;
                }
            }
            const int name_order = strcmp(
                phy_ir_symbol_name(left->context->ir, left_index->name),
                phy_ir_symbol_name(right->context->ir, right_index->name));
            if (name_order != 0) {
                return name_order;
            }
            if (left_index->variance != right_index->variance) {
                return left_index->variance < right_index->variance ? -1 : 1;
            }
        }
    }
    return 0;
}

static phy_status coefficient_is_zero(phy_tensor_monomial *monomial,
                                      bool *out_zero)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    const phy_status status = phy_cas_is_zero(
        monomial->context->cas, monomial->coefficient, &decision);
    if (status != PHY_OK) {
        return status;
    }
    *out_zero = decision == PHY_CAS_ZERO;
    return PHY_OK;
}

phy_status phy_expression_collect_term(phy_tensor_expression *expression,
                                       phy_tensor_monomial *term)
{
    bool zero = false;
    phy_status status = coefficient_is_zero(term, &zero);
    if (status != PHY_OK || zero) {
        phy_tensor_monomial_destroy(term);
        return status;
    }
    for (size_t i = 0u; i < expression->term_count; ++i) {
        phy_tensor_monomial *existing = expression->terms[i];
        if (compare_monomial_structure(existing, term) != 0) {
            continue;
        }
        const phy_ir_ref pair[2] = {
            existing->coefficient, term->coefficient};
        phy_ir_ref sum = PHY_IR_NULL;
        status = phy_cas_add(
            expression->context->cas, pair, 2u, &sum);
        phy_tensor_monomial_destroy(term);
        if (status != PHY_OK) {
            return status;
        }
        existing->coefficient = sum;
        status = coefficient_is_zero(existing, &zero);
        if (status != PHY_OK) {
            return status;
        }
        if (zero) {
            phy_tensor_monomial_destroy(existing);
            for (size_t move = i + 1u;
                 move < expression->term_count; ++move) {
                expression->terms[move - 1u] =
                    expression->terms[move];
            }
            --expression->term_count;
        }
        return PHY_OK;
    }
    if (expression->term_count >= expression->term_capacity) {
        phy_tensor_monomial_destroy(term);
        return PHY_ERR_TERM_LIMIT;
    }
    expression->terms[expression->term_count++] = term;
    return PHY_OK;
}

void phy_expression_sort(phy_tensor_expression *expression)
{
    for (size_t i = 1u; i < expression->term_count; ++i) {
        phy_tensor_monomial *value = expression->terms[i];
        size_t position = i;
        while (position != 0u &&
               compare_monomial_structure(
                   value, expression->terms[position - 1u]) < 0) {
            expression->terms[position] =
                expression->terms[position - 1u];
            --position;
        }
        expression->terms[position] = value;
    }
}

void phy_tensor_algebra_limits_defaults(
    phy_tensor_algebra_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_generated_terms =
        PHY_TENSOR_ALGEBRA_DEFAULT_GENERATED;
    out_limits->max_result_terms =
        PHY_TENSOR_ALGEBRA_DEFAULT_RESULT;
    out_limits->max_bytes = PHY_TENSOR_ALGEBRA_DEFAULT_BYTES;
    phy_tensor_canonical_limits_defaults(&out_limits->canonical);
}

static phy_status resolve_algebra_limits(
    const phy_tensor_algebra_limits *requested,
    phy_tensor_algebra_limits *out)
{
    phy_tensor_algebra_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_generated_terms != 0u) {
            out->max_generated_terms =
                requested->max_generated_terms;
        }
        if (requested->max_result_terms != 0u) {
            out->max_result_terms = requested->max_result_terms;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
        out->canonical = requested->canonical;
    }
    if (out->max_generated_terms == 0u ||
        out->max_result_terms == 0u ||
        out->max_bytes < sizeof(phy_abstract_factor)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

static bool expression_signatures_compatible(
    const phy_tensor_expression *left,
    const phy_tensor_expression *right)
{
    if (left->free_count != right->free_count) {
        return false;
    }
    for (size_t i = 0u; i < left->free_count; ++i) {
        bool found = false;
        for (size_t j = 0u; j < right->free_count; ++j) {
            const phy_abstract_index_use *a = &left->free_uses[i];
            const phy_abstract_index_use *b = &right->free_uses[j];
            if (a->space == b->space && a->name == b->name &&
                a->lower_count == b->lower_count &&
                a->upper_count == b->upper_count &&
                a->role == b->role) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static phy_status clone_canonical_scaled(
    const phy_tensor_monomial *source, phy_ir_ref scale,
    const phy_tensor_algebra_limits *limits,
    phy_tensor_monomial **out)
{
    *out = NULL;
    const size_t factor_count = source->factor_count;
    if (factor_count != 0u &&
        sizeof(phy_abstract_factor) > SIZE_MAX / factor_count) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t factor_bytes =
        factor_count * sizeof(phy_abstract_factor);
    if (factor_bytes > limits->max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_abstract_factor *factors = NULL;
    if (factor_bytes != 0u) {
        factors = phy_alloc(factor_bytes);
        if (factors == NULL) {
            return PHY_ERR_MEMORY_LIMIT;
        }
    }
    for (size_t factor = 0u; factor < factor_count; ++factor) {
        const phy_abstract_factor_record *record =
            &source->factors[factor];
        factors[factor].head = record->head;
        factors[factor].indices =
            record->index_count == 0u
                ? NULL
                : &source->indices[record->index_offset];
        factors[factor].index_count = record->index_count;
    }

    const phy_ir_ref pair[2] = {source->coefficient, scale};
    phy_ir_ref coefficient = PHY_IR_NULL;
    phy_status status = phy_cas_mul(
        source->context->cas, pair, 2u, &coefficient);
    phy_tensor_monomial *raw = NULL;
    if (status == PHY_OK) {
        status = phy_tensor_monomial_create(
            source->context, coefficient,
            factor_count == 0u ? NULL : factors,
            factor_count, &raw);
    }
    phy_tensor_canonical_limits canonical = limits->canonical;
    const size_t canonical_bytes = limits->max_bytes - factor_bytes;
    if (canonical_bytes < 4096u) {
        phy_tensor_monomial_destroy(raw);
        phy_free(factors, factor_bytes);
        return PHY_ERR_MEMORY_LIMIT;
    }
    if (canonical.max_bytes == 0u ||
        canonical.max_bytes > canonical_bytes) {
        canonical.max_bytes = canonical_bytes;
    }
    if (status == PHY_OK) {
        status = phy_tensor_monomial_canonicalize(
            raw, &canonical, out, NULL);
    }
    phy_tensor_monomial_destroy(raw);
    phy_free(factors, factor_bytes);
    return status;
}

static phy_status append_scaled_expression(
    phy_tensor_expression *destination,
    const phy_tensor_expression *source, phy_ir_ref scale,
    const phy_tensor_algebra_limits *limits)
{
    for (size_t i = 0u; i < source->term_count; ++i) {
        phy_tensor_monomial *term = NULL;
        phy_status status = clone_canonical_scaled(
            source->terms[i], scale, limits, &term);
        if (status == PHY_OK) {
            status = phy_expression_collect_term(destination, term);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

phy_status phy_tensor_expression_from_monomial(
    const phy_tensor_monomial *monomial,
    const phy_tensor_algebra_limits *requested,
    phy_tensor_expression **out_expression)
{
    if (monomial == NULL || out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_expression = NULL;
    phy_tensor_algebra_limits limits;
    phy_status status =
        resolve_algebra_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    phy_tensor_expression *result = NULL;
    status = phy_expression_create_like(
        monomial->context, limits.max_result_terms,
        monomial, &result);
    phy_ir_ref one = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_number(
            monomial->context->cas, 1, 1, &one);
    }
    phy_tensor_monomial *term = NULL;
    if (status == PHY_OK) {
        status = clone_canonical_scaled(
            monomial, one, &limits, &term);
    }
    if (status == PHY_OK) {
        status = phy_expression_collect_term(result, term);
    }
    if (status != PHY_OK) {
        phy_tensor_expression_destroy(result);
        return status;
    }
    phy_expression_sort(result);
    *out_expression = result;
    return PHY_OK;
}
phy_status phy_tensor_expression_add(
    const phy_tensor_expression *left,
    const phy_tensor_expression *right,
    const phy_tensor_algebra_limits *requested,
    phy_tensor_expression **out_expression)
{
    if (left == NULL || right == NULL ||
        out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_expression = NULL;
    if (left->context != right->context ||
        !expression_signatures_compatible(left, right)) {
        return PHY_ERR_TYPE;
    }
    phy_tensor_algebra_limits limits;
    phy_status status =
        resolve_algebra_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    phy_tensor_expression *result = NULL;
    status = phy_expression_create_with_signature(
        left->context, limits.max_result_terms,
        left->free_uses, left->free_count, &result);
    phy_ir_ref one = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_number(
            left->context->cas, 1, 1, &one);
    }
    if (status == PHY_OK) {
        status = append_scaled_expression(
            result, left, one, &limits);
    }
    if (status == PHY_OK) {
        status = append_scaled_expression(
            result, right, one, &limits);
    }
    if (status != PHY_OK) {
        phy_tensor_expression_destroy(result);
        return status;
    }
    phy_expression_sort(result);
    *out_expression = result;
    return PHY_OK;
}

phy_status phy_tensor_expression_scale(
    const phy_tensor_expression *expression, phy_ir_ref scalar,
    const phy_tensor_algebra_limits *requested,
    phy_tensor_expression **out_expression)
{
    if (expression == NULL || scalar == PHY_IR_NULL ||
        out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_expression = NULL;
    phy_tensor_algebra_limits limits;
    phy_status status =
        resolve_algebra_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    phy_tensor_expression *result = NULL;
    status = phy_expression_create_with_signature(
        expression->context, limits.max_result_terms,
        expression->free_uses, expression->free_count, &result);
    if (status == PHY_OK) {
        status = append_scaled_expression(
            result, expression, scalar, &limits);
    }
    if (status != PHY_OK) {
        phy_tensor_expression_destroy(result);
        return status;
    }
    phy_expression_sort(result);
    *out_expression = result;
    return PHY_OK;
}

static bool same_index_identity(
    const phy_abstract_index_use *left,
    const phy_abstract_index_use *right)
{
    return left->space == right->space &&
           left->name == right->name;
}

static phy_status product_signature(
    const phy_tensor_expression *left,
    const phy_tensor_expression *right, size_t max_bytes,
    phy_abstract_index_use **out_uses, size_t *out_count,
    size_t *out_bytes)
{
    *out_uses = NULL;
    *out_count = 0u;
    *out_bytes = 0u;
    if (left->free_count >
        SIZE_MAX - right->free_count) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t capacity =
        left->free_count + right->free_count;
    if (capacity != 0u &&
        sizeof(phy_abstract_index_use) > SIZE_MAX / capacity) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t bytes =
        capacity * sizeof(phy_abstract_index_use);
    if (bytes > max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_abstract_index_use *uses = NULL;
    if (bytes != 0u) {
        uses = phy_alloc(bytes);
        if (uses == NULL) {
            return PHY_ERR_MEMORY_LIMIT;
        }
    }

    size_t count = 0u;
    for (size_t i = 0u; i < left->free_count; ++i) {
        const phy_abstract_index_use *a = &left->free_uses[i];
        const phy_abstract_index_use *match = NULL;
        for (size_t j = 0u; j < right->free_count; ++j) {
            if (same_index_identity(a, &right->free_uses[j])) {
                match = &right->free_uses[j];
                break;
            }
        }
        if (match == NULL) {
            uses[count++] = *a;
            continue;
        }
        const unsigned lower =
            (unsigned)a->lower_count +
            (unsigned)match->lower_count;
        const unsigned upper =
            (unsigned)a->upper_count +
            (unsigned)match->upper_count;
        if (lower != 1u || upper != 1u) {
            phy_free(uses, bytes);
            return PHY_ERR_TYPE;
        }
        /* One lower and one upper occurrence become a dummy pair. */
    }
    for (size_t j = 0u; j < right->free_count; ++j) {
        bool matched = false;
        for (size_t i = 0u; i < left->free_count; ++i) {
            if (same_index_identity(
                    &left->free_uses[i],
                    &right->free_uses[j])) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            uses[count++] = right->free_uses[j];
        }
    }
    *out_uses = uses;
    *out_count = count;
    *out_bytes = bytes;
    return PHY_OK;
}

static phy_status canonical_product(
    const phy_tensor_monomial *left,
    const phy_tensor_monomial *right,
    const phy_tensor_algebra_limits *limits,
    phy_tensor_monomial **out)
{
    *out = NULL;
    if (left->factor_count >
        SIZE_MAX - right->factor_count) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t factor_count =
        left->factor_count + right->factor_count;
    if (factor_count != 0u &&
        sizeof(phy_abstract_factor) > SIZE_MAX / factor_count) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t factor_bytes =
        factor_count * sizeof(phy_abstract_factor);
    if (factor_bytes > limits->max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_abstract_factor *factors = NULL;
    if (factor_bytes != 0u) {
        factors = phy_alloc(factor_bytes);
        if (factors == NULL) {
            return PHY_ERR_MEMORY_LIMIT;
        }
    }
    size_t at = 0u;
    const phy_tensor_monomial *sources[2] = {left, right};
    for (size_t source = 0u; source < 2u; ++source) {
        for (size_t factor = 0u;
             factor < sources[source]->factor_count; ++factor) {
            const phy_abstract_factor_record *record =
                &sources[source]->factors[factor];
            factors[at].head = record->head;
            factors[at].indices =
                record->index_count == 0u
                    ? NULL
                    : &sources[source]->indices[
                          record->index_offset];
            factors[at].index_count = record->index_count;
            ++at;
        }
    }
    const phy_ir_ref coefficients[2] = {
        left->coefficient, right->coefficient};
    phy_ir_ref coefficient = PHY_IR_NULL;
    phy_status status = phy_cas_mul(
        left->context->cas, coefficients, 2u, &coefficient);
    phy_tensor_monomial *raw = NULL;
    if (status == PHY_OK) {
        status = phy_tensor_monomial_create(
            left->context, coefficient,
            factor_count == 0u ? NULL : factors,
            factor_count, &raw);
    }
    phy_tensor_canonical_limits canonical = limits->canonical;
    const size_t canonical_bytes = limits->max_bytes - factor_bytes;
    if (status == PHY_OK && canonical_bytes < 4096u) {
        status = PHY_ERR_MEMORY_LIMIT;
    }
    if (canonical.max_bytes == 0u ||
        canonical.max_bytes > canonical_bytes) {
        canonical.max_bytes = canonical_bytes;
    }
    if (status == PHY_OK) {
        status = phy_tensor_monomial_canonicalize(
            raw, &canonical, out, NULL);
    }
    phy_tensor_monomial_destroy(raw);
    phy_free(factors, factor_bytes);
    return status;
}

phy_status phy_tensor_expression_multiply(
    const phy_tensor_expression *left,
    const phy_tensor_expression *right,
    const phy_tensor_algebra_limits *requested,
    phy_tensor_expression **out_expression)
{
    if (left == NULL || right == NULL ||
        out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_expression = NULL;
    if (left->context != right->context) {
        return PHY_ERR_TYPE;
    }
    phy_tensor_algebra_limits limits;
    phy_status status =
        resolve_algebra_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    if (left->term_count != 0u &&
        right->term_count >
            limits.max_generated_terms / left->term_count) {
        return PHY_ERR_TERM_LIMIT;
    }

    phy_abstract_index_use *signature = NULL;
    size_t signature_count = 0u;
    size_t signature_bytes = 0u;
    status = product_signature(
        left, right, limits.max_bytes, &signature,
        &signature_count, &signature_bytes);
    if (status != PHY_OK) {
        return status;
    }
    phy_tensor_expression *result = NULL;
    status = phy_expression_create_with_signature(
        left->context, limits.max_result_terms,
        signature, signature_count, &result);
    phy_free(signature, signature_bytes);
    for (size_t i = 0u;
         i < left->term_count && status == PHY_OK; ++i) {
        for (size_t j = 0u;
             j < right->term_count && status == PHY_OK; ++j) {
            phy_tensor_monomial *term = NULL;
            status = canonical_product(
                left->terms[i], right->terms[j],
                &limits, &term);
            if (status == PHY_OK) {
                status = phy_expression_collect_term(result, term);
            }
        }
    }
    if (status != PHY_OK) {
        phy_tensor_expression_destroy(result);
        return status;
    }
    phy_expression_sort(result);
    *out_expression = result;
    return PHY_OK;
}
