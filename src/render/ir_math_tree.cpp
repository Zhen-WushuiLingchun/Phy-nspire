#include "ir_math_tree.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phy/phy.h"

namespace {

using nmarkdown::AtomClass;
using nmarkdown::MathNode;
using nmarkdown::MathNodeFlagHasSubscript;
using nmarkdown::MathNodeFlagHasSuperscript;
using nmarkdown::MathNodeId;
using nmarkdown::MathNodeKind;
using nmarkdown::MathAccent;
using nmarkdown::MathTree;
using nmarkdown::MathVariant;
using nmarkdown::kInvalidMathNode;
using nmarkdown::kMaximumMathBoxes;
using nmarkdown::kMaximumMathNesting;

constexpr int kPrecedenceEquation = 10;
constexpr int kPrecedenceAdd = 20;
constexpr int kPrecedenceProduct = 40;
constexpr int kPrecedencePower = 60;
constexpr int kPrecedenceAtom = 100;

std::string display_decimal(std::string_view value)
{
    if (!value.empty() && value.front() == '-') {
        std::string result(u8"−");
        result.append(value.data() + 1U, value.size() - 1U);
        return result;
    }
    return std::string(value);
}

std::string_view display_symbol(std::string_view name)
{
    struct Mapping {
        std::string_view name;
        std::string_view glyph;
    };
    static constexpr Mapping kMappings[] = {
        {"Alpha", u8"Α"},   {"Beta", u8"Β"},    {"Chi", u8"Χ"},
        {"Delta", u8"Δ"},   {"Epsilon", u8"Ε"}, {"Eta", u8"Η"},
        {"Gamma", u8"Γ"},   {"Infinity", u8"∞"}, {"Iota", u8"Ι"},
        {"Kappa", u8"Κ"},
        {"Lambda", u8"Λ"},  {"Mu", u8"Μ"},      {"Nu", u8"Ν"},
        {"Omega", u8"Ω"},   {"Omicron", u8"Ο"}, {"Phi", u8"Φ"},
        {"Pi", u8"π"},      {"Psi", u8"Ψ"},     {"Rho", u8"Ρ"},
        {"Sigma", u8"Σ"},   {"Tau", u8"Τ"},     {"Theta", u8"Θ"},
        {"Upsilon", u8"Υ"}, {"Xi", u8"Ξ"},      {"Zeta", u8"Ζ"},
        {"alpha", u8"α"},   {"beta", u8"β"},    {"chi", u8"χ"},
        {"delta", u8"δ"},   {"epsilon", u8"ε"}, {"eta", u8"η"},
        {"gamma", u8"γ"},   {"iota", u8"ι"},    {"kappa", u8"κ"},
        {"lambda", u8"λ"},  {"mu", u8"μ"},      {"nu", u8"ν"},
        {"omega", u8"ω"},   {"omicron", u8"ο"}, {"phi", u8"φ"},
        {"pi", u8"π"},      {"psi", u8"ψ"},     {"rho", u8"ρ"},
        {"sigma", u8"σ"},   {"tau", u8"τ"},     {"theta", u8"θ"},
        {"upsilon", u8"υ"}, {"xi", u8"ξ"},      {"zeta", u8"ζ"},
    };
    const auto found = std::lower_bound(
        std::begin(kMappings), std::end(kMappings), name,
        [](const Mapping& mapping, std::string_view value) {
            return mapping.name < value;
        });
    return found != std::end(kMappings) && found->name == name ? found->glyph
                                                               : name;
}

std::string_view display_function(std::string_view name)
{
    if (name == "asin") {
        return "arcsin";
    }
    if (name == "acos") {
        return "arccos";
    }
    if (name == "atan") {
        return "arctan";
    }
    if (name == "asinh") {
        return "arcsinh";
    }
    if (name == "acosh") {
        return "arccosh";
    }
    if (name == "atanh") {
        return "arctanh";
    }
    if (name == "gammafn") {
        return "Gamma";
    }
    if (name == "loggamma") {
        return "LogGamma";
    }
    return name;
}

class Builder {
public:
    Builder(const phy_ir_context *context, MathTree& tree)
        : context_(context), tree_(tree)
    {
    }

