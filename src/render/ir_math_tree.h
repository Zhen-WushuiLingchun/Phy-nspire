#ifndef PHY_RENDER_IR_MATH_TREE_H
#define PHY_RENDER_IR_MATH_TREE_H

#include <string>

#include "nmarkdown/math/math_atoms.h"
#include "phy/ir.h"

/*
 * Internal C++ bridge from the backend-neutral CAS IR to nMarkdown's sole
 * display tree.  The public C ABI lives in phy/formula.h; no nMarkdown object
 * crosses that boundary.
 */
bool phy_build_ir_math_tree(const phy_ir_context *context,
                            phy_ir_ref expression,
                            nmarkdown::MathTree& tree,
                            std::string& diagnostic);

#endif
