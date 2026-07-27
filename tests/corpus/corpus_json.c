/* See corpus_json.h. Test-only; never linked into phy_core. */
#include "corpus_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct phy_json_member_entry {
    char *key; /* NULL for array elements */
    phy_json_value *value;
} phy_json_member_entry;

struct phy_json_value {
    phy_json_kind kind;
    long integer;
    bool boolean;
    char *string;
    size_t count;
    size_t capacity;
    phy_json_member_entry *entries;
};

struct phy_json_document {
    phy_json_value *root;
};

typedef struct {
    const char *text;
    size_t length;
    size_t offset;
    char *error;
    size_t error_capacity;
    bool failed;
} phy_json_parser;

static void fail(phy_json_parser *parser, const char *message)
{
    if (parser->failed) {
        return;
    }
    parser->failed = true;
    if (parser->error != NULL && parser->error_capacity != 0u) {
        (void)snprintf(parser->error, parser->error_capacity,
                       "at byte %lu: %s", (unsigned long)parser->offset,
                       message);
    }
}

static void value_destroy(phy_json_value *value)
{
    if (value == NULL) {
        return;
    }
    for (size_t index = 0u; index < value->count; ++index) {
        free(value->entries[index].key);
        value_destroy(value->entries[index].value);
    }
    free(value->entries);
    free(value->string);
    free(value);
}

static phy_json_value *value_create(phy_json_parser *parser,
                                    phy_json_kind kind)
{
    phy_json_value *value = calloc(1u, sizeof *value);
    if (value == NULL) {
        fail(parser, "out of memory");
        return NULL;
    }
    value->kind = kind;
    return value;
}

static bool append_entry(phy_json_parser *parser, phy_json_value *owner,
                         char *key, phy_json_value *child)
{
    if (owner->count == owner->capacity) {
        const size_t capacity =
            owner->capacity == 0u ? 8u : owner->capacity * 2u;
        phy_json_member_entry *entries =
            realloc(owner->entries, capacity * sizeof *entries);
        if (entries == NULL) {
            fail(parser, "out of memory");
            return false;
        }
        owner->entries = entries;
        owner->capacity = capacity;
    }
    owner->entries[owner->count].key = key;
    owner->entries[owner->count].value = child;
    owner->count++;
    return true;
}

static void skip_whitespace(phy_json_parser *parser)
{
    while (parser->offset < parser->length) {
        const char c = parser->text[parser->offset];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        parser->offset++;
    }
}

static bool peek(const phy_json_parser *parser, char *out_char)
{
    if (parser->offset >= parser->length) {
        return false;
    }
    *out_char = parser->text[parser->offset];
    return true;
}

static bool expect(phy_json_parser *parser, char expected)
{
    char c = '\0';
    if (!peek(parser, &c) || c != expected) {
        char message[48];
        (void)snprintf(message, sizeof message, "expected '%c'", expected);
        fail(parser, message);
        return false;
    }
    parser->offset++;
    return true;
}

static bool match_literal(phy_json_parser *parser, const char *literal)
{
    const size_t length = strlen(literal);
    if (parser->length - parser->offset < length ||
        memcmp(parser->text + parser->offset, literal, length) != 0) {
        return false;
    }
    parser->offset += length;
    return true;
}

/*
 * Strings. The unescaped form is never longer than the escaped one, so the
 * result is built into a buffer sized from the raw span.
 */
static char *parse_string(phy_json_parser *parser)
{
    if (!expect(parser, '"')) {
        return NULL;
    }
    const size_t start = parser->offset;
    char *out = malloc(parser->length - start + 1u);
    if (out == NULL) {
        fail(parser, "out of memory");
        return NULL;
    }
    size_t used = 0u;
    while (parser->offset < parser->length) {
        const char c = parser->text[parser->offset];
        if (c == '"') {
            parser->offset++;
            out[used] = '\0';
            return out;
        }
        if (c != '\\') {
            out[used++] = c;
            parser->offset++;
            continue;
        }
        parser->offset++;
        if (parser->offset >= parser->length) {
            break;
        }
        const char escape = parser->text[parser->offset++];
        switch (escape) {
        case '"': out[used++] = '"'; break;
        case '\\': out[used++] = '\\'; break;
        case '/': out[used++] = '/'; break;
        case 'b': out[used++] = '\b'; break;
        case 'f': out[used++] = '\f'; break;
        case 'n': out[used++] = '\n'; break;
        case 'r': out[used++] = '\r'; break;
        case 't': out[used++] = '\t'; break;
        default:
            free(out);
            fail(parser,
                 "unsupported string escape; \\u is refused deliberately");
            return NULL;
        }
    }
    free(out);
    fail(parser, "unterminated string");
    return NULL;
}

