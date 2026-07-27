/*
 * Phy-nspire — the stateful notebook evaluator.
 *
 * Every layer below this one is a pure library: an IR context builds graphs, a
 * CAS rewrites them, a manifold holds charts, a Lie form holds components. None
 * of them has a notion of "the name the reader gave this object", and none of
 * them outlives one call. This header is where a notebook acquires that notion,
 * and it is the reason the geometry, Lie-algebra and Yang--Mills layers stop
 * being unreachable from the product.
 *
 * WHAT WAS WRONG BEFORE
 *
 * docs/SOURCE_LANGUAGE.md accepted `ExteriorD[omega]`, `FieldStrength[A,g]` and
 * a dozen more physics spellings, parsed them into PHY_IR_OPERATOR nodes, and
 * then handed them to the scalar CAS -- which, by its own documented contract,
 * simplifies an operator's operands and leaves the node alone. The head
 * survived a round trip and nothing computed. A reader could not tell that
 * apart from a working evaluator except by knowing the answer already, which is
 * the worst failure mode a symbolic system has.
 *
 * This layer replaces head preservation with dispatch. `ExteriorD[omega]` calls
 * phy_form_exterior_derivative. `FieldStrength[A,g]` calls
 * phy_yang_mills_field_strength. A head this layer knows and cannot evaluate is
 * a typed error, never a tidy no-op.
 *
 * WHY IT MUST BE STATEFUL
 *
 * The objects those backends operate on cannot be written as one expression.
 * A manifold is dimension, orientation, signature and a chart of named
 * coordinates; a connection is d algebra components each holding C(n,1) form
 * components. Nothing in the typed IR represents them, and nothing should: the
 * IR is a graph of values, and these are objects with identity that several
 * cells share. So the notebook keeps an environment -- names bound to typed
 * values that outlive the cell that produced them -- and a cell is evaluated
 * *against* it:
 *
 *     M = Manifold[{t,x,y,z}, Lorentzian]
 *     A = GaugeConnection[LieAlgebra[LieGroup[SU2]], M, {{...},{...},{...}}]
 *     F = FieldStrength[A, g]
 *     ZeroQ[Bianchi[A, g]]
 *
 * Each line is one cell. The third cannot be written at all without the first
 * two having happened, which is exactly what "stateful" buys.
 *
 * The environment is per-notebook and is NOT serialized. A saved document
 * stores cell sources and results, and reopening one starts with an empty
 * environment, so a cell that reads a binding fails until the notebook is
 * re-run in order -- phy_notebook_evaluate_all does that. Objects hold pointers
 * into an IR context and into each other; writing them to a file would mean
 * inventing a second, weaker serialization of every physics layer, and reading
 * one back would mean trusting it. Re-running is cheap, exact, and cannot
 * disagree with the source.
 *
 * OWNERSHIP
 *
 * The environment owns every object it creates and destroys them in reverse
 * creation order, which is the order the layers below require (forms before
 * manifolds, manifolds before charts, everything before the IR context).
 *
 * Evaluating a cell creates intermediates -- `HodgeStar[Wedge[a,b]]` builds a
 * wedge nobody names. After each command the environment sweeps: every object
 * reachable from a binding or from the command's own result survives, the rest
 * are destroyed newest-first. Reachability follows recorded dependencies, so
 * binding a form keeps its manifold alive even when the manifold's own name was
 * overwritten in the same cell. The surviving objects are then compacted with
 * their order preserved, which is what keeps "created later" and "destroyed
 * first" the same statement.
 *
 * A phy_value handed back by phy_eval_command borrows the environment. It is
 * valid until the next phy_eval_command, phy_env_reset or phy_env_destroy on
 * that environment. Convert it to something durable -- an expression, a
 * descriptor -- before the next call, or bind it.
 *
 * COORDINATE CAPTURE
 *
 * Binding a name that a live chart uses as a coordinate is PHY_ERR_ASSUMPTION,
 * and so is creating a chart whose coordinate is already bound. Without that
 * rule `x = 2` silently rewrites every component of every form on a chart with
 * an `x` axis, and the wrong answer is indistinguishable from the right one.
 * The scalar substitution below is otherwise unrestricted, which is why this
 * one case is closed rather than documented as a hazard.
 *
 * ERRORS
 *
 * Typed values, uniform across the API:
 *
 *   PHY_ERR_INVALID_ARGUMENT  a null pointer, or a malformed command;
 *   PHY_ERR_PARSE             an argument list this layer cannot read as the
 *                             head's shape;
 *   PHY_ERR_TYPE              an argument evaluated to the wrong kind of value,
 *                             or a bound object appeared in scalar arithmetic;
 *   PHY_ERR_UNSUPPORTED       a reserved head with no evaluator yet;
 *   PHY_ERR_ASSUMPTION        coordinate capture, or a backend's own
 *                             assumption failure;
 *   PHY_ERR_TERM_LIMIT        the binding or object table is full;
 *   PHY_ERR_DOMAIN,
 *   PHY_ERR_TIMEOUT, ...      propagated unchanged from the CAS and the
 *                             physics layers.
 *
 * A failed command leaves the environment exactly as it was: bindings are
 * written only after the whole right-hand side has evaluated, and the sweep
 * runs on both paths.
 */
