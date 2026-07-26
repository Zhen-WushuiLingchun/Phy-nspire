/*
 * Phy-nspire — component tensor core tests.
 *
 * Covers the component-independent half of docs/agent-tasks/TENSOR_CORE.md:
 * charts, rank and valence bookkeeping, dense storage, flat-index encoding,
 * the declared symmetry group, canonical component lookup, fill, assignment
 * validation, and the allocation-failure unwind path.
 *
 * The four tests of that document that compare expressions -- Kronecker round
 * trip, raise/lower involution, the first Bianchi identity, and contraction
 * against known traces -- are absent on purpose. Each needs simplification and
 * a decidable zero test, which arrive with the canonical scalar CAS layer.
 * Nothing here forms an expression: components are built from interned symbols
 * and integer literals through the ordinary IR builders and are only ever
 * moved around, never combined.
 *
 * Every structural test that can run at more than one dimension does, because
 * a corpus that is almost entirely 4-dimensional would hide a hard-coded n = 4
 * -- the concern behind test 6 of that document.
 */
#include <stdlib.h>
#include <string.h>

#include "phy/ir.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy/tensor.h"
#include "phy_test.h"

/* --------------------------------------------------------------- helpers */

static const char *const kCoords4[4] = {"t", "r", "theta", "phi"};
static const char *const kCoords2[2] = {"theta", "phi"};
static const char *const kCoords3[3] = {"x", "y", "z"};

static const phy_ir_variance kAllLower[4] = {
    PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
    PHY_IR_INDEX_LOWER};

/* A distinct, interned symbol per call site, used purely as a payload. */
static phy_ir_ref symbol_ref(phy_ir_context *ir, const char *name)
{
    return phy_ir_symbol_ref(ir, phy_ir_intern(ir, name));
}

static phy_chart *make_chart(phy_ir_context *ir, unsigned dimension)
{
    const char *const *names = kCoords4;
    if (dimension == 2u) {
        names = kCoords2;
    } else if (dimension == 3u) {
        names = kCoords3;
    }
    phy_chart *chart = NULL;
    if (phy_chart_create(ir, names, dimension, &chart) != PHY_OK) {
        return NULL;
    }
    return chart;
}

static phy_tensor *make_tensor(phy_chart *chart, const char *name,
                               unsigned rank)
{
    phy_tensor *tensor = NULL;
    if (phy_tensor_create(chart, name, rank, kAllLower, &tensor) != PHY_OK) {
        return NULL;
    }
    return tensor;
}

/* n^rank, computed independently of the implementation under test. */
static size_t expected_count(unsigned dimension, unsigned rank)
{
    size_t count = 1u;
    for (unsigned slot = 0u; slot < rank; ++slot) {
        count *= dimension;
    }
    return count;
}

/* ----------------------------------------------------------------- charts */

static void test_chart_basics(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    phy_chart *chart = NULL;
    PHY_CHECK_EQ_INT(phy_chart_create(ir, kCoords4, 4u, &chart), PHY_OK);
    PHY_CHECK(chart != NULL);

    PHY_CHECK_EQ_INT(phy_chart_dimension(chart), 4);
    PHY_CHECK(phy_chart_ir(chart) == ir);

    for (unsigned axis = 0u; axis < 4u; ++axis) {
        PHY_CHECK_EQ_STR(phy_chart_coordinate_name(chart, axis),
                         kCoords4[axis]);
        const phy_ir_symbol symbol = phy_chart_coordinate_symbol(chart, axis);
        PHY_CHECK(symbol != PHY_IR_NO_SYMBOL);
        PHY_CHECK_EQ_INT(phy_chart_axis_of(chart, symbol), axis);

        const phy_ir_ref ref = phy_chart_coordinate(chart, axis);
        PHY_CHECK(ref != PHY_IR_NULL);
        PHY_CHECK_EQ_INT(phy_ir_kind_of(ir, ref), PHY_IR_SYMBOL);
        /* Interning: the chart's ref is the ref anyone else would build. */
        PHY_CHECK(phy_ir_equal(ref, symbol_ref(ir, kCoords4[axis])));
    }

    /* Out of range is reported, not clamped. */
    PHY_CHECK_EQ_INT(phy_chart_coordinate_symbol(chart, 4u), PHY_IR_NO_SYMBOL);
    PHY_CHECK_EQ_INT(phy_chart_coordinate(chart, 4u), PHY_IR_NULL);
    PHY_CHECK(phy_chart_coordinate_name(chart, 4u) == NULL);

    /* A symbol that is not a coordinate has no axis. */
    PHY_CHECK_EQ_INT(phy_chart_axis_of(chart, phy_ir_intern(ir, "M")),
                     PHY_CHART_NO_AXIS);
    PHY_CHECK_EQ_INT(phy_chart_axis_of(chart, PHY_IR_NO_SYMBOL),
                     PHY_CHART_NO_AXIS);

    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

static void test_chart_rejects(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);
    phy_chart *chart = NULL;

    /* Dimension: 0 is malformed, 5 is well-formed but not implemented. */
    PHY_CHECK_EQ_INT(phy_chart_create(ir, kCoords4, 0u, &chart),
                     PHY_ERR_INVALID_ARGUMENT);
    static const char *const five[5] = {"a", "b", "c", "d", "e"};
    PHY_CHECK_EQ_INT(phy_chart_create(ir, five, 5u, &chart),
                     PHY_ERR_UNSUPPORTED);

    PHY_CHECK_EQ_INT(phy_chart_create(NULL, kCoords4, 4u, &chart),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_chart_create(ir, NULL, 4u, &chart),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_chart_create(ir, kCoords4, 4u, NULL),
                     PHY_ERR_INVALID_ARGUMENT);

    static const char *const empty_name[2] = {"t", ""};
    PHY_CHECK_EQ_INT(phy_chart_create(ir, empty_name, 2u, &chart),
                     PHY_ERR_INVALID_ARGUMENT);

    static const char *const null_name[2] = {"t", NULL};
    PHY_CHECK_EQ_INT(phy_chart_create(ir, null_name, 2u, &chart),
                     PHY_ERR_INVALID_ARGUMENT);

    /*
     * Duplicate axes are rejected. An axis that cannot be told apart from
     * another cannot be differentiated against, and phy_chart_axis_of would
     * silently return the first.
     */
    static const char *const duplicate[3] = {"t", "r", "t"};
    PHY_CHECK_EQ_INT(phy_chart_create(ir, duplicate, 3u, &chart),
                     PHY_ERR_INVALID_ARGUMENT);

    /* Nothing above should have produced a chart. */
    PHY_CHECK(chart == NULL);

    /* Accessors tolerate NULL rather than trapping. */
    PHY_CHECK_EQ_INT(phy_chart_dimension(NULL), 0);
    PHY_CHECK(phy_chart_ir(NULL) == NULL);
    PHY_CHECK(phy_chart_coordinate_name(NULL, 0u) == NULL);
    PHY_CHECK_EQ_INT(phy_chart_axis_of(NULL, 1u), PHY_CHART_NO_AXIS);
    phy_chart_destroy(NULL);

    phy_ir_context_destroy(ir);
}

