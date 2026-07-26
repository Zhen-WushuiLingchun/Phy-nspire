#include <string.h>

#include "phy/notebook.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy/storage.h"
#include "phy/workspace.h"
#include "phy_test.h"

static void test_blank_new_and_named_save(void)
{
    phy_host_storage_clear();
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_workspace *workspace = phy_workspace_create();
    PHY_CHECK(workspace != NULL);
    PHY_CHECK_EQ_INT(
        phy_notebook_cell_count(phy_workspace_notebook(workspace)), 0);
    PHY_CHECK(!phy_notebook_is_dirty(phy_workspace_notebook(workspace)));
    PHY_CHECK(!phy_workspace_has_filename(workspace));

    size_t input = 0u;
    PHY_CHECK_EQ_INT(phy_notebook_add_input(
                         phy_workspace_notebook(workspace), "x+1", &input),
                     PHY_OK);
    PHY_CHECK(phy_notebook_is_dirty(phy_workspace_notebook(workspace)));
    PHY_CHECK_EQ_INT(phy_workspace_save(workspace, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_workspace_save(workspace, "Notebook-001.tns"),
                     PHY_OK);
    PHY_CHECK(phy_workspace_has_filename(workspace));
    PHY_CHECK(strcmp(phy_workspace_filename(workspace),
                     "Notebook-001.tns") == 0);
    PHY_CHECK(!phy_notebook_is_dirty(phy_workspace_notebook(workspace)));

    PHY_CHECK_EQ_INT(phy_workspace_new(workspace), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_notebook_cell_count(phy_workspace_notebook(workspace)), 0);
    PHY_CHECK(!phy_workspace_has_filename(workspace));

    phy_workspace_destroy(workspace);
    phy_platform_shutdown();
}

static void test_open_round_trip_and_failed_open_is_transactional(void)
{
    phy_host_storage_clear();
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_workspace *workspace = phy_workspace_create();
    PHY_CHECK(workspace != NULL);
    size_t index = 0u;
    PHY_CHECK_EQ_INT(phy_notebook_add_markdown(
                         phy_workspace_notebook(workspace), "GR",
                         "Metric $g_{\\mu\\nu}$", &index),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_workspace_save(workspace, "gravity.tns"), PHY_OK);
    PHY_CHECK_EQ_INT(phy_workspace_new(workspace), PHY_OK);
    PHY_CHECK_EQ_INT(phy_workspace_open(workspace, "gravity.tns"), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_notebook_cell_count(phy_workspace_notebook(workspace)), 1);
    phy_notebook_cell_view cell;
    PHY_CHECK(phy_notebook_cell(phy_workspace_notebook(workspace), 0u, &cell));
    PHY_CHECK(strcmp(cell.primary, "GR") == 0);
    PHY_CHECK(strcmp(cell.secondary, "Metric $g_{\\mu\\nu}$") == 0);

    static const uint8_t corrupt[] = {'n', 'o', 't', 'e', 'b', 'o', 'o', 'k'};
    PHY_CHECK(phy_host_storage_put("broken.tns", corrupt, sizeof corrupt));
    PHY_CHECK_EQ_INT(phy_workspace_open(workspace, "broken.tns"),
                     PHY_ERR_CORRUPT_DOCUMENT);
    PHY_CHECK(strcmp(phy_workspace_filename(workspace), "gravity.tns") == 0);
    PHY_CHECK_EQ_INT(
        phy_notebook_cell_count(phy_workspace_notebook(workspace)), 1);

    phy_workspace_destroy(workspace);
    phy_platform_shutdown();
}

static void test_save_failure_preserves_name_and_dirty_state(void)
{
    phy_host_storage_clear();
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_workspace *workspace = phy_workspace_create();
    PHY_CHECK(workspace != NULL);
    size_t index = 0u;
    PHY_CHECK_EQ_INT(phy_notebook_add_input(
                         phy_workspace_notebook(workspace), "x", &index),
                     PHY_OK);
    phy_host_storage_fail_next_write();
    PHY_CHECK_EQ_INT(phy_workspace_save(workspace, "failed.tns"),
                     PHY_ERR_BACKEND);
    PHY_CHECK(!phy_workspace_has_filename(workspace));
    PHY_CHECK(phy_notebook_is_dirty(phy_workspace_notebook(workspace)));

    phy_workspace_destroy(workspace);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_blank_new_and_named_save);
    PHY_TEST_CASE(test_open_round_trip_and_failed_open_is_transactional);
    PHY_TEST_CASE(test_save_failure_preserves_name_and_dirty_state);
    return PHY_TEST_REPORT("test_workspace");
}
