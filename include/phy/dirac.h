/*
 * Phy-nspire — Dirac algebra: gamma strings, Lorentz contraction, and traces.
 *
 * Contracts Q-2, Q-3 and Q-4 of docs/agent-tasks/QFT_DIRAC.md, written against
 * the conventions and the algorithm register of
 * docs/references/QFT_GAUGE.md 4, 6 and 8.
 *
 * WHAT THIS LAYER IS
 *
 * A Dirac expression is a sum of terms. A term is an exact scalar coefficient
 * times an ordered product of gamma factors, each factor carrying a spin-line
 * identifier and either a Lorentz index or a momentum. That shape is not
 * decoration: FORM's second defining relation is
 *
 *     {g_(j,mu), g_(j,nu)} = 2 d_(mu,nu)      same spin line j
 *     [g_(j1,mu), g_(j2,nu)] = 0               j1 != j2
 *
 * and an engine that models a gamma string as one flat noncommutative product
 * cannot express a two-fermion-line amplitude at all. Factors on different
 * lines commute here, and the canonical form uses that to group them.
 *
 * The identity element is likewise explicit rather than implied. FORM's manual
 * warns that writing a projector as (g_(1,p)+m) instead of
 * (g_(1,p)+m*gi_(1)) is "technically not correct", because a term can end up
 * traced with no gamma matrix in it. Here the trace names the line it acts on,
 * so a term with no factors on that line traces to the coefficient times the
 * Tr[1] parameter -- a defined answer, not an accident.
 *
 * WHAT THIS LAYER IS NOT
 *
 * No gamma-5, no chiral projectors, no Levi-Civita, and no D != 4. Those are
 * deferred by reference 2 and 4.5 with a stated blocking dependency: every
 * mature implementation carries a whole scheme apparatus around gamma-5 --
 * BMHV, Larin, naive -- and a scheme choice cannot be retrofitted onto an IR
 * with no dimension tracking. The metric handed to phy_dirac_create() must
 * therefore have dimension 4; anything else is PHY_ERR_UNSUPPORTED at
 * creation rather than a wrong answer at evaluation.
 *
 * No spinors, no polarization sums, no squared amplitudes, no loop integrals,
 * and no Fierz rearrangement. No Chisholm identity: it combines traces that
 * share an index, and with no amplitude layer above there are no such traces
 * to combine.
 *
 * RESOURCES
 *
 * A trace of 2n gammas with all indices distinct expands into (2n-1)!! metric
 * terms before anything cancels: 15 at six, 105 at eight, 10,395 at twelve. So
 * the growth law is in the API rather than discovered at runtime. Contraction
 * runs first and is worth roughly a factor of fifty in generated terms on
 * FORM's own published measurements, and both a per-line gamma ceiling and a
 * generated-term ceiling are configurable and report PHY_ERR_TERM_LIMIT.
 *
 * Those two ceilings default to reference 8's proposals, which are flagged
 * UNVERIFIED there: they are combinatorial arithmetic plus workstation figures
 * quoted from FORM's manual. No CX II has been measured. Contract Q-7 settles
 * it; until then treat the defaults as provisional.
 *
 * A phy_dirac borrows its metric, and through it the CAS and IR context.
 * Destroy expressions before the phy_dirac, and the phy_dirac before the
 * metric. Nothing here is thread-safe.
 */
#ifndef PHY_DIRAC_H
#define PHY_DIRAC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/lorentz.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Gamma factors one term may carry across all of its spin lines. Two full
 * twelve-gamma lines would be a two-loop object this MVP has no layer for, so
 * the compile-time bound is one full line plus a second line's worth of
 * headroom. Exceeding it is PHY_ERR_UNSUPPORTED, the fixed-capacity status
 * every other bounded table in this project uses.
 */
#define PHY_DIRAC_MAX_FACTORS 24u

typedef struct phy_dirac phy_dirac;
typedef struct phy_dirac_expr phy_dirac_expr;

/*
 * Operation ceilings. Zero means "use the default" for each field
 * independently, matching phy_ir_limits and phy_cas_limits.
 *
 * `max_gammas_per_line` bounds one spin line's string; twelve is where
 * (2n-1)!! reaches 10,395. `max_terms` bounds the terms a single operation may
 * generate; 4,096 sits below FORM's measured thirteen-matrix output and above
 * every golden case in the MVP corpus. Both report PHY_ERR_TERM_LIMIT.
 */
typedef struct {
    uint32_t max_gammas_per_line; /* default 12 */
    uint32_t max_terms;           /* default 4096 */
} phy_dirac_limits;

void phy_dirac_limits_defaults(phy_dirac_limits *out_limits);

/* ---------------------------------------------------------------- context */

/*
 * Create a Dirac algebra over `metric`, which must have dimension 4.
 *
 * The signature is not constrained: the Clifford relation, the contraction
 * identities and the trace recursion are all signature-independent, and every
 * result this layer produces is symbolic in g. Which signature the metric
 * carries decides what those symbols mean when components are substituted, and
 * reference 4.1 pins (+,-,-,-) for the QFT corpus.
 */
