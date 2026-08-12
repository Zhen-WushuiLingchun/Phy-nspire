/*
 * Device link probe for the certified real/complex algebraic foundation.
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
    sink(phy_real_algebraic_root_index(left));
    size_t required = 0u;
    sink((unsigned)phy_real_algebraic_write_coefficient(
        left, 0u, 0, 0u, &required));
    sink((unsigned)required);
    sink((unsigned)phy_real_algebraic_write_lower(
        left, 0, 0u, &required));
    sink((unsigned)phy_real_algebraic_write_upper(
        left, 0, 0u, &required));
    sink(phy_real_algebraic_is_rational(left) ? 1u : 0u);
    bool equal = false;
    uint64_t hash = 0u;
    sink((unsigned)phy_real_algebraic_equal(left, right, &equal));
    sink(equal ? 1u : 0u);
    sink((unsigned)phy_real_algebraic_hash(left, &hash));
    sink((unsigned)hash);
    phy_real_algebraic *translated = 0;
    phy_real_algebraic *scaled = 0;
    phy_real_algebraic *reciprocal = 0;
    const phy_exact_rational_text half = {"1", "2"};
    sink((unsigned)phy_real_algebraic_translate_rational(
        left, half, &translated));
    sink((unsigned)phy_real_algebraic_scale_rational(
        left, half, &scaled));
    sink((unsigned)phy_real_algebraic_reciprocal(
        left, &reciprocal));
    phy_real_algebraic *sum = 0;
    phy_real_algebraic *difference = 0;
    phy_real_algebraic *product = 0;
    phy_real_algebraic *quotient = 0;
    phy_real_algebraic *power = 0;
    sink((unsigned)phy_real_algebraic_add(left, right, &sum));
    sink((unsigned)phy_real_algebraic_subtract(
        left, right, &difference));
    sink((unsigned)phy_real_algebraic_multiply(
        left, right, &product));
    sink((unsigned)phy_real_algebraic_divide(
        left, right, &quotient));
    sink((unsigned)phy_real_algebraic_pow_i32(left, 2, &power));
    phy_real_algebraic_destroy(power);
    phy_real_algebraic_destroy(quotient);
    phy_real_algebraic_destroy(product);
    phy_real_algebraic_destroy(difference);
    phy_real_algebraic_destroy(sum);
    phy_real_algebraic_destroy(reciprocal);
    phy_real_algebraic_destroy(scaled);
    phy_real_algebraic_destroy(translated);
    sink((unsigned)phy_real_algebraic_refine(left, 1u));
    int comparison = 0;
    sink((unsigned)phy_real_algebraic_compare(
        left, right, &comparison));
    sink((unsigned)(comparison + 1));

    static const char *complex_polynomial[] = {"1", "0", "1"};
    phy_complex_algebraic *complex_roots[2] = {0, 0};
    size_t complex_count = 0u;
    sink((unsigned)phy_algebraic_isolate_complex_roots(
        context, complex_polynomial, 3u, complex_roots, 2u,
        &complex_count));
    sink((unsigned)complex_count);
    phy_complex_algebraic *selected = 0;
    sink((unsigned)phy_complex_algebraic_create_by_index(
        context, complex_polynomial, 3u, 2u, &selected));
    if (selected != 0) {
        sink((unsigned)phy_complex_algebraic_validate(selected));
        sink((unsigned)phy_complex_algebraic_degree(selected));
        sink(phy_complex_algebraic_root_index(selected));
        sink(phy_complex_algebraic_is_real(selected) ? 1u : 0u);
        sink(phy_complex_algebraic_is_rational(selected) ? 1u : 0u);
        sink((unsigned)phy_complex_algebraic_write_coefficient(
            selected, 0u, 0, 0u, &required));
        sink((unsigned)phy_complex_algebraic_write_real_lower(
            selected, 0, 0u, &required));
        sink((unsigned)phy_complex_algebraic_write_real_upper(
            selected, 0, 0u, &required));
        sink((unsigned)phy_complex_algebraic_write_imaginary_lower(
            selected, 0, 0u, &required));
        sink((unsigned)phy_complex_algebraic_write_imaginary_upper(
            selected, 0, 0u, &required));
        phy_complex_algebraic *lifted = 0;
        phy_complex_algebraic *conjugate = 0;
        phy_complex_algebraic *complex_sum = 0;
        phy_complex_algebraic *complex_difference = 0;
        phy_complex_algebraic *complex_product = 0;
        phy_complex_algebraic *complex_quotient = 0;
        phy_complex_algebraic *complex_power = 0;
        sink((unsigned)phy_complex_algebraic_from_real(
            left, &lifted));
        sink((unsigned)phy_complex_algebraic_conjugate(
            selected, &conjugate));
        sink((unsigned)phy_complex_algebraic_equal(
            selected, selected, &equal));
        sink((unsigned)phy_complex_algebraic_hash(selected, &hash));
        sink((unsigned)phy_complex_algebraic_add(
            selected, selected, &complex_sum));
        sink((unsigned)phy_complex_algebraic_subtract(
            selected, selected, &complex_difference));
        sink((unsigned)phy_complex_algebraic_multiply(
            selected, selected, &complex_product));
        sink((unsigned)phy_complex_algebraic_divide(
            selected, selected, &complex_quotient));
        sink((unsigned)phy_complex_algebraic_pow_i32(
            selected, 2, &complex_power));
        phy_complex_algebraic_destroy(complex_power);
        phy_complex_algebraic_destroy(complex_quotient);
        phy_complex_algebraic_destroy(complex_product);
        phy_complex_algebraic_destroy(complex_difference);
        phy_complex_algebraic_destroy(complex_sum);
        phy_complex_algebraic_destroy(conjugate);
        phy_complex_algebraic_destroy(lifted);
    }
    phy_complex_algebraic_destroy(selected);
    for (size_t index = 0u; index < complex_count; ++index) {
        phy_complex_algebraic_destroy(complex_roots[index]);
    }
    phy_real_algebraic_destroy(right);
    phy_real_algebraic_destroy(left);
    phy_algebraic_context_destroy(context);
    return g_phy_algebraic_probe_sink != 0u ? 0 : 3;
}