#ifndef PHY_EVAL_H
#define PHY_EVAL_H

#include <stdbool.h>
#include <stddef.h>

#include "phy/cas.h"
#include "phy/geom.h"
#include "phy/gr.h"
#include "phy/ir.h"
#include "phy/lie.h"
#include "phy/phy.h"
#include "phy/source.h"
#include "phy/tensor.h"
#include "phy/yang_mills.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compiled ceilings.
 *
 * Both are storage constants. The object table is sized for the intermediates
 * a nested gauge expression builds between two sweeps, not for a workload:
 * `YangMillsLagrangian[FieldStrength[A,g], gmetric]` registers a curvature, a
 * Hodge dual per colour and a top-degree form, and the deepest shape the
 * grammar admits stays well under this.
 */
#define PHY_EVAL_MAX_OBJECTS 96u
#define PHY_EVAL_MAX_BINDINGS 32u

/* Bytes, including the terminator, that phy_eval_describe never exceeds. */
#define PHY_EVAL_DESCRIPTION_CAPACITY 96u

typedef struct phy_env phy_env;

/*
 * What a cell can evaluate to.
 *
 * PHY_VALUE_NONE is zero-valued and means "no value", which is what `Clear`
 * and a failed command produce. PHY_VALUE_CHART is never the value of an
 * expression: charts exist only inside manifolds, because a chart this layer
 * cannot relate to another chart is not an object a reader has any use for.
 * It is a value kind so that the object table needs no second tag.
 */
typedef enum {
    PHY_VALUE_NONE = 0,
    PHY_VALUE_SCALAR,
    PHY_VALUE_CHART,
    PHY_VALUE_MANIFOLD,
    PHY_VALUE_TENSOR,
    PHY_VALUE_FORM,
    PHY_VALUE_LIE_GROUP,
    PHY_VALUE_LIE_ALGEBRA,
    PHY_VALUE_LIE_ELEMENT,
    PHY_VALUE_LIE_FORM,
    PHY_VALUE_CURVATURE
} phy_value_kind;

/*
 * A tagged value. Only the member selected by `kind` is meaningful.
 *
 * Members are const views, because several of them are interior pointers the
 * environment does not own: `Ricci[c]` borrows a tensor from a curvature
 * bundle and `LieAlgebra[G]` borrows an algebra from its group. The owning
 * pointer lives in the environment's object table, which is what destroys it;
 * a value is for reading and for identity.
 *
 * Charts and manifolds are the exception and stay mutable. Not by preference:
 * phy_tensor_create takes a mutable chart and phy_form_create a mutable
 * manifold, because creating an object *on* one of them registers with it. A
 * const view of a manifold cannot carry a form, so this layer would only be
 * casting the qualifier away at every construction site.
 */
typedef struct {
    phy_value_kind kind;
    union {
        phy_ir_ref scalar;
        phy_chart *chart;
        phy_manifold *manifold;
        const phy_tensor *tensor;
        const phy_form *form;
        const phy_lie_group *group;
        const phy_lie_algebra *algebra;
        const phy_lie_element *element;
        const phy_lie_form *lie_form;
        const phy_gr_result *curvature;
    } as;
} phy_value;

/* Stable, allocation-free spelling of a kind. Never returns NULL. */
const char *phy_value_kind_name(phy_value_kind kind);

/* ------------------------------------------------------------- environment */

/*
 * Create an environment over `cas`. The CAS and its IR context are borrowed and
 * must outlive the environment. Returns NULL if `cas` is NULL or the
 * allocation fails.
 */
phy_env *phy_env_create(phy_cas *cas);
void phy_env_destroy(phy_env *env);

