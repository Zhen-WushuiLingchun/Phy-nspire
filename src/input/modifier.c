#include "phy/modifier.h"

#include <string.h>

void phy_modifier_tracker_init(phy_modifier_tracker *tracker)
{
    if (tracker != NULL) {
        memset(tracker, 0, sizeof *tracker);
    }
}

void phy_modifier_tracker_sample(phy_modifier_tracker *tracker,
                                 bool shift_down, bool control_down)
{
    if (tracker == NULL) {
        return;
    }
    if (shift_down && !tracker->shift_down) {
        tracker->shift_latched = true;
    }
    if (control_down && !tracker->control_down) {
        tracker->control_latched = true;
    }
    tracker->shift_down = shift_down;
    tracker->control_down = control_down;
}

char phy_modifier_tracker_text(phy_modifier_tracker *tracker, char normal,
                               char shifted, char controlled)
{
    if (tracker == NULL) {
        return normal;
    }
    const bool use_control =
        tracker->control_down || tracker->control_latched;
    const bool use_shift = tracker->shift_down || tracker->shift_latched;

    char result = normal;
    if (use_control && controlled != '\0') {
        result = controlled;
    } else if (use_shift && shifted != '\0') {
        result = shifted;
    }

    tracker->shift_latched = false;
    tracker->control_latched = false;
    return result;
}

void phy_modifier_tracker_nontext(phy_modifier_tracker *tracker)
{
    if (tracker == NULL) {
        return;
    }
    tracker->shift_latched = false;
    tracker->control_latched = false;
}
