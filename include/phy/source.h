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
    PHY_SOURCE_EXPAND,
    PHY_SOURCE_TOGETHER,
    PHY_SOURCE_NUMERATOR,
    PHY_SOURCE_DENOMINATOR,
    PHY_SOURCE_DIFFERENTIATE,
    PHY_SOURCE_INTEGRATE
} phy_source_operation;

#define PHY_SOURCE_MAX_VARIABLES 8u

typedef struct {
    phy_source_operation operation;
    phy_ir_ref expression;
    phy_ir_ref variables[PHY_SOURCE_MAX_VARIABLES];
    size_t variable_count; /* only for DIFFERENTIATE / INTEGRATE */
} phy_source_command;

/*
 * Supported surface:
 *   identifiers, exact integers/decimals, explicit or implicit multiplication,
 *   + - * / ^, (), {}, ==, function calls with [] or (), known scalar
 *   functions, and registered top-level commands.
 */
phy_status phy_source_parse(phy_ir_context *ir, const char *source,
                            phy_source_command *out_command,
                            size_t *out_error_offset);

#ifdef __cplusplus
}
#endif

#endif /* PHY_SOURCE_H */
