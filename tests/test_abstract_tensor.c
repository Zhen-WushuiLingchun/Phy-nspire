/*
 * Phy-nspire — abstract-index object model tests.
 *
 * These tests deliberately create rank-six heads while the legacy component
 * tensor remains rank-four.  No component array belongs in this layer.
 */
#include "phy/abstract_tensor.h"
#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_abstract_context *abstract;
} fixture;

static fixture fixture_open(const phy_abstract_limits *limits)
{
    fixture f = {0};
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(f.cas, limits, &f.abstract), PHY_OK);
    PHY_CHECK(f.abstract != NULL);
    return f;
}

static void fixture_close(fixture *f)
{
    phy_abstract_context_destroy(f->abstract);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
    phy_platform_shutdown();
}

static void test_index_spaces(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *lorentz = NULL;
    phy_index_space *spinor = NULL;
    phy_index_space *generic = NULL;
    const phy_ir_ref four = phy_ir_integer(f.ir, 4);
    const phy_ir_ref n = phy_ir_symbol_ref(f.ir, phy_ir_intern(f.ir, "n"));

    PHY_CHECK_EQ_INT(
        phy_index_space_create(f.abstract, "Lorentz", four,
                               PHY_METRIC_SYMMETRIC, &lorentz),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_index_space_create(f.abstract, "Spinor", four,
                               PHY_METRIC_ANTISYMMETRIC, &spinor),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_index_space_create(f.abstract, "Internal", n, PHY_METRIC_NONE,
                               &generic),
        PHY_OK);

    PHY_CHECK_EQ_STR(phy_index_space_name(lorentz), "Lorentz");
    PHY_CHECK_EQ_INT(phy_index_space_metric(lorentz), PHY_METRIC_SYMMETRIC);
    PHY_CHECK_EQ_INT(phy_index_space_metric(spinor),
                     PHY_METRIC_ANTISYMMETRIC);
    PHY_CHECK(phy_index_space_dimension(generic) == n);
    size_t dimension = 0u;
    PHY_CHECK(phy_index_space_known_dimension(lorentz, &dimension));
    PHY_CHECK_EQ_INT(dimension, 4);
    PHY_CHECK(!phy_index_space_known_dimension(generic, &dimension));

    phy_index_space *duplicate = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(f.abstract, "Lorentz", four,
                               PHY_METRIC_SYMMETRIC, &duplicate),
        PHY_ERR_ALREADY_INITIALIZED);
    PHY_CHECK(duplicate == NULL);
    PHY_CHECK_EQ_INT(phy_abstract_space_count(f.abstract), 3);

    phy_index_space *bad = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(f.abstract, "Zero", phy_ir_integer(f.ir, 0),
                               PHY_METRIC_NONE, &bad),
        PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(
        phy_index_space_create(f.abstract, "Negative",
                               phy_ir_integer(f.ir, -2), PHY_METRIC_NONE,
                               &bad),
        PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "Expression",
            phy_ir_add(
                f.ir, (phy_ir_ref[2]){n, phy_ir_integer(f.ir, 1)}, 2u),
            PHY_METRIC_NONE, &bad),
        PHY_ERR_TYPE);

    fixture_close(&f);
}

static void test_rank_six_head_and_application(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_SYMMETRIC, &space),
        PHY_OK);

    const phy_index_space *slots[6] = {
        space, space, space, space, space, space};
    phy_tensor_head *head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "T", slots, 6u, PHY_TENSOR_COMMUTING, &head),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_head_slot_count(head), 6);
    PHY_CHECK_EQ_STR(phy_tensor_head_name(head), "T");

    phy_abstract_index indices[6];
    static const char *const names[6] = {"a", "b", "c", "d", "e", "f"};
    for (size_t i = 0u; i < 6u; ++i) {
        PHY_CHECK_EQ_INT(
            phy_abstract_index_make(
                space, names[i],
                (i & 1u) != 0u ? PHY_IR_INDEX_UPPER : PHY_IR_INDEX_LOWER,
                &indices[i]),
            PHY_OK);
    }
    phy_ir_ref applied = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_apply(head, indices, 6u, &applied), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_kind_of(f.ir, applied), PHY_IR_TENSOR);
    PHY_CHECK_EQ_INT(phy_ir_child_count(f.ir, applied), 6);
    for (size_t i = 0u; i < 6u; ++i) {
        const phy_ir_ref index = phy_ir_child(f.ir, applied, i);
        PHY_CHECK_EQ_INT(phy_ir_kind_of(f.ir, index), PHY_IR_INDEX);
        PHY_CHECK_EQ_INT(
            phy_ir_index_space(f.ir, index), phy_index_space_symbol(space));
    }

    phy_index_space *other = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(f.abstract, "N", phy_ir_integer(f.ir, 3),
                               PHY_METRIC_NONE, &other),
        PHY_OK);
    phy_abstract_index wrong = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            other, "z", PHY_IR_INDEX_LOWER, &wrong), PHY_OK);
    indices[3] = wrong;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_apply(head, indices, 6u, &applied), PHY_ERR_TYPE);

    fixture_close(&f);
}

