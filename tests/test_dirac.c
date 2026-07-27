/*
 * Symbolic Dirac-algebra acceptance for Q-2, Q-3, and Q-4.
 *
 * The matrix oracle remains independent in test_qo_golden.  This suite tests
 * the native engine and compares its symbolic trace recursion against the
 * oracle for every component assignment at lengths 2, 4, and 6.
 */
#include <math.h>
#include <stdio.h>

#include "phy/dirac.h"
#include "phy/platform.h"
#include "phy_test.h"
#include "qo_dirac.h"

#define TEST_MAX_CHAIN 8u
#define TEST_TOL 1.0e-12

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_lorentz_metric *metric;
    phy_dirac *dirac;
    phy_ir_ref one;
} fixture;

static fixture fixture_open_with_limits(const phy_dirac_limits *limits)
{
    fixture f;
    f.ir = phy_ir_context_create(NULL);
    f.cas = f.ir != NULL ? phy_cas_create(f.ir, NULL) : NULL;
    f.metric = NULL;
    f.dirac = NULL;
    f.one = PHY_IR_NULL;
    PHY_CHECK(f.ir != NULL);
    PHY_CHECK(f.cas != NULL);
    if (f.cas != NULL) {
        PHY_CHECK_EQ_INT(phy_lorentz_metric_create(
                             f.cas, "g", "Lorentz", 4u,
                             PHY_LORENTZ_MOSTLY_MINUS, &f.metric),
                         PHY_OK);
    }
    if (f.metric != NULL) {
        PHY_CHECK_EQ_INT(phy_dirac_create(f.metric, limits, &f.dirac), PHY_OK);
    }
    if (f.cas != NULL) {
        PHY_CHECK_EQ_INT(phy_cas_number(f.cas, 1, 1, &f.one), PHY_OK);
    }
    return f;
}

static fixture fixture_open(void)
{
    return fixture_open_with_limits(NULL);
}

static void fixture_close(fixture *f)
{
    PHY_CHECK_EQ_INT(phy_cas_validate(f->cas), PHY_OK);
    PHY_CHECK_EQ_INT(phy_ir_validate(f->ir), PHY_OK);
    phy_dirac_destroy(f->dirac);
    phy_lorentz_metric_destroy(f->metric);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
}

static phy_ir_ref index_of(fixture *f, const char *name,
                           phy_ir_variance variance)
{
    phy_ir_ref result = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_index(f->metric, name, variance, &result), PHY_OK);
    return result;
}

static phy_ir_ref momentum_of(fixture *f, const char *name)
{
    phy_ir_ref result = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_momentum(f->metric, name, &result), PHY_OK);
    return result;
}

static phy_dirac_expr *factor_of(fixture *f, uint32_t line,
                                 phy_ir_ref argument)
{
    phy_dirac_expr *result = NULL;
    const phy_status status =
        phy_lorentz_owns_index(f->metric, argument)
            ? phy_dirac_gamma(f->dirac, line, argument, &result)
            : phy_dirac_slash(f->dirac, line, argument, &result);
    PHY_CHECK_EQ_INT(status, PHY_OK);
    return result;
}

static phy_dirac_expr *chain_of(fixture *f, uint32_t line,
                                const phy_ir_ref *arguments, size_t count)
{
    phy_dirac_expr *result = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_identity(f->dirac, line, &result), PHY_OK);
    for (size_t index = 0u; index < count; ++index) {
        phy_dirac_expr *factor = factor_of(f, line, arguments[index]);
        phy_dirac_expr *product = NULL;
        if (result != NULL && factor != NULL) {
            PHY_CHECK_EQ_INT(phy_dirac_mul(result, factor, &product), PHY_OK);
        }
        phy_dirac_expr_destroy(factor);
        phy_dirac_expr_destroy(result);
        result = product;
    }
    return result;
}

static phy_dirac_expr *scalar_of(fixture *f, phy_ir_ref scalar)
{
    phy_dirac_expr *result = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_scalar(f->dirac, scalar, &result), PHY_OK);
    return result;
}

