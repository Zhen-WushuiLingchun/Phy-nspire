/*
 * Phase 0 application shell.
 *
 * Draws one deterministic baseline frame and pumps input until the user quits
 * or the frame budget runs out. The drawing code is shared verbatim between
 * the device and host builds so that the golden fixture actually constrains
 * what appears on the calculator.
 */
#include "phy/app.h"

#include <string.h>

#include "phy/platform.h"

#define COLOR_BACKGROUND PHY_RGB565(16, 18, 24)
#define COLOR_PANEL PHY_RGB565(28, 32, 44)
#define COLOR_TITLE_BAR PHY_RGB565(48, 84, 140)
#define COLOR_TEXT PHY_RGB565(232, 236, 244)
#define COLOR_TEXT_DIM PHY_RGB565(150, 160, 180)
#define COLOR_BORDER PHY_RGB565(90, 100, 120)
#define COLOR_POINTER PHY_RGB565(255, 208, 64)

/*
 * Bounded text assembly.
 *
 * snprintf would be the obvious choice, but it drags newlib's full formatter,
 * including floating point, into the device binary: 12.7 KB of a 53 KB
 * program, to print two integers. tools/symbol-report.sh is what surfaced
 * that. These two helpers cost a few dozen bytes instead.
 */
typedef struct {
    char *data;
    size_t capacity;
    size_t length;
} text_builder;

static void text_begin(text_builder *builder, char *storage, size_t capacity)
{
    builder->data = storage;
    builder->capacity = capacity;
    builder->length = 0;
    if (capacity > 0) {
        storage[0] = '\0';
    }
}

static void text_append(text_builder *builder, const char *text)
{
    if (builder->capacity == 0) {
        return;
    }
    while (*text != '\0' && builder->length + 1 < builder->capacity) {
        builder->data[builder->length++] = *text++;
    }
    builder->data[builder->length] = '\0';
}

static void text_append_uint(text_builder *builder, unsigned long value)
{
    /* 20 digits holds any 64-bit value; the terminator is written separately. */
    char digits[20];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + (char)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul && count < sizeof digits);

    if (builder->capacity == 0) {
        return;
    }
    while (count > 0 && builder->length + 1 < builder->capacity) {
        builder->data[builder->length++] = digits[--count];
    }
    builder->data[builder->length] = '\0';
}

void phy_app_options_defaults(phy_app_options *out_options)
{
    if (out_options == NULL) {
        return;
    }
    out_options->max_frames = 0; /* run until quit */
    out_options->idle_sleep_ms = 10;
}

/*
 * Pure red, green and blue bars. On a panel wired for BGR these come out
 * swapped, which is the cheapest way to catch a channel-order mistake on real
 * hardware before any of the notebook rendering is built on top of it.
 */
static void draw_channel_check(const phy_surface *surface, int x, int y,
                               int width, int height)
{
    const uint16_t bars[3] = {
        PHY_RGB565(255, 0, 0),
        PHY_RGB565(0, 255, 0),
        PHY_RGB565(0, 0, 255),
    };
    const int bar_width = width / 3;
    for (int i = 0; i < 3; ++i) {
        /* Last bar absorbs the rounding remainder so the block stays flush. */
        const int this_width = (i == 2) ? (width - 2 * bar_width) : bar_width;
        phy_gfx_fill_rect(surface, x + i * bar_width, y, this_width, height,
                          bars[i]);
    }
    phy_gfx_draw_rect(surface, x, y, width, height, COLOR_BORDER);
}

static void draw_pointer(const phy_surface *surface, int x, int y)
{
    phy_gfx_hline(surface, x - 4, y, 9, COLOR_POINTER);
    phy_gfx_vline(surface, x, y - 4, 9, COLOR_POINTER);
}

