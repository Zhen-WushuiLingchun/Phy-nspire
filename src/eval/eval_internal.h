/*
 * Shared state of the stateful notebook evaluator.
 *
 * Split three ways: env.c owns the object table, the sweep and the bindings;
 * dispatch.c turns typed IR into values; display.c turns values back into
 * something a 320x240 cell can show.
 */
#ifndef PHY_EVAL_INTERNAL_H
#define PHY_EVAL_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/eval.h"

#define EVAL_NO_SLOT ((uint16_t)0xffffu)
#define EVAL_DEPENDENCY_WORDS \
    ((PHY_EVAL_MAX_OBJECTS + 63u) / 64u)

/*
 * Reserved operator heads, interned once at environment creation so that
 * dispatch is an id comparison rather than a string compare per node. The
 * order matches kEvalHeadNames in env.c.
 */
typedef enum {
    EVAL_HEAD_INDEX_SPACE = 0,
    EVAL_HEAD_TENSOR_HEAD,
    EVAL_HEAD_TENSOR_CANONICALIZE,
    EVAL_HEAD_YOUNG_PROJECT,
    EVAL_HEAD_COMPONENT_BASIS,
    EVAL_HEAD_TENSOR_COMPONENTS,
    EVAL_HEAD_COMPONENT_LIFT,
    EVAL_HEAD_COMPONENT_VALUE,

    EVAL_HEAD_VECTOR,
    EVAL_HEAD_MATRIX,
    EVAL_HEAD_TRANSPOSE,
    EVAL_HEAD_DOT,
    EVAL_HEAD_DETERMINANT,
    EVAL_HEAD_INVERSE,
    EVAL_HEAD_ROW_REDUCE,
    EVAL_HEAD_MATRIX_RANK,
    EVAL_HEAD_LINEAR_SOLVE,

    EVAL_HEAD_COORDINATE_MAP,
    EVAL_HEAD_BASIS_TRANSITION,
    EVAL_HEAD_JACOBIAN,
    EVAL_HEAD_PULLBACK_SCALAR,
    EVAL_HEAD_PULLBACK_COVECTOR,
    EVAL_HEAD_PUSHFORWARD_VECTOR,
    EVAL_HEAD_TRANSITION_PULLBACK,
    EVAL_HEAD_ATLAS,
    EVAL_HEAD_ATLAS_ADD_TRANSITION,
    EVAL_HEAD_ATLAS_VERIFY,
    EVAL_HEAD_ATLAS_PULLBACK,

    EVAL_HEAD_MANIFOLD,
    EVAL_HEAD_DIFFERENTIAL_FORM,
    EVAL_HEAD_METRIC,
    EVAL_HEAD_VECTOR_FIELD,
    EVAL_HEAD_COMPONENT_TENSOR,
    EVAL_HEAD_EXTERIOR_D,
    EVAL_HEAD_INTERIOR_PRODUCT,
    EVAL_HEAD_LIE_DERIVATIVE,
    EVAL_HEAD_HODGE_STAR,
    EVAL_HEAD_VOLUME,

    EVAL_HEAD_LIE_GROUP,
    EVAL_HEAD_LIE_ALGEBRA,
    EVAL_HEAD_GENERATOR,
    EVAL_HEAD_LIE_ELEMENT,
    EVAL_HEAD_LIE_BRACKET,
    EVAL_HEAD_STRUCTURE_CONSTANT,
    EVAL_HEAD_KILLING,

    EVAL_HEAD_LIE_FORM,
    EVAL_HEAD_GAUGE_CONNECTION,
    EVAL_HEAD_COVARIANT_D,
    EVAL_HEAD_FIELD_STRENGTH,
    EVAL_HEAD_GAUGE_VARIATION,
    EVAL_HEAD_BIANCHI,
    EVAL_HEAD_YANG_MILLS_LAGRANGIAN,
    EVAL_HEAD_COLOR_COMPONENT,

    EVAL_HEAD_CURVATURE,
    EVAL_HEAD_INVERSE_METRIC,
    EVAL_HEAD_CHRISTOFFEL,
    EVAL_HEAD_RIEMANN,
    EVAL_HEAD_RIEMANN_MIXED,
    EVAL_HEAD_RICCI,
    EVAL_HEAD_RICCI_SCALAR,
    EVAL_HEAD_EINSTEIN,
    EVAL_HEAD_KRETSCHMANN,
    EVAL_HEAD_WEYL,
    EVAL_HEAD_WEYL_SQUARED,
    EVAL_HEAD_GEODESIC_ACCELERATION,
    EVAL_HEAD_COVARIANT_DERIVATIVE,

    EVAL_HEAD_PHI4_LAGRANGIAN,
    EVAL_HEAD_PHI4_EOM,
    EVAL_HEAD_PHI4_DIAGRAMS,
    EVAL_HEAD_PHI4_GRAPH,
    EVAL_HEAD_PHI4_RENORMALIZATION,
    EVAL_HEAD_PHI4_COUNTERTERM,
    EVAL_HEAD_MANDELSTAM_REDUCE,
    EVAL_HEAD_DIRAC_TRACE,

    EVAL_HEAD_SUN_DELTA,
    EVAL_HEAD_SUN_F,
    EVAL_HEAD_SUN_D,
    EVAL_HEAD_SUN_T,
    EVAL_HEAD_SUN_TRACE,
    EVAL_HEAD_SUN_COMMUTATOR,
    EVAL_HEAD_SUN_DELTA_CONTRACT,
    EVAL_HEAD_SUN_CF,
    EVAL_HEAD_SUN_CA,
    EVAL_HEAD_SUN_F_COMPONENT,
    EVAL_HEAD_SUN_EXPAND_CASIMIRS,
    EVAL_HEAD_SUN_FUNDAMENTAL_CASIMIR,
    EVAL_HEAD_SUN_ADJOINT_CASIMIR,

    EVAL_HEAD_COMPONENT,
    EVAL_HEAD_DEGREE,
    EVAL_HEAD_DIMENSION,
    EVAL_HEAD_DIMENSIONS,
    EVAL_HEAD_RANK,
    EVAL_HEAD_ZERO_Q,
    EVAL_HEAD_EQUIVALENT_Q,
    EVAL_HEAD_MEMORY_STATUS,

    EVAL_HEAD_COUNT
} eval_head;

