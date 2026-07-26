#include "phy/formula.h"

#include <memory>
#include <new>
#include <string>
#include <string_view>

#include "nmarkdown/layout/fixed.h"
#include "nmarkdown/math/math_system.h"
#include "nmarkdown/render/surface565.h"
#include "nmarkdown/text/text_system.h"

namespace {

std::unique_ptr<nmarkdown::TextSystem> g_text;
std::unique_ptr<nmarkdown::MathSystem> g_math;

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
    g_math.reset();
    g_text.reset();
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
        nmarkdown::Surface565 target(surface->pixels, surface->width,
                                     surface->height, surface->width);
        const nmarkdown::Rect clip{
            clip_x, clip_y, clip_width, clip_height};
        if (!g_math->draw(target, *layout, origin_x, baseline_y, pan_x,
                          foreground, background, true, clip)) {
            return PHY_ERR_BACKEND;
        }
        export_metrics(*layout, out_metrics);
        return PHY_OK;
    } catch (const std::bad_alloc&) {
        return PHY_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return PHY_ERR_BACKEND;
    }
}
