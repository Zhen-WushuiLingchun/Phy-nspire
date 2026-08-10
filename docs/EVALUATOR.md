# The stateful notebook evaluator

`include/phy/eval.h`, `src/eval`.

## What changed, and why it mattered

Before this layer the notebook parsed `ExteriorD[omega]`, `FieldStrength[A,g]`
and a dozen more physics spellings into `PHY_IR_OPERATOR` nodes and handed them
to the scalar CAS. The CAS does exactly what
[`CAS.md`](CAS.md) says it does with an operator: it simplifies the operands and
leaves the node alone. So the head survived the round trip, the cell rendered
something plausible, and nothing had been computed.

That is the worst failure mode a symbolic system has, because a reader cannot
tell it apart from a working evaluator without already knowing the answer. The
differential-geometry, Lie-algebra and Yang--Mills backends were fully
implemented and fully tested, and completely unreachable from the product.

This layer replaces head preservation with dispatch. Two rules make that
verifiable rather than aspirational:

- every reserved head the evaluator knows is either evaluated or a typed error;
- nothing is rebuilt as an inert operator with the same head.

`tests/test_eval.c` pins the second rule directly: called with no arguments,
every evaluated head must return `PHY_ERR_PARSE`. A head that fell out of either
the parser's table or the evaluator's would instead become an ordinary function
application and return a value, and the test would fail.

## Why it has to be stateful

The objects those backends operate on cannot be written as one expression. A
manifold is a dimension, an orientation, a signature and a chart of named
coordinates; an `SU(2)` connection on a 3-manifold is 3 algebra components each
holding 3 form components. Nothing in the typed IR represents them and nothing
should — the IR is a graph of values, and these are objects with identity that
several cells share.

So the notebook owns an environment, and a cell is evaluated *against* it:

```text
M = Manifold[{x, y, z}, Euclidean]
su = LieAlgebra[LieGroup[SU2]]
A = GaugeConnection[su, M, {{1,0,0},{0,1,0},{0,0,1}}]
F = FieldStrength[A, g]
ZeroQ[Bianchi[A, g]]                       (* True *)
```

Each line is one cell. The last cannot be written at all without the first
four, which is what "stateful" buys.

## Values

| Kind | Produced by | Displays as |
| --- | --- | --- |
| `Scalar` | arithmetic, components, decisions | its own typed IR |
| `Manifold` | `Manifold[...]` | structured constructor |
| `Tensor` | `ComponentTensor`, `Metric`, `VectorField`, curvature parts | components: `List` of rows at rank <= 2; at rank 3 and 4 the `List` of nonvanishing components as `Gamma(theta,phi,phi) = ...` equations, up to 64 of them, named by the chart's coordinates |
| `Form` | `DifferentialForm`, `Wedge`, `ExteriorD`, `HodgeStar`, `Volume`, `InteriorProduct`, `YangMillsLagrangian`, `ColorComponent` | coordinate-coframe expansion |
| `LieGroup` | `LieGroup[...]` | structured constructor |
| `LieAlgebra` | `LieAlgebra[G]` | structured constructor |
| `LieElement` | `Generator`, `LieElement`, `LieBracket` | `sum_a c_a T_a` |
| `LieForm` | `LieForm`, `GaugeConnection`, `FieldStrength`, `CovariantD`, `GaugeVariation`, `Bianchi` | `sum_a T_a . (coframe expansion)` |
| `Curvature` | `Curvature[g]` | structured constructor |
| `IndexSpace` | `IndexSpace[...]` | structured constructor |
| `TensorHead` | `TensorHead[...]` | typed abstract-index signature |
| `AbstractTensor` | indexed tensor-head products | typed-IR indexed tensor expression |
| `AbstractExpression` | `YoungProject[...]`, `GarnirRelation[...]`, `YoungReduce[...]` | collected typed-IR indexed tensor sum |
| `ComponentBasis` | `ComponentBasis[...]` | structured constructor with coordinates/basis |
| `TensorComponents` | sparse realization of a `TensorHead` | typed component-index signature |
| `Vector` | `Vector[{...}]`, matrix-vector operations | exact `List` |
| `Matrix` | `Matrix[{{...},...}]`, exact linear operations | exact nested `List` |
| `CoordinateMap` | `CoordinateMap[...]` | structured constructor |
| `BasisTransition` | `BasisTransition[...]` | structured constructor |
| `Atlas` | `Atlas[...]` | structured constructor |

