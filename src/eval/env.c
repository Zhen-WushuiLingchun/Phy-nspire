/*
 * The notebook environment: object ownership, the reachability sweep, and the
 * name table.
 *
 * Everything here exists because the physics layers below have no garbage
 * collector and a strict destruction order. A form must die before its
 * manifold, a manifold before its chart. This file keeps that order true
 * without asking the evaluator to think about it: objects are appended in
 * creation order, dependencies always point backwards, and destruction runs
 * from the end.
 */
#include <string.h>

#include "phy/platform.h"

#include "eval_internal.h"

static const char *const kEvalHeadNames[EVAL_HEAD_COUNT] = {
    "IndexSpace",   "TensorHead",       "TensorCanonicalize",
    "YoungProject", "YoungDeclare",     "YoungReduce",
    "GarnirRelation", "YoungDimension",
    "ComponentBasis",   "TensorComponents",
    "ComponentLift", "ComponentValue",

    "GRComponents", "GRSpace",          "GRBasis",
    "GRHead",       "GRTensor",

    "QFTSystem",    "QFTSpace",         "QFTBasis",
    "QFTHead",      "QFTTensor",

    "Vector",       "Matrix",           "Transpose",   "Dot",
    "Determinant",  "Inverse",          "RowReduce",   "MatrixRank",
    "LinearSolve",

    "CoordinateMap", "BasisTransition",  "Jacobian",
    "PullbackScalar", "PullbackCovector", "PushForwardVector",
    "TransitionPullback", "Atlas",       "AtlasAddTransition",
    "AtlasVerify",  "AtlasPullback",

    "Manifold",     "DifferentialForm", "Metric",      "VectorField",
    "ComponentTensor",
    "ExteriorD",    "InteriorProduct",  "LieDerivative",
    "HodgeStar",    "Volume",

    "LieGroup",     "LieAlgebra",       "Generator",   "LieElement",
    "LieBracket",   "StructureConstant", "Killing",

    "LieForm",      "GaugeConnection",  "CovariantD",  "FieldStrength",
    "GaugeVariation", "Bianchi",        "YangMillsLagrangian",
    "ColorComponent",

    "Curvature",    "InverseMetric",    "Christoffel", "Riemann",
    "RiemannMixed", "Ricci",            "RicciScalar", "Einstein",
    "Kretschmann",  "Weyl",             "WeylSquared",
    "GeodesicAcceleration",             "CovariantDerivative",

    "Phi4Lagrangian", "Phi4EOM",         "Phi4Diagrams", "Phi4Graph",
    "Phi4Renormalization", "Phi4Counterterm",
    "MandelstamReduce", "DiracTrace",

    "SUNDelta",      "SUNF",             "SUND",
    "SUNT",          "SUNTrace",         "SUNCommutator",
    "SUNDeltaContract", "SUNCF",         "SUNCA",
    "SUNFComponent", "SUNExpandCasimirs", "SUNFundamentalCasimir",
    "SUNAdjointCasimir",

    "Component",    "Degree",           "Dimension",   "Dimensions",
    "Rank",
    "ZeroQ",        "EquivalentQ",      "MemoryStatus",
};

size_t phy_eval_head_count(void)
{
    return (size_t)EVAL_HEAD_COUNT;
}

const char *phy_eval_head_name(size_t index)
{
    return index < (size_t)EVAL_HEAD_COUNT ? kEvalHeadNames[index] : NULL;
}

const char *phy_value_kind_name(phy_value_kind kind)
{
    switch (kind) {
    case PHY_VALUE_NONE:
        return "None";
    case PHY_VALUE_SCALAR:
        return "Scalar";
    case PHY_VALUE_CHART:
        return "Chart";
    case PHY_VALUE_MANIFOLD:
        return "Manifold";
    case PHY_VALUE_TENSOR:
        return "Tensor";
    case PHY_VALUE_FORM:
        return "Form";
    case PHY_VALUE_LIE_GROUP:
        return "LieGroup";
    case PHY_VALUE_LIE_ALGEBRA:
        return "LieAlgebra";
    case PHY_VALUE_LIE_ELEMENT:
        return "LieElement";
    case PHY_VALUE_LIE_FORM:
        return "LieForm";
    case PHY_VALUE_CURVATURE:
        return "Curvature";
    case PHY_VALUE_INDEX_SPACE:
        return "IndexSpace";
    case PHY_VALUE_TENSOR_HEAD:
        return "TensorHead";
    case PHY_VALUE_ABSTRACT_TENSOR:
        return "AbstractTensor";
    case PHY_VALUE_ABSTRACT_EXPRESSION:
        return "AbstractExpression";
    case PHY_VALUE_COMPONENT_BASIS:
        return "ComponentBasis";
    case PHY_VALUE_COMPONENT_TENSOR:
        return "TensorComponents";
    case PHY_VALUE_VECTOR:
        return "Vector";
    case PHY_VALUE_MATRIX:
        return "Matrix";
    case PHY_VALUE_COORDINATE_MAP:
        return "CoordinateMap";
    case PHY_VALUE_BASIS_TRANSITION:
        return "BasisTransition";
    case PHY_VALUE_ATLAS:
        return "Atlas";
    case PHY_VALUE_GR_COMPONENTS:
        return "GRComponents";
    case PHY_VALUE_QFT_COMPONENTS:
        return "QFTSystem";
    default:
        break;
    }
    return "Invalid";
}

