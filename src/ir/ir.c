/*
 * Phy-nspire — expression IR core: pools, interning, and construction.
 *
 * Every node reaches the graph through intern_node(), so the invariants the
 * rest of the system relies on are established in exactly one place:
 * structurally identical nodes are one node, commutative operands are sorted,
 * associative nesting is flattened, and no construction can exceed the
 * configured ceilings.
 */
#include <string.h>

#include "ir_internal.h"
#include "phy/platform.h"

_Static_assert(sizeof(double) == 8, "IR reals assume IEEE-754 binary64");
_Static_assert(sizeof(phy_ir_node) == 32, "IR node layout grew unexpectedly");

/* ------------------------------------------------------------- kind table */

typedef struct {
    const char *name;  /* enumerator spelling */
    const char *token; /* serialization token; NULL when written literally */
    uint32_t flags;
    uint32_t rank; /* position in the canonical order */
} kind_info;

/*
 * The single source of truth for kind behaviour. Ordering, hashing,
 * interning, and serialization all read this table rather than switching on
 * the enum, so adding a kind is one row plus, where the shape is unusual, a
 * builder.
 *
 * Ranks put exact numbers first, then inexact, then atoms, then compounds.
 * Exact and inexact numbers are deliberately separated: comparing an int64
 * against a double exactly would need arithmetic wider than either target
 * offers, and an approximate comparison here would be an intransitive
 * comparator, which would silently corrupt the sort.
 */
static const kind_info kKindInfo[PHY_IR_KIND_COUNT] = {
    [PHY_IR_KIND_INVALID] = {"PHY_IR_KIND_INVALID", NULL, 0u, 0u},

    [PHY_IR_INTEGER] = {"PHY_IR_INTEGER", NULL, PHY_IR_KIND_ATOM, 1u},
    [PHY_IR_RATIONAL] = {"PHY_IR_RATIONAL", "rat", PHY_IR_KIND_ATOM, 1u},
    [PHY_IR_REAL] = {"PHY_IR_REAL", "real", PHY_IR_KIND_ATOM, 2u},
    [PHY_IR_SYMBOL] = {"PHY_IR_SYMBOL", NULL,
                       PHY_IR_KIND_ATOM | PHY_IR_KIND_HEADED, 3u},
    [PHY_IR_INDEX] = {"PHY_IR_INDEX", "idx",
                      PHY_IR_KIND_ATOM | PHY_IR_KIND_HEADED, 4u},
    [PHY_IR_ERROR] = {"PHY_IR_ERROR", "err", PHY_IR_KIND_ATOM, 5u},

    [PHY_IR_ADD] = {"PHY_IR_ADD", "+",
                    PHY_IR_KIND_NARY | PHY_IR_KIND_COMMUTATIVE |
                        PHY_IR_KIND_ASSOCIATIVE,
                    6u},
    [PHY_IR_MUL] = {"PHY_IR_MUL", "*",
                    PHY_IR_KIND_NARY | PHY_IR_KIND_COMMUTATIVE |
                        PHY_IR_KIND_ASSOCIATIVE,
                    7u},
    [PHY_IR_NCMUL] = {"PHY_IR_NCMUL", "nc*",
                      PHY_IR_KIND_NARY | PHY_IR_KIND_ASSOCIATIVE, 8u},
    [PHY_IR_POW] = {"PHY_IR_POW", "^", 0u, 9u},
    [PHY_IR_EQUATION] = {"PHY_IR_EQUATION", "=", 0u, 10u},
    [PHY_IR_FUNCTION] = {"PHY_IR_FUNCTION", "fn",
                         PHY_IR_KIND_NARY | PHY_IR_KIND_HEADED, 11u},

    [PHY_IR_TENSOR] = {"PHY_IR_TENSOR", "tensor",
                       PHY_IR_KIND_NARY | PHY_IR_KIND_HEADED, 12u},
    [PHY_IR_OPERATOR] = {"PHY_IR_OPERATOR", "op",
                         PHY_IR_KIND_NARY | PHY_IR_KIND_HEADED, 13u},
    [PHY_IR_DERIVATIVE] = {"PHY_IR_DERIVATIVE", "d", PHY_IR_KIND_NARY, 14u},
    [PHY_IR_WEDGE] = {"PHY_IR_WEDGE", "wedge",
                      PHY_IR_KIND_NARY | PHY_IR_KIND_ASSOCIATIVE, 15u},
};

static bool kind_valid(phy_ir_kind kind)
{
    return (unsigned)kind > (unsigned)PHY_IR_KIND_INVALID &&
           (unsigned)kind < (unsigned)PHY_IR_KIND_COUNT;
}

const char *phy_ir_kind_name(phy_ir_kind kind)
{
    return kind_valid(kind) ? kKindInfo[kind].name : "PHY_IR_KIND_INVALID";
}

uint32_t phy_ir_kind_flags(phy_ir_kind kind)
{
    return kind_valid(kind) ? kKindInfo[kind].flags : 0u;
}

uint32_t phy_ir_kind_rank(phy_ir_kind kind)
{
    return kind_valid(kind) ? kKindInfo[kind].rank : 0u;
}

const char *phy_ir_kind_token(phy_ir_kind kind)
{
    return kind_valid(kind) ? kKindInfo[kind].token : NULL;
}

/* ------------------------------------------------------------------ hashes */

#define FNV_OFFSET 2166136261u
#define FNV_PRIME 16777619u

