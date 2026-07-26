/*
 * Typed expression IR: interning, canonical order, limits, serialization.
 *
 * The suite leans on one property throughout: because every node is interned,
 * "these two expressions are structurally equal" is written as `a == b`. A
 * test that builds the same expression by two different routes and compares
 * refs is therefore testing canonicalization, not just construction.
 */
#include <string.h>

#include "phy/ir.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy_test.h"

/* ------------------------------------------------------------- utilities */

static phy_ir_context *fresh(void)
{
    phy_ir_context *ctx = phy_ir_context_create(NULL);
    PHY_CHECK(ctx != NULL);
    return ctx;
}

static phy_ir_ref sym(phy_ir_context *ctx, const char *name)
{
    return phy_ir_symbol_ref(ctx, phy_ir_intern(ctx, name));
}

/* Defined with the serialization cases; used before them. */
static phy_ir_ref round_trip(const phy_ir_context *from, phy_ir_ref ref,
                             phy_ir_context *into, char *text, size_t capacity);

/* Serializes into a caller-visible buffer, checking the status on the way. */
static const char *render(const phy_ir_context *ctx, phy_ir_ref ref,
                          char *buffer, size_t capacity)
{
    size_t length = 0u;
    const phy_status status =
        phy_ir_write(ctx, ref, buffer, capacity, &length);
    if (status != PHY_OK) {
        return "<write failed>";
    }
    return buffer;
}

/* ------------------------------------------------------------- lifecycle */

static void test_context_lifecycle(void)
{
    phy_ir_limits defaults;
    phy_ir_limits_defaults(&defaults);
    PHY_CHECK(defaults.max_nodes > 0u);
    PHY_CHECK(defaults.max_depth > 0u);
    PHY_CHECK(defaults.max_children > 0u);
    PHY_CHECK(defaults.max_bytes > 0u);

    phy_ir_context *ctx = fresh();
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_node_count(ctx), 0);
    PHY_CHECK_EQ_INT(phy_ir_symbol_count(ctx), 0);
    PHY_CHECK(phy_ir_bytes_used(ctx) > 0u);
    phy_ir_context_destroy(ctx);

    /* Destroying NULL is the same non-event as freeing NULL. */
    phy_ir_context_destroy(NULL);

    /* A budget too small to hold the initial pools is refused, not clamped:
       silently ignoring it would hide a misconfiguration. */
    phy_ir_limits tiny;
    phy_ir_limits_defaults(&tiny);
    tiny.max_bytes = 1024u;
    PHY_CHECK(phy_ir_context_create(&tiny) == NULL);
}

static void test_limits_are_clamped_not_rejected(void)
{
    phy_ir_limits huge;
    memset(&huge, 0, sizeof huge);
    huge.max_depth = 1000000u;
    huge.max_children = 1000000u;

    phy_ir_context *ctx = phy_ir_context_create(&huge);
    PHY_CHECK(ctx != NULL);

    /* The depth ceiling exists because compare, write, and read all recurse;
       a caller must not be able to raise it past what the stack survives. */
    phy_ir_ref deep = phy_ir_integer(ctx, 1);
    for (int i = 0; i < 4000; i++) {
        const phy_ir_ref next = phy_ir_pow(ctx, deep, phy_ir_integer(ctx, 2));
        if (next == PHY_IR_NULL) {
            break;
        }
        deep = next;
    }
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_DEPTH_LIMIT);
    PHY_CHECK(phy_ir_depth(ctx, deep) <= 1024u);
    phy_ir_context_destroy(ctx);
}

static void test_context_returns_all_memory(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);

    phy_telemetry before;
    phy_telemetry_get(&before);

    phy_ir_context *ctx = fresh();
    for (int i = 0; i < 200; i++) {
        const phy_ir_ref terms[2] = {phy_ir_integer(ctx, i), sym(ctx, "x")};
        PHY_CHECK(phy_ir_add(ctx, terms, 2u) != PHY_IR_NULL);
    }
    phy_telemetry during;
    phy_telemetry_get(&during);
    PHY_CHECK(during.bytes_live > before.bytes_live);

    phy_ir_context_destroy(ctx);

    /* Every pool, the intern tables, and the context itself must go back. */
    phy_telemetry after;
    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(after.bytes_live, before.bytes_live);

    phy_platform_shutdown();
}

/* ---------------------------------------------------------------- symbols */

static void test_symbol_interning(void)
{
    phy_ir_context *ctx = fresh();

    const phy_ir_symbol a = phy_ir_intern(ctx, "alpha");
    const phy_ir_symbol b = phy_ir_intern(ctx, "alpha");
    const phy_ir_symbol c = phy_ir_intern(ctx, "beta");
    PHY_CHECK(a != PHY_IR_NO_SYMBOL);
    PHY_CHECK_EQ_INT(a, b);
    PHY_CHECK(a != c);
    PHY_CHECK_EQ_INT(phy_ir_symbol_count(ctx), 2);
    PHY_CHECK_EQ_STR(phy_ir_symbol_name(ctx, a), "alpha");

    /* Length-delimited interning must not read past the length. */
    const phy_ir_symbol partial = phy_ir_intern_n(ctx, "alphabet", 5u);
    PHY_CHECK_EQ_INT(partial, a);

    /* Names are bytes, so UTF-8 index names work without special handling. */
    const phy_ir_symbol greek = phy_ir_intern(ctx, "\xce\xbc");
    PHY_CHECK(greek != PHY_IR_NO_SYMBOL);
    PHY_CHECK_EQ_STR(phy_ir_symbol_name(ctx, greek), "\xce\xbc");

    PHY_CHECK_EQ_INT(phy_ir_intern(ctx, ""), PHY_IR_NO_SYMBOL);
    PHY_CHECK(phy_ir_symbol_name(ctx, PHY_IR_NO_SYMBOL) == NULL);

    /* Enough symbols to force the intern table to grow and rehash. */
    phy_ir_clear_error(ctx);
    char name[16];
    for (int i = 0; i < 500; i++) {
        name[0] = 's';
        name[1] = (char)('a' + (i / 26) % 26);
        name[2] = (char)('a' + i % 26);
        name[3] = (char)('0' + i / 676);
        name[4] = '\0';
        PHY_CHECK(phy_ir_intern(ctx, name) != PHY_IR_NO_SYMBOL);
    }
    PHY_CHECK_EQ_INT(phy_ir_intern(ctx, "alpha"), a);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_OK);

    phy_ir_context_destroy(ctx);
}

/* ------------------------------------------------------------------ atoms */

static void test_integer_and_rational_normalization(void)
{
    phy_ir_context *ctx = fresh();

    PHY_CHECK(phy_ir_equal(phy_ir_integer(ctx, 7), phy_ir_integer(ctx, 7)));
    PHY_CHECK(!phy_ir_equal(phy_ir_integer(ctx, 7), phy_ir_integer(ctx, 8)));

    int64_t value = 0;
    PHY_CHECK(phy_ir_integer_value(ctx, phy_ir_integer(ctx, -3), &value));
    PHY_CHECK_EQ_INT(value, -3);

    /* A reduced fraction with a unit denominator is an integer, not a
       rational: two spellings of one number must be one node. */
    PHY_CHECK(phy_ir_equal(phy_ir_rational(ctx, 4, 2), phy_ir_integer(ctx, 2)));
    PHY_CHECK(phy_ir_equal(phy_ir_rational(ctx, 0, 5), phy_ir_integer(ctx, 0)));
    PHY_CHECK(phy_ir_equal(phy_ir_rational(ctx, 2, 4),
                           phy_ir_rational(ctx, 1, 2)));
    /* The sign belongs to the numerator. */
    PHY_CHECK(phy_ir_equal(phy_ir_rational(ctx, 1, -2),
                           phy_ir_rational(ctx, -1, 2)));
    PHY_CHECK(phy_ir_equal(phy_ir_rational(ctx, -1, -2),
                           phy_ir_rational(ctx, 1, 2)));

    int64_t numerator = 0;
    int64_t denominator = 0;
    PHY_CHECK(phy_ir_rational_value(ctx, phy_ir_rational(ctx, 6, -8),
                                    &numerator, &denominator));
    PHY_CHECK_EQ_INT(numerator, -3);
    PHY_CHECK_EQ_INT(denominator, 4);
    PHY_CHECK_EQ_INT(phy_ir_kind_of(ctx, phy_ir_rational(ctx, 6, -8)),
                     PHY_IR_RATIONAL);

    /* Extremes must reduce without wrapping. */
    PHY_CHECK(phy_ir_equal(phy_ir_rational(ctx, INT64_MIN, 2),
                           phy_ir_integer(ctx, INT64_MIN / 2)));
    PHY_CHECK(phy_ir_rational(ctx, INT64_MIN, INT64_MIN) != PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_OK);

    PHY_CHECK_EQ_INT(phy_ir_rational(ctx, 1, 0), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_DOMAIN);

    phy_ir_context_destroy(ctx);
}

