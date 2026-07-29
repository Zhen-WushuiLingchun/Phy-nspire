#include "phy/abstract_tensor.h"
#include "phy/cas.h"
#include "phy/component_tensor.h"
#include "phy/ir.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_abstract_context *abstract;
} fixture;

static fixture fixture_open(void)
{
    fixture f = {0};
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(f.cas, NULL, &f.abstract), PHY_OK);
    return f;
}

static void fixture_close(fixture *f)
{
    phy_abstract_context_destroy(f->abstract);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
    phy_platform_shutdown();
}

static int64_t exact_integer(const fixture *f, phy_ir_ref ref)
{
    int64_t value = INT64_MIN;
    PHY_CHECK(phy_ir_integer_value(f->ir, ref, &value));
    return value;
}

static void test_basis_binds_known_and_symbolic_dimensions(void)
{
    fixture f = fixture_open();
    phy_index_space *known = NULL;
    phy_index_space *symbolic = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 6),
            PHY_METRIC_SYMMETRIC, &known),
        PHY_OK);
    const phy_ir_ref n =
        phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "n"));
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "N", n, PHY_METRIC_NONE, &symbolic),
        PHY_OK);

    static const char *const coordinates[6] = {
        "x0", "x1", "x2", "x3", "x4", "x5"};
    phy_component_basis *basis = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            known, "coordinate", 6u, coordinates, NULL, &basis),
        PHY_OK);
    PHY_CHECK_EQ_STR(phy_component_basis_name(basis), "coordinate");
    PHY_CHECK_EQ_INT(phy_component_basis_dimension(basis), 6);
    PHY_CHECK(phy_component_basis_space(basis) == known);
    PHY_CHECK(phy_component_basis_has_coordinates(basis));
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(
            f.ir, phy_component_basis_coordinate_symbol(basis, 4u)),
        "x4");
    PHY_CHECK(
        phy_component_basis_coordinate(basis, 2u) != PHY_IR_NULL);
    phy_component_basis_destroy(basis);

    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            known, "wrong", 5u, NULL, NULL, &basis),
        PHY_ERR_DOMAIN);
    PHY_CHECK(basis == NULL);
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            symbolic, "internal7", 7u, NULL, NULL, &basis),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_component_basis_dimension(basis), 7);
    PHY_CHECK(!phy_component_basis_has_coordinates(basis));
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_sparse_rank_nine_does_not_allocate_dense_power(void)
{
    fixture f = fixture_open();
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 6),
            PHY_METRIC_NONE, &space),
        PHY_OK);
    phy_component_basis *basis = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            space, "e", 6u, NULL, NULL, &basis),
        PHY_OK);
    const phy_index_space *slots[9] = {
        space, space, space, space, space, space, space, space, space};
    phy_abstract_tensor_head *head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "T9", slots, 9u, PHY_TENSOR_COMMUTING, &head),
        PHY_OK);
    phy_component_basis *bases[9] = {
        basis, basis, basis, basis, basis, basis, basis, basis, basis};
    phy_ir_variance valence[9] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_component_tensor *tensor = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_create(
            head, bases, valence, NULL, &tensor),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_component_tensor_rank(tensor), 9);
    PHY_CHECK_EQ_INT(phy_component_tensor_symmetry_order(tensor), 1);
    PHY_CHECK_EQ_INT(phy_component_tensor_entry_count(tensor), 0);

    const uint32_t index[9] = {5, 4, 3, 2, 1, 0, 5, 4, 3};
    PHY_CHECK_EQ_INT(
        phy_component_tensor_set(
            tensor, index, phy_ir_integer(f.ir, 17)),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_component_tensor_entry_count(tensor), 1);
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(tensor, index, &value), PHY_OK);
    PHY_CHECK_EQ_INT(exact_integer(&f, value), 17);
    PHY_CHECK(
        phy_component_tensor_bytes_used(tensor) < 512u * 1024u);

    const uint32_t missing[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(tensor, missing, &value), PHY_OK);
    PHY_CHECK_EQ_INT(exact_integer(&f, value), 0);
    phy_component_tensor_destroy(tensor);
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_signed_component_orbits_and_forced_zero(void)
{
    fixture f = fixture_open();
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 5),
            PHY_METRIC_NONE, &space),
        PHY_OK);
    phy_component_basis *basis = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            space, "e", 5u, NULL, NULL, &basis),
        PHY_OK);
    const phy_index_space *slots[2] = {space, space};
    phy_abstract_tensor_head *two_form = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "A", slots, 2u, PHY_TENSOR_COMMUTING, &two_form),
        PHY_OK);
    static const uint16_t swap[] = {1, 0};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(two_form, swap, -1), PHY_OK);
    phy_component_basis *bases[2] = {basis, basis};
    const phy_ir_variance lower[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_component_tensor *tensor = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_create(
            two_form, bases, lower, NULL, &tensor),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_component_tensor_symmetry_order(tensor), 2);

    const uint32_t three_one[2] = {3, 1};
    const uint32_t one_three[2] = {1, 3};
    PHY_CHECK_EQ_INT(
        phy_component_tensor_set(
            tensor, three_one, phy_ir_integer(f.ir, 5)),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_component_tensor_entry_count(tensor), 1);
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(tensor, one_three, &value), PHY_OK);
    PHY_CHECK_EQ_INT(exact_integer(&f, value), -5);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(tensor, three_one, &value), PHY_OK);
    PHY_CHECK_EQ_INT(exact_integer(&f, value), 5);

    const uint32_t repeated[2] = {2, 2};
    PHY_CHECK_EQ_INT(
        phy_component_tensor_set(
            tensor, repeated, phy_ir_integer(f.ir, 1)),
        PHY_ERR_ASSUMPTION);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(tensor, repeated, &value), PHY_OK);
    PHY_CHECK_EQ_INT(exact_integer(&f, value), 0);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_set(
            tensor, repeated, phy_ir_integer(f.ir, 0)),
        PHY_OK);
    phy_component_tensor_destroy(tensor);

    const phy_ir_variance mixed[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_UPPER};
    PHY_CHECK_EQ_INT(
        phy_component_tensor_create(
            two_form, bases, mixed, NULL, &tensor),
        PHY_ERR_TYPE);
    PHY_CHECK(tensor == NULL);
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_symmetric_component_canonicalization_and_limits(void)
{
    fixture f = fixture_open();
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_NONE, &space),
        PHY_OK);
    phy_component_basis *basis = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            space, "e", 4u, NULL, NULL, &basis),
        PHY_OK);
    const phy_index_space *slots[5] = {
        space, space, space, space, space};
    phy_abstract_tensor_head *symmetric = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "S5", slots, 5u, PHY_TENSOR_COMMUTING,
            &symmetric),
        PHY_OK);
    for (size_t adjacent = 0u; adjacent + 1u < 5u; ++adjacent) {
        uint16_t swap[5] = {0, 1, 2, 3, 4};
        swap[adjacent] = (uint16_t)(adjacent + 1u);
        swap[adjacent + 1u] = (uint16_t)adjacent;
        PHY_CHECK_EQ_INT(
            phy_tensor_head_add_symmetry(
                symmetric, swap, 1), PHY_OK);
    }
    phy_component_basis *bases[5] = {
        basis, basis, basis, basis, basis};
    const phy_ir_variance valence[5] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_component_limits limits = {0};
    limits.max_entries = 2u;
    phy_component_tensor *tensor = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_create(
            symmetric, bases, valence, &limits, &tensor),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_symmetry_order(tensor), 120);
    const uint32_t descending[5] = {3, 2, 1, 0, 3};
    uint32_t canonical[5] = {0};
    int sign = 0;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_canonical_indices(
            tensor, descending, canonical, &sign),
        PHY_OK);
    static const uint32_t expected[5] = {0, 1, 2, 3, 3};
    PHY_CHECK_EQ_INT(sign, 1);
    for (size_t i = 0u; i < 5u; ++i) {
        PHY_CHECK_EQ_INT(canonical[i], expected[i]);
    }
    PHY_CHECK_EQ_INT(
        phy_component_tensor_set(
            tensor, descending, phy_ir_integer(f.ir, 4)),
        PHY_OK);
    const uint32_t second[5] = {0, 0, 0, 0, 0};
    const uint32_t third[5] = {1, 1, 1, 1, 1};
    PHY_CHECK_EQ_INT(
        phy_component_tensor_set(
            tensor, second, phy_ir_integer(f.ir, 2)),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_set(
            tensor, third, phy_ir_integer(f.ir, 3)),
        PHY_ERR_TERM_LIMIT);
    phy_component_tensor_destroy(tensor);
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_rank_zero_component_scalar(void)
{
    fixture f = fixture_open();
    phy_abstract_tensor_head *scalar_head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "ScalarField", NULL, 0u,
            PHY_TENSOR_COMMUTING, &scalar_head),
        PHY_OK);
    phy_component_tensor *scalar = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_create(
            scalar_head, NULL, NULL, NULL, &scalar),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_component_tensor_symmetry_order(scalar), 1);
    PHY_CHECK_EQ_INT(
        phy_component_tensor_set(
            scalar, NULL, phy_ir_integer(f.ir, 11)),
        PHY_OK);
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(scalar, NULL, &value), PHY_OK);
    PHY_CHECK_EQ_INT(exact_integer(&f, value), 11);
    phy_component_tensor_destroy(scalar);
    fixture_close(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_basis_binds_known_and_symbolic_dimensions);
    PHY_TEST_CASE(test_sparse_rank_nine_does_not_allocate_dense_power);
    PHY_TEST_CASE(test_signed_component_orbits_and_forced_zero);
    PHY_TEST_CASE(test_symmetric_component_canonicalization_and_limits);
    PHY_TEST_CASE(test_rank_zero_component_scalar);
    return PHY_TEST_REPORT("component_tensor");
}
