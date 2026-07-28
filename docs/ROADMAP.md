# Roadmap

Each phase must leave a runnable or independently testable artifact.

New domain-level physics features are frozen while the dependency-ordered
scalar work in [`CAS_FOUNDATION.md`](CAS_FOUNDATION.md) is active. That plan
puts exact elementary functions, polynomial/GCD cancellation, exact number
domains, and only then series/solve/special functions beneath the existing
tensor, GR, and QFT layers.

## Phase 0 — reproducible native baseline

Status: implemented, with the first on-device execution and clean-exit
acceptance complete.

Output:

- pinned Ndless SDK and upstream references — `research/upstreams.lock.json`,
  resolved by `tools/bootstrap-ndless.sh`;
- host and ARM build entry points — `CMakeLists.txt` and `Makefile`;
- size-report and symbol-report targets — `make size-report`,
  `make symbol-report`;
- a native program that initializes the CX II framebuffer, input, and clean
  exit path — `src/app`, `src/platform/ndless`.

Verification:

- host smoke test — done; the suite covers the platform, relative pointer,
  source language, drawing, notebook, the stateful evaluator, IR, tensor
  storage, differential forms, GR, Lie/QFT foundations, CAS, QFT oracle, and
  full lifecycle: Windows 33/33, WSL ASan/UBSan/leak 35/35, and 193,903
  explicit checks;
- generated `.tns` size report — 1,134,215 bytes, 18.0% of the 6 MiB ceiling,
  with the current evaluator and physics stack linked;
- launch of a Phy-nspire artifact on the real CX II — done on 2026-07-26 with
  the observable CAS smoke screen;
- clean exit without display corruption — done; `ESC` returned normally to
  Documents;
- RGB channel order is **not yet recorded**. The first pointer behavior was
  rejected on hardware; acceptance of the corrected relative navigation/click
  filtering is pending a fresh device run. `docs/BUILD.md` records the
  procedure.

## Phase 1 — notebook and CAS boundary

Status: the typed expression IR, scalar CAS, persistence shell, first
nMarkdown-backed LaTeX rendering pass, and the stateful evaluator are
implemented. The CAS and earlier input shell passed physical smoke tests; the
persistence + typesetter + evaluator build and exhaustive tour notebook are
now byte-identical on the calculator, but still need an interactive calculator
acceptance run. The current evaluator is ARM-built and link-checked, but its
QFT/GR paths still need physical-device timing and acceptance.

Output:

- Markdown heading/body cells — bounded form done, including inline `$...$` /
  `\(...\)` and display `$$...$$` / `\[...\]` mathematics through the pinned
  nMarkdown OpenType MATH stack;
- two-dimensional math output — exact fractions, powers, functions,
  multiplication, addition, equations, tensors/operators, derivatives, and
  indices are laid out directly from typed IR;
- relative touchpad and directional-key selection — done; each new touch
  continues from the cursor's previous screen position rather than mapping
  absolute finger coordinates;
- independent upper-right `RUN` badge per executable cell — done with a
  separately tested hit region;
- character-level source/Markdown editing, stale-result marking,
  selection-following scrolling, and functional `+MD`/`+Math` insertion —
  done for the bounded first shell;
- empty-notebook startup, atomic Save/Open, sorted file picker, dirty-state
  confirmation, and default `/documents/phy-nspire/notebooks` path — done;
- extensible Mathematica-style source front end — done for exact
  integers/decimals, implicit and explicit arithmetic, equations, lists,
  scalar/full-form heads, multiple derivatives, and the command registry in
  [`docs/SOURCE_LANGUAGE.md`](SOURCE_LANGUAGE.md);
- CAS/LaTeX insertion palette — done; richer tensor/physics object palettes
  now include geometry, GR, Lie/Yang--Mills, Dirac/Mandelstam, phi4, and
  symbolic SU(N) colour
  insertion entries; graphical particle/diagram palettes are not yet done;
- backend-neutral typed expression IR — done, `include/phy/ir.h`, `src/ir`,
  documented in [`docs/IR.md`](IR.md);
- native scalar algebra and rewriting — done for the documented bounded class,
  `include/phy/cas.h`, `src/cas`, documented in
  [`docs/CAS.md`](CAS.md): native arbitrary-precision integer/rational atom
  folding with an `int64` fast path, a normal form, expansion, substitution,
  differentiation, and an exact zero decision. The older polynomial
  coefficient containers remain checked `int64` pending F3 migration;
