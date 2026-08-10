/*
 * The GR slice of the abstract/component bridge, checked against the legacy
 * curvature pipeline it lifts.
 *
 * Every value assertion here is a *parity* assertion: an abstract expression
 * evaluated through the bridge against the dense tensor phy_gr_compute()
 * produced, compared by the exact CAS. Nothing transcribes a curvature value
 * into C, so a test passing here means the two paths agree, not that they
 * agree with a number someone typed.
 *
 * Three identities carry the weight, because each one fails for a different
 * reason:
 *
 *   R^a_bad  == Ricci_bd            one factor, one dummy: the contraction
 *                                   the legacy pipeline performs internally;
 *   g^bd R_bd == R                  two factors, two dummies, mixed valence;
 *   R_abcd R^abcd == Kretschmann    two factors, four dummies over the full
 *                                   Riemann slot group at both valences.
 *
 * The Einstein tensor is checked as a two-term abstract *expression* rather
 * than a monomial, since G_ab = R_ab - g_ab R/2 is a sum and the expression
 * layer is what a reader would actually write.
 */
#include "phy/gr_bridge.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_abstract_context *abstract;
    phy_chart *chart;
    phy_tensor *metric;
    phy_gr_result *result;
    phy_gr_component_view *view;
    unsigned dimension;
} gr_fixture;

static phy_ir_ref read_expr(phy_ir_context *ir, const char *text)
{
    phy_ir_ref expression = PHY_IR_NULL;
    size_t offset = 0u;
    PHY_CHECK_EQ_INT(phy_ir_read(ir, text, &expression, &offset), PHY_OK);
    return expression;
}

static void expect_zero(phy_cas *cas, phy_ir_ref value)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_cas_is_zero(cas, value, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static void expect_equivalent(phy_cas *cas, phy_ir_ref actual,
                              phy_ir_ref expected)
{
    phy_ir_ref difference = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_sub(cas, actual, expected, &difference), PHY_OK);
    expect_zero(cas, difference);
}

static gr_fixture fixture_open(const char *const *coordinates,
                               unsigned dimension,
                               const char *const *entries, unsigned options)
{
    gr_fixture f = {0};
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    f.dimension = dimension;
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(f.cas, NULL, &f.abstract), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_chart_create(f.ir, coordinates, dimension, &f.chart), PHY_OK);

    static const phy_ir_variance lower[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    PHY_CHECK_EQ_INT(
        phy_tensor_create(f.chart, "g", 2u, lower, &f.metric), PHY_OK);
    for (unsigned row = 0u; row < dimension; ++row) {
        for (unsigned column = 0u; column < dimension; ++column) {
            const unsigned indices[2] = {row, column};
            PHY_CHECK_EQ_INT(
                phy_tensor_set(
                    f.metric, indices,
                    read_expr(f.ir, entries[row * dimension + column])),
                PHY_OK);
        }
    }
    PHY_CHECK_EQ_INT(phy_gr_compute(f.cas, f.metric, &f.result), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_gr_component_view_create(f.cas, f.abstract, f.result, "M",
                                     options, NULL, &f.view),
        PHY_OK);
    PHY_CHECK(f.view != NULL);
    return f;
}

static void fixture_close(gr_fixture *f)
{
    phy_gr_component_view_destroy(f->view);
    phy_gr_result_destroy(f->result);
    phy_tensor_destroy(f->metric);
    phy_chart_destroy(f->chart);
    phy_abstract_context_destroy(f->abstract);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
    phy_platform_shutdown();
}

static phy_abstract_index bridge_index(const gr_fixture *f, const char *name,
                                       phy_ir_variance variance)
{
    phy_abstract_index index = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(phy_gr_component_view_space(f->view), name,
                                variance, &index),
        PHY_OK);
    return index;
}

