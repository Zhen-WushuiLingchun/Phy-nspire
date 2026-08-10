/*
 * Put the exact QFT domain engines in the same typed tensor picture as the
 * general abstract/component layer.
 */
#include "phy/qft_bridge.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "phy/lie.h"
#include "phy/platform.h"

#define PHY_QFT_BRIDGE_DEFAULT_BYTES (8u * 1024u)
#define PHY_QFT_MAX_RANK 3u
#define PHY_QFT_MAX_GENERATORS 2u

typedef struct {
    const char *selector;
    const char *head_name;
    uint8_t rank;
    uint8_t spaces[PHY_QFT_MAX_RANK];
    phy_tensor_commutation commutation;
    uint8_t generator_count;
    uint16_t images[PHY_QFT_MAX_GENERATORS][PHY_QFT_MAX_RANK];
    int8_t signs[PHY_QFT_MAX_GENERATORS];
} qft_quantity_info;

static const char *const kSpaceNames[PHY_QFT_SPACE_COUNT] = {
    "Lorentz", "Spinor", "ColorAdjoint", "ColorFundamental"};

/*
 * The slot symmetries are exactly the identities already enforced by the
 * Lorentz and colour engines.  No Fierz or Clifford relation is represented
 * as a signed permutation.
 */
static const qft_quantity_info kQuantities[PHY_QFT_QUANTITY_COUNT] = {
    [PHY_QFT_MINKOWSKI_METRIC] = {
        "MinkowskiMetric", "eta", 2u,
        {PHY_QFT_SPACE_LORENTZ, PHY_QFT_SPACE_LORENTZ},
        PHY_TENSOR_COMMUTING, 1u, {{1u, 0u}}, {1}},
    [PHY_QFT_MINKOWSKI_INVERSE] = {
        "MinkowskiInverse", "etaInverse", 2u,
        {PHY_QFT_SPACE_LORENTZ, PHY_QFT_SPACE_LORENTZ},
        PHY_TENSOR_COMMUTING, 1u, {{1u, 0u}}, {1}},
    [PHY_QFT_MOMENTUM] = {
        "Momentum", "Momentum", 1u, {PHY_QFT_SPACE_LORENTZ},
        PHY_TENSOR_COMMUTING, 0u, {{0u}}, {0}},
    [PHY_QFT_DIRAC_GAMMA] = {
        "DiracGamma", "DiracGamma", 3u,
        {PHY_QFT_SPACE_LORENTZ, PHY_QFT_SPACE_SPINOR,
         PHY_QFT_SPACE_SPINOR},
        PHY_TENSOR_NONCOMMUTING, 0u, {{0u}}, {0}},
    [PHY_QFT_SUN_DELTA] = {
        "SUNDelta", "SUNDelta", 2u,
        {PHY_QFT_SPACE_COLOR_ADJOINT, PHY_QFT_SPACE_COLOR_ADJOINT},
        PHY_TENSOR_COMMUTING, 1u, {{1u, 0u}}, {1}},
    [PHY_QFT_SUN_F] = {
        "SUNF", "SUNF", 3u,
        {PHY_QFT_SPACE_COLOR_ADJOINT, PHY_QFT_SPACE_COLOR_ADJOINT,
         PHY_QFT_SPACE_COLOR_ADJOINT},
        PHY_TENSOR_COMMUTING, 2u,
        {{1u, 0u, 2u}, {0u, 2u, 1u}}, {-1, -1}},
    [PHY_QFT_SUN_D] = {
        "SUND", "SUND", 3u,
        {PHY_QFT_SPACE_COLOR_ADJOINT, PHY_QFT_SPACE_COLOR_ADJOINT,
         PHY_QFT_SPACE_COLOR_ADJOINT},
        PHY_TENSOR_COMMUTING, 2u,
        {{1u, 0u, 2u}, {0u, 2u, 1u}}, {1, 1}},
    [PHY_QFT_SUN_T] = {
        "SUNT", "SUNGenerator", 3u,
        {PHY_QFT_SPACE_COLOR_ADJOINT, PHY_QFT_SPACE_COLOR_FUNDAMENTAL,
         PHY_QFT_SPACE_COLOR_FUNDAMENTAL},
        PHY_TENSOR_NONCOMMUTING, 0u, {{0u}}, {0}},
    [PHY_QFT_GAUGE_POTENTIAL] = {
        "GaugePotential", "GaugePotential", 2u,
        {PHY_QFT_SPACE_COLOR_ADJOINT, PHY_QFT_SPACE_LORENTZ},
        PHY_TENSOR_COMMUTING, 0u, {{0u}}, {0}},
    [PHY_QFT_FIELD_STRENGTH] = {
        "FieldStrength", "FieldStrengthTensor", 3u,
        {PHY_QFT_SPACE_COLOR_ADJOINT, PHY_QFT_SPACE_LORENTZ,
         PHY_QFT_SPACE_LORENTZ},
        PHY_TENSOR_COMMUTING, 1u, {{0u, 2u, 1u}}, {-1}},
};

