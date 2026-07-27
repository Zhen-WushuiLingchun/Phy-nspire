/*
 * Typed IR to typed value.
 *
 * This is the file that makes the difference the phase is about. Before it,
 * `ExteriorD[omega]` reached the scalar CAS, which simplified the operand and
 * returned the node -- a round trip that looks like a computation. Here the
 * head is looked up in a table interned at environment creation and the call
 * lands on phy_form_exterior_derivative.
 *
 * Two structural rules keep that honest:
 *
 *   every reserved head this file knows is either evaluated or a typed error;
 *   nothing is turned back into an inert operator with the same head.
 *
 * The heads of the bounded scalar-QFT layer -- ScalarField, Propagator, Vertex,
 * and the two master integrals -- are deliberately NOT in the table. They fall
 * through to the generic path, keep constructing typed IR for display, and say
 * so in docs/EVALUATOR.md. Wiring them is the next increment, not a claim this
 * one makes.
 */
#include <string.h>

#include "eval_internal.h"

#define EVAL_MAX_LIST 64u

static phy_status eval_operator(phy_env *env, phy_ir_ref expr,
                                bool *out_handled, phy_value *out_value);

/* --------------------------------------------------------------- values */

static phy_value scalar_value(phy_ir_ref ref)
{
    phy_value value;
    value.kind = PHY_VALUE_SCALAR;
    value.as.scalar = ref;
    return value;
}

static phy_status integer_value(phy_env *env, int64_t number,
                                phy_value *out_value)
{
    const phy_ir_ref ref = phy_ir_integer(env->ir, number);
    if (ref == PHY_IR_NULL) {
        return phy_ir_last_error(env->ir);
    }
    *out_value = scalar_value(ref);
    return PHY_OK;
}

/*
 * A decision, spelled. True, False and Unknown are ordinary symbols, so the
 * renderer and the document codec need nothing new -- and an undecided
 * question is visibly undecided instead of collapsing to False.
 */
static phy_status decision_value(phy_env *env, phy_cas_decision decision,
                                 phy_value *out_value)
{
    const char *name = decision == PHY_CAS_ZERO      ? "True"
                       : decision == PHY_CAS_NONZERO ? "False"
                                                     : "Unknown";
    const phy_ir_symbol symbol = phy_ir_intern(env->ir, name);
    const phy_ir_ref ref = phy_ir_symbol_ref(env->ir, symbol);
    if (ref == PHY_IR_NULL) {
        return phy_ir_last_error(env->ir);
    }
    *out_value = scalar_value(ref);
    return PHY_OK;
}

/* ------------------------------------------------------------ arguments */

static size_t arg_count(const phy_env *env, phy_ir_ref expr)
{
    return phy_ir_child_count(env->ir, expr);
}

static phy_ir_ref arg_ref(const phy_env *env, phy_ir_ref expr, size_t index)
{
    return phy_ir_child(env->ir, expr, index);
}

static phy_status arg_value(phy_env *env, phy_ir_ref expr, size_t index,
                            phy_value *out_value)
{
    return eval_node(env, arg_ref(env, expr, index), out_value);
}

static phy_status arg_typed(phy_env *env, phy_ir_ref expr, size_t index,
                            phy_value_kind kind, phy_value *out_value)
{
    const phy_status status = arg_value(env, expr, index, out_value);
    if (status != PHY_OK) {
        return status;
    }
    return out_value->kind == kind ? PHY_OK : PHY_ERR_TYPE;
}

static phy_status arg_scalar(phy_env *env, phy_ir_ref expr, size_t index,
                             phy_ir_ref *out_ref)
{
    phy_value value;
    const phy_status status =
        arg_typed(env, expr, index, PHY_VALUE_SCALAR, &value);
    if (status == PHY_OK) {
        *out_ref = value.as.scalar;
    }
    return status;
}

/* An evaluated exact integer in [0, limit). */
static phy_status arg_unsigned(phy_env *env, phy_ir_ref expr, size_t index,
                               unsigned limit, unsigned *out_value)
{
    phy_ir_ref ref = PHY_IR_NULL;
    const phy_status status = arg_scalar(env, expr, index, &ref);
    if (status != PHY_OK) {
        return status;
    }
    int64_t number = 0;
    if (!phy_ir_integer_value(env->ir, ref, &number)) {
        return PHY_ERR_TYPE;
    }
    if (number < 0 || (uint64_t)number >= (uint64_t)limit) {
        return PHY_ERR_DOMAIN;
    }
    *out_value = (unsigned)number;
    return PHY_OK;
}

/*
 * A bare identifier read as a keyword. Deliberately NOT looked up: `Lorentzian`
 * names a signature, and a document that also binds `Lorentzian` to a number
 * must not thereby change what a manifold declaration means.
 */
static const char *arg_keyword(const phy_env *env, phy_ir_ref expr,
                               size_t index)
{
    const phy_ir_ref ref = arg_ref(env, expr, index);
    if (phy_ir_kind_of(env->ir, ref) != PHY_IR_SYMBOL) {
        return NULL;
    }
    return phy_ir_symbol_name(env->ir, phy_ir_head(env->ir, ref));
}

static bool keyword_is(const char *keyword, const char *expected)
{
    return keyword != NULL && strcmp(keyword, expected) == 0;
}

/* The unevaluated elements of a `{...}` literal. */
static phy_status list_refs(const phy_env *env, phy_ir_ref ref,
                            phy_ir_ref *out_items, size_t capacity,
                            size_t *out_count)
{
    if (phy_ir_kind_of(env->ir, ref) != PHY_IR_FUNCTION ||
        phy_ir_head(env->ir, ref) != env->list_head) {
        return PHY_ERR_PARSE;
    }
    const size_t count = phy_ir_child_count(env->ir, ref);
    if (count > capacity) {
        return PHY_ERR_TERM_LIMIT;
    }
    for (size_t i = 0u; i < count; ++i) {
        out_items[i] = phy_ir_child(env->ir, ref, i);
    }
    *out_count = count;
    return PHY_OK;
}

/* `{a, b, ...}` with every element evaluated to a scalar. */
static phy_status list_scalars(phy_env *env, phy_ir_ref ref,
                               phy_ir_ref *out_items, size_t capacity,
                               size_t *out_count)
{
    phy_ir_ref raw[EVAL_MAX_LIST];
    size_t count = 0u;
    phy_status status =
        list_refs(env, ref, raw, capacity < EVAL_MAX_LIST ? capacity
                                                          : EVAL_MAX_LIST,
                  &count);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 0u; i < count; ++i) {
        phy_value value;
        status = eval_node(env, raw[i], &value);
        if (status != PHY_OK) {
            return status;
        }
        if (value.kind != PHY_VALUE_SCALAR) {
            return PHY_ERR_TYPE;
        }
        out_items[i] = value.as.scalar;
    }
    *out_count = count;
    return PHY_OK;
}

/* `{{...},{...}}` flattened row-major; every row must have `columns` entries. */
static phy_status list_matrix(phy_env *env, phy_ir_ref ref, size_t rows,
                              size_t columns, phy_ir_ref *out_items)
{
    phy_ir_ref raw[EVAL_MAX_LIST];
    size_t count = 0u;
    phy_status status = list_refs(env, ref, raw, EVAL_MAX_LIST, &count);
    if (status != PHY_OK) {
        return status;
    }
    if (count != rows) {
        return PHY_ERR_PARSE;
    }
    if (rows * columns > EVAL_MAX_LIST) {
        return PHY_ERR_TERM_LIMIT;
    }
    for (size_t row = 0u; row < rows; ++row) {
        size_t width = 0u;
        status = list_scalars(env, raw[row], &out_items[row * columns],
                              columns, &width);
        if (status != PHY_OK) {
            return status;
        }
        if (width != columns) {
            return PHY_ERR_PARSE;
        }
    }
    return PHY_OK;
}

/* -------------------------------------------------------- registration */

static const char *pending_name(const phy_env *env, const char *fallback)
{
    if (env->pending_name == PHY_IR_NO_SYMBOL) {
        return fallback;
    }
    const char *name = phy_ir_symbol_name(env->ir, env->pending_name);
    return name != NULL ? name : fallback;
}

