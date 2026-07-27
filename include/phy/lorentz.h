/*
 * Phy-nspire — Lorentz indices, momenta, and scalar products.
 *
 * This is contract Q-1 of docs/agent-tasks/QFT_DIRAC.md: the index and
 * scalar-product *objects* the Dirac and Mandelstam layers are built from. It
 * adds no rewriting of its own beyond the exact bilinear expansion of a dot
 * product, because rewriting gamma strings is Q-2 onward.
 *
 * The one design decision worth stating up front is that the signature is a
 * property of a metric object and never a global constant. Phy-nspire hosts
 * both conventions in one notebook: the QFT corpus is pinned to the
 * particle-physics signature (+,-,-,-) by docs/references/QFT_GAUGE.md 4.1,
 * while the general-relativity slice conventionally uses (-,+,+,+). Two
 * metrics therefore coexist, each owning its own index bundle, and an index
 * from one can never silently contract with an index from the other: every
 * entry point here checks the bundle and answers PHY_ERR_TYPE. That check is
 * cheap now and would be archaeology later.
 *
 * Three typed spellings are produced and nothing else:
 *
 *   Momentum[p, <space>]   an abstract 4-vector; an atom, not a component;
 *   <g>[a, b]              the metric with two index slots, symmetric, where
 *                          <g> is the metric's own name;
 *   <p>[a]                 the component p^a, an ordinary indexed tensor.
 *
 * A scalar product is LorentzDot[P, Q] over two momentum atoms, with the two
 * operands stored in the IR's canonical order so that p.q and q.p are one
 * node rather than two nodes that a later pass has to prove equal.
 *
 * A metric borrows its phy_cas and the CAS's IR context; destroy the metric
 * before either. Nothing here is thread-safe.
 */
#ifndef PHY_LORENTZ_H
#define PHY_LORENTZ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/cas.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Momenta a single metric can carry. A 2 -> 2 process needs four; the
 * headroom is for the loop momentum and the masses' auxiliary vectors. A
 * fifth-and-beyond declaration is PHY_ERR_UNSUPPORTED, matching how every
 * other bounded table in this project reports a fixed capacity.
 */
#define PHY_LORENTZ_MAX_MOMENTA 8u

typedef enum {
    PHY_LORENTZ_MOSTLY_MINUS = 0, /* (+,-,-,-): FeynCalc, Peskin, this corpus */
    PHY_LORENTZ_MOSTLY_PLUS       /* (-,+,+,+): MTW, Wald, the GR slice */
} phy_lorentz_signature;

typedef struct phy_lorentz_metric phy_lorentz_metric;

/*
 * A momentum as an exact linear combination of the metric's declared momenta.
 *
 * It is a value type on purpose: `(p1 + p2)` is arithmetic, not an allocation,
 * and a routing convention that differs only by signs should cost nothing to
 * express. A PHY_IR_NULL coefficient means exactly zero.
 */
typedef struct {
    const phy_lorentz_metric *metric;
    phy_ir_ref coefficient[PHY_LORENTZ_MAX_MOMENTA];
} phy_lorentz_vector;

/*
 * How often one index name occurs, split by position. This is the whole of
 * free/dummy classification: a name used once is free, a name used once up and
 * once down is a contracted dummy, and anything else is malformed -- two upper
 * occurrences of one name is not a contraction, it is a typo the engine must
 * not paper over.
 */
typedef struct {
    phy_ir_symbol name;
    uint32_t upper;
    uint32_t lower;
} phy_lorentz_index_use;

static inline bool phy_lorentz_index_is_free(const phy_lorentz_index_use *use)
{
    return use != NULL && use->upper + use->lower == 1u;
}

static inline bool phy_lorentz_index_is_dummy(const phy_lorentz_index_use *use)
{
    return use != NULL && use->upper == 1u && use->lower == 1u;
}

static inline bool phy_lorentz_index_is_malformed(
    const phy_lorentz_index_use *use)
{
    return use != NULL && !phy_lorentz_index_is_free(use) &&
           !phy_lorentz_index_is_dummy(use);
}

