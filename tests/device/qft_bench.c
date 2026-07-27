/*
 * Q-7 real-device benchmark for the bounded Dirac trace engine.
 *
 * This is a separate acceptance binary, not a benchmark mode hidden in the
 * notebook.  It times one cold trace operation with the CX II's 32 kHz SP804
 * timer and reports the allocation high-water mark observed through
 * phy_alloc().  The latter is deliberately labelled "tracked heap": Ndless
 * does not expose process RSS, so reporting it as RSS would be false precision.
 *
 * The timer sequence is the same hardware and save/restore discipline used by
 * Ndless's own msleep().  It is CX II-only: classic hardware has a different
 * register layout and is rejected rather than measured incorrectly.
 */
#include <libndls.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/dirac.h"
#include "phy/gfx.h"
#include "phy/platform.h"

#define COLOR_BACKGROUND PHY_RGB565(13, 17, 23)
#define COLOR_PANEL PHY_RGB565(25, 31, 42)
#define COLOR_TITLE PHY_RGB565(38, 78, 126)
#define COLOR_TEXT PHY_RGB565(235, 240, 247)
#define COLOR_DIM PHY_RGB565(159, 174, 194)
#define COLOR_PASS PHY_RGB565(78, 214, 125)
#define COLOR_FAIL PHY_RGB565(244, 91, 105)
#define COLOR_BORDER PHY_RGB565(72, 88, 110)

#define SP804_BASE 0x900D0000u
#define SP804_LOAD 0u
#define SP804_VALUE 1u
#define SP804_CONTROL 2u
#define SP804_INT_CLEAR 3u
#define SP804_ENABLE_32BIT_ONESHOT 0x83u

#define BENCH_CASE_COUNT 4u
#define MAX_CHAIN 12u

typedef struct {
    volatile uint32_t *registers;
    uint32_t saved_load;
    uint32_t saved_control;
    uint32_t start_value;
    bool active;
} cx2_timer;

typedef struct {
    const char *name;
    uint32_t gamma_count;
    bool contracted_pairs;
    bool deliberately_over_limit;
    phy_status status;
    uint32_t ticks;
    uint32_t generated_terms;
    size_t tracked_peak_delta;
    bool passed;
} bench_case;

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_lorentz_metric *metric;
    phy_dirac *dirac;
} fixture;

static bool timer_begin(cx2_timer *timer)
{
    if (timer == NULL || is_classic) {
        return false;
    }
    timer->registers = (volatile uint32_t *)SP804_BASE;
    timer->saved_load = timer->registers[SP804_LOAD];
    timer->saved_control = timer->registers[SP804_CONTROL];

    timer->registers[SP804_CONTROL] = 0u;
    timer->registers[SP804_INT_CLEAR] = 1u;
    timer->registers[SP804_LOAD] = UINT32_MAX;
    timer->registers[SP804_CONTROL] = SP804_ENABLE_32BIT_ONESHOT;
    timer->start_value = timer->registers[SP804_VALUE];
    timer->active = true;
    return true;
}

static uint32_t timer_end(cx2_timer *timer)
{
    if (timer == NULL || !timer->active) {
        return 0u;
    }
    const uint32_t stop_value = timer->registers[SP804_VALUE];
    timer->registers[SP804_CONTROL] = 0u;
    timer->registers[SP804_INT_CLEAR] = 1u;
    timer->registers[SP804_LOAD] = timer->saved_load;
    timer->registers[SP804_CONTROL] = timer->saved_control;
    timer->active = false;
    return timer->start_value - stop_value;
}

static bool fixture_open(const phy_dirac_limits *limits, fixture *out)
{
    if (out == NULL) {
        return false;
    }
    out->ir = phy_ir_context_create(NULL);
    out->cas = out->ir != NULL ? phy_cas_create(out->ir, NULL) : NULL;
    out->metric = NULL;
    out->dirac = NULL;
    if (out->cas == NULL ||
        phy_lorentz_metric_create(out->cas, "g", "Lorentz", 4u,
                                  PHY_LORENTZ_MOSTLY_MINUS,
                                  &out->metric) != PHY_OK ||
        phy_dirac_create(out->metric, limits, &out->dirac) != PHY_OK) {
        return false;
    }
    return true;
}

static bool fixture_valid(const fixture *f)
{
    return f != NULL && f->ir != NULL && f->cas != NULL &&
           phy_ir_validate(f->ir) == PHY_OK &&
           phy_cas_validate(f->cas) == PHY_OK;
}

static void fixture_close(fixture *f)
{
    if (f == NULL) {
        return;
    }
    phy_dirac_destroy(f->dirac);
    phy_lorentz_metric_destroy(f->metric);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
    f->dirac = NULL;
    f->metric = NULL;
    f->cas = NULL;
    f->ir = NULL;
}

