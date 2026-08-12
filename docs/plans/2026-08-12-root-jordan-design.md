# Root arithmetic and exact Jordan linear algebra

Date: 2026-08-12

## Decision

Keep `Root[List[a0,...,an],k]` as the persisted and reader-facing algebraic
number representation.  Do not add a new serialized IR node kind.  Instead,
the scalar CAS recognizes an exact algebraic expression tree containing
integers, rationals, `I`, canonical or non-canonical `Root` calls, `Add`,
`Mul`, and integer `Pow`; evaluates it in one bounded
`phy_complex_algebraic` context; and publishes either a rational, `I`/`-I`,
or a canonical `Root` again.  Consequently a hand-written expression and the
same expression loaded from a notebook take the same path.

Malformed `Root` applications remain typed errors rather than opaque symbols:
the coefficient list must contain at least two exact integers, the leading
coefficient must be nonzero, and the one-based all-complex ordinal must select
a root of the defining polynomial.  Reducible and scaled defining
polynomials are normalized by the canonical algebraic layer.

The bridge is resource bounded by the active CAS step, cancellation, degree,
and memory limits.  It never compares floating-point approximations.

## Linear-algebra surface

- `Eigenspace[A,lambda]` returns a row-list basis of `ker(A-lambda I)`.
- `GeneralizedEigenspace[A,lambda]` returns a row-list basis of
  `ker((A-lambda I)^n)`.
- `Eigenvectors[A]` follows Wolfram's multiplicity convention: entries align
  with `Eigenvalues[A]`; a defective repeated eigenvalue is padded with exact
  zero vectors after its independent eigenvectors.
- `JordanDecomposition[A]` returns `{P,J}` with Jordan chains stored as the
  columns of `P`, standard superdiagonal ones in `J`, and the exact invariant
  `A.P == P.J` checked before publication.

The implementation uses the existing dynamic exact matrix and null-space
kernel.  It computes the nullity ladder of `(A-lambda I)^k`, selects chains
from longest to shortest by exact rank augmentation, and does not infer a
rank from a numerical tolerance.

## Oracle and acceptance

Wolfram Language is an oracle, not a runtime dependency.  The context helper
was attempted first on 2026-08-12 but failed inside Wolfram AgentTools
(`Kernel/Tools/Context.wl`); the evaluator remained available and established
the conventions above.  Host tests must include rational, complex, defective,
and non-rational algebraic matrices plus direct serialized-Root arithmetic.

Release, sanitizer, leak, and ARM link/device-probe gates remain mandatory.
Only after those pass may the program and tour notebook be committed, pushed,
and uploaded in one calculator CLI synchronization with SHA readback.
