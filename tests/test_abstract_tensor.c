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
    phy_abstract_tensor_head *head = NULL;
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
    phy_abstract_tensor_head *riemann = NULL;
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
    phy_abstract_tensor_head *mixed = NULL;
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
    phy_abstract_tensor_head *rank_eight = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "A", slots, 8u, PHY_TENSOR_NONCOMMUTING,
            &rank_eight),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_head_slot_count(rank_eight), 8);

    phy_abstract_tensor_head *rank_nine = NULL;
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
    phy_abstract_tensor_head *a_head = NULL;
    phy_abstract_tensor_head *b_head = NULL;
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

    const phy_abstract_tensor_head *queried_head = NULL;
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
    phy_abstract_tensor_head *head = NULL;
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
    phy_abstract_tensor_head *vector = NULL;
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
    phy_abstract_tensor_head *v = NULL;
    phy_abstract_tensor_head *t = NULL;
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

static void check_exact_coefficient(const fixture *f, phy_ir_ref value,
                                    int64_t numerator, int64_t denominator)
{
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_number(
            f->cas, numerator, denominator, &expected), PHY_OK);
    phy_ir_ref difference = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f->cas, value, expected, &difference), PHY_OK);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_is_zero(f->cas, difference, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
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
    phy_abstract_tensor_head *antisymmetric = NULL;
    phy_abstract_tensor_head *a_head = NULL;
    phy_abstract_tensor_head *b_head = NULL;
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
    const phy_abstract_tensor_head *head = NULL;
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

/*
 * Mixed products are split into maximal commuting runs.  The helpers and
 * tests below pin both sides of that rule: legal reordering happens inside a
 * run, while noncommuting factors remain barriers.
 */
typedef struct {
    phy_index_space *space;
    phy_abstract_tensor_head *pair;      /* A, rank 2, antisymmetric */
    phy_abstract_tensor_head *commuting; /* W, rank 1, commuting */
    phy_abstract_tensor_head *first;     /* Y, rank 1, noncommuting */
    phy_abstract_tensor_head *second;    /* X, rank 1, noncommuting */
} run_fixture;

static run_fixture run_fixture_open(fixture *f)
{
    run_fixture r = {0};
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f->abstract, "M", phy_ir_integer(f->ir, 4),
            PHY_METRIC_SYMMETRIC, &r.space),
        PHY_OK);
    const phy_index_space *two[2] = {r.space, r.space};
    const phy_index_space *one[1] = {r.space};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f->abstract, "A", two, 2u, PHY_TENSOR_COMMUTING, &r.pair),
        PHY_OK);
    static const uint16_t swap[2] = {1u, 0u};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(r.pair, swap, -1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f->abstract, "W", one, 1u, PHY_TENSOR_COMMUTING,
            &r.commuting),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f->abstract, "Y", one, 1u, PHY_TENSOR_NONCOMMUTING,
            &r.first),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f->abstract, "X", one, 1u, PHY_TENSOR_NONCOMMUTING,
            &r.second),
        PHY_OK);
    return r;
}

static void make_indices(const phy_index_space *space,
                         phy_abstract_index *out, const char *const *names,
                         const phy_ir_variance *variances, size_t count)
{
    for (size_t i = 0u; i < count; ++i) {
        PHY_CHECK_EQ_INT(
            phy_abstract_index_make(
                space, names[i], variances[i], &out[i]),
            PHY_OK);
    }
}

static void check_factor(const fixture *f,
                         const phy_tensor_monomial *monomial, size_t which,
                         const phy_abstract_tensor_head *expected_head,
                         const char *const *expected_names,
                         size_t expected_count)
{
    const phy_abstract_tensor_head *head = NULL;
    const phy_abstract_index *indices = NULL;
    size_t count = 0u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            monomial, which, &head, &indices, &count), PHY_OK);
    PHY_CHECK(head == expected_head);
    PHY_CHECK_EQ_INT(count, expected_count);
    for (size_t slot = 0u; slot < expected_count; ++slot) {
        PHY_CHECK_EQ_STR(
            phy_ir_symbol_name(f->ir, indices[slot].name),
            expected_names[slot]);
    }
}

