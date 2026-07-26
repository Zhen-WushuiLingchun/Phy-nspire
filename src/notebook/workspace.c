#include "phy/workspace.h"

#include <string.h>

#include "phy/platform.h"

struct phy_workspace {
    phy_notebook *notebook;
    char filename[PHY_STORAGE_NAME_CAPACITY];
    bool has_filename;
};

static phy_status copy_filename(phy_workspace *workspace, const char *name)
{
    if (!phy_storage_name_valid(name)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const size_t length = strlen(name);
    memcpy(workspace->filename, name, length + 1u);
    workspace->has_filename = true;
    return PHY_OK;
}

phy_workspace *phy_workspace_create(void)
{
    if (phy_storage_ensure_notebook_directory() != PHY_OK) {
        return NULL;
    }
    phy_workspace *workspace = phy_alloc(sizeof *workspace);
    if (workspace == NULL) {
        return NULL;
    }
    memset(workspace, 0, sizeof *workspace);
    workspace->notebook = phy_notebook_create();
    if (workspace->notebook == NULL) {
        phy_free(workspace, sizeof *workspace);
        return NULL;
    }
    phy_notebook_mark_clean(workspace->notebook);
    return workspace;
}

void phy_workspace_destroy(phy_workspace *workspace)
{
    if (workspace == NULL) {
        return;
    }
    phy_notebook_destroy(workspace->notebook);
    phy_free(workspace, sizeof *workspace);
}

phy_notebook *phy_workspace_notebook(phy_workspace *workspace)
{
    return workspace != NULL ? workspace->notebook : NULL;
}

const phy_notebook *phy_workspace_notebook_const(
    const phy_workspace *workspace)
{
    return workspace != NULL ? workspace->notebook : NULL;
}

bool phy_workspace_has_filename(const phy_workspace *workspace)
{
    return workspace != NULL && workspace->has_filename;
}

const char *phy_workspace_filename(const phy_workspace *workspace)
{
    return phy_workspace_has_filename(workspace) ? workspace->filename : NULL;
}

phy_status phy_workspace_new(phy_workspace *workspace)
{
    if (workspace == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_notebook *replacement = phy_notebook_create();
    if (replacement == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_notebook_mark_clean(replacement);
    phy_notebook_destroy(workspace->notebook);
    workspace->notebook = replacement;
    workspace->filename[0] = '\0';
    workspace->has_filename = false;
    return PHY_OK;
}

phy_status phy_workspace_save(phy_workspace *workspace, const char *name)
{
    if (workspace == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const char *destination = name;
    if (destination == NULL) {
        if (!workspace->has_filename) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
        destination = workspace->filename;
    }
    if (!phy_storage_name_valid(destination)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    size_t size = 0u;
    phy_status status =
        phy_notebook_serialize(workspace->notebook, NULL, 0u, &size);
    if (status != PHY_ERR_INVALID_ARGUMENT || size == 0u) {
        return status;
    }
    uint8_t *buffer = phy_alloc(size);
    if (buffer == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    status =
        phy_notebook_serialize(workspace->notebook, buffer, size, &size);
    if (status == PHY_OK) {
        status =
            phy_storage_write_notebook_atomic(destination, buffer, size);
    }
    phy_free(buffer, size);
    if (status != PHY_OK) {
        return status;
    }
    status = copy_filename(workspace, destination);
    if (status == PHY_OK) {
        phy_notebook_mark_clean(workspace->notebook);
    }
    return status;
}

phy_status phy_workspace_open(phy_workspace *workspace, const char *name)
{
    if (workspace == NULL || !phy_storage_name_valid(name)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    size_t size = 0u;
    phy_status status =
        phy_storage_read_notebook(name, NULL, 0u, &size);
    if (status != PHY_ERR_INVALID_ARGUMENT || size == 0u) {
        return status;
    }
    uint8_t *buffer = phy_alloc(size);
    if (buffer == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    status = phy_storage_read_notebook(name, buffer, size, &size);
    phy_notebook *replacement = NULL;
    if (status == PHY_OK) {
        status = phy_notebook_deserialize(buffer, size, &replacement);
    }
    phy_free(buffer, size);
    if (status != PHY_OK) {
        return status;
    }
    status = copy_filename(workspace, name);
    if (status != PHY_OK) {
        phy_notebook_destroy(replacement);
        return status;
    }
    phy_notebook_destroy(workspace->notebook);
    workspace->notebook = replacement;
    return PHY_OK;
}

phy_status phy_workspace_catalog(const phy_workspace *workspace,
                                 phy_storage_catalog *out_catalog)
{
    if (workspace == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_storage_list_notebooks(out_catalog);
}

phy_status phy_workspace_suggest_name(
    const phy_workspace *workspace,
    char out_name[PHY_STORAGE_NAME_CAPACITY])
{
    if (workspace == NULL || out_name == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_storage_catalog catalog;
    const phy_status status =
        phy_workspace_catalog(workspace, &catalog);
    if (status != PHY_OK) {
        return status;
    }
    return phy_storage_suggest_name(&catalog, out_name);
}