static uint32_t hash_bytes(uint32_t seed, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t hash = seed;
    for (size_t i = 0; i < size; i++) {
        hash ^= (uint32_t)bytes[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

static uint32_t hash_u32(uint32_t seed, uint32_t value)
{
    return hash_bytes(seed, &value, sizeof value);
}

/* ------------------------------------------------------------------- pools */

static bool charge(phy_ir_context *ctx, size_t extra)
{
    /* The subtraction below would wrap if the budget were already spent, so
       the exhausted case is tested first rather than relied upon not to
       happen. */
    if (ctx->bytes >= ctx->limits.max_bytes ||
        extra > ctx->limits.max_bytes - ctx->bytes) {
        phy_ir_set_error(ctx, PHY_ERR_MEMORY_LIMIT);
        return false;
    }
    ctx->bytes += extra;
    return true;
}

static void discharge(phy_ir_context *ctx, size_t amount)
{
    ctx->bytes -= (amount <= ctx->bytes) ? amount : ctx->bytes;
}

/*
 * Grow to at least `needed` elements.
 *
 * The ceiling is tested against the transient peak, not the final size: there
 * is no phy_realloc, so growth holds the old and new buffers at once. Checking
 * only the final size would let a context that fits its budget still fail to
 * allocate on a device that is exactly as full as the budget says it may be.
 */
bool phy_ir_pool_reserve(phy_ir_context *ctx, phy_pool *pool, size_t elem_size,
                         size_t needed)
{
    if (needed <= pool->capacity) {
        return true;
    }

    size_t capacity = (pool->capacity != 0u) ? pool->capacity : 16u;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    if (capacity > (size_t)-1 / elem_size) {
        phy_ir_set_error(ctx, PHY_ERR_MEMORY_LIMIT);
        return false;
    }

    const size_t new_bytes = capacity * elem_size;
    const size_t old_bytes = pool->capacity * elem_size;
    if (!charge(ctx, new_bytes)) {
        return false;
    }

    void *data = phy_alloc(new_bytes);
    if (data == NULL) {
        discharge(ctx, new_bytes);
        phy_ir_set_error(ctx, PHY_ERR_OUT_OF_MEMORY);
        return false;
    }
    if (pool->data != NULL) {
        memcpy(data, pool->data, pool->count * elem_size);
        phy_free(pool->data, old_bytes);
        discharge(ctx, old_bytes);
    }
    pool->data = data;
    pool->capacity = capacity;
    return true;
}

void phy_ir_pool_release(phy_ir_context *ctx, phy_pool *pool, size_t elem_size)
{
    if (pool->data != NULL) {
        const size_t bytes = pool->capacity * elem_size;
        phy_free(pool->data, bytes);
        discharge(ctx, bytes);
    }
    pool->data = NULL;
    pool->count = 0u;
    pool->capacity = 0u;
}

/* Appends one element and returns its index, or SIZE_MAX on failure. */
static size_t pool_push(phy_ir_context *ctx, phy_pool *pool, size_t elem_size,
                        const void *value)
{
    if (!phy_ir_pool_reserve(ctx, pool, elem_size, pool->count + 1u)) {
        return (size_t)-1;
    }
    memcpy((unsigned char *)pool->data + pool->count * elem_size, value,
           elem_size);
    return pool->count++;
}

static phy_ir_node *nodes_of(const phy_ir_context *ctx)
{
    return (phy_ir_node *)ctx->nodes.data;
}

static phy_ir_symbol_record *symbols_of(const phy_ir_context *ctx)
{
    return (phy_ir_symbol_record *)ctx->symbols.data;
}

static phy_ir_ref *refs_of(const phy_pool *pool)
{
    return (phy_ir_ref *)pool->data;
}

/* --------------------------------------------------------------- accessors */

phy_status phy_ir_set_error(phy_ir_context *ctx, phy_status status)
{
    if (ctx != NULL && ctx->error == PHY_OK) {
        ctx->error = status;
    }
    return status;
}

const phy_ir_node *phy_ir_node_at(const phy_ir_context *ctx, phy_ir_ref ref)
{
    if (ctx == NULL || ref == PHY_IR_NULL || (size_t)ref >= ctx->nodes.count) {
        return NULL;
    }
    return &nodes_of(ctx)[ref];
}

const phy_ir_symbol_record *phy_ir_symbol_at(const phy_ir_context *ctx,
                                             phy_ir_symbol sym)
{
    if (ctx == NULL || sym == PHY_IR_NO_SYMBOL ||
        (size_t)sym >= ctx->symbols.count) {
        return NULL;
    }
    return &symbols_of(ctx)[sym];
}

const phy_ir_rat *phy_ir_rat_at(const phy_ir_context *ctx, uint32_t index)
{
    if (ctx == NULL || (size_t)index >= ctx->rationals.count) {
        return NULL;
    }
    return &((const phy_ir_rat *)ctx->rationals.data)[index];
}

const phy_ir_ref *phy_ir_children_of(const phy_ir_context *ctx,
                                     const phy_ir_node *node)
{
    if (node == NULL || node->child_count == 0u) {
        return NULL;
    }
    return &refs_of(&ctx->children)[node->u.child_offset];
}

/* ----------------------------------------------------------------- context */

void phy_ir_limits_defaults(phy_ir_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_nodes = 200000u;
    out_limits->max_depth = 256u;
    out_limits->max_children = 4096u;
    out_limits->max_bytes = (size_t)8u * 1024u * 1024u;
}

static bool resolve_limits(phy_ir_limits *limits)
{
    phy_ir_limits defaults;
    phy_ir_limits_defaults(&defaults);

    if (limits->max_nodes == 0u) {
        limits->max_nodes = defaults.max_nodes;
    }
    if (limits->max_depth == 0u) {
        limits->max_depth = defaults.max_depth;
    }
    if (limits->max_children == 0u) {
        limits->max_children = defaults.max_children;
    }
    if (limits->max_bytes == 0u) {
        limits->max_bytes = defaults.max_bytes;
    }

    /* Clamped rather than rejected: a caller asking for "as deep as possible"
       should get the safe maximum, not a failure. */
    if (limits->max_depth > PHY_IR_MAX_DEPTH_CEILING) {
        limits->max_depth = PHY_IR_MAX_DEPTH_CEILING;
    }
    if (limits->max_children > PHY_IR_MAX_CHILDREN_CEILING) {
        limits->max_children = PHY_IR_MAX_CHILDREN_CEILING;
    }
    return limits->max_bytes >= 64u * 1024u;
}

static bool buckets_init(phy_ir_context *ctx, phy_pool *pool, size_t count)
{
    if (!phy_ir_pool_reserve(ctx, pool, sizeof(phy_ir_ref), count)) {
        return false;
    }
    memset(pool->data, 0, count * sizeof(phy_ir_ref));
    pool->count = count;
    return true;
}

phy_ir_context *phy_ir_context_create(const phy_ir_limits *limits)
{
    phy_ir_limits resolved;
    if (limits != NULL) {
        resolved = *limits;
    } else {
        memset(&resolved, 0, sizeof resolved);
    }
    if (!resolve_limits(&resolved)) {
        return NULL;
    }

    phy_ir_context *ctx = (phy_ir_context *)phy_alloc(sizeof *ctx);
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof *ctx);
    ctx->limits = resolved;
    ctx->bytes = sizeof *ctx;

    /* Element 0 is a reserved sentinel in each handle-bearing pool so that a
       zero handle is always invalid. */
    static const phy_ir_node kNullNode = {0};
    static const phy_ir_symbol_record kNullSymbol = {0};
    static const phy_ir_symmetry_record kNullSymmetry = {0};

    const bool ok =
        pool_push(ctx, &ctx->nodes, sizeof(phy_ir_node), &kNullNode) == 0u &&
        pool_push(ctx, &ctx->symbols, sizeof(phy_ir_symbol_record),
                  &kNullSymbol) == 0u &&
        pool_push(ctx, &ctx->symmetries, sizeof(phy_ir_symmetry_record),
                  &kNullSymmetry) == 0u &&
        buckets_init(ctx, &ctx->node_buckets, 256u) &&
        buckets_init(ctx, &ctx->symbol_buckets, 128u);

    if (!ok) {
        phy_ir_context_destroy(ctx);
        return NULL;
    }
    return ctx;
}

void phy_ir_context_destroy(phy_ir_context *ctx)
{
    if (ctx == NULL) {
        return;
    }
    phy_ir_pool_release(ctx, &ctx->nodes, sizeof(phy_ir_node));
    phy_ir_pool_release(ctx, &ctx->children, sizeof(phy_ir_ref));
    phy_ir_pool_release(ctx, &ctx->rationals, sizeof(phy_ir_rat));
    phy_ir_pool_release(ctx, &ctx->symbols, sizeof(phy_ir_symbol_record));
    phy_ir_pool_release(ctx, &ctx->strings, sizeof(char));
    phy_ir_pool_release(ctx, &ctx->symmetries, sizeof(phy_ir_symmetry_record));
    phy_ir_pool_release(ctx, &ctx->node_buckets, sizeof(phy_ir_ref));
    phy_ir_pool_release(ctx, &ctx->symbol_buckets, sizeof(phy_ir_symbol));
    phy_ir_pool_release(ctx, &ctx->scratch, sizeof(phy_ir_ref));
    phy_free(ctx, sizeof *ctx);
}

phy_status phy_ir_last_error(const phy_ir_context *ctx)
{
    return (ctx != NULL) ? ctx->error : PHY_ERR_INVALID_ARGUMENT;
}

void phy_ir_clear_error(phy_ir_context *ctx)
{
    if (ctx != NULL) {
        ctx->error = PHY_OK;
    }
}

size_t phy_ir_node_count(const phy_ir_context *ctx)
{
    /* The sentinel at index 0 is not a node the caller created. */
    return (ctx != NULL && ctx->nodes.count > 0u) ? ctx->nodes.count - 1u : 0u;
}

size_t phy_ir_symbol_count(const phy_ir_context *ctx)
{
    return (ctx != NULL && ctx->symbols.count > 0u) ? ctx->symbols.count - 1u
                                                    : 0u;
}

size_t phy_ir_bytes_used(const phy_ir_context *ctx)
{
    return (ctx != NULL) ? ctx->bytes : 0u;
}

/* ----------------------------------------------------------------- symbols */

static void symbol_rehash(phy_ir_context *ctx)
{
    const size_t buckets = ctx->symbol_buckets.count;
    if (ctx->symbols.count <= buckets - buckets / 4u) {
        return;
    }
    /*
     * A failed rehash is not a failed operation: the table keeps working with
     * longer chains. Restoring the previous error keeps a recoverable event
     * from being reported to the caller as if their build had failed.
     */
    const phy_status saved = ctx->error;
    phy_pool grown = {NULL, 0u, 0u};
    if (!buckets_init(ctx, &grown, buckets * 2u)) {
        ctx->error = saved;
        return;
    }
    phy_ir_symbol *table = (phy_ir_symbol *)grown.data;
    phy_ir_symbol_record *records = symbols_of(ctx);
    for (size_t i = 1u; i < ctx->symbols.count; i++) {
        const size_t slot = records[i].name_hash & (grown.count - 1u);
        records[i].intern_next = table[slot];
        table[slot] = (phy_ir_symbol)i;
    }
    phy_ir_pool_release(ctx, &ctx->symbol_buckets, sizeof(phy_ir_symbol));
    ctx->symbol_buckets = grown;
}

phy_ir_symbol phy_ir_intern_n(phy_ir_context *ctx, const char *name,
                              size_t length)
{
    if (ctx == NULL) {
        return PHY_IR_NO_SYMBOL;
    }
    if (name == NULL || length == 0u || length > 0xffffffffu) {
        phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        return PHY_IR_NO_SYMBOL;
    }

    const uint32_t hash = hash_bytes(FNV_OFFSET, name, length);
    const size_t slot = hash & (ctx->symbol_buckets.count - 1u);
    const phy_ir_symbol *table = (const phy_ir_symbol *)ctx->symbol_buckets.data;

    for (phy_ir_symbol sym = table[slot]; sym != PHY_IR_NO_SYMBOL;
         sym = symbols_of(ctx)[sym].intern_next) {
        const phy_ir_symbol_record *record = &symbols_of(ctx)[sym];
        if (record->name_hash == hash && record->name_length == length &&
            memcmp((const char *)ctx->strings.data + record->name_offset, name,
                   length) == 0) {
            return sym;
        }
    }

    /* Miss: copy the name, then publish the record. */
    if (!phy_ir_pool_reserve(ctx, &ctx->strings, sizeof(char),
                      ctx->strings.count + length + 1u)) {
        return PHY_IR_NO_SYMBOL;
    }
    const size_t name_offset = ctx->strings.count;
    char *strings = (char *)ctx->strings.data;
    memcpy(strings + name_offset, name, length);
    strings[name_offset + length] = '\0';
    ctx->strings.count += length + 1u;

    phy_ir_symbol_record record;
    memset(&record, 0, sizeof record);
    record.name_offset = (uint32_t)name_offset;
    record.name_length = (uint32_t)length;
    record.name_hash = hash;
    record.intern_next = ((const phy_ir_symbol *)ctx->symbol_buckets.data)[slot];

    const size_t index =
        pool_push(ctx, &ctx->symbols, sizeof record, &record);
    if (index == (size_t)-1) {
        /*
         * The name is already in the string pool but the symbol that owns it
         * will not exist. Unwind the append: leaving it would be bytes no
         * symbol references and nothing can ever reach, and a caller retrying
         * under a tight budget would stack up one copy per attempt.
         *
         * Only the count is rewound. Capacity keeps its peak, which is
         * correct -- the memory is already charged and reusable.
         */
        ctx->strings.count = name_offset;
        return PHY_IR_NO_SYMBOL;
    }
    ((phy_ir_symbol *)ctx->symbol_buckets.data)[slot] = (phy_ir_symbol)index;
    symbol_rehash(ctx);
    return (phy_ir_symbol)index;
}

phy_ir_symbol phy_ir_intern(phy_ir_context *ctx, const char *name)
{
    if (name == NULL) {
        if (ctx != NULL) {
            phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        }
        return PHY_IR_NO_SYMBOL;
    }
    return phy_ir_intern_n(ctx, name, strlen(name));
}

const char *phy_ir_symbol_name(const phy_ir_context *ctx, phy_ir_symbol sym)
{
    const phy_ir_symbol_record *record = phy_ir_symbol_at(ctx, sym);
    if (record == NULL) {
        return NULL;
    }
    return (const char *)ctx->strings.data + record->name_offset;
}

/* ------------------------------------------------- assumptions, symmetries */

#define PHY_IR_ASSUME_ALL                                                      \
    (PHY_IR_ASSUME_REAL | PHY_IR_ASSUME_POSITIVE | PHY_IR_ASSUME_NEGATIVE |    \
     PHY_IR_ASSUME_INTEGER | PHY_IR_ASSUME_NONZERO | PHY_IR_ASSUME_CONSTANT |  \
     PHY_IR_ASSUME_NONCOMMUTATIVE)

phy_status phy_ir_assume(phy_ir_context *ctx, phy_ir_symbol sym,
                         uint32_t assumptions)
{
    if (ctx == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_ir_symbol_at(ctx, sym) == NULL ||
        (assumptions & ~(uint32_t)PHY_IR_ASSUME_ALL) != 0u) {
        return phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
    }

    const uint32_t merged = symbols_of(ctx)[sym].assumptions | assumptions;

    /* The IR checks only what is self-contradictory on its face. Deciding
       whether an assumption is true of an expression is the rewriter's job. */
    const bool signed_both =
        (merged & (uint32_t)PHY_IR_ASSUME_POSITIVE) != 0u &&
        (merged & (uint32_t)PHY_IR_ASSUME_NEGATIVE) != 0u;
    if (signed_both) {
        return phy_ir_set_error(ctx, PHY_ERR_ASSUMPTION);
    }

    symbols_of(ctx)[sym].assumptions = merged;
    return PHY_OK;
}

uint32_t phy_ir_assumptions(const phy_ir_context *ctx, phy_ir_symbol sym)
{
    const phy_ir_symbol_record *record = phy_ir_symbol_at(ctx, sym);
    return (record != NULL) ? record->assumptions : 0u;
}

phy_status phy_ir_declare_symmetry(phy_ir_context *ctx, phy_ir_symbol sym,
                                   uint32_t slot_a, uint32_t slot_b,
                                   phy_ir_symmetry symmetry)
{
    if (ctx == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_ir_symbol_at(ctx, sym) == NULL || slot_a == slot_b ||
        symmetry == PHY_IR_SYMMETRY_NONE ||
        (unsigned)symmetry > (unsigned)PHY_IR_SYMMETRY_ANTISYMMETRIC) {
        return phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
    }

    /* Normalized so that a pair is stored once regardless of argument order. */
    const uint32_t low = (slot_a < slot_b) ? slot_a : slot_b;
    const uint32_t high = (slot_a < slot_b) ? slot_b : slot_a;

    phy_ir_symmetry_record *records =
        (phy_ir_symmetry_record *)ctx->symmetries.data;
    for (uint32_t at = symbols_of(ctx)[sym].symmetry_head; at != 0u;
         at = records[at].next) {
        if (records[at].slot_a == low && records[at].slot_b == high) {
            /* Redeclaring the same pair differently is a contradiction: a slot
               pair cannot both commute and anticommute. */
            return (records[at].symmetry == (uint8_t)symmetry)
                       ? PHY_OK
                       : phy_ir_set_error(ctx, PHY_ERR_ASSUMPTION);
        }
    }

    phy_ir_symmetry_record record;
    memset(&record, 0, sizeof record);
    record.slot_a = low;
    record.slot_b = high;
    record.symmetry = (uint8_t)symmetry;
    record.next = symbols_of(ctx)[sym].symmetry_head;

    const size_t index = pool_push(ctx, &ctx->symmetries, sizeof record, &record);
    if (index == (size_t)-1) {
        return ctx->error;
    }
    symbols_of(ctx)[sym].symmetry_head = (uint32_t)index;
    return PHY_OK;
}

phy_ir_symmetry phy_ir_slot_symmetry(const phy_ir_context *ctx,
                                     phy_ir_symbol sym, uint32_t slot_a,
                                     uint32_t slot_b)
{
    const phy_ir_symbol_record *record = phy_ir_symbol_at(ctx, sym);
    if (record == NULL || slot_a == slot_b) {
        return PHY_IR_SYMMETRY_NONE;
    }
    const uint32_t low = (slot_a < slot_b) ? slot_a : slot_b;
    const uint32_t high = (slot_a < slot_b) ? slot_b : slot_a;

    const phy_ir_symmetry_record *records =
        (const phy_ir_symmetry_record *)ctx->symmetries.data;
    for (uint32_t at = record->symmetry_head; at != 0u; at = records[at].next) {
        if (records[at].slot_a == low && records[at].slot_b == high) {
            return (phy_ir_symmetry)records[at].symmetry;
        }
    }
    return PHY_IR_SYMMETRY_NONE;
}

/* --------------------------------------------------------------- interning */

static void node_rehash(phy_ir_context *ctx)
{
    const size_t buckets = ctx->node_buckets.count;
    if (ctx->nodes.count <= buckets - buckets / 4u) {
        return;
    }
    const phy_status saved = ctx->error; /* see symbol_rehash */
    phy_pool grown = {NULL, 0u, 0u};
    if (!buckets_init(ctx, &grown, buckets * 2u)) {
        ctx->error = saved;
        return;
    }
    phy_ir_ref *table = refs_of(&grown);
    phy_ir_node *nodes = nodes_of(ctx);
    for (size_t i = 1u; i < ctx->nodes.count; i++) {
        const size_t slot = nodes[i].hash & (grown.count - 1u);
        nodes[i].intern_next = table[slot];
        table[slot] = (phy_ir_ref)i;
    }
    phy_ir_pool_release(ctx, &ctx->node_buckets, sizeof(phy_ir_ref));
    ctx->node_buckets = grown;
}

static bool same_payload(const phy_ir_context *ctx, const phy_ir_node *stored,
                         const phy_ir_node *proto, const phy_ir_ref *children,
                         const phy_ir_rat *rat)
{
    if (stored->kind != proto->kind || stored->aux != proto->aux ||
        stored->head != proto->head ||
        stored->child_count != proto->child_count) {
        return false;
    }

    switch ((phy_ir_kind)stored->kind) {
    case PHY_IR_INTEGER:
        return stored->u.integer == proto->u.integer;
    case PHY_IR_REAL:
        /* Bit comparison, not ==. Construction already folded -0.0 to +0.0
           and rejected NaN, so distinct bits now mean distinct values. */
        return memcmp(&stored->u.real, &proto->u.real, sizeof(double)) == 0;
    case PHY_IR_RATIONAL: {
        const phy_ir_rat *held = phy_ir_rat_at(ctx, stored->u.rational_index);
        return held != NULL && rat != NULL &&
               held->numerator == rat->numerator &&
               held->denominator == rat->denominator;
    }
    default:
        break;
    }

    if (stored->child_count == 0u) {
        return true;
    }
    return memcmp(&refs_of(&ctx->children)[stored->u.child_offset], children,
                  (size_t)stored->child_count * sizeof(phy_ir_ref)) == 0;
}

/*
 * The single entry point for node creation. `proto` carries kind, aux, head,
 * depth, child_count, hash, and any inline payload; `children` and `rat` carry
 * what does not fit inline.
 */
static phy_ir_ref intern_node(phy_ir_context *ctx, const phy_ir_node *proto,
                              const phy_ir_ref *children, const phy_ir_rat *rat)
{
    const size_t slot = proto->hash & (ctx->node_buckets.count - 1u);
    for (phy_ir_ref ref = refs_of(&ctx->node_buckets)[slot]; ref != PHY_IR_NULL;
         ref = nodes_of(ctx)[ref].intern_next) {
        const phy_ir_node *stored = &nodes_of(ctx)[ref];
        if (stored->hash == proto->hash &&
            same_payload(ctx, stored, proto, children, rat)) {
            return ref;
        }
    }

    if (phy_ir_node_count(ctx) >= ctx->limits.max_nodes) {
        phy_ir_set_error(ctx, PHY_ERR_NODE_LIMIT);
        return PHY_IR_NULL;
    }

    /*
     * A node's payload is appended to its pool before the node that owns it
     * exists, so both marks are taken now and restored if publishing fails.
     * Without this, a rational or a run of child slots would remain with no
     * node pointing at them: not corruption, but entries nothing can reach
     * and nothing will reuse.
     */
    const size_t rationals_mark = ctx->rationals.count;
    const size_t children_mark = ctx->children.count;

    phy_ir_node node = *proto;

    if (rat != NULL) {
        const size_t index =
            pool_push(ctx, &ctx->rationals, sizeof *rat, rat);
        if (index == (size_t)-1) {
            ctx->rationals.count = rationals_mark;
            return PHY_IR_NULL;
        }
        node.u.rational_index = (uint32_t)index;
    } else if (proto->child_count != 0u) {
        const size_t count = proto->child_count;
        if (!phy_ir_pool_reserve(ctx, &ctx->children, sizeof(phy_ir_ref),
                          ctx->children.count + count)) {
            return PHY_IR_NULL;
        }
        node.u.child_offset = (uint32_t)ctx->children.count;
        memcpy(&refs_of(&ctx->children)[ctx->children.count], children,
               count * sizeof(phy_ir_ref));
        ctx->children.count += count;
    }

    node.intern_next = refs_of(&ctx->node_buckets)[slot];
    const size_t index = pool_push(ctx, &ctx->nodes, sizeof node, &node);
    if (index == (size_t)-1) {
        /* Counts only; capacity keeps its peak, already charged and reusable. */
        ctx->rationals.count = rationals_mark;
        ctx->children.count = children_mark;
        return PHY_IR_NULL;
    }
    refs_of(&ctx->node_buckets)[slot] = (phy_ir_ref)index;
    node_rehash(ctx);
    return (phy_ir_ref)index;
}

/* ------------------------------------------------------------ atom builders */

static phy_ir_ref make_integer(phy_ir_context *ctx, int64_t value)
{
    phy_ir_node proto;
    memset(&proto, 0, sizeof proto);
    proto.kind = (uint8_t)PHY_IR_INTEGER;
    proto.depth = 1u;
    proto.u.integer = value;
    proto.hash = hash_bytes(hash_u32(FNV_OFFSET, (uint32_t)PHY_IR_INTEGER),
                            &value, sizeof value);
    return intern_node(ctx, &proto, NULL, NULL);
}

phy_ir_ref phy_ir_integer(phy_ir_context *ctx, int64_t value)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    return make_integer(ctx, value);
}

static uint64_t magnitude(int64_t value)
{
    /* Unsigned negation is defined for every input, including INT64_MIN. */
    return (value < 0) ? (~(uint64_t)value + 1u) : (uint64_t)value;
}

static uint64_t gcd_u64(uint64_t a, uint64_t b)
{
    while (b != 0u) {
        const uint64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

phy_ir_ref phy_ir_rational(phy_ir_context *ctx, int64_t numerator,
                           int64_t denominator)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    if (denominator == 0) {
        phy_ir_set_error(ctx, PHY_ERR_DOMAIN);
        return PHY_IR_NULL;
    }
    if (numerator == 0) {
        return make_integer(ctx, 0);
    }

    const bool negative = (numerator < 0) != (denominator < 0);
    uint64_t n = magnitude(numerator);
    uint64_t d = magnitude(denominator);
    const uint64_t divisor = gcd_u64(n, d);
    n /= divisor;
    d /= divisor;

    /* Only INT64_MIN over an even denominator can still be out of range here,
       and only on the numerator side. */
    const uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u
                                    : (uint64_t)INT64_MAX;
    if (n > limit || d > (uint64_t)INT64_MAX) {
        phy_ir_set_error(ctx, PHY_ERR_OVERFLOW);
        return PHY_IR_NULL;
    }

    int64_t signed_numerator;
    if (negative) {
        signed_numerator = (n == (uint64_t)INT64_MAX + 1u) ? INT64_MIN
                                                           : -(int64_t)n;
    } else {
        signed_numerator = (int64_t)n;
    }

    if (d == 1u) {
        return make_integer(ctx, signed_numerator);
    }

    const phy_ir_rat rat = {signed_numerator, (int64_t)d};
    phy_ir_node proto;
    memset(&proto, 0, sizeof proto);
    proto.kind = (uint8_t)PHY_IR_RATIONAL;
    proto.depth = 1u;
    proto.hash = hash_bytes(hash_u32(FNV_OFFSET, (uint32_t)PHY_IR_RATIONAL),
                            &rat, sizeof rat);
    return intern_node(ctx, &proto, NULL, &rat);
}

phy_ir_ref phy_ir_real(phy_ir_context *ctx, double value)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }

    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);

    /* An all-ones exponent is an infinity or a NaN. Testing the bits avoids a
       libm dependency, which on the device is measured in kilobytes. */
    if (((bits >> 52) & 0x7ffu) == 0x7ffu) {
        phy_ir_set_error(ctx, PHY_ERR_DOMAIN);
        return PHY_IR_NULL;
    }
    /* Negative zero folds to positive zero so the two do not intern apart. */
    if (bits == 0x8000000000000000u) {
        bits = 0u;
        memcpy(&value, &bits, sizeof value);
    }

    phy_ir_node proto;
    memset(&proto, 0, sizeof proto);
    proto.kind = (uint8_t)PHY_IR_REAL;
    proto.depth = 1u;
    proto.u.real = value;
    proto.hash = hash_bytes(hash_u32(FNV_OFFSET, (uint32_t)PHY_IR_REAL), &bits,
                            sizeof bits);
    return intern_node(ctx, &proto, NULL, NULL);
}