static void check_same_monomial(const fixture *f,
                                const phy_tensor_monomial *left,
                                const phy_tensor_monomial *right)
{
    const size_t factor_count =
        phy_tensor_monomial_factor_count(left);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor_count(right), factor_count);
    check_exact_coefficient(
        f, phy_tensor_monomial_coefficient(left),
        exact_integer(f, phy_tensor_monomial_coefficient(right)), 1);
    for (size_t factor = 0u; factor < factor_count; ++factor) {
        const phy_abstract_tensor_head *left_head = NULL;
        const phy_abstract_tensor_head *right_head = NULL;
        const phy_abstract_index *left_indices = NULL;
        const phy_abstract_index *right_indices = NULL;
        size_t left_count = 0u;
        size_t right_count = 0u;
        PHY_CHECK_EQ_INT(
            phy_tensor_monomial_factor(
                left, factor, &left_head, &left_indices, &left_count),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_tensor_monomial_factor(
                right, factor, &right_head, &right_indices, &right_count),
            PHY_OK);
        PHY_CHECK(left_head == right_head);
        PHY_CHECK_EQ_INT(left_count, right_count);
        for (size_t slot = 0u; slot < left_count; ++slot) {
            PHY_CHECK(left_indices[slot].space ==
                      right_indices[slot].space);
            PHY_CHECK(left_indices[slot].name ==
                      right_indices[slot].name);
            PHY_CHECK_EQ_INT(left_indices[slot].variance,
                             right_indices[slot].variance);
        }
    }
}

static void check_dgs_matches(const fixture *f,
                              const phy_tensor_monomial *input,
                              const phy_tensor_monomial *production)
{
    phy_tensor_monomial *dgs = NULL;
    phy_tensor_dgs_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize_dgs(
            input, NULL, &dgs, &stats),
        PHY_OK);
    PHY_CHECK(dgs != NULL);
    PHY_CHECK(stats.products_visited > 0u || stats.zero_by_symmetry);
    check_same_monomial(f, production, dgs);
    phy_tensor_monomial_destroy(dgs);
}

static void test_commuting_run_order_and_idempotence(void)
{
    fixture f = fixture_open(NULL);
    run_fixture r = run_fixture_open(&f);
    static const char *const names[4] = {"w", "b", "a", "t"};
    static const phy_ir_variance variances[4] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_UPPER};
    phy_abstract_index indices[4] = {{0}};
    make_indices(r.space, indices, names, variances, 4u);

    /* W[^w] A[_b,_a] Y[^t] and A[_b,_a] W[^w] Y[^t]. */
    const phy_abstract_factor first_order[3] = {
        {r.commuting, &indices[0], 1u},
        {r.pair, &indices[1], 2u},
        {r.first, &indices[3], 1u}};
    const phy_abstract_factor second_order[3] = {
        {r.pair, &indices[1], 2u},
        {r.commuting, &indices[0], 1u},
        {r.first, &indices[3], 1u}};
    phy_tensor_monomial *first_input = NULL;
    phy_tensor_monomial *second_input = NULL;
    phy_tensor_monomial *first_canonical = NULL;
    phy_tensor_monomial *second_canonical = NULL;
    phy_tensor_monomial *canonical_twice = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), first_order, 3u,
            &first_input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), second_order, 3u,
            &second_input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            first_input, NULL, &first_canonical, NULL), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            second_input, NULL, &second_canonical, NULL), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            first_canonical, NULL, &canonical_twice, NULL), PHY_OK);

    static const char *const pair_expect[2] = {"a", "b"};
    static const char *const w_expect[1] = {"w"};
    static const char *const t_expect[1] = {"t"};
    check_factor(&f, first_canonical, 0u, r.pair, pair_expect, 2u);
    check_factor(&f, first_canonical, 1u, r.commuting, w_expect, 1u);
    check_factor(&f, first_canonical, 2u, r.first, t_expect, 1u);
    PHY_CHECK_EQ_INT(
        exact_integer(
            &f, phy_tensor_monomial_coefficient(first_canonical)), -1);
    check_same_monomial(&f, first_canonical, second_canonical);
    check_same_monomial(&f, first_canonical, canonical_twice);
    check_dgs_matches(&f, first_input, first_canonical);
    check_dgs_matches(&f, second_input, second_canonical);

    phy_tensor_monomial_destroy(canonical_twice);
    phy_tensor_monomial_destroy(second_canonical);
    phy_tensor_monomial_destroy(first_canonical);
    phy_tensor_monomial_destroy(second_input);
    phy_tensor_monomial_destroy(first_input);
    fixture_close(&f);
}

