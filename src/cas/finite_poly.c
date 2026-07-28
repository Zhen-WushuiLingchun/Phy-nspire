/*
 * Bounded arithmetic in F_p[x].
 *
 * The prime is deliberately a small word (<= 65521). Products still use
 * uint64_t so every coefficient operation is exact on both host and ARM.
 * Public calls are transactional: a local polynomial is published only after
 * the complete operation succeeds.
 */
#include <string.h>

#include "finite_poly.h"

#define PHY_FPOLY_CONTEXT_MAGIC UINT32_C(0x46504358)
#define PHY_FPOLY_MAGIC UINT32_C(0x46504f4c)
#define PHY_FPOLY_FACTORIZATION_MAGIC UINT32_C(0x46464143)

static bool context_valid(const phy_fpoly_context *context)
{
    return context != NULL &&
           context->private_magic == PHY_FPOLY_CONTEXT_MAGIC &&
           context->prime >= 2u && context->prime <= PHY_FPOLY_MAX_PRIME &&
           context->limits.max_degree <= PHY_FPOLY_HARD_MAX_DEGREE &&
           context->limits.max_steps > 0u;
}

static bool is_prime(uint32_t value)
{
    if (value < 2u) {
        return false;
    }
    if ((value & 1u) == 0u) {
        return value == 2u;
    }
    for (uint32_t divisor = 3u;
         divisor <= value / divisor; divisor += 2u) {
        if (value % divisor == 0u) {
            return false;
        }
    }
    return true;
}

static bool polynomial_valid(const phy_fpoly *polynomial)
{
    if (polynomial == NULL || polynomial->private_magic != PHY_FPOLY_MAGIC ||
        !context_valid(polynomial->context) ||
        polynomial->length > polynomial->context->limits.max_degree + 1u) {
        return false;
    }
    if (polynomial->length > 0u &&
        polynomial->coefficients[polynomial->length - 1u] == 0u) {
        return false;
    }
    for (size_t index = 0u; index < polynomial->length; ++index) {
        if (polynomial->coefficients[index] >= polynomial->context->prime) {
            return false;
        }
    }
    return true;
}

static bool compatible(const phy_fpoly *left, const phy_fpoly *right,
                       const phy_fpoly *out)
{
    return polynomial_valid(left) && polynomial_valid(right) &&
           polynomial_valid(out) && left->context == right->context &&
           left->context == out->context;
}