A form's expansion is real mathematics rather than a label: the coframe symbol
of a coordinate is its name with a `d` in front, so a chart on `(r, theta)`
spans its 1-forms with `dr` and `dtheta`, and `ExteriorD[y dx]` renders as
`-dx ^ dy`. Those are ordinary interned symbols, so a document that also uses
`dr` as a scalar will see them collide; the alternative was a coframe node kind
that the scalar CAS would then have to be taught to ignore.

An algebra-valued form uses `PHY_IR_NCMUL` between the generator and its
component, because generators do not commute and an ordinary product would
invite the CAS to collect terms it has no right to collect. A Lie element uses
ordinary multiplication, because its coefficients are scalars and that product
really is commutative.

## Surface

Assignment is `name = value`, distinguished from the equation `name == value` by
one character of lookahead. `Set[name, value]` is the FullForm spelling.
`Clear[name]` unbinds one name; `ClearAll[]` clears the environment.

### Exact discrete functions

| Spelling | Exact behavior |
| --- | --- |
| `Factorial[n]` | arbitrary-precision integer result for `0 <= n <= 512` |
| `Pochhammer[a,n]`, `RisingFactorial[a,n]` | exact rational or bounded symbolic finite product for integer `n` |
| `Binomial[a,n]` | exact generalized binomial coefficient or bounded symbolic finite product for integer `n` |
| `BernoulliB[n]`, `Bernoulli[n]` | exact Bernoulli number for integer `0 <= n <= 64`, with `B_1=-1/2` |
| `HarmonicNumber[n]`, `Harmonic[n]` | exact harmonic number for integer `0 <= n <= 4096` |
| `Digamma[x]` | exact positive integer and half-integer values plus bounded symbolic integer-shift recurrence |
| `Gamma[x]` | exact positive integers, positive half-integers and bounded symbolic integer shifts |

Exact products admit at most 512 factors and symbolic expansions at most 64.
Outside those ceilings the evaluator returns `PHY_ERR_TERM_LIMIT`; proved
poles return `PHY_ERR_DOMAIN`, and unknown noninteger orders remain explicit.
Bernoulli, harmonic and recurrence-specific ceilings are independent so a
compact input cannot force an unbounded exact expansion.

### Abstract tensors

| Spelling | Backend |
| --- | --- |
| `IndexSpace[dimension, metric?]` | `phy_index_space_create` in the notebook's lazily owned abstract context |
| `TensorHead[{spaces...}, property?, {generators...}?]` | `phy_tensor_head_create` plus signed generators |
| `A[Down[i],Up[j],...]` | typed `phy_tensor_monomial` factor using the spaces declared by `A` |
| products of indexed heads and scalar coefficients | exact monomial coefficient/factor merge and Einstein census |
| `TensorCanonicalize[monomial]` | bounded signed BSGS double-coset canonicalizer |
| `YoungProject[expression, factor?, {{slots...},...}, order?]` | normalized Young row/column projector and exact term collection |
| `YoungDeclare[head,{{slots...},...},order?]` | attach a checked Young module without changing the signed slot group |
| `YoungReduce[expression]` | project every factor carrying a declared Young module and collect exactly |
| `GarnirRelation[monomial,factor,relation]` | construct one readable antisymmetrized Garnir relation |
| `YoungDimension[{{slots...},...},dimension]` | exact hook-content dimension of the Schur module |

`metric` is `NoMetric`, `SymmetricMetric`, or `AntisymmetricMetric`.
`dimension` may be a positive concrete integer, a symbol, or a formal exact
scalar expression assembled from symbols, integers/rationals and `+`, `*`,
`^`. The last form records relations such as `N^2-1`; it is not a proof that
the expression is positive and integral. A concrete component basis remains
responsible for supplying a positive runtime dimension.
`property` is `Commuting`, `NonCommuting`, or the rank-two shortcut
`Symmetric`/`Antisymmetric`. General signed slot laws use one-based image
notation, for example
`Symmetry[{2,1,3},-1]`. Index names are scoped by their `IndexSpace`, and a
direct application rejects an explicit `Down[i,W]` when that slot belongs to
`V`. The evaluator owns and clears the entire abstract context with the
notebook environment; returned monomials own their copied factor/index arrays,
so canonical results do not dangle when an intermediate is swept.

