# CAS acceptance boundary

This is the reader-facing acceptance record for the native calculator CAS. It
separates operations that have successful executable examples from names that
are merely recognized, and it separates host evidence from physical-device
evidence.

## Executable notebook

[`examples/phy-nspire-cas-tour.tns`](../examples/phy-nspire-cas-tour.tns) is a
10,008-byte `PHYNB001` notebook with 164 source cards:

- sixteen Markdown cells with nMarkdown LaTeX;
- 148 editable Math inputs;
- no eagerly persisted output/IR cache.

The generator evaluates a validation copy of the complete document, serializes
it, opens it in a new notebook with an empty environment, and runs every cell
again. It separately serializes and reopens the source-only artifact before
writing it. Generation fails on any parse, evaluation, serialization, reopen,
or replay error. The source-only form avoids rebuilding 296 cached IR trees
during `FILE > Open`; running all inputs produces 148 typed outputs and
a 312-card session. The inputs touch every currently implemented evaluator
family at least once:

| Area | Successful reader-facing heads |
| --- | --- |
| scalar | bare exact expressions, protected constants and special values, arbitrary-precision Gaussian-rational arithmetic, `Re`, `Im`, `Conjugate`, `Abs`, assignment, `Simplify`, `FullSimplify`, `Expand`, `Together`, sparse multivariate `Cancel`, bounded exact `Factor`, `Apart`, `Series`, `Normal`, exact finite/directed/infinity `Limit`, exact polynomial and linear-system `Solve`, `Numerator`, `Denominator`, `D`, verified `Integrate`, inverse/hyperbolic/Gamma/error functions |
| exact linear algebra | dynamic `Vector` / `Matrix`, `Dot`, `Determinant`, `Inverse`, `RowReduce`, `MatrixRank`, `LinearSolve`, `Transpose`, `Dimensions` |
| abstract/component bridge | `IndexSpace`, `TensorHead`, `TensorCanonicalize`, `YoungProject`, `ComponentBasis`, `TensorComponents`, one-monomial `ComponentValue` |
| maps and atlases | `CoordinateMap`, verified `BasisTransition`, `Jacobian`, scalar/covector/vector maps, sparse mixed-valence tensor pullback, direct-edge `Atlas` operations |
| tensor/manifold | `Manifold`, `ComponentTensor`, `Metric`, `VectorField`, `Component`, `Rank`, `Dimension` |
| exterior geometry | `DifferentialForm`, `Wedge`, `ExteriorD`, `InteriorProduct`, `LieDerivative`, `HodgeStar`, `Volume`, `Degree` |
| Lie/Yang--Mills | `LieGroup`, `LieAlgebra`, `Generator`, `LieElement`, `LieBracket`, `StructureConstant`, `Killing`, `LieForm`, `GaugeConnection`, `CovariantD`, `FieldStrength`, `GaugeVariation`, `Bianchi`, `YangMillsLagrangian`, `ColorComponent` |
| GR | `Curvature`, `InverseMetric`, `Christoffel`, `RiemannMixed`, `Riemann`, `Ricci`, `RicciScalar`, `Einstein`, `Kretschmann`, `Weyl`, `WeylSquared`, `GeodesicAcceleration`, `CovariantDerivative` |
| scalar/Dirac QFT | `Phi4Lagrangian`, `Phi4EOM`, `Phi4Diagrams`, `Phi4Graph`, `Phi4Renormalization`, `Phi4Counterterm`, `DiracTrace`, `MandelstamReduce` |
| SU(N) colour | `SUNDelta`, `SUNF`, `SUND`, `SUNT`, `SUNTrace`, `SUNCommutator`, `SUNDeltaContract`, `SUNCF`, `SUNCA`, `SUNFComponent`, `SUNExpandCasimirs`, `SUNFundamentalCasimir`, `SUNAdjointCasimir` |
| decisions/resources | `ZeroQ`, `EquivalentQ`, `MemoryStatus` |