void phy_app_draw_baseline(const phy_surface *surface, int pointer_x,
                           int pointer_y)
{
    if (surface == NULL || surface->pixels == NULL) {
        return;
    }
    const int width = surface->width;
    const int height = surface->height;

    phy_gfx_clear(surface, COLOR_BACKGROUND);

    /* Title bar. */
    phy_gfx_fill_rect(surface, 0, 0, width, 16, COLOR_TITLE_BAR);
    phy_gfx_draw_text(surface, 4, 5, "Phy-nspire " PHY_VERSION_STRING, COLOR_TEXT);

    const char *platform = phy_platform_name();
    phy_gfx_draw_text(surface, width - phy_gfx_text_width(platform) - 4, 5,
                      platform, COLOR_TEXT);

    /* Body panel. */
    const int panel_x = 4;
    const int panel_y = 20;
    const int panel_w = width - 8;
    const int panel_h = height - 20 - 16;
    phy_gfx_fill_rect(surface, panel_x, panel_y, panel_w, panel_h, COLOR_PANEL);
    phy_gfx_draw_rect(surface, panel_x, panel_y, panel_w, panel_h, COLOR_BORDER);

    int line_y = panel_y + 6;
    const int text_x = panel_x + 6;

    phy_gfx_draw_text(surface, text_x, line_y, "Phase 0 native baseline",
                      COLOR_TEXT);
    line_y += PHY_TEXT_LINE_HEIGHT + 3;

    char buffer[64];
    text_builder line;

    text_begin(&line, buffer, sizeof buffer);
    text_append(&line, "framebuffer  ");
    text_append_uint(&line, (unsigned long)width);
    text_append(&line, "x");
    text_append_uint(&line, (unsigned long)height);
    text_append(&line, " RGB565");
    phy_gfx_draw_text(surface, text_x, line_y, buffer, COLOR_TEXT_DIM);
    line_y += PHY_TEXT_LINE_HEIGHT;

    text_begin(&line, buffer, sizeof buffer);
    text_append(&line, "backbuffer   ");
    text_append_uint(&line,
                     (unsigned long)((size_t)width * (size_t)height *
                                     sizeof(uint16_t)));
    text_append(&line, " bytes");
    phy_gfx_draw_text(surface, text_x, line_y, buffer, COLOR_TEXT_DIM);
    line_y += PHY_TEXT_LINE_HEIGHT;

    phy_gfx_draw_text(surface, text_x, line_y, "input        keypad + touchpad",
                      COLOR_TEXT_DIM);
    line_y += PHY_TEXT_LINE_HEIGHT + 6;

    phy_gfx_draw_text(surface, text_x, line_y, "channel check R G B:",
                      COLOR_TEXT_DIM);
    draw_channel_check(surface, text_x, line_y + PHY_TEXT_LINE_HEIGHT + 2, 132, 20);

    /* Footer. */
    const char *footer = "ESC quit   touchpad moves pointer";
    phy_gfx_draw_text(surface, 4, height - 11, footer, COLOR_TEXT_DIM);

    if (pointer_x >= 0 && pointer_y >= 0) {
        draw_pointer(surface, pointer_x, pointer_y);
    }
}

phy_status phy_app_run(const phy_app_options *options, phy_app_result *out_result)
{
    if (!phy_platform_is_initialized()) {
        return PHY_ERR_NOT_INITIALIZED;
    }

    phy_app_options effective;
    phy_app_options_defaults(&effective);
    if (options != NULL) {
        effective = *options;
    }

    uint16_t *pixels = phy_display_pixels();
    if (pixels == NULL) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    const phy_surface surface = {pixels, PHY_SCREEN_WIDTH, PHY_SCREEN_HEIGHT};

    phy_app_result result;
    memset(&result, 0, sizeof result);
    result.pointer_x = PHY_SCREEN_WIDTH / 2;
    result.pointer_y = PHY_SCREEN_HEIGHT / 2;

    bool needs_redraw = true;
    bool running = true;

    while (running) {
        if (needs_redraw) {
            phy_app_draw_baseline(&surface, (int)result.pointer_x,
                                  (int)result.pointer_y);
            const phy_status status = phy_display_present();
            if (status != PHY_OK) {
                if (out_result != NULL) {
                    *out_result = result;
                }
                return status;
            }
            result.frames_presented++;
            result.last_frame_digest = phy_gfx_digest(&surface);
            needs_redraw = false;

            if (effective.max_frames != 0 &&
                result.frames_presented >= effective.max_frames) {
                break;
            }
        }

        phy_event event;
        if (!phy_input_poll(&event)) {
            phy_sleep_ms(effective.idle_sleep_ms);
            continue;
        }
        result.events_handled++;

        switch (event.kind) {
        case PHY_EVENT_QUIT:
            result.quit_requested = true;
            running = false;
            break;
        case PHY_EVENT_KEY_DOWN:
            if (event.key == PHY_KEY_ESC) {
                result.quit_requested = true;
                running = false;
            }
            break;
        case PHY_EVENT_POINTER_MOVE:
        case PHY_EVENT_POINTER_DOWN:
        case PHY_EVENT_POINTER_UP:
            if (event.x >= 0 && event.x < PHY_SCREEN_WIDTH && event.y >= 0 &&
                event.y < PHY_SCREEN_HEIGHT) {
                result.pointer_x = (uint32_t)event.x;
                result.pointer_y = (uint32_t)event.y;
                needs_redraw = true;
            }
            break;
        case PHY_EVENT_KEY_UP:
        case PHY_EVENT_NONE:
        default:
            break;
        }
    }

    if (out_result != NULL) {
        *out_result = result;
    }
    return PHY_OK;
}
