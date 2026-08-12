# CAS acceptance boundary

This is the reader-facing acceptance record for the native calculator CAS. It
separates operations that have successful executable examples from names that
are merely recognized, and it separates host evidence from physical-device
evidence.

## Executable notebook

[`examples/phy-nspire-cas-tour.tns`](../examples/phy-nspire-cas-tour.tns) is a
12,341-byte `PHYNB001` notebook with 196 source cards:

- sixteen Markdown cells with nMarkdown LaTeX;
- 180 editable Math inputs;
- no eagerly persisted output/IR cache.

The generator evaluates a validation copy of the complete document, serializes
it, opens it in a new notebook with an empty environment, and runs every cell
again. It separately serializes and reopens the source-only artifact before
writing it. Generation fails on any parse, evaluation, serialization, reopen,
or replay error. The source-only form avoids rebuilding the cached input/output
IR trees during `FILE > Open`; running all inputs produces 180 typed outputs and
a 376-card session. The inputs touch every currently implemented evaluator
family at least once:

| Area | Successful reader-facing heads |
| --- | --- |
| scalar | bare exact expressions, protected constants and special values, arbitrary-precision Gaussian-rational arithmetic, `Re`, `Im`, `Conjugate`, `Abs`, exact `Factorial`/`Pochhammer`/`Binomial`, assignment, `Simplify`, `FullSimplify`, `Expand`, `Together`, sparse multivariate `Cancel`, bounded exact `Factor`, `Apart`, `Series`, `Normal`, exact finite/directed/infinity `Limit`, exact polynomial and linear-system `Solve`, `Numerator`, `Denominator`, `D`, verified `Integrate`, inverse/hyperbolic/Gamma/error functions |
| exact linear algebra | dynamic `Vector` / `Matrix`, `Dot`, `Determinant`, `Inverse`, `RowReduce`, `MatrixRank`, `LinearSolve`, `Transpose`, `Dimensions` |
| abstract/component bridge | `IndexSpace`, formal exact dimensions, `TensorHead`, `TensorCanonicalize`, `YoungProject`, `YoungDeclare`, `YoungReduce`, `YoungDimension`, exact abstract expression algebra, `ComponentBasis`, `TensorComponents`, checked legacy `ComponentLift`, expression-wide `ComponentValue` |
| maps and atlases | `CoordinateMap`, verified `BasisTransition`, `Jacobian`, scalar/covector/vector maps, sparse mixed-valence tensor pullback, direct-edge `Atlas` operations |
| tensor/manifold | `Manifold`, `ComponentTensor`, `Metric`, `VectorField`, `Component`, `Rank`, `Dimension` |
| exterior geometry | `DifferentialForm`, `Wedge`, `ExteriorD`, `InteriorProduct`, `LieDerivative`, `HodgeStar`, `Volume`, `Degree` |
| Lie/Yang--Mills | `LieGroup`, `LieAlgebra`, `Generator`, `LieElement`, `LieBracket`, `StructureConstant`, `Killing`, `LieForm`, `GaugeConnection`, `CovariantD`, `FieldStrength`, `GaugeVariation`, `Bianchi`, `YangMillsLagrangian`, `ColorComponent` |
| GR | `Curvature`, `InverseMetric`, `Christoffel`, `RiemannMixed`, `Riemann`, `Ricci`, `RicciScalar`, `Einstein`, `Kretschmann`, `Weyl`, `WeylSquared`, `GeodesicAcceleration`, `CovariantDerivative`, `GRComponents`/`GRHead`/`GRTensor`, and automatic first-Bianchi reduction |
| scalar/Dirac QFT | `Phi4Lagrangian`, `Phi4EOM`, `Phi4Diagrams`, `Phi4Graph`, `Phi4Renormalization`, `Phi4Counterterm`, `DiracTrace`, `MandelstamReduce`, `QFTSystem`/`QFTSpace`/`QFTHead`/`QFTTensor` |
| SU(N) colour | `SUNDelta`, `SUNF`, `SUND`, `SUNT`, `SUNTrace`, `SUNCommutator`, `SUNDeltaContract`, `SUNCF`, `SUNCA`, `SUNFComponent`, `SUNExpandCasimirs`, `SUNFundamentalCasimir`, `SUNAdjointCasimir` |
| decisions/resources | `ZeroQ`, `EquivalentQ`, `MemoryStatus` |

The tour uses staged `ClearAll[]` boundaries after its map/atlas examples and
after its first QFT session. This keeps the object table bounded, exercises
the successful empty-output document path, and recreates only the state needed
by the following geometry or stress cells.

## Meaning of a general tensor

`ComponentTensor[M,{variance...},components]` accepts every independent
`Up`/`Down` slot pattern at rank 0 through 4 and dimension 1 through 4. The test
suite constructs all five ranks, exhausts all 31 variance patterns, and checks
malformed shapes. The tour contains ranks 0, 1, 2, 3, and 4.