static phy_json_value *parse_value(phy_json_parser *parser, unsigned depth);

static phy_json_value *parse_number(phy_json_parser *parser)
{
    const size_t start = parser->offset;
    if (parser->offset < parser->length &&
        parser->text[parser->offset] == '-') {
        parser->offset++;
    }
    const size_t digits_start = parser->offset;
    while (parser->offset < parser->length &&
           parser->text[parser->offset] >= '0' &&
           parser->text[parser->offset] <= '9') {
        parser->offset++;
    }
    if (parser->offset == digits_start) {
        fail(parser, "expected a digit");
        return NULL;
    }
    if (parser->offset < parser->length) {
        const char c = parser->text[parser->offset];
        if (c == '.' || c == 'e' || c == 'E') {
            fail(parser,
                 "non-integer number; an exact corpus must not carry one");
            return NULL;
        }
    }

    char buffer[32];
    const size_t span = parser->offset - start;
    if (span >= sizeof buffer) {
        fail(parser, "integer literal too long");
        return NULL;
    }
    memcpy(buffer, parser->text + start, span);
    buffer[span] = '\0';

    phy_json_value *value = value_create(parser, PHY_JSON_INTEGER);
    if (value == NULL) {
        return NULL;
    }
    value->integer = strtol(buffer, NULL, 10);
    return value;
}

static phy_json_value *parse_array(phy_json_parser *parser, unsigned depth)
{
    if (!expect(parser, '[')) {
        return NULL;
    }
    phy_json_value *value = value_create(parser, PHY_JSON_ARRAY);
    if (value == NULL) {
        return NULL;
    }
    skip_whitespace(parser);
    char c = '\0';
    if (peek(parser, &c) && c == ']') {
        parser->offset++;
        return value;
    }
    for (;;) {
        skip_whitespace(parser);
        phy_json_value *element = parse_value(parser, depth + 1u);
        if (element == NULL) {
            value_destroy(value);
            return NULL;
        }
        if (!append_entry(parser, value, NULL, element)) {
            value_destroy(element);
            value_destroy(value);
            return NULL;
        }
        skip_whitespace(parser);
        if (!peek(parser, &c)) {
            fail(parser, "unterminated array");
            value_destroy(value);
            return NULL;
        }
        parser->offset++;
        if (c == ']') {
            return value;
        }
        if (c != ',') {
            fail(parser, "expected ',' or ']'");
            value_destroy(value);
            return NULL;
        }
    }
}

static phy_json_value *parse_object(phy_json_parser *parser, unsigned depth)
{
    if (!expect(parser, '{')) {
        return NULL;
    }
    phy_json_value *value = value_create(parser, PHY_JSON_OBJECT);
    if (value == NULL) {
        return NULL;
    }
    skip_whitespace(parser);
    char c = '\0';
    if (peek(parser, &c) && c == '}') {
        parser->offset++;
        return value;
    }
    for (;;) {
        skip_whitespace(parser);
        char *key = parse_string(parser);
        if (key == NULL) {
            value_destroy(value);
            return NULL;
        }
        for (size_t index = 0u; index < value->count; ++index) {
            if (strcmp(value->entries[index].key, key) == 0) {
                free(key);
                fail(parser, "duplicate object key");
                value_destroy(value);
                return NULL;
            }
        }
        skip_whitespace(parser);
        if (!expect(parser, ':')) {
            free(key);
            value_destroy(value);
            return NULL;
        }
        skip_whitespace(parser);
        phy_json_value *member = parse_value(parser, depth + 1u);
        if (member == NULL) {
            free(key);
            value_destroy(value);
            return NULL;
        }
        if (!append_entry(parser, value, key, member)) {
            free(key);
            value_destroy(member);
            value_destroy(value);
            return NULL;
        }
        skip_whitespace(parser);
        if (!peek(parser, &c)) {
            fail(parser, "unterminated object");
            value_destroy(value);
            return NULL;
        }
        parser->offset++;
        if (c == '}') {
            return value;
        }
        if (c != ',') {
            fail(parser, "expected ',' or '}'");
            value_destroy(value);
            return NULL;
        }
    }
}

static phy_json_value *parse_value(phy_json_parser *parser, unsigned depth)
{
    if (depth > 64u) {
        fail(parser, "nesting too deep");
        return NULL;
    }
    skip_whitespace(parser);
    char c = '\0';
    if (!peek(parser, &c)) {
        fail(parser, "unexpected end of input");
        return NULL;
    }
    if (c == '{') {
        return parse_object(parser, depth);
    }
    if (c == '[') {
        return parse_array(parser, depth);
    }
    if (c == '"') {
        char *text = parse_string(parser);
        if (text == NULL) {
            return NULL;
        }
        phy_json_value *value = value_create(parser, PHY_JSON_STRING);
        if (value == NULL) {
            free(text);
            return NULL;
        }
        value->string = text;
        return value;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return parse_number(parser);
    }
    if (match_literal(parser, "true") || match_literal(parser, "false")) {
        phy_json_value *value = value_create(parser, PHY_JSON_BOOL);
        if (value != NULL) {
            value->boolean = c == 't';
        }
        return value;
    }
    if (match_literal(parser, "null")) {
        return value_create(parser, PHY_JSON_NULL);
    }
    fail(parser, "unrecognized value");
    return NULL;
}

