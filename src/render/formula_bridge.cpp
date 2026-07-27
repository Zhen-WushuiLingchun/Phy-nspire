#include "phy/formula.h"

#include <memory>
#include <new>
#include <string>
#include <string_view>

#include "ir_math_tree.h"
#include "nmarkdown/generated/core_math_font.h"
#include "nmarkdown/layout/fixed.h"
#include "nmarkdown/math/math_layout.h"
#include "nmarkdown/math/math_system.h"
#include "nmarkdown/render/surface565.h"
#include "nmarkdown/text/text_system.h"

namespace {

std::unique_ptr<nmarkdown::TextSystem> g_text;
std::unique_ptr<nmarkdown::MathSystem> g_math;

/*
 * Typed-IR references are interned and append-only, so a laid-out expression
 * never changes for the lifetime of its context. The notebook redraws every
 * visible output on every frame; without this cache each frame rebuilds the
 * MathTree and re-runs layout twice per cell (measure, then draw), which is
 * what made large Yang-Mills and phi^4 outputs crawl on the CX II. The cache
 * must be reset whenever an IR context is destroyed, because a new context
 * can reuse the address; phy_formula_ir_cache_reset is that hook.
 */
struct IrLayoutSlot {
    const phy_ir_context *context = nullptr;
    phy_ir_ref expression = 0;
    phy_formula_style style = PHY_FORMULA_STYLE_DISPLAY;
    int pixel_size = 0;
    int maximum_width = 0;
    std::uint32_t stamp = 0;
    std::shared_ptr<const nmarkdown::MathLayoutResult> layout;
};

constexpr std::size_t kIrLayoutSlots = 16;
IrLayoutSlot g_ir_layouts[kIrLayoutSlots];
std::uint32_t g_ir_layout_clock = 0;

void ir_cache_clear()
{
    for (IrLayoutSlot& slot : g_ir_layouts) {
        slot = IrLayoutSlot();
    }
    g_ir_layout_clock = 0;
}

void ir_cache_drop_context(const phy_ir_context *context)
{
    for (IrLayoutSlot& slot : g_ir_layouts) {
        if (slot.context == context) {
            slot = IrLayoutSlot();
        }
    }
}

nmarkdown::MathStyle convert_style(phy_formula_style style)
{
    return style == PHY_FORMULA_STYLE_DISPLAY
               ? nmarkdown::MathStyle::Display
               : nmarkdown::MathStyle::Text;
}

void export_metrics(const nmarkdown::MathLayoutResult& layout,
                    phy_formula_metrics *out_metrics)
{
    if (out_metrics == nullptr) {
        return;
    }
    out_metrics->width = nmarkdown::fx_ceil(layout.metrics.width);
    out_metrics->ascent = nmarkdown::fx_ceil(layout.metrics.ascent);
    out_metrics->descent = nmarkdown::fx_ceil(layout.metrics.descent);
    out_metrics->valid = layout.valid;
    out_metrics->overflow = layout.overflow;
}

phy_status validate_request(const char *source, size_t length, int pixel_size)
{
    if ((source == nullptr && length != 0u) || pixel_size <= 0) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (g_text == nullptr || g_math == nullptr || !g_text->ready()) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    return PHY_OK;
}

phy_status layout_formula(
    const char *source, size_t length, phy_formula_style style,
    int pixel_size, int maximum_width,
    std::shared_ptr<const nmarkdown::MathLayoutResult>& out_layout)
{
    const phy_status validation =
        validate_request(source, length, pixel_size);
    if (validation != PHY_OK) {
        return validation;
    }
    const std::string_view view(source != nullptr ? source : "", length);
    const nmarkdown::Fx width =
        maximum_width > 0 ? nmarkdown::fx_from_int(maximum_width) : 0;
    if (!g_math->layout(view, convert_style(style),
                        nmarkdown::fx_from_int(pixel_size), width, out_layout) ||
        out_layout == nullptr) {
        return PHY_ERR_BACKEND;
    }
    return PHY_OK;
}

phy_status validate_ir_request(const phy_ir_context *context,
                               phy_ir_ref expression, int pixel_size)
{
    if (context == nullptr || expression == PHY_IR_NULL ||
        phy_ir_kind_of(context, expression) == PHY_IR_KIND_INVALID ||
        pixel_size <= 0) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (g_text == nullptr || g_math == nullptr || !g_text->ready()) {
        return PHY_ERR_NOT_INITIALIZED;
    }
    return PHY_OK;
}

phy_status layout_ir_formula(
    const phy_ir_context *context, phy_ir_ref expression,
    phy_formula_style style, int pixel_size, int maximum_width,
    std::shared_ptr<const nmarkdown::MathLayoutResult>& out_layout)
{
    const phy_status validation =
        validate_ir_request(context, expression, pixel_size);
    if (validation != PHY_OK) {
        return validation;
    }

    for (IrLayoutSlot& slot : g_ir_layouts) {
        if (slot.layout != nullptr && slot.context == context &&
            slot.expression == expression && slot.style == style &&
            slot.pixel_size == pixel_size &&
            slot.maximum_width == maximum_width) {
            slot.stamp = ++g_ir_layout_clock;
            out_layout = slot.layout;
            return PHY_OK;
        }
    }

    nmarkdown::MathTree tree;
    std::string diagnostic;
    if (!phy_build_ir_math_tree(context, expression, tree, diagnostic)) {
        return PHY_ERR_BACKEND;
    }

    std::shared_ptr<nmarkdown::MathLayoutResult> layout =
        std::make_shared<nmarkdown::MathLayoutResult>();
    const nmarkdown::Fx width =
        maximum_width > 0 ? nmarkdown::fx_from_int(maximum_width) : 0;
    if (!nmarkdown::layout_math_tree(
            tree, *g_text, convert_style(style),
            nmarkdown::fx_from_int(pixel_size), width,
            nmarkdown::kCoreMathFontConstants, *layout)) {
        return PHY_ERR_BACKEND;
    }

    IrLayoutSlot *victim = &g_ir_layouts[0];
    for (IrLayoutSlot& slot : g_ir_layouts) {
        if (slot.layout == nullptr) {
            victim = &slot;
            break;
        }
        if (slot.stamp < victim->stamp) {
            victim = &slot;
        }
    }
    victim->context = context;
    victim->expression = expression;
    victim->style = style;
    victim->pixel_size = pixel_size;
    victim->maximum_width = maximum_width;
    victim->stamp = ++g_ir_layout_clock;
    victim->layout = layout;

    out_layout = std::move(layout);
    return PHY_OK;
}

phy_status draw_layout(const phy_surface *surface,
                       const nmarkdown::MathLayoutResult& layout,
                       int origin_x, int baseline_y, int pan_x,
                       uint16_t foreground, uint16_t background,
                       int clip_x, int clip_y, int clip_width,
                       int clip_height)
{
    nmarkdown::Surface565 target(surface->pixels, surface->width,
                                 surface->height, surface->width);
    const nmarkdown::Rect clip{clip_x, clip_y, clip_width, clip_height};
    return g_math->draw(target, layout, origin_x, baseline_y, pan_x,
                        foreground, background, true, clip)
               ? PHY_OK
               : PHY_ERR_BACKEND;
}

}  // namespace

