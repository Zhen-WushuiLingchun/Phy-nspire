/*
 * Routed Mandelstam kinematics for four external momenta.
 *
 * A bare declaration of s, t, and u is ambiguous: p3 and p4 may be outgoing
 * (the common 2 -> 2 convention) or every momentum may be incoming (the FORM /
 * FeynCalc convention).  This object records that choice and derives every
 * external scalar product from it.
 */
#ifndef PHY_MANDELSTAM_H
#define PHY_MANDELSTAM_H

#include "phy/lorentz.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* p1 + p2 -> p3 + p4; t=(p1-p3)^2, u=(p1-p4)^2. */
    PHY_MANDELSTAM_PESKIN_2_TO_2 = 0,
    /* p1+p2+p3+p4=0; t=(p1+p3)^2, u=(p1+p4)^2. */
    PHY_MANDELSTAM_ALL_INCOMING
} phy_mandelstam_routing;

typedef enum {
    PHY_MANDELSTAM_S = 0,
    PHY_MANDELSTAM_T,
    PHY_MANDELSTAM_U
} phy_mandelstam_invariant;

typedef struct phy_mandelstam phy_mandelstam;

/*
 * Create a routed four-leg kinematic context.
 *
 * The four momenta must already be declared by `metric`.  `mass[i]` is the
 * exact mass (not mass squared) of p_i.  Creation declares the matching
 * conservation law and on-shell conditions in the Lorentz metric.
 */
phy_status phy_mandelstam_create(
    phy_lorentz_metric *metric, const phy_ir_ref momentum[4],
    const phy_ir_ref mass[4], phy_mandelstam_routing routing,
    phy_mandelstam **out_kinematics);
void phy_mandelstam_destroy(phy_mandelstam *kinematics);

phy_mandelstam_routing phy_mandelstam_routing_of(
    const phy_mandelstam *kinematics);
phy_ir_ref phy_mandelstam_symbol(
    const phy_mandelstam *kinematics, phy_mandelstam_invariant invariant);
phy_ir_ref phy_mandelstam_definition(
    const phy_mandelstam *kinematics, phy_mandelstam_invariant invariant);

/* Right-hand side of s+t+u = m1^2+m2^2+m3^2+m4^2. */
phy_ir_ref phy_mandelstam_sum_rule_rhs(
    const phy_mandelstam *kinematics);

/*
 * Replace every p_i.p_j by the routed expression in s,t,u and masses.
 * Scalar products outside this context are left unchanged.
 */
phy_status phy_mandelstam_reduce(const phy_mandelstam *kinematics,
                                 phy_ir_ref expression,
                                 phy_ir_ref *out_expression);

/* Eliminate exactly one of s,t,u with the sum rule. */
phy_status phy_mandelstam_eliminate(
    const phy_mandelstam *kinematics, phy_ir_ref expression,
    phy_mandelstam_invariant invariant, phy_ir_ref *out_expression);

#ifdef __cplusplus
}
#endif

#endif /* PHY_MANDELSTAM_H */