static phy_ir_ref make_headed_atom(phy_ir_context *ctx, phy_ir_kind kind,
                                   phy_ir_symbol head, uint8_t aux)
{
    const phy_ir_symbol_record *record = phy_ir_symbol_at(ctx, head);
    if (record == NULL) {
        phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        return PHY_IR_NULL;
    }

    phy_ir_node proto;
    memset(&proto, 0, sizeof proto);
    proto.kind = (uint8_t)kind;
    proto.aux = aux;
    proto.depth = 1u;
    proto.head = head;
    /* Hashed by the name, not the id: ids depend on interning order, so a
       hash over them would differ between contexts holding equal structure. */
    proto.hash = hash_u32(hash_u32(hash_u32(FNV_OFFSET, (uint32_t)kind),
                                   record->name_hash),
                          (uint32_t)aux);
    return intern_node(ctx, &proto, NULL, NULL);
}

phy_ir_ref phy_ir_symbol_ref(phy_ir_context *ctx, phy_ir_symbol sym)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    return make_headed_atom(ctx, PHY_IR_SYMBOL, sym, 0u);
}

phy_ir_ref phy_ir_index(phy_ir_context *ctx, phy_ir_symbol name,
                        phy_ir_variance variance)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    if ((unsigned)variance > (unsigned)PHY_IR_INDEX_UPPER) {
        phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        return PHY_IR_NULL;
    }
    return make_headed_atom(ctx, PHY_IR_INDEX, name, (uint8_t)variance);
}

