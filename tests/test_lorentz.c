/*
 * Acceptance for contract Q-1 of docs/agent-tasks/QFT_DIRAC.md: Lorentz index
 * and scalar-product objects over a signature-bearing metric.
 *
 * The cross-metric checks are the ones worth keeping honest. Phy-nspire hosts
 * the particle-physics signature and the general-relativity one in the same
 * notebook, and reference 4.1 calls mixing them an integration risk to be
 * closed early rather than debugged later.
 */
#include <stdio.h>

#include "phy/lorentz.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_lorentz_metric *qft; /* (+,-,-,-), bundle "Lorentz" */
    phy_lorentz_metric *gr;  /* (-,+,+,+), bundle "LorentzGR" */
} fixture;

static fixture fixture_open(void)
{
    fixture f;
    f.ir = phy_ir_context_create(NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    f.qft = NULL;
    f.gr = NULL;
    PHY_CHECK(f.ir != NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(phy_lorentz_metric_create(f.cas, "g", "Lorentz", 4u,
                                               PHY_LORENTZ_MOSTLY_MINUS,
                                               &f.qft),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_metric_create(f.cas, "g", "LorentzGR", 4u,
                                               PHY_LORENTZ_MOSTLY_PLUS, &f.gr),
                     PHY_OK);
    return f;
}

static void fixture_close(fixture *f)
{
    phy_lorentz_metric_destroy(f->gr);
    phy_lorentz_metric_destroy(f->qft);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static phy_ir_ref index_of(const phy_lorentz_metric *metric, const char *name,
                           phy_ir_variance variance)
{
    phy_ir_ref index = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_index(metric, name, variance, &index), PHY_OK);
    return index;
}

static phy_ir_ref momentum_of(phy_lorentz_metric *metric, const char *name)
{
    phy_ir_ref momentum = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_momentum(metric, name, &momentum), PHY_OK);
    return momentum;
}

static phy_ir_ref symbol_of(phy_ir_context *ir, const char *name)
{
    return phy_ir_symbol_ref(ir, phy_ir_intern(ir, name));
}

static void expect_zero(phy_cas *cas, phy_ir_ref left, phy_ir_ref right)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_cas_equivalent(cas, left, right, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

/* ------------------------------------------------------------ the metric */

static void test_metric_identity(void)
{
    fixture f = fixture_open();
    PHY_CHECK_EQ_STR(phy_lorentz_metric_name(f.qft), "g");
    PHY_CHECK_EQ_INT(phy_lorentz_metric_dimension(f.qft), 4);
    PHY_CHECK_EQ_INT(phy_lorentz_metric_signature(f.qft),
                     PHY_LORENTZ_MOSTLY_MINUS);
    PHY_CHECK_EQ_INT(phy_lorentz_metric_signature(f.gr),
                     PHY_LORENTZ_MOSTLY_PLUS);
    PHY_CHECK(phy_lorentz_metric_cas(f.qft) == f.cas);

    /* Same name, different bundle: the two metrics are genuinely distinct. */
    PHY_CHECK(phy_lorentz_metric_space(f.qft) !=
              phy_lorentz_metric_space(f.gr));

    phy_lorentz_metric *bad = NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_metric_create(f.cas, "g", "L", 0u,
                                               PHY_LORENTZ_MOSTLY_MINUS, &bad),
                     PHY_ERR_INVALID_ARGUMENT);
    fixture_close(&f);
}

static void test_index_bundles_do_not_mix(void)
{
    fixture f = fixture_open();
    const phy_ir_ref mu = index_of(f.qft, "mu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref nu = index_of(f.qft, "nu", PHY_IR_INDEX_LOWER);
    const phy_ir_ref mu_gr = index_of(f.gr, "mu", PHY_IR_INDEX_UPPER);

    PHY_CHECK(phy_lorentz_owns_index(f.qft, mu));
    PHY_CHECK(phy_lorentz_owns_index(f.qft, nu));
    PHY_CHECK(!phy_lorentz_owns_index(f.qft, mu_gr));
    PHY_CHECK(!phy_lorentz_owns_index(f.gr, mu));

    /* The same spelling in two bundles is two nodes, not one. */
    PHY_CHECK(mu != mu_gr);

    /* Acceptance 5: contracting across signatures is a typed error, not a
     * silently wrong answer. */
    phy_ir_ref tensor = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_metric_tensor(f.qft, mu, mu_gr, &tensor),
                     PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(phy_lorentz_metric_tensor(f.gr, mu_gr, nu, &tensor),
                     PHY_ERR_TYPE);
    fixture_close(&f);
}

static void test_metric_tensor_is_symmetric(void)
{
    fixture f = fixture_open();
    const phy_ir_ref mu = index_of(f.qft, "mu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref nu = index_of(f.qft, "nu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref nu_lower = index_of(f.qft, "nu", PHY_IR_INDEX_LOWER);
    const phy_ir_ref mu_lower = index_of(f.qft, "mu", PHY_IR_INDEX_LOWER);

    phy_ir_ref ab = PHY_IR_NULL;
    phy_ir_ref ba = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_metric_tensor(f.qft, mu, nu, &ab), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_metric_tensor(f.qft, nu, mu, &ba), PHY_OK);
    PHY_CHECK(phy_ir_equal(ab, ba));

    /* Mixed variance is the Kronecker delta and is symmetric too. */
    phy_ir_ref mixed = PHY_IR_NULL;
    phy_ir_ref mixed_swapped = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_metric_tensor(f.qft, mu, nu_lower, &mixed),
                     PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_lorentz_metric_tensor(f.qft, nu_lower, mu, &mixed_swapped), PHY_OK);
    PHY_CHECK(phy_ir_equal(mixed, mixed_swapped));

    /* g^mu_mu is the dimension, exactly. */
    phy_ir_ref trace = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_metric_tensor(f.qft, mu, mu_lower, &trace),
                     PHY_OK);
    int64_t value = 0;
    PHY_CHECK(phy_ir_integer_value(f.ir, trace, &value));
    PHY_CHECK_EQ_INT(value, 4);

    /* g^{mu mu} has lost an index and is refused. */
    phy_ir_ref lost = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_metric_tensor(f.qft, mu, mu, &lost),
                     PHY_ERR_TYPE);
    fixture_close(&f);
}