static bool build_index_name(char out[4], uint32_t ordinal)
{
    if (out == NULL || ordinal >= 100u) {
        return false;
    }
    out[0] = 'i';
    if (ordinal >= 10u) {
        out[1] = (char)('0' + ordinal / 10u);
        out[2] = (char)('0' + ordinal % 10u);
        out[3] = '\0';
    } else {
        out[1] = (char)('0' + ordinal);
        out[2] = '\0';
        out[3] = '\0';
    }
    return true;
}

static phy_dirac_expr *build_chain(fixture *f, uint32_t count,
                                   bool contracted_pairs)
{
    if (f == NULL || count > MAX_CHAIN ||
        (contracted_pairs && (count & 1u) != 0u)) {
        return NULL;
    }
    phy_dirac_expr *chain = NULL;
    if (phy_dirac_identity(f->dirac, 1u, &chain) != PHY_OK) {
        return NULL;
    }

    for (uint32_t slot = 0u; slot < count; ++slot) {
        char name[4];
        const uint32_t ordinal = contracted_pairs ? slot / 2u : slot;
        if (!build_index_name(name, ordinal)) {
            phy_dirac_expr_destroy(chain);
            return NULL;
        }
        const phy_ir_variance variance =
            contracted_pairs && (slot & 1u) != 0u
                ? PHY_IR_INDEX_LOWER
                : PHY_IR_INDEX_UPPER;
        phy_ir_ref index = PHY_IR_NULL;
        phy_dirac_expr *gamma = NULL;
        phy_dirac_expr *product = NULL;
        if (phy_lorentz_index(f->metric, name, variance, &index) != PHY_OK ||
            phy_dirac_gamma(f->dirac, 1u, index, &gamma) != PHY_OK ||
            phy_dirac_mul(chain, gamma, &product) != PHY_OK) {
            phy_dirac_expr_destroy(product);
            phy_dirac_expr_destroy(gamma);
            phy_dirac_expr_destroy(chain);
            return NULL;
        }
        phy_dirac_expr_destroy(gamma);
        phy_dirac_expr_destroy(chain);
        chain = product;
    }
    return chain;
}

static bool verify_paired_twelve(fixture *f, phy_ir_ref output)
{
    phy_ir_ref expected = PHY_IR_NULL;
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    return phy_cas_number(f->cas, 16384, 1, &expected) == PHY_OK &&
           phy_cas_equivalent(f->cas, output, expected, &decision) == PHY_OK &&
           decision == PHY_CAS_ZERO;
}

static bool verify_engine_recovers(fixture *f)
{
    phy_dirac_expr *identity = NULL;
    phy_ir_ref output = PHY_IR_NULL;
    phy_ir_ref expected = PHY_IR_NULL;
    bool passed =
        phy_dirac_identity(f->dirac, 1u, &identity) == PHY_OK &&
        phy_dirac_trace_scalar(identity, 1u, &output) == PHY_OK &&
        phy_cas_number(f->cas, 4, 1, &expected) == PHY_OK &&
        output == expected;
    phy_dirac_expr_destroy(identity);
    return passed;
}

static void run_case(bench_case *test)
{
    phy_dirac_limits limits;
    phy_dirac_limits_defaults(&limits);
    if (test->deliberately_over_limit) {
        limits.max_terms = 2u;
    }

    fixture f = {0};
    phy_dirac_expr *chain = NULL;
    phy_ir_ref output = PHY_IR_NULL;
    phy_telemetry before = {0};
    phy_telemetry after = {0};
    cx2_timer timer = {0};

    if (!fixture_open(&limits, &f) ||
        (chain = build_chain(&f, test->gamma_count,
                             test->contracted_pairs)) == NULL) {
        test->status = PHY_ERR_OUT_OF_MEMORY;
        fixture_close(&f);
        return;
    }

    phy_telemetry_reset_peak();
    phy_telemetry_get(&before);
    if (!timer_begin(&timer)) {
        test->status = PHY_ERR_UNSUPPORTED;
        phy_dirac_expr_destroy(chain);
        fixture_close(&f);
        return;
    }
    test->status = phy_dirac_trace_scalar(chain, 1u, &output);
    test->ticks = timer_end(&timer);
    phy_telemetry_get(&after);
    test->tracked_peak_delta =
        after.bytes_peak >= before.bytes_live
            ? after.bytes_peak - before.bytes_live
            : 0u;
    test->generated_terms = phy_dirac_generated_terms(f.dirac);

    if (test->deliberately_over_limit) {
        test->passed =
            test->status == PHY_ERR_TERM_LIMIT && output == PHY_IR_NULL &&
            verify_engine_recovers(&f) && fixture_valid(&f);
    } else {
        const bool expected_terms =
            (test->gamma_count == 4u && test->generated_terms == 3u) ||
            (test->gamma_count == 8u && test->generated_terms == 105u) ||
            (test->gamma_count == 12u && test->generated_terms > 0u);
        const bool expected_value =
            test->gamma_count != 12u || verify_paired_twelve(&f, output);
        test->passed = test->status == PHY_OK && output != PHY_IR_NULL &&
                       expected_terms && expected_value && fixture_valid(&f);
    }

    phy_dirac_expr_destroy(chain);
    fixture_close(&f);
}

