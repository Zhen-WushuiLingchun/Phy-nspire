#include "phy/yang_mills.h"

#include <string.h>

#include "phy/platform.h"

struct phy_lie_form {
    const phy_lie_algebra *algebra;
    phy_manifold *manifold;
    unsigned chart_index;
    unsigned degree;
    unsigned algebra_dimension;
    phy_form *components[PHY_LIE_MAX_DIM];
};

static void destroy_components(phy_lie_form *form)
{
    if (form == NULL) {
        return;
    }
    for (unsigned basis = 0u; basis < form->algebra_dimension; ++basis) {
        phy_form_destroy(form->components[basis]);
        form->components[basis] = NULL;
    }
}

static bool cas_matches(const phy_lie_form *form, const phy_cas *cas)
{
    return form != NULL && cas != NULL &&
           phy_lie_algebra_cas(form->algebra) == cas &&
           phy_manifold_ir(form->manifold) == phy_cas_ir(cas);
}

static bool compatible(const phy_lie_form *left,
                       const phy_lie_form *right)
{
    return left != NULL && right != NULL &&
           left->algebra == right->algebra &&
           left->manifold == right->manifold &&
           left->chart_index == right->chart_index;
}

phy_status phy_lie_form_create(const phy_lie_algebra *algebra,
                               phy_manifold *manifold,
                               unsigned chart_index, unsigned degree,
                               phy_lie_form **out_form)
{
    if (algebra == NULL || manifold == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_cas *cas = phy_lie_algebra_cas(algebra);
    if (cas == NULL ||
        phy_manifold_ir(manifold) != phy_cas_ir(cas)) {
        return PHY_ERR_TYPE;
    }
    const unsigned dimension = phy_lie_algebra_dimension(algebra);
    if (dimension == 0u || dimension > PHY_LIE_MAX_DIM) {
        return PHY_ERR_UNSUPPORTED;
    }
    if (chart_index >= phy_manifold_chart_count(manifold) ||
        degree > phy_manifold_dimension(manifold)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    phy_lie_form *form = phy_alloc(sizeof *form);
    if (form == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(form, 0, sizeof *form);
    form->algebra = algebra;
    form->manifold = manifold;
    form->chart_index = chart_index;
    form->degree = degree;
    form->algebra_dimension = dimension;

    phy_status status = PHY_OK;
    for (unsigned basis = 0u; basis < dimension; ++basis) {
        status = phy_form_create(manifold, chart_index, NULL, degree,
                                 &form->components[basis]);
        if (status != PHY_OK) {
            break;
        }
    }
    if (status != PHY_OK) {
        destroy_components(form);
        phy_free(form, sizeof *form);
        return status;
    }
    *out_form = form;
    return PHY_OK;
}

void phy_lie_form_destroy(phy_lie_form *form)
{
    if (form != NULL) {
        destroy_components(form);
        phy_free(form, sizeof *form);
    }
}

const phy_lie_algebra *phy_lie_form_algebra(const phy_lie_form *form)
{
    return form != NULL ? form->algebra : NULL;
}

const phy_manifold *phy_lie_form_manifold(const phy_lie_form *form)
{
    return form != NULL ? form->manifold : NULL;
}

unsigned phy_lie_form_chart_index(const phy_lie_form *form)
{
    return form != NULL ? form->chart_index : 0u;
}

unsigned phy_lie_form_degree(const phy_lie_form *form)
{
    return form != NULL ? form->degree : 0u;
}

unsigned phy_lie_form_algebra_dimension(const phy_lie_form *form)
{
    return form != NULL ? form->algebra_dimension : 0u;
}

const phy_form *phy_lie_form_component(const phy_lie_form *form,
                                       unsigned basis)
{
    return form != NULL && basis < form->algebra_dimension
               ? form->components[basis]
               : NULL;
}

phy_form *phy_lie_form_component_mut(phy_lie_form *form, unsigned basis)
{
    return form != NULL && basis < form->algebra_dimension
               ? form->components[basis]
               : NULL;
}

phy_status phy_lie_form_copy(const phy_lie_form *form,
                             phy_lie_form **out_form)
{
    if (form == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_lie_form *copy = NULL;
    phy_status status =
        phy_lie_form_create(form->algebra, form->manifold,
                            form->chart_index, form->degree, &copy);
    if (status != PHY_OK) {
        return status;
    }
    for (unsigned basis = 0u; basis < form->algebra_dimension; ++basis) {
        phy_form *component = NULL;
        status = phy_form_copy(form->components[basis], &component);
        if (status != PHY_OK) {
            phy_lie_form_destroy(copy);
            return status;
        }
        phy_form_destroy(copy->components[basis]);
        copy->components[basis] = component;
    }
    *out_form = copy;
    return PHY_OK;
}

phy_status phy_lie_form_add(phy_cas *cas, const phy_lie_form *left,
                            const phy_lie_form *right,
                            phy_lie_form **out_form)
{
    if (cas == NULL || left == NULL || right == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!cas_matches(left, cas) || !cas_matches(right, cas) ||
        !compatible(left, right) || left->degree != right->degree) {
        return PHY_ERR_TYPE;
    }

    phy_lie_form *sum = NULL;
    phy_status status =
        phy_lie_form_create(left->algebra, left->manifold,
                            left->chart_index, left->degree, &sum);
    if (status != PHY_OK) {
        return status;
    }
    for (unsigned basis = 0u; basis < left->algebra_dimension; ++basis) {
        phy_form *component = NULL;
        status = phy_form_add(cas, left->components[basis],
                              right->components[basis], &component);
        if (status != PHY_OK) {
            phy_lie_form_destroy(sum);
            return status;
        }
        phy_form_destroy(sum->components[basis]);
        sum->components[basis] = component;
    }
    *out_form = sum;
    return PHY_OK;
}

phy_status phy_lie_form_scale(phy_cas *cas, const phy_lie_form *form,
                              phy_ir_ref scalar,
                              phy_lie_form **out_form)
{
    if (cas == NULL || form == NULL || scalar == PHY_IR_NULL ||
        out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!cas_matches(form, cas)) {
        return PHY_ERR_TYPE;
    }

    phy_lie_form *scaled = NULL;
    phy_status status =
        phy_lie_form_create(form->algebra, form->manifold,
                            form->chart_index, form->degree, &scaled);
    if (status != PHY_OK) {
        return status;
    }
    for (unsigned basis = 0u; basis < form->algebra_dimension; ++basis) {
        phy_form *component = NULL;
        status = phy_form_scale(cas, form->components[basis], scalar,
                                &component);
        if (status != PHY_OK) {
            phy_lie_form_destroy(scaled);
            return status;
        }
        phy_form_destroy(scaled->components[basis]);
        scaled->components[basis] = component;
    }
    *out_form = scaled;
    return PHY_OK;
}

phy_status phy_lie_form_is_zero(phy_cas *cas, const phy_lie_form *form,
                                phy_cas_decision *out_decision)
{
    if (cas == NULL || form == NULL || out_decision == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!cas_matches(form, cas)) {
        return PHY_ERR_TYPE;
    }
    phy_cas_decision combined = PHY_CAS_ZERO;
    for (unsigned basis = 0u; basis < form->algebra_dimension; ++basis) {
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        const phy_status status =
            phy_form_is_zero(cas, form->components[basis], &decision);
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

phy_status phy_lie_form_equivalent(phy_cas *cas,
                                   const phy_lie_form *left,
                                   const phy_lie_form *right,
                                   phy_cas_decision *out_decision)
{
    if (cas == NULL || left == NULL || right == NULL ||
        out_decision == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!cas_matches(left, cas) || !cas_matches(right, cas) ||
        !compatible(left, right) || left->degree != right->degree) {
        return PHY_ERR_TYPE;
    }
    phy_cas_decision combined = PHY_CAS_ZERO;
    for (unsigned basis = 0u; basis < left->algebra_dimension; ++basis) {
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        const phy_status status = phy_form_equivalent(
            cas, left->components[basis], right->components[basis],
            &decision);
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

phy_status phy_lie_form_bracket_wedge(phy_cas *cas,
                                      const phy_lie_form *left,
                                      const phy_lie_form *right,
                                      phy_lie_form **out_form)
{
    if (cas == NULL || left == NULL || right == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!cas_matches(left, cas) || !cas_matches(right, cas) ||
        !compatible(left, right)) {
        return PHY_ERR_TYPE;
    }
    const unsigned manifold_dimension =
        phy_manifold_dimension(left->manifold);
    if (left->degree + right->degree > manifold_dimension) {
        return PHY_ERR_DOMAIN;
    }

    phy_lie_form *bracket = NULL;
    phy_status status =
        phy_lie_form_create(left->algebra, left->manifold,
                            left->chart_index,
                            left->degree + right->degree, &bracket);
    if (status != PHY_OK) {
        return status;
    }

    for (unsigned out = 0u; out < left->algebra_dimension; ++out) {
        for (unsigned a = 0u; a < left->algebra_dimension; ++a) {
            for (unsigned b = 0u; b < left->algebra_dimension; ++b) {
                const phy_ir_ref structure =
                    phy_lie_structure_constant(left->algebra, a, b, out);
                phy_cas_decision zero = PHY_CAS_UNKNOWN;
                status = phy_cas_is_zero(cas, structure, &zero);
                if (status != PHY_OK) {
                    goto fail;
                }
                if (zero == PHY_CAS_ZERO) {
                    continue;
                }

                phy_form *wedge = NULL;
                phy_form *term = NULL;
                phy_form *sum = NULL;
                status = phy_form_wedge(cas, left->components[a],
                                        right->components[b], &wedge);
                if (status == PHY_OK) {
                    status =
                        phy_form_scale(cas, wedge, structure, &term);
                }
                if (status == PHY_OK) {
                    status = phy_form_add(cas, bracket->components[out],
                                          term, &sum);
                }
                phy_form_destroy(wedge);
                phy_form_destroy(term);
                if (status != PHY_OK) {
                    phy_form_destroy(sum);
                    goto fail;
                }
                phy_form_destroy(bracket->components[out]);
                bracket->components[out] = sum;
            }
        }
    }
    *out_form = bracket;
    return PHY_OK;

fail:
    phy_lie_form_destroy(bracket);
    return status;
}

phy_status phy_lie_form_exterior_derivative(phy_cas *cas,
                                             const phy_lie_form *form,
                                             phy_lie_form **out_form)
{
    if (cas == NULL || form == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!cas_matches(form, cas)) {
        return PHY_ERR_TYPE;
    }
    if (form->degree >= phy_manifold_dimension(form->manifold)) {
        return PHY_ERR_DOMAIN;
    }

    phy_lie_form *derivative = NULL;
    phy_status status =
        phy_lie_form_create(form->algebra, form->manifold,
                            form->chart_index, form->degree + 1u,
                            &derivative);
    if (status != PHY_OK) {
        return status;
    }
    for (unsigned basis = 0u; basis < form->algebra_dimension; ++basis) {
        phy_form *component = NULL;
        status = phy_form_exterior_derivative(
            cas, form->components[basis], &component);
        if (status != PHY_OK) {
            phy_lie_form_destroy(derivative);
            return status;
        }
        phy_form_destroy(derivative->components[basis]);
        derivative->components[basis] = component;
    }
    *out_form = derivative;
    return PHY_OK;
}

static phy_status covariant_derivative_checked(
    phy_cas *cas, const phy_lie_form *connection, phy_ir_ref coupling,
    const phy_lie_form *form, phy_lie_form **out_form)
{
    phy_lie_form *derivative = NULL;
    phy_lie_form *bracket = NULL;
    phy_lie_form *interaction = NULL;
    phy_lie_form *result = NULL;

    phy_status status =
        phy_lie_form_exterior_derivative(cas, form, &derivative);
    if (status == PHY_OK) {
        status = phy_lie_form_bracket_wedge(
            cas, connection, form, &bracket);
    }
    if (status == PHY_OK) {
        status =
            phy_lie_form_scale(cas, bracket, coupling, &interaction);
    }
    if (status == PHY_OK) {
        status =
            phy_lie_form_add(cas, derivative, interaction, &result);
    }

    phy_lie_form_destroy(derivative);
    phy_lie_form_destroy(bracket);
    phy_lie_form_destroy(interaction);
    if (status != PHY_OK) {
        phy_lie_form_destroy(result);
        return status;
    }
    *out_form = result;
    return PHY_OK;
}

phy_status phy_yang_mills_covariant_derivative(
    phy_cas *cas, const phy_lie_form *connection, phy_ir_ref coupling,
    const phy_lie_form *form, phy_lie_form **out_form)
{
    if (cas == NULL || connection == NULL || form == NULL ||
        coupling == PHY_IR_NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!compatible(connection, form) ||
        !cas_matches(connection, cas) || !cas_matches(form, cas) ||
        connection->degree != 1u) {
        return PHY_ERR_TYPE;
    }
    return covariant_derivative_checked(
        cas, connection, coupling, form, out_form);
}

phy_status phy_yang_mills_field_strength(
    phy_cas *cas, const phy_lie_form *connection, phy_ir_ref coupling,
    phy_lie_form **out_curvature)
{
    if (cas == NULL || connection == NULL ||
        coupling == PHY_IR_NULL || out_curvature == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!cas_matches(connection, cas) || connection->degree != 1u) {
        return PHY_ERR_TYPE;
    }
    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref half_coupling = PHY_IR_NULL;
    phy_status status = phy_cas_number(cas, 1, 2, &half);
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {half, coupling};
        status = phy_cas_mul(cas, factors, 2u, &half_coupling);
    }

    phy_lie_form *derivative = NULL;
    phy_lie_form *bracket = NULL;
    phy_lie_form *nonlinear = NULL;
    phy_lie_form *curvature = NULL;
    if (status == PHY_OK) {
        status = phy_lie_form_exterior_derivative(
            cas, connection, &derivative);
    }
    if (status == PHY_OK) {
        status = phy_lie_form_bracket_wedge(
            cas, connection, connection, &bracket);
    }
    if (status == PHY_OK) {
        status = phy_lie_form_scale(
            cas, bracket, half_coupling, &nonlinear);
    }
    if (status == PHY_OK) {
        status = phy_lie_form_add(
            cas, derivative, nonlinear, &curvature);
    }
    phy_lie_form_destroy(derivative);
    phy_lie_form_destroy(bracket);
    phy_lie_form_destroy(nonlinear);
    if (status != PHY_OK) {
        phy_lie_form_destroy(curvature);
        return status;
    }
    *out_curvature = curvature;
    return PHY_OK;
}

phy_status phy_yang_mills_gauge_variation_connection(
    phy_cas *cas, const phy_lie_form *connection, phy_ir_ref coupling,
    const phy_lie_form *parameter, phy_lie_form **out_variation)
{
    if (parameter == NULL || phy_lie_form_degree(parameter) != 0u) {
        return parameter == NULL ? PHY_ERR_INVALID_ARGUMENT : PHY_ERR_TYPE;
    }
    return phy_yang_mills_covariant_derivative(
        cas, connection, coupling, parameter, out_variation);
}

phy_status phy_yang_mills_gauge_variation_curvature(
    phy_cas *cas, const phy_lie_form *curvature, phy_ir_ref coupling,
    const phy_lie_form *parameter, phy_lie_form **out_variation)
{
    if (cas == NULL || curvature == NULL || parameter == NULL ||
        coupling == PHY_IR_NULL || out_variation == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!compatible(curvature, parameter) ||
        !cas_matches(curvature, cas) || !cas_matches(parameter, cas) ||
        curvature->degree != 2u || parameter->degree != 0u) {
        return PHY_ERR_TYPE;
    }
    phy_lie_form *bracket = NULL;
    phy_lie_form *variation = NULL;
    phy_status status = phy_lie_form_bracket_wedge(
        cas, curvature, parameter, &bracket);
    if (status == PHY_OK) {
        status =
            phy_lie_form_scale(cas, bracket, coupling, &variation);
    }
    phy_lie_form_destroy(bracket);
    if (status != PHY_OK) {
        phy_lie_form_destroy(variation);
        return status;
    }
    *out_variation = variation;
    return PHY_OK;
}

phy_status phy_yang_mills_bianchi(
    phy_cas *cas, const phy_lie_form *connection, phy_ir_ref coupling,
    phy_lie_form **out_residual)
{
    if (cas == NULL || connection == NULL ||
        coupling == PHY_IR_NULL || out_residual == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_lie_form *curvature = NULL;
    phy_lie_form *residual = NULL;
    phy_status status = phy_yang_mills_field_strength(
        cas, connection, coupling, &curvature);
    if (status == PHY_OK) {
        status = phy_yang_mills_covariant_derivative(
            cas, connection, coupling, curvature, &residual);
    }
    phy_lie_form_destroy(curvature);
    if (status != PHY_OK) {
        phy_lie_form_destroy(residual);
        return status;
    }
    *out_residual = residual;
    return PHY_OK;
}

phy_status phy_yang_mills_lagrangian(
    phy_cas *cas, const phy_lie_form *curvature, const phy_tensor *metric,
    const phy_ir_ref *bilinear, phy_form **out_lagrangian)
{
    if (cas == NULL || curvature == NULL || metric == NULL ||
        out_lagrangian == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!cas_matches(curvature, cas) || curvature->degree != 2u) {
        return PHY_ERR_TYPE;
    }

    phy_form *sum = NULL;
    phy_status status = phy_form_create(
        curvature->manifold, curvature->chart_index, NULL,
        phy_manifold_dimension(curvature->manifold), &sum);
    if (status != PHY_OK) {
        return status;
    }

    for (unsigned a = 0u; a < curvature->algebra_dimension; ++a) {
        for (unsigned b = 0u; b < curvature->algebra_dimension; ++b) {
            phy_ir_ref inner = PHY_IR_NULL;
            if (bilinear != NULL) {
                inner =
                    bilinear[(size_t)a * curvature->algebra_dimension + b];
                if (inner == PHY_IR_NULL) {
                    status = PHY_ERR_INVALID_ARGUMENT;
                    goto fail;
                }
            } else {
                status = phy_lie_killing_component(
                    curvature->algebra, a, b, &inner);
                if (status != PHY_OK) {
                    goto fail;
                }
            }
            phy_cas_decision zero = PHY_CAS_UNKNOWN;
            status = phy_cas_is_zero(cas, inner, &zero);
            if (status != PHY_OK) {
                goto fail;
            }
            if (zero == PHY_CAS_ZERO) {
                continue;
            }

            phy_form *dual = NULL;
            phy_form *density = NULL;
            phy_form *term = NULL;
            phy_form *next = NULL;
            status = phy_form_hodge_metric(
                cas, curvature->components[b], metric, &dual);
            if (status == PHY_OK) {
                status = phy_form_wedge(
                    cas, curvature->components[a], dual, &density);
            }
            if (status == PHY_OK) {
                status = phy_form_scale(cas, density, inner, &term);
            }
            if (status == PHY_OK) {
                status = phy_form_add(cas, sum, term, &next);
            }
            phy_form_destroy(dual);
            phy_form_destroy(density);
            phy_form_destroy(term);
            if (status != PHY_OK) {
                phy_form_destroy(next);
                goto fail;
            }
            phy_form_destroy(sum);
            sum = next;
        }
    }

    phy_ir_ref minus_half = PHY_IR_NULL;
    phy_form *lagrangian = NULL;
    status = phy_cas_number(cas, -1, 2, &minus_half);
    if (status == PHY_OK) {
        status = phy_form_scale(cas, sum, minus_half, &lagrangian);
    }
    phy_form_destroy(sum);
    if (status != PHY_OK) {
        phy_form_destroy(lagrangian);
        return status;
    }
    *out_lagrangian = lagrangian;
    return PHY_OK;

fail:
    phy_form_destroy(sum);
    return status;
}
