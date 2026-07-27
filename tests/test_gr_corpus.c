/*
 * The general-relativity acceptance suite of docs/agent-tasks/GR_CURVATURE.md.
 *
 * tests/test_gr.c covers two metrics written out by hand. This suite covers the
 * committed corpus, read as committed: research/corpus/gr_golden.json is opened
 * at run time, parsed, and every one of its values is compared against what the
 * native pipeline computes. Nothing here transcribes a golden value into C, so
 * nothing here can drift away from the artifact whose agreement with the
 * literature was actually established.
 *
 * Three things are checked, and they fail for different reasons:
 *
 *   1. CORPUS REPRODUCTION. Every component of every MVP field of every metric,
 *      compared through phy_cas_equivalent() -- got minus want, decided zero --
 *      never as strings. The comparison covers the whole dense component table,
 *      not only the independent entries the corpus lists, so a component the
 *      corpus omits must come out identically zero and a component related to a
 *      listed one by a stated symmetry must actually satisfy it. That is what
 *      makes this a check on the Riemann symmetry group as well as on values.
 *
 *   2. IDENTITIES. Facts about curvature that hold for every metric and are not
 *      implied by agreeing with the corpus: g^ab g_bc = delta^a_c, metric
 *      compatibility in both variances, the first Bianchi identity, symmetry of
 *      the Ricci tensor, and the contracted second Bianchi identity
 *      nabla_a G^a_b = 0. A pipeline that reproduced the corpus by luck fails
 *      these; a pipeline that passes these is constrained well beyond the six
 *      metrics.
 *
 *   3. THE HARNESS ITSELF. A corpus test that cannot fail proves nothing, so
 *      the reader and the comparator are shown to reject wrong values before
 *      they are trusted to accept right ones.
 *
 * Determinism and resource-limit behaviour, the last two acceptance items of
 * the contract, are covered at the end.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "corpus/corpus_expr.h"
#include "corpus/corpus_json.h"
#include "phy/gr.h"
#include "phy/platform.h"
#include "phy_test.h"

#ifndef PHY_FIXTURE_DIR
#define PHY_FIXTURE_DIR "."
#endif

#define CORPUS_FILE "gr_golden.json"
#define MAX_KEY_PARTS 4u
#define MAX_PART 32u
#define EXPR_BUFFER 2048u

/* ------------------------------------------------------------ diagnostics */

static char g_label[256];

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
static const char *label(const char *format, ...);

static const char *label(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(g_label, sizeof g_label, format, arguments);
    va_end(arguments);
    return g_label;
}

static void print_expression(const char *prefix, phy_ir_context *ir,
                             phy_ir_ref ref)
{
    char buffer[EXPR_BUFFER];
    size_t length = 0u;
    if (phy_ir_write(ir, ref, buffer, sizeof buffer, &length) != PHY_OK) {
        fprintf(stderr, "       %s <%lu bytes, elided>\n", prefix,
                (unsigned long)length);
        return;
    }
    fprintf(stderr, "       %s %s\n", prefix, buffer);
}

/*
 * The corpus comparison rule of docs/agent-tasks/GR_CURVATURE.md: decide
 * `actual - expected` zero through the CAS. A serialized-string comparison
 * would reject 2*M/r against 2*M*r**(-1), which are the same expression.
 */
static bool expect_equivalent(phy_cas *cas, phy_ir_ref actual,
                              phy_ir_ref expected, const char *what)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    const phy_status status =
        phy_cas_equivalent(cas, actual, expected, &decision);
    g_phy_test_checks++;
    if (status == PHY_OK && decision == PHY_CAS_ZERO) {
        return true;
    }
    g_phy_test_failures++;
    fprintf(stderr, "FAIL [%s] %s: status %s, decision %d\n",
            g_phy_test_current, what, phy_status_name(status), (int)decision);
    print_expression("got     ", phy_cas_ir(cas), actual);
    print_expression("expected", phy_cas_ir(cas), expected);
    return false;
}

static bool expect_zero(phy_cas *cas, phy_ir_ref value, const char *what)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    const phy_status status = phy_cas_is_zero(cas, value, &decision);
    g_phy_test_checks++;
    if (status == PHY_OK && decision == PHY_CAS_ZERO) {
        return true;
    }
    g_phy_test_failures++;
    fprintf(stderr, "FAIL [%s] %s: status %s, decision %d\n",
            g_phy_test_current, what, phy_status_name(status), (int)decision);
    print_expression("got     ", phy_cas_ir(cas), value);
    return false;
}

/* ------------------------------------------------------------- corpus I/O */

static phy_json_document *open_corpus(void)
{
    static const char *const candidates[] = {
        PHY_FIXTURE_DIR "/" CORPUS_FILE,
        "research/corpus/" CORPUS_FILE,
        "../research/corpus/" CORPUS_FILE,
    };
    char error[256];
    const char *runtime_dir = getenv("PHY_FIXTURE_DIR");
    if (runtime_dir != NULL && runtime_dir[0] != '\0') {
        char path[512];
        (void)snprintf(path, sizeof path, "%s/%s", runtime_dir, CORPUS_FILE);
        phy_json_document *document =
            phy_json_read_file(path, error, sizeof error);
        if (document != NULL) {
            return document;
        }
    }
    for (size_t index = 0u;
         index < sizeof candidates / sizeof candidates[0]; ++index) {
        phy_json_document *document =
            phy_json_read_file(candidates[index], error, sizeof error);
        if (document != NULL) {
            return document;
        }
    }
    fprintf(stderr,
            "cannot read " CORPUS_FILE
            "; last reason: %s\n"
            "set PHY_FIXTURE_DIR or run from the repository root\n",
            error);
    return NULL;
}

/* ------------------------------------------------------- component keying */

/*
 * "t,r" and "r;theta,theta" both name a component by coordinate. The ';' in a
 * Christoffel key separates the upper slot from the lower pair and carries no
 * further information, so both separators split alike; the slot order in the
 * key is already the slot order of the tensor.
 */
static unsigned split_key(const char *key, char parts[][MAX_PART])
{
    unsigned count = 0u;
    size_t used = 0u;
    for (const char *cursor = key;; ++cursor) {
        if (*cursor == ',' || *cursor == ';' || *cursor == '\0') {
            if (used == 0u || count >= MAX_KEY_PARTS) {
                return 0u;
            }
            parts[count][used] = '\0';
            count++;
            used = 0u;
            if (*cursor == '\0') {
                return count;
            }
            continue;
        }
        if (used + 1u >= MAX_PART) {
            return 0u;
        }
        parts[count][used++] = *cursor;
    }
}

typedef struct {
    const char *names[PHY_TENSOR_MAX_DIM];
    unsigned dimension;
} axis_table;

static bool axis_of(const axis_table *axes, const char *name,
                    unsigned *out_axis)
{
    for (unsigned axis = 0u; axis < axes->dimension; ++axis) {
        if (strcmp(axes->names[axis], name) == 0) {
            *out_axis = axis;
            return true;
        }
    }
    return false;
}

