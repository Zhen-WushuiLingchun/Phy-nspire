/*
 * The SymPy `sstr` subset the golden corpus is written in, parsed into the
 * typed IR through the CAS's simplifying constructors. Test-only.
 *
 * docs/references/GENERAL_RELATIVITY.md records that every expression in
 * research/corpus/gr_golden.json round-trips through sympy.sympify. This is the
 * matching reader on this side of the boundary, and it is deliberately the
 * smallest grammar that accepts the corpus:
 *
 *   expr    := term (('+' | '-') term)*
 *   term    := unary (('*' | '/') unary)*
 *   unary   := ('+' | '-')* power
 *   power   := atom ['**' unary]              right associative
 *   atom    := integer | name | name '(' expr ')' | '(' expr ')'
 *
 * Precedence follows SymPy exactly, which matters in two places that would
 * otherwise silently change a golden value: `-x**2` is `-(x**2)`, and
 * `x**-2` is `x**(-2)`.
 *
 * What it refuses, and why refusing beats accepting:
 *
 *   - any function head other than sin, cos, tan, exp and log. Those five are
 *     what the CAS differentiates and what its zero decision can see inside;
 *     an unrecognized head would become an opaque generator, and a comparison
 *     against it would answer UNKNOWN rather than failing honestly;
 *   - non-integer literals, since an exact corpus has none and a decimal would
 *     enter the IR as a PHY_IR_REAL atom, which the zero decision cannot
 *     decide;
 *   - trailing content, so a truncated or concatenated entry is an error
 *     rather than a silently shorter expression.
 */
#ifndef PHY_TEST_CORPUS_EXPR_H
#define PHY_TEST_CORPUS_EXPR_H

#include <stddef.h>

#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/phy.h"

/*
 * Parse `text` into `*out_ref`, in normal form. `out_error_offset` may be
 * NULL; otherwise it receives the byte offset the parse stopped at.
 *
 * PHY_ERR_PARSE for a grammar violation or a refused construct; the CAS's own
 * statuses pass through unchanged when construction fails.
 */
phy_status phy_corpus_expr_parse(phy_cas *cas, const char *text,
                                 phy_ir_ref *out_ref,
                                 size_t *out_error_offset);

#endif /* PHY_TEST_CORPUS_EXPR_H */
