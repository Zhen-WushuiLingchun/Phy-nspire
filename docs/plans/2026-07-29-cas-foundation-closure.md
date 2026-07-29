# CAS Foundation Closure Implementation Plan

> **For Codex:** Use the `executing-plans` workflow, do not delegate to
> subagents or Claude Code, and commit only independently verified milestones.

**Goal:** Close the exact algebraic-number, multivariate-polynomial,
calculus/special-function, and equation-system gaps before the next physical
CX II deployment.

**Architecture:** Keep the existing bounded exact-number layer and typed IR as
the only semantic path. Build coefficient-generic sparse multivariate
polynomials below reader commands, reuse the certified real-root isolator for
algebraic values, and make every new calculus or solving rule prove its result
by exact reconstruction, differentiation, substitution, or ideal reduction.

**Tech Stack:** ISO C11, base-2^32 exact integers/rationals, certified Sturm
isolation, sparse distributed polynomials, bounded subresultant and Buchberger
algorithms, typed IR, MathTree/nMarkdown, CTest, ASan/UBSan/leak checks, and
the Ndless r2022 ARM toolchain.

---

## Completion boundary

This plan closes a **bounded exact calculator CAS foundation**, not feature
parity with Mathematica, Maple, or SymPy.

The milestone is complete only when all of the following are reader-visible,
serializable, resource-bounded, and covered by positive and negative tests:

1. Real algebraic values support rational translate/scale/inverse and
   algebraic add, subtract, multiply, divide, and integer power.
2. Algebraic results carry a primitive irreducible defining polynomial and a
   certified isolating interval; equality is never decided numerically.
3. Sparse multivariate polynomials support any configured number of variables,
   deterministic monomial order, exact arithmetic, content/primitive part,
   division with remainder, and a general validated GCD path.
4. `Factor`, `Cancel`, and `Together` use the general multivariate kernel when
   the configured degree/term/step ceilings permit it.
5. The special-function registry covers the documented exact values,
   derivatives, symmetries, and recurrences for factorial/binomial,
   Pochhammer, polylogarithm, Bessel, Airy, and zeta families without fake
   decimal evaluation.
6. Exact rational functions integrate through Hermite reduction and partial
   fractions; bounded polynomial-times-elementary products use integration by
   parts. Every published primitive differentiates back exactly.
7. `Solve` handles exact linear systems, including inconsistent and
   underdetermined systems, and bounded zero-dimensional polynomial systems
   through a certified Gröbner/elimination path.
8. Unsupported positive-dimensional, transcendental, branch-undecidable, or
   resource-exhausted problems return typed results and leave the evaluator
   valid.

## Hard resource ceilings

Defaults may be tuned after host and device measurements, but the algorithms
must expose and enforce:

- algebraic resultant degree, coefficient-limb, isolation, and factor-count
  ceilings;
- multivariate variable, total-degree, per-variable degree, term, pair,
  monomial, coefficient-byte, and step ceilings;
- integration recursion, partial-fraction factor, and verification ceilings;
- equation variable, equation, monomial, S-pair, basis, and substitution
  ceilings.

No completion claim is allowed without timeout, cancellation,
allocation-failure, and transactional-retry tests at these boundaries.

## Task 1: Direct rational transforms of certified real algebraic values

**Files:**

- Modify: `include/phy/algebraic.h`
- Modify: `src/exact/algebraic.c`
- Modify: `tests/test_algebraic.c`
- Modify: `tests/device/algebraic_link_probe.c`
- Modify: `docs/ALGEBRAIC.md`

**Steps:**

1. Add failing tests for `alpha + r`, `r * alpha`, and `1 / alpha` using
   `sqrt(2)`, the negative root of `x^3+2`, rational roots, negative scales,
   arbitrary-precision rationals, alias-independent outputs, and zero inverse.
2. Transform defining polynomials exactly over `Z[x]`, clear content, force a
   positive leading coefficient, and transform the isolating interval using
   exact rational arithmetic.
3. Re-certify every result with the existing Sturm path before publishing it.
4. Sweep every injected allocation failure and verify the source value and
   algebraic context remain valid and a retry succeeds.
5. Retain the new public APIs in the ARM algebraic probe.

## Task 2: Bounded exact resultant kernel

**Files:**

