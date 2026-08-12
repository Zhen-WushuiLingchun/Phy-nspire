/*
 * Reader-facing exact algebraic arithmetic.
 *
 * Root[List[a0,...,an],k] is intentionally an ordinary IR function so saved
 * notebooks remain portable.  This bridge validates and instantiates that
 * representation in the canonical complex-algebraic domain, performs one
 * bounded exact operation, and publishes a canonical rational, I, or Root.
 */
#include <limits.h>
#include <string.h>

#include "cas_internal.h"
#include "phy/algebraic.h"

enum { CAS_ALGEBRAIC_MAX_COEFFICIENTS = 33u };

typedef struct {
    bool recognized;
    bool contains_root;
} algebraic_match;

static bool node_contains_root(phy_cas *cas, phy_ir_ref ref)
{
    const phy_ir_symbol root = phy_ir_intern(cas->ir, "Root");
    if (phy_ir_kind_of(cas->ir, ref) == PHY_IR_FUNCTION &&
        phy_ir_head(cas->ir, ref) == root) {
        return true;
    }
    const size_t count = phy_ir_child_count(cas->ir, ref);
    for (size_t index = 0u; index < count; ++index) {
        if (node_contains_root(cas, phy_ir_child(cas->ir, ref, index))) {
            return true;
        }
    }
    return false;
}

static phy_algebraic_context *operation_context(phy_cas *cas)
{
    if (cas->steps >= cas->limits.max_steps) {
        return NULL;
    }
    phy_algebraic_limits limits;
    phy_algebraic_limits_defaults(&limits);
    limits.max_steps = cas->limits.max_steps - cas->steps;
    limits.exact.max_steps = limits.max_steps;
    limits.exact.max_bytes = cas->limits.max_bytes;
    limits.max_metadata_bytes = cas->limits.max_bytes;
    phy_algebraic_context *context =
        phy_algebraic_context_create(&limits);
    if (context != NULL) {
        phy_algebraic_set_cancel(context, cas->cancelled, cas->cancel_user);
    }
    return context;
}

static phy_status close_context(phy_cas *cas,
                                phy_algebraic_context *context,
                                phy_status status)
{
    const uint64_t total = phy_algebraic_total_steps(context);
    const uint32_t charge = total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
    const phy_status charged = phy_cas_charge(cas, charge);
    phy_algebraic_context_destroy(context);
    return status == PHY_OK ? charged : status;
}