static void expect_dirac_equal(const phy_dirac_expr *left,
                               const phy_dirac_expr *right)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_dirac_equivalent(left, right, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static void expect_scalar_equal(fixture *f, phy_ir_ref left, phy_ir_ref right)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(phy_cas_equivalent(f->cas, left, right, &decision),
                     PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static phy_ir_ref integer_of(fixture *f, int64_t value)
{
    phy_ir_ref result = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_number(f->cas, value, 1, &result), PHY_OK);
    return result;
}

/* ----------------------------------------------------------- construction */

static void test_context_and_typed_construction(void)
{
    fixture f = fixture_open();
    PHY_CHECK(phy_dirac_cas(f.dirac) == f.cas);
    PHY_CHECK(phy_dirac_metric(f.dirac) == f.metric);
    PHY_CHECK_EQ_INT(phy_dirac_trace_of_one(f.dirac), integer_of(&f, 4));

    phy_lorentz_metric *wrong_dimension = NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_metric_create(
                         f.cas, "h", "Lorentz3", 3u,
                         PHY_LORENTZ_MOSTLY_MINUS, &wrong_dimension),
                     PHY_OK);
    phy_dirac *unsupported = NULL;
    PHY_CHECK_EQ_INT(
        phy_dirac_create(wrong_dimension, NULL, &unsupported),
        PHY_ERR_UNSUPPORTED);
    PHY_CHECK(unsupported == NULL);

    const phy_ir_ref mu =
        index_of(&f, "mu", PHY_IR_INDEX_UPPER);
    phy_dirac_expr *gamma = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_gamma(f.dirac, 7u, mu, &gamma), PHY_OK);
    PHY_CHECK_EQ_INT(phy_dirac_expr_term_count(gamma), 1);
    PHY_CHECK_EQ_INT(phy_dirac_term_factor_count(gamma, 0u), 1);
    uint32_t line = 0u;
    phy_ir_ref argument = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_dirac_term_factor(gamma, 0u, 0u, &line, &argument), PHY_OK);
    PHY_CHECK_EQ_INT(line, 7);
    PHY_CHECK(argument == mu);

    phy_dirac_expr *bad = NULL;
    PHY_CHECK_EQ_INT(
        phy_dirac_gamma(f.dirac, 1u, integer_of(&f, 3), &bad), PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(
        phy_dirac_slash(f.dirac, 1u, mu, &bad), PHY_ERR_TYPE);

    phy_dirac_expr_destroy(gamma);
    phy_lorentz_metric_destroy(wrong_dimension);
    fixture_close(&f);
}

static void test_trace_of_identity_is_configurable(void)
{
    fixture f = fixture_open();
    phy_dirac_expr *identity = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_identity(f.dirac, 3u, &identity), PHY_OK);

    phy_ir_ref trace = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_dirac_trace_scalar(identity, 3u, &trace), PHY_OK);
    PHY_CHECK_EQ_INT(trace, integer_of(&f, 4));

    const phy_ir_ref eight = integer_of(&f, 8);
    PHY_CHECK_EQ_INT(phy_dirac_set_trace_of_one(f.dirac, eight), PHY_OK);
    PHY_CHECK_EQ_INT(phy_dirac_trace_scalar(identity, 3u, &trace), PHY_OK);
    PHY_CHECK_EQ_INT(trace, eight);

    phy_dirac_expr_destroy(identity);
    fixture_close(&f);
}

/* -------------------------------------------------------------- Clifford */

static void test_spin_lines_and_clifford_relation(void)
{
    fixture f = fixture_open();
    const phy_ir_ref mu = index_of(&f, "mu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref nu = index_of(&f, "nu", PHY_IR_INDEX_UPPER);

    phy_dirac_expr *mu_line_2 = factor_of(&f, 2u, mu);
    phy_dirac_expr *nu_line_1 = factor_of(&f, 1u, nu);
    phy_dirac_expr *left = NULL;
    phy_dirac_expr *right = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_mul(mu_line_2, nu_line_1, &left), PHY_OK);
    PHY_CHECK_EQ_INT(phy_dirac_mul(nu_line_1, mu_line_2, &right), PHY_OK);
    expect_dirac_equal(left, right);

    phy_dirac_expr_destroy(left);
    phy_dirac_expr_destroy(right);
    phy_dirac_expr_destroy(mu_line_2);
    phy_dirac_expr_destroy(nu_line_1);

    const phy_ir_ref ab_args[2] = {mu, nu};
    const phy_ir_ref ba_args[2] = {nu, mu};
    phy_dirac_expr *ab = chain_of(&f, 1u, ab_args, 2u);
    phy_dirac_expr *ba = chain_of(&f, 1u, ba_args, 2u);
    phy_dirac_expr *anticommutator = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_add(ab, ba, &anticommutator), PHY_OK);

    phy_ir_ref metric = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_metric_tensor(f.metric, mu, nu, &metric), PHY_OK);
    const phy_ir_ref twice_metric_factors[2] = {integer_of(&f, 2), metric};
    phy_ir_ref twice_metric = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, twice_metric_factors, 2u, &twice_metric), PHY_OK);
    phy_dirac_expr *expected = scalar_of(&f, twice_metric);
    expect_dirac_equal(anticommutator, expected);

    phy_dirac_expr_destroy(expected);
    phy_dirac_expr_destroy(anticommutator);
    phy_dirac_expr_destroy(ba);
    phy_dirac_expr_destroy(ab);
    fixture_close(&f);
}