/* -------------------------------------------------- free and dummy indices */

static void test_free_and_dummy_classification(void)
{
    fixture f = fixture_open();
    const phy_ir_ref mu_up = index_of(f.qft, "mu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref mu_dn = index_of(f.qft, "mu", PHY_IR_INDEX_LOWER);
    const phy_ir_ref nu_up = index_of(f.qft, "nu", PHY_IR_INDEX_UPPER);
    const phy_ir_symbol head = phy_ir_intern(f.ir, "T");

    /* T[mu^, mu_, nu^]: mu is a contracted dummy, nu is free. */
    const phy_ir_ref slots[3] = {mu_up, mu_dn, nu_up};
    const phy_ir_ref tensor = phy_ir_tensor(f.ir, head, slots, 3u);
    PHY_CHECK(tensor != PHY_IR_NULL);

    phy_lorentz_index_use uses[4];
    size_t count = 0u;
    PHY_CHECK_EQ_INT(
        phy_lorentz_index_census(f.qft, tensor, uses, 4u, &count), PHY_OK);
    PHY_CHECK_EQ_INT(count, 2);
    for (size_t i = 0u; i < count; i++) {
        if (uses[i].name == phy_ir_intern(f.ir, "mu")) {
            PHY_CHECK(phy_lorentz_index_is_dummy(&uses[i]));
            PHY_CHECK(!phy_lorentz_index_is_free(&uses[i]));
        } else {
            PHY_CHECK(phy_lorentz_index_is_free(&uses[i]));
            PHY_CHECK(!phy_lorentz_index_is_dummy(&uses[i]));
        }
        PHY_CHECK(!phy_lorentz_index_is_malformed(&uses[i]));
    }

    /* T[mu^, mu^] is neither free nor dummy: two upper occurrences of one
     * name is a lost index, and the classifier says so. */
    const phy_ir_ref twice[2] = {mu_up, mu_up};
    const phy_ir_ref broken = phy_ir_tensor(f.ir, head, twice, 2u);
    PHY_CHECK_EQ_INT(
        phy_lorentz_index_census(f.qft, broken, uses, 4u, &count), PHY_OK);
    PHY_CHECK_EQ_INT(count, 1);
    PHY_CHECK(phy_lorentz_index_is_malformed(&uses[0]));

    /* An index of the other bundle is another metric's business. */
    const phy_ir_ref gr_index = index_of(f.gr, "alpha", PHY_IR_INDEX_UPPER);
    const phy_ir_ref foreign[1] = {gr_index};
    const phy_ir_ref elsewhere = phy_ir_tensor(f.ir, head, foreign, 1u);
    PHY_CHECK_EQ_INT(
        phy_lorentz_index_census(f.qft, elsewhere, uses, 4u, &count), PHY_OK);
    PHY_CHECK_EQ_INT(count, 0);
    fixture_close(&f);
}

static void test_rename_index_round_trip(void)
{
    fixture f = fixture_open();
    const phy_ir_ref mu_up = index_of(f.qft, "mu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref mu_dn = index_of(f.qft, "mu", PHY_IR_INDEX_LOWER);
    const phy_ir_ref rho = index_of(f.qft, "rho", PHY_IR_INDEX_UPPER);
    const phy_ir_symbol head = phy_ir_intern(f.ir, "T");
    const phy_ir_ref slots[3] = {mu_up, rho, mu_dn};
    const phy_ir_ref original = phy_ir_tensor(f.ir, head, slots, 3u);

    phy_ir_ref renamed = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_rename_index(f.qft, original, "mu", "sigma", &renamed),
        PHY_OK);
    PHY_CHECK(!phy_ir_equal(renamed, original));

    /* Both positions moved together, so the dummy is still a dummy. */
    phy_lorentz_index_use uses[4];
    size_t count = 0u;
    PHY_CHECK_EQ_INT(
        phy_lorentz_index_census(f.qft, renamed, uses, 4u, &count), PHY_OK);
    PHY_CHECK_EQ_INT(count, 2);

    phy_ir_ref back = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_rename_index(f.qft, renamed, "sigma", "mu", &back), PHY_OK);
    PHY_CHECK(phy_ir_equal(back, original));
    fixture_close(&f);
}

