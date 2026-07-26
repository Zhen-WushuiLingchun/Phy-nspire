/*
 * Compact two-dimensional layout for typed IR expressions.
 *
 * This is intentionally not a LaTeX parser. Notebook results already have
 * structure in the IR, so flattening them to text and parsing that text again
 * would lose types and waste both time and memory. The layout walks the IR
 * directly and gives exact rationals a fraction bar, powers and indices a
 * raised/lowered script, and compound expressions a shared baseline.
 */
#ifndef PHY_MATH_LAYOUT_H
#define PHY_MATH_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

#include "phy/gfx.h"
#include "phy/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;
    int height;
    int baseline;
} phy_math_box;

/*
 * Measures or draws one expression. Both are allocation-free and bounded to
 * 64 recursive levels; a deeper subtree is represented by "...".
 */
bool phy_math_measure(const phy_ir_context *ir, phy_ir_ref expression,
                      phy_math_box *out_box);
int phy_math_draw(const phy_surface *surface, int x, int y,
                  const phy_ir_context *ir, phy_ir_ref expression,
                  uint16_t color);

#ifdef __cplusplus
}
#endif

#endif /* PHY_MATH_LAYOUT_H */
