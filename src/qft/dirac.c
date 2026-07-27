/*
 * Dirac algebra: gamma strings, Clifford normalisation, Lorentz contraction,
 * and traces without gamma-5. Contracts Q-2, Q-3 and Q-4 of
 * docs/agent-tasks/QFT_DIRAC.md.
 *
 * Everything here is written from the identities and the algorithm register in
 * docs/references/QFT_GAUGE.md 6, not from an upstream implementation. The
 * three rewrites are:
 *
 *   canonical        group factors by spin line, rename chain dummies,
 *                    collect and order terms;
 *   clifford_normal  sort each line under the IR order through
 *                    gamma^a gamma^b = 2 g^{ab} - gamma^b gamma^a;
 *   contract         FORM's trace rules 1 to 4 on a contracted pair;
 *
 * and the trace is the metric recursion, which is what replaces FORM's rule 5
 * so that no gamma-5 and no Levi-Civita tensor is needed.
 */
#include "phy/dirac.h"

#include <string.h>

#include "phy/platform.h"

#define DIRAC_DEFAULT_MAX_GAMMAS 12u
#define DIRAC_DEFAULT_MAX_TERMS 4096u

/* Longest decimal dummy name this layer generates, plus the 'd' and the NUL. */
#define DIRAC_NAME_CAPACITY 16u

typedef struct {
    uint32_t line;
    phy_ir_ref argument; /* PHY_IR_INDEX in the metric's bundle, or a momentum */
} dirac_factor;

typedef struct {
    phy_ir_ref coefficient;
    uint32_t count;
    dirac_factor factors[PHY_DIRAC_MAX_FACTORS];
} dirac_term;

struct phy_dirac {
    const phy_lorentz_metric *metric;
    phy_cas *cas;
    phy_dirac_limits limits;
    phy_ir_ref trace_of_one;
    phy_ir_symbol head_gamma;
    uint32_t generated;
    uint64_t total_generated;
};

struct phy_dirac_expr {
    phy_dirac *dirac;
    size_t count;
    size_t capacity;
    dirac_term *terms;
};

/* ------------------------------------------------------------- bookkeeping */

void phy_dirac_limits_defaults(phy_dirac_limits *out_limits)
{
    if (out_limits != NULL) {
        out_limits->max_gammas_per_line = DIRAC_DEFAULT_MAX_GAMMAS;
        out_limits->max_terms = DIRAC_DEFAULT_MAX_TERMS;
    }
}

static void dirac_begin(phy_dirac *dirac)
{
    dirac->generated = 0u;
}

static phy_status note_generated(phy_dirac *dirac, uint32_t terms)
{
    if (terms > dirac->limits.max_terms - dirac->generated) {
        dirac->generated = dirac->limits.max_terms;
        return PHY_ERR_TERM_LIMIT;
    }
    dirac->generated += terms;
    dirac->total_generated += terms;
    return PHY_OK;
}

static phy_status ir_failure(phy_ir_context *ir)
{
    const phy_status status = phy_ir_last_error(ir);
    return status != PHY_OK ? status : PHY_ERR_OUT_OF_MEMORY;
}

