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
power series with a geometric tail certificate. The real fast path remains
restricted to balls wholly contained in `[-1,1]`; the complex kernel accepts
general finite rectangular arguments within its 2048-term resource ceiling.
Intermediate intervals are rounded outwards to exact dyadic grids.

A `phy_complex_ball` is the rectangular enclosure
`real_ball + I imaginary_ball`. Arithmetic, integer powers, exponential,
trigonometric/hyperbolic functions, principal square root/logarithm and the
inverse trigonometric/hyperbolic families are composed only from certified
real-ball operations. A rectangle containing a pole, zero divisor, or an
unresolved principal-branch crossing fails with a typed domain error.

The complex special-function layer also implements bounded certified
`LogGamma`, `Gamma`, and `Digamma`. It translates the argument by exact
recurrences into the right half-plane, applies a Bernoulli/Stirling expansion
with an explicit remainder bound, and translates back. `Gamma` exponentiates
the certified `LogGamma` rectangle. The shift ceiling is 512 and the expansion
ceiling is 256 rounds; a pole or a rectangle that cannot prove pole exclusion
fails transactionally.

All mutating public operations build a temporary result and publish only after
success. No operation calls `double`, a floating-point formatter, libm, or an
ARM soft-float helper.

## `N`

`N[expr]` and `N[expr,digits]` return
`Around[midpoint,radius,digits]` for real values and
`ComplexAround[real_ball,imaginary_ball]` for non-real values. Before
publication every component is checked exactly against the mixed
significant-digit contract

`radius <= 10^-digits max(1,abs(midpoint))`.

If the first certified enclosure is too wide, `N` performs at most three
bounded precision attempts; failure to meet the contract is a typed resource
error, never a lower-precision result carrying the requested label. The
current certified evaluator covers:

- exact integers and rationals;
- `Pi`, `E`, and `EulerGamma` from fixed certified 40-decimal intervals;
- exact sums, products, signed integer powers and principal square roots;
- real and complex elementary trigonometric/hyperbolic functions and their
  inverses, with principal-domain and pole proofs; and
- bounded certified real/complex `Erf`/`Erfc`; and
- bounded certified complex `Gamma`, `LogGamma`, and `Digamma` away from
  unresolved poles.

The requested precision is capped at 36 decimal digits. Exact inputs may have
zero radius, so `N[1/3,12]` deliberately returns
`Around[1/3,0,12]`; the display backend uses the third field for significant
digits while retaining the exact midpoint and certificate. Inputs outside
those bounded resource/domain contracts remain typed unsupported or
resource-limited rather than falling back to floating point.

## `NSolve`

`NSolve[equation,x]` covers bounded univariate rational polynomials:

1. reduce the equation to an exact rational numerator and denominator;
2. extract and square-free the numerator polynomial;
3. clear coefficient denominators exactly;
4. isolate all real roots with the certified algebraic Sturm layer;
5. if non-real roots remain, generate rational Durand--Kerner candidate
   centres with deterministic asymmetric seeds;
6. accept a candidate square only when an exact Taylor/Pellet--Rouche
   inequality proves it contains exactly one root, all squares are disjoint,
   and their count equals the square-free degree;
7. evaluate the original reduced denominator over every real or complex ball
   and require it to exclude zero; and
8. publish branches only after the complete set succeeds.

An irreducible quadratic with negative discriminant is handled separately:
both conjugate roots are constructed from the exact quadratic formula, their
square-root rectangles are certified, and the original denominator is checked
over each complex rectangle before publication.

The complex path is bounded to square-free univariate rational polynomials of
degree at most 48. It uses at most four deterministic certification levels,
16 guard bits per level and 64 candidate iterations per level. The first level
uses the requested grid; only unresolved or colliding roots pay for the finer
fallback grid, whose centres are retained while exact certificate boxes
shrink. Repeated factors are removed exactly, so each distinct root is
returned once. Multivariate numerical systems, transcendental equations and
open-ended precision escalation remain explicit future work.

## Acceptance boundary

Host unit tests prove enclosure for real/complex arithmetic, principal
branches, inverse and hyperbolic functions, complex error/Gamma/Digamma
functions, irrational square root, parser/evaluator integration, real and
complex cubic/quartic/quintic isolation, denominator exclusion, and typed
precision ceilings.
The ARM probe touches every public real/complex ball entry point and rejects a
float formatter, libm call or ARM soft-float helper. Exact probe size is
recorded only after the current branch passes a fresh ARM run. Host
ASan/UBSan/leak and ARM link acceptance do not replace physical CX II
timing/heap checks.
