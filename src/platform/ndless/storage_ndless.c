#include "phy/storage.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "storage_internal.h"

#define STORAGE_PATH_CAPACITY 128u

static bool make_path(char path[STORAGE_PATH_CAPACITY], const char *name)
{
    const size_t directory = strlen(PHY_STORAGE_NOTEBOOK_DIRECTORY);
    const size_t leaf = strlen(name);
    if (directory + 1u + leaf + 1u > STORAGE_PATH_CAPACITY) {
        return false;
    }
    memcpy(path, PHY_STORAGE_NOTEBOOK_DIRECTORY, directory);
    path[directory] = '/';
    memcpy(path + directory + 1u, name, leaf + 1u);
    return true;
}

static bool directory_exists(const char *path)
{
    DIR *directory = opendir(path);
    if (directory == NULL) {
        return false;
    }
    (void)closedir(directory);
    return true;
}

phy_status phy_storage_ensure_notebook_directory(void)
{
    static const char parent[] = "/documents/phy-nspire";
    if (!directory_exists(parent)) {
        (void)mkdir(parent, 0777);
    }
    if (!directory_exists(PHY_STORAGE_NOTEBOOK_DIRECTORY)) {
        (void)mkdir(PHY_STORAGE_NOTEBOOK_DIRECTORY, 0777);
    }
    return directory_exists(PHY_STORAGE_NOTEBOOK_DIRECTORY)
               ? PHY_OK
               : PHY_ERR_BACKEND;
}

phy_status phy_storage_list_notebooks(phy_storage_catalog *out_catalog)
{
    if (out_catalog == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(out_catalog, 0, sizeof *out_catalog);
    phy_status status = phy_storage_ensure_notebook_directory();
    if (status != PHY_OK) {
        return status;
    }
    DIR *directory = opendir(PHY_STORAGE_NOTEBOOK_DIRECTORY);
    if (directory == NULL) {
        return PHY_ERR_BACKEND;
    }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (phy_storage_name_valid(entry->d_name)) {
            phy_storage_catalog_insert(out_catalog, entry->d_name);
        }
    }
    (void)closedir(directory);
    return PHY_OK;
}

static phy_status open_notebook(const char *name, const char *mode,
                                FILE **out_file)
{
    if (!phy_storage_name_valid(name)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    char path[STORAGE_PATH_CAPACITY];
    if (!make_path(path, name)) {
        return PHY_ERR_TERM_LIMIT;
    }
    *out_file = fopen(path, mode);
    return *out_file != NULL ? PHY_OK : PHY_ERR_BACKEND;
}

phy_status phy_storage_read_notebook(const char *name, uint8_t *buffer,
                                     size_t capacity, size_t *out_size)
{
    if (out_size == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    FILE *file = NULL;
    phy_status status = open_notebook(name, "rb", &file);
    if (status != PHY_OK) {
        return status;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return PHY_ERR_BACKEND;
    }
    const long end = ftell(file);
    if (end < 0 || (unsigned long)end > PHY_NOTEBOOK_DOCUMENT_MAX_BYTES) {
        (void)fclose(file);
        return end < 0 ? PHY_ERR_BACKEND : PHY_ERR_TERM_LIMIT;
    }
    *out_size = (size_t)end;
    if (buffer == NULL || capacity < *out_size) {
        (void)fclose(file);
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (fseek(file, 0, SEEK_SET) != 0 ||
        fread(buffer, 1u, *out_size, file) != *out_size ||
        fclose(file) != 0) {
        return PHY_ERR_BACKEND;
    }
    return PHY_OK;
}

static phy_status write_temporary(const char *path, const uint8_t *buffer,
                                  size_t size)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return PHY_ERR_BACKEND;
    }
    bool ok = fwrite(buffer, 1u, size, file) == size;
    if (ok) {
        ok = fflush(file) == 0;
    }
    /* fclose must run even when the write/flush failed. */
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        (void)remove(path);
        return PHY_ERR_BACKEND;
    }
    return PHY_OK;
}

static bool regular_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    (void)fclose(file);
    return true;
}

phy_status phy_storage_write_notebook_atomic(const char *name,
                                             const uint8_t *buffer,
                                             size_t size)
{
    static const char temporary[] =
        "/documents/phy-nspire/notebooks/.phy-save.tmp";
    static const char backup[] =
        "/documents/phy-nspire/notebooks/.phy-save.bak";
    if (!phy_storage_name_valid(name) || buffer == NULL || size == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (size > PHY_NOTEBOOK_DOCUMENT_MAX_BYTES) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_status status = phy_storage_ensure_notebook_directory();
    char destination[STORAGE_PATH_CAPACITY];
    if (status != PHY_OK) {
        return status;
    }
    if (!make_path(destination, name)) {
        return PHY_ERR_TERM_LIMIT;
    }

    (void)remove(temporary);
    status = write_temporary(temporary, buffer, size);
    if (status != PHY_OK) {
        return status;
    }

    const bool had_previous = regular_file_exists(destination);
    (void)remove(backup);
    if (had_previous && rename(destination, backup) != 0) {
        (void)remove(temporary);
        return PHY_ERR_BACKEND;
    }
    if (rename(temporary, destination) != 0) {
        if (had_previous) {
            (void)rename(backup, destination);
        }
        (void)remove(temporary);
        return PHY_ERR_BACKEND;
    }
    if (had_previous) {
        (void)remove(backup);
    }
    return PHY_OK;
}
