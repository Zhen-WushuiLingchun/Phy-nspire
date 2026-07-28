/*
 * Phy-nspire — scalar computer algebra over the typed IR.
 *
 * This is the rewrite layer docs/IR.md keeps deferring to. The IR canonicalizes
 * structure and refuses to do arithmetic; everything that changes the *value*
 * an expression denotes lives here.
 *
 * Four properties define it:
 *
 *   exact            integers and reduced rationals use an int64 fast path and
 *                    a bounded native arbitrary-precision promotion path.
 *                    Exhausting the configured exact-number budget is a typed
 *                    error, never a wrap or promotion to double. PHY_IR_REAL
 *                    atoms are carried, not folded -- see "Reals" below;
 *
 *   decidable        phy_cas_is_zero() DECIDES zero on a documented class of
 *                    expressions and answers PHY_CAS_UNKNOWN outside it. It
 *                    never estimates;
 *
 *   bounded          every operation runs against a step budget and an optional
 *                    cancellation hook, so a notebook cell can be interrupted
 *                    and a runaway rewrite fails as PHY_ERR_TIMEOUT;
 *
 *   memoized         results are cached on the interned ref, so a shared
 *                    subterm is simplified once no matter how many times the
 *                    expression mentions it.
 *
 * Every entry point returns a phy_status and writes its result through an out
 * parameter. Failure is ordinary here -- a budget runs out, a denominator is
 * zero, or a still-int64-only polynomial algorithm reaches its coefficient
 * ceiling -- and a caller that must distinguish those cases should not have to
 * consult a sticky flag to do it.
 *
 * A phy_cas borrows its phy_ir_context; it does not own it. Destroy the CAS
 * before the context. Every input phy_ir_ref must come from that same context;
 * compact refs carry no runtime context tag, so cross-context handles are a
 * caller-contract violation that cannot be diagnosed reliably. Neither object
 * is thread-safe.
 */
#ifndef PHY_CAS_H
#define PHY_CAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/ir.h"
#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_cas phy_cas;

/* ------------------------------------------------------------------ limits */

/*
 * Operation ceilings. Zero means "use the default" for each field
 * independently, matching phy_ir_limits.
 *
 * `max_steps` counts nodes visited, not nodes created: a rewrite that revisits
 * a shared subterm pays for it once, because the result is memoized. It is the
 * bound that makes an intentionally explosive expression fail as
 * PHY_ERR_TIMEOUT instead of running until the device is power-cycled.
 *
 * `max_bytes` covers the CAS object, memo cache, and operand scratch arena. The
 * cache is a cache: when growing it would breach the ceiling it is dropped and
 * rebuilt, which costs time and never correctness. Scratch is not optional, so
 * a genuinely too-small ceiling surfaces as PHY_ERR_MEMORY_LIMIT.
 */
typedef struct {
    uint32_t max_steps; /* nodes visited per operation; default 200000 */
    size_t max_bytes;   /* memo cache plus scratch; default 256 KiB */
} phy_cas_limits;

void phy_cas_limits_defaults(phy_cas_limits *out_limits);

/* ----------------------------------------------------------------- context */

/*
 * Create a CAS over `ir`. Passing NULL limits uses the defaults. Returns NULL
 * if `ir` is NULL, the limits are unusable, or the initial allocation fails.
 *
 * Creation interns the elementary, inverse, hyperbolic, and bounded special
 * functions this layer knows into `ir`, so recognizing one is a symbol-id
 * comparison rather than a string compare per visit. A caller that interns
 * the same names later gets the same ids, because interning is by bytes.
 *
 * All memory is taken through phy_alloc/phy_free, so CAS usage shows up in
 * phy_telemetry alongside the IR's.
 */
phy_cas *phy_cas_create(phy_ir_context *ir, const phy_cas_limits *limits);
void phy_cas_destroy(phy_cas *cas);

phy_ir_context *phy_cas_ir(const phy_cas *cas);

