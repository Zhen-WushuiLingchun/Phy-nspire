#include <stdio.h>

#include "phy/platform.h"
#include "phy/qft_scalar.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_ir_ref mass;
    phy_ir_ref coupling;
    phy_phi4_model *model;
} fixture;

static fixture fixture_open(void)
{
    fixture value;
    value.ir = phy_ir_context_create(NULL);
    value.cas = phy_cas_create(value.ir, NULL);
    value.mass =
        phy_ir_symbol_ref(value.ir, phy_ir_intern(value.ir, "m"));
    value.coupling =
        phy_ir_symbol_ref(value.ir, phy_ir_intern(value.ir, "lambda"));
    value.model = NULL;
    PHY_CHECK(value.ir != NULL);
    PHY_CHECK(value.cas != NULL);
    PHY_CHECK_EQ_INT(
        phy_phi4_model_create(
            value.cas, "phi", value.mass, value.coupling, 4u,
            &value.model),
        PHY_OK);
    PHY_CHECK(value.model != NULL);
    return value;
}

static void fixture_close(fixture *value)
{
    phy_phi4_model_destroy(value->model);
    phy_cas_destroy(value->cas);
    phy_ir_context_destroy(value->ir);
}

static void expect_equivalent(phy_cas *cas, phy_ir_ref actual,
                              phy_ir_ref expected)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(cas, actual, expected, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static void test_model_and_lagrangian(void)
{
    fixture f = fixture_open();
    PHY_CHECK_EQ_STR(phy_phi4_field_name(f.model), "phi");
    PHY_CHECK_EQ_INT(phy_phi4_spacetime_dimension(f.model), 4);
    PHY_CHECK_EQ_INT(phy_phi4_mass(f.model), f.mass);
    PHY_CHECK_EQ_INT(phy_phi4_coupling(f.model), f.coupling);
    PHY_CHECK(phy_phi4_model_cas(f.model) == f.cas);

    phy_ir_ref lagrangian = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_phi4_lagrangian(f.model, &lagrangian), PHY_OK);
    PHY_CHECK(lagrangian != PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_kind_of(f.ir, lagrangian), PHY_IR_ADD);

    /* The opaque physics heads survive inside the scalar expression. */
    char text[1024];
    size_t required = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, lagrangian, text, sizeof text, &required),
        PHY_OK);
    PHY_CHECK(strstr(text, "ScalarField") != NULL);
    PHY_CHECK(strstr(text, "LorentzDot") != NULL);
    PHY_CHECK(strstr(text, "Partial") != NULL);
    fixture_close(&f);
}

static void test_free_rules(void)
{
    fixture f = fixture_open();
    const phy_ir_ref p2 =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "p2"));
    phy_ir_ref inverse = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_phi4_inverse_propagator(f.model, p2, &inverse), PHY_OK);
    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref m2 = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 2, 1, &two), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_pow(f.cas, f.mass, two, &m2), PHY_OK);
    phy_ir_ref expected_inverse = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f.cas, p2, m2, &expected_inverse), PHY_OK);
    expect_equivalent(f.cas, inverse, expected_inverse);

    phy_ir_ref propagator = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_phi4_propagator(f.model, p2, &propagator), PHY_OK);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, phy_ir_head(f.ir, propagator)),
        "Propagator");

    phy_ir_ref vertex = PHY_IR_NULL;
    phy_ir_ref expected_vertex = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_phi4_vertex_stripped_i(f.model, &vertex), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_neg(f.cas, f.coupling, &expected_vertex), PHY_OK);
    expect_equivalent(f.cas, vertex, expected_vertex);
    fixture_close(&f);
}

static void test_equation_of_motion_and_tree(void)
{
    fixture f = fixture_open();
    phy_ir_ref eom = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_phi4_equation_of_motion(f.model, &eom), PHY_OK);
    char text[1024];
    size_t required = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, eom, text, sizeof text, &required), PHY_OK);
    PHY_CHECK(strstr(text, "Box") != NULL);
    PHY_CHECK(strstr(text, "ScalarField") != NULL);
    PHY_CHECK(strstr(text, "(rat 1 6)") != NULL);

    phy_phi4_diagram tree;
    PHY_CHECK_EQ_INT(phy_phi4_tree_2to2(f.model, &tree), PHY_OK);
    PHY_CHECK_EQ_INT(tree.kind, PHY_PHI4_DIAGRAM_TREE_4PT);
    PHY_CHECK_EQ_INT(tree.vertices, 1);
    PHY_CHECK_EQ_INT(tree.internal_lines, 0);
    PHY_CHECK_EQ_INT(tree.external_legs, 4);
    PHY_CHECK_EQ_INT(tree.loop_order, 0);
    PHY_CHECK_EQ_INT(tree.superficial_degree, 0);
    PHY_CHECK_EQ_INT(tree.loop_integral, PHY_IR_NULL);
    PHY_CHECK_EQ_INT(
        phy_phi4_diagram_validate(f.model, &tree), PHY_OK);

    phy_ir_ref expected = PHY_IR_NULL;
    phy_ir_ref amplitude = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_neg(f.cas, f.coupling, &expected), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_phi4_diagram_expression(f.model, &tree, &amplitude), PHY_OK);
    expect_equivalent(f.cas, amplitude, expected);
    fixture_close(&f);
}