static bool key_indices(const axis_table *axes, const char *key,
                        unsigned rank, unsigned *out_indices)
{
    char parts[MAX_KEY_PARTS][MAX_PART];
    if (split_key(key, parts) != rank) {
        return false;
    }
    for (unsigned slot = 0u; slot < rank; ++slot) {
        if (!axis_of(axes, parts[slot], &out_indices[slot])) {
            return false;
        }
    }
    return true;
}

static size_t flat_of(const axis_table *axes, unsigned rank,
                      const unsigned *indices)
{
    size_t flat = 0u;
    for (unsigned slot = 0u; slot < rank; ++slot) {
        flat = flat * axes->dimension + indices[slot];
    }
    return flat;
}

static void indices_of(const axis_table *axes, unsigned rank, size_t flat,
                       unsigned *out_indices)
{
    for (unsigned slot = rank; slot-- > 0u;) {
        out_indices[slot] = (unsigned)(flat % axes->dimension);
        flat /= axes->dimension;
    }
}

static void describe(const axis_table *axes, unsigned rank,
                     const unsigned *indices, char *buffer, size_t capacity)
{
    size_t used = 0u;
    buffer[0] = '\0';
    for (unsigned slot = 0u; slot < rank && used + 1u < capacity; ++slot) {
        const int written =
            snprintf(buffer + used, capacity - used, "%s%s",
                     slot == 0u ? "" : ",", axes->names[indices[slot]]);
        if (written < 0) {
            return;
        }
        used += (size_t)written;
    }
}

/* --------------------------------------------------- expected value tables */

/*
 * A dense expected table, built from the independent components the corpus
 * lists by applying the symmetries the corpus states. Every write is checked
 * against what is already there, so an inconsistent expansion -- or an
 * inconsistent corpus -- is a failure here rather than a wrong comparison
 * later.
 */
typedef struct {
    phy_ir_ref values[PHY_TENSOR_MAX_COMPONENTS];
    bool written[PHY_TENSOR_MAX_COMPONENTS];
    unsigned rank;
    size_t count;
} expected_table;

static void expected_init(expected_table *table, const axis_table *axes,
                          unsigned rank, phy_ir_ref zero)
{
    size_t count = 1u;
    for (unsigned slot = 0u; slot < rank; ++slot) {
        count *= axes->dimension;
    }
    table->rank = rank;
    table->count = count;
    for (size_t flat = 0u; flat < count; ++flat) {
        table->values[flat] = zero;
        table->written[flat] = false;
    }
}

static bool expected_put(expected_table *table, const axis_table *axes,
                         const unsigned *indices, phy_ir_ref value,
                         const char *what)
{
    const size_t flat = flat_of(axes, table->rank, indices);
    if (table->written[flat] && table->values[flat] != value) {
        g_phy_test_checks++;
        g_phy_test_failures++;
        fprintf(stderr,
                "FAIL [%s] %s: symmetry expansion is inconsistent at flat %lu\n",
                g_phy_test_current, what, (unsigned long)flat);
        return false;
    }
    table->values[flat] = value;
    table->written[flat] = true;
    return true;
}

typedef enum {
    FILL_SCALAR_PAIR,  /* T_ij = T_ji */
    FILL_CHRISTOFFEL,  /* Gamma^a_bc = Gamma^a_cb */
    FILL_RIEMANN       /* R_abcd = -R_bacd = -R_abdc = R_cdab */
} fill_kind;

static bool expand_component(phy_cas *cas, expected_table *table,
                             const axis_table *axes, fill_kind kind,
                             const unsigned *indices, phy_ir_ref value,
                             const char *what)
{
    phy_ir_ref negated = PHY_IR_NULL;
    if (kind == FILL_RIEMANN) {
        const phy_status status = phy_cas_neg(cas, value, &negated);
        PHY_CHECK_EQ_INT(status, PHY_OK);
        if (status != PHY_OK) {
            return false;
        }
    }

    const unsigned a = indices[0];
    const unsigned b = table->rank > 1u ? indices[1] : 0u;
    const unsigned c = table->rank > 2u ? indices[2] : 0u;
    const unsigned d = table->rank > 3u ? indices[3] : 0u;

    switch (kind) {
    case FILL_SCALAR_PAIR: {
        const unsigned swapped[2] = {b, a};
        return expected_put(table, axes, indices, value, what) &&
               expected_put(table, axes, swapped, value, what);
    }
    case FILL_CHRISTOFFEL: {
        const unsigned swapped[3] = {a, c, b};
        return expected_put(table, axes, indices, value, what) &&
               expected_put(table, axes, swapped, value, what);
    }
    case FILL_RIEMANN: {
        const unsigned positive[4][4] = {
            {a, b, c, d}, {b, a, d, c}, {c, d, a, b}, {d, c, b, a}};
        const unsigned negative[4][4] = {
            {b, a, c, d}, {a, b, d, c}, {d, c, a, b}, {c, d, b, a}};
        for (unsigned image = 0u; image < 4u; ++image) {
            if (!expected_put(table, axes, positive[image], value, what) ||
                !expected_put(table, axes, negative[image], negated, what)) {
                return false;
            }
        }
        return true;
    }
    default:
        break;
    }
    return false;
}

/*
 * Build the expected table for one corpus field and compare it against the
 * computed tensor, component by component over the whole dense table.
 */
static void compare_field(phy_cas *cas, const axis_table *axes,
                          const phy_json_value *entry, const char *field,
                          fill_kind kind, unsigned rank,
                          const phy_tensor *computed, const char *metric_name)
{
    const phy_json_value *map = phy_json_member(entry, field);
    PHY_CHECK_EQ_INT(phy_json_kind_of(map), PHY_JSON_OBJECT);
    if (phy_json_kind_of(map) != PHY_JSON_OBJECT) {
        return;
    }
    PHY_CHECK(computed != NULL);
    PHY_CHECK_EQ_INT(phy_tensor_rank(computed), rank);
    if (computed == NULL || phy_tensor_rank(computed) != rank) {
        return;
    }

    expected_table table;
    expected_init(&table, axes, rank,
                  phy_ir_integer(phy_cas_ir(cas), 0));

    const size_t entries = phy_json_count(map);
    for (size_t index = 0u; index < entries; ++index) {
        const char *key = phy_json_key_at(map, index);
        const char *text = phy_json_string(phy_json_value_at(map, index));
        PHY_CHECK(text != NULL);
        if (text == NULL) {
            return;
        }
        unsigned indices[PHY_TENSOR_MAX_RANK] = {0u};
        const bool keyed = key_indices(axes, key, rank, indices);
        if (!keyed) {
            g_phy_test_checks++;
            g_phy_test_failures++;
            fprintf(stderr, "FAIL [%s] %s.%s: unreadable component key \"%s\"\n",
                    g_phy_test_current, metric_name, field, key);
            return;
        }
        phy_ir_ref value = PHY_IR_NULL;
        const phy_status status =
            phy_corpus_expr_parse(cas, text, &value, NULL);
        if (status != PHY_OK) {
            g_phy_test_checks++;
            g_phy_test_failures++;
            fprintf(stderr, "FAIL [%s] %s.%s[%s]: cannot parse \"%s\" (%s)\n",
                    g_phy_test_current, metric_name, field, key, text,
                    phy_status_name(status));
            return;
        }
        if (!expand_component(cas, &table, axes, kind, indices, value,
                              label("%s.%s[%s]", metric_name, field, key))) {
            return;
        }
    }

    unsigned indices[PHY_TENSOR_MAX_RANK] = {0u};
    char spelled[128];
    for (size_t flat = 0u; flat < table.count; ++flat) {
        indices_of(axes, rank, flat, indices);
        phy_ir_ref actual = PHY_IR_NULL;
        const phy_status status =
            phy_tensor_component_expression(cas, computed, indices, &actual);
        PHY_CHECK_EQ_INT(status, PHY_OK);
        if (status != PHY_OK) {
            return;
        }
        describe(axes, rank, indices, spelled, sizeof spelled);
        (void)expect_equivalent(cas, actual, table.values[flat],
                                label("%s.%s[%s]", metric_name, field,
                                      spelled));
    }
}

