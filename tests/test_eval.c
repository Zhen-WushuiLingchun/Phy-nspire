/*
 * The stateful notebook evaluator.
 *
 * The point of these checks is not that a head survives a round trip -- that is
 * what the layer replaced. Each physics case reproduces, through reader-facing
 * source, a result the corresponding backend suite already certifies directly:
 * the U(1) and SU(2) curvature components of tests/test_yang_mills.c, the
 * two-sphere curvature of tests/test_gr.c, the exterior-calculus identities of
 * tests/test_geom.c. If the evaluator merely preserved operator heads, none of
 * them would hold.
 */
#include <stddef.h>
#include <string.h>

#include "phy/eval.h"
#include "phy/notebook.h"
#include "phy/platform.h"
#include "phy/source.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_env *env;
} fixture;

static fixture fixture_open(void)
{
    fixture f;
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    f.env = phy_env_create(f.cas);
    PHY_CHECK(f.env != NULL);
    return f;
}

static void fixture_close(fixture *f)
{
    PHY_CHECK_EQ_INT(phy_env_validate(f->env), PHY_OK);
    phy_env_destroy(f->env);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static phy_status run_status(fixture *f, const char *source,
                             phy_value *out_value)
{
    phy_source_command command;
    size_t offset = 0u;
    const phy_status parsed =
        phy_source_parse(f->ir, source, &command, &offset);
    if (parsed != PHY_OK) {
        return parsed;
    }
    const phy_status status = phy_eval_command(f->env, &command, out_value);
    PHY_CHECK_EQ_INT(phy_env_validate(f->env), PHY_OK);
    return status;
}

static phy_value run(fixture *f, const char *source)
{
    phy_value value;
    value.kind = PHY_VALUE_NONE;
    value.as.scalar = PHY_IR_NULL;
    const phy_status status = run_status(f, source, &value);
    if (status != PHY_OK) {
        fprintf(stderr, "  source: %s\n", source);
    }
    PHY_CHECK_EQ_INT(status, PHY_OK);
    return value;
}

static void expect_status(fixture *f, const char *source, phy_status expected)
{
    phy_value value;
    value.kind = PHY_VALUE_NONE;
    value.as.scalar = PHY_IR_NULL;
    const phy_status status = run_status(f, source, &value);
    if (status != expected) {
        fprintf(stderr, "  source: %s\n", source);
    }
    PHY_CHECK_EQ_INT(status, expected);
}

/* `source` must evaluate to a scalar equal, as a rational function, to `ir`. */
static void expect_scalar(fixture *f, const char *source, const char *ir)
{
    const phy_value value = run(f, source);
    PHY_CHECK_EQ_INT(value.kind, PHY_VALUE_SCALAR);
    if (value.kind != PHY_VALUE_SCALAR) {
        return;
    }
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_ir_read(f->ir, ir, &expected, NULL), PHY_OK);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(f->cas, value.as.scalar, expected, &decision),
        PHY_OK);
    if (decision != PHY_CAS_ZERO) {
        char text[256];
        size_t length = 0u;
        (void)phy_ir_write(f->ir, value.as.scalar, text, sizeof text, &length);
        fprintf(stderr, "  %s -> %s, expected %s\n", source, text, ir);
    }
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

/* A tri-state query result: the symbol True, False, or Unknown. */
static void expect_decision(fixture *f, const char *source,
                            const char *expected)
{
    const phy_value value = run(f, source);
    PHY_CHECK_EQ_INT(value.kind, PHY_VALUE_SCALAR);
    if (value.kind != PHY_VALUE_SCALAR) {
        return;
    }
    const char *name =
        phy_ir_symbol_name(f->ir, phy_ir_head(f->ir, value.as.scalar));
    if (name == NULL || strcmp(name, expected) != 0) {
        fprintf(stderr, "  %s -> %s, expected %s\n", source,
                name != NULL ? name : "(null)", expected);
    }
    PHY_CHECK(name != NULL && strcmp(name, expected) == 0);
}

static const char *describe(fixture *f, phy_value value)
{
    static char text[PHY_EVAL_DESCRIPTION_CAPACITY];
    PHY_CHECK_EQ_INT(phy_eval_describe(f->env, value, text, sizeof text),
                     PHY_OK);
    return text;
}

static const char *expansion(fixture *f, phy_value value)
{
    static char text[512];
    phy_ir_ref ref = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_eval_value_expression(f->env, value, &ref), PHY_OK);
    size_t length = 0u;
    PHY_CHECK_EQ_INT(phy_ir_write(f->ir, ref, text, sizeof text, &length),
                     PHY_OK);
    return text;
}

/* -------------------------------------------------------------- bindings */

static void test_scalar_state_flows_between_cells(void)
{
    fixture f = fixture_open();

    PHY_CHECK_EQ_INT(phy_env_binding_count(f.env), 0u);
    expect_scalar(&f, "a = 3", "3");
    PHY_CHECK_EQ_INT(phy_env_binding_count(f.env), 1u);

    /* The whole point: a later cell sees what an earlier one bound. */
    expect_scalar(&f, "b = a^2 + 1", "10");
    expect_scalar(&f, "a + b", "13");
    expect_scalar(&f, "Set[c, b - a]", "7");
    expect_scalar(&f, "Expand[(a + t)^2]", "(+ 9 (* 6 t) (^ t 2))");

    /* Rebinding replaces in place and keeps the binding's position. */
    expect_scalar(&f, "a = 5", "5");
    PHY_CHECK_EQ_INT(phy_env_binding_count(f.env), 3u);
    expect_scalar(&f, "a + b", "15");

    const char *name = NULL;
    phy_value bound;
    PHY_CHECK(phy_env_binding(f.env, 0u, &name, &bound));
    PHY_CHECK_EQ_STR(name, "a");
    PHY_CHECK_EQ_INT(bound.kind, PHY_VALUE_SCALAR);
    PHY_CHECK(phy_env_lookup(f.env, "b", &bound));
    PHY_CHECK(!phy_env_lookup(f.env, "zzz", &bound));

    /* An unbound name stays a free symbol rather than becoming an error. */
    expect_scalar(&f, "D[q^3, q]", "(* 3 (^ q 2))");
    fixture_close(&f);
}

