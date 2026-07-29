/*
 * Phy-nspire — the general-relativity slice of the abstract/component bridge.
 *
 * phy_gr_compute() produces dense chart tensors. The abstract layer wants
 * typed index identities, and the component layer wants sparse realizations
 * bound to declared symmetry. This file is the single, explicit mapping
 * between the two, so that a curvature identity can be written once as an
 * abstract expression and evaluated against the components the GR pipeline
 * actually produced.
 *
 * Two things are deliberately *not* claimed here.
 *
 * First, this is not a rewrite of the curvature algorithms. Christoffel,
 * Riemann, Ricci and Einstein are still computed by src/gr/gr.c over dense
 * charts; this layer lifts the finished components. What becomes
 * expression-native is the *consumer* side -- contractions, traces and
 * curvature identities -- not the producer.
 *
 * Second, the symmetry declared for each head is the textbook slot group, and
 * it is frequently *stronger* than what the legacy tensor carries. Lowering
 * R^a_bcd or contracting it to R_ab drops every declaration the source had,
 * so the legacy R_abcd and R_ab arrive with no declared group at all. Rather
 * than inherit that loss, each head here declares the group the tensor
 * actually has, and phy_component_tensor_import_legacy() then *proves* it
 * against every dense source component with the exact CAS. A metric whose
 * curvature does not satisfy the declaration is a typed failure, not a silent
 * reinterpretation.
 *
 * For covariant/contravariant Riemann and Weyl tensors the bridge additionally
 * declares the (2,2) Young module. This is not smuggled into the signed slot
 * group: component import separately proves P_T(T) = T, after which
 * YoungReduce can impose the first Bianchi identity on abstract expressions.
 * See docs/TENSOR.md.
 */
#ifndef PHY_GR_BRIDGE_H
#define PHY_GR_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>

#include "phy/component_tensor.h"
#include "phy/gr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phy_gr_component_view phy_gr_component_view;

/*
 * The quantities a view can hold, with the valence each one is lifted at.
 *
 * Valence is fixed per quantity and is not a free choice: the bridge requires
 * one upper and one lower occurrence to contract, and never inserts a metric.
 * g_ab and g^ab are therefore two heads rather than one head at two valences,
 * exactly as R^a_bcd, R_abcd and R^abcd are three.
 */
typedef enum {
    PHY_GR_METRIC = 0,     /* g_ab      symmetric                        */
    PHY_GR_INVERSE_METRIC, /* g^ab      symmetric                        */
    PHY_GR_CHRISTOFFEL,    /* G^a_bc    symmetric in the two lower slots */
    PHY_GR_RIEMANN_MIXED,  /* R^a_bcd   antisymmetric in (c,d)           */
    PHY_GR_RIEMANN,        /* R_abcd    full Riemann slot group          */
    PHY_GR_RICCI,          /* R_ab      symmetric                        */
    PHY_GR_EINSTEIN,       /* G_ab      symmetric                        */
    PHY_GR_WEYL,           /* C_abcd    full Riemann slot group          */
    PHY_GR_RIEMANN_UPPER,  /* R^abcd    full Riemann slot group          */
    PHY_GR_QUANTITY_COUNT
} phy_gr_quantity;

/*
 * Optional quantities. Both are outside phy_gr_compute() for the reason
 * gr.h gives -- each one costs a full pass over n^4 components -- so each is
 * requested rather than assumed, and requesting one computes and caches it in
 * `result` exactly as calling phy_gr_weyl()/phy_gr_kretschmann() would.
 */
#define PHY_GR_BRIDGE_WEYL 0x1u
#define PHY_GR_BRIDGE_RIEMANN_UPPER 0x2u

typedef struct {
    size_t max_bytes; /* view metadata only; default 4 KiB */
    phy_basis_limits basis;
    phy_component_limits component;
} phy_gr_bridge_limits;

void phy_gr_bridge_limits_defaults(phy_gr_bridge_limits *out_limits);

/*
 * Build one component picture of `result` inside `context`.
 *
 * One IndexSpace named `space_name` (NULL selects "M") is created with the
 * chart's dimension and a symmetric metric, and one component basis over the
 * chart's own coordinate names, so a lifted component is an expression in the
 * coordinates the reader wrote. One TensorHead and one sparse realization are
 * then created per quantity.
 *
 * The space and the heads belong to `context` and live as long as it does;
 * the basis and the realizations belong to the view. `context` must be built
 * over `cas`, and `cas` over the same IR context as the metric's chart.
 *
 * Failure returns *out_view == NULL and destroys every component object the
 * attempt created. Index spaces and tensor heads have no individual
 * destructor -- the abstract context owns them in bulk -- so a failure after
 * the heads were declared leaves unreferenced head metadata in `context`
 * until the context itself is destroyed. That is stated rather than hidden:
 * it costs bounded metadata, it is visible in phy_abstract_head_count(), and
 * it is the price of putting GR heads in the caller's context so they can be
 * used in one monomial alongside the caller's own.
 */
phy_status phy_gr_component_view_create(
    phy_cas *cas, phy_abstract_context *context, phy_gr_result *result,
    const char *space_name, unsigned options,
    const phy_gr_bridge_limits *limits,
    phy_gr_component_view **out_view);
void phy_gr_component_view_destroy(phy_gr_component_view *view);

/* "Metric", "InverseMetric", ... ; NULL outside the enumeration. */
const char *phy_gr_quantity_name(phy_gr_quantity quantity);

/*
 * Rank and valence of a quantity, independent of any view. `out_valence`
 * receives phy_gr_quantity_rank() entries. Reported so that a caller can
 * declare a compatible head of its own -- a stress tensor to set against
 * G_ab, say -- without transcribing the table by hand.
 */
size_t phy_gr_quantity_rank(phy_gr_quantity quantity);
phy_status phy_gr_quantity_valence(phy_gr_quantity quantity,
                                   phy_ir_variance *out_valence);

const phy_abstract_context *phy_gr_component_view_context(
    const phy_gr_component_view *view);
size_t phy_gr_component_view_dimension(const phy_gr_component_view *view);
const phy_index_space *phy_gr_component_view_space(
    const phy_gr_component_view *view);
phy_component_basis *phy_gr_component_view_basis(
    const phy_gr_component_view *view);

/*
 * True when the view lifted `quantity`. The seven pipeline quantities are
 * always present; Weyl and R^abcd only when they were requested.
 */
bool phy_gr_component_view_holds(const phy_gr_component_view *view,
                                 phy_gr_quantity quantity);
const phy_abstract_tensor_head *phy_gr_component_view_head(
    const phy_gr_component_view *view, phy_gr_quantity quantity);
phy_component_tensor *phy_gr_component_view_tensor(
    const phy_gr_component_view *view, phy_gr_quantity quantity);

/*
 * Add the view's basis and every realization it holds to `binding`.
 *
 * Binding is idempotent, so a view may be bound into a binding that already
 * carries the caller's own tensors, and re-binding the same view is not an
 * error. A conflicting basis already bound for the view's IndexSpace is
 * PHY_ERR_ALREADY_INITIALIZED and leaves `binding` unchanged.
 */
phy_status phy_gr_component_view_bind(const phy_gr_component_view *view,
                                      phy_component_binding *binding);

#ifdef __cplusplus
}
#endif

#endif /* PHY_GR_BRIDGE_H */
