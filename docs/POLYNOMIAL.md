# Exact polynomial foundation

The scalar CAS uses two coefficient domains with one mathematical contract:

- `Q[x]` coefficients are immutable exact IR atoms. Checked `int64` arithmetic
  is the fast path; arbitrary-precision integers/rationals are the promotion
  path.
- `F_p[x]` uses `src/cas/finite_poly.c`, a fixed-footprint dense kernel over a
  verified small prime `p <= 65521`.

The second domain is not a numerical approximation. Reduction modulo a chosen
prime is an exact algebraic homomorphism and is the next layer needed by
modular GCD and integer-polynomial factorization.

The reducer's primary multivariate path is now a distributed sparse
polynomial over exact rational IR coefficients. Every term carries a complete
exponent vector in deterministic lexicographic variable/monomial order. The
configured calculator profile admits up to 8 variables, 192 terms, and degree
48 in each variable; these are explicit resource ceilings rather than
semantic restrictions to a Kronecker image.

The sparse kernel implements exact addition, subtraction, multiplication,
monomial scaling, powers, multivariate division, recursive
content/primitive-part extraction, and a primitive pseudo-remainder GCD over
the recursively selected coefficient ring. A proposed GCD is accepted only
after exact division and multiplication reconstruct both original
polynomials. `Cancel` normalizes the denominator to a deterministic monic
form before publishing it.

The older bounded mixed-radix Kronecker candidate path is retained only as a
compatibility fallback when the sparse pass finds no cancellable divisor. It
is not the semantic multivariate GCD implementation, and its candidates still
require exact reconstruction before publication.

## Finite-field invariants

- the modulus is checked prime at context initialization;
- coefficients are canonical residues in `[0,p)`;
- zero has length zero and every nonzero polynomial has a nonzero leading
  coefficient;
- division, GCD, and factorization publish outputs only after complete
  success;
- GCDs and irreducible factors are monic;
- degree, step, and cancellation limits return typed errors;
- no heap allocation or floating-point operation is used.

The implemented kernel contains addition, subtraction, multiplication,
modular multiplication, derivative, Euclidean division, monic GCD with Bézout
certificate, modular exponentiation, square-free testing, and deterministic
Berlekamp factorization of monic square-free inputs. Berlekamp's nullspace is
row-reduced exactly in `F_p`; candidate splits are accepted only through exact
polynomial GCD, and the final factor product is recomputed before publication.

`phy_bigint_mod_u32` bridges an arbitrary-precision signed integer to a
Euclidean word remainder without first truncating it. This is the coefficient
map that later modular algorithms will use.

## General `Factor` architecture

The existing rational-root theorem path is complete only for its documented
small candidate set. General factorization over `Z[x]` requires:

1. primitive and square-free decomposition;
2. selection of a good prime and irreducible factorization in `F_p[x]`;
3. Hensel lifting to a certified coefficient bound;
4. exact Zassenhaus/van-Hoeij-style recombination and division checks.

All four steps are now connected for reader-facing univariate `Factor` through
degree 48. Rational inputs use the exact monic integer transform
`F(y)=D^n P(y/D)`. Candidate bipartitions receive a finite-field Bézout
certificate, are pair-Hensel-lifted until the modulus is beyond twice a
Landau--Mignotte coefficient bound, and are published only after exact
division in `Q[x]`. Recombination is complete within the documented
13-modular-factor/4,096-bipartition ceiling.

The square-free front end uses modular `gcd(F,F')`, multi-prime CRT
reconstruction, the same coefficient bound, and exact division of both `F`
and `F'`. Thus promoted coefficients do not force the algorithm back through
a coefficient-swelling rational Euclidean sequence.

This dependency order follows the mature separation visible in FLINT:
integer-polynomial factorization documents square-free decomposition,
finite-field factorization, Hensel lifting, and Zassenhaus recombination as
distinct layers, while the multivariate API exposes separately fallible Brown,
Hensel, subresultant, and Zippel GCD algorithms.

References:

- [FLINT integer-polynomial factorization](https://flintlib.org/doc/fmpz_poly_factor.html)
- [FLINT finite-field polynomial factorization](https://flintlib.org/doc/fmpz_mod_poly_factor.html)
- [FLINT multivariate integer polynomials](https://flintlib.org/doc/fmpz_mpoly.html)

## Remaining polynomial milestones

- faster van-Hoeij/LLL-style recombination beyond the bounded exhaustive
  4,096-partition lattice;
- a coefficient-generic subresultant path for arbitrary auxiliary GCDs, beyond
  the certified modular derivative-GCD and bounded cancellation paths;
- faster modular Brown/Zippel-style sparse GCD for inputs that exceed the
  calculator's deterministic primitive-PRS work ceiling;
- complete multivariate factorization; reader-facing `Factor` remains the
  exact bounded univariate `Q[x]` operation;
- multivariate and algebraic-extension partial fractions beyond the current
  exact univariate `Q[x]` `Apart`.