    bool run(phy_ir_ref expression)
    {
        tree_.clear();
        tree_.root = build(expression, 0U, 0);
        if (failed_ || tree_.root == kInvalidMathNode) {
            if (diagnostic_.empty()) {
                diagnostic_ = "could not convert typed IR to MathTree";
            }
            tree_.diagnostic = diagnostic_;
            tree_.recovered_error = true;
            return false;
        }
        return true;
    }

    const std::string& diagnostic() const
    {
        return diagnostic_;
    }

private:
    void fail(const char *message)
    {
        if (!failed_) {
            failed_ = true;
            diagnostic_ = message;
        }
    }

    std::pair<std::uint32_t, std::uint32_t> store(std::string_view value)
    {
        if (value.size() > UINT32_MAX ||
            tree_.strings.size() > UINT32_MAX - value.size()) {
            fail("formula text exceeds the MathTree string limit");
            return {0U, 0U};
        }
        const auto offset = static_cast<std::uint32_t>(tree_.strings.size());
        tree_.strings.append(value.data(), value.size());
        return {offset, static_cast<std::uint32_t>(value.size())};
    }

    MathNodeId add(MathNode node, const std::vector<MathNodeId>& children = {})
    {
        if (failed_) {
            return kInvalidMathNode;
        }
        if (tree_.nodes.size() >= kMaximumMathBoxes ||
            children.size() > UINT16_MAX ||
            tree_.children.size() > UINT32_MAX - children.size()) {
            fail("formula exceeds the MathTree node limit");
            return kInvalidMathNode;
        }
        for (MathNodeId child : children) {
            if (child == kInvalidMathNode) {
                fail("formula contains an invalid MathTree child");
                return kInvalidMathNode;
            }
        }
        node.first_child =
            static_cast<std::uint32_t>(tree_.children.size());
        node.child_count = static_cast<std::uint16_t>(children.size());
        tree_.children.insert(tree_.children.end(), children.begin(),
                              children.end());
        tree_.nodes.push_back(node);
        return static_cast<MathNodeId>(tree_.nodes.size() - 1U);
    }

    MathNodeId add(MathNode node,
                   std::initializer_list<MathNodeId> children)
    {
        return add(node, std::vector<MathNodeId>(children));
    }

    MathNodeId text(MathNodeKind kind, std::string_view value,
                    AtomClass atom_class = AtomClass::Ordinary)
    {
        MathNode node;
        node.kind = kind;
        node.atom_class = atom_class;
        const auto range = store(value);
        node.text_offset = range.first;
        node.text_length = range.second;
        return add(node);
    }

    MathNodeId row(const std::vector<MathNodeId>& children)
    {
        MathNode node;
        node.kind = MathNodeKind::Row;
        return add(node, children);
    }

    MathNodeId delimited(MathNodeId child,
                         std::string_view opening = "(",
                         std::string_view closing = ")")
    {
        MathNode node;
        node.kind = MathNodeKind::Delimited;
        node.atom_class = AtomClass::Inner;
        std::string delimiters(opening);
        delimiters.push_back('\0');
        delimiters.append(closing.data(), closing.size());
        const auto range = store(delimiters);
        node.text_offset = range.first;
        node.text_length = range.second;
        node.aux = static_cast<std::uint16_t>(opening.size());
        return add(node, {child});
    }

    MathNodeId styled(MathNodeId child, MathVariant variant)
    {
        MathNode node;
        node.kind = MathNodeKind::Styled;
        node.aux = static_cast<std::uint16_t>(variant);
        if (child < tree_.nodes.size()) {
            node.atom_class = tree_.nodes[child].atom_class;
        }
        return add(node, {child});
    }

    MathNodeId accented(MathNodeId child, MathAccent accent)
    {
        MathNode node;
        node.kind = MathNodeKind::Accent;
        node.aux = static_cast<std::uint16_t>(accent);
        return add(node, {child});
    }

