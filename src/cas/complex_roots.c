#include "complex_roots.h"

#include <limits.h>
#include <string.h>

#define COMPLEX_ROOT_MAX_DEGREE 48u
#define COMPLEX_ROOT_MAX_ITERATIONS 64u
#define COMPLEX_ROOT_GUARD_BITS 8u

static void destroy_bigrat(phy_bigrat *value)
{
    if (phy_bigint_is_initialized(phy_bigrat_numerator(value))) {
        phy_bigrat_destroy(value);
    }
}

static phy_status abs_rat(const phy_bigrat *value, phy_bigrat *out)
{
    return phy_bigrat_sign(value) < 0 ? phy_bigrat_negate(value, out)
                                      : phy_bigrat_copy(value, out);
}

static phy_status gaussian_set_parts(phy_gaussian *value,
                                     const phy_bigrat *real,
                                     const phy_bigrat *imaginary)
{
    phy_status status = phy_bigrat_copy(real, &value->real);
    if (status == PHY_OK) {
        status = phy_bigrat_copy(imaginary, &value->imaginary);
    }
    return status;
}

static phy_status gaussian_set_real(phy_gaussian *value,
                                    const phy_bigrat *real)
{
    phy_status status = phy_bigrat_copy(real, &value->real);
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(&value->imaginary, 0, 1);
    }
    return status;
}

static phy_status gaussian_abs_upper(const phy_gaussian *value,
                                     phy_bigrat *out)
{
    phy_exact_context *exact = phy_bigrat_numerator(out)->context;
    phy_bigrat real = {0};
    phy_bigrat imaginary = {0};
    phy_status status = phy_bigrat_init(exact, &real);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &imaginary);
    if (status == PHY_OK) status = abs_rat(&value->real, &real);
    if (status == PHY_OK) status = abs_rat(&value->imaginary, &imaginary);
    if (status == PHY_OK) status = phy_bigrat_add(&real, &imaginary, out);
    destroy_bigrat(&imaginary);
    destroy_bigrat(&real);
    return status;
}

static phy_status gaussian_abs_lower(const phy_gaussian *value,
                                     phy_bigrat *out)
{
    phy_exact_context *exact = phy_bigrat_numerator(out)->context;
    phy_bigrat real = {0};
    phy_bigrat imaginary = {0};
    phy_status status = phy_bigrat_init(exact, &real);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &imaginary);
    if (status == PHY_OK) status = abs_rat(&value->real, &real);
    if (status == PHY_OK) status = abs_rat(&value->imaginary, &imaginary);
    int order = 0;
    if (status == PHY_OK) {
        status = phy_bigrat_compare(&real, &imaginary, &order);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_copy(order >= 0 ? &real : &imaginary, out);
    }
    destroy_bigrat(&imaginary);
    destroy_bigrat(&real);
    return status;
}

static phy_status dyadic_epsilon(phy_exact_context *exact, uint32_t bits,
                                 phy_bigrat *out)
{
    phy_bigint two = {0};
    phy_bigint scale = {0};
    phy_bigrat scale_rat = {0};
    phy_status status = phy_bigint_init(exact, &two);
    if (status == PHY_OK) status = phy_bigint_init(exact, &scale);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &scale_rat);
    if (status == PHY_OK) status = phy_bigint_set_i64(&two, 2);
    if (status == PHY_OK) status = phy_bigint_pow_u32(&two, bits, &scale);
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(&scale, &scale_rat);
    }
    if (status == PHY_OK) status = phy_bigrat_set_i64(out, 1, 1);
    if (status == PHY_OK) status = phy_bigrat_divide(out, &scale_rat, out);
    destroy_bigrat(&scale_rat);
    if (phy_bigint_is_initialized(&scale)) phy_bigint_destroy(&scale);
    if (phy_bigint_is_initialized(&two)) phy_bigint_destroy(&two);
    return status;
}