static phy_status rational_from_ref(phy_cas *cas,
                                    phy_algebraic_context *context,
                                    phy_ir_ref ref,
                                    phy_complex_algebraic **out_value)
{
    phy_ir_exact_view view;
    if (!phy_ir_exact_decimal_view(cas->ir, ref, &view)) {
        return PHY_ERR_TYPE;
    }
    if (view.numerator_length == (size_t)-1 ||
        view.denominator_length == (size_t)-1 ||
        view.numerator_length + 1u >
            (size_t)-1 - (view.denominator_length + 1u)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    const size_t numerator_bytes = view.numerator_length + 1u;
    const size_t denominator_bytes = view.denominator_length + 1u;
    const size_t bytes = numerator_bytes + denominator_bytes;
    char *text = NULL;
    phy_status status = phy_cas_temp_alloc(cas, bytes, (void **)&text);
    if (status != PHY_OK) {
        return status;
    }
    memcpy(text, view.numerator, view.numerator_length);
    text[view.numerator_length] = '\0';
    char *denominator = text + numerator_bytes;
    memcpy(denominator, view.denominator, view.denominator_length);
    denominator[view.denominator_length] = '\0';
    status = phy_complex_algebraic_from_rational(
        context, (phy_exact_rational_text){text, denominator}, out_value);
    phy_cas_temp_free(cas, text, bytes);
    return status;
}

static phy_status root_from_node(phy_cas *cas,
                                 phy_algebraic_context *context,
                                 phy_ir_ref ref,
                                 phy_complex_algebraic **out_value)
{
    if (phy_ir_child_count(cas->ir, ref) != 2u) {
        return PHY_ERR_PARSE;
    }
    const phy_ir_ref coefficients = phy_ir_child(cas->ir, ref, 0u);
    const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
    if (phy_ir_kind_of(cas->ir, coefficients) != PHY_IR_FUNCTION ||
        phy_ir_head(cas->ir, coefficients) != list) {
        return PHY_ERR_TYPE;
    }
    const size_t count = phy_ir_child_count(cas->ir, coefficients);
    if (count < 2u || count > CAS_ALGEBRAIC_MAX_COEFFICIENTS) {
        return PHY_ERR_TERM_LIMIT;
    }
    int64_t ordinal = 0;
    if (!phy_ir_integer_value(
            cas->ir, phy_ir_child(cas->ir, ref, 1u), &ordinal) ||
        ordinal <= 0 || ordinal > UINT32_MAX) {
        return PHY_ERR_DOMAIN;
    }

    phy_ir_exact_view views[CAS_ALGEBRAIC_MAX_COEFFICIENTS];
    const char *texts[CAS_ALGEBRAIC_MAX_COEFFICIENTS];
    size_t bytes = 0u;
    for (size_t index = 0u; index < count; ++index) {
        const phy_ir_ref coefficient =
            phy_ir_child(cas->ir, coefficients, index);
        if (phy_ir_kind_of(cas->ir, coefficient) != PHY_IR_INTEGER ||
            !phy_ir_exact_decimal_view(cas->ir, coefficient, &views[index]) ||
            views[index].denominator_length != 1u ||
            views[index].denominator[0] != '1' ||
            views[index].numerator_length == (size_t)-1 ||
            bytes > (size_t)-1 - views[index].numerator_length - 1u) {
            return PHY_ERR_TYPE;
        }
        bytes += views[index].numerator_length + 1u;
    }
    char *storage = NULL;
    phy_status status = phy_cas_temp_alloc(cas, bytes, (void **)&storage);
    if (status != PHY_OK) {
        return status;
    }
    char *cursor = storage;
    for (size_t index = 0u; index < count; ++index) {
        texts[index] = cursor;
        memcpy(cursor, views[index].numerator, views[index].numerator_length);
        cursor[views[index].numerator_length] = '\0';
        cursor += views[index].numerator_length + 1u;
    }
    status = phy_complex_algebraic_create_by_index(
        context, texts, count, (uint32_t)ordinal, out_value);
    phy_cas_temp_free(cas, storage, bytes);
    return status;
}

static phy_status algebraic_from_node(phy_cas *cas,
                                      phy_algebraic_context *context,
                                      phy_ir_ref ref,
                                      phy_complex_algebraic **out_value,
                                      algebraic_match *out_match)
{
    *out_value = NULL;
    out_match->recognized = false;
    out_match->contains_root = false;
    if (phy_cas_is_exact(cas, ref)) {
        const phy_status status =
            rational_from_ref(cas, context, ref, out_value);
        out_match->recognized = status == PHY_OK;
        return status;
    }
    if (ref == cas->constant_i) {
        static const char *i_polynomial[] = {"1", "0", "1"};
        const phy_status status = phy_complex_algebraic_create_by_index(
            context, i_polynomial, 3u, 2u, out_value);
        out_match->recognized = status == PHY_OK;
        return status;
    }

    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, ref);
    const phy_ir_symbol root = phy_ir_intern(cas->ir, "Root");
    if (kind == PHY_IR_FUNCTION && phy_ir_head(cas->ir, ref) == root) {
        const phy_status status = root_from_node(cas, context, ref, out_value);
        out_match->recognized = status == PHY_OK;
        out_match->contains_root = true;
        return status;
    }
    if (kind != PHY_IR_ADD && kind != PHY_IR_MUL && kind != PHY_IR_POW) {
        return PHY_OK;
    }

    if (kind == PHY_IR_POW) {
        int64_t exponent = 0;
        if (!phy_ir_integer_value(
                cas->ir, phy_ir_child(cas->ir, ref, 1u), &exponent) ||
            exponent < INT32_MIN || exponent > INT32_MAX) {
            return PHY_OK;
        }
        phy_complex_algebraic *base = NULL;
        algebraic_match base_match;
        phy_status status = algebraic_from_node(
            cas, context, phy_ir_child(cas->ir, ref, 0u),
            &base, &base_match);
        if (status == PHY_OK && base_match.recognized) {
            status = phy_complex_algebraic_pow_i32(
                base, (int32_t)exponent, out_value);
        }
        phy_complex_algebraic_destroy(base);
        if (status == PHY_OK && base_match.recognized) {
            out_match->recognized = true;
            out_match->contains_root = base_match.contains_root;
        }
        return status;
    }

    const char *zero = "0";
    const char *one = "1";
    phy_complex_algebraic *accumulator = NULL;
    phy_status status = phy_complex_algebraic_from_rational(
        context,
        (phy_exact_rational_text){kind == PHY_IR_ADD ? zero : one, one},
        &accumulator);
    bool recognized = status == PHY_OK;
    bool contains_root = false;
    const size_t count = phy_ir_child_count(cas->ir, ref);
    for (size_t index = 0u;
         index < count && status == PHY_OK && recognized; ++index) {
        phy_complex_algebraic *operand = NULL;
        phy_complex_algebraic *next = NULL;
        algebraic_match match;
        status = algebraic_from_node(
            cas, context, phy_ir_child(cas->ir, ref, index),
            &operand, &match);
        recognized = status == PHY_OK && match.recognized;
        contains_root = contains_root || match.contains_root;
        if (recognized) {
            status = kind == PHY_IR_ADD
                ? phy_complex_algebraic_add(accumulator, operand, &next)
                : phy_complex_algebraic_multiply(accumulator, operand, &next);
        }
        phy_complex_algebraic_destroy(operand);
        if (status == PHY_OK && recognized) {
            phy_complex_algebraic_destroy(accumulator);
            accumulator = next;
        } else {
            phy_complex_algebraic_destroy(next);
        }
    }
    if (status == PHY_OK && recognized) {
        *out_value = accumulator;
        out_match->recognized = true;
        out_match->contains_root = contains_root;
    } else {
        phy_complex_algebraic_destroy(accumulator);
    }
    return status;
}

