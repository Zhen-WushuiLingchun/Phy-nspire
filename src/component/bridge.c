/*
 * Explicit abstract-to-component evaluation for one tensor monomial.
 *
 * The abstract layer supplies typed index identities and the Einstein census.
 * A component binding supplies one concrete basis per IndexSpace and one
 * sparse realization per TensorHead.  This file fixes free coordinates and
 * enumerates only dummy indices; it never allocates the full product of all
 * slot dimensions and never inserts a metric implicitly.
 */
#include "component_internal.h"

#include <string.h>

#define PHY_BRIDGE_DEFAULT_BASES 32u
#define PHY_BRIDGE_DEFAULT_TENSORS 64u
#define PHY_BRIDGE_DEFAULT_FREE 16u
#define PHY_BRIDGE_DEFAULT_DUMMY 16u
#define PHY_BRIDGE_DEFAULT_TERMS 65536u
#define PHY_BRIDGE_DEFAULT_STEPS 2000000u
#define PHY_BRIDGE_DEFAULT_BYTES (64u * 1024u)
#define PHY_BRIDGE_SUM_WINDOW 32u

struct phy_component_binding {
    phy_abstract_context *context;
    phy_cas *cas;
    phy_bridge_limits limits;
    phy_component_basis **bases;
    phy_component_tensor **tensors;
    size_t basis_count;
    size_t tensor_count;
    void *table;
    size_t table_bytes;
    size_t persistent_bytes;
    phy_ir_ref zero;
};

typedef struct {
    phy_component_binding *binding;
    const phy_tensor_monomial *monomial;
    size_t use_count;
    size_t factor_count;
    size_t index_count;
    size_t free_count;
    size_t dummy_count;
    void *storage;
    size_t storage_bytes;

    phy_abstract_index_use *uses;
    uint32_t *use_values;
    size_t *use_dimensions;
    size_t *use_dummy;
    size_t *index_uses;
    size_t *free_uses;
    size_t *dummy_uses;

    phy_component_tensor **factor_tensors;
    size_t *factor_stages;
    size_t *factor_offsets;
    size_t *factor_slots;

    phy_ir_ref *stage_products;
    phy_ir_ref *product_buffer;
    uint32_t *slot_buffer;
    phy_ir_ref *sum_window;
    size_t sum_count;

    uint32_t steps;
    uint64_t assignments;
    uint64_t pruned;
    uint64_t terms;
} bridge_eval;

static bool align_up_checked(size_t value, size_t alignment,
                             size_t *out_value)
{
    const size_t remainder = value % alignment;
    const size_t padding =
        remainder == 0u ? 0u : alignment - remainder;
    if (value > SIZE_MAX - padding) {
        return false;
    }
    *out_value = value + padding;
    return true;
}

static bool reserve_array(size_t *total, size_t count, size_t element,
                          size_t *out_offset)
{
    if (count != 0u && element > SIZE_MAX / count) {
        return false;
    }
    size_t offset = 0u;
    if (!align_up_checked(*total, sizeof(void *), &offset)) {
        return false;
    }
    const size_t bytes = count * element;
    if (offset > SIZE_MAX - bytes) {
        return false;
    }
    *out_offset = offset;
    *total = offset + bytes;
    return true;
}

void phy_bridge_limits_defaults(phy_bridge_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_bases = PHY_BRIDGE_DEFAULT_BASES;
    out_limits->max_tensors = PHY_BRIDGE_DEFAULT_TENSORS;
    out_limits->max_free = PHY_BRIDGE_DEFAULT_FREE;
    out_limits->max_dummy = PHY_BRIDGE_DEFAULT_DUMMY;
    out_limits->max_terms = PHY_BRIDGE_DEFAULT_TERMS;
    out_limits->max_steps = PHY_BRIDGE_DEFAULT_STEPS;
    out_limits->max_bytes = PHY_BRIDGE_DEFAULT_BYTES;
}