static void compare_scalar(phy_cas *cas, const phy_json_value *entry,
                           const char *field, phy_ir_ref computed,
                           const char *metric_name)
{
    const char *text = phy_json_string(phy_json_member(entry, field));
    PHY_CHECK(text != NULL);
    if (text == NULL) {
        return;
    }
    phy_ir_ref expected = PHY_IR_NULL;
    const phy_status status =
        phy_corpus_expr_parse(cas, text, &expected, NULL);
    PHY_CHECK_EQ_INT(status, PHY_OK);
    if (status != PHY_OK) {
        return;
    }
    (void)expect_equivalent(cas, computed, expected,
                            label("%s.%s", metric_name, field));
}

/* --------------------------------------------------------- metric building */

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_chart *chart;
    phy_tensor *metric;
    phy_gr_result *result;
    axis_table axes;
} metric_case;

static void metric_case_release(metric_case *state)
{
    phy_gr_result_destroy(state->result);
    phy_tensor_destroy(state->metric);
    phy_chart_destroy(state->chart);
    phy_cas_destroy(state->cas);
    phy_ir_context_destroy(state->ir);
    memset(state, 0, sizeof *state);
}

/*
 * Everything up to and including phy_gr_compute(). Returns false with the
 * partial state already released when the entry is malformed, so a corpus
 * problem cannot be mistaken for a curvature problem.
 */
static bool metric_case_build(metric_case *state, const phy_json_value *entry,
                              const char *metric_name)
{
    memset(state, 0, sizeof *state);

    long dimension = 0;
    if (!phy_json_integer(phy_json_member(entry, "dimension"), &dimension) ||
        dimension < 1 || dimension > (long)PHY_TENSOR_MAX_DIM) {
        fprintf(stderr, "corpus entry %s: unusable dimension\n", metric_name);
        return false;
    }
    state->axes.dimension = (unsigned)dimension;

    const phy_json_value *coordinates = phy_json_member(entry, "coordinates");
    if (phy_json_count(coordinates) != (size_t)dimension) {
        fprintf(stderr, "corpus entry %s: coordinate count disagrees with"
                        " dimension\n",
                metric_name);
        return false;
    }
    for (unsigned axis = 0u; axis < state->axes.dimension; ++axis) {
        state->axes.names[axis] =
            phy_json_string(phy_json_element(coordinates, axis));
        if (state->axes.names[axis] == NULL) {
            fprintf(stderr, "corpus entry %s: coordinate %u is not a string\n",
                    metric_name, axis);
            return false;
        }
    }

    state->ir = phy_ir_context_create(NULL);
    state->cas = state->ir != NULL ? phy_cas_create(state->ir, NULL) : NULL;
    if (state->cas == NULL) {
        metric_case_release(state);
        return false;
    }
    if (phy_chart_create(state->ir, state->axes.names, state->axes.dimension,
                         &state->chart) != PHY_OK) {
        metric_case_release(state);
        return false;
    }

    static const phy_ir_variance lower[2] = {PHY_IR_INDEX_LOWER,
                                             PHY_IR_INDEX_LOWER};
    if (phy_tensor_create(state->chart, "g", 2u, lower, &state->metric) !=
            PHY_OK ||
        phy_tensor_declare_slot_symmetry(state->metric, 0u, 1u,
                                         PHY_IR_SYMMETRY_SYMMETRIC) !=
            PHY_OK) {
        metric_case_release(state);
        return false;
    }

    const phy_json_value *components = phy_json_member(entry, "metric");
    const size_t entries = phy_json_count(components);
    for (size_t index = 0u; index < entries; ++index) {
        const char *key = phy_json_key_at(components, index);
        const char *text =
            phy_json_string(phy_json_value_at(components, index));
        unsigned indices[2] = {0u, 0u};
        phy_ir_ref value = PHY_IR_NULL;
        if (text == NULL || !key_indices(&state->axes, key, 2u, indices) ||
            phy_corpus_expr_parse(state->cas, text, &value, NULL) != PHY_OK ||
            phy_tensor_set(state->metric, indices, value) != PHY_OK) {
            fprintf(stderr, "corpus entry %s: unusable metric component %s\n",
                    metric_name, key != NULL ? key : "(null)");
            metric_case_release(state);
            return false;
        }
    }

    const phy_status status =
        phy_gr_compute(state->cas, state->metric, &state->result);
    PHY_CHECK_EQ_INT(status, PHY_OK);
    if (status != PHY_OK) {
        fprintf(stderr, "       metric %s (CAS bytes %lu)\n", metric_name,
                (unsigned long)phy_cas_bytes_used(state->cas));
        metric_case_release(state);
        return false;
    }
    return true;
}

/* -------------------------------------------------------------- identities */

/* g^ab g_bc = delta^a_c. */
static void check_inverse_metric(metric_case *state, const char *metric_name)
{
    const unsigned dimension = state->axes.dimension;
    for (unsigned a = 0u; a < dimension; ++a) {
        for (unsigned c = 0u; c < dimension; ++c) {
            phy_ir_ref sum = PHY_IR_NULL;
            phy_status status = phy_cas_number(state->cas, 0, 1, &sum);
            for (unsigned b = 0u; b < dimension && status == PHY_OK; ++b) {
                const unsigned upper[2] = {a, b};
                const unsigned lowered[2] = {b, c};
                phy_ir_ref left = PHY_IR_NULL;
                phy_ir_ref right = PHY_IR_NULL;
                phy_ir_ref term = PHY_IR_NULL;
                status = phy_tensor_component_expression(
                    state->cas, phy_gr_inverse_metric(state->result), upper,
                    &left);
                if (status == PHY_OK) {
                    status = phy_tensor_component_expression(
                        state->cas, state->metric, lowered, &right);
                }
                if (status == PHY_OK) {
                    const phy_ir_ref factors[2] = {left, right};
                    status =
                        phy_cas_mul(state->cas, factors, 2u, &term);
                }
                if (status == PHY_OK) {
                    const phy_ir_ref terms[2] = {sum, term};
                    status = phy_cas_add(state->cas, terms, 2u, &sum);
                }
            }
            PHY_CHECK_EQ_INT(status, PHY_OK);
            if (status != PHY_OK) {
                return;
            }
            const phy_ir_ref expected =
                phy_ir_integer(state->ir, a == c ? 1 : 0);
            (void)expect_equivalent(
                state->cas, sum, expected,
                label("%s: g^%s%s g_%s%s = delta", metric_name,
                      state->axes.names[a], "b", "b", state->axes.names[c]));
        }
    }
}

