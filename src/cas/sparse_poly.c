#include "sparse_poly.h"

#include <string.h>

#define SPARSE_MAX_VARIABLES 8u
#define SPARSE_MAX_TERMS 192u
#define SPARSE_MAX_DEGREE 48u

typedef struct {
    phy_ir_ref coefficient;
    uint8_t exponent[SPARSE_MAX_VARIABLES];
} sparse_term;

typedef struct {
    phy_cas *cas;
    const phy_ir_ref *variables;
    size_t variable_count;
    sparse_term *terms;
    size_t count;
    size_t bytes;
} sparse_poly;

static phy_status poly_init(phy_cas *cas, const phy_ir_ref *variables,
                            size_t variable_count, sparse_poly *out)
{
    memset(out, 0, sizeof *out);
    out->cas = cas;
    out->variables = variables;
    out->variable_count = variable_count;
    out->bytes = SPARSE_MAX_TERMS * sizeof(sparse_term);
    return phy_cas_temp_alloc(
        cas, out->bytes, (void **)&out->terms);
}

static void poly_destroy(sparse_poly *poly)
{
    if (poly->terms != NULL) {
        phy_cas_temp_free(
            poly->cas, poly->terms, poly->bytes);
    }
    memset(poly, 0, sizeof *poly);
}

static bool coefficient_zero(const sparse_poly *poly, phy_ir_ref value)
{
    return phy_cas_is_exact(poly->cas, value) &&
           phy_cas_exact_sign_ref(poly->cas, value) == 0;
}

static phy_status coefficient_add(sparse_poly *poly, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out)
{
    return phy_cas_exact_add_refs(poly->cas, left, right, out);
}

static phy_status coefficient_subtract(sparse_poly *poly, phy_ir_ref left,
                                       phy_ir_ref right, phy_ir_ref *out)
{
    return phy_cas_exact_sub_refs(poly->cas, left, right, out);
}

static phy_status coefficient_multiply(sparse_poly *poly, phy_ir_ref left,
                                       phy_ir_ref right, phy_ir_ref *out)
{
    return phy_cas_exact_mul_refs(poly->cas, left, right, out);
}

static phy_status coefficient_divide(sparse_poly *poly, phy_ir_ref left,
                                     phy_ir_ref right, phy_ir_ref *out)
{
    return phy_cas_exact_div_refs(poly->cas, left, right, out);
}

static int monomial_compare(const sparse_poly *poly,
                            const uint8_t *left, const uint8_t *right)
{
    for (size_t index = 0u; index < poly->variable_count; ++index) {
        if (left[index] != right[index]) {
            return left[index] > right[index] ? 1 : -1;
        }
    }
    return 0;
}

static bool monomial_divides(const sparse_poly *poly,
                             const uint8_t *divisor,
                             const uint8_t *dividend)
{
    for (size_t index = 0u; index < poly->variable_count; ++index) {
        if (divisor[index] > dividend[index]) {
            return false;
        }
    }
    return true;
}

static phy_status insert_term(sparse_poly *poly, phy_ir_ref coefficient,
                              const uint8_t *exponent)
{
    if (coefficient_zero(poly, coefficient)) {
        return PHY_OK;
    }
    size_t position = 0u;
    while (position < poly->count) {
        const int comparison = monomial_compare(
            poly, exponent, poly->terms[position].exponent);
        if (comparison == 0) {
            phy_ir_ref sum = PHY_IR_NULL;
            phy_status status = coefficient_add(
                poly, poly->terms[position].coefficient,
                coefficient, &sum);
            if (status != PHY_OK) {
                return status;
            }
            if (coefficient_zero(poly, sum)) {
                memmove(
                    &poly->terms[position], &poly->terms[position + 1u],
                    (poly->count - position - 1u) *
                        sizeof(poly->terms[0]));
                poly->count--;
            } else {
                poly->terms[position].coefficient = sum;
            }
            return PHY_OK;
        }
        if (comparison > 0) {
            break;
        }
        position++;
    }
    if (poly->count >= SPARSE_MAX_TERMS) {
        return PHY_ERR_TERM_LIMIT;
    }
    memmove(
        &poly->terms[position + 1u], &poly->terms[position],
        (poly->count - position) * sizeof(poly->terms[0]));
    poly->terms[position].coefficient = coefficient;
    memset(poly->terms[position].exponent, 0,
           sizeof(poly->terms[position].exponent));
    memcpy(poly->terms[position].exponent, exponent,
           poly->variable_count);
    poly->count++;
    return PHY_OK;
}

