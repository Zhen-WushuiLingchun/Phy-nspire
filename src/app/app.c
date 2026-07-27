/* Native application lifecycle and notebook event loop. */
#include "phy/app.h"

#include <string.h>

#include "phy/formula.h"
#include "phy/notebook.h"
#include "phy/palette.h"
#include "phy/platform.h"
#include "phy/storage.h"
#include "phy/workspace.h"

#define COLOR_BACKGROUND PHY_RGB565(16, 18, 24)
#define COLOR_PANEL PHY_RGB565(28, 32, 44)
#define COLOR_TITLE_BAR PHY_RGB565(48, 84, 140)
#define COLOR_TEXT PHY_RGB565(232, 236, 244)
#define COLOR_TEXT_DIM PHY_RGB565(150, 160, 180)
#define COLOR_BORDER PHY_RGB565(90, 100, 120)
#define COLOR_POINTER PHY_RGB565(255, 208, 64)
#define COLOR_MODAL PHY_RGB565(24, 29, 40)
#define COLOR_MODAL_SELECTED PHY_RGB565(35, 48, 67)
#define COLOR_ACCENT PHY_RGB565(75, 166, 255)
#define COLOR_ERROR PHY_RGB565(247, 103, 116)

typedef enum {
    APP_VIEW_NOTEBOOK = 0,
    APP_VIEW_FILE_MENU,
    APP_VIEW_SAVE_AS,
    APP_VIEW_OPEN,
    APP_VIEW_DIRTY_CONFIRM,
    APP_VIEW_PALETTE
} app_view;

typedef enum {
    APP_PENDING_NONE = 0,
    APP_PENDING_NEW,
    APP_PENDING_OPEN,
    APP_PENDING_QUIT
} app_pending;

typedef struct {
    app_view view;
    app_pending pending;
    size_t selected;
    phy_storage_catalog catalog;
    char edit_name[PHY_STORAGE_NAME_CAPACITY];
    size_t edit_cursor;
    phy_palette_kind palette_kind;
    size_t palette_category;
    phy_status status;
    bool status_visible;
} app_ui;

#define FILE_BUTTON_X 282
#define FILE_BUTTON_Y 2
#define FILE_BUTTON_WIDTH 36
#define FILE_BUTTON_HEIGHT 13
#define MODAL_X 50
#define MODAL_WIDTH 220

static void app_ui_clear_status(app_ui *ui)
{
    ui->status = PHY_OK;
    ui->status_visible = false;
}

static void app_ui_set_status(app_ui *ui, phy_status status)
{
    ui->status = status;
    ui->status_visible = status != PHY_OK;
}

static void copy_visible(char *destination, size_t capacity,
                         const char *source, size_t max_characters)
{
    if (capacity == 0u) {
        return;
    }
    size_t count = 0u;
    while (source[count] != '\0' && count < max_characters &&
           count + 1u < capacity) {
        destination[count] = source[count];
        count++;
    }
    if (source[count] != '\0' && count >= 3u) {
        destination[count - 2u] = '.';
        destination[count - 1u] = '.';
    }
    destination[count] = '\0';
}

static void app_draw_pointer(const phy_surface *surface, int x, int y)
{
    phy_gfx_hline(surface, x - 3, y, 7, COLOR_POINTER);
    phy_gfx_vline(surface, x, y - 3, 7, COLOR_POINTER);
    phy_gfx_put_pixel(surface, x + 2, y + 2, COLOR_POINTER);
}

static void draw_modal_panel(const phy_surface *surface, int y, int height,
                             const char *title)
{
    phy_gfx_fill_rect(surface, MODAL_X, y, MODAL_WIDTH, height, COLOR_MODAL);
    phy_gfx_draw_rect(surface, MODAL_X, y, MODAL_WIDTH, height, COLOR_ACCENT);
    phy_gfx_fill_rect(surface, MODAL_X + 1, y + 1, MODAL_WIDTH - 2, 18,
                      COLOR_TITLE_BAR);
    (void)phy_gfx_draw_text(surface, MODAL_X + 8, y + 6, title, COLOR_TEXT);
}

static void draw_modal_row(const phy_surface *surface, int y,
                           const char *label, bool selected)
{
    if (selected) {
        phy_gfx_fill_rect(surface, MODAL_X + 8, y - 4, MODAL_WIDTH - 16, 17,
                          COLOR_MODAL_SELECTED);
        phy_gfx_draw_rect(surface, MODAL_X + 8, y - 4, MODAL_WIDTH - 16, 17,
                          COLOR_ACCENT);
    }
    (void)phy_gfx_draw_text(surface, MODAL_X + 15, y, label,
                            selected ? COLOR_TEXT : COLOR_TEXT_DIM);
}

static void draw_status(const phy_surface *surface, const app_ui *ui,
                        int y)
{
    if (!ui->status_visible) {
        return;
    }
    (void)phy_gfx_draw_text(surface, MODAL_X + 12, y, "Error:",
                            COLOR_ERROR);
    (void)phy_gfx_draw_text(surface, MODAL_X + 56, y,
                            phy_status_name(ui->status), COLOR_ERROR);
}

