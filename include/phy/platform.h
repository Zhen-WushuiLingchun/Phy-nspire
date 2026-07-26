/*
 * Phy-nspire — platform boundary.
 *
 * This is the only header that physics, notebook, and rendering code may use
 * to reach the device. It owns the framebuffer, keypad and touchpad sampling,
 * the monotonic clock, allocation telemetry, and clean shutdown.
 *
 * Exactly one backend is linked into a binary:
 *   - src/platform/ndless  real CX II hardware under Ndless;
 *   - src/platform/host    offscreen and deterministic, for tests and fixtures.
 */
#ifndef PHY_PLATFORM_H
#define PHY_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHY_SCREEN_WIDTH 320
#define PHY_SCREEN_HEIGHT 240
#define PHY_SCREEN_PIXELS ((size_t)PHY_SCREEN_WIDTH * PHY_SCREEN_HEIGHT)
#define PHY_SCREEN_BYTES (PHY_SCREEN_PIXELS * sizeof(uint16_t))

typedef enum {
    PHY_EVENT_NONE = 0,
    PHY_EVENT_KEY_DOWN,
    PHY_EVENT_KEY_UP,
    PHY_EVENT_POINTER_MOVE,
    PHY_EVENT_POINTER_DOWN,
    PHY_EVENT_POINTER_UP,
    PHY_EVENT_QUIT
} phy_event_kind;

/*
 * Semantic keys only. Phase 0 deliberately does not expose raw scancodes:
 * the notebook shell should never learn the CX II keypad matrix.
 */
typedef enum {
    PHY_KEY_NONE = 0,
    PHY_KEY_ESC,
    PHY_KEY_ENTER,
    PHY_KEY_TAB,
    PHY_KEY_MENU,
    PHY_KEY_UP,
    PHY_KEY_DOWN,
    PHY_KEY_LEFT,
    PHY_KEY_RIGHT,
    PHY_KEY_OTHER
} phy_key;

typedef struct {
    phy_event_kind kind;
    phy_key key;
    int16_t x; /* pointer position, screen coordinates; 0 for key events */
    int16_t y;
} phy_event;

typedef struct {
    uint32_t frames_presented;
    uint32_t events_dispatched;
    size_t bytes_live;   /* currently outstanding phy_alloc bytes */
    size_t bytes_peak;   /* high-water mark since phy_platform_init */
    uint32_t alloc_count;
    uint32_t free_count;
} phy_telemetry;

/*
 * Lifecycle. init() is not reentrant and returns PHY_ERR_ALREADY_INITIALIZED
 * if called twice. shutdown() is idempotent and must restore the display to
 * the state the OS expects, so that returning to the TI shell leaves no
 * corruption behind.
 */
phy_status phy_platform_init(void);
void phy_platform_shutdown(void);
bool phy_platform_is_initialized(void);

/*
 * Mutable RGB565 back buffer, PHY_SCREEN_PIXELS entries in row-major order.
 * Owned by the platform; valid only between init and shutdown. Returns NULL
 * when not initialized.
 */
uint16_t *phy_display_pixels(void);

/* Push the back buffer to the panel. */
phy_status phy_display_present(void);

/*
 * Drain one pending event. Returns false when the queue is empty, which is
 * the signal for the caller to idle rather than spin.
 */
bool phy_input_poll(phy_event *out_event);

/* Monotonic milliseconds since phy_platform_init. */
uint32_t phy_clock_ms(void);
void phy_sleep_ms(uint32_t milliseconds);

/*
 * Tracked allocation. Every subsystem must route through these so that the
 * memory ceilings in docs/ARCHITECTURE.md can be enforced later.
 */
void *phy_alloc(size_t bytes);
void phy_free(void *pointer, size_t bytes);

void phy_telemetry_get(phy_telemetry *out_telemetry);

#ifdef __cplusplus
}
#endif

#endif /* PHY_PLATFORM_H */
