/*
 * Manifolds and differential forms: canonical storage, the four exterior
 * operations, and the identities that pin their conventions.
 *
 * The identity tests are the ones to read first. A wedge product with the
 * wrong normalization still produces plausible components, and a Hodge dual
 * with the wrong epsilon still produces a form of the right degree; what
 * neither survives is being asked to satisfy
 *
 *   alpha ^ beta = (-1)^{pq} beta ^ alpha        graded commutativity
 *   d(d alpha) = 0                                nilpotence
 *   d(alpha ^ beta) = d alpha ^ beta + (-1)^p alpha ^ d beta
 *   iota_v iota_v = 0, and iota_v as an antiderivation
 *   ** = (-1)^{p(n-p)} sign(det g)                on every degree
 *
 * simultaneously in more than one dimension and both signatures. So those are
 * checked with generic symbolic components rather than with worked examples,
 * and the worked R^2 and R^3 examples exist to catch a convention that is
 * self-consistently wrong.
 *
 * Every structural test that can run at more than one dimension does, for the
 * reason tests/test_tensor.c gives: a corpus that is almost entirely
 * four-dimensional hides a hard-coded n = 4.
 */
#include <stdio.h>
#include <string.h>

#include "phy/cas.h"
#include "phy/geom.h"
#include "phy/ir.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy/tensor.h"
#include "phy_test.h"

/* --------------------------------------------------------------- fixture */

static const char *const kCoords1[1] = {"x"};
static const char *const kCoords2[2] = {"x", "y"};
static const char *const kCoords3[3] = {"x", "y", "z"};
static const char *const kCoords4[4] = {"t", "x", "y", "z"};

static const phy_ir_variance kUpper[1] = {PHY_IR_INDEX_UPPER};
static const phy_ir_variance kLower[1] = {PHY_IR_INDEX_LOWER};

static const int8_t kEuclidean4[4] = {1, 1, 1, 1};
static const int8_t kLorentz2[2] = {-1, 1};
static const int8_t kLorentz4[4] = {-1, 1, 1, 1};

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_chart *chart;
    phy_manifold *manifold;
} fixture;

static const char *const *coordinate_names(unsigned dimension)
{
    switch (dimension) {
    case 1u:
        return kCoords1;
    case 2u:
        return kCoords2;
    case 3u:
        return kCoords3;
    default:
        return kCoords4;
    }
}

static fixture open_fixture(unsigned dimension, phy_orientation orientation,
                            const int8_t *signature)
{
    fixture f;
    memset(&f, 0, sizeof f);

    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(
        phy_chart_create(f.ir, coordinate_names(dimension), dimension,
                         &f.chart),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_manifold_create(f.ir, "M", dimension, orientation,
                                         signature, &f.manifold),
                     PHY_OK);
    unsigned index = 999u;
    PHY_CHECK_EQ_INT(phy_manifold_add_chart(f.manifold, f.chart, &index),
                     PHY_OK);
    PHY_CHECK_EQ_INT(index, 0);
    return f;
}

/* The oriented Riemannian case, which most algebraic tests do not care about. */
static fixture open_euclidean(unsigned dimension)
{
    return open_fixture(dimension, PHY_ORIENTATION_POSITIVE, NULL);
}

static void close_fixture(fixture *f)
{
    /* Every operation must leave both lower layers validating, so the suite
       asks on the way out of every case rather than in one dedicated test. */
    PHY_CHECK_EQ_INT(phy_cas_validate(f->cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(f->ir), PHY_OK);
    phy_manifold_destroy(f->manifold);
    phy_chart_destroy(f->chart);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
    memset(f, 0, sizeof *f);
}

/* ---------------------------------------------------------------- helpers */

static char g_text[4096];

static phy_ir_ref parse(phy_ir_context *ir, const char *text)
{
    phy_ir_ref ref = PHY_IR_NULL;
    size_t offset = 0u;
    const phy_status status = phy_ir_read(ir, text, &ref, &offset);
    if (status != PHY_OK) {
        fprintf(stderr, "  parse failed at %u in \"%s\": %s\n",
                (unsigned)offset, text, phy_status_name(status));
    }
    PHY_CHECK_EQ_INT(status, PHY_OK);
    return ref;
}

static const char *render(phy_ir_context *ir, phy_ir_ref ref)
{
    size_t length = 0u;
    if (phy_ir_write(ir, ref, g_text, sizeof g_text, &length) != PHY_OK) {
        return "<write failed>";
    }
    return g_text;
}

static phy_ir_ref symbol_ref(phy_ir_context *ir, const char *name)
{
    return phy_ir_symbol_ref(ir, phy_ir_intern(ir, name));
}

static char g_name[32];

static const char *indexed_name(const char *prefix, size_t position)
{
    snprintf(g_name, sizeof g_name, "%s%u", prefix, (unsigned)position);
    return g_name;
}

static phy_form *make_form(fixture *f, unsigned degree)
{
    phy_form *form = NULL;
    PHY_CHECK_EQ_INT(phy_form_create(f->manifold, 0u, NULL, degree, &form),
                     PHY_OK);
    return form;
}

/* Store `text` at a canonical position. */
static void put(fixture *f, phy_form *form, size_t position, const char *text)
{
    PHY_CHECK_EQ_INT(
        phy_form_set_at(f->cas, form, position, parse(f->ir, text)), PHY_OK);
}

static const char *component(fixture *f, const phy_form *form, size_t position)
{
    phy_ir_ref ref = PHY_IR_NULL;
    if (phy_form_get_at(form, position, &ref) != PHY_OK) {
        return "<out of range>";
    }
    return render(f->ir, ref);
}

/*
 * A form whose every component is its own symbol.
 *
 * Generic components are what make an identity a test rather than an example:
 * two components agreeing is evidence only when nothing forced them to.
 */
static phy_form *generic_form(fixture *f, unsigned degree, const char *prefix)
{
    phy_form *form = make_form(f, degree);
    const size_t count = phy_form_component_count(form);
    for (size_t position = 0u; position < count; ++position) {
        PHY_CHECK_EQ_INT(
            phy_form_set_at(f->cas, form, position,
                            symbol_ref(f->ir, indexed_name(prefix, position))),
            PHY_OK);
    }
    return form;
}

/*
 * A form whose every component is an explicit function of every coordinate,
 * so that phy_cas_diff actually differentiates rather than deferring.
 *
 * Monomial times a sine, with exponents that vary per component and per axis:
 * a mixed partial that failed to commute would leave a residue rather than
 * cancelling by symmetry of the ansatz.
 */
static phy_form *smooth_form(fixture *f, unsigned degree, int seed)
{
    phy_form *form = make_form(f, degree);
    const unsigned dimension = phy_form_dimension(form);
    const char *const *coords = coordinate_names(dimension);
    const size_t count = phy_form_component_count(form);

    for (size_t position = 0u; position < count; ++position) {
        char text[256];
        int written =
            snprintf(text, sizeof text, "(* %d", (int)position + seed);
        for (unsigned axis = 0u; axis < dimension; ++axis) {
            const unsigned exponent =
                1u + (unsigned)((position + axis + (size_t)seed) % 3u);
            written += snprintf(text + written, sizeof text - (size_t)written,
                                " (^ %s %u)", coords[axis], exponent);
        }
        if ((position + (size_t)seed) % 2u == 1u) {
            written += snprintf(text + written, sizeof text - (size_t)written,
                                " (fn sin %s)", coords[0]);
        }
        (void)snprintf(text + written, sizeof text - (size_t)written, ")");
        put(f, form, position, text);
    }
    return form;
}

/* Both forms must exist, agree on degree, and be provably equal. */
static void check_equal(fixture *f, const phy_form *left,
                        const phy_form *right)
{
    PHY_CHECK(left != NULL && right != NULL);
    if (left == NULL || right == NULL) {
        return;
    }
    PHY_CHECK_EQ_INT(phy_form_degree(left), phy_form_degree(right));
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    const phy_status status =
        phy_form_equivalent(f->cas, left, right, &decision);
    PHY_CHECK_EQ_INT(status, PHY_OK);
    if (status == PHY_OK && decision != PHY_CAS_ZERO) {
        const size_t count = phy_form_component_count(left);
        for (size_t position = 0u; position < count; ++position) {
            fprintf(stderr, "  component %u: %s\n", (unsigned)position,
                    component(f, left, position));
            fprintf(stderr, "        vs   : %s\n",
                    component(f, right, position));
        }
    }
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

/* `left` must equal `sign` times `right`. */
static void check_equal_signed(fixture *f, const phy_form *left,
                               const phy_form *right, int sign)
{
    phy_ir_ref scalar = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f->cas, sign, 1, &scalar), PHY_OK);
    phy_form *scaled = NULL;
    PHY_CHECK_EQ_INT(phy_form_scale(f->cas, right, scalar, &scaled), PHY_OK);
    check_equal(f, left, scaled);
    phy_form_destroy(scaled);
}

static void check_vanishes(fixture *f, const phy_form *form)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_form_is_zero(f->cas, form, &decision), PHY_OK);
    if (decision != PHY_CAS_ZERO) {
        const size_t count = phy_form_component_count(form);
        for (size_t position = 0u; position < count; ++position) {
            fprintf(stderr, "  residual component %u: %s\n",
                    (unsigned)position, component(f, form, position));
        }
    }
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

/* A rank-1 contravariant tensor on the fixture's chart. */
static phy_tensor *make_vector(fixture *f, const char *prefix)
{
    phy_tensor *vector = NULL;
    PHY_CHECK_EQ_INT(phy_tensor_create(f->chart, prefix, 1u, kUpper, &vector),
                     PHY_OK);
    for (unsigned axis = 0u; axis < phy_chart_dimension(f->chart); ++axis) {
        PHY_CHECK_EQ_INT(
            phy_tensor_set(vector, &axis,
                           symbol_ref(f->ir, indexed_name(prefix, axis))),
            PHY_OK);
    }
    return vector;
}

/* n choose k, computed independently of the implementation under test. */
static size_t expected_binomial(unsigned n, unsigned k)
{
    size_t table[PHY_MANIFOLD_MAX_DIM + 1u][PHY_MANIFOLD_MAX_DIM + 1u];
    for (unsigned row = 0u; row <= PHY_MANIFOLD_MAX_DIM; ++row) {
        for (unsigned column = 0u; column <= PHY_MANIFOLD_MAX_DIM; ++column) {
            if (column == 0u || column == row) {
                table[row][column] = 1u;
            } else if (column > row) {
                table[row][column] = 0u;
            } else {
                table[row][column] =
                    table[row - 1u][column - 1u] + table[row - 1u][column];
            }
        }
    }
    return (k > n) ? 0u : table[n][k];
}

