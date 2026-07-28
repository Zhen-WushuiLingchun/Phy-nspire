#include <stdint.h>
#include <string.h>

#include "phy/notebook.h"
#include "phy/platform.h"
#include "phy_test.h"

#define HEADER_BYTES 32u
#define RECORD_BYTES 20u

static uint8_t g_document[PHY_NOTEBOOK_DOCUMENT_MAX_BYTES + 1u];
static char g_expression[512];

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      (uint16_t)((uint16_t)bytes[1] << 8u));
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8u) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16u) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24u) & 0xffu);
}

static uint32_t crc32_bytes(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static void refresh_crc(uint8_t *document)
{
    const uint32_t payload = read_u32(document + 12u);
    write_u32(document + 24u,
              crc32_bytes(document + HEADER_BYTES, payload));
}

static size_t record_at(const uint8_t *document, size_t wanted)
{
    size_t at = HEADER_BYTES;
    for (size_t i = 0u; i < wanted; ++i) {
        const uint16_t primary = read_u16(document + at + 10u);
        const uint16_t secondary = read_u16(document + at + 12u);
        const uint32_t expression = read_u32(document + at + 16u);
        at += RECORD_BYTES + primary + secondary + expression;
    }
    return at;
}

static const char *expression_text(const phy_notebook *notebook,
                                   size_t index)
{
    phy_notebook_cell_view cell;
    PHY_CHECK(phy_notebook_cell(notebook, index, &cell));
    size_t length = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(phy_notebook_ir(notebook), cell.expression, g_expression,
                     sizeof g_expression, &length),
        PHY_OK);
    return g_expression;
}

static phy_notebook *make_sample(void)
{
    phy_notebook *notebook = phy_notebook_create();
    PHY_CHECK(notebook != NULL);
    PHY_CHECK(!phy_notebook_is_dirty(notebook));

    PHY_CHECK_EQ_INT(
        phy_notebook_add_markdown(
            notebook, "Curvature", "Inline $R_{mu nu}$ and display math", NULL),
        PHY_OK);
    size_t input = 0u;
    PHY_CHECK_EQ_INT(phy_notebook_add_input(
                         notebook, "Simplify[(x+m)^3]", &input),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_notebook_evaluate(notebook, input), PHY_OK);
    PHY_CHECK_EQ_STR(expression_text(notebook, input + 1u),
                     "(^ (+ m x) 3)");

    phy_notebook_mark_clean(notebook);
    PHY_CHECK(!phy_notebook_is_dirty(notebook));
    PHY_CHECK(phy_notebook_select(notebook, input));
    PHY_CHECK(phy_notebook_begin_edit_selected(notebook));
    PHY_CHECK(phy_notebook_edit_insert(notebook, ' '));
    phy_notebook_end_edit(notebook);
    PHY_CHECK(phy_notebook_is_dirty(notebook));

    size_t unsupported = 0u;
    PHY_CHECK_EQ_INT(
        phy_notebook_add_input(notebook, "Apart[1/(x^2-1)]", &unsupported),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_notebook_evaluate(notebook, unsupported),
                     PHY_ERR_UNSUPPORTED);
    return notebook;
}

