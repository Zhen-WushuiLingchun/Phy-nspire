/*
 * Promotion bridge between immutable exact IR atoms and the bounded native
 * arbitrary-precision domain.
 *
 * The mature int64 coefficient code remains the fast path. Only an operation
 * that actually sees a promoted operand pays for parsing limbs and publishing
 * a new canonical atom.
 */
#include <string.h>

#include "cas_internal.h"
#include "phy/exact.h"

phy_exact_context *phy_cas_exact_operation_context(phy_cas *cas)
{
    phy_exact_limits limits;
    phy_exact_limits_defaults(&limits);
    limits.max_limbs = UINT32_MAX;
    limits.max_steps = cas->limits.max_steps;
    limits.max_bytes = cas->limits.max_bytes;
    phy_exact_context *exact = phy_exact_context_create(&limits);
    if (exact != NULL) {
        phy_exact_set_cancel(exact, cas->cancelled, cas->cancel_user);
    }
    return exact;
}

phy_status phy_cas_exact_load_ref(phy_cas *cas, phy_exact_context *exact,
                                  phy_ir_ref ref, phy_bigrat *out_value)
{
    int64_t numerator = 0;
    int64_t denominator = 0;
    if (phy_ir_integer_value(cas->ir, ref, &numerator)) {
        return phy_bigrat_set_i64(out_value, numerator, 1);
    }
    if (phy_ir_rational_value(
            cas->ir, ref, &numerator, &denominator)) {
        return phy_bigrat_set_i64(
            out_value, numerator, denominator);
    }

    phy_ir_exact_view view;
    if (!phy_ir_exact_decimal_view(cas->ir, ref, &view)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const size_t numerator_bytes = view.numerator_length + 1u;
    const size_t denominator_bytes = view.denominator_length + 1u;
    if (numerator_bytes < view.numerator_length ||
        denominator_bytes < view.denominator_length ||
        numerator_bytes > (size_t)-1 - denominator_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t bytes = numerator_bytes + denominator_bytes;
    char *text = NULL;
    phy_status status =
        phy_cas_temp_alloc(cas, bytes, (void **)&text);
    if (status != PHY_OK) {
        return status;
    }
    memcpy(text, view.numerator, view.numerator_length);
    text[view.numerator_length] = '\0';
    char *denominator_text = text + numerator_bytes;
    memcpy(denominator_text, view.denominator, view.denominator_length);
    denominator_text[view.denominator_length] = '\0';
    status = phy_bigrat_read(out_value, text, denominator_text);
    phy_cas_temp_free(cas, text, bytes);
    (void)exact;
    return status;
}

phy_status phy_cas_exact_publish_bigrat(phy_cas *cas,
                                        const phy_bigrat *value,
                                        phy_ir_ref *out_ref)
{
    int64_t numerator = 0;
    int64_t denominator = 0;
    if (phy_bigrat_try_i64(value, &numerator, &denominator)) {
        const phy_ir_ref ref =
            phy_ir_rational(cas->ir, numerator, denominator);
        if (ref == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
        *out_ref = ref;
        return PHY_OK;
    }

    size_t numerator_required = 0u;
    size_t denominator_required = 0u;
    phy_status status = phy_bigint_write(
        phy_bigrat_numerator(value), NULL, 0u, &numerator_required);
    if (status == PHY_OK) {
        status = phy_bigint_write(
            phy_bigrat_denominator(value), NULL, 0u,
            &denominator_required);
    }
    if (status != PHY_OK ||
        numerator_required > (size_t)-1 - denominator_required) {
        return status != PHY_OK ? status : PHY_ERR_MEMORY_LIMIT;
    }
    const size_t numerator_bytes = numerator_required;
    const size_t denominator_bytes = denominator_required;
    const size_t bytes = numerator_bytes + denominator_bytes;
    char *text = NULL;
    status = phy_cas_temp_alloc(cas, bytes, (void **)&text);
    if (status != PHY_OK) {
        return status;
    }
    char *denominator_text = text + numerator_bytes;
    size_t numerator_written = 0u;
    size_t denominator_written = 0u;
    if (status == PHY_OK) {
        status = phy_bigint_write(
            phy_bigrat_numerator(value), text, numerator_bytes,
            &numerator_written);
    }
    if (status == PHY_OK) {
        status = phy_bigint_write(
            phy_bigrat_denominator(value), denominator_text,
            denominator_bytes, &denominator_written);
    }
    if (status == PHY_OK &&
        (numerator_written != numerator_bytes ||
         denominator_written != denominator_bytes)) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    phy_ir_ref result = PHY_IR_NULL;
    if (status == PHY_OK) {
        result = phy_ir_rational_text_n(
            cas->ir, text, numerator_written - 1u, denominator_text,
            denominator_written - 1u);
        if (result == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
        }
    }
    phy_cas_temp_free(cas, text, bytes);
    if (status == PHY_OK) {
        *out_ref = result;
    }
    return status;
}

typedef phy_status (*binary_operation)(const phy_bigrat *left,
                                       const phy_bigrat *right,
                                       phy_bigrat *out_value);

static phy_status binary(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                         phy_ir_ref *out_ref, binary_operation operation)
{
    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_bigrat a;
    phy_bigrat b;
    phy_bigrat result;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    memset(&result, 0, sizeof result);
    phy_status status = phy_bigrat_init(exact, &a);
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &b);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &result);
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_load_ref(cas, exact, left, &a);
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_load_ref(cas, exact, right, &b);
    }
    if (status == PHY_OK) {
        status = operation(&a, &b, &result);
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_publish_bigrat(cas, &result, out_ref);
    }
    phy_bigrat_destroy(&result);
    phy_bigrat_destroy(&b);
    phy_bigrat_destroy(&a);
    phy_exact_context_destroy(exact);
    return status;
}

