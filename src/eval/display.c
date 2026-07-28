/*
 * Values back to something a cell can show, and the command entry point.
 *
 * A notebook that computes a curvature and then prints `Form` has not shown
 * the reader anything. So an object with a canonical expansion in the typed IR
 * gets that expansion -- a form becomes its coframe sum, an algebra-valued form
 * a noncommutative sum over the algebra basis -- and the existing typed-IR
 * renderer draws it with no new layout code. Objects with no such expansion get
 * a descriptor line instead, and are honest about being a handle.
 */
#include <string.h>

#include "eval_internal.h"

#define DISPLAY_MAX_TERMS 64u

/* ------------------------------------------------------------- text ---- */

typedef struct {
    char *buffer;
    size_t capacity;
    size_t at;
    bool overflow;
} text_writer;

static void write_text(text_writer *writer, const char *text)
{
    if (text == NULL) {
        text = "?";
    }
    const size_t length = strlen(text);
    if (writer->overflow || writer->at + length + 1u > writer->capacity) {
        writer->overflow = true;
        return;
    }
    memcpy(writer->buffer + writer->at, text, length);
    writer->at += length;
    writer->buffer[writer->at] = '\0';
}

static void write_unsigned(text_writer *writer, unsigned value)
{
    char digits[12];
    size_t at = sizeof digits;
    digits[--at] = '\0';
    do {
        digits[--at] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && at > 0u);
    write_text(writer, &digits[at]);
}

/* ------------------------------------------------------- form expansion */

/*
 * The coframe symbol of an axis: the coordinate name with a `d` in front.
 * Interned rather than synthesized per call, so `dtheta` is one node however
 * many components mention it.
 */
static phy_ir_symbol coframe_symbol(phy_env *env, const phy_chart *chart,
                                    unsigned axis)
{
    const char *name = phy_chart_coordinate_name(chart, axis);
    if (name == NULL) {
        return PHY_IR_NO_SYMBOL;
    }
    char spelling[40];
    const size_t length = strlen(name);
    if (length + 2u > sizeof spelling) {
        return PHY_IR_NO_SYMBOL;
    }
    spelling[0] = 'd';
    memcpy(spelling + 1u, name, length + 1u);
    return phy_ir_intern(env->ir, spelling);
}

static phy_status form_expansion(phy_env *env, const phy_form *form,
                                 phy_ir_ref *out_ref)
{
    const unsigned degree = phy_form_degree(form);
    const phy_chart *chart = phy_form_chart(form);
    const size_t positions = phy_form_component_count(form);
    if (chart == NULL || positions > DISPLAY_MAX_TERMS) {
        return PHY_ERR_TERM_LIMIT;
    }

    phy_ir_ref terms[DISPLAY_MAX_TERMS];
    size_t term_count = 0u;
    for (size_t position = 0u; position < positions; ++position) {
        phy_ir_ref component = PHY_IR_NULL;
        phy_status status = phy_form_get_at(form, position, &component);
        if (status != PHY_OK) {
            return status;
        }
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        status = phy_cas_is_zero(env->cas, component, &decision);
        if (status != PHY_OK) {
            return status;
        }
        if (decision == PHY_CAS_ZERO) {
            continue;
        }
        if (degree == 0u) {
            terms[term_count++] = component;
            continue;
        }

        unsigned axes[PHY_FORM_MAX_DEGREE];
        status = phy_form_position_indices(form, position, axes);
        if (status != PHY_OK) {
            return status;
        }
        phy_ir_ref basis[PHY_FORM_MAX_DEGREE];
        for (unsigned i = 0u; i < degree; ++i) {
            const phy_ir_symbol symbol = coframe_symbol(env, chart, axes[i]);
            basis[i] = symbol == PHY_IR_NO_SYMBOL
                           ? PHY_IR_NULL
                           : phy_ir_symbol_ref(env->ir, symbol);
            if (basis[i] == PHY_IR_NULL) {
                return PHY_ERR_TERM_LIMIT;
            }
        }
        const phy_ir_ref covector =
            degree == 1u ? basis[0]
                         : phy_ir_wedge(env->ir, basis, (size_t)degree);
        if (covector == PHY_IR_NULL) {
            return phy_ir_last_error(env->ir);
        }
        const phy_ir_ref factors[2] = {component, covector};
        const phy_ir_ref term = phy_ir_mul(env->ir, factors, 2u);
        if (term == PHY_IR_NULL) {
            return phy_ir_last_error(env->ir);
        }
        terms[term_count++] = term;
    }

    if (term_count == 0u) {
        *out_ref = phy_ir_integer(env->ir, 0);
        return *out_ref == PHY_IR_NULL ? phy_ir_last_error(env->ir) : PHY_OK;
    }
    const phy_ir_ref sum = phy_ir_add(env->ir, terms, term_count);
    if (sum == PHY_IR_NULL) {
        return phy_ir_last_error(env->ir);
    }
    return phy_cas_simplify(env->cas, sum, out_ref);
}

