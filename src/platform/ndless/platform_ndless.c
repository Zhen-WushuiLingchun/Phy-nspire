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
#include "phy/modifier.h"
#include "phy/pointer.h"

#define EVENT_QUEUE_CAPACITY 32

typedef struct {
    const t_key *key;
    phy_key semantic;
} key_binding;

typedef struct {
    const t_key *key;
    char normal;
    char shifted;
    char controlled;
} text_binding;

typedef struct {
    bool initialized;
    bool display_switched;

    uint16_t *backbuffer;

    phy_event queue[EVENT_QUEUE_CAPACITY];
    unsigned queue_head;
    unsigned queue_count;

    /* Debounce state: previous sample for each bound key. */
    bool key_state[10];
    bool text_state[56];
    phy_modifier_tracker modifiers;

    bool pointer_pressed;
    phy_pointer_tracker pointer;

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
    {&KEY_NSPIRE_DEL, PHY_KEY_BACKSPACE},
};

#define KEY_BINDING_COUNT ((int)(sizeof kKeyBindings / sizeof kKeyBindings[0]))

static const text_binding kTextBindings[] = {
#define ALPHA(key, lower, upper) {&key, lower, upper, '\0'}
#define PLAIN(key, normal) {&key, normal, '\0', '\0'}
    ALPHA(KEY_NSPIRE_A, 'a', 'A'), ALPHA(KEY_NSPIRE_B, 'b', 'B'),
    ALPHA(KEY_NSPIRE_C, 'c', 'C'), ALPHA(KEY_NSPIRE_D, 'd', 'D'),
    ALPHA(KEY_NSPIRE_E, 'e', 'E'), ALPHA(KEY_NSPIRE_F, 'f', 'F'),
    ALPHA(KEY_NSPIRE_G, 'g', 'G'), ALPHA(KEY_NSPIRE_H, 'h', 'H'),
    ALPHA(KEY_NSPIRE_I, 'i', 'I'), ALPHA(KEY_NSPIRE_J, 'j', 'J'),
    ALPHA(KEY_NSPIRE_K, 'k', 'K'), ALPHA(KEY_NSPIRE_L, 'l', 'L'),
    ALPHA(KEY_NSPIRE_M, 'm', 'M'), ALPHA(KEY_NSPIRE_N, 'n', 'N'),
    ALPHA(KEY_NSPIRE_O, 'o', 'O'), ALPHA(KEY_NSPIRE_P, 'p', 'P'),
    ALPHA(KEY_NSPIRE_Q, 'q', 'Q'), ALPHA(KEY_NSPIRE_R, 'r', 'R'),
    ALPHA(KEY_NSPIRE_S, 's', 'S'), ALPHA(KEY_NSPIRE_T, 't', 'T'),
    ALPHA(KEY_NSPIRE_U, 'u', 'U'), ALPHA(KEY_NSPIRE_V, 'v', 'V'),
    ALPHA(KEY_NSPIRE_W, 'w', 'W'), ALPHA(KEY_NSPIRE_X, 'x', 'X'),
    ALPHA(KEY_NSPIRE_Y, 'y', 'Y'), ALPHA(KEY_NSPIRE_Z, 'z', 'Z'),
    PLAIN(KEY_NSPIRE_0, '0'), PLAIN(KEY_NSPIRE_1, '1'),
    PLAIN(KEY_NSPIRE_2, '2'), PLAIN(KEY_NSPIRE_3, '3'),
    PLAIN(KEY_NSPIRE_4, '4'), PLAIN(KEY_NSPIRE_5, '5'),
    PLAIN(KEY_NSPIRE_6, '6'), PLAIN(KEY_NSPIRE_7, '7'),
    PLAIN(KEY_NSPIRE_8, '8'), PLAIN(KEY_NSPIRE_9, '9'),
    {&KEY_NSPIRE_PLUS, '+', '>', '\0'},
    {&KEY_NSPIRE_MINUS, '-', '<', '_'},
    {&KEY_NSPIRE_NEGATIVE, '-', '_', '\0'},
    {&KEY_NSPIRE_MULTIPLY, '*', '"', '\0'},
    {&KEY_NSPIRE_DIVIDE, '/', '\\', '\0'},
    PLAIN(KEY_NSPIRE_EXP, '^'),
    {&KEY_NSPIRE_LP, '(', '[', ']'},
    {&KEY_NSPIRE_RP, ')', '{', '}'},
    {&KEY_NSPIRE_PERIOD, '.', ':', '\0'},
    {&KEY_NSPIRE_COMMA, ',', ';', '\0'},
    {&KEY_NSPIRE_SPACE, ' ', '_', '\0'},
    {&KEY_NSPIRE_EQU, '=', '|', '\0'},
    {&KEY_NSPIRE_EE, '&', '%', '@'},
#undef PLAIN
#undef ALPHA
};

