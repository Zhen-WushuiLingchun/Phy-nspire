#include "phy/mandelstam.h"

#include <string.h>

#include "phy/platform.h"

struct phy_mandelstam {
    phy_lorentz_metric *metric;
    phy_cas *cas;
    phy_mandelstam_routing routing;
    phy_ir_ref momentum[4];
    phy_ir_ref mass[4];
    phy_ir_ref mass_squared[4];
    phy_ir_ref invariant[3];
    phy_ir_ref definition[3];
    phy_ir_ref pair_dot[4][4];
    phy_ir_ref pair_value[4][4];
    phy_ir_ref mass_sum;
};

static phy_status symbol(phy_ir_context *ir, const char *name,
                         phy_ir_ref *out_ref)
{
    const phy_ir_symbol interned = phy_ir_intern(ir, name);
    if (interned == PHY_IR_NO_SYMBOL) {
        const phy_status status = phy_ir_last_error(ir);
        return status != PHY_OK ? status : PHY_ERR_OUT_OF_MEMORY;
    }
    const phy_ir_ref ref = phy_ir_symbol_ref(ir, interned);
    if (ref == PHY_IR_NULL) {
        const phy_status status = phy_ir_last_error(ir);
        return status != PHY_OK ? status : PHY_ERR_OUT_OF_MEMORY;
    }
    *out_ref = ref;
    return PHY_OK;
}

static phy_status square(phy_cas *cas, phy_ir_ref value, phy_ir_ref *out_ref)
{
    phy_ir_ref two = PHY_IR_NULL;
    phy_status status = phy_cas_number(cas, 2, 1, &two);
    return status != PHY_OK ? status : phy_cas_pow(cas, value, two, out_ref);
}

static phy_status vector_for(const phy_mandelstam *kinematics,
                             unsigned left, unsigned right, int sign,
                             phy_lorentz_vector *out_vector)
{
    phy_lorentz_vector a;
    phy_lorentz_vector b;
    phy_status status = phy_lorentz_vector_of(
        kinematics->metric, kinematics->momentum[left], &a);
    if (status == PHY_OK) {
        status = phy_lorentz_vector_of(
            kinematics->metric, kinematics->momentum[right], &b);
    }
    return status != PHY_OK
               ? status
               : (sign > 0 ? phy_lorentz_vector_add(&a, &b, out_vector)
                           : phy_lorentz_vector_sub(&a, &b, out_vector));
}

static phy_status build_definitions(phy_mandelstam *kinematics)
{
    phy_lorentz_vector channel;
    phy_status status = vector_for(kinematics, 0u, 1u, 1, &channel);
    if (status == PHY_OK) {
        status = phy_lorentz_vector_dot(
            &channel, &channel,
            &kinematics->definition[PHY_MANDELSTAM_S]);
    }
    const int crossed_sign =
        kinematics->routing == PHY_MANDELSTAM_PESKIN_2_TO_2 ? -1 : 1;
    if (status == PHY_OK) {
        status = vector_for(
            kinematics, 0u, 2u, crossed_sign, &channel);
    }
    if (status == PHY_OK) {
        status = phy_lorentz_vector_dot(
            &channel, &channel,
            &kinematics->definition[PHY_MANDELSTAM_T]);
    }
    if (status == PHY_OK) {
        status = vector_for(
            kinematics, 0u, 3u, crossed_sign, &channel);
    }
    if (status == PHY_OK) {
        status = phy_lorentz_vector_dot(
            &channel, &channel,
            &kinematics->definition[PHY_MANDELSTAM_U]);
    }
    return status;
}

static phy_ir_ref pair_channel(const phy_mandelstam *kinematics,
                               unsigned left, unsigned right)
{
    static const unsigned channel[4][4] = {
        {0u, PHY_MANDELSTAM_S, PHY_MANDELSTAM_T, PHY_MANDELSTAM_U},
        {PHY_MANDELSTAM_S, 0u, PHY_MANDELSTAM_U, PHY_MANDELSTAM_T},
        {PHY_MANDELSTAM_T, PHY_MANDELSTAM_U, 0u, PHY_MANDELSTAM_S},
        {PHY_MANDELSTAM_U, PHY_MANDELSTAM_T, PHY_MANDELSTAM_S, 0u}};
    return kinematics->invariant[channel[left][right]];
}

