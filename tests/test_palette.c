#include <string.h>

#include "phy/palette.h"
#include "phy/platform.h"
#include "phy/source.h"
#include "phy_test.h"

static void test_catalog_bounds_and_representative_entries(void)
{
    PHY_CHECK_EQ_INT(phy_palette_category_count(PHY_PALETTE_CAS), 6);
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
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 3u, 4u, &entry));
    PHY_CHECK(strstr(entry.snippet, "ComponentTensor[M") != NULL);
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 1u, 15u, &entry));
    PHY_CHECK_EQ_STR(entry.snippet, "Re[]");
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 1u, 18u, &entry));
    PHY_CHECK_EQ_STR(entry.snippet, "Abs[]");
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 4u, 0u, &entry));
    PHY_CHECK_EQ_STR(entry.snippet, "M = Manifold[{t,x,y,z},Lorentzian]");
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 4u, 6u, &entry));
    PHY_CHECK_EQ_STR(entry.snippet, "LieDerivative[a,v]");
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 4u, 7u, &entry));
    PHY_CHECK_EQ_STR(entry.snippet, "HodgeStar[a,g]");
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 5u, 0u, &entry));
    PHY_CHECK_EQ_STR(entry.snippet, "G = LieGroup[SU2]");
    PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, 5u, 7u, &entry));
    PHY_CHECK_EQ_STR(entry.snippet, "F = FieldStrength[A,g]");
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

/*
 * Every CAS snippet must be readable by the front end it is inserted into. A
 * palette entry the parser rejects would hand the reader a cell that cannot
 * run, and the only way to notice would be to click it.
 */
static void test_every_cas_snippet_parses(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    const size_t categories = phy_palette_category_count(PHY_PALETTE_CAS);
    for (size_t category = 0u; category < categories; ++category) {
        const size_t entries =
            phy_palette_entry_count(PHY_PALETTE_CAS, category);
        for (size_t item = 0u; item < entries; ++item) {
            phy_palette_entry entry;
            PHY_CHECK(phy_palette_get(PHY_PALETTE_CAS, category, item,
                                      &entry));
            /*
             * Entries with an empty argument slot are templates the reader
             * fills in, so only complete snippets are parsed.
             */
            if (strstr(entry.snippet, "[]") != NULL ||
                strstr(entry.snippet, "[,") != NULL ||
                strstr(entry.snippet, ",]") != NULL ||
                strstr(entry.snippet, "{}") != NULL) {
                continue;
            }
            phy_ir_context *ir = phy_ir_context_create(NULL);
            PHY_CHECK(ir != NULL);
            phy_source_command command;
            size_t offset = 0u;
            const phy_status status =
                phy_source_parse(ir, entry.snippet, &command, &offset);
            if (status != PHY_OK) {
                fprintf(stderr, "  unparsable snippet: %s\n", entry.snippet);
            }
            PHY_CHECK_EQ_INT(status, PHY_OK);
            phy_ir_context_destroy(ir);
        }
    }
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_catalog_bounds_and_representative_entries);
    PHY_TEST_CASE(test_every_cursor_is_inside_its_snippet);
    PHY_TEST_CASE(test_every_cas_snippet_parses);
    return PHY_TEST_REPORT("test_palette");
}
