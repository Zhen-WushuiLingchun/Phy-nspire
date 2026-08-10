/*
 * Lift a finished curvature computation into one typed abstract/component
 * picture.
 *
 * The whole file is a table plus a validation pre-pass. That is deliberate:
 * every interesting decision -- which slot group each curvature tensor really
 * has, and at which valence it is lifted -- belongs in data that can be read
 * against a textbook, not in control flow.
 */
#include "phy/gr_bridge.h"

#include <string.h>

#include "phy/platform.h"

#define PHY_GR_BRIDGE_DEFAULT_BYTES (4u * 1024u)
#define PHY_GR_BRIDGE_MAX_RANK 4u
#define PHY_GR_BRIDGE_MAX_GENERATORS 3u
#define PHY_GR_BRIDGE_OPTIONS \
    (PHY_GR_BRIDGE_WEYL | PHY_GR_BRIDGE_RIEMANN_UPPER)

typedef struct {
    const char *name;
    uint8_t rank;
    phy_ir_variance valence[PHY_GR_BRIDGE_MAX_RANK];
    uint8_t generator_count;
    uint16_t images[PHY_GR_BRIDGE_MAX_GENERATORS][PHY_GR_BRIDGE_MAX_RANK];
    int8_t signs[PHY_GR_BRIDGE_MAX_GENERATORS];
} gr_quantity_info;

#define GR_LOWER PHY_IR_INDEX_LOWER
#define GR_UPPER PHY_IR_INDEX_UPPER

/*
 * The Riemann slot group in image notation: antisymmetry in the first pair,
 * antisymmetry in the second, and exchange of the pairs. All three are
 * involutions, so this table reads the same whether `image` is taken as the
 * destination of each slot or as its source; nothing here depends on that
 * convention being resolved one way.
 */
static const gr_quantity_info kQuantities[PHY_GR_QUANTITY_COUNT] = {
    [PHY_GR_METRIC] = {"Metric",
                       2u,
                       {GR_LOWER, GR_LOWER},
                       1u,
                       {{1u, 0u}},
                       {1}},
    [PHY_GR_INVERSE_METRIC] = {"InverseMetric",
                               2u,
                               {GR_UPPER, GR_UPPER},
                               1u,
                               {{1u, 0u}},
                               {1}},
    [PHY_GR_CHRISTOFFEL] = {"Christoffel",
                            3u,
                            {GR_UPPER, GR_LOWER, GR_LOWER},
                            1u,
                            {{0u, 2u, 1u}},
                            {1}},
    [PHY_GR_RIEMANN_MIXED] = {"RiemannMixed",
                              4u,
                              {GR_UPPER, GR_LOWER, GR_LOWER, GR_LOWER},
                              1u,
                              {{0u, 1u, 3u, 2u}},
                              {-1}},
    [PHY_GR_RIEMANN] = {"Riemann",
                        4u,
                        {GR_LOWER, GR_LOWER, GR_LOWER, GR_LOWER},
                        3u,
                        {{1u, 0u, 2u, 3u},
                         {0u, 1u, 3u, 2u},
                         {2u, 3u, 0u, 1u}},
                        {-1, -1, 1}},
    [PHY_GR_RICCI] = {"Ricci", 2u, {GR_LOWER, GR_LOWER}, 1u, {{1u, 0u}}, {1}},
    [PHY_GR_EINSTEIN] = {"Einstein",
                         2u,
                         {GR_LOWER, GR_LOWER},
                         1u,
                         {{1u, 0u}},
                         {1}},
    [PHY_GR_WEYL] = {"Weyl",
                     4u,
                     {GR_LOWER, GR_LOWER, GR_LOWER, GR_LOWER},
                     3u,
                     {{1u, 0u, 2u, 3u},
                      {0u, 1u, 3u, 2u},
                      {2u, 3u, 0u, 1u}},
                     {-1, -1, 1}},
    [PHY_GR_RIEMANN_UPPER] = {"RiemannUpper",
                              4u,
                              {GR_UPPER, GR_UPPER, GR_UPPER, GR_UPPER},
                              3u,
                              {{1u, 0u, 2u, 3u},
                               {0u, 1u, 3u, 2u},
                               {2u, 3u, 0u, 1u}},
                              {-1, -1, 1}},
};

struct phy_gr_component_view {
    phy_abstract_context *context;
    const phy_index_space *space;
    phy_component_basis *basis;
    size_t dimension;
    const phy_abstract_tensor_head *heads[PHY_GR_QUANTITY_COUNT];
    phy_component_tensor *tensors[PHY_GR_QUANTITY_COUNT];
};