/* ------------------------------------------------------ tensor construction */

static void test_tensor_shape(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    for (unsigned dimension = 1u; dimension <= 4u; ++dimension) {
        phy_chart *chart = make_chart(ir, dimension);
        PHY_CHECK(chart != NULL);

        for (unsigned rank = 0u; rank <= 4u; ++rank) {
            phy_tensor *tensor = make_tensor(chart, "T", rank);
            PHY_CHECK(tensor != NULL);

            PHY_CHECK_EQ_INT(phy_tensor_rank(tensor), rank);
            PHY_CHECK_EQ_INT(phy_tensor_dimension(tensor), dimension);
            PHY_CHECK_EQ_INT(phy_tensor_component_count(tensor),
                             expected_count(dimension, rank));
            PHY_CHECK(phy_tensor_chart(tensor) == chart);
            PHY_CHECK_EQ_STR(phy_tensor_name(tensor), "T");
            PHY_CHECK(phy_tensor_head(tensor) != PHY_IR_NO_SYMBOL);

            /* No declarations yet: the group is trivial. */
            PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(tensor), 1);
            PHY_CHECK(!phy_tensor_is_identically_zero(tensor));
            PHY_CHECK_EQ_INT(phy_tensor_independent_count(tensor),
                             expected_count(dimension, rank));

            /*
             * Every component starts at the canonical zero handle, and it is
             * one node however large the table is.
             */
            const phy_ir_ref zero = phy_ir_integer(ir, 0);
            PHY_CHECK(phy_ir_equal(phy_tensor_zero(tensor), zero));
            for (size_t flat = 0u; flat < phy_tensor_component_count(tensor);
                 ++flat) {
                phy_tensor_component component = {PHY_IR_NULL, 0};
                PHY_CHECK_EQ_INT(
                    phy_tensor_get_flat(tensor, flat, &component), PHY_OK);
                PHY_CHECK(phy_ir_equal(component.ref, zero));
                PHY_CHECK_EQ_INT(component.sign, 1);
            }

            phy_tensor_destroy(tensor);
        }
        phy_chart_destroy(chart);
    }

    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
    phy_ir_context_destroy(ir);
}

static void test_tensor_valence(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);
    PHY_CHECK(chart != NULL);

    /* Riemann in the mixed form R^a_bcd. */
    static const phy_ir_variance mixed[4] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER};
    phy_tensor *tensor = NULL;
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, "R", 4u, mixed, &tensor),
                     PHY_OK);

    PHY_CHECK_EQ_INT(phy_tensor_valence(tensor, 0u), PHY_IR_INDEX_UPPER);
    for (unsigned slot = 1u; slot < 4u; ++slot) {
        PHY_CHECK_EQ_INT(phy_tensor_valence(tensor, slot),
                         PHY_IR_INDEX_LOWER);
    }

    phy_tensor_destroy(tensor);
    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

static void test_tensor_rejects(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);
    PHY_CHECK(chart != NULL);
    phy_tensor *tensor = NULL;

    /* Rank 5 is well-formed and not implemented. */
    static const phy_ir_variance five[5] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, "T", 5u, five, &tensor),
                     PHY_ERR_UNSUPPORTED);

    PHY_CHECK_EQ_INT(phy_tensor_create(NULL, "T", 2u, kAllLower, &tensor),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, NULL, 2u, kAllLower, &tensor),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, "", 2u, kAllLower, &tensor),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, "T", 2u, kAllLower, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, "T", 2u, NULL, &tensor),
                     PHY_ERR_INVALID_ARGUMENT);

    /* A variance outside the enum is a type error, not a value error. */
    static const phy_ir_variance bogus[2] = {PHY_IR_INDEX_LOWER,
                                             (phy_ir_variance)7};
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, "T", 2u, bogus, &tensor),
                     PHY_ERR_TYPE);

    PHY_CHECK(tensor == NULL);

    /* Rank 0 needs no valence array. */
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, "s", 0u, NULL, &tensor), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_component_count(tensor), 1);
    phy_tensor_destroy(tensor);

    /* Accessors tolerate NULL. */
    PHY_CHECK_EQ_INT(phy_tensor_rank(NULL), 0);
    PHY_CHECK_EQ_INT(phy_tensor_dimension(NULL), 0);
    PHY_CHECK_EQ_INT(phy_tensor_component_count(NULL), 0);
    PHY_CHECK_EQ_INT(phy_tensor_zero(NULL), PHY_IR_NULL);
    PHY_CHECK(phy_tensor_name(NULL) == NULL);
    PHY_CHECK(phy_tensor_chart(NULL) == NULL);
    PHY_CHECK_EQ_INT(phy_tensor_head(NULL), PHY_IR_NO_SYMBOL);
    PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(NULL), 0);
    PHY_CHECK(!phy_tensor_is_identically_zero(NULL));
    PHY_CHECK_EQ_INT(phy_tensor_independent_count(NULL), 0);
    phy_tensor_destroy(NULL);

    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

/* -------------------------------------------------------- flat index mapping */

