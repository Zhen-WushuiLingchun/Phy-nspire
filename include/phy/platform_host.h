/*
 * Phy-nspire — host-backend test hooks.
 *
 * Only the host backend provides these. Device code must never include this
 * header; the Ndless build does not compile the translation unit that defines
 * these symbols.
 */
#ifndef PHY_PLATFORM_HOST_H
#define PHY_PLATFORM_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "phy/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHY_HOST_EVENT_QUEUE_CAPACITY 64

/*
 * Queues a synthetic event. Returns false when the queue is full, so tests
 * fail loudly instead of silently dropping input.
 */
bool phy_host_push_event(const phy_event *event);
bool phy_host_push_key(phy_event_kind kind, phy_key key);
bool phy_host_push_pointer(phy_event_kind kind, int16_t x, int16_t y);

void phy_host_clear_events(void);
unsigned phy_host_pending_event_count(void);

/* Number of times the back buffer was pushed to the (virtual) panel. */
uint32_t phy_host_present_count(void);

/*
 * True once phy_platform_shutdown has run and left the virtual panel in the
 * restored state. The device backend has no equivalent observable, so the
 * clean-exit contract is asserted here.
 */
bool phy_host_display_was_restored(void);

/* Virtual clock control, so timing-dependent tests stay deterministic. */
void phy_host_advance_clock_ms(uint32_t milliseconds);

#ifdef __cplusplus
}
#endif

#endif /* PHY_PLATFORM_HOST_H */
