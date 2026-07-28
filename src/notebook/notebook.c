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

#define NOTEBOOK_MARKDOWN_MAX_WIDTH (NOTEBOOK_CELL_WIDTH - 42)
#define NOTEBOOK_MARKDOWN_MAX_LINES 12

/*
 * Mixed prose and inline math flow together: words and formulas are
 * unbreakable tokens laid left to right, wrapping at the card edge. Every
 * line takes the height its tallest token needs -- a fraction or an
 * integral opens the line up instead of colliding with its neighbors --
 * and the total content height is capped so a card always fits the
 * viewport when selected; the cap itself is marked on the card, never
 * silent.
 */
#define NOTEBOOK_MARKDOWN_FLOW_MAX_CONTENT 156
#define NOTEBOOK_MARKDOWN_LEFT 34
#define NOTEBOOK_MARKDOWN_RIGHT (NOTEBOOK_CELL_X + NOTEBOOK_CELL_WIDTH - 8)

static int markdown_flow_height(const char *body);

/*
 * A display formula tries the full size first and steps down before
 * overflowing; whatever still does not fit pans under the horizontal keys,
 * exactly like an output card. Physics does not get rewritten to fit a
 * 320-pixel screen.
 */
static phy_status measure_markdown_formula(const char *source, size_t length,
                                           phy_formula_metrics *out_metrics,
                                           int *out_pixel_size)
{
    static const int sizes[3] = {17, 15, 13};
    phy_status status = PHY_ERR_BACKEND;
    for (size_t attempt = 0u; attempt < 3u; ++attempt) {
        *out_pixel_size = sizes[attempt];
        status = phy_formula_measure_latex(
            source, length, PHY_FORMULA_STYLE_DISPLAY, *out_pixel_size,
            NOTEBOOK_MARKDOWN_MAX_WIDTH, out_metrics);
        if (status != PHY_OK ||
            (!out_metrics->overflow &&
             out_metrics->width <= NOTEBOOK_MARKDOWN_MAX_WIDTH)) {
            break;
        }
    }
    return status;
}

/* One space-broken line of at most per_line fixed-width glyphs. */
static size_t wrap_take(const char *cursor, size_t per_line)
{
    size_t take = strlen(cursor);
    if (take > per_line) {
        take = per_line;
        size_t split = take;
        while (split > 0u && cursor[split] != ' ') {
            split--;
        }
        if (split > 0u) {
            take = split;
        }
    }
    return take;
}

static int wrapped_line_count(const char *text, size_t per_line,
                              int max_lines)
{
    int lines = 0;
    const char *cursor = text;
    while (*cursor != '\0' && lines < max_lines) {
        cursor += wrap_take(cursor, per_line);
        while (*cursor == ' ') {
            cursor++;
        }
        lines++;
    }
    return lines > 0 ? lines : 1;
}

static bool markdown_has_inline_formula(const char *body)
{
    const char *dollar = strchr(body, '$');
    const char *paren = strstr(body, "\\(");
    return (dollar != NULL && strchr(dollar + 1, '$') != NULL) ||
           (paren != NULL && strstr(paren + 2, "\\)") != NULL);
}

static int cell_height_uncached(const notebook_cell *cell)
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
                int pixel_size = 0;
                int height = 64;
                if (measure_markdown_formula(source, length, &metrics,
                                             &pixel_size) == PHY_OK) {
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
        if (markdown_has_inline_formula(cell->secondary)) {
            return markdown_flow_height(cell->secondary);
        }
        {
            /* Prose wraps; the card grows to hold every wrapped line. */
            const int lines = wrapped_line_count(
                cell->secondary,
                (size_t)(NOTEBOOK_MARKDOWN_MAX_WIDTH / PHY_GLYPH_ADVANCE),
                NOTEBOOK_MARKDOWN_MAX_LINES);
            return 26 + lines * 11;
        }
    case PHY_NOTEBOOK_CELL_INPUT:
        return 32;
    case PHY_NOTEBOOK_CELL_OUTPUT:
    case PHY_NOTEBOOK_CELL_ERROR:
        return 42;
    default:
        return 32;
    }
}