static phy_status publish(phy_env *env, phy_value_kind kind, void *object,
                          const phy_value *first, const phy_value *second,
                          phy_value *out_value)
{
    phy_value value;
    value.kind = kind;
    switch (kind) {
    case PHY_VALUE_CHART:
        value.as.chart = (phy_chart *)object;
        break;
    case PHY_VALUE_MANIFOLD:
        value.as.manifold = (phy_manifold *)object;
        break;
    case PHY_VALUE_TENSOR:
        value.as.tensor = (const phy_tensor *)object;
        break;
    case PHY_VALUE_FORM:
        value.as.form = (const phy_form *)object;
        break;
    case PHY_VALUE_LIE_GROUP:
        value.as.group = (const phy_lie_group *)object;
        break;
    case PHY_VALUE_LIE_ELEMENT:
        value.as.element = (const phy_lie_element *)object;
        break;
    case PHY_VALUE_LIE_FORM:
        value.as.lie_form = (const phy_lie_form *)object;
        break;
    case PHY_VALUE_CURVATURE:
        value.as.curvature = (const phy_gr_result *)object;
        break;
    default:
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_status status =
        eval_register(env, value, object, first, second);
    if (status == PHY_OK) {
        *out_value = value;
    }
    return status;
}

/*
 * A tensor or algebra this layer does not own: an interior pointer of a
 * curvature bundle or of a Lie group. It is registered so that the sweep keeps
 * its owner alive for exactly as long as the view is reachable.
 */
static phy_status publish_borrowed(phy_env *env, phy_value value,
                                   const phy_value *owner,
                                   phy_value *out_value)
{
    const phy_status status = eval_register(env, value, NULL, owner, NULL);
    if (status == PHY_OK) {
        *out_value = value;
    }
    return status;
}

/* ------------------------------------------------------------ geometry */

static phy_status read_signature(const phy_env *env, phy_ir_ref ref,
                                 unsigned dimension, int8_t *out_signature)
{
    const char *keyword = phy_ir_kind_of(env->ir, ref) == PHY_IR_SYMBOL
                              ? phy_ir_symbol_name(env->ir,
                                                   phy_ir_head(env->ir, ref))
                              : NULL;
    if (keyword_is(keyword, "Euclidean") || keyword_is(keyword, "Riemannian")) {
        for (unsigned i = 0u; i < dimension; ++i) {
            out_signature[i] = 1;
        }
        return PHY_OK;
    }
    if (keyword_is(keyword, "Lorentzian") || keyword_is(keyword, "Minkowski")) {
        /* Mostly-plus, per docs/references/GENERAL_RELATIVITY.md. */
        out_signature[0] = -1;
        for (unsigned i = 1u; i < dimension; ++i) {
            out_signature[i] = 1;
        }
        return PHY_OK;
    }
    if (keyword != NULL) {
        return PHY_ERR_PARSE;
    }

    phy_ir_ref items[PHY_MANIFOLD_MAX_DIM];
    size_t count = 0u;
    const phy_status status =
        list_refs(env, ref, items, PHY_MANIFOLD_MAX_DIM, &count);
    if (status != PHY_OK) {
        return status;
    }
    if (count != (size_t)dimension) {
        return PHY_ERR_PARSE;
    }
    for (size_t i = 0u; i < count; ++i) {
        int64_t entry = 0;
        if (!phy_ir_integer_value(env->ir, items[i], &entry) ||
            (entry != 1 && entry != -1)) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
        out_signature[i] = (int8_t)entry;
    }
    return PHY_OK;
}

static phy_status read_orientation(const phy_env *env, phy_ir_ref ref,
                                   phy_orientation *out_orientation)
{
    const char *keyword = phy_ir_kind_of(env->ir, ref) == PHY_IR_SYMBOL
                              ? phy_ir_symbol_name(env->ir,
                                                   phy_ir_head(env->ir, ref))
                              : NULL;
    if (keyword_is(keyword, "Positive")) {
        *out_orientation = PHY_ORIENTATION_POSITIVE;
        return PHY_OK;
    }
    if (keyword_is(keyword, "Negative")) {
        *out_orientation = PHY_ORIENTATION_NEGATIVE;
        return PHY_OK;
    }
    if (keyword_is(keyword, "Unoriented") || keyword_is(keyword, "None")) {
        *out_orientation = PHY_ORIENTATION_NONE;
        return PHY_OK;
    }
    return PHY_ERR_PARSE;
}

static phy_status eval_manifold(phy_env *env, phy_ir_ref expr,
                                phy_value *out_value)
{
    const size_t count = arg_count(env, expr);
    if (count < 2u || count > 3u) {
        return PHY_ERR_PARSE;
    }

    phy_ir_ref coordinates[PHY_MANIFOLD_MAX_DIM];
    size_t dimension = 0u;
    phy_status status = list_refs(env, arg_ref(env, expr, 0u), coordinates,
                                  PHY_MANIFOLD_MAX_DIM, &dimension);
    if (status != PHY_OK) {
        return status;
    }
    if (dimension == 0u) {
        return PHY_ERR_PARSE;
    }

    const char *names[PHY_MANIFOLD_MAX_DIM];
    for (size_t i = 0u; i < dimension; ++i) {
        if (phy_ir_kind_of(env->ir, coordinates[i]) != PHY_IR_SYMBOL) {
            return PHY_ERR_TYPE;
        }
        const phy_ir_symbol symbol = phy_ir_head(env->ir, coordinates[i]);
        /*
         * A coordinate that is already a bound name would be substituted away
         * the next time any component mentioning it is evaluated, and the wrong
         * answer would be indistinguishable from the right one.
         */
        if (eval_lookup(env, symbol, NULL)) {
            return PHY_ERR_ASSUMPTION;
        }
        names[i] = phy_ir_symbol_name(env->ir, symbol);
        if (names[i] == NULL) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
    }

    int8_t signature[PHY_MANIFOLD_MAX_DIM];
    status =
        read_signature(env, arg_ref(env, expr, 1u), (unsigned)dimension,
                       signature);
    if (status != PHY_OK) {
        return status;
    }
    phy_orientation orientation = PHY_ORIENTATION_POSITIVE;
    if (count == 3u) {
        status =
            read_orientation(env, arg_ref(env, expr, 2u), &orientation);
        if (status != PHY_OK) {
            return status;
        }
    }

    phy_chart *chart = NULL;
    status = phy_chart_create(env->ir, names, (unsigned)dimension, &chart);
    if (status != PHY_OK) {
        return status;
    }
    phy_value chart_value;
    status = publish(env, PHY_VALUE_CHART, chart, NULL, NULL, &chart_value);
    if (status != PHY_OK) {
        return status;
    }

    phy_manifold *manifold = NULL;
    status = phy_manifold_create(env->ir, pending_name(env, "M"),
                                 (unsigned)dimension, orientation, signature,
                                 &manifold);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_manifold_add_chart(manifold, chart, NULL);
    if (status != PHY_OK) {
        phy_manifold_destroy(manifold);
        return status;
    }
    return publish(env, PHY_VALUE_MANIFOLD, manifold, &chart_value, NULL,
                   out_value);
}

static phy_status eval_differential_form(phy_env *env, phy_ir_ref expr,
                                         phy_value *out_value)
{
    const size_t count = arg_count(env, expr);
    if (count < 2u || count > 3u) {
        return PHY_ERR_PARSE;
    }
    phy_value manifold;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_MANIFOLD, &manifold);
    if (status != PHY_OK) {
        return status;
    }
    unsigned degree = 0u;
    status = arg_unsigned(env, expr, 1u,
                          phy_manifold_dimension(manifold.as.manifold) + 1u,
                          &degree);
    if (status != PHY_OK) {
        return status;
    }

    phy_form *form = NULL;
    status = phy_form_create(manifold.as.manifold, 0u,
                             pending_name(env, "omega"), degree, &form);
    if (status != PHY_OK) {
        return status;
    }
    if (count == 3u) {
        const size_t components = phy_form_component_count(form);
        phy_ir_ref values[PHY_FORM_MAX_COMPONENTS];
        size_t supplied = 0u;
        status = list_scalars(env, arg_ref(env, expr, 2u), values,
                              PHY_FORM_MAX_COMPONENTS, &supplied);
        if (status == PHY_OK && supplied != components) {
            status = PHY_ERR_PARSE;
        }
        for (size_t i = 0u; status == PHY_OK && i < components; ++i) {
            status = phy_form_set_at(env->cas, form, i, values[i]);
        }
        if (status != PHY_OK) {
            phy_form_destroy(form);
            return status;
        }
    }
    return publish(env, PHY_VALUE_FORM, form, &manifold, NULL, out_value);
}

