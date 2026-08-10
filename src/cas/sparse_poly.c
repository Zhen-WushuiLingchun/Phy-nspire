#include "sparse_poly.h"

#include <string.h>

#define SPARSE_MAX_VARIABLES 8u
#define SPARSE_MAX_TERMS 192u
#define SPARSE_MAX_DEGREE 48u
#define SPARSE_MAX_BASIS 16u
#define SPARSE_MAX_PAIRS \
    ((SPARSE_MAX_BASIS * (SPARSE_MAX_BASIS - 1u)) / 2u)
#define SPARSE_MAX_SYLVESTER 32u

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

static phy_status coefficient_add(const sparse_poly *poly, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out)
{
    return phy_cas_exact_add_refs(poly->cas, left, right, out);
}

static phy_status coefficient_subtract(const sparse_poly *poly, phy_ir_ref left,
                                       phy_ir_ref right, phy_ir_ref *out)
{
    return phy_cas_exact_sub_refs(poly->cas, left, right, out);
}

static phy_status coefficient_multiply(const sparse_poly *poly, phy_ir_ref left,
                                       phy_ir_ref right, phy_ir_ref *out)
{
    return phy_cas_exact_mul_refs(poly->cas, left, right, out);
}

static phy_status coefficient_divide(const sparse_poly *poly, phy_ir_ref left,
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

/* ----------------------------------------------------- Groebner machinery */

static void poly_remove_leading_term(sparse_poly *poly)
{
    if (poly->count == 0u) {
        return;
    }
    memmove(&poly->terms[0], &poly->terms[1],
            (poly->count - 1u) * sizeof(poly->terms[0]));
    poly->count--;
}

/* Multivariate division with a deterministic first-divisor strategy. */
static phy_status poly_normal_form(const sparse_poly *dividend,
                                   sparse_poly *const *divisors,
                                   size_t divisor_count,
                                   sparse_poly *out_remainder)
{
    sparse_poly work;
    sparse_poly scaled;
    sparse_poly next;
    sparse_poly *allocated[] = {&work, &scaled, &next};
    memset(&work, 0, sizeof work);
    memset(&scaled, 0, sizeof scaled);
    memset(&next, 0, sizeof next);
    phy_status status = PHY_OK;
    size_t initialized = 0u;
    while (status == PHY_OK &&
           initialized < sizeof allocated / sizeof allocated[0]) {
        status = poly_init(
            dividend->cas, dividend->variables, dividend->variable_count,
            allocated[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    out_remainder->count = 0u;
    if (status == PHY_OK) {
        status = poly_copy(dividend, &work);
    }
    uint8_t exponent[SPARSE_MAX_VARIABLES] = {0u};
    while (status == PHY_OK && work.count != 0u) {
        status = phy_cas_step(dividend->cas);
        if (status != PHY_OK) {
            break;
        }
        size_t reducer = divisor_count;
        for (size_t index = 0u; index < divisor_count; ++index) {
            if (divisors[index] != NULL && divisors[index]->count != 0u &&
                monomial_divides(
                    dividend, divisors[index]->terms[0].exponent,
                    work.terms[0].exponent)) {
                reducer = index;
                break;
            }
        }
        if (reducer == divisor_count) {
            status = insert_term(out_remainder, work.terms[0].coefficient,
                                 work.terms[0].exponent);
            if (status == PHY_OK) {
                poly_remove_leading_term(&work);
            }
            continue;
        }
        for (size_t variable = 0u;
             variable < dividend->variable_count; ++variable) {
            exponent[variable] =
                (uint8_t)(work.terms[0].exponent[variable] -
                          divisors[reducer]->terms[0].exponent[variable]);
        }
        phy_ir_ref coefficient = PHY_IR_NULL;
        status = coefficient_divide(
            &work, work.terms[0].coefficient,
            divisors[reducer]->terms[0].coefficient, &coefficient);
        if (status == PHY_OK) {
            status = poly_scale_monomial(
                divisors[reducer], coefficient, exponent, &scaled);
        }
        if (status == PHY_OK) {
            status = poly_subtract(&work, &scaled, &next);
        }
        if (status == PHY_OK) {
            sparse_poly swap = work;
            work = next;
            next = swap;
            next.count = 0u;
            scaled.count = 0u;
        }
    }
    while (initialized != 0u) {
        poly_destroy(allocated[--initialized]);
    }
    return status;
}

static phy_status poly_s_polynomial(const sparse_poly *left,
                                    const sparse_poly *right,
                                    sparse_poly *out)
{
    if (left->count == 0u || right->count == 0u) {
        out->count = 0u;
        return PHY_OK;
    }
    uint8_t left_shift[SPARSE_MAX_VARIABLES] = {0u};
    uint8_t right_shift[SPARSE_MAX_VARIABLES] = {0u};
    for (size_t variable = 0u; variable < left->variable_count; ++variable) {
        const uint8_t l = left->terms[0].exponent[variable];
        const uint8_t r = right->terms[0].exponent[variable];
        const uint8_t maximum = l > r ? l : r;
        left_shift[variable] = (uint8_t)(maximum - l);
        right_shift[variable] = (uint8_t)(maximum - r);
    }
    phy_ir_ref left_inverse = PHY_IR_NULL;
    phy_ir_ref right_inverse = PHY_IR_NULL;
    phy_status status = coefficient_divide(
        out, out->cas->one, left->terms[0].coefficient, &left_inverse);
    if (status == PHY_OK) {
        status = coefficient_divide(
            out, out->cas->one, right->terms[0].coefficient,
            &right_inverse);
    }
    sparse_poly left_multiple;
    sparse_poly right_multiple;
    memset(&left_multiple, 0, sizeof left_multiple);
    memset(&right_multiple, 0, sizeof right_multiple);
    if (status == PHY_OK) {
        status = poly_init(
            out->cas, out->variables, out->variable_count, &left_multiple);
    }
    if (status == PHY_OK) {
        status = poly_init(
            out->cas, out->variables, out->variable_count, &right_multiple);
    }
    if (status == PHY_OK) {
        status = poly_scale_monomial(
            left, left_inverse, left_shift, &left_multiple);
    }
    if (status == PHY_OK) {
        status = poly_scale_monomial(
            right, right_inverse, right_shift, &right_multiple);
    }
    if (status == PHY_OK) {
        status = poly_subtract(&left_multiple, &right_multiple, out);
    }
    poly_destroy(&right_multiple);
    poly_destroy(&left_multiple);
    return status;
}

typedef struct {
    uint8_t left;
    uint8_t right;
} sparse_pair;

static phy_status groebner_compute(
    phy_cas *cas, const phy_ir_ref *expressions, size_t expression_count,
    const phy_ir_ref *variables, size_t variable_count,
    sparse_poly *basis, size_t *out_basis_count)
{
    sparse_poly parser;
    sparse_poly candidate;
    sparse_poly remainder;
    sparse_poly s_polynomial;
    sparse_poly *temporary[] = {
        &parser, &candidate, &remainder, &s_polynomial};
    memset(&parser, 0, sizeof parser);
    memset(&candidate, 0, sizeof candidate);
    memset(&remainder, 0, sizeof remainder);
    memset(&s_polynomial, 0, sizeof s_polynomial);
    phy_status status = PHY_OK;
    size_t temporary_count = 0u;
    while (status == PHY_OK &&
           temporary_count < sizeof temporary / sizeof temporary[0]) {
        status = poly_init(
            cas, variables, variable_count, temporary[temporary_count]);
        if (status == PHY_OK) {
            temporary_count++;
        }
    }
    size_t initialized_basis = 0u;
    while (status == PHY_OK && initialized_basis < SPARSE_MAX_BASIS) {
        status = poly_init(
            cas, variables, variable_count, &basis[initialized_basis]);
        if (status == PHY_OK) {
            initialized_basis++;
        }
    }

    sparse_pair pairs[SPARSE_MAX_PAIRS];
    size_t pair_count = 0u;
    size_t basis_count = 0u;
    for (size_t expression = 0u;
         status == PHY_OK && expression < expression_count; ++expression) {
        candidate.count = 0u;
        remainder.count = 0u;
        status = poly_from_ir_node(
            &parser, expressions[expression], &candidate);
        sparse_poly *divisors[SPARSE_MAX_BASIS];
        for (size_t index = 0u; index < basis_count; ++index) {
            divisors[index] = &basis[index];
        }
        if (status == PHY_OK) {
            status = poly_normal_form(
                &candidate, divisors, basis_count, &remainder);
        }
        if (status == PHY_OK && remainder.count != 0u) {
            if (basis_count >= SPARSE_MAX_BASIS) {
                status = PHY_ERR_TERM_LIMIT;
                break;
            }
            status = poly_make_monic(&remainder);
            if (status == PHY_OK) {
                for (size_t prior = 0u; prior < basis_count; ++prior) {
                    if (pair_count >= SPARSE_MAX_PAIRS) {
                        status = PHY_ERR_TERM_LIMIT;
                        break;
                    }
                    pairs[pair_count++] = (sparse_pair){
                        (uint8_t)prior, (uint8_t)basis_count};
                }
            }
            if (status == PHY_OK) {
                status = poly_copy(&remainder, &basis[basis_count++]);
            }
        }
    }

    size_t pair_cursor = 0u;
    while (status == PHY_OK && pair_cursor < pair_count) {
        const sparse_pair pair = pairs[pair_cursor++];
        s_polynomial.count = 0u;
        remainder.count = 0u;
        status = poly_s_polynomial(
            &basis[pair.left], &basis[pair.right], &s_polynomial);
        sparse_poly *divisors[SPARSE_MAX_BASIS];
        for (size_t index = 0u; index < basis_count; ++index) {
            divisors[index] = &basis[index];
        }
        if (status == PHY_OK) {
            status = poly_normal_form(
                &s_polynomial, divisors, basis_count, &remainder);
        }
        if (status != PHY_OK || remainder.count == 0u) {
            continue;
        }
        if (basis_count >= SPARSE_MAX_BASIS) {
            status = PHY_ERR_TERM_LIMIT;
            break;
        }
        status = poly_make_monic(&remainder);
        for (size_t prior = 0u;
             status == PHY_OK && prior < basis_count; ++prior) {
            if (pair_count >= SPARSE_MAX_PAIRS) {
                status = PHY_ERR_TERM_LIMIT;
                break;
            }
            pairs[pair_count++] = (sparse_pair){
                (uint8_t)prior, (uint8_t)basis_count};
        }
        if (status == PHY_OK) {
            status = poly_copy(&remainder, &basis[basis_count++]);
        }
    }

    /* Buchberger and ideal-membership certificates, before any IR escapes. */
    sparse_poly *divisors[SPARSE_MAX_BASIS];
    for (size_t index = 0u; index < basis_count; ++index) {
        divisors[index] = &basis[index];
    }
    for (size_t expression = 0u;
         status == PHY_OK && expression < expression_count; ++expression) {
        candidate.count = 0u;
        remainder.count = 0u;
        status = poly_from_ir_node(
            &parser, expressions[expression], &candidate);
        if (status == PHY_OK) {
            status = poly_normal_form(
                &candidate, divisors, basis_count, &remainder);
        }
        if (status == PHY_OK && remainder.count != 0u) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        }
    }
    for (size_t left = 0u; status == PHY_OK && left < basis_count; ++left) {
        for (size_t right = left + 1u;
             status == PHY_OK && right < basis_count; ++right) {
            s_polynomial.count = 0u;
            remainder.count = 0u;
            status = poly_s_polynomial(
                &basis[left], &basis[right], &s_polynomial);
            if (status == PHY_OK) {
                status = poly_normal_form(
                    &s_polynomial, divisors, basis_count, &remainder);
            }
            if (status == PHY_OK && remainder.count != 0u) {
                status = PHY_ERR_CORRUPT_DOCUMENT;
            }
        }
    }

    if (status == PHY_OK) {
        *out_basis_count = basis_count;
    } else {
        while (initialized_basis != 0u) {
            poly_destroy(&basis[--initialized_basis]);
        }
    }
    while (temporary_count != 0u) {
        poly_destroy(temporary[--temporary_count]);
    }
    return status;
}

static void groebner_destroy(sparse_poly *basis)
{
    for (size_t index = SPARSE_MAX_BASIS; index != 0u; --index) {
        poly_destroy(&basis[index - 1u]);
    }
}

/* ------------------------------------------------ resultant/discriminant */

static phy_ir_ref coefficient_of_degree(const sparse_poly *poly,
                                         unsigned degree)
{
    for (size_t term = 0u; term < poly->count; ++term) {
        if (poly->terms[term].exponent[0] == degree) {
            return poly->terms[term].coefficient;
        }
    }
    return poly->cas->zero;
}

static phy_status coefficient_power(const sparse_poly *poly, phy_ir_ref base,
                                    unsigned exponent, phy_ir_ref *out)
{
    phy_ir_ref result = poly->cas->one;
    phy_ir_ref factor = base;
    while (exponent != 0u) {
        if ((exponent & 1u) != 0u) {
            phy_status status = coefficient_multiply(
                poly, result, factor, &result);
            if (status != PHY_OK) {
                return status;
            }
        }
        exponent >>= 1u;
        if (exponent != 0u) {
            phy_status status = coefficient_multiply(
                poly, factor, factor, &factor);
            if (status != PHY_OK) {
                return status;
            }
        }
    }
    *out = result;
    return PHY_OK;
}

static phy_status exact_matrix_determinant(const sparse_poly *poly,
                                           phy_ir_ref *matrix,
                                           size_t dimension,
                                           phy_ir_ref *out)
{
    phy_ir_ref determinant = poly->cas->one;
    bool negative = false;
    for (size_t column = 0u; column < dimension; ++column) {
        size_t pivot = column;
        while (pivot < dimension &&
               coefficient_zero(
                   poly, matrix[pivot * dimension + column])) {
            pivot++;
        }
        if (pivot == dimension) {
            *out = poly->cas->zero;
            return PHY_OK;
        }
        if (pivot != column) {
            for (size_t entry = column; entry < dimension; ++entry) {
                const size_t a = column * dimension + entry;
                const size_t b = pivot * dimension + entry;
                const phy_ir_ref swap = matrix[a];
                matrix[a] = matrix[b];
                matrix[b] = swap;
            }
            negative = !negative;
        }
        const phy_ir_ref diagonal =
            matrix[column * dimension + column];
        phy_status status = coefficient_multiply(
            poly, determinant, diagonal, &determinant);
        if (status != PHY_OK) {
            return status;
        }
        for (size_t row = column + 1u; row < dimension; ++row) {
            const phy_ir_ref first = matrix[row * dimension + column];
            if (coefficient_zero(poly, first)) {
                continue;
            }
            phy_ir_ref factor = PHY_IR_NULL;
            status = coefficient_divide(poly, first, diagonal, &factor);
            for (size_t entry = column + 1u;
                 status == PHY_OK && entry < dimension; ++entry) {
                phy_ir_ref product = PHY_IR_NULL;
                status = coefficient_multiply(
                    poly, factor,
                    matrix[column * dimension + entry], &product);
                if (status == PHY_OK) {
                    status = coefficient_subtract(
                        poly, matrix[row * dimension + entry], product,
                        &matrix[row * dimension + entry]);
                }
            }
            if (status != PHY_OK) {
                return status;
            }
            matrix[row * dimension + column] = poly->cas->zero;
        }
    }
    if (negative) {
        return coefficient_subtract(
            poly, poly->cas->zero, determinant, out);
    }
    *out = determinant;
    return PHY_OK;
}

static phy_status resultant_polys(const sparse_poly *left,
                                  const sparse_poly *right,
                                  phy_ir_ref *out)
{
    if (left->count == 0u || right->count == 0u) {
        *out = left->cas->zero;
        return PHY_OK;
    }
    const unsigned left_degree = degree_in(left, 0u);
    const unsigned right_degree = degree_in(right, 0u);
    if (left_degree == 0u) {
        return coefficient_power(
            left, coefficient_of_degree(left, 0u),
            right_degree, out);
    }
    if (right_degree == 0u) {
        return coefficient_power(
            left, coefficient_of_degree(right, 0u),
            left_degree, out);
    }
    const size_t dimension = (size_t)left_degree + right_degree;
    if (dimension > SPARSE_MAX_SYLVESTER) {
        return PHY_ERR_TERM_LIMIT;
    }
    const size_t bytes = dimension * dimension * sizeof(phy_ir_ref);
    phy_ir_ref *matrix = NULL;
    phy_status status = phy_cas_temp_alloc(
        left->cas, bytes, (void **)&matrix);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t entry = 0u; entry < dimension * dimension; ++entry) {
        matrix[entry] = left->cas->zero;
    }
    for (unsigned row = 0u; row < right_degree; ++row) {
        for (unsigned degree = 0u; degree <= left_degree; ++degree) {
            matrix[(size_t)row * dimension + row + degree] =
                coefficient_of_degree(left, left_degree - degree);
        }
    }
    for (unsigned row = 0u; row < left_degree; ++row) {
        for (unsigned degree = 0u; degree <= right_degree; ++degree) {
            matrix[((size_t)right_degree + row) * dimension + row + degree] =
                coefficient_of_degree(right, right_degree - degree);
        }
    }
    status = exact_matrix_determinant(left, matrix, dimension, out);
    phy_cas_temp_free(left->cas, matrix, bytes);
    return status;
}

static phy_status poly_derivative(const sparse_poly *source,
                                  sparse_poly *out)
{
    out->count = 0u;
    for (size_t term = 0u; term < source->count; ++term) {
        const unsigned exponent = source->terms[term].exponent[0];
        if (exponent == 0u) {
            continue;
        }
        phy_ir_ref factor = phy_ir_integer(source->cas->ir, exponent);
        if (factor == PHY_IR_NULL) {
            return phy_cas_ir_failure(source->cas);
        }
        phy_ir_ref coefficient = PHY_IR_NULL;
        phy_status status = coefficient_multiply(
            out, source->terms[term].coefficient, factor, &coefficient);
        uint8_t powers[SPARSE_MAX_VARIABLES];
        memcpy(powers, source->terms[term].exponent, sizeof powers);
        powers[0]--;
        if (status == PHY_OK) {
            status = insert_term(out, coefficient, powers);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
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

static phy_status validate_explicit_variables(
    phy_cas *cas, const phy_ir_ref *variables, size_t variable_count)
{
    if (variables == NULL || variable_count == 0u ||
        variable_count > SPARSE_MAX_VARIABLES) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t index = 0u; index < variable_count; ++index) {
        if (phy_ir_kind_of(cas->ir, variables[index]) != PHY_IR_SYMBOL) {
            return PHY_ERR_TYPE;
        }
        for (size_t prior = 0u; prior < index; ++prior) {
            if (variables[index] == variables[prior]) {
                return PHY_ERR_TYPE;
            }
        }
    }
    return PHY_OK;
}

phy_status phy_sparse_resultant(phy_cas *cas, phy_ir_ref left,
                                phy_ir_ref right, phy_ir_ref variable,
                                phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL || left == PHY_IR_NULL ||
        right == PHY_IR_NULL || variable == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_status status = validate_explicit_variables(cas, &variable, 1u);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_begin(cas);
    sparse_poly parser;
    sparse_poly left_poly;
    sparse_poly right_poly;
    sparse_poly *allocated[] = {&parser, &left_poly, &right_poly};
    memset(&parser, 0, sizeof parser);
    memset(&left_poly, 0, sizeof left_poly);
    memset(&right_poly, 0, sizeof right_poly);
    size_t initialized = 0u;
    while (status == PHY_OK &&
           initialized < sizeof allocated / sizeof allocated[0]) {
        status = poly_init(cas, &variable, 1u, allocated[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    if (status == PHY_OK) {
        status = poly_from_ir_node(&parser, left, &left_poly);
    }
    if (status == PHY_OK) {
        status = poly_from_ir_node(&parser, right, &right_poly);
    }
    if (status == PHY_OK) {
        status = resultant_polys(&left_poly, &right_poly, out_ref);
    }
    while (initialized != 0u) {
        poly_destroy(allocated[--initialized]);
    }
    return status;
}

phy_status phy_sparse_discriminant(phy_cas *cas, phy_ir_ref expression,
                                   phy_ir_ref variable,
                                   phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL || expression == PHY_IR_NULL ||
        variable == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_status status = validate_explicit_variables(cas, &variable, 1u);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_begin(cas);
    sparse_poly parser;
    sparse_poly polynomial;
    sparse_poly derivative;
    sparse_poly *allocated[] = {&parser, &polynomial, &derivative};
    memset(&parser, 0, sizeof parser);
    memset(&polynomial, 0, sizeof polynomial);
    memset(&derivative, 0, sizeof derivative);
    size_t initialized = 0u;
    while (status == PHY_OK &&
           initialized < sizeof allocated / sizeof allocated[0]) {
        status = poly_init(cas, &variable, 1u, allocated[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    if (status == PHY_OK) {
        status = poly_from_ir_node(&parser, expression, &polynomial);
    }
    const unsigned degree = degree_in(&polynomial, 0u);
    if (status == PHY_OK && polynomial.count == 0u) {
        status = PHY_ERR_DOMAIN;
    }
    if (status == PHY_OK && degree <= 1u) {
        *out_ref = cas->one;
    } else if (status == PHY_OK) {
        status = poly_derivative(&polynomial, &derivative);
        phy_ir_ref resultant = PHY_IR_NULL;
        if (status == PHY_OK) {
            status = resultant_polys(
                &polynomial, &derivative, &resultant);
        }
        phy_ir_ref quotient = PHY_IR_NULL;
        if (status == PHY_OK) {
            status = coefficient_divide(
                &polynomial, resultant, polynomial.terms[0].coefficient,
                &quotient);
        }
        if (status == PHY_OK && ((degree * (degree - 1u) / 2u) & 1u) != 0u) {
            status = coefficient_subtract(
                &polynomial, cas->zero, quotient, &quotient);
        }
        if (status == PHY_OK) {
            *out_ref = quotient;
        }
    }
    while (initialized != 0u) {
        poly_destroy(allocated[--initialized]);
    }
    return status;
}

phy_status phy_sparse_univariate_squarefree_coefficients(
    phy_cas *cas, phy_ir_ref expression, phy_ir_ref variable,
    phy_ir_ref *out_coefficients)
{
    if (cas == NULL || out_coefficients == NULL ||
        expression == PHY_IR_NULL || variable == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_coefficients = PHY_IR_NULL;
    phy_status status = validate_explicit_variables(cas, &variable, 1u);
    sparse_poly parser;
    sparse_poly polynomial;
    sparse_poly derivative;
    sparse_poly gcd;
    sparse_poly square_free;
    sparse_poly *allocated[] = {
        &parser, &polynomial, &derivative, &gcd, &square_free};
    memset(&parser, 0, sizeof parser);
    memset(&polynomial, 0, sizeof polynomial);
    memset(&derivative, 0, sizeof derivative);
    memset(&gcd, 0, sizeof gcd);
    memset(&square_free, 0, sizeof square_free);
    size_t initialized = 0u;
    while (status == PHY_OK &&
           initialized < sizeof allocated / sizeof allocated[0]) {
        status = poly_init(cas, &variable, 1u, allocated[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    if (status == PHY_OK) {
        status = poly_from_ir_node(&parser, expression, &polynomial);
    }
    if (status == PHY_OK && degree_in(&polynomial, 0u) == 0u) {
        status = PHY_ERR_UNSUPPORTED;
    }
    if (status == PHY_OK) {
        status = poly_derivative(&polynomial, &derivative);
    }
    if (status == PHY_OK) {
        status = gcd_at(&polynomial, &derivative, 0u, &gcd);
    }
    bool exact = false;
    if (status == PHY_OK) {
        status = poly_divide_exact(
            &polynomial, &gcd, &square_free, &exact);
    }
    if (status == PHY_OK && !exact) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    const unsigned degree = degree_in(&square_free, 0u);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    if (status == PHY_OK) {
        status = phy_cas_scratch_alloc(cas, degree + 1u, &offset);
    }
    for (unsigned power = 0u; status == PHY_OK && power <= degree; ++power) {
        phy_cas_scratch_at(cas, offset)[power] =
            coefficient_of_degree(&square_free, power);
    }
    if (status == PHY_OK) {
        const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
        *out_coefficients = phy_ir_function(
            cas->ir, list, phy_cas_scratch_at(cas, offset), degree + 1u);
        if (*out_coefficients == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
        }
    }
    phy_cas_scratch_release(cas, mark);
    while (initialized != 0u) {
        poly_destroy(allocated[--initialized]);
    }
    return status;
}

phy_status phy_sparse_groebner_basis(
    phy_cas *cas, const phy_ir_ref *expressions, size_t expression_count,
    const phy_ir_ref *variables, size_t variable_count,
    phy_ir_ref *out_ref)
{
    if (cas == NULL || expressions == NULL || out_ref == NULL ||
        expression_count == 0u || expression_count > SPARSE_MAX_BASIS) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_status status =
        validate_explicit_variables(cas, variables, variable_count);
    if (status != PHY_OK) {
        return status;
    }
    phy_cas_begin(cas);
    sparse_poly basis[SPARSE_MAX_BASIS];
    memset(basis, 0, sizeof basis);
    size_t basis_count = 0u;
    status = groebner_compute(
        cas, expressions, expression_count, variables, variable_count,
        basis, &basis_count);
    if (status != PHY_OK) {
        return status;
    }
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset = 0u;
    status = phy_cas_scratch_alloc(cas, basis_count, &offset);
    for (size_t index = 0u;
         status == PHY_OK && index < basis_count; ++index) {
        status = poly_to_ir(
            &basis[index], &phy_cas_scratch_at(cas, offset)[index]);
    }
    if (status == PHY_OK) {
        const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
        *out_ref = phy_ir_function(
            cas->ir, list, phy_cas_scratch_at(cas, offset), basis_count);
        if (*out_ref == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
        }
    }
    phy_cas_scratch_release(cas, mark);
    groebner_destroy(basis);
    return status;
}

phy_status phy_cas_resultant(phy_cas *cas, phy_ir_ref left,
                             phy_ir_ref right, phy_ir_ref variable,
                             phy_ir_ref *out_ref)
{
    return phy_sparse_resultant(cas, left, right, variable, out_ref);
}

phy_status phy_cas_discriminant(phy_cas *cas, phy_ir_ref expression,
                                phy_ir_ref variable,
                                phy_ir_ref *out_ref)
{
    return phy_sparse_discriminant(cas, expression, variable, out_ref);
}

phy_status phy_cas_groebner_basis(
    phy_cas *cas, const phy_ir_ref *expressions, size_t expression_count,
    const phy_ir_ref *variables, size_t variable_count,
    phy_ir_ref *out_ref)
{
    return phy_sparse_groebner_basis(
        cas, expressions, expression_count, variables, variable_count,
        out_ref);
}

/* ------------------------------------------------ polynomial systems ---- */

#define SPARSE_MAX_SYSTEM_EQUATIONS 8u
#define SPARSE_MAX_SOLUTION_BRANCHES 32u

typedef struct {
    phy_cas_rule rules[SPARSE_MAX_VARIABLES];
    size_t rule_count;
} sparse_solution_branch;

static phy_status difference_expression(phy_cas *cas, phy_ir_ref equation,
                                        phy_ir_ref *out)
{
    if (phy_ir_kind_of(cas->ir, equation) != PHY_IR_EQUATION) {
        return PHY_ERR_TYPE;
    }
    phy_ir_ref negative = PHY_IR_NULL;
    phy_status status = phy_cas_neg_node(
        cas, phy_ir_child(cas->ir, equation, 1u), &negative);
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {
            phy_ir_child(cas->ir, equation, 0u), negative};
        status = phy_cas_add_node(cas, terms, 2u, out);
    }
    return status;
}

static phy_status expression_depends_on(phy_cas *cas, phy_ir_ref expression,
                                        const phy_ir_ref *variables,
                                        size_t count, bool *out_depends)
{
    *out_depends = false;
    for (size_t index = 0u; index < count; ++index) {
        bool depends = true;
        phy_status status = phy_cas_may_depend(
            cas, expression, variables[index], &depends);
        if (status != PHY_OK) {
            return status;
        }
        if (depends) {
            *out_depends = true;
            return PHY_OK;
        }
    }
    return PHY_OK;
}

static phy_status verify_root_of_expression(phy_cas *cas,
                                            phy_ir_ref expression,
                                            phy_ir_ref variable,
                                            phy_ir_ref root)
{
    const phy_cas_rule rule = {variable, root};
    phy_ir_ref substituted = PHY_IR_NULL;
    phy_status status = phy_cas_substitute_node(
        cas, expression, &rule, 1u, &substituted);
    phy_cas_decision zero = PHY_CAS_UNKNOWN;
    if (status == PHY_OK) {
        status = phy_cas_decide_zero_node(cas, substituted, &zero);
    }
    if (status != PHY_OK) {
        return status;
    }
    if (zero == PHY_CAS_ZERO) {
        return PHY_OK;
    }
    return zero == PHY_CAS_UNKNOWN
               ? PHY_ERR_UNSUPPORTED
               : PHY_ERR_CORRUPT_DOCUMENT;
}

static phy_status solve_linear_expression(phy_cas *cas,
                                          phy_ir_ref expression,
                                          phy_ir_ref variable,
                                          phy_ir_ref *out_root,
                                          bool *out_matched)
{
    *out_matched = false;
    phy_ir_ref coefficient = PHY_IR_NULL;
    phy_status status = phy_cas_diff_node(
        cas, expression, variable, &coefficient);
    bool derivative_depends = true;
    if (status == PHY_OK) {
        status = phy_cas_may_depend(
            cas, coefficient, variable, &derivative_depends);
    }
    if (status != PHY_OK || derivative_depends) {
        return status;
    }
    const phy_cas_rule zero_rule = {variable, cas->zero};
    phy_ir_ref constant = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_substitute_node(
            cas, expression, &zero_rule, 1u, &constant);
    }
    phy_ir_ref product = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {coefficient, variable};
        status = phy_cas_mul_node(cas, factors, 2u, &product);
    }
    phy_ir_ref reconstructed = PHY_IR_NULL;
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {constant, product};
        status = phy_cas_add_node(cas, terms, 2u, &reconstructed);
    }
    phy_ir_ref negative_reconstructed = PHY_IR_NULL;
    phy_ir_ref error = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_neg_node(
            cas, reconstructed, &negative_reconstructed);
    }
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {expression, negative_reconstructed};
        status = phy_cas_add_node(cas, terms, 2u, &error);
    }
    phy_cas_decision linear = PHY_CAS_UNKNOWN;
    if (status == PHY_OK) {
        status = phy_cas_decide_zero_node(cas, error, &linear);
    }
    if (status != PHY_OK || linear != PHY_CAS_ZERO) {
        return status;
    }
    phy_cas_decision coefficient_zero_decision = PHY_CAS_UNKNOWN;
    status = phy_cas_decide_zero_node(
        cas, coefficient, &coefficient_zero_decision);
    if (status != PHY_OK) {
        return status;
    }
    if (coefficient_zero_decision != PHY_CAS_NONZERO &&
        !phy_cas_known_nonzero(cas, coefficient)) {
        return PHY_ERR_UNSUPPORTED;
    }
    phy_ir_ref inverse = PHY_IR_NULL;
    phy_ir_ref negative_constant = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_cas_pow_node(
            cas, coefficient, cas->minus_one, &inverse);
    }
    if (status == PHY_OK) {
        status = phy_cas_neg_node(cas, constant, &negative_constant);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {negative_constant, inverse};
        status = phy_cas_mul_node(cas, factors, 2u, out_root);
    }
    if (status == PHY_OK) {
        status = verify_root_of_expression(
            cas, expression, variable, *out_root);
    }
    if (status == PHY_OK) {
        *out_matched = true;
    }
    return status;
}

static phy_status roots_from_solve_result(
    phy_cas *cas, phy_ir_ref result, phy_ir_ref variable,
    phy_ir_ref *roots, size_t root_capacity, size_t *out_count)
{
    const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
    const phy_ir_symbol rule = phy_ir_intern(cas->ir, "Rule");
    if (phy_ir_kind_of(cas->ir, result) != PHY_IR_FUNCTION ||
        phy_ir_head(cas->ir, result) != list) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    const size_t count = phy_ir_child_count(cas->ir, result);
    if (count > root_capacity) {
        return PHY_ERR_TERM_LIMIT;
    }
    for (size_t index = 0u; index < count; ++index) {
        const phy_ir_ref branch = phy_ir_child(cas->ir, result, index);
        if (phy_ir_kind_of(cas->ir, branch) != PHY_IR_FUNCTION ||
            phy_ir_head(cas->ir, branch) != list ||
            phy_ir_child_count(cas->ir, branch) != 1u) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        const phy_ir_ref one_rule = phy_ir_child(cas->ir, branch, 0u);
        if (phy_ir_kind_of(cas->ir, one_rule) != PHY_IR_FUNCTION ||
            phy_ir_head(cas->ir, one_rule) != rule ||
            phy_ir_child_count(cas->ir, one_rule) != 2u ||
            phy_ir_child(cas->ir, one_rule, 0u) != variable) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        roots[index] = phy_ir_child(cas->ir, one_rule, 1u);
    }
    *out_count = count;
    return PHY_OK;
}

static phy_status solve_triangular_expression(
    phy_cas *cas, phy_ir_ref expression, phy_ir_ref variable,
    phy_ir_ref *roots, size_t root_capacity, size_t *out_count)
{
    phy_ir_ref linear_root = PHY_IR_NULL;
    bool linear = false;
    phy_status status = solve_linear_expression(
        cas, expression, variable, &linear_root, &linear);
    if (status != PHY_OK) {
        return status;
    }
    if (linear) {
        roots[0] = linear_root;
        *out_count = 1u;
        return PHY_OK;
    }
    const phy_ir_ref equation =
        phy_ir_equation(cas->ir, expression, cas->zero);
    if (equation == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    phy_ir_ref result = PHY_IR_NULL;
    status = phy_cas_solve(cas, equation, variable, &result);
    if (status == PHY_OK) {
        status = roots_from_solve_result(
            cas, result, variable, roots, root_capacity, out_count);
    }
    return status;
}

static phy_status build_polynomial_solution_result(
    phy_cas *cas, const sparse_solution_branch *branches,
    size_t branch_count, const phy_ir_ref *variables,
    size_t variable_count, phy_ir_ref *out_ref)
{
    const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
    const phy_ir_symbol rule = phy_ir_intern(cas->ir, "Rule");
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t branch_offset = 0u;
    phy_status status = phy_cas_scratch_alloc(
        cas, branch_count, &branch_offset);
    for (size_t branch = 0u;
         status == PHY_OK && branch < branch_count; ++branch) {
        phy_ir_ref rules[SPARSE_MAX_VARIABLES];
        for (size_t variable = 0u; variable < variable_count; ++variable) {
            phy_ir_ref value = PHY_IR_NULL;
            for (size_t entry = 0u;
                 entry < branches[branch].rule_count; ++entry) {
                if (branches[branch].rules[entry].from == variables[variable]) {
                    value = branches[branch].rules[entry].to;
                    break;
                }
            }
            if (value == PHY_IR_NULL) {
                status = PHY_ERR_CORRUPT_DOCUMENT;
                break;
            }
            const phy_ir_ref arguments[2] = {variables[variable], value};
            rules[variable] =
                phy_ir_function(cas->ir, rule, arguments, 2u);
            if (rules[variable] == PHY_IR_NULL) {
                status = phy_cas_ir_failure(cas);
                break;
            }
        }
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, branch_offset)[branch] =
                phy_ir_function(
                    cas->ir, list, rules, variable_count);
            if (phy_cas_scratch_at(cas, branch_offset)[branch] ==
                PHY_IR_NULL) {
                status = phy_cas_ir_failure(cas);
            }
        }
    }
    if (status == PHY_OK) {
        *out_ref = phy_ir_function(
            cas->ir, list, phy_cas_scratch_at(cas, branch_offset),
            branch_count);
        if (*out_ref == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
        }
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

phy_status phy_sparse_solve_polynomial_system(
    phy_cas *cas, const phy_ir_ref *equations, size_t equation_count,
    const phy_ir_ref *variables, size_t variable_count,
    phy_ir_ref *out_ref)
{
    if (cas == NULL || equations == NULL || out_ref == NULL ||
        equation_count == 0u ||
        equation_count > SPARSE_MAX_SYSTEM_EQUATIONS) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_status status =
        validate_explicit_variables(cas, variables, variable_count);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref expressions[SPARSE_MAX_SYSTEM_EQUATIONS];
    for (size_t index = 0u;
         status == PHY_OK && index < equation_count; ++index) {
        status = difference_expression(cas, equations[index],
                                       &expressions[index]);
    }
    if (status != PHY_OK) {
        return status;
    }

    sparse_poly basis[SPARSE_MAX_BASIS];
    memset(basis, 0, sizeof basis);
    size_t basis_count = 0u;
    status = groebner_compute(
        cas, expressions, equation_count, variables, variable_count,
        basis, &basis_count);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref basis_ir[SPARSE_MAX_BASIS];
    for (size_t index = 0u;
         status == PHY_OK && index < basis_count; ++index) {
        status = poly_to_ir(&basis[index], &basis_ir[index]);
    }
    /* A nonzero constant in the ideal is the exact inconsistency certificate. */
    bool inconsistent = false;
    for (size_t index = 0u; index < basis_count; ++index) {
        if (basis[index].count == 1u) {
            bool constant = true;
            for (size_t variable = 0u; variable < variable_count; ++variable) {
                constant = constant &&
                    basis[index].terms[0].exponent[variable] == 0u;
            }
            if (constant) {
                inconsistent = true;
                break;
            }
        }
    }
    if (status == PHY_OK && inconsistent) {
        const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
        *out_ref = phy_ir_function(cas->ir, list, NULL, 0u);
        status = *out_ref == PHY_IR_NULL
                     ? phy_cas_ir_failure(cas)
                     : PHY_OK;
        groebner_destroy(basis);
        return status;
    }

    sparse_solution_branch current[SPARSE_MAX_SOLUTION_BRANCHES];
    sparse_solution_branch next[SPARSE_MAX_SOLUTION_BRANCHES];
    memset(current, 0, sizeof current);
    size_t branch_count = 1u;
    for (size_t reverse = variable_count;
         status == PHY_OK && reverse != 0u; --reverse) {
        const size_t variable_index = reverse - 1u;
        size_t next_count = 0u;
        for (size_t branch = 0u;
             status == PHY_OK && branch < branch_count; ++branch) {
            phy_ir_ref candidate = PHY_IR_NULL;
            for (size_t polynomial = 0u;
                 status == PHY_OK && polynomial < basis_count; ++polynomial) {
                phy_ir_ref substituted = PHY_IR_NULL;
                status = phy_cas_substitute_node(
                    cas, basis_ir[polynomial], current[branch].rules,
                    current[branch].rule_count, &substituted);
                bool earlier = false;
                if (status == PHY_OK && variable_index != 0u) {
                    status = expression_depends_on(
                        cas, substituted, variables, variable_index,
                        &earlier);
                }
                bool current_variable = false;
                if (status == PHY_OK && !earlier) {
                    status = phy_cas_may_depend(
                        cas, substituted, variables[variable_index],
                        &current_variable);
                }
                if (status == PHY_OK && !earlier && current_variable) {
                    candidate = substituted;
                    break;
                }
            }
            if (status != PHY_OK) {
                break;
            }
            if (candidate == PHY_IR_NULL) {
                status = PHY_ERR_UNSUPPORTED;
                break;
            }
            phy_ir_ref roots[SPARSE_MAX_SOLUTION_BRANCHES];
            size_t root_count = 0u;
            status = solve_triangular_expression(
                cas, candidate, variables[variable_index], roots,
                SPARSE_MAX_SOLUTION_BRANCHES, &root_count);
            for (size_t root = 0u;
                 status == PHY_OK && root < root_count; ++root) {
                if (next_count >= SPARSE_MAX_SOLUTION_BRANCHES) {
                    status = PHY_ERR_TERM_LIMIT;
                    break;
                }
                next[next_count] = current[branch];
                next[next_count].rules[next[next_count].rule_count++] =
                    (phy_cas_rule){variables[variable_index], roots[root]};
                next_count++;
            }
        }
        if (status == PHY_OK) {
            memcpy(current, next, next_count * sizeof current[0]);
            branch_count = next_count;
        }
    }

    for (size_t branch = 0u;
         status == PHY_OK && branch < branch_count; ++branch) {
        for (size_t equation = 0u;
             status == PHY_OK && equation < equation_count; ++equation) {
            phy_ir_ref substituted = PHY_IR_NULL;
            status = phy_cas_substitute_node(
                cas, expressions[equation], current[branch].rules,
                current[branch].rule_count, &substituted);
            phy_cas_decision zero = PHY_CAS_UNKNOWN;
            if (status == PHY_OK) {
                status = phy_cas_decide_zero_node(cas, substituted, &zero);
            }
            if (status == PHY_OK && zero != PHY_CAS_ZERO) {
                status = zero == PHY_CAS_UNKNOWN
                             ? PHY_ERR_UNSUPPORTED
                             : PHY_ERR_CORRUPT_DOCUMENT;
            }
        }
    }
    if (status == PHY_OK) {
        status = build_polynomial_solution_result(
            cas, current, branch_count, variables, variable_count, out_ref);
    }
    groebner_destroy(basis);
    return status;
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