/* nabla_c g_ab = 0 and nabla_c g^ab = 0. */
static void check_metric_compatibility(metric_case *state,
                                       const char *metric_name)
{
    const phy_tensor *targets[2] = {state->metric,
                                    phy_gr_inverse_metric(state->result)};
    static const char *const spelled[2] = {"g_ab", "g^ab"};

    for (unsigned which = 0u; which < 2u; ++which) {
        phy_tensor *derivative = NULL;
        const phy_status status = phy_gr_covariant_derivative(
            state->cas, state->result, targets[which], "NablaG", &derivative);
        PHY_CHECK_EQ_INT(status, PHY_OK);
        if (status != PHY_OK) {
            return;
        }
        unsigned indices[PHY_TENSOR_MAX_RANK] = {0u};
        char described[128];
        const size_t count = phy_tensor_component_count(derivative);
        for (size_t flat = 0u; flat < count; ++flat) {
            indices_of(&state->axes, 3u, flat, indices);
            phy_ir_ref value = PHY_IR_NULL;
            if (phy_tensor_component_expression(state->cas, derivative,
                                                indices, &value) != PHY_OK) {
                break;
            }
            describe(&state->axes, 3u, indices, described, sizeof described);
            (void)expect_zero(state->cas, value,
                              label("%s: nabla %s [%s]", metric_name,
                                    spelled[which], described));
        }
        phy_tensor_destroy(derivative);
    }
}

/* R_abcd + R_acdb + R_adbc = 0, the first Bianchi identity. */
static void check_first_bianchi(metric_case *state, const char *metric_name)
{
    const phy_tensor *riemann = phy_gr_riemann_covariant(state->result);
    const unsigned dimension = state->axes.dimension;
    char described[128];
    for (unsigned a = 0u; a < dimension; ++a) {
        for (unsigned b = 0u; b < dimension; ++b) {
            for (unsigned c = 0u; c < dimension; ++c) {
                for (unsigned d = 0u; d < dimension; ++d) {
                    const unsigned first[4] = {a, b, c, d};
                    const unsigned second[4] = {a, c, d, b};
                    const unsigned third[4] = {a, d, b, c};
                    phy_ir_ref values[3] = {PHY_IR_NULL, PHY_IR_NULL,
                                            PHY_IR_NULL};
                    phy_status status = phy_tensor_component_expression(
                        state->cas, riemann, first, &values[0]);
                    if (status == PHY_OK) {
                        status = phy_tensor_component_expression(
                            state->cas, riemann, second, &values[1]);
                    }
                    if (status == PHY_OK) {
                        status = phy_tensor_component_expression(
                            state->cas, riemann, third, &values[2]);
                    }
                    phy_ir_ref sum = PHY_IR_NULL;
                    if (status == PHY_OK) {
                        status = phy_cas_add(state->cas, values, 3u, &sum);
                    }
                    PHY_CHECK_EQ_INT(status, PHY_OK);
                    if (status != PHY_OK) {
                        return;
                    }
                    describe(&state->axes, 4u, first, described,
                             sizeof described);
                    (void)expect_zero(
                        state->cas, sum,
                        label("%s: first Bianchi at [%s]", metric_name,
                              described));
                }
            }
        }
    }
}

/* R_ab = R_ba. */
static void check_ricci_symmetry(metric_case *state, const char *metric_name)
{
    const phy_tensor *ricci = phy_gr_ricci(state->result);
    const unsigned dimension = state->axes.dimension;
    for (unsigned a = 0u; a < dimension; ++a) {
        for (unsigned b = (unsigned)(a + 1u); b < dimension; ++b) {
            const unsigned direct[2] = {a, b};
            const unsigned swapped[2] = {b, a};
            phy_ir_ref left = PHY_IR_NULL;
            phy_ir_ref right = PHY_IR_NULL;
            phy_status status = phy_tensor_component_expression(
                state->cas, ricci, direct, &left);
            if (status == PHY_OK) {
                status = phy_tensor_component_expression(
                    state->cas, ricci, swapped, &right);
            }
            PHY_CHECK_EQ_INT(status, PHY_OK);
            if (status != PHY_OK) {
                return;
            }
            (void)expect_equivalent(
                state->cas, left, right,
                label("%s: R_%s%s = R_%s%s", metric_name,
                      state->axes.names[a], state->axes.names[b],
                      state->axes.names[b], state->axes.names[a]));
        }
    }
}

/*
 * nabla_a G^a_b = 0, the contracted second Bianchi identity.
 *
 * This is the deepest identity the layer can express: the full second Bianchi
 * identity needs the covariant derivative of the rank-4 Riemann tensor, which
 * is rank 5 and above the tensor core's ceiling. Its contraction is not, and
 * it is what makes the Einstein tensor the object that can sit opposite a
 * conserved stress tensor.
 */
static void check_contracted_bianchi(metric_case *state,
                                     const char *metric_name)
{
    phy_tensor *mixed = NULL;
    phy_tensor *derivative = NULL;
    phy_tensor *divergence = NULL;

    phy_status status = phy_tensor_raise_slot(
        state->cas, phy_gr_einstein(state->result), 0u,
        phy_gr_inverse_metric(state->result), "EinsteinMixed", &mixed);
    if (status == PHY_OK) {
        status = phy_gr_covariant_derivative(state->cas, state->result, mixed,
                                             "NablaEinstein", &derivative);
    }
    if (status == PHY_OK) {
        /* Slot 0 is the derivative index, slot 1 the raised Einstein index. */
        status = phy_tensor_contract(state->cas, derivative, 0u, 1u,
                                     "DivEinstein", &divergence);
    }
    PHY_CHECK_EQ_INT(status, PHY_OK);
    if (status == PHY_OK) {
        for (unsigned b = 0u; b < state->axes.dimension; ++b) {
            phy_ir_ref value = PHY_IR_NULL;
            if (phy_tensor_component_expression(state->cas, divergence, &b,
                                                &value) != PHY_OK) {
                break;
            }
            (void)expect_zero(state->cas, value,
                              label("%s: nabla_a G^a_%s = 0", metric_name,
                                    state->axes.names[b]));
        }
    }
    phy_tensor_destroy(divergence);
    phy_tensor_destroy(derivative);
    phy_tensor_destroy(mixed);
}

