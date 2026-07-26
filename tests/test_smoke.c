/*
 * Phase 0 smoke test.
 *
 * This is the roadmap's "host smoke test": bring up the framebuffer, render
 * the baseline, pump input, quit cleanly, and confirm the frame is bit-exact
 * against the checked-in fixture.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "phy/app.h"
#include "phy/gfx.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy_test.h"

#ifndef PHY_FIXTURE_DIR
#define PHY_FIXTURE_DIR "."
#endif
#ifndef PHY_ARTIFACT_DIR
#define PHY_ARTIFACT_DIR "."
#endif

static uint16_t g_scratch[PHY_SCREEN_PIXELS];

static void test_app_requires_platform(void)
{
    PHY_CHECK(!phy_platform_is_initialized());
    phy_app_result result;
    PHY_CHECK_EQ_INT(phy_app_run(NULL, &result), PHY_ERR_NOT_INITIALIZED);
}

static void test_defaults(void)
{
    phy_app_options options;
    memset(&options, 0xFF, sizeof options);
    phy_app_options_defaults(&options);
    PHY_CHECK_EQ_INT(options.max_frames, 0);
    PHY_CHECK(options.idle_sleep_ms > 0);
    phy_app_options_defaults(NULL); /* must not fault */
}

/* The full lifecycle, driven by scripted input, must terminate on ESC. */
static void test_lifecycle_quits_on_escape(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);

    PHY_CHECK(phy_host_push_pointer(PHY_EVENT_POINTER_MOVE, 64, 96));
    PHY_CHECK(phy_host_push_key(PHY_EVENT_KEY_DOWN, PHY_KEY_ESC));

    phy_app_options options;
    phy_app_options_defaults(&options);

    phy_app_result result;
    PHY_CHECK_EQ_INT(phy_app_run(&options, &result), PHY_OK);

    PHY_CHECK(result.quit_requested);
    PHY_CHECK_EQ_INT(result.events_handled, 2);
    PHY_CHECK_EQ_INT(result.pointer_x, 64);
    PHY_CHECK_EQ_INT(result.pointer_y, 96);
    /* Initial frame plus one redraw for the pointer move. */
    PHY_CHECK_EQ_INT(result.frames_presented, 2);
    PHY_CHECK_EQ_INT(phy_host_present_count(), 2);

    phy_platform_shutdown();
    PHY_CHECK(phy_host_display_was_restored());
}

/* A frame budget must bound the loop even with no input at all. */
static void test_frame_budget_terminates(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);

    phy_app_options options;
    phy_app_options_defaults(&options);
    options.max_frames = 1;

    phy_app_result result;
    PHY_CHECK_EQ_INT(phy_app_run(&options, &result), PHY_OK);
    PHY_CHECK_EQ_INT(result.frames_presented, 1);
    PHY_CHECK(!result.quit_requested);
    PHY_CHECK_EQ_INT(result.events_handled, 0);

    phy_platform_shutdown();
}

static void test_quit_event_stops_run(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    PHY_CHECK(phy_host_push_key(PHY_EVENT_QUIT, PHY_KEY_NONE));

    phy_app_result result;
    PHY_CHECK_EQ_INT(phy_app_run(NULL, &result), PHY_OK);
    PHY_CHECK(result.quit_requested);

    phy_platform_shutdown();
}

/* Out-of-range pointer events must be ignored, not clamped into a redraw. */
static void test_bogus_pointer_is_ignored(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    PHY_CHECK(phy_host_push_pointer(PHY_EVENT_POINTER_MOVE, -5, 10));
    PHY_CHECK(phy_host_push_pointer(PHY_EVENT_POINTER_MOVE, 10, 9000));
    PHY_CHECK(phy_host_push_key(PHY_EVENT_KEY_DOWN, PHY_KEY_ESC));

    phy_app_result result;
    PHY_CHECK_EQ_INT(phy_app_run(NULL, &result), PHY_OK);
    PHY_CHECK_EQ_INT(result.pointer_x, PHY_SCREEN_WIDTH / 2);
    PHY_CHECK_EQ_INT(result.pointer_y, PHY_SCREEN_HEIGHT / 2);
    PHY_CHECK_EQ_INT(result.frames_presented, 1); /* no redraw was triggered */

    phy_platform_shutdown();
}

