/* Native notebook model, per-cell run badges, and 2D result rendering. */
#include <stdio.h>
#include <string.h>

#include "phy/formula.h"
#include "phy/gfx.h"
#include "phy/notebook.h"
#include "phy/platform.h"
#include "phy_test.h"

#ifndef PHY_FIXTURE_DIR
#define PHY_FIXTURE_DIR "."
#endif
#ifndef PHY_ARTIFACT_DIR
#define PHY_ARTIFACT_DIR "."
#endif

static uint16_t g_pixels[PHY_SCREEN_PIXELS];

static void expect_expression(const phy_notebook *notebook, size_t index,
                              const char *expected)
{
    phy_notebook_cell_view cell;
    PHY_CHECK(phy_notebook_cell(notebook, index, &cell));
    char text[256];
    size_t length = 0u;
    const phy_status status =
        phy_ir_write(phy_notebook_ir(notebook), cell.expression, text,
                     sizeof text, &length);
    PHY_CHECK_EQ_INT(status, PHY_OK);
    PHY_CHECK(length < sizeof text);
    PHY_CHECK_EQ_STR(text, expected);
}

static void test_seeded_cell_model_and_exact_results(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_telemetry before;
    phy_telemetry_get(&before);

    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);
    PHY_CHECK_EQ_INT(phy_notebook_seed_welcome(notebook), PHY_OK);
    PHY_CHECK_EQ_INT(phy_notebook_cell_count(notebook), 5);
    PHY_CHECK_EQ_INT(phy_notebook_selected(notebook), 1);

    const phy_notebook_cell_kind expected_kinds[5] = {
        PHY_NOTEBOOK_CELL_MARKDOWN,
        PHY_NOTEBOOK_CELL_INPUT,
        PHY_NOTEBOOK_CELL_OUTPUT,
        PHY_NOTEBOOK_CELL_INPUT,
        PHY_NOTEBOOK_CELL_OUTPUT,
    };
    for (size_t i = 0u; i < 5u; ++i) {
        phy_notebook_cell_view cell;
        PHY_CHECK(phy_notebook_cell(notebook, i, &cell));
        PHY_CHECK_EQ_INT(cell.kind, expected_kinds[i]);
        PHY_CHECK_EQ_INT(cell.status, PHY_OK);
        PHY_CHECK(!cell.stale);
    }

    expect_expression(notebook, 2u, "(* 2 (fn cos x) (fn sin x))");
    expect_expression(notebook, 4u, "(+ (rat 1 2) (^ x 2))");

    PHY_CHECK_EQ_INT(phy_formula_initialize(), PHY_OK);
    phy_formula_metrics fraction_power;
    phy_notebook_cell_view result;
    PHY_CHECK(phy_notebook_cell(notebook, 4u, &result));
    PHY_CHECK_EQ_INT(
        phy_formula_measure_ir(
            phy_notebook_ir(notebook), result.expression,
            PHY_FORMULA_STYLE_TEXT, 15, 280, &fraction_power),
        PHY_OK);
    PHY_CHECK(fraction_power.ascent + fraction_power.descent >
              PHY_GLYPH_HEIGHT);
    PHY_CHECK(fraction_power.width > 0);

    phy_formula_shutdown();
    phy_notebook_destroy(notebook);
    phy_telemetry after;
    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(after.bytes_live, before.bytes_live);
    phy_platform_shutdown();
}

