/* Reader-facing Mathematica-style source must produce typed IR exactly. */
#include <stddef.h>
#include <string.h>

#include "phy/platform.h"
#include "phy/source.h"
#include "phy_test.h"

static char g_text[512];

static const char *render(phy_ir_context *ir, phy_ir_ref expression)
{
    size_t length = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(ir, expression, g_text, sizeof g_text, &length), PHY_OK);
    PHY_CHECK(length < sizeof g_text);
    return g_text;
}

static phy_source_command parse(phy_ir_context *ir, const char *source)
{
    phy_source_command command;
    size_t error = 999u;
    PHY_CHECK_EQ_INT(phy_source_parse(ir, source, &command, &error), PHY_OK);
    PHY_CHECK_EQ_INT(error, strlen(source));
    return command;
}

static void test_operator_precedence_and_exact_numbers(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    phy_source_command command = parse(ir, "1 + 2*x^3");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_SIMPLIFY);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(+ 1 (* 2 (^ x 3)))");

    command = parse(ir, "-x^2 + 1.25");
    PHY_CHECK_EQ_STR(render(ir, command.expression),
                     "(+ (rat 5 4) (* -1 (^ x 2)))");

    command = parse(ir, "1/2 + 1/3");
    PHY_CHECK_EQ_STR(render(ir, command.expression),
                     "(+ (* 1 (^ 2 -1)) (* 1 (^ 3 -1)))");

    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
}

static void test_promoted_exact_source(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    phy_source_command command =
        parse(ir, "184467440737095516160000000000000000001");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "184467440737095516160000000000000000001");

    command = parse(ir, "-184467440737095516160000000000000000001");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "-184467440737095516160000000000000000001");

    command = parse(ir, "1.000000000000000000001");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(rat 1000000000000000000001 1000000000000000000000)");

    command = parse(
        ir, "Rational[36893488147419103232, 6]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(rat 18446744073709551616 3)");

    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
}