static phy_status lie_form_expansion(phy_env *env, const phy_lie_form *form,
                                     phy_ir_ref *out_ref)
{
    const phy_lie_algebra *algebra = phy_lie_form_algebra(form);
    const unsigned colours = phy_lie_form_algebra_dimension(form);
    if (colours > DISPLAY_MAX_TERMS) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_ir_ref terms[DISPLAY_MAX_TERMS];
    size_t term_count = 0u;
    for (unsigned colour = 0u; colour < colours; ++colour) {
        phy_ir_ref expansion = PHY_IR_NULL;
        const phy_status status = form_expansion(
            env, phy_lie_form_component(form, colour), &expansion);
        if (status != PHY_OK) {
            return status;
        }
        int64_t zero = 0;
        if (phy_ir_integer_value(env->ir, expansion, &zero) && zero == 0) {
            continue;
        }
        const phy_ir_symbol basis =
            phy_lie_algebra_basis_symbol(algebra, colour);
        const phy_ir_ref generator = phy_ir_symbol_ref(env->ir, basis);
        if (generator == PHY_IR_NULL) {
            return phy_ir_last_error(env->ir);
        }
        /*
         * Noncommutative on purpose. The generators do not commute, and a
         * display that used ordinary multiplication would be inviting the
         * scalar CAS to collect terms it has no right to collect.
         */
        const phy_ir_ref factors[2] = {generator, expansion};
        const phy_ir_ref term = phy_ir_ncmul(env->ir, factors, 2u);
        if (term == PHY_IR_NULL) {
            return phy_ir_last_error(env->ir);
        }
        terms[term_count++] = term;
    }
    if (term_count == 0u) {
        *out_ref = phy_ir_integer(env->ir, 0);
        return *out_ref == PHY_IR_NULL ? phy_ir_last_error(env->ir) : PHY_OK;
    }
    const phy_ir_ref sum = phy_ir_add(env->ir, terms, term_count);
    if (sum == PHY_IR_NULL) {
        return phy_ir_last_error(env->ir);
    }
    return phy_cas_simplify(env->cas, sum, out_ref);
}

static phy_status element_expansion(phy_env *env,
                                    const phy_lie_element *element,
                                    phy_ir_ref *out_ref)
{
    const phy_lie_algebra *algebra = phy_lie_element_algebra(element);
    const unsigned dimension = phy_lie_algebra_dimension(algebra);
    phy_ir_ref terms[PHY_LIE_MAX_DIM];
    size_t term_count = 0u;
    for (unsigned i = 0u; i < dimension; ++i) {
        const phy_ir_ref coefficient =
            phy_lie_element_coefficient(element, i);
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        const phy_status status =
            phy_cas_is_zero(env->cas, coefficient, &decision);
        if (status != PHY_OK) {
            return status;
        }
        if (decision == PHY_CAS_ZERO) {
            continue;
        }
        const phy_ir_ref generator = phy_ir_symbol_ref(
            env->ir, phy_lie_algebra_basis_symbol(algebra, i));
        if (generator == PHY_IR_NULL) {
            return phy_ir_last_error(env->ir);
        }
        /* Coefficients are scalars, so this product really is commutative. */
        const phy_ir_ref factors[2] = {coefficient, generator};
        const phy_ir_ref term = phy_ir_mul(env->ir, factors, 2u);
        if (term == PHY_IR_NULL) {
            return phy_ir_last_error(env->ir);
        }
        terms[term_count++] = term;
    }
    if (term_count == 0u) {
        *out_ref = phy_ir_integer(env->ir, 0);
        return *out_ref == PHY_IR_NULL ? phy_ir_last_error(env->ir) : PHY_OK;
    }
    const phy_ir_ref sum = phy_ir_add(env->ir, terms, term_count);
    if (sum == PHY_IR_NULL) {
        return phy_ir_last_error(env->ir);
    }
    return phy_cas_simplify(env->cas, sum, out_ref);
}