Factor, relation and tableau slot positions are one-based at the reader
surface; the factor argument defaults to one. `order` is `RowLast` (the
default) or `ColumnLast`. `YoungProject` returns a real multi-term abstract
expression, not an inert operator: every generated monomial passes through the
monoterm canonicalizer, equal structures are collected with exact
coefficients, and the resulting sum uses the same MathTree renderer.
`YoungDeclare` first proves every existing signed generator is manifest on the
projector image. `YoungReduce` then supplies an equality gate modulo the
projector kernel; for a `(2,2)` Riemann head the cyclic first-Bianchi sum
reduces to a zero expression while retaining its four typed free indices.
Exact factorial arithmetic bounds this surface to tableaux of at most 20
slots; generated terms, result terms, steps and temporary bytes have separate
device-oriented ceilings.

### Exact vectors and matrices

| Spelling | Backend/result |
| --- | --- |
| `Vector[{...}]` | runtime-length exact `phy_vector` |
| `Matrix[{{...},...}]` | rectangular runtime-shape exact `phy_matrix` |
| `Dot[v,w]` | exact vector dot product |
| `Dot[A,B]`, `Dot[A,v]` | exact matrix product |
| `Transpose[A]` | exact transpose |
| `Determinant[A]`, `Inverse[A]` | exact determinant and inverse |
| `RowReduce[A]`, `MatrixRank[A]` | exact RREF and algebraic rank |
| `LinearSolve[A,b]` | exact square nonsingular solve with vector or matrix right side |

Vectors and matrices have runtime shapes and share the scalar CAS for every
entry. `Rank[v]`/`Rank[A]` report structural ranks 1/2;
`MatrixRank[A]` reports algebraic rank. `Component`, `Dimensions`, `ZeroQ`,
`EquivalentQ`, homogeneous addition and exact scalar multiplication all use
the same objects. Matrix multiplication is explicit `Dot`, so ordinary
commutative scalar multiplication never silently changes meaning.

### Abstract/component bridge and coordinate changes

```text
V  = IndexSpace[2, SymmetricMetric]
A  = TensorHead[{V,V}, Antisymmetric]
xy = ComponentBasis[V,{x,y}]
Ac = TensorComponents[A,{xy,xy},{Down,Down},{{{0,1},a}}]

Component[Ac,1,0]
ComponentValue[A[Down[i],Down[j]],{Ac},{0,1}]

c  = Curvature[g]
Rc = ComponentLift[Riemann[c],R,{xy,xy,xy,xy}]
```

`TensorComponents` stores only supplied canonical entries and applies the
head's signed slot symmetries on set/get. Runtime rank is bounded by configured
resources, not the legacy rank-four API. `ComponentValue` accepts a monomial
or a collected abstract expression, an explicit list of realizations, and
free-index coordinates in the expression's typed census order. It remaps each
term by `(IndexSpace,name,variance)`, enumerates only dummy indices, and shares
term/step/memory ceilings across the full sum. Abstract addition, exact scalar
multiplication and distributive multiplication canonicalize and collect before
publication; a zero expression retains its free-index signature.

| Spelling | Native action |
| --- | --- |
| `ComponentBasis[V,{x,y}]`, `ComponentBasis[V,n]` | bind an index space to a concrete coordinate basis or unnamed basis |
| `TensorComponents[head,{bases...},{Up/Down...},{{indices,value},...}]` | construct a sparse exact realization |
| `ComponentLift[legacy,head,{bases...}]` | prove and import a legacy chart tensor, including `P_T(T)=T` when the head declares a Young module |
| `ComponentValue[expression,{realizations...},{free coordinates...}]` | explicit expression-wide abstract-to-component evaluation |
| `CoordinateMap[source,target,{target-in-source...}]` | exact coordinate map and Jacobian |
| `BasisTransition[source,target,{forward...},{inverse...}]` | two maps proved inverse in both directions |
| `Jacobian[F]` | exact dynamic matrix |
| `PullbackScalar`, `PullbackCovector`, `PushForwardVector` | exact substitution/Jacobian action |
| `TransitionPullback[tr,Tc]` | verified mixed-valence sparse tensor change of basis |
| `Atlas[{charts...}]`, `AtlasAddTransition[...]`, `AtlasVerify[...]` | bounded chart registry with exact cocycle checks |
| `AtlasPullback[atlas,source,target,Tc]` | registered-edge tensor pullback |