/* --------------------------------------------------------------- the cases */

static void check_cross_checks(const phy_json_value *entry,
                               const char *metric_name)
{
    /*
     * The corpus records, per entry, whether its computed value agreed with an
     * independently sourced closed form. An entry that was committed with a
     * disagreement would make every comparison below meaningless, so the claim
     * is verified rather than assumed.
     */
    const phy_json_value *checks = phy_json_member(entry, "cross_checks");
    PHY_CHECK_EQ_INT(phy_json_kind_of(checks), PHY_JSON_OBJECT);
    const size_t count = phy_json_count(checks);
    PHY_CHECK(count > 0u);
    for (size_t index = 0u; index < count; ++index) {
        bool agrees = false;
        const bool present = phy_json_bool(
            phy_json_member(phy_json_value_at(checks, index), "agrees"),
            &agrees);
        g_phy_test_checks++;
        if (!present || !agrees) {
            g_phy_test_failures++;
            fprintf(stderr,
                    "FAIL [%s] %s: cross check \"%s\" does not record"
                    " agreement\n",
                    g_phy_test_current, metric_name,
                    phy_json_key_at(checks, index));
        }
    }
}

static void check_weyl(metric_case *state, const char *metric_name,
                       phy_ir_ref kretschmann)
{
    const phy_tensor *weyl = NULL;
    phy_status status =
        phy_gr_weyl(state->cas, state->result, &weyl);
    PHY_CHECK_EQ_INT(status, PHY_OK);
    if (status != PHY_OK) {
        return;
    }

    const bool conformally_flat =
        strcmp(metric_name, "schwarzschild") != 0 &&
        strcmp(metric_name, "reissner_nordstrom") != 0;
    if (conformally_flat) {
        unsigned indices[PHY_TENSOR_MAX_RANK] = {0u};
        const size_t count = phy_tensor_component_count(weyl);
        for (size_t flat = 0u; flat < count; ++flat) {
            status = phy_tensor_unflatten(weyl, flat, indices);
            PHY_CHECK_EQ_INT(status, PHY_OK);
            if (status != PHY_OK) {
                return;
            }
            phy_ir_ref value = PHY_IR_NULL;
            status = phy_tensor_component_expression(
                state->cas, weyl, indices, &value);
            PHY_CHECK_EQ_INT(status, PHY_OK);
            if (status != PHY_OK) {
                return;
            }
            (void)expect_zero(
                state->cas, value,
                label("%s: Weyl component %lu", metric_name,
                      (unsigned long)flat));
        }
    }

    /* The defining trace g^{ac} C_abcd vanishes for every corpus metric. */
    for (unsigned b = 0u; b < state->axes.dimension; ++b) {
        for (unsigned d = 0u; d < state->axes.dimension; ++d) {
            phy_ir_ref trace = PHY_IR_NULL;
            status = phy_cas_number(state->cas, 0, 1, &trace);
            for (unsigned a = 0u;
                 a < state->axes.dimension && status == PHY_OK; ++a) {
                for (unsigned c = 0u;
                     c < state->axes.dimension && status == PHY_OK; ++c) {
                    const unsigned inverse_indices[2] = {a, c};
                    const unsigned weyl_indices[4] = {a, b, c, d};
                    phy_ir_ref inverse = PHY_IR_NULL;
                    phy_ir_ref component = PHY_IR_NULL;
                    phy_ir_ref term = PHY_IR_NULL;
                    status = phy_tensor_component_expression(
                        state->cas, phy_gr_inverse_metric(state->result),
                        inverse_indices, &inverse);
                    if (status == PHY_OK) {
                        status = phy_tensor_component_expression(
                            state->cas, weyl, weyl_indices, &component);
                    }
                    if (status == PHY_OK) {
                        const phy_ir_ref factors[2] = {inverse, component};
                        status =
                            phy_cas_mul(state->cas, factors, 2u, &term);
                    }
                    if (status == PHY_OK) {
                        const phy_ir_ref terms[2] = {trace, term};
                        status =
                            phy_cas_add(state->cas, terms, 2u, &trace);
                    }
                }
            }
            PHY_CHECK_EQ_INT(status, PHY_OK);
            if (status != PHY_OK) {
                return;
            }
            (void)expect_zero(
                state->cas, trace,
                label("%s: g^ac C_abcd at b=%u,d=%u",
                      metric_name, b, d));
        }
    }

    phy_ir_ref square = PHY_IR_NULL;
    status = phy_gr_weyl_squared(
        state->cas, state->result, &square);
    PHY_CHECK_EQ_INT(status, PHY_OK);
    if (status != PHY_OK) {
        return;
    }

    phy_ir_ref expected = phy_ir_integer(state->ir, 0);
    if (strcmp(metric_name, "schwarzschild") == 0) {
        expected = kretschmann;
    } else if (strcmp(metric_name, "reissner_nordstrom") == 0) {
        status = phy_corpus_expr_parse(
            state->cas, "48*(M*r-Q**2)**2/r**8", &expected, NULL);
        PHY_CHECK_EQ_INT(status, PHY_OK);
        if (status != PHY_OK) {
            return;
        }
    }
    (void)expect_equivalent(
        state->cas, square, expected,
        label("%s: C_abcd C^abcd", metric_name));

    phy_ir_ref again = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_gr_weyl_squared(state->cas, state->result, &again), PHY_OK);
    PHY_CHECK(again == square);
}

static void run_metric_entry(const phy_json_value *entry)
{
    const char *metric_name = phy_json_string(phy_json_member(entry, "name"));
    PHY_CHECK(metric_name != NULL);
    if (metric_name == NULL) {
        return;
    }
    check_cross_checks(entry, metric_name);

    metric_case state;
    if (!metric_case_build(&state, entry, metric_name)) {
        return;
    }

    compare_field(state.cas, &state.axes, entry, "metric", FILL_SCALAR_PAIR,
                  2u, state.metric, metric_name);
    compare_field(state.cas, &state.axes, entry, "christoffel",
                  FILL_CHRISTOFFEL, 3u, phy_gr_christoffel(state.result),
                  metric_name);
    compare_field(state.cas, &state.axes, entry, "riemann_covariant",
                  FILL_RIEMANN, 4u, phy_gr_riemann_covariant(state.result),
                  metric_name);
    compare_field(state.cas, &state.axes, entry, "ricci", FILL_SCALAR_PAIR,
                  2u, phy_gr_ricci(state.result), metric_name);
    compare_field(state.cas, &state.axes, entry, "einstein", FILL_SCALAR_PAIR,
                  2u, phy_gr_einstein(state.result), metric_name);
    compare_scalar(state.cas, entry, "ricci_scalar",
                   phy_gr_scalar_curvature(state.result), metric_name);

    phy_ir_ref kretschmann = PHY_IR_NULL;
    const phy_status status =
        phy_gr_kretschmann(state.cas, state.result, &kretschmann);
    PHY_CHECK_EQ_INT(status, PHY_OK);
    if (status != PHY_OK) {
        fprintf(stderr, "       metric %s kretschmann: %s\n", metric_name,
                phy_status_name(status));
    }
    if (status == PHY_OK) {
        compare_scalar(state.cas, entry, "kretschmann", kretschmann,
                       metric_name);
        /* Caching must return the same handle, not recompute a new one. */
        phy_ir_ref again = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(
            phy_gr_kretschmann(state.cas, state.result, &again), PHY_OK);
        PHY_CHECK(again == kretschmann);
        PHY_CHECK(phy_gr_riemann_contravariant(state.result) != NULL);
        check_weyl(&state, metric_name, kretschmann);
    }

    check_inverse_metric(&state, metric_name);
    check_metric_compatibility(&state, metric_name);
    check_first_bianchi(&state, metric_name);
    check_ricci_symmetry(&state, metric_name);
    check_contracted_bianchi(&state, metric_name);

    PHY_CHECK_EQ_INT(phy_ir_validate(state.ir), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_validate(state.cas), PHY_OK);
    metric_case_release(&state);
}

