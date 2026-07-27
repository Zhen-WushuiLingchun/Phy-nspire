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
    command = parse(ir, "Numerator[x/y]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_NUMERATOR);
    command = parse(ir, "Denominator[x/y]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_DENOMINATOR);
    command = parse(ir, "FullSimplify[x+x]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_SIMPLIFY);

    command = parse(ir, "D[x y, x, y]");
    PHY_CHECK_EQ_INT(command.variable_count, 2);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(* x y)");
    command = parse(ir, "Integrate[Sin[2x], x]");
    PHY_CHECK_EQ_INT(command.operation, PHY_SOURCE_INTEGRATE);
    PHY_CHECK_EQ_INT(command.variable_count, 1);
    PHY_CHECK_EQ_STR(render(ir, command.expression), "(fn sin (* 2 x))");

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
    PHY_CHECK_EQ_INT(phy_source_parse(ir, "Factor[x^2-1]", &command, &error),
                     PHY_ERR_UNSUPPORTED);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Sin[Factor[x]]", &command, &error),
        PHY_ERR_UNSUPPORTED);
    PHY_CHECK_EQ_INT(
        phy_source_parse(ir, "Commutator[A]", &command, &error),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_source_parse(NULL, "x", &command, &error),
                     PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_operator_precedence_and_exact_numbers);
    PHY_TEST_CASE(test_commands_and_functions);
    PHY_TEST_CASE(test_diagnostics_and_bounds);
    return PHY_TEST_REPORT("test_source");
}