static void test_slash_anticommutator(void)
{
    fixture f = fixture_open();
    const phy_ir_ref p = momentum_of(&f, "p");
    const phy_ir_ref q = momentum_of(&f, "q");
    const phy_ir_ref pq_args[2] = {p, q};
    const phy_ir_ref qp_args[2] = {q, p};
    phy_dirac_expr *pq = chain_of(&f, 1u, pq_args, 2u);
    phy_dirac_expr *qp = chain_of(&f, 1u, qp_args, 2u);
    phy_dirac_expr *sum = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_add(pq, qp, &sum), PHY_OK);

    phy_ir_ref dot = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.metric, p, q, &dot), PHY_OK);
    const phy_ir_ref factors[2] = {integer_of(&f, 2), dot};
    phy_ir_ref expected_scalar = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, factors, 2u, &expected_scalar), PHY_OK);
    phy_dirac_expr *expected = scalar_of(&f, expected_scalar);
    expect_dirac_equal(sum, expected);

    phy_dirac_expr_destroy(expected);
    phy_dirac_expr_destroy(sum);
    phy_dirac_expr_destroy(qp);
    phy_dirac_expr_destroy(pq);
    fixture_close(&f);
}

/* ----------------------------------------------------------- contraction */

static void expect_contract(fixture *f, const phy_ir_ref *input,
                            size_t input_count,
                            const phy_ir_ref *expected_arguments,
                            size_t expected_count,
                            phy_ir_ref expected_coefficient)
{
    phy_dirac_expr *chain = chain_of(f, 1u, input, input_count);
    phy_dirac_expr *contracted = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_contract(chain, &contracted), PHY_OK);
    phy_dirac_expr *expected =
        chain_of(f, 1u, expected_arguments, expected_count);
    phy_dirac_expr *scaled = NULL;
    PHY_CHECK_EQ_INT(
        phy_dirac_scale(expected, expected_coefficient, &scaled), PHY_OK);
    expect_dirac_equal(contracted, scaled);
    phy_dirac_expr_destroy(scaled);
    phy_dirac_expr_destroy(expected);
    phy_dirac_expr_destroy(contracted);
    phy_dirac_expr_destroy(chain);
}

static void test_contraction_rules_and_order_regression(void)
{
    fixture f = fixture_open();
    const phy_ir_ref mu_up = index_of(&f, "mu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref mu_down = index_of(&f, "mu", PHY_IR_INDEX_LOWER);
    const phy_ir_ref nu = index_of(&f, "nu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref rho = index_of(&f, "rho", PHY_IR_INDEX_UPPER);
    const phy_ir_ref sigma = index_of(&f, "sigma", PHY_IR_INDEX_UPPER);

    const phy_ir_ref rule1[2] = {mu_up, mu_down};
    expect_contract(&f, rule1, 2u, NULL, 0u, integer_of(&f, 4));

    const phy_ir_ref rule2[3] = {mu_up, nu, mu_down};
    const phy_ir_ref rule2_expected[1] = {nu};
    expect_contract(&f, rule2, 3u, rule2_expected, 1u, integer_of(&f, -2));

    const phy_ir_ref rule3[4] = {mu_up, nu, rho, mu_down};
    phy_ir_ref nu_rho = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_metric_tensor(f.metric, nu, rho, &nu_rho), PHY_OK);
    const phy_ir_ref four_metric[2] = {integer_of(&f, 4), nu_rho};
    phy_ir_ref rule3_coefficient = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, four_metric, 2u, &rule3_coefficient), PHY_OK);
    expect_contract(&f, rule3, 4u, NULL, 0u, rule3_coefficient);

    const phy_ir_ref rule4[5] = {mu_up, nu, rho, sigma, mu_down};
    const phy_ir_ref reversed[3] = {sigma, rho, nu};
    expect_contract(&f, rule4, 5u, reversed, 3u, integer_of(&f, -2));

    /*
     * SymPy issue #23823: removing a contracted pair at the end must not
     * reverse the leading free matrices. Both locations yield 4 gamma^rho
     * gamma^sigma with that order intact.
     */
    const phy_ir_ref pair_first[4] = {mu_up, mu_down, rho, sigma};
    const phy_ir_ref pair_last[4] = {rho, sigma, mu_up, mu_down};
    const phy_ir_ref free_order[2] = {rho, sigma};
    expect_contract(&f, pair_first, 4u, free_order, 2u, integer_of(&f, 4));
    expect_contract(&f, pair_last, 4u, free_order, 2u, integer_of(&f, 4));

    fixture_close(&f);
}