No command changes basis implicitly. A component realization must match the
transition's target basis in every slot, and an atlas pullback requires a
registered verified edge. The dense transformation side is capped at 4096
components; the result returns to sparse canonical storage.

`ComponentLift` is the migration boundary for the existing GR/component
backends. It requires an explicit abstract head and an explicit basis for every
slot, copies no object merely by name, and verifies every dense source entry
against the head's signed slot group before publishing. A stronger symmetry
that the source does not satisfy is `PHY_ERR_ASSUMPTION`, with no partial
realization retained. If the head has a Young declaration, every dense
component is also checked against its normalized projector; this is what keeps
multi-term identities from becoming unverified metadata.

`GRComponents[curvature,{Weyl,RiemannUpper}]` performs that lift for the full
GR result. `GRSpace`, `GRBasis`, `GRHead[view,quantity]` and
`GRTensor[view,quantity]` return its borrowed abstract/component views.
Christoffel, Riemann and Ricci are still produced by the proven dense GR
algorithm. What has migrated is the consumer side: contractions and
multi-term identities can use the shared abstract evaluator, and the Riemann,
RiemannUpper and Weyl heads carry `(2,2)` declarations only after component
import proves them.

### QFT abstract/component system

```text
qft  = QFTSystem[3]
L    = QFTSpace[qft,Lorentz]
C    = QFTSpace[qft,ColorAdjoint]
eta  = QFTHead[qft,MinkowskiMetric]
etac = QFTTensor[qft,MinkowskiMetric]
f    = QFTHead[qft,SUNF]
fc   = QFTTensor[qft,SUNF]
```

| Spelling | Result |
| --- | --- |
| `QFTSystem[]`, `QFTSystem[N]` | structured shared view showing SU(N), typed spaces, heads, and exact tables; omitted `N` is the symbol `N` |
| `QFTSpace[qft,Lorentz\|Spinor\|ColorAdjoint\|ColorFundamental]` | a typed `IndexSpace` |
| `QFTBasis[qft,space]` | its concrete basis when available |
| `QFTHead[qft,quantity]` | the shared abstract `TensorHead` |
| `QFTTensor[qft,quantity]` | an exact sparse realization when available |

The quantity selectors are `MinkowskiMetric`, `MinkowskiInverse`, `Momentum`,
`DiracGamma`, `SUNDelta`, `SUNF`, `SUND`, `SUNT`, `GaugePotential`, and
`FieldStrength`. Their slot spaces and monoterm symmetries are part of the
heads: `SUNF` is totally antisymmetric, `SUND` totally symmetric, and
`FieldStrength` antisymmetric in its two Lorentz slots. Gamma matrices and
fundamental generators are noncommuting heads.

The component boundary is intentionally narrower than the abstract one.
Minkowski `diag(1,-1,-1,-1)` and its inverse are always bound. A concrete
colour basis within the configured resource ceilings adds `SUNDelta`; the
built-in SU(2)/SU(3) `SUNF` table is materialized only by its first
`QFTTensor` request. That lazy path constructs and validates the Lie algebra
once, rather than once per independent component. `SUND`, `SUNT`, gamma
matrices and general SU(N) numerical generators are not fabricated:
`QFTTensor` returns `PHY_ERR_NOT_INITIALIZED` for them. With symbolic `N`,
`Dimension[QFTSpace[qft,ColorAdjoint]]` is exactly `N^2-1`, while colour
component bases remain absent.

This adapter does not replace the specialized Dirac trace, Mandelstam or
colour-trace reducers. It gives their index vocabulary a single typed identity
and lets the general canonicalizer/component evaluator check contractions and
slot identities. As with `GRComponents`, one view owns its bases and
realizations while the notebook's bulk abstract context owns its spaces and
heads.

### Differential geometry

