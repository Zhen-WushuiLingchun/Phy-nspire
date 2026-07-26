#include "phy/storage.h"

#include <string.h>

#include "phy/platform_host.h"
#include "storage_internal.h"

typedef struct {
    bool used;
    char name[PHY_STORAGE_NAME_CAPACITY];
    size_t size;
    uint8_t data[PHY_NOTEBOOK_DOCUMENT_MAX_BYTES];
} host_file;

static host_file g_files[PHY_STORAGE_MAX_FILES];
static uint8_t g_staged[PHY_NOTEBOOK_DOCUMENT_MAX_BYTES];
static bool g_fail_next_write;

static host_file *find_file(const char *name)
{
    for (size_t i = 0u; i < PHY_STORAGE_MAX_FILES; ++i) {
        if (g_files[i].used &&
            phy_storage_name_compare(g_files[i].name, name) == 0) {
            return &g_files[i];
        }
    }
    return NULL;
}

static host_file *find_free_file(void)
{
    for (size_t i = 0u; i < PHY_STORAGE_MAX_FILES; ++i) {
        if (!g_files[i].used) {
            return &g_files[i];
        }
    }
    return NULL;
}

phy_status phy_storage_ensure_notebook_directory(void)
{
    return PHY_OK;
}

phy_status phy_storage_list_notebooks(phy_storage_catalog *out_catalog)
{
    if (out_catalog == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(out_catalog, 0, sizeof *out_catalog);
    for (size_t i = 0u; i < PHY_STORAGE_MAX_FILES; ++i) {
        if (g_files[i].used && phy_storage_name_valid(g_files[i].name)) {
            phy_storage_catalog_insert(out_catalog, g_files[i].name);
        }
    }
    return PHY_OK;
}

phy_status phy_storage_read_notebook(const char *name, uint8_t *buffer,
                                     size_t capacity, size_t *out_size)
{
    if (!phy_storage_name_valid(name) || out_size == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const host_file *file = find_file(name);
    if (file == NULL) {
        return PHY_ERR_BACKEND;
    }
    *out_size = file->size;
    if (buffer == NULL || capacity < file->size) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memcpy(buffer, file->data, file->size);
    return PHY_OK;
}

phy_status phy_storage_write_notebook_atomic(const char *name,
                                             const uint8_t *buffer,
                                             size_t size)
{
    if (!phy_storage_name_valid(name) || buffer == NULL || size == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (size > PHY_NOTEBOOK_DOCUMENT_MAX_BYTES) {
        return PHY_ERR_TERM_LIMIT;
    }
    if (g_fail_next_write) {
        g_fail_next_write = false;
        return PHY_ERR_BACKEND;
    }
    host_file *file = find_file(name);
    if (file == NULL) {
        file = find_free_file();
    }
    if (file == NULL) {
        return PHY_ERR_TERM_LIMIT;
    }

    memcpy(g_staged, buffer, size);
    memcpy(file->data, g_staged, size);
    const size_t name_length = strlen(name);
    memcpy(file->name, name, name_length + 1u);
    file->size = size;
    file->used = true;
    return PHY_OK;
}

void phy_host_storage_clear(void)
{
    memset(g_files, 0, sizeof g_files);
    g_fail_next_write = false;
}

bool phy_host_storage_put(const char *name, const uint8_t *buffer, size_t size)
{
    return phy_storage_write_notebook_atomic(name, buffer, size) == PHY_OK;
}

void phy_host_storage_fail_next_write(void)
{
    g_fail_next_write = true;
}