/*
 * Cancellation. `cancelled` is polled during long operations and makes the
 * operation in flight return PHY_ERR_INTERRUPTED. Pass NULL to remove it.
 *
 * The hook exists so this layer needs no clock and no platform dependency: the
 * notebook shell owns the wall-clock budget and the ESC key, and answers with a
 * bool. That also keeps the test suite deterministic.
 */
void phy_cas_set_cancel(phy_cas *cas, bool (*cancelled)(void *user),
                        void *user);

/* Steps spent by the most recent operation, and since creation. */
uint32_t phy_cas_steps(const phy_cas *cas);
uint64_t phy_cas_total_steps(const phy_cas *cas);

/* Live bytes held by the CAS object, memo cache, and scratch arena. */
size_t phy_cas_bytes_used(const phy_cas *cas);

/*
 * Drop every memoized result.
 *
 * REQUIRED AFTER CHANGING ASSUMPTIONS. A few rewrites read declared properties
 * of a symbol -- phy_cas_is_zero() consults PHY_IR_ASSUME_NONZERO, for
 * instance -- so a result cached before phy_ir_assume() was called may not be
 * what the same input would produce now. Declare first, compute second; if a
 * document declares late, clear the cache.
 */
void phy_cas_cache_clear(phy_cas *cas);

/*
 * Internal invariants: the scratch arena is fully released, the cache holds
 * only well-formed entries, and the byte count matches the pools. PHY_OK or
 * PHY_ERR_CORRUPT_DOCUMENT.
 *
 * Every operation, including one that failed part-way, must leave a CAS
 * validating -- that is how the tests show the failure paths unwind their
 * scratch rather than asserting that they do. Cheap; intended for tests and
 * debug assertions.
 */
phy_status phy_cas_validate(const phy_cas *cas);

/* --------------------------------------------------------------- arithmetic */

/*
 * Simplifying constructors. Each builds the operation and returns it in normal
 * form, which is what makes them usable as an arithmetic vocabulary: a physics
 * module writes a formula as a sequence of these calls and never sees an
 * unreduced intermediate.
 *
 * There is no division or subtraction node in the IR. `a - b` is
 * `a + (-1)*b` and `a / b` is `a * b^(-1)`; these two spell the intent so that
 * call sites read as arithmetic.
 *
 * phy_cas_div() reports PHY_ERR_DOMAIN when the simplified divisor is exactly
 * zero. It does not run the full zero decision -- that is
 * phy_cas_is_zero()'s job and a caller dividing by something subtle should ask
 * first.
 */
phy_status phy_cas_number(phy_cas *cas, int64_t numerator, int64_t denominator,
                          phy_ir_ref *out_ref);
phy_status phy_cas_add(phy_cas *cas, const phy_ir_ref *terms, size_t count,
                       phy_ir_ref *out_ref);
phy_status phy_cas_mul(phy_cas *cas, const phy_ir_ref *factors, size_t count,
                       phy_ir_ref *out_ref);
phy_status phy_cas_neg(phy_cas *cas, phy_ir_ref value, phy_ir_ref *out_ref);
phy_status phy_cas_sub(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                       phy_ir_ref *out_ref);
phy_status phy_cas_div(phy_cas *cas, phy_ir_ref numerator,
                       phy_ir_ref denominator, phy_ir_ref *out_ref);
phy_status phy_cas_pow(phy_cas *cas, phy_ir_ref base, phy_ir_ref exponent,
                       phy_ir_ref *out_ref);

/* ------------------------------------------------------------- simplification */