/* --------------------------------------------------------------- metric */

/*
 * Create a metric named `name` whose indices live in the bundle `space`.
 *
 * `name` becomes the head of the metric tensor -- "g" gives g[a,b] -- and
 * `space` becomes the index bundle every index of this metric carries, so
 * ("g", "Lorentz") and ("g", "LorentzGR") are two different metrics whose
 * indices never meet. Both are interned in the CAS's IR context.
 *
 * `dimension` is carried symbolically and is what g^mu_mu folds to. The Dirac
 * layer requires 4; nothing here does.
 */
phy_status phy_lorentz_metric_create(phy_cas *cas, const char *name,
                                     const char *space, unsigned dimension,
                                     phy_lorentz_signature signature,
                                     phy_lorentz_metric **out_metric);
void phy_lorentz_metric_destroy(phy_lorentz_metric *metric);

phy_cas *phy_lorentz_metric_cas(const phy_lorentz_metric *metric);
const char *phy_lorentz_metric_name(const phy_lorentz_metric *metric);
unsigned phy_lorentz_metric_dimension(const phy_lorentz_metric *metric);
phy_lorentz_signature phy_lorentz_metric_signature(
    const phy_lorentz_metric *metric);
phy_ir_symbol phy_lorentz_metric_space(const phy_lorentz_metric *metric);

/* ---------------------------------------------------------------- indices */

/* An index of this metric's bundle. Distinct variances are distinct nodes. */
phy_status phy_lorentz_index(const phy_lorentz_metric *metric, const char *name,
                             phy_ir_variance variance, phy_ir_ref *out_index);

/* True when `ref` is a PHY_IR_INDEX in this metric's bundle. */
bool phy_lorentz_owns_index(const phy_lorentz_metric *metric, phy_ir_ref ref);

/*
 * The metric with two index slots, canonically ordered because g is symmetric.
 *
 * Two cases are decided rather than deferred. Both slots carrying one name in
 * opposite positions is the trace g^mu_mu, which is the exact integer
 * `dimension`. Both slots carrying one name in the same position is not a
 * tensor and is PHY_ERR_TYPE.
 */
phy_status phy_lorentz_metric_tensor(const phy_lorentz_metric *metric,
                                     phy_ir_ref a, phy_ir_ref b,
                                     phy_ir_ref *out_tensor);

/*
 * Every Lorentz index of this metric occurring in `expr`, with its occurrence
 * counts. Indices of another bundle are ignored; they are another metric's
 * business. PHY_ERR_UNSUPPORTED when more than `capacity` distinct names occur.
 */
phy_status phy_lorentz_index_census(const phy_lorentz_metric *metric,
                                    phy_ir_ref expr,
                                    phy_lorentz_index_use *out_uses,
                                    size_t capacity, size_t *out_count);

/*
 * Rename one index name throughout `expr`, in both positions at once.
 *
 * This is a rename, not a canonicalization: choosing *which* name a dummy
 * should end up with, in a commutative product whose operand order depends on
 * the names themselves, is the abstract dummy-index canonicalization that
 * docs/IR.md and the Phase 2 roadmap entry both leave open. What is decidable
 * is the renaming itself, and inside an ordered gamma string the Dirac layer
 * does canonicalize -- see phy_dirac_canonical().
 */
phy_status phy_lorentz_rename_index(const phy_lorentz_metric *metric,
                                    phy_ir_ref expr, const char *from,
                                    const char *to, phy_ir_ref *out_expr);

/* --------------------------------------------------------------- momenta */

/*
 * Declare a momentum, or return the one already declared under that name.
 *
 * The returned atom is Momentum[name, space]. Declaration is what gives the
 * momentum a slot in phy_lorentz_vector and a place in the conservation and
 * on-shell tables, so an undeclared momentum atom cannot enter this layer.
 */
phy_status phy_lorentz_momentum(phy_lorentz_metric *metric, const char *name,
                                phy_ir_ref *out_momentum);
bool phy_lorentz_owns_momentum(const phy_lorentz_metric *metric,
                               phy_ir_ref ref);
