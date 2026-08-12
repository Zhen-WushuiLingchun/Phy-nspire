# Canonical Complex Algebraic Numbers — Design

**Status:** Accepted for implementation on 2026-08-12. This is a host-side
mathematical-kernel milestone; calculator upload and physical acceptance are
separate gates.

## Objective

Represent every bounded exact complex algebraic number by one canonical root
identity, not by an approximate complex value or by an incidental extension
tower. Complete exact univariate polynomial `Solve` over the algebraic closure
of the rationals and make exact matrix characteristic roots use the same
domain.

## Canonical identity

A `phy_complex_algebraic` owns:

1. a primitive, irreducible, positive-leading polynomial in `Z[x]`;
2. a one-based ordinal among **all** complex roots of that polynomial;
3. an exact rational isolating rectangle containing exactly that root.

Only the polynomial and ordinal participate in equality and hashing. The
rectangle is an executable certificate and may be refined without changing
identity.

The root order is deterministic and versioned by the reader-facing
`Root[p,k]` contract:

1. real roots first, in increasing order;
2. non-real roots use the canonical certified-rectangle order: disjoint real
   projections are ordered by real midpoint; an overlapping real projection
   is ordered by imaginary midpoint.

All comparisons are between exact rationals, and pairwise-disjoint rectangles
make the final midpoint tie impossible. The isolator follows one fixed dyadic
escalation schedule, independent of caller precision or construction history.
Real roots are counted by the Sturm layer and embedded in the leading block;
no floating point or approximate-zero test classifies a root as real. The
Wolfram development oracle checks the covered root-order examples, but is not
used as the definition of this calculator-specific total order.

This convention is backward compatible with every existing real-only
`Root[p,k]`: real roots retain the same ordinal because they remain the leading
ordered block.

## Exact certificate path

The existing exact Pellet--Rouche complex-root service is moved below the CAS
layer and reused as a root-certificate primitive. Candidate centres may come
from bounded Durand--Kerner iteration, but publication still requires exact,
pairwise-disjoint one-root rectangles.

Construction from a possibly reducible polynomial proceeds transactionally:

1. normalize to primitive positive-leading square-free `Z[x]`;
2. certify the selected complex rectangle;
3. split over `Q` with the existing bounded factorizer;
4. select the unique factor whose certified root intersects the selected
   rectangle, recursively, until irreducible;
5. isolate all roots of that minimal polynomial and assign its canonical
   ordinal;
6. link the value into its context only after all checks pass.

Ambiguous selection, exhausted resource limits, cancellation, or allocation
failure publishes no partial value.

## Arithmetic and conjugation

Addition, subtraction, multiplication, and division reuse the exact resultant
polynomials already used by the real algebraic domain. Operand rectangles are
combined with rational complex-ball arithmetic and refined until they select
one result root. The resultant is square-free factored over `Q`, the selected
irreducible factor becomes the result minimal polynomial, and the result is
re-canonicalized before publication. Integer powers are built from this closed
arithmetic.

Conjugation retains the same real minimal polynomial and selects the root in
the reflected rectangle `(Re, -Im)`. Real algebraic values map to themselves.

The existing `phy_real_algebraic` type remains a strict real subdomain. An
explicit lift produces the corresponding complex object; APIs never silently
discard a nonzero imaginary certificate.

## Reader and linear-algebra integration

`Root[List[coefficients],k]` denotes the canonical all-complex root. General
bounded rational univariate polynomial solving publishes every complex root,
with exact `Root` values rather than numeric balls. `Solve` continues to return
distinct solution rules.

Exact matrices gain:

- `CharacteristicPolynomial[A,x]`, computed as `det(x I - A)` through the
  shared exact CAS;
- `Eigenvalues[A]`, computed from the characteristic polynomial and retaining
  algebraic multiplicity.

Eigenvectors and Jordan structure are intentionally outside this milestone.

## Resource and evidence boundary

Degree, coefficient limbs, metadata, exact operations, root-isolation
escalations, and cancellation remain bounded. Exact symbolic objects never
depend on the certified numeric `N`/`NSolve` layer. Wolfram Language is a
development oracle only; the shipped calculator program has no Wolfram or
host runtime dependency.

`WolframLanguageContext` was attempted before implementation on 2026-08-12
and failed inside the MCP context helper. The reusable evaluator remained
available for explicit oracle queries. Host, sanitizer, and clean Ndless ARM
gates are mandatory before completion; no calculator acceptance is claimed
without a later physical run.
