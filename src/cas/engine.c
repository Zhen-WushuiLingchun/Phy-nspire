/*
 * Phy-nspire — scalar CAS engine: lifetime, budget, memo cache, scratch.
 *
 * Nothing here rewrites an expression. This file owns the machinery every
 * rewrite in the layer runs on, so that the rule files can be read as rules.
 *
 * Two decisions are worth stating up front.
 *
 * The memo cache is keyed on interned refs, which is only sound because the IR
 * never mutates a published node. A ref therefore names the same structure for
 * the life of the context, and a cached answer stays true -- with one exception,
 * declared assumptions, which phy_cas_cache_clear() exists for and which
 * include/phy/cas.h documents at the call site.
 *
 * The scratch arena is strictly LIFO and addressed by offset rather than by
 * pointer. A nested walk can grow it, and a pointer taken before that growth
 * would dangle. Offsets survive; that is the whole reason for the awkwardness.
 */
#include <string.h>

#include "cas_internal.h"
#include "phy/platform.h"

#define PHY_CAS_CANCEL_MASK 255u /* poll the cancel hook every 256 steps */
#define PHY_CAS_CACHE_INITIAL 128u

/* --------------------------------------------------------------- byte budget */

static bool charge(phy_cas *cas, size_t extra)
{
    if (cas->bytes >= cas->limits.max_bytes ||
        extra > cas->limits.max_bytes - cas->bytes) {
        return false;
    }
    cas->bytes += extra;
    return true;
}

static void discharge(phy_cas *cas, size_t amount)
{
    cas->bytes -= (amount <= cas->bytes) ? amount : cas->bytes;
}

/*
 * Grow to at least `needed` elements, holding both buffers at once because
 * there is no phy_realloc -- the same transient-peak accounting src/ir/ir.c
 * uses, for the same reason.
 */
static bool pool_reserve(phy_cas *cas, phy_cas_pool *pool, size_t elem_size,
                         size_t needed)
{
    if (needed <= pool->capacity) {
        return true;
    }

    size_t capacity = (pool->capacity != 0u) ? pool->capacity : 32u;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    if (capacity > (size_t)-1 / elem_size) {
        return false;
    }

    const size_t new_bytes = capacity * elem_size;
    const size_t old_bytes = pool->capacity * elem_size;
    if (!charge(cas, new_bytes)) {
        return false;
    }

    void *data = phy_alloc(new_bytes);
    if (data == NULL) {
        discharge(cas, new_bytes);
        return false;
    }
    if (pool->data != NULL) {
        memcpy(data, pool->data, pool->count * elem_size);
        phy_free(pool->data, old_bytes);
        discharge(cas, old_bytes);
    }
    pool->data = data;
    pool->capacity = capacity;
    return true;
}

static void pool_release(phy_cas *cas, phy_cas_pool *pool, size_t elem_size)
{
    if (pool->data != NULL) {
        const size_t bytes = pool->capacity * elem_size;
        phy_free(pool->data, bytes);
        discharge(cas, bytes);
    }
    pool->data = NULL;
    pool->count = 0u;
    pool->capacity = 0u;
}

/* ------------------------------------------------------------------ lifetime */

void phy_cas_limits_defaults(phy_cas_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_steps = 200000u;
    out_limits->max_bytes = (size_t)256u * 1024u;
}