static void test_clear_and_reset(void)
{
    fixture f = fixture_open();
    expect_scalar(&f, "a = 3", "3");
    expect_scalar(&f, "b = 4", "4");

    phy_value value;
    PHY_CHECK_EQ_INT(run_status(&f, "Clear[a]", &value), PHY_OK);
    PHY_CHECK_EQ_INT(value.kind, PHY_VALUE_NONE);
    PHY_CHECK_EQ_INT(phy_env_binding_count(f.env), 1u);
    expect_scalar(&f, "a", "a"); /* free again */
    expect_scalar(&f, "b", "4");

    PHY_CHECK_EQ_INT(run_status(&f, "ClearAll[]", &value), PHY_OK);
    PHY_CHECK_EQ_INT(phy_env_binding_count(f.env), 0u);
    PHY_CHECK_EQ_INT(phy_env_object_count(f.env), 0u);
    fixture_close(&f);
}

static void test_binding_rejects_reserved_and_captured_names(void)
{
    fixture f = fixture_open();

    /* Reserved spellings are not bindable; rejected by the parser. */
    expect_status(&f, "Sin = 2", PHY_ERR_TYPE);
    expect_status(&f, "Manifold = 2", PHY_ERR_TYPE);
    expect_status(&f, "Simplify = 2", PHY_ERR_TYPE);
    expect_status(&f, "Clear[Sin]", PHY_ERR_TYPE);

    /*
     * Coordinate capture, in both directions. Without this rule `x = 2` would
     * silently rewrite every component of every form on a chart with an x axis.
     */
    (void)run(&f, "M = Manifold[{x, y}, Euclidean]");
    expect_status(&f, "x = 2", PHY_ERR_ASSUMPTION);
    expect_scalar(&f, "u = 2", "2");
    expect_status(&f, "N = Manifold[{u, w}, Euclidean]", PHY_ERR_ASSUMPTION);

    /* `==` remains an equation and is not confused with assignment. */
    expect_scalar(&f, "p == p", "(= p p)");
    fixture_close(&f);
}

/* ------------------------------------------------------------- geometry */

static void test_manifolds_and_forms(void)
{
    fixture f = fixture_open();

    phy_value manifold = run(&f, "M = Manifold[{x, y}, Euclidean]");
    PHY_CHECK_EQ_INT(manifold.kind, PHY_VALUE_MANIFOLD);
    PHY_CHECK_EQ_STR(describe(&f, manifold),
                     "Manifold M dim 2 Riemannian +oriented (x,y)");

    phy_value spacetime =
        run(&f, "S = Manifold[{t, r, a, b}, Lorentzian, Negative]");
    PHY_CHECK_EQ_STR(describe(&f, spacetime),
                     "Manifold S dim 4 Lorentzian -oriented (t,r,a,b)");
    expect_scalar(&f, "Dimension[S]", "4");

    /* Explicit signature lists are accepted where a keyword will not do. */
    (void)run(&f, "U = Manifold[{p, q}, {1, -1}, Unoriented]");
    expect_status(&f, "Volume[U]", PHY_ERR_ASSUMPTION);

    /* A form is components in the coordinate coframe, and displays as one. */
    phy_value alpha = run(&f, "alpha = DifferentialForm[M, 1, {y, 0}]");
    PHY_CHECK_EQ_INT(alpha.kind, PHY_VALUE_FORM);
    PHY_CHECK_EQ_STR(describe(&f, alpha), "Form degree 1 on M dim 2");
    PHY_CHECK_EQ_STR(expansion(&f, alpha), "(* dx y)");
    expect_scalar(&f, "Degree[alpha]", "1");
    expect_scalar(&f, "Component[alpha, 0]", "y");
    expect_scalar(&f, "Component[alpha, 1]", "0");

    /* d(y dx) = dy ^ dx = -dx ^ dy. */
    phy_value dalpha = run(&f, "ExteriorD[alpha]");
    PHY_CHECK_EQ_STR(expansion(&f, dalpha), "(* -1 (wedge dx dy))");
    expect_scalar(&f, "Component[ExteriorD[alpha], 0, 1]", "-1");
    expect_scalar(&f, "Component[ExteriorD[alpha], 1, 0]", "1");

    /* Volume and the orthonormal Hodge dual of the declared signature. */
    PHY_CHECK_EQ_STR(expansion(&f, run(&f, "Volume[M]")), "(wedge dx dy)");
    PHY_CHECK_EQ_STR(expansion(&f, run(&f, "HodgeStar[alpha]")), "(* dy y)");

    /* A form with no components at all is the zero form, not an error. */
    expect_decision(&f, "ZeroQ[DifferentialForm[M, 2]]", "True");
    fixture_close(&f);
}