struct phy_qft_component_view {
    phy_cas *cas;
    phy_abstract_context *context;
    phy_ir_ref n;
    const phy_index_space *spaces[PHY_QFT_SPACE_COUNT];
    phy_component_basis *bases[PHY_QFT_SPACE_COUNT];
    const phy_abstract_tensor_head *heads[PHY_QFT_QUANTITY_COUNT];
    phy_component_tensor *tensors[PHY_QFT_QUANTITY_COUNT];
    phy_component_limits component_limits;
    size_t concrete_n;
};

static bool space_in_range(phy_qft_space space)
{
    return (unsigned)space < (unsigned)PHY_QFT_SPACE_COUNT;
}

static bool quantity_in_range(phy_qft_quantity quantity)
{
    return (unsigned)quantity < (unsigned)PHY_QFT_QUANTITY_COUNT;
}

const char *phy_qft_space_name(phy_qft_space space)
{
    return space_in_range(space) ? kSpaceNames[space] : NULL;
}

const char *phy_qft_quantity_name(phy_qft_quantity quantity)
{
    return quantity_in_range(quantity) ? kQuantities[quantity].selector : NULL;
}

void phy_qft_bridge_limits_defaults(phy_qft_bridge_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    memset(out_limits, 0, sizeof *out_limits);
    out_limits->max_bytes = PHY_QFT_BRIDGE_DEFAULT_BYTES;
    phy_basis_limits_defaults(&out_limits->basis);
    phy_component_limits_defaults(&out_limits->component);
}

static phy_status create_spaces(phy_qft_component_view *view,
                                phy_ir_ref adjoint_dimension)
{
    phy_ir_context *ir = phy_cas_ir(view->cas);
    const phy_ir_ref four = phy_ir_integer(ir, 4);
    if (four == PHY_IR_NULL) {
        return phy_ir_last_error(ir);
    }
    const phy_ir_ref dimensions[PHY_QFT_SPACE_COUNT] = {
        four, four, adjoint_dimension, view->n};
    const phy_metric_symmetry metrics[PHY_QFT_SPACE_COUNT] = {
        PHY_METRIC_SYMMETRIC, PHY_METRIC_NONE,
        PHY_METRIC_SYMMETRIC, PHY_METRIC_NONE};
    for (unsigned which = 0u; which < (unsigned)PHY_QFT_SPACE_COUNT;
         ++which) {
        phy_index_space *space = NULL;
        const phy_status status = phy_index_space_create(
            view->context, kSpaceNames[which], dimensions[which],
            metrics[which], &space);
        if (status != PHY_OK) {
            return status;
        }
        view->spaces[which] = space;
    }
    return PHY_OK;
}

static phy_status create_heads(phy_qft_component_view *view)
{
    for (unsigned quantity = 0u;
         quantity < (unsigned)PHY_QFT_QUANTITY_COUNT; ++quantity) {
        const qft_quantity_info *info = &kQuantities[quantity];
        const phy_index_space *spaces[PHY_QFT_MAX_RANK];
        const uint16_t *images[PHY_QFT_MAX_GENERATORS];
        int signs[PHY_QFT_MAX_GENERATORS];
        for (size_t slot = 0u; slot < (size_t)info->rank; ++slot) {
            spaces[slot] = view->spaces[info->spaces[slot]];
        }
        for (size_t generator = 0u;
             generator < (size_t)info->generator_count; ++generator) {
            images[generator] = info->images[generator];
            signs[generator] = (int)info->signs[generator];
        }
        phy_abstract_tensor_head *head = NULL;
        const phy_status status = phy_tensor_head_create_with_symmetries(
            view->context, info->head_name, spaces, (size_t)info->rank,
            info->commutation,
            info->generator_count == 0u ? NULL : images,
            info->generator_count == 0u ? NULL : signs,
            (size_t)info->generator_count, &head);
        if (status != PHY_OK) {
            return status;
        }
        view->heads[quantity] = head;
    }
    return PHY_OK;
}