/* ------------------------------------------------------------- the corpus */

static const phy_json_value *g_corpus_root;

static void test_corpus_scope_is_what_this_suite_covers(void)
{
    long schema = 0;
    PHY_CHECK(phy_json_integer(phy_json_member(g_corpus_root, "schema"),
                               &schema));
    PHY_CHECK_EQ_INT(schema, 1);

    const phy_json_value *scope = phy_json_member(g_corpus_root, "scope");
    long max_dimension = 0;
    PHY_CHECK(phy_json_integer(phy_json_member(scope, "max_dimension"),
                               &max_dimension));
    PHY_CHECK(max_dimension <= (long)PHY_TENSOR_MAX_DIM);

    /*
     * The fields below are the ones compare_field/compare_scalar handle. If
     * the corpus grows another, this fails rather than silently skipping it.
     */
    static const char *const covered[] = {"metric",  "christoffel",
                                          "riemann_covariant", "ricci",
                                          "ricci_scalar", "einstein"};
    const phy_json_value *mvp = phy_json_member(scope, "mvp_fields");
    PHY_CHECK_EQ_INT(phy_json_count(mvp),
                     sizeof covered / sizeof covered[0]);
    for (size_t index = 0u; index < phy_json_count(mvp); ++index) {
        const char *field = phy_json_string(phy_json_element(mvp, index));
        bool found = false;
        for (size_t known = 0u;
             known < sizeof covered / sizeof covered[0]; ++known) {
            if (field != NULL && strcmp(field, covered[known]) == 0) {
                found = true;
                break;
            }
        }
        PHY_CHECK(found);
    }

    /*
     * Kerr is excluded in both directions per the contract: no golden values
     * and no live computation. A deferred metric that appeared in `metrics`
     * would be computed by the loop below without anyone deciding to.
     */
    const phy_json_value *deferred = phy_json_member(scope,
                                                     "deferred_metrics");
    const phy_json_value *metrics = phy_json_member(g_corpus_root, "metrics");
    PHY_CHECK(phy_json_count(deferred) > 0u);
    PHY_CHECK(phy_json_count(metrics) > 0u);
    for (size_t index = 0u; index < phy_json_count(deferred); ++index) {
        const char *name = phy_json_string(phy_json_element(deferred, index));
        for (size_t entry = 0u; entry < phy_json_count(metrics); ++entry) {
            const char *present = phy_json_string(
                phy_json_member(phy_json_element(metrics, entry), "name"));
            PHY_CHECK(name != NULL && present != NULL &&
                      strcmp(name, present) != 0);
        }
    }
}

static void test_corpus_reproduction(void)
{
    const phy_json_value *metrics = phy_json_member(g_corpus_root, "metrics");
    const size_t count = phy_json_count(metrics);
    PHY_CHECK(count >= 6u);
    for (size_t index = 0u; index < count; ++index) {
        run_metric_entry(phy_json_element(metrics, index));
    }
}

/* ------------------------------------------------------ harness self-checks */

/*
 * A corpus test that cannot fail proves nothing. These show that the reader
 * and the comparator reject wrong answers, so that their accepting the right
 * ones means something.
 */
static void test_harness_rejects_wrong_values(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);

    /* Spellings the corpus actually uses, decided equal to computed forms. */
    static const char *const equal_pairs[][2] = {
        {"2*M/r", "2*M*r**(-1)"},
        {"1/tan(theta)", "cos(theta)/sin(theta)"},
        {"-sin(2*theta)/2", "-sin(theta)*cos(theta)"},
        /*
         * The sphere_2d Ricci component, in the spelling the corpus uses
         * against the one a curvature pass computes. Reduces to sin(theta)**2,
         * not to 1: R_theta,theta is 1 and R_phi,phi is sin(theta)**2.
         */
        {"sin(2*theta)/(2*tan(theta)) - cos(2*theta)", "sin(theta)**2"},
        {"(2*M - r)/r", "2*M/r - 1"},
        {"-r*sin(theta)**2", "-(r*sin(theta)**2)"},
        {"-2*M**2 + M*r", "M*(r - 2*M)"},
    };
    for (size_t index = 0u;
         index < sizeof equal_pairs / sizeof equal_pairs[0]; ++index) {
        phy_ir_ref left = PHY_IR_NULL;
        phy_ir_ref right = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(
            phy_corpus_expr_parse(cas, equal_pairs[index][0], &left, NULL),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_corpus_expr_parse(cas, equal_pairs[index][1], &right, NULL),
            PHY_OK);
        (void)expect_equivalent(cas, left, right,
                                label("equal pair %lu",
                                      (unsigned long)index));
    }

    /* Differences a golden comparison must catch, including sign slips. */
    static const char *const different_pairs[][2] = {
        {"2/a_0**2", "3/a_0**2"},
        {"2/a_0**2", "-2/a_0**2"},
        {"48*M**2/r**6", "48*M**2/r**7"},
        {"-2*M/r**3", "2*M/r**3"},
        {"12/L**2", "12/L**3"},
        {"Q**2/r**2", "Q**2/r**2 + 1"},
    };
    for (size_t index = 0u;
         index < sizeof different_pairs / sizeof different_pairs[0];
         ++index) {
        phy_ir_ref left = PHY_IR_NULL;
        phy_ir_ref right = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(
            phy_corpus_expr_parse(cas, different_pairs[index][0], &left, NULL),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_corpus_expr_parse(cas, different_pairs[index][1], &right,
                                  NULL),
            PHY_OK);
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        PHY_CHECK_EQ_INT(phy_cas_equivalent(cas, left, right, &decision),
                         PHY_OK);
        PHY_CHECK(decision != PHY_CAS_ZERO);
    }

    /* SymPy precedence, where a wrong reading changes a golden value. */
    static const struct {
        const char *text;
        const char *equivalent;
    } precedence[] = {
        {"-r**2", "-(r**2)"},
        {"r**-2", "1/r**2"},
        {"2**3**2", "512"},
        {"-2*M**2", "-(2*(M**2))"},
        {"1/2/r", "1/(2*r)"},
    };
    for (size_t index = 0u;
         index < sizeof precedence / sizeof precedence[0]; ++index) {
        phy_ir_ref left = PHY_IR_NULL;
        phy_ir_ref right = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(
            phy_corpus_expr_parse(cas, precedence[index].text, &left, NULL),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_corpus_expr_parse(cas, precedence[index].equivalent, &right,
                                  NULL),
            PHY_OK);
        (void)expect_equivalent(cas, left, right,
                                label("precedence %s",
                                      precedence[index].text));
    }

    /* Constructs the reader must refuse rather than reinterpret. */
    static const char *const refused[] = {
        "1.5",          /* an inexact literal cannot be decided */
        "besselj(r)",   /* an unknown head would become opaque */
        "2 +",          /* truncated */
        "2 2",          /* trailing content */
        "(2",           /* unbalanced */
        "",             /* empty */
    };
    for (size_t index = 0u;
         index < sizeof refused / sizeof refused[0]; ++index) {
        phy_ir_ref value = PHY_IR_NULL;
        PHY_CHECK(phy_corpus_expr_parse(cas, refused[index], &value, NULL) !=
                  PHY_OK);
    }

    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_validate(cas), PHY_OK);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

