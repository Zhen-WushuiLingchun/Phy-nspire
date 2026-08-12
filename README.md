# Phy-nspire

[中文说明 (Chinese README)](README.zh-CN.md)

Phy-nspire is an Ndless-native symbolic physics notebook for the TI-Nspire
CX II CAS.

The project targets a touchpad-driven, two-dimensional notebook rather than a
linear command shell. Its first scientific layer is tensor calculus and
differential geometry. The longer roadmap covers general relativity and black
holes, quantum mechanics, QFT and gauge theory, and compact Feynman-diagram
workflows.

## Non-negotiable design constraints

- The production application is native ARM C/C++ built with the Ndless SDK.
  TI Lua may be used only as a reference or host-side comparison, never as the
  production execution layer.
- The target device is a TI-Nspire CX II CAS running OS 6.4.0.74 and
  Ndless r2022.
- The application target is nominally 5–6 MB, with exact accounting for
  optional fonts and documentation still to be finalized.
- The UI must provide touchpad pointer interaction, palettes, notebook cells,
  two-dimensional mathematics, Markdown notes, and bounded LaTeX rendering.
- Long calculations must be cancellable and bounded by explicit memory and
  term-count limits.

## Current status

Phase 0, the reproducible native baseline, is implemented. The first Phase 1
notebook shell is now wired into the production application. The repository
builds two artifacts from one portable core:

- a host binary and test suite using a C11 core and C++17 formula bridge;
- `dist/phy-nspire.tns`, a native ARM program that brings up the CX II
  framebuffer, samples the keypad and touchpad, and exits cleanly.

The preserved Phase 0 diagnostic still renders one baseline frame for
framebuffer fixtures, but the production entry point now opens a native
notebook: editable Markdown and symbolic input cells, `+MD`/`+Math` insertion,
independent upper-right `RUN` badges, relative touchpad navigation with
persistent cursor position, atomic Save/Open under
`Documents/phy-nspire/notebooks/`, and direct two-dimensional IR layout for
exact fractions and powers. Markdown bodies now typeset inline `$...$` /
`\(...\)` and display `$$...$$` / `\[...\]` mathematics through the pinned
nMarkdown OpenType MATH engine.