/* Drop every binding and destroy every object, newest first. */
void phy_env_reset(phy_env *env);

phy_cas *phy_env_cas(const phy_env *env);
phy_ir_context *phy_env_ir(const phy_env *env);

/* Bindings in creation order; rebinding a name keeps its position. */
size_t phy_env_binding_count(const phy_env *env);
bool phy_env_binding(const phy_env *env, size_t index, const char **out_name,
                     phy_value *out_value);
bool phy_env_lookup(const phy_env *env, const char *name,
                    phy_value *out_value);

/* Live objects the environment owns; a diagnostic, and what the tests watch. */
size_t phy_env_object_count(const phy_env *env);

/*
 * Internal invariants: every dependency points at a lower live slot, every
 * binding names a live object or a scalar, and no object appears twice.
 * PHY_OK or PHY_ERR_CORRUPT_DOCUMENT.
 *
 * Every command, including one that failed part-way, must leave an environment
 * validating; that is how the tests show the failure paths sweep rather than
 * asserting that they do.
 */
phy_status phy_env_validate(const phy_env *env);

/* -------------------------------------------------------------- evaluation */

/*
 * Evaluate one parsed command against `env`.
 *
 * PHY_SOURCE_ASSIGN binds `command->target` and returns the bound value.
 * PHY_SOURCE_CLEAR unbinds one name, or every name when the target is
 * PHY_IR_NO_SYMBOL, and returns PHY_VALUE_NONE. Every other operation
 * evaluates `command->expression` and then applies the scalar operation, which
 * requires a scalar result -- `Expand[M]` on a manifold is PHY_ERR_TYPE rather
 * than a silently ignored request.
 *
 * `*out_value` is written only on PHY_OK and borrows the environment; see the
 * ownership note at the top of this header.
 */
phy_status phy_eval_command(phy_env *env, const phy_source_command *command,
                            phy_value *out_value);

/*
 * Evaluate one expression against `env`, without the command layer. This is
 * what the recursive argument evaluation uses and what a caller wanting a
 * value rather than a cell result should call.
 */
phy_status phy_eval_expression(phy_env *env, phy_ir_ref expression,
                               phy_value *out_value);

/* ----------------------------------------------------------- presentation */

/*
 * The typed IR a value displays as, or PHY_IR_NULL when it has none.
 *
 * This is real mathematics, not a label. A form becomes its coordinate-coframe
 * expansion,
 *
 *     sum over i_0 < ... < i_{p-1} of alpha_{i_0...i_{p-1}}
 *                                     dx^{i_0} ^ ... ^ dx^{i_{p-1}},
 *
 * built from the chart's coordinate names with a `d` prefix -- a chart on
 * (r, theta) spans its 1-forms with the symbols `dr` and `dtheta`. Those are
 * ordinary interned symbols, so a document that also uses `dr` as a scalar
 * will see them collide; the alternative was a coframe node kind that the
 * scalar CAS would then have to be taught to ignore.
 *
 * An algebra-valued form becomes the noncommutative sum
 * `sum_a T_a . (expansion of its colour component)`, with `T_a` the algebra's
 * own basis names. A Lie element becomes the ordinary sum `sum_a c_a T_a`,
 * commutative because the coefficients are scalars.
 *
 * A rank-0 tensor is its component; a rank-1 or rank-2 tensor is a `List` of
 * rows, which is the largest shape worth reading on a 320x240 screen. Manifolds,
 * groups, algebras, curvature bundles and rank-3+ tensors have no expansion and
 * yield PHY_IR_NULL -- phy_eval_describe is what presents those.
 *
 * Zero components are dropped from every sum, and an entirely zero object
 * yields the exact integer 0 rather than an empty sum.
 */
phy_status phy_eval_value_expression(phy_env *env, phy_value value,
                                     phy_ir_ref *out_ref);

/*
 * A one-line description of a value: kind, size, and the structural facts that
 * distinguish two objects of the same kind. Always NUL-terminated, always
 * written, never longer than PHY_EVAL_DESCRIPTION_CAPACITY including the
 * terminator. `capacity` smaller than that is PHY_ERR_INVALID_ARGUMENT rather
 * than a truncation, because a truncated description of a physics object is
 * how the wrong object gets reported as the right one.
 */
phy_status phy_eval_describe(const phy_env *env, phy_value value, char *buffer,
                             size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* PHY_EVAL_H */
