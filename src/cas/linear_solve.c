#include "cas_internal.h"

/*
 * Certified exact linear systems over the scalar field already understood by
 * the CAS.  This intentionally does not guess whether a symbolic pivot is
 * nonzero: a generic a in a*x=b needs assumptions or a conditional solver,
 * neither of which belongs in an unconditional Rule result.
 */

#define PHY_LINEAR_MAX_EQUATIONS 8u
#define PHY_LINEAR_MAX_VARIABLES 8u

static bool is_list(const phy_cas *cas, phy_ir_ref ref)
{
    if (phy_ir_kind_of(cas->ir, ref) != PHY_IR_FUNCTION) {
        return false;
    }
    const phy_ir_symbol list = phy_ir_intern(cas->ir, "List");
    return list != PHY_IR_NO_SYMBOL && phy_ir_head(cas->ir, ref) == list;
}

static phy_status subtract_node(phy_cas *cas, phy_ir_ref left,
                                phy_ir_ref right, phy_ir_ref *out_ref)
{
    phy_ir_ref negative = PHY_IR_NULL;
    phy_status status = phy_cas_neg_node(cas, right, &negative);
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {left, negative};
        status = phy_cas_add_node(cas, terms, 2u, out_ref);
    }
    return status;
}

static phy_status divide_node(phy_cas *cas, phy_ir_ref numerator,
                              phy_ir_ref denominator, phy_ir_ref *out_ref)
{
    phy_ir_ref inverse = PHY_IR_NULL;
    phy_status status =
        phy_cas_pow_node(cas, denominator, cas->minus_one, &inverse);
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {numerator, inverse};
        status = phy_cas_mul_node(cas, factors, 2u, out_ref);
    }
    return status;
}

static phy_status equation_difference(phy_cas *cas, phy_ir_ref equation,
                                      phy_ir_ref *out_ref)
{
    if (phy_ir_kind_of(cas->ir, equation) != PHY_IR_EQUATION) {
        return PHY_ERR_TYPE;
    }
    return subtract_node(cas, phy_ir_child(cas->ir, equation, 0u),
                         phy_ir_child(cas->ir, equation, 1u), out_ref);
}

