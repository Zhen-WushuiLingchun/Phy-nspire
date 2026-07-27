/* See corpus_expr.h. Test-only; never linked into phy_core. */
#include "corpus_expr.h"

#include <stdbool.h>
#include <string.h>

/*
 * The functions the CAS knows how to differentiate and to see inside during
 * the zero decision. Anything else is refused; see the header.
 */
static const char *const kKnownFunctions[] = {"sin", "cos", "tan", "exp",
                                              "log"};

typedef struct {
    phy_cas *cas;
    const char *text;
    size_t length;
    size_t offset;
    phy_status status;
} phy_corpus_parser;

static void set_error(phy_corpus_parser *parser, phy_status status)
{
    if (parser->status == PHY_OK) {
        parser->status = status;
    }
}

static void skip_spaces(phy_corpus_parser *parser)
{
    while (parser->offset < parser->length &&
           parser->text[parser->offset] == ' ') {
        parser->offset++;
    }
}

static char current(const phy_corpus_parser *parser)
{
    return parser->offset < parser->length ? parser->text[parser->offset]
                                           : '\0';
}

static char lookahead(const phy_corpus_parser *parser, size_t distance)
{
    const size_t position = parser->offset + distance;
    return position < parser->length ? parser->text[position] : '\0';
}

static bool is_name_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_name_char(char c)
{
    return is_name_start(c) || (c >= '0' && c <= '9');
}

static phy_ir_ref parse_expression(phy_corpus_parser *parser, unsigned depth);

