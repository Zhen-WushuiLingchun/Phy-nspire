/*
 * Phy-nspire — manifold metadata.
 *
 * A manifold is a dimension, an orientation, a signature, and a bounded list
 * of borrowed charts. It owns nothing in the IR and computes nothing: the
 * charts belong to the context that issued their coordinate symbols, and the
 * forms of form.c borrow the manifold in turn.
 *
 * The charts are registered, not related. There are no transition maps in this
 * layer, so nothing here identifies a component on one chart with a component
 * on another; see the pullback note in include/phy/geom.h.
 */
#include <string.h>

#include "geom_internal.h"

/*
 * The IR has no rollback for interning, by design -- see the note on
 * phy_ir_read in include/phy/ir.h. A failed intern therefore leaves a
 * well-formed symbol behind. What this file guarantees is narrower and is what
 * a caller can act on: no manifold is leaked and no half-built one is ever
 * returned.
 */
static phy_status intern_failure(phy_ir_context *ir)
{
    const phy_status status = phy_ir_last_error(ir);
    return status == PHY_OK ? PHY_ERR_OUT_OF_MEMORY : status;
}

phy_status phy_manifold_create(phy_ir_context *ir, const char *name,
                               unsigned dimension, phy_orientation orientation,
                               const int8_t *signature,
                               phy_manifold **out_manifold)
{
    if (ir == NULL || out_manifold == NULL || name == NULL || name[0] == '\0') {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (dimension == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (dimension > PHY_MANIFOLD_MAX_DIM) {
        /* Well-formed, not implemented. */
        return PHY_ERR_UNSUPPORTED;
    }
    switch (orientation) {
    case PHY_ORIENTATION_NONE:
    case PHY_ORIENTATION_POSITIVE:
    case PHY_ORIENTATION_NEGATIVE:
        break;
    default:
        return PHY_ERR_INVALID_ARGUMENT;
    }

    /*
     * A signature entry has no admissible value other than +1 or -1. This
     * layer represents an orthonormal signature, not a metric, so a 0 is a
     * degenerate metric the Hodge dual could not be defined against and a 2 is
     * a component of one.
     */
    if (signature != NULL) {
        for (unsigned axis = 0u; axis < dimension; ++axis) {
            if (signature[axis] != 1 && signature[axis] != -1) {
                return PHY_ERR_INVALID_ARGUMENT;
            }
        }
    }

    /* IR work first: it allocates inside the context, so failing here leaves
     * nothing of ours to unwind. */
    const phy_ir_symbol head = phy_ir_intern(ir, name);
    if (head == PHY_IR_NO_SYMBOL) {
        return intern_failure(ir);
    }
    const phy_ir_ref zero = phy_ir_integer(ir, 0);
    if (zero == PHY_IR_NULL) {
        return intern_failure(ir);
    }

    phy_manifold *manifold = phy_alloc(sizeof(*manifold));
    if (manifold == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(manifold, 0, sizeof(*manifold));

    manifold->ir = ir;
    manifold->head = head;
    manifold->dimension = dimension;
    manifold->orientation = orientation;
    manifold->zero = zero;

    for (unsigned axis = 0u; axis < dimension; ++axis) {
        const int8_t entry = (signature == NULL) ? (int8_t)1 : signature[axis];
        manifold->signature[axis] = entry;
        if (entry < 0) {
            manifold->negative_count++;
        }
    }

    *out_manifold = manifold;
    return PHY_OK;
}

void phy_manifold_destroy(phy_manifold *manifold)
{
    if (manifold == NULL) {
        return;
    }
    phy_free(manifold, sizeof(*manifold));
}

phy_ir_context *phy_manifold_ir(const phy_manifold *manifold)
{
    return manifold == NULL ? NULL : manifold->ir;
}

phy_ir_symbol phy_manifold_head(const phy_manifold *manifold)
{
    return manifold == NULL ? PHY_IR_NO_SYMBOL : manifold->head;
}

const char *phy_manifold_name(const phy_manifold *manifold)
{
    if (manifold == NULL) {
        return NULL;
    }
    return phy_ir_symbol_name(manifold->ir, manifold->head);
}

unsigned phy_manifold_dimension(const phy_manifold *manifold)
{
    return manifold == NULL ? 0u : manifold->dimension;
}

phy_orientation phy_manifold_orientation(const phy_manifold *manifold)
{
    return manifold == NULL ? PHY_ORIENTATION_NONE : manifold->orientation;
}

int phy_manifold_signature(const phy_manifold *manifold, unsigned axis)
{
    if (manifold == NULL || axis >= manifold->dimension) {
        return 0;
    }
    return manifold->signature[axis];
}

unsigned phy_manifold_negative_count(const phy_manifold *manifold)
{
    return manifold == NULL ? 0u : manifold->negative_count;
}

int phy_manifold_metric_sign(const phy_manifold *manifold)
{
    if (manifold == NULL) {
        return 0;
    }
    /* det g is the product of the diagonal, so its sign is (-1)^q. */
    return (manifold->negative_count % 2u == 0u) ? 1 : -1;
}

phy_status phy_manifold_add_chart(phy_manifold *manifold, phy_chart *chart,
                                  unsigned *out_index)
{
    if (manifold == NULL || chart == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_chart_dimension(chart) != manifold->dimension) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    /*
     * A ref is meaningful only in the context that issued it. Charts carry
     * their context, so unlike a bare phy_ir_ref this mismatch *is* reliably
     * detectable, and refusing it here is what keeps a form's components from
     * being differentiated against symbols from a foreign context.
     */
    if (phy_chart_ir(chart) != manifold->ir) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (unsigned index = 0u; index < manifold->chart_count; ++index) {
        if (manifold->charts[index] == chart) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
    }
    if (manifold->chart_count >= PHY_MANIFOLD_MAX_CHARTS) {
        return PHY_ERR_UNSUPPORTED;
    }

    manifold->charts[manifold->chart_count] = chart;
    if (out_index != NULL) {
        *out_index = manifold->chart_count;
    }
    manifold->chart_count++;
    return PHY_OK;
}

unsigned phy_manifold_chart_count(const phy_manifold *manifold)
{
    return manifold == NULL ? 0u : manifold->chart_count;
}

phy_chart *phy_manifold_chart(const phy_manifold *manifold, unsigned index)
{
    if (manifold == NULL || index >= manifold->chart_count) {
        return NULL;
    }
    return manifold->charts[index];
}
