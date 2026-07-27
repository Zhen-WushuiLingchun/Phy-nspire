/*
 * Bounded real scalar phi^4 field-theory objects.
 *
 * This layer creates exact typed-IR Lagrangian, propagator, vertex, and
 * one-loop master-integral objects. It deliberately separates unambiguous
 * combinatorial weights from convention-dependent factors of i and signs.
 */
#ifndef PHY_QFT_SCALAR_H
#define PHY_QFT_SCALAR_H

#include <stdint.h>

#include "phy/cas.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_phi4_model phy_phi4_model;

typedef enum {
    PHY_PHI4_DIAGRAM_TADPOLE_2PT = 0,
    PHY_PHI4_DIAGRAM_BUBBLE_S,
    PHY_PHI4_DIAGRAM_BUBBLE_T,
    PHY_PHI4_DIAGRAM_BUBBLE_U,
    PHY_PHI4_DIAGRAM_TREE_4PT
} phy_phi4_diagram_kind;

typedef struct {
    phy_phi4_diagram_kind kind;
    unsigned vertices;
    unsigned internal_lines;
    unsigned loop_order;
    unsigned external_legs;
    int superficial_degree;
    phy_ir_ref symmetry_factor;
    phy_ir_ref coupling_weight;
    /*
     * A typed formal master integral at loop order > 0. The tree graph uses
     * PHY_IR_NULL. This is not a numerical loop-integral result.
     */
    phy_ir_ref loop_integral;
} phy_phi4_diagram;

/*
 * One-loop multiplicative renormalization convention.
 *
 * D = 4 - 2 epsilon throughout. In MSbar the pole symbol is expanded
 * explicitly as 1/epsilon - EulerGamma + Log[4 Pi], so the result remains
 * ordinary typed IR and can be inspected or further simplified.
 */
typedef enum {
    PHY_PHI4_RENORM_MS = 0,
    PHY_PHI4_RENORM_MSBAR
} phy_phi4_renorm_scheme;

typedef struct {
    phy_ir_ref delta_z_field;    /* Z_phi - 1 */
    phy_ir_ref delta_z_mass;     /* Z_m - 1 */
    phy_ir_ref delta_z_coupling; /* Z_lambda - 1 */
} phy_phi4_renorm_constants;

/*
 * A bounded connected phi^4 multigraph with individually labelled external
 * legs. Off-diagonal entries count parallel internal lines; a diagonal entry
 * counts tadpole self-loops and therefore contributes twice to the degree of
 * its vertex. The adjacency matrix must be symmetric.
 *
 * Four vertices and eight external legs cover the first useful tree through
 * three-loop corpus while keeping the CX II analysis allocation-free.
 */
#define PHY_PHI4_GRAPH_MAX_VERTICES 4u
#define PHY_PHI4_GRAPH_MAX_EXTERNAL 8u

typedef struct {
    uint8_t vertices;
    uint8_t external_legs;
    uint8_t external_vertex[PHY_PHI4_GRAPH_MAX_EXTERNAL];
    uint8_t
        internal_edges[PHY_PHI4_GRAPH_MAX_VERTICES]
                      [PHY_PHI4_GRAPH_MAX_VERTICES];
} phy_phi4_graph;

typedef struct {
    unsigned internal_lines;
    unsigned loop_order;
    int superficial_degree;
    uint64_t wick_multiplicity;
    uint32_t vertex_automorphisms;
    uint32_t vertex_labelings;
    uint64_t symmetry_factor;
    phy_ir_ref symmetry_weight; /* exact 1 / symmetry_factor */
    /*
     * Exact lambda^V / symmetry_factor. Overall signs, factors of i, momentum
     * routing, and loop-integral normalization are deliberately not inferred.
     */
    phy_ir_ref coupling_weight;
} phy_phi4_graph_analysis;

/*
 * `mass` and `coupling` are exact scalar IR expressions in `cas`.
 * `spacetime_dimension` is 1..4 for the device-oriented first model.
 */
phy_status phy_phi4_model_create(phy_cas *cas, const char *field_name,
                                 phy_ir_ref mass, phy_ir_ref coupling,
                                 unsigned spacetime_dimension,
                                 phy_phi4_model **out_model);
void phy_phi4_model_destroy(phy_phi4_model *model);

phy_cas *phy_phi4_model_cas(const phy_phi4_model *model);
const char *phy_phi4_field_name(const phy_phi4_model *model);
unsigned phy_phi4_spacetime_dimension(const phy_phi4_model *model);
phy_ir_ref phy_phi4_mass(const phy_phi4_model *model);
phy_ir_ref phy_phi4_coupling(const phy_phi4_model *model);

/*
 * Minkowski density with mostly-plus contraction:
 * 1/2 d_mu(phi)d^mu(phi) - 1/2 m^2 phi^2 - lambda/4! phi^4.
 */
phy_status phy_phi4_lagrangian(const phy_phi4_model *model,
                               phy_ir_ref *out_lagrangian);