static void test_one_loop_diagram_corpus(void)
{
    fixture f = fixture_open();
    const phy_ir_ref s =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "s"));
    const phy_ir_ref t =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "t"));
    const phy_ir_ref u =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "u"));
    phy_phi4_diagram diagrams[4];
    PHY_CHECK_EQ_INT(
        phy_phi4_one_loop_diagrams(f.model, s, t, u, diagrams), PHY_OK);

    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref lambda2 = PHY_IR_NULL;
    phy_ir_ref tadpole_weight = PHY_IR_NULL;
    phy_ir_ref bubble_weight = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 1, 2, &half), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 2, 1, &two), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_pow(f.cas, f.coupling, two, &lambda2), PHY_OK);
    {
        const phy_ir_ref factors[2] = {half, f.coupling};
        PHY_CHECK_EQ_INT(
            phy_cas_mul(f.cas, factors, 2u, &tadpole_weight), PHY_OK);
    }
    {
        const phy_ir_ref factors[2] = {half, lambda2};
        PHY_CHECK_EQ_INT(
            phy_cas_mul(f.cas, factors, 2u, &bubble_weight), PHY_OK);
    }

    PHY_CHECK_EQ_INT(diagrams[0].kind, PHY_PHI4_DIAGRAM_TADPOLE_2PT);
    PHY_CHECK_EQ_INT(diagrams[0].vertices, 1);
    PHY_CHECK_EQ_INT(diagrams[0].internal_lines, 1);
    PHY_CHECK_EQ_INT(diagrams[0].loop_order, 1);
    PHY_CHECK_EQ_INT(diagrams[0].external_legs, 2);
    PHY_CHECK_EQ_INT(diagrams[0].superficial_degree, 2);
    PHY_CHECK_EQ_INT(
        phy_phi4_diagram_validate(f.model, &diagrams[0]), PHY_OK);
    expect_equivalent(
        f.cas, diagrams[0].coupling_weight, tadpole_weight);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(
            f.ir, phy_ir_head(f.ir, diagrams[0].loop_integral)),
        "TadpoleIntegral");

    static const phy_phi4_diagram_kind kinds[3] = {
        PHY_PHI4_DIAGRAM_BUBBLE_S,
        PHY_PHI4_DIAGRAM_BUBBLE_T,
        PHY_PHI4_DIAGRAM_BUBBLE_U};
    for (unsigned index = 1u; index < 4u; ++index) {
        PHY_CHECK_EQ_INT(diagrams[index].kind, kinds[index - 1u]);
        PHY_CHECK_EQ_INT(diagrams[index].vertices, 2);
        PHY_CHECK_EQ_INT(diagrams[index].internal_lines, 2);
        PHY_CHECK_EQ_INT(diagrams[index].loop_order, 1);
        PHY_CHECK_EQ_INT(diagrams[index].external_legs, 4);
        PHY_CHECK_EQ_INT(diagrams[index].superficial_degree, 0);
        PHY_CHECK_EQ_INT(
            phy_phi4_diagram_validate(f.model, &diagrams[index]), PHY_OK);
        expect_equivalent(
            f.cas, diagrams[index].symmetry_factor, half);
        expect_equivalent(
            f.cas, diagrams[index].coupling_weight, bubble_weight);
        PHY_CHECK_EQ_STR(
            phy_ir_symbol_name(
                f.ir, phy_ir_head(f.ir, diagrams[index].loop_integral)),
            "BubbleIntegral");
        phy_ir_ref expression = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(
            phy_phi4_diagram_expression(
                f.model, &diagrams[index], &expression),
            PHY_OK);
        PHY_CHECK(expression != PHY_IR_NULL);
    }
    fixture_close(&f);
}

static void test_invalid_graphs_are_rejected(void)
{
    fixture f = fixture_open();
    phy_phi4_diagram tree;
    PHY_CHECK_EQ_INT(phy_phi4_tree_2to2(f.model, &tree), PHY_OK);
    tree.external_legs = 2u;
    PHY_CHECK_EQ_INT(
        phy_phi4_diagram_validate(f.model, &tree),
        PHY_ERR_ASSUMPTION);
    tree.external_legs = 4u;
    tree.loop_order = 1u;
    PHY_CHECK_EQ_INT(
        phy_phi4_diagram_validate(f.model, &tree),
        PHY_ERR_ASSUMPTION);
    fixture_close(&f);
}

