# Native coordinate-metric GR

`include/phy/gr.h` and `src/gr/gr.c` implement the first exact general
relativity pipeline. The input is a symmetric lower rank-2 component tensor on
one coordinate chart. The output transaction owns:

- the inverse metric;
- Levi-Civita Christoffel symbols;
- mixed and fully covariant Riemann tensors;
- Ricci and Einstein tensors;
- the Ricci scalar.

All component arithmetic uses the native symbolic CAS. No floating-point
sampling is used to decide an identity. The sign and contraction conventions
are fixed in `docs/references/GENERAL_RELATIVITY.md`.

The inverse uses a deterministic cofactor/adjugate construction for dimensions
up to the tensor ceiling of four. A determinant proved to be zero is rejected.
If non-vanishing cannot be decided symbolically, evaluation proceeds under the
ordinary coordinate-patch assumption that the metric is non-degenerate; this
assumption must eventually become explicit notebook metadata.

`tests/test_gr.c` currently checks:

- four-dimensional Cartesian Minkowski space: every connection and curvature
  component vanishes;
- a round two-sphere: the non-zero connection and Riemann component, positive
  scalar curvature `2/a^2`, and identically zero two-dimensional Einstein
  tensor;
- rejection of an object that is not a lower rank-2 metric.

Not yet implemented here: geodesics, Kretschmann/Weyl invariants, tetrads,
torsionful connections, atlas transitions, differential forms, a reader-facing
GR command evaluator, device benchmarks, or Kerr-specific simplification.