static phy_status create_bases(phy_qft_component_view *view,
                               bool concrete_n, size_t n,
                               const phy_basis_limits *limits)
{
    const size_t dimensions[PHY_QFT_SPACE_COUNT] = {
        4u, 4u, concrete_n ? n * n - 1u : 0u, concrete_n ? n : 0u};
    for (unsigned which = 0u; which < (unsigned)PHY_QFT_SPACE_COUNT;
         ++which) {
        if (dimensions[which] == 0u) {
            continue;
        }
        const phy_status status = phy_component_basis_create(
            view->spaces[which], kSpaceNames[which], dimensions[which],
            NULL, limits, &view->bases[which]);
        if (status != PHY_OK) {
            return status;
        }
    }
    return PHY_OK;
}

static phy_status create_tensor(
    phy_qft_component_view *view, phy_qft_quantity quantity,
    const phy_ir_variance *valence, const phy_component_limits *limits)
{
    const qft_quantity_info *info = &kQuantities[quantity];
    phy_component_basis *bases[PHY_QFT_MAX_RANK];
    for (size_t slot = 0u; slot < (size_t)info->rank; ++slot) {
        bases[slot] = view->bases[info->spaces[slot]];
        if (bases[slot] == NULL) {
            return PHY_ERR_NOT_INITIALIZED;
        }
    }
    return phy_component_tensor_create(
        view->heads[quantity], bases, valence, limits,
        &view->tensors[quantity]);
}

static phy_status set_diagonal(phy_qft_component_view *view,
                               phy_qft_quantity quantity,
                               size_t dimension, bool minkowski)
{
    phy_ir_ref plus = PHY_IR_NULL;
    phy_ir_ref minus = PHY_IR_NULL;
    phy_status status = phy_cas_number(view->cas, 1, 1, &plus);
    if (status == PHY_OK) {
        status = phy_cas_number(view->cas, -1, 1, &minus);
    }
    for (size_t axis = 0u; status == PHY_OK && axis < dimension; ++axis) {
        if (axis > UINT32_MAX) {
            return PHY_ERR_OVERFLOW;
        }
        const uint32_t indices[2] = {
            (uint32_t)axis, (uint32_t)axis};
        status = phy_component_tensor_set(
            view->tensors[quantity], indices,
            minkowski && axis != 0u ? minus : plus);
    }
    return status;
}

static phy_status create_lorentz_components(
    phy_qft_component_view *view, const phy_component_limits *limits)
{
    static const phy_ir_variance down[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    static const phy_ir_variance up[2] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_UPPER};
    phy_status status = create_tensor(
        view, PHY_QFT_MINKOWSKI_METRIC, down, limits);
    if (status == PHY_OK) {
        status = set_diagonal(
            view, PHY_QFT_MINKOWSKI_METRIC, 4u, true);
    }
    if (status == PHY_OK) {
        status = create_tensor(
            view, PHY_QFT_MINKOWSKI_INVERSE, up, limits);
    }
    if (status == PHY_OK) {
        status = set_diagonal(
            view, PHY_QFT_MINKOWSKI_INVERSE, 4u, true);
    }
    return status;
}

static phy_status create_color_delta_components(
    phy_qft_component_view *view, size_t n,
    const phy_component_limits *limits)
{
    static const phy_ir_variance delta_valence[2] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_UPPER};
    const size_t adjoint = n * n - 1u;
    phy_status status = create_tensor(
        view, PHY_QFT_SUN_DELTA, delta_valence, limits);
    if (status == PHY_OK) {
        status = set_diagonal(
            view, PHY_QFT_SUN_DELTA, adjoint, false);
    }
    return status;
}

/*
 * Build the Lie algebra once, then copy its exact structure constants. The
 * old path rebuilt and revalidated the entire SU(3) algebra for each of the
 * 56 independent triples, which is why QFTSystem[3] took minutes on CX II.
 */