- native Giac adapter for a small scalar command set — **not needed for the
  scalar operations the tensor and curvature phases require**, which the layer
  above now supplies natively. The backend boundary in
  `docs/ARCHITECTURE.md` stands, but nothing downstream is blocked on it;
- stateful notebook evaluator — done, `include/phy/eval.h`, `src/eval`,
  documented in [`EVALUATOR.md`](EVALUATOR.md): a per-notebook environment of
  named typed values, `name = value` assignment, and dispatch of every reserved
  physics head onto the differential-geometry, Lie-algebra, Yang--Mills, tensor,
  general-relativity, and bounded QFT backends. This is what made those layers reachable from
  the product: before it they were fully implemented, fully tested, and called
  by nothing, because the notebook handed their heads to the scalar CAS, which
  preserved them.

Verification:

- deterministic framebuffer fixtures — done for the baseline and notebook;
- parse/evaluate/render workflow — done for the two seeded native CAS cells;
- save/reopen workflow — host tests done, physical-device acceptance pending;
- cancellation and expression-limit tests — done for the CAS, `tests/test_cas.c`:
  the step budget, the cancellation hook, and the IR's term limit each surface as
  a typed status and leave both layers validating;
- IR unit tests — done, `tests/test_ir.c`, 2,843 checks covering interning,
  canonical ordering, the construction ceilings, and text round-trips;
- CAS unit tests — done, `tests/test_cas.c`, 2,076 checks covering the normal
  form, exact arithmetic and arbitrary-precision promotion, differentiation, bounded
  exact factorization, and the zero decision, including the four `sphere_2d`
  corpus entries whose stated trigonometric form differs from the computed one.
- notebook tests — done, `tests/test_notebook.c`, 215 checks covering bounded
  cell storage, exact seeded results, editing, insertion, source/IR agreement,
  stale outputs, Markdown selection, independent run-badge hit testing, 2D
  metrics, nMarkdown LaTeX integration, memory return, and the framebuffer
  fixture;
- evaluator tests — done, `tests/test_eval.c`, 1,642 checks. The physics cases
  reproduce, through reader-facing source, results the backend suites already
  certify directly: the U(1) and SU(2) curvature components and vanishing
  Bianchi residuals of `tests/test_yang_mills.c`, the round two-sphere
  curvature of `tests/test_gr.c`, the wedge/`d^2 = 0`/Leibniz/interior-product
  identities of `tests/test_geom.c`, `[T1,T2] = T3` and `K_ab = -2 delta_ab`
  from `tests/test_lie.c`, plus Kretschmann/covariant-derivative and bounded
  Dirac/Mandelstam/phi4/SU(N)-colour cell paths, including exact sunset-graph
  combinatorics. The remaining cases cover state flow between cells,
  every typed-error path, the ownership sweep under rebinding and failure, the
  binding ceiling, and save/reopen;
- formula bridge tests — done, `tests/test_formula.c`, 33 checks covering
  initialization, matrices, metrics, RGB565 rendering, and local error
  recovery;
- source, palette and pointer tests — done: 296 source-language checks
  including assignment and reserved-head canonicalization, 828 palette checks
  including every CAS snippet parsing, and 29 relative touchpad checks.

The IR carries no simplification, evaluation, or arithmetic: it is the
substrate those work on. Dummy-index canonicalization and anything that
consumes declared symmetries stay in Phase 2.

The real Ndless r2022/ARM GNU toolchain link check is done for the CAS: 29/29
CAS APIs survive garbage collection and the probe packages to a 90,248-byte
`.tns` without float formatting, libm, or ARM soft-float dependencies. The
observable `phy-cas-smoke.tns` then ran seven symbolic cases on the physical
CX II on 2026-07-26, displayed 7/7 PASS, and returned cleanly to Documents.

The evaluator's real Ndless check now compiles 34 portable sources, retains
15/15 public evaluator entry points, packages a 156,328-byte isolated probe,
and contains no float formatter, libm call, or ARM soft-float helper. The
product is 1,134,215 bytes. The independent SU(N) colour probe retains 23/23
public APIs, 4,924 bytes of layer text, and packages to 52,764 bytes under the
same no-float rule. These establish ARM link/package and size, not
physical-device runtime or performance.

## Phase 2 — tensor and manifold CAS

Status: component storage, slot symmetries, exact component contraction,
raise/lower, cofactor metric inversion, and component partial derivatives are
implemented. The differential-form layer over them has also landed — manifolds
with
  orientation and signature, canonical antisymmetric components, and exact wedge,
  exterior derivative, interior product, Lie derivative and Hodge dual, documented in