static void test_run_local_identical_exchange(void)
{
    fixture f = fixture_open(NULL);
    run_fixture r = run_fixture_open(&f);
    static const char *const names[6] = {"c", "d", "w", "a", "b", "t"};
    static const phy_ir_variance variances[6] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_UPPER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_UPPER};
    phy_abstract_index indices[6] = {{0}};
    make_indices(r.space, indices, names, variances, 6u);

    const phy_abstract_factor factors[4] = {
        {r.pair, &indices[0], 2u},
        {r.commuting, &indices[2], 1u},
        {r.pair, &indices[3], 2u},
        {r.first, &indices[5], 1u}};
    phy_tensor_monomial *input = NULL;
    phy_tensor_monomial *canonical = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), factors, 4u, &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, NULL), PHY_OK);
    static const char *const low[2] = {"a", "b"};
    static const char *const high[2] = {"c", "d"};
    static const char *const w_expect[1] = {"w"};
    static const char *const t_expect[1] = {"t"};
    check_factor(&f, canonical, 0u, r.pair, low, 2u);
    check_factor(&f, canonical, 1u, r.pair, high, 2u);
    check_factor(&f, canonical, 2u, r.commuting, w_expect, 1u);
    check_factor(&f, canonical, 3u, r.first, t_expect, 1u);
    PHY_CHECK_EQ_INT(
        exact_integer(&f, phy_tensor_monomial_coefficient(canonical)), 1);
    check_dgs_matches(&f, input, canonical);
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_noncommuting_barrier_blocks_exchange(void)
{
    fixture f = fixture_open(NULL);
    run_fixture r = run_fixture_open(&f);
    static const char *const names[5] = {"c", "d", "t", "a", "b"};
    static const phy_ir_variance variances[5] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_UPPER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_abstract_index indices[5] = {{0}};
    make_indices(r.space, indices, names, variances, 5u);

    const phy_abstract_factor factors[3] = {
        {r.pair, &indices[0], 2u},
        {r.first, &indices[2], 1u},
        {r.pair, &indices[3], 2u}};
    phy_tensor_monomial *input = NULL;
    phy_tensor_monomial *canonical = NULL;
    phy_tensor_canonical_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), factors, 3u, &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, &stats), PHY_OK);
    static const char *const high[2] = {"c", "d"};
    static const char *const t_expect[1] = {"t"};
    static const char *const low[2] = {"a", "b"};
    check_factor(&f, canonical, 0u, r.pair, high, 2u);
    check_factor(&f, canonical, 1u, r.first, t_expect, 1u);
    check_factor(&f, canonical, 2u, r.pair, low, 2u);
    PHY_CHECK_EQ_INT(stats.slot_group_order, 4);
    PHY_CHECK(!stats.zero_by_symmetry);
    PHY_CHECK_EQ_INT(
        exact_integer(&f, phy_tensor_monomial_coefficient(canonical)), 1);
    check_dgs_matches(&f, input, canonical);
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_noncommuting_factors_keep_relative_order(void)
{
    fixture f = fixture_open(NULL);
    run_fixture r = run_fixture_open(&f);
    static const char *const names[5] = {"y", "w", "b", "a", "x"};
    static const phy_ir_variance variances[5] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_UPPER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_UPPER};
    phy_abstract_index indices[5] = {{0}};
    make_indices(r.space, indices, names, variances, 5u);

    const phy_abstract_factor factors[4] = {
        {r.first, &indices[0], 1u},
        {r.commuting, &indices[1], 1u},
        {r.pair, &indices[2], 2u},
        {r.second, &indices[4], 1u}};
    phy_tensor_monomial *input = NULL;
    phy_tensor_monomial *canonical = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), factors, 4u, &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, NULL), PHY_OK);
    static const char *const y_expect[1] = {"y"};
    static const char *const pair_expect[2] = {"a", "b"};
    static const char *const w_expect[1] = {"w"};
    static const char *const x_expect[1] = {"x"};
    check_factor(&f, canonical, 0u, r.first, y_expect, 1u);
    check_factor(&f, canonical, 1u, r.pair, pair_expect, 2u);
    check_factor(&f, canonical, 2u, r.commuting, w_expect, 1u);
    check_factor(&f, canonical, 3u, r.second, x_expect, 1u);
    PHY_CHECK_EQ_INT(
        exact_integer(&f, phy_tensor_monomial_coefficient(canonical)), -1);
    check_dgs_matches(&f, input, canonical);
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
    phy_abstract_tensor_head *vector = NULL;
    phy_abstract_tensor_head *two_form = NULL;
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
    const phy_abstract_tensor_head *head = NULL;
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
    check_dgs_matches(&f, input, canonical);
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
    check_dgs_matches(&f, input, canonical);
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
    phy_abstract_tensor_head *plain = NULL;
    phy_abstract_tensor_head *antisymmetric = NULL;
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
     * No metric means upper/lower orientation is not identified, so the two
     * orientations remain distinct index values and do not imply zero.  The
     * canonical index alphabet still ranks the raised member of a pair first
     * — the same order SymPy's tensor_can uses — so `A[Up[u],Down[u]]` is
     * already canonical and the antisymmetric slot swap contributes nothing.
     */
    PHY_CHECK_EQ_INT(
        exact_integer(
            &f, phy_tensor_monomial_coefficient(canonical)), 3);
    const phy_abstract_tensor_head *unmetric_head = NULL;
    const phy_abstract_index *unmetric_result = NULL;
    size_t unmetric_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            canonical, 0u, &unmetric_head, &unmetric_result,
            &unmetric_count),
        PHY_OK);
    PHY_CHECK_EQ_INT(unmetric_count, 2);
    PHY_CHECK_EQ_INT(unmetric_result[0].variance, PHY_IR_INDEX_UPPER);
    PHY_CHECK_EQ_INT(unmetric_result[1].variance, PHY_IR_INDEX_LOWER);
    phy_tensor_monomial_destroy(canonical);
    phy_tensor_monomial_destroy(input);

    /* Writing the same contraction the other way round costs the slot sign. */
    const phy_abstract_index reversed_pair[2] = {lower, upper};
    const phy_abstract_factor reversed_factor = {
        antisymmetric, reversed_pair, 2u};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 3), &reversed_factor, 1u,
            &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &canonical, &stats), PHY_OK);
    PHY_CHECK(!stats.zero_by_symmetry);
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
    phy_abstract_tensor_head *head = NULL;
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
    const phy_abstract_tensor_head *queried = NULL;
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

    /*
     * The deliberately exhaustive D*g*S implementation must reach the same
     * representative.  For three symmetric-metric dummy pairs,
     * |D| = 3! 2^3 = 48; the head's two transpositions generate |S| = 6.
     */
    phy_tensor_monomial *dgs = NULL;
    phy_tensor_dgs_stats dgs_stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize_dgs(
            input, NULL, &dgs, &dgs_stats),
        PHY_OK);
    PHY_CHECK_EQ_INT(dgs_stats.degree, 6);
    PHY_CHECK_EQ_INT(dgs_stats.dummy_pair_count, 3);
    PHY_CHECK_EQ_INT(dgs_stats.slot_group_order, 6);
    PHY_CHECK_EQ_INT(dgs_stats.dummy_group_order, 48);
    PHY_CHECK_EQ_INT(dgs_stats.products_visited, 288);
    PHY_CHECK(!dgs_stats.zero_by_symmetry);
    check_same_monomial(&f, canonical, dgs);
    phy_tensor_monomial_destroy(dgs);

    phy_tensor_dgs_limits dgs_limits = {0};
    dgs_limits.max_products = 287u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize_dgs(
            input, &dgs_limits, &dgs, &dgs_stats),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK(dgs == NULL);
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