static void test_exterior_calculus_identities(void)
{
    fixture f = fixture_open();
    (void)run(&f, "M = Manifold[{x, y, z}, Euclidean]");
    (void)run(&f, "a = DifferentialForm[M, 1, {1, 0, 0}]");
    (void)run(&f, "b = DifferentialForm[M, 1, {0, 1, 0}]");
    (void)run(&f, "w = DifferentialForm[M, 1, {x*y, 0, 0}]");

    /* Graded commutativity of the wedge at (1,1): a^b = -b^a. */
    PHY_CHECK_EQ_STR(expansion(&f, run(&f, "Wedge[a, b]")), "(wedge dx dy)");
    expect_decision(&f, "EquivalentQ[Wedge[a, b], -1*Wedge[b, a]]", "True");
    expect_decision(&f, "ZeroQ[Wedge[a, a]]", "True");

    /* d^2 = 0 on a component the CAS can differentiate. */
    PHY_CHECK_EQ_STR(expansion(&f, run(&f, "ExteriorD[w]")),
                     "(* -1 x (wedge dx dy))");
    expect_decision(&f, "ZeroQ[ExteriorD[ExteriorD[w]]]", "True");

    /* The graded Leibniz rule d(a^w) = da^w - a^dw, with da = 0. */
    expect_decision(
        &f,
        "EquivalentQ[ExteriorD[Wedge[a, w]], -1*Wedge[a, ExteriorD[w]]]",
        "True");

    /* iota_v iota_v = 0, and the contraction itself. */
    (void)run(&f, "v = VectorField[M, {1, 2, 0}]");
    expect_scalar(&f, "Component[InteriorProduct[w, v]]", "(* x y)");
    (void)run(&f, "s = DifferentialForm[M, 2, {x, 0, 0}]");
    expect_scalar(&f, "Component[InteriorProduct[s, v], 1]", "x");
    expect_decision(&f, "ZeroQ[InteriorProduct[InteriorProduct[s, v], v]]",
                    "True");

    /* Linear structure: objects add and scale through ordinary arithmetic. */
    PHY_CHECK_EQ_STR(expansion(&f, run(&f, "a + 3*b")),
                     "(+ dx (* 3 dy))");
    PHY_CHECK_EQ_STR(expansion(&f, run(&f, "a/2")), "(* (rat 1 2) dx)");
    expect_decision(&f, "ZeroQ[a - a]", "True");
    fixture_close(&f);
}

static void test_general_metric_hodge(void)
{
    fixture f = fixture_open();
    (void)run(&f, "M = Manifold[{u, v}, Euclidean]");
    (void)run(&f, "g = Metric[M, {{1, 0}, {0, r^2}}]");
    PHY_CHECK_EQ_STR(describe(&f, run(&f, "g")), "Tensor g rank 2 dim 2");
    expect_scalar(&f, "Component[g, 1, 1]", "(^ r 2)");
    expect_scalar(&f, "Rank[g]", "2");

    /* sqrt(|det g|) d^2 x with det g = r^2. */
    PHY_CHECK_EQ_STR(expansion(&f, run(&f, "Volume[M, g]")),
                     "(* (^ (^ r 2) (rat 1 2)) (wedge du dv))");
    /* *du = g^{uu} sqrt(det g) dv = r dv. */
    (void)run(&f, "one = DifferentialForm[M, 1, {1, 0}]");
    expect_decision(
        &f,
        "EquivalentQ[HodgeStar[one, g], "
        "DifferentialForm[M, 1, {0, (r^2)^(1/2)}]]",
        "True");
    fixture_close(&f);
}

/* ---------------------------------------------------------- Lie algebra */

static void test_lie_groups_and_brackets(void)
{
    fixture f = fixture_open();

    phy_value group = run(&f, "G = LieGroup[SU2]");
    PHY_CHECK_EQ_INT(group.kind, PHY_VALUE_LIE_GROUP);
    PHY_CHECK_EQ_STR(describe(&f, group), "LieGroup SU(2) rep 2 compact");

    phy_value algebra = run(&f, "su = LieAlgebra[G]");
    PHY_CHECK_EQ_STR(describe(&f, algebra), "LieAlgebra SU(2) dim 3");
    expect_scalar(&f, "Dimension[su]", "3");

    /* [T1,T2] = T3 in the built-in epsilon basis. */
    phy_value bracket =
        run(&f, "LieBracket[Generator[su, 0], Generator[su, 1]]");
    PHY_CHECK_EQ_INT(bracket.kind, PHY_VALUE_LIE_ELEMENT);
    PHY_CHECK_EQ_STR(expansion(&f, bracket), "T3");
    expect_scalar(
        &f, "Component[LieBracket[Generator[su,0], Generator[su,1]], 2]", "1");
    expect_decision(
        &f, "ZeroQ[LieBracket[Generator[su,0], Generator[su,0]]]", "True");

    /* Antisymmetry and the Killing form both come from the backend. */
    expect_scalar(&f, "StructureConstant[su, 0, 1, 2]", "1");
    expect_scalar(&f, "StructureConstant[su, 1, 0, 2]", "-1");
    expect_scalar(&f, "Killing[su, 0, 0]", "-2");
    expect_scalar(&f, "Killing[su, 0, 1]", "0");

    /* Elements are a vector space over exact scalars. */
    phy_value element = run(&f, "e = LieElement[su, {2, 0, k}]");
    PHY_CHECK_EQ_STR(expansion(&f, element), "(+ (* 2 T1) (* T3 k))");
    expect_scalar(&f, "Component[3*e, 0]", "6");

    /* A group with no structure constants here fails rather than pretends. */
    expect_status(&f, "LieGroup[SU5]", PHY_ERR_UNSUPPORTED);
    expect_status(&f, "Generator[su, 9]", PHY_ERR_DOMAIN);

    /* The generic noncommutative commutator still works on scalars. */
    expect_scalar(&f, "LieBracket[A, B]",
                  "(+ (nc* A B) (* -1 (nc* B A)))");
    fixture_close(&f);
}

