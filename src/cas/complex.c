/*
 * Exact complex-number bridge for the scalar CAS.
 *
 * The IR deliberately keeps I as an ordinary protected symbol.  This file
 * recognizes the closed Q(i) fragment -- exact rationals, I, and finite
 * Add/Mul/integer-Pow trees -- evaluates it through the bounded arbitrary-
 * precision Gaussian kernel, then publishes the unique a + b*I normal form.
 * No floating-point approximation or sign sampling enters this path.
 */
#include <limits.h>
#include <string.h>

#include "cas_internal.h"

typedef struct {
    bool recognized;
    bool contains_i;
} gaussian_match;

static bool node_contains_i(const phy_cas *cas, phy_ir_ref ref)
{
    if (ref == cas->constant_i) {
        return true;
    }
    const size_t count = phy_ir_child_count(cas->ir, ref);
    for (size_t index = 0u; index < count; ++index) {
        if (node_contains_i(
                cas, phy_ir_child(cas->ir, ref, index))) {
            return true;
        }
    }
    return false;
}

static phy_status gaussian_from_node(phy_cas *cas,
                                     phy_exact_context *exact,
                                     phy_ir_ref ref,
                                     phy_gaussian *out_value,
                                     gaussian_match *out_match)
{
    out_match->recognized = false;
    out_match->contains_i = false;

    if (phy_cas_is_exact(cas, ref)) {
        phy_status status = phy_cas_exact_load_ref(
            cas, exact, ref, &out_value->real);
        if (status == PHY_OK) {
            status = phy_bigrat_set_i64(
                &out_value->imaginary, 0, 1);
        }
        if (status == PHY_OK) {
            out_match->recognized = true;
        }
        return status;
    }
    if (ref == cas->constant_i) {
        const phy_status status =
            phy_gaussian_set_i64(out_value, 0, 1, 1, 1);
        if (status == PHY_OK) {
            out_match->recognized = true;
            out_match->contains_i = true;
        }
        return status;
    }

    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, ref);
    if (kind != PHY_IR_ADD && kind != PHY_IR_MUL &&
        kind != PHY_IR_POW) {
        return PHY_OK;
    }

    phy_gaussian accumulator;
    phy_gaussian child;
    memset(&accumulator, 0, sizeof accumulator);
    memset(&child, 0, sizeof child);
    phy_status status = phy_gaussian_init(exact, &accumulator);
    if (status == PHY_OK) {
        status = phy_gaussian_init(exact, &child);
    }

    bool recognized = status == PHY_OK;
    bool contains_i = false;
    if (status == PHY_OK && kind == PHY_IR_POW) {
        int64_t exponent = 0;
        if (!phy_ir_integer_value(
                cas->ir, phy_ir_child(cas->ir, ref, 1u), &exponent) ||
            exponent < INT32_MIN || exponent > INT32_MAX) {
            recognized = false;
        } else {
            gaussian_match base_match;
            status = gaussian_from_node(
                cas, exact, phy_ir_child(cas->ir, ref, 0u),
                &child, &base_match);
            recognized = status == PHY_OK && base_match.recognized;
            contains_i = base_match.contains_i;
            if (recognized) {
                status = phy_gaussian_pow_i32(
                    &child, (int32_t)exponent, &accumulator);
            }
        }
    } else if (status == PHY_OK) {
        status = phy_gaussian_set_i64(
            &accumulator, kind == PHY_IR_ADD ? 0 : 1, 1, 0, 1);
        const size_t count = phy_ir_child_count(cas->ir, ref);
        for (size_t index = 0u;
             index < count && status == PHY_OK && recognized;
             ++index) {
            gaussian_match child_match;
            status = gaussian_from_node(
                cas, exact, phy_ir_child(cas->ir, ref, index),
                &child, &child_match);
            recognized = status == PHY_OK && child_match.recognized;
            contains_i = contains_i || child_match.contains_i;
            if (recognized) {
                status = kind == PHY_IR_ADD
                             ? phy_gaussian_add(
                                   &accumulator, &child, &accumulator)
                             : phy_gaussian_multiply(
                                   &accumulator, &child, &accumulator);
            }
        }
    }

    if (status == PHY_OK && recognized) {
        status = phy_gaussian_copy(&accumulator, out_value);
    }
    phy_gaussian_destroy(&child);
    phy_gaussian_destroy(&accumulator);
    if (status == PHY_OK && recognized) {
        out_match->recognized = true;
        out_match->contains_i = contains_i;
    }
    return status;
}