    MathNodeId scripts(MathNodeId base, MathNodeId lower, MathNodeId upper)
    {
        if (lower == kInvalidMathNode && upper == kInvalidMathNode) {
            return base;
        }
        MathNode node;
        node.kind = MathNodeKind::Scripts;
        if (base < tree_.nodes.size()) {
            node.atom_class = tree_.nodes[base].atom_class;
        }
        std::vector<MathNodeId> children{base};
        if (lower != kInvalidMathNode) {
            node.flags |= MathNodeFlagHasSubscript;
            children.push_back(lower);
        }
        if (upper != kInvalidMathNode) {
            node.flags |= MathNodeFlagHasSuperscript;
            children.push_back(upper);
        }
        return add(node, children);
    }

    int precedence(phy_ir_ref expression) const
    {
        switch (phy_ir_kind_of(context_, expression)) {
        case PHY_IR_EQUATION:
            return kPrecedenceEquation;
        case PHY_IR_ADD:
            return kPrecedenceAdd;
        case PHY_IR_MUL:
        case PHY_IR_NCMUL:
        case PHY_IR_WEDGE:
            return kPrecedenceProduct;
        case PHY_IR_POW:
            return kPrecedencePower;
        default:
            return kPrecedenceAtom;
        }
    }

    MathNodeId child_with_precedence(phy_ir_ref child, unsigned depth,
                                     int parent_precedence,
                                     bool wrap_equal = false)
    {
        MathNodeId result = build(child, depth + 1U, parent_precedence);
        const int child_precedence = precedence(child);
        if (child_precedence < parent_precedence ||
            (wrap_equal && child_precedence == parent_precedence)) {
            result = delimited(result);
        }
        return result;
    }

    MathNodeId head_symbol(phy_ir_ref expression, bool roman)
    {
        const char *raw =
            phy_ir_symbol_name(context_, phy_ir_head(context_, expression));
        if (raw == nullptr) {
            fail("typed IR contains an invalid head symbol");
            return kInvalidMathNode;
        }
        const std::string_view name(raw);
        const std::string_view displayed = display_symbol(name);
        if (displayed == name && !roman && name.size() > 1U &&
            name[0] == 'd') {
            /*
             * Coordinate one-forms arrive as single symbols named d<coord>.
             * When the coordinate itself has a glyph, "dtheta" must read as
             * an upright d against the Greek letter, not as one long
             * italic identifier.
             */
            const std::string_view coordinate = name.substr(1U);
            const std::string_view glyph = display_symbol(coordinate);
            if (glyph != coordinate) {
                return row({styled(text(MathNodeKind::Symbol, "d"),
                                   MathVariant::Roman),
                            text(MathNodeKind::Symbol, glyph)});
            }
        }
        MathNodeId result = text(MathNodeKind::Symbol, displayed);
        if (roman && displayed == name) {
            result = styled(result, MathVariant::Roman);
        }
        return result;
    }

    MathNodeId function_head(phy_ir_ref expression)
    {
        const char *raw =
            phy_ir_symbol_name(context_, phy_ir_head(context_, expression));
        if (raw == nullptr) {
            fail("typed IR contains an invalid function head");
            return kInvalidMathNode;
        }
        return styled(
            text(MathNodeKind::Symbol, display_function(raw)),
            MathVariant::Roman);
    }

    /*
     * Juxtaposition works for single-letter factors: 2cos(x)sin(x) needs no
     * dots. A multi-letter symbol such as dphi or rs fuses illegibly with
     * whatever follows, so a commutative product separates it on both sides.
     */
    bool multi_letter_symbol(phy_ir_ref expression) const
    {
        const phy_ir_kind kind = phy_ir_kind_of(context_, expression);
        if (kind != PHY_IR_SYMBOL && kind != PHY_IR_INDEX) {
            return false;
        }
        const char *raw =
            phy_ir_symbol_name(context_, phy_ir_head(context_, expression));
        return raw != nullptr && raw[0] != '\0' && raw[1] != '\0';
    }

