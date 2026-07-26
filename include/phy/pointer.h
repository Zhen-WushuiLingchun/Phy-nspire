/*
 * Relative touchpad-to-pointer tracker.
 *
 * TI touchpad reports contain an absolute finger position. A notebook cursor
 * must behave like a mouse: a new contact establishes a motion origin and must
 * not teleport the cursor. This small state machine converts successive raw
 * samples to relative screen deltas while preserving fractional movement.
 */
#ifndef PHY_POINTER_H
#define PHY_POINTER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t x;
    int32_t y;
    int32_t last_raw_x;
    int32_t last_raw_y;
    int32_t residual_x;
    int32_t residual_y;
    bool contact_active;
} phy_pointer_tracker;

void phy_pointer_tracker_init(phy_pointer_tracker *tracker, int32_t x,
                              int32_t y);

/*
 * Feed one raw sample. raw_y increases upward; screen y increases downward.
 * Returns true only when the screen pointer position changed.
 */
bool phy_pointer_tracker_sample(phy_pointer_tracker *tracker, bool contact,
                                int32_t raw_x, int32_t raw_y,
                                int32_t raw_width, int32_t raw_height,
                                int32_t screen_width, int32_t screen_height);

void phy_pointer_tracker_position(const phy_pointer_tracker *tracker,
                                  int16_t *out_x, int16_t *out_y);

#ifdef __cplusplus
}
#endif

#endif /* PHY_POINTER_H */