/* --------------------------------------------------------------- traces */

static void test_odd_and_closed_traces(void)
{
    fixture f = fixture_open();
    const phy_ir_ref mu = index_of(&f, "mu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref nu = index_of(&f, "nu", PHY_IR_INDEX_UPPER);
    const phy_ir_ref rho = index_of(&f, "rho", PHY_IR_INDEX_UPPER);

    const phy_ir_ref one_arg[1] = {mu};
    const phy_ir_ref three_args[3] = {mu, nu, rho};
    phy_dirac_expr *one = chain_of(&f, 1u, one_arg, 1u);
    phy_dirac_expr *three = chain_of(&f, 1u, three_args, 3u);
    phy_ir_ref trace = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_dirac_trace_scalar(one, 1u, &trace), PHY_OK);
    PHY_CHECK_EQ_INT(trace, integer_of(&f, 0));
    PHY_CHECK_EQ_INT(phy_dirac_trace_scalar(three, 1u, &trace), PHY_OK);
    PHY_CHECK_EQ_INT(trace, integer_of(&f, 0));

    const phy_ir_ref two_args[2] = {mu, nu};
    phy_dirac_expr *two = chain_of(&f, 1u, two_args, 2u);
    PHY_CHECK_EQ_INT(phy_dirac_trace_scalar(two, 1u, &trace), PHY_OK);
    phy_ir_ref metric = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_lorentz_metric_tensor(f.metric, mu, nu, &metric), PHY_OK);
    const phy_ir_ref factors[2] = {integer_of(&f, 4), metric};
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_mul(f.cas, factors, 2u, &expected), PHY_OK);
    expect_scalar_equal(&f, trace, expected);

    phy_dirac_expr_destroy(two);
    phy_dirac_expr_destroy(three);
    phy_dirac_expr_destroy(one);
    fixture_close(&f);
}

typedef struct {
    phy_ir_symbol name[TEST_MAX_CHAIN];
    int component[TEST_MAX_CHAIN];
    size_t count;
    phy_ir_symbol metric;
} scalar_assignment;

static int component_of(const scalar_assignment *assignment,
                        phy_ir_symbol name, bool *ok)
{
    for (size_t index = 0u; index < assignment->count; ++index) {
        if (assignment->name[index] == name) {
            return assignment->component[index];
        }
    }
    *ok = false;
    return 0;
}

