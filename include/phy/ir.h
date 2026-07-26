/*
 * Phy-nspire — typed expression IR.
 *
 * Physics-facing code operates on this graph, never on backend strings. The
 * IR is backend-neutral by construction: nothing here knows that Giac exists.
 *
 * Four properties define it, per docs/ARCHITECTURE.md:
 *
 *   interning        structurally identical subtrees are one node, so
 *                    equality is `a == b` on a ref, not a tree walk;
 *   canonical order  commutative operands are sorted by a total order that
 *                    depends only on structure, never on creation order;
 *   explicit
 *   noncommutativity operator products are a distinct kind that preserves
 *                    operand order, so nothing silently commutes;
 *   bounded          every construction is checked against node, depth, term,
 *                    and byte ceilings, and fails as a typed status.
 *
 * Refs are indices into context-owned pools, not pointers. Pools move when
 * they grow, so a ref stays valid for the life of the context while a node
 * pointer does not. The API therefore never hands out node pointers.
 *
 * A context is not thread-safe and is not shared between documents.
 */
#ifndef PHY_IR_H
#define PHY_IR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- handles */

/*
 * A node handle. Stable for the life of its context.
 *
 * PHY_IR_NULL is the only invalid value and is what every builder returns on
 * failure; the reason is then available from phy_ir_last_error().
 */
typedef uint32_t phy_ir_ref;
#define PHY_IR_NULL ((phy_ir_ref)0)

/* An interned symbol name. PHY_IR_NO_SYMBOL is the invalid value. */
typedef uint32_t phy_ir_symbol;
#define PHY_IR_NO_SYMBOL ((phy_ir_symbol)0)

typedef struct phy_ir_context phy_ir_context;

/* ------------------------------------------------------------------ kinds */

/*
 * Node kinds.
 *
 * The machinery below -- hashing, interning, ordering, serialization -- is
 * driven by a descriptor table rather than by switch statements over this
 * enum, so a later phase adds a kind by adding a table row, not by touching
 * the core. See kKindInfo in src/ir/ir.c.
 *
 * The set here covers Phase 1 scalars and the structural vocabulary the
 * Phase 2 tensor core needs. Gamma matrices and SU(N) generators are
 * deliberately not separate kinds: both are headed noncommutative objects
 * carrying indices, which is exactly PHY_IR_OPERATOR. Giving each physics
 * object its own kind would multiply the table without adding structure.
 */
typedef enum {
    PHY_IR_KIND_INVALID = 0,

    /* Atoms. */
    PHY_IR_INTEGER,  /* exact int64 */
    PHY_IR_RATIONAL, /* exact int64 pair, reduced, denominator > 1 */
    PHY_IR_REAL,     /* double, for numeric fallback only */
    PHY_IR_SYMBOL,   /* named scalar; head is the name */
    PHY_IR_INDEX,    /* head is the name, aux is the variance */
    PHY_IR_ERROR,    /* a typed error carried as a value, aux is the status */

    /* Scalar structure. */
    PHY_IR_ADD,      /* commutative, associative, n-ary */
    PHY_IR_MUL,      /* commutative, associative, n-ary */
    PHY_IR_NCMUL,    /* noncommutative, associative, n-ary; order preserved */
    PHY_IR_POW,      /* base, exponent */
    PHY_IR_EQUATION, /* lhs, rhs */
    PHY_IR_FUNCTION, /* head is the name, children are ordered arguments */

    /* Physics structure. */
    PHY_IR_TENSOR,     /* head is the name, children are PHY_IR_INDEX */
    PHY_IR_OPERATOR,   /* headed, noncommutative; gamma matrices, generators */
    PHY_IR_DERIVATIVE, /* child 0 is the expression, the rest are variables */
    PHY_IR_WEDGE,      /* exterior product; ordered, sign is a rewrite */

    PHY_IR_KIND_COUNT
} phy_ir_kind;

/* Structural properties of a kind. Queried through phy_ir_kind_flags(). */
typedef enum {
    PHY_IR_KIND_ATOM = 1u << 0,        /* never has children */
    PHY_IR_KIND_COMMUTATIVE = 1u << 1, /* children are canonically sorted */
    PHY_IR_KIND_ASSOCIATIVE = 1u << 2, /* nested same-kind children flatten */
    PHY_IR_KIND_NARY = 1u << 3,        /* variable child count */
    PHY_IR_KIND_HEADED = 1u << 4       /* carries a symbol head */
} phy_ir_kind_flag;