unsigned phy_lorentz_momentum_count(const phy_lorentz_metric *metric);
phy_ir_ref phy_lorentz_momentum_at(const phy_lorentz_metric *metric,
                                   unsigned slot);

/* The component p^a as the ordinary indexed tensor <p>[a]. */
phy_status phy_lorentz_momentum_component(const phy_lorentz_metric *metric,
                                          phy_ir_ref momentum, phy_ir_ref index,
                                          phy_ir_ref *out_component);

/*
 * p.q as LorentzDot[P,Q], operands in the IR's canonical order.
 *
 * The ordering is what makes p.q == q.p structural rather than a theorem: both
 * calls intern the same node, so equality is a ref comparison and no later
 * pass has to know that the dot is symmetric.
 */
phy_status phy_lorentz_dot(const phy_lorentz_metric *metric, phy_ir_ref p,
                           phy_ir_ref q, phy_ir_ref *out_dot);

/* ---------------------------------------------------------------- vectors */

phy_status phy_lorentz_vector_zero(const phy_lorentz_metric *metric,
                                   phy_lorentz_vector *out_vector);
phy_status phy_lorentz_vector_of(const phy_lorentz_metric *metric,
                                 phy_ir_ref momentum,
                                 phy_lorentz_vector *out_vector);
phy_status phy_lorentz_vector_add(const phy_lorentz_vector *left,
                                  const phy_lorentz_vector *right,
                                  phy_lorentz_vector *out_vector);
phy_status phy_lorentz_vector_sub(const phy_lorentz_vector *left,
                                  const phy_lorentz_vector *right,
                                  phy_lorentz_vector *out_vector);
phy_status phy_lorentz_vector_scale(const phy_lorentz_vector *vector,
                                    phy_ir_ref scalar,
                                    phy_lorentz_vector *out_vector);

/*
 * The exact bilinear expansion sum_ij v_i w_j (p_i . p_j).
 *
 * Every generated dot is built through phy_lorentz_dot, so the p_i.p_j and
 * p_j.p_i halves of the double sum land on one node and collect.
 */
phy_status phy_lorentz_vector_dot(const phy_lorentz_vector *left,
                                  const phy_lorentz_vector *right,
                                  phy_ir_ref *out_scalar);

/* ------------------------------------------------ kinematic declarations */

/*
 * Declare a linear momentum relation `relation == 0` and eliminate one
 * momentum with it. Peskin's p1 + p2 - p3 - p4 = 0 eliminating p4, and
 * FeynCalc's all-incoming p1 + p2 + p3 + p4 = 0 eliminating p4, are the same
 * call with different signs.
 *
 * A metric carries at most one relation; declaring a second replaces the
 * first. The eliminated momentum's coefficient must be provably nonzero, or
 * the declaration is PHY_ERR_DOMAIN -- solving for a momentum that may not
 * appear in the relation is not a substitution, it is a guess.
 */
phy_status phy_lorentz_declare_conservation(phy_lorentz_metric *metric,
                                            const phy_lorentz_vector *relation,
                                            phy_ir_ref eliminated);
bool phy_lorentz_has_conservation(const phy_lorentz_metric *metric);

/* Declare p.p = mass^2. Passing PHY_IR_NULL for `mass` withdraws it. */
phy_status phy_lorentz_declare_on_shell(phy_lorentz_metric *metric,
                                        phy_ir_ref momentum, phy_ir_ref mass);

/* Apply the declared conservation relation to a vector. */
phy_status phy_lorentz_reduce_vector(const phy_lorentz_vector *vector,
                                     phy_lorentz_vector *out_vector);

/*
 * Apply conservation and then the on-shell masses to a scalar expression.
 *
 * The order matters and is not the caller's to choose: eliminating a momentum
 * creates new p_i.p_i factors, and those are exactly what the on-shell pass
 * exists to replace.
 */
phy_status phy_lorentz_reduce(const phy_lorentz_metric *metric,
                              phy_ir_ref expr, phy_ir_ref *out_expr);

#ifdef __cplusplus
}
#endif

#endif /* PHY_LORENTZ_H */
