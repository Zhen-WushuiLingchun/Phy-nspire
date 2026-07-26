/* Platform boundary contracts: lifecycle, event queue, telemetry. */
#include "phy/gfx.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy_test.h"

static void test_status_names(void)
{
    PHY_CHECK_EQ_STR(phy_status_name(PHY_OK), "PHY_OK");
    PHY_CHECK_EQ_STR(phy_status_name(PHY_ERR_NOT_INITIALIZED),
                     "PHY_ERR_NOT_INITIALIZED");
    /* Out-of-range values must not index past the table. */
    PHY_CHECK_EQ_STR(phy_status_name((phy_status)-1), "PHY_ERR_UNKNOWN");
    PHY_CHECK_EQ_STR(phy_status_name((phy_status)PHY_STATUS_COUNT),
                     "PHY_ERR_UNKNOWN");
    PHY_CHECK_EQ_STR(phy_status_name((phy_status)9999), "PHY_ERR_UNKNOWN");
}

static void test_uninitialized_access_is_refused(void)
{
    PHY_CHECK(!phy_platform_is_initialized());
    PHY_CHECK(phy_display_pixels() == NULL);
    PHY_CHECK_EQ_INT(phy_display_present(), PHY_ERR_NOT_INITIALIZED);

    phy_event event;
    PHY_CHECK(!phy_input_poll(&event));

    /* shutdown before init must be a no-op, not a crash. */
    phy_platform_shutdown();
    PHY_CHECK(!phy_platform_is_initialized());
}

static void test_lifecycle(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    PHY_CHECK(phy_platform_is_initialized());
    PHY_CHECK(phy_display_pixels() != NULL);

    /* Re-entry is refused rather than silently resetting live state. */
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_ERR_ALREADY_INITIALIZED);

    phy_platform_shutdown();
    PHY_CHECK(!phy_platform_is_initialized());
    PHY_CHECK(phy_display_pixels() == NULL);
    PHY_CHECK(phy_host_display_was_restored());

    /* Idempotent. */
    phy_platform_shutdown();
    PHY_CHECK(!phy_platform_is_initialized());
}

static void test_backbuffer_is_writable_and_sized(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    uint16_t *pixels = phy_display_pixels();
    PHY_CHECK(pixels != NULL);

    const phy_surface surface = {pixels, PHY_SCREEN_WIDTH, PHY_SCREEN_HEIGHT};
    phy_gfx_clear(&surface, PHY_RGB565(0, 0, 255));
    PHY_CHECK_EQ_INT(pixels[0], PHY_RGB565(0, 0, 255));
    PHY_CHECK_EQ_INT(pixels[PHY_SCREEN_PIXELS - 1], PHY_RGB565(0, 0, 255));
    PHY_CHECK_EQ_INT(PHY_SCREEN_BYTES, 320 * 240 * 2);

    PHY_CHECK_EQ_INT(phy_display_present(), PHY_OK);
    phy_platform_shutdown();
}

static void test_event_queue_order_and_capacity(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    PHY_CHECK_EQ_INT(phy_host_pending_event_count(), 0);

    PHY_CHECK(phy_host_push_key(PHY_EVENT_KEY_DOWN, PHY_KEY_MENU));
    PHY_CHECK(phy_host_push_pointer(PHY_EVENT_POINTER_MOVE, 11, 22));
    PHY_CHECK(phy_host_push_text('x'));
    PHY_CHECK_EQ_INT(phy_host_pending_event_count(), 3);

    phy_event event;
    PHY_CHECK(phy_input_poll(&event));
    PHY_CHECK_EQ_INT(event.kind, PHY_EVENT_KEY_DOWN);
    PHY_CHECK_EQ_INT(event.key, PHY_KEY_MENU);

    PHY_CHECK(phy_input_poll(&event));
    PHY_CHECK_EQ_INT(event.kind, PHY_EVENT_POINTER_MOVE);
    PHY_CHECK_EQ_INT(event.x, 11);
    PHY_CHECK_EQ_INT(event.y, 22);

    PHY_CHECK(phy_input_poll(&event));
    PHY_CHECK_EQ_INT(event.kind, PHY_EVENT_TEXT_INPUT);
    PHY_CHECK_EQ_INT(event.text, 'x');

    PHY_CHECK(!phy_input_poll(&event));
    PHY_CHECK(!phy_input_poll(NULL));

    /* Overflow is reported, never silently dropped. */
    for (unsigned i = 0; i < PHY_HOST_EVENT_QUEUE_CAPACITY; ++i) {
        PHY_CHECK(phy_host_push_key(PHY_EVENT_KEY_DOWN, PHY_KEY_OTHER));
    }
    PHY_CHECK(!phy_host_push_key(PHY_EVENT_KEY_DOWN, PHY_KEY_OTHER));

    phy_host_clear_events();
    PHY_CHECK_EQ_INT(phy_host_pending_event_count(), 0);
    phy_platform_shutdown();
}

