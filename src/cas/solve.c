#include "cas_internal.h"

/*
 * Reader-shaped exact equation solving.
 *
 * Polynomial decomposition stays in reduce.c; this translation unit owns the
 * semantic boundary around it: equations, denominator exclusions, exact
 * verification, and the List[List[Rule[...]]] result shape.
 */

static phy_status substitute_and_decide(phy_cas *cas, phy_ir_ref expression,
                                        phy_ir_ref variable,
                                        phy_ir_ref value,
                                        phy_cas_decision *out_decision)
{
    const phy_cas_rule rule = {variable, value};
    phy_ir_ref substituted = PHY_IR_NULL;
    phy_status status =
        phy_cas_substitute_node(cas, expression, &rule, 1u, &substituted);
    return status == PHY_OK
               ? phy_cas_decide_zero_node(
                     cas, substituted, out_decision)
               : status;
}

static phy_status build_solution_list(phy_cas *cas, phy_ir_ref variable,
                                      const phy_ir_ref *roots,
                                      size_t root_count,
                                      phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_symbol list_head = phy_ir_intern(ir, "List");
    const phy_ir_symbol rule_head = phy_ir_intern(ir, "Rule");
    if (list_head == PHY_IR_NO_SYMBOL || rule_head == PHY_IR_NO_SYMBOL) {
        return phy_cas_ir_failure(cas);
    }
    if (root_count == 0u) {
        const phy_ir_ref empty = phy_ir_function(ir, list_head, NULL, 0u);
        if (empty == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
        *out_ref = empty;
        return PHY_OK;
    }

    phy_ir_ref solutions[PHY_CAS_POLYNOMIAL_MAX_ROOTS];
    for (size_t index = 0u; index < root_count; ++index) {
        const phy_ir_ref rule_arguments[2] = {variable, roots[index]};
        const phy_ir_ref rule =
            phy_ir_function(ir, rule_head, rule_arguments, 2u);
        if (rule == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
        solutions[index] = phy_ir_function(ir, list_head, &rule, 1u);
        if (solutions[index] == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
    }
    const phy_ir_ref result =
        phy_ir_function(ir, list_head, solutions, root_count);
    if (result == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = result;
    return PHY_OK;
}

phy_status phy_cas_solve(phy_cas *cas, phy_ir_ref equation,
                         phy_ir_ref variable, phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_cas_begin(cas);
    if (phy_ir_kind_of(cas->ir, equation) != PHY_IR_EQUATION ||
        phy_ir_kind_of(cas->ir, variable) != PHY_IR_SYMBOL) {
        return PHY_ERR_TYPE;
    }

    const phy_ir_ref left = phy_ir_child(cas->ir, equation, 0u);
    const phy_ir_ref right = phy_ir_child(cas->ir, equation, 1u);
    phy_ir_ref negative_right = PHY_IR_NULL;
    phy_ir_ref difference = PHY_IR_NULL;
    phy_status status =
        phy_cas_neg_node(cas, right, &negative_right);
    if (status == PHY_OK) {
        const phy_ir_ref terms[2] = {left, negative_right};
        status = phy_cas_add_node(cas, terms, 2u, &difference);
    }
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref numerator = PHY_IR_NULL;
    phy_ir_ref denominator = PHY_IR_NULL;
    status = phy_cas_rational_reduced_node(
        cas, difference, &numerator, &denominator);
    if (status != PHY_OK) {
        return status;
    }

    phy_cas_root_set candidates = {0};
    status = phy_cas_polynomial_roots_node(
        cas, numerator, variable, &candidates);
    if (status != PHY_OK) {
        return status;
    }

    bool has_certified_algebraic = false;
    for (size_t index = 0u; index < candidates.count; ++index) {
        has_certified_algebraic =
            has_certified_algebraic ||
            candidates.certified_algebraic[index];
    }
    if (has_certified_algebraic) {
        bool coprime = false;
        status = phy_cas_polynomials_coprime_node(
            cas, numerator, denominator, variable, &coprime);
        if (status != PHY_OK) {
            return status;
        }
        if (!coprime) {
            /*
             * rational_reduced_node() must have removed a common Q[x]
             * factor. Publishing an algebraic root despite a surviving one
             * would lose a domain exclusion.
             */
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
    }

    phy_ir_ref accepted[PHY_CAS_POLYNOMIAL_MAX_ROOTS];
    size_t accepted_count = 0u;
    for (size_t index = 0u; index < candidates.count; ++index) {
        if (candidates.certified_algebraic[index]) {
            accepted[accepted_count++] = candidates.values[index];
            continue;
        }
        phy_cas_decision numerator_zero = PHY_CAS_UNKNOWN;
        status = substitute_and_decide(
            cas, numerator, variable, candidates.values[index],
            &numerator_zero);
        if (status != PHY_OK) {
            return status;
        }
        if (numerator_zero == PHY_CAS_UNKNOWN) {
            return PHY_ERR_UNSUPPORTED;
        }
        if (numerator_zero != PHY_CAS_ZERO) {
            /* A factor-derived root failing exact verification is internal. */
            return PHY_ERR_CORRUPT_DOCUMENT;
        }

        phy_cas_decision denominator_zero = PHY_CAS_UNKNOWN;
        status = substitute_and_decide(
            cas, denominator, variable, candidates.values[index],
            &denominator_zero);
        if (status != PHY_OK) {
            return status;
        }
        if (denominator_zero == PHY_CAS_UNKNOWN) {
            return PHY_ERR_UNSUPPORTED;
        }
        if (denominator_zero == PHY_CAS_ZERO) {
            continue;
        }
        accepted[accepted_count++] = candidates.values[index];
    }
    return build_solution_list(
        cas, variable, accepted, accepted_count, out_ref);
}