static double evaluate_scalar(const phy_ir_context *ir, phy_ir_ref expression,
                              const scalar_assignment *assignment, bool *ok)
{
    int64_t numerator = 0;
    int64_t denominator = 1;
    if (phy_ir_integer_value(ir, expression, &numerator)) {
        return (double)numerator;
    }
    if (phy_ir_rational_value(ir, expression, &numerator, &denominator)) {
        return (double)numerator / (double)denominator;
    }

    const phy_ir_kind kind = phy_ir_kind_of(ir, expression);
    if (kind == PHY_IR_ADD || kind == PHY_IR_MUL) {
        double value = kind == PHY_IR_ADD ? 0.0 : 1.0;
        const size_t count = phy_ir_child_count(ir, expression);
        for (size_t index = 0u; index < count; ++index) {
            const double child = evaluate_scalar(
                ir, phy_ir_child(ir, expression, index), assignment, ok);
            if (!*ok) {
                return 0.0;
            }
            value = kind == PHY_IR_ADD ? value + child : value * child;
        }
        return value;
    }
    if (kind == PHY_IR_POW) {
        const double base = evaluate_scalar(
            ir, phy_ir_child(ir, expression, 0u), assignment, ok);
        int64_t exponent = 0;
        if (!*ok ||
            !phy_ir_integer_value(ir, phy_ir_child(ir, expression, 1u),
                                  &exponent)) {
            *ok = false;
            return 0.0;
        }
        return pow(base, (double)exponent);
    }
    if (kind == PHY_IR_TENSOR && phy_ir_head(ir, expression) ==
                                        assignment->metric &&
        phy_ir_child_count(ir, expression) == 2u) {
        const phy_ir_ref left = phy_ir_child(ir, expression, 0u);
        const phy_ir_ref right = phy_ir_child(ir, expression, 1u);
        const int mu = component_of(assignment, phy_ir_head(ir, left), ok);
        const int nu = component_of(assignment, phy_ir_head(ir, right), ok);
        return *ok ? qo_metric(mu, nu) : 0.0;
    }

    *ok = false;
    return 0.0;
}

static qo_m4 evaluate_dirac_matrix(
    const phy_dirac_expr *expression, qo_basis basis,
    const scalar_assignment *assignment, bool *ok)
{
    const phy_dirac *owner = phy_dirac_expr_owner(expression);
    const phy_ir_context *ir = phy_cas_ir(phy_dirac_cas(owner));
    qo_m4 total = qo_m4_zero();
    const size_t term_count = phy_dirac_expr_term_count(expression);
    for (size_t term = 0u; term < term_count; ++term) {
        const double coefficient = evaluate_scalar(
            ir, phy_dirac_term_coefficient(expression, term), assignment, ok);
        if (!*ok) {
            return total;
        }
        qo_m4 product = qo_m4_identity();
        const size_t factor_count =
            phy_dirac_term_factor_count(expression, term);
        for (size_t factor = 0u; factor < factor_count; ++factor) {
            uint32_t line = 0u;
            phy_ir_ref argument = PHY_IR_NULL;
            if (phy_dirac_term_factor(expression, term, factor, &line,
                                      &argument) != PHY_OK ||
                line != 1u ||
                phy_ir_kind_of(ir, argument) != PHY_IR_INDEX) {
                *ok = false;
                return total;
            }
            const int component =
                component_of(assignment, phy_ir_head(ir, argument), ok);
            phy_ir_variance variance;
            if (!*ok || !phy_ir_index_variance(ir, argument, &variance)) {
                *ok = false;
                return total;
            }
            const qo_m4 gamma =
                variance == PHY_IR_INDEX_UPPER
                    ? qo_gamma(basis, component)
                    : qo_gamma_lower(basis, component);
            product = qo_m4_mul(product, gamma);
        }
        total = qo_m4_add(
            total, qo_m4_scale(product, qo_c_real(coefficient)));
    }
    return total;
}

static void test_general_even_contraction_matches_oracle(void)
{
    fixture f = fixture_open();
    static const char *const names[4] = {"a", "b", "c", "d"};
    phy_ir_ref free_indices[4];
    scalar_assignment assignment;
    assignment.count = 4u;
    assignment.metric = phy_ir_intern(f.ir, "g");
    for (size_t index = 0u; index < 4u; ++index) {
        free_indices[index] =
            index_of(&f, names[index], PHY_IR_INDEX_UPPER);
        assignment.name[index] = phy_ir_head(f.ir, free_indices[index]);
        assignment.component[index] = 0;
    }

    const phy_ir_ref arguments[6] = {
        index_of(&f, "mu", PHY_IR_INDEX_UPPER),
        free_indices[0], free_indices[1], free_indices[2], free_indices[3],
        index_of(&f, "mu", PHY_IR_INDEX_LOWER)};
    phy_dirac_expr *chain = chain_of(&f, 1u, arguments, 6u);
    phy_dirac_expr *contracted = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_contract(chain, &contracted), PHY_OK);

    for (int basis_value = 0; basis_value < QO_BASIS_COUNT; ++basis_value) {
        const qo_basis basis = (qo_basis)basis_value;
        for (unsigned code = 0u; code < 256u; ++code) {
            unsigned packed = code;
            for (size_t slot = 0u; slot < 4u; ++slot) {
                assignment.component[slot] = (int)(packed & 3u);
                packed >>= 2u;
            }

            qo_m4 direct = qo_m4_zero();
            for (int mu = 0; mu < 4; ++mu) {
                qo_m4 term = qo_gamma(basis, mu);
                for (size_t slot = 0u; slot < 4u; ++slot) {
                    term = qo_m4_mul(
                        term, qo_gamma(basis,
                                       assignment.component[slot]));
                }
                term = qo_m4_mul(term, qo_gamma_lower(basis, mu));
                direct = qo_m4_add(direct, term);
            }

            bool ok = true;
            const qo_m4 native = evaluate_dirac_matrix(
                contracted, basis, &assignment, &ok);
            PHY_CHECK(ok);
            PHY_CHECK(qo_m4_maxdiff(native, direct) <= TEST_TOL);
        }
    }

    phy_dirac_expr_destroy(contracted);
    phy_dirac_expr_destroy(chain);
    fixture_close(&f);
}