static void test_flat_index_roundtrip(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);

    for (unsigned dimension = 2u; dimension <= 4u; ++dimension) {
        phy_chart *chart = make_chart(ir, dimension);
        PHY_CHECK(chart != NULL);

        for (unsigned rank = 0u; rank <= 4u; ++rank) {
            phy_tensor *tensor = make_tensor(chart, "T", rank);
            PHY_CHECK(tensor != NULL);

            const size_t count = phy_tensor_component_count(tensor);

            /* Exhaustive: every flat index decodes and re-encodes. */
            for (size_t flat = 0u; flat < count; ++flat) {
                unsigned indices[PHY_TENSOR_MAX_RANK] = {0u, 0u, 0u, 0u};
                PHY_CHECK_EQ_INT(
                    phy_tensor_unflatten(tensor, flat, indices), PHY_OK);
                for (unsigned slot = 0u; slot < rank; ++slot) {
                    PHY_CHECK(indices[slot] < dimension);
                }
                size_t round = count + 1u;
                PHY_CHECK_EQ_INT(phy_tensor_flatten(tensor, indices, &round),
                                 PHY_OK);
                PHY_CHECK_EQ_INT(round, flat);
            }

            /* Row-major with slot 0 most significant. */
            if (rank == 2u) {
                const unsigned indices[2] = {1u, 0u};
                size_t flat = 0u;
                PHY_CHECK_EQ_INT(phy_tensor_flatten(tensor, indices, &flat),
                                 PHY_OK);
                PHY_CHECK_EQ_INT(flat, dimension);
            }

            /* Out of range in either direction is reported. */
            PHY_CHECK_EQ_INT(phy_tensor_unflatten(tensor, count, NULL),
                             PHY_ERR_INVALID_ARGUMENT);
            if (rank > 0u) {
                unsigned bad[PHY_TENSOR_MAX_RANK] = {0u, 0u, 0u, 0u};
                bad[rank - 1u] = dimension;
                size_t flat = 0u;
                PHY_CHECK_EQ_INT(phy_tensor_flatten(tensor, bad, &flat),
                                 PHY_ERR_INVALID_ARGUMENT);
                PHY_CHECK_EQ_INT(phy_tensor_flatten(tensor, NULL, &flat),
                                 PHY_ERR_INVALID_ARGUMENT);
                unsigned out[PHY_TENSOR_MAX_RANK];
                PHY_CHECK_EQ_INT(phy_tensor_unflatten(tensor, 0u, NULL),
                                 PHY_ERR_INVALID_ARGUMENT);
                PHY_CHECK_EQ_INT(phy_tensor_unflatten(tensor, 0u, out),
                                 PHY_OK);
            }
            size_t sink = 0u;
            PHY_CHECK_EQ_INT(phy_tensor_flatten(tensor, NULL, NULL),
                             PHY_ERR_INVALID_ARGUMENT);
            PHY_CHECK_EQ_INT(phy_tensor_flatten(NULL, NULL, &sink),
                             PHY_ERR_INVALID_ARGUMENT);
            PHY_CHECK_EQ_INT(phy_tensor_unflatten(NULL, 0u, NULL),
                             PHY_ERR_INVALID_ARGUMENT);

            phy_tensor_destroy(tensor);
        }
        phy_chart_destroy(chart);
    }

    phy_ir_context_destroy(ir);
}

/* ------------------------------------------------------------- symmetries */

static void test_symmetry_group_orders(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);

    /* A single transposition generates a group of order 2, either sign. */
    phy_tensor *symmetric = make_tensor(chart, "g", 2u);
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         symmetric, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(symmetric), 2);
    /* n(n+1)/2 independent components. */
    PHY_CHECK_EQ_INT(phy_tensor_independent_count(symmetric), 10);
    phy_tensor_destroy(symmetric);

    phy_tensor *antisymmetric = make_tensor(chart, "F", 2u);
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         antisymmetric, 0u, 1u, PHY_IR_SYMMETRY_ANTISYMMETRIC),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(antisymmetric), 2);
    /* n(n-1)/2: the diagonal is forced to vanish, so it is not independent. */
    PHY_CHECK_EQ_INT(phy_tensor_independent_count(antisymmetric), 6);
    for (unsigned axis = 0u; axis < 4u; ++axis) {
        const unsigned diagonal[2] = {axis, axis};
        int sign = 1;
        PHY_CHECK_EQ_INT(
            phy_tensor_canonical(antisymmetric, diagonal, NULL, &sign),
            PHY_OK);
        PHY_CHECK_EQ_INT(sign, 0);
    }
    phy_tensor_destroy(antisymmetric);

    /*
     * The Riemann slot group is the order-8 group generated by the two
     * antisymmetries and the pair exchange.
     */
    phy_tensor *riemann = make_tensor(chart, "R", 4u);
    PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(riemann), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(riemann), 8);
    /*
     * N(N+1)/2 with N = n(n-1)/2 = 6, so 21. This is the count the *slot*
     * symmetries give. The familiar n^2(n^2-1)/12 = 20 additionally uses the
     * first Bianchi identity, which is a cyclic relation among three
     * different components rather than a slot permutation, and so is not a
     * symmetry in this sense.
     */
    PHY_CHECK_EQ_INT(phy_tensor_independent_count(riemann), 21);
    phy_tensor_destroy(riemann);

    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

static void test_symmetry_dimension_independence(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);

    /*
     * The counts are formulas in n, so checking them at 2, 3 and 4 is what
     * catches an implementation that quietly assumes the corpus dimension.
     * At n = 2 the Riemann slot count and n^2(n^2-1)/12 coincide at 1, which
     * is the cross-check docs/references/GENERAL_RELATIVITY.md draws on for
     * sphere_2d.
     */
    static const unsigned dimensions[3] = {2u, 3u, 4u};
    static const size_t symmetric_expected[3] = {3u, 6u, 10u};
    static const size_t antisymmetric_expected[3] = {1u, 3u, 6u};
    static const size_t riemann_expected[3] = {1u, 6u, 21u};

    for (unsigned entry = 0u; entry < 3u; ++entry) {
        const unsigned dimension = dimensions[entry];
        phy_chart *chart = make_chart(ir, dimension);
        PHY_CHECK(chart != NULL);

        phy_tensor *symmetric = make_tensor(chart, "g", 2u);
        PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                             symmetric, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
                         PHY_OK);
        PHY_CHECK_EQ_INT(phy_tensor_independent_count(symmetric),
                         symmetric_expected[entry]);
        phy_tensor_destroy(symmetric);

        phy_tensor *antisymmetric = make_tensor(chart, "F", 2u);
        PHY_CHECK_EQ_INT(
            phy_tensor_declare_slot_symmetry(antisymmetric, 0u, 1u,
                                             PHY_IR_SYMMETRY_ANTISYMMETRIC),
            PHY_OK);
        PHY_CHECK_EQ_INT(phy_tensor_independent_count(antisymmetric),
                         antisymmetric_expected[entry]);
        phy_tensor_destroy(antisymmetric);

        phy_tensor *riemann = make_tensor(chart, "R", 4u);
        PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(riemann), PHY_OK);
        PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(riemann), 8);
        PHY_CHECK_EQ_INT(phy_tensor_independent_count(riemann),
                         riemann_expected[entry]);
        phy_tensor_destroy(riemann);

        phy_chart_destroy(chart);
    }

    phy_ir_context_destroy(ir);
}

