# Native coordinate-metric GR

`include/phy/gr.h` and `src/gr/gr.c` implement the first exact general
relativity pipeline. The input is a symmetric lower rank-2 component tensor on
one coordinate chart. The output transaction owns:

- the inverse metric;
- Levi-Civita Christoffel symbols;
- mixed and fully covariant Riemann tensors;
- Ricci and Einstein tensors;
- the Ricci scalar;
- an on-demand cached Kretschmann invariant and fully contravariant Riemann
  tensor;
- the full component covariant derivative of a rank-0 through rank-3 tensor,
  with a prepended lower derivative slot;
- the covariant Weyl tensor and the exact `C_abcd C^abcd` invariant;
- the exact geodesic acceleration
  `-Gamma^mu_nu_rho v^nu v^rho` for a contravariant velocity field.

All component arithmetic uses the native symbolic CAS. No floating-point
sampling is used to decide an identity. The sign and contraction conventions
are fixed in `docs/references/GENERAL_RELATIVITY.md`.

The inverse uses a deterministic cofactor/adjugate construction for dimensions
up to the tensor ceiling of four. A determinant proved to be zero is rejected.
If non-vanishing cannot be decided symbolically, evaluation proceeds under the
ordinary coordinate-patch assumption that the metric is non-degenerate; this
assumption must eventually become explicit notebook metadata.

`tests/test_gr.c` and the independent
`research/corpus/gr_golden.json` acceptance path check:

- four-dimensional Cartesian Minkowski space: every connection and curvature
  component vanishes;
- a round two-sphere: the non-zero connection and Riemann component, positive
  scalar curvature `2/a^2`, and identically zero two-dimensional Einstein
  tensor;
- Schwarzschild: Ricci-flatness, Einstein-flatness, and
  `R_abcd R^abcd = 48 M^2/r^6`;
- Reissner--Nordstrom: vanishing scalar curvature with nonzero Ricci/Einstein
  components;
- de Sitter: `R_abcd R^abcd = 24/L^4` and the contracted Bianchi identity;
- conformal curvature: zero Weyl for Minkowski, the 2-sphere convention,
  three-dimensional `S^2 x R`, and de Sitter; `C^2 = K` in Schwarzschild and
  `C^2 = 48 (M r - Q^2)^2/r^8` in Reissner--Nordstrom;
- polar-coordinate geodesic acceleration, including both connection terms;
- the covariant derivative identities included in the 1,362-line corpus;
- rejection of an object that is not a lower rank-2 metric.

The notebook exposes `Curvature`, all stored tensor accessors,
`Kretschmann[c]`, `Weyl[c]`, `WeylSquared[c]`,
`GeodesicAcceleration[c,v]`, and `CovariantDerivative[T,c]`.

Not yet implemented here: integration of geodesic trajectories or boundary
conditions, tetrads, torsionful connections, atlas transitions, Kerr-specific
simplification, or measured on-device curvature timings.
