#ifndef PHY_STORAGE_INTERNAL_H
#define PHY_STORAGE_INTERNAL_H

#include "phy/storage.h"

#define PHY_STORAGE_TEMPORARY_NAME "phy-save-new.tns"
#define PHY_STORAGE_BACKUP_NAME "phy-save-backup.tns"

int phy_storage_name_compare(const char *left, const char *right);
void phy_storage_catalog_insert(phy_storage_catalog *catalog,
                                const char *name);

#endif /* PHY_STORAGE_INTERNAL_H */