/*
 * The component-key reader and the symmetry expander, shown to reject before
 * they are trusted to accept. A silently mis-keyed component would compare the
 * right expression against the wrong slot.
 */
static void test_harness_rejects_bad_keys(void)
{
    axis_table axes;
    axes.dimension = 4u;
    axes.names[0] = "t";
    axes.names[1] = "r";
    axes.names[2] = "theta";
    axes.names[3] = "phi";

    unsigned indices[PHY_TENSOR_MAX_RANK] = {0u};
    PHY_CHECK(key_indices(&axes, "t,r", 2u, indices));
    PHY_CHECK_EQ_INT(indices[0], 0u);
    PHY_CHECK_EQ_INT(indices[1], 1u);
    PHY_CHECK(key_indices(&axes, "r;theta,theta", 3u, indices));
    PHY_CHECK_EQ_INT(indices[0], 1u);
    PHY_CHECK_EQ_INT(indices[1], 2u);
    PHY_CHECK_EQ_INT(indices[2], 2u);
    PHY_CHECK(key_indices(&axes, "t,phi,t,phi", 4u, indices));
    PHY_CHECK_EQ_INT(indices[3], 3u);

    PHY_CHECK(!key_indices(&axes, "t,z", 2u, indices));  /* unknown axis */
    PHY_CHECK(!key_indices(&axes, "t", 2u, indices));    /* too few slots */
    PHY_CHECK(!key_indices(&axes, "t,r,theta", 2u, indices)); /* too many */
    PHY_CHECK(!key_indices(&axes, "t,", 2u, indices));   /* empty part */
    PHY_CHECK(!key_indices(&axes, "", 1u, indices));

    /* Riemann expansion must reach exactly the eight related components. */
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);
    expected_table table;
    expected_init(&table, &axes, 4u, phy_ir_integer(ir, 0));
    const unsigned seed[4] = {0u, 1u, 0u, 1u}; /* R_trtr */
    const phy_ir_ref value = phy_ir_integer(ir, 7);
    PHY_CHECK(expand_component(cas, &table, &axes, FILL_RIEMANN, seed, value,
                               "self check"));
    size_t written = 0u;
    for (size_t flat = 0u; flat < table.count; ++flat) {
        written += table.written[flat] ? 1u : 0u;
    }
    /* {t,r}x{t,r} with a<b, c<d has four sign images, not eight distinct. */
    PHY_CHECK_EQ_INT(written, 4u);
    const unsigned swapped[4] = {1u, 0u, 0u, 1u};
    phy_ir_ref negated = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_neg(cas, value, &negated), PHY_OK);
    PHY_CHECK(table.values[flat_of(&axes, 4u, swapped)] == negated);

    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

/* ------------------------------------------------------------ determinism */

/*
 * Acceptance test 7: two runs over the whole corpus produce byte-identical
 * serialized output. Serialization is exact and context-independent, which is
 * what makes it the right comparison across two separate IR contexts.
 */
static bool serialize_case(const phy_json_value *entry, const char *name,
                           char **out_text, size_t *out_length)
{
    metric_case state;
    if (!metric_case_build(&state, entry, name)) {
        return false;
    }

    size_t capacity = 1u << 16;
    size_t used = 0u;
    char *text = malloc(capacity);
    if (text == NULL) {
        metric_case_release(&state);
        return false;
    }
    text[0] = '\0';

    const phy_tensor *tensors[6] = {
        phy_gr_inverse_metric(state.result),
        phy_gr_christoffel(state.result),
        phy_gr_riemann_mixed(state.result),
        phy_gr_riemann_covariant(state.result),
        phy_gr_ricci(state.result),
        phy_gr_einstein(state.result)};

    bool ok = true;
    char component[EXPR_BUFFER];
    for (unsigned which = 0u; which < 6u && ok; ++which) {
        const size_t count = phy_tensor_component_count(tensors[which]);
        for (size_t flat = 0u; flat < count && ok; ++flat) {
            unsigned indices[PHY_TENSOR_MAX_RANK] = {0u};
            phy_ir_ref value = PHY_IR_NULL;
            size_t length = 0u;
            ok = phy_tensor_unflatten(tensors[which], flat, indices) ==
                     PHY_OK &&
                 phy_tensor_component_expression(state.cas, tensors[which],
                                                 indices, &value) == PHY_OK &&
                 phy_ir_write(state.ir, value, component, sizeof component,
                              &length) == PHY_OK;
            if (!ok) {
                break;
            }
            if (used + length + 2u > capacity) {
                char *grown = realloc(text, capacity * 2u + length);
                if (grown == NULL) {
                    ok = false;
                    break;
                }
                text = grown;
                capacity = capacity * 2u + length;
            }
            memcpy(text + used, component, length);
            used += length;
            text[used++] = '\n';
        }
    }
    if (ok) {
        text[used] = '\0';
        *out_text = text;
        *out_length = used;
    } else {
        free(text);
    }
    metric_case_release(&state);
    return ok;
}

static void test_pipeline_is_deterministic(void)
{
    const phy_json_value *metrics = phy_json_member(g_corpus_root, "metrics");
    for (size_t index = 0u; index < phy_json_count(metrics); ++index) {
        const phy_json_value *entry = phy_json_element(metrics, index);
        const char *name = phy_json_string(phy_json_member(entry, "name"));
        char *first = NULL;
        char *second = NULL;
        size_t first_length = 0u;
        size_t second_length = 0u;
        if (!serialize_case(entry, name, &first, &first_length) ||
            !serialize_case(entry, name, &second, &second_length)) {
            PHY_CHECK(false);
            free(first);
            return;
        }
        PHY_CHECK_EQ_INT(first_length, second_length);
        g_phy_test_checks++;
        if (first_length != second_length ||
            memcmp(first, second, first_length) != 0) {
            g_phy_test_failures++;
            fprintf(stderr, "FAIL [%s] %s: two runs disagree\n",
                    g_phy_test_current, name);
        }
        free(first);
        free(second);
    }
}

