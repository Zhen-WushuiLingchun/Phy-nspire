/*
 * Exact Gaussian rationals Q(i) over the native bounded bigrat kernel.
 *
 * The public object owns no separate heap allocation: its two rational parts
 * are ordinary context-registered handles. Arithmetic computes into fresh
 * temporaries and swaps both parts only after the complete result exists.
 */
#include <string.h>

#include "exact_internal.h"
#include "phy/exact.h"

#define PHY_GAUSSIAN_MAGIC 0x47415553u /* GAUS */

static phy_exact_context *gaussian_context(const phy_gaussian *value)
{
    if (value == NULL || value->private_magic != PHY_GAUSSIAN_MAGIC ||
        phy_bigrat_validate(&value->real) != PHY_OK ||
        phy_bigrat_validate(&value->imaginary) != PHY_OK ||
        value->real.numerator.context !=
            value->imaginary.numerator.context) {
        return NULL;
    }
    return value->real.numerator.context;
}

static bool gaussian_compatible(const phy_gaussian *left,
                                const phy_gaussian *right,
                                const phy_gaussian *output)
{
    phy_exact_context *context = gaussian_context(left);
    return context != NULL && gaussian_context(right) == context &&
           gaussian_context(output) == context;
}

phy_status phy_gaussian_init(phy_exact_context *context,
                             phy_gaussian *value)
{
    if (!phy_exact_context_is_valid(context) || value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(value, 0, sizeof *value);
    phy_status status = phy_bigrat_init(context, &value->real);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &value->imaginary);
    }
    if (status != PHY_OK) {
        phy_bigrat_destroy(&value->imaginary);
        phy_bigrat_destroy(&value->real);
        memset(value, 0, sizeof *value);
        return status;
    }
    value->private_magic = PHY_GAUSSIAN_MAGIC;
    return PHY_OK;
}

void phy_gaussian_destroy(phy_gaussian *value)
{
    if (value == NULL || value->private_magic != PHY_GAUSSIAN_MAGIC) {
        return;
    }
    phy_bigrat_destroy(&value->imaginary);
    phy_bigrat_destroy(&value->real);
    memset(value, 0, sizeof *value);
}

phy_status phy_gaussian_validate(const phy_gaussian *value)
{
    return gaussian_context(value) != NULL ? PHY_OK
                                           : PHY_ERR_CORRUPT_DOCUMENT;
}

static phy_status gaussian_commit(phy_gaussian *temporary,
                                  phy_gaussian *output)
{
    phy_status status =
        phy_bigrat_swap(&temporary->real, &output->real);
    if (status == PHY_OK) {
        status = phy_bigrat_swap(
            &temporary->imaginary, &output->imaginary);
        if (status != PHY_OK) {
            const phy_status rollback =
                phy_bigrat_swap(&temporary->real, &output->real);
            if (rollback != PHY_OK) {
                return PHY_ERR_CORRUPT_DOCUMENT;
            }
        }
    }
    return status;
}

typedef phy_status (*gaussian_fill)(phy_gaussian *temporary,
                                    const void *user);

static phy_status gaussian_transaction(phy_gaussian *output,
                                       gaussian_fill fill,
                                       const void *user)
{
    phy_exact_context *context = gaussian_context(output);
    if (context == NULL || fill == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_exact_operation_begin(context);
    phy_gaussian temporary;
    memset(&temporary, 0, sizeof temporary);
    if (status == PHY_OK) {
        status = phy_gaussian_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = fill(&temporary, user);
    }
    if (status == PHY_OK) {
        status = gaussian_commit(&temporary, output);
    }
    phy_gaussian_destroy(&temporary);
    return phy_exact_operation_end(context, status);
}

typedef struct {
    int64_t real_numerator;
    int64_t real_denominator;
    int64_t imaginary_numerator;
    int64_t imaginary_denominator;
} gaussian_i64_parts;

static phy_status fill_i64(phy_gaussian *temporary, const void *user)
{
    const gaussian_i64_parts *parts =
        (const gaussian_i64_parts *)user;
    phy_status status = phy_bigrat_set_i64(
        &temporary->real, parts->real_numerator,
        parts->real_denominator);
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(
            &temporary->imaginary, parts->imaginary_numerator,
            parts->imaginary_denominator);
    }
    return status;
}

