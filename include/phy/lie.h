/*
 * Finite-dimensional Lie algebras and Lie-group metadata.
 *
 * This is a bounded basis/structure-constant layer for geometry, quantum
 * mechanics, and gauge theory. Coefficients are exact scalar-CAS expressions;
 * no matrix or floating-point backend is hidden behind the API.
 */
#ifndef PHY_LIE_H
#define PHY_LIE_H

#include <stdbool.h>

#include "phy/cas.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHY_LIE_MAX_DIM 8u

typedef struct phy_lie_algebra phy_lie_algebra;
typedef struct phy_lie_element phy_lie_element;
typedef struct phy_lie_group phy_lie_group;

typedef enum {
    PHY_LIE_GROUP_U1 = 0,
    PHY_LIE_GROUP_SU2,
    PHY_LIE_GROUP_SO3,
    PHY_LIE_GROUP_SU3,
    PHY_LIE_GROUP_SO13
} phy_lie_group_kind;

/*
 * Create [T_a,T_b] = f[a,b,c] T_c from a dense dimension^3 table.
 * Construction proves antisymmetry and every Jacobi component exactly.
 * An undecidable identity is rejected with PHY_ERR_ASSUMPTION.
 *
 * The algebra borrows `cas` and its IR context. Basis names and the algebra
 * name are interned. The coefficient handles are copied.
 */
phy_status phy_lie_algebra_create(
    phy_cas *cas, const char *name, const char *const *basis_names,
    unsigned dimension, const phy_ir_ref *structure_constants,
    phy_lie_algebra **out_algebra);
void phy_lie_algebra_destroy(phy_lie_algebra *algebra);

phy_cas *phy_lie_algebra_cas(const phy_lie_algebra *algebra);
const char *phy_lie_algebra_name(const phy_lie_algebra *algebra);
unsigned phy_lie_algebra_dimension(const phy_lie_algebra *algebra);
phy_ir_symbol phy_lie_algebra_basis_symbol(const phy_lie_algebra *algebra,
                                           unsigned basis);
phy_ir_ref phy_lie_structure_constant(const phy_lie_algebra *algebra,
                                      unsigned a, unsigned b, unsigned c);

/* Re-run the exact antisymmetry and Jacobi checks. */
phy_status phy_lie_algebra_validate(const phy_lie_algebra *algebra);

/*
 * (ad T_a)^row_col = f[a,col,row].
 * K_ab = Tr(ad T_a ad T_b).
 */
phy_ir_ref phy_lie_adjoint_component(const phy_lie_algebra *algebra,
                                     unsigned generator, unsigned row,
                                     unsigned column);
phy_status phy_lie_killing_component(const phy_lie_algebra *algebra,
                                     unsigned a, unsigned b,
                                     phy_ir_ref *out_value);

/* Elements are dense coefficient vectors over the algebra's fixed basis. */
phy_status phy_lie_element_create(const phy_lie_algebra *algebra,
                                  const phy_ir_ref *coefficients,
                                  phy_lie_element **out_element);
phy_status phy_lie_basis_element(const phy_lie_algebra *algebra,
                                 unsigned basis,
                                 phy_lie_element **out_element);
void phy_lie_element_destroy(phy_lie_element *element);
const phy_lie_algebra *phy_lie_element_algebra(
    const phy_lie_element *element);
phy_ir_ref phy_lie_element_coefficient(const phy_lie_element *element,
                                       unsigned basis);

phy_status phy_lie_element_add(const phy_lie_element *left,
                               const phy_lie_element *right,
                               phy_lie_element **out_element);
phy_status phy_lie_element_scale(const phy_lie_element *element,
                                 phy_ir_ref scalar,
                                 phy_lie_element **out_element);
phy_status phy_lie_bracket(const phy_lie_element *left,
                           const phy_lie_element *right,
                           phy_lie_element **out_element);

/* Generic noncommutative [A,B] = A.B - B.A in the shared typed IR. */
phy_status phy_lie_commutator_ir(phy_cas *cas, phy_ir_ref left,
                                 phy_ir_ref right, phy_ir_ref *out_expression);

/*
 * A first Lie-group object records global identity/representation metadata and
 * owns its algebra. It does not claim to solve global topology, exponential
 * maps, or arbitrary matrix-group recognition.
 */
phy_status phy_lie_group_create(
    phy_cas *cas, const char *name, unsigned representation_dimension,
    bool compact, const char *const *basis_names, unsigned algebra_dimension,
    const phy_ir_ref *structure_constants, phy_lie_group **out_group);
phy_status phy_lie_group_builtin(phy_cas *cas, phy_lie_group_kind kind,
                                 phy_lie_group **out_group);
void phy_lie_group_destroy(phy_lie_group *group);

const char *phy_lie_group_name(const phy_lie_group *group);
unsigned phy_lie_group_representation_dimension(const phy_lie_group *group);
bool phy_lie_group_is_compact(const phy_lie_group *group);
const phy_lie_algebra *phy_lie_group_algebra(const phy_lie_group *group);

#ifdef __cplusplus
}
#endif

#endif /* PHY_LIE_H */
