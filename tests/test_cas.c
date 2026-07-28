/*
 * Scalar CAS: normal form, exact arithmetic, differentiation, and the zero
 * decision.
 *
 * Expressions are written in the IR's own text format and parsed, so a case
 * reads as the mathematics it is about rather than as a page of builder calls.
 * Results are checked two ways: against a serialized normal form, which pins
 * the exact shape, and against another expression through the zero decision,
 * which pins the value and is what a physics module actually asks.
 *
 * The corpus section is the one to read first. It takes the four sphere_2d
 * entries from research/corpus/gr_golden.json that are written in a different
 * trigonometric form from the one a curvature pass computes, and requires the
 * decision procedure to identify them -- which is the whole reason this layer
 * reduces tan and multiple angles at all.
 */
#include <string.h>

#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy_test.h"

/* ------------------------------------------------------------- utilities */

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
} fixture;

static fixture open_fixture(void)
{
    fixture f;
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    return f;
}

static void close_fixture(fixture *f)
{
    /* Every operation must leave both layers validating, so the suite asks on
       the way out of every case rather than in a single dedicated test. */
    PHY_CHECK_EQ_INT(phy_cas_validate(f->cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(f->ir), PHY_OK);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

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

static char g_text[4096];

static const char *render(phy_ir_context *ir, phy_ir_ref ref)
{
    size_t length = 0u;
    if (phy_ir_write(ir, ref, g_text, sizeof g_text, &length) != PHY_OK) {
        return "<write failed>";
    }
    return g_text;
}

/* Simplify `text`, and report the normal form as serialized text. */
static const char *normal(fixture *f, const char *text)
{
    phy_ir_ref out = PHY_IR_NULL;
    const phy_status status =
        phy_cas_simplify(f->cas, parse(f->ir, text), &out);
    if (status != PHY_OK) {
        return phy_status_name(status);
    }
    return render(f->ir, out);
}

static const char *expanded(fixture *f, const char *text)
{
    phy_ir_ref out = PHY_IR_NULL;
    const phy_status status = phy_cas_expand(f->cas, parse(f->ir, text), &out);
    if (status != PHY_OK) {
        return phy_status_name(status);
    }
    return render(f->ir, out);
}

static phy_status simplify_status(fixture *f, const char *text)
{
    phy_ir_ref out = PHY_IR_NULL;
    return phy_cas_simplify(f->cas, parse(f->ir, text), &out);
}

static phy_cas_decision decide(fixture *f, const char *text)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_cas_is_zero(f->cas, parse(f->ir, text), &decision),
                     PHY_OK);
    return decision;
}

/* The decision on `left - right`, which is how a corpus entry is checked. */
static phy_cas_decision compare(fixture *f, const char *left, const char *right)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    const phy_status status =
        phy_cas_equivalent(f->cas, parse(f->ir, left), parse(f->ir, right),
                           &decision);
    if (status != PHY_OK) {
        fprintf(stderr, "  equivalent(%s, %s) failed: %s\n", left, right,
                phy_status_name(status));
        PHY_CHECK_EQ_INT(status, PHY_OK);
    }
    return decision;
}

/* ------------------------------------------------------------- lifecycle */

static void test_lifecycle(void)
{
    phy_cas_limits defaults;
    phy_cas_limits_defaults(&defaults);
    PHY_CHECK(defaults.max_steps > 0u);
    PHY_CHECK(defaults.max_bytes > 0u);

    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    /* The CAS borrows the context; it never creates one. */
    PHY_CHECK(phy_cas_create(NULL, NULL) == NULL);

    phy_cas *cas = phy_cas_create(ir, NULL);
    PHY_CHECK(cas != NULL);
    PHY_CHECK(phy_cas_ir(cas) == ir);
    PHY_CHECK_EQ_INT(phy_cas_steps(cas), 0);
    PHY_CHECK_EQ_INT(phy_cas_total_steps(cas), 0);
    PHY_CHECK(phy_cas_bytes_used(cas) > 0u);
    PHY_CHECK_EQ_INT(phy_cas_validate(cas), PHY_OK);
    phy_cas_destroy(cas);

    /* A budget too small to hold the structure is refused rather than clamped:
       every operation would fail for a reason the caller could not act on. */
    phy_cas_limits tiny;
    memset(&tiny, 0, sizeof tiny);
    tiny.max_bytes = 128u;
    PHY_CHECK(phy_cas_create(ir, &tiny) == NULL);

    phy_cas_destroy(NULL);
    phy_ir_context_destroy(ir);
}

static void test_all_memory_is_returned(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);

    phy_telemetry before;
    phy_telemetry_get(&before);

    fixture f = open_fixture();
    for (int i = 0; i < 40; i++) {
        phy_ir_ref out = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(
            phy_cas_expand(f.cas, parse(f.ir, "(^ (+ x y z) 3)"), &out), PHY_OK);
    }
    phy_telemetry during;
    phy_telemetry_get(&during);
    PHY_CHECK(during.bytes_live > before.bytes_live);

    close_fixture(&f);

    phy_telemetry after;
    phy_telemetry_get(&after);
    PHY_CHECK_EQ_INT(after.bytes_live, before.bytes_live);

    phy_platform_shutdown();
}

/* ------------------------------------------------------------- arithmetic */

static void test_exact_arithmetic(void)
{
    fixture f = open_fixture();
    phy_ir_ref out = PHY_IR_NULL;

    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 6, 4, &out), PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, out), "(rat 3 2)");
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 6, 3, &out), PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, out), "2");
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 1, 0, &out), PHY_ERR_DOMAIN);

    /* Empty n-ary operations are the exact additive and multiplicative
       identities. This also exercises the count-zero path without passing
       null pointers to memcpy, which is not portable C even for zero bytes. */
    PHY_CHECK_EQ_INT(phy_cas_add(f.cas, NULL, 0u, &out), PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, out), "0");
    PHY_CHECK_EQ_INT(phy_cas_mul(f.cas, NULL, 0u, &out), PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, out), "1");

    PHY_CHECK_EQ_STR(normal(&f, "(+ 2 3 4)"), "9");
    PHY_CHECK_EQ_STR(normal(&f, "(* 2 3 4)"), "24");
    PHY_CHECK_EQ_STR(normal(&f, "(+ (rat 1 2) (rat 1 3))"), "(rat 5 6)");
    PHY_CHECK_EQ_STR(normal(&f, "(* (rat 2 3) (rat 3 2))"), "1");
    PHY_CHECK_EQ_STR(normal(&f, "(^ 2 10)"), "1024");
    PHY_CHECK_EQ_STR(normal(&f, "(^ 2 -1)"), "(rat 1 2)");
    PHY_CHECK_EQ_STR(normal(&f, "(^ (rat 2 3) -2)"), "(rat 9 4)");

    /* Exact means exact: leaving int64 is an error, not a wrap and not a
       silent promotion to double. */
    PHY_CHECK_EQ_INT(simplify_status(&f, "(* 4611686018427387904 4)"),
                     PHY_ERR_OVERFLOW);
    PHY_CHECK_EQ_INT(
        simplify_status(&f, "(+ 9223372036854775807 9223372036854775807)"),
        PHY_ERR_OVERFLOW);

    /* An exact power that does not fit stays symbolic instead: unlike a product
       of two numbers, it is still a normal form. */
    PHY_CHECK_EQ_STR(normal(&f, "(^ 2 200)"), "(^ 2 200)");

    close_fixture(&f);
}

