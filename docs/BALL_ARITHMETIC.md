# Certified real ball arithmetic

`include/phy/ball.h`, `src/exact/ball.c`, and `src/cas/ball_eval.c` provide the
first numerical layer that is permitted to influence CAS publication. It is
an interval certificate layer, not a binary-floating-point convenience path.

## Representation and operations

A `phy_real_ball` denotes the closed interval

```text
[midpoint - radius, midpoint + radius], radius >= 0
```

Both fields are native arbitrary-precision rationals. Addition, subtraction,
multiplication, division, signed integer powers, lower/upper extraction and
square root preserve enclosure. Square root uses monotone exact rational
bisection with an explicit round bound. Division and negative powers first
prove that the divisor ball excludes zero. Allocation failure in that test is
reported as an error; the convenience Boolean query fails conservatively as
"contains zero".

All mutating public operations build a temporary result and publish only after
success. No operation calls `double`, a floating-point formatter, libm, or an
ARM soft-float helper.

## `N`

`N[expr]` and `N[expr,digits]` return
`Around[midpoint,radius]`. The current certified evaluator covers:

- exact integers and rationals;
- `Pi`, `E`, and `EulerGamma` from fixed certified 40-decimal intervals;
- exact sums, products and signed integer powers;
- principal real square roots whose input ball is proved nonnegative.

The requested precision is capped at 36 decimal digits. Exact inputs may have
zero radius, so `N[1/3]` deliberately returns `Around[1/3,0]`; decimal display
is not allowed to discard the exact certificate. Trigonometric, exponential,
logarithmic, complex and general special-function ball algorithms are not yet
implemented and return a typed unsupported status.

## `NSolve`

`NSolve[equation,x]` currently covers bounded univariate rational polynomials:

1. reduce the equation to an exact rational numerator and denominator;
2. extract and square-free the numerator polynomial;
3. clear coefficient denominators exactly;
4. isolate all real roots with the certified algebraic Sturm layer;
5. refine each algebraic interval to the requested rational ball;
6. evaluate the original reduced denominator over that ball and require it to
   exclude zero; and
7. publish all real branches as `{{x -> Around[...]},...}` only after every
   branch succeeds.

The result is all certified **real** roots, not all complex roots. Complex
roots, multivariate numerical systems, transcendental equations and adaptive
precision escalation remain explicit future work.

## Acceptance boundary

Host unit tests prove enclosure for arithmetic and irrational square root,
parser/evaluator/menu integration, MathTree `midpoint +/- radius` display, real
quintic isolation, empty real-root sets, denominator exclusion, and typed
precision ceilings. The 2026-08-10 ARM probe retained 17/17 public ball APIs in
4,988 bytes of layer text, packaged to 19,400 bytes, with no float formatter,
libm call or ARM soft-float helper. Host ASan/UBSan/leak and ARM link acceptance
do not replace physical CX II timing/heap checks; this document must not treat
them as a device-runtime measurement.