static void test_selection_and_run_badge_contract(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);
    PHY_CHECK_EQ_INT(phy_notebook_seed_welcome(notebook), PHY_OK);

    /* Markdown is a real selectable cell. */
    PHY_CHECK(phy_notebook_select_at(notebook, 20, 30));
    PHY_CHECK_EQ_INT(phy_notebook_selected(notebook), 0);
    PHY_CHECK_EQ_INT(phy_notebook_activate_selected(notebook),
                     PHY_ERR_UNSUPPORTED);

    /* First input starts at y=59; its RUN badge occupies the upper right. */
    phy_status run_status = PHY_ERR_BACKEND;
    PHY_CHECK(phy_notebook_run_at(notebook, 290, 64, &run_status));
    PHY_CHECK_EQ_INT(run_status, PHY_OK);
    PHY_CHECK_EQ_INT(phy_notebook_selected(notebook), 1);

    /* Card body selects but does not count as the independent run badge. */
    PHY_CHECK(!phy_notebook_run_at(notebook, 80, 75, &run_status));
    PHY_CHECK(!phy_notebook_select_at(notebook, 80, 75));
    PHY_CHECK(phy_notebook_select_next(notebook, 1));
    PHY_CHECK_EQ_INT(phy_notebook_selected(notebook), 2);
    PHY_CHECK(phy_notebook_select_next(notebook, -1));
    PHY_CHECK_EQ_INT(phy_notebook_selected(notebook), 1);

    phy_notebook_destroy(notebook);
    phy_platform_shutdown();
}

static void test_bounded_sources(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);

    char too_long[256];
    memset(too_long, 'x', sizeof too_long - 1u);
    too_long[sizeof too_long - 1u] = '\0';
    PHY_CHECK_EQ_INT(phy_notebook_add_markdown(notebook, too_long, "body", NULL),
                     PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(phy_notebook_cell_count(notebook), 0);

    PHY_CHECK_EQ_INT(phy_notebook_add_input(notebook, NULL, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_notebook_cell_count(notebook), 0);

    phy_notebook_destroy(notebook);
    phy_platform_shutdown();
}

static void test_edit_and_insert_use_reader_source(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);
    PHY_CHECK_EQ_INT(phy_notebook_seed_welcome(notebook), PHY_OK);

    /* The footer +Math button inserts after the selected input/output pair. */
    phy_status status = PHY_ERR_BACKEND;
    PHY_CHECK(phy_notebook_insert_at(notebook, 50, 225, &status));
    PHY_CHECK_EQ_INT(status, PHY_OK);
    PHY_CHECK(phy_notebook_is_editing(notebook));
    PHY_CHECK_EQ_INT(phy_notebook_selected(notebook), 3);
    PHY_CHECK_EQ_INT(phy_notebook_cell_count(notebook), 6);

    const char *source = "1/2+1/3";
    for (const char *cursor = source; *cursor != '\0'; ++cursor) {
        PHY_CHECK(phy_notebook_edit_insert(notebook, *cursor));
    }
    PHY_CHECK_EQ_INT(phy_notebook_activate_selected(notebook), PHY_OK);
    PHY_CHECK(!phy_notebook_is_editing(notebook));
    PHY_CHECK_EQ_INT(phy_notebook_cell_count(notebook), 7);
    expect_expression(notebook, 4u, "(rat 5 6)");

    phy_notebook_cell_view input;
    PHY_CHECK(phy_notebook_cell(notebook, 3u, &input));
    PHY_CHECK_EQ_STR(input.primary, "1/2+1/3");
    PHY_CHECK_EQ_STR(input.secondary,
                     "(+ (* 1 (^ 2 -1)) (* 1 (^ 3 -1)))");

    /* Editing a previous source marks its old output stale until rerun. */
    PHY_CHECK(phy_notebook_begin_edit_at(notebook, 100, 160));
    PHY_CHECK(phy_notebook_edit_backspace(notebook));
    phy_notebook_cell_view output;
    PHY_CHECK(phy_notebook_cell(notebook, 4u, &output));
    PHY_CHECK(output.stale);
    PHY_CHECK(phy_notebook_edit_insert(notebook, '4'));
    PHY_CHECK_EQ_INT(phy_notebook_activate_selected(notebook), PHY_OK);
    PHY_CHECK(phy_notebook_cell(notebook, 4u, &output));
    PHY_CHECK(!output.stale);

    /* +MD is also a real insertion and enters heading edit mode. */
    PHY_CHECK(phy_notebook_insert_at(notebook, 10, 225, &status));
    PHY_CHECK_EQ_INT(status, PHY_OK);
    PHY_CHECK(phy_notebook_is_editing(notebook));
    phy_notebook_cell_view markdown;
    PHY_CHECK(phy_notebook_cell(notebook, phy_notebook_selected(notebook),
                               &markdown));
    PHY_CHECK_EQ_INT(markdown.kind, PHY_NOTEBOOK_CELL_MARKDOWN);
    PHY_CHECK(phy_notebook_edit_switch_field(notebook));
    PHY_CHECK(phy_notebook_edit_insert(notebook, 'N'));
    PHY_CHECK(phy_notebook_edit_insert(notebook, 'o'));
    PHY_CHECK(phy_notebook_edit_insert(notebook, 't'));
    PHY_CHECK(phy_notebook_edit_insert(notebook, 'e'));
    phy_notebook_end_edit(notebook);

    phy_notebook_destroy(notebook);
    phy_platform_shutdown();
}