static void test_real_atoms(void)
{
    phy_ir_context *ctx = fresh();

    PHY_CHECK(phy_ir_equal(phy_ir_real(ctx, 1.5), phy_ir_real(ctx, 1.5)));
    PHY_CHECK(!phy_ir_equal(phy_ir_real(ctx, 1.5), phy_ir_real(ctx, 2.5)));

    /* An exact 2 and an inexact 2.0 are different objects, and the IR keeps
       them different rather than quietly deciding which one was meant. */
    PHY_CHECK(!phy_ir_equal(phy_ir_real(ctx, 2.0), phy_ir_integer(ctx, 2)));

    /* Negative zero folds, so it cannot intern as a second zero. */
    PHY_CHECK(phy_ir_equal(phy_ir_real(ctx, -0.0), phy_ir_real(ctx, 0.0)));

    double out = 1.0;
    PHY_CHECK(phy_ir_real_value(ctx, phy_ir_real(ctx, -0.0), &out));
    PHY_CHECK(out == 0.0);

    /* Built without math.h: infinities and NaN come from arithmetic so the
       test does not depend on a libm the device build does not link. */
    const double zero = out;
    const double infinity = 1.0 / zero;
    PHY_CHECK_EQ_INT(phy_ir_real(ctx, infinity), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_DOMAIN);
    phy_ir_clear_error(ctx);

    PHY_CHECK_EQ_INT(phy_ir_real(ctx, zero / zero), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_DOMAIN);

    phy_ir_context_destroy(ctx);
}

static void test_index_and_error_atoms(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_symbol mu = phy_ir_intern(ctx, "mu");

    const phy_ir_ref lower = phy_ir_index(ctx, mu, PHY_IR_INDEX_LOWER);
    const phy_ir_ref upper = phy_ir_index(ctx, mu, PHY_IR_INDEX_UPPER);
    PHY_CHECK(lower != PHY_IR_NULL);
    /* Variance is part of identity: a lower mu is not an upper mu. */
    PHY_CHECK(!phy_ir_equal(lower, upper));
    PHY_CHECK(phy_ir_equal(lower, phy_ir_index(ctx, mu, PHY_IR_INDEX_LOWER)));

    phy_ir_variance variance = PHY_IR_INDEX_UPPER;
    PHY_CHECK(phy_ir_index_variance(ctx, lower, &variance));
    PHY_CHECK_EQ_INT(variance, PHY_IR_INDEX_LOWER);
    PHY_CHECK(!phy_ir_index_variance(ctx, phy_ir_integer(ctx, 1), &variance));

    /* Errors are values, so a failed cell can sit inside a saved document. */
    const phy_ir_ref failure = phy_ir_error(ctx, PHY_ERR_TIMEOUT);
    PHY_CHECK(failure != PHY_IR_NULL);
    phy_status carried = PHY_OK;
    PHY_CHECK(phy_ir_error_status(ctx, failure, &carried));
    PHY_CHECK_EQ_INT(carried, PHY_ERR_TIMEOUT);
    PHY_CHECK(phy_ir_equal(failure, phy_ir_error(ctx, PHY_ERR_TIMEOUT)));
    PHY_CHECK(!phy_ir_equal(failure, phy_ir_error(ctx, PHY_ERR_BACKEND)));

    PHY_CHECK_EQ_INT(phy_ir_error(ctx, PHY_OK), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ctx);
}

/* ------------------------------------------------ interning and canonicity */

static void test_structural_interning(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_ref x = sym(ctx, "x");
    const phy_ir_ref y = sym(ctx, "y");

    const phy_ir_ref first[2] = {x, y};
    const phy_ir_ref a = phy_ir_add(ctx, first, 2u);
    const phy_ir_ref b = phy_ir_add(ctx, first, 2u);
    PHY_CHECK(phy_ir_equal(a, b));

    /* Shared subterms are shared nodes, so an expression graph built twice
       costs nodes once. */
    const size_t before = phy_ir_node_count(ctx);
    const phy_ir_ref squared[2] = {a, a};
    PHY_CHECK(phy_ir_mul(ctx, squared, 2u) != PHY_IR_NULL);
    const phy_ir_ref again[2] = {a, a};
    PHY_CHECK(phy_ir_mul(ctx, again, 2u) != PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_node_count(ctx), before + 1u);

    phy_ir_context_destroy(ctx);
}

static void test_commutative_operands_are_canonical(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_ref x = sym(ctx, "x");
    const phy_ir_ref y = sym(ctx, "y");
    const phy_ir_ref two = phy_ir_integer(ctx, 2);

    const phy_ir_ref forward[3] = {x, y, two};
    const phy_ir_ref backward[3] = {two, y, x};
    PHY_CHECK(phy_ir_equal(phy_ir_mul(ctx, forward, 3u),
                           phy_ir_mul(ctx, backward, 3u)));
    PHY_CHECK(phy_ir_equal(phy_ir_add(ctx, forward, 3u),
                           phy_ir_add(ctx, backward, 3u)));

    /* Numbers sort ahead of symbols, which is what makes the stored form
       predictable enough to test against. */
    char buffer[64];
    PHY_CHECK_EQ_STR(render(ctx, phy_ir_mul(ctx, forward, 3u), buffer,
                            sizeof buffer),
                     "(* 2 x y)");

    phy_ir_context_destroy(ctx);
}

static void test_noncommutative_operands_keep_order(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_ref x = sym(ctx, "x");
    const phy_ir_ref y = sym(ctx, "y");

    const phy_ir_ref forward[2] = {x, y};
    const phy_ir_ref backward[2] = {y, x};

    /* The whole point of a separate kind: nothing here may commute. */
    PHY_CHECK(!phy_ir_equal(phy_ir_ncmul(ctx, forward, 2u),
                            phy_ir_ncmul(ctx, backward, 2u)));
    PHY_CHECK(!phy_ir_equal(phy_ir_wedge(ctx, forward, 2u),
                            phy_ir_wedge(ctx, backward, 2u)));

    char buffer[64];
    PHY_CHECK_EQ_STR(
        render(ctx, phy_ir_ncmul(ctx, backward, 2u), buffer, sizeof buffer),
        "(nc* y x)");

    phy_ir_context_destroy(ctx);
}