static phy_status begin(phy_fpoly_context *context)
{
    if (!context_valid(context)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    context->steps = 0u;
    if (context->cancelled != NULL &&
        context->cancelled(context->cancel_user)) {
        return PHY_ERR_INTERRUPTED;
    }
    return PHY_OK;
}

static phy_status step(phy_fpoly_context *context, uint32_t amount)
{
    if (context->cancelled != NULL &&
        context->cancelled(context->cancel_user)) {
        return PHY_ERR_INTERRUPTED;
    }
    if (amount > context->limits.max_steps - context->steps) {
        return PHY_ERR_TIMEOUT;
    }
    context->steps += amount;
    return PHY_OK;
}

static void zero(phy_fpoly_context *context, phy_fpoly *polynomial)
{
    polynomial->context = context;
    polynomial->length = 0u;
    memset(polynomial->coefficients, 0, sizeof polynomial->coefficients);
    polynomial->private_magic = PHY_FPOLY_MAGIC;
}

static void trim(phy_fpoly *polynomial)
{
    while (polynomial->length > 0u &&
           polynomial->coefficients[polynomial->length - 1u] == 0u) {
        polynomial->length--;
    }
}

static uint32_t add_mod(uint32_t left, uint32_t right, uint32_t prime)
{
    const uint32_t sum = left + right;
    return sum >= prime ? sum - prime : sum;
}

static uint32_t subtract_mod(uint32_t left, uint32_t right, uint32_t prime)
{
    return left >= right ? left - right : prime - (right - left);
}

static uint32_t multiply_mod(uint32_t left, uint32_t right, uint32_t prime)
{
    return (uint32_t)(((uint64_t)left * (uint64_t)right) % prime);
}

static uint32_t inverse_mod(uint32_t value, uint32_t prime)
{
    /*
     * Fermat inversion. The context guarantees a prime modulus, and callers
     * reject zero. Keeping this in the same multiply primitive avoids signed
     * extended-Euclid corner cases on 32-bit ARM.
     */
    uint32_t result = 1u;
    uint32_t factor = value;
    uint32_t exponent = prime - 2u;
    while (exponent != 0u) {
        if ((exponent & 1u) != 0u) {
            result = multiply_mod(result, factor, prime);
        }
        exponent >>= 1u;
        if (exponent != 0u) {
            factor = multiply_mod(factor, factor, prime);
        }
    }
    return result;
}

static phy_status make_monic(phy_fpoly *polynomial)
{
    if (polynomial->length == 0u) {
        return PHY_OK;
    }
    phy_fpoly_context *context = polynomial->context;
    const uint32_t inverse = inverse_mod(
        polynomial->coefficients[polynomial->length - 1u], context->prime);
    for (size_t index = 0u; index < polynomial->length; ++index) {
        phy_status status = step(context, 1u);
        if (status != PHY_OK) {
            return status;
        }
        polynomial->coefficients[index] = multiply_mod(
            polynomial->coefficients[index], inverse, context->prime);
    }
    return PHY_OK;
}

static phy_status multiply_internal(const phy_fpoly *left,
                                    const phy_fpoly *right,
                                    phy_fpoly *out_product)
{
    phy_fpoly result;
    zero(left->context, &result);
    if (left->length == 0u || right->length == 0u) {
        *out_product = result;
        return PHY_OK;
    }
    const size_t length = left->length + right->length - 1u;
    if (length > left->context->limits.max_degree + 1u) {
        return PHY_ERR_TERM_LIMIT;
    }
    result.length = length;
    for (size_t left_index = 0u; left_index < left->length; ++left_index) {
        for (size_t right_index = 0u; right_index < right->length;
             ++right_index) {
            phy_status status = step(left->context, 1u);
            if (status != PHY_OK) {
                return status;
            }
            const size_t degree = left_index + right_index;
            const uint32_t product = multiply_mod(
                left->coefficients[left_index],
                right->coefficients[right_index], left->context->prime);
            result.coefficients[degree] = add_mod(
                result.coefficients[degree], product, left->context->prime);
        }
    }
    trim(&result);
    *out_product = result;
    return PHY_OK;
}

static phy_status divrem_internal(const phy_fpoly *dividend,
                                  const phy_fpoly *divisor,
                                  phy_fpoly *out_quotient,
                                  phy_fpoly *out_remainder)
{
    if (divisor->length == 0u) {
        return PHY_ERR_DOMAIN;
    }
    phy_fpoly quotient;
    phy_fpoly remainder = *dividend;
    zero(dividend->context, &quotient);
    if (dividend->length >= divisor->length) {
        quotient.length = dividend->length - divisor->length + 1u;
    }
    const uint32_t leading_inverse = inverse_mod(
        divisor->coefficients[divisor->length - 1u],
        dividend->context->prime);
    while (remainder.length >= divisor->length &&
           remainder.length != 0u) {
        phy_status status = step(dividend->context, 1u);
        if (status != PHY_OK) {
            return status;
        }
        const size_t shift = remainder.length - divisor->length;
        const uint32_t factor = multiply_mod(
            remainder.coefficients[remainder.length - 1u],
            leading_inverse, dividend->context->prime);
        quotient.coefficients[shift] = factor;
        for (size_t index = 0u; index < divisor->length; ++index) {
            status = step(dividend->context, 1u);
            if (status != PHY_OK) {
                return status;
            }
            const uint32_t product = multiply_mod(
                factor, divisor->coefficients[index],
                dividend->context->prime);
            remainder.coefficients[index + shift] = subtract_mod(
                remainder.coefficients[index + shift], product,
                dividend->context->prime);
        }
        trim(&remainder);
    }
    trim(&quotient);
    *out_quotient = quotient;
    *out_remainder = remainder;
    return PHY_OK;
}

static phy_status remainder_internal(const phy_fpoly *dividend,
                                     const phy_fpoly *divisor,
                                     phy_fpoly *out_remainder)
{
    phy_fpoly quotient;
    zero(dividend->context, &quotient);
    return divrem_internal(
        dividend, divisor, &quotient, out_remainder);
}

static phy_status multiply_remainder_internal(
    const phy_fpoly *left, const phy_fpoly *right,
    const phy_fpoly *modulus, phy_fpoly *out_remainder)
{
    if (modulus->length == 0u) {
        return PHY_ERR_DOMAIN;
    }
    phy_fpoly result;
    zero(left->context, &result);
    if (left->length == 0u || right->length == 0u ||
        modulus->length == 1u) {
        *out_remainder = result;
        return PHY_OK;
    }

    uint32_t workspace[2u * PHY_FPOLY_HARD_MAX_DEGREE + 1u];
    memset(workspace, 0, sizeof workspace);
    const size_t product_length = left->length + right->length - 1u;
    for (size_t left_index = 0u; left_index < left->length; ++left_index) {
        for (size_t right_index = 0u; right_index < right->length;
             ++right_index) {
            phy_status status = step(left->context, 1u);
            if (status != PHY_OK) {
                return status;
            }
            const size_t degree = left_index + right_index;
            const uint32_t product = multiply_mod(
                left->coefficients[left_index],
                right->coefficients[right_index], left->context->prime);
            workspace[degree] = add_mod(
                workspace[degree], product, left->context->prime);
        }
    }

    const size_t modulus_degree = modulus->length - 1u;
    const uint32_t leading_inverse = inverse_mod(
        modulus->coefficients[modulus_degree], left->context->prime);
    for (size_t degree = product_length; degree-- > modulus_degree;) {
        if (workspace[degree] == 0u) {
            continue;
        }
        const uint32_t factor = multiply_mod(
            workspace[degree], leading_inverse, left->context->prime);
        const size_t shift = degree - modulus_degree;
        for (size_t index = 0u; index < modulus->length; ++index) {
            phy_status status = step(left->context, 1u);
            if (status != PHY_OK) {
                return status;
            }
            const uint32_t product = multiply_mod(
                factor, modulus->coefficients[index],
                left->context->prime);
            workspace[index + shift] = subtract_mod(
                workspace[index + shift], product, left->context->prime);
        }
    }
    result.length =
        product_length < modulus_degree ? product_length : modulus_degree;
    memcpy(result.coefficients, workspace,
           result.length * sizeof(uint32_t));
    trim(&result);
    *out_remainder = result;
    return PHY_OK;
}

static phy_status gcd_internal(const phy_fpoly *left,
                               const phy_fpoly *right,
                               phy_fpoly *out_gcd)
{
    phy_status status = PHY_OK;
    phy_fpoly a = *left;
    phy_fpoly b = *right;
    while (status == PHY_OK && b.length != 0u) {
        phy_fpoly remainder;
        zero(left->context, &remainder);
        status = remainder_internal(&a, &b, &remainder);
        a = b;
        b = remainder;
    }
    if (status == PHY_OK) {
        status = make_monic(&a);
    }
    if (status == PHY_OK) {
        *out_gcd = a;
    }
    return status;
}

static phy_status powmod_internal(const phy_fpoly *base, uint64_t exponent,
                                  const phy_fpoly *modulus,
                                  phy_fpoly *out_remainder)
{
    phy_fpoly result;
    phy_fpoly factor;
    zero(base->context, &result);
    zero(base->context, &factor);
    result.length = 1u;
    result.coefficients[0] = 1u;
    phy_status status = remainder_internal(&result, modulus, &result);
    if (status == PHY_OK) {
        status = remainder_internal(base, modulus, &factor);
    }
    while (status == PHY_OK && exponent != 0u) {
        if ((exponent & UINT64_C(1)) != 0u) {
            status = multiply_remainder_internal(
                &result, &factor, modulus, &result);
        }
        exponent >>= 1u;
        if (status == PHY_OK && exponent != 0u) {
            status = multiply_remainder_internal(
                &factor, &factor, modulus, &factor);
        }
    }
    if (status == PHY_OK) {
        *out_remainder = result;
    }
    return status;
}

void phy_fpoly_limits_defaults(phy_fpoly_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    out_limits->max_degree = 48u;
    out_limits->max_steps = 200000u;
}

phy_status phy_fpoly_context_init(phy_fpoly_context *context, uint32_t prime,
                                  const phy_fpoly_limits *limits)
{
    if (context == NULL || !is_prime(prime) ||
        prime > PHY_FPOLY_MAX_PRIME) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_fpoly_limits selected;
    phy_fpoly_limits_defaults(&selected);
    if (limits != NULL) {
        selected = *limits;
    }
    if (selected.max_degree > PHY_FPOLY_HARD_MAX_DEGREE ||
        selected.max_steps == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(context, 0, sizeof *context);
    context->limits = selected;
    context->prime = prime;
    context->private_magic = PHY_FPOLY_CONTEXT_MAGIC;
    return PHY_OK;
}

void phy_fpoly_context_set_cancel(phy_fpoly_context *context,
                                  bool (*cancelled)(void *user), void *user)
{
    if (!context_valid(context)) {
        return;
    }
    context->cancelled = cancelled;
    context->cancel_user = user;
}

uint32_t phy_fpoly_context_steps(const phy_fpoly_context *context)
{
    return context_valid(context) ? context->steps : 0u;
}

phy_status phy_fpoly_init(phy_fpoly_context *context, phy_fpoly *polynomial)
{
    if (!context_valid(context) || polynomial == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    zero(context, polynomial);
    return PHY_OK;
}

phy_status phy_fpoly_validate(const phy_fpoly *polynomial)
{
    return polynomial_valid(polynomial) ? PHY_OK
                                        : PHY_ERR_CORRUPT_DOCUMENT;
}

phy_status phy_fpoly_set(phy_fpoly *polynomial,
                         const uint32_t *coefficients, size_t length)
{
    if (!polynomial_valid(polynomial) ||
        (coefficients == NULL && length != 0u)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (length > polynomial->context->limits.max_degree + 1u) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_status status = begin(polynomial->context);
    phy_fpoly result;
    zero(polynomial->context, &result);
    result.length = length;
    for (size_t index = 0u; status == PHY_OK && index < length; ++index) {
        status = step(polynomial->context, 1u);
        if (status == PHY_OK) {
            result.coefficients[index] =
                coefficients[index] % polynomial->context->prime;
        }
    }
    if (status == PHY_OK) {
        trim(&result);
        *polynomial = result;
    }
    return status;
}

phy_status phy_fpoly_copy(const phy_fpoly *source, phy_fpoly *destination)
{
    if (!polynomial_valid(source) || !polynomial_valid(destination) ||
        source->context != destination->context) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *destination = *source;
    return PHY_OK;
}

int phy_fpoly_degree(const phy_fpoly *polynomial)
{
    return polynomial_valid(polynomial) && polynomial->length > 0u
               ? (int)(polynomial->length - 1u)
               : -1;
}

uint32_t phy_fpoly_coefficient(const phy_fpoly *polynomial, size_t degree)
{
    return polynomial_valid(polynomial) && degree < polynomial->length
               ? polynomial->coefficients[degree]
               : 0u;
}

bool phy_fpoly_is_zero(const phy_fpoly *polynomial)
{
    return polynomial_valid(polynomial) && polynomial->length == 0u;
}

bool phy_fpoly_is_one(const phy_fpoly *polynomial)
{
    return polynomial_valid(polynomial) && polynomial->length == 1u &&
           polynomial->coefficients[0] == 1u;
}

bool phy_fpoly_equal(const phy_fpoly *left, const phy_fpoly *right)
{
    return polynomial_valid(left) && polynomial_valid(right) &&
           left->context == right->context &&
           left->length == right->length &&
           memcmp(left->coefficients, right->coefficients,
                  left->length * sizeof(uint32_t)) == 0;
}

phy_status phy_fpoly_add(const phy_fpoly *left, const phy_fpoly *right,
                         phy_fpoly *out_sum)
{
    if (!compatible(left, right, out_sum)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = begin(left->context);
    phy_fpoly result;
    zero(left->context, &result);
    result.length =
        left->length > right->length ? left->length : right->length;
    for (size_t index = 0u; status == PHY_OK && index < result.length;
         ++index) {
        status = step(left->context, 1u);
        if (status == PHY_OK) {
            result.coefficients[index] = add_mod(
                phy_fpoly_coefficient(left, index),
                phy_fpoly_coefficient(right, index), left->context->prime);
        }
    }
    if (status == PHY_OK) {
        trim(&result);
        *out_sum = result;
    }
    return status;
}

phy_status phy_fpoly_subtract(const phy_fpoly *left,
                              const phy_fpoly *right,
                              phy_fpoly *out_difference)
{
    if (!compatible(left, right, out_difference)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = begin(left->context);
    phy_fpoly result;
    zero(left->context, &result);
    result.length =
        left->length > right->length ? left->length : right->length;
    for (size_t index = 0u; status == PHY_OK && index < result.length;
         ++index) {
        status = step(left->context, 1u);
        if (status == PHY_OK) {
            result.coefficients[index] = subtract_mod(
                phy_fpoly_coefficient(left, index),
                phy_fpoly_coefficient(right, index), left->context->prime);
        }
    }
    if (status == PHY_OK) {
        trim(&result);
        *out_difference = result;
    }
    return status;
}

phy_status phy_fpoly_multiply(const phy_fpoly *left,
                              const phy_fpoly *right,
                              phy_fpoly *out_product)
{
    if (!compatible(left, right, out_product)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = begin(left->context);
    if (status == PHY_OK) {
        status = multiply_internal(left, right, out_product);
    }
    return status;
}

phy_status phy_fpoly_derivative(const phy_fpoly *polynomial,
                                phy_fpoly *out_derivative)
{
    if (!polynomial_valid(polynomial) ||
        !polynomial_valid(out_derivative) ||
        polynomial->context != out_derivative->context) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = begin(polynomial->context);
    phy_fpoly result;
    zero(polynomial->context, &result);
    if (polynomial->length > 1u) {
        result.length = polynomial->length - 1u;
    }
    for (size_t degree = 1u;
         status == PHY_OK && degree < polynomial->length; ++degree) {
        status = step(polynomial->context, 1u);
        if (status == PHY_OK) {
            result.coefficients[degree - 1u] = multiply_mod(
                polynomial->coefficients[degree],
                (uint32_t)(degree % polynomial->context->prime),
                polynomial->context->prime);
        }
    }
    if (status == PHY_OK) {
        trim(&result);
        *out_derivative = result;
    }
    return status;
}

phy_status phy_fpoly_divrem(const phy_fpoly *dividend,
                            const phy_fpoly *divisor,
                            phy_fpoly *out_quotient,
                            phy_fpoly *out_remainder)
{
    if (!compatible(dividend, divisor, out_quotient) ||
        !polynomial_valid(out_remainder) ||
        dividend->context != out_remainder->context ||
        out_quotient == out_remainder) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = begin(dividend->context);
    if (status == PHY_OK) {
        status = divrem_internal(
            dividend, divisor, out_quotient, out_remainder);
    }
    return status;
}

phy_status phy_fpoly_gcd(const phy_fpoly *left, const phy_fpoly *right,
                         phy_fpoly *out_gcd)
{
    if (!compatible(left, right, out_gcd)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = begin(left->context);
    if (status == PHY_OK) {
        status = gcd_internal(left, right, out_gcd);
    }
    return status;
}

phy_status phy_fpoly_powmod(const phy_fpoly *base, uint64_t exponent,
                            const phy_fpoly *modulus,
                            phy_fpoly *out_remainder)
{
    if (!compatible(base, modulus, out_remainder)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (modulus->length == 0u) {
        return PHY_ERR_DOMAIN;
    }
    phy_status status = begin(base->context);
    if (status == PHY_OK) {
        status = powmod_internal(base, exponent, modulus, out_remainder);
    }
    return status;
}

phy_status phy_fpoly_is_square_free(const phy_fpoly *polynomial,
                                    bool *out_square_free)
{
    if (!polynomial_valid(polynomial) || out_square_free == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = begin(polynomial->context);
    if (polynomial->length <= 2u) {
        *out_square_free = true;
        return status;
    }
    phy_fpoly derivative;
    phy_fpoly gcd;
    zero(polynomial->context, &derivative);
    zero(polynomial->context, &gcd);
    if (status == PHY_OK) {
        /*
         * Use the internal loop so one public square-free call owns one
         * cumulative budget rather than resetting it at each sub-operation.
         */
        derivative.length = polynomial->length - 1u;
        for (size_t degree = 1u;
             status == PHY_OK && degree < polynomial->length; ++degree) {
            status = step(polynomial->context, 1u);
            if (status == PHY_OK) {
                derivative.coefficients[degree - 1u] = multiply_mod(
                    polynomial->coefficients[degree],
                    (uint32_t)(degree % polynomial->context->prime),
                    polynomial->context->prime);
            }
        }
        trim(&derivative);
    }
    if (status == PHY_OK) {
        status = gcd_internal(polynomial, &derivative, &gcd);
    }
    if (status == PHY_OK) {
        *out_square_free = gcd.length <= 1u;
    }
    return status;
}

static bool factorization_valid(
    const phy_fpoly_factorization *factorization)
{
    if (factorization == NULL ||
        factorization->private_magic != PHY_FPOLY_FACTORIZATION_MAGIC ||
        !context_valid(factorization->context) ||
        factorization->count >
            factorization->context->limits.max_degree) {
        return false;
    }
    for (size_t index = 0u; index < factorization->count; ++index) {
        if (!polynomial_valid(&factorization->factors[index]) ||
            factorization->factors[index].context !=
                factorization->context) {
            return false;
        }
    }
    return true;
}

static void factorization_zero(
    phy_fpoly_context *context, phy_fpoly_factorization *factorization)
{
    factorization->context = context;
    factorization->count = 0u;
    for (size_t index = 0u;
         index < PHY_FPOLY_HARD_MAX_DEGREE; ++index) {
        zero(context, &factorization->factors[index]);
    }
    factorization->private_magic = PHY_FPOLY_FACTORIZATION_MAGIC;
}

phy_status phy_fpoly_factorization_init(
    phy_fpoly_context *context, phy_fpoly_factorization *factorization)
{
    if (!context_valid(context) || factorization == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    factorization_zero(context, factorization);
    return PHY_OK;
}

phy_status phy_fpoly_factorization_validate(
    const phy_fpoly_factorization *factorization)
{
    return factorization_valid(factorization)
               ? PHY_OK
               : PHY_ERR_CORRUPT_DOCUMENT;
}

static int polynomial_order(const phy_fpoly *left, const phy_fpoly *right)
{
    if (left->length != right->length) {
        return left->length < right->length ? -1 : 1;
    }
    for (size_t index = left->length; index-- != 0u;) {
        if (left->coefficients[index] != right->coefficients[index]) {
            return left->coefficients[index] <
                           right->coefficients[index]
                       ? -1
                       : 1;
        }
    }
    return 0;
}

static void sort_factors(phy_fpoly_factorization *factorization)
{
    for (size_t index = 1u; index < factorization->count; ++index) {
        const phy_fpoly key = factorization->factors[index];
        size_t position = index;
        while (position > 0u &&
               polynomial_order(
                   &key, &factorization->factors[position - 1u]) < 0) {
            factorization->factors[position] =
                factorization->factors[position - 1u];
            position--;
        }
        factorization->factors[position] = key;
    }
}

static phy_status berlekamp_rref(
    const phy_fpoly *polynomial,
    uint32_t matrix[PHY_FPOLY_HARD_MAX_DEGREE]
                   [PHY_FPOLY_HARD_MAX_DEGREE],
    size_t pivot_columns[PHY_FPOLY_HARD_MAX_DEGREE],
    bool is_pivot[PHY_FPOLY_HARD_MAX_DEGREE],
    size_t *out_rank)
{
    const size_t degree = polynomial->length - 1u;
    memset(matrix, 0,
           PHY_FPOLY_HARD_MAX_DEGREE *
               PHY_FPOLY_HARD_MAX_DEGREE * sizeof(uint32_t));
    memset(is_pivot, 0,
           PHY_FPOLY_HARD_MAX_DEGREE * sizeof(bool));

    phy_fpoly x;
    zero(polynomial->context, &x);
    x.length = 2u;
    x.coefficients[1] = 1u;
    for (size_t column = 0u; column < degree; ++column) {
        phy_fpoly power;
        zero(polynomial->context, &power);
        const uint64_t exponent =
            (uint64_t)polynomial->context->prime * (uint64_t)column;
        phy_status status =
            powmod_internal(&x, exponent, polynomial, &power);
        if (status != PHY_OK) {
            return status;
        }
        for (size_t row = 0u; row < degree; ++row) {
            matrix[row][column] =
                phy_fpoly_coefficient(&power, row);
        }
        matrix[column][column] = subtract_mod(
            matrix[column][column], 1u,
            polynomial->context->prime);
    }

    size_t rank = 0u;
    for (size_t column = 0u; column < degree && rank < degree; ++column) {
        size_t pivot = rank;
        while (pivot < degree && matrix[pivot][column] == 0u) {
            pivot++;
        }
        if (pivot == degree) {
            continue;
        }
        if (pivot != rank) {
            for (size_t entry = 0u; entry < degree; ++entry) {
                const uint32_t swap = matrix[rank][entry];
                matrix[rank][entry] = matrix[pivot][entry];
                matrix[pivot][entry] = swap;
            }
        }
        const uint32_t inverse = inverse_mod(
            matrix[rank][column], polynomial->context->prime);
        for (size_t entry = column; entry < degree; ++entry) {
            phy_status status = step(polynomial->context, 1u);
            if (status != PHY_OK) {
                return status;
            }
            matrix[rank][entry] = multiply_mod(
                matrix[rank][entry], inverse,
                polynomial->context->prime);
        }
        for (size_t row = 0u; row < degree; ++row) {
            if (row == rank || matrix[row][column] == 0u) {
                continue;
            }
            const uint32_t factor = matrix[row][column];
            for (size_t entry = column; entry < degree; ++entry) {
                phy_status status = step(polynomial->context, 1u);
                if (status != PHY_OK) {
                    return status;
                }
                const uint32_t product = multiply_mod(
                    factor, matrix[rank][entry],
                    polynomial->context->prime);
                matrix[row][entry] = subtract_mod(
                    matrix[row][entry], product,
                    polynomial->context->prime);
            }
        }
        pivot_columns[rank] = column;
        is_pivot[column] = true;
        rank++;
    }
    *out_rank = rank;
    return PHY_OK;
}

static phy_status split_with_basis(
    phy_fpoly_factorization *factorization, const phy_fpoly *basis,
    size_t target_count)
{
    phy_fpoly_context *context = factorization->context;
    for (uint32_t value = 0u;
         value < context->prime && factorization->count < target_count;
         ++value) {
        size_t current_count = factorization->count;
        for (size_t index = 0u;
             index < current_count &&
             factorization->count < target_count; ++index) {
            phy_fpoly *candidate = &factorization->factors[index];
            if (candidate->length <= 2u) {
                continue;
            }
            phy_fpoly shifted = *basis;
            if (shifted.length == 0u) {
                shifted.length = 1u;
            }
            shifted.coefficients[0] = subtract_mod(
                shifted.coefficients[0], value, context->prime);
            trim(&shifted);

            phy_fpoly divisor;
            zero(context, &divisor);
            phy_status status =
                gcd_internal(candidate, &shifted, &divisor);
            if (status != PHY_OK) {
                return status;
            }
            if (divisor.length <= 1u ||
                divisor.length == candidate->length) {
                continue;
            }
            if (factorization->count >= context->limits.max_degree ||
                factorization->count >= PHY_FPOLY_HARD_MAX_DEGREE) {
                return PHY_ERR_TERM_LIMIT;
            }
            phy_fpoly quotient;
            phy_fpoly remainder;
            zero(context, &quotient);
            zero(context, &remainder);
            status = divrem_internal(
                candidate, &divisor, &quotient, &remainder);
            if (status != PHY_OK) {
                return status;
            }
            if (remainder.length != 0u) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
            status = make_monic(&quotient);
            if (status != PHY_OK) {
                return status;
            }
            *candidate = divisor;
            factorization->factors[factorization->count++] = quotient;
        }
    }
    return PHY_OK;
}

phy_status phy_fpoly_factor_square_free(
    const phy_fpoly *polynomial, phy_fpoly_factorization *out_factors)
{
    if (!polynomial_valid(polynomial) ||
        !factorization_valid(out_factors) ||
        polynomial->context != out_factors->context) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (polynomial->length < 2u ||
        polynomial->coefficients[polynomial->length - 1u] != 1u) {
        return PHY_ERR_DOMAIN;
    }
    phy_status status = begin(polynomial->context);

    phy_fpoly derivative;
    phy_fpoly common;
    zero(polynomial->context, &derivative);
    zero(polynomial->context, &common);
    derivative.length = polynomial->length - 1u;
    for (size_t degree = 1u;
         status == PHY_OK && degree < polynomial->length; ++degree) {
        status = step(polynomial->context, 1u);
        if (status == PHY_OK) {
            derivative.coefficients[degree - 1u] = multiply_mod(
                polynomial->coefficients[degree],
                (uint32_t)(degree % polynomial->context->prime),
                polynomial->context->prime);
        }
    }
    trim(&derivative);
    if (status == PHY_OK) {
        status = gcd_internal(polynomial, &derivative, &common);
    }
    if (status != PHY_OK) {
        return status;
    }
    if (common.length > 1u) {
        return PHY_ERR_DOMAIN;
    }

    uint32_t matrix[PHY_FPOLY_HARD_MAX_DEGREE]
                   [PHY_FPOLY_HARD_MAX_DEGREE];
    size_t pivot_columns[PHY_FPOLY_HARD_MAX_DEGREE];
    bool is_pivot[PHY_FPOLY_HARD_MAX_DEGREE];
    size_t rank = 0u;
    status = berlekamp_rref(
        polynomial, matrix, pivot_columns, is_pivot, &rank);
    if (status != PHY_OK) {
        return status;
    }
    const size_t degree = polynomial->length - 1u;
    const size_t nullity = degree - rank;
    if (nullity == 0u || nullity > degree) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }

    phy_fpoly_factorization result;
    factorization_zero(polynomial->context, &result);
    result.count = 1u;
    result.factors[0] = *polynomial;

    for (size_t free_column = 0u;
         free_column < degree && result.count < nullity; ++free_column) {
        if (is_pivot[free_column]) {
            continue;
        }
        phy_fpoly basis;
        zero(polynomial->context, &basis);
        basis.length = degree;
        basis.coefficients[free_column] = 1u;
        for (size_t row = 0u; row < rank; ++row) {
            const size_t pivot = pivot_columns[row];
            basis.coefficients[pivot] =
                matrix[row][free_column] == 0u
                    ? 0u
                    : polynomial->context->prime -
                          matrix[row][free_column];
        }
        trim(&basis);
        status = split_with_basis(&result, &basis, nullity);
        if (status != PHY_OK) {
            return status;
        }
    }
    if (result.count != nullity) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    sort_factors(&result);

    phy_fpoly product;
    zero(polynomial->context, &product);
    product.length = 1u;
    product.coefficients[0] = 1u;
    for (size_t index = 0u; index < result.count; ++index) {
        status = multiply_internal(
            &product, &result.factors[index], &product);
        if (status != PHY_OK) {
            return status;
        }
    }
    if (!phy_fpoly_equal(&product, polynomial)) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    *out_factors = result;
    return PHY_OK;
}
