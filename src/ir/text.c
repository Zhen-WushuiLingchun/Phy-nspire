/*
 * Phy-nspire — backend-neutral IR serialization.
 *
 * The format is a small S-expression grammar documented in docs/IR.md. It
 * carries structure only, so reading text written by one context into another
 * rebuilds an identical graph.
 *
 * Two deliberate choices, both driven by what the device build costs:
 *
 * Integers are formatted by hand rather than through snprintf. Phase 0
 * measured two "%d" calls pulling newlib's float formatter into the binary --
 * 12.7 KB of a 53.8 KB image -- and the fix was bounded text helpers. Calling
 * snprintf here would undo that.
 *
 * Reals are written as their IEEE-754 bit pattern rather than as decimal or
 * as a hex float. A save format has to round-trip exactly, and the readable
 * alternatives reach for strtod and the float formatter, which is that same
 * 12.7 KB again. The human-facing form of an expression is the notebook's
 * two-dimensional rendering; this format is for storage and for tests.
 */
#include <string.h>

#include "ir_internal.h"

#define PHY_IR_TOKEN_MAX 256u

/* ------------------------------------------------------------------ writer */

/*
 * snprintf semantics without snprintf: `length` keeps counting past
 * `capacity`, so a truncated write still reports the size it needed.
 */
typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} writer;

static void emit(writer *w, const char *text, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (w->length < w->capacity) {
            w->buffer[w->length] = text[i];
        }
        w->length++;
    }
}

static void emit_cstr(writer *w, const char *text)
{
    emit(w, text, strlen(text));
}

static void emit_char(writer *w, char value)
{
    emit(w, &value, 1u);
}

static void emit_u64(writer *w, uint64_t value)
{
    char digits[20];
    size_t count = 0u;
    do {
        digits[count++] = (char)('0' + (int)(value % 10u));
        value /= 10u;
    } while (value != 0u);
    while (count > 0u) {
        emit_char(w, digits[--count]);
    }
}

static void emit_hex64(writer *w, uint64_t value)
{
    static const char kDigits[] = "0123456789abcdef";
    emit_cstr(w, "0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        emit_char(w, kDigits[(value >> shift) & 0xfu]);
    }
}

/* A name needs quoting when it would not read back as a single bare token. */
static bool name_is_bare(const char *name, size_t length)
{
    if (length == 0u) {
        return false;
    }
    /* A leading digit or minus would start a number instead. */
    if ((name[0] >= '0' && name[0] <= '9') || name[0] == '-') {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        const unsigned char c = (unsigned char)name[i];
        if (c <= ' ' || c == '(' || c == ')' || c == '|' || c == '\\') {
            return false;
        }
    }
    return true;
}

static void emit_name(writer *w, const phy_ir_context *ctx, phy_ir_symbol sym)
{
    const phy_ir_symbol_record *record = phy_ir_symbol_at(ctx, sym);
    if (record == NULL) {
        emit_cstr(w, "||");
        return;
    }
    const char *name = (const char *)ctx->strings.data + record->name_offset;
    const size_t length = record->name_length;

    /* Bytes above 0x7f stay bare: index names are routinely Greek, and
       quoting every UTF-8 name would make saved documents unreadable. */
    if (name_is_bare(name, length)) {
        emit(w, name, length);
        return;
    }
    emit_char(w, '|');
    for (size_t i = 0; i < length; i++) {
        if (name[i] == '|' || name[i] == '\\') {
            emit_char(w, '\\');
        }
        emit_char(w, name[i]);
    }
    emit_char(w, '|');
}