static void test_commands_and_functions(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    phy_source_command command = parse(ir, "D[Sin[x]^2, x]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_DIFFERENTIATE);
    PHY_CHECK_EQ_INT(command.variable_count, 1);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(^ (fn sin x) 2)");
    PHY_CHECK_EQ_STR(render(ir, command.variables[0]), "x");

    command = parse(ir, "Expand[(x+1)^2]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_EXPAND);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(^ (+ 1 x) 2)");

    command = parse(ir, "Simplify[Cos(theta)^2 + Sin(theta)^2]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_SIMPLIFY);
    PHY_CHECK_EQ_STR(render(ir, command.expression),
                     "(+ (^ (fn cos theta) 2) (^ (fn sin theta) 2))");

    command = parse(ir, "f[x, Log[Exp[y]]]");
    PHY_CHECK_EQ_STR(render(ir, command.expression),
                     "(fn f x (fn log (fn exp y)))");

    command = parse(ir, "Together[(x+1)/(x-1)]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_TOGETHER);
    command = parse(ir, "Cancel[(x^2-1)/(x^2-2x+1)]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_CANCEL);
    command = parse(ir, "Factor[x^4-1]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_FACTOR);
    command = parse(ir, "Apart[1/(x^2-1)]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_APART);
    command = parse(ir, "Numerator[x/y]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_NUMERATOR);
    command = parse(ir, "Denominator[x/y]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_DENOMINATOR);
    command = parse(ir, "FullSimplify[x+x]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_FULL_SIMPLIFY);

    command = parse(ir, "D[x y, x, y]");
    PHY_CHECK_EQ_INT(command.variable_count, 2);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(* x y)");
    command = parse(ir, "Integrate[Sin[2x], x]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_INTEGRATE);
    PHY_CHECK_EQ_INT(command.variable_count, 1);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(fn sin (* 2 x))");

    command = parse(ir, "Series[(1+x)^5,{x,2,7}]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_SERIES);
    PHY_CHECK_EQ_INT(command.variable_count, 1);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(^ (+ 1 x) 5)");
    PHY_CHECK_EQ_STR(render(ir, command.variables[0]), "x");
    PHY_CHECK_EQ_STR(render(ir, command.parameter), "2");
    PHY_CHECK_EQ_INT(command.series_order, 7);
    command = parse(ir, "Normal[x]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_NORMAL);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "x");
    command = parse(
        ir, "Normal[Series[Exp[x],{x,0,5}]]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_NORMAL);
    PHY_CHECK(command.normal_series);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(fn exp x)");
    PHY_CHECK_EQ_STR(render(ir, command.variables[0]), "x");
    PHY_CHECK_EQ_INT(command.series_order, 5);

    command = parse(ir, "ArcTan[Sinh[x]] + ArcSinh[TanH[y]]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(+ (fn asinh (fn tanh y)) (fn atan (fn sinh x)))");
    command = parse(ir, "ArcCos[x] + ArcCosh[y] + ArcTanh[z]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(+ (fn acos x) (fn acosh y) (fn atanh z))");
    command = parse(ir, "Gamma[x] + LogGamma[y] + Erf[z] + Erfc[w]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(+ (fn erf z) (fn erfc w) (fn gammafn x) (fn loggamma y))");

    command = parse(ir, "2x + (x+1)(x-1) == {x, y}");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(= (+ (* 2 x) (* (+ -1 x) (+ 1 x))) (fn List x y))");

    command = parse(ir, "Plus[Power[x,2], Rational[1,2], Sqrt[y]]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(+ (rat 1 2) (^ x 2) (^ y (rat 1 2)))");
    command = parse(ir, "Equal[Times[2,x], 4]");
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(= (* 2 x) 4)");

    command = parse(
        ir,
        "Tensor[g,Down[mu,Lorentz],Up[nu,Lorentz]]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(tensor g (idx mu dn Lorentz) (idx nu up Lorentz))");
    command = parse(
        ir,
        "NonCommutativeMultiply[Operator[Gamma,Up[mu,Lorentz]],"
        "Operator[Gamma,Down[nu,Lorentz]]]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(nc* (op Gamma (idx mu up Lorentz)) "
        "(op Gamma (idx nu dn Lorentz)))");
    command = parse(ir, "Wedge[Down[mu],Down[nu]]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(wedge (idx mu dn) (idx nu dn))");
    command = parse(ir, "Commutator[A,B]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(+ (* -1 (nc* B A)) (nc* A B))");
    command = parse(ir, "LieBracket[X,Y]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression), "(op LieBracket X Y)");
    command = parse(ir, "ScalarField[phi]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression), "(op ScalarField phi)");
    command = parse(ir, "HodgeStar[Wedge[a,b]]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression), "(op HodgeStar (wedge a b))");
    command = parse(ir, "Manifold[M,4,Lorentzian]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(op Manifold M 4 Lorentzian)");
    command = parse(ir, "HodgeStar[omega,g]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression), "(op HodgeStar omega g)");
    command = parse(ir, "FieldStrength[A,g]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression), "(op FieldStrength A g)");
    command = parse(ir, "CovariantD[A,omega,g]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression), "(op CovariantD A omega g)");
    command = parse(ir, "YangMillsLagrangian[F,gMetric,h]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(op YangMillsLagrangian F gMetric h)");

    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
}

static void test_diagnostics_and_bounds(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    phy_source_command command;
    size_t error = 0u;
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "", &command, &error),
                     PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(error, 0);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "Sin[x", &command, &error),
                     PHY_ERR_PARSE);
    PHY_CHECK(error > 0u);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "D[x^2, x+1]", &command, &error),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Integrate[x, Down[mu]]", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Sin[Factor[x]]", &command, &error),
        PHY_ERR_UNSUPPORTED);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Limit[1/x]", &command, &error),
        PHY_ERR_UNSUPPORTED);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Series[x,x]", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Series[x,{x,0}]", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Series[x,{x,y,4}]", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Series[x,{x,0,-1}]", &command, &error),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Series[x,{x,0,64}]", &command, &error),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Commutator[A]", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(NULL, "x", &command, &error),
                     PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
}

/*
 * Assignment, the environment commands, and the reserved-head canonicalization
 * the stateful evaluator depends on.
 */
static void test_assignment_and_reserved_heads(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    phy_source_command command = parse(ir, "a = 2 x + 1");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_ASSIGN);
    PHY_CHECK_EQ_STR(phy_ir_symbol_name(ir, command.target), "a");
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(+ 1 (* 2 x))");

    command = parse(ir, "Set[b, y]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_ASSIGN);
    PHY_CHECK_EQ_STR(phy_ir_symbol_name(ir, command.target), "b");

    /* One character of lookahead separates assignment from an equation. */
    command = parse(ir, "a == 3");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_SIMPLIFY);
    PHY_CHECK_EQ_INT(command.target, PHY_IR_NO_SYMBOL);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(= a 3)");

    /* Clear names one binding; ClearAll and a bare Clear name none. */
    command = parse(ir, "Clear[a]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_CLEAR);
    PHY_CHECK_EQ_STR(phy_ir_symbol_name(ir, command.target), "a");
    PHY_CHECK_EQ_INT(command.expression, PHY_IR_NULL);
    command = parse(ir, "ClearAll[]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_CLEAR);
    PHY_CHECK_EQ_INT(command.target, PHY_IR_NO_SYMBOL);
    command = parse(ir, "Clear[]");
    PHY_CHECK_EQ_INT(command.target, PHY_IR_NO_SYMBOL);

    /*
     * Head matching is case-insensitive but interning is not, so a reserved
     * head must reach the evaluator under one spelling however it was typed.
     * Two different heads here would mean `manifold[...]` silently doing
     * nothing.
     */
    command = parse(ir, "manifold[{x,y},euclidean]");
    PHY_CHECK_EQ_STR(render(ir, command.expression),
                     "(op Manifold (fn List x y) euclidean)");
    command = parse(ir, "ColorComponent[F,0]");
    PHY_CHECK_EQ_STR(render(ir, command.expression),
                     "(op ColorComponent F 0)");
    command = parse(ir, "componenttensor[M,{down,up},{{1,0},{0,1}}]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(op ComponentTensor M (fn List down up) "
        "(fn List (fn List 1 0) (fn List 0 1)))");
    command = parse(ir, "ZeroQ[Bianchi[A,g]]");
    PHY_CHECK_EQ_STR(render(ir, command.expression),
                     "(op ZeroQ (op Bianchi A g))");
    command = parse(ir, "RicciScalar[c]");
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(op RicciScalar c)");
    command = parse(ir, "memorystatus[]");
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(op MemoryStatus)");
    command = parse(ir, "sunf[a,b,c,N]");
    PHY_CHECK_EQ_STR(render(ir, command.expression),
                     "(op SUNF a b c N)");
    command = parse(ir, "SUNTrace[{a,b,c,d},N]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(op SUNTrace (fn List a b c d) N)");
    command = parse(
        ir, "phi4graph[phi,m,lambda,4,{0,1},{{0,3},{3,0}}]");
    PHY_CHECK_EQ_STR(
        render(ir, command.expression),
        "(op Phi4Graph phi m lambda 4 (fn List 0 1) "
        "(fn List (fn List 0 3) (fn List 3 0)))");

    /* A reserved spelling is not a bindable name. */
    size_t error = 0u;
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "Cos = 1", &command, &error),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "Ricci = 1", &command, &error),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "ComponentTensor = 1", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "MemoryStatus = 1", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "SUNCF = 1", &command, &error),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Phi4Graph = 1", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "Integrate = 1", &command, &error),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "Set[Sin, 1]", &command, &error),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "Pi = 3", &command, &error),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "E = 2", &command, &error),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "I = 0", &command, &error),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "EulerGamma = 1", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "Sqrt = 2", &command, &error),
                     PHY_ERR_TYPE);

    /* Mathematica constants are case-sensitive; e and i remain normal names. */
    command = parse(ir, "e = 2");
    PHY_CHECK_EQ_STR(phy_ir_symbol_name(ir, command.target), "e");
    PHY_CHECK_EQ_STR(render(ir, command.expression), "2");
    command = parse(ir, "i = 3");
    PHY_CHECK_EQ_STR(phy_ir_symbol_name(ir, command.target), "i");
    PHY_CHECK_EQ_STR(render(ir, command.expression), "3");
    command = parse(ir, "gamma = 4");
    PHY_CHECK_EQ_STR(phy_ir_symbol_name(ir, command.target), "gamma");
    PHY_CHECK_EQ_STR(render(ir, command.expression), "4");

    command = parse(ir, "Pi + E + I + EulerGamma");
    const uint32_t constant_mask = (uint32_t)PHY_IR_ASSUME_CONSTANT;
    PHY_CHECK((phy_ir_assumptions(ir, phy_ir_intern(ir, "Pi")) &
               constant_mask) != 0u);
    PHY_CHECK((phy_ir_assumptions(ir, phy_ir_intern(ir, "E")) &
               constant_mask) != 0u);
    PHY_CHECK((phy_ir_assumptions(ir, phy_ir_intern(ir, "I")) &
               constant_mask) != 0u);
    PHY_CHECK((phy_ir_assumptions(ir, phy_ir_intern(ir, "EulerGamma")) &
               constant_mask) != 0u);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "2 = x", &command, &error),
                     PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "Set[a]", &command, &error),
                     PHY_ERR_PARSE);

    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
}

static void test_foundation_capability_matrix_is_reserved(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    typedef struct {
        const char *id;
        const char *area;
        const char *source;
        phy_status expected;
        const char *semantic_class;
    } foundation_case;
#define PHY_FOUNDATION_CASE(id, area, source, expected, semantic_class) \
    {#id, area, source, expected, semantic_class},
    static const foundation_case cases[] = {
#include "corpus/cas_foundation_cases.inc"
    };
#undef PHY_FOUNDATION_CASE

    for (size_t index = 0u;
         index < sizeof cases / sizeof cases[0]; ++index) {
        phy_source_command command;
        size_t error = 0u;
        const phy_status status = phy_source_parse(
            ir, cases[index].source, &command, &error);
        if (status != cases[index].expected) {
            fprintf(stderr, "  foundation case %s (%s/%s): %s\n",
                    cases[index].id, cases[index].area,
                    cases[index].semantic_class, cases[index].source);
        }
        PHY_CHECK_EQ_INT(status, cases[index].expected);
    }

    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_operator_precedence_and_exact_numbers);
    PHY_TEST_CASE(test_promoted_exact_source);
    PHY_TEST_CASE(test_commands_and_functions);
    PHY_TEST_CASE(test_diagnostics_and_bounds);
    PHY_TEST_CASE(test_assignment_and_reserved_heads);
    PHY_TEST_CASE(test_foundation_capability_matrix_is_reserved);
    return PHY_TEST_REPORT("test_source");
}
