/*
 * Base-2^32 arbitrary-precision signed integers.
 *
 * The algorithms prefer auditability and transactional error handling over
 * cleverness. Multiplication is schoolbook; multi-limb division is binary
 * long division with a single-limb fast path. Both are protected by the
 * context work ceiling and can be replaced behind this API later.
 */
#include "exact_internal.h"

static uint64_t magnitude_i64(int64_t value)
{
    return value < 0 ? (~(uint64_t)value + 1u) : (uint64_t)value;
}

static bool compatible2(const phy_bigint *left, const phy_bigint *right)
{
    return phy_bigint_shallow_valid(left) &&
           phy_bigint_shallow_valid(right) &&
           left->context == right->context;
}

static bool compatible3(const phy_bigint *left, const phy_bigint *right,
                        const phy_bigint *output)
{
    return compatible2(left, right) && phy_bigint_shallow_valid(output) &&
           left->context == output->context;
}

static phy_status commit(phy_bigint *destination, phy_bigint *temporary,
                         phy_status status)
{
    if (status == PHY_OK) {
        phy_bigint_swap_payload(destination, temporary);
    }
    phy_bigint_destroy(temporary);
    return status;
}

phy_status phy_bigint_assign_u64(phy_bigint *value, uint64_t magnitude,
                                 int sign)
{
    if (!phy_bigint_shallow_valid(value) || sign < -1 || sign > 1) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (magnitude == 0u) {
        value->count = 0u;
        value->sign = 0;
        return PHY_OK;
    }
    const size_t needed = magnitude > 0xffffffffu ? 2u : 1u;
    phy_status status = phy_bigint_reserve(value, needed);
    if (status != PHY_OK) {
        return status;
    }
    value->limbs[0] = (uint32_t)magnitude;
    if (needed == 2u) {
        value->limbs[1] = (uint32_t)(magnitude >> 32u);
    }
    value->count = needed;
    value->sign = sign == 0 ? 1 : sign;
    return PHY_OK;
}

static phy_status copy_payload(const phy_bigint *source,
                               phy_bigint *destination, bool magnitude_only)
{
    phy_status status = phy_bigint_reserve(destination, source->count);
    if (status != PHY_OK) {
        return status;
    }
    if (source->count != 0u) {
        memcpy(destination->limbs, source->limbs,
               source->count * sizeof(uint32_t));
    }
    destination->count = source->count;
    destination->sign =
        source->count == 0u ? 0 : (magnitude_only ? 1 : source->sign);
    return PHY_OK;
}