extern "C" phy_status phy_formula_initialize(void)
{
    if (g_text != nullptr && g_math != nullptr && g_text->ready()) {
        return PHY_OK;
    }
    try {
        std::unique_ptr<nmarkdown::TextSystem> text(
            new nmarkdown::TextSystem());
        std::string error;
        if (!text->initialize(error)) {
            return PHY_ERR_BACKEND;
        }
        std::unique_ptr<nmarkdown::MathSystem> math(
            new nmarkdown::MathSystem(*text));
        g_text = std::move(text);
        g_math = std::move(math);
        phy_ir_set_destroy_observer(ir_cache_drop_context);
        return PHY_OK;
    } catch (const std::bad_alloc&) {
        g_math.reset();
        g_text.reset();
        return PHY_ERR_OUT_OF_MEMORY;
    } catch (...) {
        g_math.reset();
        g_text.reset();
        return PHY_ERR_BACKEND;
    }
}

extern "C" void phy_formula_shutdown(void)
{
    phy_ir_set_destroy_observer(NULL);
    ir_cache_clear();
    g_math.reset();
    g_text.reset();
}

extern "C" void phy_formula_ir_cache_reset(void)
{
    ir_cache_clear();
}

extern "C" bool phy_formula_is_ready(void)
{
    return g_text != nullptr && g_math != nullptr && g_text->ready();
}

