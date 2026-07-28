/*
 * Phy-nspire — scalar normal form.
 *
 * One walk, bottom up: simplify the operands, then rebuild the node under its
 * kind's rules. Because the IR interns, "rebuild" is where canonicalization
 * happens for free -- a collected sum handed to phy_ir_add() comes back as the
 * same ref every time the same sum is built, whatever route reached it.
 *
 * The two collection routines below are the substance of the file. Both work by
 * splitting each operand into a (key, weight) pair, sorting on the key, and
 * merging equal keys:
 *
 *   a sum's terms split into (non-numeric part, numeric coefficient), so
 *   2*x + 3*x merges to 5*x;
 *
 *   a product's factors split into (base, exponent), so x * x^2 merges to x^3.
 *
 * Sorting to bring equal keys together, rather than scanning pairwise, is what
 * keeps a sum of a thousand terms from costing a million comparisons.
 *
 * Identities are applied on the generic-domain convention: they hold wherever
 * the expression is defined, so x - x is 0 and x^0 is 1 without a nonzero
 * declaration. What that convention never does is manufacture a value where
 * there is none -- 1/0, 0^0 and 0^(-1) are PHY_ERR_DOMAIN.
 */
#include <string.h>

#include "cas_internal.h"

/* --------------------------------------------------------------- utilities */

static phy_status add_exact_nodes(phy_cas *cas, phy_ir_ref left,
                                  phy_ir_ref right, phy_ir_ref *out_ref)
{
    phy_cas_rat a;
    phy_cas_rat b;
    phy_cas_rat sum;
    if (phy_cas_exact_value(cas, left, &a) &&
        phy_cas_exact_value(cas, right, &b)) {
        if (!phy_cas_rat_add(a, b, &sum)) {
            return phy_cas_exact_add_refs(
                cas, left, right, out_ref);
        }
        return phy_cas_number_node(cas, sum, out_ref);
    }
    return phy_cas_exact_add_refs(cas, left, right, out_ref);
}

static phy_status multiply_exact_nodes(phy_cas *cas, phy_ir_ref left,
                                       phy_ir_ref right,
                                       phy_ir_ref *out_ref)
{
    phy_cas_rat a;
    phy_cas_rat b;
    phy_cas_rat product;
    if (phy_cas_exact_value(cas, left, &a) &&
        phy_cas_exact_value(cas, right, &b)) {
        if (!phy_cas_rat_mul(a, b, &product)) {
            return phy_cas_exact_mul_refs(
                cas, left, right, out_ref);
        }
        return phy_cas_number_node(cas, product, out_ref);
    }
    return phy_cas_exact_mul_refs(cas, left, right, out_ref);
}

/* True when `ref` is an exact number that is negative. */
static bool negative_lead(const phy_cas *cas, phy_ir_ref ref)
{
    if (phy_cas_is_exact(cas, ref)) {
        return phy_cas_exact_sign_ref(cas, ref) < 0;
    }
    /* A simplified product carries its coefficient first, so this is the whole
       of "syntactically negative" for a normalized expression. */
    if (phy_ir_kind_of(cas->ir, ref) == PHY_IR_MUL &&
        phy_cas_is_exact(cas, phy_ir_child(cas->ir, ref, 0u))) {
        return phy_cas_exact_sign_ref(
                   cas, phy_ir_child(cas->ir, ref, 0u)) < 0;
    }
    return false;
}

phy_status phy_cas_neg_node(phy_cas *cas, phy_ir_ref value, phy_ir_ref *out_ref)
{
    const phy_ir_ref factors[2] = {cas->minus_one, value};
    return phy_cas_mul_node(cas, factors, 2u, out_ref);
}

/*
 * Absorb operands of the kind being built into the operand list.
 *
 * A caller may legitimately pass a nested operand of the same kind -- negation
 * is mul(-1, value) whatever value is, and the rational form adds sums to sums
 * -- and collection must see through it. Treating a nested product as one
 * opaque factor is not merely untidy, it is wrong twice over: mul(-1, mul(-1, x))
 * would keep two numeric factors instead of cancelling to x, and
 * add(add(x, y), x) would leave x uncollected instead of reaching 2*x.
 *
 * One level suffices. Every operand has already been simplified, and a
 * normalized sum never contains a sum, so the flattening is inductive -- the
 * same argument make_nary() in src/ir/ir.c relies on.
 */
static phy_status flatten(phy_cas *cas, phy_ir_kind kind, size_t offset,
                          size_t count, size_t *out_offset, size_t *out_count)
{
    phy_ir_context *ir = cas->ir;

    size_t total = 0u;
    for (size_t i = 0u; i < count; i++) {
        const phy_ir_ref operand = phy_cas_scratch_at(cas, offset)[i];
        total += (phy_ir_kind_of(ir, operand) == kind)
                     ? phy_ir_child_count(ir, operand)
                     : 1u;
    }
    if (total == count) {
        *out_offset = offset;
        *out_count = count;
        return PHY_OK;
    }

    size_t flat;
    const phy_status status = phy_cas_scratch_alloc(cas, total, &flat);
    if (status != PHY_OK) {
        return status;
    }
    size_t used = 0u;
    for (size_t i = 0u; i < count; i++) {
        const phy_ir_ref operand = phy_cas_scratch_at(cas, offset)[i];
        if (phy_ir_kind_of(ir, operand) == kind) {
            const size_t children = phy_ir_child_count(ir, operand);
            for (size_t j = 0u; j < children; j++) {
                phy_cas_scratch_at(cas, flat)[used++] =
                    phy_ir_child(ir, operand, j);
            }
        } else {
            phy_cas_scratch_at(cas, flat)[used++] = operand;
        }
    }
    *out_offset = flat;
    *out_count = used;
    return PHY_OK;
}

/*
 * Copy a pointer operand list into the arena so that the collection routines,
 * which allocate, cannot be left holding a moved buffer.
 */
static phy_status stage(phy_cas *cas, const phy_ir_ref *refs, size_t count,
                        size_t *out_offset)
{
    const phy_status status = phy_cas_scratch_alloc(cas, count, out_offset);
    if (status != PHY_OK) {
        return status;
    }
    if (count != 0u) {
        memcpy(phy_cas_scratch_at(cas, *out_offset), refs,
               count * sizeof(phy_ir_ref));
    }
    return PHY_OK;
}

/* ------------------------------------------------------------- sum collection */

/*
 * Split a term into its numeric coefficient and the rest. A term that is not a
 * product, or a product with no leading number, has coefficient 1 -- the pair
 * still merges correctly, it just does not change the term.
 */
phy_status phy_cas_split_coefficient(phy_cas *cas, phy_ir_ref term,
                                     phy_ir_ref *out_coeff,
                                     phy_ir_ref *out_rest)
{
    phy_ir_context *ir = cas->ir;
    *out_coeff = cas->one;
    *out_rest = term;

    if (phy_ir_kind_of(ir, term) != PHY_IR_MUL) {
        return PHY_OK;
    }
    const phy_ir_ref first = phy_ir_child(ir, term, 0u);
    if (!phy_cas_is_exact(cas, first)) {
        return PHY_OK;
    }

    const size_t count = phy_ir_child_count(ir, term);
    if (count == 2u) {
        *out_coeff = first;
        *out_rest = phy_ir_child(ir, term, 1u);
        return PHY_OK;
    }

    size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    phy_status status = phy_cas_scratch_alloc(cas, count - 1u, &offset);
    if (status != PHY_OK) {
        return status;
    }
    for (size_t i = 1u; i < count; i++) {
        phy_cas_scratch_at(cas, offset)[i - 1u] = phy_ir_child(ir, term, i);
    }
    /* Already sorted and flattened, so this rebuild cannot change anything but
       the operand count. */
    const phy_ir_ref rest =
        phy_ir_mul(ir, phy_cas_scratch_at(cas, offset), count - 1u);
    phy_cas_scratch_release(cas, mark);

    if (rest == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_coeff = first;
    *out_rest = rest;
    return PHY_OK;
}

/* True when `ref` is head(u)^2 for the given known function head. */
static bool trig_squared_argument(phy_cas *cas, phy_ir_ref ref,
                                  phy_ir_symbol head,
                                  phy_ir_ref *out_argument)
{
    phy_ir_context *ir = cas->ir;
    if (phy_ir_kind_of(ir, ref) != PHY_IR_POW) {
        return false;
    }
    if (!phy_cas_is_integer(cas, phy_ir_child(ir, ref, 1u), 2)) {
        return false;
    }
    const phy_ir_ref base = phy_ir_child(ir, ref, 0u);
    if (phy_cas_known_function(cas, base) != head) {
        return false;
    }
    *out_argument = phy_ir_child(ir, base, 0u);
    return true;
}

/* head(u)^2 as an interned node, byte-identical to what collection stores. */
static phy_ir_ref trig_squared(phy_cas *cas, phy_ir_symbol head,
                               phy_ir_ref argument)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_ref call = phy_ir_function(ir, head, &argument, 1u);
    const phy_ir_ref two = phy_ir_integer(ir, 2);
    if (call == PHY_IR_NULL || two == PHY_IR_NULL) {
        return PHY_IR_NULL;
    }
    return phy_ir_pow(ir, call, two);
}