static void test_compound_power_is_not_algebraically_corrupted(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);

    size_t input_index = 0u;
    PHY_CHECK_EQ_INT(phy_notebook_add_input(
                         notebook, "Simplify[(x+m)^3]", &input_index),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_notebook_evaluate(notebook, input_index), PHY_OK);

    /*
     * The CAS preserves the compound base.  The 2D renderer has a separate
     * regression test proving that the same IR is displayed as (m+x)^3,
     * rather than the misleading m+x^3 seen on hardware.
     */
    expect_expression(notebook, input_index, "(^ (+ m x) 3)");
    expect_expression(notebook, input_index + 1u, "(^ (+ m x) 3)");

    phy_notebook_destroy(notebook);
    phy_platform_shutdown();
}

static void test_template_insertion_and_edit_context(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);

    size_t math = 0u;
    PHY_CHECK_EQ_INT(phy_notebook_add_input(notebook, "", &math), PHY_OK);
    (void)phy_notebook_select(notebook, math);
    PHY_CHECK(phy_notebook_begin_edit_selected(notebook));
    PHY_CHECK_EQ_INT(phy_notebook_edit_target_kind(notebook),
                     PHY_NOTEBOOK_EDIT_MATH);
    PHY_CHECK(phy_notebook_edit_insert_text(notebook, "Simplify[]", 9u));
    PHY_CHECK(phy_notebook_edit_insert(notebook, 'x'));
    phy_notebook_end_edit(notebook);

    phy_notebook_cell_view cell;
    PHY_CHECK(phy_notebook_cell(notebook, math, &cell));
    PHY_CHECK_EQ_STR(cell.primary, "Simplify[x]");

    size_t markdown = 0u;
    PHY_CHECK_EQ_INT(
        phy_notebook_add_markdown(notebook, "Formula", "", &markdown),
        PHY_OK);
    PHY_CHECK(phy_notebook_select(notebook, markdown));
    PHY_CHECK(phy_notebook_begin_edit_selected(notebook));
    PHY_CHECK_EQ_INT(phy_notebook_edit_target_kind(notebook),
                     PHY_NOTEBOOK_EDIT_MARKDOWN_HEADING);
    PHY_CHECK(phy_notebook_edit_switch_field(notebook));
    PHY_CHECK_EQ_INT(phy_notebook_edit_target_kind(notebook),
                     PHY_NOTEBOOK_EDIT_MARKDOWN_BODY);
    PHY_CHECK(phy_notebook_edit_insert_text(notebook, "\\frac{}{}", 6u));
    PHY_CHECK(phy_notebook_edit_insert(notebook, 'x'));
    phy_notebook_end_edit(notebook);
    PHY_CHECK_EQ_INT(phy_notebook_edit_target_kind(notebook),
                     PHY_NOTEBOOK_EDIT_NONE);
    PHY_CHECK(phy_notebook_cell(notebook, markdown, &cell));
    PHY_CHECK_EQ_STR(cell.secondary, "\\frac{x}{}");

    PHY_CHECK(!phy_notebook_edit_insert_text(notebook, "Sin[]", 4u));
    PHY_CHECK(!phy_notebook_edit_insert_text(notebook, NULL, 0u));

    phy_notebook_destroy(notebook);
    phy_platform_shutdown();
}

