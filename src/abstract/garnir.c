/*
 * Phy-nspire — general Young tableau validation, declared Young symmetry, and
 * exact multi-term Young/Garnir reduction.
 *
 * The monoterm layer in canonical.c decides equality under signed slot
 * permutations.  A Young symmetry is not of that kind: the first Bianchi
 * identity relates three index arrangements and is a member of no signed slot
 * group.  Nothing here is ever folded into the BSGS layer.  A declaration adds
 * exactly one thing — the statement that the tensor lies in the image of the
 * normalized Young projector — and the reducer below is the only code that
 * acts on it.
 *
 * The reducer applies the projector rather than rewriting with Garnir
 * relations.  The relation submodule of a projector P is exactly ker P, so two
 * expressions agree modulo the declared relations if and only if their
 * projections agree.  That makes projection a normal form whose completeness
 * needs no straightening argument.  The Garnir relations are still built,
 * because they are the readable form of the identity, but they are stated and
 * checked against the projector rather than trusted by it.
 */
#include "abstract_internal.h"

#include <limits.h>
#include <string.h>

#include "phy/permutation.h"

/*
 * Exact 64-bit combinatorics stop at 20 slots because 21! leaves the range.
 * Every group order this layer can afford to enumerate is far below that.
 */
#define PHY_YOUNG_MAX_EXACT_DEGREE 20u
#define PHY_YOUNG_MAX_STANDARD_TABLEAUX 4096u

static bool garnir_reserve(size_t *total, size_t count, size_t element,
                           size_t *out_offset)
{
    if (total == NULL || out_offset == NULL ||
        (count != 0u && element > SIZE_MAX / count)) {
        return false;
    }
    const size_t alignment = sizeof(void *);
    const size_t remainder = *total % alignment;
    const size_t padding =
        remainder == 0u ? 0u : alignment - remainder;
    if (*total > SIZE_MAX - padding) {
        return false;
    }
    const size_t aligned = *total + padding;
    const size_t bytes = count * element;
    if (aligned > SIZE_MAX - bytes) {
        return false;
    }
    *out_offset = aligned;
    *total = aligned + bytes;
    return true;
}

static bool garnir_reserve_matrix(size_t *total, size_t rows,
                                  size_t columns, size_t element,
                                  size_t *out_offset)
{
    if (columns != 0u && element > SIZE_MAX / columns) {
        return false;
    }
    return garnir_reserve(
        total, rows, columns * element, out_offset);
}

static phy_status factorial_exact(size_t value, uint64_t *out)
{
    if (value > PHY_YOUNG_MAX_EXACT_DEGREE) {
        return PHY_ERR_OVERFLOW;
    }
    uint64_t product = 1u;
    for (size_t factor = 2u; factor <= value; ++factor) {
        product *= (uint64_t)factor;
    }
    *out = product;
    return PHY_OK;
}

static phy_status factorial_product(const uint16_t *lengths, size_t count,
                                    uint64_t *out)
{
    uint64_t product = 1u;
    for (size_t group = 0u; group < count; ++group) {
        uint64_t block = 0u;
        const phy_status status =
            factorial_exact((size_t)lengths[group], &block);
        if (status != PHY_OK) {
            return status;
        }
        if (block != 0u && product > UINT64_MAX / block) {
            return PHY_ERR_OVERFLOW;
        }
        product *= block;
    }
    *out = product;
    return PHY_OK;
}

/* ------------------------------------------------------ tableau validation */

