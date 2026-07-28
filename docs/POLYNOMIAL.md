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
derivative, Euclidean division, monic GCD, modular exponentiation,
square-free testing, and deterministic Berlekamp factorization of monic
square-free inputs. Berlekamp's nullspace is row-reduced exactly in `F_p`;
candidate splits are accepted only through exact polynomial GCD, and the final
factor product is recomputed before publication.

`phy_bigint_mod_u32` bridges an arbitrary-precision signed integer to a
Euclidean word remainder without first truncating it. This is the coefficient
map that later modular algorithms will use.

## Why this layer precedes general `Factor`

The existing rational-root theorem path is complete only for its documented
small candidate set. General factorization over `Z[x]` requires:

1. primitive and square-free decomposition;
2. selection of a good prime and irreducible factorization in `F_p[x]`;
3. Hensel lifting to a certified coefficient bound;
4. exact Zassenhaus/van-Hoeij-style recombination and division checks.

Only step 2 is closed here. Reader-facing `Factor` is not widened until lifting
and recombination exist; returning a modular factor as though it were an
integer factor would be mathematically wrong.

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

- map arbitrary-precision primitive integer coefficients into good prime
  images;
- bounded Hensel lifting and exact recombination;
- sparse multivariate representation with explicit monomial order;
- content/primitive-part and a validated multivariate GCD algorithm;
- `Apart` after denominator factorization is complete.