phy_status phy_gaussian_set_i64(
    phy_gaussian *value, int64_t real_numerator,
    int64_t real_denominator, int64_t imaginary_numerator,
    int64_t imaginary_denominator)
{
    const gaussian_i64_parts parts = {
        real_numerator, real_denominator,
        imaginary_numerator, imaginary_denominator};
    return gaussian_transaction(value, fill_i64, &parts);
}

typedef struct {
    const char *real_numerator;
    const char *real_denominator;
    const char *imaginary_numerator;
    const char *imaginary_denominator;
} gaussian_text_parts;

static phy_status fill_text(phy_gaussian *temporary, const void *user)
{
    const gaussian_text_parts *parts =
        (const gaussian_text_parts *)user;
    phy_status status = phy_bigrat_read(
        &temporary->real, parts->real_numerator,
        parts->real_denominator);
    if (status == PHY_OK) {
        status = phy_bigrat_read(
            &temporary->imaginary, parts->imaginary_numerator,
            parts->imaginary_denominator);
    }
    return status;
}

phy_status phy_gaussian_read(
    phy_gaussian *value, const char *real_numerator,
    const char *real_denominator, const char *imaginary_numerator,
    const char *imaginary_denominator)
{
    if (real_numerator == NULL || real_denominator == NULL ||
        imaginary_numerator == NULL || imaginary_denominator == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const gaussian_text_parts parts = {
        real_numerator, real_denominator,
        imaginary_numerator, imaginary_denominator};
    return gaussian_transaction(value, fill_text, &parts);
}

static phy_status fill_copy(phy_gaussian *temporary, const void *user)
{
    const phy_gaussian *source = (const phy_gaussian *)user;
    phy_status status =
        phy_bigrat_copy(&source->real, &temporary->real);
    if (status == PHY_OK) {
        status = phy_bigrat_copy(
            &source->imaginary, &temporary->imaginary);
    }
    return status;
}

phy_status phy_gaussian_copy(const phy_gaussian *source,
                             phy_gaussian *destination)
{
    if (!gaussian_compatible(source, source, destination)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return gaussian_transaction(
        destination, fill_copy, source);
}

typedef struct {
    const phy_gaussian *left;
    const phy_gaussian *right;
} gaussian_binary_inputs;

static phy_status fill_add(phy_gaussian *temporary, const void *user)
{
    const gaussian_binary_inputs *inputs =
        (const gaussian_binary_inputs *)user;
    phy_status status = phy_bigrat_add(
        &inputs->left->real, &inputs->right->real,
        &temporary->real);
    if (status == PHY_OK) {
        status = phy_bigrat_add(
            &inputs->left->imaginary, &inputs->right->imaginary,
            &temporary->imaginary);
    }
    return status;
}

phy_status phy_gaussian_add(const phy_gaussian *left,
                            const phy_gaussian *right,
                            phy_gaussian *out_sum)
{
    if (!gaussian_compatible(left, right, out_sum)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const gaussian_binary_inputs inputs = {left, right};
    return gaussian_transaction(out_sum, fill_add, &inputs);
}

static phy_status fill_negate(phy_gaussian *temporary, const void *user)
{
    const phy_gaussian *value = (const phy_gaussian *)user;
    phy_status status =
        phy_bigrat_negate(&value->real, &temporary->real);
    if (status == PHY_OK) {
        status = phy_bigrat_negate(
            &value->imaginary, &temporary->imaginary);
    }
    return status;
}

phy_status phy_gaussian_negate(const phy_gaussian *value,
                               phy_gaussian *out_negated)
{
    if (!gaussian_compatible(value, value, out_negated)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return gaussian_transaction(
        out_negated, fill_negate, value);
}

static phy_status fill_subtract(phy_gaussian *temporary,
                                const void *user)
{
    const gaussian_binary_inputs *inputs =
        (const gaussian_binary_inputs *)user;
    phy_status status = phy_bigrat_subtract(
        &inputs->left->real, &inputs->right->real,
        &temporary->real);
    if (status == PHY_OK) {
        status = phy_bigrat_subtract(
            &inputs->left->imaginary, &inputs->right->imaginary,
            &temporary->imaginary);
    }
    return status;
}

phy_status phy_gaussian_subtract(const phy_gaussian *left,
                                 const phy_gaussian *right,
                                 phy_gaussian *out_difference)
{
    if (!gaussian_compatible(left, right, out_difference)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const gaussian_binary_inputs inputs = {left, right};
    return gaussian_transaction(
        out_difference, fill_subtract, &inputs);
}

static phy_status fill_multiply(phy_gaussian *temporary,
                                const void *user)
{
    const gaussian_binary_inputs *inputs =
        (const gaussian_binary_inputs *)user;
    phy_exact_context *context =
        temporary->real.numerator.context;
    phy_bigrat ac;
    phy_bigrat bd;
    phy_bigrat ad;
    phy_bigrat bc;
    memset(&ac, 0, sizeof ac);
    memset(&bd, 0, sizeof bd);
    memset(&ad, 0, sizeof ad);
    memset(&bc, 0, sizeof bc);
    phy_status status = phy_bigrat_init(context, &ac);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &bd);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &ad);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &bc);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &inputs->left->real, &inputs->right->real, &ac);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &inputs->left->imaginary,
            &inputs->right->imaginary, &bd);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &inputs->left->real,
            &inputs->right->imaginary, &ad);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &inputs->left->imaginary,
            &inputs->right->real, &bc);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_subtract(
            &ac, &bd, &temporary->real);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(
            &ad, &bc, &temporary->imaginary);
    }
    phy_bigrat_destroy(&bc);
    phy_bigrat_destroy(&ad);
    phy_bigrat_destroy(&bd);
    phy_bigrat_destroy(&ac);
    return status;
}

