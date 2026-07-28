#include "phy/math_layout.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "phy/phy.h"

#define MATH_MAX_DEPTH 64u
#define MATH_MAX_DIMENSION 16384
#define MATH_SCRIPT_SHIFT 4
#define MATH_FRACTION_GAP 2

typedef struct {
    phy_math_box box;
    bool valid;
} measured;

static const char *function_display_name(const char *name)
{
    if (strcmp(name, "asin") == 0) {
        return "arcsin";
    }
    if (strcmp(name, "acos") == 0) {
        return "arccos";
    }
    if (strcmp(name, "atan") == 0) {
        return "arctan";
    }
    if (strcmp(name, "asinh") == 0) {
        return "arcsinh";
    }
    if (strcmp(name, "acosh") == 0) {
        return "arccosh";
    }
    if (strcmp(name, "atanh") == 0) {
        return "arctanh";
    }
    if (strcmp(name, "gammafn") == 0) {
        return "Gamma";
    }
    if (strcmp(name, "loggamma") == 0) {
        return "LogGamma";
    }
    return name;
}

static int bounded_add(int left, int right)
{
    if (left >= MATH_MAX_DIMENSION - right) {
        return MATH_MAX_DIMENSION;
    }
    return left + right;
}

static phy_math_box text_box(const char *text)
{
    const phy_math_box box = {
        phy_gfx_text_width(text),
        PHY_GLYPH_HEIGHT,
        PHY_GLYPH_HEIGHT - 1,
    };
    return box;
}