/* Inversion count parity, computed the slow and obvious way. */
static int expected_parity(const unsigned *values, unsigned count)
{
    unsigned inversions = 0u;
    for (unsigned i = 0u; i < count; ++i) {
        for (unsigned j = i + 1u; j < count; ++j) {
            if (values[i] == values[j]) {
                return 0;
            }
            if (values[i] > values[j]) {
                inversions++;
            }
        }
    }
    return (inversions % 2u == 0u) ? 1 : -1;
}

/* --------------------------------------------------------- manifold tests */

static void test_manifold_metadata(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    phy_manifold *euclidean = NULL;
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "R3", 3u,
                                         PHY_ORIENTATION_POSITIVE, NULL,
                                         &euclidean),
                     PHY_OK);
    PHY_CHECK_EQ_STR(phy_manifold_name(euclidean), "R3");
    PHY_CHECK(phy_manifold_head(euclidean) != PHY_IR_NO_SYMBOL);
    PHY_CHECK_EQ_INT(phy_manifold_ir(euclidean) == ir, 1);
    PHY_CHECK_EQ_INT(phy_manifold_dimension(euclidean), 3);
    PHY_CHECK_EQ_INT(phy_manifold_orientation(euclidean),
                     PHY_ORIENTATION_POSITIVE);
    PHY_CHECK_EQ_INT(phy_manifold_negative_count(euclidean), 0);
    PHY_CHECK_EQ_INT(phy_manifold_metric_sign(euclidean), 1);
    for (unsigned axis = 0u; axis < 3u; ++axis) {
        PHY_CHECK_EQ_INT(phy_manifold_signature(euclidean, axis), 1);
    }
    /* Out of range reports 0 rather than a plausible +1. */
    PHY_CHECK_EQ_INT(phy_manifold_signature(euclidean, 3u), 0);
    PHY_CHECK_EQ_INT(phy_manifold_chart_count(euclidean), 0);
    PHY_CHECK(phy_manifold_chart(euclidean, 0u) == NULL);

    phy_manifold *minkowski = NULL;
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "M4", 4u,
                                         PHY_ORIENTATION_NEGATIVE, kLorentz4,
                                         &minkowski),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_manifold_signature(minkowski, 0u), -1);
    PHY_CHECK_EQ_INT(phy_manifold_signature(minkowski, 1u), 1);
    PHY_CHECK_EQ_INT(phy_manifold_negative_count(minkowski), 1);
    /* det g < 0 for one negative entry, and this is the factor ** turns on. */
    PHY_CHECK_EQ_INT(phy_manifold_metric_sign(minkowski), -1);
    PHY_CHECK_EQ_INT(phy_manifold_orientation(minkowski),
                     PHY_ORIENTATION_NEGATIVE);

    phy_manifold *unoriented = NULL;
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "U", 2u, PHY_ORIENTATION_NONE,
                                         NULL, &unoriented),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_manifold_orientation(unoriented),
                     PHY_ORIENTATION_NONE);

    /* The null accessors answer rather than trap. */
    PHY_CHECK(phy_manifold_ir(NULL) == NULL);
    PHY_CHECK(phy_manifold_name(NULL) == NULL);
    PHY_CHECK_EQ_INT(phy_manifold_dimension(NULL), 0);
    PHY_CHECK_EQ_INT(phy_manifold_orientation(NULL), PHY_ORIENTATION_NONE);
    PHY_CHECK_EQ_INT(phy_manifold_signature(NULL, 0u), 0);
    PHY_CHECK_EQ_INT(phy_manifold_negative_count(NULL), 0);
    PHY_CHECK_EQ_INT(phy_manifold_metric_sign(NULL), 0);
    PHY_CHECK_EQ_INT(phy_manifold_chart_count(NULL), 0);
    PHY_CHECK_EQ_INT(phy_manifold_head(NULL), PHY_IR_NO_SYMBOL);

    phy_manifold_destroy(unoriented);
    phy_manifold_destroy(minkowski);
    phy_manifold_destroy(euclidean);
    phy_manifold_destroy(NULL);
    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
    phy_ir_context_destroy(ir);
}

static void test_manifold_rejects(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_manifold *manifold = NULL;

    PHY_CHECK_EQ_INT(phy_manifold_create(NULL, "M", 2u,
                                         PHY_ORIENTATION_POSITIVE, NULL,
                                         &manifold),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, NULL, 2u,
                                         PHY_ORIENTATION_POSITIVE, NULL,
                                         &manifold),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "", 2u, PHY_ORIENTATION_POSITIVE,
                                         NULL, &manifold),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "M", 2u,
                                         PHY_ORIENTATION_POSITIVE, NULL, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "M", 0u,
                                         PHY_ORIENTATION_POSITIVE, NULL,
                                         &manifold),
                     PHY_ERR_INVALID_ARGUMENT);
    /* Well-formed, not implemented -- the distinction the header draws. */
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "M", 5u,
                                         PHY_ORIENTATION_POSITIVE, NULL,
                                         &manifold),
                     PHY_ERR_UNSUPPORTED);
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "M", 2u, (phy_orientation)7, NULL,
                                         &manifold),
                     PHY_ERR_INVALID_ARGUMENT);

    /* A signature entry has no admissible value other than +1 or -1. */
    static const int8_t kZeroEntry[2] = {1, 0};
    static const int8_t kTwoEntry[2] = {2, 1};
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "M", 2u,
                                         PHY_ORIENTATION_POSITIVE, kZeroEntry,
                                         &manifold),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "M", 2u,
                                         PHY_ORIENTATION_POSITIVE, kTwoEntry,
                                         &manifold),
                     PHY_ERR_INVALID_ARGUMENT);

    PHY_CHECK(manifold == NULL);
    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
    phy_ir_context_destroy(ir);
}