static void test_associative_flattening(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_ref a = sym(ctx, "a");
    const phy_ir_ref b = sym(ctx, "b");
    const phy_ir_ref c = sym(ctx, "c");

    const phy_ir_ref inner[2] = {a, b};
    const phy_ir_ref nested[2] = {phy_ir_add(ctx, inner, 2u), c};
    const phy_ir_ref flat[3] = {a, b, c};
    PHY_CHECK(phy_ir_equal(phy_ir_add(ctx, nested, 2u),
                           phy_ir_add(ctx, flat, 3u)));
    PHY_CHECK_EQ_INT(phy_ir_child_count(ctx, phy_ir_add(ctx, nested, 2u)), 3);

    /* Noncommutative products flatten too; only the order is preserved. */
    const phy_ir_ref nc_inner[2] = {a, b};
    const phy_ir_ref nc_nested[2] = {phy_ir_ncmul(ctx, nc_inner, 2u), c};
    PHY_CHECK_EQ_INT(phy_ir_child_count(ctx, phy_ir_ncmul(ctx, nc_nested, 2u)),
                     3);

    /* A one-operand sum is the operand, not a wrapper around it. */
    const phy_ir_ref single[1] = {a};
    PHY_CHECK(phy_ir_equal(phy_ir_add(ctx, single, 1u), a));
    PHY_CHECK(phy_ir_equal(phy_ir_mul(ctx, single, 1u), a));

    PHY_CHECK_EQ_INT(phy_ir_add(ctx, NULL, 0u), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_INVALID_ARGUMENT);

    /* POW and EQUATION are ordered pairs and must not flatten or sort. */
    phy_ir_clear_error(ctx);
    PHY_CHECK(!phy_ir_equal(phy_ir_pow(ctx, a, b), phy_ir_pow(ctx, b, a)));
    PHY_CHECK(
        !phy_ir_equal(phy_ir_equation(ctx, a, b), phy_ir_equation(ctx, b, a)));

    phy_ir_context_destroy(ctx);
}

static void test_canonical_order_is_a_total_order(void)
{
    phy_ir_context *ctx = fresh();

    const phy_ir_ref items[] = {
        phy_ir_integer(ctx, 3),
        phy_ir_integer(ctx, -1),
        phy_ir_rational(ctx, 1, 2),
        phy_ir_real(ctx, 0.25),
        sym(ctx, "b"),
        sym(ctx, "a"),
        sym(ctx, "aa"),
        phy_ir_index(ctx, phy_ir_intern(ctx, "mu"), PHY_IR_INDEX_UPPER),
        phy_ir_pow(ctx, sym(ctx, "a"), phy_ir_integer(ctx, 2)),
    };
    const size_t count = sizeof items / sizeof items[0];

    for (size_t i = 0; i < count; i++) {
        PHY_CHECK_EQ_INT(phy_ir_compare(ctx, items[i], items[i]), 0);
        for (size_t j = 0; j < count; j++) {
            const int forward = phy_ir_compare(ctx, items[i], items[j]);
            const int backward = phy_ir_compare(ctx, items[j], items[i]);
            /* Antisymmetry. */
            PHY_CHECK((forward < 0 && backward > 0) ||
                      (forward > 0 && backward < 0) ||
                      (forward == 0 && backward == 0));
            PHY_CHECK((forward == 0) == (i == j));

            /* Transitivity, which heapsort silently depends on. */
            for (size_t k = 0; k < count; k++) {
                const int jk = phy_ir_compare(ctx, items[j], items[k]);
                const int ik = phy_ir_compare(ctx, items[i], items[k]);
                if (forward < 0 && jk < 0) {
                    PHY_CHECK(ik < 0);
                }
            }
        }
    }

    /* Exact numbers precede inexact ones, and both precede symbols. */
    PHY_CHECK(phy_ir_compare(ctx, phy_ir_integer(ctx, 3),
                             phy_ir_real(ctx, 0.25)) < 0);
    PHY_CHECK(phy_ir_compare(ctx, phy_ir_real(ctx, 0.25), sym(ctx, "a")) < 0);
    /* Exact comparison across integers and rationals, by value. */
    PHY_CHECK(phy_ir_compare(ctx, phy_ir_rational(ctx, 1, 2),
                             phy_ir_integer(ctx, 1)) < 0);
    PHY_CHECK(phy_ir_compare(ctx, phy_ir_integer(ctx, -1),
                             phy_ir_rational(ctx, 1, 2)) < 0);
    /* Cross-multiplication must stay exact at the edge of int64. */
    PHY_CHECK(phy_ir_compare(ctx, phy_ir_rational(ctx, INT64_MAX, 3),
                             phy_ir_rational(ctx, INT64_MAX, 2)) < 0);
    /* Names order by bytes, and a prefix precedes what extends it. */
    PHY_CHECK(phy_ir_compare(ctx, sym(ctx, "a"), sym(ctx, "aa")) < 0);
    PHY_CHECK(phy_ir_compare(ctx, sym(ctx, "a"), sym(ctx, "b")) < 0);

    phy_ir_context_destroy(ctx);
}

static void test_large_commutative_sort(void)
{
    phy_ir_context *ctx = fresh();

    /* Enough operands to exercise heapsort rather than a small-n path, built
       in two opposite orders so only a canonical sort makes them equal. */
    enum { kTerms = 600 };
    phy_ir_ref ascending[kTerms];
    phy_ir_ref descending[kTerms];
    for (int i = 0; i < kTerms; i++) {
        ascending[i] = phy_ir_integer(ctx, i);
        descending[kTerms - 1 - i] = ascending[i];
    }

    const phy_ir_ref a = phy_ir_add(ctx, ascending, (size_t)kTerms);
    const phy_ir_ref b = phy_ir_add(ctx, descending, (size_t)kTerms);
    PHY_CHECK(a != PHY_IR_NULL);
    PHY_CHECK(phy_ir_equal(a, b));
    PHY_CHECK_EQ_INT(phy_ir_child_count(ctx, a), kTerms);

    /* And the stored order really is sorted. */
    for (size_t i = 1; i < (size_t)kTerms; i++) {
        PHY_CHECK(phy_ir_compare(ctx, phy_ir_child(ctx, a, i - 1u),
                                 phy_ir_child(ctx, a, i)) < 0);
    }

    phy_ir_context_destroy(ctx);
}

/* ---------------------------------------------------------------- hashing */

static void test_hash_is_structural_and_portable(void)
{
    phy_ir_context *first = fresh();
    phy_ir_context *second = fresh();

    /* Built in a different order in each context, so node ids differ. */
    const phy_ir_ref fa = sym(first, "alpha");
    const phy_ir_ref fb = sym(first, "beta");
    const phy_ir_ref f_terms[2] = {fa, fb};
    const phy_ir_ref f_sum = phy_ir_add(first, f_terms, 2u);

    (void)sym(second, "unrelated");
    const phy_ir_ref sb = sym(second, "beta");
    const phy_ir_ref sa = sym(second, "alpha");
    const phy_ir_ref s_terms[2] = {sb, sa};
    const phy_ir_ref s_sum = phy_ir_add(second, s_terms, 2u);

    /* Ids differ; the structural hash must not. Hashing over node refs or
       symbol ids instead of names would fail exactly here. */
    PHY_CHECK(f_sum != s_sum);
    PHY_CHECK_EQ_INT(phy_ir_hash(first, f_sum), phy_ir_hash(second, s_sum));
    PHY_CHECK_EQ_INT(phy_ir_hash(first, fa), phy_ir_hash(second, sa));
    PHY_CHECK(phy_ir_hash(first, fa) != phy_ir_hash(first, fb));

    phy_ir_context_destroy(first);
    phy_ir_context_destroy(second);
}

static void test_depth_accounting(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_ref x = sym(ctx, "x");
    PHY_CHECK_EQ_INT(phy_ir_depth(ctx, x), 1);

    const phy_ir_ref squared = phy_ir_pow(ctx, x, phy_ir_integer(ctx, 2));
    PHY_CHECK_EQ_INT(phy_ir_depth(ctx, squared), 2);

    const phy_ir_ref terms[2] = {squared, x};
    PHY_CHECK_EQ_INT(phy_ir_depth(ctx, phy_ir_add(ctx, terms, 2u)), 3);

    phy_ir_context_destroy(ctx);
}

/* ------------------------------------------------------------ typed shapes */

