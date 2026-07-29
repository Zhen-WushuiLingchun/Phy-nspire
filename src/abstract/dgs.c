/*
 * Phy-nspire — explicit bounded Butler-Portugal double-coset canonicalization.
 *
 * `src/abstract/canonical.c` answers the same question with a BSGS-guided
 * backtracking search that prunes on an exact lexicographic bound.  That is
 * the production path and it is the one that has to be fast.  This file
 * answers it by writing the double coset down and looking at all of it:
 *
 *     canonical(g) = min { d g s : d in D, s in S }
 *
 * with S the lifted slot group of the monomial and D the dummy group of its
 * contracted pairs, both enumerated element by element.  Nothing here has a
 * base, a strong generating set, a transversal or a bound; every one of the
 * |D| * |S| products is built and compared.  The two files share the
 * *definition* of the canonical form — the factor order, the key and its
 * ordering, the output assembly — and nothing of the search, so a
 * disagreement between them is evidence about a search and not about two
 * conventions drifting apart.
 *
 * The price is that |S| and |D| grow factorially, which is why the ceilings
 * here are much lower than the production ones and why reaching one is a
 * typed refusal rather than a slow answer.
 */
#include "abstract_internal.h"

#include <limits.h>
#include <string.h>

#include "permutation_internal.h"

#define PHY_DGS_DEFAULT_DEGREE 12u
#define PHY_DGS_DEFAULT_SLOT_ORDER 5040u
#define PHY_DGS_DEFAULT_DUMMY_ORDER 5040u
#define PHY_DGS_DEFAULT_PRODUCTS (1024u * 1024u)
#define PHY_DGS_DEFAULT_BYTES (1024u * 1024u)

/*
 * One index space that owns at least one contracted pair.  Its factor of the
 * dummy group is the hyperoctahedral group B_k when the space has a metric —
 * k! renumberings of the pairs times 2^k exchanges of a pair's two members —
 * and the symmetric group S_k when it does not, because without a metric the
 * two orientations are distinct index values rather than one index written
 * two ways.
 */
typedef struct {
    const phy_index_space *space;
    size_t pair_count;
    size_t first_pair;      /* this space's pair 0 in the flat pair list */
    uint64_t relabel_count; /* k! */
    uint64_t flip_count;    /* 2^k with a metric, 1 without */
    bool flip_negates;      /* an antisymmetric metric signs each exchange */
} phy_dgs_space;

typedef struct {
    const phy_tensor_monomial *input;
    phy_ir_context *ir;
    size_t degree;

    const phy_abstract_index *base;
    size_t *slot_pair; /* flat pair number, SIZE_MAX at a free slot */

    phy_dgs_space *spaces;
    size_t space_count;
    size_t pair_count;

    /* One element of D, decoded from its ordinal. */
    size_t *pair_ordinal;
    uint8_t *pair_flip;
    size_t *relabel_pool;

    /* Every element of S, enumerated by closure over the generators. */
    uint16_t *elements;
    int8_t *element_signs;
    uint32_t *buckets; /* open addressing, entry is 1 + element index */
    size_t bucket_mask;
    size_t element_count;
    bool negative_identity;

    phy_abstract_index *candidate_indices;
    phy_abstract_index *best_indices;
    phy_canonical_key *candidate_key;
    phy_canonical_key *best_key;
    int best_sign;
    bool best_found;
    bool zero;

    uint64_t products;
} phy_dgs_search;

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

void phy_tensor_dgs_limits_defaults(phy_tensor_dgs_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_degree = PHY_DGS_DEFAULT_DEGREE;
    out_limits->max_slot_group_order = PHY_DGS_DEFAULT_SLOT_ORDER;
    out_limits->max_dummy_group_order = PHY_DGS_DEFAULT_DUMMY_ORDER;
    out_limits->max_products = PHY_DGS_DEFAULT_PRODUCTS;
    out_limits->max_bytes = PHY_DGS_DEFAULT_BYTES;
}