static void test_markdown_latex_uses_native_typesetter(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);
    PHY_CHECK_EQ_INT(
        phy_notebook_add_markdown(
            notebook, "Einstein equation",
            "GR: $G_{\\mu\\nu}=8\\pi T_{\\mu\\nu}$", NULL),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_notebook_add_markdown(
            notebook, "Curvature scalar",
            "$$R=g^{\\mu\\nu}R_{\\mu\\nu}$$", NULL),
        PHY_OK);
    const phy_surface surface = {
        g_pixels,
        PHY_SCREEN_WIDTH,
        PHY_SCREEN_HEIGHT,
    };

    phy_formula_shutdown();
    memset(g_pixels, 0, sizeof g_pixels);
    phy_notebook_draw(&surface, notebook, -1, -1);
    const uint64_t raw_digest = phy_gfx_digest(&surface);

    PHY_CHECK_EQ_INT(phy_formula_initialize(), PHY_OK);
    memset(g_pixels, 0, sizeof g_pixels);
    phy_notebook_draw(&surface, notebook, -1, -1);
    const uint64_t typeset_digest = phy_gfx_digest(&surface);
    PHY_CHECK(typeset_digest != raw_digest);
    PHY_CHECK(phy_gfx_write_ppm(
        &surface, PHY_ARTIFACT_DIR "/markdown_latex.ppm"));

    phy_formula_shutdown();
    phy_notebook_destroy(notebook);
    phy_platform_shutdown();
}

static void test_notebook_frame_fixture(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);
    PHY_CHECK_EQ_INT(phy_notebook_seed_welcome(notebook), PHY_OK);

    const phy_surface surface = {
        g_pixels,
        PHY_SCREEN_WIDTH,
        PHY_SCREEN_HEIGHT,
    };
    PHY_CHECK_EQ_INT(phy_formula_initialize(), PHY_OK);
    phy_notebook_draw(&surface, notebook, -1, -1);
    const unsigned long long actual =
        (unsigned long long)phy_gfx_digest(&surface);

    const char *path = PHY_FIXTURE_DIR "/notebook_frame.digest";
    FILE *file = fopen(path, "r");
    unsigned long long expected = 0u;
    const int scanned = file != NULL ? fscanf(file, "%llx", &expected) : 0;
    if (file != NULL) {
        fclose(file);
    }
    g_phy_test_checks++;
    if (scanned != 1 || actual != expected) {
        g_phy_test_failures++;
        const char *dump = PHY_ARTIFACT_DIR "/notebook_frame_actual.ppm";
        (void)phy_gfx_write_ppm(&surface, dump);
        fprintf(stderr,
                "FAIL notebook frame: expected %016llx, got %016llx\n"
                "     wrote %s\n",
                expected, actual, dump);
    }

    memset(g_pixels, 0xA5, sizeof g_pixels);
    phy_notebook_draw(&surface, notebook, -1, -1);
    PHY_CHECK((unsigned long long)phy_gfx_digest(&surface) == actual);

    phy_formula_shutdown();
    phy_notebook_destroy(notebook);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_seeded_cell_model_and_exact_results);
    PHY_TEST_CASE(test_selection_and_run_badge_contract);
    PHY_TEST_CASE(test_bounded_sources);
    PHY_TEST_CASE(test_edit_and_insert_use_reader_source);
    PHY_TEST_CASE(test_compound_power_is_not_algebraically_corrupted);
    PHY_TEST_CASE(test_template_insertion_and_edit_context);
    PHY_TEST_CASE(test_markdown_latex_uses_native_typesetter);
    PHY_TEST_CASE(test_notebook_frame_fixture);
    return PHY_TEST_REPORT("test_notebook");
}