/*
 * Four entries. "Run all cells" is not a convenience: cells now share an
 * evaluator environment that a reopened document does not carry, so replaying
 * the notebook in order is the only way to make a loaded one consistent again.
 */
#define APP_FILE_MENU_ITEMS 4u

static void draw_file_menu(const phy_surface *surface, const app_ui *ui)
{
    draw_modal_panel(surface, 42, 154, "Notebook");
    static const char *const items[APP_FILE_MENU_ITEMS] = {
        "New blank notebook", "Save", "Open...", "Run all cells"};
    for (size_t i = 0u; i < APP_FILE_MENU_ITEMS; ++i) {
        draw_modal_row(surface, 72 + (int)i * 25, items[i],
                       ui->selected == i);
    }
    draw_status(surface, ui, 178);
}

static void draw_save_as(const phy_surface *surface, const app_ui *ui)
{
    draw_modal_panel(surface, 51, 120, "Save notebook as");
    phy_gfx_fill_rect(surface, MODAL_X + 10, 82, MODAL_WIDTH - 20, 23,
                      COLOR_BACKGROUND);
    phy_gfx_draw_rect(surface, MODAL_X + 10, 82, MODAL_WIDTH - 20, 23,
                      COLOR_ACCENT);
    char visible[34];
    size_t start = 0u;
    if (ui->edit_cursor >= 31u) {
        start = ui->edit_cursor - 30u;
    }
    copy_visible(visible, sizeof visible, ui->edit_name + start, 31u);
    (void)phy_gfx_draw_text(surface, MODAL_X + 16, 90, visible, COLOR_TEXT);
    const size_t local_cursor =
        ui->edit_cursor >= start ? ui->edit_cursor - start : 0u;
    phy_gfx_vline(surface,
                  MODAL_X + 16 + (int)local_cursor * PHY_GLYPH_ADVANCE, 88,
                  11, COLOR_POINTER);
    (void)phy_gfx_draw_text(surface, MODAL_X + 12, 116,
                            "ENTER save   ESC cancel", COLOR_TEXT_DIM);
    (void)phy_gfx_draw_text(surface, MODAL_X + 12, 130,
                            "Stored in phy-nspire/notebooks/", COLOR_TEXT_DIM);
    draw_status(surface, ui, 149);
}

static size_t open_first_visible(const app_ui *ui)
{
    const size_t visible = 8u;
    if (ui->selected < visible) {
        return 0u;
    }
    return ui->selected - visible + 1u;
}

static void draw_open_list(const phy_surface *surface, const app_ui *ui)
{
    draw_modal_panel(surface, 22, 196, "Open notebook");
    if (ui->catalog.count == 0u) {
        (void)phy_gfx_draw_text(surface, MODAL_X + 15, 63,
                                "No saved notebooks yet.", COLOR_TEXT_DIM);
    } else {
        const size_t first = open_first_visible(ui);
        const size_t limit =
            first + 8u < ui->catalog.count ? first + 8u : ui->catalog.count;
        for (size_t i = first; i < limit; ++i) {
            char visible[31];
            copy_visible(visible, sizeof visible,
                         ui->catalog.entries[i].name, 29u);
            draw_modal_row(surface, 50 + (int)(i - first) * 18, visible,
                           ui->selected == i);
        }
    }
    (void)phy_gfx_draw_text(surface, MODAL_X + 12, 197,
                            "ENTER open   ESC cancel", COLOR_TEXT_DIM);
    draw_status(surface, ui, 184);
}

static void draw_dirty_confirm(const phy_surface *surface, const app_ui *ui)
{
    draw_modal_panel(surface, 52, 132, "Unsaved changes");
    (void)phy_gfx_draw_text(surface, MODAL_X + 14, 79,
                            "Save this notebook first?", COLOR_TEXT);
    static const char *const items[] = {"Save", "Discard", "Cancel"};
    for (size_t i = 0u; i < sizeof items / sizeof items[0]; ++i) {
        draw_modal_row(surface, 105 + (int)i * 21, items[i],
                       ui->selected == i);
    }
    draw_status(surface, ui, 169);
}

