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

    phy_index_space *other = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "N", phy_ir_integer(f.ir, 4), PHY_METRIC_NONE,
            &other),
        PHY_OK);
    const phy_index_space *mixed_slots[2] = {space, other};
    phy_tensor_head *mixed = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "Mixed", mixed_slots, 2u,
            PHY_TENSOR_COMMUTING, &mixed),
        PHY_OK);
    static const uint16_t cross_space_swap[] = {1, 0};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(
            mixed, cross_space_swap, 1), PHY_ERR_TYPE);

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

static int64_t exact_integer(const fixture *f, phy_ir_ref value)
{
    int64_t integer = INT64_MIN;
    PHY_CHECK(phy_ir_integer_value(f->ir, value, &integer));
    return integer;
}

static void test_free_index_and_factor_canonicalization(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_SYMMETRIC, &space),
        PHY_OK);
    const phy_index_space *two[2] = {space, space};
    const phy_index_space *one[1] = {space};
    phy_tensor_head *antisymmetric = NULL;
    phy_tensor_head *a_head = NULL;
    phy_tensor_head *b_head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "F", two, 2u, PHY_TENSOR_COMMUTING,
            &antisymmetric),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "A", one, 1u, PHY_TENSOR_COMMUTING, &a_head),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "B", one, 1u, PHY_TENSOR_COMMUTING, &b_head),
        PHY_OK);
    static const uint16_t swap[] = {1, 0};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(
            antisymmetric, swap, -1), PHY_OK);

    phy_abstract_index a = {0};
    phy_abstract_index b = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "a", PHY_IR_INDEX_LOWER, &a), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "b", PHY_IR_INDEX_LOWER, &b), PHY_OK);
    const phy_abstract_index reversed[2] = {b, a};
    const phy_abstract_factor field = {
        antisymmetric, reversed, 2u};
    phy_tensor_monomial *input = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 3), &field, 1u, &input),
        PHY_OK);
    phy_tensor_monomial *canonical = NULL;
    phy_tensor_canonical_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, &stats), PHY_OK);
    PHY_CHECK_EQ_INT(stats.slot_group_order, 2);
    PHY_CHECK(stats.candidates_visited >= 1u);
    PHY_CHECK(stats.candidates_visited <= stats.slot_group_order);
    PHY_CHECK_EQ_INT(
        exact_integer(
            &f, phy_tensor_monomial_coefficient(canonical)), -3);
    const phy_tensor_head *head = NULL;
    const phy_abstract_index *indices = NULL;
    size_t count = 0u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            canonical, 0u, &head, &indices, &count), PHY_OK);
    PHY_CHECK(head == antisymmetric);
    PHY_CHECK_EQ_INT(count, 2);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, indices[0].name), "a");
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, indices[1].name), "b");
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);

    /*
     * Different commuting heads sort by name independently of construction
     * order.  The indices remain attached to their original heads.
     */
    const phy_abstract_factor unsorted[2] = {
        {b_head, &b, 1u}, {a_head, &a, 1u}};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), unsorted, 2u, &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, NULL), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            canonical, 0u, &head, &indices, &count), PHY_OK);
    PHY_CHECK(head == a_head);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, indices[0].name), "a");
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            canonical, 1u, &head, &indices, &count), PHY_OK);
    PHY_CHECK(head == b_head);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, indices[0].name), "b");
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_dummy_alpha_renaming_and_metric_zero(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_SYMMETRIC, &space),
        PHY_OK);
    const phy_index_space *one[1] = {space};
    const phy_index_space *two[2] = {space, space};
    phy_tensor_head *vector = NULL;
    phy_tensor_head *two_form = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "V", one, 1u, PHY_TENSOR_COMMUTING, &vector),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "A", two, 2u, PHY_TENSOR_COMMUTING, &two_form),
        PHY_OK);
    static const uint16_t swap[] = {1, 0};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(two_form, swap, -1), PHY_OK);

    phy_abstract_index q_up = {0};
    phy_abstract_index q_down = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "arbitraryDummyName", PHY_IR_INDEX_UPPER, &q_up),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "arbitraryDummyName", PHY_IR_INDEX_LOWER, &q_down),
        PHY_OK);
    const phy_abstract_factor vector_pair[2] = {
        {vector, &q_down, 1u}, {vector, &q_up, 1u}};
    phy_tensor_monomial *input = NULL;
    phy_tensor_monomial *canonical = NULL;
    phy_tensor_canonical_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 5), vector_pair, 2u, &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, &stats), PHY_OK);
    PHY_CHECK_EQ_INT(stats.slot_group_order, 2);
    const phy_tensor_head *head = NULL;
    const phy_abstract_index *first = NULL;
    const phy_abstract_index *second = NULL;
    size_t count = 0u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            canonical, 0u, &head, &first, &count), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            canonical, 1u, &head, &second, &count), PHY_OK);
    PHY_CHECK_EQ_INT(first[0].name, second[0].name);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, first[0].name), "_d0");
    PHY_CHECK_EQ_INT(first[0].variance, PHY_IR_INDEX_UPPER);
    PHY_CHECK_EQ_INT(second[0].variance, PHY_IR_INDEX_LOWER);
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);

    const phy_abstract_index trace_indices[2] = {q_up, q_down};
    const phy_abstract_factor trace = {
        two_form, trace_indices, 2u};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 7), &trace, 1u, &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, &stats), PHY_OK);
    PHY_CHECK(stats.zero_by_symmetry);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_factor_count(canonical), 0);
    PHY_CHECK_EQ_INT(
        exact_integer(
            &f, phy_tensor_monomial_coefficient(canonical)), 0);
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_metric_type_controls_dummy_orientation(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *symplectic = NULL;
    phy_index_space *unmetric = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "Symplectic", phy_ir_integer(f.ir, 4),
            PHY_METRIC_ANTISYMMETRIC, &symplectic),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "NoMetric", phy_ir_integer(f.ir, 4),
            PHY_METRIC_NONE, &unmetric),
        PHY_OK);

    const phy_index_space *symplectic_slots[2] = {
        symplectic, symplectic};
    const phy_index_space *unmetric_slots[2] = {unmetric, unmetric};
    phy_tensor_head *plain = NULL;
    phy_tensor_head *antisymmetric = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "P", symplectic_slots, 2u,
            PHY_TENSOR_COMMUTING, &plain),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "A", unmetric_slots, 2u,
            PHY_TENSOR_COMMUTING, &antisymmetric),
        PHY_OK);
    static const uint16_t swap[] = {1, 0};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(
            antisymmetric, swap, -1), PHY_OK);

    phy_abstract_index lower = {0};
    phy_abstract_index upper = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            symplectic, "s", PHY_IR_INDEX_LOWER, &lower), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            symplectic, "s", PHY_IR_INDEX_UPPER, &upper), PHY_OK);
    const phy_abstract_index symplectic_pair[2] = {lower, upper};
    const phy_abstract_factor symplectic_factor = {
        plain, symplectic_pair, 2u};
    phy_tensor_monomial *input = NULL;
    phy_tensor_monomial *canonical = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 2), &symplectic_factor, 1u,
            &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, NULL), PHY_OK);
    PHY_CHECK_EQ_INT(
        exact_integer(
            &f, phy_tensor_monomial_coefficient(canonical)), -2);
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);

    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            unmetric, "u", PHY_IR_INDEX_UPPER, &upper), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            unmetric, "u", PHY_IR_INDEX_LOWER, &lower), PHY_OK);
    const phy_abstract_index unmetric_pair[2] = {upper, lower};
    const phy_abstract_factor unmetric_factor = {
        antisymmetric, unmetric_pair, 2u};
    phy_tensor_canonical_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 3), &unmetric_factor, 1u,
            &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, &stats), PHY_OK);
    PHY_CHECK(!stats.zero_by_symmetry);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_factor_count(canonical), 1);
    /*
     * No metric means upper/lower orientation is not identified.  The slot
     * swap still chooses lower-before-upper and contributes its own minus,
     * but the two orientations remain distinct and therefore do not imply
     * zero.
     */
    PHY_CHECK_EQ_INT(
        exact_integer(
            &f, phy_tensor_monomial_coefficient(canonical)), -3);
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_xperm_rank_six_oracle_and_work_limit(void)
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
    static const uint16_t swap02[] = {2, 1, 0, 3, 4, 5};
    static const uint16_t swap04[] = {4, 1, 2, 3, 0, 5};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(head, swap02, -1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(head, swap04, -1), PHY_OK);

    static const char *const names[6] = {
        "d3", "d2", "d1", "d1", "d2", "d3"};
    phy_abstract_index indices[6] = {{0}};
    for (size_t i = 0u; i < 6u; ++i) {
        PHY_CHECK_EQ_INT(
            phy_abstract_index_make(
                space, names[i],
                i < 3u ? PHY_IR_INDEX_UPPER : PHY_IR_INDEX_LOWER,
                &indices[i]),
            PHY_OK);
    }
    const phy_abstract_factor factor = {head, indices, 6u};
    phy_tensor_monomial *input = NULL;
    phy_tensor_monomial *canonical = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), &factor, 1u, &input),
        PHY_OK);
    phy_tensor_canonical_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, &stats), PHY_OK);
    PHY_CHECK_EQ_INT(stats.slot_group_order, 6);
    PHY_CHECK(stats.candidates_visited >= 1u);
    PHY_CHECK(stats.candidates_visited < stats.slot_group_order);
    PHY_CHECK_EQ_INT(
        exact_integer(
            &f, phy_tensor_monomial_coefficient(canonical)), -1);
    const phy_tensor_head *queried = NULL;
    const phy_abstract_index *result = NULL;
    size_t count = 0u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            canonical, 0u, &queried, &result, &count), PHY_OK);
    PHY_CHECK_EQ_INT(count, 6);
    /*
     * xPerm's representative is the identity on the ordered label list
     * [d1,-d1,d2,-d2,d3,-d3], hence contracted pairs are adjacent.
     */
    for (size_t pair = 0u; pair < 3u; ++pair) {
        const size_t upper = 2u * pair;
        const size_t lower = upper + 1u;
        PHY_CHECK_EQ_INT(result[upper].variance, PHY_IR_INDEX_UPPER);
        PHY_CHECK_EQ_INT(result[lower].variance, PHY_IR_INDEX_LOWER);
        PHY_CHECK_EQ_INT(result[upper].name, result[lower].name);
    }
    phy_tensor_monomial_destroy(canonical);

    phy_tensor_canonical_limits limits = {0};
    limits.max_candidates = 1u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, &limits, &canonical, &stats), PHY_ERR_TIMEOUT);
    PHY_CHECK(canonical == NULL);
    PHY_CHECK_EQ_INT(stats.slot_group_order, 6);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_symmetric_rank_nine_is_pruned_not_enumerated(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 9),
            PHY_METRIC_NONE, &space),
        PHY_OK);
    const phy_index_space *slots[9] = {
        space, space, space, space, space, space, space, space, space};
    phy_tensor_head *symmetric = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "S", slots, 9u, PHY_TENSOR_COMMUTING,
            &symmetric),
        PHY_OK);
    for (size_t adjacent = 0u; adjacent + 1u < 9u; ++adjacent) {
        uint16_t swap[9];
        for (size_t i = 0u; i < 9u; ++i) {
            swap[i] = (uint16_t)i;
        }
        swap[adjacent] = (uint16_t)(adjacent + 1u);
        swap[adjacent + 1u] = (uint16_t)adjacent;
        PHY_CHECK_EQ_INT(
            phy_tensor_head_add_symmetry(
                symmetric, swap, 1), PHY_OK);
    }

    static const char *const reversed_names[9] = {
        "i", "h", "g", "f", "e", "d", "c", "b", "a"};
    phy_abstract_index indices[9] = {{0}};
    for (size_t i = 0u; i < 9u; ++i) {
        PHY_CHECK_EQ_INT(
            phy_abstract_index_make(
                space, reversed_names[i], PHY_IR_INDEX_LOWER,
                &indices[i]),
            PHY_OK);
    }
    const phy_abstract_factor factor = {symmetric, indices, 9u};
    phy_tensor_monomial *input = NULL;
    phy_tensor_monomial *canonical = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), &factor, 1u, &input),
        PHY_OK);
    phy_tensor_canonical_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, &stats), PHY_OK);
    PHY_CHECK_EQ_INT(stats.slot_group_order, 362880);
    PHY_CHECK(stats.candidates_visited < 100u);

    const phy_tensor_head *head = NULL;
    const phy_abstract_index *result = NULL;
    size_t count = 0u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            canonical, 0u, &head, &result, &count), PHY_OK);
    PHY_CHECK_EQ_INT(count, 9);
    for (size_t i = 0u; i < 9u; ++i) {
        const char expected[2] = {(char)('a' + i), '\0'};
        PHY_CHECK_EQ_STR(
            phy_ir_symbol_name(f.ir, result[i].name), expected);
    }
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);
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
    PHY_TEST_CASE(test_free_index_and_factor_canonicalization);
    PHY_TEST_CASE(test_dummy_alpha_renaming_and_metric_zero);
    PHY_TEST_CASE(test_metric_type_controls_dummy_orientation);
    PHY_TEST_CASE(test_xperm_rank_six_oracle_and_work_limit);
    PHY_TEST_CASE(test_symmetric_rank_nine_is_pruned_not_enumerated);
    return PHY_TEST_REPORT("abstract_tensor");
}