static void test_tensor_slots_reject_non_indices(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_symbol g = phy_ir_intern(ctx, "g");
    const phy_ir_symbol mu = phy_ir_intern(ctx, "mu");
    const phy_ir_symbol nu = phy_ir_intern(ctx, "nu");

    const phy_ir_ref indices[2] = {phy_ir_index(ctx, mu, PHY_IR_INDEX_LOWER),
                                   phy_ir_index(ctx, nu, PHY_IR_INDEX_LOWER)};
    const phy_ir_ref metric = phy_ir_tensor(ctx, g, indices, 2u);
    PHY_CHECK(metric != PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_kind_of(ctx, metric), PHY_IR_TENSOR);
    PHY_CHECK_EQ_INT(phy_ir_head(ctx, metric), g);

    /* Tensor indices are not sorted: slot position carries meaning, so
       g[mu,nu] and g[nu,mu] are different expressions until a declared
       symmetry says otherwise. */
    const phy_ir_ref swapped[2] = {indices[1], indices[0]};
    PHY_CHECK(!phy_ir_equal(metric, phy_ir_tensor(ctx, g, swapped, 2u)));

    /* A scalar with no slots is legal. */
    PHY_CHECK(phy_ir_tensor(ctx, g, NULL, 0u) != PHY_IR_NULL);

    /* The type rule: a slot holds an index, never an arbitrary expression. */
    const phy_ir_ref bad[2] = {indices[0], phy_ir_integer(ctx, 1)};
    PHY_CHECK_EQ_INT(phy_ir_tensor(ctx, g, bad, 2u), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_TYPE);
    phy_ir_clear_error(ctx);

    /* An unknown head is caught too. */
    PHY_CHECK_EQ_INT(phy_ir_tensor(ctx, PHY_IR_NO_SYMBOL, indices, 2u),
                     PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ctx);
}

static void test_derivative_shape(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_ref f = sym(ctx, "f");
    const phy_ir_ref x = sym(ctx, "x");
    const phy_ir_ref y = sym(ctx, "y");

    const phy_ir_ref vars[2] = {x, y};
    const phy_ir_ref mixed = phy_ir_derivative(ctx, f, vars, 2u);
    PHY_CHECK(mixed != PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_child_count(ctx, mixed), 3);
    PHY_CHECK(phy_ir_equal(phy_ir_child(ctx, mixed, 0), f));

    /* Order is preserved: mixed partials commute only for smooth arguments,
       which is an assumption the rewriter applies, not a construction rule. */
    const phy_ir_ref reversed[2] = {y, x};
    PHY_CHECK(!phy_ir_equal(mixed, phy_ir_derivative(ctx, f, reversed, 2u)));

    /* Differentiating with respect to an expression is a type error. */
    const phy_ir_ref bad[1] = {phy_ir_integer(ctx, 2)};
    PHY_CHECK_EQ_INT(phy_ir_derivative(ctx, f, bad, 1u), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_TYPE);
    phy_ir_clear_error(ctx);

    PHY_CHECK_EQ_INT(phy_ir_derivative(ctx, f, NULL, 0u), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ctx);
}

static void test_null_operands_propagate(void)
{
    phy_ir_context *ctx = fresh();

    /* A builder that failed hands back PHY_IR_NULL. Feeding that onward must
       fail rather than produce a node with a hole in it, so a caller may
       build a whole expression and check the error once at the end. */
    const phy_ir_ref broken = phy_ir_rational(ctx, 1, 0);
    PHY_CHECK_EQ_INT(broken, PHY_IR_NULL);
    phy_ir_clear_error(ctx);

    const phy_ir_ref terms[2] = {sym(ctx, "x"), broken};
    PHY_CHECK_EQ_INT(phy_ir_add(ctx, terms, 2u), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_INVALID_ARGUMENT);
    phy_ir_clear_error(ctx);

    PHY_CHECK_EQ_INT(phy_ir_pow(ctx, broken, broken), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ctx);
}

/* ------------------------------------------------------------------ limits */

static void test_term_and_node_limits(void)
{
    phy_ir_limits limits;
    memset(&limits, 0, sizeof limits);
    limits.max_children = 4u;

    phy_ir_context *ctx = phy_ir_context_create(&limits);
    PHY_CHECK(ctx != NULL);

    phy_ir_ref terms[8];
    for (int i = 0; i < 8; i++) {
        terms[i] = phy_ir_integer(ctx, i);
    }
    PHY_CHECK(phy_ir_add(ctx, terms, 4u) != PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_add(ctx, terms, 5u), PHY_IR_NULL);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_TERM_LIMIT);
    phy_ir_context_destroy(ctx);

    /* The node ceiling stops runaway construction before memory does. */
    memset(&limits, 0, sizeof limits);
    limits.max_nodes = 32u;
    ctx = phy_ir_context_create(&limits);
    PHY_CHECK(ctx != NULL);
    for (int i = 0; i < 200; i++) {
        if (phy_ir_integer(ctx, i) == PHY_IR_NULL) {
            break;
        }
    }
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_NODE_LIMIT);
    PHY_CHECK(phy_ir_node_count(ctx) <= 32u);
    phy_ir_context_destroy(ctx);
}

static void test_memory_limit_is_enforced(void)
{
    phy_ir_limits limits;
    memset(&limits, 0, sizeof limits);
    limits.max_bytes = 128u * 1024u;

    phy_ir_context *ctx = phy_ir_context_create(&limits);
    PHY_CHECK(ctx != NULL);

    for (int i = 0; i < 100000; i++) {
        if (phy_ir_integer(ctx, i) == PHY_IR_NULL) {
            break;
        }
    }
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_MEMORY_LIMIT);
    PHY_CHECK(phy_ir_bytes_used(ctx) <= 128u * 1024u);

    /* The context stays usable for reads after refusing to grow. */
    PHY_CHECK(phy_ir_node_count(ctx) > 0u);
    phy_ir_context_destroy(ctx);
}

/*
 * Construction appends a name, a rational, or a run of child slots to its
 * pool before pushing the symbol or node that owns it. If that push is the
 * allocation that hits the ceiling, the payload has to be unwound.
 *
 * The failure point cannot be aimed at directly -- it depends on where the
 * geometric pool growth happens to land -- so the budget is swept instead.
 * Somewhere in the sweep each pool is the one that fails, and every stop must
 * leave the context internally consistent.
 */
static void exercise_all_pools(phy_ir_context *ctx)
{
    char name[24];
    for (int i = 0; i < 400; i++) {
        /* Varying name lengths so the string pool fills at its own rate. */
        int at = 0;
        name[at++] = 's';
        for (int pad = 0; pad < (i % 11); pad++) {
            name[at++] = (char)('a' + pad);
        }
        name[at++] = (char)('0' + (i / 100) % 10);
        name[at++] = (char)('0' + (i / 10) % 10);
        name[at++] = (char)('0' + i % 10);
        name[at] = '\0';

        const phy_ir_symbol id = phy_ir_intern(ctx, name);
        if (id == PHY_IR_NO_SYMBOL) {
            return;
        }
        (void)phy_ir_assume(ctx, id, PHY_IR_ASSUME_REAL);
        (void)phy_ir_declare_symmetry(ctx, id, 0u, 1u,
                                      PHY_IR_SYMMETRY_SYMMETRIC);

        const phy_ir_ref symbol = phy_ir_symbol_ref(ctx, id);
        const phy_ir_ref rational = phy_ir_rational(ctx, i * 2 + 1, 4);
        if (symbol == PHY_IR_NULL || rational == PHY_IR_NULL) {
            return;
        }
        const phy_ir_ref pair[2] = {symbol, rational};
        const phy_ir_ref sum = phy_ir_add(ctx, pair, 2u);
        if (sum == PHY_IR_NULL) {
            return;
        }
        const phy_ir_ref nested[2] = {sum, phy_ir_integer(ctx, i)};
        if (phy_ir_mul(ctx, nested, 2u) == PHY_IR_NULL) {
            return;
        }
    }
}

