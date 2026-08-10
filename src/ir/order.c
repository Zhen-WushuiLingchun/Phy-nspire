/*
 * Phy-nspire — canonical ordering.
 *
 * The order below is what makes `x*y` and `y*x` the same node. It must
 * therefore depend on structure alone. In particular it never compares node
 * refs or symbol ids, both of which record the order things happened to be
 * created in; it compares kinds, symbol name bytes, and numeric values, all of
 * which two contexts holding the same expression agree on.
 */
#include <string.h>

#include "ir_internal.h"

static uint64_t magnitude(int64_t value)
{
    return (value < 0) ? (~(uint64_t)value + 1u) : (uint64_t)value;
}

/*
 * Map a finite IEEE-754 binary64 value to an unsigned key with the same total
 * order as the numeric value, without executing a floating-point comparison.
 *
 * The IR already serializes reals by their 64-bit representation and rejects
 * NaN/infinity at construction. Negative zero is normalized to positive zero,
 * so equal keys are equal interned values.
 */
static uint64_t ordered_real_key(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (bits & 0x8000000000000000u) != 0u
               ? ~bits
               : bits ^ 0x8000000000000000u;
}

/* 64x64 -> 128, in 32-bit pieces. The device is 32-bit, so there is no
   __int128 to lean on and exact cross-multiplication has to be built. */
static void umul64(uint64_t a, uint64_t b, uint64_t *out_high,
                   uint64_t *out_low)
{
    const uint64_t a_low = a & 0xffffffffu;
    const uint64_t a_high = a >> 32;
    const uint64_t b_low = b & 0xffffffffu;
    const uint64_t b_high = b >> 32;

    const uint64_t low_low = a_low * b_low;
    const uint64_t low_high = a_low * b_high;
    const uint64_t high_low = a_high * b_low;
    const uint64_t high_high = a_high * b_high;

    const uint64_t middle =
        (low_low >> 32) + (low_high & 0xffffffffu) + (high_low & 0xffffffffu);

    *out_low = (low_low & 0xffffffffu) | (middle << 32);
    *out_high = high_high + (low_high >> 32) + (high_low >> 32) + (middle >> 32);
}

/*
 * Compare an/ad against bn/bd exactly. Both denominators are positive.
 *
 * Exact rather than approximate on purpose: a comparator that rounded could
 * report a < b, b < c, and c < a for values that differ below the double
 * mantissa, and heapsort given an intransitive comparator produces a
 * non-canonical order rather than an obvious failure.
 */
static int compare_exact(int64_t an, int64_t ad, int64_t bn, int64_t bd)
{
    const int sign_a = (an > 0) - (an < 0);
    const int sign_b = (bn > 0) - (bn < 0);
    if (sign_a != sign_b) {
        return (sign_a < sign_b) ? -1 : 1;
    }
    if (sign_a == 0) {
        return 0;
    }

    uint64_t left_high, left_low, right_high, right_low;
    umul64(magnitude(an), (uint64_t)bd, &left_high, &left_low);
    umul64(magnitude(bn), (uint64_t)ad, &right_high, &right_low);

    int result = 0;
    if (left_high != right_high) {
        result = (left_high < right_high) ? -1 : 1;
    } else if (left_low != right_low) {
        result = (left_low < right_low) ? -1 : 1;
    }
    return (sign_a > 0) ? result : -result;
}

/* Exact value of an exact-number node as a fraction. */
static void exact_value(const phy_ir_context *ctx, const phy_ir_node *node,
                        int64_t *out_numerator, int64_t *out_denominator)
{
    if (node->kind == (uint8_t)PHY_IR_INTEGER) {
        *out_numerator = node->u.integer;
        *out_denominator = 1;
        return;
    }
    const phy_ir_rat *rat = phy_ir_rat_at(ctx, node->u.rational_index);
    *out_numerator = (rat != NULL) ? rat->numerator : 0;
    *out_denominator = (rat != NULL) ? rat->denominator : 1;
}

static int compare_decimal_magnitude(const char *left, size_t left_length,
                                     const char *right,
                                     size_t right_length)
{
    if (left_length != right_length) {
        return left_length < right_length ? -1 : 1;
    }
    const int ordered = memcmp(left, right, left_length);
    return (ordered > 0) - (ordered < 0);
}