The legacy `ComponentTensor` constructor is the bounded rank-four dense path.
The shared component library instead uses runtime rank (default ceiling 32)
with dense/sparse storage, while abstract `Tensor[head,indices...]` allocates
no `dimension^rank` array and has a separate default 64-slot ceiling. Abstract
expressions receive signed-slot and dummy-index canonicalization plus bounded
Young/Garnir reduction. These explicit resource bounds are not a claim of
unbounded xPerm/xTensor equivalence. Lorentz, colour, and spinor space labels
are required only where the QFT type checker must reject a cross-space
operation.

## Automated evidence

- Last recorded Windows strict build and CTest: 45/45.
- Current WSL GCC Release and combined ASan/UBSan/leak suites: 48/48 each.
  Focused ball/CAS/evaluator tests contain 39,364, 33,812, and 3,187 checks
  respectively; the precision tranche also visually checked ordinary complex,
  `10^-50`, and `10^50` scientific-notation frames at the native 320x240
  resolution.
- Wolfram development oracle for complex branches, special functions,
  requested precision, and clustered polynomial roots: 21/21; this is
  independent cross-check evidence, not a
  native runtime dependency.
- Assertion-bearing executables: 475,909 checks.
- Notebook MENU completeness: every supported evaluator/source command is
  present in ten scrollable CAS categories.
- Ndless r2022 ARM product: 1,278,195 bytes, 20.3% of the 6 MiB ceiling.
- Rebuilt discrete-function CAS smoke and QFT bench packages link natively at
  88,588 and 61,868 bytes respectively; this is ARM package evidence, not a
  new physical-device run.
- Isolated exact-number ARM probe: 68/68 public APIs, 17,680 bytes of exact
  number text, 23,540-byte package, and no forbidden numeric dependency.
- Isolated real/complex-ball ARM probe: 67/67 public APIs, 61,878 bytes of
  ball text, 64,924-byte package, and no forbidden numeric dependency.
- Isolated real-algebraic ARM probe: 31/31 public APIs, 38,720 bytes of
  algebraic text and a 66,572-byte package.
- Isolated CAS ARM probe: 40/40 public APIs, 145,894 bytes of CAS text,
  255,908-byte package, and no float formatter, libm call, or ARM soft-float
  helper.
- Isolated evaluator ARM probe: 17/17 public APIs, 54,355 bytes of evaluator
  text, 434,236-byte package, and no float formatter, libm call, or ARM
  soft-float helper.
- Isolated QFT abstract/component bridge probe: 14/14 public APIs, 3,152
  bytes of bridge text, 101,588-byte package, and the same no-float guarantee.

These results establish source, host, sanitizer, and ARM-link acceptance. They
do not establish calculator interaction, timing, or heap headroom until the
exact artifacts are opened and exercised on the physical CX II.

On 2026-08-10 the repository CLI atomically deployed and read back the exact
1,271,953-byte program (SHA-256
`552c20dab3cbb941f8a9d7025e2cc1550d288e6742ed50938313a8812a51124d`)
and the 12,940-byte tour (SHA-256
`db27dd8be16e7e8ae24092dd93abff8c28bc38a57d40859739c31c008801ad7d`).
Both rollback copies were removed; final listings found no `.upload` or
`.previous` files and confirmed `examples/` empty. Under this usbipd session a
fresh detach/attach was required before each new CLI process, so the two files
were deployed in separate verified sessions with 1000 ms service settling.
This proves transport integrity only; calculator open/run acceptance remains
pending for the current unuploaded artifact. On 2026-08-12 the user completed
a full run of the previously deployed tour and reported no observed problem;
that is physical acceptance of the preceding deployed build, not of later
host-only changes.

## Explicit non-features

`Reduce`, `Refine`, and the `Trig*` family are registered but return
`PHY_ERR_UNSUPPORTED`. `Solve` covers exact affine and real/complex quadratic
roots plus higher-degree all-real `Root` descriptors as documented in
`docs/CAS.md`, exact simultaneous affine systems through eight
equations/variables, and bounded verified triangular zero-dimensional
polynomial systems. `Resultant`, `Discriminant` and `GroebnerBasis` share the
exact sparse polynomial domain. `N` provides certified real and rectangular
complex balls for its documented bounded subset, including complex error and
Gamma-family values; `NSolve` combines real Sturm isolation with exact
Pellet--Rouche certification of every distinct complex root through degree 48.
General multivariate nonlinear numerical systems remain typed unsupported. Reader-facing
`CoordinateMap`, `BasisTransition`,
Jacobian scalar/covector/vector operations, sparse mixed-valence tensor
pullback, and direct-edge `Atlas` commands are implemented. General automatic
transition-path composition and independent-component output iteration remain
typed unsupported. There is no
global-topology or named-manifold catalogue, no unlimited-resource tensor rank,
no unbounded or optimized xPerm port, no gamma-five, and no general
loop-integral reduction engine. Calling those absences implemented would turn
a typed failure into a false scientific claim.

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