static phy_status validate_shape(const phy_young_tableau *tableau,
                                 size_t *out_column_count)
{
    if (tableau == NULL || tableau->slots == NULL ||
        tableau->row_lengths == NULL || tableau->row_count == 0u ||
        tableau->slot_count == 0u ||
        (tableau->order != PHY_YOUNG_ROW_SYMMETRY_LAST &&
         tableau->order != PHY_YOUNG_COLUMN_ANTISYMMETRY_LAST)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (tableau->slot_count > UINT16_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    size_t sum = 0u;
    for (size_t row = 0u; row < tableau->row_count; ++row) {
        const size_t length = tableau->row_lengths[row];
        if (length == 0u ||
            (row != 0u && length > tableau->row_lengths[row - 1u]) ||
            length > tableau->slot_count ||
            sum > tableau->slot_count - length) {
            return PHY_ERR_TYPE;
        }
        sum += length;
    }
    if (sum != tableau->slot_count) {
        return PHY_ERR_TYPE;
    }
    for (size_t i = 0u; i < tableau->slot_count; ++i) {
        if ((size_t)tableau->slots[i] >= tableau->slot_count) {
            return PHY_ERR_TYPE;
        }
        for (size_t prior = 0u; prior < i; ++prior) {
            if (tableau->slots[prior] == tableau->slots[i]) {
                return PHY_ERR_TYPE;
            }
        }
    }
    *out_column_count = tableau->row_lengths[0];
    return PHY_OK;
}

static size_t cell_offset(const phy_young_tableau *tableau, size_t row)
{
    size_t offset = 0u;
    for (size_t prior = 0u; prior < row; ++prior) {
        offset += tableau->row_lengths[prior];
    }
    return offset;
}

static bool tableau_is_standard(const phy_young_tableau *tableau)
{
    for (size_t row = 0u; row < tableau->row_count; ++row) {
        const size_t offset = cell_offset(tableau, row);
        for (size_t column = 1u;
             column < tableau->row_lengths[row]; ++column) {
            if (tableau->slots[offset + column] <=
                tableau->slots[offset + column - 1u]) {
                return false;
            }
        }
        if (row == 0u) {
            continue;
        }
        const size_t above = cell_offset(tableau, row - 1u);
        for (size_t column = 0u;
             column < tableau->row_lengths[row]; ++column) {
            if (tableau->slots[offset + column] <=
                tableau->slots[above + column]) {
                return false;
            }
        }
    }
    return true;
}

static size_t column_length_of(const phy_young_tableau *tableau,
                               size_t column)
{
    size_t length = 0u;
    for (size_t row = 0u; row < tableau->row_count; ++row) {
        if (column < tableau->row_lengths[row]) {
            ++length;
        }
    }
    return length;
}

static phy_status hook_product_of(const phy_young_tableau *tableau,
                                  uint64_t *out)
{
    uint64_t product = 1u;
    for (size_t row = 0u; row < tableau->row_count; ++row) {
        for (size_t column = 0u;
             column < tableau->row_lengths[row]; ++column) {
            uint64_t hook =
                (uint64_t)tableau->row_lengths[row] - (uint64_t)column;
            for (size_t below = row + 1u;
                 below < tableau->row_count; ++below) {
                if (column < tableau->row_lengths[below]) {
                    ++hook;
                }
            }
            if (product > UINT64_MAX / hook) {
                return PHY_ERR_OVERFLOW;
            }
            product *= hook;
        }
    }
    if (product > (uint64_t)INT64_MAX) {
        return PHY_ERR_OVERFLOW;
    }
    *out = product;
    return PHY_OK;
}

phy_status phy_young_tableau_column_lengths(
    const phy_young_tableau *tableau, uint16_t *out_lengths,
    size_t capacity, size_t *out_count)
{
    if (out_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    size_t column_count = 0u;
    const phy_status status = validate_shape(tableau, &column_count);
    if (status != PHY_OK) {
        return status;
    }
    if (out_lengths != NULL && column_count > capacity) {
        return PHY_ERR_TERM_LIMIT;
    }
    for (size_t column = 0u;
         column < column_count && out_lengths != NULL; ++column) {
        out_lengths[column] =
            (uint16_t)column_length_of(tableau, column);
    }
    *out_count = column_count;
    return PHY_OK;
}

phy_status phy_young_tableau_validate(const phy_young_tableau *tableau,
                                      phy_young_tableau_info *out_info)
{
    if (out_info == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(out_info, 0, sizeof *out_info);
    size_t column_count = 0u;
    phy_status status = validate_shape(tableau, &column_count);
    if (status != PHY_OK) {
        return status;
    }
    out_info->slot_count = tableau->slot_count;
    out_info->row_count = tableau->row_count;
    out_info->column_count = column_count;
    out_info->standard = tableau_is_standard(tableau);

    /*
     * The structural verdict above is already final.  Only the exact
     * combinatorial invariants can still fail, and they fail only by leaving
     * the 64-bit range, which needs more than twenty slots.
     */
    status = factorial_product(
        tableau->row_lengths, tableau->row_count,
        &out_info->row_group_order);
    out_info->column_group_order = 1u;
    for (size_t column = 0u;
         column < column_count && status == PHY_OK; ++column) {
        uint64_t block = 0u;
        status = factorial_exact(column_length_of(tableau, column), &block);
        if (status == PHY_OK &&
            out_info->column_group_order > UINT64_MAX / block) {
            status = PHY_ERR_OVERFLOW;
        }
        if (status == PHY_OK) {
            out_info->column_group_order *= block;
        }
    }
    if (status == PHY_OK) {
        status = hook_product_of(tableau, &out_info->hook_product);
    }
    uint64_t total = 0u;
    if (status == PHY_OK) {
        status = factorial_exact(tableau->slot_count, &total);
    }
    if (status == PHY_OK) {
        out_info->standard_tableau_count =
            total / out_info->hook_product;
    }
    return status;
}

phy_status phy_young_tableau_check_head(
    const phy_abstract_tensor_head *head,
    const phy_young_tableau *tableau, phy_young_tableau_info *out_info)
{
    if (head == NULL || tableau == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_young_tableau_info local;
    phy_status status = phy_young_tableau_validate(tableau, &local);
    if (status != PHY_OK) {
        return status;
    }
    if (tableau->slot_count != head->slot_count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    /*
     * A symmetrizer that mixed two index spaces would take a well-typed head
     * to an ill-typed application, so row and column homogeneity is a typing
     * rule rather than a convenience.
     */
    for (size_t row = 0u; row < tableau->row_count; ++row) {
        const size_t offset = cell_offset(tableau, row);
        const phy_index_space *space =
            head->slot_spaces[tableau->slots[offset]];
        for (size_t cell = 1u; cell < tableau->row_lengths[row]; ++cell) {
            if (head->slot_spaces[tableau->slots[offset + cell]] != space) {
                return PHY_ERR_TYPE;
            }
        }
    }
    for (size_t column = 0u; column < local.column_count; ++column) {
        const phy_index_space *space = NULL;
        for (size_t row = 0u; row < tableau->row_count; ++row) {
            if (column >= tableau->row_lengths[row]) {
                continue;
            }
            const phy_index_space *cell_space =
                head->slot_spaces[
                    tableau->slots[cell_offset(tableau, row) + column]];
            if (space != NULL && cell_space != space) {
                return PHY_ERR_TYPE;
            }
            space = cell_space;
        }
    }
    if (out_info != NULL) {
        *out_info = local;
    }
    return PHY_OK;
}

/* --------------------------------------------- standard tableau enumeration */

typedef struct {
    const uint16_t *row_lengths;
    size_t row_count;
    size_t slot_count;
    uint16_t *filled;   /* current length of each row */
    uint16_t *labels;   /* row-major cell labels of the current tableau */
    uint16_t *out;
    size_t capacity;    /* entries available in `out`, 0 when counting */
    size_t produced;
} phy_standard_walk;

static phy_status standard_walk(phy_standard_walk *walk, uint16_t label)
{
    if (label == (uint16_t)walk->slot_count) {
        if (walk->out != NULL) {
            if (walk->produced >= walk->capacity / walk->slot_count) {
                return PHY_ERR_TERM_LIMIT;
            }
            memcpy(&walk->out[walk->produced * walk->slot_count],
                   walk->labels,
                   walk->slot_count * sizeof(*walk->labels));
        }
        if (walk->produced == SIZE_MAX) {
            return PHY_ERR_TERM_LIMIT;
        }
        ++walk->produced;
        return PHY_OK;
    }
    size_t offset = 0u;
    for (size_t row = 0u; row < walk->row_count; ++row) {
        const size_t used = walk->filled[row];
        if (used < walk->row_lengths[row] &&
            (row == 0u || used < walk->filled[row - 1u])) {
            walk->labels[offset + used] = label;
            walk->filled[row] = (uint16_t)(used + 1u);
            const phy_status status =
                standard_walk(walk, (uint16_t)(label + 1u));
            walk->filled[row] = (uint16_t)used;
            if (status != PHY_OK) {
                return status;
            }
        }
        offset += walk->row_lengths[row];
    }
    return PHY_OK;
}

phy_status phy_young_standard_tableaux(
    const uint16_t *row_lengths, size_t row_count, uint16_t *out_entries,
    size_t capacity, size_t *out_count)
{
    if (row_lengths == NULL || row_count == 0u || out_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    size_t slot_count = 0u;
    for (size_t row = 0u; row < row_count; ++row) {
        if (row_lengths[row] == 0u ||
            (row != 0u && row_lengths[row] > row_lengths[row - 1u])) {
            return PHY_ERR_TYPE;
        }
        if (slot_count > SIZE_MAX - row_lengths[row]) {
            return PHY_ERR_TERM_LIMIT;
        }
        slot_count += row_lengths[row];
    }
    if (slot_count > PHY_YOUNG_MAX_EXACT_DEGREE) {
        return PHY_ERR_TERM_LIMIT;
    }
    uint16_t identity[PHY_YOUNG_MAX_EXACT_DEGREE];
    for (size_t slot = 0u; slot < slot_count; ++slot) {
        identity[slot] = (uint16_t)slot;
    }
    const phy_young_tableau shape = {
        identity, slot_count, row_lengths, row_count,
        PHY_YOUNG_ROW_SYMMETRY_LAST};
    uint64_t total = 0u;
    uint64_t hooks = 0u;
    phy_status status = factorial_exact(slot_count, &total);
    if (status == PHY_OK) {
        status = hook_product_of(&shape, &hooks);
    }
    if (status != PHY_OK) {
        return status;
    }
    const uint64_t expected = total / hooks;
    if (expected > PHY_YOUNG_MAX_STANDARD_TABLEAUX) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (out_entries != NULL &&
        expected > (uint64_t)(capacity / slot_count)) {
        return PHY_ERR_TERM_LIMIT;
    }

    const size_t scratch_bytes = 2u * slot_count * sizeof(uint16_t);
    uint16_t *scratch = phy_alloc(scratch_bytes);
    if (scratch == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(scratch, 0, scratch_bytes);
    phy_standard_walk walk;
    memset(&walk, 0, sizeof walk);
    walk.row_lengths = row_lengths;
    walk.row_count = row_count;
    walk.slot_count = slot_count;
    walk.filled = scratch;
    walk.labels = scratch + slot_count;
    walk.out = out_entries;
    walk.capacity = out_entries != NULL ? capacity : 0u;
    status = standard_walk(&walk, 0u);
    if (status == PHY_OK && walk.produced != (size_t)expected) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    if (status == PHY_OK) {
        *out_count = walk.produced;
    }
    phy_free(scratch, scratch_bytes);
    return status;
}

static uint64_t young_gcd(uint64_t left, uint64_t right)
{
    while (right != 0u) {
        const uint64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

phy_status phy_young_gl_dimension(const phy_young_tableau *tableau,
                                  size_t dimension,
                                  uint64_t *out_dimension)
{
    if (out_dimension == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_dimension = 0u;
    phy_young_tableau_info info;
    phy_status status = phy_young_tableau_validate(tableau, &info);
    if (status != PHY_OK) {
        return status;
    }
    if (info.slot_count > PHY_YOUNG_MAX_EXACT_DEGREE) {
        return PHY_ERR_TERM_LIMIT;
    }

    uint64_t numerators[PHY_YOUNG_MAX_EXACT_DEGREE];
    uint64_t denominators[PHY_YOUNG_MAX_EXACT_DEGREE];
    size_t cell = 0u;
    for (size_t row = 0u; row < tableau->row_count; ++row) {
        for (size_t column = 0u; column < tableau->row_lengths[row];
             ++column) {
            if (dimension <= row && column <= row - dimension) {
                *out_dimension = 0u;
                return PHY_OK;
            }
            if (dimension >= row &&
                (uint64_t)(dimension - row) >
                    UINT64_MAX - (uint64_t)column) {
                return PHY_ERR_OVERFLOW;
            }
            numerators[cell] =
                dimension >= row
                    ? (uint64_t)(dimension - row) + (uint64_t)column
                    : (uint64_t)(column - (row - dimension));
            uint64_t hook =
                (uint64_t)tableau->row_lengths[row] - (uint64_t)column;
            for (size_t below = row + 1u;
                 below < tableau->row_count; ++below) {
                if (column < tableau->row_lengths[below]) {
                    ++hook;
                }
            }
            denominators[cell] = hook;
            ++cell;
        }
    }

    for (size_t denominator = 0u; denominator < cell; ++denominator) {
        for (size_t numerator = 0u;
             numerator < cell && denominators[denominator] != 1u;
             ++numerator) {
            const uint64_t divisor = young_gcd(
                numerators[numerator], denominators[denominator]);
            numerators[numerator] /= divisor;
            denominators[denominator] /= divisor;
        }
        if (denominators[denominator] != 1u) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
    }
    uint64_t result = 1u;
    for (size_t numerator = 0u; numerator < cell; ++numerator) {
        if (numerators[numerator] != 0u &&
            result > UINT64_MAX / numerators[numerator]) {
            return PHY_ERR_OVERFLOW;
        }
        result *= numerators[numerator];
    }
    *out_dimension = result;
    return PHY_OK;
}

/* ------------------------------------------------------- shape and groups */

void phy_young_shape_release(phy_young_shape *shape)
{
    if (shape == NULL || shape->storage == NULL) {
        return;
    }
    phy_free(shape->storage, shape->storage_bytes);
    memset(shape, 0, sizeof *shape);
}

phy_status phy_young_shape_build(const phy_young_tableau *tableau,
                                 uint64_t ceiling, size_t max_bytes,
                                 phy_young_shape *out_shape)
{
    if (out_shape == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(out_shape, 0, sizeof *out_shape);
    phy_young_tableau_info info;
    phy_status status = phy_young_tableau_validate(tableau, &info);
    if (status != PHY_OK) {
        return status;
    }
    if (info.row_group_order > ceiling ||
        info.column_group_order > ceiling ||
        info.row_group_order > ceiling / info.column_group_order) {
        return PHY_ERR_TERM_LIMIT;
    }

    size_t row_offsets_at = 0u;
    size_t column_slots_at = 0u;
    size_t column_lengths_at = 0u;
    size_t column_offsets_at = 0u;
    size_t bytes = 0u;
    if (!garnir_reserve(&bytes, info.row_count, sizeof(size_t),
                        &row_offsets_at) ||
        !garnir_reserve(&bytes, info.slot_count, sizeof(uint16_t),
                        &column_slots_at) ||
        !garnir_reserve(&bytes, info.column_count, sizeof(uint16_t),
                        &column_lengths_at) ||
        !garnir_reserve(&bytes, info.column_count, sizeof(size_t),
                        &column_offsets_at) ||
        bytes > max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    uint8_t *storage = phy_alloc(bytes);
    if (storage == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(storage, 0, bytes);

    out_shape->storage = storage;
    out_shape->storage_bytes = bytes;
    out_shape->slot_count = info.slot_count;
    out_shape->row_count = info.row_count;
    out_shape->column_count = info.column_count;
    out_shape->row_slots = tableau->slots;
    out_shape->row_lengths = tableau->row_lengths;
    out_shape->row_offsets = (size_t *)(storage + row_offsets_at);
    out_shape->column_slots = (uint16_t *)(storage + column_slots_at);
    out_shape->column_lengths = (uint16_t *)(storage + column_lengths_at);
    out_shape->column_offsets = (size_t *)(storage + column_offsets_at);
    out_shape->row_order = info.row_group_order;
    out_shape->column_order = info.column_group_order;
    out_shape->hook_product = info.hook_product;

    size_t offset = 0u;
    for (size_t row = 0u; row < info.row_count; ++row) {
        out_shape->row_offsets[row] = offset;
        offset += tableau->row_lengths[row];
    }
    size_t at = 0u;
    for (size_t column = 0u; column < info.column_count; ++column) {
        out_shape->column_offsets[column] = at;
        for (size_t row = 0u; row < info.row_count; ++row) {
            if (column < tableau->row_lengths[row]) {
                out_shape->column_slots[at++] =
                    tableau->slots[out_shape->row_offsets[row] + column];
                ++out_shape->column_lengths[column];
            }
        }
    }
    return PHY_OK;
}

typedef struct {
    size_t degree;
    const uint16_t *block_slots;
    const uint16_t *block_lengths;
    const size_t *block_offsets;
    size_t block_count;
    bool alternating;
    uint16_t *state;
    uint16_t *work;
    uint16_t *images;
    int8_t *signs;
    size_t capacity;
    size_t produced;
} phy_block_enumerator;

static phy_status emit_group_element(phy_block_enumerator *enumerator,
                                     int sign)
{
    if (enumerator->produced >= enumerator->capacity) {
        return PHY_ERR_TERM_LIMIT;
    }
    memcpy(&enumerator->images[enumerator->produced * enumerator->degree],
           enumerator->state,
           enumerator->degree * sizeof(*enumerator->state));
    enumerator->signs[enumerator->produced] =
        (int8_t)(enumerator->alternating ? sign : 1);
    ++enumerator->produced;
    return PHY_OK;
}

static phy_status enumerate_blocks(phy_block_enumerator *enumerator,
                                   size_t block, int sign);

static phy_status enumerate_one_block(phy_block_enumerator *enumerator,
                                      size_t block, size_t position,
                                      int sign)
{
    const size_t offset = enumerator->block_offsets[block];
    const size_t length = enumerator->block_lengths[block];
    if (position == length) {
        const uint16_t *slots = &enumerator->block_slots[offset];
        for (size_t i = 0u; i < length; ++i) {
            enumerator->state[slots[i]] = enumerator->work[offset + i];
        }
        return enumerate_blocks(enumerator, block + 1u, sign);
    }
    for (size_t choice = position; choice < length; ++choice) {
        const size_t left = offset + position;
        const size_t right = offset + choice;
        const uint16_t held = enumerator->work[left];
        enumerator->work[left] = enumerator->work[right];
        enumerator->work[right] = held;
        const int next_sign = choice == position ? sign : -sign;
        const phy_status status =
            enumerate_one_block(enumerator, block, position + 1u, next_sign);
        enumerator->work[right] = enumerator->work[left];
        enumerator->work[left] = held;
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static phy_status enumerate_blocks(phy_block_enumerator *enumerator,
                                   size_t block, int sign)
{
    if (block == enumerator->block_count) {
        return emit_group_element(enumerator, sign);
    }
    const size_t offset = enumerator->block_offsets[block];
    const size_t length = enumerator->block_lengths[block];
    memcpy(&enumerator->work[offset], &enumerator->block_slots[offset],
           length * sizeof(*enumerator->work));
    return enumerate_one_block(enumerator, block, 0u, sign);
}

phy_status phy_young_block_group(size_t degree, const uint16_t *block_slots,
                                 const uint16_t *block_lengths,
                                 const size_t *block_offsets,
                                 size_t block_count, bool alternating,
                                 size_t capacity, uint16_t *images,
                                 int8_t *signs, uint16_t *state,
                                 uint16_t *work)
{
    for (size_t i = 0u; i < degree; ++i) {
        state[i] = (uint16_t)i;
    }
    phy_block_enumerator enumerator;
    memset(&enumerator, 0, sizeof enumerator);
    enumerator.degree = degree;
    enumerator.block_slots = block_slots;
    enumerator.block_lengths = block_lengths;
    enumerator.block_offsets = block_offsets;
    enumerator.block_count = block_count;
    enumerator.alternating = alternating;
    enumerator.state = state;
    enumerator.work = work;
    enumerator.images = images;
    enumerator.signs = signs;
    enumerator.capacity = capacity;
    const phy_status status = enumerate_blocks(&enumerator, 0u, 1);
    if (status != PHY_OK) {
        return status;
    }
    return enumerator.produced == capacity ? PHY_OK
                                           : PHY_ERR_CORRUPT_DOCUMENT;
}

/* --------------------------------------- the Young symmetrizer as an algebra */

/*
 * The Young symmetrizer expanded over the group.  R_T and C_T intersect only
 * in the identity, so the |R_T| * |C_T| products are distinct and the
 * expansion has exactly that many terms, each with coefficient +/-1.
 *
 * `keys` holds the Lehmer ranks of `images` sorted ascending, with `coeffs`
 * carried alongside, so membership is a binary search rather than a scan.
 */
typedef struct {
    size_t degree;
    size_t count;
    uint16_t *images; /* count * degree, generation order */
    int8_t *signs;    /* count, generation order */
    uint64_t *keys;   /* count, ascending */
    int8_t *coeffs;   /* count, aligned with keys */
    void *storage;
    size_t storage_bytes;
} phy_young_algebra;

static void young_algebra_release(phy_young_algebra *algebra)
{
    if (algebra == NULL || algebra->storage == NULL) {
        return;
    }
    phy_free(algebra->storage, algebra->storage_bytes);
    memset(algebra, 0, sizeof *algebra);
}

static uint64_t permutation_rank(const uint16_t *image, size_t degree)
{
    uint64_t rank = 0u;
    for (size_t i = 0u; i < degree; ++i) {
        uint64_t smaller = 0u;
        for (size_t j = i + 1u; j < degree; ++j) {
            if (image[j] < image[i]) {
                ++smaller;
            }
        }
        uint64_t weight = 1u;
        for (size_t factor = 2u; factor + i < degree; ++factor) {
            weight *= (uint64_t)factor;
        }
        rank += smaller * weight;
    }
    return rank;
}

static void sort_algebra_keys(phy_young_algebra *algebra)
{
    for (size_t i = 1u; i < algebra->count; ++i) {
        const uint64_t key = algebra->keys[i];
        const int8_t coefficient = algebra->coeffs[i];
        size_t position = i;
        while (position != 0u && algebra->keys[position - 1u] > key) {
            algebra->keys[position] = algebra->keys[position - 1u];
            algebra->coeffs[position] = algebra->coeffs[position - 1u];
            --position;
        }
        algebra->keys[position] = key;
        algebra->coeffs[position] = coefficient;
    }
}

static bool algebra_coefficient(const phy_young_algebra *algebra,
                                uint64_t key, int *out_coefficient)
{
    size_t low = 0u;
    size_t high = algebra->count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        if (algebra->keys[middle] == key) {
            *out_coefficient = algebra->coeffs[middle];
            return true;
        }
        if (algebra->keys[middle] < key) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return false;
}

static phy_status young_algebra_build(const phy_young_tableau *tableau,
                                      const phy_young_limits *limits,
                                      phy_young_algebra *out_algebra)
{
    memset(out_algebra, 0, sizeof *out_algebra);
    phy_young_limits resolved;
    phy_status status = phy_young_resolve_limits(limits, &resolved);
    if (status != PHY_OK) {
        return status;
    }
    phy_young_shape shape;
    status = phy_young_shape_build(
        tableau, (uint64_t)resolved.max_generated_terms,
        resolved.max_bytes, &shape);
    if (status != PHY_OK) {
        return status;
    }
    const size_t degree = shape.slot_count;
    if (degree > PHY_YOUNG_MAX_EXACT_DEGREE) {
        phy_young_shape_release(&shape);
        return PHY_ERR_TERM_LIMIT;
    }
    const size_t rows = (size_t)shape.row_order;
    const size_t columns = (size_t)shape.column_order;
    const size_t count = rows * columns;

    size_t images_at = 0u;
    size_t signs_at = 0u;
    size_t keys_at = 0u;
    size_t coeffs_at = 0u;
    size_t row_images_at = 0u;
    size_t row_signs_at = 0u;
    size_t column_images_at = 0u;
    size_t column_signs_at = 0u;
    size_t state_at = 0u;
    size_t work_at = 0u;
    size_t bytes = 0u;
    if (!garnir_reserve_matrix(
            &bytes, count, degree, sizeof(uint16_t), &images_at) ||
        !garnir_reserve(&bytes, count, sizeof(int8_t), &signs_at) ||
        !garnir_reserve(&bytes, count, sizeof(uint64_t), &keys_at) ||
        !garnir_reserve(&bytes, count, sizeof(int8_t), &coeffs_at) ||
        !garnir_reserve_matrix(
            &bytes, rows, degree, sizeof(uint16_t), &row_images_at) ||
        !garnir_reserve(&bytes, rows, sizeof(int8_t), &row_signs_at) ||
        !garnir_reserve_matrix(
            &bytes, columns, degree, sizeof(uint16_t),
            &column_images_at) ||
        !garnir_reserve(&bytes, columns, sizeof(int8_t),
                        &column_signs_at) ||
        !garnir_reserve(&bytes, degree, sizeof(uint16_t), &state_at) ||
        !garnir_reserve(&bytes, degree, sizeof(uint16_t), &work_at) ||
        shape.storage_bytes > resolved.max_bytes ||
        bytes > resolved.max_bytes - shape.storage_bytes) {
        phy_young_shape_release(&shape);
        return PHY_ERR_MEMORY_LIMIT;
    }
    uint8_t *storage = phy_alloc(bytes);
    if (storage == NULL) {
        phy_young_shape_release(&shape);
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(storage, 0, bytes);
    uint16_t *row_images = (uint16_t *)(storage + row_images_at);
    int8_t *row_signs = (int8_t *)(storage + row_signs_at);
    uint16_t *column_images = (uint16_t *)(storage + column_images_at);
    int8_t *column_signs = (int8_t *)(storage + column_signs_at);
    uint16_t *state = (uint16_t *)(storage + state_at);
    uint16_t *work = (uint16_t *)(storage + work_at);

    out_algebra->degree = degree;
    out_algebra->count = count;
    out_algebra->images = (uint16_t *)(storage + images_at);
    out_algebra->signs = (int8_t *)(storage + signs_at);
    out_algebra->keys = (uint64_t *)(storage + keys_at);
    out_algebra->coeffs = (int8_t *)(storage + coeffs_at);
    out_algebra->storage = storage;
    out_algebra->storage_bytes = bytes;

    status = phy_young_block_group(
        degree, shape.row_slots, shape.row_lengths, shape.row_offsets,
        shape.row_count, false, rows, row_images, row_signs, state, work);
    if (status == PHY_OK) {
        status = phy_young_block_group(
            degree, shape.column_slots, shape.column_lengths,
            shape.column_offsets, shape.column_count, true, columns,
            column_images, column_signs, state, work);
    }
    for (size_t row = 0u; row < rows && status == PHY_OK; ++row) {
        for (size_t column = 0u; column < columns && status == PHY_OK;
             ++column) {
            const size_t at = row * columns + column;
            const uint16_t *outer =
                tableau->order == PHY_YOUNG_ROW_SYMMETRY_LAST
                    ? &row_images[row * degree]
                    : &column_images[column * degree];
            const uint16_t *inner =
                tableau->order == PHY_YOUNG_ROW_SYMMETRY_LAST
                    ? &column_images[column * degree]
                    : &row_images[row * degree];
            status = phy_permutation_compose(
                outer, inner, degree,
                &out_algebra->images[at * degree]);
            out_algebra->signs[at] =
                (int8_t)(row_signs[row] * column_signs[column]);
            out_algebra->keys[at] = permutation_rank(
                &out_algebra->images[at * degree], degree);
            out_algebra->coeffs[at] = out_algebra->signs[at];
        }
    }
    phy_young_shape_release(&shape);
    if (status != PHY_OK) {
        young_algebra_release(out_algebra);
        return status;
    }
    sort_algebra_keys(out_algebra);
    /*
     * R_T and C_T meet only in the identity, so the products are distinct.
     * A repeated key would mean the shape decomposition disagreed with that,
     * and every claim below rests on the expansion being a set.
     */
    for (size_t i = 1u; i < out_algebra->count; ++i) {
        if (out_algebra->keys[i] == out_algebra->keys[i - 1u]) {
            young_algebra_release(out_algebra);
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
    }
    return PHY_OK;
}

static phy_status algebra_manifest(const phy_young_algebra *algebra,
                                   const uint16_t *image, int sign,
                                   bool *out_manifest)
{
    *out_manifest = false;
    const size_t degree = algebra->degree;
    uint16_t *composed = phy_alloc(degree * sizeof(uint16_t));
    if (composed == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    bool manifest = true;
    phy_status status = PHY_OK;
    for (size_t i = 0u; i < algebra->count && manifest; ++i) {
        status = phy_permutation_compose(
            &algebra->images[i * degree], image, degree, composed);
        if (status != PHY_OK) {
            break;
        }
        int coefficient = 0;
        if (!algebra_coefficient(
                algebra, permutation_rank(composed, degree),
                &coefficient) ||
            (int)algebra->signs[i] != sign * coefficient) {
            manifest = false;
        }
    }
    phy_free(composed, degree * sizeof(uint16_t));
    if (status == PHY_OK) {
        *out_manifest = manifest;
    }
    return status;
}

phy_status phy_young_generator_is_manifest(
    const phy_young_tableau *tableau, const uint16_t *image, int sign,
    const phy_young_limits *limits, bool *out_manifest)
{
    if (image == NULL || out_manifest == NULL ||
        (sign != 1 && sign != -1)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_manifest = false;
    phy_young_algebra algebra;
    phy_status status = young_algebra_build(tableau, limits, &algebra);
    if (status != PHY_OK) {
        return status;
    }
    if (!phy_permutation_valid(image, algebra.degree)) {
        young_algebra_release(&algebra);
        return PHY_ERR_TYPE;
    }
    status = algebra_manifest(&algebra, image, sign, out_manifest);
    young_algebra_release(&algebra);
    return status;
}

/* ---------------------------------------------------- declared symmetry */

bool phy_tensor_head_has_young_symmetry(
    const phy_abstract_tensor_head *head)
{
    return head != NULL && head->has_young;
}

phy_status phy_tensor_head_young_symmetry(
    const phy_abstract_tensor_head *head,
    phy_young_tableau *out_tableau, phy_young_tableau_info *out_info)
{
    if (head == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!head->has_young) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    if (out_tableau != NULL) {
        *out_tableau = head->young;
    }
    if (out_info != NULL) {
        *out_info = head->young_info;
    }
    return PHY_OK;
}

phy_status phy_tensor_head_set_young_symmetry(
    phy_abstract_tensor_head *head, const phy_young_tableau *tableau,
    const phy_young_limits *limits)
{
    if (head == NULL || tableau == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (head->has_young) {
        return PHY_ERR_ALREADY_INITIALIZED;
    }
    phy_young_tableau_info info;
    phy_status status =
        phy_young_tableau_check_head(head, tableau, &info);
    if (status != PHY_OK) {
        return status;
    }
    phy_young_algebra algebra;
    status = young_algebra_build(tableau, limits, &algebra);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t which = 0u;
         which < head->generator_count && status == PHY_OK; ++which) {
        bool manifest = false;
        status = algebra_manifest(
            &algebra, head->generators[which].image,
            (int)head->generators[which].sign, &manifest);
        if (status == PHY_OK && !manifest) {
            status = PHY_ERR_TYPE;
        }
    }
    young_algebra_release(&algebra);
    if (status != PHY_OK) {
        return status;
    }

    const size_t slots_bytes = tableau->slot_count * sizeof(uint16_t);
    const size_t rows_bytes = tableau->row_count * sizeof(uint16_t);
    uint16_t *slots = phy_abstract_alloc(head->context, slots_bytes);
    uint16_t *rows = phy_abstract_alloc(head->context, rows_bytes);
    if (slots == NULL || rows == NULL) {
        phy_abstract_free(head->context, slots, slots_bytes);
        phy_abstract_free(head->context, rows, rows_bytes);
        return PHY_ERR_MEMORY_LIMIT;
    }
    memcpy(slots, tableau->slots, slots_bytes);
    memcpy(rows, tableau->row_lengths, rows_bytes);
    head->young_slots = slots;
    head->young_slots_bytes = slots_bytes;
    head->young_row_lengths = rows;
    head->young_row_bytes = rows_bytes;
    head->young.slots = slots;
    head->young.slot_count = tableau->slot_count;
    head->young.row_lengths = rows;
    head->young.row_count = tableau->row_count;
    head->young.order = tableau->order;
    head->young_info = info;
    head->has_young = true;
    return PHY_OK;
}

/* ------------------------------------------------------- Garnir relations */

size_t phy_young_garnir_count(const phy_young_tableau *tableau)
{
    size_t column_count = 0u;
    if (validate_shape(tableau, &column_count) != PHY_OK) {
        return 0u;
    }
    size_t total = 0u;
    for (size_t column = 0u; column + 1u < column_count; ++column) {
        total += column_length_of(tableau, column + 1u);
    }
    return total;
}

static bool garnir_locate(const phy_young_tableau *tableau, size_t which,
                          size_t column_count, size_t *out_column,
                          size_t *out_split)
{
    size_t seen = 0u;
    for (size_t column = 0u; column + 1u < column_count; ++column) {
        const size_t next = column_length_of(tableau, column + 1u);
        if (which - seen < next) {
            *out_column = column;
            *out_split = which - seen;
            return true;
        }
        seen += next;
    }
    return false;
}

phy_status phy_young_garnir_slots(const phy_young_tableau *tableau,
                                  size_t which, uint16_t *out_slots,
                                  size_t capacity, size_t *out_count)
{
    if (out_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    size_t column_count = 0u;
    phy_status status = validate_shape(tableau, &column_count);
    if (status != PHY_OK) {
        return status;
    }
    size_t column = 0u;
    size_t split = 0u;
    if (!garnir_locate(tableau, which, column_count, &column, &split)) {
        return PHY_ERR_DOMAIN;
    }
    const size_t total = column_length_of(tableau, column) + 1u;
    if (out_slots != NULL && total > capacity) {
        return PHY_ERR_TERM_LIMIT;
    }
    size_t at = 0u;
    for (size_t row = 0u; row < tableau->row_count; ++row) {
        if (column >= tableau->row_lengths[row]) {
            continue;
        }
        const size_t offset = cell_offset(tableau, row);
        /* column j from the split row down, then column j+1 above it. */
        if (row >= split) {
            if (out_slots != NULL) {
                out_slots[at] = tableau->slots[offset + column];
            }
            ++at;
        }
        if (row <= split && column + 1u < tableau->row_lengths[row]) {
            if (out_slots != NULL) {
                out_slots[at] = tableau->slots[offset + column + 1u];
            }
            ++at;
        }
    }
    if (at != total) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    *out_count = total;
    return PHY_OK;
}

/* --------------------------------------------------------- term emission */

/*
 * One projector or antisymmetrizer acting on one factor. `outer` is applied
 * after `inner`, which distinguishes the two symmetrizer orders. Tensor-head
 * slot declarations act on the right, so the rightmost subgroup is the one
 * whose signed relations are manifest to monoterm canonicalization.
 */
typedef struct {
    size_t index_offset;
    size_t slot_count;
    size_t outer_order;
    size_t inner_order;
    const uint16_t *outer_images;
    const int8_t *outer_signs;
    const uint16_t *inner_images;
    const int8_t *inner_signs;
    uint64_t denominator;
    void *storage;
    size_t storage_bytes;
} phy_young_entry;

typedef struct {
    phy_young_entry *entries;
    size_t entry_count;
    size_t entries_bytes;
    size_t bytes_used;
    size_t max_bytes;
    uint64_t images; /* product of outer_order * inner_order */
    uint64_t denominator;
} phy_young_plan;

static void plan_release(phy_young_plan *plan)
{
    if (plan == NULL) {
        return;
    }
    for (size_t i = 0u; i < plan->entry_count; ++i) {
        if (plan->entries[i].storage != NULL) {
            phy_free(plan->entries[i].storage,
                     plan->entries[i].storage_bytes);
        }
    }
    phy_free(plan->entries, plan->entries_bytes);
    memset(plan, 0, sizeof *plan);
}

static phy_status plan_reserve(phy_young_plan *plan, size_t capacity,
                               size_t max_bytes)
{
    memset(plan, 0, sizeof *plan);
    /*
     * An empty plan is the identity operator with denominator one, which is
     * what carries a term whose heads declare no Young symmetry through the
     * reducer unchanged.
     */
    plan->images = 1u;
    plan->denominator = 1u;
    plan->max_bytes = max_bytes;
    if (capacity == 0u) {
        return PHY_OK;
    }
    if (capacity > SIZE_MAX / sizeof(phy_young_entry)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    plan->entries_bytes = capacity * sizeof(phy_young_entry);
    if (plan->entries_bytes > max_bytes) {
        plan->entries_bytes = 0u;
        return PHY_ERR_MEMORY_LIMIT;
    }
    plan->entries = phy_alloc(plan->entries_bytes);
    if (plan->entries == NULL) {
        plan->entries_bytes = 0u;
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(plan->entries, 0, plan->entries_bytes);
    plan->bytes_used = plan->entries_bytes;
    plan->images = 1u;
    plan->denominator = 1u;
    return PHY_OK;
}

/*
 * Append the Young projector of `tableau` acting on the factor that starts at
 * `index_offset`.  `budget` bounds the enumerated group order and `max_bytes`
 * the entry's own storage.
 */
static phy_status plan_append_projector(phy_young_plan *plan,
                                        size_t index_offset,
                                        const phy_young_tableau *tableau,
                                        uint64_t budget, size_t max_bytes)
{
    if (max_bytes != plan->max_bytes ||
        plan->bytes_used > plan->max_bytes) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    const size_t available = plan->max_bytes - plan->bytes_used;
    phy_young_shape shape;
    phy_status status =
        phy_young_shape_build(tableau, budget, available, &shape);
    if (status != PHY_OK) {
        return status;
    }
    const size_t degree = shape.slot_count;
    const size_t rows = (size_t)shape.row_order;
    const size_t columns = (size_t)shape.column_order;

    size_t row_images_at = 0u;
    size_t row_signs_at = 0u;
    size_t column_images_at = 0u;
    size_t column_signs_at = 0u;
    size_t state_at = 0u;
    size_t work_at = 0u;
    size_t bytes = 0u;
    if (!garnir_reserve_matrix(
            &bytes, rows, degree, sizeof(uint16_t), &row_images_at) ||
        !garnir_reserve(&bytes, rows, sizeof(int8_t), &row_signs_at) ||
        !garnir_reserve_matrix(
            &bytes, columns, degree, sizeof(uint16_t),
            &column_images_at) ||
        !garnir_reserve(&bytes, columns, sizeof(int8_t),
                        &column_signs_at) ||
        !garnir_reserve(&bytes, degree, sizeof(uint16_t), &state_at) ||
        !garnir_reserve(&bytes, degree, sizeof(uint16_t), &work_at) ||
        shape.storage_bytes > available ||
        bytes > available - shape.storage_bytes) {
        phy_young_shape_release(&shape);
        return PHY_ERR_MEMORY_LIMIT;
    }
    uint8_t *storage = phy_alloc(bytes);
    if (storage == NULL) {
        phy_young_shape_release(&shape);
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(storage, 0, bytes);
    uint16_t *row_images = (uint16_t *)(storage + row_images_at);
    int8_t *row_signs = (int8_t *)(storage + row_signs_at);
    uint16_t *column_images = (uint16_t *)(storage + column_images_at);
    int8_t *column_signs = (int8_t *)(storage + column_signs_at);
    uint16_t *state = (uint16_t *)(storage + state_at);
    uint16_t *work = (uint16_t *)(storage + work_at);

    status = phy_young_block_group(
        degree, shape.row_slots, shape.row_lengths, shape.row_offsets,
        shape.row_count, false, rows, row_images, row_signs, state, work);
    if (status == PHY_OK) {
        status = phy_young_block_group(
            degree, shape.column_slots, shape.column_lengths,
            shape.column_offsets, shape.column_count, true, columns,
            column_images, column_signs, state, work);
    }
    const uint64_t hook = shape.hook_product;
    phy_young_shape_release(&shape);
    if (status != PHY_OK) {
        phy_free(storage, bytes);
        return status;
    }

    phy_young_entry *entry = &plan->entries[plan->entry_count];
    entry->index_offset = index_offset;
    entry->slot_count = degree;
    entry->storage = storage;
    entry->storage_bytes = bytes;
    if (tableau->order == PHY_YOUNG_ROW_SYMMETRY_LAST) {
        entry->outer_order = rows;
        entry->outer_images = row_images;
        entry->outer_signs = row_signs;
        entry->inner_order = columns;
        entry->inner_images = column_images;
        entry->inner_signs = column_signs;
    } else {
        entry->outer_order = columns;
        entry->outer_images = column_images;
        entry->outer_signs = column_signs;
        entry->inner_order = rows;
        entry->inner_images = row_images;
        entry->inner_signs = row_signs;
    }
    entry->denominator = hook;
    const uint64_t product = (uint64_t)rows * (uint64_t)columns;
    if (plan->images > budget / product ||
        plan->denominator > (uint64_t)INT64_MAX / hook) {
        phy_free(storage, bytes);
        memset(entry, 0, sizeof *entry);
        return PHY_ERR_TERM_LIMIT;
    }
    plan->images *= product;
    plan->denominator *= hook;
    plan->bytes_used += bytes;
    ++plan->entry_count;
    return PHY_OK;
}

/*
 * Append the normalized antisymmetrizer over one slot set.  This is the
 * degenerate plan entry with a trivial inner group; it is what turns a Garnir
 * slot set into a relation.
 */
static phy_status plan_append_antisymmetrizer(phy_young_plan *plan,
                                              size_t index_offset,
                                              size_t degree,
                                              const uint16_t *slots,
                                              size_t slot_count,
                                              uint64_t budget,
                                              size_t max_bytes)
{
    if (max_bytes != plan->max_bytes) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    uint64_t order = 0u;
    phy_status status = factorial_exact(slot_count, &order);
    if (status != PHY_OK) {
        return status;
    }
    if (order > budget) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (order > (uint64_t)INT64_MAX) {
        return PHY_ERR_OVERFLOW;
    }
    const size_t count = (size_t)order;
    const uint16_t block_length = (uint16_t)slot_count;
    const size_t block_offset = 0u;

    size_t images_at = 0u;
    size_t signs_at = 0u;
    size_t identity_at = 0u;
    size_t identity_sign_at = 0u;
    size_t state_at = 0u;
    size_t work_at = 0u;
    size_t bytes = 0u;
    if (!garnir_reserve_matrix(
            &bytes, count, degree, sizeof(uint16_t), &images_at) ||
        !garnir_reserve(&bytes, count, sizeof(int8_t), &signs_at) ||
        !garnir_reserve(&bytes, degree, sizeof(uint16_t), &identity_at) ||
        !garnir_reserve(&bytes, 1u, sizeof(int8_t), &identity_sign_at) ||
        !garnir_reserve(&bytes, degree, sizeof(uint16_t), &state_at) ||
        !garnir_reserve(&bytes, slot_count, sizeof(uint16_t), &work_at) ||
        plan->bytes_used > plan->max_bytes ||
        bytes > plan->max_bytes - plan->bytes_used ||
        bytes > max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    uint8_t *storage = phy_alloc(bytes);
    if (storage == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(storage, 0, bytes);
    uint16_t *images = (uint16_t *)(storage + images_at);
    int8_t *signs = (int8_t *)(storage + signs_at);
    uint16_t *identity = (uint16_t *)(storage + identity_at);
    int8_t *identity_sign = (int8_t *)(storage + identity_sign_at);
    uint16_t *state = (uint16_t *)(storage + state_at);
    uint16_t *work = (uint16_t *)(storage + work_at);

    status = phy_permutation_identity(identity, degree);
    identity_sign[0] = 1;
    if (status == PHY_OK) {
        status = phy_young_block_group(
            degree, slots, &block_length, &block_offset, 1u, true, count,
            images, signs, state, work);
    }
    if (status != PHY_OK) {
        phy_free(storage, bytes);
        return status;
    }

    phy_young_entry *entry = &plan->entries[plan->entry_count];
    entry->index_offset = index_offset;
    entry->slot_count = degree;
    entry->storage = storage;
    entry->storage_bytes = bytes;
    entry->outer_order = count;
    entry->outer_images = images;
    entry->outer_signs = signs;
    entry->inner_order = 1u;
    entry->inner_images = identity;
    entry->inner_signs = identity_sign;
    entry->denominator = order;
    plan->images *= order;
    plan->denominator *= order;
    plan->bytes_used += bytes;
    ++plan->entry_count;
    return PHY_OK;
}

typedef struct {
    const phy_tensor_monomial *monomial;
    phy_tensor_expression *expression;
    phy_abstract_factor *factors;
    phy_abstract_index *indices;
    uint16_t *image;
    uint16_t *composed;
    const phy_tensor_canonical_limits *canonical;
    void *storage;
    size_t storage_bytes;
    uint64_t generated;
    uint32_t steps;
    uint32_t max_steps;
} phy_young_sink;

static void sink_release(phy_young_sink *sink)
{
    if (sink == NULL || sink->storage == NULL) {
        return;
    }
    phy_free(sink->storage, sink->storage_bytes);
    sink->storage = NULL;
    sink->storage_bytes = 0u;
}

static phy_status sink_open(phy_young_sink *sink,
                            const phy_tensor_monomial *monomial,
                            phy_tensor_expression *expression,
                            const phy_tensor_canonical_limits *canonical,
                            uint32_t max_steps, size_t max_bytes)
{
    memset(sink, 0, sizeof *sink);
    size_t factors_at = 0u;
    size_t indices_at = 0u;
    size_t image_at = 0u;
    size_t composed_at = 0u;
    size_t bytes = 0u;
    if (!garnir_reserve(&bytes, monomial->factor_count,
                        sizeof(phy_abstract_factor), &factors_at) ||
        !garnir_reserve(&bytes, monomial->index_count,
                        sizeof(phy_abstract_index), &indices_at) ||
        !garnir_reserve(&bytes, monomial->index_count, sizeof(uint16_t),
                        &image_at) ||
        !garnir_reserve(&bytes, monomial->index_count, sizeof(uint16_t),
                        &composed_at) ||
        bytes > max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    uint8_t *storage = phy_alloc(bytes == 0u ? 1u : bytes);
    if (storage == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    memset(storage, 0, bytes == 0u ? 1u : bytes);
    sink->monomial = monomial;
    sink->expression = expression;
    sink->canonical = canonical;
    sink->max_steps = max_steps;
    sink->storage = storage;
    sink->storage_bytes = bytes == 0u ? 1u : bytes;
    sink->factors = (phy_abstract_factor *)(storage + factors_at);
    sink->indices = (phy_abstract_index *)(storage + indices_at);
    sink->image = (uint16_t *)(storage + image_at);
    sink->composed = (uint16_t *)(storage + composed_at);
    return PHY_OK;
}

/*
 * Emit coefficient * sign / denominator times the monomial with `image`
 * applied to its whole index list, canonicalized and collected.
 */
static phy_status sink_emit(phy_young_sink *sink, const uint16_t *image,
                            int sign, uint64_t denominator)
{
    if (sink->steps >= sink->max_steps) {
        return PHY_ERR_TIMEOUT;
    }
    ++sink->steps;
    ++sink->generated;
    const phy_tensor_monomial *monomial = sink->monomial;
    for (size_t slot = 0u; slot < monomial->index_count; ++slot) {
        sink->indices[image[slot]] = monomial->indices[slot];
    }
    for (size_t factor = 0u; factor < monomial->factor_count; ++factor) {
        const phy_abstract_factor_record *source =
            &monomial->factors[factor];
        sink->factors[factor].head = source->head;
        sink->factors[factor].indices =
            source->index_count != 0u
                ? &sink->indices[source->index_offset]
                : NULL;
        sink->factors[factor].index_count = source->index_count;
    }
    phy_ir_ref scale = PHY_IR_NULL;
    phy_status status = phy_cas_number(
        monomial->context->cas, sign, (int64_t)denominator, &scale);
    phy_ir_ref coefficient = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref product[2] = {monomial->coefficient, scale};
        status =
            phy_cas_mul(monomial->context->cas, product, 2u, &coefficient);
    }
    phy_tensor_monomial *raw = NULL;
    if (status == PHY_OK) {
        status = phy_tensor_monomial_create(
            monomial->context, coefficient,
            monomial->factor_count == 0u ? NULL : sink->factors,
            monomial->factor_count, &raw);
    }
    phy_tensor_monomial *canonical = NULL;
    if (status == PHY_OK) {
        status = phy_tensor_monomial_canonicalize(
            raw, sink->canonical, &canonical, NULL);
    }
    phy_tensor_monomial_destroy(raw);
    if (status != PHY_OK) {
        return status;
    }
    return phy_expression_collect_term(sink->expression, canonical);
}

/*
 * Walk the direct product of the plan's per-factor groups.  Applying every
 * declared factor of one term in a single sweep is what keeps the reduction
 * well defined: canonicalization may reorder two identical commuting factors,
 * so projecting them one after another could project the same factor twice.
 */
static phy_status plan_walk(const phy_young_plan *plan,
                            phy_young_sink *sink, size_t entry, int sign)
{
    if (entry == plan->entry_count) {
        return sink_emit(sink, sink->image, sign, plan->denominator);
    }
    const phy_young_entry *current = &plan->entries[entry];
    const size_t degree = current->slot_count;
    const size_t offset = current->index_offset;
    phy_status status = PHY_OK;
    for (size_t outer = 0u;
         outer < current->outer_order && status == PHY_OK; ++outer) {
        for (size_t inner = 0u;
             inner < current->inner_order && status == PHY_OK; ++inner) {
            status = phy_permutation_compose(
                &current->outer_images[outer * degree],
                &current->inner_images[inner * degree], degree,
                sink->composed);
            if (status != PHY_OK) {
                break;
            }
            for (size_t slot = 0u; slot < degree; ++slot) {
                sink->image[offset + slot] =
                    (uint16_t)(offset + sink->composed[slot]);
            }
            status = plan_walk(
                plan, sink, entry + 1u,
                sign * current->outer_signs[outer] *
                    current->inner_signs[inner]);
        }
    }
    return status;
}

static phy_status plan_apply(const phy_young_plan *plan,
                             const phy_tensor_monomial *monomial,
                             phy_tensor_expression *expression,
                             const phy_tensor_canonical_limits *canonical,
                             uint32_t max_steps, size_t max_bytes,
                             uint64_t *out_generated, uint32_t *out_steps)
{
    phy_young_sink sink;
    phy_status status = sink_open(
        &sink, monomial, expression, canonical, max_steps, max_bytes);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_permutation_identity(sink.image, monomial->index_count);
    if (status == PHY_OK) {
        status = plan_walk(plan, &sink, 0u, 1);
    }
    if (out_generated != NULL) {
        *out_generated += sink.generated;
    }
    if (out_steps != NULL) {
        *out_steps += sink.steps;
    }
    sink_release(&sink);
    return status;
}

/* ------------------------------------------------------ public projection */

phy_status phy_tensor_monomial_young_project(
    const phy_tensor_monomial *monomial, size_t target_factor,
    const phy_young_tableau *tableau, const phy_young_limits *requested,
    phy_tensor_expression **out_expression, phy_young_stats *out_stats)
{
    if (monomial == NULL || out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_expression = NULL;
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof *out_stats);
    }
    phy_young_limits limits;
    phy_status status = phy_young_resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    if (target_factor >= monomial->factor_count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_young_tableau_info info;
    status = phy_young_tableau_check_head(
        monomial->factors[target_factor].head, tableau, &info);
    if (status != PHY_OK) {
        return status;
    }

    phy_young_plan plan;
    status = plan_reserve(&plan, 1u, limits.max_bytes);
    if (status == PHY_OK) {
        status = plan_append_projector(
            &plan, monomial->factors[target_factor].index_offset, tableau,
            (uint64_t)limits.max_generated_terms, limits.max_bytes);
    }
    phy_tensor_expression *expression = NULL;
    if (status == PHY_OK) {
        status = phy_expression_create_like(
            monomial->context, limits.max_result_terms, monomial,
            &expression);
    }
    uint64_t generated = 0u;
    if (status == PHY_OK) {
        status = plan_apply(
            &plan, monomial, expression, &limits.canonical, UINT32_MAX,
            limits.max_bytes - plan.bytes_used, &generated, NULL);
    }
    plan_release(&plan);
    if (status != PHY_OK) {
        phy_tensor_expression_destroy(expression);
        return status;
    }
    phy_expression_sort(expression);
    if (out_stats != NULL) {
        out_stats->row_group_order = info.row_group_order;
        out_stats->column_group_order = info.column_group_order;
        out_stats->hook_product = info.hook_product;
        out_stats->generated_terms = generated;
        out_stats->collected_terms =
            phy_tensor_expression_term_count(expression);
    }
    *out_expression = expression;
    return PHY_OK;
}

phy_status phy_tensor_expression_young_project(
    const phy_tensor_expression *expression, size_t factor,
    const phy_young_tableau *tableau, const phy_young_limits *requested,
    phy_tensor_expression **out_expression, phy_young_stats *out_stats)
{
    if (expression == NULL || tableau == NULL || out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_expression = NULL;
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof *out_stats);
    }
    phy_young_limits limits;
    phy_status status = phy_young_resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }

    phy_young_tableau_info info;
    if (expression->term_count == 0u) {
        status = phy_young_tableau_validate(tableau, &info);
    } else {
        const phy_tensor_monomial *first = expression->terms[0];
        if (factor >= first->factor_count) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
        const phy_abstract_tensor_head *head =
            first->factors[factor].head;
        status = phy_young_tableau_check_head(head, tableau, &info);
        for (size_t term = 1u;
             term < expression->term_count && status == PHY_OK; ++term) {
            const phy_tensor_monomial *current = expression->terms[term];
            if (factor >= current->factor_count ||
                current->factors[factor].head != head) {
                status = PHY_ERR_TYPE;
            }
        }
    }
    if (status != PHY_OK) {
        return status;
    }

    phy_tensor_expression *result = NULL;
    status = phy_expression_create_with_signature(
        expression->context, limits.max_result_terms,
        expression->free_uses, expression->free_count, &result);
    uint64_t generated = 0u;
    for (size_t term = 0u;
         term < expression->term_count && status == PHY_OK; ++term) {
        const phy_tensor_monomial *source = expression->terms[term];
        phy_young_plan plan;
        status = plan_reserve(&plan, 1u, limits.max_bytes);
        if (status == PHY_OK) {
            status = plan_append_projector(
                &plan, source->factors[factor].index_offset, tableau,
                (uint64_t)limits.max_generated_terms, limits.max_bytes);
        }
        if (status == PHY_OK &&
            (generated > (uint64_t)limits.max_generated_terms ||
             plan.images >
                 (uint64_t)limits.max_generated_terms - generated)) {
            status = PHY_ERR_TERM_LIMIT;
        }
        if (status == PHY_OK) {
            status = plan_apply(
                &plan, source, result, &limits.canonical, UINT32_MAX,
                limits.max_bytes - plan.bytes_used, &generated, NULL);
        }
        plan_release(&plan);
    }
    if (status != PHY_OK) {
        phy_tensor_expression_destroy(result);
        return status;
    }
    phy_expression_sort(result);
    if (out_stats != NULL) {
        out_stats->row_group_order = info.row_group_order;
        out_stats->column_group_order = info.column_group_order;
        out_stats->hook_product = info.hook_product;
        out_stats->generated_terms = generated;
        out_stats->collected_terms = result->term_count;
    }
    *out_expression = result;
    return PHY_OK;
}

phy_status phy_tensor_monomial_garnir_relation(
    const phy_tensor_monomial *monomial, size_t factor, size_t which,
    const phy_young_limits *requested,
    phy_tensor_expression **out_expression, phy_garnir_stats *out_stats)
{
    if (monomial == NULL || out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_expression = NULL;
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof *out_stats);
    }
    phy_young_limits limits;
    phy_status status = phy_young_resolve_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    if (factor >= monomial->factor_count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_abstract_tensor_head *head = monomial->factors[factor].head;
    if (!head->has_young) {
        return PHY_ERR_TYPE;
    }
    /*
     * The relation is stated over local slots of the factor, so it is lifted
     * to monomial slots before the antisymmetrizer is built.
     */
    size_t slot_count = 0u;
    status = phy_young_garnir_slots(
        &head->young, which, NULL, 0u, &slot_count);
    if (status != PHY_OK) {
        return status;
    }
    const size_t slots_bytes = slot_count * sizeof(uint16_t);
    uint16_t *slots = phy_alloc(slots_bytes);
    if (slots == NULL) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    status = phy_young_garnir_slots(
        &head->young, which, slots, slot_count, &slot_count);

    phy_young_plan plan;
    memset(&plan, 0, sizeof plan);
    if (status == PHY_OK) {
        status = plan_reserve(&plan, 1u, limits.max_bytes);
    }
    if (status == PHY_OK) {
        status = plan_append_antisymmetrizer(
            &plan, monomial->factors[factor].index_offset,
            head->slot_count, slots, slot_count,
            (uint64_t)limits.max_generated_terms, limits.max_bytes);
    }
    phy_free(slots, slots_bytes);
    phy_tensor_expression *expression = NULL;
    if (status == PHY_OK) {
        status = phy_expression_create_like(
            monomial->context, limits.max_result_terms, monomial,
            &expression);
    }
    uint64_t generated = 0u;
    if (status == PHY_OK) {
        status = plan_apply(
            &plan, monomial, expression, &limits.canonical, UINT32_MAX,
            limits.max_bytes - plan.bytes_used, &generated, NULL);
    }
    const uint64_t order = plan.denominator;
    plan_release(&plan);
    if (status != PHY_OK) {
        phy_tensor_expression_destroy(expression);
        return status;
    }
    phy_expression_sort(expression);
    if (out_stats != NULL) {
        out_stats->slot_count = slot_count;
        out_stats->group_order = order;
        out_stats->generated_terms = generated;
        out_stats->collected_terms =
            phy_tensor_expression_term_count(expression);
    }
    *out_expression = expression;
    return PHY_OK;
}

/* -------------------------------------------------------------- reduction */

#define PHY_YOUNG_REDUCE_DEFAULT_GENERATED 4096u
#define PHY_YOUNG_REDUCE_DEFAULT_RESULT 256u
#define PHY_YOUNG_REDUCE_DEFAULT_BYTES (512u * 1024u)
#define PHY_YOUNG_REDUCE_DEFAULT_STEPS 1000000u

void phy_young_reduce_limits_defaults(phy_young_reduce_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_generated_terms = PHY_YOUNG_REDUCE_DEFAULT_GENERATED;
    out_limits->max_result_terms = PHY_YOUNG_REDUCE_DEFAULT_RESULT;
    out_limits->max_bytes = PHY_YOUNG_REDUCE_DEFAULT_BYTES;
    out_limits->max_steps = PHY_YOUNG_REDUCE_DEFAULT_STEPS;
    phy_tensor_canonical_limits_defaults(&out_limits->canonical);
}

static phy_status resolve_reduce_limits(
    const phy_young_reduce_limits *requested, phy_young_reduce_limits *out)
{
    phy_young_reduce_limits_defaults(out);
    if (requested != NULL) {
        if (requested->max_generated_terms != 0u) {
            out->max_generated_terms = requested->max_generated_terms;
        }
        if (requested->max_result_terms != 0u) {
            out->max_result_terms = requested->max_result_terms;
        }
        if (requested->max_bytes != 0u) {
            out->max_bytes = requested->max_bytes;
        }
        if (requested->max_steps != 0u) {
            out->max_steps = requested->max_steps;
        }
        out->canonical = requested->canonical;
    }
    if (out->max_generated_terms == 0u || out->max_result_terms == 0u ||
        out->max_bytes < 4096u || out->max_steps == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

static phy_status build_declared_plan(const phy_tensor_monomial *monomial,
                                      const phy_young_reduce_limits *limits,
                                      phy_young_plan *out_plan,
                                      uint64_t *out_projected)
{
    size_t declared = 0u;
    for (size_t factor = 0u; factor < monomial->factor_count; ++factor) {
        if (monomial->factors[factor].head->has_young) {
            ++declared;
        }
    }
    phy_status status =
        plan_reserve(out_plan, declared, limits->max_bytes);
    for (size_t factor = 0u;
         factor < monomial->factor_count && status == PHY_OK; ++factor) {
        const phy_abstract_tensor_head *head =
            monomial->factors[factor].head;
        if (!head->has_young) {
            continue;
        }
        status = plan_append_projector(
            out_plan, monomial->factors[factor].index_offset, &head->young,
            (uint64_t)limits->max_generated_terms, limits->max_bytes);
    }
    if (status != PHY_OK) {
        plan_release(out_plan);
        return status;
    }
    *out_projected += (uint64_t)declared;
    return PHY_OK;
}

phy_status phy_tensor_expression_young_reduce(
    const phy_tensor_expression *expression,
    const phy_young_reduce_limits *requested,
    phy_tensor_expression **out_expression,
    phy_young_reduce_stats *out_stats)
{
    if (expression == NULL || out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_expression = NULL;
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof *out_stats);
    }
    phy_young_reduce_limits limits;
    phy_status status = resolve_reduce_limits(requested, &limits);
    if (status != PHY_OK) {
        return status;
    }
    phy_tensor_expression *result = NULL;
    status = phy_expression_create_with_signature(
        expression->context, limits.max_result_terms,
        expression->free_uses, expression->free_count, &result);
    if (status != PHY_OK) {
        return status;
    }

    uint64_t projected = 0u;
    uint64_t generated = 0u;
    uint32_t steps = 0u;
    for (size_t term = 0u;
         term < expression->term_count && status == PHY_OK; ++term) {
        const phy_tensor_monomial *source = expression->terms[term];
        phy_young_plan plan;
        status = build_declared_plan(source, &limits, &plan, &projected);
        if (status != PHY_OK) {
            break;
        }
        if (generated > (uint64_t)limits.max_generated_terms ||
            plan.images >
                (uint64_t)limits.max_generated_terms - generated) {
            plan_release(&plan);
            status = PHY_ERR_TERM_LIMIT;
            break;
        }
        status = plan_apply(
            &plan, source, result, &limits.canonical,
            limits.max_steps - steps,
            limits.max_bytes - plan.bytes_used, &generated,
            &steps);
        plan_release(&plan);
    }
    if (status != PHY_OK) {
        phy_tensor_expression_destroy(result);
        return status;
    }
    phy_expression_sort(result);
    if (out_stats != NULL) {
        out_stats->input_terms = expression->term_count;
        out_stats->collected_terms = result->term_count;
        out_stats->projected_factors = projected;
        out_stats->generated_terms = generated;
        out_stats->steps = steps;
    }
    *out_expression = result;
    return PHY_OK;
}