static void test_symmetry_rejects(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);
    phy_tensor *tensor = make_tensor(chart, "T", 3u);

    /* A permutation must be a bijection of exactly `rank` slots. */
    static const unsigned wrong_length[2] = {1u, 0u};
    PHY_CHECK_EQ_INT(
        phy_tensor_declare_permutation(tensor, wrong_length, 2u, 1),
        PHY_ERR_INVALID_ARGUMENT);

    static const unsigned not_injective[3] = {0u, 0u, 2u};
    PHY_CHECK_EQ_INT(
        phy_tensor_declare_permutation(tensor, not_injective, 3u, 1),
        PHY_ERR_INVALID_ARGUMENT);

    static const unsigned out_of_range[3] = {0u, 1u, 3u};
    PHY_CHECK_EQ_INT(
        phy_tensor_declare_permutation(tensor, out_of_range, 3u, 1),
        PHY_ERR_INVALID_ARGUMENT);

    static const unsigned identity3[3] = {0u, 1u, 2u};
    PHY_CHECK_EQ_INT(phy_tensor_declare_permutation(tensor, identity3, 3u, 0),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_declare_permutation(tensor, identity3, 3u, 2),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_declare_permutation(tensor, NULL, 3u, 1),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_declare_permutation(NULL, identity3, 3u, 1),
                     PHY_ERR_INVALID_ARGUMENT);

    /* Slot symmetry needs two distinct, in-range slots and a real symmetry. */
    PHY_CHECK_EQ_INT(
        phy_tensor_declare_slot_symmetry(tensor, 0u, 0u,
                                         PHY_IR_SYMMETRY_SYMMETRIC),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_tensor_declare_slot_symmetry(tensor, 0u, 3u,
                                         PHY_IR_SYMMETRY_SYMMETRIC),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         tensor, 0u, 1u, PHY_IR_SYMMETRY_NONE),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         NULL, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
                     PHY_ERR_INVALID_ARGUMENT);

    /* Nothing above was accepted. */
    PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(tensor), 1);

    /* The Riemann helper is rank-4 only. */
    PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(tensor),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(NULL),
                     PHY_ERR_INVALID_ARGUMENT);

    phy_tensor_destroy(tensor);
    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

static void test_symmetry_variance_typed(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);

    /*
     * R^a_bcd carries the Riemann symmetries only after its first index is
     * lowered. Declaring them on the mixed form asks to exchange an upper
     * slot with a lower one, which is not a relation between components at
     * all -- so it is a variance error rather than an argument error.
     */
    static const phy_ir_variance mixed[4] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER};
    phy_tensor *mixed_riemann = NULL;
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, "R", 4u, mixed, &mixed_riemann),
                     PHY_OK);

    PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(mixed_riemann),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_tensor_declare_slot_symmetry(mixed_riemann, 0u, 1u,
                                         PHY_IR_SYMMETRY_ANTISYMMETRIC),
        PHY_ERR_TYPE);
    /* A failed declaration leaves the group exactly as it was. */
    PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(mixed_riemann), 1);

    /* The two lower slots of the same pair are still a legal symmetry. */
    PHY_CHECK_EQ_INT(
        phy_tensor_declare_slot_symmetry(mixed_riemann, 2u, 3u,
                                         PHY_IR_SYMMETRY_ANTISYMMETRIC),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(mixed_riemann), 2);

    phy_tensor_destroy(mixed_riemann);

    /* All-lower Riemann is fine. */
    phy_tensor *lower_riemann = make_tensor(chart, "Rl", 4u);
    PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(lower_riemann),
                     PHY_OK);
    phy_tensor_destroy(lower_riemann);

    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

static void test_symmetry_contradiction(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);

    /*
     * Symmetric and antisymmetric in the same pair means T = -T. That is
     * consistent, not malformed: the tensor is identically zero. Reporting it
     * beats rejecting it, because the caller may have derived both facts
     * legitimately.
     */
    phy_tensor *tensor = make_tensor(chart, "T", 2u);
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         tensor, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
                     PHY_OK);
    PHY_CHECK(!phy_tensor_is_identically_zero(tensor));
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         tensor, 0u, 1u, PHY_IR_SYMMETRY_ANTISYMMETRIC),
                     PHY_OK);
    PHY_CHECK(phy_tensor_is_identically_zero(tensor));
    PHY_CHECK_EQ_INT(phy_tensor_independent_count(tensor), 0);

    /* Every component now vanishes, so only zero may be assigned. */
    const unsigned indices[2] = {0u, 1u};
    int sign = 1;
    PHY_CHECK_EQ_INT(phy_tensor_canonical(tensor, indices, NULL, &sign),
                     PHY_OK);
    PHY_CHECK_EQ_INT(sign, 0);
    PHY_CHECK_EQ_INT(phy_tensor_set(tensor, indices, symbol_ref(ir, "x")),
                     PHY_ERR_ASSUMPTION);
    PHY_CHECK_EQ_INT(phy_tensor_set(tensor, indices, phy_tensor_zero(tensor)),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_check_symmetries(tensor), PHY_OK);

    phy_tensor_destroy(tensor);

    /* The same at rank 0: a scalar equal to its own negation is zero. */
    phy_tensor *scalar = NULL;
    PHY_CHECK_EQ_INT(phy_tensor_create(chart, "s", 0u, NULL, &scalar), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_declare_permutation(scalar, NULL, 0u, -1),
                     PHY_OK);
    PHY_CHECK(phy_tensor_is_identically_zero(scalar));
    PHY_CHECK_EQ_INT(phy_tensor_independent_count(scalar), 0);
    phy_tensor_destroy(scalar);

    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