phy_ir_ref phy_ir_error(phy_ir_context *ctx, phy_status code)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    if (code == PHY_OK || (unsigned)code >= (unsigned)PHY_STATUS_COUNT) {
        phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        return PHY_IR_NULL;
    }

    phy_ir_node proto;
    memset(&proto, 0, sizeof proto);
    proto.kind = (uint8_t)PHY_IR_ERROR;
    proto.aux = (uint8_t)code;
    proto.depth = 1u;
    proto.hash = hash_u32(hash_u32(FNV_OFFSET, (uint32_t)PHY_IR_ERROR),
                          (uint32_t)code);
    return intern_node(ctx, &proto, NULL, NULL);
}

/* -------------------------------------------------------- compound builders */

static bool scratch_reserve(phy_ir_context *ctx, size_t count)
{
    return phy_ir_pool_reserve(ctx, &ctx->scratch, sizeof(phy_ir_ref), count);
}

/*
 * Validate operands and compute the depth the parent will have.
 * Returns false with the error already recorded.
 */
static bool operands_ok(phy_ir_context *ctx, const phy_ir_ref *refs,
                        size_t count, uint16_t *out_depth)
{
    if (count > ctx->limits.max_children) {
        phy_ir_set_error(ctx, PHY_ERR_TERM_LIMIT);
        return false;
    }

    uint16_t deepest = 0u;
    for (size_t i = 0; i < count; i++) {
        const phy_ir_node *child = phy_ir_node_at(ctx, refs[i]);
        if (child == NULL) {
            /* Covers PHY_IR_NULL, so a failed inner builder surfaces here
               rather than producing a node with a hole in it. */
            phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
            return false;
        }
        if (child->depth > deepest) {
            deepest = child->depth;
        }
    }

    if ((uint32_t)deepest + 1u > ctx->limits.max_depth) {
        phy_ir_set_error(ctx, PHY_ERR_DEPTH_LIMIT);
        return false;
    }
    *out_depth = (uint16_t)(deepest + 1u);
    return true;
}

