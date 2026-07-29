#include "component_internal.h"

#include <limits.h>
#include <string.h>

#define PHY_COMPONENT_DEFAULT_RANK 32u
#define PHY_COMPONENT_DEFAULT_DIMENSION 256u
#define PHY_COMPONENT_DEFAULT_ENTRIES 4096u
#define PHY_COMPONENT_DEFAULT_GENERATORS 256u
#define PHY_COMPONENT_DEFAULT_STRONG 1024u
#define PHY_COMPONENT_DEFAULT_STEPS 2000000u
#define PHY_COMPONENT_DEFAULT_CANDIDATES 100000u
#define PHY_COMPONENT_DEFAULT_BYTES (512u * 1024u)

typedef struct {
    phy_component_tensor *tensor;
    const uint32_t *input;
    bool best_found;
    bool zero;
    int best_sign;
    uint32_t steps;
    uint64_t candidates;
} phy_component_search;

static size_t align_up(size_t value, size_t alignment)
{
    const size_t remainder = value % alignment;
    return remainder == 0u ? value : value + alignment - remainder;
}

static bool reserve_array(size_t *total, size_t count, size_t element,
                          size_t *out_offset)
{
    if (count != 0u && element > SIZE_MAX / count) {
        return false;
    }
    const size_t offset = align_up(*total, sizeof(void *));
    const size_t bytes = count * element;
    if (offset > SIZE_MAX - bytes) {
        return false;
    }
    *out_offset = offset;
    *total = offset + bytes;
    return true;
}

void phy_component_limits_defaults(phy_component_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_rank = PHY_COMPONENT_DEFAULT_RANK;
    out_limits->max_dimension = PHY_COMPONENT_DEFAULT_DIMENSION;
    out_limits->max_entries = PHY_COMPONENT_DEFAULT_ENTRIES;
    out_limits->max_generators = PHY_COMPONENT_DEFAULT_GENERATORS;
    out_limits->max_strong_generators = PHY_COMPONENT_DEFAULT_STRONG;
    out_limits->max_steps = PHY_COMPONENT_DEFAULT_STEPS;
    out_limits->max_candidates = PHY_COMPONENT_DEFAULT_CANDIDATES;
    out_limits->max_bytes = PHY_COMPONENT_DEFAULT_BYTES;
}

