# Reader-facing symbolic source language

## Contract

Notebook input has one authoritative reader-facing source string. Running a
cell follows this path:

```text
source -> precedence parser -> typed IR -> stateful evaluator
       -> native CAS / geometry / Lie / Yang-Mills / GR backends
       -> typed value -> typed result/error -> direct 2D layout
```

The evaluator stage owns the notebook environment and is documented separately
in [`EVALUATOR.md`](EVALUATOR.md). A cell is evaluated *against* that
environment, so cells are no longer independent of one another.

After a successful parse, the cell also records stable backend-neutral IR text.
That text is a verification/persistence form, not a second hidden expression
that can disagree with the editor.

The parser and dispatcher are permanent front-end infrastructure, not a demo
case table. Physics heads can be added to evaluator registries without changing
arithmetic precedence or the notebook editor.

## Implemented syntax

### Atoms and operators

- identifiers: `x`, `theta`, `metric`;
- exact integers and finite decimals: `12`, `1.25` (stored as `5/4`);
- prefix `+` and `-`;
- `+`, `-`, `*`, `/`, right-associative `^`;
- implicit multiplication: `2x`, `x y`, `(x+1)(x-1)`;
- grouping with parentheses;
- equations with `==`;
- assignment with `=`;
- list structure with `{x,y}`.

`=` and `==` are distinguished by one character of lookahead, so `x = 1` binds
and `x == 1` is an equation; neither is a typo for the other. A reserved
command, object head, or known scalar function is not a bindable name, because
`Sin = 2` would leave a document in which `Sin[x]` means two different things
depending on cell order.

There is no silent floating-point fallback in this grammar. Decimal literals
are exact rationals and promote to the bounded arbitrary-precision domain when
their numerator or denominator leaves `int64`.

### Function and FullForm heads

Calls accept either Mathematica brackets or calculator-friendly parentheses:
`Sin[x]` and `sin(x)` are equivalent. Scalar heads recognized by the native CAS
include the elementary trigonometric, inverse, hyperbolic, exponential and
logarithmic families, `Gamma`, `LogGamma`, `Erf`, `Erfc`, and the exact complex
heads `Re`, `Im`, `Conjugate`, and `Abs`.

The parser also maps useful FullForm constructors directly to IR:

- `Plus`, `Times`, `Power`;
- `Rational`, `Sqrt`;
- `Equal`;
- `Up[index,space?]`, `Down[index,space?]`;
- `Tensor[head,indices...]`, `Operator[head,args...]`;
- `NonCommutativeMultiply`, `Wedge`;
- `Commutator[A,B]` to the exact typed noncommutative difference `A.B-B.A`;
- typed physics-object heads, listed below;
- `{...}` to the structural `List` head.

Object heads are interned under the table's own spelling rather than the
reader's. Name matching is case-insensitive, so `manifold[...]` and
`Manifold[...]` must reach the same evaluator entry rather than intern two
unrelated heads.

The optional second argument of `Up`/`Down` is an index space, not part of
ordinary variance. `Tensor[T,Down[mu],Up[nu]]` therefore uses generic indices;
`Lorentz`, `ColorAdjoint`, and the spinor spaces are written only where a QFT
operation must reject a cross-space contraction. A component tensor declares
slot variance once with
`ComponentTensor[M,{Down,Up,...},components]`; component lookup then takes
integer coordinate positions rather than repeating abstract index labels.

Coordinate-free tensors use a separate typed surface:

```text
V = IndexSpace[4, SymmetricMetric]
A = TensorHead[{V,V}, Antisymmetric]
T = TensorHead[{V,V}, Commuting]
TensorCanonicalize[A[Down[b],Down[a]]]
YoungProject[T[Down[a],Down[b]],{{1,2}}]
```

`IndexSpace` accepts a positive integer or symbolic dimension and
`NoMetric`, `SymmetricMetric`, or `AntisymmetricMetric`. `TensorHead` accepts a
list of slot spaces, `Commuting`/`NonCommuting`, and an optional list of signed
1-based generators such as `Symmetry[{2,1,3},-1]`; rank-two `Symmetric` and
`Antisymmetric` are shortcuts. Direct head application creates an abstract
monomial, products combine factors, and `TensorCanonicalize` performs exact
slot/factor/dummy/metric canonicalization. `YoungProject` takes a one-based
row tableau, optionally preceded by a one-based factor position, and returns
the normalized, exactly collected multi-term expression. An explicit space in
`Down[i,V]`/`Up[i,V]` is checked; an omitted space is inferred from the head.

Concrete runtime shapes and the explicit bridge use:

```text
v  = Vector[{1,2,3}]
A  = Matrix[{{1,2},{3,4}}]
xy = ComponentBasis[V,{x,y}]
Tc = TensorComponents[T,{xy,xy},{Down,Up},{{{0,0},a}}]
Rc = ComponentLift[Riemann[c],R,{xy,xy,xy,xy}]
ComponentValue[T[Down[i],Up[j]],{Tc},{0,0}]
```

