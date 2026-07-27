/*
 * General-relativity curvature from a coordinate metric.
 *
 * Every formula here is written in docs/references/GENERAL_RELATIVITY.md.
 * Simplifying CAS constructors are used at each elementary operation, so
 * expression swell is bounded between stages instead of carried into Riemann.
 */
#include "phy/gr.h"

#include <string.h>

#include "phy/platform.h"

struct phy_gr_result {
    phy_tensor *inverse_metric;
    phy_tensor *christoffel;
    phy_tensor *riemann_mixed;
    phy_tensor *riemann_covariant;
    phy_tensor *ricci;
    phy_ir_ref scalar_curvature;
    phy_tensor *einstein;
    /* Filled by phy_gr_kretschmann(), which is not part of the pipeline. */
    phy_tensor *riemann_contravariant;
    phy_ir_ref kretschmann;
};

static void destroy_outputs(phy_gr_result *result)
{
    if (result == NULL) {
        return;
    }
    phy_tensor_destroy(result->riemann_contravariant);
    phy_tensor_destroy(result->einstein);
    phy_tensor_destroy(result->ricci);
    phy_tensor_destroy(result->riemann_covariant);
    phy_tensor_destroy(result->riemann_mixed);
    phy_tensor_destroy(result->christoffel);
    phy_tensor_destroy(result->inverse_metric);
}

void phy_gr_result_destroy(phy_gr_result *result)
{
    if (result == NULL) {
        return;
    }
    destroy_outputs(result);
    phy_free(result, sizeof *result);
}

static phy_status component(phy_cas *cas, const phy_tensor *tensor,
                            const unsigned *indices, phy_ir_ref *out)
{
    return phy_tensor_component_expression(cas, tensor, indices, out);
}

static phy_status add2(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                       phy_ir_ref *out)
{
    const phy_ir_ref terms[2] = {left, right};
    return phy_cas_add(cas, terms, 2u, out);
}

static phy_status sub2(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                       phy_ir_ref *out)
{
    return phy_cas_sub(cas, left, right, out);
}

static phy_status mul2(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                       phy_ir_ref *out)
{
    const phy_ir_ref factors[2] = {left, right};
    return phy_cas_mul(cas, factors, 2u, out);
}

/*
 * Every stage is put in normal form before the next one reads it. See
 * phy_tensor_normalize() in include/phy/tensor.h for why this is load-bearing
 * rather than tidy: without it the inverse of a diagonal metric carries n^2-n
 * off-diagonal expressions that reduce to zero but are not the zero handle,
 * and every component is a nest of quotients that the next contraction
 * re-derives. Both compound, and Reissner-Nordström does not complete.
 */
static phy_status fold(phy_cas *cas, phy_tensor *tensor)
{
    const phy_status status = phy_tensor_normalize(cas, tensor, NULL);
    /*
     * A completed tensor stores only IR refs; memo entries are accelerators,
     * not part of its value.  Releasing them between curvature stages keeps
     * the fixed CAS arena available to the next rank-4 contraction.
     */
    phy_cas_cache_clear(cas);
    return status;
}

/*
 * The same, for a scalar.  Keep denominator factors between GR stages: fully
 * expanding them is precisely what makes Schwarzschild invariants overflow
 * even though their exact factored form is small.
 */
static phy_status fold_scalar(phy_cas *cas, phy_ir_ref *value)
{
    phy_ir_ref normalized = PHY_IR_NULL;
    phy_status status = phy_cas_expand_factored(cas, *value, &normalized);
    if (status != PHY_OK) {
        return status;
    }
    *value = normalized;

    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    status = phy_cas_is_zero(cas, normalized, &decision);
    if (status != PHY_OK) {
        /*
         * The zero test is an optional canonicalization.  Its resource
         * failure cannot invalidate the exact expression already computed.
         */
        phy_cas_cache_clear(cas);
        return PHY_OK;
    }
    if (decision == PHY_CAS_ZERO) {
        status = phy_cas_number(cas, 0, 1, value);
    }
    phy_cas_cache_clear(cas);
    return status;
}

/*
 * A diagonal inverse metric turns the eight-index definition
 *
 *   R_abcd R_efgh g^ae g^bf g^cg g^dh
 *
 * into a four-index sum.  Computing the latter directly avoids constructing
 * and then multiplying a fully raised Riemann tensor whose individually
 * correct denominator powers obscure the cancellations in Schwarzschild-like
 * coordinates.
 */
