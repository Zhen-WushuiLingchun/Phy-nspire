/*
 * Phy-nspire — differential-form storage.
 *
 * Canonical antisymmetric storage: a degree-p form on an n-dimensional chart
 * is its C(n,p) strictly increasing components, in lexicographic order. That
 * is the whole representation. Antisymmetry is not maintained by a fill
 * discipline as it is for phy_tensor, because there is nothing to maintain --
 * a repeated index names no slot, and an out-of-order tuple is reached through
 * the sort parity.
 *
 * The table is inline and its size is fixed at max_p C(4,p) = 6, so a form is
 * exactly one allocation and no loop in this layer or in exterior.c allocates
 * per component.
 *
 * Unlike src/tensor, this file does form expressions: folding a sort parity
 * into a component is a negation, and the scalar layer that makes that
 * possible has landed. Every scalar this file produces comes from phy_cas_*,
 * and every component it stores is in the CAS normal form.
 */
#include <string.h>

#include "geom_internal.h"

/* ------------------------------------------------------- combinatorics */

size_t phy_geom_binomial(unsigned n, unsigned k)
{
    if (k > n) {
        return 0u;
    }
    /*
     * The multiplicative form, which is exact at every step: after i factors
     * the running value is C(n, i+1), an integer, so the division never
     * truncates. At n <= 4 nothing here approaches an overflow.
     */
    size_t result = 1u;
    for (unsigned i = 0u; i < k; ++i) {
        result = result * (size_t)(n - i) / (size_t)(i + 1u);
    }
    return result;
}

void phy_geom_combination_at(unsigned n, unsigned k, size_t position,
                             unsigned *out_indices)
{
    for (unsigned i = 0u; i < k; ++i) {
        out_indices[i] = i;
    }
    for (size_t step = 0u; step < position; ++step) {
        /*
         * Lexicographic successor: advance the rightmost slot that is not yet
         * at its ceiling, then reset everything to its right to the tightest
         * increasing run. At most four slots, so the cost of walking to the
         * position rather than computing it is four comparisons a step.
         */
        unsigned slot = k;
        while (slot > 0u) {
            slot--;
            if (out_indices[slot] < n - k + slot) {
                out_indices[slot]++;
                for (unsigned rest = slot + 1u; rest < k; ++rest) {
                    out_indices[rest] = out_indices[rest - 1u] + 1u;
                }
                break;
            }
        }
    }
}

bool phy_geom_combination_index(unsigned n, unsigned k,
                                const unsigned *sorted_indices,
                                size_t *out_position)
{
    const size_t count = phy_geom_binomial(n, k);
    unsigned current[PHY_FORM_MAX_DEGREE];

    for (size_t position = 0u; position < count; ++position) {
        phy_geom_combination_at(n, k, position, current);
        bool same = true;
        for (unsigned slot = 0u; slot < k; ++slot) {
            if (current[slot] != sorted_indices[slot]) {
                same = false;
                break;
            }
        }
        if (same) {
            *out_position = position;
            return true;
        }
    }
    return false;
}

int phy_geom_sort_parity(unsigned *values, unsigned count)
{
    int sign = 1;

    for (unsigned i = 1u; i < count; ++i) {
        const unsigned key = values[i];
        unsigned slot = i;
        while (slot > 0u && values[slot - 1u] > key) {
            values[slot] = values[slot - 1u];
            slot--;
            sign = -sign;
        }
        values[slot] = key;
    }
    for (unsigned i = 1u; i < count; ++i) {
        if (values[i - 1u] == values[i]) {
            return 0;
        }
    }
    return sign;
}

/* ---------------------------------------------------------- validation */