static phy_tensor_monomial *bridge_monomial(
    const gr_fixture *f, const phy_abstract_factor *factors,
    size_t factor_count)
{
    phy_ir_ref one = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f->cas, 1, 1, &one), PHY_OK);
    phy_tensor_monomial *monomial = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(f->abstract, one, factors, factor_count,
                                   &monomial),
        PHY_OK);
    return monomial;
}

static phy_component_binding *bridge_binding(const gr_fixture *f)
{
    phy_component_binding *binding = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(f->abstract, NULL, &binding), PHY_OK);
    PHY_CHECK_EQ_INT(phy_gr_component_view_bind(f->view, binding), PHY_OK);
    return binding;
}

static phy_ir_ref legacy_component(const gr_fixture *f,
                                   const phy_tensor *tensor,
                                   const unsigned *indices)
{
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_component_expression(f->cas, tensor, indices, &value),
        PHY_OK);
    return value;
}

/* ---------------------------------------------------------------- shapes */

/*
 * The heads carry the textbook slot group, not the one the legacy tensor
 * happened to keep. Lowering and contracting drop every declaration, so
 * Riemann_abcd and Ricci_ab arrive with a trivial legacy group; the numbers
 * below are what the lift proved against every dense component.
 */
static void test_view_declares_the_textbook_slot_groups(void)
{
    static const char *const coordinates[2] = {"theta", "phi"};
    static const char *const entries[4] = {
        "(^ a 2)", "0", "0", "(* (^ a 2) (^ (fn sin theta) 2))"};
    gr_fixture f = fixture_open(coordinates, 2u, entries,
                                PHY_GR_BRIDGE_WEYL |
                                    PHY_GR_BRIDGE_RIEMANN_UPPER);

    PHY_CHECK_EQ_INT((int)phy_gr_component_view_dimension(f.view), 2);
    PHY_CHECK(phy_gr_component_view_basis(f.view) != NULL);
    PHY_CHECK(phy_gr_component_view_space(f.view) != NULL);
    PHY_CHECK(phy_gr_component_view_context(f.view) == f.abstract);

    for (unsigned quantity = 0u;
         quantity < (unsigned)PHY_GR_QUANTITY_COUNT; ++quantity) {
        PHY_CHECK(phy_gr_component_view_holds(
            f.view, (phy_gr_quantity)quantity));
    }

    PHY_CHECK_EQ_INT(
        (int)phy_component_tensor_symmetry_order(
            phy_gr_component_view_tensor(f.view, PHY_GR_RIEMANN)),
        8);
    PHY_CHECK_EQ_INT(
        (int)phy_component_tensor_symmetry_order(
            phy_gr_component_view_tensor(f.view, PHY_GR_RIEMANN_UPPER)),
        8);
    PHY_CHECK_EQ_INT(
        (int)phy_component_tensor_symmetry_order(
            phy_gr_component_view_tensor(f.view, PHY_GR_RIEMANN_MIXED)),
        2);
    PHY_CHECK_EQ_INT(
        (int)phy_component_tensor_symmetry_order(
            phy_gr_component_view_tensor(f.view, PHY_GR_RICCI)),
        2);
    PHY_CHECK_EQ_INT(
        (int)phy_component_tensor_symmetry_order(
            phy_gr_component_view_tensor(f.view, PHY_GR_CHRISTOFFEL)),
        2);

    /* A stored orbit representative per class, never the dense table. */
    PHY_CHECK(phy_component_tensor_entry_count(
                  phy_gr_component_view_tensor(f.view, PHY_GR_RIEMANN)) < 16u);
    PHY_CHECK(phy_component_tensor_entry_count(
                  phy_gr_component_view_tensor(f.view, PHY_GR_RICCI)) < 4u);

    /* The slot spaces are the view's own space, at the documented valence. */
    for (unsigned quantity = 0u;
         quantity < (unsigned)PHY_GR_QUANTITY_COUNT; ++quantity) {
        const phy_gr_quantity id = (phy_gr_quantity)quantity;
        const phy_abstract_tensor_head *head =
            phy_gr_component_view_head(f.view, id);
        phy_component_tensor *tensor =
            phy_gr_component_view_tensor(f.view, id);
        const size_t rank = phy_gr_quantity_rank(id);
        phy_ir_variance valence[4];
        PHY_CHECK(head != NULL);
        PHY_CHECK_EQ_INT((int)phy_tensor_head_slot_count(head), (int)rank);
        const bool expects_young =
            id == PHY_GR_RIEMANN || id == PHY_GR_WEYL ||
            id == PHY_GR_RIEMANN_UPPER;
        PHY_CHECK_EQ_INT(
            phy_tensor_head_has_young_symmetry(head), expects_young);
        if (expects_young) {
            phy_young_tableau_info young = {0};
            PHY_CHECK_EQ_INT(
                phy_tensor_head_young_symmetry(head, NULL, &young),
                PHY_OK);
            PHY_CHECK_EQ_INT(young.hook_product, 12);
        }
        PHY_CHECK_EQ_INT(phy_gr_quantity_valence(id, valence), PHY_OK);
        for (size_t slot = 0u; slot < rank; ++slot) {
            PHY_CHECK(phy_tensor_head_slot_space(head, slot) ==
                      phy_gr_component_view_space(f.view));
            PHY_CHECK(phy_component_tensor_basis(tensor, slot) ==
                      phy_gr_component_view_basis(f.view));
            PHY_CHECK_EQ_INT(
                (int)phy_component_tensor_valence(tensor, slot),
                (int)valence[slot]);
        }
    }

    fixture_close(&f);
}