/*
 * For a merged key holding a sin(u)^2 factor, the same key with cos(u)^2 in
 * its place, and what remains once the trig factor is dropped entirely.
 * `*out_base` is PHY_IR_NULL when the key was exactly the squared sine, in
 * which case the collapsed pair is a pure number. `occurrence` selects which
 * squared sine to swap when the key carries several -- sin(u)^2 * sin(v)^2
 * has two candidate partners and the caller tries each; no more candidates
 * leaves *out_partner NULL.
 */
static phy_status pythagorean_partner(phy_cas *cas, phy_ir_ref key,
                                      size_t occurrence,
                                      phy_ir_ref *out_partner,
                                      phy_ir_ref *out_base)
{
    phy_ir_context *ir = cas->ir;
    phy_ir_ref argument = PHY_IR_NULL;

    *out_partner = PHY_IR_NULL;
    *out_base = PHY_IR_NULL;
    if (trig_squared_argument(
            cas, key, cas->functions[PHY_CAS_FN_SIN], &argument)) {
        if (occurrence != 0u) {
            return PHY_OK;
        }
        *out_partner =
            trig_squared(cas, cas->functions[PHY_CAS_FN_COS], argument);
        return *out_partner == PHY_IR_NULL ? phy_cas_ir_failure(cas) : PHY_OK;
    }
    if (phy_ir_kind_of(ir, key) != PHY_IR_MUL) {
        return PHY_OK;
    }

    const size_t count = phy_ir_child_count(ir, key);
    size_t seen = 0u;
    for (size_t swap = 0u; swap < count; ++swap) {
        if (!trig_squared_argument(
                cas, phy_ir_child(ir, key, swap),
                cas->functions[PHY_CAS_FN_SIN], &argument)) {
            continue;
        }
        if (seen++ != occurrence) {
            continue;
        }
        const phy_ir_ref cosine =
            trig_squared(cas, cas->functions[PHY_CAS_FN_COS], argument);
        if (cosine == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }

        /* Front half: the key with cos^2 swapped in. Back half: the key
           with the trig factor dropped. Both built before the release. */
        const size_t mark = phy_cas_scratch_mark(cas);
        size_t offset;
        const phy_status status =
            phy_cas_scratch_alloc(cas, 2u * count, &offset);
        if (status != PHY_OK) {
            return status;
        }
        size_t rest = 0u;
        for (size_t i = 0u; i < count; ++i) {
            const phy_ir_ref child = phy_ir_child(ir, key, i);
            phy_cas_scratch_at(cas, offset)[i] = i == swap ? cosine : child;
            if (i != swap) {
                phy_cas_scratch_at(cas, offset)[count + rest] = child;
                rest++;
            }
        }
        const phy_ir_ref partner =
            phy_ir_mul(ir, phy_cas_scratch_at(cas, offset), count);
        const phy_ir_ref base =
            rest == 1u
                ? phy_cas_scratch_at(cas, offset)[count]
                : phy_ir_mul(ir, phy_cas_scratch_at(cas, offset) + count,
                             rest);
        phy_cas_scratch_release(cas, mark);
        if (partner == PHY_IR_NULL || base == PHY_IR_NULL) {
            return phy_cas_ir_failure(cas);
        }
        *out_partner = partner;
        *out_base = base;
        return PHY_OK;
    }
    return PHY_OK;
}

/*
 * The one trig identity the display normal form applies:
 *
 *     c*sin(u)^2*K + c*cos(u)^2*K  ->  c*K
 *
 * for a shared factor K -- possibly none -- and one exact coefficient c.
 * Restricted to an exactly matching pair because that move strictly removes
 * a term: it cannot ping-pong, and a sum with no such pair is left exactly
 * as written, so 1 - sin(u)^2 still reads back as entered. Anything subtler
 * belongs to the decision pipeline in normal.c, which owns the full basis.
 *
 * Runs on the merged (key, coefficient) pairs. A collapsed entry has its key
 * set to PHY_IR_NULL and the build loop skips it; a pair whose key was
 * exactly the squared sine collapses into `total`. The loop repeats because
 * a collapse can expose the next pair -- sin^2(v)*sin^2(u) + sin^2(v)*cos^2(u)
 * leaves sin^2(v), which may now meet a waiting cos^2(v) -- and it
 * terminates because every collapse removes one term.
 */
static phy_status collapse_pythagorean(phy_cas *cas, size_t pairs,
                                       size_t count, phy_ir_ref *total)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0u; i < count; i++) {
            const phy_ir_ref key = phy_cas_scratch_at(cas, pairs)[2u * i];
            if (key == PHY_IR_NULL) {
                continue;
            }
            for (size_t occurrence = 0u;; occurrence++) {
                phy_ir_ref partner = PHY_IR_NULL;
                phy_ir_ref base = PHY_IR_NULL;
                phy_status status = pythagorean_partner(
                    cas, key, occurrence, &partner, &base);
                if (status != PHY_OK) {
                    return status;
                }
                if (partner == PHY_IR_NULL) {
                    break; /* no more squared sines in this key */
                }

                bool applied = false;
                for (size_t j = 0u; j < count; j++) {
                    phy_ir_ref *slot = phy_cas_scratch_at(cas, pairs);
                    if (j == i || slot[2u * j] != partner) {
                        continue;
                    }
                    const phy_ir_ref mine = slot[2u * i + 1u];
                    const phy_ir_ref theirs = slot[2u * j + 1u];
                    if (!phy_cas_is_exact(cas, mine) ||
                        !phy_cas_is_exact(cas, theirs) ||
                        mine != theirs) {
                        break;
                    }
                    slot[2u * j] = PHY_IR_NULL;
                    if (base == PHY_IR_NULL) {
                        phy_ir_ref sum = PHY_IR_NULL;
                        status = add_exact_nodes(
                            cas, *total, mine, &sum);
                        if (status != PHY_OK) {
                            return status;
                        }
                        *total = sum;
                        slot[2u * i] = PHY_IR_NULL;
                    } else {
                        /* The freed key may already be in the sum; fold the
                           coefficient there rather than duplicating the
                           term. */
                        size_t existing = count;
                        for (size_t k = 0u; k < count; k++) {
                            if (k != i && slot[2u * k] == base) {
                                existing = k;
                                break;
                            }
                        }
                        if (existing < count) {
                            const phy_ir_ref other =
                                slot[2u * existing + 1u];
                            phy_ir_ref coefficient = PHY_IR_NULL;
                            status = add_exact_nodes(
                                cas, mine, other, &coefficient);
                            if (status != PHY_OK) {
                                return status;
                            }
                            slot = phy_cas_scratch_at(cas, pairs);
                            slot[2u * existing + 1u] = coefficient;
                            slot[2u * i] = PHY_IR_NULL;
                        } else {
                            slot[2u * i] = base;
                        }
                    }
                    changed = true;
                    applied = true;
                    break;
                }
                if (applied) {
                    break;
                }
            }
        }
    }
    return PHY_OK;
}