/* ------------------------------------------------------------ Yang-Mills */

static void test_abelian_gauge_field(void)
{
    fixture f = fixture_open();
    (void)run(&f, "M = Manifold[{x, y, z}, Euclidean]");
    (void)run(&f, "u1 = LieAlgebra[LieGroup[U1]]");

    /* A = x dy, hence F = dx ^ dy: tests/test_yang_mills.c, directly. */
    phy_value connection = run(&f, "A = GaugeConnection[u1, M, {{0, x, 0}}]");
    PHY_CHECK_EQ_INT(connection.kind, PHY_VALUE_LIE_FORM);
    PHY_CHECK_EQ_STR(describe(&f, connection),
                     "LieForm degree 1 of U(1) on M");
    PHY_CHECK_EQ_STR(expansion(&f, connection), "(nc* Q (* dy x))");

    phy_value curvature = run(&f, "F = FieldStrength[A, g]");
    expect_scalar(&f, "Component[F, 0, 0, 1]", "1");
    expect_scalar(&f, "Component[F, 0, 0, 2]", "0");
    expect_scalar(&f, "Component[F, 0, 1, 2]", "0");
    PHY_CHECK_EQ_STR(expansion(&f, curvature), "(nc* Q (wedge dx dy))");
    expect_scalar(&f, "Degree[F]", "2");

    /* dF = 0 is proved, not asserted. */
    expect_decision(&f, "ZeroQ[Bianchi[A, g]]", "True");

    /* -1/2 h_ab F^a ^ *F^b with the identity metric and h = 1. */
    (void)run(&f, "gm = Metric[M, {{1,0,0},{0,1,0},{0,0,1}}]");
    expect_scalar(&f, "Component[YangMillsLagrangian[F, gm, {{1}}], 0, 1, 2]",
                  "(rat -1 2)");
    /* The Killing form of U(1) is zero, so the default density vanishes. */
    expect_decision(&f, "ZeroQ[YangMillsLagrangian[F, gm]]", "True");

    /* A colour component is an ordinary form and behaves like one. */
    PHY_CHECK_EQ_STR(expansion(&f, run(&f, "ColorComponent[F, 0]")),
                     "(wedge dx dy)");
    fixture_close(&f);
}

static void test_nonabelian_gauge_field(void)
{
    fixture f = fixture_open();
    (void)run(&f, "M = Manifold[{x, y, z}, Euclidean]");
    (void)run(&f, "su = LieAlgebra[LieGroup[SU2]]");
    (void)run(&f,
              "A = GaugeConnection[su, M, {{1,0,0},{0,1,0},{0,0,1}}]");
    (void)run(&f, "F = FieldStrength[A, g]");

    /*
     * dA = 0 for constant components, so F = (g/2)[A,A] alone. The nine
     * components are the ones tests/test_yang_mills.c certifies against the
     * backend; reaching them from source is what this file adds.
     */
    expect_scalar(&f, "Component[F, 0, 0, 1]", "0");
    expect_scalar(&f, "Component[F, 0, 0, 2]", "0");
    expect_scalar(&f, "Component[F, 0, 1, 2]", "g");
    expect_scalar(&f, "Component[F, 1, 0, 2]", "(* -1 g)");
    expect_scalar(&f, "Component[F, 2, 0, 1]", "g");

    /* The non-Abelian Bianchi identity D_A F = 0. */
    expect_decision(&f, "ZeroQ[Bianchi[A, g]]", "True");

    /*
     * Gauge covariance: delta F = g [F, alpha] is the curvature branch, and
     * delta A = D_A alpha the connection branch. The evaluator picks the
     * formula from the operand's degree.
     */
    (void)run(&f, "al = LieForm[su, M, 0, {{c1},{c2},{c3}}]");
    expect_decision(&f,
                    "EquivalentQ[GaugeVariation[F, al, g], "
                    "g*LieBracket[F, al]]",
                    "True");
    expect_decision(&f,
                    "EquivalentQ[GaugeVariation[A, al, g], "
                    "CovariantD[A, al, g]]",
                    "True");
    expect_status(&f, "GaugeVariation[al, al, g]", PHY_ERR_TYPE);
    fixture_close(&f);
}

/* ------------------------------------------------- general relativity */