static void test_domain_errors(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_INT(simplify_status(&f, "(^ 0 0)"), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(simplify_status(&f, "(^ 0 -1)"), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(simplify_status(&f, "(* x (^ 0 -1))"), PHY_ERR_DOMAIN);

    phy_ir_ref out = PHY_IR_NULL;
    const phy_ir_ref x = parse(f.ir, "x");
    PHY_CHECK_EQ_INT(phy_cas_div(f.cas, x, parse(f.ir, "0"), &out),
                     PHY_ERR_DOMAIN);
    /* Caught because simplification already collected the divisor to zero. */
    PHY_CHECK_EQ_INT(phy_cas_div(f.cas, x, parse(f.ir, "(+ y (* -1 y))"), &out),
                     PHY_ERR_DOMAIN);

    PHY_CHECK_EQ_INT(phy_cas_div(f.cas, x, parse(f.ir, "y"), &out), PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, out), "(* x (^ y -1))");

    close_fixture(&f);
}

/* ------------------------------------------------------------- collection */

static void test_sums_collect(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_STR(normal(&f, "(+ (* 2 x) (* 3 x))"), "(* 5 x)");
    PHY_CHECK_EQ_STR(normal(&f, "(+ x x)"), "(* 2 x)");
    PHY_CHECK_EQ_STR(normal(&f, "(+ x (* -1 x))"), "0");
    PHY_CHECK_EQ_STR(normal(&f, "(+ x 0)"), "x");
    PHY_CHECK_EQ_STR(normal(&f, "(+ (* 2 x y) (* 3 y x))"), "(* 5 x y)");

    /* Terms are gathered by sorting on the non-numeric part, so they need not
       be adjacent in the operand order. */
    PHY_CHECK_EQ_STR(normal(&f, "(+ x (* 2 y) (* 3 x) (* -2 y))"), "(* 4 x)");

    /* The numeric term is one term and it sorts first. */
    PHY_CHECK_EQ_STR(normal(&f, "(+ 1 x 2)"), "(+ 3 x)");

    close_fixture(&f);
}

static void test_products_collect(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_STR(normal(&f, "(* x (^ x 2))"), "(^ x 3)");
    PHY_CHECK_EQ_STR(normal(&f, "(* x x)"), "(^ x 2)");
    PHY_CHECK_EQ_STR(normal(&f, "(* x (^ x -1))"), "1");
    PHY_CHECK_EQ_STR(normal(&f, "(* 1 x)"), "x");
    PHY_CHECK_EQ_STR(normal(&f, "(* 0 x)"), "0");
    PHY_CHECK_EQ_STR(normal(&f, "(* 2 x 3)"), "(* 6 x)");
    PHY_CHECK_EQ_STR(normal(&f, "(* (^ x a) (^ x b))"), "(^ x (+ a b))");
    PHY_CHECK_EQ_STR(normal(&f, "(* (^ x a) (^ x (* -1 a)))"), "1");

    /* A merged exponent that folds rejoins the coefficient rather than sitting
       in the factor list as a number. */
    PHY_CHECK_EQ_STR(normal(&f, "(* (^ 2 3) (^ 2 4))"), "128");

    /*
     * Integer powers of opposite polynomial bases collect exactly.  Both
     * parity directions are pinned because the canonical sort may retain
     * either A or -A; the coefficient must compensate for that choice.
     */
    PHY_CHECK_EQ_STR(
        normal(&f,
               "(* (^ (+ x y) 2) (^ (+ (* -1 x) (* -1 y)) 3))"),
        "(* -1 (^ (+ x y) 5))");
    PHY_CHECK_EQ_STR(
        normal(&f,
               "(* (^ (+ x y) 3) (^ (+ (* -1 x) (* -1 y)) 2))"),
        "(^ (+ x y) 5)");
    PHY_CHECK_EQ_STR(
        normal(&f,
               "(* (^ (+ x y) -1) (+ (* -1 x) (* -1 y)))"),
        "-1");
    PHY_CHECK_EQ_STR(
        normal(&f,
               "(* (+ x y) (^ (+ (* -1 x) (* -1 y)) -1))"),
        "-1");

    close_fixture(&f);
}

static void test_power_rules(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_STR(normal(&f, "(^ x 1)"), "x");
    PHY_CHECK_EQ_STR(normal(&f, "(^ x 0)"), "1");
    PHY_CHECK_EQ_STR(normal(&f, "(^ 1 x)"), "1");
    PHY_CHECK_EQ_STR(normal(&f, "(^ (^ x 2) 3)"), "(^ x 6)");
    PHY_CHECK_EQ_STR(normal(&f, "(^ (* x y) 2)"), "(* (^ x 2) (^ y 2))");
    PHY_CHECK_EQ_STR(normal(&f, "(^ (* 2 x) 3)"), "(* 8 (^ x 3))");
    PHY_CHECK_EQ_STR(normal(&f, "(^ (^ x (rat 1 2)) 2)"), "x");

    /*
     * A non-integer outer exponent is NOT combined: (x^2)^(1/2) is |x|, not x,
     * and a CAS that rewrites it has quietly changed the function.
     */
    PHY_CHECK_EQ_STR(normal(&f, "(^ (^ x 2) (rat 1 2))"),
                     "(^ (^ x 2) (rat 1 2))");

    close_fixture(&f);
}

static void test_known_functions(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_STR(normal(&f, "(fn sin 0)"), "0");
    PHY_CHECK_EQ_STR(normal(&f, "(fn cos 0)"), "1");
    PHY_CHECK_EQ_STR(normal(&f, "(fn tan 0)"), "0");
    PHY_CHECK_EQ_STR(normal(&f, "(fn exp 0)"), "1");
    PHY_CHECK_EQ_STR(normal(&f, "(fn log 1)"), "0");
    PHY_CHECK_EQ_STR(normal(&f, "(fn exp (fn log x))"), "x");

    /* Parity, which is what lets sin(-u) + sin(u) collect. */
    PHY_CHECK_EQ_STR(normal(&f, "(fn sin (* -1 x))"), "(* -1 (fn sin x))");
    PHY_CHECK_EQ_STR(normal(&f, "(fn cos (* -1 x))"), "(fn cos x)");
    PHY_CHECK_EQ_STR(normal(&f, "(fn tan (* -3 x))"), "(* -1 (fn tan (* 3 x)))");
    PHY_CHECK_EQ_STR(normal(&f, "(+ (fn sin (* -1 x)) (fn sin x))"), "0");

    /* An unknown head is left alone, arguments simplified. */
    PHY_CHECK_EQ_STR(normal(&f, "(fn bessel (+ x x))"), "(fn bessel (* 2 x))");

    /*
     * log(exp(u)) is NOT rewritten: it differs from u by a multiple of 2*pi*i,
     * so the identity holds only on a strip and this layer does not know it is
     * on one.
     */
    PHY_CHECK_EQ_STR(normal(&f, "(fn log (fn exp x))"), "(fn log (fn exp x))");

    close_fixture(&f);
}

static void test_simplify_is_idempotent(void)
{
    static const char *const cases[] = {
        "(+ (* 2 x) (* 3 x) y)",
        "(* (^ x 2) (^ x -3) y)",
        "(^ (+ x 1) 3)",
        "(+ (fn sin x) (fn cos x) (fn sin x))",
        "(* (fn tan theta) (^ (fn tan theta) -1))",
        "(= (+ x x) (* 2 x))",
        "(d (* x y) x)",
        "(nc* A B A)",
    };

    fixture f = open_fixture();
    for (size_t i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
        phy_ir_ref once = PHY_IR_NULL;
        phy_ir_ref twice = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(
            phy_cas_simplify(f.cas, parse(f.ir, cases[i]), &once), PHY_OK);
        PHY_CHECK_EQ_INT(phy_cas_simplify(f.cas, once, &twice), PHY_OK);
        /* Interning makes this exact: a second pass that changed anything would
           produce a different ref. */
        PHY_CHECK(phy_ir_equal(once, twice));
    }
    close_fixture(&f);
}

static void test_errors_propagate_as_values(void)
{
    fixture f = open_fixture();

    /* A typed error swallows the expression that contains it, so a failed cell
       stays an error instead of becoming a plausible-looking result. */
    PHY_CHECK_EQ_STR(normal(&f, "(+ x (err PHY_ERR_DOMAIN))"),
                     "(err PHY_ERR_DOMAIN)");
    PHY_CHECK_EQ_STR(normal(&f, "(* 2 (^ (err PHY_ERR_TIMEOUT) 2))"),
                     "(err PHY_ERR_TIMEOUT)");

    phy_ir_ref derivative = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_diff(f.cas, parse(f.ir, "(err PHY_ERR_DOMAIN)"),
                     parse(f.ir, "x"), &derivative),
        PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, derivative), "(err PHY_ERR_DOMAIN)");

    /* And it is not decided to be zero. */
    PHY_CHECK_EQ_INT(decide(&f, "(err PHY_ERR_DOMAIN)"), PHY_CAS_UNKNOWN);

    close_fixture(&f);
}

