#include "phy/notebook.h"

#include <string.h>

#include "phy/cas.h"
#include "phy/formula.h"
#include "phy/platform.h"
#include "phy/source.h"
#include "notebook_internal.h"

#define NOTEBOOK_CELL_X 4
#define NOTEBOOK_CELL_WIDTH (PHY_SCREEN_WIDTH - 8)
#define NOTEBOOK_FIRST_CELL_Y 20
#define NOTEBOOK_LAST_CELL_Y (PHY_SCREEN_HEIGHT - 23)
#define NOTEBOOK_CELL_GAP 2
#define NOTEBOOK_RUN_BADGE_WIDTH 29
#define NOTEBOOK_RUN_BADGE_HEIGHT 13
#define NOTEBOOK_RUN_BADGE_INSET 3
#define NOTEBOOK_FOOTER_Y (PHY_SCREEN_HEIGHT - 22)
#define NOTEBOOK_BUTTON_Y (PHY_SCREEN_HEIGHT - 18)
#define NOTEBOOK_BUTTON_HEIGHT 14
#define NOTEBOOK_MD_BUTTON_X 5
#define NOTEBOOK_MD_BUTTON_WIDTH 31
#define NOTEBOOK_MATH_BUTTON_X 40
#define NOTEBOOK_MATH_BUTTON_WIDTH 43

#define COLOR_BACKGROUND PHY_RGB565(12, 15, 22)
#define COLOR_TITLE PHY_RGB565(31, 71, 122)
#define COLOR_CARD PHY_RGB565(24, 29, 40)
#define COLOR_CARD_INPUT PHY_RGB565(29, 36, 50)
#define COLOR_CARD_OUTPUT PHY_RGB565(21, 34, 37)
#define COLOR_CARD_SELECTED PHY_RGB565(35, 48, 67)
#define COLOR_BORDER PHY_RGB565(63, 76, 96)
#define COLOR_ACCENT PHY_RGB565(75, 166, 255)
#define COLOR_MARKDOWN PHY_RGB565(222, 232, 246)
#define COLOR_TEXT PHY_RGB565(235, 239, 246)
#define COLOR_DIM PHY_RGB565(142, 155, 177)
#define COLOR_RESULT PHY_RGB565(116, 225, 177)
#define COLOR_ERROR PHY_RGB565(247, 103, 116)
#define COLOR_POINTER PHY_RGB565(255, 205, 78)

static bool markdown_entire_formula(const char *text,
                                    const char **out_source,
                                    size_t *out_length,
                                    phy_formula_style *out_style)
{
    if (text == NULL) {
        return false;
    }
    const char *begin = text;
    while (*begin == ' ' || *begin == '\t') {
        begin++;
    }
    const char *end = text + strlen(text);
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    size_t opening = 0u;
    size_t closing = 0u;
    phy_formula_style style = PHY_FORMULA_STYLE_TEXT;
    if ((size_t)(end - begin) >= 4u && begin[0] == '$' &&
        begin[1] == '$' && end[-2] == '$' && end[-1] == '$') {
        opening = 2u;
        closing = 2u;
        style = PHY_FORMULA_STYLE_DISPLAY;
    } else if ((size_t)(end - begin) >= 4u && begin[0] == '\\' &&
               begin[1] == '[' && end[-2] == '\\' && end[-1] == ']') {
        opening = 2u;
        closing = 2u;
        style = PHY_FORMULA_STYLE_DISPLAY;
    } else if ((size_t)(end - begin) >= 4u && begin[0] == '\\' &&
               begin[1] == '(' && end[-2] == '\\' && end[-1] == ')') {
        opening = 2u;
        closing = 2u;
    }
    if (opening == 0u) {
        return false;
    }
    if (out_source != NULL) {
        *out_source = begin + opening;
    }
    if (out_length != NULL) {
        *out_length = (size_t)(end - begin) - opening - closing;
    }
    if (out_style != NULL) {
        *out_style = style;
    }
    return true;
}

static int cell_height(const notebook_cell *cell)
{
    switch (cell->kind) {
    case PHY_NOTEBOOK_CELL_MARKDOWN:
        if (markdown_entire_formula(cell->secondary, NULL, NULL, NULL)) {
            const char *source = NULL;
            size_t length = 0u;
            phy_formula_style style = PHY_FORMULA_STYLE_TEXT;
            (void)markdown_entire_formula(cell->secondary, &source, &length,
                                          &style);
            if (style == PHY_FORMULA_STYLE_DISPLAY) {
                phy_formula_metrics metrics;
                int height = 64;
                if (phy_formula_measure_latex(
                        source, length, style, 17,
                        NOTEBOOK_CELL_WIDTH - 42, &metrics) == PHY_OK) {
                    const int measured =
                        27 + metrics.ascent + metrics.descent;
                    if (measured > height) {
                        height = measured;
                    }
                    if (height > 120) {
                        height = 120;
                    }
                }
                return height;
            }
        }
        {
            const char *dollar = strchr(cell->secondary, '$');
            const char *paren = strstr(cell->secondary, "\\(");
            if ((dollar != NULL && strchr(dollar + 1, '$') != NULL) ||
                (paren != NULL && strstr(paren + 2, "\\)") != NULL)) {
                return 45;
            }
        }
        return 37;
    case PHY_NOTEBOOK_CELL_INPUT:
        return 32;
    case PHY_NOTEBOOK_CELL_OUTPUT:
    case PHY_NOTEBOOK_CELL_ERROR:
        return 42;
    default:
        return 32;
    }
}

static int content_top(const phy_notebook *notebook, size_t index)
{
    int y = NOTEBOOK_FIRST_CELL_Y;
    for (size_t i = 0u; i < index && i < notebook->count; ++i) {
        y += cell_height(&notebook->cells[i]) + NOTEBOOK_CELL_GAP;
    }
    return y;
}

