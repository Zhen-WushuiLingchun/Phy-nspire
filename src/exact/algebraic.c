/*
 * Certified real algebraic numbers: primitive square-free Z[x] defining
 * polynomials, exact rational isolating intervals, and Sturm root counts.
 *
 * The implementation intentionally uses Q[x] Euclidean remainders. It is not
 * the eventual high-performance subresultant engine for general Factor; it is
 * a small, auditable certificate layer whose coefficient arithmetic is already
 * arbitrary precision and resource bounded.
 */
#include <string.h>

#include "phy/algebraic.h"
#include "phy/platform.h"

#define PHY_ALGEBRAIC_CONTEXT_MAGIC 0x414c4743u /* ALGC */
#define PHY_REAL_ALGEBRAIC_MAGIC 0x52414c47u    /* RALG */
#define PHY_ALGEBRAIC_MAX_DEGREE 256u

struct phy_algebraic_context {
    phy_algebraic_limits limits;
    phy_exact_context *exact;
    size_t metadata_bytes;
    uint32_t steps;
    uint64_t total_steps;
    unsigned call_depth;
    bool (*cancelled)(void *user);
    void *cancel_user;
    phy_real_algebraic *values;
    size_t value_count;
    uint32_t magic;
};

struct phy_real_algebraic {
    phy_algebraic_context *context;
    phy_bigint *coefficients;
    size_t coefficient_count;
    size_t coefficient_capacity;
    phy_bigrat lower;
    phy_bigrat upper;
    bool lower_initialized;
    bool upper_initialized;
    bool rational;
    bool linked;
    phy_real_algebraic *previous;
    phy_real_algebraic *next;
    uint32_t magic;
};

typedef struct {
    phy_bigrat *coefficients;
    size_t count;
    size_t capacity;
} rational_polynomial;

typedef struct {
    rational_polynomial *items;
    size_t count;
    size_t capacity;
} sturm_chain;

static bool context_valid(const phy_algebraic_context *context)
{
    return context != NULL &&
           context->magic == PHY_ALGEBRAIC_CONTEXT_MAGIC;
}

static bool value_valid_handle(const phy_real_algebraic *value)
{
    return value != NULL &&
           value->magic == PHY_REAL_ALGEBRAIC_MAGIC &&
           context_valid(value->context);
}

static bool checked_multiply(size_t left, size_t right, size_t *out_product)
{
    if (left != 0u && right > (size_t)-1 / left) {
        return false;
    }
    *out_product = left * right;
    return true;
}

static phy_status metadata_allocate(phy_algebraic_context *context,
                                    size_t bytes, void **out_pointer)
{
    if (!context_valid(context) || bytes == 0u || out_pointer == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_pointer = NULL;
    if (bytes > context->limits.max_metadata_bytes -
                    context->metadata_bytes) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    void *pointer = phy_alloc(bytes);
    if (pointer == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    context->metadata_bytes += bytes;
    memset(pointer, 0, bytes);
    *out_pointer = pointer;
    return PHY_OK;
}

static void metadata_free(phy_algebraic_context *context, void *pointer,
                          size_t bytes)
{
    if (!context_valid(context) || pointer == NULL || bytes == 0u) {
        return;
    }
    phy_free(pointer, bytes);
    context->metadata_bytes -=
        bytes <= context->metadata_bytes ? bytes : context->metadata_bytes;
}

static phy_status call_begin(phy_algebraic_context *context)
{
    if (!context_valid(context)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (context->call_depth == 0u) {
        context->steps = 0u;
    }
    context->call_depth++;
    return PHY_OK;
}

static phy_status call_end(phy_algebraic_context *context,
                           phy_status status)
{
    if (context_valid(context) && context->call_depth != 0u) {
        context->call_depth--;
    }
    return status;
}

static phy_status algebraic_step(phy_algebraic_context *context,
                                 uint32_t amount)
{
    if (!context_valid(context)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (amount > context->limits.max_steps - context->steps) {
        return PHY_ERR_TIMEOUT;
    }
    context->steps += amount;
    context->total_steps += amount;
    if (context->cancelled != NULL &&
        context->cancelled(context->cancel_user)) {
        return PHY_ERR_INTERRUPTED;
    }
    return PHY_OK;
}

void phy_algebraic_limits_defaults(phy_algebraic_limits *out_limits)
{
    if (out_limits == NULL) {
        return;
    }
    phy_exact_limits_defaults(&out_limits->exact);
    out_limits->exact.max_limbs = 512u;
    out_limits->exact.max_bytes = 512u * 1024u;
    out_limits->max_degree = 32u;
    out_limits->max_steps = 1000000u;
    out_limits->max_refinements = 128u;
    out_limits->max_metadata_bytes = 128u * 1024u;
}

static phy_algebraic_limits resolve_limits(
    const phy_algebraic_limits *requested)
{
    phy_algebraic_limits defaults;
    phy_algebraic_limits_defaults(&defaults);
    phy_algebraic_limits resolved =
        requested != NULL ? *requested : defaults;
    if (resolved.exact.max_limbs == 0u) {
        resolved.exact.max_limbs = defaults.exact.max_limbs;
    }
    if (resolved.exact.max_steps == 0u) {
        resolved.exact.max_steps = defaults.exact.max_steps;
    }
    if (resolved.exact.max_bytes == 0u) {
        resolved.exact.max_bytes = defaults.exact.max_bytes;
    }
    if (resolved.max_degree == 0u) {
        resolved.max_degree = defaults.max_degree;
    }
    if (resolved.max_degree > PHY_ALGEBRAIC_MAX_DEGREE) {
        resolved.max_degree = PHY_ALGEBRAIC_MAX_DEGREE;
    }
    if (resolved.max_steps == 0u) {
        resolved.max_steps = defaults.max_steps;
    }
    if (resolved.max_refinements == 0u) {
        resolved.max_refinements = defaults.max_refinements;
    }
    if (resolved.max_metadata_bytes == 0u) {
        resolved.max_metadata_bytes = defaults.max_metadata_bytes;
    }
    return resolved;
}

phy_algebraic_context *phy_algebraic_context_create(
    const phy_algebraic_limits *limits)
{
    const phy_algebraic_limits resolved = resolve_limits(limits);
    if (resolved.max_degree == 0u || resolved.max_steps == 0u ||
        resolved.max_refinements == 0u ||
        resolved.max_metadata_bytes < sizeof(phy_algebraic_context)) {
        return NULL;
    }
    phy_algebraic_context *context =
        (phy_algebraic_context *)phy_alloc(sizeof *context);
    if (context == NULL) {
        return NULL;
    }
    memset(context, 0, sizeof *context);
    context->limits = resolved;
    context->metadata_bytes = sizeof *context;
    context->exact = phy_exact_context_create(&resolved.exact);
    if (context->exact == NULL) {
        phy_free(context, sizeof *context);
        return NULL;
    }
    context->magic = PHY_ALGEBRAIC_CONTEXT_MAGIC;
    return context;
}

static void release_value_fields(phy_real_algebraic *value)
{
    if (value == NULL || !context_valid(value->context)) {
        return;
    }
    phy_algebraic_context *context = value->context;
    if (value->upper_initialized) {
        phy_bigrat_destroy(&value->upper);
        value->upper_initialized = false;
    }
    if (value->lower_initialized) {
        phy_bigrat_destroy(&value->lower);
        value->lower_initialized = false;
    }
    if (value->coefficients != NULL) {
        for (size_t i = 0u; i < value->coefficient_capacity; ++i) {
            phy_bigint_destroy(&value->coefficients[i]);
        }
        size_t bytes = 0u;
        if (checked_multiply(value->coefficient_capacity,
                             sizeof(phy_bigint), &bytes)) {
            metadata_free(context, value->coefficients, bytes);
        }
        value->coefficients = NULL;
    }
    value->coefficient_count = 0u;
    value->coefficient_capacity = 0u;
}

static void destroy_value_internal(phy_real_algebraic *value)
{
    if (value == NULL || !context_valid(value->context)) {
        return;
    }
    phy_algebraic_context *context = value->context;
    if (value->linked) {
        if (value->previous != NULL) {
            value->previous->next = value->next;
        } else {
            context->values = value->next;
        }
        if (value->next != NULL) {
            value->next->previous = value->previous;
        }
        context->value_count--;
    }
    release_value_fields(value);
    value->magic = 0u;
    metadata_free(context, value, sizeof *value);
}

void phy_algebraic_context_destroy(phy_algebraic_context *context)
{
    if (!context_valid(context)) {
        return;
    }
    while (context->values != NULL) {
        destroy_value_internal(context->values);
    }
    phy_exact_context_destroy(context->exact);
    context->exact = NULL;
    context->magic = 0u;
    phy_free(context, sizeof *context);
}

void phy_algebraic_set_cancel(phy_algebraic_context *context,
                              bool (*cancelled)(void *user), void *user)
{
    if (!context_valid(context)) {
        return;
    }
    context->cancelled = cancelled;
    context->cancel_user = user;
    phy_exact_set_cancel(context->exact, cancelled, user);
}

uint32_t phy_algebraic_steps(const phy_algebraic_context *context)
{
    return context_valid(context) ? context->steps : 0u;
}

uint64_t phy_algebraic_total_steps(const phy_algebraic_context *context)
{
    return context_valid(context) ? context->total_steps : 0u;
}

size_t phy_algebraic_metadata_bytes(
    const phy_algebraic_context *context)
{
    return context_valid(context) ? context->metadata_bytes : 0u;
}

static phy_status rational_polynomial_init(
    phy_algebraic_context *context, size_t count,
    rational_polynomial *out_polynomial)
{
    if (!context_valid(context) || count == 0u ||
        out_polynomial == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    memset(out_polynomial, 0, sizeof *out_polynomial);
    size_t bytes = 0u;
    if (!checked_multiply(count, sizeof(phy_bigrat), &bytes)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_bigrat *coefficients = NULL;
    phy_status status =
        metadata_allocate(context, bytes, (void **)&coefficients);
    size_t initialized = 0u;
    while (status == PHY_OK && initialized < count) {
        status = phy_bigrat_init(
            context->exact, &coefficients[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    if (status != PHY_OK) {
        while (initialized != 0u) {
            initialized--;
            phy_bigrat_destroy(&coefficients[initialized]);
        }
        metadata_free(context, coefficients, bytes);
        return status;
    }
    out_polynomial->coefficients = coefficients;
    out_polynomial->count = count;
    out_polynomial->capacity = count;
    return PHY_OK;
}

static void rational_polynomial_destroy(
    phy_algebraic_context *context, rational_polynomial *polynomial)
{
    if (!context_valid(context) || polynomial == NULL ||
        polynomial->coefficients == NULL) {
        return;
    }
    for (size_t i = 0u; i < polynomial->capacity; ++i) {
        phy_bigrat_destroy(&polynomial->coefficients[i]);
    }
    size_t bytes = 0u;
    if (checked_multiply(polynomial->capacity, sizeof(phy_bigrat),
                         &bytes)) {
        metadata_free(context, polynomial->coefficients, bytes);
    }
    memset(polynomial, 0, sizeof *polynomial);
}

static void rational_polynomial_trim(rational_polynomial *polynomial)
{
    while (polynomial->count > 1u &&
           phy_bigrat_sign(
               &polynomial->coefficients[polynomial->count - 1u]) == 0) {
        polynomial->count--;
    }
}

static bool rational_polynomial_is_zero(
    const rational_polynomial *polynomial)
{
    return polynomial->count == 1u &&
           phy_bigrat_sign(&polynomial->coefficients[0]) == 0;
}

static phy_status rational_polynomial_from_integers(
    phy_algebraic_context *context, const phy_bigint *coefficients,
    size_t count, rational_polynomial *out_polynomial)
{
    phy_status status =
        rational_polynomial_init(context, count, out_polynomial);
    for (size_t i = 0u; status == PHY_OK && i < count; ++i) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigrat_set_bigint(
                &coefficients[i], &out_polynomial->coefficients[i]);
        }
    }
    if (status != PHY_OK) {
        rational_polynomial_destroy(context, out_polynomial);
    }
    return status;
}

static phy_status rational_polynomial_derivative(
    phy_algebraic_context *context,
    const rational_polynomial *polynomial,
    rational_polynomial *out_derivative)
{
    const size_t result_count =
        polynomial->count > 1u ? polynomial->count - 1u : 1u;
    phy_status status =
        rational_polynomial_init(context, result_count, out_derivative);
    phy_bigrat factor;
    memset(&factor, 0, sizeof factor);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &factor);
    }
    for (size_t i = 1u; status == PHY_OK && i < polynomial->count; ++i) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigrat_set_i64(
                &factor, (int64_t)i, 1);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_multiply(
                &polynomial->coefficients[i], &factor,
                &out_derivative->coefficients[i - 1u]);
        }
    }
    phy_bigrat_destroy(&factor);
    if (status != PHY_OK) {
        rational_polynomial_destroy(context, out_derivative);
    }
    return status;
}

/*
 * Scale by 1/|leading coefficient|. This controls rational growth without
 * changing any sign in a Sturm variation.
 */
static phy_status rational_polynomial_normalize(
    phy_algebraic_context *context, rational_polynomial *polynomial)
{
    rational_polynomial_trim(polynomial);
    if (rational_polynomial_is_zero(polynomial)) {
        return PHY_OK;
    }
    phy_bigrat magnitude;
    memset(&magnitude, 0, sizeof magnitude);
    phy_status status = phy_bigrat_init(context->exact, &magnitude);
    const phy_bigrat *leading =
        &polynomial->coefficients[polynomial->count - 1u];
    if (status == PHY_OK) {
        status = phy_bigrat_sign(leading) < 0
                     ? phy_bigrat_negate(leading, &magnitude)
                     : phy_bigrat_copy(leading, &magnitude);
    }
    for (size_t i = 0u;
         status == PHY_OK && i < polynomial->count; ++i) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigrat_divide(
                &polynomial->coefficients[i], &magnitude,
                &polynomial->coefficients[i]);
        }
    }
    phy_bigrat_destroy(&magnitude);
    return status;
}

static phy_status rational_polynomial_remainder(
    phy_algebraic_context *context,
    const rational_polynomial *dividend,
    const rational_polynomial *divisor,
    rational_polynomial *out_remainder)
{
    if (rational_polynomial_is_zero(divisor)) {
        return PHY_ERR_DOMAIN;
    }
    phy_status status = rational_polynomial_init(
        context, dividend->count, out_remainder);
    for (size_t i = 0u;
         status == PHY_OK && i < dividend->count; ++i) {
        status = phy_bigrat_copy(
            &dividend->coefficients[i],
            &out_remainder->coefficients[i]);
    }

    phy_bigrat factor;
    phy_bigrat product;
    memset(&factor, 0, sizeof factor);
    memset(&product, 0, sizeof product);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &factor);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &product);
    }

    rational_polynomial_trim(out_remainder);
    while (status == PHY_OK &&
           !rational_polynomial_is_zero(out_remainder) &&
           out_remainder->count >= divisor->count) {
        const size_t shift =
            out_remainder->count - divisor->count;
        const phy_bigrat *remainder_leading =
            &out_remainder->coefficients[
                out_remainder->count - 1u];
        const phy_bigrat *divisor_leading =
            &divisor->coefficients[divisor->count - 1u];
        status = algebraic_step(
            context, (uint32_t)divisor->count);
        if (status == PHY_OK) {
            status = phy_bigrat_divide(
                remainder_leading, divisor_leading, &factor);
        }
        for (size_t i = 0u;
             status == PHY_OK && i < divisor->count; ++i) {
            status = phy_bigrat_multiply(
                &factor, &divisor->coefficients[i], &product);
            if (status == PHY_OK) {
                status = phy_bigrat_subtract(
                    &out_remainder->coefficients[i + shift],
                    &product,
                    &out_remainder->coefficients[i + shift]);
            }
        }
        rational_polynomial_trim(out_remainder);
    }

    phy_bigrat_destroy(&product);
    phy_bigrat_destroy(&factor);
    if (status != PHY_OK) {
        rational_polynomial_destroy(context, out_remainder);
    }
    return status;
}