static void test_symbolic_trace_matches_oracle_exhaustively(void)
{
    fixture f = fixture_open();
    static const char *const names[TEST_MAX_CHAIN] = {
        "i0", "i1", "i2", "i3", "i4", "i5", "i6", "i7"};
    phy_ir_ref indices[TEST_MAX_CHAIN];
    scalar_assignment assignment;
    assignment.count = TEST_MAX_CHAIN;
    assignment.metric = phy_ir_intern(f.ir, "g");
    for (size_t index = 0u; index < TEST_MAX_CHAIN; ++index) {
        indices[index] =
            index_of(&f, names[index], PHY_IR_INDEX_UPPER);
        assignment.name[index] = phy_ir_head(f.ir, indices[index]);
        assignment.component[index] = 0;
    }

    static const uint32_t lengths[3] = {2u, 4u, 6u};
    static const uint32_t leaves[3] = {1u, 3u, 15u};
    for (size_t test = 0u; test < 3u; ++test) {
        const uint32_t length = lengths[test];
        phy_dirac_expr *chain = chain_of(&f, 1u, indices, length);
        phy_ir_ref symbolic = PHY_IR_NULL;
        PHY_CHECK_EQ_INT(
            phy_dirac_trace_scalar(chain, 1u, &symbolic), PHY_OK);
        PHY_CHECK_EQ_INT(phy_dirac_generated_terms(f.dirac), leaves[test]);

        const unsigned total = 1u << (2u * length);
        int components[QO_MAX_GAMMAS];
        for (unsigned code = 0u; code < total; ++code) {
            unsigned packed = code;
            for (uint32_t slot = 0u; slot < length; ++slot) {
                const int component = (int)(packed & 3u);
                packed >>= 2u;
                components[slot] = component;
                assignment.component[slot] = component;
            }
            bool ok = true;
            const double native =
                evaluate_scalar(f.ir, symbolic, &assignment, &ok);
            const double oracle =
                qo_trace_recursive(components, (int)length);
            PHY_CHECK(ok);
            PHY_CHECK(fabs(native - oracle) <= TEST_TOL);
        }
        phy_dirac_expr_destroy(chain);
    }
    fixture_close(&f);
}

static void test_four_slash_trace(void)
{
    fixture f = fixture_open();
    phy_ir_ref p[4] = {
        momentum_of(&f, "p1"), momentum_of(&f, "p2"),
        momentum_of(&f, "p3"), momentum_of(&f, "p4")};
    phy_dirac_expr *chain = chain_of(&f, 1u, p, 4u);
    phy_ir_ref trace = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_dirac_trace_scalar(chain, 1u, &trace), PHY_OK);

    phy_ir_ref dot12, dot34, dot13, dot24, dot14, dot23;
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.metric, p[0], p[1], &dot12), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.metric, p[2], p[3], &dot34), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.metric, p[0], p[2], &dot13), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.metric, p[1], p[3], &dot24), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.metric, p[0], p[3], &dot14), PHY_OK);
    PHY_CHECK_EQ_INT(phy_lorentz_dot(f.metric, p[1], p[2], &dot23), PHY_OK);

    const phy_ir_ref first_factors[2] = {dot12, dot34};
    const phy_ir_ref second_factors[3] = {integer_of(&f, -1), dot13, dot24};
    const phy_ir_ref third_factors[2] = {dot14, dot23};
    phy_ir_ref terms[3] = {PHY_IR_NULL, PHY_IR_NULL, PHY_IR_NULL};
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, first_factors, 2u, &terms[0]), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, second_factors, 3u, &terms[1]), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, third_factors, 2u, &terms[2]), PHY_OK);
    phy_ir_ref sum = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_cas_add(f.cas, terms, 3u, &sum), PHY_OK);
    const phy_ir_ref expected_factors[2] = {integer_of(&f, 4), sum};
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_mul(f.cas, expected_factors, 2u, &expected), PHY_OK);
    expect_scalar_equal(&f, trace, expected);

    phy_dirac_expr_destroy(chain);
    fixture_close(&f);
}