static void test_curvature_pipeline(void)
{
    fixture f = fixture_open();
    (void)run(&f, "M = Manifold[{theta, phi}, Euclidean]");
    (void)run(&f, "g = Metric[M, {{a^2, 0}, {0, a^2*Sin[theta]^2}}]");
    phy_value bundle = run(&f, "c = Curvature[g]");
    PHY_CHECK_EQ_INT(bundle.kind, PHY_VALUE_CURVATURE);
    PHY_CHECK_EQ_STR(describe(&f, bundle),
                     "Curvature dim 2 (Christoffel/Riemann/Ricci/Einstein)");

    /* The round two-sphere, exactly as tests/test_gr.c certifies it. */
    expect_scalar(&f, "RicciScalar[c]", "(* 2 (^ a -2))");
    expect_scalar(&f, "Component[Christoffel[c], 0, 1, 1]",
                  "(* -1 (fn cos theta) (fn sin theta))");
    expect_scalar(&f, "Component[Riemann[c], 0, 1, 0, 1]",
                  "(* (^ a 2) (^ (fn sin theta) 2))");
    expect_decision(&f, "ZeroQ[Einstein[c]]", "True");
    expect_scalar(&f, "Component[InverseMetric[c], 0, 0]", "(^ a -2)");
    expect_scalar(&f, "Rank[Riemann[c]]", "4");
    expect_scalar(&f, "Kretschmann[c]", "(* 4 (^ a -4))");
    expect_decision(
        &f, "ZeroQ[CovariantDerivative[Ricci[c], c]]", "True");
    fixture_close(&f);
}

/* ----------------------------------------------------- bounded QFT front end */

static void test_qft_heads_reach_native_backends(void)
{
    fixture f = fixture_open();

    expect_scalar(&f, "DiracTrace[{}]", "4");
    expect_scalar(
        &f, "DiracTrace[{Up[mu,Lorentz],Down[mu,Lorentz]}]", "16");

    expect_decision(
        &f,
        "EquivalentQ["
        "MandelstamReduce[LorentzDot[p1,p2],{p1,p2,p3,p4},"
        "{m1,m2,m3,m4},Peskin],"
        "(s-m1^2-m2^2)/2]",
        "True");
    expect_decision(
        &f,
        "EquivalentQ["
        "MandelstamReduce[LorentzDot[p1,p3],{p1,p2,p3,p4},"
        "{m1,m2,m3,m4},AllIncoming],"
        "(t-m1^2-m3^2)/2]",
        "True");

    const phy_value lagrangian =
        run(&f, "Phi4Lagrangian[phi,m,lambda,4]");
    PHY_CHECK_EQ_INT(lagrangian.kind, PHY_VALUE_SCALAR);
    const char *lagrangian_text = expansion(&f, lagrangian);
    PHY_CHECK(strstr(lagrangian_text, "ScalarField") != NULL);
    PHY_CHECK(strstr(lagrangian_text, "Partial") != NULL);

    const phy_value eom = run(&f, "Phi4EOM[phi,m,lambda,4]");
    PHY_CHECK_EQ_INT(eom.kind, PHY_VALUE_SCALAR);
    const char *eom_text = expansion(&f, eom);
    PHY_CHECK(strstr(eom_text, "Box") != NULL);
    PHY_CHECK(strstr(eom_text, "(rat 1 6)") != NULL);

    const phy_value diagrams =
        run(&f, "Phi4Diagrams[phi,m,lambda,4,s,t,u]");
    PHY_CHECK_EQ_INT(diagrams.kind, PHY_VALUE_SCALAR);
    PHY_CHECK_EQ_INT(
        phy_ir_kind_of(f.ir, diagrams.as.scalar), PHY_IR_FUNCTION);
    PHY_CHECK_EQ_INT(
        phy_ir_child_count(f.ir, diagrams.as.scalar), 5);
    const char *diagram_text = expansion(&f, diagrams);
    PHY_CHECK(strstr(diagram_text, "TadpoleIntegral") != NULL);
    PHY_CHECK(strstr(diagram_text, "BubbleIntegral") != NULL);
    fixture_close(&f);
}

