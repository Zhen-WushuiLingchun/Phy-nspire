/*
 * Device link probe for the expression IR. Not part of the application.
 *
 * The shipped binary does not call the IR yet, and the device build uses
 * --gc-sections, so every phy_ir_* symbol is discarded before it reaches
 * dist/phy-nspire.tns. That is the right outcome for the shipped artifact --
 * it must not carry code nothing runs -- but it means the ordinary ARM build
 * proves only that the IR *compiles*. It never links it, so a call to a libc
 * function Ndless newlib does not provide, or a relocation the ldscript
 * cannot satisfy, would go unnoticed until Phase 1 wired the notebook up.
 *
 * This translation unit closes that gap by referencing every public entry
 * point, so the linker must keep and resolve all of them.
 * tools/ir-link-check.sh builds it, verifies the expected symbols survived,
 * verifies no floating-point formatter was dragged in, and packages the
 * result to a real .tns.
 *
 * Kept out of the Makefile's SOURCES on purpose: `make` must not link this,
 * or the size report would measure the probe instead of the product.
 */
#include "phy/ir.h"
#include "phy/platform.h"

/*
 * Results are funnelled through a volatile sink so the optimizer cannot
 * decide a call is dead and delete the reference the probe exists to make.
 */
volatile unsigned g_phy_ir_probe_sink;

static void sink(unsigned value)
{
    g_phy_ir_probe_sink += value;
}

static void probe_context_and_symbols(phy_ir_context *ctx)
{
    phy_ir_limits limits;
    phy_ir_limits_defaults(&limits);
    sink((unsigned)limits.max_nodes);

    const phy_ir_symbol g = phy_ir_intern(ctx, "g");
    const phy_ir_symbol mu = phy_ir_intern_n(ctx, "mu", 2u);
    sink(phy_ir_symbol_name(ctx, g) != 0 ? 1u : 0u);
    sink((unsigned)phy_ir_assume(ctx, g, PHY_IR_ASSUME_REAL));
    sink(phy_ir_assumptions(ctx, g));
    sink((unsigned)phy_ir_declare_symmetry(ctx, g, 0u, 1u,
                                           PHY_IR_SYMMETRY_SYMMETRIC));
    sink((unsigned)phy_ir_slot_symmetry(ctx, g, 0u, 1u));
    sink((unsigned)phy_ir_node_count(ctx));
    sink((unsigned)phy_ir_symbol_count(ctx));
    sink((unsigned)phy_ir_bytes_used(ctx));
    sink((unsigned)phy_ir_last_error(ctx));
    phy_ir_clear_error(ctx);
    sink((unsigned)mu);
}

static void probe_builders(phy_ir_context *ctx)
{
    const phy_ir_symbol head = phy_ir_intern(ctx, "f");
    const phy_ir_symbol name = phy_ir_intern(ctx, "nu");

    const phy_ir_ref two = phy_ir_integer(ctx, 2);
    const phy_ir_ref half = phy_ir_rational(ctx, 1, 2);
    const phy_ir_ref real = phy_ir_real(ctx, 0.5);
    const phy_ir_ref symbol = phy_ir_symbol_ref(ctx, head);
    const phy_ir_ref index = phy_ir_index(ctx, name, PHY_IR_INDEX_LOWER);
    const phy_ir_ref failure = phy_ir_error(ctx, PHY_ERR_TIMEOUT);

    const phy_ir_ref pair[2] = {two, symbol};
    const phy_ir_ref sum = phy_ir_add(ctx, pair, 2u);
    const phy_ir_ref product = phy_ir_mul(ctx, pair, 2u);
    sink((unsigned)phy_ir_ncmul(ctx, pair, 2u));
    sink((unsigned)phy_ir_wedge(ctx, pair, 2u));
    sink((unsigned)phy_ir_pow(ctx, symbol, two));
    sink((unsigned)phy_ir_equation(ctx, symbol, half));
    sink((unsigned)phy_ir_function(ctx, head, pair, 2u));
    sink((unsigned)phy_ir_tensor(ctx, head, &index, 1u));
    sink((unsigned)phy_ir_operator(ctx, head, &index, 1u));
    sink((unsigned)phy_ir_derivative(ctx, symbol, &symbol, 1u));

    sink((unsigned)phy_ir_kind_of(ctx, sum));
    sink((unsigned)phy_ir_kind_flags(PHY_IR_MUL));
    sink(phy_ir_kind_name(PHY_IR_ADD) != 0 ? 1u : 0u);
    sink((unsigned)phy_ir_child_count(ctx, product));
    sink((unsigned)phy_ir_child(ctx, product, 0u));
    sink((unsigned)phy_ir_head(ctx, symbol));
    sink((unsigned)phy_ir_hash(ctx, sum));
    sink((unsigned)phy_ir_depth(ctx, sum));
    sink((unsigned)phy_ir_compare(ctx, sum, product));
    sink(phy_ir_equal(sum, product) ? 1u : 0u);

    int64_t numerator = 0;
    int64_t denominator = 0;
    double value = 0.0;
    phy_ir_variance variance = PHY_IR_INDEX_LOWER;
    phy_status status = PHY_OK;
    sink(phy_ir_integer_value(ctx, two, &numerator) ? 1u : 0u);
    sink(phy_ir_rational_value(ctx, half, &numerator, &denominator) ? 1u : 0u);
    sink(phy_ir_real_value(ctx, real, &value) ? 1u : 0u);
    sink(phy_ir_index_variance(ctx, index, &variance) ? 1u : 0u);
    sink(phy_ir_error_status(ctx, failure, &status) ? 1u : 0u);
}

static void probe_serialization(phy_ir_context *ctx)
{
    char text[192];
    size_t length = 0u;
    const phy_ir_ref x = phy_ir_symbol_ref(ctx, phy_ir_intern(ctx, "x"));

    sink((unsigned)phy_ir_write(ctx, x, text, sizeof text, &length));
    sink((unsigned)length);
    sink((unsigned)phy_ir_write_declarations(ctx, text, sizeof text, &length));

    phy_ir_ref parsed = PHY_IR_NULL;
    size_t offset = 0u;
    sink((unsigned)phy_ir_read(ctx, "(+ 1 (* 2 x))", &parsed, &offset));
    sink((unsigned)parsed);
    sink((unsigned)phy_ir_read_declarations(ctx, "(declare x real)", &offset));
    sink((unsigned)offset);
}

int main(void)
{
    phy_ir_context *ctx = phy_ir_context_create(0);
    if (ctx == 0) {
        return 1;
    }

    probe_context_and_symbols(ctx);
    probe_builders(ctx);
    probe_serialization(ctx);
    sink((unsigned)phy_ir_validate(ctx));

    phy_ir_context_destroy(ctx);
    return (g_phy_ir_probe_sink != 0u) ? 0 : 2;
}