static phy_status round_rat_nearest(const phy_bigrat *value, uint32_t bits,
                                    phy_bigrat *out)
{
    phy_exact_context *exact = phy_bigrat_numerator(out)->context;
    phy_bigint two = {0};
    phy_bigint scale = {0};
    phy_bigint scaled = {0};
    phy_bigint quotient = {0};
    phy_bigint remainder = {0};
    phy_bigint twice_remainder = {0};
    phy_bigint one = {0};
    phy_bigint *integers[] = {&two, &scale, &scaled, &quotient,
                              &remainder, &twice_remainder, &one};
    size_t initialized = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && initialized < 7u) {
        status = phy_bigint_init(exact, integers[initialized]);
        if (status == PHY_OK) initialized++;
    }
    if (status == PHY_OK) status = phy_bigint_set_i64(&two, 2);
    if (status == PHY_OK) status = phy_bigint_pow_u32(&two, bits, &scale);
    if (status == PHY_OK) {
        status = phy_bigint_multiply(phy_bigrat_numerator(value), &scale,
                                     &scaled);
    }
    if (status == PHY_OK) {
        status = phy_bigint_divmod(&scaled, phy_bigrat_denominator(value),
                                   &quotient, &remainder);
    }
    if (status == PHY_OK) {
        status = phy_bigint_add(&remainder, &remainder, &twice_remainder);
    }
    if (status == PHY_OK && phy_bigint_compare_abs(
                                &twice_remainder,
                                phy_bigrat_denominator(value)) >= 0) {
        status = phy_bigint_set_i64(&one, 1);
        if (status == PHY_OK) {
            status = phy_bigrat_sign(value) < 0
                         ? phy_bigint_subtract(&quotient, &one, &quotient)
                         : phy_bigint_add(&quotient, &one, &quotient);
        }
    }
    phy_bigrat quotient_rat = {0};
    phy_bigrat scale_rat = {0};
    if (status == PHY_OK) status = phy_bigrat_init(exact, &quotient_rat);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &scale_rat);
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(&quotient, &quotient_rat);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_set_bigint(&scale, &scale_rat);
    }
    if (status == PHY_OK) {
        status = phy_bigrat_divide(&quotient_rat, &scale_rat, out);
    }
    destroy_bigrat(&scale_rat);
    destroy_bigrat(&quotient_rat);
    while (initialized != 0u) {
        phy_bigint_destroy(integers[--initialized]);
    }
    return status;
}

static phy_status round_gaussian(const phy_gaussian *value, uint32_t bits,
                                 phy_gaussian *out)
{
    phy_exact_context *exact = phy_bigrat_numerator(&out->real)->context;
    phy_bigrat real = {0};
    phy_bigrat imaginary = {0};
    phy_status status = phy_bigrat_init(exact, &real);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &imaginary);
    if (status == PHY_OK) {
        status = round_rat_nearest(&value->real, bits, &real);
    }
    if (status == PHY_OK) {
        status = round_rat_nearest(&value->imaginary, bits, &imaginary);
    }
    if (status == PHY_OK) status = gaussian_set_parts(out, &real, &imaginary);
    destroy_bigrat(&imaginary);
    destroy_bigrat(&real);
    return status;
}

static phy_status polynomial_value(const phy_bigrat *coefficients,
                                   size_t count, const phy_gaussian *point,
                                   phy_gaussian *out)
{
    phy_exact_context *exact = phy_bigrat_numerator(&out->real)->context;
    phy_gaussian accumulator = {0};
    phy_gaussian temporary = {0};
    phy_gaussian coefficient = {0};
    phy_status status = phy_gaussian_init(exact, &accumulator);
    if (status == PHY_OK) status = phy_gaussian_init(exact, &temporary);
    if (status == PHY_OK) status = phy_gaussian_init(exact, &coefficient);
    if (status == PHY_OK) {
        status = gaussian_set_real(&accumulator, &coefficients[count - 1u]);
    }
    for (size_t index = count - 1u; status == PHY_OK && index != 0u;) {
        --index;
        status = phy_gaussian_multiply(&accumulator, point, &temporary);
        if (status == PHY_OK) {
            status = gaussian_set_real(&coefficient, &coefficients[index]);
        }
        if (status == PHY_OK) {
            status = phy_gaussian_add(&temporary, &coefficient,
                                      &accumulator);
        }
    }
    if (status == PHY_OK) status = phy_gaussian_copy(&accumulator, out);
    phy_gaussian_destroy(&coefficient);
    phy_gaussian_destroy(&temporary);
    phy_gaussian_destroy(&accumulator);
    return status;
}

