#include "phy/phy.h"

static const char *const kStatusNames[PHY_STATUS_COUNT] = {
    "PHY_OK",
    "PHY_ERR_PLATFORM_INIT",
    "PHY_ERR_DISPLAY_UNSUPPORTED",
    "PHY_ERR_ALREADY_INITIALIZED",
    "PHY_ERR_NOT_INITIALIZED",
    "PHY_ERR_INVALID_ARGUMENT",
    "PHY_ERR_OUT_OF_MEMORY",
    "PHY_ERR_INTERRUPTED",
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
    if (index >= (unsigned)PHY_STATUS_COUNT) {
        return "PHY_ERR_UNKNOWN";
    }
    return kStatusNames[index];
}