static phy_status publish_algebraic(phy_cas *cas,
                                    const phy_complex_algebraic *value,
                                    phy_ir_ref *out_ref)
{
    const size_t degree = phy_complex_algebraic_degree(value);
    if (degree == 0u || degree + 1u > CAS_ALGEBRAIC_MAX_COEFFICIENTS) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    const size_t count = degree + 1u;
    size_t bytes = 0u;
    size_t required[CAS_ALGEBRAIC_MAX_COEFFICIENTS];
    for (size_t index = 0u; index < count; ++index) {
        phy_status status = phy_complex_algebraic_write_coefficient(
            value, index, NULL, 0u, &required[index]);
        if (status != PHY_OK || required[index] == 0u ||
            bytes > (size_t)-1 - required[index]) {
            return status != PHY_OK ? status : PHY_ERR_MEMORY_LIMIT;
        }
        bytes += required[index];
    }
    char *storage = NULL;
    phy_status status = phy_cas_temp_alloc(cas, bytes, (void **)&storage);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref coefficient_refs[CAS_ALGEBRAIC_MAX_COEFFICIENTS];
    char *cursor = storage;
    for (size_t index = 0u; index < count && status == PHY_OK; ++index) {
        size_t written = 0u;
        status = phy_complex_algebraic_write_coefficient(
            value, index, cursor, required[index], &written);
        if (status == PHY_OK && written != required[index]) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        }
        if (status == PHY_OK) {
            coefficient_refs[index] = phy_ir_integer_text_n(
                cas->ir, cursor, written - 1u);
            if (coefficient_refs[index] == PHY_IR_NULL) {
                status = phy_cas_ir_failure(cas);
            }
        }
        cursor += required[index];
    }

    if (status == PHY_OK && degree == 1u) {
        phy_ir_ref numerator = PHY_IR_NULL;
        status = phy_cas_neg_node(cas, coefficient_refs[0], &numerator);
        if (status == PHY_OK) {
            status = phy_cas_exact_div_refs(
                cas, numerator, coefficient_refs[1], out_ref);
        }
    } else if (status == PHY_OK && degree == 2u &&
               phy_cas_is_integer(cas, coefficient_refs[0], 1) &&
               phy_cas_is_integer(cas, coefficient_refs[1], 0) &&
               phy_cas_is_integer(cas, coefficient_refs[2], 1)) {
        const uint32_t index = phy_complex_algebraic_root_index(value);
        if (index == 2u) {
            *out_ref = cas->constant_i;
        } else if (index == 1u) {
            const phy_ir_ref factors[2] = {cas->minus_one, cas->constant_i};
            *out_ref = phy_ir_mul(cas->ir, factors, 2u);
            if (*out_ref == PHY_IR_NULL) {
                status = phy_cas_ir_failure(cas);
            }
        } else {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        }
    } else if (status == PHY_OK) {
        const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
        const phy_ir_symbol root = phy_ir_intern(cas->ir, "Root");
        const phy_ir_ref coefficients = phy_ir_function(
            cas->ir, list, coefficient_refs, count);
        const phy_ir_ref ordinal = phy_ir_integer(
            cas->ir, (int64_t)phy_complex_algebraic_root_index(value));
        const phy_ir_ref arguments[2] = {coefficients, ordinal};
        *out_ref = coefficients == PHY_IR_NULL || ordinal == PHY_IR_NULL
            ? PHY_IR_NULL
            : phy_ir_function(cas->ir, root, arguments, 2u);
        if (list == PHY_IR_NO_SYMBOL || root == PHY_IR_NO_SYMBOL ||
            *out_ref == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
        }
    }
    phy_cas_temp_free(cas, storage, bytes);
    return status;
}

