#include "phy/qft_scalar.h"

#include <limits.h>
#include <string.h>

#include "phy/platform.h"

struct phy_phi4_model {
    phy_cas *cas;
    phy_ir_symbol field_name;
    phy_ir_ref mass;
    phy_ir_ref coupling;
    unsigned dimension;
    phy_ir_symbol head_field;
    phy_ir_symbol head_partial;
    phy_ir_symbol head_box;
    phy_ir_symbol head_lorentz_dot;
    phy_ir_symbol head_propagator;
    phy_ir_symbol head_tadpole;
    phy_ir_symbol head_bubble;
    phy_ir_symbol index_mu;
    phy_ir_symbol space_lorentz;
};

static phy_status ir_failure(phy_ir_context *ir)
{
    const phy_status status = phy_ir_last_error(ir);
    return status != PHY_OK ? status : PHY_ERR_OUT_OF_MEMORY;
}

static phy_status intern_head(phy_ir_context *ir, const char *name,
                              phy_ir_symbol *out_symbol)
{
    const phy_ir_symbol symbol = phy_ir_intern(ir, name);
    if (symbol == PHY_IR_NO_SYMBOL) {
        return ir_failure(ir);
    }
    *out_symbol = symbol;
    return PHY_OK;
}

phy_status phy_phi4_model_create(phy_cas *cas, const char *field_name,
                                 phy_ir_ref mass, phy_ir_ref coupling,
                                 unsigned spacetime_dimension,
                                 phy_phi4_model **out_model)
{
    if (cas == NULL || field_name == NULL || field_name[0] == '\0' ||
        mass == PHY_IR_NULL || coupling == PHY_IR_NULL ||
        out_model == NULL || spacetime_dimension == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (spacetime_dimension > 4u) {
        return PHY_ERR_UNSUPPORTED;
    }
    phy_phi4_model *model = phy_alloc(sizeof *model);
    if (model == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(model, 0, sizeof *model);
    model->cas = cas;
    model->mass = mass;
    model->coupling = coupling;
    model->dimension = spacetime_dimension;

    phy_ir_context *ir = phy_cas_ir(cas);
    phy_status status =
        intern_head(ir, field_name, &model->field_name);
    if (status == PHY_OK) {
        status = intern_head(ir, "ScalarField", &model->head_field);
    }
    if (status == PHY_OK) {
        status = intern_head(ir, "Partial", &model->head_partial);
    }
    if (status == PHY_OK) {
        status = intern_head(ir, "Box", &model->head_box);
    }
    if (status == PHY_OK) {
        status =
            intern_head(ir, "LorentzDot", &model->head_lorentz_dot);
    }
    if (status == PHY_OK) {
        status = intern_head(ir, "Propagator", &model->head_propagator);
    }
    if (status == PHY_OK) {
        status =
            intern_head(ir, "TadpoleIntegral", &model->head_tadpole);
    }
    if (status == PHY_OK) {
        status = intern_head(ir, "BubbleIntegral", &model->head_bubble);
    }
    if (status == PHY_OK) {
        status = intern_head(ir, "mu", &model->index_mu);
    }
    if (status == PHY_OK) {
        status = intern_head(ir, "Lorentz", &model->space_lorentz);
    }
    if (status != PHY_OK) {
        phy_free(model, sizeof *model);
        return status;
    }
    *out_model = model;
    return PHY_OK;
}

void phy_phi4_model_destroy(phy_phi4_model *model)
{
    if (model != NULL) {
        phy_free(model, sizeof *model);
    }
}

phy_cas *phy_phi4_model_cas(const phy_phi4_model *model)
{
    return model != NULL ? model->cas : NULL;
}

const char *phy_phi4_field_name(const phy_phi4_model *model)
{
    return model != NULL
               ? phy_ir_symbol_name(
                     phy_cas_ir(model->cas), model->field_name)
               : NULL;
}

unsigned phy_phi4_spacetime_dimension(const phy_phi4_model *model)
{
    return model != NULL ? model->dimension : 0u;
}

phy_ir_ref phy_phi4_mass(const phy_phi4_model *model)
{
    return model != NULL ? model->mass : PHY_IR_NULL;
}

phy_ir_ref phy_phi4_coupling(const phy_phi4_model *model)
{
    return model != NULL ? model->coupling : PHY_IR_NULL;
}

static phy_status model_field(const phy_phi4_model *model,
                              phy_ir_ref *out_field)
{
    phy_ir_context *ir = phy_cas_ir(model->cas);
    const phy_ir_ref name = phy_ir_symbol_ref(ir, model->field_name);
    if (name == PHY_IR_NULL) {
        return ir_failure(ir);
    }
    const phy_ir_ref field =
        phy_ir_operator(ir, model->head_field, &name, 1u);
    if (field == PHY_IR_NULL) {
        return ir_failure(ir);
    }
    *out_field = field;
    return PHY_OK;
}

phy_status phy_phi4_lagrangian(const phy_phi4_model *model,
                               phy_ir_ref *out_lagrangian)
{
    if (model == NULL || out_lagrangian == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_context *ir = phy_cas_ir(model->cas);
    phy_ir_ref field = PHY_IR_NULL;
    phy_status status = model_field(model, &field);
    const phy_ir_ref lower = phy_ir_index_in_space(
        ir, model->index_mu, PHY_IR_INDEX_LOWER, model->space_lorentz);
    const phy_ir_ref upper = phy_ir_index_in_space(
        ir, model->index_mu, PHY_IR_INDEX_UPPER, model->space_lorentz);
    if ((lower == PHY_IR_NULL || upper == PHY_IR_NULL) &&
        status == PHY_OK) {
        status = ir_failure(ir);
    }

    phy_ir_ref partial_lower = PHY_IR_NULL;
    phy_ir_ref partial_upper = PHY_IR_NULL;
    phy_ir_ref kinetic = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref args[2] = {field, lower};
        partial_lower =
            phy_ir_operator(ir, model->head_partial, args, 2u);
        if (partial_lower == PHY_IR_NULL) {
            status = ir_failure(ir);
        }
    }
    if (status == PHY_OK) {
        const phy_ir_ref args[2] = {field, upper};
        partial_upper =
            phy_ir_operator(ir, model->head_partial, args, 2u);
        if (partial_upper == PHY_IR_NULL) {
            status = ir_failure(ir);
        }
    }
    if (status == PHY_OK) {
        const phy_ir_ref args[2] = {partial_lower, partial_upper};
        kinetic =
            phy_ir_operator(ir, model->head_lorentz_dot, args, 2u);
        if (kinetic == PHY_IR_NULL) {
            status = ir_failure(ir);
        }
    }

    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref minus_half = PHY_IR_NULL;
    phy_ir_ref minus_twenty_fourth = PHY_IR_NULL;
    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref four = PHY_IR_NULL;
    phy_ir_ref field_squared = PHY_IR_NULL;
    phy_ir_ref field_fourth = PHY_IR_NULL;
    phy_ir_ref mass_squared = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 1, 2, &half);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, -1, 2, &minus_half);
    }
    if (status == PHY_OK) {
        status =
            phy_cas_number(model->cas, -1, 24, &minus_twenty_fourth);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 2, 1, &two);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 4, 1, &four);
    }
    if (status == PHY_OK) {
        status = phy_cas_pow(model->cas, field, two, &field_squared);
    }
    if (status == PHY_OK) {
        status = phy_cas_pow(model->cas, field, four, &field_fourth);
    }
    if (status == PHY_OK) {
        status =
            phy_cas_pow(model->cas, model->mass, two, &mass_squared);
    }

    phy_ir_ref kinetic_term = PHY_IR_NULL;
    phy_ir_ref mass_term = PHY_IR_NULL;
    phy_ir_ref interaction = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {half, kinetic};
        status = phy_cas_mul(
            model->cas, factors, 2u, &kinetic_term);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[3] = {
            minus_half, mass_squared, field_squared};
        status =
            phy_cas_mul(model->cas, factors, 3u, &mass_term);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[3] = {
            minus_twenty_fourth, model->coupling, field_fourth};
        status =
            phy_cas_mul(model->cas, factors, 3u, &interaction);
    }
    if (status == PHY_OK) {
        const phy_ir_ref terms[3] = {
            kinetic_term, mass_term, interaction};
        status =
            phy_cas_add(model->cas, terms, 3u, out_lagrangian);
    }
    return status;
}

