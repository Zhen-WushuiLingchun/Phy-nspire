/*
 * Exact finite-field polynomial foundation for modular GCD/factorization.
 */
#include "finite_poly.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy_test.h"

static void set_poly(phy_fpoly *polynomial, const uint32_t *coefficients,
                     size_t length)
{
    PHY_CHECK_EQ_INT(
        phy_fpoly_set(polynomial, coefficients, length), PHY_OK);
}

static void check_coefficients(const phy_fpoly *polynomial,
                               const uint32_t *expected, size_t length)
{
    PHY_CHECK_EQ_INT(phy_fpoly_validate(polynomial), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_degree(polynomial),
                     length == 0u ? -1 : (int)(length - 1u));
    for (size_t index = 0u; index < length; ++index) {
        PHY_CHECK_EQ_INT(
            phy_fpoly_coefficient(polynomial, index), expected[index]);
    }
    PHY_CHECK_EQ_INT(phy_fpoly_coefficient(polynomial, length + 3u), 0);
}

static bool cancel_now(void *user)
{
    unsigned *calls = (unsigned *)user;
    (*calls)++;
    return true;
}

static void test_context_and_canonical_storage(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_fpoly_limits limits;
    phy_fpoly_limits_defaults(&limits);
    PHY_CHECK_EQ_INT(limits.max_degree, 48);
    PHY_CHECK(limits.max_steps >= 1000u);

    phy_fpoly_context context;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 2u, NULL), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 65521u, NULL), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 1u, NULL),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 21u, NULL),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 65537u, NULL),
        PHY_ERR_INVALID_ARGUMENT);

    phy_fpoly polynomial;
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &polynomial), PHY_OK);
    const uint32_t raw[] = {65522u, 131042u, 0u, 0u};
    set_poly(&polynomial, raw, 4u);
    const uint32_t canonical[] = {1u};
    check_coefficients(&polynomial, canonical, 1u);
    PHY_CHECK(phy_fpoly_is_one(&polynomial));
    PHY_CHECK(!phy_fpoly_is_zero(&polynomial));

    set_poly(&polynomial, NULL, 0u);
    PHY_CHECK(phy_fpoly_is_zero(&polynomial));
    PHY_CHECK_EQ_INT(phy_fpoly_degree(&polynomial), -1);

    PHY_CHECK_EQ_INT(
        phy_fpoly_init(NULL, &polynomial), PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_fpoly_init(&context, NULL), PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_fpoly_set(&polynomial, NULL, 1u),
        PHY_ERR_INVALID_ARGUMENT);
    phy_platform_shutdown();
}