phy_status phy_geom_check_cas(const phy_cas *cas, const phy_manifold *manifold)
{
    if (cas == NULL || manifold == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    /*
     * Detectable, unlike a stray phy_ir_ref: a CAS carries its context. A CAS
     * over another context would build the result out of nodes the manifold's
     * charts cannot name.
     */
    if (phy_cas_ir(cas) != manifold->ir) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return PHY_OK;
}

phy_status phy_geom_check_pair(const phy_form *left, const phy_form *right)
{
    if (left == NULL || right == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    /*
     * Same manifold and same chart. Nothing in this layer relates two charts,
     * so accepting a mismatch would be an unjustified identification rather
     * than a convenience.
     */
    if (left->manifold != right->manifold ||
        left->chart_index != right->chart_index) {
        return PHY_ERR_TYPE;
    }
    return PHY_OK;
}

/* ------------------------------------------------------------ lifecycle */

phy_status phy_form_create(phy_manifold *manifold, unsigned chart_index,
                           const char *name, unsigned degree,
                           phy_form **out_form)
{
    if (manifold == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (chart_index >= manifold->chart_count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (name != NULL && name[0] == '\0') {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    /*
     * Lambda^p is the zero vector space above the dimension, and this layer
     * has no object for it. A caller asking for one is confused about the
     * dimension, not about the degree ceiling, so this is an argument error
     * rather than PHY_ERR_UNSUPPORTED.
     */
    if (degree > manifold->dimension) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    phy_ir_symbol head = PHY_IR_NO_SYMBOL;
    if (name != NULL) {
        head = phy_ir_intern(manifold->ir, name);
        if (head == PHY_IR_NO_SYMBOL) {
            const phy_status status = phy_ir_last_error(manifold->ir);
            return status == PHY_OK ? PHY_ERR_OUT_OF_MEMORY : status;
        }
    }

    phy_form *form = phy_alloc(sizeof(*form));
    if (form == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(form, 0, sizeof(*form));

    form->manifold = manifold;
    form->chart = manifold->charts[chart_index];
    form->chart_index = chart_index;
    form->head = head;
    form->degree = degree;
    form->dimension = manifold->dimension;
    form->count = phy_geom_binomial(manifold->dimension, degree);
    form->zero = manifold->zero;

    for (size_t position = 0u; position < form->count; ++position) {
        form->components[position] = form->zero;
    }

    *out_form = form;
    return PHY_OK;
}

void phy_form_destroy(phy_form *form)
{
    if (form == NULL) {
        return;
    }
    phy_free(form, sizeof(*form));
}

phy_status phy_geom_form_like(const phy_form *like, unsigned degree,
                              phy_form **out_form)
{
    return phy_form_create(like->manifold, like->chart_index, NULL, degree,
                           out_form);
}

phy_status phy_form_copy(const phy_form *form, phy_form **out_form)
{
    if (form == NULL || out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    phy_form *copy = NULL;
    const phy_status status = phy_geom_form_like(form, form->degree, &copy);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t position = 0u; position < form->count; ++position) {
        copy->components[position] = form->components[position];
    }

    *out_form = copy;
    return PHY_OK;
}

/* ------------------------------------------------------------ accessors */

const phy_manifold *phy_form_manifold(const phy_form *form)
{
    return form == NULL ? NULL : form->manifold;
}

const phy_chart *phy_form_chart(const phy_form *form)
{
    return form == NULL ? NULL : form->chart;
}

unsigned phy_form_chart_index(const phy_form *form)
{
    return form == NULL ? 0u : form->chart_index;
}

phy_ir_symbol phy_form_head(const phy_form *form)
{
    return form == NULL ? PHY_IR_NO_SYMBOL : form->head;
}

const char *phy_form_name(const phy_form *form)
{
    if (form == NULL || form->head == PHY_IR_NO_SYMBOL) {
        return NULL;
    }
    return phy_ir_symbol_name(form->manifold->ir, form->head);
}

unsigned phy_form_degree(const phy_form *form)
{
    return form == NULL ? 0u : form->degree;
}

unsigned phy_form_dimension(const phy_form *form)
{
    return form == NULL ? 0u : form->dimension;
}

size_t phy_form_component_count(const phy_form *form)
{
    return form == NULL ? 0u : form->count;
}

phy_ir_ref phy_form_zero(const phy_form *form)
{
    return form == NULL ? PHY_IR_NULL : form->zero;
}

/* -------------------------------------------------- canonical components */

phy_status phy_form_position_indices(const phy_form *form, size_t position,
                                     unsigned *out_indices)
{
    if (form == NULL || position >= form->count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (form->degree > 0u && out_indices == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (form->degree > 0u) {
        phy_geom_combination_at(form->dimension, form->degree, position,
                                out_indices);
    }
    return PHY_OK;
}

phy_status phy_form_position_of(const phy_form *form, const unsigned *indices,
                                size_t *out_position, int *out_sign)
{
    if (form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (form->degree > 0u && indices == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    unsigned sorted[PHY_FORM_MAX_DEGREE];
    for (unsigned slot = 0u; slot < form->degree; ++slot) {
        if (indices[slot] >= form->dimension) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
        sorted[slot] = indices[slot];
    }

    const int sign = phy_geom_sort_parity(sorted, form->degree);
    if (out_sign != NULL) {
        *out_sign = sign;
    }
    if (sign == 0) {
        /* A repeated index is not a slot; it is the statement that the
         * component vanishes. *out_position stays untouched. */
        return PHY_OK;
    }

    size_t position = 0u;
    if (!phy_geom_combination_index(form->dimension, form->degree, sorted,
                                    &position)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (out_position != NULL) {
        *out_position = position;
    }
    return PHY_OK;
}

/* ----------------------------------------------------- component access */

phy_status phy_form_get_at(const phy_form *form, size_t position,
                           phy_ir_ref *out_ref)
{
    if (form == NULL || out_ref == NULL || position >= form->count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = form->components[position];
    return PHY_OK;
}

phy_status phy_form_set_at(phy_cas *cas, phy_form *form, size_t position,
                           phy_ir_ref value)
{
    if (form == NULL || value == PHY_IR_NULL || position >= form->count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_status status = phy_geom_check_cas(cas, form->manifold);
    if (status != PHY_OK) {
        return status;
    }

    /*
     * Simplified before it is stored, so that every component of every form is
     * in the normal form and phy_form_equivalent compares like with like. The
     * write happens only after the CAS has succeeded, which is what makes a
     * failed assignment leave the form exactly as it was.
     */
    phy_ir_ref simplified = PHY_IR_NULL;
    const phy_status simplify_status =
        phy_cas_simplify(cas, value, &simplified);
    if (simplify_status != PHY_OK) {
        return simplify_status;
    }

    form->components[position] = simplified;
    return PHY_OK;
}

phy_status phy_geom_apply_sign(phy_cas *cas, phy_ir_ref value, int sign,
                               phy_ir_ref *out_ref)
{
    if (sign >= 0) {
        *out_ref = value;
        return PHY_OK;
    }
    return phy_cas_neg(cas, value, out_ref);
}

phy_status phy_form_get(phy_cas *cas, const phy_form *form,
                        const unsigned *indices, phy_ir_ref *out_ref)
{
    if (form == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_cas(cas, form->manifold);
    if (status != PHY_OK) {
        return status;
    }

    size_t position = 0u;
    int sign = 0;
    status = phy_form_position_of(form, indices, &position, &sign);
    if (status != PHY_OK) {
        return status;
    }
    if (sign == 0) {
        *out_ref = form->zero;
        return PHY_OK;
    }
    return phy_geom_apply_sign(cas, form->components[position], sign, out_ref);
}

phy_status phy_form_set(phy_cas *cas, phy_form *form, const unsigned *indices,
                        phy_ir_ref value)
{
    if (form == NULL || value == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_cas(cas, form->manifold);
    if (status != PHY_OK) {
        return status;
    }

    size_t position = 0u;
    int sign = 0;
    status = phy_form_position_of(form, indices, &position, &sign);
    if (status != PHY_OK) {
        return status;
    }

    if (sign == 0) {
        /*
         * alpha_{aab} is zero by antisymmetry. An assignment that says
         * otherwise contradicts the storage, so it is an assumption failure
         * rather than a value error -- and a value that cannot be *decided*
         * zero is refused too, because accepting it would silently discard it.
         */
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        status = phy_cas_is_zero(cas, value, &decision);
        if (status != PHY_OK) {
            return status;
        }
        return decision == PHY_CAS_ZERO ? PHY_OK : PHY_ERR_ASSUMPTION;
    }

    phy_ir_ref signed_value = PHY_IR_NULL;
    status = phy_geom_apply_sign(cas, value, sign, &signed_value);
    if (status != PHY_OK) {
        return status;
    }
    return phy_form_set_at(cas, form, position, signed_value);
}

void phy_form_clear(phy_form *form)
{
    if (form == NULL) {
        return;
    }
    for (size_t position = 0u; position < form->count; ++position) {
        form->components[position] = form->zero;
    }
}

/* ------------------------------------------------------ linear structure */

phy_status phy_geom_sum(phy_cas *cas, const phy_form *form,
                        const phy_ir_ref *terms, size_t count,
                        phy_ir_ref *out_ref)
{
    if (count == 0u) {
        *out_ref = form->zero;
        return PHY_OK;
    }
    if (count == 1u) {
        *out_ref = terms[0];
        return PHY_OK;
    }
    return phy_cas_add(cas, terms, count, out_ref);
}

/*
 * The two-operand linear operations differ only in what they do to the right
 * component, so they share one body rather than three copies of the same
 * validation and the same allocation unwind.
 */
static phy_status combine(phy_cas *cas, const phy_form *left,
                          const phy_form *right, bool subtract,
                          phy_form **out_form)
{
    if (out_form == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_pair(left, right);
    if (status != PHY_OK) {
        return status;
    }
    if (left->degree != right->degree) {
        return PHY_ERR_TYPE;
    }
    status = phy_geom_check_cas(cas, left->manifold);
    if (status != PHY_OK) {
        return status;
    }

    phy_form *result = NULL;
    status = phy_geom_form_like(left, left->degree, &result);
    if (status != PHY_OK) {
        return status;
    }

    for (size_t position = 0u; position < left->count; ++position) {
        const phy_ir_ref terms[2] = {left->components[position],
                                     right->components[position]};
        phy_ir_ref value = PHY_IR_NULL;
        status = subtract ? phy_cas_sub(cas, terms[0], terms[1], &value)
                          : phy_cas_add(cas, terms, 2u, &value);
        if (status != PHY_OK) {
            phy_form_destroy(result);
            return status;
        }
        result->components[position] = value;
    }

    *out_form = result;
    return PHY_OK;
}

phy_status phy_form_add(phy_cas *cas, const phy_form *left,
                        const phy_form *right, phy_form **out_form)
{
    return combine(cas, left, right, false, out_form);
}

phy_status phy_form_sub(phy_cas *cas, const phy_form *left,
                        const phy_form *right, phy_form **out_form)
{
    return combine(cas, left, right, true, out_form);
}

phy_status phy_form_scale(phy_cas *cas, const phy_form *form,
                          phy_ir_ref scalar, phy_form **out_form)
{
    if (form == NULL || out_form == NULL || scalar == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_cas(cas, form->manifold);
    if (status != PHY_OK) {
        return status;
    }

    phy_form *result = NULL;
    status = phy_geom_form_like(form, form->degree, &result);
    if (status != PHY_OK) {
        return status;
    }

    for (size_t position = 0u; position < form->count; ++position) {
        const phy_ir_ref factors[2] = {scalar, form->components[position]};
        phy_ir_ref value = PHY_IR_NULL;
        status = phy_cas_mul(cas, factors, 2u, &value);
        if (status != PHY_OK) {
            phy_form_destroy(result);
            return status;
        }
        result->components[position] = value;
    }

    *out_form = result;
    return PHY_OK;
}

/* ---------------------------------------------------------- the decisions */

/*
 * Combine per-component decisions conservatively: one proved-nonzero component
 * settles the question, and anything undecided keeps the answer UNKNOWN. A
 * form is never reported as vanishing because a component could not be
 * decided.
 */
static phy_cas_decision merge(phy_cas_decision so_far, phy_cas_decision next)
{
    if (next == PHY_CAS_NONZERO) {
        return PHY_CAS_NONZERO;
    }
    if (next == PHY_CAS_UNKNOWN) {
        return so_far == PHY_CAS_NONZERO ? PHY_CAS_NONZERO : PHY_CAS_UNKNOWN;
    }
    return so_far;
}

phy_status phy_form_is_zero(phy_cas *cas, const phy_form *form,
                            phy_cas_decision *out_decision)
{
    if (form == NULL || out_decision == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_status check = phy_geom_check_cas(cas, form->manifold);
    if (check != PHY_OK) {
        return check;
    }

    phy_cas_decision result = PHY_CAS_ZERO;
    for (size_t position = 0u; position < form->count; ++position) {
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        const phy_status status =
            phy_cas_is_zero(cas, form->components[position], &decision);
        if (status != PHY_OK) {
            return status;
        }
        result = merge(result, decision);
        if (result == PHY_CAS_NONZERO) {
            break;
        }
    }

    *out_decision = result;
    return PHY_OK;
}

phy_status phy_form_equivalent(phy_cas *cas, const phy_form *left,
                               const phy_form *right,
                               phy_cas_decision *out_decision)
{
    if (out_decision == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = phy_geom_check_pair(left, right);
    if (status != PHY_OK) {
        return status;
    }
    if (left->degree != right->degree) {
        return PHY_ERR_TYPE;
    }
    status = phy_geom_check_cas(cas, left->manifold);
    if (status != PHY_OK) {
        return status;
    }

    phy_cas_decision result = PHY_CAS_ZERO;
    for (size_t position = 0u; position < left->count; ++position) {
        phy_cas_decision decision = PHY_CAS_UNKNOWN;
        status = phy_cas_equivalent(cas, left->components[position],
                                    right->components[position], &decision);
        if (status != PHY_OK) {
            return status;
        }
        result = merge(result, decision);
        if (result == PHY_CAS_NONZERO) {
            break;
        }
    }

    *out_decision = result;
    return PHY_OK;
}