static phy_status rational_polynomial_negate(
    phy_algebraic_context *context, rational_polynomial *polynomial)
{
    phy_status status = PHY_OK;
    for (size_t i = 0u;
         status == PHY_OK && i < polynomial->count; ++i) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigrat_negate(
                &polynomial->coefficients[i],
                &polynomial->coefficients[i]);
        }
    }
    return status;
}

static void sturm_chain_destroy(phy_algebraic_context *context,
                                sturm_chain *chain)
{
    if (!context_valid(context) || chain == NULL ||
        chain->items == NULL) {
        return;
    }
    for (size_t i = 0u; i < chain->count; ++i) {
        rational_polynomial_destroy(context, &chain->items[i]);
    }
    size_t bytes = 0u;
    if (checked_multiply(chain->capacity,
                         sizeof(rational_polynomial), &bytes)) {
        metadata_free(context, chain->items, bytes);
    }
    memset(chain, 0, sizeof *chain);
}

static phy_status sturm_chain_build(
    phy_algebraic_context *context, const phy_bigint *coefficients,
    size_t coefficient_count, sturm_chain *out_chain)
{
    memset(out_chain, 0, sizeof *out_chain);
    const size_t capacity = coefficient_count;
    size_t bytes = 0u;
    if (!checked_multiply(
            capacity, sizeof(rational_polynomial), &bytes)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_status status = metadata_allocate(
        context, bytes, (void **)&out_chain->items);
    if (status != PHY_OK) {
        return status;
    }
    out_chain->capacity = capacity;

    status = rational_polynomial_from_integers(
        context, coefficients, coefficient_count,
        &out_chain->items[0]);
    if (status == PHY_OK) {
        out_chain->count = 1u;
        status = rational_polynomial_normalize(
            context, &out_chain->items[0]);
    }
    if (status == PHY_OK) {
        status = rational_polynomial_derivative(
            context, &out_chain->items[0],
            &out_chain->items[1]);
    }
    if (status == PHY_OK) {
        out_chain->count = 2u;
        status = rational_polynomial_normalize(
            context, &out_chain->items[1]);
    }

    while (status == PHY_OK &&
           out_chain->items[out_chain->count - 1u].count > 1u) {
        if (out_chain->count >= out_chain->capacity) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
            break;
        }
        rational_polynomial *next =
            &out_chain->items[out_chain->count];
        status = rational_polynomial_remainder(
            context, &out_chain->items[out_chain->count - 2u],
            &out_chain->items[out_chain->count - 1u], next);
        if (status == PHY_OK &&
            rational_polynomial_is_zero(next)) {
            /* gcd(f,f') has positive degree: f is not square-free. */
            status = PHY_ERR_DOMAIN;
        }
        if (status == PHY_OK) {
            status = rational_polynomial_negate(context, next);
        }
        if (status == PHY_OK) {
            status = rational_polynomial_normalize(context, next);
        }
        if (status == PHY_OK) {
            out_chain->count++;
        } else {
            rational_polynomial_destroy(context, next);
        }
    }

    if (status != PHY_OK) {
        sturm_chain_destroy(context, out_chain);
    }
    return status;
}

static phy_status rational_polynomial_evaluate(
    phy_algebraic_context *context,
    const rational_polynomial *polynomial, const phy_bigrat *point,
    phy_bigrat *out_value)
{
    phy_status status = phy_bigrat_set_i64(out_value, 0, 1);
    for (size_t i = polynomial->count;
         status == PHY_OK && i-- != 0u;) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigrat_multiply(
                out_value, point, out_value);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_add(
                out_value, &polynomial->coefficients[i], out_value);
        }
    }
    return status;
}

static phy_status sturm_variations(
    phy_algebraic_context *context, const sturm_chain *chain,
    const phy_bigrat *point, uint32_t *out_variations)
{
    phy_bigrat value;
    memset(&value, 0, sizeof value);
    phy_status status = phy_bigrat_init(context->exact, &value);
    unsigned previous_sign = 0u;
    uint32_t variations = 0u;
    for (size_t i = 0u; status == PHY_OK && i < chain->count; ++i) {
        status = rational_polynomial_evaluate(
            context, &chain->items[i], point, &value);
        const int sign =
            status == PHY_OK ? phy_bigrat_sign(&value) : 0;
        if (status == PHY_OK && i == 0u && sign == 0) {
            status = PHY_ERR_DOMAIN;
        }
        if (status == PHY_OK && sign != 0) {
            const unsigned current = sign < 0 ? 1u : 2u;
            if (previous_sign != 0u && current != previous_sign) {
                variations++;
            }
            previous_sign = current;
        }
    }
    phy_bigrat_destroy(&value);
    if (status == PHY_OK) {
        *out_variations = variations;
    }
    return status;
}

static phy_status root_count_for_coefficients(
    phy_algebraic_context *context, const phy_bigint *coefficients,
    size_t coefficient_count, const phy_bigrat *lower,
    const phy_bigrat *upper, uint32_t *out_count)
{
    int order = 0;
    phy_status status =
        phy_bigrat_compare(lower, upper, &order);
    if (status == PHY_OK && order >= 0) {
        status = PHY_ERR_DOMAIN;
    }
    sturm_chain chain;
    memset(&chain, 0, sizeof chain);
    if (status == PHY_OK) {
        status = sturm_chain_build(
            context, coefficients, coefficient_count, &chain);
    }
    uint32_t lower_variations = 0u;
    uint32_t upper_variations = 0u;
    if (status == PHY_OK) {
        status = sturm_variations(
            context, &chain, lower, &lower_variations);
    }
    if (status == PHY_OK) {
        status = sturm_variations(
            context, &chain, upper, &upper_variations);
    }
    if (status == PHY_OK && lower_variations < upper_variations) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    if (status == PHY_OK) {
        *out_count = lower_variations - upper_variations;
    }
    sturm_chain_destroy(context, &chain);
    return status;
}

static void destroy_coefficient_array(
    phy_algebraic_context *context, phy_bigint *coefficients,
    size_t capacity)
{
    if (coefficients == NULL) {
        return;
    }
    for (size_t i = 0u; i < capacity; ++i) {
        phy_bigint_destroy(&coefficients[i]);
    }
    size_t bytes = 0u;
    if (checked_multiply(capacity, sizeof(phy_bigint), &bytes)) {
        metadata_free(context, coefficients, bytes);
    }
}