static void draw_palette(const phy_surface *surface, const app_ui *ui)
{
    const char *title = ui->palette_kind == PHY_PALETTE_CAS
                            ? "CAS command palette"
                            : "LaTeX command palette";
    draw_modal_panel(surface, 20, 200, title);
    (void)phy_gfx_draw_text(surface, MODAL_X + 10, 48, "<", COLOR_ACCENT);
    const char *category = phy_palette_category_name(
        ui->palette_kind, ui->palette_category);
    if (category != NULL) {
        (void)phy_gfx_draw_text(surface, MODAL_X + 25, 48, category,
                                COLOR_TEXT);
    }
    (void)phy_gfx_draw_text(surface, MODAL_X + MODAL_WIDTH - 17, 48, ">",
                            COLOR_ACCENT);

    const size_t count = phy_palette_entry_count(
        ui->palette_kind, ui->palette_category);
    for (size_t i = 0u; i < count; ++i) {
        phy_palette_entry entry;
        if (phy_palette_get(ui->palette_kind, ui->palette_category, i,
                            &entry)) {
            char visible[33];
            copy_visible(visible, sizeof visible, entry.label, 31u);
            draw_modal_row(surface, 70 + (int)i * 18, visible,
                           ui->selected == i);
        }
    }
    (void)phy_gfx_draw_text(surface, MODAL_X + 12, 201,
                            "LEFT/RIGHT category", COLOR_TEXT_DIM);
    (void)phy_gfx_draw_text(surface, MODAL_X + 12, 212,
                            "ENTER insert  ESC cancel", COLOR_TEXT_DIM);
    draw_status(surface, ui, 190);
}

static void draw_app_view(const phy_surface *surface,
                          const phy_workspace *workspace, const app_ui *ui,
                          int pointer_x, int pointer_y)
{
    const phy_notebook *notebook =
        phy_workspace_notebook_const(workspace);
    phy_notebook_draw_document(
        surface, notebook, phy_workspace_filename(workspace),
        phy_notebook_is_dirty(notebook),
        ui->view == APP_VIEW_NOTEBOOK ? pointer_x : -1,
        ui->view == APP_VIEW_NOTEBOOK ? pointer_y : -1);
    switch (ui->view) {
    case APP_VIEW_FILE_MENU:
        draw_file_menu(surface, ui);
        break;
    case APP_VIEW_SAVE_AS:
        draw_save_as(surface, ui);
        break;
    case APP_VIEW_OPEN:
        draw_open_list(surface, ui);
        break;
    case APP_VIEW_DIRTY_CONFIRM:
        draw_dirty_confirm(surface, ui);
        break;
    case APP_VIEW_PALETTE:
        draw_palette(surface, ui);
        break;
    case APP_VIEW_NOTEBOOK:
    default:
        break;
    }
    if (ui->view != APP_VIEW_NOTEBOOK && pointer_x >= 0 && pointer_y >= 0) {
        app_draw_pointer(surface, pointer_x, pointer_y);
    }
}

static void begin_file_menu(app_ui *ui)
{
    ui->view = APP_VIEW_FILE_MENU;
    ui->pending = APP_PENDING_NONE;
    ui->selected = 0u;
    app_ui_clear_status(ui);
}

static bool begin_palette(app_ui *ui, phy_notebook *notebook)
{
    phy_notebook_edit_target target =
        phy_notebook_edit_target_kind(notebook);
    if (target == PHY_NOTEBOOK_EDIT_MARKDOWN_HEADING) {
        if (!phy_notebook_edit_switch_field(notebook)) {
            return false;
        }
        target = PHY_NOTEBOOK_EDIT_MARKDOWN_BODY;
    }
    if (target != PHY_NOTEBOOK_EDIT_MATH &&
        target != PHY_NOTEBOOK_EDIT_MARKDOWN_BODY) {
        return false;
    }
    ui->palette_kind = target == PHY_NOTEBOOK_EDIT_MATH
                           ? PHY_PALETTE_CAS
                           : PHY_PALETTE_LATEX;
    ui->palette_category = 0u;
    ui->selected = 0u;
    ui->view = APP_VIEW_PALETTE;
    app_ui_clear_status(ui);
    return true;
}

static phy_status begin_save_as(app_ui *ui,
                                const phy_workspace *workspace)
{
    const phy_status status =
        phy_workspace_suggest_name(workspace, ui->edit_name);
    if (status != PHY_OK) {
        app_ui_set_status(ui, status);
        return status;
    }
    ui->edit_cursor = strlen(ui->edit_name);
    ui->view = APP_VIEW_SAVE_AS;
    app_ui_clear_status(ui);
    return PHY_OK;
}

static phy_status begin_open(app_ui *ui, const phy_workspace *workspace)
{
    const phy_status status =
        phy_workspace_catalog(workspace, &ui->catalog);
    if (status != PHY_OK) {
        app_ui_set_status(ui, status);
        return status;
    }
    ui->selected = 0u;
    ui->view = APP_VIEW_OPEN;
    app_ui_clear_status(ui);
    return PHY_OK;
}

static void request_pending(app_ui *ui, phy_workspace *workspace,
                            app_pending pending, bool *running)
{
    if (phy_notebook_is_dirty(phy_workspace_notebook(workspace))) {
        ui->pending = pending;
        ui->selected = 0u;
        ui->view = APP_VIEW_DIRTY_CONFIRM;
        app_ui_clear_status(ui);
        return;
    }
    if (pending == APP_PENDING_NEW) {
        app_ui_set_status(ui, phy_workspace_new(workspace));
        ui->view = APP_VIEW_NOTEBOOK;
    } else if (pending == APP_PENDING_OPEN) {
        (void)begin_open(ui, workspace);
    } else if (pending == APP_PENDING_QUIT) {
        *running = false;
    }
}