Coordinate changes remain explicit:

```text
uv = ComponentBasis[V,{u,v}]
tr = BasisTransition[xy,uv,{x+y,x-y},{(u+v)/2,(u-v)/2}]
Txy = TransitionPullback[tr,Tuv]
```

The evaluated object heads are:

- exact linear algebra — `Vector`, `Matrix`, `Dot`, `Transpose`,
  `Determinant`, `Inverse`, `RowReduce`, `MatrixRank`, `LinearSolve`;
- abstract/components — `IndexSpace`, `TensorHead`, indexed head application,
  `TensorCanonicalize`, `YoungProject`, `YoungDeclare`, `YoungReduce`,
  `GarnirRelation`, `YoungDimension`, `ComponentBasis`,
  `TensorComponents`, `ComponentLift`, `ComponentValue`;
- maps/atlas — `CoordinateMap`, `BasisTransition`, `Jacobian`,
  `PullbackScalar`, `PullbackCovector`, `PushForwardVector`,
  `TransitionPullback`, `Atlas`, `AtlasAddTransition`, `AtlasVerify`,
  `AtlasPullback`;
- geometry — `Manifold`, `ComponentTensor`, `DifferentialForm`, `Metric`, `VectorField`,
  `ExteriorD`, `InteriorProduct`, `LieDerivative`, `HodgeStar`, `Volume`;
- Lie — `LieGroup`, `LieAlgebra`, `Generator`, `LieElement`, `LieBracket`,
  `StructureConstant`, `Killing`;
- gauge — `LieForm`, `GaugeConnection`, `CovariantD`, `FieldStrength`,
  `GaugeVariation`, `Bianchi`, `YangMillsLagrangian`, `ColorComponent`;
- relativity — `Curvature`, `InverseMetric`, `Christoffel`, `Riemann`,
  `RiemannMixed`, `Ricci`, `RicciScalar`, `Einstein`, `Kretschmann`,
  `Weyl`, `WeylSquared`, `GeodesicAcceleration`, `CovariantDerivative`;
- QFT — `DiracTrace`, `MandelstamReduce`, `Phi4Lagrangian`, `Phi4EOM`,
  `Phi4Diagrams`, `Phi4Graph`, `Phi4Renormalization`, `Phi4Counterterm`, `SUNDelta`,
  `SUNF`, `SUND`, `SUNT`, `SUNTrace`,
  `SUNCommutator`, `SUNDeltaContract`, `SUNCF`, `SUNCA`, `SUNFComponent`,
  `SUNExpandCasimirs`, `SUNFundamentalCasimir`, `SUNAdjointCasimir`,
  `QFTSystem`, `QFTSpace`, `QFTBasis`, `QFTHead`, `QFTTensor`;
- queries — `Component`, `Degree`, `Dimension`, `Dimensions`, `Rank`, `ZeroQ`,
  `EquivalentQ`.

Each of them dispatches onto the corresponding native backend and returns a
typed value; the argument shapes are in [`EVALUATOR.md`](EVALUATOR.md). None of
them is preserved as an inert operator: a head the evaluator owns and cannot
evaluate returns a typed error.

`ScalarField`, `Propagator`, `Vertex`, `TadpoleIntegral`, `BubbleIntegral`,
`LorentzDot`, `DiracGamma`, `SUNGenerator`, and the held
`SUNTrace[N,a,b,c,d,...]` form are output vocabulary. The evaluated QFT heads
above create and reduce them; the held long-trace form is intentionally
idempotent when evaluated again. Unknown
non-reserved heads remain typed function applications and acquire no evaluator
semantics implicitly.

## Command registry

