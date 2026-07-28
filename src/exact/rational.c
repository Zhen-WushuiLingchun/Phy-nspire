/*
 * Canonical arbitrary-precision rationals over the native bigint kernel.
 *
 * Every public constructor builds a temporary, normalizes it, and swaps only
 * after success. The denominator is positive; zero is always 0/1.
 */
#include "exact_internal.h"

static bool rational_valid_shallow(const phy_bigrat *value)
{
    return value != NULL && value->private_magic == PHY_BIGRAT_MAGIC &&
           phy_bigint_shallow_valid(&value->numerator) &&
           phy_bigint_shallow_valid(&value->denominator) &&
           value->numerator.context == value->denominator.context &&
           value->denominator.sign > 0 &&
           !(value->numerator.count == 0u &&
             !(value->denominator.count == 1u &&
               value->denominator.limbs[0] == 1u));
}

static phy_exact_context *rational_context(const phy_bigrat *value)
{
    return rational_valid_shallow(value) ? value->numerator.context : NULL;
}

static bool compatible2(const phy_bigrat *left, const phy_bigrat *right)
{
    phy_exact_context *left_context = rational_context(left);
    return left_context != NULL && rational_context(right) == left_context;
}

static bool compatible3(const phy_bigrat *left, const phy_bigrat *right,
                        const phy_bigrat *output)
{
    return compatible2(left, right) &&
           rational_context(output) == rational_context(left);
}

static void swap_payload(phy_bigrat *left, phy_bigrat *right)
{
    phy_bigint_swap_payload(&left->numerator, &right->numerator);
    phy_bigint_swap_payload(&left->denominator, &right->denominator);
}

static phy_status commit(phy_bigrat *destination, phy_bigrat *temporary,
                         phy_status status)
{
    if (status == PHY_OK) {
        swap_payload(destination, temporary);
    }
    phy_bigrat_destroy(temporary);
    return status;
}

phy_status phy_bigrat_init(phy_exact_context *context, phy_bigrat *value)
{
    if (!phy_exact_context_is_valid(context) || value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(value, 0, sizeof *value);
    phy_status status = phy_bigint_init(context, &value->numerator);
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &value->denominator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_assign_u64(&value->denominator, 1u, 1);
    }
    if (status != PHY_OK) {
        if (phy_bigint_is_initialized(&value->denominator)) {
            phy_bigint_destroy(&value->denominator);
        }
        if (phy_bigint_is_initialized(&value->numerator)) {
            phy_bigint_destroy(&value->numerator);
        }
        memset(value, 0, sizeof *value);
        return status;
    }
    value->private_magic = PHY_BIGRAT_MAGIC;
    return PHY_OK;
}

void phy_bigrat_destroy(phy_bigrat *value)
{
    if (value == NULL || value->private_magic != PHY_BIGRAT_MAGIC) {
        return;
    }
    phy_bigint_destroy(&value->denominator);
    phy_bigint_destroy(&value->numerator);
    memset(value, 0, sizeof *value);
}

phy_status phy_bigrat_validate(const phy_bigrat *value)
{
    return rational_valid_shallow(value) ? PHY_OK
                                         : PHY_ERR_CORRUPT_DOCUMENT;
}

static phy_status normalize_in_place(phy_bigrat *value)
{
    if (!phy_bigint_shallow_valid(&value->numerator) ||
        !phy_bigint_shallow_valid(&value->denominator) ||
        value->numerator.context != value->denominator.context) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (value->denominator.count == 0u) {
        return PHY_ERR_DOMAIN;
    }
    phy_status status = PHY_OK;
    if (value->denominator.sign < 0) {
        status = phy_bigint_negate(
            &value->denominator, &value->denominator);
        if (status == PHY_OK) {
            status =
                phy_bigint_negate(&value->numerator, &value->numerator);
        }
    }
    if (status != PHY_OK) {
        return status;
    }
    if (value->numerator.count == 0u) {
        return phy_bigint_set_i64(&value->denominator, 1);
    }

    phy_exact_context *context = value->numerator.context;
    phy_bigint gcd = {0};
    phy_bigint reduced_numerator = {0};
    phy_bigint reduced_denominator = {0};
    status = phy_bigint_init(context, &gcd);
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &reduced_numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &reduced_denominator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_gcd(
            &value->numerator, &value->denominator, &gcd);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divide_exact(
            &value->numerator, &gcd, &reduced_numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divide_exact(
            &value->denominator, &gcd, &reduced_denominator);
    }
    if (status == PHY_OK) {
        phy_bigint_swap_payload(
            &value->numerator, &reduced_numerator);
        phy_bigint_swap_payload(
            &value->denominator, &reduced_denominator);
    }
    if (phy_bigint_is_initialized(&reduced_denominator)) {
        phy_bigint_destroy(&reduced_denominator);
    }
    if (phy_bigint_is_initialized(&reduced_numerator)) {
        phy_bigint_destroy(&reduced_numerator);
    }
    if (phy_bigint_is_initialized(&gcd)) {
        phy_bigint_destroy(&gcd);
    }
    return status;
}