static void test_manifold_charts(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_ir_context *other = phy_ir_context_create(NULL);

    phy_manifold *manifold = NULL;
    PHY_CHECK_EQ_INT(phy_manifold_create(ir, "M", 2u,
                                         PHY_ORIENTATION_POSITIVE, NULL,
                                         &manifold),
                     PHY_OK);

    phy_chart *good = NULL;
    PHY_CHECK_EQ_INT(phy_chart_create(ir, kCoords2, 2u, &good), PHY_OK);
    phy_chart *wrong_dimension = NULL;
    PHY_CHECK_EQ_INT(phy_chart_create(ir, kCoords3, 3u, &wrong_dimension),
                     PHY_OK);
    phy_chart *wrong_context = NULL;
    PHY_CHECK_EQ_INT(phy_chart_create(other, kCoords2, 2u, &wrong_context),
                     PHY_OK);

    PHY_CHECK_EQ_INT(phy_manifold_add_chart(manifold, NULL, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_manifold_add_chart(NULL, good, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_manifold_add_chart(manifold, wrong_dimension, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    /*
     * A chart carries its context, so unlike a bare ref this mismatch is
     * reliably detectable, and the layer refuses it rather than differentiating
     * against symbols from a foreign context.
     */
    PHY_CHECK_EQ_INT(phy_manifold_add_chart(manifold, wrong_context, NULL),
                     PHY_ERR_INVALID_ARGUMENT);

    unsigned index = 999u;
    PHY_CHECK_EQ_INT(phy_manifold_add_chart(manifold, good, &index), PHY_OK);
    PHY_CHECK_EQ_INT(index, 0);
    PHY_CHECK_EQ_INT(phy_manifold_chart_count(manifold), 1);
    PHY_CHECK(phy_manifold_chart(manifold, 0u) == good);
    PHY_CHECK(phy_manifold_chart(manifold, 1u) == NULL);

    /* Registering the same chart twice would give one chart two indices. */
    PHY_CHECK_EQ_INT(phy_manifold_add_chart(manifold, good, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_manifold_chart_count(manifold), 1);

    /* Fill to the ceiling, then past it. */
    phy_chart *extra[PHY_MANIFOLD_MAX_CHARTS];
    static const char *const kAlt[4][2] = {
        {"u0", "v0"}, {"u1", "v1"}, {"u2", "v2"}, {"u3", "v3"}};
    for (unsigned slot = 0u; slot < PHY_MANIFOLD_MAX_CHARTS; ++slot) {
        PHY_CHECK_EQ_INT(phy_chart_create(ir, kAlt[slot], 2u, &extra[slot]),
                         PHY_OK);
    }
    for (unsigned slot = 0u; slot + 1u < PHY_MANIFOLD_MAX_CHARTS; ++slot) {
        PHY_CHECK_EQ_INT(phy_manifold_add_chart(manifold, extra[slot], &index),
                         PHY_OK);
        PHY_CHECK_EQ_INT(index, slot + 1u);
    }
    PHY_CHECK_EQ_INT(phy_manifold_chart_count(manifold),
                     PHY_MANIFOLD_MAX_CHARTS);
    PHY_CHECK_EQ_INT(
        phy_manifold_add_chart(manifold, extra[PHY_MANIFOLD_MAX_CHARTS - 1u],
                               NULL),
        PHY_ERR_UNSUPPORTED);
    PHY_CHECK_EQ_INT(phy_manifold_chart_count(manifold),
                     PHY_MANIFOLD_MAX_CHARTS);

    for (unsigned slot = 0u; slot < PHY_MANIFOLD_MAX_CHARTS; ++slot) {
        phy_chart_destroy(extra[slot]);
    }
    phy_manifold_destroy(manifold);
    phy_chart_destroy(wrong_context);
    phy_chart_destroy(wrong_dimension);
    phy_chart_destroy(good);
    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(other), PHY_OK);
    phy_ir_context_destroy(other);
    phy_ir_context_destroy(ir);
}

/* ------------------------------------------------------------- form shape */

static void test_form_shape(void)
{
    for (unsigned dimension = 1u; dimension <= PHY_MANIFOLD_MAX_DIM;
         ++dimension) {
        fixture f = open_euclidean(dimension);

        for (unsigned degree = 0u; degree <= dimension; ++degree) {
            phy_form *form = NULL;
            PHY_CHECK_EQ_INT(
                phy_form_create(f.manifold, 0u, "alpha", degree, &form),
                PHY_OK);
            PHY_CHECK_EQ_INT(phy_form_component_count(form),
                             expected_binomial(dimension, degree));
            PHY_CHECK_EQ_INT(phy_form_degree(form), degree);
            PHY_CHECK_EQ_INT(phy_form_dimension(form), dimension);
            PHY_CHECK_EQ_INT(phy_form_chart_index(form), 0);
            PHY_CHECK(phy_form_chart(form) == f.chart);
            PHY_CHECK(phy_form_manifold(form) == f.manifold);
            PHY_CHECK_EQ_STR(phy_form_name(form), "alpha");
            PHY_CHECK(phy_form_head(form) != PHY_IR_NO_SYMBOL);
            PHY_CHECK(phy_form_zero(form) != PHY_IR_NULL);

            /* Every component starts at the canonical zero handle. */
            const size_t count = phy_form_component_count(form);
            for (size_t position = 0u; position < count; ++position) {
                phy_ir_ref ref = PHY_IR_NULL;
                PHY_CHECK_EQ_INT(phy_form_get_at(form, position, &ref), PHY_OK);
                PHY_CHECK_EQ_INT(ref, phy_form_zero(form));
            }
            phy_form_destroy(form);
        }

        /*
         * Lambda^p above the dimension is the zero space, and this layer has
         * no object for it.
         */
        phy_form *too_big = NULL;
        PHY_CHECK_EQ_INT(
            phy_form_create(f.manifold, 0u, NULL, dimension + 1u, &too_big),
            PHY_ERR_INVALID_ARGUMENT);
        PHY_CHECK(too_big == NULL);

        close_fixture(&f);
    }
}

static void test_form_rejects(void)
{
    fixture f = open_euclidean(3u);
    phy_form *form = NULL;

    PHY_CHECK_EQ_INT(phy_form_create(NULL, 0u, NULL, 1u, &form),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_create(f.manifold, 0u, NULL, 1u, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_create(f.manifold, 1u, NULL, 1u, &form),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_create(f.manifold, 0u, "", 1u, &form),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(form == NULL);

    /* An unnamed form is the normal case for an operation result. */
    PHY_CHECK_EQ_INT(phy_form_create(f.manifold, 0u, NULL, 1u, &form), PHY_OK);
    PHY_CHECK(phy_form_name(form) == NULL);
    PHY_CHECK_EQ_INT(phy_form_head(form), PHY_IR_NO_SYMBOL);

    /* Null accessors answer rather than trap. */
    PHY_CHECK(phy_form_manifold(NULL) == NULL);
    PHY_CHECK(phy_form_chart(NULL) == NULL);
    PHY_CHECK(phy_form_name(NULL) == NULL);
    PHY_CHECK_EQ_INT(phy_form_degree(NULL), 0);
    PHY_CHECK_EQ_INT(phy_form_dimension(NULL), 0);
    PHY_CHECK_EQ_INT(phy_form_component_count(NULL), 0);
    PHY_CHECK_EQ_INT(phy_form_zero(NULL), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_form_chart_index(NULL), 0);

    /* Position access validates its range. */
    phy_ir_ref ref = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_form_get_at(form, 3u, &ref),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_get_at(form, 0u, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_set_at(f.cas, form, 3u, symbol_ref(f.ir, "q")),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_set_at(f.cas, form, 0u, PHY_IR_NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_set_at(NULL, form, 0u, symbol_ref(f.ir, "q")),
                     PHY_ERR_INVALID_ARGUMENT);

    /* A CAS over another context is detectable, and is refused. */
    phy_ir_context *other = phy_ir_context_create(NULL);
    phy_cas *foreign = phy_cas_create(other, NULL);
    PHY_CHECK_EQ_INT(phy_form_set_at(foreign, form, 0u, symbol_ref(f.ir, "q")),
                     PHY_ERR_INVALID_ARGUMENT);
    phy_cas_destroy(foreign);
    phy_ir_context_destroy(other);

    phy_form_destroy(form);
    phy_form_destroy(NULL);
    close_fixture(&f);
}

/* --------------------------------------------------- canonical positions */

static void test_position_roundtrip(void)
{
    for (unsigned dimension = 1u; dimension <= PHY_MANIFOLD_MAX_DIM;
         ++dimension) {
        fixture f = open_euclidean(dimension);

        for (unsigned degree = 0u; degree <= dimension; ++degree) {
            phy_form *form = make_form(&f, degree);
            const size_t count = phy_form_component_count(form);
            PHY_CHECK_EQ_INT(count, expected_binomial(dimension, degree));

            unsigned previous[PHY_FORM_MAX_DEGREE] = {0u, 0u, 0u, 0u};

            for (size_t position = 0u; position < count; ++position) {
                unsigned indices[PHY_FORM_MAX_DEGREE] = {0u, 0u, 0u, 0u};
                PHY_CHECK_EQ_INT(
                    phy_form_position_indices(form, position, indices), PHY_OK);

                /* Strictly increasing, in range. */
                for (unsigned slot = 0u; slot < degree; ++slot) {
                    PHY_CHECK(indices[slot] < dimension);
                    if (slot > 0u) {
                        PHY_CHECK(indices[slot - 1u] < indices[slot]);
                    }
                }

                /* Lexicographic across positions. */
                if (position > 0u && degree > 0u) {
                    bool ordered = false;
                    for (unsigned slot = 0u; slot < degree; ++slot) {
                        if (previous[slot] != indices[slot]) {
                            ordered = previous[slot] < indices[slot];
                            break;
                        }
                    }
                    PHY_CHECK(ordered);
                }
                memcpy(previous, indices, sizeof previous);

                size_t back = (size_t)-1;
                int sign = 0;
                PHY_CHECK_EQ_INT(
                    phy_form_position_of(form, indices, &back, &sign), PHY_OK);
                PHY_CHECK_EQ_INT(back, position);
                PHY_CHECK_EQ_INT(sign, 1);
            }

            /* Out of range, both directions. */
            unsigned scratch[PHY_FORM_MAX_DEGREE] = {0u, 0u, 0u, 0u};
            PHY_CHECK_EQ_INT(phy_form_position_indices(form, count, scratch),
                             PHY_ERR_INVALID_ARGUMENT);
            if (degree > 0u) {
                unsigned bad[PHY_FORM_MAX_DEGREE] = {0u, 0u, 0u, 0u};
                bad[0] = dimension;
                PHY_CHECK_EQ_INT(
                    phy_form_position_of(form, bad, NULL, NULL),
                    PHY_ERR_INVALID_ARGUMENT);
                PHY_CHECK_EQ_INT(phy_form_position_of(form, NULL, NULL, NULL),
                                 PHY_ERR_INVALID_ARGUMENT);
            }
            PHY_CHECK_EQ_INT(phy_form_position_of(NULL, scratch, NULL, NULL),
                             PHY_ERR_INVALID_ARGUMENT);

            phy_form_destroy(form);
        }
        close_fixture(&f);
    }
}

/*
 * Every tuple, not only the increasing ones: the sort parity and the position
 * it lands on are what phy_form_get and the interior product rely on.
 */
static void test_position_of_every_tuple(void)
{
    for (unsigned dimension = 2u; dimension <= PHY_MANIFOLD_MAX_DIM;
         ++dimension) {
        fixture f = open_euclidean(dimension);

        for (unsigned degree = 1u; degree <= dimension; ++degree) {
            phy_form *form = make_form(&f, degree);

            size_t total = 1u;
            for (unsigned slot = 0u; slot < degree; ++slot) {
                total *= dimension;
            }

            for (size_t code = 0u; code < total; ++code) {
                unsigned indices[PHY_FORM_MAX_DEGREE] = {0u, 0u, 0u, 0u};
                size_t rest = code;
                for (unsigned slot = degree; slot > 0u; --slot) {
                    indices[slot - 1u] = (unsigned)(rest % dimension);
                    rest /= dimension;
                }

                unsigned sorted[PHY_FORM_MAX_DEGREE];
                memcpy(sorted, indices, sizeof sorted);
                const int reference = expected_parity(indices, degree);

                size_t position = (size_t)-1;
                int sign = 99;
                PHY_CHECK_EQ_INT(
                    phy_form_position_of(form, indices, &position, &sign),
                    PHY_OK);
                PHY_CHECK_EQ_INT(sign, reference);

                if (reference == 0) {
                    /* A repeated index names no slot; the position is left
                       untouched rather than set to something plausible. */
                    PHY_CHECK_EQ_INT(position, (size_t)-1);
                    continue;
                }

                /* Sort by hand and confirm the position it lands on. */
                for (unsigned i = 1u; i < degree; ++i) {
                    for (unsigned j = i; j > 0u; --j) {
                        if (sorted[j - 1u] > sorted[j]) {
                            const unsigned swap = sorted[j - 1u];
                            sorted[j - 1u] = sorted[j];
                            sorted[j] = swap;
                        }
                    }
                }
                unsigned stored[PHY_FORM_MAX_DEGREE] = {0u, 0u, 0u, 0u};
                PHY_CHECK_EQ_INT(
                    phy_form_position_indices(form, position, stored), PHY_OK);
                PHY_CHECK_EQ_INT(memcmp(stored, sorted,
                                        degree * sizeof(unsigned)),
                                 0);
            }
            phy_form_destroy(form);
        }
        close_fixture(&f);
    }
}

/* ----------------------------------------------------- component access */

static void test_component_antisymmetry(void)
{
    fixture f = open_euclidean(3u);
    phy_form *form = generic_form(&f, 2u, "a");

    /* Position 0 is (0,1), 1 is (0,2), 2 is (1,2). */
    static const unsigned kZeroOne[2] = {0u, 1u};
    static const unsigned kOneZero[2] = {1u, 0u};
    static const unsigned kZeroZero[2] = {0u, 0u};

    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_form_get(f.cas, form, kZeroOne, &value), PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, value), "a0");

    /* Reversing the indices flips the sign; nothing is stored twice. */
    PHY_CHECK_EQ_INT(phy_form_get(f.cas, form, kOneZero, &value), PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, value), "(* -1 a0)");

    /* A repeated index reads as the canonical zero handle. */
    PHY_CHECK_EQ_INT(phy_form_get(f.cas, form, kZeroZero, &value), PHY_OK);
    PHY_CHECK_EQ_INT(value, phy_form_zero(form));

    /* Writing through a reversed tuple stores the negation. */
    PHY_CHECK_EQ_INT(
        phy_form_set(f.cas, form, kOneZero, symbol_ref(f.ir, "w")), PHY_OK);
    PHY_CHECK_EQ_STR(component(&f, form, 0u), "(* -1 w)");
    PHY_CHECK_EQ_INT(phy_form_get(f.cas, form, kOneZero, &value), PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, value), "w");

    /*
     * alpha_{aa} is zero by antisymmetry, so an assignment that says otherwise
     * contradicts the storage rather than filling it.
     */
    PHY_CHECK_EQ_INT(
        phy_form_set(f.cas, form, kZeroZero, symbol_ref(f.ir, "w")),
        PHY_ERR_ASSUMPTION);
    PHY_CHECK_EQ_INT(
        phy_form_set(f.cas, form, kZeroZero, parse(f.ir, "(+ w (* -1 w))")),
        PHY_OK);
    /* ...and the refused assignment changed nothing. */
    PHY_CHECK_EQ_STR(component(&f, form, 0u), "(* -1 w)");

    /* Components are stored in the normal form, not as written. */
    put(&f, form, 1u, "(+ (* 2 q) (* 3 q))");
    PHY_CHECK_EQ_STR(component(&f, form, 1u), "(* 5 q)");

    phy_form_clear(form);
    for (size_t position = 0u; position < phy_form_component_count(form);
         ++position) {
        phy_ir_ref ref = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(phy_form_get_at(form, position, &ref), PHY_OK);
        PHY_CHECK_EQ_INT(ref, phy_form_zero(form));
    }
    phy_form_clear(NULL);

    phy_form_destroy(form);
    close_fixture(&f);
}

static void test_form_copy_and_linear(void)
{
    fixture f = open_euclidean(3u);
    phy_form *alpha = generic_form(&f, 1u, "a");
    phy_form *beta = generic_form(&f, 1u, "b");

    phy_form *copy = NULL;
    PHY_CHECK_EQ_INT(phy_form_copy(alpha, &copy), PHY_OK);
    PHY_CHECK(phy_form_name(copy) == NULL);
    check_equal(&f, copy, alpha);
    /* Independent: mutating the copy leaves the original alone. */
    put(&f, copy, 0u, "0");
    PHY_CHECK_EQ_STR(component(&f, alpha, 0u), "a0");

    phy_form *sum = NULL;
    phy_form *difference = NULL;
    PHY_CHECK_EQ_INT(phy_form_add(f.cas, alpha, beta, &sum), PHY_OK);
    PHY_CHECK_EQ_INT(phy_form_sub(f.cas, alpha, beta, &difference), PHY_OK);
    PHY_CHECK_EQ_STR(component(&f, sum, 0u), "(+ a0 b0)");
    PHY_CHECK_EQ_STR(component(&f, difference, 0u), "(+ a0 (* -1 b0))");

    /* (alpha + beta) - beta == alpha, decided rather than compared as text. */
    phy_form *back = NULL;
    PHY_CHECK_EQ_INT(phy_form_sub(f.cas, sum, beta, &back), PHY_OK);
    check_equal(&f, back, alpha);

    phy_form *scaled = NULL;
    PHY_CHECK_EQ_INT(
        phy_form_scale(f.cas, alpha, parse(f.ir, "3"), &scaled), PHY_OK);
    PHY_CHECK_EQ_STR(component(&f, scaled, 0u), "(* 3 a0)");

    /* alpha - alpha vanishes, and phy_form_is_zero says so. */
    phy_form *empty = NULL;
    PHY_CHECK_EQ_INT(phy_form_sub(f.cas, alpha, alpha, &empty), PHY_OK);
    check_vanishes(&f, empty);

    /*
     * Two forms differing by a nonzero number are proved different. Two
     * differing by a symbolic amount are not: phy_cas_is_zero proves nonzero
     * only for an exact number or a product of factors declared nonzero, so
     * a0 - b0 is honestly UNKNOWN and this layer reports that rather than
     * upgrading it.
     */
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_form_equivalent(f.cas, alpha, beta, &decision),
                     PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_UNKNOWN);

    phy_form *one = make_form(&f, 1u);
    phy_form *two = make_form(&f, 1u);
    put(&f, one, 0u, "1");
    put(&f, two, 0u, "2");
    PHY_CHECK_EQ_INT(phy_form_equivalent(f.cas, one, two, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_NONZERO);
    PHY_CHECK_EQ_INT(phy_form_equivalent(f.cas, one, one, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(phy_form_is_zero(f.cas, one, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_NONZERO);
    phy_form_destroy(two);
    phy_form_destroy(one);

    /* Degree mismatch is a type error, not a decision. */
    phy_form *two_form = generic_form(&f, 2u, "c");
    PHY_CHECK_EQ_INT(phy_form_equivalent(f.cas, alpha, two_form, &decision),
                     PHY_ERR_TYPE);
    phy_form *unused = NULL;
    PHY_CHECK_EQ_INT(phy_form_add(f.cas, alpha, two_form, &unused),
                     PHY_ERR_TYPE);
    PHY_CHECK(unused == NULL);

    phy_form_destroy(two_form);
    phy_form_destroy(empty);
    phy_form_destroy(scaled);
    phy_form_destroy(back);
    phy_form_destroy(difference);
    phy_form_destroy(sum);
    phy_form_destroy(copy);
    phy_form_destroy(beta);
    phy_form_destroy(alpha);
    close_fixture(&f);
}

/*
 * Two forms on different charts of the same manifold are not comparable, and
 * this layer refuses rather than identifying them. That refusal is what stands
 * in for the pullback that include/phy/geom.h defers.
 */
static void test_charts_do_not_mix(void)
{
    fixture f = open_euclidean(2u);

    static const char *const kOther[2] = {"u", "v"};
    phy_chart *second = NULL;
    PHY_CHECK_EQ_INT(phy_chart_create(f.ir, kOther, 2u, &second), PHY_OK);
    unsigned index = 0u;
    PHY_CHECK_EQ_INT(phy_manifold_add_chart(f.manifold, second, &index),
                     PHY_OK);
    PHY_CHECK_EQ_INT(index, 1);

    phy_form *here = generic_form(&f, 1u, "a");
    phy_form *there = NULL;
    PHY_CHECK_EQ_INT(phy_form_create(f.manifold, 1u, NULL, 1u, &there), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_form_set_at(f.cas, there, 0u, symbol_ref(f.ir, "b0")), PHY_OK);

    phy_form *result = NULL;
    PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, here, there, &result),
                     PHY_ERR_TYPE);
    PHY_CHECK(result == NULL);
    PHY_CHECK_EQ_INT(phy_form_add(f.cas, here, there, &result), PHY_ERR_TYPE);
    PHY_CHECK(result == NULL);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_form_equivalent(f.cas, here, there, &decision),
                     PHY_ERR_TYPE);

    phy_form_destroy(there);
    phy_form_destroy(here);
    phy_chart_destroy(second);
    close_fixture(&f);
}

/* ------------------------------------------------------- wedge: examples */

static void test_wedge_r2_r3_examples(void)
{
    {
        /* (a dx + b dy) ^ (c dx + e dy) = (a e - b c) dx ^ dy. */
        fixture f = open_euclidean(2u);
        phy_form *alpha = make_form(&f, 1u);
        phy_form *beta = make_form(&f, 1u);
        put(&f, alpha, 0u, "a");
        put(&f, alpha, 1u, "b");
        put(&f, beta, 0u, "c");
        put(&f, beta, 1u, "e");

        phy_form *product = NULL;
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, alpha, beta, &product), PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_degree(product), 2);
        PHY_CHECK_EQ_INT(phy_form_component_count(product), 1);

        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        phy_ir_ref value = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(phy_form_get_at(product, 0u, &value), PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_cas_equivalent(f.cas, value,
                               parse(f.ir, "(+ (* a e) (* -1 b c))"),
                               &decision),
            PHY_OK);
        PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);

        /* dx ^ dx = 0, which is the storage rather than a cancellation. */
        phy_form *square = NULL;
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, alpha, alpha, &square), PHY_OK);
        check_vanishes(&f, square);

        phy_form_destroy(square);
        phy_form_destroy(product);
        phy_form_destroy(beta);
        phy_form_destroy(alpha);
        close_fixture(&f);
    }
    {
        /* dx ^ (dy ^ dz) = dx ^ dy ^ dz, and a 0-form scales. */
        fixture f = open_euclidean(3u);
        phy_form *dx = make_form(&f, 1u);
        put(&f, dx, 0u, "1");
        phy_form *dydz = make_form(&f, 2u);
        put(&f, dydz, 2u, "1"); /* position 2 of C(3,2) is (1,2) */

        phy_form *volume = NULL;
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, dx, dydz, &volume), PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_degree(volume), 3);
        PHY_CHECK_EQ_STR(component(&f, volume, 0u), "1");

        /* dy ^ (dx ^ dz) picks up the transposition sign. */
        phy_form *dy = make_form(&f, 1u);
        put(&f, dy, 1u, "1");
        phy_form *dxdz = make_form(&f, 2u);
        put(&f, dxdz, 1u, "1"); /* position 1 is (0,2) */
        phy_form *swapped = NULL;
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, dy, dxdz, &swapped), PHY_OK);
        PHY_CHECK_EQ_STR(component(&f, swapped, 0u), "-1");

        /* A 0-form wedge is multiplication. */
        phy_form *scalar = make_form(&f, 0u);
        put(&f, scalar, 0u, "(* 3 s)");
        phy_form *scaled = NULL;
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, scalar, dx, &scaled), PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_degree(scaled), 1);
        PHY_CHECK_EQ_STR(component(&f, scaled, 0u), "(* 3 s)");

        phy_form_destroy(scaled);
        phy_form_destroy(scalar);
        phy_form_destroy(swapped);
        phy_form_destroy(dxdz);
        phy_form_destroy(dy);
        phy_form_destroy(volume);
        phy_form_destroy(dydz);
        phy_form_destroy(dx);
        close_fixture(&f);
    }
}