static phy_status resolve_limits(const phy_tensor_dgs_limits *requested,
                                 phy_tensor_dgs_limits *out)
{
    phy_tensor_dgs_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_degree != 0u) {
            out->max_degree = requested->max_degree;
        }
        if (requested->max_slot_group_order != 0u) {
            out->max_slot_group_order = requested->max_slot_group_order;
        }
        if (requested->max_dummy_group_order != 0u) {
            out->max_dummy_group_order = requested->max_dummy_group_order;
        }
        if (requested->max_products != 0u) {
            out->max_products = requested->max_products;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
    }
    if (out->max_degree == 0u || out->max_degree > UINT16_MAX ||
        out->max_slot_group_order == 0u ||
        out->max_dummy_group_order == 0u || out->max_products == 0u ||
        out->max_bytes < 4096u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

/* --------------------------------------------------------- the dummy group */

/*
 * `value!`, refusing rather than wrapping.  The enumeration multiplies these
 * together, so an unnoticed overflow here would silently visit a fraction of
 * the double coset and report the wrong minimum as canonical.
 */
static bool factorial_bounded(size_t value, uint64_t ceiling,
                              uint64_t *out_result)
{
    uint64_t result = 1u;
    for (size_t i = 2u; i <= value; ++i) {
        if (result > ceiling / (uint64_t)i) {
            return false;
        }
        result *= (uint64_t)i;
    }
    *out_result = result;
    return true;
}

static bool power_of_two_bounded(size_t exponent, uint64_t ceiling,
                                 uint64_t *out_result)
{
    uint64_t result = 1u;
    for (size_t i = 0u; i < exponent; ++i) {
        if (result > ceiling / 2u) {
            return false;
        }
        result *= 2u;
    }
    *out_result = result;
    return true;
}

/*
 * Decode one renumbering of a space's pairs from its ordinal, in the
 * factorial number system, so that the ordinals 0..k!-1 hit every element of
 * S_k exactly once.
 */
static void decode_relabel(phy_dgs_search *search,
                           const phy_dgs_space *space, uint64_t code)
{
    const size_t count = space->pair_count;
    for (size_t i = 0u; i < count; ++i) {
        search->relabel_pool[i] = i;
    }
    size_t remaining = count;
    for (size_t i = 0u; i < count; ++i) {
        uint64_t block = 1u;
        for (size_t factor = 2u; factor < remaining; ++factor) {
            block *= (uint64_t)factor;
        }
        const size_t choice = (size_t)(code / block);
        code %= block;
        search->pair_ordinal[space->first_pair + i] =
            search->relabel_pool[choice];
        for (size_t j = choice; j + 1u < remaining; ++j) {
            search->relabel_pool[j] = search->relabel_pool[j + 1u];
        }
        --remaining;
    }
}

/*
 * Decode the whole dummy-group element `code` — a mixed-radix odometer over
 * the index spaces — and return its sign.
 */
static int decode_dummy_element(phy_dgs_search *search, uint64_t code)
{
    int sign = 1;
    for (size_t which = 0u; which < search->space_count; ++which) {
        const phy_dgs_space *space = &search->spaces[which];
        const uint64_t width = space->relabel_count * space->flip_count;
        const uint64_t local = code % width;
        code /= width;
        decode_relabel(search, space, local % space->relabel_count);
        const uint64_t flips = local / space->relabel_count;
        for (size_t pair = 0u; pair < space->pair_count; ++pair) {
            const uint8_t flipped = (uint8_t)((flips >> pair) & 1u);
            search->pair_flip[space->first_pair + pair] = flipped;
            if (flipped != 0u && space->flip_negates) {
                sign = -sign;
            }
        }
    }
    return sign;
}

/*
 * Census the contracted pairs: which index spaces own them, how many each
 * owns, and which flat pair number each slot carries.  The initial numbering
 * is by first occurrence, which is only a starting point — D contains every
 * renumbering, so the choice cannot affect the answer.
 */
static phy_status census_dummy_pairs(phy_dgs_search *search,
                                     size_t *use_pair, size_t *use_space,
                                     uint64_t ceiling,
                                     uint64_t *out_order)
{
    const phy_tensor_monomial *monomial = search->input;
    for (size_t use = 0u; use < monomial->use_count; ++use) {
        use_pair[use] = SIZE_MAX;
        use_space[use] = SIZE_MAX;
    }
    search->space_count = 0u;
    search->pair_count = 0u;

    /* Pass one: discover the spaces and count their pairs. */
    for (size_t slot = 0u; slot < search->degree; ++slot) {
        const size_t use =
            phy_canonical_index_use(monomial, &search->base[slot]);
        if (use == SIZE_MAX) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        if (monomial->uses[use].role != PHY_ABSTRACT_INDEX_DUMMY ||
            use_space[use] != SIZE_MAX) {
            continue;
        }
        const phy_index_space *space = search->base[slot].space;
        size_t which = 0u;
        while (which < search->space_count &&
               search->spaces[which].space != space) {
            ++which;
        }
        if (which == search->space_count) {
            memset(&search->spaces[which], 0, sizeof search->spaces[which]);
            search->spaces[which].space = space;
            ++search->space_count;
        }
        use_space[use] = which;
        ++search->spaces[which].pair_count;
        ++search->pair_count;
    }

    /* Pass two: lay the spaces out in the flat pair list and size D. */
    uint64_t order = 1u;
    size_t next_pair = 0u;
    for (size_t which = 0u; which < search->space_count; ++which) {
        phy_dgs_space *space = &search->spaces[which];
        space->first_pair = next_pair;
        next_pair += space->pair_count;
        const phy_metric_symmetry metric =
            phy_index_space_metric(space->space);
        space->flip_negates = metric == PHY_METRIC_ANTISYMMETRIC;
        if (!factorial_bounded(space->pair_count, ceiling,
                               &space->relabel_count) ||
            !power_of_two_bounded(
                metric == PHY_METRIC_NONE ? 0u : space->pair_count,
                ceiling, &space->flip_count)) {
            return PHY_ERR_TERM_LIMIT;
        }
        if (space->relabel_count > ceiling / space->flip_count) {
            return PHY_ERR_TERM_LIMIT;
        }
        const uint64_t width = space->relabel_count * space->flip_count;
        if (order > ceiling / width) {
            return PHY_ERR_TERM_LIMIT;
        }
        order *= width;
    }

    /* Pass three: hand out the flat pair numbers by first occurrence. */
    size_t *assigned = search->pair_ordinal; /* scratch: per-space counter */
    for (size_t which = 0u; which < search->space_count; ++which) {
        assigned[which] = 0u;
    }
    for (size_t slot = 0u; slot < search->degree; ++slot) {
        const size_t use =
            phy_canonical_index_use(monomial, &search->base[slot]);
        if (use == SIZE_MAX) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        if (monomial->uses[use].role != PHY_ABSTRACT_INDEX_DUMMY) {
            search->slot_pair[slot] = SIZE_MAX;
            continue;
        }
        if (use_pair[use] == SIZE_MAX) {
            const size_t which = use_space[use];
            use_pair[use] =
                search->spaces[which].first_pair + assigned[which];
            ++assigned[which];
        }
        search->slot_pair[slot] = use_pair[use];
    }
    *out_order = order;
    return PHY_OK;
}

/* ---------------------------------------------------------- the slot group */

static uint32_t hash_image(const uint16_t *image, size_t degree)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0u; i < degree; ++i) {
        hash = (hash ^ (uint32_t)(image[i] & 0xFFu)) * 16777619u;
        hash = (hash ^ (uint32_t)(image[i] >> 8)) * 16777619u;
    }
    return hash;
}