/*
 * The first worked example in SymPy 1.14 tensor_can.canonicalize:
 *
 *   A_[d0 d1] B^[d0]_[d2] B^[d2 d1] == 0
 *
 * for antisymmetric commuting A and B and a symmetric metric.  SymPy encodes
 * it as g=[1,3,0,5,4,2,6,7], dummies=range(6), msym=0 and returns 0.  The
 * companion tools/tensor_can_oracle.py executes that independent oracle.
 */
static void test_sympy_tensor_can_zero_oracle(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_SYMMETRIC, &space),
        PHY_OK);
    const phy_index_space *slots[2] = {space, space};
    static const uint16_t swap[] = {1, 0};
    phy_abstract_tensor_head *a_head = NULL;
    phy_abstract_tensor_head *b_head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "A", slots, 2u, PHY_TENSOR_COMMUTING, &a_head),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(a_head, swap, -1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "B", slots, 2u, PHY_TENSOR_COMMUTING, &b_head),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(b_head, swap, -1), PHY_OK);

    phy_abstract_index d0_down = {0};
    phy_abstract_index d0_up = {0};
    phy_abstract_index d1_down = {0};
    phy_abstract_index d1_up = {0};
    phy_abstract_index d2_down = {0};
    phy_abstract_index d2_up = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "d0", PHY_IR_INDEX_LOWER, &d0_down),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "d0", PHY_IR_INDEX_UPPER, &d0_up),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "d1", PHY_IR_INDEX_LOWER, &d1_down),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "d1", PHY_IR_INDEX_UPPER, &d1_up),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "d2", PHY_IR_INDEX_LOWER, &d2_down),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "d2", PHY_IR_INDEX_UPPER, &d2_up),
        PHY_OK);

    const phy_abstract_index a_indices[2] = {d0_down, d1_down};
    const phy_abstract_index b0_indices[2] = {d0_up, d2_down};
    const phy_abstract_index b1_indices[2] = {d2_up, d1_up};
    const phy_abstract_factor factors[3] = {
        {a_head, a_indices, 2u},
        {b_head, b0_indices, 2u},
        {b_head, b1_indices, 2u}};
    phy_tensor_monomial *input = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), factors, 3u, &input),
        PHY_OK);

    phy_tensor_monomial *production = NULL;
    phy_tensor_canonical_stats production_stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize(
            input, NULL, &production, &production_stats),
        PHY_OK);
    PHY_CHECK(production_stats.zero_by_symmetry);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_factor_count(production), 0);
    PHY_CHECK_EQ_INT(
        exact_integer(
            &f, phy_tensor_monomial_coefficient(production)),
        0);

    phy_tensor_monomial *dgs = NULL;
    phy_tensor_dgs_stats dgs_stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_canonicalize_dgs(
            input, NULL, &dgs, &dgs_stats),
        PHY_OK);
    PHY_CHECK(dgs_stats.zero_by_symmetry);
    check_same_monomial(&f, production, dgs);

    phy_tensor_monomial_destroy(dgs);
    phy_tensor_monomial_destroy(production);
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
    phy_abstract_tensor_head *symmetric = NULL;
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

    const phy_abstract_tensor_head *head = NULL;
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