/*
 * A rank-3 or rank-4 tensor as the list of its nonvanishing components,
 * GRTensor-style: List(Gamma(theta,phi,phi) = -cos(theta)*sin(theta), ...).
 * A nested list would bury eight zeros around every interesting entry; the
 * reader of a Christoffel symbol wants the entries that exist, named by the
 * coordinates they carry. Falls back to the descriptor (a NULL expansion)
 * when the tensor is anonymous or the list would not fit a cell.
 */
static phy_status tensor_component_equations(phy_env *env,
                                             const phy_tensor *tensor,
                                             phy_ir_ref *out_ref)
{
    const unsigned rank = phy_tensor_rank(tensor);
    const phy_chart *chart = phy_tensor_chart(tensor);
    const phy_ir_symbol head = phy_tensor_head(tensor);
    *out_ref = PHY_IR_NULL;
    if (chart == NULL || head == PHY_IR_NO_SYMBOL) {
        return PHY_OK;
    }

    phy_ir_ref equations[DISPLAY_MAX_TERMS];
    size_t equation_count = 0u;
    const size_t positions = phy_tensor_component_count(tensor);
    for (size_t flat = 0u; flat < positions; ++flat) {
        unsigned indices[PHY_TENSOR_MAX_RANK];
        phy_status status = phy_tensor_unflatten(tensor, flat, indices);
        if (status != PHY_OK) {
            return status;
        }
        phy_ir_ref component = PHY_IR_NULL;
        status = phy_tensor_component_expression(env->cas, tensor, indices,
                                                 &component);
        if (status != PHY_OK) {
            return status;
        }
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        status = phy_cas_is_zero(env->cas, component, &decision);
        if (status != PHY_OK) {
            return status;
        }
        if (decision == PHY_CAS_ZERO) {
            continue;
        }
        if (equation_count >= DISPLAY_MAX_TERMS) {
            *out_ref = PHY_IR_NULL; /* too many entries: stay a descriptor */
            return PHY_OK;
        }

        phy_ir_ref labels[PHY_TENSOR_MAX_RANK];
        for (unsigned slot = 0u; slot < rank; ++slot) {
            const char *name =
                phy_chart_coordinate_name(chart, indices[slot]);
            const phy_ir_symbol label =
                name == NULL ? PHY_IR_NO_SYMBOL
                             : phy_ir_intern(env->ir, name);
            labels[slot] = label == PHY_IR_NO_SYMBOL
                               ? PHY_IR_NULL
                               : phy_ir_symbol_ref(env->ir, label);
            if (labels[slot] == PHY_IR_NULL) {
                return PHY_ERR_TERM_LIMIT;
            }
        }
        const phy_ir_ref lhs =
            phy_ir_function(env->ir, head, labels, rank);
        if (lhs == PHY_IR_NULL) {
            return phy_ir_last_error(env->ir);
        }
        const phy_ir_ref equation =
            phy_ir_equation(env->ir, lhs, component);
        if (equation == PHY_IR_NULL) {
            return phy_ir_last_error(env->ir);
        }
        equations[equation_count++] = equation;
    }

    if (equation_count == 0u) {
        *out_ref = phy_ir_integer(env->ir, 0);
        return *out_ref == PHY_IR_NULL ? phy_ir_last_error(env->ir) : PHY_OK;
    }
    *out_ref = phy_ir_function(env->ir, env->list_head, equations,
                               equation_count);
    return *out_ref == PHY_IR_NULL ? phy_ir_last_error(env->ir) : PHY_OK;
}

static phy_status tensor_expansion(phy_env *env, const phy_tensor *tensor,
                                   phy_ir_ref *out_ref)
{
    const unsigned rank = phy_tensor_rank(tensor);
    const unsigned dimension = phy_tensor_dimension(tensor);
    unsigned indices[PHY_TENSOR_MAX_RANK] = {0u, 0u, 0u, 0u};
    if (rank == 0u) {
        return phy_tensor_component_expression(env->cas, tensor, NULL,
                                               out_ref);
    }
    if (rank > 2u) {
        return tensor_component_equations(env, tensor, out_ref);
    }
    const phy_ir_symbol list = env->list_head;
    phy_ir_ref rows[PHY_TENSOR_MAX_DIM];
    for (unsigned row = 0u; row < dimension; ++row) {
        indices[0] = row;
        if (rank == 1u) {
            const phy_status status = phy_tensor_component_expression(
                env->cas, tensor, indices, &rows[row]);
            if (status != PHY_OK) {
                return status;
            }
            continue;
        }
        phy_ir_ref entries[PHY_TENSOR_MAX_DIM];
        for (unsigned column = 0u; column < dimension; ++column) {
            indices[1] = column;
            const phy_status status = phy_tensor_component_expression(
                env->cas, tensor, indices, &entries[column]);
            if (status != PHY_OK) {
                return status;
            }
        }
        rows[row] = phy_ir_function(env->ir, list, entries, dimension);
        if (rows[row] == PHY_IR_NULL) {
            return phy_ir_last_error(env->ir);
        }
    }
    *out_ref = phy_ir_function(env->ir, list, rows, dimension);
    return *out_ref == PHY_IR_NULL ? phy_ir_last_error(env->ir) : PHY_OK;
}