static phy_status materialize_sun_f(phy_qft_component_view *view)
{
    static const phy_ir_variance f_valence[3] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_UPPER, PHY_IR_INDEX_UPPER};
    const size_t n = view->concrete_n;
    if (n != 2u && n != 3u) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    phy_status status = create_tensor(
        view, PHY_QFT_SUN_F, f_valence, &view->component_limits);
    if (status != PHY_OK) {
        return status;
    }

    phy_lie_group *group = NULL;
    status = phy_lie_group_builtin(
        view->cas, n == 2u ? PHY_LIE_GROUP_SU2 : PHY_LIE_GROUP_SU3,
        &group);
    const phy_lie_algebra *algebra =
        status == PHY_OK ? phy_lie_group_algebra(group) : NULL;
    const size_t adjoint = n * n - 1u;
    for (size_t a = 0u; status == PHY_OK && a < adjoint; ++a) {
        for (size_t b = a + 1u; status == PHY_OK && b < adjoint; ++b) {
            for (size_t c = b + 1u; status == PHY_OK && c < adjoint; ++c) {
                const phy_ir_ref value =
                    phy_lie_structure_constant(
                        algebra, (unsigned)a, (unsigned)b, (unsigned)c);
                if (value == PHY_IR_NULL) {
                    status = PHY_ERR_BACKEND;
                } else {
                    const uint32_t indices[3] = {
                        (uint32_t)a, (uint32_t)b, (uint32_t)c};
                    status = phy_component_tensor_set(
                        view->tensors[PHY_QFT_SUN_F], indices, value);
                }
            }
        }
    }
    phy_lie_group_destroy(group);
    if (status != PHY_OK) {
        phy_component_tensor_destroy(
            view->tensors[PHY_QFT_SUN_F]);
        view->tensors[PHY_QFT_SUN_F] = NULL;
    }
    return status;
}