static phy_status allocate_coefficient_array(
    phy_algebraic_context *context, size_t capacity,
    phy_bigint **out_coefficients)
{
    if (!context_valid(context) || capacity == 0u ||
        out_coefficients == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_coefficients = NULL;
    size_t bytes = 0u;
    if (!checked_multiply(capacity, sizeof(phy_bigint), &bytes)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_bigint *coefficients = NULL;
    phy_status status = metadata_allocate(
        context, bytes, (void **)&coefficients);
    size_t initialized = 0u;
    while (status == PHY_OK && initialized < capacity) {
        status = phy_bigint_init(
            context->exact, &coefficients[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    if (status != PHY_OK) {
        while (initialized != 0u) {
            initialized--;
            phy_bigint_destroy(&coefficients[initialized]);
        }
        metadata_free(context, coefficients, bytes);
        return status;
    }
    *out_coefficients = coefficients;
    return PHY_OK;
}

static phy_status canonicalize_coefficient_array(
    phy_algebraic_context *context, phy_bigint *coefficients,
    size_t capacity, size_t *out_count)
{
    if (!context_valid(context) || coefficients == NULL ||
        capacity < 2u || out_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    size_t count = capacity;
    while (count != 0u &&
           phy_bigint_sign(&coefficients[count - 1u]) == 0) {
        count--;
    }
    if (count < 2u) {
        return PHY_ERR_DOMAIN;
    }

    phy_bigint content;
    memset(&content, 0, sizeof content);
    phy_status status = phy_bigint_init(context->exact, &content);
    if (status == PHY_OK) {
        status = phy_bigint_set_i64(&content, 0);
    }
    for (size_t index = 0u;
         status == PHY_OK && index < count; ++index) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK &&
            phy_bigint_sign(&coefficients[index]) != 0) {
            status = phy_bigint_gcd(
                &content, &coefficients[index], &content);
        }
    }
    if (status == PHY_OK && phy_bigint_sign(&content) == 0) {
        status = PHY_ERR_DOMAIN;
    }
    for (size_t index = 0u;
         status == PHY_OK && index < count; ++index) {
        status = phy_bigint_divide_exact(
            &coefficients[index], &content, &coefficients[index]);
    }
    phy_bigint_destroy(&content);

    if (status == PHY_OK &&
        phy_bigint_sign(&coefficients[count - 1u]) < 0) {
        for (size_t index = 0u;
             status == PHY_OK && index < count; ++index) {
            status = phy_bigint_negate(
                &coefficients[index], &coefficients[index]);
        }
    }
    if (status == PHY_OK) {
        *out_count = count;
    }
    return status;
}

static phy_status load_canonical_coefficients(
    phy_algebraic_context *context,
    const char *const *coefficient_text, size_t requested_count,
    phy_bigint **out_coefficients, size_t *out_count,
    size_t *out_capacity)
{
    if (coefficient_text == NULL || requested_count < 2u ||
        requested_count - 1u > context->limits.max_degree ||
        out_coefficients == NULL || out_count == NULL ||
        out_capacity == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_coefficients = NULL;
    *out_count = 0u;
    *out_capacity = 0u;
    phy_bigint *coefficients = NULL;
    phy_status status = allocate_coefficient_array(
        context, requested_count, &coefficients);
    for (size_t i = 0u;
         status == PHY_OK && i < requested_count; ++i) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK && coefficient_text[i] == NULL) {
            status = PHY_ERR_INVALID_ARGUMENT;
        }
        if (status == PHY_OK) {
            status = phy_bigint_read(
                &coefficients[i], coefficient_text[i]);
        }
    }
    if (status != PHY_OK) {
        destroy_coefficient_array(
            context, coefficients, requested_count);
        return status;
    }

    size_t count = 0u;
    status = canonicalize_coefficient_array(
        context, coefficients, requested_count, &count);
    if (status != PHY_OK) {
        destroy_coefficient_array(
            context, coefficients, requested_count);
        return status;
    }

    *out_coefficients = coefficients;
    *out_count = count;
    *out_capacity = requested_count;
    return PHY_OK;
}

static phy_status rational_text_read(
    phy_bigrat *out_value, phy_exact_rational_text text)
{
    if (text.numerator == NULL || text.denominator == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_bigrat_read(
        out_value, text.numerator, text.denominator);
}

phy_status phy_algebraic_count_real_roots(
    phy_algebraic_context *context,
    const char *const *coefficients, size_t coefficient_count,
    phy_exact_rational_text lower, phy_exact_rational_text upper,
    uint32_t *out_count)
{
    if (!context_valid(context) || out_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_status status = call_begin(context);
    phy_bigint *canonical = NULL;
    size_t canonical_count = 0u;
    size_t canonical_capacity = 0u;
    if (status == PHY_OK) {
        status = load_canonical_coefficients(
            context, coefficients, coefficient_count, &canonical,
            &canonical_count, &canonical_capacity);
    }
    phy_bigrat lower_value;
    phy_bigrat upper_value;
    memset(&lower_value, 0, sizeof lower_value);
    memset(&upper_value, 0, sizeof upper_value);
    bool lower_initialized = false;
    bool upper_initialized = false;
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &lower_value);
        lower_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &upper_value);
        upper_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = rational_text_read(&lower_value, lower);
    }
    if (status == PHY_OK) {
        status = rational_text_read(&upper_value, upper);
    }
    uint32_t result = 0u;
    if (status == PHY_OK) {
        status = root_count_for_coefficients(
            context, canonical, canonical_count,
            &lower_value, &upper_value, &result);
    }
    if (upper_initialized) {
        phy_bigrat_destroy(&upper_value);
    }
    if (lower_initialized) {
        phy_bigrat_destroy(&lower_value);
    }
    destroy_coefficient_array(
        context, canonical, canonical_capacity);
    if (status == PHY_OK) {
        *out_count = result;
    }
    return call_end(context, status);
}

static phy_status integer_polynomial_evaluate(
    phy_real_algebraic *value, const phy_bigrat *point,
    phy_bigrat *out_result)
{
    phy_algebraic_context *context = value->context;
    phy_bigrat coefficient;
    memset(&coefficient, 0, sizeof coefficient);
    phy_status status = phy_bigrat_init(context->exact, &coefficient);
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(out_result, 0, 1);
    }
    for (size_t i = value->coefficient_count;
         status == PHY_OK && i-- != 0u;) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigrat_multiply(
                out_result, point, out_result);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_set_bigint(
                &value->coefficients[i], &coefficient);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_add(
                out_result, &coefficient, out_result);
        }
    }
    phy_bigrat_destroy(&coefficient);
    return status;
}

phy_status phy_real_algebraic_create(
    phy_algebraic_context *context,
    const char *const *coefficients, size_t coefficient_count,
    phy_exact_rational_text lower, phy_exact_rational_text upper,
    phy_real_algebraic **out_value)
{
    if (!context_valid(context) || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = NULL;
    phy_status status = call_begin(context);
    phy_real_algebraic *value = NULL;
    if (status == PHY_OK) {
        status = metadata_allocate(
            context, sizeof *value, (void **)&value);
    }
    if (status == PHY_OK) {
        value->context = context;
        status = load_canonical_coefficients(
            context, coefficients, coefficient_count,
            &value->coefficients, &value->coefficient_count,
            &value->coefficient_capacity);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &value->lower);
        value->lower_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &value->upper);
        value->upper_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = rational_text_read(&value->lower, lower);
    }
    if (status == PHY_OK) {
        status = rational_text_read(&value->upper, upper);
    }
    uint32_t roots = 0u;
    if (status == PHY_OK) {
        status = root_count_for_coefficients(
            context, value->coefficients, value->coefficient_count,
            &value->lower, &value->upper, &roots);
    }
    if (status == PHY_OK && roots != 1u) {
        status = PHY_ERR_DOMAIN;
    }

    phy_bigrat lower_result;
    phy_bigrat upper_result;
    memset(&lower_result, 0, sizeof lower_result);
    memset(&upper_result, 0, sizeof upper_result);
    bool lower_result_initialized = false;
    bool upper_result_initialized = false;
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &lower_result);
        lower_result_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &upper_result);
        upper_result_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = integer_polynomial_evaluate(
            value, &value->lower, &lower_result);
    }
    if (status == PHY_OK) {
        status = integer_polynomial_evaluate(
            value, &value->upper, &upper_result);
    }
    if (status == PHY_OK &&
        (phy_bigrat_sign(&lower_result) == 0 ||
         phy_bigrat_sign(&upper_result) == 0 ||
         phy_bigrat_sign(&lower_result) ==
             phy_bigrat_sign(&upper_result))) {
        status = PHY_ERR_DOMAIN;
    }
    if (upper_result_initialized) {
        phy_bigrat_destroy(&upper_result);
    }
    if (lower_result_initialized) {
        phy_bigrat_destroy(&lower_result);
    }

    if (status == PHY_OK) {
        value->magic = PHY_REAL_ALGEBRAIC_MAGIC;
        value->linked = true;
        value->next = context->values;
        if (context->values != NULL) {
            context->values->previous = value;
        }
        context->values = value;
        context->value_count++;
        *out_value = value;
    } else if (value != NULL) {
        release_value_fields(value);
        metadata_free(context, value, sizeof *value);
    }
    return call_end(context, status);
}

typedef struct {
    phy_bigrat lower;
    phy_bigrat upper;
    uint32_t lower_variations;
    uint32_t upper_variations;
    bool lower_initialized;
    bool upper_initialized;
} isolation_interval;

static void isolation_intervals_destroy(
    phy_algebraic_context *context, isolation_interval *intervals,
    size_t capacity)
{
    if (intervals == NULL) {
        return;
    }
    for (size_t index = 0u; index < capacity; ++index) {
        if (intervals[index].upper_initialized) {
            phy_bigrat_destroy(&intervals[index].upper);
        }
        if (intervals[index].lower_initialized) {
            phy_bigrat_destroy(&intervals[index].lower);
        }
    }
    size_t bytes = 0u;
    if (checked_multiply(capacity, sizeof(*intervals), &bytes)) {
        metadata_free(context, intervals, bytes);
    }
}

static phy_status value_from_certificate(
    phy_algebraic_context *context, const phy_bigint *coefficients,
    size_t coefficient_count, const phy_bigrat *lower,
    const phy_bigrat *upper, phy_real_algebraic **out_value)
{
    *out_value = NULL;
    phy_real_algebraic *value = NULL;
    phy_status status =
        metadata_allocate(context, sizeof *value, (void **)&value);
    if (status != PHY_OK) {
        return status;
    }
    value->context = context;
    size_t bytes = 0u;
    if (!checked_multiply(
            coefficient_count, sizeof(phy_bigint), &bytes)) {
        status = PHY_ERR_MEMORY_LIMIT;
    }
    if (status == PHY_OK) {
        status = metadata_allocate(
            context, bytes, (void **)&value->coefficients);
    }
    if (status == PHY_OK) {
        value->coefficient_capacity = coefficient_count;
        value->coefficient_count = coefficient_count;
    }
    for (size_t index = 0u;
         index < coefficient_count && status == PHY_OK; ++index) {
        status = phy_bigint_init(
            context->exact, &value->coefficients[index]);
        if (status == PHY_OK) {
            status = phy_bigint_copy(
                &coefficients[index], &value->coefficients[index]);
        }
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &value->lower);
        value->lower_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &value->upper);
        value->upper_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(lower, &value->lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(upper, &value->upper);
    }
    if (status == PHY_OK) {
        value->magic = PHY_REAL_ALGEBRAIC_MAGIC;
        value->linked = true;
        value->next = context->values;
        if (context->values != NULL) {
            context->values->previous = value;
        }
        context->values = value;
        context->value_count++;
        *out_value = value;
        return PHY_OK;
    }
    release_value_fields(value);
    metadata_free(context, value, sizeof *value);
    return status;
}

static phy_status rational_polynomial_affine_transform(
    phy_algebraic_context *context,
    const phy_real_algebraic *value, const phy_bigrat *slope,
    const phy_bigrat *intercept,
    rational_polynomial *out_polynomial)
{
    memset(out_polynomial, 0, sizeof *out_polynomial);
    rational_polynomial current;
    rational_polynomial next;
    memset(&current, 0, sizeof current);
    memset(&next, 0, sizeof next);
    const size_t capacity = value->coefficient_count;
    phy_status status = rational_polynomial_init(
        context, capacity, &current);
    if (status == PHY_OK) {
        status = rational_polynomial_init(
            context, capacity, &next);
    }
    phy_bigrat product;
    memset(&product, 0, sizeof product);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &product);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(
            &value->coefficients[value->coefficient_count - 1u],
            &current.coefficients[0]);
        current.count = 1u;
    }

    for (size_t source = value->coefficient_count - 1u;
         status == PHY_OK && source-- != 0u;) {
        for (size_t index = 0u;
             status == PHY_OK && index < next.capacity; ++index) {
            status = phy_bigrat_set_i64(
                &next.coefficients[index], 0, 1);
        }
        next.count = current.count + 1u;
        for (size_t index = 0u;
             status == PHY_OK && index < current.count; ++index) {
            status = algebraic_step(context, 2u);
            if (status == PHY_OK) {
                status = phy_bigrat_multiply(
                    &current.coefficients[index], intercept, &product);
            }
            if (status == PHY_OK) {
                status = phy_bigrat_add(
                    &next.coefficients[index], &product,
                    &next.coefficients[index]);
            }
            if (status == PHY_OK) {
                status = phy_bigrat_multiply(
                    &current.coefficients[index], slope, &product);
            }
            if (status == PHY_OK) {
                status = phy_bigrat_add(
                    &next.coefficients[index + 1u], &product,
                    &next.coefficients[index + 1u]);
            }
        }
        if (status == PHY_OK) {
            status = phy_bigrat_set_bigint(
                &value->coefficients[source], &product);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_add(
                &next.coefficients[0], &product,
                &next.coefficients[0]);
        }
        if (status == PHY_OK) {
            const rational_polynomial swap = current;
            current = next;
            next = swap;
        }
    }

    phy_bigrat_destroy(&product);
    if (status == PHY_OK) {
        *out_polynomial = current;
        memset(&current, 0, sizeof current);
    }
    rational_polynomial_destroy(context, &next);
    rational_polynomial_destroy(context, &current);
    return status;
}

static phy_status rational_polynomial_to_primitive_integers(
    phy_algebraic_context *context,
    const rational_polynomial *polynomial,
    phy_bigint **out_coefficients, size_t *out_count,
    size_t *out_capacity)
{
    *out_coefficients = NULL;
    *out_count = 0u;
    *out_capacity = 0u;
    phy_bigint *coefficients = NULL;
    phy_status status = allocate_coefficient_array(
        context, polynomial->count, &coefficients);

    phy_bigint lcm;
    phy_bigint gcd;
    phy_bigint quotient;
    phy_bigint multiplier;
    memset(&lcm, 0, sizeof lcm);
    memset(&gcd, 0, sizeof gcd);
    memset(&quotient, 0, sizeof quotient);
    memset(&multiplier, 0, sizeof multiplier);
    if (status == PHY_OK) {
        status = phy_bigint_init(context->exact, &lcm);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context->exact, &gcd);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context->exact, &quotient);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context->exact, &multiplier);
    }
    if (status == PHY_OK) {
        status = phy_bigint_set_i64(&lcm, 1);
    }
    for (size_t index = 0u;
         status == PHY_OK && index < polynomial->count; ++index) {
        const phy_bigint *denominator = phy_bigrat_denominator(
            &polynomial->coefficients[index]);
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigint_gcd(&lcm, denominator, &gcd);
        }
        if (status == PHY_OK) {
            status = phy_bigint_divide_exact(
                &lcm, &gcd, &quotient);
        }
        if (status == PHY_OK) {
            status = phy_bigint_multiply(
                &quotient, denominator, &lcm);
        }
    }
    for (size_t index = 0u;
         status == PHY_OK && index < polynomial->count; ++index) {
        const phy_bigint *numerator = phy_bigrat_numerator(
            &polynomial->coefficients[index]);
        const phy_bigint *denominator = phy_bigrat_denominator(
            &polynomial->coefficients[index]);
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigint_divide_exact(
                &lcm, denominator, &multiplier);
        }
        if (status == PHY_OK) {
            status = phy_bigint_multiply(
                numerator, &multiplier, &coefficients[index]);
        }
    }
    size_t count = 0u;
    if (status == PHY_OK) {
        status = canonicalize_coefficient_array(
            context, coefficients, polynomial->count, &count);
    }

    phy_bigint_destroy(&multiplier);
    phy_bigint_destroy(&quotient);
    phy_bigint_destroy(&gcd);
    phy_bigint_destroy(&lcm);
    if (status != PHY_OK) {
        destroy_coefficient_array(
            context, coefficients, polynomial->count);
        return status;
    }
    *out_coefficients = coefficients;
    *out_count = count;
    *out_capacity = polynomial->count;
    return PHY_OK;
}