static void test_sun_colour_heads_reach_native_backend(void)
{
    fixture f = fixture_open();

    expect_decision(
        &f, "EquivalentQ[SUNCF[N],(N^2-1)/(2N)]", "True");
    expect_scalar(&f, "SUNCF[3]", "(rat 4 3)");
    expect_scalar(&f, "SUNCA[N]", "N");
    expect_decision(
        &f,
        "EquivalentQ[SUNExpandCasimirs[C_F+C_A,N],"
        "(N^2-1)/(2N)+N]",
        "True");

    expect_scalar(&f, "SUNFComponent[3,1,2,3]", "1");
    expect_scalar(&f, "SUNFComponent[3,1,4,7]", "(rat 1 2)");
    expect_decision(
        &f,
        "EquivalentQ[SUNFComponent[3,4,5,8],Sqrt[3]/2]",
        "True");

    expect_decision(
        &f, "EquivalentQ[SUNDelta[a,a,N],N^2-1]", "True");
    expect_scalar(&f, "SUNF[a,a,c,N]", "0");

    const phy_value trace2 = run(&f, "SUNTrace[{a,b},N]");
    PHY_CHECK_EQ_INT(trace2.kind, PHY_VALUE_SCALAR);
    const char *trace2_text = expansion(&f, trace2);
    PHY_CHECK(strstr(trace2_text, "SUNDelta") != NULL);
    PHY_CHECK(strstr(trace2_text, "(rat 1 2)") != NULL);

    const phy_value trace3 = run(&f, "SUNTrace[{a,b,c},N]");
    const char *trace3_text = expansion(&f, trace3);
    PHY_CHECK(strstr(trace3_text, "SUND") != NULL);
    PHY_CHECK(strstr(trace3_text, "SUNF") != NULL);
    PHY_CHECK(strstr(trace3_text, " I ") != NULL);

    const phy_value trace4 = run(&f, "SUNTrace[{a,b,c,d},N]");
    PHY_CHECK_EQ_INT(
        phy_ir_kind_of(f.ir, trace4.as.scalar), PHY_IR_OPERATOR);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, phy_ir_head(f.ir, trace4.as.scalar)),
        "SUNTrace");
    PHY_CHECK_EQ_INT(phy_ir_child_count(f.ir, trace4.as.scalar), 5);
    const char *held_trace = expansion(&f, trace4);
    PHY_CHECK(strstr(held_trace, "SUNTrace N") != NULL);

    /* The held long-trace spelling is stable when pasted back into a cell. */
    const phy_value trace4_again = run(&f, "SUNTrace[N,a,b,c,d]");
    PHY_CHECK_EQ_INT(trace4_again.as.scalar, trace4.as.scalar);

    const phy_value commutator = run(&f, "SUNCommutator[a,b,N]");
    const char *commutator_text = expansion(&f, commutator);
    PHY_CHECK(strstr(commutator_text, "SUNF") != NULL);
    PHY_CHECK(strstr(commutator_text, "SUNGenerator") != NULL);
    PHY_CHECK(strstr(commutator_text, " I ") != NULL);

    const phy_value contraction =
        run(&f, "SUNDeltaContract[a,b,SUNT[b,N],N]");
    const char *contraction_text = expansion(&f, contraction);
    PHY_CHECK(strstr(contraction_text, "SUNGenerator") != NULL);
    PHY_CHECK(strstr(contraction_text, "idx a up ColorAdjoint") != NULL);

    const phy_value fundamental =
        run(&f, "SUNFundamentalCasimir[N]");
    const char *fundamental_text = expansion(&f, fundamental);
    PHY_CHECK(strstr(fundamental_text, "C_F") != NULL);
    PHY_CHECK(strstr(fundamental_text, "IdentityFundamental") != NULL);
    const phy_value adjoint = run(&f, "SUNAdjointCasimir[a,b,N]");
    const char *adjoint_text = expansion(&f, adjoint);
    PHY_CHECK(strstr(adjoint_text, "C_A") != NULL);
    PHY_CHECK(strstr(adjoint_text, "SUNDelta") != NULL);

    /* A Lorentz index can never be consumed as colour. */
    expect_status(
        &f, "SUNDelta[a,Up[mu,Lorentz],N]", PHY_ERR_TYPE);
    expect_status(&f, "SUNFComponent[N,1,2,3]", PHY_ERR_UNSUPPORTED);
    fixture_close(&f);
}

/* -------------------------------------------------------- typed errors */

static void test_reserved_heads_never_silently_pass_through(void)
{
    fixture f = fixture_open();
    (void)run(&f, "M = Manifold[{x, y}, Euclidean]");
    (void)run(&f, "al = DifferentialForm[M, 1, {1, 0}]");

    /* Wrong operand kind, wrong arity, wrong index: all typed. */
    expect_status(&f, "ExteriorD[3]", PHY_ERR_TYPE);
    expect_status(&f, "ExteriorD[al, al]", PHY_ERR_PARSE);
    expect_status(&f, "HodgeStar[M]", PHY_ERR_TYPE);
    expect_status(&f, "Component[al, 5]", PHY_ERR_DOMAIN);
    expect_status(&f, "Component[al, 0, 1]", PHY_ERR_PARSE);
    expect_status(&f, "Metric[M, {{1, 0}}]", PHY_ERR_PARSE);
    expect_status(&f, "Manifold[{x2}, Weird]", PHY_ERR_PARSE);
    expect_status(&f, "DifferentialForm[M, 7, {1}]", PHY_ERR_DOMAIN);
    expect_status(&f, "Curvature[al]", PHY_ERR_TYPE);
    expect_status(&f, "Ricci[al]", PHY_ERR_TYPE);
    expect_status(&f, "Rank[al]", PHY_ERR_TYPE);

    /* An object cannot leak into scalar algebra or a scalar command. */
    expect_status(&f, "M + 1", PHY_ERR_TYPE);
    expect_status(&f, "Expand[M]", PHY_ERR_TYPE);
    expect_status(&f, "Sin[al]", PHY_ERR_TYPE);
    expect_status(&f, "Wedge[al, M]", PHY_ERR_TYPE);
    expect_status(&f, "al * al", PHY_ERR_TYPE);
    expect_status(&f, "ZeroQ[M]", PHY_ERR_TYPE);
    expect_status(&f, "EquivalentQ[al, M]", PHY_ERR_TYPE);

    /* Differentiating with respect to a bound name is not a variable. */
    expect_scalar(&f, "k = 2", "2");
    expect_status(&f, "D[k^2, k]", PHY_ERR_TYPE);

    /* Output constructors remain typed IR and are not mistaken for commands. */
    const phy_value scalar_field = run(&f, "ScalarField[phi, 4]");
    PHY_CHECK_EQ_INT(scalar_field.kind, PHY_VALUE_SCALAR);
    PHY_CHECK_EQ_INT(phy_ir_kind_of(f.ir, scalar_field.as.scalar),
                     PHY_IR_OPERATOR);
    fixture_close(&f);
}

/*
 * The parser's reserved-head table and the evaluator's dispatch table are two
 * lists that have to agree, and a name present in one but not the other fails
 * silently: the head becomes an ordinary function application, its operands are
 * evaluated, and a wrong-typed argument is the only symptom. Called with no
 * arguments, every evaluated head must reject; a head that went missing from
 * either table returns a value instead.
 */