static phy_status cauchy_radius(phy_exact_context *exact,
                               const phy_bigrat *coefficients, size_t count,
                               phy_bigrat *out)
{
    phy_bigrat leading = {0};
    phy_bigrat coefficient = {0};
    phy_bigrat ratio = {0};
    phy_bigrat maximum = {0};
    phy_bigrat one = {0};
    phy_status status = phy_bigrat_init(exact, &leading);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &coefficient);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &ratio);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &maximum);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &one);
    if (status == PHY_OK) status = abs_rat(&coefficients[count - 1u], &leading);
    if (status == PHY_OK && phy_bigrat_sign(&leading) == 0) {
        status = PHY_ERR_INVALID_ARGUMENT;
    }
    if (status == PHY_OK) status = phy_bigrat_set_i64(&maximum, 0, 1);
    for (size_t index = 0u; status == PHY_OK && index + 1u < count;
         ++index) {
        status = abs_rat(&coefficients[index], &coefficient);
        if (status == PHY_OK) {
            status = phy_bigrat_divide(&coefficient, &leading, &ratio);
        }
        int order = 0;
        if (status == PHY_OK) {
            status = phy_bigrat_compare(&ratio, &maximum, &order);
        }
        if (status == PHY_OK && order > 0) {
            status = phy_bigrat_copy(&ratio, &maximum);
        }
    }
    if (status == PHY_OK) status = phy_bigrat_set_i64(&one, 1, 1);
    if (status == PHY_OK) status = phy_bigrat_add(&maximum, &one, out);
    destroy_bigrat(&one);
    destroy_bigrat(&maximum);
    destroy_bigrat(&ratio);
    destroy_bigrat(&coefficient);
    destroy_bigrat(&leading);
    return status;
}

static phy_status seed_centers(phy_exact_context *exact,
                               const phy_bigrat *radius, size_t degree,
                               phy_gaussian *centers)
{
    phy_bigrat t = {0};
    phy_bigrat one = {0};
    phy_bigrat complement = {0};
    phy_bigrat x = {0};
    phy_bigrat y = {0};
    phy_bigrat shift = {0};
    phy_status status = phy_bigrat_init(exact, &t);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &one);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &complement);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &x);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &y);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &shift);
    if (status == PHY_OK) status = phy_bigrat_set_i64(&one, 1, 1);
    if (status == PHY_OK) {
        status = phy_bigrat_set_i64(
            &shift, 1, (int64_t)(64u * degree * degree));
    }
    if (status == PHY_OK) {
        status = phy_bigrat_multiply(&shift, radius, &shift);
    }
    for (size_t index = 0u; status == PHY_OK && index < degree; ++index) {
        const uint64_t perimeter = 4u * (uint64_t)index;
        const uint64_t quadrant = perimeter / degree;
        const uint64_t remainder = perimeter - quadrant * degree;
        status = phy_bigrat_set_i64(&t, (int64_t)remainder,
                                    (int64_t)degree);
        if (status == PHY_OK) {
            status = phy_bigrat_subtract(&one, &t, &complement);
        }
        const phy_bigrat *x_source = NULL;
        const phy_bigrat *y_source = NULL;
        bool negate_x = false;
        bool negate_y = false;
        switch (quadrant) {
        case 0u:
            x_source = &complement;
            y_source = &t;
            break;
        case 1u:
            x_source = &t;
            y_source = &complement;
            negate_x = true;
            break;
        case 2u:
            x_source = &complement;
            y_source = &t;
            negate_x = true;
            negate_y = true;
            break;
        default:
            x_source = &t;
            y_source = &complement;
            negate_y = true;
            break;
        }
        if (status == PHY_OK) {
            status = negate_x ? phy_bigrat_negate(x_source, &x)
                              : phy_bigrat_copy(x_source, &x);
        }
        if (status == PHY_OK) {
            status = negate_y ? phy_bigrat_negate(y_source, &y)
                              : phy_bigrat_copy(y_source, &y);
        }
        if (status == PHY_OK) status = phy_bigrat_multiply(&x, radius, &x);
        if (status == PHY_OK) status = phy_bigrat_multiply(&y, radius, &y);
        /* Deterministic index-dependent exact offsets break invariant
           symmetric seed subspaces such as the cardinal polygon for
           x^4 + 1.  They affect only untrusted approximants; final boxes
           still need the exact one-root certificate. */
        if (status == PHY_OK) {
            status = phy_bigrat_set_i64(
                &t, (int64_t)(index + 1u), 1);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_multiply(&shift, &t, &complement);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_add(&x, &complement, &x);
        }
        if (status == PHY_OK) {
            const uint64_t square =
                (uint64_t)(index + 1u) * (uint64_t)(index + 1u);
            status = phy_bigrat_set_i64(&t, (int64_t)square, 1);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_multiply(&shift, &t, &complement);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_add(&y, &complement, &y);
        }
        if (status == PHY_OK) {
            status = gaussian_set_parts(&centers[index], &x, &y);
        }
    }
    destroy_bigrat(&shift);
    destroy_bigrat(&y);
    destroy_bigrat(&x);
    destroy_bigrat(&complement);
    destroy_bigrat(&one);
    destroy_bigrat(&t);
    return status;
}