static char *append_text(char *cursor, const char *end, const char *text)
{
    while (cursor < end && text != NULL && *text != '\0') {
        *cursor++ = *text++;
    }
    return cursor;
}

static char *append_u32(char *cursor, const char *end, uint32_t value)
{
    char reversed[10];
    size_t count = 0u;
    do {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u && count < sizeof reversed);
    while (count > 0u && cursor < end) {
        *cursor++ = reversed[--count];
    }
    return cursor;
}

static void format_timing(const bench_case *test, char out[64])
{
    char *cursor = out;
    const char *const end = out + 63;
    const uint32_t milliseconds = (test->ticks + 16u) / 32u;
    cursor = append_text(cursor, end, "time ");
    cursor = append_u32(cursor, end, milliseconds);
    cursor = append_text(cursor, end, " ms  heap+ ");
    cursor = append_u32(cursor, end, (uint32_t)test->tracked_peak_delta);
    cursor = append_text(cursor, end, " B");
    *cursor = '\0';
}

static void format_terms(const bench_case *test, char out[64])
{
    char *cursor = out;
    const char *const end = out + 63;
    cursor = append_text(cursor, end, "ticks ");
    cursor = append_u32(cursor, end, test->ticks);
    cursor = append_text(cursor, end, "  terms ");
    cursor = append_u32(cursor, end, test->generated_terms);
    *cursor = '\0';
}

static phy_status draw_results(const bench_case cases[BENCH_CASE_COUNT],
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
    phy_gfx_draw_text(&surface, 5, 5, "Phy-nspire QFT Q-7 device bench",
                      COLOR_TEXT);

    phy_gfx_fill_rect(&surface, 4, 21, PHY_SCREEN_WIDTH - 8,
                      PHY_SCREEN_HEIGHT - 38, COLOR_PANEL);
    phy_gfx_draw_rect(&surface, 4, 21, PHY_SCREEN_WIDTH - 8,
                      PHY_SCREEN_HEIGHT - 38, COLOR_BORDER);

    int y = 27;
    for (size_t i = 0u; i < BENCH_CASE_COUNT; ++i) {
        char timing[64];
        char terms[64];
        format_timing(&cases[i], timing);
        format_terms(&cases[i], terms);
        phy_gfx_draw_text(&surface, 10, y,
                          cases[i].passed ? "PASS" : "FAIL",
                          cases[i].passed ? COLOR_PASS : COLOR_FAIL);
        phy_gfx_draw_text(&surface, 43, y, cases[i].name, COLOR_TEXT);
        phy_gfx_draw_text(&surface, 43, y + PHY_TEXT_LINE_HEIGHT,
                          timing, COLOR_DIM);
        phy_gfx_draw_text(&surface, 43, y + 2 * PHY_TEXT_LINE_HEIGHT,
                          terms, COLOR_DIM);
        y += 42;
    }

    phy_gfx_draw_text(&surface, 9, 199, "heap = tracked phy_alloc, not OS RSS",
                      COLOR_DIM);
    phy_gfx_draw_text(
        &surface, 5, PHY_SCREEN_HEIGHT - 12,
        all_passed ? "4/4 PASS - photograph this screen - ESC exits"
                   : "FAIL - photograph red row - ESC exits",
        all_passed ? COLOR_PASS : COLOR_FAIL);
    return phy_display_present();
}

static void wait_for_exit(void)
{
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
        show_msgbox("Phy-nspire QFT", "Framebuffer initialization failed.");
        return 1;
    }

    bench_case cases[BENCH_CASE_COUNT] = {
        {.name = "4 gamma, distinct",
         .gamma_count = 4u,
         .contracted_pairs = false},
        {.name = "8 gamma, distinct",
         .gamma_count = 8u,
         .contracted_pairs = false},
        {.name = "12 gamma, 6 pairs",
         .gamma_count = 12u,
         .contracted_pairs = true},
        {.name = "4 gamma, max_terms=2",
         .gamma_count = 4u,
         .contracted_pairs = false,
         .deliberately_over_limit = true},
    };

    bool all_passed = !is_classic;
    for (size_t i = 0u; i < BENCH_CASE_COUNT; ++i) {
        run_case(&cases[i]);
        all_passed = all_passed && cases[i].passed;
    }

    const phy_status display_status = draw_results(cases, all_passed);
    if (display_status != PHY_OK) {
        phy_platform_shutdown();
        show_msgbox("Phy-nspire QFT", "Could not present benchmark results.");
        return 3;
    }
    wait_for_exit();
    phy_platform_shutdown();
    return all_passed ? 0 : 2;
}
