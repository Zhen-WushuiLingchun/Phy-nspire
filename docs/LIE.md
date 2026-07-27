# Finite Lie algebra and group metadata

`include/phy/lie.h` and `src/lie/lie.c` provide the bounded algebra layer used
later by quantum mechanics and Yang--Mills.

An algebra is a named basis and an exact dense table

```text
[T_a, T_b] = f[a,b,c] T_c
```

with dimension at most eight. Construction proves antisymmetry and every
component of the Jacobi identity through the scalar CAS. A failed or
undecidable identity rejects the object rather than accepting sampled
evidence. Elements are exact CAS coefficient vectors; addition, scaling, and
the bilinear bracket preserve that representation.

The layer also exposes:

- individual structure constants;
- adjoint-representation matrix components;
- the Killing form `Tr(ad(T_a) ad(T_b))`;
- a generic typed-IR noncommutative commutator `A.B - B.A`;
- group metadata: name, compactness, standard representation dimension, and
  owned Lie algebra.

The built-in catalog fixes the following conventions:

| group | basis/convention | compact |
| --- | --- | --- |
| `U(1)` | one generator, zero bracket | yes |
| `SU(2)` | `[T_i,T_j] = epsilon_ijk T_k` | yes |
| `SO(3)` | the same real epsilon algebra, 3D standard representation | yes |
| `SU(3)` | Gell-Mann `f_abc` normalization, including `sqrt(3)/2` entries | yes |
| `SO(1,3)` | `J_i,K_i`: `[J,J]=epsilon J`, `[J,K]=epsilon K`, `[K,K]=-epsilon J` | no |

The `SU(2)` convention omits the explicit physics factor `i`; callers that use
Hermitian matrix generators must include their chosen `i` normalization when
mapping matrices to the abstract real structure constants.

Host tests cover all catalog constructors and their full Jacobi validation,
`SU(2)` brackets, symbolic bilinearity, the Killing form, the generic
commutator, and rejection of an antisymmetric table that violates Jacobi.

Not yet included:

- automatic recovery of structure constants from arbitrary matrix generators;
- roots, weights, Cartan/Weyl machinery and representations beyond metadata;
- global topology, covering groups, Haar integration, or a general
  exponential/logarithm map;
- superalgebras or infinite-dimensional algebras;
- notebook parser heads and palettes for the public API.