static void test_noncommutative_kinds_are_left_alone(void)
{
    fixture f = open_fixture();

    /* Operand order is preserved and nothing is collected across it. */
    PHY_CHECK_EQ_STR(normal(&f, "(nc* B A)"), "(nc* B A)");
    PHY_CHECK_EQ_STR(normal(&f, "(nc* (+ x x) A)"), "(nc* (* 2 x) A)");
    PHY_CHECK_EQ_STR(normal(&f, "(wedge b a)"), "(wedge b a)");

    /* Tensor slots carry meaning by position and are not reordered. */
    PHY_CHECK_EQ_STR(normal(&f, "(tensor g (idx nu dn) (idx mu dn))"),
                     "(tensor g (idx nu dn) (idx mu dn))");

    close_fixture(&f);
}

/* ---------------------------------------------------------------- expand */

static void test_expand(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_STR(expanded(&f, "(* (+ x 1) (+ x -1))"), "(+ -1 (^ x 2))");
    PHY_CHECK_EQ_STR(expanded(&f, "(^ (+ x 1) 2)"), "(+ 1 (* 2 x) (^ x 2))");
    PHY_CHECK_EQ_STR(expanded(&f, "(* 2 (+ x y))"), "(+ (* 2 x) (* 2 y))");

    /* A negative power expands its magnitude and inverts, because the rational
       form wants an expanded polynomial in the denominator too. */
    PHY_CHECK_EQ_STR(expanded(&f, "(^ (+ x 1) -2)"),
                     "(^ (+ 1 (* 2 x) (^ x 2)) -1)");

    /*
     * The magnitude of INT64_MIN is not representable in int64_t. Expansion
     * must keep the exact symbolic power instead of negating it (undefined
     * behavior) or attempting 2^63 distribution rounds.
     */
    PHY_CHECK_EQ_STR(
        expanded(&f, "(^ (+ x 1) -9223372036854775808)"),
        "(^ (+ 1 x) -9223372036854775808)");

    /* Expansion does not lose the structure-preserving guarantee of simplify:
       simplify alone leaves the product standing. */
    PHY_CHECK_EQ_STR(normal(&f, "(* (+ x 1) (+ x -1))"),
                     "(* (+ -1 x) (+ 1 x))");

    close_fixture(&f);
}

/* ----------------------------------------------------------- substitution */