phy_cas *phy_cas_create(phy_ir_context *ir, const phy_cas_limits *limits)
{
    if (ir == NULL) {
        return NULL;
    }

    phy_cas_limits resolved;
    phy_cas_limits_defaults(&resolved);
    if (limits != NULL) {
        if (limits->max_steps != 0u) {
            resolved.max_steps = limits->max_steps;
        }
        if (limits->max_bytes != 0u) {
            resolved.max_bytes = limits->max_bytes;
        }
    }
    /* Below this the struct itself would not fit and every operation would fail
       for a reason the caller could not act on. The cache, unlike scratch, is
       optional, so a small-but-workable budget is accepted. */
    if (resolved.max_bytes < 4096u) {
        return NULL;
    }

    phy_cas *cas = (phy_cas *)phy_alloc(sizeof *cas);
    if (cas == NULL) {
        return NULL;
    }
    memset(cas, 0, sizeof *cas);
    cas->ir = ir;
    cas->limits = resolved;
    cas->bytes = sizeof *cas;

    /*
     * The known-function heads and the three constants every rule reaches for.
     * Interning here rather than per visit makes recognizing sin() an integer
     * comparison; building 0, 1 and -1 here keeps the rules from re-deriving
     * them on every fold.
     */
    static const char *const kFunctionNames[PHY_CAS_FN_COUNT] = {
        NULL,   "sin",  "cos",  "tan",   "exp",
        "log",  "asin", "acos", "atan",  "sinh",
        "cosh", "tanh", "asinh", "acosh", "atanh",
        "gammafn", "loggamma", "erf", "erfc",
    };
    for (size_t index = 1u; index < PHY_CAS_FN_COUNT; ++index) {
        cas->functions[index] = phy_ir_intern(ir, kFunctionNames[index]);
    }
    cas->fn_integrate = phy_ir_intern(ir, "Integrate");
    cas->zero = phy_ir_integer(ir, 0);
    cas->one = phy_ir_integer(ir, 1);
    cas->minus_one = phy_ir_integer(ir, -1);

    const phy_ir_symbol pi = phy_ir_intern(ir, "Pi");
    const phy_ir_symbol e = phy_ir_intern(ir, "E");
    const phy_ir_symbol imaginary = phy_ir_intern(ir, "I");
    const phy_ir_symbol euler_gamma = phy_ir_intern(ir, "EulerGamma");
    cas->constant_pi = phy_ir_symbol_ref(ir, pi);
    cas->constant_e = phy_ir_symbol_ref(ir, e);
    cas->constant_i = phy_ir_symbol_ref(ir, imaginary);
    cas->constant_euler_gamma = phy_ir_symbol_ref(ir, euler_gamma);

    const uint32_t positive_constant =
        (uint32_t)PHY_IR_ASSUME_CONSTANT | (uint32_t)PHY_IR_ASSUME_REAL |
        (uint32_t)PHY_IR_ASSUME_POSITIVE | (uint32_t)PHY_IR_ASSUME_NONZERO;
    const uint32_t exact_constant =
        (uint32_t)PHY_IR_ASSUME_CONSTANT | (uint32_t)PHY_IR_ASSUME_NONZERO;
    const bool constants_ok =
        pi != PHY_IR_NO_SYMBOL && e != PHY_IR_NO_SYMBOL &&
        imaginary != PHY_IR_NO_SYMBOL &&
        euler_gamma != PHY_IR_NO_SYMBOL &&
        cas->constant_pi != PHY_IR_NULL && cas->constant_e != PHY_IR_NULL &&
        cas->constant_i != PHY_IR_NULL &&
        cas->constant_euler_gamma != PHY_IR_NULL &&
        phy_ir_assume(ir, pi, positive_constant) == PHY_OK &&
        phy_ir_assume(ir, e, positive_constant) == PHY_OK &&
        phy_ir_assume(ir, imaginary, exact_constant) == PHY_OK &&
        phy_ir_assume(ir, euler_gamma, positive_constant) == PHY_OK;

    bool functions_ok = true;
    for (size_t index = 1u; index < PHY_CAS_FN_COUNT; ++index) {
        functions_ok =
            functions_ok && cas->functions[index] != PHY_IR_NO_SYMBOL;
    }
    if (!functions_ok || !constants_ok ||
        cas->fn_integrate == PHY_IR_NO_SYMBOL ||
        cas->zero == PHY_IR_NULL || cas->one == PHY_IR_NULL ||
        cas->minus_one == PHY_IR_NULL) {
        phy_cas_destroy(cas);
        return NULL;
    }
    return cas;
}

void phy_cas_destroy(phy_cas *cas)
{
    if (cas == NULL) {
        return;
    }
    pool_release(cas, &cas->cache, sizeof(phy_cas_entry));
    pool_release(cas, &cas->scratch, sizeof(phy_ir_ref));
    phy_free(cas, sizeof *cas);
}

phy_ir_context *phy_cas_ir(const phy_cas *cas)
{
    return (cas != NULL) ? cas->ir : NULL;
}

void phy_cas_set_cancel(phy_cas *cas, bool (*cancelled)(void *user), void *user)
{
    if (cas == NULL) {
        return;
    }
    cas->cancelled = cancelled;
    cas->cancel_user = user;
}

