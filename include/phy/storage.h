/*
 * Bounded notebook storage contract.
 *
 * UI/model code never constructs arbitrary filesystem paths. Backends expose
 * one fixed notebook directory and accept validated leaf names only.
 */
#ifndef PHY_STORAGE_H
#define PHY_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phy/notebook.h"
#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHY_STORAGE_NOTEBOOK_DIRECTORY "/documents/phy-nspire/notebooks"
#define PHY_STORAGE_NAME_CAPACITY 48u
#define PHY_STORAGE_MAX_FILES 16u

typedef struct {
    char name[PHY_STORAGE_NAME_CAPACITY];
} phy_storage_entry;

typedef struct {
    phy_storage_entry entries[PHY_STORAGE_MAX_FILES];
    size_t count;
    bool truncated;
} phy_storage_catalog;

bool phy_storage_name_valid(const char *name);
phy_status phy_storage_suggest_name(const phy_storage_catalog *catalog,
                                    char out_name[PHY_STORAGE_NAME_CAPACITY]);

phy_status phy_storage_ensure_notebook_directory(void);
phy_status phy_storage_list_notebooks(phy_storage_catalog *out_catalog);

/*
 * Read uses sizing conventions like the document codec. *out_size receives
 * the exact file size; a NULL/short buffer returns PHY_ERR_INVALID_ARGUMENT.
 */
phy_status phy_storage_read_notebook(const char *name, uint8_t *buffer,
                                     size_t capacity, size_t *out_size);

/* Atomic replacement: failure must preserve the previous destination. */
phy_status phy_storage_write_notebook_atomic(const char *name,
                                             const uint8_t *buffer,
                                             size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PHY_STORAGE_H */
