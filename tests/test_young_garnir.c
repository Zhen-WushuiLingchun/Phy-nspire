/*
 * General Young/Garnir and automatic first-Bianchi acceptance tests.
 *
 * These tests keep monoterm slot symmetry and multi-term Young relations
 * separate. The former leaves 21 algebraic Riemann components in four
 * dimensions; the (2,2) Young module leaves 20.
 */
#include "phy/abstract_tensor.h"
#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy/tensor.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_abstract_context *abstract;
    phy_index_space *space;
    phy_abstract_tensor_head *riemann;
    phy_young_tableau tableau;
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
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f.abstract, "M", phy_ir_integer(f.ir, 4),
            PHY_METRIC_SYMMETRIC, &f.space),
        PHY_OK);

    const phy_index_space *slots[4] = {
        f.space, f.space, f.space, f.space};
    static const uint16_t swap_first[] = {1u, 0u, 2u, 3u};
    static const uint16_t swap_second[] = {0u, 1u, 3u, 2u};
    static const uint16_t exchange_pairs[] = {2u, 3u, 0u, 1u};
    const uint16_t *images[3] = {
        swap_first, swap_second, exchange_pairs};
    static const int signs[3] = {-1, -1, 1};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create_with_symmetries(
            f.abstract, "R", slots, 4u, PHY_TENSOR_COMMUTING,
            images, signs, 3u, &f.riemann),
        PHY_OK);

    static const uint16_t tableau_slots[4] = {0u, 2u, 1u, 3u};
    static const uint16_t row_lengths[2] = {2u, 2u};
    f.tableau.slots = tableau_slots;
    f.tableau.slot_count = 4u;
    f.tableau.row_lengths = row_lengths;
    f.tableau.row_count = 2u;
    f.tableau.order = PHY_YOUNG_ROW_SYMMETRY_LAST;
    return f;
}

static void fixture_close(fixture *f)
{
    phy_abstract_context_destroy(f->abstract);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
    phy_platform_shutdown();
}

static phy_tensor_monomial *riemann_term(
    fixture *f, const size_t order[4], int64_t coefficient)
{
    static const char *const names[4] = {"a", "b", "c", "d"};
    phy_abstract_index named[4] = {{0}};
    for (size_t which = 0u; which < 4u; ++which) {
        PHY_CHECK_EQ_INT(
            phy_abstract_index_make(
                f->space, names[which], PHY_IR_INDEX_LOWER,
                &named[which]),
            PHY_OK);
    }
    phy_abstract_index indices[4] = {{0}};
    for (size_t slot = 0u; slot < 4u; ++slot) {
        indices[slot] = named[order[slot]];
    }
    const phy_abstract_factor factor = {f->riemann, indices, 4u};
    phy_tensor_monomial *monomial = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f->abstract, phy_ir_integer(f->ir, coefficient),
            &factor, 1u, &monomial),
        PHY_OK);
    return monomial;
}

static void check_same_monomial(
    fixture *f, const phy_tensor_monomial *left,
    const phy_tensor_monomial *right)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f->cas, phy_tensor_monomial_coefficient(left),
            phy_tensor_monomial_coefficient(right), &decision),
        PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
    const size_t count = phy_tensor_monomial_factor_count(left);
    PHY_CHECK_EQ_INT(phy_tensor_monomial_factor_count(right), count);
    for (size_t factor = 0u; factor < count; ++factor) {
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
            PHY_CHECK(left_indices[slot].space == right_indices[slot].space);
            PHY_CHECK_EQ_INT(left_indices[slot].name,
                             right_indices[slot].name);
            PHY_CHECK_EQ_INT(left_indices[slot].variance,
                             right_indices[slot].variance);
        }
    }
}

static void check_same_expression(
    fixture *f, const phy_tensor_expression *left,
    const phy_tensor_expression *right)
{
    const size_t count = phy_tensor_expression_term_count(left);
    PHY_CHECK_EQ_INT(phy_tensor_expression_term_count(right), count);
    PHY_CHECK_EQ_INT(phy_tensor_expression_free_count(left),
                     phy_tensor_expression_free_count(right));
    for (size_t term = 0u; term < count; ++term) {
        check_same_monomial(
            f, phy_tensor_expression_term(left, term),
            phy_tensor_expression_term(right, term));
    }
}

static phy_tensor_expression *expression_add(
    const phy_tensor_expression *left, const phy_tensor_expression *right)
{
    phy_tensor_expression *sum = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_add(left, right, NULL, &sum), PHY_OK);
    return sum;
}

