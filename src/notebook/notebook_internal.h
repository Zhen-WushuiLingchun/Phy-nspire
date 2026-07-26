#ifndef PHY_NOTEBOOK_INTERNAL_H
#define PHY_NOTEBOOK_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/cas.h"
#include "phy/notebook.h"

#define NOTEBOOK_TEXT_CAPACITY 96u
#define NOTEBOOK_DETAIL_CAPACITY 192u

typedef struct {
    phy_notebook_cell_kind kind;
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
    notebook_cell cells[PHY_NOTEBOOK_MAX_CELLS];
    size_t count;
    size_t selected;
    uint32_t next_execution;

    bool editing;
    size_t edit_index;
    bool edit_secondary;
    size_t cursor;
    int scroll_y;
    bool dirty;
};

#endif /* PHY_NOTEBOOK_INTERNAL_H */