static phy_status collect_sum(phy_cas *cas, size_t terms, size_t count,
                              phy_ir_ref *out_ref)
{
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t pairs;
    phy_status status = phy_cas_scratch_alloc(cas, 2u * count, &pairs);
    if (status != PHY_OK) {
        return status;
    }

    /* Exact terms accumulate here rather than in the pair array: there is one
       numeric term in the result and it has no key to merge on. */
    phy_ir_ref total = cas->zero;
    size_t used = 0u;

    for (size_t i = 0u; i < count; i++) {
        const phy_ir_ref term = phy_cas_scratch_at(cas, terms)[i];
        if (phy_cas_is_exact(cas, term)) {
            phy_ir_ref sum = PHY_IR_NULL;
            status = add_exact_nodes(cas, total, term, &sum);
            if (status != PHY_OK) {
                goto done;
            }
            total = sum;
            continue;
        }

        phy_ir_ref coeff, rest;
        status = phy_cas_split_coefficient(cas, term, &coeff, &rest);
        if (status != PHY_OK) {
            goto done;
        }
        phy_ir_ref *slot = phy_cas_scratch_at(cas, pairs);
        slot[2u * used] = rest;
        slot[2u * used + 1u] = coeff;
        used++;
    }

    phy_cas_sort_pairs(cas, pairs, used);

    /* Merge equal keys. Interning makes "equal key" a ref comparison. */
    size_t merged = 0u;
    for (size_t i = 0u; i < used; i++) {
        phy_ir_ref *slot = phy_cas_scratch_at(cas, pairs);
        if (merged > 0u && slot[2u * (merged - 1u)] == slot[2u * i]) {
            const phy_ir_ref left =
                slot[2u * (merged - 1u) + 1u];
            const phy_ir_ref right = slot[2u * i + 1u];
            phy_ir_ref coeff = PHY_IR_NULL;
            status = add_exact_nodes(cas, left, right, &coeff);
            if (status != PHY_OK) {
                goto done;
            }
            phy_cas_scratch_at(cas, pairs)[2u * (merged - 1u) + 1u] = coeff;
            continue;
        }
        slot[2u * merged] = slot[2u * i];
        slot[2u * merged + 1u] = slot[2u * i + 1u];
        merged++;
    }

    status = collapse_pythagorean(cas, pairs, merged, &total);
    if (status != PHY_OK) {
        goto done;
    }

    size_t built;
    status = phy_cas_scratch_alloc(cas, merged + 1u, &built);
    if (status != PHY_OK) {
        goto done;
    }

    size_t terms_out = 0u;
    for (size_t i = 0u; i < merged; i++) {
        const phy_ir_ref rest = phy_cas_scratch_at(cas, pairs)[2u * i];
        const phy_ir_ref coeff = phy_cas_scratch_at(cas, pairs)[2u * i + 1u];

        if (rest == PHY_IR_NULL) {
            continue; /* collapsed by the Pythagorean pair above */
        }
        if (phy_cas_is_integer(cas, coeff, 0)) {
            continue; /* the terms cancelled */
        }
        phy_ir_ref term = rest;
        if (!phy_cas_is_integer(cas, coeff, 1)) {
            /* phy_ir_mul flattens, so a product `rest` absorbs the coefficient
               rather than nesting under it. */
            const phy_ir_ref factors[2] = {coeff, rest};
            term = phy_ir_mul(cas->ir, factors, 2u);
            if (term == PHY_IR_NULL) {
                status = phy_cas_ir_failure(cas);
                goto done;
            }
        }
        phy_cas_scratch_at(cas, built)[terms_out++] = term;
    }

    if (total != cas->zero) {
        phy_cas_scratch_at(cas, built)[terms_out++] = total;
    }

    if (terms_out == 0u) {
        *out_ref = cas->zero;
    } else if (terms_out == 1u) {
        *out_ref = phy_cas_scratch_at(cas, built)[0];
    } else {
        const phy_ir_ref sum =
            phy_ir_add(cas->ir, phy_cas_scratch_at(cas, built), terms_out);
        if (sum == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
            goto done;
        }
        *out_ref = sum;
    }

done:
    phy_cas_scratch_release(cas, mark);
    return status;
}

/* --------------------------------------------------- product collection */

/* Sum two exponents, exactly where both are numbers and symbolically otherwise.
   x^a * x^b is x^(a+b) on one principal branch, so this needs no assumption. */
static phy_status add_exponents(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                                phy_ir_ref *out_ref)
{
    if (phy_cas_is_exact(cas, left) &&
        phy_cas_is_exact(cas, right)) {
        return add_exact_nodes(cas, left, right, out_ref);
    }
    const phy_ir_ref terms[2] = {left, right};
    return phy_cas_add_node(cas, terms, 2u, out_ref);
}

/*
 * Distribute -1 over a simplified sum, structurally.
 *
 * The IR has no negation node, so `2*M - r` and `r - 2*M` are two unrelated
 * sums and nothing below relates them. This builds the second from the first
 * so that the collector can.
 *
 * Non-recursive at this level -- it negates the sum's terms, which are not
 * themselves sums, because a simplified ADD is flat -- and independent of
 * where it is called from, so the normal form it feeds stays canonical.
 */
