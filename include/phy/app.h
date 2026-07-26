/*
 * Phy-nspire — Phase 0 application shell.
 *
 * The Phase 0 application proves the full lifecycle end to end: bring up the
 * framebuffer, draw a deterministic baseline frame, pump input, and leave the
 * device in a clean state. It is intentionally not the notebook; the cell
 * model, expression IR, and CAS boundary land in Phase 1.
 */
#ifndef PHY_APP_H
#define PHY_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "phy/gfx.h"
#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /*
     * Stop after this many presented frames. 0 means run until the user quits.
     * The host smoke test uses a finite budget so it can never hang CI.
     */
    uint32_t max_frames;
    /* Idle milliseconds between polls when no event is pending. */
    uint32_t idle_sleep_ms;
} phy_app_options;

typedef struct {
    uint32_t frames_presented;
    uint32_t events_handled;
    uint32_t pointer_x;
    uint32_t pointer_y;
    bool quit_requested;
    /* Digest of the last presented frame; feeds the fixture tests. */
    uint64_t last_frame_digest;
} phy_app_result;

void phy_app_options_defaults(phy_app_options *out_options);

/*
 * Runs the application against an already initialized platform. Returns
 * PHY_ERR_NOT_INITIALIZED if phy_platform_init has not succeeded.
 */
phy_status phy_app_run(const phy_app_options *options, phy_app_result *out_result);

/*
 * Draws the baseline frame into an arbitrary surface. Exposed separately from
 * phy_app_run so fixtures can render without an initialized platform.
 *
 * The device and host builds share this code exactly, so the golden fixture
 * pins the layout and the drawing primitives for both. The rendered frame is
 * not byte-identical across backends: the title bar reports
 * phy_platform_name(), which differs by design. The fixture therefore records
 * the host rendering.
 *
 * pointer_x/pointer_y position the pointer crosshair; pass negative values to
 * omit it, which is what the golden fixture uses.
 */
void phy_app_draw_baseline(const phy_surface *surface, int pointer_x,
                           int pointer_y);

#ifdef __cplusplus
}
#endif

#endif /* PHY_APP_H */
