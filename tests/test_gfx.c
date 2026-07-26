/* Drawing primitives: clipping, text metrics, digest stability. */
#include <stdlib.h>

#include "phy/gfx.h"
#include "phy/platform.h"
#include "phy_test.h"

#define W 32
#define H 16

static uint16_t g_pixels[W * H];
static const phy_surface g_surface = {g_pixels, W, H};

static const uint16_t kInk = PHY_RGB565(255, 255, 255);
static const uint16_t kBg = PHY_RGB565(0, 0, 0);

static unsigned count_ink(void)
{
    unsigned total = 0;
    for (int i = 0; i < W * H; ++i) {
        if (g_pixels[i] != kBg) {
            total++;
        }
    }
    return total;
}

static void test_rgb565_packing(void)
{
    PHY_CHECK_EQ_INT(PHY_RGB565(0, 0, 0), 0x0000);
    PHY_CHECK_EQ_INT(PHY_RGB565(255, 255, 255), 0xFFFF);
    PHY_CHECK_EQ_INT(PHY_RGB565(255, 0, 0), 0xF800);
    PHY_CHECK_EQ_INT(PHY_RGB565(0, 255, 0), 0x07E0);
    PHY_CHECK_EQ_INT(PHY_RGB565(0, 0, 255), 0x001F);
}

static void test_clear_and_pixel(void)
{
    phy_gfx_clear(&g_surface, kBg);
    PHY_CHECK_EQ_INT(count_ink(), 0);

    phy_gfx_put_pixel(&g_surface, 3, 4, kInk);
    PHY_CHECK_EQ_INT(phy_gfx_get_pixel(&g_surface, 3, 4), kInk);
    PHY_CHECK_EQ_INT(count_ink(), 1);
}

/* Out-of-bounds writes must be dropped, not wrapped into the next row. */
static void test_pixel_clipping(void)
{
    phy_gfx_clear(&g_surface, kBg);
    phy_gfx_put_pixel(&g_surface, -1, 0, kInk);
    phy_gfx_put_pixel(&g_surface, 0, -1, kInk);
    phy_gfx_put_pixel(&g_surface, W, 0, kInk);
    phy_gfx_put_pixel(&g_surface, 0, H, kInk);
    phy_gfx_put_pixel(&g_surface, W + 100, H + 100, kInk);
    PHY_CHECK_EQ_INT(count_ink(), 0);
    PHY_CHECK_EQ_INT(phy_gfx_get_pixel(&g_surface, -1, -1), 0);
}

static void test_fill_rect_clipping(void)
{
    /* Straddles the top-left corner: only the in-bounds quadrant survives. */
    phy_gfx_clear(&g_surface, kBg);
    phy_gfx_fill_rect(&g_surface, -2, -3, 5, 6, kInk);
    PHY_CHECK_EQ_INT(count_ink(), 3 * 3);
    PHY_CHECK_EQ_INT(phy_gfx_get_pixel(&g_surface, 0, 0), kInk);
    PHY_CHECK_EQ_INT(phy_gfx_get_pixel(&g_surface, 3, 0), kBg);

    /* Straddles the bottom-right corner. */
    phy_gfx_clear(&g_surface, kBg);
    phy_gfx_fill_rect(&g_surface, W - 2, H - 3, 10, 10, kInk);
    PHY_CHECK_EQ_INT(count_ink(), 2 * 3);

    /* Fully outside, degenerate, and negative extents draw nothing. */
    phy_gfx_clear(&g_surface, kBg);
    phy_gfx_fill_rect(&g_surface, W + 1, 0, 4, 4, kInk);
    phy_gfx_fill_rect(&g_surface, 0, H + 1, 4, 4, kInk);
    phy_gfx_fill_rect(&g_surface, 0, 0, 0, 4, kInk);
    phy_gfx_fill_rect(&g_surface, 0, 0, 4, 0, kInk);
    phy_gfx_fill_rect(&g_surface, 0, 0, -4, -4, kInk);
    PHY_CHECK_EQ_INT(count_ink(), 0);
}

static void test_draw_rect_outline(void)
{
    phy_gfx_clear(&g_surface, kBg);
    phy_gfx_draw_rect(&g_surface, 2, 2, 6, 5, kInk);
    /* Perimeter of a 6x5 rectangle: 2*6 + 2*(5-2) = 18 pixels, corners once. */
    PHY_CHECK_EQ_INT(count_ink(), 18);
    PHY_CHECK_EQ_INT(phy_gfx_get_pixel(&g_surface, 2, 2), kInk);
    PHY_CHECK_EQ_INT(phy_gfx_get_pixel(&g_surface, 7, 6), kInk);
    PHY_CHECK_EQ_INT(phy_gfx_get_pixel(&g_surface, 4, 4), kBg); /* hollow */
}

