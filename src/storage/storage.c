#include "phy/storage.h"

#include <string.h>

#include "storage_internal.h"

static char ascii_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static bool ends_with_tns(const char *name, size_t length)
{
    return length >= 4u && name[length - 4u] == '.' &&
           ascii_lower(name[length - 3u]) == 't' &&
           ascii_lower(name[length - 2u]) == 'n' &&
           ascii_lower(name[length - 1u]) == 's';
}

static bool name_equal_folded(const char *left, const char *right);

bool phy_storage_name_valid(const char *name)
{
    if (name == NULL) {
        return false;
    }
    const size_t length = strlen(name);
    if (length < 5u || length >= PHY_STORAGE_NAME_CAPACITY ||
        name[0] == '.' || name[0] == ' ' || name[length - 1u] == ' ' ||
        !ends_with_tns(name, length)) {
        return false;
    }
    if (name_equal_folded(name, PHY_STORAGE_TEMPORARY_NAME) ||
        name_equal_folded(name, PHY_STORAGE_BACKUP_NAME)) {
        return false;
    }
    for (size_t i = 0u; i < length; ++i) {
        const char c = name[i];
        const bool alpha = (c >= 'A' && c <= 'Z') ||
                           (c >= 'a' && c <= 'z');
        const bool digit = c >= '0' && c <= '9';
        if (!alpha && !digit && c != ' ' && c != '-' && c != '_' &&
            c != '.') {
            return false;
        }
        if (c == '.' && i + 1u < length && name[i + 1u] == '.') {
            return false;
        }
    }
    return true;
}

static bool name_equal_folded(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (ascii_lower(*left) != ascii_lower(*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

int phy_storage_name_compare(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        const unsigned char a = (unsigned char)ascii_lower(*left);
        const unsigned char b = (unsigned char)ascii_lower(*right);
        if (a != b) {
            return a < b ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == '\0' ? -1 : 1;
}

void phy_storage_catalog_insert(phy_storage_catalog *catalog,
                                const char *name)
{
    if (catalog->count >= PHY_STORAGE_MAX_FILES) {
        catalog->truncated = true;
        return;
    }
    size_t position = catalog->count;
    while (position > 0u &&
           phy_storage_name_compare(
               name, catalog->entries[position - 1u].name) < 0) {
        catalog->entries[position] = catalog->entries[position - 1u];
        --position;
    }
    const size_t length = strlen(name);
    memcpy(catalog->entries[position].name, name, length + 1u);
    catalog->count++;
}

static bool catalog_contains(const phy_storage_catalog *catalog,
                             const char *name)
{
    for (size_t i = 0u; i < catalog->count; ++i) {
        if (name_equal_folded(catalog->entries[i].name, name)) {
            return true;
        }
    }
    return false;
}

static void format_suggestion(unsigned number,
                              char out_name[PHY_STORAGE_NAME_CAPACITY])
{
    static const char prefix[] = "Notebook-";
    static const char suffix[] = ".tns";
    memcpy(out_name, prefix, sizeof prefix - 1u);
    size_t at = sizeof prefix - 1u;
    out_name[at++] = (char)('0' + (number / 100u) % 10u);
    out_name[at++] = (char)('0' + (number / 10u) % 10u);
    out_name[at++] = (char)('0' + number % 10u);
    memcpy(out_name + at, suffix, sizeof suffix);
}

phy_status phy_storage_suggest_name(const phy_storage_catalog *catalog,
                                    char out_name[PHY_STORAGE_NAME_CAPACITY])
{
    if (catalog == NULL || out_name == NULL ||
        catalog->count > PHY_STORAGE_MAX_FILES) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (unsigned number = 1u; number <= 999u; ++number) {
        format_suggestion(number, out_name);
        if (!catalog_contains(catalog, out_name)) {
            return PHY_OK;
        }
    }
    out_name[0] = '\0';
    return PHY_ERR_TERM_LIMIT;
}