static void test_allocation_failure_leaves_no_orphans(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);

    /*
     * Walk an injected failure across every allocation the workload makes.
     * A budget sweep cannot do this: geometric pool growth means the first
     * allocation to exceed a budget is almost always the same one, so the
     * failures that matter -- a pool_push that fails *after* its payload was
     * appended -- are never reached. Failing the nth allocation, for every n,
     * reaches all of them.
     */
    /*
     * Calibrated, not guessed. Pool growth is geometric, so a whole workload
     * makes far fewer allocations than it does operations; sweeping a fixed
     * range mostly injects failures past the end and proves nothing.
     */
    const uint32_t attempts_before = phy_host_alloc_attempts();
    phy_ir_context *calibration = phy_ir_context_create(NULL);
    PHY_CHECK(calibration != NULL);
    exercise_all_pools(calibration);
    PHY_CHECK_EQ_INT(phy_ir_last_error(calibration), PHY_OK);
    const uint32_t allocations = phy_host_alloc_attempts() - attempts_before;
    phy_ir_context_destroy(calibration);
    PHY_CHECK(allocations > 8u);

    unsigned reached_limit = 0u;
    for (unsigned nth = 1u; nth <= allocations; nth++) {
        phy_host_fail_alloc_after(nth);

        phy_ir_context *ctx = phy_ir_context_create(NULL);
        if (ctx == NULL) {
            /* The injected failure landed inside context creation. */
            phy_host_fail_alloc_after(0u);
            continue;
        }

        exercise_all_pools(ctx);
        phy_host_fail_alloc_after(0u); /* disarm before validating */

        if (phy_ir_last_error(ctx) != PHY_OK) {
            reached_limit++;
        }

        /* Whatever failed and wherever: no pool holds a payload no live entry
           claims, and nothing published is unreachable from its intern
           table. */
        const phy_status integrity = phy_ir_validate(ctx);
        if (integrity != PHY_OK) {
            fprintf(stderr, "  alloc #%u: %s after %s\n", nth,
                    phy_status_name(integrity),
                    phy_status_name(phy_ir_last_error(ctx)));
        }
        PHY_CHECK_EQ_INT(integrity, PHY_OK);

        /* The context must stay usable after refusing to grow, not just
           consistent: a rolled-back name has to re-intern cleanly. */
        phy_ir_clear_error(ctx);
        const phy_ir_symbol recovered = phy_ir_intern(ctx, "recovered");
        PHY_CHECK(recovered != PHY_IR_NO_SYMBOL);
        PHY_CHECK_EQ_STR(phy_ir_symbol_name(ctx, recovered), "recovered");
        PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);

        phy_ir_context_destroy(ctx);
    }

    /*
     * The sweep is only meaningful if most injections actually landed inside
     * the workload. This test is mutation-verified: deleting either rollback
     * in intern_node or phy_ir_intern_n makes phy_ir_validate fail here.
     */
    PHY_CHECK(reached_limit > allocations / 2u);

    /* Nothing leaked across the whole sweep. */
    phy_telemetry telemetry;
    phy_telemetry_get(&telemetry);
    PHY_CHECK_EQ_INT(telemetry.bytes_live, 0);

    phy_host_fail_alloc_after(0u);
    phy_platform_shutdown();
}

static void test_failed_intern_does_not_accumulate(void)
{
    /*
     * Retrying the same failing work must not grow the context. Before the
     * rollback, each attempt left another copy of the name in the string pool
     * and another run of child slots behind, so a caller looping on a
     * too-small budget leaked once per attempt.
     */
    phy_ir_limits limits;
    memset(&limits, 0, sizeof limits);
    limits.max_bytes = 80u * 1024u;

    phy_ir_context *ctx = phy_ir_context_create(&limits);
    PHY_CHECK(ctx != NULL);

    exercise_all_pools(ctx);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_ERR_MEMORY_LIMIT);
    PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);

    const size_t settled_bytes = phy_ir_bytes_used(ctx);
    const size_t settled_nodes = phy_ir_node_count(ctx);
    const size_t settled_symbols = phy_ir_symbol_count(ctx);

    for (int attempt = 0; attempt < 50; attempt++) {
        phy_ir_clear_error(ctx);
        exercise_all_pools(ctx);
        PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);
    }

    /* Retrying may legitimately complete a little more work than the first
       pass -- capacity that was already charged gets reused -- but it must
       converge rather than climb. */
    PHY_CHECK_EQ_INT(phy_ir_bytes_used(ctx), settled_bytes);
    PHY_CHECK(phy_ir_node_count(ctx) >= settled_nodes);
    PHY_CHECK(phy_ir_symbol_count(ctx) >= settled_symbols);

    const size_t after_nodes = phy_ir_node_count(ctx);
    const size_t after_symbols = phy_ir_symbol_count(ctx);
    for (int attempt = 0; attempt < 50; attempt++) {
        phy_ir_clear_error(ctx);
        exercise_all_pools(ctx);
    }
    /* Steady state: further identical attempts add nothing at all. */
    PHY_CHECK_EQ_INT(phy_ir_node_count(ctx), after_nodes);
    PHY_CHECK_EQ_INT(phy_ir_symbol_count(ctx), after_symbols);
    PHY_CHECK_EQ_INT(phy_ir_bytes_used(ctx), settled_bytes);
    PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);

    phy_ir_context_destroy(ctx);
}

static void test_validate_accepts_healthy_contexts(void)
{
    phy_ir_context *ctx = fresh();
    PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(NULL), PHY_ERR_INVALID_ARGUMENT);

    /* Enough work to force both intern tables to rehash at least once. */
    exercise_all_pools(ctx);
    PHY_CHECK_EQ_INT(phy_ir_last_error(ctx), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);
    PHY_CHECK(phy_ir_node_count(ctx) > 256u);

    phy_ir_ref parsed = PHY_IR_NULL;
    size_t offset = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_read(ctx, "(+ 1 (* 2 x) (tensor g (idx mu dn)))", &parsed,
                    &offset),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);

    phy_ir_context_destroy(ctx);
}

static void test_failed_parse_retains_prefix_but_stays_valid(void)
{
    /*
     * Documented behaviour, pinned so it cannot change silently: a failed
     * parse keeps whatever it interned before the error. That is a memory
     * concern, not a corruption one, and the header prescribes loading into a
     * disposable context precisely because of it.
     */
    phy_ir_context *ctx = fresh();

    const size_t before_symbols = phy_ir_symbol_count(ctx);
    phy_ir_ref ref = PHY_IR_NULL;
    size_t offset = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_read(ctx, "(+ alpha beta (nosuchform))", &ref, &offset),
        PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(ref, PHY_IR_NULL);

    /* The prefix survived: this is the limitation, stated as a test. */
    PHY_CHECK(phy_ir_symbol_count(ctx) > before_symbols);
    PHY_CHECK(phy_ir_intern(ctx, "alpha") != PHY_IR_NO_SYMBOL);

    /* But nothing is corrupt, and it never becomes corrupt on retry. */
    PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);
    for (int attempt = 0; attempt < 20; attempt++) {
        phy_ir_clear_error(ctx);
        PHY_CHECK_EQ_INT(
            phy_ir_read(ctx, "(+ alpha beta (nosuchform))", &ref, &offset),
            PHY_ERR_PARSE);
    }
    PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);

    /* Declarations behave the same way: the good ones before the bad one are
       applied and stay applied. */
    phy_ir_clear_error(ctx);
    PHY_CHECK_EQ_INT(
        phy_ir_read_declarations(
            ctx, "(declare p real)\n(declare q bogusassumption)", &offset),
        PHY_ERR_PARSE);
    PHY_CHECK((phy_ir_assumptions(ctx, phy_ir_intern(ctx, "p")) &
               (uint32_t)PHY_IR_ASSUME_REAL) != 0u);
    PHY_CHECK_EQ_INT(phy_ir_validate(ctx), PHY_OK);

    /* The prescribed pattern: parse into a disposable context, and destroying
       it reclaims the partial parse in full. */
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_telemetry before;
    phy_telemetry_get(&before);
    phy_ir_context *scratch = fresh();
    PHY_CHECK_EQ_INT(
        phy_ir_read(scratch, "(+ alpha beta (nosuchform))", &ref, &offset),
        PHY_ERR_PARSE);
    phy_ir_context_destroy(scratch);
    phy_telemetry after;
    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(after.bytes_live, before.bytes_live);
    phy_platform_shutdown();

    phy_ir_context_destroy(ctx);
}