/* --------------------------------------------------------------- parity */

/* R^a_bad == Ricci_bd, over every free pair. */
static void check_ricci_from_mixed_riemann(const gr_fixture *f)
{
    const phy_abstract_index indices[4] = {
        bridge_index(f, "a", PHY_IR_INDEX_UPPER),
        bridge_index(f, "b", PHY_IR_INDEX_LOWER),
        bridge_index(f, "a", PHY_IR_INDEX_LOWER),
        bridge_index(f, "d", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor factor = {
        phy_gr_component_view_head(f->view, PHY_GR_RIEMANN_MIXED), indices,
        4u};
    phy_tensor_monomial *monomial = bridge_monomial(f, &factor, 1u);
    PHY_CHECK_EQ_INT((int)phy_tensor_monomial_free_count(monomial), 2);
    PHY_CHECK_EQ_INT((int)phy_tensor_monomial_dummy_count(monomial), 1);

    phy_component_binding *binding = bridge_binding(f);
    const phy_tensor *ricci = phy_gr_ricci(f->result);
    for (unsigned b = 0u; b < f->dimension; ++b) {
        for (unsigned d = 0u; d < f->dimension; ++d) {
            const uint32_t free_indices[2] = {b, d};
            const unsigned legacy_indices[2] = {b, d};
            phy_ir_ref value = PHY_IR_NULL;
            phy_bridge_stats stats = {0};
            PHY_CHECK_EQ_INT(
                phy_component_value_monomial(binding, monomial, free_indices,
                                             2u, &value, &stats),
                PHY_OK);
            PHY_CHECK_EQ_INT((int)stats.free_count, 2);
            PHY_CHECK_EQ_INT((int)stats.dummy_count, 1);
            expect_equivalent(f->cas, value,
                              legacy_component(f, ricci, legacy_indices));
        }
    }
    phy_component_binding_destroy(binding);
    phy_tensor_monomial_destroy(monomial);
}

/* g^bd R_bd == the pipeline's scalar curvature. */
static void check_scalar_curvature(const gr_fixture *f)
{
    const phy_abstract_index inverse_indices[2] = {
        bridge_index(f, "b", PHY_IR_INDEX_UPPER),
        bridge_index(f, "d", PHY_IR_INDEX_UPPER)};
    const phy_abstract_index ricci_indices[2] = {
        bridge_index(f, "b", PHY_IR_INDEX_LOWER),
        bridge_index(f, "d", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor factors[2] = {
        {phy_gr_component_view_head(f->view, PHY_GR_INVERSE_METRIC),
         inverse_indices, 2u},
        {phy_gr_component_view_head(f->view, PHY_GR_RICCI), ricci_indices,
         2u}};
    phy_tensor_monomial *monomial = bridge_monomial(f, factors, 2u);
    PHY_CHECK_EQ_INT((int)phy_tensor_monomial_free_count(monomial), 0);
    PHY_CHECK_EQ_INT((int)phy_tensor_monomial_dummy_count(monomial), 2);

    phy_component_binding *binding = bridge_binding(f);
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(binding, monomial, NULL, 0u, &value,
                                     NULL),
        PHY_OK);
    expect_equivalent(f->cas, value, phy_gr_scalar_curvature(f->result));
    phy_component_binding_destroy(binding);
    phy_tensor_monomial_destroy(monomial);
}

/* R_abcd R^abcd == the pipeline's Kretschmann invariant. */
static void check_kretschmann(const gr_fixture *f)
{
    const phy_abstract_index lower[4] = {
        bridge_index(f, "a", PHY_IR_INDEX_LOWER),
        bridge_index(f, "b", PHY_IR_INDEX_LOWER),
        bridge_index(f, "c", PHY_IR_INDEX_LOWER),
        bridge_index(f, "d", PHY_IR_INDEX_LOWER)};
    const phy_abstract_index upper[4] = {
        bridge_index(f, "a", PHY_IR_INDEX_UPPER),
        bridge_index(f, "b", PHY_IR_INDEX_UPPER),
        bridge_index(f, "c", PHY_IR_INDEX_UPPER),
        bridge_index(f, "d", PHY_IR_INDEX_UPPER)};
    const phy_abstract_factor factors[2] = {
        {phy_gr_component_view_head(f->view, PHY_GR_RIEMANN), lower, 4u},
        {phy_gr_component_view_head(f->view, PHY_GR_RIEMANN_UPPER), upper,
         4u}};
    phy_tensor_monomial *monomial = bridge_monomial(f, factors, 2u);
    PHY_CHECK_EQ_INT((int)phy_tensor_monomial_dummy_count(monomial), 4);

    phy_component_binding *binding = bridge_binding(f);
    phy_ir_ref value = PHY_IR_NULL;
    phy_bridge_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(binding, monomial, NULL, 0u, &value,
                                     &stats),
        PHY_OK);
    PHY_CHECK(stats.terms > 0u);

    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_gr_kretschmann(f->cas, f->result, &expected), PHY_OK);
    expect_equivalent(f->cas, value, expected);
    phy_component_binding_destroy(binding);
    phy_tensor_monomial_destroy(monomial);
}

/* G_ab == R_ab - g_ab R/2, evaluated as a two-term abstract expression. */
static void check_einstein_expression(const gr_fixture *f)
{
    const phy_abstract_index ricci_indices[2] = {
        bridge_index(f, "a", PHY_IR_INDEX_LOWER),
        bridge_index(f, "b", PHY_IR_INDEX_LOWER)};
    const phy_abstract_index metric_indices[2] = {
        bridge_index(f, "a", PHY_IR_INDEX_LOWER),
        bridge_index(f, "b", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor ricci_factor = {
        phy_gr_component_view_head(f->view, PHY_GR_RICCI), ricci_indices,
        2u};
    const phy_abstract_factor metric_factor = {
        phy_gr_component_view_head(f->view, PHY_GR_METRIC), metric_indices,
        2u};
    phy_tensor_monomial *ricci_term = bridge_monomial(f, &ricci_factor, 1u);
    phy_tensor_monomial *metric_term = bridge_monomial(f, &metric_factor, 1u);

    phy_ir_ref half = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f->cas, -1, 2, &half), PHY_OK);
    phy_ir_ref coefficient = PHY_IR_NULL;
    const phy_ir_ref factors[2] = {half, phy_gr_scalar_curvature(f->result)};
    PHY_CHECK_EQ_INT(phy_cas_mul(f->cas, factors, 2u, &coefficient), PHY_OK);

    phy_tensor_expression *ricci_expression = NULL;
    phy_tensor_expression *metric_expression = NULL;
    phy_tensor_expression *trace_term = NULL;
    phy_tensor_expression *einstein = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(ricci_term, NULL,
                                            &ricci_expression),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_from_monomial(metric_term, NULL,
                                            &metric_expression),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_scale(metric_expression, coefficient, NULL,
                                    &trace_term),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_add(ricci_expression, trace_term, NULL,
                                  &einstein),
        PHY_OK);

    phy_component_binding *binding = bridge_binding(f);
    const phy_tensor *legacy = phy_gr_einstein(f->result);
    for (unsigned a = 0u; a < f->dimension; ++a) {
        for (unsigned b = 0u; b < f->dimension; ++b) {
            const uint32_t free_indices[2] = {a, b};
            const unsigned legacy_indices[2] = {a, b};
            phy_ir_ref value = PHY_IR_NULL;
            PHY_CHECK_EQ_INT(
                phy_component_value_expression(binding, einstein,
                                               free_indices, 2u, &value,
                                               NULL),
                PHY_OK);
            expect_equivalent(f->cas, value,
                              legacy_component(f, legacy, legacy_indices));
        }
    }

    phy_component_binding_destroy(binding);
    phy_tensor_expression_destroy(einstein);
    phy_tensor_expression_destroy(trace_term);
    phy_tensor_expression_destroy(metric_expression);
    phy_tensor_expression_destroy(ricci_expression);
    phy_tensor_monomial_destroy(metric_term);
    phy_tensor_monomial_destroy(ricci_term);
}

