#include "phy/notebook.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "phy/ir.h"
#include "notebook_internal.h"

#define DOCUMENT_HEADER_BYTES 32u
#define DOCUMENT_RECORD_BYTES 20u
#define DOCUMENT_VERSION 1u
#define DOCUMENT_FLAG_STALE 1u
#define DOCUMENT_NO_OWNER UINT16_MAX

static const uint8_t kDocumentMagic[8] = {
    'P', 'H', 'Y', 'N', 'B', '0', '0', '1',
};

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

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8u) & 0xffu);
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

static phy_status expression_text_size(const phy_notebook *notebook,
                                       const notebook_cell *cell,
                                       size_t *out_size)
{
    *out_size = 0u;
    if (cell->expression == PHY_IR_NULL ||
        cell->kind == PHY_NOTEBOOK_CELL_INPUT) {
        return PHY_OK;
    }
    char probe[1];
    size_t length = 0u;
    const phy_status status =
        phy_ir_write(notebook->ir, cell->expression, probe, sizeof probe,
                     &length);
    if (status != PHY_OK && status != PHY_ERR_INVALID_ARGUMENT) {
        return status;
    }
    if (length >= UINT32_MAX) {
        return PHY_ERR_TERM_LIMIT;
    }
    *out_size = length + 1u; /* serialized expression includes its NUL */
    return PHY_OK;
}

static phy_status document_required_size(const phy_notebook *notebook,
                                         size_t *out_size)
{
    size_t required = DOCUMENT_HEADER_BYTES;
    for (size_t i = 0u; i < notebook->count; ++i) {
        const notebook_cell *cell = &notebook->cells[i];
        const size_t primary = strlen(cell->primary);
        const size_t secondary = strlen(cell->secondary);
        size_t expression = 0u;
        phy_status status =
            expression_text_size(notebook, cell, &expression);
        if (status != PHY_OK) {
            return status;
        }
        if (primary >= NOTEBOOK_TEXT_CAPACITY ||
            secondary >= NOTEBOOK_DETAIL_CAPACITY ||
            required > PHY_NOTEBOOK_DOCUMENT_MAX_BYTES -
                           DOCUMENT_RECORD_BYTES ||
            primary + secondary + expression >
                PHY_NOTEBOOK_DOCUMENT_MAX_BYTES - required -
                    DOCUMENT_RECORD_BYTES) {
            return PHY_ERR_TERM_LIMIT;
        }
        required += DOCUMENT_RECORD_BYTES + primary + secondary + expression;
    }
    *out_size = required;
    return PHY_OK;
}

static void write_record_header(uint8_t *record, const notebook_cell *cell,
                                uint16_t primary, uint16_t secondary,
                                uint32_t expression)
{
    record[0] = (uint8_t)cell->kind;
    record[1] = cell->stale ? DOCUMENT_FLAG_STALE : 0u;
    write_u16(record + 2u, (uint16_t)cell->status);
    write_u32(record + 4u, cell->execution);
    write_u16(record + 8u,
              (cell->kind == PHY_NOTEBOOK_CELL_OUTPUT ||
               cell->kind == PHY_NOTEBOOK_CELL_ERROR)
                  ? (uint16_t)cell->owner_input
                  : DOCUMENT_NO_OWNER);
    write_u16(record + 10u, primary);
    write_u16(record + 12u, secondary);
    write_u16(record + 14u, 0u);
    write_u32(record + 16u, expression);
}