| Spelling | Backend |
| --- | --- |
| `Manifold[{coords...}, signature, orientation?]` | `phy_chart_create` + `phy_manifold_create` |
| `ComponentTensor[M, {Down,Up,...}, components]` | general rank-0 through rank-4 `phy_tensor` |
| `DifferentialForm[M, degree, {components}?]` | `phy_form_create` |
| `Metric[M, {{...},...}]` | rank-2 covariant `phy_tensor` |
| `VectorField[M, {...}]` | rank-1 contravariant `phy_tensor` |
| `Wedge[a, b, ...]` | `phy_form_wedge` |
| `ExteriorD[a]` | `phy_form_exterior_derivative` / `phy_lie_form_exterior_derivative`; on a top-degree form the scalar `0`, so `d(d a)` is an answer on every manifold |
| `InteriorProduct[a, v]` | `phy_form_interior_product` |
| `LieDerivative[a, v]` | `phy_form_lie_derivative` (Cartan formula) |
| `HodgeStar[a]`, `HodgeStar[a, g]` | `phy_form_hodge` / `phy_form_hodge_metric` |
| `Volume[M]`, `Volume[M, g]` | `phy_form_volume` / `phy_form_volume_metric` |

`signature` is the keyword `Euclidean`/`Riemannian`, the keyword
`Lorentzian`/`Minkowski` — mostly-plus, per
[`references/GENERAL_RELATIVITY.md`](references/GENERAL_RELATIVITY.md) — or an
explicit list of `+1`/`-1`. `orientation` is `Positive` (the default),
`Negative`, or `Unoriented`/`None`. A legacy `Manifold` still carries one
legacy `phy_chart`. Multi-chart work uses the separate
`ComponentBasis`/`CoordinateMap`/`BasisTransition`/`Atlas` surface above,
whose transitions and cocycles are validated before use.

`ComponentTensor` has one variance marker per slot and one nested `List` level
per slot. A rank-0 tensor takes a scalar component. The native bound is
dimension 1 through 4, rank 0 through 4, hence at most 256 dense components;
this remains the bounded dense component backend. Coordinate-free runtime rank
belongs to the abstract surface above.

### Lie algebra

| Spelling | Backend |
| --- | --- |
| `LieGroup[U1\|SU2\|SO3\|SU3\|SO13]` | `phy_lie_group_builtin` |
| `LieAlgebra[G]` | `phy_lie_group_algebra`, borrowed |
| `Generator[alg, k]` | `phy_lie_basis_element` |
| `LieElement[alg, {coefficients}]` | `phy_lie_element_create` |
| `LieBracket[X, Y]` | `phy_lie_bracket`, `phy_lie_form_bracket_wedge`, or `phy_lie_commutator_ir` by operand kind |
| `StructureConstant[alg, a, b, c]` | `phy_lie_structure_constant` |
| `Killing[alg, a, b]` | `phy_lie_killing_component` |

A group outside the built-in catalogue is `PHY_ERR_UNSUPPORTED`, not a label:
returning an object that cannot bracket would be worse than failing.

### Yang--Mills

| Spelling | Backend |
| --- | --- |
| `LieForm[alg, M, degree, {{...},...}]` | `phy_lie_form_create` |
| `GaugeConnection[alg, M, {{...},...}]` | the same, at degree 1 |
| `FieldStrength[A, g]` | `phy_yang_mills_field_strength` |
| `CovariantD[A, omega, g]` | `phy_yang_mills_covariant_derivative` |
| `GaugeVariation[X, alpha, g]` | connection or curvature branch, chosen by `X`'s degree |
| `Bianchi[A, g]` | `phy_yang_mills_bianchi` |
| `YangMillsLagrangian[F, metric]`, `[F, metric, {{h...}}]` | `phy_yang_mills_lagrangian` |
| `ColorComponent[F, a]` | one colour component as an ordinary form |

`GaugeVariation` reads the degree because `delta A = D_A alpha` and
`delta F = g [F, alpha]` are different formulas, a connection is a 1-form and a
curvature a 2-form. Asking a reader to spell which one they meant would be
asking them to restate what the object already knows, and getting it wrong would
return a well-formed wrong answer.

Omitting the bilinear form of `YangMillsLagrangian` selects the algebra's
Killing form, which is identically zero for `U(1)`; Abelian callers supply their
representation trace form explicitly.

### General relativity

`Curvature[g]` runs the whole coordinate-metric pipeline; `InverseMetric`,
`Christoffel`, `Riemann`, `RiemannMixed`, `Ricci`, `RicciScalar` and `Einstein`
read parts of the result. `Kretschmann[c]` performs and caches the more
expensive four-index contraction on demand. `Weyl[c]` builds the covariant
conformal curvature, while `WeylSquared[c]` uses the exact invariant
decomposition to avoid raising four more tensor slots.
`GeodesicAcceleration[c,v]` returns
`-Gamma^mu_nu_rho v^nu v^rho`; it is the exact right-hand side of the affine
geodesic equation, not a numerical trajectory integrator.
`CovariantDerivative[T,c]`
constructs the full component tensor `nabla T`, with the lower derivative slot
prepended.