/* -------------------------------------------------- momenta and products */

static void test_momentum_declaration(void)
{
    fixture f = fixture_open();
    const phy_ir_ref p1 = momentum_of(f.qft, "p1");
    const phy_ir_ref again = momentum_of(f.qft, "p1");
    PHY_CHECK(phy_ir_equal(p1, again));
    PHY_CHECK_EQ_INT(phy_lorentz_momentum_count(f.qft), 1);
    PHY_CHECK(phy_lorentz_owns_momentum(f.qft, p1));
    PHY_CHECK(!phy_lorentz_owns_momentum(f.gr, p1));
    PHY_CHECK(phy_ir_equal(phy_lorentz_momentum_at(f.qft, 0u), p1));

    char text[128];
    size_t required = 0u;
    PHY_CHECK_EQ_INT(phy_ir_write(f.ir, p1, text, sizeof text, &required),
                     PHY_OK);
    PHY_CHECK_EQ_STR(text, "(op Momentum p1 Lorentz)");

    /* The bounded table reports a fixed capacity rather than growing. */
    static const char *const names[PHY_LORENTZ_MAX_MOMENTA] = {
        "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8"};
    phy_ir_ref extra = PHY_IR_NULL;
    phy_status status = PHY_OK;
    for (unsigned i = 0u; i < PHY_LORENTZ_MAX_MOMENTA && status == PHY_OK;
         i++) {
        status = phy_lorentz_momentum(f.qft, names[i], &extra);
    }
    PHY_CHECK_EQ_INT(status, PHY_ERR_UNSUPPORTED);
    PHY_CHECK_EQ_INT(phy_lorentz_momentum_count(f.qft),
                     PHY_LORENTZ_MAX_MOMENTA);
    fixture_close(&f);
}

static void test_scalar_product_is_symmetric(void)
{
    fixture f = fixture_open();
    const phy_ir_ref p = momentum_of(f.qft, "p");
    const phy_ir_ref q = momentum_of(f.qft, "q");

    phy_ir_ref pq = PHY_IR_NULL;
    phy_ir_ref qp = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.qft, p, q, &pq), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.qft, q, p, &qp), PHY_OK);

    /* Acceptance 3: structurally equal, not equal-after-simplification. */
    PHY_CHECK(phy_ir_equal(pq, qp));

    /* A momentum of the GR metric has no product with a QFT one. */
    const phy_ir_ref gr_momentum = momentum_of(f.gr, "k");
    phy_ir_ref mixed = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.qft, p, gr_momentum, &mixed),
                     PHY_ERR_TYPE);

    const phy_ir_ref mu = index_of(f.qft, "mu", PHY_IR_INDEX_UPPER);
    phy_ir_ref component = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_momentum_component(f.qft, p, mu, &component), PHY_OK);
    char text[128];
    size_t required = 0u;
    PHY_CHECK_EQ_INT(phy_ir_write(f.ir, component, text, sizeof text,
                                  &required),
                     PHY_OK);
    PHY_CHECK_EQ_STR(text, "(tensor p (idx mu up Lorentz))");

    /* A GR index on a QFT momentum is refused. */
    const phy_ir_ref gr_index = index_of(f.gr, "mu", PHY_IR_INDEX_UPPER);
    PHY_CHECK_EQ_INT(
        phy_lorentz_momentum_component(f.qft, p, gr_index, &component),
        PHY_ERR_TYPE);
    fixture_close(&f);
}