    MathNodeId arguments(phy_ir_ref expression, unsigned depth,
                         const std::vector<std::size_t>& positions)
    {
        std::vector<MathNodeId> items;
        items.reserve(positions.empty() ? 1U : positions.size() * 2U - 1U);
        for (std::size_t index = 0; index < positions.size(); ++index) {
            if (index != 0U) {
                items.push_back(
                    text(MathNodeKind::Symbol, ",", AtomClass::Punctuation));
            }
            items.push_back(build(
                phy_ir_child(context_, expression, positions[index]),
                depth + 1U, 0));
        }
        return delimited(row(items));
    }

    MathNodeId indexed_head(phy_ir_ref expression, unsigned depth,
                            bool roman, bool include_non_indices)
    {
        MathNodeId base = head_symbol(expression, roman);
        std::vector<MathNodeId> lower;
        std::vector<MathNodeId> upper;
        std::vector<std::size_t> arguments_positions;
        const std::size_t count =
            phy_ir_child_count(context_, expression);
        for (std::size_t index = 0; index < count; ++index) {
            const phy_ir_ref child =
                phy_ir_child(context_, expression, index);
            phy_ir_variance variance = PHY_IR_INDEX_LOWER;
            if (phy_ir_kind_of(context_, child) == PHY_IR_INDEX &&
                phy_ir_index_variance(context_, child, &variance)) {
                MathNodeId rendered = build(child, depth + 1U, 0);
                (variance == PHY_IR_INDEX_LOWER ? lower : upper)
                    .push_back(rendered);
            } else if (include_non_indices) {
                arguments_positions.push_back(index);
            }
        }
        base = scripts(base,
                       lower.empty() ? kInvalidMathNode : row(lower),
                       upper.empty() ? kInvalidMathNode : row(upper));
        if (!arguments_positions.empty()) {
            base = row({base,
                        arguments(expression, depth + 1U,
                                  arguments_positions)});
        }
        return base;
    }

