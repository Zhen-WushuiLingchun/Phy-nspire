/*
 * Host entry point.
 *
 * Runs the Phase 0 shell against the offscreen backend. This is what the smoke
 * test drives, and what regenerates the framebuffer fixture.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "phy/app.h"
#include "phy/gfx.h"
#include "phy/platform.h"
#include "phy/platform_host.h"

static void print_usage(const char *program)
{
    fprintf(stderr,
            "usage: %s [--frames N] [--dump-ppm PATH] [--print-digest]\n"
            "\n"
            "  --frames N       stop after N presented frames (default 1)\n"
            "  --dump-ppm PATH  write the final frame as a binary PPM\n"
            "  --print-digest   print the baseline frame digest and exit code 0\n",
            program);
}

int main(int argc, char **argv)
{
    uint32_t frames = 1;
    const char *ppm_path = NULL;
    bool print_digest = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--dump-ppm") == 0 && i + 1 < argc) {
            ppm_path = argv[++i];
        } else if (strcmp(argv[i], "--print-digest") == 0) {
            print_digest = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unrecognized argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    phy_status status = phy_platform_init();
    if (status != PHY_OK) {
        fprintf(stderr, "platform init failed: %s\n", phy_status_name(status));
        return 1;
    }

    /*
     * The fixture frame carries no pointer, so that the golden digest depends
     * only on the static baseline and not on a cursor position.
     */
    if (print_digest) {
        static uint16_t scratch[PHY_SCREEN_PIXELS];
        const phy_surface surface = {scratch, PHY_SCREEN_WIDTH, PHY_SCREEN_HEIGHT};
        phy_app_draw_baseline(&surface, -1, -1);
        printf("%016llx\n", (unsigned long long)phy_gfx_digest(&surface));
        if (ppm_path != NULL && !phy_gfx_write_ppm(&surface, ppm_path)) {
            fprintf(stderr, "failed to write %s\n", ppm_path);
            phy_platform_shutdown();
            return 1;
        }
        phy_platform_shutdown();
        return 0;
    }

    /* Drive one pointer move then quit, so the run terminates without a user. */
    phy_host_push_pointer(PHY_EVENT_POINTER_MOVE, 200, 150);
    phy_host_push_key(PHY_EVENT_KEY_DOWN, PHY_KEY_ESC);

    phy_app_options options;
    phy_app_options_defaults(&options);
    options.max_frames = frames;

    phy_app_result result;
    status = phy_app_run(&options, &result);
    if (status != PHY_OK) {
        fprintf(stderr, "app run failed: %s\n", phy_status_name(status));
        phy_platform_shutdown();
        return 1;
    }

    if (ppm_path != NULL) {
        const phy_surface surface = {phy_display_pixels(), PHY_SCREEN_WIDTH,
                                     PHY_SCREEN_HEIGHT};
        if (!phy_gfx_write_ppm(&surface, ppm_path)) {
            fprintf(stderr, "failed to write %s\n", ppm_path);
            phy_platform_shutdown();
            return 1;
        }
    }

    phy_telemetry telemetry;
    phy_telemetry_get(&telemetry);

    printf("platform        %s\n", phy_platform_name());
    printf("frames          %u\n", result.frames_presented);
    printf("events          %u\n", result.events_handled);
    printf("quit_requested  %s\n", result.quit_requested ? "yes" : "no");
    printf("last_digest     %016llx\n",
           (unsigned long long)result.last_frame_digest);
    printf("bytes_peak      %lu\n", (unsigned long)telemetry.bytes_peak);

    phy_platform_shutdown();

    if (!phy_host_display_was_restored()) {
        fprintf(stderr, "display was not restored on shutdown\n");
        return 1;
    }
    return 0;
}