uint32_t phy_cas_steps(const phy_cas *cas)
{
    return (cas != NULL) ? cas->steps : 0u;
}

uint64_t phy_cas_total_steps(const phy_cas *cas)
{
    return (cas != NULL) ? cas->total_steps : 0u;
}

size_t phy_cas_bytes_used(const phy_cas *cas)
{
    return (cas != NULL) ? cas->bytes : 0u;
}

/* -------------------------------------------------------------------- budget */

void phy_cas_begin(phy_cas *cas)
{
    cas->steps = 0u;
}

phy_status phy_cas_step(phy_cas *cas)
{
    return phy_cas_charge(cas, 1u);
}

phy_status phy_cas_charge(phy_cas *cas, uint32_t amount)
{
    if (amount > cas->limits.max_steps - cas->steps) {
        return PHY_ERR_TIMEOUT;
    }
    const uint32_t previous = cas->steps;
    cas->steps += amount;
    cas->total_steps += amount;
    /*
     * Polling every step would make an interactive cancel cost a call per node.
     * Every 256 is far below anything a user perceives and far above the cost of
     * asking.
     */
    if (cas->cancelled != NULL &&
        (previous >> 8u) != (cas->steps >> 8u) &&
        cas->cancelled(cas->cancel_user)) {
        return PHY_ERR_INTERRUPTED;
    }
    return PHY_OK;
}

phy_status phy_cas_ir_failure(const phy_cas *cas)
{
    const phy_status status = phy_ir_last_error(cas->ir);
    /* A builder returned PHY_IR_NULL, so something did fail; if the sticky flag
       does not say what, the operands are the only remaining explanation. */
    return (status != PHY_OK) ? status : PHY_ERR_INVALID_ARGUMENT;
}

/* --------------------------------------------------------------- memo cache */

static uint32_t mix(uint32_t hash, uint32_t value)
{
    hash ^= value;
    hash *= 16777619u;
    return hash ^ (hash >> 15);
}

static uint32_t entry_hash(phy_cas_memo tag, phy_ir_ref a, phy_ir_ref b)
{
    return mix(mix(mix(2166136261u, (uint32_t)tag), a), b);
}

static phy_cas_entry *entries_of(const phy_cas *cas)
{
    return (phy_cas_entry *)cas->cache.data;
}

bool phy_cas_cache_get(const phy_cas *cas, phy_cas_memo tag, phy_ir_ref a,
                       phy_ir_ref b, phy_ir_ref *out_r0, phy_ir_ref *out_r1)
{
    if (cas->cache.capacity == 0u) {
        return false;
    }
    const size_t mask = cas->cache.capacity - 1u;
    phy_cas_entry *slots = entries_of(cas);
    size_t at = (size_t)entry_hash(tag, a, b) & mask;

    /* The table is never more than three-quarters full, so this terminates on
       an empty slot rather than wrapping forever. */
    for (size_t probe = 0u; probe <= mask; probe++) {
        const phy_cas_entry *entry = &slots[at];
        if (entry->tag == (uint32_t)PHY_CAS_MEMO_NONE) {
            return false;
        }
        if (entry->tag == (uint32_t)tag && entry->a == a && entry->b == b) {
            if (out_r0 != NULL) {
                *out_r0 = entry->r0;
            }
            if (out_r1 != NULL) {
                *out_r1 = entry->r1;
            }
            return true;
        }
        at = (at + 1u) & mask;
    }
    return false;
}

static void cache_insert(phy_cas *cas, const phy_cas_entry *entry)
{
    const size_t mask = cas->cache.capacity - 1u;
    phy_cas_entry *slots = entries_of(cas);
    size_t at = (size_t)entry_hash((phy_cas_memo)entry->tag, entry->a, entry->b) &
                mask;
    while (slots[at].tag != (uint32_t)PHY_CAS_MEMO_NONE) {
        at = (at + 1u) & mask;
    }
    slots[at] = *entry;
    cas->cache_live++;
}

void phy_cas_cache_clear(phy_cas *cas)
{
    if (cas == NULL) {
        return;
    }
    if (cas->cache.data != NULL) {
        const size_t bytes = cas->cache.capacity * sizeof(phy_cas_entry);
        phy_free(cas->cache.data, bytes);
        discharge(cas, bytes);
    }
    memset(&cas->cache, 0, sizeof cas->cache);
    cas->cache_live = 0u;
}

