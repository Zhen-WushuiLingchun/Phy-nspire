/*
 * Observable on-device acceptance test for the native symbolic scalar CAS.
 *
 * This is deliberately a separate .tns, not the application entry point.
 * It evaluates representative exact expressions, renders a readable summary,
 * and waits for an explicit key before restoring the TI display mode.
 */
#include <libndls.h>

#include <stdbool.h>
#include <stddef.h>

#include "phy/cas.h"
#include "phy/gfx.h"
#include "phy/ir.h"
#include "phy/platform.h"

#define COLOR_BACKGROUND PHY_RGB565(13, 17, 23)
#define COLOR_PANEL PHY_RGB565(25, 31, 42)
#define COLOR_TITLE PHY_RGB565(38, 78, 126)
#define COLOR_TEXT PHY_RGB565(235, 240, 247)
#define COLOR_DIM PHY_RGB565(159, 174, 194)
#define COLOR_PASS PHY_RGB565(78, 214, 125)
#define COLOR_FAIL PHY_RGB565(244, 91, 105)
#define COLOR_BORDER PHY_RGB565(72, 88, 110)

#define SMOKE_CASE_COUNT 7u

typedef struct {
    const char *input;
    const char *output;
    bool passed;
} smoke_case;

static bool parse(phy_ir_context *ir, const char *text, phy_ir_ref *out)
{
    size_t error_offset = 0u;
    return phy_ir_read(ir, text, out, &error_offset) == PHY_OK;
}

static bool matches(phy_ir_context *ir, phy_ir_ref actual,
                    const char *expected_text)
{
    phy_ir_ref expected = PHY_IR_NULL;
    return parse(ir, expected_text, &expected) &&
           phy_ir_equal(actual, expected);
}

static bool simplify_matches(phy_cas *cas, phy_ir_context *ir,
                             const char *input, const char *expected)
{
    phy_ir_ref expression = PHY_IR_NULL;
    phy_ir_ref result = PHY_IR_NULL;
    return parse(ir, input, &expression) &&
           phy_cas_simplify(cas, expression, &result) == PHY_OK &&
           matches(ir, result, expected);
}

static bool expand_matches(phy_cas *cas, phy_ir_context *ir,
                           const char *input, const char *expected)
{
    phy_ir_ref expression = PHY_IR_NULL;
    phy_ir_ref result = PHY_IR_NULL;
    return parse(ir, input, &expression) &&
           phy_cas_expand(cas, expression, &result) == PHY_OK &&
           matches(ir, result, expected);
}

static bool derivative_matches(phy_cas *cas, phy_ir_context *ir,
                               const char *input, const char *variable,
                               const char *expected)
{
    phy_ir_ref expression = PHY_IR_NULL;
    phy_ir_ref var = PHY_IR_NULL;
    phy_ir_ref result = PHY_IR_NULL;
    return parse(ir, input, &expression) && parse(ir, variable, &var) &&
           phy_cas_diff(cas, expression, var, &result) == PHY_OK &&
           matches(ir, result, expected);
}

static bool zero_decision_is(phy_cas *cas, phy_ir_context *ir,
                             const char *input,
                             phy_cas_decision expected_decision)
{
    phy_ir_ref expression = PHY_IR_NULL;
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    return parse(ir, input, &expression) &&
           phy_cas_is_zero(cas, expression, &decision) == PHY_OK &&
           decision == expected_decision;
}

static bool equivalent_is_zero(phy_cas *cas, phy_ir_context *ir,
                               const char *left_text, const char *right_text)
{
    phy_ir_ref left = PHY_IR_NULL;
    phy_ir_ref right = PHY_IR_NULL;
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    return parse(ir, left_text, &left) && parse(ir, right_text, &right) &&
           phy_cas_equivalent(cas, left, right, &decision) == PHY_OK &&
           decision == PHY_CAS_ZERO;
}

static bool run_cases(smoke_case cases[SMOKE_CASE_COUNT])
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    if (ir == NULL) {
        return false;
    }
    phy_cas *cas = phy_cas_create(ir, NULL);
    if (cas == NULL) {
        phy_ir_context_destroy(ir);
        return false;
    }

    cases[0].passed =
        simplify_matches(cas, ir, "(+ (* 2 x) (* 3 x))", "(* 5 x)");
    cases[1].passed = simplify_matches(
        cas, ir, "(+ (rat 1 2) (rat 1 3))", "(rat 5 6)");
    cases[2].passed =
        derivative_matches(cas, ir, "(fn sin x)", "x", "(fn cos x)");
    cases[3].passed = expand_matches(
        cas, ir, "(^ (+ x 1) 2)", "(+ 1 (* 2 x) (^ x 2))");
    cases[4].passed = zero_decision_is(
        cas, ir, "(+ (^ (fn sin u) 2) (^ (fn cos u) 2) -1)", PHY_CAS_ZERO);
    cases[5].passed = equivalent_is_zero(
        cas, ir, "(* (rat -1 2) (fn sin (* 2 theta)))",
        "(* -1 (fn sin theta) (fn cos theta))");
    cases[6].passed = zero_decision_is(
        cas, ir, "(real 0x3ff0000000000000)", PHY_CAS_UNKNOWN);

    bool all_passed =
        phy_cas_validate(cas) == PHY_OK && phy_ir_validate(ir) == PHY_OK;
    for (size_t i = 0u; i < SMOKE_CASE_COUNT; ++i) {
        all_passed = all_passed && cases[i].passed;
    }

    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
    return all_passed;
}