static void test_signed_slot_generators(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_SYMMETRIC, &space),
        PHY_OK);
    const phy_index_space *slots[4] = {space, space, space, space};
    phy_tensor_head *riemann = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "R", slots, 4u, PHY_TENSOR_COMMUTING, &riemann),
        PHY_OK);

    static const uint16_t antisym_first[] = {1, 0, 2, 3};
    static const uint16_t antisym_second[] = {0, 1, 3, 2};
    static const uint16_t pair_exchange[] = {2, 3, 0, 1};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(riemann, antisym_first, -1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(riemann, antisym_second, -1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(riemann, pair_exchange, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_head_symmetry_count(riemann), 3);

    const uint16_t *image = NULL;
    int sign = 0;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_symmetry(riemann, 2u, &image, &sign), PHY_OK);
    PHY_CHECK_EQ_INT(sign, 1);
    for (size_t i = 0u; i < 4u; ++i) {
        PHY_CHECK_EQ_INT(image[i], pair_exchange[i]);
    }

    static const uint16_t duplicate_image[] = {0, 0, 2, 3};
    static const uint16_t outside_image[] = {0, 1, 2, 4};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(riemann, duplicate_image, 1),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(riemann, outside_image, 1),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(riemann, pair_exchange, 0),
        PHY_ERR_INVALID_ARGUMENT);

    /* Adding the same generator is idempotent. */
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(riemann, pair_exchange, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_head_symmetry_count(riemann), 3);

    fixture_close(&f);
}

static void test_limits_are_runtime_not_semantic(void)
{
    phy_abstract_limits limits = {0};
    limits.max_spaces = 1u;
    limits.max_heads = 1u;
    limits.max_slots = 8u;
    limits.max_generators = 4u;
    fixture f = fixture_open(&limits);

    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 5), PHY_METRIC_NONE,
            &space),
        PHY_OK);
    phy_index_space *too_many = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "N", phy_ir_integer(f.ir, 5), PHY_METRIC_NONE,
            &too_many),
        PHY_ERR_TERM_LIMIT);

    const phy_index_space *slots[8] = {
        space, space, space, space, space, space, space, space};
    phy_tensor_head *rank_eight = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "A", slots, 8u, PHY_TENSOR_NONCOMMUTING,
            &rank_eight),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_head_slot_count(rank_eight), 8);

    phy_tensor_head *rank_nine = NULL;
    const phy_index_space *nine[9] = {
        space, space, space, space, space, space, space, space, space};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "B", nine, 9u, PHY_TENSOR_COMMUTING, &rank_nine),
        PHY_ERR_TERM_LIMIT);

    fixture_close(&f);
}

static void test_monomial_index_census(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_SYMMETRIC, &space),
        PHY_OK);
    const phy_index_space *two_slots[2] = {space, space};
    const phy_index_space *one_slot[1] = {space};
    phy_tensor_head *a_head = NULL;
    phy_tensor_head *b_head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "A", two_slots, 2u, PHY_TENSOR_COMMUTING, &a_head),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "B", one_slot, 1u, PHY_TENSOR_COMMUTING, &b_head),
        PHY_OK);

    phy_abstract_index a_down = {0};
    phy_abstract_index b_down = {0};
    phy_abstract_index b_up = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "a", PHY_IR_INDEX_LOWER, &a_down), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "b", PHY_IR_INDEX_LOWER, &b_down), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "b", PHY_IR_INDEX_UPPER, &b_up), PHY_OK);
    const phy_abstract_index a_indices[2] = {a_down, b_down};
    const phy_abstract_factor factors[2] = {
        {a_head, a_indices, 2u},
        {b_head, &b_up, 1u}};

    phy_tensor_monomial *monomial = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 3), factors, 2u, &monomial),
        PHY_OK);
    PHY_CHECK(monomial != NULL);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_factor_count(monomial), 2);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_index_use_count(monomial), 2);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_free_count(monomial), 1);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_dummy_count(monomial), 1);

    phy_abstract_index_use use = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_index_use(monomial, 0u, &use), PHY_OK);
    PHY_CHECK(use.space == space);
    PHY_CHECK_EQ_INT(use.role, PHY_ABSTRACT_INDEX_FREE);
    PHY_CHECK_EQ_INT(use.lower_count, 1);
    PHY_CHECK_EQ_INT(use.upper_count, 0);
    PHY_CHECK_EQ_INT(use.name, a_down.name);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_index_use(monomial, 1u, &use), PHY_OK);
    PHY_CHECK_EQ_INT(use.role, PHY_ABSTRACT_INDEX_DUMMY);
    PHY_CHECK_EQ_INT(use.lower_count, 1);
    PHY_CHECK_EQ_INT(use.upper_count, 1);
    PHY_CHECK_EQ_INT(use.name, b_down.name);

    const phy_tensor_head *queried_head = NULL;
    const phy_abstract_index *queried_indices = NULL;
    size_t queried_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            monomial, 0u, &queried_head, &queried_indices, &queried_count),
        PHY_OK);
    PHY_CHECK(queried_head == a_head);
    PHY_CHECK_EQ_INT(queried_count, 2);
    PHY_CHECK_EQ_INT(queried_indices[0].name, a_down.name);

    phy_tensor_monomial_destroy(monomial);
    fixture_close(&f);
}