static void test_wedge_graded_commutativity(void)
{
    for (unsigned dimension = 2u; dimension <= PHY_MANIFOLD_MAX_DIM;
         ++dimension) {
        fixture f = open_euclidean(dimension);

        for (unsigned p = 0u; p <= dimension; ++p) {
            for (unsigned q = 0u; p + q <= dimension; ++q) {
                phy_form *alpha = generic_form(&f, p, "a");
                phy_form *beta = generic_form(&f, q, "b");

                phy_form *forward = NULL;
                phy_form *backward = NULL;
                PHY_CHECK_EQ_INT(
                    phy_form_wedge(f.cas, alpha, beta, &forward), PHY_OK);
                PHY_CHECK_EQ_INT(
                    phy_form_wedge(f.cas, beta, alpha, &backward), PHY_OK);
                PHY_CHECK_EQ_INT(phy_form_degree(forward), p + q);

                check_equal_signed(&f, forward, backward,
                                   ((p * q) % 2u == 0u) ? 1 : -1);

                phy_form_destroy(backward);
                phy_form_destroy(forward);
                phy_form_destroy(beta);
                phy_form_destroy(alpha);
            }
        }
        close_fixture(&f);
    }
}

static void test_wedge_associativity(void)
{
    fixture f = open_euclidean(4u);

    static const unsigned kDegrees[4][3] = {
        {0u, 1u, 1u}, {1u, 1u, 1u}, {1u, 1u, 2u}, {2u, 1u, 1u}};

    for (unsigned trial = 0u; trial < 4u; ++trial) {
        phy_form *alpha = generic_form(&f, kDegrees[trial][0], "a");
        phy_form *beta = generic_form(&f, kDegrees[trial][1], "b");
        phy_form *gamma = generic_form(&f, kDegrees[trial][2], "c");

        phy_form *left_pair = NULL;
        phy_form *right_pair = NULL;
        phy_form *left = NULL;
        phy_form *right = NULL;
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, alpha, beta, &left_pair),
                         PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, left_pair, gamma, &left),
                         PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, beta, gamma, &right_pair),
                         PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, alpha, right_pair, &right),
                         PHY_OK);
        check_equal(&f, left, right);

        phy_form_destroy(right);
        phy_form_destroy(right_pair);
        phy_form_destroy(left);
        phy_form_destroy(left_pair);
        phy_form_destroy(gamma);
        phy_form_destroy(beta);
        phy_form_destroy(alpha);
    }
    close_fixture(&f);
}

