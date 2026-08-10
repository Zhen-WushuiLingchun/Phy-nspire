#include "abstract_internal.h"

#include <limits.h>
#include <string.h>

#include "permutation_internal.h"

#define PHY_CANON_DEFAULT_DEGREE 32u
#define PHY_CANON_DEFAULT_GENERATORS 256u
#define PHY_CANON_DEFAULT_STRONG 1024u
#define PHY_CANON_DEFAULT_STEPS 2000000u
#define PHY_CANON_DEFAULT_CANDIDATES 100000u
#define PHY_CANON_DEFAULT_BYTES (1024u * 1024u)

typedef struct {
    const phy_tensor_monomial *input;
    phy_ir_context *ir;
    const phy_tensor_canonical_limits *limits;
    const phy_perm_group *group;
    const phy_perm_chain *chain;
    const phy_abstract_index *base_indices;
    phy_abstract_index *candidate_indices;
    phy_abstract_index *best_indices;
    phy_canonical_key *candidate_key;
    phy_canonical_key *best_key;
    size_t *dummy_ordinal;
    uint8_t *dummy_occurrence;
    uint8_t *processed_points;
    uint16_t *permutation_stack;
    uint16_t *choice_permutation;
    size_t degree;
    uint32_t steps;
    uint64_t candidates;
    int best_sign;
    bool best_found;
    bool zero;
} phy_canonical_search;

static size_t align_up(size_t value, size_t alignment)
{
    const size_t remainder = value % alignment;
    return remainder == 0u ? value : value + alignment - remainder;
}

static bool reserve_array(size_t *total, size_t count, size_t element,
                          size_t *out_offset)
{
    if (total == NULL || out_offset == NULL ||
        (count != 0u && element > SIZE_MAX / count)) {
        return false;
    }
    const size_t aligned = align_up(*total, sizeof(void *));
    const size_t bytes = count * element;
    if (aligned > SIZE_MAX - bytes) {
        return false;
    }
    *out_offset = aligned;
    *total = aligned + bytes;
    return true;
}

void phy_tensor_canonical_limits_defaults(
    phy_tensor_canonical_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_degree = PHY_CANON_DEFAULT_DEGREE;
    out_limits->max_generators = PHY_CANON_DEFAULT_GENERATORS;
    out_limits->max_strong_generators = PHY_CANON_DEFAULT_STRONG;
    out_limits->max_steps = PHY_CANON_DEFAULT_STEPS;
    out_limits->max_candidates = PHY_CANON_DEFAULT_CANDIDATES;
    out_limits->max_bytes = PHY_CANON_DEFAULT_BYTES;
}

