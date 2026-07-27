/*
 * Context-sensitive insertion catalog for the native notebook editor.
 *
 * The entries are immutable strings stored in the program image. Callers
 * insert `snippet` and leave the caret `cursor_offset` bytes from its start.
 */
#ifndef PHY_PALETTE_H
#define PHY_PALETTE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PHY_PALETTE_CAS = 0,
    PHY_PALETTE_LATEX
} phy_palette_kind;

typedef struct {
    const char *label;
    const char *snippet;
    size_t cursor_offset;
} phy_palette_entry;

size_t phy_palette_category_count(phy_palette_kind kind);
const char *phy_palette_category_name(phy_palette_kind kind,
                                      size_t category);
size_t phy_palette_entry_count(phy_palette_kind kind, size_t category);
bool phy_palette_get(phy_palette_kind kind, size_t category, size_t entry,
                     phy_palette_entry *out_entry);

#ifdef __cplusplus
}
#endif

#endif /* PHY_PALETTE_H */