static void test_wedge_domain(void)
{
    fixture f = open_euclidean(2u);
    phy_form *alpha = generic_form(&f, 1u, "a");
    phy_form *beta = generic_form(&f, 2u, "b");

    /*
     * The product is zero, not undefined. Lambda^3 on a surface is the zero
     * space, so there is no object of that degree to hand back and the caller
     * is told.
     */
    phy_form *result = NULL;
    PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, alpha, beta, &result),
                     PHY_ERR_DOMAIN);
    PHY_CHECK(result == NULL);
    PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, beta, beta, &result),
                     PHY_ERR_DOMAIN);
    PHY_CHECK(result == NULL);

    PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, alpha, beta, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, NULL, beta, &result),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_wedge(NULL, alpha, alpha, &result),
                     PHY_ERR_INVALID_ARGUMENT);

    phy_form_destroy(beta);
    phy_form_destroy(alpha);
    close_fixture(&f);
}

/* ------------------------------------------ exterior derivative: examples */

static void test_exterior_derivative_examples(void)
{
    {
        /* d(x y) = y dx + x dy. */
        fixture f = open_euclidean(2u);
        phy_form *scalar = make_form(&f, 0u);
        put(&f, scalar, 0u, "(* x y)");

        phy_form *gradient = NULL;
        PHY_CHECK_EQ_INT(
            phy_form_exterior_derivative(f.cas, scalar, &gradient), PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_degree(gradient), 1);
        PHY_CHECK_EQ_STR(component(&f, gradient, 0u), "y");
        PHY_CHECK_EQ_STR(component(&f, gradient, 1u), "x");

        /* A constant differentiates to the zero form. */
        phy_form *constant = make_form(&f, 0u);
        put(&f, constant, 0u, "7");
        phy_form *zero = NULL;
        PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, constant, &zero),
                         PHY_OK);
        check_vanishes(&f, zero);

        phy_form_destroy(zero);
        phy_form_destroy(constant);
        phy_form_destroy(gradient);
        phy_form_destroy(scalar);
        close_fixture(&f);
    }
    {
        /* d(-y dx + x dy) = 2 dx ^ dy, the standard rotation field. */
        fixture f = open_euclidean(3u);
        phy_form *alpha = make_form(&f, 1u);
        put(&f, alpha, 0u, "(* -1 y)");
        put(&f, alpha, 1u, "x");
        put(&f, alpha, 2u, "0");

        phy_form *curl = NULL;
        PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, alpha, &curl),
                         PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_degree(curl), 2);
        PHY_CHECK_EQ_STR(component(&f, curl, 0u), "2"); /* (0,1) */
        PHY_CHECK_EQ_STR(component(&f, curl, 1u), "0"); /* (0,2) */
        PHY_CHECK_EQ_STR(component(&f, curl, 2u), "0"); /* (1,2) */

        /* y z dx + z x dy + x y dz is d(x y z), so its curl vanishes. */
        phy_form *exact = make_form(&f, 1u);
        put(&f, exact, 0u, "(* y z)");
        put(&f, exact, 1u, "(* x z)");
        put(&f, exact, 2u, "(* x y)");
        phy_form *closed = NULL;
        PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, exact, &closed),
                         PHY_OK);
        check_vanishes(&f, closed);

        /* ...and it really is d of the product, componentwise. */
        phy_form *potential = make_form(&f, 0u);
        put(&f, potential, 0u, "(* x y z)");
        phy_form *gradient = NULL;
        PHY_CHECK_EQ_INT(
            phy_form_exterior_derivative(f.cas, potential, &gradient), PHY_OK);
        check_equal(&f, gradient, exact);

        phy_form_destroy(gradient);
        phy_form_destroy(potential);
        phy_form_destroy(closed);
        phy_form_destroy(exact);
        phy_form_destroy(curl);
        phy_form_destroy(alpha);
        close_fixture(&f);
    }
}

static void test_d_squared_is_zero(void)
{
    for (unsigned dimension = 2u; dimension <= PHY_MANIFOLD_MAX_DIM;
         ++dimension) {
        fixture f = open_euclidean(dimension);

        /* Every degree whose second derivative is representable. */
        for (unsigned degree = 0u; degree + 2u <= dimension; ++degree) {
            phy_form *alpha = smooth_form(&f, degree, (int)dimension);

            phy_form *first = NULL;
            phy_form *second = NULL;
            PHY_CHECK_EQ_INT(
                phy_form_exterior_derivative(f.cas, alpha, &first), PHY_OK);
            PHY_CHECK_EQ_INT(
                phy_form_exterior_derivative(f.cas, first, &second), PHY_OK);
            PHY_CHECK_EQ_INT(phy_form_degree(second), degree + 2u);

            /*
             * Proved zero by phy_cas_is_zero, which is a proof and not an
             * estimate on this class -- see include/phy/cas.h.
             *
             * Not *structurally* zero in general, and that is a property of
             * the normal form rather than of this layer. Components are
             * simplified, never expanded, so when a mixed partial is itself a
             * sum S the component is S + (-1)*S with S's terms flattened into
             * the outer sum and the negation left as a product: cancelling it
             * needs the distribution that only phy_cas_expand and the zero
             * decision perform. test_d_squared_is_structural below pins the
             * monomial case, where it does collapse on the nose.
             */
            check_vanishes(&f, second);

            phy_form_destroy(second);
            phy_form_destroy(first);
            phy_form_destroy(alpha);
        }
        close_fixture(&f);
    }
}

/*
 * Where the cancellation is structural: components that are single monomials,
 * whose partial derivatives are single monomials too, so the two orders of a
 * mixed partial reach the same interned node and the sum collapses to the
 * canonical zero handle without any distribution.
 */
static void test_d_squared_is_structural(void)
{
    fixture f = open_euclidean(3u);
    phy_form *alpha = make_form(&f, 0u);
    put(&f, alpha, 0u, "(* 5 (^ x 2) (^ y 3) z)");

    phy_form *first = NULL;
    phy_form *second = NULL;
    PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, alpha, &first),
                     PHY_OK);
    PHY_CHECK_EQ_STR(component(&f, first, 0u), "(* 10 x z (^ y 3))");
    PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, first, &second),
                     PHY_OK);

    const size_t count = phy_form_component_count(second);
    for (size_t position = 0u; position < count; ++position) {
        phy_ir_ref ref = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(phy_form_get_at(second, position, &ref), PHY_OK);
        if (ref != phy_form_zero(second)) {
            fprintf(stderr, "  d^2 residue at position %u: %s\n",
                    (unsigned)position, render(f.ir, ref));
        }
        PHY_CHECK_EQ_INT(ref, phy_form_zero(second));
    }

    phy_form_destroy(second);
    phy_form_destroy(first);
    phy_form_destroy(alpha);
    close_fixture(&f);
}

/*
 * The boundary of the nilpotence above, pinned rather than asserted away.
 *
 * A component the CAS must defer on becomes a PHY_IR_DERIVATIVE whose
 * variables are in application order, and the IR does not identify two orders
 * -- mixed partials commute only for sufficiently smooth expressions, and
 * include/phy/ir.h puts that assumption in the rewriter. So d^2 of such a
 * component is a difference of two distinct nodes, and this layer reports it
 * as such instead of quietly returning zero.
 */
static void test_d_squared_defers_on_opaque_components(void)
{
    fixture f = open_euclidean(2u);
    phy_form *alpha = make_form(&f, 0u);
    /* An unknown function of both coordinates. */
    put(&f, alpha, 0u, "(fn u x y)");

    phy_form *first = NULL;
    phy_form *second = NULL;
    PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, alpha, &first),
                     PHY_OK);
    PHY_CHECK_EQ_STR(component(&f, first, 0u), "(d (fn u x y) x)");
    PHY_CHECK_EQ_STR(component(&f, first, 1u), "(d (fn u x y) y)");

    PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, first, &second),
                     PHY_OK);
    phy_ir_ref residue = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_form_get_at(second, 0u, &residue), PHY_OK);
    PHY_CHECK(residue != phy_form_zero(second));

    phy_cas_decision decision = PHY_CAS_ZERO;
    PHY_CHECK_EQ_INT(phy_form_is_zero(f.cas, second, &decision), PHY_OK);
    PHY_CHECK(decision != PHY_CAS_ZERO);

    phy_form_destroy(second);
    phy_form_destroy(first);
    phy_form_destroy(alpha);
    close_fixture(&f);
}

