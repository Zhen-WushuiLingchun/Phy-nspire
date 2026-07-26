/*
 * Current notebook plus its persistence identity.
 *
 * This layer keeps Save/Open transactional and independent of the UI. The
 * application may switch menus or fail to draw without ever owning partially
 * loaded notebook state.
 */
#ifndef PHY_WORKSPACE_H
#define PHY_WORKSPACE_H

#include <stdbool.h>

#include "phy/notebook.h"
#include "phy/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_workspace phy_workspace;

phy_workspace *phy_workspace_create(void);
void phy_workspace_destroy(phy_workspace *workspace);

phy_notebook *phy_workspace_notebook(phy_workspace *workspace);
const phy_notebook *phy_workspace_notebook_const(
    const phy_workspace *workspace);

bool phy_workspace_has_filename(const phy_workspace *workspace);
const char *phy_workspace_filename(const phy_workspace *workspace);

/* Replace the current document with a clean, empty, unnamed notebook. */
phy_status phy_workspace_new(phy_workspace *workspace);

/*
 * Save under name, or pass NULL to replace the current named file. A failed
 * save leaves both document dirtiness and filename unchanged.
 */
phy_status phy_workspace_save(phy_workspace *workspace, const char *name);

/*
 * Transactional Open: the current document is replaced only after the whole
 * file, CRC, cell graph, and typed IR validate.
 */
phy_status phy_workspace_open(phy_workspace *workspace, const char *name);

phy_status phy_workspace_catalog(const phy_workspace *workspace,
                                 phy_storage_catalog *out_catalog);
phy_status phy_workspace_suggest_name(
    const phy_workspace *workspace,
    char out_name[PHY_STORAGE_NAME_CAPACITY]);

#ifdef __cplusplus
}
#endif

#endif /* PHY_WORKSPACE_H */