static bool inverse_is_diagonal(phy_cas *cas, const phy_tensor *inverse)
{
    const unsigned dimension = phy_tensor_dimension(inverse);
    const phy_ir_ref zero = phy_tensor_zero(inverse);
    for (unsigned a = 0u; a < dimension; ++a) {
        for (unsigned b = 0u; b < dimension; ++b) {
            if (a == b) {
                continue;
            }
            const unsigned indices[2] = {a, b};
            phy_ir_ref value = PHY_IR_NULL;
            if (component(cas, inverse, indices, &value) != PHY_OK ||
                value != zero) {
                return false;
            }
        }
    }
    return true;
}

static phy_status kretschmann_diagonal(phy_cas *cas,
                                       const phy_gr_result *result,
                                       phy_ir_ref *out_scalar)
{
    const unsigned dimension =
        phy_tensor_dimension(result->riemann_covariant);
    phy_ir_ref diagonal[PHY_TENSOR_MAX_DIM];
    for (unsigned a = 0u; a < dimension; ++a) {
        const unsigned indices[2] = {a, a};
        phy_status status =
            component(cas, result->inverse_metric, indices, &diagonal[a]);
        if (status != PHY_OK) {
            return status;
        }
    }

    phy_ir_ref sum = PHY_IR_NULL;
    phy_status status = phy_cas_number(cas, 0, 1, &sum);
    const phy_ir_ref zero = phy_tensor_zero(result->riemann_covariant);
    for (unsigned a = 0u; a < dimension && status == PHY_OK; ++a) {
        for (unsigned b = 0u; b < dimension && status == PHY_OK; ++b) {
            for (unsigned c = 0u; c < dimension && status == PHY_OK; ++c) {
                for (unsigned d = 0u; d < dimension && status == PHY_OK;
                     ++d) {
                    const unsigned indices[4] = {a, b, c, d};
                    phy_ir_ref lower = PHY_IR_NULL;
                    status = component(cas, result->riemann_covariant,
                                       indices, &lower);
                    if (status != PHY_OK || lower == zero) {
                        continue;
                    }
                    const phy_ir_ref factors[6] = {
                        lower, lower, diagonal[a], diagonal[b],
                        diagonal[c], diagonal[d]};
                    phy_ir_ref term = PHY_IR_NULL;
                    status = phy_cas_mul(cas, factors, 6u, &term);
                    if (status == PHY_OK) {
                        status = add2(cas, sum, term, &sum);
                    }
                }
            }
        }
    }
    if (status != PHY_OK) {
        return status;
    }
    status = phy_cas_expand_factored(cas, sum, out_scalar);
    return status;
}

