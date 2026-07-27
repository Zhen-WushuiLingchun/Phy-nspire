/*
 * Lorentz indices, momenta, and scalar products over a signature-bearing
 * metric. Contract Q-1 of docs/agent-tasks/QFT_DIRAC.md.
 */
#include "phy/lorentz.h"

#include <string.h>

#include "phy/platform.h"

/* An index census walks a DAG, so a pathologically shared expression could be
 * visited more times than it has nodes. The ceiling is far above anything this
 * layer builds and turns that into a typed timeout instead of a hang. */
#define LORENTZ_CENSUS_BUDGET 1000000u

struct phy_lorentz_metric {
    phy_cas *cas;
    phy_ir_symbol name;
    phy_ir_symbol space;
    phy_ir_symbol head_momentum;
    phy_ir_symbol head_dot;
    unsigned dimension;
    phy_lorentz_signature signature;

    unsigned momentum_count;
    phy_ir_symbol momentum_name[PHY_LORENTZ_MAX_MOMENTA];
    phy_ir_ref momentum[PHY_LORENTZ_MAX_MOMENTA];
    phy_ir_ref mass[PHY_LORENTZ_MAX_MOMENTA];

    bool has_conservation;
    unsigned eliminated;
    phy_ir_ref replacement[PHY_LORENTZ_MAX_MOMENTA];
};

static phy_status ir_failure(phy_ir_context *ir)
{
    const phy_status status = phy_ir_last_error(ir);
    return status != PHY_OK ? status : PHY_ERR_OUT_OF_MEMORY;
}

static phy_status intern_head(phy_ir_context *ir, const char *name,
                              phy_ir_symbol *out_symbol)
{
    const phy_ir_symbol symbol = phy_ir_intern(ir, name);
    if (symbol == PHY_IR_NO_SYMBOL) {
        return ir_failure(ir);
    }
    *out_symbol = symbol;
    return PHY_OK;
}