static bool find_element(const phy_dgs_search *search, const uint16_t *image,
                         size_t *out_index)
{
    const size_t degree = search->degree;
    size_t slot = (size_t)hash_image(image, degree) & search->bucket_mask;
    while (search->buckets[slot] != 0u) {
        const size_t candidate = (size_t)search->buckets[slot] - 1u;
        if (memcmp(&search->elements[candidate * degree], image,
                   degree * sizeof(*image)) == 0) {
            *out_index = candidate;
            return true;
        }
        slot = (slot + 1u) & search->bucket_mask;
    }
    return false;
}

static void insert_element(phy_dgs_search *search, size_t index)
{
    const size_t degree = search->degree;
    size_t slot = (size_t)hash_image(&search->elements[index * degree],
                                     degree) &
                  search->bucket_mask;
    while (search->buckets[slot] != 0u) {
        slot = (slot + 1u) & search->bucket_mask;
    }
    search->buckets[slot] = (uint32_t)(index + 1u);
}

/*
 * Close the generator list into the whole slot group.  Right multiplication
 * by generators from the identity reaches every element, so this is the
 * group and not a proper subset of it.
 *
 * Meeting a permutation that is already present but carrying the other sign
 * means the group contains the identity with sign -1, and then every element
 * carries both signs: the monomial is zero and |S| is twice the number of
 * distinct permutations.  The closure still runs to completion so that the
 * reported order is the same quantity `phy_perm_group_order` reports.
 */