static size_t serialize_sample(phy_notebook *notebook)
{
    size_t required = 0u;
    PHY_CHECK_EQ_INT(
        phy_notebook_serialize(notebook, NULL, 0u, &required),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(required >= HEADER_BYTES);
    PHY_CHECK(required <= PHY_NOTEBOOK_DOCUMENT_MAX_BYTES);
    PHY_CHECK_EQ_INT(phy_notebook_serialize(notebook, g_document, required - 1u,
                                           &required),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_notebook_serialize(notebook, g_document,
                                           sizeof g_document, &required),
                     PHY_OK);
    return required;
}

static void test_round_trip_preserves_cells_and_cached_ir(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *source = make_sample();
    const size_t bytes = serialize_sample(source);

    phy_notebook *loaded = NULL;
    PHY_CHECK_EQ_INT(
        phy_notebook_deserialize(g_document, bytes, &loaded), PHY_OK);
    PHY_CHECK(loaded != NULL);
    PHY_CHECK(!phy_notebook_is_dirty(loaded));
    PHY_CHECK_EQ_INT(phy_notebook_cell_count(loaded),
                     phy_notebook_cell_count(source));

    for (size_t i = 0u; i < phy_notebook_cell_count(source); ++i) {
        phy_notebook_cell_view before;
        phy_notebook_cell_view after;
        PHY_CHECK(phy_notebook_cell(source, i, &before));
        PHY_CHECK(phy_notebook_cell(loaded, i, &after));
        PHY_CHECK_EQ_INT(after.kind, before.kind);
        PHY_CHECK_EQ_STR(after.primary, before.primary);
        PHY_CHECK_EQ_STR(after.secondary, before.secondary);
        PHY_CHECK_EQ_INT(after.status, before.status);
        PHY_CHECK_EQ_INT(after.execution, before.execution);
        PHY_CHECK_EQ_INT(after.stale, before.stale);
        if (before.expression != PHY_IR_NULL) {
            char before_text[512];
            size_t length = 0u;
            PHY_CHECK_EQ_INT(
                phy_ir_write(phy_notebook_ir(source), before.expression,
                             before_text, sizeof before_text, &length),
                PHY_OK);
            PHY_CHECK_EQ_STR(expression_text(loaded, i), before_text);
        } else {
            PHY_CHECK_EQ_INT(after.expression, PHY_IR_NULL);
        }
    }

    phy_notebook_destroy(loaded);
    phy_notebook_destroy(source);
    phy_platform_shutdown();
}

static void expect_corrupt(const uint8_t *document, size_t bytes)
{
    phy_notebook *loaded = (phy_notebook *)(uintptr_t)1u;
    PHY_CHECK_EQ_INT(phy_notebook_deserialize(document, bytes, &loaded),
                     PHY_ERR_CORRUPT_DOCUMENT);
    PHY_CHECK(loaded == NULL);
}

static void test_header_crc_bounds_and_trailing_bytes(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *source = make_sample();
    const size_t bytes = serialize_sample(source);
    uint8_t saved;

    saved = g_document[0];
    g_document[0] ^= 0xffu;
    expect_corrupt(g_document, bytes);
    g_document[0] = saved;

    saved = g_document[8];
    g_document[8] = 2u;
    expect_corrupt(g_document, bytes);
    g_document[8] = saved;

    expect_corrupt(g_document, bytes - 1u);
    g_document[bytes] = 0u;
    expect_corrupt(g_document, bytes + 1u);

    saved = g_document[HEADER_BYTES + RECORD_BYTES];
    g_document[HEADER_BYTES + RECORD_BYTES] ^= 1u;
    expect_corrupt(g_document, bytes);
    g_document[HEADER_BYTES + RECORD_BYTES] = saved;

    PHY_CHECK_EQ_INT(
        phy_notebook_deserialize(NULL, bytes, NULL),
        PHY_ERR_INVALID_ARGUMENT);
    phy_notebook_destroy(source);
    phy_platform_shutdown();
}

static void test_structural_validation_after_valid_crc(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *source = make_sample();
    const size_t bytes = serialize_sample(source);

    const size_t output_record = record_at(g_document, 2u);
    const uint8_t saved_owner0 = g_document[output_record + 8u];
    const uint8_t saved_owner1 = g_document[output_record + 9u];
    g_document[output_record + 8u] = 0u;
    g_document[output_record + 9u] = 0u;
    refresh_crc(g_document);
    expect_corrupt(g_document, bytes);
    g_document[output_record + 8u] = saved_owner0;
    g_document[output_record + 9u] = saved_owner1;

    /*
     * An output whose stored expression text no longer parses is a version
     * mismatch, not corruption: the CRC above already proved the bytes are
     * intact. The document still opens and the cell carries the parser's
     * status instead of an expression.
     */
    const uint16_t primary = read_u16(g_document + output_record + 10u);
    const uint16_t secondary = read_u16(g_document + output_record + 12u);
    const size_t expression_at =
        output_record + RECORD_BYTES + primary + secondary;
    const uint8_t saved_expression = g_document[expression_at];
    g_document[expression_at] = '?';
    refresh_crc(g_document);
    {
        phy_notebook *degraded = NULL;
        PHY_CHECK_EQ_INT(
            phy_notebook_deserialize(g_document, bytes, &degraded), PHY_OK);
        phy_notebook_cell_view cell;
        PHY_CHECK(phy_notebook_cell(degraded, 2u, &cell));
        PHY_CHECK_EQ_INT(cell.kind, PHY_NOTEBOOK_CELL_OUTPUT);
        PHY_CHECK_EQ_INT(cell.status, PHY_ERR_PARSE);
        PHY_CHECK(cell.stale);
        PHY_CHECK_EQ_INT(cell.expression, PHY_IR_NULL);
        phy_notebook_destroy(degraded);
    }
    g_document[expression_at] = saved_expression;

    refresh_crc(g_document);
    phy_notebook *loaded = NULL;
    PHY_CHECK_EQ_INT(
        phy_notebook_deserialize(g_document, bytes, &loaded), PHY_OK);
    phy_notebook_destroy(loaded);
    phy_notebook_destroy(source);
    phy_platform_shutdown();
}

/*
 * A newer application can save source text an older parser rejects. The
 * document must still open: the failing input keeps its source, reports the
 * parser's typed status, and every other cell loads untouched.
 */
static void test_unparseable_input_source_degrades_to_stale(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *source = make_sample();
    const size_t bytes = serialize_sample(source);

    const size_t input_record = record_at(g_document, 1u);
    const uint16_t primary = read_u16(g_document + input_record + 10u);
    const size_t secondary_at = input_record + RECORD_BYTES + primary;
    const uint8_t saved = g_document[secondary_at];
    g_document[secondary_at] = '?';
    refresh_crc(g_document);

    phy_notebook *loaded = NULL;
    PHY_CHECK_EQ_INT(
        phy_notebook_deserialize(g_document, bytes, &loaded), PHY_OK);
    phy_notebook_cell_view cell;
    PHY_CHECK(phy_notebook_cell(loaded, 1u, &cell));
    PHY_CHECK_EQ_INT(cell.kind, PHY_NOTEBOOK_CELL_INPUT);
    PHY_CHECK_EQ_INT(cell.status, PHY_ERR_PARSE);
    PHY_CHECK(cell.stale);
    PHY_CHECK_EQ_INT(cell.expression, PHY_IR_NULL);
    PHY_CHECK(cell.secondary[0] == '?');
    PHY_CHECK(phy_notebook_cell(loaded, 2u, &cell));
    PHY_CHECK_EQ_INT(cell.kind, PHY_NOTEBOOK_CELL_OUTPUT);
    PHY_CHECK_EQ_INT(cell.status, PHY_OK);
    PHY_CHECK(cell.expression != PHY_IR_NULL);

    g_document[secondary_at] = saved;
    phy_notebook_destroy(loaded);
    phy_notebook_destroy(source);
    phy_platform_shutdown();
}

static void test_empty_document_round_trip(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_notebook *empty = phy_notebook_create();
    PHY_CHECK(empty != NULL);
    const size_t bytes = serialize_sample(empty);
    PHY_CHECK_EQ_INT(bytes, HEADER_BYTES);

    phy_notebook *loaded = NULL;
    PHY_CHECK_EQ_INT(
        phy_notebook_deserialize(g_document, bytes, &loaded), PHY_OK);
    PHY_CHECK_EQ_INT(phy_notebook_cell_count(loaded), 0);
    PHY_CHECK(!phy_notebook_is_dirty(loaded));

    phy_notebook_destroy(loaded);
    phy_notebook_destroy(empty);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_round_trip_preserves_cells_and_cached_ir);
    PHY_TEST_CASE(test_header_crc_bounds_and_trailing_bytes);
    PHY_TEST_CASE(test_structural_validation_after_valid_crc);
    PHY_TEST_CASE(test_unparseable_input_source_degrades_to_stale);
    PHY_TEST_CASE(test_empty_document_round_trip);
    return PHY_TEST_REPORT("test_document");
}