static void test_graded_leibniz(void)
{
    for (unsigned dimension = 2u; dimension <= PHY_MANIFOLD_MAX_DIM;
         ++dimension) {
        fixture f = open_euclidean(dimension);

        for (unsigned p = 0u; p <= dimension; ++p) {
            for (unsigned q = 0u; p + q + 1u <= dimension; ++q) {
                phy_form *alpha = smooth_form(&f, p, 1);
                phy_form *beta = smooth_form(&f, q, 2);

                phy_form *product = NULL;
                phy_form *left = NULL;
                PHY_CHECK_EQ_INT(
                    phy_form_wedge(f.cas, alpha, beta, &product), PHY_OK);
                PHY_CHECK_EQ_INT(
                    phy_form_exterior_derivative(f.cas, product, &left),
                    PHY_OK);

                phy_form *d_alpha = NULL;
                phy_form *d_beta = NULL;
                PHY_CHECK_EQ_INT(
                    phy_form_exterior_derivative(f.cas, alpha, &d_alpha),
                    PHY_OK);
                PHY_CHECK_EQ_INT(
                    phy_form_exterior_derivative(f.cas, beta, &d_beta),
                    PHY_OK);

                phy_form *first = NULL;
                phy_form *second = NULL;
                PHY_CHECK_EQ_INT(
                    phy_form_wedge(f.cas, d_alpha, beta, &first), PHY_OK);
                PHY_CHECK_EQ_INT(
                    phy_form_wedge(f.cas, alpha, d_beta, &second), PHY_OK);

                phy_form *signed_second = NULL;
                phy_ir_ref sign = PHY_IR_NULL;
                PHY_CHECK_EQ_INT(
                    phy_cas_number(f.cas, (p % 2u == 0u) ? 1 : -1, 1, &sign),
                    PHY_OK);
                PHY_CHECK_EQ_INT(
                    phy_form_scale(f.cas, second, sign, &signed_second),
                    PHY_OK);

                phy_form *right = NULL;
                PHY_CHECK_EQ_INT(
                    phy_form_add(f.cas, first, signed_second, &right), PHY_OK);
                check_equal(&f, left, right);

                phy_form_destroy(right);
                phy_form_destroy(signed_second);
                phy_form_destroy(second);
                phy_form_destroy(first);
                phy_form_destroy(d_beta);
                phy_form_destroy(d_alpha);
                phy_form_destroy(left);
                phy_form_destroy(product);
                phy_form_destroy(beta);
                phy_form_destroy(alpha);
            }
        }
        close_fixture(&f);
    }
}

static void test_exterior_derivative_domain(void)
{
    fixture f = open_euclidean(2u);
    phy_form *top = generic_form(&f, 2u, "a");
    phy_form *result = NULL;

    /* d of an n-form vanishes; Lambda^{n+1} is the zero space. */
    PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, top, &result),
                     PHY_ERR_DOMAIN);
    PHY_CHECK(result == NULL);
    PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, NULL, &result),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_exterior_derivative(f.cas, top, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_exterior_derivative(NULL, top, &result),
                     PHY_ERR_INVALID_ARGUMENT);

    phy_form_destroy(top);
    close_fixture(&f);
}

/* --------------------------------------------------- the interior product */

static void test_interior_product_examples(void)
{
    fixture f = open_euclidean(2u);
    phy_form *area = make_form(&f, 2u);
    put(&f, area, 0u, "1"); /* dx ^ dy */

    phy_tensor *v = make_vector(&f, "v");

    phy_form *contracted = NULL;
    PHY_CHECK_EQ_INT(
        phy_form_interior_product(f.cas, area, v, &contracted), PHY_OK);
    PHY_CHECK_EQ_INT(phy_form_degree(contracted), 1);
    /* iota_v (dx ^ dy) = v^0 dy - v^1 dx. */
    PHY_CHECK_EQ_STR(component(&f, contracted, 0u), "(* -1 v1)");
    PHY_CHECK_EQ_STR(component(&f, contracted, 1u), "v0");

    /* On a 1-form it is the pairing v^i alpha_i. */
    phy_form *alpha = generic_form(&f, 1u, "a");
    phy_form *pairing = NULL;
    PHY_CHECK_EQ_INT(phy_form_interior_product(f.cas, alpha, v, &pairing),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_form_degree(pairing), 0);
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_form_get_at(pairing, 0u, &value), PHY_OK);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(f.cas, value,
                           parse(f.ir, "(+ (* v0 a0) (* v1 a1))"), &decision),
        PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);

    phy_form_destroy(pairing);
    phy_form_destroy(alpha);
    phy_form_destroy(contracted);
    phy_tensor_destroy(v);
    phy_form_destroy(area);
    close_fixture(&f);
}

static void test_interior_product_is_nilpotent(void)
{
    for (unsigned dimension = 2u; dimension <= PHY_MANIFOLD_MAX_DIM;
         ++dimension) {
        fixture f = open_euclidean(dimension);
        phy_tensor *v = make_vector(&f, "v");

        for (unsigned degree = 2u; degree <= dimension; ++degree) {
            phy_form *alpha = generic_form(&f, degree, "a");
            phy_form *once = NULL;
            phy_form *twice = NULL;
            PHY_CHECK_EQ_INT(
                phy_form_interior_product(f.cas, alpha, v, &once), PHY_OK);
            PHY_CHECK_EQ_INT(
                phy_form_interior_product(f.cas, once, v, &twice), PHY_OK);
            PHY_CHECK_EQ_INT(phy_form_degree(twice), degree - 2u);
            check_vanishes(&f, twice);

            phy_form_destroy(twice);
            phy_form_destroy(once);
            phy_form_destroy(alpha);
        }
        phy_tensor_destroy(v);
        close_fixture(&f);
    }
}

static void test_interior_product_is_an_antiderivation(void)
{
    for (unsigned dimension = 3u; dimension <= PHY_MANIFOLD_MAX_DIM;
         ++dimension) {
        fixture f = open_euclidean(dimension);
        phy_tensor *v = make_vector(&f, "v");

        for (unsigned p = 1u; p <= dimension; ++p) {
            for (unsigned q = 1u; p + q <= dimension; ++q) {
                phy_form *alpha = generic_form(&f, p, "a");
                phy_form *beta = generic_form(&f, q, "b");

                phy_form *product = NULL;
                phy_form *left = NULL;
                PHY_CHECK_EQ_INT(
                    phy_form_wedge(f.cas, alpha, beta, &product), PHY_OK);
                PHY_CHECK_EQ_INT(
                    phy_form_interior_product(f.cas, product, v, &left),
                    PHY_OK);

                phy_form *iota_alpha = NULL;
                phy_form *iota_beta = NULL;
                PHY_CHECK_EQ_INT(
                    phy_form_interior_product(f.cas, alpha, v, &iota_alpha),
                    PHY_OK);
                PHY_CHECK_EQ_INT(
                    phy_form_interior_product(f.cas, beta, v, &iota_beta),
                    PHY_OK);

                phy_form *first = NULL;
                phy_form *second = NULL;
                PHY_CHECK_EQ_INT(
                    phy_form_wedge(f.cas, iota_alpha, beta, &first), PHY_OK);
                PHY_CHECK_EQ_INT(
                    phy_form_wedge(f.cas, alpha, iota_beta, &second), PHY_OK);

                phy_form *signed_second = NULL;
                phy_ir_ref sign = PHY_IR_NULL;
                PHY_CHECK_EQ_INT(
                    phy_cas_number(f.cas, (p % 2u == 0u) ? 1 : -1, 1, &sign),
                    PHY_OK);
                PHY_CHECK_EQ_INT(
                    phy_form_scale(f.cas, second, sign, &signed_second),
                    PHY_OK);

                phy_form *right = NULL;
                PHY_CHECK_EQ_INT(
                    phy_form_add(f.cas, first, signed_second, &right), PHY_OK);
                check_equal(&f, left, right);

                phy_form_destroy(right);
                phy_form_destroy(signed_second);
                phy_form_destroy(second);
                phy_form_destroy(first);
                phy_form_destroy(iota_beta);
                phy_form_destroy(iota_alpha);
                phy_form_destroy(left);
                phy_form_destroy(product);
                phy_form_destroy(beta);
                phy_form_destroy(alpha);
            }
        }
        phy_tensor_destroy(v);
        close_fixture(&f);
    }
}