static phy_status value_from_rational_point(
    phy_algebraic_context *context, const phy_bigrat *point,
    phy_real_algebraic **out_value)
{
    *out_value = NULL;
    phy_bigint *coefficients = NULL;
    phy_status status =
        allocate_coefficient_array(context, 2u, &coefficients);
    if (status == PHY_OK) {
        status = phy_bigint_negate(
            phy_bigrat_numerator(point), &coefficients[0]);
    }
    if (status == PHY_OK) {
        status = phy_bigint_copy(
            phy_bigrat_denominator(point), &coefficients[1]);
    }
    size_t count = 0u;
    if (status == PHY_OK) {
        status = canonicalize_coefficient_array(
            context, coefficients, 2u, &count);
    }
    phy_real_algebraic *result = NULL;
    if (status == PHY_OK) {
        status = value_from_certificate(
            context, coefficients, count, point, point, &result);
    }
    if (status == PHY_OK) {
        result->rational = true;
        *out_value = result;
    }
    destroy_coefficient_array(context, coefficients, 2u);
    return status;
}

static phy_status publish_transformed_interval(
    phy_algebraic_context *context, const phy_bigint *coefficients,
    size_t coefficient_count, const phy_bigrat *lower,
    const phy_bigrat *upper, phy_real_algebraic **out_value)
{
    uint32_t roots = 0u;
    phy_status status = root_count_for_coefficients(
        context, coefficients, coefficient_count, lower, upper, &roots);
    if (status == PHY_OK && roots != 1u) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    return status == PHY_OK
               ? value_from_certificate(
                     context, coefficients, coefficient_count,
                     lower, upper, out_value)
               : status;
}

static phy_status affine_transform(
    const phy_real_algebraic *value, const phy_bigrat *scale,
    const phy_bigrat *offset, phy_real_algebraic **out_value)
{
    phy_algebraic_context *context = value->context;
    if (phy_bigrat_sign(scale) == 0) {
        return value_from_rational_point(context, offset, out_value);
    }

    phy_bigrat point;
    phy_bigrat inverse_scale;
    phy_bigrat negative_offset;
    phy_bigrat intercept;
    phy_bigrat lower;
    phy_bigrat upper;
    memset(&point, 0, sizeof point);
    memset(&inverse_scale, 0, sizeof inverse_scale);
    memset(&negative_offset, 0, sizeof negative_offset);
    memset(&intercept, 0, sizeof intercept);
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    phy_status status = phy_bigrat_init(context->exact, &point);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &inverse_scale);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &negative_offset);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &intercept);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &upper);
    }

    if (status == PHY_OK && value->rational) {
        status = phy_bigrat_multiply(
            &value->lower, scale, &point);
        if (status == PHY_OK) {
            status = phy_bigrat_add(&point, offset, &point);
        }
        if (status == PHY_OK) {
            status = value_from_rational_point(
                context, &point, out_value);
        }
        phy_bigrat_destroy(&upper);
        phy_bigrat_destroy(&lower);
        phy_bigrat_destroy(&intercept);
        phy_bigrat_destroy(&negative_offset);
        phy_bigrat_destroy(&inverse_scale);
        phy_bigrat_destroy(&point);
        return status;
    }

    if (status == PHY_OK) {
        status = phy_bigrat_reciprocal(scale, &inverse_scale);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_negate(offset, &negative_offset);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(
            &negative_offset, scale, &intercept);
    }

    rational_polynomial transformed;
    memset(&transformed, 0, sizeof transformed);
    if (status == PHY_OK) {
        status = rational_polynomial_affine_transform(
            context, value, &inverse_scale, &intercept,
            &transformed);
    }
    phy_bigint *coefficients = NULL;
    size_t coefficient_count = 0u;
    size_t coefficient_capacity = 0u;
    if (status == PHY_OK) {
        status = rational_polynomial_to_primitive_integers(
            context, &transformed, &coefficients,
            &coefficient_count, &coefficient_capacity);
    }

    const phy_bigrat *first =
        phy_bigrat_sign(scale) > 0 ? &value->lower : &value->upper;
    const phy_bigrat *second =
        phy_bigrat_sign(scale) > 0 ? &value->upper : &value->lower;
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(first, scale, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(&lower, offset, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(second, scale, &upper);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_add(&upper, offset, &upper);
    }
    if (status == PHY_OK) {
        status = publish_transformed_interval(
            context, coefficients, coefficient_count,
            &lower, &upper, out_value);
    }

    destroy_coefficient_array(
        context, coefficients, coefficient_capacity);
    rational_polynomial_destroy(context, &transformed);
    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
    phy_bigrat_destroy(&intercept);
    phy_bigrat_destroy(&negative_offset);
    phy_bigrat_destroy(&inverse_scale);
    phy_bigrat_destroy(&point);
    return status;
}

phy_status phy_real_algebraic_translate_rational(
    const phy_real_algebraic *value, phy_exact_rational_text translation,
    phy_real_algebraic **out_value)
{
    if (!value_valid_handle(value) || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = NULL;
    phy_algebraic_context *context = value->context;
    phy_status status = call_begin(context);
    phy_bigrat offset;
    phy_bigrat one;
    memset(&offset, 0, sizeof offset);
    memset(&one, 0, sizeof one);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &offset);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &one);
    }
    if (status == PHY_OK) {
        status = rational_text_read(&offset, translation);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&one, 1, 1);
    }
    if (status == PHY_OK) {
        status = affine_transform(value, &one, &offset, out_value);
    }
    phy_bigrat_destroy(&one);
    phy_bigrat_destroy(&offset);
    return call_end(context, status);
}

phy_status phy_real_algebraic_scale_rational(
    const phy_real_algebraic *value, phy_exact_rational_text factor,
    phy_real_algebraic **out_value)
{
    if (!value_valid_handle(value) || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = NULL;
    phy_algebraic_context *context = value->context;
    phy_status status = call_begin(context);
    phy_bigrat scale;
    phy_bigrat zero;
    memset(&scale, 0, sizeof scale);
    memset(&zero, 0, sizeof zero);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &scale);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &zero);
    }
    if (status == PHY_OK) {
        status = rational_text_read(&scale, factor);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&zero, 0, 1);
    }
    if (status == PHY_OK) {
        status = affine_transform(value, &scale, &zero, out_value);
    }
    phy_bigrat_destroy(&zero);
    phy_bigrat_destroy(&scale);
    return call_end(context, status);
}

phy_status phy_real_algebraic_reciprocal(
    const phy_real_algebraic *value, phy_real_algebraic **out_value)
{
    if (!value_valid_handle(value) || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = NULL;
    phy_algebraic_context *context = value->context;
    phy_status status = call_begin(context);

    if (status == PHY_OK && value->rational) {
        phy_bigrat reciprocal;
        memset(&reciprocal, 0, sizeof reciprocal);
        status = phy_bigrat_init(context->exact, &reciprocal);
        if (status == PHY_OK) {
            status = phy_bigrat_reciprocal(
                &value->lower, &reciprocal);
        }
        if (status == PHY_OK) {
            status = value_from_rational_point(
                context, &reciprocal, out_value);
        }
        phy_bigrat_destroy(&reciprocal);
        return call_end(context, status);
    }

    phy_real_algebraic *working = NULL;
    if (status == PHY_OK) {
        status = value_from_certificate(
            context, value->coefficients, value->coefficient_count,
            &value->lower, &value->upper, &working);
    }
    uint32_t refinements = 0u;
    while (status == PHY_OK && !working->rational &&
           (phy_bigrat_sign(&working->lower) == 0 ||
            phy_bigrat_sign(&working->upper) == 0 ||
            phy_bigrat_sign(&working->lower) !=
                phy_bigrat_sign(&working->upper))) {
        if (refinements >= context->limits.max_refinements) {
            status = PHY_ERR_TIMEOUT;
            break;
        }
        status = phy_real_algebraic_refine(working, 1u);
        refinements++;
    }

    if (status == PHY_OK && working->rational) {
        phy_bigrat reciprocal;
        memset(&reciprocal, 0, sizeof reciprocal);
        status = phy_bigrat_init(context->exact, &reciprocal);
        if (status == PHY_OK) {
            status = phy_bigrat_reciprocal(
                &working->lower, &reciprocal);
        }
        if (status == PHY_OK) {
            status = value_from_rational_point(
                context, &reciprocal, out_value);
        }
        phy_bigrat_destroy(&reciprocal);
        phy_real_algebraic_destroy(working);
        return call_end(context, status);
    }

    phy_bigint *coefficients = NULL;
    size_t coefficient_count = 0u;
    const size_t coefficient_capacity =
        status == PHY_OK ? working->coefficient_count : 0u;
    if (status == PHY_OK) {
        status = allocate_coefficient_array(
            context, coefficient_capacity, &coefficients);
    }
    for (size_t index = 0u;
         status == PHY_OK && index < coefficient_capacity; ++index) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigint_copy(
                &working->coefficients[
                    coefficient_capacity - 1u - index],
                &coefficients[index]);
        }
    }
    if (status == PHY_OK) {
        status = canonicalize_coefficient_array(
            context, coefficients, coefficient_capacity,
            &coefficient_count);
    }

    phy_bigrat lower;
    phy_bigrat upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &upper);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_reciprocal(
            &working->upper, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_reciprocal(
            &working->lower, &upper);
    }
    if (status == PHY_OK) {
        status = publish_transformed_interval(
            context, coefficients, coefficient_count,
            &lower, &upper, out_value);
    }

    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
    destroy_coefficient_array(
        context, coefficients, coefficient_capacity);
    phy_real_algebraic_destroy(working);
    return call_end(context, status);
}

static phy_status cauchy_interval(
    phy_algebraic_context *context, const phy_bigint *coefficients,
    size_t coefficient_count, phy_bigrat *out_lower,
    phy_bigrat *out_upper)
{
    size_t maximum = 0u;
    for (size_t index = 1u; index < coefficient_count; ++index) {
        if (phy_bigint_compare_abs(
                &coefficients[index], &coefficients[maximum]) > 0) {
            maximum = index;
        }
    }
    phy_bigint magnitude;
    phy_bigint one;
    phy_bigint bound;
    phy_bigint negative_bound;
    memset(&magnitude, 0, sizeof magnitude);
    memset(&one, 0, sizeof one);
    memset(&bound, 0, sizeof bound);
    memset(&negative_bound, 0, sizeof negative_bound);
    phy_status status = phy_bigint_init(context->exact, &magnitude);
    if (status == PHY_OK) {
        status = phy_bigint_init(context->exact, &one);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context->exact, &bound);
    }
    if (status == PHY_OK) {
        status = phy_bigint_init(context->exact, &negative_bound);
    }
    if (status == PHY_OK) {
        status =
            phy_bigint_copy(&coefficients[maximum], &magnitude);
    }
    if (status == PHY_OK && phy_bigint_sign(&magnitude) < 0) {
        status = phy_bigint_negate(&magnitude, &magnitude);
    }
    if (status == PHY_OK) {
        status = phy_bigint_set_i64(&one, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigint_add(&magnitude, &one, &bound);
    }
    if (status == PHY_OK) {
        status = phy_bigint_negate(&bound, &negative_bound);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(&negative_bound, out_lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(&bound, out_upper);
    }
    phy_bigint_destroy(&negative_bound);
    phy_bigint_destroy(&bound);
    phy_bigint_destroy(&one);
    phy_bigint_destroy(&magnitude);
    return status;
}

static phy_status find_nonroot_split(
    phy_algebraic_context *context, const sturm_chain *chain,
    const isolation_interval *interval, size_t polynomial_degree,
    phy_bigrat *out_split, uint32_t *out_variations)
{
    phy_bigrat width;
    phy_bigrat fraction;
    phy_bigrat scaled;
    phy_bigrat value;
    memset(&width, 0, sizeof width);
    memset(&fraction, 0, sizeof fraction);
    memset(&scaled, 0, sizeof scaled);
    memset(&value, 0, sizeof value);
    phy_status status = phy_bigrat_init(context->exact, &width);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &fraction);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &scaled);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &value);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_subtract(
            &interval->upper, &interval->lower, &width);
    }
    const int64_t denominator =
        (int64_t)polynomial_degree + 2;
    bool found = false;
    for (size_t attempt = 1u;
         status == PHY_OK && attempt <= polynomial_degree + 1u;
         ++attempt) {
        status = phy_bigrat_set_i64(
            &fraction, (int64_t)attempt, denominator);
        if (status == PHY_OK) {
            status = phy_bigrat_multiply(
                &width, &fraction, &scaled);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_add(
                &interval->lower, &scaled, out_split);
        }
        if (status == PHY_OK) {
            status = rational_polynomial_evaluate(
                context, &chain->items[0], out_split, &value);
        }
        if (status == PHY_OK && phy_bigrat_sign(&value) != 0) {
            status = sturm_variations(
                context, chain, out_split, out_variations);
            found = status == PHY_OK;
            break;
        }
    }
    phy_bigrat_destroy(&value);
    phy_bigrat_destroy(&scaled);
    phy_bigrat_destroy(&fraction);
    phy_bigrat_destroy(&width);
    if (status == PHY_OK && !found) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    return status;
}