static phy_status draw_results(const smoke_case cases[SMOKE_CASE_COUNT],
                               bool all_passed)
{
    uint16_t *pixels = phy_display_pixels();
    if (pixels == NULL) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    const phy_surface surface = {
        pixels,
        PHY_SCREEN_WIDTH,
        PHY_SCREEN_HEIGHT,
    };

    phy_gfx_clear(&surface, COLOR_BACKGROUND);
    phy_gfx_fill_rect(&surface, 0, 0, PHY_SCREEN_WIDTH, 17, COLOR_TITLE);
    phy_gfx_draw_text(&surface, 5, 5, "Phy-nspire CAS hardware smoke",
                      COLOR_TEXT);

    phy_gfx_fill_rect(&surface, 4, 21, PHY_SCREEN_WIDTH - 8,
                      PHY_SCREEN_HEIGHT - 38, COLOR_PANEL);
    phy_gfx_draw_rect(&surface, 4, 21, PHY_SCREEN_WIDTH - 8,
                      PHY_SCREEN_HEIGHT - 38, COLOR_BORDER);

    int y = 27;
    for (size_t i = 0u; i < SMOKE_CASE_COUNT; ++i) {
        const uint16_t color = cases[i].passed ? COLOR_PASS : COLOR_FAIL;
        phy_gfx_draw_text(&surface, 10, y, cases[i].passed ? "PASS" : "FAIL",
                          color);
        phy_gfx_draw_text(&surface, 43, y, cases[i].input, COLOR_TEXT);
        phy_gfx_draw_text(&surface, 43, y + PHY_TEXT_LINE_HEIGHT,
                          cases[i].output,
                          cases[i].passed ? COLOR_DIM : COLOR_FAIL);
        y += 25;
    }

    phy_gfx_draw_text(
        &surface, 5, PHY_SCREEN_HEIGHT - 12,
        all_passed ? "7/7 PASS - exact symbolic CAS - ESC/ENTER exits"
                   : "FAIL - a red row did not match - ESC/ENTER exits",
        all_passed ? COLOR_PASS : COLOR_FAIL);
    return phy_display_present();
}

static void wait_for_exit(void)
{
    /*
     * Let the key that opened the document be released before accepting an
     * exit key, otherwise an ENTER launch can close the result immediately.
     */
    phy_sleep_ms(300u);
    phy_event event;
    while (phy_input_poll(&event)) {
    }

    for (;;) {
        if (!phy_input_poll(&event)) {
            phy_sleep_ms(10u);
            continue;
        }
        if (event.kind == PHY_EVENT_KEY_DOWN &&
            (event.key == PHY_KEY_ESC || event.key == PHY_KEY_ENTER)) {
            return;
        }
    }
}

int main(void)
{
    assert_ndless_rev(2022);

    if (phy_platform_init() != PHY_OK) {
        show_msgbox("Phy-nspire CAS", "Framebuffer initialization failed.");
        return 1;
    }

    smoke_case cases[SMOKE_CASE_COUNT] = {
        {.input = "Simplify[2*x + 3*x]", .output = "= 5*x", .passed = false},
        {.input = "1/2 + 1/3",
         .output = "= 5/6 (exact)",
         .passed = false},
        {.input = "D[Sin[x], x]", .output = "= Cos[x]", .passed = false},
        {.input = "Expand[(x+1)^2]",
         .output = "= 1 + 2*x + x^2",
         .passed = false},
        {.input = "Sin[u]^2 + Cos[u]^2 - 1",
         .output = "= 0 (proved exactly)",
         .passed = false},
        {.input = "Sphere Gamma identity",
         .output = "= True (proved exactly)",
         .passed = false},
        {.input = "IsZero[Real[1.0]]",
         .output = "= Unknown (not guessed numerically)",
         .passed = false},
    };

    const bool all_passed = run_cases(cases);
    const phy_status display_status = draw_results(cases, all_passed);
    if (display_status != PHY_OK) {
        phy_platform_shutdown();
        show_msgbox("Phy-nspire CAS", "Could not present the result screen.");
        return 3;
    }
    wait_for_exit();
    phy_platform_shutdown();
    return all_passed ? 0 : 2;
}