static void test_arithmetic_division_and_gcd(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_fpoly_context context;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 5u, NULL), PHY_OK);
    phy_fpoly a, b, product, quotient, remainder, recomposed, gcd;
    phy_fpoly bezout_left, bezout_right, left_term, right_term;
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &product), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &quotient), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &remainder), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &recomposed), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &gcd), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &bezout_left), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &bezout_right), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &left_term), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &right_term), PHY_OK);

    const uint32_t a_coefficients[] = {1u, 0u, 1u};
    const uint32_t b_coefficients[] = {1u, 1u};
    set_poly(&a, a_coefficients, 3u);
    set_poly(&b, b_coefficients, 2u);
    PHY_CHECK_EQ_INT(phy_fpoly_multiply(&a, &b, &product), PHY_OK);
    const uint32_t product_coefficients[] = {1u, 1u, 1u, 1u};
    check_coefficients(&product, product_coefficients, 4u);

    PHY_CHECK_EQ_INT(
        phy_fpoly_divrem(&product, &b, &quotient, &remainder), PHY_OK);
    PHY_CHECK(phy_fpoly_equal(&quotient, &a));
    PHY_CHECK(phy_fpoly_is_zero(&remainder));

    const uint32_t x4_minus_one[] = {4u, 0u, 0u, 0u, 1u};
    const uint32_t x3_minus_one[] = {4u, 0u, 0u, 1u};
    set_poly(&a, x4_minus_one, 5u);
    set_poly(&b, x3_minus_one, 4u);
    PHY_CHECK_EQ_INT(phy_fpoly_gcd(&a, &b, &gcd), PHY_OK);
    const uint32_t x_minus_one[] = {4u, 1u};
    check_coefficients(&gcd, x_minus_one, 2u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_xgcd(
            &a, &b, &gcd, &bezout_left, &bezout_right),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_fpoly_multiply(&bezout_left, &a, &left_term), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_fpoly_multiply(&bezout_right, &b, &right_term), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_fpoly_add(&left_term, &right_term, &recomposed), PHY_OK);
    PHY_CHECK(phy_fpoly_equal(&recomposed, &gcd));
    check_coefficients(&gcd, x_minus_one, 2u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_xgcd(
            &a, &b, &gcd, &gcd, &bezout_right),
        PHY_ERR_INVALID_ARGUMENT);

    PHY_CHECK_EQ_INT(phy_fpoly_derivative(&a, &remainder), PHY_OK);
    const uint32_t derivative[] = {0u, 0u, 0u, 4u};
    check_coefficients(&remainder, derivative, 4u);
    bool square_free = false;
    PHY_CHECK_EQ_INT(
        phy_fpoly_is_square_free(&a, &square_free), PHY_OK);
    PHY_CHECK(square_free);
    const uint32_t repeated[] = {1u, 2u, 1u};
    set_poly(&a, repeated, 3u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_is_square_free(&a, &square_free), PHY_OK);
    PHY_CHECK(!square_free);

    set_poly(&b, NULL, 0u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_divrem(&a, &b, &quotient, &remainder),
        PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(phy_fpoly_gcd(&b, &b, &gcd), PHY_OK);
    PHY_CHECK(phy_fpoly_is_zero(&gcd));
    PHY_CHECK_EQ_INT(
        phy_fpoly_xgcd(
            &b, &b, &gcd, &bezout_left, &bezout_right),
        PHY_OK);
    PHY_CHECK(phy_fpoly_is_zero(&gcd));
    PHY_CHECK(phy_fpoly_is_zero(&bezout_left));
    PHY_CHECK(phy_fpoly_is_zero(&bezout_right));

    PHY_CHECK_EQ_INT(
        phy_fpoly_add(&a, &a, &recomposed), PHY_OK);
    const uint32_t doubled[] = {2u, 4u, 2u};
    check_coefficients(&recomposed, doubled, 3u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_subtract(&recomposed, &a, &recomposed), PHY_OK);
    PHY_CHECK(phy_fpoly_equal(&recomposed, &a));

    phy_platform_shutdown();
}

static void test_exhaustive_small_xgcd_identity(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_fpoly_context context;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 3u, NULL), PHY_OK);
    phy_fpoly a, b, gcd, left, right, left_term, right_term, sum;
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &gcd), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &left), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &right), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &left_term), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &right_term), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &sum), PHY_OK);

    for (uint32_t encoded_a = 0u; encoded_a < 27u; ++encoded_a) {
        uint32_t value = encoded_a;
        uint32_t a_coefficients[3];
        for (size_t index = 0u; index < 3u; ++index) {
            a_coefficients[index] = value % 3u;
            value /= 3u;
        }
        set_poly(&a, a_coefficients, 3u);
        for (uint32_t encoded_b = 0u; encoded_b < 27u; ++encoded_b) {
            value = encoded_b;
            uint32_t b_coefficients[3];
            for (size_t index = 0u; index < 3u; ++index) {
                b_coefficients[index] = value % 3u;
                value /= 3u;
            }
            set_poly(&b, b_coefficients, 3u);
            PHY_CHECK_EQ_INT(
                phy_fpoly_xgcd(&a, &b, &gcd, &left, &right),
                PHY_OK);
            PHY_CHECK_EQ_INT(
                phy_fpoly_multiply(&left, &a, &left_term), PHY_OK);
            PHY_CHECK_EQ_INT(
                phy_fpoly_multiply(&right, &b, &right_term), PHY_OK);
            PHY_CHECK_EQ_INT(
                phy_fpoly_add(&left_term, &right_term, &sum), PHY_OK);
            PHY_CHECK(phy_fpoly_equal(&sum, &gcd));
            if (!phy_fpoly_is_zero(&gcd)) {
                PHY_CHECK_EQ_INT(
                    phy_fpoly_coefficient(
                        &gcd, (size_t)phy_fpoly_degree(&gcd)),
                    1);
            }
        }
    }
    phy_platform_shutdown();
}

