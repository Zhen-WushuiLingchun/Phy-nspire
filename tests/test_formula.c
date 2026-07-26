#include <string.h>

#include "phy/formula.h"
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

int main(void)
{
    PHY_TEST_CASE(test_formula_lifecycle_and_metrics);
    PHY_TEST_CASE(test_formula_draws_into_rgb565_surface);
    PHY_TEST_CASE(test_malformed_formula_recovers_locally);
    return PHY_TEST_REPORT("test_formula");
}