static void test_substitute(void)
{
    fixture f = open_fixture();
    phy_ir_ref out = PHY_IR_NULL;

    const phy_ir_ref expr = parse(f.ir, "(+ (* 2 x) y)");
    phy_cas_rule to_three = {parse(f.ir, "x"), parse(f.ir, "3")};
    PHY_CHECK_EQ_INT(phy_cas_substitute(f.cas, expr, &to_three, 1u, &out),
                     PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, out), "(+ 6 y)");

    /* Rules match the original subtrees, so this swaps rather than looping. */
    phy_cas_rule swap[2] = {{parse(f.ir, "x"), parse(f.ir, "y")},
                            {parse(f.ir, "y"), parse(f.ir, "x")}};
    PHY_CHECK_EQ_INT(
        phy_cas_substitute(f.cas, parse(f.ir, "(+ (* 2 x) (* 3 y))"), swap, 2u,
                           &out),
        PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, out), "(+ (* 2 y) (* 3 x))");

    /* A rule may replace any subexpression, not only a symbol, and the result
       is collected on the way back up. */
    phy_cas_rule on_shell = {parse(f.ir, "(^ x 2)"), parse(f.ir, "(* 2 x)")};
    PHY_CHECK_EQ_INT(
        phy_cas_substitute(f.cas, parse(f.ir, "(+ (^ x 2) x)"), &on_shell, 1u,
                           &out),
        PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, out), "(* 3 x)");

    close_fixture(&f);
}

/* --------------------------------------------------------- differentiation */

static const char *derivative(fixture *f, const char *text, const char *variable)
{
    phy_ir_ref out = PHY_IR_NULL;
    const phy_status status = phy_cas_diff(f->cas, parse(f->ir, text),
                                           parse(f->ir, variable), &out);
    if (status != PHY_OK) {
        return phy_status_name(status);
    }
    return render(f->ir, out);
}

static void test_differentiation(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_STR(derivative(&f, "x", "x"), "1");
    PHY_CHECK_EQ_STR(derivative(&f, "y", "x"), "0");
    PHY_CHECK_EQ_STR(derivative(&f, "42", "x"), "0");
    PHY_CHECK_EQ_STR(derivative(&f, "(^ x 3)", "x"), "(* 3 (^ x 2))");
    PHY_CHECK_EQ_STR(derivative(&f, "(* x y)", "x"), "y");
    PHY_CHECK_EQ_STR(derivative(&f, "(+ (^ x 2) (* 3 x) 7)", "x"),
                     "(+ 3 (* 2 x))");
    PHY_CHECK_EQ_STR(derivative(&f, "(^ x -1)", "x"), "(* -1 (^ x -2))");

    /* Chain rule through the known table. */
    PHY_CHECK_EQ_STR(derivative(&f, "(fn sin x)", "x"), "(fn cos x)");
    PHY_CHECK_EQ_STR(derivative(&f, "(fn cos x)", "x"), "(* -1 (fn sin x))");
    PHY_CHECK_EQ_STR(derivative(&f, "(fn tan x)", "x"), "(^ (fn cos x) -2)");
    PHY_CHECK_EQ_STR(derivative(&f, "(fn exp (* 2 x))", "x"),
                     "(* 2 (fn exp (* 2 x)))");
    PHY_CHECK_EQ_STR(derivative(&f, "(fn log x)", "x"), "(^ x -1)");
    PHY_CHECK_EQ_STR(derivative(&f, "(fn sin (^ x 2))", "x"),
                     "(* 2 x (fn cos (^ x 2)))");

    /* x^x needs the general rule, which is where the logarithm appears. The
       sum sorts before the power: canonical order is by kind rank, not by the
       order a reader would write them in. */
    PHY_CHECK_EQ_STR(derivative(&f, "(^ x x)", "x"),
                     "(* (+ 1 (fn log x)) (^ x x))");

    close_fixture(&f);
}

static void test_differentiation_defers_what_it_cannot_see(void)
{
    fixture f = open_fixture();

    /*
     * A tensor component may depend on a coordinate and nothing in the graph
     * says it does not, so the derivative is carried unevaluated. Answering
     * zero here would make a curved spacetime look flat.
     */
    PHY_CHECK_EQ_STR(derivative(&f, "(tensor g (idx mu dn) (idx nu dn))", "r"),
                     "(d (tensor g (idx mu dn) (idx nu dn)) r)");
    PHY_CHECK_EQ_STR(derivative(&f, "(op A x)", "x"), "(d (op A x) x)");

    /* An unknown function of the variable defers; of another variable, it is
       genuinely independent and differentiates to zero. */
    PHY_CHECK_EQ_STR(derivative(&f, "(fn bessel x)", "x"), "(d (fn bessel x) x)");
    PHY_CHECK_EQ_STR(derivative(&f, "(fn bessel y)", "x"), "0");

    /* A repeated derivative gains a variable rather than nesting. */
    PHY_CHECK_EQ_STR(derivative(&f, "(d (fn bessel x) x)", "x"),
                     "(d (fn bessel x) x x)");

    /* Differentiating with respect to an index is tensor calculus, not this. */
    phy_ir_ref out = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_diff(f.cas, parse(f.ir, "(^ x 2)"),
                                  parse(f.ir, "(idx mu dn)"), &out),
                     PHY_ERR_TYPE);

    close_fixture(&f);
}

/* ------------------------------------------------------------ integration */

static const char *antiderivative(fixture *f, const char *text,
                                  const char *variable)
{
    phy_ir_ref out = PHY_IR_NULL;
    const phy_status status =
        phy_cas_integrate(f->cas, parse(f->ir, text),
                          parse(f->ir, variable), &out);
    return status == PHY_OK ? render(f->ir, out) : phy_status_name(status);
}

static void test_exact_symbolic_integration(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_STR(antiderivative(&f, "3", "x"), "(* 3 x)");
    PHY_CHECK_EQ_STR(antiderivative(&f, "x", "x"),
                     "(* (rat 1 2) (^ x 2))");
    PHY_CHECK_EQ_STR(antiderivative(&f, "(^ x 3)", "x"),
                     "(* (rat 1 4) (^ x 4))");
    PHY_CHECK_EQ_STR(antiderivative(&f, "(^ x -1)", "x"), "(fn log x)");
    PHY_CHECK_EQ_STR(antiderivative(&f, "(fn sin (* 2 x))", "x"),
                     "(* (rat -1 2) (fn cos (* 2 x)))");
    PHY_CHECK_EQ_STR(antiderivative(&f, "(fn cos (* 3 x))", "x"),
                     "(* (rat 1 3) (fn sin (* 3 x)))");
    PHY_CHECK_EQ_STR(
        antiderivative(&f, "(fn exp (+ 1 (* 3 x)))", "x"),
        "(* (rat 1 3) (fn exp (+ 1 (* 3 x))))");
    PHY_CHECK_EQ_STR(antiderivative(&f, "(fn log x)", "x"),
                     "(+ (* -1 x) (* x (fn log x)))");
    PHY_CHECK_EQ_STR(
        antiderivative(&f, "(+ 3 x (^ x 2))", "x"),
        "(+ (* (rat 1 3) (^ x 3)) (* (rat 1 2) (^ x 2)) (* 3 x))");

    /* Unsupported classes remain explicit symbolic work, not numeric output. */
    PHY_CHECK_EQ_STR(antiderivative(&f, "(fn bessel x)", "x"),
                     "(fn Integrate (fn bessel x) x)");
    PHY_CHECK_EQ_STR(
        antiderivative(&f, "(tensor g (idx mu dn Lorentz))", "x"),
        "(fn Integrate (tensor g (idx mu dn Lorentz)) x)");

    phy_ir_ref out = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_integrate(f.cas, parse(f.ir, "x"),
                          parse(f.ir, "(idx mu dn)"), &out),
        PHY_ERR_TYPE);

    close_fixture(&f);
}