static void complete_pending(app_ui *ui, phy_workspace *workspace,
                             bool *running)
{
    const app_pending pending = ui->pending;
    ui->pending = APP_PENDING_NONE;
    if (pending == APP_PENDING_NONE) {
        ui->view = APP_VIEW_NOTEBOOK;
    } else if (pending == APP_PENDING_NEW) {
        app_ui_set_status(ui, phy_workspace_new(workspace));
        ui->view = APP_VIEW_NOTEBOOK;
    } else if (pending == APP_PENDING_OPEN) {
        (void)begin_open(ui, workspace);
    } else if (pending == APP_PENDING_QUIT) {
        *running = false;
    }
}

static void activate_file_menu(app_ui *ui, phy_workspace *workspace,
                               bool *running)
{
    if (ui->selected == 0u) {
        request_pending(ui, workspace, APP_PENDING_NEW, running);
    } else if (ui->selected == 1u) {
        phy_status status = PHY_OK;
        if (phy_workspace_has_filename(workspace)) {
            status = phy_workspace_save(workspace, NULL);
            app_ui_set_status(ui, status);
            if (status == PHY_OK) {
                ui->view = APP_VIEW_NOTEBOOK;
            }
        } else {
            (void)begin_save_as(ui, workspace);
        }
    } else if (ui->selected == 2u) {
        request_pending(ui, workspace, APP_PENDING_OPEN, running);
    } else {
        const phy_status status =
            phy_notebook_evaluate_all(phy_workspace_notebook(workspace));
        app_ui_set_status(ui, status);
        /*
         * A failing cell is a result, not a reason to keep the menu open: the
         * notebook itself now shows which cell it was.
         */
        ui->view = APP_VIEW_NOTEBOOK;
    }
}

static void activate_dirty(app_ui *ui, phy_workspace *workspace,
                           bool *running)
{
    if (ui->selected == 0u) {
        if (phy_workspace_has_filename(workspace)) {
            const phy_status status = phy_workspace_save(workspace, NULL);
            app_ui_set_status(ui, status);
            if (status == PHY_OK) {
                complete_pending(ui, workspace, running);
            }
        } else {
            (void)begin_save_as(ui, workspace);
        }
    } else if (ui->selected == 1u) {
        complete_pending(ui, workspace, running);
    } else {
        ui->pending = APP_PENDING_NONE;
        ui->view = APP_VIEW_NOTEBOOK;
        app_ui_clear_status(ui);
    }
}

static void activate_save_as(app_ui *ui, phy_workspace *workspace,
                             bool *running)
{
    const phy_status status =
        phy_workspace_save(workspace, ui->edit_name);
    app_ui_set_status(ui, status);
    if (status == PHY_OK) {
        complete_pending(ui, workspace, running);
    }
}

static void activate_open(app_ui *ui, phy_workspace *workspace)
{
    if (ui->catalog.count == 0u || ui->selected >= ui->catalog.count) {
        return;
    }
    const phy_status status = phy_workspace_open(
        workspace, ui->catalog.entries[ui->selected].name);
    app_ui_set_status(ui, status);
    if (status == PHY_OK) {
        ui->view = APP_VIEW_NOTEBOOK;
    }
}

static void activate_palette(app_ui *ui, phy_notebook *notebook)
{
    phy_palette_entry entry;
    if (!phy_palette_get(ui->palette_kind, ui->palette_category,
                         ui->selected, &entry)) {
        return;
    }
    if (!phy_notebook_edit_insert_text(notebook, entry.snippet,
                                       entry.cursor_offset)) {
        app_ui_set_status(ui, PHY_ERR_TERM_LIMIT);
        return;
    }
    ui->view = APP_VIEW_NOTEBOOK;
    app_ui_clear_status(ui);
}

static void move_palette_category(app_ui *ui, int direction)
{
    const size_t count = phy_palette_category_count(ui->palette_kind);
    if (count == 0u || direction == 0) {
        return;
    }
    if (direction > 0) {
        ui->palette_category = (ui->palette_category + 1u) % count;
    } else {
        ui->palette_category =
            ui->palette_category == 0u ? count - 1u
                                       : ui->palette_category - 1u;
    }
    ui->selected = 0u;
    app_ui_clear_status(ui);
}

static bool pointer_in_rect(const phy_event *event, int x, int y, int width,
                            int height)
{
    return event->x >= x && event->x < x + width && event->y >= y &&
           event->y < y + height;
}