[`docs/GEOMETRY.md`](GEOMETRY.md). Transition maps/pullbacks, abstract
dummy-index canonicalization, and higher-level covariant form operations remain
open.

The layer is now reachable from the notebook: `Manifold`, `ComponentTensor`,
`DifferentialForm`, `Metric`, `VectorField`, `Wedge`, `ExteriorD`, `InteriorProduct`,
`LieDerivative`, `HodgeStar` and `Volume` dispatch onto it through
[`docs/EVALUATOR.md`](EVALUATOR.md) rather than surviving as operator heads.

Output:

- manifolds, charts, metrics, indices, symmetries, contraction, canonical dummy
  indices, covariant derivatives, and differential forms — forms, contraction,
  raise/lower, coordinate-metric GR, and component tensor covariant
  derivatives are done; abstract canonical dummy indices and transition-map
  syntax remain outstanding;
- the component constructor covers ranks 0 through 4 and every slot-variance
  pattern inside the dimension-4 native ceiling; unbounded rank remains
  intentionally out of scope;
- optional xPerm C integration after independent tests pass.

Deferred with a named blocking dependency:

- pullback along a coordinate map, which needs a validated `phy_map` with
  provably disjoint coordinate symbols — substituting without that silently
  captures and returns a wrong answer.

Verification:

- tensor identities and canonicalization properties — the exterior-calculus
  identities are done, `tests/test_geom.c`, 4,646 checks: graded commutativity
  and associativity of the wedge, `d^2 = 0`, both graded Leibniz rules,
  `iota_v iota_v = 0`, and `** = (-1)^{p(n-p)} sign(det g)` at every degree in
  Euclidean and Lorentzian 2D, Euclidean 3D and Minkowski 4D;
- general coordinate-metric Hodge/volume cases are done for diagonal,
  non-diagonal, Lorentzian, negative-orientation, symbolic-determinant,
  singular, nonsymmetric, inertia-mismatched, and unoriented inputs in
  `tests/test_geom_metric.c`, including the general-metric `**` identity;
- comparison corpus derived from xAct examples;
- bounded rank/dimension benchmarks on desktop and CX II;
- the ARM geometry link check retains 45/45 APIs with 8,957 bytes of layer
  text and no float/libm/soft-float dependency;

## Phase 3 — general relativity and black holes

Status: the first coordinate-metric curvature pipeline is implemented. It
computes the inverse metric, Christoffel symbols, mixed/covariant/fully
contravariant Riemann tensors, Ricci tensor, scalar curvature, Einstein tensor,
Kretschmann invariant, covariant Weyl tensor and squared invariant, geodesic
acceleration, and component tensor covariant derivatives through the native
exact CAS. The committed corpus covers Cartesian Minkowski space, the round
2-sphere, Schwarzschild, Reissner--Nordstrom, and de Sitter. Numerical
trajectory integration/boundary conditions, Kerr-specific reduction, and
physical-device timing remain open.

UI commands are done: `Curvature[g]` and the `InverseMetric`, `Christoffel`,
`Riemann`, `RiemannMixed`, `Ricci`, `RicciScalar`, `Einstein`,
`Kretschmann`, `Weyl`, `WeylSquared`, `GeodesicAcceleration`, and
`CovariantDerivative` run from notebook cells, and
`tests/test_eval.c` reproduces the two-sphere invariants through them.

Output:

- Christoffel, Riemann, Ricci, Ricci scalar, Einstein tensor, the affine
  geodesic equation's exact acceleration, and selected invariants;
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