static phy_status eval_component_tensor(phy_env *env, phy_ir_ref expr,
                                        unsigned rank, phy_ir_variance valence,
                                        const char *fallback_name,
                                        phy_value *out_value)
{
    if (arg_count(env, expr) != 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value manifold;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_MANIFOLD, &manifold);
    if (status != PHY_OK) {
        return status;
    }
    const unsigned dimension = phy_manifold_dimension(manifold.as.manifold);
    phy_chart *chart = phy_manifold_chart(manifold.as.manifold, 0u);
    if (chart == NULL) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    phy_ir_ref entries[EVAL_MAX_LIST];
    if (rank == 1u) {
        size_t supplied = 0u;
        status = list_scalars(env, arg_ref(env, expr, 1u), entries,
                              (size_t)dimension, &supplied);
        if (status == PHY_OK && supplied != (size_t)dimension) {
            status = PHY_ERR_PARSE;
        }
    } else {
        status = list_matrix(env, arg_ref(env, expr, 1u), (size_t)dimension,
                             (size_t)dimension, entries);
    }
    if (status != PHY_OK) {
        return status;
    }

    const phy_ir_variance slots[2] = {valence, valence};
    phy_tensor *tensor = NULL;
    status = phy_tensor_create(chart, pending_name(env, fallback_name), rank,
                               slots, &tensor);
    if (status != PHY_OK) {
        return status;
    }
    const size_t components = phy_tensor_component_count(tensor);
    for (size_t i = 0u; status == PHY_OK && i < components; ++i) {
        status = phy_tensor_set_flat(tensor, i, entries[i]);
    }
    if (status != PHY_OK) {
        phy_tensor_destroy(tensor);
        return status;
    }
    return publish(env, PHY_VALUE_TENSOR, tensor, &manifold, NULL, out_value);
}

static phy_status eval_exterior_derivative(phy_env *env, phy_ir_ref expr,
                                           phy_value *out_value)
{
    if (arg_count(env, expr) != 1u) {
        return PHY_ERR_PARSE;
    }
    phy_value operand;
    phy_status status = arg_value(env, expr, 0u, &operand);
    if (status != PHY_OK) {
        return status;
    }
    if (operand.kind == PHY_VALUE_FORM) {
        phy_form *result = NULL;
        status = phy_form_exterior_derivative(env->cas, operand.as.form,
                                              &result);
        return status == PHY_OK
                   ? publish(env, PHY_VALUE_FORM, result, &operand, NULL,
                             out_value)
                   : status;
    }
    if (operand.kind == PHY_VALUE_LIE_FORM) {
        phy_lie_form *result = NULL;
        status = phy_lie_form_exterior_derivative(env->cas,
                                                  operand.as.lie_form,
                                                  &result);
        return status == PHY_OK
                   ? publish(env, PHY_VALUE_LIE_FORM, result, &operand, NULL,
                             out_value)
                   : status;
    }
    return PHY_ERR_TYPE;
}

