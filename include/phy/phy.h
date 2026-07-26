/*
 * Phy-nspire — core status values and build identity.
 *
 * Errors are typed values, never strings mixed into valid results, per
 * docs/ARCHITECTURE.md. Phase 0 only needs the platform-facing subset; the
 * parse/domain/backend categories arrive with the expression IR in Phase 1.
 */
#ifndef PHY_PHY_H
#define PHY_PHY_H

#ifdef __cplusplus
extern "C" {
#endif

#define PHY_VERSION_MAJOR 0
#define PHY_VERSION_MINOR 1
#define PHY_VERSION_PATCH 0
#define PHY_VERSION_STRING "0.1.0"

typedef enum {
    PHY_OK = 0,
    PHY_ERR_PLATFORM_INIT,
    PHY_ERR_DISPLAY_UNSUPPORTED,
    PHY_ERR_ALREADY_INITIALIZED,
    PHY_ERR_NOT_INITIALIZED,
    PHY_ERR_INVALID_ARGUMENT,
    PHY_ERR_OUT_OF_MEMORY,
    PHY_ERR_INTERRUPTED,
    PHY_STATUS_COUNT
} phy_status;

/* Stable, allocation-free name for a status value. Never returns NULL. */
const char *phy_status_name(phy_status status);

/* Name of the compiled-in platform backend: "ndless" or "host". */
const char *phy_platform_name(void);

#ifdef __cplusplus
}
#endif

#endif /* PHY_PHY_H */