static phy_ir_ref make_compound(phy_ir_context *ctx, phy_ir_kind kind,
                                phy_ir_symbol head, const phy_ir_ref *children,
                                size_t count)
{
    uint16_t depth = 1u;
    if (!operands_ok(ctx, children, count, &depth)) {
        return PHY_IR_NULL;
    }

    uint32_t name_hash = 0u;
    if ((kKindInfo[kind].flags & PHY_IR_KIND_HEADED) != 0u) {
        const phy_ir_symbol_record *record = phy_ir_symbol_at(ctx, head);
        if (record == NULL) {
            phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
            return PHY_IR_NULL;
        }
        name_hash = record->name_hash;
    } else {
        head = PHY_IR_NO_SYMBOL;
    }

    uint32_t hash = hash_u32(hash_u32(FNV_OFFSET, (uint32_t)kind), name_hash);
    hash = hash_u32(hash, (uint32_t)count);
    for (size_t i = 0; i < count; i++) {
        /* Child hashes, not child refs: the result must be reproducible in a
           different context that built the same structure. */
        hash = hash_u32(hash, phy_ir_node_at(ctx, children[i])->hash);
    }

    phy_ir_node proto;
    memset(&proto, 0, sizeof proto);
    proto.kind = (uint8_t)kind;
    proto.depth = depth;
    proto.child_count = (uint16_t)count;
    proto.head = head;
    proto.hash = hash;
    return intern_node(ctx, &proto, children, NULL);
}