static void test_text_metrics(void)
{
    PHY_CHECK_EQ_INT(phy_gfx_text_width(NULL), 0);
    PHY_CHECK_EQ_INT(phy_gfx_text_width(""), 0);
    PHY_CHECK_EQ_INT(phy_gfx_text_width("A"), PHY_GLYPH_WIDTH);
    PHY_CHECK_EQ_INT(phy_gfx_text_width("AB"), PHY_GLYPH_ADVANCE + PHY_GLYPH_WIDTH);

    phy_gfx_clear(&g_surface, kBg);
    const int pen = phy_gfx_draw_text(&g_surface, 1, 1, "AB", kInk);
    PHY_CHECK_EQ_INT(pen, 1 + 2 * PHY_GLYPH_ADVANCE);
    PHY_CHECK(count_ink() > 0);
}

/* A space must be blank; an unmapped byte must be visibly filled, not blank. */
static void test_text_glyph_coverage(void)
{
    phy_gfx_clear(&g_surface, kBg);
    phy_gfx_draw_text(&g_surface, 0, 0, " ", kInk);
    PHY_CHECK_EQ_INT(count_ink(), 0);

    phy_gfx_clear(&g_surface, kBg);
    phy_gfx_draw_text(&g_surface, 0, 0, "\x01", kInk);
    PHY_CHECK_EQ_INT(count_ink(), PHY_GLYPH_WIDTH * PHY_GLYPH_HEIGHT);
}

static void test_text_clipping(void)
{
    /* Drawing off every edge must not corrupt memory or wrap rows. */
    phy_gfx_clear(&g_surface, kBg);
    phy_gfx_draw_text(&g_surface, -100, -100, "clipped", kInk);
    phy_gfx_draw_text(&g_surface, W + 10, H + 10, "clipped", kInk);
    PHY_CHECK_EQ_INT(count_ink(), 0);

    /* A long run starting in bounds is truncated at the right edge. */
    phy_gfx_clear(&g_surface, kBg);
    phy_gfx_draw_text(&g_surface, W - 3, 0, "MMMMMMMM", kInk);
    for (int y = 0; y < H; ++y) {
        PHY_CHECK_EQ_INT(phy_gfx_get_pixel(&g_surface, 0, y), kBg);
    }
}

static void test_digest_is_content_addressed(void)
{
    phy_gfx_clear(&g_surface, kBg);
    const uint64_t blank = phy_gfx_digest(&g_surface);

    phy_gfx_clear(&g_surface, kBg);
    PHY_CHECK(phy_gfx_digest(&g_surface) == blank);

    phy_gfx_put_pixel(&g_surface, 0, 0, kInk);
    PHY_CHECK(phy_gfx_digest(&g_surface) != blank);

    phy_gfx_put_pixel(&g_surface, 0, 0, kBg);
    PHY_CHECK(phy_gfx_digest(&g_surface) == blank);
}

static void test_null_surface_is_safe(void)
{
    const phy_surface null_surface = {NULL, W, H};
    phy_gfx_clear(&null_surface, kInk);
    phy_gfx_fill_rect(&null_surface, 0, 0, 4, 4, kInk);
    phy_gfx_draw_text(&null_surface, 0, 0, "x", kInk);
    PHY_CHECK_EQ_INT(phy_gfx_get_pixel(&null_surface, 0, 0), 0);
    PHY_CHECK(!phy_gfx_write_ppm(&null_surface, "should-not-exist.ppm"));
}

int main(void)
{
    PHY_TEST_CASE(test_rgb565_packing);
    PHY_TEST_CASE(test_clear_and_pixel);
    PHY_TEST_CASE(test_pixel_clipping);
    PHY_TEST_CASE(test_fill_rect_clipping);
    PHY_TEST_CASE(test_draw_rect_outline);
    PHY_TEST_CASE(test_text_metrics);
    PHY_TEST_CASE(test_text_glyph_coverage);
    PHY_TEST_CASE(test_text_clipping);
    PHY_TEST_CASE(test_digest_is_content_addressed);
    PHY_TEST_CASE(test_null_surface_is_safe);
    return PHY_TEST_REPORT("test_gfx");
}
