/*
 * Phy-nspire — scalar CAS internals.
 *
 * Shared by the src/cas translation units only. Nothing outside src/cas may
 * include this header: the public contract is include/phy/cas.h.
 *
 * The layer is deliberately split the way src/ir is:
 *
 *   num.c       checked int64 rational fast path, no IR, no allocation;
 *   big_num.c   bounded arbitrary-precision promotion for exact IR atoms;
 *   engine.c    context, budget, memo cache, scratch arena, small predicates;
 *   simplify.c  the rewrite rules and the simplifying constructors;
 *   diff.c      differentiation and the dependence test it needs;
 *   normal.c    expansion, rational normal form, and the zero decision.
 *   reduce.c    LCD cancellation and bounded Q[x] polynomial algorithms.
 *
 * The recursive walks here recurse once per expression level, exactly as the
 * IR's comparison, serialization, and parsing do, and are bounded by the same
 * thing: phy_ir_limits.max_depth, itself clamped to 1024. No walk in this layer
 * may introduce recursion that is not bounded by input depth.
 */
#ifndef PHY_CAS_INTERNAL_H
#define PHY_CAS_INTERNAL_H

#include "phy/cas.h"

/* ------------------------------------------------------- exact rationals */

/*
 * An exact rational in flight. Reduced, denominator strictly positive, so
 * `den == 1` is an integer and the pair is unique for a value.
 *
 * The IR stores numbers as nodes; this is the allocation-free fast arithmetic
 * form used while every operand and result fits. Overflow here is a request to
 * use big_num.c's bounded promotion bridge, not permission to wrap.
 */
typedef struct {
    int64_t num;
    int64_t den; /* > 0 */
} phy_cas_rat;

/* All return false on overflow, leaving *out untouched. */
bool phy_cas_rat_add(phy_cas_rat a, phy_cas_rat b, phy_cas_rat *out);
bool phy_cas_rat_mul(phy_cas_rat a, phy_cas_rat b, phy_cas_rat *out);
bool phy_cas_rat_pow(phy_cas_rat base, int64_t exponent, phy_cas_rat *out);

/* Sign of a - value, where `value` is an integer. */
int phy_cas_rat_cmp_int(phy_cas_rat a, int64_t value);

/*
 * Promotion bridge for exact IR atoms. These preserve the small int64 fast
 * path but evaluate wider operands through the bounded exact-number context.
 */
phy_status phy_cas_exact_add_refs(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out_ref);
phy_status phy_cas_exact_sub_refs(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out_ref);
phy_status phy_cas_exact_mul_refs(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out_ref);
phy_status phy_cas_exact_div_refs(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out_ref);
phy_status phy_cas_exact_pow_ref(phy_cas *cas, phy_ir_ref base,
                                 int64_t exponent, phy_ir_ref *out_ref);
phy_status phy_cas_exact_mod_u32_ref(phy_cas *cas, phy_ir_ref integer,
                                     uint32_t modulus,
                                     uint32_t *out_remainder);
int phy_cas_exact_sign_ref(const phy_cas *cas, phy_ir_ref ref);

/* ------------------------------------------------------------ memo cache */

/*
 * What a cached entry answers. The tag is part of the key, so one table serves
 * every walk, and PHY_CAS_MEMO_NONE marks a free slot.
 *
 * PHY_CAS_MEMO_SUBST keys on a per-call epoch instead of a rule set, because a
 * rule set is not a ref and cannot be hashed as one. Entries from a previous
 * substitution are then unreachable rather than wrong, and are reclaimed the
 * next time the table is rebuilt.
 */
typedef enum {
    PHY_CAS_MEMO_NONE = 0,
    PHY_CAS_MEMO_SIMPLIFY, /* (expr)          -> simplified */
    PHY_CAS_MEMO_EXPAND,   /* (expr)          -> expanded */
    PHY_CAS_MEMO_DIFF,     /* (expr, var)     -> derivative */
    PHY_CAS_MEMO_INTEGRATE,/* (expr, var)     -> antiderivative */
    PHY_CAS_MEMO_DEPENDS,  /* (expr, var)     -> 0 or 1 */
    PHY_CAS_MEMO_SUBST,    /* (expr, epoch)   -> substituted */
    PHY_CAS_MEMO_TRIG,     /* (expr)          -> on one trigonometric basis */
    PHY_CAS_MEMO_RATIONAL, /* (expr)          -> numerator, denominator */
    PHY_CAS_MEMO_EXPAND_FACTORED /* (expr)    -> expanded, denominators kept */
} phy_cas_memo;