static phy_status durand_kerner_step(phy_exact_context *exact,
                                     const phy_bigrat *coefficients,
                                     size_t count,
                                     const phy_gaussian *centers,
                                     phy_gaussian *next, uint32_t bits)
{
    const size_t degree = count - 1u;
    phy_gaussian value = {0};
    phy_gaussian denominator = {0};
    phy_gaussian difference = {0};
    phy_gaussian product = {0};
    phy_gaussian correction = {0};
    phy_gaussian candidate = {0};
    phy_status status = phy_gaussian_init(exact, &value);
    if (status == PHY_OK) status = phy_gaussian_init(exact, &denominator);
    if (status == PHY_OK) status = phy_gaussian_init(exact, &difference);
    if (status == PHY_OK) status = phy_gaussian_init(exact, &product);
    if (status == PHY_OK) status = phy_gaussian_init(exact, &correction);
    if (status == PHY_OK) status = phy_gaussian_init(exact, &candidate);
    for (size_t index = 0u; status == PHY_OK && index < degree; ++index) {
        status = polynomial_value(coefficients, count, &centers[index],
                                  &value);
        if (status == PHY_OK) {
            status = phy_gaussian_set_i64(&denominator, 1, 1, 0, 1);
        }
        for (size_t other = 0u; status == PHY_OK && other < degree;
             ++other) {
            if (other == index) continue;
            status = phy_gaussian_subtract(&centers[index], &centers[other],
                                           &difference);
            if (status == PHY_OK) {
                status = phy_gaussian_multiply(&denominator, &difference,
                                               &product);
            }
            if (status == PHY_OK) {
                status = phy_gaussian_copy(&product, &denominator);
            }
        }
        if (status == PHY_OK) {
            status = phy_gaussian_divide(&value, &denominator, &correction);
        }
        if (status == PHY_OK) {
            status = phy_gaussian_subtract(&centers[index], &correction,
                                           &candidate);
        }
        if (status == PHY_OK) {
            status = round_gaussian(&candidate, bits, &next[index]);
        }
    }
    phy_gaussian_destroy(&candidate);
    phy_gaussian_destroy(&correction);
    phy_gaussian_destroy(&product);
    phy_gaussian_destroy(&difference);
    phy_gaussian_destroy(&denominator);
    phy_gaussian_destroy(&value);
    return status;
}