- Create: `src/exact/algebraic_resultant.c`
- Create: `src/exact/algebraic_internal.h`
- Modify: `src/exact/algebraic.c`
- Modify: `CMakeLists.txt`
- Modify: `Makefile`
- Extend: `tests/test_algebraic.c`

**Steps:**

1. Add a dense univariate-polynomial coefficient type for Sylvester
   elimination whose scalar coefficients are native big integers.
2. Implement fraction-free Bareiss determinants with exact-division checks.
3. Generate and verify resultants for `p(y)` with `q(x-y)`, `q(x/y)y^n`, and
   reciprocal transforms.
4. Compare every resultant against deterministic small-polynomial golden
   cases and exact substitution identities.
5. Cover degree growth, coefficient growth, cancellation, timeout, and
   allocation rollback.

## Task 3: Real algebraic arithmetic and canonical equality

**Files:**

- Modify: `include/phy/algebraic.h`
- Modify: `src/exact/algebraic.c`
- Modify: `src/exact/algebraic_resultant.c`
- Modify: `src/cas/complex.c`
- Extend: `tests/test_algebraic.c`
- Extend: `tests/test_cas.c`
- Modify: `docs/ALGEBRAIC.md`

**Steps:**

1. Implement algebraic add/subtract/multiply/divide and bounded integer power
   by resultant construction.
2. Factor each resultant with the existing exact univariate factorizer and
   select exactly one factor/root using interval arithmetic plus Sturm counts.
3. Store the selected primitive irreducible factor and certified interval.
4. Define equality by the canonical polynomial/root certificate and comparison
   by interval refinement.
5. Add mixed rational/algebraic arithmetic and denominator rationalization.
6. Verify identities such as `sqrt(2)^2=2`,
   `(sqrt(2)+sqrt(3))^2=5+2 sqrt(6)`, inverse products, and cross-polynomial
   equality without floating-point sampling.

## Task 4: Sparse multivariate polynomial kernel

**Files:**

- Create: `src/cas/sparse_poly.h`
- Create: `src/cas/sparse_poly.c`
- Create: `tests/test_sparse_poly.c`
- Modify: `CMakeLists.txt`
- Modify: `Makefile`

**Steps:**

1. Define an immutable variable order and packed-or-dynamic exponent vector.
2. Canonicalize terms by monomial order, merge equal monomials, and remove
   exact-zero coefficients.
3. Implement add, subtract, multiply, monomial multiply, derivative,
   evaluation, leading term, and exact reconstruction to typed IR.
4. Implement multivariate division with remainder and verify
   `f = sum(q_i g_i) + r` exactly.
5. Exhaust small coefficient/exponent grids and cover malformed, limit,
   cancellation, and allocation-failure cases.

## Task 5: General bounded multivariate GCD

**Files:**

- Create: `src/cas/sparse_gcd.c`
- Modify: `src/cas/reduce.c`
- Extend: `tests/test_sparse_poly.c`
- Extend: `tests/test_cas.c`
- Modify: `docs/POLYNOMIAL.md`

**Steps:**

1. Implement coefficient content and primitive part recursively by the chosen
   main variable.
2. Implement subresultant/pseudo-remainder GCD for deterministic exact
   fallback.
3. Add modular evaluation/interpolation acceleration only after the exact
   fallback passes.
4. Normalize the GCD to a deterministic monic rational polynomial.
5. Accept a computed GCD only after exact division and reconstruction of both
   inputs.
6. Route `Cancel`, `Together`, and bounded multivariate `Factor` through this
   kernel; remove the Kronecker-image special-case claim once superseded.

## Task 6: Descriptor-driven special-function layer

**Files:**

- Create: `src/cas/special.c`
- Modify: `src/cas/cas_internal.h`
- Modify: `src/cas/engine.c`
- Modify: `src/cas/diff.c`
- Modify: `src/cas/simplify.c`
- Modify: `src/notebook/source.c`
- Modify: `src/notebook/palette.c`
- Modify: `src/render/ir_math_tree.cpp`
- Extend: `tests/test_cas.c`
- Extend: `tests/test_eval.c`

**Steps:**

1. Register `Factorial`, `Pochhammer`, `Binomial`, `PolyLog`,
   `BesselJ/Y/I/K`, `AiryAi/Bi`, and `Zeta` with arity/domain metadata.
