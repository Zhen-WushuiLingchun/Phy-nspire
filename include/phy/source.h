/*
 * Small reader-facing symbolic source parser.
 *
 * The durable IR format remains the S-expression grammar in phy/ir.h. This
 * parser is for editable notebook input and accepts a compact Mathematica-like
 * surface syntax without making strings the physics data model.
 */
#ifndef PHY_SOURCE_H
#define PHY_SOURCE_H

#include <stddef.h>

#include "phy/ir.h"
#include "phy/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PHY_SOURCE_SIMPLIFY = 0,
    /*
     * Simplify with the decision-grade machinery behind it: the trig basis
     * and rational normal form of the zero decision, kept only when the
     * result is smaller than what plain simplification already shows.
     */
    PHY_SOURCE_FULL_SIMPLIFY,
    PHY_SOURCE_EXPAND,
    PHY_SOURCE_TOGETHER,
    PHY_SOURCE_NUMERATOR,
    PHY_SOURCE_DENOMINATOR,
    PHY_SOURCE_DIFFERENTIATE,
    PHY_SOURCE_INTEGRATE,

    /*
     * The two operations that read and write the notebook environment rather
     * than only the expression. `name = rhs` and `Set[name, rhs]` bind;
     * `Clear[name]` and `ClearAll[]` unbind. See include/phy/eval.h.
     */
    PHY_SOURCE_ASSIGN,
    PHY_SOURCE_CLEAR
} phy_source_operation;

#define PHY_SOURCE_MAX_VARIABLES 8u

typedef struct {
    phy_source_operation operation;
    phy_ir_ref expression;
    phy_ir_ref variables[PHY_SOURCE_MAX_VARIABLES];
    size_t variable_count; /* only for DIFFERENTIATE / INTEGRATE */
    /*
     * The name an ASSIGN binds or a CLEAR unbinds. PHY_IR_NO_SYMBOL for every
     * other operation, and for `ClearAll[]`, which clears the whole
     * environment. `expression` is PHY_IR_NULL for CLEAR and only for CLEAR.
     */
    phy_ir_symbol target;
} phy_source_command;

/*
 * Supported surface:
 *   identifiers, exact integers/decimals, explicit or implicit multiplication,
 *   + - * / ^, (), {}, ==, `name = value` assignment, function calls with []
 *   or (), known scalar functions, reserved physics heads, and registered
 *   top-level commands.
 *
 * `=` is assignment and `==` is an equation; the parser distinguishes them by
 * lookahead, so `x = 1` and `x == 1` are different commands and neither is a
 * typo for the other.
 */
phy_status phy_source_parse(phy_ir_context *ir, const char *source,
                            phy_source_command *out_command,
                            size_t *out_error_offset);

#ifdef __cplusplus
}
#endif

#endif /* PHY_SOURCE_H */