static phy_status eval_interior_product(phy_env *env, phy_ir_ref expr,
                                        phy_value *out_value)
{
    if (arg_count(env, expr) != 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value form;
    phy_status status = arg_typed(env, expr, 0u, PHY_VALUE_FORM, &form);
    if (status != PHY_OK) {
        return status;
    }
    phy_value vector;
    status = arg_typed(env, expr, 1u, PHY_VALUE_TENSOR, &vector);
    if (status != PHY_OK) {
        return status;
    }
    phy_form *result = NULL;
    status = phy_form_interior_product(env->cas, form.as.form,
                                       vector.as.tensor, &result);
    return status == PHY_OK
               ? publish(env, PHY_VALUE_FORM, result, &form, NULL, out_value)
               : status;
}

static phy_status eval_hodge(phy_env *env, phy_ir_ref expr,
                             phy_value *out_value)
{
    const size_t count = arg_count(env, expr);
    if (count < 1u || count > 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value form;
    phy_status status = arg_typed(env, expr, 0u, PHY_VALUE_FORM, &form);
    if (status != PHY_OK) {
        return status;
    }
    phy_form *result = NULL;
    if (count == 1u) {
        status = phy_form_hodge(env->cas, form.as.form, &result);
    } else {
        phy_value metric;
        status = arg_typed(env, expr, 1u, PHY_VALUE_TENSOR, &metric);
        if (status != PHY_OK) {
            return status;
        }
        status = phy_form_hodge_metric(env->cas, form.as.form,
                                       metric.as.tensor, &result);
    }
    return status == PHY_OK
               ? publish(env, PHY_VALUE_FORM, result, &form, NULL, out_value)
               : status;
}

static phy_status eval_volume(phy_env *env, phy_ir_ref expr,
                              phy_value *out_value)
{
    const size_t count = arg_count(env, expr);
    if (count < 1u || count > 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value manifold;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_MANIFOLD, &manifold);
    if (status != PHY_OK) {
        return status;
    }
    phy_form *result = NULL;
    if (count == 1u) {
        status = phy_form_volume(env->cas, manifold.as.manifold, 0u, &result);
    } else {
        phy_value metric;
        status = arg_typed(env, expr, 1u, PHY_VALUE_TENSOR, &metric);
        if (status != PHY_OK) {
            return status;
        }
        status = phy_form_volume_metric(env->cas, manifold.as.manifold, 0u,
                                        metric.as.tensor, &result);
    }
    return status == PHY_OK ? publish(env, PHY_VALUE_FORM, result, &manifold,
                                      NULL, out_value)
                            : status;
}

/* ---------------------------------------------------------- Lie algebra */

static phy_status eval_lie_group(phy_env *env, phy_ir_ref expr,
                                 phy_value *out_value)
{
    if (arg_count(env, expr) != 1u) {
        return PHY_ERR_PARSE;
    }
    static const struct {
        const char *keyword;
        phy_lie_group_kind kind;
    } kBuiltins[] = {
        {"U1", PHY_LIE_GROUP_U1},   {"SU2", PHY_LIE_GROUP_SU2},
        {"SO3", PHY_LIE_GROUP_SO3}, {"SU3", PHY_LIE_GROUP_SU3},
        {"SO13", PHY_LIE_GROUP_SO13},
    };
    const char *keyword = arg_keyword(env, expr, 0u);
    for (size_t i = 0u; i < sizeof kBuiltins / sizeof kBuiltins[0]; ++i) {
        if (!keyword_is(keyword, kBuiltins[i].keyword)) {
            continue;
        }
        phy_lie_group *group = NULL;
        const phy_status status =
            phy_lie_group_builtin(env->cas, kBuiltins[i].kind, &group);
        return status == PHY_OK ? publish(env, PHY_VALUE_LIE_GROUP, group,
                                          NULL, NULL, out_value)
                                : status;
    }
    /*
     * A group this layer has no structure constants for is unsupported, not a
     * label: returning an object that cannot bracket would be worse than
     * failing.
     */
    return keyword != NULL ? PHY_ERR_UNSUPPORTED : PHY_ERR_TYPE;
}

static phy_status eval_lie_algebra(phy_env *env, phy_ir_ref expr,
                                   phy_value *out_value)
{
    if (arg_count(env, expr) != 1u) {
        return PHY_ERR_PARSE;
    }
    phy_value group;
    const phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_GROUP, &group);
    if (status != PHY_OK) {
        return status;
    }
    phy_value algebra;
    algebra.kind = PHY_VALUE_LIE_ALGEBRA;
    algebra.as.algebra = phy_lie_group_algebra(group.as.group);
    if (algebra.as.algebra == NULL) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    return publish_borrowed(env, algebra, &group, out_value);
}

static phy_status eval_generator(phy_env *env, phy_ir_ref expr,
                                 phy_value *out_value)
{
    if (arg_count(env, expr) != 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value algebra;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_ALGEBRA, &algebra);
    if (status != PHY_OK) {
        return status;
    }
    unsigned basis = 0u;
    status = arg_unsigned(env, expr, 1u,
                          phy_lie_algebra_dimension(algebra.as.algebra),
                          &basis);
    if (status != PHY_OK) {
        return status;
    }
    phy_lie_element *element = NULL;
    status = phy_lie_basis_element(algebra.as.algebra, basis, &element);
    return status == PHY_OK ? publish(env, PHY_VALUE_LIE_ELEMENT, element,
                                      &algebra, NULL, out_value)
                            : status;
}

static phy_status eval_lie_element(phy_env *env, phy_ir_ref expr,
                                   phy_value *out_value)
{
    if (arg_count(env, expr) != 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value algebra;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_ALGEBRA, &algebra);
    if (status != PHY_OK) {
        return status;
    }
    const unsigned dimension =
        phy_lie_algebra_dimension(algebra.as.algebra);
    phy_ir_ref coefficients[PHY_LIE_MAX_DIM];
    size_t supplied = 0u;
    status = list_scalars(env, arg_ref(env, expr, 1u), coefficients,
                          PHY_LIE_MAX_DIM, &supplied);
    if (status != PHY_OK) {
        return status;
    }
    if (supplied != (size_t)dimension) {
        return PHY_ERR_PARSE;
    }
    phy_lie_element *element = NULL;
    status = phy_lie_element_create(algebra.as.algebra, coefficients,
                                    &element);
    return status == PHY_OK ? publish(env, PHY_VALUE_LIE_ELEMENT, element,
                                      &algebra, NULL, out_value)
                            : status;
}

static phy_status eval_lie_bracket(phy_env *env, phy_ir_ref expr,
                                   phy_value *out_value)
{
    if (arg_count(env, expr) != 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value left;
    phy_status status = arg_value(env, expr, 0u, &left);
    if (status != PHY_OK) {
        return status;
    }
    phy_value right;
    status = arg_value(env, expr, 1u, &right);
    if (status != PHY_OK) {
        return status;
    }
    if (left.kind != right.kind) {
        return PHY_ERR_TYPE;
    }
    if (left.kind == PHY_VALUE_LIE_ELEMENT) {
        phy_lie_element *result = NULL;
        status = phy_lie_bracket(left.as.element, right.as.element, &result);
        return status == PHY_OK ? publish(env, PHY_VALUE_LIE_ELEMENT, result,
                                          &left, &right, out_value)
                                : status;
    }
    if (left.kind == PHY_VALUE_LIE_FORM) {
        phy_lie_form *result = NULL;
        status = phy_lie_form_bracket_wedge(env->cas, left.as.lie_form,
                                            right.as.lie_form, &result);
        return status == PHY_OK ? publish(env, PHY_VALUE_LIE_FORM, result,
                                          &left, &right, out_value)
                                : status;
    }
    if (left.kind == PHY_VALUE_SCALAR) {
        /* The generic noncommutative A.B - B.A on the shared typed IR. */
        phy_ir_ref result = PHY_IR_NULL;
        status = phy_lie_commutator_ir(env->cas, left.as.scalar,
                                       right.as.scalar, &result);
        if (status == PHY_OK) {
            *out_value = scalar_value(result);
        }
        return status;
    }
    return PHY_ERR_TYPE;
}

static phy_status eval_structure_constant(phy_env *env, phy_ir_ref expr,
                                          phy_value *out_value)
{
    if (arg_count(env, expr) != 4u) {
        return PHY_ERR_PARSE;
    }
    phy_value algebra;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_ALGEBRA, &algebra);
    if (status != PHY_OK) {
        return status;
    }
    const unsigned dimension =
        phy_lie_algebra_dimension(algebra.as.algebra);
    unsigned index[3] = {0u, 0u, 0u};
    for (size_t i = 0u; i < 3u; ++i) {
        status = arg_unsigned(env, expr, i + 1u, dimension, &index[i]);
        if (status != PHY_OK) {
            return status;
        }
    }
    const phy_ir_ref constant = phy_lie_structure_constant(
        algebra.as.algebra, index[0], index[1], index[2]);
    if (constant == PHY_IR_NULL) {
        return PHY_ERR_DOMAIN;
    }
    *out_value = scalar_value(constant);
    return PHY_OK;
}

static phy_status eval_killing(phy_env *env, phy_ir_ref expr,
                               phy_value *out_value)
{
    if (arg_count(env, expr) != 3u) {
        return PHY_ERR_PARSE;
    }
    phy_value algebra;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_ALGEBRA, &algebra);
    if (status != PHY_OK) {
        return status;
    }
    const unsigned dimension =
        phy_lie_algebra_dimension(algebra.as.algebra);
    unsigned index[2] = {0u, 0u};
    for (size_t i = 0u; i < 2u; ++i) {
        status = arg_unsigned(env, expr, i + 1u, dimension, &index[i]);
        if (status != PHY_OK) {
            return status;
        }
    }
    phy_ir_ref value = PHY_IR_NULL;
    status = phy_lie_killing_component(algebra.as.algebra, index[0], index[1],
                                       &value);
    if (status == PHY_OK) {
        *out_value = scalar_value(value);
    }
    return status;
}

/* ----------------------------------------------------------- Yang-Mills */

static phy_status eval_lie_form(phy_env *env, phy_ir_ref expr,
                                bool fixed_degree_one, phy_value *out_value)
{
    const size_t expected = fixed_degree_one ? 3u : 4u;
    if (arg_count(env, expr) != expected) {
        return PHY_ERR_PARSE;
    }
    phy_value algebra;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_ALGEBRA, &algebra);
    if (status != PHY_OK) {
        return status;
    }
    phy_value manifold;
    status = arg_typed(env, expr, 1u, PHY_VALUE_MANIFOLD, &manifold);
    if (status != PHY_OK) {
        return status;
    }
    unsigned degree = 1u;
    if (!fixed_degree_one) {
        status = arg_unsigned(env, expr, 2u,
                              phy_manifold_dimension(manifold.as.manifold) + 1u,
                              &degree);
        if (status != PHY_OK) {
            return status;
        }
    }

    phy_lie_form *form = NULL;
    status = phy_lie_form_create(algebra.as.algebra, manifold.as.manifold, 0u,
                                 degree, &form);
    if (status != PHY_OK) {
        return status;
    }
    const unsigned colours = phy_lie_form_algebra_dimension(form);
    const size_t components =
        phy_form_component_count(phy_lie_form_component(form, 0u));
    phy_ir_ref entries[EVAL_MAX_LIST];
    status = list_matrix(env, arg_ref(env, expr, expected - 1u),
                         (size_t)colours, components, entries);
    for (unsigned colour = 0u; status == PHY_OK && colour < colours;
         ++colour) {
        phy_form *component = phy_lie_form_component_mut(form, colour);
        for (size_t i = 0u; status == PHY_OK && i < components; ++i) {
            status = phy_form_set_at(env->cas, component, i,
                                     entries[(size_t)colour * components + i]);
        }
    }
    if (status != PHY_OK) {
        phy_lie_form_destroy(form);
        return status;
    }
    return publish(env, PHY_VALUE_LIE_FORM, form, &algebra, &manifold,
                   out_value);
}

static phy_status eval_field_strength(phy_env *env, phy_ir_ref expr,
                                      bool bianchi, phy_value *out_value)
{
    if (arg_count(env, expr) != 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value connection;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_FORM, &connection);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref coupling = PHY_IR_NULL;
    status = arg_scalar(env, expr, 1u, &coupling);
    if (status != PHY_OK) {
        return status;
    }
    phy_lie_form *result = NULL;
    status = bianchi ? phy_yang_mills_bianchi(env->cas,
                                              connection.as.lie_form,
                                              coupling, &result)
                     : phy_yang_mills_field_strength(env->cas,
                                                     connection.as.lie_form,
                                                     coupling, &result);
    return status == PHY_OK ? publish(env, PHY_VALUE_LIE_FORM, result,
                                      &connection, NULL, out_value)
                            : status;
}