static void write_expr(writer *w, const phy_ir_context *ctx, phy_ir_ref ref)
{
    const phy_ir_node *node = phy_ir_node_at(ctx, ref);
    if (node == NULL) {
        emit_cstr(w, "()");
        return;
    }
    const phy_ir_kind kind = (phy_ir_kind)node->kind;

    switch (kind) {
    case PHY_IR_INTEGER: {
        phy_ir_exact_view exact;
        if (!phy_ir_exact_decimal_view(ctx, ref, &exact)) {
            emit_char(w, '0');
            return;
        }
        emit(w, exact.numerator, exact.numerator_length);
        return;
    }

    case PHY_IR_SYMBOL:
        emit_name(w, ctx, node->head);
        return;

    case PHY_IR_RATIONAL: {
        phy_ir_exact_view exact;
        emit_cstr(w, "(rat ");
        if (!phy_ir_exact_decimal_view(ctx, ref, &exact)) {
            emit_cstr(w, "0 1)");
            return;
        }
        emit(w, exact.numerator, exact.numerator_length);
        emit_char(w, ' ');
        emit(w, exact.denominator, exact.denominator_length);
        emit_char(w, ')');
        return;
    }

    case PHY_IR_REAL: {
        uint64_t bits;
        memcpy(&bits, &node->u.real, sizeof bits);
        emit_cstr(w, "(real ");
        emit_hex64(w, bits);
        emit_char(w, ')');
        return;
    }

    case PHY_IR_INDEX:
        emit_cstr(w, "(idx ");
        emit_name(w, ctx, node->head);
        emit_cstr(w, (node->aux == (uint8_t)PHY_IR_INDEX_UPPER) ? " up"
                                                                : " dn");
        if (node->u.index_space != PHY_IR_NO_SYMBOL) {
            emit_char(w, ' ');
            emit_name(w, ctx, node->u.index_space);
        }
        emit_char(w, ')');
        return;

    case PHY_IR_ERROR:
        emit_cstr(w, "(err ");
        emit_cstr(w, phy_status_name((phy_status)node->aux));
        emit_char(w, ')');
        return;

    default:
        break;
    }

    /* Every compound shares one shape: token, optional head, then children. */
    emit_char(w, '(');
    emit_cstr(w, phy_ir_kind_token(kind));
    if ((phy_ir_kind_flags(kind) & PHY_IR_KIND_HEADED) != 0u) {
        emit_char(w, ' ');
        emit_name(w, ctx, node->head);
    }
    const phy_ir_ref *children = phy_ir_children_of(ctx, node);
    for (uint16_t i = 0; i < node->child_count; i++) {
        emit_char(w, ' ');
        write_expr(w, ctx, children[i]);
    }
    emit_char(w, ')');
}

static phy_status writer_finish(writer *w, size_t *out_length)
{
    if (out_length != NULL) {
        *out_length = w->length;
    }
    if (w->length + 1u > w->capacity) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    w->buffer[w->length] = '\0';
    return PHY_OK;
}