static void test_one_loop_ms_renormalization(void)
{
    fixture f = fixture_open();
    const phy_ir_ref epsilon =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "epsilon"));
    phy_phi4_renorm_constants constants;
    PHY_CHECK_EQ_INT(
        phy_phi4_one_loop_renormalization(
            f.model, epsilon, PHY_PHI4_RENORM_MS, &constants),
        PHY_OK);
    PHY_CHECK_EQ_INT(constants.delta_z_field, phy_ir_integer(f.ir, 0));

    phy_ir_ref three = PHY_IR_NULL;
    phy_ir_ref expected_coupling = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 3, 1, &three), PHY_OK);
    {
        const phy_ir_ref factors[2] = {
            three, constants.delta_z_mass};
        PHY_CHECK_EQ_INT(
            phy_cas_mul(
                f.cas, factors, 2u, &expected_coupling),
            PHY_OK);
    }
    expect_equivalent(
        f.cas, constants.delta_z_coupling, expected_coupling);

    const phy_ir_ref pi =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "Pi"));
    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref thirty_two = PHY_IR_NULL;
    phy_ir_ref pi_squared = PHY_IR_NULL;
    phy_ir_ref recovered_coupling = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 2, 1, &two), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_number(f.cas, 32, 1, &thirty_two), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_pow(f.cas, pi, two, &pi_squared), PHY_OK);
    {
        const phy_ir_ref factors[4] = {
            thirty_two, pi_squared, epsilon,
            constants.delta_z_mass};
        PHY_CHECK_EQ_INT(
            phy_cas_mul(
                f.cas, factors, 4u, &recovered_coupling),
            PHY_OK);
    }
    expect_equivalent(f.cas, recovered_coupling, f.coupling);

    phy_ir_ref counterterm = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_phi4_one_loop_counterterm_lagrangian(
            f.model, epsilon, PHY_PHI4_RENORM_MS, &counterterm),
        PHY_OK);
    char text[2048];
    size_t required = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(
            f.ir, counterterm, text, sizeof text, &required),
        PHY_OK);
    PHY_CHECK(strstr(text, "ScalarField") != NULL);
    PHY_CHECK(strstr(text, "Pi") != NULL);
    PHY_CHECK(strstr(text, "epsilon") != NULL);
    fixture_close(&f);
}

static void test_one_loop_msbar_and_typed_rejections(void)
{
    fixture f = fixture_open();
    const phy_ir_ref epsilon =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "epsilon"));
    phy_phi4_renorm_constants constants;
    PHY_CHECK_EQ_INT(
        phy_phi4_one_loop_renormalization(
            f.model, epsilon, PHY_PHI4_RENORM_MSBAR, &constants),
        PHY_OK);
    char text[2048];
    size_t required = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(
            f.ir, constants.delta_z_mass, text, sizeof text, &required),
        PHY_OK);
    PHY_CHECK(strstr(text, "EulerGamma") != NULL);
    PHY_CHECK(strstr(text, "(fn log") != NULL);
    PHY_CHECK(strstr(text, "Pi") != NULL);

    const phy_ir_ref zero = phy_ir_integer(f.ir, 0);
    PHY_CHECK_EQ_INT(
        phy_phi4_one_loop_renormalization(
            f.model, zero, PHY_PHI4_RENORM_MS, &constants),
        PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(
        phy_phi4_one_loop_renormalization(
            f.model, epsilon, (phy_phi4_renorm_scheme)99, &constants),
        PHY_ERR_INVALID_ARGUMENT);
    fixture_close(&f);

    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);
    const phy_ir_ref m =
        phy_ir_symbol_ref(ir, phy_ir_intern(ir, "m"));
    const phy_ir_ref coupling =
        phy_ir_symbol_ref(ir, phy_ir_intern(ir, "lambda"));
    const phy_ir_ref eps =
        phy_ir_symbol_ref(ir, phy_ir_intern(ir, "epsilon"));
    phy_phi4_model *three_dimensional = NULL;
    PHY_CHECK_EQ_INT(
        phy_phi4_model_create(
            cas, "phi", m, coupling, 3u, &three_dimensional),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_phi4_one_loop_renormalization(
            three_dimensional, eps, PHY_PHI4_RENORM_MS, &constants),
        PHY_ERR_UNSUPPORTED);
    phy_phi4_model_destroy(three_dimensional);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

static void test_rejections_are_typed(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);
    const phy_ir_ref m =
        phy_ir_symbol_ref(ir, phy_ir_intern(ir, "m"));
    const phy_ir_ref coupling =
        phy_ir_symbol_ref(ir, phy_ir_intern(ir, "g"));
    phy_phi4_model *model = NULL;
    PHY_CHECK_EQ_INT(
        phy_phi4_model_create(cas, "phi", m, coupling, 5u, &model),
        PHY_ERR_UNSUPPORTED);
    PHY_CHECK(model == NULL);
    PHY_CHECK_EQ_INT(
        phy_phi4_model_create(
            cas, "", m, coupling, 4u, &model),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(model == NULL);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }
    PHY_TEST_CASE(test_model_and_lagrangian);
    PHY_TEST_CASE(test_free_rules);
    PHY_TEST_CASE(test_equation_of_motion_and_tree);
    PHY_TEST_CASE(test_one_loop_diagram_corpus);
    PHY_TEST_CASE(test_invalid_graphs_are_rejected);
    PHY_TEST_CASE(test_one_loop_ms_renormalization);
    PHY_TEST_CASE(test_one_loop_msbar_and_typed_rejections);
    PHY_TEST_CASE(test_rejections_are_typed);
    const int result = PHY_TEST_REPORT("test_qft_scalar");
    phy_platform_shutdown();
    return result;
}