phy_status phy_algebraic_isolate_real_roots(
    phy_algebraic_context *context,
    const char *const *coefficient_text, size_t coefficient_count,
    phy_real_algebraic **out_values, size_t value_capacity,
    size_t *out_count)
{
    if (!context_valid(context) || out_values == NULL ||
        value_capacity == 0u || out_count == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (size_t index = 0u; index < value_capacity; ++index) {
        out_values[index] = NULL;
    }
    phy_status status = call_begin(context);
    phy_bigint *coefficients = NULL;
    size_t canonical_count = 0u;
    size_t canonical_capacity = 0u;
    if (status == PHY_OK) {
        status = load_canonical_coefficients(
            context, coefficient_text, coefficient_count, &coefficients,
            &canonical_count, &canonical_capacity);
    }

    sturm_chain chain;
    memset(&chain, 0, sizeof chain);
    if (status == PHY_OK) {
        status = sturm_chain_build(
            context, coefficients, canonical_count, &chain);
    }
    phy_bigrat lower;
    phy_bigrat upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    bool lower_initialized = false;
    bool upper_initialized = false;
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &lower);
        lower_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &upper);
        upper_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = cauchy_interval(
            context, coefficients, canonical_count, &lower, &upper);
    }
    uint32_t lower_variations = 0u;
    uint32_t upper_variations = 0u;
    if (status == PHY_OK) {
        status = sturm_variations(
            context, &chain, &lower, &lower_variations);
    }
    if (status == PHY_OK) {
        status = sturm_variations(
            context, &chain, &upper, &upper_variations);
    }
    if (status == PHY_OK && lower_variations < upper_variations) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    const size_t root_count =
        status == PHY_OK
            ? (size_t)(lower_variations - upper_variations)
            : 0u;
    if (status == PHY_OK && root_count > value_capacity) {
        status = PHY_ERR_TERM_LIMIT;
    }

    isolation_interval *intervals = NULL;
    size_t interval_bytes = 0u;
    if (status == PHY_OK && root_count != 0u) {
        if (!checked_multiply(
                root_count, sizeof(*intervals), &interval_bytes)) {
            status = PHY_ERR_MEMORY_LIMIT;
        } else {
            status = metadata_allocate(
                context, interval_bytes, (void **)&intervals);
        }
    }
    size_t initialized_intervals = 0u;
    while (status == PHY_OK && initialized_intervals < root_count) {
        status = phy_bigrat_init(
            context->exact,
            &intervals[initialized_intervals].lower);
        intervals[initialized_intervals].lower_initialized =
            status == PHY_OK;
        if (status == PHY_OK) {
            status = phy_bigrat_init(
                context->exact,
                &intervals[initialized_intervals].upper);
            intervals[initialized_intervals].upper_initialized =
                status == PHY_OK;
        }
        if (status == PHY_OK) {
            initialized_intervals++;
        }
    }
    size_t interval_count = root_count == 0u ? 0u : 1u;
    if (status == PHY_OK && root_count != 0u) {
        status = phy_bigrat_copy(&lower, &intervals[0].lower);
    }
    if (status == PHY_OK && root_count != 0u) {
        status = phy_bigrat_copy(&upper, &intervals[0].upper);
    }
    if (status == PHY_OK && root_count != 0u) {
        intervals[0].lower_variations = lower_variations;
        intervals[0].upper_variations = upper_variations;
    }

    phy_bigrat split;
    memset(&split, 0, sizeof split);
    bool split_initialized = false;
    if (status == PHY_OK && root_count != 0u) {
        status = phy_bigrat_init(context->exact, &split);
        split_initialized = status == PHY_OK;
    }
    size_t current = 0u;
    size_t splits = 0u;
    const size_t degree = canonical_count == 0u
                              ? 0u
                              : canonical_count - 1u;
    const size_t max_splits =
        degree > (size_t)-1 / context->limits.max_refinements
            ? (size_t)-1
            : degree * context->limits.max_refinements;
    while (status == PHY_OK && current < interval_count) {
        isolation_interval *interval = &intervals[current];
        if (interval->lower_variations < interval->upper_variations) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
            break;
        }
        const uint32_t count =
            interval->lower_variations - interval->upper_variations;
        if (count == 1u) {
            current++;
            continue;
        }
        if (count == 0u || splits >= max_splits) {
            status =
                count == 0u ? PHY_ERR_CORRUPT_DOCUMENT : PHY_ERR_TIMEOUT;
            break;
        }
        uint32_t split_variations = 0u;
        status = find_nonroot_split(
            context, &chain, interval, degree, &split,
            &split_variations);
        if (status != PHY_OK) {
            break;
        }
        if (split_variations > interval->lower_variations ||
            split_variations < interval->upper_variations) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
            break;
        }
        const uint32_t left_count =
            interval->lower_variations - split_variations;
        const uint32_t right_count =
            split_variations - interval->upper_variations;
        if (left_count != 0u && right_count != 0u) {
            if (interval_count >= root_count) {
                status = PHY_ERR_CORRUPT_DOCUMENT;
                break;
            }
            isolation_interval *right =
                &intervals[interval_count++];
            status = phy_bigrat_copy(&split, &right->lower);
            if (status == PHY_OK) {
                status = phy_bigrat_copy(
                    &interval->upper, &right->upper);
            }
            if (status == PHY_OK) {
                right->lower_variations = split_variations;
                right->upper_variations =
                    interval->upper_variations;
                status =
                    phy_bigrat_copy(&split, &interval->upper);
            }
            if (status == PHY_OK) {
                interval->upper_variations = split_variations;
            }
        } else if (left_count != 0u) {
            status = phy_bigrat_copy(&split, &interval->upper);
            if (status == PHY_OK) {
                interval->upper_variations = split_variations;
            }
        } else if (right_count != 0u) {
            status = phy_bigrat_copy(&split, &interval->lower);
            if (status == PHY_OK) {
                interval->lower_variations = split_variations;
            }
        } else {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        }
        splits++;
    }

    for (size_t index = 1u;
         status == PHY_OK && index < interval_count; ++index) {
        size_t position = index;
        while (position > 0u) {
            int comparison = 0;
            status = phy_bigrat_compare(
                &intervals[position].lower,
                &intervals[position - 1u].lower, &comparison);
            if (status != PHY_OK || comparison >= 0) {
                break;
            }
            status = phy_bigrat_swap(
                &intervals[position].lower,
                &intervals[position - 1u].lower);
            if (status == PHY_OK) {
                status = phy_bigrat_swap(
                    &intervals[position].upper,
                    &intervals[position - 1u].upper);
            }
            if (status == PHY_OK) {
                const uint32_t lower_v =
                    intervals[position].lower_variations;
                const uint32_t upper_v =
                    intervals[position].upper_variations;
                intervals[position].lower_variations =
                    intervals[position - 1u].lower_variations;
                intervals[position].upper_variations =
                    intervals[position - 1u].upper_variations;
                intervals[position - 1u].lower_variations = lower_v;
                intervals[position - 1u].upper_variations = upper_v;
            }
            position--;
        }
    }

    /*
     * The intervals themselves are now the certificates. The Sturm chain is
     * the largest temporary metadata block, so release it before copying the
     * defining polynomial into each returned value.
     */
    sturm_chain_destroy(context, &chain);
    size_t created = 0u;
    while (status == PHY_OK && created < interval_count) {
        status = value_from_certificate(
            context, coefficients, canonical_count,
            &intervals[created].lower, &intervals[created].upper,
            &out_values[created]);
        if (status == PHY_OK) {
            created++;
        }
    }
    if (status != PHY_OK) {
        for (size_t index = 0u; index < created; ++index) {
            phy_real_algebraic_destroy(out_values[index]);
            out_values[index] = NULL;
        }
    }
    if (split_initialized) {
        phy_bigrat_destroy(&split);
    }
    isolation_intervals_destroy(
        context, intervals, root_count);
    if (upper_initialized) {
        phy_bigrat_destroy(&upper);
    }
    if (lower_initialized) {
        phy_bigrat_destroy(&lower);
    }
    sturm_chain_destroy(context, &chain);
    destroy_coefficient_array(
        context, coefficients, canonical_capacity);
    if (status == PHY_OK) {
        *out_count = created;
    }
    return call_end(context, status);
}

void phy_real_algebraic_destroy(phy_real_algebraic *value)
{
    if (!value_valid_handle(value)) {
        return;
    }
    destroy_value_internal(value);
}

phy_status phy_real_algebraic_validate(
    const phy_real_algebraic *value)
{
    if (!value_valid_handle(value) || !value->linked ||
        value->coefficients == NULL ||
        value->coefficient_count < 2u ||
        value->coefficient_count > value->coefficient_capacity ||
        value->coefficient_count - 1u >
            value->context->limits.max_degree ||
        !value->lower_initialized || !value->upper_initialized ||
        phy_bigint_sign(
            &value->coefficients[value->coefficient_count - 1u]) <= 0) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    for (size_t i = 0u; i < value->coefficient_capacity; ++i) {
        if (phy_bigint_validate(&value->coefficients[i]) != PHY_OK) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
    }
    if (phy_bigrat_validate(&value->lower) != PHY_OK ||
        phy_bigrat_validate(&value->upper) != PHY_OK) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    int order = 0;
    phy_status status =
        phy_bigrat_compare(&value->lower, &value->upper, &order);
    if (status != PHY_OK) {
        return status;
    }
    return (value->rational ? order == 0 : order < 0)
               ? PHY_OK
               : PHY_ERR_CORRUPT_DOCUMENT;
}

phy_status phy_algebraic_validate(
    const phy_algebraic_context *context)
{
    if (!context_valid(context) ||
        context->metadata_bytes < sizeof *context ||
        context->metadata_bytes > context->limits.max_metadata_bytes ||
        phy_exact_validate(context->exact) != PHY_OK) {
        return PHY_ERR_CORRUPT_DOCUMENT;
    }
    size_t count = 0u;
    size_t expected_bytes = sizeof *context;
    const phy_real_algebraic *previous = NULL;
    for (const phy_real_algebraic *value = context->values;
         value != NULL; value = value->next) {
        if (value->previous != previous ||
            value->context != context ||
            phy_real_algebraic_validate(value) != PHY_OK ||
            count >= context->value_count) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        size_t coefficient_bytes = 0u;
        if (!checked_multiply(
                value->coefficient_capacity, sizeof(phy_bigint),
                &coefficient_bytes) ||
            expected_bytes > (size_t)-1 - sizeof *value ||
            expected_bytes + sizeof *value >
                (size_t)-1 - coefficient_bytes) {
            return PHY_ERR_CORRUPT_DOCUMENT;
        }
        expected_bytes += sizeof *value + coefficient_bytes;
        previous = value;
        count++;
    }
    return count == context->value_count &&
                   expected_bytes == context->metadata_bytes
               ? PHY_OK
               : PHY_ERR_CORRUPT_DOCUMENT;
}

size_t phy_real_algebraic_degree(const phy_real_algebraic *value)
{
    return value_valid_handle(value) && value->coefficient_count != 0u
               ? value->coefficient_count - 1u
               : 0u;
}

phy_status phy_real_algebraic_write_coefficient(
    const phy_real_algebraic *value, size_t degree, char *buffer,
    size_t capacity, size_t *out_required)
{
    if (!value_valid_handle(value) ||
        degree >= value->coefficient_count) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_bigint_write(
        &value->coefficients[degree], buffer, capacity, out_required);
}

