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
    /* fclose performs the required stream flush on the Ndless libc. */
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        (void)remove(path);
        return PHY_ERR_BACKEND;
    }
    return PHY_OK;
}

static bool file_matches_buffer(const char *path, const uint8_t *buffer,
                                size_t size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    uint8_t chunk[256];
    size_t offset = 0u;
    bool ok = true;
    while (offset < size) {
        size_t count = size - offset;
        if (count > sizeof chunk) {
            count = sizeof chunk;
        }
        if (fread(chunk, 1u, count, file) != count ||
            memcmp(chunk, buffer + offset, count) != 0) {
            ok = false;
            break;
        }
        offset += count;
    }
    if (ok && fgetc(file) != EOF) {
        ok = false;
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    return ok;
}

static phy_status write_verified(const char *path, const uint8_t *buffer,
                                 size_t size)
{
    const phy_status status = write_temporary(path, buffer, size);
    if (status != PHY_OK) {
        return status;
    }
    if (!file_matches_buffer(path, buffer, size)) {
        (void)remove(path);
        return PHY_ERR_BACKEND;
    }
    return PHY_OK;
}

static bool files_equal(const char *left_path, const char *right_path)
{
    FILE *left = fopen(left_path, "rb");
    FILE *right = fopen(right_path, "rb");
    if (left == NULL || right == NULL) {
        if (left != NULL) {
            (void)fclose(left);
        }
        if (right != NULL) {
            (void)fclose(right);
        }
        return false;
    }
    uint8_t left_buffer[256];
    uint8_t right_buffer[256];
    bool equal = true;
    for (;;) {
        const size_t left_count =
            fread(left_buffer, 1u, sizeof left_buffer, left);
        const size_t right_count =
            fread(right_buffer, 1u, sizeof right_buffer, right);
        if (left_count != right_count ||
            memcmp(left_buffer, right_buffer, left_count) != 0) {
            equal = false;
            break;
        }
        if (left_count < sizeof left_buffer) {
            if (ferror(left) != 0 || ferror(right) != 0) {
                equal = false;
            }
            break;
        }
    }
    const int left_close = fclose(left);
    const int right_close = fclose(right);
    if (left_close != 0 || right_close != 0) {
        equal = false;
    }
    return equal;
}

static bool copy_file_verified(const char *source, const char *destination)
{
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(destination, "wb");
    if (input == NULL || output == NULL) {
        if (input != NULL) {
            (void)fclose(input);
        }
        if (output != NULL) {
            (void)fclose(output);
        }
        (void)remove(destination);
        return false;
    }
    uint8_t buffer[256];
    bool ok = true;
    for (;;) {
        const size_t count = fread(buffer, 1u, sizeof buffer, input);
        if (count > 0u && fwrite(buffer, 1u, count, output) != count) {
            ok = false;
            break;
        }
        if (count < sizeof buffer) {
            if (ferror(input) != 0) {
                ok = false;
            }
            break;
        }
    }
    const int input_close = fclose(input);
    const int output_close = fclose(output);
    if (input_close != 0 || output_close != 0) {
        ok = false;
    }
    if (ok) {
        ok = files_equal(source, destination);
    }
    if (!ok) {
        (void)remove(destination);
    }
    return ok;
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
        "/documents/phy-nspire/notebooks/" PHY_STORAGE_TEMPORARY_NAME;
    static const char backup[] =
        "/documents/phy-nspire/notebooks/" PHY_STORAGE_BACKUP_NAME;
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

    const bool had_previous = regular_file_exists(destination);
    if (!had_previous) {
        /*
         * First save is the common path. Write the final .tns leaf directly:
         * TI's document store need not create or rename a hidden extensionless
         * file, and readback catches a short/failed close.
         */
        status = write_verified(destination, buffer, size);
        if (status != PHY_OK) {
            (void)remove(destination);
        }
        return status;
    }

    (void)remove(temporary);
    (void)remove(backup);
    status = write_verified(temporary, buffer, size);
    if (status != PHY_OK) {
        return status;
    }

    bool backup_ready = rename(destination, backup) == 0;
    if (!backup_ready) {
        backup_ready = copy_file_verified(destination, backup);
    }
    if (!backup_ready) {
        (void)remove(temporary);
        return PHY_ERR_BACKEND;
    }

    bool installed = rename(temporary, destination) == 0 &&
                     file_matches_buffer(destination, buffer, size);
    if (!installed) {
        installed = write_verified(destination, buffer, size) == PHY_OK;
    }
    if (installed) {
        (void)remove(temporary);
        (void)remove(backup);
        return PHY_OK;
    }

    (void)remove(destination);
    bool restored = rename(backup, destination) == 0;
    if (!restored) {
        restored = copy_file_verified(backup, destination);
        if (restored) {
            (void)remove(backup);
        }
    }
    (void)remove(temporary);
    return PHY_ERR_BACKEND;
}