static phy_status build_pair_table(phy_mandelstam *kinematics)
{
    phy_ir_ref minus_one = PHY_IR_NULL;
    phy_ir_ref half = PHY_IR_NULL;
    phy_status status = phy_cas_number(
        kinematics->cas, -1, 1, &minus_one);
    if (status == PHY_OK) {
        status = phy_cas_number(kinematics->cas, 1, 2, &half);
    }

    for (unsigned left = 0u; left < 4u && status == PHY_OK; ++left) {
        for (unsigned right = left; right < 4u && status == PHY_OK; ++right) {
            status = phy_lorentz_dot(
                kinematics->metric, kinematics->momentum[left],
                kinematics->momentum[right],
                &kinematics->pair_dot[left][right]);
            if (status != PHY_OK) {
                break;
            }
            kinematics->pair_dot[right][left] =
                kinematics->pair_dot[left][right];
            if (left == right) {
                kinematics->pair_value[left][right] =
                    kinematics->mass_squared[left];
                kinematics->pair_value[right][left] =
                    kinematics->mass_squared[left];
                continue;
            }

            const phy_ir_ref terms[3] = {
                pair_channel(kinematics, left, right),
                kinematics->mass_squared[left],
                kinematics->mass_squared[right]};
            const phy_ir_ref signs[3] = {
                terms[0], PHY_IR_NULL, PHY_IR_NULL};
            phy_ir_ref negative_left = PHY_IR_NULL;
            phy_ir_ref negative_right = PHY_IR_NULL;
            status = phy_cas_mul(
                kinematics->cas,
                (const phy_ir_ref[2]){minus_one, terms[1]}, 2u,
                &negative_left);
            if (status == PHY_OK) {
                status = phy_cas_mul(
                    kinematics->cas,
                    (const phy_ir_ref[2]){minus_one, terms[2]}, 2u,
                    &negative_right);
            }
            phy_ir_ref numerator = PHY_IR_NULL;
            if (status == PHY_OK) {
                const phy_ir_ref sum[3] = {
                    signs[0], negative_left, negative_right};
                status = phy_cas_add(
                    kinematics->cas, sum, 3u, &numerator);
            }
            phy_ir_ref value = PHY_IR_NULL;
            if (status == PHY_OK) {
                status = phy_cas_mul(
                    kinematics->cas,
                    (const phy_ir_ref[2]){half, numerator}, 2u, &value);
            }

            if (status == PHY_OK &&
                kinematics->routing == PHY_MANDELSTAM_PESKIN_2_TO_2 &&
                ((left < 2u) != (right < 2u))) {
                status = phy_cas_neg(
                    kinematics->cas, value, &value);
            }
            if (status == PHY_OK) {
                kinematics->pair_value[left][right] = value;
                kinematics->pair_value[right][left] = value;
            }
        }
    }
    return status;
}

static phy_status declare_routing(phy_mandelstam *kinematics)
{
    phy_lorentz_vector relation;
    phy_status status =
        phy_lorentz_vector_zero(kinematics->metric, &relation);
    for (unsigned index = 0u; index < 4u && status == PHY_OK; ++index) {
        phy_lorentz_vector momentum;
        status = phy_lorentz_vector_of(
            kinematics->metric, kinematics->momentum[index], &momentum);
        if (status != PHY_OK) {
            break;
        }
        const bool outgoing =
            kinematics->routing == PHY_MANDELSTAM_PESKIN_2_TO_2 &&
            index >= 2u;
        phy_lorentz_vector next;
        status = outgoing
                     ? phy_lorentz_vector_sub(&relation, &momentum, &next)
                     : phy_lorentz_vector_add(&relation, &momentum, &next);
        relation = next;
    }
    if (status == PHY_OK) {
        status = phy_lorentz_declare_conservation(
            kinematics->metric, &relation, kinematics->momentum[3]);
    }
    for (unsigned index = 0u; index < 4u && status == PHY_OK; ++index) {
        status = phy_lorentz_declare_on_shell(
            kinematics->metric, kinematics->momentum[index],
            kinematics->mass[index]);
    }
    return status;
}