phy_status phy_dirac_create(const phy_lorentz_metric *metric,
                            const phy_dirac_limits *limits,
                            phy_dirac **out_dirac)
{
    if (metric == NULL || out_dirac == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_lorentz_metric_dimension(metric) != 4u) {
        /* Rules 2 and 3 are the D = 4 identities and nothing in this MVP
         * tracks a dimension, so a D != 4 metric is refused up front rather
         * than answered wrongly later. Reference 2 and 4.5. */
        return PHY_ERR_UNSUPPORTED;
    }
    phy_dirac_limits effective;
    phy_dirac_limits_defaults(&effective);
    if (limits != NULL) {
        if (limits->max_gammas_per_line != 0u) {
            effective.max_gammas_per_line = limits->max_gammas_per_line;
        }
        if (limits->max_terms != 0u) {
            effective.max_terms = limits->max_terms;
        }
    }
    if (effective.max_gammas_per_line > PHY_DIRAC_MAX_FACTORS) {
        effective.max_gammas_per_line = PHY_DIRAC_MAX_FACTORS;
    }

    phy_dirac *dirac = phy_alloc(sizeof *dirac);
    if (dirac == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(dirac, 0, sizeof *dirac);
    dirac->metric = metric;
    dirac->cas = phy_lorentz_metric_cas(metric);
    dirac->limits = effective;

    phy_ir_context *ir = phy_cas_ir(dirac->cas);
    dirac->head_gamma = phy_ir_intern(ir, "DiracGamma");
    if (dirac->head_gamma == PHY_IR_NO_SYMBOL) {
        const phy_status status = ir_failure(ir);
        phy_free(dirac, sizeof *dirac);
        return status;
    }
    const phy_status status =
        phy_cas_number(dirac->cas, 4, 1, &dirac->trace_of_one);
    if (status != PHY_OK) {
        phy_free(dirac, sizeof *dirac);
        return status;
    }
    *out_dirac = dirac;
    return PHY_OK;
}

void phy_dirac_destroy(phy_dirac *dirac)
{
    if (dirac != NULL) {
        phy_free(dirac, sizeof *dirac);
    }
}

phy_cas *phy_dirac_cas(const phy_dirac *dirac)
{
    return dirac != NULL ? dirac->cas : NULL;
}

const phy_lorentz_metric *phy_dirac_metric(const phy_dirac *dirac)
{
    return dirac != NULL ? dirac->metric : NULL;
}

phy_status phy_dirac_set_trace_of_one(phy_dirac *dirac, phy_ir_ref value)
{
    if (dirac == NULL || value == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_cas_simplify(dirac->cas, value, &dirac->trace_of_one);
}

phy_ir_ref phy_dirac_trace_of_one(const phy_dirac *dirac)
{
    return dirac != NULL ? dirac->trace_of_one : PHY_IR_NULL;
}

uint32_t phy_dirac_generated_terms(const phy_dirac *dirac)
{
    return dirac != NULL ? dirac->generated : 0u;
}

uint64_t phy_dirac_total_generated_terms(const phy_dirac *dirac)
{
    return dirac != NULL ? dirac->total_generated : 0u;
}

/* ------------------------------------------------------- expression store */

static phy_status expr_create(phy_dirac *dirac, size_t capacity,
                              phy_dirac_expr **out_expr)
{
    if (capacity > (size_t)-1 / sizeof(dirac_term)) {
        return PHY_ERR_TERM_LIMIT;
    }
    phy_dirac_expr *expr = phy_alloc(sizeof *expr);
    if (expr == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(expr, 0, sizeof *expr);
    expr->dirac = dirac;
    if (capacity != 0u) {
        expr->terms = phy_alloc(capacity * sizeof *expr->terms);
        if (expr->terms == NULL) {
            phy_free(expr, sizeof *expr);
            return PHY_ERR_OUT_OF_MEMORY;
        }
        expr->capacity = capacity;
    }
    *out_expr = expr;
    return PHY_OK;
}

void phy_dirac_expr_destroy(phy_dirac_expr *expr)
{
    if (expr == NULL) {
        return;
    }
    if (expr->terms != NULL) {
        phy_free(expr->terms, expr->capacity * sizeof *expr->terms);
    }
    phy_free(expr, sizeof *expr);
}

static phy_status expr_reserve(phy_dirac_expr *expr, size_t wanted)
{
    if (wanted <= expr->capacity) {
        return PHY_OK;
    }
    size_t capacity = expr->capacity != 0u ? expr->capacity : 4u;
    while (capacity < wanted) {
        if (capacity > (size_t)-1 / 2u) {
            capacity = wanted;
            break;
        }
        capacity *= 2u;
    }
    if (capacity > (size_t)-1 / sizeof(dirac_term)) {
        return PHY_ERR_TERM_LIMIT;
    }
    dirac_term *terms = phy_alloc(capacity * sizeof *terms);
    if (terms == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    if (expr->terms != NULL) {
        memcpy(terms, expr->terms, expr->count * sizeof *terms);
        phy_free(expr->terms, expr->capacity * sizeof *expr->terms);
    }
    expr->terms = terms;
    expr->capacity = capacity;
    return PHY_OK;
}

/*
 * Append one term without calling it newly generated.
 *
 * Every term this layer stores goes through here, so no expression can grow
 * past the ceiling whatever produced it; a rewrite that would explode fails as
 * PHY_ERR_TERM_LIMIT with no partial result escaping.
 */
static phy_status expr_append(phy_dirac_expr *expr, const dirac_term *term)
{
    if (expr->count >= expr->dirac->limits.max_terms) {
        return PHY_ERR_TERM_LIMIT;
    }
    const phy_status status = expr_reserve(expr, expr->count + 1u);
    if (status != PHY_OK) {
        return status;
    }
    expr->terms[expr->count++] = *term;
    return PHY_OK;
}

/*
 * Append a term that a rewrite rule created, and charge it to the operation's
 * generated-term budget.
 *
 * Only the rewrites that can multiply terms -- a Clifford swap, a contraction
 * branch, a product, a trace leaf -- charge the budget. Canonicalization,
 * copying and addition never produce more terms than they consume, so counting
 * them would make the reported figure say nothing about how expensive an
 * expression was to reach. It is that figure the "contract before you expand"
 * claim is measured against.
 */
static phy_status expr_push(phy_dirac_expr *expr, const dirac_term *term)
{
    const phy_status status = note_generated(expr->dirac, 1u);
    return status != PHY_OK ? status : expr_append(expr, term);
}

static void term_init(dirac_term *term, phy_ir_ref coefficient)
{
    memset(term, 0, sizeof *term);
    term->coefficient = coefficient;
}

const phy_dirac *phy_dirac_expr_owner(const phy_dirac_expr *expr)
{
    return expr != NULL ? expr->dirac : NULL;
}

size_t phy_dirac_expr_term_count(const phy_dirac_expr *expr)
{
    return expr != NULL ? expr->count : 0u;
}

phy_ir_ref phy_dirac_term_coefficient(const phy_dirac_expr *expr, size_t term)
{
    return (expr != NULL && term < expr->count) ? expr->terms[term].coefficient
                                                : PHY_IR_NULL;
}

size_t phy_dirac_term_factor_count(const phy_dirac_expr *expr, size_t term)
{
    return (expr != NULL && term < expr->count) ? expr->terms[term].count : 0u;
}

phy_status phy_dirac_term_factor(const phy_dirac_expr *expr, size_t term,
                                 size_t factor, uint32_t *out_line,
                                 phy_ir_ref *out_argument)
{
    if (expr == NULL || out_line == NULL || out_argument == NULL ||
        term >= expr->count || factor >= expr->terms[term].count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_line = expr->terms[term].factors[factor].line;
    *out_argument = expr->terms[term].factors[factor].argument;
    return PHY_OK;
}

/* ------------------------------------------------------------ construction */

phy_status phy_dirac_zero(phy_dirac *dirac, phy_dirac_expr **out_expr)
{
    if (dirac == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(dirac);
    return expr_create(dirac, 0u, out_expr);
}

static phy_status make_single(phy_dirac *dirac, phy_ir_ref coefficient,
                              const dirac_factor *factor,
                              phy_dirac_expr **out_expr)
{
    phy_dirac_expr *expr = NULL;
    phy_status status = expr_create(dirac, 1u, &expr);
    if (status != PHY_OK) {
        return status;
    }
    dirac_term term;
    term_init(&term, coefficient);
    if (factor != NULL) {
        term.factors[0] = *factor;
        term.count = 1u;
    }
    status = expr_append(expr, &term);
    if (status != PHY_OK) {
        phy_dirac_expr_destroy(expr);
        return status;
    }
    *out_expr = expr;
    return PHY_OK;
}

phy_status phy_dirac_scalar(phy_dirac *dirac, phy_ir_ref value,
                            phy_dirac_expr **out_expr)
{
    if (dirac == NULL || value == PHY_IR_NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(dirac);
    phy_ir_ref simplified;
    const phy_status status = phy_cas_simplify(dirac->cas, value, &simplified);
    if (status != PHY_OK) {
        return status;
    }
    return make_single(dirac, simplified, NULL, out_expr);
}

phy_status phy_dirac_identity(phy_dirac *dirac, uint32_t line,
                              phy_dirac_expr **out_expr)
{
    if (dirac == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    (void)line;
    dirac_begin(dirac);
    phy_ir_ref one;
    const phy_status status = phy_cas_number(dirac->cas, 1, 1, &one);
    if (status != PHY_OK) {
        return status;
    }
    return make_single(dirac, one, NULL, out_expr);
}

static phy_status make_factor_expr(phy_dirac *dirac, uint32_t line,
                                   phy_ir_ref argument,
                                   phy_dirac_expr **out_expr)
{
    dirac_begin(dirac);
    phy_ir_ref one;
    const phy_status status = phy_cas_number(dirac->cas, 1, 1, &one);
    if (status != PHY_OK) {
        return status;
    }
    dirac_factor factor;
    factor.line = line;
    factor.argument = argument;
    return make_single(dirac, one, &factor, out_expr);
}

phy_status phy_dirac_gamma(phy_dirac *dirac, uint32_t line, phy_ir_ref index,
                           phy_dirac_expr **out_expr)
{
    if (dirac == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!phy_lorentz_owns_index(dirac->metric, index)) {
        /* An index of another metric -- the GR slice's opposite signature, for
         * instance -- never enters a gamma string silently. */
        return PHY_ERR_TYPE;
    }
    return make_factor_expr(dirac, line, index, out_expr);
}

phy_status phy_dirac_slash(phy_dirac *dirac, uint32_t line, phy_ir_ref momentum,
                           phy_dirac_expr **out_expr)
{
    if (dirac == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!phy_lorentz_owns_momentum(dirac->metric, momentum)) {
        return PHY_ERR_TYPE;
    }
    return make_factor_expr(dirac, line, momentum, out_expr);
}

static phy_status expr_clone(const phy_dirac_expr *expr,
                             phy_dirac_expr **out_expr)
{
    phy_dirac_expr *copy = NULL;
    phy_status status = expr_create(expr->dirac, expr->count, &copy);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 0u; i < expr->count; i++) {
        status = expr_append(copy, &expr->terms[i]);
        if (status != PHY_OK) {
            phy_dirac_expr_destroy(copy);
            return status;
        }
    }
    *out_expr = copy;
    return PHY_OK;
}

phy_status phy_dirac_expr_copy(const phy_dirac_expr *expr,
                               phy_dirac_expr **out_expr)
{
    if (expr == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(expr->dirac);
    return expr_clone(expr, out_expr);
}

phy_status phy_dirac_add(const phy_dirac_expr *left,
                         const phy_dirac_expr *right,
                         phy_dirac_expr **out_expr)
{
    if (left == NULL || right == NULL || out_expr == NULL ||
        left->dirac != right->dirac) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(left->dirac);
    phy_dirac_expr *sum = NULL;
    phy_status status = expr_create(left->dirac, left->count + right->count,
                                    &sum);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 0u; i < left->count && status == PHY_OK; i++) {
        status = expr_append(sum, &left->terms[i]);
    }
    for (size_t i = 0u; i < right->count && status == PHY_OK; i++) {
        status = expr_append(sum, &right->terms[i]);
    }
    if (status != PHY_OK) {
        phy_dirac_expr_destroy(sum);
        return status;
    }
    *out_expr = sum;
    return PHY_OK;
}

phy_status phy_dirac_scale(const phy_dirac_expr *expr, phy_ir_ref scalar,
                           phy_dirac_expr **out_expr)
{
    if (expr == NULL || scalar == PHY_IR_NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(expr->dirac);
    phy_dirac_expr *scaled = NULL;
    phy_status status = expr_create(expr->dirac, expr->count, &scaled);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 0u; i < expr->count; i++) {
        dirac_term term = expr->terms[i];
        const phy_ir_ref pair[2] = {term.coefficient, scalar};
        status = phy_cas_mul(expr->dirac->cas, pair, 2u, &term.coefficient);
        if (status == PHY_OK) {
            status = expr_append(scaled, &term);
        }
        if (status != PHY_OK) {
            phy_dirac_expr_destroy(scaled);
            return status;
        }
    }
    *out_expr = scaled;
    return PHY_OK;
}

static phy_status check_line_lengths(const phy_dirac *dirac,
                                     const dirac_term *term)
{
    for (uint32_t i = 0u; i < term->count; i++) {
        uint32_t seen = 0u;
        for (uint32_t j = 0u; j < term->count; j++) {
            if (term->factors[j].line == term->factors[i].line) {
                seen++;
            }
        }
        if (seen > dirac->limits.max_gammas_per_line) {
            return PHY_ERR_TERM_LIMIT;
        }
    }
    return PHY_OK;
}

phy_status phy_dirac_mul(const phy_dirac_expr *left,
                         const phy_dirac_expr *right,
                         phy_dirac_expr **out_expr)
{
    if (left == NULL || right == NULL || out_expr == NULL ||
        left->dirac != right->dirac) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_dirac *dirac = left->dirac;
    dirac_begin(dirac);

    if (left->count != 0u &&
        right->count > (size_t)dirac->limits.max_terms / left->count) {
        return PHY_ERR_TERM_LIMIT;
    }

    phy_dirac_expr *product = NULL;
    phy_status status = expr_create(dirac, left->count * right->count,
                                    &product);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 0u; i < left->count && status == PHY_OK; i++) {
        for (size_t j = 0u; j < right->count && status == PHY_OK; j++) {
            const dirac_term *a = &left->terms[i];
            const dirac_term *b = &right->terms[j];
            if ((size_t)a->count + (size_t)b->count > PHY_DIRAC_MAX_FACTORS) {
                status = PHY_ERR_UNSUPPORTED;
                break;
            }
            dirac_term term;
            const phy_ir_ref pair[2] = {a->coefficient, b->coefficient};
            term_init(&term, PHY_IR_NULL);
            status = phy_cas_mul(dirac->cas, pair, 2u, &term.coefficient);
            if (status != PHY_OK) {
                break;
            }
            memcpy(term.factors, a->factors, a->count * sizeof *a->factors);
            memcpy(&term.factors[a->count], b->factors,
                   b->count * sizeof *b->factors);
            term.count = a->count + b->count;
            status = check_line_lengths(dirac, &term);
            if (status == PHY_OK) {
                status = expr_push(product, &term);
            }
        }
    }
    if (status != PHY_OK) {
        phy_dirac_expr_destroy(product);
        return status;
    }
    *out_expr = product;
    return PHY_OK;
}

/* ---------------------------------------------------------------- helpers */

/*
 * The pairing that both the Clifford relation and the trace recursion need:
 * two index slots give the metric, two momenta give a scalar product, and one
 * of each gives the component p^a. Keeping it in one function is what lets
 * gamma^mu and p-slash share a single code path, as FORM's g_(1,...) does.
 */
static phy_status slot_dot(const phy_dirac *dirac, phy_ir_ref a, phy_ir_ref b,
                           phy_ir_ref *out_scalar)
{
    const phy_lorentz_metric *metric = dirac->metric;
    const bool a_index = phy_lorentz_owns_index(metric, a);
    const bool b_index = phy_lorentz_owns_index(metric, b);
    if (a_index && b_index) {
        return phy_lorentz_metric_tensor(metric, a, b, out_scalar);
    }
    if (a_index) {
        return phy_lorentz_momentum_component(metric, b, a, out_scalar);
    }
    if (b_index) {
        return phy_lorentz_momentum_component(metric, a, b, out_scalar);
    }
    return phy_lorentz_dot(metric, a, b, out_scalar);
}

static phy_status scale_coefficient(phy_dirac *dirac, dirac_term *term,
                                    phy_ir_ref factor)
{
    const phy_ir_ref pair[2] = {term->coefficient, factor};
    return phy_cas_mul(dirac->cas, pair, 2u, &term->coefficient);
}

static phy_status scale_coefficient_int(phy_dirac *dirac, dirac_term *term,
                                        int64_t value)
{
    phy_ir_ref number;
    const phy_status status = phy_cas_number(dirac->cas, value, 1, &number);
    return status != PHY_OK ? status : scale_coefficient(dirac, term, number);
}

static void remove_factors(dirac_term *term, uint32_t first, uint32_t second)
{
    const uint32_t low = (first < second) ? first : second;
    const uint32_t high = (first < second) ? second : first;
    uint32_t out = 0u;
    for (uint32_t i = 0u; i < term->count; i++) {
        if (i == low || i == high) {
            continue;
        }
        term->factors[out++] = term->factors[i];
    }
    term->count = out;
}

/* -------------------------------------------------------- canonical form */

typedef struct {
    phy_ir_symbol name;
    uint32_t upper;   /* occurrences among the gamma factors, never elsewhere */
    uint32_t lower;
    uint32_t first;   /* position of the first occurrence among the factors */
    bool in_coefficient;
} index_record;

/*
 * The index census of one term.
 *
 * Balance is counted over the gamma factors alone. It cannot be counted over
 * the coefficient as well, because a coefficient is typically a SUM -- an
 * eight-gamma trace is a sum of 105 products -- and index balance is a
 * property of each addend, not of the sum. What the coefficient contributes
 * here is a set of names: an index the coefficient mentions is not this
 * chain's to rename or to contract, whatever it means there.
 */
static phy_status term_index_records(const phy_dirac *dirac,
                                     const dirac_term *term,
                                     index_record *records, size_t capacity,
                                     size_t *out_count)
{
    const phy_lorentz_metric *metric = dirac->metric;
    const phy_ir_context *ir = phy_cas_ir(dirac->cas);
    size_t count = 0u;

    for (uint32_t i = 0u; i < term->count; i++) {
        const phy_ir_ref argument = term->factors[i].argument;
        if (!phy_lorentz_owns_index(metric, argument)) {
            continue;
        }
        const phy_ir_symbol name = phy_ir_head(ir, argument);
        phy_ir_variance variance;
        if (!phy_ir_index_variance(ir, argument, &variance)) {
            return PHY_ERR_TYPE;
        }
        size_t slot = count;
        for (size_t k = 0u; k < count; k++) {
            if (records[k].name == name) {
                slot = k;
                break;
            }
        }
        if (slot == count) {
            if (count == capacity) {
                return PHY_ERR_UNSUPPORTED;
            }
            records[count].name = name;
            records[count].upper = 0u;
            records[count].lower = 0u;
            records[count].first = i;
            records[count].in_coefficient = false;
            count++;
        }
        if (variance == PHY_IR_INDEX_UPPER) {
            records[slot].upper++;
        } else {
            records[slot].lower++;
        }
    }

    bool any_pair = false;
    for (size_t k = 0u; k < count; k++) {
        if (records[k].upper > 1u || records[k].lower > 1u) {
            /* Three occurrences, or two in the same position, is not a
             * contraction. Refusing it is the point of a typed index. */
            return PHY_ERR_TYPE;
        }
        any_pair = any_pair || (records[k].upper == 1u && records[k].lower == 1u);
    }
    if (!any_pair) {
        /* Nothing to rename and nothing to contract, so the coefficient's
         * names cannot matter and the walk over it is skipped. */
        *out_count = count;
        return PHY_OK;
    }

    phy_lorentz_index_use uses[PHY_DIRAC_MAX_FACTORS];
    size_t use_count = 0u;
    const phy_status status = phy_lorentz_index_census(
        metric, term->coefficient, uses, PHY_DIRAC_MAX_FACTORS, &use_count);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t u = 0u; u < use_count; u++) {
        bool merged = false;
        for (size_t k = 0u; k < count; k++) {
            if (records[k].name == uses[u].name) {
                records[k].in_coefficient = true;
                merged = true;
                break;
            }
        }
        if (!merged) {
            if (count == capacity) {
                return PHY_ERR_UNSUPPORTED;
            }
            records[count].name = uses[u].name;
            records[count].upper = 0u;
            records[count].lower = 0u;
            records[count].first = term->count;
            records[count].in_coefficient = true;
            count++;
        }
    }
    *out_count = count;
    return PHY_OK;
}

static void sort_factors_by_line(dirac_term *term)
{
    /* Insertion sort, and stable on purpose: factors on one spin line must
     * keep the order they were written in, because they do not commute. */
    for (uint32_t i = 1u; i < term->count; i++) {
        const dirac_factor key = term->factors[i];
        uint32_t j = i;
        while (j > 0u && term->factors[j - 1u].line > key.line) {
            term->factors[j] = term->factors[j - 1u];
            j--;
        }
        term->factors[j] = key;
    }
}

static void dummy_name(unsigned ordinal, char *buffer)
{
    char digits[12];
    size_t used = 0u;
    unsigned value = ordinal;
    do {
        digits[used++] = (char)('0' + (int)(value % 10u));
        value /= 10u;
    } while (value != 0u);

    size_t at = 0u;
    buffer[at++] = 'd';
    while (used != 0u) {
        buffer[at++] = digits[--used];
    }
    buffer[at] = '\0';
}

/*
 * Rename the term's contracted dummies to d1, d2, ... in first-occurrence
 * order over the line-grouped factor list.
 *
 * A candidate name is skipped when the term already uses it for something that
 * is not being renamed, so a chain whose free index happens to be called d1
 * does not collide. The whole map is applied at once, which is why a term
 * whose dummies are already called d2 and d1 simply swaps them.
 */
static phy_status rename_dummies(const phy_dirac *dirac, dirac_term *term)
{
    index_record records[PHY_DIRAC_MAX_FACTORS];
    size_t count = 0u;
    phy_status status =
        term_index_records(dirac, term, records, PHY_DIRAC_MAX_FACTORS, &count);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_context *ir = phy_cas_ir(dirac->cas);

    phy_ir_symbol from[PHY_DIRAC_MAX_FACTORS];
    phy_ir_symbol to[PHY_DIRAC_MAX_FACTORS];
    size_t mapped = 0u;
    unsigned ordinal = 1u;

    for (uint32_t position = 0u; position < term->count; position++) {
        for (size_t k = 0u; k < count; k++) {
            if (records[k].first != position || records[k].in_coefficient ||
                records[k].upper != 1u || records[k].lower != 1u) {
                continue;
            }
            char name[DIRAC_NAME_CAPACITY];
            phy_ir_symbol candidate;
            for (;;) {
                dummy_name(ordinal, name);
                ordinal++;
                candidate = phy_ir_intern(ir, name);
                if (candidate == PHY_IR_NO_SYMBOL) {
                    return ir_failure(ir);
                }
                bool reserved = false;
                for (size_t r = 0u; r < count && !reserved; r++) {
                    const bool renamed = !records[r].in_coefficient &&
                                         records[r].upper == 1u &&
                                         records[r].lower == 1u;
                    reserved = !renamed && records[r].name == candidate;
                }
                if (!reserved) {
                    break;
                }
            }
            from[mapped] = records[k].name;
            to[mapped] = candidate;
            mapped++;
        }
    }
    if (mapped == 0u) {
        return PHY_OK;
    }

    const phy_ir_symbol space = phy_lorentz_metric_space(dirac->metric);
    for (uint32_t i = 0u; i < term->count; i++) {
        const phy_ir_ref argument = term->factors[i].argument;
        if (!phy_lorentz_owns_index(dirac->metric, argument)) {
            continue;
        }
        const phy_ir_symbol name = phy_ir_head(ir, argument);
        for (size_t m = 0u; m < mapped; m++) {
            if (from[m] != name) {
                continue;
            }
            phy_ir_variance variance;
            if (!phy_ir_index_variance(ir, argument, &variance)) {
                return PHY_ERR_TYPE;
            }
            const phy_ir_ref renamed =
                phy_ir_index_in_space(ir, to[m], variance, space);
            if (renamed == PHY_IR_NULL) {
                return ir_failure(ir);
            }
            term->factors[i].argument = renamed;
            break;
        }
    }
    return PHY_OK;
}

static int factor_compare(const phy_dirac *dirac, const dirac_factor *a,
                          const dirac_factor *b)
{
    if (a->line != b->line) {
        return (a->line < b->line) ? -1 : 1;
    }
    return phy_ir_compare(phy_cas_ir(dirac->cas), a->argument, b->argument);
}

static int term_compare(const phy_dirac *dirac, const dirac_term *a,
                        const dirac_term *b)
{
    if (a->count != b->count) {
        return (a->count < b->count) ? -1 : 1;
    }
    for (uint32_t i = 0u; i < a->count; i++) {
        const int order = factor_compare(dirac, &a->factors[i], &b->factors[i]);
        if (order != 0) {
            return order;
        }
    }
    return 0;
}

static void merge_terms(const phy_dirac *dirac, dirac_term *terms,
                        dirac_term *scratch, size_t low, size_t middle,
                        size_t high)
{
    size_t i = low;
    size_t j = middle;
    size_t out = low;
    while (i < middle && j < high) {
        if (term_compare(dirac, &terms[i], &terms[j]) <= 0) {
            scratch[out++] = terms[i++];
        } else {
            scratch[out++] = terms[j++];
        }
    }
    while (i < middle) {
        scratch[out++] = terms[i++];
    }
    while (j < high) {
        scratch[out++] = terms[j++];
    }
    memcpy(&terms[low], &scratch[low], (high - low) * sizeof *terms);
}

static void sort_terms(const phy_dirac *dirac, dirac_term *terms,
                       dirac_term *scratch, size_t low, size_t high)
{
    if (high - low < 2u) {
        return;
    }
    const size_t middle = low + (high - low) / 2u;
    sort_terms(dirac, terms, scratch, low, middle);
    sort_terms(dirac, terms, scratch, middle, high);
    merge_terms(dirac, terms, scratch, low, middle, high);
}

static phy_status canonical_node(const phy_dirac_expr *expr,
                                 phy_dirac_expr **out_expr)
{
    phy_dirac *dirac = expr->dirac;
    phy_dirac_expr *result = NULL;
    phy_status status = expr_create(dirac, expr->count, &result);
    if (status != PHY_OK) {
        return status;
    }

    for (size_t i = 0u; i < expr->count; i++) {
        dirac_term term = expr->terms[i];
        for (uint32_t f = 0u; f < term.count; f++) {
            const phy_ir_ref argument = term.factors[f].argument;
            if (!phy_lorentz_owns_index(dirac->metric, argument) &&
                !phy_lorentz_owns_momentum(dirac->metric, argument)) {
                status = PHY_ERR_TYPE;
                break;
            }
        }
        if (status == PHY_OK) {
            status = phy_cas_simplify(dirac->cas, term.coefficient,
                                      &term.coefficient);
        }
        if (status == PHY_OK) {
            sort_factors_by_line(&term);
            status = check_line_lengths(dirac, &term);
        }
        if (status == PHY_OK) {
            status = rename_dummies(dirac, &term);
        }
        if (status != PHY_OK) {
            phy_dirac_expr_destroy(result);
            return status;
        }
        int64_t value;
        if (phy_ir_integer_value(phy_cas_ir(dirac->cas), term.coefficient,
                                 &value) &&
            value == 0) {
            continue;
        }
        status = expr_append(result, &term);
        if (status != PHY_OK) {
            phy_dirac_expr_destroy(result);
            return status;
        }
    }

    if (result->count > 1u) {
        dirac_term *scratch = phy_alloc(result->count * sizeof *scratch);
        if (scratch == NULL) {
            phy_dirac_expr_destroy(result);
            return PHY_ERR_OUT_OF_MEMORY;
        }
        sort_terms(dirac, result->terms, scratch, 0u, result->count);
        phy_free(scratch, result->count * sizeof *scratch);
    }

    /* Collect: sorting put equal factor lists next to each other, so one
     * forward pass adds their coefficients. */
    size_t out = 0u;
    for (size_t i = 0u; i < result->count; i++) {
        if (out != 0u &&
            term_compare(dirac, &result->terms[out - 1u], &result->terms[i]) ==
                0) {
            const phy_ir_ref pair[2] = {result->terms[out - 1u].coefficient,
                                        result->terms[i].coefficient};
            status = phy_cas_add(dirac->cas, pair, 2u,
                                 &result->terms[out - 1u].coefficient);
            if (status != PHY_OK) {
                phy_dirac_expr_destroy(result);
                return status;
            }
            continue;
        }
        result->terms[out++] = result->terms[i];
    }
    result->count = out;

    out = 0u;
    for (size_t i = 0u; i < result->count; i++) {
        int64_t value;
        if (phy_ir_integer_value(phy_cas_ir(dirac->cas),
                                 result->terms[i].coefficient, &value) &&
            value == 0) {
            continue;
        }
        result->terms[out++] = result->terms[i];
    }
    result->count = out;

    *out_expr = result;
    return PHY_OK;
}

phy_status phy_dirac_canonical(const phy_dirac_expr *expr,
                               phy_dirac_expr **out_expr)
{
    if (expr == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(expr->dirac);
    return canonical_node(expr, out_expr);
}

/* ------------------------------------------------------ Clifford normal form */

static phy_status clifford_node(const phy_dirac_expr *expr,
                                phy_dirac_expr **out_expr)
{
    phy_dirac *dirac = expr->dirac;
    phy_dirac_expr *pending = NULL;
    phy_status status = canonical_node(expr, &pending);
    if (status != PHY_OK) {
        return status;
    }
    phy_dirac_expr *settled = NULL;
    status = expr_create(dirac, pending->count, &settled);
    if (status != PHY_OK) {
        phy_dirac_expr_destroy(pending);
        return status;
    }

    while (pending->count != 0u && status == PHY_OK) {
        dirac_term term = pending->terms[--pending->count];

        uint32_t swap_at = term.count;
        uint32_t equal_at = term.count;
        for (uint32_t i = 0u; i + 1u < term.count; i++) {
            if (term.factors[i].line != term.factors[i + 1u].line) {
                continue;
            }
            const int order =
                factor_compare(dirac, &term.factors[i], &term.factors[i + 1u]);
            if (order > 0 && swap_at == term.count) {
                swap_at = i;
            }
            if (order == 0 && equal_at == term.count) {
                equal_at = i;
            }
        }

        if (equal_at != term.count) {
            /* p-slash p-slash = (p.p) 1. Two equal index slots cannot reach
             * here: term_index_records() rejects a repeated position. */
            dirac_term collapsed = term;
            phy_ir_ref square;
            status = slot_dot(dirac, term.factors[equal_at].argument,
                              term.factors[equal_at].argument, &square);
            if (status == PHY_OK) {
                status = scale_coefficient(dirac, &collapsed, square);
            }
            if (status == PHY_OK) {
                remove_factors(&collapsed, equal_at, equal_at + 1u);
                status = expr_push(pending, &collapsed);
            }
            continue;
        }
        if (swap_at == term.count) {
            status = expr_append(settled, &term);
            continue;
        }

        /* gamma^a gamma^b = 2 g^{ab} 1 - gamma^b gamma^a. */
        dirac_term contracted = term;
        phy_ir_ref pairing;
        status = slot_dot(dirac, term.factors[swap_at].argument,
                          term.factors[swap_at + 1u].argument, &pairing);
        if (status == PHY_OK) {
            status = scale_coefficient(dirac, &contracted, pairing);
        }
        if (status == PHY_OK) {
            status = scale_coefficient_int(dirac, &contracted, 2);
        }
        if (status == PHY_OK) {
            remove_factors(&contracted, swap_at, swap_at + 1u);
            status = expr_push(pending, &contracted);
        }
        if (status != PHY_OK) {
            break;
        }

        dirac_term swapped = term;
        const dirac_factor keep = swapped.factors[swap_at];
        swapped.factors[swap_at] = swapped.factors[swap_at + 1u];
        swapped.factors[swap_at + 1u] = keep;
        status = scale_coefficient_int(dirac, &swapped, -1);
        if (status == PHY_OK) {
            status = expr_push(pending, &swapped);
        }
    }

    phy_dirac_expr_destroy(pending);
    if (status != PHY_OK) {
        phy_dirac_expr_destroy(settled);
        return status;
    }
    status = canonical_node(settled, out_expr);
    phy_dirac_expr_destroy(settled);
    return status;
}

phy_status phy_dirac_clifford_normal(const phy_dirac_expr *expr,
                                     phy_dirac_expr **out_expr)
{
    if (expr == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(expr->dirac);
    return clifford_node(expr, out_expr);
}

/* ---------------------------------------------------------- contraction */

/*
 * Locate one contracted pair inside a single spin line: an index name carried
 * once up and once down by two gamma factors of the same line, and absent from
 * the coefficient. The input must already be line-grouped, which is why every
 * caller runs canonical_node() first -- it is what makes "the factors between
 * the pair" a well-defined segment.
 */
static phy_status find_contracted_pair(const phy_dirac *dirac,
                                       const dirac_term *term, bool *out_found,
                                       uint32_t *out_first, uint32_t *out_last)
{
    index_record records[PHY_DIRAC_MAX_FACTORS];
    size_t count = 0u;
    const phy_status status =
        term_index_records(dirac, term, records, PHY_DIRAC_MAX_FACTORS, &count);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_context *ir = phy_cas_ir(dirac->cas);
    *out_found = false;

    for (uint32_t i = 0u; i + 1u < term->count; i++) {
        const phy_ir_ref a = term->factors[i].argument;
        if (!phy_lorentz_owns_index(dirac->metric, a)) {
            continue;
        }
        const phy_ir_symbol name = phy_ir_head(ir, a);
        bool eligible = false;
        for (size_t k = 0u; k < count; k++) {
            if (records[k].name == name) {
                eligible = !records[k].in_coefficient &&
                           records[k].upper == 1u && records[k].lower == 1u;
                break;
            }
        }
        if (!eligible) {
            continue;
        }
        for (uint32_t j = i + 1u; j < term->count; j++) {
            const phy_ir_ref b = term->factors[j].argument;
            if (!phy_lorentz_owns_index(dirac->metric, b) ||
                phy_ir_head(ir, b) != name) {
                continue;
            }
            if (term->factors[i].line != term->factors[j].line) {
                /* A pair split across two spin lines is a genuine contraction
                 * that the Clifford rules cannot touch; leave it alone. */
                break;
            }
            *out_found = true;
            *out_first = i;
            *out_last = j;
            return PHY_OK;
        }
    }
    return PHY_OK;
}

static void copy_segment(dirac_term *out, const dirac_term *in, uint32_t from,
                         uint32_t to, bool reversed)
{
    if (!reversed) {
        for (uint32_t i = from; i < to; i++) {
            out->factors[out->count++] = in->factors[i];
        }
        return;
    }
    for (uint32_t i = to; i > from; i--) {
        out->factors[out->count++] = in->factors[i - 1u];
    }
}

static phy_status contract_node(const phy_dirac_expr *expr,
                                phy_dirac_expr **out_expr)
{
    phy_dirac *dirac = expr->dirac;
    phy_dirac_expr *pending = NULL;
    phy_status status = canonical_node(expr, &pending);
    if (status != PHY_OK) {
        return status;
    }
    phy_dirac_expr *settled = NULL;
    status = expr_create(dirac, pending->count, &settled);
    if (status != PHY_OK) {
        phy_dirac_expr_destroy(pending);
        return status;
    }

    while (pending->count != 0u && status == PHY_OK) {
        const dirac_term term = pending->terms[--pending->count];
        bool found = false;
        uint32_t first = 0u;
        uint32_t last = 0u;
        status = find_contracted_pair(dirac, &term, &found, &first, &last);
        if (status != PHY_OK) {
            break;
        }
        if (!found) {
            status = expr_append(settled, &term);
            continue;
        }

        const uint32_t between = last - first - 1u;

        if (between == 0u) {
            /* Rule 1: gamma^mu gamma_mu = D . 1, and slot_dot folds g^mu_mu
             * to the metric's dimension exactly. */
            dirac_term reduced = term;
            phy_ir_ref trace;
            status = slot_dot(dirac, term.factors[first].argument,
                              term.factors[last].argument, &trace);
            if (status == PHY_OK) {
                status = scale_coefficient(dirac, &reduced, trace);
            }
            if (status == PHY_OK) {
                remove_factors(&reduced, first, last);
                status = expr_push(pending, &reduced);
            }
            continue;
        }

        if (between == 2u) {
            /* Rule 3, special case: gamma^mu b1 b2 gamma_mu = 4 g^{b1 b2}. */
            dirac_term reduced = term;
            phy_ir_ref pairing;
            status = slot_dot(dirac, term.factors[first + 1u].argument,
                              term.factors[first + 2u].argument, &pairing);
            if (status == PHY_OK) {
                status = scale_coefficient(dirac, &reduced, pairing);
            }
            if (status == PHY_OK) {
                status = scale_coefficient_int(dirac, &reduced, 4);
            }
            if (status == PHY_OK) {
                reduced.count = 0u;
                copy_segment(&reduced, &term, 0u, first, false);
                copy_segment(&reduced, &term, last + 1u, term.count, false);
                status = expr_push(pending, &reduced);
            }
            continue;
        }

        if ((between & 1u) != 0u) {
            /* Rule 2: an odd number between reverses the segment and gives
             * the factor -2. Everything outside the pair keeps its place --
             * SymPy issue #23823 is exactly this sentence being violated. */
            dirac_term reduced;
            term_init(&reduced, term.coefficient);
            copy_segment(&reduced, &term, 0u, first, false);
            copy_segment(&reduced, &term, first + 1u, last, true);
            copy_segment(&reduced, &term, last + 1u, term.count, false);
            status = scale_coefficient_int(dirac, &reduced, -2);
            if (status == PHY_OK) {
                status = expr_push(pending, &reduced);
            }
            continue;
        }

        /*
         * Rule 3, general case with an even number between:
         *   2 b_m b_1 .. b_{m-1}  +  2 b_{m-1} .. b_1 b_m .
         */
        dirac_term head_first;
        term_init(&head_first, term.coefficient);
        copy_segment(&head_first, &term, 0u, first, false);
        head_first.factors[head_first.count++] = term.factors[last - 1u];
        copy_segment(&head_first, &term, first + 1u, last - 1u, false);
        copy_segment(&head_first, &term, last + 1u, term.count, false);
        status = scale_coefficient_int(dirac, &head_first, 2);
        if (status == PHY_OK) {
            status = expr_push(pending, &head_first);
        }
        if (status != PHY_OK) {
            break;
        }

        dirac_term tail_last;
        term_init(&tail_last, term.coefficient);
        copy_segment(&tail_last, &term, 0u, first, false);
        copy_segment(&tail_last, &term, first + 1u, last - 1u, true);
        tail_last.factors[tail_last.count++] = term.factors[last - 1u];
        copy_segment(&tail_last, &term, last + 1u, term.count, false);
        status = scale_coefficient_int(dirac, &tail_last, 2);
        if (status == PHY_OK) {
            status = expr_push(pending, &tail_last);
        }
    }

    phy_dirac_expr_destroy(pending);
    if (status != PHY_OK) {
        phy_dirac_expr_destroy(settled);
        return status;
    }
    status = canonical_node(settled, out_expr);
    phy_dirac_expr_destroy(settled);
    return status;
}

phy_status phy_dirac_contract(const phy_dirac_expr *expr,
                              phy_dirac_expr **out_expr)
{
    if (expr == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(expr->dirac);
    return contract_node(expr, out_expr);
}

/* --------------------------------------------------------------- traces */

/*
 * Tr[g^{m1}..g^{m2n}] = sum_{j=2}^{2n} (-1)^j g^{m1 mj} Tr[.. mj omitted ..],
 * terminating at the Tr[1] parameter. The leaves are counted, so the
 * generated-term figure is exactly (2n-1)!! for an uncontracted string.
 */
static phy_status trace_string(phy_dirac *dirac, const phy_ir_ref *slots,
                               uint32_t count, phy_ir_ref *out_scalar)
{
    if (count == 0u) {
        const phy_status status = note_generated(dirac, 1u);
        *out_scalar = dirac->trace_of_one;
        return status;
    }
    phy_ir_ref parts[PHY_DIRAC_MAX_FACTORS];
    phy_ir_ref rest[PHY_DIRAC_MAX_FACTORS];
    uint32_t used = 0u;

    for (uint32_t j = 1u; j < count; j++) {
        phy_ir_ref pairing;
        phy_status status = slot_dot(dirac, slots[0], slots[j], &pairing);
        if (status != PHY_OK) {
            return status;
        }
        uint32_t kept = 0u;
        for (uint32_t k = 1u; k < count; k++) {
            if (k != j) {
                rest[kept++] = slots[k];
            }
        }
        phy_ir_ref sub;
        status = trace_string(dirac, rest, kept, &sub);
        if (status != PHY_OK) {
            return status;
        }
        const phy_ir_ref pair[2] = {pairing, sub};
        status = phy_cas_mul(dirac->cas, pair, 2u, &parts[used]);
        if (status != PHY_OK) {
            return status;
        }
        /* (-1)^j with j the one-based position, so slots[1] carries +1. */
        if ((j & 1u) == 0u) {
            status = phy_cas_neg(dirac->cas, parts[used], &parts[used]);
            if (status != PHY_OK) {
                return status;
            }
        }
        used++;
    }
    return phy_cas_add(dirac->cas, parts, used, out_scalar);
}

static phy_status trace_node(const phy_dirac_expr *expr, uint32_t line,
                             phy_dirac_expr **out_expr)
{
    phy_dirac *dirac = expr->dirac;
    phy_dirac_expr *canonical = NULL;
    /*
     * Contract before the metric recursion.  This is both the specified
     * FORM ordering and the resource bound: a contracted pair is removed once
     * here instead of being reproduced in every recursion leaf.
     */
    phy_status status = contract_node(expr, &canonical);
    if (status != PHY_OK) {
        return status;
    }
    phy_dirac_expr *result = NULL;
    status = expr_create(dirac, canonical->count, &result);
    if (status != PHY_OK) {
        phy_dirac_expr_destroy(canonical);
        return status;
    }

    for (size_t i = 0u; i < canonical->count && status == PHY_OK; i++) {
        const dirac_term *term = &canonical->terms[i];
        phy_ir_ref slots[PHY_DIRAC_MAX_FACTORS];
        uint32_t slot_count = 0u;
        dirac_term reduced;
        term_init(&reduced, term->coefficient);

        for (uint32_t f = 0u; f < term->count; f++) {
            if (term->factors[f].line == line) {
                slots[slot_count++] = term->factors[f].argument;
            } else {
                reduced.factors[reduced.count++] = term->factors[f];
            }
        }
        if (slot_count > dirac->limits.max_gammas_per_line) {
            status = PHY_ERR_TERM_LIMIT;
            break;
        }
        if ((slot_count & 1u) != 0u) {
            /* FORM trace rule 0: an odd-length string is traceless. */
            continue;
        }
        phy_ir_ref scalar;
        status = trace_string(dirac, slots, slot_count, &scalar);
        if (status == PHY_OK) {
            status = scale_coefficient(dirac, &reduced, scalar);
        }
        if (status == PHY_OK) {
            status = expr_append(result, &reduced);
        }
    }

    phy_dirac_expr_destroy(canonical);
    if (status != PHY_OK) {
        phy_dirac_expr_destroy(result);
        return status;
    }
    status = canonical_node(result, out_expr);
    phy_dirac_expr_destroy(result);
    return status;
}

phy_status phy_dirac_trace(const phy_dirac_expr *expr, uint32_t line,
                           phy_dirac_expr **out_expr)
{
    if (expr == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(expr->dirac);
    return trace_node(expr, line, out_expr);
}

phy_status phy_dirac_trace_scalar(const phy_dirac_expr *expr, uint32_t line,
                                  phy_ir_ref *out_scalar)
{
    if (expr == NULL || out_scalar == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    dirac_begin(expr->dirac);
    phy_dirac_expr *traced = NULL;
    phy_status status = trace_node(expr, line, &traced);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref terms[PHY_DIRAC_MAX_FACTORS];
    size_t count = 0u;
    for (size_t i = 0u; i < traced->count; i++) {
        if (traced->terms[i].count != 0u) {
            phy_dirac_expr_destroy(traced);
            return PHY_ERR_TYPE;
        }
        if (count == PHY_DIRAC_MAX_FACTORS) {
            phy_dirac_expr_destroy(traced);
            return PHY_ERR_TERM_LIMIT;
        }
        terms[count++] = traced->terms[i].coefficient;
    }
    if (count == 0u) {
        status = phy_cas_number(expr->dirac->cas, 0, 1, out_scalar);
    } else {
        status = phy_cas_add(expr->dirac->cas, terms, count, out_scalar);
    }
    phy_dirac_expr_destroy(traced);
    return status;
}

/* ------------------------------------------------------------- comparison */

phy_status phy_dirac_equivalent(const phy_dirac_expr *left,
                                const phy_dirac_expr *right,
                                phy_cas_decision *out_decision)
{
    if (left == NULL || right == NULL || out_decision == NULL ||
        left->dirac != right->dirac) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_decision = PHY_CAS_UNKNOWN;
    phy_dirac *dirac = left->dirac;
    dirac_begin(dirac);

    phy_ir_ref minus_one;
    phy_status status = phy_cas_number(dirac->cas, -1, 1, &minus_one);
    if (status != PHY_OK) {
        return status;
    }
    phy_dirac_expr *negated = NULL;
    status = expr_create(dirac, right->count, &negated);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 0u; i < right->count && status == PHY_OK; i++) {
        dirac_term term = right->terms[i];
        const phy_ir_ref pair[2] = {term.coefficient, minus_one};
        status = phy_cas_mul(dirac->cas, pair, 2u, &term.coefficient);
        if (status == PHY_OK) {
            status = expr_append(negated, &term);
        }
    }
    phy_dirac_expr *gap = NULL;
    if (status == PHY_OK) {
        phy_dirac_expr *joined = NULL;
        status = expr_create(dirac, left->count + negated->count, &joined);
        for (size_t i = 0u; i < left->count && status == PHY_OK; i++) {
            status = expr_append(joined, &left->terms[i]);
        }
        for (size_t i = 0u; i < negated->count && status == PHY_OK; i++) {
            status = expr_append(joined, &negated->terms[i]);
        }
        if (status == PHY_OK) {
            status = clifford_node(joined, &gap);
        }
        phy_dirac_expr_destroy(joined);
    }
    phy_dirac_expr_destroy(negated);
    if (status != PHY_OK) {
        return status;
    }

    /*
     * Ordered products of the generators are linearly independent in the
     * Clifford algebra, so a surviving term with a proved-nonzero coefficient
     * is a proof of difference, not merely a different-looking form.
     */
    phy_cas_decision decision = PHY_CAS_ZERO;
    for (size_t i = 0u; i < gap->count; i++) {
        phy_cas_decision term_decision;
        status = phy_cas_is_zero(dirac->cas, gap->terms[i].coefficient,
                                 &term_decision);
        if (status != PHY_OK) {
            phy_dirac_expr_destroy(gap);
            return status;
        }
        if (term_decision == PHY_CAS_NONZERO) {
            decision = PHY_CAS_NONZERO;
            break;
        }
        if (term_decision == PHY_CAS_UNKNOWN) {
            decision = PHY_CAS_UNKNOWN;
        }
    }
    phy_dirac_expr_destroy(gap);
    *out_decision = decision;
    return PHY_OK;
}

/* ---------------------------------------------------------- serialization */

phy_status phy_dirac_to_ir(const phy_dirac_expr *expr, phy_ir_ref *out_expr)
{
    if (expr == NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_dirac *dirac = expr->dirac;
    dirac_begin(dirac);
    phy_ir_context *ir = phy_cas_ir(dirac->cas);

    if (expr->count == 0u) {
        return phy_cas_number(dirac->cas, 0, 1, out_expr);
    }
    phy_ir_ref *terms = phy_alloc(expr->count * sizeof *terms);
    if (terms == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    phy_status status = PHY_OK;
    for (size_t i = 0u; i < expr->count && status == PHY_OK; i++) {
        const dirac_term *term = &expr->terms[i];
        if (term->count == 0u) {
            terms[i] = term->coefficient;
            continue;
        }
        phy_ir_ref factors[PHY_DIRAC_MAX_FACTORS];
        for (uint32_t f = 0u; f < term->count; f++) {
            const phy_ir_ref line =
                phy_ir_integer(ir, (int64_t)term->factors[f].line);
            if (line == PHY_IR_NULL) {
                status = ir_failure(ir);
                break;
            }
            const phy_ir_ref args[2] = {line, term->factors[f].argument};
            factors[f] = phy_ir_operator(ir, dirac->head_gamma, args, 2u);
            if (factors[f] == PHY_IR_NULL) {
                status = ir_failure(ir);
                break;
            }
        }
        if (status != PHY_OK) {
            break;
        }
        const phy_ir_ref product = phy_ir_ncmul(ir, factors, term->count);
        if (product == PHY_IR_NULL) {
            status = ir_failure(ir);
            break;
        }
        const phy_ir_ref pair[2] = {term->coefficient, product};
        status = phy_cas_mul(dirac->cas, pair, 2u, &terms[i]);
    }
    if (status == PHY_OK) {
        status = phy_cas_add(dirac->cas, terms, expr->count, out_expr);
    }
    phy_free(terms, expr->count * sizeof *terms);
    return status;
}
