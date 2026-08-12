# Certified real and complex algebraic foundation

`include/phy/algebraic.h` and `src/exact/algebraic.c` provide two strict exact
domains over one bounded bigint/rational kernel: canonical real algebraic
values and canonical complex algebraic values. They underlie polynomial
`Solve`, exact characteristic roots, radical comparison, and resultant-closed
algebraic arithmetic.

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

The complex domain uses the same primitive irreducible positive-leading
minimal polynomial and a one-based ordinal among **all** roots. Real roots form
the leading increasing block; non-real roots follow in deterministic order
from pairwise-disjoint certified rational rectangles. A rectangle is an
executable certificate and does not participate in equality or hashing. Thus
re-isolation, reducible defining polynomials, allocator identity and source
precision cannot change value identity.

This is the bounded calculator analogue of the canonical identity used by
FLINT `qqbar`. The real type remains a strict subdomain and is lifted only by
an explicit API.

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

For complex roots, deterministic Durand--Kerner centres are candidate data
only. Every retained rectangle passes an exact Pellet--Rouche one-root test,
all rectangles are pairwise disjoint, and the exact Sturm count fixes the real
root block. No floating-point sample participates in root existence, identity,
ordering, equality, or hashing.

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

The complex domain additionally provides:

- isolation of every distinct complex root of a square-free defining
  polynomial;
- canonicalization of reducible input to the selected root's minimal
  polynomial and all-complex ordinal;
- exact rational rectangle accessors, structural validation, equality and
  stable hashing;
- explicit real-to-complex lifting and exact conjugation;
- resultant-closed addition, subtraction, multiplication, division and signed
  integer powers.

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

The scalar Q[x] factorizer now uses the complex API for irreducible factors of
degree three or more. Reader-facing `Solve` emits
`Root[List[a0,...,an],k]`, where coefficients are in increasing degree order
and `k` is one-based among all roots of the canonical minimal polynomial. This
completes bounded rational univariate `Solve` over the complex algebraic
closure. Existing real `Root` ordinals remain source-compatible because real
roots still occupy the leading increasing block.

`CharacteristicPolynomial[A,x]` uses the pivot-free Faddeev--LeVerrier
recurrence over the shared exact CAS, and `Eigenvalues[A]` solves that exact
polynomial while retaining algebraic multiplicity. Eigenvectors, Jordan form
and generalized eigenspaces are later milestones.

## Resource model

The algebraic context has independent ceilings for:

- exact limbs and exact-operation work;
- polynomial degree (default 32, hard ceiling 256);
- algebraic polynomial steps per public call;
- comparison/refinement rounds;
- coefficient-handle and polynomial-array metadata.

Cancellation reaches the Sturm, complex-certificate, factorization and
bigint/rational layers. Creation, all-root isolation, arithmetic and refinement
are transactional.
Allocation-failure injection walks every allocation in both a representative
single-root certificate and a quintic all-root isolation. It verifies that a
failed call publishes no object, the context still validates, a retry succeeds,
and tracked live heap returns to zero. Independent step and cancellation
ceilings cover both root count and all-root isolation.

Current reproducible evidence:

- `test_exact`: 79,159 checks, zero failures;
- `test_algebraic`: 157,155 checks, zero failures, including allocation-failure,
  timeout, cancellation, arbitrary-precision, minimal-polynomial selection,
  canonical equality/hash and retry coverage;
- current WSL GCC Release and ASan/UBSan/leak suites: 48/48 tests each;
- Ndless exact-number link probe: 68/68 public entry points, 17,680 bytes of
  exact-layer ARM text, 23,540-byte packaged probe;
- Ndless real/complex-algebraic link probe: 53/53 public entry points, 48,836
  bytes of algebraic-layer ARM text, 94,588-byte packaged probe;
- neither ARM probe retains a floating-point formatter, libm call, or
  soft-float helper.

These are host and link/package results. Physical CX II timing and peak-heap
acceptance remain separate evidence.

## Deliberate omissions

This is a canonical bounded algebraic closure, not a workstation-sized
algebraic-number package:

- no radical-to-algebraic lowering in the typed IR yet;
- no reader-facing arithmetic directly on serialized `Root[...]` objects yet;
- no eigenvectors, Jordan decomposition, or algebraic extension matrices;
- no asymptotically fast large-degree factor selection.

Every arithmetic call is subject to the documented degree, coefficient, step,
memory and cancellation ceilings. Exceeding one is a typed resource error and
does not publish a partial certificate.
