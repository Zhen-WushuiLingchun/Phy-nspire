/*
 * Phy-nspire — expression IR internals.
 *
 * Shared by the src/ir translation units only. Nothing outside src/ir may
 * include this header: the
 * public contract is include/phy/ir.h, and the node layout below is expected
 * to change as the tensor core lands.
 */
#ifndef PHY_IR_INTERNAL_H
#define PHY_IR_INTERNAL_H

#include "phy/ir.h"

/*
 * Hard ceiling on limits.max_depth.
 *
 * Comparison, serialization, and parsing all recurse once per level. The CX II
 * gives the application a small stack, so the depth limit is what keeps that
 * recursion inside it; a caller must not be able to raise the limit past the
 * point where a legal expression overflows the stack. 1024 levels at the ~64
 * bytes a frame costs here is roughly 64 KiB, which the device tolerates.
 */
#define PHY_IR_MAX_DEPTH_CEILING 1024u

/* Same reasoning applied to the fields that must fit the node layout. */
#define PHY_IR_MAX_CHILDREN_CEILING 65535u
#define PHY_IR_EXACT_BIG 0x80u

typedef struct {
    int64_t numerator;
    int64_t denominator; /* always > 1; a unit denominator becomes an integer */
} phy_ir_rat;

typedef struct {
    uint32_t numerator_offset;
    uint32_t numerator_length;
    uint32_t denominator_offset;
    uint32_t denominator_length; /* zero only for an integer */
} phy_ir_big_exact;

/*
 * 32 bytes on both targets. Refs are indices, so the pools below may move;
 * a phy_ir_node * is only valid until the next allocation.
 */
typedef struct {
    uint8_t kind;
    uint8_t aux;          /* variance for INDEX, phy_status for ERROR */
    uint16_t depth;       /* atoms are 1 */
    uint16_t child_count;
    uint16_t reserved;
    uint32_t hash;        /* structural, portable across contexts */
    uint32_t intern_next; /* bucket chain, PHY_IR_NULL terminates */
    uint32_t head;        /* phy_ir_symbol, or PHY_IR_NO_SYMBOL */
    union {
        uint32_t child_offset;   /* compound: start index in ctx->children */
        uint32_t rational_index; /* RATIONAL: index in ctx->rationals */
        uint32_t index_space;    /* INDEX: phy_ir_symbol or NO_SYMBOL */
        int64_t integer;         /* INTEGER */
        double real;             /* REAL */
    } u;
} phy_ir_node;

typedef struct {
    uint32_t name_offset; /* into ctx->strings */
    uint32_t name_length;
    uint32_t name_hash;
    uint32_t assumptions;
    uint32_t symmetry_head; /* into ctx->symmetries, 0 terminates */
    uint32_t intern_next;
} phy_ir_symbol_record;

typedef struct {
    uint32_t slot_a; /* always < slot_b, normalized on declaration */
    uint32_t slot_b;
    uint32_t next;
    uint8_t symmetry;
} phy_ir_symmetry_record;

/* Growable, phy_alloc-backed, charged against limits.max_bytes. */
typedef struct {
    void *data;
    size_t count;
    size_t capacity;
} phy_pool;

struct phy_ir_context {
    phy_ir_limits limits;
    phy_status error; /* sticky; first failure wins */
    size_t bytes;     /* live pool bytes, charged against limits.max_bytes */

    /* Element 0 of nodes, symbols, and symmetries is a reserved sentinel so
       that PHY_IR_NULL and PHY_IR_NO_SYMBOL are never valid handles. */
    phy_pool nodes;      /* phy_ir_node */
    phy_pool children;   /* phy_ir_ref */
    phy_pool rationals;  /* phy_ir_rat */
    phy_pool big_exacts; /* phy_ir_big_exact */
    phy_pool exact_text; /* canonical decimal bytes, no terminators */
    phy_pool symbols;    /* phy_ir_symbol_record */
    phy_pool strings;    /* char */
    phy_pool symmetries; /* phy_ir_symmetry_record */

    phy_pool node_buckets;   /* phy_ir_ref */
    phy_pool symbol_buckets; /* phy_ir_symbol */

    phy_pool scratch; /* phy_ir_ref, operand staging for the builders */
};

/* ---- shared helpers, defined in ir.c ---- */

/* Records `status` if no error is pending. Always returns `status`. */
phy_status phy_ir_set_error(phy_ir_context *ctx, phy_status status);

/*
 * Grow a pool to at least `needed` elements, charging the context budget.
 * Exposed so the text reader can own an operand stack on the same budget as
 * everything else rather than allocating behind the limits' back.
 */
bool phy_ir_pool_reserve(phy_ir_context *ctx, phy_pool *pool, size_t elem_size,
                         size_t needed);
void phy_ir_pool_release(phy_ir_context *ctx, phy_pool *pool, size_t elem_size);

/* Serialization token for a kind, or NULL when the kind is written
   literally rather than as a list -- integers and symbols. */
const char *phy_ir_kind_token(phy_ir_kind kind);

/* NULL for PHY_IR_NULL or an out-of-range ref. Invalidated by allocation. */
const phy_ir_node *phy_ir_node_at(const phy_ir_context *ctx, phy_ir_ref ref);

const phy_ir_symbol_record *phy_ir_symbol_at(const phy_ir_context *ctx,
                                             phy_ir_symbol sym);

const phy_ir_rat *phy_ir_rat_at(const phy_ir_context *ctx, uint32_t index);
const phy_ir_big_exact *phy_ir_big_exact_at(const phy_ir_context *ctx,
                                            uint32_t index);

/* Children of a compound node. Invalidated by allocation. */
const phy_ir_ref *phy_ir_children_of(const phy_ir_context *ctx,
                                     const phy_ir_node *node);

/*
 * Rank used to group kinds in the canonical order. Exact numbers sort before
 * inexact ones, atoms before compounds. Defined in ir.c beside the kind table
 * so a new kind cannot be added without placing it in the order.
 */
uint32_t phy_ir_kind_rank(phy_ir_kind kind);

/* ---- order.c ---- */

/*
 * Sort `count` refs into canonical order in place. Heapsort: no scratch, no
 * recursion, and O(n log n) even on the adversarial inputs a term limit of
 * several thousand would otherwise allow.
 */
void phy_ir_sort_refs(const phy_ir_context *ctx, phy_ir_ref *refs,
                      size_t count);

#endif /* PHY_IR_INTERNAL_H */