static phy_status eval_covariant_derivative(phy_env *env, phy_ir_ref expr,
                                            phy_value *out_value)
{
    if (arg_count(env, expr) != 3u) {
        return PHY_ERR_PARSE;
    }
    phy_value connection;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_FORM, &connection);
    if (status != PHY_OK) {
        return status;
    }
    phy_value form;
    status = arg_typed(env, expr, 1u, PHY_VALUE_LIE_FORM, &form);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref coupling = PHY_IR_NULL;
    status = arg_scalar(env, expr, 2u, &coupling);
    if (status != PHY_OK) {
        return status;
    }
    phy_lie_form *result = NULL;
    status = phy_yang_mills_covariant_derivative(
        env->cas, connection.as.lie_form, coupling, form.as.lie_form, &result);
    return status == PHY_OK ? publish(env, PHY_VALUE_LIE_FORM, result,
                                      &connection, &form, out_value)
                            : status;
}

/*
 * delta A = D_A alpha on a connection, delta F = g [F,alpha] on a curvature.
 *
 * The two are different formulas, and the operand's degree is what tells them
 * apart: a connection is a 1-form and a curvature a 2-form. Asking a reader to
 * spell which one they meant would be asking them to restate what the object
 * already knows, and getting it wrong would return a well-formed wrong answer.
 */
static phy_status eval_gauge_variation(phy_env *env, phy_ir_ref expr,
                                       phy_value *out_value)
{
    if (arg_count(env, expr) != 3u) {
        return PHY_ERR_PARSE;
    }
    phy_value target;
    phy_status status = arg_typed(env, expr, 0u, PHY_VALUE_LIE_FORM, &target);
    if (status != PHY_OK) {
        return status;
    }
    phy_value parameter;
    status = arg_typed(env, expr, 1u, PHY_VALUE_LIE_FORM, &parameter);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref coupling = PHY_IR_NULL;
    status = arg_scalar(env, expr, 2u, &coupling);
    if (status != PHY_OK) {
        return status;
    }
    const unsigned degree = phy_lie_form_degree(target.as.lie_form);
    phy_lie_form *result = NULL;
    if (degree == 1u) {
        status = phy_yang_mills_gauge_variation_connection(
            env->cas, target.as.lie_form, coupling, parameter.as.lie_form,
            &result);
    } else if (degree == 2u) {
        status = phy_yang_mills_gauge_variation_curvature(
            env->cas, target.as.lie_form, coupling, parameter.as.lie_form,
            &result);
    } else {
        return PHY_ERR_TYPE;
    }
    return status == PHY_OK ? publish(env, PHY_VALUE_LIE_FORM, result, &target,
                                      &parameter, out_value)
                            : status;
}

static phy_status eval_yang_mills_lagrangian(phy_env *env, phy_ir_ref expr,
                                             phy_value *out_value)
{
    const size_t count = arg_count(env, expr);
    if (count < 2u || count > 3u) {
        return PHY_ERR_PARSE;
    }
    phy_value curvature;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_FORM, &curvature);
    if (status != PHY_OK) {
        return status;
    }
    phy_value metric;
    status = arg_typed(env, expr, 1u, PHY_VALUE_TENSOR, &metric);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref bilinear[EVAL_MAX_LIST];
    const phy_ir_ref *table = NULL;
    if (count == 3u) {
        const size_t colours =
            (size_t)phy_lie_form_algebra_dimension(curvature.as.lie_form);
        status = list_matrix(env, arg_ref(env, expr, 2u), colours, colours,
                             bilinear);
        if (status != PHY_OK) {
            return status;
        }
        table = bilinear;
    }
    phy_form *result = NULL;
    status = phy_yang_mills_lagrangian(env->cas, curvature.as.lie_form,
                                       metric.as.tensor, table, &result);
    return status == PHY_OK ? publish(env, PHY_VALUE_FORM, result, &curvature,
                                      &metric, out_value)
                            : status;
}

static phy_status eval_color_component(phy_env *env, phy_ir_ref expr,
                                       phy_value *out_value)
{
    if (arg_count(env, expr) != 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value lie_form;
    phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_LIE_FORM, &lie_form);
    if (status != PHY_OK) {
        return status;
    }
    unsigned colour = 0u;
    status = arg_unsigned(
        env, expr, 1u, phy_lie_form_algebra_dimension(lie_form.as.lie_form),
        &colour);
    if (status != PHY_OK) {
        return status;
    }
    phy_value component;
    component.kind = PHY_VALUE_FORM;
    component.as.form = phy_lie_form_component(lie_form.as.lie_form, colour);
    if (component.as.form == NULL) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    return publish_borrowed(env, component, &lie_form, out_value);
}

/* ---------------------------------------------------- general relativity */

static phy_status eval_curvature(phy_env *env, phy_ir_ref expr,
                                 phy_value *out_value)
{
    if (arg_count(env, expr) != 1u) {
        return PHY_ERR_PARSE;
    }
    phy_value metric;
    phy_status status = arg_typed(env, expr, 0u, PHY_VALUE_TENSOR, &metric);
    if (status != PHY_OK) {
        return status;
    }
    phy_gr_result *result = NULL;
    status = phy_gr_compute(env->cas, metric.as.tensor, &result);
    return status == PHY_OK ? publish(env, PHY_VALUE_CURVATURE, result,
                                      &metric, NULL, out_value)
                            : status;
}

static phy_status eval_curvature_part(phy_env *env, phy_ir_ref expr,
                                      eval_head head, phy_value *out_value)
{
    if (arg_count(env, expr) != 1u) {
        return PHY_ERR_PARSE;
    }
    phy_value bundle;
    const phy_status status =
        arg_typed(env, expr, 0u, PHY_VALUE_CURVATURE, &bundle);
    if (status != PHY_OK) {
        return status;
    }
    if (head == EVAL_HEAD_RICCI_SCALAR) {
        const phy_ir_ref scalar =
            phy_gr_scalar_curvature(bundle.as.curvature);
        if (scalar == PHY_IR_NULL) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        *out_value = scalar_value(scalar);
        return PHY_OK;
    }
    phy_value tensor;
    tensor.kind = PHY_VALUE_TENSOR;
    switch (head) {
    case EVAL_HEAD_INVERSE_METRIC:
        tensor.as.tensor = phy_gr_inverse_metric(bundle.as.curvature);
        break;
    case EVAL_HEAD_CHRISTOFFEL:
        tensor.as.tensor = phy_gr_christoffel(bundle.as.curvature);
        break;
    case EVAL_HEAD_RIEMANN:
        tensor.as.tensor = phy_gr_riemann_covariant(bundle.as.curvature);
        break;
    case EVAL_HEAD_RIEMANN_MIXED:
        tensor.as.tensor = phy_gr_riemann_mixed(bundle.as.curvature);
        break;
    case EVAL_HEAD_RICCI:
        tensor.as.tensor = phy_gr_ricci(bundle.as.curvature);
        break;
    case EVAL_HEAD_EINSTEIN:
        tensor.as.tensor = phy_gr_einstein(bundle.as.curvature);
        break;
    default:
        return PHY_ERR_UNSUPPORTED;
    }
    if (tensor.as.tensor == NULL) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    return publish_borrowed(env, tensor, &bundle, out_value);
}

/* --------------------------------------------------------------- queries */