/*
 * Normal form, without expansion. Structure is preserved: a product of sums
 * stays a product of sums, because that is usually what a reader wants to see.
 *
 * What it guarantees of the result:
 *
 *   - every subexpression of exact numbers is one exact number;
 *   - a sum has at most one numeric term, no zero term, and no two terms
 *     sharing a non-numeric part: 2*x + 3*x is 5*x;
 *   - a product has at most one numeric factor, never the factor 1, and no two
 *     factors sharing a base: x * x^2 is x^3;
 *   - a power has neither exponent 0 nor 1, and folds when the base is an exact
 *     number and the exponent an integer;
 *   - a PHY_IR_ERROR operand anywhere propagates outward as the whole result,
 *     so a failed cell stays a typed value rather than becoming a well-formed
 *     expression that quietly means something else.
 *
 * The result is canonical for polynomials once expanded, which is what
 * phy_cas_is_zero() rests on: expanded, collected, and interned, two equal
 * polynomials are the same ref.
 *
 * Noncommutative and physics kinds -- PHY_IR_NCMUL, PHY_IR_WEDGE,
 * PHY_IR_TENSOR, PHY_IR_OPERATOR, PHY_IR_DERIVATIVE -- have their operands
 * simplified in place and are otherwise left alone. Nothing here reorders them,
 * collects across them, or reads their indices: this layer is scalar, and index
 * canonicalization is a separate phase with its own decision procedure.
 *
 * Cancellation of x/x uses the generic-domain convention: identities hold where
 * the expression is defined, so x^0 is 1 and x - x is 0 without demanding a
 * nonzero declaration first. What that convention does NOT do is invent a
 * denominator: 1/0 is PHY_ERR_DOMAIN, as is 0^0 and 0^(-1).
 */
phy_status phy_cas_simplify(phy_cas *cas, phy_ir_ref expr,
                            phy_ir_ref *out_ref);

/*
 * Distribute products over sums and integer powers over their bases, then
 * simplify. Bounded by the IR's term limit, so a deliberately explosive
 * expansion fails as PHY_ERR_TERM_LIMIT rather than exhausting the device.
 *
 * Function arguments are expanded too. That matters for more than tidiness:
 * sin(x*(y+1)) and sin(x*y+x) must not be two different opaque generators, or
 * the zero decision below would answer UNKNOWN on expressions it can decide.
 */
phy_status phy_cas_expand(phy_cas *cas, phy_ir_ref expr, phy_ir_ref *out_ref);

/*
 * The same, except that a negative integer power of a sum is left alone:
 * (x+1)^(-2) stays a power of (x+1) instead of becoming 1/(x^2+2*x+1). Bases
 * are still expanded, so two denominators that are equal are the same ref and
 * collect against each other and against matching numerator factors.
 *
 * This is the form to carry BETWEEN stages of a long computation, and
 * phy_cas_expand() is the form to hand the zero decision at the end. The
 * difference is not stylistic. Expanding a denominator destroys the only
 * structure the product collector can use: once (r - 2*M)^(-2) has become
 * (4*M^2 - 4*M*r + r^2)^(-1) it no longer cancels against the (r - 2*M)
 * factors that the next contraction multiplies in, so the denominators
 * accumulate instead of collapsing. In the curvature pipeline that is the
 * difference between a Riemann component of a few dozen nodes and one whose
 * rational form reaches degree 33 with large coefficients. Factored expansion
 * prevents that avoidable growth even though scalar number folding itself now
 * promotes beyond the int64 fast path.
 *
 * Both are exact and both are bounded by the IR's term limit.
 */
phy_status phy_cas_expand_factored(phy_cas *cas, phy_ir_ref expr,
                                   phy_ir_ref *out_ref);

/* One replacement. `from` may be any subexpression, not only a symbol. */
typedef struct {
    phy_ir_ref from;
    phy_ir_ref to;
} phy_cas_rule;

/*
 * Replace every occurrence of each rule's `from`, then simplify. Rules are
 * applied to the original subtrees only -- a rule never rewrites what another
 * rule produced -- so a set like {x -> y, y -> x} swaps rather than looping.
 * Earlier rules win on a subtree they both match.
 */
phy_status phy_cas_substitute(phy_cas *cas, phy_ir_ref expr,
                              const phy_cas_rule *rules, size_t count,
                              phy_ir_ref *out_ref);