static phy_status christoffel(phy_cas *cas, const phy_tensor *metric,
                              const phy_tensor *inverse,
                              phy_tensor **out_christoffel)
{
    static const phy_ir_variance kValence[3] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_tensor *gamma = NULL;
    phy_status status =
        phy_tensor_create(phy_tensor_chart(metric), "Gamma", 3u, kValence,
                          &gamma);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_tensor_declare_slot_symmetry(
        gamma, 1u, 2u, PHY_IR_SYMMETRY_SYMMETRIC);

    const unsigned dimension = phy_tensor_dimension(metric);
    const phy_ir_ref half = phy_ir_rational(
        phy_chart_ir(phy_tensor_chart(metric)), 1, 2);
    if (half == PHY_IR_NULL && status == PHY_OK) {
        status = phy_ir_last_error(phy_chart_ir(phy_tensor_chart(metric)));
        if (status == PHY_OK) {
            status = PHY_ERR_OUT_OF_MEMORY;
        }
    }

    for (unsigned a = 0u; a < dimension && status == PHY_OK; ++a) {
        for (unsigned b = 0u; b < dimension && status == PHY_OK; ++b) {
            for (unsigned c = b; c < dimension && status == PHY_OK; ++c) {
                phy_ir_ref sum = PHY_IR_NULL;
                status = phy_cas_number(cas, 0, 1, &sum);
                for (unsigned d = 0u; d < dimension && status == PHY_OK; ++d) {
                    const unsigned dc[2] = {d, c};
                    const unsigned db[2] = {d, b};
                    const unsigned bc[2] = {b, c};
                    const unsigned ad[2] = {a, d};
                    phy_ir_ref gdc = PHY_IR_NULL;
                    phy_ir_ref gdb = PHY_IR_NULL;
                    phy_ir_ref gbc = PHY_IR_NULL;
                    phy_ir_ref gad = PHY_IR_NULL;
                    phy_ir_ref first = PHY_IR_NULL;
                    phy_ir_ref second = PHY_IR_NULL;
                    phy_ir_ref third = PHY_IR_NULL;
                    phy_ir_ref inside = PHY_IR_NULL;
                    phy_ir_ref term = PHY_IR_NULL;

                    status = component(cas, metric, dc, &gdc);
                    if (status == PHY_OK) {
                        status = component(cas, metric, db, &gdb);
                    }
                    if (status == PHY_OK) {
                        status = component(cas, metric, bc, &gbc);
                    }
                    if (status == PHY_OK) {
                        status = component(cas, inverse, ad, &gad);
                    }
                    if (status == PHY_OK) {
                        status = phy_cas_diff(
                            cas, gdc,
                            phy_chart_coordinate(phy_tensor_chart(metric), b),
                            &first);
                    }
                    if (status == PHY_OK) {
                        status = phy_cas_diff(
                            cas, gdb,
                            phy_chart_coordinate(phy_tensor_chart(metric), c),
                            &second);
                    }
                    if (status == PHY_OK) {
                        status = phy_cas_diff(
                            cas, gbc,
                            phy_chart_coordinate(phy_tensor_chart(metric), d),
                            &third);
                    }
                    if (status == PHY_OK) {
                        status = add2(cas, first, second, &inside);
                    }
                    if (status == PHY_OK) {
                        status = sub2(cas, inside, third, &inside);
                    }
                    if (status == PHY_OK) {
                        status = mul2(cas, gad, inside, &term);
                    }
                    if (status == PHY_OK) {
                        status = mul2(cas, half, term, &term);
                    }
                    if (status == PHY_OK) {
                        status = add2(cas, sum, term, &sum);
                    }
                }
                if (status == PHY_OK) {
                    const unsigned indices[3] = {a, b, c};
                    status = phy_tensor_set(gamma, indices, sum);
                }
            }
        }
    }

    if (status != PHY_OK) {
        phy_tensor_destroy(gamma);
        return status;
    }
    *out_christoffel = gamma;
    return PHY_OK;
}

