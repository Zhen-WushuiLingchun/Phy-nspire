/*
 * One-shot/held keyboard modifiers shared by the Ndless input backend and
 * host tests.
 *
 * TI-Nspire Shift and Ctrl are commonly tapped before the target key instead
 * of being held as a desktop chord.  Sampling only their instantaneous state
 * therefore loses the modifier before the letter arrives.  This tracker
 * supports both styles and consumes a tapped modifier after one key.
 */
#ifndef PHY_MODIFIER_H
#define PHY_MODIFIER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool shift_down;
    bool control_down;
    bool shift_latched;
    bool control_latched;
} phy_modifier_tracker;

void phy_modifier_tracker_init(phy_modifier_tracker *tracker);

/* Feed the current physical state once per keypad scan. */
void phy_modifier_tracker_sample(phy_modifier_tracker *tracker,
                                 bool shift_down, bool control_down);

/*
 * Select one character and consume any one-shot modifier.  A missing shifted
 * or controlled form falls back to `normal`.
 */
char phy_modifier_tracker_text(phy_modifier_tracker *tracker, char normal,
                               char shifted, char controlled);

/* A navigation/action key consumes a pending one-shot modifier. */
void phy_modifier_tracker_nontext(phy_modifier_tracker *tracker);

#ifdef __cplusplus
}
#endif

#endif /* PHY_MODIFIER_H */