phy_status phy_gaussian_multiply(const phy_gaussian *left,
                                 const phy_gaussian *right,
                                 phy_gaussian *out_product)
{
    if (!gaussian_compatible(left, right, out_product)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const gaussian_binary_inputs inputs = {left, right};
    return gaussian_transaction(
        out_product, fill_multiply, &inputs);
}

static phy_status norm_into(const phy_gaussian *value,
                            phy_bigrat *out_norm)
{
    phy_exact_context *context = value->real.numerator.context;
    phy_bigrat real_square;
    phy_bigrat imaginary_square;
    phy_bigrat norm;
    memset(&real_square, 0, sizeof real_square);
    memset(&imaginary_square, 0, sizeof imaginary_square);
    memset(&norm, 0, sizeof norm);
    phy_status status = phy_bigrat_init(context, &real_square);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &imaginary_square);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &norm);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &value->real, &value->real, &real_square);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(
            &value->imaginary, &value->imaginary,
            &imaginary_square);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(
            &real_square, &imaginary_square, &norm);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_swap(&norm, out_norm);
    }
    phy_bigrat_destroy(&norm);
    phy_bigrat_destroy(&imaginary_square);
    phy_bigrat_destroy(&real_square);
    return status;
}

phy_status phy_gaussian_norm(const phy_gaussian *value,
                             phy_bigrat *out_norm)
{
    phy_exact_context *context = gaussian_context(value);
    if (context == NULL || phy_bigrat_validate(out_norm) != PHY_OK ||
        out_norm->numerator.context != context) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_exact_operation_begin(context);
    if (status == PHY_OK) {
        status = norm_into(value, out_norm);
    }
    return phy_exact_operation_end(context, status);
}

static phy_status fill_reciprocal(phy_gaussian *temporary,
                                  const void *user)
{
    const phy_gaussian *value = (const phy_gaussian *)user;
    phy_exact_context *context =
        temporary->real.numerator.context;
    phy_bigrat norm;
    phy_bigrat negative_imaginary;
    memset(&norm, 0, sizeof norm);
    memset(&negative_imaginary, 0, sizeof negative_imaginary);
    phy_status status = phy_bigrat_init(context, &norm);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &negative_imaginary);
    }
    if (status == PHY_OK) {
        status = norm_into(value, &norm);
    }
    if (status == PHY_OK && phy_bigrat_sign(&norm) == 0) {
        status = PHY_ERR_DOMAIN;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_negate(
            &value->imaginary, &negative_imaginary);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(
            &value->real, &norm, &temporary->real);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(
            &negative_imaginary, &norm,
            &temporary->imaginary);
    }
    phy_bigrat_destroy(&negative_imaginary);
    phy_bigrat_destroy(&norm);
    return status;
}

