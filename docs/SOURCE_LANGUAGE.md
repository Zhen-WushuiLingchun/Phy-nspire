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
are exact rationals while their numerator/denominator fit `int64`.

### Function and FullForm heads

Calls accept either Mathematica brackets or calculator-friendly parentheses:
`Sin[x]` and `sin(x)` are equivalent. Scalar heads recognized by the native CAS
are `Sin`, `Cos`, `Tan`, `Exp`, and `Log`.

The parser also maps useful FullForm constructors directly to IR:

- `Plus`, `Times`, `Power`;
- `Rational`, `Sqrt`;
- `Equal`;
- `Up[index,space]`, `Down[index,space]`;
- `Tensor[head,indices...]`, `Operator[head,args...]`;
- `NonCommutativeMultiply`, `Wedge`;
- `Commutator[A,B]` to the exact typed noncommutative difference `A.B-B.A`;
- typed physics-object heads, listed below;
- `{...}` to the structural `List` head.

Object heads are interned under the table's own spelling rather than the
reader's. Name matching is case-insensitive, so `manifold[...]` and
`Manifold[...]` must reach the same evaluator entry rather than intern two
unrelated heads.

The evaluated object heads are:

- geometry — `Manifold`, `DifferentialForm`, `Metric`, `VectorField`,
  `ExteriorD`, `InteriorProduct`, `HodgeStar`, `Volume`;
- Lie — `LieGroup`, `LieAlgebra`, `Generator`, `LieElement`, `LieBracket`,
  `StructureConstant`, `Killing`;
- gauge — `LieForm`, `GaugeConnection`, `CovariantD`, `FieldStrength`,
  `GaugeVariation`, `Bianchi`, `YangMillsLagrangian`, `ColorComponent`;
- relativity — `Curvature`, `InverseMetric`, `Christoffel`, `Riemann`,
  `RiemannMixed`, `Ricci`, `RicciScalar`, `Einstein`;
- queries — `Component`, `Degree`, `Dimension`, `Rank`, `ZeroQ`, `EquivalentQ`.

Each of them dispatches onto the corresponding native backend and returns a
typed value; the argument shapes are in [`EVALUATOR.md`](EVALUATOR.md). None of
them is preserved as an inert operator: a head the evaluator owns and cannot
evaluate returns a typed error.

`ScalarField`, `Propagator`, `Vertex`, `TadpoleIntegral` and `BubbleIntegral`
are the exception. They are reserved and construct typed IR for display and
persistence, and the bounded `phi^4` layer behind them is **not** wired to the
evaluator yet. Unknown non-reserved heads remain typed function applications;
that is the extension point for `DiracTrace`, and it does not grant it evaluator
semantics prematurely.

## Command registry

| Command | Current native action |
| --- | --- |
| bare expression | evaluate against the environment, then `Simplify` |
| `name = value`, `Set[name,value]` | evaluate and bind in the notebook environment |
| `Clear[name]`, `ClearAll[]` | unbind one name, or reset the environment |
| `Simplify`, `FullSimplify` | exact native normal form |
| `Expand` | bounded distributive expansion |
| `Together` | native rational form reconstructed as one quotient |
| `Numerator`, `Denominator` | selected part of native rational form |
| `D[expr,x,...]` | repeated exact symbolic differentiation |
| `Integrate[expr,x,...]` | exact symbolic antiderivative on the documented linear-inner class; otherwise an explicit unevaluated `Integrate` |

Every command except assignment and a bare expression is scalar algebra, so
`Expand[M]` on a manifold is `PHY_ERR_TYPE` rather than a silently ignored
request.

Registered but not implemented commands include `Cancel`, `Factor`, `Apart`,
`Limit`, `Series`, `Solve`, `NSolve`, `Reduce`, `Refine`, and the
`Trig*` family. They return `PHY_ERR_UNSUPPORTED`. They are never accepted as
opaque ordinary functions, because that would present a no-op as successful
computer algebra.

The same rule applies when such a command is nested. General nested command
scheduling is future work; until then it fails explicitly.

## Current boundaries

- assignment binds a name to a value; there are still no definitions with
  arguments, replacement rules, patterns, or scoping;
- binding a name a live chart uses as a coordinate is `PHY_ERR_ASSUMPTION`, in
  both directions — see [`EVALUATOR.md`](EVALUATOR.md);
- no arbitrary-precision integers beyond the exact `int64` core;
- no complex literals or numeric approximation command;
- no implicit function application beyond bracket/parenthesis calls;
- no shorthand Einstein syntax yet; explicit `Up`/`Down` indices retain their
  Generic/Lorentz/Spinor/color space in typed IR;
- no strings, associations, datasets, or Wolfram Language evaluation model.

This is a deliberately compatible symbolic surface, not a claim to implement
the full Wolfram Language. The boundary is versioned through tests, and every
extension must map to typed IR plus an explicit evaluator contract.