static void test_young_row_and_column_projectors(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_NONE, &space),
        PHY_OK);
    const phy_index_space *slots[2] = {space, space};
    phy_abstract_tensor_head *head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "T", slots, 2u, PHY_TENSOR_COMMUTING, &head),
        PHY_OK);
    phy_abstract_index a = {0};
    phy_abstract_index b = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "a", PHY_IR_INDEX_LOWER, &a), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "b", PHY_IR_INDEX_LOWER, &b), PHY_OK);
    const phy_abstract_index reversed[2] = {b, a};
    const phy_abstract_factor factor = {head, reversed, 2u};
    phy_tensor_monomial *input = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), &factor, 1u, &input),
        PHY_OK);

    static const uint16_t tableau_slots[2] = {0, 1};
    static const uint16_t symmetric_rows[1] = {2};
    const phy_young_tableau symmetric = {
        tableau_slots, 2u, symmetric_rows, 1u};
    phy_tensor_expression *expression = NULL;
    phy_young_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            input, 0u, &symmetric, NULL, &expression, &stats), PHY_OK);
    PHY_CHECK_EQ_INT(stats.row_group_order, 2);
    PHY_CHECK_EQ_INT(stats.column_group_order, 1);
    PHY_CHECK_EQ_INT(stats.hook_product, 2);
    PHY_CHECK_EQ_INT(stats.generated_terms, 2);
    PHY_CHECK_EQ_INT(stats.collected_terms, 2);
    PHY_CHECK_EQ_INT(phy_tensor_expression_term_count(expression), 2);
    for (size_t term = 0u; term < 2u; ++term) {
        check_exact_coefficient(
            &f, phy_tensor_monomial_coefficient(
                    phy_tensor_expression_term(expression, term)),
            1, 2);
    }
    const phy_abstract_tensor_head *queried = NULL;
    const phy_abstract_index *result = NULL;
    size_t count = 0u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_factor(
            phy_tensor_expression_term(expression, 0u), 0u, &queried,
            &result, &count),
        PHY_OK);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, result[0].name), "a");
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, result[1].name), "b");
    phy_tensor_expression_destroy(expression);

    static const uint16_t antisymmetric_rows[2] = {1, 1};
    const phy_young_tableau antisymmetric = {
        tableau_slots, 2u, antisymmetric_rows, 2u};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            input, 0u, &antisymmetric, NULL, &expression, &stats), PHY_OK);
    PHY_CHECK_EQ_INT(stats.row_group_order, 1);
    PHY_CHECK_EQ_INT(stats.column_group_order, 2);
    PHY_CHECK_EQ_INT(stats.hook_product, 2);
    PHY_CHECK_EQ_INT(phy_tensor_expression_term_count(expression), 2);
    check_exact_coefficient(
        &f, phy_tensor_monomial_coefficient(
                phy_tensor_expression_term(expression, 0u)),
        -1, 2);
    check_exact_coefficient(
        &f, phy_tensor_monomial_coefficient(
                phy_tensor_expression_term(expression, 1u)),
        1, 2);
    phy_tensor_expression_destroy(expression);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_young_collection_hook_and_typed_validation(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    phy_index_space *other = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_NONE, &space),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "N", phy_ir_integer(f.ir, 3),
            PHY_METRIC_NONE, &other),
        PHY_OK);
    const phy_index_space *two[2] = {space, space};
    phy_abstract_tensor_head *symmetric_head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "S", two, 2u, PHY_TENSOR_COMMUTING,
            &symmetric_head),
        PHY_OK);
    static const uint16_t swap[] = {1, 0};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(
            symmetric_head, swap, 1), PHY_OK);
    phy_abstract_index a = {0};
    phy_abstract_index b = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "a", PHY_IR_INDEX_LOWER, &a), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "b", PHY_IR_INDEX_LOWER, &b), PHY_OK);
    const phy_abstract_index indices[2] = {b, a};
    const phy_abstract_factor factor = {
        symmetric_head, indices, 2u};
    phy_tensor_monomial *input = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), &factor, 1u, &input),
        PHY_OK);
    static const uint16_t tableau_slots[2] = {0, 1};
    static const uint16_t row[1] = {2};
    const phy_young_tableau tableau = {
        tableau_slots, 2u, row, 1u};
    phy_tensor_expression *expression = NULL;
    phy_young_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            input, 0u, &tableau, NULL, &expression, &stats), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_expression_term_count(expression), 1);
    check_exact_coefficient(
        &f, phy_tensor_monomial_coefficient(
                phy_tensor_expression_term(expression, 0u)),
        1, 1);
    phy_tensor_expression_destroy(expression);
    phy_tensor_monomial_destroy(input);

    const phy_index_space *three[3] = {space, space, space};
    phy_abstract_tensor_head *rank_three = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "Y", three, 3u, PHY_TENSOR_COMMUTING,
            &rank_three),
        PHY_OK);
    phy_abstract_index c = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "c", PHY_IR_INDEX_LOWER, &c), PHY_OK);
    const phy_abstract_index abc[3] = {a, b, c};
    const phy_abstract_factor rank_three_factor = {
        rank_three, abc, 3u};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), &rank_three_factor, 1u,
            &input),
        PHY_OK);
    static const uint16_t three_slots[3] = {0, 1, 2};
    static const uint16_t two_one[2] = {2, 1};
    const phy_young_tableau shape_two_one = {
        three_slots, 3u, two_one, 2u};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            input, 0u, &shape_two_one, NULL, &expression, &stats), PHY_OK);
    PHY_CHECK_EQ_INT(stats.row_group_order, 2);
    PHY_CHECK_EQ_INT(stats.column_group_order, 2);
    PHY_CHECK_EQ_INT(stats.hook_product, 3);
    PHY_CHECK_EQ_INT(stats.generated_terms, 4);
    phy_tensor_expression_destroy(expression);
    phy_tensor_monomial_destroy(input);

    const phy_index_space *mixed_slots[2] = {space, other};
    phy_abstract_tensor_head *mixed = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "MixedYoung", mixed_slots, 2u,
            PHY_TENSOR_COMMUTING, &mixed),
        PHY_OK);
    phy_abstract_index other_index = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            other, "n", PHY_IR_INDEX_LOWER, &other_index), PHY_OK);
    const phy_abstract_index mixed_indices[2] = {a, other_index};
    const phy_abstract_factor mixed_factor = {
        mixed, mixed_indices, 2u};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), &mixed_factor, 1u,
            &input),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            input, 0u, &tableau, NULL, &expression, NULL),
        PHY_ERR_TYPE);
    PHY_CHECK(expression == NULL);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_expression_algebra_collects_and_preserves_signature(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 3),
            PHY_METRIC_NONE, &space),
        PHY_OK);
    const phy_index_space *slots[2] = {space, space};
    phy_abstract_tensor_head *head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "T", slots, 2u,
            PHY_TENSOR_COMMUTING, &head),
        PHY_OK);
    phy_abstract_index a = {0};
    phy_abstract_index b = {0};
    phy_abstract_index c = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "a", PHY_IR_INDEX_LOWER, &a), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "b", PHY_IR_INDEX_LOWER, &b), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "c", PHY_IR_INDEX_LOWER, &c), PHY_OK);

    const phy_abstract_index ab[2] = {a, b};
    const phy_abstract_index ba[2] = {b, a};
    const phy_abstract_index cb[2] = {c, b};
    const phy_abstract_factor factor_ab = {head, ab, 2u};
    const phy_abstract_factor factor_ba = {head, ba, 2u};
    const phy_abstract_factor factor_cb = {head, cb, 2u};
    phy_tensor_monomial *monomial_ab = NULL;
    phy_tensor_monomial *monomial_ba = NULL;
    phy_tensor_monomial *monomial_cb = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            &factor_ab, 1u, &monomial_ab), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            &factor_ba, 1u, &monomial_ba), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            &factor_cb, 1u, &monomial_cb), PHY_OK);

    phy_tensor_expression *expr_ab = NULL;
    phy_tensor_expression *expr_ba = NULL;
    phy_tensor_expression *expr_cb = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(
            monomial_ab, NULL, &expr_ab), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(
            monomial_ba, NULL, &expr_ba), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(
            monomial_cb, NULL, &expr_cb), PHY_OK);
    PHY_CHECK(
        phy_tensor_expression_context(expr_ab) == f.abstract);
    PHY_CHECK_EQ_INT(phy_tensor_expression_free_count(expr_ab), 2u);
    phy_abstract_index_use free_use = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_free_use(
            expr_ab, 0u, &free_use), PHY_OK);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, free_use.name), "a");

    phy_tensor_expression *sum = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_add(
            expr_ab, expr_ba, NULL, &sum), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_expression_term_count(sum), 2u);
    PHY_CHECK_EQ_INT(phy_tensor_expression_free_count(sum), 2u);
    phy_tensor_expression_destroy(sum);

    phy_tensor_expression *negative = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_scale(
            expr_ab, phy_ir_integer(f.ir, -1), NULL, &negative),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_add(
            expr_ab, negative, NULL, &sum), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_expression_term_count(sum), 0u);
    PHY_CHECK_EQ_INT(phy_tensor_expression_free_count(sum), 2u);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_free_use(sum, 1u, &free_use), PHY_OK);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, free_use.name), "b");
    phy_tensor_expression_destroy(sum);
    phy_tensor_expression_destroy(negative);

    PHY_CHECK_EQ_INT(
        phy_tensor_expression_add(
            expr_ab, expr_cb, NULL, &sum), PHY_ERR_TYPE);
    PHY_CHECK(sum == NULL);
    phy_tensor_algebra_limits one_term = {0};
    one_term.max_result_terms = 1u;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_add(
            expr_ab, expr_ba, &one_term, &sum),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK(sum == NULL);

    const phy_index_space *one_slot[1] = {space};
    phy_abstract_tensor_head *vector = NULL;
    phy_abstract_tensor_head *covector = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "V", one_slot, 1u,
            PHY_TENSOR_COMMUTING, &vector), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "W", one_slot, 1u,
            PHY_TENSOR_COMMUTING, &covector), PHY_OK);
    phy_abstract_index a_up = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(
            space, "a", PHY_IR_INDEX_UPPER, &a_up), PHY_OK);
    const phy_abstract_factor vector_factor = {
        vector, &a_up, 1u};
    const phy_abstract_factor covector_factor = {
        covector, &a, 1u};
    phy_tensor_monomial *vector_monomial = NULL;
    phy_tensor_monomial *covector_monomial = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 2),
            &vector_factor, 1u, &vector_monomial), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 3),
            &covector_factor, 1u, &covector_monomial), PHY_OK);
    phy_tensor_expression *vector_expression = NULL;
    phy_tensor_expression *covector_expression = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(
            vector_monomial, NULL, &vector_expression), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(
            covector_monomial, NULL, &covector_expression), PHY_OK);
    phy_tensor_expression *product = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_multiply(
            vector_expression, covector_expression, NULL, &product),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_expression_free_count(product), 0u);
    PHY_CHECK_EQ_INT(phy_tensor_expression_term_count(product), 1u);
    check_exact_coefficient(
        &f, phy_tensor_monomial_coefficient(
                phy_tensor_expression_term(product, 0u)),
        6, 1);
    phy_tensor_expression_destroy(product);

    PHY_CHECK_EQ_INT(
        phy_tensor_expression_add(
            expr_ab, expr_ba, NULL, &sum), PHY_OK);
    phy_tensor_algebra_limits three_generated = {0};
    three_generated.max_generated_terms = 3u;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_multiply(
            sum, sum, &three_generated, &product),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK(product == NULL);
    phy_tensor_expression_destroy(sum);

    phy_tensor_expression_destroy(covector_expression);
    phy_tensor_expression_destroy(vector_expression);
    phy_tensor_monomial_destroy(covector_monomial);
    phy_tensor_monomial_destroy(vector_monomial);
    phy_tensor_expression_destroy(expr_cb);
    phy_tensor_expression_destroy(expr_ba);
    phy_tensor_expression_destroy(expr_ab);
    phy_tensor_monomial_destroy(monomial_cb);
    phy_tensor_monomial_destroy(monomial_ba);
    phy_tensor_monomial_destroy(monomial_ab);
    fixture_close(&f);
}