static phy_status gaussian_publish(phy_cas *cas,
                                   const phy_gaussian *value,
                                   phy_ir_ref *out_ref)
{
    const phy_bigrat *real = phy_gaussian_real(value);
    const phy_bigrat *imaginary = phy_gaussian_imaginary(value);
    if (real == NULL || imaginary == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    const bool real_zero = phy_bigrat_sign(real) == 0;
    const bool imaginary_zero = phy_bigrat_sign(imaginary) == 0;
    if (imaginary_zero) {
        return phy_cas_exact_publish_bigrat(cas, real, out_ref);
    }

    phy_ir_ref imaginary_coefficient = PHY_IR_NULL;
    phy_status status = phy_cas_exact_publish_bigrat(
        cas, imaginary, &imaginary_coefficient);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref imaginary_term = cas->constant_i;
    if (!phy_cas_is_integer(cas, imaginary_coefficient, 1)) {
        const phy_ir_ref factors[2] = {
            imaginary_coefficient, cas->constant_i};
        imaginary_term = phy_ir_mul(cas->ir, factors, 2u);
        if (imaginary_term == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
    }
    if (real_zero) {
        *out_ref = imaginary_term;
        return PHY_OK;
    }

    phy_ir_ref real_ref = PHY_IR_NULL;
    status = phy_cas_exact_publish_bigrat(cas, real, &real_ref);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref terms[2] = {real_ref, imaginary_term};
    const phy_ir_ref sum = phy_ir_add(cas->ir, terms, 2u);
    if (sum == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = sum;
    return PHY_OK;
}

phy_status phy_cas_gaussian_fold_at(phy_cas *cas, size_t offset,
                                    size_t count, bool sum,
                                    phy_ir_ref *out_ref,
                                    bool *out_matched)
{
    if (cas == NULL || out_ref == NULL || out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;
    bool candidate = false;
    for (size_t index = 0u; index < count && !candidate; ++index) {
        candidate = node_contains_i(
            cas, phy_cas_scratch_at(cas, offset)[index]);
    }
    if (!candidate) {
        return PHY_OK;
    }
    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }

    phy_gaussian accumulator;
    phy_gaussian operand;
    memset(&accumulator, 0, sizeof accumulator);
    memset(&operand, 0, sizeof operand);
    phy_status status = phy_gaussian_init(exact, &accumulator);
    if (status == PHY_OK) {
        status = phy_gaussian_init(exact, &operand);
    }
    if (status == PHY_OK) {
        status = phy_gaussian_set_i64(
            &accumulator, sum ? 0 : 1, 1, 0, 1);
    }

    bool recognized = status == PHY_OK;
    bool contains_i = false;
    for (size_t index = 0u;
         index < count && status == PHY_OK && recognized;
         ++index) {
        gaussian_match match;
        status = gaussian_from_node(
            cas, exact, phy_cas_scratch_at(cas, offset)[index],
            &operand, &match);
        recognized = status == PHY_OK && match.recognized;
        contains_i = contains_i || match.contains_i;
        if (recognized) {
            status =
                sum ? phy_gaussian_add(
                          &accumulator, &operand, &accumulator)
                    : phy_gaussian_multiply(
                          &accumulator, &operand, &accumulator);
        }
    }
    if (status == PHY_OK && recognized && contains_i) {
        status = gaussian_publish(cas, &accumulator, out_ref);
        *out_matched = status == PHY_OK;
    }
    phy_gaussian_destroy(&operand);
    phy_gaussian_destroy(&accumulator);
    phy_exact_context_destroy(exact);
    return status;
}

phy_status phy_cas_gaussian_pow_node(phy_cas *cas, phy_ir_ref base,
                                     int64_t exponent,
                                     phy_ir_ref *out_ref,
                                     bool *out_matched)
{
    if (cas == NULL || out_ref == NULL || out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;
    if (exponent < INT32_MIN || exponent > INT32_MAX) {
        return PHY_OK;
    }
    if (!node_contains_i(cas, base)) {
        return PHY_OK;
    }
    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_gaussian value;
    phy_gaussian power;
    memset(&value, 0, sizeof value);
    memset(&power, 0, sizeof power);
    phy_status status = phy_gaussian_init(exact, &value);
    if (status == PHY_OK) {
        status = phy_gaussian_init(exact, &power);
    }
    gaussian_match match = {false, false};
    if (status == PHY_OK) {
        status = gaussian_from_node(
            cas, exact, base, &value, &match);
    }
    if (status == PHY_OK && match.recognized && match.contains_i) {
        status = phy_gaussian_pow_i32(
            &value, (int32_t)exponent, &power);
        if (status == PHY_OK) {
            status = gaussian_publish(cas, &power, out_ref);
        }
        *out_matched = status == PHY_OK;
    }
    phy_gaussian_destroy(&power);
    phy_gaussian_destroy(&value);
    phy_exact_context_destroy(exact);
    return status;
}

phy_status phy_cas_gaussian_function(phy_cas *cas, phy_ir_symbol head,
                                     phy_ir_ref argument,
                                     phy_ir_ref *out_ref,
                                     bool *out_matched)
{
    if (cas == NULL || out_ref == NULL || out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;
    if (head != cas->fn_re && head != cas->fn_im &&
        head != cas->fn_conjugate && head != cas->fn_abs) {
        return PHY_OK;
    }

    phy_exact_context *exact = phy_cas_exact_operation_context(cas);
    if (exact == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_gaussian value;
    phy_gaussian conjugate;
    phy_bigrat norm;
    memset(&value, 0, sizeof value);
    memset(&conjugate, 0, sizeof conjugate);
    memset(&norm, 0, sizeof norm);
    phy_status status = phy_gaussian_init(exact, &value);
    if (status == PHY_OK) {
        status = phy_gaussian_init(exact, &conjugate);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(exact, &norm);
    }
    gaussian_match match = {false, false};
    if (status == PHY_OK) {
        status = gaussian_from_node(
            cas, exact, argument, &value, &match);
    }
    if (status == PHY_OK && match.recognized) {
        if (head == cas->fn_re) {
            status = phy_cas_exact_publish_bigrat(
                cas, phy_gaussian_real(&value), out_ref);
        } else if (head == cas->fn_im) {
            status = phy_cas_exact_publish_bigrat(
                cas, phy_gaussian_imaginary(&value), out_ref);
        } else if (head == cas->fn_conjugate) {
            status = phy_gaussian_conjugate(&value, &conjugate);
            if (status == PHY_OK) {
                status = gaussian_publish(cas, &conjugate, out_ref);
            }
        } else {
            status = phy_gaussian_norm(&value, &norm);
            phy_ir_ref norm_ref = PHY_IR_NULL;
            if (status == PHY_OK) {
                status = phy_cas_exact_publish_bigrat(
                    cas, &norm, &norm_ref);
            }
            phy_ir_ref half = PHY_IR_NULL;
            if (status == PHY_OK) {
                status = phy_cas_number_node(
                    cas, (phy_cas_rat){1, 2}, &half);
            }
            if (status == PHY_OK) {
                status = phy_cas_pow_node(
                    cas, norm_ref, half, out_ref);
            }
        }
        *out_matched = status == PHY_OK;
    }
    phy_bigrat_destroy(&norm);
    phy_gaussian_destroy(&conjugate);
    phy_gaussian_destroy(&value);
    phy_exact_context_destroy(exact);
    return status;
}