static void test_every_evaluated_head_rejects_empty_arguments(void)
{
    static const char *const kHeads[] = {
        "Manifold",     "DifferentialForm",    "Metric",
        "VectorField",  "ExteriorD",           "InteriorProduct",
        "HodgeStar",    "Volume",              "LieGroup",
        "LieAlgebra",   "Generator",           "LieElement",
        "LieBracket",   "StructureConstant",   "Killing",
        "LieForm",      "GaugeConnection",     "CovariantD",
        "FieldStrength", "GaugeVariation",     "Bianchi",
        "YangMillsLagrangian",                 "ColorComponent",
        "Curvature",    "InverseMetric",       "Christoffel",
        "Riemann",      "RiemannMixed",        "Ricci",
        "RicciScalar",  "Einstein",            "Kretschmann",
        "CovariantDerivative",                 "Phi4Lagrangian",
        "Phi4EOM",      "Phi4Diagrams",         "MandelstamReduce",
        "DiracTrace",   "SUNDelta",             "SUNF",
        "SUND",         "SUNT",                 "SUNTrace",
        "SUNCommutator", "SUNDeltaContract",    "SUNCF",
        "SUNCA",        "SUNFComponent",         "SUNExpandCasimirs",
        "SUNFundamentalCasimir",                "SUNAdjointCasimir",
        "Component",
        "Degree",       "Dimension",           "Rank",
        "ZeroQ",        "EquivalentQ",
    };
    fixture f = fixture_open();
    for (size_t i = 0u; i < sizeof kHeads / sizeof kHeads[0]; ++i) {
        char source[48];
        (void)snprintf(source, sizeof source, "%s[]", kHeads[i]);
        expect_status(&f, source, PHY_ERR_PARSE);
    }
    fixture_close(&f);
}

/* ------------------------------------------------------------ ownership */

static void test_objects_are_swept_and_never_leaked(void)
{
    fixture f = fixture_open();
    (void)run(&f, "M = Manifold[{x, y, z}, Euclidean]");
    (void)run(&f, "a = DifferentialForm[M, 1, {x, 0, 0}]");
    (void)run(&f, "b = DifferentialForm[M, 1, {0, y, 0}]");
    const size_t settled = phy_env_object_count(f.env);
    /* chart, manifold, two forms. */
    PHY_CHECK_EQ_INT(settled, 4u);

    /* A nested expression builds intermediates; none of them survive it. */
    expect_decision(&f, "ZeroQ[ExteriorD[Wedge[a, b]]]", "True");
    PHY_CHECK_EQ_INT(phy_env_object_count(f.env), settled);

    /* A bound result does survive, and exactly one object joins the table. */
    (void)run(&f, "c = Wedge[a, b]");
    PHY_CHECK_EQ_INT(phy_env_object_count(f.env), settled + 1u);

    /* Rebinding drops the old object. */
    (void)run(&f, "c = Wedge[b, a]");
    PHY_CHECK_EQ_INT(phy_env_object_count(f.env), settled + 1u);

    /*
     * Binding a form while overwriting its manifold's name must keep the
     * manifold alive: reachability follows dependencies, not names.
     */
    (void)run(&f, "M = Manifold[{p, q}, Euclidean]");
    PHY_CHECK_EQ_INT(phy_env_validate(f.env), PHY_OK);
    expect_scalar(&f, "Component[a, 0]", "x");
    expect_scalar(&f, "Dimension[c]", "3");

    /* A failed command sweeps too: nothing it built half-way survives. */
    const size_t before = phy_env_object_count(f.env);
    expect_status(&f, "d = Wedge[DifferentialForm[M, 1, {1, 0}], M]",
                  PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_env_object_count(f.env), before);
    PHY_CHECK(!phy_env_lookup(f.env, "d", NULL));

    /* Clearing every name releases every object. */
    phy_value value;
    PHY_CHECK_EQ_INT(run_status(&f, "ClearAll[]", &value), PHY_OK);
    PHY_CHECK_EQ_INT(phy_env_object_count(f.env), 0u);
    fixture_close(&f);
}