static phy_status riemann_mixed(phy_cas *cas, const phy_tensor *gamma,
                                phy_tensor **out_riemann)
{
    static const phy_ir_variance kValence[4] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_tensor *riemann = NULL;
    phy_status status =
        phy_tensor_create(phy_tensor_chart(gamma), "RiemannMixed", 4u,
                          kValence, &riemann);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_tensor_declare_slot_symmetry(
        riemann, 2u, 3u, PHY_IR_SYMMETRY_ANTISYMMETRIC);
    const unsigned dimension = phy_tensor_dimension(gamma);

    for (unsigned a = 0u; a < dimension && status == PHY_OK; ++a) {
        for (unsigned b = 0u; b < dimension && status == PHY_OK; ++b) {
            for (unsigned c = 0u; c < dimension && status == PHY_OK; ++c) {
                for (unsigned d = c + 1u;
                     d < dimension && status == PHY_OK; ++d) {
                    const unsigned abd[3] = {a, b, d};
                    const unsigned abc[3] = {a, b, c};
                    phy_ir_ref gamma_abd = PHY_IR_NULL;
                    phy_ir_ref gamma_abc = PHY_IR_NULL;
                    phy_ir_ref dc = PHY_IR_NULL;
                    phy_ir_ref dd = PHY_IR_NULL;
                    phy_ir_ref sum = PHY_IR_NULL;
                    status = component(cas, gamma, abd, &gamma_abd);
                    if (status == PHY_OK) {
                        status = component(cas, gamma, abc, &gamma_abc);
                    }
                    if (status == PHY_OK) {
                        status = phy_cas_diff(
                            cas, gamma_abd,
                            phy_chart_coordinate(phy_tensor_chart(gamma), c),
                            &dc);
                    }
                    if (status == PHY_OK) {
                        status = phy_cas_diff(
                            cas, gamma_abc,
                            phy_chart_coordinate(phy_tensor_chart(gamma), d),
                            &dd);
                    }
                    if (status == PHY_OK) {
                        status = sub2(cas, dc, dd, &sum);
                    }

                    for (unsigned e = 0u;
                         e < dimension && status == PHY_OK; ++e) {
                        const unsigned ace[3] = {a, c, e};
                        const unsigned ebd[3] = {e, b, d};
                        const unsigned ade[3] = {a, d, e};
                        const unsigned ebc[3] = {e, b, c};
                        phy_ir_ref left_a = PHY_IR_NULL;
                        phy_ir_ref left_b = PHY_IR_NULL;
                        phy_ir_ref right_a = PHY_IR_NULL;
                        phy_ir_ref right_b = PHY_IR_NULL;
                        phy_ir_ref positive = PHY_IR_NULL;
                        phy_ir_ref negative = PHY_IR_NULL;
                        phy_ir_ref pair = PHY_IR_NULL;
                        status = component(cas, gamma, ace, &left_a);
                        if (status == PHY_OK) {
                            status = component(cas, gamma, ebd, &left_b);
                        }
                        if (status == PHY_OK) {
                            status = component(cas, gamma, ade, &right_a);
                        }
                        if (status == PHY_OK) {
                            status = component(cas, gamma, ebc, &right_b);
                        }
                        if (status == PHY_OK) {
                            status = mul2(cas, left_a, left_b, &positive);
                        }
                        if (status == PHY_OK) {
                            status = mul2(cas, right_a, right_b, &negative);
                        }
                        if (status == PHY_OK) {
                            status = sub2(cas, positive, negative, &pair);
                        }
                        if (status == PHY_OK) {
                            status = add2(cas, sum, pair, &sum);
                        }
                    }
                    if (status == PHY_OK) {
                        const unsigned indices[4] = {a, b, c, d};
                        status = phy_tensor_set(riemann, indices, sum);
                    }
                }
            }
        }
    }
    if (status != PHY_OK) {
        phy_tensor_destroy(riemann);
        return status;
    }
    *out_riemann = riemann;
    return PHY_OK;
}

static phy_status scalar_curvature(phy_cas *cas, const phy_tensor *inverse,
                                   const phy_tensor *ricci,
                                   phy_ir_ref *out_scalar)
{
    const unsigned dimension = phy_tensor_dimension(ricci);
    phy_ir_ref sum = PHY_IR_NULL;
    phy_status status = phy_cas_number(cas, 0, 1, &sum);
    for (unsigned b = 0u; b < dimension && status == PHY_OK; ++b) {
        for (unsigned d = 0u; d < dimension && status == PHY_OK; ++d) {
            const unsigned indices[2] = {b, d};
            phy_ir_ref inverse_value = PHY_IR_NULL;
            phy_ir_ref ricci_value = PHY_IR_NULL;
            phy_ir_ref term = PHY_IR_NULL;
            status = component(cas, inverse, indices, &inverse_value);
            if (status == PHY_OK) {
                status = component(cas, ricci, indices, &ricci_value);
            }
            if (status == PHY_OK) {
                status = mul2(cas, inverse_value, ricci_value, &term);
            }
            if (status == PHY_OK) {
                status = add2(cas, sum, term, &sum);
            }
        }
    }
    if (status == PHY_OK) {
        *out_scalar = sum;
    }
    return status;
}