### Bounded QFT

| Spelling | Backend |
| --- | --- |
| `DiracTrace[{mu,nu,...}]` | four-dimensional `phy_dirac_trace_scalar`, with bare names interpreted as upper Lorentz indices |
| `DiracTrace[{Up[mu,Lorentz],Down[nu,Lorentz],...}]` | the same, with explicit variance |
| `MandelstamReduce[expr,{p1,p2,p3,p4},{m1,m2,m3,m4},Peskin]` | routed `2 -> 2` kinematics |
| the same with `AllIncoming` | FORM/FeynCalc all-incoming routing |
| `Phi4Lagrangian[phi,m,lambda,D]` | exact real-scalar model density |
| `Phi4EOM[phi,m,lambda,D]` | exact equation-of-motion left-hand side |
| `Phi4Diagrams[phi,m,lambda,D,s,t,u]` | tree amplitude and four bounded one-loop expressions |
| `Phi4Graph[phi,m,lambda,D,{external vertices},{{adjacency}}]` | exact connected multigraph/Wick/symmetry analysis |
| `Phi4Renormalization[phi,m,lambda,4,epsilon,MS]` | labelled one-loop `DeltaZPhi`, `DeltaZm`, `DeltaZLambda` for `D=4-2 epsilon` |
| the same with `MSBar` | explicit `1/epsilon-EulerGamma+Log[4 Pi]` replacement |
| `Phi4Counterterm[phi,m,lambda,4,epsilon,scheme]` | corresponding exact local counterterm density |

`DiracTrace` is deliberately four-dimensional and excludes gamma-five.
`MandelstamReduce` requires the routing keyword because the sign of the
third/fourth external momenta is not inferable from the symbols. The loop
entries returned by `Phi4Diagrams` are exact combinatorial coefficients times
typed `TadpoleIntegral`/`BubbleIntegral` masters; they are not numerical
integrals. `Phi4Graph` accepts a bounded supplied topology, proves the quartic
degree and connectedness conditions, and returns `S`, `1/S`, Wick
multiplicity, loop order, superficial degree and `lambda^V/S`. It does not
invent momentum routing, amplitude phases, or a loop integral. The
renormalization commands do not pretend to evaluate those
opaque masters: they expose the separately normalized one-loop UV result,
with the epsilon convention fixed in the command contract.

### SU(N) colour

| Spelling | Backend/result |
| --- | --- |
| `SUNDelta[a,b,N]` | canonical adjoint delta; equal indices give `N^2-1` |
| `SUNF[a,b,c,N]` | real, totally antisymmetric structure constant |
| `SUND[a,b,c,N]` | totally symmetric invariant tensor |
| `SUNT[a,N]` | fundamental generator object |
| `SUNTrace[{indices},N]` | exact traces through length 3; longer traces stay held |
| `SUNCommutator[a,b,N]` | `I f^(abc) T^c` with a fresh typed dummy |
| `SUNDeltaContract[a,b,expr,N]` | one unambiguous adjoint-delta contraction |
| `SUNCF[N]`, `SUNCA[N]` | raw exact Casimirs |
| `SUNFComponent[N,a,b,c]` | one-based exact built-in SU(2)/SU(3) component |
| `SUNExpandCasimirs[expr,N]` | replace presentation atoms `C_F`, `C_A` |
| `SUNFundamentalCasimir[N]` | `C_F IdentityFundamental` |
| `SUNAdjointCasimir[a,b,N]` | `C_A SUNDelta[a,b]` |

Bare index names become upper `ColorAdjoint` indices. An explicit Lorentz or
other-space index is `PHY_ERR_TYPE`, so colour and Dirac contraction cannot
interfere. `N` may remain symbolic. Exact component lookup is intentionally
bounded to the textbook SU(2)/SU(3) tables; abstract `SUNF` works for symbolic
`N`. Traces longer than three retain the explicit, re-runnable form
`SUNTrace[N,a,b,c,d,...]`. The conventions and the Fierz boundary are in
[`COLOR.md`](COLOR.md).

### Queries