The tour uses one staged `ClearAll[]` after its abstract/component bridge
examples. This keeps the object table bounded, exercises the successful empty
output document path, and then recreates the manifold needed by the remaining
geometry and QFT cells.

## Meaning of a general tensor

`ComponentTensor[M,{variance...},components]` accepts every independent
`Up`/`Down` slot pattern at rank 0 through 4 and dimension 1 through 4. The test
suite constructs all five ranks, exhausts all 31 variance patterns, and checks
malformed shapes. The tour contains ranks 0, 1, 2, 3, and 4.

This is a bounded dense component tensor system, not an unbounded abstract-index
canonicalizer. Abstract `Tensor[head,indices...]` expressions can use generic
`Up[i]`/`Down[j]`; Lorentz, colour, and spinor space labels are required only
where the QFT type checker must reject a cross-space operation.

## Automated evidence

- Last Windows strict build and CTest: 41/41.
- Current WSL ASan, UBSan, and leak detection: 43/43.
- Assertion-bearing tests: 306,862 checks.
- Ndless r2022 ARM product: 1,207,021 bytes, 19.2% of the 6 MiB ceiling.
- Isolated exact-number ARM probe: 68/68 public APIs, 17,680 bytes of exact
  number text, 23,540-byte package, and no forbidden numeric dependency.
- Isolated real-algebraic ARM probe: 28/28 public APIs, 24,256 bytes of
  algebraic text and a 43,160-byte package.
- Isolated CAS ARM probe: 35/35 public APIs, 109,160 bytes of CAS text,
  154,996-byte package, and no float formatter, libm call, or ARM soft-float
  helper.
- Isolated evaluator ARM probe: 15/15 public APIs, 46,819 bytes of evaluator
  text, 303,368-byte package, and no float formatter, libm call, or ARM
  soft-float helper.

These results establish source, host, sanitizer, and ARM-link acceptance. They
do not establish calculator interaction, timing, or heap headroom until the
exact artifacts are opened and exercised on the physical CX II.

## Explicit non-features

`NSolve`, `Reduce`, `Refine`, and the `Trig*` family are registered but return
`PHY_ERR_UNSUPPORTED`. `Solve` covers exact affine and real/complex quadratic
roots plus higher-degree all-real `Root` descriptors as documented in
`docs/CAS.md`, plus exact simultaneous affine systems through eight
equations/variables; unresolved higher complex factors and nonlinear systems
remain typed unsupported. Reader-facing `CoordinateMap`, `BasisTransition`,
Jacobian scalar/covector/vector operations, sparse mixed-valence tensor
pullback, and direct-edge `Atlas` commands are implemented. General automatic
transition-path composition and expression-wide component conversion remain
typed unsupported. There is no
global-topology or named-manifold catalogue, no unlimited-resource tensor rank,
no general Garnir-basis reducer, no gamma-five, and no general loop-integral
reduction engine. Calling those absences implemented would turn a typed failure
into a false scientific claim.

The exact promotion order and the positive/negative cases that must change
those statuses are frozen in
[`plans/2026-07-28-cas-foundation-f4-f5.md`](plans/2026-07-28-cas-foundation-f4-f5.md)
and compiled by `tests/corpus/cas_foundation_cases.inc`.

## Memory lifetime

Evaluator objects are swept after every successful and failed command.
Bindings keep their dependency graph alive; `Clear` releases ordinary owned
objects, while abstract index spaces and heads share one bulk-owned bounded
context that is returned by `ClearAll`/environment reset. CAS
scratch is LIFO and the memo cache is bounded and rebuildable. Interned IR nodes
are immutable and notebook-lifetime rather than individually collected; their
131,072-node/4-MiB ceilings produce typed errors. New/Open destroys the complete
old context. `MemoryStatus[]` exposes the current IR, CAS, object, and binding
counts for on-device observation.