static int compare_big_exact(const phy_ir_context *ctx, phy_ir_ref left,
                             phy_ir_ref right)
{
    phy_ir_exact_view a;
    phy_ir_exact_view b;
    if (!phy_ir_exact_decimal_view(ctx, left, &a) ||
        !phy_ir_exact_decimal_view(ctx, right, &b)) {
        return 0;
    }
    const bool negative_a =
        a.numerator_length != 0u && a.numerator[0] == '-';
    const bool negative_b =
        b.numerator_length != 0u && b.numerator[0] == '-';
    if (negative_a != negative_b) {
        return negative_a ? -1 : 1;
    }
    const char *magnitude_a = a.numerator + (negative_a ? 1u : 0u);
    const char *magnitude_b = b.numerator + (negative_b ? 1u : 0u);
    const size_t magnitude_a_length =
        a.numerator_length - (negative_a ? 1u : 0u);
    const size_t magnitude_b_length =
        b.numerator_length - (negative_b ? 1u : 0u);
    int ordered = compare_decimal_magnitude(
        magnitude_a, magnitude_a_length, magnitude_b,
        magnitude_b_length);
    if (ordered != 0) {
        return negative_a ? -ordered : ordered;
    }
    ordered = compare_decimal_magnitude(
        a.denominator, a.denominator_length, b.denominator,
        b.denominator_length);
    return negative_a ? ordered : -ordered;
}

static int compare_names(const phy_ir_context *ctx, phy_ir_symbol a,
                         phy_ir_symbol b)
{
    if (a == b) {
        return 0;
    }
    const phy_ir_symbol_record *ra = phy_ir_symbol_at(ctx, a);
    const phy_ir_symbol_record *rb = phy_ir_symbol_at(ctx, b);
    if (ra == NULL || rb == NULL) {
        return (ra == NULL) ? ((rb == NULL) ? 0 : -1) : 1;
    }

    const char *strings = (const char *)ctx->strings.data;
    const size_t shortest =
        (ra->name_length < rb->name_length) ? ra->name_length : rb->name_length;
    const int ordered =
        memcmp(strings + ra->name_offset, strings + rb->name_offset, shortest);
    if (ordered != 0) {
        return (ordered < 0) ? -1 : 1;
    }
    if (ra->name_length == rb->name_length) {
        return 0;
    }
    return (ra->name_length < rb->name_length) ? -1 : 1;
}