/* --------------------------------------------------------- resource limits */

/*
 * Acceptance test 8: with the ceilings set low the pipeline returns a typed
 * status cleanly, with no partial result presented as complete, and both
 * layers still validate.
 */
static void test_resource_limits_fail_cleanly(void)
{
    static const uint32_t node_caps[] = {64u, 256u, 1024u};
    for (size_t index = 0u;
         index < sizeof node_caps / sizeof node_caps[0]; ++index) {
        phy_ir_limits limits;
        phy_ir_limits_defaults(&limits);
        limits.max_nodes = node_caps[index];
        phy_ir_context *ir = phy_ir_context_create(&limits);
        PHY_CHECK(ir != NULL);
        if (ir == NULL) {
            continue;
        }
        phy_cas *cas = phy_cas_create(ir, NULL);
        PHY_CHECK(cas != NULL);
        if (cas == NULL) {
            phy_ir_context_destroy(ir);
            continue;
        }

        static const char *const names[4] = {"t", "r", "theta", "phi"};
        phy_chart *chart = NULL;
        phy_tensor *metric = NULL;
        static const phy_ir_variance lower[2] = {PHY_IR_INDEX_LOWER,
                                                 PHY_IR_INDEX_LOWER};
        if (phy_chart_create(ir, names, 4u, &chart) == PHY_OK &&
            phy_tensor_create(chart, "g", 2u, lower, &metric) == PHY_OK &&
            phy_tensor_declare_slot_symmetry(metric, 0u, 1u,
                                             PHY_IR_SYMMETRY_SYMMETRIC) ==
                PHY_OK) {
            /* Schwarzschild: the cheapest four-dimensional non-flat entry. */
            static const char *const keys[4] = {"t,t", "r,r", "theta,theta",
                                                "phi,phi"};
            static const char *const values[4] = {
                "(2*M - r)/r", "r/(-2*M + r)", "r**2",
                "r**2*sin(theta)**2"};
            axis_table axes;
            axes.dimension = 4u;
            for (unsigned axis = 0u; axis < 4u; ++axis) {
                axes.names[axis] = names[axis];
            }
            bool built = true;
            for (unsigned component = 0u; component < 4u && built;
                 ++component) {
                unsigned indices[2] = {0u, 0u};
                phy_ir_ref value = PHY_IR_NULL;
                built = key_indices(&axes, keys[component], 2u, indices) &&
                        phy_corpus_expr_parse(cas, values[component], &value,
                                              NULL) == PHY_OK &&
                        phy_tensor_set(metric, indices, value) == PHY_OK;
            }
            if (built) {
                phy_gr_result *result = NULL;
                const phy_status status =
                    phy_gr_compute(cas, metric, &result);
                /*
                 * The cap either bites -- and then it must bite cleanly -- or
                 * it does not, and the pass must have succeeded outright.
                 * A partially filled result is what this rules out.
                 */
                if (status == PHY_OK) {
                    PHY_CHECK(result != NULL);
                    phy_gr_result_destroy(result);
                } else {
                    PHY_CHECK(result == NULL);
                    PHY_CHECK(status == PHY_ERR_NODE_LIMIT ||
                              status == PHY_ERR_MEMORY_LIMIT ||
                              status == PHY_ERR_TERM_LIMIT ||
                              status == PHY_ERR_DEPTH_LIMIT ||
                              status == PHY_ERR_TIMEOUT ||
                              status == PHY_ERR_OUT_OF_MEMORY);
                }
            }
        }
        PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
        PHY_CHECK_EQ_INT(phy_cas_validate(cas), PHY_OK);
        phy_tensor_destroy(metric);
        phy_chart_destroy(chart);
        phy_cas_destroy(cas);
        phy_ir_context_destroy(ir);
    }

    /* The same, driven by the CAS step budget rather than the node ceiling. */
    phy_ir_context *ir = phy_ir_context_create(NULL);
    phy_cas_limits budget;
    phy_cas_limits_defaults(&budget);
    budget.max_steps = 1u;
    phy_cas *cas = phy_cas_create(ir, &budget);
    PHY_CHECK(cas != NULL);
    if (cas != NULL) {
        static const char *const names[2] = {"theta", "phi"};
        phy_chart *chart = NULL;
        phy_tensor *metric = NULL;
        static const phy_ir_variance lower[2] = {PHY_IR_INDEX_LOWER,
                                                 PHY_IR_INDEX_LOWER};
        PHY_CHECK_EQ_INT(phy_chart_create(ir, names, 2u, &chart), PHY_OK);
        PHY_CHECK_EQ_INT(phy_tensor_create(chart, "g", 2u, lower, &metric),
                         PHY_OK);
        const unsigned theta_theta[2] = {0u, 0u};
        const unsigned phi_phi[2] = {1u, 1u};
        phy_ir_ref first = PHY_IR_NULL;
        phy_ir_ref second = PHY_IR_NULL;
        if (phy_corpus_expr_parse(cas, "a_0**2", &first, NULL) == PHY_OK &&
            phy_corpus_expr_parse(cas, "a_0**2*sin(theta)**2", &second,
                                  NULL) == PHY_OK &&
            phy_tensor_set(metric, theta_theta, first) == PHY_OK &&
            phy_tensor_set(metric, phi_phi, second) == PHY_OK) {
            phy_gr_result *result = NULL;
            const phy_status status = phy_gr_compute(cas, metric, &result);
            PHY_CHECK(status != PHY_OK);
            PHY_CHECK(result == NULL);
        }
        PHY_CHECK_EQ_INT(phy_cas_validate(cas), PHY_OK);
        phy_tensor_destroy(metric);
        phy_chart_destroy(chart);
    }
    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }
    phy_json_document *corpus = open_corpus();
    if (corpus == NULL) {
        phy_platform_shutdown();
        return 1;
    }
    g_corpus_root = phy_json_root(corpus);

    PHY_TEST_CASE(test_harness_rejects_wrong_values);
    PHY_TEST_CASE(test_harness_rejects_bad_keys);
    PHY_TEST_CASE(test_corpus_scope_is_what_this_suite_covers);
    PHY_TEST_CASE(test_corpus_reproduction);
    PHY_TEST_CASE(test_pipeline_is_deterministic);
    PHY_TEST_CASE(test_resource_limits_fail_cleanly);

    const int result = PHY_TEST_REPORT("test_gr_corpus");
    phy_json_document_destroy(corpus);
    phy_platform_shutdown();
    return result;
}