static void test_supported_integrals_differentiate_back(void)
{
    fixture f = open_fixture();
    static const char *const cases[] = {
        "(+ 3 x (^ x 5))",
        "(fn sin (+ 1 (* 2 x)))",
        "(fn cos (* 3 x))",
        "(fn exp (+ -2 (* 7 x)))",
        "(^ (+ 1 (* 4 x)) (rat 3 2))",
        "(^ (+ 2 (* 5 x)) -1)",
        "(fn log (+ 3 (* 2 x)))",
    };
    const phy_ir_ref x = parse(f.ir, "x");
    for (size_t index = 0u; index < sizeof cases / sizeof cases[0]; ++index) {
        const phy_ir_ref original = parse(f.ir, cases[index]);
        phy_ir_ref integral = PHY_IR_NULL;
        phy_ir_ref derivative_result = PHY_IR_NULL;
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        PHY_CHECK_EQ_INT(
            phy_cas_integrate(f.cas, original, x, &integral), PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_cas_diff(f.cas, integral, x, &derivative_result), PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_cas_equivalent(f.cas, original, derivative_result, &decision),
            PHY_OK);
        PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
    }
    close_fixture(&f);
}

/* ------------------------------------------------------- the zero decision */

static void test_zero_decision_basics(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_INT(decide(&f, "0"), PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(decide(&f, "(+ x (* -1 x))"), PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(decide(&f, "3"), PHY_CAS_NONZERO);
    PHY_CHECK_EQ_INT(decide(&f, "x"), PHY_CAS_UNKNOWN);

    /* The decision is on the expanded form, so a product of sums is decided. */
    PHY_CHECK_EQ_INT(
        compare(&f, "(* (+ x 1) (+ x -1))", "(+ (^ x 2) -1)"), PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(compare(&f, "(^ (+ x y) 2)",
                             "(+ (^ x 2) (* 2 x y) (^ y 2))"),
                     PHY_CAS_ZERO);

    /* Rational functions: a common denominator is taken, and the numerator
       decides. No polynomial GCD is needed for that. */
    PHY_CHECK_EQ_INT(compare(&f, "(* (^ x 2) (^ x -1))", "x"), PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(
        compare(&f, "(+ (^ x -1) (^ y -1))", "(* (+ x y) (^ (* x y) -1))"),
        PHY_CAS_ZERO);

    /* Soundness: something that is not zero is never called zero. */
    PHY_CHECK(decide(&f, "(+ (^ x 2) 1)") != PHY_CAS_ZERO);
    PHY_CHECK(compare(&f, "(^ (+ x 1) 2)", "(+ (^ x 2) 1)") != PHY_CAS_ZERO);

    close_fixture(&f);
}

static void test_zero_decision_reads_assumptions(void)
{
    fixture f = open_fixture();

    PHY_CHECK_EQ_INT(decide(&f, "(* x y)"), PHY_CAS_UNKNOWN);

    const phy_ir_symbol x = phy_ir_intern(f.ir, "x");
    const phy_ir_symbol y = phy_ir_intern(f.ir, "y");
    PHY_CHECK_EQ_INT(phy_ir_assume(f.ir, x, PHY_IR_ASSUME_NONZERO), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_assume(f.ir, y, PHY_IR_ASSUME_POSITIVE), PHY_OK);

    /* Declaring after computing is exactly the case the cache must be cleared
       for; the header says so at the call site. */
    phy_cas_cache_clear(f.cas);

    PHY_CHECK_EQ_INT(decide(&f, "(* x y)"), PHY_CAS_NONZERO);
    PHY_CHECK_EQ_INT(decide(&f, "(^ x 3)"), PHY_CAS_NONZERO);
    /* exp never vanishes, whatever its argument. */
    PHY_CHECK_EQ_INT(decide(&f, "(fn exp z)"), PHY_CAS_NONZERO);
    /* A sum is not decided from assumptions: that is the zero decision itself. */
    PHY_CHECK_EQ_INT(decide(&f, "(+ x y)"), PHY_CAS_UNKNOWN);

    close_fixture(&f);
}

static void test_zero_decision_stays_honest(void)
{
    fixture f = open_fixture();

    /* Inexact values leave the decidable class; they are carried, not folded,
       precisely so that this answers UNKNOWN instead of guessing. */
    PHY_CHECK_EQ_INT(decide(&f, "(+ (real 0x3fb999999999999a) "
                                "(* -1 (real 0x3fb999999999999a)))"),
                     PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(decide(&f, "(+ (real 0x3ff0000000000000) -1)"),
                     PHY_CAS_UNKNOWN);
    PHY_CHECK_EQ_INT(decide(&f, "(real 0x3ff0000000000000)"),
                     PHY_CAS_UNKNOWN);

    /* Documented limits, each answering UNKNOWN rather than ZERO. */
    PHY_CHECK_EQ_INT(compare(&f, "(fn sin (* (rat 1 2) theta))",
                             "(fn sin theta)"),
                     PHY_CAS_UNKNOWN);
    PHY_CHECK_EQ_INT(compare(&f, "(fn sin (+ a b))",
                             "(+ (* (fn sin a) (fn cos b)) "
                             "   (* (fn cos a) (fn sin b)))"),
                     PHY_CAS_UNKNOWN);
    PHY_CHECK_EQ_INT(compare(&f, "(^ 4 500)", "(^ 2 1000)"), PHY_CAS_UNKNOWN);
    PHY_CHECK_EQ_INT(compare(&f, "(tensor g (idx mu dn) (idx nu dn))",
                             "(tensor g (idx nu dn) (idx mu dn))"),
                     PHY_CAS_UNKNOWN);

    close_fixture(&f);
}

static void test_rational_form(void)
{
    fixture f = open_fixture();
    phy_ir_ref numerator = PHY_IR_NULL;
    phy_ir_ref denominator = PHY_IR_NULL;

    PHY_CHECK_EQ_INT(
        phy_cas_rational_form(f.cas, parse(f.ir, "(+ (^ x -1) (^ y -1))"),
                              &numerator, &denominator),
        PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, numerator), "(+ x y)");
    PHY_CHECK_EQ_STR(render(f.ir, denominator), "(* x y)");

    PHY_CHECK_EQ_INT(phy_cas_rational_form(f.cas, parse(f.ir, "(rat 3 4)"),
                                           &numerator, &denominator),
                     PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, numerator), "3");
    PHY_CHECK_EQ_STR(render(f.ir, denominator), "4");

    PHY_CHECK_EQ_INT(
        phy_cas_rational_form(
            f.cas, parse(f.ir, "(^ (+ x 1) -9223372036854775808)"),
            &numerator, &denominator),
        PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, numerator),
                     "(^ (+ 1 x) -9223372036854775808)");
    PHY_CHECK_EQ_STR(render(f.ir, denominator), "1");

    /* A denominator that is identically zero means the expression is defined
       nowhere, which is a domain error rather than an answer. */
    PHY_CHECK_EQ_INT(
        phy_cas_rational_form(
            f.cas,
            parse(f.ir, "(^ (+ (^ (fn sin u) 2) (^ (fn cos u) 2) -1) -1)"),
            &numerator, &denominator),
        PHY_ERR_DOMAIN);

    close_fixture(&f);
}

/* -------------------------------------------------- trigonometric identities */

static void test_reduce_cancels_known_factors(void)
{
    fixture f = open_fixture();
    phy_ir_ref reduced = PHY_IR_NULL;
    phy_cas_decision decision = PHY_CAS_UNKNOWN;

    /* (x^2 - 1)/(x - 1): the denominator's own factor divides out. */
    PHY_CHECK_EQ_INT(
        phy_cas_reduce(
            f.cas,
            parse(f.ir, "(* (+ (^ x 2) -1) (^ (+ x -1) -1))"), &reduced),
        PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, reduced), "(+ 1 x)");

    /* 1/x + 1/x^2 combines over x^2, not x^3. */
    PHY_CHECK_EQ_INT(
        phy_cas_reduce(f.cas, parse(f.ir, "(+ (^ x -1) (^ x -2))"),
                       &reduced),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f.cas, reduced, parse(f.ir, "(* (+ 1 x) (^ x -2))"), &decision),
        PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);

    /* 1/(x-1) - 1/x keeps the factored least common denominator. */
    PHY_CHECK_EQ_INT(
        phy_cas_reduce(
            f.cas,
            parse(f.ir, "(+ (^ (+ x -1) -1) (* -1 (^ x -1)))"), &reduced),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f.cas, reduced,
            parse(f.ir, "(* (^ x -1) (^ (+ x -1) -1))"), &decision),
        PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);

    /* The telescoping identity collapses to the literal zero. */
    PHY_CHECK_EQ_INT(
        phy_cas_reduce(
            f.cas,
            parse(f.ir,
                  "(+ (^ (+ x -1) -1) (* -1 (^ x -1)) "
                  "(* -1 (^ x -1) (^ (+ x -1) -1)))"),
            &reduced),
        PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, reduced), "0");

    close_fixture(&f);
}