/*
 * Growing means rehashing into a fresh table, which needs both at once.
 *
 * When that does not fit the byte ceiling -- or fails outright -- the answer is
 * to empty the table and carry on. A cache that cannot grow costs repeated work;
 * a cache that reports failure would turn a memory ceiling into a wrong status
 * on a computation that is perfectly able to finish.
 */
static void cache_grow(phy_cas *cas)
{
    const size_t capacity =
        (cas->cache.capacity != 0u) ? cas->cache.capacity * 2u
                                    : (size_t)PHY_CAS_CACHE_INITIAL;
    const size_t bytes = capacity * sizeof(phy_cas_entry);

    if (!charge(cas, bytes)) {
        phy_cas_cache_clear(cas);
        return;
    }
    phy_cas_entry *slots = (phy_cas_entry *)phy_alloc(bytes);
    if (slots == NULL) {
        discharge(cas, bytes);
        phy_cas_cache_clear(cas);
        return;
    }
    memset(slots, 0, bytes);

    const phy_cas_pool previous = cas->cache;
    cas->cache.data = slots;
    cas->cache.capacity = capacity;
    cas->cache.count = capacity;
    cas->cache_live = 0u;

    if (previous.data != NULL) {
        const phy_cas_entry *old = (const phy_cas_entry *)previous.data;
        for (size_t i = 0u; i < previous.capacity; i++) {
            if (old[i].tag != (uint32_t)PHY_CAS_MEMO_NONE) {
                cache_insert(cas, &old[i]);
            }
        }
        phy_free(previous.data, previous.capacity * sizeof(phy_cas_entry));
        discharge(cas, previous.capacity * sizeof(phy_cas_entry));
    }
}

void phy_cas_cache_put(phy_cas *cas, phy_cas_memo tag, phy_ir_ref a,
                       phy_ir_ref b, phy_ir_ref r0, phy_ir_ref r1)
{
    if (cas->cache.capacity == 0u ||
        cas->cache_live + 1u > cas->cache.capacity - cas->cache.capacity / 4u) {
        cache_grow(cas);
        if (cas->cache.capacity == 0u) {
            return; /* no table at all; every lookup will simply miss */
        }
    }

    phy_cas_entry entry;
    memset(&entry, 0, sizeof entry);
    entry.tag = (uint32_t)tag;
    entry.a = a;
    entry.b = b;
    entry.r0 = r0;
    entry.r1 = r1;
    cache_insert(cas, &entry);
}

/* ------------------------------------------------------------ scratch arena */

size_t phy_cas_scratch_mark(const phy_cas *cas)
{
    return cas->scratch.count;
}