static void test_interior_product_rejects(void)
{
    fixture f = open_euclidean(3u);
    phy_form *alpha = generic_form(&f, 2u, "a");
    phy_form *scalar = generic_form(&f, 0u, "s");
    phy_tensor *v = make_vector(&f, "v");
    phy_form *result = NULL;

    /* A 0-form has nothing to contract. */
    PHY_CHECK_EQ_INT(phy_form_interior_product(f.cas, scalar, v, &result),
                     PHY_ERR_DOMAIN);
    PHY_CHECK(result == NULL);

    /* A covariant slot is not a vector field. */
    phy_tensor *covector = NULL;
    PHY_CHECK_EQ_INT(phy_tensor_create(f.chart, "w", 1u, kLower, &covector),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_form_interior_product(f.cas, alpha, covector, &result),
                     PHY_ERR_TYPE);

    /* Nor is a rank-2 tensor. */
    static const phy_ir_variance kTwoUpper[2] = {PHY_IR_INDEX_UPPER,
                                                 PHY_IR_INDEX_UPPER};
    phy_tensor *rank2 = NULL;
    PHY_CHECK_EQ_INT(phy_tensor_create(f.chart, "T", 2u, kTwoUpper, &rank2),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_form_interior_product(f.cas, alpha, rank2, &result),
                     PHY_ERR_TYPE);

    /* Nor is a vector field on another chart. */
    static const char *const kOther[3] = {"u", "v", "w"};
    phy_chart *second = NULL;
    PHY_CHECK_EQ_INT(phy_chart_create(f.ir, kOther, 3u, &second), PHY_OK);
    phy_tensor *elsewhere = NULL;
    PHY_CHECK_EQ_INT(phy_tensor_create(second, "e", 1u, kUpper, &elsewhere),
                     PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_form_interior_product(f.cas, alpha, elsewhere, &result),
        PHY_ERR_TYPE);

    PHY_CHECK_EQ_INT(phy_form_interior_product(f.cas, alpha, NULL, &result),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_interior_product(f.cas, NULL, v, &result),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_interior_product(f.cas, alpha, v, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(result == NULL);

    phy_tensor_destroy(elsewhere);
    phy_chart_destroy(second);
    phy_tensor_destroy(rank2);
    phy_tensor_destroy(covector);
    phy_tensor_destroy(v);
    phy_form_destroy(scalar);
    phy_form_destroy(alpha);
    close_fixture(&f);
}

/* -------------------------------------------------------------- the star */

static void test_hodge_examples(void)
{
    {
        /* Euclidean R^2: *(a dx + b dy) = -b dx + a dy. */
        fixture f = open_euclidean(2u);
        phy_form *alpha = make_form(&f, 1u);
        put(&f, alpha, 0u, "a");
        put(&f, alpha, 1u, "b");

        phy_form *dual = NULL;
        PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, alpha, &dual), PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_degree(dual), 1);
        PHY_CHECK_EQ_STR(component(&f, dual, 0u), "(* -1 b)");
        PHY_CHECK_EQ_STR(component(&f, dual, 1u), "a");

        /* *1 is the volume form. */
        phy_form *one = make_form(&f, 0u);
        put(&f, one, 0u, "1");
        phy_form *star_one = NULL;
        phy_form *volume = NULL;
        PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, one, &star_one), PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_volume(f.cas, f.manifold, 0u, &volume),
                         PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_degree(volume), 2);
        PHY_CHECK_EQ_STR(component(&f, volume, 0u), "1");
        check_equal(&f, star_one, volume);

        phy_form_destroy(volume);
        phy_form_destroy(star_one);
        phy_form_destroy(one);
        phy_form_destroy(dual);
        phy_form_destroy(alpha);
        close_fixture(&f);
    }
    {
        /* Euclidean R^3: *dx = dy ^ dz, *dy = -dx ^ dz, *dz = dx ^ dy. */
        fixture f = open_euclidean(3u);
        static const char *const kExpected[3][3] = {
            {"0", "0", "1"}, {"0", "-1", "0"}, {"1", "0", "0"}};

        for (unsigned axis = 0u; axis < 3u; ++axis) {
            phy_form *basis = make_form(&f, 1u);
            put(&f, basis, axis, "1");
            phy_form *dual = NULL;
            PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, basis, &dual), PHY_OK);
            PHY_CHECK_EQ_INT(phy_form_degree(dual), 2);
            for (unsigned position = 0u; position < 3u; ++position) {
                PHY_CHECK_EQ_STR(component(&f, dual, position),
                                 kExpected[axis][position]);
            }
            phy_form_destroy(dual);
            phy_form_destroy(basis);
        }
        close_fixture(&f);
    }
    {
        /*
         * Lorentzian R^2 with signature (-,+). Raising the time index is a
         * sign, so *(a dt + b dx) = -b dt - a dx.
         */
        fixture f = open_fixture(2u, PHY_ORIENTATION_POSITIVE, kLorentz2);
        phy_form *alpha = make_form(&f, 1u);
        put(&f, alpha, 0u, "a");
        put(&f, alpha, 1u, "b");

        phy_form *dual = NULL;
        PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, alpha, &dual), PHY_OK);
        PHY_CHECK_EQ_STR(component(&f, dual, 0u), "(* -1 b)");
        PHY_CHECK_EQ_STR(component(&f, dual, 1u), "(* -1 a)");

        phy_form_destroy(dual);
        phy_form_destroy(alpha);
        close_fixture(&f);
    }
}

/*
 * ** = (-1)^{p(n-p)} sign(det g), at every degree, in both signatures, and in
 * both orientations. This is the test the whole Hodge convention rests on: an
 * epsilon with the wrong sign or an index raised with the wrong signature
 * survives every other check in this file and fails this one.
 */
static void test_hodge_star_square(void)
{
    typedef struct {
        unsigned dimension;
        const int8_t *signature;
        phy_orientation orientation;
        int metric_sign;
    } case_t;

    static const case_t kCases[] = {
        {2u, NULL, PHY_ORIENTATION_POSITIVE, 1},
        {2u, NULL, PHY_ORIENTATION_NEGATIVE, 1},
        {2u, kLorentz2, PHY_ORIENTATION_POSITIVE, -1},
        {2u, kLorentz2, PHY_ORIENTATION_NEGATIVE, -1},
        {3u, NULL, PHY_ORIENTATION_POSITIVE, 1},
        {4u, kEuclidean4, PHY_ORIENTATION_POSITIVE, 1},
        {4u, kLorentz4, PHY_ORIENTATION_POSITIVE, -1},
        {4u, kLorentz4, PHY_ORIENTATION_NEGATIVE, -1},
    };

    for (size_t index = 0u; index < sizeof kCases / sizeof kCases[0];
         ++index) {
        const case_t *entry = &kCases[index];
        fixture f =
            open_fixture(entry->dimension, entry->orientation, entry->signature);
        PHY_CHECK_EQ_INT(phy_manifold_metric_sign(f.manifold),
                         entry->metric_sign);

        for (unsigned p = 0u; p <= entry->dimension; ++p) {
            phy_form *alpha = generic_form(&f, p, "a");

            phy_form *once = NULL;
            phy_form *twice = NULL;
            PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, alpha, &once), PHY_OK);
            PHY_CHECK_EQ_INT(phy_form_degree(once), entry->dimension - p);
            PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, once, &twice), PHY_OK);
            PHY_CHECK_EQ_INT(phy_form_degree(twice), p);

            const unsigned exponent = p * (entry->dimension - p);
            const int expected =
                ((exponent % 2u == 0u) ? 1 : -1) * entry->metric_sign;
            check_equal_signed(&f, twice, alpha, expected);

            phy_form_destroy(twice);
            phy_form_destroy(once);
            phy_form_destroy(alpha);
        }
        close_fixture(&f);
    }
}

static void test_hodge_needs_an_orientation(void)
{
    fixture f = open_fixture(3u, PHY_ORIENTATION_NONE, NULL);
    phy_form *alpha = generic_form(&f, 1u, "a");
    phy_form *result = NULL;

    /*
     * *alpha is not merely unknown on an unoriented manifold; it does not
     * exist. So this is an assumption failure, not an unsupported operation.
     */
    PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, alpha, &result),
                     PHY_ERR_ASSUMPTION);
    PHY_CHECK(result == NULL);
    PHY_CHECK_EQ_INT(phy_form_volume(f.cas, f.manifold, 0u, &result),
                     PHY_ERR_ASSUMPTION);
    PHY_CHECK(result == NULL);

    PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, NULL, &result),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, alpha, NULL),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_volume(f.cas, NULL, 0u, &result),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_form_volume(f.cas, f.manifold, 0u, NULL),
                     PHY_ERR_INVALID_ARGUMENT);

    phy_form_destroy(alpha);
    close_fixture(&f);
}

static void test_volume_form(void)
{
    fixture f = open_fixture(4u, PHY_ORIENTATION_NEGATIVE, kLorentz4);
    phy_form *volume = NULL;
    PHY_CHECK_EQ_INT(phy_form_volume(f.cas, f.manifold, 0u, &volume), PHY_OK);
    PHY_CHECK_EQ_INT(phy_form_degree(volume), 4);
    PHY_CHECK_EQ_INT(phy_form_component_count(volume), 1);
    PHY_CHECK_EQ_STR(component(&f, volume, 0u), "-1");

    /* A chart index that does not exist is refused before anything is built. */
    phy_form *bad = NULL;
    PHY_CHECK_EQ_INT(phy_form_volume(f.cas, f.manifold, 3u, &bad),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(bad == NULL);

    phy_form_destroy(volume);
    close_fixture(&f);
}

/* --------------------------------------------------- bounds and failures */

static bool always_cancel(void *user)
{
    (*(unsigned *)user)++;
    return true;
}

/*
 * The CAS owns the budget and the cancellation hook, and this layer neither
 * adds one nor swallows theirs. What is checked here is the propagation: a
 * refused scalar operation comes out of a form operation as the same typed
 * status, with no half-built form handed back and no operand touched.
 */
static void test_cancellation_and_budget(void)
{
    {
        /* Cancellation is polled every 256 steps, so it needs real work: the
           zero decision expands, which is where a notebook cell would be
           interrupted. */
        fixture f = open_euclidean(3u);
        phy_form *heavy = make_form(&f, 1u);
        phy_form *other = make_form(&f, 1u);
        put(&f, heavy, 0u, "(^ (+ a b c d) 8)");
        put(&f, other, 0u, "(^ (+ a b c e) 8)");

        unsigned polls = 0u;
        phy_cas_set_cancel(f.cas, always_cancel, &polls);

        phy_cas_decision decision = PHY_CAS_ZERO;
        PHY_CHECK_EQ_INT(phy_form_is_zero(f.cas, heavy, &decision),
                         PHY_ERR_INTERRUPTED);
        PHY_CHECK(polls > 0u);
        /* The out parameter is untouched by a failed decision. */
        PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
        PHY_CHECK_EQ_INT(phy_form_equivalent(f.cas, heavy, other, &decision),
                         PHY_ERR_INTERRUPTED);
        PHY_CHECK_EQ_INT(phy_cas_validate(f.cas), PHY_OK);
        phy_form_destroy(other);

        phy_cas_set_cancel(f.cas, NULL, NULL);
        PHY_CHECK_EQ_INT(phy_form_is_zero(f.cas, heavy, &decision), PHY_OK);
        PHY_CHECK(decision != PHY_CAS_ZERO);

        phy_form_destroy(heavy);
        close_fixture(&f);
    }
    {
        /*
         * A step budget too small for the rewrite. Two CAS objects over one
         * context: the form is built with the ordinary one, then handed to the
         * starved one, so what is measured is the operation and not the
         * construction.
         */
        phy_ir_context *ir = phy_ir_context_create(NULL);
        phy_cas *full = phy_cas_create(ir, NULL);
        phy_cas_limits limits;
        memset(&limits, 0, sizeof limits);
        limits.max_steps = 4u;
        phy_cas *starved = phy_cas_create(ir, &limits);
        PHY_CHECK(full != NULL && starved != NULL);

        phy_chart *chart = NULL;
        PHY_CHECK_EQ_INT(phy_chart_create(ir, kCoords3, 3u, &chart), PHY_OK);
        phy_manifold *manifold = NULL;
        PHY_CHECK_EQ_INT(phy_manifold_create(ir, "M", 3u,
                                             PHY_ORIENTATION_POSITIVE, NULL,
                                             &manifold),
                         PHY_OK);
        PHY_CHECK_EQ_INT(phy_manifold_add_chart(manifold, chart, NULL), PHY_OK);

        phy_form *alpha = NULL;
        phy_form *beta = NULL;
        PHY_CHECK_EQ_INT(phy_form_create(manifold, 0u, NULL, 1u, &alpha),
                         PHY_OK);
        PHY_CHECK_EQ_INT(phy_form_create(manifold, 0u, NULL, 1u, &beta),
                         PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_form_set_at(full, alpha, 0u,
                            parse(ir, "(^ (+ x y z) 6)")),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_form_set_at(full, beta, 1u, parse(ir, "(^ (+ x y z) 5)")),
            PHY_OK);

        /* Storing a component simplifies, and that alone exceeds the budget. */
        PHY_CHECK_EQ_INT(
            phy_form_set_at(starved, alpha, 0u,
                            parse(ir, "(^ (+ a b c d e) 4)")),
            PHY_ERR_TIMEOUT);
        /* ...and the refused store left the component as it was. */
        phy_ir_ref kept = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(phy_form_get_at(alpha, 0u, &kept), PHY_OK);
        PHY_CHECK_EQ_STR(render(ir, kept), "(^ (+ x y z) 6)");

        phy_form *derivative = NULL;
        PHY_CHECK_EQ_INT(
            phy_form_exterior_derivative(starved, alpha, &derivative),
            PHY_ERR_TIMEOUT);
        PHY_CHECK(derivative == NULL);

        phy_form *product = NULL;
        PHY_CHECK_EQ_INT(phy_form_wedge(starved, alpha, beta, &product),
                         PHY_ERR_TIMEOUT);
        PHY_CHECK(product == NULL);

        /* Both layers still validate after a budget failure. */
        PHY_CHECK_EQ_INT(phy_cas_validate(starved), PHY_OK);
        PHY_CHECK_EQ_INT(phy_cas_validate(full), PHY_OK);
        PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);

        /* The ordinary CAS still works on the same objects. */
        PHY_CHECK_EQ_INT(
            phy_form_exterior_derivative(full, alpha, &derivative), PHY_OK);
        phy_form_destroy(derivative);

        phy_form_destroy(beta);
        phy_form_destroy(alpha);
        phy_manifold_destroy(manifold);
        phy_chart_destroy(chart);
        phy_cas_destroy(starved);
        phy_cas_destroy(full);
        phy_ir_context_destroy(ir);
    }
}

