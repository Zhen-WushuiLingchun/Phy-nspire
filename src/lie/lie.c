#include "phy/lie.h"

#include <string.h>

#include "phy/platform.h"

struct phy_lie_algebra {
    phy_cas *cas;
    phy_ir_symbol name;
    unsigned dimension;
    phy_ir_symbol basis[PHY_LIE_MAX_DIM];
    phy_ir_ref constants[PHY_LIE_MAX_DIM * PHY_LIE_MAX_DIM *
                         PHY_LIE_MAX_DIM];
};

struct phy_lie_element {
    const phy_lie_algebra *algebra;
    phy_ir_ref coefficient[PHY_LIE_MAX_DIM];
};

struct phy_lie_group {
    phy_lie_algebra *algebra;
    phy_ir_symbol name;
    unsigned representation_dimension;
    bool compact;
};

static size_t constant_offset(unsigned dimension, unsigned a, unsigned b,
                              unsigned c)
{
    return ((size_t)a * dimension + b) * dimension + c;
}

static phy_status ir_failure(phy_ir_context *ir)
{
    const phy_status status = phy_ir_last_error(ir);
    return status != PHY_OK ? status : PHY_ERR_OUT_OF_MEMORY;
}

static phy_status require_zero(phy_cas *cas, phy_ir_ref expression)
{
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    const phy_status status =
        phy_cas_is_zero(cas, expression, &decision);
    if (status != PHY_OK) {
        return status;
    }
    return decision == PHY_CAS_ZERO ? PHY_OK : PHY_ERR_ASSUMPTION;
}

static phy_status add2(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                       phy_ir_ref *out)
{
    const phy_ir_ref terms[2] = {left, right};
    return phy_cas_add(cas, terms, 2u, out);
}

static phy_status mul2(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                       phy_ir_ref *out)
{
    const phy_ir_ref factors[2] = {left, right};
    return phy_cas_mul(cas, factors, 2u, out);
}