phy_status phy_bigint_copy_magnitude(const phy_bigint *source,
                                     phy_bigint *destination)
{
    if (!compatible2(source, destination)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return copy_payload(source, destination, true);
}

phy_status phy_bigint_set_i64(phy_bigint *value, int64_t integer)
{
    if (!phy_bigint_shallow_valid(value)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = value->context;
    phy_status status = phy_exact_operation_begin(context);
    if (status != PHY_OK) {
        return status;
    }
    phy_bigint temporary = {0};
    status = phy_bigint_init(context, &temporary);
    if (status == PHY_OK) {
        status = phy_bigint_assign_u64(
            &temporary, magnitude_i64(integer),
            integer < 0 ? -1 : (integer > 0 ? 1 : 0));
    }
    if (status == PHY_OK) {
        status = commit(value, &temporary, status);
    } else if (phy_bigint_is_initialized(&temporary)) {
        phy_bigint_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigint_copy(const phy_bigint *source, phy_bigint *destination)
{
    if (!compatible2(source, destination)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = source->context;
    phy_status status = phy_exact_operation_begin(context);
    if (status != PHY_OK) {
        return status;
    }
    if (source == destination) {
        return phy_exact_operation_end(context, PHY_OK);
    }
    phy_bigint temporary = {0};
    status = phy_bigint_init(context, &temporary);
    if (status == PHY_OK) {
        status = copy_payload(source, &temporary, false);
    }
    if (status == PHY_OK) {
        status = commit(destination, &temporary, status);
    } else if (phy_bigint_is_initialized(&temporary)) {
        phy_bigint_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

int phy_bigint_sign(const phy_bigint *value)
{
    return phy_bigint_shallow_valid(value) ? value->sign : 0;
}

static unsigned limb_bit_length(uint32_t value)
{
    unsigned bits = 0u;
    while (value != 0u) {
        bits++;
        value >>= 1u;
    }
    return bits;
}

size_t phy_bigint_bit_length(const phy_bigint *value)
{
    if (!phy_bigint_shallow_valid(value) || value->count == 0u) {
        return 0u;
    }
    return (value->count - 1u) * 32u +
           limb_bit_length(value->limbs[value->count - 1u]);
}

bool phy_bigint_try_i64(const phy_bigint *value, int64_t *out_value)
{
    if (!phy_bigint_shallow_valid(value) || out_value == NULL ||
        value->count > 2u) {
        return false;
    }
    if (value->count == 0u) {
        *out_value = 0;
        return true;
    }
    uint64_t magnitude = value->limbs[0];
    if (value->count == 2u) {
        magnitude |= (uint64_t)value->limbs[1] << 32u;
    }
    if (value->sign > 0) {
        if (magnitude > (uint64_t)INT64_MAX) {
            return false;
        }
        *out_value = (int64_t)magnitude;
        return true;
    }
    const uint64_t minimum_magnitude = (uint64_t)INT64_MAX + 1u;
    if (magnitude > minimum_magnitude) {
        return false;
    }
    *out_value = magnitude == minimum_magnitude
                     ? INT64_MIN
                     : -(int64_t)magnitude;
    return true;
}

int phy_bigint_compare_abs(const phy_bigint *left, const phy_bigint *right)
{
    if (!compatible2(left, right)) {
        return 0;
    }
    if (left->count != right->count) {
        return left->count > right->count ? 1 : -1;
    }
    for (size_t index = left->count; index-- != 0u;) {
        if (left->limbs[index] != right->limbs[index]) {
            return left->limbs[index] > right->limbs[index] ? 1 : -1;
        }
    }
    return 0;
}

int phy_bigint_compare(const phy_bigint *left, const phy_bigint *right)
{
    if (!compatible2(left, right)) {
        return 0;
    }
    if (left->sign != right->sign) {
        return left->sign > right->sign ? 1 : -1;
    }
    if (left->sign == 0) {
        return 0;
    }
    const int magnitude = phy_bigint_compare_abs(left, right);
    return left->sign > 0 ? magnitude : -magnitude;
}

static phy_status multiply_small_in_place(phy_bigint *value, uint32_t factor)
{
    if (value->count == 0u || factor == 1u) {
        return PHY_OK;
    }
    if (factor == 0u) {
        value->count = 0u;
        value->sign = 0;
        return PHY_OK;
    }
    phy_status status = phy_bigint_reserve(value, value->count + 1u);
    if (status != PHY_OK) {
        return status;
    }
    uint64_t carry = 0u;
    for (size_t index = 0u; index < value->count; ++index) {
        status = phy_exact_step(value->context, 1u);
        if (status != PHY_OK) {
            return status;
        }
        const uint64_t product =
            (uint64_t)value->limbs[index] * factor + carry;
        value->limbs[index] = (uint32_t)product;
        carry = product >> 32u;
    }
    if (carry != 0u) {
        value->limbs[value->count++] = (uint32_t)carry;
    }
    return PHY_OK;
}

static phy_status add_small_in_place(phy_bigint *value, uint32_t addend)
{
    if (addend == 0u) {
        return PHY_OK;
    }
    if (value->count == 0u) {
        return phy_bigint_assign_u64(value, addend, 1);
    }
    /*
     * Reserve before mutating. If every occupied limb overflows, the old top
     * limb is temporarily zero while the carry still needs one more limb.
     * Calling reserve in that transient state would correctly reject the
     * non-normalized value and strand the carry.
     */
    phy_status status =
        phy_bigint_reserve(value, value->count + 1u);
    if (status != PHY_OK) {
        return status;
    }
    size_t index = 0u;
    uint64_t carry = addend;
    while (carry != 0u && index < value->count) {
        status = phy_exact_step(value->context, 1u);
        if (status != PHY_OK) {
            return status;
        }
        const uint64_t sum = (uint64_t)value->limbs[index] + carry;
        value->limbs[index] = (uint32_t)sum;
        carry = sum >> 32u;
        index++;
    }
    if (carry != 0u) {
        value->limbs[value->count++] = (uint32_t)carry;
    }
    return PHY_OK;
}

phy_status phy_bigint_read(phy_bigint *value, const char *decimal)
{
    if (!phy_bigint_shallow_valid(value) || decimal == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = value->context;
    phy_status status = phy_exact_operation_begin(context);
    if (status != PHY_OK) {
        return status;
    }

    const char *cursor = decimal;
    int sign = 1;
    if (*cursor == '+' || *cursor == '-') {
        sign = *cursor == '-' ? -1 : 1;
        cursor++;
    }
    if (*cursor == '\0') {
        return phy_exact_operation_end(context, PHY_ERR_PARSE);
    }
    for (const char *scan = cursor; *scan != '\0'; ++scan) {
        if (*scan < '0' || *scan > '9') {
            return phy_exact_operation_end(context, PHY_ERR_PARSE);
        }
    }

    phy_bigint temporary = {0};
    status = phy_bigint_init(context, &temporary);
    while (status == PHY_OK && *cursor != '\0') {
        status = multiply_small_in_place(&temporary, 10u);
        if (status == PHY_OK) {
            status =
                add_small_in_place(&temporary, (uint32_t)(*cursor - '0'));
        }
        cursor++;
    }
    if (status == PHY_OK && temporary.count != 0u) {
        temporary.sign = sign;
    }
    if (status == PHY_OK) {
        status = commit(value, &temporary, status);
    } else {
        phy_bigint_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

static phy_status divide_small_in_place(phy_bigint *value, uint32_t divisor,
                                        uint32_t *out_remainder)
{
    uint64_t remainder = 0u;
    for (size_t index = value->count; index-- != 0u;) {
        phy_status status = phy_exact_step(value->context, 1u);
        if (status != PHY_OK) {
            return status;
        }
        const uint64_t current =
            (remainder << 32u) | value->limbs[index];
        value->limbs[index] = (uint32_t)(current / divisor);
        remainder = current % divisor;
    }
    phy_bigint_normalize(value);
    *out_remainder = (uint32_t)remainder;
    return PHY_OK;
}

static unsigned decimal_digits_u32(uint32_t value)
{
    unsigned digits = 1u;
    while (value >= 10u) {
        value /= 10u;
        digits++;
    }
    return digits;
}

static char *emit_u32(char *cursor, uint32_t value, unsigned width)
{
    char digits[10];
    unsigned count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count < width) {
        digits[count++] = '0';
    }
    while (count != 0u) {
        *cursor++ = digits[--count];
    }
    return cursor;
}

phy_status phy_bigint_write(const phy_bigint *value, char *buffer,
                            size_t capacity, size_t *out_required)
{
    if (!phy_bigint_shallow_valid(value) || out_required == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = value->context;
    phy_status status = phy_exact_operation_begin(context);
    if (status != PHY_OK) {
        return status;
    }
    *out_required = 0u;
    if (value->count == 0u) {
        *out_required = 2u;
        if (buffer == NULL) {
            return phy_exact_operation_end(context, PHY_OK);
        }
        if (capacity < 2u) {
            return phy_exact_operation_end(context, PHY_ERR_MEMORY_LIMIT);
        }
        buffer[0] = '0';
        buffer[1] = '\0';
        return phy_exact_operation_end(context, PHY_OK);
    }

    phy_bigint temporary = {0};
    status = phy_bigint_init(context, &temporary);
    if (status == PHY_OK) {
        status = copy_payload(value, &temporary, true);
    }
    size_t chunk_capacity = 0u;
    uint32_t *chunks = NULL;
    size_t chunk_bytes = 0u;
    if (status == PHY_OK) {
        if (value->count > ((size_t)-1 - 2u) / 10u) {
            status = PHY_ERR_MEMORY_LIMIT;
        } else {
            chunk_capacity = (value->count * 10u + 8u) / 9u + 2u;
            if (chunk_capacity > (size_t)-1 / sizeof(uint32_t)) {
                status = PHY_ERR_MEMORY_LIMIT;
            } else {
                chunk_bytes = chunk_capacity * sizeof(uint32_t);
                status = phy_exact_temp_alloc(
                    context, chunk_bytes, (void **)&chunks);
            }
        }
    }

    size_t chunk_count = 0u;
    while (status == PHY_OK && temporary.count != 0u) {
        if (chunk_count >= chunk_capacity) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
            break;
        }
        status = divide_small_in_place(
            &temporary, 1000000000u, &chunks[chunk_count]);
        if (status == PHY_OK) {
            chunk_count++;
        }
    }
    if (status == PHY_OK && chunk_count == 0u) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }

    size_t required = 0u;
    if (status == PHY_OK) {
        const unsigned leading =
            decimal_digits_u32(chunks[chunk_count - 1u]);
        const size_t sign_bytes = value->sign < 0 ? 1u : 0u;
        if (chunk_count - 1u > ((size_t)-1 - sign_bytes - leading - 1u) / 9u) {
            status = PHY_ERR_MEMORY_LIMIT;
        } else {
            required = sign_bytes + leading +
                       (chunk_count - 1u) * 9u + 1u;
            *out_required = required;
        }
    }
    if (status == PHY_OK && buffer != NULL && capacity < required) {
        status = PHY_ERR_MEMORY_LIMIT;
    }
    if (status == PHY_OK && buffer != NULL) {
        char *cursor = buffer;
        if (value->sign < 0) {
            *cursor++ = '-';
        }
        cursor = emit_u32(
            cursor, chunks[chunk_count - 1u],
            decimal_digits_u32(chunks[chunk_count - 1u]));
        for (size_t index = chunk_count - 1u; index-- != 0u;) {
            cursor = emit_u32(cursor, chunks[index], 9u);
        }
        *cursor = '\0';
    }

    if (chunks != NULL) {
        phy_exact_temp_free(context, chunks, chunk_bytes);
    }
    phy_bigint_destroy(&temporary);
    return phy_exact_operation_end(context, status);
}

static phy_status add_magnitudes(const phy_bigint *left,
                                 const phy_bigint *right,
                                 phy_bigint *output)
{
    const size_t count = left->count > right->count ? left->count
                                                    : right->count;
    phy_status status = phy_bigint_reserve(output, count + 1u);
    if (status != PHY_OK) {
        return status;
    }
    uint64_t carry = 0u;
    for (size_t index = 0u; index < count; ++index) {
        status = phy_exact_step(left->context, 1u);
        if (status != PHY_OK) {
            return status;
        }
        const uint64_t a =
            index < left->count ? left->limbs[index] : 0u;
        const uint64_t b =
            index < right->count ? right->limbs[index] : 0u;
        const uint64_t partial = a + b;
        const uint64_t sum = partial + carry;
        output->limbs[index] = (uint32_t)sum;
        carry = sum >> 32u;
    }
    output->count = count;
    if (carry != 0u) {
        output->limbs[output->count++] = (uint32_t)carry;
    }
    return PHY_OK;
}

/* |left| >= |right|. */
static phy_status subtract_magnitudes(const phy_bigint *left,
                                      const phy_bigint *right,
                                      phy_bigint *output)
{
    phy_status status = phy_bigint_reserve(output, left->count);
    if (status != PHY_OK) {
        return status;
    }
    uint64_t borrow = 0u;
    for (size_t index = 0u; index < left->count; ++index) {
        status = phy_exact_step(left->context, 1u);
        if (status != PHY_OK) {
            return status;
        }
        const uint64_t a = left->limbs[index];
        const uint64_t b =
            index < right->count ? right->limbs[index] : 0u;
        const uint64_t subtrahend = b + borrow;
        output->limbs[index] = (uint32_t)(a - subtrahend);
        borrow = a < subtrahend ? 1u : 0u;
    }
    if (borrow != 0u) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    output->count = left->count;
    output->sign = 1;
    phy_bigint_normalize(output);
    return PHY_OK;
}

static phy_status add_signed(const phy_bigint *left,
                             const phy_bigint *right, int right_sign,
                             phy_bigint *output)
{
    if (left->count == 0u) {
        phy_status status = copy_payload(right, output, false);
        if (status == PHY_OK && output->count != 0u) {
            output->sign = right_sign;
        }
        return status;
    }
    if (right->count == 0u) {
        return copy_payload(left, output, false);
    }
    if (left->sign == right_sign) {
        phy_status status = add_magnitudes(left, right, output);
        if (status == PHY_OK) {
            output->sign = left->sign;
        }
        return status;
    }
    const int order = phy_bigint_compare_abs(left, right);
    if (order == 0) {
        output->count = 0u;
        output->sign = 0;
        return PHY_OK;
    }
    const phy_bigint *larger = order > 0 ? left : right;
    const phy_bigint *smaller = order > 0 ? right : left;
    phy_status status = subtract_magnitudes(larger, smaller, output);
    if (status == PHY_OK && output->count != 0u) {
        output->sign = order > 0 ? left->sign : right_sign;
    }
    return status;
}

phy_status phy_bigint_add(const phy_bigint *left, const phy_bigint *right,
                          phy_bigint *out_sum)
{
    if (!compatible3(left, right, out_sum)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = left->context;
    phy_status status = phy_exact_operation_begin(context);
    phy_bigint temporary = {0};
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = add_signed(left, right, right->sign, &temporary);
    }
    if (status == PHY_OK) {
        status = commit(out_sum, &temporary, status);
    } else if (phy_bigint_is_initialized(&temporary)) {
        phy_bigint_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigint_subtract(const phy_bigint *left,
                               const phy_bigint *right,
                               phy_bigint *out_difference)
{
    if (!compatible3(left, right, out_difference)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = left->context;
    phy_status status = phy_exact_operation_begin(context);
    phy_bigint temporary = {0};
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = add_signed(left, right, -right->sign, &temporary);
    }
    if (status == PHY_OK) {
        status = commit(out_difference, &temporary, status);
    } else if (phy_bigint_is_initialized(&temporary)) {
        phy_bigint_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigint_negate(const phy_bigint *value, phy_bigint *out_value)
{
    if (!compatible2(value, out_value)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = value->context;
    phy_status status = phy_exact_operation_begin(context);
    phy_bigint temporary = {0};
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &temporary);
    }
    if (status == PHY_OK) {
        status = copy_payload(value, &temporary, false);
    }
    if (status == PHY_OK && temporary.count != 0u) {
        temporary.sign = -temporary.sign;
    }
    if (status == PHY_OK) {
        status = commit(out_value, &temporary, status);
    } else if (phy_bigint_is_initialized(&temporary)) {
        phy_bigint_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigint_multiply(const phy_bigint *left,
                               const phy_bigint *right,
                               phy_bigint *out_product)
{
    if (!compatible3(left, right, out_product)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = left->context;
    phy_status status = phy_exact_operation_begin(context);
    phy_bigint temporary = {0};
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &temporary);
    }
    if (status == PHY_OK && left->count != 0u && right->count != 0u) {
        if (left->count > (size_t)-1 - right->count) {
            status = PHY_ERR_MEMORY_LIMIT;
        } else {
            const size_t full_count = left->count + right->count;
            /*
             * An n-by-m product needs either n+m-1 or n+m limbs. Allow the
             * former at the configured ceiling and reject only if the final
             * carry proves that the extra limb is actually required.
             */
            const size_t minimum_count = full_count - 1u;
            const size_t count =
                full_count <= context->limits.max_limbs
                    ? full_count
                    : minimum_count;
            if (minimum_count > context->limits.max_limbs) {
                status = PHY_ERR_MEMORY_LIMIT;
            }
            if (status == PHY_OK) {
                status = phy_bigint_reserve(&temporary, count);
            }
            if (status == PHY_OK) {
                memset(temporary.limbs, 0, count * sizeof(uint32_t));
                temporary.count = count;
                temporary.sign =
                    left->sign == right->sign ? 1 : -1;
            }
        }
        for (size_t i = 0u; status == PHY_OK && i < left->count; ++i) {
            uint64_t carry = 0u;
            for (size_t j = 0u; j < right->count; ++j) {
                status = phy_exact_step(context, 1u);
                if (status != PHY_OK) {
                    break;
                }
                const size_t at = i + j;
                const uint64_t product =
                    (uint64_t)left->limbs[i] * right->limbs[j] +
                    temporary.limbs[at] + carry;
                temporary.limbs[at] = (uint32_t)product;
                carry = product >> 32u;
            }
            if (status == PHY_OK) {
                const size_t carry_at = i + right->count;
                if (carry_at < temporary.capacity) {
                    temporary.limbs[carry_at] = (uint32_t)carry;
                } else if (carry != 0u) {
                    status = PHY_ERR_MEMORY_LIMIT;
                }
            }
        }
        if (status == PHY_OK) {
            phy_bigint_normalize(&temporary);
        }
    }
    if (status == PHY_OK) {
        status = commit(out_product, &temporary, status);
    } else if (phy_bigint_is_initialized(&temporary)) {
        phy_bigint_destroy(&temporary);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigint_pow_u32(const phy_bigint *base, uint32_t exponent,
                              phy_bigint *out_power)
{
    if (!compatible2(base, out_power)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = base->context;
    phy_status status = phy_exact_operation_begin(context);
    phy_bigint result = {0};
    phy_bigint factor = {0};
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &result);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &factor);
    }
    if (status == PHY_OK) {
        status = phy_bigint_assign_u64(&result, 1u, 1);
    }
    if (status == PHY_OK) {
        status = copy_payload(base, &factor, false);
    }
    uint32_t remaining = exponent;
    while (status == PHY_OK && remaining != 0u) {
        if ((remaining & 1u) != 0u) {
            status = phy_bigint_multiply(&result, &factor, &result);
        }
        remaining >>= 1u;
        if (status == PHY_OK && remaining != 0u) {
            status = phy_bigint_multiply(&factor, &factor, &factor);
        }
    }
    if (status == PHY_OK) {
        phy_bigint_swap_payload(out_power, &result);
    }
    if (phy_bigint_is_initialized(&factor)) {
        phy_bigint_destroy(&factor);
    }
    if (phy_bigint_is_initialized(&result)) {
        phy_bigint_destroy(&result);
    }
    return phy_exact_operation_end(context, status);
}

static bool bit_at(const phy_bigint *value, size_t bit)
{
    const size_t limb = bit / 32u;
    const unsigned shift = (unsigned)(bit % 32u);
    return limb < value->count &&
           ((value->limbs[limb] >> shift) & 1u) != 0u;
}

static phy_status set_bit(phy_bigint *value, size_t bit)
{
    const size_t limb = bit / 32u;
    const unsigned shift = (unsigned)(bit % 32u);
    phy_status status = phy_bigint_reserve(value, limb + 1u);
    if (status != PHY_OK) {
        return status;
    }
    while (value->count <= limb) {
        value->limbs[value->count++] = 0u;
    }
    value->limbs[limb] |= (uint32_t)1u << shift;
    value->sign = 1;
    return PHY_OK;
}

static phy_status shift_left_one(phy_bigint *value, bool low_bit)
{
    const bool grows =
        value->count == 0u ||
        (value->limbs[value->count - 1u] & 0x80000000u) != 0u;
    const size_t needed = value->count + (grows ? 1u : 0u);
    phy_status status = phy_bigint_reserve(value, needed);
    if (status != PHY_OK) {
        return status;
    }
    uint64_t carry = low_bit ? 1u : 0u;
    for (size_t index = 0u; index < value->count; ++index) {
        status = phy_exact_step(value->context, 1u);
        if (status != PHY_OK) {
            return status;
        }
        const uint64_t shifted =
            ((uint64_t)value->limbs[index] << 1u) | carry;
        value->limbs[index] = (uint32_t)shifted;
        carry = shifted >> 32u;
    }
    if (carry != 0u) {
        value->limbs[value->count++] = (uint32_t)carry;
    }
    if (value->count != 0u) {
        value->sign = 1;
    }
    return PHY_OK;
}

/* |value| >= |subtrahend|, mutates value. */
static phy_status subtract_abs_in_place(phy_bigint *value,
                                        const phy_bigint *subtrahend)
{
    uint64_t borrow = 0u;
    for (size_t index = 0u; index < value->count; ++index) {
        phy_status status = phy_exact_step(value->context, 1u);
        if (status != PHY_OK) {
            return status;
        }
        const uint64_t right =
            index < subtrahend->count ? subtrahend->limbs[index] : 0u;
        const uint64_t total = right + borrow;
        const uint64_t left = value->limbs[index];
        value->limbs[index] = (uint32_t)(left - total);
        borrow = left < total ? 1u : 0u;
    }
    if (borrow != 0u) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    phy_bigint_normalize(value);
    if (value->count != 0u) {
        value->sign = 1;
    }
    return PHY_OK;
}

static phy_status divmod_abs(const phy_bigint *dividend,
                             const phy_bigint *divisor,
                             phy_bigint *quotient, phy_bigint *remainder)
{
    quotient->count = 0u;
    quotient->sign = 0;
    remainder->count = 0u;
    remainder->sign = 0;
    const int order = phy_bigint_compare_abs(dividend, divisor);
    if (order < 0) {
        return copy_payload(dividend, remainder, true);
    }
    if (divisor->count == 1u) {
        phy_status status = phy_bigint_reserve(quotient, dividend->count);
        if (status != PHY_OK) {
            return status;
        }
        uint64_t carry = 0u;
        const uint32_t small = divisor->limbs[0];
        for (size_t index = dividend->count; index-- != 0u;) {
            status = phy_exact_step(dividend->context, 1u);
            if (status != PHY_OK) {
                return status;
            }
            const uint64_t current =
                (carry << 32u) | dividend->limbs[index];
            quotient->limbs[index] = (uint32_t)(current / small);
            carry = current % small;
        }
        quotient->count = dividend->count;
        quotient->sign = 1;
        phy_bigint_normalize(quotient);
        return phy_bigint_assign_u64(remainder, carry, carry != 0u ? 1 : 0);
    }

    const size_t bits = phy_bigint_bit_length(dividend);
    phy_status status = phy_bigint_reserve(quotient, dividend->count);
    if (status == PHY_OK) {
        memset(quotient->limbs, 0,
               quotient->capacity * sizeof(uint32_t));
    }
    for (size_t bit = bits; status == PHY_OK && bit-- != 0u;) {
        status = phy_exact_step(dividend->context, 1u);
        if (status == PHY_OK) {
            status = shift_left_one(remainder, bit_at(dividend, bit));
        }
        if (status == PHY_OK &&
            phy_bigint_compare_abs(remainder, divisor) >= 0) {
            status = subtract_abs_in_place(remainder, divisor);
            if (status == PHY_OK) {
                status = set_bit(quotient, bit);
            }
        }
    }
    phy_bigint_normalize(quotient);
    phy_bigint_normalize(remainder);
    return status;
}

phy_status phy_bigint_divmod(const phy_bigint *dividend,
                             const phy_bigint *divisor,
                             phy_bigint *out_quotient,
                             phy_bigint *out_remainder)
{
    if (!compatible3(dividend, divisor, out_quotient) ||
        !phy_bigint_shallow_valid(out_remainder) ||
        out_remainder->context != dividend->context ||
        out_quotient == out_remainder) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (divisor->count == 0u) {
        return PHY_ERR_DOMAIN;
    }
    phy_exact_context *context = dividend->context;
    phy_status status = phy_exact_operation_begin(context);
    phy_bigint quotient = {0};
    phy_bigint remainder = {0};
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &quotient);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &remainder);
    }
    if (status == PHY_OK) {
        status = divmod_abs(dividend, divisor, &quotient, &remainder);
    }
    if (status == PHY_OK && quotient.count != 0u) {
        quotient.sign = dividend->sign == divisor->sign ? 1 : -1;
    }
    if (status == PHY_OK && remainder.count != 0u) {
        remainder.sign = dividend->sign;
    }
    if (status == PHY_OK) {
        phy_bigint_swap_payload(out_quotient, &quotient);
        phy_bigint_swap_payload(out_remainder, &remainder);
    }
    if (phy_bigint_is_initialized(&remainder)) {
        phy_bigint_destroy(&remainder);
    }
    if (phy_bigint_is_initialized(&quotient)) {
        phy_bigint_destroy(&quotient);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigint_divide_exact(const phy_bigint *dividend,
                                   const phy_bigint *divisor,
                                   phy_bigint *out_quotient)
{
    if (!compatible3(dividend, divisor, out_quotient)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = dividend->context;
    phy_status status = phy_exact_operation_begin(context);
    phy_bigint quotient = {0};
    phy_bigint remainder = {0};
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &quotient);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &remainder);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divmod(
            dividend, divisor, &quotient, &remainder);
    }
    if (status == PHY_OK && remainder.count != 0u) {
        status = PHY_ERR_DOMAIN;
    }
    if (status == PHY_OK) {
        phy_bigint_swap_payload(out_quotient, &quotient);
    }
    if (phy_bigint_is_initialized(&remainder)) {
        phy_bigint_destroy(&remainder);
    }
    if (phy_bigint_is_initialized(&quotient)) {
        phy_bigint_destroy(&quotient);
    }
    return phy_exact_operation_end(context, status);
}

phy_status phy_bigint_gcd(const phy_bigint *left, const phy_bigint *right,
                          phy_bigint *out_gcd)
{
    if (!compatible3(left, right, out_gcd)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_exact_context *context = left->context;
    phy_status status = phy_exact_operation_begin(context);
    phy_bigint a = {0};
    phy_bigint b = {0};
    phy_bigint quotient = {0};
    phy_bigint remainder = {0};
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &a);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &b);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &quotient);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context, &remainder);
    }
    if (status == PHY_OK) {
        status = copy_payload(left, &a, true);
    }
    if (status == PHY_OK) {
        status = copy_payload(right, &b, true);
    }
    while (status == PHY_OK && b.count != 0u) {
        status = phy_bigint_divmod(&a, &b, &quotient, &remainder);
        if (status == PHY_OK) {
            phy_bigint_swap_payload(&a, &b);
            phy_bigint_swap_payload(&b, &remainder);
        }
    }
    if (status == PHY_OK) {
        phy_bigint_swap_payload(out_gcd, &a);
    }
    if (phy_bigint_is_initialized(&remainder)) {
        phy_bigint_destroy(&remainder);
    }
    if (phy_bigint_is_initialized(&quotient)) {
        phy_bigint_destroy(&quotient);
    }
    if (phy_bigint_is_initialized(&b)) {
        phy_bigint_destroy(&b);
    }
    if (phy_bigint_is_initialized(&a)) {
        phy_bigint_destroy(&a);
    }
    return phy_exact_operation_end(context, status);
}