    MathNodeId series_data(phy_ir_ref expression, unsigned depth)
    {
        if (phy_ir_child_count(context_, expression) != 5U ||
            phy_ir_kind_of(
                context_, phy_ir_child(context_, expression, 0U)) !=
                PHY_IR_SYMBOL ||
            (phy_ir_kind_of(
                 context_, phy_ir_child(context_, expression, 1U)) !=
                 PHY_IR_INTEGER &&
             phy_ir_kind_of(
                 context_, phy_ir_child(context_, expression, 1U)) !=
                 PHY_IR_RATIONAL) ||
            phy_ir_kind_of(
                context_, phy_ir_child(context_, expression, 2U)) !=
                PHY_IR_INTEGER ||
            phy_ir_kind_of(
                context_, phy_ir_child(context_, expression, 3U)) !=
                PHY_IR_INTEGER ||
            phy_ir_kind_of(
                context_, phy_ir_child(context_, expression, 4U)) !=
                PHY_IR_FUNCTION) {
            fail("SeriesData metadata is malformed");
            return kInvalidMathNode;
        }

        const phy_ir_ref variable_ref =
            phy_ir_child(context_, expression, 0U);
        const phy_ir_ref center_ref =
            phy_ir_child(context_, expression, 1U);
        const phy_ir_ref valuation_ref =
            phy_ir_child(context_, expression, 2U);
        const phy_ir_ref order_ref =
            phy_ir_child(context_, expression, 3U);
        const phy_ir_ref coefficients_ref =
            phy_ir_child(context_, expression, 4U);
        const char *coefficient_head = phy_ir_symbol_name(
            context_, phy_ir_head(context_, coefficients_ref));
        int64_t valuation = 0;
        int64_t order = 0;
        const std::size_t coefficient_count =
            phy_ir_child_count(context_, coefficients_ref);
        if (coefficient_head == nullptr ||
            std::string_view(coefficient_head) != "List" ||
            !phy_ir_integer_value(
                context_, valuation_ref, &valuation) ||
            !phy_ir_integer_value(context_, order_ref, &order) ||
            valuation > order || valuation < -32 || order > 64 ||
            coefficient_count !=
                static_cast<std::size_t>(order - valuation)) {
            fail("SeriesData coefficient metadata is inconsistent");
            return kInvalidMathNode;
        }

        MathNodeId shift = build(variable_ref, depth + 1U, 0);
        phy_ir_exact_view center{};
        const bool center_is_zero =
            phy_ir_exact_decimal_view(context_, center_ref, &center) &&
            std::string_view(center.numerator,
                             center.numerator_length) == "0";
        if (!center_is_zero) {
            std::vector<MathNodeId> shifted{shift};
            if (negative_lead(center_ref)) {
                shifted.push_back(text(
                    MathNodeKind::Symbol, "+", AtomClass::Binary));
                MathNodeId magnitude = number_magnitude(center_ref);
                if (magnitude == kInvalidMathNode) {
                    magnitude = text(MathNodeKind::Symbol, "1");
                }
                shifted.push_back(magnitude);
            } else {
                shifted.push_back(text(
                    MathNodeKind::Symbol, u8"−", AtomClass::Binary));
                shifted.push_back(build(center_ref, depth + 1U, 0));
            }
            shift = delimited(row(shifted));
        }

        std::vector<MathNodeId> polynomial;
        polynomial.reserve(coefficient_count * 3U + 1U);
        for (std::size_t index = 0U; index < coefficient_count;
             ++index) {
            const phy_ir_ref coefficient =
                phy_ir_child(context_, coefficients_ref, index);
            phy_ir_exact_view exact{};
            if (!phy_ir_exact_decimal_view(
                    context_, coefficient, &exact)) {
                fail("SeriesData contains a non-exact coefficient");
                return kInvalidMathNode;
            }
            const std::string_view numerator(
                exact.numerator, exact.numerator_length);
            if (numerator == "0") {
                continue;
            }
            const int64_t exponent =
                valuation + static_cast<int64_t>(index);
            const bool negative = negative_lead(coefficient);
            if (!polynomial.empty()) {
                polynomial.push_back(text(
                    MathNodeKind::Symbol,
                    negative ? u8"−" : "+", AtomClass::Binary));
            } else if (negative) {
                polynomial.push_back(
                    text(MathNodeKind::Symbol, u8"−"));
            }

            MathNodeId coefficient_node = kInvalidMathNode;
            if (negative) {
                coefficient_node = number_magnitude(coefficient);
            } else if (
                numerator != "1" ||
                std::string_view(
                    exact.denominator,
                    exact.denominator_length) != "1") {
                coefficient_node =
                    build(coefficient, depth + 1U, 0);
            }

            MathNodeId power = kInvalidMathNode;
            if (exponent != 0) {
                power = shift;
                if (exponent != 1) {
                    power = scripts(
                        shift, kInvalidMathNode,
                        text(
                            MathNodeKind::Symbol,
                            display_decimal(
                                std::to_string(exponent))));
                }
            }
            if (coefficient_node != kInvalidMathNode &&
                power != kInvalidMathNode) {
                polynomial.push_back(
                    row({coefficient_node, power}));
            } else if (coefficient_node != kInvalidMathNode) {
                polynomial.push_back(coefficient_node);
            } else if (power != kInvalidMathNode) {
                polynomial.push_back(power);
            } else {
                polynomial.push_back(
                    text(MathNodeKind::Symbol, "1"));
            }
        }
        if (polynomial.empty()) {
            polynomial.push_back(text(MathNodeKind::Symbol, "0"));
        }

        const MathNodeId powered = scripts(
            shift, kInvalidMathNode,
            build(order_ref, depth + 1U, 0));
        const MathNodeId order_term = row({
            styled(text(MathNodeKind::Symbol, "O"), MathVariant::Roman),
            delimited(powered),
        });

        std::vector<MathNodeId> result{
            row(polynomial),
            text(MathNodeKind::Symbol, "+", AtomClass::Binary),
            order_term,
        };
        return row(result);
    }