static void test_trigonometric_identities(void)
{
    fixture f = open_fixture();

    /* The Pythagorean identity, which is what the cos reduction buys. */
    PHY_CHECK_EQ_INT(decide(&f, "(+ (^ (fn sin u) 2) (^ (fn cos u) 2) -1)"),
                     PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(compare(&f, "(^ (fn cos u) 4)",
                             "(+ 1 (* -2 (^ (fn sin u) 2)) "
                             "     (^ (fn sin u) 4))"),
                     PHY_CAS_ZERO);

    /* Multiple angles, through the addition recurrence. */
    PHY_CHECK_EQ_INT(compare(&f, "(fn sin (* 2 theta))",
                             "(* 2 (fn sin theta) (fn cos theta))"),
                     PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(compare(&f, "(fn cos (* 2 theta))",
                             "(+ 1 (* -2 (^ (fn sin theta) 2)))"),
                     PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(compare(&f, "(fn sin (* 3 theta))",
                             "(+ (* 3 (fn sin theta)) "
                             "   (* -4 (^ (fn sin theta) 3)))"),
                     PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(compare(&f, "(fn cos (* 4 theta))",
                             "(+ 1 (* -8 (^ (fn cos theta) 2)) "
                             "     (* 8 (^ (fn cos theta) 4)))"),
                     PHY_CAS_ZERO);
    /* Parity composes with the reduction. */
    PHY_CHECK_EQ_INT(compare(&f, "(fn sin (* -2 theta))",
                             "(* -2 (fn sin theta) (fn cos theta))"),
                     PHY_CAS_ZERO);

    /* tan becomes a quotient, so it cancels against what it should. */
    PHY_CHECK_EQ_INT(compare(&f, "(fn tan theta)",
                             "(* (fn sin theta) (^ (fn cos theta) -1))"),
                     PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(compare(&f, "(^ (fn tan theta) -1)",
                             "(* (fn cos theta) (^ (fn sin theta) -1))"),
                     PHY_CAS_ZERO);
    PHY_CHECK_EQ_INT(
        compare(&f, "(+ (^ (fn tan u) 2) 1)", "(^ (fn cos u) -2)"),
        PHY_CAS_ZERO);

    /* Simplification itself leaves the reader's spelling alone: the reduction
       belongs to the decision procedure, not to the display form. */
    PHY_CHECK_EQ_STR(normal(&f, "(fn tan theta)"), "(fn tan theta)");
    PHY_CHECK_EQ_STR(normal(&f, "(fn sin (* 2 theta))"),
                     "(fn sin (* 2 theta))");

    close_fixture(&f);
}

/*
 * The four sphere_2d entries from research/corpus/gr_golden.json whose stated
 * form differs from the form a curvature pass computes.
 *
 * Each left-hand side is copied from the corpus; each right-hand side is what
 * the Christoffel/Riemann/Ricci formulas produce from the metric
 * diag(a_0^2, a_0^2 sin(theta)^2). If these were not decided equal, the Phase 3
 * acceptance tests could not use the corpus at all.
 */
static void test_gr_corpus_sphere_2d(void)
{
    fixture f = open_fixture();

    /* christoffel "theta;phi,phi" */
    PHY_CHECK_EQ_INT(compare(&f, "(* (rat -1 2) (fn sin (* 2 theta)))",
                             "(* -1 (fn sin theta) (fn cos theta))"),
                     PHY_CAS_ZERO);

    /* christoffel "phi;theta,phi" */
    PHY_CHECK_EQ_INT(compare(&f, "(^ (fn tan theta) -1)",
                             "(* (fn cos theta) (^ (fn sin theta) -1))"),
                     PHY_CAS_ZERO);

    /* ricci "phi,phi" */
    PHY_CHECK_EQ_INT(
        compare(&f,
                "(+ (* (rat 1 2) (fn sin (* 2 theta)) "
                "      (^ (fn tan theta) -1)) "
                "   (* -1 (fn cos (* 2 theta))))",
                "(^ (fn sin theta) 2)"),
        PHY_CAS_ZERO);

    /* riemann_covariant "theta,phi,theta,phi" */
    PHY_CHECK_EQ_INT(
        compare(&f,
                "(* (^ a_0 2) "
                "   (+ (* (rat 1 2) (fn sin (* 2 theta)) "
                "         (^ (fn tan theta) -1)) "
                "      (* -1 (fn cos (* 2 theta)))))",
                "(* (^ a_0 2) (^ (fn sin theta) 2))"),
        PHY_CAS_ZERO);

    /* And the negative control: the corpus Ricci entry is not equal to a
       neighbouring plausible form, so the four above are not passing because
       everything passes. */
    PHY_CHECK(compare(&f,
                      "(+ (* (rat 1 2) (fn sin (* 2 theta)) "
                      "      (^ (fn tan theta) -1)) "
                      "   (* -1 (fn cos (* 2 theta))))",
                      "(^ (fn cos theta) 2)") != PHY_CAS_ZERO);

    close_fixture(&f);
}

/*
 * Minkowski in spherical coordinates: a chart with non-zero connection
 * coefficients whose curvature must vanish identically. This is the case
 * docs/agent-tasks/TENSOR_CORE.md names as the one a heuristic zero test would
 * turn into a meaningless pass.
 *
 * The radial component is R^r_{theta r theta} with Gamma^r_{theta theta} = -r
 * and Gamma^theta_{r theta} = 1/r. Every other connection coefficient of this
 * chart vanishes, so the formula
 *
 *     d_r Gamma^r_{theta theta} - d_theta Gamma^r_{r theta}
 *         + Gamma^r_{r l} Gamma^l_{theta theta}
 *         - Gamma^r_{theta l} Gamma^l_{r theta}
 *
 * collapses to d_r(-r) - Gamma^r_{theta theta} Gamma^theta_{r theta}, which is
 * -1 + r*(1/r): identically zero without being syntactically zero.
 */
static void test_flat_space_curvature_vanishes(void)
{
    fixture f = open_fixture();

    phy_ir_ref connection = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_diff(f.cas, parse(f.ir, "(* -1 r)"),
                                  parse(f.ir, "r"), &connection),
                     PHY_OK);
    PHY_CHECK_EQ_STR(render(f.ir, connection), "-1");

    PHY_CHECK_EQ_INT(decide(&f, "(+ -1 (* r (^ r -1)))"), PHY_CAS_ZERO);

    /* Dropping the cancelling term leaves the constant -1, which is decided
       nonzero rather than reported as another vanishing component. */
    PHY_CHECK_EQ_INT(decide(&f, "(+ -1 (* -1 r (^ r -1)) (* (^ r -1) r))"),
                     PHY_CAS_NONZERO);

    /* The angular component, which carries the trigonometry:
       sin(theta)^2 - sin(2*theta)/(2*tan(theta)) + cos(2*theta) - ... */
    PHY_CHECK_EQ_INT(
        decide(&f,
               "(+ (^ (fn sin theta) 2) "
               "   (* (rat -1 2) (fn sin (* 2 theta)) (^ (fn tan theta) -1)) "
               "   (fn cos (* 2 theta)) "
               "   (* -1 (fn cos (* 2 theta))) "
               "   (* (rat 1 2) (fn sin (* 2 theta)) (^ (fn tan theta) -1)) "
               "   (* -1 (^ (fn sin theta) 2)))"),
        PHY_CAS_ZERO);

    close_fixture(&f);
}

/* -------------------------------------------------------------- resources */

static bool always_cancel(void *user)
{
    (*(unsigned *)user)++;
    return true;
}

static bool never_cancel(void *user)
{
    (void)user;
    return false;
}

static void test_step_budget(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    phy_cas_limits limits;
    memset(&limits, 0, sizeof limits);
    limits.max_steps = 4u;

    phy_cas *cas = phy_cas_create(ir, &limits);
    PHY_CHECK(cas != NULL);

    phy_ir_ref out = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_expand(cas, parse(ir, "(^ (+ a b c d e) 4)"), &out),
        PHY_ERR_TIMEOUT);
    /* A refused operation must leave the layer usable, not wedged. */
    PHY_CHECK_EQ_INT(phy_cas_validate(cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_simplify(cas, parse(ir, "(+ x x)"), &out), PHY_OK);
    PHY_CHECK_EQ_STR(render(ir, out), "(* 2 x)");

    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

static void test_cancellation(void)
{
    fixture f = open_fixture();
    phy_ir_ref out = PHY_IR_NULL;

    unsigned polls = 0u;
    phy_cas_set_cancel(f.cas, always_cancel, &polls);
    PHY_CHECK_EQ_INT(phy_cas_expand(f.cas, parse(f.ir, "(^ (+ a b c d) 8)"),
                                    &out),
                     PHY_ERR_INTERRUPTED);
    PHY_CHECK(polls > 0u);
    PHY_CHECK_EQ_INT(phy_cas_validate(f.cas), PHY_OK);

    /* A hook that declines to cancel costs nothing but the poll. */
    phy_cas_set_cancel(f.cas, never_cancel, NULL);
    PHY_CHECK_EQ_INT(phy_cas_simplify(f.cas, parse(f.ir, "(+ x x)"), &out),
                     PHY_OK);

    phy_cas_set_cancel(f.cas, NULL, NULL);
    close_fixture(&f);
}

static void test_term_limit(void)
{
    phy_ir_limits limits;
    memset(&limits, 0, sizeof limits);
    limits.max_children = 8u;

    phy_ir_context *ir = phy_ir_context_create(&limits);
    PHY_CHECK(ir != NULL);
    phy_cas *cas = phy_cas_create(ir, NULL);
    PHY_CHECK(cas != NULL);

    /* An expansion that would exceed the IR's term ceiling is refused as a
       typed status rather than being built and then regretted. */
    phy_ir_ref out = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_expand(cas, parse(ir, "(^ (+ a b c) 6)"), &out),
                     PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(phy_cas_validate(cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);

    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

static void test_memoization_pays(void)
{
    fixture f = open_fixture();
    phy_ir_ref out = PHY_IR_NULL;

    /* A shared subterm is simplified once. Interning means the two operands of
       the outer sum are the same node, so the second costs a cache hit. */
    const phy_ir_ref expr = parse(f.ir, "(+ (fn sin (^ (+ x 1) 3)) "
                                        "   (fn cos (^ (+ x 1) 3)))");
    PHY_CHECK_EQ_INT(phy_cas_simplify(f.cas, expr, &out), PHY_OK);
    const uint32_t first = phy_cas_steps(f.cas);
    PHY_CHECK(first > 0u);

    PHY_CHECK_EQ_INT(phy_cas_simplify(f.cas, expr, &out), PHY_OK);
    /* The whole expression is now one cache entry. */
    PHY_CHECK_EQ_INT(phy_cas_steps(f.cas), 0);
    PHY_CHECK(phy_cas_total_steps(f.cas) >= first);

    phy_cas_cache_clear(f.cas);
    PHY_CHECK_EQ_INT(phy_cas_simplify(f.cas, expr, &out), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_steps(f.cas), (long long)first);

    close_fixture(&f);
}

static void test_cache_survives_a_tight_byte_ceiling(void)
{
    phy_ir_context *ir = phy_ir_context_create(NULL);
    PHY_CHECK(ir != NULL);

    /* Small enough that the memo table cannot be kept. The cache is a cache:
       dropping it must cost time, never correctness. */
    phy_cas_limits limits;
    memset(&limits, 0, sizeof limits);
    limits.max_bytes = 4096u;

    phy_cas *cas = phy_cas_create(ir, &limits);
    PHY_CHECK(cas != NULL);

    phy_ir_ref out = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_simplify(cas, parse(ir, "(+ (* 2 x) (* 3 x) (* 4 y))"), &out),
        PHY_OK);
    PHY_CHECK_EQ_STR(render(ir, out), "(+ (* 4 y) (* 5 x))");
    PHY_CHECK(phy_cas_bytes_used(cas) <= 4096u);
    PHY_CHECK_EQ_INT(phy_cas_validate(cas), PHY_OK);

    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
}

/*
 * Sweep an injected allocation failure across every allocation a workload
 * makes, and check after each that both layers still validate.
 *
 * This is the only way to reach the failure paths part-way through a
 * collection: squeezing a real budget always breaks at whichever allocation
 * happens to be first. What it proves here is that the scratch arena is
 * unwound on every error path -- phy_cas_validate() fails if a mark was left
 * taken.
 */
static void test_allocation_failure_unwinds_scratch(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);

    for (unsigned countdown = 1u; countdown <= 60u; countdown++) {
        phy_ir_context *ir = phy_ir_context_create(NULL);
        if (ir == NULL) {
            continue;
        }
        phy_cas *cas = phy_cas_create(ir, NULL);
        if (cas == NULL) {
            phy_ir_context_destroy(ir);
            continue;
        }

        phy_ir_ref expr = PHY_IR_NULL;
        size_t offset = 0u;
        if (phy_ir_read(ir, "(+ (* 2 x (^ y 2)) (* 3 x (^ y 2)) (fn sin x))",
                        &expr, &offset) == PHY_OK) {
            phy_host_fail_alloc_after(countdown);
            phy_ir_ref out = PHY_IR_NULL;
            (void)phy_cas_expand(cas, expr, &out);
            phy_host_fail_alloc_after(0u);

            PHY_CHECK_EQ_INT(phy_cas_validate(cas), PHY_OK);
            PHY_CHECK_EQ_INT(phy_ir_validate(ir), PHY_OK);
        }
        phy_cas_destroy(cas);
        phy_ir_context_destroy(ir);
    }

    phy_platform_shutdown();
}

/* ---------------------------------------------------------------- driver */

int main(void)
{
    PHY_TEST_CASE(test_lifecycle);
    PHY_TEST_CASE(test_all_memory_is_returned);
    PHY_TEST_CASE(test_exact_arithmetic);
    PHY_TEST_CASE(test_domain_errors);
    PHY_TEST_CASE(test_sums_collect);
    PHY_TEST_CASE(test_products_collect);
    PHY_TEST_CASE(test_power_rules);
    PHY_TEST_CASE(test_known_functions);
    PHY_TEST_CASE(test_simplify_is_idempotent);
    PHY_TEST_CASE(test_errors_propagate_as_values);
    PHY_TEST_CASE(test_noncommutative_kinds_are_left_alone);
    PHY_TEST_CASE(test_expand);
    PHY_TEST_CASE(test_substitute);
    PHY_TEST_CASE(test_differentiation);
    PHY_TEST_CASE(test_differentiation_defers_what_it_cannot_see);
    PHY_TEST_CASE(test_exact_symbolic_integration);
    PHY_TEST_CASE(test_supported_integrals_differentiate_back);
    PHY_TEST_CASE(test_zero_decision_basics);
    PHY_TEST_CASE(test_zero_decision_reads_assumptions);
    PHY_TEST_CASE(test_zero_decision_stays_honest);
    PHY_TEST_CASE(test_rational_form);
    PHY_TEST_CASE(test_reduce_cancels_known_factors);
    PHY_TEST_CASE(test_trigonometric_identities);
    PHY_TEST_CASE(test_gr_corpus_sphere_2d);
    PHY_TEST_CASE(test_flat_space_curvature_vanishes);
    PHY_TEST_CASE(test_step_budget);
    PHY_TEST_CASE(test_cancellation);
    PHY_TEST_CASE(test_term_limit);
    PHY_TEST_CASE(test_memoization_pays);
    PHY_TEST_CASE(test_cache_survives_a_tight_byte_ceiling);
    PHY_TEST_CASE(test_allocation_failure_unwinds_scratch);
    return PHY_TEST_REPORT("test_cas");
}
