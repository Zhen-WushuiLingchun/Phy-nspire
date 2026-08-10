# Certified real and complex ball arithmetic

`include/phy/ball.h`, `src/exact/ball.c`, `src/exact/complex_ball.c`, and
`src/cas/ball_eval.c` provide the numerical layer that is permitted to
influence CAS publication. It is an interval-certificate layer, not a
binary-floating-point convenience path.

## Representation and operations

A `phy_real_ball` denotes the closed interval

```text
[midpoint - radius, midpoint + radius], radius >= 0
```

Both fields are native arbitrary-precision rationals. Addition, subtraction,
multiplication, division, signed integer powers, lower/upper extraction and
square root preserve enclosure. Division and negative powers first prove that
the divisor ball excludes zero. Allocation failure in that test is reported as
an error; the convenience Boolean query fails conservatively as "contains
zero".

`Exp`, `Log`, `Sin`, `Cos`, `Tan`, the hyperbolic family and their real inverse
functions are certified over real balls. `Erf`/`Erfc` use a rigorously bounded
power series for input balls wholly contained in `[-1,1]`; exact special values
simplified by the symbolic layer can use the same path outside that direct
kernel. Intermediate intervals are rounded outwards to exact dyadic grids.

A `phy_complex_ball` is the rectangular enclosure
`real_ball + I imaginary_ball`. Arithmetic, integer powers, exponential,
trigonometric/hyperbolic functions, principal square root/logarithm and the
inverse trigonometric/hyperbolic families are composed only from certified
real-ball operations. A rectangle containing a pole, zero divisor, or an
unresolved principal-branch crossing fails with a typed domain error.

All mutating public operations build a temporary result and publish only after
success. No operation calls `double`, a floating-point formatter, libm, or an
ARM soft-float helper.

## `N`

`N[expr]` and `N[expr,digits]` return `Around[midpoint,radius]` for real values
and `ComplexAround[real_ball,imaginary_ball]` for non-real values. The current
certified evaluator covers:

- exact integers and rationals;
- `Pi`, `E`, and `EulerGamma` from fixed certified 40-decimal intervals;
- exact sums, products, signed integer powers and principal square roots;
- real and complex elementary trigonometric/hyperbolic functions and their
  inverses, with principal-domain and pole proofs; and
- bounded certified real `Erf`/`Erfc`.

The requested precision is capped at 36 decimal digits. Exact inputs may have
zero radius, so `N[1/3]` deliberately returns `Around[1/3,0]`; decimal display
is not allowed to discard the exact certificate. General complex special
functions and unrestricted Gamma/Digamma numerical algorithms remain typed
unsupported rather than falling back to floating point.

## `NSolve`

`NSolve[equation,x]` covers bounded univariate rational polynomials:

1. reduce the equation to an exact rational numerator and denominator;
2. extract and square-free the numerator polynomial;
3. clear coefficient denominators exactly;
4. isolate all real roots with the certified algebraic Sturm layer;
5. refine each algebraic interval to the requested rational ball;
6. evaluate the original reduced denominator over that ball and require it to
   exclude zero; and
7. publish all real branches only after every branch succeeds.

An irreducible quadratic with negative discriminant is handled separately:
both conjugate roots are constructed from the exact quadratic formula, their
square-root rectangles are certified, and the original denominator is checked
over each complex rectangle before publication.

The result is all certified real roots plus all complex roots for degree at
most two. General higher-degree complex root isolation, multivariate numerical
systems, transcendental equations and adaptive precision escalation remain
explicit future work.

## Acceptance boundary

Host unit tests prove enclosure for real/complex arithmetic, principal
branches, inverse and hyperbolic functions, bounded error functions,
irrational square root, parser/evaluator integration, real quintic isolation,
complex quadratic roots, denominator exclusion, and typed precision ceilings.
The ARM probe touches every public real/complex ball entry point and rejects a
float formatter, libm call or ARM soft-float helper. Exact probe size is
recorded only after the current branch passes a fresh ARM run. Host
ASan/UBSan/leak and ARM link acceptance do not replace physical CX II
timing/heap checks.