static void test_powmod_and_characteristic(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_fpoly_context context;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 5u, NULL), PHY_OK);
    phy_fpoly x, modulus, result;
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &x), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &modulus), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &result), PHY_OK);
    const uint32_t x_coefficients[] = {0u, 1u};
    const uint32_t x2_plus_one[] = {1u, 0u, 1u};
    set_poly(&x, x_coefficients, 2u);
    set_poly(&modulus, x2_plus_one, 3u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_powmod(&x, 5u, &modulus, &result), PHY_OK);
    check_coefficients(&result, x_coefficients, 2u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_powmod(&x, 0u, &modulus, &result), PHY_OK);
    const uint32_t one[] = {1u};
    check_coefficients(&result, one, 1u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_mulmod(&x, &x, &modulus, &result), PHY_OK);
    const uint32_t minus_one[] = {4u};
    check_coefficients(&result, minus_one, 1u);

    phy_fpoly_context characteristic_two;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&characteristic_two, 2u, NULL), PHY_OK);
    phy_fpoly f, derivative;
    PHY_CHECK_EQ_INT(phy_fpoly_init(&characteristic_two, &f), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_fpoly_init(&characteristic_two, &derivative), PHY_OK);
    const uint32_t x4_plus_one[] = {1u, 0u, 0u, 0u, 1u};
    set_poly(&f, x4_plus_one, 5u);
    PHY_CHECK_EQ_INT(phy_fpoly_derivative(&f, &derivative), PHY_OK);
    PHY_CHECK(phy_fpoly_is_zero(&derivative));
    bool square_free = true;
    PHY_CHECK_EQ_INT(
        phy_fpoly_is_square_free(&f, &square_free), PHY_OK);
    PHY_CHECK(!square_free);

    phy_platform_shutdown();
}

static void test_exhaustive_small_division_identity(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_fpoly_context context;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 3u, NULL), PHY_OK);
    phy_fpoly dividend, divisor, quotient, remainder, product, recomposed;
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &dividend), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &divisor), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &quotient), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &remainder), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &product), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &recomposed), PHY_OK);

    for (uint32_t encoded_dividend = 0u; encoded_dividend < 81u;
         ++encoded_dividend) {
        uint32_t value = encoded_dividend;
        uint32_t dividend_coefficients[4];
        for (size_t index = 0u; index < 4u; ++index) {
            dividend_coefficients[index] = value % 3u;
            value /= 3u;
        }
        set_poly(&dividend, dividend_coefficients, 4u);
        for (uint32_t encoded_divisor = 1u; encoded_divisor < 27u;
             ++encoded_divisor) {
            value = encoded_divisor;
            uint32_t divisor_coefficients[3];
            for (size_t index = 0u; index < 3u; ++index) {
                divisor_coefficients[index] = value % 3u;
                value /= 3u;
            }
            set_poly(&divisor, divisor_coefficients, 3u);
            if (phy_fpoly_is_zero(&divisor)) {
                continue;
            }
            PHY_CHECK_EQ_INT(
                phy_fpoly_divrem(
                    &dividend, &divisor, &quotient, &remainder),
                PHY_OK);
            PHY_CHECK_EQ_INT(
                phy_fpoly_multiply(&quotient, &divisor, &product), PHY_OK);
            PHY_CHECK_EQ_INT(
                phy_fpoly_add(&product, &remainder, &recomposed), PHY_OK);
            PHY_CHECK(phy_fpoly_equal(&recomposed, &dividend));
            PHY_CHECK(phy_fpoly_is_zero(&remainder) ||
                      phy_fpoly_degree(&remainder) <
                          phy_fpoly_degree(&divisor));
        }
    }
    phy_platform_shutdown();
}

