/* Absolute touch samples must drive a persistent relative notebook pointer. */
#include "phy/pointer.h"
#include "phy_test.h"

static void position(const phy_pointer_tracker *tracker, int expected_x,
                     int expected_y)
{
    int16_t x = -1;
    int16_t y = -1;
    phy_pointer_tracker_position(tracker, &x, &y);
    PHY_CHECK_EQ_INT(x, expected_x);
    PHY_CHECK_EQ_INT(y, expected_y);
}

static void test_new_contact_never_teleports(void)
{
    phy_pointer_tracker tracker;
    phy_pointer_tracker_init(&tracker, 160, 120);
    position(&tracker, 160, 120);

    PHY_CHECK(!phy_pointer_tracker_sample(&tracker, true, 50, 900, 1000,
                                          1000, 320, 240));
    position(&tracker, 160, 120);

    PHY_CHECK(phy_pointer_tracker_sample(&tracker, true, 150, 900, 1000,
                                         1000, 320, 240));
    position(&tracker, 191, 120);

    PHY_CHECK(!phy_pointer_tracker_sample(&tracker, false, 0, 0, 1000, 1000,
                                          320, 240));
    PHY_CHECK(!phy_pointer_tracker_sample(&tracker, true, 950, 50, 1000,
                                          1000, 320, 240));
    /* A new finger position is only a new origin. */
    position(&tracker, 191, 120);
}

static void test_axes_and_fractional_motion(void)
{
    phy_pointer_tracker tracker;
    phy_pointer_tracker_init(&tracker, 100, 100);
    PHY_CHECK(!phy_pointer_tracker_sample(&tracker, true, 500, 500, 1000,
                                          1000, 320, 240));

    /* Raw y increases upward, so screen y must decrease. */
    PHY_CHECK(phy_pointer_tracker_sample(&tracker, true, 500, 600, 1000,
                                         1000, 320, 240));
    position(&tracker, 100, 77);

    /*
     * One raw unit is below one screen pixel. Repeated slow samples must retain
     * the remainder rather than feeling dead.
     */
    for (int raw = 501; raw <= 504; ++raw) {
        (void)phy_pointer_tracker_sample(&tracker, true, raw, 600, 1000, 1000,
                                         320, 240);
    }
    position(&tracker, 101, 77);
}

static void test_edges_do_not_store_sticky_overshoot(void)
{
    phy_pointer_tracker tracker;
    phy_pointer_tracker_init(&tracker, 318, 2);
    PHY_CHECK(!phy_pointer_tracker_sample(&tracker, true, 100, 100, 1000,
                                          1000, 320, 240));
    PHY_CHECK(phy_pointer_tracker_sample(&tracker, true, 900, 900, 1000,
                                         1000, 320, 240));
    position(&tracker, 319, 0);

    /* Reverse immediately: clamped overshoot must not need to be "paid back". */
    PHY_CHECK(phy_pointer_tracker_sample(&tracker, true, 800, 800, 1000,
                                         1000, 320, 240));
    position(&tracker, 288, 23);
}

static void test_invalid_samples_are_inert(void)
{
    phy_pointer_tracker tracker;
    phy_pointer_tracker_init(&tracker, 3, 4);
    PHY_CHECK(!phy_pointer_tracker_sample(NULL, true, 1, 1, 10, 10, 320,
                                          240));
    PHY_CHECK(!phy_pointer_tracker_sample(&tracker, true, 1, 1, 0, 10, 320,
                                          240));
    position(&tracker, 3, 4);
    phy_pointer_tracker_init(NULL, 0, 0);
    phy_pointer_tracker_position(NULL, NULL, NULL);
}

int main(void)
{
    PHY_TEST_CASE(test_new_contact_never_teleports);
    PHY_TEST_CASE(test_axes_and_fractional_motion);
    PHY_TEST_CASE(test_edges_do_not_store_sticky_overshoot);
    PHY_TEST_CASE(test_invalid_samples_are_inert);
    return PHY_TEST_REPORT("test_pointer");
}
