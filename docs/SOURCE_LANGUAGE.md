# Reader-facing symbolic source language

## Contract

Notebook input has one authoritative reader-facing source string. Running a
cell follows this path:

```text
source -> precedence parser -> typed IR -> command dispatcher -> native CAS
       -> typed result/error -> direct 2D layout
```

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
- list structure with `{x,y}`.

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
- `{...}` to the structural `List` head.

Unknown non-reserved heads remain typed function applications. That is the
extension point for `Metric`, `Christoffel`, `DiracTrace`, and later physics
objects; it does not grant them evaluator semantics prematurely.

## Command registry

| Command | Current native action |
| --- | --- |
| bare expression | `Simplify` |
| `Simplify`, `FullSimplify` | exact native normal form |
| `Expand` | bounded distributive expansion |
| `Together` | native rational form reconstructed as one quotient |
| `Numerator`, `Denominator` | selected part of native rational form |
| `D[expr,x,...]` | repeated exact symbolic differentiation |

Registered but not implemented commands include `Cancel`, `Factor`, `Apart`,
`Integrate`, `Limit`, `Series`, `Solve`, `NSolve`, `Reduce`, `Refine`, and the
`Trig*` family. They return `PHY_ERR_UNSUPPORTED`. They are never accepted as
opaque ordinary functions, because that would present a no-op as successful
computer algebra.

The same rule applies when such a command is nested. General nested command
scheduling is future work; until then it fails explicitly.

## Current boundaries

- no assignments, definitions, replacement rules, patterns, or scoping;
- no arbitrary-precision integers beyond the exact `int64` core;
- no complex literals or numeric approximation command;
- no implicit function application beyond bracket/parenthesis calls;
- no tensor-index surface syntax yet;
- no strings, associations, datasets, or Wolfram Language evaluation model.

This is a deliberately compatible symbolic surface, not a claim to implement
the full Wolfram Language. The boundary is versioned through tests, and every
extension must map to typed IR plus an explicit evaluator contract.