static void test_tableau_combinatorics_and_component_count(void)
{
    fixture f = fixture_open();
    phy_young_tableau_info info = {0};
    PHY_CHECK_EQ_INT(
        phy_young_tableau_validate(&f.tableau, &info), PHY_OK);
    PHY_CHECK_EQ_INT(info.slot_count, 4);
    PHY_CHECK_EQ_INT(info.row_count, 2);
    PHY_CHECK_EQ_INT(info.column_count, 2);
    PHY_CHECK_EQ_INT(info.row_group_order, 4);
    PHY_CHECK_EQ_INT(info.column_group_order, 4);
    PHY_CHECK_EQ_INT(info.hook_product, 12);
    PHY_CHECK_EQ_INT(info.standard_tableau_count, 2);
    PHY_CHECK(info.standard);

    uint16_t columns[2] = {0};
    size_t column_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_young_tableau_column_lengths(
            &f.tableau, columns, 2u, &column_count),
        PHY_OK);
    PHY_CHECK_EQ_INT(column_count, 2);
    PHY_CHECK_EQ_INT(columns[0], 2);
    PHY_CHECK_EQ_INT(columns[1], 2);

    uint16_t standard[8] = {0};
    size_t standard_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_young_standard_tableaux(
            f.tableau.row_lengths, f.tableau.row_count,
            standard, 8u, &standard_count),
        PHY_OK);
    PHY_CHECK_EQ_INT(standard_count, 2);

    uint64_t young_components = 0u;
    PHY_CHECK_EQ_INT(
        phy_young_gl_dimension(&f.tableau, 4u, &young_components),
        PHY_OK);
    PHY_CHECK_EQ_INT(young_components, 20);

    static const char *const coordinates[4] = {"x0", "x1", "x2", "x3"};
    phy_chart *chart = NULL;
    PHY_CHECK_EQ_INT(
        phy_chart_create(f.ir, coordinates, 4u, &chart), PHY_OK);
    static const phy_ir_variance lower[4] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_tensor *components = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_create(chart, "Rc", 4u, lower, &components), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_declare_riemann_symmetry(components), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_independent_count(components), 21);

    phy_tensor_destroy(components);
    phy_chart_destroy(chart);
    fixture_close(&f);
}

