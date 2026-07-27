/*
 * Host platform backend.
 *
 * Renders to an offscreen buffer and reads input from a scripted queue, so the
 * whole Phase 0 lifecycle is exercised deterministically on a desktop with no
 * calculator attached. The clock is virtual: nothing here reads wall time, so
 * fixtures cannot drift.
 */
#include <stdlib.h>
#include <string.h>

#include "phy/platform.h"
#include "phy/platform_host.h"

typedef struct {
    bool initialized;
    bool display_restored;
    uint16_t pixels[PHY_SCREEN_PIXELS];

    phy_event queue[PHY_HOST_EVENT_QUEUE_CAPACITY];
    unsigned queue_head;
    unsigned queue_count;

    uint32_t clock_ms;
    uint32_t present_count;
    uint32_t alloc_countdown; /* 0 disables; see phy_host_fail_alloc_after */
    uint32_t alloc_attempts;
    phy_telemetry telemetry;
} host_state;

static host_state g_host;

const char *phy_platform_name(void)
{
    return "host";
}

phy_status phy_platform_init(void)
{
    if (g_host.initialized) {
        return PHY_ERR_ALREADY_INITIALIZED;
    }
    memset(&g_host, 0, sizeof g_host);
    g_host.initialized = true;
    g_host.display_restored = false;
    return PHY_OK;
}

void phy_platform_shutdown(void)
{
    if (!g_host.initialized) {
        return; /* idempotent */
    }
    /* Mirrors the device contract: blank the panel before handing it back. */
    memset(g_host.pixels, 0, sizeof g_host.pixels);
    g_host.initialized = false;
    g_host.display_restored = true;
    g_host.queue_head = 0;
    g_host.queue_count = 0;
}

bool phy_platform_is_initialized(void)
{
    return g_host.initialized;
}

uint16_t *phy_display_pixels(void)
{
    return g_host.initialized ? g_host.pixels : NULL;
}

phy_status phy_display_present(void)
{
    if (!g_host.initialized) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    g_host.present_count++;
    g_host.telemetry.frames_presented++;
    return PHY_OK;
}

bool phy_input_poll(phy_event *out_event)
{
    if (!g_host.initialized || out_event == NULL || g_host.queue_count == 0) {
        return false;
    }
    *out_event = g_host.queue[g_host.queue_head];
    g_host.queue_head = (g_host.queue_head + 1) % PHY_HOST_EVENT_QUEUE_CAPACITY;
    g_host.queue_count--;
    g_host.telemetry.events_dispatched++;
    return true;
}

uint32_t phy_clock_ms(void)
{
    return g_host.clock_ms;
}

void phy_sleep_ms(uint32_t milliseconds)
{
    /* Virtual: advancing the clock is the whole effect. */
    g_host.clock_ms += milliseconds;
}

void *phy_alloc(size_t bytes)
{
    if (bytes == 0) {
        return NULL;
    }
    g_host.alloc_attempts++;
    /* Injected failure, armed by phy_host_fail_alloc_after. Disarms itself so
       one armed shot cannot cascade into every later allocation. */
    if (g_host.alloc_countdown != 0u && --g_host.alloc_countdown == 0u) {
        return NULL;
    }
    void *pointer = malloc(bytes);
    if (pointer == NULL) {
        return NULL;
    }
    g_host.telemetry.bytes_live += bytes;
    g_host.telemetry.alloc_count++;
    if (g_host.telemetry.bytes_live > g_host.telemetry.bytes_peak) {
        g_host.telemetry.bytes_peak = g_host.telemetry.bytes_live;
    }
    return pointer;
}

void phy_free(void *pointer, size_t bytes)
{
    if (pointer == NULL) {
        return;
    }
    free(pointer);
    g_host.telemetry.bytes_live -= (bytes <= g_host.telemetry.bytes_live)
                                       ? bytes
                                       : g_host.telemetry.bytes_live;
    g_host.telemetry.free_count++;
}

void phy_telemetry_get(phy_telemetry *out_telemetry)
{
    if (out_telemetry == NULL) {
        return;
    }
    *out_telemetry = g_host.telemetry;
}

void phy_telemetry_reset_peak(void)
{
    g_host.telemetry.bytes_peak = g_host.telemetry.bytes_live;
}

/* ---- host-only test hooks ---- */

bool phy_host_push_event(const phy_event *event)
{
    if (event == NULL || g_host.queue_count >= PHY_HOST_EVENT_QUEUE_CAPACITY) {
        return false;
    }
    const unsigned tail =
        (g_host.queue_head + g_host.queue_count) % PHY_HOST_EVENT_QUEUE_CAPACITY;
    g_host.queue[tail] = *event;
    g_host.queue_count++;
    return true;
}

bool phy_host_push_key(phy_event_kind kind, phy_key key)
{
    const phy_event event = {kind, key, 0, 0, '\0'};
    return phy_host_push_event(&event);
}

bool phy_host_push_pointer(phy_event_kind kind, int16_t x, int16_t y)
{
    const phy_event event = {kind, PHY_KEY_NONE, x, y, '\0'};
    return phy_host_push_event(&event);
}

bool phy_host_push_text(char text)
{
    const phy_event event = {
        PHY_EVENT_TEXT_INPUT,
        PHY_KEY_NONE,
        0,
        0,
        text,
    };
    return phy_host_push_event(&event);
}

void phy_host_clear_events(void)
{
    g_host.queue_head = 0;
    g_host.queue_count = 0;
}

unsigned phy_host_pending_event_count(void)
{
    return g_host.queue_count;
}

uint32_t phy_host_present_count(void)
{
    return g_host.present_count;
}

bool phy_host_display_was_restored(void)
{
    return g_host.display_restored;
}

void phy_host_advance_clock_ms(uint32_t milliseconds)
{
    g_host.clock_ms += milliseconds;
}

void phy_host_fail_alloc_after(unsigned countdown)
{
    g_host.alloc_countdown = (uint32_t)countdown;
}

uint32_t phy_host_alloc_attempts(void)
{
    return g_host.alloc_attempts;
}
