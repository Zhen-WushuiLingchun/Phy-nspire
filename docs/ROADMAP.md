# Roadmap

Each phase must leave a runnable or independently testable artifact.

## Phase 0 — reproducible native baseline

Status: implemented, with the first on-device execution complete and the
clean-exit acceptance still pending.

Output:

- pinned Ndless SDK and upstream references — `research/upstreams.lock.json`,
  resolved by `tools/bootstrap-ndless.sh`;
- host and ARM build entry points — `CMakeLists.txt` and `Makefile`;
- size-report and symbol-report targets — `make size-report`,
  `make symbol-report`;
- a native program that initializes the CX II framebuffer, input, and clean
  exit path — `src/app`, `src/platform/ndless`.

Verification:

- host smoke test — done, `ctest` runs nine tests covering the platform
  contract, drawing, IR, tensor storage, CAS, QFT oracle, and full lifecycle;
- generated `.tns` size report — done, 13,440 bytes against a 6 MB ceiling;
- launch of a Phy-nspire artifact on the real CX II — done on 2026-07-26 with
  the observable CAS smoke screen;
- clean exit without display corruption, plus the RGB channel-order and
  pointer checks — **not yet recorded**. `docs/BUILD.md` records the procedure.

## Phase 1 — notebook and CAS boundary

Status: the typed expression IR and the scalar CAS over it are implemented and
the CAS has passed its first physical-device smoke test; the graphical
notebook shell is not started. The smoke screen is not the notebook UI.

Output:

- Markdown and two-dimensional math cells;
- touchpad selection and palette shell;
- editable source cells;
- backend-neutral typed expression IR — done, `include/phy/ir.h`, `src/ir`,
  documented in [`docs/IR.md`](IR.md);
- native scalar algebra and rewriting — done, `include/phy/cas.h`, `src/cas`,
  documented in [`docs/CAS.md`](CAS.md): exact rational arithmetic, a normal
  form, expansion, substitution, differentiation, and an exact zero decision;
- native Giac adapter for a small scalar command set — **not needed for the
  scalar operations the tensor and curvature phases require**, which the layer
  above now supplies natively. The backend boundary in
  `docs/ARCHITECTURE.md` stands, but nothing downstream is blocked on it.

Verification:

- deterministic framebuffer fixtures;
- parse/evaluate/render/save/reopen workflow;
- cancellation and expression-limit tests — done for the CAS, `tests/test_cas.c`:
  the step budget, the cancellation hook, and the IR's term limit each surface as
  a typed status and leave both layers validating;
- IR unit tests — done, `tests/test_ir.c`, 2,577 checks covering interning,
  canonical ordering, the construction ceilings, and text round-trips;
- CAS unit tests — done, `tests/test_cas.c`, 742 checks covering the normal
  form, exact arithmetic and its overflow statuses, differentiation, and the
  zero decision, including the four `sphere_2d` corpus entries whose stated
  trigonometric form differs from the computed one.

The IR carries no simplification, evaluation, or arithmetic: it is the
substrate those work on. Dummy-index canonicalization and anything that
consumes declared symmetries stay in Phase 2.

The real Ndless r2022/ARM GNU toolchain link check is done: 24/24 CAS APIs
survive garbage collection and the probe packages to a 37,720-byte `.tns`
without float formatting, libm, or ARM soft-float dependencies. The observable
`phy-cas-smoke.tns` then ran seven symbolic cases on the physical CX II on
2026-07-26 and displayed 7/7 PASS.

## Phase 2 — tensor and manifold CAS

Status: component-independent tensor storage and slot symmetries are complete;
scalar-dependent contraction, raise/lower, metric inversion, and component
derivatives are next. The scalar substrate is in place, including the exact
zero decision that `docs/agent-tasks/TENSOR_CORE.md` calls load-bearing.

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
  done**, and the proposed ceilings are **UNVERIFIED**. They are combinatorial
  arithmetic plus figures quoted from FORM's manual, measured on unspecified
  workstation hardware. No CX II has been used at any point, so device
  performance is unmeasured. Contract Q-7 settles this.

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