/* The ring buffer must survive more wraps than its capacity. */
static void test_event_queue_wraps(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_event event;
    for (int round = 0; round < PHY_HOST_EVENT_QUEUE_CAPACITY * 3; ++round) {
        PHY_CHECK(phy_host_push_pointer(PHY_EVENT_POINTER_MOVE, (int16_t)round,
                                        (int16_t)(round + 1)));
        PHY_CHECK(phy_input_poll(&event));
        PHY_CHECK_EQ_INT(event.x, round);
        PHY_CHECK_EQ_INT(event.y, round + 1);
    }
    phy_platform_shutdown();
}

static void test_clock_is_monotonic(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    PHY_CHECK_EQ_INT(phy_clock_ms(), 0);
    phy_sleep_ms(5);
    PHY_CHECK_EQ_INT(phy_clock_ms(), 5);
    phy_host_advance_clock_ms(10);
    PHY_CHECK_EQ_INT(phy_clock_ms(), 15);
    phy_platform_shutdown();
}

static void test_allocation_telemetry(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);

    phy_telemetry telemetry;
    phy_telemetry_get(&telemetry);
    PHY_CHECK_EQ_INT(telemetry.bytes_live, 0);
    PHY_CHECK_EQ_INT(telemetry.alloc_count, 0);

    void *block = phy_alloc(1024);
    PHY_CHECK(block != NULL);
    phy_telemetry_get(&telemetry);
    PHY_CHECK_EQ_INT(telemetry.bytes_live, 1024);
    PHY_CHECK_EQ_INT(telemetry.bytes_peak, 1024);
    PHY_CHECK_EQ_INT(telemetry.alloc_count, 1);

    phy_free(block, 1024);
    phy_telemetry_get(&telemetry);
    PHY_CHECK_EQ_INT(telemetry.bytes_live, 0);
    PHY_CHECK_EQ_INT(telemetry.bytes_peak, 1024); /* peak is a high-water mark */
    PHY_CHECK_EQ_INT(telemetry.free_count, 1);

    PHY_CHECK(phy_alloc(0) == NULL);
    phy_free(NULL, 0); /* must not fault */

    /* Telemetry must reset with the platform, not leak across runs. */
    phy_platform_shutdown();
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_telemetry_get(&telemetry);
    PHY_CHECK_EQ_INT(telemetry.bytes_live, 0);
    PHY_CHECK_EQ_INT(telemetry.alloc_count, 0);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_status_names);
    PHY_TEST_CASE(test_uninitialized_access_is_refused);
    PHY_TEST_CASE(test_lifecycle);
    PHY_TEST_CASE(test_backbuffer_is_writable_and_sized);
    PHY_TEST_CASE(test_event_queue_order_and_capacity);
    PHY_TEST_CASE(test_event_queue_wraps);
    PHY_TEST_CASE(test_clock_is_monotonic);
    PHY_TEST_CASE(test_allocation_telemetry);
    return PHY_TEST_REPORT("test_platform");
}