static void test_trace_contracts_before_expansion(void)
{
    fixture f = fixture_open();
    const phy_ir_ref arguments[6] = {
        index_of(&f, "mu", PHY_IR_INDEX_UPPER),
        index_of(&f, "mu", PHY_IR_INDEX_LOWER),
        index_of(&f, "nu", PHY_IR_INDEX_UPPER),
        index_of(&f, "nu", PHY_IR_INDEX_LOWER),
        index_of(&f, "rho", PHY_IR_INDEX_UPPER),
        index_of(&f, "rho", PHY_IR_INDEX_LOWER)};
    phy_dirac_expr *chain = chain_of(&f, 1u, arguments, 6u);
    phy_ir_ref trace = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_dirac_trace_scalar(chain, 1u, &trace), PHY_OK);
    PHY_CHECK_EQ_INT(trace, integer_of(&f, 256));

    /*
     * A raw six-gamma trace has 15 metric-recursion leaves. Contracting the
     * three adjacent dummy pairs first generates three reduced terms and one
     * scalar trace leaf, strictly fewer than the uncontracted expansion.
     */
    PHY_CHECK_EQ_INT(phy_dirac_generated_terms(f.dirac), 4);

    phy_dirac_expr_destroy(chain);
    fixture_close(&f);
}

/* ------------------------------------------------------------- resources */

static void test_resource_limits_fail_transactionally(void)
{
    phy_dirac_limits limits;
    phy_dirac_limits_defaults(&limits);
    limits.max_terms = 2u;
    fixture f = fixture_open_with_limits(&limits);
    const phy_ir_ref args[4] = {
        index_of(&f, "a", PHY_IR_INDEX_UPPER),
        index_of(&f, "b", PHY_IR_INDEX_UPPER),
        index_of(&f, "c", PHY_IR_INDEX_UPPER),
        index_of(&f, "d", PHY_IR_INDEX_UPPER)};
    phy_dirac_expr *chain = chain_of(&f, 1u, args, 4u);
    phy_ir_ref output = integer_of(&f, 37);
    const phy_ir_ref sentinel = output;
    PHY_CHECK_EQ_INT(
        phy_dirac_trace_scalar(chain, 1u, &output), PHY_ERR_TERM_LIMIT);
    PHY_CHECK(output == sentinel);

    /* A failed expansion does not poison the context or the next operation. */
    phy_dirac_expr *identity = NULL;
    PHY_CHECK_EQ_INT(phy_dirac_identity(f.dirac, 1u, &identity), PHY_OK);
    PHY_CHECK_EQ_INT(phy_dirac_trace_scalar(identity, 1u, &output), PHY_OK);
    PHY_CHECK_EQ_INT(output, integer_of(&f, 4));

    phy_dirac_expr_destroy(identity);
    phy_dirac_expr_destroy(chain);
    fixture_close(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_context_and_typed_construction);
    PHY_TEST_CASE(test_trace_of_identity_is_configurable);
    PHY_TEST_CASE(test_spin_lines_and_clifford_relation);
    PHY_TEST_CASE(test_slash_anticommutator);
    PHY_TEST_CASE(test_contraction_rules_and_order_regression);
    PHY_TEST_CASE(test_general_even_contraction_matches_oracle);
    PHY_TEST_CASE(test_odd_and_closed_traces);
    PHY_TEST_CASE(test_symbolic_trace_matches_oracle_exhaustively);
    PHY_TEST_CASE(test_four_slash_trace);
    PHY_TEST_CASE(test_trace_contracts_before_expansion);
    PHY_TEST_CASE(test_resource_limits_fail_transactionally);
    return PHY_TEST_REPORT("test_dirac");
}
