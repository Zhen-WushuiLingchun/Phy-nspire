/*
 * Phy-nspire — QFT objects in the shared abstract/component tensor model.
 *
 * The older Lorentz, Dirac and colour modules deliberately remain small,
 * exact domain engines.  This bridge gives the objects they manipulate one
 * common tensor identity:
 *
 *   Lorentz, Spinor, ColorAdjoint, ColorFundamental        IndexSpace
 *   eta, gamma, delta, f, d, T, A and F                    TensorHead
 *   Minkowski eta and the built-in SU(2)/SU(3) invariants  components
 *
 * It does not replace gamma-string or colour-trace algorithms.  It makes
 * their indices and invariant tensors composable with the general abstract
 * canonicalizer and component evaluator without identifying unlike spaces.
 */
#ifndef PHY_QFT_BRIDGE_H
#define PHY_QFT_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>

#include "phy/component_tensor.h"
#include "phy/color.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_qft_component_view phy_qft_component_view;

typedef enum {
    PHY_QFT_SPACE_LORENTZ = 0,
    PHY_QFT_SPACE_SPINOR,
    PHY_QFT_SPACE_COLOR_ADJOINT,
    PHY_QFT_SPACE_COLOR_FUNDAMENTAL,
    PHY_QFT_SPACE_COUNT
} phy_qft_space;

typedef enum {
    PHY_QFT_MINKOWSKI_METRIC = 0,
    PHY_QFT_MINKOWSKI_INVERSE,
    PHY_QFT_MOMENTUM,
    PHY_QFT_DIRAC_GAMMA,
    PHY_QFT_SUN_DELTA,
    PHY_QFT_SUN_F,
    PHY_QFT_SUN_D,
    PHY_QFT_SUN_T,
    PHY_QFT_GAUGE_POTENTIAL,
    PHY_QFT_FIELD_STRENGTH,
    PHY_QFT_QUANTITY_COUNT
} phy_qft_quantity;

typedef struct {
    size_t max_bytes; /* view metadata; default 8 KiB */
    phy_basis_limits basis;
    phy_component_limits component;
} phy_qft_bridge_limits;

void phy_qft_bridge_limits_defaults(phy_qft_bridge_limits *out_limits);

const char *phy_qft_space_name(phy_qft_space space);
const char *phy_qft_quantity_name(phy_qft_quantity quantity);

/*
 * Build one QFT tensor picture over exact SU(N).
 *
 * N may stay symbolic.  Lorentz and spinor bases, together with both
 * Minkowski metric realizations, are always available.  A concrete adjoint
 * and fundamental basis is added when N is an exact integer whose dimensions
 * fit the configured basis limit.  SUNDelta then has exact components, and
 * SUNF additionally has components for the built-in SU(2) and SU(3) bases.
 *
 * The fixed IndexSpace names intentionally match the existing exact engines:
 * "Lorentz", "Spinor", "ColorAdjoint", and "ColorFundamental".  Consequently
 * only one such declaration may be created in one abstract context.  A second
 * call returns PHY_ERR_ALREADY_INITIALIZED instead of manufacturing a second
 * Lorentz space with the same printed name.
 *
 * As with the GR bridge, spaces and heads are bulk-owned by `context`; bases
 * and component tensors are owned by the returned view.  Failure after an
 * abstract declaration can therefore leave bounded, unreferenced metadata
 * until the abstract context is reset.
 */
phy_status phy_qft_component_view_create(
    phy_cas *cas, phy_abstract_context *context, phy_ir_ref n,
    const phy_qft_bridge_limits *limits,
    phy_qft_component_view **out_view);
void phy_qft_component_view_destroy(phy_qft_component_view *view);

phy_ir_ref phy_qft_component_view_n(const phy_qft_component_view *view);
const phy_index_space *phy_qft_component_view_space(
    const phy_qft_component_view *view, phy_qft_space space);
const phy_abstract_tensor_head *phy_qft_component_view_head(
    const phy_qft_component_view *view, phy_qft_quantity quantity);

bool phy_qft_component_view_has_basis(
    const phy_qft_component_view *view, phy_qft_space space);
phy_component_basis *phy_qft_component_view_basis(
    const phy_qft_component_view *view, phy_qft_space space);

bool phy_qft_component_view_holds(
    const phy_qft_component_view *view, phy_qft_quantity quantity);
phy_component_tensor *phy_qft_component_view_tensor(
    const phy_qft_component_view *view, phy_qft_quantity quantity);

/* Add every available basis and realization to one explicit picture. */
phy_status phy_qft_component_view_bind(
    const phy_qft_component_view *view, phy_component_binding *binding);

#ifdef __cplusplus
}
#endif

#endif /* PHY_QFT_BRIDGE_H */