static phy_status resolve_limits(const phy_bridge_limits *requested,
                                 phy_bridge_limits *out)
{
    phy_bridge_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_bases != 0u) {
            out->max_bases = requested->max_bases;
        }
        if (requested->max_tensors != 0u) {
            out->max_tensors = requested->max_tensors;
        }
        if (requested->max_free != 0u) {
            out->max_free = requested->max_free;
        }
        if (requested->max_dummy != 0u) {
            out->max_dummy = requested->max_dummy;
        }
        if (requested->max_terms != 0u) {
            out->max_terms = requested->max_terms;
        }
        if (requested->max_steps != 0u) {
            out->max_steps = requested->max_steps;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
    }
    if (out->max_bases == 0u || out->max_tensors == 0u ||
        out->max_free == 0u || out->max_dummy == 0u ||
        out->max_terms == 0u || out->max_steps == 0u ||
        out->max_bytes < sizeof(phy_component_binding)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

phy_status phy_component_binding_create(
    phy_abstract_context *context, const phy_bridge_limits *requested,
    phy_component_binding **out_binding)
{
    if (context == NULL || out_binding == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_binding = NULL;

    phy_bridge_limits limits;
    phy_status status = resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }

    size_t bases_offset = 0u;
    size_t tensors_offset = 0u;
    size_t table_bytes = 0u;
    if (!reserve_array(&table_bytes, limits.max_bases,
                       sizeof(phy_component_basis *), &bases_offset) ||
        !reserve_array(&table_bytes, limits.max_tensors,
                       sizeof(phy_component_tensor *), &tensors_offset) ||
        table_bytes > limits.max_bytes - sizeof(phy_component_binding)) {
        return PHY_ERR_MEMORY_LIMIT;
    }

    phy_component_binding *binding = phy_alloc(sizeof *binding);
    if (binding == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(binding, 0, sizeof *binding);
    binding->table = phy_alloc(table_bytes);
    if (binding->table == NULL) {
        phy_free(binding, sizeof *binding);
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(binding->table, 0, table_bytes);

    binding->context = context;
    binding->cas = phy_abstract_cas(context);
    binding->limits = limits;
    binding->table_bytes = table_bytes;
    binding->persistent_bytes = sizeof *binding + table_bytes;
    uint8_t *memory = binding->table;
    binding->bases =
        (phy_component_basis **)(void *)(memory + bases_offset);
    binding->tensors =
        (phy_component_tensor **)(void *)(memory + tensors_offset);
    status = phy_cas_number(binding->cas, 0, 1, &binding->zero);
    if (status != PHY_OK) {
        phy_component_binding_destroy(binding);
        return status;
    }
    *out_binding = binding;
    return PHY_OK;
}

void phy_component_binding_destroy(phy_component_binding *binding)
{
    if (binding == NULL) {
        return;
    }
    phy_free(binding->table, binding->table_bytes);
    phy_free(binding, sizeof *binding);
}

phy_component_basis *phy_component_binding_basis(
    const phy_component_binding *binding, const phy_index_space *space)
{
    if (binding == NULL || space == NULL) {
        return NULL;
    }
    for (size_t i = 0u; i < binding->basis_count; ++i) {
        if (phy_component_basis_space(binding->bases[i]) == space) {
            return binding->bases[i];
        }
    }
    return NULL;
}

phy_component_tensor *phy_component_binding_tensor(
    const phy_component_binding *binding,
    const phy_abstract_tensor_head *head)
{
    if (binding == NULL || head == NULL) {
        return NULL;
    }
    for (size_t i = 0u; i < binding->tensor_count; ++i) {
        if (phy_component_tensor_head(binding->tensors[i]) == head) {
            return binding->tensors[i];
        }
    }
    return NULL;
}

phy_status phy_component_binding_add_basis(
    phy_component_binding *binding, phy_component_basis *basis)
{
    if (binding == NULL || basis == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_index_space *space = phy_component_basis_space(basis);
    if (space == NULL ||
        phy_index_space_context(space) != binding->context) {
        return PHY_ERR_TYPE;
    }
    phy_component_basis *existing =
        phy_component_binding_basis(binding, space);
    if (existing != NULL) {
        return existing == basis ? PHY_OK
                                 : PHY_ERR_ALREADY_INITIALIZED;
    }
    if (binding->basis_count == binding->limits.max_bases) {
        return PHY_ERR_TERM_LIMIT;
    }
    binding->bases[binding->basis_count++] = basis;
    return PHY_OK;
}

phy_status phy_component_binding_add_tensor(
    phy_component_binding *binding, phy_component_tensor *tensor)
{
    if (binding == NULL || tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_abstract_tensor_head *head =
        phy_component_tensor_head(tensor);
    if (head == NULL ||
        phy_tensor_head_context(head) != binding->context) {
        return PHY_ERR_TYPE;
    }
    phy_component_tensor *existing =
        phy_component_binding_tensor(binding, head);
    if (existing != NULL) {
        return existing == tensor ? PHY_OK
                                  : PHY_ERR_ALREADY_INITIALIZED;
    }

    const size_t rank = phy_component_tensor_rank(tensor);
    if (rank != phy_tensor_head_slot_count(head)) {
        return PHY_ERR_TYPE;
    }
    for (size_t slot = 0u; slot < rank; ++slot) {
        const phy_index_space *space =
            phy_tensor_head_slot_space(head, slot);
        phy_component_basis *basis =
            phy_component_binding_basis(binding, space);
        if (basis == NULL) {
            return PHY_ERR_NOT_INITIALIZED;
        }
        if (phy_component_tensor_basis(tensor, slot) != basis) {
            return PHY_ERR_TYPE;
        }
    }
    if (binding->tensor_count == binding->limits.max_tensors) {
        return PHY_ERR_TERM_LIMIT;
    }
    binding->tensors[binding->tensor_count++] = tensor;
    return PHY_OK;
}

size_t phy_component_binding_basis_count(
    const phy_component_binding *binding)
{
    return binding != NULL ? binding->basis_count : 0u;
}

size_t phy_component_binding_tensor_count(
    const phy_component_binding *binding)
{
    return binding != NULL ? binding->tensor_count : 0u;
}

phy_status phy_component_value_free_slots(
    const phy_tensor_monomial *monomial, phy_abstract_index_use *out_uses,
    size_t capacity, size_t *out_count)
{
    if (monomial == NULL || out_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    const size_t count =
        phy_tensor_monomial_index_use_count(monomial);
    for (size_t i = 0u; i < count; ++i) {
        phy_abstract_index_use use;
        const phy_status status =
            phy_tensor_monomial_index_use(monomial, i, &use);
        if (status != PHY_OK) {
            return status;
        }
        if (use.role != PHY_ABSTRACT_INDEX_FREE) {
            continue;
        }
        if (out_uses != NULL) {
            if (*out_count >= capacity) {
                return PHY_ERR_TERM_LIMIT;
            }
            out_uses[*out_count] = use;
        }
        ++*out_count;
    }
    return PHY_OK;
}

static void eval_release(bridge_eval *eval)
{
    phy_free(eval->storage, eval->storage_bytes);
    eval->storage = NULL;
    eval->storage_bytes = 0u;
}

static phy_status eval_allocate(bridge_eval *eval, size_t max_slots)
{
    size_t uses_offset = 0u;
    size_t use_values_offset = 0u;
    size_t use_dimensions_offset = 0u;
    size_t use_dummy_offset = 0u;
    size_t index_uses_offset = 0u;
    size_t free_uses_offset = 0u;
    size_t dummy_uses_offset = 0u;
    size_t factor_tensors_offset = 0u;
    size_t factor_stages_offset = 0u;
    size_t factor_offsets_offset = 0u;
    size_t factor_slots_offset = 0u;
    size_t stage_products_offset = 0u;
    size_t product_buffer_offset = 0u;
    size_t slot_buffer_offset = 0u;
    size_t sum_window_offset = 0u;
    size_t bytes = 0u;

    const bool valid =
        reserve_array(&bytes, eval->use_count, sizeof(*eval->uses),
                      &uses_offset) &&
        reserve_array(&bytes, eval->use_count,
                      sizeof(*eval->use_values), &use_values_offset) &&
        reserve_array(&bytes, eval->use_count,
                      sizeof(*eval->use_dimensions),
                      &use_dimensions_offset) &&
        reserve_array(&bytes, eval->use_count,
                      sizeof(*eval->use_dummy), &use_dummy_offset) &&
        reserve_array(&bytes, eval->index_count,
                      sizeof(*eval->index_uses), &index_uses_offset) &&
        reserve_array(&bytes, eval->free_count,
                      sizeof(*eval->free_uses), &free_uses_offset) &&
        reserve_array(&bytes, eval->dummy_count,
                      sizeof(*eval->dummy_uses), &dummy_uses_offset) &&
        reserve_array(&bytes, eval->factor_count,
                      sizeof(*eval->factor_tensors),
                      &factor_tensors_offset) &&
        reserve_array(&bytes, eval->factor_count,
                      sizeof(*eval->factor_stages),
                      &factor_stages_offset) &&
        reserve_array(&bytes, eval->factor_count,
                      sizeof(*eval->factor_offsets),
                      &factor_offsets_offset) &&
        reserve_array(&bytes, eval->factor_count,
                      sizeof(*eval->factor_slots),
                      &factor_slots_offset) &&
        reserve_array(&bytes, eval->dummy_count + 1u,
                      sizeof(*eval->stage_products),
                      &stage_products_offset) &&
        reserve_array(&bytes, eval->factor_count + 1u,
                      sizeof(*eval->product_buffer),
                      &product_buffer_offset) &&
        reserve_array(&bytes, max_slots, sizeof(*eval->slot_buffer),
                      &slot_buffer_offset) &&
        reserve_array(&bytes, PHY_BRIDGE_SUM_WINDOW,
                      sizeof(*eval->sum_window), &sum_window_offset);
    if (!valid ||
        eval->binding->persistent_bytes >
            eval->binding->limits.max_bytes ||
        bytes > eval->binding->limits.max_bytes -
                    eval->binding->persistent_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }

    eval->storage = phy_alloc(bytes);
    if (eval->storage == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    eval->storage_bytes = bytes;
    memset(eval->storage, 0, bytes);
    uint8_t *memory = eval->storage;
    eval->uses =
        (phy_abstract_index_use *)(void *)(memory + uses_offset);
    eval->use_values =
        (uint32_t *)(void *)(memory + use_values_offset);
    eval->use_dimensions =
        (size_t *)(void *)(memory + use_dimensions_offset);
    eval->use_dummy =
        (size_t *)(void *)(memory + use_dummy_offset);
    eval->index_uses =
        (size_t *)(void *)(memory + index_uses_offset);
    eval->free_uses =
        (size_t *)(void *)(memory + free_uses_offset);
    eval->dummy_uses =
        (size_t *)(void *)(memory + dummy_uses_offset);
    eval->factor_tensors =
        (phy_component_tensor **)(void *)(memory +
                                          factor_tensors_offset);
    eval->factor_stages =
        (size_t *)(void *)(memory + factor_stages_offset);
    eval->factor_offsets =
        (size_t *)(void *)(memory + factor_offsets_offset);
    eval->factor_slots =
        (size_t *)(void *)(memory + factor_slots_offset);
    eval->stage_products =
        (phy_ir_ref *)(void *)(memory + stage_products_offset);
    eval->product_buffer =
        (phy_ir_ref *)(void *)(memory + product_buffer_offset);
    eval->slot_buffer =
        (uint32_t *)(void *)(memory + slot_buffer_offset);
    eval->sum_window =
        (phy_ir_ref *)(void *)(memory + sum_window_offset);
    return PHY_OK;
}

static size_t find_use(const bridge_eval *eval,
                       const phy_abstract_index *index)
{
    for (size_t i = 0u; i < eval->use_count; ++i) {
        if (eval->uses[i].space == index->space &&
            eval->uses[i].name == index->name) {
            return i;
        }
    }
    return SIZE_MAX;
}

static phy_status eval_prepare(bridge_eval *eval)
{
    if (phy_tensor_monomial_context(eval->monomial) !=
        eval->binding->context) {
        return PHY_ERR_TYPE;
    }
    eval->use_count =
        phy_tensor_monomial_index_use_count(eval->monomial);
    eval->factor_count =
        phy_tensor_monomial_factor_count(eval->monomial);
    eval->free_count =
        phy_tensor_monomial_free_count(eval->monomial);
    eval->dummy_count =
        phy_tensor_monomial_dummy_count(eval->monomial);
    if (eval->free_count > eval->binding->limits.max_free ||
        eval->dummy_count > eval->binding->limits.max_dummy) {
        return PHY_ERR_TERM_LIMIT;
    }

    size_t max_slots = 0u;
    for (size_t factor = 0u; factor < eval->factor_count; ++factor) {
        const phy_abstract_tensor_head *head = NULL;
        const phy_abstract_index *indices = NULL;
        size_t slots = 0u;
        const phy_status status = phy_tensor_monomial_factor(
            eval->monomial, factor, &head, &indices, &slots);
        if (status != PHY_OK) {
            return status;
        }
        (void)head;
        (void)indices;
        if (eval->index_count > SIZE_MAX - slots) {
            return PHY_ERR_TERM_LIMIT;
        }
        eval->index_count += slots;
        if (slots > max_slots) {
            max_slots = slots;
        }
    }

    phy_status status = eval_allocate(eval, max_slots);
    if (status != PHY_OK) {
        return status;
    }

    size_t free_ordinal = 0u;
    size_t dummy_ordinal = 0u;
    for (size_t use = 0u; use < eval->use_count; ++use) {
        status = phy_tensor_monomial_index_use(
            eval->monomial, use, &eval->uses[use]);
        if (status != PHY_OK) {
            return status;
        }
        phy_component_basis *basis = phy_component_binding_basis(
            eval->binding, eval->uses[use].space);
        if (basis == NULL) {
            return PHY_ERR_NOT_INITIALIZED;
        }
        eval->use_dimensions[use] =
            phy_component_basis_dimension(basis);
        if (eval->uses[use].role == PHY_ABSTRACT_INDEX_FREE) {
            eval->use_dummy[use] = SIZE_MAX;
            eval->free_uses[free_ordinal++] = use;
        } else {
            eval->use_dummy[use] = dummy_ordinal;
            eval->dummy_uses[dummy_ordinal++] = use;
        }
    }

    size_t offset = 0u;
    for (size_t factor = 0u; factor < eval->factor_count; ++factor) {
        const phy_abstract_tensor_head *head = NULL;
        const phy_abstract_index *indices = NULL;
        size_t slots = 0u;
        status = phy_tensor_monomial_factor(
            eval->monomial, factor, &head, &indices, &slots);
        if (status != PHY_OK) {
            return status;
        }
        phy_component_tensor *tensor =
            phy_component_binding_tensor(eval->binding, head);
        if (tensor == NULL) {
            return PHY_ERR_NOT_INITIALIZED;
        }
        if (phy_component_tensor_rank(tensor) != slots) {
            return PHY_ERR_TYPE;
        }
        eval->factor_tensors[factor] = tensor;
        eval->factor_offsets[factor] = offset;
        eval->factor_slots[factor] = slots;
        eval->factor_stages[factor] = 0u;
        for (size_t slot = 0u; slot < slots; ++slot) {
            const size_t use = find_use(eval, &indices[slot]);
            if (use == SIZE_MAX) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
            phy_component_basis *basis = phy_component_binding_basis(
                eval->binding, indices[slot].space);
            if (phy_component_tensor_basis(tensor, slot) != basis ||
                phy_component_tensor_valence(tensor, slot) !=
                    indices[slot].variance) {
                return PHY_ERR_TYPE;
            }
            eval->index_uses[offset + slot] = use;
            const size_t dummy = eval->use_dummy[use];
            if (dummy != SIZE_MAX &&
                dummy + 1u > eval->factor_stages[factor]) {
                eval->factor_stages[factor] = dummy + 1u;
            }
        }
        offset += slots;
    }
    return PHY_OK;
}

static phy_status exact_zero(bridge_eval *eval, phy_ir_ref value,
                             bool *out_zero)
{
    if (value == eval->binding->zero) {
        *out_zero = true;
        return PHY_OK;
    }
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    const phy_status status =
        phy_cas_is_zero(eval->binding->cas, value, &decision);
    if (status != PHY_OK) {
        return status;
    }
    *out_zero = decision == PHY_CAS_ZERO;
    return PHY_OK;
}

static phy_status eval_stage(bridge_eval *eval, size_t stage,
                             bool *out_zero)
{
    size_t count = 0u;
    eval->product_buffer[count++] =
        stage == 0u
            ? phy_tensor_monomial_coefficient(eval->monomial)
            : eval->stage_products[stage - 1u];
    for (size_t factor = 0u; factor < eval->factor_count; ++factor) {
        if (eval->factor_stages[factor] != stage) {
            continue;
        }
        const size_t slots = eval->factor_slots[factor];
        const size_t offset = eval->factor_offsets[factor];
        for (size_t slot = 0u; slot < slots; ++slot) {
            eval->slot_buffer[slot] =
                eval->use_values[eval->index_uses[offset + slot]];
        }
        phy_ir_ref value = PHY_IR_NULL;
        phy_status status = phy_component_tensor_get(
            eval->factor_tensors[factor],
            slots == 0u ? NULL : eval->slot_buffer, &value);
        if (status != PHY_OK) {
            return status;
        }
        bool zero = false;
        status = exact_zero(eval, value, &zero);
        if (status != PHY_OK) {
            return status;
        }
        if (zero) {
            *out_zero = true;
            return PHY_OK;
        }
        eval->product_buffer[count++] = value;
    }
    phy_status status = phy_cas_mul(
        eval->binding->cas, eval->product_buffer, count,
        &eval->stage_products[stage]);
    if (status != PHY_OK) {
        return status;
    }
    return exact_zero(eval, eval->stage_products[stage], out_zero);
}

static phy_status flush_sum(bridge_eval *eval)
{
    if (eval->sum_count <= 1u) {
        return PHY_OK;
    }
    phy_ir_ref total = PHY_IR_NULL;
    const phy_status status = phy_cas_add(
        eval->binding->cas, eval->sum_window, eval->sum_count, &total);
    if (status != PHY_OK) {
        return status;
    }
    eval->sum_window[0] = total;
    eval->sum_count = 1u;
    return PHY_OK;
}

static phy_status accumulate(bridge_eval *eval, phy_ir_ref term)
{
    bool zero = false;
    phy_status status = exact_zero(eval, term, &zero);
    if (status != PHY_OK || zero) {
        return status;
    }
    if (eval->terms >= (uint64_t)eval->binding->limits.max_terms) {
        return PHY_ERR_TERM_LIMIT;
    }
    ++eval->terms;
    eval->sum_window[eval->sum_count++] = term;
    return eval->sum_count == PHY_BRIDGE_SUM_WINDOW
               ? flush_sum(eval)
               : PHY_OK;
}

static phy_status descend(bridge_eval *eval, size_t stage)
{
    if (stage == eval->dummy_count) {
        ++eval->assignments;
        return accumulate(eval, eval->stage_products[stage]);
    }
    const size_t use = eval->dummy_uses[stage];
    const size_t dimension = eval->use_dimensions[use];
    for (size_t value = 0u; value < dimension; ++value) {
        if (eval->steps >= eval->binding->limits.max_steps) {
            return PHY_ERR_TIMEOUT;
        }
        ++eval->steps;
        eval->use_values[use] = (uint32_t)value;
        bool zero = false;
        phy_status status = eval_stage(eval, stage + 1u, &zero);
        if (status != PHY_OK) {
            return status;
        }
        if (zero) {
            ++eval->pruned;
            continue;
        }
        status = descend(eval, stage + 1u);
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static void report_stats(const bridge_eval *eval,
                         phy_bridge_stats *out_stats)
{
    if (out_stats == NULL) {
        return;
    }
    out_stats->free_count = eval->free_count;
    out_stats->dummy_count = eval->dummy_count;
    out_stats->assignments = eval->assignments;
    out_stats->pruned = eval->pruned;
    out_stats->terms = eval->terms;
    out_stats->bytes_used =
        eval->binding->persistent_bytes + eval->storage_bytes;
}

phy_status phy_component_value_monomial(
    phy_component_binding *binding, const phy_tensor_monomial *monomial,
    const uint32_t *free_indices, size_t free_count,
    phy_ir_ref *out_value, phy_bridge_stats *out_stats)
{
    if (out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = PHY_IR_NULL;
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof *out_stats);
    }
    if (binding == NULL || monomial == NULL ||
        (free_count != 0u && free_indices == NULL)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    bridge_eval eval;
    memset(&eval, 0, sizeof eval);
    eval.binding = binding;
    eval.monomial = monomial;
    phy_status status = eval_prepare(&eval);
    if (status == PHY_OK && free_count != eval.free_count) {
        status = PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t ordinal = 0u;
         status == PHY_OK && ordinal < free_count; ++ordinal) {
        const size_t use = eval.free_uses[ordinal];
        if ((size_t)free_indices[ordinal] >=
            eval.use_dimensions[use]) {
            status = PHY_ERR_DOMAIN;
            break;
        }
        eval.use_values[use] = free_indices[ordinal];
    }
    if (status == PHY_OK) {
        eval.sum_window[0] = binding->zero;
        eval.sum_count = 1u;
        bool zero = false;
        status = eval_stage(&eval, 0u, &zero);
        if (status == PHY_OK && !zero) {
            status = descend(&eval, 0u);
        }
    }
    if (status == PHY_OK) {
        status = flush_sum(&eval);
    }
    if (status == PHY_OK) {
        *out_value = eval.sum_window[0];
        report_stats(&eval, out_stats);
    }
    eval_release(&eval);
    return status;
}