/*
 * Stage operands into ctx->scratch, flattening nested same-kind children for
 * associative kinds and sorting for commutative ones.
 *
 * Flattening one level suffices: every child was itself built here, so no ADD
 * ever contains an ADD, and the invariant is inductive.
 */
static phy_ir_ref make_nary(phy_ir_context *ctx, phy_ir_kind kind,
                            const phy_ir_ref *operands, size_t count)
{
    const uint32_t flags = kKindInfo[kind].flags;

    if (operands == NULL || count == 0u) {
        phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        return PHY_IR_NULL;
    }

    size_t total = 0u;
    for (size_t i = 0; i < count; i++) {
        const phy_ir_node *child = phy_ir_node_at(ctx, operands[i]);
        if (child == NULL) {
            phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
            return PHY_IR_NULL;
        }
        const bool absorb = (flags & PHY_IR_KIND_ASSOCIATIVE) != 0u &&
                            child->kind == (uint8_t)kind;
        total += absorb ? (size_t)child->child_count : 1u;
    }
    if (total > ctx->limits.max_children) {
        phy_ir_set_error(ctx, PHY_ERR_TERM_LIMIT);
        return PHY_IR_NULL;
    }
    if (!scratch_reserve(ctx, total)) {
        return PHY_IR_NULL;
    }

    phy_ir_ref *staged = refs_of(&ctx->scratch);
    size_t at = 0u;
    for (size_t i = 0; i < count; i++) {
        const phy_ir_node *child = phy_ir_node_at(ctx, operands[i]);
        if ((flags & PHY_IR_KIND_ASSOCIATIVE) != 0u &&
            child->kind == (uint8_t)kind) {
            const phy_ir_ref *inner = phy_ir_children_of(ctx, child);
            for (uint16_t j = 0; j < child->child_count; j++) {
                staged[at++] = inner[j];
            }
        } else {
            staged[at++] = operands[i];
        }
    }

    if ((flags & PHY_IR_KIND_COMMUTATIVE) != 0u) {
        phy_ir_sort_refs(ctx, staged, at);
    }

    /* A single operand is that operand. Wrapping it would make add(x) and x
       distinct nodes for the same expression, which interning exists to
       prevent. */
    if (at == 1u && (flags & PHY_IR_KIND_ASSOCIATIVE) != 0u) {
        return staged[0];
    }
    return make_compound(ctx, kind, PHY_IR_NO_SYMBOL, staged, at);
}

