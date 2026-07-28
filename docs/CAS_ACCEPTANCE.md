# CAS acceptance boundary

This is the reader-facing acceptance record for the native calculator CAS. It
separates operations that have successful executable examples from names that
are merely recognized, and it separates host evidence from physical-device
evidence.

## Executable notebook

[`examples/phy-nspire-cas-tour.tns`](../examples/phy-nspire-cas-tour.tns) is a
6,033-byte `PHYNB001` notebook with 110 source cards:

- nine Markdown cells with nMarkdown LaTeX;
- 101 editable Math inputs;
- no eagerly persisted output/IR cache.

The generator evaluates a validation copy of the complete document, serializes
it, opens it in a new notebook with an empty environment, and runs every cell
again. It separately serializes and reopens the source-only artifact before
writing it. Generation fails on any parse, evaluation, serialization, reopen,
or replay error. The source-only form avoids rebuilding 202 cached IR trees
during `FILE > Open`; running all inputs produces the same 101 typed outputs and
a 211-card session. The inputs touch every currently implemented evaluator head
at least once:

| Area | Successful reader-facing heads |
| --- | --- |
| scalar | bare exact expressions, protected constants and special values, assignment, `Simplify`, `FullSimplify`, `Expand`, `Together`, `Cancel`, `Numerator`, `Denominator`, `D`, `Integrate`, inverse/hyperbolic/Gamma/error functions |
| tensor/manifold | `Manifold`, `ComponentTensor`, `Metric`, `VectorField`, `Component`, `Rank`, `Dimension` |
| exterior geometry | `DifferentialForm`, `Wedge`, `ExteriorD`, `InteriorProduct`, `LieDerivative`, `HodgeStar`, `Volume`, `Degree` |
| Lie/Yang--Mills | `LieGroup`, `LieAlgebra`, `Generator`, `LieElement`, `LieBracket`, `StructureConstant`, `Killing`, `LieForm`, `GaugeConnection`, `CovariantD`, `FieldStrength`, `GaugeVariation`, `Bianchi`, `YangMillsLagrangian`, `ColorComponent` |
| GR | `Curvature`, `InverseMetric`, `Christoffel`, `RiemannMixed`, `Riemann`, `Ricci`, `RicciScalar`, `Einstein`, `Kretschmann`, `Weyl`, `WeylSquared`, `GeodesicAcceleration`, `CovariantDerivative` |
| scalar/Dirac QFT | `Phi4Lagrangian`, `Phi4EOM`, `Phi4Diagrams`, `Phi4Graph`, `Phi4Renormalization`, `Phi4Counterterm`, `DiracTrace`, `MandelstamReduce` |
| SU(N) colour | `SUNDelta`, `SUNF`, `SUND`, `SUNT`, `SUNTrace`, `SUNCommutator`, `SUNDeltaContract`, `SUNCF`, `SUNCA`, `SUNFComponent`, `SUNExpandCasimirs`, `SUNFundamentalCasimir`, `SUNAdjointCasimir` |
| decisions/resources | `ZeroQ`, `EquivalentQ`, `MemoryStatus` |

`Clear` and `ClearAll` are covered by the evaluator tests rather than placed at
the end of the tour, because clearing the environment would make later
single-cell reruns inconvenient.

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

- Windows strict build and CTest: 30/30.
- WSL strict build and CTest: 32/32.
- WSL ASan, UBSan, and leak detection: 32/32.
- Assertion-bearing tests: 95,416 checks.
- Ndless r2022 ARM product: 1,121,131 bytes, 17.8% of the 6 MiB ceiling.
- Isolated evaluator ARM probe: 15/15 public APIs, 28,937 bytes of evaluator
  text, 151,396-byte package, and no float formatter, libm call, or ARM
  soft-float helper.

These results establish source, host, sanitizer, and ARM-link acceptance. They
do not establish calculator interaction, timing, or heap headroom until the
exact artifacts are opened and exercised on the physical CX II.

## Explicit non-features

`Factor`, `Apart`, `Limit`, `Series`, `Solve`, `NSolve`, `Reduce`,
`Refine`, and the `Trig*` family are registered but return
`PHY_ERR_UNSUPPORTED`. There is no multi-chart transition map or pullback, no
global-topology or named-manifold catalogue, no unbounded tensor rank, no
abstract dummy-index canonicalizer, no gamma-five, and no general loop-integral
reduction engine. Calling those absences implemented would turn a typed failure
into a false scientific claim.

## Memory lifetime

Evaluator objects are swept after every successful and failed command.
Bindings keep their dependency graph alive; `Clear`/`ClearAll` release it. CAS
scratch is LIFO and the memo cache is bounded and rebuildable. Interned IR nodes
are immutable and notebook-lifetime rather than individually collected; their
131,072-node/4-MiB ceilings produce typed errors. New/Open destroys the complete
old context. `MemoryStatus[]` exposes the current IR, CAS, object, and binding
counts for on-device observation.