typedef struct {
    uint32_t tag;
    phy_ir_ref a;
    phy_ir_ref b;
    phy_ir_ref r0;
    phy_ir_ref r1;
} phy_cas_entry;

/* Growable, phy_alloc-backed, charged against limits.max_bytes. */
typedef struct {
    void *data;
    size_t count;
    size_t capacity;
} phy_cas_pool;

/*
 * Scalar functions whose semantics the CAS can see. Reader aliases are
 * normalized before they reach this registry.
 */
typedef enum {
    PHY_CAS_FN_INVALID = 0,
    PHY_CAS_FN_SIN,
    PHY_CAS_FN_COS,
    PHY_CAS_FN_TAN,
    PHY_CAS_FN_EXP,
    PHY_CAS_FN_LOG,
    PHY_CAS_FN_ASIN,
    PHY_CAS_FN_ACOS,
    PHY_CAS_FN_ATAN,
    PHY_CAS_FN_SINH,
    PHY_CAS_FN_COSH,
    PHY_CAS_FN_TANH,
    PHY_CAS_FN_ASINH,
    PHY_CAS_FN_ACOSH,
    PHY_CAS_FN_ATANH,
    PHY_CAS_FN_GAMMA,
    PHY_CAS_FN_LOGGAMMA,
    PHY_CAS_FN_ERF,
    PHY_CAS_FN_ERFC,
    PHY_CAS_FN_COUNT
} phy_cas_function;

struct phy_cas {
    phy_ir_context *ir;
    phy_cas_limits limits;
    size_t bytes;

    uint32_t steps; /* this operation */
    uint64_t total_steps;
    bool (*cancelled)(void *user);
    void *cancel_user;

    phy_cas_pool cache;   /* phy_cas_entry, power-of-two, open addressed */
    size_t cache_live;    /* occupied slots */
    phy_cas_pool scratch; /* phy_ir_ref, LIFO arena; count is the top */

    /* Heads of the functions this layer knows, interned at creation. */
    phy_ir_symbol functions[PHY_CAS_FN_COUNT];
    phy_ir_symbol fn_integrate;

    phy_ir_ref zero;
    phy_ir_ref one;
    phy_ir_ref minus_one;
    phy_ir_ref constant_pi;
    phy_ir_ref constant_e;
    phy_ir_ref constant_i;
    phy_ir_ref constant_euler_gamma;

    uint32_t subst_epoch;
};

/* --------------------------------------------------------------- engine.c */

/*
 * Charge one visited node. PHY_ERR_TIMEOUT when the step budget is spent,
 * PHY_ERR_INTERRUPTED when the cancellation hook says so. Called once per
 * cache miss, which is what makes the budget a bound on work rather than on
 * expression size.
 */
phy_status phy_cas_step(phy_cas *cas);

/* Resets the per-operation step count. Called by public entry points only. */
void phy_cas_begin(phy_cas *cas);

/*
 * Translates an IR builder's PHY_IR_NULL into a status. The IR records its
 * reason stickily; this reads it without clearing it, because that flag belongs
 * to the caller of the IR, not to this layer.
 */
phy_status phy_cas_ir_failure(const phy_cas *cas);

bool phy_cas_cache_get(const phy_cas *cas, phy_cas_memo tag, phy_ir_ref a,
                       phy_ir_ref b, phy_ir_ref *out_r0, phy_ir_ref *out_r1);
void phy_cas_cache_put(phy_cas *cas, phy_cas_memo tag, phy_ir_ref a,
                       phy_ir_ref b, phy_ir_ref r0, phy_ir_ref r1);

