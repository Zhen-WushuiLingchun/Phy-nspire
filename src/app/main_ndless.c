/*
 * Device entry point.
 *
 * Every exit path runs phy_platform_shutdown, including the failure paths, so
 * the calculator is never left in SCR_320x240_565 with the TI shell drawing
 * into it.
 */
#include <libndls.h>

#include "phy/app.h"
#include "phy/platform.h"

int main(void)
{
    /*
     * assert_ndless_rev fails loudly on an Ndless older than the pinned r2022,
     * where lcd_init is not a syscall and the display contract differs.
     */
    assert_ndless_rev(2022);

    const phy_status status = phy_platform_init();
    if (status != PHY_OK) {
        show_msgbox("Phy-nspire", "Framebuffer initialization failed.");
        return 1;
    }

    phy_app_options options;
    phy_app_options_defaults(&options);

    phy_app_result result;
    const phy_status run_status = phy_app_run(&options, &result);

    phy_platform_shutdown();

    if (run_status != PHY_OK) {
        show_msgbox("Phy-nspire", phy_status_name(run_status));
        return 1;
    }
    return 0;
}