phy_status phy_lorentz_metric_create(phy_cas *cas, const char *name,
                                     const char *space, unsigned dimension,
                                     phy_lorentz_signature signature,
                                     phy_lorentz_metric **out_metric)
{
    if (cas == NULL || name == NULL || name[0] == '\0' || space == NULL ||
        space[0] == '\0' || out_metric == NULL || dimension == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (signature != PHY_LORENTZ_MOSTLY_MINUS &&
        signature != PHY_LORENTZ_MOSTLY_PLUS) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_lorentz_metric *metric = phy_alloc(sizeof *metric);
    if (metric == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(metric, 0, sizeof *metric);
    metric->cas = cas;
    metric->dimension = dimension;
    metric->signature = signature;

    phy_ir_context *ir = phy_cas_ir(cas);
    phy_status status = intern_head(ir, name, &metric->name);
    if (status == PHY_OK) {
        status = intern_head(ir, space, &metric->space);
    }
    if (status == PHY_OK) {
        status = intern_head(ir, "Momentum", &metric->head_momentum);
    }
    if (status == PHY_OK) {
        status = intern_head(ir, "LorentzDot", &metric->head_dot);
    }
    if (status != PHY_OK) {
        phy_free(metric, sizeof *metric);
        return status;
    }
    *out_metric = metric;
    return PHY_OK;
}

void phy_lorentz_metric_destroy(phy_lorentz_metric *metric)
{
    if (metric != NULL) {
        phy_free(metric, sizeof *metric);
    }
}

phy_cas *phy_lorentz_metric_cas(const phy_lorentz_metric *metric)
{
    return metric != NULL ? metric->cas : NULL;
}

const char *phy_lorentz_metric_name(const phy_lorentz_metric *metric)
{
    return metric != NULL
               ? phy_ir_symbol_name(phy_cas_ir(metric->cas), metric->name)
               : NULL;
}

unsigned phy_lorentz_metric_dimension(const phy_lorentz_metric *metric)
{
    return metric != NULL ? metric->dimension : 0u;
}

phy_lorentz_signature phy_lorentz_metric_signature(
    const phy_lorentz_metric *metric)
{
    return metric != NULL ? metric->signature : PHY_LORENTZ_MOSTLY_MINUS;
}

phy_ir_symbol phy_lorentz_metric_space(const phy_lorentz_metric *metric)
{
    return metric != NULL ? metric->space : PHY_IR_NO_SYMBOL;
}

/* ---------------------------------------------------------------- indices */

phy_status phy_lorentz_index(const phy_lorentz_metric *metric, const char *name,
                             phy_ir_variance variance, phy_ir_ref *out_index)
{
    if (metric == NULL || name == NULL || name[0] == '\0' ||
        out_index == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_context *ir = phy_cas_ir(metric->cas);
    const phy_ir_symbol symbol = phy_ir_intern(ir, name);
    if (symbol == PHY_IR_NO_SYMBOL) {
        return ir_failure(ir);
    }
    const phy_ir_ref index =
        phy_ir_index_in_space(ir, symbol, variance, metric->space);
    if (index == PHY_IR_NULL) {
        return ir_failure(ir);
    }
    *out_index = index;
    return PHY_OK;
}

bool phy_lorentz_owns_index(const phy_lorentz_metric *metric, phy_ir_ref ref)
{
    if (metric == NULL || ref == PHY_IR_NULL) {
        return false;
    }
    const phy_ir_context *ir = phy_cas_ir(metric->cas);
    return phy_ir_kind_of(ir, ref) == PHY_IR_INDEX &&
           phy_ir_index_space(ir, ref) == metric->space;
}

phy_status phy_lorentz_metric_tensor(const phy_lorentz_metric *metric,
                                     phy_ir_ref a, phy_ir_ref b,
                                     phy_ir_ref *out_tensor)
{
    if (metric == NULL || out_tensor == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (!phy_lorentz_owns_index(metric, a) ||
        !phy_lorentz_owns_index(metric, b)) {
        return PHY_ERR_TYPE;
    }
    phy_ir_context *ir = phy_cas_ir(metric->cas);
    if (phy_ir_head(ir, a) == phy_ir_head(ir, b)) {
        phy_ir_variance va, vb;
        if (!phy_ir_index_variance(ir, a, &va) ||
            !phy_ir_index_variance(ir, b, &vb)) {
            return PHY_ERR_TYPE;
        }
        if (va == vb) {
            /* g^{mu mu} is not a tensor expression; it is a lost index. */
            return PHY_ERR_TYPE;
        }
        /* g^mu_mu is the dimension, exactly. */
        const phy_ir_ref trace = phy_ir_integer(ir, (int64_t)metric->dimension);
        if (trace == PHY_IR_NULL) {
            return ir_failure(ir);
        }
        *out_tensor = trace;
        return PHY_OK;
    }

    const bool ordered = phy_ir_compare(ir, a, b) <= 0;
    const phy_ir_ref slots[2] = {ordered ? a : b, ordered ? b : a};
    const phy_ir_ref tensor = phy_ir_tensor(ir, metric->name, slots, 2u);
    if (tensor == PHY_IR_NULL) {
        return ir_failure(ir);
    }
    *out_tensor = tensor;
    return PHY_OK;
}

typedef struct {
    const phy_lorentz_metric *metric;
    phy_lorentz_index_use *uses;
    size_t capacity;
    size_t count;
    uint32_t budget;
} census;

static phy_status census_record(census *state, phy_ir_ref index)
{
    const phy_ir_context *ir = phy_cas_ir(state->metric->cas);
    const phy_ir_symbol name = phy_ir_head(ir, index);
    phy_ir_variance variance;
    if (!phy_ir_index_variance(ir, index, &variance)) {
        return PHY_ERR_TYPE;
    }
    for (size_t i = 0u; i < state->count; i++) {
        if (state->uses[i].name == name) {
            if (variance == PHY_IR_INDEX_UPPER) {
                state->uses[i].upper++;
            } else {
                state->uses[i].lower++;
            }
            return PHY_OK;
        }
    }
    if (state->count == state->capacity) {
        return PHY_ERR_UNSUPPORTED;
    }
    state->uses[state->count].name = name;
    state->uses[state->count].upper =
        (variance == PHY_IR_INDEX_UPPER) ? 1u : 0u;
    state->uses[state->count].lower =
        (variance == PHY_IR_INDEX_UPPER) ? 0u : 1u;
    state->count++;
    return PHY_OK;
}

static phy_status census_walk(census *state, phy_ir_ref expr)
{
    if (expr == PHY_IR_NULL) {
        return PHY_OK;
    }
    if (state->budget == 0u) {
        return PHY_ERR_TIMEOUT;
    }
    state->budget--;

    const phy_ir_context *ir = phy_cas_ir(state->metric->cas);
    if (phy_lorentz_owns_index(state->metric, expr)) {
        return census_record(state, expr);
    }
    const size_t children = phy_ir_child_count(ir, expr);
    for (size_t i = 0u; i < children; i++) {
        const phy_status status =
            census_walk(state, phy_ir_child(ir, expr, i));
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

phy_status phy_lorentz_index_census(const phy_lorentz_metric *metric,
                                    phy_ir_ref expr,
                                    phy_lorentz_index_use *out_uses,
                                    size_t capacity, size_t *out_count)
{
    if (metric == NULL || expr == PHY_IR_NULL || out_count == NULL ||
        (out_uses == NULL && capacity != 0u)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    census state;
    state.metric = metric;
    state.uses = out_uses;
    state.capacity = capacity;
    state.count = 0u;
    state.budget = LORENTZ_CENSUS_BUDGET;

    const phy_status status = census_walk(&state, expr);
    *out_count = (status == PHY_OK) ? state.count : 0u;
    return status;
}

phy_status phy_lorentz_rename_index(const phy_lorentz_metric *metric,
                                    phy_ir_ref expr, const char *from,
                                    const char *to, phy_ir_ref *out_expr)
{
    if (metric == NULL || expr == PHY_IR_NULL || from == NULL || to == NULL ||
        from[0] == '\0' || to[0] == '\0' || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref from_upper, from_lower, to_upper, to_lower;
    phy_status status =
        phy_lorentz_index(metric, from, PHY_IR_INDEX_UPPER, &from_upper);
    if (status == PHY_OK) {
        status = phy_lorentz_index(metric, from, PHY_IR_INDEX_LOWER,
                                   &from_lower);
    }
    if (status == PHY_OK) {
        status = phy_lorentz_index(metric, to, PHY_IR_INDEX_UPPER, &to_upper);
    }
    if (status == PHY_OK) {
        status = phy_lorentz_index(metric, to, PHY_IR_INDEX_LOWER, &to_lower);
    }
    if (status != PHY_OK) {
        return status;
    }
    const phy_cas_rule rules[2] = {{from_upper, to_upper},
                                   {from_lower, to_lower}};
    return phy_cas_substitute(metric->cas, expr, rules, 2u, out_expr);
}

/* --------------------------------------------------------------- momenta */

static bool momentum_slot(const phy_lorentz_metric *metric, phy_ir_ref momentum,
                          unsigned *out_slot)
{
    for (unsigned i = 0u; i < metric->momentum_count; i++) {
        if (metric->momentum[i] == momentum) {
            *out_slot = i;
            return true;
        }
    }
    return false;
}

phy_status phy_lorentz_momentum(phy_lorentz_metric *metric, const char *name,
                                phy_ir_ref *out_momentum)
{
    if (metric == NULL || name == NULL || name[0] == '\0' ||
        out_momentum == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_context *ir = phy_cas_ir(metric->cas);
    const phy_ir_symbol symbol = phy_ir_intern(ir, name);
    if (symbol == PHY_IR_NO_SYMBOL) {
        return ir_failure(ir);
    }
    for (unsigned i = 0u; i < metric->momentum_count; i++) {
        if (metric->momentum_name[i] == symbol) {
            *out_momentum = metric->momentum[i];
            return PHY_OK;
        }
    }
    if (metric->momentum_count == PHY_LORENTZ_MAX_MOMENTA) {
        return PHY_ERR_UNSUPPORTED;
    }
    const phy_ir_ref name_ref = phy_ir_symbol_ref(ir, symbol);
    const phy_ir_ref space_ref = phy_ir_symbol_ref(ir, metric->space);
    if (name_ref == PHY_IR_NULL || space_ref == PHY_IR_NULL) {
        return ir_failure(ir);
    }
    const phy_ir_ref args[2] = {name_ref, space_ref};
    const phy_ir_ref atom =
        phy_ir_operator(ir, metric->head_momentum, args, 2u);
    if (atom == PHY_IR_NULL) {
        return ir_failure(ir);
    }
    metric->momentum_name[metric->momentum_count] = symbol;
    metric->momentum[metric->momentum_count] = atom;
    metric->mass[metric->momentum_count] = PHY_IR_NULL;
    metric->momentum_count++;
    *out_momentum = atom;
    return PHY_OK;
}

bool phy_lorentz_owns_momentum(const phy_lorentz_metric *metric, phy_ir_ref ref)
{
    unsigned slot;
    return metric != NULL && ref != PHY_IR_NULL &&
           momentum_slot(metric, ref, &slot);
}

unsigned phy_lorentz_momentum_count(const phy_lorentz_metric *metric)
{
    return metric != NULL ? metric->momentum_count : 0u;
}

phy_ir_ref phy_lorentz_momentum_at(const phy_lorentz_metric *metric,
                                   unsigned slot)
{
    return (metric != NULL && slot < metric->momentum_count)
               ? metric->momentum[slot]
               : PHY_IR_NULL;
}

phy_status phy_lorentz_momentum_component(const phy_lorentz_metric *metric,
                                          phy_ir_ref momentum, phy_ir_ref index,
                                          phy_ir_ref *out_component)
{
    if (metric == NULL || out_component == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    unsigned slot;
    if (!phy_lorentz_owns_index(metric, index) ||
        !momentum_slot(metric, momentum, &slot)) {
        return PHY_ERR_TYPE;
    }
    phy_ir_context *ir = phy_cas_ir(metric->cas);
    const phy_ir_ref component =
        phy_ir_tensor(ir, metric->momentum_name[slot], &index, 1u);
    if (component == PHY_IR_NULL) {
        return ir_failure(ir);
    }
    *out_component = component;
    return PHY_OK;
}

phy_status phy_lorentz_dot(const phy_lorentz_metric *metric, phy_ir_ref p,
                           phy_ir_ref q, phy_ir_ref *out_dot)
{
    if (metric == NULL || out_dot == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    unsigned slot;
    if (!momentum_slot(metric, p, &slot) || !momentum_slot(metric, q, &slot)) {
        return PHY_ERR_TYPE;
    }
    phy_ir_context *ir = phy_cas_ir(metric->cas);
    const bool ordered = phy_ir_compare(ir, p, q) <= 0;
    const phy_ir_ref args[2] = {ordered ? p : q, ordered ? q : p};
    const phy_ir_ref dot = phy_ir_operator(ir, metric->head_dot, args, 2u);
    if (dot == PHY_IR_NULL) {
        return ir_failure(ir);
    }
    *out_dot = dot;
    return PHY_OK;
}

/* ---------------------------------------------------------------- vectors */

phy_status phy_lorentz_vector_zero(const phy_lorentz_metric *metric,
                                   phy_lorentz_vector *out_vector)
{
    if (metric == NULL || out_vector == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(out_vector, 0, sizeof *out_vector);
    out_vector->metric = metric;
    return PHY_OK;
}

phy_status phy_lorentz_vector_of(const phy_lorentz_metric *metric,
                                 phy_ir_ref momentum,
                                 phy_lorentz_vector *out_vector)
{
    if (metric == NULL || out_vector == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    unsigned slot;
    if (!momentum_slot(metric, momentum, &slot)) {
        return PHY_ERR_TYPE;
    }
    phy_ir_ref one;
    const phy_status status = phy_cas_number(metric->cas, 1, 1, &one);
    if (status != PHY_OK) {
        return status;
    }
    (void)phy_lorentz_vector_zero(metric, out_vector);
    out_vector->coefficient[slot] = one;
    return PHY_OK;
}

static phy_status vector_combine(const phy_lorentz_vector *left,
                                 const phy_lorentz_vector *right, bool subtract,
                                 phy_lorentz_vector *out_vector)
{
    if (left == NULL || right == NULL || out_vector == NULL ||
        left->metric == NULL || left->metric != right->metric) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_cas *cas = left->metric->cas;
    phy_lorentz_vector result;
    (void)phy_lorentz_vector_zero(left->metric, &result);

    for (unsigned i = 0u; i < PHY_LORENTZ_MAX_MOMENTA; i++) {
        const phy_ir_ref a = left->coefficient[i];
        const phy_ir_ref b = right->coefficient[i];
        if (b == PHY_IR_NULL) {
            result.coefficient[i] = a;
            continue;
        }
        phy_ir_ref term = b;
        phy_status status = PHY_OK;
        if (subtract) {
            status = phy_cas_neg(cas, b, &term);
        }
        if (status == PHY_OK && a != PHY_IR_NULL) {
            const phy_ir_ref pair[2] = {a, term};
            status = phy_cas_add(cas, pair, 2u, &term);
        }
        if (status != PHY_OK) {
            return status;
        }
        result.coefficient[i] = term;
    }
    *out_vector = result;
    return PHY_OK;
}

phy_status phy_lorentz_vector_add(const phy_lorentz_vector *left,
                                  const phy_lorentz_vector *right,
                                  phy_lorentz_vector *out_vector)
{
    return vector_combine(left, right, false, out_vector);
}

phy_status phy_lorentz_vector_sub(const phy_lorentz_vector *left,
                                  const phy_lorentz_vector *right,
                                  phy_lorentz_vector *out_vector)
{
    return vector_combine(left, right, true, out_vector);
}

phy_status phy_lorentz_vector_scale(const phy_lorentz_vector *vector,
                                    phy_ir_ref scalar,
                                    phy_lorentz_vector *out_vector)
{
    if (vector == NULL || out_vector == NULL || vector->metric == NULL ||
        scalar == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_lorentz_vector result;
    (void)phy_lorentz_vector_zero(vector->metric, &result);
    for (unsigned i = 0u; i < PHY_LORENTZ_MAX_MOMENTA; i++) {
        if (vector->coefficient[i] == PHY_IR_NULL) {
            continue;
        }
        const phy_ir_ref pair[2] = {vector->coefficient[i], scalar};
        const phy_status status =
            phy_cas_mul(vector->metric->cas, pair, 2u, &result.coefficient[i]);
        if (status != PHY_OK) {
            return status;
        }
    }
    *out_vector = result;
    return PHY_OK;
}

phy_status phy_lorentz_vector_dot(const phy_lorentz_vector *left,
                                  const phy_lorentz_vector *right,
                                  phy_ir_ref *out_scalar)
{
    if (left == NULL || right == NULL || out_scalar == NULL ||
        left->metric == NULL || left->metric != right->metric) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_lorentz_metric *metric = left->metric;
    phy_ir_ref terms[PHY_LORENTZ_MAX_MOMENTA * PHY_LORENTZ_MAX_MOMENTA];
    size_t count = 0u;

    for (unsigned i = 0u; i < metric->momentum_count; i++) {
        if (left->coefficient[i] == PHY_IR_NULL) {
            continue;
        }
        for (unsigned j = 0u; j < metric->momentum_count; j++) {
            if (right->coefficient[j] == PHY_IR_NULL) {
                continue;
            }
            phy_ir_ref dot;
            phy_status status = phy_lorentz_dot(metric, metric->momentum[i],
                                                metric->momentum[j], &dot);
            if (status != PHY_OK) {
                return status;
            }
            const phy_ir_ref factors[3] = {left->coefficient[i],
                                           right->coefficient[j], dot};
            status = phy_cas_mul(metric->cas, factors, 3u, &terms[count]);
            if (status != PHY_OK) {
                return status;
            }
            count++;
        }
    }
    if (count == 0u) {
        return phy_cas_number(metric->cas, 0, 1, out_scalar);
    }
    return phy_cas_add(metric->cas, terms, count, out_scalar);
}

/* ------------------------------------------------ kinematic declarations */

phy_status phy_lorentz_declare_conservation(phy_lorentz_metric *metric,
                                            const phy_lorentz_vector *relation,
                                            phy_ir_ref eliminated)
{
    if (metric == NULL || relation == NULL || relation->metric != metric) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    unsigned slot;
    if (!momentum_slot(metric, eliminated, &slot)) {
        return PHY_ERR_TYPE;
    }
    const phy_ir_ref pivot = relation->coefficient[slot];
    if (pivot == PHY_IR_NULL) {
        return PHY_ERR_DOMAIN;
    }
    phy_cas_decision decision;
    phy_status status = phy_cas_is_zero(metric->cas, pivot, &decision);
    if (status != PHY_OK) {
        return status;
    }
    if (decision != PHY_CAS_NONZERO) {
        /* Solving for a momentum whose coefficient is not proved nonzero is a
         * guess, not a substitution. */
        return PHY_ERR_DOMAIN;
    }

    phy_ir_ref replacement[PHY_LORENTZ_MAX_MOMENTA];
    for (unsigned i = 0u; i < PHY_LORENTZ_MAX_MOMENTA; i++) {
        replacement[i] = PHY_IR_NULL;
        if (i == slot || relation->coefficient[i] == PHY_IR_NULL) {
            continue;
        }
        phy_ir_ref negated;
        status = phy_cas_neg(metric->cas, relation->coefficient[i], &negated);
        if (status == PHY_OK) {
            status = phy_cas_div(metric->cas, negated, pivot, &replacement[i]);
        }
        if (status != PHY_OK) {
            return status;
        }
    }
    metric->has_conservation = true;
    metric->eliminated = slot;
    memcpy(metric->replacement, replacement, sizeof replacement);
    return PHY_OK;
}

bool phy_lorentz_has_conservation(const phy_lorentz_metric *metric)
{
    return metric != NULL && metric->has_conservation;
}

phy_status phy_lorentz_declare_on_shell(phy_lorentz_metric *metric,
                                        phy_ir_ref momentum, phy_ir_ref mass)
{
    if (metric == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    unsigned slot;
    if (!momentum_slot(metric, momentum, &slot)) {
        return PHY_ERR_TYPE;
    }
    metric->mass[slot] = mass;
    return PHY_OK;
}

static phy_status conservation_vector(const phy_lorentz_metric *metric,
                                      phy_lorentz_vector *out_vector)
{
    (void)phy_lorentz_vector_zero(metric, out_vector);
    memcpy(out_vector->coefficient, metric->replacement,
           sizeof out_vector->coefficient);
    return PHY_OK;
}

phy_status phy_lorentz_reduce_vector(const phy_lorentz_vector *vector,
                                     phy_lorentz_vector *out_vector)
{
    if (vector == NULL || out_vector == NULL || vector->metric == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_lorentz_metric *metric = vector->metric;
    if (!metric->has_conservation ||
        vector->coefficient[metric->eliminated] == PHY_IR_NULL) {
        *out_vector = *vector;
        return PHY_OK;
    }
    phy_lorentz_vector substitute;
    (void)conservation_vector(metric, &substitute);
    phy_status status = phy_lorentz_vector_scale(
        &substitute, vector->coefficient[metric->eliminated], &substitute);
    if (status != PHY_OK) {
        return status;
    }
    phy_lorentz_vector rest = *vector;
    rest.coefficient[metric->eliminated] = PHY_IR_NULL;
    return phy_lorentz_vector_add(&rest, &substitute, out_vector);
}

phy_status phy_lorentz_reduce(const phy_lorentz_metric *metric,
                              phy_ir_ref expr, phy_ir_ref *out_expr)
{
    if (metric == NULL || expr == PHY_IR_NULL || out_expr == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref current = expr;
    phy_status status = PHY_OK;

    if (metric->has_conservation) {
        phy_lorentz_vector substitute;
        (void)conservation_vector(metric, &substitute);

        phy_cas_rule conservation_rules[PHY_LORENTZ_MAX_MOMENTA];
        size_t conservation_count = 0u;
        for (unsigned j = 0u; j < metric->momentum_count && status == PHY_OK;
             j++) {
            phy_ir_ref from;
            status = phy_lorentz_dot(metric,
                                     metric->momentum[metric->eliminated],
                                     metric->momentum[j], &from);
            if (status != PHY_OK) {
                break;
            }
            phy_lorentz_vector other;
            if (j == metric->eliminated) {
                other = substitute;
            } else {
                status =
                    phy_lorentz_vector_of(metric, metric->momentum[j], &other);
                if (status != PHY_OK) {
                    break;
                }
            }
            phy_ir_ref to;
            status = phy_lorentz_vector_dot(&substitute, &other, &to);
            if (status != PHY_OK) {
                break;
            }
            conservation_rules[conservation_count].from = from;
            conservation_rules[conservation_count].to = to;
            conservation_count++;
        }
        if (status == PHY_OK && conservation_count != 0u) {
            status = phy_cas_substitute(metric->cas, current,
                                        conservation_rules, conservation_count,
                                        &current);
        }
        if (status != PHY_OK) {
            return status;
        }
    }

    phy_cas_rule on_shell_rules[PHY_LORENTZ_MAX_MOMENTA];
    size_t on_shell_count = 0u;
    for (unsigned i = 0u; i < metric->momentum_count; i++) {
        if (metric->mass[i] == PHY_IR_NULL) {
            continue;
        }
        phy_ir_ref from, two, mass_squared;
        status = phy_lorentz_dot(metric, metric->momentum[i],
                                 metric->momentum[i], &from);
        if (status == PHY_OK) {
            status = phy_cas_number(metric->cas, 2, 1, &two);
        }
        if (status == PHY_OK) {
            status =
                phy_cas_pow(metric->cas, metric->mass[i], two, &mass_squared);
        }
        if (status != PHY_OK) {
            return status;
        }
        on_shell_rules[on_shell_count].from = from;
        on_shell_rules[on_shell_count].to = mass_squared;
        on_shell_count++;
    }
    if (on_shell_count != 0u) {
        return phy_cas_substitute(metric->cas, current, on_shell_rules,
                                  on_shell_count, out_expr);
    }
    return phy_cas_simplify(metric->cas, current, out_expr);
}