phy_ir_ref phy_ir_add(phy_ir_context *ctx, const phy_ir_ref *terms,
                      size_t count)
{
    return (ctx != NULL) ? make_nary(ctx, PHY_IR_ADD, terms, count)
                         : PHY_IR_NULL;
}

phy_ir_ref phy_ir_mul(phy_ir_context *ctx, const phy_ir_ref *factors,
                      size_t count)
{
    return (ctx != NULL) ? make_nary(ctx, PHY_IR_MUL, factors, count)
                         : PHY_IR_NULL;
}

phy_ir_ref phy_ir_ncmul(phy_ir_context *ctx, const phy_ir_ref *factors,
                        size_t count)
{
    return (ctx != NULL) ? make_nary(ctx, PHY_IR_NCMUL, factors, count)
                         : PHY_IR_NULL;
}

phy_ir_ref phy_ir_wedge(phy_ir_context *ctx, const phy_ir_ref *factors,
                        size_t count)
{
    return (ctx != NULL) ? make_nary(ctx, PHY_IR_WEDGE, factors, count)
                         : PHY_IR_NULL;
}

phy_ir_ref phy_ir_pow(phy_ir_context *ctx, phy_ir_ref base, phy_ir_ref exponent)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    const phy_ir_ref operands[2] = {base, exponent};
    return make_compound(ctx, PHY_IR_POW, PHY_IR_NO_SYMBOL, operands, 2u);
}

phy_ir_ref phy_ir_equation(phy_ir_context *ctx, phy_ir_ref lhs, phy_ir_ref rhs)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    const phy_ir_ref operands[2] = {lhs, rhs};
    return make_compound(ctx, PHY_IR_EQUATION, PHY_IR_NO_SYMBOL, operands, 2u);
}

phy_ir_ref phy_ir_function(phy_ir_context *ctx, phy_ir_symbol head,
                           const phy_ir_ref *args, size_t count)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    if (args == NULL && count != 0u) {
        phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        return PHY_IR_NULL;
    }
    return make_compound(ctx, PHY_IR_FUNCTION, head, args, count);
}

phy_ir_ref phy_ir_tensor(phy_ir_context *ctx, phy_ir_symbol head,
                         const phy_ir_ref *indices, size_t count)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    if (indices == NULL && count != 0u) {
        phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        return PHY_IR_NULL;
    }
    /* The type check is the point of a typed IR: a tensor slot holds an index,
       never an arbitrary expression. */
    for (size_t i = 0; i < count; i++) {
        if (phy_ir_kind_of(ctx, indices[i]) != PHY_IR_INDEX) {
            phy_ir_set_error(ctx, PHY_ERR_TYPE);
            return PHY_IR_NULL;
        }
    }
    return make_compound(ctx, PHY_IR_TENSOR, head, indices, count);
}

phy_ir_ref phy_ir_operator(phy_ir_context *ctx, phy_ir_symbol head,
                           const phy_ir_ref *args, size_t count)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    if (args == NULL && count != 0u) {
        phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        return PHY_IR_NULL;
    }
    return make_compound(ctx, PHY_IR_OPERATOR, head, args, count);
}

phy_ir_ref phy_ir_derivative(phy_ir_context *ctx, phy_ir_ref expr,
                             const phy_ir_ref *vars, size_t count)
{
    if (ctx == NULL) {
        return PHY_IR_NULL;
    }
    if (vars == NULL || count == 0u) {
        phy_ir_set_error(ctx, PHY_ERR_INVALID_ARGUMENT);
        return PHY_IR_NULL;
    }
    for (size_t i = 0; i < count; i++) {
        const phy_ir_kind kind = phy_ir_kind_of(ctx, vars[i]);
        if (kind != PHY_IR_SYMBOL && kind != PHY_IR_INDEX) {
            phy_ir_set_error(ctx, PHY_ERR_TYPE);
            return PHY_IR_NULL;
        }
    }
    if (count + 1u > ctx->limits.max_children) {
        phy_ir_set_error(ctx, PHY_ERR_TERM_LIMIT);
        return PHY_IR_NULL;
    }
    if (!scratch_reserve(ctx, count + 1u)) {
        return PHY_IR_NULL;
    }

    phy_ir_ref *staged = refs_of(&ctx->scratch);
    staged[0] = expr;
    memcpy(&staged[1], vars, count * sizeof(phy_ir_ref));
    return make_compound(ctx, PHY_IR_DERIVATIVE, PHY_IR_NO_SYMBOL, staged,
                         count + 1u);
}

/* --------------------------------------------------------- node accessors */

phy_ir_kind phy_ir_kind_of(const phy_ir_context *ctx, phy_ir_ref ref)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    return (node != NULL) ? (phy_ir_kind)node->kind : PHY_IR_KIND_INVALID;
}

size_t phy_ir_child_count(const phy_ir_context *ctx, phy_ir_ref ref)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    return (node != NULL) ? (size_t)node->child_count : 0u;
}

phy_ir_ref phy_ir_child(const phy_ir_context *ctx, phy_ir_ref ref,
                        size_t position)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    if (node == NULL || position >= (size_t)node->child_count) {
        return PHY_IR_NULL;
    }
    return phy_ir_children_of(ctx, node)[position];
}

phy_ir_symbol phy_ir_head(const phy_ir_context *ctx, phy_ir_ref ref)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    return (node != NULL) ? node->head : PHY_IR_NO_SYMBOL;
}