phy_status phy_dirac_create(const phy_lorentz_metric *metric,
                            const phy_dirac_limits *limits,
                            phy_dirac **out_dirac);
void phy_dirac_destroy(phy_dirac *dirac);

phy_cas *phy_dirac_cas(const phy_dirac *dirac);
const phy_lorentz_metric *phy_dirac_metric(const phy_dirac *dirac);

/*
 * Tr[1], defaulting to the exact integer 4.
 *
 * A parameter and not a literal, following FORM's `unittrace` and FeynCalc's
 * TraceOfOne -> 4 rather than SymPy's hard-coded gctr = 4. It matters for
 * dimensional regularisation and for non-4-dimensional Clifford algebras, both
 * deferred and both foreseeable. Note that it is independent of the spacetime
 * dimension: this layer's contraction rules are 4-dimensional whatever Tr[1]
 * is set to, which is exactly FORM's split between `unittrace` and `trace4`.
 */
phy_status phy_dirac_set_trace_of_one(phy_dirac *dirac, phy_ir_ref value);
phy_ir_ref phy_dirac_trace_of_one(const phy_dirac *dirac);

/*
 * Terms generated by the most recent operation, and since creation.
 *
 * This is the observable that makes "contract before you expand" a measured
 * claim rather than an assertion: tracing a string with contracted pairs
 * without contracting first generates strictly more terms for the same value.
 */
uint32_t phy_dirac_generated_terms(const phy_dirac *dirac);
uint64_t phy_dirac_total_generated_terms(const phy_dirac *dirac);

/* ------------------------------------------------------------ construction */

/* The empty sum. */
phy_status phy_dirac_zero(phy_dirac *dirac, phy_dirac_expr **out_expr);

/* One term, coefficient `value`, no gamma factors on any line. */
phy_status phy_dirac_scalar(phy_dirac *dirac, phy_ir_ref value,
                            phy_dirac_expr **out_expr);

/*
 * The identity on one spin line, FORM's gi_(j).
 *
 * Structurally this is phy_dirac_scalar(1): a term with no factors is the
 * identity on every line at once, which is the mathematically right statement
 * and is why a trace has to name its line. The constructor exists so that call
 * sites can say what they mean.
 */
phy_status phy_dirac_identity(phy_dirac *dirac, uint32_t line,
                              phy_dirac_expr **out_expr);

/* gamma^mu on `line`. `index` must belong to this Dirac layer's metric. */
phy_status phy_dirac_gamma(phy_dirac *dirac, uint32_t line, phy_ir_ref index,
                           phy_dirac_expr **out_expr);

/*
 * The slash p-slash on `line`. `momentum` must be declared in the metric.
 *
 * FORM spells this g_(1,p1), the same head as g_(1,mu) with a vector in the
 * index slot, and keeping one code path for both is why the factor's argument
 * is a single typed ref rather than a tagged union of two shapes.
 */
phy_status phy_dirac_slash(phy_dirac *dirac, uint32_t line, phy_ir_ref momentum,
                           phy_dirac_expr **out_expr);

void phy_dirac_expr_destroy(phy_dirac_expr *expr);
phy_status phy_dirac_expr_copy(const phy_dirac_expr *expr,
                               phy_dirac_expr **out_expr);

phy_status phy_dirac_add(const phy_dirac_expr *left,
                         const phy_dirac_expr *right,
                         phy_dirac_expr **out_expr);
phy_status phy_dirac_scale(const phy_dirac_expr *expr, phy_ir_ref scalar,
                           phy_dirac_expr **out_expr);

/*
 * Noncommutative product. Each line's factors are concatenated in order,
 * left's before right's, and factors on different lines never cross.
 */
phy_status phy_dirac_mul(const phy_dirac_expr *left,
                         const phy_dirac_expr *right,
                         phy_dirac_expr **out_expr);

/* ------------------------------------------------------------- inspection */

const phy_dirac *phy_dirac_expr_owner(const phy_dirac_expr *expr);
size_t phy_dirac_expr_term_count(const phy_dirac_expr *expr);
phy_ir_ref phy_dirac_term_coefficient(const phy_dirac_expr *expr, size_t term);
size_t phy_dirac_term_factor_count(const phy_dirac_expr *expr, size_t term);
phy_status phy_dirac_term_factor(const phy_dirac_expr *expr, size_t term,
                                 size_t factor, uint32_t *out_line,
                                 phy_ir_ref *out_argument);

/* ----------------------------------------------------------------- rewrites */

/*
 * Canonical form. Coefficients are simplified and provably zero terms dropped,
 * each term's factors are grouped by ascending spin line with the order inside
 * a line preserved, contracted dummy indices are renamed deterministically,
 * equal terms are collected, and the terms themselves are put in a structural
 * order.
 *
 * Two expressions that differ only by which names their contracted dummies
 * carry, or by the order in which factors on different lines were written,
 * reach the same canonical form. Two expressions that differ by the order of
 * factors on ONE line do not -- that is the Clifford relation's job, below,
 * and conflating the two would make gamma matrices commute.
 *
 * Renaming dummies is decidable here precisely because a gamma string is
 * ordered: first-occurrence order is a property of the term. The general
 * problem, over a commutative product whose operand order depends on the names
 * being chosen, is the abstract dummy canonicalization that docs/IR.md and the
 * Phase 2 roadmap entry both leave open.
 */