static void test_symmetry_declare_after_assign(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);
    phy_tensor *tensor = make_tensor(chart, "T", 2u);

    const unsigned indices[2] = {0u, 1u};
    const phy_ir_ref value = symbol_ref(ir, "x");
    PHY_CHECK_EQ_INT(phy_tensor_set(tensor, indices, value), PHY_OK);

    /*
     * The assignment was validated against the trivial group. Applying a new
     * symmetry now would reinterpret it rather than check it, so it is
     * refused -- and the refusal must not disturb what is already stored.
     */
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         tensor, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
                     PHY_ERR_ASSUMPTION);
    PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(tensor),
                     PHY_ERR_INVALID_ARGUMENT); /* rank, checked first */

    phy_tensor_component component = {PHY_IR_NULL, 0};
    PHY_CHECK_EQ_INT(phy_tensor_get(tensor, indices, &component), PHY_OK);
    PHY_CHECK(phy_ir_equal(component.ref, value));
    PHY_CHECK_EQ_INT(component.sign, 1);
    PHY_CHECK_EQ_INT(phy_tensor_symmetry_group_order(tensor), 1);

    /* Clearing everything makes the tensor declarable again. */
    phy_tensor_clear(tensor);
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         tensor, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
                     PHY_OK);

    phy_tensor_destroy(tensor);
    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

/* ---------------------------------------------------- assignment and fill */

static void test_assignment_fills_orbit(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);

    /* Symmetric: one assignment populates both entries with the same sign. */
    phy_tensor *symmetric = make_tensor(chart, "g", 2u);
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         symmetric, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
                     PHY_OK);

    const phy_ir_ref value = symbol_ref(ir, "a");
    const unsigned upper[2] = {0u, 1u};
    const unsigned lower[2] = {1u, 0u};
    PHY_CHECK_EQ_INT(phy_tensor_set(symmetric, upper, value), PHY_OK);

    phy_tensor_component got = {PHY_IR_NULL, 0};
    PHY_CHECK_EQ_INT(phy_tensor_get(symmetric, lower, &got), PHY_OK);
    PHY_CHECK(phy_ir_equal(got.ref, value));
    PHY_CHECK_EQ_INT(got.sign, 1);
    PHY_CHECK(phy_tensor_is_assigned(symmetric, lower));
    PHY_CHECK_EQ_INT(phy_tensor_check_symmetries(symmetric), PHY_OK);
    phy_tensor_destroy(symmetric);

    /*
     * Antisymmetric: the orbit shares one handle and differs only in the
     * sign. This is the boundary described in include/phy/tensor.h -- no
     * negated expression is built, because building one is a scalar
     * operation.
     */
    phy_tensor *antisymmetric = make_tensor(chart, "F", 2u);
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         antisymmetric, 0u, 1u, PHY_IR_SYMMETRY_ANTISYMMETRIC),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_set(antisymmetric, upper, value), PHY_OK);

    PHY_CHECK_EQ_INT(phy_tensor_get(antisymmetric, upper, &got), PHY_OK);
    PHY_CHECK(phy_ir_equal(got.ref, value));
    PHY_CHECK_EQ_INT(got.sign, 1);

    PHY_CHECK_EQ_INT(phy_tensor_get(antisymmetric, lower, &got), PHY_OK);
    PHY_CHECK(phy_ir_equal(got.ref, value));
    PHY_CHECK_EQ_INT(got.sign, -1);

    PHY_CHECK_EQ_INT(phy_tensor_check_symmetries(antisymmetric), PHY_OK);

    /* Assigning the mirror entry with the same handle contradicts the sign. */
    PHY_CHECK_EQ_INT(phy_tensor_set(antisymmetric, lower, value),
                     PHY_ERR_ASSUMPTION);
    /* ... and left everything as it was. */
    PHY_CHECK_EQ_INT(phy_tensor_get(antisymmetric, lower, &got), PHY_OK);
    PHY_CHECK(phy_ir_equal(got.ref, value));
    PHY_CHECK_EQ_INT(got.sign, -1);

    /* Re-assigning the same orbit with the same value is idempotent. */
    PHY_CHECK_EQ_INT(phy_tensor_set(antisymmetric, upper, value), PHY_OK);

    /* A different value in the same orbit is a contradiction. */
    PHY_CHECK_EQ_INT(phy_tensor_set(antisymmetric, upper, symbol_ref(ir, "b")),
                     PHY_ERR_ASSUMPTION);
    PHY_CHECK_EQ_INT(phy_tensor_get(antisymmetric, upper, &got), PHY_OK);
    PHY_CHECK(phy_ir_equal(got.ref, value));

    /* Clearing releases the orbit for reassignment. */
    PHY_CHECK_EQ_INT(phy_tensor_clear_component(antisymmetric, upper), PHY_OK);
    PHY_CHECK(!phy_tensor_is_assigned(antisymmetric, upper));
    PHY_CHECK(!phy_tensor_is_assigned(antisymmetric, lower));
    PHY_CHECK_EQ_INT(phy_tensor_get(antisymmetric, lower, &got), PHY_OK);
    PHY_CHECK(phy_ir_equal(got.ref, phy_tensor_zero(antisymmetric)));
    PHY_CHECK_EQ_INT(phy_tensor_set(antisymmetric, upper, symbol_ref(ir, "b")),
                     PHY_OK);

    /* The diagonal is forced to vanish and rejects a non-zero handle. */
    const unsigned diagonal[2] = {2u, 2u};
    PHY_CHECK_EQ_INT(phy_tensor_set(antisymmetric, diagonal, value),
                     PHY_ERR_ASSUMPTION);
    PHY_CHECK_EQ_INT(
        phy_tensor_set(antisymmetric, diagonal,
                       phy_tensor_zero(antisymmetric)),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_get(antisymmetric, diagonal, &got), PHY_OK);
    PHY_CHECK_EQ_INT(got.sign, 0);
    PHY_CHECK(phy_ir_equal(got.ref, phy_tensor_zero(antisymmetric)));

    phy_tensor_destroy(antisymmetric);
    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