static phy_status enumerate_slot_group(phy_dgs_search *search,
                                       const phy_perm_group *group,
                                       uint64_t capacity, uint16_t *product)
{
    const size_t degree = search->degree;
    memset(search->buckets, 0,
           (search->bucket_mask + 1u) * sizeof(*search->buckets));
    phy_permutation_identity(&search->elements[0], degree);
    search->element_signs[0] = 1;
    search->element_count = 1u;
    search->negative_identity = false;
    insert_element(search, 0u);

    for (size_t which = 0u; which < search->element_count; ++which) {
        for (size_t generator = 0u; generator < group->generator_count;
             ++generator) {
            phy_perm_compose_unchecked(
                &search->elements[which * degree],
                &group->generators[generator * degree], degree, product);
            const int sign = search->element_signs[which] *
                             group->generator_signs[generator];
            size_t existing = 0u;
            if (find_element(search, product, &existing)) {
                if (search->element_signs[existing] != sign) {
                    search->negative_identity = true;
                }
                continue;
            }
            if ((uint64_t)search->element_count >= capacity) {
                return PHY_ERR_TERM_LIMIT;
            }
            memcpy(&search->elements[search->element_count * degree],
                   product, degree * sizeof(*product));
            search->element_signs[search->element_count] = (int8_t)sign;
            insert_element(search, search->element_count);
            ++search->element_count;
        }
    }
    return PHY_OK;
}

/*
 * Every declared slot symmetry is an automorphism of the typed slot list, so
 * no element of S can move an index into a slot belonging to a different
 * index space.  The key deliberately does not compare index spaces, which is
 * sound only because of that.  Check it rather than assume it.
 */
static phy_status check_space_invariance(const phy_dgs_search *search)
{
    const size_t degree = search->degree;
    for (size_t which = 0u; which < search->element_count; ++which) {
        const uint16_t *image = &search->elements[which * degree];
        for (size_t slot = 0u; slot < degree; ++slot) {
            if (search->base[image[slot]].space !=
                search->base[slot].space) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
        }
    }
    return PHY_OK;
}

/* ------------------------------------------------------- the double coset */

static void build_candidate(phy_dgs_search *search, const uint16_t *image)
{
    for (size_t slot = 0u; slot < search->degree; ++slot) {
        const size_t source = image[slot];
        phy_abstract_index *out = &search->candidate_indices[slot];
        phy_canonical_key *key = &search->candidate_key[slot];
        *out = search->base[source];
        memset(key, 0, sizeof *key);
        const size_t pair = search->slot_pair[source];
        if (pair == SIZE_MAX) {
            key->role = (uint8_t)PHY_ABSTRACT_INDEX_FREE;
            key->free_name = out->name;
        } else {
            key->role = (uint8_t)PHY_ABSTRACT_INDEX_DUMMY;
            key->ordinal = search->pair_ordinal[pair];
            if (search->pair_flip[pair] != 0u) {
                out->variance = out->variance == PHY_IR_INDEX_UPPER
                                    ? PHY_IR_INDEX_LOWER
                                    : PHY_IR_INDEX_UPPER;
            }
        }
        key->orientation = phy_canonical_orientation_rank(out->variance);
    }
}

static int compare_candidate(const phy_dgs_search *search)
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

static void accept_candidate(phy_dgs_search *search, int sign)
{
    memcpy(search->best_indices, search->candidate_indices,
           search->degree * sizeof(*search->best_indices));
    memcpy(search->best_key, search->candidate_key,
           search->degree * sizeof(*search->best_key));
    search->best_sign = sign;
    search->best_found = true;
}

static phy_status visit_double_coset(phy_dgs_search *search,
                                     uint64_t dummy_order,
                                     uint64_t max_products)
{
    for (uint64_t element = 0u; element < dummy_order; ++element) {
        const int dummy_sign = decode_dummy_element(search, element);
        for (size_t which = 0u; which < search->element_count; ++which) {
            if (search->products >= max_products) {
                return PHY_ERR_TERM_LIMIT;
            }
            ++search->products;
            build_candidate(
                search, &search->elements[which * search->degree]);
            const int sign =
                dummy_sign * search->element_signs[which];
            if (!search->best_found) {
                accept_candidate(search, sign);
                continue;
            }
            const int order = compare_candidate(search);
            if (order < 0) {
                accept_candidate(search, sign);
            } else if (order == 0 && search->best_sign != sign) {
                /*
                 * One arrangement reached with both signs.  Sign is a
                 * homomorphism and the orbit is transitive, so this holds at
                 * every arrangement in the coset, not only at this one.
                 */
                search->zero = true;
                return PHY_OK;
            }
        }
    }
    return PHY_OK;
}

/* ------------------------------------------------------------ entry point */

