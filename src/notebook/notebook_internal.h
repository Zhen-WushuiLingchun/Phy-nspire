#ifndef PHY_NOTEBOOK_INTERNAL_H
#define PHY_NOTEBOOK_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/cas.h"
#include "phy/eval.h"
#include "phy/notebook.h"

/*
 * A six-gamma DiracTrace with explicit Lorentz index spaces is 109 bytes of
 * source; 96 cut it off.
 */
#define NOTEBOOK_TEXT_CAPACITY 128u
/*
 * Holds the canonical IR text an input reparses on load. A 4x4 symbolic
 * metric -- the Schwarzschild line in the CAS tour -- canonicalizes to just
 * over 200 bytes, so 192 was the binding constraint.
 */
#define NOTEBOOK_DETAIL_CAPACITY 320u

typedef struct {
    phy_notebook_cell_kind kind;
    /*
     * Markdown: heading.
     * Input: reader-facing source.
     * Output: the descriptor line of a typed physics object, empty for an
     *         ordinary scalar result. It is what the cell shows when the value
     *         has no expansion in the typed IR -- a manifold, a group, a
     *         curvature bundle -- and it persists with the document.
     */
    char primary[NOTEBOOK_TEXT_CAPACITY];
    /*
     * Markdown: paragraph body.
     * Input: stable serialized IR of the most recent successful parse.
     */
    char secondary[NOTEBOOK_DETAIL_CAPACITY];
    phy_ir_ref expression;
    phy_status status;
    uint32_t execution;
    size_t owner_input;
    bool stale;
} notebook_cell;

struct phy_notebook {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_env *env;
    notebook_cell cells[PHY_NOTEBOOK_MAX_CELLS];
    size_t count;
    size_t selected;
    uint32_t next_execution;

    bool editing;
    size_t edit_index;
    bool edit_secondary;
    size_t cursor;
    int scroll_y;
    /*
     * Horizontal viewport offset of the selected output cell's formula, in
     * pixels. Runtime-only: it resets when the selection moves and is never
     * serialized.
     */
    int output_pan;
    bool dirty;
};

#endif /* PHY_NOTEBOOK_INTERNAL_H */