static void test_transactional_head_declaration(void)
{
    fixture f = fixture_open(NULL);
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 3),
            PHY_METRIC_NONE, &space),
        PHY_OK);
    const phy_index_space *slots[3] = {space, space, space};
    static const uint16_t invalid[3] = {1u, 1u, 2u};
    const uint16_t *invalid_generators[1] = {invalid};
    const int signs[1] = {-1};
    const size_t heads_before =
        phy_abstract_head_count(f.abstract);
    const size_t bytes_before =
        phy_abstract_bytes_used(f.abstract);
    phy_abstract_tensor_head *head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create_with_symmetries(
            f.abstract, "Bad", slots, 3u, PHY_TENSOR_COMMUTING,
            invalid_generators, signs, 1u, &head),
        PHY_ERR_TYPE);
    PHY_CHECK(head == NULL);
    PHY_CHECK_EQ_INT(
        phy_abstract_head_count(f.abstract), heads_before);
    PHY_CHECK_EQ_INT(
        phy_abstract_bytes_used(f.abstract), bytes_before);

    static const uint16_t swap[3] = {1u, 0u, 2u};
    const uint16_t *valid_generators[1] = {swap};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create_with_symmetries(
            f.abstract, "Good", slots, 3u, PHY_TENSOR_COMMUTING,
            valid_generators, signs, 1u, &head),
        PHY_OK);
    PHY_CHECK(head != NULL);
    PHY_CHECK_EQ_INT(phy_tensor_head_symmetry_count(head), 1);
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
    PHY_TEST_CASE(test_commuting_run_order_and_idempotence);
    PHY_TEST_CASE(test_run_local_identical_exchange);
    PHY_TEST_CASE(test_noncommuting_barrier_blocks_exchange);
    PHY_TEST_CASE(test_noncommuting_factors_keep_relative_order);
    PHY_TEST_CASE(test_dummy_alpha_renaming_and_metric_zero);
    PHY_TEST_CASE(test_metric_type_controls_dummy_orientation);
    PHY_TEST_CASE(test_xperm_rank_six_oracle_and_work_limit);
    PHY_TEST_CASE(test_sympy_tensor_can_zero_oracle);
    PHY_TEST_CASE(test_symmetric_rank_nine_is_pruned_not_enumerated);
    PHY_TEST_CASE(test_young_row_and_column_projectors);
    PHY_TEST_CASE(test_young_collection_hook_and_typed_validation);
    PHY_TEST_CASE(test_expression_algebra_collects_and_preserves_signature);
    PHY_TEST_CASE(test_transactional_head_declaration);
    return PHY_TEST_REPORT("abstract_tensor");
}