/* Every dense component read back through a monomial with only free slots. */
static void check_dense_readback(const gr_fixture *f)
{
    const phy_abstract_index indices[4] = {
        bridge_index(f, "a", PHY_IR_INDEX_LOWER),
        bridge_index(f, "b", PHY_IR_INDEX_LOWER),
        bridge_index(f, "c", PHY_IR_INDEX_LOWER),
        bridge_index(f, "d", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor factor = {
        phy_gr_component_view_head(f->view, PHY_GR_RIEMANN), indices, 4u};
    phy_tensor_monomial *monomial = bridge_monomial(f, &factor, 1u);
    phy_component_binding *binding = bridge_binding(f);
    const phy_tensor *legacy = phy_gr_riemann_covariant(f->result);
    unsigned legacy_indices[4] = {0u};
    const size_t count = phy_tensor_component_count(legacy);
    for (size_t flat = 0u; flat < count; ++flat) {
        PHY_CHECK_EQ_INT(
            phy_tensor_unflatten(legacy, flat, legacy_indices), PHY_OK);
        uint32_t free_indices[4];
        for (size_t slot = 0u; slot < 4u; ++slot) {
            free_indices[slot] = (uint32_t)legacy_indices[slot];
        }
        phy_ir_ref value = PHY_IR_NULL;
        phy_bridge_stats stats = {0};
        PHY_CHECK_EQ_INT(
            phy_component_value_monomial(binding, monomial, free_indices, 4u,
                                         &value, &stats),
            PHY_OK);
        PHY_CHECK_EQ_INT((int)stats.dummy_count, 0);
        expect_equivalent(f->cas, value,
                          legacy_component(f, legacy, legacy_indices));
    }
    phy_component_binding_destroy(binding);
    phy_tensor_monomial_destroy(monomial);
}

static void test_round_two_sphere_parity(void)
{
    static const char *const coordinates[2] = {"theta", "phi"};
    static const char *const entries[4] = {
        "(^ a 2)", "0", "0", "(* (^ a 2) (^ (fn sin theta) 2))"};
    gr_fixture f = fixture_open(coordinates, 2u, entries,
                                PHY_GR_BRIDGE_WEYL |
                                    PHY_GR_BRIDGE_RIEMANN_UPPER);

    check_ricci_from_mixed_riemann(&f);
    check_scalar_curvature(&f);
    check_kretschmann(&f);
    check_einstein_expression(&f);
    check_dense_readback(&f);

    /* Conformal curvature vanishes identically below three dimensions. */
    phy_component_tensor *weyl =
        phy_gr_component_view_tensor(f.view, PHY_GR_WEYL);
    for (uint32_t a = 0u; a < 2u; ++a) {
        for (uint32_t b = 0u; b < 2u; ++b) {
            const uint32_t indices[4] = {a, b, a, b};
            phy_ir_ref value = PHY_IR_NULL;
            PHY_CHECK_EQ_INT(
                phy_component_tensor_get(weyl, indices, &value), PHY_OK);
            expect_zero(f.cas, value);
        }
    }
    fixture_close(&f);
}

/*
 * The 2-sphere has G_ab == 0 identically, so it cannot tell a working
 * expression layer from one that returns zero. The round 3-sphere has
 * G_ab = -g_ab/a^2, which can.
 */
static void test_round_three_sphere_einstein_expression(void)
{
    static const char *const coordinates[3] = {"chi", "theta", "phi"};
    static const char *const entries[9] = {
        "(^ a 2)",
        "0",
        "0",
        "0",
        "(* (^ a 2) (^ (fn sin chi) 2))",
        "0",
        "0",
        "0",
        "(* (* (^ a 2) (^ (fn sin chi) 2)) (^ (fn sin theta) 2))",
    };
    gr_fixture f = fixture_open(coordinates, 3u, entries, 0u);

    PHY_CHECK(!phy_gr_component_view_holds(f.view, PHY_GR_WEYL));
    PHY_CHECK(!phy_gr_component_view_holds(f.view, PHY_GR_RIEMANN_UPPER));
    PHY_CHECK(phy_gr_component_view_head(f.view, PHY_GR_WEYL) == NULL);
    PHY_CHECK(phy_gr_component_view_tensor(f.view, PHY_GR_WEYL) == NULL);

    check_ricci_from_mixed_riemann(&f);
    check_scalar_curvature(&f);
    check_einstein_expression(&f);

    /*
     * Both halves of the harness are shown to be able to fail. G_ab is not
     * identically zero here, so the expression check above compared something
     * against something; and the comparator rejects a value that is off by
     * one, so agreement was decided rather than assumed.
     */
    const unsigned diagonal[2] = {0u, 0u};
    phy_ir_ref g00 = legacy_component(&f, phy_gr_einstein(f.result), diagonal);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_cas_is_zero(f.cas, g00, &decision), PHY_OK);
    PHY_CHECK(decision != PHY_CAS_ZERO);

    phy_ir_ref perturbed = PHY_IR_NULL;
    const phy_ir_ref addends[2] = {g00, phy_ir_integer(f.ir, 1)};
    PHY_CHECK_EQ_INT(
        phy_cas_add(f.cas, addends, 2u, &perturbed), PHY_OK);
    phy_ir_ref difference = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f.cas, g00, perturbed, &difference), PHY_OK);
    decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_cas_is_zero(f.cas, difference, &decision), PHY_OK);
    PHY_CHECK(decision != PHY_CAS_ZERO);

    fixture_close(&f);
}