/* Stable, allocation-free spelling of a kind. Never returns NULL. */
const char *phy_ir_kind_name(phy_ir_kind kind);
uint32_t phy_ir_kind_flags(phy_ir_kind kind);

/* Index position. Distinct variances are distinct nodes. */
typedef enum {
    PHY_IR_INDEX_LOWER = 0,
    PHY_IR_INDEX_UPPER = 1
} phy_ir_variance;

/* ------------------------------------------------------------- assumptions */

/*
 * Declared properties of a symbol. These are inputs to later simplification,
 * not facts the IR verifies; the IR only rejects combinations that cannot
 * both hold.
 */
typedef enum {
    PHY_IR_ASSUME_REAL = 1u << 0,
    PHY_IR_ASSUME_POSITIVE = 1u << 1,
    PHY_IR_ASSUME_NEGATIVE = 1u << 2,
    PHY_IR_ASSUME_INTEGER = 1u << 3,
    PHY_IR_ASSUME_NONZERO = 1u << 4,
    PHY_IR_ASSUME_CONSTANT = 1u << 5,
    PHY_IR_ASSUME_NONCOMMUTATIVE = 1u << 6
} phy_ir_assumption;

/* Declared slot symmetry of a tensor or operator head. */
typedef enum {
    PHY_IR_SYMMETRY_NONE = 0,
    PHY_IR_SYMMETRY_SYMMETRIC,
    PHY_IR_SYMMETRY_ANTISYMMETRIC
} phy_ir_symmetry;

/* ------------------------------------------------------------------ limits */

/*
 * Construction ceilings. Zero means "use the default" for each field
 * independently, so a caller can raise one limit without restating the rest.
 */
typedef struct {
    uint32_t max_nodes;    /* distinct interned nodes; default 200000 */
    uint32_t max_depth;    /* tree depth, root is 1; default 256 */
    uint32_t max_children; /* operands of one n-ary node; default 4096 */
    size_t max_bytes;      /* total pool bytes; default 8 MiB */
} phy_ir_limits;

void phy_ir_limits_defaults(phy_ir_limits *out_limits);

/* ---------------------------------------------------------------- context */

/*
 * Create a context. Passing NULL limits uses the defaults. Returns NULL if
 * the limits are invalid or the initial pools cannot be allocated.
 *
 * All memory is taken through phy_alloc/phy_free so that it appears in
 * phy_telemetry, and the total is additionally capped by limits.max_bytes.
 */
phy_ir_context *phy_ir_context_create(const phy_ir_limits *limits);
void phy_ir_context_destroy(phy_ir_context *ctx);

/*
 * The first error since the last phy_ir_clear_error(). Builders return
 * PHY_IR_NULL on failure; this says why. Sticky, so a caller may build a
 * whole expression and check once.
 */
phy_status phy_ir_last_error(const phy_ir_context *ctx);
void phy_ir_clear_error(phy_ir_context *ctx);

/* Live counts, for tests and for the size-report culture of this project. */
size_t phy_ir_node_count(const phy_ir_context *ctx);
size_t phy_ir_symbol_count(const phy_ir_context *ctx);
size_t phy_ir_bytes_used(const phy_ir_context *ctx);

/* ---------------------------------------------------------------- symbols */

/*
 * Intern a name. Returns the same id for the same bytes. Names are compared
 * and ordered as bytes, so ordering is locale-independent.
 */
phy_ir_symbol phy_ir_intern(phy_ir_context *ctx, const char *name);
phy_ir_symbol phy_ir_intern_n(phy_ir_context *ctx, const char *name,
                              size_t length);

/* Name of a symbol, NUL-terminated. NULL if the id is invalid. */
const char *phy_ir_symbol_name(const phy_ir_context *ctx, phy_ir_symbol sym);

/*
 * Add assumptions to a symbol. Assumptions accumulate. Returns
 * PHY_ERR_ASSUMPTION if the result would be contradictory -- positive and
 * negative together, or either with a declared zero -- and leaves the symbol
 * unchanged.
 */
phy_status phy_ir_assume(phy_ir_context *ctx, phy_ir_symbol sym,
                         uint32_t assumptions);