const void *eval_value_pointer(const phy_value *value)
{
    if (value == NULL) {
        return NULL;
    }
    switch (value->kind) {
    case PHY_VALUE_CHART:
        return value->as.chart;
    case PHY_VALUE_MANIFOLD:
        return value->as.manifold;
    case PHY_VALUE_TENSOR:
        return value->as.tensor;
    case PHY_VALUE_FORM:
        return value->as.form;
    case PHY_VALUE_LIE_GROUP:
        return value->as.group;
    case PHY_VALUE_LIE_ALGEBRA:
        return value->as.algebra;
    case PHY_VALUE_LIE_ELEMENT:
        return value->as.element;
    case PHY_VALUE_LIE_FORM:
        return value->as.lie_form;
    case PHY_VALUE_CURVATURE:
        return value->as.curvature;
    case PHY_VALUE_INDEX_SPACE:
        return value->as.index_space;
    case PHY_VALUE_TENSOR_HEAD:
        return value->as.tensor_head;
    case PHY_VALUE_ABSTRACT_TENSOR:
        return value->as.abstract_tensor;
    case PHY_VALUE_ABSTRACT_EXPRESSION:
        return value->as.abstract_expression;
    case PHY_VALUE_COMPONENT_BASIS:
        return value->as.component_basis;
    case PHY_VALUE_COMPONENT_TENSOR:
        return value->as.component_tensor;
    case PHY_VALUE_VECTOR:
        return value->as.vector;
    case PHY_VALUE_MATRIX:
        return value->as.matrix;
    case PHY_VALUE_COORDINATE_MAP:
        return value->as.coordinate_map;
    case PHY_VALUE_BASIS_TRANSITION:
        return value->as.basis_transition;
    case PHY_VALUE_GR_COMPONENTS:
        return value->as.gr_components;
    case PHY_VALUE_QFT_COMPONENTS:
        return value->as.qft_components;
    case PHY_VALUE_ATLAS:
        return value->as.atlas;
    default:
        break;
    }
    return NULL;
}

static void destroy_owned(phy_value_kind kind, void *owned)
{
    if (owned == NULL) {
        return;
    }
    switch (kind) {
    case PHY_VALUE_CHART:
        phy_chart_destroy((phy_chart *)owned);
        break;
    case PHY_VALUE_MANIFOLD:
        phy_manifold_destroy((phy_manifold *)owned);
        break;
    case PHY_VALUE_TENSOR:
        phy_tensor_destroy((phy_tensor *)owned);
        break;
    case PHY_VALUE_FORM:
        phy_form_destroy((phy_form *)owned);
        break;
    case PHY_VALUE_LIE_GROUP:
        phy_lie_group_destroy((phy_lie_group *)owned);
        break;
    case PHY_VALUE_LIE_ELEMENT:
        phy_lie_element_destroy((phy_lie_element *)owned);
        break;
    case PHY_VALUE_LIE_FORM:
        phy_lie_form_destroy((phy_lie_form *)owned);
        break;
    case PHY_VALUE_CURVATURE:
        phy_gr_result_destroy((phy_gr_result *)owned);
        break;
    case PHY_VALUE_ABSTRACT_TENSOR:
        phy_tensor_monomial_destroy(
            (phy_tensor_monomial *)owned);
        break;
    case PHY_VALUE_ABSTRACT_EXPRESSION:
        phy_tensor_expression_destroy(
            (phy_tensor_expression *)owned);
        break;
    case PHY_VALUE_COMPONENT_BASIS:
        phy_component_basis_destroy(
            (phy_component_basis *)owned);
        break;
    case PHY_VALUE_COMPONENT_TENSOR:
        phy_component_tensor_destroy(
            (phy_component_tensor *)owned);
        break;
    case PHY_VALUE_VECTOR:
    case PHY_VALUE_MATRIX:
        phy_matrix_destroy((phy_matrix *)owned);
        break;
    case PHY_VALUE_COORDINATE_MAP:
        phy_coordinate_map_destroy(
            (phy_coordinate_map *)owned);
        break;
    case PHY_VALUE_BASIS_TRANSITION:
        phy_basis_transition_destroy(
            (phy_basis_transition *)owned);
        break;
    case PHY_VALUE_ATLAS:
        phy_atlas_destroy((phy_atlas *)owned);
        break;
    case PHY_VALUE_GR_COMPONENTS:
        phy_gr_component_view_destroy(
            (phy_gr_component_view *)owned);
        break;
    case PHY_VALUE_QFT_COMPONENTS:
        phy_qft_component_view_destroy(
            (phy_qft_component_view *)owned);
        break;
    default:
        /* Scalars and borrowed algebras never own anything. */
        break;
    }
}