static void test_riemann_projector_is_idempotent(void)
{
    fixture f = fixture_open();
    PHY_CHECK_EQ_INT(
        phy_tensor_head_set_young_symmetry(
            f.riemann, &f.tableau, NULL),
        PHY_OK);
    PHY_CHECK(phy_tensor_head_has_young_symmetry(f.riemann));

    static const size_t identity[4] = {0u, 1u, 2u, 3u};
    phy_tensor_monomial *input = riemann_term(&f, identity, 1);
    phy_tensor_expression *once = NULL;
    phy_young_stats once_stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            input, 0u, &f.tableau, NULL, &once, &once_stats),
        PHY_OK);
    PHY_CHECK_EQ_INT(once_stats.row_group_order, 4);
    PHY_CHECK_EQ_INT(once_stats.column_group_order, 4);
    PHY_CHECK_EQ_INT(once_stats.hook_product, 12);
    PHY_CHECK_EQ_INT(once_stats.generated_terms, 16);

    phy_tensor_expression *twice = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_young_project(
            once, 0u, &f.tableau, NULL, &twice, NULL),
        PHY_OK);
    check_same_expression(&f, once, twice);

    static const uint16_t identity_image[4] = {0u, 1u, 2u, 3u};
    const size_t before = phy_tensor_head_symmetry_count(f.riemann);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(
            f.riemann, identity_image, -1),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_tensor_head_symmetry_count(f.riemann), before);

    phy_tensor_expression_destroy(twice);
    phy_tensor_expression_destroy(once);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_general_shape_projectors_and_dimension(void)
{
    fixture f = fixture_open();
    static const uint16_t slots[3] = {0u, 1u, 2u};
    static const uint16_t rows[2] = {2u, 1u};
    phy_young_tableau tableau = {
        slots, 3u, rows, 2u, PHY_YOUNG_ROW_SYMMETRY_LAST};
    phy_young_tableau_info info = {0};
    PHY_CHECK_EQ_INT(
        phy_young_tableau_validate(&tableau, &info), PHY_OK);
    PHY_CHECK_EQ_INT(info.row_group_order, 2);
    PHY_CHECK_EQ_INT(info.column_group_order, 2);
    PHY_CHECK_EQ_INT(info.hook_product, 3);
    PHY_CHECK_EQ_INT(info.standard_tableau_count, 2);
    uint64_t dimension = 0u;
    PHY_CHECK_EQ_INT(
        phy_young_gl_dimension(&tableau, 3u, &dimension), PHY_OK);
    PHY_CHECK_EQ_INT(dimension, 8);
    PHY_CHECK_EQ_INT(
        phy_young_gl_dimension(&tableau, 1u, &dimension), PHY_OK);
    PHY_CHECK_EQ_INT(dimension, 0);

    uint16_t standard[6] = {0};
    size_t standard_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_young_standard_tableaux(
            rows, 2u, standard, 6u, &standard_count),
        PHY_OK);
    PHY_CHECK_EQ_INT(standard_count, 2);
    uint16_t short_buffer[5] = {
        UINT16_C(0x55aa), UINT16_C(0x55aa), UINT16_C(0x55aa),
        UINT16_C(0x55aa), UINT16_C(0x55aa)};
    PHY_CHECK_EQ_INT(
        phy_young_standard_tableaux(
            rows, 2u, short_buffer, 5u, &standard_count),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(standard_count, 0);
    for (size_t entry = 0u; entry < 5u; ++entry) {
        PHY_CHECK_EQ_INT(short_buffer[entry], UINT16_C(0x55aa));
    }
    static const uint16_t too_many_rows[2] = {10u, 10u};
    PHY_CHECK_EQ_INT(
        phy_young_standard_tableaux(
            too_many_rows, 2u, NULL, 0u, &standard_count),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(standard_count, 0);

    const phy_index_space *head_slots[3] = {
        f.space, f.space, f.space};
    phy_abstract_tensor_head *head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "U", head_slots, 3u,
            PHY_TENSOR_COMMUTING, &head),
        PHY_OK);
    static const char *const names[3] = {"i", "j", "k"};
    phy_abstract_index indices[3] = {{0}};
    for (size_t slot = 0u; slot < 3u; ++slot) {
        PHY_CHECK_EQ_INT(
            phy_abstract_index_make(
                f.space, names[slot], PHY_IR_INDEX_LOWER,
                &indices[slot]),
            PHY_OK);
    }
    const phy_abstract_factor factor = {head, indices, 3u};
    phy_tensor_monomial *input = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1), &factor, 1u,
            &input),
        PHY_OK);

    for (size_t order = 0u; order < 2u; ++order) {
        tableau.order = (phy_young_order)order;
        phy_tensor_expression *once = NULL;
        phy_tensor_expression *twice = NULL;
        PHY_CHECK_EQ_INT(
            phy_tensor_monomial_young_project(
                input, 0u, &tableau, NULL, &once, NULL),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_tensor_expression_young_project(
                once, 0u, &tableau, NULL, &twice, NULL),
            PHY_OK);
        check_same_expression(&f, once, twice);
        phy_tensor_expression_destroy(twice);
        phy_tensor_expression_destroy(once);
    }

    tableau.order = PHY_YOUNG_ROW_SYMMETRY_LAST;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_set_young_symmetry(head, &tableau, NULL),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_young_garnir_count(&tableau), 1);
    phy_tensor_expression *relation = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_garnir_relation(
            input, 0u, 0u, NULL, &relation, NULL),
        PHY_OK);
    phy_tensor_expression *reduced = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_young_reduce(
            relation, NULL, &reduced, NULL),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_expression_term_count(reduced), 0);

    phy_tensor_expression_destroy(reduced);
    phy_tensor_expression_destroy(relation);
    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

static void test_first_bianchi_and_garnir_reduce_to_zero(void)
{
    fixture f = fixture_open();
    PHY_CHECK_EQ_INT(
        phy_tensor_head_set_young_symmetry(
            f.riemann, &f.tableau, NULL),
        PHY_OK);

    static const size_t abcd[4] = {0u, 1u, 2u, 3u};
    static const size_t acdb[4] = {0u, 2u, 3u, 1u};
    static const size_t adbc[4] = {0u, 3u, 1u, 2u};
    phy_tensor_monomial *first = riemann_term(&f, abcd, 1);
    phy_tensor_monomial *second = riemann_term(&f, acdb, 1);
    phy_tensor_monomial *third = riemann_term(&f, adbc, 1);
    phy_tensor_expression *first_expression = NULL;
    phy_tensor_expression *second_expression = NULL;
    phy_tensor_expression *third_expression = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(
            first, NULL, &first_expression),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(
            second, NULL, &second_expression),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(
            third, NULL, &third_expression),
        PHY_OK);
    phy_tensor_expression *partial =
        expression_add(first_expression, second_expression);
    phy_tensor_expression *cyclic = expression_add(partial, third_expression);

    phy_tensor_expression *reduced = NULL;
    phy_young_reduce_stats reduce_stats = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_young_reduce(
            cyclic, NULL, &reduced, &reduce_stats),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_expression_term_count(reduced), 0);
    PHY_CHECK_EQ_INT(phy_tensor_expression_free_count(reduced), 4);
    PHY_CHECK(reduce_stats.projected_factors > 0u);

    PHY_CHECK_EQ_INT(phy_young_garnir_count(&f.tableau), 2);
    for (size_t relation = 0u; relation < 2u; ++relation) {
        phy_tensor_expression *garnir = NULL;
        phy_garnir_stats garnir_stats = {0};
        PHY_CHECK_EQ_INT(
            phy_tensor_monomial_garnir_relation(
                first, 0u, relation, NULL, &garnir, &garnir_stats),
            PHY_OK);
        PHY_CHECK(garnir_stats.generated_terms > 0u);
        phy_tensor_expression *garnir_reduced = NULL;
        PHY_CHECK_EQ_INT(
            phy_tensor_expression_young_reduce(
                garnir, NULL, &garnir_reduced, NULL),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_tensor_expression_term_count(garnir_reduced), 0);
        phy_tensor_expression_destroy(garnir_reduced);
        phy_tensor_expression_destroy(garnir);
    }

    phy_young_reduce_limits step_limit = {0};
    step_limit.max_steps = 1u;
    phy_tensor_expression *refused = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_young_reduce(
            first_expression, &step_limit, &refused, NULL),
        PHY_ERR_TIMEOUT);
    PHY_CHECK(refused == NULL);

    phy_young_limits term_limit = {0};
    term_limit.max_generated_terms = 15u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            first, 0u, &f.tableau, &term_limit, &refused, NULL),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK(refused == NULL);

    phy_tensor_expression_destroy(reduced);
    phy_tensor_expression_destroy(cyclic);
    phy_tensor_expression_destroy(partial);
    phy_tensor_expression_destroy(third_expression);
    phy_tensor_expression_destroy(second_expression);
    phy_tensor_expression_destroy(first_expression);
    phy_tensor_monomial_destroy(third);
    phy_tensor_monomial_destroy(second);
    phy_tensor_monomial_destroy(first);
    fixture_close(&f);
}