/*
 * Partial derivative with respect to a symbol, simplified. `var` must be a
 * PHY_IR_SYMBOL; an index is PHY_ERR_TYPE, because differentiating with
 * respect to an index is tensor calculus and not this layer's business.
 *
 * Known elementary/inverse/hyperbolic functions plus Gamma, LogGamma, Erf,
 * and Erfc differentiate through the chain rule. Anything whose dependence on
 * `var` this layer cannot see through --
 * an unknown function, a tensor, an operator, a noncommutative product --
 * yields an unevaluated PHY_IR_DERIVATIVE node rather than a wrong zero. That
 * distinction is the whole point of the typed IR: a tensor component may well
 * depend on a coordinate, and nothing in the graph says it does not.
 */
phy_status phy_cas_diff(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                        phy_ir_ref *out_ref);

/*
 * Exact symbolic antiderivative on the currently decidable class:
 * constants and sums, products with a variable-independent coefficient,
 * rational powers of a linear inner expression; elementary and hyperbolic
 * functions with linear inner expressions; the four inverse-function kernels
 * 1/(1+u^2), 1/(1-u^2), 1/sqrt(1+u^2), 1/sqrt(1-u^2); Erf/Erfc with linear
 * inner expressions; and exact Gaussian exp(-(a*x+b)^2) cases. Anything
 * outside that class is returned as an unevaluated Integrate[expr,var] IR
 * function, never guessed numerically.
 */
phy_status phy_cas_integrate(phy_cas *cas, phy_ir_ref expr, phy_ir_ref var,
                             phy_ir_ref *out_ref);

/* ------------------------------------------------------------ the zero decision */

/*
 * The result of a decision this layer will not guess at.
 *
 * PHY_CAS_UNKNOWN is zero-valued on purpose: a zeroed structure means "no
 * information", never "proved".
 */
typedef enum {
    PHY_CAS_UNKNOWN = 0,
    PHY_CAS_ZERO,
    PHY_CAS_NONZERO
} phy_cas_decision;

/*
 * Rewrite as a quotient of expanded, collected polynomials over the
 * expression's generators. Both parts are returned; the denominator is 1 for a
 * polynomial input.
 *
 * A generator is any subexpression this layer cannot see inside: a symbol, a
 * function application, a power with a non-integer exponent, a tensor.
 *
 * Trigonometry is put on one basis first, so that a trigonometric polynomial in
 * an argument is canonical rather than merely tidy. Three rewrites do it, and
 * they apply here and in the zero decision only -- phy_cas_simplify() leaves
 * tan(u) and sin(2*u) alone, because they are what the reader wrote:
 *
 *   tan(u)                -> sin(u) / cos(u);
 *   sin(k*u), cos(k*u)    -> polynomials in sin(u) and cos(u), for integer
 *                            |k| in 2..64, by the angle-addition recurrence;
 *   cos(u)^k, k >= 2      -> (1 - sin(u)^2)^(k/2) * cos(u)^(k mod 2).
 *
 * The result is a polynomial of degree at most one in each cos(u), which is
 * what makes equal trigonometric expressions reach equal refs. The corpus in
 * research/corpus/gr_golden.json needs exactly this: sphere_2d states its
 * connection as -sin(2*theta)/2 and its Ricci component as
 * sin(2*theta)/(2*tan(theta)) - cos(2*theta), and both must be decided equal to
 * the forms a curvature pass actually computes.
 *
 * The numerator is divided exactly by each of the denominator's own factors
 * wherever the remainder vanishes. A bounded Euclidean GCD over Q[x] then
 * cancels hidden common factors of univariate polynomials through degree 48.
 * Multivariate polynomial GCD is not claimed: a common factor that requires
 * treating another symbol as a coefficient stays explicit.
 */
phy_status phy_cas_rational_form(phy_cas *cas, phy_ir_ref expr,
                                 phy_ir_ref *out_numerator,
                                 phy_ir_ref *out_denominator);

