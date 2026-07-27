#include <string.h>

#include "phy/palette.h"
#include "phy_test.h"

static void test_catalog_bounds_and_representative_entries(void)
{
    PHY_CHECK_EQ_INT(phy_palette_category_count(PHY_PALETTE_CAS), 4);
    PHY_CHECK_EQ_INT(phy_palette_category_count(PHY_PALETTE_LATEX), 6);
    PHY_CHECK_EQ_STR(phy_palette_category_name(PHY_PALETTE_CAS, 0u),
                     "Algebra");
    PHY_CHECK(phy_palette_category_name(PHY_PALETTE_CAS, 99u) == NULL);

    phy_palette_entry entry;
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 0u, 0u, &entry));
    PHY_CHECK_EQ_STR(entry.snippet, "Simplify[]");
    PHY_CHECK_EQ_INT(entry.cursor_offset, strlen("Simplify["));

    PHY_CHECK(phy_palette_get(PHY_PALETTE_LATEX, 0u, 2u, &entry));
    PHY_CHECK_EQ_STR(entry.snippet, "\\frac{}{}");
    PHY_CHECK_EQ_INT(entry.cursor_offset, strlen("\\frac{"));

    PHY_CHECK(phy_palette_get(PHY_PALETTE_LATEX, 5u, 0u, &entry));
    PHY_CHECK(strstr(entry.snippet, "\\begin{matrix}") != NULL);
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 3u, 2u, &entry));
    PHY_CHECK(strstr(entry.snippet, "Tensor[g") != NULL);
    PHY_CHECK(!phy_palette_get(PHY_PALETTE_CAS, 20u, 0u, &entry));
    PHY_CHECK(!phy_palette_get(PHY_PALETTE_CAS, 0u, 20u, &entry));
    PHY_CHECK(!phy_palette_get(PHY_PALETTE_CAS, 0u, 0u, NULL));
}

static void test_every_cursor_is_inside_its_snippet(void)
{
    const phy_palette_kind kinds[] = {PHY_PALETTE_CAS, PHY_PALETTE_LATEX};
    for (size_t kind = 0u; kind < sizeof kinds / sizeof kinds[0]; ++kind) {
        const size_t categories = phy_palette_category_count(kinds[kind]);
        for (size_t category = 0u; category < categories; ++category) {
            const size_t entries =
                phy_palette_entry_count(kinds[kind], category);
            PHY_CHECK(entries > 0u);
            for (size_t item = 0u; item < entries; ++item) {
                phy_palette_entry entry;
                PHY_CHECK(
                    phy_palette_get(kinds[kind], category, item, &entry));
                PHY_CHECK(entry.label != NULL);
                PHY_CHECK(entry.snippet != NULL);
                PHY_CHECK(entry.cursor_offset <= strlen(entry.snippet));
            }
        }
    }
}

int main(void)
{
    PHY_TEST_CASE(test_catalog_bounds_and_representative_entries);
    PHY_TEST_CASE(test_every_cursor_is_inside_its_snippet);
    return PHY_TEST_REPORT("test_palette");
}
