/*
 * Renders the built-in 5x7 font as ASCII art.
 *
 * The font table is hand-written data, so it needs a way to be read back and
 * checked by eye. Run `phy-dump-font` and confirm the glyphs look like their
 * labels; that is the only verification this table gets, and it is enough for
 * a debug font that ships no assets.
 */
#include <stdio.h>
#include <string.h>

#include "phy/gfx.h"
#include "phy/platform.h"

#define CELL_W PHY_GLYPH_ADVANCE
#define CELL_H (PHY_GLYPH_HEIGHT + 1)
#define COLUMNS 16

int main(int argc, char **argv)
{
    const char *sample = NULL;
    if (argc == 2) {
        sample = argv[1];
    } else if (argc > 2) {
        fprintf(stderr, "usage: %s [text]\n", argv[0]);
        return 2;
    }

    if (sample != NULL) {
        const int width = phy_gfx_text_width(sample) + 2;
        const int height = PHY_GLYPH_HEIGHT;
        static uint16_t pixels[PHY_SCREEN_PIXELS];
        if ((size_t)width * (size_t)height > PHY_SCREEN_PIXELS) {
            fprintf(stderr, "sample too wide\n");
            return 2;
        }
        memset(pixels, 0, (size_t)width * (size_t)height * sizeof pixels[0]);
        const phy_surface surface = {pixels, width, height};
        phy_gfx_draw_text(&surface, 0, 0, sample, PHY_COLOR_WHITE);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                putchar(phy_gfx_get_pixel(&surface, x, y) ? '#' : '.');
            }
            putchar('\n');
        }
        return 0;
    }

    /* Full table, 16 glyphs per row, labelled by code point. */
    static uint16_t pixels[PHY_SCREEN_PIXELS];
    for (int base = 0x20; base <= 0x7E; base += COLUMNS) {
        char row[COLUMNS + 1];
        int count = 0;
        for (int c = base; c < base + COLUMNS && c <= 0x7E; ++c) {
            row[count++] = (char)c;
        }
        row[count] = '\0';

        const int width = count * CELL_W;
        memset(pixels, 0, (size_t)width * CELL_H * sizeof pixels[0]);
        const phy_surface surface = {pixels, width, CELL_H};
        phy_gfx_draw_text(&surface, 0, 0, row, PHY_COLOR_WHITE);

        printf("0x%02X  %s\n", base, row);
        for (int y = 0; y < PHY_GLYPH_HEIGHT; ++y) {
            fputs("      ", stdout);
            for (int x = 0; x < width; ++x) {
                putchar(phy_gfx_get_pixel(&surface, x, y) ? '#' : '.');
            }
            putchar('\n');
        }
        putchar('\n');
    }
    return 0;
}
