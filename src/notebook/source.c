#include "phy/source.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SOURCE_NAME_CAPACITY 32u
#define SOURCE_MAX_ARGUMENTS 16u

typedef struct {
    phy_ir_context *ir;
    const char *source;
    size_t length;
    size_t at;
    size_t error_at;
    phy_status status;
} source_reader;

typedef struct {
    const char *name;
    phy_source_operation operation;
    bool supported;
    bool derivative;
} command_descriptor;

static const command_descriptor kCommands[] = {
    {"Simplify", PHY_SOURCE_SIMPLIFY, true, false},
    {"FullSimplify", PHY_SOURCE_SIMPLIFY, true, false},
    {"Expand", PHY_SOURCE_EXPAND, true, false},
    {"Together", PHY_SOURCE_TOGETHER, true, false},
    {"Numerator", PHY_SOURCE_NUMERATOR, true, false},
    {"Denominator", PHY_SOURCE_DENOMINATOR, true, false},
    {"D", PHY_SOURCE_DIFFERENTIATE, true, true},
    {"Integrate", PHY_SOURCE_INTEGRATE, true, true},
    {"Set", PHY_SOURCE_ASSIGN, true, false},
    {"Clear", PHY_SOURCE_CLEAR, true, false},
    {"ClearAll", PHY_SOURCE_CLEAR, true, false},

    /*
     * Reserved Wolfram-style commands must fail honestly until their algebra
     * exists. Treating Factor[x] as an opaque mathematical function would look
     * successful while doing no factorization.
     */
    {"Cancel", PHY_SOURCE_SIMPLIFY, false, false},
    {"Factor", PHY_SOURCE_SIMPLIFY, false, false},
    {"Apart", PHY_SOURCE_SIMPLIFY, false, false},
    {"Limit", PHY_SOURCE_SIMPLIFY, false, false},
    {"Series", PHY_SOURCE_SIMPLIFY, false, false},
    {"Solve", PHY_SOURCE_SIMPLIFY, false, false},
    {"NSolve", PHY_SOURCE_SIMPLIFY, false, false},
    {"Reduce", PHY_SOURCE_SIMPLIFY, false, false},
    {"Refine", PHY_SOURCE_SIMPLIFY, false, false},
    {"TrigExpand", PHY_SOURCE_SIMPLIFY, false, false},
    {"TrigReduce", PHY_SOURCE_SIMPLIFY, false, false},
    {"TrigFactor", PHY_SOURCE_SIMPLIFY, false, false},
};

static void skip_space(source_reader *reader)
{
    while (reader->at < reader->length &&
           (unsigned char)reader->source[reader->at] <= (unsigned char)' ') {
        reader->at++;
    }
}

static char peek(source_reader *reader)
{
    skip_space(reader);
    return reader->at < reader->length ? reader->source[reader->at] : '\0';
}

static bool take(source_reader *reader, char expected)
{
    skip_space(reader);
    if (reader->at < reader->length &&
        reader->source[reader->at] == expected) {
        reader->at++;
        return true;
    }
    return false;
}

static void fail(source_reader *reader, phy_status status)
{
    if (reader->status == PHY_OK) {
        reader->status = status;
        reader->error_at = reader->at;
    }
}

static bool is_name_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool is_name_char(char c)
{
    return is_name_start(c) || (c >= '0' && c <= '9');
}

static bool read_name(source_reader *reader,
                      char out_name[SOURCE_NAME_CAPACITY])
{
    skip_space(reader);
    if (reader->at >= reader->length ||
        !is_name_start(reader->source[reader->at])) {
        return false;
    }
    size_t count = 0u;
    while (reader->at < reader->length &&
           is_name_char(reader->source[reader->at])) {
        if (count + 1u >= SOURCE_NAME_CAPACITY) {
            fail(reader, PHY_ERR_TERM_LIMIT);
            return false;
        }
        out_name[count++] = reader->source[reader->at++];
    }
    out_name[count] = '\0';
    return true;
}