static phy_status expand_at_center(phy_exact_context *exact,
                                   const phy_bigrat *coefficients,
                                   size_t count,
                                   const phy_gaussian *center,
                                   phy_gaussian *expanded)
{
    phy_gaussian product = {0};
    phy_gaussian sum = {0};
    phy_gaussian coefficient = {0};
    phy_status status = phy_gaussian_init(exact, &product);
    if (status == PHY_OK) status = phy_gaussian_init(exact, &sum);
    if (status == PHY_OK) status = phy_gaussian_init(exact, &coefficient);
    if (status == PHY_OK) {
        status = gaussian_set_real(&expanded[0], &coefficients[count - 1u]);
    }
    size_t current_degree = 0u;
    for (size_t source = count - 1u; status == PHY_OK && source != 0u;) {
        --source;
        status = phy_gaussian_copy(&expanded[current_degree],
                                   &expanded[current_degree + 1u]);
        for (size_t slot = current_degree; status == PHY_OK && slot != 0u;
             --slot) {
            status = phy_gaussian_multiply(center, &expanded[slot],
                                           &product);
            if (status == PHY_OK) {
                status = phy_gaussian_add(&expanded[slot - 1u], &product,
                                          &sum);
            }
            if (status == PHY_OK) {
                status = phy_gaussian_copy(&sum, &expanded[slot]);
            }
        }
        if (status == PHY_OK) {
            status = phy_gaussian_multiply(center, &expanded[0], &product);
        }
        if (status == PHY_OK) {
            status = gaussian_set_real(&coefficient, &coefficients[source]);
        }
        if (status == PHY_OK) {
            status = phy_gaussian_add(&product, &coefficient, &sum);
        }
        if (status == PHY_OK) {
            status = phy_gaussian_copy(&sum, &expanded[0]);
        }
        current_degree++;
    }
    phy_gaussian_destroy(&coefficient);
    phy_gaussian_destroy(&sum);
    phy_gaussian_destroy(&product);
    return status;
}

static phy_status pellet_one(phy_exact_context *exact,
                             const phy_bigrat *coefficients, size_t count,
                             const phy_gaussian *center,
                             const phy_bigrat *radius, bool *out_certified)
{
    const size_t degree = count - 1u;
    *out_certified = false;
    phy_gaussian expanded[COMPLEX_ROOT_MAX_DEGREE + 1u];
    memset(expanded, 0, sizeof expanded);
    size_t initialized = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && initialized <= degree) {
        status = phy_gaussian_init(exact, &expanded[initialized]);
        if (status == PHY_OK) initialized++;
    }
    if (status == PHY_OK) {
        status = expand_at_center(exact, coefficients, count, center,
                                  expanded);
    }
    phy_bigrat lower = {0};
    phy_bigrat upper = {0};
    phy_bigrat left = {0};
    phy_bigrat right = {0};
    phy_bigrat two_radius = {0};
    phy_bigrat power = {0};
    phy_bigrat contribution = {0};
    phy_bigrat *values[] = {&lower, &upper, &left, &right,
                            &two_radius, &power, &contribution};
    size_t rat_count = 0u;
    while (status == PHY_OK && rat_count < 7u) {
        status = phy_bigrat_init(exact, values[rat_count]);
        if (status == PHY_OK) rat_count++;
    }
    if (status == PHY_OK) status = gaussian_abs_lower(&expanded[1], &lower);
    if (status == PHY_OK) status = phy_bigrat_multiply(&lower, radius, &left);
    if (status == PHY_OK) status = phy_bigrat_set_i64(&right, 0, 1);
    if (status == PHY_OK) {
        status = phy_bigrat_add(radius, radius, &two_radius);
    }
    if (status == PHY_OK) status = phy_bigrat_set_i64(&power, 1, 1);
    for (size_t slot = 0u; status == PHY_OK && slot <= degree; ++slot) {
        if (slot != 1u) {
            status = gaussian_abs_upper(&expanded[slot], &upper);
            if (status == PHY_OK) {
                status = phy_bigrat_multiply(&upper, &power, &contribution);
            }
            if (status == PHY_OK) {
                status = phy_bigrat_add(&right, &contribution, &right);
            }
        }
        if (status == PHY_OK && slot != degree) {
            status = phy_bigrat_multiply(&power, &two_radius, &power);
        }
    }
    int order = 0;
    if (status == PHY_OK) status = phy_bigrat_compare(&left, &right, &order);
    if (status == PHY_OK) *out_certified = order > 0;
    while (rat_count != 0u) phy_bigrat_destroy(values[--rat_count]);
    while (initialized != 0u) {
        phy_gaussian_destroy(&expanded[--initialized]);
    }
    return status;
}