phy_status phy_tensor_monomial_canonicalize_dgs(
    const phy_tensor_monomial *monomial,
    const phy_tensor_dgs_limits *requested,
    phy_tensor_monomial **out_monomial, phy_tensor_dgs_stats *out_stats)
{
    if (monomial == NULL || out_monomial == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_monomial = NULL;
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof *out_stats);
        out_stats->degree = monomial->index_count;
    }

    phy_tensor_dgs_limits limits;
    phy_status status = resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    const size_t degree = monomial->index_count;
    if (degree > limits.max_degree) {
        return PHY_ERR_TERM_LIMIT;
    }

    size_t element_capacity = (size_t)limits.max_slot_group_order;
    if ((uint64_t)element_capacity != limits.max_slot_group_order ||
        element_capacity > UINT32_MAX) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    if (element_capacity > SIZE_MAX / 2u) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    size_t bucket_count = 1u;
    while (bucket_count < element_capacity * 2u) {
        if (bucket_count > SIZE_MAX / 2u) {
            return PHY_ERR_MEMORY_LIMIT;
        }
        bucket_count *= 2u;
    }

    size_t order_offset = 0u;
    size_t offsets_offset = 0u;
    size_t base_offset = 0u;
    size_t slot_pair_offset = 0u;
    size_t spaces_offset = 0u;
    size_t pair_ordinal_offset = 0u;
    size_t pair_flip_offset = 0u;
    size_t relabel_pool_offset = 0u;
    size_t use_pair_offset = 0u;
    size_t use_space_offset = 0u;
    size_t elements_offset = 0u;
    size_t element_signs_offset = 0u;
    size_t buckets_offset = 0u;
    size_t product_offset = 0u;
    size_t image_offset = 0u;
    size_t candidate_offset = 0u;
    size_t best_offset = 0u;
    size_t candidate_key_offset = 0u;
    size_t best_key_offset = 0u;
    size_t symbols_offset = 0u;
    size_t factors_offset = 0u;
    size_t scratch_bytes = 0u;
    /*
     * `use_count` bounds both the number of distinct dummy-bearing index
     * spaces and the number of contracted pairs, so it sizes every census
     * array without a second traversal.
     */
    const size_t use_count = monomial->use_count;
    const bool layout_ok =
        reserve_array(&scratch_bytes, monomial->factor_count,
                      sizeof(size_t), &order_offset) &&
        reserve_array(&scratch_bytes, monomial->factor_count,
                      sizeof(size_t), &offsets_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(phy_abstract_index),
                      &base_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(size_t),
                      &slot_pair_offset) &&
        reserve_array(&scratch_bytes, use_count, sizeof(phy_dgs_space),
                      &spaces_offset) &&
        reserve_array(&scratch_bytes, use_count, sizeof(size_t),
                      &pair_ordinal_offset) &&
        reserve_array(&scratch_bytes, use_count, sizeof(uint8_t),
                      &pair_flip_offset) &&
        reserve_array(&scratch_bytes, use_count, sizeof(size_t),
                      &relabel_pool_offset) &&
        reserve_array(&scratch_bytes, use_count, sizeof(size_t),
                      &use_pair_offset) &&
        reserve_array(&scratch_bytes, use_count, sizeof(size_t),
                      &use_space_offset) &&
        reserve_array(&scratch_bytes,
                      degree == 0u ? 0u : element_capacity * degree,
                      sizeof(uint16_t), &elements_offset) &&
        reserve_array(&scratch_bytes, degree == 0u ? 0u : element_capacity,
                      sizeof(int8_t), &element_signs_offset) &&
        reserve_array(&scratch_bytes, degree == 0u ? 0u : bucket_count,
                      sizeof(uint32_t), &buckets_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(uint16_t),
                      &product_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(uint16_t),
                      &image_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(phy_abstract_index),
                      &candidate_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(phy_abstract_index),
                      &best_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(phy_canonical_key),
                      &candidate_key_offset) &&
        reserve_array(&scratch_bytes, degree, sizeof(phy_canonical_key),
                      &best_key_offset) &&
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
    const size_t owned_bytes = scratch_bytes == 0u ? 1u : scratch_bytes;

    size_t *order = (size_t *)(scratch + order_offset);
    size_t *offsets = (size_t *)(scratch + offsets_offset);
    phy_abstract_index *base =
        (phy_abstract_index *)(scratch + base_offset);
    phy_ir_symbol *symbols = (phy_ir_symbol *)(scratch + symbols_offset);
    phy_abstract_factor *factors =
        (phy_abstract_factor *)(scratch + factors_offset);

    phy_dgs_search search;
    memset(&search, 0, sizeof search);
    search.input = monomial;
    search.ir = monomial->context->ir;
    search.degree = degree;
    search.base = base;
    search.slot_pair = (size_t *)(scratch + slot_pair_offset);
    search.spaces = (phy_dgs_space *)(scratch + spaces_offset);
    search.pair_ordinal = (size_t *)(scratch + pair_ordinal_offset);
    search.pair_flip = (uint8_t *)(scratch + pair_flip_offset);
    search.relabel_pool = (size_t *)(scratch + relabel_pool_offset);
    search.elements = (uint16_t *)(scratch + elements_offset);
    search.element_signs = (int8_t *)(scratch + element_signs_offset);
    search.buckets = (uint32_t *)(scratch + buckets_offset);
    search.bucket_mask = bucket_count - 1u;
    search.candidate_indices =
        (phy_abstract_index *)(scratch + candidate_offset);
    search.best_indices = (phy_abstract_index *)(scratch + best_offset);
    search.candidate_key =
        (phy_canonical_key *)(scratch + candidate_key_offset);
    search.best_key = (phy_canonical_key *)(scratch + best_key_offset);
    search.best_sign = 1;

    phy_canonical_factor_order(monomial, order);
    phy_canonical_arrange_indices(monomial, order, offsets, base);

    if (degree == 0u) {
        status = phy_canonical_build_output(
            monomial, order, offsets, search.best_indices, search.best_key,
            symbols, 1, false, factors, out_monomial);
        phy_free(scratch, owned_bytes);
        return status;
    }

    uint64_t dummy_order = 1u;
    status = census_dummy_pairs(
        &search, (size_t *)(scratch + use_pair_offset),
        (size_t *)(scratch + use_space_offset),
        limits.max_dummy_group_order, &dummy_order);
    if (out_stats != NULL) {
        out_stats->dummy_pair_count = search.pair_count;
    }
    if (status != PHY_OK) {
        phy_free(scratch, owned_bytes);
        return status;
    }
    if (out_stats != NULL) {
        out_stats->dummy_group_order = dummy_order;
    }

    /*
     * The generators are taken from the same builder the production search
     * feeds to Schreier-Sims, so the two paths are compared on identical
     * input.  The group object is only a generator container here; no base or
     * strong generating set is ever built from it.
     */
    if (limits.max_bytes - scratch_bytes < sizeof(phy_perm_group)) {
        phy_free(scratch, owned_bytes);
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_perm_limits perm_limits = {0};
    perm_limits.max_degree = limits.max_degree;
    perm_limits.max_bytes = limits.max_bytes - scratch_bytes;
    phy_perm_group *group = NULL;
    status = phy_perm_group_create(degree, &perm_limits, &group);
    if (status == PHY_OK) {
        status = phy_canonical_slot_generators(
            monomial, order, offsets, group,
            (uint16_t *)(scratch + image_offset));
    }
    if (status == PHY_OK) {
        status = enumerate_slot_group(
            &search, group, limits.max_slot_group_order,
            (uint16_t *)(scratch + product_offset));
    }
    phy_perm_group_destroy(group);
    if (status == PHY_OK) {
        status = check_space_invariance(&search);
    }
    if (status != PHY_OK) {
        phy_free(scratch, owned_bytes);
        return status;
    }

    const uint64_t slot_order = (uint64_t)search.element_count *
                                (search.negative_identity ? 2u : 1u);
    if (slot_order > limits.max_slot_group_order) {
        phy_free(scratch, owned_bytes);
        return PHY_ERR_TERM_LIMIT;
    }
    if (out_stats != NULL) {
        out_stats->slot_group_order = slot_order;
    }
    if (dummy_order > limits.max_products / (uint64_t)search.element_count) {
        phy_free(scratch, owned_bytes);
        return PHY_ERR_TERM_LIMIT;
    }

    search.zero = search.negative_identity;
    if (!search.zero) {
        status = visit_double_coset(
            &search, dummy_order, limits.max_products);
    }
    if (out_stats != NULL) {
        out_stats->products_visited = search.products;
        out_stats->zero_by_symmetry = search.zero;
    }
    if (status == PHY_OK && !search.zero && !search.best_found) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    if (status == PHY_OK) {
        status = phy_canonical_build_output(
            monomial, order, offsets, search.best_indices, search.best_key,
            symbols, search.best_sign, search.zero, factors, out_monomial);
    }
    phy_free(scratch, owned_bytes);
    return status;
}