static void handle_modal_pointer(app_ui *ui, phy_workspace *workspace,
                                 const phy_event *event, bool *running)
{
    if (ui->view == APP_VIEW_FILE_MENU) {
        for (size_t i = 0u; i < APP_FILE_MENU_ITEMS; ++i) {
            if (pointer_in_rect(event, MODAL_X + 8,
                                68 + (int)i * 25, MODAL_WIDTH - 16, 21)) {
                ui->selected = i;
                activate_file_menu(ui, workspace, running);
                return;
            }
        }
    } else if (ui->view == APP_VIEW_OPEN) {
        const size_t first = open_first_visible(ui);
        const size_t limit =
            first + 8u < ui->catalog.count ? first + 8u : ui->catalog.count;
        for (size_t i = first; i < limit; ++i) {
            if (pointer_in_rect(event, MODAL_X + 8,
                                46 + (int)(i - first) * 18,
                                MODAL_WIDTH - 16, 17)) {
                ui->selected = i;
                activate_open(ui, workspace);
                return;
            }
        }
    } else if (ui->view == APP_VIEW_DIRTY_CONFIRM) {
        for (size_t i = 0u; i < 3u; ++i) {
            if (pointer_in_rect(event, MODAL_X + 8,
                                101 + (int)i * 21, MODAL_WIDTH - 16, 17)) {
                ui->selected = i;
                activate_dirty(ui, workspace, running);
                return;
            }
        }
    } else if (ui->view == APP_VIEW_PALETTE) {
        if (pointer_in_rect(event, MODAL_X + 4, 42, 18, 20)) {
            move_palette_category(ui, -1);
            return;
        }
        if (pointer_in_rect(event, MODAL_X + MODAL_WIDTH - 22, 42, 18,
                            20)) {
            move_palette_category(ui, 1);
            return;
        }
        const size_t count = phy_palette_entry_count(
            ui->palette_kind, ui->palette_category);
        for (size_t i = 0u; i < count; ++i) {
            if (pointer_in_rect(event, MODAL_X + 8,
                                66 + (int)i * 18, MODAL_WIDTH - 16, 17)) {
                ui->selected = i;
                activate_palette(ui, phy_workspace_notebook(workspace));
                return;
            }
        }
    }
}

static void move_modal_selection(app_ui *ui, int direction)
{
    size_t count = 0u;
    if (ui->view == APP_VIEW_FILE_MENU) {
        count = APP_FILE_MENU_ITEMS;
    } else if (ui->view == APP_VIEW_DIRTY_CONFIRM) {
        count = 3u;
    } else if (ui->view == APP_VIEW_OPEN) {
        count = ui->catalog.count;
    } else if (ui->view == APP_VIEW_PALETTE) {
        count = phy_palette_entry_count(ui->palette_kind,
                                        ui->palette_category);
    }
    if (count == 0u || direction == 0) {
        return;
    }
    if (direction > 0) {
        ui->selected = (ui->selected + 1u) % count;
    } else {
        ui->selected = ui->selected == 0u ? count - 1u : ui->selected - 1u;
    }
}

static void handle_save_name_text(app_ui *ui, char character)
{
    if ((unsigned char)character < (unsigned char)' ' ||
        (unsigned char)character > (unsigned char)'~') {
        return;
    }
    const size_t length = strlen(ui->edit_name);
    if (length + 1u >= sizeof ui->edit_name || ui->edit_cursor > length) {
        return;
    }
    memmove(ui->edit_name + ui->edit_cursor + 1u,
            ui->edit_name + ui->edit_cursor,
            length - ui->edit_cursor + 1u);
    ui->edit_name[ui->edit_cursor++] = character;
    app_ui_clear_status(ui);
}

static void handle_save_name_backspace(app_ui *ui)
{
    if (ui->edit_cursor == 0u) {
        return;
    }
    const size_t length = strlen(ui->edit_name);
    if (ui->edit_cursor > length) {
        ui->edit_cursor = length;
    }
    memmove(ui->edit_name + ui->edit_cursor - 1u,
            ui->edit_name + ui->edit_cursor,
            length - ui->edit_cursor + 1u);
    ui->edit_cursor--;
    app_ui_clear_status(ui);
}

/*
 * Bounded text assembly.
 *
 * snprintf would be the obvious choice, but it drags newlib's full formatter,
 * including floating point, into the device binary: 12.7 KB of a 53 KB
 * program, to print two integers. tools/symbol-report.sh is what surfaced
 * that. These two helpers cost a few dozen bytes instead.
 */
typedef struct {
    char *data;
    size_t capacity;
    size_t length;
} text_builder;

static void text_begin(text_builder *builder, char *storage, size_t capacity)
{
    builder->data = storage;
    builder->capacity = capacity;
    builder->length = 0;
    if (capacity > 0) {
        storage[0] = '\0';
    }
}

static void text_append(text_builder *builder, const char *text)
{
    if (builder->capacity == 0) {
        return;
    }
    while (*text != '\0' && builder->length + 1 < builder->capacity) {
        builder->data[builder->length++] = *text++;
    }
    builder->data[builder->length] = '\0';
}