phy_status phy_eval_value_expression(phy_env *env, phy_value value,
                                     phy_ir_ref *out_ref)
{
    if (env == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    switch (value.kind) {
    case PHY_VALUE_SCALAR:
        *out_ref = value.as.scalar;
        return PHY_OK;
    case PHY_VALUE_FORM:
        return form_expansion(env, value.as.form, out_ref);
    case PHY_VALUE_LIE_FORM:
        return lie_form_expansion(env, value.as.lie_form, out_ref);
    case PHY_VALUE_LIE_ELEMENT:
        return element_expansion(env, value.as.element, out_ref);
    case PHY_VALUE_TENSOR:
        return tensor_expansion(env, value.as.tensor, out_ref);
    default:
        break;
    }
    return PHY_OK;
}

/* ---------------------------------------------------------- descriptors */

phy_status phy_eval_describe(const phy_env *env, phy_value value, char *buffer,
                             size_t capacity)
{
    if (env == NULL || buffer == NULL ||
        capacity < PHY_EVAL_DESCRIPTION_CAPACITY) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    text_writer writer = {buffer, PHY_EVAL_DESCRIPTION_CAPACITY, 0u, false};
    buffer[0] = '\0';
    write_text(&writer, phy_value_kind_name(value.kind));

    switch (value.kind) {
    case PHY_VALUE_MANIFOLD: {
        const phy_manifold *manifold = value.as.manifold;
        const unsigned dimension = phy_manifold_dimension(manifold);
        const unsigned negative = phy_manifold_negative_count(manifold);
        const phy_orientation orientation =
            phy_manifold_orientation(manifold);
        write_text(&writer, " ");
        write_text(&writer, phy_manifold_name(manifold));
        write_text(&writer, " dim ");
        write_unsigned(&writer, dimension);
        write_text(&writer, negative == 0u  ? " Riemannian"
                            : negative == 1u ? " Lorentzian"
                                             : " mixed");
        write_text(&writer,
                   orientation == PHY_ORIENTATION_NONE ? " unoriented"
                   : orientation == PHY_ORIENTATION_NEGATIVE ? " -oriented"
                                                             : " +oriented");
        const phy_chart *chart = phy_manifold_chart(manifold, 0u);
        if (chart != NULL) {
            write_text(&writer, " (");
            for (unsigned i = 0u; i < dimension; ++i) {
                if (i != 0u) {
                    write_text(&writer, ",");
                }
                write_text(&writer, phy_chart_coordinate_name(chart, i));
            }
            write_text(&writer, ")");
        }
        break;
    }
    case PHY_VALUE_FORM:
        write_text(&writer, " degree ");
        write_unsigned(&writer, phy_form_degree(value.as.form));
        write_text(&writer, " on ");
        write_text(&writer,
                   phy_manifold_name(phy_form_manifold(value.as.form)));
        write_text(&writer, " dim ");
        write_unsigned(&writer, phy_form_dimension(value.as.form));
        break;
    case PHY_VALUE_TENSOR:
        write_text(&writer, " ");
        write_text(&writer, phy_tensor_name(value.as.tensor));
        write_text(&writer, " rank ");
        write_unsigned(&writer, phy_tensor_rank(value.as.tensor));
        write_text(&writer, " dim ");
        write_unsigned(&writer, phy_tensor_dimension(value.as.tensor));
        break;
    case PHY_VALUE_LIE_GROUP:
        write_text(&writer, " ");
        write_text(&writer, phy_lie_group_name(value.as.group));
        write_text(&writer, " rep ");
        write_unsigned(&writer, phy_lie_group_representation_dimension(
                                    value.as.group));
        write_text(&writer, phy_lie_group_is_compact(value.as.group)
                                ? " compact"
                                : " noncompact");
        break;
    case PHY_VALUE_LIE_ALGEBRA:
        write_text(&writer, " ");
        write_text(&writer, phy_lie_algebra_name(value.as.algebra));
        write_text(&writer, " dim ");
        write_unsigned(&writer,
                       phy_lie_algebra_dimension(value.as.algebra));
        break;
    case PHY_VALUE_LIE_ELEMENT:
        write_text(&writer, " of ");
        write_text(&writer, phy_lie_algebra_name(
                                phy_lie_element_algebra(value.as.element)));
        break;
    case PHY_VALUE_LIE_FORM:
        write_text(&writer, " degree ");
        write_unsigned(&writer, phy_lie_form_degree(value.as.lie_form));
        write_text(&writer, " of ");
        write_text(&writer, phy_lie_algebra_name(
                                phy_lie_form_algebra(value.as.lie_form)));
        write_text(&writer, " on ");
        write_text(&writer, phy_manifold_name(
                                phy_lie_form_manifold(value.as.lie_form)));
        break;
    case PHY_VALUE_CURVATURE: {
        const phy_tensor *ricci = phy_gr_ricci(value.as.curvature);
        write_text(&writer, " dim ");
        write_unsigned(&writer,
                       ricci != NULL ? phy_tensor_dimension(ricci) : 0u);
        write_text(&writer, " (Christoffel/Riemann/Ricci/Einstein)");
        break;
    }
    default:
        break;
    }
    /*
     * A truncated description names the wrong object as confidently as a
     * complete one, so overflow is a status rather than a shorter string. The
     * buffer still holds everything that fit, which is what a diagnostic wants.
     */
    return writer.overflow ? PHY_ERR_TERM_LIMIT : PHY_OK;
}

/* -------------------------------------------------------------- commands */

static phy_status apply_scalar_operation(phy_env *env,
                                         const phy_source_command *command,
                                         phy_ir_ref value,
                                         phy_ir_ref *out_ref)
{
    switch (command->operation) {
    case PHY_SOURCE_SIMPLIFY:
        *out_ref = value;
        return PHY_OK;
    case PHY_SOURCE_FULL_SIMPLIFY:
        return phy_cas_full_simplify(env->cas, value, out_ref);
    case PHY_SOURCE_EXPAND:
        return phy_cas_expand(env->cas, value, out_ref);
    case PHY_SOURCE_CANCEL:
        return phy_cas_reduce(env->cas, value, out_ref);
    case PHY_SOURCE_FACTOR:
        return phy_cas_factor(env->cas, value, out_ref);
    case PHY_SOURCE_APART:
        return phy_cas_apart(env->cas, value, out_ref);
    case PHY_SOURCE_TOGETHER:
    case PHY_SOURCE_NUMERATOR:
    case PHY_SOURCE_DENOMINATOR: {
        phy_ir_ref numerator = PHY_IR_NULL;
        phy_ir_ref denominator = PHY_IR_NULL;
        const phy_status status = phy_cas_rational_form(
            env->cas, value, &numerator, &denominator);
        if (status != PHY_OK) {
            return status;
        }
        if (command->operation == PHY_SOURCE_NUMERATOR) {
            *out_ref = numerator;
            return PHY_OK;
        }
        if (command->operation == PHY_SOURCE_DENOMINATOR) {
            *out_ref = denominator;
            return PHY_OK;
        }
        return phy_cas_div(env->cas, numerator, denominator, out_ref);
    }
    case PHY_SOURCE_DIFFERENTIATE:
    case PHY_SOURCE_INTEGRATE: {
        phy_ir_ref result = value;
        for (size_t i = 0u; i < command->variable_count; ++i) {
            const phy_ir_ref variable = command->variables[i];
            /*
             * Differentiating with respect to a bound name would differentiate
             * with respect to that name's value, which is not a variable.
             */
            if (phy_ir_kind_of(env->ir, variable) == PHY_IR_SYMBOL &&
                eval_lookup(env, phy_ir_head(env->ir, variable), NULL)) {
                return PHY_ERR_TYPE;
            }
            const phy_status status =
                command->operation == PHY_SOURCE_DIFFERENTIATE
                    ? phy_cas_diff(env->cas, result, variable, &result)
                    : phy_cas_integrate(env->cas, result, variable, &result);
            if (status != PHY_OK) {
                return status;
            }
        }
        *out_ref = result;
        return PHY_OK;
    }
    case PHY_SOURCE_SERIES:
        if (command->variable_count != 1u ||
            command->parameter == PHY_IR_NULL) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        if (eval_lookup(
                env, phy_ir_head(env->ir, command->variables[0]), NULL)) {
            return PHY_ERR_TYPE;
        }
        return phy_cas_series(
            env->cas, value, command->variables[0], command->parameter,
            command->series_order, out_ref);
    case PHY_SOURCE_NORMAL:
        if (command->normal_series) {
            if (eval_lookup(
                    env, phy_ir_head(env->ir, command->variables[0]),
                    NULL)) {
                return PHY_ERR_TYPE;
            }
            phy_ir_ref data = PHY_IR_NULL;
            phy_status status = phy_cas_series(
                env->cas, value, command->variables[0],
                command->parameter, command->series_order, &data);
            return status == PHY_OK
                       ? phy_cas_series_normal(env->cas, data, out_ref)
                       : status;
        }
        return phy_cas_series_normal(env->cas, value, out_ref);
    case PHY_SOURCE_LIMIT:
        if (command->variable_count != 1u ||
            command->parameter == PHY_IR_NULL) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        if (eval_lookup(
                env, phy_ir_head(env->ir, command->variables[0]), NULL)) {
            return PHY_ERR_TYPE;
        }
        return phy_cas_limit(
            env->cas, value, command->variables[0], command->parameter,
            (phy_cas_limit_direction)command->limit_direction, out_ref);
    case PHY_SOURCE_SOLVE:
        if (command->variable_count != 1u) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        if (eval_lookup(
                env, phy_ir_head(env->ir, command->variables[0]), NULL)) {
            return PHY_ERR_TYPE;
        }
        return phy_cas_solve(
            env->cas, value, command->variables[0], out_ref);
    default:
        break;
    }
    return PHY_ERR_UNSUPPORTED;
}

phy_status phy_eval_command(phy_env *env, const phy_source_command *command,
                            phy_value *out_value)
{
    if (env == NULL || command == NULL || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    if (command->operation == PHY_SOURCE_CLEAR) {
        if (command->target == PHY_IR_NO_SYMBOL) {
            phy_env_reset(env);
        } else {
            eval_unbind(env, command->target);
            eval_sweep(env, NULL);
        }
        out_value->kind = PHY_VALUE_NONE;
        return PHY_OK;
    }

    /*
     * The name an object constructor stamps on what it builds. Set before the
     * right-hand side runs and cleared after, so a nested constructor inside a
     * non-assignment cell is not mislabelled by a stale target.
     */
    env->pending_name = command->operation == PHY_SOURCE_ASSIGN
                            ? command->target
                            : PHY_IR_NO_SYMBOL;

    phy_value value;
    value.kind = PHY_VALUE_NONE;
    value.as.scalar = PHY_IR_NULL;
    phy_status status = eval_node(env, command->expression, &value);
    env->pending_name = PHY_IR_NO_SYMBOL;

    /*
     * FullSimplify of a typed object passes it through like Simplify does:
     * the object's components are already in normal form, and refusing
     * FullSimplify[Ricci[c]] with a type error would punish the reader for
     * asking politely.
     */
    if (status == PHY_OK && command->operation != PHY_SOURCE_ASSIGN &&
        command->operation != PHY_SOURCE_SIMPLIFY &&
        !(command->operation == PHY_SOURCE_FULL_SIMPLIFY &&
          value.kind != PHY_VALUE_SCALAR)) {
        /*
         * Every remaining operation is scalar algebra. `Expand[M]` on a
         * manifold is a type error rather than a silently ignored request.
         */
        if (value.kind != PHY_VALUE_SCALAR) {
            status = PHY_ERR_TYPE;
        } else {
            phy_ir_ref result = PHY_IR_NULL;
            status = apply_scalar_operation(env, command, value.as.scalar,
                                            &result);
            if (status == PHY_OK) {
                value.kind = PHY_VALUE_SCALAR;
                value.as.scalar = result;
            }
        }
    }

    if (status == PHY_OK && command->operation == PHY_SOURCE_ASSIGN) {
        status = eval_bind(env, command->target, value);
    }

    /*
     * The sweep runs on both paths. A command that failed half-way through a
     * gauge expression has already registered intermediates, and they are
     * exactly what nothing will ever reach again.
     */
    eval_sweep(env, status == PHY_OK ? &value : NULL);
    if (status != PHY_OK) {
        return status;
    }
    *out_value = value;
    return PHY_OK;
}