static void test_vector_algebra(void)
{
    fixture f = fixture_open();
    const phy_ir_ref p1 = momentum_of(f.qft, "p1");
    const phy_ir_ref p2 = momentum_of(f.qft, "p2");

    phy_lorentz_vector v1, v2, sum, difference;
    PHY_CHECK_EQ_INT(phy_lorentz_vector_of(f.qft, p1, &v1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_vector_of(f.qft, p2, &v2), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_vector_add(&v1, &v2, &sum), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_vector_sub(&v1, &v2, &difference), PHY_OK);

    phy_ir_ref square = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_vector_dot(&sum, &sum, &square), PHY_OK);

    phy_ir_ref d11 = PHY_IR_NULL;
    phy_ir_ref d12 = PHY_IR_NULL;
    phy_ir_ref d22 = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.qft, p1, p1, &d11), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.qft, p1, p2, &d12), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.qft, p2, p2, &d22), PHY_OK);

    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref cross = PHY_IR_NULL;
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 2, 1, &two), PHY_OK);
    const phy_ir_ref pair[2] = {two, d12};
    PHY_CHECK_EQ_INT(phy_cas_mul(f.cas, pair, 2u, &cross), PHY_OK);
    const phy_ir_ref terms[3] = {d11, cross, d22};
    PHY_CHECK_EQ_INT(phy_cas_add(f.cas, terms, 3u, &expected), PHY_OK);
    expect_zero(f.cas, square, expected);

    /* (p1-p2).(p1-p2) differs from (p1+p2).(p1+p2) by the cross term only. */
    phy_ir_ref difference_square = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_vector_dot(&difference, &difference, &difference_square),
        PHY_OK);
    phy_ir_ref gap = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_sub(f.cas, square, difference_square, &gap),
                     PHY_OK);
    phy_ir_ref four_dot = PHY_IR_NULL;
    phy_ir_ref four = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 4, 1, &four), PHY_OK);
    const phy_ir_ref quad[2] = {four, d12};
    PHY_CHECK_EQ_INT(phy_cas_mul(f.cas, quad, 2u, &four_dot), PHY_OK);
    expect_zero(f.cas, gap, four_dot);
    fixture_close(&f);
}

/* ------------------------------------------------------- kinematic rules */

typedef struct {
    fixture f;
    phy_lorentz_vector v[4];
    phy_ir_ref mass[4];
} kinematics;

static kinematics kinematics_open(void)
{
    kinematics k;
    k.f = fixture_open();
    static const char *const names[4] = {"p1", "p2", "p3", "p4"};
    static const char *const masses[4] = {"m1", "m2", "m3", "m4"};
    for (unsigned i = 0u; i < 4u; i++) {
        const phy_ir_ref momentum = momentum_of(k.f.qft, names[i]);
        PHY_CHECK_EQ_INT(phy_lorentz_vector_of(k.f.qft, momentum, &k.v[i]),
                         PHY_OK);
        k.mass[i] = symbol_of(k.f.ir, masses[i]);
        PHY_CHECK_EQ_INT(
            phy_lorentz_declare_on_shell(k.f.qft, momentum, k.mass[i]), PHY_OK);
    }
    return k;
}

static void test_on_shell_substitution(void)
{
    kinematics k = kinematics_open();
    phy_ir_ref square = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_vector_dot(&k.v[0], &k.v[0], &square), PHY_OK);

    phy_ir_ref reduced = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_reduce(k.f.qft, square, &reduced), PHY_OK);

    phy_ir_ref two = PHY_IR_NULL;
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(k.f.cas, 2, 1, &two), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_pow(k.f.cas, k.mass[0], two, &expected), PHY_OK);
    expect_zero(k.f.cas, reduced, expected);
    fixture_close(&k.f);
}

/*
 * Acceptance 4. Declaring p1 + p2 = p3 + p4 must make (p1+p2)^2 and (p3+p4)^2
 * compare equal, and the sum rule s + t + u = sum p_i^2 must then hold
 * symbolically for arbitrary masses -- that is the identity the Mandelstam
 * contract rests on, and it is a property of the routing, not of a fixture.
 */