/*
 * Euler-Lagrange equation, returned as the exact left-hand side:
 *
 *   Box[phi] + m^2 phi + lambda/3! phi^3.
 *
 * The result is zero on shell. Box is deliberately a typed differential
 * operator; this layer does not invent a coordinate chart for the field.
 */
phy_status phy_phi4_equation_of_motion(const phy_phi4_model *model,
                                       phy_ir_ref *out_lhs);

/* p^2 - m^2 and an opaque Propagator[p^2,m,D] typed object. */
phy_status phy_phi4_inverse_propagator(const phy_phi4_model *model,
                                       phy_ir_ref momentum_squared,
                                       phy_ir_ref *out_expression);
phy_status phy_phi4_propagator(const phy_phi4_model *model,
                               phy_ir_ref momentum_squared,
                               phy_ir_ref *out_propagator);

/* The quartic vertex with its overall factor of i stripped: -lambda. */
phy_status phy_phi4_vertex_stripped_i(const phy_phi4_model *model,
                                      phy_ir_ref *out_vertex);

/*
 * Connected amputated tree-level 2 -> 2 graph. With labelled external legs its
 * symmetry factor is one, and its stripped-i amplitude is -lambda.
 */
phy_status phy_phi4_tree_2to2(const phy_phi4_model *model,
                              phy_phi4_diagram *out_diagram);

/*
 * Enumerate the bounded one-loop corpus:
 *   2-point tadpole (weight lambda/2),
 *   s/t/u 4-point bubbles (each lambda^2/2).
 *
 * `s`, `t`, and `u` are exact Mandelstam invariant expressions. Each loop
 * integral is retained as a typed master head, never guessed numerically.
 */
phy_status phy_phi4_one_loop_diagrams(const phy_phi4_model *model,
                                      phy_ir_ref s, phy_ir_ref t,
                                      phy_ir_ref u,
                                      phy_phi4_diagram out_diagrams[4]);

/*
 * Verify the phi^4 half-edge identity 4V = 2I + E and, for a connected graph,
 * L = I - V + 1. The stored superficial degree must equal D L - 2 I.
 */
phy_status phy_phi4_diagram_validate(const phy_phi4_model *model,
                                     const phy_phi4_diagram *diagram);

/* Exact amputated graph expression: weight at tree level, weight*master above. */
phy_status phy_phi4_diagram_expression(const phy_phi4_model *model,
                                       const phy_phi4_diagram *diagram,
                                       phy_ir_ref *out_expression);

/*
 * Exact one-loop MS/MSbar constants for real lambda phi^4 / 4! theory:
 *
 *   delta Z_phi    = 0
 *   delta Z_m      = lambda P / (32 Pi^2)
 *   delta Z_lambda = 3 lambda P / (32 Pi^2)
 *
 * where P = 1/epsilon in MS and
 * P = 1/epsilon - EulerGamma + Log[4 Pi] in MSbar.
 *
 * These are relative multiplicative constants in the convention used by the
 * pinned FeynCalc phi4 example, not additive delta(m^2) and delta(lambda).
 * Only a four-dimensional model is accepted.
 */
phy_status phy_phi4_one_loop_renormalization(
    const phy_phi4_model *model, phy_ir_ref epsilon,
    phy_phi4_renorm_scheme scheme,
    phy_phi4_renorm_constants *out_constants);

/*
 * One-loop counterterm density in the same convention:
 *
 *  -1/2 m^2 delta Z_m phi^2
 *  -lambda/4! delta Z_lambda phi^4
 *
 * delta Z_phi vanishes at this order. No loop integral is evaluated
 * numerically.
 */
phy_status phy_phi4_one_loop_counterterm_lagrangian(
    const phy_phi4_model *model, phy_ir_ref epsilon,
    phy_phi4_renorm_scheme scheme, phy_ir_ref *out_lagrangian);

/*
 * Validate and analyse a connected graph transactionally.
 *
 * Every vertex must have degree four:
 *
 *   external_i + 2 adjacency[i][i] + sum_(j != i) adjacency[i][j] = 4.
 *
 * For individually labelled external legs, the exact combinatorics are
 *
 *   D = product_i 2^m_ii m_ii! product_(i<j) m_ij!
 *   Wick = (4!)^V / D
 *   S = |Aut_vertices| D
 *   weight = lambda^V / S.
 *
 * A vertex automorphism must preserve the full multigraph and every external
 * leg attachment. Invalid topology returns PHY_ERR_ASSUMPTION; unsupported
 * bounds return PHY_ERR_UNSUPPORTED. `out_analysis` is untouched on failure.
 */
phy_status phy_phi4_graph_analyze(
    const phy_phi4_model *model, const phy_phi4_graph *graph,
    phy_phi4_graph_analysis *out_analysis);

#ifdef __cplusplus
}
#endif

#endif /* PHY_QFT_SCALAR_H */