extern "C" phy_status phy_formula_measure_latex(
    const char *source, size_t length, phy_formula_style style,
    int pixel_size, int maximum_width, phy_formula_metrics *out_metrics)
{
    if (out_metrics == nullptr) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_metrics = {};
    try {
        std::shared_ptr<const nmarkdown::MathLayoutResult> layout;
        const phy_status status =
            layout_formula(source, length, style, pixel_size, maximum_width,
                           layout);
        if (status != PHY_OK) {
            return status;
        }
        export_metrics(*layout, out_metrics);
        return PHY_OK;
    } catch (const std::bad_alloc&) {
        return PHY_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return PHY_ERR_BACKEND;
    }
}

extern "C" phy_status phy_formula_draw_latex(
    const phy_surface *surface, const char *source, size_t length,
    phy_formula_style style, int pixel_size, int maximum_width, int origin_x,
    int baseline_y, int pan_x, uint16_t foreground, uint16_t background,
    int clip_x, int clip_y, int clip_width, int clip_height,
    phy_formula_metrics *out_metrics)
{
    if (surface == nullptr || surface->pixels == nullptr ||
        surface->width <= 0 || surface->height <= 0 || clip_width <= 0 ||
        clip_height <= 0 || pan_x < 0) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (out_metrics != nullptr) {
        *out_metrics = {};
    }
    try {
        std::shared_ptr<const nmarkdown::MathLayoutResult> layout;
        const phy_status status =
            layout_formula(source, length, style, pixel_size, maximum_width,
                           layout);
        if (status != PHY_OK) {
            return status;
        }
        const phy_status draw_status =
            draw_layout(surface, *layout, origin_x, baseline_y, pan_x,
                        foreground, background, clip_x, clip_y, clip_width,
                        clip_height);
        if (draw_status != PHY_OK) {
            return draw_status;
        }
        export_metrics(*layout, out_metrics);
        return PHY_OK;
    } catch (const std::bad_alloc&) {
        return PHY_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return PHY_ERR_BACKEND;
    }
}

extern "C" phy_status phy_formula_measure_ir(
    const phy_ir_context *context, phy_ir_ref expression,
    phy_formula_style style, int pixel_size, int maximum_width,
    phy_formula_metrics *out_metrics)
{
    if (out_metrics == nullptr) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_metrics = {};
    try {
        std::shared_ptr<const nmarkdown::MathLayoutResult> layout;
        const phy_status status =
            layout_ir_formula(context, expression, style, pixel_size,
                              maximum_width, layout);
        if (status != PHY_OK) {
            return status;
        }
        export_metrics(*layout, out_metrics);
        return PHY_OK;
    } catch (const std::bad_alloc&) {
        return PHY_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return PHY_ERR_BACKEND;
    }
}

extern "C" phy_status phy_formula_draw_ir(
    const phy_surface *surface, const phy_ir_context *context,
    phy_ir_ref expression, phy_formula_style style, int pixel_size,
    int maximum_width, int origin_x, int baseline_y, int pan_x,
    uint16_t foreground, uint16_t background, int clip_x, int clip_y,
    int clip_width, int clip_height, phy_formula_metrics *out_metrics)
{
    if (surface == nullptr || surface->pixels == nullptr ||
        surface->width <= 0 || surface->height <= 0 || clip_width <= 0 ||
        clip_height <= 0 || pan_x < 0) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (out_metrics != nullptr) {
        *out_metrics = {};
    }
    try {
        std::shared_ptr<const nmarkdown::MathLayoutResult> layout;
        const phy_status status =
            layout_ir_formula(context, expression, style, pixel_size,
                              maximum_width, layout);
        if (status != PHY_OK) {
            return status;
        }
        const phy_status draw_status =
            draw_layout(surface, *layout, origin_x, baseline_y, pan_x,
                        foreground, background, clip_x, clip_y, clip_width,
                        clip_height);
        if (draw_status != PHY_OK) {
            return draw_status;
        }
        export_metrics(*layout, out_metrics);
        return PHY_OK;
    } catch (const std::bad_alloc&) {
        return PHY_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return PHY_ERR_BACKEND;
    }
}
