/*
 * Phy-nspire — coordinate charts.
 *
 * A chart is a dimension and one interned coordinate symbol per axis. It owns
 * nothing in the IR: the symbols and refs belong to the context that issued
 * them and stay valid for that context's life, which is why the context must
 * outlive the chart rather than the other way round.
 */
#include <string.h>

#include "tensor_internal.h"

/*
 * The IR has no rollback for interning, by design -- see the note on
 * phy_ir_read in include/phy/ir.h. So a chart whose fourth coordinate fails
 * to intern leaves the first three symbols in the context. That is a memory
 * concern and not a correctness one: the symbols are well-formed and
 * reachable, phy_ir_validate still passes, and destroying the context
 * reclaims them. What this file does guarantee is that no *chart* is leaked
 * and no half-built chart is ever returned.
 */
static phy_status intern_failure(phy_ir_context *ir)
{
    const phy_status status = phy_ir_last_error(ir);
    return status == PHY_OK ? PHY_ERR_OUT_OF_MEMORY : status;
}

phy_status phy_chart_create(phy_ir_context *ir,
                            const char *const *coordinate_names,
                            unsigned dimension, phy_chart **out_chart)
{
    if (ir == NULL || coordinate_names == NULL || out_chart == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (dimension == 0u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (dimension > PHY_TENSOR_MAX_DIM) {
        /* Well-formed, not implemented. */
        return PHY_ERR_UNSUPPORTED;
    }

    for (unsigned axis = 0u; axis < dimension; ++axis) {
        const char *name = coordinate_names[axis];
        if (name == NULL || name[0] == '\0') {
            return PHY_ERR_INVALID_ARGUMENT;
        }
        /*
         * Distinct names are not cosmetic. An axis that cannot be told apart
         * from another cannot be differentiated against, and the duplicate
         * would silently shadow the first everywhere phy_chart_axis_of is
         * used.
         */
        for (unsigned other = 0u; other < axis; ++other) {
            if (strcmp(name, coordinate_names[other]) == 0) {
                return PHY_ERR_INVALID_ARGUMENT;
            }
        }
    }

    phy_chart *chart = phy_alloc(sizeof(*chart));
    if (chart == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(chart, 0, sizeof(*chart));
    chart->ir = ir;
    chart->dimension = dimension;

    for (unsigned axis = 0u; axis < dimension; ++axis) {
        const phy_ir_symbol symbol = phy_ir_intern(ir, coordinate_names[axis]);
        if (symbol == PHY_IR_NO_SYMBOL) {
            const phy_status status = intern_failure(ir);
            phy_free(chart, sizeof(*chart));
            return status;
        }
        const phy_ir_ref ref = phy_ir_symbol_ref(ir, symbol);
        if (ref == PHY_IR_NULL) {
            const phy_status status = intern_failure(ir);
            phy_free(chart, sizeof(*chart));
            return status;
        }
        chart->symbols[axis] = symbol;
        chart->refs[axis] = ref;
    }

    *out_chart = chart;
    return PHY_OK;
}

void phy_chart_destroy(phy_chart *chart)
{
    if (chart == NULL) {
        return;
    }
    phy_free(chart, sizeof(*chart));
}

phy_ir_context *phy_chart_ir(const phy_chart *chart)
{
    return chart == NULL ? NULL : chart->ir;
}

unsigned phy_chart_dimension(const phy_chart *chart)
{
    return chart == NULL ? 0u : chart->dimension;
}

phy_ir_symbol phy_chart_coordinate_symbol(const phy_chart *chart, unsigned axis)
{
    if (chart == NULL || axis >= chart->dimension) {
        return PHY_IR_NO_SYMBOL;
    }
    return chart->symbols[axis];
}

phy_ir_ref phy_chart_coordinate(const phy_chart *chart, unsigned axis)
{
    if (chart == NULL || axis >= chart->dimension) {
        return PHY_IR_NULL;
    }
    return chart->refs[axis];
}

const char *phy_chart_coordinate_name(const phy_chart *chart, unsigned axis)
{
    if (chart == NULL || axis >= chart->dimension) {
        return NULL;
    }
    return phy_ir_symbol_name(chart->ir, chart->symbols[axis]);
}

unsigned phy_chart_axis_of(const phy_chart *chart, phy_ir_symbol symbol)
{
    if (chart == NULL || symbol == PHY_IR_NO_SYMBOL) {
        return PHY_CHART_NO_AXIS;
    }
    for (unsigned axis = 0u; axis < chart->dimension; ++axis) {
        if (chart->symbols[axis] == symbol) {
            return axis;
        }
    }
    return PHY_CHART_NO_AXIS;
}