static phy_status einstein_tensor(phy_cas *cas, const phy_tensor *metric,
                                  const phy_tensor *ricci,
                                  phy_ir_ref scalar,
                                  phy_tensor **out_einstein)
{
    static const phy_ir_variance kLower[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_tensor *einstein = NULL;
    phy_status status =
        phy_tensor_create(phy_tensor_chart(metric), "Einstein", 2u, kLower,
                          &einstein);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_tensor_declare_slot_symmetry(
        einstein, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC);
    const unsigned dimension = phy_tensor_dimension(metric);
    const phy_ir_ref half = phy_ir_rational(
        phy_chart_ir(phy_tensor_chart(metric)), 1, 2);
    if (half == PHY_IR_NULL && status == PHY_OK) {
        status = phy_ir_last_error(phy_chart_ir(phy_tensor_chart(metric)));
        if (status == PHY_OK) {
            status = PHY_ERR_OUT_OF_MEMORY;
        }
    }

    for (unsigned a = 0u; a < dimension && status == PHY_OK; ++a) {
        for (unsigned b = a; b < dimension && status == PHY_OK; ++b) {
            const unsigned indices[2] = {a, b};
            phy_ir_ref ricci_value = PHY_IR_NULL;
            phy_ir_ref metric_value = PHY_IR_NULL;
            phy_ir_ref trace_term = PHY_IR_NULL;
            phy_ir_ref value = PHY_IR_NULL;
            status = component(cas, ricci, indices, &ricci_value);
            if (status == PHY_OK) {
                status = component(cas, metric, indices, &metric_value);
            }
            if (status == PHY_OK) {
                const phy_ir_ref factors[3] = {
                    half, scalar, metric_value};
                status = phy_cas_mul(cas, factors, 3u, &trace_term);
            }
            if (status == PHY_OK) {
                status = sub2(cas, ricci_value, trace_term, &value);
            }
            if (status == PHY_OK) {
                status = phy_tensor_set(einstein, indices, value);
            }
        }
    }
    if (status != PHY_OK) {
        phy_tensor_destroy(einstein);
        return status;
    }
    *out_einstein = einstein;
    return PHY_OK;
}

phy_status phy_gr_compute(phy_cas *cas, const phy_tensor *metric,
                          phy_gr_result **out_result)
{
    if (cas == NULL || metric == NULL || out_result == NULL ||
        phy_cas_ir(cas) != phy_chart_ir(phy_tensor_chart(metric))) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_tensor_rank(metric) != 2u ||
        phy_tensor_valence(metric, 0u) != PHY_IR_INDEX_LOWER ||
        phy_tensor_valence(metric, 1u) != PHY_IR_INDEX_LOWER) {
        return PHY_ERR_TYPE;
    }

    phy_gr_result *result = phy_alloc(sizeof *result);
    if (result == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(result, 0, sizeof *result);

    phy_status status = phy_tensor_inverse_metric(
        cas, metric, "InverseMetric", &result->inverse_metric);
    if (status == PHY_OK) {
        status = fold(cas, result->inverse_metric);
    }
    if (status == PHY_OK) {
        status = christoffel(cas, metric, result->inverse_metric,
                             &result->christoffel);
    }
    if (status == PHY_OK) {
        status = fold(cas, result->christoffel);
    }
    if (status == PHY_OK) {
        status = riemann_mixed(cas, result->christoffel,
                               &result->riemann_mixed);
    }
    if (status == PHY_OK) {
        status = fold(cas, result->riemann_mixed);
    }
    if (status == PHY_OK) {
        status = phy_tensor_lower_slot(
            cas, result->riemann_mixed, 0u, metric, "Riemann",
            &result->riemann_covariant);
    }
    if (status == PHY_OK) {
        status = fold(cas, result->riemann_covariant);
    }
    if (status == PHY_OK) {
        status = phy_tensor_contract(
            cas, result->riemann_mixed, 0u, 2u, "Ricci",
            &result->ricci);
    }
    if (status == PHY_OK) {
        status = fold(cas, result->ricci);
    }
    if (status == PHY_OK) {
        status = scalar_curvature(
            cas, result->inverse_metric, result->ricci,
            &result->scalar_curvature);
    }
    if (status == PHY_OK) {
        status = fold_scalar(cas, &result->scalar_curvature);
    }
    if (status == PHY_OK) {
        status = einstein_tensor(
            cas, metric, result->ricci, result->scalar_curvature,
            &result->einstein);
    }
    if (status == PHY_OK) {
        status = fold(cas, result->einstein);
    }
    if (status != PHY_OK) {
        destroy_outputs(result);
        phy_free(result, sizeof *result);
        return status;
    }
    *out_result = result;
    return PHY_OK;
}

/*
 * K = R_abcd R^abcd.
 *
 * Three raises rather than four: the mixed tensor already carries the first
 * index up, so raising slots 1, 2 and 3 of R^a_bcd reaches R^abcd, and the
 * contraction pairs it against the covariant tensor the pipeline already
 * holds. The two intermediates are scratch and are released before returning.
 */
phy_status phy_gr_kretschmann(phy_cas *cas, phy_gr_result *result,
                              phy_ir_ref *out_scalar)
{
    if (cas == NULL || result == NULL || out_scalar == NULL ||
        phy_cas_ir(cas) !=
            phy_chart_ir(phy_tensor_chart(result->riemann_mixed))) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (result->riemann_contravariant != NULL) {
        *out_scalar = result->kretschmann;
        return PHY_OK;
    }

    phy_tensor *raised_one = NULL;
    phy_tensor *raised_two = NULL;
    phy_tensor *raised_three = NULL;
    phy_status status = phy_tensor_raise_slot(
        cas, result->riemann_mixed, 1u, result->inverse_metric,
        "RiemannRaised1", &raised_one);
    if (status == PHY_OK) {
        status = fold(cas, raised_one);
    }
    if (status == PHY_OK) {
        status = phy_tensor_raise_slot(cas, raised_one, 2u,
                                       result->inverse_metric,
                                       "RiemannRaised2", &raised_two);
    }
    if (status == PHY_OK) {
        status = fold(cas, raised_two);
    }
    if (status == PHY_OK) {
        status = phy_tensor_raise_slot(cas, raised_two, 3u,
                                       result->inverse_metric,
                                       "RiemannUpper", &raised_three);
    }
    if (status == PHY_OK) {
        status = fold(cas, raised_three);
    }

    phy_ir_ref sum = PHY_IR_NULL;
    const bool diagonal =
        status == PHY_OK &&
        inverse_is_diagonal(cas, result->inverse_metric);
    if (diagonal) {
        status = kretschmann_diagonal(cas, result, &sum);
    } else if (status == PHY_OK) {
        status = phy_cas_number(cas, 0, 1, &sum);
    }
    const unsigned dimension = phy_tensor_dimension(result->riemann_mixed);
    for (unsigned a = 0u;
         !diagonal && a < dimension && status == PHY_OK; ++a) {
        for (unsigned b = 0u; b < dimension && status == PHY_OK; ++b) {
            for (unsigned c = 0u; c < dimension && status == PHY_OK; ++c) {
                for (unsigned d = 0u;
                     d < dimension && status == PHY_OK; ++d) {
                    const unsigned indices[4] = {a, b, c, d};
                    phy_ir_ref lower = PHY_IR_NULL;
                    phy_ir_ref upper = PHY_IR_NULL;
                    phy_ir_ref term = PHY_IR_NULL;
                    status = component(cas, result->riemann_covariant,
                                       indices, &lower);
                    if (status == PHY_OK) {
                        status = component(cas, raised_three, indices,
                                           &upper);
                    }
                    if (status == PHY_OK) {
                        status = mul2(cas, lower, upper, &term);
                    }
                    if (status == PHY_OK) {
                        status = add2(cas, sum, term, &sum);
                    }
                }
            }
        }
    }

    if (status == PHY_OK) {
        status = fold_scalar(cas, &sum);
    }

    phy_tensor_destroy(raised_two);
    phy_tensor_destroy(raised_one);
    if (status != PHY_OK) {
        phy_tensor_destroy(raised_three);
        return status;
    }
    result->riemann_contravariant = raised_three;
    result->kretschmann = sum;
    *out_scalar = sum;
    return PHY_OK;
}

phy_status phy_gr_covariant_derivative(phy_cas *cas,
                                       const phy_gr_result *result,
                                       const phy_tensor *tensor,
                                       const char *name,
                                       phy_tensor **out_tensor)
{
    if (cas == NULL || result == NULL || tensor == NULL || name == NULL ||
        out_tensor == NULL ||
        phy_cas_ir(cas) != phy_chart_ir(phy_tensor_chart(tensor)) ||
        phy_tensor_chart(tensor) != phy_tensor_chart(result->christoffel)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const unsigned rank = phy_tensor_rank(tensor);
    if (rank + 1u > PHY_TENSOR_MAX_RANK) {
        return PHY_ERR_UNSUPPORTED;
    }

    phy_ir_variance valence[PHY_TENSOR_MAX_RANK];
    valence[0] = PHY_IR_INDEX_LOWER;
    for (unsigned slot = 0u; slot < rank; ++slot) {
        valence[slot + 1u] = phy_tensor_valence(tensor, slot);
    }
    phy_tensor *derivative = NULL;
    phy_status status = phy_tensor_create(phy_tensor_chart(tensor), name,
                                          rank + 1u, valence, &derivative);
    if (status != PHY_OK) {
        return status;
    }

    const phy_tensor *gamma = result->christoffel;
    const unsigned dimension = phy_tensor_dimension(tensor);
    const size_t count = phy_tensor_component_count(derivative);
    unsigned output[PHY_TENSOR_MAX_RANK] = {0u};
    unsigned base[PHY_TENSOR_MAX_RANK] = {0u};
    unsigned shifted[PHY_TENSOR_MAX_RANK] = {0u};

    for (size_t flat = 0u; flat < count && status == PHY_OK; ++flat) {
        status = phy_tensor_unflatten(derivative, flat, output);
        if (status != PHY_OK) {
            break;
        }
        const unsigned axis = output[0];
        for (unsigned slot = 0u; slot < rank; ++slot) {
            base[slot] = output[slot + 1u];
        }

        phy_ir_ref value = PHY_IR_NULL;
        phy_ir_ref sum = PHY_IR_NULL;
        status = component(cas, tensor, base, &value);
        if (status == PHY_OK) {
            status = phy_cas_diff(
                cas, value,
                phy_chart_coordinate(phy_tensor_chart(tensor), axis), &sum);
        }

        for (unsigned slot = 0u; slot < rank && status == PHY_OK; ++slot) {
            const bool upper =
                phy_tensor_valence(tensor, slot) == PHY_IR_INDEX_UPPER;
            for (unsigned index = 0u;
                 index < rank && status == PHY_OK; ++index) {
                shifted[index] = base[index];
            }
            for (unsigned e = 0u; e < dimension && status == PHY_OK; ++e) {
                /*
                 * Gamma^{base[slot]}_{axis,e} for an upper slot, and
                 * Gamma^{e}_{axis,base[slot]} for a lower one. The connection
                 * is symmetric in its two lower slots, so the order of the
                 * last two entries does not matter; it is written in the
                 * reference order anyway.
                 */
                const unsigned gamma_indices[3] = {
                    upper ? base[slot] : e, axis,
                    upper ? e : base[slot]};
                shifted[slot] = e;
                phy_ir_ref connection = PHY_IR_NULL;
                phy_ir_ref shifted_value = PHY_IR_NULL;
                phy_ir_ref term = PHY_IR_NULL;
                status = component(cas, gamma, gamma_indices, &connection);
                if (status == PHY_OK) {
                    status = component(cas, tensor, shifted, &shifted_value);
                }
                if (status == PHY_OK) {
                    status = mul2(cas, connection, shifted_value, &term);
                }
                if (status == PHY_OK) {
                    status = upper ? add2(cas, sum, term, &sum)
                                   : sub2(cas, sum, term, &sum);
                }
            }
        }
        if (status == PHY_OK) {
            status = phy_tensor_set(derivative, output, sum);
        }
    }

    if (status != PHY_OK) {
        phy_tensor_destroy(derivative);
        return status;
    }
    *out_tensor = derivative;
    return PHY_OK;
}

const phy_tensor *phy_gr_inverse_metric(const phy_gr_result *result)
{
    return result != NULL ? result->inverse_metric : NULL;
}

const phy_tensor *phy_gr_christoffel(const phy_gr_result *result)
{
    return result != NULL ? result->christoffel : NULL;
}

const phy_tensor *phy_gr_riemann_mixed(const phy_gr_result *result)
{
    return result != NULL ? result->riemann_mixed : NULL;
}

const phy_tensor *phy_gr_riemann_covariant(const phy_gr_result *result)
{
    return result != NULL ? result->riemann_covariant : NULL;
}

const phy_tensor *phy_gr_ricci(const phy_gr_result *result)
{
    return result != NULL ? result->ricci : NULL;
}

phy_ir_ref phy_gr_scalar_curvature(const phy_gr_result *result)
{
    return result != NULL ? result->scalar_curvature : PHY_IR_NULL;
}

const phy_tensor *phy_gr_einstein(const phy_gr_result *result)
{
    return result != NULL ? result->einstein : NULL;
}

const phy_tensor *phy_gr_riemann_contravariant(const phy_gr_result *result)
{
    return result != NULL ? result->riemann_contravariant : NULL;
}