#define TEXT_BINDING_COUNT                                                     \
    ((int)(sizeof kTextBindings / sizeof kTextBindings[0]))

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
    const phy_event event = {kind, key, 0, 0, '\0'};
    queue_push(&event);
}

static void push_pointer(phy_event_kind kind, int16_t x, int16_t y)
{
    const phy_event event = {kind, PHY_KEY_NONE, x, y, '\0'};
    queue_push(&event);
}

static void push_text(char text)
{
    const phy_event event = {
        PHY_EVENT_TEXT_INPUT,
        PHY_KEY_NONE,
        0,
        0,
        text,
    };
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

    phy_pointer_tracker_init(&g_ndless.pointer, PHY_SCREEN_WIDTH / 2,
                             PHY_SCREEN_HEIGHT / 2);
    phy_modifier_tracker_init(&g_ndless.modifiers);
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
    phy_modifier_tracker_sample(
        &g_ndless.modifiers,
        isKeyPressed(KEY_NSPIRE_SHIFT) ? true : false,
        isKeyPressed(KEY_NSPIRE_CTRL) ? true : false);

    for (int i = 0; i < KEY_BINDING_COUNT; ++i) {
        /* isKeyPressed is a macro that takes the address of its argument. */
        const bool down = isKeyPressed(*kKeyBindings[i].key) ? true : false;
        if (down != g_ndless.key_state[i]) {
            g_ndless.key_state[i] = down;
            push_key(down ? PHY_EVENT_KEY_DOWN : PHY_EVENT_KEY_UP,
                     kKeyBindings[i].semantic);
            if (down) {
                phy_modifier_tracker_nontext(&g_ndless.modifiers);
            }
        }
    }
    for (int i = 0; i < TEXT_BINDING_COUNT; ++i) {
        const bool down = isKeyPressed(*kTextBindings[i].key) ? true : false;
        if (down != g_ndless.text_state[i]) {
            g_ndless.text_state[i] = down;
            if (down) {
                const char text = phy_modifier_tracker_text(
                    &g_ndless.modifiers, kTextBindings[i].normal,
                    kTextBindings[i].shifted, kTextBindings[i].controlled);
                push_text(text);
            }
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

    /*
     * The four directional keys are physically part of the touchpad.  Some
     * CX II reports expose a simultaneous pad press/contact for them; if that
     * becomes POINTER_DOWN the editor interprets it as a click and teleports
     * the insertion cursor to the on-screen pointer.  Directional navigation
     * is a key event, so suppress the overlapping pad sample completely.
     */
    const bool navigation_key =
        isKeyPressed(KEY_NSPIRE_UP) || isKeyPressed(KEY_NSPIRE_DOWN) ||
        isKeyPressed(KEY_NSPIRE_LEFT) || isKeyPressed(KEY_NSPIRE_RIGHT);
    const bool contact = report.contact && !navigation_key;
    if (phy_pointer_tracker_sample(
            &g_ndless.pointer, contact, (int32_t)report.x, (int32_t)report.y,
            (int32_t)info->width, (int32_t)info->height, PHY_SCREEN_WIDTH,
            PHY_SCREEN_HEIGHT)) {
        int16_t pointer_x = 0;
        int16_t pointer_y = 0;
        phy_pointer_tracker_position(&g_ndless.pointer, &pointer_x, &pointer_y);
        push_pointer(PHY_EVENT_POINTER_MOVE, pointer_x, pointer_y);
    }

    const bool pressed = report.pressed && !navigation_key;
    if (pressed != g_ndless.pointer_pressed) {
        g_ndless.pointer_pressed = pressed;
        int16_t pointer_x = 0;
        int16_t pointer_y = 0;
        phy_pointer_tracker_position(&g_ndless.pointer, &pointer_x, &pointer_y);
        push_pointer(pressed ? PHY_EVENT_POINTER_DOWN : PHY_EVENT_POINTER_UP,
                     pointer_x, pointer_y);
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