/*
 * Scratch arena, strictly LIFO.
 *
 * Callers hold OFFSETS, never pointers: a nested walk may grow the arena and
 * move it, so phy_cas_scratch_at() must be re-evaluated after anything that can
 * allocate. Every path out of a function that took a mark must release it, and
 * phy_cas_validate() fails if one did not.
 */
size_t phy_cas_scratch_mark(const phy_cas *cas);
phy_status phy_cas_scratch_alloc(phy_cas *cas, size_t count,
                                 size_t *out_offset);
phy_ir_ref *phy_cas_scratch_at(phy_cas *cas, size_t offset);
void phy_cas_scratch_release(phy_cas *cas, size_t mark);

/*
 * Bounded transient storage for algorithms whose work items are not IR refs.
 * The allocation is charged to phy_cas_limits.max_bytes for its full lifetime;
 * callers must release the exact byte count on every path.
 */
phy_status phy_cas_temp_alloc(phy_cas *cas, size_t bytes, void **out_pointer);
void phy_cas_temp_free(phy_cas *cas, void *pointer, size_t bytes);

/* Sorts `count` (key, value) ref pairs at `offset` by canonical key order. */
void phy_cas_sort_pairs(phy_cas *cas, size_t offset, size_t count);

/* True for PHY_IR_INTEGER and PHY_IR_RATIONAL, the exact numbers. */
bool phy_cas_is_exact(const phy_cas *cas, phy_ir_ref ref);
bool phy_cas_exact_value(const phy_cas *cas, phy_ir_ref ref,
                         phy_cas_rat *out_value);

/* Builds an exact number node, reducing through the IR's normalization. */
phy_status phy_cas_number_node(phy_cas *cas, phy_cas_rat value,
                               phy_ir_ref *out_ref);

/* True when `ref` is the integer `value`. */
bool phy_cas_is_integer(const phy_cas *cas, phy_ir_ref ref, int64_t value);

/*
 * Proved nonzero, from structure and declared assumptions. Conservative: false
 * means "not proved", never "zero". Sums are always unproved, because deciding
 * a sum is the zero decision itself.
 */
bool phy_cas_known_nonzero(const phy_cas *cas, phy_ir_ref ref);

/* The function head, or PHY_IR_NO_SYMBOL when `ref` is not a known function
   application of exactly one argument. */
phy_ir_symbol phy_cas_known_function(const phy_cas *cas, phy_ir_ref ref);
bool phy_cas_is_known_head(const phy_cas *cas, phy_ir_symbol head);
phy_cas_function phy_cas_function_id(const phy_cas *cas, phy_ir_symbol head);
phy_cas_function phy_cas_function_of(const phy_cas *cas, phy_ir_ref ref);

/* True for kinds this scalar layer treats as opaque: tensors, operators,
   noncommutative products, wedges, unevaluated derivatives, indices. */
bool phy_cas_kind_is_opaque(phy_ir_kind kind);

/* ------------------------------------------------------------- simplify.c */

phy_status phy_cas_simplify_node(phy_cas *cas, phy_ir_ref expr,
                                 phy_ir_ref *out_ref);

/*
 * Split a normalized expression into its leading exact coefficient and the
 * rest, so that 2*x yields (2, x) and x yields (1, x).
 *
 * Collecting a sum needs it, and so does recognizing the k in sin(k*u): both
 * questions are "what number is in front of this?", and asking them through one
 * routine keeps the answer from drifting between the two.
 */
phy_status phy_cas_split_coefficient(phy_cas *cas, phy_ir_ref expr,
                                     phy_ir_ref *out_coefficient,
                                     phy_ir_ref *out_rest);

/*
 * Simplifying builders, without the per-operation budget reset.
 *
 * The `_at` forms read their operands from the scratch arena, which is how a
 * caller that is mid-collection passes a list: an operand array can be longer
 * than any fixed local, and a pointer into the arena would not survive the
 * builder's own allocations. The pointer forms are for fixed local arrays and
 * MUST NOT be handed arena memory.
 */
phy_status phy_cas_add_at(phy_cas *cas, size_t offset, size_t count,
                          phy_ir_ref *out_ref);