phy_status phy_cas_scratch_alloc(phy_cas *cas, size_t count, size_t *out_offset)
{
    const size_t offset = cas->scratch.count;
    if (count > (size_t)-1 - offset) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    if (!pool_reserve(cas, &cas->scratch, sizeof(phy_ir_ref), offset + count)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    cas->scratch.count = offset + count;
    *out_offset = offset;
    return PHY_OK;
}

phy_ir_ref *phy_cas_scratch_at(phy_cas *cas, size_t offset)
{
    return &((phy_ir_ref *)cas->scratch.data)[offset];
}

void phy_cas_scratch_release(phy_cas *cas, size_t mark)
{
    cas->scratch.count = mark;
}

phy_status phy_cas_temp_alloc(phy_cas *cas, size_t bytes, void **out_pointer)
{
    if (cas == NULL || out_pointer == NULL || bytes == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_pointer = NULL;
    if (!charge(cas, bytes)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    void *pointer = phy_alloc(bytes);
    if (pointer == NULL) {
        discharge(cas, bytes);
        return PHY_ERR_OUT_OF_MEMORY;
    }
    *out_pointer = pointer;
    return PHY_OK;
}

void phy_cas_temp_free(phy_cas *cas, void *pointer, size_t bytes)
{
    if (cas == NULL || pointer == NULL || bytes == 0u) {
        return;
    }
    phy_free(pointer, bytes);
    discharge(cas, bytes);
}

/* ----------------------------------------------------------- pair sorting */

/*
 * Heapsort over (key, value) pairs, for the same three reasons src/ir/order.c
 * gives: unconditional O(n log n) on operand counts the term limit allows,
 * iterative, and no scratch of its own -- which matters here, since the array
 * being sorted already lives in the arena.
 *
 * Nothing in this function allocates, so the base pointer is stable throughout.
 */
static void sift_down(const phy_ir_context *ir, phy_ir_ref *pairs, size_t root,
                      size_t count)
{
    for (;;) {
        size_t largest = root;
        const size_t left = 2u * root + 1u;
        const size_t right = left + 1u;

        if (left < count &&
            phy_ir_compare(ir, pairs[2u * left], pairs[2u * largest]) > 0) {
            largest = left;
        }
        if (right < count &&
            phy_ir_compare(ir, pairs[2u * right], pairs[2u * largest]) > 0) {
            largest = right;
        }
        if (largest == root) {
            return;
        }
        for (size_t i = 0u; i < 2u; i++) {
            const phy_ir_ref swap = pairs[2u * root + i];
            pairs[2u * root + i] = pairs[2u * largest + i];
            pairs[2u * largest + i] = swap;
        }
        root = largest;
    }
}

void phy_cas_sort_pairs(phy_cas *cas, size_t offset, size_t count)
{
    if (count < 2u) {
        return;
    }
    phy_ir_ref *pairs = phy_cas_scratch_at(cas, offset);
    for (size_t i = count / 2u; i > 0u; i--) {
        sift_down(cas->ir, pairs, i - 1u, count);
    }
    for (size_t end = count - 1u; end > 0u; end--) {
        for (size_t i = 0u; i < 2u; i++) {
            const phy_ir_ref swap = pairs[i];
            pairs[i] = pairs[2u * end + i];
            pairs[2u * end + i] = swap;
        }
        sift_down(cas->ir, pairs, 0u, end);
    }
}

/* -------------------------------------------------------------- predicates */

bool phy_cas_is_exact(const phy_cas *cas, phy_ir_ref ref)
{
    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, ref);
    return kind == PHY_IR_INTEGER || kind == PHY_IR_RATIONAL;
}

bool phy_cas_exact_value(const phy_cas *cas, phy_ir_ref ref,
                         phy_cas_rat *out_value)
{
    int64_t value;
    if (phy_ir_integer_value(cas->ir, ref, &value)) {
        out_value->num = value;
        out_value->den = 1;
        return true;
    }
    int64_t numerator, denominator;
    if (phy_ir_rational_value(cas->ir, ref, &numerator, &denominator)) {
        out_value->num = numerator;
        out_value->den = denominator;
        return true;
    }
    return false;
}

phy_status phy_cas_number_node(phy_cas *cas, phy_cas_rat value,
                               phy_ir_ref *out_ref)
{
    /* The IR reduces, moves the sign to the numerator, and collapses a unit
       denominator to an integer, so there is exactly one node per value. */
    const phy_ir_ref ref = phy_ir_rational(cas->ir, value.num, value.den);
    if (ref == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = ref;
    return PHY_OK;
}

bool phy_cas_is_integer(const phy_cas *cas, phy_ir_ref ref, int64_t value)
{
    int64_t held;
    return phy_ir_integer_value(cas->ir, ref, &held) && held == value;
}

bool phy_cas_kind_is_opaque(phy_ir_kind kind)
{
    switch (kind) {
    case PHY_IR_INDEX:
    case PHY_IR_NCMUL:
    case PHY_IR_TENSOR:
    case PHY_IR_OPERATOR:
    case PHY_IR_DERIVATIVE:
    case PHY_IR_WEDGE:
        return true;
    default:
        return false;
    }
}

bool phy_cas_is_known_head(const phy_cas *cas, phy_ir_symbol head)
{
    return phy_cas_function_id(cas, head) != PHY_CAS_FN_INVALID;
}

phy_cas_function phy_cas_function_id(const phy_cas *cas, phy_ir_symbol head)
{
    if (cas == NULL || head == PHY_IR_NO_SYMBOL) {
        return PHY_CAS_FN_INVALID;
    }
    for (size_t index = 1u; index < PHY_CAS_FN_COUNT; ++index) {
        if (head == cas->functions[index]) {
            return (phy_cas_function)index;
        }
    }
    return PHY_CAS_FN_INVALID;
}

phy_cas_function phy_cas_function_of(const phy_cas *cas, phy_ir_ref ref)
{
    if (cas == NULL || phy_ir_kind_of(cas->ir, ref) != PHY_IR_FUNCTION ||
        phy_ir_child_count(cas->ir, ref) != 1u) {
        return PHY_CAS_FN_INVALID;
    }
    return phy_cas_function_id(cas, phy_ir_head(cas->ir, ref));
}

phy_ir_symbol phy_cas_known_function(const phy_cas *cas, phy_ir_ref ref)
{
    if (phy_ir_kind_of(cas->ir, ref) != PHY_IR_FUNCTION ||
        phy_ir_child_count(cas->ir, ref) != 1u) {
        return PHY_IR_NO_SYMBOL;
    }
    const phy_ir_symbol head = phy_ir_head(cas->ir, ref);
    return phy_cas_function_id(cas, head) != PHY_CAS_FN_INVALID
               ? head
               : PHY_IR_NO_SYMBOL;
}

bool phy_cas_known_nonzero(const phy_cas *cas, phy_ir_ref ref)
{
    const phy_ir_context *ir = cas->ir;
    phy_cas_rat value;
    if (phy_cas_exact_value(cas, ref, &value)) {
        return value.num != 0;
    }
    if (phy_cas_is_exact(cas, ref)) {
        return phy_cas_exact_sign_ref(cas, ref) != 0;
    }

    switch (phy_ir_kind_of(ir, ref)) {
    case PHY_IR_SYMBOL: {
        /* A declared sign implies nonzero; so does the declaration itself. */
        const uint32_t assumptions =
            phy_ir_assumptions(ir, phy_ir_head(ir, ref));
        return (assumptions & ((uint32_t)PHY_IR_ASSUME_NONZERO |
                               (uint32_t)PHY_IR_ASSUME_POSITIVE |
                               (uint32_t)PHY_IR_ASSUME_NEGATIVE)) != 0u;
    }

    case PHY_IR_MUL: {
        /* A product of nonzero factors is nonzero; one unproved factor is
           enough to lose the proof. */
        const size_t count = phy_ir_child_count(ir, ref);
        for (size_t i = 0u; i < count; i++) {
            if (!phy_cas_known_nonzero(cas, phy_ir_child(ir, ref, i))) {
                return false;
            }
        }
        return true;
    }

    case PHY_IR_POW:
        /* Where it is defined, a power of a nonzero base is nonzero. */
        return phy_cas_known_nonzero(cas, phy_ir_child(ir, ref, 0u));

    case PHY_IR_FUNCTION:
        /* exp never vanishes. The other known functions do. */
        return phy_cas_function_of(cas, ref) == PHY_CAS_FN_EXP;

    default:
        /* Sums included: deciding a sum is the zero decision, and answering it
           from here would be either circular or a guess. */
        return false;
    }
}

/* --------------------------------------------------------------- integrity */

phy_status phy_cas_validate(const phy_cas *cas)
{
    if (cas == NULL || cas->ir == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    /*
     * Every mark taken was released. An operation that failed part-way through a
     * collection is exactly where a leak would hide, so the tests validate after
     * forced failures rather than only after successes.
     */
    if (cas->scratch.count != 0u) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    const size_t expected = sizeof *cas +
                            cas->cache.capacity * sizeof(phy_cas_entry) +
                            cas->scratch.capacity * sizeof(phy_ir_ref);
    if (cas->bytes != expected) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    size_t live = 0u;
    const phy_cas_entry *slots = entries_of(cas);
    for (size_t i = 0u; i < cas->cache.capacity; i++) {
        const phy_cas_entry *entry = &slots[i];
        if (entry->tag == (uint32_t)PHY_CAS_MEMO_NONE) {
            continue;
        }
        if (entry->tag > (uint32_t)PHY_CAS_MEMO_EXPAND_FACTORED) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        /* Keys and answers are nodes of the borrowed context. `b` is exempt:
           the substitution tag stores a call epoch there, not a ref. */
        if (phy_ir_kind_of(cas->ir, entry->a) == PHY_IR_KIND_INVALID ||
            phy_ir_kind_of(cas->ir, entry->r0) == PHY_IR_KIND_INVALID) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        live++;
    }
    if (live != cas->cache_live) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    return PHY_OK;
}