static int cell_height(const notebook_cell *cell)
{
    if (cell->height > 0) {
        return cell->height;
    }
    const int measured = cell_height_uncached(cell);
    /*
     * Markdown heights depend on formula layout; before the font pack is
     * up they fall back to defaults that must not stick. The cell lives in
     * a mutable notebook, so shedding const here writes through to it.
     */
    if (cell->kind != PHY_NOTEBOOK_CELL_MARKDOWN || phy_formula_is_ready()) {
        ((notebook_cell *)(uintptr_t)cell)->height = measured;
    }
    return measured;
}

/*
 * Editing a Markdown body swaps the rendered flow for a raw-source grid of
 * fixed 45-glyph rows: a character grid keeps the cursor's position obvious
 * while typing LaTeX. The card holds up to eight rows, windowed around the
 * cursor, and the cell keeps that height for exactly as long as the editor
 * is open.
 */
#define NOTEBOOK_EDIT_COLUMNS \
    ((size_t)(NOTEBOOK_MARKDOWN_MAX_WIDTH / PHY_GLYPH_ADVANCE))
#define NOTEBOOK_EDIT_MAX_LINES 8u

static bool editing_markdown_body(const phy_notebook *notebook, size_t index)
{
    return notebook->editing && notebook->edit_index == index &&
           notebook->edit_secondary &&
           notebook->cells[index].kind == PHY_NOTEBOOK_CELL_MARKDOWN;
}

static int display_height(const phy_notebook *notebook, size_t index)
{
    if (editing_markdown_body(notebook, index)) {
        size_t lines =
            strlen(notebook->cells[index].secondary) / NOTEBOOK_EDIT_COLUMNS +
            1u;
        if (lines > NOTEBOOK_EDIT_MAX_LINES) {
            lines = NOTEBOOK_EDIT_MAX_LINES;
        }
        return 29 + (int)lines * 11;
    }
    return cell_height(&notebook->cells[index]);
}