static void test_environment_bounds(void)
{
    fixture f = fixture_open();
    char source[32];
    for (unsigned i = 0u; i < PHY_EVAL_MAX_BINDINGS; ++i) {
        (void)snprintf(source, sizeof source, "n%u = %u", i, i);
        expect_status(&f, source, PHY_OK);
    }
    PHY_CHECK_EQ_INT(phy_env_binding_count(f.env), PHY_EVAL_MAX_BINDINGS);
    expect_status(&f, "overflowing = 1", PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(phy_env_validate(f.env), PHY_OK);
    fixture_close(&f);
}

/* ------------------------------------------------ notebook integration */

static void test_notebook_shares_state_between_cells(void)
{
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);
    size_t manifold_cell = 0u;
    size_t form_cell = 0u;
    size_t query_cell = 0u;
    PHY_CHECK_EQ_INT(
        phy_notebook_add_input(notebook, "M = Manifold[{x, y}, Euclidean]",
                               &manifold_cell),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_notebook_add_input(notebook,
                               "al = DifferentialForm[M, 1, {y, 0}]",
                               &form_cell),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_notebook_add_input(notebook, "Component[ExteriorD[al], 0, 1]",
                               &query_cell),
        PHY_OK);

    /* Out of order, the later cells cannot see a binding that does not exist. */
    PHY_CHECK_EQ_INT(phy_notebook_evaluate(notebook, form_cell),
                     PHY_ERR_TYPE);

    PHY_CHECK_EQ_INT(phy_notebook_evaluate_all(notebook), PHY_OK);

    phy_notebook_cell_view view;
    /* The manifold output has a descriptor and no expression. */
    PHY_CHECK(phy_notebook_cell(notebook, manifold_cell + 1u, &view));
    PHY_CHECK_EQ_INT(view.kind, PHY_NOTEBOOK_CELL_OUTPUT);
    PHY_CHECK_EQ_INT(view.expression, PHY_IR_NULL);
    PHY_CHECK_EQ_STR(view.primary,
                     "Manifold M dim 2 Riemannian +oriented (x,y)");

    /* The form output has the coframe expansion and no descriptor. */
    PHY_CHECK(phy_notebook_cell(notebook, form_cell + 2u, &view));
    PHY_CHECK(view.expression != PHY_IR_NULL);
    PHY_CHECK_EQ_STR(view.primary, "");

    /* The query cell computed a real exterior derivative. */
    PHY_CHECK(phy_notebook_cell(notebook, query_cell + 3u, &view));
    PHY_CHECK_EQ_INT(view.kind, PHY_NOTEBOOK_CELL_OUTPUT);
    int64_t component = 0;
    PHY_CHECK(phy_ir_integer_value(phy_notebook_ir(notebook), view.expression,
                                   &component));
    PHY_CHECK_EQ_INT(component, -1);

    phy_env *env = phy_notebook_environment(notebook);
    PHY_CHECK(env != NULL);
    PHY_CHECK(phy_env_lookup(env, "al", NULL));
    PHY_CHECK_EQ_INT(phy_env_validate(env), PHY_OK);

    /* Re-running an earlier cell makes every later result stale. */
    PHY_CHECK_EQ_INT(phy_notebook_evaluate(notebook, manifold_cell), PHY_OK);
    PHY_CHECK(phy_notebook_cell(notebook, query_cell + 3u, &view));
    PHY_CHECK(view.stale);
    PHY_CHECK(phy_notebook_cell(notebook, manifold_cell + 1u, &view));
    PHY_CHECK(!view.stale);

    phy_notebook_destroy(notebook);
}

static void test_notebook_round_trip_keeps_descriptors(void)
{
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);
    PHY_CHECK_EQ_INT(
        phy_notebook_add_input(notebook, "G = LieGroup[SU3]", NULL), PHY_OK);
    PHY_CHECK_EQ_INT(phy_notebook_evaluate_all(notebook), PHY_OK);

    uint8_t buffer[PHY_NOTEBOOK_DOCUMENT_MAX_BYTES];
    size_t size = 0u;
    PHY_CHECK_EQ_INT(
        phy_notebook_serialize(notebook, buffer, sizeof buffer, &size),
        PHY_OK);
    phy_notebook *loaded = NULL;
    PHY_CHECK_EQ_INT(phy_notebook_deserialize(buffer, size, &loaded), PHY_OK);
    PHY_CHECK(loaded != NULL);

    phy_notebook_cell_view view;
    PHY_CHECK(phy_notebook_cell(loaded, 1u, &view));
    PHY_CHECK_EQ_INT(view.kind, PHY_NOTEBOOK_CELL_OUTPUT);
    PHY_CHECK_EQ_INT(view.status, PHY_OK);
    PHY_CHECK_EQ_STR(view.primary, "LieGroup SU(3) rep 3 compact");

    /*
     * The document restores cells, never objects: a loaded notebook has an
     * empty environment until it is replayed. That is the honest state, and it
     * is why phy_notebook_evaluate_all exists.
     */
    PHY_CHECK_EQ_INT(phy_env_binding_count(phy_notebook_environment(loaded)),
                     0u);
    PHY_CHECK_EQ_INT(phy_notebook_evaluate_all(loaded), PHY_OK);
    PHY_CHECK_EQ_INT(phy_env_binding_count(phy_notebook_environment(loaded)),
                     1u);

    phy_notebook_destroy(loaded);
    phy_notebook_destroy(notebook);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }
    PHY_TEST_CASE(test_scalar_state_flows_between_cells);
    PHY_TEST_CASE(test_clear_and_reset);
    PHY_TEST_CASE(test_binding_rejects_reserved_and_captured_names);
    PHY_TEST_CASE(test_manifolds_and_forms);
    PHY_TEST_CASE(test_exterior_calculus_identities);
    PHY_TEST_CASE(test_general_metric_hodge);
    PHY_TEST_CASE(test_lie_groups_and_brackets);
    PHY_TEST_CASE(test_abelian_gauge_field);
    PHY_TEST_CASE(test_nonabelian_gauge_field);
    PHY_TEST_CASE(test_curvature_pipeline);
    PHY_TEST_CASE(test_qft_heads_reach_native_backends);
    PHY_TEST_CASE(test_sun_colour_heads_reach_native_backend);
    PHY_TEST_CASE(test_reserved_heads_never_silently_pass_through);
    PHY_TEST_CASE(test_every_evaluated_head_rejects_empty_arguments);
    PHY_TEST_CASE(test_objects_are_swept_and_never_leaked);
    PHY_TEST_CASE(test_environment_bounds);
    PHY_TEST_CASE(test_notebook_shares_state_between_cells);
    PHY_TEST_CASE(test_notebook_round_trip_keeps_descriptors);
    const int result = PHY_TEST_REPORT("test_eval");
    phy_platform_shutdown();
    return result;
}
