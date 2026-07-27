#include <stdio.h>

#include "phy/geom.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_chart *chart;
    phy_manifold *manifold;
} fixture;

static phy_ir_ref read_expr(phy_ir_context *ir, const char *text)
{
    phy_ir_ref expression = PHY_IR_NULL;
    size_t offset = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_read(ir, text, &expression, &offset), PHY_OK);
    return expression;
}

static fixture open_fixture(phy_orientation orientation)
{
    fixture f = {0};
    f.ir = phy_ir_context_create(NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    static const char *const coordinates[2] = {"x", "y"};
    PHY_CHECK_EQ_INT(
        phy_chart_create(f.ir, coordinates, 2u, &f.chart), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_manifold_create(f.ir, "M", 2u, orientation, NULL,
                            &f.manifold),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_manifold_add_chart(f.manifold, f.chart, NULL), PHY_OK);
    return f;
}

static void close_fixture(fixture *f)
{
    PHY_CHECK_EQ_INT(phy_cas_validate(f->cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(f->ir), PHY_OK);
    phy_manifold_destroy(f->manifold);
    phy_chart_destroy(f->chart);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static phy_tensor *make_metric(fixture *f,
                               const char *const entries[4])
{
    static const phy_ir_variance lower[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_tensor *metric = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_create(f->chart, "g", 2u, lower, &metric), PHY_OK);
    for (unsigned row = 0u; row < 2u; ++row) {
        for (unsigned column = 0u; column < 2u; ++column) {
            const unsigned indices[2] = {row, column};
            PHY_CHECK_EQ_INT(
                phy_tensor_set(
                    metric, indices,
                    read_expr(f->ir, entries[row * 2u + column])),
                PHY_OK);
        }
    }
    return metric;
}

static phy_ir_ref component(const phy_form *form, size_t position)
{
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_form_get_at(form, position, &value), PHY_OK);
    return value;
}

static void expect_equivalent(fixture *f, phy_ir_ref actual,
                              const char *expected)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f->cas, actual, read_expr(f->ir, expected), &decision),
        PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static phy_form *basis_one_form(fixture *f, unsigned axis)
{
    phy_form *form = NULL;
    PHY_CHECK_EQ_INT(
        phy_form_create(f->manifold, 0u, NULL, 1u, &form), PHY_OK);
    const unsigned index[1] = {axis};
    PHY_CHECK_EQ_INT(
        phy_form_set(
            f->cas, form, index, read_expr(f->ir, "1")),
        PHY_OK);
    return form;
}

static void test_diagonal_coordinate_metric(void)
{
    fixture f = open_fixture(PHY_ORIENTATION_POSITIVE);
    static const char *const entries[4] = {"4", "0", "0", "9"};
    phy_tensor *metric = make_metric(&f, entries);

    phy_form *dx = basis_one_form(&f, 0u);
    phy_form *star_dx = NULL;
    PHY_CHECK_EQ_INT(
        phy_form_hodge_metric(f.cas, dx, metric, &star_dx), PHY_OK);
    expect_equivalent(&f, component(star_dx, 0u), "0");
    expect_equivalent(
        &f, component(star_dx, 1u),
        "(* (rat 1 4) (^ 36 (rat 1 2)))");

    phy_form *dy = basis_one_form(&f, 1u);
    phy_form *star_dy = NULL;
    PHY_CHECK_EQ_INT(
        phy_form_hodge_metric(f.cas, dy, metric, &star_dy), PHY_OK);
    expect_equivalent(
        &f, component(star_dy, 0u),
        "(* (rat -1 9) (^ 36 (rat 1 2)))");
    expect_equivalent(&f, component(star_dy, 1u), "0");

    phy_form *volume = NULL;
    PHY_CHECK_EQ_INT(
        phy_form_volume_metric(
            f.cas, f.manifold, 0u, metric, &volume),
        PHY_OK);
    expect_equivalent(
        &f, component(volume, 0u), "(^ 36 (rat 1 2))");

    phy_form_destroy(volume);
    phy_form_destroy(star_dy);
    phy_form_destroy(dy);
    phy_form_destroy(star_dx);
    phy_form_destroy(dx);
    phy_tensor_destroy(metric);
    close_fixture(&f);
}

static void test_nondiagonal_coordinate_metric(void)
{
    fixture f = open_fixture(PHY_ORIENTATION_POSITIVE);
    static const char *const entries[4] = {"2", "1", "1", "2"};
    phy_tensor *metric = make_metric(&f, entries);
    phy_form *dx = basis_one_form(&f, 0u);

    phy_form *dual = NULL;
    PHY_CHECK_EQ_INT(
        phy_form_hodge_metric(f.cas, dx, metric, &dual), PHY_OK);
    expect_equivalent(
        &f, component(dual, 0u),
        "(* (rat 1 3) (^ 3 (rat 1 2)))");
    expect_equivalent(
        &f, component(dual, 1u),
        "(* (rat 2 3) (^ 3 (rat 1 2)))");

    phy_form_destroy(dual);
    phy_form_destroy(dx);
    phy_tensor_destroy(metric);
    close_fixture(&f);
}

static void test_metric_hodge_rejections_are_typed(void)
{
    fixture f = open_fixture(PHY_ORIENTATION_POSITIVE);
    static const char *const singular_entries[4] = {"1", "0", "0", "0"};
    phy_tensor *singular = make_metric(&f, singular_entries);
    phy_form *dx = basis_one_form(&f, 0u);
    phy_form *sentinel = NULL;
    PHY_CHECK_EQ_INT(
        phy_form_hodge_metric(f.cas, dx, singular, &sentinel),
        PHY_ERR_DOMAIN);
    PHY_CHECK(sentinel == NULL);

    phy_tensor_destroy(singular);
    phy_form_destroy(dx);
    close_fixture(&f);

    f = open_fixture(PHY_ORIENTATION_NONE);
    static const char *const identity_entries[4] = {"1", "0", "0", "1"};
    phy_tensor *identity = make_metric(&f, identity_entries);
    dx = basis_one_form(&f, 0u);
    PHY_CHECK_EQ_INT(
        phy_form_hodge_metric(f.cas, dx, identity, &sentinel),
        PHY_ERR_ASSUMPTION);
    PHY_CHECK(sentinel == NULL);
    phy_tensor_destroy(identity);
    phy_form_destroy(dx);
    close_fixture(&f);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }
    PHY_TEST_CASE(test_diagonal_coordinate_metric);
    PHY_TEST_CASE(test_nondiagonal_coordinate_metric);
    PHY_TEST_CASE(test_metric_hodge_rejections_are_typed);
    const int result = PHY_TEST_REPORT("test_geom_metric");
    phy_platform_shutdown();
    return result;
}
