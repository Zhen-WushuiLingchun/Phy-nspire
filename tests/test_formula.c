#include <string.h>

#include "phy/formula.h"
#include "phy/platform.h"
#include "phy_test.h"

static uint16_t g_pixels[320u * 240u];

static void test_formula_lifecycle_and_metrics(void)
{
    phy_formula_shutdown();
    PHY_CHECK(!phy_formula_is_ready());
    PHY_CHECK_EQ_INT(phy_formula_initialize(), PHY_OK);
    PHY_CHECK(phy_formula_is_ready());
    /* Initialization is intentionally idempotent for app error paths. */
    PHY_CHECK_EQ_INT(phy_formula_initialize(), PHY_OK);

    static const char source[] =
        "\\frac{1}{2}g_{\\mu\\nu}+\\sqrt{p^\\alpha p_\\alpha}";
    phy_formula_metrics metrics;
    PHY_CHECK_EQ_INT(
        phy_formula_measure_latex(
            source, strlen(source), PHY_FORMULA_STYLE_DISPLAY, 16, 300,
            &metrics),
        PHY_OK);
    PHY_CHECK(metrics.width > 20);
    PHY_CHECK(metrics.ascent > 0);
    PHY_CHECK(metrics.descent >= 0);
    PHY_CHECK(metrics.valid);
    PHY_CHECK(!metrics.overflow);

    phy_formula_shutdown();
    PHY_CHECK(!phy_formula_is_ready());
}

static void test_formula_draws_into_rgb565_surface(void)
{
    PHY_CHECK_EQ_INT(phy_formula_initialize(), PHY_OK);
    memset(g_pixels, 0, sizeof g_pixels);
    const phy_surface surface = {g_pixels, 320, 240};
    static const char source[] =
        "\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}";
    phy_formula_metrics metrics;
    PHY_CHECK_EQ_INT(
        phy_formula_draw_latex(
            &surface, source, strlen(source), PHY_FORMULA_STYLE_DISPLAY, 18,
            280, 16, 70, 0, PHY_COLOR_WHITE, PHY_COLOR_BLACK, 8, 8, 304, 110,
            &metrics),
        PHY_OK);
    PHY_CHECK(metrics.width > 0);
    PHY_CHECK(metrics.ascent > 0);

    size_t lit = 0u;
    for (size_t i = 0u; i < sizeof g_pixels / sizeof g_pixels[0]; ++i) {
        if (g_pixels[i] != 0u) {
            lit++;
        }
    }
    PHY_CHECK(lit > 20u);
    phy_formula_shutdown();
}

static void test_malformed_formula_recovers_locally(void)
{
    PHY_CHECK_EQ_INT(phy_formula_initialize(), PHY_OK);
    static const char source[] = "\\frac{";
    phy_formula_metrics metrics;
    PHY_CHECK_EQ_INT(
        phy_formula_measure_latex(
            source, strlen(source), PHY_FORMULA_STYLE_TEXT, 13, 100, &metrics),
        PHY_OK);
    PHY_CHECK(metrics.width > 0);
    PHY_CHECK(!metrics.valid);
    phy_formula_shutdown();
}

static void test_typed_ir_uses_the_shared_math_tree_pipeline(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);
    PHY_CHECK_EQ_INT(phy_formula_initialize(), PHY_OK);

    const phy_ir_symbol x_name = phy_ir_intern(ir, "x");
    const phy_ir_symbol m_name = phy_ir_intern(ir, "m");
    const phy_ir_symbol g_name = phy_ir_intern(ir, "g");
    const phy_ir_symbol mu_name = phy_ir_intern(ir, "mu");
    const phy_ir_symbol nu_name = phy_ir_intern(ir, "nu");
    const phy_ir_ref x = phy_ir_symbol_ref(ir, x_name);
    const phy_ir_ref m = phy_ir_symbol_ref(ir, m_name);
    const phy_ir_ref sum_terms[] = {x, m};
    const phy_ir_ref compound =
        phy_ir_add(ir, sum_terms, sizeof sum_terms / sizeof sum_terms[0]);
    const phy_ir_ref cube =
        phy_ir_pow(ir, compound, phy_ir_integer(ir, 3));
    const phy_ir_ref indices[] = {
        phy_ir_index(ir, mu_name, PHY_IR_INDEX_LOWER),
        phy_ir_index(ir, nu_name, PHY_IR_INDEX_UPPER),
    };
    const phy_ir_ref metric =
        phy_ir_tensor(ir, g_name, indices,
                      sizeof indices / sizeof indices[0]);
    const phy_ir_ref terms[] = {
        phy_ir_rational(ir, 1, 2),
        cube,
        metric,
    };
    const phy_ir_ref expression =
        phy_ir_add(ir, terms, sizeof terms / sizeof terms[0]);
    PHY_CHECK(expression != PHY_IR_NULL);

    phy_formula_metrics metrics;
    PHY_CHECK_EQ_INT(
        phy_formula_measure_ir(
            ir, expression, PHY_FORMULA_STYLE_DISPLAY, 18, 300, &metrics),
        PHY_OK);
    PHY_CHECK(metrics.valid);
    PHY_CHECK(metrics.width > 30);
    PHY_CHECK(metrics.ascent > 0);
    PHY_CHECK(metrics.descent > 0);

    memset(g_pixels, 0, sizeof g_pixels);
    const phy_surface surface = {g_pixels, 320, 240};
    PHY_CHECK_EQ_INT(
        phy_formula_draw_ir(
            &surface, ir, expression, PHY_FORMULA_STYLE_DISPLAY, 18, 300,
            10, 80, 0, PHY_COLOR_WHITE, PHY_COLOR_BLACK, 2, 2, 316, 120,
            &metrics),
        PHY_OK);
    size_t lit = 0u;
    for (size_t index = 0u;
         index < sizeof g_pixels / sizeof g_pixels[0]; ++index) {
        if (g_pixels[index] != 0u) {
            lit++;
        }
    }
    PHY_CHECK(lit > 40u);

    phy_formula_shutdown();
    PHY_CHECK_EQ_INT(
        phy_formula_measure_ir(
            ir, expression, PHY_FORMULA_STYLE_TEXT, 15, 200, &metrics),
        PHY_ERR_NOT_INITIALIZED);
    PHY_CHECK_EQ_INT(
        phy_formula_measure_ir(
            NULL, expression, PHY_FORMULA_STYLE_TEXT, 15, 200, &metrics),
        PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_formula_lifecycle_and_metrics);
    PHY_TEST_CASE(test_formula_draws_into_rgb565_surface);
    PHY_TEST_CASE(test_malformed_formula_recovers_locally);
    PHY_TEST_CASE(test_typed_ir_uses_the_shared_math_tree_pipeline);
    return PHY_TEST_REPORT("test_formula");
}