2. Add only exact special values, parity/symmetry identities, derivatives,
   and recurrence rules whose preconditions are proved.
3. Preserve unproved calls as typed explicit functions.
4. Round-trip every head through source, IR, save/open, MathTree, and palette.

## Task 7: Exact rational and bounded elementary integration

**Files:**

- Modify: `src/cas/integrate.c`
- Modify: `src/cas/reduce.c`
- Extend: `tests/test_cas.c`
- Extend: `tests/test_eval.c`
- Modify: `docs/CAS.md`

**Steps:**

1. Add Hermite reduction for repeated rational factors.
2. Integrate the square-free remainder through exact partial fractions,
   logarithms, and real quadratic arctangent forms.
3. Add bounded integration by parts for polynomial times `Exp`, `Sin`, `Cos`,
   `Sinh`, `Cosh`, and `Log`.
4. Differentiate every candidate primitive and publish it only when exact
   simplification proves the original integrand.
5. Keep branch-sensitive or unsupported integrals explicit.

## Task 8: Exact linear equation systems

**Files:**

- Create: `src/cas/linear_solve.c`
- Modify: `src/cas/solve.c`
- Modify: `src/notebook/source.c`
- Extend: `tests/test_cas.c`
- Extend: `tests/test_eval.c`

**Steps:**

1. Parse `Solve[{eq1,...},{x1,...}]` into exact coefficient matrices.
2. Implement fraction-free elimination/RREF over exact rationals.
3. Return unique solution rules, parametric rules for underdetermined systems,
   and an empty list for proved inconsistency.
4. Reject nonlinear terms transactionally into the polynomial-system path.
5. Verify every returned rule by exact substitution into every equation.

## Task 9: Bounded zero-dimensional polynomial systems

**Files:**

- Create: `src/cas/groebner.c`
- Modify: `src/cas/solve.c`
- Extend: `tests/test_sparse_poly.c`
- Extend: `tests/test_cas.c`
- Extend: `tests/test_eval.c`
- Modify: `docs/CAS.md`

**Steps:**

1. Implement bounded Buchberger reduction with deterministic pair order and
   exact rational coefficients.
2. Verify every basis generator reduces the input ideal and every processed
   S-polynomial reduces to zero.
3. Detect positive-dimensional or resource-exhausted systems and return typed
   unsupported/resource results.
4. For zero-dimensional triangular bases, solve univariate factors with the
   existing certified solver and back-substitute exactly.
5. Substitute every solution tuple into every original equation before
   publishing it.

## Task 10: Release gates and geometry handoff

**Files:**

- Modify: `docs/CAS_FOUNDATION.md`
- Modify: `docs/CAS_ACCEPTANCE.md`
- Modify: `docs/ROADMAP.md`
- Modify: `docs/BUILD.md`
- Regenerate: `examples/phy-nspire-cas-tour.tns`

**Steps:**

1. Run the complete Windows strict suite.
2. Run WSL warning-as-error plus ASan/UBSan/leak suites.
3. Run exact/algebraic/CAS/evaluator ARM link probes, dependency scans,
   symbol report, and product size report.
4. Add tour cells covering every new positive class and representative typed
   negative/resource cases.
5. Push the verified commits.
6. Deploy main program and tour with CLI SHA-256 readback.
7. Record physical launch, timing, peak heap, cancellation, save/reopen/replay,
   and clean exit.
8. Resume differential geometry only after the handoff gate below passes.

## Differential-geometry handoff gate

Work may return to differential geometry, GR, and QFT when:

1. exact scalar arithmetic, algebraic values, multivariate polynomial GCD, and
   linear systems pass host and ARM gates;
2. tensor/geometry expressions can use the same scalar evaluator without a
   private fallback;
3. `D`, `Simplify`, `Cancel`, `Factor`, `Apart`, `Series`, `Limit`, and the
   documented `Integrate` subset pass the generated tour;
4. the device retains at least 3 MiB program headroom and representative
   algebraic/GCD cells stay within the measured heap ceiling;
5. no open correctness bug can silently change a tensor contraction,
   curvature component, Lie bracket, or QFT coefficient.

Special-function breadth and zero-dimensional polynomial systems may continue
in parallel with later physics only after their unfinished classes remain
explicitly typed and cannot be reached accidentally from geometry backends.
