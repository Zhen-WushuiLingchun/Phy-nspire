/*
 * Phy-nspire — core status values and build identity.
 *
 * Errors are typed values, never strings mixed into valid results, per
 * docs/ARCHITECTURE.md. The platform-facing subset arrived in Phase 0; the
 * parse/domain/backend categories arrived with the expression IR in Phase 1.
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

    /*
     * Expression-layer categories. Appended, never reordered: these values
     * are written into saved documents by the IR serializer, so their
     * numbering is part of the on-disk format.
     */
    PHY_ERR_PARSE,             /* malformed input text */
    PHY_ERR_TYPE,              /* a node kind rejected an operand's kind */
    PHY_ERR_DOMAIN,            /* mathematically undefined, e.g. n/0 */
    PHY_ERR_ASSUMPTION,        /* declared assumptions cannot all hold */
    PHY_ERR_UNSUPPORTED,       /* well-formed but not implemented */
    PHY_ERR_OVERFLOW,          /* exact arithmetic left the int64 range */
    PHY_ERR_TIMEOUT,           /* wall-clock budget exhausted */
    PHY_ERR_NODE_LIMIT,        /* too many distinct nodes */
    PHY_ERR_DEPTH_LIMIT,       /* expression nested too deeply */
    PHY_ERR_TERM_LIMIT,        /* too many operands on one node */
    PHY_ERR_MEMORY_LIMIT,      /* configured byte ceiling reached */
    PHY_ERR_BACKEND,           /* the CAS backend failed or was rejected */
    PHY_ERR_CORRUPT_DOCUMENT,  /* a saved notebook did not round-trip */

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
