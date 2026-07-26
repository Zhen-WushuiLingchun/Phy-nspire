#include "phy/gfx.h"

#include <stdio.h>
#include <string.h>

#include "font5x7.h"

static bool surface_valid(const phy_surface *surface)
{
    return surface != NULL && surface->pixels != NULL && surface->width > 0 &&
           surface->height > 0;
}

/*
 * Clips a rectangle to the surface. Returns false when nothing remains, which
 * lets every primitive share one overflow-safe path instead of open-coding
 * bounds checks that tend to disagree with each other.
 */
static bool clip_rect(const phy_surface *surface, int *x, int *y, int *width,
                      int *height)
{
    if (!surface_valid(surface) || *width <= 0 || *height <= 0) {
        return false;
    }
    if (*x < 0) {
        *width += *x;
        *x = 0;
    }
    if (*y < 0) {
        *height += *y;
        *y = 0;
    }
    if (*x >= surface->width || *y >= surface->height) {
        return false;
    }
    if (*width <= 0 || *height <= 0) {
        return false;
    }
    if (*x + *width > surface->width) {
        *width = surface->width - *x;
    }
    if (*y + *height > surface->height) {
        *height = surface->height - *y;
    }
    return *width > 0 && *height > 0;
}

void phy_gfx_clear(const phy_surface *surface, uint16_t color)
{
    if (!surface_valid(surface)) {
        return;
    }
    const size_t count = (size_t)surface->width * (size_t)surface->height;
    for (size_t i = 0; i < count; ++i) {
        surface->pixels[i] = color;
    }
}

void phy_gfx_put_pixel(const phy_surface *surface, int x, int y, uint16_t color)
{
    if (!surface_valid(surface) || x < 0 || y < 0 || x >= surface->width ||
        y >= surface->height) {
        return;
    }
    surface->pixels[(size_t)y * (size_t)surface->width + (size_t)x] = color;
}

uint16_t phy_gfx_get_pixel(const phy_surface *surface, int x, int y)
{
    if (!surface_valid(surface) || x < 0 || y < 0 || x >= surface->width ||
        y >= surface->height) {
        return 0;
    }
    return surface->pixels[(size_t)y * (size_t)surface->width + (size_t)x];
}

void phy_gfx_fill_rect(const phy_surface *surface, int x, int y, int width,
                       int height, uint16_t color)
{
    if (!clip_rect(surface, &x, &y, &width, &height)) {
        return;
    }
    for (int row = 0; row < height; ++row) {
        uint16_t *line =
            surface->pixels + (size_t)(y + row) * (size_t)surface->width + (size_t)x;
        for (int col = 0; col < width; ++col) {
            line[col] = color;
        }
    }
}

void phy_gfx_hline(const phy_surface *surface, int x, int y, int width,
                   uint16_t color)
{
    phy_gfx_fill_rect(surface, x, y, width, 1, color);
}

void phy_gfx_vline(const phy_surface *surface, int x, int y, int height,
                   uint16_t color)
{
    phy_gfx_fill_rect(surface, x, y, 1, height, color);
}

void phy_gfx_draw_rect(const phy_surface *surface, int x, int y, int width,
                       int height, uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    phy_gfx_hline(surface, x, y, width, color);
    phy_gfx_hline(surface, x, y + height - 1, width, color);
    /* Corners are already covered by the horizontal runs. */
    phy_gfx_vline(surface, x, y + 1, height - 2, color);
    phy_gfx_vline(surface, x + width - 1, y + 1, height - 2, color);
}

static void draw_glyph(const phy_surface *surface, int x, int y,
                       unsigned char code, uint16_t color)
{
    if (code < PHY_FONT_FIRST_CHAR || code > PHY_FONT_LAST_CHAR) {
        /* Visible tofu: an unrepresentable byte must never render as blank. */
        phy_gfx_fill_rect(surface, x, y, PHY_GLYPH_WIDTH, PHY_GLYPH_HEIGHT, color);
        return;
    }
    const uint8_t *glyph = kPhyFont5x7[code - PHY_FONT_FIRST_CHAR];
    for (int col = 0; col < PHY_GLYPH_WIDTH; ++col) {
        const uint8_t bits = glyph[col];
        for (int row = 0; row < PHY_GLYPH_HEIGHT; ++row) {
            if (bits & (uint8_t)(1u << row)) {
                phy_gfx_put_pixel(surface, x + col, y + row, color);
            }
        }
    }
}

int phy_gfx_draw_text(const phy_surface *surface, int x, int y,
                      const char *text, uint16_t color)
{
    if (text == NULL) {
        return x;
    }
    int pen = x;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor;
         ++cursor) {
        draw_glyph(surface, pen, y, *cursor, color);
        pen += PHY_GLYPH_ADVANCE;
    }
    return pen;
}

int phy_gfx_text_width(const char *text)
{
    if (text == NULL) {
        return 0;
    }
    const size_t length = strlen(text);
    if (length == 0) {
        return 0;
    }
    /* Trailing spacing column is not part of the inked width. */
    return (int)length * PHY_GLYPH_ADVANCE - (PHY_GLYPH_ADVANCE - PHY_GLYPH_WIDTH);
}

uint64_t phy_gfx_digest(const phy_surface *surface)
{
    /* FNV-1a 64. Hashed byte-wise, low byte first, so the value does not
       depend on host endianness. */
    uint64_t hash = 1469598103934665603ULL;
    if (!surface_valid(surface)) {
        return hash;
    }
    const size_t count = (size_t)surface->width * (size_t)surface->height;
    for (size_t i = 0; i < count; ++i) {
        const uint16_t pixel = surface->pixels[i];
        const uint8_t bytes[2] = {(uint8_t)(pixel & 0xFFu),
                                  (uint8_t)((pixel >> 8) & 0xFFu)};
        for (int b = 0; b < 2; ++b) {
            hash ^= bytes[b];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

bool phy_gfx_write_ppm(const phy_surface *surface, const char *path)
{
    if (!surface_valid(surface) || path == NULL) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    if (fprintf(file, "P6\n%d %d\n255\n", surface->width, surface->height) < 0) {
        fclose(file);
        return false;
    }
    bool ok = true;
    const size_t count = (size_t)surface->width * (size_t)surface->height;
    for (size_t i = 0; i < count && ok; ++i) {
        const uint16_t pixel = surface->pixels[i];
        /* Expand 5/6/5 to 8 bits by bit replication so white stays 0xFFFFFF. */
        const unsigned r5 = (pixel >> 11) & 0x1Fu;
        const unsigned g6 = (pixel >> 5) & 0x3Fu;
        const unsigned b5 = pixel & 0x1Fu;
        const unsigned char rgb[3] = {
            (unsigned char)((r5 << 3) | (r5 >> 2)),
            (unsigned char)((g6 << 2) | (g6 >> 4)),
            (unsigned char)((b5 << 3) | (b5 >> 2)),
        };
        ok = fwrite(rgb, 1, sizeof rgb, file) == sizeof rgb;
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    return ok;
}