    /*
     * True when a sum term wears a minus sign a reader would move onto the
     * separator: a negative number, or a product led by one. INT64_MIN is
     * excluded because its magnitude does not exist in int64.
     */
    bool negative_lead(phy_ir_ref expression) const
    {
        const phy_ir_kind kind = phy_ir_kind_of(context_, expression);
        if (kind == PHY_IR_INTEGER || kind == PHY_IR_RATIONAL) {
            phy_ir_exact_view exact{};
            return phy_ir_exact_decimal_view(
                       context_, expression, &exact) &&
                   exact.numerator_length != 0U &&
                   exact.numerator[0] == '-';
        }
        if (kind == PHY_IR_MUL &&
            phy_ir_child_count(context_, expression) > 1U) {
            const phy_ir_ref first = phy_ir_child(context_, expression, 0U);
            const phy_ir_kind first_kind = phy_ir_kind_of(context_, first);
            return (first_kind == PHY_IR_INTEGER ||
                    first_kind == PHY_IR_RATIONAL) &&
                   negative_lead(first);
        }
        return false;
    }

    /*
     * The magnitude of a negative number as a rendered node, or
     * kInvalidMathNode for exactly -1 -- a coefficient of one is not
     * printed. Only called on numbers negative_lead() accepted.
     */
    MathNodeId number_magnitude(phy_ir_ref expression)
    {
        phy_ir_exact_view exact{};
        if (phy_ir_exact_decimal_view(context_, expression, &exact) &&
            exact.numerator_length > 1U && exact.numerator[0] == '-') {
            const std::string_view numerator(
                exact.numerator + 1U, exact.numerator_length - 1U);
            const std::string_view denominator(
                exact.denominator, exact.denominator_length);
            if (numerator == "1" && denominator == "1") {
                return kInvalidMathNode;
            }
            if (denominator == "1") {
                return text(MathNodeKind::Symbol, numerator);
            }
            MathNode fraction;
            fraction.kind = MathNodeKind::Fraction;
            fraction.atom_class = AtomClass::Inner;
            return add(fraction,
                       {text(MathNodeKind::Symbol, numerator),
                        text(MathNodeKind::Symbol, denominator)});
        }
        fail("stripped a sign from a term without a numeric lead");
        return kInvalidMathNode;
    }

    /* A sum term with its leading sign stripped: the separator drew it. */
    MathNodeId negated_term(phy_ir_ref term, unsigned depth)
    {
        if (phy_ir_kind_of(context_, term) == PHY_IR_MUL) {
            return product(term, depth, true);
        }
        const MathNodeId magnitude = number_magnitude(term);
        return magnitude == kInvalidMathNode
                   ? text(MathNodeKind::Symbol, "1")
                   : magnitude;
    }

    /*
     * A product row. `negated` renders the magnitude of the leading numeric
     * factor because the caller already drew the sign; an unnegated product
     * led by exactly -1 draws a unary minus instead of the literal
     * coefficient, so (* -1 cos sin) reads -cos(theta)sin(theta).
     */
    MathNodeId product(phy_ir_ref expression, unsigned depth, bool negated)
    {
        const std::size_t count = phy_ir_child_count(context_, expression);
        std::vector<MathNodeId> items;
        items.reserve(count * 2U + 1U);
        std::size_t start = 0U;
        bool leading_magnitude = false;
        if (negated) {
            const MathNodeId magnitude =
                number_magnitude(phy_ir_child(context_, expression, 0U));
            start = 1U;
            if (magnitude != kInvalidMathNode) {
                items.push_back(magnitude);
                leading_magnitude = true;
            }
        } else {
            phy_ir_exact_view exact{};
            const bool minus_one =
                count > 1U &&
                phy_ir_exact_decimal_view(
                    context_, phy_ir_child(context_, expression, 0U),
                    &exact) &&
                std::string_view(exact.numerator,
                                 exact.numerator_length) == "-1" &&
                std::string_view(exact.denominator,
                                 exact.denominator_length) == "1";
            if (minus_one) {
                items.push_back(text(MathNodeKind::Symbol, u8"−"));
                start = 1U;
            }
        }
        for (std::size_t index = start; index < count; ++index) {
            if (index != start || leading_magnitude) {
                std::string_view gap;
                if (multi_letter_symbol(phy_ir_child(context_, expression,
                                                     index - 1U)) ||
                    multi_letter_symbol(
                        phy_ir_child(context_, expression, index))) {
                    gap = u8"⋅";
                }
                if (!gap.empty()) {
                    items.push_back(text(MathNodeKind::Symbol, gap,
                                         AtomClass::Binary));
                }
            }
            items.push_back(child_with_precedence(
                phy_ir_child(context_, expression, index), depth,
                kPrecedenceProduct));
        }
        return row(items);
    }