static void check_factorization_product(
    const phy_fpoly *polynomial,
    const phy_fpoly_factorization *factorization)
{
    PHY_CHECK_EQ_INT(
        phy_fpoly_factorization_validate(factorization), PHY_OK);
    phy_fpoly product;
    PHY_CHECK_EQ_INT(
        phy_fpoly_init(polynomial->context, &product), PHY_OK);
    const uint32_t one[] = {1u};
    set_poly(&product, one, 1u);
    size_t degree_sum = 0u;
    for (size_t index = 0u; index < factorization->count; ++index) {
        PHY_CHECK_EQ_INT(
            phy_fpoly_multiply(
                &product, &factorization->factors[index], &product),
            PHY_OK);
        degree_sum +=
            (size_t)phy_fpoly_degree(&factorization->factors[index]);
        PHY_CHECK_EQ_INT(
            phy_fpoly_coefficient(
                &factorization->factors[index],
                (size_t)phy_fpoly_degree(
                    &factorization->factors[index])),
            1);
    }
    PHY_CHECK(phy_fpoly_equal(&product, polynomial));
    PHY_CHECK_EQ_INT(degree_sum, (size_t)phy_fpoly_degree(polynomial));
}

static void test_berlekamp_square_free_factorization(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_fpoly_context context;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 2u, NULL), PHY_OK);
    phy_fpoly linear, quadratic, cubic, product;
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &linear), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &quadratic), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &cubic), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &product), PHY_OK);
    const uint32_t x_plus_one[] = {1u, 1u};
    const uint32_t x2_x_one[] = {1u, 1u, 1u};
    const uint32_t x3_x_one[] = {1u, 1u, 0u, 1u};
    set_poly(&linear, x_plus_one, 2u);
    set_poly(&quadratic, x2_x_one, 3u);
    set_poly(&cubic, x3_x_one, 4u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_multiply(&linear, &quadratic, &product), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_fpoly_multiply(&product, &cubic, &product), PHY_OK);

    phy_fpoly_factorization factors;
    PHY_CHECK_EQ_INT(
        phy_fpoly_factorization_init(&context, &factors), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_fpoly_factor_square_free(&product, &factors), PHY_OK);
    PHY_CHECK_EQ_INT(factors.count, 3);
    PHY_CHECK(phy_fpoly_equal(&factors.factors[0], &linear));
    PHY_CHECK(phy_fpoly_equal(&factors.factors[1], &quadratic));
    PHY_CHECK(phy_fpoly_equal(&factors.factors[2], &cubic));
    check_factorization_product(&product, &factors);

    const uint32_t irreducible_quartic[] = {1u, 1u, 0u, 0u, 1u};
    set_poly(&product, irreducible_quartic, 5u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_factor_square_free(&product, &factors), PHY_OK);
    PHY_CHECK_EQ_INT(factors.count, 1);
    PHY_CHECK(phy_fpoly_equal(&factors.factors[0], &product));

    PHY_CHECK_EQ_INT(
        phy_fpoly_multiply(&linear, &linear, &product), PHY_OK);
    const phy_fpoly_factorization sentinel = factors;
    PHY_CHECK_EQ_INT(
        phy_fpoly_factor_square_free(&product, &factors), PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(factors.count, sentinel.count);
    PHY_CHECK(
        phy_fpoly_equal(&factors.factors[0], &sentinel.factors[0]));

    phy_fpoly_context five;
    PHY_CHECK_EQ_INT(phy_fpoly_context_init(&five, 5u, NULL), PHY_OK);
    phy_fpoly nonmonic_five;
    phy_fpoly_factorization factors_five;
    PHY_CHECK_EQ_INT(phy_fpoly_init(&five, &nonmonic_five), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_fpoly_factorization_init(&five, &factors_five), PHY_OK);
    const uint32_t two_x_plus_one[] = {1u, 2u};
    set_poly(&nonmonic_five, two_x_plus_one, 2u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_factor_square_free(&nonmonic_five, &factors_five),
        PHY_ERR_DOMAIN);

    phy_platform_shutdown();
}

