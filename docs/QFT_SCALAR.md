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
- the one-loop two-point tadpole and the three `s/t/u` four-point bubbles;
- exact symmetry factors and coupling weights;
- unevaluated typed `TadpoleIntegral` and `BubbleIntegral` masters.

The loop corpus separates topology/combinatorics from convention-dependent
amplitude phases:

| object | external legs | symmetry factor | coupling weight |
| --- | ---: | ---: | ---: |
| tadpole | 2 | `1/2` | `lambda/2` |
| `s` bubble | 4 | `1/2` | `lambda^2/2` |
| `t` bubble | 4 | `1/2` | `lambda^2/2` |
| `u` bubble | 4 | `1/2` | `lambda^2/2` |

No integral is evaluated numerically and no UV pole is asserted yet. The
typed masters are the stable boundary for a later dimensional-regularization
table and FeynCalc golden comparison.

Host tests cover Lagrangian structure, inverse propagator and vertex,
propagator/master heads, the four one-loop objects, all exact weights, and
typed rejection of unsupported dimensions.

Not yet implemented:

- Euler--Lagrange or functional differentiation;
- Wick-contraction and general graph generation;
- counterterms, dimensional regularization, UV-pole extraction, or
  renormalization-group functions;
- multi-loop topology reduction or IBP;
- field-theory notebook heads, palettes, and diagram editor.
