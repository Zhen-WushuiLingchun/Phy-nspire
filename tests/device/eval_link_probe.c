/*
 * Device link-retention probe for include/phy/eval.h.
 *
 * This is the layer the notebook now calls, so unlike the layers below it the
 * evaluator will genuinely reach dist/phy-nspire.tns. The probe still exists
 * for the two things the product build cannot show on its own: that every
 * declared entry point survives --gc-sections, and that pulling the geometry,
 * Lie and Yang-Mills backends in behind one dispatcher drags in no floating
 * point formatter, libm call, or ARM soft-float helper.
 *
 * It is never part of the product binary.
 */
#include "phy/eval.h"
#include "phy/platform.h"

volatile unsigned g_phy_eval_probe_sink;

static void sink(unsigned value)
{
    g_phy_eval_probe_sink += value;
}

static void probe_environment(phy_env *env)
{
    phy_value value;
    const char *name = 0;

    sink(phy_env_cas(env) != 0 ? 1u : 0u);
    sink(phy_env_ir(env) != 0 ? 1u : 0u);
    sink((unsigned)phy_env_binding_count(env));
    sink((unsigned)phy_env_object_count(env));
    sink(phy_env_binding(env, 0u, &name, &value) ? 1u : 0u);
    sink(phy_env_lookup(env, "M", &value) ? 1u : 0u);
    sink((unsigned)phy_env_validate(env));
    sink(name != 0 ? 1u : 0u);
    phy_env_reset(env);
}

static void probe_evaluation(phy_env *env, phy_ir_context *ir)
{
    phy_source_command command;
    phy_value value;
    phy_ir_ref expression = PHY_IR_NULL;
    char description[PHY_EVAL_DESCRIPTION_CAPACITY];

    command.operation = PHY_SOURCE_SIMPLIFY;
    command.expression = phy_ir_integer(ir, 1);
    command.variable_count = 0u;
    command.target = PHY_IR_NO_SYMBOL;
    command.variables[0] = PHY_IR_NULL;

    sink((unsigned)phy_eval_command(env, &command, &value));
    sink((unsigned)phy_eval_expression(env, command.expression, &value));
    sink((unsigned)phy_eval_value_expression(env, value, &expression));
    sink((unsigned)phy_eval_describe(env, value, description,
                                     sizeof description));
    sink((unsigned)expression);
    sink(description[0] != '\0' ? 1u : 0u);
    sink(phy_value_kind_name(value.kind) != 0 ? 1u : 0u);
}

int main(void)
{
    if (phy_platform_init() != PHY_OK) {
        return 1;
    }
    phy_ir_context *ir = phy_ir_context_create(0);
    phy_cas *cas = ir != 0 ? phy_cas_create(ir, 0) : 0;
    phy_env *env = cas != 0 ? phy_env_create(cas) : 0;
    if (env != 0) {
        probe_environment(env);
        probe_evaluation(env, ir);
        phy_env_destroy(env);
    }
    phy_cas_destroy(cas);
    phy_ir_context_destroy(ir);
    phy_platform_shutdown();
    return (int)(g_phy_eval_probe_sink & 1u);
}