static void test_momentum_conservation(void)
{
    kinematics k = kinematics_open();

    phy_lorentz_vector relation, incoming, outgoing;
    PHY_CHECK_EQ_INT(phy_lorentz_vector_add(&k.v[0], &k.v[1], &incoming),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_vector_add(&k.v[2], &k.v[3], &outgoing),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_vector_sub(&incoming, &outgoing, &relation),
                     PHY_OK);

    PHY_CHECK(!phy_lorentz_has_conservation(k.f.qft));
    PHY_CHECK_EQ_INT(
        phy_lorentz_declare_conservation(k.f.qft, &relation,
                                         phy_lorentz_momentum_at(k.f.qft, 3u)),
        PHY_OK);
    PHY_CHECK(phy_lorentz_has_conservation(k.f.qft));

    phy_lorentz_vector reduced_in, reduced_out;
    PHY_CHECK_EQ_INT(phy_lorentz_reduce_vector(&incoming, &reduced_in), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_reduce_vector(&outgoing, &reduced_out),
                     PHY_OK);
    phy_ir_ref s_in = PHY_IR_NULL;
    phy_ir_ref s_out = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_vector_dot(&reduced_in, &reduced_in, &s_in),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_vector_dot(&reduced_out, &reduced_out, &s_out),
                     PHY_OK);
    expect_zero(k.f.cas, s_in, s_out);

    /* s + t + u = p1^2 + p2^2 + p3^2 + p4^2 in the Peskin routing. Checked
     * before the on-shell pass so it is an identity in the momenta, not an
     * arithmetic accident of four mass symbols. */
    phy_lorentz_vector t_vector, u_vector;
    PHY_CHECK_EQ_INT(phy_lorentz_vector_sub(&k.v[0], &k.v[2], &t_vector),
                     PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_vector_sub(&k.v[0], &k.v[3], &u_vector),
                     PHY_OK);

    phy_ir_ref s = PHY_IR_NULL;
    phy_ir_ref t = PHY_IR_NULL;
    phy_ir_ref u = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_vector_dot(&incoming, &incoming, &s), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_vector_dot(&t_vector, &t_vector, &t), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_vector_dot(&u_vector, &u_vector, &u), PHY_OK);

    phy_ir_ref squares[4];
    for (unsigned i = 0u; i < 4u; i++) {
        PHY_CHECK_EQ_INT(
            phy_lorentz_vector_dot(&k.v[i], &k.v[i], &squares[i]), PHY_OK);
    }
    phy_ir_ref stu = PHY_IR_NULL;
    phy_ir_ref total = PHY_IR_NULL;
    const phy_ir_ref invariants[3] = {s, t, u};
    PHY_CHECK_EQ_INT(phy_cas_add(k.f.cas, invariants, 3u, &stu), PHY_OK);
    PHY_CHECK_EQ_INT(phy_cas_add(k.f.cas, squares, 4u, &total), PHY_OK);

    phy_ir_ref reduced_stu = PHY_IR_NULL;
    phy_ir_ref reduced_total = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_reduce(k.f.qft, stu, &reduced_stu), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_reduce(k.f.qft, total, &reduced_total),
                     PHY_OK);
    expect_zero(k.f.cas, reduced_stu, reduced_total);

    fixture_close(&k.f);
}

static void test_conservation_rejects_absent_momentum(void)
{
    kinematics k = kinematics_open();
    phy_lorentz_vector relation;
    PHY_CHECK_EQ_INT(phy_lorentz_vector_add(&k.v[0], &k.v[1], &relation),
                     PHY_OK);
    /* p3 does not appear in the relation, so there is nothing to solve for. */
    PHY_CHECK_EQ_INT(
        phy_lorentz_declare_conservation(k.f.qft, &relation,
                                         phy_lorentz_momentum_at(k.f.qft, 2u)),
        PHY_ERR_DOMAIN);
    PHY_CHECK(!phy_lorentz_has_conservation(k.f.qft));
    fixture_close(&k.f);
}

int main(void)
{
    PHY_TEST_CASE(test_metric_identity);
    PHY_TEST_CASE(test_index_bundles_do_not_mix);
    PHY_TEST_CASE(test_metric_tensor_is_symmetric);
    PHY_TEST_CASE(test_free_and_dummy_classification);
    PHY_TEST_CASE(test_rename_index_round_trip);
    PHY_TEST_CASE(test_momentum_declaration);
    PHY_TEST_CASE(test_scalar_product_is_symmetric);
    PHY_TEST_CASE(test_vector_algebra);
    PHY_TEST_CASE(test_on_shell_substitution);
    PHY_TEST_CASE(test_momentum_conservation);
    PHY_TEST_CASE(test_conservation_rejects_absent_momentum);
    return PHY_TEST_REPORT("test_lorentz");
}