phy_status phy_notebook_serialize(const phy_notebook *notebook,
                                  uint8_t *buffer, size_t capacity,
                                  size_t *out_size)
{
    if (notebook == NULL || out_size == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    size_t required = 0u;
    phy_status status = document_required_size(notebook, &required);
    if (status != PHY_OK) {
        return status;
    }
    *out_size = required;
    if (buffer == NULL || capacity < required) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    memset(buffer, 0, DOCUMENT_HEADER_BYTES);
    memcpy(buffer, kDocumentMagic, sizeof kDocumentMagic);
    write_u16(buffer + 8u, DOCUMENT_VERSION);
    write_u16(buffer + 10u, DOCUMENT_HEADER_BYTES);
    write_u32(buffer + 12u, (uint32_t)(required - DOCUMENT_HEADER_BYTES));
    write_u16(buffer + 16u, (uint16_t)notebook->count);
    write_u16(buffer + 18u, 0u);
    write_u32(buffer + 20u, notebook->next_execution);
    write_u32(buffer + 28u, 0u);

    size_t at = DOCUMENT_HEADER_BYTES;
    for (size_t i = 0u; i < notebook->count; ++i) {
        const notebook_cell *cell = &notebook->cells[i];
        const size_t primary = strlen(cell->primary);
        const size_t secondary = strlen(cell->secondary);
        size_t expression = 0u;
        status = expression_text_size(notebook, cell, &expression);
        if (status != PHY_OK) {
            return status;
        }
        write_record_header(buffer + at, cell, (uint16_t)primary,
                            (uint16_t)secondary, (uint32_t)expression);
        at += DOCUMENT_RECORD_BYTES;
        memcpy(buffer + at, cell->primary, primary);
        at += primary;
        memcpy(buffer + at, cell->secondary, secondary);
        at += secondary;
        if (expression != 0u) {
            size_t written = 0u;
            status = phy_ir_write(
                notebook->ir, cell->expression, (char *)(buffer + at),
                expression, &written);
            if (status != PHY_OK || written + 1u != expression) {
                return status == PHY_OK ? PHY_ERR_CORRUPT_DOCUMENT : status;
            }
            at += expression;
        }
    }
    if (at != required) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    write_u32(buffer + 24u,
              crc32_bytes(buffer + DOCUMENT_HEADER_BYTES,
                          required - DOCUMENT_HEADER_BYTES));
    return PHY_OK;
}

static bool valid_kind(uint8_t kind)
{
    return kind <= (uint8_t)PHY_NOTEBOOK_CELL_ERROR;
}

static bool valid_cell_shape(const phy_notebook *notebook, size_t index,
                             const notebook_cell *cell)
{
    if (cell->kind == PHY_NOTEBOOK_CELL_OUTPUT ||
        cell->kind == PHY_NOTEBOOK_CELL_ERROR) {
        return index > 0u && cell->owner_input == index - 1u &&
               notebook->cells[index - 1u].kind == PHY_NOTEBOOK_CELL_INPUT;
    }
    return true;
}

static phy_status parse_cell_expression(phy_notebook *notebook,
                                        notebook_cell *cell,
                                        const uint8_t *expression,
                                        uint32_t expression_size)
{
    if (cell->kind == PHY_NOTEBOOK_CELL_INPUT) {
        if (cell->secondary[0] == '\0') {
            cell->expression = PHY_IR_NULL;
            return PHY_OK;
        }
        return phy_ir_read(notebook->ir, cell->secondary, &cell->expression,
                           NULL);
    }
    if (expression_size == 0u) {
        cell->expression = PHY_IR_NULL;
        return cell->kind == PHY_NOTEBOOK_CELL_OUTPUT && cell->status == PHY_OK
                   ? PHY_ERR_CORRUPT_DOCUMENT
                   : PHY_OK;
    }
    if (expression[expression_size - 1u] != '\0') {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    return phy_ir_read(notebook->ir, (const char *)expression,
                       &cell->expression, NULL);
}

phy_status phy_notebook_deserialize(const uint8_t *buffer, size_t size,
                                    phy_notebook **out_notebook)
{
    if (buffer == NULL || out_notebook == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_notebook = NULL;
    if (size < DOCUMENT_HEADER_BYTES ||
        size > PHY_NOTEBOOK_DOCUMENT_MAX_BYTES ||
        memcmp(buffer, kDocumentMagic, sizeof kDocumentMagic) != 0 ||
        read_u16(buffer + 8u) != DOCUMENT_VERSION ||
        read_u16(buffer + 10u) != DOCUMENT_HEADER_BYTES ||
        read_u16(buffer + 18u) != 0u || read_u32(buffer + 28u) != 0u) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    const uint32_t payload = read_u32(buffer + 12u);
    const uint16_t cell_count = read_u16(buffer + 16u);
    const uint32_t next_execution = read_u32(buffer + 20u);
    if ((size_t)payload != size - DOCUMENT_HEADER_BYTES ||
        cell_count > PHY_NOTEBOOK_MAX_CELLS || next_execution == 0u ||
        read_u32(buffer + 24u) !=
            crc32_bytes(buffer + DOCUMENT_HEADER_BYTES, payload)) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    phy_notebook *notebook = phy_notebook_create();
    if (notebook == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    notebook->next_execution = next_execution;
    size_t at = DOCUMENT_HEADER_BYTES;
    phy_status status = PHY_OK;
    for (size_t i = 0u; i < cell_count && status == PHY_OK; ++i) {
        if (at > size || size - at < DOCUMENT_RECORD_BYTES) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
            break;
        }
        const uint8_t *record = buffer + at;
        const uint8_t kind = record[0];
        const uint8_t flags = record[1];
        const uint16_t raw_status = read_u16(record + 2u);
        const uint16_t owner = read_u16(record + 8u);
        const uint16_t primary = read_u16(record + 10u);
        const uint16_t secondary = read_u16(record + 12u);
        const uint16_t reserved = read_u16(record + 14u);
        const uint32_t expression = read_u32(record + 16u);
        at += DOCUMENT_RECORD_BYTES;

        const size_t data_size =
            (size_t)primary + (size_t)secondary + (size_t)expression;
        if (!valid_kind(kind) || (flags & ~DOCUMENT_FLAG_STALE) != 0u ||
            raw_status >= PHY_STATUS_COUNT || reserved != 0u ||
            primary >= NOTEBOOK_TEXT_CAPACITY ||
            secondary >= NOTEBOOK_DETAIL_CAPACITY || data_size > size - at) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
            break;
        }

        notebook_cell *cell = &notebook->cells[i];
        memset(cell, 0, sizeof *cell);
        cell->kind = (phy_notebook_cell_kind)kind;
        cell->stale = (flags & DOCUMENT_FLAG_STALE) != 0u;
        cell->status = (phy_status)raw_status;
        cell->execution = read_u32(record + 4u);
        cell->owner_input =
            owner == DOCUMENT_NO_OWNER ? 0u : (size_t)owner;
        memcpy(cell->primary, buffer + at, primary);
        cell->primary[primary] = '\0';
        at += primary;
        memcpy(cell->secondary, buffer + at, secondary);
        cell->secondary[secondary] = '\0';
        at += secondary;
        status = parse_cell_expression(notebook, cell, buffer + at, expression);
        at += expression;
        notebook->count = i + 1u;
        if (status == PHY_OK && !valid_cell_shape(notebook, i, cell)) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        }
    }
    if (status == PHY_OK && at != size) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    if (status == PHY_OK) {
        status = phy_ir_validate(notebook->ir);
    }
    if (status != PHY_OK) {
        phy_notebook_destroy(notebook);
        return status == PHY_ERR_OUT_OF_MEMORY ? status
                                               : PHY_ERR_CORRUPT_DOCUMENT;
    }
    notebook->selected = 0u;
    notebook->editing = false;
    notebook->scroll_y = 0;
    notebook->dirty = false;
    *out_notebook = notebook;
    return PHY_OK;
}