static char ascii_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static bool name_equals(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (ascii_lower(*left) != ascii_lower(*right)) {
            return false;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static const command_descriptor *find_command(const char *name)
{
    for (size_t i = 0u; i < sizeof kCommands / sizeof kCommands[0]; ++i) {
        if (name_equals(name, kCommands[i].name)) {
            return &kCommands[i];
        }
    }
    return NULL;
}

static const char *canonical_function(const char *name)
{
    static const char *const known[] = {"sin", "cos", "tan", "exp", "log"};
    for (size_t i = 0u; i < sizeof known / sizeof known[0]; ++i) {
        if (name_equals(name, known[i])) {
            return known[i];
        }
    }
    return name;
}

/*
 * Heads that construct a typed physics object rather than a function
 * application. Everything here becomes a PHY_IR_OPERATOR whose head is the
 * spelling *in this table*, not the spelling the reader typed: name matching is
 * case-insensitive, so `manifold[...]` and `Manifold[...]` must reach the same
 * evaluator entry rather than intern two unrelated heads.
 *
 * The geometry, Lie, Yang-Mills, relativity, bounded QFT, SU(N)-colour and
 * query groups are evaluated by src/eval against the corresponding native
 * backend. Output-only heads such as ScalarField and BubbleIntegral share this
 * canonical operator vocabulary but are constructed by evaluated commands;
 * docs/EVALUATOR.md records that boundary.
 */
static const char *const kObjectHeads[] = {
    "Manifold",     "DifferentialForm", "Metric",        "VectorField",
    "ExteriorD",    "InteriorProduct",  "LieDerivative", "HodgeStar",
    "Volume",

    "LieGroup",     "LieAlgebra",       "Generator",     "LieElement",
    "LieBracket",   "StructureConstant", "Killing",

    "LieForm",      "GaugeConnection",  "CovariantD",    "FieldStrength",
    "GaugeVariation", "Bianchi",        "YangMillsLagrangian",
    "ColorComponent",

    "Curvature",    "InverseMetric",    "Christoffel",   "Riemann",
    "RiemannMixed", "Ricci",            "RicciScalar",   "Einstein",
    "Kretschmann",  "Weyl",             "WeylSquared",
    "GeodesicAcceleration",             "CovariantDerivative",

    "Component",    "Degree",           "Dimension",     "Rank",
    "ZeroQ",        "EquivalentQ",

    "ScalarField",  "Propagator",       "Vertex",        "TadpoleIntegral",
    "BubbleIntegral", "Phi4Lagrangian",  "Phi4EOM",       "Phi4Diagrams",
    "Phi4Renormalization", "Phi4Counterterm",
    "LorentzDot",   "MandelstamReduce", "DiracTrace",

    "SUNDelta",     "SUNF",              "SUND",          "SUNT",
    "SUNTrace",     "SUNCommutator",     "SUNDeltaContract",
    "SUNCF",        "SUNCA",             "SUNFComponent",
    "SUNExpandCasimirs", "SUNFundamentalCasimir",
    "SUNAdjointCasimir",
};

static const char *canonical_object_head(const char *name)
{
    for (size_t i = 0u; i < sizeof kObjectHeads / sizeof kObjectHeads[0];
         ++i) {
        if (name_equals(name, kObjectHeads[i])) {
            return kObjectHeads[i];
        }
    }
    return NULL;
}

static phy_ir_ref parse_expression(source_reader *reader);
static phy_ir_ref parse_unary(source_reader *reader);

static phy_ir_ref build_add(source_reader *reader, phy_ir_ref left,
                            phy_ir_ref right)
{
    const phy_ir_ref terms[2] = {left, right};
    const phy_ir_ref result = phy_ir_add(reader->ir, terms, 2u);
    if (result == PHY_IR_NULL) {
        fail(reader, phy_ir_last_error(reader->ir));
    }
    return result;
}

static phy_ir_ref build_mul(source_reader *reader, phy_ir_ref left,
                            phy_ir_ref right)
{
    const phy_ir_ref factors[2] = {left, right};
    const phy_ir_ref result = phy_ir_mul(reader->ir, factors, 2u);
    if (result == PHY_IR_NULL) {
        fail(reader, phy_ir_last_error(reader->ir));
    }
    return result;
}

static phy_ir_ref build_neg(source_reader *reader, phy_ir_ref value)
{
    int64_t integer = 0;
    if (phy_ir_integer_value(reader->ir, value, &integer) &&
        integer != INT64_MIN) {
        return phy_ir_integer(reader->ir, -integer);
    }
    int64_t numerator = 0;
    int64_t denominator = 0;
    if (phy_ir_rational_value(reader->ir, value, &numerator, &denominator) &&
        numerator != INT64_MIN) {
        return phy_ir_rational(reader->ir, -numerator, denominator);
    }
    const phy_ir_ref minus_one = phy_ir_integer(reader->ir, -1);
    return build_mul(reader, minus_one, value);
}

static phy_ir_ref parse_number(source_reader *reader)
{
    skip_space(reader);
    uint64_t whole = 0u;
    bool saw_digit = false;
    while (reader->at < reader->length &&
           reader->source[reader->at] >= '0' &&
           reader->source[reader->at] <= '9') {
        const unsigned digit =
            (unsigned)(reader->source[reader->at] - '0');
        if (whole > ((uint64_t)INT64_MAX - digit) / 10u) {
            fail(reader, PHY_ERR_OVERFLOW);
            return PHY_IR_NULL;
        }
        whole = whole * 10u + digit;
        reader->at++;
        saw_digit = true;
    }
    if (!saw_digit) {
        fail(reader, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }

    uint64_t fraction = 0u;
    uint64_t scale = 1u;
    if (reader->at < reader->length && reader->source[reader->at] == '.') {
        reader->at++;
        bool saw_fraction = false;
        while (reader->at < reader->length &&
               reader->source[reader->at] >= '0' &&
               reader->source[reader->at] <= '9') {
            const unsigned digit =
                (unsigned)(reader->source[reader->at] - '0');
            if (scale > (uint64_t)INT64_MAX / 10u ||
                fraction > ((uint64_t)INT64_MAX - digit) / 10u) {
                fail(reader, PHY_ERR_OVERFLOW);
                return PHY_IR_NULL;
            }
            scale *= 10u;
            fraction = fraction * 10u + digit;
            reader->at++;
            saw_fraction = true;
        }
        if (!saw_fraction) {
            fail(reader, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
    }
    if (whole > ((uint64_t)INT64_MAX - fraction) / scale) {
        fail(reader, PHY_ERR_OVERFLOW);
        return PHY_IR_NULL;
    }
    const uint64_t combined = whole * scale + fraction;
    const phy_ir_ref result =
        scale == 1u
            ? phy_ir_integer(reader->ir, (int64_t)combined)
            : phy_ir_rational(reader->ir, (int64_t)combined, (int64_t)scale);
    if (result == PHY_IR_NULL) {
        fail(reader, phy_ir_last_error(reader->ir));
    }
    return result;
}

static phy_ir_ref parse_primary(source_reader *reader)
{
    const char current = peek(reader);
    if (current >= '0' && current <= '9') {
        return parse_number(reader);
    }
    if (take(reader, '(')) {
        const phy_ir_ref inside = parse_expression(reader);
        if (!take(reader, ')')) {
            fail(reader, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
        return inside;
    }
    if (take(reader, '{')) {
        phy_ir_ref items[SOURCE_MAX_ARGUMENTS];
        size_t count = 0u;
        if (peek(reader) != '}') {
            for (;;) {
                if (count >= SOURCE_MAX_ARGUMENTS) {
                    fail(reader, PHY_ERR_TERM_LIMIT);
                    return PHY_IR_NULL;
                }
                items[count++] = parse_expression(reader);
                if (reader->status != PHY_OK) {
                    return PHY_IR_NULL;
                }
                if (!take(reader, ',')) {
                    break;
                }
            }
        }
        if (!take(reader, '}')) {
            fail(reader, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
        const phy_ir_symbol list = phy_ir_intern(reader->ir, "List");
        const phy_ir_ref result =
            phy_ir_function(reader->ir, list, items, count);
        if (result == PHY_IR_NULL) {
            fail(reader, phy_ir_last_error(reader->ir));
        }
        return result;
    }

    char name[SOURCE_NAME_CAPACITY];
    if (!read_name(reader, name)) {
        fail(reader, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }
    const char opener = peek(reader);
    if (opener != '[' && opener != '(') {
        const phy_ir_symbol symbol = phy_ir_intern(reader->ir, name);
        const phy_ir_ref result = phy_ir_symbol_ref(reader->ir, symbol);
        if (result == PHY_IR_NULL) {
            fail(reader, phy_ir_last_error(reader->ir));
        }
        return result;
    }

    reader->at++;
    const char closer = opener == '[' ? ']' : ')';
    phy_ir_ref arguments[SOURCE_MAX_ARGUMENTS];
    size_t count = 0u;
    if (peek(reader) != closer) {
        for (;;) {
            if (count >= SOURCE_MAX_ARGUMENTS) {
                fail(reader, PHY_ERR_TERM_LIMIT);
                return PHY_IR_NULL;
            }
            arguments[count++] = parse_expression(reader);
            if (reader->status != PHY_OK) {
                return PHY_IR_NULL;
            }
            if (!take(reader, ',')) {
                break;
            }
        }
    }
    if (!take(reader, closer)) {
        fail(reader, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }

    if (find_command(name) != NULL) {
        /*
         * Command evaluation is deliberately a top-level notebook action.
         * Until nested command scheduling exists, never turn one into an
         * inert ordinary function.
         */
        fail(reader, PHY_ERR_UNSUPPORTED);
        return PHY_IR_NULL;
    }

    phy_ir_ref result = PHY_IR_NULL;
    if (name_equals(name, "Plus")) {
        result = phy_ir_add(reader->ir, arguments, count);
    } else if (name_equals(name, "Times")) {
        result = phy_ir_mul(reader->ir, arguments, count);
    } else if (name_equals(name, "Power") && count == 2u) {
        result = phy_ir_pow(reader->ir, arguments[0], arguments[1]);
    } else if (name_equals(name, "Equal") && count == 2u) {
        result = phy_ir_equation(reader->ir, arguments[0], arguments[1]);
    } else if (name_equals(name, "Sqrt") && count == 1u) {
        const phy_ir_ref half = phy_ir_rational(reader->ir, 1, 2);
        result = phy_ir_pow(reader->ir, arguments[0], half);
    } else if (name_equals(name, "Rational") && count == 2u) {
        int64_t numerator = 0;
        int64_t denominator = 0;
        if (!phy_ir_integer_value(reader->ir, arguments[0], &numerator) ||
            !phy_ir_integer_value(reader->ir, arguments[1], &denominator)) {
            fail(reader, PHY_ERR_TYPE);
            return PHY_IR_NULL;
        }
        result = phy_ir_rational(reader->ir, numerator, denominator);
    } else if ((name_equals(name, "Up") || name_equals(name, "Down")) &&
               (count == 1u || count == 2u)) {
        if (phy_ir_kind_of(reader->ir, arguments[0]) != PHY_IR_SYMBOL ||
            (count == 2u &&
             phy_ir_kind_of(reader->ir, arguments[1]) != PHY_IR_SYMBOL)) {
            fail(reader, PHY_ERR_TYPE);
            return PHY_IR_NULL;
        }
        const phy_ir_symbol index_name =
            phy_ir_head(reader->ir, arguments[0]);
        const phy_ir_symbol space =
            count == 2u ? phy_ir_head(reader->ir, arguments[1])
                        : PHY_IR_NO_SYMBOL;
        result = phy_ir_index_in_space(
            reader->ir, index_name,
            name_equals(name, "Up") ? PHY_IR_INDEX_UPPER
                                    : PHY_IR_INDEX_LOWER,
            space);
    } else if ((name_equals(name, "Tensor") ||
                name_equals(name, "Operator")) &&
               count >= 1u) {
        if (phy_ir_kind_of(reader->ir, arguments[0]) != PHY_IR_SYMBOL) {
            fail(reader, PHY_ERR_TYPE);
            return PHY_IR_NULL;
        }
        const phy_ir_symbol head = phy_ir_head(reader->ir, arguments[0]);
        result = name_equals(name, "Tensor")
                     ? phy_ir_tensor(reader->ir, head, &arguments[1],
                                     count - 1u)
                     : phy_ir_operator(reader->ir, head, &arguments[1],
                                       count - 1u);
    } else if (name_equals(name, "NonCommutativeMultiply")) {
        result = phy_ir_ncmul(reader->ir, arguments, count);
    } else if (name_equals(name, "Wedge")) {
        result = phy_ir_wedge(reader->ir, arguments, count);
    } else if (name_equals(name, "Commutator")) {
        if (count != 2u) {
            fail(reader, PHY_ERR_TYPE);
            return PHY_IR_NULL;
        }
        const phy_ir_ref forward_factors[2] = {
            arguments[0], arguments[1]};
        const phy_ir_ref reverse_factors[2] = {
            arguments[1], arguments[0]};
        const phy_ir_ref forward =
            phy_ir_ncmul(reader->ir, forward_factors, 2u);
        const phy_ir_ref reverse =
            phy_ir_ncmul(reader->ir, reverse_factors, 2u);
        const phy_ir_ref minus_one = phy_ir_integer(reader->ir, -1);
        const phy_ir_ref negative_factors[2] = {minus_one, reverse};
        const phy_ir_ref negative =
            phy_ir_mul(reader->ir, negative_factors, 2u);
        const phy_ir_ref terms[2] = {forward, negative};
        result = phy_ir_add(reader->ir, terms, 2u);
    } else if (canonical_object_head(name) != NULL) {
        const phy_ir_symbol head =
            phy_ir_intern(reader->ir, canonical_object_head(name));
        result = phy_ir_operator(reader->ir, head, arguments, count);
    } else {
        const char *head_name = canonical_function(name);
        const phy_ir_symbol head = phy_ir_intern(reader->ir, head_name);
        result = phy_ir_function(reader->ir, head, arguments, count);
    }
    if (result == PHY_IR_NULL) {
        fail(reader, phy_ir_last_error(reader->ir));
    }
    return result;
}

static phy_ir_ref parse_power(source_reader *reader)
{
    phy_ir_ref base = parse_primary(reader);
    if (reader->status != PHY_OK) {
        return PHY_IR_NULL;
    }
    if (take(reader, '^')) {
        const phy_ir_ref exponent = parse_unary(reader);
        base = phy_ir_pow(reader->ir, base, exponent);
        if (base == PHY_IR_NULL) {
            fail(reader, phy_ir_last_error(reader->ir));
        }
    }
    return base;
}

static phy_ir_ref parse_unary(source_reader *reader)
{
    if (take(reader, '+')) {
        return parse_unary(reader);
    }
    if (take(reader, '-')) {
        const phy_ir_ref value = parse_unary(reader);
        return build_neg(reader, value);
    }
    return parse_power(reader);
}

static phy_ir_ref parse_product(source_reader *reader)
{
    phy_ir_ref left = parse_unary(reader);
    while (reader->status == PHY_OK) {
        if (take(reader, '*')) {
            left = build_mul(reader, left, parse_unary(reader));
        } else if (take(reader, '/')) {
            const phy_ir_ref right = parse_unary(reader);
            const phy_ir_ref minus_one = phy_ir_integer(reader->ir, -1);
            const phy_ir_ref reciprocal =
                phy_ir_pow(reader->ir, right, minus_one);
            if (reciprocal == PHY_IR_NULL) {
                fail(reader, phy_ir_last_error(reader->ir));
                return PHY_IR_NULL;
            }
            left = build_mul(reader, left, reciprocal);
        } else if (peek(reader) == '(' || peek(reader) == '{' ||
                   is_name_start(peek(reader)) ||
                   (peek(reader) >= '0' && peek(reader) <= '9')) {
            left = build_mul(reader, left, parse_unary(reader));
        } else {
            break;
        }
    }
    return left;
}

static phy_ir_ref parse_sum(source_reader *reader)
{
    phy_ir_ref left = parse_product(reader);
    while (reader->status == PHY_OK) {
        if (take(reader, '+')) {
            left = build_add(reader, left, parse_product(reader));
        } else if (take(reader, '-')) {
            const phy_ir_ref right = parse_product(reader);
            left = build_add(reader, left, build_neg(reader, right));
        } else {
            break;
        }
    }
    return left;
}

static phy_ir_ref parse_expression(source_reader *reader)
{
    phy_ir_ref left = parse_sum(reader);
    skip_space(reader);
    if (reader->at + 1u < reader->length &&
        reader->source[reader->at] == '=' &&
        reader->source[reader->at + 1u] == '=') {
        reader->at += 2u;
        const phy_ir_ref right = parse_sum(reader);
        left = phy_ir_equation(reader->ir, left, right);
        if (left == PHY_IR_NULL) {
            fail(reader, phy_ir_last_error(reader->ir));
        }
    }
    return left;
}

/*
 * `name` is bindable when it is neither a registered command nor a reserved
 * object head nor a known scalar function. Allowing `Sin = 2` would leave the
 * reader with a document in which `Sin[x]` means two different things depending
 * on cell order, which no diagnostic afterwards can untangle.
 */
static bool bindable_name(const char *name)
{
    return find_command(name) == NULL && canonical_object_head(name) == NULL &&
           canonical_function(name) == name;
}

/*
 * `name = rhs`, distinguished from the equation `name == rhs` by one character
 * of lookahead. Returns PHY_IR_NO_SYMBOL and restores the read position when
 * the input is not an assignment.
 */
static phy_ir_symbol begin_assignment(source_reader *reader)
{
    const size_t saved = reader->at;
    char name[SOURCE_NAME_CAPACITY];
    if (!read_name(reader, name)) {
        reader->at = saved;
        return PHY_IR_NO_SYMBOL;
    }
    skip_space(reader);
    if (reader->at >= reader->length || reader->source[reader->at] != '=' ||
        (reader->at + 1u < reader->length &&
         reader->source[reader->at + 1u] == '=')) {
        reader->at = saved;
        return PHY_IR_NO_SYMBOL;
    }
    if (!bindable_name(name)) {
        fail(reader, PHY_ERR_TYPE);
        return PHY_IR_NO_SYMBOL;
    }
    reader->at++;
    return phy_ir_intern(reader->ir, name);
}

static const command_descriptor *begin_command(source_reader *reader,
                                               char *out_closer)
{
    const size_t saved = reader->at;
    char name[SOURCE_NAME_CAPACITY];
    if (!read_name(reader, name)) {
        reader->at = saved;
        return NULL;
    }
    const command_descriptor *descriptor = find_command(name);
    if (descriptor == NULL) {
        reader->at = saved;
        return NULL;
    }
    const char opener = peek(reader);
    if (opener != '[' && opener != '(') {
        reader->at = saved;
        return NULL;
    }
    reader->at++;
    *out_closer = opener == '[' ? ']' : ')';
    if (!descriptor->supported) {
        fail(reader, PHY_ERR_UNSUPPORTED);
    }
    return descriptor;
}

phy_status phy_source_parse(phy_ir_context *ir, const char *source,
                            phy_source_command *out_command,
                            size_t *out_error_offset)
{
    if (ir == NULL || source == NULL || out_command == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_clear_error(ir);
    source_reader reader = {
        ir,
        source,
        strlen(source),
        0u,
        0u,
        PHY_OK,
    };
    phy_source_command command = {
        PHY_SOURCE_SIMPLIFY,
        PHY_IR_NULL,
        {PHY_IR_NULL},
        0u,
        PHY_IR_NO_SYMBOL,
    };

    char closer = '\0';
    const command_descriptor *descriptor = begin_command(&reader, &closer);
    if (descriptor != NULL && reader.status == PHY_OK &&
        descriptor->operation == PHY_SOURCE_CLEAR) {
        /*
         * `Clear[name]` names one binding; `ClearAll[]` and a bare `Clear[]`
         * clear the environment. The expression stays PHY_IR_NULL, which is
         * the one operation for which that is well-formed.
         */
        command.operation = PHY_SOURCE_CLEAR;
        if (peek(&reader) != closer) {
            char name[SOURCE_NAME_CAPACITY];
            if (!read_name(&reader, name)) {
                fail(&reader, PHY_ERR_PARSE);
            } else if (!bindable_name(name)) {
                fail(&reader, PHY_ERR_TYPE);
            } else {
                command.target = phy_ir_intern(ir, name);
            }
        }
        if (!take(&reader, closer)) {
            fail(&reader, PHY_ERR_PARSE);
        }
    } else if (descriptor != NULL && reader.status == PHY_OK &&
               descriptor->operation == PHY_SOURCE_ASSIGN) {
        /* `Set[name, rhs]`, the FullForm spelling of `name = rhs`. */
        command.operation = PHY_SOURCE_ASSIGN;
        char name[SOURCE_NAME_CAPACITY];
        if (!read_name(&reader, name)) {
            fail(&reader, PHY_ERR_PARSE);
        } else if (!bindable_name(name)) {
            fail(&reader, PHY_ERR_TYPE);
        } else if (!take(&reader, ',')) {
            fail(&reader, PHY_ERR_PARSE);
        } else {
            command.target = phy_ir_intern(ir, name);
            command.expression = parse_expression(&reader);
        }
        if (reader.status == PHY_OK && !take(&reader, closer)) {
            fail(&reader, PHY_ERR_PARSE);
        }
    } else if (descriptor != NULL && reader.status == PHY_OK) {
        command.operation = descriptor->operation;
        command.expression = parse_expression(&reader);
        if (descriptor->derivative) {
            while (reader.status == PHY_OK && take(&reader, ',')) {
                if (command.variable_count >= PHY_SOURCE_MAX_VARIABLES) {
                    fail(&reader, PHY_ERR_TERM_LIMIT);
                    break;
                }
                const phy_ir_ref variable = parse_expression(&reader);
                if (phy_ir_kind_of(ir, variable) != PHY_IR_SYMBOL) {
                    fail(&reader, PHY_ERR_TYPE);
                    break;
                }
                command.variables[command.variable_count++] = variable;
            }
            if (reader.status == PHY_OK && command.variable_count == 0u) {
                fail(&reader, PHY_ERR_PARSE);
            }
        }
        if (!take(&reader, closer)) {
            fail(&reader, PHY_ERR_PARSE);
        }
    } else if (descriptor == NULL) {
        const phy_ir_symbol target = begin_assignment(&reader);
        if (target != PHY_IR_NO_SYMBOL) {
            command.operation = PHY_SOURCE_ASSIGN;
            command.target = target;
        }
        if (reader.status == PHY_OK) {
            command.expression = parse_expression(&reader);
        }
    }

    skip_space(&reader);
    if (reader.status == PHY_OK && reader.at != reader.length) {
        fail(&reader, PHY_ERR_PARSE);
    }
    if (reader.status == PHY_OK &&
        command.operation != PHY_SOURCE_CLEAR &&
        command.expression == PHY_IR_NULL) {
        fail(&reader, phy_ir_last_error(ir));
    }
    if (out_error_offset != NULL) {
        *out_error_offset =
            reader.status == PHY_OK ? reader.length : reader.error_at;
    }
    if (reader.status != PHY_OK) {
        return reader.status;
    }
    *out_command = command;
    return PHY_OK;
}