The native foundations now in place behind that shell include the typed
expression IR; a symbolic scalar computer algebra layer over it with
exact rational arithmetic, a normal form, expansion, substitution,
differentiation, bounded exact antiderivatives, arbitrary-precision exact
integer/rational and Gaussian-rational promotion, exact `I`,
`Re`/`Im`/`Conjugate`/`Abs`, bounded univariate factorization and partial
fractions, bounded sparse multivariate GCD cancellation, certified
real-algebraic resultant arithmetic with canonical minimal-polynomial identity,
canonical complex-algebraic closure and exact arithmetic on directly typed or
reopened `Root[...]` expressions,
exact Taylor/Laurent `Series`/`Normal`,
proof-producing finite, directed and rational-infinity `Limit`, exact bounded
polynomial `Solve` with complex quadratic roots and certified higher-degree
real roots, verified exact linear and zero-dimensional polynomial systems,
bounded `Resultant`/`Discriminant`/`GroebnerBasis`, exact
Bernoulli/Harmonic/Digamma/Gamma recurrences, and certified real/complex
rational-ball layers with elementary, inverse, hyperbolic, complex error-,
Gamma-, LogGamma- and Digamma-function support for `N`; `NSolve` has certified
real isolation plus general bounded complex root rectangles through degree 48,
and an exact zero decision;
the component tensor core with charts, dense storage, valence, signed slot
symmetries, contraction, metric inversion, index raising/lowering, and
component derivatives. A new abstract-index layer adds runtime-rank typed
index spaces and tensor heads, exact free/dummy census, signed BSGS
slot-orbit canonicalization with deterministic dummy normalization,
metric-aware zero detection, identical-factor exchange, and normalized Young
projectors with exact generated-term collection. Declared Young modules now
add exact expression-wide projection, explicit Garnir relations, bounded
multi-term reduction, hook-length/content invariants, and automatic first-
Bianchi reduction without pretending a cyclic identity is a signed slot
permutation. The runtime component layer
binds coordinate or internal bases to bounded sparse component tables; the
explicit abstract-to-component bridge now evaluates collected expressions at
selected free coordinates and contracts only their dummy indices with exact
CAS arithmetic. It never inserts a metric or allocates the dense slot product.
The notebook now owns `ComponentBasis`/`TensorComponents`, and its explicit
`ComponentValue` command accepts monomials, Young projections, exact sums,
scalar multiples and distributive products. `ComponentLift` now proves and
imports an existing dense GR/geometry tensor into that sparse realization,
with explicit head and bases; a declared Young module is checked by proving
`P_T(T)=T` component by component. GR computation itself remains on the
proven legacy pipeline, while `GRComponents` publishes proved abstract heads
and sparse realizations; covariant/contravariant Riemann and Weyl heads carry
the verified `(2,2)` Young declaration. Independent-component iteration
remains pending. `QFTSystem` now gives the Lorentz, spinor, adjoint-colour and
fundamental-colour objects the same abstract identity. Its typed heads cover
the Minkowski metric, momentum, Dirac gamma, SU(N) invariants/generators, gauge
potential and field strength; exact Minkowski components are always present,
while concrete SU(2)/SU(3) views also bind the built-in delta and structure-
constant tables. Symbolic SU(N) retains `Dimension[ColorAdjoint]=N^2-1`
without inventing a numerical colour basis. Validated
coordinate maps add
exact Jacobians, proved two-way transitions, arbitrary-degree exterior-form
pullbacks through exact minors, vector pushforwards along maps, a bounded atlas
registry with exact triangle-cocycle checks, and general mixed-valence tensor
change of coordinates across verified transitions.
`IndexSpace`, `TensorHead`, direct indexed head application and
`TensorCanonicalize` expose the coordinate-free monoterm layer in notebook
cells, while `YoungProject` exposes normalized multi-term row/column
projection and `YoungDeclare`/`YoungReduce` expose the corresponding relation
module. Exact runtime vectors/matrices now include algebraic eigenspaces,
generalized eigenspaces, multiplicity-aligned eigenvectors and verified Jordan
decompositions. Sparse component construction,
verified coordinate maps/transitions, Jacobian actions, mixed-valence tensor
pullback and cocycle-checked atlases are now reader-facing evaluator objects;
the existing GR pipeline still computes on the legacy dense backend and now
has a checked, reader-facing migration adapter into the shared bridge.
The differential-geometry layer with oriented
manifolds, canonical antisymmetric forms, exact wedge, exterior derivative,
interior product, Lie derivative, and both orthonormal/general-metric Hodge duals. The native
GR layer computes Christoffel, Riemann, Ricci, scalar-curvature and Einstein
tensors, the Kretschmann and Weyl invariants, affine geodesic acceleration,
and tensor covariant derivatives from a coordinate metric. These operations are exposed as notebook commands and
checked against a committed curvature corpus.
Finite exact Lie algebras/groups, a bounded scalar `phi^4` model, and a
classical Yang--Mills layer add structure constants/Jacobi/Killing operations,
an exact field equation, tree and one-loop graph combinatorics, gauge
curvature, covariant derivatives, gauge variations, Bianchi residuals, and
exact `F wedge star_g(F)` densities. A four-dimensional Lorentz/Dirac layer
adds typed momenta and index spaces, Clifford normalisation and contraction,
traces without gamma-five, routed Mandelstam reduction, and exact symbolic
SU(N) colour tensors, traces, commutators, and Casimirs with symbolic `N`.
Those domain algorithms remain specialized exact reducers, but their indices
and invariant tensors now cross an explicit `QFTSystem` adapter into the
shared abstract/component canonicalizer.
The scalar sector also exposes convention-pinned one-loop MS/MSbar
renormalization constants and the local phi4 counterterm density for
`D = 4 - 2 epsilon`. A bounded connected phi4 multigraph command proves
quartic degree/connectedness and returns exact Wick multiplicities, vertex
automorphisms, `S`, `1/S`, loop order and `lambda^V/S` for a supplied topology.
The notebook can construct a general dense component tensor at ranks 0 through
4 with any per-slot `Up`/`Down` pattern. Generic tensor indices do not carry a
Lorentz label; explicit Lorentz/colour/spinor spaces are retained only where
the QFT type checker must prevent invalid contractions.
The CAS answers "unknown" rather than guessing outside its decidable class, so
the scalar operations needed by the tensor, geometry, and GR phases no longer depend on
integrating Giac.