phy_status phy_lie_algebra_create(
    phy_cas *cas, const char *name, const char *const *basis_names,
    unsigned dimension, const phy_ir_ref *structure_constants,
    phy_lie_algebra **out_algebra)
{
    if (cas == NULL || name == NULL || basis_names == NULL ||
        structure_constants == NULL || out_algebra == NULL ||
        dimension == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (dimension > PHY_LIE_MAX_DIM) {
        return PHY_ERR_UNSUPPORTED;
    }

    phy_lie_algebra *algebra = phy_alloc(sizeof *algebra);
    if (algebra == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(algebra, 0, sizeof *algebra);
    algebra->cas = cas;
    algebra->dimension = dimension;

    phy_ir_context *ir = phy_cas_ir(cas);
    algebra->name = phy_ir_intern(ir, name);
    if (algebra->name == PHY_IR_NO_SYMBOL) {
        phy_free(algebra, sizeof *algebra);
        return ir_failure(ir);
    }
    for (unsigned basis = 0u; basis < dimension; ++basis) {
        if (basis_names[basis] == NULL) {
            phy_free(algebra, sizeof *algebra);
            return PHY_ERR_INVALID_ARGUMENT;
        }
        algebra->basis[basis] = phy_ir_intern(ir, basis_names[basis]);
        if (algebra->basis[basis] == PHY_IR_NO_SYMBOL) {
            phy_free(algebra, sizeof *algebra);
            return ir_failure(ir);
        }
        for (unsigned prior = 0u; prior < basis; ++prior) {
            if (algebra->basis[prior] == algebra->basis[basis]) {
                phy_free(algebra, sizeof *algebra);
                return PHY_ERR_ASSUMPTION;
            }
        }
    }

    const size_t count = (size_t)dimension * dimension * dimension;
    memcpy(algebra->constants, structure_constants,
           count * sizeof *structure_constants);
    const phy_status status = phy_lie_algebra_validate(algebra);
    if (status != PHY_OK) {
        phy_free(algebra, sizeof *algebra);
        return status;
    }
    *out_algebra = algebra;
    return PHY_OK;
}

void phy_lie_algebra_destroy(phy_lie_algebra *algebra)
{
    if (algebra != NULL) {
        phy_free(algebra, sizeof *algebra);
    }
}

phy_cas *phy_lie_algebra_cas(const phy_lie_algebra *algebra)
{
    return algebra != NULL ? algebra->cas : NULL;
}

const char *phy_lie_algebra_name(const phy_lie_algebra *algebra)
{
    return algebra != NULL
               ? phy_ir_symbol_name(phy_cas_ir(algebra->cas), algebra->name)
               : NULL;
}

unsigned phy_lie_algebra_dimension(const phy_lie_algebra *algebra)
{
    return algebra != NULL ? algebra->dimension : 0u;
}

phy_ir_symbol phy_lie_algebra_basis_symbol(const phy_lie_algebra *algebra,
                                           unsigned basis)
{
    return algebra != NULL && basis < algebra->dimension
               ? algebra->basis[basis]
               : PHY_IR_NO_SYMBOL;
}

phy_ir_ref phy_lie_structure_constant(const phy_lie_algebra *algebra,
                                      unsigned a, unsigned b, unsigned c)
{
    if (algebra == NULL || a >= algebra->dimension ||
        b >= algebra->dimension || c >= algebra->dimension) {
        return PHY_IR_NULL;
    }
    return algebra->constants[
        constant_offset(algebra->dimension, a, b, c)];
}

phy_status phy_lie_algebra_validate(const phy_lie_algebra *algebra)
{
    if (algebra == NULL || algebra->cas == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_cas *cas = algebra->cas;
    const unsigned n = algebra->dimension;

    for (unsigned a = 0u; a < n; ++a) {
        for (unsigned b = 0u; b < n; ++b) {
            for (unsigned c = 0u; c < n; ++c) {
                phy_ir_ref antisymmetric = PHY_IR_NULL;
                phy_status status = add2(
                    cas, phy_lie_structure_constant(algebra, a, b, c),
                    phy_lie_structure_constant(algebra, b, a, c),
                    &antisymmetric);
                if (status == PHY_OK) {
                    status = require_zero(cas, antisymmetric);
                }
                if (status != PHY_OK) {
                    return status;
                }
            }
        }
    }

    /*
     * f_bc^d f_ad^e + f_ca^d f_bd^e + f_ab^d f_cd^e = 0
     * is the coefficient of T_e in
     * [T_a,[T_b,T_c]] + cyclic.
     */
    for (unsigned a = 0u; a < n; ++a) {
        for (unsigned b = 0u; b < n; ++b) {
            for (unsigned c = 0u; c < n; ++c) {
                for (unsigned e = 0u; e < n; ++e) {
                    phy_ir_ref sum = PHY_IR_NULL;
                    phy_status status = phy_cas_number(cas, 0, 1, &sum);
                    for (unsigned d = 0u; d < n && status == PHY_OK; ++d) {
                        const phy_ir_ref factors[6] = {
                            phy_lie_structure_constant(algebra, b, c, d),
                            phy_lie_structure_constant(algebra, a, d, e),
                            phy_lie_structure_constant(algebra, c, a, d),
                            phy_lie_structure_constant(algebra, b, d, e),
                            phy_lie_structure_constant(algebra, a, b, d),
                            phy_lie_structure_constant(algebra, c, d, e)};
                        phy_ir_ref first = PHY_IR_NULL;
                        phy_ir_ref second = PHY_IR_NULL;
                        phy_ir_ref third = PHY_IR_NULL;
                        phy_ir_ref terms[4] = {sum, PHY_IR_NULL,
                                               PHY_IR_NULL, PHY_IR_NULL};
                        status = mul2(cas, factors[0], factors[1], &first);
                        if (status == PHY_OK) {
                            status =
                                mul2(cas, factors[2], factors[3], &second);
                        }
                        if (status == PHY_OK) {
                            status =
                                mul2(cas, factors[4], factors[5], &third);
                        }
                        if (status == PHY_OK) {
                            terms[1] = first;
                            terms[2] = second;
                            terms[3] = third;
                            status = phy_cas_add(cas, terms, 4u, &sum);
                        }
                    }
                    if (status == PHY_OK) {
                        status = require_zero(cas, sum);
                    }
                    if (status != PHY_OK) {
                        return status;
                    }
                }
            }
        }
    }
    return PHY_OK;
}

phy_ir_ref phy_lie_adjoint_component(const phy_lie_algebra *algebra,
                                     unsigned generator, unsigned row,
                                     unsigned column)
{
    return phy_lie_structure_constant(algebra, generator, column, row);
}

phy_status phy_lie_killing_component(const phy_lie_algebra *algebra,
                                     unsigned a, unsigned b,
                                     phy_ir_ref *out_value)
{
    if (algebra == NULL || out_value == NULL || a >= algebra->dimension ||
        b >= algebra->dimension) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref sum = PHY_IR_NULL;
    phy_status status = phy_cas_number(algebra->cas, 0, 1, &sum);
    for (unsigned c = 0u;
         c < algebra->dimension && status == PHY_OK; ++c) {
        for (unsigned d = 0u;
             d < algebra->dimension && status == PHY_OK; ++d) {
            phy_ir_ref term = PHY_IR_NULL;
            status = mul2(
                algebra->cas,
                phy_lie_adjoint_component(algebra, a, c, d),
                phy_lie_adjoint_component(algebra, b, d, c), &term);
            if (status == PHY_OK) {
                status = add2(algebra->cas, sum, term, &sum);
            }
        }
    }
    if (status == PHY_OK) {
        *out_value = sum;
    }
    return status;
}

phy_status phy_lie_element_create(const phy_lie_algebra *algebra,
                                  const phy_ir_ref *coefficients,
                                  phy_lie_element **out_element)
{
    if (algebra == NULL || coefficients == NULL || out_element == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_lie_element *element = phy_alloc(sizeof *element);
    if (element == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(element, 0, sizeof *element);
    element->algebra = algebra;
    memcpy(element->coefficient, coefficients,
           algebra->dimension * sizeof *coefficients);
    *out_element = element;
    return PHY_OK;
}

phy_status phy_lie_basis_element(const phy_lie_algebra *algebra,
                                 unsigned basis,
                                 phy_lie_element **out_element)
{
    if (algebra == NULL || out_element == NULL ||
        basis >= algebra->dimension) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref coefficients[PHY_LIE_MAX_DIM];
    phy_status status = PHY_OK;
    for (unsigned index = 0u; index < algebra->dimension; ++index) {
        status = phy_cas_number(algebra->cas, index == basis ? 1 : 0, 1,
                                &coefficients[index]);
        if (status != PHY_OK) {
            return status;
        }
    }
    return phy_lie_element_create(algebra, coefficients, out_element);
}

void phy_lie_element_destroy(phy_lie_element *element)
{
    if (element != NULL) {
        phy_free(element, sizeof *element);
    }
}

const phy_lie_algebra *phy_lie_element_algebra(
    const phy_lie_element *element)
{
    return element != NULL ? element->algebra : NULL;
}

phy_ir_ref phy_lie_element_coefficient(const phy_lie_element *element,
                                       unsigned basis)
{
    return element != NULL && basis < element->algebra->dimension
               ? element->coefficient[basis]
               : PHY_IR_NULL;
}

phy_status phy_lie_element_add(const phy_lie_element *left,
                               const phy_lie_element *right,
                               phy_lie_element **out_element)
{
    if (left == NULL || right == NULL || out_element == NULL ||
        left->algebra != right->algebra) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref coefficients[PHY_LIE_MAX_DIM];
    phy_status status = PHY_OK;
    for (unsigned basis = 0u;
         basis < left->algebra->dimension && status == PHY_OK; ++basis) {
        status = add2(left->algebra->cas, left->coefficient[basis],
                      right->coefficient[basis], &coefficients[basis]);
    }
    return status == PHY_OK
               ? phy_lie_element_create(left->algebra, coefficients,
                                        out_element)
               : status;
}

phy_status phy_lie_element_scale(const phy_lie_element *element,
                                 phy_ir_ref scalar,
                                 phy_lie_element **out_element)
{
    if (element == NULL || out_element == NULL ||
        scalar == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref coefficients[PHY_LIE_MAX_DIM];
    phy_status status = PHY_OK;
    for (unsigned basis = 0u;
         basis < element->algebra->dimension && status == PHY_OK; ++basis) {
        status = mul2(element->algebra->cas, scalar,
                      element->coefficient[basis], &coefficients[basis]);
    }
    return status == PHY_OK
               ? phy_lie_element_create(element->algebra, coefficients,
                                        out_element)
               : status;
}

phy_status phy_lie_bracket(const phy_lie_element *left,
                           const phy_lie_element *right,
                           phy_lie_element **out_element)
{
    if (left == NULL || right == NULL || out_element == NULL ||
        left->algebra != right->algebra) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_lie_algebra *algebra = left->algebra;
    phy_ir_ref coefficients[PHY_LIE_MAX_DIM];
    phy_status status = PHY_OK;
    for (unsigned c = 0u;
         c < algebra->dimension && status == PHY_OK; ++c) {
        phy_ir_ref sum = PHY_IR_NULL;
        status = phy_cas_number(algebra->cas, 0, 1, &sum);
        for (unsigned a = 0u;
             a < algebra->dimension && status == PHY_OK; ++a) {
            for (unsigned b = 0u;
                 b < algebra->dimension && status == PHY_OK; ++b) {
                const phy_ir_ref factors[3] = {
                    left->coefficient[a], right->coefficient[b],
                    phy_lie_structure_constant(algebra, a, b, c)};
                phy_ir_ref term = PHY_IR_NULL;
                status =
                    phy_cas_mul(algebra->cas, factors, 3u, &term);
                if (status == PHY_OK) {
                    status = add2(algebra->cas, sum, term, &sum);
                }
            }
        }
        coefficients[c] = sum;
    }
    return status == PHY_OK
               ? phy_lie_element_create(algebra, coefficients, out_element)
               : status;
}

phy_status phy_lie_commutator_ir(phy_cas *cas, phy_ir_ref left,
                                 phy_ir_ref right, phy_ir_ref *out_expression)
{
    if (cas == NULL || left == PHY_IR_NULL || right == PHY_IR_NULL ||
        out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_context *ir = phy_cas_ir(cas);
    const phy_ir_ref forward_factors[2] = {left, right};
    const phy_ir_ref reverse_factors[2] = {right, left};
    const phy_ir_ref forward =
        phy_ir_ncmul(ir, forward_factors, 2u);
    const phy_ir_ref reverse =
        phy_ir_ncmul(ir, reverse_factors, 2u);
    if (forward == PHY_IR_NULL || reverse == PHY_IR_NULL) {
        return ir_failure(ir);
    }
    return phy_cas_sub(cas, forward, reverse, out_expression);
}

phy_status phy_lie_group_create(
    phy_cas *cas, const char *name, unsigned representation_dimension,
    bool compact, const char *const *basis_names, unsigned algebra_dimension,
    const phy_ir_ref *structure_constants, phy_lie_group **out_group)
{
    if (cas == NULL || name == NULL || representation_dimension == 0u ||
        out_group == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_lie_group *group = phy_alloc(sizeof *group);
    if (group == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(group, 0, sizeof *group);
    group->name = phy_ir_intern(phy_cas_ir(cas), name);
    if (group->name == PHY_IR_NO_SYMBOL) {
        phy_free(group, sizeof *group);
        return ir_failure(phy_cas_ir(cas));
    }
    phy_status status = phy_lie_algebra_create(
        cas, name, basis_names, algebra_dimension, structure_constants,
        &group->algebra);
    if (status != PHY_OK) {
        phy_free(group, sizeof *group);
        return status;
    }
    group->representation_dimension = representation_dimension;
    group->compact = compact;
    *out_group = group;
    return PHY_OK;
}

static void fill_zeros(phy_cas *cas, phy_ir_ref *constants, unsigned n,
                       phy_status *out_status)
{
    phy_ir_ref zero = PHY_IR_NULL;
    *out_status = phy_cas_number(cas, 0, 1, &zero);
    if (*out_status == PHY_OK) {
        for (size_t index = 0u; index < (size_t)n * n * n; ++index) {
            constants[index] = zero;
        }
    }
}

static void set_antisymmetric(phy_ir_ref *constants, unsigned n,
                              unsigned a, unsigned b, unsigned c,
                              phy_ir_ref value, phy_ir_ref negative)
{
    constants[constant_offset(n, a, b, c)] = value;
    constants[constant_offset(n, b, a, c)] = negative;
}

static int epsilon3(unsigned a, unsigned b, unsigned c)
{
    if (a == b || b == c || a == c) {
        return 0;
    }
    return ((a == 0u && b == 1u && c == 2u) ||
            (a == 1u && b == 2u && c == 0u) ||
            (a == 2u && b == 0u && c == 1u))
               ? 1
               : -1;
}

static phy_status make_epsilon3(phy_cas *cas, const char *name,
                                unsigned representation_dimension,
                                bool compact, phy_lie_group **out_group)
{
    static const char *const basis[3] = {"T1", "T2", "T3"};
    phy_ir_ref constants[27];
    phy_status status = PHY_OK;
    fill_zeros(cas, constants, 3u, &status);
    for (unsigned a = 0u; a < 3u && status == PHY_OK; ++a) {
        for (unsigned b = 0u; b < 3u; ++b) {
            for (unsigned c = 0u; c < 3u; ++c) {
                status = phy_cas_number(
                    cas, epsilon3(a, b, c), 1,
                    &constants[constant_offset(3u, a, b, c)]);
            }
        }
    }
    return status == PHY_OK
               ? phy_lie_group_create(
                     cas, name, representation_dimension, compact, basis, 3u,
                     constants, out_group)
               : status;
}

static phy_status make_lorentz(phy_cas *cas, phy_lie_group **out_group)
{
    static const char *const basis[6] = {
        "J1", "J2", "J3", "K1", "K2", "K3"};
    phy_ir_ref constants[216];
    phy_status status = PHY_OK;
    fill_zeros(cas, constants, 6u, &status);
    if (status != PHY_OK) {
        return status;
    }
    for (unsigned i = 0u; i < 3u; ++i) {
        for (unsigned j = 0u; j < 3u; ++j) {
            for (unsigned k = 0u; k < 3u; ++k) {
                const int epsilon = epsilon3(i, j, k);
                if (epsilon == 0) {
                    continue;
                }
                phy_ir_ref value = PHY_IR_NULL;
                phy_ir_ref negative = PHY_IR_NULL;
                status = phy_cas_number(cas, epsilon, 1, &value);
                if (status == PHY_OK) {
                    status = phy_cas_neg(cas, value, &negative);
                }
                if (status != PHY_OK) {
                    return status;
                }
                constants[constant_offset(6u, i, j, k)] = value;
                constants[constant_offset(6u, i, 3u + j, 3u + k)] = value;
                constants[constant_offset(6u, 3u + i, j, 3u + k)] = value;
                constants[constant_offset(6u, 3u + i, 3u + j, k)] =
                    negative;
            }
        }
    }
    return phy_lie_group_create(cas, "SO(1,3)", 4u, false, basis, 6u,
                                constants, out_group);
}

static phy_status make_su3(phy_cas *cas, phy_lie_group **out_group)
{
    static const char *const basis[8] = {
        "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
    static const struct {
        unsigned a;
        unsigned b;
        unsigned c;
        int numerator;
        int denominator;
        bool root_three;
    } nonzero[] = {
        {0u, 1u, 2u, 1, 1, false}, {0u, 3u, 6u, 1, 2, false},
        {0u, 4u, 5u, -1, 2, false}, {1u, 3u, 5u, 1, 2, false},
        {1u, 4u, 6u, 1, 2, false}, {2u, 3u, 4u, 1, 2, false},
        {2u, 5u, 6u, -1, 2, false}, {3u, 4u, 7u, 1, 2, true},
        {5u, 6u, 7u, 1, 2, true}};
    phy_ir_ref constants[512];
    phy_status status = PHY_OK;
    fill_zeros(cas, constants, 8u, &status);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref three = PHY_IR_NULL;
    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref exponent = PHY_IR_NULL;
    phy_ir_ref root_three = PHY_IR_NULL;
    status = phy_cas_number(cas, 3, 1, &three);
    if (status == PHY_OK) {
        status = phy_cas_number(cas, 1, 2, &half);
    }
    if (status == PHY_OK) {
        exponent = half;
        status = phy_cas_pow(cas, three, exponent, &root_three);
    }
    for (size_t index = 0u;
         index < sizeof nonzero / sizeof nonzero[0] && status == PHY_OK;
         ++index) {
        phy_ir_ref value = PHY_IR_NULL;
        if (nonzero[index].root_three) {
            status = mul2(cas, half, root_three, &value);
        } else {
            status = phy_cas_number(
                cas, nonzero[index].numerator,
                nonzero[index].denominator, &value);
        }
        if (status == PHY_OK && nonzero[index].numerator < 0 &&
            nonzero[index].root_three) {
            status = phy_cas_neg(cas, value, &value);
        }
        phy_ir_ref negative = PHY_IR_NULL;
        if (status == PHY_OK) {
            status = phy_cas_neg(cas, value, &negative);
        }
        if (status == PHY_OK) {
            set_antisymmetric(
                constants, 8u, nonzero[index].a, nonzero[index].b,
                nonzero[index].c, value, negative);
            /*
             * f_abc is totally antisymmetric in the Gell-Mann convention.
             * Seed the other cyclic pairs as well as b,a.
             */
            set_antisymmetric(
                constants, 8u, nonzero[index].b, nonzero[index].c,
                nonzero[index].a, value, negative);
            set_antisymmetric(
                constants, 8u, nonzero[index].c, nonzero[index].a,
                nonzero[index].b, value, negative);
        }
    }
    return status == PHY_OK
               ? phy_lie_group_create(cas, "SU(3)", 3u, true, basis, 8u,
                                      constants, out_group)
               : status;
}

phy_status phy_lie_group_builtin(phy_cas *cas, phy_lie_group_kind kind,
                                 phy_lie_group **out_group)
{
    if (cas == NULL || out_group == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    switch (kind) {
    case PHY_LIE_GROUP_U1: {
        static const char *const basis[1] = {"Q"};
        phy_ir_ref zero = PHY_IR_NULL;
        const phy_status status = phy_cas_number(cas, 0, 1, &zero);
        return status == PHY_OK
                   ? phy_lie_group_create(cas, "U(1)", 1u, true, basis, 1u,
                                          &zero, out_group)
                   : status;
    }
    case PHY_LIE_GROUP_SU2:
        return make_epsilon3(cas, "SU(2)", 2u, true, out_group);
    case PHY_LIE_GROUP_SO3:
        return make_epsilon3(cas, "SO(3)", 3u, true, out_group);
    case PHY_LIE_GROUP_SU3:
        return make_su3(cas, out_group);
    case PHY_LIE_GROUP_SO13:
        return make_lorentz(cas, out_group);
    default:
        return PHY_ERR_INVALID_ARGUMENT;
    }
}

void phy_lie_group_destroy(phy_lie_group *group)
{
    if (group != NULL) {
        phy_lie_algebra_destroy(group->algebra);
        phy_free(group, sizeof *group);
    }
}

const char *phy_lie_group_name(const phy_lie_group *group)
{
    return group != NULL
               ? phy_ir_symbol_name(
                     phy_cas_ir(group->algebra->cas), group->name)
               : NULL;
}

unsigned phy_lie_group_representation_dimension(const phy_lie_group *group)
{
    return group != NULL ? group->representation_dimension : 0u;
}

bool phy_lie_group_is_compact(const phy_lie_group *group)
{
    return group != NULL && group->compact;
}

const phy_lie_algebra *phy_lie_group_algebra(const phy_lie_group *group)
{
    return group != NULL ? group->algebra : NULL;
}