static void format_i64(int64_t value, char buffer[32])
{
    char reversed[21];
    size_t count = 0u;
    const bool negative = value < 0;
    uint64_t magnitude;
    if (negative) {
        magnitude = (uint64_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint64_t)value;
    }

    do {
        reversed[count++] = (char)('0' + (char)(magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0u && count < sizeof reversed);

    size_t out = 0u;
    if (negative) {
        buffer[out++] = '-';
    }
    while (count > 0u) {
        buffer[out++] = reversed[--count];
    }
    buffer[out] = '\0';
}

static measured measure_expr(const phy_ir_context *ir, phy_ir_ref expression,
                             unsigned depth);

static int expression_precedence(phy_ir_kind kind)
{
    switch (kind) {
    case PHY_IR_EQUATION:
        return 1;
    case PHY_IR_ADD:
        return 2;
    case PHY_IR_MUL:
    case PHY_IR_NCMUL:
    case PHY_IR_WEDGE:
        return 3;
    case PHY_IR_POW:
        return 4;
    default:
        return 5;
    }
}

static bool child_needs_parentheses(phy_ir_kind parent, phy_ir_kind child,
                                    size_t position)
{
    if (parent == PHY_IR_POW) {
        /*
         * A two-dimensional superscript groups its exponent visually, but the
         * base still needs explicit grouping.  In particular, (m+x)^3 must
         * never be displayed as m+x^3, and (x^a)^b must not look like x^a^b.
         */
        return position == 0u &&
               expression_precedence(child) <=
                   expression_precedence(PHY_IR_POW);
    }
    return expression_precedence(child) < expression_precedence(parent);
}

static measured invalid_measure(void)
{
    const measured result = {{0, 0, 0}, false};
    return result;
}

static measured text_measure(const char *text)
{
    const measured result = {text_box(text), true};
    return result;
}

static measured measure_child(const phy_ir_context *ir, phy_ir_ref expression,
                              phy_ir_kind parent, size_t position,
                              unsigned depth)
{
    measured child = measure_expr(ir, expression, depth);
    if (!child.valid ||
        !child_needs_parentheses(parent, phy_ir_kind_of(ir, expression),
                                 position)) {
        return child;
    }

    const int paren_width = phy_gfx_text_width("(") + phy_gfx_text_width(")");
    const int baseline =
        child.box.baseline > PHY_GLYPH_HEIGHT - 1
            ? child.box.baseline
            : PHY_GLYPH_HEIGHT - 1;
    const int child_descent = child.box.height - child.box.baseline;
    const int descent = child_descent > 1 ? child_descent : 1;
    child.box.width = bounded_add(child.box.width, paren_width);
    child.box.height = bounded_add(baseline, descent);
    child.box.baseline = baseline;
    return child;
}

static measured measure_joined(const phy_ir_context *ir,
                               phy_ir_ref expression, const char *separator,
                               unsigned depth)
{
    const size_t count = phy_ir_child_count(ir, expression);
    if (count == 0u) {
        return text_measure("()");
    }

    int width = 0;
    int baseline = 0;
    int descent = 0;
    const int separator_width = phy_gfx_text_width(separator);
    const phy_ir_kind parent_kind = phy_ir_kind_of(ir, expression);
    for (size_t i = 0u; i < count; ++i) {
        const measured child = measure_child(
            ir, phy_ir_child(ir, expression, i), parent_kind, i, depth + 1u);
        if (!child.valid) {
            return invalid_measure();
        }
        if (i != 0u) {
            width = bounded_add(width, separator_width);
        }
        width = bounded_add(width, child.box.width);
        if (child.box.baseline > baseline) {
            baseline = child.box.baseline;
        }
        const int child_descent = child.box.height - child.box.baseline;
        if (child_descent > descent) {
            descent = child_descent;
        }
    }
    const measured result = {{width, bounded_add(baseline, descent), baseline},
                             true};
    return result;
}

static measured measure_function(const phy_ir_context *ir,
                                 phy_ir_ref expression, unsigned depth)
{
    const char *name = phy_ir_symbol_name(ir, phy_ir_head(ir, expression));
    if (name == NULL) {
        name = "?";
    } else {
        name = function_display_name(name);
    }
    int width = bounded_add(phy_gfx_text_width(name), phy_gfx_text_width("("));
    int baseline = PHY_GLYPH_HEIGHT - 1;
    int descent = 1;
    const size_t count = phy_ir_child_count(ir, expression);
    for (size_t i = 0u; i < count; ++i) {
        const measured child =
            measure_expr(ir, phy_ir_child(ir, expression, i), depth + 1u);
        if (!child.valid) {
            return invalid_measure();
        }
        if (i != 0u) {
            width = bounded_add(width, phy_gfx_text_width(", "));
        }
        width = bounded_add(width, child.box.width);
        if (child.box.baseline > baseline) {
            baseline = child.box.baseline;
        }
        const int child_descent = child.box.height - child.box.baseline;
        if (child_descent > descent) {
            descent = child_descent;
        }
    }
    width = bounded_add(width, phy_gfx_text_width(")"));
    const measured result = {{width, bounded_add(baseline, descent), baseline},
                             true};
    return result;
}

static measured measure_expr(const phy_ir_context *ir, phy_ir_ref expression,
                             unsigned depth)
{
    if (ir == NULL || expression == PHY_IR_NULL) {
        return invalid_measure();
    }
    if (depth >= MATH_MAX_DEPTH) {
        return text_measure("...");
    }

    char number[32];
    int64_t integer = 0;
    int64_t numerator = 0;
    int64_t denominator = 0;
    if (phy_ir_integer_value(ir, expression, &integer)) {
        format_i64(integer, number);
        return text_measure(number);
    }
    if (phy_ir_rational_value(ir, expression, &numerator, &denominator)) {
        char top[32];
        char bottom[32];
        format_i64(numerator, top);
        format_i64(denominator, bottom);
        const int content_width =
            phy_gfx_text_width(top) > phy_gfx_text_width(bottom)
                ? phy_gfx_text_width(top)
                : phy_gfx_text_width(bottom);
        const measured result = {
            {bounded_add(content_width, 4),
             PHY_GLYPH_HEIGHT * 2 + MATH_FRACTION_GAP * 2 + 1,
             PHY_GLYPH_HEIGHT + MATH_FRACTION_GAP},
            true,
        };
        return result;
    }

    const phy_ir_kind kind = phy_ir_kind_of(ir, expression);
    switch (kind) {
    case PHY_IR_SYMBOL: {
        const char *name =
            phy_ir_symbol_name(ir, phy_ir_head(ir, expression));
        return text_measure(name != NULL ? name : "?");
    }
    case PHY_IR_INDEX: {
        const char *name =
            phy_ir_symbol_name(ir, phy_ir_head(ir, expression));
        const phy_math_box name_box = text_box(name != NULL ? name : "?");
        phy_ir_variance variance = PHY_IR_INDEX_LOWER;
        (void)phy_ir_index_variance(ir, expression, &variance);
        const int shift =
            variance == PHY_IR_INDEX_UPPER ? MATH_SCRIPT_SHIFT : 0;
        const measured result = {
            {name_box.width, name_box.height + MATH_SCRIPT_SHIFT,
             name_box.baseline + shift},
            true,
        };
        return result;
    }
    case PHY_IR_ADD:
        return measure_joined(ir, expression, " + ", depth);
    case PHY_IR_MUL:
        return measure_joined(ir, expression, " ", depth);
    case PHY_IR_NCMUL:
        return measure_joined(ir, expression, " . ", depth);
    case PHY_IR_WEDGE:
        return measure_joined(ir, expression, " ^ ", depth);
    case PHY_IR_POW: {
        const measured base = measure_child(
            ir, phy_ir_child(ir, expression, 0u), PHY_IR_POW, 0u, depth + 1u);
        const measured exponent =
            measure_expr(ir, phy_ir_child(ir, expression, 1u), depth + 1u);
        if (!base.valid || !exponent.valid) {
            return invalid_measure();
        }
        const int baseline = base.box.baseline + MATH_SCRIPT_SHIFT;
        const int height =
            base.box.height + MATH_SCRIPT_SHIFT > exponent.box.height
                ? base.box.height + MATH_SCRIPT_SHIFT
                : exponent.box.height;
        const measured result = {
            {bounded_add(base.box.width, exponent.box.width), height, baseline},
            true,
        };
        return result;
    }
    case PHY_IR_EQUATION:
        return measure_joined(ir, expression, " = ", depth);
    case PHY_IR_FUNCTION:
    case PHY_IR_TENSOR:
    case PHY_IR_OPERATOR:
        return measure_function(ir, expression, depth);
    case PHY_IR_DERIVATIVE: {
        measured args = measure_joined(ir, expression, ", ", depth);
        if (!args.valid) {
            return invalid_measure();
        }
        const int wrapper = phy_gfx_text_width("D(") + phy_gfx_text_width(")");
        args.box.width = bounded_add(args.box.width, wrapper);
        return args;
    }
    case PHY_IR_ERROR: {
        phy_status status = PHY_ERR_CORRUPT_DOCUMENT;
        (void)phy_ir_error_status(ir, expression, &status);
        return text_measure(phy_status_name(status));
    }
    case PHY_IR_REAL:
        return text_measure("real");
    case PHY_IR_KIND_INVALID:
    case PHY_IR_KIND_COUNT:
    default:
        return text_measure("?");
    }
}

bool phy_math_measure(const phy_ir_context *ir, phy_ir_ref expression,
                      phy_math_box *out_box)
{
    if (out_box == NULL) {
        return false;
    }
    const measured result = measure_expr(ir, expression, 0u);
    if (!result.valid) {
        out_box->width = 0;
        out_box->height = 0;
        out_box->baseline = 0;
        return false;
    }
    *out_box = result.box;
    return true;
}

static void draw_expr(const phy_surface *surface, int x, int y,
                      const phy_ir_context *ir, phy_ir_ref expression,
                      uint16_t color, unsigned depth);

static void draw_child(const phy_surface *surface, int x, int y,
                       const phy_ir_context *ir, phy_ir_ref expression,
                       phy_ir_kind parent, size_t position, uint16_t color,
                       unsigned depth)
{
    const phy_ir_kind child_kind = phy_ir_kind_of(ir, expression);
    if (!child_needs_parentheses(parent, child_kind, position)) {
        draw_expr(surface, x, y, ir, expression, color, depth);
        return;
    }

    const measured wrapper =
        measure_child(ir, expression, parent, position, depth);
    const measured child = measure_expr(ir, expression, depth);
    const int text_y =
        y + wrapper.box.baseline - (PHY_GLYPH_HEIGHT - 1);
    int pen = phy_gfx_draw_text(surface, x, text_y, "(", color);
    draw_expr(surface, pen, y + wrapper.box.baseline - child.box.baseline, ir,
              expression, color, depth);
    pen += child.box.width;
    (void)phy_gfx_draw_text(surface, pen, text_y, ")", color);
}

static void draw_joined(const phy_surface *surface, int x, int y,
                        const phy_ir_context *ir, phy_ir_ref expression,
                        const char *separator, uint16_t color, unsigned depth)
{
    const measured parent = measure_expr(ir, expression, depth);
    if (!parent.valid) {
        return;
    }
    int pen = x;
    const size_t count = phy_ir_child_count(ir, expression);
    const phy_ir_kind parent_kind = phy_ir_kind_of(ir, expression);
    for (size_t i = 0u; i < count; ++i) {
        const phy_ir_ref ref = phy_ir_child(ir, expression, i);
        const measured child =
            measure_child(ir, ref, parent_kind, i, depth + 1u);
        if (i != 0u) {
            pen = phy_gfx_draw_text(surface, pen,
                                    y + parent.box.baseline -
                                        (PHY_GLYPH_HEIGHT - 1),
                                    separator, color);
        }
        draw_child(surface, pen,
                   y + parent.box.baseline - child.box.baseline, ir, ref,
                   parent_kind, i, color, depth + 1u);
        pen += child.box.width;
    }
}

static void draw_function(const phy_surface *surface, int x, int y,
                          const phy_ir_context *ir, phy_ir_ref expression,
                          uint16_t color, unsigned depth)
{
    const measured parent = measure_expr(ir, expression, depth);
    const char *name = phy_ir_symbol_name(ir, phy_ir_head(ir, expression));
    if (name == NULL) {
        name = "?";
    } else {
        name = function_display_name(name);
    }
    const int text_y =
        y + parent.box.baseline - (PHY_GLYPH_HEIGHT - 1);
    int pen = phy_gfx_draw_text(surface, x, text_y, name, color);
    pen = phy_gfx_draw_text(surface, pen, text_y, "(", color);
    const size_t count = phy_ir_child_count(ir, expression);
    for (size_t i = 0u; i < count; ++i) {
        const phy_ir_ref ref = phy_ir_child(ir, expression, i);
        const measured child = measure_expr(ir, ref, depth + 1u);
        if (i != 0u) {
            pen = phy_gfx_draw_text(surface, pen, text_y, ", ", color);
        }
        draw_expr(surface, pen, y + parent.box.baseline - child.box.baseline,
                  ir, ref, color, depth + 1u);
        pen += child.box.width;
    }
    (void)phy_gfx_draw_text(surface, pen, text_y, ")", color);
}

static void draw_expr(const phy_surface *surface, int x, int y,
                      const phy_ir_context *ir, phy_ir_ref expression,
                      uint16_t color, unsigned depth)
{
    if (depth >= MATH_MAX_DEPTH) {
        (void)phy_gfx_draw_text(surface, x, y, "...", color);
        return;
    }

    char number[32];
    int64_t integer = 0;
    int64_t numerator = 0;
    int64_t denominator = 0;
    if (phy_ir_integer_value(ir, expression, &integer)) {
        format_i64(integer, number);
        (void)phy_gfx_draw_text(surface, x, y, number, color);
        return;
    }
    if (phy_ir_rational_value(ir, expression, &numerator, &denominator)) {
        char top[32];
        char bottom[32];
        format_i64(numerator, top);
        format_i64(denominator, bottom);
        const measured box = measure_expr(ir, expression, depth);
        const int top_x = x + (box.box.width - phy_gfx_text_width(top)) / 2;
        const int bottom_x =
            x + (box.box.width - phy_gfx_text_width(bottom)) / 2;
        (void)phy_gfx_draw_text(surface, top_x, y, top, color);
        const int line_y = y + PHY_GLYPH_HEIGHT + MATH_FRACTION_GAP;
        phy_gfx_hline(surface, x, line_y, box.box.width, color);
        (void)phy_gfx_draw_text(surface, bottom_x,
                                line_y + MATH_FRACTION_GAP, bottom, color);
        return;
    }

    const phy_ir_kind kind = phy_ir_kind_of(ir, expression);
    switch (kind) {
    case PHY_IR_SYMBOL: {
        const char *name =
            phy_ir_symbol_name(ir, phy_ir_head(ir, expression));
        (void)phy_gfx_draw_text(surface, x, y, name != NULL ? name : "?",
                                color);
        break;
    }
    case PHY_IR_INDEX: {
        const char *name =
            phy_ir_symbol_name(ir, phy_ir_head(ir, expression));
        phy_ir_variance variance = PHY_IR_INDEX_LOWER;
        (void)phy_ir_index_variance(ir, expression, &variance);
        const int shift =
            variance == PHY_IR_INDEX_UPPER ? MATH_SCRIPT_SHIFT : 0;
        (void)phy_gfx_draw_text(surface, x, y + shift,
                                name != NULL ? name : "?", color);
        break;
    }
    case PHY_IR_ADD:
        draw_joined(surface, x, y, ir, expression, " + ", color, depth);
        break;
    case PHY_IR_MUL:
        draw_joined(surface, x, y, ir, expression, " ", color, depth);
        break;
    case PHY_IR_NCMUL:
        draw_joined(surface, x, y, ir, expression, " . ", color, depth);
        break;
    case PHY_IR_WEDGE:
        draw_joined(surface, x, y, ir, expression, " ^ ", color, depth);
        break;
    case PHY_IR_EQUATION:
        draw_joined(surface, x, y, ir, expression, " = ", color, depth);
        break;
    case PHY_IR_POW: {
        const phy_ir_ref base_ref = phy_ir_child(ir, expression, 0u);
        const phy_ir_ref exponent_ref = phy_ir_child(ir, expression, 1u);
        const measured base =
            measure_child(ir, base_ref, PHY_IR_POW, 0u, depth + 1u);
        draw_child(surface, x, y + MATH_SCRIPT_SHIFT, ir, base_ref,
                   PHY_IR_POW, 0u, color, depth + 1u);
        draw_expr(surface, x + base.box.width, y, ir, exponent_ref, color,
                  depth + 1u);
        break;
    }
    case PHY_IR_FUNCTION:
    case PHY_IR_TENSOR:
    case PHY_IR_OPERATOR:
        draw_function(surface, x, y, ir, expression, color, depth);
        break;
    case PHY_IR_DERIVATIVE:
        (void)phy_gfx_draw_text(surface, x, y, "D(", color);
        draw_joined(surface, x + phy_gfx_text_width("D("), y, ir, expression,
                    ", ", color, depth);
        {
            const measured box = measure_expr(ir, expression, depth);
            (void)phy_gfx_draw_text(
                surface, x + box.box.width - phy_gfx_text_width(")"),
                y + box.box.baseline - (PHY_GLYPH_HEIGHT - 1), ")", color);
        }
        break;
    case PHY_IR_ERROR: {
        phy_status status = PHY_ERR_CORRUPT_DOCUMENT;
        (void)phy_ir_error_status(ir, expression, &status);
        (void)phy_gfx_draw_text(surface, x, y, phy_status_name(status), color);
        break;
    }
    case PHY_IR_REAL:
        (void)phy_gfx_draw_text(surface, x, y, "real", color);
        break;
    case PHY_IR_KIND_INVALID:
    case PHY_IR_KIND_COUNT:
    default:
        (void)phy_gfx_draw_text(surface, x, y, "?", color);
        break;
    }
}

int phy_math_draw(const phy_surface *surface, int x, int y,
                  const phy_ir_context *ir, phy_ir_ref expression,
                  uint16_t color)
{
    phy_math_box box;
    if (surface == NULL || !phy_math_measure(ir, expression, &box)) {
        return x;
    }
    draw_expr(surface, x, y, ir, expression, color, 0u);
    return x + box.width;
}