static size_t slot_of(const phy_env *env, const phy_value *value)
{
    const void *pointer = eval_value_pointer(value);
    if (pointer == NULL) {
        return (size_t)EVAL_NO_SLOT;
    }
    for (size_t i = 0u; i < env->object_count; ++i) {
        if (eval_value_pointer(&env->objects[i].value) == pointer) {
            return i;
        }
    }
    return (size_t)EVAL_NO_SLOT;
}

phy_env *phy_env_create(phy_cas *cas)
{
    if (cas == NULL) {
        return NULL;
    }
    phy_ir_context *ir = phy_cas_ir(cas);
    if (ir == NULL) {
        return NULL;
    }
    phy_env *env = phy_alloc(sizeof *env);
    if (env == NULL) {
        return NULL;
    }
    memset(env, 0, sizeof *env);
    env->cas = cas;
    env->ir = ir;
    env->pending_name = PHY_IR_NO_SYMBOL;
    for (size_t i = 0u; i < (size_t)EVAL_HEAD_COUNT; ++i) {
        env->head[i] = phy_ir_intern(ir, kEvalHeadNames[i]);
        if (env->head[i] == PHY_IR_NO_SYMBOL) {
            phy_free(env, sizeof *env);
            return NULL;
        }
    }
    env->list_head = phy_ir_intern(ir, "List");
    if (env->list_head == PHY_IR_NO_SYMBOL) {
        phy_free(env, sizeof *env);
        return NULL;
    }
    return env;
}

void phy_env_reset(phy_env *env)
{
    if (env == NULL) {
        return;
    }
    env->binding_count = 0u;
    for (size_t i = env->object_count; i-- > 0u;) {
        destroy_owned(env->objects[i].value.kind, env->objects[i].owned);
    }
    env->object_count = 0u;
    env->pending_name = PHY_IR_NO_SYMBOL;
    phy_abstract_context_destroy(env->abstract);
    env->abstract = NULL;
}

void phy_env_destroy(phy_env *env)
{
    if (env == NULL) {
        return;
    }
    phy_env_reset(env);
    phy_free(env, sizeof *env);
}

phy_cas *phy_env_cas(const phy_env *env)
{
    return env != NULL ? env->cas : NULL;
}

phy_ir_context *phy_env_ir(const phy_env *env)
{
    return env != NULL ? env->ir : NULL;
}

size_t phy_env_object_count(const phy_env *env)
{
    return env != NULL ? env->object_count : 0u;
}