Status: the finite-dimensional Lie-algebra foundation is implemented with
exact structure constants, Jacobi validation, brackets, adjoint/Killing
operations, and built-in `U(1)`, `SU(2)`, `SO(3)`, `SU(3)`, and `SO(1,3)`
metadata. A bounded real `phi^4` layer now emits the exact Lagrangian,
propagator/vertex objects, and the one-loop tadpole plus `s/t/u` bubble
topologies with exact symmetry/coupling weights and unevaluated master
integrals. Its supplied-topology analyser covers connected quartic multigraphs
through four vertices/eight labelled external legs and returns exact Wick
multiplicity, vertex automorphisms, `S`, `1/S`, loop order, superficial degree,
and `lambda^V/S`. It also returns the exact one-loop MS/MSbar multiplicative
renormalization constants and local counterterm density in the explicitly
declared `D=4-2 epsilon` convention. The differential-form/Lie foundation now also supports
Lie-algebra-valued forms, `D_A`, non-Abelian
`F=dA+(g/2)[A,A]`, infinitesimal gauge variations, explicit Bianchi residuals,
and `-1/2 h_ab F^a wedge star_g(F^b)` with a general coordinate metric.
The four-dimensional Lorentz/Dirac layer now implements typed metric/index
spaces, momenta and scalar products, Clifford normalisation, contraction,
multi-spin-line ordering, traces without gamma-five, and explicit Peskin or
all-incoming Mandelstam routing. Native symbolic `SU(N)` colour algebra now
covers symbolic `N`, invariant tensors, generator commutators, traces
through length three, held long traces, exact `C_F/C_A`, and textbook SU(2)/
SU(3) components. General colour dummy canonicalization, Fierz/completeness,
finite dimensionally regulated master integrals, general graph generation,
gauge fixing/ghosts, Ward identities, and renormalization beyond the bounded
phi4 one-loop MS/MSbar result remain scoped rather than implemented.
The MVP boundary, the pinned
conventions, the algorithm specification and the verified identity set are in
[`docs/references/QFT_GAUGE.md`](references/QFT_GAUGE.md); the contracts that
implement them are in
[`docs/agent-tasks/QFT_DIRAC.md`](agent-tasks/QFT_DIRAC.md).

Output, MVP:

- Lorentz contraction, Dirac algebra and traces without gamma-5, Mandelstam
  substitutions, and SU(N) color algebra.

Output, deferred:

- gamma-5 and chiral projectors, Fierz rearrangement, spin and polarization
  sums, squared amplitudes, general loop reduction, perturbative gauge-field
  vertices, gauge fixing/ghosts, and Ward identities.

Exterior algebra, Lie-algebra-valued forms, the bounded `phi^4`
field/vertex/propagator workflow, and the classical Yang--Mills
connection/curvature slice are implemented and documented in
[`docs/GEOMETRY.md`](GEOMETRY.md),
[`docs/QFT_SCALAR.md`](QFT_SCALAR.md), and
[`docs/YANG_MILLS.md`](YANG_MILLS.md). The exact colour conventions and
command boundary are documented in [`docs/COLOR.md`](COLOR.md).

The Lie, Yang--Mills, Dirac/Mandelstam and bounded phi4 slices are reachable
from the notebook: `LieGroup`,
`LieAlgebra`, `Generator`, `LieElement`, `LieBracket`, `StructureConstant`,
`Killing`, `LieForm`, `GaugeConnection`, `CovariantD`, `FieldStrength`,
`GaugeVariation`, `Bianchi`, `YangMillsLagrangian`, `ColorComponent`,
`DiracTrace`, `MandelstamReduce`, `Phi4Lagrangian`, `Phi4EOM`,
`Phi4Diagrams`, `Phi4Graph`, `Phi4Renormalization`, `Phi4Counterterm`, plus `SUNDelta`,
`SUNF`, `SUND`, `SUNT`, `SUNTrace`,
`SUNCommutator`, `SUNCF`, `SUNCA`, and the other bounded `SUN*` commands,
dispatch onto native backends. Typed master-integral and
gamma/momentum heads remain output vocabulary, not no-op commands.

Verification:

- golden corpus certified against an explicit matrix representation in
  `tests/oracle/` — done for the MVP set, 44,295 checks over 19 identities in
  both the Dirac and Weyl representations;
- Clifford and group-theory identities — done. Ward-identity checks move with
  the deferred amplitude layer;
- classical gauge acceptance — done for exact U(1)/SU(2) curvature,
  infinitesimal variation, Bianchi residual, and a general-metric quadratic
  density; the ARM probe retains 22/22 Yang--Mills APIs with 4,524 bytes of
  layer text and no float/libm/soft-float dependency;
- Dirac resource-limit behavior is host-tested and the whole evaluator stack
  passes the ARM link/no-soft-float check. The dedicated 42,948-byte Q-7
  benchmark has been built, transferred to the CX II, and read back
  byte-identically. The proposed throughput ceilings remain **UNVERIFIED on
  CX II** until that artifact is actually run and its four displayed rows plus
  a subsequent notebook-save check are recorded.
- SU(N) colour contract Q-6 is done: 216 direct exact tests, the independent
  C-1...C-7 generalized Gell-Mann oracle, reader-facing evaluator coverage,
  and the 23/23-API ARM link check. The Fierz identity remains explicitly
  deferred.

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