static void text_append_uint(text_builder *builder, unsigned long value)
{
    /* 20 digits holds any 64-bit value; the terminator is written separately. */
    char digits[20];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + (char)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul && count < sizeof digits);

    if (builder->capacity == 0) {
        return;
    }
    while (count > 0 && builder->length + 1 < builder->capacity) {
        builder->data[builder->length++] = digits[--count];
    }
    builder->data[builder->length] = '\0';
}

void phy_app_options_defaults(phy_app_options *out_options)
{
    if (out_options == NULL) {
        return;
    }
    out_options->max_frames = 0; /* run until quit */
    out_options->idle_sleep_ms = 10;
}

/*
 * Pure red, green and blue bars. On a panel wired for BGR these come out
 * swapped, which is the cheapest way to catch a channel-order mistake on real
 * hardware before any of the notebook rendering is built on top of it.
 */
static void draw_channel_check(const phy_surface *surface, int x, int y,
                               int width, int height)
{
    const uint16_t bars[3] = {
        PHY_RGB565(255, 0, 0),
        PHY_RGB565(0, 255, 0),
        PHY_RGB565(0, 0, 255),
    };
    const int bar_width = width / 3;
    for (int i = 0; i < 3; ++i) {
        /* Last bar absorbs the rounding remainder so the block stays flush. */
        const int this_width = (i == 2) ? (width - 2 * bar_width) : bar_width;
        phy_gfx_fill_rect(surface, x + i * bar_width, y, this_width, height,
                          bars[i]);
    }
    phy_gfx_draw_rect(surface, x, y, width, height, COLOR_BORDER);
}

static void draw_pointer(const phy_surface *surface, int x, int y)
{
    phy_gfx_hline(surface, x - 4, y, 9, COLOR_POINTER);
    phy_gfx_vline(surface, x, y - 4, 9, COLOR_POINTER);
}

void phy_app_draw_baseline(const phy_surface *surface, int pointer_x,
                           int pointer_y)
{
    if (surface == NULL || surface->pixels == NULL) {
        return;
    }
    const int width = surface->width;
    const int height = surface->height;

    phy_gfx_clear(surface, COLOR_BACKGROUND);

    /* Title bar. */
    phy_gfx_fill_rect(surface, 0, 0, width, 16, COLOR_TITLE_BAR);
    phy_gfx_draw_text(surface, 4, 5, "Phy-nspire " PHY_VERSION_STRING, COLOR_TEXT);

    const char *platform = phy_platform_name();
    phy_gfx_draw_text(surface, width - phy_gfx_text_width(platform) - 4, 5,
                      platform, COLOR_TEXT);

    /* Body panel. */
    const int panel_x = 4;
    const int panel_y = 20;
    const int panel_w = width - 8;
    const int panel_h = height - 20 - 16;
    phy_gfx_fill_rect(surface, panel_x, panel_y, panel_w, panel_h, COLOR_PANEL);
    phy_gfx_draw_rect(surface, panel_x, panel_y, panel_w, panel_h, COLOR_BORDER);

    int line_y = panel_y + 6;
    const int text_x = panel_x + 6;

    phy_gfx_draw_text(surface, text_x, line_y, "Phase 0 native baseline",
                      COLOR_TEXT);
    line_y += PHY_TEXT_LINE_HEIGHT + 3;

    char buffer[64];
    text_builder line;

    text_begin(&line, buffer, sizeof buffer);
    text_append(&line, "framebuffer  ");
    text_append_uint(&line, (unsigned long)width);
    text_append(&line, "x");
    text_append_uint(&line, (unsigned long)height);
    text_append(&line, " RGB565");
    phy_gfx_draw_text(surface, text_x, line_y, buffer, COLOR_TEXT_DIM);
    line_y += PHY_TEXT_LINE_HEIGHT;

    text_begin(&line, buffer, sizeof buffer);
    text_append(&line, "backbuffer   ");
    text_append_uint(&line,
                     (unsigned long)((size_t)width * (size_t)height *
                                     sizeof(uint16_t)));
    text_append(&line, " bytes");
    phy_gfx_draw_text(surface, text_x, line_y, buffer, COLOR_TEXT_DIM);
    line_y += PHY_TEXT_LINE_HEIGHT;

    phy_gfx_draw_text(surface, text_x, line_y, "input        keypad + touchpad",
                      COLOR_TEXT_DIM);
    line_y += PHY_TEXT_LINE_HEIGHT + 6;

    phy_gfx_draw_text(surface, text_x, line_y, "channel check R G B:",
                      COLOR_TEXT_DIM);
    draw_channel_check(surface, text_x, line_y + PHY_TEXT_LINE_HEIGHT + 2, 132, 20);

    /* Footer. */
    const char *footer = "ESC quit   touchpad moves pointer";
    phy_gfx_draw_text(surface, 4, height - 11, footer, COLOR_TEXT_DIM);

    if (pointer_x >= 0 && pointer_y >= 0) {
        draw_pointer(surface, pointer_x, pointer_y);
    }
}

