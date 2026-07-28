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
integer/rational promotion, bounded univariate factorization and partial
fractions, and an exact zero decision;
the component tensor core with charts, dense storage, valence, signed slot
symmetries, contraction, metric inversion, index raising/lowering, and
component derivatives; and the differential-geometry layer with oriented
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
notebook combines ten Markdown/LaTeX explanations with 106 executable examples
that touch every implemented evaluator head. Its distributable form contains 116
source cells so opening does not eagerly rebuild all cached results; a separate
fully evaluated copy is serialized, reopened, and replayed during generation.

The strict Windows host suite passes 34/34. The WSL ASan/UBSan/leak suite
passes 36/36, and the assertion-bearing executables contain 212,985 explicit
checks.

The current native build is measured at 1,153,412 bytes, 18.3% of the 6 MiB
ceiling. Its evaluator ARM probe links the complete current physics stack,
retains 15/15 public evaluator entry points, packages to a `.tns`, and imports
no libm, floating-point formatter, or ARM soft-float helper.

The native CAS smoke artifact has run on the target CX II and shown all seven
exact symbolic checks passing. Returning from it restored Documents normally.
The earlier notebook shell also passed its input and touchpad acceptance. The
previous 1,121,131-byte evaluator build and the earlier 13,588-byte cached CAS
tour notebook were transferred through the repository-owned CLI on 2026-07-27 and
verified byte-for-byte by calculator readback. The evaluator build still
requires an explicit calculator acceptance run after transfer; an ARM link and
byte-identical upload do not establish on-device runtime or performance. That
cached tour exposed a CX II load-time failure consistent with eager IR/heap
pressure and has since been replaced by the 6,342-byte source-only tour; the
replacement and the current 1,153,412-byte program have not been uploaded in
this build and still require explicit calculator open/run checks. The
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