int phy_ir_compare(const phy_ir_context *ctx, phy_ir_ref a, phy_ir_ref b)
{
    /* Interning makes this the common case and an exact answer. */
    if (a == b) {
        return 0;
    }

    const phy_ir_node *na = phy_ir_node_at(ctx, a);
    const phy_ir_node *nb = phy_ir_node_at(ctx, b);
    if (na == NULL || nb == NULL) {
        return (na == NULL) ? ((nb == NULL) ? 0 : -1) : 1;
    }

    const uint32_t rank_a = phy_ir_kind_rank((phy_ir_kind)na->kind);
    const uint32_t rank_b = phy_ir_kind_rank((phy_ir_kind)nb->kind);
    if (rank_a != rank_b) {
        return (rank_a < rank_b) ? -1 : 1;
    }

    switch ((phy_ir_kind)na->kind) {
    case PHY_IR_INTEGER:
    case PHY_IR_RATIONAL: {
        /*
         * Promoted exact atoms form a separate structural sub-order after the
         * inline fast path. This keeps comparison allocation-free and fully
         * transitive on the calculator. Arithmetic comparison of promoted
         * rationals belongs to the exact/CAS layer, not the IR's sort hook.
         */
        const bool big_a = na->aux == PHY_IR_EXACT_BIG;
        const bool big_b = nb->aux == PHY_IR_EXACT_BIG;
        if (big_a != big_b) {
            phy_ir_exact_view view_a;
            phy_ir_exact_view view_b;
            if (!phy_ir_exact_decimal_view(ctx, a, &view_a) ||
                !phy_ir_exact_decimal_view(ctx, b, &view_b)) {
                return big_a ? 1 : -1;
            }
            const bool negative_a =
                view_a.numerator[0] == '-';
            const bool negative_b =
                view_b.numerator[0] == '-';
            if (negative_a != negative_b) {
                return negative_a ? -1 : 1;
            }
            if (negative_a) {
                return big_a ? -1 : 1;
            }
            return big_a ? 1 : -1;
        }
        if (big_a) {
            return compare_big_exact(ctx, a, b);
        }
        /* One rank, two kinds: an integer and a reduced rational share the
           exact-number rank and are compared by value. Reduction guarantees no
           rational equals an integer, so this never needs a kind tie-break. */
        int64_t an, ad, bn, bd;
        exact_value(ctx, na, &an, &ad);
        exact_value(ctx, nb, &bn, &bd);
        return compare_exact(an, ad, bn, bd);
    }

    case PHY_IR_REAL: {
        const uint64_t key_a = ordered_real_key(na->u.real);
        const uint64_t key_b = ordered_real_key(nb->u.real);
        return (key_a > key_b) - (key_a < key_b);
    }

    case PHY_IR_SYMBOL:
        return compare_names(ctx, na->head, nb->head);

    case PHY_IR_INDEX: {
        const int by_name = compare_names(ctx, na->head, nb->head);
        if (by_name != 0) {
            return by_name;
        }
        if (na->aux != nb->aux) {
            return (na->aux < nb->aux) ? -1 : 1;
        }
        return compare_names(ctx, na->u.index_space, nb->u.index_space);
    }

    case PHY_IR_ERROR:
        return (na->aux < nb->aux) ? -1 : 1;

    default:
        break;
    }

    /* Compounds. Same rank means same kind for every compound in the table. */
    if (na->kind != nb->kind) {
        return (na->kind < nb->kind) ? -1 : 1;
    }
    if (na->head != nb->head) {
        const int by_name = compare_names(ctx, na->head, nb->head);
        if (by_name != 0) {
            return by_name;
        }
    }
    if (na->child_count != nb->child_count) {
        return (na->child_count < nb->child_count) ? -1 : 1;
    }

    const phy_ir_ref *ca = phy_ir_children_of(ctx, na);
    const phy_ir_ref *cb = phy_ir_children_of(ctx, nb);
    for (uint16_t i = 0; i < na->child_count; i++) {
        const int ordered = phy_ir_compare(ctx, ca[i], cb[i]);
        if (ordered != 0) {
            return ordered;
        }
    }

    /* Structurally identical nodes are the same node, so this is unreachable
       for interned input; returning 0 keeps the comparator total regardless. */
    return 0;
}

/* ---------------------------------------------------------------- sorting */

static void sift_down(const phy_ir_context *ctx, phy_ir_ref *refs, size_t root,
                      size_t count)
{
    for (;;) {
        size_t largest = root;
        const size_t left = 2u * root + 1u;
        const size_t right = left + 1u;

        if (left < count && phy_ir_compare(ctx, refs[left], refs[largest]) > 0) {
            largest = left;
        }
        if (right < count &&
            phy_ir_compare(ctx, refs[right], refs[largest]) > 0) {
            largest = right;
        }
        if (largest == root) {
            return;
        }

        const phy_ir_ref swap = refs[root];
        refs[root] = refs[largest];
        refs[largest] = swap;
        root = largest;
    }
}

/*
 * Heapsort, not insertion sort or quicksort.
 *
 * The term limit allows several thousand operands, which rules out insertion
 * sort's quadratic worst case; quicksort would need either recursion or an
 * explicit stack, and its worst case is reachable from user input. Heapsort is
 * O(n log n) unconditionally, iterative, and needs no scratch buffer -- the
 * three properties that matter on a device with a small stack and a fixed
 * memory budget.
 */
void phy_ir_sort_refs(const phy_ir_context *ctx, phy_ir_ref *refs, size_t count)
{
    if (refs == NULL || count < 2u) {
        return;
    }
    for (size_t i = count / 2u; i > 0u; i--) {
        sift_down(ctx, refs, i - 1u, count);
    }
    for (size_t end = count - 1u; end > 0u; end--) {
        const phy_ir_ref swap = refs[0];
        refs[0] = refs[end];
        refs[end] = swap;
        sift_down(ctx, refs, 0u, end);
    }
}
