#include <stdio.h>

#include "phy/mandelstam.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_lorentz_metric *metric;
    phy_ir_ref momentum[4];
    phy_ir_ref mass[4];
} fixture;

static fixture fixture_open(void)
{
    fixture f;
    f.ir = phy_ir_context_create(NULL);
    f.cas = f.ir != NULL ? phy_cas_create(f.ir, NULL) : NULL;
    f.metric = NULL;
    PHY_CHECK(f.ir != NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(phy_lorentz_metric_create(
                         f.cas, "g", "Lorentz", 4u,
                         PHY_LORENTZ_MOSTLY_MINUS, &f.metric),
                     PHY_OK);
    static const char *const momentum_name[4] = {"p1", "p2", "p3", "p4"};
    static const char *const mass_name[4] = {"m1", "m2", "m3", "m4"};
    for (unsigned index = 0u; index < 4u; ++index) {
        PHY_CHECK_EQ_INT(phy_lorentz_momentum(
                             f.metric, momentum_name[index],
                             &f.momentum[index]),
                         PHY_OK);
        f.mass[index] = phy_ir_symbol_ref(
            f.ir, phy_ir_intern(f.ir, mass_name[index]));
        PHY_CHECK(f.mass[index] != PHY_IR_NULL);
    }
    return f;
}

static void fixture_close(fixture *f)
{
    PHY_CHECK_EQ_INT(phy_cas_validate(f->cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(f->ir), PHY_OK);
    phy_lorentz_metric_destroy(f->metric);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static void expect_equal(fixture *f, phy_ir_ref left, phy_ir_ref right)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(f->cas, left, right, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static void check_routing(phy_mandelstam_routing routing)
{
    fixture f = fixture_open();
    phy_mandelstam *kinematics = NULL;
    PHY_CHECK_EQ_INT(phy_mandelstam_create(
                         f.metric, f.momentum, f.mass, routing, &kinematics),
                     PHY_OK);
    PHY_CHECK(kinematics != NULL);
    PHY_CHECK_EQ_INT(phy_mandelstam_routing_of(kinematics), routing);

    /* Each routed definition reduces to its named invariant. */
    for (unsigned invariant = 0u; invariant < 3u; ++invariant) {
        phy_ir_ref reduced = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(phy_mandelstam_reduce(
                             kinematics,
                             phy_mandelstam_definition(
                                 kinematics,
                                 (phy_mandelstam_invariant)invariant),
                             &reduced),
                         PHY_OK);
        expect_equal(
            &f, reduced,
            phy_mandelstam_symbol(
                kinematics, (phy_mandelstam_invariant)invariant));
    }

    /* The declared sum rule eliminates any chosen invariant exactly. */
    const phy_ir_ref s =
        phy_mandelstam_symbol(kinematics, PHY_MANDELSTAM_S);
    const phy_ir_ref t =
        phy_mandelstam_symbol(kinematics, PHY_MANDELSTAM_T);
    const phy_ir_ref u =
        phy_mandelstam_symbol(kinematics, PHY_MANDELSTAM_U);
    const phy_ir_ref sum_terms[4] = {
        s, t, u, PHY_IR_NULL};
    phy_ir_ref negative_mass_sum = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_neg(
                         f.cas, phy_mandelstam_sum_rule_rhs(kinematics),
                         &negative_mass_sum),
                     PHY_OK);
    phy_ir_ref residual_terms[4] = {
        sum_terms[0], sum_terms[1], sum_terms[2], negative_mass_sum};
    phy_ir_ref residual = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_add(f.cas, residual_terms, 4u, &residual), PHY_OK);
    for (unsigned invariant = 0u; invariant < 3u; ++invariant) {
        phy_ir_ref eliminated = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(phy_mandelstam_eliminate(
                             kinematics, residual,
                             (phy_mandelstam_invariant)invariant,
                             &eliminated),
                         PHY_OK);
        phy_cas_decision zero = PHY_CAS_UNKNOWN;
        PHY_CHECK_EQ_INT(
            phy_cas_is_zero(f.cas, eliminated, &zero), PHY_OK);
        PHY_CHECK_EQ_INT(zero, PHY_CAS_ZERO);
    }

    /*
     * The p1.p3 sign distinguishes the routings.  With all momenta incoming it
     * is (t-m1^2-m3^2)/2; for physical outgoing p3 it is the negative.
     */
    phy_ir_ref dot = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_dot(f.metric, f.momentum[0], f.momentum[2], &dot),
        PHY_OK);
    phy_ir_ref reduced_dot = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_mandelstam_reduce(kinematics, dot, &reduced_dot), PHY_OK);
    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref m1_squared = PHY_IR_NULL;
    phy_ir_ref m3_squared = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 2, 1, &two), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 1, 2, &half), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_pow(f.cas, f.mass[0], two, &m1_squared), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_pow(f.cas, f.mass[2], two, &m3_squared), PHY_OK);
    phy_ir_ref minus_m1 = PHY_IR_NULL;
    phy_ir_ref minus_m3 = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_neg(f.cas, m1_squared, &minus_m1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_neg(f.cas, m3_squared, &minus_m3), PHY_OK);
    const phy_ir_ref numerator_terms[3] = {t, minus_m1, minus_m3};
    phy_ir_ref numerator = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_add(f.cas, numerator_terms, 3u, &numerator), PHY_OK);
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_mul(
            f.cas, (const phy_ir_ref[2]){half, numerator}, 2u, &expected),
        PHY_OK);
    if (routing == PHY_MANDELSTAM_PESKIN_2_TO_2) {
        PHY_CHECK_EQ_INT(
            phy_cas_neg(f.cas, expected, &expected), PHY_OK);
    }
    expect_equal(&f, reduced_dot, expected);

    phy_mandelstam_destroy(kinematics);
    fixture_close(&f);
}

static void test_both_routings(void)
{
    check_routing(PHY_MANDELSTAM_PESKIN_2_TO_2);
    check_routing(PHY_MANDELSTAM_ALL_INCOMING);
}

static void test_rejections_are_typed(void)
{
    fixture f = fixture_open();
    phy_ir_ref duplicate[4] = {
        f.momentum[0], f.momentum[1], f.momentum[2], f.momentum[2]};
    phy_mandelstam *kinematics = NULL;
    PHY_CHECK_EQ_INT(phy_mandelstam_create(
                         f.metric, duplicate, f.mass,
                         PHY_MANDELSTAM_ALL_INCOMING, &kinematics),
                     PHY_ERR_ASSUMPTION);
    PHY_CHECK(kinematics == NULL);

    phy_ir_ref bad[4] = {
        f.momentum[0], f.momentum[1], f.momentum[2], f.mass[0]};
    PHY_CHECK_EQ_INT(phy_mandelstam_create(
                         f.metric, bad, f.mass,
                         PHY_MANDELSTAM_ALL_INCOMING, &kinematics),
                     PHY_ERR_TYPE);
    PHY_CHECK(kinematics == NULL);
    fixture_close(&f);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }
    PHY_TEST_CASE(test_both_routings);
    PHY_TEST_CASE(test_rejections_are_typed);
    const int result = PHY_TEST_REPORT("test_mandelstam");
    phy_platform_shutdown();
    return result;
}