| Command | Current native action |
| --- | --- |
| bare expression | evaluate against the environment, then `Simplify` |
| `name = value`, `Set[name,value]` | evaluate and bind in the notebook environment |
| `Clear[name]`, `ClearAll[]` | unbind one name, or reset the environment |
| `Simplify` | exact native normal form |
| `FullSimplify` | normal form, then the decision-grade trig-basis rational form; the shorter of the two is returned, so `Sin[x]^2+Cos[x]^2` reaches `1` while `1/Tan[q]` keeps its spelling. On a typed object it passes through like `Simplify` |
| `Expand` | bounded distributive expansion |
| `Together` | native rational form reconstructed as one quotient |
| `Cancel` | exact univariate Q[x] and bounded sparse multivariate GCD cancellation, with exact reconstruction before publication |
| `Factor` | complete exact factorization on the documented bounded Q[x] class; unsupported rather than partial outside it |
| `Apart` | exact polynomial division and partial fractions over the unique variable on the documented bounded Q[x] class |
| `Numerator`, `Denominator` | selected part of native rational form |
| `D[expr,x,...]` | repeated exact symbolic differentiation |
| `Integrate[expr,x,...]` | exact symbolic antiderivative on the documented linear-inner and bounded polynomial-times-elementary classes; every evaluated result is differentiated back before publication |
| `Series[expr,{x,a,n}]` | exact bounded Taylor/Laurent expansion through power `n`, retaining `O((x-a)^(n+1))` in typed `SeriesData` |
| `Normal[series]` | remove a well-formed series order term; `Normal[Series[...]]` is supported as one combined reader action |
| `Limit[expr,{x,a}]` | exact finite two-sided limit when both directions agree |
| `Limit[expr,{x,a,FromAbove}]`, `Limit[expr,{x,a,FromBelow}]` | exact directed finite limit; `Direction->"FromAbove"` and `Direction->"FromBelow"` are equivalent spellings |
| `Limit[expr,{x,Infinity}]`, `Limit[expr,{x,-Infinity}]` | exact rational/Laurent infinity limit through the certified `t=1/x` transform |
| `Solve[equation,x]` | exact distinct roots for bounded reduced Q[x] equations: rational/constant affine roots, real or complex quadratic radicals, and certified `Root[{a0,...,an},k]` values when a higher irreducible factor is proved all-real; denominator roots are excluded |
| `Solve[{equation,...},{x,...}]` | exact linear systems through eight equations/variables, then bounded zero-dimensional sparse polynomial systems when a verified triangular Gröbner basis is obtained; every solution is substituted back |
| `Resultant[f,g,x]` | exact bounded Sylvester resultant over the shared rational polynomial domain |
| `Discriminant[f,x]` | exact derivative/resultant discriminant with the conventional leading-coefficient/sign normalization |
| `GroebnerBasis[{f,...},{x,...}]` | bounded exact lexicographic Buchberger basis, published only after generator-membership and S-pair verification |
| `N[expr]`, `N[expr,digits]` | certified real or rectangular complex rational ball for exact arithmetic, constants, principal roots/logarithms, trigonometric/hyperbolic and inverse functions, plus bounded real `Erf`/`Erfc`; up to 36 requested decimal digits |
| `NSolve[equation,x]` | all certified real roots of a bounded univariate rational polynomial plus both certified complex roots for irreducible quadratics, with denominator exclusion |

Every command except assignment, a bare expression, and the two simplifies is
scalar algebra, so `Expand[M]` on a manifold is `PHY_ERR_TYPE` rather than a
silently ignored request.

Registered but not implemented commands include `Reduce`, `Refine`, and the
`Trig*` family. They return `PHY_ERR_UNSUPPORTED`. They are never accepted as
opaque ordinary functions, because that would present a no-op as successful
computer algebra.

Their promotion cases are compiled from
`tests/corpus/cas_foundation_cases.inc`; changing a registry flag without
updating that matrix and adding evaluator/backend tests fails the strict suite.

The same rule applies when such a command is nested. General nested command
scheduling is future work; the explicitly implemented
`Normal[Series[...]]` composition is the only current nested command action.

## Current boundaries

- assignment binds a name to a value; there are still no definitions with
  arguments, replacement rules, patterns, or scoping;
- binding a name a live chart uses as a coordinate is `PHY_ERR_ASSUMPTION`, in
  both directions — see [`EVALUATOR.md`](EVALUATOR.md);
- `N` now certifies real and rectangular complex arithmetic, principal
  elementary/inverse/hyperbolic functions and bounded real error functions;
  `NSolve` certifies all real roots of its univariate rational-polynomial
  class and both non-real roots of irreducible quadratics. Higher-degree
  complex isolation is still deferred;
- `Solve` publishes exact rational/constant affine roots, real and complex
  quadratic radicals, and certified roots of higher-degree all-real factors.
  `Root[{a0,...,an},k]` uses increasing coefficient
  order and a one-based index among that factor's increasing real roots; full
  complex-algebraic root ordering is deferred. An unresolved non-real factor
  of degree at least three, identity with
  infinitely many solutions, nonlinear multivariate equation, or
  transcendental equation returns `PHY_ERR_UNSUPPORTED`; exact simultaneous
  affine and bounded triangular zero-dimensional polynomial systems are
  supported and never return a partial rule list;
- no `a+bi` literal token; exact complex expressions use the protected symbol
  `I`, while the numeric ball layer is deliberately real-only;
- no implicit function application beyond bracket/parenthesis calls;
- no shorthand Einstein syntax yet; explicit `Up`/`Down` indices retain their
  Generic/Lorentz/Spinor/color space in typed IR;
- no strings, associations, datasets, or Wolfram Language evaluation model.

This is a deliberately compatible symbolic surface, not a claim to implement
the full Wolfram Language. The boundary is versioned through tests, and every
extension must map to typed IR plus an explicit evaluator contract.
