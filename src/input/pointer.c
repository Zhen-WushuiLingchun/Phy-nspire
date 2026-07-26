#include "phy/pointer.h"

#include <stddef.h>

static int32_t clamp_axis(int64_t value, int32_t extent, bool *out_clamped)
{
    if (value < 0) {
        *out_clamped = true;
        return 0;
    }
    if (value >= extent) {
        *out_clamped = true;
        return extent - 1;
    }
    *out_clamped = false;
    return (int32_t)value;
}

void phy_pointer_tracker_init(phy_pointer_tracker *tracker, int32_t x,
                              int32_t y)
{
    if (tracker == NULL) {
        return;
    }
    tracker->x = x;
    tracker->y = y;
    tracker->last_raw_x = 0;
    tracker->last_raw_y = 0;
    tracker->residual_x = 0;
    tracker->residual_y = 0;
    tracker->contact_active = false;
}

bool phy_pointer_tracker_sample(phy_pointer_tracker *tracker, bool contact,
                                int32_t raw_x, int32_t raw_y,
                                int32_t raw_width, int32_t raw_height,
                                int32_t screen_width, int32_t screen_height)
{
    if (tracker == NULL || raw_width <= 0 || raw_height <= 0 ||
        screen_width <= 0 || screen_height <= 0) {
        return false;
    }
    if (!contact) {
        tracker->contact_active = false;
        tracker->residual_x = 0;
        tracker->residual_y = 0;
        return false;
    }
    if (!tracker->contact_active) {
        tracker->last_raw_x = raw_x;
        tracker->last_raw_y = raw_y;
        tracker->residual_x = 0;
        tracker->residual_y = 0;
        tracker->contact_active = true;
        return false;
    }

    const int64_t delta_x = (int64_t)raw_x - tracker->last_raw_x;
    const int64_t delta_y = (int64_t)raw_y - tracker->last_raw_y;
    tracker->last_raw_x = raw_x;
    tracker->last_raw_y = raw_y;

    const int64_t scaled_x =
        delta_x * (screen_width - 1) + tracker->residual_x;
    const int64_t scaled_y =
        -delta_y * (screen_height - 1) + tracker->residual_y;
    const int64_t move_x = scaled_x / raw_width;
    const int64_t move_y = scaled_y / raw_height;
    tracker->residual_x = (int32_t)(scaled_x % raw_width);
    tracker->residual_y = (int32_t)(scaled_y % raw_height);

    bool clamped_x = false;
    bool clamped_y = false;
    const int32_t next_x =
        clamp_axis((int64_t)tracker->x + move_x, screen_width, &clamped_x);
    const int32_t next_y =
        clamp_axis((int64_t)tracker->y + move_y, screen_height, &clamped_y);
    if (clamped_x) {
        tracker->residual_x = 0;
    }
    if (clamped_y) {
        tracker->residual_y = 0;
    }

    const bool moved = next_x != tracker->x || next_y != tracker->y;
    tracker->x = next_x;
    tracker->y = next_y;
    return moved;
}

void phy_pointer_tracker_position(const phy_pointer_tracker *tracker,
                                  int16_t *out_x, int16_t *out_y)
{
    if (tracker == NULL) {
        return;
    }
    if (out_x != NULL) {
        *out_x = (int16_t)tracker->x;
    }
    if (out_y != NULL) {
        *out_y = (int16_t)tracker->y;
    }
}