static phy_status eval_component(phy_env *env, phy_ir_ref expr,
                                 phy_value *out_value)
{
    const size_t count = arg_count(env, expr);
    if (count < 1u) {
        return PHY_ERR_PARSE;
    }
    phy_value target;
    phy_status status = arg_value(env, expr, 0u, &target);
    if (status != PHY_OK) {
        return status;
    }
    const size_t supplied = count - 1u;
    unsigned indices[PHY_TENSOR_MAX_RANK + 1u];
    if (supplied > PHY_TENSOR_MAX_RANK + 1u) {
        return PHY_ERR_PARSE;
    }

    if (target.kind == PHY_VALUE_LIE_ELEMENT) {
        if (supplied != 1u) {
            return PHY_ERR_PARSE;
        }
        const phy_lie_algebra *algebra =
            phy_lie_element_algebra(target.as.element);
        status = arg_unsigned(env, expr, 1u,
                              phy_lie_algebra_dimension(algebra), &indices[0]);
        if (status != PHY_OK) {
            return status;
        }
        const phy_ir_ref value =
            phy_lie_element_coefficient(target.as.element, indices[0]);
        if (value == PHY_IR_NULL) {
            return PHY_ERR_DOMAIN;
        }
        *out_value = scalar_value(value);
        return PHY_OK;
    }

    const phy_form *form = NULL;
    size_t first_form_index = 1u;
    if (target.kind == PHY_VALUE_FORM) {
        form = target.as.form;
    } else if (target.kind == PHY_VALUE_LIE_FORM) {
        unsigned colour = 0u;
        status = arg_unsigned(
            env, expr, 1u,
            phy_lie_form_algebra_dimension(target.as.lie_form), &colour);
        if (status != PHY_OK) {
            return status;
        }
        form = phy_lie_form_component(target.as.lie_form, colour);
        first_form_index = 2u;
    }
    if (form != NULL) {
        const unsigned degree = phy_form_degree(form);
        if (supplied != (size_t)degree + (first_form_index - 1u)) {
            return PHY_ERR_PARSE;
        }
        const unsigned dimension = phy_form_dimension(form);
        for (unsigned i = 0u; i < degree; ++i) {
            status = arg_unsigned(env, expr, first_form_index + i, dimension,
                                  &indices[i]);
            if (status != PHY_OK) {
                return status;
            }
        }
        phy_ir_ref value = PHY_IR_NULL;
        status = degree == 0u
                     ? phy_form_get_at(form, 0u, &value)
                     : phy_form_get(env->cas, form, indices, &value);
        if (status == PHY_OK) {
            *out_value = scalar_value(value);
        }
        return status;
    }

    if (target.kind == PHY_VALUE_TENSOR) {
        const unsigned rank = phy_tensor_rank(target.as.tensor);
        if (supplied != (size_t)rank) {
            return PHY_ERR_PARSE;
        }
        const unsigned dimension = phy_tensor_dimension(target.as.tensor);
        for (unsigned i = 0u; i < rank; ++i) {
            status =
                arg_unsigned(env, expr, 1u + i, dimension, &indices[i]);
            if (status != PHY_OK) {
                return status;
            }
        }
        phy_ir_ref value = PHY_IR_NULL;
        status = phy_tensor_component_expression(env->cas, target.as.tensor,
                                                 indices, &value);
        if (status == PHY_OK) {
            *out_value = scalar_value(value);
        }
        return status;
    }
    return PHY_ERR_TYPE;
}

static phy_status eval_measure(phy_env *env, phy_ir_ref expr, eval_head head,
                               phy_value *out_value)
{
    if (arg_count(env, expr) != 1u) {
        return PHY_ERR_PARSE;
    }
    phy_value target;
    const phy_status status = arg_value(env, expr, 0u, &target);
    if (status != PHY_OK) {
        return status;
    }
    if (head == EVAL_HEAD_DEGREE) {
        if (target.kind == PHY_VALUE_FORM) {
            return integer_value(env, (int64_t)phy_form_degree(target.as.form),
                                 out_value);
        }
        if (target.kind == PHY_VALUE_LIE_FORM) {
            return integer_value(
                env, (int64_t)phy_lie_form_degree(target.as.lie_form),
                out_value);
        }
        return PHY_ERR_TYPE;
    }
    if (head == EVAL_HEAD_RANK) {
        if (target.kind != PHY_VALUE_TENSOR) {
            return PHY_ERR_TYPE;
        }
        return integer_value(env, (int64_t)phy_tensor_rank(target.as.tensor),
                             out_value);
    }

    /* Dimension: of the underlying space where there is one, of the algebra
     * or representation otherwise. */
    switch (target.kind) {
    case PHY_VALUE_MANIFOLD:
        return integer_value(
            env, (int64_t)phy_manifold_dimension(target.as.manifold),
            out_value);
    case PHY_VALUE_FORM:
        return integer_value(
            env, (int64_t)phy_form_dimension(target.as.form), out_value);
    case PHY_VALUE_TENSOR:
        return integer_value(
            env, (int64_t)phy_tensor_dimension(target.as.tensor), out_value);
    case PHY_VALUE_LIE_FORM:
        return integer_value(
            env,
            (int64_t)phy_manifold_dimension(
                phy_lie_form_manifold(target.as.lie_form)),
            out_value);
    case PHY_VALUE_LIE_ALGEBRA:
        return integer_value(
            env, (int64_t)phy_lie_algebra_dimension(target.as.algebra),
            out_value);
    case PHY_VALUE_LIE_ELEMENT:
        return integer_value(
            env,
            (int64_t)phy_lie_algebra_dimension(
                phy_lie_element_algebra(target.as.element)),
            out_value);
    case PHY_VALUE_LIE_GROUP:
        return integer_value(
            env,
            (int64_t)phy_lie_group_representation_dimension(target.as.group),
            out_value);
    default:
        break;
    }
    return PHY_ERR_TYPE;
}

static phy_status tensor_is_zero(phy_env *env, const phy_tensor *tensor,
                                 phy_cas_decision *out_decision)
{
    const size_t count = phy_tensor_component_count(tensor);
    unsigned indices[PHY_TENSOR_MAX_RANK];
    phy_cas_decision combined = PHY_CAS_ZERO;
    for (size_t flat = 0u; flat < count; ++flat) {
        phy_status status = phy_tensor_unflatten(tensor, flat, indices);
        if (status != PHY_OK) {
            return status;
        }
        phy_ir_ref value = PHY_IR_NULL;
        status = phy_tensor_component_expression(env->cas, tensor, indices,
                                                 &value);
        if (status != PHY_OK) {
            return status;
        }
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        status = phy_cas_is_zero(env->cas, value, &decision);
        if (status != PHY_OK) {
            return status;
        }
        if (decision == PHY_CAS_NONZERO) {
            *out_decision = PHY_CAS_NONZERO;
            return PHY_OK;
        }
        if (decision == PHY_CAS_UNKNOWN) {
            combined = PHY_CAS_UNKNOWN;
        }
    }
    *out_decision = combined;
    return PHY_OK;
}

static phy_status element_is_zero(phy_env *env, const phy_lie_element *element,
                                  phy_cas_decision *out_decision)
{
    const unsigned dimension =
        phy_lie_algebra_dimension(phy_lie_element_algebra(element));
    phy_cas_decision combined = PHY_CAS_ZERO;
    for (unsigned i = 0u; i < dimension; ++i) {
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        const phy_status status = phy_cas_is_zero(
            env->cas, phy_lie_element_coefficient(element, i), &decision);
        if (status != PHY_OK) {
            return status;
        }
        if (decision == PHY_CAS_NONZERO) {
            *out_decision = PHY_CAS_NONZERO;
            return PHY_OK;
        }
        if (decision == PHY_CAS_UNKNOWN) {
            combined = PHY_CAS_UNKNOWN;
        }
    }
    *out_decision = combined;
    return PHY_OK;
}

static phy_status eval_zero_query(phy_env *env, phy_ir_ref expr,
                                  phy_value *out_value)
{
    if (arg_count(env, expr) != 1u) {
        return PHY_ERR_PARSE;
    }
    phy_value target;
    phy_status status = arg_value(env, expr, 0u, &target);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    switch (target.kind) {
    case PHY_VALUE_SCALAR:
        status = phy_cas_is_zero(env->cas, target.as.scalar, &decision);
        break;
    case PHY_VALUE_FORM:
        status = phy_form_is_zero(env->cas, target.as.form, &decision);
        break;
    case PHY_VALUE_LIE_FORM:
        status =
            phy_lie_form_is_zero(env->cas, target.as.lie_form, &decision);
        break;
    case PHY_VALUE_TENSOR:
        status = tensor_is_zero(env, target.as.tensor, &decision);
        break;
    case PHY_VALUE_LIE_ELEMENT:
        status = element_is_zero(env, target.as.element, &decision);
        break;
    default:
        return PHY_ERR_TYPE;
    }
    return status == PHY_OK ? decision_value(env, decision, out_value)
                            : status;
}