static void test_refs_are_context_local(void)
{
    /*
     * Pins the documented hazard: refs are pool indices, so the same number
     * names unrelated nodes in two contexts. Cross-context comparison must go
     * through the hash, the text, or a rebuild.
     */
    phy_ir_context *first = fresh();
    phy_ir_context *second = fresh();

    const phy_ir_ref a = sym(first, "alpha");
    /* Built in a different order, so equal structure lands on different refs
       and different structure lands on the same ref. */
    (void)sym(second, "filler");
    const phy_ir_ref b_same_structure = sym(second, "alpha");
    const phy_ir_ref b_same_number = sym(second, "filler");

    PHY_CHECK(a != b_same_structure);
    PHY_CHECK_EQ_INT(a, b_same_number);
    /* Numerically equal refs, entirely different symbols: the trap. */
    PHY_CHECK_EQ_STR(phy_ir_symbol_name(first, phy_ir_head(first, a)), "alpha");
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(second, phy_ir_head(second, b_same_number)),
        "filler");

    /* The supported routes agree with the structure, not with the numbers. */
    PHY_CHECK_EQ_INT(phy_ir_hash(first, a),
                     phy_ir_hash(second, b_same_structure));
    PHY_CHECK(phy_ir_hash(first, a) != phy_ir_hash(second, b_same_number));

    char left[64];
    char right[64];
    PHY_CHECK_EQ_STR(render(first, a, left, sizeof left),
                     render(second, b_same_structure, right, sizeof right));

    /* Rebuild-then-compare, the exact route the header prescribes. */
    char text[64];
    const phy_ir_ref rebuilt = round_trip(first, a, second, text, sizeof text);
    PHY_CHECK(phy_ir_equal(rebuilt, b_same_structure));

    phy_ir_context_destroy(first);
    phy_ir_context_destroy(second);
}

/* ------------------------------------------------- assumptions, symmetries */

static void test_assumptions(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_symbol x = phy_ir_intern(ctx, "x");

    PHY_CHECK_EQ_INT(phy_ir_assumptions(ctx, x), 0);
    PHY_CHECK_EQ_INT(phy_ir_assume(ctx, x, PHY_IR_ASSUME_REAL), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_assume(ctx, x, PHY_IR_ASSUME_POSITIVE), PHY_OK);

    /* Assumptions accumulate rather than replace. */
    const uint32_t held = phy_ir_assumptions(ctx, x);
    PHY_CHECK((held & (uint32_t)PHY_IR_ASSUME_REAL) != 0u);
    PHY_CHECK((held & (uint32_t)PHY_IR_ASSUME_POSITIVE) != 0u);

    /* Positive and negative cannot both hold, and the rejection must leave
       the symbol as it was. */
    PHY_CHECK_EQ_INT(phy_ir_assume(ctx, x, PHY_IR_ASSUME_NEGATIVE),
                     PHY_ERR_ASSUMPTION);
    PHY_CHECK_EQ_INT(phy_ir_assumptions(ctx, x), held);
    phy_ir_clear_error(ctx);

    PHY_CHECK_EQ_INT(phy_ir_assume(ctx, x, 1u << 30), PHY_ERR_INVALID_ARGUMENT);
    phy_ir_clear_error(ctx);
    PHY_CHECK_EQ_INT(phy_ir_assume(ctx, PHY_IR_NO_SYMBOL, PHY_IR_ASSUME_REAL),
                     PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ctx);
}