    MathNodeId build(phy_ir_ref expression, unsigned depth,
                     int parent_precedence)
    {
        (void)parent_precedence;
        if (failed_) {
            return kInvalidMathNode;
        }
        if (context_ == nullptr || expression == PHY_IR_NULL ||
            phy_ir_kind_of(context_, expression) == PHY_IR_KIND_INVALID) {
            fail("typed IR reference is invalid");
            return kInvalidMathNode;
        }
        if (depth >= kMaximumMathNesting) {
            return text(MathNodeKind::Text, u8"…");
        }

        const phy_ir_kind kind = phy_ir_kind_of(context_, expression);
        switch (kind) {
        case PHY_IR_INTEGER: {
            phy_ir_exact_view exact{};
            if (!phy_ir_exact_decimal_view(
                    context_, expression, &exact)) {
                fail("integer payload is invalid");
                return kInvalidMathNode;
            }
            return text(
                MathNodeKind::Symbol,
                display_decimal(std::string_view(
                    exact.numerator, exact.numerator_length)));
        }
        case PHY_IR_RATIONAL: {
            phy_ir_exact_view exact{};
            if (!phy_ir_exact_decimal_view(
                    context_, expression, &exact)) {
                fail("rational payload is invalid");
                return kInvalidMathNode;
            }
            MathNode fraction;
            fraction.kind = MathNodeKind::Fraction;
            fraction.atom_class = AtomClass::Inner;
            return add(
                fraction,
                {text(MathNodeKind::Symbol,
                      display_decimal(std::string_view(
                          exact.numerator, exact.numerator_length))),
                 text(MathNodeKind::Symbol,
                      std::string_view(
                          exact.denominator,
                          exact.denominator_length))});
        }
        case PHY_IR_REAL:
            return styled(text(MathNodeKind::Text, "real"), MathVariant::Roman);
        case PHY_IR_SYMBOL:
        case PHY_IR_INDEX:
            return head_symbol(expression, false);
        case PHY_IR_ERROR: {
            phy_status status = PHY_ERR_BACKEND;
            if (!phy_ir_error_status(context_, expression, &status)) {
                fail("error payload is invalid");
                return kInvalidMathNode;
            }
            return styled(text(MathNodeKind::Text, phy_status_name(status)),
                          MathVariant::Roman);
        }
        case PHY_IR_MUL:
            return product(expression, depth, false);
        case PHY_IR_ADD:
        case PHY_IR_NCMUL:
        case PHY_IR_WEDGE: {
            const int current_precedence = precedence(expression);
            const std::string_view separator =
                kind == PHY_IR_ADD ? "+"
                : kind == PHY_IR_NCMUL ? u8"⋅"
                                       : u8"∧";
            std::vector<MathNodeId> items;
            const std::size_t count =
                phy_ir_child_count(context_, expression);
            items.reserve(count * 2U);
            for (std::size_t index = 0; index < count; ++index) {
                const phy_ir_ref term =
                    phy_ir_child(context_, expression, index);
                if (index != 0U && kind == PHY_IR_ADD &&
                    negative_lead(term)) {
                    /* a + (-b) reads a − b, the way a reader writes it. */
                    items.push_back(text(MathNodeKind::Symbol, u8"−",
                                         AtomClass::Binary));
                    items.push_back(negated_term(term, depth));
                    continue;
                }
                if (index != 0U) {
                    items.push_back(text(MathNodeKind::Symbol, separator,
                                         AtomClass::Binary));
                }
                items.push_back(child_with_precedence(
                    term, depth, current_precedence));
            }
            return row(items);
        }
        case PHY_IR_POW: {
            const phy_ir_ref base_ref =
                phy_ir_child(context_, expression, 0U);
            const phy_ir_ref exponent_ref =
                phy_ir_child(context_, expression, 1U);
            MathNodeId base = child_with_precedence(
                base_ref, depth, kPrecedencePower, true);
            MathNodeId exponent = build(exponent_ref, depth + 1U, 0);
            return scripts(base, kInvalidMathNode, exponent);
        }
        case PHY_IR_EQUATION: {
            std::vector<MathNodeId> items;
            items.push_back(child_with_precedence(
                phy_ir_child(context_, expression, 0U), depth,
                kPrecedenceEquation));
            items.push_back(
                text(MathNodeKind::Symbol, "=", AtomClass::Relation));
            items.push_back(child_with_precedence(
                phy_ir_child(context_, expression, 1U), depth,
                kPrecedenceEquation));
            return row(items);
        }
        case PHY_IR_FUNCTION: {
            const char *raw_head = phy_ir_symbol_name(
                context_, phy_ir_head(context_, expression));
            const std::string_view head_name =
                raw_head == nullptr ? std::string_view()
                                    : std::string_view(raw_head);
            const std::size_t count =
                phy_ir_child_count(context_, expression);
            if (head_name == "List") {
                std::vector<MathNodeId> items;
                items.reserve(count == 0U ? 1U : count * 2U - 1U);
                for (std::size_t index = 0U; index < count; ++index) {
                    if (index != 0U) {
                        items.push_back(text(
                            MathNodeKind::Symbol, ",",
                            AtomClass::Punctuation));
                    }
                    items.push_back(build(
                        phy_ir_child(context_, expression, index),
                        depth + 1U, 0));
                }
                return delimited(row(items), "{", "}");
            }
            if (head_name == "Rule" && count == 2U) {
                return row({
                    build(
                        phy_ir_child(context_, expression, 0U),
                        depth + 1U, 0),
                    text(
                        MathNodeKind::Symbol, u8"→",
                        AtomClass::Relation),
                    build(
                        phy_ir_child(context_, expression, 1U),
                        depth + 1U, 0),
                });
            }
            if (head_name == "Abs" && count == 1U) {
                return delimited(
                    build(
                        phy_ir_child(context_, expression, 0U),
                        depth + 1U, 0),
                    "|", "|");
            }
            if (head_name == "Conjugate" && count == 1U) {
                return accented(
                    build(
                        phy_ir_child(context_, expression, 0U),
                        depth + 1U, 0),
                    MathAccent::Overline);
            }
            MathNodeId head = function_head(expression);
            std::vector<std::size_t> positions;
            positions.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                positions.push_back(index);
            }
            return row({head, arguments(expression, depth + 1U, positions)});
        }
        case PHY_IR_TENSOR:
            return indexed_head(expression, depth, false, false);
        case PHY_IR_OPERATOR: {
            const char *head = phy_ir_symbol_name(
                context_, phy_ir_head(context_, expression));
            if (head != nullptr &&
                std::string_view(head) == "SeriesData") {
                return series_data(expression, depth);
            }
            return indexed_head(expression, depth, true, true);
        }
        case PHY_IR_DERIVATIVE: {
            std::vector<MathNodeId> items;
            items.push_back(styled(text(MathNodeKind::Symbol, "D"),
                                   MathVariant::Roman));
            std::vector<std::size_t> positions;
            const std::size_t count =
                phy_ir_child_count(context_, expression);
            positions.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                positions.push_back(index);
            }
            items.push_back(arguments(expression, depth + 1U, positions));
            return row(items);
        }
        case PHY_IR_KIND_INVALID:
        case PHY_IR_KIND_COUNT:
            break;
        }
        fail("typed IR contains an unsupported node kind");
        return kInvalidMathNode;
    }

    const phy_ir_context *context_ = nullptr;
    MathTree& tree_;
    bool failed_ = false;
    std::string diagnostic_;
};

}  // namespace

bool phy_build_ir_math_tree(const phy_ir_context *context,
                            phy_ir_ref expression,
                            nmarkdown::MathTree& tree,
                            std::string& diagnostic)
{
    Builder builder(context, tree);
    if (!builder.run(expression)) {
        diagnostic = builder.diagnostic();
        return false;
    }
    diagnostic.clear();
    return true;
}
