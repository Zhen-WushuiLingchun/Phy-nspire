# Certified real algebraic foundation

`include/phy/algebraic.h` and `src/exact/algebraic.c` provide the certified
real-root layer underneath polynomial `Solve`, radical comparison, and future
denominator rationalization.

## Representation

A real algebraic value is stored canonically as:

1. its primitive, irreducible polynomial in `Z[x]`, with positive leading
   coefficient;
2. its one-based position among the real roots of that polynomial; and
3. a deterministic exact dyadic open interval containing exactly that root.

The polynomial is therefore the value's **minimal polynomial over `Q`**, not
merely a defining polynomial. Construction may start from a reducible
polynomial and any valid isolating interval: the bounded modular
Berlekamp/Hensel/Zassenhaus path factors the square-free input, exact Sturm
counts select the unique irreducible factor, and deterministic bisection
normalizes the interval. Equality and hashing depend only on the normalized
minimal polynomial and real-root index; caller interval choices and allocator
identity are irrelevant.

This is the real-algebraic analogue of the canonical identity used by FLINT
`qqbar`. It intentionally stops short of FLINT's complex enclosure and complete
complex-algebraic arithmetic.

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
4. computes the square-free part, factors it over `Q`, and retains only the
   irreducible factor with one root in the supplied interval;
5. constructs the exact `Q[x]` Sturm sequence
   `f, f', -rem(f,f'), ...`;
6. evaluates the sequence exactly at both rational endpoints, ignoring zero
   intermediate sequence values as Sturm's theorem requires;
7. computes the selected factor's absolute real-root index; and
8. bisects from a deterministic Cauchy bound until the canonical dyadic cell
   for that indexed root is obtained.

No floating-point sample participates in root existence, ordering, or
equality.

## Operations now available

- exact real-root count on an open rational interval;
- automatic isolation of every real root inside a conservative exact Cauchy
  bound, returned in strictly increasing order;
- certified construction and structural validation;
- access to the canonical minimal polynomial, root index and interval;
- transactional bisection refinement;
- collapse to an exact rational point when a midpoint is the root;
- safe comparison using disjoint intervals;
- exact canonical equality and stable hashing across unrelated input
  certificates that describe the same real algebraic value;
- exact rational translation `alpha + r`, including arbitrary-precision
  offsets;
- exact rational scaling `r alpha`, including negative order reversal and the
  rational zero result;
- exact reciprocal `1/alpha`, with private interval refinement when the
  original certificate crosses zero;
- exact addition, subtraction, multiplication and division between two
  certified real algebraic values;
- exact signed integer powers, with zero and negative-power domain checks.

Each rational transform constructs a candidate integer polynomial, selects its
irreducible factor, normalizes the root identity and interval, and runs the
Sturm certificate again before publishing the result. A source certificate is
never modified. Rational results collapse to a canonical linear polynomial,
and reciprocal of exact zero returns `PHY_ERR_DOMAIN`.

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

The square-free resultant is never published directly. It is factored and the
interval-selected irreducible factor is canonicalized first. This closes
addition, subtraction, multiplication and division under exact equality and
hashing for the supported bounded real-algebraic domain.

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
- `test_algebraic`: 157,070 checks, zero failures, including allocation-failure,
  timeout, cancellation, arbitrary-precision, minimal-polynomial selection,
  canonical equality/hash and retry coverage;
- strict Windows suite: 34/34 tests;
- ASan/UBSan/leak suite: 36/36 tests;
- Ndless exact-number link probe: 68/68 public entry points, 17,680 bytes of
  exact-layer ARM text, 23,540-byte packaged probe;
- Ndless real-algebraic link probe: 31/31 public entry points, 38,720 bytes of
  algebraic-layer ARM text, 66,572-byte packaged probe;
- neither ARM probe retains a floating-point formatter, libm call, or
  soft-float helper.

These are host and link/package results. Physical CX II timing and peak-heap
acceptance remain separate evidence.

## Deliberate omissions

This is a canonical bounded **real** algebraic closure, not a complete complex
algebraic-number package:

- no complex isolating rectangles;
- no radical-to-algebraic lowering in the typed IR;
- no reader-facing arithmetic directly on serialized `Root[...]` objects yet;
- no asymptotically fast large-degree factor selection.

Every arithmetic call is subject to the documented degree, coefficient, step,
memory and cancellation ceilings. Exceeding one is a typed resource error and
does not publish a partial certificate.