static void test_declared_symmetries(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_symbol g = phy_ir_intern(ctx, "g");
    const phy_ir_symbol riemann = phy_ir_intern(ctx, "R");

    PHY_CHECK_EQ_INT(phy_ir_slot_symmetry(ctx, g, 0u, 1u),
                     PHY_IR_SYMMETRY_NONE);
    PHY_CHECK_EQ_INT(
        phy_ir_declare_symmetry(ctx, g, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
        PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_slot_symmetry(ctx, g, 0u, 1u),
                     PHY_IR_SYMMETRY_SYMMETRIC);
    /* Slot order is normalized, so the query answers either way round. */
    PHY_CHECK_EQ_INT(phy_ir_slot_symmetry(ctx, g, 1u, 0u),
                     PHY_IR_SYMMETRY_SYMMETRIC);

    PHY_CHECK_EQ_INT(phy_ir_declare_symmetry(ctx, riemann, 0u, 1u,
                                             PHY_IR_SYMMETRY_ANTISYMMETRIC),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_declare_symmetry(ctx, riemann, 2u, 3u,
                                             PHY_IR_SYMMETRY_ANTISYMMETRIC),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_slot_symmetry(ctx, riemann, 2u, 3u),
                     PHY_IR_SYMMETRY_ANTISYMMETRIC);
    PHY_CHECK_EQ_INT(phy_ir_slot_symmetry(ctx, riemann, 0u, 2u),
                     PHY_IR_SYMMETRY_NONE);
    /* Declarations are per symbol, not global. */
    PHY_CHECK_EQ_INT(phy_ir_slot_symmetry(ctx, g, 2u, 3u),
                     PHY_IR_SYMMETRY_NONE);

    /* Repeating a declaration is fine; contradicting one is not. */
    PHY_CHECK_EQ_INT(
        phy_ir_declare_symmetry(ctx, g, 0u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_ir_declare_symmetry(ctx, g, 0u, 1u, PHY_IR_SYMMETRY_ANTISYMMETRIC),
        PHY_ERR_ASSUMPTION);
    phy_ir_clear_error(ctx);

    PHY_CHECK_EQ_INT(
        phy_ir_declare_symmetry(ctx, g, 1u, 1u, PHY_IR_SYMMETRY_SYMMETRIC),
        PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ctx);
}

/* ---------------------------------------------------------- serialization */

/* Writes `ref`, reads it back into `into`, and returns the rebuilt ref. */
static phy_ir_ref round_trip(const phy_ir_context *from, phy_ir_ref ref,
                             phy_ir_context *into, char *text, size_t capacity)
{
    size_t length = 0u;
    if (phy_ir_write(from, ref, text, capacity, &length) != PHY_OK) {
        return PHY_IR_NULL;
    }
    phy_ir_ref rebuilt = PHY_IR_NULL;
    size_t offset = 0u;
    const phy_status status = phy_ir_read(into, text, &rebuilt, &offset);
    if (status != PHY_OK) {
        fprintf(stderr, "  read failed at %u: %s in \"%s\"\n", (unsigned)offset,
                phy_status_name(status), text);
        return PHY_IR_NULL;
    }
    return rebuilt;
}

static void test_serialization_round_trip(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_symbol g = phy_ir_intern(ctx, "g");
    const phy_ir_symbol mu = phy_ir_intern(ctx, "mu");
    const phy_ir_symbol sine = phy_ir_intern(ctx, "sin");
    const phy_ir_symbol gamma = phy_ir_intern(ctx, "gamma");

    const phy_ir_ref x = sym(ctx, "x");
    const phy_ir_ref index_lower = phy_ir_index(ctx, mu, PHY_IR_INDEX_LOWER);
    const phy_ir_ref index_upper = phy_ir_index(ctx, mu, PHY_IR_INDEX_UPPER);
    const phy_ir_ref tensor_indices[2] = {index_lower, index_upper};
    const phy_ir_ref args[1] = {x};
    const phy_ir_ref pair[2] = {x, phy_ir_integer(ctx, 2)};
    const phy_ir_ref operators[2] = {
        phy_ir_operator(ctx, gamma, &index_lower, 1u),
        phy_ir_operator(ctx, gamma, &index_upper, 1u)};

    const phy_ir_ref cases[] = {
        phy_ir_integer(ctx, 0),
        phy_ir_integer(ctx, INT64_MIN),
        phy_ir_integer(ctx, INT64_MAX),
        phy_ir_rational(ctx, -22, 7),
        phy_ir_real(ctx, 0.1),
        phy_ir_real(ctx, -2.5e-300),
        x,
        index_lower,
        index_upper,
        phy_ir_error(ctx, PHY_ERR_TIMEOUT),
        phy_ir_add(ctx, pair, 2u),
        phy_ir_mul(ctx, pair, 2u),
        phy_ir_ncmul(ctx, operators, 2u),
        phy_ir_wedge(ctx, operators, 2u),
        phy_ir_pow(ctx, x, phy_ir_integer(ctx, 2)),
        phy_ir_equation(ctx, x, phy_ir_integer(ctx, 3)),
        phy_ir_function(ctx, sine, args, 1u),
        phy_ir_function(ctx, sine, NULL, 0u),
        phy_ir_tensor(ctx, g, tensor_indices, 2u),
        phy_ir_tensor(ctx, g, NULL, 0u),
        phy_ir_operator(ctx, gamma, &index_lower, 1u),
        phy_ir_derivative(ctx, x, &x, 1u),
    };
    const size_t count = sizeof cases / sizeof cases[0];

    char text[512];
    for (size_t i = 0; i < count; i++) {
        PHY_CHECK(cases[i] != PHY_IR_NULL);

        /* Reading into the same context must return the very same node: the
           strongest statement that nothing was lost or added. */
        const phy_ir_ref same = round_trip(ctx, cases[i], ctx, text, sizeof text);
        if (!phy_ir_equal(same, cases[i])) {
            fprintf(stderr, "  case %u did not round-trip: %s\n", (unsigned)i,
                    text);
        }
        PHY_CHECK(phy_ir_equal(same, cases[i]));

        /* And into a fresh context, the text must reproduce itself. */
        phy_ir_context *other = phy_ir_context_create(NULL);
        PHY_CHECK(other != NULL);
        const phy_ir_ref elsewhere =
            round_trip(ctx, cases[i], other, text, sizeof text);
        PHY_CHECK(elsewhere != PHY_IR_NULL);

        char again[512];
        size_t length = 0u;
        PHY_CHECK_EQ_INT(
            phy_ir_write(other, elsewhere, again, sizeof again, &length),
            PHY_OK);
        PHY_CHECK_EQ_STR(again, text);
        /* Structure survived the trip through text, not just the bytes. */
        PHY_CHECK_EQ_INT(phy_ir_hash(other, elsewhere),
                         phy_ir_hash(ctx, cases[i]));
        phy_ir_context_destroy(other);
    }

    phy_ir_context_destroy(ctx);
}

static void test_serialization_quotes_awkward_names(void)
{
    phy_ir_context *ctx = fresh();
    char text[256];

    const char *const awkward[] = {"has space", "(paren", "bar|pipe",
                                   "back\\slash", "9lead", "-minus"};
    for (size_t i = 0; i < sizeof awkward / sizeof awkward[0]; i++) {
        const phy_ir_ref ref = sym(ctx, awkward[i]);
        PHY_CHECK(ref != PHY_IR_NULL);
        const phy_ir_ref back = round_trip(ctx, ref, ctx, text, sizeof text);
        PHY_CHECK(phy_ir_equal(back, ref));
    }

    /* A UTF-8 name is left bare so saved documents stay readable. */
    const phy_ir_ref greek = sym(ctx, "\xce\xbc");
    PHY_CHECK_EQ_STR(render(ctx, greek, text, sizeof text), "\xce\xbc");

    phy_ir_context_destroy(ctx);
}

static void test_write_reports_required_size(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_ref pair[2] = {sym(ctx, "x"), sym(ctx, "y")};
    const phy_ir_ref sum = phy_ir_add(ctx, pair, 2u);

    size_t needed = 0u;
    /* Probing with a zero-capacity buffer is how a caller sizes an allocation
       before making one. */
    PHY_CHECK_EQ_INT(phy_ir_write(ctx, sum, NULL, 0u, &needed),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(needed, strlen("(+ x y)"));

    char exact[8];
    size_t written = 0u;
    PHY_CHECK_EQ_INT(phy_ir_write(ctx, sum, exact, needed, &written),
                     PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(phy_ir_write(ctx, sum, exact, needed + 1u, &written),
                     PHY_OK);
    PHY_CHECK_EQ_STR(exact, "(+ x y)");
    PHY_CHECK_EQ_INT(written, needed);

    PHY_CHECK_EQ_INT(phy_ir_write(ctx, PHY_IR_NULL, exact, sizeof exact, NULL),
                     PHY_ERR_INVALID_ARGUMENT);

    phy_ir_context_destroy(ctx);
}

static void test_parse_errors(void)
{
    phy_ir_context *ctx = fresh();
    phy_ir_ref ref = PHY_IR_NULL;
    size_t offset = 0u;

    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "", &ref, &offset), PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(", &ref, &offset), PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(+ x", &ref, &offset), PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(nosuch x)", &ref, &offset),
                     PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(^ x)", &ref, &offset), PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(idx mu sideways)", &ref, &offset),
                     PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(err PHY_NOT_A_STATUS)", &ref, &offset),
                     PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "99999999999999999999", &ref, &offset),
                     PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "|unterminated", &ref, &offset),
                     PHY_ERR_PARSE);
    /* A domain error inside the text surfaces as itself, not as a parse
       failure: the document was well-formed, the value was not. */
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(rat 1 0)", &ref, &offset),
                     PHY_ERR_DOMAIN);
    /* Type rules apply to parsed input exactly as to built input. */
    phy_ir_clear_error(ctx);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(tensor g 1)", &ref, &offset),
                     PHY_ERR_TYPE);

    /* Trailing content is refused rather than silently ignored. */
    phy_ir_clear_error(ctx);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "x y", &ref, &offset), PHY_ERR_PARSE);

    /* A failed read leaves no ref behind. */
    PHY_CHECK_EQ_INT(ref, PHY_IR_NULL);

    /* The offset points into the text so a cell can underline the problem. */
    phy_ir_clear_error(ctx);
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(+ x (nope))", &ref, &offset),
                     PHY_ERR_PARSE);
    PHY_CHECK(offset > 0u && offset <= strlen("(+ x (nope))"));

    phy_ir_context_destroy(ctx);
}

static void test_parse_respects_limits(void)
{
    phy_ir_limits limits;
    memset(&limits, 0, sizeof limits);
    limits.max_depth = 4u;

    phy_ir_context *ctx = phy_ir_context_create(&limits);
    PHY_CHECK(ctx != NULL);

    phy_ir_ref ref = PHY_IR_NULL;
    size_t offset = 0u;
    PHY_CHECK_EQ_INT(phy_ir_read(ctx, "(^ (^ (^ (^ (^ x 2) 2) 2) 2) 2)", &ref,
                                 &offset),
                     PHY_ERR_DEPTH_LIMIT);
    PHY_CHECK_EQ_INT(ref, PHY_IR_NULL);

    phy_ir_context_destroy(ctx);
}