static phy_status fold_expression(phy_cas *cas, phy_ir_ref expression,
                                  phy_ir_ref *out_ref,
                                  bool *out_matched)
{
    *out_matched = false;
    if (!node_contains_root(cas, expression)) {
        return PHY_OK;
    }
    phy_algebraic_context *context = operation_context(cas);
    if (context == NULL) {
        return cas->steps >= cas->limits.max_steps
            ? PHY_ERR_TIMEOUT : PHY_ERR_OUT_OF_MEMORY;
    }
    phy_complex_algebraic *value = NULL;
    algebraic_match match;
    phy_status status = algebraic_from_node(
        cas, context, expression, &value, &match);
    if (status == PHY_OK && match.recognized && match.contains_root) {
        status = publish_algebraic(cas, value, out_ref);
        *out_matched = status == PHY_OK;
    }
    phy_complex_algebraic_destroy(value);
    return close_context(cas, context, status);
}

phy_status phy_cas_algebraic_fold_at(phy_cas *cas, size_t offset,
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
        candidate = node_contains_root(
            cas, phy_cas_scratch_at(cas, offset)[index]);
    }
    if (!candidate) {
        return PHY_OK;
    }
    const phy_ir_ref expression = sum
        ? phy_ir_add(cas->ir, phy_cas_scratch_at(cas, offset), count)
        : phy_ir_mul(cas->ir, phy_cas_scratch_at(cas, offset), count);
    if (expression == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    return fold_expression(cas, expression, out_ref, out_matched);
}