static phy_status boxes_disjoint(phy_exact_context *exact,
                                 const phy_gaussian *centers, size_t degree,
                                 const phy_bigrat *radius, bool *out_disjoint)
{
    *out_disjoint = true;
    phy_bigrat dx = {0};
    phy_bigrat dy = {0};
    phy_bigrat abs_dx = {0};
    phy_bigrat abs_dy = {0};
    phy_bigrat diameter = {0};
    phy_status status = phy_bigrat_init(exact, &dx);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &dy);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &abs_dx);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &abs_dy);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &diameter);
    if (status == PHY_OK) status = phy_bigrat_add(radius, radius, &diameter);
    for (size_t left = 0u; status == PHY_OK && left < degree; ++left) {
        for (size_t right = left + 1u;
             status == PHY_OK && right < degree; ++right) {
            status = phy_bigrat_subtract(&centers[left].real,
                                         &centers[right].real, &dx);
            if (status == PHY_OK) {
                status = phy_bigrat_subtract(&centers[left].imaginary,
                                             &centers[right].imaginary, &dy);
            }
            if (status == PHY_OK) status = abs_rat(&dx, &abs_dx);
            if (status == PHY_OK) status = abs_rat(&dy, &abs_dy);
            int x_order = 0;
            int y_order = 0;
            if (status == PHY_OK) {
                status = phy_bigrat_compare(&abs_dx, &diameter, &x_order);
            }
            if (status == PHY_OK) {
                status = phy_bigrat_compare(&abs_dy, &diameter, &y_order);
            }
            if (status == PHY_OK && x_order <= 0 && y_order <= 0) {
                *out_disjoint = false;
                left = degree;
                break;
            }
        }
    }
    destroy_bigrat(&diameter);
    destroy_bigrat(&abs_dy);
    destroy_bigrat(&abs_dx);
    destroy_bigrat(&dy);
    destroy_bigrat(&dx);
    return status;
}

static phy_status publish_boxes(phy_exact_context *exact,
                                const phy_gaussian *centers, size_t degree,
                                const phy_bigrat *radius,
                                phy_complex_ball *roots)
{
    phy_bigrat lower = {0};
    phy_bigrat upper = {0};
    phy_status status = phy_bigrat_init(exact, &lower);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &upper);
    for (size_t index = 0u; status == PHY_OK && index < degree; ++index) {
        status = phy_bigrat_subtract(&centers[index].real, radius, &lower);
        if (status == PHY_OK) {
            status = phy_bigrat_add(&centers[index].real, radius, &upper);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_set_interval(&roots[index].real, &lower,
                                                &upper);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_subtract(&centers[index].imaginary, radius,
                                         &lower);
        }
        if (status == PHY_OK) {
            status = phy_bigrat_add(&centers[index].imaginary, radius,
                                    &upper);
        }
        if (status == PHY_OK) {
            status = phy_real_ball_set_interval(&roots[index].imaginary,
                                                &lower, &upper);
        }
    }
    destroy_bigrat(&upper);
    destroy_bigrat(&lower);
    return status;
}

