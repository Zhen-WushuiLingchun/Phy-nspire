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
are `Sin`, `Cos`, `Tan`, `Exp`, and `Log`.

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

The evaluated object heads are:

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
  `SUNExpandCasimirs`, `SUNFundamentalCasimir`, `SUNAdjointCasimir`;
- queries — `Component`, `Degree`, `Dimension`, `Rank`, `ZeroQ`, `EquivalentQ`.

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
| `Cancel` | exact known-factor and bounded univariate Q[x] GCD cancellation |
| `Factor` | complete exact factorization on the documented bounded Q[x] class; unsupported rather than partial outside it |
| `Apart` | exact polynomial division and partial fractions over the unique variable on the documented bounded Q[x] class |
| `Numerator`, `Denominator` | selected part of native rational form |
| `D[expr,x,...]` | repeated exact symbolic differentiation |
| `Integrate[expr,x,...]` | exact symbolic antiderivative on the documented linear-inner class; otherwise an explicit unevaluated `Integrate` |
| `Series[expr,{x,a,n}]` | exact bounded Taylor/Laurent expansion through power `n`, retaining `O((x-a)^(n+1))` in typed `SeriesData` |
| `Normal[series]` | remove a well-formed series order term; `Normal[Series[...]]` is supported as one combined reader action |
| `Limit[expr,{x,a}]` | exact finite two-sided limit when both directions agree |
| `Limit[expr,{x,a,FromAbove}]`, `Limit[expr,{x,a,FromBelow}]` | exact directed finite limit; `Direction->"FromAbove"` and `Direction->"FromBelow"` are equivalent spellings |
| `Limit[expr,{x,Infinity}]`, `Limit[expr,{x,-Infinity}]` | exact rational/Laurent infinity limit through the certified `t=1/x` transform |
| `Solve[equation,x]` | exact distinct real roots for bounded reduced Q[x] equations: rationals, real quadratic radicals, and certified `Root[{a0,...,an},k]` values for higher irreducible factors; denominator roots are excluded |

Every command except assignment, a bare expression, and the two simplifies is
scalar algebra, so `Expand[M]` on a manifold is `PHY_ERR_TYPE` rather than a
silently ignored request.

Registered but not implemented commands include `NSolve`, `Reduce`,
`Refine`, and the
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
- no complex arbitrary-precision numeric approximation layer; exact integers
  and rationals do promote natively under explicit resource ceilings;
- `Solve` publishes exact rational, real quadratic-radical, and certified
  higher-degree real roots. `Root[{a0,...,an},k]` uses increasing coefficient
  order and a one-based index among that factor's increasing real roots; full
  complex-root ordering is deferred. A non-real factor, identity with
  infinitely many solutions, multivariate equation, or transcendental
  equation returns `PHY_ERR_UNSUPPORTED`; it never returns a partial root list;
- no complex literals or numeric approximation command;
- no implicit function application beyond bracket/parenthesis calls;
- no shorthand Einstein syntax yet; explicit `Up`/`Down` indices retain their
  Generic/Lorentz/Spinor/color space in typed IR;
- no strings, associations, datasets, or Wolfram Language evaluation model.

This is a deliberately compatible symbolic surface, not a claim to implement
the full Wolfram Language. The boundary is versioned through tests, and every
extension must map to typed IR plus an explicit evaluator contract.
