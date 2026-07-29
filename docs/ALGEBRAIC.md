# Certified real algebraic foundation

`include/phy/algebraic.h` and `src/exact/algebraic.c` provide the certified
real-root layer underneath polynomial `Solve`, radical comparison, and future
denominator rationalization.

## Representation

A real algebraic value is stored as:

1. a primitive, square-free polynomial in `Z[x]`, with positive leading
   coefficient; and
2. an exact rational open interval containing exactly one real root of that
   polynomial.

The polynomial is called a **defining polynomial**, not a minimal polynomial.
That distinction is mathematical, not cosmetic: square-free plus an isolating
interval uniquely identifies one root, but irreducibility over `Q` is a
separate certificate. General modular factorization must exist before this
project can canonicalize every value by a minimal polynomial.

This representation matches the deliberately non-minimal traditional
representation documented by CGAL's univariate Algebraic Kernel: a square-free
polynomial plus an isolating interval. FLINT `qqbar`, by contrast, uses a
minimal polynomial and a complex enclosure; it is the later, more expensive
canonical target, not a claim made by this milestone.

References:

- [CGAL Algebraic Kernel user manual](https://doc.cgal.org/latest/Algebraic_kernel_d/index.html)
- [CGAL `Algebraic_kernel_d_1` model](https://doc.cgal.org/latest/Algebraic_kernel_d/classCGAL_1_1Algebraic__kernel__d__1.html)
- [FLINT `qqbar` documentation](https://flintlib.org/doc/qqbar.html)

## Certificate path

Input coefficients are signed decimal integers in increasing degree order.
Construction:

1. parses every coefficient with the native bounded bigint kernel;
2. removes high zero coefficients and rejects degree zero;
3. divides by the positive coefficient content and makes the leading
   coefficient positive;
4. constructs the exact `Q[x]` Sturm sequence
   `f, f', -rem(f,f'), ...`;
5. rejects a zero remainder before the constant stage, which proves that
   `gcd(f,f')` has positive degree and the input is not square-free;
6. evaluates the sequence exactly at both rational endpoints, ignoring zero
   intermediate sequence values as Sturm's theorem requires;
7. accepts a value only when the variation difference is exactly one and
   neither endpoint is a root.

No floating-point sample participates in root existence, ordering, or
equality.

## Operations now available

- exact real-root count on an open rational interval;
- automatic isolation of every real root inside a conservative exact Cauchy
  bound, returned in strictly increasing order;
- certified construction and structural validation;
- access to the canonical defining polynomial and interval;
- transactional bisection refinement;
- collapse to an exact rational point when a midpoint is the root;
- safe comparison using disjoint intervals;
- certified equality for overlapping isolating intervals of the same defining
  polynomial;
- certified equality between a rational point and any defining polynomial that
  vanishes there.
- exact rational translation `alpha + r`, including arbitrary-precision
  offsets;
- exact rational scaling `r alpha`, including negative order reversal and the
  rational zero result;
- exact reciprocal `1/alpha`, with private interval refinement when the
  original certificate crosses zero;
- exact addition, subtraction, multiplication and division between two
  certified real algebraic values;
- exact signed integer powers, with zero and negative-power domain checks.

Each rational transform constructs the new integer defining polynomial,
normalizes its content and leading sign, transforms the interval with exact
rational arithmetic, and runs the Sturm certificate again before publishing
the result. A source certificate is never modified. Rational results collapse
to a canonical linear polynomial, and reciprocal of exact zero returns
`PHY_ERR_DOMAIN`.

For two non-rational operands, the arithmetic path evaluates the exact
Sylvester resultant at deterministic integer sample points, reconstructs it
by exact Newton interpolation, removes repeated factors over `Q[x]`, makes the
integer polynomial primitive, and isolates the unique interval selected by
interval arithmetic. Addition uses
`Res_y(p(y),q(x-y))`; multiplication uses
`Res_y(p(y),y^deg(q) q(x/y))`. Subtraction and division are exact compositions
with negation and reciprocal. The candidate is published only after a fresh
Sturm certificate proves that its open rational interval contains one root.
No floating-point sample participates in resultant construction or root
selection.

The output is deliberately a square-free **defining polynomial**, not a claim
of an irreducible minimal polynomial. Consequently structural equality across
unrelated defining polynomials is not generally canonical yet. Disjoint
certified intervals, a shared defining polynomial, and equality to a rational
point remain exact; otherwise comparison returns `PHY_ERR_UNSUPPORTED` rather
than guessing.

The scalar Q[x] factorizer now uses this API for irreducible factors of degree
three or more. Reader-facing `Solve` emits
`Root[List[a0,...,an],k]`, where coefficients are in increasing degree order
and `k` is one-based among that factor's increasing real roots. This is a
certified real-root convention; the project does not yet claim Mathematica's
ordering over all complex roots.

## Resource model

The algebraic context has independent ceilings for:

- exact limbs and exact-operation work;
- polynomial degree (default 32, hard ceiling 256);
- algebraic polynomial steps per public call;
- comparison/refinement rounds;
- coefficient-handle and polynomial-array metadata.

Cancellation reaches both the Sturm layer and the bigint/rational layer.
Creation, all-root isolation, and refinement are transactional.
Allocation-failure injection walks every allocation in both a representative
single-root certificate and a quintic all-root isolation. It verifies that a
failed call publishes no object, the context still validates, a retry succeeds,
and tracked live heap returns to zero. Independent step and cancellation
ceilings cover both root count and all-root isolation.

Current reproducible evidence:

- `test_exact`: 79,159 checks, zero failures;
- `test_algebraic`: 81,754 checks, zero failures, including allocation-failure,
  timeout, cancellation, arbitrary-precision and retry coverage for every
  rational transform and resultant arithmetic;
- strict Windows suite: 34/34 tests;
- ASan/UBSan/leak suite: 36/36 tests;
- Ndless exact-number link probe: 51/51 public entry points, 14,516 bytes of
  exact-layer ARM text, 19,912-byte packaged probe;
- Ndless real-algebraic link probe: 28/28 public entry points, 24,256 bytes of
  algebraic-layer ARM text, 43,160-byte packaged probe;
- neither ARM probe retains a floating-point formatter, libm call, or
  soft-float helper.

These are host and link/package results. Physical CX II timing and peak-heap
acceptance remain separate evidence.

## Deliberate omissions

This is a bounded real-algebraic closure, not yet a canonical complex
algebraic-number package:

- no proof of equality across unrelated irrational defining polynomials;
- no complex isolating rectangles;
- no radical-to-algebraic lowering in the typed IR;
- no claim that a defining polynomial is minimal.

Every arithmetic call is subject to the documented degree, coefficient, step,
memory and cancellation ceilings. Exceeding one is a typed resource error and
does not publish a partial certificate.