phy_status phy_qft_component_view_create(
    phy_cas *cas, phy_abstract_context *context, phy_ir_ref n,
    const phy_qft_bridge_limits *limits,
    phy_qft_component_view **out_view)
{
    if (out_view == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_view = NULL;
    if (cas == NULL || context == NULL || n == PHY_IR_NULL ||
        phy_abstract_cas(context) != cas) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_qft_bridge_limits resolved;
    phy_qft_bridge_limits_defaults(&resolved);
    if (limits != NULL) {
        resolved = *limits;
        if (resolved.max_bytes == 0u) {
            resolved.max_bytes = PHY_QFT_BRIDGE_DEFAULT_BYTES;
        }
    }
    if (sizeof(phy_qft_component_view) > resolved.max_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }

    phy_color *color = NULL;
    phy_status status = phy_color_create(cas, n, &color);
    phy_ir_ref adjoint_dimension = PHY_IR_NULL;
    if (status == PHY_OK) {
        status = phy_color_adjoint_dimension(color, &adjoint_dimension);
    }
    if (status != PHY_OK) {
        phy_color_destroy(color);
        return status;
    }

    phy_qft_component_view *view = phy_alloc(sizeof *view);
    if (view == NULL) {
        phy_color_destroy(color);
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(view, 0, sizeof *view);
    view->cas = cas;
    view->context = context;
    view->n = n;
    view->component_limits = resolved.component;

    status = create_spaces(view, adjoint_dimension);
    if (status == PHY_OK) {
        status = create_heads(view);
    }

    int64_t n_value = 0;
    const bool concrete_n =
        phy_ir_integer_value(phy_cas_ir(cas), n, &n_value) &&
        n_value >= 2 &&
        (uint64_t)n_value <= (uint64_t)SIZE_MAX &&
        (size_t)n_value <= SIZE_MAX / (size_t)n_value;
    phy_basis_limits basis_defaults;
    phy_component_limits component_defaults;
    phy_basis_limits_defaults(&basis_defaults);
    phy_component_limits_defaults(&component_defaults);
    const size_t basis_max_dimension =
        resolved.basis.max_dimension != 0u
            ? resolved.basis.max_dimension
            : basis_defaults.max_dimension;
    const size_t component_max_dimension =
        resolved.component.max_dimension != 0u
            ? resolved.component.max_dimension
            : component_defaults.max_dimension;
    const size_t component_max_entries =
        resolved.component.max_entries != 0u
            ? resolved.component.max_entries
            : component_defaults.max_entries;
    const size_t adjoint =
        concrete_n ? (size_t)n_value * (size_t)n_value - 1u : 0u;
    const bool concrete_basis =
        concrete_n && (size_t)n_value <= basis_max_dimension &&
        adjoint <= basis_max_dimension &&
        adjoint <= component_max_dimension &&
        adjoint <= component_max_entries;
    view->concrete_n =
        concrete_basis ? (size_t)n_value : 0u;
    if (status == PHY_OK) {
        status = create_bases(
            view, concrete_basis,
            concrete_basis ? (size_t)n_value : 0u,
            &resolved.basis);
    }
    if (status == PHY_OK) {
        status = create_lorentz_components(view, &resolved.component);
    }
    if (status == PHY_OK && concrete_basis) {
        status = create_color_delta_components(
            view, (size_t)n_value, &resolved.component);
    }
    phy_color_destroy(color);
    if (status != PHY_OK) {
        phy_qft_component_view_destroy(view);
        return status;
    }
    *out_view = view;
    return PHY_OK;
}

void phy_qft_component_view_destroy(phy_qft_component_view *view)
{
    if (view == NULL) {
        return;
    }
    for (unsigned quantity = 0u;
         quantity < (unsigned)PHY_QFT_QUANTITY_COUNT; ++quantity) {
        phy_component_tensor_destroy(view->tensors[quantity]);
    }
    for (unsigned space = 0u; space < (unsigned)PHY_QFT_SPACE_COUNT;
         ++space) {
        phy_component_basis_destroy(view->bases[space]);
    }
    phy_free(view, sizeof *view);
}

phy_ir_ref phy_qft_component_view_n(const phy_qft_component_view *view)
{
    return view != NULL ? view->n : PHY_IR_NULL;
}

const phy_index_space *phy_qft_component_view_space(
    const phy_qft_component_view *view, phy_qft_space space)
{
    return view != NULL && space_in_range(space) ? view->spaces[space] : NULL;
}

const phy_abstract_tensor_head *phy_qft_component_view_head(
    const phy_qft_component_view *view, phy_qft_quantity quantity)
{
    return view != NULL && quantity_in_range(quantity)
               ? view->heads[quantity]
               : NULL;
}

bool phy_qft_component_view_has_basis(
    const phy_qft_component_view *view, phy_qft_space space)
{
    return phy_qft_component_view_basis(view, space) != NULL;
}

phy_component_basis *phy_qft_component_view_basis(
    const phy_qft_component_view *view, phy_qft_space space)
{
    return view != NULL && space_in_range(space) ? view->bases[space] : NULL;
}

bool phy_qft_component_view_holds(
    const phy_qft_component_view *view, phy_qft_quantity quantity)
{
    return phy_qft_component_view_tensor(view, quantity) != NULL;
}

phy_component_tensor *phy_qft_component_view_tensor(
    const phy_qft_component_view *view, phy_qft_quantity quantity)
{
    return view != NULL && quantity_in_range(quantity)
               ? view->tensors[quantity]
               : NULL;
}

phy_status phy_qft_component_view_materialize(
    phy_qft_component_view *view, phy_qft_quantity quantity)
{
    if (view == NULL || !quantity_in_range(quantity)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (view->tensors[quantity] != NULL) {
        return PHY_OK;
    }
    if (quantity == PHY_QFT_SUN_F) {
        return materialize_sun_f(view);
    }
    return PHY_ERR_NOT_INITIALIZED;
}

phy_status phy_qft_component_view_bind(
    const phy_qft_component_view *view, phy_component_binding *binding)
{
    if (view == NULL || binding == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = PHY_OK;
    for (unsigned space = 0u;
         status == PHY_OK && space < (unsigned)PHY_QFT_SPACE_COUNT;
         ++space) {
        if (view->bases[space] != NULL) {
            status = phy_component_binding_add_basis(
                binding, view->bases[space]);
        }
    }
    for (unsigned quantity = 0u;
         status == PHY_OK &&
         quantity < (unsigned)PHY_QFT_QUANTITY_COUNT; ++quantity) {
        if (view->tensors[quantity] != NULL) {
            status = phy_component_binding_add_tensor(
                binding, view->tensors[quantity]);
        }
    }
    return status;
}