/* ------------------------------------------------------- typed failures */

static void test_invalid_space_and_variance_are_typed_and_transactional(void)
{
    static const char *const coordinates[2] = {"theta", "phi"};
    static const char *const entries[4] = {
        "(^ a 2)", "0", "0", "(* (^ a 2) (^ (fn sin theta) 2))"};
    gr_fixture f = fixture_open(coordinates, 2u, entries, 0u);

    /*
     * A foreign IndexSpace never reaches the component layer: the abstract
     * factor is rejected when the monomial is built.
     */
    phy_index_space *foreign = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(f.abstract, "Other", phy_ir_integer(f.ir, 2),
                               PHY_METRIC_SYMMETRIC, &foreign),
        PHY_OK);
    phy_abstract_index foreign_indices[2];
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(foreign, "a", PHY_IR_INDEX_LOWER,
                                &foreign_indices[0]),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(foreign, "b", PHY_IR_INDEX_LOWER,
                                &foreign_indices[1]),
        PHY_OK);
    const phy_abstract_factor foreign_factor = {
        phy_gr_component_view_head(f.view, PHY_GR_RICCI), foreign_indices,
        2u};
    phy_ir_ref one = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 1, 1, &one), PHY_OK);
    phy_tensor_monomial *rejected = (phy_tensor_monomial *)(uintptr_t)1u;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(f.abstract, one, &foreign_factor, 1u,
                                   &rejected),
        PHY_ERR_TYPE);
    PHY_CHECK(rejected == NULL);

    /*
     * A variance the realization does not carry is well formed abstractly --
     * R^a_b is a tensor -- and is rejected by the bridge, which will not
     * insert a metric to reconcile it. Nothing is returned and the statistics
     * are cleared.
     */
    const phy_abstract_index raised[2] = {
        bridge_index(&f, "a", PHY_IR_INDEX_UPPER),
        bridge_index(&f, "b", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor raised_factor = {
        phy_gr_component_view_head(f.view, PHY_GR_RICCI), raised, 2u};
    phy_tensor_monomial *mixed = bridge_monomial(&f, &raised_factor, 1u);
    phy_component_binding *binding = bridge_binding(&f);
    const uint32_t free_indices[2] = {0u, 1u};
    phy_ir_ref value = (phy_ir_ref)1;
    phy_bridge_stats stats = {1u, 1u, 1u, 1u, 1u, 1u, 1u};
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(binding, mixed, free_indices, 2u, &value,
                                     &stats),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT((int)value, (int)PHY_IR_NULL);
    PHY_CHECK_EQ_INT((int)stats.free_count, 0);
    PHY_CHECK_EQ_INT((int)stats.terms, 0);

    /* The binding is unchanged and still evaluates the correct spelling. */
    const phy_abstract_index lowered[2] = {
        bridge_index(&f, "a", PHY_IR_INDEX_LOWER),
        bridge_index(&f, "b", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor lowered_factor = {
        phy_gr_component_view_head(f.view, PHY_GR_RICCI), lowered, 2u};
    phy_tensor_monomial *plain = bridge_monomial(&f, &lowered_factor, 1u);
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(binding, plain, free_indices, 2u, &value,
                                     NULL),
        PHY_OK);
    const unsigned legacy_indices[2] = {0u, 1u};
    expect_equivalent(f.cas, value,
                      legacy_component(&f, phy_gr_ricci(f.result),
                                       legacy_indices));

    /*
     * A second basis for the same IndexSpace conflicts with the view's, and
     * the rejection leaves the binding usable.
     */
    phy_component_basis *rival = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(phy_gr_component_view_space(f.view),
                                   "rival", 2u, NULL, NULL, &rival),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_component_binding_add_basis(binding, rival),
                     PHY_ERR_ALREADY_INITIALIZED);
    PHY_CHECK_EQ_INT(
        (int)phy_component_binding_basis_count(binding), 1);
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(binding, plain, free_indices, 2u, &value,
                                     NULL),
        PHY_OK);
    /* Re-binding the same view is idempotent. */
    PHY_CHECK_EQ_INT(phy_gr_component_view_bind(f.view, binding), PHY_OK);
    PHY_CHECK_EQ_INT((int)phy_component_binding_basis_count(binding), 1);
    PHY_CHECK_EQ_INT((int)phy_component_binding_tensor_count(binding), 7);

    phy_component_basis_destroy(rival);
    phy_component_binding_destroy(binding);
    phy_tensor_monomial_destroy(plain);
    phy_tensor_monomial_destroy(mixed);
    fixture_close(&f);
}