phy_status phy_mandelstam_create(
    phy_lorentz_metric *metric, const phy_ir_ref momentum[4],
    const phy_ir_ref mass[4], phy_mandelstam_routing routing,
    phy_mandelstam **out_kinematics)
{
    if (metric == NULL || momentum == NULL || mass == NULL ||
        out_kinematics == NULL ||
        (routing != PHY_MANDELSTAM_PESKIN_2_TO_2 &&
         routing != PHY_MANDELSTAM_ALL_INCOMING)) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    for (unsigned index = 0u; index < 4u; ++index) {
        if (!phy_lorentz_owns_momentum(metric, momentum[index]) ||
            mass[index] == PHY_IR_NULL) {
            return PHY_ERR_TYPE;
        }
        for (unsigned previous = 0u; previous < index; ++previous) {
            if (momentum[index] == momentum[previous]) {
                return PHY_ERR_ASSUMPTION;
            }
        }
    }

    phy_mandelstam *kinematics = phy_alloc(sizeof *kinematics);
    if (kinematics == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    memset(kinematics, 0, sizeof *kinematics);
    kinematics->metric = metric;
    kinematics->cas = phy_lorentz_metric_cas(metric);
    kinematics->routing = routing;
    memcpy(kinematics->momentum, momentum, sizeof kinematics->momentum);
    memcpy(kinematics->mass, mass, sizeof kinematics->mass);

    phy_ir_context *ir = phy_cas_ir(kinematics->cas);
    phy_status status = symbol(ir, "s", &kinematics->invariant[0]);
    if (status == PHY_OK) {
        status = symbol(ir, "t", &kinematics->invariant[1]);
    }
    if (status == PHY_OK) {
        status = symbol(ir, "u", &kinematics->invariant[2]);
    }
    for (unsigned index = 0u; index < 4u && status == PHY_OK; ++index) {
        status = square(
            kinematics->cas, mass[index],
            &kinematics->mass_squared[index]);
    }
    if (status == PHY_OK) {
        status = phy_cas_add(
            kinematics->cas, kinematics->mass_squared, 4u,
            &kinematics->mass_sum);
    }
    if (status == PHY_OK) {
        status = declare_routing(kinematics);
    }
    if (status == PHY_OK) {
        status = build_definitions(kinematics);
    }
    if (status == PHY_OK) {
        status = build_pair_table(kinematics);
    }
    if (status != PHY_OK) {
        phy_free(kinematics, sizeof *kinematics);
        return status;
    }
    *out_kinematics = kinematics;
    return PHY_OK;
}

void phy_mandelstam_destroy(phy_mandelstam *kinematics)
{
    if (kinematics != NULL) {
        phy_free(kinematics, sizeof *kinematics);
    }
}

phy_mandelstam_routing phy_mandelstam_routing_of(
    const phy_mandelstam *kinematics)
{
    return kinematics != NULL ? kinematics->routing
                              : PHY_MANDELSTAM_PESKIN_2_TO_2;
}

phy_ir_ref phy_mandelstam_symbol(
    const phy_mandelstam *kinematics, phy_mandelstam_invariant invariant)
{
    return kinematics != NULL && invariant <= PHY_MANDELSTAM_U
               ? kinematics->invariant[invariant]
               : PHY_IR_NULL;
}

phy_ir_ref phy_mandelstam_definition(
    const phy_mandelstam *kinematics, phy_mandelstam_invariant invariant)
{
    return kinematics != NULL && invariant <= PHY_MANDELSTAM_U
               ? kinematics->definition[invariant]
               : PHY_IR_NULL;
}

phy_ir_ref phy_mandelstam_sum_rule_rhs(
    const phy_mandelstam *kinematics)
{
    return kinematics != NULL ? kinematics->mass_sum : PHY_IR_NULL;
}

phy_status phy_mandelstam_reduce(const phy_mandelstam *kinematics,
                                 phy_ir_ref expression,
                                 phy_ir_ref *out_expression)
{
    if (kinematics == NULL || expression == PHY_IR_NULL ||
        out_expression == NULL) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref current = expression;
    for (unsigned left = 0u; left < 4u; ++left) {
        for (unsigned right = left; right < 4u; ++right) {
            phy_ir_ref replaced = PHY_IR_NULL;
            const phy_cas_rule rule = {
                kinematics->pair_dot[left][right],
                kinematics->pair_value[left][right]};
            const phy_status status = phy_cas_substitute(
                kinematics->cas, current, &rule, 1u, &replaced);
            if (status != PHY_OK) {
                return status;
            }
            current = replaced;
        }
    }
    return phy_cas_simplify(
        kinematics->cas, current, out_expression);
}

phy_status phy_mandelstam_eliminate(
    const phy_mandelstam *kinematics, phy_ir_ref expression,
    phy_mandelstam_invariant invariant, phy_ir_ref *out_expression)
{
    if (kinematics == NULL || expression == PHY_IR_NULL ||
        out_expression == NULL || invariant > PHY_MANDELSTAM_U) {
        return PHY_ERR_INVALID_ARGUMENT;
    }
    phy_ir_ref terms[3] = {
        kinematics->mass_sum, PHY_IR_NULL, PHY_IR_NULL};
    unsigned used = 1u;
    for (unsigned candidate = 0u; candidate < 3u; ++candidate) {
        if (candidate == (unsigned)invariant) {
            continue;
        }
        phy_status status = phy_cas_neg(
            kinematics->cas, kinematics->invariant[candidate],
            &terms[used]);
        if (status != PHY_OK) {
            return status;
        }
        used++;
    }
    phy_ir_ref replacement = PHY_IR_NULL;
    phy_status status = phy_cas_add(
        kinematics->cas, terms, used, &replacement);
    if (status == PHY_OK) {
        const phy_cas_rule rule = {
            kinematics->invariant[invariant], replacement};
        status = phy_cas_substitute(
            kinematics->cas, expression, &rule, 1u, out_expression);
    }
    return status;
}
