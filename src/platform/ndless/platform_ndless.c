/*
 * Ndless platform backend for the TI-Nspire CX II CAS.
 *
 * Owns the framebuffer mode switch, an off-screen back buffer, keypad and
 * touchpad sampling, and the restore-on-exit path. Nothing above this file
 * knows the CX II keypad matrix or the panel mode.
 *
 * Display contract: lcd_init(SCR_320x240_565) switches the panel, and
 * lcd_init(SCR_TYPE_INVALID) hands it back in the mode the OS expects. Both
 * are required for "launch/exit without display corruption"; skipping the
 * second leaves the TI shell drawing into the wrong format.
 */
#include <libndls.h>
#include <stdlib.h>
#include <string.h>

#include "phy/platform.h"

#define EVENT_QUEUE_CAPACITY 32

typedef struct {
    const t_key *key;
    phy_key semantic;
} key_binding;

typedef struct {
    bool initialized;
    bool display_switched;

    uint16_t *backbuffer;

    phy_event queue[EVENT_QUEUE_CAPACITY];
    unsigned queue_head;
    unsigned queue_count;

    /* Debounce state: previous sample for each bound key. */
    bool key_state[10];

    bool pointer_pressed;
    int16_t pointer_x;
    int16_t pointer_y;

    uint32_t clock_ms;
    phy_telemetry telemetry;
} ndless_state;

static ndless_state g_ndless;

/*
 * Bound in the order used by key_state[]. Keep the two in sync: adding a key
 * means growing key_state.
 */
static const key_binding kKeyBindings[] = {
    {&KEY_NSPIRE_ESC, PHY_KEY_ESC},     {&KEY_NSPIRE_ENTER, PHY_KEY_ENTER},
    {&KEY_NSPIRE_TAB, PHY_KEY_TAB},     {&KEY_NSPIRE_MENU, PHY_KEY_MENU},
    {&KEY_NSPIRE_UP, PHY_KEY_UP},       {&KEY_NSPIRE_DOWN, PHY_KEY_DOWN},
    {&KEY_NSPIRE_LEFT, PHY_KEY_LEFT},   {&KEY_NSPIRE_RIGHT, PHY_KEY_RIGHT},
};

#define KEY_BINDING_COUNT ((int)(sizeof kKeyBindings / sizeof kKeyBindings[0]))

const char *phy_platform_name(void)
{
    return "ndless";
}

static bool queue_push(const phy_event *event)
{
    if (g_ndless.queue_count >= EVENT_QUEUE_CAPACITY) {
        return false; /* Dropping input is preferable to blocking the UI. */
    }
    const unsigned tail =
        (g_ndless.queue_head + g_ndless.queue_count) % EVENT_QUEUE_CAPACITY;
    g_ndless.queue[tail] = *event;
    g_ndless.queue_count++;
    return true;
}

static void push_key(phy_event_kind kind, phy_key key)
{
    const phy_event event = {kind, key, 0, 0};
    queue_push(&event);
}

static void push_pointer(phy_event_kind kind, int16_t x, int16_t y)
{
    const phy_event event = {kind, PHY_KEY_NONE, x, y};
    queue_push(&event);
}