/* Non-quit keys must not end the run. */
static void test_other_keys_do_not_quit(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    PHY_CHECK(phy_host_push_key(PHY_EVENT_KEY_DOWN, PHY_KEY_MENU));
    PHY_CHECK(phy_host_push_key(PHY_EVENT_KEY_UP, PHY_KEY_ESC));
    PHY_CHECK(phy_host_push_key(PHY_EVENT_KEY_DOWN, PHY_KEY_ESC));

    phy_app_result result;
    PHY_CHECK_EQ_INT(phy_app_run(NULL, &result), PHY_OK);
    PHY_CHECK(result.quit_requested);
    PHY_CHECK_EQ_INT(result.events_handled, 3);

    phy_platform_shutdown();
}

static void test_baseline_is_deterministic(void)
{
    const phy_surface surface = {g_scratch, PHY_SCREEN_WIDTH, PHY_SCREEN_HEIGHT};

    phy_app_draw_baseline(&surface, -1, -1);
    const uint64_t first = phy_gfx_digest(&surface);

    memset(g_scratch, 0xA5, sizeof g_scratch);
    phy_app_draw_baseline(&surface, -1, -1);
    PHY_CHECK(phy_gfx_digest(&surface) == first);

    /* The pointer overlay must actually change pixels. */
    phy_app_draw_baseline(&surface, 100, 100);
    PHY_CHECK(phy_gfx_digest(&surface) != first);

    phy_app_draw_baseline(NULL, -1, -1); /* must not fault */
}

/*
 * Golden fixture. On mismatch the actual frame is written next to the build so
 * the difference can be looked at rather than guessed at.
 */
static void test_baseline_matches_fixture(void)
{
    const char *path = PHY_FIXTURE_DIR "/baseline_frame.digest";
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        g_phy_test_failures++;
        fprintf(stderr, "FAIL cannot open fixture %s\n", path);
        return;
    }
    unsigned long long expected = 0;
    const int scanned = fscanf(file, "%llx", &expected);
    fclose(file);
    if (scanned != 1) {
        g_phy_test_failures++;
        fprintf(stderr, "FAIL malformed fixture %s\n", path);
        return;
    }

    const phy_surface surface = {g_scratch, PHY_SCREEN_WIDTH, PHY_SCREEN_HEIGHT};
    phy_app_draw_baseline(&surface, -1, -1);
    const unsigned long long actual =
        (unsigned long long)phy_gfx_digest(&surface);

    g_phy_test_checks++;
    if (actual != expected) {
        g_phy_test_failures++;
        const char *dump = PHY_ARTIFACT_DIR "/baseline_frame_actual.ppm";
        phy_gfx_write_ppm(&surface, dump);
        fprintf(stderr,
                "FAIL baseline frame digest: expected %016llx, got %016llx\n"
                "     wrote %s; regenerate with `phy-host --print-digest`\n",
                expected, actual, dump);
    }
}

int main(void)
{
    PHY_TEST_CASE(test_app_requires_platform);
    PHY_TEST_CASE(test_defaults);
    PHY_TEST_CASE(test_lifecycle_quits_on_escape);
    PHY_TEST_CASE(test_frame_budget_terminates);
    PHY_TEST_CASE(test_quit_event_stops_run);
    PHY_TEST_CASE(test_bogus_pointer_is_ignored);
    PHY_TEST_CASE(test_other_keys_do_not_quit);
    PHY_TEST_CASE(test_baseline_is_deterministic);
    PHY_TEST_CASE(test_baseline_matches_fixture);
    return PHY_TEST_REPORT("test_smoke");
}