`Component[obj, indices...]`, `Degree[form]`, `Rank[obj]`, `Dimension[obj]`,
`Dimensions[obj]`, `ZeroQ[obj]`, `EquivalentQ[a, b]`, `MemoryStatus[]`.

`Component` of a Lie form takes the colour index first, then the form indices;
a degree-0 form takes none. `Dimension` reports the underlying space where
there is one, and the algebra or representation dimension otherwise.
`Dimensions` reports every concrete extent of vectors, matrices, legacy
tensors, sparse component tensors and bases.

The two decisions return the symbols `True`, `False` and `Unknown`, following
`phy_cas_is_zero`: an undecided question stays visibly undecided instead of
collapsing to `False`.

`MemoryStatus[]` returns exact rules for `IRNodes`, `IRBytes`, `CASBytes`,
`LiveObjects`, and `Bindings`. It is a diagnostic snapshot, not a request to
collect; normal command evaluation already performs the object sweep.

### Structural algebra

`alpha + beta` and `s * alpha` work on forms, algebra-valued forms, Lie
elements, vectors and matrices, so `(g/2)*LieBracket[A, A]` reads as the
formula it is. Subtraction
and division need no cases — the parser already writes `a - b` as `a + (-1)*b`
and `a/2` as `a * 2^-1`. Sums are homogeneous, and a product admits at most one
object factor, because the product of two forms is the wedge and has its own
spelling.

## Output constructors

`ScalarField`, `Propagator`, `Vertex`, `TadpoleIntegral`, `BubbleIntegral`,
`LorentzDot`, `DiracGamma`, and `SUNGenerator` are typed output vocabulary. They remain visible
IR rather than pretending to be independent commands. The reader-facing
commands above construct and reduce them through the native backends.

## Ownership

The environment owns every object it creates and destroys them in reverse
creation order, which is the order the layers below require: forms before
manifolds, manifolds before charts, everything before the IR context.

Evaluating a cell creates intermediates — `HodgeStar[Wedge[a, b]]` builds a
wedge nobody names. After each command the environment sweeps: everything
reachable from a binding or from the command's own result survives, the rest are
destroyed newest-first. Reachability follows a bounded dependency bitmap, so a
form keeps its manifold alive and a dynamic-rank tensor retains every distinct
slot basis even after their own names are cleared. Survivors are then compacted
with their order preserved, which
is what keeps "created later" and "destroyed first" the same statement.

The sweep runs on the failure path too. A command that failed half-way through a
gauge expression has already registered intermediates, and they are exactly what
nothing will ever reach again.

`phy_env_validate` checks the three invariants that make this sound — every
dependency points at a lower live slot, every binding names a live object or a
scalar, no object appears twice — and `tests/test_eval.c` calls it after every
single command, including the ones that fail.

## Coordinate capture

Binding a name that a live chart uses as a coordinate is `PHY_ERR_ASSUMPTION`,
and so is creating a chart whose coordinate is already bound. Without that rule
`x = 2` silently rewrites every component of every form on a chart with an `x`
axis, and the wrong answer is indistinguishable from the right one. Scalar
substitution is otherwise unrestricted, which is why this one case is closed
rather than documented as a hazard.

Differentiating with respect to a bound name is `PHY_ERR_TYPE` for the same
reason: `D[x^2, x]` after `x = 2` is not a derivative.

## Persistence

The environment is **not** serialized. A saved document stores cell sources and
results; reopening one starts with an empty environment, so a cell that reads a
binding fails until `phy_notebook_evaluate_all` replays the notebook in order —
`FILE` > `Run all cells` in the shell.

Objects hold pointers into an IR context and into each other. Writing them to a
file would mean inventing a second, weaker serialization of every physics layer,
and reading one back would mean trusting it. Replaying is cheap, exact, and
cannot disagree with the source.

The consequence for the cell model is that evaluation is now forward-dependent:
running one cell marks every result after it stale, because a cell that binds a
name changes what the cells below it mean.

## Limits

`PHY_EVAL_MAX_OBJECTS` is 96 and `PHY_EVAL_MAX_BINDINGS` is 32; exceeding either
is `PHY_ERR_TERM_LIMIT`. The notebook's own ceilings were raised with this phase
— 65,536 IR nodes, 2 MiB of IR pools, 200,000 CAS steps, 512 KiB of CAS memo and
scratch — because a 4D curvature pass interns several thousand nodes and the IR
has no collection, so a document that computes one and then edits a cell has to
be able to compute it again. They stay limits: an intentionally explosive
expression still fails as a typed `PHY_ERR_NODE_LIMIT`.