phy_status phy_complex_roots_isolate(
    phy_exact_context *exact, const phy_bigrat *coefficients,
    size_t coefficient_count, uint32_t bits, phy_complex_ball *roots,
    size_t root_capacity, size_t *out_root_count)
{
    if (exact == NULL || coefficients == NULL || roots == NULL ||
        out_root_count == NULL || coefficient_count < 3u ||
        coefficient_count > COMPLEX_ROOT_MAX_DEGREE + 1u || bits < 8u) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    const size_t degree = coefficient_count - 1u;
    if (root_capacity < degree) return PHY_ERR_MEMORY_LIMIT;
    *out_root_count = 0u;
    for (size_t index = 0u; index < coefficient_count; ++index) {
        if (phy_bigrat_validate(&coefficients[index]) != PHY_OK ||
            phy_bigrat_numerator(&coefficients[index])->context != exact) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
    }
    for (size_t index = 0u; index < degree; ++index) {
        if (phy_complex_ball_validate(&roots[index]) != PHY_OK ||
            phy_bigrat_numerator(&roots[index].real.midpoint)->context !=
                exact) {
            return PHY_ERR_INVALID_ARGUMENT;
        }
    }
    phy_gaussian centers[COMPLEX_ROOT_MAX_DEGREE];
    phy_gaussian next[COMPLEX_ROOT_MAX_DEGREE];
    memset(centers, 0, sizeof centers);
    memset(next, 0, sizeof next);
    size_t center_count = 0u;
    size_t next_count = 0u;
    phy_status status = PHY_OK;
    while (status == PHY_OK && center_count < degree) {
        status = phy_gaussian_init(exact, &centers[center_count]);
        if (status == PHY_OK) center_count++;
    }
    while (status == PHY_OK && next_count < degree) {
        status = phy_gaussian_init(exact, &next[next_count]);
        if (status == PHY_OK) next_count++;
    }
    phy_bigrat radius = {0};
    phy_bigrat enclosure = {0};
    if (status == PHY_OK) status = phy_bigrat_init(exact, &radius);
    if (status == PHY_OK) status = phy_bigrat_init(exact, &enclosure);
    if (status == PHY_OK) {
        status = cauchy_radius(exact, coefficients, coefficient_count,
                               &enclosure);
    }
    if (status == PHY_OK) {
        status = seed_centers(exact, &enclosure, degree, centers);
    }
    const uint32_t point_bits =
        bits > UINT32_MAX - COMPLEX_ROOT_GUARD_BITS
            ? UINT32_MAX
            : bits + COMPLEX_ROOT_GUARD_BITS;
    if (status == PHY_OK) status = dyadic_epsilon(exact, bits, &radius);
    bool certified = false;
    for (uint32_t iteration = 0u;
         status == PHY_OK && iteration < COMPLEX_ROOT_MAX_ITERATIONS;
         ++iteration) {
        /*
         * Coarse Durand--Kerner iterates do not benefit from carrying the
         * final dyadic precision.  Raising it gradually avoids spending most
         * of the exact-operation budget on the deliberately rough starting
         * polygon.  The result remains certified by the exact Pellet/Rouche
         * test below; these iterates are only candidate centres.
         */
        const uint32_t adaptive_bits =
            iteration > (UINT32_MAX - 24u) / 8u
                ? UINT32_MAX
                : 24u + iteration * 8u;
        const uint32_t iteration_bits =
            adaptive_bits < point_bits ? adaptive_bits : point_bits;
        status = durand_kerner_step(exact, coefficients, coefficient_count,
                                    centers, next, iteration_bits);
        if (status == PHY_OK) {
            for (size_t index = 0u; index < degree; ++index) {
                status = phy_gaussian_copy(&next[index], &centers[index]);
                if (status != PHY_OK) break;
            }
        }
        if (status != PHY_OK || iteration_bits != point_bits ||
            iteration < 3u || (iteration & 3u) != 3u) {
            continue;
        }
        bool disjoint = false;
        status = boxes_disjoint(exact, centers, degree, &radius, &disjoint);
        if (status != PHY_OK || !disjoint) continue;
        certified = true;
        for (size_t index = 0u;
             status == PHY_OK && certified && index < degree; ++index) {
            bool one = false;
            status = pellet_one(exact, coefficients, coefficient_count,
                                &centers[index], &radius, &one);
            certified = one;
        }
        if (status == PHY_OK && certified) break;
    }
    if (status == PHY_OK && !certified) status = PHY_ERR_TERM_LIMIT;
    if (status == PHY_OK) {
        status = publish_boxes(exact, centers, degree, &radius, roots);
    }
    if (status == PHY_OK) *out_root_count = degree;
    destroy_bigrat(&enclosure);
    destroy_bigrat(&radius);
    while (next_count != 0u) phy_gaussian_destroy(&next[--next_count]);
    while (center_count != 0u) {
        phy_gaussian_destroy(&centers[--center_count]);
    }
    return status;
}