Those foundations are now **reachable from the notebook**. A stateful evaluator,
[docs/EVALUATOR.md](docs/EVALUATOR.md), gives the notebook an environment of
named typed values — manifolds, forms, metrics, Lie groups and algebras,
algebra-valued forms, curvature bundles — and dispatches each reserved physics
head onto the corresponding native backend. `ExteriorD[alpha]` calls the
exterior derivative, `LieDerivative[alpha,v]` evaluates Cartan's formula, and
`FieldStrength[A,g]` calls the Yang--Mills curvature;
`ZeroQ[Bianchi[A,g]]` proves the identity rather than asserting it;
`DiracTrace[...]`, `MandelstamReduce[...]`, `Phi4Lagrangian[...]`,
`Phi4EOM[...]`, `Phi4Diagrams[...]`, `Phi4Graph[...]`, and the `SUN*` colour commands reach their
native QFT backends. Before this layer those heads were parsed into typed IR and handed to the scalar CAS, which
by contract preserves an operator and simplifies only its operands: the head
survived a round trip and nothing computed.

The generated
[`examples/phy-nspire-cas-tour.tns`](examples/phy-nspire-cas-tour.tns)
notebook combines seventeen Markdown/LaTeX explanations with 187 executable examples
that touch every implemented evaluator family. Its distributable form contains 204
source cells so opening does not eagerly rebuild all cached results; a separate
fully evaluated copy is serialized, reopened, and replayed during generation.

The last recorded strict Windows host suite passes 45/45. The current WSL GCC
and combined ASan/UBSan/leak suites pass 48/48, and the assertion-bearing
executables contain 478,792 explicit checks. The context-sensitive CAS menu is
also checked against the authoritative evaluator and source-command registries:
every supported operation is discoverable through its ten scrollable
categories.

The current native build is measured at 1,291,027 bytes, 20.5% of the 6 MiB
ceiling. Its evaluator ARM probe links the complete current physics stack,
retains 17/17 public evaluator entry points, packages to a `.tns`, and imports
no libm, floating-point formatter, or ARM soft-float helper.
The QFT abstract/component probe independently retains 14/14 public entry
points and packages with its dependencies to 101,588 bytes under the same
no-float rule.
The separate dynamic-component bridge probe retains 35/35 public entry points,
packages to a 111,964-byte `.tns`, and also imports no floating-point
formatter or parser.

The native CAS smoke artifact has run on the target CX II and shown all seven
exact symbolic checks passing. Returning from it restored Documents normally.
The earlier notebook shell also passed its input and touchpad acceptance. The
previous 1,121,131-byte evaluator build and the earlier 13,588-byte cached CAS
tour notebook were transferred through the repository-owned CLI on 2026-07-27 and
verified byte-for-byte by calculator readback. The evaluator build still
requires an explicit calculator acceptance run after transfer; an ARM link and
byte-identical upload do not establish on-device runtime or performance. That
cached tour exposed a CX II load-time failure consistent with eager IR/heap
pressure and has since been replaced by the 12,200-byte source-only tour; that
tour and the then-current 1,221,725-byte program were atomically deployed and
downloaded back byte-identically on 2026-07-30. The previous 1,222,416-byte
program, including the complete scrollable MENU, was subsequently deployed and
read back byte-identically on the same date. A later 1,226,713-byte build added
structured physics-object output and lazy/one-pass SU(3) initialization. The
current 1,291,027-byte Root/Jordan build has not been uploaded in this work
round; its calculator transfer and open/run check remain a separate gate. The
separate baseline channel-order check remains tracked in
[docs/BUILD.md](docs/BUILD.md).

