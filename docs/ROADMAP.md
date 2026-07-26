# Roadmap

Each phase must leave a runnable or independently testable artifact.

## Phase 0 — reproducible native baseline

Status: implemented, pending on-device verification.

Output:

- pinned Ndless SDK and upstream references — `research/upstreams.lock.json`,
  resolved by `tools/bootstrap-ndless.sh`;
- host and ARM build entry points — `CMakeLists.txt` and `Makefile`;
- size-report and symbol-report targets — `make size-report`,
  `make symbol-report`;
- a native program that initializes the CX II framebuffer, input, and clean
  exit path — `src/app`, `src/platform/ndless`.

Verification:

- host smoke test — done, `ctest` runs four suites covering the platform
  contract, drawing, and the full lifecycle;
- generated `.tns` size report — done, 12,676 bytes against a 6 MB ceiling;
- launch/exit on the real CX II without display corruption — **not done**, no
  hardware has been used. `docs/BUILD.md` records the procedure, including the
  RGB channel-order check and the exit check that Phase 0 exists to protect.

## Phase 1 — notebook and CAS boundary

Status: the typed expression IR is implemented; the rest is not started.

Output:

- Markdown and two-dimensional math cells;
- touchpad selection and palette shell;
- editable source cells;
- backend-neutral typed expression IR — done, `include/phy/ir.h`, `src/ir`,
  documented in `docs/IR.md`;
- native Giac adapter for a small scalar command set.

Verification:

- deterministic framebuffer fixtures;
- parse/evaluate/render/save/reopen workflow;
- cancellation and expression-limit tests;
- IR unit tests — done, `tests/test_ir.c`, 2,259 checks covering interning,
  canonical ordering, the construction ceilings, and text round-trips.

The IR carries no simplification, evaluation, or arithmetic: it is the
substrate those work on. Dummy-index canonicalization and anything that
consumes declared symmetries stay in Phase 2.

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

Status: scoped and sourced, not implemented. The MVP boundary, the pinned
conventions, the algorithm specification and the verified identity set are in
[`docs/references/QFT_GAUGE.md`](references/QFT_GAUGE.md); the contracts that
implement them are in
[`docs/agent-tasks/QFT_DIRAC.md`](agent-tasks/QFT_DIRAC.md).

Output, MVP:

- Lorentz contraction, Dirac algebra and traces without gamma-5, Mandelstam
  substitutions, and SU(N) color algebra.

Output, deferred with a named blocking dependency (reference §2):

- gamma-5 and chiral projectors, Fierz rearrangement, spin and polarization
  sums, squared amplitudes, loop integrals, and the covariant
  derivative/field-strength layer, which needs the Phase 2 tensor core.

Verification:

- golden corpus certified against an explicit matrix representation in
  `tests/oracle/` — done for the MVP set, 44,295 checks over 19 identities in
  both the Dirac and Weyl representations;
- Clifford and group-theory identities — done. Ward-identity checks move with
  the deferred amplitude layer;
- resource-limit behavior on intentionally explosive expressions — **not
  done**. The limits proposed in reference §8 are combinatorial arithmetic plus
  figures quoted from FORM's manual, and no hardware has been used. Contract
  Q-7 settles this.

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
