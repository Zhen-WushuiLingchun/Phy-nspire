# Roadmap

Each phase must leave a runnable or independently testable artifact.

## Phase 0 — reproducible native baseline

Output:

- pinned Ndless SDK and upstream references;
- host and ARM build entry points;
- size-report and symbol-report targets;
- a native program that initializes the CX II framebuffer, input, and clean
  exit path.

Verification:

- host smoke test;
- generated `.tns` size report;
- launch/exit on the real CX II without display corruption.

## Phase 1 — notebook and CAS boundary

Output:

- Markdown and two-dimensional math cells;
- touchpad selection and palette shell;
- editable source cells;
- backend-neutral typed expression IR;
- native Giac adapter for a small scalar command set.

Verification:

- deterministic framebuffer fixtures;
- parse/evaluate/render/save/reopen workflow;
- cancellation and expression-limit tests.

## Phase 2 — tensor and manifold CAS

Output:

- manifolds, charts, metrics, indices, symmetries, contraction, canonical dummy
  indices, covariant derivatives, and differential forms;
- optional xPerm C integration after independent tests pass.

Verification:

- tensor identities and canonicalization properties;
- comparison corpus derived from xAct examples;
- bounded rank/dimension benchmarks on desktop and CX II.

## Phase 3 — general relativity and black holes

Output:

- Christoffel, Riemann, Ricci, Ricci scalar, Einstein tensor, geodesics, and
  selected invariants;
- Schwarzschild and Kerr-family example notebooks.

Verification:

- flat-space zero-curvature tests;
- known Schwarzschild/Kerr identities;
- coordinate-transformation consistency checks.

## Phase 4 — quantum mechanics

Output:

- operator algebra, bra-ket notation, Pauli/angular momentum helpers, tensor
  products, density matrices, and expectation values.

Verification:

- canonical commutators;
- spin identities;
- Hermiticity, trace, and basis-change properties.

## Phase 5 — QFT and gauge theory

Output:

- Lorentz contraction, Dirac algebra/traces, spin and polarization sums,
  Mandelstam substitutions, SU(N) color algebra, covariant derivatives, and
  field strengths.

Verification:

- small FeynCalc-compatible golden corpus;
- Clifford, Ward-identity, and group-theory identities;
- resource-limit behavior on intentionally explosive expressions.

## Phase 6 — diagram notebook cells

Output:

- touchpad graph editor;
- standard propagator drawing styles;
- serialization and rule-linked tree-level amplitudes;
- textual/LaTeX-oriented export.

Verification:

- graph round trips;
- hit-testing and 320 × 240 interaction fixtures;
- simple QED tree diagrams and amplitude skeletons.