uint32_t phy_ir_assumptions(const phy_ir_context *ctx, phy_ir_symbol sym);

/*
 * Declare that slots a and b of this head commute (symmetric) or anticommute
 * (antisymmetric). Slots are zero-based child positions.
 *
 * The IR stores and reports these; it does not yet rewrite with them.
 * Canonicalizing indices under a symmetry group -- including the sign
 * bookkeeping antisymmetry needs -- is Phase 2 work and belongs in the
 * rewriter, not in construction.
 */
phy_status phy_ir_declare_symmetry(phy_ir_context *ctx, phy_ir_symbol sym,
                                   uint32_t slot_a, uint32_t slot_b,
                                   phy_ir_symmetry symmetry);
phy_ir_symmetry phy_ir_slot_symmetry(const phy_ir_context *ctx,
                                     phy_ir_symbol sym, uint32_t slot_a,
                                     uint32_t slot_b);

/* --------------------------------------------------------------- builders */

/*
 * Every builder interns: building the same structure twice returns the same
 * ref. Every builder returns PHY_IR_NULL on failure and sets the sticky
 * error. A PHY_IR_NULL operand propagates as PHY_ERR_INVALID_ARGUMENT, so an
 * unchecked chain of builders fails at the end rather than corrupting a tree.
 */

phy_ir_ref phy_ir_integer(phy_ir_context *ctx, int64_t value);

/*
 * Exact rational. Normalizes: the sign moves to the numerator, the fraction
 * is reduced, and a unit denominator yields a PHY_IR_INTEGER instead. A zero
 * denominator is PHY_ERR_DOMAIN. Normalization is canonical form, not
 * simplification -- the IR performs no arithmetic beyond it.
 */
phy_ir_ref phy_ir_rational(phy_ir_context *ctx, int64_t numerator,
                           int64_t denominator);

/* Inexact fallback. NaN and infinities are PHY_ERR_DOMAIN. */
phy_ir_ref phy_ir_real(phy_ir_context *ctx, double value);

phy_ir_ref phy_ir_symbol_ref(phy_ir_context *ctx, phy_ir_symbol sym);
phy_ir_ref phy_ir_index(phy_ir_context *ctx, phy_ir_symbol name,
                        phy_ir_variance variance);

/* A typed error as a value. `code` must be an error, not PHY_OK. */
phy_ir_ref phy_ir_error(phy_ir_context *ctx, phy_status code);

/*
 * n-ary builders. `count` of 0 is PHY_ERR_INVALID_ARGUMENT; a single operand
 * returns that operand unchanged for associative kinds, which keeps
 * `add(x) == x` rather than creating a one-term sum.
 */
phy_ir_ref phy_ir_add(phy_ir_context *ctx, const phy_ir_ref *terms,
                      size_t count);
phy_ir_ref phy_ir_mul(phy_ir_context *ctx, const phy_ir_ref *factors,
                      size_t count);
phy_ir_ref phy_ir_ncmul(phy_ir_context *ctx, const phy_ir_ref *factors,
                        size_t count);
phy_ir_ref phy_ir_wedge(phy_ir_context *ctx, const phy_ir_ref *factors,
                        size_t count);

phy_ir_ref phy_ir_pow(phy_ir_context *ctx, phy_ir_ref base,
                      phy_ir_ref exponent);
phy_ir_ref phy_ir_equation(phy_ir_context *ctx, phy_ir_ref lhs,
                           phy_ir_ref rhs);

phy_ir_ref phy_ir_function(phy_ir_context *ctx, phy_ir_symbol head,
                           const phy_ir_ref *args, size_t count);

/* Children must all be PHY_IR_INDEX; anything else is PHY_ERR_TYPE. */
phy_ir_ref phy_ir_tensor(phy_ir_context *ctx, phy_ir_symbol head,
                         const phy_ir_ref *indices, size_t count);

phy_ir_ref phy_ir_operator(phy_ir_context *ctx, phy_ir_symbol head,
                           const phy_ir_ref *args, size_t count);

/*
 * Differentiate `expr` with respect to `vars`, in order. Variables must be
 * PHY_IR_SYMBOL or PHY_IR_INDEX.
 *
 * The order is preserved rather than sorted. Mixed partials commute only for
 * sufficiently smooth expressions, which is an assumption the rewriter can
 * apply and construction cannot.
 */