phy_status phy_dirac_canonical(const phy_dirac_expr *expr,
                               phy_dirac_expr **out_expr);

/*
 * Clifford normal form: canonical form, and additionally each line's factors
 * sorted under the IR's structural order by
 *
 *     gamma^a gamma^b = 2 g^{ab} 1 - gamma^b gamma^a,
 *
 * with an adjacent repeated momentum collapsing through p-slash p-slash =
 * (p.p) 1. Equal expressions therefore reach equal normal forms, which is what
 * makes {gamma^mu, gamma^nu} = 2 g^{mu nu} a computation rather than a claim.
 *
 * Each swap either removes two factors or removes one inversion, so the
 * rewrite terminates; the branch that removes two factors is what can multiply
 * terms, and it is bounded by max_terms.
 */
phy_status phy_dirac_clifford_normal(const phy_dirac_expr *expr,
                                     phy_dirac_expr **out_expr);

/*
 * Contract every repeated Lorentz index that occurs once up and once down
 * inside one spin line, by FORM's trace rules 1 to 4 at D = 4:
 *
 *   rule 1  gamma^mu gamma_mu                     = 4 . 1
 *   rule 2  gamma^mu b_1..b_m gamma_mu, m odd     = -2 b_m..b_1
 *   rule 3  gamma^mu b_1 b_2 gamma_mu             = 4 g^{b_1 b_2} . 1
 *           gamma^mu b_1..b_m gamma_mu, m even    = 2 b_m b_1..b_{m-1}
 *                                                 + 2 b_{m-1}..b_1 b_m
 *
 * Factors outside the contracted pair keep their positions and their order.
 * That sentence is the whole of SymPy issue #23823, in which a mature package
 * returned 4 gamma^sigma gamma^rho for gamma^rho gamma^sigma gamma^mu
 * gamma_mu: the leading matrices were removed and reinserted by a loop that
 * ran backwards. A test asserting only the scalar factor 4 does not catch it.
 *
 * Each application removes two gamma factors, so the rewrite terminates and
 * cannot revisit a state. An index paired with an occurrence outside the gamma
 * factors -- in a coefficient, say -- is left alone rather than guessed at.
 *
 * FORM's rule 5 is deliberately absent: it introduces gamma-5 and the
 * Levi-Civita tensor. The trace recursion below closes the recursion instead.
 */
phy_status phy_dirac_contract(const phy_dirac_expr *expr,
                              phy_dirac_expr **out_expr);

/*
 * Trace over one spin line, by the metric recursion
 *
 *   Tr[g^{m1}..g^{m2n}] = sum_{j=2}^{2n} (-1)^j g^{m1 mj}
 *                         Tr[g^{m2}..^g^{mj}..g^{m2n}]
 *
 * terminating at the Tr[1] parameter, with odd-length strings zero. The
 * factors on `line` are removed and the coefficient multiplied by the scalar;
 * factors on other lines are untouched, because tracing over one spin space
 * says nothing about another.
 *
 * The implementation runs phy_dirac_contract() before expanding the trace.
 * It is not merely faster: the scalar result here is a polynomial in metric
 * tensors and scalar products, and this layer does not contract repeated
 * indices *between* two such tensors. Contracting the gamma string first
 * avoids creating those redundant metric products in the first place.
 */
phy_status phy_dirac_trace(const phy_dirac_expr *expr, uint32_t line,
                           phy_dirac_expr **out_expr);

/*
 * phy_dirac_trace() where the result is expected to be a pure scalar.
 * PHY_ERR_TYPE if any gamma factor survives, which happens exactly when the
 * expression carries a spin line other than `line`.
 */
phy_status phy_dirac_trace_scalar(const phy_dirac_expr *expr, uint32_t line,
                                  phy_ir_ref *out_scalar);

/*
 * Compare two expressions through their Clifford normal forms.
 *
 * PHY_CAS_ZERO means proved equal: same factors, and coefficients the scalar
 * CAS decides equal. PHY_CAS_UNKNOWN means a coefficient left the CAS's
 * decidable class, never that the expressions look different.
 */
phy_status phy_dirac_equivalent(const phy_dirac_expr *left,
                                const phy_dirac_expr *right,
                                phy_cas_decision *out_decision);

/*
 * Serialize into the typed IR as a sum of
 *
 *     coefficient * NonCommutativeMultiply[DiracGamma[line, arg], ...]
 *
 * with a term carrying no factors emitted as its coefficient alone. The head
 * is DiracGamma rather than the notebook palette's one-argument Gamma because
 * the spin line is not optional here.
 */
phy_status phy_dirac_to_ir(const phy_dirac_expr *expr, phy_ir_ref *out_expr);

#ifdef __cplusplus
}
#endif

#endif /* PHY_DIRAC_H */