static phy_status eval_equivalent_query(phy_env *env, phy_ir_ref expr,
                                        phy_value *out_value)
{
    if (arg_count(env, expr) != 2u) {
        return PHY_ERR_PARSE;
    }
    phy_value left;
    phy_status status = arg_value(env, expr, 0u, &left);
    if (status != PHY_OK) {
        return status;
    }
    phy_value right;
    status = arg_value(env, expr, 1u, &right);
    if (status != PHY_OK) {
        return status;
    }
    if (left.kind != right.kind) {
        return PHY_ERR_TYPE;
    }
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    switch (left.kind) {
    case PHY_VALUE_SCALAR:
        status = phy_cas_equivalent(env->cas, left.as.scalar, right.as.scalar,
                                    &decision);
        break;
    case PHY_VALUE_FORM:
        status = phy_form_equivalent(env->cas, left.as.form, right.as.form,
                                     &decision);
        break;
    case PHY_VALUE_LIE_FORM:
        status = phy_lie_form_equivalent(env->cas, left.as.lie_form,
                                         right.as.lie_form, &decision);
        break;
    default:
        return PHY_ERR_TYPE;
    }
    return status == PHY_OK ? decision_value(env, decision, out_value)
                            : status;
}

/* ------------------------------------------------------------- dispatch */

static phy_status eval_operator(phy_env *env, phy_ir_ref expr,
                                bool *out_handled, phy_value *out_value)
{
    const phy_ir_symbol head = phy_ir_head(env->ir, expr);
    eval_head which = EVAL_HEAD_COUNT;
    for (size_t i = 0u; i < (size_t)EVAL_HEAD_COUNT; ++i) {
        if (env->head[i] == head) {
            which = (eval_head)i;
            break;
        }
    }
    /*
     * `handled` is what keeps an unevaluated reserved head from being rebuilt
     * as itself. Only a head absent from the table falls through to the generic
     * path; a head this layer owns and cannot evaluate returns its error.
     */
    *out_handled = which != EVAL_HEAD_COUNT;
    switch (which) {
    case EVAL_HEAD_MANIFOLD:
        return eval_manifold(env, expr, out_value);
    case EVAL_HEAD_DIFFERENTIAL_FORM:
        return eval_differential_form(env, expr, out_value);
    case EVAL_HEAD_METRIC:
        return eval_component_tensor(env, expr, 2u, PHY_IR_INDEX_LOWER, "g",
                                     out_value);
    case EVAL_HEAD_VECTOR_FIELD:
        return eval_component_tensor(env, expr, 1u, PHY_IR_INDEX_UPPER, "v",
                                     out_value);
    case EVAL_HEAD_EXTERIOR_D:
        return eval_exterior_derivative(env, expr, out_value);
    case EVAL_HEAD_INTERIOR_PRODUCT:
        return eval_interior_product(env, expr, out_value);
    case EVAL_HEAD_HODGE_STAR:
        return eval_hodge(env, expr, out_value);
    case EVAL_HEAD_VOLUME:
        return eval_volume(env, expr, out_value);

    case EVAL_HEAD_LIE_GROUP:
        return eval_lie_group(env, expr, out_value);
    case EVAL_HEAD_LIE_ALGEBRA:
        return eval_lie_algebra(env, expr, out_value);
    case EVAL_HEAD_GENERATOR:
        return eval_generator(env, expr, out_value);
    case EVAL_HEAD_LIE_ELEMENT:
        return eval_lie_element(env, expr, out_value);
    case EVAL_HEAD_LIE_BRACKET:
        return eval_lie_bracket(env, expr, out_value);
    case EVAL_HEAD_STRUCTURE_CONSTANT:
        return eval_structure_constant(env, expr, out_value);
    case EVAL_HEAD_KILLING:
        return eval_killing(env, expr, out_value);

    case EVAL_HEAD_LIE_FORM:
        return eval_lie_form(env, expr, false, out_value);
    case EVAL_HEAD_GAUGE_CONNECTION:
        return eval_lie_form(env, expr, true, out_value);
    case EVAL_HEAD_COVARIANT_D:
        return eval_covariant_derivative(env, expr, out_value);
    case EVAL_HEAD_FIELD_STRENGTH:
        return eval_field_strength(env, expr, false, out_value);
    case EVAL_HEAD_BIANCHI:
        return eval_field_strength(env, expr, true, out_value);
    case EVAL_HEAD_GAUGE_VARIATION:
        return eval_gauge_variation(env, expr, out_value);
    case EVAL_HEAD_YANG_MILLS_LAGRANGIAN:
        return eval_yang_mills_lagrangian(env, expr, out_value);
    case EVAL_HEAD_COLOR_COMPONENT:
        return eval_color_component(env, expr, out_value);

    case EVAL_HEAD_CURVATURE:
        return eval_curvature(env, expr, out_value);
    case EVAL_HEAD_INVERSE_METRIC:
    case EVAL_HEAD_CHRISTOFFEL:
    case EVAL_HEAD_RIEMANN:
    case EVAL_HEAD_RIEMANN_MIXED:
    case EVAL_HEAD_RICCI:
    case EVAL_HEAD_RICCI_SCALAR:
    case EVAL_HEAD_EINSTEIN:
        return eval_curvature_part(env, expr, which, out_value);

    case EVAL_HEAD_COMPONENT:
        return eval_component(env, expr, out_value);
    case EVAL_HEAD_DEGREE:
    case EVAL_HEAD_DIMENSION:
    case EVAL_HEAD_RANK:
        return eval_measure(env, expr, which, out_value);
    case EVAL_HEAD_ZERO_Q:
        return eval_zero_query(env, expr, out_value);
    case EVAL_HEAD_EQUIVALENT_Q:
        return eval_equivalent_query(env, expr, out_value);

    default:
        break;
    }
    return PHY_ERR_UNSUPPORTED;
}

/* --------------------------------------------------- structural algebra */

/*
 * `alpha + beta` and `s * alpha` on objects.
 *
 * Sums are homogeneous -- adding a form to a Lie element is not a slip the
 * evaluator should paper over -- and a product admits at most one object
 * factor, because the product of two forms is the wedge and has its own
 * spelling. Subtraction and division need no cases: the parser already writes
 * `a - b` as `a + (-1)*b` and `a/2` as `a * 2^-1`.
 */
static phy_status eval_object_sum(phy_env *env, const phy_value *terms,
                                  size_t count, phy_value *out_value)
{
    phy_value accumulator = terms[0];
    for (size_t i = 1u; i < count; ++i) {
        if (terms[i].kind != accumulator.kind) {
            return PHY_ERR_TYPE;
        }
        phy_status status = PHY_ERR_TYPE;
        phy_value next = accumulator;
        if (accumulator.kind == PHY_VALUE_FORM) {
            phy_form *sum = NULL;
            status = phy_form_add(env->cas, accumulator.as.form,
                                  terms[i].as.form, &sum);
            if (status == PHY_OK) {
                status = publish(env, PHY_VALUE_FORM, sum, &accumulator,
                                 &terms[i], &next);
            }
        } else if (accumulator.kind == PHY_VALUE_LIE_FORM) {
            phy_lie_form *sum = NULL;
            status = phy_lie_form_add(env->cas, accumulator.as.lie_form,
                                      terms[i].as.lie_form, &sum);
            if (status == PHY_OK) {
                status = publish(env, PHY_VALUE_LIE_FORM, sum, &accumulator,
                                 &terms[i], &next);
            }
        } else if (accumulator.kind == PHY_VALUE_LIE_ELEMENT) {
            phy_lie_element *sum = NULL;
            status = phy_lie_element_add(accumulator.as.element,
                                         terms[i].as.element, &sum);
            if (status == PHY_OK) {
                status = publish(env, PHY_VALUE_LIE_ELEMENT, sum, &accumulator,
                                 &terms[i], &next);
            }
        }
        if (status != PHY_OK) {
            return status;
        }
        accumulator = next;
    }
    *out_value = accumulator;
    return PHY_OK;
}

static phy_status eval_object_scale(phy_env *env, phy_value object,
                                    phy_ir_ref scalar, phy_value *out_value)
{
    phy_status status = PHY_ERR_TYPE;
    if (object.kind == PHY_VALUE_FORM) {
        phy_form *result = NULL;
        status = phy_form_scale(env->cas, object.as.form, scalar, &result);
        return status == PHY_OK ? publish(env, PHY_VALUE_FORM, result, &object,
                                          NULL, out_value)
                                : status;
    }
    if (object.kind == PHY_VALUE_LIE_FORM) {
        phy_lie_form *result = NULL;
        status =
            phy_lie_form_scale(env->cas, object.as.lie_form, scalar, &result);
        return status == PHY_OK ? publish(env, PHY_VALUE_LIE_FORM, result,
                                          &object, NULL, out_value)
                                : status;
    }
    if (object.kind == PHY_VALUE_LIE_ELEMENT) {
        phy_lie_element *result = NULL;
        status = phy_lie_element_scale(object.as.element, scalar, &result);
        return status == PHY_OK ? publish(env, PHY_VALUE_LIE_ELEMENT, result,
                                          &object, NULL, out_value)
                                : status;
    }
    return PHY_ERR_TYPE;
}