bool phy_ir_integer_value(const phy_ir_context *ctx, phy_ir_ref ref,
                          int64_t *out_value)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    if (node == NULL || node->kind != (uint8_t)PHY_IR_INTEGER ||
        out_value == NULL) {
        return false;
    }
    *out_value = node->u.integer;
    return true;
}

bool phy_ir_rational_value(const phy_ir_context *ctx, phy_ir_ref ref,
                           int64_t *out_numerator, int64_t *out_denominator)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    if (node == NULL || node->kind != (uint8_t)PHY_IR_RATIONAL ||
        out_numerator == NULL || out_denominator == NULL) {
        return false;
    }
    const phy_ir_rat *rat = phy_ir_rat_at(ctx, node->u.rational_index);
    if (rat == NULL) {
        return false;
    }
    *out_numerator = rat->numerator;
    *out_denominator = rat->denominator;
    return true;
}

bool phy_ir_real_value(const phy_ir_context *ctx, phy_ir_ref ref,
                       double *out_value)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    if (node == NULL || node->kind != (uint8_t)PHY_IR_REAL ||
        out_value == NULL) {
        return false;
    }
    *out_value = node->u.real;
    return true;
}

bool phy_ir_index_variance(const phy_ir_context *ctx, phy_ir_ref ref,
                           phy_ir_variance *out_variance)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    if (node == NULL || node->kind != (uint8_t)PHY_IR_INDEX ||
        out_variance == NULL) {
        return false;
    }
    *out_variance = (phy_ir_variance)node->aux;
    return true;
}

bool phy_ir_error_status(const phy_ir_context *ctx, phy_ir_ref ref,
                         phy_status *out_status)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    if (node == NULL || node->kind != (uint8_t)PHY_IR_ERROR ||
        out_status == NULL) {
        return false;
    }
    *out_status = (phy_status)node->aux;
    return true;
}

uint32_t phy_ir_hash(const phy_ir_context *ctx, phy_ir_ref ref)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    return (node != NULL) ? node->hash : 0u;
}

uint32_t phy_ir_depth(const phy_ir_context *ctx, phy_ir_ref ref)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    return (node != NULL) ? (uint32_t)node->depth : 0u;
}

/* --------------------------------------------------------------- integrity */

/*
 * Two properties, checked by counting rather than by walking:
 *
 *   nothing orphaned -- the bytes, child slots, and rationals the pools hold
 *   are exactly what the live symbols and nodes claim, so a failed
 *   construction cannot have left a payload behind;
 *
 *   nothing lost -- every symbol and node is reachable from its intern
 *   bucket, so a failed construction cannot have left an entry that the next
 *   identical request will fail to find and duplicate.
 */
phy_status phy_ir_validate(const phy_ir_context *ctx)
{
    if (ctx == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    /* Symbols own their name bytes, one NUL-terminated run each. */
    const phy_ir_symbol_record *symbols = symbols_of(ctx);
    size_t claimed_string_bytes = 0u;
    for (size_t i = 1u; i < ctx->symbols.count; i++) {
        const phy_ir_symbol_record *record = &symbols[i];
        if ((size_t)record->name_offset + record->name_length + 1u >
            ctx->strings.count) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        claimed_string_bytes += (size_t)record->name_length + 1u;
    }
    if (claimed_string_bytes != ctx->strings.count) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    /* Nodes own their child slots and, for rationals, one pool entry each. */
    const phy_ir_node *nodes = nodes_of(ctx);
    size_t claimed_children = 0u;
    size_t claimed_rationals = 0u;
    for (size_t i = 1u; i < ctx->nodes.count; i++) {
        const phy_ir_node *node = &nodes[i];

        if (node->kind == (uint8_t)PHY_IR_RATIONAL) {
            if ((size_t)node->u.rational_index >= ctx->rationals.count) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
            claimed_rationals++;
        }

        if (node->child_count == 0u) {
            continue;
        }
        if ((size_t)node->u.child_offset + node->child_count >
            ctx->children.count) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        const phy_ir_ref *children = phy_ir_children_of(ctx, node);
        for (uint16_t c = 0; c < node->child_count; c++) {
            /* A child must be a real node, and one built before its parent. */
            if (children[c] == PHY_IR_NULL || (size_t)children[c] >= i) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
        }
        claimed_children += node->child_count;
    }
    if (claimed_children != ctx->children.count ||
        claimed_rationals != ctx->rationals.count) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    /* Every node reachable from exactly one bucket chain. */
    size_t reachable = 0u;
    for (size_t bucket = 0u; bucket < ctx->node_buckets.count; bucket++) {
        size_t guard = 0u;
        for (phy_ir_ref ref = refs_of(&ctx->node_buckets)[bucket];
             ref != PHY_IR_NULL; ref = nodes[ref].intern_next) {
            if ((size_t)ref >= ctx->nodes.count ||
                ++guard > ctx->nodes.count) {
                return PHY_ERR_CORRUPT_DOCUMENT; /* dangling or looped chain */
            }
            if ((nodes[ref].hash & (ctx->node_buckets.count - 1u)) != bucket) {
                return PHY_ERR_CORRUPT_DOCUMENT; /* filed under the wrong key */
            }
            reachable++;
        }
    }
    if (reachable != ctx->nodes.count - 1u) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    /* And every symbol. */
    reachable = 0u;
    for (size_t bucket = 0u; bucket < ctx->symbol_buckets.count; bucket++) {
        size_t guard = 0u;
        for (phy_ir_symbol sym =
                 ((const phy_ir_symbol *)ctx->symbol_buckets.data)[bucket];
             sym != PHY_IR_NO_SYMBOL; sym = symbols[sym].intern_next) {
            if ((size_t)sym >= ctx->symbols.count ||
                ++guard > ctx->symbols.count) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
            if ((symbols[sym].name_hash & (ctx->symbol_buckets.count - 1u)) !=
                bucket) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
            reachable++;
        }
    }
    if (reachable != ctx->symbols.count - 1u) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    /* Symmetry records are owned by exactly one symbol's list. */
    const phy_ir_symmetry_record *symmetries =
        (const phy_ir_symmetry_record *)ctx->symmetries.data;
    size_t owned = 0u;
    for (size_t i = 1u; i < ctx->symbols.count; i++) {
        size_t guard = 0u;
        for (uint32_t at = symbols[i].symmetry_head; at != 0u;
             at = symmetries[at].next) {
            if ((size_t)at >= ctx->symmetries.count ||
                ++guard > ctx->symmetries.count) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
            owned++;
        }
    }
    if (owned != ctx->symmetries.count - 1u) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    return PHY_OK;
}