phy_status phy_real_algebraic_write_lower(
    const phy_real_algebraic *value, char *buffer, size_t capacity,
    size_t *out_required)
{
    if (!value_valid_handle(value)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_bigrat_write(
        &value->lower, buffer, capacity, out_required);
}

phy_status phy_real_algebraic_write_upper(
    const phy_real_algebraic *value, char *buffer, size_t capacity,
    size_t *out_required)
{
    if (!value_valid_handle(value)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    return phy_bigrat_write(
        &value->upper, buffer, capacity, out_required);
}

bool phy_real_algebraic_is_rational(
    const phy_real_algebraic *value)
{
    return value_valid_handle(value) && value->rational;
}

phy_status phy_real_algebraic_refine(
    phy_real_algebraic *value, uint32_t rounds)
{
    if (!value_valid_handle(value) ||
        rounds > value->context->limits.max_refinements) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_algebraic_context *context = value->context;
    phy_status status = call_begin(context);
    phy_bigrat lower;
    phy_bigrat upper;
    phy_bigrat midpoint;
    phy_bigrat half;
    phy_bigrat evaluated;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    memset(&midpoint, 0, sizeof midpoint);
    memset(&half, 0, sizeof half);
    memset(&evaluated, 0, sizeof evaluated);
    bool lower_initialized = false;
    bool upper_initialized = false;
    bool midpoint_initialized = false;
    bool half_initialized = false;
    bool evaluated_initialized = false;
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &lower);
        lower_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &upper);
        upper_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &midpoint);
        midpoint_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &half);
        half_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &evaluated);
        evaluated_initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(&value->lower, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(&value->upper, &upper);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&half, 1, 2);
    }

    bool rational = value->rational;
    for (uint32_t round = 0u;
         status == PHY_OK && round < rounds && !rational; ++round) {
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigrat_add(&lower, &upper, &midpoint);
        }
        if (status == PHY_OK) {
            status =
                phy_bigrat_multiply(&midpoint, &half, &midpoint);
        }
        if (status == PHY_OK) {
            status = integer_polynomial_evaluate(
                value, &midpoint, &evaluated);
        }
        const int midpoint_sign =
            status == PHY_OK ? phy_bigrat_sign(&evaluated) : 0;
        if (status == PHY_OK && midpoint_sign == 0) {
            status = phy_bigrat_copy(&midpoint, &lower);
            if (status == PHY_OK) {
                status = phy_bigrat_copy(&midpoint, &upper);
            }
            rational = status == PHY_OK;
            continue;
        }
        if (status == PHY_OK) {
            status = integer_polynomial_evaluate(
                value, &lower, &evaluated);
        }
        const int lower_sign =
            status == PHY_OK ? phy_bigrat_sign(&evaluated) : 0;
        if (status == PHY_OK && lower_sign == 0) {
            status = PHY_ERR_CORRUPT_DOCUMENT;
        } else if (status == PHY_OK &&
                   lower_sign != midpoint_sign) {
            status = phy_bigrat_copy(&midpoint, &upper);
        } else if (status == PHY_OK) {
            status = phy_bigrat_copy(&midpoint, &lower);
        }
    }

    if (status == PHY_OK) {
        status = phy_bigrat_swap(&value->lower, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_swap(&value->upper, &upper);
    }
    if (status == PHY_OK) {
        value->rational = rational;
    }

    if (evaluated_initialized) {
        phy_bigrat_destroy(&evaluated);
    }
    if (half_initialized) {
        phy_bigrat_destroy(&half);
    }
    if (midpoint_initialized) {
        phy_bigrat_destroy(&midpoint);
    }
    if (upper_initialized) {
        phy_bigrat_destroy(&upper);
    }
    if (lower_initialized) {
        phy_bigrat_destroy(&lower);
    }
    return call_end(context, status);
}

static bool same_defining_polynomial(
    const phy_real_algebraic *left,
    const phy_real_algebraic *right)
{
    if (left->coefficient_count != right->coefficient_count) {
        return false;
    }
    for (size_t i = 0u; i < left->coefficient_count; ++i) {
        if (phy_bigint_compare(
                &left->coefficients[i], &right->coefficients[i]) != 0) {
            return false;
        }
    }
    return true;
}

static phy_status rational_point_matches(
    phy_real_algebraic *point_value,
    phy_real_algebraic *candidate, bool *out_matches)
{
    *out_matches = false;
    const phy_bigrat *point = &point_value->lower;
    int order = 0;
    phy_status status =
        phy_bigrat_compare(point, &candidate->lower, &order);
    if (status == PHY_OK && order < 0) {
        return PHY_OK;
    }
    if (status == PHY_OK) {
        status = phy_bigrat_compare(
            point, &candidate->upper, &order);
    }
    if (status == PHY_OK && order > 0) {
        return PHY_OK;
    }
    phy_bigrat evaluated;
    memset(&evaluated, 0, sizeof evaluated);
    bool initialized = false;
    if (status == PHY_OK) {
        status = phy_bigrat_init(
            candidate->context->exact, &evaluated);
        initialized = status == PHY_OK;
    }
    if (status == PHY_OK) {
        status = integer_polynomial_evaluate(
            candidate, point, &evaluated);
    }
    if (status == PHY_OK) {
        *out_matches = phy_bigrat_sign(&evaluated) == 0;
    }
    if (initialized) {
        phy_bigrat_destroy(&evaluated);
    }
    return status;
}

phy_status phy_real_algebraic_compare(
    phy_real_algebraic *left, phy_real_algebraic *right,
    int *out_comparison)
{
    if (!value_valid_handle(left) || !value_valid_handle(right) ||
        left->context != right->context || out_comparison == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    if (left == right) {
        *out_comparison = 0;
        return PHY_OK;
    }
    phy_algebraic_context *context = left->context;
    phy_status status = call_begin(context);
    for (uint32_t round = 0u;
         status == PHY_OK &&
         round <= context->limits.max_refinements; ++round) {
        int order = 0;
        status =
            phy_bigrat_compare(&left->upper, &right->lower, &order);
        if (status == PHY_OK && order < 0) {
            *out_comparison = -1;
            return call_end(context, PHY_OK);
        }
        status =
            status == PHY_OK
                ? phy_bigrat_compare(
                      &right->upper, &left->lower, &order)
                : status;
        if (status == PHY_OK && order < 0) {
            *out_comparison = 1;
            return call_end(context, PHY_OK);
        }
        if (status != PHY_OK) {
            break;
        }

        if (left->rational && right->rational) {
            status = phy_bigrat_compare(
                &left->lower, &right->lower, &order);
            if (status == PHY_OK) {
                *out_comparison = order;
            }
            return call_end(context, status);
        }

        if (left->rational || right->rational) {
            bool matches = false;
            status =
                left->rational
                    ? rational_point_matches(left, right, &matches)
                    : rational_point_matches(right, left, &matches);
            if (status == PHY_OK && matches) {
                *out_comparison = 0;
                return call_end(context, PHY_OK);
            }
        }

        if (same_defining_polynomial(left, right)) {
            const phy_bigrat *intersection_lower = &left->lower;
            const phy_bigrat *intersection_upper = &left->upper;
            status = phy_bigrat_compare(
                &left->lower, &right->lower, &order);
            if (status == PHY_OK && order < 0) {
                intersection_lower = &right->lower;
            }
            if (status == PHY_OK) {
                status = phy_bigrat_compare(
                    &left->upper, &right->upper, &order);
            }
            if (status == PHY_OK && order > 0) {
                intersection_upper = &right->upper;
            }
            if (status == PHY_OK) {
                status = phy_bigrat_compare(
                    intersection_lower, intersection_upper, &order);
            }
            if (status == PHY_OK && order < 0) {
                uint32_t roots = 0u;
                status = root_count_for_coefficients(
                    context, left->coefficients,
                    left->coefficient_count, intersection_lower,
                    intersection_upper, &roots);
                if (status == PHY_OK && roots == 1u) {
                    *out_comparison = 0;
                    return call_end(context, PHY_OK);
                }
            }
        }

        if (status == PHY_OK &&
            round < context->limits.max_refinements) {
            status = phy_real_algebraic_refine(left, 1u);
        }
        if (status == PHY_OK &&
            round < context->limits.max_refinements) {
            status = phy_real_algebraic_refine(right, 1u);
        }
    }
    if (status == PHY_OK) {
        status = PHY_ERR_UNSUPPORTED;
    }
    return call_end(context, status);
}

/* ------------------------------------------------ resultant arithmetic */

typedef enum {
    ALGEBRAIC_BINARY_SUM = 0,
    ALGEBRAIC_BINARY_PRODUCT
} algebraic_binary_operation;

static phy_status rational_polynomial_copy(
    phy_algebraic_context *context,
    const rational_polynomial *source,
    rational_polynomial *out_copy)
{
    phy_status status =
        rational_polynomial_init(context, source->count, out_copy);
    for (size_t index = 0u;
         status == PHY_OK && index < source->count; ++index) {
        status = phy_bigrat_copy(
            &source->coefficients[index],
            &out_copy->coefficients[index]);
    }
    if (status != PHY_OK) {
        rational_polynomial_destroy(context, out_copy);
    }
    return status;
}

static phy_status rational_polynomial_exact_quotient(
    phy_algebraic_context *context,
    const rational_polynomial *dividend,
    const rational_polynomial *divisor,
    rational_polynomial *out_quotient)
{
    if (rational_polynomial_is_zero(divisor) ||
        dividend->count < divisor->count) {
        return PHY_ERR_DOMAIN;
    }
    const size_t quotient_count =
        dividend->count - divisor->count + 1u;
    phy_status status = rational_polynomial_init(
        context, quotient_count, out_quotient);
    rational_polynomial remainder;
    memset(&remainder, 0, sizeof remainder);
    if (status == PHY_OK) {
        status = rational_polynomial_copy(
            context, dividend, &remainder);
    }
    phy_bigrat factor;
    phy_bigrat product;
    memset(&factor, 0, sizeof factor);
    memset(&product, 0, sizeof product);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &factor);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &product);
    }
    while (status == PHY_OK &&
           !rational_polynomial_is_zero(&remainder) &&
           remainder.count >= divisor->count) {
        const size_t shift = remainder.count - divisor->count;
        status = algebraic_step(context, 1u);
        if (status == PHY_OK) {
            status = phy_bigrat_divide(
                &remainder.coefficients[remainder.count - 1u],
                &divisor->coefficients[divisor->count - 1u],
                &factor);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_copy(
                &factor, &out_quotient->coefficients[shift]);
        }
        for (size_t index = 0u;
             status == PHY_OK && index < divisor->count; ++index) {
            status = phy_bigrat_multiply(
                &factor, &divisor->coefficients[index], &product);
            if (status == PHY_OK) {
                status = phy_bigrat_subtract(
                    &remainder.coefficients[index + shift], &product,
                    &remainder.coefficients[index + shift]);
            }
        }
        rational_polynomial_trim(&remainder);
    }
    if (status == PHY_OK &&
        !rational_polynomial_is_zero(&remainder)) {
        status = PHY_ERR_CORRUPT_DOCUMENT;
    }
    phy_bigrat_destroy(&product);
    phy_bigrat_destroy(&factor);
    rational_polynomial_destroy(context, &remainder);
    if (status != PHY_OK) {
        rational_polynomial_destroy(context, out_quotient);
    } else {
        rational_polynomial_trim(out_quotient);
    }
    return status;
}

static phy_status rational_polynomial_gcd(
    phy_algebraic_context *context,
    const rational_polynomial *left,
    const rational_polynomial *right,
    rational_polynomial *out_gcd)
{
    rational_polynomial a;
    rational_polynomial b;
    rational_polynomial remainder;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    memset(&remainder, 0, sizeof remainder);
    phy_status status =
        rational_polynomial_copy(context, left, &a);
    if (status == PHY_OK) {
        status = rational_polynomial_copy(context, right, &b);
    }
    if (status == PHY_OK) {
        status = rational_polynomial_normalize(context, &a);
    }
    if (status == PHY_OK) {
        status = rational_polynomial_normalize(context, &b);
    }
    while (status == PHY_OK &&
           !rational_polynomial_is_zero(&b)) {
        status = rational_polynomial_remainder(
            context, &a, &b, &remainder);
        if (status == PHY_OK &&
            !rational_polynomial_is_zero(&remainder)) {
            status =
                rational_polynomial_normalize(context, &remainder);
        }
        if (status == PHY_OK) {
            rational_polynomial_destroy(context, &a);
            a = b;
            b = remainder;
            memset(&remainder, 0, sizeof remainder);
        }
    }
    if (status == PHY_OK) {
        *out_gcd = a;
        memset(&a, 0, sizeof a);
    }
    rational_polynomial_destroy(context, &remainder);
    rational_polynomial_destroy(context, &b);
    rational_polynomial_destroy(context, &a);
    return status;
}

