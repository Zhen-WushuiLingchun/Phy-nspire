/*
 * nMarkdown-backed mathematical formula rendering.
 *
 * This is the only C/C++ boundary exposed to the portable notebook.  Raw
 * LaTeX remains document data; the bridge owns the C++ text and math systems
 * and never exposes an upstream object across the ABI.
 */
#ifndef PHY_FORMULA_H
#define PHY_FORMULA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/gfx.h"
#include "phy/ir.h"
#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PHY_FORMULA_STYLE_DISPLAY = 0,
    PHY_FORMULA_STYLE_TEXT
} phy_formula_style;

typedef struct {
    int width;
    int ascent;
    int descent;
    bool valid;
    bool overflow;
} phy_formula_metrics;

/*
 * One process-wide renderer is shared by every notebook cell. Initialization
 * loads the embedded font pack and is idempotent; shutdown is also idempotent.
 */
phy_status phy_formula_initialize(void);
void phy_formula_shutdown(void);
bool phy_formula_is_ready(void);

/*
 * Drop every cached typed-IR layout. Destroying an IR context already evicts
 * its entries through the phy_ir_set_destroy_observer hook that
 * phy_formula_initialize registers; this is the manual override.
 */
void phy_formula_ir_cache_reset(void);

/*
 * The layout engine's own words for the most recent measure or draw whose
 * metrics came back with valid == false: a parse that recovered still lays
 * out (as "!" plus the source), and only this string says why. Empty when
 * the last operation was valid. Returns the full diagnostic length.
 */
size_t phy_formula_last_diagnostic(char *buffer, size_t capacity);

/*
 * source does not need to be NUL terminated. pixel_size and maximum_width are
 * integer pixels. A non-positive maximum_width disables overflow checking.
 */
phy_status phy_formula_measure_latex(const char *source, size_t length,
                                     phy_formula_style style, int pixel_size,
                                     int maximum_width,
                                     phy_formula_metrics *out_metrics);

/*
 * baseline_y is the mathematical baseline. pan_x is a non-negative horizontal
 * viewport offset. The clip rectangle is intersected with the RGB565 surface.
 */
phy_status phy_formula_draw_latex(const phy_surface *surface,
                                  const char *source, size_t length,
                                  phy_formula_style style, int pixel_size,
                                  int maximum_width, int origin_x,
                                  int baseline_y, int pan_x,
                                  uint16_t foreground, uint16_t background,
                                  int clip_x, int clip_y, int clip_width,
                                  int clip_height,
                                  phy_formula_metrics *out_metrics);

/*
 * Typed CAS output enters the same MathTree/layout/draw pipeline directly.
 * No LaTeX serialization or reparsing occurs, so exact fractions, operator
 * order, and tensor-index variance remain structural all the way to pixels.
 */
phy_status phy_formula_measure_ir(const phy_ir_context *context,
                                  phy_ir_ref expression,
                                  phy_formula_style style, int pixel_size,
                                  int maximum_width,
                                  phy_formula_metrics *out_metrics);

phy_status phy_formula_draw_ir(const phy_surface *surface,
                               const phy_ir_context *context,
                               phy_ir_ref expression,
                               phy_formula_style style, int pixel_size,
                               int maximum_width, int origin_x,
                               int baseline_y, int pan_x,
                               uint16_t foreground, uint16_t background,
                               int clip_x, int clip_y, int clip_width,
                               int clip_height,
                               phy_formula_metrics *out_metrics);

#ifdef __cplusplus
}
#endif

#endif /* PHY_FORMULA_H */