static void test_incompatible_declaration_is_transactional(void)
{
    fixture f = fixture_open();
    const phy_index_space *slots[4] = {
        f.space, f.space, f.space, f.space};
    phy_abstract_tensor_head *bad = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f.abstract, "Bad", slots, 4u, PHY_TENSOR_COMMUTING, &bad),
        PHY_OK);
    static const uint16_t identity[4] = {0u, 1u, 2u, 3u};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(bad, identity, -1), PHY_OK);
    const size_t bytes_before = phy_abstract_bytes_used(f.abstract);
    PHY_CHECK_EQ_INT(
        phy_tensor_head_set_young_symmetry(
            bad, &f.tableau, NULL),
        PHY_ERR_TYPE);
    PHY_CHECK(!phy_tensor_head_has_young_symmetry(bad));
    PHY_CHECK_EQ_INT(phy_abstract_bytes_used(f.abstract), bytes_before);
    fixture_close(&f);
}

static void test_projector_allocation_failures_are_transactional(void)
{
    fixture f = fixture_open();
    static const size_t identity[4] = {0u, 1u, 2u, 3u};
    phy_tensor_monomial *input = riemann_term(&f, identity, 1);

    /* Warm all reusable scalar nodes before measuring owned scratch. */
    phy_tensor_expression *warm = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            input, 0u, &f.tableau, NULL, &warm, NULL),
        PHY_OK);
    phy_tensor_expression_destroy(warm);

    const uint32_t attempts_before = phy_host_alloc_attempts();
    warm = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            input, 0u, &f.tableau, NULL, &warm, NULL),
        PHY_OK);
    phy_tensor_expression_destroy(warm);
    const uint32_t allocations =
        phy_host_alloc_attempts() - attempts_before;
    PHY_CHECK(allocations > 0u);

    phy_telemetry baseline = {0};
    phy_telemetry after = {0};
    phy_telemetry_get(&baseline);
    for (uint32_t nth = 1u; nth <= allocations; ++nth) {
        phy_host_fail_alloc_after(nth);
        phy_tensor_expression *result = NULL;
        const phy_status status =
            phy_tensor_monomial_young_project(
                input, 0u, &f.tableau, NULL, &result, NULL);
        phy_host_fail_alloc_after(0u);
        if (status == PHY_OK) {
            PHY_CHECK(result != NULL);
            phy_tensor_expression_destroy(result);
        } else {
            PHY_CHECK(
                status == PHY_ERR_MEMORY_LIMIT ||
                status == PHY_ERR_OUT_OF_MEMORY);
            PHY_CHECK(result == NULL);
        }
        phy_telemetry_get(&after);
        PHY_CHECK_EQ_INT(after.bytes_live, baseline.bytes_live);
    }

    phy_tensor_monomial_destroy(input);
    fixture_close(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_tableau_combinatorics_and_component_count);
    PHY_TEST_CASE(test_riemann_projector_is_idempotent);
    PHY_TEST_CASE(test_general_shape_projectors_and_dimension);
    PHY_TEST_CASE(test_first_bianchi_and_garnir_reduce_to_zero);
    PHY_TEST_CASE(test_incompatible_declaration_is_transactional);
    PHY_TEST_CASE(test_projector_allocation_failures_are_transactional);
    return PHY_TEST_REPORT("young_garnir");
}
