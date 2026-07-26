#ifndef PHY_STORAGE_INTERNAL_H
#define PHY_STORAGE_INTERNAL_H

#include "phy/storage.h"

int phy_storage_name_compare(const char *left, const char *right);
void phy_storage_catalog_insert(phy_storage_catalog *catalog,
                                const char *name);

#endif /* PHY_STORAGE_INTERNAL_H */