typedef struct {
    phy_value value;   /* const view; identity and consumption */
    void *owned;       /* mutable owning pointer, NULL when borrowed */
    /*
     * A dynamic-rank component realization can borrow one distinct basis per
     * slot. Two fixed dependency fields were therefore a correctness bug, not
     * merely a small limit. A bit set here names an earlier object-table slot;
     * 96 objects cost only 16 bytes per entry and retain arbitrary fan-in.
     */
    uint64_t dependency[EVAL_DEPENDENCY_WORDS];
    bool marked;
} eval_object;

typedef struct {
    phy_ir_symbol name;
    phy_value value;
} eval_binding;

struct phy_env {
    phy_cas *cas;
    phy_ir_context *ir;
    phy_abstract_context *abstract;

    phy_ir_symbol head[EVAL_HEAD_COUNT];
    phy_ir_symbol list_head; /* the `{...}` constructor the parser emits */

    eval_object objects[PHY_EVAL_MAX_OBJECTS];
    size_t object_count;

    eval_binding bindings[PHY_EVAL_MAX_BINDINGS];
    size_t binding_count;

    /*
     * The name the current assignment is binding, or PHY_IR_NO_SYMBOL. Object
     * constructors read it so that `M = Manifold[...]` produces a manifold
     * whose own head symbol is `M`, which is what makes a descriptor line name
     * the object the reader named rather than a placeholder.
     */
    phy_ir_symbol pending_name;
};

/* --------------------------------------------------------------- env.c */

const void *eval_value_pointer(const phy_value *value);

/*
 * Take ownership of a freshly created object. `owned` is the mutable pointer
 * to destroy, or NULL for a borrowed interior pointer. The dependencies are
 * values already registered; passing one that is not registered is
 * PHY_ERR_CORRUPT_DOCUMENT, because a dependency that cannot be found is
 * exactly the case in which the sweep would free something still in use.
 *
 * On failure the object is destroyed, so a caller never has to unwind.
 */
phy_status eval_register(phy_env *env, phy_value value, void *owned,
                         const phy_value *first, const phy_value *second);

/*
 * The general registration path. Dependencies may repeat; each distinct live
 * object is retained once. On failure `owned` is destroyed exactly as in
 * eval_register.
 */
phy_status eval_register_many(phy_env *env, phy_value value, void *owned,
                              const phy_value *dependencies,
                              size_t dependency_count);

/* Destroy every object not reachable from a binding or from `keep`. */
void eval_sweep(phy_env *env, const phy_value *keep);

phy_status eval_bind(phy_env *env, phy_ir_symbol name, phy_value value);
void eval_unbind(phy_env *env, phy_ir_symbol name);
bool eval_lookup(const phy_env *env, phy_ir_symbol name, phy_value *out_value);

/* True when `name` is a coordinate axis of a chart the environment holds. */
bool eval_is_chart_coordinate(const phy_env *env, phy_ir_symbol name);

/* ---------------------------------------------------------- dispatch.c */

phy_status eval_node(phy_env *env, phy_ir_ref expr, phy_value *out_value);

#endif /* PHY_EVAL_INTERNAL_H */
