# Bounded scalar `phi^4` QFT layer

`include/phy/qft_scalar.h` and `src/qft/scalar.c` implement the first native
field-theory object model rather than treating a Feynman diagram as an
unconnected picture.

For a real field with

```text
L = 1/2 d_mu(phi) d^mu(phi)
  - 1/2 m^2 phi^2
  - lambda/4! phi^4
```

the layer creates:

- a typed Minkowski Lagrangian expression using `ScalarField`, `Partial`, and
  `LorentzDot` heads;
- the exact free inverse propagator `p^2 - m^2`;
- an opaque typed `Propagator[p^2,m,D]` object;
- the quartic vertex with the overall factor of `i` stripped, `-lambda`;
- the exact Euler--Lagrange left-hand side
  `Box[phi] + m^2 phi + lambda phi^3/3!`;
- the labelled, amputated tree-level `2 -> 2` graph and stripped-`i`
  amplitude `-lambda`;
- the one-loop two-point tadpole and the three `s/t/u` four-point bubbles;
- exact symmetry factors and coupling weights;
- exact graph checks `4V = 2I + E`, `L = I - V + 1`, and superficial degree
  `omega = D L - 2 I`;
- unevaluated typed `TadpoleIntegral` and `BubbleIntegral` masters;
- exact one-loop `MS` and `MSBar` multiplicative renormalization constants
  and the corresponding local counterterm density in `D = 4 - 2 epsilon`.

The loop corpus separates topology/combinatorics from convention-dependent
amplitude phases:

| object | V | I | E | L | symmetry factor | stripped weight |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| labelled tree vertex | 1 | 0 | 4 | 0 | `1` | `-lambda` |
| tadpole | 1 | 1 | 2 | 1 | `1/2` | `lambda/2` |
| `s` bubble | 2 | 2 | 4 | 1 | `1/2` | `lambda^2/2` |
| `t` bubble | 2 | 2 | 4 | 1 | `1/2` | `lambda^2/2` |
| `u` bubble | 2 | 2 | 4 | 1 | `1/2` | `lambda^2/2` |

No integral is evaluated numerically. The typed masters remain the stable
boundary for a later finite-part table; the one-loop UV constants below use a
separately declared normalization rather than silently assigning one to those
opaque master heads.

Notebook cells call the backend through
`Phi4Lagrangian[phi,m,lambda,D]`, `Phi4EOM[...]`, and
`Phi4Diagrams[phi,m,lambda,D,s,t,u]`. The last result is an ordered list
containing the tree amplitude, tadpole, and `s/t/u` bubbles. Host tests cover
these evaluator paths as well as the direct graph topology, exact weights,
field equation, and typed rejection of inconsistent graphs and unsupported
dimensions.

The one-loop renormalization commands are:

```text
Phi4Renormalization[phi,m,lambda,4,epsilon,MS]
Phi4Renormalization[phi,m,lambda,4,epsilon,MSBar]
Phi4Counterterm[phi,m,lambda,4,epsilon,MSBar]
```

The first returns labelled `Rule` values in the order
`DeltaZPhi`, `DeltaZm`, `DeltaZLambda`. With

```text
P_MS    = 1/epsilon
P_MSBar = 1/epsilon - EulerGamma + Log[4 Pi]
```

the exact result is

```text
DeltaZPhi    = 0
DeltaZm      = lambda P / (32 Pi^2)
DeltaZLambda = 3 lambda P / (32 Pi^2)
```

These are the relative multiplicative constants `Z - 1`, not additive
`delta(m^2)` and `delta(lambda)`. The counterterm density is

```text
-m^2 DeltaZm ScalarField[phi]^2 / 2
-lambda DeltaZLambda ScalarField[phi]^4 / 4!
```

This convention and its factor of two are pinned to `D = 4 - 2 epsilon`.
They match the FeynCalc phi4 one-loop example's published MS/MSbar comparison
to Bailin and Love:
<https://feyncalc.github.io/FeynCalcExamples/Phi4/OneLoop/Renormalization>.
Keeping the convention in the public API avoids mixing that epsilon with a
`D = 4 - epsilon` formula.

Not yet implemented:

- functional differentiation beyond the built-in model equation;
- Wick-contraction and general graph generation;
- finite parts of dimensionally regulated masters, automatic UV-pole
  extraction from arbitrary integrands, or renormalization-group running;
- multi-loop topology reduction or IBP;
- a graphical diagram editor.