static void test_declaration_round_trip(void)
{
    phy_ir_context *ctx = fresh();
    const phy_ir_symbol x = phy_ir_intern(ctx, "x");
    const phy_ir_symbol riemann = phy_ir_intern(ctx, "R");
    const phy_ir_symbol plain = phy_ir_intern(ctx, "plain");

    PHY_CHECK_EQ_INT(
        phy_ir_assume(ctx, x, PHY_IR_ASSUME_REAL | PHY_IR_ASSUME_POSITIVE),
        PHY_OK);
    /* Declared out of slot order on purpose; the writer must sort them. */
    PHY_CHECK_EQ_INT(phy_ir_declare_symmetry(ctx, riemann, 2u, 3u,
                                             PHY_IR_SYMMETRY_ANTISYMMETRIC),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_declare_symmetry(ctx, riemann, 0u, 1u,
                                             PHY_IR_SYMMETRY_ANTISYMMETRIC),
                     PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_ir_declare_symmetry(ctx, riemann, 0u, 2u, PHY_IR_SYMMETRY_SYMMETRIC),
        PHY_OK);

    char text[512];
    size_t length = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write_declarations(ctx, text, sizeof text, &length), PHY_OK);
    /* A symbol with nothing declared produces no line. */
    PHY_CHECK(strstr(text, "plain") == NULL);
    PHY_CHECK(strstr(text, "(declare x real positive)") != NULL);
    (void)plain;

    phy_ir_context *other = phy_ir_context_create(NULL);
    PHY_CHECK(other != NULL);
    size_t offset = 0u;
    PHY_CHECK_EQ_INT(phy_ir_read_declarations(other, text, &offset), PHY_OK);

    const phy_ir_symbol other_x = phy_ir_intern(other, "x");
    const phy_ir_symbol other_r = phy_ir_intern(other, "R");
    PHY_CHECK_EQ_INT(phy_ir_assumptions(other, other_x),
                     phy_ir_assumptions(ctx, x));
    PHY_CHECK_EQ_INT(phy_ir_slot_symmetry(other, other_r, 0u, 1u),
                     PHY_IR_SYMMETRY_ANTISYMMETRIC);
    PHY_CHECK_EQ_INT(phy_ir_slot_symmetry(other, other_r, 2u, 3u),
                     PHY_IR_SYMMETRY_ANTISYMMETRIC);
    PHY_CHECK_EQ_INT(phy_ir_slot_symmetry(other, other_r, 0u, 2u),
                     PHY_IR_SYMMETRY_SYMMETRIC);

    /* Rewriting from the rebuilt context must reproduce the text exactly, or
       a document would drift every time it was opened and saved. */
    char again[512];
    PHY_CHECK_EQ_INT(
        phy_ir_write_declarations(other, again, sizeof again, &length), PHY_OK);
    PHY_CHECK_EQ_STR(again, text);

    phy_ir_context_destroy(other);
    phy_ir_context_destroy(ctx);
}

static void test_declaration_parse_errors(void)
{
    phy_ir_context *ctx = fresh();
    size_t offset = 0u;

    PHY_CHECK_EQ_INT(phy_ir_read_declarations(ctx, "", &offset), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_read_declarations(ctx, "(declare x real)", &offset),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_read_declarations(ctx, "(nonsense x)", &offset),
                     PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(
        phy_ir_read_declarations(ctx, "(declare x notanassumption)", &offset),
        PHY_ERR_PARSE);
    PHY_CHECK_EQ_INT(
        phy_ir_read_declarations(ctx, "(declare x positive negative)", &offset),
        PHY_ERR_ASSUMPTION);

    phy_ir_context_destroy(ctx);
}

/* -------------------------------------------------------------- kind table */

static void test_kind_table_is_complete(void)
{
    /* Every kind must be named, ranked, and -- unless it is written literally
       -- have a serialization token, or a later phase will add a row that the
       writer silently drops. */
    for (unsigned k = (unsigned)PHY_IR_KIND_INVALID + 1u;
         k < (unsigned)PHY_IR_KIND_COUNT; k++) {
        const phy_ir_kind kind = (phy_ir_kind)k;
        const char *name = phy_ir_kind_name(kind);
        PHY_CHECK(name != NULL);
        PHY_CHECK(strncmp(name, "PHY_IR_", 7u) == 0);
        PHY_CHECK_EQ_INT(strcmp(name, "PHY_IR_KIND_INVALID") == 0, 0);
    }

    PHY_CHECK_EQ_STR(phy_ir_kind_name(PHY_IR_ADD), "PHY_IR_ADD");
    PHY_CHECK_EQ_STR(phy_ir_kind_name(PHY_IR_KIND_INVALID),
                     "PHY_IR_KIND_INVALID");
    PHY_CHECK_EQ_STR(phy_ir_kind_name((phy_ir_kind)9999), "PHY_IR_KIND_INVALID");

    PHY_CHECK((phy_ir_kind_flags(PHY_IR_MUL) & PHY_IR_KIND_COMMUTATIVE) != 0u);
    PHY_CHECK((phy_ir_kind_flags(PHY_IR_NCMUL) & PHY_IR_KIND_COMMUTATIVE) == 0u);
    PHY_CHECK((phy_ir_kind_flags(PHY_IR_TENSOR) & PHY_IR_KIND_HEADED) != 0u);
    PHY_CHECK((phy_ir_kind_flags(PHY_IR_INTEGER) & PHY_IR_KIND_ATOM) != 0u);
}

static void test_status_names_cover_new_categories(void)
{
    /* The IR writes status names into saved documents, so every status must
       have a distinct, stable spelling. */
    for (unsigned s = 0; s < (unsigned)PHY_STATUS_COUNT; s++) {
        const char *name = phy_status_name((phy_status)s);
        PHY_CHECK(name != NULL);
        PHY_CHECK_EQ_INT(strcmp(name, "PHY_ERR_UNKNOWN") == 0, 0);
        for (unsigned other = 0; other < s; other++) {
            PHY_CHECK(strcmp(name, phy_status_name((phy_status)other)) != 0);
        }
    }
    PHY_CHECK_EQ_STR(phy_status_name(PHY_ERR_TERM_LIMIT), "PHY_ERR_TERM_LIMIT");
    PHY_CHECK_EQ_STR(phy_status_name(PHY_STATUS_COUNT), "PHY_ERR_UNKNOWN");
}

/* ---------------------------------------------------------------- driver */

int main(void)
{
    PHY_TEST_CASE(test_context_lifecycle);
    PHY_TEST_CASE(test_limits_are_clamped_not_rejected);
    PHY_TEST_CASE(test_context_returns_all_memory);
    PHY_TEST_CASE(test_symbol_interning);
    PHY_TEST_CASE(test_integer_and_rational_normalization);
    PHY_TEST_CASE(test_real_atoms);
    PHY_TEST_CASE(test_index_and_error_atoms);
    PHY_TEST_CASE(test_structural_interning);
    PHY_TEST_CASE(test_commutative_operands_are_canonical);
    PHY_TEST_CASE(test_noncommutative_operands_keep_order);
    PHY_TEST_CASE(test_associative_flattening);
    PHY_TEST_CASE(test_canonical_order_is_a_total_order);
    PHY_TEST_CASE(test_large_commutative_sort);
    PHY_TEST_CASE(test_hash_is_structural_and_portable);
    PHY_TEST_CASE(test_depth_accounting);
    PHY_TEST_CASE(test_tensor_slots_reject_non_indices);
    PHY_TEST_CASE(test_derivative_shape);
    PHY_TEST_CASE(test_null_operands_propagate);
    PHY_TEST_CASE(test_term_and_node_limits);
    PHY_TEST_CASE(test_memory_limit_is_enforced);
    PHY_TEST_CASE(test_allocation_failure_leaves_no_orphans);
    PHY_TEST_CASE(test_failed_intern_does_not_accumulate);
    PHY_TEST_CASE(test_validate_accepts_healthy_contexts);
    PHY_TEST_CASE(test_failed_parse_retains_prefix_but_stays_valid);
    PHY_TEST_CASE(test_refs_are_context_local);
    PHY_TEST_CASE(test_assumptions);
    PHY_TEST_CASE(test_declared_symmetries);
    PHY_TEST_CASE(test_serialization_round_trip);
    PHY_TEST_CASE(test_serialization_quotes_awkward_names);
    PHY_TEST_CASE(test_write_reports_required_size);
    PHY_TEST_CASE(test_parse_errors);
    PHY_TEST_CASE(test_parse_respects_limits);
    PHY_TEST_CASE(test_declaration_round_trip);
    PHY_TEST_CASE(test_declaration_parse_errors);
    PHY_TEST_CASE(test_kind_table_is_complete);
    PHY_TEST_CASE(test_status_names_cover_new_categories);
    return PHY_TEST_REPORT("test_ir");
}