Start here:

- [Building](docs/BUILD.md)
- [Scientific calculation scope](docs/SCIENTIFIC_SCOPE.md)
- [Native architecture](docs/ARCHITECTURE.md)
- [Typed expression IR](docs/IR.md)
- [Component tensor core](docs/TENSOR.md)
- [Native coordinate-metric GR](docs/GR.md)
- [Finite Lie algebra and group metadata](docs/LIE.md)
- [Bounded scalar phi4 QFT layer](docs/QFT_SCALAR.md)
- [Exact symbolic SU(N) colour algebra](docs/COLOR.md)
- [Yang--Mills symbolic layer](docs/YANG_MILLS.md)
- [Scalar computer algebra](docs/CAS.md)
- [Certified real algebraic foundation](docs/ALGEBRAIC.md)
- [Certified real ball arithmetic and numeric solving](docs/BALL_ARITHMETIC.md)
- [Notebook shell and 2D layout](docs/NOTEBOOK.md)
- [Reader-facing symbolic source language](docs/SOURCE_LANGUAGE.md)
- [Stateful notebook evaluator](docs/EVALUATOR.md)
- [CAS acceptance boundary and executable tour](docs/CAS_ACCEPTANCE.md)
- [Manifolds and differential forms](docs/GEOMETRY.md)
- [Roadmap](docs/ROADMAP.md)
- [ADR-0001: native Ndless architecture](docs/adr/0001-native-ndless-architecture.md)
- [Initial feasibility evidence](research/feasibility-2026-07-26.md)
- [QFT Q-7 CX II measurement record](research/qft-q7-cx2-measurement.md)
- [QFT and gauge theory: MVP source reference](docs/references/QFT_GAUGE.md)
- [Differential geometry, Lie theory, and scalar/gauge QFT reference pack](docs/references/DIFF_GEOM_LIE_QFT.md)
- [Agent task pack: Dirac algebra and SU(N)](docs/agent-tasks/QFT_DIRAC.md)

## Layout

```
include/phy/      public headers: platform boundary, drawing, app shell
src/core/         portable, backend-neutral core
src/ir/           typed expression IR: interning, ordering, serialization
src/exact/        bounded bigint/bigrat arithmetic and algebraic certificates
src/cas/          scalar algebra: normal form, calculus, the zero decision
src/tensor/       component tensors: charts, storage, slot symmetries
src/abstract/     abstract indices, signed canonicalization, Young projectors
src/component/    dynamic bases, sparse components, coordinate maps/pullbacks
src/gr/           coordinate-metric GR curvature pipeline
src/geom/         manifolds and differential forms: wedge, d, iota, Hodge
src/lie/          exact finite Lie algebras and built-in group metadata
src/qft/          phi4, Lorentz/Dirac/Mandelstam, and Yang--Mills operations
src/gfx/          RGB565 primitives and the built-in debug font
src/render/       typed-IR layout and the narrow nMarkdown C++ bridge
src/input/        relative pointer tracking
src/notebook/     source parser, bounded cells, CAS dispatch, native renderer
src/app/          native notebook event loop and the two entry points
src/platform/     one subdirectory per backend: ndless (device), host (tests)
src/tools/        developer utilities
tests/            host test suite and framebuffer fixtures
tests/oracle/     host-only numeric oracle certifying the QFT golden cases
tools/            SDK bootstrap, size and symbol reports
third_party/      pinned nMarkdown submodule and retained license notices
docs/references/  source-backed capability references
docs/agent-tasks/ executable contracts derived from those references
```

## Licensing

Phy-nspire is licensed under
[GNU GPL version 3](LICENSE). The pinned nMarkdown mathematical typesetter is
GPL-3.0 and is linked into the product. Its transitive FreeType, HarfBuzz,
KaTeX-derived data, fonts, and Unicode notices are retained in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and in the submodule.