phy_json_document *phy_json_read_file(const char *path, char *error,
                                      size_t error_capacity)
{
    if (error != NULL && error_capacity != 0u) {
        error[0] = '\0';
    }
    if (path == NULL) {
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        if (error != NULL && error_capacity != 0u) {
            (void)snprintf(error, error_capacity, "cannot open %s", path);
        }
        return NULL;
    }

    size_t capacity = 1u << 16;
    size_t length = 0u;
    char *text = malloc(capacity);
    if (text == NULL) {
        (void)fclose(file);
        return NULL;
    }
    for (;;) {
        if (length == capacity) {
            char *grown = realloc(text, capacity * 2u);
            if (grown == NULL) {
                free(text);
                (void)fclose(file);
                return NULL;
            }
            text = grown;
            capacity *= 2u;
        }
        const size_t read = fread(text + length, 1u, capacity - length, file);
        length += read;
        if (read == 0u) {
            break;
        }
    }
    (void)fclose(file);

    phy_json_parser parser;
    parser.text = text;
    parser.length = length;
    parser.offset = 0u;
    parser.error = error;
    parser.error_capacity = error_capacity;
    parser.failed = false;

    phy_json_value *root = parse_value(&parser, 0u);
    if (root != NULL) {
        skip_whitespace(&parser);
        if (parser.offset != parser.length) {
            fail(&parser, "trailing content after the root value");
            value_destroy(root);
            root = NULL;
        }
    }
    free(text);
    if (root == NULL) {
        return NULL;
    }

    phy_json_document *document = calloc(1u, sizeof *document);
    if (document == NULL) {
        value_destroy(root);
        return NULL;
    }
    document->root = root;
    return document;
}

void phy_json_document_destroy(phy_json_document *document)
{
    if (document == NULL) {
        return;
    }
    value_destroy(document->root);
    free(document);
}

const phy_json_value *phy_json_root(const phy_json_document *document)
{
    return document != NULL ? document->root : NULL;
}

phy_json_kind phy_json_kind_of(const phy_json_value *value)
{
    return value != NULL ? value->kind : PHY_JSON_NULL;
}

size_t phy_json_count(const phy_json_value *value)
{
    if (value == NULL ||
        (value->kind != PHY_JSON_ARRAY && value->kind != PHY_JSON_OBJECT)) {
        return 0u;
    }
    return value->count;
}

const phy_json_value *phy_json_element(const phy_json_value *value,
                                       size_t index)
{
    if (value == NULL || value->kind != PHY_JSON_ARRAY ||
        index >= value->count) {
        return NULL;
    }
    return value->entries[index].value;
}

const char *phy_json_key_at(const phy_json_value *value, size_t index)
{
    if (value == NULL || value->kind != PHY_JSON_OBJECT ||
        index >= value->count) {
        return NULL;
    }
    return value->entries[index].key;
}

const phy_json_value *phy_json_value_at(const phy_json_value *value,
                                        size_t index)
{
    if (value == NULL || value->kind != PHY_JSON_OBJECT ||
        index >= value->count) {
        return NULL;
    }
    return value->entries[index].value;
}

const phy_json_value *phy_json_member(const phy_json_value *value,
                                      const char *key)
{
    if (value == NULL || value->kind != PHY_JSON_OBJECT || key == NULL) {
        return NULL;
    }
    for (size_t index = 0u; index < value->count; ++index) {
        if (strcmp(value->entries[index].key, key) == 0) {
            return value->entries[index].value;
        }
    }
    return NULL;
}

const char *phy_json_string(const phy_json_value *value)
{
    if (value == NULL || value->kind != PHY_JSON_STRING) {
        return NULL;
    }
    return value->string;
}

bool phy_json_integer(const phy_json_value *value, long *out_number)
{
    if (value == NULL || value->kind != PHY_JSON_INTEGER ||
        out_number == NULL) {
        return false;
    }
    *out_number = value->integer;
    return true;
}

bool phy_json_bool(const phy_json_value *value, bool *out_boolean)
{
    if (value == NULL || value->kind != PHY_JSON_BOOL || out_boolean == NULL) {
        return false;
    }
    *out_boolean = value->boolean;
    return true;
}
