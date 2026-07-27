/*
 * Device link probe for the scalar CAS. Not part of the application.
 *
 * The same gap tests/device/ir_link_probe.c exists to close, one layer up: the
 * notebook does not call the CAS yet, --gc-sections therefore discards every
 * phy_cas_* symbol, and the ordinary ARM build proves only that this layer
 * *compiles*. It never links it.
 *
 * The risk is concrete and specific here. This layer does 64-bit integer
 * arithmetic, including division and modulo in the gcd, which on a 32-bit ARM
 * target become calls to libgcc helpers -- __aeabi_ldivmod and friends. Whether
 * those resolve under Ndless's newlib and ldscript is a link-time question that
 * compiling cannot answer. It also must not acquire libm or a floating-point
 * formatter: exact integer/rational arithmetic is the whole design, while
 * inexact real atoms are carried symbolically rather than evaluated here.
 *
 * Referencing every public entry point forces the linker to keep and resolve
 * all of them. tools/link-check.sh cas does the rest.
 *
 * Kept out of the Makefile's SOURCES on purpose: `make` must not link this, or
 * the size report would measure the probe instead of the product.
 */
#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/platform.h"

/* A volatile sink, so the optimizer cannot decide a call is dead and delete the
   reference the probe exists to make. */
volatile unsigned g_phy_cas_probe_sink;

static void sink(unsigned value)
{
    g_phy_cas_probe_sink += value;
}

static bool never_cancel(void *user)
{
    (void)user;
    return false;
}

static void probe_lifetime(phy_cas *cas, phy_ir_context *ir)
{
    phy_cas_limits limits;
    phy_cas_limits_defaults(&limits);
    sink((unsigned)limits.max_steps);

    sink(phy_cas_ir(cas) == ir ? 1u : 0u);
    phy_cas_set_cancel(cas, never_cancel, 0);
    phy_cas_set_cancel(cas, 0, 0);
    sink((unsigned)phy_cas_steps(cas));
    sink((unsigned)phy_cas_total_steps(cas));
    sink((unsigned)phy_cas_bytes_used(cas));
    phy_cas_cache_clear(cas);
    sink((unsigned)phy_cas_validate(cas));
}

static void probe_arithmetic(phy_cas *cas, phy_ir_context *ir)
{
    phy_ir_ref out = PHY_IR_NULL;
    const phy_ir_ref x = phy_ir_symbol_ref(ir, phy_ir_intern(ir, "x"));
    const phy_ir_ref two = phy_ir_integer(ir, 2);

    /* A rational, so the gcd reduction and its 64-bit division are linked. */
    sink((unsigned)phy_cas_number(cas, 6, 4, &out));
    sink((unsigned)out);

    const phy_ir_ref pair[2] = {x, two};
    sink((unsigned)phy_cas_add(cas, pair, 2u, &out));
    sink((unsigned)phy_cas_mul(cas, pair, 2u, &out));
    sink((unsigned)phy_cas_neg(cas, x, &out));
    sink((unsigned)phy_cas_sub(cas, x, two, &out));
    sink((unsigned)phy_cas_div(cas, x, two, &out));
    sink((unsigned)phy_cas_pow(cas, x, two, &out));
}

static void probe_rewrites(phy_cas *cas, phy_ir_context *ir)
{
    phy_ir_ref out = PHY_IR_NULL;
    phy_ir_ref expr = PHY_IR_NULL;
    size_t offset = 0u;

    /* Built by the parser rather than by hand: the shapes below exercise
       collection, the trigonometric reduction, and a derivative in one go. */
    sink((unsigned)phy_ir_read(
        ir, "(+ (* 2 x) (* 3 x) (fn tan theta) (fn sin (* 2 theta)))", &expr,
        &offset));

    sink((unsigned)phy_cas_simplify(cas, expr, &out));
    sink((unsigned)phy_cas_expand(cas, expr, &out));
    sink((unsigned)phy_cas_diff(cas, expr,
                                phy_ir_symbol_ref(ir, phy_ir_intern(ir, "x")),
                                &out));
    sink((unsigned)phy_cas_integrate(
        cas, expr, phy_ir_symbol_ref(ir, phy_ir_intern(ir, "x")), &out));

    const phy_cas_rule rule = {phy_ir_symbol_ref(ir, phy_ir_intern(ir, "x")),
                               phy_ir_integer(ir, 3)};
    sink((unsigned)phy_cas_substitute(cas, expr, &rule, 1u, &out));
}

static void probe_decisions(phy_cas *cas, phy_ir_context *ir)
{
    phy_ir_ref numerator = PHY_IR_NULL;
    phy_ir_ref denominator = PHY_IR_NULL;
    phy_ir_ref expr = PHY_IR_NULL;
    size_t offset = 0u;
    phy_cas_decision decision = PHY_CAS_UNKNOWN;

    sink((unsigned)phy_ir_read(
        ir, "(+ (^ (fn sin u) 2) (^ (fn cos u) 2) -1)", &expr, &offset));

    sink((unsigned)phy_cas_rational_form(cas, expr, &numerator, &denominator));
    sink((unsigned)numerator);
    sink((unsigned)denominator);
    sink((unsigned)phy_cas_is_zero(cas, expr, &decision));
    sink((unsigned)decision);
    sink((unsigned)phy_cas_equivalent(cas, numerator, denominator, &decision));
}

int main(void)
{
    phy_ir_context *ir = phy_ir_context_create(0);
    if (ir == 0) {
        return 1;
    }
    phy_cas *cas = phy_cas_create(ir, 0);
    if (cas == 0) {
        phy_ir_context_destroy(ir);
        return 2;
    }

    probe_lifetime(cas, ir);
    probe_arithmetic(cas, ir);
    probe_rewrites(cas, ir);
    probe_decisions(cas, ir);

    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
    return (g_phy_cas_probe_sink != 0u) ? 0 : 3;
}