static phy_status eval_form_wedge(phy_env *env, const phy_value *factors,
                                  size_t count, phy_value *out_value)
{
    phy_value accumulator = factors[0];
    for (size_t i = 1u; i < count; ++i) {
        if (factors[i].kind != PHY_VALUE_FORM) {
            return PHY_ERR_TYPE;
        }
        phy_form *product = NULL;
        phy_status status = phy_form_wedge(env->cas, accumulator.as.form,
                                           factors[i].as.form, &product);
        if (status != PHY_OK) {
            return status;
        }
        phy_value next;
        status = publish(env, PHY_VALUE_FORM, product, &accumulator,
                         &factors[i], &next);
        if (status != PHY_OK) {
            return status;
        }
        accumulator = next;
    }
    *out_value = accumulator;
    return PHY_OK;
}

static phy_status eval_nary(phy_env *env, phy_ir_ref expr, phy_ir_kind kind,
                            phy_value *out_value)
{
    const size_t count = phy_ir_child_count(env->ir, expr);
    if (count == 0u || count > EVAL_MAX_LIST) {
        return count == 0u ? PHY_ERR_INVALID_ARGUMENT : PHY_ERR_TERM_LIMIT;
    }
    phy_value values[EVAL_MAX_LIST];
    phy_ir_ref scalars[EVAL_MAX_LIST];
    size_t scalar_count = 0u;
    size_t object_index = EVAL_MAX_LIST;
    size_t object_count = 0u;
    for (size_t i = 0u; i < count; ++i) {
        const phy_status status =
            eval_node(env, phy_ir_child(env->ir, expr, i), &values[i]);
        if (status != PHY_OK) {
            return status;
        }
        if (values[i].kind == PHY_VALUE_SCALAR) {
            scalars[scalar_count++] = values[i].as.scalar;
        } else {
            object_index = i;
            object_count++;
        }
    }

    if (object_count == 0u) {
        phy_ir_ref result = PHY_IR_NULL;
        phy_status status;
        if (kind == PHY_IR_ADD) {
            status = phy_cas_add(env->cas, scalars, scalar_count, &result);
        } else if (kind == PHY_IR_MUL) {
            status = phy_cas_mul(env->cas, scalars, scalar_count, &result);
        } else {
            result = phy_ir_wedge(env->ir, scalars, scalar_count);
            status = result == PHY_IR_NULL ? phy_ir_last_error(env->ir)
                                           : phy_cas_simplify(env->cas, result,
                                                              &result);
        }
        if (status == PHY_OK) {
            *out_value = scalar_value(result);
        }
        return status;
    }

    if (kind == PHY_IR_ADD) {
        return object_count == count
                   ? eval_object_sum(env, values, count, out_value)
                   : PHY_ERR_TYPE;
    }
    if (kind == PHY_IR_WEDGE) {
        return object_count == count
                   ? eval_form_wedge(env, values, count, out_value)
                   : PHY_ERR_TYPE;
    }
    if (object_count != 1u) {
        return PHY_ERR_TYPE;
    }
    phy_ir_ref coefficient = PHY_IR_NULL;
    phy_status status =
        scalar_count == 0u
            ? phy_cas_number(env->cas, 1, 1, &coefficient)
            : phy_cas_mul(env->cas, scalars, scalar_count, &coefficient);
    if (status != PHY_OK) {
        return status;
    }
    return eval_object_scale(env, values[object_index], coefficient,
                             out_value);
}

/* ------------------------------------------------------- the entry point */

phy_status eval_node(phy_env *env, phy_ir_ref expr, phy_value *out_value)
{
    if (env == NULL || out_value == NULL || expr == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_ir_kind kind = phy_ir_kind_of(env->ir, expr);
    switch (kind) {
    case PHY_IR_SYMBOL: {
        phy_value bound;
        if (eval_lookup(env, phy_ir_head(env->ir, expr), &bound)) {
            *out_value = bound;
            return PHY_OK;
        }
        *out_value = scalar_value(expr);
        return PHY_OK;
    }
    case PHY_IR_OPERATOR: {
        /*
         * A reserved head evaluates; every other headed operator -- gamma
         * matrices, generators, and the bounded scalar-QFT objects -- keeps its
         * structure and has its operands evaluated, which is what the scalar
         * layer already promised for them.
         */
        bool handled = false;
        const phy_status status =
            eval_operator(env, expr, &handled, out_value);
        if (handled) {
            return status;
        }
        break;
    }
    case PHY_IR_ADD:
    case PHY_IR_MUL:
    case PHY_IR_WEDGE:
        return eval_nary(env, expr, kind, out_value);
    default:
        break;
    }

    /*
     * The scalar remainder. Children are evaluated first so that a binding or a
     * reserved head nested inside ordinary arithmetic is resolved -- which is
     * why `2*Component[g,0,0] + 1` is a number and not a structure.
     */
    const size_t count = phy_ir_child_count(env->ir, expr);
    if (count == 0u || kind == PHY_IR_TENSOR) {
        phy_ir_ref result = PHY_IR_NULL;
        const phy_status status = phy_cas_simplify(env->cas, expr, &result);
        if (status == PHY_OK) {
            *out_value = scalar_value(result);
        }
        return status;
    }
    if (count > EVAL_MAX_LIST) {
        return PHY_ERR_TERM_LIMIT;
    }

    phy_ir_ref children[EVAL_MAX_LIST];
    const size_t evaluated = kind == PHY_IR_DERIVATIVE ? 1u : count;
    for (size_t i = 0u; i < count; ++i) {
        const phy_ir_ref child = phy_ir_child(env->ir, expr, i);
        if (i >= evaluated) {
            /*
             * A derivative's variables are not values. Substituting a bound
             * name into one would silently turn D[x^2,x] into nonsense, so a
             * bound variable is a typed error instead.
             */
            if (phy_ir_kind_of(env->ir, child) == PHY_IR_SYMBOL &&
                eval_lookup(env, phy_ir_head(env->ir, child), NULL)) {
                return PHY_ERR_TYPE;
            }
            children[i] = child;
            continue;
        }
        phy_value value;
        const phy_status status = eval_node(env, child, &value);
        if (status != PHY_OK) {
            return status;
        }
        if (value.kind != PHY_VALUE_SCALAR) {
            return PHY_ERR_TYPE;
        }
        children[i] = value.as.scalar;
    }

    phy_ir_ref rebuilt = PHY_IR_NULL;
    switch (kind) {
    case PHY_IR_POW:
        rebuilt = phy_ir_pow(env->ir, children[0], children[1]);
        break;
    case PHY_IR_EQUATION:
        rebuilt = phy_ir_equation(env->ir, children[0], children[1]);
        break;
    case PHY_IR_NCMUL:
        rebuilt = phy_ir_ncmul(env->ir, children, count);
        break;
    case PHY_IR_FUNCTION:
        rebuilt = phy_ir_function(env->ir, phy_ir_head(env->ir, expr),
                                  children, count);
        break;
    case PHY_IR_OPERATOR:
        rebuilt = phy_ir_operator(env->ir, phy_ir_head(env->ir, expr),
                                  children, count);
        break;
    case PHY_IR_DERIVATIVE:
        rebuilt = phy_ir_derivative(env->ir, children[0], &children[1],
                                    count - 1u);
        break;
    default:
        return PHY_ERR_UNSUPPORTED;
    }
    if (rebuilt == PHY_IR_NULL) {
        return phy_ir_last_error(env->ir);
    }
    phy_ir_ref result = PHY_IR_NULL;
    const phy_status status = phy_cas_simplify(env->cas, rebuilt, &result);
    if (status == PHY_OK) {
        *out_value = scalar_value(result);
    }
    return status;
}

phy_status phy_eval_expression(phy_env *env, phy_ir_ref expression,
                               phy_value *out_value)
{
    return eval_node(env, expression, out_value);
}