phy_status phy_platform_init(void)
{
    if (g_ndless.initialized) {
        return PHY_ERR_ALREADY_INITIALIZED;
    }
    memset(&g_ndless, 0, sizeof g_ndless);

    g_ndless.backbuffer = phy_alloc(PHY_SCREEN_BYTES);
    if (g_ndless.backbuffer == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(g_ndless.backbuffer, 0, PHY_SCREEN_BYTES);

    if (!lcd_init(SCR_320x240_565)) {
        phy_free(g_ndless.backbuffer, PHY_SCREEN_BYTES);
        g_ndless.backbuffer = NULL;
        return PHY_ERR_DISPLAY_UNSUPPORTED;
    }
    g_ndless.display_switched = true;

    g_ndless.pointer_x = PHY_SCREEN_WIDTH / 2;
    g_ndless.pointer_y = PHY_SCREEN_HEIGHT / 2;
    g_ndless.initialized = true;
    return PHY_OK;
}

void phy_platform_shutdown(void)
{
    if (!g_ndless.initialized) {
        return; /* idempotent */
    }
    if (g_ndless.display_switched) {
        /* Blank first so the restored mode never shows our stale pixels. */
        if (g_ndless.backbuffer != NULL) {
            memset(g_ndless.backbuffer, 0, PHY_SCREEN_BYTES);
            lcd_blit(g_ndless.backbuffer, SCR_320x240_565);
        }
        lcd_init(SCR_TYPE_INVALID);
        g_ndless.display_switched = false;
    }
    if (g_ndless.backbuffer != NULL) {
        phy_free(g_ndless.backbuffer, PHY_SCREEN_BYTES);
        g_ndless.backbuffer = NULL;
    }
    g_ndless.initialized = false;
    g_ndless.queue_head = 0;
    g_ndless.queue_count = 0;
}

bool phy_platform_is_initialized(void)
{
    return g_ndless.initialized;
}

uint16_t *phy_display_pixels(void)
{
    return g_ndless.initialized ? g_ndless.backbuffer : NULL;
}

phy_status phy_display_present(void)
{
    if (!g_ndless.initialized || g_ndless.backbuffer == NULL) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    lcd_blit(g_ndless.backbuffer, SCR_320x240_565);
    g_ndless.telemetry.frames_presented++;
    return PHY_OK;
}

static void sample_keys(void)
{
    for (int i = 0; i < KEY_BINDING_COUNT; ++i) {
        /* isKeyPressed is a macro that takes the address of its argument. */
        const bool down = isKeyPressed(*kKeyBindings[i].key) ? true : false;
        if (down != g_ndless.key_state[i]) {
            g_ndless.key_state[i] = down;
            push_key(down ? PHY_EVENT_KEY_DOWN : PHY_EVENT_KEY_UP,
                     kKeyBindings[i].semantic);
        }
    }
}

static void sample_touchpad(void)
{
    if (!is_touchpad) {
        return;
    }
    touchpad_report_t report;
    if (touchpad_scan(&report) != 0) {
        return;
    }

    const touchpad_info_t *info = touchpad_getinfo();
    if (info == NULL || info->width == 0 || info->height == 0) {
        return;
    }

    const bool contact = report.contact ? true : false;
    if (contact) {
        /* Touchpad origin is bottom-left; the framebuffer origin is top-left. */
        int32_t x = (int32_t)report.x * (PHY_SCREEN_WIDTH - 1) / info->width;
        int32_t y = (PHY_SCREEN_HEIGHT - 1) -
                    (int32_t)report.y * (PHY_SCREEN_HEIGHT - 1) / info->height;
        if (x < 0) {
            x = 0;
        } else if (x > PHY_SCREEN_WIDTH - 1) {
            x = PHY_SCREEN_WIDTH - 1;
        }
        if (y < 0) {
            y = 0;
        } else if (y > PHY_SCREEN_HEIGHT - 1) {
            y = PHY_SCREEN_HEIGHT - 1;
        }
        if ((int16_t)x != g_ndless.pointer_x || (int16_t)y != g_ndless.pointer_y) {
            g_ndless.pointer_x = (int16_t)x;
            g_ndless.pointer_y = (int16_t)y;
            push_pointer(PHY_EVENT_POINTER_MOVE, g_ndless.pointer_x,
                         g_ndless.pointer_y);
        }
    }

    const bool pressed = report.pressed ? true : false;
    if (pressed != g_ndless.pointer_pressed) {
        g_ndless.pointer_pressed = pressed;
        push_pointer(pressed ? PHY_EVENT_POINTER_DOWN : PHY_EVENT_POINTER_UP,
                     g_ndless.pointer_x, g_ndless.pointer_y);
    }
}

bool phy_input_poll(phy_event *out_event)
{
    if (!g_ndless.initialized || out_event == NULL) {
        return false;
    }
    if (g_ndless.queue_count == 0) {
        sample_keys();
        sample_touchpad();
    }
    if (g_ndless.queue_count == 0) {
        return false;
    }
    *out_event = g_ndless.queue[g_ndless.queue_head];
    g_ndless.queue_head = (g_ndless.queue_head + 1) % EVENT_QUEUE_CAPACITY;
    g_ndless.queue_count--;
    g_ndless.telemetry.events_dispatched++;
    return true;
}

/*
 * Ndless exposes no wall-clock syscall, so this counts the time we explicitly
 * yield. That is sufficient for Phase 0 idle pacing. The wall-time evaluation
 * limits in docs/ARCHITECTURE.md need a real timer source; that arrives with
 * the Phase 1 evaluator, not here.
 */
uint32_t phy_clock_ms(void)
{
    return g_ndless.clock_ms;
}

void phy_sleep_ms(uint32_t milliseconds)
{
    if (milliseconds == 0) {
        idle();
        return;
    }
    msleep(milliseconds);
    g_ndless.clock_ms += milliseconds;
}

void *phy_alloc(size_t bytes)
{
    if (bytes == 0) {
        return NULL;
    }
    void *pointer = malloc(bytes);
    if (pointer == NULL) {
        return NULL;
    }
    g_ndless.telemetry.bytes_live += bytes;
    g_ndless.telemetry.alloc_count++;
    if (g_ndless.telemetry.bytes_live > g_ndless.telemetry.bytes_peak) {
        g_ndless.telemetry.bytes_peak = g_ndless.telemetry.bytes_live;
    }
    return pointer;
}

void phy_free(void *pointer, size_t bytes)
{
    if (pointer == NULL) {
        return;
    }
    free(pointer);
    g_ndless.telemetry.bytes_live -= (bytes <= g_ndless.telemetry.bytes_live)
                                         ? bytes
                                         : g_ndless.telemetry.bytes_live;
    g_ndless.telemetry.free_count++;
}

void phy_telemetry_get(phy_telemetry *out_telemetry)
{
    if (out_telemetry == NULL) {
        return;
    }
    *out_telemetry = g_ndless.telemetry;
}