phy_status phy_cas_mul_at(phy_cas *cas, size_t offset, size_t count,
                          phy_ir_ref *out_ref);
phy_status phy_cas_add_node(phy_cas *cas, const phy_ir_ref *terms, size_t count,
                            phy_ir_ref *out_ref);
phy_status phy_cas_mul_node(phy_cas *cas, const phy_ir_ref *factors,
                            size_t count, phy_ir_ref *out_ref);
phy_status phy_cas_pow_node(phy_cas *cas, phy_ir_ref base, phy_ir_ref exponent,
                            phy_ir_ref *out_ref);

/* -1 * value, simplified. The IR has no negation and no subtraction. */
phy_status phy_cas_neg_node(phy_cas *cas, phy_ir_ref value, phy_ir_ref *out_ref);

/*
 * Rebuild `kind` from `count` already-simplified operands in the arena,
 * applying that kind's rules. The one place a kind's rule set is selected, so
 * simplification and substitution cannot disagree about what a node means.
 */
phy_status phy_cas_rebuild_at(phy_cas *cas, phy_ir_kind kind,
                              phy_ir_symbol head, size_t offset, size_t count,
                              phy_ir_ref *out_ref);

/* --------------------------------------------------------------- diff.c */

phy_status phy_cas_diff_node(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                             phy_ir_ref *out_ref);
phy_status phy_cas_integrate_node(phy_cas *cas, phy_ir_ref expr,
                                  phy_ir_ref var, phy_ir_ref *out_ref);

/*
 * May `expr` depend on `var`? True for the variable itself, and for every
 * opaque kind: a tensor component may depend on a coordinate and nothing in the
 * graph says otherwise, so "false" has to mean provably independent.
 */
phy_status phy_cas_may_depend(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                              bool *out_depends);

/* --------------------------------------------------------------- normal.c */

phy_status phy_cas_expand_node(phy_cas *cas, phy_ir_ref expr,
                               phy_ir_ref *out_ref);
phy_status phy_cas_expand_factored_node(phy_cas *cas, phy_ir_ref expr,
                                        phy_ir_ref *out_ref);
phy_status phy_cas_rational_node(phy_cas *cas, phy_ir_ref expr,
                                 phy_ir_ref *out_numerator,
                                 phy_ir_ref *out_denominator);

/* Polished numerator over the factored, cancellation-reduced denominator. */
phy_status phy_cas_rational_reduced_node(phy_cas *cas, phy_ir_ref expr,
                                         phy_ir_ref *out_numerator,
                                         phy_ir_ref *out_denominator);

/* --------------------------------------------------------------- reduce.c */

/*
 * n1/d1 + n2/d2 over the least common denominator: each base factor at its
 * larger exponent, integer coefficients at their least common multiple.
 */
phy_status phy_cas_combine_sum_lcd(phy_cas *cas, phy_ir_ref n1, phy_ir_ref d1,
                                   phy_ir_ref n2, phy_ir_ref d2,
                                   phy_ir_ref *out_num, phy_ir_ref *out_den);

/*
 * Divide `numerator` exactly by the factors of the factored `denominator`
 * wherever the division leaves no remainder. No factorization is attempted:
 * only the denominator's own bases are tried.
 */
phy_status phy_cas_cancel_known_factors(phy_cas *cas, phy_ir_ref numerator,
                                        phy_ir_ref denominator,
                                        phy_ir_ref *out_num,
                                        phy_ir_ref *out_den);

/*
 * Largest |k| for which sin(k*u) and cos(k*u) are expanded into polynomials in
 * sin(u) and cos(u).
 *
 * The recurrence costs one polynomial multiplication per step and the result
 * has on the order of k terms, so an unbounded k would let one small-looking
 * argument -- sin(1000000*theta) -- turn a decision into a term-limit failure.
 * Past the cap the application stays an opaque generator, which costs
 * completeness (an UNKNOWN) and never soundness. The corpus needs k = 2.
 */
#define PHY_CAS_MAX_MULTIPLE_ANGLE 64

#endif /* PHY_CAS_INTERNAL_H */