Object intermediates are mark-and-swept after every successful or failed
command. `Clear[name]` and `ClearAll[]` release object graphs no longer reachable
from bindings. CAS scratch memory is LIFO and the memo cache is bounded,
discarding and rebuilding itself at its ceiling. Interned IR nodes are immutable
and deliberately have no per-node collector: they live for the notebook
context, deduplicate identical expressions, and stop at the node/byte ceilings
above. New/Open destroys the old context and returns all of that memory. Thus a
document can reach a typed memory or node-limit error, but cannot write beyond
its configured arenas.

## Verification

`tests/test_eval.c`, 3,011 checks. The physics cases deliberately reproduce,
through reader-facing source, results the backend suites already certify
directly:

- the `U(1)` connection `A = x dy` giving `F = dx ^ dy`, its vanishing Bianchi
  residual, and the `-1/2` quadratic density of `tests/test_yang_mills.c`;
- the nine `SU(2)` curvature components of the same file, from constant
  `A^a = dx^a`, and `D_A F_A = 0`;
- `delta F = g[F, alpha]` and `delta A = D_A alpha` agreeing with the branch the
  evaluator selects from the operand degree;
- the round two-sphere of `tests/test_gr.c`: `R = 2/a^2`, the two Christoffel
  symbols, `R_{theta phi theta phi}`, a vanishing Einstein tensor,
  `K = 4/a^4`, and a vanishing covariant derivative of Ricci;
- four-dimensional Dirac traces, both routed Mandelstam conventions, the
  exact phi4 Lagrangian/EOM/tree-plus-loop set, and the sunset graph's exact
  Wick/symmetry analysis;
- symbolic-`N` colour Casimirs, exact SU(3) `f123`, `f147`, `f458`, invariant
  tensor symmetries, generator commutators, short/held traces, and typed
  Lorentz/colour separation from `tests/test_color.c`;
- graded commutativity of the wedge, `d^2 = 0`, the graded Leibniz rule, and
  `iota_v iota_v = 0` from `tests/test_geom.c`;
- `[T1,T2] = T3`, structure-constant antisymmetry, and `K_ab = -2 delta_ab` from
  `tests/test_lie.c`;
- the general coordinate-metric volume form and Hodge dual of
  `tests/test_geom_metric.c`.

If the evaluator merely preserved operator heads, none of them would hold.

The remaining cases cover state flow between cells, `Clear`/`ClearAll`, the
capture rules, every typed-error path, the sweep's object accounting under
rebinding and failure, the binding ceiling, and the notebook integration
including save/reopen with structured physics objects.

`tests/test_palette.c` additionally parses every CAS palette snippet, because a
palette that inserts something the evaluator rejects is worse than no palette.
The evaluator and supported source-command registries are public read-only
enumerations used by the test: every one of the 109 evaluator heads and 18
supported source commands must be named by at least one CAS insertion snippet.
This makes adding a backend operation without adding a discoverable notebook
entry a test failure.

The ARM link check is `make eval-link-check` and
`tests/device/eval_link_probe.c`: 17 declared entry points, the whole physics
stack behind one dispatcher, and the same no-float/no-libm/no-soft-float
standard the CAS and geometry layers are held to. It now links 69 portable
sources, retains 17/17 public evaluator entry points, contains no forbidden
float/libm/soft-float dependency, and packages as a 385,752-byte isolated
probe. That probe size includes its dependencies and is not an incremental
product-size measurement.

One consequence of this phase that the earlier link-check reports called out as
future work has now happened: the application genuinely calls the geometry,
Lie, Yang--Mills, and QFT layers, so `--gc-sections` no longer drops them.
The preserved `dist-foundation/phy-nspire.tns` baseline is 1,173,026 bytes.
The current `dist/phy-nspire.tns`, with the abstract tensor evaluator reachable,
is 1,252,366 bytes (19.9% of the 6 MiB ceiling); the final ELF retains
`phy_index_space_create`, `phy_tensor_head_create_with_symmetries`,
`phy_tensor_monomial_create`, `phy_tensor_monomial_canonicalize`,
`phy_tensor_monomial_young_project`, and
`phy_tensor_expression_term_count`, together with the GR and QFT bridge entry
points.