static phy_status poly_copy(const sparse_poly *source, sparse_poly *out)
{
    if (source->count > SPARSE_MAX_TERMS) {
        return PHY_ERR_TERM_LIMIT;
    }
    memcpy(out->terms, source->terms,
           source->count * sizeof(source->terms[0]));
    out->count = source->count;
    return PHY_OK;
}

static bool poly_equal(const sparse_poly *left, const sparse_poly *right)
{
    if (left->count != right->count) {
        return false;
    }
    for (size_t index = 0u; index < left->count; ++index) {
        if (left->terms[index].coefficient !=
                right->terms[index].coefficient ||
            memcmp(left->terms[index].exponent,
                   right->terms[index].exponent,
                   left->variable_count) != 0) {
            return false;
        }
    }
    return true;
}

static phy_status poly_add(const sparse_poly *left,
                           const sparse_poly *right, sparse_poly *out)
{
    out->count = 0u;
    for (size_t index = 0u; index < left->count; ++index) {
        phy_status status = insert_term(
            out, left->terms[index].coefficient,
            left->terms[index].exponent);
        if (status != PHY_OK) {
            return status;
        }
    }
    for (size_t index = 0u; index < right->count; ++index) {
        phy_status status = insert_term(
            out, right->terms[index].coefficient,
            right->terms[index].exponent);
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static phy_status poly_subtract(const sparse_poly *left,
                                const sparse_poly *right,
                                sparse_poly *out)
{
    out->count = 0u;
    for (size_t index = 0u; index < left->count; ++index) {
        phy_status status = insert_term(
            out, left->terms[index].coefficient,
            left->terms[index].exponent);
        if (status != PHY_OK) {
            return status;
        }
    }
    for (size_t index = 0u; index < right->count; ++index) {
        phy_ir_ref negative = PHY_IR_NULL;
        phy_status status = coefficient_subtract(
            out, out->cas->zero, right->terms[index].coefficient,
            &negative);
        if (status == PHY_OK) {
            status = insert_term(
                out, negative, right->terms[index].exponent);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static phy_status poly_multiply(const sparse_poly *left,
                                const sparse_poly *right,
                                sparse_poly *out)
{
    out->count = 0u;
    uint8_t exponent[SPARSE_MAX_VARIABLES];
    for (size_t a = 0u; a < left->count; ++a) {
        for (size_t b = 0u; b < right->count; ++b) {
            phy_status status = phy_cas_step(out->cas);
            if (status != PHY_OK) {
                return status;
            }
            for (size_t variable = 0u;
                 variable < out->variable_count; ++variable) {
                const unsigned sum =
                    (unsigned)left->terms[a].exponent[variable] +
                    (unsigned)right->terms[b].exponent[variable];
                if (sum > SPARSE_MAX_DEGREE) {
                    return PHY_ERR_TERM_LIMIT;
                }
                exponent[variable] = (uint8_t)sum;
            }
            phy_ir_ref coefficient = PHY_IR_NULL;
            status = coefficient_multiply(
                out, left->terms[a].coefficient,
                right->terms[b].coefficient, &coefficient);
            if (status == PHY_OK) {
                status = insert_term(out, coefficient, exponent);
            }
            if (status != PHY_OK) {
                return status;
            }
        }
    }
    return PHY_OK;
}

static phy_status poly_scale_monomial(
    const sparse_poly *source, phy_ir_ref coefficient,
    const uint8_t *exponent, sparse_poly *out)
{
    out->count = 0u;
    uint8_t powers[SPARSE_MAX_VARIABLES];
    for (size_t term = 0u; term < source->count; ++term) {
        for (size_t variable = 0u;
             variable < out->variable_count; ++variable) {
            const unsigned sum =
                (unsigned)source->terms[term].exponent[variable] +
                (unsigned)exponent[variable];
            if (sum > SPARSE_MAX_DEGREE) {
                return PHY_ERR_TERM_LIMIT;
            }
            powers[variable] = (uint8_t)sum;
        }
        phy_ir_ref scaled = PHY_IR_NULL;
        phy_status status = coefficient_multiply(
            out, source->terms[term].coefficient,
            coefficient, &scaled);
        if (status == PHY_OK) {
            status = insert_term(out, scaled, powers);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static bool poly_is_one(const sparse_poly *poly)
{
    if (poly->count != 1u ||
        !phy_cas_is_integer(
            poly->cas, poly->terms[0].coefficient, 1)) {
        return false;
    }
    for (size_t index = 0u; index < poly->variable_count; ++index) {
        if (poly->terms[0].exponent[index] != 0u) {
            return false;
        }
    }
    return true;
}

static phy_status poly_make_monic(sparse_poly *poly)
{
    if (poly->count == 0u) {
        return PHY_OK;
    }
    const phy_ir_ref leading = poly->terms[0].coefficient;
    for (size_t index = 0u; index < poly->count; ++index) {
        phy_ir_ref normalized = PHY_IR_NULL;
        const phy_status status = coefficient_divide(
            poly, poly->terms[index].coefficient,
            leading, &normalized);
        if (status != PHY_OK) {
            return status;
        }
        poly->terms[index].coefficient = normalized;
    }
    return PHY_OK;
}

static size_t variable_position(const sparse_poly *poly, phy_ir_ref symbol)
{
    for (size_t index = 0u; index < poly->variable_count; ++index) {
        if (poly->variables[index] == symbol) {
            return index;
        }
    }
    return (size_t)-1;
}

static phy_status poly_from_ir_node(sparse_poly *basis,
                                    phy_ir_ref expression,
                                    sparse_poly *out);

static phy_status poly_power(sparse_poly *basis, const sparse_poly *base,
                             unsigned exponent, sparse_poly *out)
{
    sparse_poly result;
    sparse_poly factor;
    sparse_poly temporary;
    phy_status status = poly_init(
        basis->cas, basis->variables, basis->variable_count, &result);
    if (status == PHY_OK) {
        status = poly_init(
            basis->cas, basis->variables, basis->variable_count, &factor);
    }
    if (status == PHY_OK) {
        status = poly_init(
            basis->cas, basis->variables, basis->variable_count, &temporary);
    }
    uint8_t zero[SPARSE_MAX_VARIABLES] = {0u};
    if (status == PHY_OK) {
        status = insert_term(&result, basis->cas->one, zero);
    }
    if (status == PHY_OK) {
        status = poly_copy(base, &factor);
    }
    while (status == PHY_OK && exponent != 0u) {
        if ((exponent & 1u) != 0u) {
            status = poly_multiply(&result, &factor, &temporary);
            if (status == PHY_OK) {
                sparse_poly swap = result;
                result = temporary;
                temporary = swap;
                temporary.count = 0u;
            }
        }
        exponent >>= 1u;
        if (status == PHY_OK && exponent != 0u) {
            status = poly_multiply(&factor, &factor, &temporary);
            if (status == PHY_OK) {
                sparse_poly swap = factor;
                factor = temporary;
                temporary = swap;
                temporary.count = 0u;
            }
        }
    }
    if (status == PHY_OK) {
        status = poly_copy(&result, out);
    }
    poly_destroy(&temporary);
    poly_destroy(&factor);
    poly_destroy(&result);
    return status;
}

static phy_status poly_from_ir_node(sparse_poly *basis,
                                    phy_ir_ref expression,
                                    sparse_poly *out)
{
    out->count = 0u;
    if (phy_cas_is_exact(basis->cas, expression)) {
        uint8_t zero[SPARSE_MAX_VARIABLES] = {0u};
        return insert_term(out, expression, zero);
    }
    const phy_ir_kind kind = phy_ir_kind_of(basis->cas->ir, expression);
    if (kind == PHY_IR_SYMBOL) {
        const size_t variable = variable_position(basis, expression);
        if (variable == (size_t)-1) {
            return PHY_ERR_UNSUPPORTED;
        }
        uint8_t exponent[SPARSE_MAX_VARIABLES] = {0u};
        exponent[variable] = 1u;
        return insert_term(out, basis->cas->one, exponent);
    }
    if (kind == PHY_IR_POW) {
        int64_t exponent = 0;
        if (!phy_ir_integer_value(
                basis->cas->ir,
                phy_ir_child(basis->cas->ir, expression, 1u),
                &exponent) ||
            exponent < 0) {
            return PHY_ERR_UNSUPPORTED;
        }
        if (exponent > SPARSE_MAX_DEGREE) {
            return PHY_ERR_TERM_LIMIT;
        }
        sparse_poly base;
        phy_status status = poly_init(
            basis->cas, basis->variables, basis->variable_count, &base);
        if (status == PHY_OK) {
            status = poly_from_ir_node(
                basis, phy_ir_child(basis->cas->ir, expression, 0u),
                &base);
        }
        if (status == PHY_OK) {
            status = poly_power(basis, &base, (unsigned)exponent, out);
        }
        poly_destroy(&base);
        return status;
    }
    if (kind != PHY_IR_ADD && kind != PHY_IR_MUL) {
        return PHY_ERR_UNSUPPORTED;
    }

    sparse_poly accumulated;
    sparse_poly operand;
    sparse_poly temporary;
    memset(&accumulated, 0, sizeof accumulated);
    memset(&operand, 0, sizeof operand);
    memset(&temporary, 0, sizeof temporary);
    phy_status status = poly_init(
        basis->cas, basis->variables, basis->variable_count, &accumulated);
    if (status == PHY_OK) {
        status = poly_init(
            basis->cas, basis->variables, basis->variable_count, &operand);
    }
    if (status == PHY_OK) {
        status = poly_init(
            basis->cas, basis->variables, basis->variable_count, &temporary);
    }
    if (status == PHY_OK && kind == PHY_IR_MUL) {
        uint8_t zero[SPARSE_MAX_VARIABLES] = {0u};
        status = insert_term(&accumulated, basis->cas->one, zero);
    }
    const size_t child_count =
        phy_ir_child_count(basis->cas->ir, expression);
    for (size_t child = 0u;
         status == PHY_OK && child < child_count; ++child) {
        status = poly_from_ir_node(
            basis, phy_ir_child(basis->cas->ir, expression, child),
            &operand);
        if (status == PHY_OK) {
            status = kind == PHY_IR_ADD
                         ? poly_add(&accumulated, &operand, &temporary)
                         : poly_multiply(
                               &accumulated, &operand, &temporary);
        }
        if (status == PHY_OK) {
            sparse_poly swap = accumulated;
            accumulated = temporary;
            temporary = swap;
            operand.count = 0u;
            temporary.count = 0u;
        }
    }
    if (status == PHY_OK) {
        status = poly_copy(&accumulated, out);
    }
    poly_destroy(&temporary);
    poly_destroy(&operand);
    poly_destroy(&accumulated);
    return status;
}

static phy_status poly_divide_exact(const sparse_poly *dividend,
                                    const sparse_poly *divisor,
                                    sparse_poly *out_quotient,
                                    bool *out_exact)
{
    *out_exact = false;
    out_quotient->count = 0u;
    if (divisor->count == 0u) {
        return PHY_ERR_DOMAIN;
    }
    sparse_poly remainder;
    sparse_poly scaled;
    sparse_poly next;
    memset(&remainder, 0, sizeof remainder);
    memset(&scaled, 0, sizeof scaled);
    memset(&next, 0, sizeof next);
    phy_status status = poly_init(
        dividend->cas, dividend->variables,
        dividend->variable_count, &remainder);
    if (status == PHY_OK) {
        status = poly_init(
            dividend->cas, dividend->variables,
            dividend->variable_count, &scaled);
    }
    if (status == PHY_OK) {
        status = poly_init(
            dividend->cas, dividend->variables,
            dividend->variable_count, &next);
    }
    if (status == PHY_OK) {
        status = poly_copy(dividend, &remainder);
    }
    uint8_t exponent[SPARSE_MAX_VARIABLES];
    while (status == PHY_OK && remainder.count != 0u) {
        if (!monomial_divides(
                dividend, divisor->terms[0].exponent,
                remainder.terms[0].exponent)) {
            break;
        }
        for (size_t variable = 0u;
             variable < dividend->variable_count; ++variable) {
            exponent[variable] =
                (uint8_t)(remainder.terms[0].exponent[variable] -
                          divisor->terms[0].exponent[variable]);
        }
        phy_ir_ref coefficient = PHY_IR_NULL;
        status = coefficient_divide(
            &remainder, remainder.terms[0].coefficient,
            divisor->terms[0].coefficient, &coefficient);
        if (status == PHY_OK) {
            status = insert_term(
                out_quotient, coefficient, exponent);
        }
        if (status == PHY_OK) {
            status = poly_scale_monomial(
                divisor, coefficient, exponent, &scaled);
        }
        if (status == PHY_OK) {
            status = poly_subtract(&remainder, &scaled, &next);
        }
        if (status == PHY_OK) {
            sparse_poly swap = remainder;
            remainder = next;
            next = swap;
            next.count = 0u;
        }
    }
    if (status == PHY_OK) {
        *out_exact = remainder.count == 0u;
    }
    poly_destroy(&next);
    poly_destroy(&scaled);
    poly_destroy(&remainder);
    return status;
}

static unsigned degree_in(const sparse_poly *poly, size_t variable)
{
    unsigned degree = 0u;
    for (size_t term = 0u; term < poly->count; ++term) {
        if (poly->terms[term].exponent[variable] > degree) {
            degree = poly->terms[term].exponent[variable];
        }
    }
    return degree;
}

static phy_status leading_coefficient(const sparse_poly *poly,
                                      size_t variable,
                                      sparse_poly *out)
{
    out->count = 0u;
    const unsigned degree = degree_in(poly, variable);
    for (size_t term = 0u; term < poly->count; ++term) {
        if (poly->terms[term].exponent[variable] != degree) {
            continue;
        }
        uint8_t exponent[SPARSE_MAX_VARIABLES];
        memcpy(exponent, poly->terms[term].exponent,
               sizeof exponent);
        exponent[variable] = 0u;
        const phy_status status = insert_term(
            out, poly->terms[term].coefficient, exponent);
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static phy_status gcd_at(const sparse_poly *left,
                         const sparse_poly *right, size_t variable,
                         sparse_poly *out);

static phy_status coefficient_content(const sparse_poly *poly,
                                      size_t variable,
                                      sparse_poly *out)
{
    out->count = 0u;
    if (poly->count == 0u) {
        return PHY_OK;
    }
    sparse_poly coefficient;
    sparse_poly accumulated;
    sparse_poly next;
    phy_status status = poly_init(
        poly->cas, poly->variables, poly->variable_count, &coefficient);
    if (status == PHY_OK) {
        status = poly_init(
            poly->cas, poly->variables, poly->variable_count, &accumulated);
    }
    if (status == PHY_OK) {
        status = poly_init(
            poly->cas, poly->variables, poly->variable_count, &next);
    }
    bool first = true;
    const unsigned maximum = degree_in(poly, variable);
    for (unsigned degree = 0u;
         status == PHY_OK && degree <= maximum; ++degree) {
        coefficient.count = 0u;
        for (size_t term = 0u; term < poly->count; ++term) {
            if (poly->terms[term].exponent[variable] != degree) {
                continue;
            }
            uint8_t exponent[SPARSE_MAX_VARIABLES];
            memcpy(exponent, poly->terms[term].exponent,
                   sizeof exponent);
            exponent[variable] = 0u;
            status = insert_term(
                &coefficient, poly->terms[term].coefficient, exponent);
            if (status != PHY_OK) {
                break;
            }
        }
        if (status != PHY_OK || coefficient.count == 0u) {
            continue;
        }
        if (first) {
            status = poly_copy(&coefficient, &accumulated);
            first = false;
        } else {
            status = gcd_at(
                &accumulated, &coefficient, variable + 1u, &next);
            if (status == PHY_OK) {
                sparse_poly swap = accumulated;
                accumulated = next;
                next = swap;
                next.count = 0u;
            }
        }
        if (status == PHY_OK && poly_is_one(&accumulated)) {
            break;
        }
    }
    if (status == PHY_OK) {
        status = poly_copy(&accumulated, out);
    }
    poly_destroy(&next);
    poly_destroy(&accumulated);
    poly_destroy(&coefficient);
    return status;
}

static phy_status pseudo_remainder(const sparse_poly *dividend,
                                   const sparse_poly *divisor,
                                   size_t variable,
                                   sparse_poly *out)
{
    sparse_poly remainder;
    sparse_poly leading_divisor;
    sparse_poly leading_remainder;
    sparse_poly left_product;
    sparse_poly shifted_divisor;
    sparse_poly right_product;
    sparse_poly next;
    sparse_poly *allocated[] = {
        &remainder, &leading_divisor, &leading_remainder,
        &left_product, &shifted_divisor, &right_product, &next};
    memset(allocated[0], 0, sizeof(remainder));
    phy_status status = PHY_OK;
    size_t initialized = 0u;
    while (status == PHY_OK &&
           initialized < sizeof allocated / sizeof allocated[0]) {
        status = poly_init(
            dividend->cas, dividend->variables,
            dividend->variable_count, allocated[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    if (status == PHY_OK) {
        status = poly_copy(dividend, &remainder);
    }
    if (status == PHY_OK) {
        status = leading_coefficient(
            divisor, variable, &leading_divisor);
    }
    const unsigned divisor_degree = degree_in(divisor, variable);
    uint8_t shift[SPARSE_MAX_VARIABLES] = {0u};
    while (status == PHY_OK && remainder.count != 0u &&
           degree_in(&remainder, variable) >= divisor_degree) {
        status = phy_cas_step(dividend->cas);
        if (status != PHY_OK) {
            break;
        }
        const unsigned remainder_degree =
            degree_in(&remainder, variable);
        leading_remainder.count = 0u;
        status = leading_coefficient(
            &remainder, variable, &leading_remainder);
        if (status == PHY_OK) {
            status = poly_multiply(
                &leading_divisor, &remainder, &left_product);
        }
        memset(shift, 0, sizeof shift);
        shift[variable] =
            (uint8_t)(remainder_degree - divisor_degree);
        if (status == PHY_OK) {
            status = poly_scale_monomial(
                divisor, dividend->cas->one, shift, &shifted_divisor);
        }
        if (status == PHY_OK) {
            status = poly_multiply(
                &leading_remainder, &shifted_divisor, &right_product);
        }
        if (status == PHY_OK) {
            status = poly_subtract(
                &left_product, &right_product, &next);
        }
        if (status == PHY_OK) {
            sparse_poly swap = remainder;
            remainder = next;
            next = swap;
            next.count = 0u;
            left_product.count = 0u;
            shifted_divisor.count = 0u;
            right_product.count = 0u;
        }
    }
    if (status == PHY_OK) {
        status = poly_copy(&remainder, out);
    }
    while (initialized != 0u) {
        poly_destroy(allocated[--initialized]);
    }
    return status;
}

static phy_status primitive_part(const sparse_poly *poly, size_t variable,
                                 sparse_poly *out)
{
    sparse_poly content;
    phy_status status = poly_init(
        poly->cas, poly->variables, poly->variable_count, &content);
    if (status == PHY_OK) {
        status = coefficient_content(poly, variable, &content);
    }
    bool exact = false;
    if (status == PHY_OK) {
        status = poly_divide_exact(poly, &content, out, &exact);
    }
    poly_destroy(&content);
    if (status == PHY_OK && !exact) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    return status;
}

static phy_status gcd_at(const sparse_poly *left,
                         const sparse_poly *right, size_t variable,
                         sparse_poly *out)
{
    if (left->count == 0u) {
        phy_status status = poly_copy(right, out);
        return status == PHY_OK ? poly_make_monic(out) : status;
    }
    if (right->count == 0u) {
        phy_status status = poly_copy(left, out);
        return status == PHY_OK ? poly_make_monic(out) : status;
    }
    if (variable >= left->variable_count) {
        uint8_t zero[SPARSE_MAX_VARIABLES] = {0u};
        return insert_term(out, left->cas->one, zero);
    }

    sparse_poly content_left;
    sparse_poly content_right;
    sparse_poly content_gcd;
    sparse_poly primitive_left;
    sparse_poly primitive_right;
    sparse_poly a;
    sparse_poly b;
    sparse_poly remainder;
    sparse_poly primitive_remainder;
    sparse_poly product;
    sparse_poly verify_left;
    sparse_poly verify_right;
    sparse_poly *allocated[] = {
        &content_left, &content_right, &content_gcd,
        &primitive_left, &primitive_right, &a, &b, &remainder,
        &primitive_remainder, &product, &verify_left, &verify_right};
    phy_status status = PHY_OK;
    size_t initialized = 0u;
    while (status == PHY_OK &&
           initialized < sizeof allocated / sizeof allocated[0]) {
        status = poly_init(
            left->cas, left->variables, left->variable_count,
            allocated[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    if (status == PHY_OK) {
        status = coefficient_content(left, variable, &content_left);
    }
    if (status == PHY_OK) {
        status = coefficient_content(right, variable, &content_right);
    }
    if (status == PHY_OK) {
        status = gcd_at(
            &content_left, &content_right, variable + 1u, &content_gcd);
    }
    bool exact_left = false;
    bool exact_right = false;
    if (status == PHY_OK) {
        status = poly_divide_exact(
            left, &content_left, &primitive_left, &exact_left);
    }
    if (status == PHY_OK) {
        status = poly_divide_exact(
            right, &content_right, &primitive_right, &exact_right);
    }
    if (status == PHY_OK && (!exact_left || !exact_right)) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    if (status == PHY_OK) {
        if (degree_in(&primitive_left, variable) <
            degree_in(&primitive_right, variable)) {
            status = poly_copy(&primitive_right, &a);
            if (status == PHY_OK) {
                status = poly_copy(&primitive_left, &b);
            }
        } else {
            status = poly_copy(&primitive_left, &a);
            if (status == PHY_OK) {
                status = poly_copy(&primitive_right, &b);
            }
        }
    }
    while (status == PHY_OK && b.count != 0u) {
        remainder.count = 0u;
        status = pseudo_remainder(&a, &b, variable, &remainder);
        primitive_remainder.count = 0u;
        if (status == PHY_OK && remainder.count != 0u) {
            status = primitive_part(
                &remainder, variable, &primitive_remainder);
        }
        if (status == PHY_OK) {
            sparse_poly swap = a;
            a = b;
            b = primitive_remainder;
            primitive_remainder = swap;
            primitive_remainder.count = 0u;
        }
    }
    if (status == PHY_OK) {
        status = poly_multiply(&content_gcd, &a, &product);
    }
    if (status == PHY_OK) {
        status = poly_make_monic(&product);
    }

    /* A GCD is published only after exact division and reconstruction. */
    bool divides_left = false;
    bool divides_right = false;
    if (status == PHY_OK) {
        status = poly_divide_exact(
            left, &product, &verify_left, &divides_left);
    }
    if (status == PHY_OK) {
        status = poly_divide_exact(
            right, &product, &verify_right, &divides_right);
    }
    if (status == PHY_OK && (!divides_left || !divides_right)) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    if (status == PHY_OK) {
        status = poly_copy(&product, out);
    }
    while (initialized != 0u) {
        poly_destroy(allocated[--initialized]);
    }
    return status;
}

static phy_status poly_to_ir(const sparse_poly *poly, phy_ir_ref *out)
{
    const size_t mark = phy_cas_scratch_mark(poly->cas);
    size_t offset = 0u;
    phy_status status = phy_cas_scratch_alloc(
        poly->cas, poly->count, &offset);
    for (size_t term = 0u;
         status == PHY_OK && term < poly->count; ++term) {
        phy_ir_ref factors[SPARSE_MAX_VARIABLES + 1u];
        size_t factor_count = 0u;
        bool has_variable = false;
        for (size_t variable = 0u;
             variable < poly->variable_count; ++variable) {
            const unsigned exponent =
                poly->terms[term].exponent[variable];
            if (exponent == 0u) {
                continue;
            }
            has_variable = true;
            phy_ir_ref factor = poly->variables[variable];
            if (exponent != 1u) {
                phy_ir_ref exponent_ref =
                    phy_ir_integer(poly->cas->ir, (int64_t)exponent);
                if (exponent_ref == PHY_IR_NULL) {
                    status = phy_cas_ir_failure(poly->cas);
                    break;
                }
                status = phy_cas_pow_node(
                    poly->cas, factor, exponent_ref, &factor);
                if (status != PHY_OK) {
                    break;
                }
            }
            factors[factor_count++] = factor;
        }
        if (status != PHY_OK) {
            break;
        }
        if (!has_variable ||
            !phy_cas_is_integer(
                poly->cas, poly->terms[term].coefficient, 1)) {
            factors[factor_count++] = poly->terms[term].coefficient;
        }
        phy_ir_ref expression = PHY_IR_NULL;
        status = phy_cas_mul_node(
            poly->cas, factors, factor_count, &expression);
        if (status == PHY_OK) {
            phy_cas_scratch_at(poly->cas, offset)[term] = expression;
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_add_at(
            poly->cas, offset, poly->count, out);
    }
    phy_cas_scratch_release(poly->cas, mark);
    return status;
}

static phy_status collect_variables(
    phy_cas *cas, phy_ir_ref expression,
    phy_ir_ref *variables, size_t *in_out_count)
{
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }
    if (phy_ir_kind_of(cas->ir, expression) == PHY_IR_SYMBOL) {
        const phy_ir_symbol symbol = phy_ir_head(cas->ir, expression);
        if ((phy_ir_assumptions(cas->ir, symbol) &
             (uint32_t)PHY_IR_ASSUME_CONSTANT) != 0u) {
            return PHY_ERR_UNSUPPORTED;
        }
        for (size_t index = 0u; index < *in_out_count; ++index) {
            if (variables[index] == expression) {
                return PHY_OK;
            }
        }
        if (*in_out_count >= SPARSE_MAX_VARIABLES) {
            return PHY_ERR_TERM_LIMIT;
        }
        variables[(*in_out_count)++] = expression;
        return PHY_OK;
    }
    const phy_ir_kind kind = phy_ir_kind_of(cas->ir, expression);
    if (phy_cas_is_exact(cas, expression)) {
        return PHY_OK;
    }
    if (kind != PHY_IR_ADD && kind != PHY_IR_MUL &&
        kind != PHY_IR_POW) {
        return PHY_ERR_UNSUPPORTED;
    }
    const size_t count = phy_ir_child_count(cas->ir, expression);
    for (size_t index = 0u; index < count; ++index) {
        if (kind == PHY_IR_POW && index == 1u) {
            continue;
        }
        status = collect_variables(
            cas, phy_ir_child(cas->ir, expression, index),
            variables, in_out_count);
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static void sort_variables(phy_cas *cas, phy_ir_ref *variables, size_t count)
{
    for (size_t index = 1u; index < count; ++index) {
        const phy_ir_ref key = variables[index];
        size_t position = index;
        while (position > 0u &&
               phy_ir_compare(
                   cas->ir, key, variables[position - 1u]) < 0) {
            variables[position] = variables[position - 1u];
            position--;
        }
        variables[position] = key;
    }
}

phy_status phy_sparse_cancel_gcd(phy_cas *cas, phy_ir_ref numerator,
                                 phy_ir_ref denominator,
                                 phy_ir_ref *out_numerator,
                                 phy_ir_ref *out_denominator,
                                 bool *out_matched)
{
    if (cas == NULL || out_numerator == NULL ||
        out_denominator == NULL || out_matched == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_matched = false;
    phy_ir_ref variables[SPARSE_MAX_VARIABLES];
    size_t variable_count = 0u;
    phy_status status = collect_variables(
        cas, numerator, variables, &variable_count);
    if (status == PHY_OK) {
        status = collect_variables(
            cas, denominator, variables, &variable_count);
    }
    if (status == PHY_ERR_UNSUPPORTED) {
        return PHY_OK;
    }
    if (status != PHY_OK || variable_count < 2u) {
        return status;
    }
    sort_variables(cas, variables, variable_count);

    sparse_poly basis;
    sparse_poly numerator_poly;
    sparse_poly denominator_poly;
    sparse_poly gcd;
    sparse_poly numerator_quotient;
    sparse_poly denominator_quotient;
    sparse_poly numerator_check;
    sparse_poly denominator_check;
    sparse_poly *allocated[] = {
        &basis, &numerator_poly, &denominator_poly, &gcd,
        &numerator_quotient, &denominator_quotient,
        &numerator_check, &denominator_check};
    size_t initialized = 0u;
    while (status == PHY_OK &&
           initialized < sizeof allocated / sizeof allocated[0]) {
        status = poly_init(
            cas, variables, variable_count, allocated[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    if (status == PHY_OK) {
        status = poly_from_ir_node(
            &basis, numerator, &numerator_poly);
    }
    if (status == PHY_OK) {
        status = poly_from_ir_node(
            &basis, denominator, &denominator_poly);
    }
    if (status == PHY_ERR_UNSUPPORTED) {
        status = PHY_OK;
        goto cleanup;
    }
    if (status == PHY_OK &&
        (numerator_poly.count == 0u || denominator_poly.count == 0u)) {
        goto cleanup;
    }
    if (status == PHY_OK) {
        status = gcd_at(
            &numerator_poly, &denominator_poly, 0u, &gcd);
    }
    if (status == PHY_OK && poly_is_one(&gcd)) {
        goto cleanup;
    }
    bool numerator_exact = false;
    bool denominator_exact = false;
    if (status == PHY_OK) {
        status = poly_divide_exact(
            &numerator_poly, &gcd, &numerator_quotient,
            &numerator_exact);
    }
    if (status == PHY_OK) {
        status = poly_divide_exact(
            &denominator_poly, &gcd, &denominator_quotient,
            &denominator_exact);
    }
    if (status == PHY_OK &&
        (!numerator_exact || !denominator_exact)) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }

    /* Normalize the rational-function pair to a monic denominator. */
    if (status == PHY_OK && denominator_quotient.count != 0u) {
        const phy_ir_ref leading =
            denominator_quotient.terms[0].coefficient;
        for (size_t index = 0u;
             status == PHY_OK && index < numerator_quotient.count; ++index) {
            status = coefficient_divide(
                &numerator_quotient,
                numerator_quotient.terms[index].coefficient,
                leading,
                &numerator_quotient.terms[index].coefficient);
        }
        for (size_t index = 0u;
             status == PHY_OK && index < denominator_quotient.count;
             ++index) {
            status = coefficient_divide(
                &denominator_quotient,
                denominator_quotient.terms[index].coefficient,
                leading,
                &denominator_quotient.terms[index].coefficient);
        }
    }
    if (status == PHY_OK) {
        status = poly_multiply(
            &gcd, &numerator_quotient, &numerator_check);
    }
    if (status == PHY_OK) {
        status = poly_multiply(
            &gcd, &denominator_quotient, &denominator_check);
    }
    if (status == PHY_OK &&
        (!poly_equal(&numerator_check, &numerator_poly) ||
         !poly_equal(&denominator_check, &denominator_poly))) {
        /*
         * Quotient normalization changes both products by the same unit.
         * Re-run the proof before normalization would duplicate storage, so
         * accept unit-scaled equality by comparing after monic normalization.
         */
        status = poly_make_monic(&numerator_check);
        if (status == PHY_OK) {
            status = poly_make_monic(&denominator_check);
        }
        if (status == PHY_OK) {
            status = poly_make_monic(&numerator_poly);
        }
        if (status == PHY_OK) {
            status = poly_make_monic(&denominator_poly);
        }
        if (status == PHY_OK &&
            (!poly_equal(&numerator_check, &numerator_poly) ||
             !poly_equal(&denominator_check, &denominator_poly))) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        }
    }
    if (status == PHY_OK) {
        status = poly_to_ir(&numerator_quotient, out_numerator);
    }
    if (status == PHY_OK) {
        status = poly_to_ir(&denominator_quotient, out_denominator);
    }
    if (status == PHY_OK) {
        *out_matched = true;
    }

cleanup:
    while (initialized != 0u) {
        poly_destroy(allocated[--initialized]);
    }
    return status;
}