static void test_monomial_rejects_malformed_indices(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_SYMMETRIC, &space),
        PHY_OK);
    const phy_index_space *slots[2] = {space, space};
    phy_tensor_head *head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "T", slots, 2u, PHY_TENSOR_COMMUTING, &head),
        PHY_OK);
    phy_abstract_index lower[2] = {{0}};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "i", PHY_IR_INDEX_LOWER, &lower[0]), PHY_OK);
    lower[1] = lower[0];
    const phy_abstract_factor same_variance = {head, lower, 2u};
    phy_tensor_monomial *bad = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), &same_variance, 1u, &bad),
        PHY_ERR_TYPE);
    PHY_CHECK(bad == NULL);

    /* Three occurrences are ambiguous even when two have opposite variance. */
    const phy_index_space *single_slot[1] = {space};
    phy_tensor_head *vector = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "V", single_slot, 1u, PHY_TENSOR_COMMUTING, &vector),
        PHY_OK);
    phy_abstract_index upper = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "i", PHY_IR_INDEX_UPPER, &upper), PHY_OK);
    const phy_abstract_factor triple[2] = {
        {head, lower, 2u}, {vector, &upper, 1u}};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), triple, 2u, &bad),
        PHY_ERR_TYPE);

    phy_abstract_index valid_pair[2] = {lower[0], upper};
    const phy_abstract_factor valid = {head, valid_pair, 2u};
    phy_ir_ref tensor_coefficient = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_apply(head, valid_pair, 2u, &tensor_coefficient),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, tensor_coefficient, &valid, 1u, &bad),
        PHY_ERR_TYPE);

    fixture_close(&f);
}

static void test_same_name_in_different_spaces_is_distinct(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *m = NULL;
    phy_index_space *colour = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4), PHY_METRIC_NONE, &m),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "Color", phy_ir_integer(f.ir, 8),
            PHY_METRIC_NONE, &colour),
        PHY_OK);
    const phy_index_space *m_slot[1] = {m};
    const phy_index_space *c_slot[1] = {colour};
    phy_tensor_head *v = NULL;
    phy_tensor_head *t = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "V", m_slot, 1u, PHY_TENSOR_COMMUTING, &v),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "Ta", c_slot, 1u, PHY_TENSOR_NONCOMMUTING, &t),
        PHY_OK);
    phy_abstract_index spacetime_i = {0};
    phy_abstract_index colour_i = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            m, "i", PHY_IR_INDEX_UPPER, &spacetime_i), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            colour, "i", PHY_IR_INDEX_UPPER, &colour_i), PHY_OK);
    const phy_abstract_factor factors[2] = {
        {v, &spacetime_i, 1u}, {t, &colour_i, 1u}};
    phy_tensor_monomial *monomial = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), factors, 2u, &monomial),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_free_count(monomial), 2);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_dummy_count(monomial), 0);
    phy_tensor_monomial_destroy(monomial);
    fixture_close(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_index_spaces);
    PHY_TEST_CASE(test_rank_six_head_and_application);
    PHY_TEST_CASE(test_signed_slot_generators);
    PHY_TEST_CASE(test_limits_are_runtime_not_semantic);
    PHY_TEST_CASE(test_monomial_index_census);
    PHY_TEST_CASE(test_monomial_rejects_malformed_indices);
    PHY_TEST_CASE(test_same_name_in_different_spaces_is_distinct);
    return PHY_TEST_REPORT("abstract_tensor");
}