static phy_status depends_on_variables(phy_cas *cas, phy_ir_ref expression,
                                       const phy_ir_ref *variables,
                                       size_t variable_count,
                                       bool *out_depends)
{
    *out_depends = false;
    for (size_t column = 0u; column < variable_count; ++column) {
        bool depends = true;
        const phy_status status = phy_cas_may_depend(
            cas, expression, variables[column], &depends);
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

/*
 * Extract a_0,...,a_(n-1),b from expression == 0 as
 * a_0*x_0+...+a_(n-1)*x_(n-1) == b.  Derivatives propose coefficients, but an
 * exact reconstruction/zero proof is what certifies linearity.
 */
static phy_status extract_linear_row(phy_cas *cas, phy_ir_ref equation,
                                     const phy_ir_ref *variables,
                                     size_t variable_count,
                                     phy_ir_ref *row)
{
    phy_ir_ref expression = PHY_IR_NULL;
    phy_status status = equation_difference(cas, equation, &expression);
    if (status != PHY_OK) {
        return status;
    }

    phy_cas_rule zero_rules[PHY_LINEAR_MAX_VARIABLES];
    for (size_t column = 0u; column < variable_count; ++column) {
        zero_rules[column].from = variables[column];
        zero_rules[column].to = cas->zero;
        status = phy_cas_diff_node(
            cas, expression, variables[column], &row[column]);
        if (status != PHY_OK) {
            return status;
        }
        bool depends = true;
        status = depends_on_variables(
            cas, row[column], variables, variable_count, &depends);
        if (status != PHY_OK) {
            return status;
        }
        if (depends) {
            return PHY_ERR_UNSUPPORTED;
        }
    }

    phy_ir_ref constant = PHY_IR_NULL;
    status = phy_cas_substitute_node(
        cas, expression, zero_rules, variable_count, &constant);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref terms[PHY_LINEAR_MAX_VARIABLES + 1u];
    terms[0] = constant;
    for (size_t column = 0u; column < variable_count; ++column) {
        const phy_ir_ref factors[2] = {row[column], variables[column]};
        status = phy_cas_mul_node(
            cas, factors, 2u, &terms[column + 1u]);
        if (status != PHY_OK) {
            return status;
        }
    }
    phy_ir_ref reconstructed = PHY_IR_NULL;
    status = phy_cas_add_node(
        cas, terms, variable_count + 1u, &reconstructed);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref reconstruction_error = PHY_IR_NULL;
    status = subtract_node(
        cas, expression, reconstructed, &reconstruction_error);
    phy_cas_decision linear = PHY_CAS_UNKNOWN;
    if (status == PHY_OK) {
        status = phy_cas_decide_zero_node(
            cas, reconstruction_error, &linear);
    }
    if (status != PHY_OK) {
        return status;
    }
    if (linear != PHY_CAS_ZERO) {
        return PHY_ERR_UNSUPPORTED;
    }
    return phy_cas_neg_node(cas, constant, &row[variable_count]);
}

static phy_status classify_pivot(phy_cas *cas, phy_ir_ref value,
                                 phy_cas_decision *out_decision)
{
    phy_status status =
        phy_cas_decide_zero_node(cas, value, out_decision);
    if (status == PHY_OK && *out_decision == PHY_CAS_UNKNOWN &&
        phy_cas_known_nonzero(cas, value)) {
        *out_decision = PHY_CAS_NONZERO;
    }
    if (status == PHY_OK && *out_decision == PHY_CAS_UNKNOWN) {
        phy_ir_ref inverse = PHY_IR_NULL;
        bool gaussian = false;
        status = phy_cas_gaussian_pow_node(
            cas, value, -1, &inverse, &gaussian);
        if (status == PHY_OK && gaussian) {
            *out_decision = PHY_CAS_NONZERO;
        }
    }
    return status;
}

static phy_status rref(phy_cas *cas,
                       phy_ir_ref matrix[PHY_LINEAR_MAX_EQUATIONS]
                                        [PHY_LINEAR_MAX_VARIABLES + 1u],
                       size_t row_count, size_t variable_count,
                       size_t *pivot_for_column, size_t *out_rank)
{
    const size_t no_pivot = (size_t)-1;
    for (size_t column = 0u; column < variable_count; ++column) {
        pivot_for_column[column] = no_pivot;
    }

    size_t rank = 0u;
    for (size_t column = 0u;
         column < variable_count && rank < row_count; ++column) {
        size_t pivot = no_pivot;
        bool saw_unknown = false;
        for (size_t row = rank; row < row_count; ++row) {
            phy_cas_decision decision = PHY_CAS_UNKNOWN;
            phy_status status =
                classify_pivot(cas, matrix[row][column], &decision);
            if (status != PHY_OK) {
                return status;
            }
            if (decision == PHY_CAS_NONZERO) {
                pivot = row;
                break;
            }
            saw_unknown = saw_unknown || decision == PHY_CAS_UNKNOWN;
        }
        if (pivot == no_pivot) {
            if (saw_unknown) {
                return PHY_ERR_UNSUPPORTED;
            }
            continue;
        }

        if (pivot != rank) {
            for (size_t entry = 0u; entry <= variable_count; ++entry) {
                const phy_ir_ref temporary = matrix[rank][entry];
                matrix[rank][entry] = matrix[pivot][entry];
                matrix[pivot][entry] = temporary;
            }
        }

        const phy_ir_ref divisor = matrix[rank][column];
        for (size_t entry = 0u; entry <= variable_count; ++entry) {
            phy_ir_ref quotient = PHY_IR_NULL;
            const phy_status status = divide_node(
                cas, matrix[rank][entry], divisor, &quotient);
            if (status != PHY_OK) {
                return status;
            }
            matrix[rank][entry] = quotient;
        }

        for (size_t row = 0u; row < row_count; ++row) {
            if (row == rank) {
                continue;
            }
            const phy_ir_ref factor = matrix[row][column];
            phy_cas_decision zero = PHY_CAS_UNKNOWN;
            phy_status status = phy_cas_decide_zero_node(
                cas, factor, &zero);
            if (status != PHY_OK) {
                return status;
            }
            if (zero == PHY_CAS_ZERO) {
                continue;
            }
            for (size_t entry = 0u; entry <= variable_count; ++entry) {
                const phy_ir_ref product_factors[2] = {
                    factor, matrix[rank][entry]};
                phy_ir_ref product = PHY_IR_NULL;
                status = phy_cas_mul_node(
                    cas, product_factors, 2u, &product);
                if (status != PHY_OK) {
                    return status;
                }
                phy_ir_ref reduced = PHY_IR_NULL;
                status = subtract_node(
                    cas, matrix[row][entry], product, &reduced);
                if (status != PHY_OK) {
                    return status;
                }
                matrix[row][entry] = reduced;
            }
        }
        pivot_for_column[column] = rank++;
    }
    *out_rank = rank;
    return PHY_OK;
}

static phy_status build_system_result(
    phy_cas *cas,
    phy_ir_ref matrix[PHY_LINEAR_MAX_EQUATIONS]
                     [PHY_LINEAR_MAX_VARIABLES + 1u],
    size_t variable_count, const size_t *pivot_for_column,
    const phy_ir_ref *variables, phy_ir_ref *solutions,
    phy_cas_rule *verification_rules, size_t *out_rule_count,
    phy_ir_ref *out_ref)
{
    const size_t no_pivot = (size_t)-1;
    const phy_ir_symbol list_head = phy_ir_intern(cas->ir, "List");
    const phy_ir_symbol rule_head = phy_ir_intern(cas->ir, "Rule");
    if (list_head == PHY_IR_NO_SYMBOL || rule_head == PHY_IR_NO_SYMBOL) {
        return phy_cas_ir_failure(cas);
    }

    size_t rule_count = 0u;
    for (size_t column = 0u; column < variable_count; ++column) {
        const size_t row = pivot_for_column[column];
        if (row == no_pivot) {
            continue;
        }
        phy_ir_ref terms[PHY_LINEAR_MAX_VARIABLES + 1u];
        size_t term_count = 0u;
        terms[term_count++] = matrix[row][variable_count];
        for (size_t free_column = 0u;
             free_column < variable_count; ++free_column) {
            if (pivot_for_column[free_column] != no_pivot) {
                continue;
            }
            const phy_ir_ref factors[2] = {
                matrix[row][free_column], variables[free_column]};
            phy_ir_ref product = PHY_IR_NULL;
            phy_status status =
                phy_cas_mul_node(cas, factors, 2u, &product);
            if (status != PHY_OK) {
                return status;
            }
            status = phy_cas_neg_node(
                cas, product, &terms[term_count++]);
            if (status != PHY_OK) {
                return status;
            }
        }
        phy_ir_ref value = PHY_IR_NULL;
        phy_status status =
            phy_cas_add_node(cas, terms, term_count, &value);
        if (status != PHY_OK) {
            return status;
        }
        const phy_ir_ref arguments[2] = {variables[column], value};
        solutions[rule_count] =
            phy_ir_function(cas->ir, rule_head, arguments, 2u);
        if (solutions[rule_count] == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
        verification_rules[rule_count].from = variables[column];
        verification_rules[rule_count].to = value;
        rule_count++;
    }

    const phy_ir_ref branch =
        phy_ir_function(cas->ir, list_head, solutions, rule_count);
    if (branch == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    const phy_ir_ref result =
        phy_ir_function(cas->ir, list_head, &branch, 1u);
    if (result == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_rule_count = rule_count;
    *out_ref = result;
    return PHY_OK;
}

static phy_status verify_system_result(phy_cas *cas,
                                       const phy_ir_ref *equations,
                                       size_t equation_count,
                                       const phy_cas_rule *rules,
                                       size_t rule_count)
{
    for (size_t row = 0u; row < equation_count; ++row) {
        phy_ir_ref expression = PHY_IR_NULL;
        phy_status status =
            equation_difference(cas, equations[row], &expression);
        if (status != PHY_OK) {
            return status;
        }
        phy_ir_ref substituted = PHY_IR_NULL;
        status = phy_cas_substitute_node(
            cas, expression, rules, rule_count, &substituted);
        phy_cas_decision verified = PHY_CAS_UNKNOWN;
        if (status == PHY_OK) {
            status = phy_cas_decide_zero_node(
                cas, substituted, &verified);
        }
        if (status != PHY_OK) {
            return status;
        }
        if (verified != PHY_CAS_ZERO) {
            return verified == PHY_CAS_UNKNOWN
                       ? PHY_ERR_UNSUPPORTED
                       : PHY_ERR_CORRUPT_DOCUMENT;
        }
    }
    return PHY_OK;
}

phy_status phy_cas_solve_system(
    phy_cas *cas, phy_ir_ref equation_spec,
    const phy_ir_ref *variables, size_t variable_count,
    phy_ir_ref *out_ref)
{
    if (cas == NULL || variables == NULL || out_ref == NULL ||
        variable_count == 0u ||
        variable_count > PHY_LINEAR_MAX_VARIABLES) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;

    phy_ir_ref equations[PHY_LINEAR_MAX_EQUATIONS];
    size_t equation_count = 1u;
    if (is_list(cas, equation_spec)) {
        equation_count = phy_ir_child_count(cas->ir, equation_spec);
        if (equation_count == 0u ||
            equation_count > PHY_LINEAR_MAX_EQUATIONS) {
            return PHY_ERR_TERM_LIMIT;
        }
        for (size_t row = 0u; row < equation_count; ++row) {
            equations[row] = phy_ir_child(cas->ir, equation_spec, row);
        }
    } else {
        equations[0] = equation_spec;
    }

    if (variable_count == 1u && equation_count == 1u) {
        return phy_cas_solve(
            cas, equations[0], variables[0], out_ref);
    }

    phy_cas_begin(cas);
    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t column = 0u; column < variable_count; ++column) {
        if (phy_ir_kind_of(cas->ir, variables[column]) != PHY_IR_SYMBOL) {
            return PHY_ERR_TYPE;
        }
        for (size_t prior = 0u; prior < column; ++prior) {
            if (variables[column] == variables[prior]) {
                return PHY_ERR_TYPE;
            }
        }
    }

    phy_ir_ref matrix[PHY_LINEAR_MAX_EQUATIONS]
                     [PHY_LINEAR_MAX_VARIABLES + 1u];
    for (size_t row = 0u; row < equation_count; ++row) {
        status = extract_linear_row(
            cas, equations[row], variables, variable_count, matrix[row]);
        if (status != PHY_OK) {
            return status;
        }
    }

    size_t pivot_for_column[PHY_LINEAR_MAX_VARIABLES];
    size_t rank = 0u;
    status = rref(
        cas, matrix, equation_count, variable_count,
        pivot_for_column, &rank);
    if (status != PHY_OK) {
        return status;
    }

    for (size_t row = rank; row < equation_count; ++row) {
        bool all_zero = true;
        for (size_t column = 0u; column < variable_count; ++column) {
            phy_cas_decision coefficient = PHY_CAS_UNKNOWN;
            status = phy_cas_decide_zero_node(
                cas, matrix[row][column], &coefficient);
            if (status != PHY_OK) {
                return status;
            }
            if (coefficient == PHY_CAS_UNKNOWN) {
                return PHY_ERR_UNSUPPORTED;
            }
            all_zero = all_zero && coefficient == PHY_CAS_ZERO;
        }
        if (!all_zero) {
            continue;
        }
        phy_cas_decision rhs = PHY_CAS_UNKNOWN;
        status = classify_pivot(
            cas, matrix[row][variable_count], &rhs);
        if (status != PHY_OK) {
            return status;
        }
        if (rhs == PHY_CAS_UNKNOWN) {
            return PHY_ERR_UNSUPPORTED;
        }
        if (rhs == PHY_CAS_NONZERO) {
            const phy_ir_symbol list_head =
                phy_ir_intern(cas->ir, "List");
            const phy_ir_ref empty =
                phy_ir_function(cas->ir, list_head, NULL, 0u);
            if (empty == PHY_IR_NULL) {
                return phy_cas_ir_failure(cas);
            }
            *out_ref = empty;
            return PHY_OK;
        }
    }

    phy_ir_ref rules_ir[PHY_LINEAR_MAX_VARIABLES];
    phy_cas_rule verification_rules[PHY_LINEAR_MAX_VARIABLES];
    size_t rule_count = 0u;
    phy_ir_ref candidate = PHY_IR_NULL;
    status = build_system_result(
        cas, matrix, variable_count, pivot_for_column, variables,
        rules_ir, verification_rules, &rule_count, &candidate);
    if (status != PHY_OK) {
        return status;
    }
    status = verify_system_result(
        cas, equations, equation_count, verification_rules, rule_count);
    if (status != PHY_OK) {
        return status;
    }
    *out_ref = candidate;
    return PHY_OK;
}