phy_ir_ref phy_ir_derivative(phy_ir_context *ctx, phy_ir_ref expr,
                             const phy_ir_ref *vars, size_t count);

/* -------------------------------------------------------------- accessors */

/* PHY_IR_KIND_INVALID for PHY_IR_NULL or an out-of-range ref. */
phy_ir_kind phy_ir_kind_of(const phy_ir_context *ctx, phy_ir_ref ref);
size_t phy_ir_child_count(const phy_ir_context *ctx, phy_ir_ref ref);
phy_ir_ref phy_ir_child(const phy_ir_context *ctx, phy_ir_ref ref,
                        size_t position);
phy_ir_symbol phy_ir_head(const phy_ir_context *ctx, phy_ir_ref ref);

bool phy_ir_integer_value(const phy_ir_context *ctx, phy_ir_ref ref,
                          int64_t *out_value);
bool phy_ir_rational_value(const phy_ir_context *ctx, phy_ir_ref ref,
                           int64_t *out_numerator, int64_t *out_denominator);
bool phy_ir_real_value(const phy_ir_context *ctx, phy_ir_ref ref,
                       double *out_value);
bool phy_ir_index_variance(const phy_ir_context *ctx, phy_ir_ref ref,
                           phy_ir_variance *out_variance);
bool phy_ir_error_status(const phy_ir_context *ctx, phy_ir_ref ref,
                         phy_status *out_status);

/* Structural hash. Equal structure implies an equal hash. */
uint32_t phy_ir_hash(const phy_ir_context *ctx, phy_ir_ref ref);

/* Tree depth; an atom is 1. Bounded by limits.max_depth at construction. */
uint32_t phy_ir_depth(const phy_ir_context *ctx, phy_ir_ref ref);

/*
 * Structural equality. Interning makes this exact ref comparison; it exists
 * so call sites read as intent rather than as a pointer trick.
 */
static inline bool phy_ir_equal(phy_ir_ref a, phy_ir_ref b)
{
    return a == b && a != PHY_IR_NULL;
}

/*
 * Total order over all nodes of one context. Negative, zero, or positive.
 *
 * Depends only on structure -- kind, symbol name bytes, numeric value,
 * children -- so two contexts that built the same expressions by different
 * routes agree. This is the order commutative operands are stored in.
 */
int phy_ir_compare(const phy_ir_context *ctx, phy_ir_ref a, phy_ir_ref b);

/* ----------------------------------------------------------- serialization */

/*
 * Backend-neutral text. The grammar is a small S-expression form; see
 * docs/IR.md. It carries structure only, so a reader in another context
 * rebuilds an identical graph:
 *
 *     phy_ir_read(other, phy_ir_write(ctx, e))  ==  the same structure
 *
 * Writing follows snprintf conventions: *out_length always receives the byte
 * count the text needs, excluding the terminating NUL. PHY_OK means it fit.
 * PHY_ERR_INVALID_ARGUMENT with *out_length set means the buffer was too
 * small and the caller should retry with that size plus one.
 */
phy_status phy_ir_write(const phy_ir_context *ctx, phy_ir_ref ref, char *buffer,
                        size_t capacity, size_t *out_length);

/*
 * Parse one expression. Trailing content after it is PHY_ERR_PARSE.
 *
 * `out_error_offset` may be NULL; otherwise it receives the byte offset the
 * parse stopped at, which is what a cell-level diagnostic needs.
 */
phy_status phy_ir_read(phy_ir_context *ctx, const char *text,
                       phy_ir_ref *out_ref, size_t *out_error_offset);

/*
 * Symbol declarations -- assumptions and symmetries -- as text. Expressions
 * do not carry them, because a symbol is shared by every expression that
 * mentions it; a document writes declarations once and then its cells.
 *
 * Same buffer conventions as phy_ir_write.
 */
phy_status phy_ir_write_declarations(const phy_ir_context *ctx, char *buffer,
                                     size_t capacity, size_t *out_length);
phy_status phy_ir_read_declarations(phy_ir_context *ctx, const char *text,
                                    size_t *out_error_offset);

#ifdef __cplusplus
}
#endif

#endif /* PHY_IR_H */