static phy_ir_ref parse_integer(phy_corpus_parser *parser)
{
    int64_t value = 0;
    const size_t start = parser->offset;
    while (parser->offset < parser->length) {
        const char c = parser->text[parser->offset];
        if (c < '0' || c > '9') {
            break;
        }
        /* The corpus has no literal near the int64 ceiling; refuse one. */
        if (value > (INT64_MAX - (c - '0')) / 10) {
            set_error(parser, PHY_ERR_OVERFLOW);
            return PHY_IR_NULL;
        }
        value = value * 10 + (c - '0');
        parser->offset++;
    }
    if (parser->offset == start) {
        set_error(parser, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }
    if (current(parser) == '.') {
        /* An inexact literal cannot be decided by phy_cas_is_zero(). */
        set_error(parser, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }
    phy_ir_ref result = PHY_IR_NULL;
    const phy_status status =
        phy_cas_number(parser->cas, value, 1, &result);
    if (status != PHY_OK) {
        set_error(parser, status);
        return PHY_IR_NULL;
    }
    return result;
}

static phy_ir_ref parse_name(phy_corpus_parser *parser, unsigned depth)
{
    const size_t start = parser->offset;
    while (parser->offset < parser->length &&
           is_name_char(parser->text[parser->offset])) {
        parser->offset++;
    }
    const size_t length = parser->offset - start;
    char name[64];
    if (length == 0u || length >= sizeof name) {
        set_error(parser, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }
    memcpy(name, parser->text + start, length);
    name[length] = '\0';

    phy_ir_context *ir = phy_cas_ir(parser->cas);
    skip_spaces(parser);
    if (current(parser) != '(') {
        const phy_ir_symbol symbol = phy_ir_intern(ir, name);
        const phy_ir_ref reference = phy_ir_symbol_ref(ir, symbol);
        if (reference == PHY_IR_NULL) {
            set_error(parser, PHY_ERR_OUT_OF_MEMORY);
        }
        return reference;
    }

    bool known = false;
    for (size_t index = 0u;
         index < sizeof kKnownFunctions / sizeof kKnownFunctions[0];
         ++index) {
        if (strcmp(name, kKnownFunctions[index]) == 0) {
            known = true;
            break;
        }
    }
    if (!known) {
        /* An opaque head would make every comparison against it UNKNOWN. */
        set_error(parser, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }

    parser->offset++; /* '(' */
    const phy_ir_ref argument = parse_expression(parser, depth + 1u);
    if (argument == PHY_IR_NULL) {
        return PHY_IR_NULL;
    }
    skip_spaces(parser);
    if (current(parser) != ')') {
        set_error(parser, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }
    parser->offset++;

    const phy_ir_symbol head = phy_ir_intern(ir, name);
    const phy_ir_ref applied = phy_ir_function(ir, head, &argument, 1u);
    if (applied == PHY_IR_NULL) {
        set_error(parser, PHY_ERR_OUT_OF_MEMORY);
        return PHY_IR_NULL;
    }
    phy_ir_ref simplified = PHY_IR_NULL;
    const phy_status status =
        phy_cas_simplify(parser->cas, applied, &simplified);
    if (status != PHY_OK) {
        set_error(parser, status);
        return PHY_IR_NULL;
    }
    return simplified;
}

static phy_ir_ref parse_atom(phy_corpus_parser *parser, unsigned depth)
{
    skip_spaces(parser);
    const char c = current(parser);
    if (c == '(') {
        parser->offset++;
        const phy_ir_ref inner = parse_expression(parser, depth + 1u);
        if (inner == PHY_IR_NULL) {
            return PHY_IR_NULL;
        }
        skip_spaces(parser);
        if (current(parser) != ')') {
            set_error(parser, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
        parser->offset++;
        return inner;
    }
    if (c >= '0' && c <= '9') {
        return parse_integer(parser);
    }
    if (is_name_start(c)) {
        return parse_name(parser, depth);
    }
    set_error(parser, PHY_ERR_PARSE);
    return PHY_IR_NULL;
}

static phy_ir_ref parse_unary(phy_corpus_parser *parser, unsigned depth);

/* Right associative, and binding tighter than unary minus. */
static phy_ir_ref parse_power(phy_corpus_parser *parser, unsigned depth)
{
    const phy_ir_ref base = parse_atom(parser, depth);
    if (base == PHY_IR_NULL) {
        return PHY_IR_NULL;
    }
    skip_spaces(parser);
    if (current(parser) != '*' || lookahead(parser, 1u) != '*') {
        return base;
    }
    parser->offset += 2u;
    const phy_ir_ref exponent = parse_unary(parser, depth + 1u);
    if (exponent == PHY_IR_NULL) {
        return PHY_IR_NULL;
    }
    phy_ir_ref result = PHY_IR_NULL;
    const phy_status status =
        phy_cas_pow(parser->cas, base, exponent, &result);
    if (status != PHY_OK) {
        set_error(parser, status);
        return PHY_IR_NULL;
    }
    return result;
}

static phy_ir_ref parse_unary(phy_corpus_parser *parser, unsigned depth)
{
    if (depth > 64u) {
        set_error(parser, PHY_ERR_DEPTH_LIMIT);
        return PHY_IR_NULL;
    }
    skip_spaces(parser);
    bool negate = false;
    for (;;) {
        const char c = current(parser);
        if (c == '-') {
            negate = !negate;
            parser->offset++;
            skip_spaces(parser);
            continue;
        }
        if (c == '+') {
            parser->offset++;
            skip_spaces(parser);
            continue;
        }
        break;
    }
    const phy_ir_ref value = parse_power(parser, depth);
    if (value == PHY_IR_NULL || !negate) {
        return value;
    }
    phy_ir_ref negated = PHY_IR_NULL;
    const phy_status status = phy_cas_neg(parser->cas, value, &negated);
    if (status != PHY_OK) {
        set_error(parser, status);
        return PHY_IR_NULL;
    }
    return negated;
}

static phy_ir_ref parse_term(phy_corpus_parser *parser, unsigned depth)
{
    phy_ir_ref accumulated = parse_unary(parser, depth);
    if (accumulated == PHY_IR_NULL) {
        return PHY_IR_NULL;
    }
    for (;;) {
        skip_spaces(parser);
        const char c = current(parser);
        /* '**' belongs to parse_power and must not be eaten here. */
        if ((c != '*' || lookahead(parser, 1u) == '*') && c != '/') {
            return accumulated;
        }
        parser->offset++;
        const phy_ir_ref operand = parse_unary(parser, depth + 1u);
        if (operand == PHY_IR_NULL) {
            return PHY_IR_NULL;
        }
        phy_ir_ref result = PHY_IR_NULL;
        phy_status status;
        if (c == '*') {
            const phy_ir_ref factors[2] = {accumulated, operand};
            status = phy_cas_mul(parser->cas, factors, 2u, &result);
        } else {
            status = phy_cas_div(parser->cas, accumulated, operand, &result);
        }
        if (status != PHY_OK) {
            set_error(parser, status);
            return PHY_IR_NULL;
        }
        accumulated = result;
    }
}

static phy_ir_ref parse_expression(phy_corpus_parser *parser, unsigned depth)
{
    if (depth > 64u) {
        set_error(parser, PHY_ERR_DEPTH_LIMIT);
        return PHY_IR_NULL;
    }
    phy_ir_ref accumulated = parse_term(parser, depth);
    if (accumulated == PHY_IR_NULL) {
        return PHY_IR_NULL;
    }
    for (;;) {
        skip_spaces(parser);
        const char c = current(parser);
        if (c != '+' && c != '-') {
            return accumulated;
        }
        parser->offset++;
        const phy_ir_ref operand = parse_term(parser, depth + 1u);
        if (operand == PHY_IR_NULL) {
            return PHY_IR_NULL;
        }
        phy_ir_ref result = PHY_IR_NULL;
        phy_status status;
        if (c == '+') {
            const phy_ir_ref terms[2] = {accumulated, operand};
            status = phy_cas_add(parser->cas, terms, 2u, &result);
        } else {
            status = phy_cas_sub(parser->cas, accumulated, operand, &result);
        }
        if (status != PHY_OK) {
            set_error(parser, status);
            return PHY_IR_NULL;
        }
        accumulated = result;
    }
}

phy_status phy_corpus_expr_parse(phy_cas *cas, const char *text,
                                 phy_ir_ref *out_ref,
                                 size_t *out_error_offset)
{
    if (cas == NULL || text == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_corpus_parser parser;
    parser.cas = cas;
    parser.text = text;
    parser.length = strlen(text);
    parser.offset = 0u;
    parser.status = PHY_OK;

    const phy_ir_ref result = parse_expression(&parser, 0u);
    if (result != PHY_IR_NULL) {
        skip_spaces(&parser);
        if (parser.offset != parser.length) {
            set_error(&parser, PHY_ERR_PARSE);
        }
    } else if (parser.status == PHY_OK) {
        parser.status = PHY_ERR_PARSE;
    }
    if (out_error_offset != NULL) {
        *out_error_offset = parser.offset;
    }
    if (parser.status != PHY_OK) {
        return parser.status;
    }
    *out_ref = result;
    return PHY_OK;
}