phy_status phy_cas_algebraic_pow_node(phy_cas *cas, phy_ir_ref base,
                                      int64_t exponent,
                                      phy_ir_ref *out_ref,
                                      bool *out_matched)
{
    if (cas == NULL || out_ref == NULL || out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;
    if (exponent < INT32_MIN || exponent > INT32_MAX ||
        !node_contains_root(cas, base)) {
        return PHY_OK;
    }
    const phy_ir_ref exponent_ref = phy_ir_integer(cas->ir, exponent);
    const phy_ir_ref expression = phy_ir_pow(cas->ir, base, exponent_ref);
    if (expression == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    return fold_expression(cas, expression, out_ref, out_matched);
}

phy_status phy_cas_algebraic_div_node(
    phy_cas *cas, phy_ir_ref numerator, phy_ir_ref denominator,
    phy_ir_ref *out_ref, bool *out_matched)
{
    if (cas == NULL || out_ref == NULL || out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;
    if (!node_contains_root(cas, numerator) &&
        !node_contains_root(cas, denominator)) {
        return PHY_OK;
    }
    phy_algebraic_context *context = operation_context(cas);
    if (context == NULL) {
        return cas->steps >= cas->limits.max_steps
            ? PHY_ERR_TIMEOUT : PHY_ERR_OUT_OF_MEMORY;
    }
    phy_complex_algebraic *left = NULL;
    phy_complex_algebraic *right = NULL;
    phy_complex_algebraic *quotient = NULL;
    algebraic_match left_match;
    algebraic_match right_match;
    phy_status status = algebraic_from_node(
        cas, context, numerator, &left, &left_match);
    if (status == PHY_OK) {
        status = algebraic_from_node(
            cas, context, denominator, &right, &right_match);
    }
    const bool recognized = status == PHY_OK &&
        left_match.recognized && right_match.recognized &&
        (left_match.contains_root || right_match.contains_root);
    if (recognized) {
        status = phy_complex_algebraic_divide(left, right, &quotient);
    }
    if (status == PHY_OK && recognized) {
        status = publish_algebraic(cas, quotient, out_ref);
        *out_matched = status == PHY_OK;
    }
    phy_complex_algebraic_destroy(quotient);
    phy_complex_algebraic_destroy(right);
    phy_complex_algebraic_destroy(left);
    return close_context(cas, context, status);
}

phy_status phy_cas_algebraic_root_function(
    phy_cas *cas, phy_ir_symbol head, const phy_ir_ref *arguments,
    size_t count, phy_ir_ref *out_ref, bool *out_matched)
{
    if (cas == NULL || arguments == NULL || out_ref == NULL ||
        out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;
    if (head != phy_ir_intern(cas->ir, "Root")) {
        return PHY_OK;
    }
    if (count != 2u) {
        return PHY_ERR_PARSE;
    }
    const phy_ir_ref expression = phy_ir_function(
        cas->ir, head, arguments, count);
    if (expression == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    return fold_expression(cas, expression, out_ref, out_matched);
}

phy_status phy_cas_algebraic_function(phy_cas *cas, phy_ir_symbol head,
                                      phy_ir_ref argument,
                                      phy_ir_ref *out_ref,
                                      bool *out_matched)
{
    if (cas == NULL || out_ref == NULL || out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;
    if (head != cas->fn_conjugate || !node_contains_root(cas, argument)) {
        return PHY_OK;
    }
    phy_algebraic_context *context = operation_context(cas);
    if (context == NULL) {
        return cas->steps >= cas->limits.max_steps
            ? PHY_ERR_TIMEOUT : PHY_ERR_OUT_OF_MEMORY;
    }
    phy_complex_algebraic *value = NULL;
    phy_complex_algebraic *conjugate = NULL;
    algebraic_match match;
    phy_status status = algebraic_from_node(
        cas, context, argument, &value, &match);
    if (status == PHY_OK && match.recognized && match.contains_root) {
        status = phy_complex_algebraic_conjugate(value, &conjugate);
    }
    if (status == PHY_OK && match.recognized && match.contains_root) {
        status = publish_algebraic(cas, conjugate, out_ref);
        *out_matched = status == PHY_OK;
    }
    phy_complex_algebraic_destroy(conjugate);
    phy_complex_algebraic_destroy(value);
    return close_context(cas, context, status);
}

phy_status phy_cas_algebraic_decide_zero(
    phy_cas *cas, phy_ir_ref expression,
    phy_cas_decision *out_decision, bool *out_matched)
{
    if (cas == NULL || out_decision == NULL || out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;
    if (!node_contains_root(cas, expression)) return PHY_OK;
    /* Simplification canonicalizes every standalone Root first.  A published
     * Root has irreducible degree at least two (rational results were already
     * collapsed), hence cannot be zero.  This O(1) certificate is important
     * to exact Gaussian elimination: pivot tests must not re-isolate the same
     * algebraic number for every row. */
    if (phy_ir_kind_of(cas->ir, expression) == PHY_IR_FUNCTION &&
        phy_ir_head(cas->ir, expression) == phy_ir_intern(cas->ir, "Root")) {
        *out_decision = PHY_CAS_NONZERO;
        *out_matched = true;
        return PHY_OK;
    }
    phy_algebraic_context *context = operation_context(cas);
    if (context == NULL) {
        return cas->steps >= cas->limits.max_steps
            ? PHY_ERR_TIMEOUT : PHY_ERR_OUT_OF_MEMORY;
    }
    phy_complex_algebraic *value = NULL;
    algebraic_match match;
    phy_status status = algebraic_from_node(
        cas, context, expression, &value, &match);
    phy_ir_ref canonical = PHY_IR_NULL;
    if (status == PHY_OK && match.recognized && match.contains_root) {
        status = publish_algebraic(cas, value, &canonical);
    }
    if (status == PHY_OK && match.recognized && match.contains_root) {
        *out_decision = phy_cas_is_integer(cas, canonical, 0)
            ? PHY_CAS_ZERO : PHY_CAS_NONZERO;
        *out_matched = true;
    }
    phy_complex_algebraic_destroy(value);
    return close_context(cas, context, status);
}