static void test_view_creation_rejects_mismatched_arguments(void)
{
    static const char *const coordinates[2] = {"theta", "phi"};
    static const char *const entries[4] = {
        "(^ a 2)", "0", "0", "(* (^ a 2) (^ (fn sin theta) 2))"};
    gr_fixture f = fixture_open(coordinates, 2u, entries, 0u);

    phy_gr_component_view *view = (phy_gr_component_view *)(uintptr_t)1u;
    PHY_CHECK_EQ_INT(
        phy_gr_component_view_create(NULL, f.abstract, f.result, "M", 0u,
                                     NULL, &view),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(view == NULL);

    view = (phy_gr_component_view *)(uintptr_t)1u;
    PHY_CHECK_EQ_INT(
        phy_gr_component_view_create(f.cas, f.abstract, f.result, "M", 0x40u,
                                     NULL, &view),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(view == NULL);

    /* An abstract context over a different CAS is not this result's. */
    phy_cas *other_cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(other_cas != NULL);
    phy_abstract_context *other = NULL;
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(other_cas, NULL, &other), PHY_OK);
    view = (phy_gr_component_view *)(uintptr_t)1u;
    PHY_CHECK_EQ_INT(
        phy_gr_component_view_create(f.cas, other, f.result, "M", 0u, NULL,
                                     &view),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(view == NULL);
    PHY_CHECK_EQ_INT((int)phy_abstract_head_count(other), 0);
    PHY_CHECK_EQ_INT((int)phy_abstract_space_count(other), 0);

    /* A byte ceiling below the view's own metadata fails before declaring. */
    phy_gr_bridge_limits limits;
    phy_gr_bridge_limits_defaults(&limits);
    limits.max_bytes = 1u;
    const size_t heads_before = phy_abstract_head_count(f.abstract);
    view = (phy_gr_component_view *)(uintptr_t)1u;
    PHY_CHECK_EQ_INT(
        phy_gr_component_view_create(f.cas, f.abstract, f.result, "M", 0u,
                                     &limits, &view),
        PHY_ERR_MEMORY_LIMIT);
    PHY_CHECK(view == NULL);
    PHY_CHECK_EQ_INT((int)phy_abstract_head_count(f.abstract),
                     (int)heads_before);

    PHY_CHECK(phy_gr_quantity_name(PHY_GR_RICCI) != NULL);
    PHY_CHECK(phy_gr_quantity_name(PHY_GR_QUANTITY_COUNT) == NULL);
    PHY_CHECK_EQ_INT((int)phy_gr_quantity_rank(PHY_GR_QUANTITY_COUNT), 0);
    PHY_CHECK_EQ_INT(
        phy_gr_quantity_valence(PHY_GR_QUANTITY_COUNT, NULL),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(!phy_gr_component_view_holds(NULL, PHY_GR_RICCI));

    phy_abstract_context_destroy(other);
    phy_cas_destroy(other_cas);
    fixture_close(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_view_declares_the_textbook_slot_groups);
    PHY_TEST_CASE(test_round_two_sphere_parity);
    PHY_TEST_CASE(test_round_three_sphere_einstein_expression);
    PHY_TEST_CASE(test_invalid_space_and_variance_are_typed_and_transactional);
    PHY_TEST_CASE(test_view_creation_rejects_mismatched_arguments);
    return PHY_TEST_REPORT("gr_bridge");
}