phy_status eval_register_many(phy_env *env, phy_value value, void *owned,
                              const phy_value *dependencies,
                              size_t dependency_count)
{
    if (env == NULL || eval_value_pointer(&value) == NULL) {
        destroy_owned(value.kind, owned);
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (env->object_count >= PHY_EVAL_MAX_OBJECTS) {
        destroy_owned(value.kind, owned);
        return PHY_ERR_TERM_LIMIT;
    }

    if (dependency_count != 0u && dependencies == NULL) {
        destroy_owned(value.kind, owned);
        return PHY_ERR_INVALID_ARGUMENT;
    }
    uint64_t dependency[EVAL_DEPENDENCY_WORDS];
    memset(dependency, 0, sizeof dependency);
    for (size_t i = 0u; i < dependency_count; ++i) {
        if (dependencies[i].kind == PHY_VALUE_NONE ||
            dependencies[i].kind == PHY_VALUE_SCALAR) {
            continue;
        }
        const size_t slot = slot_of(env, &dependencies[i]);
        if (slot == (size_t)EVAL_NO_SLOT) {
            destroy_owned(value.kind, owned);
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        dependency[slot / 64u] |= UINT64_C(1) << (slot % 64u);
    }

    eval_object *entry = &env->objects[env->object_count];
    memset(entry, 0, sizeof *entry);
    entry->value = value;
    entry->owned = owned;
    memcpy(entry->dependency, dependency, sizeof dependency);
    env->object_count++;
    return PHY_OK;
}

phy_status eval_register(phy_env *env, phy_value value, void *owned,
                         const phy_value *first, const phy_value *second)
{
    phy_value dependencies[2];
    size_t count = 0u;
    if (first != NULL) {
        dependencies[count++] = *first;
    }
    if (second != NULL) {
        dependencies[count++] = *second;
    }
    return eval_register_many(env, value, owned, dependencies, count);
}

/*
 * Mark, destroy, compact.
 *
 * A dependency always names a lower slot, so one downward pass propagates the
 * whole closure: by the time slot i is examined, every slot above it that could
 * have marked it has been. That is the property creation-order registration
 * buys, and it is why compaction below preserves order rather than filling
 * holes with whatever is at the end.
 */
void eval_sweep(phy_env *env, const phy_value *keep)
{
    if (env == NULL) {
        return;
    }
    for (size_t i = 0u; i < env->object_count; ++i) {
        env->objects[i].marked = false;
    }
    for (size_t i = 0u; i < env->binding_count; ++i) {
        const size_t slot = slot_of(env, &env->bindings[i].value);
        if (slot != (size_t)EVAL_NO_SLOT) {
            env->objects[slot].marked = true;
        }
    }
    if (keep != NULL) {
        const size_t slot = slot_of(env, keep);
        if (slot != (size_t)EVAL_NO_SLOT) {
            env->objects[slot].marked = true;
        }
    }
    for (size_t i = env->object_count; i-- > 0u;) {
        if (!env->objects[i].marked) {
            continue;
        }
        for (size_t slot = 0u; slot < i; ++slot) {
            if ((env->objects[i].dependency[slot / 64u] &
                 (UINT64_C(1) << (slot % 64u))) != 0u) {
                env->objects[slot].marked = true;
            }
        }
    }

    for (size_t i = env->object_count; i-- > 0u;) {
        if (!env->objects[i].marked) {
            destroy_owned(env->objects[i].value.kind, env->objects[i].owned);
        }
    }

    const size_t previous_count = env->object_count;
    uint16_t remap[PHY_EVAL_MAX_OBJECTS];
    size_t surviving = 0u;
    for (size_t i = 0u; i < env->object_count; ++i) {
        if (env->objects[i].marked) {
            remap[i] = (uint16_t)surviving;
            env->objects[surviving] = env->objects[i];
            surviving++;
        } else {
            remap[i] = EVAL_NO_SLOT;
        }
    }
    env->object_count = surviving;
    for (size_t i = 0u; i < env->object_count; ++i) {
        uint64_t mapped[EVAL_DEPENDENCY_WORDS];
        memset(mapped, 0, sizeof mapped);
        for (size_t old = 0u; old < previous_count; ++old) {
            if ((env->objects[i].dependency[old / 64u] &
                 (UINT64_C(1) << (old % 64u))) == 0u) {
                continue;
            }
            const uint16_t replacement = remap[old];
            if (replacement != EVAL_NO_SLOT) {
                mapped[replacement / 64u] |=
                    UINT64_C(1) << (replacement % 64u);
            }
        }
        memcpy(env->objects[i].dependency, mapped, sizeof mapped);
    }
}

bool eval_is_chart_coordinate(const phy_env *env, phy_ir_symbol name)
{
    if (env == NULL || name == PHY_IR_NO_SYMBOL) {
        return false;
    }
    for (size_t i = 0u; i < env->object_count; ++i) {
        const phy_value *value = &env->objects[i].value;
        if (value->kind == PHY_VALUE_CHART) {
            if (phy_chart_axis_of(value->as.chart, name) !=
                PHY_CHART_NO_AXIS) {
                return true;
            }
        } else if (value->kind == PHY_VALUE_COMPONENT_BASIS) {
            const phy_component_basis *basis =
                value->as.component_basis;
            const size_t dimension =
                phy_component_basis_dimension(basis);
            for (size_t axis = 0u; axis < dimension; ++axis) {
                if (phy_component_basis_coordinate_symbol(
                        basis, axis) == name) {
                    return true;
                }
            }
        }
    }
    return false;
}

phy_status eval_bind(phy_env *env, phy_ir_symbol name, phy_value value)
{
    if (env == NULL || name == PHY_IR_NO_SYMBOL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (eval_is_chart_coordinate(env, name)) {
        return PHY_ERR_ASSUMPTION;
    }
    for (size_t i = 0u; i < env->binding_count; ++i) {
        if (env->bindings[i].name == name) {
            env->bindings[i].value = value;
            return PHY_OK;
        }
    }
    if (env->binding_count >= PHY_EVAL_MAX_BINDINGS) {
        return PHY_ERR_TERM_LIMIT;
    }
    env->bindings[env->binding_count].name = name;
    env->bindings[env->binding_count].value = value;
    env->binding_count++;
    return PHY_OK;
}

void eval_unbind(phy_env *env, phy_ir_symbol name)
{
    if (env == NULL) {
        return;
    }
    for (size_t i = 0u; i < env->binding_count; ++i) {
        if (env->bindings[i].name != name) {
            continue;
        }
        for (size_t j = i + 1u; j < env->binding_count; ++j) {
            env->bindings[j - 1u] = env->bindings[j];
        }
        env->binding_count--;
        return;
    }
}

bool eval_lookup(const phy_env *env, phy_ir_symbol name, phy_value *out_value)
{
    if (env == NULL || name == PHY_IR_NO_SYMBOL) {
        return false;
    }
    for (size_t i = 0u; i < env->binding_count; ++i) {
        if (env->bindings[i].name == name) {
            if (out_value != NULL) {
                *out_value = env->bindings[i].value;
            }
            return true;
        }
    }
    return false;
}

size_t phy_env_binding_count(const phy_env *env)
{
    return env != NULL ? env->binding_count : 0u;
}

bool phy_env_binding(const phy_env *env, size_t index, const char **out_name,
                     phy_value *out_value)
{
    if (env == NULL || index >= env->binding_count) {
        return false;
    }
    if (out_name != NULL) {
        *out_name = phy_ir_symbol_name(env->ir, env->bindings[index].name);
    }
    if (out_value != NULL) {
        *out_value = env->bindings[index].value;
    }
    return true;
}

bool phy_env_lookup(const phy_env *env, const char *name, phy_value *out_value)
{
    if (env == NULL || name == NULL) {
        return false;
    }
    /*
     * Interning a name to look it up would grow the symbol table on every miss,
     * which a UI that polls bindings would do forever. Compare the bytes.
     */
    for (size_t i = 0u; i < env->binding_count; ++i) {
        const char *bound = phy_ir_symbol_name(env->ir, env->bindings[i].name);
        if (bound != NULL && strcmp(bound, name) == 0) {
            if (out_value != NULL) {
                *out_value = env->bindings[i].value;
            }
            return true;
        }
    }
    return false;
}

phy_status phy_env_validate(const phy_env *env)
{
    if (env == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (env->object_count > PHY_EVAL_MAX_OBJECTS ||
        env->binding_count > PHY_EVAL_MAX_BINDINGS) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    for (size_t i = 0u; i < env->object_count; ++i) {
        const eval_object *entry = &env->objects[i];
        const void *pointer = eval_value_pointer(&entry->value);
        if (pointer == NULL) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        if (entry->owned != NULL && entry->owned != pointer) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        for (size_t j = 0u; j < i; ++j) {
            if (eval_value_pointer(&env->objects[j].value) == pointer) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
        }
        for (size_t slot = i; slot < PHY_EVAL_MAX_OBJECTS; ++slot) {
            if ((entry->dependency[slot / 64u] &
                 (UINT64_C(1) << (slot % 64u))) != 0u) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
        }
    }
    for (size_t i = 0u; i < env->binding_count; ++i) {
        const phy_value *value = &env->bindings[i].value;
        if (value->kind == PHY_VALUE_SCALAR || value->kind == PHY_VALUE_NONE) {
            continue;
        }
        if (slot_of(env, value) == (size_t)EVAL_NO_SLOT) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        for (size_t j = 0u; j < i; ++j) {
            if (env->bindings[j].name == env->bindings[i].name) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
        }
    }
    return PHY_OK;
}