static phy_status resolve_limits(const phy_component_limits *requested,
                                 phy_component_limits *out)
{
    phy_component_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_rank != 0u) {
            out->max_rank = requested->max_rank;
        }
        if (requested->max_dimension != 0u) {
            out->max_dimension = requested->max_dimension;
        }
        if (requested->max_entries != 0u) {
            out->max_entries = requested->max_entries;
        }
        if (requested->max_generators != 0u) {
            out->max_generators = requested->max_generators;
        }
        if (requested->max_strong_generators != 0u) {
            out->max_strong_generators =
                requested->max_strong_generators;
        }
        if (requested->max_steps != 0u) {
            out->max_steps = requested->max_steps;
        }
        if (requested->max_candidates != 0u) {
            out->max_candidates = requested->max_candidates;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
    }
    if (out->max_rank == 0u || out->max_rank > UINT16_MAX ||
        out->max_dimension == 0u ||
        out->max_dimension > UINT32_MAX ||
        out->max_entries == 0u || out->max_generators == 0u ||
        out->max_strong_generators == 0u ||
        out->max_steps == 0u || out->max_candidates == 0u ||
        out->max_bytes < 8192u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

static phy_status validate_realization(
    const phy_tensor_head *head, phy_component_basis *const *bases,
    const phy_ir_variance *valence,
    const phy_component_limits *limits)
{
    const size_t rank = phy_tensor_head_slot_count(head);
    if (rank > limits->max_rank) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (rank != 0u && (bases == NULL || valence == NULL)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t slot = 0u; slot < rank; ++slot) {
        if (bases[slot] == NULL ||
            bases[slot]->space != phy_tensor_head_slot_space(head, slot) ||
            bases[slot]->dimension > limits->max_dimension) {
            return PHY_ERR_TYPE;
        }
        if (valence[slot] != PHY_IR_INDEX_LOWER &&
            valence[slot] != PHY_IR_INDEX_UPPER) {
            return PHY_ERR_TYPE;
        }
    }
    for (size_t generator = 0u;
         generator < phy_tensor_head_symmetry_count(head); ++generator) {
        const uint16_t *image = NULL;
        int sign = 0;
        const phy_status status = phy_tensor_head_symmetry(
            head, generator, &image, &sign);
        if (status != PHY_OK) {
            return status;
        }
        (void)sign;
        for (size_t slot = 0u; slot < rank; ++slot) {
            if (bases[slot] != bases[image[slot]] ||
                valence[slot] != valence[image[slot]]) {
                return PHY_ERR_TYPE;
            }
        }
    }
    return PHY_OK;
}

static phy_status allocate_metadata(phy_component_tensor *tensor,
                                    phy_component_basis *const *bases,
                                    const phy_ir_variance *valence)
{
    const size_t rank = tensor->rank;
    size_t bases_offset = 0u;
    size_t valence_offset = 0u;
    size_t stack_offset = 0u;
    size_t choice_offset = 0u;
    size_t processed_offset = 0u;
    size_t best_offset = 0u;
    size_t bytes = 0u;
    bool valid =
        reserve_array(&bytes, rank, sizeof(*tensor->bases),
                      &bases_offset) &&
        reserve_array(&bytes, rank, sizeof(*tensor->valence),
                      &valence_offset) &&
        reserve_array(
            &bytes, rank == 0u ? 0u : (rank + 1u) * rank,
            sizeof(*tensor->permutation_stack), &stack_offset) &&
        reserve_array(&bytes, rank,
                      sizeof(*tensor->choice_permutation),
                      &choice_offset) &&
        reserve_array(&bytes, rank == 0u ? 0u : rank * rank,
                      sizeof(*tensor->processed_points),
                      &processed_offset) &&
        reserve_array(&bytes, rank, sizeof(*tensor->best_indices),
                      &best_offset);
    if (!valid ||
        bytes > tensor->other_budget - sizeof(*tensor)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    tensor->metadata = phy_alloc(bytes == 0u ? 1u : bytes);
    if (tensor->metadata == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    tensor->metadata_bytes = bytes == 0u ? 1u : bytes;
    tensor->bytes_used += tensor->metadata_bytes;
    memset(tensor->metadata, 0, tensor->metadata_bytes);
    uint8_t *memory = tensor->metadata;
    tensor->bases =
        (phy_component_basis **)(void *)(memory + bases_offset);
    tensor->valence =
        (phy_ir_variance *)(void *)(memory + valence_offset);
    tensor->permutation_stack =
        (uint16_t *)(void *)(memory + stack_offset);
    tensor->choice_permutation =
        (uint16_t *)(void *)(memory + choice_offset);
    tensor->processed_points = memory + processed_offset;
    tensor->best_indices =
        (uint32_t *)(void *)(memory + best_offset);
    if (rank != 0u) {
        memcpy(tensor->bases, bases, rank * sizeof(*bases));
        memcpy(tensor->valence, valence, rank * sizeof(*valence));
    }
    tensor->scratch_bytes =
        tensor->metadata_bytes -
        rank * (sizeof(*tensor->bases) + sizeof(*tensor->valence));
    return PHY_OK;
}

static phy_status build_slot_group(phy_component_tensor *tensor,
                                   size_t group_budget)
{
    if (tensor->rank == 0u) {
        return PHY_OK;
    }
    phy_perm_limits group_limits = {0};
    group_limits.max_degree = tensor->limits.max_rank;
    group_limits.max_generators = tensor->limits.max_generators;
    group_limits.max_strong_generators =
        tensor->limits.max_strong_generators;
    group_limits.max_steps = tensor->limits.max_steps;
    group_limits.max_bytes = group_budget;
    phy_status status = phy_perm_group_create(
        tensor->rank, &group_limits, &tensor->group);
    for (size_t generator = 0u;
         status == PHY_OK &&
         generator < phy_tensor_head_symmetry_count(tensor->head);
         ++generator) {
        const uint16_t *image = NULL;
        int sign = 0;
        status = phy_tensor_head_symmetry(
            tensor->head, generator, &image, &sign);
        if (status == PHY_OK) {
            status = phy_perm_group_add_generator(
                tensor->group, image, sign);
        }
    }
    if (status == PHY_OK) {
        status = phy_perm_group_build_bsgs(tensor->group);
    }
    if (status == PHY_OK) {
        status = phy_perm_chain_allocate(
            tensor->group, &tensor->chain);
    }
    uint32_t steps = 0u;
    if (status == PHY_OK) {
        status = phy_perm_chain_build(
            tensor->group, &tensor->chain, &steps);
    }
    return status;
}

phy_status phy_component_tensor_create(
    const phy_tensor_head *head, phy_component_basis *const *bases,
    const phy_ir_variance *valence,
    const phy_component_limits *requested,
    phy_component_tensor **out_tensor)
{
    if (head == NULL || out_tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_tensor = NULL;
    phy_component_limits limits;
    phy_status status = resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    status = validate_realization(head, bases, valence, &limits);
    if (status != PHY_OK) {
        return status;
    }

    phy_component_tensor *tensor = phy_alloc(sizeof *tensor);
    if (tensor == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(tensor, 0, sizeof *tensor);
    tensor->head = head;
    tensor->limits = limits;
    tensor->rank = phy_tensor_head_slot_count(head);
    phy_abstract_context *abstract = phy_tensor_head_context(head);
    tensor->cas = phy_abstract_cas(abstract);
    tensor->ir = phy_cas_ir(tensor->cas);
    tensor->bytes_used = sizeof *tensor;

    const size_t group_budget =
        tensor->rank == 0u ? 0u : limits.max_bytes / 2u;
    tensor->other_budget = limits.max_bytes - group_budget;
    if (sizeof *tensor > tensor->other_budget) {
        phy_component_tensor_destroy(tensor);
        return PHY_ERR_MEMORY_LIMIT;
    }
    status = allocate_metadata(tensor, bases, valence);
    if (status == PHY_OK) {
        status = build_slot_group(tensor, group_budget);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(tensor->cas, 0, 1, &tensor->zero);
    }
    if (status != PHY_OK) {
        phy_component_tensor_destroy(tensor);
        return status;
    }
    *out_tensor = tensor;
    return PHY_OK;
}

void phy_component_tensor_destroy(phy_component_tensor *tensor)
{
    if (tensor == NULL) {
        return;
    }
    phy_free(tensor->entries, tensor->entry_bytes);
    phy_perm_chain_free(&tensor->chain);
    phy_perm_group_destroy(tensor->group);
    phy_free(tensor->metadata, tensor->metadata_bytes);
    phy_free(tensor, sizeof *tensor);
}

const phy_tensor_head *phy_component_tensor_head(
    const phy_component_tensor *tensor)
{
    return tensor != NULL ? tensor->head : NULL;
}

size_t phy_component_tensor_rank(const phy_component_tensor *tensor)
{
    return tensor != NULL ? tensor->rank : 0u;
}

const phy_component_basis *phy_component_tensor_basis(
    const phy_component_tensor *tensor, size_t slot)
{
    return tensor != NULL && slot < tensor->rank
               ? tensor->bases[slot]
               : NULL;
}

phy_ir_variance phy_component_tensor_valence(
    const phy_component_tensor *tensor, size_t slot)
{
    return tensor != NULL && slot < tensor->rank
               ? tensor->valence[slot]
               : PHY_IR_INDEX_LOWER;
}

size_t phy_component_tensor_entry_count(
    const phy_component_tensor *tensor)
{
    return tensor != NULL ? tensor->entry_count : 0u;
}

uint64_t phy_component_tensor_symmetry_order(
    const phy_component_tensor *tensor)
{
    if (tensor == NULL) {
        return 0u;
    }
    return tensor->rank == 0u ? 1u
                              : phy_perm_group_order(tensor->group);
}

size_t phy_component_tensor_bytes_used(
    const phy_component_tensor *tensor)
{
    if (tensor == NULL) {
        return 0u;
    }
    size_t bytes = tensor->bytes_used;
    if (tensor->group != NULL) {
        bytes += tensor->group->persistent_bytes + tensor->chain.bytes;
    }
    return bytes;
}

static phy_status validate_indices(const phy_component_tensor *tensor,
                                   const uint32_t *indices)
{
    if (tensor->rank != 0u && indices == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t slot = 0u; slot < tensor->rank; ++slot) {
        if ((size_t)indices[slot] >= tensor->bases[slot]->dimension) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
    }
    return PHY_OK;
}

static int compare_candidate_to_best(const phy_component_search *search,
                                     const uint16_t *permutation,
                                     size_t prefix_count)
{
    for (size_t slot = 0u; slot < prefix_count; ++slot) {
        const uint32_t candidate =
            search->input[permutation[slot]];
        const uint32_t best = search->tensor->best_indices[slot];
        if (candidate != best) {
            return candidate < best ? -1 : 1;
        }
    }
    return 0;
}

static phy_status visit_component_candidate(
    phy_component_search *search, const uint16_t *permutation, int sign)
{
    if (search->candidates >= search->tensor->limits.max_candidates) {
        return PHY_ERR_TIMEOUT;
    }
    ++search->candidates;
    const int order = search->best_found
                          ? compare_candidate_to_best(
                                search, permutation, search->tensor->rank)
                          : -1;
    if (!search->best_found || order < 0) {
        for (size_t slot = 0u; slot < search->tensor->rank; ++slot) {
            search->tensor->best_indices[slot] =
                search->input[permutation[slot]];
        }
        search->best_sign = sign;
        search->best_found = true;
    } else if (order == 0 && search->best_sign != sign) {
        search->zero = true;
    }
    return PHY_OK;
}

static phy_status traverse_components(phy_component_search *search,
                                      size_t level, int sign)
{
    phy_component_tensor *tensor = search->tensor;
    const size_t rank = tensor->rank;
    if (search->zero) {
        return PHY_OK;
    }
    if (level == rank) {
        return visit_component_candidate(
            search, &tensor->permutation_stack[level * rank], sign);
    }
    const uint16_t *current =
        &tensor->permutation_stack[level * rank];
    uint8_t *processed =
        &tensor->processed_points[level * rank];
    memset(processed, 0, rank * sizeof(*processed));
    const size_t choices = tensor->chain.orbit_count[level];
    for (size_t choice = 0u; choice < choices; ++choice) {
        size_t best_point = SIZE_MAX;
        uint32_t best_value = UINT32_MAX;
        for (size_t point = 0u; point < rank; ++point) {
            if (processed[point] != 0u ||
                tensor->chain.valid[level * rank + point] == 0u) {
                continue;
            }
            if ((uint64_t)rank >
                (uint64_t)tensor->limits.max_steps -
                    (uint64_t)search->steps) {
                return PHY_ERR_TIMEOUT;
            }
            search->steps += (uint32_t)rank;
            const uint16_t *transversal =
                &tensor->chain.transversal[
                    (level * rank + point) * rank];
            phy_perm_compose_unchecked(
                current, transversal, rank,
                tensor->choice_permutation);
            const uint32_t value =
                search->input[tensor->choice_permutation[level]];
            if (best_point == SIZE_MAX || value < best_value) {
                best_point = point;
                best_value = value;
            }
        }
        if (best_point == SIZE_MAX) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        processed[best_point] = 1u;
        const uint16_t *transversal =
            &tensor->chain.transversal[
                (level * rank + best_point) * rank];
        uint16_t *next =
            &tensor->permutation_stack[(level + 1u) * rank];
        phy_perm_compose_unchecked(current, transversal, rank, next);
        if (search->best_found &&
            compare_candidate_to_best(search, next, level + 1u) > 0) {
            continue;
        }
        const int next_sign =
            sign * tensor->chain.sign[level * rank + best_point];
        const phy_status status =
            traverse_components(search, level + 1u, next_sign);
        if (status != PHY_OK || search->zero) {
            return status;
        }
    }
    return PHY_OK;
}

phy_status phy_component_tensor_canonical_indices(
    phy_component_tensor *tensor, const uint32_t *indices,
    uint32_t *out_indices, int *out_sign)
{
    if (tensor == NULL || out_sign == NULL ||
        (tensor->rank != 0u && out_indices == NULL)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = validate_indices(tensor, indices);
    if (status != PHY_OK) {
        return status;
    }
    if (tensor->rank == 0u) {
        *out_sign = 1;
        return PHY_OK;
    }
    if (phy_perm_group_has_negative_identity(tensor->group)) {
        memcpy(out_indices, indices,
               tensor->rank * sizeof(*out_indices));
        *out_sign = 0;
        return PHY_OK;
    }

    phy_component_search search;
    memset(&search, 0, sizeof search);
    search.tensor = tensor;
    search.input = indices;
    phy_permutation_identity(
        tensor->permutation_stack, tensor->rank);
    status = traverse_components(&search, 0u, 1);
    if (status != PHY_OK) {
        return status;
    }
    if (!search.best_found) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    memcpy(out_indices, tensor->best_indices,
           tensor->rank * sizeof(*out_indices));
    *out_sign = search.zero ? 0 : search.best_sign;
    return PHY_OK;
}

static size_t entry_block_bytes(size_t rank, size_t capacity,
                                size_t *out_values_offset)
{
    if (rank != 0u && capacity > SIZE_MAX / rank) {
        return SIZE_MAX;
    }
    const size_t key_count = rank * capacity;
    if (key_count > SIZE_MAX / sizeof(uint32_t)) {
        return SIZE_MAX;
    }
    const size_t keys_bytes = key_count * sizeof(uint32_t);
    const size_t values_offset =
        align_up(keys_bytes, sizeof(phy_ir_ref));
    if (capacity >
        (SIZE_MAX - values_offset) / sizeof(phy_ir_ref)) {
        return SIZE_MAX;
    }
    *out_values_offset = values_offset;
    return values_offset + capacity * sizeof(phy_ir_ref);
}

static phy_status grow_entries(phy_component_tensor *tensor)
{
    if (tensor->entry_capacity >= tensor->limits.max_entries) {
        return PHY_ERR_TERM_LIMIT;
    }
    size_t capacity =
        tensor->entry_capacity == 0u ? 8u
                                     : tensor->entry_capacity * 2u;
    if (capacity < tensor->entry_capacity ||
        capacity > tensor->limits.max_entries) {
        capacity = tensor->limits.max_entries;
    }
    size_t values_offset = 0u;
    const size_t bytes =
        entry_block_bytes(tensor->rank, capacity, &values_offset);
    if (bytes == SIZE_MAX ||
        bytes > tensor->other_budget - tensor->bytes_used) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    void *entries = phy_alloc(bytes);
    if (entries == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(entries, 0, bytes);
    uint32_t *keys = entries;
    phy_ir_ref *values =
        (phy_ir_ref *)(void *)((uint8_t *)entries + values_offset);
    if (tensor->entry_count != 0u) {
        memcpy(keys, tensor->entry_indices,
               tensor->entry_count * tensor->rank *
                   sizeof(*tensor->entry_indices));
        memcpy(values, tensor->entry_values,
               tensor->entry_count * sizeof(*tensor->entry_values));
    }
    tensor->bytes_used += bytes;
    phy_free(tensor->entries, tensor->entry_bytes);
    tensor->bytes_used -= tensor->entry_bytes;
    tensor->entries = entries;
    tensor->entry_indices = keys;
    tensor->entry_values = values;
    tensor->entry_capacity = capacity;
    tensor->entry_bytes = bytes;
    return PHY_OK;
}

static bool same_key(const phy_component_tensor *tensor, size_t entry,
                     const uint32_t *indices)
{
    return tensor->rank == 0u ||
           memcmp(&tensor->entry_indices[entry * tensor->rank],
                  indices, tensor->rank * sizeof(*indices)) == 0;
}

static size_t find_entry(const phy_component_tensor *tensor,
                         const uint32_t *indices)
{
    for (size_t entry = 0u; entry < tensor->entry_count; ++entry) {
        if (same_key(tensor, entry, indices)) {
            return entry;
        }
    }
    return SIZE_MAX;
}

static phy_status exact_zero(phy_component_tensor *tensor,
                             phy_ir_ref value, bool *out_zero)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    const phy_status status =
        phy_cas_is_zero(tensor->cas, value, &decision);
    if (status != PHY_OK) {
        return status;
    }
    *out_zero = decision == PHY_CAS_ZERO;
    return PHY_OK;
}

static void remove_entry(phy_component_tensor *tensor, size_t entry)
{
    for (size_t move = entry + 1u;
         move < tensor->entry_count; ++move) {
        if (tensor->rank != 0u) {
            memcpy(
                &tensor->entry_indices[(move - 1u) * tensor->rank],
                &tensor->entry_indices[move * tensor->rank],
                tensor->rank * sizeof(*tensor->entry_indices));
        }
        tensor->entry_values[move - 1u] =
            tensor->entry_values[move];
    }
    --tensor->entry_count;
}

phy_status phy_component_tensor_set(
    phy_component_tensor *tensor, const uint32_t *indices,
    phy_ir_ref value)
{
    if (tensor == NULL || value == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    int sign = 0;
    phy_status status = phy_component_tensor_canonical_indices(
        tensor, indices, tensor->best_indices, &sign);
    if (status != PHY_OK) {
        return status;
    }
    bool zero = false;
    status = exact_zero(tensor, value, &zero);
    if (status != PHY_OK) {
        return status;
    }
    if (sign == 0) {
        return zero ? PHY_OK : PHY_ERR_ASSUMPTION;
    }
    phy_ir_ref canonical_value = value;
    if (sign < 0) {
        status = phy_cas_neg(
            tensor->cas, value, &canonical_value);
        if (status != PHY_OK) {
            return status;
        }
        status = exact_zero(tensor, canonical_value, &zero);
        if (status != PHY_OK) {
            return status;
        }
    }
    const size_t found = find_entry(tensor, tensor->best_indices);
    if (zero) {
        if (found != SIZE_MAX) {
            remove_entry(tensor, found);
        }
        return PHY_OK;
    }
    if (found != SIZE_MAX) {
        tensor->entry_values[found] = canonical_value;
        return PHY_OK;
    }
    if (tensor->entry_count == tensor->entry_capacity) {
        status = grow_entries(tensor);
        if (status != PHY_OK) {
            return status;
        }
    }
    if (tensor->rank != 0u) {
        memcpy(
            &tensor->entry_indices[tensor->entry_count * tensor->rank],
            tensor->best_indices,
            tensor->rank * sizeof(*tensor->entry_indices));
    }
    tensor->entry_values[tensor->entry_count] = canonical_value;
    ++tensor->entry_count;
    return PHY_OK;
}

phy_status phy_component_tensor_get(
    phy_component_tensor *tensor, const uint32_t *indices,
    phy_ir_ref *out_value)
{
    if (tensor == NULL || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    int sign = 0;
    phy_status status = phy_component_tensor_canonical_indices(
        tensor, indices, tensor->best_indices, &sign);
    if (status != PHY_OK) {
        return status;
    }
    if (sign == 0) {
        *out_value = tensor->zero;
        return PHY_OK;
    }
    const size_t found = find_entry(tensor, tensor->best_indices);
    phy_ir_ref value =
        found != SIZE_MAX ? tensor->entry_values[found] : tensor->zero;
    if (sign < 0) {
        status = phy_cas_neg(tensor->cas, value, &value);
        if (status != PHY_OK) {
            return status;
        }
    }
    *out_value = value;
    return PHY_OK;
}