phy_status phy_gaussian_reciprocal(const phy_gaussian *value,
                                   phy_gaussian *out_reciprocal)
{
    if (!gaussian_compatible(value, value, out_reciprocal)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return gaussian_transaction(
        out_reciprocal, fill_reciprocal, value);
}

phy_status phy_gaussian_divide(const phy_gaussian *dividend,
                               const phy_gaussian *divisor,
                               phy_gaussian *out_quotient)
{
    if (!gaussian_compatible(dividend, divisor, out_quotient)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context =
        dividend->real.numerator.context;
    phy_status status = phy_exact_operation_begin(context);
    phy_gaussian reciprocal;
    memset(&reciprocal, 0, sizeof reciprocal);
    if (status == PHY_OK) {
        status = phy_gaussian_init(context, &reciprocal);
    }
    if (status == PHY_OK) {
        status = phy_gaussian_reciprocal(divisor, &reciprocal);
    }
    if (status == PHY_OK) {
        status = phy_gaussian_multiply(
            dividend, &reciprocal, out_quotient);
    }
    phy_gaussian_destroy(&reciprocal);
    return phy_exact_operation_end(context, status);
}

static phy_status fill_conjugate(phy_gaussian *temporary,
                                 const void *user)
{
    const phy_gaussian *value = (const phy_gaussian *)user;
    phy_status status =
        phy_bigrat_copy(&value->real, &temporary->real);
    if (status == PHY_OK) {
        status = phy_bigrat_negate(
            &value->imaginary, &temporary->imaginary);
    }
    return status;
}

phy_status phy_gaussian_conjugate(const phy_gaussian *value,
                                  phy_gaussian *out_conjugate)
{
    if (!gaussian_compatible(value, value, out_conjugate)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return gaussian_transaction(
        out_conjugate, fill_conjugate, value);
}

phy_status phy_gaussian_pow_i32(const phy_gaussian *base, int32_t exponent,
                                phy_gaussian *out_power)
{
    if (!gaussian_compatible(base, base, out_power)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = base->real.numerator.context;
    phy_status status = phy_exact_operation_begin(context);
    phy_gaussian result;
    phy_gaussian factor;
    memset(&result, 0, sizeof result);
    memset(&factor, 0, sizeof factor);
    if (status == PHY_OK) {
        status = phy_gaussian_init(context, &result);
    }
    if (status == PHY_OK) {
        status = phy_gaussian_init(context, &factor);
    }
    if (status == PHY_OK) {
        status = phy_gaussian_set_i64(&result, 1, 1, 0, 1);
    }
    if (status == PHY_OK && exponent < 0) {
        status = phy_gaussian_reciprocal(base, &factor);
    } else if (status == PHY_OK) {
        status = phy_gaussian_copy(base, &factor);
    }
    uint32_t remaining =
        exponent < 0 ? (uint32_t)(-(int64_t)exponent)
                     : (uint32_t)exponent;
    while (status == PHY_OK && remaining != 0u) {
        if ((remaining & 1u) != 0u) {
            status = phy_gaussian_multiply(
                &result, &factor, &result);
        }
        remaining >>= 1u;
        if (status == PHY_OK && remaining != 0u) {
            status = phy_gaussian_multiply(
                &factor, &factor, &factor);
        }
    }
    if (status == PHY_OK) {
        status = gaussian_commit(&result, out_power);
    }
    phy_gaussian_destroy(&factor);
    phy_gaussian_destroy(&result);
    return phy_exact_operation_end(context, status);
}

const phy_bigrat *phy_gaussian_real(const phy_gaussian *value)
{
    return gaussian_context(value) != NULL ? &value->real : NULL;
}

const phy_bigrat *phy_gaussian_imaginary(const phy_gaussian *value)
{
    return gaussian_context(value) != NULL
               ? &value->imaginary
               : NULL;
}