/*
 * Simplification with the zero decision's normal form behind it. Runs the
 * plain simplifier first, then the trig-basis rational form, and returns
 * whichever of the two prints shorter -- so sin(x)^2 + cos(x)^2 reaches 1,
 * while 1/tan(theta) is not forcibly rewritten onto the sin/cos basis. A
 * rational pass that exhausts a resource budget (or hits an identically
 * zero denominator) falls back to the plain result rather than failing.
 */
phy_status phy_cas_full_simplify(phy_cas *cas, phy_ir_ref expr,
                                 phy_ir_ref *out_ref);

/*
 * `expr` as one reduced quotient: the cancelled numerator times the factored
 * denominator's inverse powers, 12*rs^2*rq^-6 rather than seventeen terms
 * over mixed denominators. The reduction class is the decision class below;
 * outside it the parts stay opaque generators and the result is still exact.
 */
phy_status phy_cas_reduce(phy_cas *cas, phy_ir_ref expr, phy_ir_ref *out_ref);

/*
 * Complete exact factorization over Q[x] on the bounded class documented in
 * docs/CAS.md. The current kernel extracts rational linear factors, performs
 * square-free decomposition, and proves irreducibility of residual factors of
 * degree two or three. A higher-degree square-free residual that cannot yet be
 * split is PHY_ERR_UNSUPPORTED; returning a merely partial product as a
 * successful Factor result would be misleading.
 */
phy_status phy_cas_factor(phy_cas *cas, phy_ir_ref expr,
                          phy_ir_ref *out_ref);

/*
 * Decide whether `expr` is zero.
 *
 * PHY_CAS_ZERO means identically zero as a rational function, wherever the
 * expression is defined -- the numerator of the form above reduces to exactly
 * 0. It is a proof, not a heuristic, and it is exact on this class:
 *
 *     exact numbers, symbols, +, *, integer powers, and sin, cos and tan of an
 *     integer multiple k*u, |k| <= 64, of an argument u drawn from the same
 *     class,
 *
 * which covers docs/references/GENERAL_RELATIVITY.md's corpus. Curvature that
 * must vanish for a non-flat-looking chart -- Minkowski in spherical
 * coordinates is the standard case -- is decided here, not estimated, and so is
 * the equality of a computed component with a corpus entry written in a
 * different but equivalent trigonometric form.
 *
 * PHY_CAS_NONZERO means proved nonzero: the numerator reduces to a nonzero
 * exact number, or to a product of factors each known nonzero, which is where
 * PHY_IR_ASSUME_NONZERO and the sign assumptions are read.
 *
 * PHY_CAS_UNKNOWN means the expression left the decidable class. Outside it are
 * PHY_IR_REAL atoms, non-integer powers, unknown functions, tensors and
 * operators, and exact powers too large to fold. Trigonometry leaves it in
 * three specific ways, all of which answer UNKNOWN rather than guessing:
 * a non-integer multiple of an argument, since sin(u/2) is not a polynomial in
 * sin(u) and cos(u); a multiple beyond 64, which is treated as opaque rather
 * than expanded into a polynomial with that many terms; and a sum inside an
 * argument, since sin(u + v) is not rewritten through the addition formula.
 *
 * A PHY_ERR_* return is a failed decision -- budget, overflow, memory -- and is
 * distinct from a successful PHY_CAS_UNKNOWN.
 */
phy_status phy_cas_is_zero(phy_cas *cas, phy_ir_ref expr,
                           phy_cas_decision *out_decision);

/*
 * phy_cas_is_zero() on left - right, which is how a golden corpus is checked:
 * equal expressions reached by different routes are equal values even when
 * they are different graphs.
 */
phy_status phy_cas_equivalent(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                              phy_cas_decision *out_decision);

#ifdef __cplusplus
}
#endif

#endif /* PHY_CAS_H */
