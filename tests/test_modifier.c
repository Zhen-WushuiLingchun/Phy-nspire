#include "phy/modifier.h"
#include "phy_test.h"

static void test_tapped_shift_is_one_shot(void)
{
    phy_modifier_tracker tracker;
    phy_modifier_tracker_init(&tracker);

    phy_modifier_tracker_sample(&tracker, true, false);
    phy_modifier_tracker_sample(&tracker, false, false);
    PHY_CHECK_EQ_INT(
        phy_modifier_tracker_text(&tracker, 'a', 'A', '\0'), 'A');
    PHY_CHECK_EQ_INT(
        phy_modifier_tracker_text(&tracker, 'b', 'B', '\0'), 'b');
}

static void test_held_shift_applies_to_each_character(void)
{
    phy_modifier_tracker tracker;
    phy_modifier_tracker_init(&tracker);

    phy_modifier_tracker_sample(&tracker, true, false);
    PHY_CHECK_EQ_INT(
        phy_modifier_tracker_text(&tracker, 'a', 'A', '\0'), 'A');
    PHY_CHECK_EQ_INT(
        phy_modifier_tracker_text(&tracker, 'b', 'B', '\0'), 'B');
    phy_modifier_tracker_sample(&tracker, false, false);
    PHY_CHECK_EQ_INT(
        phy_modifier_tracker_text(&tracker, 'c', 'C', '\0'), 'c');
}

static void test_control_precedes_shift_and_falls_back(void)
{
    phy_modifier_tracker tracker;
    phy_modifier_tracker_init(&tracker);

    phy_modifier_tracker_sample(&tracker, true, true);
    PHY_CHECK_EQ_INT(
        phy_modifier_tracker_text(&tracker, '(', '[', ']'), ']');

    phy_modifier_tracker_sample(&tracker, false, false);
    phy_modifier_tracker_sample(&tracker, false, true);
    PHY_CHECK_EQ_INT(
        phy_modifier_tracker_text(&tracker, 'x', 'X', '\0'), 'x');
}

static void test_nontext_consumes_latch(void)
{
    phy_modifier_tracker tracker;
    phy_modifier_tracker_init(&tracker);

    phy_modifier_tracker_sample(&tracker, true, false);
    phy_modifier_tracker_sample(&tracker, false, false);
    phy_modifier_tracker_nontext(&tracker);
    PHY_CHECK_EQ_INT(
        phy_modifier_tracker_text(&tracker, 'a', 'A', '\0'), 'a');
}

int main(void)
{
    PHY_TEST_CASE(test_tapped_shift_is_one_shot);
    PHY_TEST_CASE(test_held_shift_applies_to_each_character);
    PHY_TEST_CASE(test_control_precedes_shift_and_falls_back);
    PHY_TEST_CASE(test_nontext_consumes_latch);
    return PHY_TEST_REPORT("test_modifier");
}