static phy_status resolve_limits(
    const phy_tensor_canonical_limits *requested,
    phy_tensor_canonical_limits *out)
{
    phy_tensor_canonical_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_degree != 0u) {
            out->max_degree = requested->max_degree;
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
    if (out->max_degree == 0u || out->max_degree > UINT16_MAX ||
        out->max_generators == 0u ||
        out->max_strong_generators == 0u ||
        out->max_steps == 0u || out->max_candidates == 0u ||
        out->max_bytes < 4096u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

static int compare_factor_heads(const phy_tensor_monomial *monomial,
                                size_t left, size_t right)
{
    const phy_abstract_tensor_head *left_head = monomial->factors[left].head;
    const phy_abstract_tensor_head *right_head = monomial->factors[right].head;
    if (left_head == right_head) {
        return 0;
    }
    return strcmp(phy_tensor_head_name(left_head),
                  phy_tensor_head_name(right_head));
}

/*
 * Stable insertion sort of one half-open run of factor positions by head name.
 * Stability preserves construction order between factors that share a head,
 * which is what leaves identical factors adjacent for the exchange generators
 * built below.
 */
static void sort_commuting_run(const phy_tensor_monomial *monomial,
                               size_t *order, size_t start, size_t end)
{
    for (size_t i = start + 1u; i < end; ++i) {
        const size_t value = order[i];
        size_t position = i;
        while (position != start &&
               compare_factor_heads(
                   monomial, value, order[position - 1u]) < 0) {
            order[position] = order[position - 1u];
            --position;
        }
        order[position] = value;
    }
}

/*
 * A commuting factor may be reordered inside its maximal commuting run; a
 * noncommuting factor is a barrier.  The old all-or-nothing rule abandoned
 * canonical ordering for every commuting factor as soon as one noncommuting
 * factor appeared anywhere in the monomial.
 *
 * Sorting each run independently preserves the noncommuting subsequence and
 * never carries a commuting factor across a noncommuting boundary.
 */
void phy_canonical_factor_order(const phy_tensor_monomial *monomial,
                                size_t *order)
{
    for (size_t i = 0u; i < monomial->factor_count; ++i) {
        order[i] = i;
    }
    size_t start = 0u;
    for (size_t i = 0u; i <= monomial->factor_count; ++i) {
        const bool barrier =
            i == monomial->factor_count ||
            monomial->factors[i].head->commutation !=
                PHY_TENSOR_COMMUTING;
        if (!barrier) {
            continue;
        }
        sort_commuting_run(monomial, order, start, i);
        start = i + 1u;
    }
}

void phy_canonical_arrange_indices(const phy_tensor_monomial *monomial,
                                   const size_t *order, size_t *offsets,
                                   phy_abstract_index *out_indices)
{
    size_t offset = 0u;
    for (size_t position = 0u; position < monomial->factor_count;
         ++position) {
        const phy_abstract_factor_record *factor =
            &monomial->factors[order[position]];
        offsets[position] = offset;
        if (factor->index_count != 0u) {
            memcpy(&out_indices[offset],
                   &monomial->indices[factor->index_offset],
                   factor->index_count * sizeof(*out_indices));
        }
        offset += factor->index_count;
    }
}

static phy_status add_lifted_generators(
    const phy_tensor_monomial *monomial, const size_t *order,
    const size_t *offsets, phy_perm_group *group, uint16_t *image)
{
    const size_t degree = monomial->index_count;
    for (size_t position = 0u; position < monomial->factor_count;
         ++position) {
        const phy_abstract_tensor_head *head =
            monomial->factors[order[position]].head;
        for (size_t generator = 0u;
             generator < head->generator_count; ++generator) {
            phy_permutation_identity(image, degree);
            for (size_t slot = 0u; slot < head->slot_count; ++slot) {
                image[offsets[position] + slot] =
                    (uint16_t)(offsets[position] +
                               head->generators[generator].image[slot]);
            }
            const phy_status status = phy_perm_group_add_generator(
                group, image, head->generators[generator].sign);
            if (status != PHY_OK) {
                return status;
            }
        }
    }
    return PHY_OK;
}

static phy_status add_factor_exchange_generators(
    const phy_tensor_monomial *monomial, const size_t *order,
    const size_t *offsets, phy_perm_group *group, uint16_t *image)
{
    const size_t degree = monomial->index_count;
    for (size_t position = 1u; position < monomial->factor_count;
         ++position) {
        const phy_abstract_tensor_head *left =
            monomial->factors[order[position - 1u]].head;
        const phy_abstract_tensor_head *right =
            monomial->factors[order[position]].head;
        /*
         * The sorted order preserves barriers.  Identical commuting heads in
         * different runs therefore cannot become adjacent and cannot acquire
         * an illegal exchange generator across a noncommuting factor.
         */
        if (left != right || left->commutation != PHY_TENSOR_COMMUTING ||
            left->slot_count == 0u) {
            continue;
        }
        const size_t width = left->slot_count;
        phy_permutation_identity(image, degree);
        for (size_t slot = 0u; slot < width; ++slot) {
            image[offsets[position - 1u] + slot] =
                (uint16_t)(offsets[position] + slot);
            image[offsets[position] + slot] =
                (uint16_t)(offsets[position - 1u] + slot);
        }
        const phy_status status =
            phy_perm_group_add_generator(group, image, 1);
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

phy_status phy_canonical_slot_generators(const phy_tensor_monomial *monomial,
                                         const size_t *order,
                                         const size_t *offsets,
                                         phy_perm_group *group,
                                         uint16_t *image)
{
    const phy_status status =
        add_lifted_generators(monomial, order, offsets, group, image);
    if (status != PHY_OK) {
        return status;
    }
    return add_factor_exchange_generators(
        monomial, order, offsets, group, image);
}

size_t phy_canonical_index_use(const phy_tensor_monomial *monomial,
                               const phy_abstract_index *index)
{
    for (size_t use = 0u; use < monomial->use_count; ++use) {
        if (monomial->uses[use].space == index->space &&
            monomial->uses[use].name == index->name) {
            return use;
        }
    }
    return SIZE_MAX;
}

static size_t next_dummy_ordinal(const phy_canonical_search *search,
                                 const phy_index_space *space)
{
    size_t next = 0u;
    for (size_t use = 0u; use < search->input->use_count; ++use) {
        if (search->input->uses[use].space == space &&
            search->dummy_ordinal[use] != SIZE_MAX &&
            search->dummy_ordinal[use] >= next) {
            next = search->dummy_ordinal[use] + 1u;
        }
    }
    return next;
}

/*
 * Position of one slot's variance in the canonical index alphabet.
 *
 * SymPy's `tensor_can` and xPerm both order that alphabet as the free indices
 * ascending by name followed by d0^, d0_, d1^, d1_, ..., so the raised member
 * of a contracted pair precedes the lowered one.  `phy_ir_variance` numbers
 * PHY_IR_INDEX_LOWER first, which is a storage detail of the IR and not this
 * ordering, so the rank is computed rather than cast.
 */
uint8_t phy_canonical_orientation_rank(phy_ir_variance variance)
{
    return variance == PHY_IR_INDEX_UPPER ? (uint8_t)0u : (uint8_t)1u;
}

int phy_canonical_compare_key(phy_ir_context *ir,
                              const phy_canonical_key *left,
                              const phy_canonical_key *right)
{
    if (left->role != right->role) {
        return left->role < right->role ? -1 : 1;
    }
    if (left->role == (uint8_t)PHY_ABSTRACT_INDEX_FREE) {
        const int name_order =
            strcmp(phy_ir_symbol_name(ir, left->free_name),
                   phy_ir_symbol_name(ir, right->free_name));
        if (name_order != 0) {
            return name_order;
        }
    } else if (left->ordinal != right->ordinal) {
        return left->ordinal < right->ordinal ? -1 : 1;
    }
    if (left->orientation != right->orientation) {
        return left->orientation < right->orientation ? -1 : 1;
    }
    return 0;
}

static int compare_keys(const phy_canonical_search *search)
{
    for (size_t slot = 0u; slot < search->degree; ++slot) {
        const int order = phy_canonical_compare_key(
            search->ir, &search->candidate_key[slot],
            &search->best_key[slot]);
        if (order != 0) {
            return order;
        }
    }
    return 0;
}

/*
 * The search traverses inverse slot permutations.  The group is closed under
 * inversion and a +/- character has the same sign on an element and its
 * inverse.  In this orientation permutation[slot] directly selects the source
 * index for that output slot, so a BSGS base 0,1,... fixes the canonical key
 * from left to right and admits exact lexicographic pruning.
 */
static phy_status normalize_prefix(phy_canonical_search *search,
                                   const uint16_t *permutation,
                                   size_t prefix_count,
                                   int *out_dummy_sign)
{
    for (size_t slot = 0u; slot < prefix_count; ++slot) {
        search->candidate_indices[slot] =
            search->base_indices[permutation[slot]];
    }
    for (size_t use = 0u; use < search->input->use_count; ++use) {
        search->dummy_ordinal[use] = SIZE_MAX;
        search->dummy_occurrence[use] = 0u;
    }

    int dummy_sign = 1;
    for (size_t slot = 0u; slot < prefix_count; ++slot) {
        phy_abstract_index *index = &search->candidate_indices[slot];
        const size_t use =
            phy_canonical_index_use(search->input, index);
        if (use == SIZE_MAX) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        const phy_abstract_index_use *census =
            &search->input->uses[use];
        phy_canonical_key *key = &search->candidate_key[slot];
        memset(key, 0, sizeof *key);
        key->role = (uint8_t)census->role;
        if (census->role == PHY_ABSTRACT_INDEX_FREE) {
            key->free_name = index->name;
            key->orientation =
                phy_canonical_orientation_rank(index->variance);
            continue;
        }

        if (search->dummy_ordinal[use] == SIZE_MAX) {
            search->dummy_ordinal[use] =
                next_dummy_ordinal(search, index->space);
        }
        key->ordinal = search->dummy_ordinal[use];
        const uint8_t occurrence = search->dummy_occurrence[use]++;
        if (occurrence > 1u) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        if (index->space->metric != PHY_METRIC_NONE) {
            /*
             * A metric identifies the two orientations of a contracted pair,
             * so the raised member is moved to the pair's first slot.  Doing
             * that across an antisymmetric metric contributes one minus sign.
             * Without a metric the two orientations are distinct index
             * values, the pair is left exactly as written, and the
             * orientation rank below is what distinguishes the arrangements.
             */
            if (occurrence == 0u &&
                index->variance == PHY_IR_INDEX_LOWER &&
                index->space->metric == PHY_METRIC_ANTISYMMETRIC) {
                dummy_sign = -dummy_sign;
            }
            index->variance =
                occurrence == 0u ? PHY_IR_INDEX_UPPER
                                 : PHY_IR_INDEX_LOWER;
        }
        key->orientation =
            phy_canonical_orientation_rank(index->variance);
    }
    *out_dummy_sign = dummy_sign;
    return PHY_OK;
}

static int compare_prefix_to_best(const phy_canonical_search *search,
                                  size_t prefix_count)
{
    for (size_t slot = 0u; slot < prefix_count; ++slot) {
        const int order = phy_canonical_compare_key(
            search->ir, &search->candidate_key[slot],
            &search->best_key[slot]);
        if (order != 0) {
            return order;
        }
    }
    return 0;
}

static phy_status evaluate_candidate(phy_canonical_search *search,
                                     const uint16_t *permutation,
                                     int slot_sign)
{
    if (search->candidates >= search->limits->max_candidates) {
        return PHY_ERR_TIMEOUT;
    }
    ++search->candidates;
    int dummy_sign = 1;
    const phy_status normalized = normalize_prefix(
        search, permutation, search->degree, &dummy_sign);
    if (normalized != PHY_OK) {
        return normalized;
    }

    const int total_sign = slot_sign * dummy_sign;
    if (!search->best_found) {
        memcpy(search->best_indices, search->candidate_indices,
               search->degree * sizeof(*search->best_indices));
        memcpy(search->best_key, search->candidate_key,
               search->degree * sizeof(*search->best_key));
        search->best_sign = total_sign;
        search->best_found = true;
        return PHY_OK;
    }
    const int order = compare_keys(search);
    if (order < 0) {
        memcpy(search->best_indices, search->candidate_indices,
               search->degree * sizeof(*search->best_indices));
        memcpy(search->best_key, search->candidate_key,
               search->degree * sizeof(*search->best_key));
        search->best_sign = total_sign;
    } else if (order == 0 && search->best_sign != total_sign) {
        search->zero = true;
    }
    return PHY_OK;
}

static phy_status traverse_slot_group(phy_canonical_search *search,
                                      size_t level, int sign)
{
    if (search->zero) {
        return PHY_OK;
    }
    if (level == search->degree) {
        return evaluate_candidate(
            search,
            &search->permutation_stack[level * search->degree], sign);
    }

    const size_t n = search->degree;
    const uint16_t *current =
        &search->permutation_stack[level * n];
    uint8_t *processed = &search->processed_points[level * n];
    memset(processed, 0, n * sizeof(*processed));
    const size_t choices = search->chain->orbit_count[level];
    for (size_t choice = 0u; choice < choices; ++choice) {
        size_t best_point = SIZE_MAX;
        phy_canonical_key best_entry;
        memset(&best_entry, 0, sizeof best_entry);

        /*
         * Visit this stabilizer orbit in canonical-key order.  Once the first
         * leaf is known, every greater prefix can be discarded exactly.
         */
        for (size_t point = 0u; point < n; ++point) {
            if (processed[point] != 0u ||
                search->chain->valid[level * n + point] == 0u) {
                continue;
            }
            if ((uint64_t)n >
                (uint64_t)search->limits->max_steps -
                    (uint64_t)search->steps) {
                return PHY_ERR_TIMEOUT;
            }
            search->steps += (uint32_t)n;
            const uint16_t *transversal =
                &search->chain->transversal[(level * n + point) * n];
            phy_perm_compose_unchecked(
                current, transversal, n, search->choice_permutation);
            int ignored_sign = 1;
            const phy_status normalized = normalize_prefix(
                search, search->choice_permutation, level + 1u,
                &ignored_sign);
            if (normalized != PHY_OK) {
                return normalized;
            }
            if (best_point == SIZE_MAX ||
                phy_canonical_compare_key(
                    search->ir, &search->candidate_key[level],
                    &best_entry) < 0) {
                best_point = point;
                best_entry = search->candidate_key[level];
            }
        }
        if (best_point == SIZE_MAX) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        processed[best_point] = 1u;
        const uint16_t *transversal =
            &search->chain->transversal[
                (level * n + best_point) * n];
        uint16_t *next =
            &search->permutation_stack[(level + 1u) * n];
        phy_perm_compose_unchecked(current, transversal, n, next);

        int ignored_sign = 1;
        phy_status status = normalize_prefix(
            search, next, level + 1u, &ignored_sign);
        if (status != PHY_OK) {
            return status;
        }
        if (search->best_found &&
            compare_prefix_to_best(search, level + 1u) > 0) {
            continue;
        }
        const int next_sign =
            sign * search->chain->sign[level * n + best_point];
        status = traverse_slot_group(search, level + 1u, next_sign);
        if (status != PHY_OK || search->zero) {
            return status;
        }
    }
    return PHY_OK;
}

static bool free_name_collision(const phy_tensor_monomial *monomial,
                                const phy_index_space *space,
                                const char *name)
{
    for (size_t use = 0u; use < monomial->use_count; ++use) {
        if (monomial->uses[use].role == PHY_ABSTRACT_INDEX_FREE &&
            monomial->uses[use].space == space &&
            strcmp(phy_ir_symbol_name(monomial->context->ir,
                                      monomial->uses[use].name),
                   name) == 0) {
            return true;
        }
    }
    return false;
}

static size_t append_decimal(char *buffer, size_t used, size_t capacity,
                             size_t value)
{
    char reversed[3u * sizeof(size_t) + 1u];
    size_t digits = 0u;
    do {
        reversed[digits++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u && digits < sizeof reversed);
    while (digits != 0u && used + 1u < capacity) {
        buffer[used++] = reversed[--digits];
    }
    buffer[used] = '\0';
    return used;
}

static phy_status dummy_symbol(const phy_tensor_monomial *monomial,
                               const phy_index_space *space,
                               size_t ordinal, phy_ir_symbol *out_symbol)
{
    char name[48];
    for (size_t suffix = 0u; suffix < 1000u; ++suffix) {
        size_t used = 0u;
        name[used++] = '_';
        name[used++] = 'd';
        name[used] = '\0';
        used = append_decimal(name, used, sizeof name, ordinal);
        if (suffix != 0u && used + 1u < sizeof name) {
            name[used++] = '_';
            name[used] = '\0';
            (void)append_decimal(name, used, sizeof name, suffix);
        }
        if (!free_name_collision(monomial, space, name)) {
            const phy_ir_symbol symbol =
                phy_ir_intern(monomial->context->ir, name);
            if (symbol == PHY_IR_NO_SYMBOL) {
                return phy_ir_last_error(monomial->context->ir);
            }
            *out_symbol = symbol;
            return PHY_OK;
        }
    }
    return PHY_ERR_TERM_LIMIT;
}

static phy_status rename_best_dummies(
    const phy_tensor_monomial *monomial, phy_abstract_index *indices,
    const phy_canonical_key *key, phy_ir_symbol *symbols)
{
    for (size_t slot = 0u; slot < monomial->index_count; ++slot) {
        symbols[slot] = PHY_IR_NO_SYMBOL;
        if (key[slot].role ==
            (uint8_t)PHY_ABSTRACT_INDEX_FREE) {
            continue;
        }
        for (size_t prior = 0u; prior < slot; ++prior) {
            if (key[prior].role ==
                    (uint8_t)PHY_ABSTRACT_INDEX_DUMMY &&
                indices[prior].space == indices[slot].space &&
                key[prior].ordinal == key[slot].ordinal) {
                symbols[slot] = symbols[prior];
                break;
            }
        }
        if (symbols[slot] == PHY_IR_NO_SYMBOL) {
            const phy_status status = dummy_symbol(
                monomial, indices[slot].space, key[slot].ordinal,
                &symbols[slot]);
            if (status != PHY_OK) {
                return status;
            }
        }
        indices[slot].name = symbols[slot];
    }
    return PHY_OK;
}

phy_status phy_canonical_build_output(
    const phy_tensor_monomial *monomial, const size_t *order,
    const size_t *offsets, phy_abstract_index *indices,
    const phy_canonical_key *key, phy_ir_symbol *symbols, int sign,
    bool zero, phy_abstract_factor *factors,
    phy_tensor_monomial **out_monomial)
{
    phy_ir_ref coefficient = monomial->coefficient;
    if (zero) {
        phy_status status =
            phy_cas_number(monomial->context->cas, 0, 1, &coefficient);
        if (status != PHY_OK) {
            return status;
        }
        return phy_tensor_monomial_create(
            monomial->context, coefficient, NULL, 0u, out_monomial);
    }
    if (sign < 0) {
        const phy_status status = phy_cas_neg(
            monomial->context->cas, coefficient, &coefficient);
        if (status != PHY_OK) {
            return status;
        }
    }
    phy_status status =
        rename_best_dummies(monomial, indices, key, symbols);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t position = 0u; position < monomial->factor_count;
         ++position) {
        const phy_abstract_factor_record *source =
            &monomial->factors[order[position]];
        factors[position].head = source->head;
        factors[position].indices =
            source->index_count != 0u ? &indices[offsets[position]]
                                      : NULL;
        factors[position].index_count = source->index_count;
    }
    return phy_tensor_monomial_create(
        monomial->context, coefficient, factors, monomial->factor_count,
        out_monomial);
}

phy_status phy_tensor_monomial_canonicalize(
    const phy_tensor_monomial *monomial,
    const phy_tensor_canonical_limits *requested,
    phy_tensor_monomial **out_monomial,
    phy_tensor_canonical_stats *out_stats)
{
    if (monomial == NULL || out_monomial == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_monomial = NULL;
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof *out_stats);
        out_stats->degree = monomial->index_count;
    }

    phy_tensor_canonical_limits limits;
    phy_status status = resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    const size_t degree = monomial->index_count;
    if (degree > limits.max_degree) {
        return PHY_ERR_TERM_LIMIT;
    }

    size_t order_offset = 0u;
    size_t offsets_offset = 0u;
    size_t base_offset = 0u;
    size_t candidate_offset = 0u;
    size_t best_offset = 0u;
    size_t candidate_key_offset = 0u;
    size_t best_key_offset = 0u;
    size_t dummy_ordinal_offset = 0u;
    size_t dummy_occurrence_offset = 0u;
    size_t processed_points_offset = 0u;
    size_t stack_offset = 0u;
    size_t choice_permutation_offset = 0u;
    size_t image_offset = 0u;
    size_t symbols_offset = 0u;
    size_t factors_offset = 0u;
    size_t scratch_bytes = 0u;
    bool layout_ok =
        reserve_array(&scratch_bytes, monomial->factor_count,
                      sizeof(size_t), &order_offset) &&
        reserve_array(&scratch_bytes, monomial->factor_count,
                      sizeof(size_t), &offsets_offset) &&
        reserve_array(&scratch_bytes, degree,
                      sizeof(phy_abstract_index), &base_offset) &&
        reserve_array(&scratch_bytes, degree,
                      sizeof(phy_abstract_index), &candidate_offset) &&
        reserve_array(&scratch_bytes, degree,
                      sizeof(phy_abstract_index), &best_offset) &&
        reserve_array(&scratch_bytes, degree,
                      sizeof(phy_canonical_key), &candidate_key_offset) &&
        reserve_array(&scratch_bytes, degree,
                      sizeof(phy_canonical_key), &best_key_offset) &&
        reserve_array(&scratch_bytes, monomial->use_count,
                      sizeof(size_t), &dummy_ordinal_offset) &&
        reserve_array(&scratch_bytes, monomial->use_count,
                      sizeof(uint8_t), &dummy_occurrence_offset) &&
        reserve_array(&scratch_bytes,
                      degree == 0u ? 0u : degree * degree,
                      sizeof(uint8_t), &processed_points_offset) &&
        reserve_array(&scratch_bytes,
                      degree == 0u ? 0u : (degree + 1u) * degree,
                      sizeof(uint16_t), &stack_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(uint16_t),
                      &choice_permutation_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(uint16_t),
                      &image_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(phy_ir_symbol),
                      &symbols_offset) &&
        reserve_array(&scratch_bytes, monomial->factor_count,
                      sizeof(phy_abstract_factor), &factors_offset);
    if (!layout_ok || scratch_bytes > limits.max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }

    uint8_t *scratch = phy_alloc(scratch_bytes == 0u ? 1u : scratch_bytes);
    if (scratch == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(scratch, 0, scratch_bytes == 0u ? 1u : scratch_bytes);
    size_t *order = (size_t *)(scratch + order_offset);
    size_t *offsets = (size_t *)(scratch + offsets_offset);
    phy_abstract_index *base_indices =
        (phy_abstract_index *)(scratch + base_offset);
    phy_abstract_index *candidate_indices =
        (phy_abstract_index *)(scratch + candidate_offset);
    phy_abstract_index *best_indices =
        (phy_abstract_index *)(scratch + best_offset);
    phy_canonical_key *candidate_key =
        (phy_canonical_key *)(scratch + candidate_key_offset);
    phy_canonical_key *best_key =
        (phy_canonical_key *)(scratch + best_key_offset);
    size_t *dummy_ordinal =
        (size_t *)(scratch + dummy_ordinal_offset);
    uint8_t *dummy_occurrence =
        (uint8_t *)(scratch + dummy_occurrence_offset);
    uint8_t *processed_points =
        (uint8_t *)(scratch + processed_points_offset);
    uint16_t *permutation_stack =
        (uint16_t *)(scratch + stack_offset);
    uint16_t *choice_permutation =
        (uint16_t *)(scratch + choice_permutation_offset);
    uint16_t *image = (uint16_t *)(scratch + image_offset);
    phy_ir_symbol *symbols =
        (phy_ir_symbol *)(scratch + symbols_offset);
    phy_abstract_factor *factors =
        (phy_abstract_factor *)(scratch + factors_offset);

    phy_canonical_factor_order(monomial, order);
    phy_canonical_arrange_indices(
        monomial, order, offsets, base_indices);

    if (degree == 0u) {
        status = phy_canonical_build_output(
            monomial, order, offsets, best_indices, best_key, symbols, 1,
            false, factors, out_monomial);
        phy_free(scratch, scratch_bytes == 0u ? 1u : scratch_bytes);
        return status;
    }

    if (limits.max_bytes - scratch_bytes < sizeof(phy_perm_group)) {
        phy_free(scratch, scratch_bytes);
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_perm_limits perm_limits = {0};
    perm_limits.max_degree = limits.max_degree;
    perm_limits.max_generators = limits.max_generators;
    perm_limits.max_strong_generators = limits.max_strong_generators;
    perm_limits.max_steps = limits.max_steps;
    perm_limits.max_bytes = limits.max_bytes - scratch_bytes;
    phy_perm_group *group = NULL;
    status = phy_perm_group_create(degree, &perm_limits, &group);
    if (status == PHY_OK) {
        status = phy_canonical_slot_generators(
            monomial, order, offsets, group, image);
    }
    if (status == PHY_OK) {
        status = phy_perm_group_build_bsgs(group);
    }
    if (status != PHY_OK) {
        phy_perm_group_destroy(group);
        phy_free(scratch, scratch_bytes);
        return status;
    }
    if (out_stats != NULL) {
        out_stats->slot_group_order = phy_perm_group_order(group);
    }

    bool zero = phy_perm_group_has_negative_identity(group);
    phy_canonical_search search;
    memset(&search, 0, sizeof search);
    phy_perm_chain chain;
    memset(&chain, 0, sizeof chain);
    if (status == PHY_OK && !zero) {
        status = phy_perm_chain_allocate(group, &chain);
    }
    uint32_t chain_steps = 0u;
    if (status == PHY_OK && !zero) {
        status = phy_perm_chain_build(group, &chain, &chain_steps);
    }
    if (status == PHY_OK && !zero) {
        search.input = monomial;
        search.ir = monomial->context->ir;
        search.limits = &limits;
        search.group = group;
        search.chain = &chain;
        search.base_indices = base_indices;
        search.candidate_indices = candidate_indices;
        search.best_indices = best_indices;
        search.candidate_key = candidate_key;
        search.best_key = best_key;
        search.dummy_ordinal = dummy_ordinal;
        search.dummy_occurrence = dummy_occurrence;
        search.processed_points = processed_points;
        search.permutation_stack = permutation_stack;
        search.choice_permutation = choice_permutation;
        search.degree = degree;
        phy_permutation_identity(permutation_stack, degree);
        status = traverse_slot_group(&search, 0u, 1);
        zero = search.zero;
    }
    if (out_stats != NULL) {
        out_stats->candidates_visited = search.candidates;
        out_stats->zero_by_symmetry = zero;
    }
    if (status == PHY_OK && !zero && !search.best_found) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    if (status == PHY_OK) {
        status = phy_canonical_build_output(
            monomial, order, offsets, best_indices, best_key, symbols,
            search.best_sign, zero, factors, out_monomial);
    }

    phy_perm_chain_free(&chain);
    phy_perm_group_destroy(group);
    phy_free(scratch, scratch_bytes);
    return status;
}