static int screen_top(const phy_notebook *notebook, size_t index)
{
    return content_top(notebook, index) - notebook->scroll_y;
}

static void ensure_selected_visible(phy_notebook *notebook)
{
    if (notebook == NULL || notebook->count == 0u ||
        notebook->selected >= notebook->count) {
        return;
    }
    const int top = content_top(notebook, notebook->selected);
    const int bottom =
        top + cell_height(&notebook->cells[notebook->selected]);
    if (top - notebook->scroll_y < NOTEBOOK_FIRST_CELL_Y) {
        notebook->scroll_y = top - NOTEBOOK_FIRST_CELL_Y;
    } else if (bottom - notebook->scroll_y > NOTEBOOK_LAST_CELL_Y) {
        notebook->scroll_y = bottom - NOTEBOOK_LAST_CELL_Y;
    }
    if (notebook->scroll_y < 0) {
        notebook->scroll_y = 0;
    }
}

static phy_status copy_text(char *destination, size_t capacity,
                            const char *source)
{
    if (destination == NULL || capacity == 0u || source == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const size_t length = strlen(source);
    if (length >= capacity) {
        return PHY_ERR_TERM_LIMIT;
    }
    memcpy(destination, source, length + 1u);
    return PHY_OK;
}

static bool is_output_kind(phy_notebook_cell_kind kind)
{
    return kind == PHY_NOTEBOOK_CELL_OUTPUT ||
           kind == PHY_NOTEBOOK_CELL_ERROR;
}

static phy_status insert_cell(phy_notebook *notebook, size_t index,
                              const notebook_cell *cell)
{
    if (notebook == NULL || cell == NULL || index > notebook->count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (notebook->count >= PHY_NOTEBOOK_MAX_CELLS) {
        return PHY_ERR_TERM_LIMIT;
    }
    for (size_t i = notebook->count; i > index; --i) {
        notebook->cells[i] = notebook->cells[i - 1u];
        if (is_output_kind(notebook->cells[i].kind) &&
            notebook->cells[i].owner_input >= index) {
            notebook->cells[i].owner_input++;
        }
    }
    notebook->cells[index] = *cell;
    notebook->count++;
    notebook->dirty = true;
    if (notebook->selected >= index && notebook->count > 1u) {
        notebook->selected++;
    }
    if (notebook->editing && notebook->edit_index >= index) {
        notebook->edit_index++;
    }
    return PHY_OK;
}

phy_notebook *phy_notebook_create(void)
{
    phy_notebook *notebook = phy_alloc(sizeof *notebook);
    if (notebook == NULL) {
        return NULL;
    }
    memset(notebook, 0, sizeof *notebook);
    notebook->next_execution = 1u;

    phy_ir_limits ir_limits;
    phy_ir_limits_defaults(&ir_limits);
    ir_limits.max_nodes = 4096u;
    ir_limits.max_depth = 64u;
    ir_limits.max_children = 256u;
    ir_limits.max_bytes = 512u * 1024u;
    notebook->ir = phy_ir_context_create(&ir_limits);
    if (notebook->ir == NULL) {
        phy_free(notebook, sizeof *notebook);
        return NULL;
    }

    phy_cas_limits cas_limits;
    phy_cas_limits_defaults(&cas_limits);
    cas_limits.max_steps = 50000u;
    cas_limits.max_bytes = 128u * 1024u;
    notebook->cas = phy_cas_create(notebook->ir, &cas_limits);
    if (notebook->cas == NULL) {
        phy_ir_context_destroy(notebook->ir);
        phy_free(notebook, sizeof *notebook);
        return NULL;
    }
    return notebook;
}

void phy_notebook_destroy(phy_notebook *notebook)
{
    if (notebook == NULL) {
        return;
    }
    phy_cas_destroy(notebook->cas);
    phy_ir_context_destroy(notebook->ir);
    phy_free(notebook, sizeof *notebook);
}

phy_status phy_notebook_add_markdown(phy_notebook *notebook,
                                     const char *heading, const char *body,
                                     size_t *out_index)
{
    if (notebook == NULL || heading == NULL || body == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    notebook_cell cell;
    memset(&cell, 0, sizeof cell);
    cell.kind = PHY_NOTEBOOK_CELL_MARKDOWN;
    cell.status = PHY_OK;
    phy_status status = copy_text(cell.primary, sizeof cell.primary, heading);
    if (status == PHY_OK) {
        status = copy_text(cell.secondary, sizeof cell.secondary, body);
    }
    if (status != PHY_OK) {
        return status;
    }
    const size_t index = notebook->count;
    status = insert_cell(notebook, index, &cell);
    if (status == PHY_OK && out_index != NULL) {
        *out_index = index;
    }
    return status;
}

phy_status phy_notebook_add_input(phy_notebook *notebook, const char *source,
                                  size_t *out_index)
{
    if (notebook == NULL || source == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    notebook_cell cell;
    memset(&cell, 0, sizeof cell);
    cell.kind = PHY_NOTEBOOK_CELL_INPUT;
    cell.status = PHY_OK;
    const phy_status copied =
        copy_text(cell.primary, sizeof cell.primary, source);
    if (copied != PHY_OK) {
        return copied;
    }
    const size_t index = notebook->count;
    const phy_status status = insert_cell(notebook, index, &cell);
    if (status == PHY_OK && out_index != NULL) {
        *out_index = index;
    }
    return status;
}

static phy_status ensure_output(phy_notebook *notebook, size_t input_index,
                                notebook_cell **out_cell)
{
    const size_t output_index = input_index + 1u;
    if (output_index < notebook->count &&
        is_output_kind(notebook->cells[output_index].kind) &&
        notebook->cells[output_index].owner_input == input_index) {
        *out_cell = &notebook->cells[output_index];
        return PHY_OK;
    }

    notebook_cell output;
    memset(&output, 0, sizeof output);
    output.kind = PHY_NOTEBOOK_CELL_OUTPUT;
    output.owner_input = input_index;
    const phy_status status = insert_cell(notebook, output_index, &output);
    if (status != PHY_OK) {
        return status;
    }
    *out_cell = &notebook->cells[output_index];
    return PHY_OK;
}

static phy_status evaluate_command(phy_notebook *notebook,
                                   const phy_source_command *command,
                                   phy_ir_ref *out_result)
{
    switch (command->operation) {
    case PHY_SOURCE_SIMPLIFY:
        return phy_cas_simplify(notebook->cas, command->expression, out_result);
    case PHY_SOURCE_EXPAND:
        return phy_cas_expand(notebook->cas, command->expression, out_result);
    case PHY_SOURCE_TOGETHER:
    case PHY_SOURCE_NUMERATOR:
    case PHY_SOURCE_DENOMINATOR: {
        phy_ir_ref numerator = PHY_IR_NULL;
        phy_ir_ref denominator = PHY_IR_NULL;
        phy_status status = phy_cas_rational_form(
            notebook->cas, command->expression, &numerator, &denominator);
        if (status != PHY_OK) {
            return status;
        }
        if (command->operation == PHY_SOURCE_NUMERATOR) {
            *out_result = numerator;
            return PHY_OK;
        }
        if (command->operation == PHY_SOURCE_DENOMINATOR) {
            *out_result = denominator;
            return PHY_OK;
        }
        return phy_cas_div(notebook->cas, numerator, denominator, out_result);
    }
    case PHY_SOURCE_DIFFERENTIATE: {
        phy_ir_ref result = command->expression;
        for (size_t i = 0u; i < command->variable_count; ++i) {
            phy_status status = phy_cas_diff(notebook->cas, result,
                                             command->variables[i], &result);
            if (status != PHY_OK) {
                return status;
            }
        }
        *out_result = result;
        return PHY_OK;
    }
    case PHY_SOURCE_INTEGRATE: {
        phy_ir_ref result = command->expression;
        for (size_t i = 0u; i < command->variable_count; ++i) {
            phy_status status =
                phy_cas_integrate(notebook->cas, result,
                                  command->variables[i], &result);
            if (status != PHY_OK) {
                return status;
            }
        }
        *out_result = result;
        return PHY_OK;
    }
    default:
        return PHY_ERR_UNSUPPORTED;
    }
}

phy_status phy_notebook_evaluate(phy_notebook *notebook, size_t input_index)
{
    if (notebook == NULL || input_index >= notebook->count ||
        notebook->cells[input_index].kind != PHY_NOTEBOOK_CELL_INPUT) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    notebook_cell *output = NULL;
    phy_status status = ensure_output(notebook, input_index, &output);
    if (status != PHY_OK) {
        return status;
    }
    notebook_cell *input = &notebook->cells[input_index];
    if (input->execution == 0u) {
        input->execution = notebook->next_execution++;
    }

    input->secondary[0] = '\0';
    input->expression = PHY_IR_NULL;
    phy_source_command command;
    size_t error_offset = 0u;
    status = phy_source_parse(notebook->ir, input->primary, &command,
                              &error_offset);
    (void)error_offset;
    if (status == PHY_OK) {
        input->expression = command.expression;
        size_t length = 0u;
        status = phy_ir_write(notebook->ir, command.expression,
                              input->secondary, sizeof input->secondary,
                              &length);
        if (status == PHY_OK && length >= sizeof input->secondary) {
            status = PHY_ERR_TERM_LIMIT;
        }
    }

    phy_ir_ref result = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = evaluate_command(notebook, &command, &result);
    }

    input->status = status;
    input->stale = false;
    output->kind =
        status == PHY_OK ? PHY_NOTEBOOK_CELL_OUTPUT : PHY_NOTEBOOK_CELL_ERROR;
    output->expression = result;
    output->status = status;
    output->execution = input->execution;
    output->owner_input = input_index;
    output->stale = false;
    notebook->dirty = true;
    ensure_selected_visible(notebook);
    return status;
}

phy_status phy_notebook_activate_selected(phy_notebook *notebook)
{
    if (notebook == NULL || notebook->selected >= notebook->count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (notebook->cells[notebook->selected].kind !=
        PHY_NOTEBOOK_CELL_INPUT) {
        return PHY_ERR_UNSUPPORTED;
    }
    notebook->editing = false;
    return phy_notebook_evaluate(notebook, notebook->selected);
}

size_t phy_notebook_cell_count(const phy_notebook *notebook)
{
    return notebook != NULL ? notebook->count : 0u;
}

bool phy_notebook_cell(const phy_notebook *notebook, size_t index,
                       phy_notebook_cell_view *out_view)
{
    if (notebook == NULL || out_view == NULL || index >= notebook->count) {
        return false;
    }
    const notebook_cell *cell = &notebook->cells[index];
    out_view->kind = cell->kind;
    out_view->primary = cell->primary;
    out_view->secondary = cell->secondary;
    out_view->expression = cell->expression;
    out_view->status = cell->status;
    out_view->execution = cell->execution;
    out_view->stale = cell->stale;
    return true;
}

const phy_ir_context *phy_notebook_ir(const phy_notebook *notebook)
{
    return notebook != NULL ? notebook->ir : NULL;
}

size_t phy_notebook_selected(const phy_notebook *notebook)
{
    return notebook != NULL ? notebook->selected : 0u;
}

void phy_notebook_end_edit(phy_notebook *notebook)
{
    if (notebook != NULL) {
        notebook->editing = false;
    }
}

bool phy_notebook_select(phy_notebook *notebook, size_t index)
{
    if (notebook == NULL || index >= notebook->count) {
        return false;
    }
    const bool changed = notebook->selected != index;
    notebook->selected = index;
    notebook->editing = false;
    ensure_selected_visible(notebook);
    return changed;
}

bool phy_notebook_select_next(phy_notebook *notebook, int direction)
{
    if (notebook == NULL || notebook->count == 0u || direction == 0) {
        return false;
    }
    size_t next = notebook->selected;
    if (direction > 0) {
        next = (next + 1u) % notebook->count;
    } else {
        next = next == 0u ? notebook->count - 1u : next - 1u;
    }
    return phy_notebook_select(notebook, next);
}

static bool cell_at(const phy_notebook *notebook, int x, int y,
                    size_t *out_index, int *out_top)
{
    if (notebook == NULL || x < NOTEBOOK_CELL_X ||
        x >= NOTEBOOK_CELL_X + NOTEBOOK_CELL_WIDTH ||
        y < NOTEBOOK_FIRST_CELL_Y || y >= NOTEBOOK_LAST_CELL_Y) {
        return false;
    }
    for (size_t i = 0u; i < notebook->count; ++i) {
        const int top = screen_top(notebook, i);
        const int height = cell_height(&notebook->cells[i]);
        if (y >= top && y < top + height) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_top != NULL) {
                *out_top = top;
            }
            return true;
        }
    }
    return false;
}

bool phy_notebook_select_at(phy_notebook *notebook, int x, int y)
{
    size_t index = 0u;
    if (!cell_at(notebook, x, y, &index, NULL)) {
        return false;
    }
    return phy_notebook_select(notebook, index);
}

bool phy_notebook_run_at(phy_notebook *notebook, int x, int y,
                         phy_status *out_status)
{
    size_t index = 0u;
    int top = 0;
    if (!cell_at(notebook, x, y, &index, &top) ||
        notebook->cells[index].kind != PHY_NOTEBOOK_CELL_INPUT) {
        return false;
    }
    const int badge_x =
        NOTEBOOK_CELL_X + NOTEBOOK_CELL_WIDTH - NOTEBOOK_RUN_BADGE_WIDTH -
        NOTEBOOK_RUN_BADGE_INSET;
    const int badge_y = top + NOTEBOOK_RUN_BADGE_INSET;
    if (x < badge_x || x >= badge_x + NOTEBOOK_RUN_BADGE_WIDTH ||
        y < badge_y || y >= badge_y + NOTEBOOK_RUN_BADGE_HEIGHT) {
        return false;
    }
    notebook->selected = index;
    notebook->editing = false;
    const phy_status status = phy_notebook_evaluate(notebook, index);
    if (out_status != NULL) {
        *out_status = status;
    }
    return true;
}

static size_t insertion_after_selection(const phy_notebook *notebook)
{
    size_t index = notebook->selected + 1u;
    if (notebook->cells[notebook->selected].kind ==
            PHY_NOTEBOOK_CELL_INPUT &&
        index < notebook->count && is_output_kind(notebook->cells[index].kind) &&
        notebook->cells[index].owner_input == notebook->selected) {
        index++;
    }
    return index;
}

bool phy_notebook_insert_at(phy_notebook *notebook, int x, int y,
                            phy_status *out_status)
{
    if (notebook == NULL || y < NOTEBOOK_BUTTON_Y ||
        y >= NOTEBOOK_BUTTON_Y + NOTEBOOK_BUTTON_HEIGHT) {
        return false;
    }
    const bool markdown =
        x >= NOTEBOOK_MD_BUTTON_X &&
        x < NOTEBOOK_MD_BUTTON_X + NOTEBOOK_MD_BUTTON_WIDTH;
    const bool math =
        x >= NOTEBOOK_MATH_BUTTON_X &&
        x < NOTEBOOK_MATH_BUTTON_X + NOTEBOOK_MATH_BUTTON_WIDTH;
    if (!markdown && !math) {
        return false;
    }
    notebook_cell cell;
    memset(&cell, 0, sizeof cell);
    cell.kind =
        markdown ? PHY_NOTEBOOK_CELL_MARKDOWN : PHY_NOTEBOOK_CELL_INPUT;
    cell.status = PHY_OK;
    if (markdown) {
        (void)copy_text(cell.primary, sizeof cell.primary, "New note");
    }

    size_t index = notebook->count;
    if (notebook->count != 0u) {
        index = insertion_after_selection(notebook);
    }
    const phy_status status = insert_cell(notebook, index, &cell);
    if (status == PHY_OK) {
        notebook->selected = index;
        notebook->editing = true;
        notebook->edit_index = index;
        notebook->edit_secondary = false;
        notebook->cursor = strlen(notebook->cells[index].primary);
        ensure_selected_visible(notebook);
    }
    if (out_status != NULL) {
        *out_status = status;
    }
    return true;
}

static size_t bounded_cursor_from_x(const char *text, int x, int text_x,
                                    int advance)
{
    const size_t length = strlen(text);
    if (x <= text_x) {
        return 0u;
    }
    const int relative = x - text_x + advance / 2;
    const size_t position = (size_t)(relative / advance);
    return position < length ? position : length;
}

bool phy_notebook_begin_edit_at(phy_notebook *notebook, int x, int y)
{
    size_t index = 0u;
    int top = 0;
    if (!cell_at(notebook, x, y, &index, &top)) {
        return false;
    }
    notebook_cell *cell = &notebook->cells[index];
    if (cell->kind != PHY_NOTEBOOK_CELL_INPUT &&
        cell->kind != PHY_NOTEBOOK_CELL_MARKDOWN) {
        (void)phy_notebook_select(notebook, index);
        return false;
    }
    notebook->selected = index;
    notebook->editing = true;
    notebook->edit_index = index;
    if (cell->kind == PHY_NOTEBOOK_CELL_MARKDOWN) {
        notebook->edit_secondary = y >= top + 22;
        const char *text =
            notebook->edit_secondary ? cell->secondary : cell->primary;
        notebook->cursor =
            bounded_cursor_from_x(text, x, 34,
                                  notebook->edit_secondary
                                      ? PHY_GLYPH_ADVANCE
                                      : PHY_GLYPH_ADVANCE * 2);
    } else {
        notebook->edit_secondary = false;
        notebook->cursor = bounded_cursor_from_x(
            cell->primary, x, 55, PHY_GLYPH_ADVANCE);
    }
    ensure_selected_visible(notebook);
    return true;
}

bool phy_notebook_begin_edit_selected(phy_notebook *notebook)
{
    if (notebook == NULL || notebook->selected >= notebook->count) {
        return false;
    }
    const phy_notebook_cell_kind kind =
        notebook->cells[notebook->selected].kind;
    if (kind != PHY_NOTEBOOK_CELL_INPUT &&
        kind != PHY_NOTEBOOK_CELL_MARKDOWN) {
        return false;
    }
    notebook->editing = true;
    notebook->edit_index = notebook->selected;
    notebook->edit_secondary = false;
    notebook->cursor =
        strlen(notebook->cells[notebook->edit_index].primary);
    return true;
}

bool phy_notebook_is_editing(const phy_notebook *notebook)
{
    return notebook != NULL && notebook->editing;
}

phy_notebook_edit_target
phy_notebook_edit_target_kind(const phy_notebook *notebook)
{
    if (notebook == NULL || !notebook->editing ||
        notebook->edit_index >= notebook->count) {
        return PHY_NOTEBOOK_EDIT_NONE;
    }
    const notebook_cell *cell = &notebook->cells[notebook->edit_index];
    if (cell->kind == PHY_NOTEBOOK_CELL_INPUT) {
        return PHY_NOTEBOOK_EDIT_MATH;
    }
    if (cell->kind == PHY_NOTEBOOK_CELL_MARKDOWN) {
        return notebook->edit_secondary
                   ? PHY_NOTEBOOK_EDIT_MARKDOWN_BODY
                   : PHY_NOTEBOOK_EDIT_MARKDOWN_HEADING;
    }
    return PHY_NOTEBOOK_EDIT_NONE;
}

bool phy_notebook_is_dirty(const phy_notebook *notebook)
{
    return notebook != NULL && notebook->dirty;
}

void phy_notebook_mark_clean(phy_notebook *notebook)
{
    if (notebook != NULL) {
        notebook->dirty = false;
    }
}

static char *edit_buffer(phy_notebook *notebook, size_t *out_capacity)
{
    if (notebook == NULL || !notebook->editing ||
        notebook->edit_index >= notebook->count) {
        return NULL;
    }
    notebook_cell *cell = &notebook->cells[notebook->edit_index];
    if (cell->kind == PHY_NOTEBOOK_CELL_MARKDOWN &&
        notebook->edit_secondary) {
        *out_capacity = sizeof cell->secondary;
        return cell->secondary;
    }
    *out_capacity = sizeof cell->primary;
    return cell->primary;
}

static void mark_source_stale(phy_notebook *notebook)
{
    notebook_cell *input = &notebook->cells[notebook->edit_index];
    if (input->kind != PHY_NOTEBOOK_CELL_INPUT) {
        return;
    }
    input->stale = true;
    const size_t output_index = notebook->edit_index + 1u;
    if (output_index < notebook->count &&
        is_output_kind(notebook->cells[output_index].kind) &&
        notebook->cells[output_index].owner_input == notebook->edit_index) {
        notebook->cells[output_index].stale = true;
    }
}

bool phy_notebook_edit_insert(phy_notebook *notebook, char character)
{
    if ((unsigned char)character < (unsigned char)' ' ||
        (unsigned char)character > (unsigned char)'~') {
        return false;
    }
    const char text[2] = {character, '\0'};
    return phy_notebook_edit_insert_text(notebook, text, 1u);
}

bool phy_notebook_edit_insert_text(phy_notebook *notebook, const char *text,
                                   size_t cursor_offset)
{
    if (text == NULL) {
        return false;
    }
    const size_t text_length = strlen(text);
    if (text_length == 0u || cursor_offset > text_length) {
        return false;
    }
    for (size_t i = 0u; i < text_length; ++i) {
        if ((unsigned char)text[i] < (unsigned char)' ' ||
            (unsigned char)text[i] > (unsigned char)'~') {
            return false;
        }
    }
    size_t capacity = 0u;
    char *buffer = edit_buffer(notebook, &capacity);
    if (buffer == NULL) {
        return false;
    }
    const size_t length = strlen(buffer);
    if (text_length >= capacity || length > capacity - text_length - 1u ||
        notebook->cursor > length) {
        return false;
    }
    const size_t insertion = notebook->cursor;
    memmove(buffer + insertion + text_length, buffer + insertion,
            length - notebook->cursor + 1u);
    memcpy(buffer + insertion, text, text_length);
    notebook->cursor = insertion + cursor_offset;
    mark_source_stale(notebook);
    notebook->dirty = true;
    return true;
}

bool phy_notebook_edit_backspace(phy_notebook *notebook)
{
    size_t capacity = 0u;
    char *buffer = edit_buffer(notebook, &capacity);
    (void)capacity;
    if (buffer == NULL || notebook->cursor == 0u) {
        return false;
    }
    const size_t length = strlen(buffer);
    if (notebook->cursor > length) {
        notebook->cursor = length;
    }
    memmove(buffer + notebook->cursor - 1u, buffer + notebook->cursor,
            length - notebook->cursor + 1u);
    notebook->cursor--;
    mark_source_stale(notebook);
    notebook->dirty = true;
    return true;
}

bool phy_notebook_edit_move(phy_notebook *notebook, int direction)
{
    size_t capacity = 0u;
    char *buffer = edit_buffer(notebook, &capacity);
    (void)capacity;
    if (buffer == NULL || direction == 0) {
        return false;
    }
    if (direction < 0 && notebook->cursor > 0u) {
        notebook->cursor--;
        return true;
    }
    if (direction > 0 && notebook->cursor < strlen(buffer)) {
        notebook->cursor++;
        return true;
    }
    return false;
}

bool phy_notebook_edit_switch_field(phy_notebook *notebook)
{
    if (notebook == NULL || !notebook->editing ||
        notebook->cells[notebook->edit_index].kind !=
            PHY_NOTEBOOK_CELL_MARKDOWN) {
        return false;
    }
    notebook->edit_secondary = !notebook->edit_secondary;
    size_t capacity = 0u;
    char *buffer = edit_buffer(notebook, &capacity);
    (void)capacity;
    notebook->cursor = buffer != NULL ? strlen(buffer) : 0u;
    return true;
}

phy_status phy_notebook_seed_welcome(phy_notebook *notebook)
{
    if (notebook == NULL || notebook->count != 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_notebook_add_markdown(
        notebook, "Symbolic Physics",
        "Exact CAS notebook - native Ndless", NULL);
    size_t first_input = 0u;
    if (status == PHY_OK) {
        status = phy_notebook_add_input(notebook, "D[Sin[x]^2, x]",
                                        &first_input);
    }
    if (status == PHY_OK) {
        status = phy_notebook_evaluate(notebook, first_input);
    }
    size_t second_input = 0u;
    if (status == PHY_OK) {
        status = phy_notebook_add_input(
            notebook, "Simplify[x^2 + 1/2]", &second_input);
    }
    if (status == PHY_OK) {
        status = phy_notebook_evaluate(notebook, second_input);
    }
    if (status == PHY_OK) {
        notebook->selected = first_input;
        ensure_selected_visible(notebook);
    }
    return status;
}

static void unsigned_text(uint32_t value, char buffer[12])
{
    char reversed[10];
    size_t count = 0u;
    do {
        reversed[count++] = (char)('0' + (char)(value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof reversed);
    size_t out = 0u;
    while (count > 0u) {
        buffer[out++] = reversed[--count];
    }
    buffer[out] = '\0';
}

static void draw_execution_label(const phy_surface *surface, int x, int y,
                                 const char *prefix, uint32_t execution,
                                 uint16_t color)
{
    int pen = phy_gfx_draw_text(surface, x, y, prefix, color);
    if (execution == 0u) {
        pen = phy_gfx_draw_text(surface, pen, y, " ", color);
    } else {
        char digits[12];
        unsigned_text(execution, digits);
        pen = phy_gfx_draw_text(surface, pen, y, digits, color);
    }
    (void)phy_gfx_draw_text(surface, pen, y, "]:", color);
}

static void draw_pointer(const phy_surface *surface, int x, int y)
{
    phy_gfx_hline(surface, x - 3, y, 7, COLOR_POINTER);
    phy_gfx_vline(surface, x, y - 3, 7, COLOR_POINTER);
    phy_gfx_put_pixel(surface, x + 2, y + 2, COLOR_POINTER);
}

static void draw_editable(const phy_surface *surface, int x, int y,
                          const char *text, size_t cursor, bool editing,
                          size_t max_chars, unsigned scale, uint16_t color)
{
    const size_t length = strlen(text);
    size_t start = 0u;
    if (editing && cursor >= max_chars && max_chars > 1u) {
        start = cursor - max_chars + 1u;
    }
    char visible[48];
    size_t count = 0u;
    while (start + count < length && count < max_chars &&
           count + 1u < sizeof visible) {
        visible[count] = text[start + count];
        count++;
    }
    visible[count] = '\0';
    (void)phy_gfx_draw_text_scaled(surface, x, y, visible, scale, color);
    if (editing) {
        const size_t local_cursor = cursor >= start ? cursor - start : 0u;
        const int cursor_x =
            x + (int)local_cursor * PHY_GLYPH_ADVANCE * (int)scale;
        phy_gfx_vline(surface, cursor_x, y - 1,
                      PHY_GLYPH_HEIGHT * (int)scale + 2, COLOR_ACCENT);
    }
}

static int draw_text_span(const phy_surface *surface, int x, int y,
                          const char *text, size_t length, uint16_t color)
{
    char span[NOTEBOOK_DETAIL_CAPACITY];
    if (length >= sizeof span) {
        length = sizeof span - 1u;
    }
    memcpy(span, text, length);
    span[length] = '\0';
    return phy_gfx_draw_text(surface, x, y, span, color);
}

static bool find_inline_formula(const char *text, const char **out_open,
                                const char **out_source, size_t *out_length,
                                const char **out_after)
{
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == '$' && cursor[1] != '$') {
            const char *close = strchr(cursor + 1, '$');
            if (close != NULL) {
                *out_open = cursor;
                *out_source = cursor + 1;
                *out_length = (size_t)(close - cursor - 1);
                *out_after = close + 1;
                return true;
            }
        }
        if (cursor[0] == '\\' && cursor[1] == '(') {
            const char *close = strstr(cursor + 2, "\\)");
            if (close != NULL) {
                *out_open = cursor;
                *out_source = cursor + 2;
                *out_length = (size_t)(close - cursor - 2);
                *out_after = close + 2;
                return true;
            }
        }
    }
    return false;
}

static void draw_markdown_body(const phy_surface *surface, int card_y,
                               int card_height, const char *body)
{
    const int left = 34;
    const int right = NOTEBOOK_CELL_X + NOTEBOOK_CELL_WIDTH - 8;
    const int top = card_y + 23;
    const int clip_height = card_height - 25;
    const char *formula = NULL;
    size_t formula_length = 0u;
    phy_formula_style style = PHY_FORMULA_STYLE_TEXT;

    if (markdown_entire_formula(body, &formula, &formula_length, &style)) {
        phy_formula_metrics metrics;
        const int pixel_size =
            style == PHY_FORMULA_STYLE_DISPLAY ? 17 : 13;
        const int maximum_width = right - left;
        if (phy_formula_measure_latex(formula, formula_length, style,
                                      pixel_size, maximum_width,
                                      &metrics) == PHY_OK) {
            int origin_x = left;
            if (style == PHY_FORMULA_STYLE_DISPLAY &&
                metrics.width < maximum_width) {
                origin_x += (maximum_width - metrics.width) / 2;
            }
            (void)phy_formula_draw_latex(
                surface, formula, formula_length, style, pixel_size,
                maximum_width, origin_x, top + metrics.ascent, 0,
                COLOR_MARKDOWN, COLOR_CARD, left, top, maximum_width,
                clip_height, NULL);
            return;
        }
    }

    int pen = left;
    const int text_y = card_y + 25;
    const int baseline = card_y + 32;
    const char *cursor = body;
    const char *open = NULL;
    const char *source = NULL;
    const char *after = NULL;
    size_t length = 0u;
    while (pen < right &&
           find_inline_formula(cursor, &open, &source, &length, &after)) {
        pen = draw_text_span(surface, pen, text_y, cursor,
                             (size_t)(open - cursor), COLOR_DIM);
        if (pen >= right) {
            return;
        }
        phy_formula_metrics metrics;
        if (phy_formula_measure_latex(
                source, length, PHY_FORMULA_STYLE_TEXT, 13, right - pen,
                &metrics) != PHY_OK) {
            pen = draw_text_span(surface, pen, text_y, open,
                                 (size_t)(after - open), COLOR_DIM);
        } else {
            (void)phy_formula_draw_latex(
                surface, source, length, PHY_FORMULA_STYLE_TEXT, 13,
                right - pen, pen, baseline, 0, COLOR_MARKDOWN, COLOR_CARD,
                left, top, right - left, clip_height, NULL);
            pen += metrics.width + 1;
        }
        cursor = after;
    }
    if (pen < right) {
        (void)draw_text_span(surface, pen, text_y, cursor, strlen(cursor),
                             COLOR_DIM);
    }
}

static void draw_button(const phy_surface *surface, int x, int width,
                        const char *label)
{
    phy_gfx_fill_rect(surface, x, NOTEBOOK_BUTTON_Y, width,
                      NOTEBOOK_BUTTON_HEIGHT, COLOR_CARD_SELECTED);
    phy_gfx_draw_rect(surface, x, NOTEBOOK_BUTTON_Y, width,
                      NOTEBOOK_BUTTON_HEIGHT, COLOR_ACCENT);
    (void)phy_gfx_draw_text(surface, x + 4, NOTEBOOK_BUTTON_Y + 3, label,
                            COLOR_TEXT);
}

static void draw_header_filename(const phy_surface *surface,
                                 const char *filename, bool dirty)
{
    char visible[26];
    const char *source = filename != NULL ? filename : "Untitled";
    size_t count = 0u;
    while (source[count] != '\0' && count < sizeof visible - 2u) {
        visible[count] = source[count];
        count++;
    }
    if (source[count] != '\0' && count >= 3u) {
        visible[count - 2u] = '.';
        visible[count - 1u] = '.';
    }
    if (dirty && count + 1u < sizeof visible) {
        visible[count++] = '*';
    }
    visible[count] = '\0';
    (void)phy_gfx_draw_text(surface, 68, 5, visible,
                            dirty ? COLOR_POINTER : COLOR_TEXT);
}

void phy_notebook_draw_document(const phy_surface *surface,
                                const phy_notebook *notebook,
                                const char *filename, bool dirty,
                                int pointer_x, int pointer_y)
{
    if (surface == NULL || surface->pixels == NULL || notebook == NULL) {
        return;
    }
    phy_gfx_clear(surface, COLOR_BACKGROUND);

    if (notebook->count == 0u) {
        phy_gfx_fill_rect(surface, 12, 54, surface->width - 24, 86,
                          COLOR_CARD);
        phy_gfx_draw_rect(surface, 12, 54, surface->width - 24, 86,
                          COLOR_BORDER);
        (void)phy_gfx_draw_text_scaled(surface, 31, 68,
                                       "New physics notebook", 2u,
                                       COLOR_MARKDOWN);
        (void)phy_gfx_draw_text(surface, 31, 96,
                                "+MD writes notes and LaTeX", COLOR_DIM);
        (void)phy_gfx_draw_text(surface, 31, 110,
                                "+Math creates a CAS cell", COLOR_DIM);
        (void)phy_gfx_draw_text(surface, 31, 124,
                                "MENU or FILE opens documents", COLOR_DIM);
    }

    for (size_t i = 0u; i < notebook->count; ++i) {
        const notebook_cell *cell = &notebook->cells[i];
        const int y = screen_top(notebook, i);
        const int height = cell_height(cell);
        if (y >= NOTEBOOK_LAST_CELL_Y ||
            y + height <= NOTEBOOK_FIRST_CELL_Y) {
            continue;
        }
        const bool selected = i == notebook->selected;
        uint16_t fill = COLOR_CARD;
        if (cell->kind == PHY_NOTEBOOK_CELL_INPUT) {
            fill = selected ? COLOR_CARD_SELECTED : COLOR_CARD_INPUT;
        } else if (is_output_kind(cell->kind)) {
            fill = COLOR_CARD_OUTPUT;
        }
        phy_gfx_fill_rect(surface, NOTEBOOK_CELL_X, y, NOTEBOOK_CELL_WIDTH,
                          height, fill);
        phy_gfx_draw_rect(surface, NOTEBOOK_CELL_X, y, NOTEBOOK_CELL_WIDTH,
                          height, selected ? COLOR_ACCENT : COLOR_BORDER);
        if (selected) {
            phy_gfx_fill_rect(surface, NOTEBOOK_CELL_X, y, 3, height,
                              COLOR_ACCENT);
        }

        const bool editing =
            notebook->editing && notebook->edit_index == i;
        if (cell->kind == PHY_NOTEBOOK_CELL_MARKDOWN) {
            (void)phy_gfx_draw_text(surface, 10, y + 5, "MD", COLOR_DIM);
            draw_editable(surface, 34, y + 4, cell->primary, notebook->cursor,
                          editing && !notebook->edit_secondary, 23u, 2u,
                          COLOR_MARKDOWN);
            if (editing && notebook->edit_secondary) {
                draw_editable(surface, 34, y + 25, cell->secondary,
                              notebook->cursor, true, 45u, 1u, COLOR_DIM);
            } else {
                draw_markdown_body(surface, y, height, cell->secondary);
            }
        } else if (cell->kind == PHY_NOTEBOOK_CELL_INPUT) {
            draw_execution_label(surface, 10, y + 12, "In[", cell->execution,
                                 COLOR_ACCENT);
            draw_editable(surface, 55, y + 12, cell->primary,
                          notebook->cursor, editing, 36u, 1u, COLOR_TEXT);
            const int badge_x = NOTEBOOK_CELL_X + NOTEBOOK_CELL_WIDTH -
                                NOTEBOOK_RUN_BADGE_WIDTH -
                                NOTEBOOK_RUN_BADGE_INSET;
            const int badge_y = y + NOTEBOOK_RUN_BADGE_INSET;
            phy_gfx_fill_rect(surface, badge_x, badge_y,
                              NOTEBOOK_RUN_BADGE_WIDTH,
                              NOTEBOOK_RUN_BADGE_HEIGHT, COLOR_TITLE);
            phy_gfx_draw_rect(surface, badge_x, badge_y,
                              NOTEBOOK_RUN_BADGE_WIDTH,
                              NOTEBOOK_RUN_BADGE_HEIGHT,
                              selected ? COLOR_ACCENT : COLOR_BORDER);
            (void)phy_gfx_draw_text(surface, badge_x + 6, badge_y + 3, "RUN",
                                    COLOR_TEXT);
        } else if (cell->kind == PHY_NOTEBOOK_CELL_OUTPUT) {
            const uint16_t result_color =
                cell->stale ? COLOR_DIM : COLOR_RESULT;
            draw_execution_label(surface, 10, y + 16, "Out[",
                                 cell->execution, result_color);
            phy_formula_metrics metrics;
            const int maximum_width =
                NOTEBOOK_CELL_X + NOTEBOOK_CELL_WIDTH - 63;
            const phy_status measure_status =
                phy_formula_measure_ir(
                    notebook->ir, cell->expression,
                    PHY_FORMULA_STYLE_TEXT, 15, maximum_width, &metrics);
            if (measure_status == PHY_OK) {
                const int formula_height = metrics.ascent + metrics.descent;
                const int baseline =
                    y + (height - formula_height) / 2 + metrics.ascent;
                (void)phy_formula_draw_ir(
                    surface, notebook->ir, cell->expression,
                    PHY_FORMULA_STYLE_TEXT, 15, maximum_width, 61, baseline, 0,
                    result_color, fill, 61, y + 1, maximum_width,
                    height - 2, NULL);
            } else {
                (void)phy_gfx_draw_text(surface, 61, y + 16,
                                        phy_status_name(measure_status),
                                        COLOR_ERROR);
            }
            if (cell->stale) {
                (void)phy_gfx_draw_text(surface, PHY_SCREEN_WIDTH - 39,
                                        y + height - 11, "stale", COLOR_DIM);
            }
        } else {
            draw_execution_label(surface, 10, y + 16, "Err[",
                                 cell->execution, COLOR_ERROR);
            (void)phy_gfx_draw_text(surface, 61, y + 16,
                                    phy_status_name(cell->status), COLOR_ERROR);
        }
    }

    /* Header/footer are painted last so scrolled cards cannot bleed into them. */
    phy_gfx_fill_rect(surface, 0, 0, surface->width, 17, COLOR_TITLE);
    (void)phy_gfx_draw_text(surface, 5, 5, "Phy-nspire", COLOR_TEXT);
    draw_header_filename(surface, filename, dirty);
    if (notebook->editing) {
        (void)phy_gfx_draw_text(surface, 248, 5, "EDIT", COLOR_POINTER);
    }
    phy_gfx_fill_rect(surface, 282, 2, 36, 13, COLOR_CARD_SELECTED);
    phy_gfx_draw_rect(surface, 282, 2, 36, 13, COLOR_ACCENT);
    (void)phy_gfx_draw_text(surface, 288, 5, "FILE", COLOR_TEXT);

    phy_gfx_fill_rect(surface, 0, NOTEBOOK_FOOTER_Y, surface->width,
                      surface->height - NOTEBOOK_FOOTER_Y, COLOR_BACKGROUND);
    phy_gfx_hline(surface, 0, NOTEBOOK_FOOTER_Y, surface->width, COLOR_BORDER);
    draw_button(surface, NOTEBOOK_MD_BUTTON_X, NOTEBOOK_MD_BUTTON_WIDTH, "+MD");
    draw_button(surface, NOTEBOOK_MATH_BUTTON_X, NOTEBOOK_MATH_BUTTON_WIDTH,
                "+Math");
    (void)phy_gfx_draw_text(
        surface, 89, NOTEBOOK_BUTTON_Y + 3,
        notebook->editing ? "MENU insert  ENTER/run  ESC done"
                          : "MENU files  ENTER/run  ESC done",
        COLOR_DIM);

    if (pointer_x >= 0 && pointer_y >= 0) {
        draw_pointer(surface, pointer_x, pointer_y);
    }
}

void phy_notebook_draw(const phy_surface *surface,
                       const phy_notebook *notebook, int pointer_x,
                       int pointer_y)
{
    phy_notebook_draw_document(surface, notebook, NULL,
                               phy_notebook_is_dirty(notebook), pointer_x,
                               pointer_y);
}