static int content_top(const phy_notebook *notebook, size_t index)
{
    int y = NOTEBOOK_FIRST_CELL_Y;
    for (size_t i = 0u; i < index && i < notebook->count; ++i) {
        y += display_height(notebook, i) + NOTEBOOK_CELL_GAP;
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
        top + display_height(notebook, notebook->selected);
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

    /*
     * The ceilings a stateful physics evaluator needs rather than the ones a
     * two-cell scalar demo needed. A 4D curvature pass interns several thousand
     * nodes and the IR has no collection, so a document that computes one and
     * then edits a cell has to be able to compute it again. The limits stay
     * limits: an intentionally explosive expression still fails as a typed
     * PHY_ERR_NODE_LIMIT rather than exhausting the calculator.
     */
    phy_ir_limits ir_limits;
    phy_ir_limits_defaults(&ir_limits);
    /*
     * Headroom above the measured tour peak (~15k nodes / 1 MiB through the
     * Schwarzschild extras and the eight-gamma trace). Boyer-Lindquist Kerr
     * curvature was measured at two million nodes and 151 MiB on a host --
     * out of reach of any ceiling here until the CAS gains a rational
     * normal form that keeps Sigma/Delta symbolic. Pools grow on demand, so
     * an unused ceiling costs no calculator memory.
     */
    ir_limits.max_nodes = 131072u;
    ir_limits.max_depth = 64u;
    ir_limits.max_children = 1024u;
    ir_limits.max_bytes = 4096u * 1024u;
    notebook->ir = phy_ir_context_create(&ir_limits);
    if (notebook->ir == NULL) {
        phy_free(notebook, sizeof *notebook);
        return NULL;
    }

    phy_cas_limits cas_limits;
    phy_cas_limits_defaults(&cas_limits);
    cas_limits.max_steps = 1000000u;
    cas_limits.max_bytes = 1024u * 1024u;
    notebook->cas = phy_cas_create(notebook->ir, &cas_limits);
    if (notebook->cas == NULL) {
        phy_ir_context_destroy(notebook->ir);
        phy_free(notebook, sizeof *notebook);
        return NULL;
    }

    notebook->env = phy_env_create(notebook->cas);
    if (notebook->env == NULL) {
        phy_cas_destroy(notebook->cas);
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
    /* Objects borrow the CAS and the IR context; they die first. */
    phy_env_destroy(notebook->env);
    phy_cas_destroy(notebook->cas);
    phy_ir_context_destroy(notebook->ir);
    phy_free(notebook, sizeof *notebook);
}

phy_env *phy_notebook_environment(phy_notebook *notebook)
{
    return notebook != NULL ? notebook->env : NULL;
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

/*
 * Evaluation is now forward-dependent: a cell may bind a name that later cells
 * read, so running one invalidates every result after it. Marking those stale
 * is the only honest thing to display -- the alternative is an `Out[7]` that
 * was computed against a state the document no longer has.
 */
static void mark_following_stale(phy_notebook *notebook, size_t after)
{
    for (size_t i = after + 1u; i < notebook->count; ++i) {
        if (notebook->cells[i].kind != PHY_NOTEBOOK_CELL_MARKDOWN) {
            notebook->cells[i].stale = true;
        }
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
    if (status == PHY_OK && command.expression != PHY_IR_NULL) {
        input->expression = command.expression;
        size_t length = 0u;
        status = phy_ir_write(notebook->ir, command.expression,
                              input->secondary, sizeof input->secondary,
                              &length);
        if (status == PHY_OK && length >= sizeof input->secondary) {
            status = PHY_ERR_TERM_LIMIT;
        }
    }

    phy_value value;
    value.kind = PHY_VALUE_NONE;
    value.as.scalar = PHY_IR_NULL;
    phy_ir_ref result = PHY_IR_NULL;
    char description[PHY_EVAL_DESCRIPTION_CAPACITY];
    description[0] = '\0';
    if (status == PHY_OK) {
        status = phy_eval_command(notebook->env, &command, &value);
    }
    if (status == PHY_OK) {
        status = phy_eval_value_expression(notebook->env, value, &result);
    }
    if (status == PHY_OK && result == PHY_IR_NULL &&
        value.kind != PHY_VALUE_NONE) {
        /*
         * A manifold, a group, a curvature bundle: no expansion in the typed
         * IR, so the cell shows what the object is instead of nothing.
         */
        status = phy_eval_describe(notebook->env, value, description,
                                   sizeof description);
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
    if (status == PHY_OK) {
        (void)copy_text(output->primary, sizeof output->primary, description);
    } else {
        output->primary[0] = '\0';
    }
    mark_following_stale(notebook, input_index + 1u);
    notebook->dirty = true;
    ensure_selected_visible(notebook);
    return status;
}

phy_status phy_notebook_evaluate_all(phy_notebook *notebook)
{
    if (notebook == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    /*
     * Reopening a document restores its cells but not its environment, so the
     * only way to make a stateful notebook consistent again is to replay it
     * from an empty one. Every input runs; the first failing status is
     * reported, and later cells still run so the reader sees which of them
     * depended on the failure.
     */
    phy_env_reset(notebook->env);
    phy_status first_error = PHY_OK;
    for (size_t i = 0u; i < notebook->count; ++i) {
        if (notebook->cells[i].kind != PHY_NOTEBOOK_CELL_INPUT) {
            continue;
        }
        const phy_status status = phy_notebook_evaluate(notebook, i);
        if (status != PHY_OK && first_error == PHY_OK) {
            first_error = status;
        }
    }
    return first_error;
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
    if (changed) {
        notebook->output_pan = 0;
    }
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
        const int height = display_height(notebook, i);
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
    notebook->cells[notebook->edit_index].height = 0;
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
    notebook->cells[notebook->edit_index].height = 0;
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

bool phy_notebook_edit_move_line(phy_notebook *notebook, int direction)
{
    size_t capacity = 0u;
    char *buffer = edit_buffer(notebook, &capacity);
    (void)capacity;
    if (buffer == NULL || direction == 0 || !notebook->edit_secondary ||
        notebook->cells[notebook->edit_index].kind !=
            PHY_NOTEBOOK_CELL_MARKDOWN) {
        return false;
    }
    const size_t columns = NOTEBOOK_EDIT_COLUMNS;
    const size_t length = strlen(buffer);
    if (notebook->cursor > length) {
        notebook->cursor = length;
    }
    if (direction < 0) {
        if (notebook->cursor < columns) {
            return false;
        }
        notebook->cursor -= columns;
        return true;
    }
    if (notebook->cursor / columns == length / columns) {
        return false;
    }
    const size_t target = notebook->cursor + columns;
    notebook->cursor = target > length ? length : target;
    return true;
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

#define NOTEBOOK_RESULT_MAX_WIDTH \
    (NOTEBOOK_CELL_X + NOTEBOOK_CELL_WIDTH - 63)

/*
 * A result wider than the card steps down through smaller pixel sizes before
 * giving up. Every attempted layout lands in the bridge cache, so the
 * retries cost only the first frame. Draw and pan share this so the clamp
 * always matches what is actually on screen.
 */
static phy_status measure_result_formula(const phy_notebook *notebook,
                                         const notebook_cell *cell,
                                         phy_formula_metrics *out_metrics,
                                         int *out_pixel_size)
{
    static const int sizes[3] = {15, 12, 10};
    phy_status status = PHY_ERR_BACKEND;
    for (size_t attempt = 0u; attempt < 3u; ++attempt) {
        *out_pixel_size = sizes[attempt];
        status = phy_formula_measure_ir(
            notebook->ir, cell->expression, PHY_FORMULA_STYLE_TEXT,
            *out_pixel_size, NOTEBOOK_RESULT_MAX_WIDTH, out_metrics);
        if (status != PHY_OK ||
            (!out_metrics->overflow &&
             out_metrics->width <= NOTEBOOK_RESULT_MAX_WIDTH)) {
            break;
        }
    }
    return status;
}

static int selected_result_excess(const phy_notebook *notebook)
{
    if (notebook->count == 0u || notebook->editing) {
        return 0;
    }
    const notebook_cell *cell = &notebook->cells[notebook->selected];
    phy_formula_metrics metrics;
    int pixel_size = 0;
    if (cell->kind == PHY_NOTEBOOK_CELL_MARKDOWN) {
        const char *source = NULL;
        size_t length = 0u;
        phy_formula_style style = PHY_FORMULA_STYLE_TEXT;
        if (!markdown_entire_formula(cell->secondary, &source, &length,
                                     &style) ||
            style != PHY_FORMULA_STYLE_DISPLAY ||
            measure_markdown_formula(source, length, &metrics,
                                     &pixel_size) != PHY_OK) {
            return 0;
        }
        return metrics.width > NOTEBOOK_MARKDOWN_MAX_WIDTH
                   ? metrics.width - NOTEBOOK_MARKDOWN_MAX_WIDTH
                   : 0;
    }
    if (cell->kind != PHY_NOTEBOOK_CELL_OUTPUT ||
        cell->expression == PHY_IR_NULL) {
        return 0;
    }
    if (measure_result_formula(notebook, cell, &metrics, &pixel_size) !=
        PHY_OK) {
        return 0;
    }
    return metrics.width > NOTEBOOK_RESULT_MAX_WIDTH
               ? metrics.width - NOTEBOOK_RESULT_MAX_WIDTH
               : 0;
}

bool phy_notebook_pan_selected(phy_notebook *notebook, int direction)
{
    if (notebook == NULL || direction == 0) {
        return false;
    }
    const int excess = selected_result_excess(notebook);
    if (excess <= 0) {
        return false;
    }
    int pan = notebook->output_pan + direction * 48;
    if (pan < 0) {
        pan = 0;
    }
    if (pan > excess) {
        pan = excess;
    }
    notebook->output_pan = pan;
    /*
     * A wide result owns the horizontal keys even at either end of its
     * travel: falling through to a selection move mid-formula would throw
     * the reader somewhere else entirely.
     */
    return true;
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

/*
 * The Markdown body editor: the raw source in fixed 45-glyph rows with the
 * cursor kept inside an eight-row window. Corner markers say when rows are
 * hidden above or below, mirroring the pan markers on wide formulas.
 */
static void draw_editable_body_grid(const phy_surface *surface, int x, int y,
                                    const char *text, size_t cursor)
{
    const size_t columns = NOTEBOOK_EDIT_COLUMNS;
    const size_t length = strlen(text);
    if (cursor > length) {
        cursor = length;
    }
    const size_t total_lines = length / columns + 1u;
    const size_t cursor_line = cursor / columns;
    size_t first = 0u;
    if (cursor_line >= NOTEBOOK_EDIT_MAX_LINES) {
        first = cursor_line - NOTEBOOK_EDIT_MAX_LINES + 1u;
    }
    for (size_t line = 0u; line < NOTEBOOK_EDIT_MAX_LINES; ++line) {
        const size_t index = first + line;
        if (index >= total_lines) {
            break;
        }
        const size_t offset = index * columns;
        size_t count = length - offset;
        if (count > columns) {
            count = columns;
        }
        (void)draw_text_span(surface, x, y + (int)line * 11, text + offset,
                             count, COLOR_DIM);
    }
    if (first > 0u) {
        (void)phy_gfx_draw_text(surface, NOTEBOOK_MARKDOWN_RIGHT, y, "^",
                                COLOR_DIM);
    }
    if (first + NOTEBOOK_EDIT_MAX_LINES < total_lines) {
        (void)phy_gfx_draw_text(
            surface, NOTEBOOK_MARKDOWN_RIGHT,
            y + ((int)NOTEBOOK_EDIT_MAX_LINES - 1) * 11, "v", COLOR_DIM);
    }
    const int cursor_x = x + (int)(cursor % columns) * PHY_GLYPH_ADVANCE;
    const int cursor_y = y + (int)(cursor_line - first) * 11;
    phy_gfx_vline(surface, cursor_x, cursor_y - 1, PHY_GLYPH_HEIGHT + 2,
                  COLOR_ACCENT);
}

/*
 * A physics-object descriptor across the two lines an output card has room
 * for, broken at spaces so a manifold's coordinate tuple stays legible.
 */
static void draw_wrapped_text(const phy_surface *surface, int x, int y,
                              int width, const char *text, uint16_t color)
{
    const size_t per_line =
        width > 0 ? (size_t)(width / PHY_GLYPH_ADVANCE) : 0u;
    if (per_line == 0u) {
        return;
    }
    const char *cursor = text;
    for (int line = 0; line < 2 && *cursor != '\0'; ++line) {
        size_t take = strlen(cursor);
        if (take > per_line) {
            take = per_line;
            size_t split = take;
            while (split > 0u && cursor[split] != ' ') {
                split--;
            }
            if (split > 0u) {
                take = split;
            }
        }
        (void)draw_text_span(surface, x, y + line * 11, cursor, take, color);
        cursor += take;
        while (*cursor == ' ') {
            cursor++;
        }
    }
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

/*
 * Word-and-formula flow shared by measuring and drawing. Words and inline
 * formulas are unbreakable tokens laid left to right, wrapping at the card
 * edge. A line's tokens are buffered until it breaks, because the line's
 * baseline is only known once its tallest ascent is; one walker produces
 * both the content height and the pixels, so the card height can never
 * disagree with what is drawn.
 */
#define NOTEBOOK_FLOW_MAX_TOKENS 48

typedef struct {
    const char *text; /* word bytes, or the formula source */
    size_t length;
    int x;
    bool is_formula;
} flow_token;

typedef struct {
    const phy_surface *surface; /* NULL measures without drawing */
    int card_y;
    int clip_height;
    int pen;
    int content;    /* height consumed by flushed lines */
    bool full;      /* the height cap is reached: stop consuming tokens */
    bool truncated; /* content was dropped or clipped; the card says so */
    flow_token tokens[NOTEBOOK_FLOW_MAX_TOKENS];
    size_t token_count;
    int line_ascent;  /* text glyphs sit on the baseline: at least 7 */
    int line_descent; /* plus one guard row below it */
} markdown_flow;

static void flow_init(markdown_flow *flow, const phy_surface *surface,
                      int card_y, int clip_height)
{
    memset(flow, 0, sizeof *flow);
    flow->surface = surface;
    flow->card_y = card_y;
    flow->clip_height = clip_height;
    flow->pen = NOTEBOOK_MARKDOWN_LEFT;
    flow->line_ascent = 7;
    flow->line_descent = 1;
}

static void flow_flush_line(markdown_flow *flow)
{
    if (flow->token_count == 0u) {
        flow->pen = NOTEBOOK_MARKDOWN_LEFT;
        return;
    }
    const int line_height = flow->line_ascent + flow->line_descent + 3;
    if (flow->content + line_height > NOTEBOOK_MARKDOWN_FLOW_MAX_CONTENT) {
        flow->full = true;
        flow->truncated = true;
        flow->token_count = 0u;
        return;
    }
    if (flow->surface != NULL) {
        const int baseline =
            flow->card_y + 25 + flow->content + flow->line_ascent;
        for (size_t i = 0u; i < flow->token_count; ++i) {
            const flow_token *token = &flow->tokens[i];
            if (token->is_formula) {
                (void)phy_formula_draw_latex(
                    flow->surface, token->text, token->length,
                    PHY_FORMULA_STYLE_TEXT, 13,
                    NOTEBOOK_MARKDOWN_RIGHT - token->x, token->x, baseline,
                    0, COLOR_MARKDOWN, COLOR_CARD, NOTEBOOK_MARKDOWN_LEFT,
                    flow->card_y + 23,
                    NOTEBOOK_MARKDOWN_RIGHT - NOTEBOOK_MARKDOWN_LEFT,
                    flow->clip_height, NULL);
            } else {
                (void)draw_text_span(flow->surface, token->x, baseline - 7,
                                     token->text, token->length, COLOR_DIM);
            }
        }
    }
    flow->content += line_height;
    flow->token_count = 0u;
    flow->pen = NOTEBOOK_MARKDOWN_LEFT;
    flow->line_ascent = 7;
    flow->line_descent = 1;
}

static void flow_push_token(markdown_flow *flow, bool is_formula,
                            const char *text, size_t length, int width,
                            int ascent, int descent)
{
    if (flow->full) {
        return;
    }
    if ((flow->pen + width > NOTEBOOK_MARKDOWN_RIGHT &&
         flow->pen > NOTEBOOK_MARKDOWN_LEFT) ||
        flow->token_count >= NOTEBOOK_FLOW_MAX_TOKENS) {
        flow_flush_line(flow);
        if (flow->full) {
            return;
        }
    }
    flow_token *token = &flow->tokens[flow->token_count++];
    token->text = text;
    token->length = length;
    token->x = flow->pen;
    token->is_formula = is_formula;
    flow->pen += width + (is_formula ? 1 : 0);
    if (ascent > flow->line_ascent) {
        flow->line_ascent = ascent;
    }
    if (descent > flow->line_descent) {
        flow->line_descent = descent;
    }
    if (width > NOTEBOOK_MARKDOWN_RIGHT - NOTEBOOK_MARKDOWN_LEFT) {
        /* Wider than a whole line: it draws clipped, and the card says so. */
        flow->truncated = true;
    }
}

static void flow_words(markdown_flow *flow, const char *text, size_t span)
{
    size_t offset = 0u;
    while (offset < span && !flow->full) {
        if (text[offset] == ' ') {
            if (flow->pen > NOTEBOOK_MARKDOWN_LEFT) {
                flow->pen += PHY_GLYPH_ADVANCE;
            }
            offset++;
            continue;
        }
        size_t word = 0u;
        while (offset + word < span && text[offset + word] != ' ') {
            word++;
        }
        flow_push_token(flow, false, text + offset, word,
                        (int)word * PHY_GLYPH_ADVANCE, 7, 1);
        offset += word;
    }
}

static void flow_formula(markdown_flow *flow, const char *raw,
                         const char *source, size_t length,
                         const char *after)
{
    phy_formula_metrics metrics;
    const int line_width = NOTEBOOK_MARKDOWN_RIGHT - NOTEBOOK_MARKDOWN_LEFT;
    if (phy_formula_measure_latex(source, length, PHY_FORMULA_STYLE_TEXT, 13,
                                  line_width, &metrics) != PHY_OK) {
        /* Unmeasurable math flows as its raw dollar-fenced text. */
        flow_words(flow, raw, (size_t)(after - raw));
        return;
    }
    flow_push_token(flow, true, source, length, metrics.width,
                    metrics.ascent, metrics.descent);
}

static void markdown_flow_run(markdown_flow *flow, const char *body)
{
    const char *cursor = body;
    while (*cursor != '\0' && !flow->full) {
        const char *open = NULL;
        const char *source = NULL;
        const char *after = NULL;
        size_t length = 0u;
        if (find_inline_formula(cursor, &open, &source, &length, &after)) {
            flow_words(flow, cursor, (size_t)(open - cursor));
            if (!flow->full) {
                flow_formula(flow, open, source, length, after);
            }
            cursor = after;
        } else {
            flow_words(flow, cursor, strlen(cursor));
            break;
        }
    }
    flow_flush_line(flow);
}

static int markdown_flow_height(const char *body)
{
    markdown_flow flow;
    flow_init(&flow, NULL, 0, 0);
    markdown_flow_run(&flow, body);
    int content = flow.content;
    if (content < 11) {
        content = 11;
    }
    return 29 + content;
}

static void draw_markdown_body(const phy_surface *surface, int card_y,
                               int card_height, const char *body, int pan)
{
    const int left = NOTEBOOK_MARKDOWN_LEFT;
    const int top = card_y + 23;
    const int clip_height = card_height - 25;
    const char *formula = NULL;
    size_t formula_length = 0u;
    phy_formula_style style = PHY_FORMULA_STYLE_TEXT;

    if (markdown_entire_formula(body, &formula, &formula_length, &style)) {
        phy_formula_metrics metrics;
        int pixel_size = 13;
        phy_status status;
        const int maximum_width = NOTEBOOK_MARKDOWN_MAX_WIDTH;
        if (style == PHY_FORMULA_STYLE_DISPLAY) {
            status = measure_markdown_formula(formula, formula_length,
                                              &metrics, &pixel_size);
        } else {
            status = phy_formula_measure_latex(
                formula, formula_length, style, pixel_size, maximum_width,
                &metrics);
        }
        if (status != PHY_OK) {
            /*
             * The raw-text fallback below hides why layout failed, and that
             * cost a debugging round on the device once. Name the status in
             * the corner so the next photograph answers the question.
             */
            (void)phy_gfx_draw_text(surface, left + maximum_width - 96,
                                    card_y + 2, phy_status_name(status),
                                    COLOR_DIM);
        }
        if (status == PHY_OK) {
            const int excess = metrics.width > maximum_width
                                   ? metrics.width - maximum_width
                                   : 0;
            if (pan > excess) {
                pan = excess;
            }
            int origin_x = left;
            if (style == PHY_FORMULA_STYLE_DISPLAY &&
                metrics.width < maximum_width) {
                origin_x += (maximum_width - metrics.width) / 2;
            }
            (void)phy_formula_draw_latex(
                surface, formula, formula_length, style, pixel_size,
                maximum_width, origin_x, top + metrics.ascent, pan,
                COLOR_MARKDOWN, COLOR_CARD, left, top, maximum_width,
                clip_height, NULL);
            if (pan > 0) {
                (void)phy_gfx_draw_text(surface, left, card_y + 2, "<",
                                        COLOR_DIM);
            }
            if (excess > pan) {
                (void)phy_gfx_draw_text(surface, left + maximum_width - 8,
                                        card_y + 2, ">", COLOR_DIM);
            }
            if (!metrics.valid) {
                /*
                 * The layout recovered from a parse error and is showing
                 * "!" plus the raw source. Its own diagnostic is the only
                 * record of why, so put it where the reader is looking.
                 */
                char diagnostic[44];
                (void)phy_formula_last_diagnostic(diagnostic,
                                                  sizeof diagnostic);
                (void)phy_gfx_draw_text(surface, left,
                                        card_y + card_height - 11,
                                        diagnostic, COLOR_ERROR);
            }
            return;
        }
    }

    if (!markdown_has_inline_formula(body)) {
        /* Prose: the card was sized for exactly these wrapped lines. */
        const size_t per_line =
            (size_t)(NOTEBOOK_MARKDOWN_MAX_WIDTH / PHY_GLYPH_ADVANCE);
        const char *cursor = body;
        for (int line = 0; line < NOTEBOOK_MARKDOWN_MAX_LINES &&
                           *cursor != '\0';
             ++line) {
            const size_t take = wrap_take(cursor, per_line);
            (void)draw_text_span(surface, left, card_y + 25 + line * 11,
                                 cursor, take, COLOR_DIM);
            cursor += take;
            while (*cursor == ' ') {
                cursor++;
            }
        }
        if (*cursor != '\0') {
            /* The line cap dropped text; never let that pass silently. */
            (void)phy_gfx_draw_text(surface, NOTEBOOK_MARKDOWN_RIGHT,
                                    card_y + card_height - 9, "+",
                                    COLOR_DIM);
        }
        return;
    }

    markdown_flow flow;
    flow_init(&flow, surface, card_y, clip_height);
    markdown_flow_run(&flow, body);
    if (flow.truncated) {
        (void)phy_gfx_draw_text(surface, NOTEBOOK_MARKDOWN_RIGHT,
                                card_y + card_height - 9, "+", COLOR_DIM);
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
        const int height = display_height(notebook, i);
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
                draw_editable_body_grid(surface, 34, y + 25,
                                        cell->secondary, notebook->cursor);
            } else {
                draw_markdown_body(surface, y, height, cell->secondary,
                                   selected && !notebook->editing
                                       ? notebook->output_pan
                                       : 0);
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
            if (cell->expression == PHY_IR_NULL &&
                cell->primary[0] != '\0') {
                /*
                 * A typed physics object. It has no expansion in the IR, so the
                 * cell reports what it is rather than drawing nothing.
                 */
                draw_wrapped_text(surface, 61, y + 9, maximum_width,
                                  cell->primary, result_color);
                if (cell->stale) {
                    (void)phy_gfx_draw_text(surface, PHY_SCREEN_WIDTH - 39,
                                            y + height - 11, "stale",
                                            COLOR_DIM);
                }
                continue;
            }
            int pixel_size = 0;
            const phy_status measure_status = measure_result_formula(
                notebook, cell, &metrics, &pixel_size);
            if (measure_status == PHY_OK) {
                const int excess =
                    metrics.width > maximum_width
                        ? metrics.width - maximum_width
                        : 0;
                int pan = 0;
                if (selected && !notebook->editing) {
                    pan = notebook->output_pan;
                    if (pan > excess) {
                        pan = excess;
                    }
                }
                const int formula_height = metrics.ascent + metrics.descent;
                const int baseline =
                    y + (height - formula_height) / 2 + metrics.ascent;
                (void)phy_formula_draw_ir(
                    surface, notebook->ir, cell->expression,
                    PHY_FORMULA_STYLE_TEXT, pixel_size, maximum_width, 61,
                    baseline, pan, result_color, fill, 61, y + 1,
                    maximum_width, height - 2, NULL);
                /*
                 * Top-corner markers: content hidden on that side. The
                 * bottom-right corner stays free for the stale label.
                 */
                if (pan > 0) {
                    (void)phy_gfx_draw_text(surface, 61, y + 2, "<",
                                            COLOR_DIM);
                }
                if (excess > pan) {
                    (void)phy_gfx_draw_text(surface,
                                            61 + maximum_width - 8, y + 2,
                                            ">", COLOR_DIM);
                }
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
