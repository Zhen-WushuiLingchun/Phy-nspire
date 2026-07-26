#include <stddef.h>

#include "phy/phy.h"

/*
 * Designated initializers, not positional ones. The table was short enough to
 * read positionally in Phase 0; with the expression-layer categories added it
 * is long enough that a misaligned insertion would rename several statuses at
 * once without any diagnostic. Indexing by the enumerator makes that class of
 * edit impossible, and a forgotten entry is a NULL the accessor reports rather
 * than a wrong name it returns confidently.
 */
static const char *const kStatusNames[PHY_STATUS_COUNT] = {
    [PHY_OK] = "PHY_OK",
    [PHY_ERR_PLATFORM_INIT] = "PHY_ERR_PLATFORM_INIT",
    [PHY_ERR_DISPLAY_UNSUPPORTED] = "PHY_ERR_DISPLAY_UNSUPPORTED",
    [PHY_ERR_ALREADY_INITIALIZED] = "PHY_ERR_ALREADY_INITIALIZED",
    [PHY_ERR_NOT_INITIALIZED] = "PHY_ERR_NOT_INITIALIZED",
    [PHY_ERR_INVALID_ARGUMENT] = "PHY_ERR_INVALID_ARGUMENT",
    [PHY_ERR_OUT_OF_MEMORY] = "PHY_ERR_OUT_OF_MEMORY",
    [PHY_ERR_INTERRUPTED] = "PHY_ERR_INTERRUPTED",
    [PHY_ERR_PARSE] = "PHY_ERR_PARSE",
    [PHY_ERR_TYPE] = "PHY_ERR_TYPE",
    [PHY_ERR_DOMAIN] = "PHY_ERR_DOMAIN",
    [PHY_ERR_ASSUMPTION] = "PHY_ERR_ASSUMPTION",
    [PHY_ERR_UNSUPPORTED] = "PHY_ERR_UNSUPPORTED",
    [PHY_ERR_OVERFLOW] = "PHY_ERR_OVERFLOW",
    [PHY_ERR_TIMEOUT] = "PHY_ERR_TIMEOUT",
    [PHY_ERR_NODE_LIMIT] = "PHY_ERR_NODE_LIMIT",
    [PHY_ERR_DEPTH_LIMIT] = "PHY_ERR_DEPTH_LIMIT",
    [PHY_ERR_TERM_LIMIT] = "PHY_ERR_TERM_LIMIT",
    [PHY_ERR_MEMORY_LIMIT] = "PHY_ERR_MEMORY_LIMIT",
    [PHY_ERR_BACKEND] = "PHY_ERR_BACKEND",
    [PHY_ERR_CORRUPT_DOCUMENT] = "PHY_ERR_CORRUPT_DOCUMENT",
};

const char *phy_status_name(phy_status status)
{
    /*
     * Compared as unsigned deliberately. The ARM EABI builds with
     * -fshort-enums, so phy_status is an unsigned char on the device and a
     * signed int on most hosts; a `status < 0` test is a warning on one and
     * necessary on the other. Widening to unsigned folds a negative value to a
     * large one, which the upper bound then rejects on both.
     */
    const unsigned index = (unsigned)status;
    if (index >= (unsigned)PHY_STATUS_COUNT || kStatusNames[index] == NULL) {
        return "PHY_ERR_UNKNOWN";
    }
    return kStatusNames[index];
}
