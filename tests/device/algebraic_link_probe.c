/*
 * Device link probe for the certified real-algebraic foundation.
 *
 * The notebook does not expose Root objects yet, so --gc-sections can discard
 * this entire layer from the product. Referencing every public entry point
 * here proves that bigint/rational/Sturm code really links and packages under
 * the Ndless ARM ABI without retaining floating-point helpers.
 */
#include "phy/algebraic.h"

volatile unsigned g_phy_algebraic_probe_sink;

static void sink(unsigned value)
{
    g_phy_algebraic_probe_sink += value;
}

static bool never_cancel(void *user)
{
    (void)user;
    return false;
}

int main(void)
{
    phy_algebraic_limits limits;
    phy_algebraic_limits_defaults(&limits);
    sink(limits.max_degree);

    phy_algebraic_context *context =
        phy_algebraic_context_create(&limits);
    if (context == 0) {
        return 1;
    }
    phy_algebraic_set_cancel(context, never_cancel, 0);
    phy_algebraic_set_cancel(context, 0, 0);
    sink(phy_algebraic_steps(context));
    sink((unsigned)phy_algebraic_total_steps(context));
    sink((unsigned)phy_algebraic_metadata_bytes(context));
    sink((unsigned)phy_algebraic_validate(context));

    static const char *polynomial[] = {"-2", "0", "1"};
    const phy_exact_rational_text lower = {"1", "1"};
    const phy_exact_rational_text upper = {"2", "1"};
    uint32_t roots = 0u;
    sink((unsigned)phy_algebraic_count_real_roots(
        context, polynomial, 3u, lower, upper, &roots));
    sink(roots);
    phy_real_algebraic *isolated[2] = {0, 0};
    size_t isolated_count = 0u;
    sink((unsigned)phy_algebraic_isolate_real_roots(
        context, polynomial, 3u, isolated, 2u, &isolated_count));
    sink((unsigned)isolated_count);
    for (size_t index = 0u; index < isolated_count; ++index) {
        phy_real_algebraic_destroy(isolated[index]);
    }

    phy_real_algebraic *left = 0;
    phy_real_algebraic *right = 0;
    sink((unsigned)phy_real_algebraic_create(
        context, polynomial, 3u, lower, upper, &left));
    sink((unsigned)phy_real_algebraic_create(
        context, polynomial, 3u, lower, upper, &right));
    if (left == 0 || right == 0) {
        phy_algebraic_context_destroy(context);
        return 2;
    }

    sink((unsigned)phy_real_algebraic_validate(left));
    sink((unsigned)phy_real_algebraic_degree(left));
    size_t required = 0u;
    sink((unsigned)phy_real_algebraic_write_coefficient(
        left, 0u, 0, 0u, &required));
    sink((unsigned)required);
    sink((unsigned)phy_real_algebraic_write_lower(
        left, 0, 0u, &required));
    sink((unsigned)phy_real_algebraic_write_upper(
        left, 0, 0u, &required));
    sink(phy_real_algebraic_is_rational(left) ? 1u : 0u);
    sink((unsigned)phy_real_algebraic_refine(left, 1u));
    int comparison = 0;
    sink((unsigned)phy_real_algebraic_compare(
        left, right, &comparison));
    sink((unsigned)(comparison + 1));

    phy_real_algebraic_destroy(right);
    phy_real_algebraic_destroy(left);
    phy_algebraic_context_destroy(context);
    return g_phy_algebraic_probe_sink != 0u ? 0 : 3;
}