static void test_exhaustive_small_berlekamp_contract(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    const uint32_t primes[] = {2u, 3u};
    for (size_t prime_index = 0u;
         prime_index < sizeof primes / sizeof primes[0]; ++prime_index) {
        const uint32_t prime = primes[prime_index];
        phy_fpoly_context context;
        PHY_CHECK_EQ_INT(
            phy_fpoly_context_init(&context, prime, NULL), PHY_OK);
        phy_fpoly polynomial;
        PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &polynomial), PHY_OK);
        phy_fpoly_factorization factors;
        PHY_CHECK_EQ_INT(
            phy_fpoly_factorization_init(&context, &factors), PHY_OK);

        uint32_t possibilities = 1u;
        for (size_t degree = 1u; degree <= 4u; ++degree) {
            possibilities *= prime;
            for (uint32_t encoded = 0u; encoded < possibilities; ++encoded) {
                uint32_t coefficients[5] = {0u, 0u, 0u, 0u, 0u};
                uint32_t value = encoded;
                for (size_t index = 0u; index < degree; ++index) {
                    coefficients[index] = value % prime;
                    value /= prime;
                }
                coefficients[degree] = 1u;
                set_poly(&polynomial, coefficients, degree + 1u);
                bool square_free = false;
                PHY_CHECK_EQ_INT(
                    phy_fpoly_is_square_free(
                        &polynomial, &square_free),
                    PHY_OK);
                const phy_status status =
                    phy_fpoly_factor_square_free(&polynomial, &factors);
                if (!square_free) {
                    PHY_CHECK_EQ_INT(status, PHY_ERR_DOMAIN);
                    continue;
                }
                PHY_CHECK_EQ_INT(status, PHY_OK);
                check_factorization_product(&polynomial, &factors);
                for (size_t factor = 0u; factor < factors.count; ++factor) {
                    phy_fpoly_factorization irreducible;
                    PHY_CHECK_EQ_INT(
                        phy_fpoly_factorization_init(
                            &context, &irreducible),
                        PHY_OK);
                    PHY_CHECK_EQ_INT(
                        phy_fpoly_factor_square_free(
                            &factors.factors[factor], &irreducible),
                        PHY_OK);
                    PHY_CHECK_EQ_INT(irreducible.count, 1);
                }
            }
        }
    }
    phy_platform_shutdown();
}

static void test_resource_and_transaction_contracts(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_fpoly_limits limits = {2u, 100u};
    phy_fpoly_context context;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 5u, &limits), PHY_OK);
    phy_fpoly a, b, output;
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &output), PHY_OK);
    const uint32_t quadratic[] = {1u, 1u, 1u};
    const uint32_t sentinel[] = {3u, 2u};
    set_poly(&a, quadratic, 3u);
    set_poly(&b, quadratic, 3u);
    set_poly(&output, sentinel, 2u);
    PHY_CHECK_EQ_INT(
        phy_fpoly_multiply(&a, &b, &output), PHY_ERR_TERM_LIMIT);
    check_coefficients(&output, sentinel, 2u);

    limits.max_degree = 8u;
    limits.max_steps = 100u;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 5u, &limits), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &output), PHY_OK);
    set_poly(&a, quadratic, 3u);
    set_poly(&b, quadratic, 3u);
    set_poly(&output, sentinel, 2u);
    context.limits.max_steps = 1u;
    PHY_CHECK_EQ_INT(
        phy_fpoly_multiply(&a, &b, &output), PHY_ERR_TIMEOUT);
    check_coefficients(&output, sentinel, 2u);

    limits.max_steps = 100u;
    PHY_CHECK_EQ_INT(
        phy_fpoly_context_init(&context, 5u, &limits), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &a), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &b), PHY_OK);
    PHY_CHECK_EQ_INT(phy_fpoly_init(&context, &output), PHY_OK);
    set_poly(&a, quadratic, 3u);
    set_poly(&b, quadratic, 3u);
    set_poly(&output, sentinel, 2u);
    unsigned calls = 0u;
    phy_fpoly_context_set_cancel(&context, cancel_now, &calls);
    PHY_CHECK_EQ_INT(
        phy_fpoly_multiply(&a, &b, &output), PHY_ERR_INTERRUPTED);
    PHY_CHECK(calls > 0u);
    check_coefficients(&output, sentinel, 2u);

    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_context_and_canonical_storage);
    PHY_TEST_CASE(test_arithmetic_division_and_gcd);
    PHY_TEST_CASE(test_exhaustive_small_xgcd_identity);
    PHY_TEST_CASE(test_powmod_and_characteristic);
    PHY_TEST_CASE(test_exhaustive_small_division_identity);
    PHY_TEST_CASE(test_berlekamp_square_free_factorization);
    PHY_TEST_CASE(test_exhaustive_small_berlekamp_contract);
    PHY_TEST_CASE(test_resource_and_transaction_contracts);
    return PHY_TEST_REPORT("test_finite_poly");
}
