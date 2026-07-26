#include <stdint.h>
#include <string.h>

#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy/storage.h"
#include "phy_test.h"

static uint8_t g_large[PHY_NOTEBOOK_DOCUMENT_MAX_BYTES + 1u];

static void numbered_name(unsigned number,
                          char name[PHY_STORAGE_NAME_CAPACITY])
{
    memcpy(name, "File-00.tns", sizeof "File-00.tns");
    name[5] = (char)('0' + (number / 10u) % 10u);
    name[6] = (char)('0' + number % 10u);
}

static void test_name_validation_and_suggestion(void)
{
    PHY_CHECK(phy_storage_name_valid("Notebook-001.tns"));
    PHY_CHECK(phy_storage_name_valid("GR notes.TNS"));
    PHY_CHECK(!phy_storage_name_valid(NULL));
    PHY_CHECK(!phy_storage_name_valid(""));
    PHY_CHECK(!phy_storage_name_valid(".hidden.tns"));
    PHY_CHECK(!phy_storage_name_valid("../escape.tns"));
    PHY_CHECK(!phy_storage_name_valid("bad/name.tns"));
    PHY_CHECK(!phy_storage_name_valid("bad\\name.tns"));
    PHY_CHECK(!phy_storage_name_valid("bad:name.tns"));
    PHY_CHECK(!phy_storage_name_valid("notebook.md"));

    phy_storage_catalog catalog = {0};
    memcpy(catalog.entries[0].name, "Notebook-001.tns",
           sizeof "Notebook-001.tns");
    memcpy(catalog.entries[1].name, "notebook-002.TNS",
           sizeof "notebook-002.TNS");
    catalog.count = 2u;
    char suggestion[PHY_STORAGE_NAME_CAPACITY];
    PHY_CHECK_EQ_INT(phy_storage_suggest_name(&catalog, suggestion), PHY_OK);
    PHY_CHECK_EQ_STR(suggestion, "Notebook-003.tns");
    PHY_CHECK_EQ_INT(phy_storage_suggest_name(NULL, suggestion),
                     PHY_ERR_INVALID_ARGUMENT);
}

static void test_write_list_read_replace_and_atomic_failure(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_host_storage_clear();
    static const uint8_t alpha[] = {1u, 2u, 3u};
    static const uint8_t beta[] = {9u, 8u};
    PHY_CHECK_EQ_INT(phy_storage_ensure_notebook_directory(), PHY_OK);
    PHY_CHECK_EQ_INT(phy_storage_write_notebook_atomic(
                         "zeta.tns", alpha, sizeof alpha),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_storage_write_notebook_atomic(
                         "Alpha.tns", beta, sizeof beta),
                     PHY_OK);

    phy_storage_catalog catalog;
    PHY_CHECK_EQ_INT(phy_storage_list_notebooks(&catalog), PHY_OK);
    PHY_CHECK_EQ_INT(catalog.count, 2);
    PHY_CHECK(!catalog.truncated);
    PHY_CHECK_EQ_STR(catalog.entries[0].name, "Alpha.tns");
    PHY_CHECK_EQ_STR(catalog.entries[1].name, "zeta.tns");

    size_t required = 0u;
    PHY_CHECK_EQ_INT(phy_storage_read_notebook(
                         "zeta.tns", NULL, 0u, &required),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(required, sizeof alpha);
    uint8_t readback[8] = {0};
    PHY_CHECK_EQ_INT(phy_storage_read_notebook(
                         "zeta.tns", readback, sizeof readback, &required),
                     PHY_OK);
    PHY_CHECK(memcmp(readback, alpha, sizeof alpha) == 0);

    static const uint8_t replacement[] = {4u, 5u, 6u, 7u};
    phy_host_storage_fail_next_write();
    PHY_CHECK_EQ_INT(phy_storage_write_notebook_atomic(
                         "zeta.tns", replacement, sizeof replacement),
                     PHY_ERR_BACKEND);
    PHY_CHECK_EQ_INT(phy_storage_read_notebook(
                         "zeta.tns", readback, sizeof readback, &required),
                     PHY_OK);
    PHY_CHECK_EQ_INT(required, sizeof alpha);
    PHY_CHECK(memcmp(readback, alpha, sizeof alpha) == 0);

    PHY_CHECK_EQ_INT(phy_storage_write_notebook_atomic(
                         "zeta.tns", replacement, sizeof replacement),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_storage_read_notebook(
                         "zeta.tns", readback, sizeof readback, &required),
                     PHY_OK);
    PHY_CHECK_EQ_INT(required, sizeof replacement);
    PHY_CHECK(memcmp(readback, replacement, sizeof replacement) == 0);

    phy_platform_shutdown();
}

static void test_capacity_and_bounds(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_host_storage_clear();
    static const uint8_t byte = 42u;
    for (unsigned i = 0u; i < PHY_STORAGE_MAX_FILES; ++i) {
        char name[PHY_STORAGE_NAME_CAPACITY];
        numbered_name(i, name);
        PHY_CHECK_EQ_INT(
            phy_storage_write_notebook_atomic(name, &byte, 1u), PHY_OK);
    }
    PHY_CHECK_EQ_INT(phy_storage_write_notebook_atomic(
                         "overflow.tns", &byte, 1u),
                     PHY_ERR_TERM_LIMIT);

    PHY_CHECK_EQ_INT(
        phy_storage_write_notebook_atomic("../bad.tns", &byte, 1u),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_storage_write_notebook_atomic("valid.tns", NULL, 1u),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_storage_write_notebook_atomic(
                         "valid.tns", g_large, sizeof g_large),
                     PHY_ERR_TERM_LIMIT);
    size_t size = 0u;
    PHY_CHECK_EQ_INT(phy_storage_read_notebook(
                         "missing.tns", g_large, sizeof g_large, &size),
                     PHY_ERR_BACKEND);
    PHY_CHECK_EQ_INT(phy_storage_list_notebooks(NULL),
                     PHY_ERR_INVALID_ARGUMENT);

    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_name_validation_and_suggestion);
    PHY_TEST_CASE(test_write_list_read_replace_and_atomic_failure);
    PHY_TEST_CASE(test_capacity_and_bounds);
    return PHY_TEST_REPORT("test_storage");
}