phy_status phy_bigrat_set_i64(phy_bigrat *value, int64_t numerator,
                              int64_t denominator)
{
    phy_exact_context *context = rational_context(value);
    if (context == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_exact_operation_begin(context);
    phy_bigrat temporary;
    memset(&temporary, 0, sizeof temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_bigint_set_i64(&temporary.numerator, numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_set_i64(&temporary.denominator, denominator);
    }
    if (status == PHY_OK) {
        status = normalize_in_place(&temporary);
    }
    if (status == PHY_OK) {
        status = commit(value, &temporary, status);
    } else if (temporary.private_magic == PHY_BIGRAT_MAGIC) {
        phy_bigrat_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigrat_read(phy_bigrat *value, const char *numerator,
                           const char *denominator)
{
    phy_exact_context *context = rational_context(value);
    if (context == NULL || numerator == NULL || denominator == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_exact_operation_begin(context);
    phy_bigrat temporary;
    memset(&temporary, 0, sizeof temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_bigint_read(&temporary.numerator, numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_read(&temporary.denominator, denominator);
    }
    if (status == PHY_OK) {
        status = normalize_in_place(&temporary);
    }
    if (status == PHY_OK) {
        status = commit(value, &temporary, status);
    } else if (temporary.private_magic == PHY_BIGRAT_MAGIC) {
        phy_bigrat_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigrat_copy(const phy_bigrat *source,
                           phy_bigrat *destination)
{
    if (!compatible2(source, destination)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = rational_context(source);
    phy_status status = phy_exact_operation_begin(context);
    if (source == destination) {
        return phy_exact_operation_end(context, PHY_OK);
    }
    phy_bigrat temporary;
    memset(&temporary, 0, sizeof temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_bigint_copy(
            &source->numerator, &temporary.numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_copy(
            &source->denominator, &temporary.denominator);
    }
    if (status == PHY_OK) {
        status = commit(destination, &temporary, status);
    } else if (temporary.private_magic == PHY_BIGRAT_MAGIC) {
        phy_bigrat_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigrat_write(const phy_bigrat *value, char *buffer,
                            size_t capacity, size_t *out_required)
{
    phy_exact_context *context = rational_context(value);
    if (context == NULL || out_required == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_exact_operation_begin(context);
    size_t numerator_required = 0u;
    size_t denominator_required = 0u;
    if (status == PHY_OK) {
        status = phy_bigint_write(
            &value->numerator, NULL, 0u, &numerator_required);
    }
    const bool integer =
        value->denominator.count == 1u &&
        value->denominator.limbs[0] == 1u;
    if (status == PHY_OK && !integer) {
        status = phy_bigint_write(
            &value->denominator, NULL, 0u, &denominator_required);
    }
    size_t required = 0u;
    if (status == PHY_OK) {
        if (integer) {
            required = numerator_required;
        } else if (numerator_required >
                   (size_t)-1 - denominator_required) {
            status = PHY_ERR_MEMORY_LIMIT;
        } else {
            required = numerator_required + denominator_required;
        }
    }
    if (status == PHY_OK) {
        *out_required = required;
        if (buffer != NULL && capacity < required) {
            status = PHY_ERR_MEMORY_LIMIT;
        }
    }
    char *temporary = NULL;
    if (status == PHY_OK && buffer != NULL) {
        status = phy_exact_temp_alloc(
            context, required, (void **)&temporary);
    }
    if (status == PHY_OK && temporary != NULL) {
        size_t ignored = 0u;
        status = phy_bigint_write(
            &value->numerator, temporary, required, &ignored);
        if (status == PHY_OK && !integer) {
            temporary[numerator_required - 1u] = '/';
            status = phy_bigint_write(
                &value->denominator,
                temporary + numerator_required,
                required - numerator_required, &ignored);
        }
        if (status == PHY_OK) {
            memcpy(buffer, temporary, required);
        }
    }
    if (temporary != NULL) {
        phy_exact_temp_free(context, temporary, required);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigrat_add(const phy_bigrat *left, const phy_bigrat *right,
                          phy_bigrat *out_sum)
{
    if (!compatible3(left, right, out_sum)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = rational_context(left);
    phy_status status = phy_exact_operation_begin(context);
    phy_bigrat temporary;
    phy_bigint gcd = {0};
    phy_bigint left_scale = {0};
    phy_bigint right_scale = {0};
    phy_bigint left_term = {0};
    phy_bigint right_term = {0};
    memset(&temporary, 0, sizeof temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &gcd);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &left_scale);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &right_scale);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &left_term);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &right_term);
    }
    if (status == PHY_OK) {
        status = phy_bigint_gcd(
            &left->denominator, &right->denominator, &gcd);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divide_exact(
            &right->denominator, &gcd, &left_scale);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divide_exact(
            &left->denominator, &gcd, &right_scale);
    }
    if (status == PHY_OK) {
        status = phy_bigint_multiply(
            &left->numerator, &left_scale, &left_term);
    }
    if (status == PHY_OK) {
        status = phy_bigint_multiply(
            &right->numerator, &right_scale, &right_term);
    }
    if (status == PHY_OK) {
        status = phy_bigint_add(
            &left_term, &right_term, &temporary.numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_multiply(
            &left->denominator, &left_scale,
            &temporary.denominator);
    }
    if (status == PHY_OK) {
        status = normalize_in_place(&temporary);
    }
    if (status == PHY_OK) {
        status = commit(out_sum, &temporary, status);
    } else if (temporary.private_magic == PHY_BIGRAT_MAGIC) {
        phy_bigrat_destroy(&temporary);
    }
    phy_bigint_destroy(&right_term);
    phy_bigint_destroy(&left_term);
    phy_bigint_destroy(&right_scale);
    phy_bigint_destroy(&left_scale);
    phy_bigint_destroy(&gcd);
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigrat_multiply(const phy_bigrat *left,
                               const phy_bigrat *right,
                               phy_bigrat *out_product)
{
    if (!compatible3(left, right, out_product)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = rational_context(left);
    phy_status status = phy_exact_operation_begin(context);
    phy_bigrat temporary;
    phy_bigint gcd_left = {0};
    phy_bigint gcd_right = {0};
    phy_bigint left_numerator = {0};
    phy_bigint right_numerator = {0};
    phy_bigint left_denominator = {0};
    phy_bigint right_denominator = {0};
    memset(&temporary, 0, sizeof temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &gcd_left);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &gcd_right);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &left_numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &right_numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &left_denominator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &right_denominator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_gcd(
            &left->numerator, &right->denominator, &gcd_left);
    }
    if (status == PHY_OK) {
        status = phy_bigint_gcd(
            &right->numerator, &left->denominator, &gcd_right);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divide_exact(
            &left->numerator, &gcd_left, &left_numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divide_exact(
            &right->denominator, &gcd_left, &right_denominator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divide_exact(
            &right->numerator, &gcd_right, &right_numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divide_exact(
            &left->denominator, &gcd_right, &left_denominator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_multiply(
            &left_numerator, &right_numerator, &temporary.numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_multiply(
            &left_denominator, &right_denominator,
            &temporary.denominator);
    }
    if (status == PHY_OK) {
        status = normalize_in_place(&temporary);
    }
    if (status == PHY_OK) {
        status = commit(out_product, &temporary, status);
    } else if (temporary.private_magic == PHY_BIGRAT_MAGIC) {
        phy_bigrat_destroy(&temporary);
    }
    phy_bigint_destroy(&right_denominator);
    phy_bigint_destroy(&left_denominator);
    phy_bigint_destroy(&right_numerator);
    phy_bigint_destroy(&left_numerator);
    phy_bigint_destroy(&gcd_right);
    phy_bigint_destroy(&gcd_left);
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigrat_reciprocal(const phy_bigrat *value,
                                 phy_bigrat *out_reciprocal)
{
    if (!compatible2(value, out_reciprocal)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (value->numerator.count == 0u) {
        return PHY_ERR_DOMAIN;
    }
    phy_exact_context *context = rational_context(value);
    phy_status status = phy_exact_operation_begin(context);
    phy_bigrat temporary;
    memset(&temporary, 0, sizeof temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = phy_bigint_copy(
            &value->denominator, &temporary.numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigint_copy(
            &value->numerator, &temporary.denominator);
    }
    if (status == PHY_OK) {
        status = normalize_in_place(&temporary);
    }
    if (status == PHY_OK) {
        status = commit(out_reciprocal, &temporary, status);
    } else if (temporary.private_magic == PHY_BIGRAT_MAGIC) {
        phy_bigrat_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigrat_pow_i32(const phy_bigrat *base, int32_t exponent,
                              phy_bigrat *out_power)
{
    if (!compatible2(base, out_power)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (exponent < 0 && base->numerator.count == 0u) {
        return PHY_ERR_DOMAIN;
    }
    phy_exact_context *context = rational_context(base);
    phy_status status = phy_exact_operation_begin(context);
    phy_bigrat temporary;
    memset(&temporary, 0, sizeof temporary);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context, &temporary);
    }
    const uint32_t magnitude =
        exponent < 0 ? (uint32_t)(-(int64_t)exponent)
                     : (uint32_t)exponent;
    if (status == PHY_OK && exponent >= 0) {
        status = phy_bigint_pow_u32(
            &base->numerator, magnitude, &temporary.numerator);
        if (status == PHY_OK) {
            status = phy_bigint_pow_u32(
                &base->denominator, magnitude,
                &temporary.denominator);
        }
    } else if (status == PHY_OK) {
        status = phy_bigint_pow_u32(
            &base->denominator, magnitude, &temporary.numerator);
        if (status == PHY_OK) {
            status = phy_bigint_pow_u32(
                &base->numerator, magnitude, &temporary.denominator);
        }
    }
    if (status == PHY_OK) {
        status = normalize_in_place(&temporary);
    }
    if (status == PHY_OK) {
        status = commit(out_power, &temporary, status);
    } else if (temporary.private_magic == PHY_BIGRAT_MAGIC) {
        phy_bigrat_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigrat_compare(const phy_bigrat *left,
                              const phy_bigrat *right, int *out_comparison)
{
    if (!compatible2(left, right) || out_comparison == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = rational_context(left);
    phy_status status = phy_exact_operation_begin(context);
    phy_bigint left_cross = {0};
    phy_bigint right_cross = {0};
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &left_cross);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &right_cross);
    }
    if (status == PHY_OK) {
        status = phy_bigint_multiply(
            &left->numerator, &right->denominator, &left_cross);
    }
    if (status == PHY_OK) {
        status = phy_bigint_multiply(
            &right->numerator, &left->denominator, &right_cross);
    }
    if (status == PHY_OK) {
        *out_comparison =
            phy_bigint_compare(&left_cross, &right_cross);
    }
    phy_bigint_destroy(&right_cross);
    phy_bigint_destroy(&left_cross);
    return phy_exact_operation_end(context, status);
}

const phy_bigint *phy_bigrat_numerator(const phy_bigrat *value)
{
    return rational_valid_shallow(value) ? &value->numerator : NULL;
}

const phy_bigint *phy_bigrat_denominator(const phy_bigrat *value)
{
    return rational_valid_shallow(value) ? &value->denominator : NULL;
}

bool phy_bigrat_try_i64(const phy_bigrat *value, int64_t *out_numerator,
                        int64_t *out_denominator)
{
    return rational_valid_shallow(value) && out_numerator != NULL &&
           out_denominator != NULL &&
           phy_bigint_try_i64(&value->numerator, out_numerator) &&
           phy_bigint_try_i64(&value->denominator, out_denominator);
}