static phy_status rational_polynomial_square_free(
    phy_algebraic_context *context,
    const rational_polynomial *polynomial,
    rational_polynomial *out_square_free)
{
    rational_polynomial derivative;
    rational_polynomial common;
    memset(&derivative, 0, sizeof derivative);
    memset(&common, 0, sizeof common);
    phy_status status = rational_polynomial_derivative(
        context, polynomial, &derivative);
    if (status == PHY_OK) {
        status = rational_polynomial_gcd(
            context, polynomial, &derivative, &common);
    }
    if (status == PHY_OK) {
        status = rational_polynomial_exact_quotient(
            context, polynomial, &common, out_square_free);
    }
    if (status == PHY_OK) {
        status = rational_polynomial_normalize(
            context, out_square_free);
    }
    rational_polynomial_destroy(context, &common);
    rational_polynomial_destroy(context, &derivative);
    return status;
}

static phy_status rational_resultant(
    phy_algebraic_context *context,
    const rational_polynomial *left,
    const rational_polynomial *right,
    phy_bigrat *out_result)
{
    const size_t left_degree = left->count - 1u;
    const size_t right_degree = right->count - 1u;
    const size_t order = left_degree + right_degree;
    if (left_degree == 0u || right_degree == 0u || order == 0u) {
        return PHY_ERR_DOMAIN;
    }
    size_t entries = 0u;
    size_t bytes = 0u;
    if (!checked_multiply(order, order, &entries) ||
        !checked_multiply(entries, sizeof(phy_bigrat), &bytes)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_bigrat *matrix = NULL;
    phy_status status =
        metadata_allocate(context, bytes, (void **)&matrix);
    size_t initialized = 0u;
    while (status == PHY_OK && initialized < entries) {
        status = phy_bigrat_init(
            context->exact, &matrix[initialized]);
        if (status == PHY_OK) {
            initialized++;
            status = phy_bigrat_set_i64(
                &matrix[initialized - 1u], 0, 1);
        }
    }
    for (size_t row = 0u;
         status == PHY_OK && row < right_degree; ++row) {
        for (size_t column = 0u;
             status == PHY_OK && column <= left_degree; ++column) {
            status = phy_bigrat_copy(
                &left->coefficients[column],
                &matrix[row * order + row + column]);
        }
    }
    for (size_t row = 0u;
         status == PHY_OK && row < left_degree; ++row) {
        for (size_t column = 0u;
             status == PHY_OK && column <= right_degree; ++column) {
            status = phy_bigrat_copy(
                &right->coefficients[column],
                &matrix[(right_degree + row) * order +
                        row + column]);
        }
    }

    phy_bigrat previous;
    phy_bigrat first_product;
    phy_bigrat second_product;
    phy_bigrat numerator;
    memset(&previous, 0, sizeof previous);
    memset(&first_product, 0, sizeof first_product);
    memset(&second_product, 0, sizeof second_product);
    memset(&numerator, 0, sizeof numerator);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &previous);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &first_product);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &second_product);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &numerator);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&previous, 1, 1);
    }
    int determinant_sign = 1;
    bool singular = false;
    for (size_t pivot_index = 0u;
         status == PHY_OK && !singular &&
         pivot_index + 1u < order; ++pivot_index) {
        size_t pivot_row = pivot_index;
        while (pivot_row < order &&
               phy_bigrat_sign(
                   &matrix[pivot_row * order + pivot_index]) == 0) {
            pivot_row++;
        }
        if (pivot_row == order) {
            singular = true;
            break;
        }
        if (pivot_row != pivot_index) {
            for (size_t column = 0u; column < order; ++column) {
                status = phy_bigrat_swap(
                    &matrix[pivot_index * order + column],
                    &matrix[pivot_row * order + column]);
                if (status != PHY_OK) {
                    break;
                }
            }
            determinant_sign = -determinant_sign;
        }
        const phy_bigrat *pivot =
            &matrix[pivot_index * order + pivot_index];
        for (size_t row = pivot_index + 1u;
             status == PHY_OK && row < order; ++row) {
            for (size_t column = pivot_index + 1u;
                 status == PHY_OK && column < order; ++column) {
                status = algebraic_step(context, 3u);
                if (status == PHY_OK) {
                    status = phy_bigrat_multiply(
                        &matrix[row * order + column], pivot,
                        &first_product);
                }
                if (status == PHY_OK) {
                    status = phy_bigrat_multiply(
                        &matrix[row * order + pivot_index],
                        &matrix[pivot_index * order + column],
                        &second_product);
                }
                if (status == PHY_OK) {
                    status = phy_bigrat_subtract(
                        &first_product, &second_product, &numerator);
                }
                if (status == PHY_OK) {
                    status = phy_bigrat_divide(
                        &numerator, &previous,
                        &matrix[row * order + column]);
                }
            }
            if (status == PHY_OK) {
                status = phy_bigrat_set_i64(
                    &matrix[row * order + pivot_index], 0, 1);
            }
        }
        if (status == PHY_OK) {
            status = phy_bigrat_copy(pivot, &previous);
        }
    }
    if (status == PHY_OK) {
        if (singular) {
            status = phy_bigrat_set_i64(out_result, 0, 1);
        } else if (determinant_sign < 0) {
            status = phy_bigrat_negate(
                &matrix[entries - 1u], out_result);
        } else {
            status = phy_bigrat_copy(
                &matrix[entries - 1u], out_result);
        }
    }
    phy_bigrat_destroy(&numerator);
    phy_bigrat_destroy(&second_product);
    phy_bigrat_destroy(&first_product);
    phy_bigrat_destroy(&previous);
    for (size_t index = 0u; index < initialized; ++index) {
        phy_bigrat_destroy(&matrix[index]);
    }
    metadata_free(context, matrix, bytes);
    return status;
}

static phy_status resultant_sample_polynomial(
    phy_algebraic_context *context,
    const phy_real_algebraic *value, const phy_bigrat *sample,
    algebraic_binary_operation operation,
    rational_polynomial *out_polynomial)
{
    if (operation == ALGEBRAIC_BINARY_SUM) {
        phy_bigrat minus_one;
        memset(&minus_one, 0, sizeof minus_one);
        phy_status status =
            phy_bigrat_init(context->exact, &minus_one);
        if (status == PHY_OK) {
            status = phy_bigrat_set_i64(&minus_one, -1, 1);
        }
        if (status == PHY_OK) {
            status = rational_polynomial_affine_transform(
                context, value, &minus_one, sample, out_polynomial);
        }
        phy_bigrat_destroy(&minus_one);
        return status;
    }

    const size_t degree = value->coefficient_count - 1u;
    phy_status status = rational_polynomial_init(
        context, value->coefficient_count, out_polynomial);
    phy_bigrat sample_power;
    phy_bigrat coefficient;
    memset(&sample_power, 0, sizeof sample_power);
    memset(&coefficient, 0, sizeof coefficient);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &sample_power);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &coefficient);
    }
    for (size_t source = 0u;
         status == PHY_OK && source <= degree; ++source) {
        status = phy_bigrat_pow_i32(
            sample, (int32_t)source, &sample_power);
        if (status == PHY_OK) {
            status = phy_bigrat_set_bigint(
                &value->coefficients[source], &coefficient);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_multiply(
                &coefficient, &sample_power,
                &out_polynomial->coefficients[degree - source]);
        }
    }
    phy_bigrat_destroy(&coefficient);
    phy_bigrat_destroy(&sample_power);
    if (status != PHY_OK) {
        rational_polynomial_destroy(context, out_polynomial);
    } else {
        rational_polynomial_trim(out_polynomial);
    }
    return status;
}

static int64_t interpolation_abscissa(size_t index)
{
    if (index == 0u) {
        return 0;
    }
    const int64_t magnitude = (int64_t)((index + 1u) / 2u);
    return (index & 1u) != 0u ? magnitude : -magnitude;
}

static phy_status resultant_polynomial(
    phy_algebraic_context *context,
    const phy_real_algebraic *left,
    const phy_real_algebraic *right,
    algebraic_binary_operation operation,
    rational_polynomial *out_polynomial)
{
    const size_t left_degree = left->coefficient_count - 1u;
    const size_t right_degree = right->coefficient_count - 1u;
    if (left_degree > context->limits.max_degree / right_degree) {
        return PHY_ERR_TERM_LIMIT;
    }
    const size_t degree_bound = left_degree * right_degree;
    const size_t sample_count = degree_bound + 1u;
    size_t bytes = 0u;
    if (!checked_multiply(
            sample_count * 2u, sizeof(phy_bigrat), &bytes)) {
        return PHY_ERR_MEMORY_LIMIT;
    }
    phy_bigrat *samples = NULL;
    phy_status status =
        metadata_allocate(context, bytes, (void **)&samples);
    size_t initialized = 0u;
    while (status == PHY_OK && initialized < sample_count * 2u) {
        status = phy_bigrat_init(
            context->exact, &samples[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    rational_polynomial left_polynomial;
    memset(&left_polynomial, 0, sizeof left_polynomial);
    if (status == PHY_OK) {
        status = rational_polynomial_from_integers(
            context, left->coefficients, left->coefficient_count,
            &left_polynomial);
    }
    for (size_t index = 0u;
         status == PHY_OK && index < sample_count; ++index) {
        status = phy_bigrat_set_i64(
            &samples[index], interpolation_abscissa(index), 1);
        rational_polynomial sampled;
        memset(&sampled, 0, sizeof sampled);
        if (status == PHY_OK) {
            status = resultant_sample_polynomial(
                context, right, &samples[index], operation, &sampled);
        }
        if (status == PHY_OK) {
            status = rational_resultant(
                context, &left_polynomial, &sampled,
                &samples[sample_count + index]);
        }
        rational_polynomial_destroy(context, &sampled);
    }

    /* In-place Newton divided differences. */
    phy_bigrat difference;
    phy_bigrat spacing;
    memset(&difference, 0, sizeof difference);
    memset(&spacing, 0, sizeof spacing);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &difference);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &spacing);
    }
    for (size_t order = 1u;
         status == PHY_OK && order < sample_count; ++order) {
        for (size_t index = sample_count;
             status == PHY_OK && index-- > order;) {
            status = phy_bigrat_subtract(
                &samples[sample_count + index],
                &samples[sample_count + index - 1u], &difference);
            if (status == PHY_OK) {
                status = phy_bigrat_subtract(
                    &samples[index], &samples[index - order], &spacing);
            }
            if (status == PHY_OK) {
                status = phy_bigrat_divide(
                    &difference, &spacing,
                    &samples[sample_count + index]);
            }
        }
    }

    rational_polynomial current;
    rational_polynomial next;
    memset(&current, 0, sizeof current);
    memset(&next, 0, sizeof next);
    if (status == PHY_OK) {
        status = rational_polynomial_init(
            context, sample_count, &current);
    }
    if (status == PHY_OK) {
        status = rational_polynomial_init(
            context, sample_count, &next);
    }
    if (status == PHY_OK) {
        current.count = 1u;
        status = phy_bigrat_copy(
            &samples[sample_count + degree_bound],
            &current.coefficients[0]);
    }
    phy_bigrat product;
    memset(&product, 0, sizeof product);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &product);
    }
    for (size_t stage = degree_bound;
         status == PHY_OK && stage-- != 0u;) {
        for (size_t index = 0u;
             status == PHY_OK && index < next.capacity; ++index) {
            status = phy_bigrat_set_i64(
                &next.coefficients[index], 0, 1);
        }
        next.count = current.count + 1u;
        for (size_t index = 0u;
             status == PHY_OK && index < current.count; ++index) {
            status = phy_bigrat_multiply(
                &current.coefficients[index], &samples[stage],
                &product);
            if (status == PHY_OK) {
                status = phy_bigrat_subtract(
                    &next.coefficients[index], &product,
                    &next.coefficients[index]);
            }
            if (status == PHY_OK) {
                status = phy_bigrat_add(
                    &next.coefficients[index + 1u],
                    &current.coefficients[index],
                    &next.coefficients[index + 1u]);
            }
        }
        if (status == PHY_OK) {
            status = phy_bigrat_add(
                &next.coefficients[0],
                &samples[sample_count + stage],
                &next.coefficients[0]);
        }
        if (status == PHY_OK) {
            const rational_polynomial swap = current;
            current = next;
            next = swap;
        }
    }
    phy_bigrat_destroy(&product);
    if (status == PHY_OK) {
        rational_polynomial_trim(&current);
        *out_polynomial = current;
        memset(&current, 0, sizeof current);
    }
    rational_polynomial_destroy(context, &next);
    rational_polynomial_destroy(context, &current);
    phy_bigrat_destroy(&spacing);
    phy_bigrat_destroy(&difference);
    rational_polynomial_destroy(context, &left_polynomial);
    for (size_t index = 0u; index < initialized; ++index) {
        phy_bigrat_destroy(&samples[index]);
    }
    metadata_free(context, samples, bytes);
    return status;
}