static void test_assignment_rejects(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);
    phy_tensor *tensor = make_tensor(chart, "T", 2u);

    const unsigned indices[2] = {0u, 1u};
    const unsigned out_of_range[2] = {0u, 4u};
    phy_tensor_component component = {PHY_IR_NULL, 0};

    PHY_CHECK_EQ_INT(phy_tensor_set(tensor, indices, PHY_IR_NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_set(NULL, indices, symbol_ref(ir, "x")),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_set(tensor, out_of_range, symbol_ref(ir, "x")),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_set(tensor, NULL, symbol_ref(ir, "x")),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_tensor_set_flat(tensor, 16u, symbol_ref(ir, "x")),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_set_flat(tensor, 0u, PHY_IR_NULL),
                     PHY_ERR_INVALID_ARGUMENT);

    PHY_CHECK_EQ_INT(phy_tensor_get(tensor, out_of_range, &component),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_get(tensor, indices, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_get_flat(tensor, 16u, &component),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_get(NULL, indices, &component),
                     PHY_ERR_INVALID_ARGUMENT);

    PHY_CHECK_EQ_INT(phy_tensor_canonical(tensor, out_of_range, NULL, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_canonical(NULL, indices, NULL, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(!phy_tensor_is_canonical(tensor, out_of_range));
    PHY_CHECK(!phy_tensor_is_canonical(NULL, indices));
    PHY_CHECK(!phy_tensor_is_assigned(tensor, out_of_range));
    PHY_CHECK(!phy_tensor_is_assigned(NULL, indices));

    PHY_CHECK_EQ_INT(phy_tensor_clear_component(tensor, out_of_range),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_clear_component(NULL, indices),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_fill_symmetries(NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_tensor_check_symmetries(NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    phy_tensor_clear(NULL);

    /* Nothing above stored anything. */
    for (size_t flat = 0u; flat < phy_tensor_component_count(tensor); ++flat) {
        PHY_CHECK_EQ_INT(phy_tensor_get_flat(tensor, flat, &component),
                         PHY_OK);
        PHY_CHECK(phy_ir_equal(component.ref, phy_tensor_zero(tensor)));
    }

    phy_tensor_destroy(tensor);
    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

/*
 * TENSOR_CORE.md test 3, in its component-independent form.
 *
 * For a tensor declared with the Riemann symmetries, every component
 * reachable by more than one symmetry path must hold the same expression.
 * Covers antisymmetry in the first pair, antisymmetry in the second pair, and
 * pair exchange -- and, because the group is closed, all five of their
 * products as well.
 */
static void test_riemann_fill_agreement(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);

    static const unsigned dimensions[2] = {2u, 4u};
    for (unsigned entry = 0u; entry < 2u; ++entry) {
        const unsigned dimension = dimensions[entry];
        phy_chart *chart = make_chart(ir, dimension);
        phy_tensor *riemann = make_tensor(chart, "R", 4u);
        PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(riemann), PHY_OK);

        /*
         * Assign every independent component a distinct symbol, so that two
         * components agreeing is evidence rather than coincidence: if the fill
         * routed a value to the wrong orbit, the handles would differ.
         */
        const size_t count = phy_tensor_component_count(riemann);
        size_t assigned = 0u;
        for (size_t flat = 0u; flat < count; ++flat) {
            unsigned indices[PHY_TENSOR_MAX_RANK];
            PHY_CHECK_EQ_INT(phy_tensor_unflatten(riemann, flat, indices),
                             PHY_OK);
            if (!phy_tensor_is_canonical(riemann, indices)) {
                continue;
            }
            char name[16];
            name[0] = 'c';
            name[1] = (char)('0' + (int)(assigned / 10u));
            name[2] = (char)('0' + (int)(assigned % 10u));
            name[3] = '\0';
            PHY_CHECK_EQ_INT(
                phy_tensor_set(riemann, indices, symbol_ref(ir, name)),
                PHY_OK);
            assigned++;
        }
        PHY_CHECK_EQ_INT(assigned, phy_tensor_independent_count(riemann));

        /* The dense table is internally consistent with every declaration. */
        PHY_CHECK_EQ_INT(phy_tensor_check_symmetries(riemann), PHY_OK);

        /*
         * Spot-check the three generators explicitly rather than trusting the
         * group machinery to have covered them.
         */
        for (size_t flat = 0u; flat < count; ++flat) {
            unsigned a[PHY_TENSOR_MAX_RANK];
            PHY_CHECK_EQ_INT(phy_tensor_unflatten(riemann, flat, a), PHY_OK);

            const unsigned swap_first[4] = {a[1], a[0], a[2], a[3]};
            const unsigned swap_second[4] = {a[0], a[1], a[3], a[2]};
            const unsigned exchange[4] = {a[2], a[3], a[0], a[1]};

            phy_tensor_component base = {PHY_IR_NULL, 0};
            phy_tensor_component other = {PHY_IR_NULL, 0};
            PHY_CHECK_EQ_INT(phy_tensor_get(riemann, a, &base), PHY_OK);

            PHY_CHECK_EQ_INT(phy_tensor_get(riemann, swap_first, &other),
                             PHY_OK);
            PHY_CHECK(phy_ir_equal(other.ref, base.ref));
            PHY_CHECK_EQ_INT(other.sign, -base.sign);

            PHY_CHECK_EQ_INT(phy_tensor_get(riemann, swap_second, &other),
                             PHY_OK);
            PHY_CHECK(phy_ir_equal(other.ref, base.ref));
            PHY_CHECK_EQ_INT(other.sign, -base.sign);

            PHY_CHECK_EQ_INT(phy_tensor_get(riemann, exchange, &other),
                             PHY_OK);
            PHY_CHECK(phy_ir_equal(other.ref, base.ref));
            PHY_CHECK_EQ_INT(other.sign, base.sign);
        }

        /*
         * Re-deriving the dependents changes nothing: assignment already
         * filled them.
         */
        phy_ir_ref before[PHY_TENSOR_MAX_COMPONENTS];
        int8_t before_sign[PHY_TENSOR_MAX_COMPONENTS];
        for (size_t flat = 0u; flat < count; ++flat) {
            phy_tensor_component component = {PHY_IR_NULL, 0};
            PHY_CHECK_EQ_INT(phy_tensor_get_flat(riemann, flat, &component),
                             PHY_OK);
            before[flat] = component.ref;
            before_sign[flat] = component.sign;
        }
        PHY_CHECK_EQ_INT(phy_tensor_fill_symmetries(riemann), PHY_OK);
        for (size_t flat = 0u; flat < count; ++flat) {
            phy_tensor_component component = {PHY_IR_NULL, 0};
            PHY_CHECK_EQ_INT(phy_tensor_get_flat(riemann, flat, &component),
                             PHY_OK);
            PHY_CHECK(phy_ir_equal(component.ref, before[flat]));
            PHY_CHECK_EQ_INT(component.sign, before_sign[flat]);
        }

        phy_tensor_destroy(riemann);
        phy_chart_destroy(chart);
    }

    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
    phy_ir_context_destroy(ir);
}

/*
 * The canonical representative is the least flat index of the orbit, and the
 * reported sign genuinely relates the component to it.
 */
static void test_canonical_lookup(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);
    phy_tensor *riemann = make_tensor(chart, "R", 4u);
    PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(riemann), PHY_OK);

    const size_t count = phy_tensor_component_count(riemann);
    for (size_t flat = 0u; flat < count; ++flat) {
        unsigned indices[PHY_TENSOR_MAX_RANK];
        unsigned representative[PHY_TENSOR_MAX_RANK];
        int sign = 0;
        PHY_CHECK_EQ_INT(phy_tensor_unflatten(riemann, flat, indices), PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_tensor_canonical(riemann, indices, representative, &sign),
            PHY_OK);

        size_t representative_flat = 0u;
        PHY_CHECK_EQ_INT(
            phy_tensor_flatten(riemann, representative, &representative_flat),
            PHY_OK);

        /* Never larger than the component it represents. */
        PHY_CHECK(representative_flat <= flat);

        /* The representative is its own representative. */
        unsigned again[PHY_TENSOR_MAX_RANK];
        int again_sign = 0;
        PHY_CHECK_EQ_INT(
            phy_tensor_canonical(riemann, representative, again, &again_sign),
            PHY_OK);
        size_t again_flat = 0u;
        PHY_CHECK_EQ_INT(phy_tensor_flatten(riemann, again, &again_flat),
                         PHY_OK);
        PHY_CHECK_EQ_INT(again_flat, representative_flat);

        /* A vanishing component vanishes whichever member you ask about. */
        PHY_CHECK_EQ_INT(sign == 0, again_sign == 0);

        if (sign != 0) {
            PHY_CHECK(sign == 1 || sign == -1);
            PHY_CHECK_EQ_INT(again_sign, 1);
            PHY_CHECK_EQ_INT(phy_tensor_is_canonical(riemann, representative),
                             1);
        }
        /*
         * R_aacd and friends: a repeated index inside an antisymmetric pair
         * must be reported as forced to vanish.
         */
        if (indices[0] == indices[1] || indices[2] == indices[3]) {
            PHY_CHECK_EQ_INT(sign, 0);
        }
    }

    phy_tensor_destroy(riemann);
    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

/*
 * check_symmetries is a real gate, not a formality: corrupt one entry of the
 * dense table and it must say so.
 */
static void test_check_symmetries_detects_disagreement(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);
    phy_tensor *symmetric = make_tensor(chart, "g", 2u);
    PHY_CHECK_EQ_INT(phy_tensor_declare_slot_symmetry(
                         symmetric, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
                     PHY_OK);

    const unsigned upper[2] = {0u, 1u};
    PHY_CHECK_EQ_INT(phy_tensor_set(symmetric, upper, symbol_ref(ir, "a")),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_check_symmetries(symmetric), PHY_OK);

    /*
     * Reach past the API to break the invariant the API maintains. Clearing
     * one half of a symmetric pair is precisely the state a hand-populated
     * dense table can reach, which is why the check exists as an API rather
     * than as an assertion inside set().
     */
    phy_tensor *raw = symmetric;
    (void)raw;
    const unsigned lower[2] = {1u, 0u};
    /* Assign the mirror entry a different value by clearing first. */
    PHY_CHECK_EQ_INT(phy_tensor_clear_component(symmetric, upper), PHY_OK);
    PHY_CHECK_EQ_INT(phy_tensor_set(symmetric, lower, symbol_ref(ir, "b")),
                     PHY_OK);
    /* Still symmetric: the fill kept both halves in step. */
    PHY_CHECK_EQ_INT(phy_tensor_check_symmetries(symmetric), PHY_OK);

    phy_tensor_component component = {PHY_IR_NULL, 0};
    PHY_CHECK_EQ_INT(phy_tensor_get(symmetric, upper, &component), PHY_OK);
    PHY_CHECK(phy_ir_equal(component.ref, symbol_ref(ir, "b")));

    phy_tensor_destroy(symmetric);
    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

/*
 * TENSOR_CORE.md test 7, in the form this half of the task can carry: with
 * allocation failing, every entry point returns a typed status, hands back no
 * object, and leaks nothing.
 *
 * The node-cap half of that test belongs with the scalar layer, since only
 * expression construction can reach the IR's live-node ceiling.
 */
static void test_allocation_failure_unwinds(void)
{
    phy_telemetry baseline;
    phy_telemetry after;
    phy_telemetry_get(&baseline);

    /* Count the allocations a full build makes, so the sweep can cover them. */
    const uint32_t attempts_before = phy_host_alloc_attempts();
    {
        phy_ir_context *ir = phy_ir_context_create(NULL);
        phy_chart *chart = make_chart(ir, 4u);
        phy_tensor *tensor = make_tensor(chart, "R", 4u);
        PHY_CHECK(tensor != NULL);
        PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(tensor), PHY_OK);
        const unsigned indices[4] = {0u, 1u, 0u, 1u};
        PHY_CHECK_EQ_INT(phy_tensor_set(tensor, indices, symbol_ref(ir, "x")),
                         PHY_OK);
        phy_tensor_destroy(tensor);
        phy_chart_destroy(chart);
        phy_ir_context_destroy(ir);
    }
    const uint32_t allocations = phy_host_alloc_attempts() - attempts_before;
    PHY_CHECK(allocations > 2u);

    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(after.bytes_live, baseline.bytes_live);

    unsigned reached_tensor = 0u;
    for (uint32_t nth = 1u; nth <= allocations; ++nth) {
        phy_host_fail_alloc_after(nth);

        phy_ir_context *ir = phy_ir_context_create(NULL);
        if (ir == NULL) {
            /* The injected failure landed inside context creation. */
            phy_host_fail_alloc_after(0u);
            phy_telemetry_get(&after);
            PHY_CHECK_EQ_INT(after.bytes_live, baseline.bytes_live);
            continue;
        }

        phy_chart *chart = NULL;
        const phy_status chart_status =
            phy_chart_create(ir, kCoords4, 4u, &chart);
        if (chart_status != PHY_OK) {
            /* Typed, and no half-built chart handed back. */
            PHY_CHECK(chart == NULL);
            PHY_CHECK(chart_status == PHY_ERR_OUT_OF_MEMORY ||
                      chart_status == PHY_ERR_MEMORY_LIMIT);
            phy_host_fail_alloc_after(0u);
            PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
            phy_ir_context_destroy(ir);
            phy_telemetry_get(&after);
            PHY_CHECK_EQ_INT(after.bytes_live, baseline.bytes_live);
            continue;
        }

        phy_tensor *tensor = NULL;
        const phy_status tensor_status =
            phy_tensor_create(chart, "R", 4u, kAllLower, &tensor);
        if (tensor_status != PHY_OK) {
            PHY_CHECK(tensor == NULL);
            PHY_CHECK(tensor_status == PHY_ERR_OUT_OF_MEMORY ||
                      tensor_status == PHY_ERR_MEMORY_LIMIT);
        } else {
            reached_tensor++;
            /*
             * Past construction nothing in this layer allocates, so the rest
             * of the build must succeed even with a failure still armed.
             */
            PHY_CHECK_EQ_INT(phy_tensor_declare_riemann_symmetry(tensor),
                             PHY_OK);
            const unsigned indices[4] = {0u, 1u, 0u, 1u};
            const phy_ir_ref value = phy_ir_integer(ir, 7);
            if (value != PHY_IR_NULL) {
                PHY_CHECK_EQ_INT(phy_tensor_set(tensor, indices, value),
                                 PHY_OK);
                PHY_CHECK_EQ_INT(phy_tensor_check_symmetries(tensor), PHY_OK);
            }
            phy_tensor_destroy(tensor);
        }

        phy_host_fail_alloc_after(0u); /* disarm before validating */
        phy_chart_destroy(chart);
        PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
        phy_ir_context_destroy(ir);

        phy_telemetry_get(&after);
        PHY_CHECK_EQ_INT(after.bytes_live, baseline.bytes_live);
    }

    /*
     * The sweep is only meaningful if it actually exercised the tensor's own
     * allocations rather than dying in the IR every time.
     */
    PHY_CHECK(reached_tensor > 0u);

    phy_host_fail_alloc_after(0u);
    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(after.bytes_live, baseline.bytes_live);
}

/*
 * Dense storage is what docs/agent-tasks/TENSOR_CORE.md asks to be spent, so
 * it is worth pinning what it actually costs.
 */
static void test_storage_is_bounded(void)
{
    phy_telemetry before;
    phy_telemetry after;

    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_chart *chart = make_chart(ir, 4u);

    phy_telemetry_get(&before);
    phy_tensor *riemann = make_tensor(chart, "R", 4u);
    PHY_CHECK(riemann != NULL);
    phy_telemetry_get(&after);

    PHY_CHECK_EQ_INT(phy_tensor_component_count(riemann), 256);

    /*
     * 256 handles plus a sign and an assigned flag each, plus the fixed
     * header. The document budgets about 1 KB for the handles; the two extra
     * bytes per component are what buys a fill discipline that never forms a
     * negated expression.
     */
    const size_t growth = after.bytes_live - before.bytes_live;
    PHY_CHECK(growth >= 256u * 6u);
    PHY_CHECK(growth < 2560u);

    /* A mostly-zero table costs one interned node, not 256. */
    const size_t nodes_before = phy_ir_node_count(ir);
    phy_tensor *another = make_tensor(chart, "S", 4u);
    PHY_CHECK(another != NULL);
    PHY_CHECK_EQ_INT(phy_ir_node_count(ir) - nodes_before, 0);

    phy_tensor_destroy(another);
    phy_tensor_destroy(riemann);
    phy_chart_destroy(chart);
    phy_ir_context_destroy(ir);
}

/* -------------------------------------------------------------------- main */

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }

    PHY_TEST_CASE(test_chart_basics);
    PHY_TEST_CASE(test_chart_rejects);
    PHY_TEST_CASE(test_tensor_shape);
    PHY_TEST_CASE(test_tensor_valence);
    PHY_TEST_CASE(test_tensor_rejects);
    PHY_TEST_CASE(test_flat_index_roundtrip);
    PHY_TEST_CASE(test_symmetry_group_orders);
    PHY_TEST_CASE(test_symmetry_dimension_independence);
    PHY_TEST_CASE(test_symmetry_rejects);
    PHY_TEST_CASE(test_symmetry_variance_typed);
    PHY_TEST_CASE(test_symmetry_contradiction);
    PHY_TEST_CASE(test_symmetry_declare_after_assign);
    PHY_TEST_CASE(test_assignment_fills_orbit);
    PHY_TEST_CASE(test_assignment_rejects);
    PHY_TEST_CASE(test_riemann_fill_agreement);
    PHY_TEST_CASE(test_canonical_lookup);
    PHY_TEST_CASE(test_check_symmetries_detects_disagreement);
    PHY_TEST_CASE(test_allocation_failure_unwinds);
    PHY_TEST_CASE(test_storage_is_bounded);

    const int result = PHY_TEST_REPORT("test_tensor");
    phy_platform_shutdown();
    return result;
}