phy_status phy_phi4_equation_of_motion(const phy_phi4_model *model,
                                       phy_ir_ref *out_lhs)
{
    if (model == NULL || out_lhs == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_context *ir = phy_cas_ir(model->cas);
    phy_ir_ref field = PHY_IR_NULL;
    phy_status status = model_field(model, &field);
    phy_ir_ref box = PHY_IR_NULL;
    if (status == PHY_OK) {
        box = phy_ir_operator(ir, model->head_box, &field, 1u);
        if (box == PHY_IR_NULL) {
            status = ir_failure(ir);
        }
    }

    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref three = PHY_IR_NULL;
    phy_ir_ref sixth = PHY_IR_NULL;
    phy_ir_ref mass_squared = PHY_IR_NULL;
    phy_ir_ref field_cubed = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 2, 1, &two);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 3, 1, &three);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 1, 6, &sixth);
    }
    if (status == PHY_OK) {
        status =
            phy_cas_pow(model->cas, model->mass, two, &mass_squared);
    }
    if (status == PHY_OK) {
        status = phy_cas_pow(model->cas, field, three, &field_cubed);
    }

    phy_ir_ref mass_term = PHY_IR_NULL;
    phy_ir_ref interaction = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {mass_squared, field};
        status = phy_cas_mul(model->cas, factors, 2u, &mass_term);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[3] = {
            sixth, model->coupling, field_cubed};
        status = phy_cas_mul(model->cas, factors, 3u, &interaction);
    }
    if (status == PHY_OK) {
        const phy_ir_ref terms[3] = {box, mass_term, interaction};
        status = phy_cas_add(model->cas, terms, 3u, out_lhs);
    }
    return status;
}