phy_status phy_cas_exact_add_refs(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out_ref)
{
    return binary(cas, left, right, out_ref, phy_bigrat_add);
}

phy_status phy_cas_exact_sub_refs(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out_ref)
{
    return binary(cas, left, right, out_ref, phy_bigrat_subtract);
}

phy_status phy_cas_exact_mul_refs(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out_ref)
{
    return binary(cas, left, right, out_ref, phy_bigrat_multiply);
}

phy_status phy_cas_exact_div_refs(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out_ref)
{
    return binary(cas, left, right, out_ref, phy_bigrat_divide);
}

phy_status phy_cas_exact_pow_ref(phy_cas *cas, phy_ir_ref base,
                                 int64_t exponent, phy_ir_ref *out_ref)
{
    if (exponent < INT32_MIN || exponent > INT32_MAX) {
        return PHY_ERR_UNSUPPORTED;
    }
    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_bigrat value;
    phy_bigrat result;
    memset(&value, 0, sizeof value);
    memset(&result, 0, sizeof result);
    phy_status status = phy_bigrat_init(exact, &value);
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &result);
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_load_ref(cas, exact, base, &value);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_pow_i32(
            &value, (int32_t)exponent, &result);
    }
    if (status == PHY_OK) {
        status = phy_cas_exact_publish_bigrat(cas, &result, out_ref);
    }
    phy_bigrat_destroy(&result);
    phy_bigrat_destroy(&value);
    phy_exact_context_destroy(exact);
    return status;
}

phy_status phy_cas_exact_mod_u32_ref(phy_cas *cas, phy_ir_ref integer,
                                     uint32_t modulus,
                                     uint32_t *out_remainder)
{
    if (cas == NULL || out_remainder == NULL || modulus == 0u ||
        phy_ir_kind_of(cas->ir, integer) != PHY_IR_INTEGER) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    int64_t small = 0;
    if (phy_ir_integer_value(cas->ir, integer, &small)) {
        const int64_t signed_modulus = (int64_t)modulus;
        int64_t remainder = small % signed_modulus;
        if (remainder < 0) {
            remainder += signed_modulus;
        }
        *out_remainder = (uint32_t)remainder;
        return PHY_OK;
    }

    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_bigrat value;
    memset(&value, 0, sizeof value);
    phy_status status = phy_bigrat_init(exact, &value);
    if (status == PHY_OK) {
        status = phy_cas_exact_load_ref(cas, exact, integer, &value);
    }
    if (status == PHY_OK) {
        status = phy_bigint_mod_u32(
            phy_bigrat_numerator(&value), modulus, out_remainder);
    }
    phy_bigrat_destroy(&value);
    phy_exact_context_destroy(exact);
    return status;
}

int phy_cas_exact_sign_ref(const phy_cas *cas, phy_ir_ref ref)
{
    phy_ir_exact_view view;
    if (!phy_ir_exact_decimal_view(cas->ir, ref, &view) ||
        view.numerator_length == 0u) {
        return 0;
    }
    if (view.numerator[0] == '-') {
        return -1;
    }
    return view.numerator_length == 1u && view.numerator[0] == '0' ? 0 : 1;
}