/*
 * The allocation-failure sweep, as tests/test_tensor.c does it: walk an
 * injected failure across every allocation a full workload makes, and after
 * each one require a typed status, no object handed back, both lower layers
 * validating, and the telemetry back at its baseline.
 */
static void test_allocation_failure_unwinds(void)
{
    phy_telemetry baseline;
    phy_telemetry after;
    phy_telemetry_get(&baseline);

    const uint32_t attempts_before = phy_host_alloc_attempts();
    {
        fixture f = open_euclidean(3u);
        phy_form *alpha = generic_form(&f, 1u, "a");
        phy_form *beta = generic_form(&f, 1u, "b");
        phy_form *product = NULL;
        PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, alpha, beta, &product), PHY_OK);
        phy_form *dual = NULL;
        PHY_CHECK_EQ_INT(phy_form_hodge(f.cas, product, &dual), PHY_OK);
        phy_form_destroy(dual);
        phy_form_destroy(product);
        phy_form_destroy(beta);
        phy_form_destroy(alpha);
        close_fixture(&f);
    }
    const uint32_t allocations = phy_host_alloc_attempts() - attempts_before;
    PHY_CHECK(allocations > 4u);

    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(after.bytes_live, baseline.bytes_live);

    unsigned reached_wedge = 0u;
    for (uint32_t nth = 1u; nth <= allocations; ++nth) {
        phy_host_fail_alloc_after(nth);

        phy_ir_context *ir = phy_ir_context_create(NULL);
        phy_cas *cas = (ir == NULL) ? NULL : phy_cas_create(ir, NULL);
        phy_chart *chart = NULL;
        phy_manifold *manifold = NULL;
        phy_form *alpha = NULL;
        phy_form *beta = NULL;
        phy_form *product = NULL;

        if (ir != NULL && cas != NULL) {
            if (phy_chart_create(ir, kCoords3, 3u, &chart) != PHY_OK) {
                PHY_CHECK(chart == NULL);
            } else if (phy_manifold_create(ir, "M", 3u,
                                           PHY_ORIENTATION_POSITIVE, NULL,
                                           &manifold) != PHY_OK) {
                PHY_CHECK(manifold == NULL);
            } else {
                PHY_CHECK_EQ_INT(phy_manifold_add_chart(manifold, chart, NULL),
                                 PHY_OK);
                const phy_status a_status =
                    phy_form_create(manifold, 0u, NULL, 1u, &alpha);
                const phy_status b_status =
                    (a_status != PHY_OK)
                        ? a_status
                        : phy_form_create(manifold, 0u, NULL, 1u, &beta);
                if (a_status != PHY_OK) {
                    PHY_CHECK(alpha == NULL);
                    PHY_CHECK(a_status == PHY_ERR_OUT_OF_MEMORY ||
                              a_status == PHY_ERR_MEMORY_LIMIT);
                } else if (b_status != PHY_OK) {
                    PHY_CHECK(beta == NULL);
                } else {
                    const phy_status w_status =
                        phy_form_wedge(cas, alpha, beta, &product);
                    if (w_status != PHY_OK) {
                        PHY_CHECK(product == NULL);
                        PHY_CHECK(w_status == PHY_ERR_OUT_OF_MEMORY ||
                                  w_status == PHY_ERR_MEMORY_LIMIT);
                    } else {
                        reached_wedge++;
                    }
                }
            }
        }

        phy_host_fail_alloc_after(0u);

        if (ir != NULL) {
            PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
        }
        if (cas != NULL) {
            PHY_CHECK_EQ_INT(phy_cas_validate(cas), PHY_OK);
        }
        phy_form_destroy(product);
        phy_form_destroy(beta);
        phy_form_destroy(alpha);
        phy_manifold_destroy(manifold);
        phy_chart_destroy(chart);
        phy_cas_destroy(cas);
        phy_ir_context_destroy(ir);

        phy_telemetry_get(&after);
        PHY_CHECK_EQ_INT(after.bytes_live, baseline.bytes_live);
    }

    /* The sweep must actually have reached the interesting part. */
    PHY_CHECK(reached_wedge > 0u);
    phy_host_fail_alloc_after(0u);
}

static void test_storage_is_bounded(void)
{
    phy_telemetry before;
    phy_telemetry after;

    fixture f = open_euclidean(4u);

    phy_telemetry_get(&before);
    phy_form *form = make_form(&f, 2u);
    phy_telemetry_get(&after);

    PHY_CHECK_EQ_INT(phy_form_component_count(form),
                     PHY_FORM_MAX_COMPONENTS); /* C(4,2) = 6 */

    /*
     * One allocation, of a fixed size. The six handles are inline, so the
     * largest form on the largest chart is well under a hundred bytes and a
     * curvature loop pays a constant per intermediate.
     */
    const size_t growth = after.bytes_live - before.bytes_live;
    PHY_CHECK_EQ_INT(after.alloc_count - before.alloc_count, 1);
    PHY_CHECK(growth >= PHY_FORM_MAX_COMPONENTS * sizeof(phy_ir_ref));
    PHY_CHECK(growth < 256u);

    /* A form of zero components costs the same, and never more. */
    phy_telemetry_get(&before);
    phy_form *scalar = make_form(&f, 0u);
    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(after.bytes_live - before.bytes_live, growth);

    /* An operation result is one allocation too, and interns no head. */
    const size_t symbols_before = phy_ir_symbol_count(f.ir);
    phy_telemetry_get(&before);
    phy_form *product = NULL;
    PHY_CHECK_EQ_INT(phy_form_wedge(f.cas, scalar, form, &product), PHY_OK);
    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(phy_ir_symbol_count(f.ir), symbols_before);
    PHY_CHECK(after.bytes_live - before.bytes_live <= growth);

    phy_form_destroy(product);
    phy_form_destroy(scalar);
    phy_form_destroy(form);
    close_fixture(&f);
}

/* -------------------------------------------------------------------- main */

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }

    PHY_TEST_CASE(test_manifold_metadata);
    PHY_TEST_CASE(test_manifold_rejects);
    PHY_TEST_CASE(test_manifold_charts);

    PHY_TEST_CASE(test_form_shape);
    PHY_TEST_CASE(test_form_rejects);
    PHY_TEST_CASE(test_position_roundtrip);
    PHY_TEST_CASE(test_position_of_every_tuple);
    PHY_TEST_CASE(test_component_antisymmetry);
    PHY_TEST_CASE(test_form_copy_and_linear);
    PHY_TEST_CASE(test_charts_do_not_mix);

    PHY_TEST_CASE(test_wedge_r2_r3_examples);
    PHY_TEST_CASE(test_wedge_graded_commutativity);
    PHY_TEST_CASE(test_wedge_associativity);
    PHY_TEST_CASE(test_wedge_domain);

    PHY_TEST_CASE(test_exterior_derivative_examples);
    PHY_TEST_CASE(test_d_squared_is_zero);
    PHY_TEST_CASE(test_d_squared_is_structural);
    PHY_TEST_CASE(test_d_squared_defers_on_opaque_components);
    PHY_TEST_CASE(test_graded_leibniz);
    PHY_TEST_CASE(test_exterior_derivative_domain);

    PHY_TEST_CASE(test_interior_product_examples);
    PHY_TEST_CASE(test_interior_product_is_nilpotent);
    PHY_TEST_CASE(test_interior_product_is_an_antiderivation);
    PHY_TEST_CASE(test_interior_product_rejects);

    PHY_TEST_CASE(test_hodge_examples);
    PHY_TEST_CASE(test_hodge_star_square);
    PHY_TEST_CASE(test_hodge_needs_an_orientation);
    PHY_TEST_CASE(test_volume_form);

    PHY_TEST_CASE(test_cancellation_and_budget);
    PHY_TEST_CASE(test_allocation_failure_unwinds);
    PHY_TEST_CASE(test_storage_is_bounded);

    const int result = PHY_TEST_REPORT("test_geom");
    phy_platform_shutdown();
    return result;
}