static bool quantity_in_range(phy_gr_quantity quantity)
{
    return (unsigned)quantity < (unsigned)PHY_GR_QUANTITY_COUNT;
}

const char *phy_gr_quantity_name(phy_gr_quantity quantity)
{
    return quantity_in_range(quantity) ? kQuantities[quantity].name : NULL;
}

size_t phy_gr_quantity_rank(phy_gr_quantity quantity)
{
    return quantity_in_range(quantity)
               ? (size_t)kQuantities[quantity].rank
               : 0u;
}

phy_status phy_gr_quantity_valence(phy_gr_quantity quantity,
                                   phy_ir_variance *out_valence)
{
    if (!quantity_in_range(quantity) || out_valence == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const gr_quantity_info *info = &kQuantities[quantity];
    for (size_t slot = 0u; slot < (size_t)info->rank; ++slot) {
        out_valence[slot] = info->valence[slot];
    }
    return PHY_OK;
}

void phy_gr_bridge_limits_defaults(phy_gr_bridge_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    memset(out_limits, 0, sizeof *out_limits);
    out_limits->max_bytes = PHY_GR_BRIDGE_DEFAULT_BYTES;
    phy_basis_limits_defaults(&out_limits->basis);
    phy_component_limits_defaults(&out_limits->component);
}

static const phy_tensor *quantity_source(const phy_gr_result *result,
                                         const phy_tensor *weyl,
                                         phy_gr_quantity quantity)
{
    switch (quantity) {
    case PHY_GR_METRIC:
        return phy_gr_metric(result);
    case PHY_GR_INVERSE_METRIC:
        return phy_gr_inverse_metric(result);
    case PHY_GR_CHRISTOFFEL:
        return phy_gr_christoffel(result);
    case PHY_GR_RIEMANN_MIXED:
        return phy_gr_riemann_mixed(result);
    case PHY_GR_RIEMANN:
        return phy_gr_riemann_covariant(result);
    case PHY_GR_RICCI:
        return phy_gr_ricci(result);
    case PHY_GR_EINSTEIN:
        return phy_gr_einstein(result);
    case PHY_GR_WEYL:
        return weyl;
    case PHY_GR_RIEMANN_UPPER:
        return phy_gr_riemann_contravariant(result);
    default:
        break;
    }
    return NULL;
}

static bool quantity_selected(phy_gr_quantity quantity, unsigned options)
{
    if (quantity == PHY_GR_WEYL) {
        return (options & PHY_GR_BRIDGE_WEYL) != 0u;
    }
    if (quantity == PHY_GR_RIEMANN_UPPER) {
        return (options & PHY_GR_BRIDGE_RIEMANN_UPPER) != 0u;
    }
    return true;
}

static bool quantity_has_riemann_young(phy_gr_quantity quantity)
{
    return quantity == PHY_GR_RIEMANN ||
           quantity == PHY_GR_WEYL ||
           quantity == PHY_GR_RIEMANN_UPPER;
}

/*
 * Reject a shape mismatch before anything is allocated.
 *
 * phy_component_tensor_import_legacy() re-checks rank, dimension, chart
 * coordinates and IR context on its own, so this pass exists for the one
 * thing it cannot check -- that the source really carries the valence this
 * table documents -- and to move the remaining diagnostics ahead of the first
 * head declaration, which cannot be undone.
 */
static phy_status validate_sources(const phy_gr_result *result,
                                   const phy_tensor *weyl, unsigned options,
                                   const phy_chart *chart, size_t dimension)
{
    for (unsigned quantity = 0u; quantity < (unsigned)PHY_GR_QUANTITY_COUNT;
         ++quantity) {
        if (!quantity_selected((phy_gr_quantity)quantity, options)) {
            continue;
        }
        const gr_quantity_info *info = &kQuantities[quantity];
        const phy_tensor *source =
            quantity_source(result, weyl, (phy_gr_quantity)quantity);
        if (source == NULL) {
            return PHY_ERR_NOT_INITIALIZED;
        }
        if (phy_tensor_chart(source) != chart ||
            (size_t)phy_tensor_dimension(source) != dimension ||
            (size_t)phy_tensor_rank(source) != (size_t)info->rank) {
            return PHY_ERR_TYPE;
        }
        for (unsigned slot = 0u; slot < info->rank; ++slot) {
            if (phy_tensor_valence(source, slot) != info->valence[slot]) {
                return PHY_ERR_TYPE;
            }
        }
    }
    return PHY_OK;
}

static phy_status build_quantity(phy_gr_component_view *view,
                                 const phy_gr_result *result,
                                 const phy_tensor *weyl,
                                 phy_gr_quantity quantity,
                                 const phy_component_limits *limits)
{
    const gr_quantity_info *info = &kQuantities[quantity];
    const phy_index_space *spaces[PHY_GR_BRIDGE_MAX_RANK];
    phy_component_basis *bases[PHY_GR_BRIDGE_MAX_RANK];
    const uint16_t *images[PHY_GR_BRIDGE_MAX_GENERATORS];
    int signs[PHY_GR_BRIDGE_MAX_GENERATORS];
    for (size_t slot = 0u; slot < (size_t)info->rank; ++slot) {
        spaces[slot] = view->space;
        bases[slot] = view->basis;
    }
    for (size_t generator = 0u; generator < (size_t)info->generator_count;
         ++generator) {
        images[generator] = info->images[generator];
        signs[generator] = (int)info->signs[generator];
    }

    /*
     * Tensor-head names are private identities, not the public quantity
     * selector. Prefix them with the view's IndexSpace so two component
     * pictures can coexist in one abstract context without aliasing heads
     * that belong to different spaces.
     */
    const char *space_name = phy_index_space_name(view->space);
    if (space_name == NULL) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    const size_t space_length = strlen(space_name);
    const size_t quantity_length = strlen(info->name);
    if (space_length > SIZE_MAX - quantity_length - 2u) {
        return PHY_ERR_OVERFLOW;
    }
    const size_t head_name_bytes = space_length + quantity_length + 2u;
    char *head_name = phy_alloc(head_name_bytes);
    if (head_name == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memcpy(head_name, space_name, space_length);
    head_name[space_length] = '$';
    memcpy(head_name + space_length + 1u, info->name,
           quantity_length + 1u);

    phy_abstract_tensor_head *head = NULL;
    phy_status status = phy_tensor_head_create_with_symmetries(
        view->context, head_name, spaces, (size_t)info->rank,
        PHY_TENSOR_COMMUTING, images, signs,
        (size_t)info->generator_count, &head);
    phy_free(head_name, head_name_bytes);
    if (status == PHY_OK && quantity_has_riemann_young(quantity)) {
        static const uint16_t young_slots[4] = {0u, 2u, 1u, 3u};
        static const uint16_t young_rows[2] = {2u, 2u};
        const phy_young_tableau tableau = {
            young_slots, 4u, young_rows, 2u,
            PHY_YOUNG_ROW_SYMMETRY_LAST};
        status = phy_tensor_head_set_young_symmetry(
            head, &tableau, NULL);
    }
    if (status != PHY_OK) {
        return status;
    }
    phy_component_tensor *tensor = NULL;
    status = phy_component_tensor_import_legacy(
        quantity_source(result, weyl, quantity), head, bases, limits,
        &tensor);
    if (status != PHY_OK) {
        return status;
    }
    view->heads[quantity] = head;
    view->tensors[quantity] = tensor;
    return PHY_OK;
}

phy_status phy_gr_component_view_create(
    phy_cas *cas, phy_abstract_context *context, phy_gr_result *result,
    const char *space_name, unsigned options,
    const phy_gr_bridge_limits *limits,
    phy_gr_component_view **out_view)
{
    if (out_view == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_view = NULL;
    if (cas == NULL || context == NULL || result == NULL ||
        (options & ~(unsigned)PHY_GR_BRIDGE_OPTIONS) != 0u ||
        phy_abstract_cas(context) != cas) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const phy_tensor *metric = phy_gr_metric(result);
    if (metric == NULL) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    const phy_chart *chart = phy_tensor_chart(metric);
    phy_ir_context *ir = phy_chart_ir(chart);
    if (ir != phy_cas_ir(cas)) {
        return PHY_ERR_TYPE;
    }

    phy_gr_bridge_limits resolved;
    phy_gr_bridge_limits_defaults(&resolved);
    if (limits != NULL) {
        resolved = *limits;
        if (resolved.max_bytes == 0u) {
            resolved.max_bytes = PHY_GR_BRIDGE_DEFAULT_BYTES;
        }
    }
    if (sizeof(phy_gr_component_view) > resolved.max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }

    /*
     * The optional quantities are computed before anything is declared, so
     * that an expensive failure costs no abstract metadata.
     */
    const phy_tensor *weyl = NULL;
    phy_status status = PHY_OK;
    if ((options & PHY_GR_BRIDGE_WEYL) != 0u) {
        status = phy_gr_weyl(cas, result, &weyl);
    }
    if (status == PHY_OK && (options & PHY_GR_BRIDGE_RIEMANN_UPPER) != 0u) {
        phy_ir_ref kretschmann = PHY_IR_NULL;
        status = phy_gr_kretschmann(cas, result, &kretschmann);
    }
    if (status != PHY_OK) {
        return status;
    }

    const size_t dimension = (size_t)phy_tensor_dimension(metric);
    status = validate_sources(result, weyl, options, chart, dimension);
    if (status != PHY_OK) {
        return status;
    }

    phy_gr_component_view *view = phy_alloc(sizeof *view);
    if (view == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(view, 0, sizeof *view);
    view->context = context;
    view->dimension = dimension;

    if (dimension > PHY_TENSOR_MAX_DIM) {
        phy_free(view, sizeof *view);
        return PHY_ERR_UNSUPPORTED;
    }
    const char *coordinates[PHY_TENSOR_MAX_DIM];
    for (size_t axis = 0u; axis < dimension; ++axis) {
        coordinates[axis] =
            phy_chart_coordinate_name(chart, (unsigned)axis);
        if (coordinates[axis] == NULL) {
            phy_free(view, sizeof *view);
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
    }

    const phy_ir_ref extent = phy_ir_integer(ir, (int64_t)dimension);
    if (extent == PHY_IR_NULL) {
        status = phy_ir_last_error(ir);
        phy_free(view, sizeof *view);
        return status == PHY_OK ? PHY_ERR_OUT_OF_MEMORY : status;
    }

    phy_index_space *space = NULL;
    status = phy_index_space_create(
        context, space_name != NULL ? space_name : "M", extent,
        PHY_METRIC_SYMMETRIC, &space);
    if (status != PHY_OK) {
        phy_free(view, sizeof *view);
        return status;
    }
    view->space = space;
    status = phy_component_basis_create(
        space, "chart", dimension, coordinates, &resolved.basis,
        &view->basis);
    for (unsigned quantity = 0u;
         status == PHY_OK && quantity < (unsigned)PHY_GR_QUANTITY_COUNT;
         ++quantity) {
        if (!quantity_selected((phy_gr_quantity)quantity, options)) {
            continue;
        }
        status = build_quantity(view, result, weyl,
                                (phy_gr_quantity)quantity,
                                &resolved.component);
    }
    if (status != PHY_OK) {
        phy_gr_component_view_destroy(view);
        return status;
    }
    *out_view = view;
    return PHY_OK;
}

void phy_gr_component_view_destroy(phy_gr_component_view *view)
{
    if (view == NULL) {
        return;
    }
    for (unsigned quantity = 0u; quantity < (unsigned)PHY_GR_QUANTITY_COUNT;
         ++quantity) {
        phy_component_tensor_destroy(view->tensors[quantity]);
    }
    phy_component_basis_destroy(view->basis);
    phy_free(view, sizeof *view);
}

const phy_abstract_context *phy_gr_component_view_context(
    const phy_gr_component_view *view)
{
    return view != NULL ? view->context : NULL;
}

size_t phy_gr_component_view_dimension(const phy_gr_component_view *view)
{
    return view != NULL ? view->dimension : 0u;
}

const phy_index_space *phy_gr_component_view_space(
    const phy_gr_component_view *view)
{
    return view != NULL ? view->space : NULL;
}

phy_component_basis *phy_gr_component_view_basis(
    const phy_gr_component_view *view)
{
    return view != NULL ? view->basis : NULL;
}

bool phy_gr_component_view_holds(const phy_gr_component_view *view,
                                 phy_gr_quantity quantity)
{
    return view != NULL && quantity_in_range(quantity) &&
           view->tensors[quantity] != NULL;
}

const phy_abstract_tensor_head *phy_gr_component_view_head(
    const phy_gr_component_view *view, phy_gr_quantity quantity)
{
    return phy_gr_component_view_holds(view, quantity)
               ? view->heads[quantity]
               : NULL;
}

phy_component_tensor *phy_gr_component_view_tensor(
    const phy_gr_component_view *view, phy_gr_quantity quantity)
{
    return phy_gr_component_view_holds(view, quantity)
               ? view->tensors[quantity]
               : NULL;
}

phy_status phy_gr_component_view_bind(const phy_gr_component_view *view,
                                      phy_component_binding *binding)
{
    if (view == NULL || binding == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status =
        phy_component_binding_add_basis(binding, view->basis);
    for (unsigned quantity = 0u;
         status == PHY_OK && quantity < (unsigned)PHY_GR_QUANTITY_COUNT;
         ++quantity) {
        if (view->tensors[quantity] == NULL) {
            continue;
        }
        status = phy_component_binding_add_tensor(
            binding, view->tensors[quantity]);
    }
    return status;
}