phy_status phy_app_run(const phy_app_options *options, phy_app_result *out_result)
{
    if (!phy_platform_is_initialized()) {
        return PHY_ERR_NOT_INITIALIZED;
    }

    phy_app_options effective;
    phy_app_options_defaults(&effective);
    if (options != NULL) {
        effective = *options;
    }

    uint16_t *pixels = phy_display_pixels();
    if (pixels == NULL) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    const phy_surface surface = {pixels, PHY_SCREEN_WIDTH, PHY_SCREEN_HEIGHT};

    const phy_status formula_status = phy_formula_initialize();
    if (formula_status != PHY_OK) {
        return formula_status;
    }

    phy_workspace *workspace = phy_workspace_create();
    if (workspace == NULL) {
        phy_formula_shutdown();
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_notebook *notebook = phy_workspace_notebook(workspace);
    app_ui ui;
    memset(&ui, 0, sizeof ui);
    ui.view = APP_VIEW_NOTEBOOK;

    phy_app_result result;
    memset(&result, 0, sizeof result);
    result.pointer_x = PHY_SCREEN_WIDTH / 2;
    result.pointer_y = PHY_SCREEN_HEIGHT / 2;

    bool needs_redraw = true;
    bool running = true;

    while (running) {
        if (needs_redraw) {
            draw_app_view(&surface, workspace, &ui, (int)result.pointer_x,
                          (int)result.pointer_y);
            const phy_status status = phy_display_present();
            if (status != PHY_OK) {
                if (out_result != NULL) {
                    *out_result = result;
                }
                phy_workspace_destroy(workspace);
                phy_formula_shutdown();
                return status;
            }
            result.frames_presented++;
            result.last_frame_digest = phy_gfx_digest(&surface);
            needs_redraw = false;

            if (effective.max_frames != 0 &&
                result.frames_presented >= effective.max_frames) {
                break;
            }
        }

        phy_event event;
        if (!phy_input_poll(&event)) {
            phy_sleep_ms(effective.idle_sleep_ms);
            continue;
        }
        result.events_handled++;

        switch (event.kind) {
        case PHY_EVENT_QUIT:
            if (ui.view == APP_VIEW_NOTEBOOK &&
                phy_notebook_is_dirty(notebook)) {
                request_pending(&ui, workspace, APP_PENDING_QUIT, &running);
                needs_redraw = true;
            } else {
                result.quit_requested = true;
                running = false;
            }
            break;
        case PHY_EVENT_KEY_DOWN:
            if (ui.view != APP_VIEW_NOTEBOOK) {
                if (event.key == PHY_KEY_ESC) {
                    if (ui.view == APP_VIEW_DIRTY_CONFIRM) {
                        ui.pending = APP_PENDING_NONE;
                    }
                    ui.view = APP_VIEW_NOTEBOOK;
                    app_ui_clear_status(&ui);
                } else if (event.key == PHY_KEY_UP) {
                    move_modal_selection(&ui, -1);
                } else if (event.key == PHY_KEY_DOWN ||
                           event.key == PHY_KEY_TAB) {
                    move_modal_selection(&ui, 1);
                } else if (ui.view == APP_VIEW_PALETTE &&
                           event.key == PHY_KEY_LEFT) {
                    move_palette_category(&ui, -1);
                } else if (ui.view == APP_VIEW_PALETTE &&
                           event.key == PHY_KEY_RIGHT) {
                    move_palette_category(&ui, 1);
                } else if (ui.view == APP_VIEW_SAVE_AS &&
                           event.key == PHY_KEY_LEFT &&
                           ui.edit_cursor > 0u) {
                    ui.edit_cursor--;
                } else if (ui.view == APP_VIEW_SAVE_AS &&
                           event.key == PHY_KEY_RIGHT &&
                           ui.edit_cursor < strlen(ui.edit_name)) {
                    ui.edit_cursor++;
                } else if (ui.view == APP_VIEW_SAVE_AS &&
                           event.key == PHY_KEY_BACKSPACE) {
                    handle_save_name_backspace(&ui);
                } else if (event.key == PHY_KEY_ENTER) {
                    if (ui.view == APP_VIEW_FILE_MENU) {
                        activate_file_menu(&ui, workspace, &running);
                    } else if (ui.view == APP_VIEW_SAVE_AS) {
                        activate_save_as(&ui, workspace, &running);
                    } else if (ui.view == APP_VIEW_OPEN) {
                        activate_open(&ui, workspace);
                    } else if (ui.view == APP_VIEW_DIRTY_CONFIRM) {
                        activate_dirty(&ui, workspace, &running);
                    } else if (ui.view == APP_VIEW_PALETTE) {
                        activate_palette(&ui, notebook);
                    }
                }
                notebook = phy_workspace_notebook(workspace);
                needs_redraw = true;
            } else if (event.key == PHY_KEY_ESC) {
                if (phy_notebook_is_editing(notebook)) {
                    phy_notebook_end_edit(notebook);
                    needs_redraw = true;
                } else {
                    request_pending(&ui, workspace, APP_PENDING_QUIT,
                                    &running);
                    if (!running) {
                        result.quit_requested = true;
                    }
                    needs_redraw = true;
                }
            } else if (event.key == PHY_KEY_MENU) {
                if (phy_notebook_is_editing(notebook)) {
                    (void)begin_palette(&ui, notebook);
                } else {
                    begin_file_menu(&ui);
                }
                needs_redraw = true;
            } else if (event.key == PHY_KEY_BACKSPACE &&
                       phy_notebook_is_editing(notebook)) {
                needs_redraw =
                    phy_notebook_edit_backspace(notebook) || needs_redraw;
            } else if (event.key == PHY_KEY_UP ||
                       event.key == PHY_KEY_LEFT) {
                if (phy_notebook_is_editing(notebook) &&
                    event.key == PHY_KEY_LEFT) {
                    needs_redraw =
                        phy_notebook_edit_move(notebook, -1) || needs_redraw;
                } else if (event.key == PHY_KEY_LEFT &&
                           phy_notebook_pan_selected(notebook, -1)) {
                    needs_redraw = true;
                } else {
                    needs_redraw =
                        phy_notebook_select_next(notebook, -1) || needs_redraw;
                }
            } else if (event.key == PHY_KEY_DOWN ||
                       event.key == PHY_KEY_RIGHT ||
                       event.key == PHY_KEY_TAB) {
                if (phy_notebook_is_editing(notebook) &&
                    event.key == PHY_KEY_RIGHT) {
                    needs_redraw =
                        phy_notebook_edit_move(notebook, 1) || needs_redraw;
                } else if (phy_notebook_is_editing(notebook) &&
                           event.key == PHY_KEY_TAB) {
                    needs_redraw =
                        phy_notebook_edit_switch_field(notebook) ||
                        needs_redraw;
                } else if (event.key == PHY_KEY_RIGHT &&
                           phy_notebook_pan_selected(notebook, 1)) {
                    needs_redraw = true;
                } else {
                    needs_redraw =
                        phy_notebook_select_next(notebook, 1) || needs_redraw;
                }
            } else if (event.key == PHY_KEY_ENTER) {
                if (phy_notebook_activate_selected(notebook) ==
                    PHY_ERR_UNSUPPORTED) {
                    (void)phy_notebook_begin_edit_selected(notebook);
                }
                needs_redraw = true;
            }
            break;
        case PHY_EVENT_POINTER_MOVE:
        case PHY_EVENT_POINTER_DOWN:
        case PHY_EVENT_POINTER_UP:
            if (event.x >= 0 && event.x < PHY_SCREEN_WIDTH && event.y >= 0 &&
                event.y < PHY_SCREEN_HEIGHT) {
                result.pointer_x = (uint32_t)event.x;
                result.pointer_y = (uint32_t)event.y;
                needs_redraw = true;
                if (event.kind == PHY_EVENT_POINTER_DOWN) {
                    if (ui.view != APP_VIEW_NOTEBOOK) {
                        handle_modal_pointer(&ui, workspace, &event, &running);
                        notebook = phy_workspace_notebook(workspace);
                    } else if (pointer_in_rect(
                                   &event, FILE_BUTTON_X, FILE_BUTTON_Y,
                                   FILE_BUTTON_WIDTH, FILE_BUTTON_HEIGHT)) {
                        if (!phy_notebook_is_editing(notebook)) {
                            begin_file_menu(&ui);
                        }
                    } else {
                        phy_status action_status = PHY_OK;
                        if (!phy_notebook_insert_at(notebook, event.x, event.y,
                                                    &action_status) &&
                            !phy_notebook_run_at(notebook, event.x, event.y,
                                                 &action_status) &&
                            !phy_notebook_begin_edit_at(notebook, event.x,
                                                        event.y)) {
                            (void)phy_notebook_select_at(notebook, event.x,
                                                         event.y);
                        }
                        (void)action_status;
                    }
                }
            }
            break;
        case PHY_EVENT_TEXT_INPUT:
            if (ui.view == APP_VIEW_SAVE_AS) {
                handle_save_name_text(&ui, event.text);
                needs_redraw = true;
            } else if (ui.view == APP_VIEW_NOTEBOOK) {
                if (!phy_notebook_is_editing(notebook)) {
                    (void)phy_notebook_begin_edit_selected(notebook);
                }
                needs_redraw =
                    phy_notebook_edit_insert(notebook, event.text) ||
                    needs_redraw;
            }
            break;
        case PHY_EVENT_KEY_UP:
        case PHY_EVENT_NONE:
        default:
            break;
        }
        if (!running) {
            result.quit_requested = true;
        }
    }

    if (out_result != NULL) {
        *out_result = result;
    }
    phy_workspace_destroy(workspace);
    phy_formula_shutdown();
    return PHY_OK;
}