phy_status phy_ir_write(const phy_ir_context *ctx, phy_ir_ref ref, char *buffer,
                        size_t capacity, size_t *out_length)
{
    if (ctx == NULL || (buffer == NULL && capacity != 0u)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (phy_ir_node_at(ctx, ref) == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    writer w = {buffer, capacity, 0u};
    write_expr(&w, ctx, ref);
    return writer_finish(&w, out_length);
}

/* --------------------------------------------------------- declaration text */

static const struct {
    const char *name;
    uint32_t bit;
} kAssumptionNames[] = {
    {"real", (uint32_t)PHY_IR_ASSUME_REAL},
    {"positive", (uint32_t)PHY_IR_ASSUME_POSITIVE},
    {"negative", (uint32_t)PHY_IR_ASSUME_NEGATIVE},
    {"integer", (uint32_t)PHY_IR_ASSUME_INTEGER},
    {"nonzero", (uint32_t)PHY_IR_ASSUME_NONZERO},
    {"constant", (uint32_t)PHY_IR_ASSUME_CONSTANT},
    {"noncommutative", (uint32_t)PHY_IR_ASSUME_NONCOMMUTATIVE},
};

#define ASSUMPTION_NAME_COUNT                                                  \
    (sizeof kAssumptionNames / sizeof kAssumptionNames[0])

/*
 * Symmetries are stored as a prepend list, so walking it yields reverse
 * declaration order. Emitting that directly would make write(read(text))
 * differ from text, which a document format cannot afford. Selecting the
 * next pair in slot order instead makes the output depend only on the set of
 * declarations. The lists are short -- a rank-4 tensor has at most six pairs
 * -- so the quadratic selection costs nothing.
 */
static bool next_symmetry_in_order(const phy_ir_context *ctx,
                                   uint32_t symmetry_head, uint32_t after_a,
                                   uint32_t after_b, bool have_previous,
                                   const phy_ir_symmetry_record **out_record)
{
    const phy_ir_symmetry_record *records =
        (const phy_ir_symmetry_record *)ctx->symmetries.data;
    const phy_ir_symmetry_record *best = NULL;

    for (uint32_t at = symmetry_head; at != 0u; at = records[at].next) {
        const phy_ir_symmetry_record *candidate = &records[at];
        if (have_previous) {
            const bool after = candidate->slot_a > after_a ||
                               (candidate->slot_a == after_a &&
                                candidate->slot_b > after_b);
            if (!after) {
                continue;
            }
        }
        if (best == NULL || candidate->slot_a < best->slot_a ||
            (candidate->slot_a == best->slot_a &&
             candidate->slot_b < best->slot_b)) {
            best = candidate;
        }
    }
    *out_record = best;
    return best != NULL;
}

phy_status phy_ir_write_declarations(const phy_ir_context *ctx, char *buffer,
                                     size_t capacity, size_t *out_length)
{
    if (ctx == NULL || (buffer == NULL && capacity != 0u)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    writer w = {buffer, capacity, 0u};

    const phy_ir_symbol_record *records =
        (const phy_ir_symbol_record *)ctx->symbols.data;
    for (size_t i = 1u; i < ctx->symbols.count; i++) {
        const phy_ir_symbol_record *record = &records[i];
        if (record->assumptions == 0u && record->symmetry_head == 0u) {
            continue; /* Nothing declared; the name alone needs no line. */
        }

        if (w.length != 0u) {
            emit_char(&w, '\n');
        }
        emit_cstr(&w, "(declare ");
        emit_name(&w, ctx, (phy_ir_symbol)i);

        for (size_t a = 0; a < ASSUMPTION_NAME_COUNT; a++) {
            if ((record->assumptions & kAssumptionNames[a].bit) != 0u) {
                emit_char(&w, ' ');
                emit_cstr(&w, kAssumptionNames[a].name);
            }
        }

        const phy_ir_symmetry_record *current = NULL;
        uint32_t previous_a = 0u;
        uint32_t previous_b = 0u;
        bool have_previous = false;
        while (next_symmetry_in_order(ctx, record->symmetry_head, previous_a,
                                      previous_b, have_previous, &current)) {
            emit_cstr(&w, (current->symmetry ==
                           (uint8_t)PHY_IR_SYMMETRY_ANTISYMMETRIC)
                              ? " (asym "
                              : " (sym ");
            emit_u64(&w, current->slot_a);
            emit_char(&w, ' ');
            emit_u64(&w, current->slot_b);
            emit_char(&w, ')');
            previous_a = current->slot_a;
            previous_b = current->slot_b;
            have_previous = true;
        }
        emit_char(&w, ')');
    }

    return writer_finish(&w, out_length);
}

/* ------------------------------------------------------------------ reader */

typedef struct {
    phy_ir_context *ctx;
    const char *text;
    size_t length;
    size_t at;
    uint32_t depth;
    phy_status error;
    /*
     * One shared operand stack for the whole parse. Each nested list records
     * its base and pops back to it, so nesting costs no separate allocation
     * and the peak is the deepest operand chain rather than the sum.
     */
    phy_pool stack;
} reader;

static phy_status fail(reader *r, phy_status status)
{
    if (r->error == PHY_OK) {
        r->error = (status == PHY_OK) ? PHY_ERR_PARSE : status;
    }
    return r->error;
}

static void skip_space(reader *r)
{
    while (r->at < r->length && (unsigned char)r->text[r->at] <= ' ') {
        r->at++;
    }
}

static bool peek(reader *r, char expected)
{
    skip_space(r);
    return r->at < r->length && r->text[r->at] == expected;
}

static bool accept(reader *r, char expected)
{
    if (!peek(r, expected)) {
        return false;
    }
    r->at++;
    return true;
}

static bool is_terminator(unsigned char c)
{
    return c <= ' ' || c == '(' || c == ')';
}

/*
 * Read one bare or quoted token into `out`, returning its length. Quoted
 * tokens are unescaped as they are copied.
 */
static size_t read_token(reader *r, char *out, size_t capacity)
{
    skip_space(r);
    size_t count = 0u;

    if (r->at < r->length && r->text[r->at] == '|') {
        r->at++;
        while (r->at < r->length && r->text[r->at] != '|') {
            char c = r->text[r->at++];
            if (c == '\\' && r->at < r->length) {
                c = r->text[r->at++];
            }
            if (count < capacity) {
                out[count] = c;
            }
            count++;
        }
        if (r->at >= r->length) {
            fail(r, PHY_ERR_PARSE);
            return 0u;
        }
        r->at++; /* closing bar */
    } else {
        while (r->at < r->length &&
               !is_terminator((unsigned char)r->text[r->at])) {
            if (count < capacity) {
                out[count] = r->text[r->at];
            }
            count++;
            r->at++;
        }
    }

    if (count == 0u || count > capacity) {
        fail(r, PHY_ERR_PARSE);
        return 0u;
    }
    return count;
}

static phy_ir_symbol read_symbol(reader *r)
{
    char token[PHY_IR_TOKEN_MAX];
    const size_t length = read_token(r, token, sizeof token);
    if (length == 0u) {
        return PHY_IR_NO_SYMBOL;
    }
    const phy_ir_symbol sym = phy_ir_intern_n(r->ctx, token, length);
    if (sym == PHY_IR_NO_SYMBOL) {
        fail(r, phy_ir_last_error(r->ctx));
    }
    return sym;
}

static bool read_bare_span(reader *r, const char **out_text,
                           size_t *out_length)
{
    skip_space(r);
    const size_t start = r->at;
    while (r->at < r->length &&
           !is_terminator((unsigned char)r->text[r->at])) {
        r->at++;
    }
    if (r->at == start) {
        fail(r, PHY_ERR_PARSE);
        return false;
    }
    *out_text = r->text + start;
    *out_length = r->at - start;
    return true;
}

static bool token_to_i64(const char *token, size_t length, int64_t *out_value)
{
    if (length == 0u) {
        return false;
    }
    size_t at = 0u;
    bool negative = false;
    if (token[0] == '-') {
        negative = true;
        at = 1u;
    }
    if (at >= length) {
        return false;
    }

    uint64_t value = 0u;
    for (; at < length; at++) {
        if (token[at] < '0' || token[at] > '9') {
            return false;
        }
        const uint64_t digit = (uint64_t)(token[at] - '0');
        if (value > ((uint64_t)-1 - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
    }

    const uint64_t limit =
        negative ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;
    if (value > limit) {
        return false;
    }
    if (negative) {
        *out_value =
            (value == (uint64_t)INT64_MAX + 1u) ? INT64_MIN : -(int64_t)value;
    } else {
        *out_value = (int64_t)value;
    }
    return true;
}

static bool token_is_decimal_integer(const char *token, size_t length)
{
    if (length == 0u) {
        return false;
    }
    size_t at = token[0] == '-' || token[0] == '+' ? 1u : 0u;
    if (at == length) {
        return false;
    }
    for (; at < length; ++at) {
        if (token[at] < '0' || token[at] > '9') {
            return false;
        }
    }
    return true;
}

static int64_t read_i64(reader *r)
{
    char token[32];
    const size_t length = read_token(r, token, sizeof token);
    int64_t value = 0;
    if (length == 0u || !token_to_i64(token, length, &value)) {
        fail(r, PHY_ERR_PARSE);
        return 0;
    }
    return value;
}

static bool read_u32(reader *r, uint32_t *out_value)
{
    const int64_t value = read_i64(r);
    if (r->error != PHY_OK || value < 0 || value > (int64_t)0xffffffff) {
        fail(r, PHY_ERR_PARSE);
        return false;
    }
    *out_value = (uint32_t)value;
    return true;
}

static bool token_to_hex64(const char *token, size_t length, uint64_t *out)
{
    if (length != 18u || token[0] != '0' || token[1] != 'x') {
        return false;
    }
    uint64_t value = 0u;
    for (size_t i = 2u; i < length; i++) {
        const char c = token[i];
        uint64_t digit;
        if (c >= '0' && c <= '9') {
            digit = (uint64_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (uint64_t)(c - 'a') + 10u;
        } else {
            return false;
        }
        value = (value << 4) | digit;
    }
    *out = value;
    return true;
}

static phy_ir_kind kind_for_token(const char *token, size_t length)
{
    for (unsigned k = (unsigned)PHY_IR_KIND_INVALID + 1u;
         k < (unsigned)PHY_IR_KIND_COUNT; k++) {
        const char *candidate = phy_ir_kind_token((phy_ir_kind)k);
        if (candidate != NULL && strlen(candidate) == length &&
            memcmp(candidate, token, length) == 0) {
            return (phy_ir_kind)k;
        }
    }
    return PHY_IR_KIND_INVALID;
}

static phy_ir_ref parse_expr(reader *r);

static bool stack_push(reader *r, phy_ir_ref ref)
{
    if (!phy_ir_pool_reserve(r->ctx, &r->stack, sizeof(phy_ir_ref),
                             r->stack.count + 1u)) {
        fail(r, phy_ir_last_error(r->ctx));
        return false;
    }
    ((phy_ir_ref *)r->stack.data)[r->stack.count++] = ref;
    return true;
}

/*
 * Parse operands onto the shared stack until the closing paren, then hand the
 * slice to the matching builder. The builders stage into the context's own
 * scratch pool, so passing a pointer into this stack is safe.
 */
static phy_ir_ref parse_compound(reader *r, phy_ir_kind kind)
{
    phy_ir_symbol head = PHY_IR_NO_SYMBOL;
    if ((phy_ir_kind_flags(kind) & PHY_IR_KIND_HEADED) != 0u) {
        head = read_symbol(r);
        if (head == PHY_IR_NO_SYMBOL) {
            return PHY_IR_NULL;
        }
    }

    const size_t base = r->stack.count;
    phy_ir_ref result = PHY_IR_NULL;

    while (!peek(r, ')')) {
        if (r->at >= r->length || r->error != PHY_OK) {
            fail(r, PHY_ERR_PARSE);
            goto done;
        }
        const phy_ir_ref child = parse_expr(r);
        if (child == PHY_IR_NULL) {
            goto done;
        }
        if (r->stack.count - base >= (size_t)r->ctx->limits.max_children) {
            fail(r, PHY_ERR_TERM_LIMIT);
            goto done;
        }
        if (!stack_push(r, child)) {
            goto done;
        }
    }

    {
        const size_t count = r->stack.count - base;
        phy_ir_ref *children = &((phy_ir_ref *)r->stack.data)[base];

        switch (kind) {
        case PHY_IR_ADD:
            result = phy_ir_add(r->ctx, children, count);
            break;
        case PHY_IR_MUL:
            result = phy_ir_mul(r->ctx, children, count);
            break;
        case PHY_IR_NCMUL:
            result = phy_ir_ncmul(r->ctx, children, count);
            break;
        case PHY_IR_WEDGE:
            result = phy_ir_wedge(r->ctx, children, count);
            break;
        case PHY_IR_POW:
            if (count != 2u) {
                fail(r, PHY_ERR_PARSE);
                goto done;
            }
            result = phy_ir_pow(r->ctx, children[0], children[1]);
            break;
        case PHY_IR_EQUATION:
            if (count != 2u) {
                fail(r, PHY_ERR_PARSE);
                goto done;
            }
            result = phy_ir_equation(r->ctx, children[0], children[1]);
            break;
        case PHY_IR_FUNCTION:
            result = phy_ir_function(r->ctx, head, children, count);
            break;
        case PHY_IR_TENSOR:
            result = phy_ir_tensor(r->ctx, head, children, count);
            break;
        case PHY_IR_OPERATOR:
            result = phy_ir_operator(r->ctx, head, children, count);
            break;
        case PHY_IR_DERIVATIVE:
            if (count < 2u) {
                fail(r, PHY_ERR_PARSE);
                goto done;
            }
            result =
                phy_ir_derivative(r->ctx, children[0], &children[1], count - 1u);
            break;
        default:
            fail(r, PHY_ERR_PARSE);
            goto done;
        }
    }

done:
    r->stack.count = base;
    return result;
}

static phy_ir_ref parse_list(reader *r)
{
    char token[PHY_IR_TOKEN_MAX];
    const size_t length = read_token(r, token, sizeof token);
    if (length == 0u) {
        return PHY_IR_NULL;
    }

    const phy_ir_kind kind = kind_for_token(token, length);
    if (kind == PHY_IR_KIND_INVALID) {
        fail(r, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }

    phy_ir_ref result = PHY_IR_NULL;
    switch (kind) {
    case PHY_IR_RATIONAL: {
        const char *numerator = NULL;
        const char *denominator = NULL;
        size_t numerator_length = 0u;
        size_t denominator_length = 0u;
        if (!read_bare_span(r, &numerator, &numerator_length) ||
            !read_bare_span(
                r, &denominator, &denominator_length)) {
            return PHY_IR_NULL;
        }
        if (!token_is_decimal_integer(numerator, numerator_length) ||
            !token_is_decimal_integer(
                denominator, denominator_length)) {
            fail(r, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
        result = phy_ir_rational_text_n(
            r->ctx, numerator, numerator_length, denominator,
            denominator_length);
        break;
    }

    case PHY_IR_REAL: {
        char bits_token[32];
        const size_t bits_length = read_token(r, bits_token, sizeof bits_token);
        uint64_t bits = 0u;
        if (bits_length == 0u ||
            !token_to_hex64(bits_token, bits_length, &bits)) {
            fail(r, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
        double value;
        memcpy(&value, &bits, sizeof value);
        result = phy_ir_real(r->ctx, value);
        break;
    }

    case PHY_IR_INDEX: {
        const phy_ir_symbol name = read_symbol(r);
        char variance[8];
        const size_t variance_length = read_token(r, variance, sizeof variance);
        if (r->error != PHY_OK || variance_length != 2u) {
            fail(r, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
        phy_ir_variance position;
        if (memcmp(variance, "up", 2u) == 0) {
            position = PHY_IR_INDEX_UPPER;
        } else if (memcmp(variance, "dn", 2u) == 0) {
            position = PHY_IR_INDEX_LOWER;
        } else {
            fail(r, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
        const phy_ir_symbol space =
            peek(r, ')') ? PHY_IR_NO_SYMBOL : read_symbol(r);
        if (r->error != PHY_OK) {
            return PHY_IR_NULL;
        }
        result = phy_ir_index_in_space(r->ctx, name, position, space);
        break;
    }

    case PHY_IR_ERROR: {
        char name[64];
        const size_t name_length = read_token(r, name, sizeof name);
        if (name_length == 0u) {
            return PHY_IR_NULL;
        }
        phy_status found = PHY_OK;
        for (unsigned s = 1u; s < (unsigned)PHY_STATUS_COUNT; s++) {
            const char *candidate = phy_status_name((phy_status)s);
            if (strlen(candidate) == name_length &&
                memcmp(candidate, name, name_length) == 0) {
                found = (phy_status)s;
                break;
            }
        }
        if (found == PHY_OK) {
            fail(r, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
        result = phy_ir_error(r->ctx, found);
        break;
    }

    default:
        result = parse_compound(r, kind);
        break;
    }

    if (result == PHY_IR_NULL) {
        fail(r, phy_ir_last_error(r->ctx));
        return PHY_IR_NULL;
    }
    if (!accept(r, ')')) {
        fail(r, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }
    return result;
}

static phy_ir_ref parse_expr(reader *r)
{
    if (r->error != PHY_OK) {
        return PHY_IR_NULL;
    }
    if (r->depth >= r->ctx->limits.max_depth) {
        fail(r, PHY_ERR_DEPTH_LIMIT);
        return PHY_IR_NULL;
    }

    skip_space(r);
    if (r->at >= r->length) {
        fail(r, PHY_ERR_PARSE);
        return PHY_IR_NULL;
    }

    if (accept(r, '(')) {
        r->depth++;
        const phy_ir_ref ref = parse_list(r);
        r->depth--;
        return ref;
    }

    /* A bare token is either an integer or a symbol. */
    const char first = r->text[r->at];
    if (first == '-' || (first >= '0' && first <= '9')) {
        const char *token = NULL;
        size_t length = 0u;
        if (!read_bare_span(r, &token, &length)) {
            return PHY_IR_NULL;
        }
        if (!token_is_decimal_integer(token, length)) {
            fail(r, PHY_ERR_PARSE);
            return PHY_IR_NULL;
        }
        const phy_ir_ref ref =
            phy_ir_integer_text_n(r->ctx, token, length);
        if (ref == PHY_IR_NULL) {
            fail(r, phy_ir_last_error(r->ctx));
        }
        return ref;
    }

    const phy_ir_symbol sym = read_symbol(r);
    if (sym == PHY_IR_NO_SYMBOL) {
        return PHY_IR_NULL;
    }
    const phy_ir_ref ref = phy_ir_symbol_ref(r->ctx, sym);
    if (ref == PHY_IR_NULL) {
        fail(r, phy_ir_last_error(r->ctx));
    }
    return ref;
}

static void reader_release(reader *r)
{
    phy_ir_pool_release(r->ctx, &r->stack, sizeof(phy_ir_ref));
}

phy_status phy_ir_read(phy_ir_context *ctx, const char *text,
                       phy_ir_ref *out_ref, size_t *out_error_offset)
{
    if (ctx == NULL || text == NULL || out_ref == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_ref = PHY_IR_NULL;

    reader r;
    memset(&r, 0, sizeof r);
    r.ctx = ctx;
    r.text = text;
    r.length = strlen(text);
    r.error = PHY_OK;

    const phy_ir_ref ref = parse_expr(&r);

    if (r.error == PHY_OK) {
        skip_space(&r);
        if (r.at != r.length) {
            r.error = PHY_ERR_PARSE;
        }
    }
    if (out_error_offset != NULL) {
        *out_error_offset = r.at;
    }
    reader_release(&r);

    if (r.error != PHY_OK) {
        return r.error;
    }
    *out_ref = ref;
    return PHY_OK;
}

/* ------------------------------------------------- declaration parsing */

static phy_status parse_declaration(reader *r)
{
    const phy_ir_symbol sym = read_symbol(r);
    if (sym == PHY_IR_NO_SYMBOL) {
        return fail(r, PHY_ERR_PARSE);
    }

    uint32_t assumptions = 0u;
    while (!peek(r, ')')) {
        if (r->at >= r->length) {
            return fail(r, PHY_ERR_PARSE);
        }

        if (accept(r, '(')) {
            char token[16];
            const size_t length = read_token(r, token, sizeof token);
            phy_ir_symmetry symmetry = PHY_IR_SYMMETRY_NONE;
            if (length == 3u && memcmp(token, "sym", 3u) == 0) {
                symmetry = PHY_IR_SYMMETRY_SYMMETRIC;
            } else if (length == 4u && memcmp(token, "asym", 4u) == 0) {
                symmetry = PHY_IR_SYMMETRY_ANTISYMMETRIC;
            } else {
                return fail(r, PHY_ERR_PARSE);
            }

            uint32_t slot_a = 0u;
            uint32_t slot_b = 0u;
            if (!read_u32(r, &slot_a) || !read_u32(r, &slot_b)) {
                return r->error;
            }
            if (!accept(r, ')')) {
                return fail(r, PHY_ERR_PARSE);
            }
            const phy_status status =
                phy_ir_declare_symmetry(r->ctx, sym, slot_a, slot_b, symmetry);
            if (status != PHY_OK) {
                return fail(r, status);
            }
            continue;
        }

        char token[32];
        const size_t length = read_token(r, token, sizeof token);
        if (length == 0u) {
            return r->error;
        }
        bool matched = false;
        for (size_t a = 0; a < ASSUMPTION_NAME_COUNT; a++) {
            if (strlen(kAssumptionNames[a].name) == length &&
                memcmp(kAssumptionNames[a].name, token, length) == 0) {
                assumptions |= kAssumptionNames[a].bit;
                matched = true;
                break;
            }
        }
        if (!matched) {
            return fail(r, PHY_ERR_PARSE);
        }
    }

    if (!accept(r, ')')) {
        return fail(r, PHY_ERR_PARSE);
    }
    if (assumptions != 0u) {
        const phy_status status = phy_ir_assume(r->ctx, sym, assumptions);
        if (status != PHY_OK) {
            return fail(r, status);
        }
    }
    return PHY_OK;
}

phy_status phy_ir_read_declarations(phy_ir_context *ctx, const char *text,
                                    size_t *out_error_offset)
{
    if (ctx == NULL || text == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }

    reader r;
    memset(&r, 0, sizeof r);
    r.ctx = ctx;
    r.text = text;
    r.length = strlen(text);
    r.error = PHY_OK;

    while (r.error == PHY_OK) {
        skip_space(&r);
        if (r.at >= r.length) {
            break;
        }
        if (!accept(&r, '(')) {
            fail(&r, PHY_ERR_PARSE);
            break;
        }
        char token[16];
        const size_t length = read_token(&r, token, sizeof token);
        if (length != 7u || memcmp(token, "declare", 7u) != 0) {
            fail(&r, PHY_ERR_PARSE);
            break;
        }
        (void)parse_declaration(&r);
    }

    if (out_error_offset != NULL) {
        *out_error_offset = r.at;
    }
    reader_release(&r);
    return r.error;
}