static phy_status binary_interval(
    phy_algebraic_context *context,
    const phy_real_algebraic *left,
    const phy_real_algebraic *right,
    algebraic_binary_operation operation,
    phy_bigrat *out_lower, phy_bigrat *out_upper)
{
    if (operation == ALGEBRAIC_BINARY_SUM) {
        phy_status status = phy_bigrat_add(
            &left->lower, &right->lower, out_lower);
        return status == PHY_OK
                   ? phy_bigrat_add(
                         &left->upper, &right->upper, out_upper)
                   : status;
    }
    phy_bigrat products[4];
    memset(products, 0, sizeof products);
    phy_status status = PHY_OK;
    size_t initialized = 0u;
    while (status == PHY_OK && initialized < 4u) {
        status = phy_bigrat_init(
            context->exact, &products[initialized]);
        if (status == PHY_OK) {
            initialized++;
        }
    }
    const phy_bigrat *left_points[2] = {&left->lower, &left->upper};
    const phy_bigrat *right_points[2] = {&right->lower, &right->upper};
    for (size_t i = 0u; status == PHY_OK && i < 2u; ++i) {
        for (size_t j = 0u; status == PHY_OK && j < 2u; ++j) {
            status = phy_bigrat_multiply(
                left_points[i], right_points[j],
                &products[i * 2u + j]);
        }
    }
    size_t minimum = 0u;
    size_t maximum = 0u;
    for (size_t index = 1u;
         status == PHY_OK && index < 4u; ++index) {
        int comparison = 0;
        status = phy_bigrat_compare(
            &products[index], &products[minimum], &comparison);
        if (status == PHY_OK && comparison < 0) {
            minimum = index;
        }
        if (status == PHY_OK) {
            status = phy_bigrat_compare(
                &products[index], &products[maximum], &comparison);
        }
        if (status == PHY_OK && comparison > 0) {
            maximum = index;
        }
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(&products[minimum], out_lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(&products[maximum], out_upper);
    }
    for (size_t index = 0u; index < initialized; ++index) {
        phy_bigrat_destroy(&products[index]);
    }
    return status;
}

static phy_status publish_resultant_value(
    phy_algebraic_context *context,
    const phy_real_algebraic *left,
    const phy_real_algebraic *right,
    algebraic_binary_operation operation,
    const phy_bigint *coefficients, size_t coefficient_count,
    phy_real_algebraic **out_value)
{
    if (coefficient_count == 2u) {
        phy_bigrat numerator;
        phy_bigrat denominator;
        phy_bigrat root;
        memset(&numerator, 0, sizeof numerator);
        memset(&denominator, 0, sizeof denominator);
        memset(&root, 0, sizeof root);
        phy_status status =
            phy_bigrat_init(context->exact, &numerator);
        if (status == PHY_OK) {
            status = phy_bigrat_init(context->exact, &denominator);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_init(context->exact, &root);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_set_bigint(
                &coefficients[0], &numerator);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_negate(&numerator, &numerator);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_set_bigint(
                &coefficients[1], &denominator);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_divide(
                &numerator, &denominator, &root);
        }
        if (status == PHY_OK) {
            status = value_from_rational_point(
                context, &root, out_value);
        }
        phy_bigrat_destroy(&root);
        phy_bigrat_destroy(&denominator);
        phy_bigrat_destroy(&numerator);
        return status;
    }

    phy_real_algebraic *left_work = NULL;
    phy_real_algebraic *right_work = NULL;
    phy_status status = value_from_certificate(
        context, left->coefficients, left->coefficient_count,
        &left->lower, &left->upper, &left_work);
    if (status == PHY_OK) {
        status = value_from_certificate(
            context, right->coefficients, right->coefficient_count,
            &right->lower, &right->upper, &right_work);
    }
    phy_bigrat lower;
    phy_bigrat upper;
    memset(&lower, 0, sizeof lower);
    memset(&upper, 0, sizeof upper);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &lower);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &upper);
    }
    for (uint32_t refinement = 0u;
         status == PHY_OK &&
         refinement <= context->limits.max_refinements; ++refinement) {
        status = binary_interval(
            context, left_work, right_work, operation, &lower, &upper);
        uint32_t roots = 0u;
        if (status == PHY_OK) {
            status = root_count_for_coefficients(
                context, coefficients, coefficient_count,
                &lower, &upper, &roots);
        }
        if (status == PHY_OK && roots == 1u) {
            status = value_from_certificate(
                context, coefficients, coefficient_count,
                &lower, &upper, out_value);
            break;
        }
        if (status == PHY_OK &&
            refinement == context->limits.max_refinements) {
            status = PHY_ERR_TIMEOUT;
            break;
        }
        if (status == PHY_OK) {
            status = phy_real_algebraic_refine(left_work, 1u);
        }
        if (status == PHY_OK) {
            status = phy_real_algebraic_refine(right_work, 1u);
        }
    }
    phy_bigrat_destroy(&upper);
    phy_bigrat_destroy(&lower);
    phy_real_algebraic_destroy(right_work);
    phy_real_algebraic_destroy(left_work);
    return status;
}

static phy_status nonrational_binary(
    const phy_real_algebraic *left,
    const phy_real_algebraic *right,
    algebraic_binary_operation operation,
    phy_real_algebraic **out_value)
{
    phy_algebraic_context *context = left->context;
    rational_polynomial resultant;
    rational_polynomial square_free;
    memset(&resultant, 0, sizeof resultant);
    memset(&square_free, 0, sizeof square_free);
    phy_status status = resultant_polynomial(
        context, left, right, operation, &resultant);
    if (status == PHY_OK) {
        status = rational_polynomial_square_free(
            context, &resultant, &square_free);
    }
    phy_bigint *coefficients = NULL;
    size_t coefficient_count = 0u;
    size_t coefficient_capacity = 0u;
    if (status == PHY_OK) {
        status = rational_polynomial_to_primitive_integers(
            context, &square_free, &coefficients,
            &coefficient_count, &coefficient_capacity);
    }
    if (status == PHY_OK) {
        status = publish_resultant_value(
            context, left, right, operation, coefficients,
            coefficient_count, out_value);
    }
    destroy_coefficient_array(
        context, coefficients, coefficient_capacity);
    rational_polynomial_destroy(context, &square_free);
    rational_polynomial_destroy(context, &resultant);
    return status;
}

static phy_status binary_arguments(
    const phy_real_algebraic *left,
    const phy_real_algebraic *right,
    phy_real_algebraic **out_value)
{
    if (!value_valid_handle(left) || !value_valid_handle(right) ||
        left->context != right->context || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = NULL;
    return PHY_OK;
}

phy_status phy_real_algebraic_add(
    const phy_real_algebraic *left, const phy_real_algebraic *right,
    phy_real_algebraic **out_value)
{
    phy_status status = binary_arguments(left, right, out_value);
    if (status != PHY_OK) {
        return status;
    }
    phy_algebraic_context *context = left->context;
    status = call_begin(context);
    phy_bigrat one;
    memset(&one, 0, sizeof one);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &one);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&one, 1, 1);
    }
    if (status == PHY_OK && right->rational) {
        status = affine_transform(left, &one, &right->lower, out_value);
    } else if (status == PHY_OK && left->rational) {
        status = affine_transform(right, &one, &left->lower, out_value);
    } else if (status == PHY_OK) {
        status = nonrational_binary(
            left, right, ALGEBRAIC_BINARY_SUM, out_value);
    }
    phy_bigrat_destroy(&one);
    return call_end(context, status);
}

phy_status phy_real_algebraic_subtract(
    const phy_real_algebraic *left, const phy_real_algebraic *right,
    phy_real_algebraic **out_value)
{
    phy_status status = binary_arguments(left, right, out_value);
    if (status != PHY_OK) {
        return status;
    }
    phy_algebraic_context *context = left->context;
    status = call_begin(context);
    phy_bigrat minus_one;
    phy_bigrat offset;
    memset(&minus_one, 0, sizeof minus_one);
    memset(&offset, 0, sizeof offset);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &minus_one);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &offset);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&minus_one, -1, 1);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&offset, 0, 1);
    }
    if (status == PHY_OK && right->rational) {
        status = phy_bigrat_negate(&right->lower, &offset);
        if (status == PHY_OK) {
            phy_bigrat one;
            memset(&one, 0, sizeof one);
            status = phy_bigrat_init(context->exact, &one);
            if (status == PHY_OK) {
                status = phy_bigrat_set_i64(&one, 1, 1);
            }
            if (status == PHY_OK) {
                status = affine_transform(left, &one, &offset, out_value);
            }
            phy_bigrat_destroy(&one);
        }
    } else if (status == PHY_OK && left->rational) {
        status = affine_transform(
            right, &minus_one, &left->lower, out_value);
    } else if (status == PHY_OK) {
        phy_real_algebraic *negative = NULL;
        status = affine_transform(right, &minus_one, &offset, &negative);
        if (status == PHY_OK) {
            status = nonrational_binary(
                left, negative, ALGEBRAIC_BINARY_SUM, out_value);
        }
        phy_real_algebraic_destroy(negative);
    }
    phy_bigrat_destroy(&offset);
    phy_bigrat_destroy(&minus_one);
    return call_end(context, status);
}

phy_status phy_real_algebraic_multiply(
    const phy_real_algebraic *left, const phy_real_algebraic *right,
    phy_real_algebraic **out_value)
{
    phy_status status = binary_arguments(left, right, out_value);
    if (status != PHY_OK) {
        return status;
    }
    phy_algebraic_context *context = left->context;
    status = call_begin(context);
    phy_bigrat zero;
    memset(&zero, 0, sizeof zero);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &zero);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&zero, 0, 1);
    }
    if (status == PHY_OK && right->rational) {
        status = affine_transform(left, &right->lower, &zero, out_value);
    } else if (status == PHY_OK && left->rational) {
        status = affine_transform(right, &left->lower, &zero, out_value);
    } else if (status == PHY_OK) {
        status = nonrational_binary(
            left, right, ALGEBRAIC_BINARY_PRODUCT, out_value);
    }
    phy_bigrat_destroy(&zero);
    return call_end(context, status);
}

phy_status phy_real_algebraic_divide(
    const phy_real_algebraic *left, const phy_real_algebraic *right,
    phy_real_algebraic **out_value)
{
    phy_status status = binary_arguments(left, right, out_value);
    if (status != PHY_OK) {
        return status;
    }
    phy_algebraic_context *context = left->context;
    status = call_begin(context);
    phy_real_algebraic *inverse = NULL;
    if (status == PHY_OK) {
        status = phy_real_algebraic_reciprocal(right, &inverse);
    }
    if (status == PHY_OK) {
        status = phy_real_algebraic_multiply(
            left, inverse, out_value);
    }
    phy_real_algebraic_destroy(inverse);
    return call_end(context, status);
}

phy_status phy_real_algebraic_pow_i32(
    const phy_real_algebraic *base, int32_t exponent,
    phy_real_algebraic **out_value)
{
    if (!value_valid_handle(base) || out_value == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    *out_value = NULL;
    phy_algebraic_context *context = base->context;
    phy_status status = call_begin(context);
    phy_real_algebraic *factor = NULL;
    phy_real_algebraic *result = NULL;
    if (status == PHY_OK && exponent < 0) {
        status = phy_real_algebraic_reciprocal(base, &factor);
    } else if (status == PHY_OK) {
        status = value_from_certificate(
            context, base->coefficients, base->coefficient_count,
            &base->lower, &base->upper, &factor);
        if (status == PHY_OK) {
            factor->rational = base->rational;
        }
    }
    phy_bigrat one;
    memset(&one, 0, sizeof one);
    if (status == PHY_OK) {
        status = phy_bigrat_init(context->exact, &one);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&one, 1, 1);
    }
    if (status == PHY_OK) {
        status = value_from_rational_point(context, &one, &result);
    }
    uint32_t power =
        exponent < 0 ? (uint32_t)(-(int64_t)exponent)
                     : (uint32_t)exponent;
    while (status == PHY_OK && power != 0u) {
        if ((power & 1u) != 0u) {
            phy_real_algebraic *next = NULL;
            status = phy_real_algebraic_multiply(
                result, factor, &next);
            if (status == PHY_OK) {
                phy_real_algebraic_destroy(result);
                result = next;
            }
        }
        power >>= 1u;
        if (status == PHY_OK && power != 0u) {
            phy_real_algebraic *next = NULL;
            status = phy_real_algebraic_multiply(
                factor, factor, &next);
            if (status == PHY_OK) {
                phy_real_algebraic_destroy(factor);
                factor = next;
            }
        }
    }
    phy_bigrat_destroy(&one);
    phy_real_algebraic_destroy(factor);
    if (status == PHY_OK) {
        *out_value = result;
    } else {
        phy_real_algebraic_destroy(result);
    }
    return call_end(context, status);
}