static phy_status negate_sum(phy_cas *cas, phy_ir_ref sum, phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const size_t count = phy_ir_child_count(ir, sum);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t terms;
    phy_status status = phy_cas_scratch_alloc(cas, count, &terms);

    for (size_t i = 0u; i < count && status == PHY_OK; i++) {
        phy_ir_ref negated;
        status = phy_cas_neg_node(cas, phy_ir_child(ir, sum, i), &negated);
        if (status == PHY_OK) {
            phy_cas_scratch_at(cas, terms)[i] = negated;
        }
    }
    if (status == PHY_OK) {
        status = phy_cas_add_at(cas, terms, count, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

/*
 * Collect a base against the negation of another base: A^m * (-A)^n is
 * (-1)^n * A^(m+n) for integer n, on every branch and without an assumption.
 *
 * `pairs` holds `count` (base, exponent) entries in canonical base order, with
 * equal bases already merged. A consumed entry has its base set to
 * PHY_IR_NULL; the surviving one is whichever sorts first, so the outcome does
 * not depend on the order the factors arrived in.
 *
 * This is not cosmetic. A coordinate metric whose g_tt and g_rr are negatives
 * of each other -- Schwarzschild and Reissner-Nordström both are -- puts an
 * uncancelled A * (-A)^-1 into its determinant, and without this rule every
 * inverse-metric component, every Christoffel symbol and every curvature
 * component inherits it. The exact int64 arithmetic then overflows deciding
 * whether a component is zero, on expressions that would have collapsed to a
 * single sign here. Restricted to integer exponents because (-A)^(1/2) is not
 * (-1)^(1/2) * A^(1/2).
 */
static phy_status merge_negated_bases(phy_cas *cas, size_t pairs, size_t count,
                                      phy_ir_ref *coefficient)
{
    phy_ir_context *ir = cas->ir;

    for (size_t i = 0u; i + 1u < count; i++) {
        const phy_ir_ref base = phy_cas_scratch_at(cas, pairs)[2u * i];
        if (base == PHY_IR_NULL || phy_ir_kind_of(ir, base) != PHY_IR_ADD) {
            continue;
        }
        phy_ir_ref negated;
        phy_status status = negate_sum(cas, base, &negated);
        if (status != PHY_OK) {
            return status;
        }
        for (size_t j = i + 1u; j < count; j++) {
            phy_ir_ref *slot = phy_cas_scratch_at(cas, pairs);
            if (slot[2u * j] != negated) {
                continue;
            }
            phy_cas_rat exponent;
            if (!phy_cas_exact_value(cas, slot[2u * j + 1u], &exponent) ||
                exponent.den != 1) {
                break;
            }
            phy_cas_rat sign;
            const phy_cas_rat minus_one = {-1, 1};
            if (!phy_cas_rat_pow(minus_one, exponent.num, &sign)) {
                return PHY_ERR_OVERFLOW;
            }
            phy_ir_ref sign_ref = PHY_IR_NULL;
            status = phy_cas_number_node(cas, sign, &sign_ref);
            if (status != PHY_OK) {
                return status;
            }
            phy_ir_ref product = PHY_IR_NULL;
            status = multiply_exact_nodes(
                cas, *coefficient, sign_ref, &product);
            if (status != PHY_OK) {
                return status;
            }
            *coefficient = product;
            phy_ir_ref combined;
            status = add_exponents(cas, slot[2u * i + 1u],
                                   slot[2u * j + 1u], &combined);
            if (status != PHY_OK) {
                return status;
            }
            slot = phy_cas_scratch_at(cas, pairs);
            slot[2u * i + 1u] = combined;
            slot[2u * j] = PHY_IR_NULL;
            break;
        }
    }
    return PHY_OK;
}

/*
 * One collection pass over a factor list.
 *
 * `out_dirty` reports that a rebuilt factor was itself a product, which happens
 * when a merged exponent turns (u*v)^1 * (u*v)^1 into (u*v)^2 and the power rule
 * distributes it. The children of that product may collide with factors already
 * in the list, so the caller runs the pass again. One repeat always suffices --
 * distribution leaves non-product bases behind, so a second pass cannot produce
 * another product -- and the cap is there to make that argument unnecessary to
 * trust.
 */
static phy_status collect_product_pass(phy_cas *cas, size_t factors, size_t count,
                                       size_t *out_offset, size_t *out_count,
                                       bool *out_dirty, phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    phy_status status = PHY_OK;
    size_t pairs;

    *out_dirty = false;
    *out_ref = PHY_IR_NULL;

    status = phy_cas_scratch_alloc(cas, 2u * count, &pairs);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref coefficient = cas->one;
    size_t used = 0u;

    for (size_t i = 0u; i < count; i++) {
        const phy_ir_ref factor = phy_cas_scratch_at(cas, factors)[i];
        if (phy_cas_is_exact(cas, factor)) {
            phy_ir_ref product = PHY_IR_NULL;
            status = multiply_exact_nodes(
                cas, coefficient, factor, &product);
            if (status != PHY_OK) {
                return status;
            }
            coefficient = product;
            continue;
        }

        phy_ir_ref base = factor;
        phy_ir_ref exponent = cas->one;
        if (phy_ir_kind_of(ir, factor) == PHY_IR_POW) {
            base = phy_ir_child(ir, factor, 0u);
            exponent = phy_ir_child(ir, factor, 1u);
        }
        phy_ir_ref *slot = phy_cas_scratch_at(cas, pairs);
        slot[2u * used] = base;
        slot[2u * used + 1u] = exponent;
        used++;
    }

    phy_cas_sort_pairs(cas, pairs, used);

    size_t merged = 0u;
    for (size_t i = 0u; i < used; i++) {
        phy_ir_ref *slot = phy_cas_scratch_at(cas, pairs);
        if (merged > 0u && slot[2u * (merged - 1u)] == slot[2u * i]) {
            phy_ir_ref sum;
            status = add_exponents(cas, slot[2u * (merged - 1u) + 1u],
                                   slot[2u * i + 1u], &sum);
            if (status != PHY_OK) {
                return status;
            }
            phy_cas_scratch_at(cas, pairs)[2u * (merged - 1u) + 1u] = sum;
            continue;
        }
        slot[2u * merged] = slot[2u * i];
        slot[2u * merged + 1u] = slot[2u * i + 1u];
        merged++;
    }

    status = merge_negated_bases(cas, pairs, merged, &coefficient);
    if (status != PHY_OK) {
        return status;
    }

    size_t built;
    status = phy_cas_scratch_alloc(cas, merged + 1u, &built);
    if (status != PHY_OK) {
        return status;
    }

    size_t factors_out = 0u;
    for (size_t i = 0u; i < merged; i++) {
        const phy_ir_ref base = phy_cas_scratch_at(cas, pairs)[2u * i];
        const phy_ir_ref exponent = phy_cas_scratch_at(cas, pairs)[2u * i + 1u];
        if (base == PHY_IR_NULL) {
            continue; /* absorbed into its negation above */
        }

        phy_ir_ref power;
        status = phy_cas_pow_node(cas, base, exponent, &power);
        if (status != PHY_OK) {
            return status;
        }

        if (phy_cas_is_exact(cas, power)) {
            /* A folded power joins the coefficient; 2^a * 2^b becomes 8 rather
               than a numeric factor the next pass would have to collect. */
            phy_ir_ref product = PHY_IR_NULL;
            status = multiply_exact_nodes(
                cas, coefficient, power, &product);
            if (status != PHY_OK) {
                return status;
            }
            coefficient = product;
            continue;
        }
        if (phy_ir_kind_of(ir, power) == PHY_IR_MUL) {
            *out_dirty = true;
        }
        phy_cas_scratch_at(cas, built)[factors_out++] = power;
    }

    if (coefficient == cas->zero) {
        /* Zero annihilates. */
        *out_ref = cas->zero;
        return PHY_OK;
    }

    if (coefficient != cas->one) {
        phy_cas_scratch_at(cas, built)[factors_out++] = coefficient;
    }

    *out_offset = built;
    *out_count = factors_out;
    return PHY_OK;
}

static phy_status collect_product(phy_cas *cas, size_t factors, size_t count,
                                  phy_ir_ref *out_ref)
{
    const size_t mark = phy_cas_scratch_mark(cas);
    phy_status status = PHY_OK;
    size_t offset = factors;
    size_t remaining = count;

    for (unsigned pass = 0u; pass < 4u; pass++) {
        bool dirty = false;
        phy_ir_ref collapsed = PHY_IR_NULL;
        size_t next_offset = 0u;
        size_t next_count = 0u;

        /* A distributed power leaves a product in the list, whose factors have
           to be absorbed before they can be collected against the rest. */
        status = flatten(cas, PHY_IR_MUL, offset, remaining, &offset, &remaining);
        if (status != PHY_OK) {
            goto done;
        }
        status = collect_product_pass(cas, offset, remaining, &next_offset,
                                      &next_count, &dirty, &collapsed);
        if (status != PHY_OK) {
            goto done;
        }
        if (collapsed != PHY_IR_NULL) {
            *out_ref = collapsed;
            goto done;
        }
        offset = next_offset;
        remaining = next_count;
        if (!dirty) {
            break;
        }
    }

    if (remaining == 0u) {
        *out_ref = cas->one;
    } else if (remaining == 1u) {
        *out_ref = phy_cas_scratch_at(cas, offset)[0];
    } else {
        const phy_ir_ref product =
            phy_ir_mul(cas->ir, phy_cas_scratch_at(cas, offset), remaining);
        if (product == PHY_IR_NULL) {
            status = phy_cas_ir_failure(cas);
            goto done;
        }
        *out_ref = product;
    }

done:
    phy_cas_scratch_release(cas, mark);
    return status;
}

/* --------------------------------------------------------------- powers */

/*
 * Pull square factors from a positive integer without turning simplification
 * into an unbounded factorization job.  The ceiling is part of the embedded
 * contract: larger radicands remain exact powers and can be handled later by
 * the polynomial layer or an optional host backend.
 */
static bool square_decompose_bounded(int64_t value, int64_t *out_square,
                                     int64_t *out_square_free)
{
    enum { MAX_RADICAND_TO_FACTOR = 1000000 };
    if (value <= 0 || value > MAX_RADICAND_TO_FACTOR) {
        return false;
    }

    int64_t remaining = value;
    int64_t square = 1;
    int64_t square_free = 1;
    for (int64_t divisor = 2; divisor <= remaining / divisor; ++divisor) {
        unsigned multiplicity = 0u;
        while (remaining % divisor == 0) {
            remaining /= divisor;
            multiplicity++;
        }
        for (unsigned pair = 0u; pair < multiplicity / 2u; ++pair) {
            square *= divisor;
        }
        if ((multiplicity & 1u) != 0u) {
            square_free *= divisor;
        }
    }
    if (remaining > 1) {
        square_free *= remaining;
    }
    *out_square = square;
    *out_square_free = square_free;
    return true;
}

phy_status phy_cas_pow_node(phy_cas *cas, phy_ir_ref base, phy_ir_ref exponent,
                            phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;

    if (base == PHY_IR_NULL || exponent == PHY_IR_NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_ir_kind_of(ir, base) == PHY_IR_ERROR) {
        *out_ref = base;
        return PHY_OK;
    }
    if (phy_ir_kind_of(ir, exponent) == PHY_IR_ERROR) {
        *out_ref = exponent;
        return PHY_OK;
    }

    int64_t integer_exponent = 0;
    const bool integral =
        phy_ir_integer_value(ir, exponent, &integer_exponent);

    if (integral && integer_exponent == 0) {
        /* 0^0 has no value to choose; anything else to the zero is 1 wherever it
           is defined, which is the generic-domain convention this layer uses. */
        if (phy_cas_is_integer(cas, base, 0)) {
            return PHY_ERR_DOMAIN;
        }
        *out_ref = cas->one;
        return PHY_OK;
    }
    if (integral && integer_exponent == 1) {
        *out_ref = base;
        return PHY_OK;
    }
    if (phy_cas_is_integer(cas, base, 1)) {
        *out_ref = cas->one;
        return PHY_OK;
    }

    phy_cas_rat exact_base;
    const bool numeric_base = phy_cas_exact_value(cas, base, &exact_base);
    phy_cas_rat exact_exponent;
    const bool numeric_exponent =
        phy_cas_exact_value(cas, exponent, &exact_exponent);

    if (numeric_base && exact_base.num == 0) {
        if (numeric_exponent) {
            if (exact_exponent.num < 0) {
                return PHY_ERR_DOMAIN; /* a division by zero, spelled 0^(-n) */
            }
            *out_ref = cas->zero;
            return PHY_OK;
        }
        /* 0^x with x of unknown sign is not decidable here; leave it standing. */
    } else if (numeric_base && exact_base.num > 0 && numeric_exponent &&
               exact_exponent.num == 1 && exact_exponent.den == 2) {
        int64_t numerator_square = 1;
        int64_t numerator_free = 1;
        int64_t denominator_square = 1;
        int64_t denominator_free = 1;
        if (square_decompose_bounded(exact_base.num, &numerator_square,
                                     &numerator_free) &&
            square_decompose_bounded(exact_base.den, &denominator_square,
                                     &denominator_free)) {
            const phy_cas_rat coefficient = {
                numerator_square, denominator_square};
            const phy_cas_rat radicand = {numerator_free, denominator_free};
            if (phy_cas_rat_cmp_int(radicand, 1) == 0) {
                return phy_cas_number_node(cas, coefficient, out_ref);
            }
            if (phy_cas_rat_cmp_int(coefficient, 1) != 0) {
                phy_ir_ref coefficient_ref = PHY_IR_NULL;
                phy_ir_ref radicand_ref = PHY_IR_NULL;
                phy_ir_ref root = PHY_IR_NULL;
                phy_status status =
                    phy_cas_number_node(cas, coefficient, &coefficient_ref);
                if (status == PHY_OK) {
                    status =
                        phy_cas_number_node(cas, radicand, &radicand_ref);
                }
                if (status == PHY_OK) {
                    root = phy_ir_pow(ir, radicand_ref, exponent);
                    status = root == PHY_IR_NULL ? phy_cas_ir_failure(cas)
                                                 : PHY_OK;
                }
                if (status == PHY_OK) {
                    const phy_ir_ref factors[2] = {coefficient_ref, root};
                    status = phy_cas_mul_node(cas, factors, 2u, out_ref);
                }
                return status;
            }
        }
    } else if (integral) {
        if (numeric_base) {
            phy_cas_rat folded;
            if (phy_cas_rat_pow(exact_base, integer_exponent, &folded)) {
                return phy_cas_number_node(cas, folded, out_ref);
            }
            const phy_status promoted = phy_cas_exact_pow_ref(
                cas, base, integer_exponent, out_ref);
            if (promoted != PHY_ERR_UNSUPPORTED) {
                return promoted;
            }
        } else if (phy_cas_is_exact(cas, base)) {
            const phy_status promoted = phy_cas_exact_pow_ref(
                cas, base, integer_exponent, out_ref);
            if (promoted != PHY_ERR_UNSUPPORTED) {
                return promoted;
            }
        } else if (phy_ir_kind_of(ir, base) == PHY_IR_POW) {
            /* (u^a)^k = u^(a*k) for integer k, on the principal branch. The
               restriction matters: (u^2)^(1/2) is not u. */
            const phy_ir_ref factors[2] = {phy_ir_child(ir, base, 1u), exponent};
            phy_ir_ref product;
            const phy_status status =
                phy_cas_mul_node(cas, factors, 2u, &product);
            if (status != PHY_OK) {
                return status;
            }
            return phy_cas_pow_node(cas, phy_ir_child(ir, base, 0u), product,
                                    out_ref);
        } else if (phy_ir_kind_of(ir, base) == PHY_IR_MUL) {
            /* (u*v)^k = u^k * v^k, again only for integer k. */
            const size_t count = phy_ir_child_count(ir, base);
            const size_t mark = phy_cas_scratch_mark(cas);
            size_t offset;
            phy_status status = phy_cas_scratch_alloc(cas, count, &offset);
            if (status == PHY_OK) {
                for (size_t i = 0u; i < count && status == PHY_OK; i++) {
                    phy_ir_ref power;
                    status = phy_cas_pow_node(cas, phy_ir_child(ir, base, i),
                                              exponent, &power);
                    if (status == PHY_OK) {
                        phy_cas_scratch_at(cas, offset)[i] = power;
                    }
                }
                if (status == PHY_OK) {
                    status = phy_cas_mul_at(cas, offset, count, out_ref);
                }
            }
            phy_cas_scratch_release(cas, mark);
            return status;
        }
    }

    const phy_ir_ref power = phy_ir_pow(ir, base, exponent);
    if (power == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = power;
    return PHY_OK;
}

/* -------------------------------------------------------- known functions */

enum {
    SPECIAL_ZERO = 0,
    SPECIAL_ONE = 1,
    SPECIAL_HALF = 2,
    SPECIAL_SQRT2_HALF = 3,
    SPECIAL_SQRT3_HALF = 4,
    SPECIAL_SQRT3 = 5,
    SPECIAL_SQRT3_THIRD = 6,
    SPECIAL_UNSUPPORTED = 126,
    SPECIAL_DOMAIN = 127
};

static bool pi_twelfths(phy_cas *cas, phy_ir_ref argument, unsigned *out_step)
{
    phy_cas_rat multiple = {0, 1};
    if (argument == cas->constant_pi) {
        multiple.num = 1;
    } else {
        phy_ir_ref coefficient = PHY_IR_NULL;
        phy_ir_ref rest = PHY_IR_NULL;
        if (phy_cas_split_coefficient(cas, argument, &coefficient, &rest) !=
                PHY_OK ||
            rest != cas->constant_pi ||
            !phy_cas_exact_value(cas, coefficient, &multiple)) {
            return false;
        }
    }
    if (multiple.den <= 0 || 12 % multiple.den != 0) {
        return false;
    }
    const int64_t period = 2 * multiple.den;
    int64_t normalized = multiple.num % period;
    if (normalized < 0) {
        normalized += period;
    }
    *out_step =
        (unsigned)(normalized * (12 / multiple.den));
    return *out_step < 24u;
}

static phy_status special_value(phy_cas *cas, int code,
                                phy_ir_ref *out_ref)
{
    if (code == SPECIAL_UNSUPPORTED) {
        return PHY_ERR_UNSUPPORTED;
    }
    if (code == SPECIAL_DOMAIN) {
        return PHY_ERR_DOMAIN;
    }
    const bool negative = code < 0;
    if (negative) {
        code = -code;
    }
    if (code == SPECIAL_ZERO) {
        *out_ref = cas->zero;
        return PHY_OK;
    }

    phy_cas_rat coefficient = {1, 1};
    int64_t radicand = 0;
    switch (code) {
    case SPECIAL_ONE:
        break;
    case SPECIAL_HALF:
        coefficient.den = 2;
        break;
    case SPECIAL_SQRT2_HALF:
        coefficient.den = 2;
        radicand = 2;
        break;
    case SPECIAL_SQRT3_HALF:
        coefficient.den = 2;
        radicand = 3;
        break;
    case SPECIAL_SQRT3:
        radicand = 3;
        break;
    case SPECIAL_SQRT3_THIRD:
        coefficient.den = 3;
        radicand = 3;
        break;
    default:
        return PHY_ERR_UNSUPPORTED;
    }
    if (negative) {
        coefficient.num = -coefficient.num;
    }
    if (radicand == 0) {
        return phy_cas_number_node(cas, coefficient, out_ref);
    }

    phy_ir_ref base = PHY_IR_NULL;
    phy_ir_ref half = PHY_IR_NULL;
    phy_ir_ref root = PHY_IR_NULL;
    phy_ir_ref scale = PHY_IR_NULL;
    phy_status status =
        phy_cas_number_node(cas, (phy_cas_rat){radicand, 1}, &base);
    if (status == PHY_OK) {
        status = phy_cas_number_node(cas, (phy_cas_rat){1, 2}, &half);
    }
    if (status == PHY_OK) {
        status = phy_cas_pow_node(cas, base, half, &root);
    }
    if (status == PHY_OK) {
        status = phy_cas_number_node(cas, coefficient, &scale);
    }
    if (status == PHY_OK) {
        const phy_ir_ref factors[2] = {scale, root};
        status = phy_cas_mul_node(cas, factors, 2u, out_ref);
    }
    return status;
}

static phy_status exact_trig_value(phy_cas *cas, phy_cas_function function,
                                   phy_ir_ref argument, phy_ir_ref *out_ref,
                                   bool *out_matched)
{
    static const int8_t sine[24] = {
        SPECIAL_ZERO, SPECIAL_UNSUPPORTED, SPECIAL_HALF,
        SPECIAL_SQRT2_HALF, SPECIAL_SQRT3_HALF, SPECIAL_UNSUPPORTED,
        SPECIAL_ONE, SPECIAL_UNSUPPORTED, SPECIAL_SQRT3_HALF,
        SPECIAL_SQRT2_HALF, SPECIAL_HALF, SPECIAL_UNSUPPORTED,
        SPECIAL_ZERO, SPECIAL_UNSUPPORTED, -SPECIAL_HALF,
        -SPECIAL_SQRT2_HALF, -SPECIAL_SQRT3_HALF, SPECIAL_UNSUPPORTED,
        -SPECIAL_ONE, SPECIAL_UNSUPPORTED, -SPECIAL_SQRT3_HALF,
        -SPECIAL_SQRT2_HALF, -SPECIAL_HALF, SPECIAL_UNSUPPORTED,
    };
    static const int8_t cosine[24] = {
        SPECIAL_ONE, SPECIAL_UNSUPPORTED, SPECIAL_SQRT3_HALF,
        SPECIAL_SQRT2_HALF, SPECIAL_HALF, SPECIAL_UNSUPPORTED,
        SPECIAL_ZERO, SPECIAL_UNSUPPORTED, -SPECIAL_HALF,
        -SPECIAL_SQRT2_HALF, -SPECIAL_SQRT3_HALF, SPECIAL_UNSUPPORTED,
        -SPECIAL_ONE, SPECIAL_UNSUPPORTED, -SPECIAL_SQRT3_HALF,
        -SPECIAL_SQRT2_HALF, -SPECIAL_HALF, SPECIAL_UNSUPPORTED,
        SPECIAL_ZERO, SPECIAL_UNSUPPORTED, SPECIAL_HALF,
        SPECIAL_SQRT2_HALF, SPECIAL_SQRT3_HALF, SPECIAL_UNSUPPORTED,
    };
    static const int8_t tangent[24] = {
        SPECIAL_ZERO, SPECIAL_UNSUPPORTED, SPECIAL_SQRT3_THIRD,
        SPECIAL_ONE, SPECIAL_SQRT3, SPECIAL_UNSUPPORTED,
        SPECIAL_DOMAIN, SPECIAL_UNSUPPORTED, -SPECIAL_SQRT3,
        -SPECIAL_ONE, -SPECIAL_SQRT3_THIRD, SPECIAL_UNSUPPORTED,
        SPECIAL_ZERO, SPECIAL_UNSUPPORTED, SPECIAL_SQRT3_THIRD,
        SPECIAL_ONE, SPECIAL_SQRT3, SPECIAL_UNSUPPORTED,
        SPECIAL_DOMAIN, SPECIAL_UNSUPPORTED, -SPECIAL_SQRT3,
        -SPECIAL_ONE, -SPECIAL_SQRT3_THIRD, SPECIAL_UNSUPPORTED,
    };

    unsigned step = 0u;
    if (!pi_twelfths(cas, argument, &step)) {
        *out_matched = false;
        return PHY_OK;
    }
    int code = SPECIAL_UNSUPPORTED;
    if (function == PHY_CAS_FN_SIN) {
        code = sine[step];
    } else if (function == PHY_CAS_FN_COS) {
        code = cosine[step];
    } else if (function == PHY_CAS_FN_TAN) {
        code = tangent[step];
    }
    if (code == SPECIAL_UNSUPPORTED) {
        *out_matched = false;
        return PHY_OK;
    }
    *out_matched = true;
    return special_value(cas, code, out_ref);
}

static phy_status apply_function(phy_cas *cas, phy_ir_symbol head,
                                 phy_ir_ref argument, phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const bool zero_argument = phy_cas_is_integer(cas, argument, 0);

    const phy_cas_function function = phy_cas_function_id(cas, head);
    if (function == PHY_CAS_FN_SIN || function == PHY_CAS_FN_COS ||
        function == PHY_CAS_FN_TAN) {
        bool matched = false;
        phy_status status =
            exact_trig_value(cas, function, argument, out_ref, &matched);
        if (status != PHY_OK || matched) {
            return status;
        }
        if (zero_argument) {
            *out_ref =
                function == PHY_CAS_FN_COS ? cas->one : cas->zero;
            return PHY_OK;
        }
        /*
         * Parity: cos is even, sin and tan are odd. Folding the sign out of the
         * argument is what lets sin(-u) + sin(u) collect to zero later, and it
         * is cheap because a normalized product wears its sign on the front.
         *
         * It also puts the argument in the form the multiple-angle reduction in
         * normal.c looks for, so cos(-2*u) reaches the same polynomial as
         * cos(2*u) rather than becoming a second generator.
         */
        if (negative_lead(cas, argument)) {
            phy_ir_ref positive;
            status = phy_cas_neg_node(cas, argument, &positive);
            if (status != PHY_OK) {
                return status;
            }
            phy_ir_ref folded;
            status = apply_function(cas, head, positive, &folded);
            if (status != PHY_OK) {
                return status;
            }
            if (function == PHY_CAS_FN_COS) {
                *out_ref = folded; /* even */
                return PHY_OK;
            }
            return phy_cas_neg_node(cas, folded, out_ref); /* odd */
        }
    } else if (function == PHY_CAS_FN_SINH ||
               function == PHY_CAS_FN_COSH ||
               function == PHY_CAS_FN_TANH ||
               function == PHY_CAS_FN_ASIN ||
               function == PHY_CAS_FN_ATAN ||
               function == PHY_CAS_FN_ASINH ||
               function == PHY_CAS_FN_ATANH) {
        if (zero_argument) {
            *out_ref =
                function == PHY_CAS_FN_COSH ? cas->one : cas->zero;
            return PHY_OK;
        }
        if (negative_lead(cas, argument)) {
            phy_ir_ref positive = PHY_IR_NULL;
            phy_ir_ref folded = PHY_IR_NULL;
            phy_status status =
                phy_cas_neg_node(cas, argument, &positive);
            if (status == PHY_OK) {
                status = apply_function(cas, head, positive, &folded);
            }
            return status == PHY_OK
                       ? phy_cas_neg_node(cas, folded, out_ref)
                       : status;
        }
    } else if (function == PHY_CAS_FN_ACOS) {
        if (phy_cas_is_integer(cas, argument, 1)) {
            *out_ref = cas->zero;
            return PHY_OK;
        }
        if (zero_argument) {
            phy_ir_ref half = PHY_IR_NULL;
            phy_status status = phy_cas_number_node(
                cas, (phy_cas_rat){1, 2}, &half);
            if (status != PHY_OK) {
                return status;
            }
            const phy_ir_ref factors[2] = {half, cas->constant_pi};
            return phy_cas_mul_node(cas, factors, 2u, out_ref);
        }
    } else if (function == PHY_CAS_FN_ACOSH) {
        if (phy_cas_is_integer(cas, argument, 1)) {
            *out_ref = cas->zero;
            return PHY_OK;
        }
    } else if (function == PHY_CAS_FN_EXP) {
        if (zero_argument) {
            *out_ref = cas->one;
            return PHY_OK;
        }
        if (phy_cas_is_integer(cas, argument, 1)) {
            *out_ref = cas->constant_e;
            return PHY_OK;
        }
        /* exp(log(u)) is u wherever log(u) exists, which is the principal
           branch's defining property. The converse is not: log(exp(u)) differs
           from u by a multiple of 2*pi*i, so it is not rewritten. */
        if (phy_cas_function_of(cas, argument) == PHY_CAS_FN_LOG) {
            *out_ref = phy_ir_child(ir, argument, 0u);
            return PHY_OK;
        }
    } else if (function == PHY_CAS_FN_LOG) {
        if (phy_cas_is_integer(cas, argument, 1)) {
            *out_ref = cas->zero;
            return PHY_OK;
        }
        if (argument == cas->constant_e) {
            *out_ref = cas->one;
            return PHY_OK;
        }
    } else if (function == PHY_CAS_FN_GAMMA) {
        phy_cas_rat value;
        if (phy_cas_exact_value(cas, argument, &value)) {
            if (value.den == 1 && value.num <= 0) {
                return PHY_ERR_DOMAIN;
            }
            if (value.den == 1 && value.num <= 21) {
                int64_t factorial = 1;
                bool fits = true;
                for (int64_t factor = 2; factor < value.num; ++factor) {
                    if (factorial > INT64_MAX / factor) {
                        fits = false;
                        break;
                    }
                    factorial *= factor;
                }
                if (fits) {
                    return phy_cas_number_node(
                        cas, (phy_cas_rat){factorial, 1}, out_ref);
                }
            }
            if (value.num == 1 && value.den == 2) {
                phy_ir_ref half = PHY_IR_NULL;
                phy_status status = phy_cas_number_node(
                    cas, (phy_cas_rat){1, 2}, &half);
                return status == PHY_OK
                           ? phy_cas_pow_node(
                                 cas, cas->constant_pi, half, out_ref)
                           : status;
            }
        }
    } else if (function == PHY_CAS_FN_LOGGAMMA) {
        phy_cas_rat value;
        if (phy_cas_exact_value(cas, argument, &value) &&
            value.den == 1 && value.num <= 0) {
            return PHY_ERR_DOMAIN;
        }
        if (phy_cas_is_integer(cas, argument, 1) ||
            phy_cas_is_integer(cas, argument, 2)) {
            *out_ref = cas->zero;
            return PHY_OK;
        }
    } else if (function == PHY_CAS_FN_ERF ||
               function == PHY_CAS_FN_ERFC) {
        if (zero_argument) {
            *out_ref =
                function == PHY_CAS_FN_ERFC ? cas->one : cas->zero;
            return PHY_OK;
        }
        if (negative_lead(cas, argument)) {
            phy_ir_ref positive = PHY_IR_NULL;
            phy_ir_ref folded = PHY_IR_NULL;
            phy_status status =
                phy_cas_neg_node(cas, argument, &positive);
            if (status == PHY_OK && function == PHY_CAS_FN_ERF) {
                status = apply_function(cas, head, positive, &folded);
                if (status == PHY_OK) {
                    status = phy_cas_neg_node(cas, folded, out_ref);
                }
                return status;
            }
            if (status == PHY_OK && function == PHY_CAS_FN_ERFC) {
                status = apply_function(cas, head, positive, &folded);
                if (status == PHY_OK) {
                    phy_ir_ref two = PHY_IR_NULL;
                    phy_ir_ref negated = PHY_IR_NULL;
                    status = phy_cas_number_node(
                        cas, (phy_cas_rat){2, 1}, &two);
                    if (status == PHY_OK) {
                        status =
                            phy_cas_neg_node(cas, folded, &negated);
                    }
                    if (status == PHY_OK) {
                        const phy_ir_ref terms[2] = {two, negated};
                        status =
                            phy_cas_add_node(cas, terms, 2u, out_ref);
                    }
                }
                return status;
            }
        }
    }

    const phy_ir_ref call = phy_ir_function(ir, head, &argument, 1u);
    if (call == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = call;
    return PHY_OK;
}

/* ------------------------------------------------------------- rebuilding */

phy_status phy_cas_rebuild_at(phy_cas *cas, phy_ir_kind kind,
                              phy_ir_symbol head, size_t offset, size_t count,
                              phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;

    switch (kind) {
    case PHY_IR_ADD:
        return phy_cas_add_at(cas, offset, count, out_ref);
    case PHY_IR_MUL:
        return phy_cas_mul_at(cas, offset, count, out_ref);
    case PHY_IR_POW: {
        const phy_ir_ref *kids = phy_cas_scratch_at(cas, offset);
        const phy_ir_ref base = kids[0];
        const phy_ir_ref exponent = kids[1];
        return phy_cas_pow_node(cas, base, exponent, out_ref);
    }
    case PHY_IR_FUNCTION:
        /* Arity is part of recognition: a one-argument sin goes through the
           rules, and anything else keeps its head and is rebuilt below. */
        if (count == 1u && phy_cas_is_known_head(cas, head)) {
            return apply_function(cas, head, phy_cas_scratch_at(cas, offset)[0],
                                  out_ref);
        }
        break;
    default:
        break;
    }

    /*
     * Everything else is rebuilt structurally, operand order preserved.
     * PHY_IR_NCMUL, PHY_IR_WEDGE, PHY_IR_TENSOR, PHY_IR_OPERATOR and
     * PHY_IR_DERIVATIVE reach here: their operands have been simplified, and
     * nothing in this scalar layer may reorder them or read their indices.
     */
    const phy_ir_ref *kids = phy_cas_scratch_at(cas, offset);
    phy_ir_ref rebuilt = PHY_IR_NULL;
    switch (kind) {
    case PHY_IR_EQUATION:
        rebuilt = phy_ir_equation(ir, kids[0], kids[1]);
        break;
    case PHY_IR_FUNCTION:
        rebuilt = phy_ir_function(ir, head, kids, count);
        break;
    case PHY_IR_NCMUL:
        rebuilt = phy_ir_ncmul(ir, kids, count);
        break;
    case PHY_IR_WEDGE:
        rebuilt = phy_ir_wedge(ir, kids, count);
        break;
    case PHY_IR_TENSOR:
        rebuilt = phy_ir_tensor(ir, head, kids, count);
        break;
    case PHY_IR_OPERATOR:
        rebuilt = phy_ir_operator(ir, head, kids, count);
        break;
    case PHY_IR_DERIVATIVE:
        rebuilt = phy_ir_derivative(ir, kids[0], &kids[1], count - 1u);
        break;
    default:
        return PHY_ERR_UNSUPPORTED;
    }
    if (rebuilt == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = rebuilt;
    return PHY_OK;
}

/* --------------------------------------------------------------- the walk */

phy_status phy_cas_simplify_node(phy_cas *cas, phy_ir_ref expr,
                                 phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;
    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);

    if (kind == PHY_IR_KIND_INVALID) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_cas_cache_get(cas, PHY_CAS_MEMO_SIMPLIFY, expr, PHY_IR_NULL, out_ref,
                          NULL)) {
        return PHY_OK;
    }

    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }

    /* Atoms, including PHY_IR_ERROR and PHY_IR_REAL, are their own normal form.
       A real is carried rather than folded: inexact arithmetic would make the
       zero decision an estimate. */
    if ((phy_ir_kind_flags(kind) & PHY_IR_KIND_ATOM) != 0u) {
        *out_ref = expr;
        phy_cas_cache_put(cas, PHY_CAS_MEMO_SIMPLIFY, expr, PHY_IR_NULL, expr,
                          PHY_IR_NULL);
        return PHY_OK;
    }

    const size_t count = phy_ir_child_count(ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    status = phy_cas_scratch_alloc(cas, count, &offset);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref result = PHY_IR_NULL;
    for (size_t i = 0u; i < count; i++) {
        phy_ir_ref child;
        status = phy_cas_simplify_node(cas, phy_ir_child(ir, expr, i), &child);
        if (status != PHY_OK) {
            goto done;
        }
        /* A typed error value swallows the expression that contains it, so a
           failed cell stays an error instead of becoming a valid-looking
           expression that means something else. */
        if (phy_ir_kind_of(ir, child) == PHY_IR_ERROR) {
            result = child;
            goto publish;
        }
        phy_cas_scratch_at(cas, offset)[i] = child;
    }

    status = phy_cas_rebuild_at(cas, kind, phy_ir_head(ir, expr), offset, count,
                                &result);
    if (status != PHY_OK) {
        goto done;
    }

publish:
    phy_cas_cache_put(cas, PHY_CAS_MEMO_SIMPLIFY, expr, PHY_IR_NULL, result,
                      PHY_IR_NULL);
    *out_ref = result;

done:
    phy_cas_scratch_release(cas, mark);
    return status;
}

/* ------------------------------------------------------------ substitution */

static phy_status substitute_walk(phy_cas *cas, phy_ir_ref expr,
                                  const phy_cas_rule *rules, size_t count,
                                  phy_ir_ref *out_ref)
{
    phy_ir_context *ir = cas->ir;

    /*
     * Match against the original subtree, before descending. A rule therefore
     * never sees what another rule produced, which is what makes
     * {x -> y, y -> x} a swap instead of a loop, and it is why the epoch below
     * is enough to key the cache: the rule set cannot change mid-walk.
     */
    for (size_t i = 0u; i < count; i++) {
        if (rules[i].from == expr) {
            *out_ref = rules[i].to;
            return PHY_OK;
        }
    }

    const phy_ir_ref epoch = (phy_ir_ref)cas->subst_epoch;
    if (phy_cas_cache_get(cas, PHY_CAS_MEMO_SUBST, expr, epoch, out_ref, NULL)) {
        return PHY_OK;
    }

    phy_status status = phy_cas_step(cas);
    if (status != PHY_OK) {
        return status;
    }

    const phy_ir_kind kind = phy_ir_kind_of(ir, expr);
    if ((phy_ir_kind_flags(kind) & PHY_IR_KIND_ATOM) != 0u) {
        /* An atom that matched no rule is unchanged; it still needs no
           simplification. */
        *out_ref = expr;
        phy_cas_cache_put(cas, PHY_CAS_MEMO_SUBST, expr, epoch, expr,
                          PHY_IR_NULL);
        return PHY_OK;
    }

    const size_t children = phy_ir_child_count(ir, expr);
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    status = phy_cas_scratch_alloc(cas, children, &offset);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref result = PHY_IR_NULL;
    for (size_t i = 0u; i < children; i++) {
        phy_ir_ref child;
        status = substitute_walk(cas, phy_ir_child(ir, expr, i), rules, count,
                                 &child);
        if (status != PHY_OK) {
            goto done;
        }
        phy_cas_scratch_at(cas, offset)[i] = child;
    }

    /* Rebuilding through the kind's rules simplifies as it climbs, so a
       substitution that creates 2*x + 3*x does not leave it uncollected. */
    status = phy_cas_rebuild_at(cas, kind, phy_ir_head(ir, expr), offset,
                                children, &result);
    if (status != PHY_OK) {
        goto done;
    }
    phy_cas_cache_put(cas, PHY_CAS_MEMO_SUBST, expr, epoch, result,
                      PHY_IR_NULL);
    *out_ref = result;

done:
    phy_cas_scratch_release(cas, mark);
    return status;
}

/* ------------------------------------------------------- operand entry points */

phy_status phy_cas_add_at(phy_cas *cas, size_t offset, size_t count,
                          phy_ir_ref *out_ref)
{
    if (count == 0u) {
        *out_ref = cas->zero;
        return PHY_OK;
    }
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t terms, total;
    phy_status status =
        flatten(cas, PHY_IR_ADD, offset, count, &terms, &total);
    if (status == PHY_OK) {
        status = collect_sum(cas, terms, total, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

phy_status phy_cas_mul_at(phy_cas *cas, size_t offset, size_t count,
                          phy_ir_ref *out_ref)
{
    if (count == 0u) {
        *out_ref = cas->one;
        return PHY_OK;
    }
    const size_t mark = phy_cas_scratch_mark(cas);
    size_t factors, total;
    phy_status status =
        flatten(cas, PHY_IR_MUL, offset, count, &factors, &total);
    if (status == PHY_OK) {
        status = collect_product(cas, factors, total, out_ref);
    }
    phy_cas_scratch_release(cas, mark);
    return status;
}

/*
 * The operand list of a sum or product, staged and normalized.
 *
 * Operands are simplified rather than assumed simplified, so that a caller may
 * mix raw IR construction with these builders and still get a normal form. It
 * is nearly free where the operands already came from this layer: a memo hit
 * costs no step and no allocation.
 */
static phy_status staged_operation(phy_cas *cas, const phy_ir_ref *refs,
                                   size_t count, bool sum, phy_ir_ref *out_ref)
{
    if (refs == NULL && count != 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0u; i < count; i++) {
        if (phy_ir_kind_of(cas->ir, refs[i]) == PHY_IR_KIND_INVALID) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
    }

    const size_t mark = phy_cas_scratch_mark(cas);
    size_t offset;
    phy_status status = stage(cas, refs, count, &offset);
    if (status != PHY_OK) {
        goto done;
    }
    for (size_t i = 0u; i < count; i++) {
        phy_ir_ref operand;
        status = phy_cas_simplify_node(
            cas, phy_cas_scratch_at(cas, offset)[i], &operand);
        if (status != PHY_OK) {
            goto done;
        }
        if (phy_ir_kind_of(cas->ir, operand) == PHY_IR_ERROR) {
            *out_ref = operand; /* an error value swallows the operation */
            goto done;
        }
        phy_cas_scratch_at(cas, offset)[i] = operand;
    }
    status = sum ? phy_cas_add_at(cas, offset, count, out_ref)
                 : phy_cas_mul_at(cas, offset, count, out_ref);

done:
    phy_cas_scratch_release(cas, mark);
    return status;
}

phy_status phy_cas_add_node(phy_cas *cas, const phy_ir_ref *terms, size_t count,
                            phy_ir_ref *out_ref)
{
    return staged_operation(cas, terms, count, true, out_ref);
}

phy_status phy_cas_mul_node(phy_cas *cas, const phy_ir_ref *factors,
                            size_t count, phy_ir_ref *out_ref)
{
    return staged_operation(cas, factors, count, false, out_ref);
}

/* ------------------------------------------------------------ public surface */

/* Every public entry point resets the step budget, checks its arguments, and
   clears the caller's out parameter before it can be read as a result. */
static phy_status enter(phy_cas *cas, phy_ir_ref *out_ref)
{
    if (cas == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;
    phy_cas_begin(cas);
    return PHY_OK;
}

phy_status phy_cas_number(phy_cas *cas, int64_t numerator, int64_t denominator,
                          phy_ir_ref *out_ref)
{
    phy_status status = enter(cas, out_ref);
    if (status != PHY_OK) {
        return status;
    }
    if (denominator == 0) {
        return PHY_ERR_DOMAIN;
    }
    /* Straight to the IR, which owns rational normalization: reduce, sign on the
       numerator, and a unit denominator becomes an integer. Doing it here as
       well would be a second normalizer to keep in agreement with the first. */
    const phy_ir_ref number = phy_ir_rational(cas->ir, numerator, denominator);
    if (number == PHY_IR_NULL) {
        return phy_cas_ir_failure(cas);
    }
    *out_ref = number;
    return PHY_OK;
}

phy_status phy_cas_add(phy_cas *cas, const phy_ir_ref *terms, size_t count,
                       phy_ir_ref *out_ref)
{
    phy_status status = enter(cas, out_ref);
    return (status != PHY_OK) ? status
                              : phy_cas_add_node(cas, terms, count, out_ref);
}

phy_status phy_cas_mul(phy_cas *cas, const phy_ir_ref *factors, size_t count,
                       phy_ir_ref *out_ref)
{
    phy_status status = enter(cas, out_ref);
    return (status != PHY_OK) ? status
                              : phy_cas_mul_node(cas, factors, count, out_ref);
}

phy_status phy_cas_neg(phy_cas *cas, phy_ir_ref value, phy_ir_ref *out_ref)
{
    phy_status status = enter(cas, out_ref);
    return (status != PHY_OK) ? status : phy_cas_neg_node(cas, value, out_ref);
}

phy_status phy_cas_sub(phy_cas *cas, phy_ir_ref left, phy_ir_ref right,
                       phy_ir_ref *out_ref)
{
    phy_status status = enter(cas, out_ref);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref negated;
    status = phy_cas_neg_node(cas, right, &negated);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref terms[2] = {left, negated};
    return phy_cas_add_node(cas, terms, 2u, out_ref);
}

phy_status phy_cas_div(phy_cas *cas, phy_ir_ref numerator,
                       phy_ir_ref denominator, phy_ir_ref *out_ref)
{
    phy_status status = enter(cas, out_ref);
    if (status != PHY_OK) {
        return status;
    }

    phy_ir_ref reduced;
    status = phy_cas_simplify_node(cas, denominator, &reduced);
    if (status != PHY_OK) {
        return status;
    }
    /*
     * A syntactic zero is caught here because simplification has already
     * collected x - x to 0. A subtler denominator is the caller's to check with
     * phy_cas_is_zero() -- running the full decision on every division would
     * make a curvature pass pay for a normal form per component.
     */
    if (phy_cas_is_integer(cas, reduced, 0)) {
        return PHY_ERR_DOMAIN;
    }

    phy_ir_ref inverse;
    status = phy_cas_pow_node(cas, reduced, cas->minus_one, &inverse);
    if (status != PHY_OK) {
        return status;
    }
    const phy_ir_ref factors[2] = {numerator, inverse};
    return phy_cas_mul_node(cas, factors, 2u, out_ref);
}

phy_status phy_cas_pow(phy_cas *cas, phy_ir_ref base, phy_ir_ref exponent,
                       phy_ir_ref *out_ref)
{
    phy_status status = enter(cas, out_ref);
    if (status != PHY_OK) {
        return status;
    }
    phy_ir_ref reduced_base, reduced_exponent;
    status = phy_cas_simplify_node(cas, base, &reduced_base);
    if (status != PHY_OK) {
        return status;
    }
    status = phy_cas_simplify_node(cas, exponent, &reduced_exponent);
    if (status != PHY_OK) {
        return status;
    }
    return phy_cas_pow_node(cas, reduced_base, reduced_exponent, out_ref);
}

phy_status phy_cas_simplify(phy_cas *cas, phy_ir_ref expr, phy_ir_ref *out_ref)
{
    phy_status status = enter(cas, out_ref);
    return (status != PHY_OK) ? status
                              : phy_cas_simplify_node(cas, expr, out_ref);
}

phy_status phy_cas_substitute(phy_cas *cas, phy_ir_ref expr,
                              const phy_cas_rule *rules, size_t count,
                              phy_ir_ref *out_ref)
{
    phy_status status = enter(cas, out_ref);
    return status == PHY_OK
               ? phy_cas_substitute_node(cas, expr, rules, count, out_ref)
               : status;
}

phy_status phy_cas_substitute_node(phy_cas *cas, phy_ir_ref expr,
                                   const phy_cas_rule *rules, size_t count,
                                   phy_ir_ref *out_ref)
{
    phy_status status = PHY_OK;
    if (rules == NULL && count != 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0u; i < count; i++) {
        if (phy_ir_kind_of(cas->ir, rules[i].from) == PHY_IR_KIND_INVALID ||
            phy_ir_kind_of(cas->ir, rules[i].to) == PHY_IR_KIND_INVALID) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
    }

    phy_ir_ref reduced;
    status = phy_cas_simplify_node(cas, expr, &reduced);
    if (status != PHY_OK) {
        return status;
    }

    /* A fresh epoch, so cached answers from an earlier rule set are unreachable
       rather than wrong. */
    cas->subst_epoch++;
    if (cas->subst_epoch == 0u) {
        /*
         * After 2^32 calls an epoch would otherwise alias the first rule set.
         * This is unlikely on a calculator but cheap to make impossible.
         */
        phy_cas_cache_clear(cas);
        cas->subst_epoch = 1u;
    }
    return substitute_walk(cas, reduced, rules, count, out_ref);
}