phy_status phy_phi4_inverse_propagator(const phy_phi4_model *model,
                                       phy_ir_ref momentum_squared,
                                       phy_ir_ref *out_expression)
{
    if (model == NULL || momentum_squared == PHY_IR_NULL ||
        out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref mass_squared = PHY_IR_NULL;
    phy_status status = phy_cas_number(model->cas, 2, 1, &two);
    if (status == PHY_OK) {
        status =
            phy_cas_pow(model->cas, model->mass, two, &mass_squared);
    }
    if (status == PHY_OK) {
        status = phy_cas_sub(
            model->cas, momentum_squared, mass_squared, out_expression);
    }
    return status;
}

phy_status phy_phi4_propagator(const phy_phi4_model *model,
                               phy_ir_ref momentum_squared,
                               phy_ir_ref *out_propagator)
{
    if (model == NULL || momentum_squared == PHY_IR_NULL ||
        out_propagator == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref dimension = PHY_IR_NULL;
    phy_status status = phy_cas_number(
        model->cas, (int64_t)model->dimension, 1, &dimension);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref args[3] = {
        momentum_squared, model->mass, dimension};
    const phy_ir_ref result = phy_ir_operator(
        phy_cas_ir(model->cas), model->head_propagator, args, 3u);
    if (result == PHY_IR_NULL) {
        return ir_failure(phy_cas_ir(model->cas));
    }
    *out_propagator = result;
    return PHY_OK;
}

phy_status phy_phi4_vertex_stripped_i(const phy_phi4_model *model,
                                      phy_ir_ref *out_vertex)
{
    if (model == NULL || out_vertex == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_cas_neg(model->cas, model->coupling, out_vertex);
}

phy_status phy_phi4_diagram_validate(const phy_phi4_model *model,
                                     const phy_phi4_diagram *diagram)
{
    if (model == NULL || diagram == NULL ||
        diagram->symmetry_factor == PHY_IR_NULL ||
        diagram->coupling_weight == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const uint64_t vertex_half_edges = 4ull * diagram->vertices;
    const uint64_t line_half_edges =
        2ull * diagram->internal_lines + diagram->external_legs;
    if (vertex_half_edges != line_half_edges) {
        return PHY_ERR_ASSUMPTION;
    }
    if (diagram->internal_lines + 1u < diagram->vertices ||
        diagram->loop_order !=
            diagram->internal_lines - diagram->vertices + 1u) {
        return PHY_ERR_ASSUMPTION;
    }
    const int64_t degree =
        (int64_t)model->dimension * diagram->loop_order -
        2ll * diagram->internal_lines;
    if (degree < INT_MIN || degree > INT_MAX) {
        return PHY_ERR_OVERFLOW;
    }
    if (diagram->superficial_degree != (int)degree) {
        return PHY_ERR_ASSUMPTION;
    }
    if ((diagram->loop_order == 0u) !=
        (diagram->loop_integral == PHY_IR_NULL)) {
        return PHY_ERR_ASSUMPTION;
    }
    return PHY_OK;
}

phy_status phy_phi4_diagram_expression(const phy_phi4_model *model,
                                       const phy_phi4_diagram *diagram,
                                       phy_ir_ref *out_expression)
{
    if (out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_status status = phy_phi4_diagram_validate(model, diagram);
    if (status != PHY_OK) {
        return status;
    }
    if (diagram->loop_integral == PHY_IR_NULL) {
        *out_expression = diagram->coupling_weight;
        return PHY_OK;
    }
    const phy_ir_ref factors[2] = {
        diagram->coupling_weight, diagram->loop_integral};
    return phy_cas_mul(model->cas, factors, 2u, out_expression);
}

phy_status phy_phi4_tree_2to2(const phy_phi4_model *model,
                              phy_phi4_diagram *out_diagram)
{
    if (model == NULL || out_diagram == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_phi4_diagram diagram;
    memset(&diagram, 0, sizeof diagram);
    diagram.kind = PHY_PHI4_DIAGRAM_TREE_4PT;
    diagram.vertices = 1u;
    diagram.external_legs = 4u;
    diagram.superficial_degree = 0;
    phy_status status =
        phy_cas_number(model->cas, 1, 1, &diagram.symmetry_factor);
    if (status == PHY_OK) {
        status = phy_phi4_vertex_stripped_i(
            model, &diagram.coupling_weight);
    }
    if (status == PHY_OK) {
        status = phy_phi4_diagram_validate(model, &diagram);
    }
    if (status == PHY_OK) {
        *out_diagram = diagram;
    }
    return status;
}

static phy_status master_integral(
    const phy_phi4_model *model, phy_ir_symbol head,
    phy_ir_ref invariant, phy_ir_ref *out_integral)
{
    phy_ir_ref dimension = PHY_IR_NULL;
    phy_status status = phy_cas_number(
        model->cas, (int64_t)model->dimension, 1, &dimension);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref args[3] = {model->mass, dimension, PHY_IR_NULL};
    size_t count = 2u;
    if (invariant != PHY_IR_NULL) {
        args[0] = invariant;
        args[1] = model->mass;
        args[2] = dimension;
        count = 3u;
    }
    const phy_ir_ref integral = phy_ir_operator(
        phy_cas_ir(model->cas), head, args, count);
    if (integral == PHY_IR_NULL) {
        return ir_failure(phy_cas_ir(model->cas));
    }
    *out_integral = integral;
    return PHY_OK;
}

static phy_status fill_diagram(
    const phy_phi4_model *model, phy_phi4_diagram_kind kind,
    unsigned external_legs, phy_ir_ref invariant,
    phy_phi4_diagram *out_diagram)
{
    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref coupling_power = model->coupling;
    phy_ir_ref two = PHY_IR_NULL;
    phy_status status = phy_cas_number(model->cas, 1, 2, &half);
    if (status == PHY_OK && kind != PHY_PHI4_DIAGRAM_TADPOLE_2PT) {
        status = phy_cas_number(model->cas, 2, 1, &two);
    }
    if (status == PHY_OK && kind != PHY_PHI4_DIAGRAM_TADPOLE_2PT) {
        status = phy_cas_pow(
            model->cas, model->coupling, two, &coupling_power);
    }
    phy_ir_ref weight = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {half, coupling_power};
        status = phy_cas_mul(model->cas, factors, 2u, &weight);
    }
    phy_ir_ref integral = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = master_integral(
            model,
            kind == PHY_PHI4_DIAGRAM_TADPOLE_2PT
                ? model->head_tadpole
                : model->head_bubble,
            invariant, &integral);
    }
    if (status == PHY_OK) {
        phy_phi4_diagram result;
        memset(&result, 0, sizeof result);
        result.kind = kind;
        result.vertices =
            kind == PHY_PHI4_DIAGRAM_TADPOLE_2PT ? 1u : 2u;
        result.internal_lines =
            kind == PHY_PHI4_DIAGRAM_TADPOLE_2PT ? 1u : 2u;
        result.loop_order = 1u;
        result.external_legs = external_legs;
        result.superficial_degree =
            (int)model->dimension -
            2 * (int)result.internal_lines;
        result.symmetry_factor = half;
        result.coupling_weight = weight;
        result.loop_integral = integral;
        status = phy_phi4_diagram_validate(model, &result);
        if (status == PHY_OK) {
            *out_diagram = result;
        }
    }
    return status;
}

phy_status phy_phi4_one_loop_diagrams(const phy_phi4_model *model,
                                      phy_ir_ref s, phy_ir_ref t,
                                      phy_ir_ref u,
                                      phy_phi4_diagram out_diagrams[4])
{
    if (model == NULL || s == PHY_IR_NULL || t == PHY_IR_NULL ||
        u == PHY_IR_NULL || out_diagrams == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_phi4_diagram built[4];
    phy_status status = fill_diagram(
        model, PHY_PHI4_DIAGRAM_TADPOLE_2PT, 2u, PHY_IR_NULL, &built[0]);
    if (status == PHY_OK) {
        status = fill_diagram(
            model, PHY_PHI4_DIAGRAM_BUBBLE_S, 4u, s, &built[1]);
    }
    if (status == PHY_OK) {
        status = fill_diagram(
            model, PHY_PHI4_DIAGRAM_BUBBLE_T, 4u, t, &built[2]);
    }
    if (status == PHY_OK) {
        status = fill_diagram(
            model, PHY_PHI4_DIAGRAM_BUBBLE_U, 4u, u, &built[3]);
    }
    if (status == PHY_OK) {
        memcpy(out_diagrams, built, sizeof built);
    }
    return status;
}

static phy_status one_loop_pole_factor(
    const phy_phi4_model *model, phy_ir_ref epsilon,
    phy_phi4_renorm_scheme scheme, phy_ir_ref *out_factor)
{
    if (model == NULL || epsilon == PHY_IR_NULL || out_factor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (model->dimension != 4u) {
        return PHY_ERR_UNSUPPORTED;
    }
    if (scheme != PHY_PHI4_RENORM_MS &&
        scheme != PHY_PHI4_RENORM_MSBAR) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    phy_ir_context *ir = phy_cas_ir(model->cas);
    phy_ir_ref one = PHY_IR_NULL;
    phy_ir_ref inverse_epsilon = PHY_IR_NULL;
    phy_status status = phy_cas_number(model->cas, 1, 1, &one);
    if (status == PHY_OK) {
        status =
            phy_cas_div(model->cas, one, epsilon, &inverse_epsilon);
    }
    if (status != PHY_OK || scheme == PHY_PHI4_RENORM_MS) {
        if (status == PHY_OK) {
            *out_factor = inverse_epsilon;
        }
        return status;
    }

    const phy_ir_symbol euler_symbol = phy_ir_intern(ir, "EulerGamma");
    const phy_ir_symbol pi_symbol = phy_ir_intern(ir, "Pi");
    const phy_ir_symbol log_symbol = phy_ir_intern(ir, "log");
    const phy_ir_ref euler = phy_ir_symbol_ref(ir, euler_symbol);
    const phy_ir_ref pi = phy_ir_symbol_ref(ir, pi_symbol);
    phy_ir_ref four = PHY_IR_NULL;
    if (euler == PHY_IR_NULL || pi == PHY_IR_NULL ||
        log_symbol == PHY_IR_NO_SYMBOL) {
        return ir_failure(ir);
    }
    status = phy_cas_number(model->cas, 4, 1, &four);
    phy_ir_ref four_pi = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {four, pi};
        status = phy_cas_mul(model->cas, factors, 2u, &four_pi);
    }
    phy_ir_ref logarithm = PHY_IR_NULL;
    if (status == PHY_OK) {
        logarithm = phy_ir_function(ir, log_symbol, &four_pi, 1u);
        if (logarithm == PHY_IR_NULL) {
            status = ir_failure(ir);
        }
    }
    phy_ir_ref without_euler = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_sub(
            model->cas, inverse_epsilon, euler, &without_euler);
    }
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {without_euler, logarithm};
        status = phy_cas_add(model->cas, terms, 2u, out_factor);
    }
    return status;
}

phy_status phy_phi4_one_loop_renormalization(
    const phy_phi4_model *model, phy_ir_ref epsilon,
    phy_phi4_renorm_scheme scheme,
    phy_phi4_renorm_constants *out_constants)
{
    if (model == NULL || epsilon == PHY_IR_NULL ||
        out_constants == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    phy_ir_ref pole = PHY_IR_NULL;
    phy_status status =
        one_loop_pole_factor(model, epsilon, scheme, &pole);
    phy_ir_ref pi = PHY_IR_NULL;
    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref thirty_two = PHY_IR_NULL;
    phy_ir_ref pi_squared = PHY_IR_NULL;
    phy_ir_ref denominator = PHY_IR_NULL;
    phy_ir_ref normalized_pole = PHY_IR_NULL;
    if (status == PHY_OK) {
        phy_ir_context *ir = phy_cas_ir(model->cas);
        pi = phy_ir_symbol_ref(ir, phy_ir_intern(ir, "Pi"));
        if (pi == PHY_IR_NULL) {
            status = ir_failure(ir);
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 2, 1, &two);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 32, 1, &thirty_two);
    }
    if (status == PHY_OK) {
        status = phy_cas_pow(model->cas, pi, two, &pi_squared);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {thirty_two, pi_squared};
        status =
            phy_cas_mul(model->cas, factors, 2u, &denominator);
    }
    if (status == PHY_OK) {
        status = phy_cas_div(
            model->cas, pole, denominator, &normalized_pole);
    }

    phy_phi4_renorm_constants result = {
        PHY_IR_NULL, PHY_IR_NULL, PHY_IR_NULL};
    if (status == PHY_OK) {
        status =
            phy_cas_number(model->cas, 0, 1, &result.delta_z_field);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {
            model->coupling, normalized_pole};
        status = phy_cas_mul(
            model->cas, factors, 2u, &result.delta_z_mass);
    }
    if (status == PHY_OK) {
        phy_ir_ref three = PHY_IR_NULL;
        status = phy_cas_number(model->cas, 3, 1, &three);
        if (status == PHY_OK) {
            const phy_ir_ref factors[3] = {
                three, model->coupling, normalized_pole};
            status = phy_cas_mul(
                model->cas, factors, 3u, &result.delta_z_coupling);
        }
    }
    if (status == PHY_OK) {
        *out_constants = result;
    }
    return status;
}

phy_status phy_phi4_one_loop_counterterm_lagrangian(
    const phy_phi4_model *model, phy_ir_ref epsilon,
    phy_phi4_renorm_scheme scheme, phy_ir_ref *out_lagrangian)
{
    if (model == NULL || epsilon == PHY_IR_NULL ||
        out_lagrangian == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_phi4_renorm_constants constants = {
        PHY_IR_NULL, PHY_IR_NULL, PHY_IR_NULL};
    phy_status status = phy_phi4_one_loop_renormalization(
        model, epsilon, scheme, &constants);
    phy_ir_ref field = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = model_field(model, &field);
    }

    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref four = PHY_IR_NULL;
    phy_ir_ref minus_half = PHY_IR_NULL;
    phy_ir_ref minus_twenty_fourth = PHY_IR_NULL;
    phy_ir_ref mass_squared = PHY_IR_NULL;
    phy_ir_ref field_squared = PHY_IR_NULL;
    phy_ir_ref field_fourth = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 2, 1, &two);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, 4, 1, &four);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(model->cas, -1, 2, &minus_half);
    }
    if (status == PHY_OK) {
        status = phy_cas_number(
            model->cas, -1, 24, &minus_twenty_fourth);
    }
    if (status == PHY_OK) {
        status =
            phy_cas_pow(model->cas, model->mass, two, &mass_squared);
    }
    if (status == PHY_OK) {
        status =
            phy_cas_pow(model->cas, field, two, &field_squared);
    }
    if (status == PHY_OK) {
        status =
            phy_cas_pow(model->cas, field, four, &field_fourth);
    }

    phy_ir_ref mass_counterterm = PHY_IR_NULL;
    phy_ir_ref coupling_counterterm = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref factors[4] = {
            minus_half, mass_squared, constants.delta_z_mass,
            field_squared};
        status = phy_cas_mul(
            model->cas, factors, 4u, &mass_counterterm);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[4] = {
            minus_twenty_fourth, model->coupling,
            constants.delta_z_coupling, field_fourth};
        status = phy_cas_mul(
            model->cas, factors, 4u, &coupling_counterterm);
    }
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {
            mass_counterterm, coupling_counterterm};
        status =
            phy_cas_add(model->cas, terms, 2u, out_lagrangian);
    }
    return status;
}
