/*
 * Phy-nspire — RGB565 drawing primitives.
 *
 * Phase 0 keeps this deliberately small: enough to prove the framebuffer is
 * live and correctly ordered on real hardware, and to produce deterministic
 * fixtures. The FreeType/HarfBuzz text stack and bounded LaTeX layout arrive
 * with the notebook shell in Phase 1; the built-in 5x7 font here exists so the
 * baseline has no font assets and no heap dependency.
 *
 * Every primitive clips against the surface. Callers may pass out-of-range or
 * negative geometry without checking first.
 */
#ifndef PHY_GFX_H
#define PHY_GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t *pixels;
    int width;
    int height;
} phy_surface;

/* Channel packing matches the CX II panel in SCR_320x240_565 mode. */
#define PHY_RGB565(r, g, b)                                                    \
    ((uint16_t)((((uint32_t)(r) & 0xF8u) << 8) |                               \
                (((uint32_t)(g) & 0xFCu) << 3) |                               \
                (((uint32_t)(b) & 0xF8u) >> 3)))

#define PHY_COLOR_BLACK PHY_RGB565(0, 0, 0)
#define PHY_COLOR_WHITE PHY_RGB565(255, 255, 255)

#define PHY_GLYPH_WIDTH 5
#define PHY_GLYPH_HEIGHT 7
#define PHY_GLYPH_ADVANCE 6 /* 5 columns plus one spacing column */

/* Character cell height used for line stepping in the baseline frame. */
#define PHY_TEXT_LINE_HEIGHT 9

void phy_gfx_clear(const phy_surface *surface, uint16_t color);
void phy_gfx_fill_rect(const phy_surface *surface, int x, int y, int width,
                       int height, uint16_t color);
/* One-pixel outline drawn inside the given rectangle. */
void phy_gfx_draw_rect(const phy_surface *surface, int x, int y, int width,
                       int height, uint16_t color);
void phy_gfx_hline(const phy_surface *surface, int x, int y, int width,
                   uint16_t color);
void phy_gfx_vline(const phy_surface *surface, int x, int y, int height,
                   uint16_t color);
void phy_gfx_put_pixel(const phy_surface *surface, int x, int y,
                       uint16_t color);
uint16_t phy_gfx_get_pixel(const phy_surface *surface, int x, int y);

/*
 * Draws printable ASCII (0x20..0x7E). Unsupported bytes render as a filled
 * box so that encoding faults are visible instead of silent. Returns the pen x
 * position after the last glyph, so callers can chain runs.
 */
int phy_gfx_draw_text(const phy_surface *surface, int x, int y,
                      const char *text, uint16_t color);

/* Advance width of the string in pixels, without drawing. */
int phy_gfx_text_width(const char *text);

/*
 * Integer-scaled variant used for notebook headings. Scale 1 is bit-identical
 * to phy_gfx_draw_text; scale 0 is rejected and leaves the pen unchanged.
 */
int phy_gfx_draw_text_scaled(const phy_surface *surface, int x, int y,
                             const char *text, unsigned scale,
                             uint16_t color);
int phy_gfx_text_width_scaled(const char *text, unsigned scale);

/*
 * FNV-1a over the whole pixel buffer. Used by the deterministic framebuffer
 * fixtures; stable across platforms because the buffer is little-endian
 * uint16_t and hashed byte-wise in a fixed order.
 */
uint64_t phy_gfx_digest(const phy_surface *surface);

/* Writes a binary PPM (P6) for human inspection of a fixture mismatch. */
bool phy_gfx_write_ppm(const phy_surface *surface, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* PHY_GFX_H */
