/*
 * Native notebook cell model and 320x240 shell.
 *
 * The model owns one typed-IR context, one native CAS, and one evaluator
 * environment. Cell source remains separate from evaluated IR so a failed
 * calculation never makes the document unsaveable. Storage is bounded: this
 * first shell has twelve cells and fixed source buffers rather than untracked
 * heap growth.
 *
 * Cells are no longer independent. A cell may bind a name that later cells
 * read -- see include/phy/eval.h -- so running one marks every following result
 * stale, and a document reopened from disk starts with an empty environment
 * until phy_notebook_evaluate_all replays it.
 */
#ifndef PHY_NOTEBOOK_H
#define PHY_NOTEBOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/eval.h"
#include "phy/gfx.h"
#include "phy/ir.h"
#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHY_NOTEBOOK_MAX_CELLS 12u
#define PHY_NOTEBOOK_DOCUMENT_MAX_BYTES (64u * 1024u)

typedef struct phy_notebook phy_notebook;

typedef enum {
    PHY_NOTEBOOK_CELL_MARKDOWN = 0,
    PHY_NOTEBOOK_CELL_INPUT,
    PHY_NOTEBOOK_CELL_OUTPUT,
    PHY_NOTEBOOK_CELL_ERROR
} phy_notebook_cell_kind;

typedef enum {
    PHY_NOTEBOOK_EDIT_NONE = 0,
    PHY_NOTEBOOK_EDIT_MATH,
    PHY_NOTEBOOK_EDIT_MARKDOWN_HEADING,
    PHY_NOTEBOOK_EDIT_MARKDOWN_BODY
} phy_notebook_edit_target;

/*
 * Read-only snapshot. Text pointers remain valid until the notebook is
 * modified; callers should not retain them across add/evaluate calls.
 *
 * For an output cell `primary` is the descriptor of a typed physics object and
 * is empty for an ordinary scalar result, which is carried by `expression`
 * instead. Exactly one of the two is populated on a successful output.
 */
typedef struct {
    phy_notebook_cell_kind kind;
    const char *primary;
    const char *secondary;
    phy_ir_ref expression;
    phy_status status;
    uint32_t execution;
    bool stale;
} phy_notebook_cell_view;

phy_notebook *phy_notebook_create(void);
void phy_notebook_destroy(phy_notebook *notebook);

phy_status phy_notebook_add_markdown(phy_notebook *notebook,
                                     const char *heading, const char *body,
                                     size_t *out_index);
phy_status phy_notebook_add_input(phy_notebook *notebook,
                                  const char *source, size_t *out_index);

/*
 * Evaluate one input against the current environment and insert/update its
 * following output cell. Every result after `input_index` becomes stale,
 * because a cell that binds a name changes what the cells below it mean.
 */
phy_status phy_notebook_evaluate(phy_notebook *notebook, size_t input_index);

/*
 * Clear the environment and replay every input in order. This is what makes a
 * reopened document consistent again: the codec restores cells, never objects.
 * Returns the first failing status; later cells still run, so the reader can
 * see which of them depended on the failure.
 */
phy_status phy_notebook_evaluate_all(phy_notebook *notebook);

phy_status phy_notebook_activate_selected(phy_notebook *notebook);

size_t phy_notebook_cell_count(const phy_notebook *notebook);
bool phy_notebook_cell(const phy_notebook *notebook, size_t index,
                       phy_notebook_cell_view *out_view);
const phy_ir_context *phy_notebook_ir(const phy_notebook *notebook);

/* The notebook's evaluator environment; NULL for a NULL notebook. */
phy_env *phy_notebook_environment(phy_notebook *notebook);

size_t phy_notebook_selected(const phy_notebook *notebook);
bool phy_notebook_select(phy_notebook *notebook, size_t index);
bool phy_notebook_select_next(phy_notebook *notebook, int direction);
bool phy_notebook_select_at(phy_notebook *notebook, int x, int y);

/*
 * Runs the input whose upper-right RUN badge contains (x,y). Returns true
 * when a badge was hit; the evaluation result is written separately because
 * a real cell error is different from missing the button.
 */
bool phy_notebook_run_at(phy_notebook *notebook, int x, int y,
                         phy_status *out_status);

/* Functional footer buttons: insert a Markdown or Math cell after selection. */
bool phy_notebook_insert_at(phy_notebook *notebook, int x, int y,
                            phy_status *out_status);

/* Character editor shared by Markdown fields and Math source. */
bool phy_notebook_begin_edit_at(phy_notebook *notebook, int x, int y);
bool phy_notebook_begin_edit_selected(phy_notebook *notebook);
bool phy_notebook_is_editing(const phy_notebook *notebook);
phy_notebook_edit_target
phy_notebook_edit_target_kind(const phy_notebook *notebook);
bool phy_notebook_is_dirty(const phy_notebook *notebook);
void phy_notebook_mark_clean(phy_notebook *notebook);
void phy_notebook_end_edit(phy_notebook *notebook);
bool phy_notebook_edit_insert(phy_notebook *notebook, char character);
/*
 * Insert one ASCII template transactionally and leave the cursor at
 * cursor_offset bytes from the template start.
 */
bool phy_notebook_edit_insert_text(phy_notebook *notebook, const char *text,
                                   size_t cursor_offset);
bool phy_notebook_edit_backspace(phy_notebook *notebook);
bool phy_notebook_edit_move(phy_notebook *notebook, int direction);
bool phy_notebook_edit_switch_field(phy_notebook *notebook);

/*
 * Versioned, checksummed document codec. Loading is transactional and returns
 * a newly allocated notebook only after the whole document validates.
 *
 * Serialize follows snprintf sizing conventions: *out_size is the required
 * byte count. A NULL/short buffer returns PHY_ERR_INVALID_ARGUMENT after
 * setting it.
 */
phy_status phy_notebook_serialize(const phy_notebook *notebook,
                                  uint8_t *buffer, size_t capacity,
                                  size_t *out_size);
phy_status phy_notebook_deserialize(const uint8_t *buffer, size_t size,
                                    phy_notebook **out_notebook);

/* Seed the first runnable document used by the application shell. */
phy_status phy_notebook_seed_welcome(phy_notebook *notebook);

/* Deterministic native renderer; negative pointer coordinates omit the cursor. */
void phy_notebook_draw(const phy_surface *surface,
                       const phy_notebook *notebook, int pointer_x,
                       int pointer_y);
/*
 * Product chrome variant. filename may be NULL for a new document; dirty is
 * shown independently so the app can expose unsaved state in the title bar.
 */
void phy_notebook_draw_document(const phy_surface *surface,
                                const phy_notebook *notebook,
                                const char *filename, bool dirty,
                                int pointer_x, int pointer_y);

#ifdef __cplusplus
}
#endif

#endif /* PHY_NOTEBOOK_H */
